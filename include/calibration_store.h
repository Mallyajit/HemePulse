#pragma once

#include "types.h"

namespace hb {

class CalibrationStore {
 public:
  void begin();

  const CalibrationProfile& profile() const;

  void setPhotodiodeSensitivity(float valueAw);
  void setAmplifierGain(float valueVPerA);
  void setBaselineVoltage(float valueMv);

  void setTrustedBaseline(float redAvg, float irAvg, float ratioR, uint8_t confidence,
                          bool valid);
  void setUserBaseline(float valueR, bool valid);
  void clearUserBaseline();

 private:
  void loadDefaults();
  void loadFromStorage();
  void saveToStorage();

  CalibrationProfile profile_;
};

}  // namespace hb
