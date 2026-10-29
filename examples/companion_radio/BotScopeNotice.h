#pragma once
#include "BotTypes.h"
#include <cstring>
#include <cstdio>

// Channel messages expose display names rather than authenticated sender keys.
// Keep live entries for the full hour; a full table suppresses new notices
// instead of evicting users and allowing repeated warnings.
class BotScopeNotice {
  struct Entry {
    char name[BOT_MAX_SENDER_NAME_LEN+1]{};
    uint32_t remaining=0;
  } entries[64];
  uint32_t last=0;
public:
  bool accept(const char* name,uint32_t now) {
    const uint32_t elapsed=now-last;last=now;
    Entry* free=nullptr;
    bool blocked=false;
    for(auto& entry:entries) {
      entry.remaining=elapsed>=entry.remaining ? 0 : entry.remaining-elapsed;
      if(!entry.remaining) {if(!free)free=&entry;}
      else if(!strcmp(entry.name,name))blocked=true;
    }
    if(blocked || !free)return false;
    snprintf(free->name,sizeof(free->name),"%s",name);
    free->remaining=60UL*60UL*1000UL;
    return true;
  }
};
