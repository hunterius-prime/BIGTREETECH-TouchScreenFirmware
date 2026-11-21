// Turtle CNC screens - see CncMenu.h and API/Cnc.h

#ifdef TURTLE_CNC

#include "CncMenu.h"
#include "includes.h"

// ============================================================================ look

#define CNC_COLOR_X      0xFAC7  // #ff5a3c
#define CNC_COLOR_Y      0x8EA7  // #8bd43a
#define CNC_COLOR_Z      0x2E3E  // #2ec5f0
#define CNC_COLOR_DIM    0x8C71  // #8c8c8c
#define CNC_COLOR_FOCUS  0xFEA0  // #ffd400
#define CNC_COLOR_SEP    0x2965  // #2e2e2e
#define CNC_COLOR_VALUE  WHITE

#define CNC_LONG_PRESS_MS KNOB_LONG_PRESS_MS  // knob: shorter = click, longer (up to the 1.5 s mode switch) = long press
#define CNC_QUERY_MS      250   // position query while idle
#define CNC_QUERY_JOB_MS  1000  // position query during a job

// readout layout (480 x 272): one line X | Y | Z, state and machine position under each value
#define DRO_TOP      (TITLE_END_Y + 14)  // top of the large glyphs
#define DRO_SEP_Y    (TITLE_END_Y + 58)  // line under the values
#define DRO_BADGE_Y  (DRO_SEP_Y + 6)
#define DRO_MCS_Y    (DRO_BADGE_Y + BYTE_HEIGHT + 6)
#define DRO_BOTTOM   (ICON_START_Y + ICON_HEIGHT + SPACE_Y - 6)

typedef struct
{
  int16_t  x0, x1;
  uint16_t color;
} DRO_COLUMN;

static const DRO_COLUMN droCol[CNC_AXES] = {
  {12,  184, CNC_COLOR_X},
  {194, 364, CNC_COLOR_Y},
  {374, 468, CNC_COLOR_Z},
};

// ============================================================================ large readout font

static const CNC_DRO_GLYPH * droGlyph(char c)
{
  for (uint8_t i = 0; i < cncDroFont.count; i++)
  {
    if (cncDroFont.glyph[i].ch == c)
      return &cncDroFont.glyph[i];
  }

  return NULL;
}

static uint16_t droBlend(uint16_t bg, uint16_t fg, uint8_t a)  // a: 0..15
{
  uint16_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a)) / 15;
  uint16_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (15 - a)) / 15;
  uint16_t b = ((fg & 0x1F) * a + (bg & 0x1F) * (15 - a)) / 15;

  return (r << 11) | (g << 5) | b;
}

static int16_t droTextWidth(const char * s)
{
  int16_t w = 0;

  for (; *s; s++)
  {
    const CNC_DRO_GLYPH * g = droGlyph(*s);

    w += g ? g->advance : cncDroFont.height / 3;
  }

  return w;
}

// draws s at x, y (top of the glyphs); returns the x after the text
static int16_t droDrawText(int16_t x, int16_t y, const char * s, uint16_t fg, uint16_t bg)
{
  uint16_t lut[16];
  const uint8_t h = cncDroFont.height;

  for (uint8_t a = 0; a < 16; a++)
    lut[a] = droBlend(bg, fg, a);

  for (; *s; s++)
  {
    const CNC_DRO_GLYPH * g = droGlyph(*s);

    if (g == NULL)  // unknown character: gap
    {
      GUI_FillRectColor(x, y, x + h / 3, y + h, bg);
      x += h / 3;
      continue;
    }

    const uint8_t * p = cncDroFont.data + g->offset;
    const uint8_t bytesPerRow = (g->advance + 1) / 2;

    LCD_SetWindow(x, y, x + g->advance - 1, y + h - 1);

    for (uint8_t row = 0; row < h; row++)
    {
      for (uint8_t col = 0; col < g->advance; col++)
      {
        uint8_t b = p[row * bytesPerRow + col / 2];

        LCD_WR_16BITS_DATA(lut[(col & 1) ? (b & 0x0F) : (b >> 4)]);
      }
    }

    x += g->advance;
  }

  return x;
}

// ============================================================================ readout (status screen)

static char droShown[CNC_AXES][16];
static char droMcsShown[CNC_AXES][16];
static int8_t droBadgeShown[CNC_AXES];

static void droFormat(char * buf, uint8_t size, float v)
{
  if (v > -0.005f && v < 0.005f)
    v = 0.0f;  // no "-0.00"

  snprintf(buf, size, "%.2f", v);
}

static void droDrawStatic(void)
{
  uint16_t bg = infoSettings.bg_color;

  GUI_ClearRect(0, TITLE_END_Y, LCD_WIDTH, DRO_BOTTOM);
  GUI_FillRectColor(droCol[0].x0, DRO_SEP_Y, droCol[2].x1, DRO_SEP_Y + 1, CNC_COLOR_SEP);
  GUI_FillRectColor(droCol[0].x1 + 4, TITLE_END_Y + 6, droCol[0].x1 + 5, DRO_BOTTOM, CNC_COLOR_SEP);
  GUI_FillRectColor(droCol[1].x1 + 4, TITLE_END_Y + 6, droCol[1].x1 + 5, DRO_BOTTOM, CNC_COLOR_SEP);

  for (uint8_t i = 0; i < CNC_AXES; i++)
  {
    char letter[2] = {"XYZ"[i], '\0'};

    droDrawText(droCol[i].x0, DRO_TOP, letter, droCol[i].color, bg);
    droShown[i][0] = '\0';
    droMcsShown[i][0] = '\0';
    droBadgeShown[i] = -1;
  }
}

static void droDrawBadge(uint8_t axis)
{
  // 0 = not homed, 1 = homed, 2 = manual (Z)
  int8_t state = axis == Z_AXIS ? 2 : (cncState.homed[axis] ? 1 : 0);
  const char * text = state == 2 ? "MANUAL" : (state == 1 ? "HOMED" : "NOT HOMED");
  uint16_t color = state == 2 ? CNC_COLOR_DIM : (state == 1 ? CNC_COLOR_Y : CNC_COLOR_FOCUS);
  int16_t x0 = droCol[axis].x0;
  int16_t w = strlen(text) * BYTE_WIDTH + 8;

  if (state == droBadgeShown[axis])
    return;

  droBadgeShown[axis] = state;

  GUI_ClearRect(x0, DRO_BADGE_Y, droCol[axis].x1, DRO_BADGE_Y + BYTE_HEIGHT + 2);
  GUI_SetColor(color);
  GUI_DrawRect(x0, DRO_BADGE_Y, x0 + w, DRO_BADGE_Y + BYTE_HEIGHT + 2);
  GUI_SetTextMode(GUI_TEXTMODE_TRANS);
  GUI_DispString(x0 + 4, DRO_BADGE_Y + 1, (uint8_t *)text);
  GUI_RestoreColorDefault();
}

