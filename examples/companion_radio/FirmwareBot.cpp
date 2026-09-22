#include "FirmwareBot.h"

#include "BotCommandRegistry.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

uint64_t fnv1aUpdate(uint64_t hash, uint8_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
  return hash;
}

uint64_t fnv1aUpdateBytes(uint64_t hash, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    hash = fnv1aUpdate(hash, data[i]);
  }
  return hash;
}

uint64_t fnv1aUpdateTextLower(uint64_t hash, const char* value, size_t len) {
  for (size_t i = 0; i < len; i++) {
    hash = fnv1aUpdate(hash, (uint8_t)tolower((unsigned char)value[i]));
  }
  return hash;
}

size_t boundedStrLen(const char* value, size_t max_len);

uint64_t fnv1aUpdateChannel(uint64_t hash, const BotMessage& message) {
  hash = fnv1aUpdate(hash, (uint8_t)message.channel_kind);
  const char* channel_name = message.channel_name;
  size_t channel_name_len = boundedStrLen(message.channel_name, sizeof(message.channel_name));
  if (channel_name_len > 0 && channel_name[0] == '#') {
    channel_name++;
    channel_name_len--;
  }
  return fnv1aUpdateTextLower(hash, channel_name, channel_name_len);
}

bool isSpaceByte(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool isControlByte(char ch) {
  unsigned char value = (unsigned char)ch;
  return value < 0x20 || value == 0x7F;
}

bool isCommandDelimiter(char ch) {
  return ch == ' ' || ch == ':' || ch == ',' || ch == '.' || ch == ';' || ch == '?' || ch == '!';
}

uint64_t fnv1aUpdateU32(uint64_t hash, uint32_t value) {
  hash = fnv1aUpdate(hash, (uint8_t)(value & 0xFF));
  hash = fnv1aUpdate(hash, (uint8_t)((value >> 8) & 0xFF));
  hash = fnv1aUpdate(hash, (uint8_t)((value >> 16) & 0xFF));
  hash = fnv1aUpdate(hash, (uint8_t)((value >> 24) & 0xFF));
  return hash;
}

size_t boundedStrLen(const char* value, size_t max_len) {
  size_t len = 0;
  while (len < max_len && value[len] != 0) len++;
  return len;
}

void formatTimestampHms(uint32_t timestamp, char* output, size_t output_len) {
  if (!output || output_len == 0) return;
  if (timestamp == 0) {
    snprintf(output, output_len, "Unknown");
    return;
  }
  int64_t adjusted = (int64_t)timestamp +
                     (int64_t)FirmwareBot::easternUtcOffsetSeconds(timestamp);
  uint32_t seconds = (uint32_t)((adjusted % 86400LL + 86400LL) % 86400LL);
  snprintf(output, output_len, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600UL),
           (unsigned long)((seconds / 60UL) % 60UL), (unsigned long)(seconds % 60UL));
}

}

