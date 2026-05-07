#include "Constants.h"
#include <Wire.h>
#include "Power_Manager.h"
#include "Calibration_Manager.h"
#include "BLE_Handler.h"
#include "ABPM_Manager.h"
#include "Display_Handler.h"
#include "BloodPressure.h"
#include <Arduino.h>
#include "DynamicInflation.h"
#include "Filter.h"
#include "Storage.h"
#include "Buzzer_Handler.h"
#include "POST_Manager.h"

// PWM control parameters
const int PWM_START = 255;            // Starting PWM value
const int PWM_MAX = 255;              // Maximum PWM value
const float PRESSURE_THRESHOLD = 50; // Start increasing PWM after this pressure
// Linear Deflation Parameters
const float DEFLATION_RATE = 3.5;     // mmHg per second

// PID Controller Parameters for Valve Control
float Kp = 1;
float Ki = 0.01;
float Kd = 0.0;

static uint32_t lastTime_inflation = 0;

#define MANUFACTURE_NAME            "IF MedTech Pvt. Ltd."
#define FIRMWARE_VERSION            CURRENT_FIRMWARE_VERSION

// Circular buffer for runtime data collection
float data[MAX_DATA_POINTS];
static int fill_index = 0;
volatile bool measuring = false;
volatile bool inflating = false;
static int currentPWM = PWM_START;
volatile DeviceState currentBLEState = IDLE;

// Arrays for processing - Globals (externed in BloodPressure.cpp to save RAM)
float filtered_data[MAX_DATA_POINTS];
float x_fft_uenv[MAX_DATA_POINTS];
float x_f_lenv[MAX_DATA_POINTS];
int peaks[MAX_DATA_POINTS / 8];
int valleys[MAX_DATA_POINTS / 8];
static int main_peaks[5];

// [gkrish] Dynamic valve control based on mean_at_inflation
static bool  valveOpenByMean = false;
static float valveOpenThreshold = 0.0f;   // pressure at which we open valve

// Calibration handle
#define pressureCalibM Calibration_Manager::calibM
#define pressureCalibC Calibration_Manager::calibC

// Global variables for BP values
volatile float systolicBP = 0;
volatile float diastolicBP = 0;
volatile float heartRate = 0;
volatile int bpErrorCode = 0;
volatile uint8_t measurementCounter = 0;

void startMeasurement();
void stopMeasurement();
void handleChargingMode();
void simpleInflation();
void runLinearDeflationControl(uint32_t currentTime);

// UI & Button state
bool screenOn = true;
uint32_t lastActivityTime = 0;
bool isIO19Locked = false;         
uint32_t io19PressStartTime = 0;
static bool io19NeedsFreshPress = false; // Requires full release+press cycle after STOP
static bool io18WasPressed = false;
static uint32_t io18PressStartTime = 0;
static bool menuTriggeredThisPress = false;
static uint32_t io18UnlockCooldownTime = 0; // Blocks ghost toggle after long-press unlock
static bool io18NeedsFreshPress = false;    // Requires full release+press cycle after unlock
const uint32_t SCREEN_TIMEOUT_MS = 60000; 
bool measurementStartedViaBLE = false;
bool g_resultsNeedRedraw = true; // Reset each new measurement, forces result screen redraw

// ── PCF8574 Button Helper ──────────────────────────────────────────────────
// Reads one byte from PCF8574. Only START button is on P3 (active LOW).
// WAKE button is on GPIO18 (direct digitalRead).
static uint8_t _readPCF8574Buttons() {
    uint8_t bytesReceived = Wire.requestFrom((uint8_t)PCF8574_ADDR, (uint8_t)1);
    if (bytesReceived == 0) {
        Serial.printf("[PCF_DBG] I2C ERROR: requestFrom(0x%02X) returned 0 bytes! Bus issue or wrong address.\n", PCF8574_ADDR);
        return 0xFF; // Default: all HIGH = no buttons pressed
    }
    if (Wire.available()) {
        uint8_t val = Wire.read();
        return val;
    }
    Serial.println("[PCF_DBG] I2C ERROR: requestFrom OK but Wire.available() = 0. Unexpected.");
    return 0xFF;
}
// Blocking helper used in MAINTENANCE_MENU power-off wait
static bool _wakeButtonPressed() { return (digitalRead(BUTTON_WAKE_PIN) == LOW); }
// ─────────────────────────────────────────────────────────────────────────────

// BLE Advertising Timeout (independent of screen state)
uint32_t bleAdvertisingStartTime = 0;  // Time when advertising started
const uint32_t BLE_ADV_TIMEOUT_MS = 60000; // 1 minute advertising window

// State machine - using DeviceState from Constants.h
DeviceState currentState = IDLE;
bool systemActive = false;       // New: Manual unlock status
unsigned long standbyStartTime = 0; 
unsigned long stateStartTime = 0;

// PID Control Variables for Linear Deflation
float integralError = 0.0;
float previousError = 0.0; 
float currentValvePWM = 0.0;
float lastError = 0.0; // For debugging
float deflationStartPressure = 0.0;
// Global variables for metrics
uint32_t inflationStartTime = 0;
uint32_t inflationStopTime = 0;
uint32_t valveOpenTime = 0;
float inflationStopPressure = 0.0f;
extern float s_meanAtInflation;
float _readPressure(void);

// ========== BLE Command Handlers ==========

void handleBLEAuth(bool authorized) {
  if (authorized) {
    Serial.println("[Main] BLE client authorized");
  } else {
    Serial.println("[Main] BLE authentication failed");
  }
}

