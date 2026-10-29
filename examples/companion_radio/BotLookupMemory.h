#pragma once
#include <cstdlib>
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

namespace BotPathLookup {
// TLS requires internal RAM in the pinned SDK. Prefer PSRAM for bulk data,
// with a normal-heap fallback on boards without usable PSRAM.
inline void* allocateBulk(size_t bytes) {
#if defined(ESP32)
  if(void* memory=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)) return memory;
#endif
  return std::malloc(bytes);
}
struct FreeBulk {
  void operator()(char* memory) const { std::free(memory); }
};
}
