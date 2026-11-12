#include "FmMenu.h"
#include "includes.h"

// ============================================================================ common

#define FM_LOAD_TIMEOUT_MS 3000
#define FM_ALARM_TOGGLE_MS 2000

typedef enum
{
  FM_ACT_NONE = 0,
  FM_ACT_START,
  FM_ACT_PAUSE,
  FM_ACT_RESUME,
  FM_ACT_SPOOL_CHANGED,
  FM_ACT_CLEAR,
  FM_ACT_HOME,
  FM_ACT_CHANGE,
  FM_ACT_STOP,
  FM_ACT_START_NOWIND,
  FM_ACT_PAUSE_NOW,
  FM_ACT_RESET_COUNTERS,
  FM_ACT_MOTORS_OFF,
  FM_ACT_COUNT
} FM_ACTION;

typedef struct
{
  uint16_t     icon;
  const char * label;
  const char * cmd;      // FM_<cmd>
  const char * confirm;  // question before sending, NULL = send at once
} FM_ACTION_DEF;

static const FM_ACTION_DEF fmActions[FM_ACT_COUNT] = {
  [FM_ACT_NONE]           = {ICON_NULL,             NULL,              NULL,             NULL},
  [FM_ACT_START]          = {ICON_RESUME,           "Start",           "START",          NULL},
  [FM_ACT_PAUSE]          = {ICON_PAUSE,            "Pause",           "PAUSE",          NULL},
  [FM_ACT_RESUME]         = {ICON_RESUME,           "Resume",          "RESUME",         NULL},
  [FM_ACT_SPOOL_CHANGED]  = {ICON_LOAD,             "Spool changed",   "SPOOL_CHANGED",  NULL},
  [FM_ACT_CLEAR]          = {ICON_APPLY,            "Clear error",     "CLEAR",          NULL},
  [FM_ACT_HOME]           = {ICON_HOME,             "Home winder",     "HOME",           NULL},
  [FM_ACT_CHANGE]         = {ICON_UNLOAD,           "Change spool",    "CHANGE_SPOOL",   "Empty the buffer and stop for a spool change?"},
  [FM_ACT_STOP]           = {ICON_STOP,             "Stop",            "STOP",           "Stop spooling now?"},
  [FM_ACT_START_NOWIND]   = {ICON_EXTRUDE,          "No winding",      "START_NOWIND",   "Start spooling without the winder traverse?"},
  [FM_ACT_PAUSE_NOW]      = {ICON_PAUSE,            "Pause now",       "PAUSE_HARD",     "Stop the spool immediately (buffer is not emptied)?"},
  [FM_ACT_RESET_COUNTERS] = {ICON_RESET_VALUE,      "Reset counters",  "RESET_COUNTERS", "Reset length, revolutions and layers to zero?"},
  [FM_ACT_MOTORS_OFF]     = {ICON_DISABLE_STEPPERS, "Motors off",      "MOTORS_OFF",     "Switch the motors off? The winder must be homed again."},
};

static FM_ACTION fmPendingAction = FM_ACT_NONE;

static void fmSendPending(void)
{
  if (fmPendingAction != FM_ACT_NONE && fmActions[fmPendingAction].cmd != NULL)
    fmCommand(fmActions[fmPendingAction].cmd);

  fmPendingAction = FM_ACT_NONE;
}

static void fmDialog(DIALOG_TYPE type, const char * title, const char * msg, bool question, void (* ok_action)(void))
{
  _setDialogTitleStr((uint8_t *)title);
  _setDialogMsgStr((uint8_t *)msg);
  _setDialogOkTextLabel(question ? LABEL_CONFIRM : LABEL_CONFIRM);
  _setDialogCancelTextLabel(question ? LABEL_CANCEL : LABEL_NULL);
  showDialog(type, ok_action, NULL, NULL);
}

