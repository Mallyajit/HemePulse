# HemePulse Project Plan and Codebase Reference

## Update (April 2026): Sensor Node + App Analytics Split

The architecture has moved to a two-layer model:
- ESP32-C3: always-awake sensor node (pulse LEDs, ADC sampling, ambient subtraction,
  tiny smoothing/confidence, baseline storage, BLE packet send at periodic cadence).
- Flutter app: BPM engine, trend analysis, multi-session comparison,
  motion-noise discrimination, charting, and final interpretation.

A compact BLE packet characteristic is now used:
- `4f9c0109-a1f2-4c31-98cb-1cce5caa1009`

Runtime note:
- sleep-test firmware target has been removed.
- main firmware stays active and publishes periodic Hb snapshots every 20 seconds.
- pulse-check mode switches to RED-only sampling and continues until explicit stop/disconnect.

## 1) Mission and Scientific Scope

This project implements a non-invasive ear-lobe PPG pipeline on ESP32-C3 and a Flutter companion app.

Critical scope boundary:
- The firmware and app **do not compute absolute hemoglobin concentration**.
- The system computes a **Hemoglobin Risk Index** from dual-wavelength optical trend behavior.
- The ratio-based logic is baseline-relative and confidence-gated.

Scientific assumptions encoded in current implementation:
- RED and IR values are sampled in a time-multiplexed sequence with ambient subtraction.
- AC and DC components are extracted from corrected channels.
- Normalized ratio uses:
  - `red_norm = AC_red / DC_red`
  - `ir_norm = AC_ir / DC_ir`
  - `R = red_norm / ir_norm`
- In pulse-check mode, app-side BPM is computed from corrected RED signal drops.
- Risk is derived from drift in `R` relative to per-user baseline, not from direct Hb conversion.

## 2) Hardware Mapping and Runtime Decisions

Configured hardware mapping:
- RED LED: GPIO 1
- IR LED: GPIO 2
- Photodiode + OPA analog output: GPIO 0 (ADC)

Runtime decisions implemented:
- Full cycle target: 25 Hz (`40 ms` ambient+RED+IR cycle)
- RED-only pulse mode target: 50 Hz (`20 ms` cycle)
- BLE stack: NimBLE-Arduino
- BLE telemetry notify cadence: 50 Hz (`20 ms` throttle)
- Baseline workflow: 60-second capture window with confidence threshold

## 3) Firmware Architecture (PlatformIO)

Firmware root: `HemePulse/`

### 3.1 Build and Dependencies

### `platformio.ini`
Purpose:
- Defines board (`lolin_c3_mini`) and framework (`arduino`).
- Adds NimBLE dependency:
  - `h2zero/NimBLE-Arduino @ ^1.4.2`
- Keeps USB CDC flags for serial debug output.

### 3.2 Shared Configuration and Types

### `include/app_config.h`
Purpose:
- Central source for configurable constants.
- Includes pin mapping, ADC scaling, cycle timing, window sizes, BPM bounds, confidence thresholds, baseline timing, risk thresholds, BLE cadence.

Key configurable constants:
- `kMeasurementCyclePeriodUs = 40000` (25 Hz)
- `kLedPulsePeriodUs = 20000` (50 Hz in RED-only pulse mode)
- `kLedSettleUs = 4000`
- `kDarkGapUs = 1000`
- `kBleNotifyIntervalMs = 20`
- `kBaselineCaptureDurationMs = 60000`
- `kMinConfidenceForBaseline = 65`

### `include/types.h`
Purpose:
- Shared domain types for all modules.

Main structures:
- `RawCycleSample`: ambient, red, ir, corrected values, timestamp.
- `ProcessedMetrics`: AC/DC, normalized values, ratio R, bandpassed IR, noise.
- `PulseMetrics`: BPM, beat flag, peak stability.
- `QualityMetrics`: confidence + sub-scores.
- `RiskMetrics`: baseline state, drift EMAs, risk score, warning enum.
- `TelemetryPacket`: minimal BLE payload fields.
- `CalibrationProfile`: placeholder hardware parameters and baseline.
- `BleCommand`: parsed control commands from app.

