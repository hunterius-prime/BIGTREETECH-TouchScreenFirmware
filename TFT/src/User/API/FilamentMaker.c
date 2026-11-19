#include "FilamentMaker.h"
#include "includes.h"

#define FM_REPORT_PERIOD_S   1
#define FM_STALE_MS          3500  // no FM: line for this long -> ask again for the report
#define FM_RETRY_MS          3000

FM_STATUS fmStatus = {0};

static bool fmActive = true;  // this firmware is FilamentMaker only: its screens from boot, "offline" until the controller answers
static uint32_t fmLastRequestMs = 0;

static FM_PARAM fmParams[FM_MAX_PARAMS];
static uint8_t fmParamsCount = 0;
static bool fmParamsReady = false;
static uint32_t fmParamsSerial = 0;

static FM_INFO_LINE fmInfoLines[FM_MAX_INFO];
static uint8_t fmInfoLinesCount = 0;
static bool fmInfoLinesReady = false;

static const char * const fmStateTexts[FM_STATE_COUNT] = {
  "Idle - not homed",
  "Homing winder",
  "Idle - ready",
  "Spooling",
  "Spooling + winding",
  "Paused",
  "Spool change - emptying",
  "Mount a new spool",
  "Refilling buffer",
  "Error",
};

static const char * const fmAlarmNames[FM_ALARM_COUNT] = {
  "Worker link lost",
  "Worker fault",
  "T1 high",
  "T1 low",
  "T1 sensor",
  "T2 high",
  "T2 low",
  "T2 sensor",
  "Buffer full",
  "Homing failed",
  "Config sync",
  "Standalone",
};

// ---------------------------------------------------------------- life cycle

bool fmIsActive(void)
{
  return fmActive;
}

static void fmRequestReport(void)
{
  // count the attempt even when the queue is full: retrying on every loop pass
  // would flash the "Busy processing" toast continuously
  fmLastRequestMs = OS_GetTimeMs();

  char cmd[24];

  snprintf(cmd, sizeof(cmd), "FM_REPORT S%d\n", FM_REPORT_PERIOD_S);

  if (isFullCmdQueue() || isEnqueued(cmd))  // full: no toast; queued: one request is enough
    return;

  storeCmd("%s", cmd);
}

void fmDetected(void)
{
  fmActive = true;
  fmStatus.valid = false;
  fmRequestReport();

  // leave the stock screens if they are shown
  if (MENU_IS(menuStatus))
    REPLACE_MENU(menuFmStatus);
  else if (MENU_IS(menuMain))
    REPLACE_MENU(menuFmMenu);
}

void fmLoop(void)
{
  if (!fmActive || !infoHost.connected)
    return;

  uint32_t now = OS_GetTimeMs();

  // controller restarted (it forgets FM_REPORT) or the request got lost
  if ((!fmStatus.valid || now - fmStatus.updatedMs > FM_STALE_MS) && now - fmLastRequestMs > FM_RETRY_MS)
    fmRequestReport();
}

// ---------------------------------------------------------------- parsing helpers

// copy field up to '|' or end of line; returns pointer after the separator (or at the end)
static const char * fmField(const char * p, char * out, uint8_t size)
{
  uint8_t n = 0;

  while (*p && *p != '|' && *p != '\n' && *p != '\r')
  {
    if (n + 1 < size)
      out[n++] = *p;

    p++;
  }

  out[n] = '\0';

  return (*p == '|') ? p + 1 : p;
}

static const char * fmFieldInt(const char * p, int32_t * out)
{
  char tmp[14];

  p = fmField(p, tmp, sizeof(tmp));
  *out = strtol(tmp, NULL, 10);

  return p;
}

// "FM:S3 V850 N850 B2 R1234 Y5 L123 A0 F50 G0 W1 K1 T412 U230 X0"
static void fmParseStatus(const char * p)
{
  while (*p && *p != '\n')
  {
    while (*p == ' ')
      p++;

    char key = *p;

    if (key == '\0' || key == '\n')
      break;

    char * end;
    long v = strtol(p + 1, &end, 10);

    if (end == p + 1)  // no number -> skip token
    {
      while (*p && *p != ' ' && *p != '\n')
        p++;

      continue;
    }

    switch (key)
    {
      case 'S': fmStatus.state = (v >= 0 && v < FM_STATE_COUNT) ? v : FM_ERROR; break;
      case 'V': fmStatus.sps = v; break;
      case 'N': fmStatus.nominalSps = v; break;
      case 'B': fmStatus.buffer = (v >= 0 && v <= 4) ? v : -1; break;
      case 'R': fmStatus.revs100 = v; break;
      case 'Y': fmStatus.layers = v; break;
      case 'L': fmStatus.lengthDm = v; break;
      case 'A': fmStatus.alarms = strtoul(p + 1, NULL, 10); break;
      case 'F': fmStatus.fan1 = v; break;
      case 'G': fmStatus.fan2 = v; break;
      case 'W': fmStatus.winding = v != 0; break;
      case 'K': fmStatus.link = v != 0; break;
      case 'T': fmStatus.temp1 = v; break;
      case 'U': fmStatus.temp2 = v; break;
      case 'X': fmStatus.simulated = v != 0; break;
      default: break;
    }

    p = end;
  }

  fmStatus.valid = true;
  fmStatus.updatedMs = OS_GetTimeMs();
}

