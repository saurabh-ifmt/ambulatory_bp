# User Guide: BT2501 BP Track Device

This guide explains how to operate your Blood Pressure device once the firmware is flashed.

## 1. Physical Button Map

| Button | Label | Location | Function |
| :--- | :--- | :--- | :--- |
| **IO18** | **Wake/Power** | Main Case | Power ON/OFF, Screen Toggle, Charging Info. |
| **P1** | **Flip** | Expander | Rotates the screen 180° (for easier reading). |
| **P2** | **Event** | Expander | Marks a time-stamped "Event" in the log. |
| **P3** | **Start/Stop** | Expander | Starts or Stops a manual BP measurement. |

---

## 2. Basic Operations

### Power ON
*   **Action**: Hold **IO18** for **2 seconds**.
*   **Feedback**: You will hear a short beep and the screen will show the **IDLE** screen.
*   **BLE**: The device will start advertising for 60 seconds.

### Power OFF (Deep Shutdown)
*   **Action**: Hold **IO18** for **3 seconds**.
*   **Feedback**: The screen will show "POWER OFF", you will hear **3 beeps**, and the device will enter a deep shutdown mode. 
*   **Note**: This also cancels any active ABPM schedules.

### Manual Measurement
*   **Start**: Short-press **P3**. The pump will start.
*   **Stop**: Short-press **P3** again at any time.
*   **Emergency Stop**: Hold **P3** for 0.5s during a measurement to fully open the valve and stop immediately.

### Flip Display
*   **Action**: Short-press **P1**.
*   **Effect**: The OLED content rotates 180°. Use this if you are wearing the device on a different arm or in a different orientation.

---

## 3. Advanced Features

### Marking an Event
*   **Action**: Press **P2**.
*   **Feedback**: Two fast chirps and the screen displays **"Event Marked"**.
*   **Result**: A record is saved in the history with SBP/DBP/HR all set to `0`. This allows you to track specific times (like taking medication or feeling symptoms) in the mobile app.

### Charging Mode
*   **Detection**: Plug in the USB cable. The device enters **Standby** automatically.
*   **Check Battery**: While plugged in, short-press **IO18**.
    *   The screen will show the **Percentage** and estimated **Time to Full Charge**.
    *   The screen will automatically turn OFF after **60 seconds** to save the OLED.
*   **Unplugging**: When you remove the charger, the device wakes up to **IDLE** mode automatically.

---

## 4. ABPM (Scheduled) Mode

When you configure ABPM from the mobile app:
1.  **Preparation**: 30 seconds before a scheduled reading, the device will **beep 3 times** to warn you.
2.  **Background Operation**: If the screen was OFF, it stays OFF during the measurement (Silent Mode).
3.  **Viewing Progress**: If you want to see the pressure during an ABPM reading, just short-press **IO18** to wake the screen.
4.  **Automatic Sleep**: Once the reading is finished and saved, the device returns to light-sleep automatically.

---

## 5. Maintenance & Reset
If you need to clear all data or reset the device:
*   Connect the device to the App.
*   Authenticate using the password.
*   Send the **RESET (0x03)** command from the App.
*   The device will clear all memory and restart.
