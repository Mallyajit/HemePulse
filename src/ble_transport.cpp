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

  // Initialize with empty values
  uint8_t emptyPacket[kMaxBatch * kSampleBytes] = {0};
  gPacketChar->setValue(emptyPacket, kSampleBytes);  // single empty sample
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
  batchCount_ = 0;
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
  batchCount_ = 0;
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

bool BleTransport::queueRawSample(const RawBlePacket& sample) {
  if (!gConnected || gPacketChar == nullptr) {
    batchCount_ = 0;
    return false;
  }

  // Serialize sample into batch buffer
  const size_t offset = static_cast<size_t>(batchCount_) * kSampleBytes;
  memcpy(batchBuffer_ + offset, &sample.timestampMs, sizeof(uint32_t));
  memcpy(batchBuffer_ + offset + 4, &sample.ambientRaw, sizeof(int16_t));
  memcpy(batchBuffer_ + offset + 6, &sample.redCorrected, sizeof(int16_t));
  memcpy(batchBuffer_ + offset + 8, &sample.irCorrected, sizeof(int16_t));
  batchBuffer_[offset + 10] = sample.mode;

  ++batchCount_;

  // If batch full, send it
  if (batchCount_ >= kMaxBatch) {
    const uint32_t nowMs = millis();
    if ((nowMs - lastNotifyMs_) >= config::kBleNotifyIntervalMs) {
      gPacketChar->setValue(batchBuffer_, static_cast<size_t>(batchCount_) * kSampleBytes);
      gPacketChar->notify();
      lastNotifyMs_ = nowMs;
      batchCount_ = 0;
      return true;
    }
    // If rate-limited, drop oldest and shift
    memmove(batchBuffer_, batchBuffer_ + kSampleBytes,
            static_cast<size_t>(kMaxBatch - 1) * kSampleBytes);
    batchCount_ = kMaxBatch - 1;
  }

  return false;
}

void BleTransport::flushBatch() {
  if (!gConnected || gPacketChar == nullptr || batchCount_ == 0) {
    return;
  }

  gPacketChar->setValue(batchBuffer_, static_cast<size_t>(batchCount_) * kSampleBytes);
  gPacketChar->notify();
  lastNotifyMs_ = millis();
  batchCount_ = 0;
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
