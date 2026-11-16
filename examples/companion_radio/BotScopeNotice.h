#pragma once
#include "BotTypes.h"
#include <cstring>
#include <cstdio>

// Channel messages expose display names rather than authenticated sender keys.
// Reserve briefly while awaiting a repeat; only confirmation starts the hour.
// A full table suppresses new notices rather than evicting live entries.
class BotScopeNotice {
  struct Entry {
    char name[BOT_MAX_SENDER_NAME_LEN+1]{};
    uint32_t remaining=0;
    uint32_t requestId=0;
    bool pending=false;
  } entries[64];
  uint32_t last=0;
  void age(uint32_t now) {
    const uint32_t elapsed=now-last;last=now;
    for(auto& e:entries)e.remaining=elapsed>=e.remaining ? 0 : e.remaining-elapsed;
  }
public:
  // Undo a reserved cooldown when the radio could not queue the greeting.
  void cancel(const char* name) {
    for(auto& entry:entries)
      if(entry.remaining && !strcmp(entry.name,name))entry.remaining=0;
  }
  void bind(const char* name,uint32_t requestId) {
    for(auto& e:entries)if(e.remaining && e.pending && !strcmp(e.name,name))e.requestId=requestId;
  }
  void confirm(uint32_t requestId,uint32_t now) {
    age(now);
    if(!requestId)return;
    for(auto& e:entries)if(e.remaining && e.pending && e.requestId==requestId) {
      e.pending=false;e.remaining=60UL*60UL*1000UL;
      return; // Duplicate repeats never extend an existing cooldown.
    }
  }
  void extendPending(uint32_t requestId,uint32_t now) {
    age(now);
    for(auto& e:entries)if(e.remaining && e.pending && e.requestId==requestId)
      e.remaining=120000UL; // Await confirmation; do not start the hour cooldown.
  }
  void failed(uint32_t requestId) {
    if(!requestId)return;
    for(auto& e:entries)if(e.pending && e.requestId==requestId)e.remaining=0;
  }
  bool accept(const char* name,uint32_t now) {
    age(now);
    Entry* free=nullptr;
    bool blocked=false;
    for(auto& entry:entries) {
      if(!entry.remaining) {if(!free)free=&entry;}
      else if(!strcmp(entry.name,name))blocked=true;
    }
    if(blocked || !free)return false;
    snprintf(free->name,sizeof(free->name),"%s",name);
    free->remaining=120000UL;free->requestId=0;free->pending=true;
    return true;
  }
};
