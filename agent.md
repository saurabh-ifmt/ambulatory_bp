# Agent Configuration for BT2501 BP Device Project

## Overview
This agent configuration file customizes AI assistance for the Blood Pressure (BP) measurement device project. It provides specialized guidance for embedded systems development, signal processing, and ESP32 firmware engineering.

## Project Context

**Project**: BT2501_BP_Track - ESP32-based Blood Pressure Measurement Device  
**Technology**: Embedded C/C++ (Arduino), ESP32-C3, BLE (Bluetooth Low Energy), Signal Processing  
**Domain**: Medical Device Firmware, Biomedical Signal Processing

## When to Invoke This Agent
- Debugging signal processing algorithms for BP calculation
- Implementing new ESP32 features (BLE, RTC, Power Management)
- Optimizing memory usage and performance
- Adding new hardware interfaces or sensors
- Signal processing pipeline modifications
- Testing and validation of measurement accuracy

## Code Conventions

### File Organization
- Header files (.h) contain declarations and inline functions
- Implementation files (.cpp) contain algorithm implementations
- Constants defined in Constants.h for easy configuration
- Components are modular: `BloodPressure.cpp`, `DynamicInflation.cpp`, `Filter.cpp`, etc.

### Naming Conventions
- Functions: camelCase with descriptive names (e.g., `calculateBloodPressure()`, `_readPressure()`)
- Macros/Constants: UPPER_CASE (e.g., `MAX_DATA_POINTS`, `PWM_CHANNEL`)
- Variables: camelCase (e.g., `systolicBP`, `fill_index`)
- Global State: `volatile` qualifiers for task synchronization

### Hardware Pins (ESP32-C3)
- **I2C SDA**: 0
- **I2C SCL**: 1
- **PRESSURE_SENSOR_ADDR**: 0x6D (WF100D)
- **MOTOR_PIN**: 10 (Inflation pump control)
- **VALVE_PIN**: 3 (Normally Open valve control)
- **BUZZER_PIN**: 19 (Active buzzer / Alarm)
- **BATTERY_ADC**: 2 (Vbat measurement)
- **CHARGING_DETECT**: 6 (VBUS sensing)
- **WAKE_BUTTON**: 18 (Power/Wake)
- **PWM Channels**: 0 (motor), 1 (valve), LEDC (buzzer)

## BLE Authentication & Protocol

### Two-Byte Password Validation
- **Password:** `{'c', '5'}` (0x63, 0x35)
- **Transmission:** Write to AUTH characteristic (UUID: 1BF0272E-A068-486A-9889-FDAB3CA2D837)
- **Success:** Sets `is_client_authorized = true`, notifies AUTH_STATUS = 1
- **Failure:** Immediate disconnect or command blocking
- **Timeout:** 30 seconds from connection if not authenticated

### Protected Operations
All commands and notifications require successful authentication:
- START (0x01), STOP (0x02), RESET (0x03 - Factory Reset)
- STATUS, LIVE_BP, RESULTS, HISTORY_DUMP, ABPM_CONFIG
-
-## System Features
-- **Power Management**: ESP32 Light Sleep with GPIO/Timer wakeup.
-- **Charging Mode**: Auto-standby on plug-in; 60s timeout; Time-to-full estimation.
-- **Responsive Startup**: Hold IO18 for 2s to start (reduced from 3s).
-- **Maintenance**: Hardware-based menu removed for security; all maintenance handled via BLE 0x03 command.

## Key Implementation Details

### Signal Processing Flow (Oscillometric)
1. **Dynamic Inflation**: Monitor oscillations during pump-up to stop at target pressure.
2. **Band-pass Filtering**: 0.5 - 5 Hz `filtfilt` extraction.
3. **FFT Analysis**: Identify heart rate and optimal window distance.
4. **Oscillometric Envelope**: Calculate pulse amplitudes vs cuff pressure.
5. **Polynomial Fitting**: 8th-degree curve fitting for smooth envelope analysis.
6. **Ratio-based BP**: Calculate SBP/DBP/MAP from envelope thresholds.

### Memory & Stability Management
- **MAX_DATA_POINTS**: 3200 (Critical balance for RAM vs Deflation time)
- **Static Storage**: All local calculation arrays (Ci_list, Ai_list, etc.) must be `static` to prevent 8KB Stack Overflow.
- **Heap Management**: FFT buffers (32KB) use heap with NULL checks to prevent fragmentation crashes.
- **Index Guards**: Recursive boundary checks in all sampling loops to prevent `abort()` crashes.

### Communication Protocols
Device advertises as **"BP_Device"**. Characteristics are grouped into 2 primary services:

1. **Clinical & Auth Service** (`9E6EEA7F-D406-430D-B80B-D5A7DFAC7398`)
   - **AUTH** (`...D837`): Write 'c5' to authorize.
   - **AUTH_STATUS** (`...7393`): Notify 1=Authorized.
   - **COMMAND** (`...7397`): Write 0x01 START, 0x02 STOP, 0x03 RESET, 0x04 SYNC.
   - **ABPM_CONFIG** (`...7399`): Read/Write ABPM settings. Supports up to 4 concurrent schedules (5 bytes each). Any new write overwrites existing schedules. Format: `[Enabled][StartHr][EndHr][IntMin][Buzzer]`.
   - **STATUS** (`...7396`): Notify device state.
   - **LIVE_BP** (`...7395`): Notify real-time pressure.
   - **RESULTS** (`...7394`): Notify final BP/HR results.
   - **HISTORY** (`...7392`): Notify memory dump packets.

2. **Time Sync Service** (`9E6EEA7F-D406-430D-B80B-D5A7DFAC7390`)
   - **TIME** (`...7391`): Write 4-byte Unix TS.

## Common Tasks

### Debugging Measurement Issues
1. Inspect raw 24-bit 2's complement sensor readings in `pressure.cpp`
2. Verify buffer shifting logic in `BP_Dev.ino` when `index >= 3200`
3. Validate polynomial fit quality in `Interpolation.cpp`

### Performance Optimization
1. Monitor Free Heap (~70KB expected after BLE init)
2. Use `static` for any local array larger than 100 bytes
3. Check task synchronization between BLE task and Loop task using `volatile`

## Troubleshooting Guide

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| Stack protection fault | Large local array | Move to `static` storage |
| `abort()` during calculation | Buffer overflow | Check index guard at 3200 points |
| BLE Malloc failed | Too much static RAM | Reduce `MAX_DATA_POINTS` below 4000 |
| Negative pressure values | Sign bit missing | Implement 24-bit sign extension |

## Important Notes
- This is a medical device - prioritize measurement accuracy and reliability
- Changes to signal processing may require recalibration
- Test thoroughly before deploying firmware updates
- Measurement time is ~64 seconds; use shifting buffer for longer runs
