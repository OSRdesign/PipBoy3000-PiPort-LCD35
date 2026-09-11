#include "motion_sensor.h"
#include "gfx.h"
#include <math.h>
#include <string.h>

// Radii scaled ~4/3 from the 2.8" fork's 320x240 canvas to fill this
// board's bigger 480x320 one (r scaled by the height factor, the tighter of
// the two axis growth rates, so the semicircle can't clip vertically).
#define MAX_ENTITIES      14
#define ARENA_MAX_R       173.0f
#define ARENA_MIN_DY      11.0f  // stay at least this far above the flat edge
#define ENCOUNTER_R       19.0f
#define PICKUP_R          19.0f
#define ALERT_R           53.0f
#define NOTE_HOLD_MS      3500
#define ENCOUNTER_DROUGHT_MS 10000
#define SOLO_SOLDIER_BACKUP_MS 20000

enum EntKind : uint8_t { ENT_NONE = 0, ENT_GHOUL, ENT_SOLDIER, ENT_MEDIC, ENT_AMMO };

struct Entity {
  EntKind kind;
  float dx, dy;     // offset from center; dy negative = above the flat edge
  float dirDeg;     // walking direction, only meaningful for GHOUL/SOLDIER
  float speed;      // px/sec, only meaningful for GHOUL/SOLDIER
  uint32_t nextTurnAt;
  bool buffed;      // soldier only: crossed ammo/medic, wins its next ghoul encounter for free
};

static Entity s_ents[MAX_ENTITIES];
static uint32_t s_lastUpdateMs = 0;
static bool s_ghoulAlertActive = false;
static int8_t s_lastSoldierBand = -1; // 0 = none, 1 = one-or-two, 2 = three-plus
static uint32_t s_lastEncounterMs = 0;
static uint32_t s_soloSoldierSinceMs = 0; // 0 = not currently tracking (soldier count != 1)
#define MAX_GHOULS 6

static const char *const RANDOM_NAMES[] = {
  "JOHN", "JACK", "PETER", "MICHAEL", "SERGE", "MITCH", "DAVID", "STEVE", "CHRIS", "FRANK", "TOM", "ALEX",
};
#define NAME_COUNT (sizeof(RANDOM_NAMES) / sizeof(RANDOM_NAMES[0]))

#define MAX_PENDING_NOTES 3
struct LostNote { bool used; const char *name; uint32_t shownAt; };
static LostNote s_notes[MAX_PENDING_NOTES];
static int8_t s_activeNoteIdx = -1;

static float randF(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static uint8_t countKind(EntKind kind) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_ENTITIES; i++) if (s_ents[i].kind == kind) n++;
  return n;
}

static int8_t findFreeSlot() {
  for (uint8_t i = 0; i < MAX_ENTITIES; i++) if (s_ents[i].kind == ENT_NONE) return i;
  return -1;
}

static void randomPointInArena(float *outDx, float *outDy) {
  for (uint8_t tries = 0; tries < 20; tries++) {
    float dx = randF(-ARENA_MAX_R, ARENA_MAX_R);
    float dy = randF(-ARENA_MAX_R, -ARENA_MIN_DY);
    if (dx * dx + dy * dy <= ARENA_MAX_R * ARENA_MAX_R) { *outDx = dx; *outDy = dy; return; }
  }
  *outDx = 0; *outDy = -ARENA_MIN_DY;
}

static void spawnEntity(EntKind kind) {
  int8_t slot = findFreeSlot();
  if (slot < 0) return; // at capacity - keep the display from getting overcrowded
  Entity &e = s_ents[slot];
  e.kind = kind;
  e.buffed = false; // slot may be reused from a previously-removed entity
  randomPointInArena(&e.dx, &e.dy);
  if (kind == ENT_GHOUL || kind == ENT_SOLDIER) {
    e.dirDeg = randF(0, 360);
    // Speeds scaled with ARENA_MAX_R too, so crossing time (gameplay pace)
    // is unchanged even though the arena is visually bigger now.
    e.speed = (kind == ENT_GHOUL) ? randF(7, 13) : randF(11, 21);
    e.nextTurnAt = millis() + random(3000, 5001);
  }
}

static void queueSoldierLostNotification() {
  for (uint8_t i = 0; i < MAX_PENDING_NOTES; i++) {
    if (!s_notes[i].used) {
      s_notes[i].used = true;
      s_notes[i].name = RANDOM_NAMES[random(0, NAME_COUNT)];
      s_notes[i].shownAt = 0;
      return;
    }
  }
}

