#include "screen_sensors.h"
#include "heater.h"
#include "faults.h"
#include "uaprint.h"
#include "encoder.h"
#include "config.h"
#include "background.h"
#include <math.h>
#include <avr/pgmspace.h>

// ─── Розмітка (320×240) ──────────────────────────────────────────────────────
// y=0..135   — фонове зображення (background.h, 136px)
// y=136..163 — рядок стану + шкала потужності (28px)
// y=164..189 — темп.блоки ряд 1: камера | вихлоп (26px)
// y=190..215 — темп.блоки ряд 2: вихід  | вхід   (26px)
// y=216..239 — вентилятор + насос (24px)

#define STAT_Y     136    // статус + шкала
#define STAT_H      28
#define TEMP1_Y    164    // температурний ряд 1
#define TEMP2_Y    190    // температурний ряд 2
#define TEMP_H      26    // висота температурного ряду
#define BOT_Y      216    // рядок вент+насос
#define BOT_H       24
#define BLK_W      160    // ширина одного блоку (половина екрану)

#define STAT_BG    ST77XX_BLACK
#define TEMP_BG    tft.color565(12, 12, 28)
#define BOT_BG     tft.color565(10, 10, 10)
#define DIVCLR     tft.color565(45, 45, 70)
#define LABEL_CLR  tft.color565(120, 120, 120)

// Шкала потужності: 10 прямокутників після тексту стану
#define BAR_X      120    // x першого сегменту
#define BAR_SEG_W   18    // ширина сегменту
#define BAR_SEG_G    2    // проміжок між сегментами
#define BAR_SEG_H   16    // висота сегменту
#define BAR_Y     (STAT_Y + (STAT_H - BAR_SEG_H) / 2)   // = 142
// Загальна ширина шкали: 10*18 + 9*2 = 198px
#define BAR_W     (10 * BAR_SEG_W + 9 * BAR_SEG_G)

// Блок: 6cp мітка + 6cp значення
// label: x = bx+4..bx+76 (6×12)
// value: x = bx+84..bx+156 (6×12), right-aligned
#define LABEL_CP    6
#define VAL_CP      6
#define VAL_X_OFF  (4 + LABEL_CP * UA_ADVANCE)   // = 76 → value starts at bx+76... wait
// Actually right-aligned: value starts at bx + BLK_W - 4 - VAL_CP * UA_ADVANCE
//   = 160 - 4 - 72 = 84. Gap between label(ends 76) and value(starts 84) = 8px.

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

// ─── Статус + шкала потужності ───────────────────────────────────────────────
static void drawStatePower() {
  // Фон рядка (залишаємо 3px внизу для смужки прогресу)
  tft.fillRect(0, STAT_Y, 320, STAT_H - 3, STAT_BG);

  // Текст стану: 8cp, baseline = STAT_Y+20
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
  tft.setCursor(4, STAT_Y + 20);
  printUAn(stateStr[si], 0, 8, stateColor(heater.state), STAT_BG);

  // Індикатор редагування потужності (жовта лінія над шкалою)
  uint16_t indClr = powerEditMode ? ST77XX_YELLOW : STAT_BG;
  tft.drawFastHLine(BAR_X, BAR_Y - 3, BAR_W, indClr);

  // Шкала: 10 прямокутників
  bool    active = (heater.state == HEATER_RUNNING  ||
                    heater.state == HEATER_STARTING ||
                    heater.state == HEATER_RESTARTING);
  uint16_t onClr  = active ? tft.color565(0, 190, 0) : tft.color565(80, 80, 0);
  uint16_t offClr = tft.color565(30, 30, 30);
  for (uint8_t i = 0; i < 10; i++) {
    uint16_t clr = (i < heater.power) ? onClr : offClr;
    tft.fillRect(BAR_X + i * (BAR_SEG_W + BAR_SEG_G), BAR_Y, BAR_SEG_W, BAR_SEG_H, clr);
  }
}

// ─── Прогрес-смужка утримання (3px, y=161..163) ──────────────────────────────
static void drawToggleProgress(uint32_t held) {
  uint16_t w = (uint16_t)(held * 320UL / 2000UL);
  if (w > 320) w = 320;
  tft.fillRect(0, 161, w,       3, tft.color565(255, 110, 0));
  tft.fillRect(w, 161, 320 - w, 3, STAT_BG);
}

