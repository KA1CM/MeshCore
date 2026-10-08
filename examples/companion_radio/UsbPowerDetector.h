#pragma once
#include <stdint.h>
#include <stddef.h>
// Voltage-signature heuristic for the tested Heltec V4.3 + 18650.
// Boot calibration assumes the normal USB-powered startup; no GPIO USB sensor.
class UsbPowerDetector {
public:
  enum State : uint8_t { Calibrating, Connected, Lost };
  enum Event : uint8_t { None, Calibrated, PowerLost, PowerRestored };
private:
  struct Sample { uint32_t ms;uint16_t mv; } samples[12]{};
  size_t next=0,count=0;
  uint32_t started=0,last=0,candidateSince=0;
  bool initialized=false,candidate=false;
  State current=Calibrating;
  uint16_t average=0,baseline=0;
public:
  State state()const{return current;}
  uint16_t averagedMillivolts()const{return average;}
  bool due(uint32_t ms)const{return !initialized || (uint32_t)(ms-last)>=1000;}
  Event add(uint32_t ms,uint16_t mv) {
    if(!due(ms))return None;
    const bool gap=initialized && (uint32_t)(ms-last)>5000;
    if(!initialized){initialized=true;started=ms;}
    last=ms;
    if(!mv){candidate=false;count=next=0;return None;}
    samples[next]={ms,mv};next=(next+1)%12;if(count<12)++count;
    uint32_t sum=0;size_t n=0;
    for(size_t i=0;i<count;++i) {
      const auto& s=samples[(next+11-i)%12];
      if((uint32_t)(ms-s.ms)>10000)break;
      sum+=s.mv;++n;
    }
    average=n ? sum/n : 0;
    // Require fresh samples after stalls; elapsed gaps aren't persistence evidence.
    if(n<7 || gap){candidate=false;return None;}
    if(current==Calibrating) {
      if((uint32_t)(ms-started)<60000)return None;
      baseline=average;current=Connected;return Calibrated;
    }
    const int delta=(int)average-baseline;
    const bool changed=current==Lost ? delta>=25 : delta<=-25;
    if(!changed){candidate=false;return None;}
    if(!candidate){candidate=true;candidateSince=ms;return None;}
    if((uint32_t)(ms-candidateSince)<20000)return None;
    baseline=average;candidate=false;
    current=current==Lost ? Connected : Lost;
    return current==Lost ? PowerLost : PowerRestored;
  }
};
