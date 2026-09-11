#include "display.h"
#include <SPI.h>
#include <Wire.h>

static SPIClass LCDspi(FSPI);
#define LCD_BL_PWM_CHANNEL 1

// --- TCA9554 I2C GPIO expander (standard PCA9554-family registers) ---
// LCD reset is expander pin P1. Config register: 1=input (default), 0=output.
#define TCA_REG_OUTPUT  0x01
#define TCA_REG_CONFIG  0x03

static void TCA_WriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission(true);
}

// Pulses LCD_RST (TCA9554 P1) low then high, per Waveshare's tested
// lcd_reset() (Arduino/examples/08_gfx_helloworld/08_gfx_helloworld.ino).
static void LCD_ResetViaExpander() {
  TCA_WriteReg(TCA_REG_CONFIG, 0xFD); // P1 = output, all other pins left as input
  TCA_WriteReg(TCA_REG_OUTPUT, 0x02); // P1 high
  delay(10);
  TCA_WriteReg(TCA_REG_OUTPUT, 0x00); // P1 low
  delay(10);
  TCA_WriteReg(TCA_REG_OUTPUT, 0x02); // P1 high
  delay(200);
}

static void LCD_WriteCommand(uint8_t cmd) {
  LCDspi.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LCD_DC, LOW);
  LCDspi.transfer(cmd);
  LCDspi.endTransaction();
}

static void LCD_WriteData(uint8_t data) {
  LCDspi.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LCD_DC, HIGH);
  LCDspi.transfer(data);
  LCDspi.endTransaction();
}

static void LCD_WriteBytes(const uint8_t *data, uint32_t size) {
  LCDspi.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LCD_DC, HIGH);
  LCDspi.transferBytes((uint8_t *)data, NULL, size);
  LCDspi.endTransaction();
}

// Converts a row of RGB565 pixels to the 3-bytes-per-pixel RGB666 format
// this panel's 18bpp COLMOD setting expects (each 6-bit channel left-
// justified in its own byte; the 5-bit R/B fields are widened to 6 bits by
// repeating the top bit into the new LSB) and writes it in one SPI burst.
static void LCD_WriteRowRGB666(const uint16_t *row, uint16_t count) {
  static uint8_t buf[LCD_WIDTH * 3];
  for (uint16_t i = 0; i < count; i++) {
    uint16_t px = row[i];
    uint8_t r5 = (px >> 11) & 0x1F, g6 = (px >> 5) & 0x3F, b5 = px & 0x1F;
    uint8_t r6 = (r5 << 1) | (r5 >> 4);
    uint8_t b6 = (b5 << 1) | (b5 >> 4);
    buf[i * 3 + 0] = r6 << 2;
    buf[i * 3 + 1] = g6 << 2;
    buf[i * 3 + 2] = b6 << 2;
  }
  LCD_WriteBytes(buf, (uint32_t)count * 3);
}

static void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  LCD_WriteCommand(0x2A); // CASET
  LCD_WriteData(x0 >> 8); LCD_WriteData(x0 & 0xFF);
  LCD_WriteData(x1 >> 8); LCD_WriteData(x1 & 0xFF);
  LCD_WriteCommand(0x2B); // RASET
  LCD_WriteData(y0 >> 8); LCD_WriteData(y0 & 0xFF);
  LCD_WriteData(y1 >> 8); LCD_WriteData(y1 & 0xFF);
  LCD_WriteCommand(0x2C); // RAMWR
}

