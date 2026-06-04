# sensor — ESP32 Posture Detection with Edge AI

Real-time posture classification firmware for the ESP32 + MPU6050 using an on-device Random Forest model. Every 5 seconds the device reads the IMU, runs inference, and publishes a JSON payload to MQTT:

```json
{"timestamp": 15000, "roll": 2.14, "pitch": -1.03, "class": 0}
```

---

## Prerequisites

| Requirement | Notes |
|---|---|
| [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) | CLI (`pip install platformio`) or VS Code extension |
| Python ≥ 3.8 | For the model split script |
| `model/model_rf.h` present at `../../model/model_rf.h` | The trained Random Forest header from Eloquent ML |
| ESP32 dev board + MPU6050 sensor | SDA → GPIO21, SCL → GPIO22 |
| MQTT broker reachable on the local network | Default: `192.168.1.10:1883` |

---

## Project Structure

```
hardware/sensor/
├── include/
│   └── model_rf_split.h     ← Generated wrapper header (committed)
├── model/                   ← GITIGNORED — generated RF tree shards
│   ├── rf_part_0.cpp
│   ├── rf_part_1.cpp
│   └── ... rf_part_14.cpp
├── src/
│   └── sensor.cpp           ← Main application
├── split_rf_model.py        ← Code generation script
├── platformio.ini
└── .gitignore
```

> **Why is `model/` gitignored?**  
> The 750-tree Random Forest compiles to ~1.5 MB of firmware. The source shards (`rf_part_*.cpp`) are generated automatically from `model/model_rf.h` and are fully reproducible. Committing 17 MB of generated C++ provides no value and pollutes diffs.

---

## Step-by-Step: First-Time Build

### 1. Split the model into translation units

This is **required before the first build** (and after every model retrain). The Xtensa LX6 CPU (ESP32) has a 256 KB address range limit per function for floating-point constants. The split script partitions the 750-tree model into 15 separate `.cpp` files — each compiled as its own translation unit — to stay within that limit.

```bash
# Run from the project root (hardware/sensor/)
python3 split_rf_model.py
```

Expected output:

```
Reading model...
Extracting trees...
  Found 750 trees
Writing part files (50 trees/file)...
  Wrote .../model/rf_part_0.cpp  (50 trees)
  ...
  Wrote .../model/rf_part_14.cpp  (50 trees)
Writing header (model_rf_split.h) with 15 parts...
  Wrote .../include/model_rf_split.h

Done. 750 trees split into 15 TUs.
```

The script reads from `../../model/model_rf.h` and writes into `model/` and `include/`.

> **Re-run this step** every time a new model is exported from the training pipeline.

---

### 2. Configure WiFi and MQTT

Edit the constants at the top of `src/sensor.cpp`:

```cpp
const char *WIFI_SSID   = "YourNetworkName";
const char *WIFI_PASS   = "YourPassword";
const char *MQTT_SERVER = "192.168.x.x";   // your broker IP
const uint16_t MQTT_PORT = 1883;
const char *MQTT_USER   = "";              // leave empty if no auth
const char *MQTT_PASS   = "";
```

---

### 3. Build

```bash
# From hardware/sensor/
~/.platformio/penv/bin/pio run
```

Or via VS Code: **PlatformIO sidebar → Build** (✓ checkmark).

Expected memory usage (approximate):

```
RAM:   [=         ]  13.8%  (45 KB / 320 KB)
Flash: [=======   ]  71.9%  (1.5 MB / 2 MB)
```

> The project uses the `no_ota.csv` partition scheme to maximize the available code flash region. OTA updates are not supported with this firmware.

---

### 4. Upload

Connect the ESP32 via USB, then:

```bash
~/.platformio/penv/bin/pio run --target upload
```

PlatformIO auto-detects the serial port. If it fails on auto-detect:

```bash
~/.platformio/penv/bin/pio run --target upload --upload-port /dev/ttyUSB0
```

Replace `/dev/ttyUSB0` with your actual port (`ls /dev/tty*` to list devices).

---

### 5. Monitor serial output

```bash
~/.platformio/penv/bin/pio device monitor
```

The monitor speed is set to `115200` baud. On startup you will see:

```
===== ESP32 SYSTEM INFO =====
Chip Model: ESP32-D0WDQ6
...
=============================
Kalibrasi gyro... (pastikan sensor diam)
Kalibrasi gyro selesai
Offset X: 12.33
Offset Y: -4.10
Offset Z: 0.87
=================================
Orientasi dikalibrasi
Roll offset: 1.04
Pitch offset: -0.52
=================================
Sistem siap. Publish setiap 5000ms
```

After calibration, every 5 seconds:

```
Roll: 2.14    Pitch: -1.03    Class: 0
```

---

## MQTT Payload

Published to topic `sensors/mpu6050` every **5 seconds**:

```json
{
  "timestamp": 15000,
  "roll": 2.14,
  "pitch": -1.03,
  "class": 0
}
```

| Field | Type | Description |
|---|---|---|
| `timestamp` | `uint32` | Milliseconds since boot (`millis()`) |
| `roll` | `float` | Tilt around X-axis (°), relative to calibration position |
| `pitch` | `float` | Tilt around Y-axis (°), relative to calibration position |
| `class` | `int` | Predicted posture class: `0`, `1`, `2`, or `3` |

---

## Class Labels

The model was trained on four posture classes. Contact the ML team for the mapping; the firmware currently outputs the raw integer index (0–3) matching the training dataset's label encoding.

---

## Hardware Re-calibration

Press and **release** the push button on **GPIO14** at any time. The firmware will:

1. Wait 300 ms for the user to settle.
2. Re-run gyro bias calibration (200 samples, sensor must be **still**).
3. Set the current orientation as the new roll/pitch zero reference.

The button uses an internal pull-up (`INPUT_PULLUP`), so it reads `HIGH` when idle and `LOW` when pressed. The firmware detects the **rising edge** (release event) with 50 ms debounce.

---

## Rebuild After Model Retrain

When the ML team exports a new `model_rf.h`:

1. Replace `../../model/model_rf.h` with the new file.
2. Re-run the split script:
   ```bash
   python3 split_rf_model.py
   ```
3. Build and upload:
   ```bash
   ~/.platformio/penv/bin/pio run --target upload
   ```

The `include/model_rf_split.h` wrapper is **regenerated** each time — do not hand-edit it.

---

## Why `-mtext-section-literals`?

The Xtensa LX6 ISA loads floating-point constants (thresholds in the decision trees) using the `l32r` instruction, which encodes the constant's address as an 18-bit PC-relative offset. This limits the reachable literal pool to ±256 KB from the instruction. A single predict function spanning 50 trees already exceeds that.

The flag `-mtext-section-literals` (set in `platformio.ini`) instructs the assembler to scatter literal entries inline within the `.text` (code) section instead of aggregating them at the section end, ensuring every `l32r` can always reach its literal. This flag is safe and has no effect on runtime behavior.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `model/ does not exist` build error | Split script not run | `python3 split_rf_model.py` |
| `literal target out of range` linker error | Missing `-mtext-section-literals` in `platformio.ini` | Check `build_flags` in `platformio.ini` |
| `l32r` assembler error on a single part file | `TREES_PER_FILE` too large | Reduce `TREES_PER_FILE` in `split_rf_model.py` and re-split |
| MQTT not connecting | Wrong broker IP or firewall | Check `MQTT_SERVER`, ensure broker is reachable |
| All `class` outputs are `0` | Calibration failed or sensor disconnected | Press recalibration button; check I2C wiring |
| Garbled serial output | Wrong baud rate | Set monitor to `115200` baud |
