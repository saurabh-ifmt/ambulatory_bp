# BLE Command Guide (Quick Reference)

This file summarizes how to interact with the **BP_Device** via BLE.

## 1. Authentication (CRITICAL)
**Must be done first.**
*   **Service**: `...7398` (Clinical & Auth)
*   **Characteristic (Write)**: `...D837`
*   **Payload**: `c5` (Hex: `0x63 0x35`)
*   **Verification**: Check **Auth Status** (`...7393`). `0x01` means success.

---

## 2. Measurement Control
*   **Service**: `...7398`
*   **Characteristic (Write)**: `...7397`
*   **Commands**:
    *   `0x01`: START
    *   `0x02`: STOP
    *   `0x03`: RESET
    *   `0x04`: SYNC (History dump)

---

## 3. ABPM Configuration
*   **Service**: `...7398`
*   **Characteristic (Read/Write)**: `...7399`
*   **Payload**: Multiple of 5 Bytes (Up to 4 schedules).
*   **Block Format**: `[Enabled (0x01/0x00)] [StartHr] [EndHr] [IntervalMin] [Buzzer (0x01/0x00)]`
*   **Note**: All previous schedules are wiped when a new write occurs.

---

## 4. Time Synchronization
*   **Service**: `...7390` (Time Sync Service)
*   **Characteristic (Write)**: `...7391`
*   **Payload**: 4 Bytes (Unix Timestamp, Little Endian).

---

## 5. Monitoring Data (Notify)
*   **Service**: `...7398`
    *   **Status** (`...7396`)
    *   **Live BP** (`...7395`)
    *   **Results** (`...7394`)
    *   **History** (`...7392`)
