#pragma once
#include "RepeaterMonitorCore.h"
namespace RepeaterOrder {
// Contact coordinates are signed millionths of a degree; (0,0) is unset.
template<class Mesh>
void westToEast(const MonitorCore::Entry* entries,size_t count,Mesh& mesh,size_t* order) {
  bool known[MonitorCore::MAX_REPEATERS]{};
  int32_t longitude[MonitorCore::MAX_REPEATERS]{};
  for(size_t i=0;i<count;++i) {
    order[i]=i;
    auto* contact=mesh.lookupContactByPubKey(entries[i].key,32);
    if(contact && MonitorCore::validLocation(contact->gps_lat,contact->gps_lon)) {
      known[i]=true;longitude[i]=contact->gps_lon;
    } else if(MonitorCore::validLocation(entries[i].learnedLatitude,entries[i].learnedLongitude)) {
      known[i]=true;longitude[i]=entries[i].learnedLongitude;
    }
  }
  // Stable insertion sort keeps tied longitudes and unknown locations in saved order.
  for(size_t i=1;i<count;++i) {
    size_t value=order[i],j=i;
    while(j && known[value] && (!known[order[j-1]] || longitude[value]<longitude[order[j-1]])) {
      order[j]=order[j-1];--j;
    }
    order[j]=value;
  }
}
}
