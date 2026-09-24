/*
 * ============================================================
 *  Smart Gas Leakage Detection System
 * ------------------------------------------------------------
 *  ESP32-based domestic LPG leakage detector with:
 *    - MQ-6 analog gas sensing
 *    - Active air sampling (5V fan + sheet diffuser)
 *    - Auto-calibration of clean-air baseline
 *    - User-adjustable sensitivity via Blynk
 *    - Rate-of-rise detection
 *    - Fire-alarm style buzzer pattern
 *    - Servo-driven gas valve shutoff
 *    - Blynk IoT dashboard
 * ------------------------------------------------------------
 *  Author : Syed Hashir Ahmed
 *  License: MIT
 *  Repo   : https://github.com/SyedHashirA
 * ============================================================
 */

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>

// ================== BLYNK CREDENTIALS ==================
#define BLYNK_TEMPLATE_ID   "YourTemplateID"
#define BLYNK_TEMPLATE_NAME "YourTemplateName"
#define BLYNK_AUTH_TOKEN    "YourAuthToken"

char ssid[] = "YourWiFiSSID";
char pass[] = "YourWiFiPassword";

// ================== PIN DEFINITIONS ==================
#define MQ6_PIN     34
#define SERVO_PIN   13
#define BUZZER1_PIN 25
#define BUZZER2_PIN 26
#define LED_PIN     2

// ================== CALIBRATION SETTINGS ==================
#define CALIBRATION_DURATION_MS  60000UL  // 60 seconds of clean-air sampling
#define CALIBRATION_SAMPLES      120      // 1 sample every 500 ms
#define WARMUP_DELAY_MS          30000UL  // 30 s warm-up before calibration
#define DEFAULT_MARGIN           300      // Fallback offset before calibration
#define SENSITIVITY_MIN          100      // Slider low  -> very sensitive
#define SENSITIVITY_MAX          800      // Slider high -> less sensitive

// ================== RISE DETECTION ==================
#define RISE_DELTA        20     // Min jump between readings to trigger
#define RISE_WINDOW_MS    1000   // Time window for a "sudden" rise

// ================== BEEP PATTERN (Alarm) ==================
#define BEEP_ON_MS   300
#define BEEP_OFF_MS  200

// ================== CALIBRATION LED & BEEP ==================
#define CAL_LED_ON_MS    250   // LED blink ON duration during calibration
#define CAL_LED_OFF_MS   250   // LED blink OFF duration during calibration
#define CAL_BEEP_ON_MS   400   // Confirmation beep ON duration
#define CAL_BEEP_GAP_MS  300   // Gap between the two confirmation beeps

// ================== GLOBALS ==================
Servo myServo;
BlynkTimer timer;

int gasValue        = 0;
int lastGasValue    = 0;
unsigned long lastRiseCheck = 0;

// ---------- Calibration State Machine ----------
enum CalibrationState {
  CAL_IDLE    = 0,
  CAL_RUNNING = 1,
  CAL_DONE    = 2,
  CAL_FAILED  = 3
};

CalibrationState calState = CAL_IDLE;

// Calibration data
bool calibrating        = false;
bool calibrated         = false;
long calibrationSum     = 0;
int  calibrationCount   = 0;
int  baselineValue      = 0;
unsigned long calibrationStart = 0;

// Calibration LED blink state
bool calLedState = false;
unsigned long lastCalLedToggle = 0;

// Calibration confirmation beep state machine
bool playingConfirmBeep   = false;
int  confirmBeepStage     = 0;   // 1=beep1 ON, 2=gap, 3=beep2 ON
unsigned long confirmBeepTimer = 0;

// Threshold
int gasThreshold      = DEFAULT_MARGIN;
int sensitivityMargin = DEFAULT_MARGIN;

// Alarm state
bool alarmActive = false;
bool testMode    = false;

// Alarm beep state
bool beepState = false;
unsigned long lastBeepToggle = 0;

// ================== FORWARD DECLARATIONS ==================
void readGasSensor();
void updateBeepPattern();
void updateCalibrationLed();
void updateConfirmBeep();
void startCalibration();
void finishCalibration();
void activateAlarm(const char* reason);
void deactivateAlarm();
void updateCalStatusWidget();
String calStateToString(CalibrationState s);

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  // Pins
  pinMode(MQ6_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Servo
  myServo.attach(SERVO_PIN);
  myServo.write(0);

  // Buzzer PWM (ESP32 core 3.x API)
  ledcAttach(BUZZER1_PIN, 2000, 10);
  ledcAttach(BUZZER2_PIN, 2000, 10);
  ledcWriteTone(BUZZER1_PIN, 0);
  ledcWriteTone(BUZZER2_PIN, 0);

  // Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Periodic tasks
  timer.setInterval(500L, readGasSensor);
  timer.setInterval(50L,  updateBeepPattern);
  timer.setInterval(50L,  updateCalibrationLed);
  timer.setInterval(50L,  updateConfirmBeep);

  Serial.println("==================================================");
  Serial.println("Smart Gas Leakage Detection System");
  Serial.println("Booting...");
  Serial.println("Waiting for MQ-6 warm-up (30 s recommended)");
  Serial.println("==================================================");

  // Warm-up
  delay(WARMUP_DELAY_MS);

  // Auto-start calibration
  startCalibration();
}

