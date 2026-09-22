#pragma once

#include "BotTypes.h"

namespace BotPolicy {

BotChannelKind classifyChannel(const char* name, size_t len, bool direct_message);
BotChannelKind classifyChannel(const char* name, size_t len, bool direct_message, const BotPrefs& prefs);
BotPolicyDecision decide(BotChannelKind kind);
bool isNormalAllowed(BotChannelKind kind);
bool isEmergency(BotChannelKind kind);
bool isPrefixlessCommandAllowed(BotChannelKind kind);

}
