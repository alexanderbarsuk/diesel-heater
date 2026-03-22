#include "heater.h"
#include "config.h"
#include "faults.h"
#include <SPI.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <math.h>

// ─── Таблиця потужності (цільові оберти для ПІД) ────────────────────────────
// Насос (1 імп/об): 60 RPM = 1 Гц, 420 RPM = 7 Гц
PowerStep POWER_TABLE[10] = {
  {1200,  60},  //  1 — мінімум  (1 Гц насос)
  {1550,  78},  //  2
  {1950, 102},  //  3
  {2350, 132},  //  4
  {2750, 168},  //  5
  {3150, 210},  //  6
  {3550, 258},  //  7
  {3950, 312},  //  8
  {4350, 372},  //  9
  {4800, 420},  // 10 — максимум (7 Гц насос)
};

// ─── ПІД-регулятори (velocity-form PI, Ts = 1 с) ─────────────────────────────
// Δu = Kp*(e−ePrev) + Ki*e;  u[k] = clamp(u[k−1] + Δu, 0, 255)
struct PidCtrl {
  int16_t prevErr;   // e[k-1], RPM
  uint8_t  pwm;      // u[k-1], ШІМ 0..255
};

static PidCtrl fanPid  = {0, 0};
static PidCtrl pumpPid = {0, 0};

// Динамічні коефіцієнти [ШІМ/RPM] (змінюються через heaterSetPid)
static float fanKp  = 0.04f;
static float fanKi  = 0.008f;
static float pumpKp = 0.40f;
static float pumpKi = 0.08f;

static uint8_t pidStep(PidCtrl& pid, uint16_t target, uint16_t measured,
                        float kp, float ki) {
  if (target == 0) {
    pid.prevErr = 0;
    pid.pwm     = 0;
    return 0;
  }
  int16_t err   = (int16_t)target - (int16_t)measured;
  float   delta = kp * (float)(err - pid.prevErr) + ki * (float)err;
  pid.prevErr   = err;
  int16_t out   = (int16_t)pid.pwm + (int16_t)delta;
  if (out <   0) out =   0;
  if (out > 255) out = 255;
  pid.pwm = (uint8_t)out;
  return pid.pwm;
}

// ─── AHT20 + BMP280 ──────────────────────────────────────────────────────────
static Adafruit_AHTX0 aht;
static Adafruit_BMP280 bmp;
static bool ahtOk = false;
static bool bmpOk = false;

// ─── Глобальний стан ──────────────────────────────────────────────────────────
HeaterData heater = { HEATER_OFF, 5, NAN, NAN, NAN, NAN, 0, 0, 0, 0, 0.0f, MODE_HEAT, NAN, NAN, NAN, NAN };

static float    chargerThreshold  = 10.5f;   // В; змінюється через heaterSetChargerThreshold()
static uint8_t  primingMode       = 0;       // 0=утримання, 1=таймер
static uint32_t primingDurationMs = 10000;   // мс; змінюється через heaterSetPrimingDuration()

// ─── Параметри послідовності запуску/зупинки ──────────────────────────────────
static uint32_t startupStartMs  = 0;      // millis() початку STARTING/RESTARTING
static uint32_t ignitionTimeMs  = 60000;  // тривалість роботи свічки (мс)
static uint16_t startFanRpm     = 1200;   // оберти вент. під час запуску
static uint16_t startPumpRpm    = 60;     // оберти насоса під час запуску
static uint32_t startPumpDelayMs = 0;     // затримка запуску насоса (мс)
static uint16_t startFireTemp   = 300;    // температура займання (°C)
static uint16_t coolFanRpm      = 1500;   // оберти вент. під час охолодження
static uint16_t coolStopTemp    = 80;     // температура зупинки (°C)

// ─── Пороги аварій ────────────────────────────────────────────────────────────
static int16_t faultMaxExhaust  = 380;  // °C перегрів вихлопу
static int16_t faultMaxChamber  = 310;  // °C перегрів камери
static int16_t faultMaxAirIn    = 20;   // °C перегрів впуску
// ACS712: 100 мВ/А, ADC 10-bit 5В → ~20.5 ADC/А; за замовч. 2А → 41 ADC
static int16_t ignCurrentMinAdc = 41;   // мінімальний |ADC-512| для "є струм"
static float    voltageMin      = 8.0f;
static float    voltageMax      = 30.0f;
static bool     faultPending    = false; // аварія під час роботи → STOPPING → FAULT
static uint32_t buzzerDurationMs = 3000; // тривалість сигналу бузера (мс)
static uint32_t buzzerEndMs      = 0;    // millis() завершення; 0 = не активний

