// Turtle FilamentMaker build (env TURTLE_FM_TFT43, define TURTLE_FM)
//
// The printer screens are not built (platformio.ini [turtle_fm]). The shared Marlin host code
// (reply parser, command handler, menu engine) still refers to them; these stand-ins keep it linking.
// A printer screen that would be opened shows the FilamentMaker status screen instead, and printer
// replies (PID / MPC tuning, mesh, leveling, case light, print start) are ignored.
// The FilamentMaker controller never sends any of them.

#ifdef TURTLE_FM

#include "includes.h"

// ---------------------------------------------------------------- screens

void menuMain(void)  // stock main menu -> FilamentMaker menu
{
  REPLACE_MENU(menuFmMenu);
}

#define FM_NO_PRINTER_MENU(name) void name(void) { REPLACE_MENU(menuFmStatus); }

FM_NO_PRINTER_MENU(menuPrint)
FM_NO_PRINTER_MENU(menuPrinting)
FM_NO_PRINTER_MENU(menuHeat)
FM_NO_PRINTER_MENU(menuUnifiedHeat)
FM_NO_PRINTER_MENU(menuMove)
FM_NO_PRINTER_MENU(menuUnifiedMove)
FM_NO_PRINTER_MENU(menuExtrude)
FM_NO_PRINTER_MENU(menuLoadUnload)
FM_NO_PRINTER_MENU(menuSpeed)
FM_NO_PRINTER_MENU(menuBabystep)
FM_NO_PRINTER_MENU(menuZOffset)
FM_NO_PRINTER_MENU(menuMBL)
FM_NO_PRINTER_MENU(menuMeshEditor)
FM_NO_PRINTER_MENU(menuPid)
FM_NO_PRINTER_MENU(menuMPC)
FM_NO_PRINTER_MENU(menuTuning)
FM_NO_PRINTER_MENU(menuTuneExtruder)
FM_NO_PRINTER_MENU(menuCaseLight)
FM_NO_PRINTER_MENU(menuLEDColor)
FM_NO_PRINTER_MENU(menuCallMacro)

// ---------------------------------------------------------------- printer replies / commands

void startPrinting(void) {}
void startPrintingFromRemoteHost(const char * filename) { (void)filename; }
void rrfShowRunningMacro(void) {}

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

#endif  // TURTLE_FM
