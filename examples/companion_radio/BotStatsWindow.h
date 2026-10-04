#pragma once
#include "BotTypes.h"
#include "BotCommandRegistry.h"
#include "BotVoltageList.h"
#include <stdint.h>
#include <cstring>
#include <cstdio>

// Bounded memory: 96 quarter-hour buckets. Never includes events older than
// 24h; the oldest partial bucket is discarded (up to 15 minutes of precision).
class BotStatsWindow {
  static constexpr size_t COMMANDS=37, METRICS=7, SLOTS=96;
  static constexpr uint32_t PERIOD=900000;
  struct Bucket { uint32_t commands[COMMANDS]{}, metrics[METRICS]{}; } buckets[SLOTS];
  size_t current=0;
  uint32_t lastMillis=0, remainder=0;
  bool started=false;
  uint32_t previous[METRICS]{};
  void advance(uint32_t now) {
    if(!started){started=true;lastMillis=now;return;}
    const uint32_t delta=now-lastMillis;lastMillis=now;
    const uint64_t elapsed=(uint64_t)remainder+delta;
    uint64_t steps=elapsed/PERIOD;remainder=elapsed%PERIOD;
    if(steps>=SLOTS){for(auto& b:buckets)b=Bucket{};current=0;return;}
    while(steps--){current=(current+1)%SLOTS;buckets[current]=Bucket{};}
  }
public:
  void accepted(BotCommandId id,uint32_t now){advance(now);if((size_t)id<COMMANDS)++buckets[current].commands[id];}
  // Cumulative counters sampled from the main loop; unsigned deltas handle wrap.
  void sample(uint32_t now,const BotStats& stats,uint32_t rx,uint32_t tx,uint32_t errors){
    advance(now);
    const uint32_t values[METRICS]={stats.observed_messages,stats.eligible_messages,stats.sent_messages,stats.send_failures,rx,tx,errors};
    for(size_t i=0;i<METRICS;++i){buckets[current].metrics[i]+=values[i]-previous[i];previous[i]=values[i];}
  }
  void totals(uint32_t now,uint64_t* counts) {
    advance(now);
    for(size_t i=0;i<COMMANDS;++i)counts[i]=0;
    for(const auto& b:buckets)for(size_t i=0;i<COMMANDS;++i)counts[i]+=b.commands[i];
  }
  void snapshot(uint32_t now,BotVoltageList::Snapshot& out){
    advance(now);out.count=0;Bucket sum;
    for(const auto& b:buckets){for(size_t i=0;i<COMMANDS;++i)sum.commands[i]+=b.commands[i];for(size_t i=0;i<METRICS;++i)sum.metrics[i]+=b.metrics[i];}
    auto line=[&]()->char*{return out.lines[out.count++];};
    uint64_t total=0;for(auto n:sum.commands)total+=n;
    snprintf(line(),BotVoltageList::LINE_SIZE,"Last 24h: %lu responses",(unsigned long)total);
    // Fold hidden admin DM commands into one category before sorting.
    uint64_t counts[COMMANDS]{};
    uint64_t admin=0;
    for(size_t i=0;i<COMMANDS;++i) {
      const auto* meta=BotCommandRegistry::findById((BotCommandId)i);
      if(meta && meta->visibility==BOT_COMMAND_VISIBILITY_HIDDEN) admin+=sum.commands[i];
      else counts[i]=sum.commands[i];
    }
    const size_t adminIndex=BOT_COMMAND_ADVERT;
    counts[adminIndex]=admin;
    bool shown[COMMANDS]{};
    for(size_t n=0;n<COMMANDS;++n){
      size_t best=0;uint64_t value=0;
      for(size_t i=0;i<COMMANDS;++i)if(!shown[i]&&counts[i]>value){best=i;value=counts[i];}
      if(!value)break;
      shown[best]=true;
      const auto* meta=BotCommandRegistry::findById((BotCommandId)best);
      const char* name=best==adminIndex ? "admin" : meta ? meta->name : "unknown";
      snprintf(line(),BotVoltageList::LINE_SIZE,"%s %u%%",name,(unsigned)((value*100ULL+total/2)/total));
    }

  }
};