void MotionSensor_Init() {
  memset(s_ents, 0, sizeof(s_ents));
  memset(s_notes, 0, sizeof(s_notes));
  s_activeNoteIdx = -1;
  s_ghoulAlertActive = false;

  spawnEntity(ENT_MEDIC);
  spawnEntity(ENT_AMMO);
  spawnEntity(ENT_GHOUL);
  spawnEntity(ENT_GHOUL);
  spawnEntity(ENT_GHOUL);
  spawnEntity(ENT_SOLDIER);
  spawnEntity(ENT_SOLDIER);
  s_lastSoldierBand = 1; // matches the 2 soldiers just spawned - avoids a false trigger on tick 1
  s_lastEncounterMs = millis();
  s_soloSoldierSinceMs = 0;
}

void MotionSensor_OnEnter() {
  s_lastUpdateMs = 0; // next tick uses dt=0, so time away isn't simulated all at once
  s_lastEncounterMs = millis(); // don't let drought time accrue while the tab wasn't visible
  s_soloSoldierSinceMs = 0; // restart the solo-survival clock too, same reasoning
}

// Fires reinforcement only once per *transition* into a band, not on every
// encounter while the population sits in that band - otherwise "add 3
// ghouls while soldiers are at 1-2" refires on nearly every subsequent
// encounter (since replenishment keeps soldiers hovering at 1-2) and the
// ghoul count runs away. A hard cap is kept too as a backstop.
static int8_t soldierBandOf(uint8_t soldiers) {
  return (soldiers == 0) ? 0 : (soldiers <= 2) ? 1 : 2;
}

static void checkPopulationBalance() {
  int8_t band = soldierBandOf(countKind(ENT_SOLDIER));
  if (band == s_lastSoldierBand) return;

  if (band == 0) {
    spawnEntity(ENT_SOLDIER);
  } else if (band == 1) {
    for (uint8_t i = 0; i < 3 && countKind(ENT_GHOUL) < MAX_GHOULS; i++) spawnEntity(ENT_GHOUL);
  }

  // Recompute from the post-spawn count, not the pre-spawn one, so the next
  // transition (e.g. the replacement soldier dying again) is still detected.
  s_lastSoldierBand = soldierBandOf(countKind(ENT_SOLDIER));
}

// If the population is down to exactly one soldier and it survives 20
// continuous seconds (any change away from a count of 1 resets the clock),
// a second soldier respawns as backup.
static void checkSoloSoldierBackup() {
  uint32_t now = millis();
  if (countKind(ENT_SOLDIER) != 1) {
    s_soloSoldierSinceMs = 0;
    return;
  }
  if (s_soloSoldierSinceMs == 0) {
    s_soloSoldierSinceMs = now;
    return;
  }
  if (now - s_soloSoldierSinceMs >= SOLO_SOLDIER_BACKUP_MS) {
    spawnEntity(ENT_SOLDIER);
    s_lastSoldierBand = soldierBandOf(countKind(ENT_SOLDIER));
    s_soloSoldierSinceMs = 0;
  }
}

static void resolveEncounters() {
  for (uint8_t gi = 0; gi < MAX_ENTITIES; gi++) {
    if (s_ents[gi].kind != ENT_GHOUL) continue;
    for (uint8_t si = 0; si < MAX_ENTITIES; si++) {
      if (s_ents[si].kind != ENT_SOLDIER) continue;
      float ddx = s_ents[gi].dx - s_ents[si].dx;
      float ddy = s_ents[gi].dy - s_ents[si].dy;
      if (ddx * ddx + ddy * ddy < ENCOUNTER_R * ENCOUNTER_R) {
        s_lastEncounterMs = millis();
        bool soldierWins = s_ents[si].buffed || random(0, 2) == 1;
        s_ents[si].buffed = false; // a buff is consumed by its next encounter either way
        if (soldierWins) {
          s_ents[gi].kind = ENT_NONE;
        } else {
          queueSoldierLostNotification();
          s_ents[si].kind = ENT_NONE;
        }
        checkPopulationBalance();
        break; // this ghoul is resolved for this tick either way
      }
    }
  }
}

