#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "RepeaterMonitor.h"
#endif
#include "MyMesh.h"

#include <Arduino.h> // needed for PlatformIO
#include <Mesh.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if CMESH_BOT_ENABLED
#include "BotCommandRegistry.h"
#include "BotCommands.h"
#include "BotPathLookup.h"
#include "BotPolicy.h"
#include "BotPrefs.h"
#include "EmergencyForwarder.h"
#include "FirmwareBot.h"
#include "KnownBotRegistry.h"
#endif

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE_KEY       54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_GET_ALLOWED_REPEAT_FREQ   60
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SEND_CHANNEL_DATA         62
#define CMD_SET_DEFAULT_FLOOD_SCOPE   63
#define CMD_GET_DEFAULT_FLOOD_SCOPE   64
#define CMD_SEND_RAW_PACKET           65

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QUERY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25
#define RESP_ALLOWED_REPEAT_FREQ      26
#define RESP_CODE_CHANNEL_DATA_RECV   27
#define RESP_CODE_DEFAULT_FLOOD_SCOPE 28

#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

#if CMESH_BOT_ENABLED
static size_t botBoundedStrLen(const char *value, size_t max_len) {
  size_t len = 0;
  while (value && len < max_len && value[len] != 0) len++;
  return len;
}

static void botCopyString(char *dest, size_t dest_len, const char *src) {
  if (!dest || dest_len == 0) return;
  size_t len = botBoundedStrLen(src, dest_len - 1);
  if (len > 0) memcpy(dest, src, len);
  dest[len] = 0;
}

static bool botFormatResponseForChannelKind(BotChannelKind channel_kind, const char *text, size_t text_len, char *output,
                                            size_t output_len, size_t *written) {
  BotWriteResult result = FirmwareBot::writeResponseForChannel(
      channel_kind, BotPolicy::isPrefixlessCommandAllowed(channel_kind), text, text_len, output, output_len, written);
  return result != BOT_WRITE_NO_SPACE && written && *written > 0;
}

static bool botFormatResponseForChannel(const BotMessage &message, const char *text, size_t text_len, char *output,
                                        size_t output_len, size_t *written) {
  return botFormatResponseForChannelKind(message.channel_kind, text, text_len, output, output_len, written);
}

static bool botParseU32(const char *text, uint32_t *value, const char **end_out) {
  if (!text || !value || !isdigit((unsigned char)text[0])) return false;
  uint32_t parsed = 0;
  while (isdigit((unsigned char)*text)) {
    uint32_t next = parsed * 10UL + (uint32_t)(*text - '0');
    if (next < parsed) return false;
    parsed = next;
    text++;
  }
  *value = parsed;
  if (end_out) *end_out = text;
  return true;
}

static void botSkipSpaces(const char **text) {
  while (text && *text && **text == ' ') (*text)++;
}

static bool botReadToken(const char **text, char *output, size_t output_len) {
  if (!text || !*text || !output || output_len == 0) return false;
  botSkipSpaces(text);
  const char *start = *text;
  size_t len = 0;
  while (start[len] != 0 && start[len] != ' ') len++;
  if (len == 0 || len + 1 > output_len) return false;
  memcpy(output, start, len);
  output[len] = 0;
  *text = start + len;
  return true;
}

static bool botNoMoreTokens(const char *text) {
  botSkipSpaces(&text);
  return text && *text == 0;
}

static int botHexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static bool botTraceFlagForHashSize(uint8_t hash_size, uint8_t *flags) {
  if (!flags) return false;
  if (hash_size == 1) {
    *flags = 0;
    return true;
  }
  if (hash_size == 2) {
    *flags = 1;
    return true;
  }
  if (hash_size == 4) {
    *flags = 2;
    return true;
  }
  return false;
}

static uint8_t botConfiguredTraceHashSize(uint8_t path_hash_mode) {
  if (path_hash_mode == 0) return 1;
  if (path_hash_mode == 1) return 2;
  return 4;
}

static uint8_t botTraceHashSize(uint8_t flags) {
  return (uint8_t)(1U << (flags & 0x03));
}

static bool botTracePathShapeValid(uint8_t path_len, uint8_t flags) {
  uint8_t hash_size = botTraceHashSize(flags);
  return path_len <= BOT_MAX_PATH_BYTES && path_len + 9 <= MAX_PACKET_PAYLOAD &&
         (path_len % hash_size) == 0 && (path_len / hash_size) <= MAX_PATH_SIZE;
}

static void botCopyReversedPath(uint8_t *dest, const uint8_t *src, uint8_t hop_count, uint8_t hash_size) {
  for (uint8_t i = 0; i < hop_count; i++) {
    memcpy(&dest[i * hash_size], &src[(hop_count - i - 1) * hash_size], hash_size);
  }
}

static bool botParseTraceHexPath(const BotCommand &command, uint8_t flags, uint8_t path[BOT_MAX_PATH_BYTES],
                                 uint8_t *path_len) {
  if (!path || !path_len || command.args_len == 0) return false;

  uint8_t hash_size = botTraceHashSize(flags);
  bool comma = false;
  for (size_t i = 0; i < command.args_len; i++) {
    if (command.args[i] == ',') comma = true;
  }

  uint8_t parsed_len = 0;
  if (comma) {
    size_t pos = 0;
    while (pos < command.args_len) {
      if (pos + (hash_size * 2) > command.args_len) return false;
      for (uint8_t i = 0; i < hash_size; i++) {
        int high = botHexValue(command.args[pos + i * 2]);
        int low = botHexValue(command.args[pos + i * 2 + 1]);
        if (high < 0 || low < 0 || parsed_len >= BOT_MAX_PATH_BYTES) return false;
        path[parsed_len++] = (uint8_t)((high << 4) | low);
      }
      pos += hash_size * 2;
      if (pos == command.args_len) break;
      if (command.args[pos] != ',') return false;
      pos++;
      if (pos == command.args_len) return false;
    }
  } else {
    if ((command.args_len & 1) != 0 || command.args_len / 2 > BOT_MAX_PATH_BYTES) return false;
    parsed_len = (uint8_t)(command.args_len / 2);
    for (uint8_t i = 0; i < parsed_len; i++) {
      int high = botHexValue(command.args[i * 2]);
      int low = botHexValue(command.args[i * 2 + 1]);
      if (high < 0 || low < 0) return false;
      path[i] = (uint8_t)((high << 4) | low);
    }
  }

  if (!botTracePathShapeValid(parsed_len, flags)) return false;
  *path_len = parsed_len;
  return true;
}

static BotCommandResult botCommandResult(BotCommandResultCode code, size_t text_len) {
  BotCommandResult result = { code, text_len };
  return result;
}

static BotCommandResult botWriteText(char *output, size_t output_len, const char *text) {
  if (!output || output_len == 0) return botCommandResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  size_t text_len = botBoundedStrLen(text, BOT_MAX_RESPONSE_LEN + 1);
  size_t copy_len = text_len;
  if (copy_len + 1 > output_len) copy_len = output_len - 1;
  if (copy_len > 0) memcpy(output, text, copy_len);
  output[copy_len] = 0;
  return botCommandResult(copy_len < text_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, copy_len);
}

static BotCommandResult botWriteFormatted(char *output, size_t output_len, const char *format, ...) {
  if (!output || output_len == 0) return botCommandResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  va_list args;
  va_start(args, format);
  int written = vsnprintf(output, output_len, format, args);
  va_end(args);
  if (written < 0) {
    output[0] = 0;
    return botCommandResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  }
  size_t actual = (size_t)written;
  if (actual >= output_len) actual = output_len - 1;
  return botCommandResult((size_t)written >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}

static bool botParsePubKeyPrefixHex(const BotCommand &command, uint8_t prefix[PUB_KEY_SIZE], uint8_t *prefix_len) {
  if (!prefix || !prefix_len || command.args_len == 0 || (command.args_len & 1) != 0) return false;
  size_t byte_len = command.args_len / 2;
  if (byte_len < 1 || byte_len > PUB_KEY_SIZE) return false;
  for (size_t i = 0; i < byte_len; i++) {
    int high = botHexValue(command.args[i * 2]);
    int low = botHexValue(command.args[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    prefix[i] = (uint8_t)((high << 4) | low);
  }
  *prefix_len = (uint8_t)byte_len;
  return true;
}

static void botFormatKeyPrefixHex(const uint8_t *key, char *output, size_t output_len) {
  static const char hex[] = "0123456789abcdef";
  if (!output || output_len == 0) return;
  if (!key || output_len < BOT_SENDER_KEY_PREFIX_LEN * 2 + 1) {
    output[0] = 0;
    return;
  }
  for (size_t i = 0; i < BOT_SENDER_KEY_PREFIX_LEN; i++) {
    output[i * 2] = hex[key[i] >> 4];
    output[i * 2 + 1] = hex[key[i] & 0x0F];
  }
  output[BOT_SENDER_KEY_PREFIX_LEN * 2] = 0;
}

static void botCopyContactName(const ContactInfo &contact, char *output, size_t output_len) {
  if (!output || output_len == 0) return;
  size_t len = botBoundedStrLen(contact.name, sizeof(contact.name));
  if (len == 0) {
    botCopyString(output, output_len, "contact");
    return;
  }
  if (len + 1 > output_len) len = output_len - 1;
  memcpy(output, contact.name, len);
  output[len] = 0;
}

static void botFormatQuarters(int8_t quarters, char *output, size_t output_len) {
  if (!output || output_len == 0) return;
  int value = quarters;
  const char *sign = value < 0 ? "-" : "";
  if (value < 0) value = -value;
  snprintf(output, output_len, "%s%d.%02d", sign, value / 4, (value % 4) * 25);
}

static void botAppendHex(char *output, size_t output_len, size_t *pos, const uint8_t *data, size_t data_len) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < data_len; i++) {
    if (*pos + 2 < output_len) {
      output[*pos] = hex[data[i] >> 4];
      output[*pos + 1] = hex[data[i] & 0x0F];
    }
    *pos += 2;
  }
  if (output_len > 0) output[*pos < output_len ? *pos : output_len - 1] = 0;
}

static void botAppendLiteral(char *output, size_t output_len, size_t *pos, const char *text) {
  if (!output || output_len == 0 || !pos || !text) return;
  for (size_t i = 0; text[i] != 0; i++) {
    if (*pos + 1 < output_len) output[*pos] = text[i];
    (*pos)++;
  }
  output[*pos < output_len ? *pos : output_len - 1] = 0;
}

static void botAppendPathHops(char *output, size_t output_len, size_t *pos, const uint8_t *path, uint8_t hash_size,
                              uint8_t hash_count) {
  for (uint8_t hop = 0; hop < hash_count; hop++) {
    if (hop != 0) botAppendLiteral(output, output_len, pos, " -> ");
    botAppendHex(output, output_len, pos, &path[(size_t)hop * hash_size], hash_size);
  }
}

static size_t botFormatTraceSent(char *output, size_t output_len, uint8_t hop_count) {
  if (!output || output_len == 0) return 0;
  int written = hop_count == 0 ? snprintf(output, output_len, "Trace sent on direct zero-hop route")
                               : snprintf(output, output_len, "Trace sent on %u-hop route", (unsigned)hop_count);
  if (written < 0) {
    output[0] = 0;
    return 0;
  }
  return botBoundedStrLen(output, output_len);
}

#endif

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

bool MyMesh::Frame::isChannelMsg() const {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3 ||
         buf[0] == RESP_CODE_CHANNEL_DATA_RECV;
}

void MyMesh::addToOfflineQueue(const uint8_t frame[], int len) {
  if (offline_queue_len >= OFFLINE_QUEUE_SIZE) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int pos = 0;
    while (pos < offline_queue_len) {
      if (offline_queue[pos].isChannelMsg()) {
        for (int i = pos; i < offline_queue_len - 1; i++) { // delete oldest channel msg from queue
          offline_queue[i] = offline_queue[i + 1];
        }
        MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
        offline_queue[offline_queue_len - 1].len = len;
        memcpy(offline_queue[offline_queue_len - 1].buf, frame, len);
        return;
      }
      pos++;
    }
    MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
  } else {
    offline_queue[offline_queue_len].len = len;
    memcpy(offline_queue[offline_queue_len].buf, frame, len);
    offline_queue_len++;
  }
}

int MyMesh::getFromOfflineQueue(uint8_t frame[]) {
  if (offline_queue_len > 0) {         // check offline queue
    size_t len = offline_queue[0].len; // take from top of queue
    memcpy(frame, offline_queue[0].buf, len);

    offline_queue_len--;
    for (int i = 0; i < offline_queue_len; i++) { // delete top item from queue
      offline_queue[i] = offline_queue[i + 1];
    }
    return len;
  }
  return 0; // queue is empty
}

float MyMesh::getAirtimeBudgetFactor() const {
  return _prefs.airtime_factor;
}

