// Drawing primitives into the display framebuffer. Colours are RGB565.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define COL_BLACK   0x0000
#define COL_WHITE   RGB565(255, 255, 255)
#define COL_RED     RGB565(255, 0, 0)
#define COL_GREEN   RGB565(0, 255, 0)
#define COL_BLUE    RGB565(0, 0, 255)
#define COL_C2      RGB565(64, 128, 255)  // lighter blue, readable on black
#define COL_YELLOW  RGB565(255, 255, 0)
#define COL_PURPLE  RGB565(255, 0, 255)
#define COL_GRAY    RGB565(96, 96, 96)
#define COL_DKGRAY  RGB565(48, 48, 48)
#define COL_CYAN    RGB565(0, 255, 255)

#define GFX_OHM "\x7F"  // Omega glyph

#define FONT_CELL_W 6  // 5 px glyph + 1 px spacing
#define FONT_CELL_H 8

void gfx_clear(uint16_t color);
void gfx_pixel(int x, int y, uint16_t color);
void gfx_hline(int x, int y, int w, uint16_t color);
void gfx_vline(int x, int y, int h, uint16_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t color);
// Clip rectangle applied to gfx_line (used for the plot area).
void gfx_set_line_clip(int x0, int y0, int x1, int y1);

// Transparent-background text (bg == fg means transparent). Returns end x.
int gfx_text(int x, int y, const char *s, uint16_t fg, int scale);
int gfx_text_bg(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
int gfx_text_width(const char *s, int scale);
