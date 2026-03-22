#include "timer.h"
#include "heater.h"
#include <Arduino.h>

static bool     _enabled   = false;
static uint32_t _duration  = 0;
static uint32_t _remaining = 0;
static uint32_t _lastMs    = 0;
static bool     _running   = false;

void timerSetEnabled(bool en) {
  _enabled = en;
  if (!en) _running = false;
}

void timerSetDuration(uint32_t secs) { _duration = secs; }

void timerStart() {
  if (!_enabled || _duration == 0) return;
  _remaining = _duration;
  _running   = true;
  _lastMs    = millis();
}

void timerStop() { _running = false; }

void timerUpdate() {
  if (!_running || !_enabled) return;
  // Зупиняємо таймер якщо обігрівач вимкнено зовні (аварія, ручна зупинка)
  if (heater.state == HEATER_OFF  ||
      heater.state == HEATER_FAULT ||
      heater.state == HEATER_STOPPING) {
    _running = false;
    return;
  }
  uint32_t now     = millis();
  uint32_t elapsed = (now - _lastMs) / 1000UL;
  if (elapsed == 0) return;
  _lastMs += elapsed * 1000UL;
  if (elapsed >= _remaining) {
    _remaining = 0;
    _running   = false;
    heaterToggle();   // зупиняємо обігрівач
  } else {
    _remaining -= elapsed;
  }
}

bool     timerIsEnabled()  { return _enabled; }
bool     timerIsRunning()  { return _running && _enabled; }
uint32_t timerRemaining()  { return _running ? _remaining : 0; }
