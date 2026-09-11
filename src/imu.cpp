#include "imu.h"
#include <Wire.h>
#include <math.h>

#define REG_CTRL1  0x02
#define REG_CTRL2  0x03
#define REG_CTRL5  0x06
#define REG_CTRL6  0x07
#define REG_CTRL7  0x08
#define REG_AX_L   0x35

static const float ACC_SCALE = 4.0f / 32768.0f; // +-4G range

static bool i2cRead(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((int)IMU_ADDR, (int)len);
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void i2cWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission(true);
}

void IMU_Init() {
  Wire.begin(I2C_SDA, I2C_SCL);

  uint8_t ctrl1 = 0;
  i2cRead(REG_CTRL1, &ctrl1, 1);
  ctrl1 &= 0xFE;  // enable 2MHz oscillator
  ctrl1 |= 0x40;  // auto address increment
  i2cWrite(REG_CTRL1, ctrl1);

  i2cWrite(REG_CTRL7, 0x43); // enable accel + gyro, full mode
  i2cWrite(REG_CTRL6, 0x00);

  // CTRL2: accel range +-4G (0x1 << 4) | ODR normal 8000Hz (0x0)
  i2cWrite(REG_CTRL2, (0x1 << 4) | 0x0);
  // CTRL5: accel low-pass filter mode 0, enabled
  i2cWrite(REG_CTRL5, 0x01);
}

float IMU_GetHeadingDeg() {
  static float smoothed = -1.0f;

  uint8_t buf[6];
  if (!i2cRead(REG_AX_L, buf, 6)) return smoothed < 0 ? 0 : smoothed;

  int16_t rawX = (int16_t)((buf[1] << 8) | buf[0]);
  int16_t rawY = (int16_t)((buf[3] << 8) | buf[2]);
  float ax = rawX * ACC_SCALE;
  float ay = rawY * ACC_SCALE;

  float angle = atan2f(ay, ax) * 180.0f / (float)PI;
  if (angle < 0) angle += 360.0f;

  if (smoothed < 0) {
    smoothed = angle;
  } else {
    float diff = angle - smoothed;
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    smoothed += diff * 0.15f;
    if (smoothed < 0) smoothed += 360.0f;
    if (smoothed >= 360.0f) smoothed -= 360.0f;
  }
  return smoothed;
}