// run an action, asking first if it has a confirmation text
static void fmRunAction(FM_ACTION action, bool forceConfirm)
{
  const FM_ACTION_DEF * def = &fmActions[action];

  if (action == FM_ACT_NONE || def->cmd == NULL)
    return;

  fmPendingAction = action;

  if (def->confirm != NULL && forceConfirm)
    fmDialog(DIALOG_TYPE_QUESTION, def->label, def->confirm, true, fmSendPending);
  else
    fmSendPending();
}

static void fmSetItem(ITEM * item, FM_ACTION action)
{
  item->icon = fmActions[action].icon;

  if (fmActions[action].label != NULL)
    item->label.address = (void *)fmActions[action].label;
  else
    item->label.index = LABEL_NULL;
}

static bool fmIsRunning(uint8_t state)
{
  return state == FM_SPOOLING || state == FM_SPOOLING_WINDING || state == FM_CHANGE_EMPTYING ||
         state == FM_CHANGE_REBALANCE || state == FM_HOMING;
}

static bool fmOnline(void)
{
  return infoHost.connected && fmStatus.valid;
}

// ============================================================================ status screen

static const char * fmGroupName = "Process";
static bool fmGroupNeedLoad = true;

static void fmOpenGroup(const char * group)
{
  fmGroupName = group;
  fmGroupNeedLoad = true;
  OPEN_MENU(menuFmGroup);
}

// counters strip between the two icon rows (same place as the stock XYZ strip)
static const GUI_RECT fmStripRect = {START_X,                                ICON_HEIGHT + ICON_START_Y + STATUS_GANTRY_YOFFSET,
                                     START_X + 4 * ICON_WIDTH + 3 * SPACE_X, ICON_HEIGHT + SPACE_Y + ICON_START_Y - STATUS_GANTRY_YOFFSET};

static char fmTitle[48];
static char fmTileText[4][16];
static int8_t fmBarShown = -2;
static bool fmShowAlarms = false;

static void fmBuildTitle(char * buf, uint8_t size)
{
  if (!infoHost.connected)
    snprintf(buf, size, "FilamentMaker - offline");
  else if (!fmStatus.valid)
    snprintf(buf, size, "FilamentMaker - waiting");
  else
    snprintf(buf, size, "%s%s", fmStateText(fmStatus.state), fmStatus.simulated ? "  [SIM]" : "");
}

static void fmActionsFor(FM_ACTION * a4, FM_ACTION * a5)
{
  *a4 = FM_ACT_NONE;
  *a5 = FM_ACT_NONE;

  if (!fmOnline())
    return;

  switch (fmStatus.state)
  {
    case FM_IDLE_UNHOMED:
    case FM_IDLE_HOMED:
      *a4 = FM_ACT_START;
      *a5 = FM_ACT_HOME;
      break;

    case FM_SPOOLING:
    case FM_SPOOLING_WINDING:
      *a4 = FM_ACT_PAUSE;
      *a5 = FM_ACT_CHANGE;
      break;

    case FM_PAUSED:
      *a4 = FM_ACT_RESUME;
      *a5 = FM_ACT_CHANGE;
      break;

    case FM_CHANGE_EMPTYING:
      *a4 = FM_ACT_PAUSE_NOW;
      break;

    case FM_CHANGE_PAUSE:
      *a4 = FM_ACT_SPOOL_CHANGED;
      break;

    case FM_CHANGE_REBALANCE:
      *a4 = FM_ACT_PAUSE;
      break;

    case FM_ERROR:
      *a4 = FM_ACT_CLEAR;
      break;

    default:
      break;
  }
}

static void fmTempText(char * buf, uint8_t size, int16_t deci)
{
  if (!fmOnline() || deci == FM_TEMP_INVALID)
  {
    snprintf(buf, size, "--");
    return;
  }

  int16_t a = deci < 0 ? -deci : deci;

  snprintf(buf, size, "%s%d.%d℃", deci < 0 ? "-" : "", a / 10, a % 10);
}

