#include "gfx.h"
#include "font_pipboy.h"
#include <esp_heap_caps.h>

static uint16_t scaleColor565(uint16_t color, uint8_t v) {
  uint16_t r5 = (color >> 11) & 0x1F, g6 = (color >> 5) & 0x3F, b5 = color & 0x1F;
  r5 = (uint16_t)(r5 * v) / 255;
  g6 = (uint16_t)(g6 * v) / 255;
  b5 = (uint16_t)(b5 * v) / 255;
  return (r5 << 11) | (g6 << 5) | b5;
}

static uint16_t *s_fb = nullptr;

void GFX_Init() {
  s_fb = (uint16_t *)heap_caps_malloc((size_t)LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_SPIRAM);
  GFX_Clear(PIP_BLACK);
}

void GFX_Clear(uint16_t color) {
  for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) s_fb[i] = color;
}

void GFX_SetPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  s_fb[y * LCD_WIDTH + x] = color;
}

void GFX_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  for (int16_t j = y; j < y + h; j++)
    for (int16_t i = x; i < x + w; i++)
      GFX_SetPixel(i, j, color);
}

void GFX_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  GFX_HLine(x, y, w, color);
  GFX_HLine(x, y + h - 1, w, color);
  GFX_VLine(x, y, h, color);
  GFX_VLine(x + w - 1, y, h, color);
}

void GFX_HLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  for (int16_t i = 0; i < w; i++) GFX_SetPixel(x + i, y, color);
}

void GFX_VLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  for (int16_t i = 0; i < h; i++) GFX_SetPixel(x, y + i, color);
}

void GFX_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  while (true) {
    GFX_SetPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void GFX_DrawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  int16_t x = r, y = 0, err = 0;
  while (x >= y) {
    GFX_SetPixel(cx + x, cy + y, color); GFX_SetPixel(cx + y, cy + x, color);
    GFX_SetPixel(cx - y, cy + x, color); GFX_SetPixel(cx - x, cy + y, color);
    GFX_SetPixel(cx - x, cy - y, color); GFX_SetPixel(cx - y, cy - x, color);
    GFX_SetPixel(cx + y, cy - x, color); GFX_SetPixel(cx + x, cy - y, color);
    y += 1;
    err += 1 + 2 * y;
    if (2 * (err - x) + 1 > 0) { x -= 1; err += 1 - 2 * x; }
  }
}

void GFX_FillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  for (int16_t y = -r; y <= r; y++)
    for (int16_t x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) GFX_SetPixel(cx + x, cy + y, color);
}

void GFX_DrawChar(int16_t x, int16_t y, char c, uint16_t color, uint8_t scale) {
  if ((uint8_t)c > 127) c = '?';
  const uint8_t *glyph = FONT_PIPBOY[(uint8_t)c];
  for (int8_t row = 0; row < 8; row++) {
    for (int8_t col = 0; col < 8; col++) {
      uint8_t v = glyph[row * 8 + col];
      if (v < 8) continue; // near-black, leave background showing through
      uint16_t px = scaleColor565(color, v);
      if (scale == 1) {
        GFX_SetPixel(x + col, y + row, px);
      } else {
        GFX_FillRect(x + col * scale, y + row * scale, scale, scale, px);
      }
    }
  }
}

void GFX_DrawString(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t scale) {
  int16_t cx = x;
  while (*s) {
    GFX_DrawChar(cx, y, *s, color, scale);
    cx += 8 * scale;
    s++;
  }
}

int16_t GFX_StringWidth(const char *s, uint8_t scale) {
  return (int16_t)strlen(s) * 8 * scale;
}

void GFX_BlitMono(int16_t x, int16_t y, const uint8_t *data, int16_t w, int16_t h, uint8_t scale,
                   uint8_t brightness) {
  // Continuous grayscale->green shading (not a hard threshold) so the
  // source art's anti-aliased edges stay smooth instead of blocky.
  for (int16_t sy = 0; sy < h; sy++) {
    for (int16_t sx = 0; sx < w; sx++) {
      uint8_t v = data[sy * w + sx];
      if (brightness != 255) v = (uint8_t)(((uint16_t)v * brightness) / 255);
      if (v < 4) continue; // near-black, leave background showing through
      uint8_t rr = (uint8_t)((70 * (uint16_t)v) / 255);
      uint8_t bb = (uint8_t)((110 * (uint16_t)v) / 255);
      uint16_t color = RGB565(rr, v, bb);
      if (scale == 1) {
        GFX_SetPixel(x + sx, y + sy, color);
      } else {
        GFX_FillRect(x + sx * scale, y + sy * scale, scale, scale, color);
      }
    }
  }
}

void GFX_Present() {
  Display_Push(s_fb);
}
