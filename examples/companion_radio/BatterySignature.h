#pragma once
#include <stdint.h>
#include <stddef.h>
// Main-loop owned; elapsed times preserve sampling gaps rather than inventing data.
struct BatterySignature {
  struct Sample { uint32_t elapsed; uint16_t mv; uint8_t phase, lookup, detected; };
  static constexpr size_t capacity = 601;
  Sample samples[capacity];
  size_t count = 0;
  uint32_t started = 0, last = 0, utc = 0;
  uint32_t lostAt = 0, restoredAt = 0;
  uint8_t phase = 0; // manually labelled connected / unplugged / restored
  bool running = false;
  // Experimental only: start assumes USB connected; manual markers never drive it.
  uint8_t detected = 0; // 0 calibrating, 1 connected, 2 suspected lost, 3 suspected restored
  uint16_t average = 0, baseline = 0;
  uint32_t candidateSince = 0;
  bool candidate = false;
  void detect(uint32_t elapsed) {
    uint32_t sum=0; size_t n=0;
    for(size_t i=count;i>0;--i) {
      if(elapsed-samples[i-1].elapsed>10000)break;
      sum+=samples[i-1].mv; ++n;
    }
    average=n ? sum/n : 0;
    // Missing samples cannot count as evidence that a change persisted.
    if(n<7 || (count>1 && elapsed-samples[count-2].elapsed>5000)) {
      candidate=false; return;
    }
    if(!detected) {
      if(elapsed<60000)return;
      baseline=average; detected=1; return;
    }
    const int delta=(int)average-baseline;
    const bool changed=detected==2 ? delta>=25 : delta<=-25;
    if(!changed){candidate=false;return;}
    if(!candidate){candidate=true;candidateSince=elapsed;return;}
    if(elapsed-candidateSince>=20000) {
      detected=detected==2 ? 3 : 2;
      baseline=average;candidate=false;
    }
  }
  void start(uint32_t ms, uint32_t timestamp) {
    count=0; started=ms; last=ms-1000; utc=timestamp;
    lostAt=restoredAt=0; phase=0; running=true;
    detected=0; average=baseline=0; candidate=false; candidateSince=0;
  }
  bool due(uint32_t ms) {
    if(running && (uint32_t)(ms-started)>600000UL) running=false;
    return running && count<capacity && (uint32_t)(ms-last)>=1000;
  }
  bool mark(uint8_t next, uint32_t ms) {
    if(!running || next!=phase+1 || next>2)return false;
    phase=next;
    if(next==1)lostAt=ms-started; else restoredAt=ms-started;
    return true;
  }
  void add(uint32_t ms,uint16_t mv,bool lookup) {
    if(!due(ms))return;
    samples[count++]={ms-started,mv,phase,(uint8_t)lookup,detected};last=ms;
    detect(ms-started);samples[count-1].detected=detected;
    if(count==capacity || (uint32_t)(ms-started)>=600000UL)running=false;
  }
};
