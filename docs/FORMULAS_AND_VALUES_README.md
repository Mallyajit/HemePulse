# HemePulse Formulas and Values README

## 1) Why this document exists

This README is a single technical reference for all major formulas, thresholds, constants, and calculation logic used in HemePulse.

It is designed to answer three practical questions quickly:

1. What numerical values are being used right now?
2. Which equations are physically grounded vs heuristic?
3. How should values be tuned without breaking the pipeline?

This is intentionally detailed and includes both firmware and app layers.

## Critical scientific boundary

HemePulse uses reflective red/IR optical trends and baseline-relative drift logic.

It does not directly output absolute clinical hemoglobin concentration.

Any absolute concentration claim would require controlled calibration against clinical reference datasets, with robust compensation for tissue path, perfusion variability, sensor geometry, and analog front-end behavior.

---

## 2) Fast architecture context for formulas

The formula flow is:

1. Firmware acquires ambient, red, and IR ADC values in each cycle.
2. Firmware applies ambient subtraction and EMA smoothing.
3. Firmware derives ratio, confidence, and local warning.
4. Firmware packs compact BLE packet.
5. App parses packet and runs additional BPM/motion/confidence/warning refinement.
6. App creates trend and session summaries.

This means formula responsibility is split:

- Firmware: acquisition, real-time quality gate, low-power immediate warning.
- App: richer temporal analytics and user-facing interpretation.

---

## 3) Firmware constants (current values)

Source: include/app_config.h

### 3.1 GPIO and ADC mapping

- kRedLedPin = 1
- kIrLedPin = 2
- kAdcPin = 0
- kAdcResolutionBits = 12
- kAdcReferenceMv = 3300.0
- kAdcFullScale = 4095.0

### 3.2 Timing and scheduler constants

- kMeasurementCyclePeriodUs = 20000 (50 Hz)
- kLedSettleUs = 1500
- kDarkGapUs = 1000

### 3.3 Signal/history framework values

- kSampleHistoryBufferSlots = 256
- kAcDcWindowSamples = 50
- kPeakHistorySlots = 12
- kRatioConsistencyWindow = 40
- kBandpassHpHz = 0.5
- kBandpassLpHz = 4.0
- kSampleRateHz = 50.0

### 3.4 BPM and peak guardrails

- kMinBpm = 40
- kMaxBpm = 190
- kMinPeakSpacingMs = 60000 / 190 = 315 ms (integer division result)
- kPulseWindowMs = 8000
- kPeakThresholdScale = 0.35
- kPeakThresholdFloor = 2.0

### 3.5 Confidence and motion constants

- kConfidencePoor = 35
- kConfidenceFair = 55
- kConfidenceGood = 75
- kMinConfidenceForBaseline = 65

- kEmaAlpha = 0.18
- kMinIrSignal = 20.0
- kMinRedSignal = 12.0
- kMotionRatioJumpThreshold = 0.09
- kMotionSignalJumpThreshold = 0.28

### 3.6 Drift and warning constants

- kStableDriftThreshold = 0.05
- kElevatedDriftThreshold = 0.09
- kHighDriftThreshold = 0.14
- kElevatedStreakThreshold = 4
- kHighStreakThreshold = 8

### 3.7 Baseline capture constants

- kBaselineCaptureDurationMs = 60000
- kMinBaselineSamples = 200
- kBaselineCapturePaddingMs = 10000

### 3.8 Session/sleep/connection constants

- kSessionActiveMs = 25000
- kNoConnectionSleepMs = 8000
- kSleepIntervalMs = 20000
- kManualWakePin = 9
- kManualActiveWindowMs = 60000
- kAutoMeasurementWindowMs = 2000
- kBleAdvertisingWindowMs = 12000
- kConnectedSessionWindowMs = 15000
- kEnableDeepSleep = true
- kMinSessionSamples = 30

### 3.9 BLE and debug constants

- kBleNotifyIntervalMs = 20
- kBleDeviceName = "HemePulse-C3"
- kEnableSerialDebug = false
- kEnableSerialWaveform = true
- kWaveformPrintIntervalMs = 20

---

## 4) Firmware equations and logic math

Source: src/main.cpp and src/scheduler.cpp

### 4.1 ADC to millivolts mapping

\[
V_{mV} = raw \times \left(\frac{kAdcReferenceMv}{kAdcFullScale}\right)
\]

With current constants:

\[
V_{mV} = raw \times \left(\frac{3300}{4095}\right)
\]

### 4.2 Ambient subtraction

For each lit phase:

\[
redCorrected = \max(redRaw - ambientRaw, 0)
\]

