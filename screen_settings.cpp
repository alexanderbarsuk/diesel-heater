#include "screen_settings.h"
#include "uaprint.h"
#include "encoder.h"
#include "config.h"
#include <EEPROM.h>

// ─── Типи елементів меню ─────────────────────────────────────────────────────
enum MenuItemType : uint8_t {
  ITEM_SLIDER,    // int16_t в [minVal..maxVal], відображається з одиницями
  ITEM_CHECKBOX,  // int16_t 0/1 → "ВИКЛ"/"ВКЛ"
  ITEM_TIME,      // uint32_t секунди (0..86399), 3 поля редагування: Г/Х/С
  ITEM_RANGE,     // два int16_t packed lo|hi<<16, обидва в [minVal..maxVal], lo≤hi
  ITEM_SELECT,    // int16_t індекс 0..maxVal в масиві data[]
  ITEM_SUBMENU,   // перехід у підменю; data → SubMenuDef; valIdx=NO_VAL
  ITEM_BACK,      // повернення до батьківського меню; valIdx=NO_VAL
};

#define NO_VAL 0xFF  // valIdx для ITEM_SUBMENU та ITEM_BACK

// ─── Структура підменю ───────────────────────────────────────────────────────
struct MenuItem;  // forward declaration

struct SubMenuDef {
  const char*     title;   // заголовок підменю (UTF-8, до 12 символів)
  const MenuItem* items;   // масив елементів
  uint8_t         count;   // кількість елементів
};

// ─── Структура елемента меню ─────────────────────────────────────────────────
struct MenuItem {
  const char*   label;      // UTF-8, рівно 10 видимих символів
  MenuItemType  type;
  uint8_t       valIdx;     // індекс у values[]; NO_VAL для SUBMENU/BACK
  uint8_t       eepromAddr; // EEPROM-адреса (2B для SLIDER/CHECKBOX/SELECT, 4B для TIME/RANGE)
  int16_t       minVal;     // SLIDER/RANGE: нижня межа; SELECT: 0; SUBMENU/BACK: 0
  int16_t       maxVal;     // SLIDER/RANGE: верхня межа; SELECT: кількість опцій-1
  int32_t       defaultVal; // значення за замовчуванням
  const char*   units;      // UTF-8 рядок одиниць або NULL
  const void*   data;       // SELECT: const char**; SUBMENU: const SubMenuDef*; інакше NULL
};

// ─── Рядки одиниць ───────────────────────────────────────────────────────────
static const char UNITS_C[]   = "\xC2\xB0""C";              // °C  (2 CP)
static const char UNITS_PCT[] = "%";
static const char UNITS_HPA[] = "\xD0\xB3\xD0\x9F\xD0\xB0"; // гПа (3 CP)

// ─── Опції для ITEM_SELECT ───────────────────────────────────────────────────
static const char* modeOpts[]  = { "Авто  ", "Нагрів", "Вент  " };
static const char* resetOpts[] = { "Ні ", "Так" };

// ─── Підменю "КОРЕКЦІЇ" ───────────────────────────────────────────────────────
// Для додавання нових підменю: 1) визначте масив MenuItem + SubMenuDef,
// 2) додайте ITEM_SUBMENU до відповідного меню,
// 3) додайте &yourSub до allSubMenus[].
static const MenuItem corrItems[] = {
  // label           type       valIdx  addr  min  max  default  units      data
  { "< Назад   ", ITEM_BACK,   NO_VAL, 0,     0,   0,  0,       nullptr,   nullptr   },
  { "Корект.тем", ITEM_SLIDER,  14,    32,   -5,   5,  0,       UNITS_C,   nullptr   },
  { "Корект.вол", ITEM_SLIDER,  15,    34,  -10,  10,  0,       UNITS_PCT, nullptr   },
  { "Корект.тис", ITEM_SLIDER,  16,    36,  -50,  50,  0,       UNITS_HPA, nullptr   },
};
static const SubMenuDef corrSubDef = { "КОРЕКЦІЇ", corrItems, 4 };

