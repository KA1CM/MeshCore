#pragma once
#include "BotPathLookup.h"
#include <cstring>

// Main-loop scope: associate synchronous sends with one diagnostics row.
// Allocate rows only for handled requests, never for ignored commands.
class BotCommandTrace {
  uint32_t& context;
  uint32_t previous,id=0;
  const BotMessage& message;
  const uint8_t* key;
  BotCommandId command;
  bool retry,mentioned;
  void create() {
    if(id || command==BOT_COMMAND_UNKNOWN || command==BOT_COMMAND_UNSUPPORTED)return;
    id=BotPathLookup::recordEvent(message.sender_name,"Command received",
      message.channel_kind==BOT_CHANNEL_DM ? "DM" : message.channel_name);
    BotPathLookup::recordCommand(id,command);
    const auto scope=message.channel_kind==BOT_CHANNEL_FAIRFIELD && message.fairfield_scope_valid
      ? BOT_REPLY_CT_FFC : message.reply_scope;
    BotPathLookup::recordRequestContext(id,scope,mentioned);
    if(key && message.channel_kind==BOT_CHANNEL_DM)
      BotPathLookup::recordDmRequest(id,key,message.sender_timestamp,retry,message.path_is_inbound);
    context=id;
  }
public:
  BotCommandTrace(uint32_t& context,const BotMessage& message,const uint8_t* key,BotCommandId command,bool retry,bool mentioned=false)
    :context(context),previous(context),message(message),key(key),command(command),retry(retry),mentioned(mentioned) {
    context=0;
  }
  ~BotCommandTrace(){context=previous;}
  bool hasHistory() const {return id!=0;}
  uint32_t historyId() { create();return id; }
  void stage(const char* text){
    if(text && !strncmp(text,"Ignored:",8))return;
    create();BotPathLookup::recordCommandStage(id,text);
  }
};