\[
irCorrected = \max(irRaw - ambientRaw, 0)
\]

This removes static ambient contribution and clamps negative corrected values to zero.

### 4.3 Safe ratio

\[
R = \frac{red}{ir}
\]

But firmware safeguards apply:

- if \(ir \le 1e-6\), ratio is forced to 0.
- if ratio is non-finite, ratio is forced to 0.

### 4.4 EMA smoothing

For each channel (red and IR):

\[
EMA_{new} = EMA_{old} + \alpha (x - EMA_{old})
\]

where \(\alpha = 0.18\).

Interpretation:

- higher alpha => faster response, more noise sensitivity.
- lower alpha => smoother but slower adaptation.

### 4.5 Firmware motion-likelihood heuristics

Using previous EMA and ratio values:

\[
redJump = \frac{|red_{sample} - redAvg|}{\max(1, redAvg)}
\]

\[
irJump = \frac{|ir_{sample} - irAvg|}{\max(1, irAvg)}
\]

\[
ratioJump = |R - R_{prev}|
\]

Motion likely when any condition is true:

- ratioJump > 0.09
- redJump > 0.28
- irJump > 0.28

### 4.6 Firmware confidence score formula

Firmware uses additive penalties from a base of 100:

\[
score = 100 - P_{irLow} - P_{redLow} - P_{ratioInvalid} - P_{ambient} - P_{motion}
\]

Current penalties:

- irAvg < 20 => -35
- redAvg < 12 => -22
- ratio <= 1e-6 => -20
- ambientVsIr > 0.88 => -18
- motionLikely => -30

Then clamp to [0, 100].

### 4.7 Drift metric (firmware and app family)

\[
drift = \left|\frac{R - R_{baseline}}{R_{baseline}}\right|
\]

This is the central trend indicator and warning driver.

### 4.8 Firmware warning thresholds

Warning decision order:

1. If no baseline => Baseline Needed
2. If confidence < 35 or motion => Low Signal
3. Else drift logic + streak counters:
   - drift < 0.05 => Normal and decay suspicious streak
   - drift >= 0.14 with suspiciousStreak >= 8 => High
   - drift >= 0.09 with suspiciousStreak >= 4 => Elevated
   - else Normal

### 4.9 Firmware session risk summary logic

After accumulating session samples:

- valid session if sampleCount >= 30
- riskFlag:
  - 2 when invalid or badSignal dominates
  - 1 when suspicious weight dominates
  - 0 otherwise

---

## 5) App-side formulas and thresholds

Source: hb_monitor_app/lib/state/app_state.dart

### 5.1 App BPM estimation pipeline

The app estimates BPM using IR tail analysis:

1. Use up to last 300 IR samples.
2. Detrend each point by subtracting local 5-point moving average:

\[
filtered_i = ir_i - \frac{ir_{i-2}+ir_{i-1}+ir_i+ir_{i+1}+ir_{i+2}}{5}
\]

3. Compute mean and standard deviation of filtered sequence.
4. Dynamic threshold:

\[
threshold = mean + \max(2.0, 0.45 \times stddev)
\]

5. Peak condition at index i:

- filtered[i] > filtered[i-1]
- filtered[i] >= filtered[i+1]
- filtered[i] > threshold

6. Peak spacing gate: at least 320 ms between accepted peaks.
7. Interval filter: keep intervals in [300, 1500] ms.
8. Convert interval to BPM:

\[
BPM = round\left(\frac{60000}{avgIntervalMs}\right)
\]

9. Final guardrail: valid BPM only in [40, 190].

### 5.2 App motion-likelihood formula

Using recent ~20 samples:

\[
irNorm = \frac{mean(|\Delta ir|)}{\max(5, mean(|ir|))}
\]

\[
redNorm = \frac{mean(|\Delta red|)}{\max(5, mean(|red|))}
\]

\[
ratioNorm = mean(|\Delta ratio|)
\]

Motion likely when:

- irNorm > 0.22 OR
- redNorm > 0.22 OR
- ratioNorm > 0.07

### 5.3 App confidence refinement

App starts from packet confidence and applies additional penalties:

- bpm == 0 => -12
- irCorrected < 18 => -20
- redCorrected < 10 => -12
- signalSpan(ir, tail=60) < 8 => -12
- motionLikely => -25

Then clamp to [0,100].

### 5.4 App warning thresholds (not identical to firmware)

App warning logic is stricter in some places:

