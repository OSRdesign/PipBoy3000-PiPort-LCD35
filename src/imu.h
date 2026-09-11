#pragma once
#include <Arduino.h>
#include "display.h" // I2C_SDA / I2C_SCL

// QMI8658 accel/gyro - same chip as the 2.8" board, confirmed at the same
// address (SensorLib's QMI8658_L_SLAVE_ADDRESS). Unlike that board it's on
// the single shared main I2C bus here, not a dedicated one.
#define IMU_ADDR      0x6B

void IMU_Init();
// Tilt-derived heading in degrees [0,360), smoothed. There's no
// magnetometer on this IMU, so this tracks device tilt/rotation rather
// than true magnetic north - good enough to make the MAP compass react
// to physically turning the prop.
float IMU_GetHeadingDeg();
