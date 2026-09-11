#pragma once
#include <Arduino.h>

enum PipTab { TAB_STAT = 0, TAB_INV, TAB_DATA, TAB_MAP, TAB_SCAN, TAB_RADIO, TAB_TERM, TAB_COUNT };

// Plays the boot GIF (boot_anim.h) once, full-screen, with the boot chime
// (sfx_init.h) running in parallel.
void UI_PlayBootAnimation();
void UI_DrawFrame(PipTab tab, uint8_t batteryPercent, float headingDeg);
// Handles a tap on the RADIO tab's on-screen buttons (-/+ volume, MUSIC,
// ALERT), given screen coordinates from Touch_TappedAt().
void UI_RadioTapAt(int16_t screenX, int16_t screenY);