// ─── Головне меню (15 пунктів) ───────────────────────────────────────────────
// valIdx = індекс у values[] (0..16); NO_VAL = без значення (SUBMENU/BACK)
// EEPROM: 2B на SLIDER/CHECKBOX/SELECT, 4B на TIME/RANGE
static const MenuItem rootItems[] = {
  // label           type            valIdx addr  min   max  default                    units       data
  { "Темп уст. ", ITEM_SLIDER,    0,    0,    5,   99,  22,                            UNITS_C,    nullptr                       },
  { "Гістерезис", ITEM_SLIDER,    1,    2,    1,   10,   1,                            UNITS_C,    nullptr                       },
  { "Вент. мін%", ITEM_SLIDER,    2,    4,    0,  100,  20,                            UNITS_PCT,  nullptr                       },
  { "Режим     ", ITEM_SELECT,    3,    6,    0,    2,   0,                            nullptr,    (const void*)modeOpts          },
  { "Потуж.нагр", ITEM_SLIDER,    4,    8,    0,  100,  80,                            UNITS_PCT,  nullptr                       },
  { "Темп нічна", ITEM_SLIDER,    5,   10,    5,   30,  18,                            UNITS_C,    nullptr                       },
  { "Темп денна", ITEM_SLIDER,    6,   12,    5,   30,  22,                            UNITS_C,    nullptr                       },
  { "Розклад   ", ITEM_CHECKBOX,  7,   14,    0,    1,   0,                            nullptr,    nullptr                       },
  { "Яскравість", ITEM_SLIDER,    8,   16,   10,  100, 100,                            UNITS_PCT,  nullptr                       },
  { "Вент. макс", ITEM_SLIDER,    9,   18,    0,  100, 100,                            UNITS_PCT,  nullptr                       },
  { "Поч.час   ", ITEM_TIME,     10,   20,    0,    0,  (int32_t)(6UL*3600),           nullptr,    nullptr                       },
  { "Кін.час   ", ITEM_TIME,     11,   24,    0,    0,  (int32_t)(22UL*3600),          nullptr,    nullptr                       },
  { "Темп діап ", ITEM_RANGE,    12,   28,    5,   40,  (int32_t)((uint16_t)18|((uint32_t)(uint16_t)25<<16)), UNITS_C, nullptr   },
  { "Корекції  ", ITEM_SUBMENU, NO_VAL, 0,    0,    0,   0,                            nullptr,    &corrSubDef                   },
  { "Скид нал. ", ITEM_SELECT,  13,   38,    0,    1,   0,                            nullptr,    (const void*)resetOpts         },
};
#define ROOT_COUNT  15

// ─── Реєстр усіх підменю (для saveAll/loadAll) ───────────────────────────────
// Додайте сюди кожен новий SubMenuDef для автоматичного збереження/завантаження.
static const SubMenuDef* allSubMenus[] = { &corrSubDef };
#define NUM_SUBMENUS  1

#define TOTAL_VALUES   17   // кількість слотів у values[] (valIdx 0..16)
#define ITEMS_PER_PAGE  4

#define EEPROM_MAGIC_ADDR  100
#define EEPROM_MAGIC_VAL   0xA6

// ─── Розмітка рядків (SF Mono 22pt 2-bit AA, UA_ADVANCE=14, UA_ASCENT=21) ────
#define ROW_Y_START  39
#define ROW_HEIGHT   44

// ─── Поточний стан навігації ─────────────────────────────────────────────────
struct MenuLevel {
  const MenuItem* items;
  uint8_t         count;
  uint8_t         savedSelected;
  uint8_t         savedPage;
  const char*     savedTitle;
};
static MenuLevel       navStack[2];            // підтримка 1 рівня підменю
static uint8_t         navDepth      = 0;
static const MenuItem* currentItems  = rootItems;
static uint8_t         currentCount  = ROOT_COUNT;
static const char*     currentTitle  = "НАЛАШТУВАННЯ";

