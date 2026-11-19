// Turtle CNC build (env TURTLE_CNC_TFT43, define TURTLE_CNC)
//
// The printer screens are not built (platformio.ini [turtle_cnc]). The shared Marlin host code
// (reply parser, command handler, menu engine) still refers to them; these stand-ins keep it linking.
// A printer screen that would be opened shows the CNC status screen instead, and printer replies
// (PID / MPC tuning, mesh, leveling, case light) are ignored. The mill never sends any of them.
// The file browser (Print.c), terminal, settings and Marlin mode are the stock ones.
// menuMain, menuPrinting, menuPrintRestore and startPrinting are in CncMenu.c.

#ifdef TURTLE_CNC

#include "includes.h"

// ---------------------------------------------------------------- screens

#define CNC_NO_PRINTER_MENU(name) void name(void) { REPLACE_MENU(menuCncStatus); }

CNC_NO_PRINTER_MENU(menuHeat)
CNC_NO_PRINTER_MENU(menuUnifiedHeat)
CNC_NO_PRINTER_MENU(menuMove)
CNC_NO_PRINTER_MENU(menuUnifiedMove)
CNC_NO_PRINTER_MENU(menuExtrude)
CNC_NO_PRINTER_MENU(menuLoadUnload)
CNC_NO_PRINTER_MENU(menuSpeed)
CNC_NO_PRINTER_MENU(menuBabystep)
CNC_NO_PRINTER_MENU(menuZOffset)
CNC_NO_PRINTER_MENU(menuMBL)
CNC_NO_PRINTER_MENU(menuMeshEditor)
CNC_NO_PRINTER_MENU(menuPid)
CNC_NO_PRINTER_MENU(menuMPC)
CNC_NO_PRINTER_MENU(menuTuning)
CNC_NO_PRINTER_MENU(menuTuneExtruder)
CNC_NO_PRINTER_MENU(menuCaseLight)
CNC_NO_PRINTER_MENU(menuLEDColor)
CNC_NO_PRINTER_MENU(menuCallMacro)

// ---------------------------------------------------------------- printer replies / commands

void rrfShowRunningMacro(void) {}
void printSummaryPopup(void) {}  // "previous print data" (printer statistics) is hidden in the CNC file browser

void heatSetCurrentIndex(uint8_t index) { (void)index; }
void pidUpdateStatus(PID_STATUS status) { (void)status; }

MPC_STATUS getMpcTuningStatus(void) { return DONE; }  // never tuning
void setMpcTuningStatus(MPC_STATUS status) { (void)status; }
void setMpcTuningResult(MPC_RESULT result) { (void)result; }
void setMpcHeaterPower(uint8_t index, uint8_t power) { (void)index; (void)power; }
void setMpcFilHeatCapacity(uint8_t index, float capacity) { (void)index; (void)capacity; }

void ablUpdateStatus(bool succeeded) { (void)succeeded; }
void mblUpdateStatus(bool succeeded) { (void)succeeded; }
bool meshIsWaitingData(void) { return false; }
void meshUpdateData(char * dataRow) { (void)dataRow; }
void setHSmode(BLT_HS_MODE hsMode) { (void)hsMode; }

void caseLightSetState(bool state) { (void)state; }
void caseLightSetPercent(uint8_t brightness) { (void)brightness; }

#endif  // TURTLE_CNC
