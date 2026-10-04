#pragma once
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include <Arduino.h>
#include "MonitorWebServer.h"
#include <ArduinoJson.h>
#include <memory>
#include "RepeaterMonitorCore.h"
#include "MonitorCredentials.h"
#include "BotAdminContacts.h"
#include "BotVoltageList.h"
#include "RepeaterOrder.h"
class MyMesh;
struct ContactInfo;

class RepeaterMonitor {
  MyMesh& mesh;
  MonitorCredentials credentials;
  BotAdminContacts adminContacts;
  bool adminCheckPending = false, adminCheckIsSync = false;
  uint8_t adminCheckRecipient[32]{};
  char adminCheckName[33]{}, adminCheckReply[96]{};
  char adminEditReply[128]{};
  uint32_t adminCheckExpires = 0;
  void pollAdminCheckReply();
  BotVoltageList::Snapshot sunriseNotification;
  bool sunriseNotificationPending = false;
  size_t sunriseRecipient = 0;
  uint32_t sunriseNotificationDue = 0, sunriseNotificationExpires = 0;
  void scheduleSunriseNotification();
  void pollSunriseNotification();
  enum SyncPhase { SyncIdle, SyncLogin, SyncReady, SyncReply, SyncResetReady, SyncRebootWait } syncPhase = SyncIdle;
  int syncTarget = -1;
  bool adminConfirmed[MonitorCore::MAX_REPEATERS]{};
  uint8_t syncResetCount = 0, syncCommandAttempts = 0;
  uint8_t syncLoginAttempt = 0;
  uint32_t syncLoginSentUtc = 0, syncLoginSentMillis = 0;
  uint8_t syncToken = 0;
  bool startSyncLogin();
  bool startClockSync(int target);
  uint32_t syncDeadline = 0, lastNtpSync = 0;
  uint32_t syncCommandSentUtc = 0, syncCommandSentMillis = 0;
  void recordClockSample(uint32_t remoteMinute);
  void chooseSyncAction(int64_t offset);
  char syncPrefix[4]{};
  String syncResult;
  bool busy() const { return running || syncPhase != SyncIdle; }
  bool syncTimeReady() const { return synced && (uint32_t)(millis() - lastNtpSync) < 21600000UL; }
  void pollClockSync();
  void finishClockSync(const char* result);

  MonitorWebServer server{80};
  MonitorCore::Entry entries[MonitorCore::MAX_REPEATERS];
  size_t count = 0;
  uint32_t sequence = 0;
  int savedSlot = -1;
  bool storageOK = true, synced = false, running = false, automatic = false;
  bool mdnsStarted = false, wasConnected = false;
  int active = -1;
  int singleTarget = -1;
  enum Phase { Idle, Next, Login, Status, NeedStatus, Retry, RetryFloodStatus } phase = Idle;
  uint32_t clockSentUtc = 0, clockSentMillis = 0, clockUncertainty = 0;
  int64_t clockOffset = 0;
  bool clockKnown = false;
  uint8_t attempt = 0;
  uint8_t preLoginFloodAttempts = 0;
  uint8_t postLoginStatusAttempts = 0;
  uint32_t deadline = 0, tag = 0, runDay = 0, lastManual = 0;
  uint32_t wifiAttempt = 0, nextPrune = 0;
  uint32_t wifiWindowStart = 0;
  bool wifiStopped = false;
  size_t wifiIndex = 0;
  void connectWifi(size_t index);
  String lastError;
  bool requestIsAdmin = false;
  bool authorized(bool mutation = false);
  void routes();
  void exportState(JsonDocument& doc, bool history, bool ordered = true);
  bool importState(JsonDocument& doc, bool history);
  bool readSlot(int slot, JsonDocument& doc, uint32_t& seq);
  bool save();
  void load();
  void rememberNames();
  uint32_t nextNameCheck = 0;
  struct AdvertClockCandidate { uint8_t key[32]{}; uint32_t remote = 0, received = 0; };
  AdvertClockCandidate advertClocks[MonitorCore::MAX_REPEATERS]{};
  void prune(uint32_t now);
  uint32_t now() const;
  bool startRun(bool scheduled);
  void nextEntry();
  void sendLogin();
  void sendStatus(bool flood = false);
  void failed(uint8_t result);
  void finish(uint8_t result, uint16_t millivolts = 0);
  void protectListedContacts();
  ContactInfo* contact();
  void poll();
public:
  const char* adminPassword(const uint8_t* sender, const char* args);
  const char* adminNotes(const uint8_t* sender, const char* args, BotVoltageList::Snapshot& out);
  const char* adminEditRepeater(const uint8_t* sender, const char* action, const char* query);
  const char* startAdminCheck(const uint8_t* sender, const char* query, bool sync = false);
  bool clockReady() const { return synced; }
  const BotAdminContacts& botAdmins() const { return adminContacts; }
  explicit RepeaterMonitor(MyMesh& m) : mesh(m) {}
  void onVerifiedAdvert(const uint8_t* key, uint32_t timestamp, uint8_t pathLen);
  void voltageList(BotVoltageList::Snapshot& out, bool lowOnly = false);
  void begin();
  void loop();
  bool onCommandResponse(const ContactInfo& from, const char* text);
  bool onResponse(const ContactInfo& from, const uint8_t* data, size_t len);
};
#endif
