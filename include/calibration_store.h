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

  void setSessionCounters(uint16_t stableCount, uint16_t suspiciousCount,
                          uint16_t badSignalCount);
  void saveLastSessionSummary(const SessionSummary& summary);
  bool loadLastSessionSummary(SessionSummary& summaryOut) const;

 private:
  void loadDefaults();
  void loadFromStorage();
  void saveToStorage();
  void saveSessionSummaryToStorage() const;

  CalibrationProfile profile_;
  SessionSummary lastSession_;
};

}  // namespace hb
