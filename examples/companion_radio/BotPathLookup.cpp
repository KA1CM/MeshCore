#include "BotInternetProbe.h"
#include "BotPathLookup.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "BotPathTrust.h"
#include "BotPathLookupResult.h"
#include "BotLookupBody.h"
#include "BotSnapshotFile.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/platform.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <time.h>
#include <new>
#include <lwip/netdb.h>
#include <errno.h>

namespace BotPathLookup {
namespace {
portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;
Status lastStatus;
HistoryEntry lookupHistory[30];
size_t historyNext=0, historyCount=0;
int historyActive=-1;
uint32_t tlsPsramAllocations=0; // Worker-owned; copied into Status at lookup stages.
uint32_t internalFree() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); }
uint32_t internalBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); }
void beginHistory(const char* requester, const char* channel="") {
  const uint32_t utc=(uint32_t)time(nullptr), ticks=millis(), heap=internalFree(), block=internalBlock();
  portENTER_CRITICAL(&statusMux);
  historyActive=(int)historyNext;
  auto& entry=lookupHistory[historyNext];entry=HistoryEntry{};
  snprintf(entry.requester,sizeof(entry.requester),"%s",requester && *requester ? requester : "Unknown user");
  snprintf(entry.channel,sizeof(entry.channel),"%s",channel && *channel ? channel : "Unknown channel");
  entry.timestamp=utc;entry.startedMillis=ticks;entry.minHeap=heap;entry.minBlock=block;
  historyNext=(historyNext+1)%30;if(historyCount<30)++historyCount;
  portEXIT_CRITICAL(&statusMux);
}
void finishHistory() {
  const uint32_t ticks=millis();
  portENTER_CRITICAL(&statusMux);
  if(historyActive>=0){auto& e=lookupHistory[historyActive];e.finished=true;e.elapsedMs=ticks-e.startedMillis;historyActive=-1;}
  portEXIT_CRITICAL(&statusMux);
}
void report(const char* stage,int detail=0,bool failure=false,uint32_t verifyFlags=0) {
  Status next; next.stage=stage; next.detail=detail;
  if(failure) {
    next.failureUtc=(uint32_t)time(nullptr);
    next.verifyFlags=verifyFlags;
    if(verifyFlags) mbedtls_x509_crt_verify_info(next.verifyInfo,sizeof(next.verifyInfo),"",verifyFlags);
  }
  next.freeHeap=internalFree(); next.largestBlock=internalBlock();
  next.psramFree=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  next.psramBlock=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  next.tlsPsramAllocations=tlsPsramAllocations;
  portENTER_CRITICAL(&statusMux);
  next.lastFailure=failure ? stage : lastStatus.lastFailure;
  next.lastFailureDetail=failure ? detail : lastStatus.lastFailureDetail;
  next.failureHeap=failure ? next.freeHeap : lastStatus.failureHeap;
  next.failureBlock=failure ? next.largestBlock : lastStatus.failureBlock;
  if(!failure) {
    next.failureUtc=lastStatus.failureUtc; next.verifyFlags=lastStatus.verifyFlags;
    memcpy(next.verifyInfo,lastStatus.verifyInfo,sizeof(next.verifyInfo));
  }
  if(historyActive>=0) {
    auto& e=lookupHistory[historyActive];e.stage=stage;e.detail=detail;e.elapsedMs=millis()-e.startedMillis;
    if(next.freeHeap<e.minHeap)e.minHeap=next.freeHeap;
    if(next.largestBlock<e.minBlock)e.minBlock=next.largestBlock;
    if(failure){e.failure=stage;e.failureDetail=detail;}
  }
  lastStatus=next;
  portEXIT_CRITICAL(&statusMux);
}
// Resolve in the worker with the socket API, avoiding WiFi.hostByName's shared
// event-bit/callback state. Preserve the original host for SNI and certificate
// verification when connecting to the resolved address. No DNS settings change.
class LookupClient : public WiFiClientSecure {
public:
  bool connectionFailed=false;
  int connect(const char* host,uint16_t port,int32_t timeout) override {
    connectionFailed=false;
    report("Resolving analyzer hostname");
    struct addrinfo hints{};
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    struct addrinfo* addresses=nullptr;
    const int dnsError=getaddrinfo(host,nullptr,&hints,&addresses);
    if(dnsError || !addresses || !addresses->ai_addr) {
      if(addresses) freeaddrinfo(addresses);
      report("DNS resolution failed",dnsError,true); connectionFailed=true; return 0;
    }
    const auto* address=reinterpret_cast<const struct sockaddr_in*>(addresses->ai_addr);
    const IPAddress ip(address->sin_addr.s_addr);
    freeaddrinfo(addresses);
    _timeout=timeout;
    report("Connecting with verified TLS");
    errno=0;
    // Mirror the pinned Arduino secure-client connect, but record verification
    // flags before stop() frees the TLS context. Verification remains required.
    const uint32_t connectStarted=millis();
    const int result=start_ssl_client(sslclient,ip,port,host,_timeout,_CA_cert,
      _use_ca_bundle,_cert,_private_key,nullptr,nullptr,_use_insecure,_alpn_protos);
    _lastError=result;
    const int connected=result>=0;
    if(!connected) {
      const int socketError=errno;
      const uint32_t flags=result==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ?
        mbedtls_ssl_get_verify_result(&sslclient->ssl_ctx) : 0;
      // The pinned SDK frees/closes the socket on TCP connect failure. A -1
      // with a live TLS context after the configured deadline is its handshake timeout.
      const uint32_t elapsed=millis()-connectStarted;
      const bool handshakeTimeout=result==-1 && sslclient->socket>=0 &&
        sslclient->ssl_ctx.state!=MBEDTLS_SSL_HELLO_REQUEST &&
        elapsed>=sslclient->handshake_timeout;
      report(handshakeTimeout ? "TLS handshake timed out (milliseconds)" :
             result==MBEDTLS_ERR_SSL_ALLOC_FAILED ? "TLS allocation failed" :
             result==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? "TLS certificate verification failed" :
             result < -1 ? "TLS handshake failed" : "TCP connection failed",
             handshakeTimeout ? (int)elapsed : result < -1 ? result : socketError,true,flags);
      stop();
      connectionFailed=true;
    } else _connected=true;
    return connected;
  }
};
// Let HTTPClient decode transfer framing, while limiting the decoded payload.
class BodyStream : public Stream {
  uint32_t started=millis();
public:
  Body body;
  size_t write(uint8_t byte) override { return write(&byte,1); }
  size_t write(const uint8_t* bytes,size_t count) override {
    if(millis()-started>6000) return 0;
    return body.append(bytes,count);
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};
struct Cached {
  char prefix[7]{}, name[97]{};
  uint32_t expires=0, expiresUtc=0;
  bool ambiguous=false;
};
constexpr size_t CacheCapacity=128;
constexpr uint32_t PositiveCacheSeconds=100UL*60*60;
Cached* cache=nullptr; // Allocated once, preferably outside TLS's internal heap.
static_assert(sizeof(Cached)*CacheCapacity<=16384,"Bound lookup name cache memory");
bool ensureCache() {
  if(cache) return true;
  auto* memory=static_cast<Cached*>(allocateBulk(sizeof(Cached)*CacheCapacity));
  if(!memory) return false;
  for(size_t i=0;i<CacheCapacity;++i) new(memory+i) Cached{};
  cache=memory;
  return true;
}
size_t nextCache=0;
struct SavedCacheEntry { char prefix[7], name[97]; uint32_t expiresUtc; };
struct SavedCache {
  uint32_t version,count,savedUtc,savedDay;
  SavedCacheEntry entries[CacheCapacity];
};
BotSnapshotFile cacheSnapshots{"/pathnames0.bin","/pathnames1.bin"};
bool cacheRestored=false,cacheSaveAttempted=false;
uint32_t cacheDailyDay=0,cacheLastSaveUtc=0,cacheLastAttempt=0,cacheRestoreRetry=0;
const char* cacheSaveError="NTP not confirmed";
uint32_t nextFetch=0;
uint32_t successfulRequests=0;
struct InternetPolicy { bool available=true;uint32_t recovery=0; } internetPolicy;
uint32_t workerRecovery=0; // worker-owned, just like cache/nextFetch
InternetPolicy internetPolicySnapshot() {
  portENTER_CRITICAL(&statusMux);auto result=internetPolicy;portEXIT_CRITICAL(&statusMux);return result;
}
bool throttled=false, busy=false;
bool historyStoragePending=false; // Main loop only; existing worker may finish.
QueueHandle_t requests=nullptr, completed=nullptr;
TaskHandle_t workerTask=nullptr;
#if defined(CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) && defined(MBEDTLS_PLATFORM_MEMORY) && \
    !defined(MBEDTLS_PLATFORM_CALLOC_MACRO) && !defined(MBEDTLS_PLATFORM_FREE_MACRO)
// Install once before TLS starts; never swap global allocators around a request.
// Only this worker's large TLS allocations prefer PSRAM. Everything else keeps
// the pinned SDK's internal-RAM allocation policy and compatible heap free.
void* tlsCalloc(size_t count,size_t size) {
  if(size && count>SIZE_MAX/size) return nullptr;
  if(workerTask && xTaskGetCurrentTaskHandle()==workerTask && count*size>=16384) {
    if(void* memory=heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)) {
      ++tlsPsramAllocations;
      return memory;
    }
  }
  return heap_caps_calloc(count,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}
void tlsFree(void* memory) { heap_caps_free(memory); }
bool installTlsAllocator() {
  static bool installed=false;
  if(!installed) installed=mbedtls_platform_set_calloc_free(tlsCalloc,tlsFree)==0;
  return installed;
}
#else
bool installTlsAllocator() { return true; } // Preserve other SDK configurations.
#endif
// Reserve the worker's internal stack at startup instead of carving a new
// 12 KB block out of the heap for every TLS request.
constexpr size_t WorkerStackBytes=12288;
alignas(16) StackType_t workerStack[WorkerStackBytes/sizeof(StackType_t)];
static_assert(sizeof(workerStack)==WorkerStackBytes,"Lookup stack must remain 12 KB");
StaticTask_t workerControl;
StaticQueue_t requestsControl, completedControl;
uint8_t requestsStorage[sizeof(BotPath::Route*)], completedStorage[sizeof(BotPath::Route*)];
bool future(uint32_t deadline) { return (int32_t)(millis()-deadline)<0; }
void resolve(BotPath::Route* route) {
  bool missing[BOT_MAX_PATH_BYTES]{};
  String hops;
  // Cached identities are cheap and apply to the entire route. Only new
  // internet requests retain the ordinary PATH first-five-hop restriction.
  for(size_t i=0;i<route->count;++i) {
    if(route->names[i][0] || route->ambiguous[i]) continue;
    char prefix[9]; BotPath::hash(*route,i,prefix);
    bool found=false;
    for(size_t j=0;j<CacheCapacity;++j) {
      const auto& item=cache[j];
      if(!strcmp(item.prefix,prefix) && future(item.expires)) {
        BotPath::shortName(item.name,route->names[i]); route->ambiguous[i]=item.ambiguous;
        found=true; break;
      }
    }
    if(found) continue;
    if(i>=lookupHopCount(*route)) continue;
    missing[i]=true;
    if(hops.length()) hops+=',';
    hops+=prefix;
  }
  if(!hops.length()) { report("Using cached names"); return; }
  const auto policy=internetPolicySnapshot();
  if(!policy.available) {report("Internet unavailable; using local/cached names");return;}
  if(policy.recovery!=workerRecovery) {
    // A confirmed recovery supersedes the failed lookup's old retry backoff.
    workerRecovery=policy.recovery;throttled=false;nextFetch=0;
  }
  if(throttled && future(nextFetch)) {
    const uint32_t wait=nextFetch-millis();
    // Wait out the short success pause in this worker, never in the radio loop.
    // Keep failure backoff as a fallback rather than holding the queue for a minute.
    if(wait>5000) { report("Waiting before retry"); return; }
    report("Waiting for lookup interval");
    while(future(nextFetch)) {
      if(!internetPolicySnapshot().available){report("Internet unavailable; using local/cached names");return;}
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  if(!internetPolicySnapshot().available){report("Internet unavailable; using local/cached names");return;}
  if(WiFi.status()!=WL_CONNECTED) { report("Wi-Fi disconnected",0,true); return; }
  if(time(nullptr)<=1700000000) { report("System clock not ready for HTTPS",0,true); return; }
  // No radio, contact, or dashboard state is touched from this task.
  if(hops.length() && (!throttled || !future(nextFetch)) &&
     WiFi.status()==WL_CONNECTED && time(nullptr)>1700000000) {
    throttled=true; nextFetch=millis()+60000; // Failure backoff.
    LookupClient client;
    client.setCACert(BOT_PATH_ROOT_CA);
    client.setHandshakeTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(4000); http.setTimeout(4000);
    http.useHTTP10(true); // The analyzer may close-delimit the body (no length header).
    const String url=String("https://analyzer.ctmesh.org/api/resolve-hops?hops=")+hops;
    report("Connecting to analyzer");
    if(http.begin(client,url)) {
      const int status=http.GET();
      if(status<0 && !client.connectionFailed) {
        char tlsMessage[160]{};
        const int tlsError=client.lastError(tlsMessage,sizeof(tlsMessage));
        report(tlsError ? "TLS connection failed" : "HTTP connection failed",tlsError ? tlsError : status,true);
      } else if(status>=0) report("Analyzer HTTP response",status,status!=200);
      const int length=http.getSize();
      if(Body::accepts(status,length)) {
        BodyStream stream;
        report("Reading analyzer response");
        const int transferred=http.writeToStream(&stream);
        JsonDocument doc;
        const bool complete=stream.body.complete(length,transferred);
        if(!complete) report("Response read failed or truncated",transferred,true);
        DeserializationError jsonError;
        if(complete) { jsonError=deserializeJson(doc,stream.body.data(),stream.body.size()); if(jsonError) report("Analyzer JSON invalid",(int)jsonError.code(),true); }
        if(complete && !jsonError) {
          JsonObjectConst resolved=doc["resolved"].as<JsonObjectConst>();
          if(resolved.isNull()) report("Analyzer missing resolved object",0,true);
          if(!resolved.isNull()) {
            unsigned matched=0;
            nextFetch=millis()+5000;
            portENTER_CRITICAL(&statusMux);++successfulRequests;portEXIT_CRITICAL(&statusMux);
            for(size_t i=0;i<lookupHopCount(*route);++i) if(missing[i]) {
              char prefix[9]; BotPath::hash(*route,i,prefix);
              JsonObjectConst entry=resolved[prefix].as<JsonObjectConst>();
              Cached result{};
              snprintf(result.prefix,sizeof(result.prefix),"%s",prefix);
              // Cache missing entries as unresolved too, avoiding repeated
              // background requests for a prefix absent from the analyzer.
              readEntry(entry,prefix,result.name,result.ambiguous,true,sizeof(result.name));
              // Refuse the analyzer's heuristic best pick for colliding prefixes.
              // Keep resolved names for 100 hours; retry missing/ambiguous
              // identities after 15 minutes so new analyzer data can be found.
              const bool positive=result.name[0] && !result.ambiguous;
              result.expires=millis()+(positive ? PositiveCacheSeconds*1000 : 900000UL);
              result.expiresUtc=positive ? (uint32_t)time(nullptr)+PositiveCacheSeconds : 0;
              cache[nextCache++%CacheCapacity]=result;
              BotPath::shortName(result.name,route->names[i]); route->ambiguous[i]=result.ambiguous;
              if(result.name[0]) ++matched;
            }
            report("Names resolved",matched);
          }
        }
      }
      if(status==200 && !Body::accepts(status,length)) report("Response size rejected",length,true);
      http.end();
    } else report("Could not initialize HTTPS",0,true);
  }
}
void worker(void*) {
  for(;;) {
    BotPath::Route* route=nullptr;
    if(xQueueReceive(requests,&route,portMAX_DELAY)!=pdTRUE) continue;
    resolve(route); // Destroy per-request Strings/HTTP/JSON before publishing.
    finishHistory();
    xQueueSend(completed,&route,portMAX_DELAY);
    // Block on the next request; retain the same task and stack for its lifetime.
  }
}
// Main loop only, while busy==false. Large snapshot staging stays off the
// radio receive stack and prefers PSRAM, like the live cache itself.
bool restoreNameCache(uint32_t utc) {
  if(cacheRestored) return true;
  if(!ensureCache()) { cacheSaveError="cache allocation failed"; return false; }
  std::unique_ptr<char[],FreeBulk> memory(static_cast<char*>(allocateBulk(sizeof(SavedCache))));
  if(!memory) { cacheSaveError="snapshot allocation failed"; return false; }
  auto* saved=new(memory.get()) SavedCache;
  const auto loaded=cacheSnapshots.load(saved,sizeof(*saved));
  if(loaded==BotSnapshotFile::Unavailable) { cacheSaveError=cacheSnapshots.lastError(); return false; }
  cacheRestored=true; // Missing/invalid optional cache must not disable lookups.
  if(loaded==BotSnapshotFile::Missing) { cacheSaveError=""; return true; }
  if(loaded!=BotSnapshotFile::Loaded) { cacheSaveError=cacheSnapshots.lastError(); return true; }
  bool valid=saved->version==1 && saved->count<=CacheCapacity &&
    saved->savedUtc>=1700000000UL && saved->savedUtc<=utc && saved->savedDay<=utc/86400UL;
  for(size_t i=0;valid && i<saved->count;++i) {
    const auto& entry=saved->entries[i];
    valid=memchr(entry.prefix,0,sizeof(entry.prefix)) && memchr(entry.name,0,sizeof(entry.name)) && entry.name[0];
    if(!valid) break;
    const size_t length=strlen(entry.prefix);
    valid=(length==4 || length==6) && entry.expiresUtc>saved->savedUtc &&
      entry.expiresUtc-saved->savedUtc<=PositiveCacheSeconds;
    for(size_t k=0;valid && k<length;++k)
      valid=(entry.prefix[k]>='0' && entry.prefix[k]<='9') || (entry.prefix[k]>='a' && entry.prefix[k]<='f');
    for(size_t k=0;valid && entry.name[k];++k) valid=(uint8_t)entry.name[k]>=32 && entry.name[k]!=127;
    for(size_t k=0;valid && k<i;++k) valid=strcmp(entry.prefix,saved->entries[k].prefix)!=0;
  }
  if(!valid) { cacheSaveError="cache snapshot invalid"; return true; }
  cacheDailyDay=saved->savedDay;cacheLastSaveUtc=saved->savedUtc;
  for(size_t i=0;i<saved->count;++i) {
    const auto& entry=saved->entries[i];
    if(entry.expiresUtc<=utc) continue;
    bool present=false;
    for(size_t j=0;j<CacheCapacity;++j)
      if(!strcmp(cache[j].prefix,entry.prefix) && future(cache[j].expires)) { present=true;break; }
    if(present) continue; // Preserve any fresher in-memory lookup result.
    auto& item=cache[nextCache++%CacheCapacity];
    memcpy(item.prefix,entry.prefix,sizeof(item.prefix));memcpy(item.name,entry.name,sizeof(item.name));
    item.ambiguous=false;item.expiresUtc=entry.expiresUtc;
    item.expires=millis()+(entry.expiresUtc-utc)*1000UL;
  }
  cacheSaveError="";return true;
}
bool writeNameCache(uint32_t utc,uint32_t dailyDay) {
  std::unique_ptr<char[],FreeBulk> memory(static_cast<char*>(allocateBulk(sizeof(SavedCache))));
  if(!memory) { cacheSaveError="snapshot allocation failed"; return false; }
  auto* saved=new(memory.get()) SavedCache;
  memset(saved,0,sizeof(*saved));saved->version=1;saved->savedUtc=utc;saved->savedDay=dailyDay;
  for(size_t i=0;i<CacheCapacity;++i) {
    const auto& item=cache[i];
    if(!item.name[0] || item.ambiguous || !future(item.expires) || item.expiresUtc<=utc ||
       item.expiresUtc-utc>PositiveCacheSeconds) continue;
    // A route may contain the same prefix more than once. Persist one copy,
    // retaining the freshest expiry rather than rejecting it after restart.
    size_t slot=0;
    while(slot<saved->count && strcmp(saved->entries[slot].prefix,item.prefix)) ++slot;
    if(slot<saved->count && saved->entries[slot].expiresUtc>=item.expiresUtc) continue;
    if(slot==saved->count) ++saved->count;
    auto& entry=saved->entries[slot];
    memcpy(entry.prefix,item.prefix,sizeof(entry.prefix));memcpy(entry.name,item.name,sizeof(entry.name));
    entry.expiresUtc=item.expiresUtc;
  }
  if(!cacheSnapshots.save(saved,sizeof(*saved))) { cacheSaveError=cacheSnapshots.lastError(); return false; }
  cacheDailyDay=dailyDay;cacheLastSaveUtc=utc;cacheSaveError="";return true;
}
}
void pollNameCache(uint32_t utc,uint32_t day,bool saveDaily) {
  if(utc<1700000000UL || busy) return; // Caller supplies UTC only after NTP confirmation.
  if(!cacheRestored) {
    if(cacheRestoreRetry && future(cacheRestoreRetry)) return;
    cacheRestoreRetry=millis()+60000;
    if(!restoreNameCache(utc)) return;
  }
  if(!saveDaily || !day || day<=cacheDailyDay ||
     (cacheSaveAttempted && uint32_t(millis()-cacheLastAttempt)<3600000UL)) return;
  cacheSaveAttempted=true;cacheLastAttempt=millis();
  if(writeNameCache(utc,day)) cacheSaveAttempted=false;
}
bool saveNameCacheNow(uint32_t utc) {
  if(utc<1700000000UL) { cacheSaveError="NTP not confirmed"; return false; }
  if(busy) { cacheSaveError="lookup busy; retry"; return false; }
  return restoreNameCache(utc) && writeNameCache(utc,cacheDailyDay);
}
const char* nameCacheSaveError() { return cacheSaveError; }
uint32_t nameCacheLastSavedUtc() { return cacheLastSaveUtc; }
bool begin() {
  if(workerTask) return true;
  if(!installTlsAllocator()) { report("Cannot initialize TLS allocator",0,true); return false; }
  if(!requests) requests=xQueueCreateStatic(1,sizeof(BotPath::Route*),requestsStorage,&requestsControl);
  if(!completed) completed=xQueueCreateStatic(1,sizeof(BotPath::Route*),completedStorage,&completedControl);
  if(!requests || !completed) { report("Cannot initialize lookup queues",0,true); return false; }
  // ESP-IDF's task API specifies stack depth in bytes, unlike vanilla FreeRTOS.
  // A TLS key-exchange step can spend seconds in crypto without blocking.
  // Keep it off the Wi-Fi core (whose idle task is watchdog-monitored).
  // Use normal priority: idle priority can starve behind loopTask long enough
  // for the peer to close the TLS handshake. Equal priority shares CPU time
  // with loopTask through the pinned SDK's preemptive scheduler.
  workerTask=xTaskCreateStaticPinnedToCore(worker,"pathLookup",WorkerStackBytes,nullptr,
    tskIDLE_PRIORITY+1,workerStack,&workerControl,ARDUINO_RUNNING_CORE);
  if(!workerTask) { report("Cannot initialize lookup task",0,true); return false; }
  return true;
}
void setInternetAvailable(bool available) {
  portENTER_CRITICAL(&statusMux);
  if(available && !internetPolicy.available)++internetPolicy.recovery;
  internetPolicy.available=available;
  portEXIT_CRITICAL(&statusMux);
}
uint32_t successSequence() {
  portENTER_CRITICAL(&statusMux);auto result=successfulRequests;portEXIT_CRITICAL(&statusMux);return result;
}
Status status() {
  portENTER_CRITICAL(&statusMux); Status result=lastStatus; portEXIT_CRITICAL(&statusMux);
  return result;
}
void recordEvent(const char* requester, const char* outcome, const char* channel) {
  const uint32_t utc=(uint32_t)time(nullptr), heap=internalFree(), block=internalBlock();
  portENTER_CRITICAL(&statusMux);
  // An active entry can age out after 30 newer events; never update its reused slot.
  if(historyActive==(int)historyNext)historyActive=-1;
  auto& e=lookupHistory[historyNext];e=HistoryEntry{};
  snprintf(e.requester,sizeof(e.requester),"%s",requester && *requester ? requester : "Unknown user");
  snprintf(e.channel,sizeof(e.channel),"%s",channel && *channel ? channel : "Unknown channel");
  e.timestamp=utc;e.stage=outcome;e.finished=true;e.minHeap=heap;e.minBlock=block;
  historyNext=(historyNext+1)%30;if(historyCount<30)++historyCount;
  portEXIT_CRITICAL(&statusMux);
}
bool history(size_t index, HistoryEntry& entry) {
  portENTER_CRITICAL(&statusMux);
  const bool found=index<historyCount;
  if(found)entry=lookupHistory[(historyNext+29-index)%30];
  portEXIT_CRITICAL(&statusMux);return found;
}
bool start(const BotPath::Route& route, const char* requester, const char* channel) {
  if(busy || historyStoragePending || BotInternetProbe::busy()) return false;
  if(!route.count) return false;
  beginHistory(requester,channel);
  if(route.width!=2 && route.width!=3) { report("Unsupported hash width",route.width); finishHistory(); return false; }
  if(size_t(route.count)*route.width>BOT_MAX_PATH_BYTES) { report("Invalid route length",route.count); finishHistory(); return false; }
  bool missing=false;
  for(size_t i=0;i<route.count;++i) if(!route.names[i][0] && !route.ambiguous[i]) missing=true;
  if(!missing) { report("Using local contacts"); finishHistory(); return false; }
  if(!ensureCache()) { report("Cannot allocate name cache",0,true); finishHistory(); return false; }
  if(!begin()) { finishHistory(); return false; }
  auto* copy=new(std::nothrow) BotPath::Route(route);
  if(!copy) { report("Cannot allocate lookup request",0,true); finishHistory(); return false; }
  report("Starting lookup");
  busy=true;
  if(xQueueSend(requests,&copy,0)!=pdTRUE) {
    report("Lookup request queue full",0,true); busy=false; delete copy; finishHistory(); return false;
  }
  return true;
}
bool inProgress() { return busy; }
void setStoragePending(bool pending) { historyStoragePending=pending; }
bool storagePending() { return historyStoragePending; }
bool cachedName(const char* prefix,char* name,size_t capacity,bool& ambiguous) {
  if(!name || !capacity)return false;
  name[0]=0;ambiguous=false;
  if(busy || !prefix || !cache)return false;
  for(size_t j=0;j<CacheCapacity;++j) {
    const auto& item=cache[j];
    if(!strcmp(item.prefix,prefix) && future(item.expires)) {
      snprintf(name,capacity,"%s",item.name);ambiguous=item.ambiguous;return true;
    }
  }
  return false;
}
size_t cachedNameCount() {
  if(busy || !cache) return 0;
  size_t count=0;
  for(size_t i=0;i<CacheCapacity;++i) {
    const auto& item=cache[i];
    if(!item.name[0] || item.ambiguous || !future(item.expires)) continue;
    bool duplicate=false;
    for(size_t j=0;j<i;++j) {
      const auto& previous=cache[j];
      if(previous.name[0] && !previous.ambiguous && future(previous.expires) &&
         !strcmp(previous.prefix,item.prefix)) {duplicate=true;break;}
    }
    if(!duplicate) ++count;
  }
  return count;
}
bool take(BotPath::Route& route) {
  BotPath::Route* result=nullptr;
  if(!busy || xQueueReceive(completed,&result,0)!=pdTRUE) return false;
  route=*result; delete result; busy=false; return true;
}
bool discard() {
  BotPath::Route* result=nullptr;
  if(!busy || xQueueReceive(completed,&result,0)!=pdTRUE)return false;
  delete result;busy=false;return true;
}
}
#endif
