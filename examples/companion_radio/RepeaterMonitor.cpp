#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "RepeaterMonitor.h"
#include "RepeaterMonitorPage.h"
#include "MyMesh.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <esp_sntp.h>
#ifndef BOT_MONITOR_PASSWORD
#error Define BOT_MONITOR_PASSWORD in platformio.local.ini for the local dashboard
#endif
#if !defined(BOT_NTP_SSID) || !defined(BOT_NTP_PASSWORD)
#error Define Wi-Fi credentials in platformio.local.ini
#endif
using namespace MonitorCore;
static const char* slots[] = {"/monitor0.json", "/monitor1.json"};
static const struct { const char* ssid; const char* password; } wifiNetworks[] = {
  {BOT_NTP_SSID, BOT_NTP_PASSWORD},
#if defined(BOT_WIFI_SECONDARY_SSID) && defined(BOT_WIFI_SECONDARY_PASSWORD)
  {BOT_WIFI_SECONDARY_SSID, BOT_WIFI_SECONDARY_PASSWORD},
#endif
};
static constexpr size_t wifiNetworkCount = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

uint32_t RepeaterMonitor::now() const {
  return synced ? (uint32_t)time(nullptr) : mesh.getRTCClock()->getCurrentTime();
}

void RepeaterMonitor::exportState(JsonDocument& doc, bool history) {
  doc["version"] = 1;
  JsonArray list = doc["repeaters"].to<JsonArray>();
  uint32_t today = synced && now() >= VALID_TIME ? easternDay(now()) : 0;
  for (size_t i = 0; i < count; ++i) {
    const Entry& e = entries[i];
    char key[65]; formatKey(e.key, key);
    JsonObject item = list.add<JsonObject>();
    item["key"] = key; item["name"] = e.name; item["enabled"] = e.enabled;
    if (!history) continue;
    auto* known = mesh.lookupContactByPubKey(e.key, 32);
    item["displayName"] = e.name[0] ? e.name : (known ? known->name : "");
    item["scheduledDay"] = e.scheduledDay;
    JsonArray records = item["readings"].to<JsonArray>();
    for (const Reading& r : e.readings) {
      if (!r.day || (today && !retained(r.day, today))) continue;
      JsonObject rec = records.add<JsonObject>();
      rec["day"] = r.day; rec["timestamp"] = r.timestamp; rec["result"] = r.result;
      rec["status"] = resultName(r.result);
      if (r.result == Ok) rec["millivolts"] = r.millivolts;
      else rec["millivolts"] = nullptr;
    }
  }
}

bool RepeaterMonitor::importState(JsonDocument& doc, bool history) {
  if (doc["version"].as<int>() != 1 || !doc["repeaters"].is<JsonArray>()) return false;
  JsonArray list = doc["repeaters"];
  if (list.size() > MAX_REPEATERS) return false;
  // Validate in a separate allocation so an invalid edit cannot partly replace the live list.
  std::unique_ptr<Entry[]> candidate(new Entry[MAX_REPEATERS]);
  size_t n = 0;
  for (JsonObject item : list) {
    Entry& e = candidate[n];
    if (!parseKey(item["key"] | "", e.key) || !item["enabled"].is<bool>()) return false;
    const char* name = item["name"] | "";
    if (strlen(name) > 32) return false;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) if (*p < 32 || *p == 127) return false;
    for (size_t j = 0; j < n; ++j) if (memcmp(candidate[j].key, e.key, 32) == 0) return false;
    if (!history) {
      for (size_t j = 0; j < count; ++j) if (memcmp(entries[j].key, e.key, 32) == 0) e = entries[j];
    }
    strlcpy(e.name, name, sizeof(e.name));
    e.enabled = item["enabled"];
    if (history) {
      e.scheduledDay = item["scheduledDay"] | 0UL;
      if (!item["readings"].is<JsonArray>() || item["readings"].size() > 7) return false;
      for (JsonObject rec : item["readings"].as<JsonArray>()) {
        Reading r;
        r.day = rec["day"] | 0UL; r.timestamp = rec["timestamp"] | 0UL;
        r.result = rec["result"] | 255;
        if (!r.day || r.timestamp < VALID_TIME || easternDay(r.timestamp) != r.day || r.result > NoContactSpace) return false;
        if (e.readings[r.day % 7].day) return false;
        if (r.result == Ok) {
          if (!rec["millivolts"].is<uint16_t>()) return false;
          r.millivolts = rec["millivolts"];
        }
        if (r.result == Checking) r.result = Interrupted;
        e.readings[r.day % 7] = r;
      }
    }
    ++n;
  }
  memcpy(entries, candidate.get(), sizeof(entries));
  count = n;
  return true;
}

