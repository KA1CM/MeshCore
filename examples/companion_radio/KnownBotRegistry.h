#pragma once

#include "BotTypes.h"

namespace KnownBotRegistry {

void clear(BotKnownBotEntry entries[], size_t entry_count);
bool add(BotKnownBotEntry entries[], size_t entry_count, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN],
         uint8_t flags, const char* label);
bool remove(BotKnownBotEntry entries[], size_t entry_count, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]);
const BotKnownBotEntry* find(const BotKnownBotEntry entries[], size_t entry_count,
                             const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], size_t key_prefix_len);
bool canSuppressNormal(const BotKnownBotEntry entries[], size_t entry_count,
                       const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], size_t key_prefix_len);

}
