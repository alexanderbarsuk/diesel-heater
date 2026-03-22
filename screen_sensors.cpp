#include "screen_sensors.h"
#include "heater.h"
#include "faults.h"
#include "uaprint.h"
#include "encoder.h"
#include "config.h"
#include "timer.h"
#include "background.h"
#include <math.h>
#include <avr/pgmspace.h>
#include <Fonts/FreeSans9pt7b.h>   // ~10px digit height — для оверлея температур
#include "FreeSans7pt7b.h"         // ~7px digit height — для вент/насос і env блоків

// ─── Розмітка (320×240) ──────────────────────────────────────────────────────
// y=0..135   — фонове зображення (background.h, 136px)
// y=136..191 — вентилятор + насос з піктограмами (56px, centered)
// y=192..215 — стан обігрівача + таймер (24px)
// y=216..239 — шкала потужності full-width (24px, самий низ)

#define BOT_BORDER_Y 140  // 1px роздільна лінія над блоками
#define BOT_Y        140  // рядок вент+насос
#define BOT_H         52
#define STATE_Y    192    // текст стану + таймер
#define STATE_H     24
#define STAT_Y     216    // шкала потужності (самий низ)
#define STAT_H      24
#define STAT_BAR_Y (STAT_Y + STAT_H - 3)   // = 237, прогрес-смужка утримання
#define BLK_W      160    // ширина одного блоку (половина екрану)
#define BOT_SIDE_W  115                       // ширина колонки вент/насос
#define BOT_MID_X   BOT_SIDE_W               // початок центральної колонки
#define BOT_MID_W   (320 - 2 * BOT_SIDE_W)  // ширина центральної колонки (160px)
#define BOT_PUMP_X  (320 - BOT_SIDE_W)      // початок колонки насоса

#define STAT_BG    ST77XX_BLACK
#define TEMP_BG    tft.color565(12, 12, 28)
#define BOT_BG     tft.color565(10, 10, 10)
#define DIVCLR     tft.color565(45, 45, 70)
#define LABEL_CLR  tft.color565(120, 120, 120)

// Шкала потужності: 10 прямокутників після тексту стану
#define BAR_X       4     // x першого сегменту (full-width)
#define BAR_SEG_W  29     // ширина сегменту: 10*29+9*2=308px, x=4..312
#define BAR_SEG_G   2     // проміжок між сегментами
#define BAR_SEG_H  16     // висота сегменту
#define BAR_Y     (STAT_Y + (STAT_H - BAR_SEG_H) / 2)   // = 164
// Загальна ширина шкали: 10*29 + 9*2 = 308px
#define BAR_W     (10 * BAR_SEG_W + 9 * BAR_SEG_G)

// Блок: 6cp мітка + 6cp значення
// label: x = bx+4..bx+76 (6×12)
// value: x = bx+84..bx+156 (6×12), right-aligned
#define LABEL_CP    6
#define VAL_CP      6
#define VAL_X_OFF  (4 + LABEL_CP * UA_ADVANCE)   // = 76 → value starts at bx+76... wait
// Actually right-aligned: value starts at bx + BLK_W - 4 - VAL_CP * UA_ADVANCE
//   = 160 - 4 - 72 = 84. Gap between label(ends 76) and value(starts 84) = 8px.

// Буфер рядка фонового зображення (320 пікселів × 2 байти = 640 байт SRAM)
static uint16_t _bgRowBuf[BG_IMG_W];

// ─── Накладки зверху (на фоновому зображенні, y=4..27) ───────────────────────
// Ліворуч: напруга 5cp ("12.3В"), праворуч: статус 5cp
#define OVL_Y  14     // верхній край
#define OVL_H  24     // висота (baseline = OVL_Y+20)
#define OVL_BL (OVL_Y + 20)
#define OVL_BG  tft.color565(0, 0, 20)
// 5cp × 12px = 60px + 4px margin = rect 68px wide
// Ліво: x=0..67; Право: x=252..319
#define OVL_W   68

