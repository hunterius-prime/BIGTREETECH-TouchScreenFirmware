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
  if (axis >= CNC_AXES || !infoHost.connected || isPrinting() || cncFmState() != CNC_FM_IDLE)
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

// ---------------------------------------------------------------- feed move

static struct
{
  CNC_FM_STATE state;
  uint8_t      axis;
  float        target;     // work coordinates
  float        from;       // start of the move or of the resume
  float        sent;       // end of the last segment sent
  uint16_t     feed;       // mm/min of the next segment
  uint32_t     busyUntil;  // estimated time Marlin finishes the segments sent
  bool         endSent;    // M400 after the last segment is queued
  bool         stopSent;   // M410 is queued
  uint32_t     pausedMs;   // time the stop finished
  bool         done;       // the move reached its target (cncFmTakeDone)
} fm = {.feed = 50};

static float cncFmSegment(uint16_t feed)
{
  float seg = feed / 60.0f * CNC_FM_SEG_MS / 1000.0f;

  return seg > CNC_FM_SEG_MIN_MM ? seg : CNC_FM_SEG_MIN_MM;
}

static void cncFmBegin(void)
{
  fm.from = fm.sent = cncState.work[fm.axis];
  fm.busyUntil = OS_GetTimeMs();
  fm.endSent = false;
  fm.stopSent = false;
  fm.state = CNC_FM_MOVING;

  if (feedShown != 100 || feedSent != 100)  // the feed on the screen is the real one: no override left from a job
  {
    mustStoreCmd("M220 S100\n");
    feedShown = feedSent = 100;
  }

  mustStoreCmd("G90\n");
}

bool cncFmStart(uint8_t axis, float target, uint16_t feed)
{
  if (axis >= Z_AXIS || fm.state != CNC_FM_IDLE || !infoHost.connected || isPrinting() || cncJogPending())
    return false;

  fm.axis = axis;
  fm.target = target;
  fm.done = false;
  cncFmSetFeed(feed);
  cncFmBegin();
  return true;
}

void cncFmStop(void)
{
  if (fm.state == CNC_FM_MOVING)
    fm.state = CNC_FM_STOPPING;
}

bool cncFmResume(void)
{
  if (fm.state != CNC_FM_PAUSED || OS_GetTimeMs() - fm.pausedMs < CNC_FM_RESUME_MS || !infoHost.connected)
    return false;

  cncFmBegin();
  return true;
}

void cncFmCancel(void)
{
  if (fm.state == CNC_FM_PAUSED)
    fm.state = CNC_FM_IDLE;
}

void cncFmSetFeed(uint16_t feed)
{
  if (feed < CNC_FM_FEED_MIN) feed = CNC_FM_FEED_MIN;
  if (feed > CNC_FM_FEED_MAX) feed = CNC_FM_FEED_MAX;

  fm.feed = feed;
}

CNC_FM_STATE cncFmState(void) { return fm.state; }
uint8_t cncFmAxis(void)       { return fm.axis; }
float cncFmTarget(void)       { return fm.target; }
float cncFmFrom(void)         { return fm.from; }
uint16_t cncFmFeed(void)      { return fm.feed; }
uint32_t cncFmPausedMs(void)  { return OS_GetTimeMs() - fm.pausedMs; }

bool cncFmTakeDone(void)
{
  bool done = fm.done;

  fm.done = false;
  return done;
}

static void cncFmLoop(void)
{
  uint32_t now = OS_GetTimeMs();

  switch (fm.state)
  {
    case CNC_FM_MOVING:
      if (!cncIdle())  // the segment before is not taken yet (its "ok" comes once Marlin has planned it)
        return;

      if (fm.endSent)  // M400 answered: the table is at the target
      {
        fm.state = CNC_FM_IDLE;
        fm.done = true;
        lastQueryMs = 0;
        return;
      }

      if (fm.sent == fm.target)  // all segments sent: M400 once Marlin should be done, so Stop never waits for it long
      {
        if ((int32_t)(fm.busyUntil - now) <= 0)
        {
          mustStoreCmd("M400\n");
          fm.endSent = true;
        }

        return;
      }

      if ((int32_t)(fm.busyUntil - now) < CNC_FM_AHEAD_MS)
      {
        float seg = cncFmSegment(fm.feed);
        float left = fm.target - fm.sent;
        float next = fm.target;
        float len;

        // the last segment takes the rest when less than 1.5 segments are left (no short tail)
        if (left > 1.5f * seg)
          next = fm.sent + seg;
        else if (left < -1.5f * seg)
          next = fm.sent - seg;

        len = next > fm.sent ? next - fm.sent : fm.sent - next;
        mustStoreCmd("G1 %c%.3f F%d\n", cncAxisId[fm.axis], next, fm.feed);
        fm.sent = next;

        // its time at the feed and the M220 override Marlin applies to it
        if ((int32_t)(fm.busyUntil - now) < 0)
          fm.busyUntil = now;

        fm.busyUntil += (uint32_t)(len / fm.feed * 60000.0f * 100.0f / (feedSent ? feedSent : 100));
      }
      break;

    case CNC_FM_STOPPING:
      if (!cncIdle())  // a segment (or M114 / M154) waiting for its "ok": takes milliseconds
        return;

      if (!fm.stopSent)
      {
        if (fm.endSent)  // only M400 was left: the move has just finished
        {
          fm.state = CNC_FM_IDLE;
          fm.done = true;
          lastQueryMs = 0;
          return;
        }

        mustStoreCmd("M410\n");  // emergency parser: Marlin stops on receiving it
        mustStoreCmd("M400\n");
        mustStoreCmd("M114\n");  // stop position (after M410 Marlin's position is where the steppers stopped)
        fm.stopSent = true;
        return;
      }

      fm.state = CNC_FM_PAUSED;
      fm.pausedMs = now;
      break;

    default:
      break;
  }
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
    fm.state = CNC_FM_IDLE;  // Marlin restarted: whatever was planned is gone
    fm.done = false;
  }

  if (!infoHost.connected)
    return;

  cncFmLoop();
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
