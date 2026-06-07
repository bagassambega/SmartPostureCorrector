# sensor — ESP32 Posture Detection with Edge AI

Real-time posture classification firmware for the ESP32 + MPU6050 using an on-device edge AI model (emlearn export). Every 5 seconds the device reads the IMU, constructs a feature vector, runs inference, and publishes a JSON payload to MQTT:

```json
{"timestamp": 15000, "roll": 2.14, "pitch": -1.03, "class": 0}
```

When the predicted class matches `VIBRATION_TRIGGER_CLASS` (default: `0`), a haptic alert is sent to the vibration motor on GPIO5.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) | CLI (`pip install platformio`) or VS Code extension |
| ESP32 dev board + MPU6050 | SDA → GPIO21, SCL → GPIO22 |
| Vibration motor module | IN → GPIO5 (D5), VCC, GND |
| Calibration button | GPIO14 → GND (uses internal pull-up) |
| MQTT broker reachable on LAN | Default config: port `1883` |
| emlearn-exported model header | `<repo_root>/model/<model_name>.h` |

---

## Project Structure

```
hardware/sensor/
├── include/               ← (empty — no generated files)
├── src/
│   └── sensor.cpp         ← Main application
├── platformio.ini
├── split_rf_model.py      ← Legacy script (Eloquent ML, no longer used)
├── README.md
└── .gitignore

<repo_root>/model/
├── randomforest_model.h   ← Current active model (emlearn RF)
└── extratrees_model.h     ← (example) alternative model
```

The compiler include path is configured via `-I../../model` in `platformio.ini`, so any `.h` file in the repo-root `model/` directory is directly includable from `sensor.cpp`.

---

## Build

### 1. Confirm the model header is present

```bash
ls ../../model/randomforest_model.h
```

If missing, request it from the ML team. The file must be committed at `<repo_root>/model/`.

### 2. Configure WiFi and MQTT

Edit the constants near the top of `src/sensor.cpp`:

```cpp
const char *WIFI_SSID   = "YourNetworkName";
const char *WIFI_PASS   = "YourPassword";
const char *MQTT_SERVER = "192.168.x.x";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_USER   = "";   // leave empty if broker has no auth
const char *MQTT_PASS   = "";
```

### 3. Build

```bash
# From hardware/sensor/
~/.platformio/penv/bin/pio run
```

Or via VS Code: **PlatformIO sidebar → Build** (✓ checkmark).

Expected output (emlearn RF, 50 trees):

```
RAM:   [=         ]  13.8%  (45 KB / 320 KB)
Flash: [====      ]  37.9%  (795 KB / 2 MB)
========================= [SUCCESS] ============================
```

> The project uses the `no_ota.csv` partition scheme for maximum code flash headroom. OTA updates are not supported.

---

## Deploy (Upload)

Connect the ESP32 via USB, then:

```bash
~/.platformio/penv/bin/pio run --target upload
```

PlatformIO auto-detects the serial port. If auto-detect fails, specify it explicitly:

```bash
~/.platformio/penv/bin/pio run --target upload --upload-port /dev/ttyUSB0
```

Find your port with `ls /dev/ttyUSB* /dev/ttyACM*`.

To build and upload in one command:

```bash
~/.platformio/penv/bin/pio run --target upload && ~/.platformio/penv/bin/pio device monitor
```

---

## Monitor

```bash
~/.platformio/penv/bin/pio device monitor
```

Baud rate is `115200`. On startup:

```
===== ESP32 SYSTEM INFO =====
Chip Model: ESP32-D0WDQ6
...
Kalibrasi gyro... (pastikan sensor diam)
Kalibrasi gyro selesai
Offset X: 12.33  Offset Y: -4.10  Offset Z: 0.87
Orientasi dikalibrasi
Roll offset: 1.04   Pitch offset: -0.52
Sistem siap. Publish setiap 5000ms
```

Every 5 seconds during operation:

```
Roll: 2.14    Pitch: -1.03    Class: 0 [MOTOR ON]
Roll: 1.87    Pitch: -0.94    Class: 1 [motor off]
```

`[MOTOR ON]` appears when the predicted class equals `VIBRATION_TRIGGER_CLASS`.

---

## Changing the Active Model

### What "changing the model" means

An emlearn-exported model is a single self-contained `.h` file containing:
- One `static inline` C function per tree
- A `<modelname>_predict(const int16_t*, int32_t)` entry point that performs majority voting

To swap models you change the `#include` and update the predict call — **no splitting, no code generation, no build system changes**.

### Step-by-step: switching to a different emlearn model

Suppose the ML team delivers `extratrees_model.h` (ExtraTrees, same emlearn export format):

**1. Place the new header** in `<repo_root>/model/`:
```
model/extratrees_model.h
```

**2. Update `src/sensor.cpp` — change the include:**
```cpp
// Before:
#include "randomforest_model.h"

// After:
#include "extratrees_model.h"
```

**3. Update the predict function call** (the function name follows the filename convention `<modelname>_predict`):
```cpp
// Before:
int predictedClass = (int)randomforest_model_predict(modelInput, MODEL_FEATURES_LENGTH);

// After:
int predictedClass = (int)extratrees_model_predict(modelInput, MODEL_FEATURES_LENGTH);
```

**4. Verify and update the feature vector if it changed.**  
See the section below.

**5. Build and upload:**
```bash
~/.platformio/penv/bin/pio run --target upload
```

### What you do NOT need to change

- `platformio.ini` — the `-I../../model` flag already covers any header in `model/`
- `bacaSensor()` — all 11 sensor values are always collected regardless of model
- The `sensorData[11]` snapshot — always populated
- MQTT publish logic — unchanged

