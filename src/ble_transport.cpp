#include "ble_transport.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"

namespace hb {

namespace {

const char* kServiceUuid = "4f9c0100-a1f2-4c31-98cb-1cce5caa1000";
const char* kControlUuid = "4f9c0107-a1f2-4c31-98cb-1cce5caa1007";
const char* kBaselineUuid = "4f9c0108-a1f2-4c31-98cb-1cce5caa1008";
const char* kPacketUuid = "4f9c0109-a1f2-4c31-98cb-1cce5caa1009";

constexpr size_t kCompactPacketPayloadSize =
  sizeof(uint32_t) +  // timestampMs
  sizeof(int16_t) +   // ambientRaw
  sizeof(int16_t) +   // redCorrected
  sizeof(int16_t) +   // irCorrected
  sizeof(float) +     // ratioR
  sizeof(uint8_t) +   // confidence
  sizeof(uint8_t) +   // warning
  sizeof(uint8_t);    // flags

static_assert(kCompactPacketPayloadSize == 17,
        "Compact BLE packet contract changed unexpectedly");

NimBLEServer* gServer = nullptr;
NimBLECharacteristic* gControlChar = nullptr;
NimBLECharacteristic* gBaselineChar = nullptr;
NimBLECharacteristic* gPacketChar = nullptr;

bool gConnected = false;
bool gAllowAdvertising = true;
char gIncomingCommand[64] = {0};
volatile bool gIncomingCommandPending = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) override {
    (void)server;
    gConnected = true;
  }

  void onDisconnect(NimBLEServer* server) override {
    (void)server;
    gConnected = false;
    if (gAllowAdvertising) {
      NimBLEDevice::startAdvertising();
    }
  }
};

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }

    const size_t copyLen = value.size() < (sizeof(gIncomingCommand) - 1)
                               ? value.size()
                               : (sizeof(gIncomingCommand) - 1);

    memcpy(gIncomingCommand, value.data(), copyLen);
    gIncomingCommand[copyLen] = '\0';
    gIncomingCommandPending = true;
  }
};

}  // namespace

void BleTransport::begin(const char* deviceName) {
  gAllowAdvertising = true;

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);

  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = gServer->createService(kServiceUuid);

    gPacketChar = service->createCharacteristic(
      kPacketUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  gControlChar = service->createCharacteristic(
      kControlUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  gControlChar->setCallbacks(new ControlCallbacks());

  gBaselineChar = service->createCharacteristic(
      kBaselineUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);

  uint8_t emptyPacket[kCompactPacketPayloadSize] = {0};
  gPacketChar->setValue(emptyPacket, sizeof(emptyPacket));
  gControlChar->setValue("READY");

  setBaselineValue(0.0f, false);

  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();

  hasPendingCommand_ = false;
  pendingCommand_ = BleCommand();
  lastNotifyMs_ = 0;
}

void BleTransport::update(uint32_t nowMs) {
  (void)nowMs;
  if (gIncomingCommandPending) {
    gIncomingCommandPending = false;
    handleControlCommand(gIncomingCommand);
    gIncomingCommand[0] = '\0';
  }
}

void BleTransport::shutdown() {
  gAllowAdvertising = false;
  gConnected = false;

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising != nullptr) {
    advertising->stop();
  }

  NimBLEDevice::deinit(true);

  gServer = nullptr;
  gPacketChar = nullptr;
  gControlChar = nullptr;
  gBaselineChar = nullptr;
  hasPendingCommand_ = false;
  pendingCommand_ = BleCommand();
}

void BleTransport::setBaselineValue(float baselineR, bool valid) {
  baselineValid_ = valid;

  if (gBaselineChar == nullptr) {
    return;
  }

  uint8_t payload[5] = {0};
  memcpy(payload, &baselineR, sizeof(float));
  payload[4] = valid ? 1 : 0;

  gBaselineChar->setValue(payload, sizeof(payload));
  if (gConnected) {
    gBaselineChar->notify();
  }
}

void BleTransport::setBaselineCapturing(bool capturing) { baselineCapturing_ = capturing; }

