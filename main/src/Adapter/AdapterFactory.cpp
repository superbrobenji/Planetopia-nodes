#include "AdapterFactory.h"
#include "src/utils/Logger.h"
#include "src/utils/ErrorHandler.h"
#include <EEPROM.h>
// Include all adapter headers
#include "src/Adapter/PIR_Adapter/PIR_Adapter.h"
#include "src/Adapter/Serial_Adapter/Serial_Adapter.h"

namespace planetopia {
namespace adapter {

using namespace planetopia::utils;

Adapter* AdapterFactory::createAdapter(adapter_types type, int pin) {
  switch (type) {
    case PIR_ADAPTER:
      Logger::logln("Factory", "Creating PIR_Adapter", LogLevel::LOG_INFO);
      return new PIR_Adapter(pin);

    case SERIAL_ADAPTER:
      Logger::logln("Factory", "Creating Serial_Adapter", LogLevel::LOG_INFO);
      return new Serial_Adapter(pin);

      // case WIFI_ADAPTER:
      //   Logger::logln("Factory", "Creating WiFi_Adapter", LogLevel::LOG_INFO);
      //   return new WiFi_Adapter(pin);

      // case LED_ADAPTER:
      //   Logger::logln("Factory", "Creating LED_Adapter", LogLevel::LOG_INFO);
      //   return new LED_Adapter(pin);

    default:
      ErrorHandler::getInstance().signalError(
        ErrorType::CONFIG_ERROR,
        "AdapterFactory: Unknown adapter type");
      Logger::logln("Factory", "Error: Unknown adapter type", LogLevel::LOG_ERROR);
      return nullptr;
  }
}

static constexpr int EEPROM_SIZE_FACTORY = 128;
static constexpr int ADAPTER_TYPE_ADDR_FACTORY = 8;

adapter_types AdapterFactory::loadAdapterTypeFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE_FACTORY);
  uint8_t v = EEPROM.read(ADAPTER_TYPE_ADDR_FACTORY);
  EEPROM.end();
  if (v == 0xFF) return PIR_ADAPTER;  // default
  return static_cast<adapter_types>(static_cast<int8_t>(v));
}

void AdapterFactory::saveAdapterTypeToEEPROM(adapter_types type) {
  EEPROM.begin(EEPROM_SIZE_FACTORY);
  EEPROM.write(ADAPTER_TYPE_ADDR_FACTORY, static_cast<uint8_t>(static_cast<int8_t>(type)));
  EEPROM.commit();
  EEPROM.end();
}

Adapter* AdapterFactory::createFromEEPROM() {
  adapter_types type = loadAdapterTypeFromEEPROM();
  int pin = getDefaultPinForAdapter(type);
  return createAdapter(type, pin);
}

void AdapterFactory::initializeDefaultsIfUnset() {
  // If adapter type unset (0xFF), set default PIR
  EEPROM.begin(EEPROM_SIZE_FACTORY);
  uint8_t v = EEPROM.read(ADAPTER_TYPE_ADDR_FACTORY);
  if (v == 0xFF) {
    EEPROM.write(ADAPTER_TYPE_ADDR_FACTORY, static_cast<uint8_t>(static_cast<int8_t>(PIR_ADAPTER)));
    EEPROM.commit();
  }
  EEPROM.end();
}

int AdapterFactory::getDefaultPinForAdapter(adapter_types type) {
  switch (type) {
    case PIR_ADAPTER:
      return PIR_ADAPTER_DEFAULT_PIN;
    case WIFI_ADAPTER:
      return WIFI_ADAPTER_DEFAULT_PIN;
    case LED_ADAPTER:
      return LED_ADAPTER_DEFAULT_PIN;
    case SERIAL_ADAPTER:
      return SERIAL_ADAPTER_DEFAULT_PIN;
    default:
      return PIR_ADAPTER_DEFAULT_PIN;  // fallback
  }
}

}
}
