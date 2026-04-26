# Beginner's Guide: HemePulse Data Pipeline

Welcome! If you are new to the HemePulse project, this guide is written specifically for you. It explains how raw light data from the ESP32 sensor makes its way to the Flutter app, how that data is turned into a Heart Rate (BPM), and how it is processed to predict Hemoglobin (Hb).

Most importantly, it will show you **exactly where and how to tweak the formulas** so you can fine-tune the model to match your specific hardware!

---

## 1. How data flows from the ESP32 to the Flutter App

1. **The Sensor:** The MAX30101 (or similar) sensor on the ESP32 fires a Red LED, then an Infrared (IR) LED. It measures how much light bounces back.
2. **The ESP32:** The ESP32 collects this data, subtracts ambient background light, and packs it into tiny 11-byte chunks to save Bluetooth bandwidth.
3. **Bluetooth (BLE):** The ESP32 sends these packets to the phone over Bluetooth Low Energy.
4. **The Flutter App:** The app receives these packets. In `lib/services/payload_parser.dart`, the `PayloadParser.parseBatch()` function unpacks the raw bytes back into usable numbers:
   - `timestampMs`
   - `ambientRaw`
   - `redCorrected`
   - `irCorrected`

These raw numbers are dumped into giant lists (buffers) inside `lib/state/app_state.dart`. 

---

## 2. How BPM (Heart Rate) is Calculated

Every time your heart beats, blood rushes into your finger. Blood absorbs light. Therefore, the amount of light returning to the sensor goes up and down with every heartbeat. 

If you look at the code in `_estimateBpmFromChunk()` inside `app_state.dart`:
1. **Smoothing:** The app takes a 3-second chunk of **RED** light data. It applies an "Exponential Moving Average" (EMA) to smooth out tiny jitters.
2. **AC Detrending:** It removes the large, slow-moving baseline (the DC part) to isolate just the fast heartbeat pulses (the AC part).
3. **Peak Detection:** The code scans this AC signal to find the highest points (peaks) that cross a specific dynamic threshold. 
4. **Math:** Once it finds the peaks, it measures the time (in milliseconds) between them. 
   - `BPM = 60000 / time_between_peaks`

---

## 3. How Hemoglobin (Hb) is Processed

Unlike BPM which relies on the *pulsing* peaks, the Hemoglobin prediction model needs a **highly stable average** of the light reflecting through the tissue. 

If you look at `_computeHb()` inside `app_state.dart`:

1. **Collect 20 Seconds of Data:** The app looks at all the `red` and `ir` samples collected during the 20-second scan.
2. **Filter Out Bad Data (IQR Filter):** Your finger might twitch, causing huge spikes. The code uses a statistical filter called IQR (Interquartile Range) to throw away the extreme highs and extreme lows, leaving only the "clean" samples.
3. **Find the Median:** From these clean samples, the app takes the **median** (the exact middle value) to get one single, rock-solid number for Red and one for IR:
   ```dart
   double fingerRed = iqrMedian(cleanRed);
   double fingerIr  = iqrMedian(cleanIr);
   ```
4. **Calculate the Ratio:** It divides Red by IR.
   ```dart
   double ourR = fingerRed / fingerIr;
   ```
5. **Logarithmic Transformation:** The dataset our regression tree was trained on requires the Natural Logarithm (`ln`) of the ratio:
   ```dart
   final lnRatio = math.log(ourR);
   ```
6. **Predict:** Finally, `lnRatio`, `Gender`, and `Age` are passed into `HbPredictor.predict()`.

---

## 4. How to Fine-Tune the R and IR Values

Because every hardware sensor (the LEDs, the glass, the casing) is slightly different, your ESP32 might output slightly higher or lower raw numbers than the hardware the original dataset was recorded with.

To **fine-tune** the application so it perfectly matches your regression model, you can inject mathematical adjustments (adding, subtracting, or multiplying) directly into `app_state.dart`.

### Where to edit:
Open `hb_monitor_app/lib/state/app_state.dart` and find the `_computeHb()` function. Look for where `fingerRed` and `fingerIr` are calculated, right before `ourR` is created.

### Example 1: Multiplying to scale the values
If your Red LED is physically a bit weaker than the one in the dataset, you can boost its value by 10% by multiplying by `1.10`:
```dart
double fingerRed = iqrMedian(cleanRed);
double fingerIr  = iqrMedian(cleanIr);

// --- FINE TUNING HERE ---
fingerRed = fingerRed * 1.10;  // Boost Red by 10%
fingerIr = fingerIr * 0.95;    // Decrease IR by 5%
// ------------------------

final ourR = fingerRed / fingerIr;
final lnRatio = math.log(ourR);
```

### Example 2: Adding or Subtracting constant offsets
If ambient light or the plastic casing adds a constant "noise" value to the sensor, you can subtract it:
```dart
// --- FINE TUNING HERE ---
fingerRed = fingerRed - 150.0; // Subtract hardware baseline offset
fingerIr = fingerIr - 300.0;
// ------------------------
```

### Example 3: Editing the Ratio Directly
Instead of modifying the raw Red/IR, you can simply push the final calculated Ratio (`ourR`) up or down to align with your model's expectations:
```dart
double ourR = fingerRed / fingerIr;

// --- FINE TUNING HERE ---
ourR = ourR + 0.15; // Shift the whole ratio up by 0.15
// ------------------------

final lnRatio = math.log(ourR);
```

### Tips for Tuning:
1. **Use `print` statements:** The code already prints `[Hb] RATIO: ourR=X.XXX, ln(R)=Y.YYY` to the console. Watch these numbers in the terminal while the app runs!
2. **Make small changes:** Change a multiplier by `0.05` or an offset by `0.1` and test again.
3. **Compare to Ground Truth:** Take a reading with a real clinical Hemoglobin meter, look at what the app predicts, and adjust your offsets up or down until they match.
