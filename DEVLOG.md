# Pip-Boy 3000 Cosplay Firmware — Dev Log

**This is the `PipBoy3000-PiPort-LCD35` fork** — a hardware-only port of
[`PipBoy3000-PiPort`](../PipBoy3000-PiPort) (2.8" board) to the Waveshare
ESP32-S3-Touch-LCD-3.5. Everything below the "Hardware" section is inherited
history from that project (UI, game logic, features) and is still accurate —
none of it changed here. See "Fork: PipBoy3000-PiPort-LCD35" near the bottom
for what *did* change.

## Hardware

**Board:** Waveshare ESP32-S3-Touch-LCD-3.5, connected via native USB-JTAG/Serial.
- MCU: ESP32-S3R8 — 16MB quad flash, 8MB octal PSRAM (`CONFIG_SPIRAM_MODE_OCT`)
- Display: 3.5" ST7796 SPI IPS, 320x480 native portrait (rotated to 480x320 landscape;
  the UI framebuffer runs at the full 480x320 — see "Filling the bigger screen" below)
- Touch: FT6336 capacitive, on the **shared main** I2C bus (unlike the 2.8" board, which had a
  dedicated second bus for touch)
- IMU: QMI8658 accel/gyro — same chip as the 2.8" board, on the shared main I2C bus
- RTC: PCF85063 (on main I2C bus — **not yet used** in our firmware, same as before)
- Power: AXP2101 PMU chip on the main I2C bus — supplies every rail on the board (including
  the display and audio codec) and owns the physical power button, replacing the 2.8" board's
  bare battery-ADC pin + power-hold latch GPIO
- Audio: ES8311 I2S codec (needs I2C register configuration, unlike the 2.8" board's bare
  PCM5101 DAC) + onboard speaker
- Also present but unused by this firmware: SD card slot (SD_MMC), OV5640/OV2640 camera header

All pin numbers were confirmed by downloading Waveshare's own official demo package
(`files.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5/ESP32-S3-Touch-LCD-3.5-Demo.zip`) and reading
their tested driver source, rather than guessing from the public wiki page (same lesson learned
porting the 2.8" board originally — its pinout tables are collapsed/JS-rendered and don't come
through via a plain page fetch).

