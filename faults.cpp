#include "faults.h"
#include <EEPROM.h>

// EEPROM layout:
//   200..213 — 7 слотів × 2 байти (code, arg)  — кільцевий буфер
//   214      — кількість записів (0..FAULT_LOG_MAX)
//   215      — head: індекс найстарішого запису (0..FAULT_LOG_MAX-1)
//   216      — seed magic (0xB2 = тестові помилки вже додано)
#define FAULT_EEPROM_BASE  200
#define FAULT_COUNT_ADDR   214
#define FAULT_HEAD_ADDR    215
#define FAULT_SEEDED_ADDR  216

static uint8_t _count = 0;
static uint8_t _head  = 0;   // індекс найстарішого запису в кільці

void faultsInit() {
  _count = EEPROM.read(FAULT_COUNT_ADDR);
  _head  = EEPROM.read(FAULT_HEAD_ADDR);
  if (_count > FAULT_LOG_MAX) { _count = 0; EEPROM.write(FAULT_COUNT_ADDR, 0); }
  if (_head  >= FAULT_LOG_MAX) { _head  = 0; EEPROM.write(FAULT_HEAD_ADDR,  0); }

  if (EEPROM.read(FAULT_SEEDED_ADDR) != 0xB2) {
    faultLog(FAULT_OVERHEAT_CHM, 160);   // arg=160 → 320°C (arg×2)
    faultLog(FAULT_NO_IGNITION,    0);
    faultLog(FAULT_SENSOR_EXH,     0);
    EEPROM.write(FAULT_SEEDED_ADDR, 0xB2);
  }
}

void faultLog(FaultCode code, uint8_t arg) {
  uint8_t slot;
  if (_count < FAULT_LOG_MAX) {
    // є вільне місце — пишемо в кінець кільця
    slot = (_head + _count) % FAULT_LOG_MAX;
    _count++;
    EEPROM.write(FAULT_COUNT_ADDR, _count);
  } else {
    // буфер повний — витісняємо найстаріший запис
    slot  = _head;
    _head = (_head + 1) % FAULT_LOG_MAX;
    EEPROM.write(FAULT_HEAD_ADDR, _head);
  }
  EEPROM.write(FAULT_EEPROM_BASE + slot * 2,     (uint8_t)code);
  EEPROM.write(FAULT_EEPROM_BASE + slot * 2 + 1, arg);
}

void faultsClear() {
  _count = 0;
  _head  = 0;
  EEPROM.write(FAULT_COUNT_ADDR, 0);
  EEPROM.write(FAULT_HEAD_ADDR,  0);
}

uint8_t faultsCount() { return _count; }

FaultEntry faultGet(uint8_t idx) {
  FaultEntry e = { FAULT_NONE, 0 };
  if (idx >= _count) return e;
  uint8_t  slot = (_head + idx) % FAULT_LOG_MAX;
  uint16_t addr = FAULT_EEPROM_BASE + slot * 2;
  e.code = (FaultCode)EEPROM.read(addr);
  e.arg  = EEPROM.read(addr + 1);
  return e;
}

const char* faultShortName(FaultCode code) {
  switch (code) {
    case FAULT_OVERHEAT_CHM: return "Е:КАМ";
    case FAULT_OVERHEAT_EXH: return "Е:ВИХ";
    case FAULT_NO_IGNITION:  return "Е:ЗАП";
    case FAULT_SENSOR_CHM:   return "Е:СКМ";
    case FAULT_SENSOR_EXH:   return "Е:СВХ";
    case FAULT_LOW_VOLTAGE:  return "Е:НАП";
    case FAULT_OVERVOLTAGE:  return "Е:ВАП";
    case FAULT_EMERGENCY:    return "Е:КОН";
    case FAULT_FUEL_OVERFLOW:return "Е:ПЕР";
    case FAULT_FUEL_MIN:     return "Е:БАК";
    default:                 return "Е:???";
  }
}

const char* faultName(FaultCode code) {
  switch (code) {
    case FAULT_OVERHEAT_CHM: return "ПЕРЕГРІВ КАМЕРИ";
    case FAULT_OVERHEAT_EXH: return "ПЕРЕГРІВ ВИХЛОПУ";
    case FAULT_NO_IGNITION:  return "НЕ ЗАПАЛЮЄТЬСЯ";
    case FAULT_SENSOR_CHM:   return "ДАТЧИК КАМЕРИ";
    case FAULT_SENSOR_EXH:   return "ДАТЧИК ВИХЛОПУ";
    case FAULT_LOW_VOLTAGE:  return "НИЗЬКА НАПРУГА";
    case FAULT_OVERVOLTAGE:  return "ВИСОКА НАПРУГА";
    case FAULT_EMERGENCY:    return "АВАРІЙНИЙ КОНТАКТ";
    case FAULT_FUEL_OVERFLOW:return "ПЕРЕЛІВО ПАЛЬНОГО";
    case FAULT_FUEL_MIN:     return "МІНІМУМ ПАЛЬНОГО";
    default:                 return "НЕВІДОМА ПОМИЛКА";
  }
}
