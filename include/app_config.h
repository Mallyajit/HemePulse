#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hb {
namespace config {

static constexpr uint8_t kRedLedPin = 1;
static constexpr uint8_t kIrLedPin = 2;
static constexpr uint8_t kAdcPin = 0;

static constexpr uint8_t kAdcResolutionBits = 12;
static constexpr float kAdcReferenceMv = 3300.0f;
static constexpr float kAdcFullScale = 4095.0f;

static constexpr uint32_t kMeasurementCyclePeriodUs = 40000;  // 25 Hz (longer LEDs on)
static constexpr uint32_t kLedSettleUs = 4000;  // 4ms settle = visible RED flash
static constexpr uint32_t kDarkGapUs = 1000;

static constexpr size_t kSampleHistoryBufferSlots = 256;
static constexpr size_t kAcDcWindowSamples = 50;
static constexpr size_t kPeakHistorySlots = 12;
static constexpr size_t kRatioConsistencyWindow = 40;

static constexpr float kBandpassHpHz = 0.5f;
static constexpr float kBandpassLpHz = 4.0f;
static constexpr float kSampleRateHz = 25.0f;  // Matches 25Hz cycle

static constexpr uint16_t kMinBpm = 40;
static constexpr uint16_t kMaxBpm = 190;
static constexpr uint32_t kMinPeakSpacingMs = 60000UL / kMaxBpm;
static constexpr uint32_t kPulseWindowMs = 8000;
static constexpr float kPeakThresholdScale = 0.35f;
static constexpr float kPeakThresholdFloor = 2.0f;

static constexpr uint8_t kConfidencePoor = 35;
static constexpr uint8_t kConfidenceFair = 55;
static constexpr uint8_t kConfidenceGood = 75;
static constexpr uint8_t kMinConfidenceForBaseline = 65;

static constexpr float kEmaAlpha = 0.18f;
static constexpr float kMinIrSignal = 20.0f;
static constexpr float kMinRedSignal = 12.0f;
static constexpr float kMotionRatioJumpThreshold = 0.09f;
static constexpr float kMotionSignalJumpThreshold = 0.28f;

static constexpr float kStableDriftThreshold = 0.05f;
static constexpr float kElevatedDriftThreshold = 0.09f;
static constexpr float kHighDriftThreshold = 0.14f;
static constexpr uint8_t kElevatedStreakThreshold = 4;
static constexpr uint8_t kHighStreakThreshold = 8;

static constexpr uint32_t kBaselineCaptureDurationMs = 60000;
static constexpr uint16_t kMinBaselineSamples = 3;

static constexpr float kRiskElevatedThreshold = 40.0f;
static constexpr float kRiskHighThreshold = 65.0f;

// Always-awake runtime cadence: produce one processed reading every 20 seconds.
static constexpr uint32_t kReadingIntervalMs = 20000;
static constexpr uint16_t kMinSessionSamples = 3;

// ── Two-mode architecture constants ──
static constexpr uint32_t kHbIntervalMs       = 20000;   // Hb snapshot every 20 seconds
static constexpr uint32_t kHbBurstMs          = 2500;    // 2.5s measurement burst
static constexpr uint32_t kPulseSessionMs     = 45000;   // Max BPM session duration
static constexpr uint32_t kIdleLoopDelayMs    = 10;      // Idle loop yield (ms)
static constexpr uint16_t kLedPulsePeriodUs   = 20000;   // LED cycle period (50Hz)
static constexpr uint16_t kLedOnTimeUs        = 5000;    // LED on-time per phase

// ── BPM detection constants ──
static constexpr uint16_t kBpmSampleBufferSize = 150;    // ~3 seconds at 50Hz
static constexpr float    kBpmPeakThreshold    = 0.15f;  // Lower for valley detection
static constexpr uint32_t kBpmMinIntervalMs    = 316;    // ~190 BPM max
static constexpr uint32_t kBpmMaxIntervalMs    = 1500;   // ~40 BPM min

static constexpr uint32_t kBleNotifyIntervalMs = 20;  // 50 Hz
static constexpr char kBleDeviceName[] = "HemePulse-C3";

// Keep serial output lightweight and only for commissioning.
static constexpr bool kEnableSerialDebug = true;
// Waveform streaming (disabled — causes high CPU load at 50Hz in idle).
static constexpr bool kEnableSerialWaveform = false;
static constexpr uint32_t kWaveformPrintIntervalMs = 20;

// ── Debug throttle ──
static constexpr uint32_t kDebugPrintIntervalMs = 2000;  // Print debug every 2s

}  // namespace config
}  // namespace hb
