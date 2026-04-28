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

bool gConnectionSeen = false;

// Mode state
OpMode gMode = OpMode::kIdle;
uint32_t gLastHbSnapshotMs = 0;
uint32_t gHbBurstStartMs = 0;
uint32_t gBurstSampleCount = 0;
uint32_t gLastDebugMs = 0;

// Baseline capture state (must stay on firmware — controls LEDs)
struct BaselineCaptureState {
  bool active = false;
  uint32_t startMs = 0;
  float sumRed = 0.0f;
  float sumIr = 0.0f;
  float sumRatio = 0.0f;
  uint32_t sumConfidence = 0;
  uint16_t count = 0;
};

BaselineCaptureState gBaselineCapture;

// ── Utility ──

int16_t quantizeSignal(float value) {
  if (value < -32768.0f) return -32768;
  if (value > 32767.0f) return 32767;
  return static_cast<int16_t>(value >= 0.0f ? (value + 0.5f) : (value - 0.5f));
}

uint8_t modeToU8(OpMode m) {
  return static_cast<uint8_t>(m);
}

// ── Baseline capture (firmware-side: needs LED control) ──

void startBaselineCapture(uint32_t nowMs) {
  gBaselineCapture.active = true;
  gBaselineCapture.startMs = nowMs;
  gBaselineCapture.sumRed = 0.0f;
  gBaselineCapture.sumIr = 0.0f;
  gBaselineCapture.sumRatio = 0.0f;
  gBaselineCapture.sumConfidence = 0;
  gBaselineCapture.count = 0;
  gBleTransport.setBaselineCapturing(true);

  // Baseline needs both LEDs — ensure we're in dual-LED mode
  if (gMode != OpMode::kHbBurst) {
    gMode = OpMode::kHbBurst;
    gHbBurstStartMs = nowMs;
    gBurstSampleCount = 0;
    gScheduler.begin(gHardware, micros());
  }

  if (config::kEnableSerialDebug) Serial.println("[BASE] Starting 60s baseline capture");
}

void updateBaselineCapture(const RawCycleSample& sample, uint32_t nowMs) {
  if (!gBaselineCapture.active) return;

  // Only accept samples with valid RED and IR
  if (sample.redCorrected > 1.0f && sample.irCorrected > 1.0f) {
    const float ratio = sample.redCorrected / sample.irCorrected;
    if (isfinite(ratio) && ratio > 0.01f && ratio < 10.0f) {
      gBaselineCapture.sumRed += sample.redCorrected;
      gBaselineCapture.sumIr += sample.irCorrected;
      gBaselineCapture.sumRatio += ratio;
      ++gBaselineCapture.count;
    }
  }

  if ((nowMs - gBaselineCapture.startMs) < config::kBaselineCaptureDurationMs) return;

  // Capture complete
  gBaselineCapture.active = false;
  gBleTransport.setBaselineCapturing(false);

  if (gBaselineCapture.count < config::kMinBaselineSamples) {
    if (config::kEnableSerialDebug) {
      Serial.print("[BASE] Failed: only ");
      Serial.print(gBaselineCapture.count);
      Serial.println(" valid samples");
    }
    return;
  }

  const float invCount = 1.0f / static_cast<float>(gBaselineCapture.count);
  const float avgRatio = gBaselineCapture.sumRatio * invCount;

  gCalibrationStore.setTrustedBaseline(
      gBaselineCapture.sumRed * invCount,
      gBaselineCapture.sumIr * invCount,
      avgRatio, 80, true);

  gBleTransport.setBaselineValue(avgRatio, true);

  if (config::kEnableSerialDebug) {
    Serial.print("[BASE] Complete: R=");
    Serial.print(avgRatio, 4);
    Serial.print(" samples=");
    Serial.println(gBaselineCapture.count);
  }
}

// ── Mode transitions ──

void enterPulseMode() {
  gMode = OpMode::kPulse;
  gScheduler.begin(gHardware, micros());
  if (config::kEnableSerialDebug) Serial.println("[MODE] PULSE (RED & IR 20Hz)");
}

void enterIdleMode() {
  gMode = OpMode::kIdle;
  gScheduler.stop();
  gHardware.allLedsOff();
  // Flush any remaining BLE samples
  gBleTransport.flushBatch();
  if (config::kEnableSerialDebug) Serial.println("[MODE] IDLE");
}

void enterHbBurst(uint32_t nowMs) {
  gMode = OpMode::kHbBurst;
  gHbBurstStartMs = nowMs;
  gBurstSampleCount = 0;
  gScheduler.begin(gHardware, micros());
  if (config::kEnableSerialDebug) Serial.println("[HB] Starting 3s burst");
}

bool timeForHbSnapshot(uint32_t nowMs) {
  return (nowMs - gLastHbSnapshotMs) >= config::kHbIntervalMs;
}

