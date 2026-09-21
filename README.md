# 🛡️ Smart Gas Leakage Detection System

### *An ESP32-based domestic LPG leakage detector with active air sampling, adaptive auto-calibration, and IoT monitoring via Blynk.*

---

# 📖 Table of Contents

1. Project Overview
2. Why This Project
3. Hardware Architecture
4. System Block Diagram
5. Circuit Wiring
6. How the Detection Logic Works
7. Auto-Calibration Explained
8. User-Adjustable Sensitivity
9. The Fan Idea — Active Air Sampling
10. Rate-of-Rise Detection
11. Alarm System
12. Blynk IoT Dashboard
13. Full Firmware Code
14. Installation & Setup
15. User Manual
16. Bill of Materials
17. Testing & Validation
18. Known Limitations
19. Future Improvements
20. License

---
---

# 🔍 Project Overview

### What This Is

This project is an **ESP32-based LPG (Liquefied Petroleum Gas) leakage detector** designed specifically for **domestic homes**. Unlike off-the-shelf detectors that use a fixed hard-coded threshold, this system adapts to its environment.

### Key Features

- **Auto-calibrates** to each home's unique clean-air baseline
- **Adapts to environmental differences** across different homes
- **Gives the user control** over sensitivity
- **Detects both slow leaks and sudden bursts** using rate-of-rise logic
- **Provides remote monitoring and control** through the Blynk IoT app
- **Uses active air sampling** via a small fan for faster response

### Intended Use

The system is intended to be a **sellable consumer product**, not just a hobby prototype. Every design decision — from the fan placement to the calibration routine — was made with real-world deployment in mind.

---
---

# ❓ Why This Project

## The Problem With Common DIY Gas Detectors

Most tutorials online hard-code a single threshold:

```cpp
#define GAS_THRESHOLD 1500
if (analogRead(MQ6_PIN) > GAS_THRESHOLD) alarm();
```

This works in the developer's home but **fails in the real world**.

### Why a Fixed Threshold Fails

| Factor | Impact on Baseline Reading |
| :--- | :--- |
| **Temperature** | Sensor resistance drifts with heat |
| **Humidity** | A coastal home reads very differently from a dry inland home |
| **Background VOCs** | Cooking, cleaning sprays, air fresheners raise the baseline |
| **Sensor-to-sensor variation** | Even same-batch MQ-6 units differ slightly |
| **Aging** | Baseline drifts over months of use |

### The Consequences

A fixed threshold therefore produces two failure modes:

- **False alarms** in homes with high baselines → user frustration, device unplugged
- **Missed leaks** in homes with low baselines → dangerous failure

Neither is acceptable in a safety product.

## Our Solution

A **dynamic, three-layer detection system**:

1. **Auto-calibration** → establishes a per-home baseline
2. **User-adjustable margin** → lets the user tune sensitivity without re-flashing firmware
3. **Rate-of-rise detection** → catches leaks earlier than threshold alone

Combined with an active fan, IoT dashboard, and clear audiovisual feedback, this becomes a product that **actually works** in every home it's installed in.

---
---

# 🧱 Hardware Architecture

## Core Components

| Component | Pin (ESP32) | Function |
| :--- | :--- | :--- |
| **MQ-6 Gas Sensor** | GPIO 34 (ADC) | Analog gas concentration reading |
| **SG90 Servo** | GPIO 13 | Physical gas valve shutoff (0°–180°) |
| **Passive Buzzer #1** | GPIO 25 (PWM) | Fire alarm tone (tone A) |
| **Passive Buzzer #2** | GPIO 26 (PWM) | Fire alarm tone (tone B) |
| **Status LED** | GPIO 2 | Internal indicator (calibration / alarm) |
| **5V Cooling Fan** | Wired directly to 5V | Active air sampling (no GPIO control) |
| **Potentiometer (on MQ-6 module)** | N/A | Adjusts digital output threshold (unused) |

## MQ-6 Potentiometer — Explained

The onboard potentiometer on the MQ-6 module does **not** affect the analog output. It only adjusts the **digital comparator threshold** (the D0 pin).

Since this project uses the **analog pin (A0 → GPIO 34)**, the potentiometer can be left untouched.

It is available as a **hardware fallback** if you ever want a second, purely hardware-based threshold trigger.

---
---

# 📐 System Block Diagram

## High-Level Architecture

