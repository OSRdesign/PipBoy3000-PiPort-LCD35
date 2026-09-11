#pragma once
#include <Arduino.h>
#include "display.h" // I2C_SDA / I2C_SCL

// FT6336 touch, confirmed from Waveshare's official demo
// (Arduino/libraries/SensorLib/src/REG/FT6X36Constants.h) - unlike the 2.8"
// board's CST328, this runs on the same shared main I2C bus as everything
// else (touch/IMU/RTC/PMU/LCD-reset-expander), not a separate bus. No
// dedicated INT/RST line is broken out for it on this board, so we poll
// over I2C instead of using an interrupt (see touch.cpp).
#define TOUCH_ADDR      0x38

void Touch_Init();
// Returns true once per new touch-down event (edge-triggered, debounced).
bool Touch_Tapped();
// Same edge-triggered/debounced tap as Touch_Tapped(), but also reports the
// touch-down position already mapped into screen pixel coordinates
// (0..LCD_WIDTH-1, 0..LCD_HEIGHT-1). The FT6336 reports raw coordinates in
// the panel's native PORTRAIT orientation, not rotated to match this
// board's 480x320 landscape screen - the mapping in touch.cpp
// (screen_x = raw Y, screen_y = inverted raw X) was determined from live
// captures on real hardware (see DEVLOG.md), not the datasheet, so treat it
// as calibrated-by-observation rather than derived.
bool Touch_TappedAt(int16_t *outScreenX, int16_t *outScreenY);
// True if (px,py) falls within the rectangle (rx,ry,rw,rh) - for hit-
// testing an on-screen button against Touch_TappedAt()'s output.
bool Touch_PointInRect(int16_t px, int16_t py, int16_t rx, int16_t ry, int16_t rw, int16_t rh);
