#include "EmergencyForwarder.h"

#include <stdio.h>
#include <string.h>

namespace {

size_t boundedStrLen(const char* value, size_t max_len) {
  size_t len = 0;
  while (value && len < max_len && value[len] != 0) len++;
  return len;
}

bool prefixEqual(const char* text, size_t text_len, const char* prefix) {
  size_t prefix_len = strlen(prefix);
  if (text_len < prefix_len) return false;
  for (size_t i = 0; i < prefix_len; i++) {
    if (text[i] != prefix[i]) return false;
  }
  return true;
}

size_t appendText(char* output, size_t output_len, size_t pos, const char* text, size_t text_len) {
  if (!output || output_len == 0) return 0;
  while (pos + 1 < output_len && text_len > 0) {
    output[pos++] = *text++;
    text_len--;
  }
  output[pos] = 0;
  return pos;
}

size_t appendRepeated(char* output, size_t output_len, size_t pos, char ch, size_t count) {
  while (pos + 1 < output_len && count > 0) {
    output[pos++] = ch;
    count--;
  }
  output[pos] = 0;
  return pos;
}

void writePart(BotEmergencyForward& forward, uint8_t part_idx, const char* header, size_t header_len,
               const char* text, size_t text_len, bool multipart) {
  char* output = forward.parts[part_idx];
  size_t output_len = sizeof(forward.parts[part_idx]);
  output[0] = 0;
  size_t pos = appendText(output, output_len, 0, header, header_len);

  if (multipart) {
    char marker[8];
    int n = snprintf(marker, sizeof(marker), "[%u/%u] ", (unsigned)(part_idx + 1), (unsigned)forward.part_count);
    if (n > 0) pos = appendText(output, output_len, pos, marker, (size_t)n);
  }

  pos = appendText(output, output_len, pos, text, text_len);
  forward.part_lens[part_idx] = pos;
}

}

namespace EmergencyForwarder {

bool isForwardedEmergencyText(const char* text, size_t text_len) {
  if (!text) return false;
  size_t len = boundedStrLen(text, text_len);
  return prefixEqual(text, len, BOT_EMERGENCY_PREFIX);
}

bool format(const BotMessage& message, BotEmergencyForward& forward) {
  memset(&forward, 0, sizeof(forward));
  if (message.channel_kind != BOT_CHANNEL_EMERGENCY) return false;
  if (isForwardedEmergencyText(message.text, message.text_len)) return false;

  char header[BOT_MAX_GROUP_RESPONSE_LEN + 1];
  const char* sender = message.sender_name[0] ? message.sender_name : "unknown";
  int header_len_int = snprintf(header, sizeof(header), BOT_EMERGENCY_PREFIX "@[%s]: ", sender);
  if (header_len_int < 0) return false;
  size_t header_len = (size_t)header_len_int;
  if (header_len >= sizeof(header)) header_len = sizeof(header) - 1;
  if (header_len >= BOT_MAX_GROUP_RESPONSE_LEN) return false;

  size_t text_len = boundedStrLen(message.text, message.text_len);
  size_t one_part_capacity = BOT_MAX_GROUP_RESPONSE_LEN - header_len;
  if (text_len <= one_part_capacity) {
    forward.part_count = 1;
    forward.truncated = message.text_truncated;
    writePart(forward, 0, header, header_len, message.text, text_len, false);
    return true;
  }

  size_t multipart_header_extra = 6;
  if (header_len + multipart_header_extra >= BOT_MAX_GROUP_RESPONSE_LEN) return false;
  size_t part_capacity = BOT_MAX_GROUP_RESPONSE_LEN - header_len - multipart_header_extra;
  size_t needed_parts = (text_len + part_capacity - 1) / part_capacity;
  forward.part_count = needed_parts > BOT_EMERGENCY_MAX_PARTS ? BOT_EMERGENCY_MAX_PARTS : (uint8_t)needed_parts;
  forward.truncated = message.text_truncated || needed_parts > BOT_EMERGENCY_MAX_PARTS;

  size_t offset = 0;
  for (uint8_t i = 0; i < forward.part_count; i++) {
    size_t chunk_len = text_len - offset;
    if (chunk_len > part_capacity) chunk_len = part_capacity;
    writePart(forward, i, header, header_len, &message.text[offset], chunk_len, true);
    offset += chunk_len;
  }

  if (forward.truncated && forward.part_count > 0) {
    uint8_t last = forward.part_count - 1;
    size_t pos = forward.part_lens[last];
    if (pos > 3) pos -= 3;
    forward.parts[last][pos] = 0;
    pos = appendRepeated(forward.parts[last], sizeof(forward.parts[last]), pos, '.', 3);
    forward.part_lens[last] = pos;
  }

  return forward.part_count > 0;
}

}
