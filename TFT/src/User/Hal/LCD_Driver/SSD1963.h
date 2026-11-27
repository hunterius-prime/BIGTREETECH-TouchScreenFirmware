#ifndef _SSD1963_H_
#define _SSD1963_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t LCD_DriveIsSSD1963(void);
void SSD1963_Init_Sequential(void);
bool SSD1963_ConfigOk(void);  // false: the controller lost its configuration (static discharge, reset)
void SSD1963_Reinit(void);    // init again while running (PLL back to crystal first)
void SSD1963_LastReadback(uint16_t * m, uint16_t * pixelIf, uint16_t * hdp);  // values of the last ConfigOk()
void SSD1963_SetDirection(uint8_t rotate);
void SSD1963_SetWindow(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey);
uint32_t SSD1963_ReadPixel_24Bit(int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif

#endif