static void updateOutputs();  // forward declaration

// Логує аварію і переводить обігрівач у STOPPING (або одразу FAULT якщо неактивний)
static void triggerFault(FaultCode code, uint8_t arg = 0) {
  faultLog(code, arg);
  if (buzzerDurationMs > 0) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerEndMs = millis() + buzzerDurationMs;
  }
  if (heater.state != HEATER_OFF  &&
      heater.state != HEATER_FAULT &&
      heater.state != HEATER_STOPPING) {
    faultPending = true;
    pumpPid      = {0, 0};
    heater.state = HEATER_STOPPING;
    updateOutputs();
  } else if (heater.state != HEATER_STOPPING) {
    heater.state = HEATER_FAULT;
  }
}

static float readVoltage() {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(VOLT_PIN);
  float vAdc = (float)(sum >> 2) * (5.0f / 1023.0f);
  return vAdc * (float)(VOLT_R1_KOHM + VOLT_R2_KOHM) / (float)VOLT_R2_KOHM;
}

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
static OneWire           owIn(DS_AIR_IN_PIN);
static DallasTemperature dsIn(&owIn);

// ─── Тахометр (лічильники переривань) ────────────────────────────────────────
static volatile uint32_t fanCount  = 0;
static volatile uint32_t pumpCount = 0;

static void fanISR()  { fanCount++;  }
static void pumpISR() { pumpCount++; }

// ─── updateModeLeds ──────────────────────────────────────────────────────────
static void updateModeLeds() {
  digitalWrite(LED_HEAT_PIN, heater.mode == MODE_HEAT ? HIGH : LOW);
  digitalWrite(LED_VENT_PIN, heater.mode == MODE_VENT ? HIGH : LOW);
}

// ─── updateOutputs ───────────────────────────────────────────────────────────
// Синхронізує клапан і свічку із поточним станом. Викликати після зміни стану.
static void updateOutputs() {
  bool valveOpen = (heater.state == HEATER_STARTING    ||
                    heater.state == HEATER_RESTARTING  ||
                    heater.state == HEATER_RUNNING      ||
                    heater.state == HEATER_PRIMING);
  // Свічка активна тільки під час запуску і поки не минув час ignitionTimeMs
  bool ignActive = (heater.state == HEATER_STARTING || heater.state == HEATER_RESTARTING) &&
                   (ignitionTimeMs == 0 || (millis() - startupStartMs < ignitionTimeMs));
  digitalWrite(FUEL_VALVE_PIN, valveOpen ? HIGH : LOW);
  digitalWrite(IGNITION_PIN,   ignActive ? HIGH : LOW);
}

