#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include "Constants.h"

// I2C Display (SH1107 128x128 OLED)
// Shares the same I2C bus as the pressure sensor (SDA=0, SCL=1)
#define OLED_ADDRESS    0x3D    // Common address (usually 0x3C or 0x3D)
#define OLED_WIDTH      128
#define OLED_HEIGHT     128

// Battery ADC
// Assumes a voltage divider from VDD (battery) to this pin
// e.g., 100K + 100K → ADC reads Vbat/2. Adjust BATTERY_DIVIDER_RATIO as needed.
#define BATTERY_ADC_PIN      BATTERY_PIN
#define BATTERY_DIVIDER_RATIO 2.0f     // Vbat = ADC_voltage * ratio
#define BATTERY_ADC_VREF     3.3f      // ESP32-C3 reference voltage
#define BATTERY_MAX_V        4.2f      // 100% charge
#define BATTERY_MIN_V        3.1f      // 0% charge / deep sleep threshold

// VBUS Detection (Charger connected)
// Connect USB VBUS (5V) through 100K + 100K voltage divider to this GPIO
#define VBUS_DETECT_PIN      VBUS_PIN

namespace Display_Handler {
    void init();
    void off();
    void on();
    void flipScreen(bool flipped); // 180° hardware flip for mounting orientation change

    // Screen state renderers
    void showBoot();
    void showWaitForStart(uint8_t battPercent, bool bleActive);
    void showCharging(uint8_t battPercent, float battVoltage);
    void showChargingIdle(uint8_t battPercent, int minutesToFull);    // Button press while charging
    void showChargingFull(uint8_t battPercent);    // New: dedicated charging screen
    void showIdle(uint8_t battPercent, bool bleConnected, bool isAdv, bool abpmOn, float pressure, uint8_t sbp, uint8_t dbp, uint8_t hr);
    void showInflating(bool showText, uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv);
    void showMeasuring(bool alternateText, uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv);
    void showResults(uint8_t sbp, uint8_t dbp, uint8_t hr, uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv);
    void showAbpmAlarm(uint8_t secondsLeft);
    void showAbpmSaving();
    void showEventMarked();
    void showSleep();
    void showPowerOff();
    void showError(const char* msg);

    // Battery utility
    float readBatteryVoltage();
    uint8_t getBatteryPercent();
    bool isCharging();
    bool isBatteryCritical();
}

#endif
