#include "config.h"
#include "display.h"
#include "encoder.h"
#include "screen_manager.h"

void setup() {
  Serial.begin(115200);
  displaySetup();
  encoderSetup();
  pinMode(BUTTON, INPUT_PULLUP);
  screenManagerInit();
  Serial.println(F("MegaHeater2 started"));
}

void loop() {
  static bool lastBtn = HIGH;

  // ── Кнопка BUTTON — переключення екранів ──
  bool btn = digitalRead(BUTTON);
  if (btn != lastBtn) {
    lastBtn = btn;
    if (btn == LOW) screenManagerNext();
  }

  screenManagerUpdate();
}