void handleBLECommand(uint8_t cmd) {
  Serial.print("[Main] handleBLECommand called with: 0x");
  Serial.println(cmd, HEX);
  
  switch(cmd) {
    case BLE_Handler::CMD_START:
      Serial.println("[Main] START command processing...");
      if (!measuring) {
        startMeasurement();
        currentBLEState = INFLATING;
        BLE_Handler::getInstance().notifyStatus(INFLATING);
      } else {
        Serial.println("[Main] ✗ Start ignored: Already measuring.");
      }
      break;
      
    case BLE_Handler::CMD_STOP:
      // BLE STOP: Abort current measurement only - do NOT kill ABPM Schedule
      measuring = false;
      inflating = false;
      currentState = IDLE;
      ledcWrite(MOTOR_PIN, 0);
      ledcWrite(VALVE_PIN, 0);
      ABPM_Manager::clearPendingTrigger();
      fill_index = 0;
      memset(data, 0, sizeof(data));
      DI_Reset();
      BLE_Handler::getInstance().notifyStatus(IDLE);
      Serial.println("[Main] 🛑 BLE STOP: Measurement aborted (ABPM schedule preserved).");
      break;

    case BLE_Handler::CMD_SYNC_HISTORY:
      Serial.println("[Main] SYNC_HISTORY requested");
      BLE_Handler::getInstance().syncHistory();
      break;

    case 0x05: // ABPM Toggle (Manual override: only allow STOP if active)
      if (abpmMode) {
          ABPM_Manager::setConfig(nullptr, 0);
          Serial.println("[Main] ABPM Mode STOPPED via manual toggle");
      }
      break;

    case BLE_Handler::CMD_RESET:
      Serial.println("[Main] RESET command received - Full Factory Wipe");
      
      // Wipe ABPM Settings
      ABPM_Manager::setConfig(nullptr, 0);
      
      // Emergency stop
      ledcWrite(MOTOR_PIN, 0); 
      ledcWrite(VALVE_PIN, 0);  // Open valve
      
      fill_index = 0;
      systolicBP = 0;
      diastolicBP = 0;
      bpErrorCode = 0;
      heartRate = 0;
      measurementCounter = 0;
      
      // Wipe clinical history logs from flash
      Storage::clearHistory();
      
      memset(data, 0, sizeof(data));
      memset(filtered_data, 0, sizeof(filtered_data));
      
      delay(500);
      ESP.restart();
      break;
    default:
      Serial.print("[Main] ✗ Unknown command: 0x");
      Serial.println(cmd, HEX);
      break;
  }
  BLE_Handler::getInstance().clearLastCommand();
}


void startMeasurement() {
  if (measuring) return; // Prevent double start

  measuring = true;
  inflating = true;
  fill_index = 0;
  bpErrorCode = 0;

  DI_Reset();                      // [gkrish] reset dynamic inflation state
  valveOpenByMean = false;         // [gkrish]
  valveOpenThreshold = 0.0f;       // [gkrish]

  // Reset metrics
  inflationStartTime = 0;
  inflationStopTime = 0;
  valveOpenTime = 0;
  valveOpenTime = 0;
  inflationStopPressure = 0.0f;

  currentPWM = PWM_MAX;
  ledcWrite(MOTOR_PIN, currentPWM);
  inflationStartTime = millis();
  ledcWrite(VALVE_PIN, 1023); // Close valve
  Serial.println("Starting BP measurement: Inflating cuff...");
  Display_Handler::showInflating(true, Display_Handler::getBatteryPercent(), abpmMode, BLE_Handler::getInstance().isConnected(), (bleAdvertisingStartTime > 0));
  currentState = INFLATING;
  stateStartTime = millis();
  // Signal that a new measurement cycle has started — forces results redraw later
  extern bool g_resultsNeedRedraw;
  g_resultsNeedRedraw = true;
}