int MyMesh::getInterferenceThreshold() const {
  return 0; // disabled for now, until currentRSSI() problem is resolved
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.5f);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.2f);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint8_t MyMesh::getExtraAckTransmitCount() const {
  return _prefs.multi_acks;
}

void MyMesh::logRx(mesh::Packet* packet, int len, float score) {
  (void)len;
  (void)score;
#if CMESH_BOT_ENABLED
  // Runs before duplicate filtering or delayed processing, with this packet's RSSI.
  // Flood paths record traversed repeaters; direct paths describe the route ahead.
  if (!packet || !packet->isRouteFlood() || packet->path_len > 0xFF ||
      !mesh::Packet::isValidPathLen((uint8_t)packet->path_len) ||
      packet->getPathHashCount() == 0) return;
  const uint8_t hash_size = packet->getPathHashSize();
  const uint8_t* last_hash = packet->path + (packet->getPathHashCount() - 1) * hash_size;
  ContactsIterator iter;
  ContactInfo candidate;
  uint8_t matched_key[PUB_KEY_SIZE];
  unsigned matches = 0;
  while (iter.hasNext(this, candidate)) {
    if (candidate.type != ADV_TYPE_REPEATER ||
        memcmp(candidate.id.pub_key, last_hash, hash_size) != 0) continue;
    if (++matches > 1) return; // Do not guess when short hashes collide.
    memcpy(matched_key, candidate.id.pub_key, sizeof(matched_key));
  }
  if (matches == 1) {
    float rssi = radio_driver.getLastRSSI();
    if (rssi > 32767.0f) rssi = 32767.0f;
    if (rssi < -32768.0f) rssi = -32768.0f;
    recordBotNeighbor(matched_key, (int16_t)rssi, (int8_t)(packet->getSNR() * 4));
  }
#else
  (void)packet;
#endif
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }

  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }

  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE); // delete from storage
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  // BaseChatMesh calls this after signature validation and its advert replay check.
  if (repeaterMonitor && contact.type == ADV_TYPE_REPEATER)
    repeaterMonitor->onVerifiedAdvert(contact.id.pub_key, contact.last_advert_timestamp, path_len);
#endif
#if CMESH_BOT_ENABLED
  // Zero-hop adverts also identify neighbors, including newly discovered repeaters.
  if (contact.type == ADV_TYPE_REPEATER && (path_len & 63) == 0) {
    float rssi = radio_driver.getLastRSSI();
    if (rssi > 32767.0f) rssi = 32767.0f;
    if (rssi < -32768.0f) rssi = -32768.0f;
    int8_t snr_q = (int8_t)(radio_driver.getLastSNR() * 4);
    recordBotNeighbor(contact.id.pub_key, (int16_t)rssi, snr_q);
  }
#endif

  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  } else {
#ifdef DISPLAY_CLASS
    if (_ui) _ui->notify(UIEventType::newContactMessage);
#endif
  }

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      // NOTE: the same ACK can be received multiple times!
#if CMESH_BOT_ENABLED
      uint32_t received_ack = 0;
      memcpy(&received_ack, data, sizeof(received_ack));
      if (pending_bot_dm_ack.active &&
          pending_bot_dm_ack.expected_ack == received_ack) {
        pending_bot_dm_ack.active = false;
      }
#endif
      expected_ack_table[i].ack = 0; // clear expected hash, now that we have received ACK
      return expected_ack_table[i].contact;
    }
  }
  return checkConnectionsAck(data);
}

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

#ifdef DISPLAY_CLASS
  // we only want to show text messages on display, not cli data
  bool should_display = txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN;
  if (should_display && _ui) {
    _ui->newMsg(path_len, from.name, text, offline_queue_len);
    if (!_serial->isConnected()) {
      _ui->notify(UIEventType::contactMessage);
    }
  }
#endif
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // REVISIT: try to determine which Region (from transport_codes[1]) that Sender is indicating for replies/responses
  //    if unknown, fallback to finding Region from transport_codes[0], the 'scope' used by Sender
  return false;
}

bool MyMesh::allowPacketForward(const mesh::Packet* packet) {
  return _prefs.client_repeat != 0;
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
  }
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);
#if CMESH_BOT_ENABLED
  observeBotDirectMessage(from, sender_timestamp, from.id.pub_key, BOT_SENDER_KEY_PREFIX_LEN, text, pkt);
#endif
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  if (repeaterMonitor && repeaterMonitor->onCommandResponse(from, text)) return;
#endif
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);
#if CMESH_BOT_ENABLED
  observeBotDirectMessage(from, sender_timestamp, sender_prefix, 4, text, pkt);
#endif
}

#if CMESH_BOT_ENABLED
void MyMesh::applyBotPrefs() {
  BotPrefsCodec::validate(bot_prefs);
  KnownBotRegistry::clear(known_bot_entries, BOT_KNOWN_BOT_SLOTS);
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    if (bot_prefs.known_bots[i].active) {
      KnownBotRegistry::add(known_bot_entries, BOT_KNOWN_BOT_SLOTS, bot_prefs.known_bots[i].key_prefix,
                            bot_prefs.known_bots[i].flags, bot_prefs.known_bots[i].label);
    }
  }
  if (bot_prefs.enabled) {
    scheduleBotLocalAdvert(bot_prefs.local_advert_interval_ms ? BOT_PREFS_INITIAL_LOCAL_ADVERT_MILLIS : 0);
    scheduleBotFloodAdvert(bot_prefs.flood_advert_interval_ms ? BOT_PREFS_INITIAL_FLOOD_ADVERT_MILLIS : 0);
  } else {
    scheduleBotLocalAdvert(0);
    scheduleBotFloodAdvert(0);
  }
}

bool MyMesh::saveBotPrefs() {
  BotPrefsCodec::validate(bot_prefs);
  bool success = _store->saveBotPrefs(bot_prefs);
  if (!success) bot_prefs.prefs_save_failures++;
  return success;
}

static void printBotPrefsSaveResult(const char *success_message, bool saved) {
  Serial.println(saved ? success_message : "  Error: bot prefs save failed");
}

void MyMesh::printBotPrefs() {
  Serial.printf("  > bot %s\n", bot_prefs.enabled ? "enabled" : "disabled");
  Serial.printf("  > channels bot=%s testing=%s emergency=%s public=%s\n", bot_prefs.bot_channel,
                bot_prefs.testing_channel, bot_prefs.emergency_channel, bot_prefs.public_channel);
  Serial.printf("  > advert local=%lu flood=%lu\n", (unsigned long)bot_prefs.local_advert_interval_ms,
                (unsigned long)bot_prefs.flood_advert_interval_ms);
}

bool MyMesh::handleBotCLI(const char *args) {
  if (!args) return false;
  botSkipSpaces(&args);
  if (*args == 0) {
    printBotPrefs();
    return true;
  }
  if (strcmp(args, "enable") == 0) {
    bot_prefs.enabled = true;
    applyBotPrefs();
    printBotPrefsSaveResult("  > bot enabled", saveBotPrefs());
    return true;
  }
  if (strcmp(args, "disable") == 0) {
    bot_prefs.enabled = false;
    memset(&pending_bot_dm_ack, 0, sizeof(pending_bot_dm_ack));
    applyBotPrefs();
    printBotPrefsSaveResult("  > bot disabled", saveBotPrefs());
    return true;
  }
  if (strcmp(args, "channels") == 0) {
    Serial.printf("  > %s %s %s %s\n", bot_prefs.bot_channel, bot_prefs.testing_channel,
                  bot_prefs.emergency_channel, bot_prefs.public_channel);
    return true;
  }
  if (memcmp(args, "channels ", 9) == 0) {
    const char *pos = args + 9;
    char bot[BOT_MAX_CHANNEL_NAME_LEN + 1];
    char testing[BOT_MAX_CHANNEL_NAME_LEN + 1];
    char emergency[BOT_MAX_CHANNEL_NAME_LEN + 1];
    char public_name[BOT_MAX_CHANNEL_NAME_LEN + 1];
    BotPrefs updated = bot_prefs;
    if (botReadToken(&pos, bot, sizeof(bot)) && botReadToken(&pos, testing, sizeof(testing)) &&
        botReadToken(&pos, emergency, sizeof(emergency)) && botReadToken(&pos, public_name, sizeof(public_name)) &&
        botNoMoreTokens(pos) && BotPrefsCodec::channelNameValid(bot, false) &&
        BotPrefsCodec::channelNameValid(testing, false) && BotPrefsCodec::channelNameValid(emergency, false) &&
        BotPrefsCodec::channelNameValid(public_name, true)) {
      botCopyString(updated.bot_channel, sizeof(updated.bot_channel), bot);
      botCopyString(updated.testing_channel, sizeof(updated.testing_channel), testing);
      botCopyString(updated.emergency_channel, sizeof(updated.emergency_channel), emergency);
      botCopyString(updated.public_channel, sizeof(updated.public_channel), public_name);
      if (BotPrefsCodec::channelConfigValid(updated)) {
        bot_prefs = updated;
        printBotPrefsSaveResult("  > bot channels saved", saveBotPrefs());
      } else {
        Serial.println("  Error: duplicate bot channel names");
      }
    } else {
      Serial.println("  Error: usage bot channels <bot> <testing> <emergency> <public>");
    }
    return true;
  }
  if (memcmp(args, "advert ", 7) == 0) {
    const char *pos = args + 7;
    uint32_t local = 0;
    uint32_t flood = 0;
    if (botParseU32(pos, &local, &pos)) {
      botSkipSpaces(&pos);
      if (botParseU32(pos, &flood, &pos) && *pos == 0 && local <= BOT_PREFS_MAX_ADVERT_MILLIS &&
          flood <= BOT_PREFS_MAX_ADVERT_MILLIS) {
        bot_prefs.local_advert_interval_ms = local;
        bot_prefs.flood_advert_interval_ms = flood;
        applyBotPrefs();
        printBotPrefsSaveResult("  > bot advert saved", saveBotPrefs());
      } else {
        Serial.println("  Error: usage bot advert <local_ms> <flood_ms>");
      }
    } else {
      Serial.println("  Error: usage bot advert <local_ms> <flood_ms>");
    }
    return true;
  }
  if (strcmp(args, "known list") == 0) {
    char hex[BOT_SENDER_KEY_PREFIX_LEN * 2 + 1];
    for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
      if (!bot_prefs.known_bots[i].active) continue;
      BotPrefsCodec::formatKeyPrefixHex(bot_prefs.known_bots[i].key_prefix, hex, sizeof(hex));
      Serial.printf("  > %s %s flags=%u\n", hex, bot_prefs.known_bots[i].label,
                    (unsigned)bot_prefs.known_bots[i].flags);
    }
    return true;
  }
  if (memcmp(args, "known add ", 10) == 0) {
    const char *pos = args + 10;
    char key_hex[BOT_SENDER_KEY_PREFIX_LEN * 2 + 1];
    char label[BOT_KNOWN_BOT_LABEL_LEN];
    label[0] = 0;
    if (botReadToken(&pos, key_hex, sizeof(key_hex))) {
      bool have_label = botReadToken(&pos, label, sizeof(label));
      uint8_t key[BOT_SENDER_KEY_PREFIX_LEN];
      if (botNoMoreTokens(pos) && (!have_label || label[0] != 0) && BotPrefsCodec::parseKeyPrefixHex(key_hex, key) &&
          BotPrefsCodec::addKnownBot(bot_prefs, key, BOT_KNOWN_BOT_FLAG_SUPPRESS_NORMAL, have_label ? label : "bot")) {
        applyBotPrefs();
        printBotPrefsSaveResult("  > known bot saved", saveBotPrefs());
      } else {
        Serial.println("  Error: known bot table full or invalid key");
      }
    } else {
      Serial.println("  Error: usage bot known add <hex-prefix> [label]");
    }
    return true;
  }
  if (memcmp(args, "known remove ", 13) == 0) {
    const char *pos = args + 13;
    char key_hex[BOT_SENDER_KEY_PREFIX_LEN * 2 + 1];
    uint8_t key[BOT_SENDER_KEY_PREFIX_LEN];
    if (botReadToken(&pos, key_hex, sizeof(key_hex)) && botNoMoreTokens(pos) &&
        BotPrefsCodec::parseKeyPrefixHex(key_hex, key) && BotPrefsCodec::removeKnownBot(bot_prefs, key)) {
      applyBotPrefs();
      printBotPrefsSaveResult("  > known bot removed", saveBotPrefs());
    } else {
      Serial.println("  Error: known bot not found");
    }
    return true;
  }
  if (strcmp(args, "commands") == 0) {
    for (size_t i = 0; i < BotCommandRegistry::commandCount(); i++) {
      const BotCommandMetadata *command = BotCommandRegistry::commandAt(i);
      if (!command || command->visibility != BOT_COMMAND_VISIBILITY_DISCOVERABLE) continue;
      Serial.printf("  > %s %s\n", command->name,
                    BotPrefsCodec::commandEnabled(bot_prefs, command->id) ? "enabled" : "disabled");
    }
    return true;
  }
  if (memcmp(args, "commands enable ", 16) == 0 || memcmp(args, "commands disable ", 17) == 0) {
    bool enable = memcmp(args, "commands enable ", 16) == 0;
    const char *name = args + (enable ? 16 : 17);
    BotCommandId command_id;
    if (BotPrefsCodec::commandIdForName(name, &command_id)) {
      BotPrefsCodec::setCommandEnabled(bot_prefs, command_id, enable);
      if (saveBotPrefs()) {
        Serial.printf("  > command %s %s\n", BotPrefsCodec::commandName(command_id), enable ? "enabled" : "disabled");
      } else {
        Serial.println("  Error: bot prefs save failed");
      }
    } else {
      Serial.println("  Error: unknown bot command");
    }
    return true;
  }
  if (strcmp(args, "stats") == 0) {
    Serial.printf("  > observed=%lu ignored=%lu eligible=%lu sent=%lu failed=%lu emergency=%lu/%lu\n",
                  (unsigned long)bot_stats.observed_messages, (unsigned long)bot_stats.ignored_messages,
                  (unsigned long)bot_stats.eligible_messages, (unsigned long)bot_stats.sent_messages,
                  (unsigned long)bot_stats.send_failures,
                  (unsigned long)bot_stats.emergency_forwards, (unsigned long)bot_stats.emergency_forward_failures);
    Serial.printf("  > prefs load_failures=%lu save_failures=%lu\n", (unsigned long)bot_prefs.prefs_load_failures,
                  (unsigned long)bot_prefs.prefs_save_failures);
    return true;
  }
  if (strcmp(args, "save") == 0) {
    Serial.println(saveBotPrefs() ? "  > bot prefs saved" : "  Error: bot prefs save failed");
    return true;
  }
  return false;
}

