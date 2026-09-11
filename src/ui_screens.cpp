#include "ui_screens.h"
#include "gfx.h"
#include "display.h"
#include "stat_anim.h"
#include "stat_blips.h"
#include "boot_anim.h"
#include "vaulttec_logo.h"
#include "motion_sensor.h"
#include "audio.h"
#include "touch.h"
#include <AnimatedGIF.h>
#include <math.h>
#include <string.h>

static const char *TAB_LABELS[TAB_COUNT] = {"STAT", "INV", "DATA", "MAP", "SCAN", "RADIO", "TERM"};

static void drawWrapped(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t maxChars, uint8_t scale) {
  char buf[160];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  char line[72] = "";
  int16_t cy = y;
  int16_t lineStep = 8 * scale + 8;
  char *word = strtok(buf, " ");
  while (word) {
    char test[72];
    if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
    else snprintf(test, sizeof(test), "%s", word);

    if (strlen(test) > maxChars) {
      GFX_DrawString(x, cy, line, color, scale);
      cy += lineStep;
      strncpy(line, word, sizeof(line) - 1);
      line[sizeof(line) - 1] = 0;
    } else {
      strncpy(line, test, sizeof(line) - 1);
      line[sizeof(line) - 1] = 0;
    }
    word = strtok(nullptr, " ");
  }
  if (line[0]) GFX_DrawString(x, cy, line, color, scale);
}

// ---------- boot animation (boot_anim.h, played once at startup) ----------
static AnimatedGIF s_bootGif;

static void bootGifDraw(GIFDRAW *pDraw) {
  int16_t y = pDraw->iY + pDraw->y;
  if (y < 0 || y >= LCD_HEIGHT) return;
  int16_t x0 = pDraw->iX;
  int16_t width = pDraw->iWidth;
  if (x0 + width > LCD_WIDTH) width = LCD_WIDTH - x0;
  const uint8_t *src = pDraw->pPixels;
  const uint16_t *pal = pDraw->pPalette;
  if (pDraw->ucHasTransparency) {
    uint8_t trans = pDraw->ucTransparent;
    for (int16_t i = 0; i < width; i++) {
      uint8_t idx = src[i];
      if (idx != trans) GFX_SetPixel(x0 + i, y, pal[idx]);
    }
  } else {
    for (int16_t i = 0; i < width; i++) GFX_SetPixel(x0 + i, y, pal[src[i]]);
  }
}

void UI_PlayBootAnimation() {
  GFX_Clear(PIP_BLACK);
  GFX_Present();

  Audio_PlayBootSound();

  // LE, not BE: our palette values are consumed as plain uint16_t RGB565
  // ints (GFX_SetPixel/display.cpp extract R/G/B via bit-shifts on the
  // integer, no raw-byte SPI handoff), which is what LE gives on this
  // little-endian MCU. BE is for hardware that wants pre-swapped bytes
  // memcpy'd straight to a SPI/DMA buffer, which isn't our pipeline.
  s_bootGif.begin(GIF_PALETTE_RGB565_LE);
  if (s_bootGif.open((uint8_t *)BOOT_ANIM_DATA, BOOT_ANIM_LEN, bootGifDraw)) {
    int delayMs = 0;
    while (s_bootGif.playFrame(false, &delayMs)) {
      GFX_Present();
      if (delayMs > 0) delay(delayMs);
    }
    s_bootGif.close();
  }
}

// ---------- corner bezel accents ----------
static void drawCornerBrackets() {
  const int16_t len = 14; // was 10 at 320x240, scaled for the bigger panel
  uint16_t c = PIP_GREEN_DIM;
  GFX_HLine(2, 2, len, c);              GFX_VLine(2, 2, len, c);
  GFX_HLine(LCD_WIDTH - 2 - len, 2, len, c); GFX_VLine(LCD_WIDTH - 3, 2, len, c);
  GFX_HLine(2, LCD_HEIGHT - 3, len, c); GFX_VLine(2, LCD_HEIGHT - 2 - len, len, c);
  GFX_HLine(LCD_WIDTH - 2 - len, LCD_HEIGHT - 3, len, c);
  GFX_VLine(LCD_WIDTH - 3, LCD_HEIGHT - 2 - len, len, c);
}

