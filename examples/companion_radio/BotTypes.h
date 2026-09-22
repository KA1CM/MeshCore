#pragma once

#include <stddef.h>
#include <stdint.h>

#define BOT_MAX_TEXT_LEN 160
#define BOT_MAX_RESPONSE_LEN 144
#define BOT_MAX_COMMAND_NAME_LEN 15
#define BOT_MAX_COMMAND_ARGS_LEN 79
#define BOT_MAX_CHANNEL_NAME_LEN 23
#define BOT_MAX_SENDER_NAME_LEN 31
#define BOT_MAX_FIRMWARE_VERSION_LEN 19
#define BOT_MAX_BUILD_DATE_LEN 15
#define BOT_MAX_PATH_BYTES 64
#define BOT_GROUP_RESPONSE_PREFIX_RESERVE (BOT_MAX_SENDER_NAME_LEN + 2)
#define BOT_MAX_GROUP_RESPONSE_LEN (BOT_MAX_TEXT_LEN - BOT_GROUP_RESPONSE_PREFIX_RESERVE)
#define BOT_GROUP_RESPONSE_GUARD_PREFIX "# "
#define BOT_GROUP_RESPONSE_GUARD_PREFIX_LEN 2
#define BOT_COMMAND_COOLDOWN_MILLIS 5000UL
#define BOT_TRACE_COOLDOWN_MILLIS 60000UL
#define BOT_TRACE_TIMEOUT_MILLIS 30000UL
#define BOT_PENDING_TRACE_SLOTS 2
#define BOT_EMERGENCY_PREFIX "EMERGENCY MESSAGE FROM "
#define BOT_EMERGENCY_MAX_PARTS 3
#define BOT_PENDING_EMERGENCY_SLOTS BOT_EMERGENCY_MAX_PARTS
#define BOT_COORDINATOR_PENDING_SLOTS 8
#define BOT_COORDINATOR_RECENT_SLOTS 16
#define BOT_KNOWN_BOT_SLOTS 8
#define BOT_NEIGHBOR_SLOTS 16
#define BOT_NEIGHBOR_RECENT_MILLIS (60UL * 60UL * 1000UL)
#define BOT_RESPONSE_DELAY_BASE_MILLIS 1500UL
#define BOT_RESPONSE_DELAY_JITTER_MILLIS 2200UL
#define BOT_RESPONSE_PENDING_TTL_MILLIS 95000UL
#define BOT_RESPONSE_RECENT_TTL_MILLIS 30000UL
#define BOT_TIE_BREAK_SPREAD_MILLIS 900UL
#define BOT_HOP_TIER_GUARD_MILLIS 600UL
#define BOT_KNOWN_BOT_FLAG_SUPPRESS_NORMAL 0x01
#define BOT_SENDER_KEY_PREFIX_LEN 6
#define BOT_MIN_AUTH_SENDER_KEY_PREFIX_LEN 4
#define BOT_KNOWN_BOT_LABEL_LEN 12
#define BOT_PREFS_MAGIC 0x31504642UL
#define BOT_PREFS_VERSION 7
#define BOT_PREFS_DEFAULT_LOCAL_ADVERT_MILLIS (24UL * 60UL * 60UL * 1000UL)
#define BOT_PREFS_DEFAULT_FLOOD_ADVERT_MILLIS (24UL * 60UL * 60UL * 1000UL)
#define BOT_PREFS_INITIAL_LOCAL_ADVERT_MILLIS 60000UL
#define BOT_PREFS_INITIAL_FLOOD_ADVERT_MILLIS 5000UL
#define BOT_PREFS_MAX_DELAY_MILLIS 60000U
#define BOT_PREFS_MAX_ADVERT_MILLIS (7UL * 24UL * 60UL * 60UL * 1000UL)
#define BOT_PREFS_SERIALIZED_SIZE 296
#define BOT_HOP_STEP_MILLIS_DEFAULT 2000U
#define BOT_HOP_STEP_MILLIS_MAX 30000U
#define BOT_HOP_GROW_MILLIS 400UL
#define BOT_HOP_BIAS_MAX_MILLIS 50000UL

enum BotChannelKind : uint8_t {
  BOT_CHANNEL_DM = 0,
  BOT_CHANNEL_PUBLIC,
  BOT_CHANNEL_BOT,
  BOT_CHANNEL_TESTING,
  BOT_CHANNEL_FAIRFIELD,
  BOT_CHANNEL_EMERGENCY,
  BOT_CHANNEL_OTHER
};

enum BotPolicyDecision : uint8_t {
  BOT_POLICY_IGNORE = 0,
  BOT_POLICY_ALLOW_NORMAL,
  BOT_POLICY_EMERGENCY_FORWARD
};

