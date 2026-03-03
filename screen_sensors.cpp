#include "screen_sensors.h"
#include "heater.h"
#include "uaprint.h"
#include "encoder.h"
#include "config.h"
#include "background.h"
#include <math.h>

// ─── Розмітка екрану (320×240, UA_ADVANCE=12, 26 CP/рядок) ──────────────────
// Всі рядки починаються з x=4, ширина 26 CP = 312 px
#define HDR_BL    22   // baseline стану
#define PWR_BL    54   // baseline потужності
#define TEMP_BL0  86   // baseline Т.камера
#define TEMP_BL1 118   // baseline Т.вихлоп
#define TEMP_BL2 150   // baseline Т.вихід
#define TEMP_BL3 182   // baseline Т.вхід
#define BOT_BL   214   // baseline (вент + насос)

#define ROW_X     4
#define ROW_W    (26 * UA_ADVANCE)   // 312 px
#define BG        ST77XX_BLACK

static int  lastEncoderPos;
static bool lastSW;

// ─── Колір стану ─────────────────────────────────────────────────────────────
static uint16_t stateColor(HeaterState s) {
  switch (s) {
    case HEATER_OFF:      return tft.color565(100, 100, 100);
    case HEATER_STARTING: return ST77XX_YELLOW;
    case HEATER_RUNNING:  return ST77XX_GREEN;
    case HEATER_STOPPING: return tft.color565(255, 128, 0);
    case HEATER_FAULT:    return ST77XX_RED;
  }
  return ST77XX_WHITE;
}

static const char* stateText(HeaterState s) {
  switch (s) {
    case HEATER_OFF:      return "         ЗУПИНЕНО         ";  // 26 CP
    case HEATER_STARTING: return "         ЗАПУСКАЄ         ";
    case HEATER_RUNNING:  return "          РОБОТА          ";
    case HEATER_STOPPING: return "         ЗУПИНКА          ";
    case HEATER_FAULT:    return "         ПОМИЛКА          ";
  }
  return "                          ";
}

// ─── Малювання елементів ─────────────────────────────────────────────────────
static void drawState() {
  tft.setCursor(ROW_X, HDR_BL);
  printUA(stateText(heater.state), stateColor(heater.state), BG);
}

static void drawPower() {
  tft.setCursor(ROW_X, PWR_BL);

  // "N " (2 CP)
  char prefix[3];
  prefix[0] = (heater.power < 10) ? ('0' + heater.power) : '1';
  prefix[1] = (heater.power < 10) ? ' ' : '0';
  prefix[2] = '\0';
  printUA(prefix, ST77XX_WHITE, BG);

  // Шкала (10 CP заповнено + 10 порожньо → разом 20 CP), але ми маємо ще 4 вільних
  // 2 + 10 + 14 = 26 CP ✓
  uint16_t fillClr = (heater.state == HEATER_RUNNING || heater.state == HEATER_STARTING)
                       ? ST77XX_GREEN : ST77XX_YELLOW;
  char bars[15];
  memset(bars, '#', heater.power);
  bars[heater.power] = '\0';
  if (heater.power > 0) printUA(bars, fillClr, BG);

  uint8_t ep = 10 - heater.power;
  memset(bars, '-', ep);
  bars[ep] = '\0';
  if (ep > 0) printUA(bars, tft.color565(50, 50, 50), BG);

  // Залишок до 26 CP: 2+10+14=26
  printUA("              ", tft.color565(20, 20, 20), BG);
}

// ─── drawTemperature ─────────────────────────────────────────────────────────
// Малює значення температури (9 CP фікс.) з верхнього лівого кута (x, y_top).
// Формат: right-align число до 6 CP + " °C"
static void drawTemperature(int16_t x, int16_t yTop, float t) {
  tft.setCursor(x, yTop + UA_ASCENT);
  if (isnan(t) || t < -100.0f || t > 1400.0f) {
    printUA("  --.- \xC2\xB0""C", ST77XX_WHITE, BG);  // 9 CP
  } else {
    char num[8];
    snprintf(num, sizeof(num), "%.1f", t);
    uint8_t numCP = (uint8_t)strlen(num);
    uint8_t pad   = (numCP < 6) ? 6 - numCP : 0;
    char sp[7];
    memset(sp, ' ', pad);
    sp[pad] = '\0';
    printUA(sp,  ST77XX_WHITE, BG);
    printUA(num, ST77XX_WHITE, BG);
    printUA(" \xC2\xB0""C", ST77XX_WHITE, BG);
  }
}

