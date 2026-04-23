# HemePulse Firmware Source (Always-Awake Runtime)

This folder contains the active ESP32 sensor-node firmware files:

- `main.cpp`
- `scheduler.cpp`
- `hardware_io.cpp`
- `ble_transport.cpp`
- `calibration_store.cpp`

Legacy and non-runtime modules are intentionally removed from the active path to keep the firmware stable and easy to present.

## Runtime Behavior

Sleep features are fully removed from runtime behavior.

The board now runs in an always-awake mode:

1. BLE remains available continuously.
2. When disconnected, device stays in advertising mode.
3. When connected, device stays in connected mode.
4. No deep-sleep entry, no wake-cause routing, and no BLE shutdown/deinit due to sleep.

## Measurement Cadence

Readings are generated every 20 seconds using:

- `kReadingIntervalMs = 20000`

Within each reading event, the existing acquisition pipeline is used:

- ambient / RED / IR cycle timing and ADC reads
- lightweight preprocessing (ambient correction + EMA)
- local baseline comparison and warning logic
- compact BLE packet publish

## Baseline Capture in 20s Cadence Mode

Because measurements are now periodic every 20 seconds, baseline sample thresholds are tuned for this cadence:

- `kBaselineCaptureDurationMs = 60000`
- `kMinBaselineSamples = 3`

This allows baseline capture to complete in always-awake low-rate mode.

## Serial Output

Waveform/debug preview output remains enabled for demos:

- `kEnableSerialWaveform = true`
- Output format per sample: `>sin:<filtered_ir_value>`

This is serial plotter friendly and intended for presentation.

## Build and Upload

From repository root:

- Build: `python -m platformio run -e lolin_c3_mini`
- Upload: `python -m platformio run -e lolin_c3_mini -t upload`
