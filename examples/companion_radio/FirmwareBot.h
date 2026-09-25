#pragma once

#include "BotTypes.h"

namespace FirmwareBot {

BotWriteResult normalizeText(const char* input, size_t input_len, char* output, size_t output_len, size_t* written);
BotWriteResult normalizeChannelText(const char* text, char* sender, size_t sender_len, char* output, size_t output_len,
                                    size_t* written);
bool parseCommand(const char* text, size_t text_len, BotCommand* command);
bool parseCommand(const char* text, size_t text_len, BotCommand* command, bool allow_prefixless);
bool splitChannelText(const char* text, size_t text_len, char* sender, size_t sender_len, const char** body,
                      size_t* body_len);
BotWriteResult writeResponse(char* output, size_t output_len, const char* text, size_t text_len, size_t* written);
BotWriteResult writeResponseForChannel(BotChannelKind channel_kind, bool allow_prefixless, const char* text,
                                       size_t text_len, char* output, size_t output_len, size_t* written);
BotWriteResult writeAckResponse(const BotMessage& message, const BotCommand& command, char* output, size_t output_len,
                                size_t* written);
int32_t easternUtcOffsetSeconds(uint32_t timestamp);
BotCommandId commandIdForName(const char* name, size_t len);
size_t maxResponseLenForChannel(BotChannelKind channel_kind);
bool isCommandOnCooldown(const BotCommandCooldown* cooldowns, size_t cooldown_count, BotCommandId command_id,
                         uint32_t now_millis);
void recordCommandCooldown(BotCommandCooldown* cooldowns, size_t cooldown_count, BotCommandId command_id,
                           uint32_t now_millis, uint32_t cooldown_millis);

}