void stopMeasurement() {
  // Hard reset all logical states
  measuring = false;
  inflating = false;
  currentState = IDLE;
  
  // Hard reset all hardware
  ledcWrite(MOTOR_PIN, 0);   
  ledcWrite(VALVE_PIN, 0);   // Open valve fully

  // Clear the current cycle's trigger flags
  ABPM_Manager::clearPendingTrigger();
  ABPM_Manager::clearPreAlarm();

  // CRITICAL: If ABPM is active, reschedule the NEXT reading from NOW.
  // Without this, the live trigger check (now >= nextScheduledReading) is still
  // true and will fire a new measurement within milliseconds of this stop.
  if (abpmMode) {
    Serial.println("[Main] ABPM rescheduling next reading from now...");
    ABPM_Manager::updateSchedule();
  }

  Serial.println("[Main] 🛑 Hardware STOP: Current measurement aborted. ABPM schedule unchanged.");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.print("  Device: "); Serial.println(MANUFACTURE_NAME);
  Serial.print("  Firmware: "); Serial.println(FIRMWARE_VERSION);
  Serial.println("========================================\n");

  // Initialize I2C early to ensure sensor is reachable
  Wire.begin(PRESSURE_SDA, PRESSURE_SCL);
  Serial.println("[System] I2C Bus Initialized");
  Buzzer_Handler::init();

  // ── Reset Reason Detection ──────────────────────────────────────────────
  // esp_reset_reason() reliably detects genuine resets (power-on, flash,
  // ESP.restart(), panic) unlike esp_sleep_get_wakeup_cause() which can
  // return a stale value after a software reset on ESP32-C3.
  esp_reset_reason_t reset_reason = esp_reset_reason();
  bool isGenuineReset = (reset_reason == ESP_RST_POWERON  ||   // Power cable / battery
                         reset_reason == ESP_RST_SW       ||   // Flash upload / ESP.restart()
                         reset_reason == ESP_RST_PANIC    ||   // Crash / exception
                         reset_reason == ESP_RST_BROWNOUT ||   // Low voltage
                         reset_reason == ESP_RST_WDT      ||   // Watchdog
                         reset_reason == ESP_RST_UNKNOWN);

  if (isGenuineReset) {
    // Force ABPM OFF — user must explicitly re-enable via app after every power cycle
    abpmMode = false;
    nextScheduledReading = 0;
    Serial.printf("[System] Reset reason: %d → ABPM cleared (OFF by default)\n", (int)reset_reason);
  }

  // Power Logic: Check entry path (only relevant for Deep Sleep, not Light Sleep)
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (!isGenuineReset && wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    currentState = IDLE;
    systemActive = true;
    Serial.println("[System] Timer Wakeup - Skipping Lock Screen");
  } else {
    currentState = WAIT_FOR_START;
    systemActive = false;
    standbyStartTime = millis();
    Serial.println("[System] Manual Wakeup - Showing LOCK Screen");
  }

  // Handle Internal ABPM Logic
  ABPM_Manager::handleWakeup();
  ABPM_Manager::init();

  Serial.println("[DEBUG] Mounting SPIFFS...");
  Calibration_Manager::init(); // Handled internally by SPIFFS.begin now
  
  // Initialize WAKE button on GPIO18 (direct GPIO, also used for light-sleep wakeup)
  pinMode(BUTTON_WAKE_PIN, INPUT_PULLUP);
  // Initialize PCF8574 — write 0xFF to set all pins as inputs with pull-ups
  // START button is on P3; P4 is free.
  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(0xFF);
  Wire.endTransmission();
  Serial.println("[System] GPIO18 (WAKE) + PCF8574 (START=P3) initialized.");
  lastActivityTime = millis();
  
  Serial.println("[DEBUG] Reading calibration...");
  Calibration_Manager::load();
  Serial.flush();

  Serial.println("[DEBUG] Initializing PWM setup for Motor and Valve...");
  Serial.flush();
  
  // 2. Initialize Motor (5kHz, 8-bit)
  ledcAttach(MOTOR_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcWrite(MOTOR_PIN, 0); // Motor OFF
  
  // 3. Initialize Valve (5kHz, 10-bit)
  pinMode(VALVE_PIN, OUTPUT);
  ledcAttach(VALVE_PIN, VALVE_PWM_FREQ, VALVE_PWM_RES);
  ledcWrite(VALVE_PIN, 1023); // Valve CLOSED (PWM 1023)
  
  Serial.println("[DEBUG] Initializing BLE Handler...");
  Serial.printf("[DEBUG] Free Heap before BLE: %u bytes\n", ESP.getFreeHeap());
  Serial.flush();
  BLE_Handler& bleHandler = BLE_Handler::getInstance();
  bleHandler.init();
  bleHandler.setCommandCallback(handleBLECommand);
  bleHandler.setAuthCallback(handleBLEAuth);

  Serial.println("[DEBUG] Setup complete - waiting for BLE connection...");
  Serial.flush();

  // Initialize Display and Power
  Display_Handler::init();
  Power_Manager::init();

  // Force screen OFF initially for Dark Boot
  Display_Handler::off();
  screenOn = false;

  // POST and Wait-for-start are now handled after long-press in loop()
}

void loop() {
  Buzzer_Handler::tick(); // Process any active non-blocking beep patterns (like pre-alarm)
  BLE_Handler& bleHandler = BLE_Handler::getInstance();
  
  // WAKE button: direct GPIO18 read (fast, also supports sleep wakeup)
  // START button: PCF8574 P3 via I2C
  bool wakePressed  = (digitalRead(BUTTON_WAKE_PIN) == LOW);
  uint8_t _pcfByte  = _readPCF8574Buttons();
  bool startPressed = !(_pcfByte & PCF8574_BTN_START_MASK);  // P3 LOW = pressed
  static uint32_t lastUIPrintTime = millis() - 2001;

  bool flipPressed  = !(_pcfByte & PCF8574_BTN_FLIP_MASK);  // P1 LOW = pressed
  bool eventPressed = !(_pcfByte & PCF8574_BTN_EVENT_MASK); // P2 LOW = pressed

  // [PCF_DEBUG] Print only when a button state changes
  static uint8_t _lastPcfByte = 0xFF;
  static bool    _lastWake = false;
  if (_pcfByte != _lastPcfByte || wakePressed != _lastWake) {
    if (startPressed || flipPressed || eventPressed || wakePressed) {
       Serial.printf("[PCF_DBG] Raw=0x%02X | P1(flip)=%d | P2(event)=%d | P3(start)=%d | wake=%d | State=%d\n",
                  _pcfByte,
                  (_pcfByte >> 1) & 1,
                  (_pcfByte >> 2) & 1,
                  (_pcfByte >> 3) & 1,
                  (int)wakePressed,
                  (int)currentState);
    }
    _lastPcfByte = _pcfByte;
    _lastWake = wakePressed;
  }

  // --- 0. CHARGING LOGIC ---
  static bool wasCharging = false;
  bool isChargingNow = Display_Handler::isCharging();
  int chargingMinsToFull = 0;
  
  if (isChargingNow) {
      if (!wasCharging) {
          Serial.println("[System] Charger connected -> Entering Standby");
          if (!measuring) {
              currentState = WAIT_FOR_START;
              screenOn = false;
              Display_Handler::off();
          }
          wasCharging = true;
      }
      
      // Calculate estimated minutes to full charge
      uint8_t curB = Display_Handler::getBatteryPercent();
      if (curB < 100) {
          float remainingMah = BATTERY_CAPACITY_MAH * (1.0f - (curB / 100.0f));
          chargingMinsToFull = (int)((remainingMah / CHARGE_CURRENT_MA) * 60.0f);
      }
  } else if (wasCharging) {
      Serial.println("[System] Charger removed -> Auto-waking");
      currentState = IDLE;
      screenOn = true;
      Display_Handler::on();
      lastActivityTime = millis();
      wasCharging = false;
  }

  // --- 1. SYSTEM STATE GUARDS (Top Priority) ---
  if (currentState == WAIT_FOR_START) {
    if (millis() - lastUIPrintTime > 2000) {
      Serial.println("[OLED] --- HOLD BUTTON 18 TO START ---");
      lastUIPrintTime = millis();
    }
    
    if (millis() - standbyStartTime > 60000) {
      Serial.println("[System] No activity in standby - Powering OFF");
      Display_Handler::on();
      Display_Handler::showSleep();
      delay(1500);
      Display_Handler::off();
      ABPM_Manager::goToSleep(false, false);
    }
    
    static uint32_t unlockStartTime = 0;
    static bool standbyWasPressed = false;  // Tracks release-edge for short-tap detection
     if (wakePressed) {
        if (unlockStartTime == 0) unlockStartTime = millis();
        standbyWasPressed = true;
        
        // Threshold: 3 seconds for a secure start
        if (millis() - unlockStartTime > 3000) {
          Serial.println("[System] STARTING: Running POST...");
          
          // 1. Turn on display to show POST status
          Display_Handler::on();
          Display_Handler::showBoot(); // Shows "Checking..." style logo
          delay(500);

          // 2. Run Hardware Self-Test
          // If this fails, it will halt and show an error on the screen
          POST_Manager::run(true); 

          // 3. Transition to normal operation
          Serial.println("[OLED] *** SYSTEM READY ***");
          currentState = IDLE;
          screenOn = true;
          systemActive = true;
          lastActivityTime = millis();
          unlockStartTime = 0;
          standbyWasPressed = false;
          
          // Confirmation beeps
          ledcWrite(BUZZER_PIN, BUZZER_DUTY); delay(100); ledcWrite(BUZZER_PIN, 0); delay(50);
          ledcWrite(BUZZER_PIN, BUZZER_DUTY); delay(100); ledcWrite(BUZZER_PIN, 0);

          // Start advertising
          BLE_Handler::getInstance().startAdvertising();
          bleAdvertisingStartTime = millis();
          
          Display_Handler::showIdle(Display_Handler::getBatteryPercent(),
                                    bleHandler.isConnected(), true, abpmMode, _readPressure(),
                                    (uint8_t)systolicBP, (uint8_t)diastolicBP, (uint8_t)heartRate);
          
          io18NeedsFreshPress = true;
          io18WasPressed = false;
          io18PressStartTime = 0;
          menuTriggeredThisPress = false;
          io18UnlockCooldownTime = millis();
        } else if (millis() - unlockStartTime > 500) {
         // Show hold progress or charging state on display while waiting for threshold
         if (Display_Handler::isCharging()) {
             Display_Handler::showChargingIdle(Display_Handler::getBatteryPercent(), chargingMinsToFull);
         } else {
             Display_Handler::showWaitForStart(Display_Handler::getBatteryPercent(), false);
         }
       }
    } else {
       // Release edge: detect short tap (< 1500ms) = screen on/off toggle in lock screen
       if (standbyWasPressed && unlockStartTime > 0 && (millis() - unlockStartTime) < 1500) {
         screenOn = !screenOn;
         Serial.printf("[UI] Standby short-press: screen %s\n", screenOn ? "ON" : "OFF");
         if (screenOn) {
           Display_Handler::on();
           if (Display_Handler::isCharging()) {
                 Display_Handler::showChargingIdle(Display_Handler::getBatteryPercent(), chargingMinsToFull);
           } else {
               Display_Handler::showWaitForStart(Display_Handler::getBatteryPercent(), false);
           }
         } else {
           Display_Handler::off();
         }
       }
       unlockStartTime = 0;
       standbyWasPressed = false;
    }
    
    // 1-minute auto-off ONLY during charging to protect screen
    if (screenOn && Display_Handler::isCharging() && (millis() - lastActivityTime > 60000)) {
        Serial.println("[Charging] Auto-dimming screen (60s timeout)");
        screenOn = false;
        Display_Handler::off();
    }

    return; // Block everything else in standby
  }

  // --- 2. ACTIVE SYSTEM BLOCK (Only reached if Unlocked) ---
  
  // A. Connection Check
  if (!bleHandler.isConnected() && measuring && measurementStartedViaBLE) {
    measuring = false;
    inflating = false;
    digitalWrite(MOTOR_PIN, LOW);
    ledcWrite(VALVE_PIN, 0);
    currentBLEState = DISCONNECTED;
    Serial.println("[Main] BLE measurement aborted - client disconnected");
  }

  // B. P2 (Event Marker) Logic
  static uint32_t lastEventTime = 0;
  if (eventPressed) {
    if (millis() - lastEventTime > 2000) { // 2 second debounce/cooldown
      lastEventTime = millis();
      Serial.println("[UI] P2 (Event) Pressed. Logging marker...");
      
      // 1. Audio: Fast double-chirp
      Buzzer_Handler::eventBeep();
      
      // 2. Storage: Save an all-zero reading as the event marker
      time_t now; time(&now);
      measurementCounter++;
      Storage::saveReading(0, 0, 0, measurementCounter, (uint32_t)now);
      
      // 3. UI: Show "Event Marked" for 3 seconds
      bool screenWasOff = !screenOn;
      screenOn = true;
      Display_Handler::on();
      Display_Handler::showEventMarked();
      
      // Block for 3 seconds (keeping buzzer ticking if needed)
      uint32_t waitStart = millis();
      while(millis() - waitStart < 3000) {
          Buzzer_Handler::tick();
          delay(10);
      }
      
      // Restore screen state
      if (screenWasOff) {
          screenOn = false;
          Display_Handler::off();
          ABPM_Manager::goToSleep(bleHandler.isConnected(), measuring);
      } else {
          // Signal main loop to redraw normal screen
          extern bool g_resultsNeedRedraw;
          g_resultsNeedRedraw = true;
      }
    }
  }

  // C. P3 (Start / Stop) Logic
  static uint32_t lastIO19ReleaseTime = 0;
  if (startPressed) {
    if (millis() - lastIO19ReleaseTime < 100) {
      // Debounce: Ignore bouncing contacts immediately following a release
    } else {
      lastActivityTime = millis();
      if (io19NeedsFreshPress) {
        // Ignore: still holding from a previous STOP — wait for full release first
      } else if (measuring) {
        // Emergency STOP: P3 held > 500ms during active measurement
        // NOTE: This runs OUTSIDE isIO19Locked so the hold-to-stop always works,
        // even though isIO19Locked=true was set at the moment measurement started.
        if (io19PressStartTime == 0) {
          io19PressStartTime = millis();
        } else if (millis() - io19PressStartTime > 500) {
          Serial.println("[Hardware] P3 Silent Emergency STOP Triggered!");
          stopMeasurement();
          io19NeedsFreshPress = true; // Require full release before next start
          isIO19Locked = true;
        }
      } else if (!isIO19Locked) {
        // If waking from sleep, wake display hardware first, then clear FINISHED state
        if (!screenOn) {
          screenOn = true;
          Display_Handler::on();   // BUG FIX: must wake hardware, not just set flag
          Serial.println("[UI] P3 Waking screen & hardware ON");
          bleHandler.startAdvertising();
          bleAdvertisingStartTime = millis();
          if (currentState == FINISHED) {
             currentState = IDLE;
          }
        }

        if (currentState == FINISHED) {
          // Single P3 press from results → start next measurement directly
          Serial.println("[UI] P3 from FINISHED → starting new measurement");
          startMeasurement();
          measurementStartedViaBLE = false;
          isIO19Locked = true;
        } else {
          // IDLE: start a fresh measurement
          startMeasurement();
          measurementStartedViaBLE = false;
          isIO19Locked = true;
        }
      }
    }
  } else {
    lastIO19ReleaseTime = millis();
    if (io19NeedsFreshPress) {
      io19NeedsFreshPress = false; // Button released — ready for fresh press
      Serial.println("[UI] IO19 ready (post-stop release registered)");
    }
    isIO19Locked = false;
    io19PressStartTime = 0;
  }
  
  // C. IO18 (Toggle / Menu) Logic
  if (wakePressed) {
    if (io18NeedsFreshPress) {
      // Still inherited from unlock press - wait for button to fully release first
    } else if (!io18WasPressed) {
      io18WasPressed = true;
      io18PressStartTime = millis();
      menuTriggeredThisPress = false;
    } else if (!menuTriggeredThisPress) {
      uint32_t duration = millis() - io18PressStartTime;
      if (duration > 3000) {
        Serial.println("[System] Long Press IO18: POWERING OFF...");
        Serial.println("[System] Status: Disabling ABPM & Clearing Schedule...");
        
        // FULL STOP: Disable ABPM Mode before shutdown
        ABPM_Manager::setConfig(nullptr, 0);
        
        Serial.println("[System] ---> RELEASE BUTTON TO SHUT DOWN <---");
        while(digitalRead(BUTTON_WAKE_PIN) == LOW) delay(10); // Wait for GPIO18 release
        delay(500); 
        
        currentState = WAIT_FOR_START;
        systemActive = false;
        standbyStartTime = millis();
        io18WasPressed = false; 
        io18PressStartTime = 0;
        menuTriggeredThisPress = true; // Mark as handled

        Display_Handler::showPowerOff();
        Buzzer_Handler::alarmBeep(); // 3-beep sequence
        
        uint32_t waitStart = millis();
        while (millis() - waitStart < 1500) {
            Buzzer_Handler::tick();
            delay(10);
        }
        Display_Handler::off();
        
        ABPM_Manager::goToSleep(false, false);
      }
    }
    lastActivityTime = millis();
  } else {
    if (io18NeedsFreshPress) {
      // Button fully released after unlock - ready for next fresh press
      io18NeedsFreshPress = false;
      Serial.println("[UI] IO18 ready (post-unlock release registered)");
    } else if (io18WasPressed) {
      uint32_t duration = millis() - io18PressStartTime;
      static uint32_t lastToggleTime = 0;
      bool debouncePassed = (millis() - lastToggleTime > 300); // 300ms debounce
      if (duration < 500 && !menuTriggeredThisPress && debouncePassed) {
        // Allow screen to turn ON during a measurement (to peek at background ABPM),
        // but block turning it OFF during active measurement to prevent accidental vibration hits.
        if (!measuring || !screenOn) {
          lastToggleTime = millis();
          screenOn = !screenOn;
          Serial.printf("[UI] Screen Manual Toggle: %s\n", screenOn ? "ON" : "OFF");
          if (screenOn) {
            Display_Handler::on();
            
            // Debug print: Show current time and next scheduled reading when screen is woken
            if (abpmMode) {
                uint32_t nextT = ABPM_Manager::getNextScheduledTime();
                if (nextT > 0) {
                    time_t now;
                    time(&now);
                    struct tm *cur_tm = localtime(&now);
                    char curBuf[16];
                    snprintf(curBuf, sizeof(curBuf), "%02d:%02d:%02d", cur_tm->tm_hour, cur_tm->tm_min, cur_tm->tm_sec);
                    
                    time_t nt = (time_t)nextT;
                    struct tm *nt_tm = localtime(&nt);
                    char nextBuf[16];
                    snprintf(nextBuf, sizeof(nextBuf), "%02d:%02d:%02d", nt_tm->tm_hour, nt_tm->tm_min, nt_tm->tm_sec);
                    
                    Serial.printf("[ABPM] Screen Woke | Current Time: %s | Next Scheduled: %s\n", curBuf, nextBuf);
                }
            }

            // BUG FIX: Force redraw after screen-on so display shows content, not blank
            // If results are waiting, redraw them; if idle, idle loop handles it automatically
            if (currentState == FINISHED) {
              extern bool g_resultsNeedRedraw;
              g_resultsNeedRedraw = true;
              Serial.println("[UI] Screen ON in FINISHED state — forcing results redraw");
            }
            if (!bleHandler.isConnected()) {
              bleHandler.startAdvertising();
              bleAdvertisingStartTime = millis();
            }
          } else {
            Display_Handler::off();
            bleHandler.stopAdvertising();
            bleAdvertisingStartTime = 0;
            // Go to sleep immediately to save power and set ABPM wakeup timer
            ABPM_Manager::goToSleep(bleHandler.isConnected(), measuring);
          }
        } else {
          Serial.println("[UI] Screen toggle blocked - measuring active");
        }
      }
    }
    io18WasPressed = false;
    io18PressStartTime = 0;
  }

  // E. P1 (PCF8574) Short-Press → Screen Flip 180°
  // Flips the display orientation instantly.
  // Useful when the OLED is mounted vertically and needs to be read from the opposite side.
  {
    static uint32_t p1PressStart   = 0;
    static bool     p1FlipFired    = false;
    static bool     g_screenFlipped = false;
    if (flipPressed && screenOn) {
      if (p1PressStart == 0) p1PressStart = millis();
      if (!p1FlipFired && (millis() - p1PressStart > 100)) { // 100ms = short press / debounced
        g_screenFlipped = !g_screenFlipped;
        Display_Handler::flipScreen(g_screenFlipped);
        Buzzer_Handler::beep(80); // Short single beep to confirm flip
        p1FlipFired = true;
        Serial.printf("[UI] P1 press: screen %s\n", g_screenFlipped ? "FLIPPED" : "NORMAL");
        // Force immediate redraw so the rotated content appears right away
        if (currentState == FINISHED) {
          extern bool g_resultsNeedRedraw;
          g_resultsNeedRedraw = true;
        }
        // IDLE redraws automatically within 500ms via the idle update loop
      }
    } else {
      p1PressStart = 0;
      p1FlipFired  = false;
    }
  }

  // D. Screen Auto-Timeout Logic
  if (screenOn && (millis() - lastActivityTime > SCREEN_TIMEOUT_MS)) {
    if (!measuring) {
      Serial.println("[System] Screen Timeout - Entering Sleep Mode");
      screenOn = false;
      Display_Handler::showSleep();
      delay(1500); // Allow user to see the sleep message
      Display_Handler::off();
      ABPM_Manager::goToSleep(bleHandler.isConnected(), measuring);
    }
  }

  // E. BLE Advertising Timeout (1 minute, independent of screen)
  if (bleAdvertisingStartTime > 0 && !bleHandler.isConnected()) {
    if (millis() - bleAdvertisingStartTime > BLE_ADV_TIMEOUT_MS) {
      Serial.println("[BLE] Advertising timeout (60s, no client) - Stopping discovery");
      bleHandler.stopAdvertising();
      bleAdvertisingStartTime = 0; // Reset so it can be started again
    }
  } else if (bleHandler.isConnected()) {
    bleAdvertisingStartTime = 0; // Client connected, no timeout needed
  }


  // Handle Pre-Measurement Alarm (30s before start)
  if (ABPM_Manager::isPreAlarmTriggered()) {
      Serial.println("\n****************************************");
      Serial.println("* ABPM PRE-ALARM TRIGGERED (30s to go) *");
      Serial.println("****************************************\n");
      
      // If screen is already on, show the countdown
      if (screenOn) {
          Display_Handler::showAbpmAlarm(30);
          lastActivityTime = millis(); // Refresh timeout
      }

      // Beep only if the buzzer is enabled for the current ABPM window
      if (ABPM_Manager::isBuzzerActiveNow()) {
          Serial.println("[ABPM] Buzzer ENABLED - Playing Alarm...");
          Buzzer_Handler::alarmBeep();  // Triple-beep pattern
          
          // Wait for buzzer to finish to guarantee it's heard in background
          uint32_t bzStart = millis();
          while (Buzzer_Handler::isBusy() && millis() - bzStart < 2000) {
              Buzzer_Handler::tick();
              delay(10);
          }
      } else {
          Serial.println("[ABPM] Buzzer DISABLED for this slot (Silent Background Mode)");
      }
      
      BLE_Handler::getInstance().notifyStatus(IDLE);
      ABPM_Manager::clearPreAlarm();
  }

  // Handle Autonomous ABPM measurement
  // Guard: skip if post-stop button not yet released (prevents same-cycle re-trigger)
  if (ABPM_Manager::isPendingTrigger() && !measuring && !io19NeedsFreshPress) {
       Serial.println("[ABPM] Triggering scheduled measurement...");
       // Removed forced screen-on: allows background measurement if screen was already OFF
       if (screenOn) {
           lastActivityTime = millis(); // Refresh timeout only if screen is already being watched
       }
       measurementStartedViaBLE = false;
       startMeasurement();
       ABPM_Manager::clearPendingTrigger();
       ABPM_Manager::updateSchedule(); // Explicitly advance the schedule ONLY for autonomous triggers
  }

  // 4. BLE / System Logic
  bleHandler.checkAuthTimeout();
  
  // 5. Idle Display Update
  if (screenOn && !measuring) {
    if (currentState == IDLE) {
      static uint32_t lastIdleUpdateTime = 0;
      if (millis() - lastIdleUpdateTime > 500) {
        lastIdleUpdateTime = millis();
        Display_Handler::showIdle(Display_Handler::getBatteryPercent(), 
                                  bleHandler.isConnected(), (bleAdvertisingStartTime > 0), 
                                  abpmMode, _readPressure(), (uint8_t)systolicBP, 
                                  (uint8_t)diastolicBP, (uint8_t)heartRate);
      }
    } else if (currentState == FINISHED) {
      // Only redraw when a new measurement has completed (g_resultsNeedRedraw flag)
      if (g_resultsNeedRedraw) {
        Serial.printf("[Result] SBP=%d DBP=%d HR=%d\n", (int)systolicBP, (int)diastolicBP, (int)heartRate);
        Display_Handler::showResults((uint8_t)systolicBP, (uint8_t)diastolicBP, (uint8_t)heartRate,
                                     Display_Handler::getBatteryPercent(), abpmMode,
                                     bleHandler.isConnected(), (bleAdvertisingStartTime > 0));

        g_resultsNeedRedraw = false;
      }
    }
  }

  // Buzzer non-blocking tick — must run every loop
  Buzzer_Handler::tick();

  if (bleHandler.getLastCommand() != 0) {
    lastActivityTime = millis(); // Any remote command also wakes/refreshes
    uint8_t cmd = (uint8_t)bleHandler.getLastCommand();
    
    // Set flag if it's a START command
    if (cmd == BLE_Handler::CMD_START) {
      measurementStartedViaBLE = true;
    }
    
    Serial.print("[Main] loop() - processing BLE command: 0x");
    Serial.println(cmd, HEX);
    bleHandler.clearLastCommand(); // Clear immediately
    handleBLECommand(cmd);
  }

  // Serial fallback for debugging
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    // Serial commands for manual control
    
    if (cmd == "1" && !measuring && bleHandler.isConnected()) {
      startMeasurement();
      Serial.println("[Serial] Measurement started");
    }
    else if (cmd == "stop" && measuring) {
      stopMeasurement();
      Serial.println("[Serial] Measurement stopped");
    }
    else if (cmd == "reset") {
      Serial.println("[Serial] Resetting device...");
      delay(200);
      ESP.restart();
    }
  }

  // Main measurement state machine
  if (measuring) {
    lastActivityTime = millis(); // Prevent screen and device from sleeping during measurement
    
    // New: Handle external STOP request from BLE ABPM command
    if (ABPM_Manager::abpmKillRequest) {
      Serial.println("[Main] ABPM Kill Request received - Stopping mid-reading!");
      stopMeasurement();
      ABPM_Manager::abpmKillRequest = false;
      return; 
    }

    if (inflating) {
      static uint32_t lastInflPhaseUI = 0;
      static bool inflTextToggle = true;
      if (millis() - lastInflPhaseUI > 1000) { // toggle every 1 sec
        lastInflPhaseUI = millis();
        if (screenOn) Display_Handler::showInflating(inflTextToggle, Display_Handler::getBatteryPercent(), abpmMode, bleHandler.isConnected(), (bleAdvertisingStartTime > 0));
        inflTextToggle = !inflTextToggle;
      }
      
      simpleInflation();
      if (millis() - inflationStartTime > 30000) {
        inflating = false; // Fix: Stop inflation phase on timeout
        ledcWrite(MOTOR_PIN, 0); // CRITICAL: Stop motor
        currentState = WAITING;
        stateStartTime = millis();
        Serial.println("Inflation timeout - transitioning to waiting state");
      }
    }
    else {
      runLinearDeflationControl(millis());
    }
  }
}

