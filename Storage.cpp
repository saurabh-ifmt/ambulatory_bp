#include "Storage.h"
#include <Arduino.h>

namespace Storage {

// Meta-data to track circular buffer position
struct HistoryMeta {
    uint16_t nextIndex; // 0 to 499
    uint16_t count;     // Total records stored (max 500)
};

static HistoryMeta loadMeta() {
    HistoryMeta meta = {0, 0};
    File file = SPIFFS.open("/history_meta.bin", FILE_READ);
    if (file) {
        file.read((uint8_t*)&meta, sizeof(meta));
        file.close();
    } else {
        Serial.println("[Storage] No meta file found, starting at 0");
    }
    return meta;
}

static void saveMeta(HistoryMeta meta) {
    File file = SPIFFS.open("/history_meta.bin", FILE_WRITE);
    if (file) {
        file.write((uint8_t*)&meta, sizeof(meta));
        file.close();
    } else {
        Serial.println("[Storage] CRITICAL: Failed to save meta file!");
    }
}

void saveReading(uint8_t sbp, uint8_t dbp, uint8_t hr, uint8_t id, uint32_t timestamp) {
    HistoryMeta meta = loadMeta();
    
    // Integrity Check: File must be exactly 4000 bytes (500 records * 8 bytes)
    bool fileValid = false;
    if (SPIFFS.exists(HISTORY_FILE)) {
        File f = SPIFFS.open(HISTORY_FILE, FILE_READ);
        if (f && f.size() == (MAX_HISTORY_RECORDS * RECORD_SIZE)) {
            fileValid = true;
        }
        if (f) f.close();
    }

    if (!fileValid) {
        Serial.println("[Storage] History file invalid or missing. Initializing 4000-byte buffer...");
        SPIFFS.remove(HISTORY_FILE);
        File f = SPIFFS.open(HISTORY_FILE, FILE_WRITE);
        if (f) {
            uint8_t zero[RECORD_SIZE] = {0,0,0,0,0,0,0,0};
            for(int i=0; i<MAX_HISTORY_RECORDS; i++) f.write(zero, RECORD_SIZE);
            f.close();
            meta = {0, 0};
            saveMeta(meta);
        } else {
            Serial.println("[Storage] ERR: Could not create history file!");
            return;
        }
    }

    // Now open for actual writing
    File file = SPIFFS.open(HISTORY_FILE, "r+");
    if (file) {
        Serial.printf("[Storage] Saving record ID %d with TS %u at slot %d\n", id, timestamp, meta.nextIndex);
        file.seek(meta.nextIndex * RECORD_SIZE);
        
        uint8_t data[8];
        data[0] = sbp;
        data[1] = dbp;
        data[2] = hr;
        data[3] = id;
        // Pack timestamp (little endian)
        data[4] = (uint8_t)(timestamp & 0xFF);
        data[5] = (uint8_t)((timestamp >> 8) & 0xFF);
        data[6] = (uint8_t)((timestamp >> 16) & 0xFF);
        data[7] = (uint8_t)((timestamp >> 24) & 0xFF);
        
        file.write(data, RECORD_SIZE);
        file.close();
        
        meta.nextIndex = (meta.nextIndex + 1) % MAX_HISTORY_RECORDS;
        if (meta.count < MAX_HISTORY_RECORDS) meta.count++;
        saveMeta(meta);
        
        Serial.printf("[Storage] Save complete. Total records: %d\n", meta.count);
    }
}

int getHistoryCount() {
    return loadMeta().count;
}

bool getReading(int index, uint8_t* buffer) {
    HistoryMeta meta = loadMeta();
    if (index >= meta.count) return false;
    
    int physicalIndex = (meta.nextIndex - 1 - index + MAX_HISTORY_RECORDS) % MAX_HISTORY_RECORDS;
    
    File file = SPIFFS.open(HISTORY_FILE, FILE_READ);
    if (!file) {
        Serial.println("[Storage] ERR: Could not open history file for sync!");
        return false;
    }
    
    // Check if the file is actually large enough to contain this index
    if (file.size() < (physicalIndex + 1) * RECORD_SIZE) {
        Serial.printf("[Storage] ERR: File too small (%d) for physical index %d\n", (int)file.size(), physicalIndex);
        file.close();
        return false;
    }

    if (file.seek(physicalIndex * RECORD_SIZE)) {
        size_t readLen = file.read(buffer, RECORD_SIZE);
        file.close();
        if (readLen != RECORD_SIZE) {
            Serial.printf("[Storage] ERR: Read %d bytes, expected %d\n", (int)readLen, RECORD_SIZE);
            return false;
        }
        return true;
    } else {
        Serial.printf("[Storage] ERR: Seek to %d failed!\n", physicalIndex * RECORD_SIZE);
    }
    
    file.close();
    return false;
}

void clearHistory() {
    SPIFFS.remove(HISTORY_FILE);
    SPIFFS.remove("/history_meta.bin");
    Serial.println("[Storage] History and Meta cleared");
}

} // namespace Storage