// ─── heaterSetup ─────────────────────────────────────────────────────────────
void heaterSetup() {
  // AHT20 + BMP280 (I2C: SDA=20, SCL=21)
  Wire.begin();
  ahtOk = aht.begin();
  bmpOk = bmp.begin(0x76);
  if (!bmpOk) bmpOk = bmp.begin(0x77);

  // CS піни MAX6675
  pinMode(TC_CHAMBER_CS, OUTPUT);
  pinMode(TC_EXHAUST_CS, OUTPUT);
  pinMode(TC_AIR_OUT_CS, OUTPUT);
  digitalWrite(TC_CHAMBER_CS, HIGH);
  digitalWrite(TC_EXHAUST_CS, HIGH);
  digitalWrite(TC_AIR_OUT_CS, HIGH);

  // Dallas DS18B20: запуск першої конвертації
  dsIn.begin();
  dsIn.setWaitForConversion(false);
  dsIn.requestTemperatures();

  // Цифрові виходи (закрито/вимкнено на старті)
  pinMode(FUEL_VALVE_PIN, OUTPUT);
  pinMode(IGNITION_PIN,   OUTPUT);
  pinMode(CHARGER_PIN,    OUTPUT);
  pinMode(BUZZER_PIN,     OUTPUT);
  digitalWrite(FUEL_VALVE_PIN, LOW);
  digitalWrite(IGNITION_PIN,   LOW);
  digitalWrite(CHARGER_PIN,    LOW);
  digitalWrite(BUZZER_PIN,     LOW);

  // ШІМ виходи двигунів (вимкнено на старті)
  pinMode(FAN_PWM_PIN,  OUTPUT);
  pinMode(PUMP_PWM_PIN, OUTPUT);
  analogWrite(FAN_PWM_PIN,  0);
  analogWrite(PUMP_PWM_PIN, 0);

  // Кнопки режиму з LED
  pinMode(BTN_HEAT_PIN, INPUT);
  pinMode(BTN_VENT_PIN, INPUT);
  pinMode(LED_HEAT_PIN, OUTPUT);
  pinMode(LED_VENT_PIN, OUTPUT);
  updateModeLeds();  // Нагрів активний за замовчуванням

  // Кнопка прокачки пального
  pinMode(PRIMING_BTN_PIN, INPUT);

  // Контактні датчики
  pinMode(SENSOR_EMERGENCY_PIN,     INPUT);
  pinMode(SENSOR_FUEL_OVERFLOW_PIN, INPUT);
  pinMode(SENSOR_FUEL_MIN_PIN,      INPUT);

  // Тахометр
  pinMode(TACH_FAN_PIN,  INPUT_PULLUP);
  pinMode(TACH_PUMP_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TACH_FAN_PIN),  fanISR,  FALLING);
  attachInterrupt(digitalPinToInterrupt(TACH_PUMP_PIN), pumpISR, FALLING);
}

