#pragma once
#include "BotScopedRepeaters.h"
#include "BotVoltageList.h"
#include "BotPath.h"

namespace BotScopedHashList {
// Snapshot compact, complete hashes into the existing multipart queue. Twelve
// hashes fit each small row; the pager joins rows with commas, not newlines.
inline void build(const BotScopedRepeaters::Store& saved,uint32_t day,BotVoltageList::Snapshot& out) {
  out.count=0;
  size_t used=0,inRow=0;
  for(size_t i=0;i<saved.count && i<BotScopedRepeaters::Capacity;++i) {
    const auto& entry=saved.entries[i];
    if(!day || !entry.lastDay || entry.lastDay>day || day-entry.lastDay>=30) continue;
    if(!inRow) {
      if(out.count>=sizeof(out.lines)/sizeof(out.lines[0])) break;
      out.lines[out.count++][0]=0;used=0;
    }
    char hash[6];
    snprintf(hash,sizeof(hash),"%02x%02x%s",entry.hash[0],entry.hash[1],
      BotPath::fixedName(entry.hash,2) ? "." : "");
    const size_t length=strlen(hash);
    if(used+(inRow ? 1 : 0)+length>=BotVoltageList::LINE_SIZE) {
      if(out.count>=sizeof(out.lines)/sizeof(out.lines[0])) break;
      out.lines[out.count++][0]=0;used=0;inRow=0;
    }
    char* row=out.lines[out.count-1];
    if(inRow) row[used++]=',';
    memcpy(row+used,hash,length);used+=length;row[used]=0;
    if(++inRow==12) inRow=0;
  }
  if(!out.count) {
    strcpy(out.lines[out.count++],"No ct-ffc repeaters observed in the last 30 days.");
  }
}
}