static int32_t values[TOTAL_VALUES]; // runtime-значення; valIdx є індексом
static uint8_t selectedItem = 0;
static uint8_t currentPage  = 0;
static bool    editMode     = false;
static uint8_t editField    = 0;     // TIME: 0=Г 1=Х 2=С; RANGE: 0=lo 1=hi
static int     lastEncoderPos;
static bool    lastSW;

// ─── EEPROM-помічники ─────────────────────────────────────────────────────────
static uint8_t fieldsForType(MenuItemType t) {
  switch (t) {
    case ITEM_TIME:  return 3;
    case ITEM_RANGE: return 2;
    default:         return 1;
  }
}

static void saveValue(const MenuItem* item) {
  if (item->valIdx == NO_VAL) return;
  switch (item->type) {
    case ITEM_SLIDER:
    case ITEM_CHECKBOX:
    case ITEM_SELECT: {
      int16_t v = (int16_t)values[item->valIdx];
      EEPROM.put(item->eepromAddr, v);
      break;
    }
    case ITEM_TIME: {
      uint32_t t = (uint32_t)values[item->valIdx];
      EEPROM.put(item->eepromAddr, t);
      break;
    }
    case ITEM_RANGE: {
      int16_t lo = (int16_t)(values[item->valIdx] & 0xFFFF);
      int16_t hi = (int16_t)((uint32_t)values[item->valIdx] >> 16);
      EEPROM.put(item->eepromAddr,     lo);
      EEPROM.put(item->eepromAddr + 2, hi);
      break;
    }
    default: break;
  }
}

static void loadValue(const MenuItem* item) {
  if (item->valIdx == NO_VAL) return;
  switch (item->type) {
    case ITEM_SLIDER:
    case ITEM_CHECKBOX:
    case ITEM_SELECT: {
      int16_t v;
      EEPROM.get(item->eepromAddr, v);
      values[item->valIdx] = constrain(v, item->minVal, item->maxVal);
      break;
    }
    case ITEM_TIME: {
      uint32_t t;
      EEPROM.get(item->eepromAddr, t);
      if (t > 86399) t = 0;
      values[item->valIdx] = (int32_t)t;
      break;
    }
    case ITEM_RANGE: {
      int16_t lo, hi;
      EEPROM.get(item->eepromAddr,     lo);
      EEPROM.get(item->eepromAddr + 2, hi);
      lo = constrain(lo, item->minVal, item->maxVal);
      hi = constrain(hi, lo, item->maxVal);
      values[item->valIdx] = (int32_t)((uint16_t)lo | ((uint32_t)(uint16_t)hi << 16));
      break;
    }
    default: break;
  }
}

static void initDefaults() {
  for (uint8_t i = 0; i < ROOT_COUNT; i++)
    if (rootItems[i].valIdx != NO_VAL)
      values[rootItems[i].valIdx] = rootItems[i].defaultVal;
  for (uint8_t s = 0; s < NUM_SUBMENUS; s++)
    for (uint8_t i = 0; i < allSubMenus[s]->count; i++)
      if (allSubMenus[s]->items[i].valIdx != NO_VAL)
        values[allSubMenus[s]->items[i].valIdx] = allSubMenus[s]->items[i].defaultVal;
}

static void saveAll() {
  for (uint8_t i = 0; i < ROOT_COUNT; i++)
    saveValue(&rootItems[i]);
  for (uint8_t s = 0; s < NUM_SUBMENUS; s++)
    for (uint8_t i = 0; i < allSubMenus[s]->count; i++)
      saveValue(&allSubMenus[s]->items[i]);
}

static void loadAll() {
  initDefaults();
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL) {
    saveAll();
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
    return;
  }
  for (uint8_t i = 0; i < ROOT_COUNT; i++)
    loadValue(&rootItems[i]);
  for (uint8_t s = 0; s < NUM_SUBMENUS; s++)
    for (uint8_t i = 0; i < allSubMenus[s]->count; i++)
      loadValue(&allSubMenus[s]->items[i]);
}

