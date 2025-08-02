#define DEBUG
#include "Logger.h"

namespace planetopia {
namespace utils {

void Logger::log(const String& tag, const String& message) {
#ifdef DEBUG
  Serial.print("[");
  Serial.print(tag);
  Serial.print("] ");
  Serial.print(message);
#endif
}

void Logger::logln(const String& tag, const String& message) {
  log(tag, message);
#ifdef DEBUG
  Serial.println();
#endif
}

void Logger::error(const char* fmt, ...) {
#ifdef DEBUG
  va_list args;
  va_start(args, fmt);
  Serial.print("[ERROR] ");
  Serial.vprintf(fmt, args);
  Serial.println();
  va_end(args);
#endif
}

}
}
