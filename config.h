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
