#pragma once
#include "RepeaterMonitorCore.h"
#include "BotShortName.h"
#include <cstdio>
#include <cstring>
namespace BotVoltageList {
constexpr size_t LINE_SIZE = 64;
constexpr const char* EMPTY_LOW = "no repeaters with voltage lower than 3.6v or N/A";
struct Snapshot { char lines[40][LINE_SIZE]{}; size_t count = 0; };
inline void shortName(const char* name, const uint8_t* key, char* out) {
  BotShortName::write(name,out);
  if (!out[0]) snprintf(out, 33, "[%02x%02x%02x%02x]", key[0],key[1],key[2],key[3]);
}
inline void build(const MonitorCore::Entry* entries, const char names[][33], size_t count, Snapshot& out, const size_t* order = nullptr, bool lowOnly = false) {
  const size_t total = count > MonitorCore::MAX_REPEATERS ? MonitorCore::MAX_REPEATERS : count;
  out.count = 0;
  for(size_t row=0;row<total;++row) {
    const size_t i=order ? order[row] : row;
    if (!entries[i].enabled) continue;
    bool duplicate=false;
    for(size_t j=0;j<total;++j) if(i!=j && entries[j].enabled && !strcmp(names[i],names[j])) duplicate=true;
    char label[48];
    if(duplicate) snprintf(label,sizeof(label),"%s[%02x%02x%02x%02x]",names[i],entries[i].key[0],entries[i].key[1],entries[i].key[2],entries[i].key[3]);
    else snprintf(label,sizeof(label),"%s",names[i]);
    const MonitorCore::Reading* latest=nullptr;
    for(const auto& reading:entries[i].readings)
      if(reading.timestamp && (!latest || reading.timestamp>latest->timestamp)) latest=&reading;
    if(lowOnly && latest && latest->result==MonitorCore::Ok && latest->millivolts>=3600) continue;
    const size_t outputRow=out.count++;
    if(latest && latest->result==MonitorCore::Ok)
      snprintf(out.lines[outputRow],LINE_SIZE,"%s %u.%02uV",label,((latest->millivolts+5)/10)/100,((latest->millivolts+5)/10)%100);
    else snprintf(out.lines[outputRow],LINE_SIZE,"%s N/A",label);
  }
}
// One message, keeping complete rows in their existing priority order.
inline bool singleMessage(const Snapshot& s, char* out, size_t capacity) {
  if (!capacity) return false;
  size_t used=0;out[0]=0;
  for(size_t i=0;i<s.count;++i) {
    const size_t len=strlen(s.lines[i]), separator=i ? 1 : 0;
    if(used+separator+len>=capacity) break;
    if(separator)out[used++]='\n';
    memcpy(out+used,s.lines[i],len);used+=len;out[used]=0;
  }
  return used!=0;
}
// Reserve enough room for the largest possible heading: 32/32 plus newline.
inline size_t pageEnd(const Snapshot& s, size_t next, size_t capacity, bool noteChunks=false) {
  size_t used=6; const size_t start=next;
  while(next<s.count) {
    const size_t len=strlen(s.lines[next]);
    const size_t separator=next>start && (!noteChunks || next==1) ? 1 : 0;
    if(used+len+separator>=capacity) break;
    used+=len+separator; ++next;
  }
  return next;
}
inline bool page(const Snapshot& s, size_t& next, unsigned part, char* out, size_t capacity, bool noteChunks=false) {
  if(capacity<80 || next>=s.count) return false;
  unsigned total=0;
  for(size_t cursor=0;cursor<s.count;++total) cursor=pageEnd(s,cursor,capacity,noteChunks);
  const size_t end=pageEnd(s,next,capacity,noteChunks);
  size_t used=snprintf(out,capacity,"%u/%u\n",part,total);
  const size_t start=next;
  while(next<end) {
    const size_t len=strlen(s.lines[next]);
    if(next>start && (!noteChunks || next==1)) out[used++]='\n';
    memcpy(out+used,s.lines[next],len);used+=len;++next;
  }
  out[used]=0;return true;
}
}