Enums:
- `WarningState`: normal/elevated/high/low-signal/baseline-needed.
- `CommandType`: snapshot, baseline control, calibration placeholder updates.

### `include/ring_buffer.h`
Purpose:
- Generic fixed-size circular buffer utility used by acquisition history, AC/DC windows, peak timestamps, and ratio consistency windows.

### 3.3 Hardware and Scheduling

### `include/hardware_io.h` and `src/hardware_io.cpp`
Purpose:
- Minimal hardware abstraction for LED drive and ADC read.

Implemented behavior:
- Initializes LED GPIO directions and default OFF state.
- Configures ADC resolution and attenuation.
- Provides `readAdcRaw()` and raw-to-mV helper.

### `include/scheduler.h` and `src/scheduler.cpp`
Purpose:
- Non-blocking time state machine for one complete measurement cycle.

Sequence implemented:
1. LED OFF settle -> read ambient
2. RED ON settle -> read red
3. LEDs OFF dark gap
4. IR ON settle -> read ir
5. LEDs OFF and cycle complete

Outputs:
- One `RawCycleSample` per complete cycle with ambient-subtracted:
  - `redCorrected = max(red - ambient, 0)`
  - `irCorrected = max(ir - ambient, 0)`

Notes:
- No blocking `delay()` loops.
- Cycle start cadence enforced using elapsed time checks.

### 3.4 Signal Processing and Physiology Features

### `include/signal_processing.h` and `src/signal_processing.cpp`
Purpose:
- Compute AC/DC components, normalized metrics, ratio R, and IR bandpassed waveform.

Implemented logic:
- Stores corrected RED/IR samples in rolling windows.
- DC: moving mean over window.
- AC: half peak-to-peak over same window.
- Normalization and ratio:
  - `redNorm = redAc / redDc`
  - `irNorm = irAc / irDc`
  - `ratioR = redNorm / irNorm`
- Bandpass (conceptual pipeline):
  - first-order high-pass + first-order low-pass on IR corrected signal.
- Noise estimate from absolute derivative of bandpassed IR.

### `include/pulse_detector.h` and `src/pulse_detector.cpp`
Purpose:
- Detect IR pulse peaks and compute BPM.

Implemented logic:
- Local peak detection on bandpassed IR (`prev2`, `prev1`, `current`).
- Adaptive threshold floor plus scale from IR AC amplitude.
- Refractory period using minimum spacing from max BPM.
- BPM from average intervals in recent window.
- Stability score from interval coefficient of variation.

### `include/quality_score.h` and `src/quality_score.cpp`
Purpose:
- Convert signal properties into confidence score (0-100).

Sub-scores implemented:
- Amplitude score from perfusion-like ratio (`irAc/irDc`).
- Noise score from SNR estimate (`irAc/noiseLevel`).
- Stability score from peak interval stability.
- Consistency score from short-term ratio-R variability.

Final confidence:
- Weighted combination with BPM validity penalty.

### `include/risk_engine.h` and `src/risk_engine.cpp`
Purpose:
- Trend-based hemoglobin risk logic with baseline drift model.

Implemented logic:
- Baseline load from calibration profile.
- Baseline capture mode:
  - 60-second accumulation
  - confidence-gated sample acceptance
  - minimum sample count requirement
- Drift model:
  - `drift = (R - baselineR) / baselineR`
  - short EMA and long EMA track changes
- Risk score:
  - weighted magnitude of EMA drift and trend delta
- Warning mapping:
  - low-confidence -> low-signal warning
  - elevated/high from risk score thresholds
  - no baseline -> baseline-needed warning

Scope safety:
- Absolute Hb conversion is intentionally absent.

### 3.5 Calibration Persistence

