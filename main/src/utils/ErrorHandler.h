#ifndef ERRORHANDLER_H
#define ERRORHANDLER_H

#include "src/hardware/Led.h"

namespace planetopia {
namespace utils {

enum class ErrorType {
  GENERIC,
  SENSOR_FAIL,
  COMMUNICATION_FAIL,
  MEMORY_ERROR,
  CONFIG_ERROR,
  HARDWARE_FAILURE,
  // ...add as needed
};

class ErrorHandler {
public:
  static ErrorHandler& getInstance();

  void init(hardware::Led* errorLed);

  void signalError(ErrorType errorType, const char* message = nullptr);

  // Prevent copying
  ErrorHandler(const ErrorHandler&) = delete;
  ErrorHandler& operator=(const ErrorHandler&) = delete;

private:
  ErrorHandler();
  hardware::Led* _errorLed;
  bool _initialized;

  void blinkPattern(ErrorType errorType);
  bool shouldRestart(ErrorType errorType) const;
  void restartDevice();
};

}  // namespace utils
}  // namespace planetopia
#endif