#pragma once

#include "app_config.h"
#include "ring_buffer.h"
#include "types.h"

namespace hb {

class SignalProcessor {
 public:
  void reset();
  ProcessedMetrics update(const RawCycleSample& sample);

 private:
  float updateBandpass(float input);
    float computeDc(
      const RingBuffer<float, config::kAcDcWindowSamples>& buffer) const;
    float computeAc(
      const RingBuffer<float, config::kAcDcWindowSamples>& buffer) const;

    RingBuffer<float, config::kAcDcWindowSamples> redWindow_;
    RingBuffer<float, config::kAcDcWindowSamples> irWindow_;

  float hpPrevOut_ = 0.0f;
  float hpPrevIn_ = 0.0f;
  float lpPrevOut_ = 0.0f;
  float prevBandpassed_ = 0.0f;
  float noiseLevel_ = 1.0f;
};

}  // namespace hb
