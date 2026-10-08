#pragma once
#include <stdint.h>
namespace BotConnectivity {
constexpr uint32_t INTERVAL=300000, FAILURE_RETRY=30000, REMINDER=3600000;
enum State : uint8_t { Unknown, Good, Lost };
enum Kind : uint8_t { PowerLost, PowerReminder, PowerRestored, WifiLost, WifiReminder,
  WifiRestoredOnline, WifiRestoredOffline, InternetLost, InternetReminder, InternetRestored };
struct Link { State state; uint32_t since; Link(State s=Unknown,uint32_t t=0):state(s),since(t){} };
struct Event {
  Kind kind=InternetLost;uint32_t utc=0,since=0,durationMs=0;uint16_t battery=0;
};
struct Monitor {
  Link power,wifi,internet;
  uint32_t nextProbe=0,firstFailure=0,firstFailureMs=0;
  uint32_t powerLostMs=0,wifiLostMs=0,wifiRestoredMs=0,internetLostMs=0;
  uint32_t lastPowerReminder=0,lastWifiReminder=0,lastInternetReminder=0;
  uint8_t failures=0,successes=0;
  bool initialized=false,wifiRestorePending=false;
  void begin(uint32_t ms){initialized=true;nextProbe=ms+INTERVAL;}
  bool outage()const{return power.state==Lost || wifi.state==Lost || internet.state==Lost;}
  bool due(uint32_t ms)const{return initialized && wifi.state==Good && (int32_t)(ms-nextProbe)>=0;}
  void attempted(uint32_t ms){nextProbe=ms+INTERVAL;}
  bool setPower(bool present,uint32_t utc,uint32_t ms) {
    const State next=present?Good:Lost;if(power.state==next)return false;
    const bool alert=!present || power.state==Lost;
    if(!present)powerLostMs=ms;
    power={next,present?0:utc};return alert;
  }
  bool setWifi(bool connected,uint32_t utc,uint32_t ms) {
    const State next=connected?Good:Lost;if(wifi.state==next)return false;
    const bool alert=wifi.state==Lost || !connected;
    wifiRestorePending=connected && wifi.state==Lost;
    wifi={next,connected?0:utc};
    if(!connected) {
      wifiLostMs=ms;
      if(internet.state!=Lost){internet={Lost,utc};internetLostMs=ms;}
      failures=successes=0;
    } else if(wifiRestorePending)wifiRestoredMs=ms;
    return alert;
  }
  bool result(bool ok,uint32_t ms,uint32_t utc) {
    nextProbe=ms+INTERVAL;
    if(wifiRestorePending) {
      wifiRestorePending=false;successes=0;
      if(ok){internet={Good,0};failures=0;firstFailure=0;}
      else {if(!failures){firstFailure=utc;firstFailureMs=ms;}if(failures<3)++failures;}
      return true;
    }
    if(ok) {
      failures=0;firstFailure=0;
      if(internet.state==Lost && ++successes<2)return false;
      const bool restored=internet.state==Lost;
      internet={Good,0};successes=0;return restored;
    }
    successes=0;
    if(!failures){firstFailure=utc;firstFailureMs=ms;}
    if(failures<3)++failures;
    if(failures<3 && internet.state!=Lost)nextProbe=ms+FAILURE_RETRY;
    if(failures==3 && internet.state!=Lost){internet={Lost,firstFailure};internetLostMs=firstFailureMs;return true;}
    return false;
  }
  bool reminder(uint32_t ms,Kind kind){
    bool lost=false;uint32_t* last=nullptr;
    if(kind==PowerReminder){lost=power.state==Lost;last=&lastPowerReminder;}
    if(kind==WifiReminder){lost=wifi.state==Lost;last=&lastWifiReminder;}
    if(kind==InternetReminder){lost=internet.state==Lost && wifi.state==Good && !wifiRestorePending;last=&lastInternetReminder;}
    if(!last || !lost || ms-*last<REMINDER)return false;
    *last=ms;return true;
  }
  Event event(uint32_t ms,uint32_t utc,Kind kind) {
    Event e;e.kind=kind;e.utc=utc;
    if(kind<=PowerRestored){lastPowerReminder=ms;e.since=power.since;e.durationMs=ms-powerLostMs;}
    else if(kind<=WifiRestoredOffline){
      lastWifiReminder=ms;e.since=kind==WifiRestoredOffline?internet.since:wifi.since;
      e.durationMs=wifiRestoredMs-wifiLostMs;
      if(kind==WifiRestoredOffline)lastInternetReminder=ms;
    } else {lastInternetReminder=ms;e.since=internet.since;e.durationMs=ms-internetLostMs;}
    if(kind==PowerLost || kind==WifiLost || kind==InternetLost)e.utc=e.since;
    return e;
  }
};
}
