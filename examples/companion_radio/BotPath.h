#pragma once
#include "BotTypes.h"
#include "BotShortName.h"
#include <cstdio>
#include <cstring>

namespace BotPath {
// Explicit local exception for a known forwarding companion. Match only the
// configured two-byte hash; do not guess identities for wider prefixes.
inline const char* fixedName(const uint8_t* hash, size_t width) {
  return width==2 && hash[0]==0x08 && hash[1]==0x83 ? "SUPER_SQUIRREL 🐿️" : nullptr;
}
struct Route {
  bool all = false;
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
// Share the 20-character short-name rule with list and signal responses.
inline void shortName(const char* name, char* out) { BotShortName::write(name,out); }
inline BotCommandResult format(const Route& route, const char* target, char* out, size_t capacity);
inline BotCommandResult formatAll(const Route& route, const char* target, size_t first,
                                  size_t& next, char* out, size_t capacity) {
  next=first;
  if (!out || !capacity || !route.width || route.width>4 ||
      route.count*route.width>BOT_MAX_PATH_BYTES || (route.count && first>=route.count))
    return {BOT_COMMAND_RESULT_NO_SPACE,0};
  auto label=[&](size_t hop,char* text) {
    if(route.names[hop][0] && !route.ambiguous[hop])strcpy(text,route.names[hop]);
    else {
      hash(route,hop,text);
      // A dot means no reliable name, including colliding prefixes.
      // Bare hashes are reserved for deliberately displaying a known hash.
      text[route.width*2]='.';text[route.width*2+1]=0;
    }
  };
  // Reserve the largest possible page number (64/64) so counting and
  // writing use identical boundaries, without shortening any resolved name.
  char heading[BOT_MAX_SENDER_NAME_LEN+12];
  const int reserved=target && *target ?
    snprintf(heading,sizeof(heading),"@[%.*s] 64/64\n",BOT_MAX_SENDER_NAME_LEN,target) :
    snprintf(heading,sizeof(heading),"64/64\n");
  if(reserved<0 || (size_t)reserved>=capacity)return {BOT_COMMAND_RESULT_NO_SPACE,0};
  auto endOfPart=[&](size_t start) {
    size_t used=reserved,end=start;
    while(end<route.count) {
      char text[33];label(end,text);
      const size_t length=strlen(text)+(end>start ? 1 : 0);
      if(used+length>=capacity)break;
      used+=length;++end;
    }
    return end;
  };
  size_t total=0,page=0,end=0;
  for(size_t start=0;start<route.count;) {
    const size_t limit=endOfPart(start);
    if(limit==start)return {BOT_COMMAND_RESULT_NO_SPACE,0};
    ++total;
    if(start==first){page=total;end=limit;}
    start=limit;
  }
  if(!route.count){total=page=1;}
  if(!page)return {BOT_COMMAND_RESULT_NO_SPACE,0};
  int written=target && *target ?
    snprintf(out,capacity,"@[%.*s] %u/%u\n",BOT_MAX_SENDER_NAME_LEN,target,(unsigned)page,(unsigned)total) :
    snprintf(out,capacity,"%u/%u\n",(unsigned)page,(unsigned)total);
  if(written<0 || (size_t)written>=capacity)return {BOT_COMMAND_RESULT_NO_SPACE,0};
  size_t pos=written;
  if(!route.count) {
    if(pos+6>=capacity)return {BOT_COMMAND_RESULT_NO_SPACE,0};
    memcpy(out+pos,"Direct",6);pos+=6;
  }
  for(size_t hop=first;hop<end;++hop) {
    char text[33];label(hop,text);
    if(hop>first)out[pos++]='\n';
    const size_t length=strlen(text);memcpy(out+pos,text,length);pos+=length;
  }
  out[pos]=0;next=end;
  return {BOT_COMMAND_RESULT_OK,pos};
}
inline BotCommandResult format(const Route& route, const char* target, char* out, size_t capacity) {
  if (!out || !capacity) return {BOT_COMMAND_RESULT_NO_SPACE,0};
  out[0]=0;
  if (!route.width || route.width>4 || route.count*route.width>BOT_MAX_PATH_BYTES)
    return {BOT_COMMAND_RESULT_NO_SPACE,0};
  if(route.width==1 && route.count) {
    const char* text="1-byte paths are not supported.";
    const int length=target && *target ?
      snprintf(out,capacity,"@[%.*s] %s",BOT_MAX_SENDER_NAME_LEN,target,text) :
      snprintf(out,capacity,"%s",text);
    if(length<0 || (size_t)length>=capacity) { out[0]=0; return {BOT_COMMAND_RESULT_NO_SPACE,0}; }
    return {BOT_COMMAND_RESULT_OK,(size_t)length};
  }
  char heading[BOT_MAX_SENDER_NAME_LEN+5]{};
  if (target && *target) snprintf(heading,sizeof(heading),"@[%.*s]\n",BOT_MAX_SENDER_NAME_LEN,target);
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
  auto unresolvedName=[&](size_t i) {
    // Retain the special-case marker if the fixed name is compacted to a hash.
    return !route.names[i][0] || route.ambiguous[i] || fixedName(route.bytes+i*route.width,route.width);
  };
  size_t omitted=0;
  auto required = [&]() {
    size_t used=head, lines=0;
    for(size_t i=0;i<route.count;++i) if(shown[i]) {
      used+=named[i] ? displayLen[i] : route.width*2+(unresolvedName(i)?1:0); ++lines;
    }
    if(omitted) {
      used+=3; ++lines; // A standalone "..." marks omitted middle hops.
    }
    return used+(lines ? lines-1 : 0);
  };
  // Protect the first five hops and the final hop. Compact only the middle.
  while(required()>=capacity) {
    size_t best=route.count, saving=0;
    for(size_t i=5;i+1<route.count;++i) if(named[i] && strlen(route.names[i])>route.width*2) {
      const size_t gain=strlen(route.names[i])-route.width*2;
      if(gain>saving) {saving=gain; best=i;}
    }
    if(best==route.count) break;
    named[best]=false;
  }
  // Even hashes may exceed one packet. Explicitly omit a contiguous middle
  // section after the first five hops, retaining the final hop.
  for(size_t i=5; required()>=capacity && i+1<route.count; ++i) {
    shown[i]=false; ++omitted;
  }
  // If the protected first five names still exceed the packet, replace the
  // longest ones with complete hashes. Keep their positions and the final name.
  while(required()>=capacity) {
    size_t best=route.count, saving=0;
    for(size_t i=0;i<5 && i+1<route.count;++i) if(named[i] && displayLen[i]>route.width*2) {
      const size_t gain=displayLen[i]-route.width*2;
      if(gain>saving){best=i;saving=gain;}
    }
    if(best==route.count)break;
    named[best]=false;
  }
  if(required()>=capacity && named[route.count-1])named[route.count-1]=false;
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

    }
    else {
      hash(route,i,label);
      if(unresolvedName(i)){label[route.width*2]='.';label[route.width*2+1]=0;}
    }
    if(!first) out[pos++]='\n';
    const size_t len=strlen(label); memcpy(out+pos,label,len); pos+=len; first=false;
  }
  out[pos]=0;
  return {BOT_COMMAND_RESULT_OK,pos};
}
}
