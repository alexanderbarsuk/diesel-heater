#include "heater.h"
#include "config.h"
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// ─── Глобальний стан ──────────────────────────────────────────────────────────
HeaterData heater = { HEATER_OFF, 5, NAN, NAN, NAN, NAN, 0, 0.0f };

// ─── MAX6675 (raw SPI read) ───────────────────────────────────────────────────
// Протокол: CS↓ → 16-bit read MSB-first SPI_MODE0 → CS↑
// Bit 2 = 1: термопара не підключена → NAN
// Bits 14:3 = температура × 0.25 °C
static float readMAX6675(uint8_t csPin) {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(csPin, LOW);
  delayMicroseconds(2);
  uint16_t v = ((uint16_t)SPI.transfer(0) << 8) | SPI.transfer(0);
  digitalWrite(csPin, HIGH);
  SPI.endTransaction();
  if (v & 0x0004) return NAN;
  return (float)(v >> 3) * 0.25f;
}

// ─── Dallas DS18B20 ───────────────────────────────────────────────────────────
static OneWire           owOut(DS_AIR_OUT_PIN);
static OneWire           owIn(DS_AIR_IN_PIN);
static DallasTemperature dsOut(&owOut);
static DallasTemperature dsIn(&owIn);

// ─── Тахометр (лічильники переривань) ────────────────────────────────────────
static volatile uint32_t fanCount  = 0;
static volatile uint32_t pumpCount = 0;

static void fanISR()  { fanCount++;  }
static void pumpISR() { pumpCount++; }

// ─── heaterSetup ─────────────────────────────────────────────────────────────
void heaterSetup() {
  // CS піни MAX6675
  pinMode(TC_CHAMBER_CS, OUTPUT);
  pinMode(TC_EXHAUST_CS, OUTPUT);
  digitalWrite(TC_CHAMBER_CS, HIGH);
  digitalWrite(TC_EXHAUST_CS, HIGH);

  // Dallas DS18B20: запуск першої конвертації
  dsOut.begin();
  dsIn.begin();
  dsOut.setWaitForConversion(false);
  dsIn.setWaitForConversion(false);
  dsOut.requestTemperatures();
  dsIn.requestTemperatures();

  // Тахометр
  pinMode(TACH_FAN_PIN,  INPUT_PULLUP);
  pinMode(TACH_PUMP_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TACH_FAN_PIN),  fanISR,  FALLING);
  attachInterrupt(digitalPinToInterrupt(TACH_PUMP_PIN), pumpISR, FALLING);
}

// ─── heaterUpdate ────────────────────────────────────────────────────────────
// Non-blocking, викликається кожен loop(). Оновлює дані раз на 1000 мс.
void heaterUpdate() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 1000) return;
  lastMs = now;

  // MAX6675 (вбудований SPI, ~20 мкс на читання)
  heater.tempChamber = readMAX6675(TC_CHAMBER_CS);
  heater.tempExhaust = readMAX6675(TC_EXHAUST_CS);

  // DS18B20: читаємо попередню конвертацію, запускаємо нову
  float t;
  t = dsOut.getTempCByIndex(0);
  heater.tempAirOut = (t <= DEVICE_DISCONNECTED_C + 1.0f) ? NAN : t;
  t = dsIn.getTempCByIndex(0);
  heater.tempAirIn  = (t <= DEVICE_DISCONNECTED_C + 1.0f) ? NAN : t;
  dsOut.requestTemperatures();
  dsIn.requestTemperatures();

  // Тахометр: читаємо лічильники за 1 с
  uint32_t fc, pc;
  noInterrupts();
  fc = fanCount;  fanCount  = 0;
  pc = pumpCount; pumpCount = 0;
  interrupts();
  heater.fanRPM = (uint16_t)((fc * 60u) / FAN_PULSES_PER_REV);
  heater.pumpHz = (float)pc;
}

// ─── heaterToggle ─────────────────────────────────────────────────────────────
void heaterToggle() {
  if (heater.state == HEATER_OFF || heater.state == HEATER_FAULT) {
    heater.state = HEATER_RUNNING;   // TODO: замінити на HEATER_STARTING + реальну послідовність запуску
  } else if (heater.state == HEATER_RUNNING || heater.state == HEATER_STARTING) {
    heater.state = HEATER_OFF;       // TODO: замінити на HEATER_STOPPING + охолодження
  }
}

// ─── heaterSetPower ───────────────────────────────────────────────────────────
void heaterSetPower(uint8_t pwr) {
  if (pwr < 1)  pwr = 1;
  if (pwr > 10) pwr = 10;
  heater.power = pwr;
}