static void drawVoltageOverlay(float v) {
  char buf[8];
  dtostrf(v, 4, 1, buf);   // " 9.9" або "12.5" (4 ASCII chars)
  buf[4] = 'B'; buf[5] = '\0';   // "12.5B" = 5cp, все ASCII
  tft.fillRect(0, OVL_Y, OVL_W, OVL_H, OVL_BG);
  tft.setCursor(4, OVL_BL);
  printUAn(buf, 0, 5, ST77XX_WHITE, OVL_BG);
}

// ─── Відновлення прямокутника фонового зображення ────────────────────────────
static void redrawBgRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  for (int16_t y = y0; y <= y1; y++) {
    uint_farptr_t base;
    int16_t rowInPart;
    if      (y <  34) { base = pgm_get_far_address(backgroundBitmap0); rowInPart = y;        }
    else if (y <  68) { base = pgm_get_far_address(backgroundBitmap1); rowInPart = y - 34;   }
    else if (y < 102) { base = pgm_get_far_address(backgroundBitmap2); rowInPart = y - 68;   }
    else              { base = pgm_get_far_address(backgroundBitmap3); rowInPart = y - 102;  }
    memcpy_PF(_bgRowBuf, base + (uint_farptr_t)rowInPart * BG_IMG_W * 2, (size_t)BG_IMG_W * 2);
    tft.startWrite();
    tft.setAddrWindow(x0, y, x1 - x0 + 1, 1);
    for (int16_t i = x0; i <= x1; i++) tft.writeColor(_bgRowBuf[i], 1);
    tft.endWrite();
  }
}

// ─── Оверлей температури (FreeSans9pt7b ~10px, прозорий фон) ─────────────────
// x, bl — лівий край та baseline тексту
static void drawTemperatureOverlay(float t, int16_t x, int16_t bl) {
  // FreeSans9pt7b не має ° (0xB0), малюємо його як коло через drawCircle
  redrawBgRect(x, bl - 15, x + 53, bl + 3);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(x, bl);
  if (isnan(t) || t < -100.0f || t > 9999.0f) {
    tft.print("???");
  } else {
    tft.print((int16_t)(t + 0.5f));
  }
  int16_t cx = tft.getCursorX();
  tft.drawCircle(cx + 3, bl - 10, 2, ST77XX_WHITE);
  tft.setCursor(cx + 7, bl);
  tft.print("C");
  tft.setFont(nullptr);
}

static void drawStatusOverlay() {
  const char* txt;
  uint16_t    clr;
  if (faultsCount() > 0) {
    FaultEntry last = faultGet(faultsCount() - 1);
    txt = faultShortName(last.code);
    clr = ST77XX_RED;
  } else if (heater.state == HEATER_PRIMING) {
    txt = "ПРОК ";
    clr = tft.color565(0, 200, 255);
  } else if (heater.state == HEATER_RUNNING  ||
             heater.state == HEATER_STARTING ||
             heater.state == HEATER_RESTARTING) {
    txt = "RUN";
    clr = ST77XX_GREEN;
  } else {
    txt = " ОК";
    clr = tft.color565(0, 220, 0);
  }
  tft.fillRect(320 - OVL_W, OVL_Y, OVL_W, OVL_H, OVL_BG);
  tft.setCursor(320 - OVL_W + 4, OVL_BL);
  printUAn(txt, 0, 5, clr, OVL_BG);
}

static int      lastEncoderPos;
static bool     lastSW      = HIGH;
static uint32_t swPressMs   = 0;     // millis натискання (0 = не натиснуто)
static uint32_t lastProgMs  = 0;
static bool     powerEditMode = false;
static uint32_t powerEditMs   = 0;   // millis() останньої дії в режимі редагування

// ─── Стан → колір ────────────────────────────────────────────────────────────
static uint16_t stateColor(HeaterState s) {
  switch (s) {
    case HEATER_OFF:         return tft.color565(100, 100, 100);
    case HEATER_STARTING:    return ST77XX_YELLOW;
    case HEATER_RUNNING:     return ST77XX_GREEN;
    case HEATER_STOPPING:    return tft.color565(255, 128, 0);
    case HEATER_FAULT:       return ST77XX_RED;
    case HEATER_PRIMING:     return tft.color565(0, 200, 255);
    case HEATER_RESTARTING:  return tft.color565(255, 200, 0);
  }
  return ST77XX_WHITE;
}

