#pragma once
#include "BotTypes.h"
#include <strings.h>

namespace BotFairfieldHelp {
constexpr char listReply[]=
  "The complete list of repeaters forwarding ct-ffc packets can be found here http://tiny.cc/ffclist";
constexpr char listHelp[]=
  "list: complete list link.\n"
  "list all: hashes heard in 30 days.\n"
  "list <name>: matching repeater names.";
static_assert(sizeof(listReply)-1<=BOT_MAX_GROUP_RESPONSE_LEN,"Fairfield list link must fit one message");
static_assert(sizeof(listHelp)-1<=BOT_MAX_GROUP_RESPONSE_LEN,"Fairfield list help must fit one message");
constexpr char commands[]=
  "ct-ffc commands:\n"
  "help cmd hello about version time status stats users air ping test path snr channels lora id neighbors list";
static_assert(sizeof(commands)-1<=BOT_MAX_GROUP_RESPONSE_LEN,"Fairfield help must fit one channel message");

// Return nullptr for shared help so its existing registry descriptions remain
// the source of truth. Only channel-specific behavior is described here.
inline const char* reply(const BotCommand& command) {
  if(command.id==BOT_COMMAND_CMD) return commands;
  if(command.id!=BOT_COMMAND_HELP) return nullptr;
  if(!command.args_len) return commands;
  if(!strcasecmp(command.args,"list"))
    return listHelp;
  if(!strcasecmp(command.args,"list all"))
    return "list all: all ct-ffc repeater hashes observed in the last 30 days, sent in paced messages.";
  if(!strcasecmp(command.args,"list low"))
    return "list <name>: matching known ct-ffc repeater names from the last 30 days. Example: list gbarc";
  if(!strcasecmp(command.args,"ping"))
    return "ping: reply with Pong! and the count of confirmed ct-ffc repeaters observed in the last 30 days.";
  if(!strcasecmp(command.args,"hello"))
    return "hello: greet you and explain ct-ffc scoped messaging in #fairfield-county.";
  if(!strcasecmp(command.args,"help"))
    return "help <command>: show command details for #fairfield-county. Send commands scoped with ct-ffc.";
  return nullptr;
}
}
