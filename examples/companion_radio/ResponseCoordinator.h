#pragma once

#include "BotTypes.h"

namespace ResponseCoordinator {

void clear(BotCoordinatorPending pending[], size_t pending_count);
void clearRecent(BotCoordinatorRecent recent[], size_t recent_count);
uint32_t responseDelayMillis(const BotMessage& message, BotCommandId command_id, BotFingerprint request_fingerprint,
                             uint32_t bot_identity_seed, uint8_t queue_depth, uint32_t jitter_seed);
uint32_t responseDelayMillis(const BotMessage& message, BotCommandId command_id, BotFingerprint request_fingerprint,
                             uint32_t bot_identity_seed, uint8_t queue_depth, uint32_t jitter_seed,
                             uint16_t base_delay_millis, uint16_t jitter_millis);
uint32_t responseDelayMillis(const BotMessage& message, BotCommandId command_id, BotFingerprint request_fingerprint,
                             uint32_t bot_identity_seed, uint8_t queue_depth, uint32_t jitter_seed,
                             uint16_t base_delay_millis, uint16_t jitter_millis, uint16_t hop_step_millis);
BotCoordinatorScheduleResult schedule(BotCoordinatorPending pending[], size_t pending_count,
                                      const BotMessage& message, BotCommandId command_id,
                                      BotFingerprint request_fingerprint, BotFingerprint response_fingerprint,
                                      uint32_t now_millis, uint32_t jitter_seed, uint32_t bot_identity_seed,
                                      uint8_t queue_depth, BotFingerprint* fingerprint, uint32_t* due_at_millis);
BotCoordinatorScheduleResult schedule(BotCoordinatorPending pending[], size_t pending_count,
                                      const BotMessage& message, BotCommandId command_id,
                                      BotFingerprint request_fingerprint, BotFingerprint response_fingerprint,
                                      uint32_t now_millis, uint32_t jitter_seed, uint32_t bot_identity_seed,
                                      uint8_t queue_depth, uint16_t base_delay_millis, uint16_t jitter_millis,
                                      BotFingerprint* fingerprint, uint32_t* due_at_millis);
BotCoordinatorScheduleResult schedule(BotCoordinatorPending pending[], size_t pending_count,
                                      const BotMessage& message, BotCommandId command_id,
                                      BotFingerprint request_fingerprint, BotFingerprint response_fingerprint,
                                      uint32_t now_millis, uint32_t jitter_seed, uint32_t bot_identity_seed,
                                      uint8_t queue_depth, uint16_t base_delay_millis, uint16_t jitter_millis,
                                      uint16_t hop_step_millis, BotFingerprint* fingerprint, uint32_t* due_at_millis);
bool suppress(BotCoordinatorPending pending[], size_t pending_count, BotFingerprint response_fingerprint);
bool suppressByRequestToken(BotCoordinatorPending pending[], size_t pending_count, uint16_t request_token);
bool cancel(BotCoordinatorPending pending[], size_t pending_count, BotFingerprint request_fingerprint);
BotCoordinatorReady poll(BotCoordinatorPending pending[], size_t pending_count, uint32_t now_millis);
void recordRecent(BotCoordinatorRecent recent[], size_t recent_count, BotFingerprint response_fingerprint,
                  uint32_t now_millis);
bool recentlySent(BotCoordinatorRecent recent[], size_t recent_count, BotFingerprint response_fingerprint,
                  uint32_t now_millis);
void recordRequestToken(BotCoordinatorRecent recent[], size_t recent_count, uint16_t request_token,
                        uint32_t now_millis);
bool recentlyAnswered(BotCoordinatorRecent recent[], size_t recent_count, uint16_t request_token,
                      uint32_t now_millis);

}