// Open-bottom bracket around the active tab's label - top + side edges only,
// its bottom implied by the divider line drawn right under it - the "file
// folder tab" look on the real game's PIPBOY_3000-style tab bar (see
// DEVLOG.md), hand-drawn with our own primitives rather than importing that
// project's bitmaps (which turned out to be whole baked screens, not plain
// backgrounds - see DEVLOG for why that reuse was dropped).
static void drawTabBracket(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  GFX_HLine(x, y, w, color);
  GFX_VLine(x, y, h, color);
  GFX_VLine(x + w - 1, y, h, color);
}

// Small radiation-trefoil icon (center dot + 3 radiating blades), pulsing in
// size - not a real sensor reading (this board has none), same cosmetic
// spirit as the boot sequence's "RADIATION SENSOR......OK" line and MAP's
// tilt-only compass.
static void drawRadIcon(int16_t cx, int16_t cy, uint16_t color) {
  float t = millis() / 260.0f;
  int16_t r = 2 + (int16_t)((0.5f + 0.5f * sinf(t)) * 1.5f);
  GFX_FillCircle(cx, cy, r, color);
  for (uint8_t i = 0; i < 3; i++) {
    float ang = (float)i * (2.0f * PI / 3.0f) - PI / 2.0f;
    int16_t bx = cx + (int16_t)(cosf(ang) * 7.0f);
    int16_t by = cy + (int16_t)(sinf(ang) * 7.0f);
    GFX_DrawLine(cx, cy, bx, by, color);
    GFX_FillCircle(bx, by, 2, color);
  }
}

// ---------- chrome (top tab bar + status row) ----------
static void drawChrome(PipTab tab, uint8_t batteryPercent) {
  const int16_t tabW = LCD_WIDTH / TAB_COUNT;
  for (uint8_t i = 0; i < TAB_COUNT; i++) {
    int16_t x = i * tabW;
    bool active = (i == (uint8_t)tab);
    uint16_t color = active ? PIP_GREEN : PIP_GREEN_DIM;
    int16_t tw = GFX_StringWidth(TAB_LABELS[i], 1);
    int16_t tx = x + (tabW - tw) / 2;
    GFX_DrawString(tx, 5, TAB_LABELS[i], color, 1);
    if (active) drawTabBracket(tx - 5, 1, tw + 10, 16, PIP_GREEN);
  }
  GFX_HLine(0, 18, LCD_WIDTH, PIP_GREEN_DIM);

  char lvl[8] = "LV.8";
  GFX_DrawString(6, 22, lvl, PIP_GREEN, 1);

  char batStr[6];
  snprintf(batStr, sizeof(batStr), "%3d%%", batteryPercent);
  GFX_DrawString(LCD_WIDTH - GFX_StringWidth(batStr, 1) - 6, 22, batStr, PIP_GREEN, 1);

  // Cosmetic RADS readout, centered - drifts gently instead of sitting
  // static, same "no real sensor, but reads alive" spirit as MAP's compass.
  int16_t rad = 3 + (int16_t)(sinf(millis() / 4000.0f) * 2.5f);
  char radStr[10];
  snprintf(radStr, sizeof(radStr), "RADS %03d", rad);
  int16_t radX = (LCD_WIDTH - GFX_StringWidth(radStr, 1) - 14) / 2;
  drawRadIcon(radX + 5, 26, PIP_GREEN);
  GFX_DrawString(radX + 14, 22, radStr, PIP_GREEN, 1);

  GFX_HLine(0, 32, LCD_WIDTH, PIP_GREEN_DIM);
  drawCornerBrackets();
}

// ---------- STAT ----------
static void drawStatBar(int16_t x, int16_t y, int16_t w, float frac, uint16_t color) {
  GFX_DrawRect(x, y, w, 14, color);
  int16_t fillW = (int16_t)((w - 4) * frac);
  if (fillW > 0) GFX_FillRect(x + 2, y + 2, fillW, 10, color);
}

