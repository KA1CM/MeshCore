#pragma once

#include "BotTypes.h"

#include <stddef.h>
#include <stdint.h>

namespace BotPrefsCodec {

size_t serializedSize();
void defaults(BotPrefs& prefs);
void validate(BotPrefs& prefs);
bool serialize(const BotPrefs& prefs, uint8_t* output, size_t output_len);
bool deserialize(const uint8_t* data, size_t data_len, BotPrefs& prefs);
bool channelNameValid(const char* value, bool public_channel);
bool channelConfigValid(const BotPrefs& prefs);

uint32_t commandMaskFor(BotCommandId command_id);
bool commandEnabled(const BotPrefs& prefs, BotCommandId command_id);
void setCommandEnabled(BotPrefs& prefs, BotCommandId command_id, bool enabled);
const char* commandName(BotCommandId command_id);
bool commandIdForName(const char* name, BotCommandId* command_id);

bool parseKeyPrefixHex(const char* text, uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]);
void formatKeyPrefixHex(const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], char* output, size_t output_len);
bool addKnownBot(BotPrefs& prefs, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], uint8_t flags,
                 const char* label);
bool removeKnownBot(BotPrefs& prefs, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]);
const BotKnownBotEntry* findKnownBot(const BotPrefs& prefs, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]);

}
