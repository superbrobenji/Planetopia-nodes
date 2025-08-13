#ifndef PLANETOPA_ERROR_H
#define PLANETOPA_ERROR_H

#include "ErrorCodes.h"
#include "ErrorCore.h"
#include "../core/Logger.h"
#include <esp_err.h>
#include <cstdint>

namespace planetopia {
namespace err {

inline bool fail(utils::ErrorType type, const char* msg) {
  utils::Logger::logln("ERROR", msg, utils::LogLevel::LOG_ERROR);
  utils::ErrorCore::getInstance().signalError(type, msg);
  return false;
}
[[noreturn]] inline void fatal(utils::ErrorType type, const char* msg) {
  utils::Logger::logln("FATAL", msg, utils::LogLevel::LOG_ERROR);
  utils::ErrorCore::getInstance().signalError(type, msg);
  while (true) {}
}
inline bool check(bool condition, utils::ErrorType type, const char* msg) {
  return condition ? true : fail(type, msg);
}
inline bool checkEsp(esp_err_t status, utils::ErrorType type, const char* msg) {
  if (status == ESP_OK) return true;
  utils::Logger::logln("ESP", String(msg) + ": " + esp_err_to_name(status), utils::LogLevel::LOG_ERROR);
  return fail(type, msg);
}
}
}
#define ERROR_ASSERT(cond, msg) planetopia::err::check((cond), utils::ErrorType::CONFIG_ERROR, (msg))
#define ERROR_CHECK(cond, t, msg) planetopia::err::check((cond), (t), (msg))
#define ERROR_CHECK_ESP_OK(expr, t, m) planetopia::err::checkEsp((expr), (t), (m))

#endif