// ─── Відображення ─────────────────────────────────────────────────────────────
static void drawPageIndicator() {
  uint8_t totalPages = (currentCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
  tft.fillRect(220, 0, 100, 37, ST77XX_BLACK);
  int cx = 310 - (totalPages - 1) * 18;
  for (uint8_t p = 0; p < totalPages; p++, cx += 18) {
    if (p == currentPage)
      tft.fillCircle(cx, 18, 5, ST77XX_CYAN);
    else
      tft.drawCircle(cx, 18, 5, tft.color565(80, 80, 80));
  }
}

static uint8_t countCP(const char* s) {
  uint8_t n = 0;
  while (*s) {
    if (((uint8_t)*s & 0xC0) != 0x80) n++;
    s++;
  }
  return n;
}

static void printSpaces(uint8_t n, uint16_t clr, uint16_t bg) {
  char sp[13];
  if (n > 12) n = 12;
  memset(sp, ' ', n);
  sp[n] = '\0';
  printUA(sp, clr, bg);
}

// Відображає поле значення (12 CP) з поточної позиції курсора
static void drawValueArea(const MenuItem* item, uint16_t rowBg) {
  bool     isEdit  = editMode && (&currentItems[selectedItem] == item);
  uint16_t normClr = ST77XX_WHITE;
  uint16_t editClr = ST77XX_YELLOW;
  uint16_t valClr  = isEdit ? editClr : normClr;

  switch (item->type) {

    case ITEM_SUBMENU:
      // "           >" — стрілка вправо вказує на підменю
      printUA("           >", normClr, rowBg);
      break;

    case ITEM_BACK:
      printSpaces(12, normClr, rowBg);
      break;

    case ITEM_SLIDER: {
      char numStr[8];
      snprintf(numStr, sizeof(numStr), "%d", (int)(int16_t)values[item->valIdx]);
      uint8_t numCP  = (uint8_t)strlen(numStr);
      uint8_t unitCP = item->units ? countCP(item->units) : 0;
      uint8_t total  = numCP + (unitCP > 0 ? 1 + unitCP : 0);
      uint8_t pad    = (total < 12) ? 12 - total : 0;
      printSpaces(pad, normClr, rowBg);
      printUA(numStr, valClr, rowBg);
      if (unitCP > 0) {
        printUA(" ", normClr, rowBg);
        printUA(item->units, normClr, rowBg);
      }
      break;
    }

    case ITEM_CHECKBOX:
      // 9 пробілів + "ВКЛ"(3CP) або 8 пробілів + "ВИКЛ"(4CP) = 12 CP
      printUA(values[item->valIdx]
                ? "         \xD0\x92\xD0\x9A\xD0\x9B"
                : "        \xD0\x92\xD0\x98\xD0\x9A\xD0\x9B",
              valClr, rowBg);
      break;

    case ITEM_SELECT: {
      const char** opts = (const char**)item->data;
      const char*  opt  = opts[(int)values[item->valIdx]];
      uint8_t optCP = countCP(opt);
      uint8_t pad   = (optCP < 12) ? 12 - optCP : 0;
      printSpaces(pad, normClr, rowBg);
      printUA(opt, valClr, rowBg);
      break;
    }

    case ITEM_TIME: {
      uint32_t t = (uint32_t)values[item->valIdx];
      uint8_t  h = t / 3600, m = (t % 3600) / 60, s = t % 60;
      char hStr[3], mStr[3], sStr[3];
      snprintf(hStr, 3, "%02d", h);
      snprintf(mStr, 3, "%02d", m);
      snprintf(sStr, 3, "%02d", s);
      // "   HH:MM:SS " = 3+2+1+2+1+2+1 = 12 CP
      printUA("   ", normClr, rowBg);
      printUA(hStr, (isEdit && editField == 0) ? editClr : normClr, rowBg);
      printUA(":", normClr, rowBg);
      printUA(mStr, (isEdit && editField == 1) ? editClr : normClr, rowBg);
      printUA(":", normClr, rowBg);
      printUA(sStr, (isEdit && editField == 2) ? editClr : normClr, rowBg);
      printUA(" ", normClr, rowBg);
      break;
    }

    case ITEM_RANGE: {
      int16_t lo = (int16_t)(values[item->valIdx] & 0xFFFF);
      int16_t hi = (int16_t)((uint32_t)values[item->valIdx] >> 16);
      char loStr[5], hiStr[5];
      snprintf(loStr, 5, "%d", (int)lo);
      snprintf(hiStr, 5, "%d", (int)hi);
      uint8_t loCP   = (uint8_t)strlen(loStr);
      uint8_t hiCP   = (uint8_t)strlen(hiStr);
      uint8_t unitCP = item->units ? countCP(item->units) : 0;
      uint8_t content = loCP + 2 + hiCP + (unitCP > 0 ? 1 + unitCP : 0);
      uint8_t pad     = (content < 12) ? 12 - content : 0;
      printSpaces(pad, normClr, rowBg);
      printUA(loStr, (isEdit && editField == 0) ? editClr : normClr, rowBg);
      printUA("..", normClr, rowBg);
      printUA(hiStr, (isEdit && editField == 1) ? editClr : normClr, rowBg);
      if (unitCP > 0) {
        printUA(" ", normClr, rowBg);
        printUA(item->units, normClr, rowBg);
      }
      break;
    }
  }
}

static void drawRow(uint8_t row) {
  uint8_t idx = currentPage * ITEMS_PER_PAGE + row;
  int     y   = ROW_Y_START + row * ROW_HEIGHT;

  if (idx >= currentCount) {
    tft.fillRect(0, y, 320, ROW_HEIGHT - 2, ST77XX_BLACK);
    return;
  }

  const MenuItem* item  = &currentItems[idx];
  bool            sel   = (idx == selectedItem);
  uint16_t        rowBg = sel ? tft.color565(0, 0, 80) : ST77XX_BLACK;

  // ITEM_SUBMENU і ITEM_BACK: ціанова мітка для навігаційних пунктів
  uint16_t labelClr = (item->type == ITEM_SUBMENU || item->type == ITEM_BACK)
                        ? ST77XX_CYAN : ST77XX_WHITE;

  tft.fillRect(  0, y,      320,  8, rowBg);
  tft.fillRect(  0, y + 34, 320,  8, rowBg);
  tft.fillRect(  0, y +  8,   4, 26, rowBg);
  tft.fillRect(312, y +  8,   8, 26, rowBg);

  tft.setCursor(4, y + 29);
  printUA(item->label, labelClr, rowBg);
  drawValueArea(item, rowBg);
}

static void drawFullMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(4, 30);
  printUA(currentTitle, ST77XX_CYAN, ST77XX_BLACK);
  tft.drawFastHLine(0, 37, 320, ST77XX_CYAN);
  drawPageIndicator();
  for (uint8_t r = 0; r < ITEMS_PER_PAGE; r++) drawRow(r);
}