// "FM_P:<name>|<group>|<label>|<unit>|<decimals>|<value>|<default>|<min>|<max>|<options>"
static void fmParseParam(const char * p)
{
  if (fmParamsCount >= FM_MAX_PARAMS)
    return;

  FM_PARAM * prm = &fmParams[fmParamsCount];
  char group[16];
  int32_t dec;

  p = fmField(p, prm->name, sizeof(prm->name));
  p = fmField(p, group, sizeof(group));
  p = fmField(p, prm->label, sizeof(prm->label));
  p = fmField(p, prm->unit, sizeof(prm->unit));
  p = fmFieldInt(p, &dec);
  p = fmFieldInt(p, &prm->value);
  p = fmFieldInt(p, &prm->def);
  p = fmFieldInt(p, &prm->min);
  p = fmFieldInt(p, &prm->max);
  p = fmField(p, prm->options, sizeof(prm->options));

  prm->decimals = (dec >= 0 && dec <= 4) ? dec : 0;

  if (prm->name[0] != '\0')
    fmParamsCount++;
}

// "FM_V:<name>|<value>"
static void fmParseValue(const char * p)
{
  char name[FM_NAME_LEN];
  int32_t value;

  p = fmField(p, name, sizeof(name));
  fmFieldInt(p, &value);

  for (uint8_t i = 0; i < fmParamsCount; i++)
  {
    if (strcmp(fmParams[i].name, name) == 0)
    {
      fmParams[i].value = value;
      fmParamsSerial++;
      break;
    }
  }
}

// "FM_I:<key>|<value>"
static void fmParseInfo(const char * p)
{
  if (fmInfoLinesCount >= FM_MAX_INFO)
    return;

  FM_INFO_LINE * line = &fmInfoLines[fmInfoLinesCount++];

  p = fmField(p, line->key, sizeof(line->key));
  fmField(p, line->value, sizeof(line->value));
}

void fmParseAck(const char * line)
{
  if (strncmp(line, "FM:", 3) == 0)
    fmParseStatus(line + 3);
  else if (strncmp(line, "FM_P_END:", 9) == 0)
    fmParamsReady = true;
  else if (strncmp(line, "FM_P:", 5) == 0)
    fmParseParam(line + 5);
  else if (strncmp(line, "FM_V:", 5) == 0)
    fmParseValue(line + 5);
  else if (strncmp(line, "FM_I_END", 8) == 0)
    fmInfoLinesReady = true;
  else if (strncmp(line, "FM_I:", 5) == 0)
    fmParseInfo(line + 5);
}

// ---------------------------------------------------------------- commands

void fmCommand(const char * cmd)
{
  mustStoreCmd("FM_%s\n", cmd);
}

void fmRequestGroup(const char * group)
{
  fmParamsCount = 0;
  fmParamsReady = false;
  mustStoreCmd("FM_GET %s\n", group);
}

bool fmGroupReady(void)
{
  return fmParamsReady;
}

uint8_t fmParamCount(void)
{
  return fmParamsCount;
}

FM_PARAM * fmParam(uint8_t index)
{
  return index < fmParamsCount ? &fmParams[index] : NULL;
}

void fmSetParam(uint8_t index, int32_t raw)
{
  if (index >= fmParamsCount)
    return;

  mustStoreCmd("FM_SET %s %ld\n", fmParams[index].name, (long)raw);
}

uint32_t fmParamSerial(void)
{
  return fmParamsSerial;
}

void fmRequestInfo(void)
{
  fmInfoLinesCount = 0;
  fmInfoLinesReady = false;
  mustStoreCmd("FM_INFO\n");
}

bool fmInfoReady(void)
{
  return fmInfoLinesReady;
}

uint8_t fmInfoCount(void)
{
  return fmInfoLinesCount;
}

const FM_INFO_LINE * fmInfo(uint8_t index)
{
  return index < fmInfoLinesCount ? &fmInfoLines[index] : NULL;
}

// ---------------------------------------------------------------- text helpers

const char * fmStateText(uint8_t state)
{
  return state < FM_STATE_COUNT ? fmStateTexts[state] : "?";
}

const char * fmAlarmName(uint8_t bit)
{
  return bit < FM_ALARM_COUNT ? fmAlarmNames[bit] : "?";
}

void fmFormatRaw(char * buf, uint8_t size, int32_t raw, uint8_t decimals)
{
  if (decimals == 0)
  {
    snprintf(buf, size, "%ld", (long)raw);
    return;
  }

  int32_t div = 1;

  for (uint8_t i = 0; i < decimals; i++)
    div *= 10;

  int32_t a = raw < 0 ? -raw : raw;

  snprintf(buf, size, "%s%ld.%0*ld", raw < 0 ? "-" : "", (long)(a / div), decimals, (long)(a % div));
}

bool fmOption(const char * options, uint8_t index, char * buf, uint8_t size)
{
  const char * p = options;

  for (uint8_t i = 0; i < index; i++)
  {
    p = strchr(p, ',');

    if (p == NULL)
      return false;

    p++;
  }

  uint8_t n = 0;

  while (*p && *p != ',' && n + 1 < size)
    buf[n++] = *p++;

  buf[n] = '\0';

  return n > 0;
}

uint8_t fmOptionCount(const char * options)
{
  if (options == NULL || options[0] == '\0')
    return 0;

  uint8_t n = 1;

  for (const char * p = options; *p; p++)
  {
    if (*p == ',')
      n++;
  }

  return n;
}
