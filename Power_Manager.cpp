#include "Power_Manager.h"
#include "Display_Handler.h"
#include "Constants.h"
#include "ABPM_Manager.h"

namespace Power_Manager {

void init() {
    pinMode(VBUS_PIN, INPUT_PULLDOWN); // Reserved for future VBUS detection
}

void handleChargingMode() {
    // No VBUS detection on this PCB revision — function is a no-op
}

void goToSleep(bool isConnected, bool isMeasuring) {
    ABPM_Manager::goToSleep(isConnected, isMeasuring);
}

} // namespace Power_Manager
