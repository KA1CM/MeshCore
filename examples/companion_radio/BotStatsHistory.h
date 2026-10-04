#pragma once
#include "BotTypes.h"
#include "BotCommandRegistry.h"
#include <cstring>
#include <stdint.h>

// Calendar-day counters, independent of the rolling 24-hour window.
class BotStatsHistory {
public:
  static constexpr size_t DAYS=30, COMMANDS=BOT_COMMAND_PASSWORD+1;
  struct Day {uint32_t day=0;uint32_t counts[COMMANDS]{};};
  struct Store {uint32_t version=1,startedDay=0,latestDay=0;Day days[DAYS];};
private:
  Store data;
public:
  const Store& store() const {return data;}
  static bool valid(const Store& s) {
    if(s.version!=1 || (s.startedDay==0)!=(s.latestDay==0) || s.startedDay>s.latestDay) return false;
    for(size_t i=0;i<DAYS;++i) {
      const auto& d=s.days[i];
      if(d.day && (d.day%DAYS!=i || d.day<s.startedDay || d.day>s.latestDay || s.latestDay-d.day>=DAYS)) return false;
      if(!d.day) for(auto n:d.counts) if(n) return false;
    }
    return true;
  }
  bool restore(const Store& s) {if(!valid(s))return false;data=s;return true;}
  bool observe(uint32_t day) {
    if(!day || day<data.latestDay) return false;
    if(!data.startedDay)data.startedDay=day;
    if(day==data.latestDay)return false;
    data.latestDay=day;
    for(auto& d:data.days) if(d.day && day-d.day>=DAYS)d=Day{};
    return true;
  }
  bool accepted(BotCommandId id,uint32_t day) {
    if(!day || (size_t)id>=COMMANDS || (data.latestDay && day<=data.latestDay && data.latestDay-day>=DAYS)) return false;
    observe(day);
    if(day<data.startedDay)return false;
    const auto* meta=BotCommandRegistry::findById(id);
    if(meta && meta->visibility==BOT_COMMAND_VISIBILITY_HIDDEN)id=BOT_COMMAND_ADVERT;
    auto& d=data.days[day%DAYS];
    if(d.day!=day){d=Day{};d.day=day;}
    if(d.counts[id]!=UINT32_MAX)++d.counts[id];
    return true;
  }
  const Day* at(uint32_t day) const {const auto& d=data.days[day%DAYS];return d.day==day ? &d : nullptr;}
};
