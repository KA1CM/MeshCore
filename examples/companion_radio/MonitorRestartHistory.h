#pragma once
#include "BotSnapshotFile.h"
#include <Arduino.h>
#include <esp_system.h>

class MonitorRestartHistory {
public:
  static constexpr size_t Capacity=16;
  struct Entry {uint32_t reason=0,bootUtc=0,freeHeap=0,largestBlock=0;};
  struct Store {uint32_t version=1,count=0;Entry entries[Capacity]{};};
private:
  BotSnapshotFile files{"/restarts0.bin","/restarts1.bin"};
  Store data;
  bool initialized=false,ready=false,dirty=false,attempted=false;
  uint32_t lastAttempt=0;
  const char* error="Not initialized";
public:
  static const char* reasonName(uint32_t reason) {
    switch((esp_reset_reason_t)reason) {
      case ESP_RST_POWERON:return "Power on";
      case ESP_RST_EXT:return "External reset";
      case ESP_RST_SW:return "Software reset";
      case ESP_RST_PANIC:return "Panic / exception";
      case ESP_RST_INT_WDT:return "Interrupt watchdog";
      case ESP_RST_TASK_WDT:return "Task watchdog";
      case ESP_RST_WDT:return "Watchdog";
      case ESP_RST_DEEPSLEEP:return "Deep-sleep wake";
      case ESP_RST_BROWNOUT:return "Brownout";
      case ESP_RST_SDIO:return "SDIO reset";
      default:return "Unknown reset";
    }
  }
  void begin() {
    if(initialized)return;
    initialized=true;
    const auto loaded=files.load(&data,sizeof(data));
    ready=loaded==BotSnapshotFile::Missing || loaded==BotSnapshotFile::Loaded;
    if(loaded==BotSnapshotFile::Loaded && (data.version!=1 || data.count>Capacity)) {
      ready=false;error="Restart history incompatible";
    } else error=ready ? "" : files.lastError();
    if(!ready)data=Store{}; // Preserve bad files; show the current reset in RAM.
    const size_t count=data.count<Capacity ? data.count : Capacity-1;
    for(size_t i=count;i>0;--i)data.entries[i]=data.entries[i-1];
    data.count=count+1;
    auto& entry=data.entries[0];entry=Entry{};
    entry.reason=(uint32_t)esp_reset_reason();
    entry.freeHeap=ESP.getFreeHeap();entry.largestBlock=ESP.getMaxAllocHeap();
    dirty=true;poll(0,false);
  }
  void poll(uint32_t utc,bool timeReady) {
    if(!initialized)return;
    if(timeReady && utc>=1700000000UL && !data.entries[0].bootUtc) {
      data.entries[0].bootUtc=utc-millis()/1000;dirty=true;
    }
    if(!ready || !dirty || (attempted && uint32_t(millis()-lastAttempt)<60000UL))return;
    attempted=true;lastAttempt=millis();
    if(files.save(&data,sizeof(data))){dirty=false;attempted=false;error="";}
    else error=files.lastError();
  }
  const Store& records() const {return data;}
  bool available() const {return ready;}
  const char* lastError() const {return error;}
};
