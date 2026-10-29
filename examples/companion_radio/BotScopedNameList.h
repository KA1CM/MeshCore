#pragma once
#include "BotScopedRepeaters.h"
#include "BotLookupMemory.h"
#include <memory>
#include <new>
#include <cstring>
#include <cctype>

namespace BotScopedNameList {
// Allocate only for a radio search; bulk storage prefers PSRAM over TLS RAM.
struct Snapshot { char lines[BotScopedRepeaters::Capacity][97]; size_t count=0; };
struct Free { void operator()(Snapshot* value) const {
  if(value) { value->~Snapshot(); std::free(value); }
} };
using Pointer=std::unique_ptr<Snapshot,Free>;
inline Pointer allocate() {
  void* memory=BotPathLookup::allocateBulk(sizeof(Snapshot));
  return Pointer(memory ? new(memory) Snapshot : nullptr);
}
inline bool matches(const char* name,const char* query) {
  if(!*query)return false;
  for(;*name;++name) {
    const char* a=name;const char* b=query;
    while(*a && *b && std::tolower(static_cast<unsigned char>(*a))==
        std::tolower(static_cast<unsigned char>(*b))) {++a;++b;}
    if(!*b)return true;
  }
  return false;
}
}
