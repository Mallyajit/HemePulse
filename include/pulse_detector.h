#pragma once

#include "app_config.h"
#include "ring_buffer.h"
#include "types.h"

namespace hb {

class PulseDetector {
 public:
  void reset();
  PulseMetrics update(float irBandpassed, float irAc, uint32_t timestampMs);

 private:
  uint16_t computeBpm() const;
  float computeStability() const;

    RingBuffer<uint32_t, config::kPeakHistorySlots> peakTimes_;
  float prev2_ = 0.0f;
  float prev1_ = 0.0f;
  bool primed_ = false;
  uint32_t lastPeakMs_ = 0;
  float lastPeakAmplitude_ = 0.0f;
};

}  // namespace hb
