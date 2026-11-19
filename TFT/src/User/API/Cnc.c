// Turtle CNC support - see Cnc.h

#ifdef TURTLE_CNC

#include "Cnc.h"
#include "includes.h"

CNC_STATE cncState;

static float    jogPending[CNC_AXES];  // mm not sent yet, per axis
static bool     jogRunning = false;    // a jog move (ending with M400) was sent and is not answered yet
static uint32_t lastQueryMs = 0;
static uint16_t feedShown = 100;       // value on the TFT
static uint16_t feedSent = 100;        // value last sent with M220
static uint32_t feedChangedMs = 0;
static bool     wasConnected = false;

static const char cncAxisId[CNC_AXES] = {'X', 'Y', 'Z'};

bool cncIsActive(void)
{
  return true;
}

// ---------------------------------------------------------------- position

static bool cncReadFloat(const char * from, const char * key, float * value)
{
  const char * p = strstr(from, key);

  if (p == NULL)
    return false;

  *value = strtof(p + strlen(key), NULL);
  return true;
}

static float cncStepsPerMm(uint8_t axis)
{
  float steps = getParameter(P_STEPS_PER_MM, axis);

  return steps > 0.0f ? steps : CNC_STEPS_DEFAULT;
}

void cncParsePosition(const char * line)
{
  const char * count = strstr(line, "Count");
  float v;

  // work coordinates come first, before "Count"
  for (uint8_t i = 0; i < CNC_AXES; i++)
  {
    char key[3] = {cncAxisId[i], ':', '\0'};

    if (cncReadFloat(line, key, &v))
      cncState.work[i] = v;
  }

  cncState.mcsKnown = false;

  if (count != NULL)
  {
    bool all = true;

    for (uint8_t i = 0; i < CNC_AXES; i++)
    {
      char key[3] = {cncAxisId[i], ':', '\0'};

      if (cncReadFloat(count, key, &v))
        cncState.mcs[i] = v / cncStepsPerMm(i);
      else
        all = false;
    }

    cncState.mcsKnown = all;
  }

  cncState.known = true;
  cncState.updatedMs = OS_GetTimeMs();
}

void cncQueryPosition(uint16_t intervalMs)
{
  uint32_t now = OS_GetTimeMs();

  if (!infoHost.connected || now - lastQueryMs < intervalMs)
    return;

  lastQueryMs = now;
  coordinateQuery(0);  // M114 (or a single report when Marlin auto-reports)
}

// ---------------------------------------------------------------- commands

bool cncIdle(void)
{
  return !isNotEmptyCmdQueue() && !isPendingCmd();
}

void cncJog(uint8_t axis, float distance)
{
  if (axis >= CNC_AXES || !infoHost.connected || isPrinting())
    return;

  jogPending[axis] += distance;
}

bool cncJogPending(void)
{
  for (uint8_t i = 0; i < CNC_AXES; i++)
  {
    if (jogPending[i] != 0.0f)
      return true;
  }

  return jogRunning;
}

void cncJogCancel(void)
{
  for (uint8_t i = 0; i < CNC_AXES; i++)
    jogPending[i] = 0.0f;
}

static void cncJogLoop(void)
{
  if (jogRunning)
  {
    if (!cncIdle())
      return;

    jogRunning = false;  // M400 answered: the move is finished
    lastQueryMs = 0;     // refresh the readout now
  }

  if (!cncIdle())
    return;

  for (uint8_t i = 0; i < CNC_AXES; i++)
  {
    float d = jogPending[i];

    // whole steps of 0.001 mm only; tiny float rests are dropped
    if (d > -0.0005f && d < 0.0005f)
    {
      jogPending[i] = 0.0f;
      continue;
    }

    jogPending[i] = 0.0f;

    mustStoreCmd("G91\n");
    mustStoreCmd("G1 %c%.3f F%d\n", cncAxisId[i], d, CNC_JOG_FEED);
    mustStoreCmd("G90\n");
    mustStoreCmd("M400\n");
    jogRunning = true;
    return;  // one axis per move
  }
}

void cncZero(bool x, bool y, bool z)
{
  char cmd[24] = "G92";

  if (x) strcat(cmd, " X0");
  if (y) strcat(cmd, " Y0");
  if (z) strcat(cmd, " Z0");

  if (!x && !y && !z)
    return;

  mustStoreCmd("%s\n", cmd);
  lastQueryMs = 0;
}

void cncHome(uint8_t axis)
{
  if (axis >= Z_AXIS)  // Z is manual
    return;

  cncJogCancel();
  mustStoreCmd("G28 %c\n", cncAxisId[axis]);
  cncState.homed[axis] = true;  // Marlin halts (kill) if homing fails; the reconnect clears the flags
  lastQueryMs = 0;
}

void cncGotoZero(void)
{
  cncJogCancel();
  mustStoreCmd("G90\n");
  mustStoreCmd("G0 X0 Y0 F%d\n", CNC_JOG_FEED);
  lastQueryMs = 0;
}

void cncMotorsOff(void)
{
  cncJogCancel();
  mustStoreCmd("M18\n");

  for (uint8_t i = 0; i < CNC_AXES; i++)
    cncState.homed[i] = false;
}

// ---------------------------------------------------------------- feed override

uint16_t cncGetFeed(void)
{
  return feedShown;
}

void cncSetFeed(uint16_t percent)
{
  if (percent < CNC_FEED_MIN) percent = CNC_FEED_MIN;
  if (percent > CNC_FEED_MAX) percent = CNC_FEED_MAX;

  feedShown = percent;
  feedChangedMs = OS_GetTimeMs();
}

static void cncFeedLoop(void)
{
  if (feedShown == feedSent || OS_GetTimeMs() - feedChangedMs < CNC_FEED_REST_MS || !infoHost.connected)
    return;

  if (storeCmd("M220 S%d\n", feedShown))
    feedSent = feedShown;
}

// ---------------------------------------------------------------- background

void cncLoop(void)
{
  if (infoHost.connected != wasConnected)
  {
    wasConnected = infoHost.connected;

    // new connection = Marlin (re)started: nothing is homed, feed is 100 %, no old position
    for (uint8_t i = 0; i < CNC_AXES; i++)
      cncState.homed[i] = false;

    cncState.known = false;
    cncState.mcsKnown = false;
    cncJogCancel();
    jogRunning = false;
    feedShown = feedSent = 100;
  }

  if (!infoHost.connected)
    return;

  cncJogLoop();
  cncFeedLoop();
}

// ---------------------------------------------------------------- job start

void cncStartCheckText(char * buf, size_t size, const char * filename)
{
  if (cncState.mcsKnown)
  {
    // work zero in machine coordinates = machine - work
    snprintf(buf, size, "%s\n\nSpindle on? (manual)\nTool height set? (Z manual)\nXY zero at machine X %.2f Y %.2f",
             filename, cncState.mcs[X_AXIS] - cncState.work[X_AXIS], cncState.mcs[Y_AXIS] - cncState.work[Y_AXIS]);
  }
  else
  {
    snprintf(buf, size, "%s\n\nSpindle on? (manual)\nTool height set? (Z manual)\nXY zero: not known yet", filename);
  }
}

#endif  // TURTLE_CNC
