#pragma once
#include <stdint.h>
#include <string.h>

// Diagnostic-only: remember authenticated sender, timestamp and text digest.
// No plaintext messages/passwords and no changes to command acceptance.
class BotDmRequestLog {
  struct Entry {
    uint8_t key[32]{},digest[8]{};
    uint32_t timestamp=0,expires=0;
    bool used=false;
  } entries[16];
  unsigned next=0;
public:
  bool repeated(const uint8_t* key,uint32_t timestamp,const uint8_t* digest,uint32_t now) {
    for(const auto& e:entries)
      if(e.used && int32_t(now-e.expires)<0 && e.timestamp==timestamp &&
          !memcmp(e.key,key,32) && !memcmp(e.digest,digest,8))return true;
    auto& e=entries[next];next=(next+1)%16;
    memcpy(e.key,key,32);memcpy(e.digest,digest,8);
    e.timestamp=timestamp;e.expires=now+600000UL;e.used=true;
    return false;
  }
};
