#include "BotCommands.h"

#include "BotCommandRegistry.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

const uint16_t kRollMax = 10000;
const uint8_t kMaxDiceTerms = 10;

struct ParsedPathArg {
  uint8_t bytes[BOT_MAX_PATH_BYTES];
  uint8_t byte_len;
  uint8_t hash_size;
  uint8_t hash_count;
};

struct DiceTerm {
  uint8_t count;
  uint16_t sides;
};

struct ParsedDice {
  DiceTerm terms[kMaxDiceTerms];
  uint8_t term_count;
  uint8_t total_count;
  bool decade;
};

BotCommandResult makeResult(BotCommandResultCode code, size_t text_len) {
  BotCommandResult result = { code, text_len };
  return result;
}

size_t boundedStrLen(const char* value, size_t max_len) {
  size_t len = 0;
  while (value && len < max_len && value[len] != 0) len++;
  return len;
}

BotCommandResult writeText(char* output, size_t output_len, const char* text) {
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);

  size_t text_len = boundedStrLen(text, BOT_MAX_RESPONSE_LEN + 1);
  size_t copy_len = text_len;
  if (copy_len + 1 > output_len) copy_len = output_len - 1;
  if (copy_len > 0) memcpy(output, text, copy_len);
  output[copy_len] = 0;

  return makeResult(copy_len < text_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, copy_len);
}

BotCommandResult writeFormatted(char* output, size_t output_len, const char* format, ...) {
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);

  va_list args;
  va_start(args, format);
  int n = vsnprintf(output, output_len, format, args);
  va_end(args);

  if (n < 0) {
    output[0] = 0;
    return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  }

  size_t written = (size_t)n;
  if (written >= output_len) written = output_len - 1;
  return makeResult((size_t)n >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, written);
}

void appendText(char* output, size_t output_len, size_t* pos, const char* text) {
  if (!output || output_len == 0 || !pos || !text) return;
  for (size_t i = 0; text[i] != 0; i++) {
    if (*pos + 1 < output_len) output[*pos] = text[i];
    (*pos)++;
  }
  output[*pos < output_len ? *pos : output_len - 1] = 0;
}

void appendFormatted(char* output, size_t output_len, size_t* pos, const char* format, ...) {
  char temp[32];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(temp, sizeof(temp), format, args);
  va_end(args);
  if (n < 0) return;
  appendText(output, output_len, pos, temp);
}

