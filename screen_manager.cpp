#include "screen_manager.h"
#include "screen_greeting.h"
#include "screen_sensors.h"
#include "screen_settings.h"
#include "screen_faults.h"

typedef struct {
  void (*init)();
  void (*update)();
} ScreenDef;

static const ScreenDef screens[] = {
  { greetingInit,      greetingUpdate      },
  { sensorsInit,       sensorsUpdate       },
  { settingsInit,      settingsUpdate      },
  { faultsScreenInit,  faultsScreenUpdate  },
};

static const uint8_t SCREEN_COUNT = 4;
static uint8_t current = 0;

void screenManagerInit() {
  screens[current].init();
}

void screenManagerUpdate() {
  screens[current].update();
}

void screenManagerNext() {
  // Екран привітання (0) показується тільки раз при старті — в ручному циклі пропускається
  if (current == 0) current = 1;
  else current = (current < SCREEN_COUNT - 1) ? current + 1 : 1;
  screens[current].init();
}
