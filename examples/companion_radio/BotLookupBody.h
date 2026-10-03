#pragma once
#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include <memory>
#include <new>
namespace BotPathLookup {
// HTTP bodies may be close-delimited or chunked, with no Content-Length.
// Independently cap decoded bytes, and reject failed/truncated transfers.
class Body {
  static constexpr size_t LIMIT=32768;
  std::unique_ptr<char[]> buffer;
  size_t used=0;
  bool failed=false;
public:
  Body() : buffer(new(std::nothrow) char[LIMIT+1]) { if(buffer) buffer[0]=0; }
  static bool accepts(int status, int length) {
    return status==200 && (length==-1 || (length>0 && length<=(int)LIMIT));
  }
  size_t append(const uint8_t* bytes, size_t count) {
    if(failed || !buffer || count>LIMIT-used) {failed=true; return 0;}
    if(count) std::memcpy(buffer.get()+used,bytes,count);
    used+=count; buffer[used]=0; return count;
  }
  bool complete(int length, int transferred) const {
    return !failed && buffer && transferred>0 && (size_t)transferred==used &&
           (length==-1 || (length>0 && (size_t)length==used));
  }
  const char* data() const { return buffer.get(); }
  size_t size() const { return used; }
};
}