void MyMesh::observeBotDirectMessage(const ContactInfo &from, uint32_t sender_timestamp, const uint8_t *sender_prefix,
                                     size_t sender_prefix_len, const char *text, const mesh::Packet *packet) {
  BotMessage message;
  memset(&message, 0, sizeof(message));
  message.channel_kind = BotPolicy::classifyChannel(NULL, 0, true, bot_prefs);
  StrHelper::strzcpy(message.sender_name, from.name, sizeof(message.sender_name));
  size_t prefix_len = sender_prefix_len;
  if (prefix_len > sizeof(message.sender_key_prefix)) prefix_len = sizeof(message.sender_key_prefix);
  if (sender_prefix && prefix_len > 0) memcpy(message.sender_key_prefix, sender_prefix, prefix_len);
  message.sender_key_prefix_len = prefix_len;
  message.sender_timestamp = sender_timestamp;
  message.received_at_timestamp = getRTCClock()->getCurrentTime();
  message.packet_snr_quarters = packet ? (int8_t)(packet->getSNR() * 4) : (int8_t)0;
  if (packet && packet->isRouteFlood() && packet->path_len <= 0xFF && mesh::Packet::isValidPathLen((uint8_t)packet->path_len)) {
    message.path_len = (uint8_t)packet->path_len;
    message.path_hash_size = packet->getPathHashSize();
    message.path_hash_count = packet->getPathHashCount();
    message.path = packet->path;
    message.path_is_inbound = true;
  } else if (from.out_path_len != OUT_PATH_UNKNOWN && mesh::Packet::isValidPathLen(from.out_path_len)) {
    message.path_len = from.out_path_len;
    message.path_hash_size = (from.out_path_len >> 6) + 1;
    message.path_hash_count = from.out_path_len & 63;
    message.path = from.out_path;
  } else {
    message.path_len = 0;
    message.path_hash_size = botConfiguredTraceHashSize(_prefs.path_hash_mode);
    message.path_hash_count = 0;
    message.path = NULL;
  }
  message.text_truncated = FirmwareBot::normalizeText(text, botBoundedStrLen(text, BOT_MAX_TEXT_LEN + 1), message.text,
                                                       sizeof(message.text), &message.text_len) == BOT_WRITE_TRUNCATED;
  recordBotObservation(message, &from, 0xFF);
}

void MyMesh::observeBotChannelMessage(uint8_t channel_idx, const char *channel_name, const char *text,
                                      uint32_t sender_timestamp, const mesh::Packet *packet) {
  BotMessage message;
  memset(&message, 0, sizeof(message));
  size_t channel_len = botBoundedStrLen(channel_name, BOT_MAX_CHANNEL_NAME_LEN);
  message.channel_kind = BotPolicy::classifyChannel(channel_name, channel_len, false, bot_prefs);
  if (channel_name && channel_len > 0) {
    memcpy(message.channel_name, channel_name, channel_len);
    message.channel_name[channel_len] = 0;
  }
  message.sender_timestamp = sender_timestamp;
  message.received_at_timestamp = getRTCClock()->getCurrentTime();
  if (packet && packet->isRouteFlood() && packet->path_len <= 0xFF && mesh::Packet::isValidPathLen((uint8_t)packet->path_len)) {
    message.path_len = (uint8_t)packet->path_len;
    message.path_hash_size = packet->getPathHashSize();
    message.path_hash_count = packet->getPathHashCount();
    message.packet_snr_quarters = (int8_t)(packet->getSNR() * 4);
    message.path = packet->path;
    message.path_is_inbound = true;
  }

  message.text_truncated = FirmwareBot::normalizeChannelText(text, message.sender_name, sizeof(message.sender_name),
                                                              message.text, sizeof(message.text),
                                                              &message.text_len) == BOT_WRITE_TRUNCATED;
  recordBotObservation(message, NULL, channel_idx);
}

void MyMesh::buildBotCommandContext(BotCommandContext &context, BotCommandId command_id) {
  memset(&context, 0, sizeof(context));
  StrHelper::strzcpy(context.node_name, _prefs.node_name, sizeof(context.node_name));
  StrHelper::strzcpy(context.bot_channel, bot_prefs.bot_channel, sizeof(context.bot_channel));
  StrHelper::strzcpy(context.testing_channel, bot_prefs.testing_channel, sizeof(context.testing_channel));
  StrHelper::strzcpy(context.emergency_channel, bot_prefs.emergency_channel, sizeof(context.emergency_channel));
  StrHelper::strzcpy(context.public_channel, bot_prefs.public_channel, sizeof(context.public_channel));
  context.uptime_seconds = _ms->getMillis() / 1000;
  context.observed_messages = bot_stats.observed_messages;
  context.ignored_messages = bot_stats.ignored_messages;
  context.eligible_messages = bot_stats.eligible_messages;
  context.sent_messages = bot_stats.sent_messages;
  context.send_failures = bot_stats.send_failures;
  context.emergency_forwards = bot_stats.emergency_forwards;
  context.emergency_forward_failures = bot_stats.emergency_forward_failures;
  if (command_id == BOT_COMMAND_STATUS || command_id == BOT_COMMAND_STATS) {
    context.battery_millivolts = board.getBattMilliVolts();
    context.storage_used_kb = _store->getStorageUsedKb();
    context.storage_total_kb = _store->getStorageTotalKb();
  }
  if (command_id == BOT_COMMAND_CHANNELS) {
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails channel;
      if (getChannel(i, channel) && channel.name[0]) context.channel_count++;
    }
  }
  if (command_id == BOT_COMMAND_VERSION) {
    StrHelper::strzcpy(context.firmware_version, FIRMWARE_VERSION, sizeof(context.firmware_version));
    StrHelper::strzcpy(context.firmware_build_date, FIRMWARE_BUILD_DATE, sizeof(context.firmware_build_date));
  }
  if (command_id == BOT_COMMAND_STATUS || command_id == BOT_COMMAND_STATS ||
      command_id == BOT_COMMAND_TEST || command_id == BOT_COMMAND_SIG || command_id == BOT_COMMAND_AIR) {
    context.queue_depth = (uint8_t)_mgr->getOutboundTotal();
    context.noise_floor = (int16_t)_radio->getNoiseFloor();
    context.last_rssi = (int8_t)radio_driver.getLastRSSI();
    context.last_snr_quarters = (int8_t)(radio_driver.getLastSNR() * 4);
    context.tx_airtime_seconds = getTotalAirTime() / 1000;
    context.rx_airtime_seconds = getReceiveAirTime() / 1000;
    context.packets_recv = radio_driver.getPacketsRecv();
    context.packets_sent = radio_driver.getPacketsSent();
    context.flood_sent = getNumSentFlood();
    context.direct_sent = getNumSentDirect();
    context.flood_recv = getNumRecvFlood();
    context.direct_recv = getNumRecvDirect();
    context.packets_recv_errors = radio_driver.getPacketsRecvErrors();
  }
}

bool MyMesh::findBotChannel(BotChannelKind kind, uint8_t &channel_idx) {
  for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails channel;
    if (getChannel(i, channel)) {
      size_t name_len = botBoundedStrLen(channel.name, BOT_MAX_CHANNEL_NAME_LEN);
      if (BotPolicy::classifyChannel(channel.name, name_len, false, bot_prefs) == kind) {
        channel_idx = i;
        return true;
      }
    }
  }
  return false;
}

BotCommandResult MyMesh::executeBotHelloCommand(const BotMessage &message, char *output, size_t output_len) {
  uint32_t now = getRTCClock()->getCurrentTime();
  const char *target = message.sender_name[0] ? message.sender_name : NULL;

  const char *greeting = "Hello";

  if (now != 0) {
    int64_t adjusted = (int64_t)now +
                       (int64_t)FirmwareBot::easternUtcOffsetSeconds(now);
    uint32_t seconds_of_day =
        (uint32_t)((adjusted % 86400LL + 86400LL) % 86400LL);
    uint8_t hour = (uint8_t)(seconds_of_day / 3600UL);

    if (hour < 12) {
      greeting = "Good Morning";
    } else if (hour < 18) {
      greeting = "Good Afternoon";
    } else {
      greeting = "Good Evening";
    }
  }

  if (target) {
    return botWriteFormatted(output, output_len, "%s @[%s]", greeting, target);
  }

  return botWriteText(output, output_len, greeting);
}