// ─── heaterUpdate ────────────────────────────────────────────────────────────
// Non-blocking, викликається кожен loop(). Оновлює дані раз на 1000 мс.
void heaterUpdate() {
  // ── Бузер: вимикаємо після закінчення тривалості ─────────────────────────
  if (buzzerEndMs != 0 && millis() >= buzzerEndMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerEndMs = 0;
  }

  // ── Прокачка: перевірка кнопки (без затримки, кожен виклик) ─────────────
  static bool     prevBtn        = false;  // LOW = не натиснуто (INPUT)
  static uint32_t primingStartMs = 0;
  bool btn = digitalRead(PRIMING_BTN_PIN);
  if (btn != prevBtn) {
    if (btn && (heater.state == HEATER_OFF || heater.state == HEATER_FAULT)) {
      // Натиснули → прокачка: насос на максимум, вентилятор стоп
      heater.state   = HEATER_PRIMING;
      heater.pumpPwm = 255;
      heater.fanPwm  = 0;
      analogWrite(PUMP_PWM_PIN, 255);
      analogWrite(FAN_PWM_PIN,  0);
      pumpPid        = {0, 0};
      primingStartMs = millis();
    } else if (!btn && heater.state == HEATER_PRIMING && primingMode == 0) {
      // Відпустили → зупинка тільки в режимі утримання
      heater.state   = HEATER_OFF;
      heater.pumpPwm = 0;
      heater.fanPwm  = 0;
      analogWrite(PUMP_PWM_PIN, 0);
      analogWrite(FAN_PWM_PIN,  0);
      pumpPid = {0, 0};
    }
    prevBtn = btn;
    updateOutputs();
  }
  // Таймерний режим: авто-зупинка після заданого часу
  if (primingMode == 1 && heater.state == HEATER_PRIMING &&
      primingDurationMs > 0 && (millis() - primingStartMs >= primingDurationMs)) {
    heater.state   = HEATER_OFF;
    heater.pumpPwm = 0;
    heater.fanPwm  = 0;
    analogWrite(PUMP_PWM_PIN, 0);
    analogWrite(FAN_PWM_PIN,  0);
    pumpPid = {0, 0};
    updateOutputs();
  }

  // ── Кнопки режиму (тільки при вимкненому обігрівачі) ─────────────────────
  static bool prevBtnHeat = false;
  static bool prevBtnVent = false;
  bool btnHeat = digitalRead(BTN_HEAT_PIN);
  bool btnVent = digitalRead(BTN_VENT_PIN);
  if (heater.state == HEATER_OFF || heater.state == HEATER_FAULT) {
    if (btnHeat && !prevBtnHeat && heater.mode != MODE_HEAT) {
      heater.mode = MODE_HEAT;
      updateModeLeds();
    }
    if (btnVent && !prevBtnVent && heater.mode != MODE_VENT) {
      heater.mode = MODE_VENT;
      updateModeLeds();
    }
  }
  prevBtnHeat = btnHeat;
  prevBtnVent = btnVent;

  if (heater.state == HEATER_PRIMING) return;

  // ── Перевірка струму свічки через ACS712_IGN_DELAY_MS після ввімкнення ──────
  // Виконується кожен loop() — не блокується 1-секундним гейтом
  {
    static bool ignCurrentChecked = false;
    bool ignActive = (heater.state == HEATER_STARTING ||
                      heater.state == HEATER_RESTARTING) &&
                     (ignitionTimeMs == 0 ||
                      millis() - startupStartMs < ignitionTimeMs);
    if (!ignActive) {
      ignCurrentChecked = false;  // скидаємо при вимкненні свічки
    } else if (!ignCurrentChecked &&
               millis() - startupStartMs >= ACS712_IGN_DELAY_MS) {
      ignCurrentChecked = true;
      if (ignCurrentMinAdc > 0) {
        int16_t raw   = (int16_t)analogRead(ACS712_IGN_PIN);
        int16_t delta = raw - 512;
        if (delta < 0) delta = -delta;
        if (delta < ignCurrentMinAdc) {
          triggerFault(FAULT_IGNITION_OPEN);
        }
      }
    }
  }

  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 1000) return;
  lastMs = now;

  // MAX6675 (вбудований SPI, ~20 мкс на читання)
  heater.tempChamber = readMAX6675(TC_CHAMBER_CS);
  heater.tempExhaust = readMAX6675(TC_EXHAUST_CS);
  heater.tempAirOut  = readMAX6675(TC_AIR_OUT_CS);

  // AHT20 + BMP280
  if (ahtOk) {
    sensors_event_t hum, temp;
    aht.getEvent(&hum, &temp);
    heater.ambientTemp = temp.temperature;
    heater.humidity    = hum.relative_humidity;
  }
  if (bmpOk) {
    heater.pressure = bmp.readPressure() / 100.0f;   // Па → гПа
  }

  // DS18B20: читаємо попередню конвертацію, запускаємо нову
  float t = dsIn.getTempCByIndex(0);
  heater.tempAirIn  = (t <= DEVICE_DISCONNECTED_C + 1.0f) ? NAN : t;
  dsIn.requestTemperatures();

  // Тахометр: читаємо лічильники за 1 с → об/хв
  uint32_t fc, pc;
  noInterrupts();
  fc = fanCount;  fanCount  = 0;
  pc = pumpCount; pumpCount = 0;
  interrupts();
  heater.fanRpm  = (uint16_t)((fc * 60u) / FAN_PULSES_PER_REV);
  heater.pumpRpm = (uint16_t)((pc * 60u) / PUMP_PULSES_PER_REV);

  // Контактні датчики (HIGH = спрацювало, INPUT)
  static bool prevEmerg    = false;
  static bool prevOverflow = false;
  static bool prevFuelMin  = false;
  bool emerg    = digitalRead(SENSOR_EMERGENCY_PIN);
  bool overflow = digitalRead(SENSOR_FUEL_OVERFLOW_PIN);
  bool fuelMin  = digitalRead(SENSOR_FUEL_MIN_PIN);
  if (emerg    && !prevEmerg)    triggerFault(FAULT_EMERGENCY);
  if (overflow && !prevOverflow) triggerFault(FAULT_FUEL_OVERFLOW);
  if (fuelMin  && !prevFuelMin)  triggerFault(FAULT_FUEL_MIN);
  prevEmerg    = emerg;
  prevOverflow = overflow;
  prevFuelMin  = fuelMin;

  // Напруга живлення та керування зарядкою
  heater.voltage = readVoltage();
  digitalWrite(CHARGER_PIN, (heater.voltage < chargerThreshold) ? HIGH : LOW);

  // ACS712: струм свічки (оновлюємо тільки коли активна)
  bool ignNow = (heater.state == HEATER_STARTING || heater.state == HEATER_RESTARTING) &&
                (ignitionTimeMs == 0 || now - startupStartMs < ignitionTimeMs);
  if (ignNow) {
    int16_t raw   = (int16_t)analogRead(ACS712_IGN_PIN);
    int16_t delta = raw - 512;
    if (delta < 0) delta = -delta;
    heater.ignitCurrent = (float)delta / 20.5f;
  } else {
    heater.ignitCurrent = NAN;
  }

  // ─── Перевірка аварій (тільки під час активної роботи) ───────────────────
  bool isActive = (heater.state == HEATER_RUNNING   ||
                   heater.state == HEATER_STARTING  ||
                   heater.state == HEATER_RESTARTING);
  if (isActive) {
    if (!isnan(heater.tempExhaust) && heater.tempExhaust > (float)faultMaxExhaust)
      triggerFault(FAULT_OVERHEAT_EXH, (uint8_t)((uint16_t)heater.tempExhaust >> 1));
    else if (!isnan(heater.tempChamber) && heater.tempChamber > (float)faultMaxChamber)
      triggerFault(FAULT_OVERHEAT_CHM, (uint8_t)((uint16_t)heater.tempChamber >> 1));
    else if (!isnan(heater.tempAirIn) && heater.tempAirIn > (float)faultMaxAirIn)
      triggerFault(FAULT_OVERHEAT_AIR);
    else if (heater.voltage < voltageMin)
      triggerFault(FAULT_LOW_VOLTAGE);
    else if (heater.voltage > voltageMax)
      triggerFault(FAULT_OVERVOLTAGE);
  }

  // Занизька температура вихлопу під час RUNNING → RESTARTING (згасла полум'я)
  if (heater.state == HEATER_RUNNING &&
      !isnan(heater.tempExhaust) &&
      heater.tempExhaust < (float)startFireTemp) {
    heater.state   = HEATER_RESTARTING;
    startupStartMs = now;
    updateOutputs();
  }

  // ─── Автомат стану запуску/зупинки ────────────────────────────────────────
  if (heater.state == HEATER_STARTING || heater.state == HEATER_RESTARTING) {
    bool fired    = !isnan(heater.tempExhaust) && heater.tempExhaust >= (float)startFireTemp;
    bool timedOut = (ignitionTimeMs > 0) && (now - startupStartMs >= ignitionTimeMs);
    if (fired) {
      heater.state = HEATER_RUNNING;
      updateOutputs();
    } else if (timedOut) {
      if (heater.state == HEATER_RESTARTING) {
        faultLog(FAULT_NO_IGNITION, 0);
        pumpPid      = {0, 0};
        heater.state = HEATER_STOPPING;
      } else {
        heater.state   = HEATER_RESTARTING;
        startupStartMs = now;
      }
      updateOutputs();
    }
  }

  if (heater.state == HEATER_STOPPING) {
    bool cooled = !isnan(heater.tempChamber) && heater.tempChamber <= (float)coolStopTemp;
    if (cooled) {
      fanPid       = {0, 0};
      heater.state = faultPending ? HEATER_FAULT : HEATER_OFF;
      faultPending = false;
      updateOutputs();
    }
  }

  // ─── ПІД-регулятори ────────────────────────────────────────────────────────
  uint16_t fTarget = 0, pTarget = 0;
  switch (heater.state) {
    case HEATER_STARTING:
    case HEATER_RESTARTING:
      fTarget = startFanRpm;
      pTarget = (startPumpDelayMs == 0 || (now - startupStartMs) >= startPumpDelayMs)
                ? startPumpRpm : 0;
      break;
    case HEATER_RUNNING:
      fTarget = POWER_TABLE[heater.power - 1].fanRpm;
      pTarget = POWER_TABLE[heater.power - 1].pumpRpm;
      break;
    case HEATER_STOPPING:
      fTarget = coolFanRpm;
      pTarget = 0;
      break;
    default: break;
  }
  uint8_t fPwm = pidStep(fanPid,  fTarget, heater.fanRpm,  fanKp,  fanKi);
  uint8_t pPwm = pidStep(pumpPid, pTarget, heater.pumpRpm, pumpKp, pumpKi);

  if (heater.fanPwm != fPwm) {
    heater.fanPwm = fPwm;
    analogWrite(FAN_PWM_PIN, fPwm);
  }
  if (heater.pumpPwm != pPwm) {
    heater.pumpPwm = pPwm;
    analogWrite(PUMP_PWM_PIN, pPwm);
  }
}