// one live tile: icon, title (top right) and value (center); the icon is only redrawn when the text width changes
static void fmDrawTile(uint8_t index, uint16_t icon, const char * title, const char * value, bool force)
{
  if (!force && strcmp(fmTileText[index], value) == 0)
    return;

  bool redraw = force || strlen(fmTileText[index]) != strlen(value);
  LIVE_INFO lv;

  strncpy_no_pad(fmTileText[index], value, sizeof(fmTileText[index]));

  lv.iconIndex = icon;
  lv.enabled[0] = true;
  lv.lines[0].h_align = RIGHT;
  lv.lines[0].v_align = TOP;
  lv.lines[0].pos = (GUI_POINT){SS_ICON_WIDTH - BYTE_WIDTH / 2, SS_ICON_NAME_Y0};
  lv.lines[0].font = SS_ICON_TITLE_FONT_SIZE;
  lv.lines[0].fn_color = SS_NAME_COLOR;
  lv.lines[0].text_mode = GUI_TEXTMODE_TRANS;
  lv.lines[0].text = (uint8_t *)title;

  lv.enabled[1] = true;
  lv.lines[1].h_align = CENTER;
  lv.lines[1].v_align = CENTER;
  lv.lines[1].pos = (GUI_POINT){SS_ICON_WIDTH / 2, SS_ICON_VAL_Y0};
  lv.lines[1].font = SS_ICON_VAL_FONT_SIZE;
  lv.lines[1].fn_color = SS_VAL_COLOR;
  lv.lines[1].text_mode = GUI_TEXTMODE_TRANS;
  lv.lines[1].text = (uint8_t *)fmTileText[index];

  lv.enabled[2] = false;

  showLiveInfo(index, &lv, redraw);

  if (redraw && index == 3)
    fmBarShown = -2;  // icon repainted -> bar must be drawn again
}

// buffer position as 5 segments inside the buffer tile
static void fmDrawBufferBar(void)
{
  int8_t pos = fmOnline() ? fmStatus.buffer : -1;

  if (pos == fmBarShown)
    return;

  fmBarShown = pos;

  const GUI_RECT * r = &rect_of_key[3];
  const int16_t segW = 14, gap = 2;
  const int16_t total = 5 * segW + 4 * gap;
  const int16_t x0 = r->x0 + ((r->x1 - r->x0) - total) / 2;
  const int16_t y0 = r->y0 + 32, y1 = r->y0 + 44;

  for (int8_t i = 0; i < 5; i++)
  {
    uint16_t color = DARKGRAY;

    if (pos >= 0 && i <= pos)
      color = (pos == 0 || pos == 4) ? MAT_ORANGE : MAT_GREEN;

    int16_t x = x0 + i * (segW + gap);

    GUI_FillRectColor(x, y0, x + segW, y1, color);
  }
}

static void fmDrawTiles(bool force)
{
  char v[16];

  fmTempText(v, sizeof(v), fmStatus.temp1);
  fmDrawTile(0, ICON_STATUS_NOZZLE, "T1", v, force);

  fmTempText(v, sizeof(v), fmStatus.temp2);
  fmDrawTile(1, ICON_STATUS_BED, "T2", v, force);

  if (fmOnline())
    snprintf(v, sizeof(v), "%ld", (long)fmStatus.sps);
  else
    snprintf(v, sizeof(v), "--");

  fmDrawTile(2, ICON_STATUS_SPEED, "Speed", v, force);

  if (fmOnline() && fmStatus.buffer >= 0)
    snprintf(v, sizeof(v), "%d/4", fmStatus.buffer);
  else
    snprintf(v, sizeof(v), "--");

  fmDrawTile(3, ICON_STATUS_FLOW, "Buffer", v, force);
  fmDrawBufferBar();
}

