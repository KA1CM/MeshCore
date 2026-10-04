#pragma once
#include "BotStatsHistory.h"
#include <Preferences.h>
#include <memory>

class MonitorBotStatsHistory {
  Preferences prefs;
  BotStatsHistory history;
  bool initialized=false,ready=false,dirty=false;
  uint32_t lastAttempt=0;
public:
  void begin(uint32_t now) {
    if(initialized)return;
    initialized=true;lastAttempt=now;
    if(!prefs.begin("bot-stats",false))return;
    if(prefs.isKey("daily")) {
      std::unique_ptr<BotStatsHistory::Store> saved(new BotStatsHistory::Store);
      if(prefs.getBytesLength("daily")!=sizeof(*saved) || prefs.getBytes("daily",saved.get(),sizeof(*saved))!=sizeof(*saved) || !history.restore(*saved))return;
    }
    ready=true;
  }
  bool available() const {return ready;}
  const BotStatsHistory& counters() const {return history;}
  void observe(uint32_t day,uint32_t now) {begin(now);dirty=history.observe(day)||dirty;}
  void accepted(BotCommandId id,uint32_t day,uint32_t now) {begin(now);dirty=history.accepted(id,day)||dirty;}
  void poll(uint32_t now) {
    if(!ready || !dirty || uint32_t(now-lastAttempt)<3600000UL)return;
    lastAttempt=now;
    const auto& saved=history.store();
    if(prefs.putBytes("daily",&saved,sizeof(saved))==sizeof(saved))dirty=false;
  }
};