static void screenStat() {
  // subtabs, like the real STATUS/SPECIAL/PERKS row (decorative, STATUS active)
  const char *subtabs[] = {"STATUS", "SPECIAL", "PERKS"};
  int16_t sx = 72;
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t c = (i == 0) ? PIP_GREEN : PIP_GREEN_DIM;
    GFX_DrawString(sx, 44, subtabs[i], c, 2);
    sx += GFX_StringWidth(subtabs[i], 2) + 24;
  }

  // walking Vault Boy, animated at native resolution (no upscale blockiness)
  static uint8_t s_frame = 0;
  static uint32_t s_nextFrameAt = 0;
  if (millis() >= s_nextFrameAt) {
    s_frame = (s_frame + 1) % STAT_ANIM_FRAMES;
    s_nextFrameAt = millis() + STAT_ANIM_DELAY_MS[s_frame];
  }
  const uint8_t *frameData = &STAT_ANIM_DATA[(uint32_t)s_frame * STAT_ANIM_W * STAT_ANIM_H];
  int16_t sprX = (LCD_WIDTH - STAT_ANIM_W) / 2;
  int16_t sprY = 72;
  // Kept at native resolution (no upscaling) even on the bigger panel - a
  // stretched blit would go blocky (see DEVLOG.md item 6).
  GFX_BlitMono(sprX, sprY, frameData, STAT_ANIM_W, STAT_ANIM_H, 1);

  // Target-blip accents flanking the sprite, cropped from stats.gif (see
  // stat_blips.h) - static (the source footage's own blips don't move or
  // animate either, just the 3D model they surround), placed clear of the
  // subtabs row above and the vitals block below.
  GFX_BlitMono(sprX - 54, sprY + 35, STAT_BLIP_DATA, STAT_BLIP_W, STAT_BLIP_H, 1);
  GFX_BlitMono(sprX - 54, sprY + 95, STAT_BLIP_DATA, STAT_BLIP_W, STAT_BLIP_H, 1);
  GFX_BlitMono(sprX + STAT_ANIM_W + 10, sprY + 35, STAT_BLIP_DATA, STAT_BLIP_W, STAT_BLIP_H, 1);
  GFX_BlitMono(sprX + STAT_ANIM_W + 10, sprY + 95, STAT_BLIP_DATA, STAT_BLIP_W, STAT_BLIP_H, 1);

  // Vitals block, at 2x text now that there's room, spread down to the
  // bottom of the screen instead of bunched up right under the sprite -
  // this panel has ~100px of headroom below the (native-res, unscaled)
  // sprite that the 2.8" board's 320x240 layout never had.
  int16_t vy = 224;
  GFX_DrawString(9, vy, "EQUIPPED: 10MM PISTOL", PIP_GREEN, 2);
  vy += 24;
  GFX_DrawString(9, vy, "ARMOR: COMBAT ARMOR", PIP_GREEN, 2);
  vy += 24;
  GFX_DrawString(9, vy, "STIMPAK (4)   RADAWAY (2)", PIP_GREEN, 2);
  vy += 24;

  GFX_DrawString(9, vy, "HP 86/100", PIP_GREEN, 2);
  GFX_DrawString(200, vy, "LEVEL 8", PIP_GREEN, 2);
  drawStatBar(340, vy + 1, 120, 0.86f, PIP_GREEN);
}

// ---------- INV ----------
// Item data transcribed (not code-ported) from zapwizard/pypboy's
// settings.py WEAPONS/ARMOR/AID/MISC/AMMO tables - plain data, no assets.
struct InvItem { const char *name; const char *qty; const char *wt; };
struct InvCategory { const char *label; const InvItem *items; uint8_t count; const char *footer; };

static const InvItem WEAPONS_ITEMS[] = {
  {"10MM PISTOL",     "x1", "3.5"},
  {"COMBAT KNIFE",    "x1", "1.0"},
  {"LASER MUSKET",    "x1", "12.6"},
  {"FRAG GRENADE",    "x2", "0.5"},
  {"BOTTLECAP MINE",  "x1", "0.5"},
};
static const InvItem APPAREL_ITEMS[] = {
  {"VAULT JUMPSUIT",  "-", "1.0"},
  {"COMBAT ARMOR",    "-", "9.0"},
  {"ARMY HELMET",     "-", "1.0"},
  {"WEDDING RING",    "-", "0.0"},
};
static const InvItem AID_ITEMS[] = {
  {"STIMPAK",         "x4", "0.1"},
  {"RADAWAY",         "x2", "0.1"},
  {"PURIFIED WATER",  "x3", "0.5"},
};
static const InvItem MISC_ITEMS[] = {
  {"BOBBY PIN",       "x12",  "0.0"},
  {"DUCT TAPE",       "x2",   "0.1"},
  {"PRE-WAR MONEY",   "x250", "0.0"},
};
static const InvItem AMMO_ITEMS[] = {
  {"10MM ROUNDS",     "x24", "0.2"},
  {"FUSION CELLS",    "x18", "0.1"},
};

