#pragma once

#include <stdint.h>

#include "hardware_io.h"
#include "types.h"

namespace hb {

enum class SchedulerState : uint8_t {
  kInit = 0,
  kAmbientSettle = 1,
  kRedSettle = 2,
  kDarkSettle = 3,
  kIrSettle = 4,
  kWaitNextCycle = 5
};

class MeasurementScheduler {
 public:
  void begin(HardwareIO& hw, uint32_t nowUs);
  void beginRedOnly(HardwareIO& hw, uint32_t nowUs);
  void stop();
  bool update(uint32_t nowUs, RawCycleSample& sampleOut);
  SchedulerState state() const;

 private:
  void startNewCycle(uint32_t nowUs);

  HardwareIO* hw_ = nullptr;
  SchedulerState state_ = SchedulerState::kInit;
  uint32_t cycleStartUs_ = 0;
  uint32_t stateStartUs_ = 0;
  RawCycleSample current_;
  bool redOnly_ = false;
};

}  // namespace hb

