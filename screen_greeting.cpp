#include "screen_greeting.h"
#include "screen_manager.h"
#include <Fonts/FreeSansBold24pt7b.h>

#define LETTER_DELAY_MS   600UL
#define LETTER_ANIM_MS    450UL
#define AUTO_SWITCH_MS    700UL   // пауза після останньої букви перед переходом

static uint32_t startTime;
static uint32_t finishTime;   // момент завершення анімації (0 = ще не завершена)
static int16_t  lx[3], ly[3];
static bool     letterDone[3];

static void calcPositions() {
  tft.setFont(&FreeSansBold24pt7b);
  tft.setTextSize(3);

  const char* str[3] = {"D", "K", "S"};
  int16_t  bx[3], by[3];
  uint16_t bw[3], bh[3];

  for (uint8_t i = 0; i < 3; i++)
    tft.getTextBounds(str[i], 0, 0, &bx[i], &by[i], &bw[i], &bh[i]);

  // Кожна буква центрується у своїй третині екрану: центри на 53, 160, 267 px
  for (uint8_t i = 0; i < 3; i++) {
    int cellCenterX = 320 * (2 * i + 1) / 6;
    lx[i] = cellCenterX - (int)bw[i] / 2 - bx[i];
    ly[i] = (240 - (int)bh[i]) / 2 - by[i];
  }

  tft.setFont(nullptr);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
}

static void drawLetter(uint8_t i, uint16_t color) {
  const char ch[3] = {'D', 'K', 'S'};
  tft.setFont(&FreeSansBold24pt7b);
  tft.setTextSize(3);
  tft.setTextColor(color, ST77XX_BLACK);
  tft.setCursor(lx[i], ly[i]);
  tft.print(ch[i]);
  tft.setFont(nullptr);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
}

void greetingInit() {
  tft.fillScreen(ST77XX_BLACK);
  calcPositions();
  startTime  = millis();
  finishTime = 0;
  letterDone[0] = letterDone[1] = letterDone[2] = false;
}

void greetingUpdate() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < 3; i++) {
    if (letterDone[i]) continue;

    int32_t t = (int32_t)(now - startTime) - (int32_t)(i * LETTER_DELAY_MS);
    if (t <= 0) continue;

    if (t >= (int32_t)LETTER_ANIM_MS) {
      drawLetter(i, tft.color565(0, 0, 255));
      letterDone[i] = true;
    } else {
      drawLetter(i, tft.color565(0, 0, (uint8_t)(t * 255L / LETTER_ANIM_MS)));
    }
  }

  // Автоперехід на екран сенсорів після завершення анімації
  if (letterDone[0] && letterDone[1] && letterDone[2]) {
    if (finishTime == 0) finishTime = now;
    if (now - finishTime >= AUTO_SWITCH_MS) screenManagerNext();
  }
}