BotCommandResult resultForAppend(char* output, size_t output_len, size_t pos) {
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  size_t actual = boundedStrLen(output, output_len);
  return makeResult(pos >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}

bool parseUInt(const char* text, size_t len, size_t* pos, uint16_t max_value, uint16_t* value) {
  uint32_t parsed = 0;
  size_t start = *pos;
  while (*pos < len && isdigit((unsigned char)text[*pos])) {
    parsed = parsed * 10 + (uint32_t)(text[*pos] - '0');
    if (parsed > max_value) return false;
    (*pos)++;
  }
  if (*pos == start) return false;
  *value = (uint16_t)parsed;
  return true;
}

bool textEqualsIgnoreCase(const char* text, size_t len, const char* expected) {
  size_t expected_len = boundedStrLen(expected, BOT_MAX_COMMAND_ARGS_LEN + 1);
  if (len != expected_len) return false;
  for (size_t i = 0; i < len; i++) {
    if (tolower((unsigned char)text[i]) != tolower((unsigned char)expected[i])) return false;
  }
  return true;
}

bool isSupportedDiceSides(uint16_t sides) {
  return sides == 4 || sides == 6 || sides == 8 || sides == 10 || sides == 12 || sides == 20 || sides == 100;
}

void skipSpaces(const char* text, size_t len, size_t* pos) {
  while (*pos < len && text[*pos] == ' ') (*pos)++;
}

bool parseRoll(const BotCommand& command, uint16_t* low, uint16_t* high) {
  *low = 1;
  *high = 100;
  if (command.args_len == 0) return true;

  const char* text = command.args;
  size_t len = command.args_len;
  size_t pos = 0;
  uint16_t first = 0;
  if (!parseUInt(text, len, &pos, kRollMax, &first) || first == 0) return false;

  skipSpaces(text, len, &pos);
  if (pos == len) {
    *high = first;
    return true;
  }

  uint16_t second = 0;
  if (!parseUInt(text, len, &pos, kRollMax, &second) || second == 0) return false;
  skipSpaces(text, len, &pos);
  if (pos != len || first > second) return false;

  *low = first;
  *high = second;
  return true;
}

bool parseDiceTerm(const char* text, size_t len, size_t* pos, DiceTerm* term) {
  uint16_t first = 0;
  term->count = 1;
  term->sides = 6;

  if (*pos >= len) return false;
  if (text[*pos] == 'd' || text[*pos] == 'D') {
    (*pos)++;
    if (!parseUInt(text, len, pos, 1000, &term->sides)) return false;
  } else {
    if (!parseUInt(text, len, pos, kRollMax, &first)) return false;
    if (*pos < len && (text[*pos] == 'd' || text[*pos] == 'D')) {
      if (first == 0 || first > 10) return false;
      term->count = (uint8_t)first;
      (*pos)++;
      if (!parseUInt(text, len, pos, 1000, &term->sides)) return false;
    } else {
      term->sides = first;
    }
  }

  return term->count >= 1 && term->count <= 10 && isSupportedDiceSides(term->sides);
}

bool parseDice(const BotCommand& command, ParsedDice* parsed) {
  memset(parsed, 0, sizeof(*parsed));
  if (command.args_len == 0) {
    parsed->terms[0].count = 1;
    parsed->terms[0].sides = 6;
    parsed->term_count = 1;
    parsed->total_count = 1;
    return true;
  }

  const char* text = command.args;
  size_t len = command.args_len;
  if (textEqualsIgnoreCase(text, len, "decade")) {
    parsed->decade = true;
    return true;
  }

  size_t pos = 0;
  while (pos < len) {
    skipSpaces(text, len, &pos);
    if (pos >= len) break;
    if (parsed->term_count >= kMaxDiceTerms) return false;
    DiceTerm* term = &parsed->terms[parsed->term_count];
    if (!parseDiceTerm(text, len, &pos, term)) return false;
    parsed->total_count = (uint8_t)(parsed->total_count + term->count);
    if (parsed->total_count > 10) return false;
    parsed->term_count++;
    if (pos < len && text[pos] != ' ') return false;
  }

  return parsed->term_count > 0;
}

uint16_t rollOnce(uint32_t* state, uint16_t sides) {
  *state = (*state * 1664525UL) + 1013904223UL;
  return (uint16_t)((*state >> 16) % sides) + 1;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHexByte(const char* text, uint8_t* value) {
  int hi = hexValue(text[0]);
  int lo = hexValue(text[1]);
  if (hi < 0 || lo < 0) return false;
  *value = (uint8_t)((hi << 4) | lo);
  return true;
}

bool parsePathArgument(const char* text, size_t len, uint8_t configured_hash_size, ParsedPathArg* parsed) {
  memset(parsed, 0, sizeof(*parsed));
  if (!text || len == 0) return false;
  if (configured_hash_size != 0 && configured_hash_size != 1 && configured_hash_size != 2 && configured_hash_size != 4) return false;

  bool has_comma = false;
  for (size_t i = 0; i < len; i++) {
    if (text[i] == ',') has_comma = true;
  }

  if (has_comma) {
    size_t pos = 0;
    size_t chunk_hex_len = 0;
    while (pos < len) {
      size_t start = pos;
      while (pos < len && text[pos] != ',') {
        if (hexValue(text[pos]) < 0) return false;
        pos++;
      }
      size_t token_len = pos - start;
      if (token_len != 2 && token_len != 4 && token_len != 8) return false;
      if (configured_hash_size != 0 && token_len != (size_t)configured_hash_size * 2) return false;
      if (chunk_hex_len == 0) chunk_hex_len = token_len;
      if (token_len != chunk_hex_len) return false;
      if ((size_t)parsed->byte_len + (token_len / 2) > sizeof(parsed->bytes)) return false;
      for (size_t i = 0; i < token_len; i += 2) {
        if (!parseHexByte(&text[start + i], &parsed->bytes[parsed->byte_len])) return false;
        parsed->byte_len++;
      }
      parsed->hash_count++;
      if (pos < len) {
        pos++;
        if (pos == len) return false;
      }
    }
    parsed->hash_size = (uint8_t)(chunk_hex_len / 2);
    return parsed->hash_size != 0 && parsed->hash_count != 0;
  }

  if ((len % 2) != 0 || len / 2 > sizeof(parsed->bytes)) return false;
  for (size_t i = 0; i < len; i++) {
    if (hexValue(text[i]) < 0) return false;
  }
  parsed->byte_len = (uint8_t)(len / 2);
  if (parsed->byte_len == 0) return false;
  for (size_t i = 0; i < parsed->byte_len; i++) {
    if (!parseHexByte(&text[i * 2], &parsed->bytes[i])) return false;
  }
  parsed->hash_size = configured_hash_size != 0 ? configured_hash_size : ((parsed->byte_len % 2) == 0 ? 2 : 1);
  if ((parsed->byte_len % parsed->hash_size) != 0) return false;
  parsed->hash_count = (uint8_t)(parsed->byte_len / parsed->hash_size);
  return parsed->hash_count != 0;
}

void appendPathHex(char* output, size_t output_len, size_t* pos, const uint8_t* path, size_t path_len) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < path_len; i++) {
    if (*pos + 2 < output_len) {
      output[*pos] = hex[path[i] >> 4];
      output[*pos + 1] = hex[path[i] & 0x0F];
    }
    *pos += 2;
  }
  if (output_len > 0) output[*pos < output_len ? *pos : output_len - 1] = 0;
}

