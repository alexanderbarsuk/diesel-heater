#include "clk2hz.h"
#include "config.h"
#include <avr/io.h>
#include <Arduino.h>

// ─── Timer5 CTC, OC5A на піні 46 ─────────────────────────────────────────────
// f_OC5A = F_CPU / (2 * prescaler * (OCR5A + 1))
// 2 Гц:  16 000 000 / (2 * 1024 * 3906) = 2.0003 Гц
#define CLK2HZ_OCR  3905

void clk2hzSetup() {
  pinMode(CLK2HZ_PIN,    OUTPUT);
  pinMode(CLK_ECHO_IN,   INPUT);   // вхідний сигнал
  pinMode(CLK_ECHO_OUT,  OUTPUT);  // вихідний дублікат

  // Timer5 CTC mode (WGM52=1), toggle OC5A on compare match (COM5A0=1),
  // prescaler 1024 (CS52=1, CS51=0, CS50=1)
  TCCR5A = (1 << COM5A0);
  TCCR5B = (1 << WGM52) | (1 << CS52) | (1 << CS50);
  OCR5A  = CLK2HZ_OCR;
}

// ─── clk2hzUpdate ────────────────────────────────────────────────────────────
// Викликати кожен loop(). Читає CLK_ECHO_IN і дублює на CLK_ECHO_OUT.
void clk2hzUpdate() {
  digitalWrite(CLK_ECHO_OUT, digitalRead(CLK_ECHO_IN));
}