1. no baseline => Baseline Needed
2. motionLikely or confidence < 35 => Low Signal
3. compute drift
4. suspicious streak increases when drift >= 0.07 and confidence >= 55
5. suspicious streak decays otherwise
6. High if drift >= 0.12 and (streak >= 8 or repeated session drift)
7. Elevated if drift >= 0.07 and streak >= 3
8. else Normal

### 5.5 Repeated session drift rule (app)

Inspects last 4 sessions:

- include only sessions with confidence >= 55 and not motionLikely
- count sessions with drift >= 0.07
- repeatedSessionDrift = true when count >= 3

---

## 6) BLE payload formulas and value mapping

### 6.1 Compact packet size

Payload size is fixed at 17 bytes in firmware and expected as minimum 17 bytes in app parser.

### 6.2 Compact packet field map

1. bytes 0..3: timestampMs (uint32 little-endian)
2. bytes 4..5: ambientRaw (int16 little-endian)
3. bytes 6..7: redCorrected (int16 little-endian)
4. bytes 8..9: irCorrected (int16 little-endian)
5. bytes 10..13: ratioR (float32 little-endian)
6. byte 14: confidence (uint8)
7. byte 15: warning (uint8)
8. byte 16: flags (uint8)

### 6.3 Baseline payload map

5-byte payload:

- bytes 0..3 baselineRatio (float32 little-endian)
- byte 4 validity flag (0 or 1)

### 6.4 Flag semantics currently used

Firmware sets flag bits:

- 0x02: base marker bit used by current protocol
- 0x04: low confidence condition
- 0x08: elevated/high warning
- 0x10: baseline valid
- 0x20: baseline capture active
- 0x40: motion likely

App may additionally set 0x01 internally for UI convenience when bpm > 0.

---

## 7) Physical/electronics formulas used during calibration thinking

These are engineering formulas for interpretation and setup tuning.

### 7.1 LED current estimation

\[
I_{LED} = \frac{V_{supply} - V_f}{R}
\]

Where:

- \(V_{supply}\): drive voltage
- \(V_f\): LED forward voltage at operating current
- \(R\): series resistor

### 7.2 Photodiode current estimation

\[
I_{PD} = Responsivity \times OpticalPower
\]

### 7.3 TIA output relation (first-order)

\[
V_{out} \approx V_{bias} \pm I_{PD} \times R_f
\]

Higher \(R_f\): more gain, more saturation/noise sensitivity.

### 7.4 Pulsatile swing estimation

Given current variation \(\Delta I\):

\[
\Delta V \approx \Delta I \times R_f
\]

This helps estimate whether pulsatile signal exceeds ADC noise floor.

### 7.5 Sampling relations

At 50 Hz sampling:

\[
T_s = \frac{1}{50} = 20\,ms
\]

Nyquist frequency:

\[
f_N = \frac{f_s}{2} = 25\,Hz
\]

Cardiac band 0.5 to 4 Hz is well below Nyquist.

---

## 8) Worked numeric examples with current project assumptions

### 8.1 ADC conversion example

If raw ADC = 2048:

\[
V_{mV} = 2048 \times \frac{3300}{4095} \approx 1650.4\,mV
\]

### 8.2 LED current (red) example

Assume \(V_{supply}=3.3V\), \(V_f=1.9V\), \(R=150\Omega\):

\[
I = \frac{3.3-1.9}{150} = 0.00933A = 9.33mA
\]

### 8.3 LED current (IR) example

Assume \(V_f=1.25V\), same resistor:

\[
I = \frac{3.3-1.25}{150} = 13.67mA
\]

### 8.4 Drift example

Given baseline \(R_b = 1.020\), current \(R=1.110\):

\[
drift = \left|\frac{1.110-1.020}{1.020}\right| = 0.0882 = 8.82\%
\]

Interpretation:

- firmware elevated threshold is 9% with streak gate.
- app elevated threshold is 7% with streak gate.

### 8.5 BPM interval example

If intervals are 820 ms:

\[
BPM = \frac{60000}{820} = 73.17 \rightarrow 73
\]

---

## 9) Classification: formula confidence levels

### 9.1 Physics-grounded equations

- Ohm-law LED current relation
- photodiode current from responsivity and optical power
- TIA gain relation
- sampling and Nyquist relations
- normalized drift formula

### 9.2 Implementation-grounded equations

- ambient subtraction with floor at zero
- EMA update equation
- peak interval to BPM conversion

### 9.3 Heuristic thresholds (prototype-tunable)

- motion cutoffs
- confidence penalties
- suspicious streak lengths
- drift escalation thresholds

These are behavior-shaping values, not universal physiological constants.

---

## 10) Tuning guidance by constant family

