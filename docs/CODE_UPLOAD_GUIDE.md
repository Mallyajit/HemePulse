# HemePulse: Code Upload & Deployment Guide

A complete guide for uploading and testing code changes in the HemePulse project (ESP32-C3 firmware + Flutter app).

---

## Table of Contents
1. [Firmware Upload (ESP32-C3)](#firmware-upload-esp32-c3)
2. [Flutter App Deployment (Android)](#flutter-app-deployment-android)
3. [Troubleshooting: Code Changes Not Appearing](#troubleshooting-code-changes-not-appearing)
4. [Complete Workflow: Edit → Build → Deploy → Verify](#complete-workflow-edit--build--deploy--verify)

---

## Firmware Upload (ESP32-C3)

### Quick Start: Upload Code to ESP32-C3
```powershell
# 1. Navigate to firmware directory
cd "C:\Users\mally\Documents\Mallyajit Codes\College_Works\HemePulse\HemePulse"

# 2. Activate virtual environment (if needed)
.\.venv\Scripts\Activate.ps1

# 3. Upload firmware
platformio run --target upload
```

### Step-by-Step: Uploading Firmware

#### Step 1: Verify Device Connection
```powershell
# List connected devices
platformio device list
```
✅ **Expected Output:** Your ESP32-C3 device should appear with a COM port (e.g., `COM3`)

#### Step 2: Edit Your Code
- Edit files in `src/` or `include/` directories
- Examples:
  - `src/main.cpp` - Main application logic
  - `src/ble_transport.cpp` - BLE communication
  - `include/pulse_detector.h` - Pulse detection headers

#### Step 3: Build the Project
```powershell
platformio run
```
⏱️ **Time:** First build takes ~30-60 seconds, subsequent builds are faster

✅ **Expected Output:**
```
Checking size __pycache__\...
Compiling .pio/build/esp32-c3-devkitm-1/src/main.cpp.o
...
Advanced memory usage is X% (used X bytes from 327680 bytes)
```

#### Step 4: Upload to Device
```powershell
platformio run --target upload
```
⏱️ **Time:** 10-30 seconds

✅ **Expected Output:**
```
Uploading .pio/build/esp32-c3-devkitm-1/firmware.bin
...
Hard resetting via RTS pin...
```

#### Step 5: Verify on Device
- Check serial output: `platformio device monitor`
- Look for startup messages or behavior changes

### Important Notes for Firmware

**Auto-reset on Upload:**
- PlatformIO automatically resets the device after upload
- No manual restart needed

**Common Issues:**
| Problem | Solution |
|---------|----------|
| "Cannot find device" | Check USB cable, verify `platformio device list` |
| "Upload timeout" | Device may be stuck; press reset button on ESP32-C3 |
| "Permission denied" | Restart terminal as Administrator |

---

## Flutter App Deployment (Android)

### Quick Start: Deploy to Android Device
```powershell
# 1. Navigate to app directory
cd "C:\Users\mally\Documents\Mallyajit Codes\College_Works\HemePulse\hb_monitor_app"

# 2. Run on device
flutter run -d RMX3395
```

### Step-by-Step: Deploying Flutter App

#### Step 1: Verify Device Connection
```powershell
flutter devices
```
✅ **Expected Output:**
```
RMX3395 (mobile) • 75OVNBJZAE5DLZGQ • android-arm64 • Android 13 (or similar)
```

#### Step 2: Edit Your Code
Edit files in `lib/` directory:
- `lib/main.dart` - App entry point, theme, navigation
- `lib/ui/screens/` - UI screens (dashboard, pulse check, etc.)
- `lib/services/` - BLE service, communication
- `lib/state/` - App state management
- `lib/models/` - Data models

#### Step 3: Verify Code Quality (Optional but Recommended)
```powershell
flutter analyze
flutter test
```
✅ **Expected Output:**
- No issues found (analyze)
- Tests pass (test)

#### Step 4: Deploy to Device
```powershell
flutter run -d RMX3395
```

⏱️ **Time:**
- First run after clean: 2-3 minutes (full build)
- Subsequent runs: 30-60 seconds

✅ **Expected Output:**
```
Running Gradle task 'assembleDebug'...
✓ Built build\app\outputs\flutter-apk\app-debug.apk
Installing build\app\outputs\flutter-apk\app-debug.apk...
Flutter run key commands.
r Hot reload. 🔥🔥🔥
```

#### Step 5: Hot Reload During Development
While app is running (from Step 4):
```
r     - Hot reload (instant code update, keeps app state)
R     - Hot restart (restart app, clears state)
d     - Detach (stop debugger, app keeps running)
q     - Quit (stop app and debugger)
```

#### Step 6: Verify Changes on Device
- Check that UI changes appear immediately
- Colors, text, layout should update
- App functionality should work

### Important Notes for Flutter

**Hot Reload Requirements:**
- Only works for Dart code changes in `lib/`
- Does NOT work for:
  - `pubspec.yaml` changes
  - Native code in `android/` folder
  - Asset additions/changes

**When to Use Full Rebuild:**
- Dependencies changed (`pubspec.yaml`)
- Gradle configuration changed
- Android native code modified
- If hot reload fails/causes issues

---

## Troubleshooting: Code Changes Not Appearing

### Problem: You edited code, but changes don't show on device

**Root Cause:** Usually cache-related (old compiled code, stale APK, or stale build artifacts)

### Solution: Full Cache Clear & Rebuild

Follow these steps **in order**:

#### 1. Stop Running App
```powershell
# If flutter run is still active:
# Press 'q' in terminal
q
```

#### 2. Clear All Caches
```powershell
cd "C:\Users\mally\Documents\Mallyajit Codes\College_Works\HemePulse\hb_monitor_app"

# Clear Flutter and Dart caches
flutter clean

# Clear Android Gradle cache
Remove-Item -Recurse -Force "android\.gradle" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "android\build" -ErrorAction SilentlyContinue
```

#### 3. Uninstall Old App from Device
```powershell
# Find adb path and uninstall
$adbPath = "C:\Users\mally\AppData\Local\Android\Sdk\platform-tools\adb.exe"
& $adbPath uninstall com.example.hb_monitor_app
```

#### 4. Full Rebuild & Deploy
```powershell
flutter run -d RMX3395
```

⏱️ **Total Time:** 3-5 minutes (because cache is empty)

### Expected Results After Fix:
- ✅ New theme colors appear
- ✅ New text/labels show up
- ✅ New UI elements visible
- ✅ New BLE commands work
- ✅ No old code running

### When to Use This Process
- Code changes not appearing
- Theme/color changes not showing
- New BLE commands not working
- App seems "stuck" on old version

---

## Complete Workflow: Edit → Build → Deploy → Verify

### Firmware Workflow

```
┌─────────────────────────────────────┐
│ 1. EDIT: Modify src/ or include/    │
│    Example: src/main.cpp            │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│ 2. BUILD: Compile code              │
│    $ platformio run                 │
│    ⏱️ 30-60 seconds                 │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│ 3. UPLOAD: Send to ESP32-C3         │
│    $ platformio run --target upload │
│    ⏱️ 10-30 seconds                 │
│    Device auto-resets               │
└────────────┬────────────────────────┘
             │
┌────────────▼────────────────────────┐
│ 4. VERIFY: Check serial output      │
│    $ platformio device monitor      │
│    Look for expected messages       │
└─────────────────────────────────────┘
```

### Flutter Workflow

```
┌──────────────────────────────────────┐
│ 1. EDIT: Modify lib/                 │
│    Example: lib/main.dart            │
└─────────────┬──────────────────────┘
              │
┌─────────────▼──────────────────────┐
│ 2. ANALYZE: Check for errors       │
│    $ flutter analyze               │
│    ⏱️ 10-20 seconds                │
└─────────────┬──────────────────────┘
              │
┌─────────────▼──────────────────────┐
│ 3. DEPLOY: Build & install        │
│    $ flutter run -d RMX3395       │
│    ⏱️ 30-180 seconds              │
└─────────────┬──────────────────────┘
              │
┌─────────────▼──────────────────────┐
│ 4. VERIFY: Check UI on device     │
│    • Colors correct?              │
│    • Text updated?                │
│    • Features working?            │
└──────────────────────────────────────┘
```

### Faster Iteration: Hot Reload

Once app is deployed and running:

```
┌──────────────────────────────┐
│ 1. EDIT: Modify lib/         │
└──────────┬───────────────────┘
           │
┌──────────▼───────────────────┐
│ 2. HOT RELOAD: Press 'r'    │
│    ⏱️ 1-3 seconds            │
│    App updates instantly     │
└──────────┬───────────────────┘
           │
┌──────────▼───────────────────┐
│ 3. VERIFY: See changes live │
└──────────────────────────────┘
```

**Note:** Hot reload doesn't work for:
- `pubspec.yaml` changes → use `R` (hot restart)
- Android native code → use full `flutter run`
- Dependencies added → use full `flutter run`

---

## Checklists

### Before Uploading Firmware
- [ ] You've edited file in `src/` or `include/`
- [ ] ESP32-C3 is connected via USB (check: `platformio device list`)
- [ ] Virtual environment activated (`.venv`)
- [ ] No serial monitor is running (close if open)

### Before Deploying Flutter
- [ ] You've edited file in `lib/`
- [ ] Android device is connected (check: `flutter devices`)
- [ ] `pubspec.yaml` didn't change (if it did, can't use hot reload)
- [ ] Code has no obvious errors

### If Code Changes Don't Appear
- [ ] Close app on device (swipe away)
- [ ] Clear caches: `flutter clean`
- [ ] Uninstall old app: `adb uninstall com.example.hb_monitor_app`
- [ ] Full deploy: `flutter run -d RMX3395`
- [ ] Wait 30 seconds for app to start
- [ ] Check device screen for new code

---

## Quick Reference: Commands

### Firmware (ESP32-C3)
```powershell
# Navigate to firmware folder
cd "C:\Users\mally\Documents\Mallyajit Codes\College_Works\HemePulse\HemePulse"

# Activate environment
.\.venv\Scripts\Activate.ps1

# Build
platformio run

# Upload
platformio run --target upload

# Monitor serial output
platformio device monitor

# List devices
platformio device list

# Clean build
platformio run --target clean
```

### Flutter App (Android)
```powershell
# Navigate to app folder
cd "C:\Users\mally\Documents\Mallyajit Codes\College_Works\HemePulse\hb_monitor_app"

# Check connected devices
flutter devices

# Analyze code
flutter analyze

# Run tests
flutter test

# Deploy to device
flutter run -d RMX3395

# Full clean rebuild
flutter clean
flutter run -d RMX3395

# Uninstall app from device
$adbPath = "C:\Users\mally\AppData\Local\Android\Sdk\platform-tools\adb.exe"
& $adbPath uninstall com.example.hb_monitor_app
```

---

## Common Mistakes & How to Avoid Them

| Mistake | Impact | Fix |
|---------|--------|-----|
| Forget to save file before deploy | Old code runs | Save (Ctrl+S) before running commands |
| Don't check device list first | "Device not found" error | Run `flutter devices` or `platformio device list` first |
| Use hot reload after `pubspec.yaml` change | Hot reload fails | Use `R` (hot restart) or full `flutter run` |
| Don't clear cache when code doesn't appear | Wasted time debugging | Follow "Code Changes Not Appearing" section |
| Try to upload while serial monitor is open | Upload fails | Close monitor first |
| Edit wrong file (app instead of firmware) | Changes don't deploy | Double-check you're editing correct folder |
| Don't check `flutter analyze` before deploy | App crashes on device | Run `flutter analyze` first |

---

## Summary

**Firmware Upload:**
1. Edit `src/` or `include/`
2. Run `platformio run` (build)
3. Run `platformio run --target upload` (send to device)
4. Device auto-resets with new code

**Flutter Deployment:**
1. Edit `lib/`
2. Run `flutter analyze` (optional, recommended)
3. Run `flutter run -d RMX3395` (build, install, run)
4. Use `r` for hot reload during development
5. Use `R` for hot restart if needed
6. Press `q` to quit

**Code Not Showing?**
1. Run `flutter clean`
2. Uninstall old app: `adb uninstall com.example.hb_monitor_app`
3. Full deploy: `flutter run -d RMX3395`

---

## Next Steps

- Practice uploading a small change to each system
- Try using hot reload for quick iterations
- Learn to read error messages (they're helpful!)
- Bookmark this guide for future reference

Good luck with your HemePulse project! 🎉
