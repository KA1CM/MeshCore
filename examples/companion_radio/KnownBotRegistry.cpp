#include "KnownBotRegistry.h"

#include <string.h>

namespace {

bool keyEqual(const uint8_t a[BOT_SENDER_KEY_PREFIX_LEN], const uint8_t b[BOT_SENDER_KEY_PREFIX_LEN]) {
  return memcmp(a, b, BOT_SENDER_KEY_PREFIX_LEN) == 0;
}

bool keyPrefixEqual(const uint8_t a[BOT_SENDER_KEY_PREFIX_LEN], const uint8_t b[BOT_SENDER_KEY_PREFIX_LEN], size_t len) {
  return memcmp(a, b, len) == 0;
}

void copyLabel(char dest[12], const char* label) {
  size_t i = 0;
  if (label) {
    while (i + 1 < 12 && label[i] != 0) {
      dest[i] = label[i];
      i++;
    }
  }
  dest[i] = 0;
}

}

namespace KnownBotRegistry {

void clear(BotKnownBotEntry entries[], size_t entry_count) {
  if (!entries) return;
  memset(entries, 0, sizeof(BotKnownBotEntry) * entry_count);
}

const BotKnownBotEntry* find(const BotKnownBotEntry entries[], size_t entry_count,
                             const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], size_t key_prefix_len) {
  if (!entries || !key_prefix || key_prefix_len < BOT_MIN_AUTH_SENDER_KEY_PREFIX_LEN) return NULL;
  if (key_prefix_len > BOT_SENDER_KEY_PREFIX_LEN) key_prefix_len = BOT_SENDER_KEY_PREFIX_LEN;

  const BotKnownBotEntry* match = NULL;
  for (size_t i = 0; i < entry_count; i++) {
    if (!entries[i].active || !keyPrefixEqual(entries[i].key_prefix, key_prefix, key_prefix_len)) continue;
    if (match) return NULL;
    match = &entries[i];
  }
  return match;
}

bool add(BotKnownBotEntry entries[], size_t entry_count, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN],
         uint8_t flags, const char* label) {
  if (!entries || !key_prefix || entry_count == 0) return false;

  size_t slot = entry_count;
  for (size_t i = 0; i < entry_count; i++) {
    if (entries[i].active && keyEqual(entries[i].key_prefix, key_prefix)) {
      slot = i;
      break;
    }
    if (slot == entry_count && !entries[i].active) slot = i;
  }
  if (slot == entry_count) return false;

  entries[slot].active = true;
  memcpy(entries[slot].key_prefix, key_prefix, BOT_SENDER_KEY_PREFIX_LEN);
  entries[slot].flags = flags;
  copyLabel(entries[slot].label, label);
  return true;
}

bool remove(BotKnownBotEntry entries[], size_t entry_count, const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN]) {
  if (!entries || !key_prefix) return false;
  for (size_t i = 0; i < entry_count; i++) {
    if (entries[i].active && keyEqual(entries[i].key_prefix, key_prefix)) {
      memset(&entries[i], 0, sizeof(entries[i]));
      return true;
    }
  }
  return false;
}

bool canSuppressNormal(const BotKnownBotEntry entries[], size_t entry_count,
                       const uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN], size_t key_prefix_len) {
  const BotKnownBotEntry* entry = find(entries, entry_count, key_prefix, key_prefix_len);
  return entry && (entry->flags & BOT_KNOWN_BOT_FLAG_SUPPRESS_NORMAL) != 0;
}

}