// ── Command handler ──
void handleBleCommand(const BleCommand& command, uint32_t nowMs) {
  switch (command.type) {
    case CommandType::kRequestSnapshot:
      // Flush current batch so app gets latest data
      gBleTransport.flushBatch();
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
      if (gMode != OpMode::kPulse) enterPulseMode();
      break;

    case CommandType::kBpmStop:
    case CommandType::kModeIdle:
      if (gMode != OpMode::kIdle) enterIdleMode();
      break;

    default:
      break;
  }
}

// ── Debug printing ──
void printPeriodicDebug(uint32_t nowMs) {
  if (!config::kEnableSerialDebug) return;
  if ((nowMs - gLastDebugMs) < config::kDebugPrintIntervalMs) return;
  gLastDebugMs = nowMs;
  Serial.print("[DBG] mode=");
  Serial.print(modeToU8(gMode));
  Serial.print(" heap=");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" conn=");
  Serial.println(gBleTransport.isConnected() ? 1 : 0);
}

}  // namespace

// ══════════════════════════════════════════════════
// PUBLIC ENTRY POINTS
// ══════════════════════════════════════════════════

void appSetup() {
  if (config::kEnableSerialDebug) {
    Serial.begin(115200);
    delay(25);
  }

  gHardware.begin();
  gCalibrationStore.begin();

  gBleTransport.begin(config::kBleDeviceName);
  gBleTransport.setBaselineCapturing(false);
  gBleTransport.setBaselineValue(gCalibrationStore.profile().userBaselineR,
                                 gCalibrationStore.profile().baselineValid);

  gBaselineCapture = BaselineCaptureState();
  gConnectionSeen = gBleTransport.isConnected();

  // Start in idle mode — LEDs off
  gMode = OpMode::kIdle;
  gLastHbSnapshotMs = millis() - config::kHbIntervalMs;  // trigger first burst immediately

  if (config::kEnableSerialDebug) {
    Serial.println("[BOOT] HemePulse v3 — raw streamer firmware");
    Serial.print("[BOOT] 20Hz cycle, batch BLE 4Hz, Hb every ");
    Serial.print(config::kHbIntervalMs / 1000);
    Serial.print("s, heap=");
    Serial.println(ESP.getFreeHeap());
  }
}

void appLoop() {
  const uint32_t nowMs = millis();

  // 1. Poll BLE
  gBleTransport.update(nowMs);

  // 2. Handle connection changes
  const bool connectedNow = gBleTransport.isConnected();
  if (connectedNow && !gConnectionSeen) {
    gConnectionSeen = true;
    gBleTransport.flushBatch();
  }
  if (!connectedNow && gConnectionSeen) {
    gConnectionSeen = false;
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
      if (timeForHbSnapshot(nowMs)) {
        enterHbBurst(nowMs);
      } else {
        delay(config::kIdleLoopDelayMs);
      }
      break;
    }

    case OpMode::kHbBurst: {
      const uint32_t elapsed = nowMs - gHbBurstStartMs;

      if (elapsed < config::kHbBurstMs || gBaselineCapture.active) {
        // Keep running during baseline capture even past burst time
        RawCycleSample sample;
        if (gScheduler.update(micros(), sample)) {
          ++gBurstSampleCount;

          // Update baseline if capturing
          updateBaselineCapture(sample, nowMs);

          // Stream raw sample to phone
          RawBlePacket pkt;
          pkt.timestampMs = sample.timestampMs;
          pkt.ambientRaw = sample.ambientRaw;
          pkt.redCorrected = quantizeSignal(sample.redCorrected);
          pkt.irCorrected = quantizeSignal(sample.irCorrected);
          pkt.mode = modeToU8(gMode);
          gBleTransport.queueRawSample(pkt);
        }
      } else {
        // Burst complete
        gBleTransport.flushBatch();
        gLastHbSnapshotMs = nowMs;
        if (config::kEnableSerialDebug) {
          Serial.print("[HB] Burst done, samples=");
          Serial.println(gBurstSampleCount);
        }
        enterIdleMode();
      }
      break;
    }

    case OpMode::kPulse: {
      // RED & IR 20Hz streaming — all processing on phone
      RawCycleSample sample;
      if (gScheduler.update(micros(), sample)) {
        RawBlePacket pkt;
        pkt.timestampMs = sample.timestampMs;
        pkt.ambientRaw = sample.ambientRaw;
        pkt.redCorrected = quantizeSignal(sample.redCorrected);
        pkt.irCorrected = quantizeSignal(sample.irCorrected);
        pkt.mode = modeToU8(gMode);
        gBleTransport.queueRawSample(pkt);
      }
      break;
    }
  }

  // 5. Debug
  printPeriodicDebug(nowMs);
}

}  // namespace hb

void setup() { hb::appSetup(); }
void loop() { hb::appLoop(); }