static void droUpdate(bool force)
{
  uint16_t bg = infoSettings.bg_color;
  char v[16];

  if (force)
    droDrawStatic();

  for (uint8_t i = 0; i < CNC_AXES; i++)
  {
    char letter[2] = {"XYZ"[i], '\0'};
    int16_t xv = droCol[i].x0 + droTextWidth(letter) + 2;

    // value
    if (infoHost.connected && cncState.known)
      droFormat(v, sizeof(v), cncState.work[i]);
    else
      strcpy(v, "--.--");

    if (strcmp(v, droShown[i]) != 0)
    {
      strcpy(droShown[i], v);

      if (i == Z_AXIS)  // Z is manual: small and grey, bottom aligned with X and Y
      {
        GUI_ClearRect(xv, DRO_TOP, droCol[i].x1, DRO_TOP + cncDroFont.height);
        GUI_SetColor(CNC_COLOR_DIM);
        GUI_DispString(xv + 4, DRO_TOP + cncDroFont.height - BYTE_HEIGHT, (uint8_t *)v);
        GUI_RestoreColorDefault();
      }
      else
      {
        int16_t end = droDrawText(xv, DRO_TOP, v, CNC_COLOR_VALUE, bg);

        if (end < droCol[i].x1)
          GUI_FillRectColor(end, DRO_TOP, droCol[i].x1, DRO_TOP + cncDroFont.height, bg);
      }
    }

    // homed / manual
    droDrawBadge(i);

    // machine position
    if (infoHost.connected && cncState.mcsKnown)
    {
      droFormat(v, sizeof(v), cncState.mcs[i]);

      if (i != Z_AXIS)
      {
        char t[16];

        snprintf(t, sizeof(t), "MCS %s", v);
        strcpy(v, t);
      }
    }
    else
    {
      strcpy(v, i != Z_AXIS ? "MCS --" : "--");
    }

    if (strcmp(v, droMcsShown[i]) != 0)
    {
      strcpy(droMcsShown[i], v);
      GUI_ClearRect(droCol[i].x0, DRO_MCS_Y, droCol[i].x1, DRO_MCS_Y + BYTE_HEIGHT);
      GUI_SetColor(CNC_COLOR_DIM);
      GUI_DispString(droCol[i].x0, DRO_MCS_Y, (uint8_t *)v);
      GUI_RestoreColorDefault();
    }
  }
}

// ============================================================================ strip (jog, zero): live work position

static const GUI_RECT cncStripRect = {START_X,                                ICON_HEIGHT + ICON_START_Y + STATUS_GANTRY_YOFFSET,
                                      START_X + 4 * ICON_WIDTH + 3 * SPACE_X, ICON_HEIGHT + SPACE_Y + ICON_START_Y - STATUS_GANTRY_YOFFSET};
static char cncStripShown[48];

static void stripUpdate(bool force)
{
  char text[48];

  if (infoHost.connected && cncState.known)
  {
    char x[12], y[12], z[12];

    droFormat(x, sizeof(x), cncState.work[X_AXIS]);
    droFormat(y, sizeof(y), cncState.work[Y_AXIS]);
    droFormat(z, sizeof(z), cncState.work[Z_AXIS]);
    snprintf(text, sizeof(text), "X %s   Y %s   Z %s", x, y, z);
  }
  else
  {
    snprintf(text, sizeof(text), infoHost.connected ? "Waiting for position" : "Marlin not connected");
  }

  if (!force && strcmp(text, cncStripShown) == 0)
    return;

  strcpy(cncStripShown, text);
  GUI_SetColor(GANTRY_XYZ_BG_COLOR);
  GUI_FillPrect(&cncStripRect);
  GUI_SetTextMode(GUI_TEXTMODE_TRANS);
  GUI_SetColor(GANTRY_XYZ_FONT_COLOR);
  GUI_DispStringInPrect(&cncStripRect, (uint8_t *)text);
  GUI_RestoreColorDefault();
}

// ============================================================================ knob

typedef enum
{
  KNOB_NONE = 0,
  KNOB_CLICK,
  KNOB_LONG,
} KNOB_PRESS;

// detents since the last call (consumed: menuKeyGetValue() does not see them) and the button press
static int16_t knobRead(KNOB_PRESS * press)
{
  int16_t detents = 0;

  *press = KNOB_NONE;

  #if LCD_ENCODER_SUPPORT
    detents = encoderPosition;
    encoderPosition = 0;

    switch (LCD_Enc_ReadPress(CNC_LONG_PRESS_MS))
    {
      case ENC_PRESS_SHORT: *press = KNOB_CLICK; break;
      case ENC_PRESS_LONG:  *press = KNOB_LONG;  break;
      default: break;
    }
  #endif

  return detents;
}

static void knobReset(void)
{
  #if LCD_ENCODER_SUPPORT
    encoderPosition = 0;
    LCD_Enc_ResetClick();
  #endif
}

// ============================================================================ dialogs

static void cncAsk(const char * title, const char * msg, void (* ok_action)(void))
{
  _setDialogTitleStr((uint8_t *)title);
  _setDialogMsgStr((uint8_t *)msg);
  _setDialogOkTextLabel(LABEL_CONFIRM);
  _setDialogCancelTextLabel(LABEL_CANCEL);
  showDialog(DIALOG_TYPE_QUESTION, ok_action, NULL, NULL);
}

static void cncZeroXY(void)       { cncZero(true, true, false); addToast(DIALOG_TYPE_SUCCESS, "XY zero set here"); }
static void cncHomeX(void)        { cncHome(X_AXIS); }
static void cncHomeY(void)        { cncHome(Y_AXIS); }
static void cncDoGotoZero(void)   { cncGotoZero(); }
static void cncDoMotorsOff(void)  { cncMotorsOff(); }
static void cncDoStop(void)       { abortPrint(); }

static void askZeroXY(void)
{
  static char msg[80];

  if (cncState.mcsKnown)
    snprintf(msg, sizeof(msg), "Make this spot X0 Y0?\nMachine X %.2f  Y %.2f", cncState.mcs[X_AXIS], cncState.mcs[Y_AXIS]);
  else
    snprintf(msg, sizeof(msg), "Make this spot X0 Y0?");

  cncAsk("Zero XY", msg, cncZeroXY);
}

static void askHome(uint8_t axis)
{
  static char msg[120];

  snprintf(msg, sizeof(msg), "Home the %c axis?\nGoes to the endstop, then back to the table centre.\nRaise the tool first. Clears the %c work zero.",
           "XY"[axis], "XY"[axis]);
  cncAsk(axis == X_AXIS ? "Home X" : "Home Y", msg, axis == X_AXIS ? cncHomeX : cncHomeY);
}

