#include "calibration_store.h"

#include <Preferences.h>

namespace hb {

namespace {

const char* kNamespace = "hbcal";
const char* kSchemaKey = "schema";
const char* kPdKey = "pd_aw";
const char* kGainKey = "gain_vpa";
const char* kVrefKey = "vref_mv";
const char* kBaseRKey = "base_r";
const char* kBaseValidKey = "base_ok";
const char* kBaseRedKey = "base_red";
const char* kBaseIrKey = "base_ir";
const char* kBaseConfKey = "base_conf";
const char* kBpmMinKey = "bpm_min";
const char* kBpmMaxKey = "bpm_max";
const char* kStableSessionsKey = "sess_st";
const char* kSuspSessionsKey = "sess_sp";
const char* kBadSessionsKey = "sess_bad";

const char* kSummaryTsKey = "sum_ts";
const char* kSummaryRedKey = "sum_red";
const char* kSummaryIrKey = "sum_ir";
const char* kSummaryRatioKey = "sum_ratio";
const char* kSummaryBpmKey = "sum_bpm";
const char* kSummaryConfKey = "sum_conf";
const char* kSummaryRiskKey = "sum_risk";
const char* kSummaryValidKey = "sum_ok";

}  // namespace

void CalibrationStore::begin() {
  loadDefaults();
  loadFromStorage();
}

const CalibrationProfile& CalibrationStore::profile() const { return profile_; }

void CalibrationStore::setPhotodiodeSensitivity(float valueAw) {
  profile_.photodiodeSensitivityAw = valueAw;
  saveToStorage();
}

void CalibrationStore::setAmplifierGain(float valueVPerA) {
  profile_.amplifierGainVPerA = valueVPerA;
  saveToStorage();
}

void CalibrationStore::setBaselineVoltage(float valueMv) {
  profile_.baselineVoltageMv = valueMv;
  saveToStorage();
}

void CalibrationStore::setTrustedBaseline(float redAvg, float irAvg, float ratioR,
                                          uint8_t confidence, bool valid) {
  profile_.baselineRedAvg = redAvg;
  profile_.baselineIrAvg = irAvg;
  profile_.userBaselineR = ratioR;
  profile_.baselineConfidence = confidence;
  profile_.baselineValid = valid && (ratioR > 1e-6f);
  saveToStorage();
}

void CalibrationStore::setUserBaseline(float valueR, bool valid) {
  profile_.userBaselineR = valueR;
  profile_.baselineValid = valid && (valueR > 1e-6f);
  saveToStorage();
}

void CalibrationStore::clearUserBaseline() {
  profile_.userBaselineR = 0.0f;
  profile_.baselineRedAvg = 0.0f;
  profile_.baselineIrAvg = 0.0f;
  profile_.baselineConfidence = 0;
  profile_.baselineValid = false;
  saveToStorage();
}

void CalibrationStore::setSessionCounters(uint16_t stableCount,
                                          uint16_t suspiciousCount,
                                          uint16_t badSignalCount) {
  profile_.stableSessionCount = stableCount;
  profile_.suspiciousSessionCount = suspiciousCount;
  profile_.badSignalSessionCount = badSignalCount;
  saveToStorage();
}

void CalibrationStore::saveLastSessionSummary(const SessionSummary& summary) {
  lastSession_ = summary;
  saveSessionSummaryToStorage();
}

bool CalibrationStore::loadLastSessionSummary(SessionSummary& summaryOut) const {
  if (!lastSession_.valid) {
    return false;
  }
  summaryOut = lastSession_;
  return true;
}

void CalibrationStore::loadDefaults() {
  profile_ = CalibrationProfile();
  profile_.schemaVersion = 2;

  // Placeholder values until real bench calibration is supplied.
  profile_.photodiodeSensitivityAw = 0.0f;
  profile_.amplifierGainVPerA = 0.0f;
  profile_.baselineVoltageMv = 1200.0f;
  profile_.baselineRedAvg = 0.0f;
  profile_.baselineIrAvg = 0.0f;
  profile_.baselineConfidence = 0;
  profile_.stableBpmMin = 0;
  profile_.stableBpmMax = 0;
  profile_.userBaselineR = 0.0f;
  profile_.baselineValid = false;
  profile_.stableSessionCount = 0;
  profile_.suspiciousSessionCount = 0;
  profile_.badSignalSessionCount = 0;

  lastSession_ = SessionSummary();
}

void CalibrationStore::loadFromStorage() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }

  if (!prefs.isKey(kSchemaKey)) {
    prefs.end();
    saveToStorage();
    return;
  }

  profile_.schemaVersion = prefs.getUShort(kSchemaKey, profile_.schemaVersion);
  profile_.photodiodeSensitivityAw = prefs.getFloat(kPdKey, profile_.photodiodeSensitivityAw);
  profile_.amplifierGainVPerA = prefs.getFloat(kGainKey, profile_.amplifierGainVPerA);
  profile_.baselineVoltageMv = prefs.getFloat(kVrefKey, profile_.baselineVoltageMv);
  profile_.baselineRedAvg = prefs.getFloat(kBaseRedKey, profile_.baselineRedAvg);
  profile_.baselineIrAvg = prefs.getFloat(kBaseIrKey, profile_.baselineIrAvg);
  profile_.userBaselineR = prefs.getFloat(kBaseRKey, profile_.userBaselineR);
  profile_.baselineConfidence = prefs.getUChar(kBaseConfKey, profile_.baselineConfidence);
  profile_.stableBpmMin = prefs.getUShort(kBpmMinKey, profile_.stableBpmMin);
  profile_.stableBpmMax = prefs.getUShort(kBpmMaxKey, profile_.stableBpmMax);
  profile_.baselineValid = prefs.getBool(kBaseValidKey, profile_.baselineValid);
  profile_.stableSessionCount = prefs.getUShort(kStableSessionsKey, profile_.stableSessionCount);
  profile_.suspiciousSessionCount = prefs.getUShort(kSuspSessionsKey, profile_.suspiciousSessionCount);
  profile_.badSignalSessionCount = prefs.getUShort(kBadSessionsKey, profile_.badSignalSessionCount);

  lastSession_.timestampMs = prefs.getUInt(kSummaryTsKey, 0);
  lastSession_.avgRed = prefs.getFloat(kSummaryRedKey, 0.0f);
  lastSession_.avgIr = prefs.getFloat(kSummaryIrKey, 0.0f);
  lastSession_.ratioR = prefs.getFloat(kSummaryRatioKey, 0.0f);
  lastSession_.bpm = prefs.getUShort(kSummaryBpmKey, 0);
  lastSession_.confidence = prefs.getUChar(kSummaryConfKey, 0);
  lastSession_.riskFlag = prefs.getUChar(kSummaryRiskKey, 0);
  lastSession_.valid = prefs.getBool(kSummaryValidKey, false);

  prefs.end();
}

