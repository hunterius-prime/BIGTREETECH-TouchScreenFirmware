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
  const char * label;    // icon label: max 9 characters (wider text is cut)
  const char * title;    // dialog title (full name)
  const char * cmd;      // FM_<cmd>
  const char * confirm;  // question before sending, NULL = send at once
} FM_ACTION_DEF;

static const FM_ACTION_DEF fmActions[FM_ACT_COUNT] = {
  [FM_ACT_NONE]           = {ICON_NULL,             NULL,         NULL,              NULL,             NULL},
  [FM_ACT_START]          = {ICON_RESUME,           "Start",      "Start",           "START",          NULL},
  [FM_ACT_PAUSE]          = {ICON_PAUSE,            "Pause",      "Pause",           "PAUSE",          NULL},
  [FM_ACT_RESUME]         = {ICON_RESUME,           "Resume",     "Resume",          "RESUME",         NULL},
  [FM_ACT_SPOOL_CHANGED]  = {ICON_LOAD,             "Continue",   "Spool changed",   "SPOOL_CHANGED",  NULL},
  [FM_ACT_CLEAR]          = {ICON_APPLY,            "Clear err",  "Clear error",     "CLEAR",          NULL},
  [FM_ACT_HOME]           = {ICON_HOME,             "Home",       "Home winder",     "HOME",           NULL},
  [FM_ACT_CHANGE]         = {ICON_UNLOAD,           "New spool",  "Change spool",    "CHANGE_SPOOL",   "Empty the buffer and stop for a spool change?"},
  [FM_ACT_STOP]           = {ICON_STOP,             "Stop",       "Stop",            "STOP",           "Stop spooling now?"},
  [FM_ACT_START_NOWIND]   = {ICON_EXTRUDE,          "No wind",    "Start, no winding", "START_NOWIND", "Start spooling without the winder traverse?"},
  [FM_ACT_PAUSE_NOW]      = {ICON_PAUSE,            "Pause now",  "Pause now",       "PAUSE_HARD",     "Stop the spool immediately (buffer is not emptied)?"},
  [FM_ACT_RESET_COUNTERS] = {ICON_RESET_VALUE,      "Reset cnt",  "Reset counters",  "RESET_COUNTERS", "Reset length, revolutions and layers to zero?"},
  [FM_ACT_MOTORS_OFF]     = {ICON_DISABLE_STEPPERS, "Motors off", "Motors off",      "MOTORS_OFF",     "Switch the motors off? The winder must be homed again."},
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
  _setDialogOkTextLabel(LABEL_CONFIRM);
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
    fmDialog(DIALOG_TYPE_QUESTION, def->title, def->confirm, true, fmSendPending);
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
  if (value == NULL)  // tile without a value line (buffer: the bar is drawn there)
    value = "";

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

  lv.enabled[1] = (value[0] != '\0');
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

  // in the value row (below the icon graphic, above the colored underline)
  const GUI_RECT * r = &rect_of_key[3];
  const int16_t segW = 14, gap = 3;
  const int16_t total = 5 * segW + 4 * gap;
  const int16_t x0 = r->x0 + ((r->x1 - r->x0) - total) / 2;
  const int16_t y0 = r->y0 + SS_ICON_VAL_Y0 - 7, y1 = r->y0 + SS_ICON_VAL_Y0 + 7;

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

  // titles: max 3 characters, the icon graphic fills the left part of the tile (like the stock "Sp." / "Fr.")
  fmDrawTile(2, ICON_STATUS_SPEED, "Sp.", v, force);
  fmDrawTile(3, ICON_STATUS_FLOW, "Buf", fmOnline() && fmStatus.buffer >= 0 ? NULL : "--", force);

  if (fmTileText[3][0] == '\0')
    fmDrawBufferBar();
  else
    fmBarShown = -2;
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
        fmOpenGroup("Standalone");  // speed per buffer position
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
  fmMoreItems.items[4].label.address = "TFT setup";

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

static const char * const fmGroups[] = {"Process", "Spool", "Temperature", "Motion", "Standalone"};  // ESP32 PARAM_GROUPS
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

static uint8_t fmEditIndex = 0;  // parameter opened in menuFmEditParam

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

  fmEditIndex = index;
  OPEN_MENU(menuFmEditParam);
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

// ============================================================================ value editor (knob)

// One numeric setting, edited with the knob, the - / + icons or the keypad (tap the value).
// Nothing is sent until Save (icon or a short press on the knob); Back leaves without sending.
// The value always stays inside the controller's min .. max.

#define FM_EDIT_FAST_MS    40    // detents closer than this: fast turn
#define FM_EDIT_MEDIUM_MS  90    // detents closer than this: medium turn
#define FM_EDIT_FAST_MUL   10
#define FM_EDIT_MEDIUM_MUL 3
#define FM_EDIT_MAX_STEPS  1000  // initial step: smallest power of ten giving at most this many steps over the range

// draws a text centered in a horizontal band of the value area, cut to the band width
static void fmEditBand(int16_t y0, int16_t y1, const char * text, uint16_t color, bool large)
{
  char buf[32];
  uint8_t maxChars = (exhibitRect.x1 - exhibitRect.x0) / (large ? LARGE_BYTE_WIDTH : BYTE_WIDTH);

  if (maxChars >= sizeof(buf))
    maxChars = sizeof(buf) - 1;

  snprintf(buf, maxChars + 1, "%s", text);

  GUI_ClearRect(exhibitRect.x0, y0, exhibitRect.x1, y1);
  GUI_SetColor(color);

  if (large)
    setFontSize(FONT_SIZE_LARGE);

  GUI_DispStringInRect(exhibitRect.x0, y0, exhibitRect.x1, y1, (uint8_t *)buf);

  setFontSize(FONT_SIZE_NORMAL);
  GUI_RestoreColorDefault();
}

// value area: "min..max unit" / new value (highlighted when changed) / "Now <controller value>"
static void fmEditDraw(const FM_PARAM * p, int32_t value, bool all)
{
  char a[16], b[16], line[48];
  int16_t valueY0 = exhibitRect.y0 + BYTE_HEIGHT;
  int16_t valueY1 = exhibitRect.y1 - BYTE_HEIGHT;

  if (all)
  {
    fmFormatRaw(a, sizeof(a), p->min, p->decimals);
    fmFormatRaw(b, sizeof(b), p->max, p->decimals);
    snprintf(line, sizeof(line), "%s..%s %s", a, b, p->unit);
    fmEditBand(exhibitRect.y0, valueY0, line, infoSettings.font_color, false);

    fmFormatRaw(a, sizeof(a), p->value, p->decimals);
    snprintf(line, sizeof(line), "Now %s", a);
    fmEditBand(valueY1, exhibitRect.y1, line, infoSettings.font_color, false);
  }

  fmFormatRaw(a, sizeof(a), value, p->decimals);
  fmEditBand(valueY0, valueY1, a, value != p->value ? infoSettings.reminder_color : infoSettings.font_color, true);
}

static int32_t fmEditClamp(const FM_PARAM * p, int64_t value)
{
  if (value < p->min)
    return p->min;

  if (value > p->max)
    return p->max;

  return (int32_t)value;
}

// step icon and label, e.g. "Step 0.01" (icon labels show 9 characters)
static void fmEditSetStepItem(ITEM * item, char * label, uint8_t size, const FM_PARAM * p, int32_t step, uint8_t tier)
{
  char v[16];

  fmFormatRaw(v, sizeof(v), step, p->decimals);

  if (strlen(v) <= 4)
    snprintf(label, size, "Step %s", v);
  else
    snprintf(label, size, "%s", v);

  item->icon = tier == 0 ? ICON_SLOW_SPEED : (tier == 1 ? ICON_NORMAL_SPEED : ICON_FAST_SPEED);
  item->label.address = label;
}

void menuFmEditParam(void)
{
  static char title[FM_LABEL_LEN];
  static char stepLabel[16];

  MENUITEMS editItems = {
    // title
    {.address = title},
    // icon                label
    {
      {ICON_DEC,           LABEL_DEC},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_INC,           LABEL_INC},
      {ICON_SLOW_SPEED,    LABEL_NULL},
      {ICON_RESET_VALUE,   LABEL_NULL},
      {ICON_APPLY,         LABEL_NULL},
      {ICON_BACK,          LABEL_BACK},
    }
  };

  FM_PARAM * p = fmParam(fmEditIndex);

  if (p == NULL || fmOptionCount(p->options) > 0)
  {
    CLOSE_MENU();
    return;
  }

  int64_t range = (int64_t)p->max - p->min;
  int32_t value = fmEditClamp(p, p->value);
  int32_t step = 1;
  uint8_t stepTier = 0;
  uint32_t serial = fmParamSerial();
  KEY_VALUES key_num;

  #if LCD_ENCODER_SUPPORT
    uint32_t lastTurnMs = 0;
  #endif

  while (range / step > FM_EDIT_MAX_STEPS)
  {
    step *= 10;
    stepTier++;
  }

  editItems.items[KEY_ICON_5].label.address = (void *)"Default";
  editItems.items[KEY_ICON_6].label.address = (void *)"Save";
  snprintf(title, sizeof(title), "%s", p->label);
  fmEditSetStepItem(&editItems.items[KEY_ICON_4], stepLabel, sizeof(stepLabel), p, step, stepTier);

  menuDrawPage(&editItems);
  fmEditDraw(p, value, true);

  #if LCD_ENCODER_SUPPORT
    LCD_Enc_ResetClick();  // a press still held from before does not save
  #endif

  while (MENU_IS(menuFmEditParam))
  {
    int32_t newValue = value;
    bool save = false;

    #if LCD_ENCODER_SUPPORT
      int16_t detents = encoderPosition;  // read before menuKeyGetValue(), which folds all detents into one key

      encoderPosition = 0;

      if (detents != 0)
      {
        uint32_t now = OS_GetTimeMs();
        int32_t mul = 1;

        if (now - lastTurnMs < FM_EDIT_FAST_MS)
          mul = FM_EDIT_FAST_MUL;
        else if (now - lastTurnMs < FM_EDIT_MEDIUM_MS)
          mul = FM_EDIT_MEDIUM_MUL;

        lastTurnMs = now;
        newValue = fmEditClamp(p, (int64_t)value + (int64_t)detents * step * mul);
      }

      if (LCD_Enc_ReadClick())
        save = true;
    #endif

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
      case KEY_DECREASE:
        newValue = fmEditClamp(p, (int64_t)value - step);
        break;

      case KEY_ICON_3:
      case KEY_INCREASE:
        newValue = fmEditClamp(p, (int64_t)value + step);
        break;

      case KEY_ICON_4:  // step 1, 10, 100 .. (raw units) while the range holds at least 10 steps, then back to 1
        if ((int64_t)step * 10 * 10 <= range)
        {
          step *= 10;
          stepTier++;
        }
        else
        {
          step = 1;
          stepTier = 0;
        }

        fmEditSetStepItem(&editItems.items[KEY_ICON_4], stepLabel, sizeof(stepLabel), p, step, stepTier);
        menuDrawItem(&editItems.items[KEY_ICON_4], KEY_ICON_4);
        break;

      case KEY_ICON_5:
        newValue = fmEditClamp(p, p->def);
        break;

      case KEY_ICON_6:
        save = true;
        break;

      case KEY_INFOBOX:  // tap on the value: keypad
      {
        char numTitle[48];
        int64_t typed;

        if (p->unit[0] != '\0')
          snprintf(numTitle, sizeof(numTitle), "%s (%s)", p->label, p->unit);
        else
          snprintf(numTitle, sizeof(numTitle), "%s", p->label);

        if (p->decimals == 0)
        {
          typed = numPadInt((uint8_t *)numTitle, value, p->def, p->min < 0);
        }
        else
        {
          double scale = 1;

          for (uint8_t i = 0; i < p->decimals; i++)
            scale *= 10;

          double v = numPadFloat((uint8_t *)numTitle, value / scale, p->def / scale, p->min < 0);

          typed = (int64_t)(v * scale + (v >= 0 ? 0.5 : -0.5));
        }

        value = fmEditClamp(p, typed);
        newValue = value;

        if (value != typed)
          addToast(DIALOG_TYPE_INFO, "Limited to the allowed range");

        menuDrawPage(&editItems);
        fmEditDraw(p, value, true);

        #if LCD_ENCODER_SUPPORT
          encoderPosition = 0;   // turns while the keypad was open
          LCD_Enc_ResetClick();
        #endif
        break;
      }

      case KEY_ICON_7:
        CLOSE_MENU();
        break;

      default:
        break;
    }

    if (newValue != value)
    {
      value = newValue;
      fmEditDraw(p, value, false);
    }

    if (save)
    {
      if (value != p->value)
        fmSetParam(fmEditIndex, value);

      BUZZER_PLAY(SOUND_OK);
      CLOSE_MENU();
      break;
    }

    if (serial != fmParamSerial())  // FM_V: the value on the controller changed
    {
      serial = fmParamSerial();
      p = fmParam(fmEditIndex);

      if (p == NULL)
      {
        CLOSE_MENU();
        break;
      }

      fmEditDraw(p, value, true);
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