static bool lockedInJob(void)
{
  if (!isPrinting())
    return false;

  addToast(DIALOG_TYPE_ERROR, "Not while a job is running");
  return true;
}

// ============================================================================ status

static char cncTitle[48];

static void statusTitle(char * buf, uint8_t size)
{
  if (!infoHost.connected)
  {
    snprintf(buf, size, "RCNC  Marlin not connected");
  }
  else if (isPrinting())
  {
    char name[200];  // getPrintTitle() takes an 8-bit length
    uint32_t t = getPrintTime();

    if (!getPrintTitle(name, sizeof(name)))
      strcpy(name, "Job");

    if (strlen(name) > 22)  // keep percent and time visible
      strcpy(name + 19, "...");

    snprintf(buf, size, "%s%s  %d%%  %02lu:%02lu", isPaused() ? "PAUSED " : "", name, getPrintProgress(),
             (unsigned long)(t / 3600 ? t / 3600 : t / 60), (unsigned long)(t / 3600 ? (t / 60) % 60 : t % 60));
  }
  else if (!cncState.homed[X_AXIS] || !cncState.homed[Y_AXIS])
  {
    snprintf(buf, size, "RCNC  Idle   NOT HOMED:%s%s", cncState.homed[X_AXIS] ? "" : " X", cncState.homed[Y_AXIS] ? "" : " Y");
  }
  else
  {
    snprintf(buf, size, "RCNC  Idle   X Y homed");
  }
}

// bottom row: 0 = idle, 1 = running, 2 = paused
static void statusItems(MENUITEMS * items, uint8_t mode, char * feedLabel, uint8_t feedSize)
{
  static const ITEM idleItems[4] = {
    {ICON_MOVE,     {.address = "Jog"}},
    {ICON_POINT_1,  {.address = "Zero XY"}},
    {ICON_FILE,     {.address = "Files"}},
    {ICON_MAINMENU, {.address = "Menu"}},
  };

  if (mode == 0)
  {
    for (uint8_t i = 0; i < 4; i++)
      items->items[4 + i] = idleItems[i];

    return;
  }

  snprintf(feedLabel, feedSize, "Feed %d%%", cncGetFeed());
  items->items[4] = (ITEM){mode == 2 ? ICON_RESUME : ICON_PAUSE, {.address = mode == 2 ? "Resume" : "Pause"}};
  items->items[5] = (ITEM){ICON_STOP, {.address = "Stop"}};
  items->items[6] = (ITEM){ICON_PERCENTAGE, {.address = feedLabel}};
  items->items[7] = (ITEM){ICON_MAINMENU, {.address = "Menu"}};
}

void menuCncStatus(void)
{
  static char feedLabel[16];

  MENUITEMS items = {
    // title
    {.address = cncTitle},
    // icon                label
    {
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
    }
  };

  uint8_t mode = isPrinting() ? (isPaused() ? 2 : 1) : 0;
  uint16_t feedDrawn = cncGetFeed();
  char title[sizeof(cncTitle)];
  KEY_VALUES key_num;

  statusTitle(cncTitle, sizeof(cncTitle));
  statusItems(&items, mode, feedLabel, sizeof(feedLabel));

  GUI_SetBkColor(infoSettings.bg_color);
  menuDrawPage(&items);
  droUpdate(true);
  knobReset();

  while (MENU_IS(menuCncStatus))
  {
    KNOB_PRESS press;
    int16_t detents = knobRead(&press);
    uint8_t newMode = isPrinting() ? (isPaused() ? 2 : 1) : 0;

    cncQueryPosition(newMode ? CNC_QUERY_JOB_MS : CNC_QUERY_MS);

    if (newMode != mode || (newMode != 0 && cncGetFeed() != feedDrawn))
    {
      mode = newMode;
      feedDrawn = cncGetFeed();
      statusItems(&items, mode, feedLabel, sizeof(feedLabel));

      for (uint8_t i = 4; i < 8; i++)
        menuDrawItem(&items.items[i], i);
    }

    statusTitle(title, sizeof(title));

    if (strcmp(title, cncTitle) != 0)
    {
      strcpy(cncTitle, title);
      menuDrawTitle();
    }

    droUpdate(false);

    // knob: idle -> turning does nothing, click = Jog; job -> turning = feed override, click = Feed
    if (detents != 0 && mode != 0)
      cncSetFeed(cncGetFeed() + detents * 5);

    if (press == KNOB_CLICK)
      OPEN_MENU(mode != 0 ? menuCncFeed : menuCncJog);
    else if (press == KNOB_LONG)
      OPEN_MENU(menuCncMenu);

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_4:
        if (mode == 0)
          OPEN_MENU(menuCncJog);
        else
          pausePrint(mode == 1, PAUSE_EXTERNAL);  // hold / continue the stream, no moves
        break;

      case KEY_ICON_5:
        if (mode == 0)
          askZeroXY();
        else
          cncAsk("Stop job", "Stop sending the file?\nMoves already in Marlin's buffer finish.\nThe spindle is manual: switch it off yourself.", cncDoStop);
        break;

      case KEY_ICON_6:
        if (mode == 0)
          OPEN_MENU(menuPrint);
        else
          OPEN_MENU(menuCncFeed);
        break;

      case KEY_ICON_7:
        OPEN_MENU(menuCncMenu);
        break;

      default:
        break;
    }

    loopProcess();
  }
}

// ============================================================================ jog

static const float jogSteps[] = {0.01f, 0.1f, 1.0f, 10.0f};
static const uint16_t jogStepIcons[] = {ICON_001_MM, ICON_01_MM, ICON_1_MM, ICON_10_MM};
static const char * const jogStepLabels[] = {"0.01 mm", "0.1 mm", "1 mm", "10 mm"};
static uint8_t jogStep = 1;         // 0.1 mm
static uint8_t jogKnobAxis = X_AXIS;

static float jogKnobStep(void)  // the knob moves at most 1 mm per detent; 10 mm is for the buttons only
{
  return jogSteps[jogStep] > 1.0f ? 1.0f : jogSteps[jogStep];
}

static void jogTitle(char * buf, uint8_t size)
{
  snprintf(buf, size, "Jog   knob %c  %s mm%s", "XY"[jogKnobAxis], jogStep == 0 ? "0.01" : (jogStep == 1 ? "0.1" : "1"),
           jogStep == 3 ? " (max)" : "");
}