bool RepeaterMonitor::readSlot(int slot, JsonDocument& doc, uint32_t& seq) {
  File f = SPIFFS.open(slots[slot], "r");
  if (!f || f.size() < 16 || f.size() > 65536) return false;
  uint32_t header[4];
  if (f.read((uint8_t*)header, sizeof(header)) != sizeof(header) || header[0] != 0x4D4F4E31 || header[2] != f.size() - 16) return false;
  String payload = f.readString();
  if (!validSnapshot(header, payload.c_str(), payload.length())) return false;
  if (deserializeJson(doc, payload)) return false;
  seq = header[1];
  return true;
}

bool RepeaterMonitor::save() {
  JsonDocument doc; exportState(doc, true);
  String payload; serializeJson(doc, payload);
  if (doc.overflowed() || payload.length() > 65000) { storageOK = false; return false; }
  int dest = savedSlot == 0 ? 1 : 0;
  uint32_t header[] = {0x4D4F4E31, sequence + 1, (uint32_t)payload.length(), crc32(payload.c_str(), payload.length())};
  File f = SPIFFS.open(slots[dest], "w");
  bool ok = f && f.write((uint8_t*)header, sizeof(header)) == sizeof(header)
      && f.write((const uint8_t*)payload.c_str(), payload.length()) == payload.length();
  f.flush(); f.close();
  JsonDocument check; uint32_t seq = 0;
  ok = ok && readSlot(dest, check, seq) && seq == sequence + 1;
  storageOK = ok;
  if (ok) { savedSlot = dest; sequence = seq; }
  else lastError = "Storage write failed. Polling is paused; export the list before restarting.";
  return ok;
}

void RepeaterMonitor::load() {
  JsonDocument a, b; uint32_t sa = 0, sb = 0;
  bool va = readSlot(0, a, sa), vb = readSlot(1, b, sb);
  int first = vb && (!va || (int32_t)(sb - sa) > 0) ? 1 : 0;
  for (int attemptSlot = 0; attemptSlot < 2; ++attemptSlot) {
    int slot = attemptSlot ? 1-first : first;
    if ((slot ? vb : va) && importState(slot ? b : a, true)) {
      savedSlot = slot; sequence = slot ? sb : sa; return;
    }
  }
  if (SPIFFS.exists(slots[0]) || SPIFFS.exists(slots[1])) {
    storageOK = false;
    lastError = "Saved settings could not be read. Restore a list backup to recover.";
  }
}

void RepeaterMonitor::prune(uint32_t utc) {
  bool changed = false;
  for (size_t i = 0; i < count; ++i) for (Reading& r : entries[i].readings) {
    if (r.day && !retained(r.day, easternDay(utc))) { r = Reading{}; changed = true; }
  }
  if (changed) save();
}

bool RepeaterMonitor::authorized(bool mutation) {
  if (!server.authenticate("admin", BOT_MONITOR_PASSWORD)) {
    server.requestAuthentication(DIGEST_AUTH, "Mesh battery monitor"); return false;
  }
  server.sendHeader("Cache-Control", "no-store");
  if (mutation && server.header("X-Mesh-Monitor") != "1") {
    server.send(403, "text/plain", "Use the dashboard to make changes."); return false;
  }
  return true;
}

