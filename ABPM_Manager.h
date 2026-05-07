#ifndef ABPM_MANAGER_H
#define ABPM_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include "Constants.h"

struct ABPMSchedule {
    uint8_t enabled;    // 0x01 = Enabled, 0x00 = Disabled
    uint8_t startHour;  // 0-23
    uint8_t endHour;    // 0-23
    uint8_t interval;   // Minutes
    uint8_t buzzer;     // 0x01 = On, 0x00 = Off
};

#define MAX_ABPM_SCHEDULES 4

// RTC Memory (Survives Sleep)
extern RTC_DATA_ATTR bool abpmMode;
extern RTC_DATA_ATTR ABPMSchedule abpmSchedules[MAX_ABPM_SCHEDULES];
extern RTC_DATA_ATTR uint32_t nextScheduledReading;

namespace ABPM_Manager {
    void init();
    void goToSleep(bool isConnected, bool isMeasuring);
    void handleWakeup();
    void setConfig(const uint8_t* data, size_t len);
    bool isPendingTrigger();
    bool isPreAlarmTriggered();
    bool hasBuzzerEnabled();
    bool isBuzzerActiveNow();
    uint32_t getNextScheduledTime();
    void clearPendingTrigger();
    void clearPreAlarm();
    void updateSchedule();
    extern bool abpmKillRequest;
}

#endif
