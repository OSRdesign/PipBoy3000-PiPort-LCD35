#include "audio.h"
#include "display.h" // I2C_SDA / I2C_SCL
#include "es8311.h"
#include "sfx_tab.h"
#include "sfx_music.h"
#include "sfx_alert.h"
#include "sfx_init.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define I2S_PORT       I2S_NUM_0
#define SAMPLE_RATE_HZ 16000
#define MCLK_FREQ_HZ   (SAMPLE_RATE_HZ * 256) // 4.096MHz - a supported ES8311 mclk/rate pair

static es8311_handle_t s_es8311 = nullptr;
static uint8_t s_volume = 70;

static void es8311CodecInit() {
  s_es8311 = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (!s_es8311) return;

  es8311_clock_config_t clk = {};
  clk.mclk_inverted = false;
  clk.sclk_inverted = false;
  clk.mclk_from_mclk_pin = true;
  clk.mclk_frequency = MCLK_FREQ_HZ;
  clk.sample_frequency = SAMPLE_RATE_HZ;

  es8311_init(s_es8311, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  es8311_voice_volume_set(s_es8311, s_volume, NULL);
  es8311_microphone_config(s_es8311, false);
}

void Audio_Init() {
  Wire.begin(I2C_SDA, I2C_SCL);
  es8311CodecInit();

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE_HZ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    // 8*512 = 4096 frames (~256ms) buffered, a bit more headroom than the
    // original 4*256 (~64ms) for the longer streamed clips (see below).
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);

  i2s_pin_config_t pins = {
    // ES8311 needs a real MCLK line (unlike the 2.8" board's PCM5101) -
    // must be set explicitly, since an unset mck_io_num silently defaults
    // to GPIO0 (the BOOT-strap pin) on this IDF version.
    .mck_io_num = PIN_I2S_MCLK,
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRC,
    .data_out_num = PIN_I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };
  i2s_set_pin(I2S_PORT, &pins);
}

// Blocking tone/click writer. Durations here are short (<=60ms) so the
// brief block from the main loop is not perceptible.
static void writeTone(float freqHz, uint32_t ms, float amplitude, bool decay) {
  uint32_t samples = SAMPLE_RATE_HZ * ms / 1000;
  for (uint32_t i = 0; i < samples; i++) {
    float t = (float)i / SAMPLE_RATE_HZ;
    float env = decay ? expf(-t * 40.0f) : 1.0f;
    int16_t s = (int16_t)(sinf(2.0f * PI * freqHz * t) * 32767.0f * amplitude * env);
    int16_t stereo[2] = {s, s};
    size_t written;
    i2s_write(I2S_PORT, stereo, sizeof(stereo), &written, portMAX_DELAY);
  }
}

void Audio_Beep(uint16_t freqHz, uint16_t durationMs) {
  writeTone(freqHz, durationMs, 0.35f, false);
}

void Audio_SetVolume(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_volume = percent;
  if (s_es8311) es8311_voice_volume_set(s_es8311, s_volume, NULL);
}

uint8_t Audio_GetVolume() {
  return s_volume;
}

static_assert(SFX_TAB_SAMPLE_RATE == SAMPLE_RATE_HZ,
              "sfx_tab.h was resampled for a different rate than audio.cpp's fixed I2S rate");

// Blocking, like writeTone() - this clip is ~99ms (longer than the ~60ms
// tones elsewhere in this file), so a tab switch briefly delays the next
// main-loop frame. Acceptable for a one-shot UI sound; trim the source clip
// and regenerate sfx_tab.h if that stutter becomes noticeable.
void Audio_PlayTabSound() {
  for (uint32_t i = 0; i < SFX_TAB_LEN; i++) {
    int16_t stereo[2] = {SFX_TAB_DATA[i], SFX_TAB_DATA[i]};
    size_t written;
    i2s_write(I2S_PORT, stereo, sizeof(stereo), &written, portMAX_DELAY);
  }
}

static_assert(SFX_MUSIC_SAMPLE_RATE == SAMPLE_RATE_HZ,
              "sfx_music.h was resampled for a different rate than audio.cpp's fixed I2S rate");
static_assert(SFX_ALERT_SAMPLE_RATE == SAMPLE_RATE_HZ,
              "sfx_alert.h was resampled for a different rate than audio.cpp's fixed I2S rate");
static_assert(SFX_INIT_SAMPLE_RATE == SAMPLE_RATE_HZ,
              "sfx_init.h was resampled for a different rate than audio.cpp's fixed I2S rate");

// Streaming playback for the RADIO tab's music/alert clips, on its own
// FreeRTOS task rather than the main loop. These run several seconds -
// playing them with a blocking i2s_write() loop like Audio_Beep()/
// Audio_PlayTabSound() do would freeze the whole UI for the clip's
// duration, but feeding them incrementally from the main loop with
// non-blocking (0 ticks_to_wait) writes (the first approach here) glitched
// audibly on real hardware - any jitter in the UI's own draw/SPI timing
// could starve the DMA queue. A dedicated task can just block on
// i2s_write(portMAX_DELAY,...) and let the I2S driver pace it in real time,
// completely decoupled from the UI loop - the standard, robust way to do
// this on FreeRTOS.
struct StreamJob { const int16_t *data; uint32_t len; };
static StreamJob s_pendingJob;
static TaskHandle_t s_streamTask = nullptr;
static volatile bool s_stopRequested = false;
static volatile bool s_playing = false;

static void streamTaskFn(void *arg) {
  StreamJob job = *(StreamJob *)arg; // copied before this task can race a new startStream()

  const uint32_t CHUNK = 256; // 16ms/write at 16kHz
  int16_t stereoBuf[CHUNK * 2];
  for (uint32_t pos = 0; pos < job.len && !s_stopRequested; ) {
    uint32_t n = job.len - pos;
    if (n > CHUNK) n = CHUNK;
    for (uint32_t i = 0; i < n; i++) {
      int16_t s = job.data[pos + i];
      stereoBuf[i * 2] = s;
      stereoBuf[i * 2 + 1] = s;
    }
    size_t written = 0;
    i2s_write(I2S_PORT, stereoBuf, n * 4, &written, portMAX_DELAY);
    pos += written / 4;
  }
  s_playing = false;
  s_streamTask = nullptr;
  vTaskDelete(nullptr);
}

static void startStream(const int16_t *data, uint32_t len) {
  Audio_StopPlayback(); // stop and join any previous clip before starting a new one
  s_pendingJob = {data, len};
  s_stopRequested = false;
  s_playing = true;
  xTaskCreatePinnedToCore(streamTaskFn, "audio_stream", 4096, &s_pendingJob, 1, &s_streamTask, 0);
}

void Audio_PlayMusic() { startStream(SFX_MUSIC_DATA, SFX_MUSIC_LEN); }
void Audio_PlayAlert() { startStream(SFX_ALERT_DATA, SFX_ALERT_LEN); }
void Audio_PlayBootSound() { startStream(SFX_INIT_DATA, SFX_INIT_LEN); }

void Audio_StopPlayback() {
  if (s_streamTask) {
    s_stopRequested = true;
    while (s_streamTask) delay(1); // wait for the task to actually exit
  }
  s_playing = false;
}

bool Audio_IsPlaying() {
  return s_playing;
}