```
                    ┌──────────────────────────────────────┐
                    │           ESP32 DevKit               │
                    │                                      │
   [MQ-6 Sensor] ───┤ GPIO 34 (ADC)                        │
                    │                                      │
                    │ GPIO 13 (PWM) ────► [SG90 Servo]     │
                    │                                      │
                    │ GPIO 25 (PWM) ────► [Buzzer 1]       │
                    │ GPIO 26 (PWM) ────► [Buzzer 2]       │
                    │                                      │
                    │ GPIO 2  ──────────► [Status LED]     │
                    │                                      │
                    │ WiFi  ──────────► [Blynk Cloud] ◄──► [Mobile App]
                    └──────────────────────────────────────┘
                                  ▲
                                  │
                            [5V Fan]  ← wired directly to 5V/GND
                                  │
                          ┌───────┴────────┐
                          │  Sheet Baffle  │   ← separates fan from sensor
                          └────────────────┘
                                  │
                            [MQ-6 Sensor]
```

## Physical Layout (Sensor Chamber)

```
        ┌─────────────────────┐
        │   MQ-6  Sensor      │   ← sensor sits above the sheet
        ├─────────────────────┤
        │   Sheet Separator   │   ← air diffuser, prevents direct blast
        ├─────────────────────┤
        │   5V DC Fan         │   ← pushes air upward
        └─────────────────────┘
```

<img width="350" height="454" alt="image" src="https://github.com/user-attachments/assets/01fe195e-6919-4b89-a093-2a5bcb026d2a" />


---
---

# 🔌 Circuit Wiring

## Wiring Table

| ESP32 Pin | Connected To | Notes |
| :--- | :--- | :--- |
| GPIO 34 | MQ-6 A0 | Analog input, ADC1 channel |
| GPIO 13 | Servo signal | PWM output, 50 Hz |
| GPIO 25 | Buzzer 1 (+) | PWM tone, 3 kHz |
| GPIO 26 | Buzzer 2 (+) | PWM tone, 2.5 kHz |
| GPIO 2 | LED (+) via 220Ω | Status indicator |
| 5V | Fan (+), Servo VCC | Common 5V rail |
| 3.3V | MQ-6 VCC | Sensor supply |
| GND | All GNDs | Common ground |

## Notes on Power

- **ESP32 onboard 5V regulator** can typically handle the fan (~200 mA) + servo (peak ~500 mA) + buzzers (~30 mA each).
- If you experience brownouts, power the **fan and servo from a separate 5V supply** with a shared ground.

---
---

# 🧠 How the Detection Logic Works

The firmware uses a **three-layer decision system**. An alarm is raised if *any* of the layers triggers.

## Layer 1 — Absolute Threshold

```cpp
if (gasValue > gasThreshold) → ALARM
```

Where:

```cpp
gasThreshold = baselineValue + sensitivityMargin;
```

## Layer 2 — Rate-of-Rise (Sudden Spike)

```cpp
if (gasValue > (gasThreshold - sensitivityMargin / 2) &&
    (gasValue - lastGasValue) >= RISE_DELTA) → ALARM
```

Catches fast leaks *before* the full threshold is reached.

## Layer 3 — Recovery (Hysteresis)

```cpp
if (gasValue <= (baselineValue + sensitivityMargin / 2)) → ALARM OFF
```

Prevents the alarm from flickering on/off when the reading hovers around the threshold.

## Visualized

```
Reading
  ↑
700 ┤━━━━━━━━━━━━━━━━━━━━━━━  ← gasThreshold  → ALARM ON
    │
550 ┤─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  ← Clear point  → ALARM OFF
    │
400 ┤━━━━━━━━━━━━━━━━━━━━━━━  ← baselineValue (clean air)
    │
  0 ┤
    └────────────────────────→ Time
```

---
---

# 🎯 Auto-Calibration Explained

The single most important design decision in this project.

## Why Calibration Is Necessary

"Clean air" is **not a fixed number**. A raw MQ-6 reading in clean air can range anywhere from **200 to 800** depending on:

- Room temperature
- Humidity level
- Air pressure
- Sensor age and batch
- Background VOCs

Because of this, we cannot ship a device with a hard-coded threshold and expect it to work reliably.

## The Calibration Process

1. **Warm-up (30 seconds)** — sensor heater stabilizes
2. **Sampling (60 seconds)** — collects 120 samples in clean air
3. **Averaging** — the mean becomes `baselineValue`
4. **Threshold derivation** — `gasThreshold = baselineValue + sensitivityMargin`

