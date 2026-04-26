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

void CalibrationStore::loadDefaults() {
  profile_ = CalibrationProfile();
  profile_.schemaVersion = 3;  // Bumped to version 3 (removed session tracking)

  // Placeholder values until real bench calibration is supplied.
  profile_.photodiodeSensitivityAw = 0.0f;
  profile_.amplifierGainVPerA = 0.0f;
  profile_.baselineVoltageMv = 1200.0f;
  profile_.baselineRedAvg = 0.0f;
  profile_.baselineIrAvg = 0.0f;
  profile_.baselineConfidence = 0;
  profile_.userBaselineR = 0.0f;
  profile_.baselineValid = false;
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
  profile_.baselineValid = prefs.getBool(kBaseValidKey, profile_.baselineValid);

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
  prefs.putBool(kBaseValidKey, profile_.baselineValid);

  prefs.end();
}

}  // namespace hb