static void fmDrawStrip(void)
{
  char text[64];
  uint16_t bg = GANTRY_XYZ_BG_COLOR;

  if (!fmOnline())
  {
    snprintf(text, sizeof(text), "No data from the controller");
  }
  else if (fmShowAlarms && fmStatus.alarms != 0)
  {
    uint8_t n = 0;

    n += snprintf(text, sizeof(text), "ALARM");

    for (uint8_t i = 0; i < FM_ALARM_COUNT && n < sizeof(text) - 2; i++)
    {
      if (fmStatus.alarms & (1UL << i))
        n += snprintf(text + n, sizeof(text) - n, "%s %s", n > 5 ? "," : ":", fmAlarmName(i));
    }

    bg = MAT_RED;
  }
  else
  {
    int32_t revs = fmStatus.revs100 < 0 ? -fmStatus.revs100 : fmStatus.revs100;

    snprintf(text, sizeof(text), "%ld.%ld m   %ld.%02ld rev   %lu layers",
             (long)(fmStatus.lengthDm / 10), (long)(fmStatus.lengthDm % 10),
             (long)(revs / 100), (long)(revs % 100), (unsigned long)fmStatus.layers);
  }

  GUI_SetColor(bg);
  GUI_FillPrect(&fmStripRect);
  GUI_SetTextMode(GUI_TEXTMODE_TRANS);
  GUI_SetColor(GANTRY_XYZ_FONT_COLOR);
  GUI_DispStringInPrect(&fmStripRect, (uint8_t *)text);
  GUI_RestoreColorDefault();
}

void menuFmStatus(void)
{
  if (!fmIsActive())
  {
    REPLACE_MENU(menuStatus);
    return;
  }

  static MENUITEMS fmStatusItems = {
    // title
    LABEL_NULL,
    // icon                          label
    {
      {ICON_STATUS_NOZZLE,           LABEL_NULL},
      {ICON_STATUS_BED,              LABEL_NULL},
      {ICON_STATUS_SPEED,            LABEL_NULL},
      {ICON_STATUS_FLOW,             LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_STOP,                    LABEL_NULL},
      {ICON_MAINMENU,                LABEL_MAINMENU},
    }
  };

  KEY_VALUES key_num;
  FM_ACTION a4, a5;
  FM_ACTION shownA4, shownA5;
  uint32_t lastUpdate = 0;
  uint32_t alarmToggleMs = OS_GetTimeMs();
  bool lastOnline;
  char title[sizeof(fmTitle)];

  fmBuildTitle(fmTitle, sizeof(fmTitle));
  fmStatusItems.title.address = fmTitle;

  fmActionsFor(&a4, &a5);
  fmSetItem(&fmStatusItems.items[4], a4);
  fmSetItem(&fmStatusItems.items[5], a5);
  fmSetItem(&fmStatusItems.items[6], FM_ACT_STOP);
  shownA4 = a4;
  shownA5 = a5;
  lastOnline = fmOnline();

  GUI_SetBkColor(infoSettings.bg_color);
  menuDrawPage(&fmStatusItems);
  fmDrawTiles(true);
  fmDrawStrip();

  while (MENU_IS(menuFmStatus))
  {
    uint32_t now = OS_GetTimeMs();
    bool online = fmOnline();
    bool changed = (fmStatus.updatedMs != lastUpdate) || (online != lastOnline);

    if (fmStatus.alarms != 0 && now - alarmToggleMs >= FM_ALARM_TOGGLE_MS)
    {
      alarmToggleMs = now;
      fmShowAlarms = !fmShowAlarms;
      changed = true;
    }
    else if (fmStatus.alarms == 0 && fmShowAlarms)
    {
      fmShowAlarms = false;
      changed = true;
    }

    if (changed)
    {
      lastUpdate = fmStatus.updatedMs;
      lastOnline = online;

      fmBuildTitle(title, sizeof(title));

      if (strcmp(title, fmTitle) != 0)
      {
        strcpy(fmTitle, title);
        menuDrawTitle();
      }

      fmActionsFor(&a4, &a5);

      if (a4 != shownA4)
      {
        shownA4 = a4;
        fmSetItem(&fmStatusItems.items[4], a4);
        menuDrawItem(&fmStatusItems.items[4], 4);
      }

      if (a5 != shownA5)
      {
        shownA5 = a5;
        fmSetItem(&fmStatusItems.items[5], a5);
        menuDrawItem(&fmStatusItems.items[5], 5);
      }

      fmDrawTiles(false);
      fmDrawStrip();
    }

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
      case KEY_ICON_1:
        fmOpenGroup("Temperature");
        break;

      case KEY_ICON_2:
        fmOpenGroup("Process");
        break;

      case KEY_ICON_3:
        fmOpenGroup("Buffer");
        break;

      case KEY_ICON_4:
        fmRunAction(shownA4, true);
        break;

      case KEY_ICON_5:
        fmRunAction(shownA5, true);
        break;

      case KEY_ICON_6:
        if (online)
          fmRunAction(FM_ACT_STOP, fmIsRunning(fmStatus.state) || fmStatus.state == FM_PAUSED);
        break;

      case KEY_ICON_7:
        OPEN_MENU(menuFmMenu);
        break;

      default:
        break;
    }

    loopProcess();
  }
}