void menuCncJog(void)
{
  MENUITEMS items = {
    // title
    {.address = cncTitle},
    // icon                label
    {
      {ICON_01_MM,         {.address = "0.1 mm"}},
      {ICON_Y_INC,         {.address = "Y+"}},
      {ICON_KNOB_X,        {.address = "Knob: X"}},
      {ICON_POINT_1,       {.address = "Zero XY"}},
      {ICON_X_DEC,         {.address = "X-"}},
      {ICON_Y_DEC,         {.address = "Y-"}},
      {ICON_X_INC,         {.address = "X+"}},
      {ICON_BACK,          LABEL_BACK},
    }
  };

  KEY_VALUES key_num;

  if (lockedInJob())
  {
    CLOSE_MENU();
    return;
  }

  items.items[0].icon = jogStepIcons[jogStep];
  items.items[0].label.address = (void *)jogStepLabels[jogStep];
  items.items[2].icon = jogKnobAxis == X_AXIS ? ICON_KNOB_X : ICON_KNOB_Y;
  items.items[2].label.address = jogKnobAxis == X_AXIS ? "Knob: X" : "Knob: Y";
  jogTitle(cncTitle, sizeof(cncTitle));

  menuDrawPage(&items);
  stripUpdate(true);
  knobReset();

  while (MENU_IS(menuCncJog))
  {
    KNOB_PRESS press;
    int16_t detents = knobRead(&press);
    bool axisChanged = false;

    cncQueryPosition(CNC_QUERY_MS);
    stripUpdate(false);

    if (detents != 0)
      cncJog(jogKnobAxis, detents * jogKnobStep());

    if (press == KNOB_CLICK)
      axisChanged = true;

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0:
        jogStep = (jogStep + 1) % COUNT(jogSteps);
        items.items[0].icon = jogStepIcons[jogStep];
        items.items[0].label.address = (void *)jogStepLabels[jogStep];
        menuDrawItem(&items.items[0], 0);
        jogTitle(cncTitle, sizeof(cncTitle));
        menuDrawTitle();
        break;

      case KEY_ICON_1: cncJog(Y_AXIS, jogSteps[jogStep]);  break;
      case KEY_ICON_2: axisChanged = true;                   break;
      case KEY_ICON_3: cncZeroXY();                          break;
      case KEY_ICON_4: cncJog(X_AXIS, -jogSteps[jogStep]); break;
      case KEY_ICON_5: cncJog(Y_AXIS, -jogSteps[jogStep]); break;
      case KEY_ICON_6: cncJog(X_AXIS, jogSteps[jogStep]);  break;

      case KEY_ICON_7:
        cncJogCancel();
        CLOSE_MENU();
        break;

      default:
        break;
    }

    if (axisChanged)
    {
      jogKnobAxis = jogKnobAxis == X_AXIS ? Y_AXIS : X_AXIS;
      items.items[2].icon = jogKnobAxis == X_AXIS ? ICON_KNOB_X : ICON_KNOB_Y;
      items.items[2].label.address = jogKnobAxis == X_AXIS ? "Knob: X" : "Knob: Y";
      menuDrawItem(&items.items[2], 2);
      jogTitle(cncTitle, sizeof(cncTitle));
      menuDrawTitle();
    }

    loopProcess();
  }
}

// ============================================================================ work zero

void menuCncZero(void)
{
  MENUITEMS items = {
    // title
    {.address = "Work zero  (G92)"},
    // icon                label
    {
      {ICON_POINT_1,       {.address = "Zero XY"}},
      {ICON_ZERO_X,        {.address = "Zero X"}},
      {ICON_ZERO_Y,        {.address = "Zero Y"}},
      {ICON_ZERO_Z,        {.address = "Zero Z"}},
      {ICON_GOTO_ZERO,     {.address = "Go to 0"}},
      {ICON_X_HOME,        {.address = "Home X"}},
      {ICON_Y_HOME,        {.address = "Home Y"}},
      {ICON_BACK,          LABEL_BACK},
    }
  };

  KEY_VALUES key_num;

  if (lockedInJob())
  {
    CLOSE_MENU();
    return;
  }

  menuDrawPage(&items);
  stripUpdate(true);

  while (MENU_IS(menuCncZero))
  {
    cncQueryPosition(CNC_QUERY_MS);
    stripUpdate(false);

    key_num = menuKeyGetValue();  // knob: focus frame + click (menu.c)

    switch (key_num)
    {
      case KEY_ICON_0: cncZeroXY(); break;
      case KEY_ICON_1: cncZero(true, false, false); addToast(DIALOG_TYPE_SUCCESS, "X zero set here"); break;
      case KEY_ICON_2: cncZero(false, true, false); addToast(DIALOG_TYPE_SUCCESS, "Y zero set here"); break;
      case KEY_ICON_3: cncZero(false, false, true); addToast(DIALOG_TYPE_SUCCESS, "Z zero set here"); break;

      case KEY_ICON_4:
        cncAsk("Go to X0 Y0", "Move to the work zero at 600 mm/min?\nRaise the tool first.", cncDoGotoZero);
        break;

      case KEY_ICON_5: askHome(X_AXIS); break;
      case KEY_ICON_6: askHome(Y_AXIS); break;
      case KEY_ICON_7: CLOSE_MENU();    break;

      default:
        break;
    }

    loopProcess();
  }
}

// ============================================================================ feed move
//
// One axis to a target at a cutting feed (API: cncFm* in Cnc.c). Design: the fmove / fmfeed screens of the mockup.
// Idle: axis | value (tap: keypad) | ABS / REL, then Step | Feed | Send | Back; knob: turn = value by the step,
// click = next step, long press = Send. Moving: Stop | Feed - | Feed + | (Back locked); knob: turn = feed,
// click = Stop. Stopped: Resume | Feed - | Feed + | Cancel with the standard knob focus, starting on Cancel.

#define CNC_COLOR_ERR  0xFBCD  // #ff7a6e

typedef enum
{
  FM_MSG_OK = 0,
  FM_MSG_WARN,
  FM_MSG_ERR,
} FM_MSG;

static struct
{
  uint8_t  axis;    // X_AXIS / Y_AXIS
  bool     rel;     // false = go to a position, true = move by a distance
  float    abs[2];  // go to, per axis (work coordinates)
  float    dist;    // move by
  uint8_t  step;    // knob step: index in jogSteps[]
  uint16_t feed;    // mm/min
} fmUi = {X_AXIS, false, {0.0f, 0.0f}, 1.0f, 2, 50};

static const uint16_t fmPresets[] = {20, 50, 100};
static const uint16_t fmPresetIcons[] = {ICON_FM_P20, ICON_FM_P50, ICON_FM_P100};
static const char * const fmPresetLabels[] = {"20 mm/min", "50 mm/min", "100 mm/min"};

static float fmRound(float v)  // values are shown and typed with 2 decimals
{
  return (float)(int32_t)(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f)) / 100.0f;
}