// ================== LOOP ==================
void loop() {
  Blynk.run();
  timer.run();
}

// ================== SENSOR READ ==================
void readGasSensor() {
  gasValue = analogRead(MQ6_PIN);
  unsigned long now = millis();

  // ---------- Calibration in progress ----------
  if (calibrating) {
    calibrationSum += gasValue;
    calibrationCount++;

    Blynk.virtualWrite(V8, gasValue);

    Serial.print("Calibrating... sample ");
    Serial.print(calibrationCount);
    Serial.print("/");
    Serial.print(CALIBRATION_SAMPLES);
    Serial.print("  value=");
    Serial.println(gasValue);

    if (calibrationCount >= CALIBRATION_SAMPLES ||
        (now - calibrationStart) >= CALIBRATION_DURATION_MS) {
      finishCalibration();
    }
    return;
  }

  // ---------- Normal operation ----------
  Serial.print("Gas Value: ");
  Serial.print(gasValue);
  Serial.print(" | Baseline: ");
  Serial.print(baselineValue);
  Serial.print(" | Threshold: ");
  Serial.print(gasThreshold);
  Serial.print(" | Last: ");
  Serial.println(lastGasValue);

  Blynk.virtualWrite(V8, gasValue);

  // Update threshold dynamically if sensitivity changed
  gasThreshold = baselineValue + sensitivityMargin;

  // ========== CONDITION 1: Above absolute threshold ==========
  if (gasValue > gasThreshold && !alarmActive) {
    activateAlarm("Threshold exceeded");
  }

  // ========== CONDITION 2: Sudden rise near threshold ==========
  else if (!alarmActive &&
           gasValue > (gasThreshold - sensitivityMargin / 2) &&
           (gasValue - lastGasValue) >= RISE_DELTA &&
           (now - lastRiseCheck) <= RISE_WINDOW_MS) {
    activateAlarm("Sudden rise detected");
  }

  // ========== CONDITION 3: Back to normal ==========
  else if (gasValue <= (baselineValue + sensitivityMargin / 2) &&
           alarmActive && !testMode) {
    deactivateAlarm();
  }

  lastGasValue  = gasValue;
  lastRiseCheck = now;
}

// ================== CALIBRATION ==================
void startCalibration() {
  Serial.println(">> Starting auto-calibration...");
  Serial.println(">> Ensure the device is in CLEAN AIR.");
  Serial.println(">> Do NOT use gas appliances during calibration.");

  calibrating      = true;
  calibrated       = false;
  calibrationSum   = 0;
  calibrationCount = 0;
  calibrationStart = millis();

  // Reset LED blink state
  calLedState      = false;
  lastCalLedToggle = millis();
  digitalWrite(LED_PIN, LOW);

  // Reset confirmation beep state
  playingConfirmBeep = false;
  confirmBeepStage   = 0;

  calState = CAL_RUNNING;
  updateCalStatusWidget();
}

void finishCalibration() {
  calibrating = false;

  if (calibrationCount == 0) {
    Serial.println("!! Calibration failed: no samples.");
    calState = CAL_FAILED;
    updateCalStatusWidget();
    return;
  }

  baselineValue = calibrationSum / calibrationCount;
  calibrated    = true;

  gasThreshold = baselineValue + sensitivityMargin;

  Serial.println("==================================================");
  Serial.print("Calibration COMPLETE. Baseline = ");
  Serial.println(baselineValue);
  Serial.print("Alarm threshold = ");
  Serial.println(gasThreshold);
  Serial.println("==================================================");

  // Turn LED OFF (beep starts next)
  digitalWrite(LED_PIN, LOW);

  // Start two confirmation beeps
  startConfirmBeep();

  calState = CAL_DONE;
  updateCalStatusWidget();
}

String calStateToString(CalibrationState s) {
  switch (s) {
    case CAL_IDLE:    return "IDLE";
    case CAL_RUNNING: return "CALIBRATING";
    case CAL_DONE:    return "CALIBRATED";
    case CAL_FAILED:  return "FAILED";
    default:          return "UNKNOWN";
  }
}

void updateCalStatusWidget() {
  Blynk.virtualWrite(V13, calStateToString(calState));
}

// ================== CALIBRATION LED BLINK ==================
void updateCalibrationLed() {
  if (!calibrating) return;

  unsigned long now = millis();
  unsigned long interval = calLedState ? CAL_LED_ON_MS : CAL_LED_OFF_MS;

  if (now - lastCalLedToggle >= interval) {
    calLedState = !calLedState;
    digitalWrite(LED_PIN, calLedState ? HIGH : LOW);
    lastCalLedToggle = now;
  }
}