// ============================================================================ menus

void menuFmMenu(void)
{
  if (!fmIsActive())
  {
    REPLACE_MENU(menuMain);
    return;
  }

  static MENUITEMS fmMenuItems = {
    // title
    LABEL_MAINMENU,
    // icon                          label
    {
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_FAN,                     LABEL_FAN},
      {ICON_PARAMETER,               LABEL_NULL},
      {ICON_SCREEN_INFO,             LABEL_NULL},
      {ICON_MORE,                    LABEL_MORE},
      {ICON_BACK,                    LABEL_BACK},
    }
  };

  KEY_VALUES key_num;

  fmMenuItems.title.address = "FilamentMaker";
  fmSetItem(&fmMenuItems.items[0], FM_ACT_HOME);
  fmSetItem(&fmMenuItems.items[1], FM_ACT_START_NOWIND);
  fmSetItem(&fmMenuItems.items[2], FM_ACT_PAUSE_NOW);
  fmMenuItems.items[4].label.address = "Settings";
  fmMenuItems.items[5].label.address = "Info";

  menuDrawPage(&fmMenuItems);

  while (MENU_IS(menuFmMenu))
  {
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
        fmRunAction(FM_ACT_HOME, false);
        break;

      case KEY_ICON_1:
        fmRunAction(FM_ACT_START_NOWIND, true);
        break;

      case KEY_ICON_2:
        fmRunAction(FM_ACT_PAUSE_NOW, true);
        break;

      case KEY_ICON_3:
        OPEN_MENU(menuFan);
        break;

      case KEY_ICON_4:
        OPEN_MENU(menuFmSettings);
        break;

      case KEY_ICON_5:
        OPEN_MENU(menuFmInfo);
        break;

      case KEY_ICON_6:
        OPEN_MENU(menuFmMore);
        break;

      case KEY_ICON_7:
        CLOSE_MENU();
        break;

      default:
        break;
    }

    loopProcess();
  }
}

void menuFmMore(void)
{
  static MENUITEMS fmMoreItems = {
    // title
    LABEL_MORE,
    // icon                          label
    {
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_GCODE,                   LABEL_TERMINAL},
      {ICON_SETTINGS,                LABEL_NULL},
      {ICON_CUSTOM,                  LABEL_CUSTOM},
      {ICON_NULL,                    LABEL_NULL},
      {ICON_BACK,                    LABEL_BACK},
    }
  };

  KEY_VALUES key_num;

  fmSetItem(&fmMoreItems.items[0], FM_ACT_RESET_COUNTERS);
  fmSetItem(&fmMoreItems.items[1], FM_ACT_MOTORS_OFF);
  fmSetItem(&fmMoreItems.items[2], FM_ACT_CLEAR);
  fmMoreItems.items[4].label.address = "TFT settings";

  menuDrawPage(&fmMoreItems);

  while (MENU_IS(menuFmMore))
  {
    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
        fmRunAction(FM_ACT_RESET_COUNTERS, true);
        break;

      case KEY_ICON_1:
        fmRunAction(FM_ACT_MOTORS_OFF, true);
        break;

      case KEY_ICON_2:
        fmRunAction(FM_ACT_CLEAR, false);
        break;

      case KEY_ICON_3:
        OPEN_MENU(menuTerminal);
        break;

      case KEY_ICON_4:
        OPEN_MENU(menuSettings);
        break;

      case KEY_ICON_5:
        OPEN_MENU(menuCustom);
        break;

      case KEY_ICON_7:
        CLOSE_MENU();
        break;

      default:
        break;
    }

    loopProcess();
  }
}

