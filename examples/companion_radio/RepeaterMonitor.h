#pragma once
#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <memory>
#include "RepeaterMonitorCore.h"
class MyMesh;
struct ContactInfo;

class RepeaterMonitor {
  MyMesh& mesh;
  WebServer server{80};
  MonitorCore::Entry entries[MonitorCore::MAX_REPEATERS];
  size_t count = 0;
  uint32_t sequence = 0;
  int savedSlot = -1;
  bool storageOK = true, synced = false, running = false, automatic = false;
  bool mdnsStarted = false, wasConnected = false;
  int active = -1;
  int singleTarget = -1;
  enum Phase { Idle, Next, Login, Status, NeedStatus, Retry, RetryFloodStatus } phase = Idle;
  uint8_t attempt = 0;
  bool reuseFloodSent = false;
  uint32_t deadline = 0, tag = 0, runDay = 0, lastManual = 0;
  uint32_t wifiAttempt = 0, nextPrune = 0;
  uint32_t wifiWindowStart = 0;
  bool wifiStopped = false;
  size_t wifiIndex = 0;
  void connectWifi(size_t index);
  String lastError;
  bool authorized(bool mutation = false);
  void routes();
  void exportState(JsonDocument& doc, bool history);
  bool importState(JsonDocument& doc, bool history);
  bool readSlot(int slot, JsonDocument& doc, uint32_t& seq);
  bool save();
  void load();
  void prune(uint32_t now);
  uint32_t now() const;
  bool startRun(bool scheduled);
  void nextEntry();
  void sendLogin();
  void sendStatus(bool flood = false);
  void failed(uint8_t result);
  void finish(uint8_t result, uint16_t millivolts = 0);
  ContactInfo* contact();
  void poll();
public:
  explicit RepeaterMonitor(MyMesh& m) : mesh(m) {}
  void begin();
  void loop();
  bool onResponse(const ContactInfo& from, const uint8_t* data, size_t len);
};
#endif
