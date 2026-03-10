#include "screen_faults.h"
#include "faults.h"
#include "uaprint.h"
#include "config.h"

// ─── Кольори ─────────────────────────────────────────────────────────────────
#define CLR_HDR_RED    tft.color565(200,   0,   0)
#define CLR_BODY_RED   tft.color565( 18,   0,   0)
#define CLR_ROW_EVEN   tft.color565( 22,   2,   2)
#define CLR_ROW_ODD    tft.color565( 38,   5,   5)
#define CLR_ORANGE     tft.color565(255, 110,   0)
#define CLR_BTN        tft.color565( 65,   0,   0)
#define CLR_NUM        tft.color565(255, 140,   0)
#define CLR_HDR_GREEN  tft.color565(  0, 155,   0)
#define CLR_BODY_GREEN tft.color565(  0,  18,   0)
#define CLR_TEXT_GREEN tft.color565(  0, 230,   0)

// ─── Розмітка ─────────────────────────────────────────────────────────────────
// Заголовок: y=0..46, baseline=36
// Рядки:     y=47+i×24, baseline=+20  (7 рядків → y=47..214)
// Сепаратор: y=214
// Кнопка:    y=215..236 (текст), y=237..239 (прогрес-смужка)
#define FAULT_HDR_H   47
#define FAULT_ROW_H   24
#define FAULT_LIST_Y  47
#define FAULT_BTN_Y  215    // початок кнопки (текст)
#define FAULT_BAR_Y  237    // прогрес-смужка (3px до низу екрану)
// 26 CP/рядок: x=4..316 при UA_ADVANCE=12

static bool     lastSW    = HIGH;
static uint32_t swPressMs = 0;   // millis() моменту натискання (0 = не натиснуто)
static uint32_t lastBarMs = 0;   // час останнього оновлення кнопки

// ─── Перемалювання кнопки ────────────────────────────────────────────────────
static void drawButton(uint16_t bg) {
  tft.fillRect(0, FAULT_BTN_Y, 320, FAULT_BAR_Y - FAULT_BTN_Y, bg);
  tft.setCursor(4, 233);
  printUA("      [ СТЕРТИ ВСІ ]      ", ST77XX_WHITE, bg);   // 26cp
}

static void drawButtonProgress(uint32_t held) {
  // Фон кнопки: від CLR_BTN (r=65) до CLR_HDR_RED (r=200)
  uint8_t prog = (uint8_t)(held * 255UL / 2000UL);
  uint8_t r    = 65 + (uint8_t)((uint32_t)(200 - 65) * prog / 255);
  uint16_t bg  = tft.color565(r, 0, 0);
  drawButton(bg);

  // Прогрес-смужка внизу (3px)
  uint16_t w = (uint16_t)(held * 320UL / 2000UL);
  if (w > 320) w = 320;
  tft.fillRect(0,   FAULT_BAR_Y, w,       3, CLR_ORANGE);
  tft.fillRect(w,   FAULT_BAR_Y, 320 - w, 3, bg);
}

// ─── Перемалювати весь екран ──────────────────────────────────────────────────
static void drawFaultsScreen() {
  uint8_t cnt = faultsCount();

  if (cnt == 0) {
    // ── Немає помилок ─────────────────────────────────────────────────────────
    tft.fillScreen(CLR_BODY_GREEN);
    uint16_t hdr = CLR_HDR_GREEN;
    tft.fillRect(0, 0, 320, FAULT_HDR_H, hdr);
    tft.setCursor(4, 36);
    printUA("     АВАРІЙ НЕМАЄ         ", ST77XX_WHITE, hdr);        // 26cp
    tft.setCursor(4, 120);
    printUA("      СИСТЕМА СПРАВНА     ", CLR_TEXT_GREEN, CLR_BODY_GREEN);
    tft.setCursor(4, 148);
    printUA("      ОПАЛЮВАЧ ГОТОВИЙ    ", tft.color565(0, 100, 0), CLR_BODY_GREEN);

  } else {
    // ── Є помилки ─────────────────────────────────────────────────────────────
    tft.fillScreen(CLR_BODY_RED);

    // Заголовок
    uint16_t hdr = CLR_HDR_RED;
    tft.fillRect(0, 0, 320, FAULT_HDR_H, hdr);
    tft.setCursor(4, 36);
    // 26cp: " ! АВАРІЇ !  "(13) + cnt(2) + " ПОМИЛОК   "(11)
    printUA(" ! АВАРІЇ !  ", ST77XX_WHITE, hdr);
    char cntBuf[3];
    snprintf(cntBuf, 3, "%2u", cnt);
    printUA(cntBuf, ST77XX_YELLOW, hdr);
    printUA(" ПОМИЛОК   ", ST77XX_WHITE, hdr);

    tft.drawFastHLine(0, FAULT_HDR_H, 320, CLR_ORANGE);

    // Список
    for (uint8_t i = 0; i < FAULT_LOG_MAX; i++) {
      uint16_t rowBg = (i & 1) ? CLR_ROW_ODD : CLR_ROW_EVEN;
      int16_t  ry    = FAULT_LIST_Y + i * FAULT_ROW_H;
      tft.fillRect(0, ry, 320, FAULT_ROW_H, rowBg);

      if (i < cnt) {
        FaultEntry e = faultGet(i);
        tft.setCursor(4, ry + 20);
        char numBuf[3];
        snprintf(numBuf, 3, "%u ", i + 1);
        printUA(numBuf, CLR_NUM, rowBg);                          // 2cp
        printUAn(faultName(e.code), 0, 20, ST77XX_WHITE, rowBg); // 20cp
        char argBuf[5];
        if (e.arg > 0) snprintf(argBuf, 5, "%4u", e.arg);
        else           strcpy(argBuf, "    ");
        printUA(argBuf, ST77XX_YELLOW, rowBg);                    // 4cp
      }
    }

    // Кнопка
    tft.drawFastHLine(0, FAULT_BTN_Y - 1, 320, CLR_ORANGE);
    drawButton(CLR_BTN);
    tft.fillRect(0, FAULT_BAR_Y, 320, 3, CLR_BTN);  // порожня смужка
  }
}

// ─── faultsScreenInit ────────────────────────────────────────────────────────
void faultsScreenInit() {
  swPressMs = 0;
  lastBarMs = 0;
  drawFaultsScreen();
  lastSW = digitalRead(ENCODER_SW);
}

// ─── faultsScreenUpdate ──────────────────────────────────────────────────────
void faultsScreenUpdate() {
  bool     sw  = digitalRead(ENCODER_SW);
  uint32_t now = millis();

  if (sw == LOW && lastSW == HIGH) {
    // Щойно натиснули
    swPressMs = now;
    lastBarMs = now;
  } else if (sw == HIGH && lastSW == LOW) {
    // Відпустили раніше часу — скидаємо кнопку в початковий стан
    swPressMs = 0;
    if (faultsCount() > 0) {
      drawButton(CLR_BTN);
      tft.fillRect(0, FAULT_BAR_Y, 320, 3, CLR_BTN);
    }
  }
  lastSW = sw;

  // Тримається і є що стирати
  if (sw == LOW && swPressMs != 0 && faultsCount() > 0) {
    uint32_t held = now - swPressMs;
    if (held >= 2000) {
      faultsClear();
      swPressMs = 0;
      drawFaultsScreen();
    } else if (now - lastBarMs >= 50) {
      // Оновлюємо прогрес ≈20 разів/с
      lastBarMs = now;
      drawButtonProgress(held);
    }
  }
}
