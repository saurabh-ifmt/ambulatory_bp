#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

namespace Power_Manager {
    void init();
    void handleChargingMode();
    void goToSleep(bool isConnected, bool isMeasuring);
}

#endif
