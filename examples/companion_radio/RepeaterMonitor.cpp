#if defined(ESP32) && defined(BOT_REPEATER_MONITOR)
#include "RepeaterMonitor.h"
#include "RepeaterMonitorPage.h"
#include "MyMesh.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <esp_sntp.h>
#if __has_include("../../out/monitor-secrets.h")
#include "../../out/monitor-secrets.h"
#endif
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
    auto* known = mesh.lookupContactByPubKey(e.key, 32);
    const char* displayName = known && known->name[0] ? known->name : e.learnedName[0] ? e.learnedName : e.name;
    if (!history) {
      // Export the same name shown in Battery readings, including learned contact names.
      item["name"] = displayName;
      continue;
    }
    item["displayName"] = displayName;
    item["learnedName"] = known && known->name[0] ? known->name : e.learnedName;
    item["scheduledDay"] = e.scheduledDay;
    item["lastSynced"] = e.lastSynced;
    if (e.clockCheckedAt) {
      item["clockCheckedAt"] = e.clockCheckedAt;
      item["clockOffset"] = e.clockOffset;
      item["clockUncertainty"] = e.clockUncertainty;
    }
    JsonArray records = item["readings"].to<JsonArray>();
    for (const Reading& r : e.readings) {
      if (!r.day || (today && !retained(r.day, today))) continue;
      JsonObject rec = records.add<JsonObject>();
      rec["day"] = r.day; rec["timestamp"] = r.timestamp; rec["result"] = r.result;
      rec["status"] = resultName(r.result);
      rec["clockKnown"] = r.clockKnown;
      if (r.clockKnown) { rec["clockOffset"] = r.clockOffset; rec["clockUncertainty"] = r.clockUncertainty; }
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
      // Migrate older snapshots which saved displayName but never restored it.
      const char* learned = item["learnedName"] | "";
      if (item["learnedName"].isNull()) {
        const char* displayed = item["displayName"] | "";
        if (strcmp(displayed, name)) learned = displayed;
      }
      if (strlen(learned) > 32) return false;
      for (const unsigned char* p = (const unsigned char*)learned; *p; ++p) if (*p < 32 || *p == 127) return false;
      strlcpy(e.learnedName, learned, sizeof(e.learnedName));
      e.scheduledDay = item["scheduledDay"] | 0UL;
      if (!item["lastSynced"].isNull() && !item["lastSynced"].is<uint32_t>()) return false;
      e.lastSynced = item["lastSynced"] | 0UL;
      if (e.lastSynced && e.lastSynced < VALID_TIME) return false;
      if (!item["clockCheckedAt"].isNull()) {
        if (!item["clockCheckedAt"].is<uint32_t>() || !item["clockOffset"].is<int64_t>() ||
            !item["clockUncertainty"].is<uint32_t>()) return false;
        e.clockCheckedAt = item["clockCheckedAt"];
        if (e.clockCheckedAt < VALID_TIME) return false;
        e.clockOffset = item["clockOffset"].as<int64_t>();
        e.clockUncertainty = item["clockUncertainty"].as<uint32_t>();
      }
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
        if (rec["clockKnown"] | false) {
          if (!rec["clockOffset"].is<int64_t>() || !rec["clockUncertainty"].is<uint32_t>()) return false;
          r.clockKnown = true; r.clockOffset = rec["clockOffset"].as<int64_t>();
          r.clockUncertainty = rec["clockUncertainty"].as<uint32_t>();
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

void RepeaterMonitor::rememberNames() {
  if (!storageOK) return;
  bool changed = false;
  for (size_t i = 0; i < count; ++i) {
    auto* known = mesh.lookupContactByPubKey(entries[i].key, 32);
    if (known && known->name[0] && strcmp(known->name, entries[i].learnedName)) {
      strlcpy(entries[i].learnedName, known->name, sizeof(entries[i].learnedName));
      changed = true;
    }
  }
  if (changed) save();
}

void RepeaterMonitor::prune(uint32_t utc) {
  bool changed = false;
  for (size_t i = 0; i < count; ++i) for (Reading& r : entries[i].readings) {
    if (r.day && !retained(r.day, easternDay(utc))) { r = Reading{}; changed = true; }
  }
  if (changed) save();
}

bool RepeaterMonitor::authorized(bool mutation) {
  requestIsAdmin = server.authenticate("admin", BOT_MONITOR_PASSWORD);
  bool guest = false;
#ifdef BOT_MONITOR_GUEST_PASSWORD
  if (!requestIsAdmin) guest = server.authenticate("gu3st", BOT_MONITOR_GUEST_PASSWORD);
#endif
  if (!requestIsAdmin && !guest) {
    server.requestAuthentication(DIGEST_AUTH, "Mesh battery monitor"); return false;
  }
  server.sendHeader("Cache-Control", "no-store");
  if (mutation && !requestIsAdmin) {
    server.send(403, "text/plain", "Guest access is read-only."); return false;
  }
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
    doc["canManage"] = requestIsAdmin;
    doc["clockSyncRunning"] = syncPhase != SyncIdle;
    doc["clockSyncResult"] = syncResult;
    if (syncTarget >= 0 && syncTarget < (int)count) {
      char key[65]; formatKey(entries[syncTarget].key, key); doc["clockSyncKey"] = key;
    }
    if (requestIsAdmin) {
      doc["credentialsReady"] = credentials.available();
      doc["clockSyncReady"] = syncTimeReady();
      for (size_t i = 0; i < count; ++i)
        doc["repeaters"][i]["passwordConfigured"] = credentials.password(entries[i].key)[0] != 0;
    }
    doc["now"] = utc; doc["timeReady"] = synced; doc["storageOK"] = storageOK;
    doc["running"] = running; doc["active"] = active;
    doc["error"] = lastError; doc["ip"] = WiFi.localIP().toString();
    doc["node"] = mesh.getNodeName();
    JsonArray neighbors = doc["neighbors"].to<JsonArray>();
#if CMESH_BOT_ENABLED
    for (size_t i = 0; i < BOT_NEIGHBOR_SLOTS; ++i) {
      BotNeighbor neighbor{}; uint32_t ageSeconds = 0;
      if (!mesh.getBotNeighbor(i, neighbor, ageSeconds)) continue;
      char prefix[BOT_SENDER_KEY_PREFIX_LEN * 2 + 1];
      static const char hex[] = "0123456789abcdef";
      for (size_t j = 0; j < BOT_SENDER_KEY_PREFIX_LEN; ++j) {
        prefix[j * 2] = hex[neighbor.pub_key_prefix[j] >> 4];
        prefix[j * 2 + 1] = hex[neighbor.pub_key_prefix[j] & 15];
      }
      prefix[sizeof(prefix) - 1] = 0;
      auto* contact = mesh.lookupContactByPubKey(neighbor.pub_key_prefix, BOT_SENDER_KEY_PREFIX_LEN);
      JsonObject item = neighbors.add<JsonObject>();
      item["keyPrefix"] = prefix;
      const char* neighborName = contact && contact->name[0] ? contact->name : "";
      if (!neighborName[0]) {
        const Entry* matched = nullptr;
        bool ambiguous = false;
        for (size_t j = 0; j < count; ++j) {
          if (memcmp(entries[j].key, neighbor.pub_key_prefix, BOT_SENDER_KEY_PREFIX_LEN)) continue;
          if (matched) { ambiguous = true; break; }
          matched = &entries[j];
        }
        if (matched && !ambiguous)
          neighborName = matched->learnedName[0] ? matched->learnedName : matched->name;
      }
      item["name"] = neighborName;
      item["rssi"] = neighbor.rssi_dbm;
      item["snr"] = neighbor.snr_quarters / 4.0;
      item["samples"] = neighbor.sample_count;
      item["ageSeconds"] = ageSeconds;
    }
#endif
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
    if (busy()) { server.send(409, "text/plain", "Wait for the current check to finish."); return; }
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
    lastError = "";
    syncTarget = -1; syncResult = "";
    memset(adminConfirmed, 0, sizeof(adminConfirmed));
    if (!credentials.retain(entries, count)) lastError = "Private credential storage unavailable.";
    protectListedContacts();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/check", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (!synced || !storageOK || busy() || (lastManual && millis() - lastManual < 600000)) {
      server.send(409, "text/plain", "Wait for time sync, storage recovery, or the 10-minute check cooldown."); return;
    }
    if (!startRun(false)) { server.send(409, "text/plain", "Enable at least one repeater first."); return; }
    lastManual = millis(); server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/check-one", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (!synced || !storageOK || busy()) {
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
  server.on("/api/password", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (busy() || !storageOK || !credentials.available()) {
      server.send(409, "text/plain", "Wait for the current operation or recover storage."); return;
    }
    String body = server.arg("plain"); JsonDocument doc; uint8_t key[32];
    if (body.length() > 512 || deserializeJson(doc, body) || !doc["password"].is<const char*>()) {
      server.send(400, "text/plain", "Invalid password request."); return;
    }
    JsonString password = doc["password"].as<JsonString>();
    bool all = doc["all"].is<bool>() && doc["all"].as<bool>();
    if (password.size() > 15 || strlen(password.c_str()) != password.size() ||
        (all && !doc["key"].isNull()) || (!all && !parseKey(doc["key"] | "", key))) {
      server.send(400, "text/plain", "Use a saved key or all repeaters, and a password up to 15 bytes."); return;
    }
    bool known = all && count > 0;
    if (!all) for (size_t i = 0; i < count; ++i) known |= memcmp(entries[i].key, key, 32) == 0;
    if (!known) { server.send(404, "text/plain", "Repeater is not in the saved list."); return; }
    if (!credentials.set(entries, count, all ? nullptr : key, password.c_str())) {
      server.send(507, "text/plain", "Could not save private credentials."); return;
    }
    for (size_t i = 0; i < count; ++i)
      if (all || !memcmp(entries[i].key, key, 32)) adminConfirmed[i] = false;
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/sync-time", HTTP_POST, [this]() {
    if (!authorized(true)) return;
    if (busy() || !storageOK || !credentials.available() || !syncTimeReady()) {
      server.send(409, "text/plain", "Wait for a recent NTP sync, working storage, and an idle monitor."); return;
    }
    String body = server.arg("plain"); JsonDocument doc; uint8_t key[32];
    if (body.length() > 256 || deserializeJson(doc, body) || !parseKey(doc["key"] | "", key)) {
      server.send(400, "text/plain", "Provide a saved repeater's full public key."); return;
    }
    int target = -1;
    for (size_t i = 0; i < count; ++i) if (!memcmp(entries[i].key, key, 32)) target = (int)i;
    if (target < 0) { server.send(404, "text/plain", "Repeater is not in the saved list."); return; }
    auto* c = mesh.lookupContactByPubKey(key, 32);
    if (!c || !credentials.password(key)[0]) {
      server.send(409, "text/plain", "A saved password and repeater contact are required."); return;
    }
    if (!startClockSync(target)) {
      server.send(503, "text/plain", "Could not start clock sync."); return;
    }
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
  if (storageOK) {
#ifdef BOT_REPEATER_INITIAL_PASSWORD
    const char* initial = BOT_REPEATER_INITIAL_PASSWORD;
#else
    const char* initial = "";
#endif
    if (!credentials.begin(entries, count, initial)) lastError = "Private credential storage unavailable.";
  }
  protectListedContacts();
  bool restoredNames = false;
  for (size_t i = 0; i < count; ++i) restoredNames |= mesh.restoreMonitorContactName(entries[i].key);
  if (restoredNames) mesh.saveMonitorContacts();
  rememberNames();
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

void RepeaterMonitor::protectListedContacts() {
  bool changed = false;
  // Protect all existing list members before adding any missing contacts: adding can evict.
  for (size_t i = 0; i < count; ++i) {
    auto* c = mesh.lookupContactByPubKey(entries[i].key, 32);
    if (c && !(c->flags & 0x01)) {
      c->flags |= 0x01;
      c->lastmod = mesh.getRTCClock()->getCurrentTime();
      changed = true;
    }
  }
  for (size_t i = 0; i < count; ++i) {
    const Entry& e = entries[i];
    if (mesh.lookupContactByPubKey(e.key, 32)) continue;
    ContactInfo temp{};
    memcpy(temp.id.pub_key, e.key, 32);
    temp.type = ADV_TYPE_REPEATER; temp.flags = 0x01;
    temp.out_path_len = OUT_PATH_UNKNOWN;
    temp.lastmod = mesh.getRTCClock()->getCurrentTime();
    // Leave unknown names blank so an advert can supply the learned name later.
    strlcpy(temp.name, e.learnedName[0] ? e.learnedName : e.name, sizeof(temp.name));
    if (mesh.addContact(temp)) changed = true;
    else lastError = "Contact list full: could not add every monitored repeater as a favorite.";
  }
  if (changed) mesh.saveMonitorContacts();
}

ContactInfo* RepeaterMonitor::contact() {
  Entry& e = entries[active];
  auto* c = mesh.lookupContactByPubKey(e.key, 32);
  if (!c) {
    ContactInfo temp{}; memcpy(temp.id.pub_key, e.key, 32);
    temp.type = ADV_TYPE_REPEATER; temp.flags = 0x01; temp.out_path_len = OUT_PATH_UNKNOWN;
    char key[65]; formatKey(e.key, key);
    strlcpy(temp.name, e.learnedName[0] ? e.learnedName : e.name, sizeof(temp.name));
    if (!mesh.addContact(temp)) return nullptr;
    mesh.saveMonitorContacts();
    c = mesh.lookupContactByPubKey(e.key, 32);
  }
  return c;
}

void RepeaterMonitor::nextEntry() {
  if (easternDay(now()) != runDay) { running = false; phase = Idle; active = -1; return; }
  if (singleTarget >= 0) active = singleTarget;
  else do { ++active; } while (active < (int)count && (!entries[active].enabled || (automatic && entries[active].scheduledDay >= runDay)));
  if (active >= (int)count) { running = false; phase = Idle; active = -1; return; }
  clockKnown = false; clockOffset = 0; clockUncertainty = 0;
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
  clockSentUtc = now(); clockSentMillis = millis();
  if (mesh.sendLogin(target, credentials.password(entries[active].key), estimate) == MSG_SEND_FAILED) { failed(SendFailed); return; }
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
  measured.clockKnown = result == Ok && clockKnown;
  measured.clockOffset = clockOffset; measured.clockUncertainty = clockUncertainty;
  if (!save()) { running = false; phase = Idle; return; }
  if (singleTarget >= 0) { running = false; phase = Idle; active = -1; singleTarget = -1; }
  else {
    phase = Next; deadline = millis() + 10000;
    // The persisted daily-check marker also bounds this to one automatic sync per day.
    if (automatic && timeSyncDue(entries[active], now())) startClockSync(active);
  }
}

bool RepeaterMonitor::onResponse(const ContactInfo& from, const uint8_t* data, size_t len) {
  if (syncPhase == SyncLogin && syncTarget >= 0 && syncTarget < (int)count &&
      !elapsed(millis(), syncDeadline) && !memcmp(from.id.pub_key, entries[syncTarget].key, 32) && len >= 13 && len <= 16 && data[4] == 0) {
    adminConfirmed[syncTarget] = data[6] == 1;
    if (!adminConfirmed[syncTarget]) finishClockSync("Login did not grant admin access; clock unchanged.");
    else { syncPhase = SyncReady; syncDeadline = millis() + 3000; syncResult = "Admin login accepted."; }
    return true;
  }
  if (!running || active < 0 || active >= (int)count || memcmp(from.id.pub_key, entries[active].key, 32) != 0) return false;
  if (phase == Login && loginOK(data, len)) {
    adminConfirmed[active] = len >= 13 && len <= 16 && data[4] == 0 && data[6] == 1;
    // Reuse the required authorization reply; never send a separate clock request.
    clockKnown = loginClock(data, len, clockSentUtc, (uint32_t)(millis() - clockSentMillis), clockOffset, clockUncertainty);
    phase = NeedStatus; deadline = millis() + 3000; return true;
  }
  if (phase == Status) {
    uint16_t mv;
    if (voltage(data, len, tag, mv)) { finish(Ok, mv); return true; }
  }
  return false;
}

void RepeaterMonitor::poll() {
  if (!synced || !storageOK || syncPhase != SyncIdle) return;
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

bool RepeaterMonitor::startClockSync(int target) {
  if (syncPhase != SyncIdle || target < 0 || target >= (int)count) return false;
  syncTarget = target;
  if (!storageOK || !credentials.available() || !syncTimeReady() ||
      !credentials.password(entries[target].key)[0] || !mesh.lookupContactByPubKey(entries[target].key, 32)) {
    syncResult = "Clock sync skipped: needs recent NTP, working storage, a saved password and contact.";
    return false;
  }
  syncLoginAttempted = false; syncResetAttempted = false; syncToken = (uint8_t)esp_random();
  if (adminConfirmed[target]) {
    syncPhase = SyncReady; syncDeadline = millis();
    syncResult = "Reusing confirmed repeater admin access.";
    return true;
  }
  return startSyncLogin();
}

bool RepeaterMonitor::startSyncLogin() {
  if (syncTarget < 0 || syncTarget >= (int)count || !syncTimeReady()) {
    finishClockSync("Could not authorize clock sync; no fresh NTP time."); return false;
  }
  adminConfirmed[syncTarget] = false;
  syncLoginAttempted = true;
  auto* c = mesh.lookupContactByPubKey(entries[syncTarget].key, 32);
  const char* password = credentials.password(entries[syncTarget].key);
  uint32_t estimate = 0;
  if (!c || !password[0] || mesh.sendLogin(*c, password, estimate) == MSG_SEND_FAILED) {
    finishClockSync("Could not send admin login; synchronization unconfirmed."); return false;
  }
  syncPhase = SyncLogin; syncDeadline = millis() + timeout(estimate);
  syncResult = "Waiting for repeater admin login.";
  return true;
}

void RepeaterMonitor::finishClockSync(const char* result) {
  syncPhase = SyncIdle; syncResult = result;
  if (running && automatic) deadline = millis() + 10000;
}

void RepeaterMonitor::pollClockSync() {
  if (syncPhase == SyncIdle || !elapsed(millis(), syncDeadline)) return;
  if (syncPhase == SyncLogin) { adminConfirmed[syncTarget] = false; finishClockSync("Admin login timed out; synchronization unconfirmed."); return; }
  if (syncPhase == SyncReply) {
    adminConfirmed[syncTarget] = false;
    if (!syncLoginAttempted) startSyncLogin();
    else finishClockSync("No confirmation received; clock may have changed.");
    return;
  }
  if (syncPhase == SyncCheckReply) { finishClockSync("Could not verify future clock; no reset sent."); return; }
  if (syncPhase == SyncRebootWait) { startSyncLogin(); return; }
  if (!syncTimeReady()) { finishClockSync("NTP time is stale; synchronization stopped."); return; }
  auto* c = mesh.lookupContactByPubKey(entries[syncTarget].key, 32);
  uint32_t stamp = mesh.getRTCClock()->getCurrentTimeUnique(), utc = now(), estimate = 0;
  if (!c || stamp < utc || stamp - utc > 5) {
    finishClockSync("Bot timestamp is not close to NTP; synchronization stopped."); return;
  }
  const bool checking = syncPhase == SyncCheckReady, resetting = syncPhase == SyncResetReady;
  if (resetting && syncResetAttempted) { finishClockSync("Reset already attempted; no further reboot sent."); return; }
  // Each step has a different echoed prefix, so late replies from earlier steps cannot advance it.
  snprintf(syncPrefix, sizeof(syncPrefix), "%02X|", (unsigned)++syncToken);
  char command[16];
  snprintf(command, sizeof(command), "%s%s", syncPrefix, resetting ? "clkreboot" : checking ? "clock" : "clock sync");
  syncCommandSentUtc = utc; syncCommandSentMillis = millis();
  if (mesh.sendCommandData(*c, stamp, 0, command, estimate) == MSG_SEND_FAILED) {
    finishClockSync("Could not send clock command; synchronization stopped."); return;
  }
  syncDeadline = millis() + timeout(estimate);
  if (resetting) {
    syncResetAttempted = true; adminConfirmed[syncTarget] = false;
    syncPhase = SyncRebootWait; syncDeadline += 30000;
    syncResult = "Clock reset/reboot sent once; waiting before logging in and retrying sync.";
  } else if (checking) {
    syncPhase = SyncCheckReply; syncResult = "Checking the repeater wall clock before considering a reset.";
  } else {
    syncPhase = SyncReply; syncResult = syncResetAttempted ? "Waiting for sync confirmation after reset." : "Waiting for clock-sync confirmation.";
  }
}

void RepeaterMonitor::recordClockSample(uint32_t remoteMinute) {
  Entry& e = entries[syncTarget];
  const uint32_t roundTrip = millis() - syncCommandSentMillis;
  e.clockCheckedAt = now();
  // CLI clock truncates seconds. Use the minute midpoint and expose that uncertainty.
  e.clockOffset = (int64_t)remoteMinute + 30 - ((int64_t)syncCommandSentUtc + roundTrip / 2000);
  e.clockUncertainty = 31 + (roundTrip + 1999) / 2000;
}

bool RepeaterMonitor::onCommandResponse(const ContactInfo& from, const char* text) {
  if ((syncPhase != SyncReply && syncPhase != SyncCheckReply && syncPhase != SyncRebootWait) ||
      syncTarget < 0 || syncTarget >= (int)count || elapsed(millis(), syncDeadline) ||
      memcmp(from.id.pub_key, entries[syncTarget].key, 32) || strncmp(text, syncPrefix, 3)) return false;
  const char* reply = text + 3;
  if (syncPhase == SyncRebootWait) {
    // Supported clkreboot reboots immediately without a CLI reply.
    finishClockSync("Repeater returned a reply to clkreboot; reset was not confirmed. Stopped.");
  } else if (syncPhase == SyncCheckReply) {
    uint32_t remote;
    if (!parseClockReply(reply, remote)) finishClockSync("Could not parse repeater wall clock; no reset sent.");
    else if (!syncTimeReady()) finishClockSync("NTP time is stale; no reset sent.");
    else {
      const Entry previous = entries[syncTarget];
      recordClockSample(remote);
      if (!save()) {
        entries[syncTarget] = previous;
        finishClockSync("Clock checked, but its offset could not be saved; no reset sent.");
        return true;
      }
      // Do not reboot a healthy repeater for minute quantization or small differences.
      if ((int64_t)remote - now() <= 60) finishClockSync("Clock checked; not clearly ahead. Approximate offset updated; no reset needed.");
      else { syncPhase = SyncResetReady; syncDeadline = millis() + 3000; syncResult = "Future wall clock confirmed; preparing one clock reset/reboot."; }
    }
  } else if (!strncmp(reply, "OK - clock set:", 15)) {
    adminConfirmed[syncTarget] = true;
    const Entry previous = entries[syncTarget];
    entries[syncTarget].lastSynced = now();
    entries[syncTarget].clockCheckedAt = 0; // Do not retain a pre-adjustment offset as current.
    uint32_t remote;
    if (parseClockReply(reply + 15, remote)) recordClockSample(remote);
    if (!save()) {
      entries[syncTarget] = previous;
      finishClockSync("Repeater confirmed sync, but its timestamp could not be saved.");
      return true;
    }
    finishClockSync(syncResetAttempted ? "Repeater confirmed clock sync after reset/reboot." : "Repeater confirmed its clock was moved forward.");
  } else if (!strcmp(reply, "ERR: clock cannot go backwards") || !strcmp(reply, "(ERR: clock cannot go backwards)")) {
    adminConfirmed[syncTarget] = true;
    if (syncResetAttempted) finishClockSync("Clock sync still refused after reset; no further reboot sent.");
    else { syncPhase = SyncCheckReady; syncDeadline = millis() + 3000; syncResult = "Backward change refused; checking whether the repeater is actually ahead."; }
  } else finishClockSync("Repeater did not confirm clock synchronization.");
  return true;
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
    mesh.getRTCClock()->setCurrentTimeFromSync((uint32_t)time(nullptr)); synced = true; lastNtpSync = millis();
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  }
  server.handleClient();
  if (elapsed(millis(), nextNameCheck)) { nextNameCheck = millis() + 10000; rememberNames(); }
  if (synced && elapsed(millis(), nextPrune)) { prune(now()); nextPrune = millis() + 60000; }
  pollClockSync();
  poll();
}
#endif
