#pragma once

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ─── Піни дисплея ───────────────────────────────────────────────────────────
#define TFT_CS  53
#define TFT_DC  48
#define TFT_RST 49

// ─── Піни енкодера ──────────────────────────────────────────────────────────
#define ENCODER_CLK 2
#define ENCODER_DT  3
#define ENCODER_SW  4

// ─── Додаткова кнопка ───────────────────────────────────────────────────────
#define BUTTON 5

// ─── Термопари MAX6675 (SPI: SCK=52, MISO=50) ───────────────────────────────
#define TC_CHAMBER_CS   10   // K-type: камера згоряння
#define TC_EXHAUST_CS   11   // K-type: вихлоп

// ─── DS18B20 (One-Wire) ──────────────────────────────────────────────────────
#define DS_AIR_OUT_PIN  22   // повітря на виході (тепле)
#define DS_AIR_IN_PIN   23   // повітря на вході (холодне)

// ─── Тахометр (імпульсний вхід) ──────────────────────────────────────────────
#define TACH_FAN_PIN    18   // вентилятор (INT3 на ATmega2560)
#define TACH_PUMP_PIN   19   // паливний насос (INT2 на ATmega2560)
#define FAN_PULSES_PER_REV  2  // імпульсів на оберт вентилятора
