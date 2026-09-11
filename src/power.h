#pragma once
#include <Arduino.h>

// Unlike the 2.8" board (a bare battery-ADC pin + a power-hold latch GPIO),
// this board's power tree is entirely managed by an AXP2101 PMU chip on the
// shared main I2C bus - it supplies every rail (including the ones the LCD
// and audio codec run on) and owns the physical power button, reporting
// short/long presses as I2C IRQ flags instead of a raw GPIO edge. See
// power.cpp and Waveshare's official demo
// (Arduino/examples/02_axp2101_example/02_axp2101_example.ino).

enum PressEvent { PRESS_NONE, PRESS_SHORT, PRESS_LONG };

void Power_Init();
float Power_GetVolts();
uint8_t Power_GetPercent(); // AXP2101 fuel-gauge battery percent
// Polls the AXP2101 power-key IRQ flags; fires PRESS_SHORT once on a short
// press, or PRESS_LONG once on a long press (never both for the same
// press) - mirrors the chip's own short/long classification rather than a
// hand-timed GPIO hold, since the PMU already debounces and classifies this
// for us.
PressEvent Power_PollKey();
