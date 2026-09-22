#include "BotPrefs.h"

#include "BotCommandRegistry.h"

#include <string.h>

namespace {

const size_t SERIALIZED_SIZE = BOT_PREFS_SERIALIZED_SIZE;

size_t boundedStrLen(const char* value, size_t max_len) {
  size_t len = 0;
  while (value && len < max_len && value[len] != 0) len++;
  return len;
}

void copyString(char* dest, size_t dest_len, const char* src) {
  if (!dest || dest_len == 0) return;
  size_t len = boundedStrLen(src, dest_len - 1);
  if (len > 0) memcpy(dest, src, len);
  dest[len] = 0;
}

bool channelNameEqual(const char* lhs, const char* rhs) {
  size_t lhs_len = boundedStrLen(lhs, BOT_MAX_CHANNEL_NAME_LEN + 1);
  size_t rhs_len = boundedStrLen(rhs, BOT_MAX_CHANNEL_NAME_LEN + 1);
  if (lhs_len != rhs_len || lhs_len == 0) return false;
  return memcmp(lhs, rhs, lhs_len) == 0;
}

bool channelHasShape(const char* value, bool public_channel) {
  size_t len = boundedStrLen(value, BOT_MAX_CHANNEL_NAME_LEN + 1);
  if (len == 0 || len > BOT_MAX_CHANNEL_NAME_LEN) return false;
  if (public_channel) return value[0] != '#';
  return value[0] == '#' && len > 1;
}

uint32_t checksumBytes(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

void put8(uint8_t* data, size_t& pos, uint8_t value) {
  data[pos++] = value;
}

void put16(uint8_t* data, size_t& pos, uint16_t value) {
  data[pos++] = (uint8_t)(value & 0xFF);
  data[pos++] = (uint8_t)(value >> 8);
}

void put32(uint8_t* data, size_t& pos, uint32_t value) {
  data[pos++] = (uint8_t)(value & 0xFF);
  data[pos++] = (uint8_t)((value >> 8) & 0xFF);
  data[pos++] = (uint8_t)((value >> 16) & 0xFF);
  data[pos++] = (uint8_t)((value >> 24) & 0xFF);
}

uint8_t get8(const uint8_t* data, size_t& pos) {
  return data[pos++];
}

uint16_t get16(const uint8_t* data, size_t& pos) {
  uint16_t value = data[pos];
  value |= ((uint16_t)data[pos + 1]) << 8;
  pos += 2;
  return value;
}

uint32_t get32(const uint8_t* data, size_t& pos) {
  uint32_t value = data[pos];
  value |= ((uint32_t)data[pos + 1]) << 8;
  value |= ((uint32_t)data[pos + 2]) << 16;
  value |= ((uint32_t)data[pos + 3]) << 24;
  pos += 4;
  return value;
}

void putFixedString(uint8_t* data, size_t& pos, const char* value, size_t fixed_len) {
  memset(&data[pos], 0, fixed_len);
  size_t len = boundedStrLen(value, fixed_len);
  if (len > 0) memcpy(&data[pos], value, len);
  pos += fixed_len;
}

void getFixedString(const uint8_t* data, size_t& pos, char* value, size_t fixed_len) {
  memcpy(value, &data[pos], fixed_len);
  value[fixed_len - 1] = 0;
  pos += fixed_len;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool sameKeyPrefix(const uint8_t lhs[BOT_SENDER_KEY_PREFIX_LEN], const uint8_t rhs[BOT_SENDER_KEY_PREFIX_LEN]) {
  return memcmp(lhs, rhs, BOT_SENDER_KEY_PREFIX_LEN) == 0;
}

}

namespace BotPrefsCodec {

size_t serializedSize() {
  return SERIALIZED_SIZE;
}

void defaults(BotPrefs& prefs) {
  memset(&prefs, 0, sizeof(prefs));
  prefs.enabled = true;
  prefs.normal_delay_ms = BOT_RESPONSE_DELAY_BASE_MILLIS;
  prefs.normal_jitter_ms = BOT_RESPONSE_DELAY_JITTER_MILLIS;
  prefs.hop_step_ms = BOT_HOP_STEP_MILLIS_DEFAULT;
  prefs.local_advert_interval_ms = BOT_PREFS_DEFAULT_LOCAL_ADVERT_MILLIS;
  prefs.flood_advert_interval_ms = BOT_PREFS_DEFAULT_FLOOD_ADVERT_MILLIS;
  prefs.command_mask = BOT_COMMAND_MASK_ALL;
  prefs.max_response_parts = BOT_EMERGENCY_MAX_PARTS;
  copyString(prefs.bot_channel, sizeof(prefs.bot_channel), "#bot");
  copyString(prefs.testing_channel, sizeof(prefs.testing_channel), "#test");
  copyString(prefs.emergency_channel, sizeof(prefs.emergency_channel), "#emergency");
  copyString(prefs.public_channel, sizeof(prefs.public_channel), "Public");
}

void validate(BotPrefs& prefs) {
  prefs.normal_delay_ms = prefs.normal_delay_ms > BOT_PREFS_MAX_DELAY_MILLIS ? BOT_PREFS_MAX_DELAY_MILLIS : prefs.normal_delay_ms;
  prefs.normal_jitter_ms = prefs.normal_jitter_ms > BOT_PREFS_MAX_DELAY_MILLIS ? BOT_PREFS_MAX_DELAY_MILLIS : prefs.normal_jitter_ms;
  if (prefs.hop_step_ms == 0) prefs.hop_step_ms = BOT_HOP_STEP_MILLIS_DEFAULT;
  if (prefs.hop_step_ms > BOT_HOP_STEP_MILLIS_MAX) prefs.hop_step_ms = BOT_HOP_STEP_MILLIS_MAX;
  if (prefs.local_advert_interval_ms > BOT_PREFS_MAX_ADVERT_MILLIS) prefs.local_advert_interval_ms = BOT_PREFS_MAX_ADVERT_MILLIS;
  if (prefs.flood_advert_interval_ms > BOT_PREFS_MAX_ADVERT_MILLIS) prefs.flood_advert_interval_ms = BOT_PREFS_MAX_ADVERT_MILLIS;
  prefs.command_mask &= BOT_COMMAND_MASK_ALL;
  if (prefs.max_response_parts == 0 || prefs.max_response_parts > BOT_EMERGENCY_MAX_PARTS) {
    prefs.max_response_parts = BOT_EMERGENCY_MAX_PARTS;
  }
  if (!channelHasShape(prefs.bot_channel, false)) copyString(prefs.bot_channel, sizeof(prefs.bot_channel), "#bot");
  if (!channelHasShape(prefs.testing_channel, false)) copyString(prefs.testing_channel, sizeof(prefs.testing_channel), "#test");
  if (!channelHasShape(prefs.emergency_channel, false)) copyString(prefs.emergency_channel, sizeof(prefs.emergency_channel), "#emergency");
  if (!channelHasShape(prefs.public_channel, true)) copyString(prefs.public_channel, sizeof(prefs.public_channel), "Public");
  prefs.bot_channel[BOT_MAX_CHANNEL_NAME_LEN] = 0;
  prefs.testing_channel[BOT_MAX_CHANNEL_NAME_LEN] = 0;
  prefs.emergency_channel[BOT_MAX_CHANNEL_NAME_LEN] = 0;
  prefs.public_channel[BOT_MAX_CHANNEL_NAME_LEN] = 0;
  if (!channelConfigValid(prefs)) {
    copyString(prefs.bot_channel, sizeof(prefs.bot_channel), "#bot");
    copyString(prefs.testing_channel, sizeof(prefs.testing_channel), "#test");
    copyString(prefs.emergency_channel, sizeof(prefs.emergency_channel), "#emergency");
    copyString(prefs.public_channel, sizeof(prefs.public_channel), "Public");
  }
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    prefs.known_bots[i].flags &= BOT_KNOWN_BOT_FLAG_SUPPRESS_NORMAL;
    prefs.known_bots[i].label[BOT_KNOWN_BOT_LABEL_LEN - 1] = 0;
  }
}

bool channelNameValid(const char* value, bool public_channel) {
  return channelHasShape(value, public_channel);
}

bool channelConfigValid(const BotPrefs& prefs) {
  return channelHasShape(prefs.bot_channel, false) && channelHasShape(prefs.testing_channel, false) &&
         channelHasShape(prefs.emergency_channel, false) && channelHasShape(prefs.public_channel, true) &&
         !channelNameEqual(prefs.bot_channel, prefs.testing_channel) &&
         !channelNameEqual(prefs.bot_channel, prefs.emergency_channel) &&
         !channelNameEqual(prefs.testing_channel, prefs.emergency_channel) &&
         !channelNameEqual(prefs.bot_channel, prefs.public_channel) &&
         !channelNameEqual(prefs.testing_channel, prefs.public_channel) &&
         !channelNameEqual(prefs.emergency_channel, prefs.public_channel);
}

bool serialize(const BotPrefs& prefs, uint8_t* output, size_t output_len) {
  if (!output || output_len < SERIALIZED_SIZE) return false;

  BotPrefs clean = prefs;
  validate(clean);

  memset(output, 0, output_len);
  size_t pos = 0;
  put32(output, pos, BOT_PREFS_MAGIC);
  put16(output, pos, BOT_PREFS_VERSION);
  put16(output, pos, (uint16_t)SERIALIZED_SIZE);
  size_t checksum_pos = pos;
  put32(output, pos, 0);
  put8(output, pos, clean.enabled ? 1 : 0);
  put16(output, pos, clean.normal_delay_ms);
  put16(output, pos, clean.normal_jitter_ms);
  put16(output, pos, clean.hop_step_ms);
  put32(output, pos, clean.local_advert_interval_ms);
  put32(output, pos, clean.flood_advert_interval_ms);
  put32(output, pos, clean.command_mask);
  put8(output, pos, clean.max_response_parts);
  putFixedString(output, pos, clean.bot_channel, BOT_MAX_CHANNEL_NAME_LEN + 1);
  putFixedString(output, pos, clean.testing_channel, BOT_MAX_CHANNEL_NAME_LEN + 1);
  putFixedString(output, pos, clean.emergency_channel, BOT_MAX_CHANNEL_NAME_LEN + 1);
  putFixedString(output, pos, clean.public_channel, BOT_MAX_CHANNEL_NAME_LEN + 1);
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    put8(output, pos, clean.known_bots[i].active ? 1 : 0);
    memcpy(&output[pos], clean.known_bots[i].key_prefix, BOT_SENDER_KEY_PREFIX_LEN);
    pos += BOT_SENDER_KEY_PREFIX_LEN;
    put8(output, pos, clean.known_bots[i].flags);
    putFixedString(output, pos, clean.known_bots[i].label, BOT_KNOWN_BOT_LABEL_LEN);
  }
  put32(output, pos, clean.prefs_load_failures);
  put32(output, pos, clean.prefs_save_failures);

  if (pos != SERIALIZED_SIZE) return false;
  uint32_t checksum = checksumBytes(&output[12], SERIALIZED_SIZE - 12);
  size_t write_pos = checksum_pos;
  put32(output, write_pos, checksum);
  return true;
}

bool deserialize(const uint8_t* data, size_t data_len, BotPrefs& prefs) {
  if (!data || data_len != SERIALIZED_SIZE) {
    defaults(prefs);
    return false;
  }

  size_t pos = 0;
  uint32_t magic = get32(data, pos);
  uint16_t version = get16(data, pos);
  uint16_t length = get16(data, pos);
  uint32_t checksum = get32(data, pos);
  if (magic != BOT_PREFS_MAGIC || version != BOT_PREFS_VERSION || length != SERIALIZED_SIZE) {
    defaults(prefs);
    return false;
  }
  if (checksumBytes(&data[12], SERIALIZED_SIZE - 12) != checksum) {
    defaults(prefs);
    return false;
  }

  BotPrefs loaded;
  memset(&loaded, 0, sizeof(loaded));
  loaded.enabled = get8(data, pos) != 0;
  loaded.normal_delay_ms = get16(data, pos);
  loaded.normal_jitter_ms = get16(data, pos);
  loaded.hop_step_ms = get16(data, pos);
  loaded.local_advert_interval_ms = get32(data, pos);
  loaded.flood_advert_interval_ms = get32(data, pos);
  loaded.command_mask = get32(data, pos);
  loaded.max_response_parts = get8(data, pos);
  getFixedString(data, pos, loaded.bot_channel, sizeof(loaded.bot_channel));
  getFixedString(data, pos, loaded.testing_channel, sizeof(loaded.testing_channel));
  getFixedString(data, pos, loaded.emergency_channel, sizeof(loaded.emergency_channel));
  getFixedString(data, pos, loaded.public_channel, sizeof(loaded.public_channel));
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    loaded.known_bots[i].active = get8(data, pos) != 0;
    memcpy(loaded.known_bots[i].key_prefix, &data[pos], BOT_SENDER_KEY_PREFIX_LEN);
    pos += BOT_SENDER_KEY_PREFIX_LEN;
    loaded.known_bots[i].flags = get8(data, pos);
    getFixedString(data, pos, loaded.known_bots[i].label, sizeof(loaded.known_bots[i].label));
  }
  loaded.prefs_load_failures = get32(data, pos);
  loaded.prefs_save_failures = get32(data, pos);
  if (pos != SERIALIZED_SIZE) {
    defaults(prefs);
    return false;
  }