void appendPathHops(char* output, size_t output_len, size_t* pos, const uint8_t* path, uint8_t hash_size,
                    uint8_t hash_count) {
  for (uint8_t hop = 0; hop < hash_count; hop++) {
    if (hop != 0) appendText(output, output_len, pos, " -> ");
    appendPathHex(output, output_len, pos, &path[(size_t)hop * hash_size], hash_size);
  }
}

void formatQuarters(int8_t quarters, char* output, size_t output_len) {
  if (!output || output_len == 0) return;
  int value = quarters;
  const char* sign = value < 0 ? "-" : "";
  if (value < 0) value = -value;
  snprintf(output, output_len, "%s%d.%02d", sign, value / 4, (value % 4) * 25);
}

bool commandInClasses(const BotCommandMetadata* command, const BotCommandContextClass* classes, size_t class_count) {
  for (size_t i = 0; i < class_count; i++) {
    if (command->context_class == classes[i]) return true;
  }
  return false;
}

// Listings are derived from the registry so an added command can never
// be missed; classes split the list because all commands do not fit one
// group-channel message.
void appendCommandsByClass(char* output, size_t output_len, size_t* pos, const BotCommandContextClass* classes,
                           size_t class_count) {
  bool first = true;
  for (size_t i = 0; i < BotCommandRegistry::commandCount(); i++) {
    const BotCommandMetadata* command = BotCommandRegistry::commandAt(i);
    if (!command || command->visibility != BOT_COMMAND_VISIBILITY_DISCOVERABLE) continue;
    if (!commandInClasses(command, classes, class_count)) continue;
    if (!first) appendText(output, output_len, pos, " ");
    appendText(output, output_len, pos, command->name);
    first = false;
  }
}

const BotCommandContextClass kChatClasses[] = { BOT_COMMAND_CONTEXT_NORMAL, BOT_COMMAND_CONTEXT_TRACE };
const BotCommandContextClass kDiagClasses[] = { BOT_COMMAND_CONTEXT_DIAGNOSTIC, BOT_COMMAND_CONTEXT_LOCAL_CONTACT };

BotCommandResult executeCmd(const BotCommand& command, char* output, size_t output_len) {
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  output[0] = 0;
  size_t pos = 0;
  if (textEqualsIgnoreCase(command.args, command.args_len, "diag")) {
    appendText(output, output_len, &pos, "Diag: ");
    appendCommandsByClass(output, output_len, &pos, kDiagClasses, 2);
    return resultForAppend(output, output_len, pos);
  }
  appendText(output, output_len, &pos, "Commands: ");
  appendCommandsByClass(output, output_len, &pos, kChatClasses, 2);
  appendText(output, output_len, &pos, " | cmd diag");
  return resultForAppend(output, output_len, pos);
}

