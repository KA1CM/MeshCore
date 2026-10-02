#pragma once
#include <Preferences.h>
#include <stdint.h>
#include <cstring>

// Explicit public-key permissions, independent of contact names and favorites.
class BotAdminContacts {
public:
  enum Permission : uint8_t { Commands = 1, Notifications = 2 };
  struct Record { uint8_t key[32]{}; uint8_t permissions = 0; };
  static constexpr size_t Capacity = 16;
private:
  struct Store { uint32_t version = 1; uint32_t count = 0; Record records[Capacity]{}; } data;
  Preferences prefs;
  bool ready = false;
  static bool valid(const Store& s) {
    if (s.version != 1 || s.count > Capacity) return false;
    for (size_t i=0;i<s.count;++i) {
      bool nonzero=false;for(auto b:s.records[i].key) nonzero |= b!=0;
      if (!nonzero || (s.records[i].permissions & ~3)) return false;
      for(size_t j=0;j<i;++j) if(!memcmp(s.records[i].key,s.records[j].key,32)) return false;
    }
    return true;
  }
  bool commit(const Store& candidate) {
    if(prefs.putBytes("contacts", &candidate, sizeof(candidate))!=sizeof(candidate)) return false;
    data=candidate;return true;
  }
public:
  bool begin() {
    ready=false;
    if(!prefs.begin("bot-admins",false)) return false;
    if(prefs.isKey("contacts")) {
      Store loaded;
      if(prefs.getBytesLength("contacts")!=sizeof(loaded) ||
         prefs.getBytes("contacts",&loaded,sizeof(loaded))!=sizeof(loaded) || !valid(loaded)) return false;
      data=loaded;
    } else {
      // One-time seed: four favorite KA1CM companions verified over USB on 2026-09-30.
      // Never repeat a name-based search or regrant permissions after revocation.
      const uint8_t initial[][32] = {
    {0x34,0xbd,0x40,0x9c,0x22,0x50,0xf0,0x8b,0x1d,0xb0,0x4f,0x8d,0xea,0x58,0x10,0x8f,0x3c,0x17,0x0d,0x65,0x63,0xa0,0x8f,0xfd,0xc1,0xdd,0x5f,0xde,0xe9,0xd0,0xcc,0xd5},
    {0x97,0xc3,0xdc,0x56,0x63,0xde,0xd6,0xca,0xf9,0xa0,0x23,0x55,0x80,0xea,0x0d,0xbb,0x33,0x66,0x5c,0x64,0x13,0x86,0x73,0xc0,0xd5,0x6b,0xb1,0xa1,0x64,0x16,0xcf,0xe8},
    {0x4e,0xae,0xd3,0x09,0xe4,0x3f,0xd2,0xda,0x80,0x61,0x48,0x7c,0xce,0xfc,0xcf,0xbf,0x1f,0x92,0x88,0x7d,0x59,0x1d,0xd3,0x13,0xcc,0x3d,0xc6,0x9d,0xe4,0x69,0xc2,0xb3},
    {0x94,0x1d,0x02,0xdb,0x10,0xa4,0x01,0xa0,0xc9,0xa2,0xeb,0x9f,0x57,0x11,0x5e,0x04,0xda,0x18,0x1b,0x72,0xe6,0xd2,0xce,0xd8,0xf3,0x72,0x73,0x33,0x69,0xd3,0x1b,0x68},
      };
      Store first;first.count=sizeof(initial)/sizeof(initial[0]);
      for(size_t i=0;i<first.count;++i) {
        memcpy(first.records[i].key,initial[i],32);
        first.records[i].permissions=Commands | Notifications;
      }
      if(!commit(first)) return false;
    }
    ready=true;return true;
  }
  bool available() const {return ready;}
  size_t count() const {return ready?data.count:0;}
  const Record* at(size_t i) const {return ready&&i<data.count?&data.records[i]:nullptr;}
  bool allows(const uint8_t* fullKey, Permission permission) const {
    if(!ready || !fullKey) return false;
    for(size_t i=0;i<data.count;++i) if(!memcmp(fullKey,data.records[i].key,32))
      return (data.records[i].permissions & permission)!=0;
    return false;
  }
  bool add(const uint8_t* fullKey, bool commands, bool notifications) {
    if(!ready || !fullKey || data.count>=Capacity) return false;
    bool nonzero=false;for(size_t i=0;i<32;++i) nonzero |= fullKey[i]!=0;
    if(!nonzero) return false;
    for(size_t i=0;i<data.count;++i) if(!memcmp(fullKey,data.records[i].key,32)) return false;
    Store candidate=data;auto& r=candidate.records[candidate.count++];
    memcpy(r.key,fullKey,32);r.permissions=(commands?Commands:0)|(notifications?Notifications:0);
    return commit(candidate);
  }
  bool remove(const uint8_t* fullKey) {
    if(!ready || !fullKey) return false;
    Store candidate=data;
    for(size_t i=0;i<candidate.count;++i) if(!memcmp(fullKey,candidate.records[i].key,32)) {
      for(size_t j=i+1;j<candidate.count;++j) candidate.records[j-1]=candidate.records[j];
      candidate.records[--candidate.count]=Record{};
      return commit(candidate);
    }
    return false;
  }
  bool set(const uint8_t* fullKey, bool commands, bool notifications) {
    if(!ready || !fullKey) return false;
    Store candidate=data;
    for(size_t i=0;i<candidate.count;++i) if(!memcmp(fullKey,candidate.records[i].key,32)) {
      candidate.records[i].permissions=(commands?Commands:0)|(notifications?Notifications:0);
      return commit(candidate);
    }
    return false;
  }
};