// ─── Тільки шкала потужності (без тексту стану) ──────────────────────────────
static void drawPowerBar() {
  bool active = (heater.state == HEATER_RUNNING  ||
                 heater.state == HEATER_STARTING ||
                 heater.state == HEATER_RESTARTING);
  uint16_t onClr  = powerEditMode ? ST77XX_YELLOW
                  : (active ? tft.color565(0, 190, 0) : tft.color565(80, 80, 0));
  uint16_t offClr = tft.color565(30, 30, 30);
  for (uint8_t i = 0; i < 10; i++) {
    uint16_t clr = (i < heater.power) ? onClr : offClr;
    tft.fillRect(BAR_X + i * (BAR_SEG_W + BAR_SEG_G), BAR_Y, BAR_SEG_W, BAR_SEG_H, clr);
  }
  // Рамка навколо шкали: жовта в режимі редагування, прозора інакше
  uint16_t borderClr = powerEditMode ? ST77XX_YELLOW : STAT_BG;
  tft.drawRect(BAR_X - 2, BAR_Y - 1, BAR_W + 4, BAR_SEG_H + 2, borderClr);
}

// ─── Стан + таймер (в одному рядку) + шкала потужності ──────────────────────
static void drawStatePower(bool force = false) {
  static HeaterState prevState    = (HeaterState)0xFF;
  static bool        prevTimerEn  = false;
  static bool        prevTimerRun = false;
  static uint32_t    prevTimerRem = 0xFFFFFFFFUL;

  bool     timerEn  = timerIsEnabled();
  bool     timerRun = timerIsRunning();
  uint32_t timerRem = timerRemaining();

  bool stateChanged = force || (heater.state != prevState);
  bool timerChanged = force || (timerEn != prevTimerEn) || (timerRun != prevTimerRun)
                     || (timerRun && timerRem != prevTimerRem);

  if (!stateChanged && !timerChanged) return;

  prevState    = heater.state;
  prevTimerEn  = timerEn;
  prevTimerRun = timerRun;
  prevTimerRem = timerRem;

  // Рядок стану: текст стану зліва, таймер справа (якщо ввімкнено)
  tft.fillRect(0, STATE_Y, 320, STATE_H, STAT_BG);

  static const char* stateStr[] = {
    "ЗУПИНЕНО",   // HEATER_OFF        = 0
    "ЗАПУСКАЄ",   // HEATER_STARTING   = 1
    "РОБОТА  ",   // HEATER_RUNNING    = 2
    "ЗУПИНКА ",   // HEATER_STOPPING   = 3
    "ПОМИЛКА ",   // HEATER_FAULT      = 4
    "ПРОКАЧКА",   // HEATER_PRIMING    = 5
    "ПЕРЕЗАП.",   // HEATER_RESTARTING = 6
  };
  uint8_t si = (heater.state <= HEATER_RESTARTING) ? (uint8_t)heater.state : 0;
  tft.setCursor(4, STATE_Y + 20);
  printUAn(stateStr[si], 0, 8, stateColor(heater.state), STAT_BG);

  if (timerEn) {
    char buf[14];
    uint16_t clr = timerRun ? tft.color565(0, 220, 80) : tft.color565(80, 80, 80);
    if (timerRem >= 3600UL) {
      uint8_t h = (uint8_t)(timerRem / 3600UL);
      uint8_t m = (uint8_t)((timerRem % 3600UL) / 60UL);
      uint8_t s = (uint8_t)(timerRem % 60UL);
      snprintf(buf, sizeof(buf), "%2u:%02u:%02u", h, m, s);
    } else {
      uint8_t m = (uint8_t)(timerRem / 60UL);
      uint8_t s = (uint8_t)(timerRem % 60UL);
      snprintf(buf, sizeof(buf), "   %02u:%02u", m, s);
    }
    tft.setCursor(320 - 4 - 8 * UA_ADVANCE, STATE_Y + 20);
    printUAn(buf, 0, 8, clr, STAT_BG);
  }

  // Шкала потужності (лише якщо стан змінився)
  if (stateChanged) {
    tft.fillRect(0, STAT_Y, 320, STAT_H - 3, STAT_BG);
    drawPowerBar();
  }
}

