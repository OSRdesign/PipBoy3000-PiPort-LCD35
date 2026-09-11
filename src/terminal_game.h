#pragma once
#include <Arduino.h>

// ROBCO-style "guess the password by likeness" terminal hacking minigame,
// ported (as a from-scratch reimplementation, not a code port) from the
// idea in zapwizard/pypboy's modules/passcode. Our touch hardware is only
// used tap-anywhere/edge-triggered elsewhere in this firmware (see
// touch.cpp), so instead of tap-to-pick-a-word-by-position, the cursor
// auto-cycles through the candidate words and a single tap confirms
// whichever word is currently highlighted - no new touch/hardware code
// needed, just reuses Touch_Tapped() as already wired in main.cpp.
// Leaving the tab (win, lose, or mid-game) is done with the BOOT/POWER
// button tab navigation in main.cpp, same as every other tab.

void TerminalGame_Enter();    // call once when the active tab becomes TAB_TERM
void TerminalGame_Confirm();  // call on tap while already on TAB_TERM
void TerminalGame_Draw();     // renders current state; advances cursor/result timers
