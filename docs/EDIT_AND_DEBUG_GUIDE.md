# HemePulse Edit and Debug Guide

## Update Note (April 2026)

Current firmware is always-awake and no longer uses deep sleep:

- no runtime deep-sleep transitions,
- no `src/sleep_test_main.cpp`,
- no `lolin_c3_mini_sleep_test` PlatformIO environment,
- periodic reading interval is `kReadingIntervalMs = 20000`.

Treat any deep-sleep troubleshooting references below as legacy context.

## 1. Purpose

This document is a practical, step-by-step playbook for editing and debugging the HemePulse system without breaking cross-module behavior.

Use this guide when you need to:

- tune signal quality,
- diagnose unstable BPM,
- fix BLE issues,
- verify deep sleep,
- adjust baseline behavior,
- understand whether a bug is in hardware, firmware, or app logic.

This guide assumes beginner-to-intermediate experience and intentionally avoids hidden steps.

## Critical Scope Reminder

HemePulse is a trend/risk estimation system using relative red/IR optical changes and baseline drift.

It does **not** provide direct absolute hemoglobin concentration.

Do not interpret it as a lab analyzer.

---

## 2. Safe Edit Rules (Before You Change Anything)

## 2.1 Golden rules

1. Change one thing at a time.
2. Build after every change.
3. Test only one subsystem at a time.
4. Keep a tuning log with date, old value, new value, and observed effect.
5. Never edit firmware packet format without editing app parser in the same session.

## 2.2 Minimal safe-change workflow

1. Create a small change.
2. Run firmware build:
   - `python -m platformio run -e lolin_c3_mini`
3. Run app analysis:
   - `flutter analyze` (inside `hb_monitor_app`)
4. Flash and test a single scenario.
5. Record results.
6. Continue only if behavior improved.

## 2.3 Versioning your tuning

Maintain a plain text log per test session:

```
Date:
Board:
Sensor placement:
Change:
Old value:
New value:
Result:
Keep or revert:
```

---

## 3. Debug One Subsystem at a Time

This section is ordered by dependency. Do not skip ahead.

## 3.1 LED pulsing subsystem

### Goal

Verify that ambient, red, and IR phases are happening at expected cadence.

### Where to inspect

- `src/scheduler.cpp`
- `src/hardware_io.cpp`
- `include/app_config.h` timing constants

### Step-by-step

1. Confirm pin mapping in `app_config.h`:
   - `kRedLedPin`, `kIrLedPin`, `kAdcPin`.
2. Confirm scheduler phases execute.
3. Temporarily enable serial debug if needed.
4. Use scope or logic probe on LED pins.
5. Verify period roughly 20 ms (50 Hz cycle).

### Expected

- LEDs alternate according to scheduler states.
- No stuck-on LED in normal loop.

### If failed

- Check wrong pin numbers.
- Check LED polarity.
- Check wiring and resistor values.

---

## 3.2 ADC acquisition subsystem

### Goal

Verify meaningful ambient, red, and IR reads and corrected values.

### Where to inspect

- `src/hardware_io.cpp`
- `src/scheduler.cpp`
- waveform print in `src/main.cpp`

### Step-by-step

1. Keep `kEnableSerialWaveform = true`.
2. Observe serial stream format:
   - `ir_wave:<value>,red_wave:<value>`
3. Compare no-contact vs contact values.
4. Verify corrected values are not always zero.
5. Check for clipping near ADC max.

### Expected

- Contact changes waveform amplitude.
- IR and red channels both move with pulse and contact pressure.

### If failed

- If flat near zero: optical signal too weak, wrong wiring, poor contact.
- If saturated high: too much gain/current or wrong bias.

---

## 3.3 BPM detection subsystem (App side)

### Goal

Verify app-side peak extraction and BPM estimate are stable for a still user.

### Where to inspect

- `hb_monitor_app/lib/state/app_state.dart`
  - `_estimateBpm()`

### Step-by-step

1. Connect device and collect at least 20 to 30 seconds still data.
2. Verify `_irSeries` has enough points.
3. Check filtered signal and threshold behavior.
4. Verify peak spacing and interval filter limits.
5. Observe BPM consistency over time.

### Expected

- BPM converges to plausible range (40 to 190 guardrail).
- Low confidence periods reduce trust.

### If failed

- Too few peaks: threshold too high or signal too weak.
- Too many peaks: noise/motion or threshold too low.

---

## 3.4 BLE transmission subsystem

### Goal

Confirm packet bytes sent by firmware are correctly parsed by app.

