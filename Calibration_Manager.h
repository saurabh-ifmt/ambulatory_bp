#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include "SPIFFS.h"

namespace Calibration_Manager {
    extern float calibM;
    extern float calibC;

    void init();
    bool load();
    bool save(float m, float c);
}

#endif
