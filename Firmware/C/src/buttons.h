// Debounced buttons and trigger switch. Call buttons_tick() every 5 ms.
#pragma once

#include <stdbool.h>

#define BUTTON_TICK_MS 5

typedef enum { BTN_LEFT, BTN_RIGHT, BTN_MODE, BTN_SELECT, BTN_COUNT } button_t;
typedef enum { SWITCH_UNKNOWN, SWITCH_READY, SWITCH_START } switch_pos_t;

void buttons_init(void);
void buttons_tick(void);

// Returns the number of pending press events for a button (and clears them).
// LEFT/RIGHT auto-repeat when held; `fast` reports that repeat has sped up.
int button_take(button_t b, bool *fast);

switch_pos_t switch_position(void);
bool button_raw(button_t b);
