#pragma once
#include <stdint.h>
namespace BotAnalyzerClock {
enum Result : uint8_t { NotChecked, Good, NoData, HttpFailed, InvalidData, AllocationFailed };
struct Summary {
  uint32_t checkedAt=0, attemptDay=0, fetchedAt=0, advertAt=0, observedAt=0, samples=0;
  float skew=0, drift=0;
  uint8_t severity=0, result=NotChecked;
  bool calibrated=false;
};
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
const char* severityName(uint8_t severity);
const char* resultName(uint8_t result);
bool busy();
bool start(const uint8_t key[32]);
bool take(uint8_t key[32],Summary& summary);
#endif
}