// ============================================================================ settings

static const char * const fmGroups[] = {"Process", "Temperature", "Spool", "Winder", "Buffer", "System"};
#define FM_GROUP_COUNT COUNT(fmGroups)

static void fmLoadGroupItem(LISTITEM * item, uint16_t index, uint8_t itemPos)
{
  (void)itemPos;

  if (index >= FM_GROUP_COUNT)
  {
    item->icon = CHARICON_NULL;
    return;
  }

  item->icon = CHARICON_SETTING1;
  item->itemType = LIST_MOREBUTTON;
  item->titlelabel.address = (void *)fmGroups[index];
}

void menuFmSettings(void)
{
  static uint16_t page = 0;
  LABEL title = {.address = "Settings"};
  uint16_t curIndex;

  listViewCreate(title, NULL, FM_GROUP_COUNT, &page, false, NULL, fmLoadGroupItem);

  while (MENU_IS(menuFmSettings))
  {
    curIndex = listViewGetSelectedIndex();

    if (curIndex == KEY_BACK)
    {
      page = 0;
      CLOSE_MENU();
    }
    else if (curIndex < FM_GROUP_COUNT)
    {
      fmOpenGroup(fmGroups[curIndex]);
    }

    loopProcess();
  }
}

// waits for an answer while keeping the TFT alive; draws a simple "Loading" page
static bool fmWaitFor(bool (* ready)(void), const char * title, void (* menu)(void))
{
  uint32_t start = OS_GetTimeMs();

  GUI_Clear(infoSettings.bg_color);
  GUI_SetColor(infoSettings.font_color);
  GUI_DispStringCenter(LCD_WIDTH / 2, LCD_HEIGHT / 2 - BYTE_HEIGHT, (uint8_t *)title);
  GUI_DispStringCenter(LCD_WIDTH / 2, LCD_HEIGHT / 2 + 4, (uint8_t *)"Loading...");
  GUI_RestoreColorDefault();

  while (!ready() && OS_GetTimeMs() - start < FM_LOAD_TIMEOUT_MS && MENU_IS(menu))
    loopProcess();

  return ready();
}

static void fmLoadParamItem(LISTITEM * item, uint16_t index, uint8_t itemPos)
{
  FM_PARAM * p = fmParam(index);
  char v[16];

  if (p == NULL)
  {
    item->icon = CHARICON_NULL;
    return;
  }

  item->itemType = LIST_CUSTOMVALUE;
  item->titlelabel.address = p->label;
  item->valueLabel.index = LABEL_CUSTOM_VALUE;

  if (fmOptionCount(p->options) > 0)
  {
    item->icon = CHARICON_UP_DOWN;

    if (!fmOption(p->options, p->value - p->min, v, sizeof(v)))
      fmFormatRaw(v, sizeof(v), p->value, p->decimals);
  }
  else
  {
    item->icon = CHARICON_EDIT;
    fmFormatRaw(v, sizeof(v), p->value, p->decimals);
  }

  v[8] = '\0';  // list value buffer holds 9 characters
  setDynamicTextValue(itemPos, v);
}

