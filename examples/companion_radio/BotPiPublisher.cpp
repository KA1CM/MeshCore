#include "BotPiPublisher.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "BotInternetProbe.h"
#include "BotPathLookup.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <cstring>
#include <cctype>
#include <lwip/netdb.h>
namespace BotPiPublisher {
namespace {
struct Settings {
  uint32_t version=1,attemptDay=0,completedDay=0,completedAt=0;
  uint16_t port=8787;
  uint8_t enabled=0,attempts=0;
  char host[64]{},token[65]{};
} settings;
Preferences prefs;
bool ready=false,active=false,manual=false,preparing=false,pending=false;
const char* status="Disabled";
Completion completion=Completion::None;
const char* completionReason=nullptr;
uint32_t retryAt=0,nextPoll=0,started=0,snapshotDay=0;
char* payload=nullptr;
size_t payloadLength=0;
char jobId[65]{};
uint8_t posts=0;
int lastHttp=0;
class PsramAllocator : public ArduinoJson::Allocator {
  void* allocate(size_t n) override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
  void deallocate(void* p) override{heap_caps_free(p);}
  void* reallocate(void* p,size_t n) override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
} allocator;
enum RemoteState {Invalid,Queued,Running,Published,Unchanged,Failed};
struct Request {char host[64],token[65],id[65];uint16_t port;const char* body;size_t length;};
struct Result {int http=0;RemoteState state=Invalid;char id[65]{};};
QueueHandle_t results=nullptr;
StaticQueue_t resultsControl;
uint8_t resultsBytes[sizeof(Result)];
bool configured(){return ready && settings.host[0] && settings.token[0] && settings.port;}
bool save(const Settings& candidate) {
  if(prefs.putBytes("config",&candidate,sizeof(candidate))!=sizeof(candidate))return false;
  settings=candidate;return true;
}
bool validHost(const char* host) {
  if(!host || !*host || strlen(host)>=sizeof(settings.host))return false;
  for(const char* p=host;*p;++p)if(!isalnum(static_cast<unsigned char>(*p)) && *p!='.' && *p!='-')return false;
  return true;
}
bool validToken(const char* token) {
  if(!token || strlen(token)<32 || strlen(token)>64)return false;
  for(const char* p=token;*p;++p)if(!isalnum(static_cast<unsigned char>(*p)) && *p!='_' && *p!='-')return false;
  return true;
}
class Client : public WiFiClient {
public:
  using WiFiClient::connect;
  int connect(const char* host,uint16_t port,int32_t timeout) override {
    IPAddress ip;
    if(ip.fromString(host))return WiFiClient::connect(ip,port,timeout);
    struct addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    struct addrinfo* addresses=nullptr;
    if(getaddrinfo(host,nullptr,&hints,&addresses) || !addresses || !addresses->ai_addr) {
      if(addresses)freeaddrinfo(addresses);return 0;
    }
    ip=IPAddress(reinterpret_cast<const struct sockaddr_in*>(addresses->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(addresses);return WiFiClient::connect(ip,port,timeout);
  }
};
class Body : public Stream {
public:
  char bytes[512]{};size_t size=0;bool failed=false;
  uint32_t began=millis();
  size_t write(uint8_t c) override{return write(&c,1);}
  size_t write(const uint8_t* p,size_t n) override {
    if(failed || n>sizeof(bytes)-1-size || millis()-began>6000){failed=true;return 0;}
    memcpy(bytes+size,p,n);size+=n;bytes[size]=0;return n;
  }
  int available() override{return 0;}int read() override{return -1;}
  int peek() override{return -1;}void flush() override{}
};
void worker(void* argument) {
  auto* request=static_cast<Request*>(argument);Result result;
  {
    Client client;HTTPClient http;
    char url[176];
    if(request->id[0])snprintf(url,sizeof(url),"http://%s:%u/status/%s",request->host,request->port,request->id);
    else snprintf(url,sizeof(url),"http://%s:%u/snapshot",request->host,request->port);
    http.setConnectTimeout(3000);http.setTimeout(4000);http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(http.begin(client,url)) {
      char authorization[73];snprintf(authorization,sizeof(authorization),"Bearer %s",request->token);
      http.addHeader("Authorization",authorization);http.addHeader("Content-Type","application/json");
      result.http=request->id[0] ? http.GET() :
        http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(request->body)),request->length);
      memset(authorization,0,sizeof(authorization));
      const int length=http.getSize();
      if((result.http==200 || result.http==202) && length!=0 && length<512) {
        Body body;const int transferred=http.writeToStream(&body);
        JsonDocument doc(&allocator);
        if(!body.failed && transferred>0 && size_t(transferred)==body.size &&
            (length<0 || size_t(length)==body.size) && !deserializeJson(doc,body.bytes,body.size) && !doc.overflowed()) {
          const char* id=doc["id"] | "",*state=doc["status"] | "";
          bool valid=strlen(id)==64;
          for(const char* p=id;valid && *p;++p)valid=(*p>='0'&&*p<='9')||(*p>='a'&&*p<='f');
          if(request->id[0] && strcmp(request->id,id))valid=false;
          if(valid) {
            strcpy(result.id,id);
            const char* states[]={"","queued","running","published","unchanged","failed"};
            for(uint8_t i=1;i<6;++i)if(!strcmp(state,states[i]))result.state=RemoteState(i);
          }
        }
      }
      http.end();
    }
  }
  memset(request->token,0,sizeof(request->token));heap_caps_free(request);
  xQueueSend(results,&result,portMAX_DELAY);vTaskDelete(nullptr);
}
bool launch() {
  if(active || BotPathLookup::inProgress() || BotInternetProbe::busy() || WiFi.status()!=WL_CONNECTED)return false;
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<12288 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<6144)return false;
  if(!results)results=xQueueCreateStatic(1,sizeof(Result),resultsBytes,&resultsControl);
  auto* request=static_cast<Request*>(heap_caps_malloc(sizeof(Request),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!request || !results){heap_caps_free(request);return false;}
  strcpy(request->host,settings.host);strcpy(request->token,settings.token);strcpy(request->id,jobId);
  request->port=settings.port;request->body=payload;request->length=payloadLength;
  active=true;
  if(xTaskCreatePinnedToCore(worker,"piPublish",4096,request,tskIDLE_PRIORITY+1,nullptr,ARDUINO_RUNNING_CORE)!=pdPASS) {
    active=false;memset(request->token,0,sizeof(request->token));heap_caps_free(request);return false;
  }
  if(!jobId[0])++posts;
  return true;
}
void release(){heap_caps_free(payload);payload=nullptr;payloadLength=0;jobId[0]=0;pending=false;preparing=false;manual=false;}
}
ArduinoJson::Allocator* jsonAllocator(){return &allocator;}
bool begin() {
  ready=prefs.begin("ctffc-pi",false);
  if(!ready){status="Private Pi settings unavailable";return false;}
  if(prefs.isKey("config")) {
    Settings loaded;
    if(prefs.getBytesLength("config")!=sizeof(loaded) || prefs.getBytes("config",&loaded,sizeof(loaded))!=sizeof(loaded) ||
        loaded.version!=1 || loaded.enabled>1 || loaded.attempts>3 ||
        !memchr(loaded.host,0,sizeof(loaded.host)) || !memchr(loaded.token,0,sizeof(loaded.token)) ||
        !validHost(loaded.host) || !validToken(loaded.token) || !loaded.port) {
      ready=false;status="Invalid private Pi settings";return false;
    }
    settings=loaded;
  }
  status=settings.enabled?"Waiting for 02:30 Eastern":"Automatic upload disabled";return true;
}
bool busy(){return active;}
bool manualPending(){return manual;}
Completion takeCompletion(const char*& reason) {
  const Completion result=completion;reason=completionReason;completion=Completion::None;return result;
}
bool configure(bool enabled,const char* host,uint16_t port,const char* token) {
  if(!ready || active || pending || preparing || manual || !validHost(host) || !port || !token)return false;
  Settings candidate=settings;candidate.enabled=enabled;candidate.port=port;strcpy(candidate.host,host);
  if(*token) {
    if(!validToken(token))return false;
    strcpy(candidate.token,token);
  }
  if(!validToken(candidate.token))return false;
  const bool ok=save(candidate);memset(candidate.token,0,sizeof(candidate.token));
  if(ok)status=enabled?"Waiting for 02:30 Eastern":"Automatic upload disabled";
  return ok;
}
const char* requestManual(uint32_t day,uint32_t ms) {
  if(!configured())return "Save the Pi host, port and shared token first.";
  if(active || pending || preparing || manual)return "Pi update already running or queued.";
  if(settings.attemptDay==day && settings.attempts>=3)return "Daily limit reached (three update attempts).";
  if(retryAt && int32_t(ms-retryAt)<0)return "Wait for the five-minute update cooldown.";
  completion=Completion::None;manual=true;status="Manual Pi update queued";return nullptr;
}
bool due(uint32_t day,uint32_t ms) {
  return configured() && day && !active && !pending &&
    (manual || (settings.enabled && settings.completedDay!=day)) &&
    (preparing || settings.attemptDay!=day || settings.attempts<3) &&
    (!retryAt || int32_t(ms-retryAt)>=0);
}
bool beginPreparation(uint32_t day) {
  if(preparing || !due(day,millis()))return false;
  Settings candidate=settings;
  if(candidate.attemptDay!=day){candidate.attemptDay=day;candidate.attempts=0;}
  ++candidate.attempts;
  const bool ok=save(candidate);memset(candidate.token,0,sizeof(candidate.token));
  if(ok){preparing=true;status="Checking all ct-ffc names";}
  return ok;
}
void preparationFailed(const char* reason,uint32_t ms){release();status=reason;retryAt=ms+300000UL;completion=Completion::Failed;completionReason=reason;}
void resolvingNames(){status="Resolving ct-ffc names for Pi";}
bool start(char* body,size_t length,uint32_t day,uint32_t ms) {
  if(!preparing || active || pending || !body || !length || length>SnapshotLimit)return false;
  payload=body;payloadLength=length;snapshotDay=day;jobId[0]=0;posts=0;
  if(!launch()) {
    payload=nullptr;payloadLength=0;preparationFailed("Could not start local HTTP upload",ms);return false;
  }
  pending=true;preparing=false;manual=false;started=ms;nextPoll=ms+15000UL;
  status="Sending snapshot to Pi";return true;
}
void poll(uint32_t utc,uint32_t ms,bool networkIdle) {
  Result result;
  if(active && xQueueReceive(results,&result,0)==pdTRUE) {
    active=false;lastHttp=result.http;
    if(result.http==401){preparationFailed("Pi rejected the shared token",ms);return;}
    if((result.http==200 || result.http==202) && result.state!=Invalid) {
      strcpy(jobId,result.id);
      if(result.state==Published || result.state==Unchanged) {
        completion=result.state==Published ? Completion::Published : Completion::Unchanged;completionReason=nullptr;
        Settings candidate=settings;candidate.completedDay=snapshotDay;candidate.completedAt=utc;
        const bool saved=save(candidate);
        settings.completedDay=candidate.completedDay;settings.completedAt=candidate.completedAt;
        memset(candidate.token,0,sizeof(candidate.token));release();retryAt=ms+300000UL;
        status=!saved?"Pi confirmed update; local status save failed":
          result.state==Published?"Wiki updated by Pi":"Pi confirmed wiki already up to date";
        return;
      }
      if(result.state==Failed){preparationFailed("Pi could not publish wiki; check Pi app logs/settings",ms);return;}
      status="Pi received snapshot; waiting for publication";nextPoll=ms+15000UL;
    } else if(jobId[0] && result.http==404 && posts<3) {
      jobId[0]=0;nextPoll=ms+30000UL;status="Pi lost job; resending same snapshot";
    } else if(jobId[0] && result.http==404) {
      preparationFailed("Pi lost job; resend limit reached",ms);return;
    } else if(jobId[0]) {
      status="Publication not confirmed; retrying Pi status";nextPoll=ms+30000UL;
    } else {
      preparationFailed(result.http==400?"Pi rejected the snapshot":
        result.http==409?"Pi receiver busy; update deferred":
        result.http==503?"Pi receiver storage unavailable":
        "Pi HTTP upload failed; check host, port and app",ms);return;
    }
  }
  if(!pending || active)return;
  if(ms-started>600000UL){preparationFailed("Pi publication confirmation timed out",ms);return;}
  if(networkIdle && int32_t(ms-nextPoll)>=0) {
    if(!launch())nextPoll=ms+15000UL;
  }
}
void exportState(JsonObject out) {
  out["ready"]=ready;out["configured"]=configured();out["enabled"]=bool(settings.enabled);
  out["busy"]=active;out["pending"]=pending;out["preparing"]=preparing;out["queued"]=manual;
  out["host"]=settings.host;out["port"]=settings.port;out["status"]=status;out["http"]=lastHttp;
  out["completedAt"]=settings.completedAt;out["attempts"]=settings.attempts;
}
}
#endif
