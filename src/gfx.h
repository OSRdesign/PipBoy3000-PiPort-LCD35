#pragma once
#include <Arduino.h>
#include "display.h"

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

#define PIP_BLACK     0x0000
#define PIP_GREEN     RGB565(70, 255, 110)
#define PIP_GREEN_DIM RGB565(20, 100, 45)

void GFX_Init();
void GFX_Clear(uint16_t color);
void GFX_SetPixel(int16_t x, int16_t y, uint16_t color);
void GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void GFX_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void GFX_HLine(int16_t x, int16_t y, int16_t w, uint16_t color);
void GFX_VLine(int16_t x, int16_t y, int16_t h, uint16_t color);
void GFX_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void GFX_DrawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void GFX_FillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void GFX_DrawChar(int16_t x, int16_t y, char c, uint16_t color, uint8_t scale);
void GFX_DrawString(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t scale);
int16_t GFX_StringWidth(const char *s, uint8_t scale);
// Blits an 8-bit intensity sprite (0-255), thresholded to the 2-tone
// green palette, nearest-neighbor scaled by an integer factor. `brightness`
// (0-255, default full) scales every pixel's intensity first, for a
// pulsing/dimming effect without needing separate pre-rendered frames.
void GFX_BlitMono(int16_t x, int16_t y, const uint8_t *data, int16_t w, int16_t h, uint8_t scale,
                   uint8_t brightness = 255);
void GFX_Present();
