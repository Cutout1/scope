#include "calibrate.h"

#include "acquisition.h"
#include "board.h"
#include "buttons.h"
#include "display.h"
#include "gfx.h"
#include "pico/stdlib.h"

static void show(const char *msg) {
    display_wait();
    gfx_clear(COL_BLACK);
    gfx_text(2, 2, msg, COL_WHITE, 1);
    display_flush();
    display_wait();
}

static void wait_select(void) {
    int n = 0;
    while (n < 5) {
        n = button_raw(BTN_SELECT) ? n + 1 : 0;
        sleep_ms(10);
    }
    n = 0;
    while (n < 5) {
        n = button_raw(BTN_SELECT) ? 0 : n + 1;
        sleep_ms(10);
    }
}

// Engine settings that put the ADC on each clock with all four inputs.
static const acq_config_t cfg48 = {.rate_hz = 100, .trig = TRIG_RISING, .cap = CAP_REPEAT};
static const acq_config_t cfg192 = {.rate_hz = 500000, .trig = TRIG_RISING, .cap = CAP_REPEAT};

// Measures all four inputs at both ADC clocks. out[0] = 48 MHz, out[1] = 192 MHz.
static void measure_both(float out[2][4]) {
    show("Measuring...");
    acq_configure(&cfg48);
    sleep_ms(20);
    acq_raw_average(out[0], 200);
    acq_configure(&cfg192);
    sleep_ms(20);
    acq_raw_average(out[1], 200);
}

static void store(calibration_t *c, const float v[4], bool full) {
    if (full) {
        c->c1ref_3v3 = v[ADC_CH_C1REF];
        c->c1_3v3 = v[ADC_CH_C1];
        c->c2ref_3v3 = v[ADC_CH_C2REF];
        c->c2_3v3 = v[ADC_CH_C2];
    } else {
        c->c1ref_0 = v[ADC_CH_C1REF];
        c->c1_0 = v[ADC_CH_C1];
        c->c2ref_0 = v[ADC_CH_C2REF];
        c->c2_0 = v[ADC_CH_C2];
    }
}

static bool valid(const calibration_t *c) {
    const float zero[4] = {c->c1ref_0, c->c1_0, c->c2ref_0, c->c2_0};
    const float full[4] = {c->c1ref_3v3, c->c1_3v3, c->c2ref_3v3, c->c2_3v3};
    for (int i = 0; i < 4; i++)
        if (zero[i] < 25000 || zero[i] > 40000 || full[i] < 55000) return false;
    return true;
}

void calibrate_run(calibration_set_t *cal) {
    for (;;) {
        float v[2][4];
        show("GND Calibration: Connect\nC1-, C1+, C2-, and C2+\ndirectly to GND then press\nthe Select button.");
        wait_select();
        measure_both(v);
        store(&cal->adc48, v[0], false);
        store(&cal->adc192, v[1], false);

        show("3.3V Calibration: Connect\nC1-, C1+, C2-, and C2+\ndirectly to +3.3V then\npress the Select button.");
        wait_select();
        measure_both(v);
        store(&cal->adc48, v[0], true);
        store(&cal->adc192, v[1], true);

        if (valid(&cal->adc48) && valid(&cal->adc192)) break;
        show("Calibration failed. Please\ntry again.");
        sleep_ms(3000);
    }

    // Core 1 runs from RAM while parked, so flash can be written safely.
    acq_pause();
    calibration_save(cal);
    acq_resume();
}