### Where to inspect

- `src/ble_transport.cpp`
- `hb_monitor_app/lib/services/payload_parser.dart`
- `hb_monitor_app/lib/services/ble_service.dart`

### Step-by-step

1. Confirm app can scan and connect.
2. Confirm compact packet characteristic notify is active.
3. Verify packet length is at least 17 bytes.
4. Confirm parser accepts finite ratio and confidence <= 100.
5. Check UI updates with live values.

### Expected

- No parser null returns for valid stream.
- Dashboard metrics change in near-real time.

### If failed

- Connection succeeds but no data: wrong notify characteristic or parser mismatch.
- Frequent parser drops: corrupted payload length/order.

---

## 3.5 Deep sleep subsystem

### Goal

Confirm timer wake and GPIO9 manual wake behavior.

### Where to inspect

- `src/main.cpp` state machine and `enterDeepSleep()`
- `src/sleep_test_main.cpp` for isolated verification
- `include/app_config.h` sleep constants

### Step-by-step

1. Flash sleep test environment and verify 20-second wake cycle.
2. Flash main firmware and observe `sleep_enter_ms` logs.
3. Verify timer wake interval matches 20 seconds.
4. Verify manual wake pin logic with GPIO9 short to GND.
5. Confirm device returns to sleep after manual/connected windows.

### Expected

- predictable wake-sample-advertise-sleep cycle.

### If failed

- continuous awake: state window conditions not reached.
- no manual wake: pin/wiring/pull-up issue.

---

## 3.6 Baseline storage subsystem

### Goal

Confirm baseline capture, save, reload, and synchronization with app.

### Where to inspect

- `src/calibration_store.cpp`
- `src/main.cpp` baseline capture functions
- app baseline handling in `app_state.dart`

### Step-by-step

1. Connect app and trigger baseline capture.
2. Hold still for capture duration.
3. Verify baseline validity becomes true.
4. Reboot device and reconnect.
5. Verify baseline persists and warning behavior changes.

### Expected

- baseline survives reboot.
- warning no longer stuck at baseline-needed.

### If failed

- insufficient confident samples,
- NVS save/load issue,
- parser mismatch for baseline payload.

---

## 3.7 App display subsystem

### Goal

Confirm UI reflects state model values correctly.

### Where to inspect

- `dashboard_screen.dart`
- `trend_chart.dart`
- `hemoglobin_status_screen.dart`
- `scan_screen.dart`

### Step-by-step

1. Verify scan list appears when scanning.
2. Connect and observe metric card updates.
3. Confirm trend chart redraws with new samples.
4. Confirm warning label reflects state warning enum.
5. Confirm session summary appears after disconnect.

### Expected

- no stale values after reconnect.

### If failed

- check provider state updates and notify listeners.

---

## 4. Hardware vs Firmware vs App Triage

Use this triage matrix before changing thresholds.

| Symptom | Most likely layer | First check |
|---|---|---|
| No waveform change with finger contact | Hardware | LED wiring, photodiode orientation, pin map |
| BLE scan finds nothing | Firmware or hardware state | advertising window timing, deep sleep state, board power |
| Connected but no live data | Firmware/app contract | packet characteristic and parser offsets |
| Values update but BPM unstable | Signal quality + app logic | motion, thresholding, contact pressure |
| Warning always baseline-needed | Baseline pipeline | baseline capture completion and persistence |
| Device drains battery fast | Firmware/power | deep sleep entry and BLE shutdown path |

General rule:

1. Hardware first for impossible/flat/saturated signals.
2. Firmware next for timing, packet generation, sleep control.
3. App next for parsing, filters, visualization.

---

## 5. Symptom Interpretation Guide

## 5.1 Noisy signal

Likely causes:

- motion,
- ambient leakage,
- weak grounding,
- high gain instability.

Actions:

1. stabilize sensor physically.
2. shield from ambient light.
3. reduce front-end gain if clipping/noise bursts.
4. verify app motion detection threshold behavior.

## 5.2 Flat signal

Likely causes:

- wrong GPIO,
- LEDs off or reversed,
- no photodiode response,
- ADC read not from expected pin.

Actions:

1. verify pin constants and wiring.
2. inspect LED current path.
3. verify corrected values differ from ambient.

## 5.3 Saturated signal

Likely causes:

- excessive LED current,
- TIA gain too high,
- DC bias too near rail.

Actions:

1. increase LED resistor or reduce duty.
2. lower TIA feedback resistor.
3. re-center bias/reference.

