#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace BotShortName {
// Callers provide 33 bytes. Preserve UTF-8 boundaries and the storage limit,
// as well as the 20-character display limit.
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
inline bool gridWord(const char* p) {
  return (p[0]=='f' || p[0]=='F') && (p[1]=='n' || p[1]=='N') && p[2]=='3' && p[3]=='1';
}
inline void write(const char* name, char* out) {
  out[0]=0;
  if(!name) return;
  while(*name==' ') ++name;
  size_t n=0, characters=0;
  const char* start=name;
  while(*name && characters<20) {
    // Preserve the existing trailing grid-suffix rule, now case-insensitive.
    if(name[0]=='-' && name[1]==' ' && gridWord(name+2)) break;
    // Remove whole space-delimited words before applying display limits.
    if((name==start || name[-1]==' ') && gridWord(name)) {
      while(*name && *name!=' ' && leadingBytes(name)) name+=leadingBytes(name);
      while(*name==' ') ++name;
      continue;
    }
    const size_t bytes=leadingBytes(name);
    if(!bytes || n+bytes>32) break;
    memcpy(out+n,name,bytes); n+=bytes; name+=bytes; ++characters;
    // Keep an emoji presentation selector with its preceding character.
    if(bytes>1 && (unsigned char)name[0]==0xef &&
       (unsigned char)name[1]==0xb8 &&
       ((unsigned char)name[2]==0x8f || (unsigned char)name[2]==0x8e)) {
      if(n+3>32) break;
      memcpy(out+n,name,3); n+=3; name+=3;
    }
  }
  // A multiword name must not retain the word hit by the character cutoff.
  // Ignore discarded grid suffixes and trailing spaces when checking for overflow.
  const char* remainder=name;
  while(*remainder) {
    while(*remainder==' ')++remainder;
    if(remainder[0]=='-' && remainder[1]==' ' && gridWord(remainder+2)){remainder+=strlen(remainder);break;}
    if((remainder==start || remainder[-1]==' ') && gridWord(remainder)) {
      while(*remainder && *remainder!=' ' && leadingBytes(remainder))remainder+=leadingBytes(remainder);
      if(*remainder && *remainder!=' ')break;
      continue;
    }
    break;
  }
  bool multipleWords=false;
  for(const char* p=start;*p;++p)if(*p==' ') {
    while(*p==' ')++p;
    if(*p)multipleWords=true;
    break;
  }
  if(characters==20 && leadingBytes(remainder) && multipleWords && n && out[n-1]!=' ') {
    while(n && out[n-1]!=' ')--n;
  }
  while(n && out[n-1]==' ') --n;
  out[n]=0;
}
}
