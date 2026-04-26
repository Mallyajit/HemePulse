#pragma once

#include <stdint.h>

namespace hb {

// ── Operating modes ──
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

// ── Raw sample from one measurement cycle ──
struct RawCycleSample {
  uint32_t timestampMs = 0;
  int16_t ambientRaw = 0;
  int16_t redRaw = 0;
  int16_t irRaw = 0;
  float redCorrected = 0.0f;
  float irCorrected = 0.0f;
  bool valid = false;
};

// ── Minimal raw sample for BLE streaming (11 bytes) ──
struct RawBlePacket {
  uint32_t timestampMs = 0;   // 4 bytes
  int16_t  ambientRaw  = 0;   // 2 bytes
  int16_t  redCorrected = 0;  // 2 bytes (redRaw - ambient, clamped to int16)
  int16_t  irCorrected  = 0;  // 2 bytes (irRaw - ambient, clamped; 0 in pulse mode)
  uint8_t  mode         = 0;  // 1 byte (0=idle, 1=hbBurst, 2=pulse)
};

// ── Calibration stored in NVS ──
struct CalibrationProfile {
  uint16_t schemaVersion = 1;
  float photodiodeSensitivityAw = 0.0f;
  float amplifierGainVPerA = 0.0f;
  float baselineVoltageMv = 0.0f;

  float baselineRedAvg = 0.0f;
  float baselineIrAvg = 0.0f;
  uint8_t baselineConfidence = 0;

  float userBaselineR = 0.0f;
  bool baselineValid = false;
};

// ── BLE command from phone ──
struct BleCommand {
  CommandType type = CommandType::kNone;
  float valueF32 = 0.0f;
  uint8_t valueU8 = 0;
  bool valid = false;
};

}  // namespace hb