  // Migrate legacy testing channel name.
  if (strcmp(loaded.testing_channel, "#testing") == 0) {
    copyString(loaded.testing_channel, sizeof(loaded.testing_channel), "#test");
  }

  validate(loaded);
  prefs = loaded;
  return true;
}

uint32_t commandMaskFor(BotCommandId command_id) {
  return BotCommandRegistry::commandMask(command_id);
}

bool commandEnabled(const BotPrefs& prefs, BotCommandId command_id) {
  uint32_t mask = commandMaskFor(command_id);
  return mask != 0 && (prefs.command_mask & mask) != 0;
}

void setCommandEnabled(BotPrefs& prefs, BotCommandId command_id, bool enabled) {
  uint32_t mask = commandMaskFor(command_id);
  if (mask == 0) return;
  if (enabled) {
    prefs.command_mask |= mask;
  } else {
    prefs.command_mask &= ~mask;
  }
  validate(prefs);
}

const char* commandName(BotCommandId command_id) {
  return BotCommandRegistry::commandName(command_id);
}

bool commandIdForName(const char* name, BotCommandId* command_id) {
  if (!name || !command_id) return false;
  size_t len = boundedStrLen(name, BOT_MAX_COMMAND_NAME_LEN + 1);
  const BotCommandMetadata* command = BotCommandRegistry::findByName(name, len);
  if (!command || command->mask == 0) return false;
  *command_id = command->id;
  return true;
}

