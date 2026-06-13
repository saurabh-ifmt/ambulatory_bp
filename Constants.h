#ifndef CONSTANTS_H
#define CONSTANTS_H

// Pin definitions (ESP32-C3) — Final PCB rev
#define ANALOG_PIN 2       // Battery ADC input pin
#define MOTOR_PIN 3        // IO3  - Motor PWM (moved from IO19; IO19=USB D+, triggers on USB plug-in)
#define VALVE_PIN 18       // IO18 - Valve PWM
#define PRESSURE_SDA 0     // I2C SDA pin
#define PRESSURE_SCL 1     // I2C SCL pin
#define BUZZER_PIN       7   // IO7  - Active Buzzer driven via LEDC PWM
#define BATTERY_PIN      2   // IO2  - Battery Voltage (same as ANALOG_PIN)
#define VBUS_PIN         19  // IO19 - Charger/VBUS Detection (reserved for future use; IO19 now free)

// Direct GPIO buttons (replaces PCF8574 I2C expander)
#define BUTTON_WAKE_PIN  10  // IO10 - SW2: Wake / Power / Screen Toggle
#define BTN_START_PIN    4   // IO4  - SW3: Start / Stop Measurement
#define BTN_EVENT_PIN    5   // IO5  - SW4: Event Marker
#define BTN_FLIP_PIN     6   // IO6  - SW1: Screen orientation flip

// Battery Estimation Constants
#define BATTERY_CAPACITY_MAH  500.0f  // Adjust to your actual battery mAh
#define CHARGE_CURRENT_MA     250.0f  // Typical charging current in mA

// Buzzer LEDC PWM configuration (Modern v3.0+ API)
#define BUZZER_FREQ       2000  // 2kHz — clearly audible, works with active buzzer
#define BUZZER_RESOLUTION    8  // 8-bit resolution → duty range 0–255
#define BUZZER_DUTY        128  // 50% duty cycle — buzzer ON state


// Constants
#define BP_FS 50          // Sampling frequency in Hz
#define MAX_DATA_POINTS 3200         // Maximum number of data points (Restored to 3200)
#define PADLEN 80      // Padding length for filtering
#define BUFFER_SIZE MAX_DATA_POINTS  // Circular buffer size

#define MAX_PRESSURE 270.0 // Maximum cuff pressure in mmHg
#define MIN_PRESSURE 45.0  // Minimum pressure to stop data collection in mmHg
#define DATA_CONVERSION_DELAY 5

#define DEBUG_BUFF_LOG 1

// BLE Configuration - Custom 128-bit UUIDs
#define BT_UUID_CUSTOM_AUTH_VAL \
    BT_UUID_128_ENCODE(0x9e6eea7f, 0xd406, 0x430d, 0xb80b, 0xd5a7dfac7398)

#define BT_UUID_CUSTOM_AUTH \
    BT_UUID_DECLARE_128(BT_UUID_CUSTOM_AUTH_VAL)

#define BT_UUID_CUSTOM_AUTH_CHAR_VAL \
    BT_UUID_128_ENCODE(0x1bf0272e, 0xa068, 0x486a, 0x9889, 0xfdab3ca2d837)

#define BT_UUID_CUSTOM_AUTH_CHAR \
    BT_UUID_DECLARE_128(BT_UUID_CUSTOM_AUTH_CHAR_VAL)

// Custom UUIDs matching the working ble_thread.c implementation
#define SERVICE_UUID          "9E6EEA7F-D406-430D-B80B-D5A7DFAC7398"  // Custom Clinical Service
#define TIME_SERVICE_UUID     "9E6EEA7F-D406-430D-B80B-D5A7DFAC7390"  // Dedicated Time Service
#define SERVICE_CLINICAL_UUID  "9E6EEA7F-D406-430D-B80B-D5A7DFAC7398"  // Combined Clinical Data Service
#define SERVICE_AUTH_UUID      "9E6EEA7F-D406-430D-B80B-D5A7DFAC73A0"  // Dedicated Authentication Service
#define SERVICE_COMMAND_UUID   "9E6EEA7F-D406-430D-B80B-D5A7DFAC73B0"  // Dedicated Command/Control Service
#define SERVICE_ABPM_UUID      "9E6EEA7F-D406-430D-B80B-D5A7DFAC73C0"  // Dedicated ABPM Configuration Service
#define SERVICE_TIME_UUID      "9E6EEA7F-D406-430D-B80B-D5A7DFAC7390"  // Time Sync Service
#define CHAR_COMMAND_UUID      "9E6EEA7F-D406-430D-B80B-D5A7DFAC7397"  // COMMAND (write): 0x03=RESET, 0x01=START, 0x02=STOP
#define CHAR_STATUS_UUID       "9E6EEA7F-D406-430D-B80B-D5A7DFAC7396"  // STATUS (read/notify)
#define CHAR_LIVE_BP_UUID      "9E6EEA7F-D406-430D-B80B-D5A7DFAC7395"  // LIVE_BP (read/notify)
#define CHAR_RESULT_UUID       "9E6EEA7F-D406-430D-B80B-D5A7DFAC7394"  // RESULTS (read/notify)
#define CHAR_TIME_UUID         "9E6EEA7F-D406-430D-B80B-D5A7DFAC7391"  // TIME_SYNC (write/read)
#define CHAR_AUTH_UUID         "1BF0272E-A068-486A-9889-FDAB3CA2D837"  // AUTH (write)
#define CHAR_AUTH_STATUS_UUID  "9E6EEA7F-D406-430D-B80B-D5A7DFAC7393"  // AUTH_STATUS (read/notify)
#define CHAR_HISTORY_UUID      "9E6EEA7F-D406-430D-B80B-D5A7DFAC7392"  // HISTORY_DUMP (read/notify)
#define CHAR_ABPM_CONFIG_UUID  "9E6EEA7F-D406-430D-B80B-D5A7DFAC7399"  // ABPM_CONFIG (read/write)

// Authentication
#define AUTH_TIMEOUT_MS 30000  // 30 seconds to authenticate

// PWM Configuration (Modern v3.0+ API)
#define PWM_FREQUENCY 5000         // Motor PWM frequency (Hz)
#define PWM_RESOLUTION 8           // Motor PWM resolution (bits) - Hardware requirement
#define VALVE_PWM_FREQ 5000        // Valve PWM frequency (Hz)
#define VALVE_PWM_RES 10           // Valve PWM resolution (bits)

// Device state enum for state machine
enum DeviceState { 
  IDLE=0,          // Waiting for measurement command
  INFLATING=1,     // Cuff is inflating
  MEASURING=2,     // Recording pressure data
  WAITING=3,       // Waiting before deflation
  DEFLATING=4,     // Cuff is deflating
  FINISHED=5,      // Measurement complete
  ERROR=6,         // Error state
  DISCONNECTED=7,   // BLE disconnected
  WAIT_FOR_START=8  // Standby mode (Waiting for long press to unlock)
};

#define CURRENT_FIRMWARE_VERSION "1.0.0.BP-BLE"

#endif
