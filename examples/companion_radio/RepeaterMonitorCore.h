#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <time.h>

// Hardware-independent scheduling and wire-format helpers.
namespace MonitorCore {
constexpr uint32_t VALID_TIME = 1735689600UL;
constexpr size_t MAX_REPEATERS = 32;
enum Result : uint8_t { Empty, Checking, Ok, NoResponse, LoginFailed, SendFailed, Interrupted, NoContactSpace };
struct Reading { uint32_t day = 0, timestamp = 0; uint16_t millivolts = 0; uint8_t result = Empty; };
struct Entry {
  uint8_t key[32] = {};
  char name[33] = {};
  bool enabled = true;
  uint32_t scheduledDay = 0;
  Reading readings[7];
};
inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool parseKey(const char* text, uint8_t* key) {
  if (!text || strlen(text) != 64) return false;
  bool nonzero = false;
  for (int i = 0; i < 32; ++i) {
    int a = hexDigit(text[i * 2]), b = hexDigit(text[i * 2 + 1]);
    if (a < 0 || b < 0) return false;
    key[i] = (a << 4) | b;
    nonzero |= key[i] != 0;
  }
  return nonzero;
}
inline void formatKey(const uint8_t* key, char* text) {
  const char* hex = "0123456789ABCDEF";
  for (int i = 0; i < 32; ++i) { text[2*i] = hex[key[i] >> 4]; text[2*i+1] = hex[key[i] & 15]; }
  text[64] = 0;
}
// Gregorian civil date to days since 1970-01-01 (Howard Hinnant algorithm).
inline int32_t civilDay(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = y - era * 400;
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
inline uint32_t easternDay(uint32_t utc) {
  time_t t = utc;
  const tm calendar = *gmtime(&t);
  int y = calendar.tm_year + 1900;
  int march1 = civilDay(y, 3, 1), nov1 = civilDay(y, 11, 1);
  int marchSunday = 1 + (7 - (march1 + 4) % 7) % 7 + 7;
  int novSunday = 1 + (7 - (nov1 + 4) % 7) % 7;
  uint32_t start = civilDay(y, 3, marchSunday) * 86400UL + 7 * 3600;
  uint32_t end = civilDay(y, 11, novSunday) * 86400UL + 6 * 3600;
  return (utc - ((utc >= start && utc < end) ? 4 : 5) * 3600UL) / 86400UL;
}
inline uint32_t sunrise(uint32_t day) {
  // NOAA fractional-year equations, Bridgeport city centre, east-positive longitude.
  // https://gml.noaa.gov/grad/solcalc/solareqns.PDF
  time_t t = day * 86400UL + 12 * 3600;
  const tm calendar = *gmtime(&t);
  int y = calendar.tm_year + 1900;
  bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
  const double pi = 3.14159265358979323846;
  double gamma = 2 * pi / (leap ? 366 : 365) * calendar.tm_yday;
  double eq = 229.18 * (0.000075 + 0.001868*cos(gamma) - 0.032077*sin(gamma)
      - 0.014615*cos(2*gamma) - 0.040849*sin(2*gamma));
  double dec = 0.006918 - 0.399912*cos(gamma) + 0.070257*sin(gamma)
      - 0.006758*cos(2*gamma) + 0.000907*sin(2*gamma)
      - 0.002697*cos(3*gamma) + 0.00148*sin(3*gamma);
  double lat = 41.18 * pi / 180;
  double ha = acos(cos(90.833*pi/180)/(cos(lat)*cos(dec)) - tan(lat)*tan(dec)) * 180/pi;
  double minutes = 720 - 4 * (-73.19 + ha) - eq;
  return day * 86400UL + (uint32_t)lround(minutes * 60);
}
inline bool retained(uint32_t day, uint32_t today) { return day && day <= today && today - day < 7; }
inline bool due(const Entry& e, uint32_t utc) {
  if (utc < VALID_TIME || !e.enabled) return false;
  uint32_t day = easternDay(utc);
  return e.scheduledDay < day && utc >= sunrise(day);
}
inline bool elapsed(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
inline uint32_t timeout(uint32_t estimate) { return estimate < 30000 ? 30000 : (estimate > 180000 ? 180000 : estimate); }
inline bool loginOK(const uint8_t* data, size_t len) {
  // Decryption preserves zero padding to 16-byte blocks. A PATH response
  // removes its variable-length path prefix but still retains that padding.
  return (len >= 6 && data[4] == 'O' && data[5] == 'K') || (len >= 13 && data[4] == 0);
}
inline bool voltage(const uint8_t* data, size_t len, uint32_t tag, uint16_t& mv) {
  if (len < 52) return false; // basic legacy repeater stats; reject login/short payloads
  uint32_t received = (uint32_t)data[0] | (uint32_t)data[1]<<8 | (uint32_t)data[2]<<16 | (uint32_t)data[3]<<24;
  if (received != tag) return false;
  mv = (uint16_t)data[4] | (uint16_t)data[5]<<8;
  return true;
}
inline uint32_t crc32(const char* data, size_t len) {
  uint32_t crc = ~0UL;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint8_t)data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}
inline bool validSnapshot(const uint32_t header[4], const char* payload, size_t len) {
  return header[0] == 0x4D4F4E31 && header[2] == len && len <= 65000
      && header[3] == crc32(payload, len);
}
inline const char* resultName(uint8_t r) {
  const char* names[] = {"not_checked", "checking", "ok", "no_response", "login_failed", "send_failed", "interrupted", "contacts_full"};
  return r <= NoContactSpace ? names[r] : "invalid";
}
}
