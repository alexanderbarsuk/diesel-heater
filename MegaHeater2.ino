#include "config.h"
#include "display.h"
#include "encoder.h"
#include "heater.h"
#include "faults.h"
#include "timer.h"
#include "screen_manager.h"

void setup() {
  displaySetup();
  encoderSetup();
  heaterSetup();
  faultsInit();
  pinMode(BUTTON, INPUT_PULLUP);  // перемикання екранів
  screenManagerInit();
}

void loop() {
  static bool lastBtn = HIGH;

  heaterUpdate();
  timerUpdate();

  // ── Кнопка BUTTON — переключення екранів ──
  bool btn = digitalRead(BUTTON);
  if (btn != lastBtn) {
    lastBtn = btn;
    if (btn == LOW) screenManagerNext();
  }

  screenManagerUpdate();
}
