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
uint32_t successSequence();
void setInternetAvailable(bool available);
// Main-loop-only API. The worker owns its cache and never accesses mesh state.
struct HistoryEntry {
  char requester[33]{};
  char channel[BOT_MAX_CHANNEL_NAME_LEN+1]{};
  const char* stage="Starting lookup";
  const char* failure="";
  int detail=0, failureDetail=0;
  uint32_t timestamp=0, startedMillis=0, elapsedMs=0, minHeap=0, minBlock=0;
  bool finished=false;
};
bool history(size_t index, HistoryEntry& entry);
void recordEvent(const char* requester, const char* outcome, const char* channel="");
inline size_t lookupHopCount(const BotPath::Route& route) { return route.count<5 ? route.count : 5; }
bool start(const BotPath::Route& route, const char* requester="", const char* channel="");
bool take(BotPath::Route& route);
// Main loop only: serialize dashboard JSON generation against the lookup worker.
bool inProgress();
}
#endif