### `include/calibration_store.h` and `src/calibration_store.cpp`
Purpose:
- Store calibration placeholders and user baseline in non-volatile storage.

Storage backend:
- `Preferences` NVS namespace (`hbcal`).

Stored fields:
- schema version
- photodiode sensitivity (A/W)
- amplifier gain (V/A)
- baseline voltage reference (mV)
- user baseline R and validity flag

Notes:
- Values are placeholders until real bench calibration data is supplied.

### 3.6 BLE Transport and Commands

### `include/ble_transport.h` and `src/ble_transport.cpp`
Purpose:
- Expose firmware outputs over BLE GATT, parse app control commands.

Service and characteristics:
- Service: `4f9c0100-a1f2-4c31-98cb-1cce5caa1000`
- Control (write): `...1007`
- Baseline payload: `...1008`
- Compact packet notify: `...1009` (active runtime telemetry)

Control commands parsed:
- `SNAP`
- `BASE_START`
- `BASE_CLEAR`
- `BASE_SET=<float>`
- `CAL_PD=<float>`
- `CAL_GAIN=<float>`
- `CAL_VREF=<float>`
- `BPM_START`
- `BPM_STOP`
- `MODE_PULSE`
- `MODE_IDLE`

Transport behavior:
- Notifications are emitted when cycle output is available and notify throttle allows it.
- Notifies all required telemetry characteristics.
- Tracks connection status and restarts advertising on disconnect.

### 3.7 Application Orchestration

### `src/main.cpp`
Purpose:
- Glue all modules in deterministic pipeline.

Main flow:
1. Setup modules (hardware, scheduler, DSP, quality, risk, calibration, BLE).
2. Poll BLE and consume commands.
3. On completed cycle sample:
  - push history buffer
  - process AC/DC + ratio
  - detect pulse and BPM
  - compute confidence
  - update baseline capture state
  - update risk status
  - build telemetry packet and publish over BLE
4. Optional serial debug output at reduced rate.

Responsibility split:
- Main contains orchestration only.
- Scientific and transport logic stays in dedicated modules.

## 4) Flutter Companion App Architecture

Flutter root: sibling folder `../hb_monitor_app/`

### `pubspec.yaml`
Purpose:
- Declares Flutter dependencies:
  - `flutter_blue_plus` (BLE)
  - `provider` (state)
  - `shared_preferences` (local calibration persistence)

### `README.md`
Purpose:
- Documents BLE UUID contract, control commands, and run instructions.

### 4.1 App Entry and Shell

### `lib/main.dart`
Purpose:
- Bootstraps app with `ChangeNotifierProvider<HemePulseAppState>`.
- Provides bottom-navigation shell:
  - Scan
  - Dashboard
  - Calibration

### 4.2 BLE Config, Models, and Parsers

### `lib/config/ble_config.dart`
Purpose:
- Single source of BLE UUID constants matching firmware.

### `lib/models/vital_snapshot.dart`
Purpose:
- Live data model used by dashboard and trend graph.
- Includes helper flags (`beatDetected`, `baselineAvailable`, `baselineCapturing`).

### `lib/models/calibration_profile.dart`
Purpose:
- Calibration placeholder model with copy and map serialization helpers.

### `lib/services/payload_parser.dart`
Purpose:
- Decodes little-endian payloads from BLE chars:
  - uint16 BPM
  - float32 ratio
  - int16 waveform
  - warning enum mapping

### `lib/services/storage_service.dart`
Purpose:
- Loads/saves `CalibrationProfile` in `SharedPreferences` via JSON.

### `lib/services/ble_service.dart`
Purpose:
- BLE scanner, connector, characteristic discovery, notification subscriptions, and control writes.

Responsibilities:
- Filters scan results for HemePulse naming.
- Discovers service and characteristic handles.
- Exposes update stream by characteristic UUID.
- Supports write commands used by app-state workflows.

### 4.3 App State and Modes

### `lib/state/app_state.dart`
Purpose:
- Central application state manager.

