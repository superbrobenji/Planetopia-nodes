#include "src/Mesh/Mesh.h"
#include "src/Adapter/AdapterFactory.h"
#include "src/utils/Logger.h"
#include "src/hardware/output/Led.h"
#include "src/hardware/input/Button.h"
#include "src/utils/ErrorHandler.h"
#include <EEPROM.h>
#include <esp_wifi.h>
#include <esp_wifi.h>

constexpr int EEPROM_SIZE = 128;
constexpr int MASTER_FLAG_ADDR = 0;                        // Use address 0 for the master flag
constexpr unsigned long MASTER_BEACON_INTERVAL_MS = 2000;  // 2 seconds

using namespace planetopia::utils;
// Avoid 'mesh' ambiguity by not importing the namespace
using namespace planetopia::adapter;
using namespace planetopia::hardware;

// Pins
constexpr int RED_LED_PIN = 33;
constexpr int GREEN_LED_PIN = 26;
constexpr int CONFIG_BUTTON_PIN = 32;

constexpr unsigned long BUTTON_HOLD_TIME_MS = 5000;  // 5 seconds

Led greenLed(GREEN_LED_PIN);
Led redLed(RED_LED_PIN);
Button configButton(CONFIG_BUTTON_PIN);

planetopia::mesh::Mesh mesh;
planetopia::mesh::mesh_message transmissionMessage;

Adapter* adapter = nullptr;

//define all known MAC addresses for your mesh (update with your real MACs!)
const uint8_t defaultPeerList[][6] = {
  { 0xEC, 0x64, 0xC9, 0x5D, 0xAC, 0x18 },
  { 0xEC, 0x64, 0xC9, 0x5D, 0x22, 0x20 },
  // Add all known node MACs, including THIS node's MAC!
};
constexpr int NUM_DEFAULT_PEERS = sizeof(defaultPeerList) / 6 / sizeof(uint8_t);

// --- Serial control opcodes for SERIAL_ADAPTER (data[0]) ---
static constexpr uint8_t OP_CONFIG_SET = 0xA0;     // [A0][6B targetMac][1B adapterType][1B optPin]
static constexpr uint8_t OP_HEALTH_REQ = 0xB0;     // [B0]
static constexpr uint8_t OP_HEALTH_REPORT = 0xB1;  // [B1][1B adapterType][6B mac][4B uptimeSec]

static inline void getOwnMac(uint8_t out[6]) {
  esp_wifi_get_mac(WIFI_IF_STA, out);
}

static inline void sendHealthReport() {
  uint8_t data[12] = { 0 };
  data[0] = OP_HEALTH_REPORT;
  data[1] = static_cast<uint8_t>(adapter ? adapter->getAdapterType() : UNKNOWN_ADAPTER);
  uint8_t mac[6];
  getOwnMac(mac);
  memcpy(&data[2], mac, 6);
  uint32_t uptimeSec = millis() / 1000;
  data[8] = static_cast<uint8_t>(uptimeSec & 0xFF);
  data[9] = static_cast<uint8_t>((uptimeSec >> 8) & 0xFF);
  data[10] = static_cast<uint8_t>((uptimeSec >> 16) & 0xFF);
  data[11] = static_cast<uint8_t>((uptimeSec >> 24) & 0xFF);
  planetopia::mesh::Mesh::transmit(SERIAL_ADAPTER, data);
}

// Keep main thin; adapter handles health/config

bool eepromHasPeers() {
  EEPROM.begin(EEPROM_SIZE);
  bool hasPeers = false;
  for (int i = 0; i < NUM_DEFAULT_PEERS; ++i) {
    bool found = false;
    for (int j = 0; j < 6; ++j) {
      if (EEPROM.read(16 + i * 6 + j) != 0xFF) {
        found = true;
        break;
      }
    }
    if (found) {
      hasPeers = true;
      break;
    }
  }
  EEPROM.end();
  return hasPeers;
}

void writeDefaultPeersToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  // Wipe first
  for (int i = 0; i < planetopia::mesh::MAX_PEERS * 6; ++i) {
    EEPROM.write(16 + i, 0xFF);
  }
  for (int i = 0; i < NUM_DEFAULT_PEERS; ++i) {
    for (int j = 0; j < 6; ++j) {
      EEPROM.write(16 + i * 6 + j, defaultPeerList[i][j]);
    }
  }
  EEPROM.commit();
  EEPROM.end();
}

bool loadMasterFlagFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t flag = EEPROM.read(MASTER_FLAG_ADDR);
  EEPROM.end();
  return (flag == 1);  // 1 = master, 0 = not master
}

void saveMasterFlagToEEPROM(bool isMaster) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(MASTER_FLAG_ADDR, isMaster ? 1 : 0);
  EEPROM.commit();
  EEPROM.end();
}

void dataRecvCallback(planetopia::mesh::mesh_message message) {
  Logger::logln("MESH", "Data received callback triggered", LogLevel::LOG_DEBUG);
  // Ensure all nodes can handle SERIAL control messages even if they don't use Serial_Adapter
  if (message.dataType == SERIAL_ADAPTER) {
    uint8_t op = message.data[0];
    if (op == OP_CONFIG_SET) {
      uint8_t myMac[6];
      getOwnMac(myMac);
      bool targetIsBroadcast = true;
      for (int i = 0; i < 6; ++i)
        if (message.data[1 + i] != 0xFF) {
          targetIsBroadcast = false;
          break;
        }
      bool isTarget = targetIsBroadcast || (memcmp(&message.data[1], myMac, 6) == 0);
      if (isTarget) {
        // Apply configuration change
        adapter_types newType = static_cast<adapter_types>(static_cast<int8_t>(message.data[7]));
        planetopia::adapter::AdapterFactory::saveAdapterTypeToEEPROM(newType);
        // Pin is automatically inferred from adapter type - no need to store it

        // Create new adapter with correct pin for the new type
        Adapter* newAdapter = planetopia::adapter::AdapterFactory::createFromEEPROM();
        if (newAdapter) {
          if (adapter) {
            delete adapter;
            adapter = nullptr;
          }
          adapter = newAdapter;
          adapter->setTransmitFn(&planetopia::mesh::Mesh::transmit);
          if (!adapter->init()) {
            Logger::logln("MAIN", "New adapter failed to initialize", LogLevel::LOG_ERROR);
          } else {
            Logger::logln("MAIN", "Adapter switched via CONFIG_SET", LogLevel::LOG_INFO);
          }
          sendHealthReport();
        }
      }
    } else if (op == OP_HEALTH_REQ) {
      sendHealthReport();
    }
  }
  if (adapter) {
    adapter->onMeshData(message);
  }
  greenLed.blink(2, 100, 100);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Planetopia Starting...");
  Logger::setLogLevel(LogLevel::LOG_DEBUG);  // Set global log level at boot

  Logger::logln("MAIN", "Logger initialized", LogLevel::LOG_INFO);

  Led::setSystemErrorLed(&redLed);

  if (!redLed.init()) {
    Logger::logln("MAIN", "FATAL: Failed to initialize red LED!", LogLevel::LOG_ERROR);
    if (greenLed.init()) {
      while (true) {
        greenLed.blink(6, 100, 100);
        delay(1000);
      }
    } else {
      Logger::logln("MAIN", "FATAL: No LEDs available. System halted.", LogLevel::LOG_ERROR);
      while (true) {
        delay(1000);
      }
    }
  }

  ErrorHandler::getInstance().init(&redLed);

  if (!greenLed.isInitialized()) {
    if (!greenLed.init()) {
      Logger::logln("MAIN", "FATAL: Failed to initialize green LED!", LogLevel::LOG_ERROR);
      ErrorHandler::getInstance().signalError(
        ErrorType::HARDWARE_FAILURE,
        "MAIN: Failed to initialize green LED");
      while (true) {
        delay(1000);
      }
    }
  }

  if (!configButton.init()) {
    Logger::error("Config button initialization failed!");
    ErrorHandler::getInstance().signalError(ErrorType::HARDWARE_FAILURE, "Config button init failed!");
  }

  // Declare peers to EEPROM (only if empty)
  if (!eepromHasPeers()) {
    writeDefaultPeersToEEPROM();
    Logger::logln("MAIN", "Wrote default peer MACs to EEPROM.", LogLevel::LOG_INFO);
  }

  // Initialize EEPROM defaults if not set
  planetopia::adapter::AdapterFactory::initializeDefaultsIfUnset();

  // Create adapter from EEPROM (will automatically use correct pin for adapter type)
  adapter = planetopia::adapter::AdapterFactory::createFromEEPROM();
  if (!adapter) {
    Logger::logln("MAIN", "Failed to create adapter", LogLevel::LOG_ERROR);
    ErrorHandler::getInstance().signalError(
      ErrorType::HARDWARE_FAILURE,
      "MAIN: Failed to create PIR adapter");
    while (true) {
      redLed.blink(3, 150, 150);
      delay(800);
    }
  }
  Logger::logln("MAIN", "Adapter created", LogLevel::LOG_INFO);

  if (!adapter->init()) {
    Logger::logln("MAIN", "Adapter failed to initialize", LogLevel::LOG_ERROR);
    ErrorHandler::getInstance().signalError(
      ErrorType::HARDWARE_FAILURE,
      "MAIN: Adapter failed to initialize");
    while (true) {
      redLed.blink(6, 100, 100);
      delay(1000);
    }
  }
  Logger::logln("MAIN", "Adapter initialized", LogLevel::LOG_INFO);

  if (!mesh.init()) {
    Logger::logln("MESH", "Mesh failed to initialize", LogLevel::LOG_ERROR);
    ErrorHandler::getInstance().signalError(
      ErrorType::COMMUNICATION_FAIL,
      "MAIN: Mesh failed to initialize");
    while (true) {
      redLed.blink(3, 150, 150);
      delay(800);
    }
  }
  bool isMaster = loadMasterFlagFromEEPROM();
  mesh.setIsMaster(isMaster);
  Logger::logln("MESH", "Mesh initialized", LogLevel::LOG_INFO);
  Logger::logln("MAIN", String("Booted as: ") + (isMaster ? "MASTER" : "NODE"), LogLevel::LOG_INFO);

  adapter->setTransmitFn(&planetopia::mesh::Mesh::transmit);

  mesh.linkDataRecvCallback(dataRecvCallback);
  greenLed.blink(2, 200, 200);
  redLed.blink(2, 200, 200);
}