// ─── Навігація по рівнях ──────────────────────────────────────────────────────
static void enterSubMenu() {
  if (navDepth >= 2) return;
  const SubMenuDef* sub = (const SubMenuDef*)currentItems[selectedItem].data;
  navStack[navDepth] = { currentItems, currentCount, selectedItem, currentPage, currentTitle };
  navDepth++;
  currentItems = sub->items;
  currentCount = sub->count;
  currentTitle = sub->title;
  // Починаємо з першого не-BACK пункту (якщо 0-й є BACK, то з 1-го)
  selectedItem = (currentCount > 0 && currentItems[0].type == ITEM_BACK) ? 1 : 0;
  if (selectedItem >= currentCount) selectedItem = 0;
  currentPage  = selectedItem / ITEMS_PER_PAGE;
  editMode     = false;
  editField    = 0;
  drawFullMenu();
}

static void exitSubMenu() {
  if (navDepth == 0) return;
  navDepth--;
  currentItems = navStack[navDepth].items;
  currentCount = navStack[navDepth].count;
  selectedItem = navStack[navDepth].savedSelected;
  currentPage  = navStack[navDepth].savedPage;
  currentTitle = navStack[navDepth].savedTitle;
  editMode     = false;
  editField    = 0;
  drawFullMenu();
}

