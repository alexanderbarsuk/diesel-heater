#include "config.h"
#include "display.h"
#include "encoder.h"
#include "heater.h"
#include "faults.h"
#include "timer.h"
#include "screen_manager.h"
#include "clk2hz.h"

void setup() {
  clk2hzSetup();   // 2 Гц на піні 46 + ехо на 34, 35 — одразу після старту
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
  clk2hzUpdate();

  // ── Кнопка BUTTON — переключення екранів ──
  bool btn = digitalRead(BUTTON);
  if (btn != lastBtn) {
    lastBtn = btn;
    if (btn == LOW) screenManagerNext();
  }

  screenManagerUpdate();
}