enum BotCommandId : uint8_t {
  BOT_COMMAND_NONE = 0,
  BOT_COMMAND_HELP = 1,
  BOT_COMMAND_CMD = 2,
  BOT_COMMAND_PING = 3,
  BOT_COMMAND_TEST = 4,
  BOT_COMMAND_HELLO = 5,
  BOT_COMMAND_ABOUT = 6,
  BOT_COMMAND_ROLL = 7,
  BOT_COMMAND_DICE = 8,
  BOT_COMMAND_STATUS = 9,
  BOT_COMMAND_CHANNELS = 10,
  BOT_COMMAND_VERSION = 11,
  BOT_COMMAND_STATS = 12,
  BOT_COMMAND_MAGIC8 = 13,
  BOT_COMMAND_PATH = 14,
  BOT_COMMAND_TRACE = 15,
  BOT_COMMAND_TRACER = 16,
  BOT_COMMAND_PREFIX = 17,
  BOT_COMMAND_TIME = 18,
  BOT_COMMAND_LORA = 19,
  BOT_COMMAND_ID = 20,
  BOT_COMMAND_NEIGHBORS = 21,
  BOT_COMMAND_SIG = 22,
  BOT_COMMAND_AIR = 23,
  BOT_COMMAND_COIN = 24,
  BOT_COMMAND_UNSUPPORTED = 25,
  BOT_COMMAND_UNKNOWN = 26
};

enum BotCommandVisibility : uint8_t {
  BOT_COMMAND_VISIBILITY_DISCOVERABLE = 0,
  BOT_COMMAND_VISIBILITY_HIDDEN,
  BOT_COMMAND_VISIBILITY_INTERNAL
};

enum BotCommandContextClass : uint8_t {
  BOT_COMMAND_CONTEXT_NORMAL = 0,
  BOT_COMMAND_CONTEXT_DIAGNOSTIC,
  BOT_COMMAND_CONTEXT_TRACE,
  BOT_COMMAND_CONTEXT_LOCAL_CONTACT,
  BOT_COMMAND_CONTEXT_UNSUPPORTED,
  BOT_COMMAND_CONTEXT_INTERNAL
};

struct BotCommandMetadata {
  BotCommandId id;
  const char* name;
  const char* const* aliases;
  uint8_t alias_count;
  uint32_t mask;
  BotCommandVisibility visibility;
  BotCommandContextClass context_class;
  const char* summary;
  const char* usage;
  const char* details;
};

#define BOT_COMMAND_MASK_HELP (1UL << BOT_COMMAND_HELP)
#define BOT_COMMAND_MASK_CMD (1UL << BOT_COMMAND_CMD)
#define BOT_COMMAND_MASK_PING (1UL << BOT_COMMAND_PING)
#define BOT_COMMAND_MASK_TEST (1UL << BOT_COMMAND_TEST)
#define BOT_COMMAND_MASK_HELLO (1UL << BOT_COMMAND_HELLO)
#define BOT_COMMAND_MASK_ABOUT (1UL << BOT_COMMAND_ABOUT)
#define BOT_COMMAND_MASK_ROLL (1UL << BOT_COMMAND_ROLL)
#define BOT_COMMAND_MASK_DICE (1UL << BOT_COMMAND_DICE)
#define BOT_COMMAND_MASK_STATUS (1UL << BOT_COMMAND_STATUS)
#define BOT_COMMAND_MASK_CHANNELS (1UL << BOT_COMMAND_CHANNELS)
#define BOT_COMMAND_MASK_VERSION (1UL << BOT_COMMAND_VERSION)
#define BOT_COMMAND_MASK_STATS (1UL << BOT_COMMAND_STATS)
#define BOT_COMMAND_MASK_MAGIC8 (1UL << BOT_COMMAND_MAGIC8)
#define BOT_COMMAND_MASK_PATH (1UL << BOT_COMMAND_PATH)
#define BOT_COMMAND_MASK_TRACE (1UL << BOT_COMMAND_TRACE)
#define BOT_COMMAND_MASK_TRACER (1UL << BOT_COMMAND_TRACER)
#define BOT_COMMAND_MASK_PREFIX (1UL << BOT_COMMAND_PREFIX)
#define BOT_COMMAND_MASK_TIME (1UL << BOT_COMMAND_TIME)
#define BOT_COMMAND_MASK_LORA (1UL << BOT_COMMAND_LORA)
#define BOT_COMMAND_MASK_ID (1UL << BOT_COMMAND_ID)
#define BOT_COMMAND_MASK_NEIGHBORS (1UL << BOT_COMMAND_NEIGHBORS)
#define BOT_COMMAND_MASK_SIG (1UL << BOT_COMMAND_SIG)
#define BOT_COMMAND_MASK_AIR (1UL << BOT_COMMAND_AIR)
#define BOT_COMMAND_MASK_COIN (1UL << BOT_COMMAND_COIN)
#define BOT_COMMAND_MASK_ALL (BOT_COMMAND_MASK_HELP | BOT_COMMAND_MASK_CMD | BOT_COMMAND_MASK_PING | \
                              BOT_COMMAND_MASK_TEST | BOT_COMMAND_MASK_HELLO | BOT_COMMAND_MASK_ABOUT | \
                              BOT_COMMAND_MASK_ROLL | BOT_COMMAND_MASK_DICE | BOT_COMMAND_MASK_STATUS | \
                              BOT_COMMAND_MASK_CHANNELS | BOT_COMMAND_MASK_VERSION | BOT_COMMAND_MASK_STATS | \
                              BOT_COMMAND_MASK_MAGIC8 | BOT_COMMAND_MASK_PATH | BOT_COMMAND_MASK_TRACE | \
                              BOT_COMMAND_MASK_TRACER | BOT_COMMAND_MASK_PREFIX | BOT_COMMAND_MASK_TIME | \
                              BOT_COMMAND_MASK_LORA | BOT_COMMAND_MASK_ID | BOT_COMMAND_MASK_NEIGHBORS | \
                              BOT_COMMAND_MASK_SIG | BOT_COMMAND_MASK_AIR | BOT_COMMAND_MASK_COIN)

