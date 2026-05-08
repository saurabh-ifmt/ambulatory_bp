#include "BLE_Handler.h"
#include "Storage.h"
#include "ABPM_Manager.h"
#include <algorithm>
#include <cctype>
#include <sys/time.h>
#include <time.h>

// Static instance
BLE_Handler* BLE_Handler::instance = nullptr;

// BLE Callback Classes
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    BLE_Handler::getInstance().onConnect();
  }
  
  void onDisconnect(BLEServer* pServer) {
    BLE_Handler::getInstance().onDisconnect();
  }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    String charUUID = pChar->getUUID().toString();
    charUUID.toLowerCase(); 
    
    // CRITICAL: Fetch raw bytes and length directly to handle binary data (0x00) correctly.
    std::string val((char*)pChar->getData(), pChar->getLength());
    
    if (val.length() == 0) return;
    
    Serial.printf("[BLE_RECEIVE] -> Target UUID: %s | Data Length: %d\n", charUUID.c_str(), val.length());
    
    String authUUID = String(CHAR_AUTH_UUID); authUUID.toLowerCase();
    String timeUUID = String(CHAR_TIME_UUID); timeUUID.toLowerCase();
    String abpmUUID = String(CHAR_ABPM_CONFIG_UUID); abpmUUID.toLowerCase();
    String cmdUUID = String(CHAR_COMMAND_UUID); cmdUUID.toLowerCase();

    if (charUUID == authUUID || charUUID == "1bf0272e-a068-486a-9889-fdab3ca2d837") {
      Serial.println("[BLE_ROUTING] Case: AUTHENTICATION");
      BLE_Handler::getInstance().handleAuthWrite(val);
    }
    else if (charUUID == timeUUID) {
      Serial.println("[BLE_ROUTING] Case: TIME_SYNC");
      BLE_Handler::getInstance().handleTimeWrite(val);
    }
    else if (charUUID == abpmUUID) {
      Serial.println("[BLE_ROUTING] Case: ABPM_CONFIG");
      BLE_Handler::getInstance().handleABPMWrite(val);
    }
    else if (charUUID == cmdUUID) {
      Serial.println("[BLE_ROUTING] Case: COMMAND (START/STOP)");
      BLE_Handler::getInstance().handleCommandWrite(charUUID.c_str(), val);
    }
    else {
      Serial.printf("[BLE_ROUTING] Case: UNKNOWN UUID (%s)\n", charUUID.c_str());
    }
  }
};

class MyDescriptorCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) {
    uint8_t* val = pDescriptor->getValue();
    size_t len = pDescriptor->getLength();
    if (len >= 1) {
      bool notifications = val[0] & 0x01;
      bool indications = val[0] & 0x02;
      Serial.print("[BLE_CCCD] Configuration changed - Notifications: ");
      Serial.print(notifications ? "ON" : "OFF");
      Serial.print(", Indications: ");
      Serial.println(indications ? "ON" : "OFF");
    }
  }
};

// Singleton getter
BLE_Handler& BLE_Handler::getInstance() {
  if (instance == nullptr) {
    instance = new BLE_Handler();
  }
  return *instance;
}

// Constructor
BLE_Handler::BLE_Handler() 
  : pServer(nullptr), pCharStart(nullptr), pCharStatus(nullptr), pCharLiveBP(nullptr),
    pCharResult(nullptr), pCharAuth(nullptr), pCharAuthStatus(nullptr),
    pCharHistory(nullptr), bleConnected(false), is_client_authorized(false),
    bleConnectTime(0), lastCommand(static_cast<BLECommand>(0)),
    lastLiveBPNotify(0), commandCallback(nullptr), authCallback(nullptr) {
    pServerCallbacks = new MyServerCallbacks();
    pCharCallbacks = new MyCharacteristicCallbacks();
    pDescCallbacks = new MyDescriptorCallbacks();
}