static float fmAbs(float v)
{
  return v < 0.0f ? -v : v;
}

static float fmValue(void)
{
  return fmUi.rel ? fmUi.dist : fmUi.abs[fmUi.axis];
}

static void fmSetValue(float v)
{
  v = fmRound(v);

  if (v > 999.99f)  v = 999.99f;  // travel is 300 mm; keeps the value inside its box
  if (v < -999.99f) v = -999.99f;

  if (fmUi.rel)
    fmUi.dist = v;
  else
    fmUi.abs[fmUi.axis] = v;
}

static uint16_t fmFeedStep(uint16_t feed, int8_t dir)  // 5 mm/min below 100, then 10
{
  if (dir > 0)
    feed += feed < 100 ? 5 : 10;
  else
    feed -= feed <= 100 ? 5 : 10;

  if (feed < CNC_FM_FEED_MIN || feed > 60000)  // 60000: wrapped below 0
    feed = CNC_FM_FEED_MIN;

  return feed > CNC_FM_FEED_MAX ? CNC_FM_FEED_MAX : feed;
}

static void fmSetFeed(uint16_t feed)
{
  cncFmSetFeed(feed);  // clamps; used by the next segments while moving or by Resume
  fmUi.feed = cncFmFeed();
}

static void fmTime(char * buf, uint8_t size, float dist, uint16_t feed)
{
  uint32_t s = (uint32_t)(dist / feed * 60.0f + 0.5f);

  snprintf(buf, size, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// what Send would do: the target, a line for the strip, false if it cannot be sent
static bool fmCheck(float * to, char * msg, uint8_t size, FM_MSG * kind)
{
  uint8_t a = fmUi.axis;
  char ax = "XY"[a];
  float from = fmRound(cncState.work[a]);
  char f[12], t[12], d[12], tm[12];
  float dist;

  *kind = FM_MSG_ERR;
  *to = fmUi.rel ? fmRound(from + fmUi.dist) : fmUi.abs[a];

  if (!infoHost.connected)
  {
    snprintf(msg, size, "Marlin not connected");
    return false;
  }

  if (!cncState.known)
  {
    snprintf(msg, size, "Waiting for position");
    return false;
  }

  dist = fmAbs(*to - from);
  droFormat(f, sizeof(f), from);
  droFormat(t, sizeof(t), *to);
  droFormat(d, sizeof(d), dist);
  fmTime(tm, sizeof(tm), dist, fmUi.feed);

  if (dist < 0.005f)
  {
    *kind = FM_MSG_WARN;
    snprintf(msg, size, "%c is already at %s", ax, t);
    return false;
  }

  if (!cncState.homed[a] || !cncState.mcsKnown)
  {
    *kind = FM_MSG_WARN;
    snprintf(msg, size, "%c not homed, no limit check  %s", ax, tm);
    return true;
  }

  // travel in work coordinates: work = machine + (work - machine)
  float offset = cncState.work[a] - cncState.mcs[a];
  float lo = fmRound((a == X_AXIS ? CNC_X_MIN : CNC_Y_MIN) + offset);
  float hi = fmRound((a == X_AXIS ? CNC_X_MAX : CNC_Y_MAX) + offset);

  if (*to < lo - 0.0005f || *to > hi + 0.0005f)
  {
    droFormat(f, sizeof(f), lo);
    droFormat(t, sizeof(t), hi);
    snprintf(msg, size, "Outside travel: %c %s..%s", ax, f, t);
    return false;
  }

  *kind = FM_MSG_OK;
  snprintf(msg, size, "%c %s -> %s  %s mm  %s", ax, f, t, d, tm);
  return true;
}

// strip line: what Send would do, or the move in progress
static FM_MSG fmSummary(char * msg, uint8_t size)
{
  CNC_FM_STATE st = cncFmState();
  uint8_t a = cncFmAxis();
  char w[12], t[12], d[12], tm[12];
  float left;

  if (st == CNC_FM_IDLE)
  {
    float to;
    FM_MSG kind;

    fmCheck(&to, msg, size, &kind);
    return kind;
  }

  left = fmAbs(cncFmTarget() - cncState.work[a]);
  droFormat(w, sizeof(w), cncState.work[a]);
  droFormat(t, sizeof(t), cncFmTarget());
  droFormat(d, sizeof(d), left);

  if (st == CNC_FM_MOVING)
  {
    fmTime(tm, sizeof(tm), left, cncFmFeed());
    snprintf(msg, size, "%c %s -> %s  %s mm  %s", "XY"[a], w, t, d, tm);
    return FM_MSG_OK;
  }

  if (st == CNC_FM_STOPPING)
    snprintf(msg, size, "Stopping ...");
  else
    snprintf(msg, size, "Stopped at %c %s  %s mm left", "XY"[a], w, d);

  return FM_MSG_WARN;
}

// progress of the move (or resume) in 1/1000, -1 = no bar
static int16_t fmProgress(void)
{
  CNC_FM_STATE st = cncFmState();
  float all, done;

  if (st == CNC_FM_IDLE)
    return -1;

  all = fmAbs(cncFmTarget() - cncFmFrom());
  done = all - fmAbs(cncFmTarget() - cncState.work[cncFmAxis()]);

  if (all < 0.001f || done <= 0.0f)
    return 0;

  return done >= all ? 1000 : (int16_t)(done / all * 1000.0f);
}

static char fmStripShown[48];
static int8_t fmKindShown;
static int16_t fmBarShown;

static void fmStripUpdate(bool force)
{
  char text[48];
  FM_MSG kind = fmSummary(text, sizeof(text));
  int16_t bar = fmProgress();
  const int16_t barY = cncStripRect.y1 - 3;

  if (force || kind != fmKindShown || strcmp(text, fmStripShown) != 0)
  {
    strcpy(fmStripShown, text);
    fmKindShown = kind;
    GUI_SetColor(GANTRY_XYZ_BG_COLOR);
    GUI_FillPrect(&cncStripRect);
    GUI_SetTextMode(GUI_TEXTMODE_TRANS);
    GUI_SetColor(kind == FM_MSG_ERR ? CNC_COLOR_ERR : (kind == FM_MSG_WARN ? CNC_COLOR_FOCUS : GANTRY_XYZ_FONT_COLOR));
    GUI_DispStringInPrect(&cncStripRect, (uint8_t *)text);
    GUI_RestoreColorDefault();
    fmBarShown = -2;  // the fill erased the bar
  }

  if (bar == fmBarShown)
    return;

  fmBarShown = bar;

  if (bar < 0)
    return;

  int16_t x = cncStripRect.x0 + (int32_t)(cncStripRect.x1 - cncStripRect.x0) * bar / 1000;

  if (x > cncStripRect.x0)
    GUI_FillRectColor(cncStripRect.x0, barY, x, cncStripRect.y1, CNC_COLOR_Z);

  if (x < cncStripRect.x1)
    GUI_FillRectColor(x, barY, cncStripRect.x1, cncStripRect.y1, CNC_COLOR_SEP);
}

// value box: the space between the first and the last icon of the top row (taps: exhibitRect = KEY_INFOBOX)
static char fmExhShown[64];

static void fmExhibitDraw(const char * letter, uint16_t color, const char * value, const char * sub, bool force)
{
  const GUI_RECT r = {exhibitRect.x0 - SPACE_X + 4, exhibitRect.y0, exhibitRect.x1 + 2 * SPACE_X - 4, exhibitRect.y1};
  uint16_t bg = infoSettings.bg_color;
  int16_t gap = letter[0] != '\0' ? 10 : 0;
  char key[64];

  snprintf(key, sizeof(key), "%s|%04X|%s|%s", letter, color, value, sub);

  if (!force && strcmp(key, fmExhShown) == 0)
    return;

  strcpy(fmExhShown, key);

  int16_t w = droTextWidth(letter) + gap + droTextWidth(value);
  int16_t x = (r.x0 + r.x1 - w) / 2;
  int16_t y = r.y0 + 8;

  GUI_ClearPrect(&r);

  if (letter[0] != '\0')
    x = droDrawText(x, y, letter, color, bg) + gap;

  droDrawText(x, y, value, CNC_COLOR_VALUE, bg);

  GUI_SetColor(CNC_COLOR_DIM);
  GUI_SetTextMode(GUI_TEXTMODE_TRANS);
  GUI_DispStringInRect(r.x0, r.y1 - BYTE_HEIGHT - 6, r.x1, r.y1 - 6, (uint8_t *)sub);
  GUI_RestoreColorDefault();
}

static void fmExhibitUpdate(bool force)
{
  bool idle = cncFmState() == CNC_FM_IDLE;
  uint8_t a = idle ? fmUi.axis : cncFmAxis();
  char letter[2] = {"XY"[a], '\0'};
  char value[16], sub[24];
  float v = idle ? fmValue() : cncFmTarget();

  droFormat(value, sizeof(value), v);

  if (idle && fmUi.rel && value[0] != '-')  // a distance shows its sign
  {
    memmove(value + 1, value, strlen(value) + 1);
    value[0] = '+';
  }

  if (idle)
    snprintf(sub, sizeof(sub), "%s  tap: keypad", fmUi.rel ? "move by" : "go to");
  else
    snprintf(sub, sizeof(sub), "F %d mm/min", fmUi.feed);

  fmExhibitDraw(letter, a == X_AXIS ? CNC_COLOR_X : CNC_COLOR_Y, value, sub, force);
}

static void fmTitle(CNC_FM_STATE st)
{
  const char * hint = st == CNC_FM_IDLE ? "hold knob: Send" :
                      st == CNC_FM_MOVING ? "knob: feed  click: Stop" :
                      st == CNC_FM_STOPPING ? "STOPPING" : "STOPPED";

  snprintf(cncTitle, sizeof(cncTitle), "Feed move   %s", hint);
}

static char fmFeedLabel[16];

static void fmSetItems(MENUITEMS * items, CNC_FM_STATE st)
{
  items->items[0] = (ITEM){fmUi.axis == X_AXIS ? ICON_FM_AX_X : ICON_FM_AX_Y, {.address = fmUi.axis == X_AXIS ? "Axis X" : "Axis Y"}};
  items->items[1] = (ITEM){ICON_NULL, LABEL_NULL};
  items->items[2] = (ITEM){ICON_NULL, LABEL_NULL};
  items->items[3] = (ITEM){fmUi.rel ? ICON_FM_REL : ICON_FM_ABS, {.address = fmUi.rel ? "Move by" : "Go to"}};

  if (st == CNC_FM_IDLE)
  {
    snprintf(fmFeedLabel, sizeof(fmFeedLabel), "F %d", fmUi.feed);
    items->items[4] = (ITEM){jogStepIcons[fmUi.step], {.address = (void *)jogStepLabels[fmUi.step]}};
    items->items[5] = (ITEM){ICON_FM_FEED, {.address = fmFeedLabel}};
    items->items[6] = (ITEM){ICON_FM_SEND, {.address = "Send"}};
    items->items[7] = (ITEM){ICON_BACK, LABEL_BACK};
  }
  else
  {
    bool moving = st == CNC_FM_MOVING;

    items->items[4] = (ITEM){moving ? ICON_PAUSE : ICON_RESUME, {.address = moving ? "Stop" : "Resume"}};
    items->items[5] = (ITEM){ICON_DEC, {.address = "Feed -"}};
    items->items[6] = (ITEM){ICON_INC, {.address = "Feed +"}};
    items->items[7] = moving ? (ITEM){ICON_BACK, LABEL_BACK} : (ITEM){ICON_STOP, {.address = "Cancel"}};
  }
}

static void fmDrawAll(MENUITEMS * items, CNC_FM_STATE st)
{
  fmSetItems(items, st);
  fmTitle(st);

  #ifdef KNOB_NAV
    menuKnobRemember(st == CNC_FM_IDLE || st == CNC_FM_MOVING ? -1 : KEY_ICON_7);  // stopped: focus on Cancel
  #endif

  menuDrawPage(items);
  fmExhibitUpdate(true);
  fmStripUpdate(true);
}

static void fmSend(void)
{
  float to;
  char msg[48];
  FM_MSG kind;

  if (!fmCheck(&to, msg, sizeof(msg), &kind))
  {
    addToast(kind == FM_MSG_ERR ? DIALOG_TYPE_ERROR : DIALOG_TYPE_INFO, msg);
    return;
  }

  if (lockedInJob())
    return;

  if (!cncFmStart(fmUi.axis, to, fmUi.feed))
    addToast(DIALOG_TYPE_ERROR, "Busy: wait for the jog to end");
}

static void fmFeedTurn(int16_t detents)
{
  uint16_t feed = fmUi.feed;

  for (; detents > 0; detents--)
    feed = fmFeedStep(feed, 1);

  for (; detents < 0; detents++)
    feed = fmFeedStep(feed, -1);

  fmSetFeed(feed);
}

// opened from the Menu: "go to" starts at the current position
static void cncFeedMoveOpen(void)
{
  if (cncFmState() == CNC_FM_IDLE && cncState.known)
  {
    fmUi.abs[X_AXIS] = fmRound(cncState.work[X_AXIS]);
    fmUi.abs[Y_AXIS] = fmRound(cncState.work[Y_AXIS]);
  }

  fmUi.feed = cncFmFeed();
  OPEN_MENU(menuCncFeedMove);
}

void menuCncFeedMove(void)
{
  MENUITEMS items = {
    // title
    {.address = cncTitle},
    // icon                label
    {
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
    }
  };

  CNC_FM_STATE st = cncFmState();
  KEY_VALUES key_num;

  if (st == CNC_FM_IDLE && lockedInJob())
  {
    CLOSE_MENU();
    return;
  }

  fmDrawAll(&items, st);
  knobReset();

  while (MENU_IS(menuCncFeedMove))
  {
    CNC_FM_STATE now = cncFmState();
    KNOB_PRESS press = KNOB_NONE;
    int16_t detents = 0;
    bool redraw = false;

    if (now != st)
    {
      if (cncFmTakeDone())
      {
        static char msg[24];
        char t[12];

        droFormat(t, sizeof(t), cncFmTarget());
        snprintf(msg, sizeof(msg), "%c at %s", "XY"[cncFmAxis()], t);
        fmUi.abs[cncFmAxis()] = cncFmTarget();
        addToast(DIALOG_TYPE_SUCCESS, msg);
      }

      st = now;
      fmDrawAll(&items, st);
      knobReset();
    }

    // readout: M114 when idle or stopped; while moving M154 S1 (M114 would give the end of the planned segments)
    if (st == CNC_FM_MOVING)
    {
      if (infoMachineSettings.autoReportPos == 1)
        coordinateQuery(1);
      else
        cncQueryPosition(1000);  // Marlin without auto report: the readout jumps by segments
    }
    else if (st != CNC_FM_STOPPING)
    {
      cncQueryPosition(CNC_QUERY_MS);
    }

    // knob: idle and moving use it here; stopped: focus frame + click (menu.c)
    if (st == CNC_FM_IDLE || st == CNC_FM_MOVING)
      detents = knobRead(&press);

    if (st == CNC_FM_IDLE)
    {
      if (detents != 0)
      {
        fmSetValue(fmValue() + detents * jogSteps[fmUi.step]);
      }

      if (press == KNOB_CLICK)
      {
        fmUi.step = (fmUi.step + 1) % COUNT(jogSteps);
        redraw = true;
      }
      else if (press == KNOB_LONG)
      {
        fmSend();
      }
    }
    else if (st == CNC_FM_MOVING)
    {
      if (detents != 0)
        fmFeedTurn(detents);

      if (press == KNOB_CLICK)
        cncFmStop();
    }

    key_num = menuKeyGetValue();

    if (st == CNC_FM_IDLE)
    {
      switch (key_num)
      {
        case KEY_ICON_0:
          fmUi.axis = fmUi.axis == X_AXIS ? Y_AXIS : X_AXIS;
          redraw = true;
          break;

        case KEY_ICON_3:
          fmUi.rel = !fmUi.rel;
          redraw = true;
          break;

        case KEY_INFOBOX:  // tap on the value: keypad
        {
          char t[24];

          snprintf(t, sizeof(t), "%s %c (mm)", fmUi.rel ? "Move by" : "Go to", "XY"[fmUi.axis]);
          fmSetValue(numPadFloat((uint8_t *)t, fmValue(), fmUi.rel ? 0.0 : fmRound(cncState.work[fmUi.axis]), true));
          fmDrawAll(&items, st);
          knobReset();
          break;
        }

        case KEY_ICON_4:
          fmUi.step = (fmUi.step + 1) % COUNT(jogSteps);
          redraw = true;
          break;

        case KEY_ICON_5: OPEN_MENU(menuCncFmFeed); break;
        case KEY_ICON_6: fmSend();                 break;
        case KEY_ICON_7: CLOSE_MENU();             break;

        default:
          break;
      }
    }
    else
    {
      switch (key_num)
      {
        case KEY_ICON_0:
        case KEY_ICON_3:
        case KEY_INFOBOX:
          addToast(DIALOG_TYPE_INFO, st == CNC_FM_MOVING ? "Stop the move first" : "Cancel the move first");
          break;

        case KEY_ICON_4:
          if (st == CNC_FM_MOVING)
            cncFmStop();
          else if (st == CNC_FM_PAUSED && !cncFmResume())
            addToast(DIALOG_TYPE_INFO, "Wait: Marlin drops moves 1 s after a stop");
          break;

        case KEY_ICON_5: fmSetFeed(fmFeedStep(fmUi.feed, -1)); break;
        case KEY_ICON_6: fmSetFeed(fmFeedStep(fmUi.feed, 1));  break;

        case KEY_ICON_7:
          if (st == CNC_FM_MOVING)
            addToast(DIALOG_TYPE_INFO, "Stop the move first");
          else if (st == CNC_FM_PAUSED)
            cncFmCancel();
          break;

        default:
          break;
      }
    }

    if (redraw)  // axis, mode or step: icons, title and value
    {
      fmSetItems(&items, st);
      menuDrawItem(&items.items[0], 0);
      menuDrawItem(&items.items[3], 3);
      menuDrawItem(&items.items[4], 4);
    }

    fmExhibitUpdate(false);
    fmStripUpdate(false);
    loopProcess();
  }
}

static void fmFeedDraw(bool force)
{
  char value[8];

  snprintf(value, sizeof(value), "%d", fmUi.feed);
  fmExhibitDraw("", WHITE, value, "5 to 600 mm/min", force);
}

void menuCncFmFeed(void)
{
  MENUITEMS items = {
    // title
    {.address = "Feed move: feed   tap: keypad"},
    // icon                label
    {
      {ICON_DEC,           {.address = "-"}},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_INC,           {.address = "+"}},
      {ICON_FM_P20,        {.address = "20 mm/min"}},
      {ICON_FM_P50,        {.address = "50 mm/min"}},
      {ICON_FM_P100,       {.address = "100 mm/min"}},
      {ICON_BACK,          LABEL_BACK},
    }
  };

  KEY_VALUES key_num;

  for (uint8_t i = 0; i < COUNT(fmPresets); i++)
  {
    items.items[4 + i].icon = fmPresetIcons[i];
    items.items[4 + i].label.address = (void *)fmPresetLabels[i];
  }

  #ifdef KNOB_NAV
    menuKnobRemember(-1);  // the knob changes the value here
  #endif

  menuDrawPage(&items);
  fmFeedDraw(true);
  knobReset();

  while (MENU_IS(menuCncFmFeed))
  {
    KNOB_PRESS press;
    int16_t detents = knobRead(&press);

    if (detents != 0)
      fmFeedTurn(detents);

    if (press == KNOB_CLICK)
      CLOSE_MENU();

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0: fmSetFeed(fmFeedStep(fmUi.feed, -1)); break;
      case KEY_ICON_3: fmSetFeed(fmFeedStep(fmUi.feed, 1));  break;

      case KEY_ICON_4:
      case KEY_ICON_5:
      case KEY_ICON_6:
        fmSetFeed(fmPresets[key_num - KEY_ICON_4]);
        break;

      case KEY_INFOBOX:  // tap on the value: keypad
      {
        int32_t typed = numPadInt((uint8_t *)"Feed (mm/min)", fmUi.feed, 50, false);

        fmSetFeed(typed < 0 ? CNC_FM_FEED_MIN : (typed > CNC_FM_FEED_MAX ? CNC_FM_FEED_MAX : (uint16_t)typed));

        if (fmUi.feed != typed)
          addToast(DIALOG_TYPE_INFO, "Feed: 5 to 600 mm/min");

        menuDrawPage(&items);
        fmFeedDraw(true);
        knobReset();
        break;
      }

      case KEY_ICON_7: CLOSE_MENU(); break;

      default:
        break;
    }

    fmFeedDraw(false);
    loopProcess();
  }
}