bool parseKeyPrefixHex(const char* text, uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]) {
  if (!text || !key_prefix) return false;
  for (size_t i = 0; i < BOT_SENDER_KEY_PREFIX_LEN; i++) {
    int high = hexValue(text[i * 2]);
    int low = hexValue(text[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    key_prefix[i] = (uint8_t)((high << 4) | low);
  }
  return text[BOT_SENDER_KEY_PREFIX_LEN * 2] == 0;
}

void formatKeyPrefixHex(const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], char* output, size_t output_len) {
  static const char hex[] = "0123456789abcdef";
  if (!output || output_len == 0) return;
  if (!key_prefix || output_len < BOT_SENDER_KEY_PREFIX_LEN * 2 + 1) {
    output[0] = 0;
    return;
  }
  for (size_t i = 0; i < BOT_SENDER_KEY_PREFIX_LEN; i++) {
    output[i * 2] = hex[key_prefix[i] >> 4];
    output[i * 2 + 1] = hex[key_prefix[i] & 0x0F];
  }
  output[BOT_SENDER_KEY_PREFIX_LEN * 2] = 0;
}

const BotKnownBotEntry* findKnownBot(const BotPrefs& prefs, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]) {
  if (!key_prefix) return NULL;
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    if (prefs.known_bots[i].active && sameKeyPrefix(prefs.known_bots[i].key_prefix, key_prefix)) return &prefs.known_bots[i];
  }
  return NULL;
}

