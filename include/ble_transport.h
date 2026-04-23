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

  void publish(const TelemetryPacket& packet, bool cycleComplete, bool force = false);

  bool popCommand(BleCommand& commandOut);
  bool isConnected() const;

 private:
  void handleControlCommand(const char* text);

  bool hasPendingCommand_ = false;
  BleCommand pendingCommand_;

  uint32_t lastNotifyMs_ = 0;
  bool baselineValid_ = false;
  bool baselineCapturing_ = false;
};

}  // namespace hb
