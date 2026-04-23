#include "scheduler.h"

#include <math.h>

#include "app_config.h"

namespace hb {

namespace {

bool elapsedUs(uint32_t nowUs, uint32_t startUs, uint32_t waitUs) {
  return static_cast<uint32_t>(nowUs - startUs) >= waitUs;
}

float correctSample(int16_t litRaw, int16_t ambientRaw) {
  const float corrected = static_cast<float>(litRaw - ambientRaw);
  return corrected > 0.0f ? corrected : 0.0f;
}

}  // namespace

void MeasurementScheduler::begin(HardwareIO& hw, uint32_t nowUs) {
  hw_ = &hw;
  state_ = SchedulerState::kInit;
  cycleStartUs_ = nowUs;
  stateStartUs_ = nowUs;
  current_ = RawCycleSample();
  redOnly_ = false;
  hw_->setLedState(false, false);
}

void MeasurementScheduler::beginRedOnly(HardwareIO& hw, uint32_t nowUs) {
  hw_ = &hw;
  state_ = SchedulerState::kInit;
  cycleStartUs_ = nowUs;
  stateStartUs_ = nowUs;
  current_ = RawCycleSample();
  redOnly_ = true;
  hw_->setLedState(false, false);
}

void MeasurementScheduler::stop() {
  if (hw_ != nullptr) {
    hw_->allLedsOff();
  }
  state_ = SchedulerState::kInit;
  current_ = RawCycleSample();
  redOnly_ = false;
}

bool MeasurementScheduler::update(uint32_t nowUs, RawCycleSample& sampleOut) {
  if (hw_ == nullptr) {
    return false;
  }

  switch (state_) {
    case SchedulerState::kInit:
      startNewCycle(nowUs);
      return false;

    case SchedulerState::kAmbientSettle:
      if (!elapsedUs(nowUs, stateStartUs_, config::kLedSettleUs)) {
        return false;
      }
      current_.ambientRaw = hw_->readAdcRaw();
      hw_->setLedState(true, false);
      state_ = SchedulerState::kRedSettle;
      stateStartUs_ = nowUs;
      return false;

    case SchedulerState::kRedSettle:
      if (!elapsedUs(nowUs, stateStartUs_, config::kLedSettleUs)) {
        return false;
      }
      current_.redRaw = hw_->readAdcRaw();
      hw_->setLedState(false, false);

      if (redOnly_) {
        // RED-only mode: skip IR, compute corrected RED and finish cycle
        current_.irRaw = 0;
        current_.redCorrected = correctSample(current_.redRaw, current_.ambientRaw);
        current_.irCorrected = 0.0f;
        current_.timestampMs = nowUs / 1000UL;
        current_.valid = true;
        sampleOut = current_;
        state_ = SchedulerState::kWaitNextCycle;
        stateStartUs_ = nowUs;
        return true;
      }

      state_ = SchedulerState::kDarkSettle;
      stateStartUs_ = nowUs;
      return false;

    case SchedulerState::kDarkSettle:
      if (!elapsedUs(nowUs, stateStartUs_, config::kDarkGapUs)) {
        return false;
      }
      hw_->setLedState(false, true);
      state_ = SchedulerState::kIrSettle;
      stateStartUs_ = nowUs;
      return false;

    case SchedulerState::kIrSettle:
      if (!elapsedUs(nowUs, stateStartUs_, config::kLedSettleUs)) {
        return false;
      }
      current_.irRaw = hw_->readAdcRaw();
      hw_->setLedState(false, false);

      current_.redCorrected = correctSample(current_.redRaw, current_.ambientRaw);
      current_.irCorrected = correctSample(current_.irRaw, current_.ambientRaw);
      current_.timestampMs = nowUs / 1000UL;
      current_.valid = true;

      sampleOut = current_;
      state_ = SchedulerState::kWaitNextCycle;
      stateStartUs_ = nowUs;
      return true;

    case SchedulerState::kWaitNextCycle:
      if (elapsedUs(nowUs, cycleStartUs_, config::kMeasurementCyclePeriodUs)) {
        startNewCycle(nowUs);
      }
      return false;

    default:
      state_ = SchedulerState::kInit;
      return false;
  }
}

SchedulerState MeasurementScheduler::state() const { return state_; }

void MeasurementScheduler::startNewCycle(uint32_t nowUs) {
  cycleStartUs_ = nowUs;
  stateStartUs_ = nowUs;
  current_ = RawCycleSample();
  current_.timestampMs = nowUs / 1000UL;
  hw_->setLedState(false, false);
  state_ = SchedulerState::kAmbientSettle;
}

}  // namespace hb