static const InvCategory INV_CATEGORIES[] = {
  {"WEAPONS", WEAPONS_ITEMS, sizeof(WEAPONS_ITEMS) / sizeof(WEAPONS_ITEMS[0]), "WEIGHT 18.1/200   CAPS: 35"},
  {"APPAREL", APPAREL_ITEMS, sizeof(APPAREL_ITEMS) / sizeof(APPAREL_ITEMS[0]), "WEIGHT 11.0/200   DMG RESIST: 12"},
  {"AID",     AID_ITEMS,     sizeof(AID_ITEMS) / sizeof(AID_ITEMS[0]),         "WEIGHT 0.7/200    HEALTH: 86/100"},
  {"MISC",    MISC_ITEMS,    sizeof(MISC_ITEMS) / sizeof(MISC_ITEMS[0]),       "WEIGHT 0.1/200    CAPS: 35"},
  {"AMMO",    AMMO_ITEMS,    sizeof(AMMO_ITEMS) / sizeof(AMMO_ITEMS[0]),       "WEIGHT 0.3/200"},
};
#define INV_CATEGORY_COUNT (sizeof(INV_CATEGORIES) / sizeof(INV_CATEGORIES[0]))

static void screenInv() {
  static uint8_t catIdx = 0;
  static uint32_t nextSwitch = 0;
  if (millis() >= nextSwitch) {
    catIdx = (catIdx + 1) % INV_CATEGORY_COUNT;
    nextSwitch = millis() + 4000;
  }
  const InvCategory &cat = INV_CATEGORIES[catIdx];

  GFX_DrawString(9, 44, cat.label, PIP_GREEN_DIM, 2);
  GFX_DrawString(9, 80, "ITEM", PIP_GREEN, 2);
  GFX_DrawString(320, 80, "QTY", PIP_GREEN, 2);
  GFX_DrawString(410, 80, "WT", PIP_GREEN, 2);
  GFX_HLine(9, 104, 465, PIP_GREEN_DIM);

  int16_t y = 124;
  for (uint8_t i = 0; i < cat.count; i++) {
    GFX_DrawString(9, y, cat.items[i].name, PIP_GREEN, 2);
    GFX_DrawString(320, y, cat.items[i].qty, PIP_GREEN, 2);
    GFX_DrawString(410, y, cat.items[i].wt, PIP_GREEN, 2);
    y += 30;
  }

  GFX_HLine(9, 270, 465, PIP_GREEN_DIM);
  // Footer strings run up to 33 chars - too wide for scale 2 (528px), so
  // this one stays at scale 1 to avoid running off the right edge.
  GFX_DrawString(9, 290, cat.footer, PIP_GREEN_DIM, 1);
}

// ---------- DATA ----------
// Quest/perk descriptions transcribed (not code-ported) from
// zapwizard/pypboy's settings.py QUESTS/PERKS tables - plain text data.
struct Quest { const char *title; const char *desc; };
static const Quest QUESTS[] = {
  {"FIND THE WATER CHIP",  "VAULT 13'S WATER CHIP HAS FAILED. LOCATE A REPLACEMENT BEFORE THE RESERVE RUNS DRY."},
  {"LOCATE VAULT 15",      "RUMORED TO HOLD SPARE PARTS AND SURVIVORS. LAST KNOWN POSITION IS SOUTHWEST OF HERE."},
  {"RETURN TO VAULT 13",   "DELIVER THE WATER CHIP TO THE OVERSEER BEFORE THE 150-DAY DEADLINE EXPIRES."},
  {"TALK TO THE OVERSEER", "REPORT FINDINGS FROM THE WASTELAND AND RECEIVE FURTHER INSTRUCTIONS."},
};
#define QUEST_COUNT (sizeof(QUESTS) / sizeof(QUESTS[0]))

struct Perk { const char *name; uint8_t rank; const char *desc; };
static const Perk PERKS[] = {
  {"GUNSLINGER",     1, "NON-AUTOMATIC PISTOLS DO 20% MORE DAMAGE."},
  {"AWARENESS",      1, "VIEW A TARGET'S DAMAGE RESISTANCES IN V.A.T.S."},
  {"RIFLEMAN",       1, "NON-AUTOMATIC RIFLES DO DOUBLE DAMAGE AND IGNORE 30% OF ARMOR."},
  {"HACKER",         1, "TERMINALS NEVER LOCK YOU OUT WHEN HACKING FAILS."},
  {"LONE WANDERER",  1, "30% LESS DAMAGE AND +100 CARRY WEIGHT WHEN ADVENTURING SOLO."},
};
#define PERK_COUNT (sizeof(PERKS) / sizeof(PERKS[0]))