// ─── Прогрес-смужка утримання (3px, y=STAT_BAR_Y) ───────────────────────────
static void drawToggleProgress(uint32_t held) {
  if (held < 500) {
    tft.fillRect(0, STAT_BAR_Y, 320, 3, STAT_BG);
    return;
  }
  uint16_t w = (uint16_t)((held - 500) * 320UL / 1500UL);
  if (w > 320) w = 320;
  tft.fillRect(0, STAT_BAR_Y, w,       3, tft.color565(255, 110, 0));
  tft.fillRect(w, STAT_BAR_Y, 320 - w, 3, STAT_BG);
}

// ─── Температурний блок (160×26px) ───────────────────────────────────────────
// label: 6cp нормалізовано; value: 6cp right-aligned
// label ends at bx+76, value starts at bx+84 (8px gap)
static void drawTempBlock(int16_t bx, int16_t by, float t, const char* label) {
  tft.fillRect(bx, by, BLK_W, 26, TEMP_BG);
  int16_t bl = by + 20;

  // Мітка (6cp, сірий)
  tft.setCursor(bx + 4, bl);
  printUAn(label, 0, LABEL_CP, LABEL_CLR, TEMP_BG);

  // Значення (6cp, білий) — right-aligned: x = bx+160-4-6*12 = bx+84
  char val[10];
  if (isnan(t) || t < -100.0f || t > 9999.0f) {
    strcpy(val, "???°C");   // 6cp: 3 spaces + ? + °C
  } else {
    snprintf(val, 10, "%4.0f°C", t);   // e.g. " 320°C", 6cp
  }
  tft.setCursor(bx + BLK_W - 4 - VAL_CP * UA_ADVANCE, bl);
  printUA(val, ST77XX_WHITE, TEMP_BG);
}

// Порівняння з точністю до 1°
static bool tempDiff(float a, float b) {
  bool na = isnan(a) || a < -100.0f || a > 9999.0f;
  bool nb = isnan(b) || b < -100.0f || b > 9999.0f;
  if (na != nb) return true;
  if (na && nb) return false;
  return (int16_t)a != (int16_t)b;
}



// ─── Піктограма вентилятора (5 лопатей, статична) ───────────────────────────
// Кожух r=9; лопать: inner r=3 → outer r=7, sweep 35°
// Координати inner/outer для 5 лопатей (θ=0,72,144,216,288°)
static const int8_t FAN_IX[5] PROGMEM = { 3,  1, -2, -2,  1};
static const int8_t FAN_IY[5] PROGMEM = { 0,  3,  2, -2, -3};
static const int8_t FAN_TX[5] PROGMEM = { 6, -2, -7, -2,  6};
static const int8_t FAN_TY[5] PROGMEM = { 4,  7,  0, -7, -4};

static void drawIconFan(int16_t cx, int16_t cy, uint16_t color) {
  tft.drawCircle(cx, cy, 9, color);                    // кожух
  for (uint8_t i = 0; i < 5; i++) {
    int16_t ix = cx + (int8_t)pgm_read_byte(&FAN_IX[i]);
    int16_t iy = cy + (int8_t)pgm_read_byte(&FAN_IY[i]);
    int16_t tx = cx + (int8_t)pgm_read_byte(&FAN_TX[i]);
    int16_t ty = cy + (int8_t)pgm_read_byte(&FAN_TY[i]);
    tft.drawLine(ix, iy, tx, ty, color);               // лопать
    tft.drawLine(ix, iy, tx + (ty > cy ? 1 : (ty < cy ? -1 : 0)),
                         ty + (tx < cx ? 1 : (tx > cx ? -1 : 0)), color); // ширина
  }
  tft.drawCircle(cx, cy, 3, color);                    // маточина (кільце)
  tft.fillCircle(cx, cy, 2, color);                    // маточина (центр)
}