// ─── heaterToggle ─────────────────────────────────────────────────────────────
void heaterToggle() {
  if (heater.state == HEATER_OFF || heater.state == HEATER_FAULT) {
    fanPid         = {0, 0};
    pumpPid        = {0, 0};
    faultPending   = false;
    startupStartMs = millis();
    heater.state   = HEATER_STARTING;
  } else if (heater.state == HEATER_RUNNING  ||
             heater.state == HEATER_STARTING ||
             heater.state == HEATER_RESTARTING) {
    pumpPid      = {0, 0};  // зупиняємо насос; вентилятор продовжує через coolFanRpm
    heater.state = HEATER_STOPPING;
  }
  updateOutputs();
}

// ─── heaterSetPowerStep ───────────────────────────────────────────────────────
void heaterSetPowerStep(uint8_t step, uint16_t fanRpm, uint16_t pumpRpm) {
  if (step >= 10) return;
  POWER_TABLE[step].fanRpm  = fanRpm;
  POWER_TABLE[step].pumpRpm = pumpRpm;
}

// ─── heaterSetPid ─────────────────────────────────────────────────────────────
void heaterSetPid(float fKp, float fKi, float pKp, float pKi) {
  fanKp  = fKp;
  fanKi  = fKi;
  pumpKp = pKp;
  pumpKi = pKi;
}