static void screenData() {
  static uint8_t page = 0; // 0 = quests, 1 = perks
  static uint32_t nextPage = 0;
  if (millis() >= nextPage) {
    page = 1 - page;
    nextPage = millis() + 6000;
  }

  int16_t y = 92;
  if (page == 0) {
    GFX_DrawString(9, 44, "ACTIVE QUESTS", PIP_GREEN_DIM, 2);
    GFX_HLine(9, 76, 465, PIP_GREEN_DIM);
    uint8_t hi = (millis() / 2500) % QUEST_COUNT;
    for (uint8_t i = 0; i < QUEST_COUNT; i++) {
      char line[40];
      snprintf(line, sizeof(line), "> %s", QUESTS[i].title);
      GFX_DrawString(9, y, line, (i == hi) ? PIP_GREEN : PIP_GREEN_DIM, 2);
      y += 26;
    }
    y += 10;
    GFX_HLine(9, y, 465, PIP_GREEN_DIM);
    y += 16;
    drawWrapped(9, y, QUESTS[hi].desc, PIP_GREEN, 28, 2);
  } else {
    GFX_DrawString(9, 44, "PERKS", PIP_GREEN_DIM, 2);
    GFX_HLine(9, 76, 465, PIP_GREEN_DIM);
    uint8_t hi = (millis() / 2500) % PERK_COUNT;
    for (uint8_t i = 0; i < PERK_COUNT; i++) {
      char line[32];
      snprintf(line, sizeof(line), "%s (RANK %d)", PERKS[i].name, PERKS[i].rank);
      GFX_DrawString(9, y, line, (i == hi) ? PIP_GREEN : PIP_GREEN_DIM, 2);
      y += 26;
    }
    y += 10;
    GFX_HLine(9, y, 465, PIP_GREEN_DIM);
    y += 16;
    drawWrapped(9, y, PERKS[hi].desc, PIP_GREEN, 28, 2);
  }
}

// ---------- MAP ----------
static void rotatePoint(int16_t cx, int16_t cy, float px, float py, float rad,
                         int16_t *outX, int16_t *outY) {
  float rx = (px - cx) * cosf(rad) - (py - cy) * sinf(rad);
  float ry = (px - cx) * sinf(rad) + (py - cy) * cosf(rad);
  *outX = cx + (int16_t)roundf(rx);
  *outY = cy + (int16_t)roundf(ry);
}

// Fake "local area scan" - there's no GPS/magnetometer on this hardware, so
// this isn't a real map: the ring/POIs turn with IMU tilt (as before) and a
// radar-style sweep line spins on its own clock for a "live scanning" feel.
// (See PORTING_FROM_PYPBOY.md - real GPS/OSM map tiles like pypboy's MAP
// module aren't feasible on this MCU/display, so this is the cosmetic
// substitute mentioned there.)
struct MapPOI { int16_t dx, dy; const char *label; };
static const MapPOI MAP_POIS[] = {
  { 63, -47, "VLT" },  // Vault
  { -86, 31, "SNC" },  // Sanctuary
  { 23, 94,  "CMP" },  // Camp
  { -47, -86, "RUIN" },// Ruins
  { 109, 16, "OP" },   // Outpost
};
#define MAP_POI_COUNT (sizeof(MAP_POIS) / sizeof(MAP_POIS[0]))

static void drawTerrainGrid(int16_t cx, int16_t cy, int16_t r) {
  for (int16_t gx = cx - r; gx <= cx + r; gx += 20) {
    for (int16_t gy = cy - r; gy <= cy + r; gy += 20) {
      int16_t dx = gx - cx, dy = gy - cy;
      if (dx * dx + dy * dy <= r * r) GFX_SetPixel(gx, gy, PIP_GREEN_DIM);
    }
  }
}

static void drawRadarSweep(int16_t cx, int16_t cy, int16_t r) {
  float sweepDeg = fmodf(millis() / 12.0f, 360.0f); // ~4.3s per revolution
  float srad = sweepDeg * (float)PI / 180.0f;
  int16_t ex = cx + (int16_t)(r * sinf(srad));
  int16_t ey = cy - (int16_t)(r * cosf(srad));
  GFX_DrawLine(cx, cy, ex, ey, PIP_GREEN_DIM);
}

