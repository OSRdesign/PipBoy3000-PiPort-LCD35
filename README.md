# PipBoy3000-PiPort-LCD35

A wearable **Pip-Boy 3000 style prop** for cosplay, running on a single
[Waveshare ESP32-S3-Touch-LCD-3.5](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5) board.
It shows a green-phosphor, CRT-styled seven-tab interface (STAT / INV / DATA / MAP / SCAN / RADIO / TERM)
with an animated Vault Boy, a tilt-reactive compass, a simulated motion sensor, sound effects, and
an optional rotary encoder for one-handed navigation.

Everything on screen is cosmetic and self-contained. There is no Wi-Fi, GPS, or radio dependence, and no
SD card is required: all art and audio are embedded in flash.

> **Fan project.** This is an unofficial, non-commercial cosplay prop. It is not affiliated with or
> endorsed by Bethesda Softworks or ZeniMax. *Fallout*, *Pip-Boy*, and *Vault-Tec* are trademarks of their
> respective owners. See [Credits](#credits-and-references) and [Assets and licensing](#assets-and-licensing).

This repository is the 3.5" / 480x320 hardware port of `PipBoy3000-PiPort` (which targets a 2.8" board).
[`DEVLOG.md`](DEVLOG.md) is the full engineering history, including every bring-up bug and how it was found.

---

## Features

### Seven tabs

| Tab | What it shows |
|---|---|
| **STAT** | Animated walking Vault Boy (19-frame cycle), subtab row, target-bracket accents, and a vitals block. |
| **INV** | Five-item inventory (10mm pistol, combat armor, Stimpak, bobby pin, 10mm ammo) with an auto-advancing selection box, original hand-drawn item icons, and a weight readout. |
| **DATA** | Alternates between an **ACTIVE QUESTS** page and a **PERKS** page, with word-wrapped descriptions. |
| **MAP** | Radar-style sweep, dot-grid terrain texture, named points of interest, and a compass ring that rotates as you tilt the device (accelerometer-based; there is no magnetometer, so it is not true north). |
| **SCAN** | Self-contained motion-sensor simulation: ghouls and soldiers wander a semicircular arena, encounter each other, and trigger "LOST VITAL SIGNS" and "GHOUL ALERT" banners. MEDIC and AMMO crates grant a one-shot buff. Population-balance rules keep the simulation from dying out. |
| **RADIO** | Animated signal bars, software volume **-/+** buttons, and **MUSIC** / **ALERT** clip buttons. Clips play in the background on a dedicated audio task. |
| **TERM** | ROBCO-styled live diagnostics (uptime, battery, volume, IMU tilt, audio state, heap/PSRAM/flash usage) beside a pulsing Vault-Tec emblem, plus a two-step **SHUT DOWN** button that fully cuts power via the PMU. |

### Across the whole UI

- 480x320 landscape UI, rendered from a PSRAM framebuffer with a custom grayscale font
  (rasterized from Monofonto) and continuous green shading.
- Animated boot sequence: a 60-frame GIF, decoded frame by frame at runtime, plus a boot chime.
- Transfer-time CRT scanline effect: alternate rows are dimmed only in the SPI transfer buffer, so the
  framebuffer never compounds and text stays readable.
- Shared chrome: tab bar with an open-bottom "folder tab" bracket, level and battery status, and a pulsing RADS icon.
- Recorded tab-switch sound, boot chime, and music/alert clips through the ES8311 codec and onboard speaker.
- Backlight toggle on a long power-key press.

### Controls

| Input | Action |
|---|---|
| **Touch: tap anywhere** | Next tab. |
| **Touch: tap RADIO buttons** | -/+ volume, MUSIC, ALERT (position-aware). |
| **BOOT button** (GPIO0) | Previous tab. |
| **Power button, short press** | Next tab. |
| **Power button, long press** | Toggle the backlight. |
| **Encoder: turn** | Switch tabs (tab-navigation mode), or move the highlight (inside a tab). |
| **Encoder: short push** | Enter the current tab, or activate the highlighted item. |
| **Encoder: long push** (600 ms) | Return to tab navigation. |
| **TERM > SHUT DOWN** (encoder) | Push to arm (blinking `CONFIRM?`), push again within 4 s to power off. The power button turns the board back on. |

The rotary encoder is optional. Everything else works with touch and the two onboard buttons.

---

## Hardware

### Required

| Part | Notes |
|---|---|
| **Waveshare ESP32-S3-Touch-LCD-3.5** | The whole build. ESP32-S3R8 (16 MB flash, 8 MB octal PSRAM), 3.5" 320x480 ST7796 IPS LCD, FT6336 capacitive touch, QMI8658 IMU, AXP2101 PMU, ES8311 audio codec with onboard speaker, BOOT and power buttons. |
| **USB-C cable** | Flashing, serial monitor, and power. |

