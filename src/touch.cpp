#include "touch.h"
#include <Wire.h>

// FT6336 registers, from Waveshare's official demo (SensorLib's
// FT6X36Constants.h). REG_STATUS low nibble is the number of touch points;
// the touch-1 X/Y registers immediately follow it (0x03-0x06), so one burst
// read from REG_STATUS covers all five bytes we need.
#define REG_STATUS  0x02

// Native (portrait, unrotated) raw coordinate ranges this panel's FT6336
// reports - inferred from live captures topping out at X=298, Y=441 (see
// DEVLOG.md), rounded to the panel's actual native resolution (320x480)
// rather than pinned to the exact observed max.
#define TOUCH_RAW_X_MAX 319
#define TOUCH_RAW_Y_MAX 479

static uint32_t s_lastTapMs = 0;
static bool s_wasDown = false;

void Touch_Init() {
  Wire.begin(I2C_SDA, I2C_SCL);
}

bool Touch_TappedAt(int16_t *outScreenX, int16_t *outScreenY) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(REG_STATUS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_ADDR, 5) != 5) return false;
  uint8_t status = Wire.read();
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  uint8_t points = status & 0x0F;

  bool down = (points > 0 && points <= 2);
  bool tapped = false;
  if (down && !s_wasDown) {
    uint32_t now = millis();
    if (now - s_lastTapMs >= 350) { // debounce
      s_lastTapMs = now;
      tapped = true;
      if (outScreenX || outScreenY) {
        int16_t rawX = ((xh & 0x0F) << 8) | xl;
        int16_t rawY = ((yh & 0x0F) << 8) | yl;
        // screen_x = raw Y (direct), screen_y = raw X (inverted) - see
        // TOUCH_RAW_X_MAX/TOUCH_RAW_Y_MAX above for where these come from.
        int32_t sx = (int32_t)rawY * (LCD_WIDTH - 1) / TOUCH_RAW_Y_MAX;
        int32_t sy = (int32_t)(TOUCH_RAW_X_MAX - rawX) * (LCD_HEIGHT - 1) / TOUCH_RAW_X_MAX;
        if (sx < 0) sx = 0; if (sx > LCD_WIDTH - 1) sx = LCD_WIDTH - 1;
        if (sy < 0) sy = 0; if (sy > LCD_HEIGHT - 1) sy = LCD_HEIGHT - 1;
        if (outScreenX) *outScreenX = (int16_t)sx;
        if (outScreenY) *outScreenY = (int16_t)sy;
      }
    }
  }
  s_wasDown = down;
  return tapped;
}

bool Touch_Tapped() {
  return Touch_TappedAt(nullptr, nullptr);
}

bool Touch_PointInRect(int16_t px, int16_t py, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}
