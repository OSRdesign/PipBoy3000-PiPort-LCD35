#include "buttons.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define ENC_PCNT_UNIT PCNT_UNIT_0

struct DebouncedKey {
  uint8_t pin;
  bool wasPressed;
  uint32_t lastTapMs;
};

static DebouncedKey s_bootKey = {PIN_BOOT_KEY, false, 0};
static int16_t s_encAccum = 0; // counts not yet reported as whole detents

// Encoder push switch, sampled every SW_SAMPLE_MS on an esp_timer rather
// than from the main loop (which only polls every ~100ms, render +
// delay(60), and would miss a quick tap). An integrator debounce needs
// SW_DEBOUNCE_SAMPLES net low samples (~30ms) to call it pressed. This
// replaced a falling-edge ISR latch, which on USB power turned
// sub-microsecond noise on the (weakly pulled-up) switch line into phantom
// presses - confirmed from a serial capture, see DEVLOG.md item 43.
#define SW_SAMPLE_MS        5
#define SW_DEBOUNCE_SAMPLES 6

static portMUX_TYPE s_swMux = portMUX_INITIALIZER_UNLOCKED;
static volatile PressEvent s_swEvent = PRESS_NONE; // written by the timer, consumed by Buttons_EncoderPress()
static uint8_t s_swIntegrator = 0;
static bool s_swDown = false;
static bool s_swLongFired = false;
static uint32_t s_swDownAt = 0;

static void swSampleCb(void *) {
  bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw) {
    if (s_swIntegrator < SW_DEBOUNCE_SAMPLES) s_swIntegrator++;
  } else if (s_swIntegrator > 0) {
    s_swIntegrator--;
  }

  PressEvent ev = PRESS_NONE;
  uint32_t now = millis();
  if (!s_swDown && s_swIntegrator == SW_DEBOUNCE_SAMPLES) {
    s_swDown = true;
    s_swLongFired = false;
    s_swDownAt = now;
  } else if (s_swDown && s_swIntegrator == 0) {
    s_swDown = false;
    if (!s_swLongFired) ev = PRESS_SHORT;
  } else if (s_swDown && !s_swLongFired && (now - s_swDownAt) >= ENC_LONG_PRESS_MS) {
    s_swLongFired = true;
    ev = PRESS_LONG;
  }
  if (ev != PRESS_NONE) {
    portENTER_CRITICAL(&s_swMux);
    s_swEvent = ev;
    portEXIT_CRITICAL(&s_swMux);
  }
}

static bool keyTapped(DebouncedKey &k) {
  bool pressed = digitalRead(k.pin) == LOW; // active-low, like the power button
  uint32_t now = millis();
  if (pressed) {
    if (!k.wasPressed && (now - k.lastTapMs) > 250) {
      k.wasPressed = true;
      k.lastTapMs = now;
      return true;
    }
  } else {
    k.wasPressed = false;
  }
  return false;
}

// Standard two-channel x4 quadrature decode (same channel/mode setup as
// ESP-IDF 4.4's legacy rotary_encoder example): each channel counts both
// edges of one signal, with the other signal's level deciding direction.
// Every field is assigned explicitly - a zero-defaulted GPIO field here
// would silently claim GPIO0 (the BOOT key), the same trap as audio.cpp's
// .mck_io_num (see DEVLOG.md item 13).
static void encoderInit() {
  pcnt_config_t cfg = {};
  cfg.unit = ENC_PCNT_UNIT;
  cfg.counter_h_lim = 32767;
  cfg.counter_l_lim = -32768;
  cfg.lctrl_mode = PCNT_MODE_REVERSE;
  cfg.hctrl_mode = PCNT_MODE_KEEP;

  cfg.channel = PCNT_CHANNEL_0;
  cfg.pulse_gpio_num = PIN_ENC_A;
  cfg.ctrl_gpio_num = PIN_ENC_B;
  cfg.pos_mode = PCNT_COUNT_DEC;
  cfg.neg_mode = PCNT_COUNT_INC;
  pcnt_unit_config(&cfg);

  cfg.channel = PCNT_CHANNEL_1;
  cfg.pulse_gpio_num = PIN_ENC_B;
  cfg.ctrl_gpio_num = PIN_ENC_A;
  cfg.pos_mode = PCNT_COUNT_INC;
  cfg.neg_mode = PCNT_COUNT_DEC;
  pcnt_unit_config(&cfg);

  // Internal pull-ups so a bare encoder (no module) works as-is; harmless
  // alongside a KY-040's own 10k pull-ups.
  gpio_pullup_en((gpio_num_t)PIN_ENC_A);
  gpio_pullup_en((gpio_num_t)PIN_ENC_B);

  // Max glitch filter (1023 APB cycles, ~12.8us) - rejects contact-bounce
  // spikes; slower bounce just nets out as +1/-1 in the x4 count.
  pcnt_set_filter_value(ENC_PCNT_UNIT, 1023);
  pcnt_filter_enable(ENC_PCNT_UNIT);

  pcnt_counter_pause(ENC_PCNT_UNIT);
  pcnt_counter_clear(ENC_PCNT_UNIT);
  pcnt_counter_resume(ENC_PCNT_UNIT);
}

void Buttons_Init() {
  pinMode(PIN_BOOT_KEY, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  static esp_timer_handle_t swTimer;
  const esp_timer_create_args_t args = {swSampleCb, nullptr, ESP_TIMER_TASK, "enc_sw", false};
  esp_timer_create(&args, &swTimer);
  esp_timer_start_periodic(swTimer, SW_SAMPLE_MS * 1000);
  encoderInit();
}

bool Buttons_BootTapped() {
  return keyTapped(s_bootKey);
}

PressEvent Buttons_EncoderPress() {
  portENTER_CRITICAL(&s_swMux);
  PressEvent ev = s_swEvent;
  s_swEvent = PRESS_NONE;
  portEXIT_CRITICAL(&s_swMux);
  return ev;
}

int16_t Buttons_EncoderSteps() {
  int16_t count = 0;
  pcnt_get_counter_value(ENC_PCNT_UNIT, &count);
  if (count == 0) return 0;
  // Drain the hardware counter into a software accumulator every poll so it
  // never nears its +/-32767 limit (where it'd reset to 0 and lose the
  // delta). An edge landing between the read and the clear would be lost,
  // but that window is a couple of register accesses long.
  pcnt_counter_clear(ENC_PCNT_UNIT);
  s_encAccum += count;
  int16_t steps = s_encAccum / ENC_COUNTS_PER_DETENT; // truncates toward 0: partial turns stay pending
  s_encAccum -= steps * ENC_COUNTS_PER_DETENT;
  return steps;
}