// ─── Температурний блок (160×26px) ───────────────────────────────────────────
// label: 6cp нормалізовано; value: 6cp right-aligned
// label ends at bx+76, value starts at bx+84 (8px gap)
static void drawTempBlock(int16_t bx, int16_t by, float t, const char* label) {
  tft.fillRect(bx, by, BLK_W, TEMP_H, TEMP_BG);
  int16_t bl = by + 20;

  // Мітка (6cp, сірий)
  tft.setCursor(bx + 4, bl);
  printUAn(label, 0, LABEL_CP, LABEL_CLR, TEMP_BG);

  // Значення (6cp, білий) — right-aligned: x = bx+160-4-6*12 = bx+84
  char val[10];
  if (isnan(t) || t < -100.0f || t > 9999.0f) {
    strcpy(val, "   ?\xC2\xB0""C");   // 6cp: 3 spaces + ? + °C
  } else {
    snprintf(val, 10, "%4.0f\xC2\xB0""C", t);   // e.g. " 320°C", 6cp
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

static void drawAllTemps(bool force = false) {
  static float pChamber = 1e9f, pExhaust = 1e9f, pAirOut = 1e9f, pAirIn = 1e9f;
  bool redrawDiv = false;
  if (force || tempDiff(heater.tempChamber, pChamber)) {
    pChamber = heater.tempChamber;
    drawTempBlock(0, TEMP1_Y, heater.tempChamber, "КАМЕРА");
    redrawDiv = true;
  }
  if (force || tempDiff(heater.tempExhaust, pExhaust)) {
    pExhaust = heater.tempExhaust;
    drawTempBlock(BLK_W, TEMP1_Y, heater.tempExhaust, "ВИХЛОП");
    redrawDiv = true;
  }
  if (force || tempDiff(heater.tempAirOut, pAirOut)) {
    pAirOut = heater.tempAirOut;
    drawTempBlock(0, TEMP2_Y, heater.tempAirOut, "ВИХІД ");
    redrawDiv = true;
  }
  if (force || tempDiff(heater.tempAirIn, pAirIn)) {
    pAirIn = heater.tempAirIn;
    drawTempBlock(BLK_W, TEMP2_Y, heater.tempAirIn, "ВХІД  ");
    redrawDiv = true;
  }
  if (redrawDiv) {
    tft.drawFastVLine(BLK_W, TEMP1_Y, TEMP_H * 2, DIVCLR);
    tft.drawFastHLine(0, TEMP2_Y, 320, DIVCLR);
  }
}

// ─── Нижній рядок: вентилятор + насос ────────────────────────────────────────
static void drawBottom(bool force = false) {
  static uint8_t pFanPwm  = 0xFF;
  static uint8_t pPumpPwm = 0xFF;
  bool changedFan  = force || (heater.fanPwm  != pFanPwm);
  bool changedPump = force || (heater.pumpPwm != pPumpPwm);
  if (!changedFan && !changedPump) return;
  pFanPwm  = heater.fanPwm;
  pPumpPwm = heater.pumpPwm;
  tft.fillRect(0, BOT_Y, 320, BOT_H, BOT_BG);
  tft.drawFastVLine(BLK_W, BOT_Y, BOT_H, DIVCLR);
  int16_t bl = BOT_Y + 20;

  // Вентилятор (лівий блок): 6cp мітка + 6cp значення (ШІМ 0..100%)
  tft.setCursor(4, bl);
  printUAn("ВЕНТЛ", 0, LABEL_CP, LABEL_CLR, BOT_BG);
  char buf[10];
  snprintf(buf, 10, "%5u%%", (uint16_t)(heater.fanPwm * 100u / 255u));
  tft.setCursor(BLK_W - 4 - VAL_CP * UA_ADVANCE, bl);
  printUA(buf, ST77XX_WHITE, BOT_BG);

  // Насос (правий блок): 6cp мітка + 6cp значення (ШІМ 0..100%)
  tft.setCursor(BLK_W + 4, bl);
  printUAn("НАСОС", 0, LABEL_CP, LABEL_CLR, BOT_BG);
  snprintf(buf, 10, "%5u%%", (uint16_t)(heater.pumpPwm * 100u / 255u));
  tft.setCursor(BLK_W + BLK_W - 4 - VAL_CP * UA_ADVANCE, bl);
  printUA(buf, ST77XX_WHITE, BOT_BG);
}

// ─── Фонове зображення ───────────────────────────────────────────────────────
static uint16_t _bgRowBuf[BG_IMG_W];

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

// ─── sensorsInit ─────────────────────────────────────────────────────────────
void sensorsInit() {
  swPressMs     = 0;
  powerEditMode = false;
  drawBackground();
  drawVoltageOverlay(heater.voltage);
  drawStatusOverlay();
  drawStatePower();
  tft.fillRect(0, 161, 320, 3, STAT_BG);   // порожня прогрес-смужка
  drawAllTemps(true);
  drawBottom(true);

  lastEncoderPos = encoderGetPos();
  lastSW = digitalRead(ENCODER_SW);
}

// ─── sensorsUpdate ───────────────────────────────────────────────────────────
void sensorsUpdate() {
  static HeaterState prevState = (HeaterState)0xFF;
  static uint32_t    lastMs    = 0;

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
      drawStatePower();
      tft.fillRect(0, 161, 320, 3, STAT_BG);
    }
  }

  // ── Кнопка енкодера ───────────────────────────────────────────────────────
  if (sw == LOW && lastSW == HIGH) {
    // Натиснули
    swPressMs  = now;
    lastProgMs = now;
  } else if (sw == HIGH && lastSW == LOW) {
    if (swPressMs != 0) {
      // Відпустили до 2с — короткий клік: перемикаємо режим редагування
      powerEditMode = !powerEditMode;
      powerEditMs   = now;
      tft.fillRect(0, 161, 320, 3, STAT_BG);
      drawStatePower();
    }
    swPressMs = 0;
  }
  lastSW = sw;

  // Утримання 2с → СТАРТ / СТОП
  if (sw == LOW && swPressMs != 0) {
    uint32_t held = now - swPressMs;
    if (held >= 2000) {
      heaterToggle();
      swPressMs     = 0;
      powerEditMode = false;
      drawStatePower();
      tft.fillRect(0, 161, 320, 3, STAT_BG);
      prevState = heater.state;
    } else if (now - lastProgMs >= 50) {
      lastProgMs = now;
      drawToggleProgress(held);
    }
  }

  // ── Таймаут 3с → виходимо з режиму редагування ───────────────────────────
  if (powerEditMode && (now - powerEditMs >= 3000)) {
    powerEditMode = false;
    drawStatePower();
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

    drawAllTemps();
    drawBottom();
    if (heater.state != prevState) {
      drawStatePower();
      // Відновити смужку якщо тримаємо
      if (sw == LOW && swPressMs != 0)
        drawToggleProgress(now - swPressMs);
      prevState = heater.state;
    }
  }
}
