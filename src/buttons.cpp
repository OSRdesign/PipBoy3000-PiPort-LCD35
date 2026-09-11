#include "buttons.h"

static bool s_wasPressed = false;
static uint32_t s_lastTapMs = 0;

void Buttons_Init() {
  pinMode(PIN_BOOT_KEY, INPUT_PULLUP);
}

bool Buttons_BootTapped() {
  bool pressed = digitalRead(PIN_BOOT_KEY) == LOW; // active-low, like the power button
  uint32_t now = millis();
  if (pressed) {
    if (!s_wasPressed && (now - s_lastTapMs) > 250) {
      s_wasPressed = true;
      s_lastTapMs = now;
      return true;
    }
  } else {
    s_wasPressed = false;
  }
  return false;
}
