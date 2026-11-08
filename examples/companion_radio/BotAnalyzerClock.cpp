#include "BotAnalyzerClock.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "BotInternetProbe.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <lwip/netdb.h>
#include <cmath>
#include <cstring>
#include <strings.h>
namespace BotAnalyzerClock {
const char* severityName(uint8_t severity) {
  static const char* names[]={"unknown","ok","warning","critical","absurd","no_clock","bimodal_clock"};
  return severity<7 ? names[severity] : names[0];
}
const char* resultName(uint8_t result) {
  static const char* names[]={"Not checked","Updated","No analyzer data","HTTP request failed","Invalid analyzer data","Not enough PSRAM"};
  return result<6 ? names[result] : names[0];
}
namespace {
struct Reply {uint8_t key[32]{};Summary summary;};
bool active=false;
TaskHandle_t task=nullptr;
QueueHandle_t requests=nullptr,results=nullptr;
StaticQueue_t requestControl,resultControl;
uint8_t requestBytes[32],resultBytes[sizeof(Reply)];
StaticTask_t taskControl;
constexpr size_t StackBytes=4096;
alignas(16) StackType_t stack[StackBytes/sizeof(StackType_t)];
// Decode HTTP transfer framing through HTTPClient, with a strict body cap.
class Body : public Stream {
public:
  static constexpr size_t Limit=256*1024;
  char* bytes=static_cast<char*>(heap_caps_malloc(Limit+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  size_t size=0;bool failed=false;uint32_t started=millis();
  ~Body(){heap_caps_free(bytes);}
  size_t write(uint8_t c) override{return write(&c,1);}
  size_t write(const uint8_t* p,size_t n) override {
    if(!bytes || failed || n>Limit-size || millis()-started>15000){failed=true;return 0;}
    memcpy(bytes+size,p,n);size+=n;bytes[size]=0;return n;
  }
  int available() override{return 0;}int read() override{return -1;}
  int peek() override{return -1;}void flush() override{}
};
class Client : public WiFiClient {
public:
  using WiFiClient::connect;
  int connect(const char* host,uint16_t port,int32_t timeout) override {
    struct addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    struct addrinfo* addresses=nullptr;
    if(getaddrinfo(host,nullptr,&hints,&addresses) || !addresses || !addresses->ai_addr) {
      if(addresses)freeaddrinfo(addresses);return 0;
    }
    const IPAddress ip(reinterpret_cast<const struct sockaddr_in*>(addresses->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(addresses);return WiFiClient::connect(ip,port,timeout);
  }
};
Summary fetch(const uint8_t key[32]) {
  Summary out;out.result=HttpFailed;
  char hex[65];for(size_t i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",key[i]);
  char url[160];snprintf(url,sizeof(url),"http://analyzer.ctmesh.org/api/nodes/%s/clock-skew",hex);
  Client client;HTTPClient http;
  http.setConnectTimeout(4000);http.setTimeout(4000);http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,url))return out;
  const int status=http.GET();
  if(status==404){out.result=NoData;http.end();return out;}
  const int length=http.getSize();
  if(status!=200){http.end();return out;}
  if(length==0 || length>int(Body::Limit)){out.result=InvalidData;http.end();return out;}
  Body body;
  if(!body.bytes){out.result=AllocationFailed;http.end();return out;}
  const int transferred=http.writeToStream(&body);http.end();
  if(body.failed || transferred<=0 || size_t(transferred)!=body.size ||
      (length>=0 && size_t(length)!=body.size))return out;
  JsonDocument filter;
  for(const char* field:{"pubkey","recentMedianSkewSec","driftPerDaySec","severity","sampleCount","calibrated","lastAdvertTS","lastObservedTS"})filter[field]=true;
  JsonDocument doc;
  if(deserializeJson(doc,body.bytes,body.size,DeserializationOption::Filter(filter)) || doc.overflowed()) {
    out.result=InvalidData;return out;
  }
  if(doc.isNull()){out.result=NoData;return out;}
  const char* returned=doc["pubkey"] | "";
  const char* severity=doc["severity"] | "";
  for(uint8_t i=1;i<7;++i)if(!strcmp(severity,severityName(i)))out.severity=i;
  if(!strcasecmp(returned,hex) && !out.severity && !doc["sampleCount"].as<uint32_t>()) {
    out.result=NoData;return out;
  }
  if(strcasecmp(returned,hex) || !out.severity || !doc["recentMedianSkewSec"].is<double>() ||
      !doc["driftPerDaySec"].is<double>() || !doc["sampleCount"].is<uint32_t>() ||
      !doc["lastAdvertTS"].is<uint32_t>() || !doc["lastObservedTS"].is<uint32_t>() ||
      !doc["calibrated"].is<bool>()) {out.result=InvalidData;return out;}
  out.skew=doc["recentMedianSkewSec"].as<float>();out.drift=doc["driftPerDaySec"].as<float>();
  if(!std::isfinite(out.skew) || !std::isfinite(out.drift)){out.result=InvalidData;return out;}
  out.samples=doc["sampleCount"];out.advertAt=doc["lastAdvertTS"];out.observedAt=doc["lastObservedTS"];
  out.calibrated=doc["calibrated"];out.result=Good;return out;
}
void worker(void*) {
  for(;;) {
    Reply reply;
    if(xQueueReceive(requests,reply.key,portMAX_DELAY)!=pdTRUE)continue;
    reply.summary=fetch(reply.key); // HTTP/JSON/body are destroyed before publication.
    xQueueSend(results,&reply,portMAX_DELAY);
  }
}
}
bool busy(){return active;}
bool start(const uint8_t key[32]) {
  if(active || BotInternetProbe::busy() || WiFi.status()!=WL_CONNECTED)return false;
  if(!requests)requests=xQueueCreateStatic(1,32,requestBytes,&requestControl);
  if(!results)results=xQueueCreateStatic(1,sizeof(Reply),resultBytes,&resultControl);
  if(!requests || !results)return false;
  if(!task)task=xTaskCreateStaticPinnedToCore(worker,"analyzerClock",StackBytes,nullptr,
    tskIDLE_PRIORITY+1,stack,&taskControl,ARDUINO_RUNNING_CORE);
  if(!task)return false;
  active=true;
  if(xQueueSend(requests,key,0)!=pdTRUE){active=false;return false;}
  return true;
}
bool take(uint8_t key[32],Summary& summary) {
  Reply reply;
  if(!active || xQueueReceive(results,&reply,0)!=pdTRUE)return false;
  memcpy(key,reply.key,32);summary=reply.summary;active=false;return true;
}
}
#endif