void BleTransport::publish(const TelemetryPacket& packet, bool cycleComplete, bool force) {
  if (!cycleComplete && !force) {
    return;
  }

  if (!gConnected) {
    return;
  }

  if (gPacketChar == nullptr) {
    return;
  }

  const uint32_t nowMs = millis();
  if (!force && ((nowMs - lastNotifyMs_) < config::kBleNotifyIntervalMs)) {
    return;
  }

  uint8_t liveFlags = packet.flags;
  if (baselineValid_) {
    liveFlags |= 0x10;
  }
  if (baselineCapturing_) {
    liveFlags |= 0x20;
  }

  uint8_t payload[kCompactPacketPayloadSize] = {0};
  uint8_t warningRaw = static_cast<uint8_t>(packet.warning);

  size_t offset = 0;
  memcpy(payload + offset, &packet.timestampMs, sizeof(packet.timestampMs));
  offset += sizeof(packet.timestampMs);

  memcpy(payload + offset, &packet.ambientRaw, sizeof(packet.ambientRaw));
  offset += sizeof(packet.ambientRaw);

  memcpy(payload + offset, &packet.redCorrected, sizeof(packet.redCorrected));
  offset += sizeof(packet.redCorrected);

  memcpy(payload + offset, &packet.irCorrected, sizeof(packet.irCorrected));
  offset += sizeof(packet.irCorrected);

  memcpy(payload + offset, &packet.ratioR, sizeof(packet.ratioR));
  offset += sizeof(packet.ratioR);

  payload[offset++] = packet.confidence;
  payload[offset++] = warningRaw;
  payload[offset++] = liveFlags;

  gPacketChar->setValue(payload, sizeof(payload));
  gPacketChar->notify();

  lastNotifyMs_ = nowMs;
}

bool BleTransport::popCommand(BleCommand& commandOut) {
  if (!hasPendingCommand_) {
    return false;
  }

  commandOut = pendingCommand_;
  pendingCommand_ = BleCommand();
  hasPendingCommand_ = false;
  return true;
}

bool BleTransport::isConnected() const { return gConnected; }

void BleTransport::handleControlCommand(const char* text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  BleCommand command;

  if (strcmp(text, "SNAP") == 0) {
    command.type = CommandType::kRequestSnapshot;
    command.valid = true;
  } else if (strcmp(text, "BASE_START") == 0) {
    command.type = CommandType::kStartBaselineCapture;
    command.valid = true;
  } else if (strcmp(text, "BASE_CLEAR") == 0) {
    command.type = CommandType::kClearBaseline;
    command.valid = true;
  } else if (strncmp(text, "BASE_SET=", 9) == 0) {
    command.type = CommandType::kSetBaseline;
    command.valueF32 = static_cast<float>(atof(text + 9));
    command.valid = true;
  } else if (strncmp(text, "CAL_PD=", 7) == 0) {
    command.type = CommandType::kSetPhotodiodeSensitivity;
    command.valueF32 = static_cast<float>(atof(text + 7));
    command.valid = true;
  } else if (strncmp(text, "CAL_GAIN=", 9) == 0) {
    command.type = CommandType::kSetAmplifierGain;
    command.valueF32 = static_cast<float>(atof(text + 9));
    command.valid = true;
  } else if (strncmp(text, "CAL_VREF=", 9) == 0) {
    command.type = CommandType::kSetBaselineVoltage;
    command.valueF32 = static_cast<float>(atof(text + 9));
    command.valid = true;
  } else if (strcmp(text, "BPM_START") == 0) {
    command.type = CommandType::kBpmStart;
    command.valid = true;
  } else if (strcmp(text, "BPM_STOP") == 0) {
    command.type = CommandType::kBpmStop;
    command.valid = true;
  } else if (strcmp(text, "MODE_IDLE") == 0) {
    command.type = CommandType::kModeIdle;
    command.valid = true;
  } else if (strcmp(text, "MODE_PULSE") == 0) {
    command.type = CommandType::kModePulse;
    command.valid = true;
  }

  if (!command.valid) {
    return;
  }

  pendingCommand_ = command;
  hasPendingCommand_ = true;
}

}  // namespace hb
