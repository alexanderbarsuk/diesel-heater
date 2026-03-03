#pragma once
#include <stdint.h>

// ─── Стани обігрівача ─────────────────────────────────────────────────────────
enum HeaterState : uint8_t {
  HEATER_OFF,       // вимкнено
  HEATER_STARTING,  // запускається
  HEATER_RUNNING,   // працює
  HEATER_STOPPING,  // охолодження/зупинка
  HEATER_FAULT,     // помилка
};

// ─── Поточні дані обігрівача ──────────────────────────────────────────────────
struct HeaterData {
  HeaterState state;
  uint8_t     power;        // 1..10
  float       tempChamber;  // °C (NAN = немає даних)
  float       tempExhaust;  // °C (NAN = немає даних)
  float       tempAirOut;   // °C (NAN = немає даних)
  float       tempAirIn;    // °C (NAN = немає даних)
  uint16_t    fanRPM;       // об/хв
  float       pumpHz;       // Гц
};

extern HeaterData heater;

void heaterSetup();
void heaterUpdate();              // викликати в кожній ітерації loop() (non-blocking)
void heaterToggle();              // СТАРТ якщо OFF/FAULT, СТОП якщо RUNNING/STARTING
void heaterSetPower(uint8_t pwr); // 1..10
