#pragma once
#include "BotPathLookup.h"

// Main-loop scope: associate synchronous sends with one diagnostics row.
// PATH creates its row when its queued lookup starts; blocked PATHs log here.
class BotCommandTrace {
  uint32_t& context;
  uint32_t previous,id=0;
  const BotMessage& message;
  const uint8_t* key;
  BotCommandId command;
  bool retry;
  void create() {
    if(id || command==BOT_COMMAND_UNKNOWN || command==BOT_COMMAND_UNSUPPORTED)return;
    id=BotPathLookup::recordEvent(message.sender_name,"Command received",
      message.channel_kind==BOT_CHANNEL_DM ? "DM" : message.channel_name);
    BotPathLookup::recordCommand(id,command);
    if(key && message.channel_kind==BOT_CHANNEL_DM)
      BotPathLookup::recordDmRequest(id,key,message.sender_timestamp,retry,message.path_is_inbound);
    context=id;
  }
public:
  BotCommandTrace(uint32_t& context,const BotMessage& message,const uint8_t* key,BotCommandId command,bool retry)
    :context(context),previous(context),message(message),key(key),command(command),retry(retry) {
    context=0;
    if(command!=BOT_COMMAND_PATH)create();
  }
  ~BotCommandTrace(){context=previous;}
  uint32_t historyId() const { return id; }
  void stage(const char* text){create();BotPathLookup::recordCommandStage(id,text);}
};
