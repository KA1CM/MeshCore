#include "BotPolicy.h"

#include <ctype.h>
#include <string.h>

namespace {

bool equalsIgnoreCase(const char* value, size_t len, const char* expected) {
  if (!value || !expected) return false;

  if (len > 0 && value[0] == '#') {
    value++;
    len--;
  }

  size_t expected_len = strlen(expected);
  if (expected_len > 0 && expected[0] == '#') {
    expected++;
    expected_len--;
  }
  if (len != expected_len) return false;

  for (size_t i = 0; i < len; i++) {
    if (tolower((unsigned char)value[i]) != tolower((unsigned char)expected[i])) return false;
  }
  return true;
}

bool equalsExact(const char* value, size_t len, const char* expected) {
  if (!value || !expected) return false;
  size_t expected_len = strlen(expected);
  if (len != expected_len) return false;
  for (size_t i = 0; i < len; i++) {
    if (value[i] != expected[i]) return false;
  }
  return true;
}

}

namespace BotPolicy {

BotChannelKind classifyChannel(const char* name, size_t len, bool direct_message) {
  if (direct_message) return BOT_CHANNEL_DM;
  if (equalsExact(name, len, "Public")) return BOT_CHANNEL_PUBLIC;
  if (equalsIgnoreCase(name, len, "#bot")) return BOT_CHANNEL_BOT;
  if (equalsIgnoreCase(name, len, "#test")) return BOT_CHANNEL_TESTING;
  if (equalsIgnoreCase(name, len, "#fairfield-county")) return BOT_CHANNEL_FAIRFIELD;
  if (equalsExact(name, len, "#emergency")) return BOT_CHANNEL_EMERGENCY;
  return BOT_CHANNEL_OTHER;
}

BotChannelKind classifyChannel(const char* name, size_t len, bool direct_message, const BotPrefs& prefs) {
  if (direct_message) return BOT_CHANNEL_DM;
  if (equalsExact(name, len, prefs.public_channel)) return BOT_CHANNEL_PUBLIC;
  if (equalsIgnoreCase(name, len, prefs.bot_channel)) return BOT_CHANNEL_BOT;
  if (equalsIgnoreCase(name, len, prefs.testing_channel)) return BOT_CHANNEL_TESTING;
  if (equalsIgnoreCase(name, len, "#fairfield-county")) return BOT_CHANNEL_FAIRFIELD;
  if (equalsExact(name, len, prefs.emergency_channel)) return BOT_CHANNEL_EMERGENCY;
  return BOT_CHANNEL_OTHER;
}

BotPolicyDecision decide(BotChannelKind kind) {
  if (kind == BOT_CHANNEL_DM || kind == BOT_CHANNEL_BOT ||
      kind == BOT_CHANNEL_TESTING || kind == BOT_CHANNEL_FAIRFIELD) {
    return BOT_POLICY_ALLOW_NORMAL;
  }
  if (kind == BOT_CHANNEL_EMERGENCY) return BOT_POLICY_EMERGENCY_FORWARD;
  return BOT_POLICY_IGNORE;
}

bool isNormalAllowed(BotChannelKind kind) {
  return decide(kind) == BOT_POLICY_ALLOW_NORMAL;
}

bool isEmergency(BotChannelKind kind) {
  return decide(kind) == BOT_POLICY_EMERGENCY_FORWARD;
}

bool isPrefixlessCommandAllowed(BotChannelKind kind) {
  return kind == BOT_CHANNEL_DM || kind == BOT_CHANNEL_BOT ||
         kind == BOT_CHANNEL_TESTING || kind == BOT_CHANNEL_FAIRFIELD;
}

}
