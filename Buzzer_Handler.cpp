#include "Buzzer_Handler.h"

// ── Internal State ────────────────────────────────────────────────────────────
static bool     _buzzerActive    = false;
static uint32_t _buzzerOffAt     = 0;     // millis() when to turn buzzer OFF

// Alarm pattern state
static bool     _alarmPending    = false;
static uint8_t  _alarmBeepCount  = 0;
static uint32_t _alarmNextAction = 0;     // millis() for next alarm state change
static bool     _alarmBuzzerOn   = false; // current buzzer state in alarm sequence

// ── Internal helpers ──────────────────────────────────────────────────────────
// Uses modern ESP32 Arduino v3.0+ API. ledcWrite(pin, duty) automatically
// handles channel management.
static void _on()  { ledcWrite(BUZZER_PIN, BUZZER_DUTY); }
static void _off() { ledcWrite(BUZZER_PIN, 0);           }

// ── Public API ────────────────────────────────────────────────────────────────

void Buzzer_Handler::init() {
    // Attach LEDC PWM to BUZZER_PIN using modern API (ESP32 Arduino v3.0+)
    // The core manages channel allocation automatically.
    ledcAttach(BUZZER_PIN, BUZZER_FREQ, BUZZER_RESOLUTION);
    _off(); // Ensure buzzer is silent on boot
    Serial.printf("[Buzzer] GPIO%d LEDC init (Modern API) — %dHz, %d-bit. Buzzer OFF.\n",
                  BUZZER_PIN, BUZZER_FREQ, BUZZER_RESOLUTION);
}

void Buzzer_Handler::tick() {
    uint32_t now = millis();

    // Handle single beep timeout
    if (_buzzerActive && now >= _buzzerOffAt) {
        _off();
        _buzzerActive = false;
    }

    // Handle alarm sequence (triple-beep: ON 200ms, OFF 150ms, × 3)
    if (_alarmPending && now >= _alarmNextAction) {
        if (_alarmBuzzerOn) {
            // Currently ON → turn OFF, wait 150ms
            _off();
            _alarmBuzzerOn   = false;
            _alarmNextAction = now + 150;
            _alarmBeepCount++;

            if (_alarmBeepCount >= 3) {
                // All 3 beeps done
                _alarmPending   = false;
                _alarmBeepCount = 0;
            }
        } else {
            // Currently OFF → turn ON, wait 200ms
            _on();
            _alarmBuzzerOn   = true;
            _alarmNextAction = now + 200;
        }
    }
}

void Buzzer_Handler::beep(uint32_t ms) {
    if (_alarmPending) return; // Don't interrupt alarm sequence
    _on();
    _buzzerActive = true;
    _buzzerOffAt  = millis() + ms;
}

void Buzzer_Handler::alarmBeep() {
    if (_alarmPending) return; // Already running
    _alarmPending    = true;
    _alarmBeepCount  = 0;
    _alarmBuzzerOn   = false;
    _alarmNextAction = millis(); // Start immediately
    Serial.println("[Buzzer] Triple-beep started.");
}

void Buzzer_Handler::eventBeep() {
    // Fast double-chirp (blocking is fine here as it's part of a 3s UI pause)
    _on(); delay(50);
    _off(); delay(50);
    _on(); delay(50);
    _off();
}

bool Buzzer_Handler::isBusy() {
    return _buzzerActive || _alarmPending;
}