### Optional

| Part | Notes |
|---|---|
| **EC11-style rotary encoder with push switch** (or a KY-040 module) | For knob navigation. |
| **Single-cell Li-ion/LiPo battery** | For untethered use, through the board's battery connector (managed by the AXP2101). Check the connector polarity against the board's silkscreen before plugging in. |
| **Dupont wires or a 1x4 housing** | To connect the encoder to the 2.54 mm expansion header. |
| **3D-printed housing** | Not included in this repository. |

### On-board devices and pins

All pin assignments were taken from Waveshare's own demo package and driver source, not guessed. They are
defined in `src/display.h`, `src/audio.h`, `src/buttons.h`, `src/touch.h`, and `src/imu.h`.

| Function | Device | Pins / address |
|---|---|---|
| LCD SPI (40 MHz) | ST7796, 18-bit RGB666 | SCLK=5, MOSI=1, MISO=2, DC=3, CS tied low, Backlight=6 (PWM) |
| LCD reset | via TCA9554 I2C GPIO expander | I2C addr `0x20`, pin P1 (not a bare GPIO) |
| Main I2C bus | Shared by all I2C devices | SDA=8, SCL=7 |
| Touch | FT6336 | I2C `0x38` (reports raw portrait coordinates; remapped in `touch.cpp`) |
| IMU | QMI8658 (accelerometer only is used) | I2C `0x6B` |
| PMU | AXP2101 (rails, battery, power key) | I2C, driven through XPowersLib |
| Audio | ES8311 codec + speaker | I2S: MCLK=12, BCLK=13, LRC/WS=15, DOUT=16; codec control over I2C |
| BOOT key | GPIO0 | Active low |
| Power key | Routed through the AXP2101 | Short and long press reported by PMU IRQ flags |
| RTC (PCF85063), SD slot, camera header | Present on the board | **Unused** by this firmware |

### Wiring the rotary encoder

The encoder uses three GPIOs that the board routes to the camera connector, so they are free as long as
**no camera is fitted**. On the 32-pin 2.54 mm expansion header, header pins 3, 5, 7, and 9 sit in one
straight run, which suits a single 1x4 Dupont housing.

| Encoder pin | Header pin | ESP32-S3 GPIO |
|---|---|---|
| **C / GND** (common) and one push-switch leg | 3 | GND |
| **A / CLK** | 5 | GPIO21 |
| **B / DT** | 7 | GPIO38 |
| **SW** (other push-switch leg) | 9 | GPIO39 |

```
   EC11 encoder                     Waveshare 2.54 mm header
  ┌────────────┐
  │  A  ───────┼───────────────►  pin 5  (GPIO21)
  │  B  ───────┼───────────────►  pin 7  (GPIO38)
  │  C  ───────┼──┐
  │            │  ├────────────►  pin 3  (GND)
  │  SW ───────┼──┼─────────────►  pin 9  (GPIO39)
  │  SW ───────┼──┘   (second switch leg to GND)
  └────────────┘
```

All three inputs are **active low** with internal pull-ups.

- **KY-040 modules:** the `+` pin goes to **3V3** (header pin 31 or 32), **never** to the header's 5 V pin.
- **Direction:** if clockwise steps go backwards, swap `PIN_ENC_A` and `PIN_ENC_B` in `src/buttons.h`.
- **Detent size:** if one click moves the highlight two rows, change `ENC_COUNTS_PER_DETENT` from 4 to 2.
- **Noise on USB power:** the push switch is debounced in software (5 ms sampling, integrator). If a noisy
  charger still causes stray presses, add an external 10 kΩ pull-up to 3V3 and a 100 nF capacitor to GND on SW.

---

## Building and flashing

Requires [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).

```bash
git clone https://github.com/OSRdesign/PipBoy3000-PiPort-LCD35.git
cd PipBoy3000-PiPort-LCD35
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial monitor, 115200 baud
```

Things to know:

- **Serial port.** `platformio.ini` sets `upload_port` and `monitor_port` to `COM31`, the port of the
  author's board. Change both to your port (see `pio device list`), or remove them to auto-detect.
- **Platform is pinned** to `espressif32@6.9.0` (Arduino core 2.0.17 / IDF 4.4). The firmware uses the legacy
  `ledcSetup`/`ledcAttachPin` and `driver/pcnt.h` APIs, which do not exist in Arduino core 3.x. Do not unpin it.
