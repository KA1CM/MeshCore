#pragma once
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include <ArduinoJson.h>
namespace BotPiPublisher {
constexpr size_t SnapshotLimit=49152;
bool begin();
bool busy();
bool manualPending();
enum class Completion {None,Published,Unchanged,Failed};
Completion takeCompletion(const char*& reason);
bool due(uint32_t day,uint32_t ms);
bool beginPreparation(uint32_t day);
void preparationFailed(const char* reason,uint32_t ms);
void resolvingNames();
const char* requestManual(uint32_t day,uint32_t ms);
bool configure(bool enabled,const char* host,uint16_t port,const char* token);
ArduinoJson::Allocator* jsonAllocator(); // PSRAM only; stable lifetime.
// Takes ownership only when true; retains the snapshot until Pi confirmation.
bool start(char* body,size_t length,uint32_t day,uint32_t ms);
void poll(uint32_t utc,uint32_t ms,bool networkIdle);
void exportState(JsonObject out);
}
#endif