void loop() {
  if (mesh.getIsMaster()) {
    mesh.broadcastMasterBeacon();
  }

  if (adapter) {
    adapter->loop();
  }

  static bool buttonWasPressed = false;
  static unsigned long holdStart = 0;

  if (configButton.isPressed()) {
    if (!buttonWasPressed) {
      // Just started pressing
      buttonWasPressed = true;
      holdStart = millis();
    } else {
      // Already holding, check duration
      if (millis() - holdStart >= BUTTON_HOLD_TIME_MS) {
        // Toggle master flag!
        bool wasMaster = loadMasterFlagFromEEPROM();
        bool newMaster = !wasMaster;
        saveMasterFlagToEEPROM(newMaster);
        Logger::logln("MAIN", String("Button held 5s: CONFIG TOGGLED. Now ") + (newMaster ? "MASTER" : "NODE"), LogLevel::LOG_INFO);
        Logger::logln("MAIN", "Restarting in 2 seconds for new role...", LogLevel::LOG_INFO);
        if (newMaster) {
          greenLed.blink(3, 200, 200);
        } else {
          greenLed.blink(2, 200, 200);
        }
        delay(2000);
        ESP.restart();
      }
    }
  } else {
    buttonWasPressed = false;  // Reset state if released
  }
  // Periodic health report
  static unsigned long lastHealth = 0;
  if (millis() - lastHealth > 5000) {
    lastHealth = millis();
    sendHealthReport();
  }
}
