#ifndef _FM_MENU_H_
#define _FM_MENU_H_

// Turtle FilamentMaker screens (see API/FilamentMaker.h)

#ifdef __cplusplus
extern "C" {
#endif

void menuFmStatus(void);    // home screen: temperatures, speed, buffer, counters, process buttons
void menuFmMenu(void);      // main menu
void menuFmMore(void);
void menuFmSettings(void);  // settings groups
void menuFmGroup(void);     // settings of one group (list + numpad)
void menuFmInfo(void);      // controller / worker / network info

#ifdef __cplusplus
}
#endif

#endif