float _readPressure(void) {
  const uint8_t ADDR = 0x6D; // WF100D Address
  const uint8_t START_REG = 0x30;
  const uint8_t DATA_REG = 0x06;
  
  // 1. Start Measurement
  Wire.beginTransmission(ADDR); 
  Wire.write(START_REG);        
  Wire.write(0x0A);             
  if (Wire.endTransmission() != 0) {
      Serial.println("[Sensor] I2C Write Failed - WF100D not responding at 0x6D");
      return 0.0f;
  }
  delay(DATA_CONVERSION_DELAY); 
  
  // 2. Read Data
  Wire.beginTransmission(ADDR);
  Wire.write(DATA_REG);
  Wire.endTransmission(false); // Repeated start
  
  Wire.requestFrom(ADDR, (uint8_t)5);
  if (Wire.available() >= 3) {
      uint8_t b1 = Wire.read();
      uint8_t b2 = Wire.read();
      uint8_t b3 = Wire.read();
      Wire.read(); Wire.read(); // Discard temperature bytes
      
      uint32_t reading = ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | (uint32_t)b3;
      float fDat;
      
      if (reading >= 8388608) // 2^23 sign bit
          fDat = ((int32_t)(reading - 16777216)) / 8388608.0;
      else
          fDat = reading / 8388608.0;
      
      // Convert to kPa then to mmHg (1 kPa = 7.50062 mmHg)
      float _pressure = (fDat * 50.0 + 5.0) * 7.50062; 
      
      // Apply calibration
      _pressure = _pressure - ((pressureCalibM * _pressure) + pressureCalibC);
      
      return _pressure;
  } else {
      Serial.println("[Sensor] I2C Read Failed (Timeout)");
      return 0.0f;
  }
}