- **`--no-stub` upload.** The board's native-USB port drops the connection during esptool's stub handoff,
  so `upload_flags = --no-stub` is set.
- **Flash layout.** 16 MB QIO flash, octal PSRAM (`qio_opi`), and one 3 MB factory app partition
  (`partitions.csv`). The firmware uses about 60% of it, because the boot GIF and audio clips are embedded.
- **Library dependencies** are fetched automatically: `lewisxhe/XPowersLib` and `bitbank2/AnimatedGIF`.

---

## Project layout

```
src/
  main.cpp             setup/loop, input routing (~60 ms loop)
  ui_screens.*         boot animation, the 7 tabs, shared chrome
  gfx.*                PSRAM framebuffer, primitives, grayscale sprite/font blitter
  display.*            ST7796 init, RGB565->RGB666 push, scanline dimming, backlight PWM
  touch.*              FT6336 driver (tap and tap-at-position)
  buttons.*            BOOT key + rotary encoder (PCNT quadrature decode, debounced switch)
  power.*              AXP2101 via XPowersLib: battery, key events, shutdown
  imu.*                QMI8658 accelerometer, smoothed tilt
  audio.*              I2S output, ES8311 volume, background clip playback task
  es8311.* es8311_reg.h  Espressif ES8311 codec driver (Apache-2.0)
  motion_sensor.*      SCAN-tab simulation
  font_pipboy.h        8x8 grayscale font
  *.h (sprites, sfx)   embedded art and audio (boot GIF, Vault Boy frames, icons, PCM clips)
DEVLOG.md              full engineering log
PORTING_FROM_PYPBOY.md assessment of ideas borrowed from pypboy
```

---

## Assets and licensing

- **Code:** original firmware, except for the third-party components listed below. **No license has been
  chosen for this repository yet.** Until one is added, the default is all rights reserved.
- **Font:** Monofonto by Typodermic Fonts is distributed free for personal use. It is used here for a
  personal, non-commercial prop. Check its license before any commercial use.
- **`stats.gif`** is a capture from *Fallout 4* and is used for personal, non-commercial cosplay purposes only.
- **Icons:** the INV item icons were drawn from scratch for this project.
- **Game-derived content:** the quest and perk text was hand-transcribed from pypboy's data tables.
  Anything derived from Bethesda's games remains their property.

---

## Credits and references

**Hardware and vendor resources**

- [Waveshare ESP32-S3-Touch-LCD-3.5 wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5) and the
  [official demo package](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5-Demo.zip).
  Pinouts, the ST7796 init sequence, the AXP2101 rail setup, and the touch/audio bring-up were verified against
  its tested driver source. The ST7796 init sequence comes from the
  [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) library that the package bundles.
- [Espressif](https://github.com/espressif) for the ES8311 codec driver (`es8311.c/.h`, Apache-2.0,
  as adapted in Waveshare's demo) and the ESP-IDF PCNT rotary-encoder example that the decoding follows.

**Libraries**

- [XPowersLib](https://github.com/lewisxhe/XPowersLib) by Lewis He: AXP2101 power management.
- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) by Larry Bank (bitbank2): runtime GIF decoding for
  the boot animation.
- [PlatformIO](https://platformio.org/) and the
  [arduino-esp32](https://github.com/espressif/arduino-esp32) core.

**Projects that inspired or supplied ideas**

- [zapwizard/pypboy](https://github.com/zapwizard/pypboy) (MIT): a Python/pygame Pip-Boy UI for Raspberry Pi.
  The INV/DATA content structure, the local-area-scan MAP concept, and the (since retired) terminal
  hacking minigame idea came from here. No code was copied; see
  [`PORTING_FROM_PYPBOY.md`](PORTING_FROM_PYPBOY.md).
- [jejelinge/PIPBOY_3000](https://github.com/jejelinge/PIPBOY_3000): an ESP32/TFT_eSPI Pip-Boy build.
  The animated boot GIF (`boot_anim.h`), the boot chime, and the style reference for the active-tab bracket
  come from this project. Its art and audio are original work, per the project owner.
- PipDroid, an Android Pip-Boy simulator: reviewed at asset-listing level only, for feature ideas
  (real radio audio, UI sound set). No assets were taken.

**Typography and source material**

- [Monofonto](https://www.typodermic.com/) by Typodermic Fonts (Ray Larabie): the typeface used in the
  *Fallout 3* and *New Vegas* Pip-Boy UI, rasterized into `font_pipboy.h`.
- *Fallout* and the Pip-Boy by Bethesda Game Studios / Bethesda Softworks: the source of the aesthetic this
  prop pays homage to.