// Soldiers that walk over the medic or ammo landmark are buffed to
// automatically win their next ghoul encounter (one-shot, consumed on use).
// The landmarks themselves aren't consumed - any soldier can pick it up.
static void checkPickups() {
  for (uint8_t si = 0; si < MAX_ENTITIES; si++) {
    if (s_ents[si].kind != ENT_SOLDIER || s_ents[si].buffed) continue;
    for (uint8_t ii = 0; ii < MAX_ENTITIES; ii++) {
      if (s_ents[ii].kind != ENT_MEDIC && s_ents[ii].kind != ENT_AMMO) continue;
      float ddx = s_ents[si].dx - s_ents[ii].dx;
      float ddy = s_ents[si].dy - s_ents[ii].dy;
      if (ddx * ddx + ddy * ddy < PICKUP_R * PICKUP_R) {
        s_ents[si].buffed = true;
        break;
      }
    }
  }
}

static void updateSimulation() {
  uint32_t now = millis();
  float dt = (s_lastUpdateMs == 0) ? 0.0f : (now - s_lastUpdateMs) / 1000.0f;
  s_lastUpdateMs = now;

  for (uint8_t i = 0; i < MAX_ENTITIES; i++) {
    Entity &e = s_ents[i];
    if (e.kind != ENT_GHOUL && e.kind != ENT_SOLDIER) continue;

    if (now >= e.nextTurnAt) {
      e.dirDeg = randF(0, 360);
      e.nextTurnAt = now + random(3000, 5001);
    }

    float rad = e.dirDeg * (float)PI / 180.0f;
    float nx = e.dx + cosf(rad) * e.speed * dt;
    float ny = e.dy + sinf(rad) * e.speed * dt;
    if (nx * nx + ny * ny > ARENA_MAX_R * ARENA_MAX_R || ny > -ARENA_MIN_DY) {
      // hit the arena edge - turn immediately rather than walking through it
      e.dirDeg = randF(0, 360);
      e.nextTurnAt = now + random(3000, 5001);
    } else {
      e.dx = nx;
      e.dy = ny;
    }
  }

  checkPickups();
  resolveEncounters();
  checkSoloSoldierBackup();

  // No fight in a while - the horde grows. Recurs every further drought
  // period (not just once), capped at MAX_GHOULS.
  if (now - s_lastEncounterMs > ENCOUNTER_DROUGHT_MS) {
    if (countKind(ENT_GHOUL) < MAX_GHOULS) spawnEntity(ENT_GHOUL);
    s_lastEncounterMs = now;
  }

  bool anyClose = false;
  for (uint8_t i = 0; i < MAX_ENTITIES; i++) {
    if (s_ents[i].kind != ENT_GHOUL) continue;
    if (s_ents[i].dx * s_ents[i].dx + s_ents[i].dy * s_ents[i].dy < ALERT_R * ALERT_R) { anyClose = true; break; }
  }
  s_ghoulAlertActive = anyClose;

  if (s_activeNoteIdx < 0) {
    for (uint8_t i = 0; i < MAX_PENDING_NOTES; i++) {
      if (s_notes[i].used) { s_activeNoteIdx = i; s_notes[i].shownAt = now; break; }
    }
  } else if (now - s_notes[s_activeNoteIdx].shownAt > NOTE_HOLD_MS) {
    s_notes[s_activeNoteIdx].used = false;
    s_activeNoteIdx = -1;
  }
}

// --- tiny procedural icons, ~12px tall, drawn at (cx,cy) center ---
static void drawIconGhoul(int16_t cx, int16_t cy, uint16_t c) {
  GFX_FillCircle(cx, cy - 6, 3, c);
  GFX_DrawLine(cx, cy - 3, cx, cy + 3, c);
  GFX_DrawLine(cx, cy - 2, cx - 5, cy - 6, c);  // raised left arm
  GFX_DrawLine(cx, cy - 2, cx + 4, cy - 9, c);  // raised higher right arm (shambling)
  GFX_DrawLine(cx, cy + 3, cx - 3, cy + 8, c);
  GFX_DrawLine(cx, cy + 3, cx + 3, cy + 8, c);
}

static void drawIconSoldier(int16_t cx, int16_t cy, uint16_t c) {
  GFX_FillCircle(cx, cy - 6, 3, c);
  GFX_HLine(cx - 4, cy - 9, 9, c); // helmet brim
  GFX_FillRect(cx - 2, cy - 3, 4, 6, c);
  GFX_DrawLine(cx - 2, cy - 1, cx - 5, cy + 1, c);
  GFX_DrawLine(cx + 2, cy - 1, cx + 5, cy + 1, c);
  GFX_DrawLine(cx - 1, cy + 3, cx - 2, cy + 8, c);
  GFX_DrawLine(cx + 1, cy + 3, cx + 2, cy + 8, c);
}