// Init register sequence transcribed from Waveshare's own bundled
// Arduino_GFX library (GFX_Library_for_Arduino/src/display/Arduino_ST7796.h,
// st7796_init_operations[]) - i.e. the exact values their tested demo uses
// for this panel, just replayed through our own raw SPI writer instead of
// depending on the Arduino_GFX library itself.
static void LCD_InitSequence() {
  // COLMOD: 18bpp/RGB666 (3 bytes/pixel), not 16bpp/RGB565 (2 bytes/pixel)
  // like the sibling 2.8" board's ST7789 uses. This ST7796 module doesn't
  // handle 16bpp over SPI correctly - real hardware showed a data-dependent
  // color/streaking corruption with 0x55 (16bpp) that a lower SPI clock
  // didn't fix, confirming it was a format mismatch, not a signal-integrity
  // issue. Waveshare's own bundled Arduino_GFX header even leaves a
  // "// 0x66" comment next to the 0x55 value it actually sends. See
  // LCD_WriteRGB666() below for the corresponding 3-byte-per-pixel packing.
  LCD_WriteCommand(0x3A); LCD_WriteData(0x66);

  LCD_WriteCommand(0xF0); LCD_WriteData(0xC3); // Command Set Control
  LCD_WriteCommand(0xF0); LCD_WriteData(0x96);

  LCD_WriteCommand(0xB4); LCD_WriteData(0x01);

  LCD_WriteCommand(0xB6);
  LCD_WriteData(0x80); LCD_WriteData(0x22); LCD_WriteData(0x3B);

  LCD_WriteCommand(0xE8);
  LCD_WriteData(0x40); LCD_WriteData(0x8A); LCD_WriteData(0x00); LCD_WriteData(0x00);
  LCD_WriteData(0x29); LCD_WriteData(0x19); LCD_WriteData(0xA5); LCD_WriteData(0x33);

  LCD_WriteCommand(0xC1); LCD_WriteData(0x06);
  LCD_WriteCommand(0xC2); LCD_WriteData(0xA7);
  LCD_WriteCommand(0xC5); LCD_WriteData(0x18);

  LCD_WriteCommand(0xE0);
  LCD_WriteData(0xF0); LCD_WriteData(0x09); LCD_WriteData(0x0B); LCD_WriteData(0x06);
  LCD_WriteData(0x04); LCD_WriteData(0x15); LCD_WriteData(0x2F); LCD_WriteData(0x54);
  LCD_WriteData(0x42); LCD_WriteData(0x3C); LCD_WriteData(0x17); LCD_WriteData(0x14);
  LCD_WriteData(0x18); LCD_WriteData(0x1B);

  LCD_WriteCommand(0xE1);
  LCD_WriteData(0xE0); LCD_WriteData(0x09); LCD_WriteData(0x0B); LCD_WriteData(0x06);
  LCD_WriteData(0x04); LCD_WriteData(0x03); LCD_WriteData(0x2B); LCD_WriteData(0x43);
  LCD_WriteData(0x42); LCD_WriteData(0x3B); LCD_WriteData(0x16); LCD_WriteData(0x14);
  LCD_WriteData(0x17); LCD_WriteData(0x1B);

  LCD_WriteCommand(0xF0); LCD_WriteData(0x3C);
  LCD_WriteCommand(0xF0); LCD_WriteData(0x69);

  LCD_WriteCommand(0x11); // SLPOUT
  delay(120);

  // MADCTL: rotate to 480x320 landscape (MX|MV). Waveshare's own Arduino_GFX
  // rotation table always ORs in the BGR bit (0x68) for this panel, but that
  // produced wrong hues (green rendering blue/pink) on real hardware here,
  // so this drops it in favor of RGB order - same MX|MV bits the sibling
  // 2.8" board (ST7789, also no BGR bit) uses successfully. If the image
  // comes out mirrored instead of correctly rotated, try 0xA0 (MY|MV)
  // instead - see the 2.8" board's display.cpp for the precedent.
  LCD_WriteCommand(0x36); LCD_WriteData(0x60);

  LCD_WriteCommand(0x38); // IDMOFF
  LCD_WriteCommand(0x29); // DISPON
  delay(120);

  // This panel is IPS (Waveshare's own Arduino_ST7796 instantiation passes
  // ips=true), and Arduino_ST7796::tftInit() always finishes with
  // invertDisplay(false), which for an IPS panel resolves to sending INVON
  // rather than INVOFF. Without this, colors come out inverted (white
  // background instead of black).
  LCD_WriteCommand(0x21); // INVON
}

// Blanks the panel to black before the first real frame is ready, so
// power-on GRAM noise never flashes on screen.
static void LCD_ClearToBlack() {
  static const uint8_t zeroLine[LCD_WIDTH * 3] = {0}; // 3 bytes/pixel (RGB666)
  LCD_SetWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
  for (int y = 0; y < LCD_HEIGHT; y++) {
    LCD_WriteBytes(zeroLine, sizeof(zeroLine));
  }
}

void Display_Init() {
  Wire.begin(I2C_SDA, I2C_SCL);
  LCD_ResetViaExpander();

  pinMode(PIN_LCD_DC, OUTPUT);

  LCDspi.begin(PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);

  LCD_InitSequence();
  LCD_ClearToBlack();

  ledcSetup(LCD_BL_PWM_CHANNEL, 20000, 10);
  ledcAttachPin(PIN_LCD_BL, LCD_BL_PWM_CHANNEL);
  Display_SetBacklight(100);
}

void Display_SetBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint32_t duty = (uint32_t)percent * 1023 / 100;
  ledcWrite(LCD_BL_PWM_CHANNEL, duty);
}

void Display_Push(const uint16_t *fb) {
  LCD_SetWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
  for (int16_t y = 0; y < LCD_HEIGHT; y++) {
    LCD_WriteRowRGB666(fb + (size_t)y * LCD_WIDTH, LCD_WIDTH);
  }
}
