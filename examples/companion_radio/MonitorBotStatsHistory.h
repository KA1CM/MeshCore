#pragma once
#include "BotStatsHistory.h"
#include "BotSnapshotFile.h"
#include <memory>

class MonitorBotStatsHistory {
  BotSnapshotFile snapshots{"/botstats0.bin","/botstats1.bin"};
  BotStatsHistory history;
  bool initialized=false,ready=false,dirty=false;
  uint32_t lastAttempt=0,lastSavedDay=0;
  bool attempted=false;
  const char* error="storage unavailable";
public:
  void begin(uint32_t now) {
    if(initialized)return;
    initialized=true;lastAttempt=now;
    std::unique_ptr<BotStatsHistory::Store> saved(new(std::nothrow) BotStatsHistory::Store);
    if(!saved){error="out of memory";return;}
    const auto loaded=snapshots.load(saved.get(),sizeof(*saved));
    if(loaded==BotSnapshotFile::Loaded) {
      if(!history.restore(*saved)){error="snapshot incompatible";return;}
    } else if(loaded!=BotSnapshotFile::Missing) {error=snapshots.lastError();return;}
    // Existing snapshots already identify the calendar day of their save.
    lastSavedDay=history.store().latestDay;
    ready=true;error="";
  }
  bool available() const {return ready;}
  const char* lastError() const {return error;}
  uint32_t savedDay() const {return lastSavedDay;}
  const BotStatsHistory& counters() const {return history;}
  bool saveNow(uint32_t now,uint32_t day) {
    begin(now);
    if(!ready)return false;
    if(!day){error="NTP not confirmed";return false;}
    observe(day,now);
    const auto& saved=history.store();
    // A manual snapshot does not advance the in-memory daily schedule.
    if(!snapshots.save(&saved,sizeof(saved))){error=snapshots.lastError();return false;}
    dirty=false;error="";return true;
  }
  void observe(uint32_t day,uint32_t now) {begin(now);dirty=history.observe(day)||dirty;}
  void accepted(BotCommandId id,uint32_t day,uint32_t now) {begin(now);dirty=history.accepted(id,day)||dirty;}
  // Save at the first poll at/after 03:00 Eastern. A missed time is caught up.
  // Restoring the saved day prevents repeat writes after a same-day reboot.
  void poll(uint32_t now,uint32_t day,uint32_t secondsOfDay) {
    if(!ready || !dirty || !day || day<=lastSavedDay || secondsOfDay<3*3600UL || secondsOfDay>=86400UL)return;
    // Retry failed storage writes hourly without delaying the scheduled save.
    if(attempted && uint32_t(now-lastAttempt)<3600000UL)return;
    lastAttempt=now;attempted=true;
    const auto& saved=history.store();
    if(snapshots.save(&saved,sizeof(saved))){dirty=false;lastSavedDay=day;attempted=false;error="";}
    else error=snapshots.lastError();
  }
};
