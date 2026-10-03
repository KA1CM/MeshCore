#pragma once

#include "BotTypes.h"
#include "BotPath.h"

namespace BotCommands {

bool pathRoute(const BotCommand& command, const BotCommandContext& context, BotPath::Route& route);

BotCommandResult executeCommand(const BotCommand& command, const BotCommandContext& context, char* output,
                                size_t output_len);

size_t formatTraceResult(char* output, size_t output_len, const char* target, uint32_t tag, uint8_t hash_size,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t hop_count,
                         int8_t tail_snr_quarters);

}