static void screenMap(float headingDeg) {
  const int16_t cx = LCD_WIDTH / 2, cy = 160, r = 120;
  float rad = -headingDeg * (float)PI / 180.0f;

  drawTerrainGrid(cx, cy, r);

  // concentric rings are rotationally symmetric, no need to rotate them
  GFX_DrawCircle(cx, cy, r, PIP_GREEN_DIM);
  GFX_DrawCircle(cx, cy, r * 2 / 3, PIP_GREEN_DIM);
  GFX_DrawCircle(cx, cy, r / 3, PIP_GREEN_DIM);

  drawRadarSweep(cx, cy, r);

  // crosshair + cardinal labels rotate with device tilt
  int16_t nx, ny, sx, sy, ex, ey, wx, wy;
  rotatePoint(cx, cy, cx, cy - r, rad, &nx, &ny);
  rotatePoint(cx, cy, cx, cy + r, rad, &sx, &sy);
  rotatePoint(cx, cy, cx + r, cy, rad, &ex, &ey);
  rotatePoint(cx, cy, cx - r, cy, rad, &wx, &wy);
  GFX_DrawLine(nx, ny, sx, sy, PIP_GREEN_DIM);
  GFX_DrawLine(ex, ey, wx, wy, PIP_GREEN_DIM);

  GFX_DrawString(nx - 6, ny - 18, "N", PIP_GREEN, 2);
  GFX_DrawString(sx - 6, sy + 4, "S", PIP_GREEN, 2);
  GFX_DrawString(ex + 4, ey - 8, "E", PIP_GREEN, 2);
  GFX_DrawString(wx - 20, wy - 8, "W", PIP_GREEN, 2);

  // points of interest, world-fixed, rotate opposite to device heading
  for (uint8_t i = 0; i < MAP_POI_COUNT; i++) {
    int16_t px, py;
    rotatePoint(cx, cy, cx + MAP_POIS[i].dx, cy + MAP_POIS[i].dy, rad, &px, &py);
    GFX_FillRect(px - 3, py - 3, 7, 7, PIP_GREEN);
    GFX_DrawString(px + 8, py - 6, MAP_POIS[i].label, PIP_GREEN, 2);
  }

  GFX_FillCircle(cx, cy, 5, PIP_GREEN); // player, always fixed at center

  char hdg[12];
  snprintf(hdg, sizeof(hdg), "HDG: %3d", (int)headingDeg);
  GFX_DrawString(cx - GFX_StringWidth(hdg, 2) / 2, cy + r + 12, hdg, PIP_GREEN_DIM, 2);
}

// ---------- RADIO ----------
// On-screen -/+ volume buttons. Left box = -5%, right box = +5%, clamped.
#define VOL_BTN_Y  90
#define VOL_BTN_H  36
#define VOL_BTN_W  70
#define VOL_BTN_L_X  9
#define VOL_BTN_R_X  (LCD_WIDTH - 9 - VOL_BTN_W)

// MUSIC/ALERT playback buttons, below the volume row.
#define CLIP_BTN_Y  158
#define CLIP_BTN_H  32
#define CLIP_BTN_W  150
#define MUSIC_BTN_X  60
#define ALERT_BTN_X  270

enum RadioClip { RADIO_CLIP_NONE, RADIO_CLIP_MUSIC, RADIO_CLIP_ALERT };
static RadioClip s_radioClip = RADIO_CLIP_NONE; // which clip UI_RadioTapAt() last started, for highlighting

// screenX/screenY come from Touch_TappedAt() (see touch.h/.cpp for the
// raw-to-screen calibration, confirmed live on real hardware). A tap
// outside all four boxes is a no-op - this tab intentionally has no other
// touch behavior. Tapping the currently-playing clip's own button stops it.
void UI_RadioTapAt(int16_t screenX, int16_t screenY) {
  bool hitMinus = Touch_PointInRect(screenX, screenY, VOL_BTN_L_X, VOL_BTN_Y, VOL_BTN_W, VOL_BTN_H);
  bool hitPlus = Touch_PointInRect(screenX, screenY, VOL_BTN_R_X, VOL_BTN_Y, VOL_BTN_W, VOL_BTN_H);
  if (hitMinus || hitPlus) {
    int16_t v = (int16_t)Audio_GetVolume() + (hitPlus ? 5 : -5);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    Audio_SetVolume((uint8_t)v);
    Audio_PlayTabSound(); // audible confirmation at the new level
    return;
  }

  bool hitMusic = Touch_PointInRect(screenX, screenY, MUSIC_BTN_X, CLIP_BTN_Y, CLIP_BTN_W, CLIP_BTN_H);
  bool hitAlert = Touch_PointInRect(screenX, screenY, ALERT_BTN_X, CLIP_BTN_Y, CLIP_BTN_W, CLIP_BTN_H);
  if (hitMusic) {
    if (s_radioClip == RADIO_CLIP_MUSIC && Audio_IsPlaying()) {
      Audio_StopPlayback();
      s_radioClip = RADIO_CLIP_NONE;
    } else {
      Audio_PlayMusic();
      s_radioClip = RADIO_CLIP_MUSIC;
    }
  } else if (hitAlert) {
    if (s_radioClip == RADIO_CLIP_ALERT && Audio_IsPlaying()) {
      Audio_StopPlayback();
      s_radioClip = RADIO_CLIP_NONE;
    } else {
      Audio_PlayAlert();
      s_radioClip = RADIO_CLIP_ALERT;
    }
  }
}

