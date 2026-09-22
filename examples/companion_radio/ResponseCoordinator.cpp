#include "ResponseCoordinator.h"

#include "BotPolicy.h"

#include <string.h>

namespace {

bool isNormalChannel(BotChannelKind kind) {
  return BotPolicy::isNormalAllowed(kind);
}

bool sameFingerprint(BotFingerprint a, BotFingerprint b) {
  return a.value == b.value;
}

uint16_t requestToken(BotFingerprint fingerprint) {
  return (uint16_t)(fingerprint.value & 0xFFFFu);
}

uint32_t channelDelayBias(BotChannelKind kind) {
  if (kind == BOT_CHANNEL_DM) return 0;
  if (kind == BOT_CHANNEL_BOT) return 200;
  if (kind == BOT_CHANNEL_TESTING) return 400;
  return 800;
}

uint32_t hopDelayBias(BotChannelKind kind, uint8_t path_hash_count, uint16_t hop_step_millis, uint16_t jitter_millis) {
  if (kind == BOT_CHANNEL_DM) return 0;
  uint32_t hop = (uint32_t)path_hash_count;
  uint32_t tier_step = (uint32_t)hop_step_millis + (uint32_t)jitter_millis + BOT_TIE_BREAK_SPREAD_MILLIS + BOT_HOP_TIER_GUARD_MILLIS;
  uint32_t bias = hop * tier_step + hop * hop * BOT_HOP_GROW_MILLIS;
  if (bias > BOT_HOP_BIAS_MAX_MILLIS) bias = BOT_HOP_BIAS_MAX_MILLIS;
  return bias;
}

uint32_t tieBreakBias(BotFingerprint request_fingerprint, uint32_t bot_identity_seed) {
  uint32_t mixed = (uint32_t)request_fingerprint.value ^ (uint32_t)(request_fingerprint.value >> 32) ^ bot_identity_seed;
  mixed ^= mixed >> 16;
  mixed *= 0x7feb352dUL;
  mixed ^= mixed >> 15;
  return mixed % BOT_TIE_BREAK_SPREAD_MILLIS;
}

uint32_t queueDelayBias(uint8_t queue_depth) {
  return (uint32_t)queue_depth * 150UL;
}

bool millisDue(uint32_t now_millis, uint32_t then_millis) {
  return (int32_t)(now_millis - then_millis) >= 0;
}

}

