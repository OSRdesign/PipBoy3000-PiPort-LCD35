#include "terminal_game.h"
#include "gfx.h"
#include <math.h>

#define TG_WORD_LEN        5
#define TG_NUM_WORDS       10
#define TG_MAX_ATTEMPTS    4
#define TG_CYCLE_MS        550
#define TG_RESULT_HOLD_MS  2500

// All entries must be exactly TG_WORD_LEN letters - our own word list
// (not copied from pypboy's bundled dictionary file).
static const char *const WORD_POOL[] = {
  "VAULT", "RADIO", "STEAM", "LEVER", "POWER", "VALVE", "ARMOR", "MEDIC",
  "CACHE", "TRACE", "PULSE", "FUSED", "GRAIN", "METAL", "GAUGE", "RELAY",
  "SCRAP", "NUKED", "ATOMS", "CRANK",
};
#define TG_POOL_SIZE (sizeof(WORD_POOL) / sizeof(WORD_POOL[0]))

enum TGStatus { TG_PLAYING, TG_WON, TG_LOST };

static const char *s_candidates[TG_NUM_WORDS];
static bool        s_tried[TG_NUM_WORDS];
static int8_t      s_lastLikeness[TG_NUM_WORDS];
static uint8_t     s_secretIndex;
static uint8_t     s_attemptsLeft;
static uint8_t     s_cursor;
static uint32_t    s_nextCycleAt;
static TGStatus    s_status;
static uint32_t    s_resultAt;

static int8_t likeness(const char *a, const char *b) {
  int8_t n = 0;
  for (uint8_t i = 0; i < TG_WORD_LEN; i++) if (a[i] == b[i]) n++;
  return n;
}

void TerminalGame_Enter() {
  bool used[TG_POOL_SIZE] = {false};
  for (uint8_t i = 0; i < TG_NUM_WORDS; i++) {
    uint8_t idx;
    do { idx = random(TG_POOL_SIZE); } while (used[idx]);
    used[idx] = true;
    s_candidates[i] = WORD_POOL[idx];
    s_tried[i] = false;
    s_lastLikeness[i] = -1;
  }
  s_secretIndex = random(TG_NUM_WORDS);
  s_attemptsLeft = TG_MAX_ATTEMPTS;
  s_cursor = 0;
  s_status = TG_PLAYING;
  s_nextCycleAt = millis() + TG_CYCLE_MS;
}

void TerminalGame_Confirm() {
  if (s_status != TG_PLAYING) return;
  if (s_tried[s_cursor]) return;
  s_tried[s_cursor] = true;
  if (s_cursor == s_secretIndex) {
    s_lastLikeness[s_cursor] = TG_WORD_LEN;
    s_status = TG_WON;
    s_resultAt = millis();
    return;
  }
  s_lastLikeness[s_cursor] = likeness(s_candidates[s_cursor], s_candidates[s_secretIndex]);
  s_attemptsLeft--;
  if (s_attemptsLeft == 0) {
    s_status = TG_LOST;
    s_resultAt = millis();
  }
}

static void drawAttempts(int16_t x, int16_t y) {
  char buf[24];
  snprintf(buf, sizeof(buf), "ATTEMPTS: ");
  GFX_DrawString(x, y, buf, PIP_GREEN_DIM, 2);
  int16_t cx = x + GFX_StringWidth(buf, 2);
  for (uint8_t i = 0; i < TG_MAX_ATTEMPTS; i++) {
    uint16_t c = (i < s_attemptsLeft) ? PIP_GREEN : PIP_GREEN_DIM;
    GFX_DrawString(cx, y, "!", c, 2);
    cx += 26;
  }
}

void TerminalGame_Draw() {
  // Terminal-only input model (see terminal_game.h): no touch coordinates
  // are used, so the highlight has to auto-advance for a tap to have
  // something to confirm.
  if (s_status != TG_PLAYING && millis() - s_resultAt > TG_RESULT_HOLD_MS) {
    TerminalGame_Enter();
  }
  if (s_status == TG_PLAYING && millis() >= s_nextCycleAt) {
    do { s_cursor = (s_cursor + 1) % TG_NUM_WORDS; } while (s_tried[s_cursor]);
    s_nextCycleAt = millis() + TG_CYCLE_MS;
  }

  // Title and the "tap to enter" hint run 30-35 chars - too wide for scale 2
  // (480-560px) at this screen width, so those two stay at scale 1; the
  // grid and attempts counter fit scale 2 fine.
  GFX_DrawString(9, 44, "ROBCO TERMLINK - PASSWORD REQUIRED", PIP_GREEN_DIM, 1);
  drawAttempts(9, 64);
  GFX_HLine(9, 92, 465, PIP_GREEN_DIM);

  const int16_t colX[2] = {30, 270};
  const int16_t rowY0 = 108, rowStep = 34;
  for (uint8_t i = 0; i < TG_NUM_WORDS; i++) {
    int16_t col = i / 5, row = i % 5;
    int16_t x = colX[col], y = rowY0 + row * rowStep;

    char line[16];
    if (s_tried[i]) {
      if (i == s_secretIndex && s_status == TG_WON)
        snprintf(line, sizeof(line), "%s", s_candidates[i]);
      else
        snprintf(line, sizeof(line), "%s (%d)", s_candidates[i], s_lastLikeness[i]);
    } else {
      snprintf(line, sizeof(line), "%s", s_candidates[i]);
    }

    bool highlighted = (s_status == TG_PLAYING && i == s_cursor);
    uint16_t color = highlighted ? PIP_GREEN : PIP_GREEN_DIM;

    if (highlighted) {
      // Blinking cursor, not a static arrow - the classic terminal-caret
      // look, distinct from the steady ">" pypboy/PIPBOY_3000 both use.
      bool cursorOn = (millis() % 600) < 350;
      if (cursorOn) GFX_DrawString(x - 22, y, ">", PIP_GREEN, 2);
    }
    GFX_DrawString(x, y, line, color, 2);
  }

  GFX_HLine(9, 276, 465, PIP_GREEN_DIM);
  bool blink = (millis() % 700) < 400;
  if (s_status == TG_WON) {
    GFX_DrawString(9, 284, "ACCESS GRANTED", blink ? PIP_GREEN : PIP_GREEN_DIM, 1);
  } else if (s_status == TG_LOST) {
    GFX_DrawString(9, 284, "TERMINAL LOCKED", blink ? PIP_GREEN : PIP_GREEN_DIM, 1);
    char reveal[24];
    snprintf(reveal, sizeof(reveal), "PASSWORD WAS: %s", s_candidates[s_secretIndex]);
    GFX_DrawString(9, 296, reveal, PIP_GREEN_DIM, 1);
  } else {
    GFX_DrawString(9, 284, "TAP TO ENTER HIGHLIGHTED WORD", PIP_GREEN_DIM, 1);
  }
  GFX_DrawString(9, 308, "BOOT/PWR: SWITCH TABS", PIP_GREEN_DIM, 1);
}