| Function | Pin(s) |
|---|---|
| LCD SPI | SCLK=5, MOSI=1, MISO=2, DC=3, CS=tied low (n/a), Backlight=6 (PWM) |
| LCD reset | via TCA9554 I2C GPIO expander (addr 0x20), pin P1 — not a bare GPIO |
| Main I2C (Wire) | SDA=8, SCL=7 — touch (FT6336, addr 0x38), IMU (QMI8658, addr 0x6B), RTC (PCF85063, unused), PMU (AXP2101), LCD-reset expander (TCA9554) all share this one bus |
| I2S audio | MCLK=12, BCLK=13, LRC/WS=15, DOUT=16 (DIN=14 unused, no mic) |
| SD_MMC (unused) | CLK=11, CMD=10, D0=9 |
| Camera XCLK (unused) | GPIO38 |
| Buttons | BOOT=GPIO0 (unchanged from 2.8" board), power button routed through the AXP2101 (not a raw GPIO) |

## Toolchain

PlatformIO, `platform = espressif32` (resolved to Arduino core 2.0.17 / IDF 4.4.7 in this
environment — **note:** this core uses the legacy `ledcSetup/ledcAttachPin/ledcWrite(channel,...)`
API, not the newer `ledcAttach(pin,...)` API Waveshare's own demo uses, so backlight PWM had to
be written against the old API). Board: `esp32-s3-devkitc-1` with overrides:
`board_upload.flash_size=16MB`, `board_build.arduino.memory_type=qio_opi` (confirmed via verbose
build log that this actually links the octal-PSRAM SDK lib variant). Custom `partitions.csv`
(single 3MB factory app partition, no OTA/spiffs needed).

## Firmware architecture (`src/`)

- `display.*` — ST7789 bring-up (init sequence copied verbatim from Waveshare's tested code) +
  raw framebuffer push over SPI. Landscape via MADCTL=0x60.
- `touch.*` — CST328 driver on Wire1; exposes only `Touch_Tapped()` (edge-triggered, debounced) —
  we deliberately don't do position-aware touch, just "any tap cycles to next tab."
- `gfx.*` — framebuffer (PSRAM-allocated 320x240 RGB565) + primitives (rect/line/circle) + text
  + `GFX_BlitMono()` for grayscale sprite/font blitting with continuous (non-thresholded) shading.
- `font_pipboy.h` — see Font section below.
- `power.*` — battery %, power-hold latch, long-press detection for backlight toggle.
- `imu.*` — minimal QMI8658 driver (accel only), smoothed tilt angle for the compass.
- `audio.*` — legacy `driver/i2s.h` tone generator (no MP3/codec library), used for the
  tab-switch beep.
- `ui_screens.*` — boot sequence + the 5 tabs (STAT/INV/DATA/MAP/RADIO) + chrome (top tab bar,
  level/battery status row, corner bezel accents).
- `stat_anim.h`, `intro_image.h` — embedded image/animation data (see below).
- `main.cpp` — wires it all together; ~60ms main loop.

## What we built, in order

1. **Base hardware bring-up** — display + touch + PlatformIO project from scratch, verified by
   flashing and confirming boot over serial.
2. **Initial Pip-Boy UI** — 5 tabs matching the real game's tab names (STAT/INV/DATA/MAP/RADIO),
   green monochrome CRT palette, custom 8x8 font renderer, tap-anywhere-to-cycle-tabs.
3. **Rotation fix** — user reported the screen was 90° rotated; switched panel to landscape
   (320x240) via MADCTL and redesigned all 5 screens for the new aspect ratio (top tab bar,
   status row, corner bezel brackets).
4. **Boot splash** — user supplied `intro.png` (Vault-Tec "WELCOME" screen); converted to a raw
   320x240 RGB565 array (`intro_image.h`) and blitted directly for 5s at boot before the ROBCO
   terminal text sequence.
5. **Tilt-reactive MAP compass + audio** — added QMI8658 IMU driving a rotating compass
   ring/crosshair on the MAP tab (tilt-based, no magnetometer on this IMU so it's not true
   magnetic north — cosmetic but reacts convincingly to turning the device), plus an I2S
   tab-switch beep. Also added ambient "Geiger clicking" — **later removed** per user request.
6. **Animated STAT screen** — replaced the static SPECIAL/vitals layout with an actual animated
   Vault Boy:
   - First pass used `stats.gif` (a captured Fallout 4 STAT screen, 24 frames) — cropped out just
     the character, converted to an 8-bit intensity array, blitted at 2x nearest-neighbor scale.
     User reported this looked pixelated/hard to read.
   - Second pass switched to `vaultboywalking.gif` (already a clean, tightly-cropped 19-frame
     walk cycle) at **native resolution with no upscaling**, and changed the blit from a 2-level
     threshold to **continuous grayscale-to-green shading** — this fixed the blockiness.
7. **CRT flicker, iterated twice:**
   - v1: per-scanline pixel darkening in the framebuffer — made text and the animation hard to
     read, removed per user feedback.
   - v2: tried a backlight-PWM-based brightness flicker instead (doesn't touch pixels at all) —
     user didn't perceive any effect. Removed per user request; **backlight is now a constant
     100%, no flicker effect currently implemented.**
8. **Font swap** — replaced the placeholder public-domain IBM 8x8 font with **Monofonto**, the
   actual font used in Fallout 3/New Vegas's Pip-Boy UI, downloaded from its legitimate
   free-for-personal-use distribution on dafont.com (explicitly *not* extracted from the
   PipDroid APK, which bundles a licensed copy for its own commercial app). Rendering it at a
   literal 8px size produced broken glyphs for wide letters (M, W) because the font has no
   hinting for such tiny sizes, so instead each glyph is rendered large and clean, then
   downsampled to an 8x8 **grayscale** cell (not 1-bit) and shaded continuously at draw time —
   same technique as the sprite blitting.
9. **PipDroid analysis** — inspected (resource/filename listing only, no bytecode decompilation)
   an Android Pip-Boy simulator app's asset bundle for feature ideas. See Backlog below.

## Fork: PipBoy3000-PiPort

This is a fork of the main `PipBoy3000` project, created to assess and port ideas from
[zapwizard/pypboy](https://github.com/zapwizard/pypboy) (a Python/pygame Pip-Boy UI for
Raspberry Pi). The full assessment is in `PORTING_FROM_PYPBOY.md` — short version: none of
pypboy's Python code ports directly (different language, hardware class, and I/O model), but
several of its *ideas* and *data content* were reimplemented from scratch for this hardware.

The base project's "Firmware architecture" section above is now stale for this fork: there are
**7 tabs**, not 5 (`STAT/INV/DATA/MAP/SCAN/RADIO/TERM`), and three new file pairs exist alongside
the original ones - `terminal_game.h/.cpp` (the hacking minigame, #12 below), `buttons.h/.cpp`
(BOOT-button polling, #13 below), and `motion_sensor.h/.cpp` (the SCAN tab, #14 below).

What changed, in order:

10. **Fake "local area scan" on MAP** — real GPS/OSM map tiles (pypboy's approach) aren't
    feasible on this MCU/display, so the existing tilt-driven compass ring was extended with a
    radar-style sweep line (spins on its own clock, independent of tilt), a static dot-grid
    "terrain scan" texture, and a small set of named, world-fixed POIs (VLT/SNC/CMP/RUIN/OP)
    that rotate opposite to device heading like the two placeholder points did before. Still
    entirely cosmetic/tilt-based — no GPS, no magnetometer.
11. **INV/DATA data tables ported from pypboy's `settings.py`** — the INV tab now cycles
    automatically (every 4s) through WEAPONS/APPAREL/AID/MISC/AMMO categories, each with a small
    item table (name/qty/weight) plus a footer stat line, instead of one flat hardcoded list. The
    DATA tab now alternates (every 6s) between an ACTIVE QUESTS page and a PERKS page, each
    auto-highlighting one entry at a time and word-wrapping its description underneath. All text
    content was transcribed by hand from pypboy's data tables, not copied as assets.
12. **Terminal hacking minigame** (`terminal_game.h/.cpp`) — a from-scratch reimplementation of
    the classic Fallout "guess the password by likeness" game (the idea came from pypboy's
    `modules/passcode`, its code did not — that one's pygame/curses-based and MIT-licensed but
    architecturally unportable). New 6th tab, `TERM`. Because touch on this board is
    intentionally position-agnostic (see gotchas below), word selection is done differently than
    in pypboy: the highlighted candidate auto-cycles every 550ms through 10 words drawn from a
    20-word pool (all our own words, not pypboy's bundled dictionary file), and a single tap
    confirms whichever word is lit — reusing `Touch_Tapped()` exactly as already wired, no new
    touch/hardware code. 4 attempts, likeness = letter-position matches, auto-resets into a new
    puzzle ~2.5s after a win/loss. (Originally exited via long-press-power; superseded by #13.)
13. **BOOT/POWER physical-button tab navigation** (`buttons.h/.cpp`, `power.h/.cpp`) — BOOT
    (GPIO0, the standard ESP32/ESP32-S3 BOOT-strap pin, present as a labeled button on this
    board alongside the power button) now navigates to the previous tab, and a short tap of the
    power button navigates to the next tab, both working on every tab including `TERM`. This is
    now how you back out of the hacking minigame instead of the old hold-power-to-exit special
    case. `power.cpp`'s button handling was refactored from a single `Power_LongPress()` into
    `Power_PollKey()` returning `PRESS_NONE`/`PRESS_SHORT`/`PRESS_LONG`, so a short tap (navigate)
    and a >1.2s hold (backlight toggle, unchanged, now uniform across all tabs) share one
    state machine on the same pin without conflicting. Touch-tap-anywhere still works as before
    on every tab (cycle tabs, or confirm a word guess on `TERM`) — the buttons are additive.
    - **Root cause of the BOOT button "not working," found the hard way:** it wasn't a wrong
      pin. `audio.cpp`'s `i2s_pin_config_t` never set `.mck_io_num` (master clock pin) - in
      this IDF version that field silently defaults to **GPIO0** when left unset via designated
      initializers, so the moment `Audio_Init()` ran, the I2S driver claimed GPIO0 as its MCLK
      output and drove it low, permanently overriding the BOOT button input right after boot.
      Tracked down by bisection: an on-screen (not serial, to rule out USB/RTS-DTR interference
      as a confound) diagnostic printed `digitalRead(0)` after every subsystem's init call in
      `setup()` - it read `1` (correct) through `IMU_Init()` and flipped to a permanently stuck
      `0` right after `Audio_Init()`. Fix: explicitly set `.mck_io_num = I2S_PIN_NO_CHANGE` (the
      PCM5101 doesn't need an external MCLK line anyway). Several earlier hypotheses along the
      way (wrong GPIO, flaky switch contact, PC-side RTS/DTR holding the line via the serial
      monitor) were tested and ruled out one at a time with live captures rather than assumed -
      worth remembering that a "hardware" symptom that appears/disappears across builds is
      worth suspecting a specific code change for before suspecting the board itself.
      **The exact same bug existed in the main `PipBoy3000` project's `audio.cpp`** (identical
      missing `.mck_io_num`) - harmless there since nothing used GPIO0, but a landmine for later.
      Fixed there too (same one-line change) once this root cause was confirmed.
14. **MOTION SENSOR tab** (`motion_sensor.h/.cpp`) — replaced an earlier BLE-RSSI-based version
    of this tab (real `BLEDevice`/`BLEScan` code that hashed nearby MAC addresses into fake
    "creature" blips) with a fully self-contained simulation once real BLE scanning proved more
    trouble than it was worth for what's ultimately a cosmetic feature - no radio hardware
    involved at all now. On the semicircle: a MEDIC and an AMMO crate are placed once at random,
    fixed positions; 3 GHOULs and 2 SOLDIERs wander continuously, picking a new random direction
    every 3-5s and bouncing off the arena edge. When a ghoul and soldier cross paths (within
    `ENCOUNTER_R`), one is removed at random - unless the soldier had walked over the medic/ammo
    landmark first, which grants a one-shot "wins its next encounter" buff (shown as a small dot
    above its helmet, consumed on use). A soldier loss shows a 3x-scale "triangle-and-exclamation
    + random first name + LOST VITAL SIGNS" toast for 3.5s; a ghoul wandering within `ALERT_R` of
    the center shows a blinking 3x-scale "GHOUL ALERT" banner for as long as it stays close.
    Population is kept from dying out or running away via several rules, all fixed after being
    caught live on hardware:
    - Soldiers hitting 0 respawn 1; soldiers at 1-2 add 3 ghouls (capped at `MAX_GHOULS`=6) -
      both edge-triggered off a tracked "band" so they fire once per transition, not once per
      encounter while sitting in that band (the first version re-fired on almost every encounter
      once soldiers dropped to 1-2, since replenishment kept them hovering there, and ghouls ran
      away to 11 before hitting the old hard cap).
      A second bug let the ghoul cap be overshot anyway (checked once, then spawned 3
      unconditionally - 5+3=8) - fixed by checking the cap before each individual spawn.
      A third bug left the respawn tracker permanently out of sync after the very first
      auto-respawn (it recorded the *pre*-spawn band, so the tracker and the real count
      immediately disagreed and the very next 0-soldiers transition went undetected, silently
      breaking all further respawns) - fixed by recomputing the tracked band from the
      post-spawn count.
    - No ghoul/soldier encounter for 10s adds one more ghoul (capped at `MAX_GHOULS`), repeating
      every further 10s of quiet rather than firing only once.
    - Exactly 1 soldier surviving 20 continuous seconds (any count change resets the clock)
      spawns a backup soldier.
    The simulation only advances while this tab is on screen (paused otherwise, resuming from
    where it left off - `MotionSensor_OnEnter()` resets the delta-time and drought/solo-survival
    clocks so time spent on other tabs doesn't count against any of them).

Build verified with `pio run` and flashed to the physical board over COM29; all tabs, the fake
map, the ported INV/DATA content, the terminal minigame, BOOT/POWER navigation, and the motion
sensor's population-balance rules have been confirmed working live after fixes.
`platformio.ini` needed `upload_flags = --no-stub` — this board's native-USB port was dropping
the esptool connection during the stub-loader handoff at both 921600 and 115200 baud; skipping
the stub fixed it. Not yet carried over to the main `PipBoy3000` project's `platformio.ini`
(only the `audio.cpp` `mck_io_num` fix was) — worth adding there too if a future flash hangs
the same way.

## Backlog / ideas not yet implemented (fork-specific)

Proposed in conversation but not built:

- **Radiation/signal hunt** — reuse the IMU already wired for the MAP tilt-compass as an actual
  "search" mechanic: hide a random bearing, intensify the Geiger-click audio (built once, then
  removed as ambient noise - see item 5 above) as the player physically turns toward it.
- **V.A.T.S. timing/reflex hit** — a target reticle sweeps back and forth; tap to "fire" when
  it's in a highlighted zone for a bonus. Same auto-cycle-then-confirm input pattern as the
  `TERM` hacking minigame (#12), just visual instead of textual.
- **Simon-says memory game** — play back a beep sequence (`Audio_Beep` already exists), player
  taps in the same count/rhythm.
- **Vault-Tec slots/lucky dice** — trivial random-number gambling screen for caps, low-effort
  filler content.
- **Weapon 360° turntable animation** — pre-render a rotating 3D model (Blender or similar,
  offline) into a 24-36 frame sprite sheet, then blit it with the same technique already used for
  the animated Vault Boy on STAT (`GFX_BlitMono`). Real cost is asset prep (rendering the frames
  per weapon), not firmware code. Was going to be the fake substitute for pypboy's `objloader`
  3D weapon viewer (not feasible on this MCU/display - see `PORTING_FROM_PYPBOY.md`) but hasn't
  been started.

## Backlog / ideas not yet implemented (from PipDroid analysis)

- **Real radio audio** — PipDroid bundles actual looping station audio (Galaxy News Radio,
  Enclave Radio, New Vegas Radio). We could do the same: SD card + the `ESP32-audioI2S` library
  (already present in Waveshare's own demo `Arduino/libraries/` folder) to play real audio files
  through the I2S DAC we already wired for beeps.
- **Live clock on DATA tab** — the onboard PCF85063 RTC is still completely unused.
- **UI sound polish** — distinct sounds for power on/off (click), tab switch (have it), and
  item-select (don't have it yet) — matches PipDroid's `ui_pipboy_light_on/off.wav`,
  `newtab.wav`, `item_select.wav`.
- **Per-limb CONDITION popup** — we had a simple version of this before the STAT screen redesign
  around the animated character; could bring it back as a secondary view.
- A proper CRT flicker effect that's actually perceptible without hurting readability is still
  an open problem — worth revisiting with a different approach if wanted.

## Known limitations / gotchas for next time

- This PlatformIO environment resolves `espressif32` to Arduino core 2.0.17 — check before using
  any newer Arduino-ESP32 API (e.g. `ledcAttach(pin,...)`, which doesn't exist here; use
  `ledcSetup`/`ledcAttachPin` instead).
- The QMI8658 has no magnetometer — anything IMU-driven is tilt/orientation, not true heading.
- Touch is intentionally position-agnostic (tap-anywhere-cycles-tabs) — no touch coordinate
  calibration has been done.

## Fork: PipBoy3000-PiPort-LCD35

This started as a **hardware-only** fork of `PipBoy3000-PiPort`, targeting the Waveshare
ESP32-S3-Touch-LCD-3.5 instead of the 2.8" board: initially only `display.*`, `touch.*`, `imu.*`
(pins only), `power.*`, and `audio.*` were rewritten against the new board's chips, with
`ui_screens.*`, `gfx.*`, `terminal_game.*`, `motion_sensor.*`, and `buttons.*` copied over
unmodified and every hardware module keeping the 2.8" board's exact public function signatures.
That held through the bring-up fixes (items 15-18), but stopped being true once the UI was
resized to the bigger screen and RADIO grew real touch-driven controls (items 19-22 below) -
`ui_screens.*`/`terminal_game.*`/`motion_sensor.*` now have fork-specific layouts, and
`touch.h`/`audio.h`'s public APIs have grown well beyond what the 2.8" board's versions expose.

What changed, and why:

- **Display**: ST7796 instead of ST7789. The 3.5" panel's real resolution is 320x480 (portrait),
  rotated to 480x320 landscape. `gfx.cpp` needed zero changes to support the bigger canvas since
  it was already fully driven by the `LCD_WIDTH`/`LCD_HEIGHT` macros, not hardcoded numbers - only
  `ui_screens.cpp`, `terminal_game.cpp`, and `motion_sensor.cpp`'s hand-tuned pixel layouts needed
  updating (see "Filling the bigger screen" below). The ST7796 init register sequence was
  transcribed from Waveshare's own bundled Arduino_GFX library source (`Arduino_ST7796.h`'s
  `st7796_init_operations[]`) rather than guessed, replayed through our own raw-SPI writer to
  keep the same no-dependency style as the 2.8" board's `display.cpp`.
  This board's LCD reset line isn't a bare GPIO — it's wired through a TCA9554 I2C GPIO expander
  (shared on the main I2C bus), so `Display_Init()` now does an I2C register write dance
  (`LCD_ResetViaExpander()`) before touching SPI at all.
- **Touch**: FT6336 instead of CST328, and it's on the shared main I2C bus rather than a
  dedicated second bus (`Wire1` doesn't exist in this fork at all). Kept the same
  position-agnostic `Touch_Tapped()` semantics (any tap cycles tabs / confirms a `TERM` word) —
  this board's demo doesn't break out a touch interrupt pin either, so like the FT6336 register
  read is polled every loop instead of interrupt-driven, functionally equivalent to before.
- **IMU**: same QMI8658 chip, same register-level driver code, same I2C address (0x6B) — only the
  bus pins changed (now the shared main bus instead of a dedicated one). The MAP tab's
  tilt-reactive compass required no logic changes at all.
- **Power**: the single biggest change. The 2.8" board's `power.cpp` read a raw battery-ADC pin
  and drove a power-hold latch GPIO by hand; this board's AXP2101 PMU chip *is* the board's power
  tree — it supplies the rails the display and audio codec run on, and owns the physical power
  button, reporting short/long presses as I2C IRQ flags rather than a GPIO level. Hand-transcribing
  AXP2101's ~15 rail-voltage/enable registers seemed too risky to guess at (an overvolted rail can
  damage the board), so this fork depends on Waveshare's own tested `XPowersLib` (added via
  `platformio.ini` `lib_deps`) rather than reimplementing PMU register bring-up from scratch — the
  one hardware module in this fork that isn't purely hand-rolled I2C, deliberately. The exact rail
  voltages/enables in `Power_Init()` are copied verbatim from Waveshare's own demo
  (`02_axp2101_example.ino`). `Power_PollKey()` now trusts the AXP2101's own short/long
  press-IRQ classification instead of hand-timing a GPIO hold, since the PMU already debounces
  and classifies this in hardware.
- **Audio**: ES8311 codec instead of a bare PCM5101 DAC. A DAC just needs I2S data; a codec needs
  I2C register configuration (volume, clock source, resolution) before it'll pass any audio at
  all, plus a real MCLK line — the 2.8" board's `audio.cpp` deliberately left `mck_io_num` at
  `I2S_PIN_NO_CHANGE` (see item 13's root-cause writeup above) because the PCM5101 didn't need
  one; ES8311 does. Vendored Espressif's own `es8311.c/.h` driver (as bundled in Waveshare's demo,
  already adapted there to use Arduino `Wire` rather than raw ESP-IDF I2C calls, so it coexists
  cleanly with the rest of this fork's Wire-based peripherals on the same bus) directly into
  `src/` rather than pulling a separate library, since it's small and self-contained. The
  tone-generator `Audio_Beep()` itself is unchanged - same sine-wave writer over legacy
  `driver/i2s.h`, just now with a codec actually configured to convert it to analog first.
- **Not ported / out of scope**: this board's SD card slot and camera header aren't used by this
  firmware (the base project doesn't need either), and the onboard RTC remains unused as it was
  on the 2.8" board.

### Bring-up on real hardware

Flashed and iterated on the physical board (`COM31`, confirmed via `pio device list`'s
Espressif VID:PID). Several things the untested port above got wrong, found and fixed live:

15. **Display inversion** — first boot showed the background as white instead of black (and
    everything else inverted with it). This panel is IPS and Waveshare's own `Arduino_ST7796`
    instantiation passes `ips=true`; their `tftInit()` always finishes with `invertDisplay(false)`,
    which for an IPS panel resolves to sending **INVON** (0x21), not INVOFF - a command this
    fork's transcribed init sequence had dropped. Added it back at the end of `LCD_InitSequence()`.
16. **Color order (MADCTL BGR bit)** — with INVON added, saturated colors still came out wrong
    (green rendering pink/blue). Arduino_GFX's ST7796 rotation table always ORs in the BGR bit
    (0x68) for every rotation on this panel, but on real hardware that produced wrong hues.
    Dropped it in favor of plain RGB order (0x60) - the same MADCTL bits (MX|MV, no BGR) the
    sibling 2.8" board's ST7789 already uses successfully.
17. **Bit depth (COLMOD)** — even after fixing inversion and color order, detailed image content
    (the boot splash) came out streaky/wrong-colored while solid-color text was fine - a
    data-dependent symptom that first looked like a signal-integrity problem, so the SPI clock
    was dropped from 40MHz to 10MHz as a diagnostic. That didn't fix it, confirming it wasn't
    signal integrity. The real cause: this ST7796 module doesn't handle 16bpp/RGB565 (2
    bytes/pixel, COLMOD 0x55 - what the sibling ST7789 board uses, and what this fork originally
    copied) correctly over SPI. It needs 18bpp/RGB666 (3 bytes/pixel, COLMOD **0x66**) instead -
    Waveshare's own bundled header even leaves a `// 0x66` comment next to the 0x55 value it
    actually sends, evidently for exactly this reason. `display.cpp` now converts the RGB565
    framebuffer to 3-bytes-per-pixel RGB666 per scanline in `LCD_WriteRowRGB666()` before pushing.
    Once this was fixed the SPI clock was restored to 40MHz (item 17's 10MHz was never the actual
    fix, just a ruled-out hypothesis).
18. **Boot splash aspect ratio** — after the color fixes, the boot splash image was clipped
    top-and-bottom. Cause: the conversion script used a "cover" fit (scale to fill 480x320,
    crop the overflow) on source art that's a different aspect ratio (547x396, ~1.38) than the
    new frame (480x320, 1.5) - wider than the old 320x240 target (1.333) the art was originally
    cropped for, so covering it now trimmed real content off the top and bottom evenly. Switched
    to a "contain" fit (`ImageOps.pad`, scale to fit entirely inside the frame, letterbox the
    remainder) so no content is lost - costs a little black bar on the left/right instead.

### Filling the bigger screen

The initial port (items above, pre-hardware) deliberately kept the UI's logical framebuffer at
320x240 - the 2.8" board's original resolution - centered on the new 480x320 panel with a black
margin, to avoid conflating a hardware port with a UI redesign. Once the display was working
correctly on real hardware, the UI was resized to actually use the bigger screen:

- `LCD_WIDTH`/`LCD_HEIGHT` in `display.h` became 480/320 (no more letterbox offset - `gfx.cpp`
  needed no changes, see above).
- The boot splash (`intro_image.h`) was regenerated at native 480x320 from `intro.png` (see item
  18).
- Every hand-tuned pixel position/radius/line-width in `ui_screens.cpp`, `terminal_game.cpp`, and
  `motion_sensor.cpp` was rescaled by the panel's actual growth factors (1.5x width, 1.333x
  height; circular/radial elements scaled by the tighter 1.333x factor so nothing clips
  vertically) rather than left as literal 320x240-era numbers now surrounded by unused margin.
  `motion_sensor.cpp`'s arena radius/speed constants are physics parameters as much as drawing
  ones (entity positions and hit-test radii live in the same coordinate space), so speeds were
  scaled by the same factor as the arena size to keep gameplay pacing unchanged.
- Text was bumped from scale 1 to scale 2 wherever a line's width still fits the new 480px
  span - the STAT vitals block, INV's item table, DATA's quest/perk lists and wrapped
  descriptions (`drawWrapped()` gained a `scale` parameter for this), MAP's cardinal/POI labels,
  RADIO's headers, TERM's password grid and attempts counter, and SCAN's title/status line. A
  few strings are simply too long for scale 2 at this width (TERM's 35-char title and 30-char
  "tap to enter" hint would run 480-560px) and stay at scale 1 - noted inline where that's why.
  The 7-tab top chrome also stays at scale 1: at scale 2 the longest tab label needs 80px but
  480/7 tabs only get ~68px each, so bumping it would overlap adjacent tabs.
- STAT's vitals block specifically was redistributed down toward the bottom of the screen instead
  of bunched immediately under the (still native-resolution, unscaled) Vault Boy sprite, per
  direct user feedback that the resized-but-not-redesigned first pass wasn't using the extra
  vertical room.

### Tab-navigation sound and volume control

19. **Recorded tab-switch sound** — replaced the synthesized `Audio_Beep()` tone on all three
    tab-navigation triggers (touch tap, BOOT, power tap) with a real recorded clip. Converted from
    a user-supplied WAV (`tab_sfx.wav`, 44.1kHz mono) down to 16kHz mono to match `audio.cpp`'s
    fixed I2S/codec rate (resampling a 4-second clip properly would want a real sinc filter, but
    this one's ~99ms so plain linear interpolation is fine) and embedded as a plain `int16_t`
    array (`sfx_tab.h`, ~3KB) - same pattern as `intro_image.h`, no SD card needed for something
    this small. `Audio_PlayTabSound()` plays it via the same blocking `i2s_write()` loop
    `Audio_Beep()` already used; at ~99ms it blocks slightly longer than that function's own
    "keep it under ~60ms" guideline, noted inline as a known trade-off.
20. **Software volume control** — this board has no physical volume potentiometer like the 2.8"
    one; ES8311 has a real digital volume register instead, so `audio.cpp` now keeps its
    `es8311_handle_t` around (previously scoped to init only) and exposes `Audio_SetVolume()`/
    `Audio_GetVolume()` over it.
21. **First position-aware touch in this project** — every prior board (both this fork and the
    2.8" original) deliberately treated touch as "any tap, no position" (see `touch.h`'s original
    comment). Controlling volume with real -/+ buttons needed actual coordinates, so
    `Touch_TappedAt()` now reports a screen-space touch point and `Touch_PointInRect()` does
    rectangle hit-testing, for this and any future on-screen button.
    The FT6336 turned out to report raw coordinates in the panel's native **portrait** orientation
    (0-319 x, 0-479 y) rather than rotated to match this board's 480x320 landscape screen - not
    documented anywhere, found by adding a temporary debug print and reading real coordinates off
    the serial monitor while tapping known screen locations live (same "verify, don't guess"
    approach as the BOOT-button root-cause dig on the 2.8" board). Two live captures nailed both
    axes: tapping known left vs. right points showed screen-x = raw Y (direct); tapping known top
    vs. bottom points showed screen-y = inverted raw X. Both transforms live in `touch.cpp`.
    RADIO's volume buttons (`UI_RadioTapAt()` in `ui_screens.cpp`) were the first consumer - real
    rectangle hit-testing against the drawn button boxes, not just an approximate screen-half
    split.
22. **MUSIC/ALERT clips, non-blocking playback** — two user-supplied MP3s (`music.mp3` 9.5s,
    `alert.mp3` 13.3s) play from two new MUSIC/ALERT buttons on RADIO (`UI_RadioTapAt()`, extended
    to also hit-test these via the same `Touch_PointInRect()` machinery as the volume buttons -
    tapping the currently-playing clip's own button stops it). No MP3 decoder is wired into this
    firmware, so both were decoded and resampled to 16kHz mono PCM **at build time** (via a
    portable ffmpeg binary, `imageio-ffmpeg`, driven from a one-off Python script - no system
    ffmpeg install needed) and embedded the same way as `sfx_tab.h`, ~725KB combined (flash went
    from 30% to 53% of the 3MB partition - still comfortable, no SD card needed for clips this
    short).
    Unlike every other sound in this fork, these run several seconds - playing them with the same
    blocking `i2s_write()` loop `Audio_Beep()`/`Audio_PlayTabSound()` use would freeze the whole UI
    for the clip's duration.
    First attempt: an `Audio_Update()` called once per main-loop iteration that fed the I2S DMA
    queue a bit more of the active clip each time with a 0-timeout (non-blocking) `i2s_write()`,
    advancing only by however many samples were actually accepted. This **glitched audibly on
    real hardware** ("hashed"/crackling throughout playback, not just at tab switches) - the
    incremental feed rate was too exposed to jitter in the UI's own draw/SPI timing, even after
    bumping the I2S DMA buffer from 4x256 to 8x512 frames (~64ms to ~256ms) to give it more
    headroom.
    Fixed by moving playback onto its own FreeRTOS task (`streamTaskFn` in `audio.cpp`, pinned to
    core 0) that just blocks on `i2s_write(..., portMAX_DELAY)` in a loop - the I2S driver paces
    it in real time, completely decoupled from the main loop, which is the standard way to do
    sustained audio streaming under FreeRTOS rather than hand-rolling a non-blocking poll.
    `Audio_StopPlayback()` signals the task to stop and blocks briefly until it has actually
    exited (join-like), so starting a new clip while one is playing can't race the old task's
    writes. `Audio_Update()` and its main-loop call were removed entirely - nothing needs to run
    per-tick any more, playback continues in the background across tab switches on its own.

### Animated boot sequence, sourced from PIPBOY_3000

23. **Fork survey: [jejelinge/PIPBOY_3000](https://github.com/jejelinge/PIPBOY_3000)** — a
    hardware-button, `TFT_eSPI`-based Pip-Boy build (ESP32 + 480x320 panel, SHT31 temp/humidity
    sensor, DFPlayer MP3 module) with a very different architecture from this project (raw
    `.ino` sketches, no touch, no modular C++ split) but full-screen bitmap/GIF UI assets that
    are relevant here regardless. Per the user (who knows the source), the art/audio in that repo
    is original work, not extracted from the Fallout games, clearing the concern raised for
    similarly-named assets during the pypboy survey (see `PORTING_FROM_PYPBOY.md`). First item
    pulled from it: its animated boot sequence, replacing this project's static `intro_image.h`
    splash.
24. **`boot_anim.h`** — `images/INIT.h` from that repo is itself a raw GIF89a byte stream (not
    pre-decoded pixels): 480x320, 60 frames, LZW-compressed, ~307KB, meant to be decoded at
    runtime by the `AnimatedGIF` library (which is what that repo's `GIFDraw.ino` does). That's
    the right approach here too - decoding all 60 frames to raw RGB565 ahead of time (as
    `stat_anim.h` does for its 19-frame sprite) would run tens of MB, far past this board's
    3MB app partition. So `boot_anim.h` is that file's byte array reused as-is (just renamed to
    `BOOT_ANIM_DATA`/`BOOT_ANIM_LEN`, and dropped the `PROGMEM` qualifier/shim it used for
    AVR-style Arduino targets - unneeded here, matching how `stat_anim.h`/the old `intro_image.h`
    already declared their arrays as plain `static const`), and `bitbank2/AnimatedGIF@^2.2.0`
    was added as a `platformio.ini` lib dependency to decode it.
    `UI_PlayBootAnimation()` (`ui_screens.cpp`) opens it with a `GIFDRAW` callback
    (`bootGifDraw()`) that writes each decoded line straight into the existing framebuffer via
    `GFX_SetPixel()` (skipping transparent-indexed pixels, so frames that only redraw a changed
    region composite correctly over what's already there), then calls `GFX_Present()` once per
    frame - same manual-timing pattern `stat_anim.h`'s walk cycle already uses, driven here by
    `playFrame(false, &delayMs)` returning each frame's encoded delay instead of a hand-picked
    constant. Palette format is `GIF_PALETTE_RGB565_LE`, not `_BE` - this pipeline consumes
    palette entries as plain `uint16_t` RGB565 integers via bit-shifts (`GFX_SetPixel` →
    `display.cpp`'s `LCD_WriteRowRGB666()`), not a raw byte stream handed to hardware DMA, which
    is what `_BE` is for. `main.cpp`'s `setup()` now calls `UI_PlayBootAnimation()` in place of
    the old `Display_Push(INTRO_IMAGE); delay(5000);`, and `intro_image.h` was deleted (dead code
    once nothing referenced it; still regenerable from `intro.png` if ever needed again).
25. **Boot chime, `sfx_init.h`** — that repo's boot sequence also plays a sound
    (`myDFPlayer.playMp3Folder(1)`, i.e. its `mp3/0001.mp3`, a ~6s clip) right before the INIT
    GIF starts. Pulled that file and converted it the same way `music.mp3`/`alert.mp3` were
    (`imageio-ffmpeg` → 16kHz mono `s16le` → embedded `int16_t` array, ~190KB), then added
    `Audio_PlayBootSound()` to `audio.cpp` - a thin wrapper around the existing `startStream()`
    machinery, same as `Audio_PlayMusic()`/`Audio_PlayAlert()`. `UI_PlayBootAnimation()` calls it
    right before opening the GIF, so it runs on its own FreeRTOS task in parallel with the
    frame-by-frame decode/blit on the main core, rather than blocking it.
    Flash cost of both additions together: +307KB (GIF bytes) + 190KB (PCM) ≈ 497KB, bringing
    total usage from 53% to **59.8%** of the 3MB app partition - confirmed via `pio run`, still
    comfortable headroom.

### Full-screen tab backgrounds: investigated, dropped for a style-only chrome tweak

26. **Why the STAT/INV/DATA_1 backgrounds were dropped** — the plan was to reuse
    `images/STAT.h`/`INV.h`/`DATA_1.h` from PIPBOY_3000 as full-screen backgrounds behind our
    own dynamic STAT/INV/DATA content, the same way `boot_anim.h` reused `INIT.h` (all three are
    the same "raw GIF bytes, decode at runtime" format, and STAT/INV chose to fit the remaining
    ~1.21MB flash budget while RADIO's 1MB GIF didn't - see the prior conversation turn for that
    sizing). Decoding a frame from each to PNG for a visual check (not just trusting the byte
    format) showed they're not neutral backgrounds at all - they're whole baked screens from the
    actual game capture, complete with their own tab bar (`STAT INV DATA TIME RADIO`, a `TIME`
    tab we don't have, missing our `MAP/SCAN/TERM` tabs), their own subtabs, and hardcoded
    numbers that never update in the source firmware either (HP 115/115, LEVEL 6, a fixed
    `10mm Pistol` item list). Layering our own chrome and dynamic content on top would have
    produced two overlapping tab bars and mismatched data, not a clean composite - so this reuse
    was dropped in favor of a lighter style-only pass, per direct user choice when shown preview
    renders of the three images.
27. **Active-tab bracket** — the one concrete style cue pulled from those images: the active tab
    is drawn inside an open-bottom bracket (top + both side edges, hand-drawn with
    `GFX_HLine`/`GFX_VLine`, no bitmap) rather than the plain underline bar `drawChrome()` used
    before, echoing the "file folder tab" look from the reference screenshots without importing
    any of their art. `drawTabBracket()` in `ui_screens.cpp`; confirmed live on hardware.

### Animated status icons

28. **Pulsing RADS icon** — a small hand-drawn radiation-trefoil icon (center dot + 3 radiating
    blades, `GFX_DrawLine`/`GFX_FillCircle`, no bitmap) added to the chrome status row, centered
    between the existing `LV.8` and battery-% readouts. Pulses in size via `sinf(millis())` and
    sits next to a `RADS ###` reading that drifts slowly over a ~4s sine cycle rather than
    holding a static number - cosmetic only (this board has no real radiation sensor), same
    spirit as the boot sequence's "RADIATION SENSOR......OK" line and MAP's tilt-only compass.
    Visible on every tab (it's in the shared chrome, not a per-tab element).
29. **Blinking TERM cursor** — the hacking minigame's `>` selection marker (`terminal_game.cpp`)
    used to draw as a steady arrow; now blinks (350ms on / 250ms off, plain `millis() % 600`
    check) for the classic terminal-caret look, distinct from the static `>` both pypboy and
    PIPBOY_3000 use. RADIO's signal-strength bars (`screenRadio()`) were already animated
    (sine-driven bar heights, item 10 era) so needed no change here - the "RADIO barres
    clignotantes" backlog item was already satisfied.
    Both confirmed live on hardware.

### stats.gif and retiring the old text boot sequence

30. **STAT target-blip accents from `stats.gif`** — this repo's own `stats.gif` (root, 1.65MB,
    1200x675, 24 frames) had sat unused since item 6 (the earlier attempt that cropped its Vault
    Boy out, found it pixelated at 2x upscale, and switched to `vaultboywalking.gif` instead -
    what still powers the STAT sprite today). Unlike `PIPBOY_3000`'s assets, this one *is* an
    actual Fallout 4 screen capture - confirmed with the user before reusing it further, given
    it's the user's own personal/cosplay use, not redistribution. Re-examined by decoding real
    frames to PNG (not just trusting the byte format) rather than repeating the earlier
    crop-the-character attempt: found 6 small static "target bracket" pill markers flanking the
    3D-rotating Vault Boy in the source footage (a bottom one turned out to be a distinct dimmer
    fill-bar element, not a plain marker - left alone). Connected-component analysis
    (`scipy.ndimage.label` on a thresholded crop) gave exact bounding boxes for the 5 plain
    markers without eyeballing coordinates; one (45x14, `stat_blips.h`, `STAT_BLIP_DATA`) was
    converted to an 8-bit intensity strip the same way as `stat_anim.h` and reused via
    `GFX_BlitMono` at all 4 side positions (top/bottom were skipped - too close to the subtabs
    row and vitals block respectively to fit without crowding them). Confirmed live on hardware.
31. **Old text boot sequence retired** — now that `UI_PlayBootAnimation()` (item 24) plays a real
    animated boot, the original scrolling "ROBCO INDUSTRIES... WELCOME, VAULT DWELLER" text
    sequence (`UI_BootSequence()`, the project's very first boot splash - see item 4) was
    redundant framing after it. Removed entirely from `ui_screens.cpp`/`.h` and its `main.cpp`
    call, per direct user request - `setup()` now just calls `UI_PlayBootAnimation()`.

### CRT scanline effect, third attempt - this one stuck

32. Two earlier attempts at a CRT-style effect (item 7, on the original 2.8"/320x240 board) were
    both reverted: darkening every scanline directly in the framebuffer made text unreadable, and
    a backlight-PWM brightness flicker wasn't perceptible at all. Before retrying on this board, a
    git checkpoint was made first (`ad5560c`, this fork previously had no git history) specifically
    so this attempt could be cleanly rolled back if it didn't land either.
    This attempt avoids both earlier failure modes structurally, not just by retuning a constant:
    `LCD_WriteRowRGB666()` (`display.cpp`) now takes a `dim` flag and dims alternating rows to 2/3
    brightness **only in the transfer buffer**, right before the RGB565→RGB666 conversion for
    SPI - the source framebuffer (`s_fb` in `gfx.cpp`) is read, never written, so it can't
    compound. That distinction matters here specifically because of the boot GIF: it calls
    `GFX_Present()` once per decoded frame without a full `GFX_Clear()` in between (relying on
    per-frame transparency to preserve untouched pixels across frames) - dimming the framebuffer
    itself would have re-darkened those already-dimmed pixels on every subsequent frame,
    compounding toward black over the animation's 60 frames. Dimming only the transfer copy in
    `Display_Push()` sidesteps that regardless of how many times a given framebuffer state gets
    pushed. Confirmed live on hardware and well received this time - likely helped by this
    board's bigger 480x320 panel (vs. the original attempt's 320x240) giving scanlines more room
    to read as a texture rather than fighting with the font's stroke width.

### TERM tab: hacking minigame retired for live system diagnostics

33. **Why it changed** — direct user feedback: the "guess the password" hacking minigame (item
    12, `terminal_game.cpp`/`.h`) is fine for sitting down to play, but doesn't fit a prop that's
    worn/glanced at while cosplaying. Replaced with a ROBCO-styled live diagnostics dump instead -
    real uptime, battery %, volume, IMU tilt, audio playback state, free heap, free PSRAM, and
    flash usage (`ESP.getFreeHeap()`/`getHeapSize()`/`getFreePsram()`/`getPsramSize()`/
    `getSketchSize()`/`getFreeSketchSpace()`), dot-leader formatted like the old boot sequence's
    "MEMORY CHECK..........OK" for the same visual flavor. `terminal_game.cpp`/`.h` deleted
    (dead code once nothing referenced them); `main.cpp`'s TAB_TERM-specific touch handling
    (`TerminalGame_Confirm()`/`Enter()`) removed too - TERM now behaves like every other tab
    (tap-anywhere cycles). Doubles as an actual live debug view of the running firmware.
34. **Split layout + Vault-Tec emblem** — per follow-up feedback that the diagnostics-only screen
    felt static, split it into a left data column and a right graphic panel (divided by a
    `GFX_VLine`) showing a Vault-Tec logo the user supplied (`pov.png`), gently pulsing in
    brightness rather than sitting still. `GFX_BlitMono()` (`gfx.cpp`/`.h`) gained an optional
    `brightness` (0-255, default 255) parameter that scales intensity before the existing
    near-black cutoff, reusable for future pulsing sprites - `screenTerm()` drives it with a
    `sinf(millis())` cycle, the same pattern as the chrome's RADS icon (item 28).
    Converting `pov.png` needed a proper black-point, not just its darkest pixel: its navy-blue
    background isn't flat - luminance clusters at 44-53 (a subtle gradient) with sparse
    anti-aliasing up to ~138 before the white logo/text starts at 234. An initial conversion used
    the background's single darkest pixel (41) as the black point, which left most of the actual
    background around intensity 8-10 - just above `GFX_BlitMono`'s `v<4` transparency cutoff, so
    a faint dim rectangle showed behind the logo instead of true black. Re-examined via a full
    luminance histogram (not just min/max) and fixed by moving the black point to 150 (well past
    all background/AA noise, still well below the glyph's 234-255 band) - confirmed clean on
    hardware. Pre-scaled to its 200x99 on-screen size in the conversion script itself (`Pillow`
    `LANCZOS`), same "avoid runtime upscale blockiness" lesson as items 6/18.

### Known follow-ups

- `upload_port`/`monitor_port` in `platformio.ini` is set to `COM31`, confirmed for the board
  used during bring-up - may differ on a different machine/USB port.
- The MAP/SCAN screens' enlarged radii and STAT/INV/DATA/TERM's scale-2 layouts were sized by
  calculation (checked against `LCD_WIDTH`/`LCD_HEIGHT` bounds) but only spot-verified live on
  hardware for a subset of screens - worth a full pass through every tab if a layout looks off.
- The touch-to-screen calibration (item 21) was confirmed with a handful of live taps at known
  extremes (near each edge), not a dense grid - if a future button placed away from the screen
  edges/corners seems slightly off, it's worth re-checking the linear scale, not just the axis
  mapping/direction.
