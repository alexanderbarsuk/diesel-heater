#include "config.h"
#include "display.h"
#include "encoder.h"
#include "heater.h"
#include "screen_manager.h"

void setup() {
  Serial.begin(115200);
  displaySetup();
  encoderSetup();
  heaterSetup();
  pinMode(BUTTON, INPUT_PULLUP);
  screenManagerInit();
  Serial.println(F("MegaHeater2 started"));
}

void loop() {
  static bool lastBtn = HIGH;

  heaterUpdate();

  // ── Кнопка BUTTON — переключення екранів ──
  bool btn = digitalRead(BUTTON);
  if (btn != lastBtn) {
    lastBtn = btn;
    if (btn == LOW) screenManagerNext();
  }

  screenManagerUpdate();
}