// ============================================================================ menu

void menuCncMenu(void)
{
  MENUITEMS items = {
    // title
    {.address = "Menu"},
    // icon                    label
    {
      {ICON_MOVE,              {.address = "Jog"}},
      {ICON_POINT_1,           {.address = "Work zero"}},
      {ICON_FILE,              {.address = "Files"}},
      {ICON_GCODE,             {.address = "Terminal"}},
      {ICON_DISABLE_STEPPERS,  {.address = "Motors off"}},
      {ICON_SETTINGS,          LABEL_SETTINGS},  // also holds Info
      {ICON_FEED_MOVE,         {.address = "Feed move"}},
      {ICON_BACK,              LABEL_BACK},
    }
  };

  KEY_VALUES key_num;

  menuDrawPage(&items);

  while (MENU_IS(menuCncMenu))
  {
    key_num = menuKeyGetValue();  // knob: focus frame + click (menu.c)

    switch (key_num)
    {
      case KEY_ICON_0: if (!lockedInJob()) OPEN_MENU(menuCncJog);  break;
      case KEY_ICON_1: if (!lockedInJob()) OPEN_MENU(menuCncZero); break;
      case KEY_ICON_2: if (!lockedInJob()) OPEN_MENU(menuPrint);   break;
      case KEY_ICON_3: OPEN_MENU(menuTerminal);                    break;

      case KEY_ICON_4:
        if (!lockedInJob())
          cncAsk("Motors off", "Switch the drivers off?\nThe table can then be moved by hand.\nHome X and Y again before a job.", cncDoMotorsOff);
        break;

      case KEY_ICON_5: OPEN_MENU(menuSettings); break;
      case KEY_ICON_6: if (!lockedInJob()) cncFeedMoveOpen(); break;
      case KEY_ICON_7: CLOSE_MENU();            break;

      default:
        break;
    }

    loopProcess();
  }
}

