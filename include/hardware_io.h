#pragma once

#include <stdint.h>

namespace hb {

class HardwareIO {
 public:
  void begin();
  void setLedState(bool redOn, bool irOn);
  void allLedsOff();
  int16_t readAdcRaw() const;
  float rawToMillivolts(int16_t raw) const;
};

}  // namespace hb
