#include "screen_faults.h"
#include "faults.h"
#include "uaprint.h"
#include "encoder.h"
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
// Рядки:     y=47..214  (7 рядків × 24px)
// Кнопка:    y=215..236
// Смужка:    y=237..239
#define FAULT_HDR_H    47
#define FAULT_ROW_H    24
#define FAULT_LIST_Y   47
#define FAULT_BTN_Y   215
#define FAULT_BAR_Y   237

#define ITEMS_PER_PAGE   7

static bool     lastSW      = HIGH;
static uint32_t swPressMs   = 0;
static uint32_t lastBarMs   = 0;
static uint8_t  currentPage = 0;
static int      lastEncoderPos;

// ─── Індикатор сторінок ───────────────────────────────────────────────────────
static void drawPageIndicator(uint8_t cnt) {
  uint8_t totalPages = (cnt + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
  if (totalPages < 2) totalPages = 1;
  tft.fillRect(148, 0, 172, FAULT_HDR_H, CLR_HDR_RED);
  int cx = 314 - (totalPages - 1) * 16;
  for (uint8_t p = 0; p < totalPages; p++, cx += 16) {
    if (p == currentPage)
      tft.fillCircle(cx, 13, 4, ST77XX_WHITE);
    else
      tft.drawCircle(cx, 13, 4, tft.color565(120, 40, 40));
  }
}

// ─── Кнопка \"СТЕРТИ ВСІ\" ─────────────────────────────────────────────────────
static void drawButton(uint16_t bg) {
  tft.fillRect(0, FAULT_BTN_Y, 320, FAULT_BAR_Y - FAULT_BTN_Y, bg);
  tft.setCursor(4, 233);
  printUA("      [ СТЕРТИ ВСІ ]      ", ST77XX_WHITE, bg);   // 26cp
}

static void drawButtonProgress(uint32_t held) {
  uint8_t  prog = (uint8_t)(held * 255UL / 2000UL);
  uint8_t  r    = 65 + (uint8_t)((uint32_t)(200 - 65) * prog / 255);
  uint16_t bg   = tft.color565(r, 0, 0);
  drawButton(bg);
  uint16_t w = (uint16_t)(held * 320UL / 2000UL);
  if (w > 320) w = 320;
  tft.fillRect(0, FAULT_BAR_Y, w,       3, CLR_ORANGE);
  tft.fillRect(w, FAULT_BAR_Y, 320 - w, 3, bg);
}

// ─── Список помилок (7 рядків поточної сторінки, зворотній порядок) ───────────
static void drawFaultsList(uint8_t cnt) {
  for (uint8_t row = 0; row < ITEMS_PER_PAGE; row++) {
    uint16_t rowBg = (row & 1) ? CLR_ROW_ODD : CLR_ROW_EVEN;
    int16_t  ry    = FAULT_LIST_Y + row * FAULT_ROW_H;
    tft.fillRect(0, ry, 320, FAULT_ROW_H, rowBg);

    // зворотній порядок: найновіша помилка першою
    int16_t fi = (int16_t)cnt - 1 - (int16_t)(currentPage * ITEMS_PER_PAGE + row);
    if (fi >= 0) {
      FaultEntry e = faultGet((uint8_t)fi);
      tft.setCursor(4, ry + 20);
      char numBuf[4];
      snprintf(numBuf, 4, "%-3u", (uint8_t)fi + 1);
      printUA(numBuf, CLR_NUM, rowBg);                            // 3cp
      printUAn(faultName(e.code), 0, 19, ST77XX_WHITE, rowBg);   // 19cp
      char argBuf[5];
      if (e.arg > 0) snprintf(argBuf, 5, "%4u", e.arg);
      else           strcpy(argBuf, "    ");
      printUA(argBuf, ST77XX_YELLOW, rowBg);                      // 4cp
    }
  }
}

// ─── Повне перемалювання екрану ───────────────────────────────────────────────
static void drawFaultsScreen() {
  uint8_t cnt = faultsCount();

  if (cnt == 0) {
    tft.fillScreen(CLR_BODY_GREEN);
    uint16_t hdr = CLR_HDR_GREEN;
    tft.fillRect(0, 0, 320, FAULT_HDR_H, hdr);
    tft.setCursor(4, 36);
    printUA("     АВАРІЙ НЕМАЄ         ", ST77XX_WHITE, hdr);
    tft.setCursor(4, 120);
    printUA("      СИСТЕМА СПРАВНА     ", CLR_TEXT_GREEN, CLR_BODY_GREEN);
    tft.setCursor(4, 148);
    printUA("      ОПАЛЮВАЧ ГОТОВИЙ    ", tft.color565(0, 100, 0), CLR_BODY_GREEN);
  } else {
    tft.fillScreen(CLR_BODY_RED);
    uint16_t hdr = CLR_HDR_RED;
    tft.fillRect(0, 0, 320, FAULT_HDR_H, hdr);
    tft.setCursor(4, 36);
    char cntBuf[4];
    snprintf(cntBuf, 4, "%3u", cnt);
    printUA(" ! АВАРІЇ ! ", ST77XX_WHITE, hdr);    // 12cp
    printUA(cntBuf, ST77XX_YELLOW, hdr);            //  3cp
    printUA(" ПОМИЛОК   ", ST77XX_WHITE, hdr);      // 11cp
    drawPageIndicator(cnt);
    tft.drawFastHLine(0, FAULT_HDR_H, 320, CLR_ORANGE);
    drawFaultsList(cnt);
    tft.drawFastHLine(0, FAULT_BTN_Y - 1, 320, CLR_ORANGE);
    drawButton(CLR_BTN);
    tft.fillRect(0, FAULT_BAR_Y, 320, 3, CLR_BTN);
  }
}

// ─── faultsScreenInit ────────────────────────────────────────────────────────
void faultsScreenInit() {
  currentPage    = 0;
  swPressMs      = 0;
  lastBarMs      = 0;
  lastEncoderPos = encoderGetPos();
  drawFaultsScreen();
  lastSW = digitalRead(ENCODER_SW);
}

// ─── faultsScreenUpdate ──────────────────────────────────────────────────────
void faultsScreenUpdate() {
  bool     sw  = digitalRead(ENCODER_SW);
  uint32_t now = millis();

  // ── Перелистування сторінок енкодером ────────────────────────────────────
  int pos   = encoderGetPos();
  int delta = pos - lastEncoderPos;
  if (delta != 0) {
    lastEncoderPos = pos;
    uint8_t cnt = faultsCount();
    if (cnt > ITEMS_PER_PAGE) {
      uint8_t totalPages = (cnt + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
      int newPage = (int)currentPage + (delta > 0 ? 1 : -1);
      if (newPage < 0) newPage = 0;
      if (newPage >= totalPages) newPage = totalPages - 1;
      if ((uint8_t)newPage != currentPage) {
        currentPage = (uint8_t)newPage;
        drawPageIndicator(cnt);
        drawFaultsList(cnt);
      }
    }
  }

  // ── Кнопка: утримування 2с → стирання ────────────────────────────────────
  if (sw == LOW && lastSW == HIGH) {
    swPressMs = now;
    lastBarMs = now;
  } else if (sw == HIGH && lastSW == LOW) {
    swPressMs = 0;
    if (faultsCount() > 0) {
      drawButton(CLR_BTN);
      tft.fillRect(0, FAULT_BAR_Y, 320, 3, CLR_BTN);
    }
  }
  lastSW = sw;

  if (sw == LOW && swPressMs != 0 && faultsCount() > 0) {
    uint32_t held = now - swPressMs;
    if (held >= 2000) {
      faultsClear();
      swPressMs   = 0;
      currentPage = 0;
      drawFaultsScreen();
    } else if (now - lastBarMs >= 50) {
      lastBarMs = now;
      drawButtonProgress(held);
    }
  }
}