// ─── Піктограма насоса (відцентровий, 4 лопаті, статична) ───────────────────
// Корпус r=8; лопать: inner r=2 → outer r=5, sweep 25°
static const int8_t PUMP_IX[4] PROGMEM = { 2,  0, -2,  0};
static const int8_t PUMP_IY[4] PROGMEM = { 0,  2,  0, -2};
static const int8_t PUMP_TX[4] PROGMEM = { 5, -2, -5,  2};
static const int8_t PUMP_TY[4] PROGMEM = { 2,  5, -2, -5};

static void drawIconPump(int16_t cx, int16_t cy, uint16_t color) {
  tft.drawCircle(cx, cy, 8, color);                    // корпус (зовнішній)
  tft.drawCircle(cx, cy, 7, color);                    // корпус (товщина стінки)
  tft.fillRect(cx-2, cy-11, 5, 4, color);              // вхідний патрубок (зверху)
  tft.fillRect(cx+7, cy-2,  4, 5, color);              // вихідний патрубок (праворуч)
  for (uint8_t i = 0; i < 4; i++) {
    int16_t ix = cx + (int8_t)pgm_read_byte(&PUMP_IX[i]);
    int16_t iy = cy + (int8_t)pgm_read_byte(&PUMP_IY[i]);
    int16_t tx = cx + (int8_t)pgm_read_byte(&PUMP_TX[i]);
    int16_t ty = cy + (int8_t)pgm_read_byte(&PUMP_TY[i]);
    tft.drawLine(ix, iy, tx, ty, color);               // лопать
    tft.drawLine(ix + (iy > cy ? 1 : (iy < cy ? -1 : 0)),
                 iy + (ix < cx ? 1 : (ix > cx ? -1 : 0)),
                 tx, ty, color);                       // ширина
  }
  tft.fillCircle(cx, cy, 2, color);                    // маточина
}

// ─── Центральна колонка: температура/вологість/тиск (AHT20+BMP280) ───────────
static void drawEnvBlock(bool force = false) {
  static float pTemp = 1e9f, pHum = 1e9f, pPres = 1e9f;
  float t = heater.ambientTemp, h = heater.humidity, p = heater.pressure;
  bool changed = force
    || (int16_t)t    != (int16_t)pTemp  || (isnan(t) != isnan(pTemp))
    || (int16_t)h    != (int16_t)pHum   || (isnan(h) != isnan(pHum))
    || (int16_t)p    != (int16_t)pPres  || (isnan(p) != isnan(pPres));
  if (!changed) return;
  pTemp = t; pHum = h; pPres = p;

  tft.fillRect(BOT_MID_X, BOT_Y, BOT_MID_W, BOT_H, BOT_BG);
  tft.drawFastVLine(BOT_MID_X,  BOT_Y, BOT_H, DIVCLR);
  tft.drawFastVLine(BOT_PUMP_X, BOT_Y, BOT_H, DIVCLR);

  tft.setFont(&FreeSans7pt7b);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  int16_t cx = BOT_MID_X + BOT_MID_W / 2;
  // 3 рядки: ascent~10px, рівномірно у блоці 56px
  int16_t bls[3] = { (int16_t)(BOT_Y + 13), (int16_t)(BOT_Y + 31), (int16_t)(BOT_Y + 49) };

  auto printMid = [&](const char* s, int16_t bl) {
    int16_t tx, ty; uint16_t tw, th;
    tft.getTextBounds(s, 0, 0, &tx, &ty, &tw, &th);
    tft.setCursor(cx - (int16_t)(tw / 2), bl);
    tft.print(s);
  };

  // Рядок 1: температура з символом °C
  if (!isnan(t)) {
    char num[6]; itoa((int16_t)(t + 0.5f), num, 10);
    int16_t tx, ty; uint16_t tw, th;
    tft.getTextBounds(num, 0, 0, &tx, &ty, &tw, &th);
    int16_t sx = cx - (int16_t)((tw + 12) / 2);  // 12px = gap+circle(5)+gap+C(5)
    tft.setCursor(sx, bls[0]);
    tft.print(num);
    int16_t ax = tft.getCursorX() + 1;
    tft.drawCircle(ax + 2, bls[0] - 9, 2, ST77XX_WHITE);  // superscript °
    tft.setCursor(ax + 6, bls[0]);
    tft.print("C");
  } else { printMid("---", bls[0]); }

  // Рядок 2: вологість
  if (!isnan(h)) {
    char buf[6]; snprintf(buf, sizeof(buf), "%d%%", (int16_t)(h + 0.5f));
    printMid(buf, bls[1]);
  } else { printMid("---", bls[1]); }

  // Рядок 3: тиск
  if (!isnan(p)) {
    char buf[10]; snprintf(buf, sizeof(buf), "%dhPa", (int16_t)(p + 0.5f));
    printMid(buf, bls[2]);
  } else { printMid("---", bls[2]); }
}