// Initialize BLE
void BLE_Handler::init() {
  Serial.println("[BLE_Handler] Initializing BLE...");
  
  Serial.println("[BLE_DEBUG] -> BLEDevice::init");
  BLEDevice::init("BP_Device");
  
  Serial.println("[BLE_DEBUG] -> createServer");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks((BLEServerCallbacks*)pServerCallbacks);
  
  Serial.println("[BLE_DEBUG] -> createService(SERVICE_UUID)");
  BLEService* pService = pServer->createService(SERVICE_UUID);
  
  Serial.println("[BLE_DEBUG] -> createCharacteristic(Auth)");
  pCharAuth = pService->createCharacteristic(CHAR_AUTH_UUID, BLECharacteristic::PROPERTY_WRITE);
  pCharAuth->setCallbacks((BLECharacteristicCallbacks*)pCharCallbacks);
  
  pCharAuthStatus = pService->createCharacteristic(CHAR_AUTH_STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  uint8_t zero = 0;
  pCharAuthStatus->setValue(&zero, 1);
  pCharAuthStatus->addDescriptor(new BLE2902());

  Serial.println("[BLE_DEBUG] -> createCharacteristic(Command)");
  pCharStart = pService->createCharacteristic(CHAR_COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE);
  pCharStart->setCallbacks((BLECharacteristicCallbacks*)pCharCallbacks);

  Serial.println("[BLE_DEBUG] -> createCharacteristic(ABPM)");
  pCharABPM = pService->createCharacteristic(CHAR_ABPM_CONFIG_UUID, 
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharABPM->setCallbacks((BLECharacteristicCallbacks*)pCharCallbacks);
  uint8_t initABPM = 0; 
  pCharABPM->setValue(&initABPM, 1);

  Serial.println("[BLE_DEBUG] -> createCharacteristic(Status)");
  pCharStatus = pService->createCharacteristic(CHAR_STATUS_UUID, 
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  BLE2902* descStatus = new BLE2902();
  descStatus->setCallbacks((BLEDescriptorCallbacks*)pDescCallbacks);
  pCharStatus->addDescriptor(descStatus);
  uint8_t initState = IDLE;
  pCharStatus->setValue(&initState, 1);
  
  Serial.println("[BLE_DEBUG] -> createCharacteristic(LiveBP)");
  pCharLiveBP = pService->createCharacteristic(CHAR_LIVE_BP_UUID, 
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  BLE2902* descLive = new BLE2902();
  descLive->setCallbacks((BLEDescriptorCallbacks*)pDescCallbacks);
  pCharLiveBP->addDescriptor(descLive);
  uint16_t initPressure = 0;
  pCharLiveBP->setValue((uint8_t*)&initPressure, 2);
  
  Serial.println("[BLE_DEBUG] -> createCharacteristic(Result)");
  pCharResult = pService->createCharacteristic(CHAR_RESULT_UUID, 
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  BLE2902* descResult = new BLE2902();
  descResult->setCallbacks((BLEDescriptorCallbacks*)pDescCallbacks);
  pCharResult->addDescriptor(descResult);
  uint8_t initResult[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  pCharResult->setValue(initResult, 8);
  
  Serial.println("[BLE_DEBUG] -> createCharacteristic(History)");
  pCharHistory = pService->createCharacteristic(CHAR_HISTORY_UUID, 
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  BLE2902* descHistory = new BLE2902();
  descHistory->setCallbacks((BLEDescriptorCallbacks*)pDescCallbacks);
  pCharHistory->addDescriptor(descHistory);
  uint8_t emptyHistory[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  pCharHistory->setValue(emptyHistory, 8);
  
  Serial.println("[BLE_DEBUG] -> pService->start()");
  pService->start();
  
  Serial.println("[BLE_DEBUG] -> createService(TIME)");
  BLEService* pTimeService = pServer->createService(SERVICE_TIME_UUID);
  pCharTime = pTimeService->createCharacteristic(CHAR_TIME_UUID, 
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharTime->setCallbacks((BLECharacteristicCallbacks*)pCharCallbacks);
  pTimeService->start();
  
  Serial.println("[BLE_DEBUG] -> Configure Advertising");
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  
  Serial.println("[BLE_Handler] BLE Services initialized (Advertising OFF)");
}

// Connection handlers
void BLE_Handler::onConnect() {
  bleConnected = true;
  is_client_authorized = false;
  bleConnectTime = millis();
  
  if (pCharStatus) {
    uint8_t state = IDLE;
    pCharStatus->setValue(&state, 1);
    pCharStatus->notify();
  }
  
  if (pCharAuthStatus) {
    uint8_t authStatus = 0;
    pCharAuthStatus->setValue(&authStatus, 1);
    pCharAuthStatus->notify();
  }
  
  Serial.println("[BLE] Client connected - waiting for authentication (30 seconds)");
}

void BLE_Handler::onDisconnect() {
  bleConnected = false;
  is_client_authorized = false;
  Serial.println("[BLE] Client disconnected");
  // Advertising restart is now handled by the main loop/screen state
}

// Auth handler
void BLE_Handler::handleAuthWrite(std::string val) {
  Serial.print("[BLE_AUTH] Auth write received - Length: ");
  Serial.print(val.length());
  Serial.print(" bytes: ");
  for (size_t i = 0; i < val.length(); i++) {
    Serial.print("0x");
    Serial.print((uint8_t)val[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  // Validate two-byte password {'c', '5'} = {0x63, 0x35}
  if (val.length() == 2 && val[0] == 'c' && val[1] == '5') {
    is_client_authorized = true;
    Serial.println("[BLE_AUTH] ✓ Authentication PASSED - client authorized");
    Serial.println("[BLE_AUTH] AUTH characteristic will return 0x01 (PASS) on read");
    
    // Set AUTH characteristic value for immediate read feedback
    if (pCharAuth) {
      uint8_t authResult = 1;  // 0x01 = authenticated
      pCharAuth->setValue(&authResult, 1);
    }
    
    if (pCharAuthStatus) {
      uint8_t authStatus = 1;
      pCharAuthStatus->setValue(&authStatus, 1);
      pCharAuthStatus->notify();
      Serial.println("[BLE_AUTH] AUTH_STATUS notified: 0x01 (AUTHED)");
    }
    
    if (authCallback) {
      authCallback(true);
    }
  } else {
    // Set AUTH characteristic to indicate failure
    if (pCharAuth) {
      uint8_t authResult = 0;  // 0x00 = not authenticated
      pCharAuth->setValue(&authResult, 1);
      Serial.println("[BLE_AUTH] AUTH characteristic set to 0x00 (FAIL) for read");
    }
    
    Serial.print("[BLE_AUTH] ✗ Authentication FAILED - Expected: 0x63 0x35 ('c' '5'), Got: ");
    for (size_t i = 0; i < val.length(); i++) {
      Serial.print("0x");
      Serial.print((uint8_t)val[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    Serial.println("[BLE_AUTH] Disconnecting unauthorized client...");
    pServer->disconnect(pServer->getConnId());
    is_client_authorized = false;
    
    if (authCallback) {
      authCallback(false);
    }
  }
}

// Command handler
void BLE_Handler::handleCommandWrite(std::string charUUID, std::string val) {
  if (val.length() == 0) return;
  
  Serial.print("[BLE_CMD] Command write received - UUID: ");
  Serial.print(charUUID.c_str());
  Serial.print(", Length: ");
  Serial.print(val.length());
  Serial.print(" bytes: 0x");
  Serial.println((uint8_t)val[0], HEX);
  
  Serial.print("[BLE_CMD] DEBUG: is_client_authorized=");
  Serial.print(is_client_authorized);
  Serial.print(", bleConnected=");
  Serial.print(bleConnected);
  Serial.print(", CHAR_COMMAND_UUID=");
  Serial.print(CHAR_COMMAND_UUID);
  Serial.print(", charUUID=");
  Serial.println(charUUID.c_str());
  
  // Check authorization first
  if (!is_client_authorized) {
    Serial.print("[BLE_CMD] ✗ Access denied - client not authorized (is_client_authorized=");
    Serial.print(is_client_authorized);
    Serial.println(")");
    Serial.println("[BLE_CMD] ⚠ Authenticate first: Write 0x63 0x35 to AUTH characteristic");
    return;
  }
  
  Serial.println("[BLE_CMD] ✓ Authorization check PASSED - proceeding with command");
  
  uint8_t cmdValue = val[0];
  
  // Convert incoming UUID to uppercase for comparison
  std::string upperCharUUID = charUUID;
  std::transform(upperCharUUID.begin(), upperCharUUID.end(), upperCharUUID.begin(), ::toupper);
  
  // Single COMMAND characteristic with different values
  if (upperCharUUID == CHAR_COMMAND_UUID) {
    Serial.println("[BLE_CMD] ✓ UUID matches CHAR_COMMAND_UUID");
    switch(cmdValue) {
      case 0x03:
        lastCommand = CMD_RESET;
        Serial.println("[BLE_CMD] ✓ RESET command received (0x03) - Device will reboot");
        break;
      case 0x01:
        lastCommand = CMD_START;
        Serial.println("[BLE_CMD] ✓ START command received (0x01) - Beginning measurement");
        break;
      case 0x02:
        lastCommand = CMD_STOP;
        Serial.println("[BLE_CMD] ✓ STOP command received (0x02) - Aborting measurement");
        break;
      case 0x04:
        lastCommand = CMD_SYNC_HISTORY;
        Serial.println("[BLE_CMD] ✓ SYNC_HISTORY command received (0x04)");
        break;
      default:
        Serial.print("[BLE_CMD] ✗ Unknown command value: 0x");
        Serial.println(cmdValue, HEX);
        return;
    }
    
    // We don't invoke callback here anymore to avoid blocking the BLE task.
    // Instead, the Main Loop will pick up 'lastCommand' and execute it.
    Serial.println("[BLE_CMD] ✓ Command queued for Main Loop");
  } else {
    Serial.print("[BLE_CMD] ✗ UUID mismatch! Expected: ");
    Serial.print(CHAR_COMMAND_UUID);
    Serial.print(", Got: ");
    Serial.println(charUUID.c_str());
  }
}

// Status check methods
bool BLE_Handler::isConnected() {
  return bleConnected;
}

bool BLE_Handler::isAuthorized() {
  return is_client_authorized;
}

// Notification methods
void BLE_Handler::notifyStatus(uint8_t status) {
  if (bleConnected && pCharStatus) {
    pCharStatus->setValue(&status, 1);
    
    // Debug: Print status value
    const char* statusName = "UNKNOWN";
    switch(status) {
      case IDLE: statusName = "IDLE"; break;
      case INFLATING: statusName = "INFLATING"; break;
      case MEASURING: statusName = "MEASURING"; break;
      case WAITING: statusName = "WAITING"; break;
      case DEFLATING: statusName = "DEFLATING"; break;
      case FINISHED: statusName = "FINISHED"; break;
      case ERROR: statusName = "ERROR"; break;
      case DISCONNECTED: statusName = "DISCONNECTED"; break;
      case WAIT_FOR_START: statusName = "WAIT_FOR_START"; break;
    }
    Serial.print("[BLE_NOTIFY] STATUS: 0x");
    Serial.print(status, HEX);
    Serial.print(" (");
    Serial.print(statusName);
    Serial.println(")");
    
    pCharStatus->notify();
  }
}

void BLE_Handler::notifyLivePressure(uint16_t pressure) {
  if (bleConnected && is_client_authorized && pCharLiveBP && 
      (millis() - lastLiveBPNotify) >= 500) {
    pCharLiveBP->setValue((uint8_t*)&pressure, 2);
    
    // Debug: Print live pressure (little-endian uint16_t)
    Serial.print("[BLE_NOTIFY] LIVE_BP: ");
    Serial.print(pressure);
    Serial.print(" mmHg | Raw bytes: 0x");
    Serial.print((uint8_t)pressure, HEX);
    Serial.print(" 0x");
    Serial.println((uint8_t)(pressure >> 8), HEX);
    
    pCharLiveBP->notify();
    lastLiveBPNotify = millis();
  }
}

void BLE_Handler::notifyResults(uint8_t sbp, uint8_t dbp, uint8_t heartRate, uint8_t readingId) {
  if (bleConnected && is_client_authorized && pCharResult) {
    time_t now;
    time(&now);
    uint32_t ts = (uint32_t)now;

    uint8_t resultBytes[8];
    resultBytes[0] = sbp;
    resultBytes[1] = dbp;
    resultBytes[2] = heartRate;
    resultBytes[3] = readingId;
    // Pack timestamp (little-endian)
    resultBytes[4] = (uint8_t)(ts & 0xFF);
    resultBytes[5] = (uint8_t)((ts >> 8) & 0xFF);
    resultBytes[6] = (uint8_t)((ts >> 16) & 0xFF);
    resultBytes[7] = (uint8_t)((ts >> 24) & 0xFF);

    pCharResult->setValue(resultBytes, 8);
    
    // Debug: Print results with all bytes
    Serial.print("[BLE_NOTIFY] RESULTS: SBP=");
    Serial.print(sbp);
    Serial.print(" mmHg, DBP=");
    Serial.print(dbp);
    Serial.print(" mmHg, BPM=");
    Serial.print(heartRate);
    Serial.print(" | TS=");
    Serial.print(ts);
    Serial.print(" | Reading ID: ");
    Serial.print(readingId);
    Serial.print(" | Raw bytes: [0x");
    for(int i = 0; i < 8; i++) {
      Serial.print(resultBytes[i], HEX);
      if(i < 7) Serial.print(", 0x");
    }
    Serial.println("]");
    
    pCharResult->notify();
  }
}

void BLE_Handler::syncHistory() {
  if (bleConnected && pCharHistory) {
    int count = Storage::getHistoryCount();
    Serial.printf("[BLE] Syncing %d history records from flash...\n", count);
    
    if (count == 0) {
        Serial.println("[BLE] No records found to sync.");
    }
    
    for (int i = 0; i < count; i++) {
        uint8_t buffer[8];
        if (Storage::getReading(i, buffer)) {
            // Unpack timestamp for log
            uint32_t ts = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
            Serial.printf("[BLE] Sending history #%d: [%02X %02X %02X %02X] TS: %u\n", 
                          i, buffer[0], buffer[1], buffer[2], buffer[3], ts);
            pCharHistory->setValue(buffer, 8);
            pCharHistory->notify();
            delay(20); 
        } else {
            Serial.printf("[BLE] Error reading record #%d\n", i);
        }
    }
    
    // End of history marker (8 bytes of 0xFF)
    uint8_t eof[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    pCharHistory->setValue(eof, 8);
    pCharHistory->notify();
    
    Serial.println("[BLE] History sync complete.");
  }
}

// Callback registration
void BLE_Handler::setCommandCallback(BLE_CommandCallback_t callback) {
  commandCallback = callback;
}

void BLE_Handler::setAuthCallback(BLE_AuthCallback_t callback) {
  authCallback = callback;
}

// Command tracking
BLE_Handler::BLECommand BLE_Handler::getLastCommand() {
  return lastCommand;
}

void BLE_Handler::clearLastCommand() {
  lastCommand = static_cast<BLECommand>(0);
}

// Auth timeout check
void BLE_Handler::checkAuthTimeout() {
  if (bleConnected && !is_client_authorized) {
    if (millis() - bleConnectTime > AUTH_TIMEOUT_MS) {
      Serial.println("[BLE] ✗ Authentication timeout - disconnecting client");
      pServer->disconnect(pServer->getConnId());
      bleConnected = false;
      is_client_authorized = false;
    }
  }
}

void BLE_Handler::startAdvertising() {
  if (!bleConnected) {
    Serial.println("[BLE] Starting discovery (Advertising ON)");
    pServer->getAdvertising()->start();
  }
}

void BLE_Handler::stopAdvertising() {
  Serial.println("[BLE] Stopping discovery (Advertising OFF)");
  pServer->getAdvertising()->stop();
}

void BLE_Handler::disconnect() {
  if (pServer && bleConnected) {
    pServer->disconnect(pServer->getConnId());
    bleConnected = false;
    is_client_authorized = false;
  }
}

// Time Sync handler
void BLE_Handler::handleTimeWrite(std::string val) {
  if (val.length() >= 4) {
    uint32_t ts = (uint8_t)val[0] | ((uint8_t)val[1] << 8) | ((uint8_t)val[2] << 16) | ((uint8_t)val[3] << 24);
    
    struct timeval tv;
    tv.tv_sec = ts;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    
    // VERIFICATION: Print the human-readable time back to the console
    time_t now = tv.tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    Serial.printf("[BLE_TIME] ✓ Clock Synchronized: %s\n", buf);
    
    // Update ABPM Schedule so it doesn't trigger immediately due to "Time Jump"
    if (abpmMode) {
      ABPM_Manager::updateSchedule();
    }
    
    // Update characteristic value so it can be read back
    pCharTime->setValue((uint8_t*)&ts, 4);
  } else {
    Serial.println("[BLE_TIME] ✗ Invalid time sync write length");
  }
}
void BLE_Handler::handleABPMWrite(std::string val) {
    if (val.length() >= 5) {
        Serial.printf("[BLE_ABPM] Received %d bytes of config data\n", val.length());
        
        // Call Manager with raw bytes
        ABPM_Manager::setConfig((const uint8_t*)val.data(), val.length());
        
        // Update the characteristic so it can be read back
        pCharABPM->setValue((uint8_t*)val.data(), val.length());
    } else if (val.length() > 0 && (uint8_t)val[0] == 0x00) {
        // Special case: single 0x00 or similar to stop
        ABPM_Manager::setConfig((const uint8_t*)val.data(), val.length());
        pCharABPM->setValue((uint8_t*)val.data(), val.length());
    } else {
        Serial.printf("[BLE_ABPM] ✗ Error: Invalid configuration length (%d)\n", val.length());
    }
}
