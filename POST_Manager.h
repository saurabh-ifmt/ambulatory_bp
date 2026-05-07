#ifndef POST_MANAGER_H
#define POST_MANAGER_H

#include <Arduino.h>

// ─── POST_Manager ──────────────────────────────────────────────────────────────
// Power-On Self Test — runs every boot to verify all hardware subsystems.
// Critical failures halt the device and show an error on the OLED.
// Warnings are logged to Serial but do not block boot.
namespace POST_Manager {
    // Run all POST checks.
    // isGenuineReset: true = power-on/flash/crash, false = sleep wakeup.
    // Critical failures halt the device. Warnings log to Serial and continue.
    // Returns true if all critical tests passed (device may proceed to boot).
    bool run(bool isGenuineReset);
}

#endif