// ─── Рядок вентилятор (ліво) + насос (право) ─────────────────────────────────
// Іконка зверху, нижче: фактичні оберти, потім (ШІМ%)
static void drawBottom(bool force = false) {
  static uint8_t  pFanPwm   = 0xFF;
  static uint8_t  pPumpPwm  = 0xFF;
  static uint16_t pFanRpm   = 0xFFFF;
  static uint16_t pPumpRpm  = 0xFFFF;
  bool changedFan  = force || (heater.fanPwm  != pFanPwm)  || (heater.fanRpm  != pFanRpm);
  bool changedPump = force || (heater.pumpPwm != pPumpPwm) || (heater.pumpRpm != pPumpRpm);

  int16_t icY  = BOT_Y + 19;
  int16_t fanCx  = BOT_SIDE_W / 2;
  int16_t pumpCx = BOT_PUMP_X + BOT_SIDE_W / 2;

  auto drawSide = [&](uint8_t pwm, uint16_t rpm, int16_t blockX, int16_t blockCx) {
    tft.fillRect(blockX, BOT_Y, BOT_SIDE_W, BOT_H, BOT_BG);
    tft.setFont(&FreeSans7pt7b);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u (%u%%)", rpm, (uint16_t)(pwm * 100u / 255u));
    int16_t tx, ty; uint16_t tw, th;
    tft.getTextBounds(buf, 0, 0, &tx, &ty, &tw, &th);
    tft.setCursor(blockCx - (int16_t)(tw / 2), BOT_Y + BOT_H - 5);
    tft.print(buf);
  };

  if (changedFan) {
    pFanPwm = heater.fanPwm; pFanRpm = heater.fanRpm;
    drawSide(heater.fanPwm, heater.fanRpm, 0, fanCx);
    drawIconFan(fanCx, icY, LABEL_CLR);
  }

  if (changedPump) {
    pPumpPwm = heater.pumpPwm; pPumpRpm = heater.pumpRpm;
    drawSide(heater.pumpPwm, heater.pumpRpm, BOT_PUMP_X, pumpCx);
    tft.drawFastVLine(BOT_PUMP_X, BOT_Y, BOT_H, DIVCLR);
    drawIconPump(pumpCx, icY, LABEL_CLR);
  }

  drawEnvBlock(force);
  tft.drawFastHLine(0, BOT_BORDER_Y,     320, DIVCLR); // бордер зверху
  tft.drawFastHLine(0, BOT_Y + BOT_H,   320, DIVCLR); // бордер знизу
}

// ─── Фонове зображення ───────────────────────────────────────────────────────

#define DRAW_BG_PART(bitmap, startRow) \
  { uint_farptr_t _a = pgm_get_far_address(bitmap); \
    for (uint8_t _r = 0; _r < BG_ROWS_PART; _r++) { \
      memcpy_PF(_bgRowBuf, _a, (size_t)BG_IMG_W * 2); \
      _a += (uint_farptr_t)BG_IMG_W * 2; \
      tft.startWrite(); \
      tft.setAddrWindow(0, (startRow) + _r, BG_IMG_W, 1); \
      for (uint16_t _i = 0; _i < BG_IMG_W; _i++) tft.writeColor(_bgRowBuf[_i], 1); \
      tft.endWrite(); \
    } }

