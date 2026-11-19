#ifndef _CNC_H_
#define _CNC_H_

// Turtle CNC support (env TURTLE_CNC_TFT43, define TURTLE_CNC): the RCNC mini mill (Marlin, SKR 1.4).
//
// Position: Marlin answers M114 (and the M154 auto report) with
//   "X:12.35 Y:4.20 Z:0.00 Count X:-21530 Y:-7160 Z:0"
// X/Y/Z are work (logical) coordinates; Count is the planner position in steps = machine coordinates.
// The work offset (G92) is therefore known without the TFT tracking it: offset = work - machine.
//
// Homed state: Marlin does not report it. The TFT marks an axis homed when it sends G28 for it and
// clears all on M18 (motors off) and when the connection to Marlin is lost (reset).
//
// Jog: moves are sent as "G91 / G1 <axis><d> F<f> / G90 / M400". The next move is only sent when the
// previous one is finished (M400 answered); detents and taps during a move are added up and sent as one
// move afterwards, so the table never runs on after the knob stops.

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define CNC_AXES          3       // X Y Z
#define CNC_STEPS_DEFAULT 200.0f  // steps/mm if Marlin did not report M92 yet
#define CNC_JOG_FEED      600     // mm/min, Marlin max feed is 10 mm/s
#define CNC_FEED_MIN      10      // feed override range (%)
#define CNC_FEED_MAX      150
#define CNC_FEED_REST_MS  400     // feed override is sent when the value rests this long

typedef struct
{
  float    work[CNC_AXES];   // work (logical) position, mm
  float    mcs[CNC_AXES];    // machine position, mm
  bool     known;            // a position report was received since connecting
  bool     mcsKnown;         // the report had step counts
  bool     homed[CNC_AXES];  // homed by this TFT since the connection (see above)
  uint32_t updatedMs;        // time of the last position report
} CNC_STATE;

extern CNC_STATE cncState;

bool cncIsActive(void);                     // CNC screens in use (always in the TURTLE_CNC build)
void cncLoop(void);                         // background: connection changes, pending jog and feed
void cncParsePosition(const char * line);   // position report line ("X:.. Y:.. Z:.. Count X:.. ..")
void cncQueryPosition(uint16_t intervalMs);  // ask for the position (M114) at most every intervalMs

bool cncIdle(void);                         // nothing queued and no command waiting for its "ok"
void cncJog(uint8_t axis, float distance);  // add a relative move (mm); sent when the previous one is done
bool cncJogPending(void);                   // a jog move is waiting or running
void cncJogCancel(void);                    // drop moves not sent yet

void cncZero(bool x, bool y, bool z);       // G92: the current position becomes 0 on the given axes
void cncHome(uint8_t axis);                 // G28 X / G28 Y
void cncGotoZero(void);                     // G90 G0 X0 Y0
void cncMotorsOff(void);                    // M18

uint16_t cncGetFeed(void);                  // feed override shown on the TFT (%)
void cncSetFeed(uint16_t percent);          // change it; M220 is sent once the value rests

void cncStartCheckText(char * buf, size_t size, const char * filename);  // question before a job

#ifdef __cplusplus
}
#endif

#endif