namespace FirmwareBot {

// Return the UTC offset for U.S. Eastern Time.
//
// Since 2007:
//   DST starts: second Sunday in March at 07:00 UTC
//   DST ends:   first Sunday in November at 06:00 UTC
//
// Standard time = UTC-5
// Daylight time = UTC-4
int32_t easternUtcOffsetSeconds(uint32_t timestamp) {
  // Convert Unix time to whole days since 1970-01-01.
  int64_t days = timestamp / 86400UL;

  // Howard Hinnant civil-date conversion.
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int year = (int)yoe + (int)(era * 400);
  unsigned doy =
      doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned day = doy - (153 * mp + 2) / 5 + 1;
  unsigned month = mp + (mp < 10 ? 3 : -9);
  year += (month <= 2);

  // Return weekday for a civil date:
  // Sunday=0 ... Saturday=6.
  auto weekday = [](int y, unsigned m, unsigned d) -> unsigned {
    if (m < 3) {
      y--;
      m += 12;
    }
    int64_t k = y % 100;
    int64_t j = y / 100;
    int64_t h =
        (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    // Zeller: 0=Saturday. Convert to 0=Sunday.
    return (unsigned)((h + 6) % 7);
  };

  // Outside the transition months the answer is immediate.
  if (month < 3 || month > 11) return -5 * 3600;
  if (month > 3 && month < 11) return -4 * 3600;

  uint32_t seconds_of_day = timestamp % 86400UL;

  if (month == 3) {
    // Second Sunday in March.
    unsigned first_weekday = weekday(year, 3, 1);
    unsigned first_sunday = 1 + ((7 - first_weekday) % 7);
    unsigned second_sunday = first_sunday + 7;

    if (day < second_sunday) return -5 * 3600;
    if (day > second_sunday) return -4 * 3600;

    // 02:00 EST == 07:00 UTC.
    return seconds_of_day >= 7UL * 3600UL ? -4 * 3600 : -5 * 3600;
  }

  // First Sunday in November.
  unsigned first_weekday = weekday(year, 11, 1);
  unsigned first_sunday = 1 + ((7 - first_weekday) % 7);

  if (day < first_sunday) return -4 * 3600;
  if (day > first_sunday) return -5 * 3600;

  // 02:00 EDT == 06:00 UTC.
  return seconds_of_day >= 6UL * 3600UL ? -5 * 3600 : -4 * 3600;
}

BotWriteResult normalizeText(const char* input, size_t input_len, char* output, size_t output_len, size_t* written) {
  if (written) *written = 0;
  if (!output || output_len == 0) return BOT_WRITE_NO_SPACE;

  size_t out = 0;
  bool pending_space = false;
  bool truncated = false;

  for (size_t i = 0; i < input_len; i++) {
    char ch = input ? input[i] : 0;
    if (ch == 0) break;

    if (isSpaceByte(ch) || isControlByte(ch)) {
      pending_space = out > 0;
      continue;
    }

    if (pending_space) {
      if (out + 1 >= output_len) {
        truncated = true;
        break;
      }
      output[out++] = ' ';
      pending_space = false;
    }

    if (out + 1 >= output_len) {
      truncated = true;
      break;
    }
    output[out++] = ch;
  }

  output[out] = 0;
  if (written) *written = out;
  return truncated ? BOT_WRITE_TRUNCATED : BOT_WRITE_OK;
}

BotCommandId commandIdForName(const char* name, size_t len) {
  const BotCommandMetadata* command = BotCommandRegistry::findByName(name, len);
  return command ? command->id : BOT_COMMAND_UNKNOWN;
}

static BotCommandId cooldownKeyFor(BotCommandId command_id) {
  return command_id == BOT_COMMAND_TRACER ? BOT_COMMAND_TRACE : command_id;
}

size_t maxResponseLenForChannel(BotChannelKind channel_kind) {
  return channel_kind == BOT_CHANNEL_DM ? BOT_MAX_RESPONSE_LEN : BOT_MAX_GROUP_RESPONSE_LEN;
}

bool isCommandOnCooldown(const BotCommandCooldown* cooldowns, size_t cooldown_count, BotCommandId command_id,
                         uint32_t now_millis) {
  if (!cooldowns || command_id == BOT_COMMAND_NONE) return false;
  BotCommandId cooldown_key = cooldownKeyFor(command_id);
  for (size_t i = 0; i < cooldown_count; i++) {
    if (cooldowns[i].command_id == cooldown_key && (int32_t)(cooldowns[i].expires_at_millis - now_millis) > 0) return true;
  }
  return false;
}

void recordCommandCooldown(BotCommandCooldown* cooldowns, size_t cooldown_count, BotCommandId command_id,
                           uint32_t now_millis, uint32_t cooldown_millis) {
  if (!cooldowns || cooldown_count == 0 || command_id == BOT_COMMAND_NONE || cooldown_millis == 0) return;
  BotCommandId cooldown_key = cooldownKeyFor(command_id);

  size_t slot = cooldown_count;
  for (size_t i = 0; i < cooldown_count; i++) {
    if (cooldowns[i].command_id == cooldown_key) {
      slot = i;
      break;
    }
    if (slot == cooldown_count &&
        (cooldowns[i].command_id == BOT_COMMAND_NONE || (int32_t)(cooldowns[i].expires_at_millis - now_millis) <= 0)) {
      slot = i;
    }
  }
  if (slot == cooldown_count) slot = 0;

  cooldowns[slot].command_id = cooldown_key;
  cooldowns[slot].expires_at_millis = now_millis + cooldown_millis;
}

bool parseCommand(const char* text, size_t text_len, BotCommand* command) {
  return parseCommand(text, text_len, command, false);
}

bool parseCommand(const char* text, size_t text_len, BotCommand* command, bool allow_prefixless) {
  if (!command) return false;
  memset(command, 0, sizeof(*command));
  command->id = BOT_COMMAND_NONE;

  char normalized[BOT_MAX_TEXT_LEN + 1];
  size_t normalized_len = 0;
  normalizeText(text, text_len, normalized, sizeof(normalized), &normalized_len);

  if (normalized_len == 0) return false;

  bool has_prefix = normalized[0] == '!' || normalized[0] == '/';
  if (!has_prefix && !allow_prefixless) return false;
  if (has_prefix && normalized_len < 2) return false;

  size_t pos = has_prefix ? 1 : 0;
  while (pos < normalized_len && normalized[pos] == ' ') pos++;
  size_t name_start = pos;
  while (pos < normalized_len && !isCommandDelimiter(normalized[pos])) pos++;
  size_t name_len = pos - name_start;
  if (name_len == 0) return false;

  size_t copy_name_len = name_len;
  if (copy_name_len > BOT_MAX_COMMAND_NAME_LEN) copy_name_len = BOT_MAX_COMMAND_NAME_LEN;
  for (size_t i = 0; i < copy_name_len; i++) {
    command->name[i] = (char)tolower((unsigned char)normalized[name_start + i]);
  }
  command->name[copy_name_len] = 0;
  const BotCommandMetadata* metadata = name_len > BOT_MAX_COMMAND_NAME_LEN ? NULL : BotCommandRegistry::findByName(command->name, copy_name_len);
  command->id = metadata ? metadata->id : BOT_COMMAND_UNKNOWN;
  if (!has_prefix && (!metadata || metadata->visibility != BOT_COMMAND_VISIBILITY_DISCOVERABLE)) return false;

  while (pos < normalized_len && isCommandDelimiter(normalized[pos])) pos++;
  size_t args_len = normalized_len - pos;
  if (args_len > BOT_MAX_COMMAND_ARGS_LEN) args_len = BOT_MAX_COMMAND_ARGS_LEN;
  if (args_len > 0) memcpy(command->args, &normalized[pos], args_len);
  command->args[args_len] = 0;
  command->args_len = args_len;

  return true;
}

bool splitChannelText(const char* text, size_t text_len, char* sender, size_t sender_len, const char** body,
                      size_t* body_len) {
  if (body) *body = text;
  if (body_len) *body_len = text_len;
  if (!text) return false;

  for (size_t i = 0; i < text_len; i++) {
    if (text[i] == 0) break;
    if (text[i] == ':' && i + 1 < text_len && text[i + 1] == ' ') {
      if (sender && sender_len > 0) {
        size_t copy_len = i;
        if (copy_len >= sender_len) copy_len = sender_len - 1;
        if (copy_len > 0) memcpy(sender, text, copy_len);
        sender[copy_len] = 0;
      }
      size_t start = i + 2;
      if (body) *body = &text[start];
      if (body_len) *body_len = text_len - start;
      return true;
    }
  }
  return false;
}

BotWriteResult normalizeChannelText(const char* text, char* sender, size_t sender_len, char* output, size_t output_len,
                                    size_t* written) {
  if (sender && sender_len > 0) sender[0] = 0;
  const char* body = text;
  size_t raw_len = boundedStrLen(text, BOT_MAX_TEXT_LEN + BOT_MAX_SENDER_NAME_LEN + 3);
  size_t body_len = raw_len;
  splitChannelText(text, body_len, sender, sender_len, &body, &body_len);
  bool truncated = raw_len > BOT_MAX_TEXT_LEN || body_len > BOT_MAX_TEXT_LEN;
  if (body_len > BOT_MAX_TEXT_LEN) body_len = BOT_MAX_TEXT_LEN;
  BotWriteResult result = normalizeText(body, body_len, output, output_len, written);
  return truncated ? BOT_WRITE_TRUNCATED : result;
}

BotWriteResult writeResponse(char* output, size_t output_len, const char* text, size_t text_len, size_t* written) {
  if (written) *written = 0;
  if (!output || output_len == 0) return BOT_WRITE_NO_SPACE;

  if (!text && text_len > 0) {
    output[0] = 0;
    return BOT_WRITE_NO_SPACE;
  }

  size_t copy_len = text_len;
  if (copy_len + 1 > output_len) copy_len = output_len - 1;
  if (copy_len > 0) memcpy(output, text, copy_len);
  output[copy_len] = 0;
  if (written) *written = copy_len;

  return copy_len < text_len ? BOT_WRITE_TRUNCATED : BOT_WRITE_OK;
}

BotWriteResult writeResponseForChannel(BotChannelKind channel_kind, bool allow_prefixless, const char* text,
                                       size_t text_len, char* output, size_t output_len, size_t* written) {
  BotCommand command;
  bool needs_guard = channel_kind != BOT_CHANNEL_DM && allow_prefixless &&
                     parseCommand(text, text_len, &command, true);
  size_t max_len = maxResponseLenForChannel(channel_kind);
  if (max_len + 1 < output_len) output_len = max_len + 1;
  if (!needs_guard) return writeResponse(output, output_len, text, text_len, written);

  if (!output || output_len == 0) {
    if (written) *written = 0;
    return BOT_WRITE_NO_SPACE;
  }
  if (output_len <= BOT_GROUP_RESPONSE_GUARD_PREFIX_LEN) {
    output[0] = 0;
    if (written) *written = 0;
    return BOT_WRITE_NO_SPACE;
  }
  memcpy(output, BOT_GROUP_RESPONSE_GUARD_PREFIX, BOT_GROUP_RESPONSE_GUARD_PREFIX_LEN);
  size_t body_written = 0;
  BotWriteResult result = writeResponse(&output[BOT_GROUP_RESPONSE_GUARD_PREFIX_LEN],
                                        output_len - BOT_GROUP_RESPONSE_GUARD_PREFIX_LEN, text, text_len,
                                        &body_written);
  if (written) *written = BOT_GROUP_RESPONSE_GUARD_PREFIX_LEN + body_written;
  return result;
}

namespace {

void appendBlock(char* output, size_t output_len, size_t* pos, bool* truncated, const char* block, size_t block_len) {
  if (!output || output_len == 0 || !pos || !truncated || !block) return;
  size_t available = *pos + 1 < output_len ? output_len - 1 - *pos : 0;
  size_t copy_len = block_len < available ? block_len : available;
  if (copy_len > 0) memcpy(&output[*pos], block, copy_len);
  *pos += block_len;
  if (block_len > available) *truncated = true;
  output[*pos < output_len ? *pos : output_len - 1] = 0;
}

}

BotWriteResult writeAckResponse(const BotMessage& message, const BotCommand& command, char* output, size_t output_len,
                                size_t* written) {
  if (written) *written = 0;
  if (!output || output_len == 0) return BOT_WRITE_NO_SPACE;

  const char* sender = message.sender_name[0] ? message.sender_name : "unknown";
  int n;
  n = snprintf(output, output_len, "@[%s]", sender);
  if (n < 0) {
    output[0] = 0;
    return BOT_WRITE_NO_SPACE;
  }
  size_t pos = (size_t)n;
  bool truncated = pos >= output_len;
  if (truncated) pos = output_len - 1;

  int value = message.packet_snr_quarters;
  const char* sign = value < 0 ? "-" : "";
  if (value < 0) value = -value;
  char path_block[40];
  int path_n;
  if (message.path_hash_count == 0 || message.path_hash_size == 0) {
    path_n = snprintf(path_block, sizeof(path_block), " | 0 hops, SNR %s%d.%02d", sign, value / 4, (value % 4) * 25);
  } else {
    path_n = snprintf(path_block, sizeof(path_block), " | %u hops, %u-byte hashes, SNR %s%d.%02d",
                      (unsigned)message.path_hash_count, (unsigned)message.path_hash_size, sign, value / 4,
                      (value % 4) * 25);
  }
  if (path_n > 0) appendBlock(output, output_len, &pos, &truncated, path_block, (size_t)path_n);

  char received_at[9];
  formatTimestampHms(message.received_at_timestamp, received_at, sizeof(received_at));
  char received_block[32];
  int received_n = snprintf(received_block, sizeof(received_block), " | recv %s", received_at);
  if (received_n < 0) return BOT_WRITE_NO_SPACE;
  appendBlock(output, output_len, &pos, &truncated, received_block, (size_t)received_n);

  if (command.args_len > 0) {
    char args_block[BOT_MAX_COMMAND_ARGS_LEN + 8];
    int args_n = snprintf(args_block, sizeof(args_block), " | %s", command.args);
    if (args_n > 0) appendBlock(output, output_len, &pos, &truncated, args_block, (size_t)args_n);
  }

  size_t actual = boundedStrLen(output, output_len);
  if (written) *written = actual;
  return truncated ? BOT_WRITE_TRUNCATED : BOT_WRITE_OK;
}

BotFingerprint fingerprintFor(const BotMessage& message) {
  uint64_t hash = 1469598103934665603ULL;
  hash = fnv1aUpdateChannel(hash, message);
  hash = fnv1aUpdateBytes(hash, message.sender_key_prefix, sizeof(message.sender_key_prefix));
  hash = fnv1aUpdateTextLower(hash, message.sender_name, boundedStrLen(message.sender_name, sizeof(message.sender_name)));
  if (message.channel_kind == BOT_CHANNEL_DM) hash = fnv1aUpdateU32(hash, message.sender_timestamp);

  char normalized[BOT_MAX_TEXT_LEN + 1];
  size_t normalized_len = 0;
  normalizeText(message.text, message.text_len, normalized, sizeof(normalized), &normalized_len);
  hash = fnv1aUpdateTextLower(hash, normalized, normalized_len);

  BotFingerprint fingerprint = { hash };
  return fingerprint;
}

static int hexNibble(char c);

bool parseRequestTokenPrefix(const char* text, size_t text_len, uint16_t* token, size_t* prefix_len) {
  if (token) *token = 0;
  if (prefix_len) *prefix_len = 0;
  if (!text || text_len < 7) return false;
  if (text[0] != '[' || text[5] != ']' || text[6] != ' ') return false;
  uint16_t value = 0;
  for (int i = 0; i < 4; i++) {
    int n = hexNibble(text[1 + i]);
    if (n < 0) return false;
    value = (uint16_t)((value << 4) | (uint16_t)n);
  }
  if (token) *token = value;
  if (prefix_len) *prefix_len = 7;
  return true;
}

BotFingerprint responseFingerprintFor(const BotMessage& message, const char* response_text, size_t response_text_len) {
  uint64_t hash = 1469598103934665603ULL;
  hash = fnv1aUpdateChannel(hash, message);
  if (message.channel_kind == BOT_CHANNEL_DM) {
    hash = fnv1aUpdate(hash, message.sender_key_prefix_len);
    hash = fnv1aUpdateBytes(hash, message.sender_key_prefix, message.sender_key_prefix_len);
  }

  uint16_t token = 0;
  size_t prefix_len = 0;
  if (parseRequestTokenPrefix(response_text, response_text_len, &token, &prefix_len)) {
    response_text += prefix_len;
    response_text_len -= prefix_len;
  }

  char normalized[BOT_MAX_RESPONSE_LEN + 1];
  size_t normalized_len = 0;
  normalizeText(response_text, response_text_len, normalized, sizeof(normalized), &normalized_len);
  hash = fnv1aUpdateTextLower(hash, normalized, normalized_len);

  BotFingerprint fingerprint = { hash };
  return fingerprint;
}

uint16_t requestToken(BotFingerprint request_fingerprint) {
  // Take the low 16 bits of the request fingerprint. Two bots computing
  // fingerprintFor() against the same request derive the same token, so
  // a bot can recognise another bot's response to a request it also queued
  // even when the response text differs (different hop count, SNR, recv time).
  return (uint16_t)(request_fingerprint.value & 0xFFFFu);
}

void formatRequestToken(uint16_t token, char out[5]) {
  if (!out) return;
  static const char hex[] = "0123456789abcdef";
  out[0] = hex[(token >> 12) & 0xF];
  out[1] = hex[(token >> 8) & 0xF];
  out[2] = hex[(token >> 4) & 0xF];
  out[3] = hex[token & 0xF];
  out[4] = 0;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

BotWriteResult prependRequestToken(BotFingerprint request_fingerprint, char* text, size_t text_len, size_t buf_len,
                                   size_t* new_len) {
  if (new_len) *new_len = text_len;
  if (!text || buf_len == 0) return BOT_WRITE_NO_SPACE;
  if (text_len + 7 + 1 > buf_len) {
    // No room for "[XXXX] " prefix + null terminator without dropping content.
    return BOT_WRITE_NO_SPACE;
  }
  char hex[5];
  formatRequestToken(requestToken(request_fingerprint), hex);
  memmove(text + 7, text, text_len);
  text[0] = '[';
  text[1] = hex[0];
  text[2] = hex[1];
  text[3] = hex[2];
  text[4] = hex[3];
  text[5] = ']';
  text[6] = ' ';
  size_t total = text_len + 7;
  if (total < buf_len) text[total] = 0;
  if (new_len) *new_len = total;
  return BOT_WRITE_OK;
}

}
