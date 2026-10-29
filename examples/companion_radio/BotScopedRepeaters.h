#pragma once
#include <cstdint>
#include <cstddef>

// Observed two-byte prefixes, not proven unique repeater identities.
class BotScopedRepeaters {
public:
  static constexpr size_t Capacity=256;
  struct Entry {uint8_t hash[2]{};uint32_t lastDay=0;};
  struct Store {uint32_t version=1,savedDay=0,count=0;Entry entries[Capacity]{};};
  Store data;
private:
  uint32_t prunedDay=0;
public:
  void prune(uint32_t day) {
    if(!day || day==prunedDay)return;
    prunedDay=day;
    size_t keep=0;
    for(size_t i=0;i<data.count;++i)
      if(data.entries[i].lastDay<=day && day-data.entries[i].lastDay<30)
        data.entries[keep++]=data.entries[i];
    data.count=keep;
  }
  void observe(const uint8_t* path,size_t hops,size_t width,uint32_t day) {
    if(!path || !day || (width!=2 && width!=3) || hops*width>64)return;
    prune(day);
    for(size_t hop=0;hop<hops;++hop) {
      const uint8_t* hash=path+hop*width;
      size_t slot=data.count,oldest=0;
      for(size_t i=0;i<data.count;++i) {
        if(data.entries[i].hash[0]==hash[0] && data.entries[i].hash[1]==hash[1]){slot=i;break;}
        if(data.entries[i].lastDay<data.entries[oldest].lastDay)oldest=i;
      }
      if(slot==data.count) {
        if(data.count==Capacity)slot=oldest;
        else ++data.count;
      }
      data.entries[slot].hash[0]=hash[0];data.entries[slot].hash[1]=hash[1];
      data.entries[slot].lastDay=day;
    }
  }
};
