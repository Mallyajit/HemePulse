#include "hardware_io.h"

#include <Arduino.h>

#include "app_config.h"

namespace hb {

void HardwareIO::begin() {
  pinMode(config::kRedLedPin, OUTPUT);
  pinMode(config::kIrLedPin, OUTPUT);
  setLedState(false, false);

  analogReadResolution(config::kAdcResolutionBits);
  analogSetAttenuation(ADC_11db);
}

void HardwareIO::setLedState(bool redOn, bool irOn) {
  digitalWrite(config::kRedLedPin, redOn ? HIGH : LOW);
  digitalWrite(config::kIrLedPin, irOn ? HIGH : LOW);
}

int16_t HardwareIO::readAdcRaw() const {
  return static_cast<int16_t>(analogRead(config::kAdcPin));
}

float HardwareIO::rawToMillivolts(int16_t raw) const {
  const float adcToMv = config::kAdcReferenceMv / config::kAdcFullScale;
  return static_cast<float>(raw) * adcToMv;
}

void HardwareIO::allLedsOff() {
  digitalWrite(config::kRedLedPin, LOW);
  digitalWrite(config::kIrLedPin, LOW);
}

}  // namespace hb
