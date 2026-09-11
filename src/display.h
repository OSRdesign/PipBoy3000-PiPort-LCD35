#pragma once
#include <Arduino.h>

// Pin map confirmed from Waveshare's own official demo package
// (files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5-Demo.zip,
// Arduino/examples/08_gfx_helloworld/08_gfx_helloworld.ino) for the ST7796 SPI panel.
//
// Unlike the 2.8" board, this display's RST line is not wired to a bare
// GPIO - it goes through the TCA9554 I2C GPIO expander (shared with the rest
// of the board's I2C peripherals), pin P1. CS is tied permanently low (this
// is the only SPI device on the bus), hence PIN_LCD_CS = -1.
//
// The panel is native 320(w) x 480(h) portrait; rotated 90deg via MADCTL
// (see display.cpp) it becomes 480x320 landscape. The logical framebuffer
// now matches the full physical panel (the 2.8" fork's UI was originally
// letterboxed at 320x240 here too, before every screen in ui_screens.cpp,
// terminal_game.cpp, and motion_sensor.cpp was rescaled to fill this bigger
// canvas - see DEVLOG.md).
#define LCD_WIDTH   480
#define LCD_HEIGHT  320

// The corruption seen on real hardware turned out to be a COLMOD bit-depth
// mismatch (see display.cpp), not a signal-integrity problem - dropping this
// to 10MHz as a diagnostic step didn't fix it. Restored to a reasonable
// speed; Waveshare's own demo doesn't state a confirmed-safe max for this
// board's wiring like it does for the 2.8" board's 80MHz, so this is a
// conservative pick rather than a confirmed one - lower it if the picture
// gets unreliable, or try pushing it higher for more headroom.
#define LCD_SPI_FREQ    40000000
#define PIN_LCD_MISO    2
#define PIN_LCD_MOSI    1
#define PIN_LCD_SCLK    5
#define PIN_LCD_CS      -1
#define PIN_LCD_DC      3
#define PIN_LCD_BL      6

// TCA9554 I2C GPIO expander - LCD reset is its P1 pin. Shared main I2C bus
// (see also touch.h, imu.h, power.cpp).
#define I2C_SDA         8
#define I2C_SCL         7
#define TCA9554_ADDR    0x20

void Display_Init();
void Display_SetBacklight(uint8_t percent); // 0-100
void Display_Push(const uint16_t *fb);      // push a full 480x320 RGB565 framebuffer
