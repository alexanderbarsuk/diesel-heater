#pragma once
#include <stdint.h>

// ─── Таблиця потужності (10 ступенів) ────────────────────────────────────────
struct PowerStep {
  uint16_t fanRpm;    // цільові оберти вентилятора
  uint16_t pumpRpm;   // цільові оберти насоса
};

extern PowerStep POWER_TABLE[10];  // визначення у heater.cpp

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
  uint8_t     fanPwm;       // ШІМ вентилятора 0..255
  uint8_t     pumpPwm;      // ШІМ насоса     0..255
  uint16_t    fanRpm;       // об/хв (виміряне)
  uint16_t    pumpRpm;      // об/хв (виміряне)
  float       voltage;      // В
  HeaterMode  mode;         // режим: нагрів / вентиляція
  float       ambientTemp;   // °C від AHT20
  float       humidity;      // % від AHT20
  float       pressure;      // гПа від BMP280
  float       ignitCurrent;  // А від ACS712 (NAN = свічка неактивна)
};

extern HeaterData heater;

void heaterSetup();
void heaterUpdate();
void heaterToggle();
void heaterSetPower(uint8_t pwr);
void heaterSetPowerStep(uint8_t step, uint16_t fanRpm, uint16_t pumpRpm);
void heaterSetPid(float fKp, float fKi, float pKp, float pKi);
void heaterSetChargerThreshold(float threshV);
void heaterSetPrimingMode(uint8_t mode);
void heaterSetPrimingDuration(uint32_t seconds);
void heaterSetIgnitionTime(uint32_t seconds);
void heaterSetStartFanRpm(uint16_t rpm);
void heaterSetStartPumpRpm(uint16_t rpm);
void heaterSetStartPumpDelay(uint32_t seconds);
void heaterSetStartFireTemp(uint16_t temp);
void heaterSetCoolFanRpm(uint16_t rpm);
void heaterSetCoolStopTemp(uint16_t temp);
void heaterSetFaultThresholds(int16_t maxExhaust, int16_t maxChamber, int16_t maxAirIn);
void heaterSetIgnitionCurrentMin(uint8_t amperes);
void heaterSetVoltageRange(float vMin, float vMax);
void heaterSetBuzzerDuration(uint32_t seconds);
