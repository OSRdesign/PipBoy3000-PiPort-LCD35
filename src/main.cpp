#include <Arduino.h>
#include "display.h"
#include "touch.h"
#include "gfx.h"
#include "power.h"
#include "imu.h"
#include "audio.h"
#include "ui_screens.h"
#include "buttons.h"
#include "motion_sensor.h"

static PipTab s_tab = TAB_STAT;
static PipTab s_prevTab = TAB_STAT;

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  Power_Init();
  Buttons_Init();
  Display_Init();
  GFX_Init();
  Touch_Init();
  IMU_Init();
  Audio_Init();
  MotionSensor_Init();

  UI_PlayBootAnimation();
}

void loop() {
  // Touch_TappedAt() (not Touch_Tapped()) so its one edge-triggered event
  // per loop iteration is shared between the RADIO tab's -/+ buttons (which
  // need the coordinates) and every other tab (which just needs the
  // boolean) - calling both would double-consume the same touch event.
  int16_t touchX = 0, touchY = 0;
  bool tapped = Touch_TappedAt(&touchX, &touchY);
  bool bootTapped = Buttons_BootTapped();
  PressEvent pwr = Power_PollKey();

  // Touch: tap-anywhere still cycles tabs normally, except TAB_RADIO where
  // a tap hits one of the on-screen buttons.
  if (s_tab == TAB_RADIO) {
    if (tapped) UI_RadioTapAt(touchX, touchY);
  } else if (tapped) {
    s_tab = (PipTab)((s_tab + 1) % TAB_COUNT);
    Audio_PlayTabSound();
  }

  // Physical buttons: BOOT navigates left, a power-button tap navigates
  // right - on every tab. Holding power past 1.2s still toggles the
  // backlight, unchanged.
  if (bootTapped) {
    s_tab = (PipTab)((s_tab + TAB_COUNT - 1) % TAB_COUNT);
    Audio_PlayTabSound();
  }
  if (pwr == PRESS_SHORT) {
    s_tab = (PipTab)((s_tab + 1) % TAB_COUNT);
    Audio_PlayTabSound();
  } else if (pwr == PRESS_LONG) {
    static bool backlightOn = true;
    backlightOn = !backlightOn;
    Display_SetBacklight(backlightOn ? 100 : 0);
  }

  // Rotary encoder: by default turning switches tabs and a short push
  // enters the current tab; once inside, turning moves the tab's highlight,
  // a short push activates it (see UI_EncoderInput()), and a long push
  // returns to tab navigation.
  int16_t encSteps = Buttons_EncoderSteps();
  PressEvent encPress = Buttons_EncoderPress();
  if (UI_InTabElements()) {
    if (encPress == PRESS_LONG) {
      UI_ExitTabElements();
      Audio_PlayTabSound();
    } else if (encSteps != 0 || encPress == PRESS_SHORT) {
      UI_EncoderInput(s_tab, encSteps, encPress == PRESS_SHORT);
    }
  } else {
    if (encSteps != 0) {
      s_tab = (PipTab)(((s_tab + encSteps) % TAB_COUNT + TAB_COUNT) % TAB_COUNT);
      Audio_PlayTabSound();
    }
    if (encPress == PRESS_SHORT && UI_EnterTabElements(s_tab)) Audio_PlayTabSound();
  }

  if (s_tab != s_prevTab) {
    UI_ExitTabElements(); // any tab switch (touch/BOOT/power) drops back to tab navigation
    if (s_tab == TAB_SCAN) MotionSensor_OnEnter();
    s_prevTab = s_tab;
  }

  float heading = IMU_GetHeadingDeg();
  UI_DrawFrame(s_tab, Power_GetPercent(), heading);
  delay(60);
}
