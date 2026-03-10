#include "config.h"
#include "display.h"
#include "encoder.h"
#include "heater.h"
#include "faults.h"
#include "screen_manager.h"

void setup() {
  displaySetup();
  encoderSetup();
  heaterSetup();
  faultsInit();
  pinMode(BUTTON, INPUT_PULLUP);
  screenManagerInit();
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
