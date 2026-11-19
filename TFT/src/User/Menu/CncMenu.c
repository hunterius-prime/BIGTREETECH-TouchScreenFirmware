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

#define CNC_LONG_PRESS_MS 600   // knob: shorter = click, longer (up to the 1.5 s mode switch) = long press
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

// focus frame for icon menus: turn moves it, click runs the focused icon (returned as its key)
static void navFrame(int8_t index, bool on)
{
  if (index < 0)
    return;

  const GUI_RECT * r = &rect_of_key[index];

  GUI_SetColor(on ? CNC_COLOR_FOCUS : infoSettings.bg_color);
  GUI_DrawRect(r->x0, r->y0, r->x1 - 1, r->y1 - 1);
  GUI_DrawRect(r->x0 + 1, r->y0 + 1, r->x1 - 2, r->y1 - 2);
  GUI_RestoreColorDefault();
}

static KEY_VALUES navHandle(const MENUITEMS * items, int8_t * focus, int16_t detents, KNOB_PRESS press)
{
  if (detents != 0)
  {
    int8_t f = *focus;
    int8_t dir = detents > 0 ? 1 : -1;

    if (f < 0)  // first turn: show the frame on the first icon in that direction
      f = dir > 0 ? -1 : ITEM_PER_PAGE;

    for (uint8_t n = 0; n < ITEM_PER_PAGE; n++)
    {
      f = (f + dir + ITEM_PER_PAGE) % ITEM_PER_PAGE;

      if (items->items[f].icon != ICON_NULL)
        break;
    }

    navFrame(*focus, false);
    *focus = f;
    navFrame(*focus, true);
  }

  if (press == KNOB_CLICK && *focus >= 0)
    return (KEY_VALUES)*focus;

  return KEY_IDLE;
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
  int8_t focus = -1;

  if (lockedInJob())
  {
    CLOSE_MENU();
    return;
  }

  menuDrawPage(&items);
  stripUpdate(true);
  knobReset();

  while (MENU_IS(menuCncZero))
  {
    KNOB_PRESS press;
    int16_t detents = knobRead(&press);

    cncQueryPosition(CNC_QUERY_MS);
    stripUpdate(false);

    key_num = navHandle(&items, &focus, detents, press);

    if (key_num == KEY_IDLE)
      key_num = menuKeyGetValue();

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
      {ICON_SETTINGS,          LABEL_SETTINGS},
      {ICON_SCREEN_INFO,       {.address = "Info"}},
      {ICON_BACK,              LABEL_BACK},
    }
  };

  KEY_VALUES key_num;
  int8_t focus = -1;

  menuDrawPage(&items);
  knobReset();

  while (MENU_IS(menuCncMenu))
  {
    KNOB_PRESS press;
    int16_t detents = knobRead(&press);

    key_num = navHandle(&items, &focus, detents, press);

    if (key_num == KEY_IDLE)
      key_num = menuKeyGetValue();

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
      case KEY_ICON_6: OPEN_MENU(menuInfo);     break;
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
