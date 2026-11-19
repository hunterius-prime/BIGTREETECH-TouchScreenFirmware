#ifndef _CNC_MENU_H_
#define _CNC_MENU_H_

// Turtle CNC screens (env TURTLE_CNC_TFT43). Design: Turtle.CNC.Mill/tft/design/rcnc-tft43-screens.html

#ifdef __cplusplus
extern "C" {
#endif

void menuCncStatus(void);  // home screen: X Y Z readout, Jog / Zero XY / Files / Menu (job: Pause / Stop / Feed / Menu)
void menuCncJog(void);     // jog by touch, knob = handwheel
void menuCncZero(void);    // work zero, go to zero, home X / Y
void menuCncMenu(void);    // main menu
void menuCncFeed(void);    // feed override

#ifdef __cplusplus
}
#endif

#endif
