# BLE Authentication Flow

The device implements a strict "Challenge/Response" security model to protect clinical data.

## 1. The Handshake Process
1.  **Connection**: The app connects to the `BP_Device`.
2.  **Lockdown**: All sensitive characteristics (History, Commands, ABPM) are blocked.
3.  **Timer Started**: The firmware starts a **30-second** safety timer.
4.  **Submission**:
    *   The app writes `c5` (ASCII) to UUID: `1BF0272E-A068-486A-9889-FDAB3CA2D837`.
    *   This characteristic is located within the **Clinical & Auth Service** (`...7398`).
5.  **Validation**:
    *   **Success**: Timer is cancelled. `Auth Status` notifies `0x01`. Device is unlocked.
    *   **Failure/Timeout**: Device calls `pServer->disconnect()` immediately.

## 2. Security Rules
*   Authentication must be performed **every time** the app connects.
*   The device will **Broadly Advertise** for 60 seconds only after **IO18 is held for 2 seconds** (Power On).
*   Once authorized, the device stays unlocked until the app disconnects, the user manually powers off (3s hold), or the 1-minute inactivity timeout occurs.
*   While in **Charging Mode**, advertising is disabled unless the charger is removed.
