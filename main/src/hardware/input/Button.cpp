#include "Button.h"
#include <Arduino.h>
#include <cstdint>

namespace planetopia {
namespace hardware {

Button::Button(uint8_t pin)
  : GpioInput(pin) {}

bool Button::isPressed() {
  // Active LOW (assumes INPUT_PULLUP)
  return digitalRead(_pin) == LOW;
}

bool Button::waitForHold(uint32_t ms) {
  uint32_t start = millis();
  if (!isPressed()) return false;
  while (isPressed()) {
    if (static_cast<uint32_t>(millis() - start) >= ms) return true;
    delay(10);  // debounce, yield to RTOS
  }
  return false;
}

}  // namespace hardware
}  // namespace planetopia