## Firmware Implementation

```cpp
void startCalibration() {
  calibrating      = true;
  calibrationSum   = 0;
  calibrationCount = 0;
  calibrationStart = millis();
}

void finishCalibration() {
  baselineValue = calibrationSum / calibrationCount;
  calibrated    = true;
  gasThreshold  = baselineValue + sensitivityMargin;
}
```

## User Feedback During Calibration

- **LED blinks at ~2 Hz** throughout the 60-second window
- **Two confirmation beeps** play when calibration completes
- **Blynk label** shows `CALIBRATING` → `CALIBRATED`

This gives the user **clear confirmation** that the device is in calibration mode and should not be disturbed.

<img width="317" height="75" alt="Calibratiing" src="https://github.com/user-attachments/assets/6f0138f3-ebbc-4510-8e27-f3ea6300a732" />

---
---

# 🎚️ User-Adjustable Sensitivity

Not all homes are equal. A kitchen that frequently cooks with strong spices or a workshop with solvents will produce more VOCs than a bedroom.

Rather than forcing one sensitivity for all, the user can tune it **live** through the Blynk slider (Virtual Pin V12).

## Sensitivity Range

| Slider Value | Margin | Behavior |
| :--- | :--- | :--- |
| 100 | 100 pts | Very sensitive — triggers easily |
| 300 | 300 pts | **Balanced (default)** |
| 500 | 500 pts | Tolerant — for kitchens |
| 800 | 800 pts | Very tolerant — only strong leaks |

## How It Works

```cpp
BLYNK_WRITE(V12) {
  int userMargin = param.asInt();
  if (userMargin < SENSITIVITY_MIN) userMargin = SENSITIVITY_MIN;
  if (userMargin > SENSITIVITY_MAX) userMargin = SENSITIVITY_MAX;
  sensitivityMargin = userMargin;
  if (calibrated) gasThreshold = baselineValue + sensitivityMargin;
}
```

The threshold updates **instantly** — no reboot required.

<img width="336" height="72" alt="SensitivityAdjustment" src="https://github.com/user-attachments/assets/dce29108-d9b9-43e6-8235-dad03853d1b4" />



---
---

# 💨 The Fan Idea — Active Air Sampling

The fan is a **deliberate design choice**, not an afterthought.

## Why It Matters

MQ-6 sensors rely on **passive diffusion** of gas into the sensing element. Without airflow:

- Detection can take **10–30+ seconds**
- Heavier gases (LPG/butane) pool in low areas and may not reach the sensor

Adding a fan converts the sensor from *passive* to *active*, cutting response time to **2–5 seconds**.

## The Sheet Separator

The fan is **not** pointed directly at the sensor. A sheet of material sits between them, acting as a **diffuser**. This:

1. **Prevents direct cooling** of the sensing element (which would skew readings)
2. **Spreads airflow evenly** across the sensor
3. **Shields the sensor** from dust and motor noise
4. **Channels gas** smoothly through the sensing chamber

## Ideal Layout

```
   [ Sensor ]
   ──────────   ← sheet with a small gap on one side
   ++++++++++   ← fan pushing air upward
```

A small gap in the sheet is essential — a fully sealed chamber creates a dead air pocket and slows detection.

---
---

# ⚡ Rate-of-Rise Detection

A **sudden burst** of gas is more dangerous than a slow accumulation. Traditional threshold detection may catch it too late.

Rate-of-rise detection watches the **speed of change**, not just the absolute value.

## The Logic

```cpp
if (gasValue > (gasThreshold - sensitivityMargin / 2) &&
    (gasValue - lastGasValue) >= RISE_DELTA &&
    (now - lastRiseCheck) <= RISE_WINDOW_MS) {
    activateAlarm("Sudden rise detected");
}
```

With default values:

- `RISE_DELTA = 20` — must rise by at least 20 points
- `RISE_WINDOW_MS = 1000` — within 1 second

## Example Walkthrough

| Time | Reading | Δ | Decision |
| :--- | :--- | :--- | :--- |
| 0.0s | 400 | — | Normal |
| 0.5s | 410 | +10 | Normal (slow drift) |
| 1.0s | 425 | +15 | Normal |
| 1.5s | **460** | **+35** | **ALARM** — sudden jump detected |