static void drawIconMedic(int16_t cx, int16_t cy, uint16_t c) {
  GFX_FillRect(cx - 1, cy - 4, 2, 8, c);
  GFX_FillRect(cx - 4, cy - 1, 8, 2, c);
}

static void drawIconAmmo(int16_t cx, int16_t cy, uint16_t c) {
  GFX_FillRect(cx - 2, cy - 2, 4, 7, c);
  GFX_FillRect(cx - 1, cy - 5, 2, 3, c);
}

static void drawWarningIcon(int16_t cx, int16_t cy, uint16_t c, uint8_t scale) {
  GFX_DrawLine(cx, cy - 6 * scale, cx - 6 * scale, cy + 5 * scale, c);
  GFX_DrawLine(cx, cy - 6 * scale, cx + 6 * scale, cy + 5 * scale, c);
  GFX_DrawLine(cx - 6 * scale, cy + 5 * scale, cx + 6 * scale, cy + 5 * scale, c);
  GFX_DrawLine(cx, cy - 2 * scale, cx, cy + 1 * scale, c);
  GFX_FillCircle(cx, cy + 3 * scale, scale, c);
}

static void drawSemicircleArc(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  for (int16_t deg = 180; deg <= 360; deg += 2) {
    float rad = deg * (float)PI / 180.0f;
    int16_t x = cx + (int16_t)roundf(r * cosf(rad));
    int16_t y = cy + (int16_t)roundf(r * sinf(rad));
    GFX_SetPixel(x, y, color);
  }
}

void MotionSensor_Draw() {
  updateSimulation();

  const int16_t cx = LCD_WIDTH / 2, cy = 293;

  GFX_DrawString(9, 44, "MOTION SENSOR", PIP_GREEN_DIM, 2);

  GFX_FillRect(0, 68, LCD_WIDTH, 143, PIP_BLACK);
  int16_t noteY = 71;
  if (s_ghoulAlertActive) {
    bool blink = (millis() % 600) < 350;
    const char *msg = "GHOUL ALERT";
    int16_t w = GFX_StringWidth(msg, 3);
    GFX_DrawString((LCD_WIDTH - w) / 2, noteY, msg, blink ? PIP_GREEN : PIP_GREEN_DIM, 3);
    noteY += 40;
  }
  if (s_activeNoteIdx >= 0) {
    drawWarningIcon(30, noteY + 19, PIP_GREEN, 3);
    GFX_DrawString(69, noteY, s_notes[s_activeNoteIdx].name, PIP_GREEN, 3);
    noteY += 40;
    GFX_DrawString(9, noteY, "LOST VITAL", PIP_GREEN, 3);
    noteY += 35;
    GFX_DrawString(9, noteY, "SIGNS", PIP_GREEN, 3);
  }

  const int16_t bandR[3] = {60, 120, 173};
  for (uint8_t i = 0; i < 3; i++) drawSemicircleArc(cx, cy, bandR[i], PIP_GREEN_DIM);
  GFX_FillCircle(cx, cy, 3, PIP_GREEN); // player, at the flat edge/center

  for (uint8_t i = 0; i < MAX_ENTITIES; i++) {
    Entity &e = s_ents[i];
    if (e.kind == ENT_NONE) continue;
    int16_t px = cx + (int16_t)roundf(e.dx);
    int16_t py = cy + (int16_t)roundf(e.dy);
    switch (e.kind) {
      case ENT_GHOUL:   drawIconGhoul(px, py, PIP_GREEN); break;
      case ENT_SOLDIER:
        drawIconSoldier(px, py, PIP_GREEN);
        if (e.buffed) GFX_FillCircle(px, py - 12, 2, PIP_GREEN); // buffed: wins its next encounter
        break;
      case ENT_MEDIC:   drawIconMedic(px, py, PIP_GREEN); break;
      case ENT_AMMO:    drawIconAmmo(px, py, PIP_GREEN); break;
      default: break;
    }
  }

  char status[32];
  snprintf(status, sizeof(status), "GHOULS:%d SOLDIERS:%d", countKind(ENT_GHOUL), countKind(ENT_SOLDIER));
  GFX_DrawString(9, 298, status, PIP_GREEN_DIM, 2);
}
