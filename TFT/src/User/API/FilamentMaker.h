#ifndef _FILAMENT_MAKER_H_
#define _FILAMENT_MAKER_H_

// Turtle FilamentMaker support.
//
// The FilamentMaker controller (ESP32) emulates a minimal Marlin host and identifies itself with
// "FIRMWARE_NAME:FilamentMaker" in its M115 answer. When detected, the TFT keeps working as for
// Marlin (temperatures, fans, notifications, host prompts) and additionally:
//   - asks for a periodic compact status line   FM_REPORT S1  ->  "FM:S.. V.. N.. ..."
//   - shows the FilamentMaker screens instead of the stock status screen and main menu
//   - edits the controller settings             FM_GET <group> / FM_SET <name> <value>
//   - shows controller / network info           FM_INFO
// Protocol: Turtle.FilamentMaker/FilamentMaker.ESP32/src/TftLink.h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define FM_TEMP_INVALID   -32768
#define FM_MAX_PARAMS     16  // largest settings group
#define FM_MAX_INFO       12
#define FM_NAME_LEN       24
#define FM_LABEL_LEN      26
#define FM_UNIT_LEN       10
#define FM_OPTIONS_LEN    24
#define FM_INFO_KEY_LEN   14
#define FM_INFO_VAL_LEN   34

// process state, same order as fp::ProcessState (shared/lib/FilamentProtocol/ProcessState.h)
typedef enum
{
  FM_IDLE_UNHOMED = 0,
  FM_HOMING,
  FM_IDLE_HOMED,
  FM_SPOOLING,
  FM_SPOOLING_WINDING,
  FM_PAUSED,
  FM_CHANGE_EMPTYING,
  FM_CHANGE_PAUSE,
  FM_CHANGE_REBALANCE,
  FM_ERROR,
  FM_STATE_COUNT
} FM_STATE;

// alarm bits, same as Alarm:: in MachineController.h
#define FM_ALARM_COUNT 12

typedef struct
{
  bool     valid;       // at least one FM: line received
  uint32_t updatedMs;   // time of the last FM: line
  uint8_t  state;       // FM_STATE
  int32_t  sps;         // current spool speed, steps/s
  int32_t  nominalSps;  // control loop nominal speed
  int8_t   buffer;      // 0 = empty .. 4 = full, -1 = unknown
  int32_t  revs100;     // spool revolutions * 100
  uint32_t layers;
  int32_t  lengthDm;    // filament length, 0.1 m
  uint32_t alarms;      // FM_ALARM bit mask
  uint8_t  fan1;        // %
  uint8_t  fan2;        // %
  bool     winding;
  bool     link;        // worker (ATmega) connected
  int16_t  temp1;       // 0.1 C, FM_TEMP_INVALID = no sensor
  int16_t  temp2;
  bool     simulated;   // the worker is simulated by the controller
} FM_STATUS;

typedef struct
{
  char    name[FM_NAME_LEN];
  char    label[FM_LABEL_LEN];
  char    unit[FM_UNIT_LEN];
  char    options[FM_OPTIONS_LEN];  // "Off,On" for enumerations, empty for numbers
  uint8_t decimals;                 // displayed = raw / 10^decimals
  int32_t value;
  int32_t def;
  int32_t min;
  int32_t max;
} FM_PARAM;

typedef struct
{
  char key[FM_INFO_KEY_LEN];
  char value[FM_INFO_VAL_LEN];
} FM_INFO_LINE;

extern FM_STATUS fmStatus;

// --- detection / life cycle ---
bool fmIsActive(void);                // FilamentMaker controller detected
void fmDetected(void);                // called from the M115 parser
void fmLoop(void);                    // background: keeps the status report alive

// --- parser (called from parseAck for lines starting with "FM") ---
void fmParseAck(const char * line);

// --- commands ---
void fmCommand(const char * cmd);     // "START" -> "FM_START\n"

// --- settings group cache ---
void fmRequestGroup(const char * group);
bool fmGroupReady(void);              // FM_P_END received for the last request
uint8_t fmParamCount(void);
FM_PARAM * fmParam(uint8_t index);
void fmSetParam(uint8_t index, int32_t raw);
uint32_t fmParamSerial(void);         // increments whenever a cached value changes (FM_V)

// --- info cache ---
void fmRequestInfo(void);
bool fmInfoReady(void);
uint8_t fmInfoCount(void);
const FM_INFO_LINE * fmInfo(uint8_t index);

// --- text helpers ---
const char * fmStateText(uint8_t state);
const char * fmAlarmName(uint8_t bit);
void fmFormatRaw(char * buf, uint8_t size, int32_t raw, uint8_t decimals);  // "1.900"
bool fmOption(const char * options, uint8_t index, char * buf, uint8_t size);  // n-th entry of "A,B,C"
uint8_t fmOptionCount(const char * options);

#ifdef __cplusplus
}
#endif

#endif