BotCommandResult MyMesh::executeBotTimeCommand(const BotMessage &message, char *output, size_t output_len) {
  uint32_t now = getRTCClock()->getCurrentTime();
  uint32_t uptime_seconds = _ms->getMillis() / 1000;
  char time_str[16];
  if (now == 0) {
    snprintf(time_str, sizeof(time_str), "not set");
  } else {
    int64_t adjusted = (int64_t)now +
                       (int64_t)FirmwareBot::easternUtcOffsetSeconds(now);
    uint32_t seconds_of_day = (uint32_t)((adjusted % 86400LL + 86400LL) % 86400LL);
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu:%02lu", (unsigned long)(seconds_of_day / 3600UL),
             (unsigned long)((seconds_of_day / 60UL) % 60UL), (unsigned long)(seconds_of_day % 60UL));
  }
  uint32_t days = uptime_seconds / 86400UL;
  uint32_t hours = (uptime_seconds / 3600UL) % 24UL;
  uint32_t mins = (uptime_seconds / 60UL) % 60UL;
  const char *tz = (now != 0 && FirmwareBot::easternUtcOffsetSeconds(now) == -4 * 3600) ? "EDT" : "EST";
  const char *target = message.sender_name[0] ? message.sender_name : NULL;
  if (target) {
    return botWriteFormatted(output, output_len, "@[%s] %s %s | up %lud %luh %lum", target, time_str, tz,
                             (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
  }
  return botWriteFormatted(output, output_len, "%s %s | up %lud %luh %lum", time_str, tz, (unsigned long)days,
                           (unsigned long)hours, (unsigned long)mins);
}

BotCommandResult MyMesh::executeBotLoraCommand(const BotMessage &message, char *output, size_t output_len) {
  unsigned long freq_khz_total = (unsigned long)(_prefs.freq * 1000.0f + 0.5f);
  unsigned long freq_mhz = freq_khz_total / 1000UL;
  unsigned long freq_khz = freq_khz_total % 1000UL;
  unsigned long bw_dh_total = (unsigned long)(_prefs.bw * 10.0f + 0.5f);
  unsigned long bw_khz = bw_dh_total / 10UL;
  unsigned long bw_dh = bw_dh_total % 10UL;
  const char *target = message.sender_name[0] ? message.sender_name : NULL;
  if (target) {
    return botWriteFormatted(output, output_len,
                             "@[%s] %lu.%03luMHz | SF%u | BW%lu.%lukHz | CR%u | %+ddBm", target, freq_mhz, freq_khz,
                             (unsigned)_prefs.sf, bw_khz, bw_dh, (unsigned)_prefs.cr, (int)_prefs.tx_power_dbm);
  }
  return botWriteFormatted(output, output_len, "%lu.%03luMHz | SF%u | BW%lu.%lukHz | CR%u | %+ddBm", freq_mhz, freq_khz,
                           (unsigned)_prefs.sf, bw_khz, bw_dh, (unsigned)_prefs.cr, (int)_prefs.tx_power_dbm);
}

BotCommandResult MyMesh::executeBotIdCommand(const BotMessage &message, char *output, size_t output_len) {
  char key_hex[BOT_SENDER_KEY_PREFIX_LEN * 2 + 1];
  botFormatKeyPrefixHex(self_id.pub_key, key_hex, sizeof(key_hex));
  const char *name = _prefs.node_name[0] ? _prefs.node_name : "MeshCore bot";
  const char *target = message.sender_name[0] ? message.sender_name : NULL;
  if (target) {
    return botWriteFormatted(output, output_len, "@[%s] %s I'm %s", target, key_hex, name);
  }
  return botWriteFormatted(output, output_len, "%s I'm %s", key_hex, name);
}

bool MyMesh::getBotNeighbor(size_t index, BotNeighbor& neighbor, uint32_t& ageSeconds) const {
  if (index >= BOT_NEIGHBOR_SLOTS || !bot_neighbors[index].active) return false;
  neighbor = bot_neighbors[index];
  ageSeconds = (uint32_t)(_ms->getMillis() - neighbor.last_heard_millis) / 1000;
  return true;
}

void MyMesh::recordBotNeighbor(const uint8_t *pub_key, int16_t rssi_dbm, int8_t snr_quarters) {
  if (!pub_key) return;
  size_t slot = BOT_NEIGHBOR_SLOTS;
  size_t oldest_slot = 0;
  uint32_t oldest_millis = 0;
  bool found_oldest = false;
  for (size_t i = 0; i < BOT_NEIGHBOR_SLOTS; i++) {
    if (bot_neighbors[i].active &&
        memcmp(bot_neighbors[i].pub_key_prefix, pub_key, BOT_SENDER_KEY_PREFIX_LEN) == 0) {
      slot = i;
      break;
    }
    if (!bot_neighbors[i].active) {
      if (slot == BOT_NEIGHBOR_SLOTS) slot = i;
    } else if (!found_oldest || (int32_t)(bot_neighbors[i].last_heard_millis - oldest_millis) < 0) {
      oldest_millis = bot_neighbors[i].last_heard_millis;
      oldest_slot = i;
      found_oldest = true;
    }
  }
  if (slot == BOT_NEIGHBOR_SLOTS) slot = oldest_slot;

  if (!bot_neighbors[slot].active ||
      memcmp(bot_neighbors[slot].pub_key_prefix, pub_key, BOT_SENDER_KEY_PREFIX_LEN) != 0) {
    bot_neighbors[slot] = BotNeighbor{}; // Never carry samples across different neighbors.
  }
  bot_neighbors[slot].active = true;
  memcpy(bot_neighbors[slot].pub_key_prefix, pub_key, BOT_SENDER_KEY_PREFIX_LEN);
  bot_neighbors[slot].last_heard_millis = _ms->getMillis();
  bot_neighbors[slot].addSignalSample(rssi_dbm, snr_quarters);
}

BotCommandResult MyMesh::executeBotNeighborsCommand(const BotMessage &message, char *output, size_t output_len, BotVoltageList::Snapshot* snapshot) {
  if (snapshot) snapshot->count = 0;
  static_assert(BOT_NEIGHBOR_SLOTS <= MonitorCore::MAX_REPEATERS, "Neighbor snapshot capacity");
  // Build only as large as this channel can actually transmit.
  size_t response_limit = FirmwareBot::maxResponseLenForChannel(message.channel_kind);
  if (response_limit + 1 < output_len) output_len = response_limit + 1;

  // Most samples first, then highest average SNR, then most recently heard.
  uint8_t idx[BOT_NEIGHBOR_SLOTS];
  uint8_t count = 0;
  for (size_t i = 0; i < BOT_NEIGHBOR_SLOTS; i++) {
    if (!bot_neighbors[i].active) continue;
    idx[count++] = (uint8_t)i;
  }
  for (uint8_t a = 0; a + 1 < count; a++) {
    for (uint8_t b = a + 1; b < count; b++) {
      const auto& left = bot_neighbors[idx[a]];
      const auto& right = bot_neighbors[idx[b]];
      if (right.sample_count > left.sample_count ||
          (right.sample_count == left.sample_count &&
           (right.snr_quarters > left.snr_quarters ||
            (right.snr_quarters == left.snr_quarters &&
             (int32_t)(right.last_heard_millis - left.last_heard_millis) > 0)))) {
        uint8_t tmp = idx[a];
        idx[a] = idx[b];
        idx[b] = tmp;
      }
    }
  }

  if (count == 0) {
    return botWriteText(output, output_len, "No repeaters heard directly");
  }

  if (!output || output_len == 0) return botCommandResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  output[0] = 0;
  size_t pos = 0;
  bool truncated = false;

  for (uint8_t k = 0; k < count; k++) {
    const BotNeighbor &n = bot_neighbors[idx[k]];
    char hex_name[9];
    const char *display_name;
    ContactInfo *contact = lookupContactByPubKey(n.pub_key_prefix, sizeof(n.pub_key_prefix));
    if (contact && contact->name[0]) {
      display_name = contact->name;
    } else {
      static const char hex[] = "0123456789abcdef";
      for (size_t i = 0; i < 4; i++) {
        hex_name[i * 2] = hex[n.pub_key_prefix[i] >> 4];
        hex_name[i * 2 + 1] = hex[n.pub_key_prefix[i] & 0x0F];
      }
      hex_name[8] = 0;
      display_name = hex_name;
    }
    char snr_str[8];
    botFormatQuarters(n.snr_quarters, snr_str, sizeof(snr_str));
    char entry[64];
    int entry_n = snprintf(entry, sizeof(entry),
                           "%s%s %d/%s",
                           k == 0 ? "" : "\n", display_name,
                           (int)n.rssi_dbm, snr_str);
    if (snapshot) {
      snprintf(snapshot->lines[snapshot->count++], BotVoltageList::LINE_SIZE,
               "%s %d/%s", display_name, (int)n.rssi_dbm, snr_str);
      continue;
    }
    if (entry_n < 0) break;
    size_t entry_len = (size_t)entry_n;
    size_t available = pos + 1 < output_len ? output_len - 1 - pos : 0;
    if (entry_len > available) {
      break;
    }
    memcpy(&output[pos], entry, entry_len);
    pos += entry_len;
    output[pos] = 0;
  }

  return botCommandResult(truncated ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, pos);
}

bool MyMesh::enqueueEmergencyForward(const BotMessage &message) {
  BotEmergencyForward forward;
  if (!EmergencyForwarder::format(message, forward)) return false;

  uint8_t free_slots = 0;
  for (size_t i = 0; i < BOT_PENDING_EMERGENCY_SLOTS; i++) {
    if (!pending_emergency_forwards[i].active) free_slots++;
  }
  if (free_slots < forward.part_count) return false;

  uint8_t part_idx = 0;
  for (size_t i = 0; i < BOT_PENDING_EMERGENCY_SLOTS && part_idx < forward.part_count; i++) {
    PendingEmergencyForward *pending = &pending_emergency_forwards[i];
    if (!pending->active) {
      pending->text_len = forward.part_lens[part_idx];
      if (pending->text_len > BOT_MAX_GROUP_RESPONSE_LEN) pending->text_len = BOT_MAX_GROUP_RESPONSE_LEN;
      if (pending->text_len > 0) memcpy(pending->text, forward.parts[part_idx], pending->text_len);
      pending->text[pending->text_len] = 0;
      pending->active = true;
      part_idx++;
    }
  }

  return true;
}

bool MyMesh::handleBotAdminCommand(const BotMessage &message, const ContactInfo *direct_recipient,
                                  const BotCommand &command) {
  // Bare help from an authorized admin DM has a dedicated command list.
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  const bool adminHelp = command.id == BOT_COMMAND_HELP && command.args_len == 0 &&
      message.channel_kind == BOT_CHANNEL_DM && direct_recipient && repeaterMonitor &&
      repeaterMonitor->botAdmins().allows(direct_recipient->id.pub_key, BotAdminContacts::Commands);
  if (command.id != BOT_COMMAND_ADVERT && command.id != BOT_COMMAND_CHECK && command.id != BOT_COMMAND_SYNC && !adminHelp) return false;
  // Use the authenticated DM contact's full key, never a channel name or key prefix.
  if (message.channel_kind != BOT_CHANNEL_DM || !direct_recipient || !repeaterMonitor ||
      !repeaterMonitor->botAdmins().allows(direct_recipient->id.pub_key, BotAdminContacts::Commands) ||
      message.text_truncated || pending_admin_advert.active || pending_voltage_list.active || pending_bot_dm_ack.active ||
      FirmwareBot::isCommandOnCooldown(bot_command_cooldowns, BOT_COMMAND_COOLDOWN_SLOTS,
                                      command.id, _ms->getMillis())) {
    bot_stats.ignored_messages++;
    return true;
  }
  const char *response = nullptr;
  if (adminHelp) {
    response = "Admin help:\nadvert\ncheck <repeater>\nsync <repeater>";
  } else if (command.id == BOT_COMMAND_SYNC) {
    response = repeaterMonitor->startAdminCheck(direct_recipient->id.pub_key, command.args, true);
    if (!response) response = "Syncing... I will send the result when finished";
  } else if (command.id == BOT_COMMAND_CHECK) {
    response = repeaterMonitor->startAdminCheck(direct_recipient->id.pub_key, command.args);
    // Worst case: 10 requests x 180s, eight 15s retry pauses, and 3s after login.
    // Round 1923 seconds up to 33 minutes; successful checks usually finish sooner.
    if (!response) response = "Checking... this might take up to 33 minutes";
  } else if (command.args_len) {
    response = "Usage: advert";
  } else {
    memcpy(pending_admin_advert.key, direct_recipient->id.pub_key, PUB_KEY_SIZE);
    pending_admin_advert.result = 0;
    pending_admin_advert.deadline = _ms->getMillis() + 600000;
    pending_admin_advert.active = true;
    if (!sendBotSelfAdvert(true, true)) {
      pending_admin_advert.active = false;
      response = "Advert failed";
      bot_stats.send_failures++;
    }
  }
  bot_stats.eligible_messages++;
  FirmwareBot::recordCommandCooldown(bot_command_cooldowns, BOT_COMMAND_COOLDOWN_SLOTS,
                                    command.id, _ms->getMillis(), BOT_COMMAND_COOLDOWN_MILLIS);
  if (response && !sendBotResponse(message, direct_recipient, 0xFF, response, strlen(response))) {
    bot_stats.send_failures++;
  }
#else
  if (command.id != BOT_COMMAND_ADVERT && command.id != BOT_COMMAND_CHECK && command.id != BOT_COMMAND_SYNC) return false;
  (void)message;
  (void)direct_recipient;
  bot_stats.ignored_messages++;
#endif
  return true;
}

void MyMesh::recordBotObservation(const BotMessage &message, const ContactInfo *direct_recipient, uint8_t channel_idx) {
  bot_stats.observed_messages++;
  BotPolicyDecision decision = BotPolicy::decide(message.channel_kind);
  if (decision == BOT_POLICY_IGNORE) {
    bot_stats.ignored_messages++;
    return;
  }
  if (decision == BOT_POLICY_EMERGENCY_FORWARD) {
    bot_stats.emergency_messages++;
    if (!enqueueEmergencyForward(message)) bot_stats.emergency_forward_failures++;
    return;
  }

  if (!bot_prefs.enabled) {
    bot_stats.ignored_messages++;
    return;
  }

  BotCommand command;
  if (!FirmwareBot::parseCommand(message.text, message.text_len, &command,
                                 BotPolicy::isPrefixlessCommandAllowed(message.channel_kind))) {
    if (message.text_len > 0 && (message.text[0] == '!' || message.text[0] == '/')) bot_stats.parse_errors++;
    return;
  }
  if (handleBotAdminCommand(message, direct_recipient, command)) return;
  if ((command.id != BOT_COMMAND_UNKNOWN && command.id != BOT_COMMAND_UNSUPPORTED &&
       !BotPrefsCodec::commandEnabled(bot_prefs, command.id)) ||
      FirmwareBot::isCommandOnCooldown(bot_command_cooldowns, BOT_COMMAND_COOLDOWN_SLOTS, command.id, _ms->getMillis())) {
    bot_stats.ignored_messages++;
    return;
  }

  const bool neighborsAll = command.id == BOT_COMMAND_NEIGHBORS && command.args_len == 3 && !strcasecmp(command.args, "all");
  const bool listLow = command.args_len == 3 &&
    (command.args[0]=='l' || command.args[0]=='L') &&
    (command.args[1]=='o' || command.args[1]=='O') &&
    (command.args[2]=='w' || command.args[2]=='W');
  // Keep the single DM acknowledgement slot intact while a list is being sent.
  if (pending_voltage_list.active && message.channel_kind == BOT_CHANNEL_DM) {
    bot_stats.ignored_messages++; return;
  }
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  if (((command.id == BOT_COMMAND_LIST && (!command.args_len || listLow) && repeaterMonitor) || neighborsAll) &&
      !pending_voltage_list.active && !pending_bot_dm_ack.active) {
    if (neighborsAll) {
      char unused[1];
      executeBotNeighborsCommand(message, unused, sizeof(unused), &pending_voltage_list.snapshot);
    } else repeaterMonitor->voltageList(pending_voltage_list.snapshot,listLow);
    if (pending_voltage_list.snapshot.count) {
      auto& q=pending_voltage_list;
      q.command=command.id; q.notification=false;
      q.kind=message.channel_kind; q.channel=channel_idx; q.next=0; q.part=1; q.failures=0;
      if (direct_recipient) memcpy(q.key,direct_recipient->id.pub_key,PUB_KEY_SIZE);
      q.deadline=_ms->getMillis(); q.expires=q.deadline+600000; q.active=true;
      bot_stats.eligible_messages++;
      FirmwareBot::recordCommandCooldown(bot_command_cooldowns,BOT_COMMAND_COOLDOWN_SLOTS,
        command.id,_ms->getMillis(),BOT_COMMAND_COOLDOWN_MILLIS);
      return;
    }
  }
#endif
  char response[BOT_MAX_RESPONSE_LEN + 1];
  BotCommandResult result;
  bool result_ready = false;
  BotCommandContext context;
  buildBotCommandContext(context, command.id);
  context.sender_timestamp = message.sender_timestamp;
  context.received_at_timestamp = message.received_at_timestamp;
  if (message.sender_name[0]) {
    StrHelper::strzcpy(context.response_target, message.sender_name, sizeof(context.response_target));
  }
  if (command.id == BOT_COMMAND_PATH ||
      command.id == BOT_COMMAND_TEST ||
      command.id == BOT_COMMAND_SIG) {
    context.path_len = message.path_len;
    context.path_hash_size = command.args_len > 0 ?
      (command.id == BOT_COMMAND_PATH ? _prefs.path_hash_mode + 1 : botConfiguredTraceHashSize(_prefs.path_hash_mode)) : message.path_hash_size;
    context.path_hash_count = message.path_hash_count;
    context.path_snr_quarters = message.packet_snr_quarters;
    context.path = message.path;
  }
  if (command.id == BOT_COMMAND_PATH) {
    BotPath::Route route;
    if (BotCommands::pathRoute(command,context,route)) {
      resolveLocalPathNames(route);
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
      if (!pending_bot_path.active && BotPathLookup::start(route)) {
        auto& q=pending_bot_path;
        q.active=true; q.ready=false; q.route=route; q.message=message;
        q.message.path=nullptr; // The received packet will be freed after this call.
        q.channel=channel_idx; q.lookupDeadline=futureMillis(12000); q.expires=futureMillis(30000);
        if(direct_recipient) memcpy(q.key,direct_recipient->id.pub_key,PUB_KEY_SIZE);
        bot_stats.eligible_messages++;
        FirmwareBot::recordCommandCooldown(bot_command_cooldowns,BOT_COMMAND_COOLDOWN_SLOTS,
          command.id,_ms->getMillis(),BOT_COMMAND_COOLDOWN_MILLIS);
        return;
      }
#endif
      const size_t budget=FirmwareBot::maxResponseLenForChannel(message.channel_kind);
      result=BotPath::format(route,context.response_target,response,budget+1);
      result_ready=true;
    }
  }
  char last_repeater_name[BOT_MAX_SENDER_NAME_LEN + 1] = {};
  if (command.id == BOT_COMMAND_SIG && message.path_is_inbound && message.path &&
      message.path_hash_count > 0 && message.path_hash_size > 0 && message.path_hash_size <= 4 &&
      (size_t)message.path_hash_count * message.path_hash_size <= BOT_MAX_PATH_BYTES) {
    const uint8_t* last_hash = message.path + (message.path_hash_count - 1) * message.path_hash_size;
    ContactsIterator iter;
    ContactInfo contact;
    unsigned matches = 0;
    while (iter.hasNext(this, contact)) {
      if (contact.type != ADV_TYPE_REPEATER ||
          memcmp(contact.id.pub_key, last_hash, message.path_hash_size) != 0) continue;
      if (++matches > 1) break;  // Short path hashes can identify multiple repeaters.
      if (contact.name[0]) {
        char short_name[33];
        BotVoltageList::shortName(contact.name,contact.id.pub_key,short_name);
        StrHelper::strzcpy(last_repeater_name,short_name,sizeof(last_repeater_name));
      }
    }
    if (matches != 1 || !last_repeater_name[0]) {
      static const char hex[] = "0123456789abcdef";
      last_repeater_name[0] = '[';
      for (uint8_t i = 0; i < message.path_hash_size; ++i) {
        last_repeater_name[1 + i * 2] = hex[last_hash[i] >> 4];
        last_repeater_name[2 + i * 2] = hex[last_hash[i] & 15];
      }
      last_repeater_name[1 + message.path_hash_size * 2] = ']';
      last_repeater_name[2 + message.path_hash_size * 2] = 0;
    }
    context.last_repeater_name = last_repeater_name;
  }
  if (!result_ready) {
    switch (command.id) {
      case BOT_COMMAND_HELLO:
        result = executeBotHelloCommand(message, response, sizeof(response));
        break;
      case BOT_COMMAND_TIME:
        result = executeBotTimeCommand(message, response, sizeof(response));
        break;
      case BOT_COMMAND_LORA:
        result = executeBotLoraCommand(message, response, sizeof(response));
        break;
      case BOT_COMMAND_ID:
        result = executeBotIdCommand(message, response, sizeof(response));
        break;
      case BOT_COMMAND_LIST:
        result = botWriteText(response,sizeof(response),command.args_len && !listLow ? "Usage: list [low]" :
          pending_voltage_list.active || pending_bot_dm_ack.active ? "Bot busy; try list again shortly" : listLow ? BotVoltageList::EMPTY_LOW : "No repeater readings available");
        break;
      case BOT_COMMAND_NEIGHBORS:
        result = command.args_len && !neighborsAll ? botWriteText(response,sizeof(response),"Usage: neighbors [all]") :
          neighborsAll && (pending_voltage_list.active || pending_bot_dm_ack.active) ? botWriteText(response,sizeof(response),"Bot busy; try neighbors all again shortly") :
          executeBotNeighborsCommand(message, response, sizeof(response));
        break;
      default:
        result = BotCommands::executeCommand(command, context, response, sizeof(response));
        break;
    }
  }
  if (result.code == BOT_COMMAND_RESULT_NOT_HANDLED || result.code == BOT_COMMAND_RESULT_NO_SPACE || result.text_len == 0) {
    bot_stats.parse_errors++;
    return;
  }

  char final_response[BOT_MAX_RESPONSE_LEN + 1];
  size_t final_response_len = 0;
  if (!botFormatResponseForChannel(message, response, result.text_len, final_response, sizeof(final_response), &final_response_len)) {
    bot_stats.send_failures++;
    return;
  }

  bot_stats.eligible_messages++;
  FirmwareBot::recordCommandCooldown(bot_command_cooldowns, BOT_COMMAND_COOLDOWN_SLOTS,
                                     command.id, _ms->getMillis(),
                                     BOT_COMMAND_COOLDOWN_MILLIS);

  if (!sendBotResponse(message, direct_recipient, channel_idx,
                       final_response, final_response_len)) {
    bot_stats.send_failures++;
  }
}

bool MyMesh::sendBotGroupMessage(uint8_t channel_idx, const char *text, size_t text_len) {
  ChannelDetails channel;
  if (!getChannel(channel_idx, channel)) return false;

  uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();

  size_t channel_name_len =
      botBoundedStrLen(channel.name, BOT_MAX_CHANNEL_NAME_LEN);
  BotChannelKind channel_kind =
      BotPolicy::classifyChannel(channel.name, channel_name_len, false, bot_prefs);

  if (channel_kind == BOT_CHANNEL_FAIRFIELD) {
    // #fairfield-county bot responses travel inside the #ct-ffc scope.
    TransportKey saved_scope = send_scope;

    TransportKeyStore temp;
    TransportKey ct_ffc_scope;
    temp.getAutoKeyFor(0, "#ct-ffc", ct_ffc_scope);

    send_scope = ct_ffc_scope;
    bool success = sendGroupMessage(timestamp, channel.channel,
                                    _prefs.node_name, text, text_len);
    send_scope = saved_scope;
    return success;
  }

  return sendGroupMessage(timestamp, channel.channel,
                          _prefs.node_name, text, text_len);
}

bool MyMesh::sendBotResponse(const BotMessage &message, const ContactInfo *direct_recipient,
                             uint8_t channel_idx, const char *text, size_t text_len) {
  if (message.channel_kind == BOT_CHANNEL_DM) {
    if (!direct_recipient) return false;

    uint32_t expected_ack = 0;
    uint32_t est_timeout = 0;
    uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();

    int result = sendMessage(*direct_recipient, timestamp, 0,
                             text, expected_ack, est_timeout);

    if (result == MSG_SEND_FAILED) return false;

    if (expected_ack) {
      expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis();
      expected_ack_table[next_ack_idx].ack = expected_ack;
      expected_ack_table[next_ack_idx].contact =
          const_cast<ContactInfo *>(direct_recipient);
      next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;

      pending_bot_dm_ack.active = true;
      memcpy(pending_bot_dm_ack.recipient_pub_key,
             direct_recipient->id.pub_key,
             sizeof(pending_bot_dm_ack.recipient_pub_key));
      pending_bot_dm_ack.expected_ack = expected_ack;
      pending_bot_dm_ack.timestamp = timestamp;
      pending_bot_dm_ack.ack_deadline_millis =
          futureMillis(est_timeout);
      pending_bot_dm_ack.attempt = 0;
      pending_bot_dm_ack.text_len = text_len;
      memcpy(pending_bot_dm_ack.text, text, text_len);
      pending_bot_dm_ack.text[text_len] = 0;
    }

    bot_stats.sent_messages++;
    return true;
  }

  if (channel_idx == 0xFF) return false;

  if (!sendBotGroupMessage(channel_idx, text, text_len)) {
    return false;
  }

  bot_stats.sent_messages++;
  return true;
}

void MyMesh::sendQueuedEmergencyForwards() {
  uint8_t public_channel_idx = 0xFF;
  bool have_public = findBotChannel(BOT_CHANNEL_PUBLIC, public_channel_idx);
  ChannelDetails public_channel;
  if (have_public) have_public = getChannel(public_channel_idx, public_channel);

  for (size_t i = 0; i < BOT_PENDING_EMERGENCY_SLOTS; i++) {
    PendingEmergencyForward *pending = &pending_emergency_forwards[i];
    if (!pending->active) continue;

    bool success = false;
    if (have_public) {
      uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
      success = sendGroupMessage(timestamp, public_channel.channel, _prefs.node_name, pending->text, pending->text_len);
    }

    if (success) {
      bot_stats.emergency_forwards++;
      pending->active = false;
    } else {
      bot_stats.emergency_forward_failures++;
    }
  }
}

bool MyMesh::sendBotSelfAdvert(bool flood, bool trackAdmin) {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (!pkt) return false;
  if (trackAdmin) {
    pending_admin_advert.packet = pkt;
    pkt->calculatePacketHash(pending_admin_advert.hash);
  }

  if (flood) {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));
    sendFloodScoped(default_scope, pkt, 0);
  } else {
    sendZeroHop(pkt);
  }
  return true;
}