BotCommandResult executeHelp(const BotCommand& command, char* output, size_t output_len) {
  if (command.args_len == 0) {
    if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
    output[0] = 0;
    size_t pos = 0;
    appendText(output, output_len, &pos, "Commands: ");
    appendCommandsByClass(output, output_len, &pos, kChatClasses, 2);
    appendText(output, output_len, &pos, " | cmd diag | help <command>");
    return resultForAppend(output, output_len, pos);
  }

  const BotCommandMetadata* metadata = BotCommandRegistry::findByName(command.args, command.args_len);
  if (!metadata) return writeFormatted(output, output_len, "No help for %s", command.args);
  if (metadata->visibility != BOT_COMMAND_VISIBILITY_DISCOVERABLE) {
    return writeFormatted(output, output_len, "%s is not available", command.args);
  }
  return writeFormatted(output, output_len, "%s: %s. Usage: %s", metadata->name, metadata->details, metadata->usage);
}

BotCommandResult executeMagic8(const BotCommandContext& context, char* output, size_t output_len) {
  static const char* responses[] = {
    "It is certain", "Looks good", "Ask again later", "Cannot predict now", "Doubtful", "Very likely",
    "Signs point yes", "No", "Reply hazy", "Absolutely"
  };
  uint32_t seed = context.random_seed ? context.random_seed : 1;
  seed = seed * 1664525UL + 1013904223UL;
  return writeFormatted(output, output_len, "Magic 8-ball: %s", responses[(seed >> 16) % (sizeof(responses) / sizeof(responses[0]))]);
}

