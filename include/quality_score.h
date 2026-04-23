#pragma once

#include "app_config.h"
#include "ring_buffer.h"
#include "types.h"

namespace hb {

class QualityScorer {
 public:
  void reset();
  QualityMetrics update(const ProcessedMetrics& processed, const PulseMetrics& pulse);

 private:
  float clamp01(float value) const;

  RingBuffer<float, config::kRatioConsistencyWindow> ratioHistory_;
};

}  // namespace hb