static void fmEditParam(uint8_t index)
{
  FM_PARAM * p = fmParam(index);

  if (p == NULL)
    return;

  uint8_t options = fmOptionCount(p->options);

  if (options > 0)  // enumeration: step to the next option
  {
    int32_t next = p->value + 1;

    if (next > p->max || next - p->min >= options)
      next = p->min;

    fmSetParam(index, next);
    return;
  }

  char title[48];
  int32_t raw;

  if (p->unit[0] != '\0')
    snprintf(title, sizeof(title), "%s (%s)", p->label, p->unit);
  else
    snprintf(title, sizeof(title), "%s", p->label);

  if (p->decimals == 0)
  {
    raw = numPadInt((uint8_t *)title, p->value, p->def, p->min < 0);
  }
  else
  {
    double scale = 1;

    for (uint8_t i = 0; i < p->decimals; i++)
      scale *= 10;

    double v = numPadFloat((uint8_t *)title, p->value / scale, p->def / scale, p->min < 0);

    raw = (int32_t)(v * scale + (v >= 0 ? 0.5 : -0.5));
  }

  if (raw == p->value)
    return;

  if (raw < p->min || raw > p->max)
  {
    static char msg[64];
    char lo[16], hi[16];

    fmFormatRaw(lo, sizeof(lo), p->min, p->decimals);
    fmFormatRaw(hi, sizeof(hi), p->max, p->decimals);
    snprintf(msg, sizeof(msg), "Allowed range: %s .. %s %s", lo, hi, p->unit);
    fmDialog(DIALOG_TYPE_ERROR, p->label, msg, false, NULL);
    return;
  }

  fmSetParam(index, raw);
}

void menuFmGroup(void)
{
  static uint16_t page = 0;
  LABEL title = {.address = (void *)fmGroupName};
  uint16_t curIndex;

  if (fmGroupNeedLoad)
  {
    fmGroupNeedLoad = false;
    page = 0;
    fmRequestGroup(fmGroupName);

    if (!fmWaitFor(fmGroupReady, fmGroupName, menuFmGroup))
    {
      if (MENU_IS(menuFmGroup))
      {
        CLOSE_MENU();
        addToast(DIALOG_TYPE_ERROR, "No answer from the controller");
      }

      return;
    }
  }

  uint32_t serial = fmParamSerial();

  listViewCreate(title, NULL, fmParamCount(), &page, false, NULL, fmLoadParamItem);

  while (MENU_IS(menuFmGroup))
  {
    curIndex = listViewGetSelectedIndex();

    if (curIndex == KEY_BACK)
    {
      CLOSE_MENU();
    }
    else if (curIndex < fmParamCount())
    {
      fmEditParam(curIndex);

      if (MENU_IS(menuFmGroup))  // not left for an error dialog
        listViewRefreshMenu();
    }

    if (serial != fmParamSerial())  // FM_V answer arrived
    {
      serial = fmParamSerial();
      listViewRefreshPage();
    }

    loopProcess();
  }
}

// ============================================================================ info

static char fmInfoText[FM_MAX_INFO][FM_INFO_KEY_LEN + FM_INFO_VAL_LEN + 4];

static void fmLoadInfoItem(LISTITEM * item, uint16_t index, uint8_t itemPos)
{
  (void)itemPos;

  const FM_INFO_LINE * line = fmInfo(index);

  if (line == NULL)
  {
    item->icon = CHARICON_NULL;
    return;
  }

  snprintf(fmInfoText[index], sizeof(fmInfoText[index]), "%s: %s", line->key, line->value);
  item->icon = CHARICON_DETAIL;
  item->itemType = LIST_LABEL;
  item->titlelabel.address = fmInfoText[index];
}

void menuFmInfo(void)
{
  static uint16_t page = 0;
  LABEL title = {.address = "Info"};
  uint16_t curIndex;

  fmRequestInfo();

  if (!fmWaitFor(fmInfoReady, "Info", menuFmInfo))
  {
    if (MENU_IS(menuFmInfo))
    {
      CLOSE_MENU();
      addToast(DIALOG_TYPE_ERROR, "No answer from the controller");
    }

    return;
  }

  listViewCreate(title, NULL, fmInfoCount(), &page, false, NULL, fmLoadInfoItem);

  while (MENU_IS(menuFmInfo))
  {
    curIndex = listViewGetSelectedIndex();

    if (curIndex == KEY_BACK)
    {
      page = 0;
      CLOSE_MENU();
    }
    else if (curIndex < fmInfoCount())  // tap = refresh
    {
      fmRequestInfo();

      if (fmWaitFor(fmInfoReady, "Info", menuFmInfo))
        listViewCreate(title, NULL, fmInfoCount(), &page, false, NULL, fmLoadInfoItem);
    }

    loopProcess();
  }
}
