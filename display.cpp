#include "display.h"

#define TFT_LINE_CHARS 26   // 320px / (12px × textSize2) ≈ 26 символів

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void displaySetup() {
  tft.init(240, 320);
  tft.setRotation(3);
  tft.invertDisplay(false);
  tft.fillScreen(ST77XX_BLACK);

}
