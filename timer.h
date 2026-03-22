#pragma once
#include <stdint.h>

void     timerSetEnabled(bool en);
void     timerSetDuration(uint32_t seconds);
void     timerStart();
void     timerStop();
void     timerUpdate();
bool     timerIsEnabled();
bool     timerIsRunning();
uint32_t timerRemaining();
