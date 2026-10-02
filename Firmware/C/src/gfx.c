#include "gfx.h"

#include <string.h>

#include "display.h"
#include "font5x7.h"

static inline uint16_t swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static int clip_x0 = 0, clip_y0 = 0, clip_x1 = DISP_W - 1, clip_y1 = DISP_H - 1;

void gfx_clear(uint16_t color) {
    uint16_t c = swap(color);
    if ((c & 0xFF) == (c >> 8)) {
        memset(display_fb, c & 0xFF, sizeof display_fb);
        return;
    }
    uint16_t *p = &display_fb[0][0];
    for (int i = 0; i < DISP_W * DISP_H; i++) p[i] = c;
}

void gfx_pixel(int x, int y, uint16_t color) {
    if ((unsigned)x < DISP_W && (unsigned)y < DISP_H) display_fb[y][x] = swap(color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > DISP_W) x1 = DISP_W;
    if (y1 > DISP_H) y1 = DISP_H;
    uint16_t c = swap(color);
    for (int yy = y; yy < y1; yy++)
        for (int xx = x; xx < x1; xx++) display_fb[yy][xx] = c;
}

void gfx_hline(int x, int y, int w, uint16_t color) { gfx_fill_rect(x, y, w, 1, color); }
void gfx_vline(int x, int y, int h, uint16_t color) { gfx_fill_rect(x, y, 1, h, color); }

void gfx_set_line_clip(int x0, int y0, int x1, int y1) {
    clip_x0 = x0;
    clip_y0 = y0;
    clip_x1 = x1;
    clip_y1 = y1;
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t color) {
    uint16_t c = swap(color);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;  // negative
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= clip_x0 && x0 <= clip_x1 && y0 >= clip_y0 && y0 <= clip_y1)
            display_fb[y0][x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_char(int x, int y, char ch, uint16_t fg, uint16_t bg, int scale, bool opaque) {
    unsigned char uc = (unsigned char)ch;
    if (uc < FONT_FIRST || uc > FONT_LAST) uc = '?';
    const uint8_t *g = &font5x7[(uc - FONT_FIRST) * FONT_W];
    for (int col = 0; col < FONT_CELL_W; col++) {
        uint8_t bits = col < FONT_W ? g[col] : 0;
        for (int row = 0; row < FONT_CELL_H; row++) {
            bool on = (bits >> row) & 1;
            if (on || opaque)
                gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, on ? fg : bg);
        }
    }
}

static int text_impl(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale, bool opaque) {
    int x0 = x;
    for (; *s; s++) {
        if (*s == '\n') {
            x = x0;
            y += (FONT_CELL_H + 1) * scale;
            continue;
        }
        draw_char(x, y, *s, fg, bg, scale, opaque);
        x += FONT_CELL_W * scale;
    }
    return x;
}

int gfx_text(int x, int y, const char *s, uint16_t fg, int scale) {
    return text_impl(x, y, s, fg, fg, scale, false);
}

int gfx_text_bg(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    return text_impl(x, y, s, fg, bg, scale, true);
}

int gfx_text_width(const char *s, int scale) { return (int)strlen(s) * FONT_CELL_W * scale; }
