#pragma once
#include <Arduino.h>
#include "power.h" // PressEvent

// GPIO0 is the standard ESP32/ESP32-S3 "BOOT" button (used to select
// download mode at power-on, held high the rest of the time) - present as a
// labeled physical button on this board alongside the power button already
// wired in power.h. Safe to read as a normal button once boot has finished.
#define PIN_BOOT_KEY 0

// External EC11-style rotary encoder with push switch, on the 2.54mm
// expansion header (header pins 3/5/7/9 = GND/GPIO21/GPIO38/GPIO39, one
// straight 1x4 run). These three GPIOs are otherwise routed to the camera
// connector, which this build leaves empty - never plug a camera in with the
// encoder fitted. All three inputs are active-low to GND (encoder common C
// and one switch leg on GND); if a KY-040 module is used, its "+" goes to
// 3V3 (header pin 31/32), never the header's 5V pin.
#define PIN_ENC_A    21 // CLK - swap A/B here if clockwise steps backwards
#define PIN_ENC_B    38 // DT
#define PIN_ENC_SW   39 // push switch

// Quadrature counts per mechanical detent. The PCNT unit counts every edge
// of both channels (x4), which is 4 counts/detent on most EC11 parts; some
// detent at half-cycles and need 2 instead - one "click" moving the
// highlight two rows means this should be halved.
#define ENC_COUNTS_PER_DETENT 4

// Push held this long counts as a long press (fires while still held,
// without waiting for release).
#define ENC_LONG_PRESS_MS 600

void Buttons_Init();
// True once per debounced press of the BOOT button (edge-triggered, like Touch_Tapped()).
bool Buttons_BootTapped();
// Whole detents turned since the last call (+ = clockwise, - = counter-
// clockwise). Counted in hardware by the PCNT peripheral, so steps aren't
// lost while the main loop is busy/sleeping between polls.
int16_t Buttons_EncoderSteps();
// Encoder push switch: PRESS_SHORT once on release of a press shorter than
// ENC_LONG_PRESS_MS, or PRESS_LONG once as soon as a hold reaches it (never
// both for the same press) - same contract as Power_PollKey().
PressEvent Buttons_EncoderPress();
