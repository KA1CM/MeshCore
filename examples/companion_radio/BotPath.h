#pragma once
#include "BotTypes.h"
#include "BotShortName.h"
#include <cstdio>
#include <cstring>

namespace BotPath {
struct Route {
  uint8_t bytes[BOT_MAX_PATH_BYTES]{};
  uint8_t width = 0, count = 0;
  char names[BOT_MAX_PATH_BYTES][33]{};
  bool ambiguous[BOT_MAX_PATH_BYTES]{};
};
inline void hash(const Route& route, size_t hop, char* out) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i=0; i<route.width; ++i) {
    const uint8_t b=route.bytes[hop*route.width+i];
    out[2*i]=hex[b>>4]; out[2*i+1]=hex[b&15];
  }
  out[2*route.width]=0;
}
// Share the 24-character short-name rule with list and signal responses.
inline void shortName(const char* name, char* out) { BotShortName::write(name,out); }
inline BotCommandResult format(const Route& route, const char* target, char* out, size_t capacity) {
  if (!out || !capacity) return {BOT_COMMAND_RESULT_NO_SPACE,0};
  out[0]=0;
  if (!route.width || route.width>4 || route.count*route.width>BOT_MAX_PATH_BYTES)
    return {BOT_COMMAND_RESULT_NO_SPACE,0};
  if(route.width==1 && route.count) {
    const char* text="1-byte paths are not supported.";
    const int length=target && *target ?
      snprintf(out,capacity,"@%.*s %s",BOT_MAX_SENDER_NAME_LEN,target,text) :
      snprintf(out,capacity,"%s",text);
    if(length<0 || (size_t)length>=capacity) { out[0]=0; return {BOT_COMMAND_RESULT_NO_SPACE,0}; }
    return {BOT_COMMAND_RESULT_OK,(size_t)length};
  }
  char heading[BOT_MAX_SENDER_NAME_LEN+3]{};
  if (target && *target) snprintf(heading,sizeof(heading),"@%.*s\n",BOT_MAX_SENDER_NAME_LEN,target);
  const size_t head=strlen(heading);
  if (!route.count) {
    if (head+6>=capacity) return {BOT_COMMAND_RESULT_NO_SPACE,0};
    snprintf(out,capacity,"%sDirect",heading);
    return {BOT_COMMAND_RESULT_OK,strlen(out)};
  }
  bool named[BOT_MAX_PATH_BYTES]{};
  bool shown[BOT_MAX_PATH_BYTES]{};
  size_t displayLen[BOT_MAX_PATH_BYTES]{};
  for (size_t i=0;i<route.count;++i) {
    named[i]=route.names[i][0] && !route.ambiguous[i]; shown[i]=true;
    displayLen[i]=named[i] ? strlen(route.names[i]) : route.width*2;
  }
  size_t omitted=0;
  auto required = [&]() {
    size_t used=head, lines=0;
    for(size_t i=0;i<route.count;++i) if(shown[i]) {
      used+=named[i] ? displayLen[i] : route.width*2; ++lines;
    }
    if(omitted) {
      used+=3; ++lines; // A standalone "..." marks omitted middle hops.
    }
    return used+(lines ? lines-1 : 0);
  };
  // Protect the first three hops and the final hop. Compact only the middle.
  while(required()>=capacity) {
    size_t best=route.count, saving=0;
    for(size_t i=3;i+1<route.count;++i) if(named[i] && strlen(route.names[i])>route.width*2) {
      const size_t gain=strlen(route.names[i])-route.width*2;
      if(gain>saving) {saving=gain; best=i;}
    }
    if(best==route.count) break;
    named[best]=false;
  }
  // Even hashes may exceed one packet. Explicitly omit a contiguous middle
  // section after the first three hops, retaining the final hop.
  for(size_t i=3; required()>=capacity && i+1<route.count; ++i) {
    shown[i]=false; ++omitted;
  }
  // Four 32-byte names plus a sender may exceed a packet by themselves.
  // Keep the final name intact; explicitly abbreviate the longest of the first
  // three names with '~' until they fit. Never replace protected names by hashes.
  while(required()>=capacity) {
    size_t best=route.count, longest=4;
    for(size_t i=0;i<3 && i+1<route.count;++i) if(named[i] && displayLen[i]>longest && displayLen[i]>BotShortName::leadingBytes(route.names[i])+1) {
      best=i; longest=displayLen[i];
    }
    if(best==route.count) break;
    --displayLen[best];
  }
  if(required()>=capacity) return {BOT_COMMAND_RESULT_NO_SPACE,0};
  size_t pos=0;
  memcpy(out,heading,head); pos=head;
  bool first=true, markerWritten=false;
  for(size_t i=0;i<route.count;++i) {
    char label[33];
    if(!shown[i]) {
      if(markerWritten) continue;
      snprintf(label,sizeof(label),"..."); markerWritten=true;
    } else if(named[i]) {
      memcpy(label,route.names[i],displayLen[i]); label[displayLen[i]]=0;
      if(displayLen[i]<strlen(route.names[i])) {
        size_t keep=displayLen[i]-1;
        while(keep && ((unsigned char)route.names[i][keep]&0xc0)==0x80) --keep;
        label[keep]='~'; label[keep+1]=0;
      }
    }
    else hash(route,i,label);
    if(!first) out[pos++]='\n';
    const size_t len=strlen(label); memcpy(out+pos,label,len); pos+=len; first=false;
  }
  out[pos]=0;
  return {BOT_COMMAND_RESULT_OK,pos};
}
}
