#pragma once

#include "BotTypes.h"

namespace EmergencyForwarder {

bool isForwardedEmergencyText(const char* text, size_t text_len);
bool format(const BotMessage& message, BotEmergencyForward& forward);

}
