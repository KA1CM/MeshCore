#include "BotInternetProbe.h"
#include "BotPathLookup.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "BotPathTrust.h"
#include "BotPathLookupResult.h"
#include "BotLookupBody.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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
void beginHistory(const char* requester, const char* channel="") {
  const uint32_t utc=(uint32_t)time(nullptr), ticks=millis(), heap=ESP.getFreeHeap(), block=ESP.getMaxAllocHeap();
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
  next.freeHeap=ESP.getFreeHeap(); next.largestBlock=ESP.getMaxAllocHeap();
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
  char prefix[7]{}, name[33]{};
  uint32_t expires=0;
  bool ambiguous=false;
};
Cached cache[64]; // Worker-owned. No flash writes and no contact-table imports.
size_t nextCache=0;
uint32_t nextFetch=0;
uint32_t successfulRequests=0;
struct InternetPolicy { bool available=true;uint32_t recovery=0; } internetPolicy;
uint32_t workerRecovery=0; // worker-owned, just like cache/nextFetch
InternetPolicy internetPolicySnapshot() {
  portENTER_CRITICAL(&statusMux);auto result=internetPolicy;portEXIT_CRITICAL(&statusMux);return result;
}
bool throttled=false, busy=false;
QueueHandle_t completed=nullptr;
bool future(uint32_t deadline) { return (int32_t)(millis()-deadline)<0; }
void resolve(BotPath::Route* route) {
  bool missing[BOT_MAX_PATH_BYTES]{};
  String hops;
  for(size_t i=0;i<lookupHopCount(*route);++i) {
    if(route->names[i][0] || route->ambiguous[i]) continue;
    char prefix[9]; BotPath::hash(*route,i,prefix);
    bool found=false;
    for(const auto& item:cache) if(!strcmp(item.prefix,prefix) && future(item.expires)) {
      memcpy(route->names[i],item.name,33); route->ambiguous[i]=item.ambiguous;
      found=true; break;
    }
    if(found) continue;
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
              if(!readEntry(entry,prefix,result.name,result.ambiguous)) continue;
              // Refuse the analyzer's heuristic best pick for colliding prefixes.
              result.expires=millis()+(result.name[0] ? 86400000UL : 900000UL);
              cache[nextCache++%64]=result;
              memcpy(route->names[i],result.name,33); route->ambiguous[i]=result.ambiguous;
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
void worker(void* argument) {
  auto* route=static_cast<BotPath::Route*>(argument);
  resolve(route); // Destroy Strings/HTTP/JSON objects before deleting the task.
  finishHistory();
  xQueueSend(completed,&route,portMAX_DELAY);
  vTaskDelete(nullptr);
}
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
  const uint32_t utc=(uint32_t)time(nullptr), heap=ESP.getFreeHeap(), block=ESP.getMaxAllocHeap();
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
  if(busy || BotInternetProbe::busy()) return false;
  if(!route.count) return false;
  beginHistory(requester,channel);
  if(route.width!=2 && route.width!=3) { report("Unsupported hash width",route.width); finishHistory(); return false; }
  bool missing=false;
  for(size_t i=0;i<lookupHopCount(route);++i) if(!route.names[i][0] && !route.ambiguous[i]) missing=true;
  if(!missing) { report("Using local contacts"); finishHistory(); return false; }
  if(!completed) completed=xQueueCreate(1,sizeof(BotPath::Route*));
  if(!completed) { report("Cannot allocate lookup queue",0,true); finishHistory(); return false; }
  auto* copy=new(std::nothrow) BotPath::Route(route);
  if(!copy) { report("Cannot allocate lookup request",0,true); finishHistory(); return false; }
  report("Starting lookup");
  busy=true;
  if(xTaskCreate(worker,"pathLookup",12288,copy,1,nullptr)!=pdPASS) {
    report("Cannot allocate lookup task",0,true); busy=false; delete copy; finishHistory(); return false;
  }
  return true;
}
bool inProgress() { return busy; }
bool take(BotPath::Route& route) {
  BotPath::Route* result=nullptr;
  if(!busy || xQueueReceive(completed,&result,0)!=pdTRUE) return false;
  route=*result; delete result; busy=false; return true;
}
}
#endif