void RepeaterMonitor::routes() {
  const char* headers[] = {"X-Mesh-Monitor"}; server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [this]() {
    if (!authorized()) return;
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.send_P(200, "text/html", MONITOR_PAGE);
  });
  server.on("/api/state", HTTP_GET, [this]() {
    if (!authorized()) return;
    JsonDocument doc; exportState(doc, true);
    uint32_t utc = now();
    doc["now"] = utc; doc["timeReady"] = synced; doc["storageOK"] = storageOK;
    doc["running"] = running; doc["active"] = active;
    doc["error"] = lastError; doc["ip"] = WiFi.localIP().toString();
    doc["node"] = mesh.getNodeName();
    doc["manualReady"] = !lastManual || (uint32_t)(millis() - lastManual) >= 600000;
    if (synced) {
      uint32_t day = easternDay(utc);
      doc["today"] = day;
      doc["sunrise"] = sunrise(day);
      bool unfinished = false;
      for (size_t i = 0; i < count; ++i) if (entries[i].enabled && entries[i].scheduledDay < day) unfinished = true;
      doc["nextSunrise"] = sunrise(day + ((utc >= sunrise(day) && !unfinished) ? 1 : 0));
    }
    String json; serializeJson(doc, json); server.send(200, "application/json", json);
  });
  server.on("/api/list", HTTP_GET, [this]() {
    if (!authorized()) return;
    JsonDocument doc; exportState(doc, false);
    String json; serializeJsonPretty(doc, json);
    server.sendHeader("Content-Disposition", "attachment; filename=repeaters.json");
    server.send(200, "application/json", json);
  });
  server.on("/api/list", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (running) { server.send(409, "text/plain", "Wait for the current check to finish."); return; }
    String body = server.arg("plain");
    JsonDocument doc;
    if (body.length() > 8192 || deserializeJson(doc, body)) { server.send(400, "text/plain", "Invalid list file."); return; }
    std::unique_ptr<Entry[]> backup(new Entry[MAX_REPEATERS]);
    memcpy(backup.get(), entries, sizeof(entries)); size_t oldCount = count;
    if (!importState(doc, false)) { server.send(400, "text/plain", "Use unique 64-digit public keys, names up to 32 bytes, and at most 32 repeaters."); return; }
    if (!save()) {
      memcpy(entries, backup.get(), sizeof(entries)); count = oldCount;
      server.send(507, "text/plain", "Could not save settings."); return;
    }
    lastError = ""; server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/check", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (!synced || !storageOK || running || (lastManual && millis() - lastManual < 600000)) {
      server.send(409, "text/plain", "Wait for time sync, storage recovery, or the 10-minute check cooldown."); return;
    }
    if (!startRun(false)) { server.send(409, "text/plain", "Enable at least one repeater first."); return; }
    lastManual = millis(); server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/check-one", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (!synced || !storageOK || running) {
      server.send(409, "text/plain", "Wait for time sync, storage recovery, or the current check to finish."); return;
    }
    String body = server.arg("plain"); JsonDocument doc; uint8_t key[32];
    if (body.length() > 256 || deserializeJson(doc, body) ||
        !doc["key"].is<const char*>() || !parseKey(doc["key"].as<const char*>(), key)) {
      server.send(400, "text/plain", "Provide a saved repeater's full public key."); return;
    }
    int target = -1;
    for (size_t i = 0; i < count; ++i) if (memcmp(entries[i].key, key, 32) == 0) { target = (int)i; break; }
    if (target < 0) { server.send(404, "text/plain", "Repeater is not in the saved list."); return; }
    singleTarget = target; running = true; automatic = false;
    runDay = easternDay(now()); active = -1; phase = Next; deadline = millis();
    // Individual checks neither observe nor restart the full-list cooldown.
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.onNotFound([this]() { server.send(404, "text/plain", "Not found"); });
}

void RepeaterMonitor::connectWifi(size_t index) {
  wifiIndex = index;
  WiFi.disconnect();
  WiFi.begin(wifiNetworks[index].ssid, wifiNetworks[index].password);
  wifiAttempt = millis();
}

void RepeaterMonitor::begin() {
  load(); mesh.setRepeaterMonitor(this);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); connectWifi(0);
  wifiAttempt = wifiWindowStart = millis(); wifiStopped = false; board.setInhibitSleep(true);
  routes(); server.begin();
}

bool RepeaterMonitor::startRun(bool scheduled) {
  bool any = false;
  for (size_t i = 0; i < count; ++i) if (entries[i].enabled && (!scheduled || due(entries[i], now()))) any = true;
  if (!any) return false;
  singleTarget = -1;
  running = true; automatic = scheduled; runDay = easternDay(now()); active = -1;
  phase = Next; deadline = millis(); return true;
}

ContactInfo* RepeaterMonitor::contact() {
  Entry& e = entries[active];
  auto* c = mesh.lookupContactByPubKey(e.key, 32);
  if (!c) {
    ContactInfo temp{}; memcpy(temp.id.pub_key, e.key, 32);
    temp.type = ADV_TYPE_REPEATER; temp.out_path_len = OUT_PATH_UNKNOWN;
    char key[65]; formatKey(e.key, key);
    strlcpy(temp.name, e.name[0] ? e.name : key, sizeof(temp.name));
    if (!mesh.addContact(temp)) return nullptr;
    c = mesh.lookupContactByPubKey(e.key, 32);
  }
  return c;
}

void RepeaterMonitor::nextEntry() {
  if (easternDay(now()) != runDay) { running = false; phase = Idle; active = -1; return; }
  if (singleTarget >= 0) active = singleTarget;
  else do { ++active; } while (active < (int)count && (!entries[active].enabled || (automatic && entries[active].scheduledDay >= runDay)));
  if (active >= (int)count) { running = false; phase = Idle; active = -1; return; }
  attempt = 0;
  reuseFloodSent = false;
  if (automatic || now() >= sunrise(runDay)) entries[active].scheduledDay = runDay;
  Reading& r = entries[active].readings[runDay % 7];
  r = Reading{}; r.day = runDay; r.timestamp = now(); r.result = Checking;
  // Persist the start before transmitting: reboot cannot repeat an already-started daily check.
  if (!save()) { running = false; phase = Idle; return; }
  // Reuse the repeater's existing authorization for this node when possible.
  // Try the saved route, then flood, before either of the two flood logins.
  sendStatus();
}

