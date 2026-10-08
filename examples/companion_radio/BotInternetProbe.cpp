#include "BotInternetProbe.h"
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include <WiFi.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <cstring>
namespace BotInternetProbe {
namespace {
QueueHandle_t results=nullptr;
bool active=false;
// Bound the decoded response; never allocate an arbitrary portal/error body.
class Body : public Stream {
public:
 char data[40]{};size_t size=0;uint32_t started=millis();
 size_t write(uint8_t c)override{return write(&c,1);}
 size_t write(const uint8_t* p,size_t n)override{
   if(n>sizeof(data)-1-size || millis()-started>4000)return 0;
   memcpy(data+size,p,n);size+=n;data[size]=0;return n;
 }
 int available()override{return 0;} int read()override{return -1;}
 int peek()override{return -1;} void flush()override{}
};
bool probe(const char* url,const char* expected) {
 WiFiClient client;HTTPClient http;
 http.setConnectTimeout(3000);http.setTimeout(3000);
 http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);http.useHTTP10(true);
 if(!http.begin(client,url))return false;
 bool ok=false;
 if(http.GET()==200 && (http.getSize()<0 || http.getSize()<=39)) {
   Body body;int n=http.writeToStream(&body);
   ok=n==(int)body.size && !strcmp(body.data,expected);
 }
 http.end();return ok;
}
void worker(void*) {
 bool ok=probe("http://www.msftconnecttest.com/connecttest.txt","Microsoft Connect Test");
 if(!ok && WiFi.status()==WL_CONNECTED)ok=probe("http://detectportal.firefox.com/success.txt","success\n");
 xQueueSend(results,&ok,portMAX_DELAY);vTaskDelete(nullptr);
}
}
bool busy(){return active;}
bool start(){
 if(active)return false;
 if(!results)results=xQueueCreate(1,sizeof(bool));
 if(!results)return false;
 active=true;
 if(xTaskCreate(worker,"internetProbe",4096,nullptr,1,nullptr)!=pdPASS){active=false;return false;}
 return true;
}
bool take(bool& ok){
 if(!active || xQueueReceive(results,&ok,0)!=pdTRUE)return false;
 active=false;return true;
}
}
#endif