// ─────────────────────────────────────────────────────────────────────────────
void settingsInit() {
  loadAll();
  navDepth     = 0;
  currentItems = rootItems;
  currentCount = ROOT_COUNT;
  currentTitle = "НАЛАШТУВАННЯ";
  editMode     = false;
  editField    = 0;
  selectedItem = 0;
  currentPage  = 0;
  lastEncoderPos = encoderGetPos();
  lastSW         = digitalRead(ENCODER_SW);
  drawFullMenu();
}

void settingsUpdate() {
  // ── Енкодер ──────────────────────────────────────────────────────────────
  int pos   = encoderGetPos();
  int delta = pos - lastEncoderPos;

  if (delta != 0) {
    lastEncoderPos = pos;

    if (!editMode) {
      int newSel = constrain((int)selectedItem + (delta > 0 ? 1 : -1),
                             0, (int)currentCount - 1);
      uint8_t oldPage = currentPage;
      uint8_t oldRow  = selectedItem % ITEMS_PER_PAGE;

      selectedItem = (uint8_t)newSel;
      currentPage  = selectedItem / ITEMS_PER_PAGE;

      if (currentPage != oldPage) {
        drawFullMenu();
      } else {
        drawRow(oldRow);
        drawRow(selectedItem % ITEMS_PER_PAGE);
      }
    } else {
      const MenuItem* item = &currentItems[selectedItem];
      switch (item->type) {

        case ITEM_SLIDER: {
          int32_t newV = values[item->valIdx] + delta;
          values[item->valIdx] = constrain(newV,
                                           (int32_t)item->minVal,
                                           (int32_t)item->maxVal);
          break;
        }

        case ITEM_CHECKBOX:
          values[item->valIdx] ^= 1;
          break;

        case ITEM_SELECT: {
          int32_t count = (int32_t)item->maxVal + 1;
          values[item->valIdx] = ((values[item->valIdx] + delta) % count + count) % count;
          break;
        }

        case ITEM_TIME: {
          uint32_t t = (uint32_t)values[item->valIdx];
          uint8_t h = t / 3600, m = (t % 3600) / 60, s = t % 60;
          if (editField == 0) h = (uint8_t)constrain((int)h + delta, 0, 23);
          if (editField == 1) m = (uint8_t)constrain((int)m + delta, 0, 59);
          if (editField == 2) s = (uint8_t)constrain((int)s + delta, 0, 59);
          values[item->valIdx] = (int32_t)((uint32_t)h * 3600u + m * 60u + s);
          break;
        }

        case ITEM_RANGE: {
          int16_t lo = (int16_t)(values[item->valIdx] & 0xFFFF);
          int16_t hi = (int16_t)((uint32_t)values[item->valIdx] >> 16);
          if (editField == 0)
            lo = (int16_t)constrain((int)lo + delta, (int)item->minVal, (int)hi);
          else
            hi = (int16_t)constrain((int)hi + delta, (int)lo, (int)item->maxVal);
          values[item->valIdx] = (int32_t)((uint16_t)lo | ((uint32_t)(uint16_t)hi << 16));
          break;
        }

        default: break;
      }
      drawRow(selectedItem % ITEMS_PER_PAGE);
    }
  }

  // ── Кнопка енкодера ──────────────────────────────────────────────────────
  bool sw = digitalRead(ENCODER_SW);
  if (sw != lastSW) {
    lastSW = sw;
    if (sw == LOW) {
      const MenuItem* item = &currentItems[selectedItem];

      if (!editMode) {
        if (item->type == ITEM_SUBMENU) {
          enterSubMenu();   // drawFullMenu() всередині
          return;
        }
        if (item->type == ITEM_BACK) {
          exitSubMenu();    // drawFullMenu() всередині
          return;
        }
        // Звичайний елемент → входимо в режим редагування
        editMode  = true;
        editField = 0;
        drawRow(selectedItem % ITEMS_PER_PAGE);
      } else {
        uint8_t maxF = fieldsForType(item->type);
        if (editField < maxF - 1) {
          editField++;
        } else {
          saveValue(item);
          editMode  = false;
          editField = 0;
        }
        drawRow(selectedItem % ITEMS_PER_PAGE);
      }
    }
  }
}