// ================== CONFIRMATION BEEP (2 beeps) ==================
void startConfirmBeep() {
  Serial.println(">> Playing calibration confirmation beeps...");
  playingConfirmBeep = true;
  confirmBeepStage   = 1;   // start with beep 1 ON
  confirmBeepTimer   = millis();

  ledcWriteTone(BUZZER1_PIN, 2500);
  ledcWriteTone(BUZZER2_PIN, 2000);
}

void updateConfirmBeep() {
  if (!playingConfirmBeep) return;

  unsigned long now = millis();

  switch (confirmBeepStage) {
    case 1:  // Beep 1 ON
      if (now - confirmBeepTimer >= CAL_BEEP_ON_MS) {
        ledcWriteTone(BUZZER1_PIN, 0);
        ledcWriteTone(BUZZER2_PIN, 0);
        confirmBeepStage = 2;
        confirmBeepTimer = now;
      }
      break;

    case 2:  // Gap
      if (now - confirmBeepTimer >= CAL_BEEP_GAP_MS) {
        ledcWriteTone(BUZZER1_PIN, 2500);
        ledcWriteTone(BUZZER2_PIN, 2000);
        confirmBeepStage = 3;
        confirmBeepTimer = now;
      }
      break;

    case 3:  // Beep 2 ON
      if (now - confirmBeepTimer >= CAL_BEEP_ON_MS) {
        ledcWriteTone(BUZZER1_PIN, 0);
        ledcWriteTone(BUZZER2_PIN, 0);
        confirmBeepStage = 4;
        playingConfirmBeep = false;
        Serial.println(">> Confirmation beeps done.");
      }
      break;

    default:
      playingConfirmBeep = false;
      break;
  }
}

// ================== ALARM CONTROL ==================
void activateAlarm(const char* reason) {
  alarmActive = true;
  digitalWrite(LED_PIN, HIGH);
  myServo.write(90);

  Serial.print(">>> ALARM ACTIVATED: ");
  Serial.println(reason);

  Blynk.virtualWrite(V13, "ALARM");
}

void deactivateAlarm() {
  alarmActive = false;
  digitalWrite(LED_PIN, LOW);
  myServo.write(0);
  ledcWriteTone(BUZZER1_PIN, 0);
  ledcWriteTone(BUZZER2_PIN, 0);

  Serial.println(">>> Alarm cleared.");

  updateCalStatusWidget();
}

// ================== ALARM BEEP PATTERN ==================
void updateBeepPattern() {
  // Don't override the calibration confirmation beeps
  if (playingConfirmBeep) return;

  if (!alarmActive && !testMode) {
    ledcWriteTone(BUZZER1_PIN, 0);
    ledcWriteTone(BUZZER2_PIN, 0);
    return;
  }

  unsigned long now = millis();

  if (beepState) {
    if (now - lastBeepToggle >= BEEP_ON_MS) {
      beepState = false;
      lastBeepToggle = now;
    }
  } else {
    if (now - lastBeepToggle >= BEEP_OFF_MS) {
      beepState = true;
      lastBeepToggle = now;
    }
  }

  if (beepState) {
    ledcWriteTone(BUZZER1_PIN, 3000);
    ledcWriteTone(BUZZER2_PIN, 2500);
  } else {
    ledcWriteTone(BUZZER1_PIN, 0);
    ledcWriteTone(BUZZER2_PIN, 0);
  }
}

// ================== BLYNK HANDLERS ==================

// V9 -> Servo slider (0-180)
BLYNK_WRITE(V9) {
  int servoPos = param.asInt();
  myServo.write(servoPos);
}

// V10 -> Reset button
BLYNK_WRITE(V10) {
  if (param.asInt() == 1) {
    deactivateAlarm();
    myServo.write(0);
    ledcWriteTone(BUZZER1_PIN, 0);
    ledcWriteTone(BUZZER2_PIN, 0);
    Serial.println("System reset from Blynk");
  }
}

// V11 -> Test buzzer button
BLYNK_WRITE(V11) {
  if (param.asInt() == 1) {
    testMode = true;
    Serial.println("Buzzer test ON");
  } else {
    testMode = false;
    if (!alarmActive) {
      ledcWriteTone(BUZZER1_PIN, 0);
      ledcWriteTone(BUZZER2_PIN, 0);
    }
    Serial.println("Buzzer test OFF");
  }
}

// V12 -> Sensitivity slider (100 .. 800)
BLYNK_WRITE(V12) {
  int userMargin = param.asInt();

  if (userMargin < SENSITIVITY_MIN) userMargin = SENSITIVITY_MIN;
  if (userMargin > SENSITIVITY_MAX) userMargin = SENSITIVITY_MAX;

  sensitivityMargin = userMargin;

  if (calibrated) {
    gasThreshold = baselineValue + sensitivityMargin;
  }

  Serial.print("Sensitivity updated. Margin = ");
  Serial.print(sensitivityMargin);
  Serial.print(" | New threshold = ");
  Serial.println(gasThreshold);
}

// V14 -> Re-calibrate button
BLYNK_WRITE(V14) {
  if (param.asInt() == 1) {
    Serial.println("Re-calibration requested from Blynk");
    startCalibration();
  }
}