void simpleInflation() {
  if (millis() - lastTime_inflation > ((1000 / BP_FS))) {
    lastTime_inflation = millis();
    
    float pressure = _readPressure();
    Serial.println(pressure);
    
    // Notify live pressure during inflation
    BLE_Handler::getInstance().notifyLivePressure((uint16_t)pressure);
    
    if (pressure <= 0) {
      lastTime_inflation = 0;
      return;
    }

    // Adjust PWM based on pressure
    if (pressure >= PRESSURE_THRESHOLD) {
      int newPWM = map(constrain(pressure, PRESSURE_THRESHOLD, 300.0), PRESSURE_THRESHOLD, 300.0, PWM_MAX, 255);
      if (newPWM != currentPWM) {
        currentPWM = newPWM;
        ledcWrite(MOTOR_PIN, currentPWM);
      }
    } else {
      int newPWM = map(constrain(pressure, 0.0, PRESSURE_THRESHOLD), 0.0, PRESSURE_THRESHOLD, 255, PWM_MAX);
      if (newPWM != currentPWM) {
        currentPWM = newPWM;
        ledcWrite(MOTOR_PIN, currentPWM);
      }
    }

    // [gkrish] Dynamic inflation controller
    if (DI_OnInflationSample(pressure)) {
      inflating = false;
      fill_index = 0;
      inflationStopTime = millis();
      inflationStopPressure = pressure;
      ledcWrite(MOTOR_PIN, 0);
      ledcWrite(VALVE_PIN, 1023);
      Serial.println("Inflation complete (dynamic), starting measurement...");

      // Transition to WAITING state for 2-second hold before deflation
      currentState = WAITING;
      stateStartTime = millis();

      // [gkrish] Set valve open threshold
      if (DI_HasMeanAtInflation()) {
        float meanInfl = DI_GetMeanAtInflation();
        // meanAtInflation = meanInfl;
        float thresh   = meanInfl - 110.0f;
        if (thresh < MIN_PRESSURE) {
          thresh = MIN_PRESSURE;
        }
        valveOpenThreshold = thresh;
        Serial.printf("[VALVE] mean_at_inflation=%.2f, threshold=%.2f\n",
                      meanInfl, valveOpenThreshold);
      } else {
        valveOpenThreshold = 0.0f;
      }
    }
    // Fallback absolute safety clamp
    else if (pressure >= MAX_PRESSURE) {
      inflating = false;
      fill_index = 0;
      inflationStopTime = millis();
      inflationStopPressure = pressure;
      ledcWrite(MOTOR_PIN, 0);
      ledcWrite(VALVE_PIN, 1023);
      Serial.println("Inflation complete (MAX_PRESSURE), starting measurement...");

      currentState = WAITING;
      stateStartTime = millis();
    }
  }
}

