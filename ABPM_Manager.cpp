#include "ABPM_Manager.h"
#include "BLE_Handler.h"
#include "Buzzer_Handler.h"
#include <esp_sleep.h>

RTC_DATA_ATTR bool abpmMode = false;
RTC_DATA_ATTR ABPMSchedule abpmSchedules[MAX_ABPM_SCHEDULES];
RTC_DATA_ATTR uint32_t nextScheduledReading = 0; 
static bool abpmTriggered = false;
static bool preAlarmTriggered = false;
RTC_DATA_ATTR uint32_t lastPreAlarmReading = 0; // Tracks which reading we already buzzed for
bool ABPM_Manager::abpmKillRequest = false;

namespace ABPM_Manager {

static bool isInWindow(int hour, int start, int end) {
    if (start == end) return true; // Interpret equal start/end as 24h
    if (start < end) {
        return (hour >= start && hour < end);
    } else {
        return (hour >= start || hour < end);
    }
}

void init() {
    Serial.println("\n[ABPM_INIT] Restoring multi-schedule settings...");
    Serial.printf("  - Mode: %s\n", abpmMode ? "ENABLED" : "DISABLED");
    if (abpmMode) {
        for (int i = 0; i < MAX_ABPM_SCHEDULES; i++) {
            if (abpmSchedules[i].enabled) {
                Serial.printf("  - [#%d] %02d:00-%02d:00 | Int: %d min | Buzzer: %s\n", 
                    i, abpmSchedules[i].startHour, abpmSchedules[i].endHour, 
                    abpmSchedules[i].interval, abpmSchedules[i].buzzer ? "ON" : "OFF");
            }
        }
    }
    Serial.println("----------------------------------------");
}

void updateSchedule() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int currentHour = timeinfo.tm_hour;

    int activeInterval = 0;
    bool foundActive = false;

    // Search in reverse order to give priority to the "latest" defined schedule
    for (int i = MAX_ABPM_SCHEDULES - 1; i >= 0; i--) {
        if (abpmSchedules[i].enabled && isInWindow(currentHour, abpmSchedules[i].startHour, abpmSchedules[i].endHour)) {
            activeInterval = abpmSchedules[i].interval;
            foundActive = true;
            break;
        }
    }

    if (foundActive && activeInterval > 0) {
        nextScheduledReading = (uint32_t)now + (activeInterval * 60);
    } else {
        // Not in any active window. Find the next schedule start time.
        uint32_t nearestStart = 0;
        uint32_t minWait = 0xFFFFFFFF;

        for (int i = 0; i < MAX_ABPM_SCHEDULES; i++) {
            if (abpmSchedules[i].enabled) {
                struct tm startTm = timeinfo;
                startTm.tm_hour = abpmSchedules[i].startHour;
                startTm.tm_min = 0;
                startTm.tm_sec = 0;
                
                time_t startT = mktime(&startTm);
                if (startT <= now) {
                    startT += 86400; // Tomorrow
                }
                
                uint32_t wait = (uint32_t)(startT - now);
                if (wait < minWait) {
                    minWait = wait;
                    nearestStart = (uint32_t)startT;
                }
            }
        }

        if (minWait != 0xFFFFFFFF) {
            nextScheduledReading = nearestStart;
        } else {
            nextScheduledReading = 0; // No active schedules at all
        }
    }
    
    if (nextScheduledReading > 0) {
        time_t next = (time_t)nextScheduledReading;
        struct tm curInfo;
        localtime_r(&now, &curInfo);
        char curBuf[32];
        strftime(curBuf, sizeof(curBuf), "%H:%M:%S", &curInfo);
        
        struct tm nextInfo;
        localtime_r(&next, &nextInfo);
        char nextBuf[32];
        strftime(nextBuf, sizeof(nextBuf), "%H:%M:%S", &nextInfo);
        
        Serial.printf("[ABPM] Clock: %s | Next trigger: %s\n", curBuf, nextBuf);
    } else {
        Serial.println("[ABPM] No upcoming scheduled readings.");
    }
}

void goToSleep(bool isConnected, bool isMeasuring) {
    if (isConnected || isMeasuring) return;

    // Wait for buzzer to finish its pattern before sleeping
    while (Buzzer_Handler::isBusy()) {
        Buzzer_Handler::tick();
        delay(10);
    }

    Serial.println("----------------------------------------");
    if (abpmMode && nextScheduledReading > 0) {
        time_t now;
        time(&now);
        
        if (nextScheduledReading <= (uint32_t)now) {
            updateSchedule();
        }

        uint32_t diffSecs = nextScheduledReading - (uint32_t)now;
        
        if (diffSecs > 35) {
            uint32_t sleepTime = diffSecs - 30;
            Serial.printf("[ABPM_PWR] Sleep: Wake in %ds (30s pre-alarm)\n", sleepTime);
            esp_sleep_enable_timer_wakeup((uint64_t)sleepTime * 1000000ULL);
        } else {
            if (diffSecs < 5) diffSecs = 5;
            Serial.printf("[ABPM_PWR] Final Sleep: Wake in %ds\n", diffSecs);
            esp_sleep_enable_timer_wakeup((uint64_t)diffSecs * 1000000ULL);
        }
    } else {
        Serial.println("[ABPM_PWR] Power Save: Waiting for user button...");
    }

    // GPIO18 (WAKE button) can also wake from light sleep
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)BUTTON_WAKE_PIN, GPIO_INTR_LOW_LEVEL);
    
    Serial.println("[ABPM_PWR] Action: LIGHT SLEEP START");
    Serial.println("----------------------------------------");
    Serial.flush();
    
    esp_light_sleep_start();
    handleWakeup();
}

