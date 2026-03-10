#pragma once
#include <stdint.h>

// ─── Режим роботи ────────────────────────────────────────────────────────────
enum HeaterMode : uint8_t {
  MODE_HEAT = 0,  // нагрів (за замовчуванням)
  MODE_VENT = 1,  // вентиляція (насос вимкнено)
};

// ─── Стани обігрівача ─────────────────────────────────────────────────────────
enum HeaterState : uint8_t {
  HEATER_OFF,       // вимкнено
  HEATER_STARTING,  // запускається
  HEATER_RUNNING,   // працює
  HEATER_STOPPING,  // охолодження/зупинка
  HEATER_FAULT,     // помилка
  HEATER_PRIMING,   // прокачка пального (кнопка BUTTON утримується)
};

// ─── Поточні дані обігрівача ──────────────────────────────────────────────────
struct HeaterData {
  HeaterState state;
  uint8_t     power;        // 1..10
  float       tempChamber;  // °C (NAN = немає даних)
  float       tempExhaust;  // °C (NAN = немає даних)
  float       tempAirOut;   // °C (NAN = немає даних)
  float       tempAirIn;    // °C (NAN = немає даних)
  uint8_t     fanPwm;       // ШІМ вентилятора 0..255 (задане)
  uint8_t     pumpPwm;      // ШІМ насоса     0..255 (задане)
  uint16_t    fanRpm;       // об/хв (виміряне)
  uint16_t    pumpRpm;      // об/хв (виміряне)
  float       voltage;      // В (напруга живлення)
  HeaterMode  mode;         // режим: нагрів / вентиляція
};

extern HeaterData heater;

void heaterSetup();
void heaterUpdate();              // викликати в кожній ітерації loop() (non-blocking)
void heaterToggle();              // СТАРТ якщо OFF/FAULT, СТОП якщо RUNNING/STARTING
void heaterSetPower(uint8_t pwr); // 1..10
// Оновити один ступінь таблиці потужності (з меню/EEPROM)
void heaterSetPowerStep(uint8_t step, uint16_t fanRpm, uint16_t pumpRpm);
// Оновити коефіцієнти ПІД-регуляторів (з меню/EEPROM; значення у [ШІМ/RPM])
void heaterSetPid(float fKp, float fKi, float pKp, float pKi);
// Встановити поріг напруги для ввімкнення зарядки (В)
void heaterSetChargerThreshold(float threshV);
// Режим прокачки: 0=утримання кнопки, 1=таймер
void heaterSetPrimingMode(uint8_t mode);
// Тривалість таймера прокачки (секунди)
void heaterSetPrimingDuration(uint32_t seconds);
