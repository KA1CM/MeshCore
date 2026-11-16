#pragma once
#include "BotPath.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
namespace BotPathLookup {
// Main loop only. Start the reusable worker before Wi-Fi/dashboard allocations.
bool begin();
void setStoragePending(bool pending);
bool storagePending();
// Main loop only; pass confirmed NTP UTC. Never access storage while a lookup runs.
void pollNameCache(uint32_t utc,uint32_t day,bool saveDaily);
bool saveNameCacheNow(uint32_t utc);
const char* nameCacheSaveError();
uint32_t nameCacheLastSavedUtc();
uint32_t nameCacheSavedDay();
struct Status {
  const char* stage="No path lookup yet";
  int detail=0;
  const char* lastFailure="";
  int lastFailureDetail=0;
  uint32_t failureHeap=0, failureBlock=0;
  uint32_t failureUtc=0;
  uint32_t freeHeap=0, largestBlock=0;
  uint32_t psramFree=0, psramBlock=0;
};
Status status();
uint32_t successSequence();
void setInternetAvailable(bool available);
// Main-loop-only API. The worker owns its cache and never accesses mesh state.
struct HistoryEntry {
  BotCommandId command=BOT_COMMAND_PATH;
  BotReplyScope requestScope=BOT_REPLY_UNSCOPED;
  bool mentioned=false;
  uint32_t id=0;
  const char* reply="";
  char requester[33]{};
  char channel[BOT_MAX_CHANNEL_NAME_LEN+1]{};
  const char* stage="Starting lookup";
  const char* failure="";
  int detail=0, failureDetail=0;
  uint32_t timestamp=0, startedMillis=0, elapsedMs=0, minHeap=0, minBlock=0;
  bool finished=false;
  uint16_t queued=0, transmitted=0, repeated=0, txFailed=0;
  char dmSenderKey[17]{};
  uint32_t dmTimestamp=0;
  bool dmRetry=false, dmInbound=false;
};
bool history(size_t index, HistoryEntry& entry);
uint32_t recordEvent(const char* requester, const char* outcome, const char* channel="", const char* reply="");
// Main loop packet events: 0 queued, 1 transmitted, 2 repeated, 3 TX failed.
void recordDelivery(uint32_t historyId,unsigned event,bool notice);
void recordDmRequest(uint32_t historyId,const uint8_t* key,uint32_t timestamp,bool retry,bool inbound);
void recordCommand(uint32_t historyId,BotCommandId command);
void recordCommandStage(uint32_t historyId,const char* stage);
void recordRequestContext(uint32_t historyId,BotReplyScope scope,bool mentioned);
// Update only the matching request row; never attach an outcome by name alone.
void recordReply(uint32_t historyId,const char* outcome);
// Internet lookup limit only; cached names are checked for every route hop.
constexpr size_t RequestHopLimit=10;
inline size_t lookupHopCount(const BotPath::Route& route) { return route.all || route.count<RequestHopLimit ? route.count : RequestHopLimit; }
bool start(const BotPath::Route& route, const char* requester="", const char* channel="",uint32_t* historyId=nullptr);
bool take(BotPath::Route& route);
// Consume a background result whose names are already in the shared cache.
bool discard();
// Main-loop-only; returns cached positive, negative, or ambiguous results.
// Cache reads are refused while the worker owns it.
bool cachedName(const char* prefix,char* name,size_t capacity,bool& ambiguous);
// Main loop only, while idle: unique, unexpired resolved names (not negative entries).
size_t cachedNameCount();
// Main loop only: serialize dashboard JSON generation against the lookup worker.
bool inProgress();
}
#endif
