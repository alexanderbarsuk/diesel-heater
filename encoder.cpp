#include "encoder.h"

static volatile int encoderRaw = 0;
static volatile int encoderPos = 0;

static void encoderISR() {
  static uint8_t state = 0;
  state = ((state << 2)
           | (digitalRead(ENCODER_CLK) << 1)
           | digitalRead(ENCODER_DT)) & 0x0F;

  static const int8_t tbl[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  encoderRaw += tbl[state];
  encoderPos  = encoderRaw >> 2;
}

void encoderSetup() {
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT,  INPUT_PULLUP);
  pinMode(ENCODER_SW,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT),  encoderISR, CHANGE);
}

int encoderGetPos() {
  return encoderPos;
}
