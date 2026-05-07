# Blood Pressure Device: System Flow

This document describes the high-level logic and state transitions of the firmware.

## 1. Operating Modes

### A. Manual Mode (Interactive)
*   **Trigger**: IO18 Button Press (Wake/Screen Toggle).
*   **State**: Screen ON, BLE Advertising.
*   **Control**: App commands via **Command Service** (`...73B0`); User can short-press **P3 (Start/Stop Button)** to start.
*   **Timeout**: If no action for 1 minute, device enters **Hibernation** (Light Sleep).

### B. ABPM Mode (Autonomous)
*   **Trigger**: Writing to `ABPM_CONFIG` (`...7399`).
*   **Configuration**: Multi-schedule support (up to 4 independent windows).
*   **Priority**: If two windows overlap, the latest one defined in the BLE packet takes priority.
*   **Behavior**: Device wakes from Timer -> Checks active schedule for Interval & Buzzer -> Measures silently -> Returns to Light Sleep.
*   **Buzzer**: 30-second pre-alarm window with a triple-beep pattern. The system stays awake to ensure the full audio pattern plays before hibernating.
*   **Background Mode**: Measurements occur completely silently with the screen OFF unless the user manually presses IO18 to watch the progress.

### C. Charging Mode
*   **Trigger**: GPIO 6 (VBUS) goes HIGH.
*   **Behavior**: Device automatically enters **Standby Mode** (Screen OFF, BLE advertising stopped) to focus on charging.
*   **Interaction**: Pressing **IO18** toggles the screen ON to show charging percentage and estimated **Time to Full Charge**.
*   **Safety**: If the screen is left ON while charging, it automatically turns OFF after **60 seconds** to protect the OLED.
*   **Resume**: Unplugging the charger automatically wakes the device and returns it to **Normal Mode**.

---

## 2. Power Management (Light Sleep)
The device uses ESP32 **Light Sleep** to maximize battery life while maintaining BLE connection if active.

*   **Wakeup Sources**:
    1.  **Timer**: For scheduled ABPM readings.
    2.  **GPIO 18**: For user interaction / Manual Toggle.
    3.  **GPIO 6 (VBUS)**: Charging detection.

## 3. Hardware Requirements

### A. Battery Monitoring (GPIO 2)
To measure battery percentage correctly, a **100kΩ / 100kΩ Voltage Divider** is required on GPIO 2. This drops the 4.2V battery voltage to a safe 2.1V for the ESP32 ADC.

### B. Charging Detection (GPIO 6)
The VBUS line (5V) must be stepped down to 3.3V (using a voltage divider or Zener diode) before connecting to GPIO 6. The firmware detects a HIGH signal as "Charging Active".

---

## 3. Hardware Buttons (PCF8574 I2C Expander)
*   **P1 (Flip Button)**: Short press to instantly flip the OLED screen 180 degrees.
*   **P2 (Event Marker)**: Press to log a special "Event" timestamp in the history log (saved as SBP=0, DBP=0, HR=0). Triggers a double-chirp and a 3-second visual confirmation.
*   **P3 (Start/Stop)**: Tap to start a measurement. Hold for 500ms during inflation to trigger an **Emergency Stop**.

---

## 4. Safety Protocols
*   **Hardware Stop**: Holding P3 for >500ms stops the motor and opens the valve instantly.
*   **Disconnected BLE**: If a measurement is started via the app and the app disconnects, the device **Aborts** immediately for safety.
*   **Screen Lockout**: Turning the screen OFF via IO18 is blocked during active measurement to prevent accidental vibration hits.