// ============================================================================ feed override

static void feedDraw(void)
{
  char text[16];

  snprintf(text, sizeof(text), "%d%%", cncGetFeed());
  GUI_ClearPrect(&exhibitRect);
  setFontSize(FONT_SIZE_LARGE);
  GUI_DispStringInPrect(&exhibitRect, (uint8_t *)text);
  setFontSize(FONT_SIZE_NORMAL);
}

void menuCncFeed(void)
{
  static const uint8_t feedSteps[] = {1, 5, 10};
  static const uint16_t feedStepIcons[] = {ICON_E_1_PERCENT, ICON_E_5_PERCENT, ICON_E_10_PERCENT};
  static const char * const feedStepLabels[] = {"Step 1%", "Step 5%", "Step 10%"};
  static uint8_t feedStep = 1;

  MENUITEMS items = {
    // title
    {.address = "Feed override  (M220)"},
    // icon                label
    {
      {ICON_DEC,           LABEL_DEC},
      {ICON_NULL,          LABEL_NULL},
      {ICON_NULL,          LABEL_NULL},
      {ICON_INC,           LABEL_INC},
      {ICON_E_5_PERCENT,   {.address = "Step 5%"}},
      {ICON_NULL,          LABEL_NULL},
      {ICON_RESET_VALUE,   {.address = "100%"}},
      {ICON_BACK,          LABEL_BACK},
    }
  };

  KEY_VALUES key_num;
  uint16_t shown;

  items.items[4].icon = feedStepIcons[feedStep];
  items.items[4].label.address = (void *)feedStepLabels[feedStep];

  menuDrawPage(&items);
  feedDraw();
  shown = cncGetFeed();
  knobReset();

  while (MENU_IS(menuCncFeed))
  {
    KNOB_PRESS press;
    int16_t detents = knobRead(&press);

    if (detents != 0)
      cncSetFeed(cncGetFeed() + detents * feedSteps[feedStep]);

    if (press == KNOB_CLICK)
      CLOSE_MENU();

    key_num = menuKeyGetValue();

    switch (key_num)
    {
      case KEY_ICON_0: cncSetFeed(cncGetFeed() - feedSteps[feedStep]); break;
      case KEY_ICON_3: cncSetFeed(cncGetFeed() + feedSteps[feedStep]); break;

      case KEY_ICON_4:
        feedStep = (feedStep + 1) % COUNT(feedSteps);
        items.items[4].icon = feedStepIcons[feedStep];
        items.items[4].label.address = (void *)feedStepLabels[feedStep];
        menuDrawItem(&items.items[4], 4);
        break;

      case KEY_ICON_6: cncSetFeed(100); break;
      case KEY_ICON_7: CLOSE_MENU();    break;

      default:
        break;
    }

    if (cncGetFeed() != shown)
    {
      shown = cncGetFeed();
      feedDraw();
    }

    loopProcess();
  }
}

// ============================================================================ stock hooks (printer screens not built)

void menuMain(void)  // stock main menu -> CNC menu
{
  REPLACE_MENU(menuCncMenu);
}

void menuPrinting(void)  // a running job is shown on the status screen
{
  infoMenu.cur = 0;
  REPLACE_MENU(menuCncStatus);
}

void menuPrintRestore(void)  // no power-loss resume on a mill: a restarted job would cut from a wrong position
{
  CLOSE_MENU();
}

// start a job from the TFT SD / USB (called by the file browser after the start check)
void startPrinting(void)
{
  if (!startPrint())
  {
    exitFolder();
    return;
  }

  clearInfoFile();  // free the file list (the stock printing screen does the same)
  cncSetFeed(100);
  infoMenu.cur = 0;
  REPLACE_MENU(menuCncStatus);
}

void startPrintingFromRemoteHost(const char * filename)
{
  if (!startPrintFromRemoteHost(filename))
    return;

  infoMenu.cur = 0;
  REPLACE_MENU(menuCncStatus);
}

#endif  // TURTLE_CNC