void handleWakeup() {
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    if (reason == ESP_SLEEP_WAKEUP_TIMER) {
        time_t now;
        time(&now);
        if (nextScheduledReading > 0 && nextScheduledReading > (uint32_t)now + 5) {
            preAlarmTriggered = true;
            abpmTriggered = false;
            
            // Check if buzzer should be used for this window
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            bool useBuzzer = false;
            for (int i = MAX_ABPM_SCHEDULES - 1; i >= 0; i--) {
                if (abpmSchedules[i].enabled && isInWindow(timeinfo.tm_hour, abpmSchedules[i].startHour, abpmSchedules[i].endHour)) {
                    useBuzzer = abpmSchedules[i].buzzer;
                    break;
                }
            }
            Serial.printf("[ABPM_WAKE] Pre-alarm (Buzzer: %s)\n", useBuzzer ? "ON" : "OFF");
        } else {
            abpmTriggered = true;
            preAlarmTriggered = false;
            Serial.println("[ABPM_WAKE] Triggering Measurement.");
        }
    } else if (reason == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.println("[ABPM_WAKE] User Button Triggered.");
    } else {
        // Cold boot: stay off unless RTC memory says otherwise
        // (Note: abpmMode is saved in RTC memory)
        if (nextScheduledReading == 0) {
            abpmMode = false;
        }
    }
}

void setConfig(const uint8_t* data, size_t len) {
    // Clear all existing schedules first (Overwriting policy)
    for (int i = 0; i < MAX_ABPM_SCHEDULES; i++) {
        abpmSchedules[i].enabled = 0;
    }

    if (len >= 5 && data[0] != 0x00) {
        abpmMode = true;
        int numSchedules = len / 5;
        if (numSchedules > MAX_ABPM_SCHEDULES) numSchedules = MAX_ABPM_SCHEDULES;

        for (int i = 0; i < numSchedules; i++) {
            const uint8_t* block = &data[i * 5];
            abpmSchedules[i].enabled   = block[0];
            abpmSchedules[i].startHour = block[1];
            abpmSchedules[i].endHour   = block[2];
            abpmSchedules[i].interval  = block[3];
            abpmSchedules[i].buzzer    = block[4];
            
            Serial.printf("[ABPM_CFG] Slot %d: %02d-%02d, Int %d, Buzz %d\n", 
                          i, block[1], block[2], block[3], block[4]);
        }
        updateSchedule();
    } else {
        abpmMode = false;
        nextScheduledReading = 0;
        abpmKillRequest = true;
        Serial.println("[ABPM_CFG] ABPM Disabled / Schedules Cleared.");
    }
}

bool isPendingTrigger() {
    if (!abpmMode) return false;
    if (abpmTriggered) return true; // Direct timer wake

    // Live check for when the device is awake/connected
    time_t now;
    time(&now);
    if (nextScheduledReading > 0 && (uint32_t)now >= nextScheduledReading) {
        return true;
    }
    return false;
}

bool isPreAlarmTriggered() {
    if (!abpmMode || nextScheduledReading == 0) return false;
    
    // If set by timer wakeup, return true immediately
    if (preAlarmTriggered) return true;

    // Live check for when the device is already awake
    time_t now;
    time(&now);
    uint32_t cur = (uint32_t)now;

    // Trigger if we are in the 30-second window before the reading
    if (cur >= nextScheduledReading - 30 && cur < nextScheduledReading) {
        // Only trigger once per scheduled reading
        if (lastPreAlarmReading != nextScheduledReading) {
            return true;
        }
    }
    return false;
}

void clearPendingTrigger() {
    abpmTriggered = false;
}

void clearPreAlarm() {
    preAlarmTriggered = false;
    // Mark this reading as handled so we don't trigger again in the next loop cycle
    lastPreAlarmReading = nextScheduledReading;
}

bool hasBuzzerEnabled() {
    for (int i = 0; i < MAX_ABPM_SCHEDULES; i++) {
        if (abpmSchedules[i].enabled && abpmSchedules[i].buzzer != 0) {
            return true;
        }
    }
    return false;
}

bool isBuzzerActiveNow() {
    if (!abpmMode) return false;
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    for (int i = MAX_ABPM_SCHEDULES - 1; i >= 0; i--) {
        if (abpmSchedules[i].enabled && isInWindow(timeinfo.tm_hour, abpmSchedules[i].startHour, abpmSchedules[i].endHour)) {
            return (abpmSchedules[i].buzzer != 0);
        }
    }
    return false;
}

uint32_t getNextScheduledTime() {
    return nextScheduledReading;
}

}