namespace ResponseCoordinator {

void clear(BotCoordinatorPending pending[], size_t pending_count) {
  if (!pending) return;
  memset(pending, 0, sizeof(BotCoordinatorPending) * pending_count);
}

void clearRecent(BotCoordinatorRecent recent[], size_t recent_count) {
  if (!recent) return;
  memset(recent, 0, sizeof(BotCoordinatorRecent) * recent_count);
}

uint32_t responseDelayMillis(const BotMessage& message, BotCommandId command_id, BotFingerprint request_fingerprint,
                             uint32_t bot_identity_seed, uint8_t queue_depth, uint32_t jitter_seed) {
  return responseDelayMillis(message, command_id, request_fingerprint, bot_identity_seed, queue_depth, jitter_seed,
                             BOT_RESPONSE_DELAY_BASE_MILLIS, BOT_RESPONSE_DELAY_JITTER_MILLIS,
                             BOT_HOP_STEP_MILLIS_DEFAULT);
}

uint32_t responseDelayMillis(const BotMessage& message, BotCommandId command_id, BotFingerprint request_fingerprint,
                             uint32_t bot_identity_seed, uint8_t queue_depth, uint32_t jitter_seed,
                             uint16_t base_delay_millis, uint16_t jitter_millis) {
  return responseDelayMillis(message, command_id, request_fingerprint, bot_identity_seed, queue_depth, jitter_seed,
                             base_delay_millis, jitter_millis, BOT_HOP_STEP_MILLIS_DEFAULT);
}

uint32_t responseDelayMillis(const BotMessage& message, BotCommandId command_id, BotFingerprint request_fingerprint,
                             uint32_t bot_identity_seed, uint8_t queue_depth, uint32_t jitter_seed,
                             uint16_t base_delay_millis, uint16_t jitter_millis, uint16_t hop_step_millis) {
  (void)command_id;
  uint32_t jitter = jitter_millis ? jitter_seed % jitter_millis : 0;
  return (uint32_t)base_delay_millis + channelDelayBias(message.channel_kind) +
         hopDelayBias(message.channel_kind, message.path_hash_count, hop_step_millis, jitter_millis) +
         queueDelayBias(queue_depth) + tieBreakBias(request_fingerprint, bot_identity_seed) + jitter;
}

BotCoordinatorScheduleResult schedule(BotCoordinatorPending pending[], size_t pending_count,
                                      const BotMessage& message, BotCommandId command_id,
                                      BotFingerprint request_fingerprint, BotFingerprint response_fingerprint,
                                      uint32_t now_millis, uint32_t jitter_seed, uint32_t bot_identity_seed,
                                      uint8_t queue_depth, BotFingerprint* fingerprint, uint32_t* due_at_millis) {
  return schedule(pending, pending_count, message, command_id, request_fingerprint, response_fingerprint, now_millis,
                  jitter_seed, bot_identity_seed, queue_depth, BOT_RESPONSE_DELAY_BASE_MILLIS,
                  BOT_RESPONSE_DELAY_JITTER_MILLIS, BOT_HOP_STEP_MILLIS_DEFAULT, fingerprint, due_at_millis);
}

BotCoordinatorScheduleResult schedule(BotCoordinatorPending pending[], size_t pending_count,
                                      const BotMessage& message, BotCommandId command_id,
                                      BotFingerprint request_fingerprint, BotFingerprint response_fingerprint,
                                      uint32_t now_millis, uint32_t jitter_seed, uint32_t bot_identity_seed,
                                      uint8_t queue_depth, uint16_t base_delay_millis, uint16_t jitter_millis,
                                      BotFingerprint* fingerprint, uint32_t* due_at_millis) {
  return schedule(pending, pending_count, message, command_id, request_fingerprint, response_fingerprint, now_millis,
                  jitter_seed, bot_identity_seed, queue_depth, base_delay_millis, jitter_millis,
                  BOT_HOP_STEP_MILLIS_DEFAULT, fingerprint, due_at_millis);
}

BotCoordinatorScheduleResult schedule(BotCoordinatorPending pending[], size_t pending_count,
                                      const BotMessage& message, BotCommandId command_id,
                                      BotFingerprint request_fingerprint, BotFingerprint response_fingerprint,
                                      uint32_t now_millis, uint32_t jitter_seed, uint32_t bot_identity_seed,
                                      uint8_t queue_depth, uint16_t base_delay_millis, uint16_t jitter_millis,
                                      uint16_t hop_step_millis, BotFingerprint* fingerprint,
                                      uint32_t* due_at_millis) {
  if (fingerprint) fingerprint->value = 0;
  if (due_at_millis) *due_at_millis = 0;
  if (!pending || pending_count == 0 || !isNormalChannel(message.channel_kind) || request_fingerprint.value == 0 || response_fingerprint.value == 0) return BOT_COORDINATOR_NOT_NORMAL;

  uint32_t due = now_millis + responseDelayMillis(message, command_id, request_fingerprint, bot_identity_seed, queue_depth,
                                                  jitter_seed, base_delay_millis, jitter_millis, hop_step_millis);
  size_t slot = pending_count;

  for (size_t i = 0; i < pending_count; i++) {
    if (pending[i].active && sameFingerprint(pending[i].request_fingerprint, request_fingerprint)) {
      slot = i;
      break;
    }
    if (slot == pending_count && !pending[i].active) slot = i;
  }
  if (slot == pending_count) return BOT_COORDINATOR_NO_SPACE;

  bool replaced = pending[slot].active;
  bool preserve_suppressed = replaced && pending[slot].suppressed &&
                             sameFingerprint(pending[slot].request_fingerprint, request_fingerprint);
  pending[slot].active = true;
  pending[slot].suppressed = preserve_suppressed;
  pending[slot].request_fingerprint = request_fingerprint;
  pending[slot].response_fingerprint = response_fingerprint;
  pending[slot].due_at_millis = due;
  pending[slot].expires_at_millis = now_millis + BOT_RESPONSE_PENDING_TTL_MILLIS;
  if (fingerprint) *fingerprint = request_fingerprint;
  if (due_at_millis) *due_at_millis = due;
  return replaced ? BOT_COORDINATOR_REPLACED : BOT_COORDINATOR_SCHEDULED;
}

bool suppress(BotCoordinatorPending pending[], size_t pending_count, BotFingerprint response_fingerprint) {
  if (!pending || response_fingerprint.value == 0) return false;
  bool suppressed = false;
  for (size_t i = 0; i < pending_count; i++) {
    if (pending[i].active && sameFingerprint(pending[i].response_fingerprint, response_fingerprint)) {
      pending[i].suppressed = true;
      suppressed = true;
    }
  }
  return suppressed;
}

bool suppressByRequestToken(BotCoordinatorPending pending[], size_t pending_count, uint16_t request_token) {
  if (!pending) return false;
  bool suppressed = false;
  for (size_t i = 0; i < pending_count; i++) {
    if (!pending[i].active) continue;
    if (pending[i].request_fingerprint.value == 0) continue;
    uint16_t token = requestToken(pending[i].request_fingerprint);
    if (token == request_token) {
      pending[i].suppressed = true;
      suppressed = true;
    }
  }
  return suppressed;
}

bool cancel(BotCoordinatorPending pending[], size_t pending_count, BotFingerprint request_fingerprint) {
  if (!pending || request_fingerprint.value == 0) return false;
  for (size_t i = 0; i < pending_count; i++) {
    if (pending[i].active && sameFingerprint(pending[i].request_fingerprint, request_fingerprint)) {
      pending[i].active = false;
      return true;
    }
  }
  return false;
}

BotCoordinatorReady poll(BotCoordinatorPending pending[], size_t pending_count, uint32_t now_millis) {
  BotCoordinatorReady ready;
  ready.result = BOT_COORDINATOR_READY_NONE;
  ready.request_fingerprint.value = 0;
  ready.response_fingerprint.value = 0;
  if (!pending) return ready;

  for (size_t i = 0; i < pending_count; i++) {
    if (!pending[i].active) continue;
    if (pending[i].suppressed) {
      ready.result = BOT_COORDINATOR_READY_SUPPRESSED;
      ready.request_fingerprint = pending[i].request_fingerprint;
      ready.response_fingerprint = pending[i].response_fingerprint;
      pending[i].active = false;
      return ready;
    }
    if (millisDue(now_millis, pending[i].expires_at_millis)) {
      ready.result = BOT_COORDINATOR_READY_EXPIRED;
      ready.request_fingerprint = pending[i].request_fingerprint;
      ready.response_fingerprint = pending[i].response_fingerprint;
      pending[i].active = false;
      return ready;
    }
    if (millisDue(now_millis, pending[i].due_at_millis)) {
      ready.result = BOT_COORDINATOR_READY_SEND;
      ready.request_fingerprint = pending[i].request_fingerprint;
      ready.response_fingerprint = pending[i].response_fingerprint;
      pending[i].active = false;
      return ready;
    }
  }

  return ready;
}

void recordRecent(BotCoordinatorRecent recent[], size_t recent_count, BotFingerprint response_fingerprint,
                  uint32_t now_millis) {
  if (!recent || recent_count == 0 || response_fingerprint.value == 0) return;

  size_t slot = recent_count;
  for (size_t i = 0; i < recent_count; i++) {
    if (recent[i].active && sameFingerprint(recent[i].response_fingerprint, response_fingerprint)) {
      slot = i;
      break;
    }
    bool expired_response = !recent[i].active || recent[i].response_fingerprint.value == 0 || millisDue(now_millis, recent[i].response_expires_at_millis);
    bool expired_request = !recent[i].active || !recent[i].request_active || millisDue(now_millis, recent[i].request_expires_at_millis);
    if (slot == recent_count && expired_response && expired_request) slot = i;
  }
  if (slot == recent_count) slot = 0;
  recent[slot].active = true;
  recent[slot].response_fingerprint = response_fingerprint;
  recent[slot].response_expires_at_millis = now_millis + BOT_RESPONSE_RECENT_TTL_MILLIS;
}

bool recentlySent(BotCoordinatorRecent recent[], size_t recent_count, BotFingerprint response_fingerprint,
                  uint32_t now_millis) {
  if (!recent || response_fingerprint.value == 0) return false;
  for (size_t i = 0; i < recent_count; i++) {
    if (!recent[i].active) continue;
    if (millisDue(now_millis, recent[i].response_expires_at_millis)) {
      recent[i].response_fingerprint.value = 0;
    }
    if (recent[i].response_fingerprint.value != 0 && sameFingerprint(recent[i].response_fingerprint, response_fingerprint)) return true;
    if (recent[i].response_fingerprint.value == 0 && !recent[i].request_active) recent[i].active = false;
  }
  return false;
}

void recordRequestToken(BotCoordinatorRecent recent[], size_t recent_count, uint16_t request_token,
                        uint32_t now_millis) {
  if (!recent || recent_count == 0) return;

  size_t slot = recent_count;
  for (size_t i = 0; i < recent_count; i++) {
    if (recent[i].active && millisDue(now_millis, recent[i].response_expires_at_millis)) {
      recent[i].response_fingerprint.value = 0;
    }
    if (recent[i].active && recent[i].request_active && millisDue(now_millis, recent[i].request_expires_at_millis)) {
      recent[i].request_active = false;
    }
    if (recent[i].active && recent[i].request_active && recent[i].request_token == request_token) {
      slot = i;
      break;
    }
    bool expired_response = !recent[i].active || recent[i].response_fingerprint.value == 0;
    bool expired_request = !recent[i].active || !recent[i].request_active;
    if (slot == recent_count && expired_response && expired_request) slot = i;
  }
  if (slot == recent_count) slot = 0;
  recent[slot].active = true;
  recent[slot].request_active = true;
  recent[slot].request_token = request_token;
  recent[slot].request_expires_at_millis = now_millis + BOT_RESPONSE_RECENT_TTL_MILLIS;
}

bool recentlyAnswered(BotCoordinatorRecent recent[], size_t recent_count, uint16_t request_token,
                      uint32_t now_millis) {
  if (!recent) return false;
  for (size_t i = 0; i < recent_count; i++) {
    if (!recent[i].active) continue;
    if (recent[i].request_active && millisDue(now_millis, recent[i].request_expires_at_millis)) {
      recent[i].request_active = false;
    }
    if (recent[i].request_active && recent[i].request_token == request_token) return true;
    if (recent[i].response_fingerprint.value == 0 && !recent[i].request_active) recent[i].active = false;
  }
  return false;
}

}
