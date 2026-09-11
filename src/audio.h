#pragma once
#include <Arduino.h>

// I2S pins to the onboard ES8311 codec, confirmed from Waveshare's official
// demo (Arduino/examples/04_es8311_example/04_es8311_example.ino). Unlike
// the 2.8" board's bare PCM5101 DAC, ES8311 is a real codec chip that needs
// I2C register configuration (see es8311.h/.cpp, vendored from Espressif's
// own driver as bundled in Waveshare's demo) *and* a real MCLK line before
// it'll pass any audio - it's not just "wire up I2S and go".
#define PIN_I2S_MCLK  12
#define PIN_I2S_BCLK  13
#define PIN_I2S_LRC   15
#define PIN_I2S_DOUT  16

void Audio_Init();
void Audio_Beep(uint16_t freqHz, uint16_t durationMs);
// Plays the recorded tab-navigation UI sound (sfx_tab.h), used for
// touch/BOOT/power-driven tab switches instead of a synthesized beep.
void Audio_PlayTabSound();
// Software volume control (0-100) over the ES8311 codec's I2C volume
// register - this board has no physical potentiometer like the 2.8" one.
void Audio_SetVolume(uint8_t percent);
uint8_t Audio_GetVolume();

// Starts streaming one of the RADIO tab's longer clips (sfx_music.h /
// sfx_alert.h) on a dedicated FreeRTOS task, interrupting whichever one (if
// either) is already playing. Playback continues in the background across
// tab switches - nothing needs to be called from the main loop for it.
void Audio_PlayMusic();
void Audio_PlayAlert();
// Plays the boot chime (sfx_init.h) once, on the same streaming task as
// PlayMusic/PlayAlert, so it runs in parallel with the boot GIF decode/blit
// instead of blocking it.
void Audio_PlayBootSound();
void Audio_StopPlayback(); // blocks briefly until the streaming task has actually exited
bool Audio_IsPlaying();