static void drawClipButton(int16_t x, const char *label, bool active) {
  uint16_t c = active ? PIP_GREEN : PIP_GREEN_DIM;
  GFX_DrawRect(x, CLIP_BTN_Y, CLIP_BTN_W, CLIP_BTN_H, c);
  int16_t tw = GFX_StringWidth(label, 2);
  GFX_DrawString(x + (CLIP_BTN_W - tw) / 2, CLIP_BTN_Y + (CLIP_BTN_H - 16) / 2, label, c, 2);
}

static void screenRadio() {
  if (!Audio_IsPlaying()) s_radioClip = RADIO_CLIP_NONE; // clip finished on its own

  GFX_DrawString(9, 44, "GALAXY NEWS RADIO", PIP_GREEN, 2);
  GFX_DrawString(9, 68, "FREQ: 98.7 MHZ", PIP_GREEN, 2);

  GFX_DrawRect(VOL_BTN_L_X, VOL_BTN_Y, VOL_BTN_W, VOL_BTN_H, PIP_GREEN);
  GFX_DrawString(VOL_BTN_L_X + (VOL_BTN_W - 8 * 3) / 2, VOL_BTN_Y + (VOL_BTN_H - 8 * 3) / 2,
                 "-", PIP_GREEN, 3);
  GFX_DrawRect(VOL_BTN_R_X, VOL_BTN_Y, VOL_BTN_W, VOL_BTN_H, PIP_GREEN);
  GFX_DrawString(VOL_BTN_R_X + (VOL_BTN_W - 8 * 3) / 2, VOL_BTN_Y + (VOL_BTN_H - 8 * 3) / 2,
                 "+", PIP_GREEN, 3);

  char volStr[6];
  snprintf(volStr, sizeof(volStr), "%d%%", Audio_GetVolume());
  int16_t vw = GFX_StringWidth(volStr, 3);
  GFX_DrawString((LCD_WIDTH - vw) / 2, VOL_BTN_Y + 2, volStr, PIP_GREEN, 3);
  drawStatBar(140, VOL_BTN_Y + VOL_BTN_H + 6, 200, Audio_GetVolume() / 100.0f, PIP_GREEN);

  drawClipButton(MUSIC_BTN_X, "MUSIC", s_radioClip == RADIO_CLIP_MUSIC);
  drawClipButton(ALERT_BTN_X, "ALERT", s_radioClip == RADIO_CLIP_ALERT);

  const int16_t barCount = 20, baseY = 270, x0 = 15, spacing = 23, maxH = 68;
  for (int16_t i = 0; i < barCount; i++) {
    float phase = millis() / 180.0f + i * 0.7f;
    float h = (sinf(phase) * 0.5f + 0.5f) * maxH;
    GFX_FillRect(x0 + i * spacing, baseY - (int16_t)h, 14, (int16_t)h, PIP_GREEN);
  }

  GFX_HLine(15, baseY + 10, 450, PIP_GREEN_DIM);
  float t = (sinf(millis() / 1000.0f) * 0.5f + 0.5f);
  int16_t markerX = 15 + (int16_t)(t * 450);
  GFX_FillRect(markerX - 3, baseY + 2, 7, 16, PIP_GREEN);

  GFX_DrawString(9, 302, "-/+: VOLUME   BOOT/PWR: SWITCH TABS", PIP_GREEN_DIM, 1);
}

// ---------- TERM (live system diagnostics) ----------
// Used to be a Fallout-style "guess the password" hacking minigame, but per
// direct user feedback that didn't fit a prop meant to be worn/used while
// cosplaying (a minigame wants you sitting there playing it, not glancing at
// it) - replaced with a ROBCO-styled dump of real hardware state instead.
// Doubles as an actual debug view of the running firmware.
#define DIAG_COL_X     9
#define DIAG_DIVIDER_X 236
#define DIAG_LOGO_X    255

