#pragma once
#include "BotPath.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
namespace BotPathLookup {
struct Status {
  const char* stage="No path lookup yet";
  int detail=0;
  const char* lastFailure="";
  int lastFailureDetail=0;
  uint32_t failureHeap=0, failureBlock=0;
  uint32_t failureUtc=0, verifyFlags=0;
  char verifyInfo[256]{};
  uint32_t freeHeap=0, largestBlock=0;
};
Status status();
// Main-loop-only API. The worker owns its cache and never accesses mesh state.
bool start(const BotPath::Route& route);
bool take(BotPath::Route& route);
}
#endif
