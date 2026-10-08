#pragma once
#include <Preferences.h>
#include <memory>
#include <new>
#include "RepeaterMonitorCore.h"

// Private NVS blob. Never serialize this store into dashboard or list JSON.
class MonitorCredentials {
  struct Record { uint8_t key[32]{}; char password[16]{}; };
  struct Store { uint32_t version = 1; Record records[MonitorCore::MAX_REPEATERS]{}; } data;
  Preferences prefs;
  bool ready = false;
  static bool valid(const Store& s) {
    if (s.version != 1) return false;
    for (size_t i = 0; i < MonitorCore::MAX_REPEATERS; ++i) {
      const auto& r = s.records[i];
      if (!memchr(r.password, 0, sizeof(r.password))) return false;
      if (!r.password[0]) continue;
      bool nonzero = false;
      for (auto byte : r.key) nonzero |= byte != 0;
      if (!nonzero) return false;
      for (size_t j = 0; j < i; ++j)
        if (s.records[j].password[0] && !memcmp(r.key, s.records[j].key, 32)) return false;
    }
    return true;
  }
  bool commit(const Store& candidate) {
    if (prefs.putBytes("credentials", &candidate, sizeof(candidate)) != sizeof(candidate)) return false;
    data = candidate;
    return true;
  }
public:
  bool begin(const MonitorCore::Entry* entries, size_t count, const char* initial = "") {
    ready = false;
    if (count > MonitorCore::MAX_REPEATERS || strlen(initial) > 15 || !prefs.begin("monitor-private", false)) return false;
    if (prefs.isKey("credentials")) {
      Store loaded;
      if (prefs.getBytesLength("credentials") != sizeof(loaded) ||
          prefs.getBytes("credentials", &loaded, sizeof(loaded)) != sizeof(loaded) || !valid(loaded)) return false;
      data = loaded;
    } else {
      Store first;
      // One-time provisioning binds only the currently saved public keys.
      for (size_t i = 0; i < count && initial[0]; ++i) {
        memcpy(first.records[i].key, entries[i].key, 32);
        memcpy(first.records[i].password, initial, strlen(initial) + 1);
      }
      if (!commit(first)) return false;
    }
    ready = true;
    return retain(entries, count);
  }
  bool available() const { return ready; }
  const char* password(const uint8_t* key) const {
    if (ready) for (const auto& r : data.records)
      if (r.password[0] && !memcmp(r.key, key, 32)) return r.password;
    return "";
  }
  bool set(const MonitorCore::Entry* entries, size_t count, const uint8_t* key, const char* password) {
    if (!ready || count > MonitorCore::MAX_REPEATERS || strlen(password) > 15) return false;
    // Called from the radio receive handler: keep the 1540-byte store off its stack.
    std::unique_ptr<Store> candidate(new (std::nothrow) Store(data));
    if (!candidate) return false;
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
      if (key && memcmp(entries[i].key, key, 32)) continue;
      found = true;
      Record* slot = nullptr;
      for (auto& r : candidate->records) if (!memcmp(r.key, entries[i].key, 32)) { slot = &r; break; }
      if (!slot) for (auto& r : candidate->records) if (!r.password[0]) { slot = &r; break; }
      if (!slot) return false;
      *slot = Record{};
      if (password[0]) {
        memcpy(slot->key, entries[i].key, 32);
        memcpy(slot->password, password, strlen(password) + 1);
      }
    }
    return found && commit(*candidate);
  }
  bool retain(const MonitorCore::Entry* entries, size_t count) {
    if (!ready || count > MonitorCore::MAX_REPEATERS) return false;
    Store candidate = data;
    bool changed = false;
    for (auto& r : candidate.records) {
      if (!r.password[0]) continue;
      bool found = false;
      for (size_t i = 0; i < count; ++i) found |= memcmp(r.key, entries[i].key, 32) == 0;
      if (!found) { r = Record{}; changed = true; }
    }
    if (changed && !commit(candidate)) { ready = false; return false; }
    return true;
  }
};
