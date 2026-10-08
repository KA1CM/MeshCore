#pragma once
#include "BotTypes.h"
#include "BotVoltageList.h"
#include <cstring>
#include <cstdio>
// Quarter-hour buckets match rolling command stats. Oldest partial bucket is discarded.
class BotTopUsers {
public:
  struct User { char name[BOT_MAX_SENDER_NAME_LEN+1]{}; uint16_t counts[96]{}; };
private:
  User users[32]{};
  size_t slot=0;uint32_t last=0,remainder=0;bool started=false;
  uint32_t overflowAt=0;bool overflow=false;
  static uint32_t total(const User& u){uint32_t n=0;for(auto v:u.counts)n+=v;return n;}
  void advance(uint32_t now){
    if(!started){started=true;last=now;return;}
    uint64_t elapsed=(uint64_t)remainder+(uint32_t)(now-last);last=now;
    uint64_t steps=elapsed/900000;remainder=elapsed%900000;
    if(steps>=96){for(auto& u:users)memset(u.counts,0,sizeof(u.counts));slot=0;}
    else while(steps--){slot=(slot+1)%96;for(auto& u:users)u.counts[slot]=0;}
    if(overflow && (uint32_t)(now-overflowAt)>=86400000)overflow=false;
  }
public:
  void accepted(const BotMessage& message,uint32_t now){
    advance(now);
    // Display-name grouping is only for statistics, never authorization.
    if(!message.sender_name[0])return;
    User* found=nullptr;User* empty=nullptr;
    for(auto& u:users){
      if(!total(u)){if(!empty)empty=&u;continue;}
      if(!strcmp(u.name,message.sender_name)){found=&u;break;}
    }
    if(!found){if(!empty){overflow=true;overflowAt=now;return;}found=empty;memset(found->counts,0,sizeof(found->counts));}
    snprintf(found->name,sizeof(found->name),"%s",message.sender_name[0]?message.sender_name:"Unknown user");
    if(found->counts[slot]<65535)++found->counts[slot];
  }
  size_t top(uint32_t now,const User** out,uint32_t* counts,size_t limit=5){
    if(limit>10)limit=10;
    advance(now);size_t n=0;
    for(const auto& u:users){const uint32_t value=total(u);if(!value)continue;
      size_t pos=0;while(pos<n&&(counts[pos]>value||(counts[pos]==value&&strcmp(out[pos]->name,u.name)<=0)))++pos;
      if(pos>=limit)continue;
      if(n<limit)++n;
      for(size_t j=n-1;j>pos;--j){out[j]=out[j-1];counts[j]=counts[j-1];}
      out[pos]=&u;counts[pos]=value;
    }return n;
  }
  void snapshot(uint32_t now,BotVoltageList::Snapshot& out){
    out.count=0;strcpy(out.lines[out.count++],"Top 5 users in the last 24h");
    const User* users[5]{};uint32_t counts[5]{};const size_t n=top(now,users,counts);
    for(size_t i=0;i<n;++i){
      char name[BOT_MAX_SENDER_NAME_LEN+1];memcpy(name,users[i]->name,sizeof(name));
      for(char* p=name;*p;++p)if(*p=='['||*p==']'||(unsigned char)*p<32)*p=' ';
      snprintf(out.lines[out.count++],BotVoltageList::LINE_SIZE,"@[%s] %lu",name,(unsigned long)counts[i]);
    }
    if(!n)strcpy(out.lines[out.count++],"No users recorded yet");
  }
  bool limited()const{return overflow;}
};