static void drawBottom() {
  // 26 CP: "  RRRR            HH.H  "
  // = 2 + 4 + 12 + 4 + 4 = 26 ✓
  char buf[8];
  tft.setCursor(ROW_X, BOT_BL);
  printUA("  ", ST77XX_WHITE, BG);
  snprintf(buf, sizeof(buf), "%4u", heater.fanRPM);
  printUA(buf, ST77XX_WHITE, BG);
  printUA("            ", tft.color565(40, 40, 40), BG);
  snprintf(buf, sizeof(buf), "%4.1f", heater.pumpHz);
  printUA(buf, ST77XX_WHITE, BG);
  printUA("    ", tft.color565(20, 20, 20), BG);
}

// ─── Фонове зображення ───────────────────────────────────────────────────────
static void drawBackground() {
  const int16_t xOff = (320 - BG_IMG_W) / 2;
  const int16_t yOff = (240 - BG_IMG_H) / 2;

  tft.fillScreen(BG);

  uint32_t addr = pgm_get_far_address(backgroundBitmap0);
  for (uint8_t row = 0; row < BG_ROWS_P; row++) {
    tft.startWrite();
    tft.setAddrWindow(xOff, yOff + row, BG_IMG_W, 1);
    for (uint16_t col = 0; col < BG_IMG_W; col++, addr += 2)
      tft.writeColor(pgm_read_word_far(addr), 1);
    tft.endWrite();
  }

  addr = pgm_get_far_address(backgroundBitmap1);
  for (uint8_t row = BG_ROWS_P; row < BG_IMG_H; row++) {
    tft.startWrite();
    tft.setAddrWindow(xOff, yOff + row, BG_IMG_W, 1);
    for (uint16_t col = 0; col < BG_IMG_W; col++, addr += 2)
      tft.writeColor(pgm_read_word_far(addr), 1);
    tft.endWrite();
  }
}

// ─── sensorsInit ─────────────────────────────────────────────────────────────
void sensorsInit() {
  drawBackground();
  drawState();
  drawPower();
  drawTemperature(  4,  68, heater.tempChamber);   // Т.камера
  drawTemperature(160,  40, heater.tempExhaust);   // Т.вихлоп
  drawTemperature(  4, 132, heater.tempAirOut);    // Т.вихід
  drawTemperature(  4, 164, heater.tempAirIn);     // Т.вхід
  drawBottom();

  lastEncoderPos = encoderGetPos();
  lastSW = digitalRead(ENCODER_SW);
}

// ─── sensorsUpdate ───────────────────────────────────────────────────────────
void sensorsUpdate() {
  static HeaterState prevState = (HeaterState)0xFF;
  static uint32_t    lastMs    = 0;

  // ── Енкодер → потужність ──────────────────────────────────────────────────
  int pos   = encoderGetPos();
  int delta = pos - lastEncoderPos;
  if (delta != 0) {
    lastEncoderPos = pos;
    int newPwr = (int)heater.power + (delta > 0 ? 1 : -1);
    if (newPwr < 1)  newPwr = 1;
    if (newPwr > 10) newPwr = 10;
    heaterSetPower((uint8_t)newPwr);
    drawPower();
  }

  // ── Кнопка → СТАРТ / СТОП ────────────────────────────────────────────────
  bool sw = digitalRead(ENCODER_SW);
  if (sw != lastSW) {
    lastSW = sw;
    if (sw == LOW) {
      heaterToggle();
      drawState();
      drawPower();
      prevState = heater.state;
    }
  }

  // ── Оновлення раз на 500 мс ───────────────────────────────────────────────
  uint32_t now = millis();
  if (now - lastMs >= 500) {
    lastMs = now;
    drawTemperature(  4,  68, heater.tempChamber);
    drawTemperature(160,  40, heater.tempExhaust);
    drawTemperature(  4, 132, heater.tempAirOut);
    drawTemperature(  4, 164, heater.tempAirIn);
    drawBottom();
    if (heater.state != prevState) {
      drawState();
      drawPower();
      prevState = heater.state;
    }
  }
}
