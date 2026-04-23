#include <Arduino.h>
#include <math.h>
#include "app_config.h"
#include "ble_transport.h"
#include "calibration_store.h"
#include "hardware_io.h"
#include "scheduler.h"
#include "types.h"

namespace hb {
namespace {

// ── Operating mode ──
enum class OpMode : uint8_t { kIdle = 0, kHbBurst = 1, kPulse = 2 };

HardwareIO gHardware;
MeasurementScheduler gScheduler;
CalibrationStore gCalibrationStore;
BleTransport gBleTransport;

TelemetryPacket gLastPacket;
bool gHasLastPacket = false;
bool gConnectionSeen = false;

// Mode state
OpMode gMode = OpMode::kIdle;
uint32_t gLastHbSnapshotMs = 0;
uint32_t gHbBurstStartMs = 0;
uint32_t gPulseModeStartMs = 0;
uint32_t gLastDebugMs = 0;
uint32_t gLastWaveformMs = 0;

// ── Realtime state (shared between burst and pulse) ──
struct RealtimeState {
  bool initialized = false;
  float redAvg = 0.0f;
  float irAvg = 0.0f;
  float prevRatio = 0.0f;
  uint8_t suspiciousStreak = 0;
  uint8_t stableStreak = 0;
  float irWindow[5] = {};
  uint8_t irWindowCount = 0;
  uint8_t irWindowIndex = 0;
  float irFiltered = 0.0f;
};

struct BaselineCaptureState {
  bool active = false;
  uint32_t startMs = 0;
  float sumRed = 0.0f;
  float sumIr = 0.0f;
  float sumRatio = 0.0f;
  uint32_t sumConfidence = 0;
  uint16_t count = 0;
};

struct SessionAccumulator {
  uint32_t sampleCount = 0;
  uint32_t goodSampleCount = 0;
  uint32_t suspiciousSampleCount = 0;
  uint32_t badSignalSampleCount = 0;
  float sumRed = 0.0f;
  float sumIr = 0.0f;
  float sumRatio = 0.0f;
  uint32_t sumConfidence = 0;
};

RealtimeState gRealtime;
BaselineCaptureState gBaselineCapture;
SessionAccumulator gSession;

// Burst sample counter
uint32_t gBurstSampleCount = 0;

// ── Utility functions ──

uint8_t clampU8(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

int16_t quantizeSignal(float value) {
  if (value < -32768.0f) return -32768;
  if (value > 32767.0f) return 32767;
  return static_cast<int16_t>(value >= 0.0f ? (value + 0.5f) : (value - 0.5f));
}

float safeRatio(float redValue, float irValue) {
  if (irValue <= 1e-6f) return 0.0f;
  const float ratio = redValue / irValue;
  return isfinite(ratio) ? ratio : 0.0f;
}

bool isMotionLikely(const RawCycleSample& sample, float ratio) {
  if (!gRealtime.initialized) return false;
  const float redJump = fabsf(sample.redCorrected - gRealtime.redAvg) / fmaxf(1.0f, gRealtime.redAvg);
  // In pulse mode, IR is off — only use RED jump for motion detection
  if (gMode == OpMode::kPulse) {
    return (redJump > config::kMotionSignalJumpThreshold);
  }
  const float irJump = fabsf(sample.irCorrected - gRealtime.irAvg) / fmaxf(1.0f, gRealtime.irAvg);
  const float ratioJump = fabsf(ratio - gRealtime.prevRatio);
  return (ratioJump > config::kMotionRatioJumpThreshold) ||
         (redJump > config::kMotionSignalJumpThreshold) ||
         (irJump > config::kMotionSignalJumpThreshold);
}

uint8_t computeSimpleConfidence(const RawCycleSample& sample, float redAvg, float irAvg,
                               float ratio, bool motionLikely) {
  int score = 100;
  // In pulse mode, IR is intentionally off — skip IR penalties
  if (gMode != OpMode::kPulse) {
    if (irAvg < config::kMinIrSignal) score -= 35;
    if (ratio <= 1e-6f) score -= 20;
    const float ambientVsIr = static_cast<float>(sample.ambientRaw) / fmaxf(1.0f, static_cast<float>(sample.irRaw));
    if (ambientVsIr > 0.88f) score -= 18;
  }
  if (redAvg < config::kMinRedSignal) score -= 22;
  if (motionLikely) score -= 30;
  return clampU8(score);
}

WarningState evaluateLocalWarning(float ratio, uint8_t confidence, bool motionLikely) {
  const CalibrationProfile& profile = gCalibrationStore.profile();
  const float baseline = profile.userBaselineR;
  if (!profile.baselineValid || baseline <= 1e-6f) return WarningState::kBaselineNeeded;
  if (confidence < config::kConfidencePoor || motionLikely) return WarningState::kLowSignal;
  const float driftAbs = fabsf((ratio - baseline) / baseline);
  if (driftAbs < config::kStableDriftThreshold) {
    gRealtime.stableStreak = static_cast<uint8_t>(gRealtime.stableStreak + 1);
    if (gRealtime.suspiciousStreak > 0) gRealtime.suspiciousStreak--;
    return WarningState::kNormal;
  }
  gRealtime.stableStreak = 0;
  gRealtime.suspiciousStreak = static_cast<uint8_t>(gRealtime.suspiciousStreak + 1);
  if (driftAbs >= config::kHighDriftThreshold && gRealtime.suspiciousStreak >= config::kHighStreakThreshold)
    return WarningState::kHigh;
  if (driftAbs >= config::kElevatedDriftThreshold && gRealtime.suspiciousStreak >= config::kElevatedStreakThreshold)
    return WarningState::kElevated;
  return WarningState::kNormal;
}

uint8_t makeFlags(WarningState warning, uint8_t confidence, bool baselineValid,
                  bool baselineCapturing, bool motionLikely) {
  uint8_t flags = 0;
  flags |= 0x02;
  if (confidence < config::kConfidenceFair) flags |= 0x04;
  if (warning == WarningState::kElevated || warning == WarningState::kHigh) flags |= 0x08;
  if (baselineValid) flags |= 0x10;
  if (baselineCapturing) flags |= 0x20;
  if (motionLikely) flags |= 0x40;
  return flags;
}

float updateFilteredIrPreview(float irValue) {
  gRealtime.irWindow[gRealtime.irWindowIndex] = irValue;
  gRealtime.irWindowIndex = static_cast<uint8_t>((gRealtime.irWindowIndex + 1U) % 5U);
  if (gRealtime.irWindowCount < 5U) ++gRealtime.irWindowCount;
  float movingAverage = 0.0f;
  for (uint8_t i = 0; i < gRealtime.irWindowCount; ++i) movingAverage += gRealtime.irWindow[i];
  movingAverage /= static_cast<float>(gRealtime.irWindowCount);
  gRealtime.irFiltered = irValue - movingAverage;
  return gRealtime.irFiltered;
}

void resetSessionAccumulator() { gSession = SessionAccumulator(); }

void startBaselineCapture(uint32_t nowMs) {
  gBaselineCapture.active = true;
  gBaselineCapture.startMs = nowMs;
  gBaselineCapture.sumRed = 0.0f;
  gBaselineCapture.sumIr = 0.0f;
  gBaselineCapture.sumRatio = 0.0f;
  gBaselineCapture.sumConfidence = 0;
  gBaselineCapture.count = 0;
  gBleTransport.setBaselineCapturing(true);
}

void updateBaselineCapture(float redAvg, float irAvg, float ratio, uint8_t confidence, uint32_t nowMs) {
  if (!gBaselineCapture.active) return;
  if (ratio > 1e-6f && confidence >= config::kMinConfidenceForBaseline) {
    gBaselineCapture.sumRed += redAvg;
    gBaselineCapture.sumIr += irAvg;
    gBaselineCapture.sumRatio += ratio;
    gBaselineCapture.sumConfidence += confidence;
    ++gBaselineCapture.count;
  }
  if ((nowMs - gBaselineCapture.startMs) < config::kBaselineCaptureDurationMs) return;
  gBaselineCapture.active = false;
  gBleTransport.setBaselineCapturing(false);
  if (gBaselineCapture.count < config::kMinBaselineSamples) return;
  const float invCount = 1.0f / static_cast<float>(gBaselineCapture.count);
  gCalibrationStore.setTrustedBaseline(
      gBaselineCapture.sumRed * invCount, gBaselineCapture.sumIr * invCount,
      gBaselineCapture.sumRatio * invCount,
      static_cast<uint8_t>(gBaselineCapture.sumConfidence / static_cast<uint32_t>(gBaselineCapture.count)), true);
  gBleTransport.setBaselineValue(gBaselineCapture.sumRatio * invCount, true);
}

void updateSessionAccumulator(float redAvg, float irAvg, float ratio, uint8_t confidence,
                              WarningState warning, bool motionLikely) {
  ++gSession.sampleCount;
  gSession.sumRed += redAvg;
  gSession.sumIr += irAvg;
  gSession.sumRatio += ratio;
  gSession.sumConfidence += confidence;
  if (confidence >= config::kConfidenceFair && !motionLikely) ++gSession.goodSampleCount;
  if (warning == WarningState::kElevated || warning == WarningState::kHigh) ++gSession.suspiciousSampleCount;
  if (warning == WarningState::kLowSignal || confidence < config::kConfidencePoor || motionLikely)
    ++gSession.badSignalSampleCount;
}

void saveSessionSummary(uint32_t nowMs) {
  SessionSummary summary;
  summary.timestampMs = nowMs;
  if (gSession.sampleCount > 0) {
    const float invCount = 1.0f / static_cast<float>(gSession.sampleCount);
    summary.avgRed = gSession.sumRed * invCount;
    summary.avgIr = gSession.sumIr * invCount;
    summary.ratioR = gSession.sumRatio * invCount;
    summary.confidence = static_cast<uint8_t>(gSession.sumConfidence / static_cast<uint32_t>(gSession.sampleCount));
  }
  summary.bpm = 0;
  summary.valid = gSession.sampleCount >= config::kMinSessionSamples;
  if (!summary.valid || gSession.badSignalSampleCount > (gSession.sampleCount / 2)) summary.riskFlag = 2;
  else if (gSession.suspiciousSampleCount * 3 > gSession.goodSampleCount) summary.riskFlag = 1;
  else summary.riskFlag = 0;
  uint16_t stableSessions = gCalibrationStore.profile().stableSessionCount;
  uint16_t suspiciousSessions = gCalibrationStore.profile().suspiciousSessionCount;
  uint16_t badSessions = gCalibrationStore.profile().badSignalSessionCount;
  if (summary.riskFlag == 2) ++badSessions;
  else if (summary.riskFlag == 1) ++suspiciousSessions;
  else ++stableSessions;
  gCalibrationStore.setSessionCounters(stableSessions, suspiciousSessions, badSessions);
  gCalibrationStore.saveLastSessionSummary(summary);
}

// ── Process one measurement cycle and build/send packet ──
bool processMeasurementCycle(uint32_t nowUs, bool publishBle) {
  RawCycleSample sample;
  if (!gScheduler.update(nowUs, sample)) return false;

  if (!gRealtime.initialized) {
    gRealtime.redAvg = sample.redCorrected;
    gRealtime.irAvg = sample.irCorrected;
    gRealtime.prevRatio = safeRatio(gRealtime.redAvg, gRealtime.irAvg);
    gRealtime.initialized = true;
  } else {
    gRealtime.redAvg += config::kEmaAlpha * (sample.redCorrected - gRealtime.redAvg);
    gRealtime.irAvg += config::kEmaAlpha * (sample.irCorrected - gRealtime.irAvg);
  }

  const float filteredIr = updateFilteredIrPreview(gRealtime.irAvg);
  const float ratio = safeRatio(gRealtime.redAvg, gRealtime.irAvg);
  const bool motionLikely = isMotionLikely(sample, ratio);
  const uint8_t confidence = computeSimpleConfidence(sample, gRealtime.redAvg, gRealtime.irAvg, ratio, motionLikely);

  updateBaselineCapture(gRealtime.redAvg, gRealtime.irAvg, ratio, confidence, sample.timestampMs);
  const WarningState warning = evaluateLocalWarning(ratio, confidence, motionLikely);
  const bool baselineValid = gCalibrationStore.profile().baselineValid;

  TelemetryPacket packet;
  packet.timestampMs = sample.timestampMs;
  packet.ambientRaw = sample.ambientRaw;
  packet.redCorrected = quantizeSignal(gRealtime.redAvg);
  packet.irCorrected = quantizeSignal(gRealtime.irAvg);
  packet.bpmHint = 0;
  packet.bpm = 0;
  packet.ratioR = ratio;
  packet.confidence = confidence;
  packet.warning = warning;
  packet.flags = makeFlags(warning, confidence, baselineValid, gBaselineCapture.active, motionLikely);

  gLastPacket = packet;
  gHasLastPacket = true;

  if (publishBle) {
    gBleTransport.publish(packet, true);
  }

  updateSessionAccumulator(gRealtime.redAvg, gRealtime.irAvg, ratio, confidence, warning, motionLikely);
  gRealtime.prevRatio = ratio;

  // Waveform serial output (only when enabled, throttled)
  if (config::kEnableSerialWaveform) {
    const uint32_t nowMs = millis();
    if ((nowMs - gLastWaveformMs) >= config::kWaveformPrintIntervalMs) {
      gLastWaveformMs = nowMs;
      Serial.print(">sin:");
      Serial.println(filteredIr, 3);
    }
  }

  return true;
}

// ── Mode transitions ──

void enterPulseMode(uint32_t nowMs) {
  gMode = OpMode::kPulse;
  gPulseModeStartMs = nowMs;
  gRealtime = RealtimeState();
  // RED-only scheduler: only flash RED LED, skip IR
  gScheduler.beginRedOnly(gHardware, micros());
  if (config::kEnableSerialDebug) Serial.println("[MODE] Entering PULSE mode (RED-only)");
}

void enterIdleMode() {
  gMode = OpMode::kIdle;
  gScheduler.stop();
  gHardware.allLedsOff();
  if (config::kEnableSerialDebug) Serial.println("[MODE] Entering IDLE mode");
}

void enterHbBurst(uint32_t nowMs) {
  gMode = OpMode::kHbBurst;
  gHbBurstStartMs = nowMs;
  gBurstSampleCount = 0;
  gRealtime = RealtimeState();
  gScheduler.begin(gHardware, micros());
  if (config::kEnableSerialDebug) Serial.println("[HB] Starting measurement burst");
}

bool timeForHbSnapshot(uint32_t nowMs) {
  return (nowMs - gLastHbSnapshotMs) >= config::kHbIntervalMs;
}

// ── Command handler ──
void handleBleCommand(const BleCommand& command, uint32_t nowMs) {
  switch (command.type) {
    case CommandType::kRequestSnapshot:
      if (gHasLastPacket) gBleTransport.publish(gLastPacket, true, true);
      break;
    case CommandType::kStartBaselineCapture:
      startBaselineCapture(nowMs);
      break;
    case CommandType::kSetBaseline:
      if (command.valueF32 > 1e-6f) {
        const CalibrationProfile& profile = gCalibrationStore.profile();
        gCalibrationStore.setTrustedBaseline(profile.baselineRedAvg, profile.baselineIrAvg,
                                             command.valueF32, profile.baselineConfidence, true);
        gBleTransport.setBaselineValue(command.valueF32, true);
      }
      break;
    case CommandType::kClearBaseline:
      gCalibrationStore.clearUserBaseline();
      gBleTransport.setBaselineValue(0.0f, false);
      break;
    case CommandType::kSetPhotodiodeSensitivity:
      gCalibrationStore.setPhotodiodeSensitivity(command.valueF32);
      break;
    case CommandType::kSetAmplifierGain:
      gCalibrationStore.setAmplifierGain(command.valueF32);
      break;
    case CommandType::kSetBaselineVoltage:
      gCalibrationStore.setBaselineVoltage(command.valueF32);
      break;
    case CommandType::kBpmStart:
    case CommandType::kModePulse:
      if (gMode != OpMode::kPulse) enterPulseMode(nowMs);
      break;
    case CommandType::kBpmStop:
    case CommandType::kModeIdle:
      if (gMode != OpMode::kIdle) enterIdleMode();
      break;
    default:
      break;
  }
}

// ── Debug printing (throttled) ──
void printPeriodicDebug(uint32_t nowMs) {
  if (!config::kEnableSerialDebug) return;
  if ((nowMs - gLastDebugMs) < config::kDebugPrintIntervalMs) return;
  gLastDebugMs = nowMs;
  Serial.print("[DBG] mode=");
  Serial.print(static_cast<uint8_t>(gMode));
  Serial.print(" heap=");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" conn=");
  Serial.print(gBleTransport.isConnected() ? 1 : 0);
  if (gHasLastPacket) {
    Serial.print(" R=");
    Serial.print(gLastPacket.ratioR, 4);
    Serial.print(" conf=");
    Serial.print(gLastPacket.confidence);
    Serial.print(" red=");
    Serial.print(gLastPacket.redCorrected);
  }
  Serial.println();
}

}  // namespace

// ══════════════════════════════════════════════════
// PUBLIC ENTRY POINTS
// ══════════════════════════════════════════════════

void appSetup() {
  if (config::kEnableSerialDebug || config::kEnableSerialWaveform) {
    Serial.begin(115200);
    delay(25);
  }

  gHardware.begin();
  gCalibrationStore.begin();

  gBleTransport.begin(config::kBleDeviceName);
  gBleTransport.setBaselineCapturing(false);
  gBleTransport.setBaselineValue(gCalibrationStore.profile().userBaselineR,
                                 gCalibrationStore.profile().baselineValid);

  gRealtime = RealtimeState();
  gBaselineCapture = BaselineCaptureState();
  gLastWaveformMs = 0;
  resetSessionAccumulator();

  gConnectionSeen = gBleTransport.isConnected();

  // Start in idle mode — LEDs off, scheduler stopped
  gMode = OpMode::kIdle;
  gLastHbSnapshotMs = millis() - config::kHbIntervalMs; // trigger first snapshot immediately

  if (config::kEnableSerialDebug) {
    Serial.println("[BOOT] HemePulse two-mode firmware started");
    Serial.print("[BOOT] Hb interval=");
    Serial.print(config::kHbIntervalMs);
    Serial.print("ms, burst=");
    Serial.print(config::kHbBurstMs);
    Serial.print("ms, heap=");
    Serial.println(ESP.getFreeHeap());
  }
}

void appLoop() {
  const uint32_t nowMs = millis();

  // 1. Poll BLE (lightweight)
  gBleTransport.update(nowMs);

  // 2. Handle connection changes
  const bool connectedNow = gBleTransport.isConnected();
  if (connectedNow && !gConnectionSeen) {
    gConnectionSeen = true;
    if (gHasLastPacket) gBleTransport.publish(gLastPacket, true, true);
  }
  if (!connectedNow && gConnectionSeen) {
    gConnectionSeen = false;
    saveSessionSummary(nowMs);
    resetSessionAccumulator();
    // Exit pulse mode on disconnect
    if (gMode == OpMode::kPulse) enterIdleMode();
  }

  // 3. Consume BLE commands
  BleCommand command;
  while (gBleTransport.popCommand(command)) {
    handleBleCommand(command, nowMs);
  }

  // 4. Mode dispatch
  switch (gMode) {
    case OpMode::kIdle: {
      // Check if it's time for a Hb snapshot
      if (timeForHbSnapshot(nowMs)) {
        enterHbBurst(nowMs);
      } else {
        // LOW POWER IDLE — yield CPU
        delay(config::kIdleLoopDelayMs);
      }
      break;
    }

    case OpMode::kHbBurst: {
      // Run measurement cycles for burst duration
      const uint32_t elapsed = nowMs - gHbBurstStartMs;
      if (elapsed < config::kHbBurstMs) {
        // Only publish the LAST sample of the burst
        bool isFinalSample = elapsed >= (config::kHbBurstMs - 25);
        if (processMeasurementCycle(micros(), isFinalSample)) {
          ++gBurstSampleCount;
        }
      } else {
        // Burst complete — send final packet and return to idle
        if (gHasLastPacket && gBleTransport.isConnected()) {
          gBleTransport.publish(gLastPacket, true, true);
        }
        gLastHbSnapshotMs = nowMs;
        if (config::kEnableSerialDebug) {
          Serial.print("[HB] Burst complete, samples=");
          Serial.print(gBurstSampleCount);
          if (gHasLastPacket) {
            Serial.print(" R=");
            Serial.print(gLastPacket.ratioR, 4);
            Serial.print(" conf=");
            Serial.print(gLastPacket.confidence);
          }
          Serial.println();
        }
        enterIdleMode();
      }
      break;
    }

    case OpMode::kPulse: {
      // Check timeout
      if ((nowMs - gPulseModeStartMs) >= config::kPulseSessionMs) {
        if (config::kEnableSerialDebug) Serial.println("[PULSE] Session timeout, returning to idle");
        enterIdleMode();
        break;
      }
      // RED-only continuous measurement — stream raw data to app
      // App handles all BPM computation from corrected RED signal
      if (processMeasurementCycle(micros(), true)) {
        // Data is published via processMeasurementCycle
      }
      break;
    }
  }

  // 5. Periodic debug output
  printPeriodicDebug(nowMs);
}

}  // namespace hb

void setup() { hb::appSetup(); }
void loop() { hb::appLoop(); }
