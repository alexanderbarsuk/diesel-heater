#pragma once

// ─── Піни дисплея ────────────────────────────────────────────────────────────
#define TFT_CS   53
#define TFT_DC   48
#define TFT_RST  49

// ─── Піни енкодера ───────────────────────────────────────────────────────────
#define ENCODER_CLK  2
#define ENCODER_DT   3
#define ENCODER_SW   4

// ─── Кнопки ──────────────────────────────────────────────────────────────────
#define BUTTON          5    // перемикання екранів
#define PRIMING_BTN_PIN 32   // прокачка пального

// ─── Кнопки режиму з LED (INPUT; HIGH = натиснуто) ───────────────────────────
#define BTN_HEAT_PIN  28
#define LED_HEAT_PIN  29
#define BTN_VENT_PIN  30
#define LED_VENT_PIN  31

// ─── Термопари MAX6675 (SPI: SCK=52, MISO=50) ────────────────────────────────
#define TC_CHAMBER_CS  10   // K-type: камера згоряння
#define TC_EXHAUST_CS  11   // K-type: вихлоп
#define TC_AIR_OUT_CS  12   // K-type: повітря на виході (тепле)

// ─── DS18B20 (One-Wire) ──────────────────────────────────────────────────────
#define DS_AIR_IN_PIN  23   // повітря на вході (холодне)

// ─── Тахометр ────────────────────────────────────────────────────────────────
#define TACH_FAN_PIN         18   // вентилятор (INT3 на ATmega2560)
#define TACH_PUMP_PIN        19   // паливний насос (INT2 на ATmega2560)
#define FAN_PULSES_PER_REV    2   // імпульсів на оберт вентилятора
#define PUMP_PULSES_PER_REV   1   // імпульсів на оберт насоса

// ─── ACS712 — датчик струму свічки запалювання ───────────────────────────────
// ACS712F 20A: 2.5V (ADC≈512) = 0A; чутливість 100 мВ/А ≈ 20 ADC/А
// Поріг спрацювання: ~0.75A → 15 ADC одиниць від центру
#define ACS712_IGN_PIN        A1
#define ACS712_IGN_THRESHOLD  15   // мінімальний |ADC - 512| для "є струм"
#define ACS712_IGN_DELAY_MS  1000  // затримка після ввімкнення перед перевіркою

// ─── Вимірювання напруги (дільник на A0) ─────────────────────────────────────
// Vmax = 5.0 * (R1+R2)/R2; при R1=30кОм, R2=10кОм → Vmax=20В
#define VOLT_PIN      A0
#define VOLT_R1_KOHM  30
#define VOLT_R2_KOHM  10

// ─── Контактні датчики (INPUT; HIGH = спрацювало) ────────────────────────────
#define SENSOR_EMERGENCY_PIN     24   // аварійний контакт
#define SENSOR_FUEL_OVERFLOW_PIN 25   // перелів пального
#define SENSOR_FUEL_MIN_PIN      26   // мінімум пального в баку

// ─── Виходи управління ───────────────────────────────────────────────────────
#define FUEL_VALVE_PIN   8   // паливний клапан (HIGH = відкрито)
#define IGNITION_PIN     9   // свічка запалювання (HIGH = активна)
#define CHARGER_PIN     27   // зарядка акумулятора (HIGH = заряджати)

// ─── Бузер ───────────────────────────────────────────────────────────────────
#define BUZZER_PIN  33   // активний бузер (HIGH = звук)

// ─── ШІМ виходи двигунів ─────────────────────────────────────────────────────
#define FAN_PWM_PIN   6   // вентилятор  (ATmega2560: OC4A, Timer4)
#define PUMP_PWM_PIN  7   // паливний насос (ATmega2560: OC4B, Timer4)
