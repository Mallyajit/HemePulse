#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hb {
namespace config {

// ── GPIO pins ──
static constexpr uint8_t kRedLedPin = 1;
static constexpr uint8_t kIrLedPin = 2;
static constexpr uint8_t kAdcPin = 0;

// ── ADC settings ──
static constexpr uint8_t kAdcResolutionBits = 12;
static constexpr float kAdcReferenceMv = 3300.0f;
static constexpr float kAdcFullScale = 4095.0f;

// ── Measurement timing (20 Hz = 50ms cycle) ──
static constexpr uint32_t kMeasurementCyclePeriodUs = 50000;  // 20 Hz dual-LED (Hb mode)
static constexpr uint32_t kPulseCyclePeriodUs       = 50000;  // 20 Hz RED-only (pulse mode)
static constexpr uint32_t kLedSettleUs = 4000;  // 4ms LED settle time
static constexpr uint32_t kDarkGapUs   = 1000;  // 1ms dark gap between RED and IR

// ── Hb two-mode architecture ──
static constexpr uint32_t kHbIntervalMs    = 20000;  // Hb snapshot every 20 seconds
static constexpr uint32_t kHbBurstMs       = 3000;   // 3s burst = 60 samples at 20Hz
static constexpr uint32_t kIdleLoopDelayMs = 10;     // Idle loop yield

// ── Baseline capture ──
static constexpr uint32_t kBaselineCaptureDurationMs = 60000;  // 60s baseline
static constexpr uint16_t kMinBaselineSamples        = 10;     // need at least 10 good samples
static constexpr uint8_t  kMinConfidenceForBaseline  = 50;     // minimum quality for baseline

// ── BLE batching ──
// Batch 5 raw samples (11 bytes each = 55 bytes) and send at 4 Hz
static constexpr uint8_t  kBatchSampleCount    = 5;
static constexpr uint8_t  kRawSampleSize       = 11;  // per-sample bytes
static constexpr uint32_t kBleNotifyIntervalMs = 250;  // 4 Hz notifications
static constexpr char     kBleDeviceName[]     = "HemePulse-C3";

// ── Serial debug ──
static constexpr bool     kEnableSerialDebug        = true;
static constexpr uint32_t kDebugPrintIntervalMs     = 2000;

}  // namespace config
}  // namespace hb
