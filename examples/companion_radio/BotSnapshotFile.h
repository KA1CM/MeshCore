#pragma once
#include <SPIFFS.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

// Alternate files so a failed or interrupted write preserves the last snapshot.
// Uses the filesystem already mounted by firmware; never formats or erases it.
class BotSnapshotFile {
  struct Header {uint32_t magic=0x42534e50,version=1,length=0,generation=0,checksum=0;};
  const char* paths[2];
  int active=-1;
  uint32_t generation=0;
  const char* error="storage unavailable";
  static uint32_t hash(uint32_t value,const uint8_t* data,size_t size) {
    for(size_t i=0;i<size;++i)value=(value^data[i])*16777619UL;
    return value;
  }
  bool read(int slot,void* dest,size_t size,uint32_t& sequence) {
    auto file=SPIFFS.open(paths[slot],FILE_READ);
    Header header;
    if(!file || file.read((uint8_t*)&header,sizeof(header))!=sizeof(header) ||
        header.magic!=0x42534e50 || header.version!=1 || header.length!=size ||
        file.size()!=sizeof(header)+size)return false;
    uint8_t block[128];size_t offset=0;
    uint32_t checksum=hash(2166136261UL,(const uint8_t*)&header,offsetof(Header,checksum));
    while(offset<size) {
      const size_t length=size-offset<sizeof(block) ? size-offset : sizeof(block);
      if(file.read(block,length)!=length)return false;
      checksum=hash(checksum,block,length);
      if(dest)memcpy((uint8_t*)dest+offset,block,length);
      offset+=length;
    }
    if(checksum!=header.checksum)return false;
    sequence=header.generation;return true;
  }
public:
  enum Load {Missing,Loaded,Invalid,Unavailable};
  BotSnapshotFile(const char* first,const char* second):paths{first,second}{}
  const char* lastError() const {return error;}
  Load load(void* dest,size_t size) {
    if(!SPIFFS.totalBytes()){error="filesystem unavailable";return Unavailable;}
    uint32_t first=0,second=0;
    const bool a=read(0,nullptr,size,first),b=read(1,nullptr,size,second);
    if(!a && !b) {
      if(SPIFFS.exists(paths[0]) || SPIFFS.exists(paths[1])){error="snapshot invalid";return Invalid;}
      return Missing;
    }
    active=b && (!a || (int32_t)(second-first)>0) ? 1 : 0;
    if(!read(active,dest,size,generation)){error="snapshot read failed";return Invalid;}
    error="";return Loaded;
  }
  bool save(const void* data,size_t size) {
    if(!SPIFFS.totalBytes()){error="filesystem unavailable";return false;}
    const int slot=active==0 ? 1 : 0;
    Header header;header.length=size;header.generation=generation+1;
    header.checksum=hash(hash(2166136261UL,(const uint8_t*)&header,offsetof(Header,checksum)),(const uint8_t*)data,size);
    auto file=SPIFFS.open(paths[slot],FILE_WRITE);
    if(!file){error="snapshot open failed";return false;}
    const bool written=file.write((const uint8_t*)&header,sizeof(header))==sizeof(header) &&
      file.write((const uint8_t*)data,size)==size;
    file.flush();file.close();
    if(!written){error="snapshot write failed";return false;}
    uint32_t sequence=0;
    if(!read(slot,nullptr,size,sequence) || sequence!=header.generation){error="snapshot verify failed";return false;}
    active=slot;generation=sequence;error="";return true;
  }
};
