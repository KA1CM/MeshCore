#include "BotCommands.h"

#include "BotCommandRegistry.h"
#include "FirmwareBot.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {


struct ParsedPathArg {
  uint8_t bytes[BOT_MAX_PATH_BYTES];
  uint8_t byte_len;
  uint8_t hash_size;
  uint8_t hash_count;
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


BotCommandResult resultForAppend(char* output, size_t output_len, size_t pos) {
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);
  size_t actual = boundedStrLen(output, output_len);
  return makeResult(pos >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}


bool textEqualsIgnoreCase(const char* text, size_t len, const char* expected) {
  size_t expected_len = boundedStrLen(expected, BOT_MAX_COMMAND_ARGS_LEN + 1);
  if (len != expected_len) return false;
  for (size_t i = 0; i < len; i++) {
    if (tolower((unsigned char)text[i]) != tolower((unsigned char)expected[i])) return false;
  }
  return true;
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
    if (hop != 0) appendText(output, output_len, pos, ", ");
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

BotCommandResult executeCmd(const BotCommand& command, char* output, size_t output_len) {
  return writeText(output, output_len,
                   "help  cmd  hello  about  version  time  status  stats  air  ping  test  path  snr  channels  lora  id  neighbors  list");
}

BotCommandResult executeHelp(const BotCommand& command, char* output, size_t output_len) {
  if (command.args_len == 0) {
    return writeText(output, output_len,
                     "help  cmd  hello  about  version  time  status  stats  air  ping  test  path  snr  channels  lora  id  neighbors  list");
  }

  if (command.args_len == 4 && !strcmp(command.args,"list"))
    return writeText(output,output_len,
      "list: list of all managed repeaters with last saved voltage\n"
      "list low: list of repeaters with voltage below 3.6V or N/A");
  if (command.args_len == 8 && !strcmp(command.args,"list low"))
    return writeText(output,output_len,"list low: list of repeaters with voltage below 3.6V or N/A");
  const BotCommandMetadata* metadata = BotCommandRegistry::findByName(command.args, command.args_len);
  if (!metadata) return writeFormatted(output, output_len, "No help for %s", command.args);
  if (metadata->visibility != BOT_COMMAND_VISIBILITY_DISCOVERABLE) {
    return writeFormatted(output, output_len, "%s is not available", command.args);
  }
  if (metadata->id == BOT_COMMAND_NEIGHBORS) {
    return writeText(output, output_len,
      "Neighbors: list of directly heard repeaters in one page\n"
      "Neighbors all: list of all directly heard repeaters");
  }
  if (metadata->id == BOT_COMMAND_SIG) {
    return writeFormatted(output, output_len, "%s Usage: %s", metadata->details, metadata->usage);
  }
  return writeFormatted(output, output_len, "%s: %s. Usage: %s",
                        metadata->name, metadata->details, metadata->usage);
}

BotCommandResult executePathLike(const BotCommandContext& context, char* output, size_t output_len, const char* label) {
  if (context.path_hash_size == 0) return writeFormatted(output, output_len, "%s unavailable", label);
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);

  const char* target = context.response_target[0] ? context.response_target : NULL;
  if (context.path_hash_count == 0 || context.path_len == 0) {
    if (target) return writeFormatted(output, output_len, "@[%s] Direct", target);
    return writeText(output, output_len, "Direct");
  }
  if (!context.path) return writeFormatted(output, output_len, "%s unavailable", label);

  output[0] = 0;
  size_t pos = 0;
  if (target) {
    appendText(output, output_len, &pos, "@[");
    appendText(output, output_len, &pos, target);
    appendText(output, output_len, &pos, "] ");
  }
  appendPathHops(output, output_len, &pos, context.path, context.path_hash_size, context.path_hash_count);
  size_t actual = boundedStrLen(output, output_len);
  return makeResult(pos >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}

BotCommandResult executePathArg(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  ParsedPathArg path;
  if (!parsePathArgument(command.args, command.args_len, context.path_hash_size, &path)) return writeText(output, output_len, "Usage: path [path]");
  if (!output || output_len == 0) return makeResult(BOT_COMMAND_RESULT_NO_SPACE, 0);

  output[0] = 0;
  size_t pos = 0;
  appendPathHops(output, output_len, &pos, path.bytes, path.hash_size, path.hash_count);
  size_t actual = boundedStrLen(output, output_len);
  return makeResult(pos >= output_len ? BOT_COMMAND_RESULT_TRUNCATED : BOT_COMMAND_RESULT_OK, actual);
}

BotCommandResult executePath(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  if (command.args_len != 0) return executePathArg(command, context, output, output_len);
  return executePathLike(context, output, output_len, "Path");
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
  const char* repeater = context.last_repeater_name;
  const char* from = repeater && repeater[0] ? " from " : "";
  if (!repeater) repeater = "";
  unsigned hops = (unsigned)context.path_hash_count;

  if (target) {
    if (hops == 0) {
      return writeFormatted(output, output_len,
                            "@[%s]\nDirect\nSNR %s | RSSI %d dBm | noise %d dBm",
                            target, snr,
                            (int)context.last_rssi, (int)context.noise_floor);
    }

    return writeFormatted(output, output_len,
                          "@[%s]\n%u %s\nLast hop%s%s SNR %s | RSSI %d dBm | noise %d dBm",
                          target, hops, hops == 1 ? "hop" : "hops", from, repeater, snr,
                          (int)context.last_rssi, (int)context.noise_floor);
  }

  if (hops == 0) {
    return writeFormatted(output, output_len,
                          "Direct\nSNR %s | RSSI %d dBm | noise %d dBm",
                          snr, (int)context.last_rssi, (int)context.noise_floor);
  }

  return writeFormatted(output, output_len,
                        "%u %s\nLast hop%s%s SNR %s | RSSI %d dBm | noise %d dBm",
                        hops, hops == 1 ? "hop" : "hops", from, repeater, snr,
                        (int)context.last_rssi, (int)context.noise_floor);
}

BotCommandResult executeAir(const BotCommandContext& context, char* output, size_t output_len) {
  const char* target = context.response_target[0] ? context.response_target : NULL;
  if (target) {
    return writeFormatted(output, output_len, "@[%s]: tx %lus rx %lus | rx flood %lu direct %lu | tx flood %lu direct %lu",
                          target, (unsigned long)context.tx_airtime_seconds, (unsigned long)context.rx_airtime_seconds,
                          (unsigned long)context.flood_recv, (unsigned long)context.direct_recv,
                          (unsigned long)context.flood_sent, (unsigned long)context.direct_sent);
  }
  return writeFormatted(output, output_len, "tx %lus rx %lus | rx flood %lu direct %lu | tx flood %lu direct %lu",
                        (unsigned long)context.tx_airtime_seconds, (unsigned long)context.rx_airtime_seconds,
                        (unsigned long)context.flood_recv, (unsigned long)context.direct_recv,
                        (unsigned long)context.flood_sent, (unsigned long)context.direct_sent);
}

BotCommandResult executeTest(const BotCommand& command, const BotCommandContext& context, char* output, size_t output_len) {
  (void)command;

  char snr[8];
  formatQuarters(context.path_snr_quarters, snr, sizeof(snr));

  const char* target = context.response_target[0] ? context.response_target : NULL;
  unsigned hops = (unsigned)context.path_hash_count;

  uint32_t now = context.received_at_timestamp;
  // Receive time includes transit delay; compare in 64 bits to avoid unsigned wrap.
  int64_t offset = (int64_t)context.sender_timestamp - (int64_t)now;
  if (offset < 0) offset = -offset;
  char clock_detail[40];
  const char* clock_status = !context.sender_timestamp || !now ? "Clock comparison unavailable" :
      offset > 600 ? "Your clock is way off" :
      offset > 60 ? clock_detail : "Your clock is ok";
  if (context.sender_timestamp && now && offset > 60 && offset <= 600) {
    snprintf(clock_detail, sizeof(clock_detail), "Your clock is off by %lum %lus",
             (unsigned long)(offset / 60), (unsigned long)(offset % 60));
  }


  int32_t utc_offset = FirmwareBot::easternUtcOffsetSeconds(now);
  uint32_t eastern_time =
      (uint32_t)((int64_t)now + (int64_t)utc_offset);

  char received_at[9];
  formatSecondsHms(eastern_time, received_at, sizeof(received_at));

  const char* tz = utc_offset == -4 * 3600 ? "EDT" : "EST";

  if (hops == 0) {
    if (target) {
      return writeFormatted(output, output_len,
                            "@[%s]\nDirect\nSNR %s | RSSI %d dBm | noise %d dBm\nrecv %s %s\n%s",
                            target, snr,
                            (int)context.last_rssi,
                            (int)context.noise_floor,
                            received_at, tz, clock_status);
    }

    return writeFormatted(output, output_len,
                          "Direct\nSNR %s | RSSI %d dBm | noise %d dBm\nrecv %s %s\n%s",
                          snr,
                          (int)context.last_rssi,
                          (int)context.noise_floor,
                          received_at, tz, clock_status);
  }

  if (target) {
    return writeFormatted(output, output_len,
                          "@[%s]\n%u hops, %u-byte\nrecv %s %s\n%s",
                          target, hops,
                          (unsigned)context.path_hash_size,
                          received_at, tz, clock_status);
  }

  return writeFormatted(output, output_len,
                        "%u hops, %u-byte\nrecv %s %s\n%s",
                        hops,
                        (unsigned)context.path_hash_size,
                        received_at, tz, clock_status);
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
                  ? snprintf(output, output_len, "@[%s] %08lx direct zero-hop tail %s", target,
                             (unsigned long)tag, tail_snr)
                  : snprintf(output, output_len, "Trace %08lx direct zero-hop tail %s", (unsigned long)tag, tail_snr);
    if (written < 0) {
      output[0] = 0;
      return 0;
    }
    return boundedStrLen(output, output_len);
  }
  written = has_target
                ? snprintf(output, output_len, "@[%s] %08lx %uh tail %s | ", target, (unsigned long)tag,
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
      return writeText(output, output_len,
                       "Pong!\n"
                       "If you\'re in Fairfield County, check out #fairfield-county");
    case BOT_COMMAND_TEST:
      return executeTest(command, context, output, output_len);
    case BOT_COMMAND_HELLO: {
      const char* node = context.node_name[0] ? context.node_name : "MeshCore bot";
      const char* target = context.response_target[0] ? context.response_target : NULL;
      if (target) return writeFormatted(output, output_len, "Hello @[%s], I'm %s", target, node);
      return writeFormatted(output, output_len, "Hello, I'm %s", node);
    }
    case BOT_COMMAND_ABOUT:
      return writeText(output, output_len,
                       "I'm a Heltec V4 running modified Colorado Mesh Bot firmware.\n"
                       "Located near Trumbull Mall, CT [FN31jf] [06606]");
    case BOT_COMMAND_STATUS: {
      char up_str[20];
      formatUptime(context.uptime_seconds, up_str, sizeof(up_str));

      if (context.battery_millivolts > 0) {
        return writeFormatted(
            output, output_len,
            "Batt %u.%02uV\n"
            "up %s\n"
            "Storage %lu/%luKB\n"
            "seen %lu sent %lu fail %lu\n"
            "RX %lu %luF/%luD %lus\n"
            "TX %lu %luF/%luD %lus\n"
            "%luerr",
            (unsigned)(context.battery_millivolts / 1000),
            (unsigned)((context.battery_millivolts % 1000) / 10),
            up_str,
            (unsigned long)context.storage_used_kb,
            (unsigned long)context.storage_total_kb,
            (unsigned long)context.observed_messages,
            (unsigned long)context.sent_messages,
            (unsigned long)context.send_failures,
            (unsigned long)context.packets_recv,
            (unsigned long)context.flood_recv,
            (unsigned long)context.direct_recv,
            (unsigned long)context.rx_airtime_seconds,
            (unsigned long)context.packets_sent,
            (unsigned long)context.flood_sent,
            (unsigned long)context.direct_sent,
            (unsigned long)context.tx_airtime_seconds,
            (unsigned long)context.packets_recv_errors);
      }

      return writeFormatted(
          output, output_len,
          "up %s\n"
          "Storage %lu/%luKB\n"
          "seen %lu sent %lu fail %lu\n"
          "RX %lu %luF/%luD %lus\n"
          "TX %lu %luF/%luD %lus\n"
          "%luerr",
          up_str,
          (unsigned long)context.storage_used_kb,
          (unsigned long)context.storage_total_kb,
          (unsigned long)context.observed_messages,
          (unsigned long)context.sent_messages,
          (unsigned long)context.send_failures,
          (unsigned long)context.packets_recv,
          (unsigned long)context.flood_recv,
          (unsigned long)context.direct_recv,
          (unsigned long)context.rx_airtime_seconds,
          (unsigned long)context.packets_sent,
          (unsigned long)context.flood_sent,
          (unsigned long)context.direct_sent,
          (unsigned long)context.tx_airtime_seconds,
          (unsigned long)context.packets_recv_errors);
    }
    case BOT_COMMAND_CHANNELS:
      return writeText(output, output_len,
                       "I will respond only on these Channels:\n"
                       "#bot #test #fairfield-county and Direct Message");
    case BOT_COMMAND_VERSION:
      return writeFormatted(output, output_len, "Firmware %s built %s", context.firmware_version[0] ? context.firmware_version : "unknown",
                            context.firmware_build_date[0] ? context.firmware_build_date : "unknown");
    case BOT_COMMAND_STATS: {
      const char* target = context.response_target[0] ? context.response_target : NULL;
      if (target) {
        return writeFormatted(output, output_len, "@[%s]: %lu seen | %lu ok | %lu sent | %lu fail | RF %lu rx | %lu tx | %lu err",
                              target, (unsigned long)context.observed_messages, (unsigned long)context.eligible_messages,
                              (unsigned long)context.sent_messages, (unsigned long)context.send_failures,
                              (unsigned long)context.packets_recv, (unsigned long)context.packets_sent,
                              (unsigned long)context.packets_recv_errors);
      }
      return writeFormatted(output, output_len, "%lu seen | %lu ok | %lu sent | %lu fail | RF %lu rx | %lu tx | %lu err",
                            (unsigned long)context.observed_messages, (unsigned long)context.eligible_messages,
                            (unsigned long)context.sent_messages, (unsigned long)context.send_failures,
                            (unsigned long)context.packets_recv, (unsigned long)context.packets_sent,
                            (unsigned long)context.packets_recv_errors);
    }
    case BOT_COMMAND_SIG:
      return executeSig(context, output, output_len);
    case BOT_COMMAND_AIR:
      return executeAir(context, output, output_len);
    case BOT_COMMAND_PATH:
      return executePath(command, context, output, output_len);
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
