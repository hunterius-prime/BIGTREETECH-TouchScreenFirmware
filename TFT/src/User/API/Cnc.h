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
//
// Feed move: one axis to a target (work coordinates) at a cutting feed. Marlin cannot change the feed of a
// move it has planned (M220 only affects later moves), so the TFT streams the move as G1 segments of about
// 1 s (at least 0.5 mm) and keeps only about 2 s of them in Marlin's planner; a feed change applies from the
// next segment. What Marlin still has to run is estimated from the time each segment takes (feed and the
// M220 override); acceleration only makes Marlin slower than the estimate, so the planner never runs dry.
// The end is the "ok" of M400, sent once the estimate says the last segment is done. The feed override is
// set back to 100 % when a move starts, so the feed on the screen is the real feed.
// Stop: no more segments, then M410 (Marlin needs EMERGENCY_PARSER so it acts on M410 at once), M400, M114
// for the stop position. After M410 Marlin drops new moves for about 1 s, so Resume waits CNC_FM_RESUME_MS.
// During the move the readout comes from M154 S1 (needs AUTO_REPORT_REAL_POSITION in Marlin: M114 would
// report the end of the planned segments, not where the table is).

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

#define CNC_FM_FEED_MIN   5       // feed move feed range (mm/min)
#define CNC_FM_FEED_MAX   600
#define CNC_FM_SEG_MS     1000    // feed move segment: this long at the feed ...
#define CNC_FM_SEG_MIN_MM 0.5f    // ... but at least this long
#define CNC_FM_AHEAD_MS   1000    // send the next segment when Marlin has less than this left
#define CNC_FM_RESUME_MS  1500    // Marlin drops new moves for about 1 s after M410

// travel in machine coordinates (Marlin X/Y_MIN_POS, X/Y_MAX_POS)
#define CNC_X_MIN  -150.0f
#define CNC_X_MAX   150.0f
#define CNC_Y_MIN   -65.0f
#define CNC_Y_MAX    65.0f

typedef enum
{
  CNC_FM_IDLE = 0,
  CNC_FM_MOVING,    // segments are streamed
  CNC_FM_STOPPING,  // Stop pressed: M410 is on its way
  CNC_FM_PAUSED,    // stopped; Resume (same target) or Cancel
} CNC_FM_STATE;

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

bool cncFmStart(uint8_t axis, float target, uint16_t feed);  // feed move (see above); false if not possible now
void cncFmStop(void);                       // moving -> stopping -> paused
bool cncFmResume(void);                     // paused -> moving to the same target; false within CNC_FM_RESUME_MS
void cncFmCancel(void);                     // paused -> idle, nothing is sent
void cncFmSetFeed(uint16_t feed);           // feed (mm/min) of the next segments
CNC_FM_STATE cncFmState(void);
uint8_t cncFmAxis(void);
float cncFmTarget(void);
float cncFmFrom(void);                      // where the move (or the resume) started
uint16_t cncFmFeed(void);
uint32_t cncFmPausedMs(void);               // time since the stop finished
bool cncFmTakeDone(void);                   // true once after a move reached its target

void cncStartCheckText(char * buf, size_t size, const char * filename);  // question before a job

#ifdef __cplusplus
}
#endif

#endif
