#include "BotCommandRegistry.h"

#include <ctype.h>
#include <string.h>

namespace {


size_t boundedStrLen(const char* value, size_t max_len) {
  size_t len = 0;
  while (value && len < max_len && value[len] != 0) len++;
  return len;
}

bool namesEqual(const char* name, size_t len, const char* expected) {
  size_t expected_len = boundedStrLen(expected, BOT_MAX_COMMAND_NAME_LEN + 1);
  if (len != expected_len) return false;
  for (size_t i = 0; i < len; i++) {
    if (tolower((unsigned char)name[i]) != tolower((unsigned char)expected[i])) return false;
  }
  return true;
}

const BotCommandMetadata kCommands[] = {
  { BOT_COMMAND_HELP, "help", NULL, 0, BOT_COMMAND_MASK_HELP, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "Show bot help", "help [command]", "Show available commands or details for one command" },
  { BOT_COMMAND_CMD, "cmd", NULL, 0, BOT_COMMAND_MASK_CMD, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "List commands", "cmd", "List compact command names supported by this firmware bot" },
  { BOT_COMMAND_PING, "ping", NULL, 0, BOT_COMMAND_MASK_PING, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "Check bot response", "ping", "Reply with Pong when the bot is alive" },
  { BOT_COMMAND_TEST, "test", NULL, 0, BOT_COMMAND_MASK_TEST, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "Test connection", "test", "Get test response with connection info" },
  { BOT_COMMAND_HELLO, "hello", NULL, 0, BOT_COMMAND_MASK_HELLO, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "Greet from the node", "hello", "Reply with GM, GA, or GE" },
  { BOT_COMMAND_ABOUT, "about", NULL, 0, BOT_COMMAND_MASK_ABOUT, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "Describe this bot", "about", "Describe the local firmware bot" },
  { BOT_COMMAND_STATUS, "status", NULL, 0, BOT_COMMAND_MASK_STATUS, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show node status", "status", "Show local uptime, battery, storage, and bot send counters" },
  { BOT_COMMAND_CHANNELS, "channels", NULL, 0, BOT_COMMAND_MASK_CHANNELS, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show configured channels", "channels", "Show local bot, testing, emergency, and public channel names" },
  { BOT_COMMAND_VERSION, "version", NULL, 0, BOT_COMMAND_MASK_VERSION, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_NORMAL, "Show firmware version", "version", "Show local firmware version and build date" },
  { BOT_COMMAND_USER, "users", NULL, 0, BOT_COMMAND_MASK_STATS, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Top users", "users", "Top 5 users by accepted commands in the last 24h" },
  { BOT_COMMAND_STATS, "stats", NULL, 0, BOT_COMMAND_MASK_STATS, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show bot counters", "stats", "Show last-24-hour command counts/percentages" },
  { BOT_COMMAND_PATH, "path", NULL, 0, BOT_COMMAND_MASK_PATH, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_TRACE, "Show or decode path", "path [hex-path]", "Show path from requester to local bot" },
  { BOT_COMMAND_TIME, "time", NULL, 0, BOT_COMMAND_MASK_TIME, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show bot time and uptime", "time", "Show local bot wall-clock time and uptime." },
  { BOT_COMMAND_LORA, "lora", NULL, 0, BOT_COMMAND_MASK_LORA, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show LoRa radio settings", "lora", "Show local LoRa frequency, spreading factor, bandwidth, coding rate, and TX power." },
  { BOT_COMMAND_ID, "id", NULL, 0, BOT_COMMAND_MASK_ID, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show bot public-key prefix", "id", "Show local bot public-key prefix and node name" },
  { BOT_COMMAND_NEIGHBORS, "neighbors", NULL, 0, BOT_COMMAND_MASK_NEIGHBORS, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show direct repeater neighbors", "neighbors [all]",
    "Direct neighbors with average RSSI/SNR. Use neighbors all for all pages." },
  { BOT_COMMAND_SIG, "snr", NULL, 0, BOT_COMMAND_MASK_SIG, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Signal Report", "snr",
    "Signal Report: hops count, last repeater, SNR, RSSI and noise floor." },
  { BOT_COMMAND_AIR, "air", NULL, 0, BOT_COMMAND_MASK_AIR, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "Show radio airtime", "air",
    "Show local TX/RX airtime and flood/direct packet counters." },
  { BOT_COMMAND_LIST, "list", NULL, 0, BOT_COMMAND_MASK_LIST, BOT_COMMAND_VISIBILITY_DISCOVERABLE,
    BOT_COMMAND_CONTEXT_DIAGNOSTIC, "List repeater voltages", "list or list low",
    "Latest saved repeater voltages" },
  { BOT_COMMAND_PASSWORD, "password", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Set repeater password", "password <rpt> | <passwd>", "Admin DM only: add or replace the saved private repeater password." },
  { BOT_COMMAND_NOTES, "notes", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "View or replace repeater notes", "notes <rpt> or notes set <rpt> | <text>", "Admin DM only: view or replace saved notes." },
  { BOT_COMMAND_ADD, "add", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Manage repeater list", "add <full key>", "Admin DM only: manage repeaters." },
  { BOT_COMMAND_REMOVE, "remove", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Manage repeater list", "remove <repeater>", "Admin DM only: manage repeaters." },
  { BOT_COMMAND_ENABLE, "enable", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Manage repeater list", "enable <repeater>", "Admin DM only: manage repeaters." },
  { BOT_COMMAND_DISABLE, "disable", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Manage repeater list", "disable <repeater>", "Admin DM only: manage repeaters." },
  { BOT_COMMAND_SYNC, "sync", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Sync repeater clock", "sync <repeater>", "Admin DM only: synchronize one uniquely matching repeater." },
  { BOT_COMMAND_CHECK, "check", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Check repeater voltage", "check <repeater name>", "Admin DM only: check one uniquely matching repeater." },
  { BOT_COMMAND_ADVERT, "advert", NULL, 0, 0, BOT_COMMAND_VISIBILITY_HIDDEN,
    BOT_COMMAND_CONTEXT_INTERNAL, "Send flood advert", "advert", "Admin DM only: queue a flood self advert." },
  { BOT_COMMAND_UNKNOWN, "unknown", NULL, 0, 0, BOT_COMMAND_VISIBILITY_INTERNAL,
    BOT_COMMAND_CONTEXT_INTERNAL, "Unknown command", "unknown", "Internal unknown-command handler." }
};

}

namespace BotCommandRegistry {

size_t commandCount() {
  return sizeof(kCommands) / sizeof(kCommands[0]);
}

const BotCommandMetadata* commandAt(size_t index) {
  return index < commandCount() ? &kCommands[index] : NULL;
}

const BotCommandMetadata* findById(BotCommandId id) {
  for (size_t i = 0; i < commandCount(); i++) {
    if (kCommands[i].id == id) return &kCommands[i];
  }
  return NULL;
}

const BotCommandMetadata* findByName(const char* name, size_t len) {
  if (!name || len == 0 || len > BOT_MAX_COMMAND_NAME_LEN) return NULL;
  for (size_t i = 0; i < commandCount(); i++) {
    if (namesEqual(name, len, kCommands[i].name)) return &kCommands[i];
    for (uint8_t j = 0; j < kCommands[i].alias_count; j++) {
      if (namesEqual(name, len, kCommands[i].aliases[j])) return &kCommands[i];
    }
  }
  return NULL;
}

const char* commandName(BotCommandId id) {
  const BotCommandMetadata* command = findById(id);
  return command ? command->name : "";
}

uint32_t commandMask(BotCommandId id) {
  const BotCommandMetadata* command = findById(id);
  return command ? command->mask : 0;
}

bool isDiscoverable(BotCommandId id) {
  const BotCommandMetadata* command = findById(id);
  return command && command->visibility == BOT_COMMAND_VISIBILITY_DISCOVERABLE;
}

}