void CalibrationStore::saveToStorage() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }

  prefs.putUShort(kSchemaKey, profile_.schemaVersion);
  prefs.putFloat(kPdKey, profile_.photodiodeSensitivityAw);
  prefs.putFloat(kGainKey, profile_.amplifierGainVPerA);
  prefs.putFloat(kVrefKey, profile_.baselineVoltageMv);
  prefs.putFloat(kBaseRedKey, profile_.baselineRedAvg);
  prefs.putFloat(kBaseIrKey, profile_.baselineIrAvg);
  prefs.putFloat(kBaseRKey, profile_.userBaselineR);
  prefs.putUChar(kBaseConfKey, profile_.baselineConfidence);
  prefs.putUShort(kBpmMinKey, profile_.stableBpmMin);
  prefs.putUShort(kBpmMaxKey, profile_.stableBpmMax);
  prefs.putBool(kBaseValidKey, profile_.baselineValid);
  prefs.putUShort(kStableSessionsKey, profile_.stableSessionCount);
  prefs.putUShort(kSuspSessionsKey, profile_.suspiciousSessionCount);
  prefs.putUShort(kBadSessionsKey, profile_.badSignalSessionCount);

  prefs.end();

  saveSessionSummaryToStorage();
}

void CalibrationStore::saveSessionSummaryToStorage() const {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }

  prefs.putUInt(kSummaryTsKey, lastSession_.timestampMs);
  prefs.putFloat(kSummaryRedKey, lastSession_.avgRed);
  prefs.putFloat(kSummaryIrKey, lastSession_.avgIr);
  prefs.putFloat(kSummaryRatioKey, lastSession_.ratioR);
  prefs.putUShort(kSummaryBpmKey, lastSession_.bpm);
  prefs.putUChar(kSummaryConfKey, lastSession_.confidence);
  prefs.putUChar(kSummaryRiskKey, lastSession_.riskFlag);
  prefs.putBool(kSummaryValidKey, lastSession_.valid);

  prefs.end();
}

}  // namespace hb