The alarm fires **before** the reading reaches the full threshold — gaining precious seconds.

---
---

# 🚨 Alarm System

Once triggered, the alarm delivers three simultaneous outputs.

## 1. Sound — Fire Alarm Pattern

Both passive buzzers are driven via PWM with an **alternating on/off pattern** to mimic a fire alarm:

```cpp
#define BEEP_ON_MS   300
#define BEEP_OFF_MS  200
```

- **Buzzer 1:** 3000 Hz
- **Buzzer 2:** 2500 Hz

Result: a **loud, pulsing, urgent** alarm that cuts through household noise.

## 2. Servo — 90° Rotation

```cpp
myServo.write(90);
```

The servo rotates 90°, designed to operate a **physical gas valve** for automatic shutoff.

If a valve isn't installed, it still provides **visual confirmation** of alarm state.

## 3. LED — Steady On

GPIO 2 drives a status LED that stays on throughout the alarm for local indication.

## Auto-Recovery

When gas drops below the hysteresis point:

```cpp
if (gasValue <= (baselineValue + sensitivityMargin / 2)) deactivateAlarm();
```

The buzzer silences, LED turns off. No user intervention needed.

**📷 *[Placeholder: Video/GIF of buzzer + servo alarm in action]***

---
---

# 📱 Blynk IoT Dashboard

The Blynk app provides **remote monitoring and control**.

## Virtual Pin Map

| Pin | Widget | Purpose |
| :--- | :--- | :--- |
| **V8** | Gauge | Live gas reading |
| **V9** | Slider (0–180) | Manual servo control |
| **V10** | Button | Reset alarm |
| **V11** | Button | Test buzzers |
| **V12** | Slider (100–800) | Sensitivity adjustment |
| **V13** | Label | Calibration status |
| **V14** | Button | Trigger re-calibration |

## Dashboard Layout

```
┌─────────────────────────────────────┐
│           GAS MONITOR               │
├─────────────────────────────────────┤
│                                     │
│         [ GAS GAUGE  V8 ]           │
│                                     │
│  Sensitivity     [Slider V12]       │
│  Servo           [Slider V9]        │
│  Status          [Label  V13]       │
│                                     │
│  [Reset V10]  [Test V11]  [Cal V14] │
└─────────────────────────────────────┘
```

<img width="350" height="450" alt="WhatsApp Image 2026-09-21 at 22 32 08" src="https://github.com/user-attachments/assets/bacd91ea-0ef6-4191-a916-3f2ce340dd7d" />

**📷 *[Placeholder: Screenshot of Blynk during alarm state]***

---
---

# 💻 Full Firmware Code

See [`gas_detector.ino`](./gas_detector.ino) in this repository.

## Code Organization

The code is organized into clearly commented sections:

- **Setup / Loop**
- **Sensor Reading**
- **Calibration Routine**
- **Alarm Control**
- **Beep Pattern**
- **Blynk Handlers**

## Key Constants (Tunable at the Top)

```cpp
#define CALIBRATION_DURATION_MS  60000UL
#define CALIBRATION_SAMPLES      120
#define DEFAULT_MARGIN           300
#define SENSITIVITY_MIN          100
#define SENSITIVITY_MAX          800
#define RISE_DELTA               20
#define RISE_WINDOW_MS           1000
#define BEEP_ON_MS               300
#define BEEP_OFF_MS              200
#define CAL_LED_ON_MS            250
#define CAL_LED_OFF_MS           250
#define CAL_BEEP_ON_MS           400
#define CAL_BEEP_GAP_MS          300
```

---
---

# ⚙️ Installation & Setup

## 1. Prerequisites

- **Arduino IDE** 2.x or PlatformIO
- **ESP32 board package** (Espressif)
- Libraries:
  - `Blynk` by Volodymyr Shymanskyy
  - `ESP32Servo` by Kevin Harrington

## 2. Blynk Configuration

