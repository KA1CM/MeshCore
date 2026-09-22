#pragma once

#include "BotTypes.h"

#include <stddef.h>

namespace BotCommandRegistry {

size_t commandCount();
const BotCommandMetadata* commandAt(size_t index);
const BotCommandMetadata* findById(BotCommandId id);
const BotCommandMetadata* findByName(const char* name, size_t len);
const char* commandName(BotCommandId id);
uint32_t commandMask(BotCommandId id);
bool isDiscoverable(BotCommandId id);

}
