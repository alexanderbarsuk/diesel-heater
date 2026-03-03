#include "screen_sensors.h"
#include "uaprint.h"

// ─── TODO: підключити реальні сенсори ────────────────────────────────────────

void sensorsInit() {
  tft.fillScreen(ST77XX_BLACK);

  // Заголовок "ДАТЧИКИ" (SF Mono 22pt 2-bit AA, baseline=30)
  tft.setCursor(4, 30);
  printUA("ДАТЧИКИ", ST77XX_CYAN, ST77XX_BLACK);
  tft.drawFastHLine(0, 40, 320, ST77XX_CYAN);

  // Рядки даних (UA_ASCENT=21, spacing=32px між baseline'ами)
  tft.setCursor(4, 63);
  printUA("Темп:     --.- C     ", ST77XX_WHITE, ST77XX_BLACK);

  tft.setCursor(4, 95);
  printUA("Вологість: --.- %    ", ST77XX_WHITE, ST77XX_BLACK);

  tft.setCursor(4, 127);
  printUA("Тиск:  ---- гПа      ", ST77XX_WHITE, ST77XX_BLACK);

  tft.drawFastHLine(0, 138, 320, ST77XX_CYAN);

  tft.setCursor(4, 161);
  printUA("Стан:     --         ", ST77XX_WHITE, ST77XX_BLACK);

  tft.setCursor(4, 193);
  printUA("Час:      --         ", ST77XX_WHITE, ST77XX_BLACK);
}

void sensorsUpdate() {
  // TODO: читати сенсори і оновлювати значення
}