BotCommandResult executePathLike(const BotCommandContext& context, char* output, size_t output_len, const char* label) {
  if (context.path_hash_size == 0) return writeFormatted(output, output_len, "%s unavailable", label);
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);

  char snr[8];
  formatQuarters(context.path_snr_quarters, snr, sizeof(snr));
  const char* target = context.response_target[0] ? context.response_target : NULL;
  if (context.path_hash_count == 0 || context.path_len == 0) {
    if (target) return writeFormatted(output, output_len, "%s @[%s] direct zero-hop, SNR %s", label, target, snr);
    return writeFormatted(output, output_len, "%s direct zero-hop, SNR %s", label, snr);
  }
  if (!context.path) return writeFormatted(output, output_len, "%s unavailable", label);

  int written = target
                    ? snprintf(output, output_len, "%s @[%s] %u hops, %u-byte hashes, SNR %s | ", label, target,
                               (unsigned)context.path_hash_count, (unsigned)context.path_hash_size, snr)
                    : snprintf(output, output_len, "%s %u hops, %u-byte hashes, SNR %s | ", label,
                               (unsigned)context.path_hash_count, (unsigned)context.path_hash_size, snr);
  if (written < 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  size_t pos = (size_t)written;
  appendPathHops(output, output_len, &pos, context.path, context.path_hash_size, context.path_hash_count);
  size_t actual = boundedStrLen(output, output_len);
  return makeResult(pos >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}

BotCommandResult executePathArg(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  ParsedPathArg path;
  if (!parsePathArgument(command.args, command.args_len, context.path_hash_size, &path)) return writeText(output, output_len, "Usage: path [path]");
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  int written = snprintf(output, output_len, "Path %u hops, %u-byte hashes | ", (unsigned)path.hash_count, (unsigned)path.hash_size);
  if (written < 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  size_t pos = (size_t)written;
  appendPathHops(output, output_len, &pos, path.bytes, path.hash_size, path.hash_count);
  size_t actual = boundedStrLen(output, output_len);
  return makeResult(pos >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}

BotCommandResult executePath(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  if (command.args_len != 0) return executePathArg(command, context, output, output_len);
  return executePathLike(context, output, output_len, "Path");
}

BotCommandResult executeTraceLike(const BotCommand& command, const BotCommandContext& context, const char* label,
                                  const char* usage, char* output, size_t output_len) {
  if (command.args_len == 0) {
    if (context.path_hash_size == 0) {
      return writeFormatted(output, output_len, "%s needs a known route. Try path first.", label);
    }
    if (context.path_hash_count == 0 || context.path_len == 0) return writeFormatted(output, output_len, "%s ready: direct zero-hop route", label);
    if (!context.path) return writeFormatted(output, output_len, "%s needs a known route. Try path first.", label);
    return writeFormatted(output, output_len, "%s ready: %u-hop route", label, (unsigned)context.path_hash_count);
  }
  ParsedPathArg path;
  if (!parsePathArgument(command.args, command.args_len, context.path_hash_size, &path)) {
    return writeFormatted(output, output_len, "Usage: %s [path]", usage);
  }
  return writeFormatted(output, output_len, "%s ready: %u-hop route", label, (unsigned)path.hash_count);
}

BotCommandResult executeRoll(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  uint16_t low = 1;
  uint16_t high = 100;
  if (!parseRoll(command, &low, &high)) {
    return writeText(output, output_len, "Invalid number. Use roll, roll X, or roll low high (max 10000)");
  }

  uint32_t state = context.random_seed ^ ((uint32_t)low << 16) ^ high;
  if (state == 0) state = 1;
  uint16_t span = (uint16_t)(high - low + 1);
  uint16_t value = (uint16_t)(low + rollOnce(&state, span) - 1);
  return writeFormatted(output, output_len, "Roll %u-%u: %u", (unsigned)low, (unsigned)high, (unsigned)value);
}

void appendDiceRolls(char* output, size_t output_len, size_t* pos, uint32_t* state, const DiceTerm& term, uint16_t* total) {
  if (term.count == 1) {
    uint16_t roll = rollOnce(state, term.sides);
    *total = (uint16_t)(*total + roll);
    appendFormatted(output, output_len, pos, "d%u: %u", (unsigned)term.sides, (unsigned)roll);
    return;
  }

  appendFormatted(output, output_len, pos, "%ud%u: [", (unsigned)term.count, (unsigned)term.sides);
  for (uint8_t i = 0; i < term.count; i++) {
    uint16_t roll = rollOnce(state, term.sides);
    *total = (uint16_t)(*total + roll);
    if (i != 0) appendText(output, output_len, pos, ", ");
    appendFormatted(output, output_len, pos, "%u", (unsigned)roll);
  }
  appendText(output, output_len, pos, "]");
}

BotCommandResult executeDice(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  ParsedDice parsed;
  if (!parseDice(command, &parsed)) {
    return writeText(output, output_len, "Invalid dice type. Use dice, dice d20, dice 2d6, or dice decade");
  }

  uint32_t state = context.random_seed ^ 0x9E3779B9UL;
  if (state == 0) state = 1;

  if (parsed.decade) {
    uint16_t value = (uint16_t)(rollOnce(&state, 10) * 10);
    return writeFormatted(output, output_len, "Dice decade: %u", (unsigned)value);
  }

  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  output[0] = 0;
  size_t pos = 0;
  uint16_t total = 0;
  appendText(output, output_len, &pos, "Dice ");

  if (parsed.term_count == 1 && parsed.terms[0].count == 1) {
    uint16_t roll = rollOnce(&state, parsed.terms[0].sides);
    return writeFormatted(output, output_len, "Dice d%u: %u", (unsigned)parsed.terms[0].sides, (unsigned)roll);
  }

  if (parsed.term_count == 1) {
    appendDiceRolls(output, output_len, &pos, &state, parsed.terms[0], &total);
    appendFormatted(output, output_len, &pos, " = %u", (unsigned)total);
    return resultForAppend(output, output_len, pos);
  }

  for (uint8_t i = 0; i < parsed.term_count; i++) {
    if (i != 0) appendText(output, output_len, &pos, " + ");
    appendDiceRolls(output, output_len, &pos, &state, parsed.terms[i], &total);
  }
  appendFormatted(output, output_len, &pos, " | Total: %u", (unsigned)total);
  return resultForAppend(output, output_len, pos);
}

void formatSecondsHms(uint32_t timestamp, char* output, size_t output_len) {
  if (!output || output_len == 0) return;
  if (timestamp == 0) {
    snprintf(output, output_len, "Unknown");
    return;
  }
  uint32_t seconds = timestamp % 86400UL;
  snprintf(output, output_len, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600UL),
           (unsigned long)((seconds / 60UL) % 60UL), (unsigned long)(seconds % 60UL));
}

void formatUptime(uint32_t seconds, char* output, size_t output_len) {
  if (!output || output_len == 0) return;
  uint32_t days = seconds / 86400UL;
  uint32_t hours = (seconds / 3600UL) % 24UL;
  uint32_t mins = (seconds / 60UL) % 60UL;
  snprintf(output, output_len, "%lud %luh %lum", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
}

uint8_t batteryPercentFromMillivolts(uint16_t mv) {
  if (mv == 0) return 0;
  const uint16_t kEmpty = 3300;
  const uint16_t kFull = 4200;
  if (mv <= kEmpty) return 0;
  if (mv >= kFull) return 100;
  return (uint8_t)(((uint32_t)(mv - kEmpty) * 100UL) / (uint32_t)(kFull - kEmpty));
}

BotCommandResult executeSig(const BotCommandContext& context, char* output, size_t output_len) {
  char snr[8];
  formatQuarters(context.path_snr_quarters, snr, sizeof(snr));
  const char* target = context.response_target[0] ? context.response_target : NULL;
  if (target) {
    return writeFormatted(output, output_len, "Sig @[%s]: heard you at SNR %s | last RSSI %d dBm, noise %d dBm", target,
                          snr, (int)context.last_rssi, (int)context.noise_floor);
  }
  return writeFormatted(output, output_len, "Sig: heard you at SNR %s | last RSSI %d dBm, noise %d dBm", snr,
                        (int)context.last_rssi, (int)context.noise_floor);
}

BotCommandResult executeAir(const BotCommandContext& context, char* output, size_t output_len) {
  return writeFormatted(output, output_len, "Air: tx %lus rx %lus | rx flood %lu direct %lu | tx flood %lu direct %lu",
                        (unsigned long)context.tx_airtime_seconds, (unsigned long)context.rx_airtime_seconds,
                        (unsigned long)context.flood_recv, (unsigned long)context.direct_recv,
                        (unsigned long)context.flood_sent, (unsigned long)context.direct_sent);
}

BotCommandResult executeCoin(const BotCommandContext& context, char* output, size_t output_len) {
  uint32_t state = context.random_seed ^ 0x636F696EUL;
  if (state == 0) state = 1;
  return writeFormatted(output, output_len, "Coin: %s", rollOnce(&state, 2) == 1 ? "Heads" : "Tails");
}

BotCommandResult executeTest(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  const char* name = context.node_name[0] ? context.node_name : "local";
  char received_at[9];
  formatSecondsHms(context.uptime_seconds, received_at, sizeof(received_at));
  if (command.args_len == 0) {
    return writeFormatted(output, output_len, "@[%s] local | recv %s", name, received_at);
  }
  return writeFormatted(output, output_len, "@[%s] local | recv %s | %s", name, received_at, command.args);
}

}

namespace BotCommands {

size_t formatTraceResult(char* output, size_t output_len, const char* target, uint32_t tag, uint8_t hash_size,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t hop_count,
                         int8_t tail_snr_quarters) {
  if (!output || output_len == 0) return 0;
  char tail_snr[8];
  formatQuarters(tail_snr_quarters, tail_snr, sizeof(tail_snr));
  bool has_target = target && target[0];
  int written;
  if (hop_count == 0 || hash_size == 0) {
    written = has_target
                  ? snprintf(output, output_len, "Trace @[%s] %08lx direct zero-hop tail %s", target,
                             (unsigned long)tag, tail_snr)
                  : snprintf(output, output_len, "Trace %08lx direct zero-hop tail %s", (unsigned long)tag, tail_snr);
    if (written < 0) {
      output[0] = 0;
      return 0;
    }
    return boundedStrLen(output, output_len);
  }
  written = has_target
                ? snprintf(output, output_len, "Trace @[%s] %08lx %uh tail %s | ", target, (unsigned long)tag,
                           (unsigned)hop_count, tail_snr)
                : snprintf(output, output_len, "Trace %08lx %uh tail %s | ", (unsigned long)tag,
                           (unsigned)hop_count, tail_snr);
  if (written < 0) {
    output[0] = 0;
    return 0;
  }
  size_t pos = (size_t)written;
  for (uint8_t hop = 0; hop < hop_count; hop++) {
    if (hop != 0) appendText(output, output_len, &pos, " -> ");
    appendPathHex(output, output_len, &pos, &path_hashes[(size_t)hop * hash_size], hash_size);
    appendText(output, output_len, &pos, "@");
    char hop_snr[8];
    formatQuarters(path_snrs ? (int8_t)path_snrs[hop] : 0, hop_snr, sizeof(hop_snr));
    appendText(output, output_len, &pos, hop_snr);
  }
  return boundedStrLen(output, output_len);
}

BotCommandResult executeCommand(const BotCommand& command, const BotCommandContext& context, char* output,
                                size_t output_len) {
  switch (command.id) {
    case BOT_COMMAND_HELP:
      return executeHelp(command, output, output_len);
    case BOT_COMMAND_CMD:
      return executeCmd(command, output, output_len);
    case BOT_COMMAND_PING:
      return writeText(output, output_len, "Pong!");
    case BOT_COMMAND_TEST:
      return executeTest(command, context, output, output_len);
    case BOT_COMMAND_HELLO: {
      const char* node = context.node_name[0] ? context.node_name : "MeshCore bot";
      const char* target = context.response_target[0] ? context.response_target : NULL;
      if (target) return writeFormatted(output, output_len, "Hello @[%s], from %s", target, node);
      return writeFormatted(output, output_len, "Hello from %s", node);
    }
    case BOT_COMMAND_ABOUT:
      return writeText(output, output_len, "Colorado Mesh firmware bot: local commands only, no internet required.");
    case BOT_COMMAND_ROLL:
      return executeRoll(command, context, output, output_len);
    case BOT_COMMAND_DICE:
      return executeDice(command, context, output, output_len);
    case BOT_COMMAND_STATUS: {
      char up_str[20];
      formatUptime(context.uptime_seconds, up_str, sizeof(up_str));
      const char* name = context.node_name[0] ? context.node_name : "bot";
      if (context.battery_millivolts > 0) {
        uint8_t pct = batteryPercentFromMillivolts(context.battery_millivolts);
        return writeFormatted(output, output_len, "%s | up %s | batt %umV %u%% | storage %lu/%luKB | seen %lu sent %lu fail %lu",
                              name, up_str, (unsigned)context.battery_millivolts, (unsigned)pct,
                              (unsigned long)context.storage_used_kb, (unsigned long)context.storage_total_kb,
                              (unsigned long)context.observed_messages, (unsigned long)context.sent_messages,
                              (unsigned long)context.send_failures);
      }
      return writeFormatted(output, output_len, "%s | up %s | storage %lu/%luKB | seen %lu sent %lu fail %lu",
                            name, up_str, (unsigned long)context.storage_used_kb,
                            (unsigned long)context.storage_total_kb, (unsigned long)context.observed_messages,
                            (unsigned long)context.sent_messages, (unsigned long)context.send_failures);
    }
    case BOT_COMMAND_CHANNELS:
      return writeFormatted(output, output_len, "Channels: bot %s | testing %s | emergency %s | public %s (%u total)",
                            context.bot_channel[0] ? context.bot_channel : "#bot",
                            context.testing_channel[0] ? context.testing_channel : "#test",
                            context.emergency_channel[0] ? context.emergency_channel : "#emergency",
                            context.public_channel[0] ? context.public_channel : "Public", (unsigned)context.channel_count);
    case BOT_COMMAND_VERSION:
      return writeFormatted(output, output_len, "Firmware %s built %s", context.firmware_version[0] ? context.firmware_version : "unknown",
                            context.firmware_build_date[0] ? context.firmware_build_date : "unknown");
    case BOT_COMMAND_STATS:
      return writeFormatted(output, output_len, "Bot: %lu seen, %lu ok, %lu sent, %lu fail | RF: %lu rx, %lu tx, %lu err, q %u",
                            (unsigned long)context.observed_messages, (unsigned long)context.eligible_messages,
                            (unsigned long)context.sent_messages, (unsigned long)context.send_failures,
                            (unsigned long)context.packets_recv, (unsigned long)context.packets_sent,
                            (unsigned long)context.packets_recv_errors, (unsigned)context.queue_depth);
    case BOT_COMMAND_MAGIC8:
      return executeMagic8(context, output, output_len);
    case BOT_COMMAND_SIG:
      return executeSig(context, output, output_len);
    case BOT_COMMAND_AIR:
      return executeAir(context, output, output_len);
    case BOT_COMMAND_COIN:
      return executeCoin(context, output, output_len);
    case BOT_COMMAND_PATH:
      return executePath(command, context, output, output_len);
    case BOT_COMMAND_TRACE:
      return executeTraceLike(command, context, "Trace", "trace", output, output_len);
    case BOT_COMMAND_TRACER:
      return executeTraceLike(command, context, "Tracer", "tracer", output, output_len);
    case BOT_COMMAND_PREFIX:
      return writeText(output, output_len, "Prefix lookup requires local contacts");
    case BOT_COMMAND_UNSUPPORTED:
      return writeText(output, output_len, "Unknown command. Try help");
    case BOT_COMMAND_UNKNOWN:
      return writeText(output, output_len, "Unknown command. Try help");
    default:
      return makeResult(BOT_COMMAND_RESULT_NOT_HANDLED, 0);
  }
}

}