void MyMesh::completeAdminAdvert(mesh::Packet* packet, bool success) {
  auto& pending = pending_admin_advert;
  if (!pending.active || pending.result || pending.packet != packet) return;
  uint8_t hash[MAX_HASH_SIZE]; packet->calculatePacketHash(hash);
  if (memcmp(hash, pending.hash, sizeof(hash))) return;
  pending.result = success ? 1 : 2;
  pending.packet = nullptr;
}

void MyMesh::logTx(mesh::Packet* packet, int len) {
  BaseChatMesh::logTx(packet, len);
  if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->payload_len >= PUB_KEY_SIZE &&
      !memcmp(packet->payload, self_id.pub_key, PUB_KEY_SIZE)) {
    last_bot_advert_time = getRTCClock()->getCurrentTime();
  }
  completeAdminAdvert(packet, true); // Dispatcher calls this after isSendComplete().
}

void MyMesh::logTxFail(mesh::Packet* packet, int len) {
  BaseChatMesh::logTxFail(packet, len);
  completeAdminAdvert(packet, false);
}

void MyMesh::pollAdminAdvert() {
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  auto& pending = pending_admin_advert;
  if (!pending.active) return;
  if (!bot_prefs.enabled || !repeaterMonitor ||
      !repeaterMonitor->botAdmins().allows(pending.key, BotAdminContacts::Commands)) {
    pending.active=false; pending.packet=nullptr; return;
  }
  if (!pending.result && millisHasNowPassed(pending.deadline)) {
    pending.result=3; pending.packet=nullptr;
  }
  if (!pending.result || pending_bot_dm_ack.active || pending_voltage_list.active) return;
  auto* recipient=lookupContactByPubKey(pending.key,PUB_KEY_SIZE);
  const char* response=pending.result==1 ? "Advert sent" :
      pending.result==2 ? "Advert failed" : "Advert transmission not confirmed";
  BotMessage message{};message.channel_kind=BOT_CHANNEL_DM;
  pending.active=false;
  if (pending.result!=1) ++bot_stats.send_failures;
  if (!recipient || !sendBotResponse(message,recipient,0xFF,response,strlen(response))) ++bot_stats.send_failures;
#endif
}

