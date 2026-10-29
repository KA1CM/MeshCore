#pragma once
#include "BotPath.h"
#include <ArduinoJson.h>
namespace BotPathLookup {
inline bool readEntry(JsonObjectConst entry, const char* prefix, char* name, bool& ambiguous, bool fullName=false, size_t capacity=33) {
  name[0]=0; ambiguous=false;
  if(entry.isNull()) return false;
  const char* confidence=entry["confidence"] | "";
  JsonArrayConst candidates=entry["candidates"].as<JsonArrayConst>();
  JsonArrayConst conflicts=entry["conflicts"].as<JsonArrayConst>();
  ambiguous=candidates.size()>1 || conflicts.size()>0;
  if(!strcmp(confidence,"unique_prefix") && candidates.size()==1 && !ambiguous) {
    const char* key=candidates[0]["pubkey"] | "";
    bool valid=strlen(key)==64 && !strncmp(key,prefix,strlen(prefix));
    for(size_t k=0;valid && k<64;++k)
      valid=(key[k]>='0' && key[k]<='9') || (key[k]>='a' && key[k]<='f');
    if(valid) {
      const char* source=candidates[0]["name"] | "";
      if(!fullName)BotPath::shortName(source,name);
      else if(capacity) {
        size_t length=0;
        while(source[length] && (uint8_t)source[length]>=32 && length+1<capacity)++length;
        if(source[length] && ((uint8_t)source[length]&0xc0)==0x80)
          while(length && ((uint8_t)source[length]&0xc0)==0x80)--length;
        memcpy(name,source,length);name[length]=0;
      }
    }
  }
  return true;
}
}
