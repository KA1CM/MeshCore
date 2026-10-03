#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace BotShortName {
// Callers provide 33 bytes. Preserve UTF-8 boundaries and the storage limit,
// as well as the 24-character display limit.
inline size_t leadingBytes(const char* text) {
  if(!text || !*text) return 0;
  const unsigned char first=(unsigned char)text[0];
  if(first<0x80) return first>=0x20 && first!=0x7f ? 1 : 0;
  size_t size=first>=0xc2 && first<=0xdf ? 2 : first>=0xe0 && first<=0xef ? 3 : first>=0xf0 && first<=0xf4 ? 4 : 0;
  if(!size) return 0;
  uint32_t value=first & (size==2 ? 31 : size==3 ? 15 : 7);
  for(size_t i=1;i<size;++i) {
    const unsigned char b=(unsigned char)text[i];
    if((b&0xc0)!=0x80) return 0;
    value=(value<<6)|(b&63);
  }
  if((size==2 && value<0x80) || (size==3 && value<0x800) || (size==4 && value<0x10000) ||
     value>0x10ffff || (value>=0xd800 && value<=0xdfff) || value<0xa0 ||
     (value==0x200b || value==0x200e || value==0x200f) || (value>=0x2028 && value<=0x202e) ||
     (value>=0x2060 && value<=0x206f) || value==0xfeff) return 0;
  return size;
}
inline void write(const char* name, char* out) {
  out[0]=0;
  if(!name) return;
  while(*name==' ') ++name;
  size_t n=0, characters=0;
  bool firstWord=true;
  while(name[n] && characters<24) {
    if(name[n]==' ') firstWord=false;
    size_t bytes=0;
    if(firstWord) bytes=leadingBytes(name+n);
    else if((name[n]>='A' && name[n]<='Z') || (name[n]>='a' && name[n]<='z') ||
            (name[n]>='0' && name[n]<='9') || name[n]==' ') bytes=1;
    if(!bytes || n+bytes>32) break;
    memcpy(out+n,name+n,bytes); n+=bytes; ++characters;
    // Keep an emoji presentation selector with its preceding character.
    if(firstWord && bytes>1 && (unsigned char)name[n]==0xef &&
       (unsigned char)name[n+1]==0xb8 &&
       ((unsigned char)name[n+2]==0x8f || (unsigned char)name[n+2]==0x8e)) {
      if(n+3>32) break;
      memcpy(out+n,name+n,3); n+=3;
    }
  }
  while(n && out[n-1]==' ') --n;
  out[n]=0;
}
}
