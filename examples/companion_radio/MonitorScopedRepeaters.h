#pragma once
#include "BotScopedRepeaters.h"
#include "BotSnapshotFile.h"
#include <Preferences.h>

class MonitorScopedRepeaters {
  Preferences prefs;
  BotSnapshotFile snapshots{"/ctffc0.bin","/ctffc1.bin"};
  BotScopedRepeaters list;
  bool initialized=false,ready=false,attempted=false;
  uint32_t lastAttempt=0,lastDailyDay=0;
  const char* error="storage unavailable";
public:
  void begin() {
    if(initialized)return;
    initialized=true;
    auto& saved=list.data;
    const auto loaded=snapshots.load(&saved,sizeof(saved));
    if(loaded==BotSnapshotFile::Invalid || loaded==BotSnapshotFile::Unavailable){error=snapshots.lastError();saved.count=0;return;}
    if(loaded==BotSnapshotFile::Missing && prefs.begin("ct-ffc-hops",true)) {
      // Retain the old NVS snapshot during filesystem migration.
      if(prefs.isKey("list") && (prefs.getBytesLength("list")!=sizeof(saved) ||
          prefs.getBytes("list",&saved,sizeof(saved))!=sizeof(saved))) {
        prefs.end();saved.count=0;error="legacy list incompatible";return;
      }
      prefs.end();
    }
    if(saved.version!=1 || saved.count>BotScopedRepeaters::Capacity){saved.count=0;error="list incompatible";return;}
    for(size_t i=0;i<saved.count;++i)
      if(!saved.entries[i].lastDay || saved.entries[i].lastDay>saved.savedDay){saved.count=0;error="list dates invalid";return;}
    lastDailyDay=list.data.savedDay;
    ready=true;error="";
  }
  void observe(const uint8_t* path,size_t hops,size_t width,uint32_t day) {
    // begin() runs in the main loop; radio reception never opens storage.
    if(ready)list.observe(path,hops,width,day);
  }
  void saveAfterActivity(uint32_t now,uint32_t day) {
    if(!ready || !day)return;
    list.prune(day);
    if(day<=lastDailyDay || (attempted && uint32_t(now-lastAttempt)<3600000UL))return;
    attempted=true;lastAttempt=now;
    const uint32_t previous=list.data.savedDay;list.data.savedDay=day;
    if(snapshots.save(&list.data,sizeof(list.data))){attempted=false;lastDailyDay=day;error="";}
    else {list.data.savedDay=previous;error=snapshots.lastError();}
  }
  bool available() const {return ready;}
  const char* lastError() const {return error;}
  bool saveNow(uint32_t day) {
    begin();if(!ready)return false;
    if(!day){error="NTP not confirmed";return false;}
    list.prune(day);
    const uint32_t previous=list.data.savedDay;list.data.savedDay=day;
    const bool success=snapshots.save(&list.data,sizeof(list.data));
    if(!success){list.data.savedDay=previous;error=snapshots.lastError();}
    else error="";
    return success;
  }
  const BotScopedRepeaters::Store& entries() const {return list.data;}
};
