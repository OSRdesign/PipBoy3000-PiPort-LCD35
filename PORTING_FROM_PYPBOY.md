# Porting Assessment: pypboy → PipBoy3000 (ESP32 fork)

Source: https://github.com/zapwizard/pypboy — a Python/pygame Pip-Boy 3000 UI built for
Raspberry Pi + a 720x720 display. Code license is MIT; several bundled asset files are a
separate concern (see below). Cloned shallow for inspection only, not vendored into this repo.

## Architecture mismatch to keep in mind

pypboy targets Pi-class hardware: pygame, background threads, live GPS/OpenStreetMap/Google
Maps tile fetching, a 3D `.obj` model viewer for weapon inspection, OGG playback with
numpy-based waveform visualization, a 720x720 canvas, and a keyboard/GPIO control scheme. It
loads all images/sounds from disk at runtime.

Our firmware is an ESP32-S3 Arduino/C++ project: no filesystem beyond flash-embedded arrays (no
SD card yet), 320x240 display, tap-to-cycle touch (no keyboard/GPIO knobs), and only an I2S tone
generator (no audio file playback yet). **None of pypboy's Python code ports directly** — every
item below is "reimplement the idea/data within our constraints," not a code port.

## High-value, low-effort: data content

`settings.py` holds plain data tables for SPECIAL stat descriptions, weapons, armor, aid items,
misc items, ammo, perks, skills, and quests — pure text/number data, trivial to transcribe into
`PROGMEM` C structs for our INV/DATA tabs (our INV tab is currently a placeholder). This is the
fastest, highest-value thing to port.

## Good candidate: terminal hacking minigame (passcode module)

`pypboy/modules/passcode/passcode.py` + `passwordgen.py` implement the classic Fallout
"guess the password by likeness" hacking game: pick N same-length words, the player guesses, the
game reports how many letters match by position ("likeness"), limited attempts before lockout.
The logic is just string comparison — no graphics-library dependency — so it would fit well as a
new tab or DATA sub-screen. We'd need our own small curated word list (not their
`google-10000-english-usa-no-swears.txt` verbatim) and a monospace grid render, which we can do
with the grayscale font blitter we already built for Monofonto.

## Good candidate: richer boot text

`pypboy/modules/boot/boot_text.py` has extended fake diagnostic/memory-discovery boot log text
(hex addresses, "CPU0 starting cell relocation", etc.) beyond what we currently scroll. It's
pure text, cheap to adapt into our existing ROBCO boot sequence for more flavor.

## Confirms our existing backlog plan: radio module

`pypboy/modules/radio/live_radio.py` (current implementation) plays folder-per-station OGG/MP3
files with a waveform visualizer; `radio.py` (older, fully commented out) was the predecessor.
This confirms "one folder per station, files inside it" is a sane structure for our own
SD-card radio backlog item — but their actual audio files are Bethesda game-audio rips (same
copyright situation as the PipDroid APK we looked at before): borrow the folder-structure idea
only, don't copy their `sounds/radio` or `sounds/pipboy` files, and source or record our own
audio if we build this.

## Not practical to port (hardware ceiling)

- Live OSM/Google Maps tile fetching + local/world map rendering (`modules/map/*`) — needs
  internet access, a tile cache, and a full 2D map renderer. Our MAP tab's cosmetic
  tilt-compass is the right scope for this hardware.
- 3D weapon inspection viewer (`objloader/`, `.obj` models for the bottlecap mine, laser
  musket) — no GPU on this MCU/display combo, not feasible.
- GPIO knob/dial input scheme (`gpio.py`) — built for physical rotary controls on a Pi case;
  not applicable to our tap-cycle touch model unless we add physical controls later.

## Needs a license/content check before any reuse

- `fonts/monofonto.ttf` — they bundle a copy too; we already sourced our own from dafont's
  personal-use distribution, no need to touch theirs.
- `sounds/pipboy/*` (`UI_PipBoy_LightOn.ogg`, `UI_PipBoy_Hum_LP.ogg`, etc.) and `sounds/radio/*`
  — filenames match Bethesda's internal sound-bank naming, almost certainly extracted from the
  game files. Same treatment as the PipDroid analysis: fine as a reference list of what UI
  sounds exist, not fine to copy the actual files into our firmware.
- `holotapes/*` (Eddie_Winter, Edwins_Terminal, Hi_Honey, System_Calibration, bedford_station)
  — flavor-text terminal entries, likely fan-transcribed or extracted game dialogue/lore.
  Reference only; write our own flavor text if we want a "read a holotape" feature.

## Suggested next step

Recommend starting with the item/perk/quest data tables (fastest win, fleshes out the INV/DATA
tabs) and/or the terminal hacking minigame (self-contained, fun, no new hardware dependencies).
Both can be built without touching the display/touch/audio drivers already working in the main
project.