static void drawBackground() {
  tft.fillScreen(ST77XX_BLACK);
  DRAW_BG_PART(backgroundBitmap0,   0)
  DRAW_BG_PART(backgroundBitmap1,  34)
  DRAW_BG_PART(backgroundBitmap2,  68)
  DRAW_BG_PART(backgroundBitmap3, 102)
}

// ─── Іконка свічки запалювання + струм під нею ───────────────────────────────
static void drawIgnitionIcon(bool visible) {
  const int16_t cx = 200, cy = 90;
  redrawBgRect(cx - 12, cy - 14, cx + 12, cy + 24); // +10px знизу для тексту
  if (!visible) return;
  uint16_t c = tft.color565(255, 50, 50);

  tft.fillRect(cx-7, cy-14, 15, 5, c);   // гайка
  tft.fillRect(cx-4, cy-9,   9, 2, c);   // перехід
  tft.fillRect(cx-3, cy-7,   7, 8, c);   // ізолятор
  tft.fillRect(cx-5, cy+1,  11, 4, c);   // корпус
  tft.fillRect(cx-1, cy+5,   3, 5, c);   // центральний електрод
  tft.fillRect(cx-5, cy+5,   2, 7, c);   // земляний електрод (вертик.)
  tft.fillRect(cx-5, cy+11,  4, 2, c);   // земляний електрод (горизонт.)
  tft.drawLine(cx-1, cy+13, cx+1, cy+11, c);  // іскра
  tft.drawLine(cx-1, cy+11, cx+1, cy+13, c);

  // Струм під іконкою
  if (!isnan(heater.ignitCurrent)) {
    tft.setFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(c);
    char buf[6];
    snprintf(buf, sizeof(buf), "%dA", (int16_t)(heater.ignitCurrent + 0.5f));
    int16_t tx, ty; uint16_t tw, th;
    tft.getTextBounds(buf, 0, 0, &tx, &ty, &tw, &th);
    tft.setCursor(cx - (int16_t)(tw / 2), cy + 17);
    tft.print(buf);
  }
}

// ─── sensorsInit ─────────────────────────────────────────────────────────────
void sensorsInit() {
  swPressMs     = 0;
  powerEditMode = false;
  drawBackground();
  drawVoltageOverlay(heater.voltage);
  drawTemperatureOverlay(heater.tempExhaust, 190, 50);
  drawTemperatureOverlay(heater.tempChamber, 120, 99);
  drawTemperatureOverlay(heater.tempAirOut,   2, 130);
  drawTemperatureOverlay(heater.tempAirIn, 260, 130);  // правий край x=280
  drawIgnitionIcon(digitalRead(IGNITION_PIN) == HIGH);
  drawStatusOverlay();
  drawStatePower(true);
  tft.fillRect(0, STAT_BAR_Y, 320, 3, STAT_BG);   // порожня прогрес-смужка
  drawBottom(true);

  lastEncoderPos = encoderGetPos();
  lastSW = digitalRead(ENCODER_SW);
}

