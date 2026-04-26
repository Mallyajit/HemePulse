#pragma once

#include <stdint.h>

#include "types.h"

namespace hb {

class BleTransport {
 public:
  void begin(const char* deviceName);
  void update(uint32_t nowMs);
  void shutdown();

  void setBaselineValue(float baselineR, bool valid);
  void setBaselineCapturing(bool capturing);

  /// Queue a raw sample into the batch buffer.
  /// Returns true when the batch is full and was sent.
  bool queueRawSample(const RawBlePacket& sample);

  /// Force-flush any queued samples (even if batch not full).
  void flushBatch();

  bool popCommand(BleCommand& commandOut);
  bool isConnected() const;

 private:
  void handleControlCommand(const char* text);

  bool hasPendingCommand_ = false;
  BleCommand pendingCommand_;

  uint32_t lastNotifyMs_ = 0;
  bool baselineValid_ = false;
  bool baselineCapturing_ = false;

  // Batch buffer: up to 5 samples of 11 bytes each = 55 bytes max
  static constexpr uint8_t kMaxBatch = 5;
  static constexpr uint8_t kSampleBytes = 11;
  uint8_t batchBuffer_[kMaxBatch * kSampleBytes] = {};
  uint8_t batchCount_ = 0;
};

}  // namespace hb
