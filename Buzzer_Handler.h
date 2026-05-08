#ifndef BUZZER_HANDLER_H
#define BUZZER_HANDLER_H

#include <Arduino.h>
#include "Constants.h"  // For BUZZER_PIN (GPIO19)

// Active buzzer on BUZZER_PIN:
//   HIGH → buzzer ON  (current flows through buzzer)
//   LOW  → buzzer OFF
#define BUZZER_ACTIVE   HIGH
#define BUZZER_INACTIVE LOW

namespace Buzzer_Handler {
    void init();                  // Configure BUZZER_PIN as OUTPUT, ensure buzzer off
    void tick();                  // Call every loop() — handles non-blocking timing
    void beep(uint32_t ms);       // Single beep for given duration (non-blocking)
    void alarmBeep();             // Triple-beep pattern for ABPM pre-alarm
    void eventBeep();             // Fast double-chirp for event marker
    bool isBusy();                // Returns true if a beep is currently active
}

#endif