// ─── sensorsUpdate ───────────────────────────────────────────────────────────
void sensorsUpdate() {
  static uint32_t lastMs = 0;

  bool     sw  = digitalRead(ENCODER_SW);
  uint32_t now = millis();

  // ── Енкодер → потужність (тільки в режимі редагування) ───────────────────
  int pos   = encoderGetPos();
  int delta = pos - lastEncoderPos;
  if (delta != 0) {
    lastEncoderPos = pos;
    if (powerEditMode) {
      int newPwr = (int)heater.power + (delta > 0 ? 1 : -1);
      if (newPwr < 1)  newPwr = 1;
      if (newPwr > 10) newPwr = 10;
      heaterSetPower((uint8_t)newPwr);
      powerEditMs = now;   // скидаємо таймер бездіяльності
      drawPowerBar();
      tft.fillRect(0, STAT_BAR_Y, 320, 3, STAT_BG);
    }
  }

  // ── Кнопка енкодера ───────────────────────────────────────────────────────
  if (sw == LOW && lastSW == HIGH) {
    // Натиснули
    swPressMs  = now;
    lastProgMs = now;
  } else if (sw == HIGH && lastSW == LOW) {
    if (swPressMs != 0 && (now - swPressMs) < 500) {
      // Короткий клік (<500мс): перемикаємо режим редагування
      powerEditMode = !powerEditMode;
      powerEditMs   = now;
      drawPowerBar();
    }
    tft.fillRect(0, STAT_BAR_Y, 320, 3, STAT_BG);  // завжди очищаємо прогрес при відпусканні
    swPressMs = 0;
  }
  lastSW = sw;

  // Утримання 2с → СТАРТ / СТОП
  if (sw == LOW && swPressMs != 0) {
    uint32_t held = now - swPressMs;
    if (held >= 2000) {
      bool wasOff = (heater.state == HEATER_OFF || heater.state == HEATER_FAULT);
      heaterToggle();
      if (wasOff) timerStart(); else timerStop();
      swPressMs     = 0;
      powerEditMode = false;
      drawStatePower();
      tft.fillRect(0, STAT_BAR_Y, 320, 3, STAT_BG);
    } else if (now - lastProgMs >= 50) {
      lastProgMs = now;
      drawToggleProgress(held);
    }
  }

  // ── Таймаут 3с → виходимо з режиму редагування ───────────────────────────
  if (powerEditMode && (now - powerEditMs >= 3000)) {
    powerEditMode = false;
    drawPowerBar();
  }

  // ── Іконка запалювання + струм (оновлюємо при зміні стану або значення) ────
  {
    static bool    prevIgn     = false;
    static int16_t prevIgnAmps = -1;
    bool    ign     = digitalRead(IGNITION_PIN) == HIGH;
    int16_t curAmps = ign && !isnan(heater.ignitCurrent)
                      ? (int16_t)(heater.ignitCurrent + 0.5f) : -1;
    if (ign != prevIgn || curAmps != prevIgnAmps) {
      prevIgn = ign; prevIgnAmps = curAmps;
      drawIgnitionIcon(ign);
    }
  }

  // ── Оновлення раз на 500 мс ───────────────────────────────────────────────
  if (now - lastMs >= 500) {
    lastMs = now;

    // Напруга (оновлюємо якщо змінилась на ≥0.1В)
    static float prevVolt = -99.0f;
    float v = heater.voltage;
    if (v - prevVolt > 0.05f || prevVolt - v > 0.05f) {
      prevVolt = v;
      drawVoltageOverlay(v);
    }

    // Статус (оновлюємо якщо змінився стан або остання помилка)
    static uint8_t     prevFaultCnt  = 0xFF;
    static FaultCode   prevLastCode  = FAULT_NONE;
    static HeaterState prevStatSt    = (HeaterState)0xFF;
    uint8_t    fc  = faultsCount();
    FaultCode  lc  = fc > 0 ? faultGet(fc - 1).code : FAULT_NONE;
    if (fc != prevFaultCnt || lc != prevLastCode || heater.state != prevStatSt) {
      prevFaultCnt = fc;  prevLastCode = lc;  prevStatSt = heater.state;
      drawStatusOverlay();
    }

    static float prevExhaust = 1e9f;
    static float prevChamber = 1e9f;
    static float prevAirOut  = 1e9f;
    static float prevAirIn   = 1e9f;
    if (tempDiff(heater.tempExhaust, prevExhaust)) {
      prevExhaust = heater.tempExhaust;
      drawTemperatureOverlay(heater.tempExhaust, 190, 50);
    }
    if (tempDiff(heater.tempChamber, prevChamber)) {
      prevChamber = heater.tempChamber;
      drawTemperatureOverlay(heater.tempChamber, 120, 99);
    }
    if (tempDiff(heater.tempAirOut, prevAirOut)) {
      prevAirOut = heater.tempAirOut;
      drawTemperatureOverlay(heater.tempAirOut, 2, 130);
    }
    if (tempDiff(heater.tempAirIn, prevAirIn)) {
      prevAirIn = heater.tempAirIn;
      drawTemperatureOverlay(heater.tempAirIn, 260, 130);
    }

    drawBottom();
    drawStatePower();
    if (sw == LOW && swPressMs != 0)
      drawToggleProgress(now - swPressMs);
  }
}
