#pragma once
#include <stdint.h>

// ─── Режим роботи ────────────────────────────────────────────────────────────
enum HeaterMode : uint8_t {
  MODE_HEAT = 0,  // нагрів (за замовчуванням)
  MODE_VENT = 1,  // вентиляція (насос вимкнено)
};

// ─── Стани обігрівача ─────────────────────────────────────────────────────────
enum HeaterState : uint8_t {
  HEATER_OFF,          // вимкнено
  HEATER_STARTING,     // перша спроба запуску
  HEATER_RUNNING,      // нормальна робота
  HEATER_STOPPING,     // охолодження вентилятором після зупинки
  HEATER_FAULT,        // помилка
  HEATER_PRIMING,      // прокачка пального
  HEATER_RESTARTING,   // друга спроба запуску (після першої невдалої)
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
void heaterToggle();              // СТАРТ якщо OFF/FAULT, СТОП якщо RUNNING/STARTING/RESTARTING
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
// Параметри послідовності запуску/зупинки
void heaterSetIgnitionTime(uint32_t seconds);   // тривалість роботи свічки
void heaterSetStartFanRpm(uint16_t rpm);         // оберти вент. під час запуску
void heaterSetStartPumpRpm(uint16_t rpm);        // оберти насоса під час запуску
void heaterSetStartFireTemp(uint16_t temp);      // температура займання (°C)
void heaterSetCoolFanRpm(uint16_t rpm);          // оберти вент. під час охолодження
void heaterSetCoolStopTemp(uint16_t temp);       // температура зупинки вент. (°C)