// ─── heaterSetChargerThreshold ───────────────────────────────────────────────
void heaterSetChargerThreshold(float threshV) {
  chargerThreshold = threshV;
}

// ─── heaterSetPrimingMode / heaterSetPrimingDuration ─────────────────────────
void heaterSetPrimingMode(uint8_t mode) {
  primingMode = mode;
}

void heaterSetPrimingDuration(uint32_t seconds) {
  primingDurationMs = seconds * 1000UL;
}

// ─── Setter-и параметрів запуску/зупинки ──────────────────────────────────────
void heaterSetIgnitionTime(uint32_t seconds)   { ignitionTimeMs   = seconds * 1000UL; }
void heaterSetStartFanRpm(uint16_t rpm)        { startFanRpm      = rpm; }
void heaterSetStartPumpRpm(uint16_t rpm)       { startPumpRpm     = rpm; }
void heaterSetStartPumpDelay(uint32_t seconds) { startPumpDelayMs = seconds * 1000UL; }
void heaterSetStartFireTemp(uint16_t temp)     { startFireTemp  = temp; }
void heaterSetCoolFanRpm(uint16_t rpm)         { coolFanRpm     = rpm; }
void heaterSetCoolStopTemp(uint16_t temp)      { coolStopTemp   = temp; }

// ─── heaterSetFaultThresholds / heaterSetVoltageRange ────────────────────────
void heaterSetFaultThresholds(int16_t maxExhaust, int16_t maxChamber, int16_t maxAirIn) {
  faultMaxExhaust = maxExhaust;
  faultMaxChamber = maxChamber;
  faultMaxAirIn   = maxAirIn;
}

void heaterSetIgnitionCurrentMin(uint8_t amperes) {
  // ACS712F 20A: 100 мВ/А; ADC 10-bit, Vref=5В → 1А ≈ 20.5 ADC одиниці
  ignCurrentMinAdc = (int16_t)amperes * 205 / 10;
}

void heaterSetVoltageRange(float vMin, float vMax) {
  voltageMin = vMin;
  voltageMax = vMax;
}

void heaterSetBuzzerDuration(uint32_t seconds) {
  buzzerDurationMs = seconds * 1000UL;
}

// ─── heaterSetPower ───────────────────────────────────────────────────────────
void heaterSetPower(uint8_t pwr) {
  if (pwr < 1)  pwr = 1;
  if (pwr > 10) pwr = 10;
  heater.power = pwr;
}