1. Create a **Blynk Template** on [blynk.cloud](https://blynk.cloud)
2. Add the **Virtual Pins** listed above
3. Copy your:
   - `BLYNK_TEMPLATE_ID`
   - `BLYNK_TEMPLATE_NAME`
   - `BLYNK_AUTH_TOKEN`
4. Paste them into the top of the firmware

## 3. WiFi Credentials

```cpp
char ssid[] = "YourWiFiSSID";
char pass[] = "YourWiFiPassword";
```

## 4. Flash the ESP32

- Board: **ESP32 Dev Module**
- Upload Speed: 921600
- Flash Frequency: 80 MHz

## 5. First Boot Sequence

```
Power ON
   ↓
30s warm-up (LED off)
   ↓
60s calibration (LED blinks)
   ↓
"beep... beep" (calibration done)
   ↓
Ready — normal operation begins
```

---
---

# 📘 User Manual

## For the Homeowner

### First-Time Setup

1. Place the device in an open area **away from the stove**
2. Ensure **windows are open** for fresh air
3. Plug in the device
4. **Do not touch it for 90 seconds** while it calibrates (LED will blink)
5. Wait for the **two confirmation beeps**
6. Device is now armed and monitoring

### Normal Operation

- LED off, silent → all good
- LED on + alarm sound + servo moves → **gas detected, ventilate immediately**

### If False Alarms Happen

1. Open the Blynk app
2. Move the **Sensitivity slider (V12)** higher by 50–100 points
3. Wait 30 seconds — the new threshold takes effect immediately

### If You Suspect Sensor Drift (after months)

1. Open the Blynk app
2. Tap **Re-Calibrate (V14)**
3. Ensure the room is in clean air
4. Wait 60 seconds for the blink pattern to stop

---
---

# 🧾 Bill of Materials

| Component | Qty | Approx. Cost (INR) |
| :--- | :--- | :--- |
| ESP32 DevKit V1 | 1 | ₹350 |
| MQ-6 Gas Sensor Module | 1 | ₹180 |
| SG90 Micro Servo | 1 | ₹120 |
| Passive Buzzer (5V) | 2 | ₹30 |
| 5V Cooling Fan (30mm) | 1 | ₹68 |
| LED + 220Ω resistor | 1 | ₹5 |
| Perfboard / PCB | 1 | ₹50 |
| Enclosure | 1 | ₹100 |
| 5V 2A Power Supply | 1 | ₹150 |
| **Total** | | **≈ ₹1,050** |

---
---

# 🧪 Testing & Validation

## Bench Tests

| Test | Method | Result |
| :--- | :--- | :--- |
| Clean-air baseline stability | 10-min continuous reading | ✅ ±15 points |
| Response to lighter gas | Release near intake | ✅ Alarm < 3 s |
| Servo activation | Observe physical position | ✅ Rotates to 90° |
| Buzzer pattern | Listen | ✅ Clear beep-pause-beep |
| Blynk sync latency | Observe gauge | ✅ < 1 s |
| Auto-recovery | Ventilate after alarm | ✅ Alarm clears |

## Field Tests

Tested in **three different homes** with different:

- Ambient temperatures (22°C, 28°C, 35°C)
- Humidity levels (30%, 55%, 80%)
- Background VOCs (cooking, cleaning)

The auto-calibration adapted correctly in all cases, and no false alarms occurred after the sensitivity was tuned.

**📷 *[Placeholder: Photo of sensor response graph]***

**📷 *[Placeholder: Photo of test setup with lighter]***

---
---

# ⚠️ Known Limitations

1. **Long-term baseline drift** — Recalibration is recommended every 3–6 months
2. **No temperature/humidity compensation** — Future revisions could add a DHT22 for automatic correction
3. **No flash persistence** — Calibration resets on reboot (planned for next version)
4. **MQ-6 cross-sensitivity** — Also responds to alcohol, methane, and smoke
5. **Servo may not close a real valve** — SG90 lacks torque for gas cylinder valves

---
---

# 🚀 Future Improvements

- [ ] Store calibration in **NVS flash** to survive reboots
- [ ] Add **DHT22** for temperature/humidity compensation
- [ ] Add **OLED display** for local reading
- [ ] Add **battery backup**
- [ ] Migrate to a **higher-torque servo** for real valve control
- [ ] Add **mobile push notifications** on alarm
- [ ] Add **daily log** of readings to Blynk
- [ ] FCC/CE certification for commercial sale

---
---

# 📜 License

MIT License — see [LICENSE](./LICENSE) for details.

---
---

# 🙏 Acknowledgements

- **Blynk IoT Platform** for the free cloud dashboard
- **Espressif** for the ESP32 Arduino core
- **Winsen Electronics** for the MQ-6 datasheet

---

**📷 *[Placeholder: Product hero shot of finished device]***

---

*Last updated: 2026-09-21*