---

## Adjusting the Feature Vector

The sensor firmware always collects all 11 sensor values into `sensorData[]`:

| Index | Variable | Description |
|---|---|---|
| `sensorData[0]` | `rawAccX` | Raw accel X (int16) |
| `sensorData[1]` | `rawAccY` | Raw accel Y (int16) |
| `sensorData[2]` | `rawAccZ` | Raw accel Z (int16) |
| `sensorData[3]` | `rawGyroX` | Raw gyro X (int16) |
| `sensorData[4]` | `rawGyroY` | Raw gyro Y (int16) |
| `sensorData[5]` | `rawGyroZ` | Raw gyro Z (int16) |
| `sensorData[6]` | `kemiringanX` | Roll (°, relative to calibration) |
| `sensorData[7]` | `kemiringanY` | Pitch (°, relative to calibration) |
| `sensorData[8]` | `kecepatanRotasiX` | Gyro X (°/s, bias-corrected) |
| `sensorData[9]` | `kecepatanRotasiY` | Gyro Y (°/s, bias-corrected) |
| `sensorData[10]` | `kecepatanRotasiZ` | Gyro Z (°/s, bias-corrected) |

Only a subset of these is fed into `modelInput[]` for the active model.

### When to update `modelInput[]`

You **must** update `modelInput[]` in `sensor.cpp` when the new model:
- Uses a **different number of features** → update `MODEL_FEATURES_LENGTH`
- Uses **different columns** from `sensorData` → update the index assignments
- Uses **different column order** → reorder the assignments to match

The mapping comment in `sensor.cpp` documents the current configuration:

```cpp
#define MODEL_FEATURES_LENGTH 5

int16_t modelInput[MODEL_FEATURES_LENGTH];
modelInput[0] = (int16_t)sensorData[6];   // kemiringanX (roll °)
modelInput[1] = (int16_t)sensorData[7];   // kemiringanY (pitch °)
modelInput[2] = (int16_t)sensorData[8];   // kecepatanRotasiX (gx °/s)
modelInput[3] = (int16_t)sensorData[9];   // kecepatanRotasiY (gy °/s)
modelInput[4] = (int16_t)sensorData[10];  // kecepatanRotasiZ (gz °/s)
```

**To verify against the training script**, find the feature selection line in Python:

```python
X = df[["kemiringanX", "kemiringanY", "kecepatanRotasiX", "kecepatanRotasiY", "kecepatanRotasiZ"]]
```

The column order in `X` must match the index order in `modelInput[]` exactly. A mismatch will not cause a compile error but will produce wrong predictions.

> **Note on `int16_t`:** emlearn exports models that accept integer input. Float sensor values are cast to `int16_t` via truncation. If the training data used float values (e.g., roll/pitch in degrees), ensure the values were not further scaled before training — otherwise apply the same scale factor here before casting.

---

## Changing the Alert Class

The vibration motor fires when the predicted class equals `VIBRATION_TRIGGER_CLASS`. To change which class triggers the motor, edit the define at the top of `sensor.cpp`:

```cpp
#define VIBRATION_TRIGGER_CLASS 0   // change to 1, 2, or 3 as needed
```

---

## MQTT Payload

Published to topic `sensors/mpu6050` every **5 seconds**:

```json
{"timestamp": 15000, "roll": 2.14, "pitch": -1.03, "class": 0}
```

| Field | Type | Description |
|---|---|---|
| `timestamp` | `uint32` | Milliseconds since boot |
| `roll` | `float` | Roll angle (°), relative to calibration |
| `pitch` | `float` | Pitch angle (°), relative to calibration |
| `class` | `int` | Predicted posture class (0–N) |

---

## Hardware Re-calibration

Press and **release** the button on GPIO14 at any time. The firmware will:

1. Wait 300 ms for the user to settle into position.
2. Re-sample gyro bias (200 samples, sensor must be **still**).
3. Set the current orientation as the new roll/pitch zero reference.

The button uses `INPUT_PULLUP` (idle = HIGH, pressed = LOW). The firmware detects the **rising edge** (release event) with 50 ms debounce.

---

## Why emlearn — Technical Background

The previous model (Eloquent ML, 750-tree Random Forest) compiled all tree logic into a single `predict()` function body ~357,000 lines long. The Xtensa LX6 ISA loads float constants via the `l32r` instruction using an 18-bit PC-relative offset, giving a ±256 KB literal pool range. A function that large overflows that range, producing `literal target out of range` linker errors.

emlearn compiles each tree as a separate `static inline` function with **integer** comparisons — no float constants, no literal pool. Each tree function is independent and small. The 50-tree model is 19,000 lines total, well within any addressing limit. No splitting workaround is needed.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `fatal error: randomforest_model.h: No such file` | Header not in `model/` | Add the file at `<repo_root>/model/` |
| `undefined reference to randomforest_model_predict` | Wrong function name after model swap | Match function name to header filename convention `<name>_predict` |
| Build succeeds but predictions are all wrong class | Feature vector order mismatch | Verify `modelInput[]` order against Python training `X = df[[...]]` |
| MQTT not connecting | Wrong IP or no broker | Check `MQTT_SERVER`; verify broker is running |
| Motor never fires | Wrong `VIBRATION_TRIGGER_CLASS` | Match the define to the actual "bad posture" label |
| Motor always ON | Model always predicts trigger class | Check calibration; verify feature vector mapping |
| Garbled serial | Wrong baud rate | Set monitor to `115200` baud |
| `l32r: literal target out of range` | Old Eloquent ML model included directly | Use emlearn export, not Eloquent ML |
