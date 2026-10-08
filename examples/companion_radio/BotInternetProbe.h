#pragma once
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
namespace BotInternetProbe {
bool busy(); // main loop only
bool start();
bool take(bool& ok);
}
#endif