void MyMesh::resolveLocalPathNames(BotPath::Route& route) {
  for(size_t hop=0;hop<route.count;++hop) {
    ContactsIterator iter; ContactInfo contact; unsigned matches=0;
    char local[33]{};
    while(iter.hasNext(this,contact)) {
      if(contact.type!=ADV_TYPE_REPEATER || memcmp(contact.id.pub_key,route.bytes+hop*route.width,route.width)) continue;
      if(++matches>1) break;
      BotPath::shortName(contact.name,local);
    }
    if(matches>1) { route.ambiguous[hop]=true; route.names[hop][0]=0; }
    else if(matches==1 && local[0]) { memcpy(route.names[hop],local,33); route.ambiguous[hop]=false; }
  }
}

void MyMesh::pollBotPath() {
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  auto& q=pending_bot_path;
  if(!q.active) { BotPathLookup::take(q.route); return; } // Drain a late worker result.
  if(!q.ready) q.ready=BotPathLookup::take(q.route);
  if(!q.ready && millisHasNowPassed(q.lookupDeadline)) q.ready=true;
  if(!q.ready) return;
  if(!bot_prefs.enabled || !BotPrefsCodec::commandEnabled(bot_prefs,BOT_COMMAND_PATH) || millisHasNowPassed(q.expires)) {
    q.active=false; return;
  }
  if(q.message.channel_kind==BOT_CHANNEL_DM && (pending_bot_dm_ack.active || pending_voltage_list.active)) return;
  resolveLocalPathNames(q.route); // Adverts received during the lookup take priority.
  char response[BOT_MAX_RESPONSE_LEN+1], formatted[BOT_MAX_RESPONSE_LEN+1];
  const size_t budget=FirmwareBot::maxResponseLenForChannel(q.message.channel_kind);
  const auto result=BotPath::format(q.route,q.message.sender_name,response,budget+1);
  size_t written=0;
  auto* recipient=q.message.channel_kind==BOT_CHANNEL_DM ? lookupContactByPubKey(q.key,PUB_KEY_SIZE) : nullptr;
  q.active=false;
  if(result.code!=BOT_COMMAND_RESULT_OK ||
     !botFormatResponseForChannel(q.message,response,result.text_len,formatted,sizeof(formatted),&written) ||
     !sendBotResponse(q.message,recipient,q.channel,formatted,written)) ++bot_stats.send_failures;
#endif
}

void MyMesh::scheduleBotLocalAdvert(unsigned long interval_millis) {
  next_bot_local_advert = interval_millis > 0 ? futureMillis(interval_millis) : 0;
}

void MyMesh::scheduleBotFloodAdvert(unsigned long interval_millis) {
  next_bot_flood_advert = interval_millis > 0 ? futureMillis(interval_millis) : 0;
}

#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
bool MyMesh::sendAdminCheckResult(const uint8_t* key, const char* text) {
  if (!bot_prefs.enabled || pending_voltage_list.active || pending_bot_dm_ack.active) return false;
  if (!repeaterMonitor || !repeaterMonitor->botAdmins().allows(key,BotAdminContacts::Commands)) return true;
  auto* recipient=lookupContactByPubKey(key,PUB_KEY_SIZE);
  if(!recipient) {++bot_stats.send_failures;return true;}
  BotMessage message{};message.channel_kind=BOT_CHANNEL_DM;
  const bool sent=sendBotResponse(message,recipient,0xFF,text,strlen(text));
  if(!sent) ++bot_stats.send_failures;
  return true; // Existing DM ACK/retry machinery owns delivery once queued.
}

bool MyMesh::queueSunriseNotification(const uint8_t* key, const BotVoltageList::Snapshot& snapshot) {
  if (!bot_prefs.enabled || pending_voltage_list.active || pending_bot_dm_ack.active ||
      !repeaterMonitor || !repeaterMonitor->botAdmins().allows(key, BotAdminContacts::Notifications)) return false;
  auto& q = pending_voltage_list;
  q.snapshot = snapshot; q.command = BOT_COMMAND_LIST; q.notification = true;
  q.kind = BOT_CHANNEL_DM; q.channel = 0xFF; memcpy(q.key, key, PUB_KEY_SIZE);
  q.next = 0; q.part = 1; q.failures = 0;
  q.deadline = _ms->getMillis(); q.expires = q.deadline + 600000; q.active = true;
  return true;
}
#endif

void MyMesh::sendNextVoltageListPart() {
  auto& q=pending_voltage_list;
  if (!q.active) return;
  if (!bot_prefs.enabled || (!q.notification && !BotPrefsCodec::commandEnabled(bot_prefs,q.command)) || millisHasNowPassed(q.expires)) {
    q.active=false; return;
  }
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  if (q.notification && (!repeaterMonitor ||
      !repeaterMonitor->botAdmins().allows(q.key, BotAdminContacts::Notifications))) { q.active=false; return; }
#endif
  if (pending_bot_dm_ack.active || !millisHasNowPassed(q.deadline)) return;
  ContactInfo* recipient=q.kind==BOT_CHANNEL_DM ? lookupContactByPubKey(q.key,PUB_KEY_SIZE) : nullptr;
  if(q.kind==BOT_CHANNEL_DM && !recipient) {q.active=false;bot_stats.send_failures++;return;}
  char body[121],formatted[BOT_MAX_RESPONSE_LEN+1];size_t next=q.next,written=0;
  const char* heading = q.notification ? "Low Voltage Report:\n" : "";
  const size_t headingLen = strlen(heading);
  memcpy(body, heading, headingLen);
  if (q.notification && q.snapshot.count == 1 && !strcmp(q.snapshot.lines[0], BotVoltageList::EMPTY_LOW)) {
    strcpy(body + headingLen, BotVoltageList::EMPTY_LOW); next=1;
  } else if(!BotVoltageList::page(q.snapshot,next,q.part,body + headingLen,sizeof(body) - headingLen)) {q.active=false;return;}
  BotMessage message{};message.channel_kind=q.kind;
  if(!botFormatResponseForChannel(message,body,strlen(body),formatted,sizeof(formatted),&written) ||
     !sendBotResponse(message,recipient,q.channel,formatted,written)) {
    bot_stats.send_failures++;if(++q.failures>=3) q.active=false;
  } else {q.next=next;++q.part;q.failures=0;if(q.next>=q.snapshot.count) q.active=false;}
  q.deadline=_ms->getMillis()+5000;
}

void MyMesh::tickBot() {
  pollAdminAdvert();
  pollBotPath();
  sendNextVoltageListPart();
  uint32_t now = _ms->getMillis();

  if (pending_bot_dm_ack.active &&
      millisHasNowPassed(pending_bot_dm_ack.ack_deadline_millis)) {
    if (pending_bot_dm_ack.attempt >= 1) {
      pending_bot_dm_ack.active = false;
      bot_stats.send_failures++;
    } else {
      ContactInfo *recipient =
          lookupContactByPubKey(pending_bot_dm_ack.recipient_pub_key, PUB_KEY_SIZE);

      if (!recipient) {
        pending_bot_dm_ack.active = false;
        bot_stats.send_failures++;
      } else {
        // The known direct route did not ACK. Forget it so the retry
        // falls back to MeshCore's flood routing.
        resetPathTo(*recipient);
        pending_bot_dm_ack.attempt++;

        uint32_t expected_ack = 0;
        uint32_t est_timeout = 0;
        int result = sendMessage(*recipient,
                                 pending_bot_dm_ack.timestamp,
                                 pending_bot_dm_ack.attempt,
                                 pending_bot_dm_ack.text,
                                 expected_ack,
                                 est_timeout);

        if (result == MSG_SEND_FAILED || expected_ack == 0) {
          pending_bot_dm_ack.active = false;
          bot_stats.send_failures++;
        } else {
          pending_bot_dm_ack.expected_ack = expected_ack;
          pending_bot_dm_ack.ack_deadline_millis =
              futureMillis(est_timeout);

          expected_ack_table[next_ack_idx].msg_sent = now;
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }
      }
    }
  }

  sendQueuedEmergencyForwards();
  if (!bot_prefs.enabled) return;
  if (next_bot_local_advert && millisHasNowPassed(next_bot_local_advert)) {
    sendBotSelfAdvert(false);
    scheduleBotLocalAdvert(bot_prefs.local_advert_interval_ms);
  }
  if (next_bot_flood_advert && millisHasNowPassed(next_bot_flood_advert)) {
    sendBotSelfAdvert(true);
    scheduleBotFloodAdvert(bot_prefs.flood_advert_interval_ms);
  }
}
#endif


void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  } else {
#ifdef DISPLAY_CLASS
    if (_ui) _ui->notify(UIEventType::channelMessage);
#endif
  }
  const char *channel_name = "Unknown";
  ChannelDetails channel_details;
  if (getChannel(channel_idx, channel_details)) {
    channel_name = channel_details.name;
  }
#ifdef DISPLAY_CLASS
  if (_ui) _ui->newMsg(path_len, channel_name, text, offline_queue_len);
#endif
#if CMESH_BOT_ENABLED
  observeBotChannelMessage(channel_idx, channel_name, text, timestamp, pkt);
#endif
}

void MyMesh::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                               const uint8_t *data, size_t data_len) {
  if (data_len > MAX_CHANNEL_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: dropping payload_len=%d exceeds frame limit=%d",
                       (uint32_t)data_len, (uint32_t)MAX_CHANNEL_DATA_LENGTH);
    return;
  }

  int i = 0;
  out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
  out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
  out_frame[i++] = 0; // reserved1
  out_frame[i++] = 0; // reserved2

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = (uint8_t)(data_type & 0xFF);
  out_frame[i++] = (uint8_t)(data_type >> 8);
  out_frame[i++] = (uint8_t)data_len;

  int copy_len = (int)data_len;
  if (copy_len > 0) {
    memcpy(&out_frame[i], data, copy_len);
    i += copy_len;
  }
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;

    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  }
  return 0; // unknown
}

#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
bool MyMesh::restoreMonitorContactName(const uint8_t* key) {
  auto* c = lookupContactByPubKey(key, PUB_KEY_SIZE);
  if (!c) return false;
  uint8_t raw[256];
  const uint8_t len = exportContact(*c, raw);
  mesh::Packet packet;
  constexpr size_t appOffset = PUB_KEY_SIZE + 4 + SIGNATURE_SIZE;
  if (!len || !packet.readFrom(raw, len) || packet.getPayloadType() != PAYLOAD_TYPE_ADVERT ||
      packet.payload_len <= appOffset || packet.payload_len > appOffset + MAX_ADVERT_DATA_SIZE ||
      memcmp(packet.payload, key, PUB_KEY_SIZE)) return false;
  uint32_t timestamp; memcpy(&timestamp, packet.payload + PUB_KEY_SIZE, 4);
  if (timestamp < c->last_advert_timestamp) return false;
  const uint8_t* app = packet.payload + appOffset;
  const size_t appLen = packet.payload_len - appOffset;
  const size_t nameOffset = 1 + ((app[0] & 0x10) ? 8 : 0) + ((app[0] & 0x20) ? 2 : 0) + ((app[0] & 0x40) ? 2 : 0);
  if (!(app[0] & 0x80) || nameOffset >= appLen || appLen - nameOffset >= sizeof(c->name)) return false;
  uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE];
  memcpy(message, packet.payload, PUB_KEY_SIZE + 4);
  memcpy(message + PUB_KEY_SIZE + 4, app, appLen);
  if (!c->id.verify(packet.payload + PUB_KEY_SIZE + 4, message, PUB_KEY_SIZE + 4 + appLen)) return false;
  char name[32]{};
  memcpy(name, app + nameOffset, appLen - nameOffset);
  for (size_t i = 0; i < appLen - nameOffset; ++i) if ((uint8_t)name[i] < 32 || name[i] == 127) return false;
  bool changed = strcmp(c->name, name) != 0;
  strlcpy(c->name, name, sizeof(c->name));
  if (app[0] & 0x10) {
    int32_t lat,lon;memcpy(&lat,app+1,4);memcpy(&lon,app+5,4);
    if ((lat || lon) && lat >= -90000000 && lat <= 90000000 && lon >= -180000000 && lon <= 180000000 &&
        (c->gps_lat != lat || c->gps_lon != lon)) {
      c->gps_lat=lat;c->gps_lon=lon;changed=true;
    }
  }
  if (!changed) return false;
  c->lastmod = getRTCClock()->getCurrentTime();
  return true;
}
#endif

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  if (len < 4) return;
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
  if (repeaterMonitor && repeaterMonitor->onResponse(contact, data, len)) return;
