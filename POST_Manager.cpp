#include "POST_Manager.h"
#include <Wire.h>
#include <SPIFFS.h>
#include "Constants.h"
#include "Display_Handler.h"

// ─── Internal Helpers ─────────────────────────────────────────────────────────

// Probe an I2C address. Returns true if device ACKs.
static bool _postProbeI2C(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

// Print a structured POST result line to Serial.
static void _postLog(bool pass, bool critical, const char* test, const char* detail) {
    const char* tag = pass ? "PASS" : (critical ? "FAIL" : "WARN");
    Serial.printf("[POST] %-4s | %-28s | %s\n", tag, test, detail);
}

// Halt: show error on OLED (if present), audible fault beep loop, never return.
static void _postHalt(bool oledOK, const char* failTest, const char* failDetail) {
    Serial.printf("\n[POST] *** CRITICAL FAILURE: %s ***\n", failTest);
    Serial.printf("[POST] Reason  : %s\n", failDetail);
    Serial.println("[POST] Device HALTED — fix hardware and reset.");

    if (oledOK) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.30s", failTest);
        Display_Handler::showError(buf);
    }

    // Repeating double-beep fault pattern — audible even without serial monitor
    while (true) {
        ledcWrite(BUZZER_PIN, BUZZER_DUTY); delay(150);
        ledcWrite(BUZZER_PIN, 0);           delay(100);
        ledcWrite(BUZZER_PIN, BUZZER_DUTY); delay(150);
        ledcWrite(BUZZER_PIN, 0);           delay(800);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool POST_Manager::run(bool isGenuineReset) {
    Serial.println("\n============================================================");
    Serial.println("[POST] Power-On Self Test — checking all subsystems...");
    Serial.println("------------------------------------------------------------");

    bool allCriticalOK   = true;
    bool oledFound       = false;
    char failTest  [40]  = "";
    char failDetail[80]  = "";

    // Record first critical failure (plain helper, avoids lambda compiler issues)
    #define RECORD_FAIL(t, d)  do { \
        if (failTest[0] == '\0') { \
            strncpy(failTest,   (t), sizeof(failTest)   - 1); \
            strncpy(failDetail, (d), sizeof(failDetail) - 1); \
        } \
        allCriticalOK = false; \
    } while(0)

    // ── Test 1: OLED Display (I2C 0x3C) — CRITICAL ───────────────────────────
    // NOTE: Display_Handler::init() already ran before POST, so OLED should be
    // responding. Probe again to explicitly confirm hardware presence.
    oledFound = _postProbeI2C(OLED_ADDRESS);
    _postLog(oledFound, true, "OLED (0x3C)",
             oledFound ? "Found — OK" : "NOT FOUND — check SDA/SCL wiring");
    if (!oledFound) RECORD_FAIL("OLED Not Found", "No ACK at I2C 0x3C");

    // ── Test 2: Pressure Sensor WF100D (I2C 0x6D) — CRITICAL ─────────────────
    bool sensorFound = _postProbeI2C(0x6D);
    _postLog(sensorFound, true, "Pressure Sensor (0x6D)",
             sensorFound ? "Found — WF100D OK" : "NOT FOUND — BP measurement impossible");
    if (!sensorFound) RECORD_FAIL("Pressure Sensor Missing", "No ACK at I2C 0x6D");

    // ── Test 3: PCF8574 I/O Expander (I2C 0x20) — WARNING ────────────────────
    bool pcfFound = _postProbeI2C(PCF8574_ADDR);
    _postLog(pcfFound, false, "PCF8574 Buttons (0x20)",
             pcfFound ? "Found — P1/P2/P3 OK" : "NOT FOUND — hardware buttons disabled");

    // ── Test 4: SPIFFS Filesystem — WARNING ───────────────────────────────────
    // Calibration_Manager::init() already called SPIFFS.begin() earlier in setup().
    // Check mounted state without remounting to avoid accidental format.
    bool spiffsOK = SPIFFS.begin(false); // returns true immediately if already mounted
    char spiffsDetail[56] = "";
    if (spiffsOK) {
        snprintf(spiffsDetail, sizeof(spiffsDetail), "OK — Total=%uKB Used=%uKB",
                 (unsigned)(SPIFFS.totalBytes() / 1024),
                 (unsigned)(SPIFFS.usedBytes()  / 1024));
    } else {
        strncpy(spiffsDetail, "Mount FAILED — calibration/history unavailable", sizeof(spiffsDetail) - 1);
    }
    _postLog(spiffsOK, false, "SPIFFS Filesystem", spiffsDetail);

    // ── Test 5: Battery Voltage — WARNING ─────────────────────────────────────
    int   rawADC = analogRead(BATTERY_ADC_PIN);
    float adcV   = (rawADC / 4095.0f) * 3.3f;
    float batV   = adcV * 2.0f;                         // 1:1 resistive divider
    bool  batOK  = (batV >= 2.8f && batV <= 4.5f);
    char  batDetail[32] = "";
    snprintf(batDetail, sizeof(batDetail), "%.2fV — %s",
             batV, batOK ? "OK" : "LOW or OUT OF RANGE");
    _postLog(batOK, false, "Battery Voltage", batDetail);

    // ── Test 6: Buzzer proof-of-life beep ─────────────────────────────────────
    // Always runs. Longer on genuine reset (300ms = clearly audible confirmation),
    // shorter on sleep wakeup (80ms = brief, doesn't disturb quiet environments).
    uint32_t beepDuration = isGenuineReset ? 300 : 80;
    Serial.printf("[POST] INFO | %-28s | %ums beep (%s)\n",
                  "Buzzer (GPIO19)", beepDuration,
                  isGenuineReset ? "genuine reset" : "sleep wakeup");
    ledcWrite(BUZZER_PIN, BUZZER_DUTY); delay(beepDuration); ledcWrite(BUZZER_PIN, 0);

    // ── Summary ───────────────────────────────────────────────────────────────
    Serial.println("------------------------------------------------------------");
    if (allCriticalOK) {
        Serial.println("[POST] All critical checks PASSED — booting normally.");
        Serial.println("============================================================\n");
        return true;
    }

    // Critical failure — halt device
    _postHalt(oledFound, failTest, failDetail);
    return false; // Never reached

    #undef RECORD_FAIL
}