## 5.4 Unstable BPM

Likely causes:

- noisy IR waveform,
- poor peak thresholding,
- motion,
- short sample windows.

Actions:

1. capture longer still segment.
2. inspect peak interval distribution.
3. adjust thresholds only after waveform quality is verified.

## 5.5 No BLE scan result

Likely causes:

- device sleeping,
- wrong firmware flashed,
- advertising not started,
- power/cable instability.

Actions:

1. use manual wake mode (GPIO9 to GND).
2. verify board is running main environment.
3. check serial logs around wake and advertising windows.

## 5.6 ESP32 heating

Likely causes:

- excessive LED current,
- never entering deep sleep,
- continuous BLE duty.

Actions:

1. verify sleep entry path.
2. inspect LED current estimation.
3. reduce active window for tests if needed.

## 5.7 Device not sleeping

Likely causes:

- still in manual active window,
- baseline capture delay guard active,
- state transition not hitting sleep condition.

Actions:

1. log current state and elapsed window times.
2. verify baseline capture flags.
3. verify `gSleepRequested` usage and transitions.

---

## 6. Calibration Tuning Guide

## 6.1 LED current resistor changes

Approximate formula:

$I_{LED} = (V_{supply} - V_f) / R$

Effects of lowering `R`:

- increases emitted light,
- increases signal amplitude,
- increases power and heating,
- increases saturation risk.

Effects of raising `R`:

- reduces power and saturation risk,
- may reduce signal below noise floor.

## 6.2 TIA feedback resistor changes

Approximate small-signal relation:

$V_{out} \approx I_{PD} * R_f$

Higher `R_f`:

- larger voltage per photodiode current,
- better small-signal visibility,
- greater saturation risk,
- possible bandwidth/noise tradeoff.

Lower `R_f`:

- less saturation,
- smaller pulsatile swing,
- harder peak detection.

## 6.3 Photodiode voltage and noise interpretation

- If swing is tiny and noisy, increase optical path quality first (contact/light isolation), then consider gain.
- If baseline near rail, adjust bias or reduce gain/current.

## 6.4 Tuning order recommendation

1. mechanical contact and light shielding.
2. LED current.
3. TIA gain.
4. firmware confidence and motion thresholds.
5. app BPM/warning thresholds.

Never reverse this order in early debugging.

---

## 7. Constants: What to Change and What Not to Hardcode

## 7.1 Constants you should not hardcode in random files

Always keep these centralized:

- GPIO pins,
- timing windows,
- confidence thresholds,
- warning drift thresholds,
- BLE notify intervals,
- packet field assumptions.

Use `include/app_config.h` and app-level config/state constants.

## 7.2 Constants you can tune during testing

Firmware candidates:

- `kLedSettleUs`
- `kDarkGapUs`
- `kEmaAlpha`
- motion thresholds
- drift thresholds
- streak thresholds
- advertising/session windows

App candidates:

- BPM threshold multipliers,
- motion normalization cutoffs,
- warning drift/streak settings.

## 7.3 Constants you should avoid changing first

- BLE packet byte order and offsets.
- enum integer values for warning mapping.
- baseline validity semantics.
- sleep wake source setup logic.

## 7.4 Compare old vs new constants procedure

1. Copy original constants into test log.
2. Edit one constant group.
3. Record observed waveform quality, confidence distribution, BPM stability, warning behavior.
4. Revert if no clear gain.

---

## 8. Step-by-Step Debug Workflow (Full Playbook)

Use this every debug day.

1. Define one target problem.
2. Select one subsystem only.
3. Add minimal diagnostic prints.
4. Run controlled test with still subject.
5. Capture logs and metrics.
6. Compare values against expected ranges.
7. Apply one small change.
8. Re-test same scenario.
9. Keep or revert.
10. Only then move to next subsystem.

### Print statement strategy

- Keep prints structured and grep-friendly.
- Include units in labels.
- Remove temporary high-rate prints after diagnosis.

### Range verification strategy

Track at minimum:

- ambient raw range,
- corrected red/ir range,
- confidence distribution,
- ratio drift behavior,
- BPM interval consistency.

### Reintegration rule

After isolated fix:

1. re-enable adjacent subsystem,
2. run full path test,
3. ensure no regression in previous module.

---

## 9. Manual Calculation Templates

Copy these templates during debugging sessions.

## 9.1 LED current estimate

Given:

- supply voltage `V_supply = ____`
- LED forward voltage `Vf = ____`
- resistor `R = ____`

Compute:

$I = (V_supply - Vf) / R = ____ A = ____ mA$

## 9.2 Photodiode output estimate

Given:

- responsivity `S = ____ A/W`
- optical power variation `DeltaP = ____ W`
- TIA resistor `Rf = ____ ohm`

Compute:

$DeltaI = S * DeltaP = ____ A$

$DeltaV = DeltaI * Rf = ____ V$

## 9.3 AC/DC extraction concept

Given sample window values:

- `DC ~ mean(window)`
- `AC ~ (max(window) - min(window))/2`

Compute:

$redNorm = AC_red / DC_red$

$irNorm = AC_ir / DC_ir$

$R = redNorm / irNorm$

Note: active runtime currently uses simplified EMA/ratio path, but this template helps reason about optical feature quality.

## 9.4 BPM from peak intervals

Given peak timestamps (ms):

`t1 = ____`, `t2 = ____`, `t3 = ____`

Intervals:

`d1 = t2 - t1`, `d2 = t3 - t2`

Average interval:

$avg = (d1 + d2 + ...)/N$

BPM:

$BPM = 60000 / avg$

## 9.5 Trend drift from baseline

Given:

- baseline ratio `Rb = ____`
- current ratio `Rc = ____`

Compute:

$drift = |(Rc - Rb) / Rb|$

Interpret using configured thresholds and streak logic.

---

## 10. Experimental Validation Plan

## 10.1 Still-user validation

1. Seat subject comfortably.
2. Ensure stable sensor contact.
3. Capture 60-second baseline.
4. Observe confidence and BPM stability.
5. Save session and compare drift near zero.

## 10.2 Motion validation

1. Repeat with intentional mild motion.
2. Confirm motion flag increases.
3. Confirm confidence drops.
4. Confirm warning escalation is suppressed unless repeated stable drift occurs.

## 10.3 Placement variation

Test multiple placements:

- ear lobe,
- finger side,
- slight pressure changes.

Record which placement provides highest confidence and most stable waveform.

## 10.4 Red vs IR comparison

- Compare amplitude and stability channel by channel.
- IR should usually provide stronger pulse periodicity.
- If red appears stronger, inspect LED drive and optical geometry.

---

## 11. What Not To Change First

1. Do not change BLE schema and DSP logic in one commit.
2. Do not tune warning thresholds before validating raw signal quality.
3. Do not tune BPM limits before fixing motion/noise issues.
4. Do not modify multiple timing windows at once.
5. Do not edit baseline logic and session trend logic together in first pass.
6. Do not treat one successful run as final; repeat under same conditions.

---

## 12. Suggested MCP/Tooling Setup for Better Coding and Debugging

For this project stack, use:

- PlatformIO extension (firmware build/flash/monitor),
- Dart extension,
- Flutter extension,
- optional serial plotter for waveform trend checks.

The repository already includes recommended VS Code extensions in `.vscode/extensions.json`.

For communication validation, always verify firmware packet schema and app parser together after any related edit.

---

## 13. Final Debug Session Checklist

Use this before ending each debug session.

- [ ] I changed only one subsystem or one constant group.
- [ ] Firmware build passed.
- [ ] Flutter analyze passed.
- [ ] Device connected and packet parsing worked.
- [ ] I verified waveform quality before threshold tuning.
- [ ] I verified confidence and motion behavior under still and motion tests.
- [ ] I verified baseline validity behavior.
- [ ] I verified warning behavior over repeated samples/sessions.
- [ ] I captured logs and recorded old/new constants.
- [ ] I documented keep/revert decision for each change.

---

## 14. Troubleshooting Quick Commands

From repository root:

- Main firmware build:
  - `python -m platformio run -e lolin_c3_mini`
- Sleep test build:
  - `python -m platformio run -e lolin_c3_mini_sleep_test`
- Main firmware upload:
  - `python -m platformio run -e lolin_c3_mini -t upload`
- Sleep test upload:
  - `python -m platformio run -e lolin_c3_mini_sleep_test -t upload`

From app folder `hb_monitor_app`:

- Analyze:
  - `flutter analyze`
- Run:
  - `flutter run`

If upload fails, check serial port occupancy first (busy COM port is common).

---

## 15. Practical Closing Guidance

When debugging biomedical optical prototypes, the fastest path is always:

1. verify physics path (light, sensor, analog range),
2. verify firmware timing and packet validity,
3. verify app parsing and trend logic,
4. only then tune warning thresholds.

If you keep this order and log every change, you will avoid most repeated failure loops.