#endif
  uint32_t tag;
  memcpy(&tag, data, 4);

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
    }
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }

}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (path_hash_count + 1));
}

void MyMesh::onSendTimeout() {}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _ui(ui) {
  _iter_started = false;
  _cli_rescue = false;
  offline_queue_len = 0;
  app_target_ver = 0;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  dirty_contacts_expiry = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(send_scope.key, 0, sizeof(send_scope.key));
  send_unscoped = false;
#if CMESH_BOT_ENABLED
  BotPrefsCodec::defaults(bot_prefs);
  memset(&bot_stats, 0, sizeof(bot_stats));
  memset(&pending_bot_dm_ack, 0, sizeof(pending_bot_dm_ack));
  memset(pending_emergency_forwards, 0, sizeof(pending_emergency_forwards));
  memset(bot_command_cooldowns, 0, sizeof(bot_command_cooldowns));
  memset(bot_neighbors, 0, sizeof(bot_neighbors));
  KnownBotRegistry::clear(known_bot_entries, BOT_KNOWN_BOT_SLOTS);
  next_bot_local_advert = 0;
  next_bot_flood_advert = 0;
#endif

  // defaults
  memset(&_prefs, 0, sizeof(_prefs));
  _prefs.airtime_factor = 1.0;
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
#if CMESH_BOT_ENABLED
  _prefs.path_hash_mode = 1;
#endif
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default
#endif
#endif
}

void MyMesh::begin(bool has_display) {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // if build provides default-scope, init with that
#ifdef DEFAULT_FLOOD_SCOPE_NAME
  strcpy(_prefs.default_scope_name, DEFAULT_FLOOD_SCOPE_NAME);
  {
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs, sensors.node_lat, sensors.node_lon);

  // sanitise bad pref values
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 150.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, -9, MAX_LORA_TX_POWER);
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours
#if CMESH_BOT_ENABLED
  _prefs.autoadd_config |= AUTO_ADD_OVERWRITE_OLDEST;
#endif

#ifdef BLE_PIN_CODE // 123456 by default
  if (_prefs.ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (has_display && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      _active_ble_pin = rng.nextInt(100000, 999999); // random pin each session
    } else {
      _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
    }
#else
    _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
#endif
  } else {
    _active_ble_pin = _prefs.ble_pin;
  }
#else
  _active_ble_pin = 0;
#endif

  resetContacts();
  _store->loadContacts(this);
  bootstrapRTCfromContacts();
  addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  _store->loadChannels(this);

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
#if CMESH_BOT_ENABLED
  if (!_store->loadBotPrefs(bot_prefs)) bot_prefs.prefs_load_failures++;
  BotPrefsCodec::validate(bot_prefs);
  applyBotPrefs();
#endif
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

static FreqRange repeat_freq_ranges[] = {
  #ifdef ALLOWED_REPEAT_FREQ_RANGE
  ALLOWED_REPEAT_FREQ_RANGE
  #else
  { 433000, 433000 },
  { 869495, 869495 },
  { 918000, 918000 }
  #endif
};

bool MyMesh::isValidClientRepeatFreq(uint32_t f) const {
  for (int i = 0; i < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]); i++) {
    auto r = &repeat_freq_ranges[i];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();
}