bool addKnownBot(BotPrefs& prefs, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], uint8_t flags,
                 const char* label) {
  if (!key_prefix) return false;
  size_t slot = BOT_KNOWN_BOT_SLOTS;
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    if (prefs.known_bots[i].active && sameKeyPrefix(prefs.known_bots[i].key_prefix, key_prefix)) {
      slot = i;
      break;
    }
    if (slot == BOT_KNOWN_BOT_SLOTS && !prefs.known_bots[i].active) slot = i;
  }
  if (slot == BOT_KNOWN_BOT_SLOTS) return false;

  prefs.known_bots[slot].active = true;
  memcpy(prefs.known_bots[slot].key_prefix, key_prefix, BOT_SENDER_KEY_PREFIX_LEN);
  prefs.known_bots[slot].flags = flags & BOT_KNOWN_BOT_FLAG_SUPPRESS_NORMAL;
  copyString(prefs.known_bots[slot].label, sizeof(prefs.known_bots[slot].label), label ? label : "bot");
  return true;
}

bool removeKnownBot(BotPrefs& prefs, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]) {
  if (!key_prefix) return false;
  for (size_t i = 0; i < BOT_KNOWN_BOT_SLOTS; i++) {
    if (prefs.known_bots[i].active && sameKeyPrefix(prefs.known_bots[i].key_prefix, key_prefix)) {
      memset(&prefs.known_bots[i], 0, sizeof(prefs.known_bots[i]));
      return true;
    }
  }
  return false;
}

}
