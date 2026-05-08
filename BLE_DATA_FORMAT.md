# BLE Data Format Documentation

## 1. Service Overview

| Service Name | UUID | Description |
| :--- | :--- | :--- |
| **Clinical & Auth** | `9E6EEA7F-D406-430D-B80B-D5A7DFAC7398` | Primary interface for control, data, and security. |
| **Time Sync** | `9E6EEA7F-D406-430D-B80B-D5A7DFAC7390` | Dedicated service for POSIX time synchronization. |

---

## 2. Clinical & Auth Service (`...7398`)

### A. Auth Characteristic (`...D837`) [Write]
Used to authorize the client. Features are locked until password is verified.
*   **Packet**: 2 Bytes (ASCII)
*   **Password**: `c5`
*   **Example Packet**: `0x63 0x35` (Hex)

### B. Auth Status (`...7393`) [Read/Notify]
*   `0x00`: Unauthorized (locked)
*   `0x01`: Authorized (unlocked)
*   **Example Packet**: `0x01` (Meaning: Device is unlocked)

### C. Command Characteristic (`...7397`) [Write]
*   `0x01`: Start Measurement
*   `0x02`: Stop/Emergency Stop
*   `0x03`: Factory Reset
*   `0x04`: Request History Sync
*   **Example Packet**: `0x01` (Meaning: Trigger pump start)

### D. Status Characteristic (`...7396`) [Read/Notify]
Current device state:
*   `0x00`: IDLE | `0x01`: INFLATING | `0x02`: MEASURING | `0x03`: WAITING | `0x04`: DEFLATING | `0x05`: FINISHED | `0x06`: ERROR | `0x08`: STANDBY
*   **Example Packet**: `0x02` (Meaning: Currently measuring pulses)

### E. ABPM Config (`...7399`) [Read/Write]
*   **Format**: `[Control] [StartHr] [EndHr] [Interval] [Buzzer]` (5 Bytes per slot)
    *   **Control**: `0x01` (Active), `0x00` (Disabled)
    *   **Interval**: Minutes between readings
    *   **Buzzer**: `0x01` (Bell icon on, 30s alarm), `0x00` (Silent)
*   **Example Packet**: `0x01 0x08 0x14 0x1E 0x01`
    *   *Interpretation: Active, 8 AM to 8 PM, every 30 mins, Buzzer ON.*

---

## 3. History Data Service (`...7392`) [Notify]
Device pushes 8-byte packets when history sync is requested.
*   **Format**: `[ID] [SBP] [DBP] [HR] [Timestamp (4 bytes)]`
*   **Byte Order**: Little Endian for Timestamp.
*   **Example (BP)**: `0x0A 0x78 0x50 0x4B 0x10 0x93 0xDA 0x69`
    *   *Meaning: ID 10, 120/80 mmHg, 75 HR, May 05 2026.*
*   **Example (Event)**: `0x0B 0x00 0x00 0x00 0x20 0x93 0xDA 0x69`
    *   *Meaning: User pressed Event button (SBP/DBP/HR = 0).*

---

## 4. Time Sync Service (`...7390`)

### Time Characteristic (`...7391`) [Read/Write]
*   **Packet**: 4 Bytes (Unix Timestamp)
*   **Byte Order**: Little Endian.
*   **Example Packet**: `0x28 0x93 0xDA 0x69` 
    *   *Meaning: Sets time to Tue May 05 2026 10:30:00 UTC.*
