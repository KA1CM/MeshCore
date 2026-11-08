#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Main-loop-only passive tracking. Packet hashes exclude the changing route.
template<size_t HashSize> class BotReplyTracker {
public:
  enum Event { Queued, Transmitted, Repeated, Failed, Acknowledged };
  struct Entry {
    uint8_t hash[HashSize]{};
    uint32_t historyId=0, expires=0;
    uint32_t ack=0, tag=0;
    bool acknowledged=false;
    bool notice=false, transmitted=false, repeated=false, failed=false;
  };
  bool add(const uint8_t* hash,uint32_t id,bool notice,uint32_t now,uint32_t ack=0,uint32_t tag=0) {
    for(auto& e:entries) if(!e.historyId || int32_t(now-e.expires)>=0) {
      e=Entry{};memcpy(e.hash,hash,HashSize);e.historyId=id;
      e.notice=notice;e.ack=ack;e.tag=tag;e.expires=now+120000UL;return true;
    }
    return false; // Never overwrite a live packet or interfere with sending.
  }
  template<class Callback> void observe(const uint8_t* hash,Event event,uint32_t now,Callback callback) {
    for(auto& e:entries) {
      if(!e.historyId || e.acknowledged || int32_t(now-e.expires)>=0 || memcmp(e.hash,hash,HashSize))continue;
      if(event==Transmitted && !e.transmitted && !e.failed) {
        e.transmitted=true;callback(e,event);
      } else if(event==Repeated && e.transmitted && !e.repeated && !e.failed) {
        e.repeated=true;callback(e,event);
      } else if(event==Failed && !e.transmitted && !e.failed) {
        e.failed=true;callback(e,event);
      }
    }
  }
  template<class Callback> void acknowledge(uint32_t ack,uint32_t now,Callback callback) {
    if(!ack)return;
    for(auto& e:entries)if(e.historyId && e.ack==ack && !e.acknowledged && int32_t(now-e.expires)<0) {
      // A retry has a different ACK but belongs to the same logical DM part.
      for(auto& sibling:entries)
        if(sibling.historyId==e.historyId && sibling.tag==e.tag && sibling.ack)sibling.acknowledged=true;
      callback(e,Acknowledged);return;
    }
  }
private:
  Entry entries[32];
};
