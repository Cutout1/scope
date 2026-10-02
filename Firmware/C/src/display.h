// ST7735 160x128 display with a RAM framebuffer flushed over DMA.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISP_W 160
#define DISP_H 128

// Framebuffer, row-major. Pixels are stored byte-swapped (big-endian RGB565)
// so the DMA can stream bytes straight to the panel; use gfx.h to draw.
extern uint16_t display_fb[DISP_H][DISP_W];

void display_init(void);

// Starts sending the framebuffer. Returns immediately; the framebuffer must
// not be modified until display_wait() returns.
void display_flush(void);
void display_wait(void);
bool display_busy(void);