static void drawDiagLine(int16_t y, const char *label, const char *value) {
  char line[40];
  int16_t total = 27; // chars - fits DIAG_COL_X..DIAG_DIVIDER_X at scale 1,
                       // dot-leader padded like the old boot sequence's
                       // "MEMORY CHECK..........OK"
  int16_t labelLen = (int16_t)strlen(label), valueLen = (int16_t)strlen(value);
  int16_t dots = total - labelLen - valueLen;
  if (dots < 3) dots = 3;
  strncpy(line, label, sizeof(line) - 1);
  line[sizeof(line) - 1] = 0;
  int16_t pos = labelLen;
  for (int16_t i = 0; i < dots && pos < (int16_t)sizeof(line) - 1; i++) line[pos++] = '.';
  line[pos] = 0;
  strncat(line, value, sizeof(line) - strlen(line) - 1);
  GFX_DrawString(DIAG_COL_X, y, line, PIP_GREEN, 1);
}

static void screenTerm(uint8_t batteryPercent, float headingDeg) {
  GFX_DrawString(9, 44, "ROBCO INDUSTRIES (TM) SYSTEM DIAGNOSTICS", PIP_GREEN, 1);
  GFX_HLine(9, 58, 465, PIP_GREEN_DIM);
  GFX_VLine(DIAG_DIVIDER_X, 66, 220, PIP_GREEN_DIM);

  char val[24];
  int16_t y = 74;
  const int16_t step = 20;

  uint32_t upSec = millis() / 1000;
  snprintf(val, sizeof(val), "%02lu:%02lu:%02lu", (unsigned long)(upSec / 3600),
           (unsigned long)((upSec / 60) % 60), (unsigned long)(upSec % 60));
  drawDiagLine(y, "UPTIME", val); y += step;

  snprintf(val, sizeof(val), "%d%%", batteryPercent);
  drawDiagLine(y, "BATTERY", val); y += step;

  snprintf(val, sizeof(val), "%d%%", Audio_GetVolume());
  drawDiagLine(y, "VOLUME", val); y += step;

  snprintf(val, sizeof(val), "%+.1f DEG", headingDeg);
  drawDiagLine(y, "TILT", val); y += step;

  drawDiagLine(y, "AUDIO", Audio_IsPlaying() ? "PLAYING" : "IDLE");
  y += step + 8;

  snprintf(val, sizeof(val), "%u/%uKB", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(ESP.getHeapSize() / 1024));
  drawDiagLine(y, "FREE HEAP", val); y += step;

  snprintf(val, sizeof(val), "%u/%uKB", (unsigned)(ESP.getFreePsram() / 1024),
           (unsigned)(ESP.getPsramSize() / 1024));
  drawDiagLine(y, "FREE PSRAM", val); y += step;

  snprintf(val, sizeof(val), "%u/%uKB", (unsigned)(ESP.getSketchSize() / 1024),
           (unsigned)((ESP.getSketchSize() + ESP.getFreeSketchSpace()) / 1024));
  drawDiagLine(y, "FLASH USED", val); y += step;

  // Vault-Tec emblem on the right, gently pulsing (per user feedback that
  // the diagnostics-only layout felt too static) rather than sitting still.
  uint8_t glow = 160 + (uint8_t)(95.0f * (0.5f + 0.5f * sinf(millis() / 900.0f)));
  int16_t logoY = 66 + (220 - VAULTTEC_LOGO_H) / 2;
  GFX_BlitMono(DIAG_LOGO_X, logoY, VAULTTEC_LOGO_DATA, VAULTTEC_LOGO_W, VAULTTEC_LOGO_H, 1, glow);

  GFX_HLine(9, 288, 465, PIP_GREEN_DIM);
  GFX_DrawString(9, 300, "SYSTEM NOMINAL", PIP_GREEN_DIM, 1);
}

void UI_DrawFrame(PipTab tab, uint8_t batteryPercent, float headingDeg) {
  GFX_Clear(PIP_BLACK);
  drawChrome(tab, batteryPercent);
  switch (tab) {
    case TAB_STAT:  screenStat();  break;
    case TAB_INV:   screenInv();   break;
    case TAB_DATA:  screenData();  break;
    case TAB_MAP:   screenMap(headingDeg); break;
    case TAB_SCAN:  MotionSensor_Draw(); break;
    case TAB_RADIO: screenRadio(); break;
    case TAB_TERM:  screenTerm(batteryPercent, headingDeg); break;
    default: break;
  }
  GFX_Present();
}