void RepeaterMonitor::sendLogin() {
  ++attempt;
  auto* c = contact();
  if (!c) { finish(NoContactSpace); return; }
  uint32_t estimate = 0;
  ContactInfo target = *c;
  target.out_path_len = OUT_PATH_UNKNOWN; // both login attempts rediscover the route
  if (mesh.sendLogin(target, "", estimate) == MSG_SEND_FAILED) { failed(SendFailed); return; }
  phase = Login; deadline = millis() + timeout(estimate);
}

void RepeaterMonitor::sendStatus(bool flood) {
  auto* c = contact(); uint32_t estimate = 0;
  if (!c) { finish(NoContactSpace); return; }
  ContactInfo target = *c;
  if (flood) target.out_path_len = OUT_PATH_UNKNOWN;
  if (attempt == 0 && target.out_path_len == OUT_PATH_UNKNOWN) reuseFloodSent = true;
  if (mesh.sendRequest(target, REQ_TYPE_GET_STATUS, tag, estimate) == MSG_SEND_FAILED) { failed(SendFailed); return; }
  phase = Status; deadline = millis() + timeout(estimate);
}

void RepeaterMonitor::failed(uint8_t result) {
  if (attempt == 0 && !reuseFloodSent) { phase = RetryFloodStatus; deadline = millis() + 15000; }
  else if (attempt < 2) { phase = Retry; deadline = millis() + 15000; }
  else finish(result);
}

void RepeaterMonitor::finish(uint8_t result, uint16_t mv) {
  Reading& r = entries[active].readings[runDay % 7];
  r.result = result; r.millivolts = result == Ok ? mv : 0;
  // Keep a reading in the calendar day when measured, even if a manual run crossed midnight.
  uint32_t measuredDay = easternDay(now());
  if (measuredDay != runDay) { r = Reading{}; }
  Reading& measured = entries[active].readings[measuredDay % 7];
  measured.day = measuredDay; measured.timestamp = now(); measured.result = result;
  measured.millivolts = result == Ok ? mv : 0;
  if (!save()) { running = false; phase = Idle; return; }
  if (singleTarget >= 0) { running = false; phase = Idle; active = -1; singleTarget = -1; }
  else { phase = Next; deadline = millis() + 10000; }
}

bool RepeaterMonitor::onResponse(const ContactInfo& from, const uint8_t* data, size_t len) {
  if (!running || active < 0 || active >= (int)count || memcmp(from.id.pub_key, entries[active].key, 32) != 0) return false;
  if (phase == Login && loginOK(data, len)) {
    phase = NeedStatus; deadline = millis() + 3000; return true;
  }
  if (phase == Status) {
    uint16_t mv;
    if (voltage(data, len, tag, mv)) { finish(Ok, mv); return true; }
  }
  return false;
}

void RepeaterMonitor::poll() {
  if (!synced || !storageOK) return;
  if (!running) {
    for (size_t i = 0; i < count; ++i) if (due(entries[i], now())) { startRun(true); break; }
  }
  if (!running || !elapsed(millis(), deadline)) return;
  switch (phase) {
    case Next: nextEntry(); break;
    case NeedStatus: sendStatus(); break;
    case RetryFloodStatus: sendStatus(true); break;
    case Retry: sendLogin(); break;
    case Login: failed(LoginFailed); break;
    case Status: failed(NoResponse); break;
    default: break;
  }
}

void RepeaterMonitor::loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    if (!mdnsStarted) { mdnsStarted = MDNS.begin("mesh-bot"); if (mdnsStarted) MDNS.addService("http", "tcp", 80); }
    // USB carries the companion binary protocol; do not inject dashboard logs into it.
  }
  // Each outage gets a fresh ten-minute window. Stop until reboot if exhausted.
  // Unsigned subtraction also handles millis() rollover.
  if (!connected && wasConnected) {
    wifiWindowStart = millis(); wifiStopped = false; connectWifi(0);
  } else if (!connected && !wifiStopped) {
    if (millis() - wifiWindowStart >= 600000UL) {
      wifiStopped = true;
      WiFi.disconnect(true); // stop the station; automatic reconnect is disabled
    } else if (millis() - wifiAttempt >= 30000) {
      connectWifi((wifiIndex + 1) % wifiNetworkCount);
    }
  }
  wasConnected = connected;
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && time(nullptr) >= VALID_TIME) {
    mesh.getRTCClock()->setCurrentTime((uint32_t)time(nullptr)); synced = true;
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  }
  server.handleClient();
  if (synced && elapsed(millis(), nextPrune)) { prune(now()); nextPrune = millis() + 60000; }
  poll();
}
#endif
