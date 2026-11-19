#ifndef _CNC_DRO_FONT_H_
#define _CNC_DRO_FONT_H_

// Large readout font of the Turtle CNC build (generated: tools/cnc/make_dro_font.ps1 -> CncDroFont.c)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CNC_DRO_FONT_MAX_GLYPHS 16

typedef struct
{
  char     ch;
  uint8_t  advance;  // cell width in pixels (glyph data width)
  uint16_t offset;   // byte offset of the glyph in data
} CNC_DRO_GLYPH;

typedef struct
{
  uint8_t             height;
  uint8_t             count;
  CNC_DRO_GLYPH       glyph[CNC_DRO_FONT_MAX_GLYPHS];
  const uint8_t *     data;  // 4-bit coverage, 2 pixels per byte (high nibble first), row-major, rows padded to even width
} CNC_DRO_FONT;

extern const CNC_DRO_FONT cncDroFont;

#ifdef __cplusplus
}
#endif

#endif
