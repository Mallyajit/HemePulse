#pragma once

#include <stdint.h>

namespace hb {

enum class WarningState : uint8_t {
  kNormal = 0,
  kElevated = 1,
  kHigh = 2,
  kLowSignal = 3,
  kBaselineNeeded = 4
};

enum class CommandType : uint8_t {
  kNone = 0,
  kRequestSnapshot = 1,
  kStartBaselineCapture = 2,
  kSetBaseline = 3,
  kClearBaseline = 4,
  kSetPhotodiodeSensitivity = 5,
  kSetAmplifierGain = 6,
  kSetBaselineVoltage = 7,
  kBpmStart = 8,
  kBpmStop = 9,
  kModeIdle = 10,
  kModePulse = 11
};

struct RawCycleSample {
  uint32_t timestampMs = 0;
  int16_t ambientRaw = 0;
  int16_t redRaw = 0;
  int16_t irRaw = 0;
  float redCorrected = 0.0f;
  float irCorrected = 0.0f;
  bool valid = false;
};

struct ProcessedMetrics {
  uint32_t timestampMs = 0;
  float redDc = 0.0f;
  float irDc = 0.0f;
  float redAc = 0.0f;
  float irAc = 0.0f;
  float redNorm = 0.0f;
  float irNorm = 0.0f;
  float ratioR = 0.0f;
  float irBandpassed = 0.0f;
  float noiseLevel = 0.0f;
  bool valid = false;
};

struct PulseMetrics {
  uint16_t bpm = 0;
  bool beatDetected = false;
  float intervalStability = 0.0f;
  float peakAmplitude = 0.0f;
  bool valid = false;
};

struct QualityMetrics {
  uint8_t confidence = 0;
  float snr = 0.0f;
  float amplitudeScore = 0.0f;
  float stabilityScore = 0.0f;
  float noiseScore = 0.0f;
  float consistencyScore = 0.0f;
};

struct RiskMetrics {
  bool baselineAvailable = false;
  bool baselineCapturing = false;
  float baselineR = 0.0f;
  float shortDrift = 0.0f;
  float longDrift = 0.0f;
  float riskScore = 0.0f;
  WarningState warning = WarningState::kBaselineNeeded;
};

struct TelemetryPacket {
  uint32_t timestampMs = 0;
  int16_t ambientRaw = 0;
  int16_t redCorrected = 0;
  int16_t irCorrected = 0;
  uint16_t bpmHint = 0;
  uint16_t bpm = 0;
  float ratioR = 0.0f;
  uint8_t confidence = 0;
  WarningState warning = WarningState::kBaselineNeeded;
  uint8_t flags = 0;
};

struct SessionSummary {
  uint32_t timestampMs = 0;
  float avgRed = 0.0f;
  float avgIr = 0.0f;
  float ratioR = 0.0f;
  uint16_t bpm = 0;
  uint8_t confidence = 0;
  uint8_t riskFlag = 0;  // 0=stable, 1=suspicious, 2=bad-signal
  bool valid = false;
};

struct CalibrationProfile {
  uint16_t schemaVersion = 1;
  float photodiodeSensitivityAw = 0.0f;
  float amplifierGainVPerA = 0.0f;
  float baselineVoltageMv = 0.0f;

  float baselineRedAvg = 0.0f;
  float baselineIrAvg = 0.0f;
  uint8_t baselineConfidence = 0;
  uint16_t stableBpmMin = 0;
  uint16_t stableBpmMax = 0;

  float userBaselineR = 0.0f;
  bool baselineValid = false;

  uint16_t stableSessionCount = 0;
  uint16_t suspiciousSessionCount = 0;
  uint16_t badSignalSessionCount = 0;
};

struct BleCommand {
  CommandType type = CommandType::kNone;
  float valueF32 = 0.0f;
  uint8_t valueU8 = 0;
  bool valid = false;
};

}  // namespace hb
