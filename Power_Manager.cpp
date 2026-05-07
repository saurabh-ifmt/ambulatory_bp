#include "Power_Manager.h"
#include "Display_Handler.h"
#include "Constants.h"
#include "ABPM_Manager.h"

namespace Power_Manager {

void init() {
    pinMode(VBUS_PIN, INPUT_PULLDOWN);
}

void handleChargingMode() {
    if (!Display_Handler::isCharging()) return;

    Serial.println("[Power] Entering Charging Loop...");
    while (Display_Handler::isCharging()) {
        Display_Handler::showCharging(Display_Handler::getBatteryPercent(), Display_Handler::readBatteryVoltage());
        
        // Check WAKE button directly on GPIO18
        if (digitalRead(BUTTON_WAKE_PIN) == LOW) {
            Display_Handler::on();
            
            uint8_t curB = Display_Handler::getBatteryPercent();
            int minsToFull = 0;
            if (curB < 100) {
                float remainingMah = BATTERY_CAPACITY_MAH * (1.0f - (curB / 100.0f));
                minsToFull = (int)((remainingMah / CHARGE_CURRENT_MA) * 60.0f);
            }
            
            Display_Handler::showChargingIdle(curB, minsToFull);
            delay(3000); 
        }
        
        // Deep power save while charging (wakes on timer OR GPIO18 button press)
        esp_sleep_enable_timer_wakeup(5000000); // 5s refresh
        esp_sleep_enable_gpio_wakeup();
        gpio_wakeup_enable((gpio_num_t)BUTTON_WAKE_PIN, GPIO_INTR_LOW_LEVEL);
        esp_light_sleep_start();
    }
    Serial.println("[Power] Charger removed.");
    
    // Silent Resume: Skip any missed readings during charging 
    // and wait for the next full interval.
    if (abpmMode) {
        Serial.println("[Power] ABPM Rescheduled (Silent Resume)");
        ABPM_Manager::updateSchedule();
    }
}

void goToSleep(bool isConnected, bool isMeasuring) {
    ABPM_Manager::goToSleep(isConnected, isMeasuring);
}

} // namespace Power_Manager
