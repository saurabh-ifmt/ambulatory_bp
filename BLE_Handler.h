#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "Constants.h"

// BLE Event Callback Function Types
typedef void (*BLE_AuthCallback_t)(bool authorized);
typedef void (*BLE_CommandCallback_t)(uint8_t command);

// BLE Handler Class - Encapsulates all BLE communication
class BLE_Handler {
public:
  enum BLECommand {
    CMD_START = 1,
    CMD_STOP = 2,
    CMD_RESET = 3,
    CMD_SYNC_HISTORY = 4
  };

  // Singleton pattern
  static BLE_Handler& getInstance();

  // Initialize BLE and advertising
  void init();

  // Check connection status
  bool isConnected();

  // Check authorization status
  bool isAuthorized();

  // Notify device status change
  void notifyStatus(uint8_t status);

  // Notify live pressure update
  void notifyLivePressure(uint16_t pressure);

  // Notify measurement results
  void notifyResults(uint8_t sbp, uint8_t dbp, uint8_t heartRate, uint8_t readingId);

  // Sync entire flash history to client
  void syncHistory();

  // Register command callback
  void setCommandCallback(BLE_CommandCallback_t callback);

  // Register auth callback
  void setAuthCallback(BLE_AuthCallback_t callback);

  // Get last received command
  BLECommand getLastCommand();

  // Clear last command
  void clearLastCommand();

  // Disconnect current client
  void disconnect();
  
  // Check and handle authentication timeout
  void checkAuthTimeout();

  // Manage advertising based on screen state
  void startAdvertising();
  void stopAdvertising();

  // Get BLE server instance (for advanced operations)
  BLEServer* getServer();

private:
  BLE_Handler();  // Private constructor (singleton)
  ~BLE_Handler() = default;
 
  static BLE_Handler* instance;

  BLEServer* pServer;
  BLECharacteristic* pCharStart;        // Single COMMAND characteristic (0x00=RESET, 0x01=START, 0x02=STOP)
  BLECharacteristic* pCharStatus;       // 1 byte: state
  BLECharacteristic* pCharLiveBP;       // 2 bytes: pressure (uint16_t)
  BLECharacteristic* pCharResult;       // 8 bytes: SBP|DBP|HR|ID|TIMESTAMP
  BLECharacteristic* pCharAuth;         // 2 bytes: password write
  BLECharacteristic* pCharAuthStatus;   // 1 byte: auth status (0=not authed, 1=authed)
  BLECharacteristic* pCharHistory;      // History dump (8-byte packets)
  BLECharacteristic* pCharTime;         // 4 bytes: Unix timestamp sync
  BLECharacteristic* pCharABPM;         // 3 bytes: Mode|DayInt|NightInt

  volatile bool bleConnected;
  volatile bool is_client_authorized;
  volatile uint32_t bleConnectTime;
  volatile BLECommand lastCommand;

  uint32_t lastLiveBPNotify;

  BLE_CommandCallback_t commandCallback;
  BLE_AuthCallback_t authCallback;

  // Callback helper methods (called by BLE callbacks)
  void handleAuthWrite(std::string val);
  void handleCommandWrite(std::string charUUID, std::string val);
  void onConnect();
  void onDisconnect();
  void handleTimeWrite(std::string val);
  void handleABPMWrite(std::string val);

  // Persistent callback instances to save heap
  void* pServerCallbacks;
  void* pCharCallbacks;
  void* pDescCallbacks;

  friend class MyServerCallbacks;
  friend class MyCharacteristicCallbacks;
};

#endif