enum BotCommandResultCode : uint8_t {
  BOT_COMMAND_RESULT_NOT_HANDLED = 0,
  BOT_COMMAND_RESULT_OK,
  BOT_COMMAND_RESULT_TRUNCATED,
  BOT_COMMAND_RESULT_NO_SPACE
};

enum BotWriteResult : uint8_t {
  BOT_WRITE_OK = 0,
  BOT_WRITE_TRUNCATED,
  BOT_WRITE_NO_SPACE
};

enum BotCoordinatorScheduleResult : uint8_t {
  BOT_COORDINATOR_SCHEDULED = 0,
  BOT_COORDINATOR_REPLACED,
  BOT_COORDINATOR_NO_SPACE,
  BOT_COORDINATOR_NOT_NORMAL
};

enum BotCoordinatorReadyResult : uint8_t {
  BOT_COORDINATOR_READY_NONE = 0,
  BOT_COORDINATOR_READY_SEND,
  BOT_COORDINATOR_READY_SUPPRESSED,
  BOT_COORDINATOR_READY_EXPIRED
};

struct BotFingerprint {
  uint64_t value;
};

struct BotMessage {
  BotChannelKind channel_kind;
  char channel_name[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char sender_name[BOT_MAX_SENDER_NAME_LEN + 1];
  uint8_t sender_key_prefix[BOT_SENDER_KEY_PREFIX_LEN];
  uint8_t sender_key_prefix_len;
  bool text_truncated;
  uint32_t sender_timestamp;
  uint32_t received_at_timestamp;
  uint8_t path_len;
  uint8_t path_hash_size;
  uint8_t path_hash_count;
  int8_t packet_snr_quarters;
  char text[BOT_MAX_TEXT_LEN + 1];
  size_t text_len;
  const uint8_t* path;
};

struct BotCommand {
  BotCommandId id;
  char name[BOT_MAX_COMMAND_NAME_LEN + 1];
  char args[BOT_MAX_COMMAND_ARGS_LEN + 1];
  size_t args_len;
};

struct BotResponse {
  BotPolicyDecision decision;
  BotFingerprint fingerprint;
  char text[BOT_MAX_RESPONSE_LEN + 1];
  size_t text_len;
  bool truncated;
};

struct BotCommandContext {
  char node_name[BOT_MAX_SENDER_NAME_LEN + 1];
  char bot_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char testing_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char emergency_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char public_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char firmware_version[BOT_MAX_FIRMWARE_VERSION_LEN + 1];
  char firmware_build_date[BOT_MAX_BUILD_DATE_LEN + 1];
  uint32_t uptime_seconds;
  uint16_t battery_millivolts;
  uint32_t storage_used_kb;
  uint32_t storage_total_kb;
  uint32_t observed_messages;
  uint32_t ignored_messages;
  uint32_t eligible_messages;
  uint32_t sent_messages;
  uint32_t send_failures;
  uint32_t suppressed_responses;
  uint32_t pending_responses;
  uint32_t emergency_forwards;
  uint32_t emergency_forward_failures;
  uint32_t packets_recv;
  uint32_t packets_sent;
  uint32_t packets_recv_errors;
  uint32_t flood_recv;
  uint32_t flood_sent;
  uint32_t direct_recv;
  uint32_t direct_sent;
  uint32_t tx_airtime_seconds;
  uint32_t rx_airtime_seconds;
  uint32_t random_seed;
  int16_t noise_floor;
  int8_t last_rssi;
  int8_t last_snr_quarters;
  uint8_t queue_depth;
  uint8_t channel_count;
  uint8_t path_len;
  uint8_t path_hash_size;
  uint8_t path_hash_count;
  int8_t path_snr_quarters;
  char response_target[BOT_MAX_SENDER_NAME_LEN + 1];
  const uint8_t* path;
};

struct BotCommandResult {
  BotCommandResultCode code;
  size_t text_len;
};

struct BotCommandCooldown {
  BotCommandId command_id;
  uint32_t expires_at_millis;
};

struct BotKnownBotEntry {
  bool active;
  uint8_t key_prefix[BOT_SENDER_KEY_PREFIX_LEN];
  uint8_t flags;
  char label[BOT_KNOWN_BOT_LABEL_LEN];
};

struct BotNeighbor {
  bool active;
  uint8_t pub_key_prefix[BOT_SENDER_KEY_PREFIX_LEN];
  uint32_t last_heard_millis;
  int16_t rssi_dbm;
  int8_t snr_quarters;
};

struct BotCoordinatorPending {
  bool active;
  bool suppressed;
  BotFingerprint request_fingerprint;
  BotFingerprint response_fingerprint;
  uint32_t due_at_millis;
  uint32_t expires_at_millis;
};

struct BotCoordinatorRecent {
  BotFingerprint response_fingerprint;
  uint32_t response_expires_at_millis;
  uint32_t request_expires_at_millis;
  uint16_t request_token;
  bool active;
  bool request_active;
};

struct BotCoordinatorReady {
  BotCoordinatorReadyResult result;
  BotFingerprint request_fingerprint;
  BotFingerprint response_fingerprint;
};

struct BotEmergencyForward {
  uint8_t part_count;
  bool truncated;
  char parts[BOT_EMERGENCY_MAX_PARTS][BOT_MAX_GROUP_RESPONSE_LEN + 1];
  size_t part_lens[BOT_EMERGENCY_MAX_PARTS];
};

struct BotPrefs {
  bool enabled;
  uint16_t normal_delay_ms;
  uint16_t normal_jitter_ms;
  uint16_t hop_step_ms;
  uint32_t local_advert_interval_ms;
  uint32_t flood_advert_interval_ms;
  uint32_t command_mask;
  uint8_t max_response_parts;
  char bot_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char testing_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char emergency_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  char public_channel[BOT_MAX_CHANNEL_NAME_LEN + 1];
  BotKnownBotEntry known_bots[BOT_KNOWN_BOT_SLOTS];
  uint32_t prefs_load_failures;
  uint32_t prefs_save_failures;
};

struct BotStats {
  uint32_t observed_messages;
  uint32_t ignored_messages;
  uint32_t eligible_messages;
  uint32_t emergency_messages;
  uint32_t emergency_forwards;
  uint32_t emergency_forward_failures;
  uint32_t parse_errors;
  uint32_t pending_responses;
  uint32_t suppressed_responses;
  uint32_t expired_responses;
  uint32_t known_bot_messages;
  uint32_t sent_messages;
  uint32_t send_failures;
};

static_assert(BOT_COMMAND_UNKNOWN < 32, "BotCommandId must fit uint32_t command masks");
static_assert(sizeof(BotMessage) <= 264, "BotMessage RAM budget exceeded");
static_assert(sizeof(BotCommand) <= 120, "BotCommand RAM budget exceeded");
static_assert(sizeof(BotResponse) <= 184, "BotResponse RAM budget exceeded");
static_assert(sizeof(BotCommandContext) <= 312, "BotCommandContext RAM budget exceeded");
static_assert(sizeof(BotCommandResult) <= 16, "BotCommandResult RAM budget exceeded");
static_assert(sizeof(BotCommandCooldown) <= 8, "BotCommandCooldown RAM budget exceeded");
static_assert(sizeof(BotKnownBotEntry) <= 24, "BotKnownBotEntry RAM budget exceeded");
static_assert(sizeof(BotNeighbor) <= 24, "BotNeighbor RAM budget exceeded");
static_assert(sizeof(BotCoordinatorPending) <= 32, "BotCoordinatorPending RAM budget exceeded");
static_assert(sizeof(BotCoordinatorRecent) <= 24, "BotCoordinatorRecent RAM budget exceeded");
static_assert(sizeof(BotCoordinatorReady) <= 24, "BotCoordinatorReady RAM budget exceeded");
static_assert(sizeof(BotEmergencyForward) <= 480, "BotEmergencyForward RAM budget exceeded");
static_assert(sizeof(BotPrefs) <= 320, "BotPrefs RAM budget exceeded");
static_assert(sizeof(BotStats) <= 64, "BotStats RAM budget exceeded");
