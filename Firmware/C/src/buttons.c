#include "buttons.h"

#include "board.h"
#include "hardware/gpio.h"

#define PRESS_TICKS 5         // 25 ms debounce
#define REPEAT_DELAY_TICKS 100  // 500 ms before auto-repeat
#define REPEAT_TICKS 5          // then every 25 ms

static const unsigned pins[BTN_COUNT] = {PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_MODE, PIN_BTN_SELECT};
static const bool repeats[BTN_COUNT] = {true, true, false, false};

static int held_ticks[BTN_COUNT];
static int pending[BTN_COUNT];
static bool pending_fast[BTN_COUNT];

static int ready_ticks, start_ticks;
static switch_pos_t sw = SWITCH_UNKNOWN;

static void input_pullup(unsigned pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void buttons_init(void) {
    for (int i = 0; i < BTN_COUNT; i++) input_pullup(pins[i]);
    input_pullup(PIN_SWITCH_READY);
    input_pullup(PIN_SWITCH_START);
}

bool button_raw(button_t b) { return !gpio_get(pins[b]); }

void buttons_tick(void) {
    for (int i = 0; i < BTN_COUNT; i++) {
        if (!button_raw((button_t)i)) {
            held_ticks[i] = 0;
            continue;
        }
        int t = ++held_ticks[i];
        if (t == PRESS_TICKS) {
            pending[i]++;
            pending_fast[i] = false;
        } else if (repeats[i] && t >= REPEAT_DELAY_TICKS) {
            held_ticks[i] = REPEAT_DELAY_TICKS - REPEAT_TICKS;
            pending[i]++;
            pending_fast[i] = true;
        }
    }

    ready_ticks = gpio_get(PIN_SWITCH_READY) ? 0 : ready_ticks + 1;
    start_ticks = gpio_get(PIN_SWITCH_START) ? 0 : start_ticks + 1;
    if (ready_ticks > PRESS_TICKS) sw = SWITCH_READY;
    if (start_ticks > PRESS_TICKS) sw = SWITCH_START;
}

int button_take(button_t b, bool *fast) {
    int n = pending[b];
    pending[b] = 0;
    if (fast) *fast = pending_fast[b];
    return n;
}

switch_pos_t switch_position(void) { return sw; }
