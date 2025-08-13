#ifndef PLANETOPA_ERRORCODES_H
#define PLANETOPA_ERRORCODES_H
namespace planetopia {
namespace core {
enum class ErrorTypeDigit : uint8_t { GENERIC = 1,
                                      SENSOR = 2,
                                      COMM = 3,
                                      MEMORY = 4,
                                      HARDWARE = 5,
                                      CONFIG = 6 };
enum class ModuleDigit : uint8_t { CORE = 1,
                                   ADAPTER = 2,
                                   MESH = 3,
                                   EEPROM = 4,
                                   HW = 5 };
constexpr uint16_t makeErrorCode(ErrorTypeDigit t, ModuleDigit m, uint8_t sub) {
  return (static_cast<uint16_t>(t) << 8) | (static_cast<uint16_t>(m) << 4) | (sub & 0x0F);
}
}
}
#endif
