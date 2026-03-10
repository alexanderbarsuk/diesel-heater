#pragma once
#include <stdint.h>

// ─── Коди помилок ────────────────────────────────────────────────────────────
enum FaultCode : uint8_t {
  FAULT_NONE         = 0,
  FAULT_OVERHEAT_CHM = 1,   // Перегрів камери згорання
  FAULT_OVERHEAT_EXH = 2,   // Перегрів вихлопу
  FAULT_NO_IGNITION  = 3,   // Немає запалення
  FAULT_SENSOR_CHM   = 4,   // Датчик камери несправний
  FAULT_SENSOR_EXH   = 5,   // Датчик вихлопу несправний
  FAULT_LOW_VOLTAGE  = 6,   // Низька напруга
  FAULT_OVERVOLTAGE  = 7,   // Висока напруга
  FAULT_EMERGENCY    = 8,   // Аварійний контакт
  FAULT_FUEL_OVERFLOW= 9,   // Переліво пального
  FAULT_FUEL_MIN     = 10,  // Мінімум пального в баку
};

struct FaultEntry {
  FaultCode code;
  uint8_t   arg;    // додатковий контекст (напр. температура/2 для перегріву)
};

#define FAULT_LOG_MAX  7

void        faultsInit();
void        faultLog(FaultCode code, uint8_t arg = 0);
void        faultsClear();
uint8_t     faultsCount();
FaultEntry  faultGet(uint8_t idx);
const char* faultName(FaultCode code);
const char* faultShortName(FaultCode code);  // 5cp для оверлею екрану
