#pragma once

#include <stdint.h>

#include "types.h"

namespace hb {

class HemoglobinRiskEngine {
 public:
  void begin(const CalibrationProfile& profile);

  void startBaselineCapture(uint32_t nowMs);
  bool updateBaselineCapture(float ratioR, uint8_t confidence, uint32_t nowMs,
                             float& newBaselineOut);
  bool isBaselineCapturing() const;

  void setBaseline(float baselineR);
  void clearBaseline();
  bool hasBaseline() const;
  float baselineR() const;

  RiskMetrics update(float ratioR, uint8_t confidence, uint32_t nowMs);
  const RiskMetrics& lastRisk() const;

 private:
  float baselineR_ = 0.0f;
  bool baselineValid_ = false;

  bool baselineCapturing_ = false;
  uint32_t captureStartMs_ = 0;
  float captureAccum_ = 0.0f;
  uint16_t captureCount_ = 0;

  float shortEmaDrift_ = 0.0f;
  float longEmaDrift_ = 0.0f;
  RiskMetrics lastRisk_;
};

}  // namespace hb
