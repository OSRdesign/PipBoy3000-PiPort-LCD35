#pragma once
#include <Arduino.h>

// GPIO0 is the standard ESP32/ESP32-S3 "BOOT" button (used to select
// download mode at power-on, held high the rest of the time) - present as a
// labeled physical button on this board alongside the power button already
// wired in power.h. Safe to read as a normal button once boot has finished.
#define PIN_BOOT_KEY 0

void Buttons_Init();
// True once per debounced press of the BOOT button (edge-triggered, like Touch_Tapped()).
bool Buttons_BootTapped();
