// Scopocket v1.3 firmware (rewrite).
//
// Core 0: UI (buttons, menu, rendering, display DMA), waveform generator.
// Core 1: acquisition engine (see acquisition.c).

#include "acquisition.h"
#include "board.h"
#include "buttons.h"
#include "calibrate.h"
#include "calibration.h"
#include "display.h"
#include "gfx.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "menu.h"
#include "meters.h"
#include "pico/stdlib.h"
#include "scope_view.h"
#include "settings.h"
#include "usbproto.h"
#include "wavegen.h"

#define MIN_FRAME_US 20000  // cap display refresh at 50 fps

static void output(unsigned pin, bool value) {
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_put(pin, value);
    gpio_set_dir(pin, GPIO_OUT);
}

static void board_init(void) {
    buttons_init();

    // Thevenin sources off (P-FET gates high)
    output(PIN_THEV_1K, 1);
    output(PIN_THEV_10K, 1);
    output(PIN_THEV_100K, 1);
    output(PIN_THEV_1M, 1);

    // Input bias resistors to GND
    output(PIN_BIAS_22, 0);
    output(PIN_BIAS_23, 0);
    output(PIN_BIAS_24, 0);
    output(PIN_BIAS_25, 0);

    gpio_init(PIN_LOW_BATT);
    gpio_set_dir(PIN_LOW_BATT, GPIO_IN);
    gpio_disable_pulls(PIN_LOW_BATT);
}

// Engine setup used by the meters: 4-input path, ADC at 48 MHz.
static void configure_for_meters(void) {
    acq_config_t c = {.rate_hz = 100, .trig = TRIG_RISING, .cap = CAP_REPEAT};
    acq_configure(&c);
}

static enum { VIEW_MAIN, VIEW_MENU } view = VIEW_MAIN;
static bool dirty = true;
static settings_t applied;

// Pushes changed settings to the acquisition engine, meters and waveform
// generator.
static void apply_settings(settings_t *applied) {
    const settings_t *s = &settings;
    if (s->mode != applied->mode) {
        if (applied->mode != MODE_SCOPE) meters_exit(applied->mode);
        if (s->mode == MODE_SCOPE) {
            acq_config_t c;
            settings_acq_config(&c);
            acq_configure(&c);
        } else {
            // Volt/Ohm share one engine setup; Cap configures its own.
            if (s->mode != MODE_CAP) configure_for_meters();
            meters_enter(s->mode);
        }
    } else if (s->mode == MODE_SCOPE &&
               (s->rate_idx != applied->rate_idx || s->trig_mode != applied->trig_mode ||
                s->trig_level_idx != applied->trig_level_idx || s->cap_mode != applied->cap_mode)) {
        // (V/div alone only changes the display; it sets the trigger
        // hysteresis the next time the acquisition is reconfigured.)
        acq_config_t c;
        settings_acq_config(&c);
        acq_configure(&c);
    }
    if (s->wave_type != applied->wave_type || s->wave_amp_idx != applied->wave_amp_idx ||
        s->dc_level_idx != applied->dc_level_idx ||
        s->wave_freq_idx != applied->wave_freq_idx)
        settings_apply_wave();
    *applied = *s;
}

static void low_battery_screen(void) {
    display_wait();
    gfx_clear(COL_BLACK);
    gfx_text(47, 60, "LOW BATTERY", COL_RED, 1);
    display_flush();
    while (!gpio_get(PIN_LOW_BATT)) {
        usbp_service();  // keep the companion app informed
        sleep_ms(10);
    }
}

// ---------------------------------------------------------------------------
// Hooks for the USB protocol
// ---------------------------------------------------------------------------
void app_set_setting(int id, int index) {
    if (id < 0 || id >= setting_count) return;
    const setting_item_t *it = &setting_items[id];
    if (index < 0 || index >= it->count) return;
    *it->value = index;
    apply_settings(&applied);
    dirty = true;
}

bool app_menu_open(void) { return view == VIEW_MENU; }
bool app_low_battery(void) { return !gpio_get(PIN_LOW_BATT); }

int main(void) {
    set_sys_clock_khz(SYS_CLOCK_HZ / 1000, true);

    board_init();
    display_init();
    gfx_clear(COL_BLACK);
    display_flush();

    wavegen_init();
    settings_apply_wave();

    acq_config_t acfg;
    settings_acq_config(&acfg);
    acq_init(&acfg);

    sleep_ms(100);
    calibration_set_t cal;
    if (button_raw(BTN_MODE) || !calibration_load(&cal)) {
        calibrate_run(&cal);
        acq_configure(&acfg);  // calibration switched the engine around
    }
    acq_set_calibration(&cal);
    meters_set_calibration(&cal.adc48);  // the meters always run at 48 MHz

    usbp_init();
    scope_view_init();
    for (int b = 0; b < BTN_COUNT; b++) button_take((button_t)b, NULL);

    uint32_t shown_version = 0;
    absolute_time_t next_tick = get_absolute_time();
    absolute_time_t next_frame = get_absolute_time();
    int lowbatt_ticks = 0;
    switch_pos_t last_switch = SWITCH_UNKNOWN;
    applied = settings;

    for (;;) {
        usbp_service();
        if (time_reached(next_tick)) {
            next_tick = delayed_by_ms(next_tick, BUTTON_TICK_MS);
            if (time_reached(next_tick)) next_tick = make_timeout_time_ms(BUTTON_TICK_MS);
            buttons_tick();

            // Trigger switch: READY -> START fires the trigger in Switch mode.
            switch_pos_t sw = switch_position();
            if (sw == SWITCH_START && last_switch == SWITCH_READY) acq_switch_trigger();
            last_switch = sw;

            if (view == VIEW_MAIN) {
                if (button_take(BTN_MODE, NULL)) {
                    view = VIEW_MENU;
                    menu_open();
                    dirty = true;
                } else if (settings.mode == MODE_SCOPE) {
                    if (scope_view_update()) dirty = true;
                } else if (meters_update(settings.mode)) {
                    dirty = true;
                }
            } else {
                bool open = menu_update();
                dirty = true;
                if (!open) {
                    view = VIEW_MAIN;
                    apply_settings(&applied);
                }
            }

            lowbatt_ticks = gpio_get(PIN_LOW_BATT) ? 0 : lowbatt_ticks + 1;
            if (lowbatt_ticks >= 5) {
                lowbatt_ticks = 0;
                low_battery_screen();
                dirty = true;
            }
        }

        if (view == VIEW_MAIN && settings.mode == MODE_SCOPE && acq_version() != shown_version)
            dirty = true;

        if (dirty && time_reached(next_frame) && !display_busy()) {
            dirty = false;
            next_frame = make_timeout_time_us(MIN_FRAME_US);
            if (view == VIEW_MENU) {
                menu_render();
            } else if (settings.mode == MODE_SCOPE) {
                shown_version = acq_version();
                scope_view_render();
            } else {
                meters_render(settings.mode);
            }
            display_flush();
        }
    }
}
