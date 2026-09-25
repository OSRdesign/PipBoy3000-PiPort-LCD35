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
// Rotary encoder "inside a tab" mode. Enter returns false (and stays in tab
// navigation) for tabs with nothing to select (SCAN).
bool UI_EnterTabElements(PipTab tab);
void UI_ExitTabElements();
bool UI_InTabElements();
// Rotary encoder input while inside the active tab: `steps` detents (+ = clockwise)
// move that tab's highlight (STAT subtab, INV row, DATA row, MAP point of
// interest, RADIO button focus), wrapping at the ends; `pressed` activates
// it where there's something to activate (RADIO: the focused button, DATA:
// flip quests/perks, TERM: arm then confirm SHUT DOWN). SCAN ignores it.
void UI_EncoderInput(PipTab tab, int16_t steps, bool pressed);