### 10.1 Acquisition timing

- kLedSettleUs, kDarkGapUs, kMeasurementCyclePeriodUs

Tune when:

- phase cross-talk appears,
- readings look unstable after LED transitions,
- cycle duty needs power/performance balancing.

### 10.2 Signal quality and confidence

- kMinIrSignal, kMinRedSignal
- ambient contamination gate
- motion thresholds

Tune when:

- confidence is always low despite stable contact,
- or falsely high during noisy conditions.

### 10.3 Warning sensitivity

Firmware:

- kStableDriftThreshold
- kElevatedDriftThreshold
- kHighDriftThreshold
- streak thresholds

App:

- drift trigger 0.07/0.12
- confidence gate 55
- streak and repeated session logic

Tune when:

- too many false alerts,
- or delayed warning during sustained drift.

### 10.4 Sleep/power behavior

- kSleepIntervalMs
- kManualActiveWindowMs
- kBleAdvertisingWindowMs
- kConnectedSessionWindowMs

Tune when:

- battery life is poor,
- reconnection windows are too short,
- UX appears unresponsive.

---

## 11) Known firmware-app threshold mismatches (intentional but important)

Some values differ by design between firmware and app:

- Firmware elevated drift starts at 0.09; app starts suspicious at 0.07.
- Firmware high drift threshold is 0.14; app high is 0.12.
- Firmware confidence penalties differ from app post-processing penalties.

This can produce slight differences between immediate device-side state and app-side displayed warning.

This is acceptable if documented and intentional.

If you want tighter alignment, tune both layers together.

---

## 12) Manual debug worksheet templates

### 12.1 Sample quality worksheet

- ambient range: _____ to _____
- red corrected range: _____ to _____
- ir corrected range: _____ to _____
- ratio range: _____ to _____
- confidence mean/min: _____ / _____
- motion events count: _____

### 12.2 Baseline validation worksheet

- baseline capture start time: _____
- accepted baseline samples: _____
- baseline R: _____
- baseline valid flag: true/false
- post-reboot baseline retained: true/false

### 12.3 Warning validation worksheet

- drift values over time: _____
- suspicious streak progression: _____
- warning transitions observed: _____
- motion present during warning? yes/no

### 12.4 BPM validation worksheet

- detected peak timestamps: _____
- filtered intervals: _____
- computed BPM: _____
- accepted by guardrail? yes/no

---

## 13) Common mistakes when editing formulas and values

1. Changing BLE packet field order without updating parser offsets.
2. Changing multiple threshold families in one test run.
3. Tuning warning thresholds before fixing optical signal quality.
4. Ignoring motion while evaluating drift logic.
5. Treating heuristic constants as universal biomedical constants.
6. Assuming placeholder calibration values are physically calibrated.

---

## 14) Minimum verification checklist after any value change

- Firmware build succeeds.
- App analysis succeeds.
- BLE packet parse succeeds for all incoming samples.
- Confidence remains plausible during still test.
- Motion condition lowers trust as expected.
- Baseline capture still completes with enough valid samples.
- Warning transitions behave consistently with drift and streak rules.
- Sleep/wake timing still matches intended user flow.

---

## 15) Quick reference table

| Category | Core formula/value | Current value(s) |
|---|---|---|
| ADC scaling | \(V_{mV}=raw*(3300/4095)\) | 12-bit, 3300 mV ref |
| Sample rate | \(f_s=50Hz\) | 20 ms period |
| EMA | \(EMA_{new}=EMA_{old}+0.18*(x-EMA_{old})\) | alpha=0.18 |
| Firmware low confidence gate | confidence < 35 | kConfidencePoor=35 |
| Firmware drift thresholds | 0.05 / 0.09 / 0.14 | stable/elevated/high |
| App drift thresholds | 0.07 / 0.12 | elevated/high candidate |
| Baseline capture duration | 60000 ms | 60 seconds |
| Baseline minimum samples | >= 200 | kMinBaselineSamples |
| BLE compact payload | 17 bytes | timestamp+signals+ratio+status |
| BLE baseline payload | 5 bytes | baseline float + validity |
| Sleep interval | 20000 ms | 20 seconds |
| Manual wake pin | GPIO9 low wake | kManualWakePin=9 |

---

## 16) Final practical note

This README gives you exact current numbers and equations, but robust behavior still depends on the physical stack:

- LED drive and optical path,
- photodiode/TIA operating region,
- sensor contact stability,
- ambient light isolation,
- motion conditions,
- per-device calibration quality.

Use equations to guide engineering decisions, and use repeated controlled experiments to validate every threshold change.