Responsibilities:
- Lifecycle:
  - initialize storage + BLE subscriptions
  - scan, connect, disconnect
- Data assembly:
  - merges characteristic updates into coherent `VitalSnapshot`
  - tracks trend history (rolling list)
- Modes:
  - Pulse Check mode
  - Hemoglobin Status mode
- Calibration:
  - saves placeholders locally
  - forwards calibration/baseline commands to firmware
- Alerts:
  - generates user-facing warning messages based on warning state and confidence

### 4.4 UI Screens and Widgets

### `lib/ui/screens/scan_screen.dart`
Purpose:
- Device scanning and connection UI.
- Shows filtered devices and RSSI.

### `lib/ui/screens/dashboard_screen.dart`
Purpose:
- Main monitoring dashboard.

Displays:
- BPM
- R value
- confidence
- warning state
- trend chart
- active alert banner
- mode switch chips

### `lib/ui/screens/pulse_check_screen.dart`
Purpose:
- Mode panel for baseline capture workflow.
- Provides baseline start and snapshot request actions.

### `lib/ui/screens/hemoglobin_status_screen.dart`
Purpose:
- Mode panel for baseline-relative trend visibility.
- Shows baseline R, current R, and drift percentage.

### `lib/ui/screens/calibration_screen.dart`
Purpose:
- Editable calibration placeholders and baseline values.
- Saves local settings and sends updates to firmware when connected.

### `lib/ui/widgets/trend_chart.dart`
Purpose:
- Lightweight custom-paint trend chart for IR waveform history.

## 5) End-to-End Data Path

Firmware:
- Ambient/RED/IR cycle -> corrected values -> AC/DC + bandpass -> pulse + BPM -> quality -> risk -> BLE packet.

App:
- BLE notifications -> payload parser -> merged snapshot in app state -> UI cards, trend graph, mode panels, and alerts.

## 6) BLE Payload Interpretation in App

Characteristic parsing map:
- Live status: 1-byte flags
- BPM: uint16 little-endian
- R ratio: float32 little-endian
- Confidence: uint8
- Warning: uint8 enum
- Waveform: int16 little-endian
- Baseline: float32 + validity byte

## 7) Calibration and Baseline Notes

Current calibration layer is intentionally placeholder-friendly.

Fields already integrated end-to-end:
- photodiode sensitivity
- amplifier gain
- baseline voltage reference
- user baseline R

Future hardware calibration can extend:
- detector responsivity curves by wavelength
- amplifier transfer characteristics
- temperature drift compensation
- subject-specific adaptation models

## 8) Validation Status and Constraints

Completed checks:
- Static/language error checks for firmware and Flutter source files report no editor diagnostics.

Environment limitation encountered:
- PlatformIO CLI (`pio`) is not available in the terminal session, so full firmware compile was not executable here.

Recommended local validation commands:
- Firmware: `pio run`
- Flutter: `flutter pub get` then `flutter analyze` and `flutter run`

## 9) What to Change First When Moving to Real Sensor Data

1. `include/app_config.h`
- Tune LED settle times, cycle period, and thresholds.

2. `src/signal_processing.cpp`
- Replace simple AC estimate with a more robust morphology-aware method if needed.

3. `src/quality_score.cpp`
- Refit confidence weights using collected field datasets.

4. `src/risk_engine.cpp`
- Refine risk score scaling/hysteresis after clinical protocol feedback.

5. `src/calibration_store.cpp`
- Extend schema for richer calibration metadata and version migration.

6. `lib/state/app_state.dart`
- Add trend persistence/export and richer user/session metadata.

## 10) Immediate Next Engineering Steps

1. Build and flash firmware with PlatformIO available locally.
2. Verify BLE characteristic discovery from Flutter app.
3. Run baseline capture in Pulse Check mode with stable finger/ear placement.
4. Validate warning transitions by controlled signal perturbations.
5. Collect pilot sessions to tune confidence and drift thresholds.
