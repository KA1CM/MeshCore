from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
src = repo / 'examples/companion_radio'
mesh = (src / 'MyMesh.cpp').read_text(encoding='utf-8')
handler = mesh[mesh.index('bool MyMesh::handleBotAdminCommand('):mesh.index('void MyMesh::recordBotObservation(')]
completion = mesh[mesh.index('void MyMesh::completeAdminAdvert('):mesh.index('void MyMesh::resolveLocalPathNames(')]
harness = r'''
#include "FirmwareBot.h"
#include "BotAdminHelp.h"
#include "BotVoltageList.h"
#include "BotCommandRegistry.h"
#include <cassert>
#include <cstring>
#include <string>
#define ESP32 1
#define BOT_REPEATER_MONITOR 1
#define BOT_COMMAND_COOLDOWN_SLOTS 9
#define PUB_KEY_SIZE 32
#define MAX_HASH_SIZE 8
#define PAYLOAD_TYPE_ADVERT 4
namespace mesh {struct Packet {uint8_t payload[32]{};int payload_len=32;int getPayloadType(){return 4;}uint8_t id=1;void calculatePacketHash(uint8_t* h){memset(h,id,8);}};}
struct BaseChatMesh {void logTx(mesh::Packet*,int){} void logTxFail(mesh::Packet*,int){}};
struct ContactInfo { struct { uint8_t pub_key[32]{}; } id; };
struct BotAdminContacts {
 enum Permission { Commands = 1, Notifications = 2 };
 uint8_t key[32]{}; unsigned permissions = Commands;
 bool allows(const uint8_t* k, Permission p) const { return !memcmp(k,key,32) && (permissions&p); }
};
struct Monitor { const char* adminPassword(const uint8_t*,const char*){return "Password saved";} const char* adminNotes(const uint8_t*,const char*,BotVoltageList::Snapshot&){return "Notes saved";} int edits=0;const char* adminEditRepeater(const uint8_t*,const char*,const char*){++edits;return "List updated";} int checks=0;const char* startAdminCheck(const uint8_t*,const char*,bool=false){++checks;return nullptr;} BotAdminContacts admins; BotAdminContacts& botAdmins() { return admins; } };
struct Clock { uint32_t now=10000; uint32_t getMillis() { return now; } };
struct MyMesh : BaseChatMesh {
 struct {bool active=false;uint8_t result=0,key[32]{},hash[8]{};mesh::Packet* packet=nullptr;uint32_t deadline=0;} pending_admin_advert;
 mesh::Packet packet;ContactInfo recipient;
 uint32_t last_bot_advert_time=0;struct {uint8_t pub_key[32]{};}self_id;
 struct RTC {uint32_t getCurrentTime(){return 2000000000;}}rtc;RTC* getRTCClock(){return &rtc;}
 struct {bool enabled=true;}bot_prefs;
 bool millisHasNowPassed(uint32_t t){return (int32_t)(clock.now-t)>0;}
 ContactInfo* lookupContactByPubKey(const uint8_t*,size_t){return &recipient;}
 void completeAdminAdvert(mesh::Packet*,bool);void logTx(mesh::Packet*,int);void logTxFail(mesh::Packet*,int);void pollAdminAdvert();
 Monitor monitor; Monitor* repeaterMonitor=&monitor;
 Clock clock; Clock* _ms=&clock;
 struct {void accepted(BotCommandId,uint32_t){}}bot_stats_window;
 BotStats bot_stats{}; BotCommandCooldown bot_command_cooldowns[BOT_COMMAND_COOLDOWN_SLOTS]{};
 struct Pending {bool active=false,notification=false,adminOnly=false;BotVoltageList::Snapshot snapshot;BotCommandId command{};BotChannelKind kind{};uint8_t channel=0,key[32]{};size_t next=0;unsigned part=0,failures=0;uint32_t deadline=0,expires=0;} pending_voltage_list,pending_bot_dm_ack;
 struct {unsigned count=0;void accepted(const BotMessage&,uint32_t){++count;}}bot_top_users;
 void recordBotCommandStats(BotCommandId){}
 bool queue_ok=true,reply_ok=true; unsigned adverts=0,replies=0; std::string reply;
 bool sendBotSelfAdvert(bool flood,bool track=false) { assert(flood&&track); ++adverts;pending_admin_advert.packet=&packet;packet.calculatePacketHash(pending_admin_advert.hash); return queue_ok; }
 bool sendBotResponse(const BotMessage& m,const ContactInfo* c,uint8_t channel,const char* text,size_t len) {
  assert(m.channel_kind==BOT_CHANNEL_DM && c && channel==0xFF); ++replies; reply.assign(text,len); return reply_ok;
 }
 bool handleBotAdminCommand(const BotMessage&,const ContactInfo*,const BotCommand&);
};
''' + handler + completion + r'''
int main() {
 BotCommand command{};
 for(const char* text : {"advert","ADVERT","!advert","/advert"}) {
  assert(FirmwareBot::parseCommand(text,strlen(text),&command,true));
  assert(command.id==BOT_COMMAND_ADVERT && !command.args_len);
 }
 assert(!FirmwareBot::parseCommand("advert",6,&command,false));
 assert(!BotCommandRegistry::isDiscoverable(BOT_COMMAND_ADVERT));
 assert(FirmwareBot::parseCommand("advert",6,&command,true));
 BotMessage message{}; message.channel_kind=BOT_CHANNEL_DM; ContactInfo admin;
 admin.id.pub_key[0]=7;
 auto prepare=[&](MyMesh& m){memcpy(m.monitor.admins.key,admin.id.pub_key,32);};
 MyMesh ok;prepare(ok);assert(ok.handleBotAdminCommand(message,&admin,command));
 assert(ok.adverts==1 && ok.replies==0 && ok.bot_top_users.count==1);
 ok.pollAdminAdvert();assert(ok.replies==0);
 mesh::Packet unrelated;ok.logTx(&unrelated,20);ok.pollAdminAdvert();assert(ok.replies==0);
 ok.packet.id=2;ok.logTx(&ok.packet,20);assert(ok.pending_admin_advert.result==0);ok.packet.id=1;
 ok.logTx(&ok.packet,20);assert(ok.last_bot_advert_time==2000000000);ok.pending_bot_dm_ack.active=true;ok.pollAdminAdvert();assert(ok.replies==0);
 ok.pending_bot_dm_ack.active=false;ok.pollAdminAdvert();assert(ok.replies==1&&ok.reply=="Advert sent");
 ok.pollAdminAdvert();assert(ok.replies==1);
 assert(ok.handleBotAdminCommand(message,&admin,command));assert(ok.adverts==1 && ok.replies==1);
 for(unsigned i=0;i<8;++i) {
  MyMesh m;prepare(m);BotMessage input=message;ContactInfo sender=admin;const ContactInfo* contact=&sender;
  if(i==0) input.channel_kind=BOT_CHANNEL_BOT;
  if(i==1) sender.id.pub_key[31]^=1; // Same prefix, different full key.
  if(i==2) m.monitor.admins.permissions=BotAdminContacts::Notifications;
  if(i==3) contact=nullptr;
  if(i==4) m.repeaterMonitor=nullptr;
  if(i==5) input.text_truncated=true;
  if(i==6) m.pending_voltage_list.active=true;
  if(i==7) m.pending_bot_dm_ack.active=true;
  assert(m.handleBotAdminCommand(input,contact,command));assert(!m.adverts && !m.replies);
 }
 MyMesh args;prepare(args);command.args_len=1;
 assert(args.handleBotAdminCommand(message,&admin,command));assert(!args.adverts && args.reply=="Usage: advert");
 command.args_len=0;
 MyMesh failed;prepare(failed);failed.queue_ok=false;
 assert(failed.handleBotAdminCommand(message,&admin,command));assert(failed.reply=="Advert failed" && failed.bot_stats.send_failures==1);
 MyMesh replyFailed;prepare(replyFailed);replyFailed.reply_ok=false;
 assert(replyFailed.handleBotAdminCommand(message,&admin,command));replyFailed.logTx(&replyFailed.packet,20);replyFailed.pollAdminAdvert();assert(replyFailed.adverts==1 && replyFailed.bot_stats.send_failures==1);
 MyMesh txfail;prepare(txfail);txfail.handleBotAdminCommand(message,&admin,command);txfail.logTxFail(&txfail.packet,20);txfail.pollAdminAdvert();assert(txfail.reply=="Advert failed");
 MyMesh timeout;prepare(timeout);timeout.handleBotAdminCommand(message,&admin,command);timeout.clock.now+=600001;timeout.pollAdminAdvert();assert(timeout.reply=="Advert transmission not confirmed");
 MyMesh revoked;prepare(revoked);revoked.handleBotAdminCommand(message,&admin,command);revoked.monitor.admins.permissions=0;revoked.logTx(&revoked.packet,20);revoked.pollAdminAdvert();assert(!revoked.replies&&!revoked.pending_admin_advert.active);
 command.id=BOT_COMMAND_HELP;command.args_len=0;
 MyMesh help;prepare(help);assert(help.handleBotAdminCommand(message,&admin,command));assert(help.reply=="Admin help:\nadvert check sync add remove enable disable notes password"&&!help.adverts);
 MyMesh guestHelp;prepare(guestHelp);guestHelp.monitor.admins.permissions=BotAdminContacts::Notifications;
 assert(!guestHelp.handleBotAdminCommand(message,&admin,command)&&!guestHelp.replies);
 MyMesh channelHelp;prepare(channelHelp);BotMessage channel=message;channel.channel_kind=BOT_CHANNEL_BOT;
 assert(!channelHelp.handleBotAdminCommand(channel,&admin,command));
 for(const char* topic : {"advert","check","sync","add","remove","enable","disable","notes","password"}) {
  std::string input=std::string("help ")+topic;
  assert(FirmwareBot::parseCommand(input.c_str(),input.size(),&command,true));
  MyMesh detail;prepare(detail);
  assert(detail.handleBotAdminCommand(message,&admin,command));
  assert(detail.reply==BotAdminHelp::reply(topic)&&detail.reply.size()<=120);
  if (!strcmp(topic,"notes")) assert(detail.reply.find("View: notes <rpt>")!=std::string::npos && detail.reply.find("Edit field:")!=std::string::npos);
  else assert(detail.reply.find("Usage:")!=std::string::npos);
  assert(!detail.adverts&&!detail.monitor.checks&&!detail.monitor.edits);
  MyMesh deniedDetail;prepare(deniedDetail);deniedDetail.monitor.admins.permissions=0;
  assert(!deniedDetail.handleBotAdminCommand(message,&admin,command)&&!deniedDetail.replies);
  BotMessage publicMessage=message;publicMessage.channel_kind=BOT_CHANNEL_PUBLIC;
  assert(!detail.handleBotAdminCommand(publicMessage,&admin,command));
 }
 assert(FirmwareBot::parseCommand("help list",9,&command,true));
 MyMesh normalHelp;prepare(normalHelp);assert(!normalHelp.handleBotAdminCommand(message,&admin,command));
 assert(!strcmp(BotAdminHelp::reply("NOTES"),BotAdminHelp::reply("notes")));

 assert(FirmwareBot::parseCommand("check Chestnut",14,&command,true)&&command.id==BOT_COMMAND_CHECK);
 MyMesh check;prepare(check);check.handleBotAdminCommand(message,&admin,command);assert(check.monitor.checks==1&&check.replies==1&&!check.adverts&&check.reply=="Checking... I will send the result when finished");
 MyMesh denied;prepare(denied);denied.monitor.admins.permissions=0;denied.handleBotAdminCommand(message,&admin,command);assert(!denied.monitor.checks&&!denied.replies);
 assert(FirmwareBot::parseCommand("sync Chestnut",13,&command,true)&&command.id==BOT_COMMAND_SYNC);
 MyMesh sync;prepare(sync);sync.handleBotAdminCommand(message,&admin,command);assert(sync.monitor.checks==1&&sync.reply=="Syncing... I will send the result when finished"&&!sync.adverts);
 MyMesh noSync;prepare(noSync);noSync.monitor.admins.permissions=BotAdminContacts::Notifications;noSync.handleBotAdminCommand(message,&admin,command);assert(!noSync.monitor.checks&&!noSync.replies);
 for(const char* text : {"add abc", "remove Hill", "enable Hill", "disable Hill"}) {
   assert(FirmwareBot::parseCommand(text,strlen(text),&command,true));
   MyMesh edit;prepare(edit);message.channel_kind=BOT_CHANNEL_DM;
   edit.handleBotAdminCommand(message,&admin,command);assert(edit.monitor.edits==1&&edit.reply=="List updated");
   MyMesh guest;prepare(guest);guest.monitor.admins.permissions=0;
   guest.handleBotAdminCommand(message,&admin,command);assert(!guest.monitor.edits&&!guest.replies);
   MyMesh channel;prepare(channel);message.channel_kind=BOT_CHANNEL_PUBLIC;
   channel.handleBotAdminCommand(message,&admin,command);assert(!channel.monitor.edits&&!channel.replies);
 }
 message.channel_kind=BOT_CHANNEL_DM;
 command.id=BOT_COMMAND_PING;MyMesh normal;
 assert(!normal.handleBotAdminCommand(message,&admin,command));assert(!normal.adverts && !normal.replies);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)
    (path/'test.cpp').write_text(harness,encoding='utf-8')
    exe=path/'test.exe'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-I',str(src),str(path/'test.cpp'),str(src/'FirmwareBot.cpp'),str(src/'BotCommandRegistry.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: advert parsing, full-key admin DM authorization, permission rejection, cooldown, busy state, arguments and send failures')
