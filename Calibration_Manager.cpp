#include "Calibration_Manager.h"

namespace Calibration_Manager {

float calibM = 0;
float calibC = 0;

void init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[CAL] SPIFFS Mount Failed");
    } else {
        Serial.println("[CAL] SPIFFS mounted OK");
        load();
    }
}

bool load() {
    Serial.println("[CAL] Loading pressure_cal.bin...");
    File file = SPIFFS.open("/pressure_cal.bin", FILE_READ);
    if (!file || file.size() != sizeof(float) * 2) {
        Serial.println("[CAL] No valid calibration file found. Using defaults.");
        calibM = 0;
        calibC = 0;
        return false;
    }
    file.read((uint8_t *)&calibM, sizeof(float));
    file.read((uint8_t *)&calibC, sizeof(float));
    file.close();
    Serial.printf("[CAL] Result: m=%f, c=%f\n", calibM, calibC);
    return true;
}

bool save(float m, float c) {
    File file = SPIFFS.open("/pressure_cal.bin", FILE_WRITE);
    if (!file) {
        Serial.println("[CAL] Failed to open file for writing");
        return false;
    }
    file.write((uint8_t *)&m, sizeof(float));
    file.write((uint8_t *)&c, sizeof(float));
    file.close();
    calibM = m;
    calibC = c;
    Serial.println("[CAL] Calibration saved successfully");
    return true;
}

} // namespace Calibration_Manager