void MyMesh::handleCmdFrame(size_t len) {
  if (cmd_frame[0] == CMD_DEVICE_QUERY && len >= 2) { // sent when app establishes connection
    app_target_ver = cmd_frame[1];                    // which version of protocol does app understand

    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    out_frame[i++] = MAX_CONTACTS / 2;   // v3+
    out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
    memcpy(&out_frame[i], &_prefs.ble_pin, 4);
    i += 4;
    memset(&out_frame[i], 0, 12);
    strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
    i += 12;
    StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out_frame[i], FIRMWARE_VERSION, 20);
    i += 20;
    out_frame[i++] = _prefs.client_repeat;   // v9+
    out_frame[i++] = _prefs.path_hash_mode;  // v10+
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_APP_START &&
             len >= 8) { // sent when app establishes connection, respond with node ID
    //  cmd_frame[1..7]  reserved future
    char *app_name = (char *)&cmd_frame[8];
    cmd_frame[len] = 0; // make app_name null terminated
    MESH_DEBUG_PRINTLN("App %s connected", app_name);

    _iter_started = false; // stop any left-over ContactsIterator
    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
    out_frame[i++] = _prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat, lon;
    lat = (sensors.node_lat * 1000000.0);
    lon = (sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks; // new v7+
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                     (_prefs.telemetry_mode_base); // v5+
    out_frame[i++] = _prefs.manual_add_contacts;

    uint32_t freq = _prefs.freq * 1000;
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = _prefs.bw * 1000;
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
    memcpy(&out_frame[i], _prefs.node_name, tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_TXT_MSG && len >= 14) {
    int i = 1;
    uint8_t txt_type = cmd_frame[i++];
    uint8_t attempt = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    uint8_t *pub_key_prefix = &cmd_frame[i];
    i += 6;
    ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
    if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      uint32_t est_timeout;
      text[tlen] = 0; // ensure null
      int result;
      uint32_t expected_ack;
      if (txt_type == TXT_TYPE_CLI_DATA) {
        msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
        result = sendCommandData(*recipient, msg_timestamp, attempt, text, est_timeout);
        expected_ack = 0; // no Ack expected
      } else {
        result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
      }
      // TODO: add expected ACK to table
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        if (expected_ack) {
          expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &expected_ack, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(recipient == NULL
                        ? ERR_CODE_NOT_FOUND
                        : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsupported TXT_TYPE_*
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_TXT_MSG) { // send GroupChannel text msg
    int i = 1;
    uint8_t txt_type = cmd_frame[i++]; // should be TXT_TYPE_PLAIN
    uint8_t channel_idx = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    const char *text = (char *)&cmd_frame[i];

    if (txt_type != TXT_TYPE_PLAIN) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else {
      ChannelDetails channel;
      bool success = getChannel(channel_idx, channel);
      if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, len - i)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
      }
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_DATA) { // send GroupChannel datagram
    if (len < 4) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }
    int i = 1;
    uint8_t channel_idx = cmd_frame[i++];
    uint8_t path_len = cmd_frame[i++];

    // validate path len, allowing 0xFF for flood
    if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA invalid path size: %d", path_len);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }

    // parse provided path if not flood
    uint8_t path[MAX_PATH_SIZE];
    if (path_len != OUT_PATH_UNKNOWN) {
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
    }

    uint16_t data_type = ((uint16_t)cmd_frame[i]) | (((uint16_t)cmd_frame[i + 1]) << 8);
    i += 2;
    const uint8_t *payload = &cmd_frame[i];
    int payload_len = (len > (size_t)i) ? (int)(len - i) : 0;

    ChannelDetails channel;
    if (!getChannel(channel_idx, channel)) {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    } else if (data_type == DATA_TYPE_RESERVED) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (payload_len > MAX_CHANNEL_DATA_LENGTH) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA payload too long: %d > %d", payload_len, MAX_CHANNEL_DATA_LENGTH);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (sendGroupData(channel.channel, path, path_len, data_type, payload, payload_len)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACTS) { // get Contact list
    if (_iter_started) {
      writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
    } else {
      if (len >= 5) { // has optional 'since' param
        memcpy(&_iter_filter_since, &cmd_frame[1], 4);
      } else {
        _iter_filter_since = 0;
      }

      uint8_t reply[5];
      reply[0] = RESP_CODE_CONTACTS_START;
      uint32_t count = getNumContacts(); // total, NOT filtered count
      memcpy(&reply[1], &count, 4);
      _serial->writeFrame(reply, 5);

      // start iterator
      _iter = startContactsIterator();
      _iter_started = true;
      _most_recent_lastmod = 0;
    }
  } else if (cmd_frame[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = len - 1;
    if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
    memcpy(_prefs.node_name, &cmd_frame[1], nlen);
    _prefs.node_name[nlen] = 0; // null terminator
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &cmd_frame[1], 4);
    memcpy(&lon, &cmd_frame[5], 4);
    if (len >= 13) {
      memcpy(&alt, &cmd_frame[9], 4); // for FUTURE support
    }
    if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
    }
  } else if (cmd_frame[0] == CMD_GET_DEVICE_TIME) {
    uint8_t reply[5];
    reply[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply[1], &now, 4);
    _serial->writeFrame(reply, 5);
  } else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &cmd_frame[1], 4);
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (secs >= curr) {
      getRTCClock()->setCurrentTime(secs);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SEND_SELF_ADVERT) {
    mesh::Packet* pkt;
    if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
      pkt = createSelfAdvert(_prefs.node_name);
    } else {
      pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
    }
    if (pkt) {
      if (len >= 2 && cmd_frame[1] == 1) { // optional param (1 = flood, 0 = zero hop)
        unsigned long delay_millis = 0;
        TransportKey default_scope;
        memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));
        sendFloodScoped(default_scope, pkt, delay_millis);
      } else {
        sendZeroHop(pkt);
      }
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_RESET_PATH && len >= 1 + 32) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
    }
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint32_t last_mod = getRTCClock()->getCurrentTime();  // fallback value if not present in cmd_frame
    if (recipient) {
      updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
      recipient->lastmod = last_mod;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      ContactInfo contact;
      updateContactFromFrame(contact, last_mod, cmd_frame, len);
      contact.lastmod = last_mod;
      contact.sync_since = 0;
      if (addContact(contact)) {
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_REMOVE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient && removeContact(*recipient)) {
      _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
    }
  } else if (cmd_frame[0] == CMD_SHARE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      if (shareContactZeroHop(*recipient)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACT_BY_KEY) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (contact) {
      writeContactRespFrame(RESP_CODE_CONTACT, *contact);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  } else if (cmd_frame[0] == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {
      // export SELF
      mesh::Packet* pkt;
      if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
        pkt = createSelfAdvert(_prefs.node_name);
      } else {
        pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
      }
      if (pkt) {
        pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        uint8_t out_len = pkt->writeTo(&out_frame[1]);
        releasePacket(pkt); // undo the obtainNewPacket()
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // Error
      }
    } else {
      uint8_t *pub_key = &cmd_frame[1];
      ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      uint8_t out_len;
      if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // not found
      }
    }
  } else if (cmd_frame[0] == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (importContact(&cmd_frame[1], len - 1)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {
    int out_len;
    if ((out_len = getFromOfflineQueue(out_frame)) > 0) {
      _serial->writeFrame(out_frame, out_len);
#ifdef DISPLAY_CLASS
      if (_ui) _ui->msgRead(offline_queue_len);
#endif
    } else {
      out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
      _serial->writeFrame(out_frame, 1);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS) {
    int i = 1;
    uint32_t freq;
    memcpy(&freq, &cmd_frame[i], 4);
    i += 4;
    uint32_t bw;
    memcpy(&bw, &cmd_frame[i], 4);
    i += 4;
    uint8_t sf = cmd_frame[i++];
    uint8_t cr = cmd_frame[i++];
    uint8_t repeat = 0;  // default - false
    if (len > i) {
      repeat = cmd_frame[i++];   // FIRMWARE_VER_CODE  9+
    }

    if (repeat && !isValidClientRepeatFreq(freq)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.client_repeat = repeat;
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = power;
      savePrefs();
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SET_TUNING_PARAMS) {
    int i = 1;
    uint32_t rx, af;
    memcpy(&rx, &cmd_frame[i], 4);
    i += 4;
    memcpy(&af, &cmd_frame[i], 4);
    i += 4;
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_TUNING_PARAMS) {
    uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
    int i = 0;
    out_frame[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out_frame[i], &rx, 4); i += 4;
    memcpy(&out_frame[i], &af, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SET_OTHER_PARAMS) {
    _prefs.manual_add_contacts = cmd_frame[1];
    if (len >= 3) {
      _prefs.telemetry_mode_base = cmd_frame[2] & 0x03; // v5+
      _prefs.telemetry_mode_loc = (cmd_frame[2] >> 2) & 0x03;
      _prefs.telemetry_mode_env = (cmd_frame[2] >> 4) & 0x03;

      if (len >= 4) {
        _prefs.advert_loc_policy = cmd_frame[3];
        if (len >= 5) {
          _prefs.multi_acks = cmd_frame[4];
        }
      }
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_PATH_HASH_MODE && cmd_frame[1] == 0 && len >= 3) {
    if (cmd_frame[2] >= 3) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.path_hash_mode = cmd_frame[2];
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
    if (dirty_contacts_expiry) { // is there are pending dirty contacts write needed?
      saveContacts();
    }
    board.reboot();
  } else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {
    uint8_t reply[11];
    int i = 0;
    reply[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used = _store->getStorageUsedKb();
    uint32_t total = _store->getStorageTotalKb();
    memcpy(&reply[i], &battery_millivolts, 2); i += 2;
    memcpy(&reply[i], &used, 4); i += 4;
    memcpy(&reply[i], &total, 4); i += 4;
    _serial->writeFrame(reply, i);
  } else if (cmd_frame[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    uint8_t reply[65];
    reply[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&reply[1], 64);
    _serial->writeFrame(reply, 65);
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&cmd_frame[1])) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
    } else {
        mesh::LocalIdentity identity;
        identity.readFrom(&cmd_frame[1], 64);
        if (_store->saveMainIdentity(identity)) {
          self_id = identity;
          writeOKFrame();
          // re-load contacts, to invalidate ecdh shared_secrets
          resetContacts();
          _store->loadContacts(this);
        } else {
          writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
    }
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_SEND_RAW_DATA && len >= 6) {
    int i = 1;
    int8_t path_len = cmd_frame[i++];
    if (path_len >= 0 && i + path_len + 4 <= len) { // minimum 4 byte payload
      uint8_t *path = &cmd_frame[i];
      i += path_len;
      auto pkt = createRawData(&cmd_frame[i], len - i);
      if (pkt) {
        sendDirect(pkt, path, path_len);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    } else {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
    }
  } else if (cmd_frame[0] == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    char *password = (char *)&cmd_frame[1 + PUB_KEY_SIZE];
    cmd_frame[len] = 0; // ensure null terminator in password
    if (recipient) {
      uint32_t est_timeout;
      int result = sendLogin(*recipient, password, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &pending_login, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    ContactInfo anon;
    if (recipient == NULL) { // FIRMWARE_VER_CODE 13+,  allow non-contact requests
      memset(&anon, 0, sizeof(anon));
      memcpy(anon.id.pub_key, pub_key, PUB_KEY_SIZE);
      anon.out_path_len = 0;   // default to zero-hop direct
      anon.type = ADV_TYPE_NONE;  // unknown

      if (addContact(anon)) recipient = &anon;
    }
    uint8_t *data = &cmd_frame[1 + PUB_KEY_SIZE];
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendAnonReq(*recipient, data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // contacts full
    }
  } else if (cmd_frame[0] == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        // FUTURE:  pending_status = tag;  // match this in onContactResponse()
        memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_PATH_DISCOVERY_REQ && cmd_frame[1] == 0 && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[2];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      // 'Path Discovery' is just a special case of flood + Telemetry req
      uint8_t req_data[9];
      req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
      req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
      memset(&req_data[2], 0, 3);  // reserved
      getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
      auto save = recipient->out_path_len;    // temporarily force sendRequest() to flood
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      int result = sendRequest(*recipient, req_data, sizeof(req_data), tag, est_timeout);
      recipient->out_path_len = save;
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_discovery = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = &cmd_frame[4];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len == 4) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint8_t *req_data = &cmd_frame[1 + PUB_KEY_SIZE];
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, req_data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    if (hasConnectionTo(pub_key)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    stopConnection(pub_key);
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_CHANNEL && len >= 2) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    if (getChannel(channel_idx, channel)) {
      int i = 0;
      out_frame[i++] = RESP_CODE_CHANNEL_INFO;
      out_frame[i++] = channel_idx;
      strcpy((char *)&out_frame[i], channel.name);
      i += 32;
      memcpy(&out_frame[i], channel.channel.secret, 16);
      i += 16; // NOTE: only 128-bit supported
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // not supported (yet)
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 16); // NOTE: only 128-bit supported
    if (setChannel(channel_idx, channel)) {
      saveChannels();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SIGN_START) {
    out_frame[0] = RESP_CODE_SIGN_START;
    out_frame[1] = 0; // reserved
    uint32_t len = MAX_SIGN_DATA_LEN;
    memcpy(&out_frame[2], &len, 4);
    _serial->writeFrame(out_frame, 6);

    if (sign_data) {
      free(sign_data);
    }
    sign_data = (uint8_t *)malloc(MAX_SIGN_DATA_LEN);
    sign_data_len = 0;
  } else if (cmd_frame[0] == CMD_SIGN_DATA && len > 1) {
    if (sign_data == NULL || sign_data_len + (len - 1) > MAX_SIGN_DATA_LEN) {
      writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
    } else {
      memcpy(&sign_data[sign_data_len], &cmd_frame[1], len - 1);
      sign_data_len += (len - 1);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SIGN_FINISH) {
    if (sign_data) {
      self_id.sign(&out_frame[1], sign_data, sign_data_len);

      free(sign_data); // don't need sign_data now
      sign_data = NULL;

      out_frame[0] = RESP_CODE_SIGNATURE;
      _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
    } else {
      writeErrFrame(ERR_CODE_BAD_STATE);
    }
  } else if (cmd_frame[0] == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD-5) {
    uint8_t path_len = len - 10;
    uint8_t flags = cmd_frame[9];
    uint8_t path_sz = flags & 0x03;  // NEW v1.11+
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      uint32_t tag, auth;
      memcpy(&tag, &cmd_frame[1], 4);
      memcpy(&auth, &cmd_frame[5], 4);
      auto pkt = createTrace(tag, auth, flags);
      if (pkt) {
        sendDirect(pkt, &cmd_frame[10], path_len);

        uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
        uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len >> path_sz);

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_SET_DEVICE_PIN && len >= 5) {

    // get pin from command frame
    uint32_t pin;
    memcpy(&pin, &cmd_frame[1], 4);

    // ensure pin is zero, or a valid 6 digit pin
    if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
      _prefs.ble_pin = pin;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE+2) {
    // FUTURE use:  uint8_t reserved = cmd_frame[1];
    uint8_t *pub_key = &cmd_frame[2];
    AdvertPath* found = NULL;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
        found = p;
        break;
      }
    }
    if (found) {
      int i = 0;
      out_frame[i++] = RESP_CODE_ADVERT_PATH;
      memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
      out_frame[i++] = found->path_len;
      i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = cmd_frame[1];
    if (stats_type == STATS_TYPE_CORE) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = _ms->getMillis() / 1000;
      uint8_t queue_len = (uint8_t)_mgr->getOutboundTotal();
      memcpy(&out_frame[i], &battery_mv, 2); i += 2;
      memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
      memcpy(&out_frame[i], &_err_flags, 2); i += 2;
      out_frame[i++] = queue_len;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_RADIO) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out_frame[i], &noise_floor, 2); i += 2;
      out_frame[i++] = last_rssi;
      out_frame[i++] = last_snr;
      memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
      memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_PACKETS) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
      memcpy(&out_frame[i], &recv, 4); i += 4;
      memcpy(&out_frame[i], &sent, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_errors, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
    }
  } else if (cmd_frame[0] == CMD_FACTORY_RESET && memcmp(&cmd_frame[1], "reset", 5) == 0) {
    if (_serial) {
      MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
      _serial->disable(); // Phone app disconnects before we can send OK frame so it's safe here
    }
    bool success = _store->formatFileSystem();
    if (success) {
      writeOKFrame();
      delay(1000);
      board.reboot();  // doesn't return
    } else {
      writeErrFrame(ERR_CODE_FILE_IO_ERROR);
    }
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 0) {
    if (len >= 2 + 16) {
      memcpy(send_scope.key, &cmd_frame[2], sizeof(send_scope.key));  // set scope override TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // reset scope override
    }
    send_unscoped = false;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 1) {  // ver 12+
    send_unscoped = true;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_DEFAULT_FLOOD_SCOPE && len >= 1) {
    if (len >= 1+31+16) {
      int n = strlen((char *) &cmd_frame[1]);
      if (n > 0 && n < 31) {
        strcpy(_prefs.default_scope_name, (char *) &cmd_frame[1]);
        memcpy(_prefs.default_scope_key, &cmd_frame[1+31], 16);
        savePrefs();
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));  // set default scope to null
      memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_GET_DEFAULT_FLOOD_SCOPE) {
    out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
    if (strlen(_prefs.default_scope_name) > 0) {
      memcpy(&out_frame[1], _prefs.default_scope_name, 31);
      memcpy(&out_frame[1+31], _prefs.default_scope_key, 16);
      _serial->writeFrame(out_frame, 1+31+16);
    } else {
      _serial->writeFrame(out_frame, 1);   // no name or key means null
    }
  } else if (cmd_frame[0] == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame[1] & 0x80) != 0) {
    auto resp = createControlData(&cmd_frame[1], len - 1);
    if (resp) {
      sendZeroHop(resp);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_SET_AUTOADD_CONFIG) {
    _prefs.autoadd_config = cmd_frame[1];
    if (len >= 3) {
      _prefs.autoadd_max_hops = min(cmd_frame[2], (uint8_t)64);
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_AUTOADD_CONFIG) {
    int i = 0;
    out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
    out_frame[i++] = _prefs.autoadd_config;
    out_frame[i++] = _prefs.autoadd_max_hops;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_GET_ALLOWED_REPEAT_FREQ) {
    int i = 0;
    out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
    for (int k = 0; k < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]) && i + 8 < sizeof(out_frame); k++) {
      auto r = &repeat_freq_ranges[k];
      memcpy(&out_frame[i], &r->lower_freq, 4); i += 4;
      memcpy(&out_frame[i], &r->upper_freq, 4); i += 4;
    }
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_RAW_PACKET && len >= 4) {
    auto pkt = obtainNewPacket();
    if (pkt) {
      uint8_t priority = cmd_frame[1];
      if (tryParsePacket(pkt, &cmd_frame[2], len - 2)) {
        sendPacket(pkt, priority, 0);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
  }
}

static bool save_filter(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE;   // don't save the transient/anon entries
}

void MyMesh::saveContacts() {
  _store->saveContacts(this, save_filter);
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      cli_command[len++] = c;
      cli_command[len] = 0;
    }
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1) {  // command buffer full
    cli_command[sizeof(cli_command)-1] = '\r';
  }

  if (len > 0 && cli_command[len - 1] == '\r') {  // received complete line
    cli_command[len - 1] = 0;  // replace newline with C string null terminator

    if (memcmp(cli_command, "bot", 3) == 0 && (cli_command[3] == 0 || cli_command[3] == ' ')) {
#if CMESH_BOT_ENABLED
      if (!handleBotCLI(&cli_command[3])) Serial.println("  Error: unknown bot command");
#else
      Serial.println("  Error: bot support is disabled in this build");
#endif
    } else if (memcmp(cli_command, "set ", 4) == 0) {
      const char* config = &cli_command[4];
      if (memcmp(config, "pin ", 4) == 0) {
        _prefs.ble_pin = atoi(&config[4]);
        savePrefs();
        Serial.printf("  > pin is now %06d\n", _prefs.ble_pin);
      } else {
        Serial.printf("  Error: unknown config: %s\n", config);
      }
    } else if (strcmp(cli_command, "rebuild") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        Serial.println("  > erase and rebuild done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (strcmp(cli_command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        Serial.println("  > erase done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (memcmp(cli_command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = &cli_command[3];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      Serial.printf("Listing files in %s\n", path);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  UserData%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  ExtraFS%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(cli_command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = &cli_command[4];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        Serial.println("Invalid path provided, must start with UserData/ or ExtraFS/");
        cli_command[0] = 0;
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(Serial, buffer, file_size);
        Serial.print("\n");

        file.close();

      }

    } else if (memcmp(cli_command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &cli_command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        Serial.println("Invalid path provided");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          Serial.println("File removed");
        } else {
          Serial.println("Failed to remove file");
        }

      }

    } else if (strcmp(cli_command, "reboot") == 0) {
      board.reboot();  // doesn't return
    } else {
      Serial.println("  Error: unknown command");
    }

    cli_command[0] = 0;  // reset command buffer
  }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    handleCmdFrame(len);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    bool found = false;
    while (_iter.hasNext(this, contact)) {
      if (contact.type != ADV_TYPE_NONE) {
        found = true;
        break;
      }
    }

    if (found) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

void MyMesh::loop() {
  BaseChatMesh::loop();

  if (_cli_rescue) {
    checkCLIRescueCmd();
  } else {
    checkSerialInterface();
  }

  // is there are pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    saveContacts();
    dirty_contacts_expiry = 0;
  }

#if CMESH_BOT_ENABLED
  tickBot();
#endif

#ifdef DISPLAY_CLASS
  if (_ui) _ui->setHasConnection(_serial->isConnected());
#endif
}

bool MyMesh::advert() {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
  return _mgr->getOutboundTotal() > 0 || dirty_contacts_expiry != 0;
}
