#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>
#include "FS.h"
#include "SPIFFS.h"

#define HISTORY_FILE "/history.bin"
#define MAX_HISTORY_RECORDS 500
#define RECORD_SIZE 8

namespace Storage {
    void saveReading(uint8_t sbp, uint8_t dbp, uint8_t hr, uint8_t id, uint32_t timestamp);
    int getHistoryCount();
    bool getReading(int index, uint8_t* buffer);
    void clearHistory();
}

#endif