void runLinearDeflationControl(unsigned long currentTime) {
  static uint32_t lastSampleTime = 0;
  static float currentPressure = 0.0;
  static float derivative = 0.0; // Persist derivative between samples
  static float currentPressureRate = 0.0;

  switch (currentState) {
    case IDLE:
      ledcWrite(VALVE_PIN, 0); // Valve open
      break;

    case INFLATING:
      // Handled in simpleInflation() function
      break;

    case WAITING:
      // Hold for 0.5 seconds, Keep Valve Closed (PWM 1023 = closed)
      ledcWrite(MOTOR_PIN, 0); // Safety: ensure motor is off
      if (currentTime - stateStartTime >= 500) {
        currentState = DEFLATING;
        currentBLEState = MEASURING;
        stateStartTime = currentTime;
        deflationStartPressure = inflationStopPressure;
        currentPressure = inflationStopPressure;
        
        // Reset PID variables
        integralError = 0.0;
        previousError = 0.0; 
        derivative = 0.0;
        currentValvePWM = 0; 
        lastSampleTime = 0; // Force immediate sample
        currentPressureRate = DEFLATION_RATE;
        Serial.println("Starting linear deflation...");
        
        // Notify STATUS change to MEASURING
        BLE_Handler::getInstance().notifyStatus(MEASURING);
        // Show Deflating UI on Screen
        Display_Handler::showMeasuring(false, Display_Handler::getBatteryPercent(), abpmMode, BLE_Handler::getInstance().isConnected(), (bleAdvertisingStartTime > 0)); 
      }
      break;

    case DEFLATING:
      {
        static uint32_t lastDeflPhaseUI = 0;
        static bool deflTextToggle = true;
        if (millis() - lastDeflPhaseUI > 1000) { // toggle every 1 sec
           lastDeflPhaseUI = millis();
           if (screenOn) Display_Handler::showMeasuring(deflTextToggle, Display_Handler::getBatteryPercent(), abpmMode, BLE_Handler::getInstance().isConnected(), (bleAdvertisingStartTime > 0));
           deflTextToggle = !deflTextToggle;
        }

        float dt_seconds = (currentTime - stateStartTime) / 1000.0;
        if(dt_seconds <= 1) return;
        // float targetPressure = deflationStartPressure - (DEFLATION_RATE * dt_seconds);
        // float targetPressure =  DEFLATION_RATE;

        currentPressure = _readPressure(); // This blocks for ~5ms
        
        // 1. Sampling & PID State Update (50Hz Rate Limited)
        if (millis() - lastSampleTime > ((1000 / BP_FS) - (DATA_CONVERSION_DELAY + 2))) {
          lastSampleTime = millis();
          
          if (fill_index < MAX_DATA_POINTS) {
            data[fill_index] = currentPressure;
            fill_index++;
          }
          
          if (fill_index >= MAX_DATA_POINTS) {
            bool shouldShift = checkLargePulseAndShift(data, fill_index, filtered_data, x_fft_uenv, x_f_lenv, peaks, valleys, main_peaks, currentPressure);
            if (shouldShift) {
                // Serial.println("Large pulse detected... shifting buffer...");
                // clear first 200 samples and shift the data (200 samples = 4 seconds)
                for (int j = 0; j < fill_index-200; j++) {
                  data[j] = data[j+200];
                }
                fill_index -= 200;
                valveOpenThreshold -= 15; 
                if (valveOpenThreshold < MIN_PRESSURE) {
                  valveOpenThreshold = MIN_PRESSURE;
                }
            } else {
                Serial.println("Sufficient valid data collected, ready to calculate BP...");
                valveOpenThreshold = currentPressure + 10.0; // Force deflation complete condition
                // Set fill_index to MAX_DATA_POINTS - 1 to be absolutely safe
                fill_index = MAX_DATA_POINTS;
            }
          }
          Serial.println(currentPressure);
          
          // Send LIVE_BP notification every 500ms (only if authorized)
          BLE_Handler::getInstance().notifyLivePressure((uint16_t)currentPressure);
          
          // Check if deflation is complete
          // currentPressure <= MIN_PRESSURE is ignored if deflation hasn't reached near threshold
          if ((currentPressure <= MIN_PRESSURE) || (currentPressure <= valveOpenThreshold)) {
            measuring = false;
            currentState = FINISHED; // Go to results state
            currentBLEState = IDLE;
            valveOpenTime = millis();
            ledcWrite(VALVE_PIN, 0);  // Open valve fully
            Serial.println("Calculating blood pressure...");
            calculateBloodPressure(data, fill_index,
                                   (float*)&systolicBP, (float*)&diastolicBP, (float*)&heartRate, (int*)&bpErrorCode);
            Serial.println("Measurement complete");
            Serial.println("Dynamic inflation data:");
            Serial.println("  Inflation stop pressure: " + String(inflationStopPressure));
            Serial.println("  Mean at inflation: " + String(s_meanAtInflation));
            Serial.println("  Inflation Time: " + String((inflationStopTime - inflationStartTime)/1000));
            Serial.println("  Total Time: " + String(((valveOpenTime - inflationStopTime)/1000)+ ((inflationStopTime - inflationStartTime)/1000)));
            
            // Increment measurement counter (1, 2, 3...)
            measurementCounter++;
            
            // Save to Flash (limit 500 records) with Timestamp
            time_t now;
            time(&now);
            // Log the result
            Storage::saveReading((uint8_t)systolicBP, (uint8_t)diastolicBP, (uint8_t)heartRate, measurementCounter, (uint32_t)now);
            
            // ABPM Logic: If we are in ABPM mode and NOT connected to a phone,
            // decide whether to show results or go back to sleep immediately.
            if (abpmMode && !BLE_Handler::getInstance().isConnected()) {
               Serial.printf("[ABPM] Result: SBP=%d DBP=%d HR=%d\n", (int)systolicBP, (int)diastolicBP, (int)heartRate);
               
               if (screenOn) {
                   Serial.println("[ABPM] Screen is ON: Showing results for 5s...");
                   Display_Handler::showResults((uint8_t)systolicBP, (uint8_t)diastolicBP, (uint8_t)heartRate,
                                                Display_Handler::getBatteryPercent(), abpmMode,
                                                false, false);
                   uint32_t sleepWait = millis();
                   while (millis() - sleepWait < 5000) { Buzzer_Handler::tick(); delay(10); }
               } else {
                   Serial.println("[ABPM] Screen is OFF: Background measurement complete. Sleeping now.");
               }
               
               ABPM_Manager::goToSleep(false, false);
            }
            
            // Send LIVE RESULTS notification (only if authorized)
            BLE_Handler::getInstance().notifyResults((uint8_t)systolicBP, (uint8_t)diastolicBP, (uint8_t)heartRate, (uint8_t)measurementCounter);
            
            // Notify STATUS change to IDLE
            BLE_Handler::getInstance().notifyStatus(IDLE);
            return;
          }
          
          // Update PID Integral and Derivative terms ONLY on new sample
          float error_sample = currentPressureRate - DEFLATION_RATE;
          // Serial.print("e:"); Serial.println(error_sample);
          // Integral
          integralError += error_sample * (1.0 / BP_FS); 
          
          // Derivative
          derivative = (error_sample - previousError) * BP_FS; 
          previousError = error_sample;
          
          // Anti-windup
          if (integralError > 1000) integralError = 1000;
          if (integralError < -1000) integralError = -1000;
        }

        static float lastRatePressure = 3;
        static uint32_t lastRateTime = 0;
        if(millis() - lastRateTime > 200)
        {
          float elapsedSec = (millis() - lastRateTime) / 1000.0f;
          currentPressureRate = (lastRatePressure-currentPressure ) / elapsedSec;
          lastRateTime = millis();
          lastRatePressure = currentPressure;
        }
        else
        {
          return;
        }

        // 2. Continuous Actuation (Runs every loop)
        // Calculate P-term using continuous target vs stepped pressure
        float current_error = currentPressureRate - DEFLATION_RATE;
        lastError = current_error;
        // Serial.print("Drop Rate: "); Serial.println(currentPressureRate);

        float openingAmount = (Kp * current_error) + (Ki * integralError) + (Kd * derivative); 
        
        // Constrain Opening Amount
        int valveOpening = (int)openingAmount;
        
        // Map Opening to PWM for Normally Open Valve
        // Range: 950 (Closed) down to 0 (Open)
        const int PWM_MIN = 409;
        const int PWM_MAX = 1023;
        
        static int finalPWM = 1023;
        finalPWM = finalPWM + valveOpening;
        
        // Clamp PWM
        if (finalPWM > PWM_MAX) finalPWM = PWM_MAX; 
        if (finalPWM < PWM_MIN) finalPWM = PWM_MIN;       
        // Serial.print("f:"); Serial.println(finalPWM);
        ledcWrite(VALVE_PIN, finalPWM);
        
      }
      break;
      
    case FINISHED:
      break;
  }
}

// Calibration logic moved to Calibration_Manager.cpp