#include "meters.h"

#include <math.h>
#include <stdio.h>

#include "acquisition.h"
#include "board.h"
#include "capmeter.h"
#include "buttons.h"
#include "gfx.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Both meters average through the acquisition engine with the ADC at its
// rated 48 MHz (all four inputs, 125k readings/s each). 100 ms windows are a whole number of 50 Hz and 60 Hz cycles, so
// mains hum averages out.
#define WINDOW_MS 100

// Raw readings this close to the ends of the ADC range mean the analog
// front end is saturated.
#define RAW_LOW_LIMIT 600.0f
#define RAW_HIGH_LIMIT 65000.0f

static calibration_t cal;
static bool remote_reset, remote_zero;  // pending actions from the companion app

static bool take(bool *flag) {
    bool v = *flag;
    *flag = false;
    return v;
}
static float k_c1, k_c1ref, k_c2, k_c2ref;  // volts per read_u16 count

void meters_set_calibration(const calibration_t *c) {
    cal = *c;
    k_c1 = 3.3f / (c->c1_3v3 - c->c1_0);
    k_c1ref = 3.3f / (c->c1ref_3v3 - c->c1ref_0);
    k_c2 = 3.3f / (c->c2_3v3 - c->c2_0);
    k_c2ref = 3.3f / (c->c2ref_3v3 - c->c2ref_0);
}

float meters_c1_single_ended(const float u[4]) { return (u[ADC_CH_C1] - cal.c1_0) * k_c1; }

static bool over_range(const float u[4]) {
    for (int i = 0; i < 4; i++)
        if (u[i] < RAW_LOW_LIMIT || u[i] > RAW_HIGH_LIMIT) return true;
    return false;
}

static void drain_pending(bool *pending) {
    float u[4];
    while (*pending && !acq_raw_average_poll(u)) tight_loop_contents();
    *pending = false;
}

// ===========================================================================
// Voltmeter
// ===========================================================================
static struct {
    bool pending;
    int nwin;          // windows received
    float win[2][2];   // [window][channel] volts, for a 200 ms sliding average
    bool over[2];
    float value[2];
    float vmin[2], vmax[2];
    bool have_minmax;
} vm;

static void volt_reset_minmax(void) { vm.have_minmax = false; }

static void volt_enter(void) {
    vm.pending = false;
    vm.nwin = 0;
    volt_reset_minmax();
}

static bool volt_update(void) {
    bool dirty = false;
    if ((button_take(BTN_SELECT, NULL) | take(&remote_reset)) != 0) {
        volt_reset_minmax();
        dirty = true;
    }
    if (!vm.pending) {
        acq_raw_average_start(WINDOW_MS);
        vm.pending = true;
    }
    float u[4];
    if (!acq_raw_average_poll(u)) return dirty;
    vm.pending = false;

    float c1 = (u[ADC_CH_C1] - cal.c1_0) * k_c1 - (u[ADC_CH_C1REF] - cal.c1ref_0) * k_c1ref;
    float c2 = (u[ADC_CH_C2] - cal.c2_0) * k_c2 - (u[ADC_CH_C2REF] - cal.c2ref_0) * k_c2ref;
    bool over1 = over_range((const float[4]){u[ADC_CH_C1], u[ADC_CH_C1REF], 30000, 30000});
    bool over2 = over_range((const float[4]){u[ADC_CH_C2], u[ADC_CH_C2REF], 30000, 30000});

    int w = vm.nwin & 1;
    vm.win[w][0] = c1;
    vm.win[w][1] = c2;
    vm.nwin++;
    int n = vm.nwin >= 2 ? 2 : 1;
    for (int ch = 0; ch < 2; ch++) {
        float sum = 0;
        for (int i = 0; i < n; i++) sum += vm.win[i][ch];
        vm.value[ch] = sum / (float)n;
    }
    vm.over[0] = over1;
    vm.over[1] = over2;

    if (vm.nwin >= 2) {  // skip the first (possibly settling) window
        for (int ch = 0; ch < 2; ch++) {
            float v = vm.value[ch];
            if (!vm.have_minmax || v < vm.vmin[ch]) vm.vmin[ch] = v;
            if (!vm.have_minmax || v > vm.vmax[ch]) vm.vmax[ch] = v;
        }
        vm.have_minmax = true;
    }
    return true;
}

static void volt_render(void) {
    char buf[32];
    gfx_clear(COL_BLACK);
    gfx_text(2, 1, "VOLTMETER", COL_YELLOW, 1);
    gfx_text(160 - gfx_text_width("200ms avg", 1) - 1, 1, "200ms avg", COL_GRAY, 1);
    gfx_hline(0, 10, 160, COL_DKGRAY);

    static const char *const names[2] = {"C1  (C1+ - C1-)", "C2  (C2+ - C2-)"};
    static const uint16_t colors[2] = {COL_RED, COL_C2};
    for (int ch = 0; ch < 2; ch++) {
        int y = 14 + ch * 52;
        gfx_text(2, y, names[ch], colors[ch], 1);
        if (vm.nwin == 0) {
            gfx_text(8, y + 11, "  ...", COL_GRAY, 3);
        } else {
            snprintf(buf, sizeof buf, "%+.3fV", (double)vm.value[ch]);
            gfx_text(8, y + 11, buf, vm.over[ch] ? COL_RED : COL_WHITE, 3);
        }
        if (vm.over[ch]) {
            gfx_text(8, y + 37, "OVER RANGE", COL_RED, 1);
        } else if (vm.have_minmax) {
            snprintf(buf, sizeof buf, "min%+.3f max%+.3f", (double)vm.vmin[ch], (double)vm.vmax[ch]);
            gfx_text(8, y + 37, buf, COL_GRAY, 1);
        }
    }
    gfx_text(2, 120, "Select:Reset min/max", COL_GRAY, 1);
}

// ===========================================================================
// Ohmmeter
//
// One of four P-FET switched sources (3.3 V through 1k/10k/100k/1M) drives
// C1+; the unknown resistor to GND forms a divider:
//   R = Rs * V / (3.3 - V)
// Calibration defines 3.3 V as the board's rail, so this is ratiometric.
// The range with Rs closest to R gives the best resolution; only the active
// range is measured, and it moves when R leaves [Rs/4, 4*Rs].
// ===========================================================================
static const float ohm_rs[4] = {1e3f, 1e4f, 1e5f, 1e6f};
static const unsigned ohm_pins[4] = {PIN_THEV_1K, PIN_THEV_10K, PIN_THEV_100K, PIN_THEV_1M};
static const char *const ohm_range_names[4] = {"1k", "10k", "100k", "1M"};

#define OHM_OPEN_LIMIT 25e6f       // above this on the 1M range: open circuit
#define OHM_RANGE_MARGIN 4.0f
#define OHM_CONTINUITY 10.0f       // ohms
#define OHM_SMOOTH 4               // readings averaged while stable
#define OHM_STABLE_TOL 0.02f       // a jump larger than this restarts the average

static struct {
    int range;
    bool settling;
    absolute_time_t settle_until;
    bool pending;
    bool have;      // a reading is available
    bool open;
    float r;        // displayed (smoothed) resistance
    float hist[OHM_SMOOTH];
    int nhist;
    bool rel;
    float rel_offset;
} om;

static void ohm_sources_off(void) {
    for (int i = 0; i < 4; i++) gpio_put(ohm_pins[i], 1);
}

static void ohm_set_range(int r) {
    ohm_sources_off();
    om.range = r;
    gpio_put(ohm_pins[r], 0);
    om.settling = true;
    om.settle_until = make_timeout_time_ms(r == 3 ? 20 : 10);
    om.nhist = 0;
}

static void ohm_enter(void) {
    // Float the 10 MOhm bias return on C1+ so it does not load the divider.
    gpio_set_dir(PIN_BIAS_24, GPIO_IN);
    gpio_disable_pulls(PIN_BIAS_24);
    om.pending = false;
    om.have = false;
    om.open = false;
    om.rel = false;
    ohm_set_range(3);  // start on 1M: least current, detects open circuit
}

static void ohm_exit(void) {
    drain_pending(&om.pending);
    ohm_sources_off();
    gpio_put(PIN_BIAS_24, 0);
    gpio_set_dir(PIN_BIAS_24, GPIO_OUT);
}

static int best_range(float r) {
    if (!(r > 0)) return 0;
    int b = (int)lroundf(log10f(r / 1e3f));
    return b < 0 ? 0 : b > 3 ? 3 : b;
}

static bool ohm_update(void) {
    bool dirty = false;
    if ((button_take(BTN_SELECT, NULL) | take(&remote_zero)) != 0) {
        // REL: null out lead resistance (toggle).
        if (om.rel) om.rel = false;
        else if (om.have && !om.open) {
            om.rel = true;
            om.rel_offset = om.r;
        }
        dirty = true;
    }

    if (om.settling) {
        if (!time_reached(om.settle_until)) return dirty;
        om.settling = false;
    }
    if (!om.pending) {
        acq_raw_average_start(WINDOW_MS);
        om.pending = true;
    }
    float u[4];
    if (!acq_raw_average_poll(u)) return dirty;
    om.pending = false;

    // C1+ measured single-ended (relative to GND), as in the Simple firmware.
    float v = (u[ADC_CH_C1] - cal.c1_0) * k_c1;
    float rs = ohm_rs[om.range];
    float r = v >= 3.299f ? INFINITY : rs * v / (3.3f - v);
    if (r < 0) r = 0;

    if (om.range == 3 && r > OHM_OPEN_LIMIT) {
        om.open = true;
        om.have = true;
        om.nhist = 0;
        return true;
    }

    // Autorange: move when outside [Rs/4, 4*Rs], jumping straight to the
    // range closest to the estimate.
    if ((r < rs / OHM_RANGE_MARGIN && om.range > 0) || (r > rs * OHM_RANGE_MARGIN && om.range < 3)) {
        int b = best_range(r);
        if (b == om.range) b += r > rs ? 1 : -1;
        ohm_set_range(b);
        return dirty;
    }

    // Smooth while the reading is stable; restart on a real change.
    if (om.nhist > 0 && fabsf(r - om.hist[(om.nhist - 1) % OHM_SMOOTH]) >
                            OHM_STABLE_TOL * fmaxf(r, 1.0f))
        om.nhist = 0;
    om.hist[om.nhist % OHM_SMOOTH] = r;
    om.nhist++;
    int n = om.nhist < OHM_SMOOTH ? om.nhist : OHM_SMOOTH;
    float sum = 0;
    for (int i = 0; i < n; i++) sum += om.hist[i];
    om.r = sum / (float)n;
    om.open = false;
    om.have = true;
    return true;
}

// 4 significant digits with an SI prefix: "4.702k", "123.4", "1.000M"
static void fmt_ohms(char *buf, int len, float r) {
    const char *pfx = "";
    float a = fabsf(r);
    if (a >= 1e6f) {
        r /= 1e6f;
        pfx = "M";
    } else if (a >= 1e3f) {
        r /= 1e3f;
        pfx = "k";
    }
    a = fabsf(r);
    int dec = a < 10.0f ? 3 : a < 100.0f ? 2 : 1;
    snprintf(buf, len, "%.*f%s" GFX_OHM, dec, (double)r, pfx);
}

static void ohm_render(void) {
    char buf[32];
    gfx_clear(COL_BLACK);
    gfx_text(2, 1, "OHMMETER", COL_YELLOW, 1);
    snprintf(buf, sizeof buf, "range %s", ohm_range_names[om.range]);
    gfx_text(160 - gfx_text_width(buf, 1) - 1, 1, buf, COL_GRAY, 1);
    gfx_hline(0, 10, 160, COL_DKGRAY);

    float r = om.r - (om.rel ? om.rel_offset : 0.0f);
    if (!om.have) {
        gfx_text(8, 30, "  ...", COL_GRAY, 3);
    } else if (om.open) {
        gfx_text(44, 30, "OPEN", COL_RED, 3);
    } else {
        fmt_ohms(buf, sizeof buf, r);
        int w = gfx_text_width(buf, 3);
        gfx_text((160 - w) / 2, 30, buf, COL_WHITE, 3);
    }

    if (om.rel) {
        fmt_ohms(buf, sizeof buf, om.rel_offset);
        char line[48];
        snprintf(line, sizeof line, "Zero -%s", buf);
        gfx_text(8, 60, line, COL_CYAN, 1);
    }

    if (om.have && !om.open && r < OHM_CONTINUITY) {
        gfx_fill_rect(20, 74, 120, 14, COL_GREEN);
        gfx_text(50, 77, "CONTINUITY", COL_BLACK, 1);
    }

    gfx_text(2, 100, "R between C1+ and GND", COL_GRAY, 1);
    gfx_text(2, 120, om.rel ? "Select:Zero off" : "Select:Zero leads", COL_GRAY, 1);
}

// ===========================================================================

void meters_enter(int mode) {
    if (mode == METER_VOLT) volt_enter();
    else if (mode == METER_OHM) ohm_enter();
    else capmeter_enter();
}

void meters_exit(int mode) {
    if (mode == METER_VOLT) drain_pending(&vm.pending);
    else if (mode == METER_OHM) ohm_exit();
    else capmeter_exit();
}

bool meters_update(int mode) {
    if (mode == METER_VOLT) return volt_update();
    if (mode == METER_OHM) return ohm_update();
    return capmeter_update();
}

void meters_render(int mode) {
    if (mode == METER_VOLT) volt_render();
    else if (mode == METER_OHM) ohm_render();
    else capmeter_render();
}

// ===========================================================================
// Remote control / reports (companion app)
// ===========================================================================
void meters_remote(int mode, int action) {
    if (mode == METER_VOLT && action == METER_ACT_RESET_MINMAX) remote_reset = true;
    else if (mode == METER_OHM && action == METER_ACT_ZERO) remote_zero = true;
    else if (mode == METER_CAP) capmeter_remote(action);
}

int meters_report(int mode, uint8_t *buf, int max) {
    if (max < 40) return 0;
    uint8_t *p = buf;
    *p++ = (uint8_t)mode;
    if (mode == METER_VOLT) {
        *p++ = (vm.nwin > 0) | vm.over[0] << 1 | vm.over[1] << 2 | vm.have_minmax << 3;
        p = put_f32(p, vm.value[0]);
        p = put_f32(p, vm.value[1]);
        p = put_f32(p, vm.vmin[0]);
        p = put_f32(p, vm.vmax[0]);
        p = put_f32(p, vm.vmin[1]);
        p = put_f32(p, vm.vmax[1]);
    } else if (mode == METER_OHM) {
        *p++ = om.have | om.open << 1 | om.rel << 2;
        *p++ = (uint8_t)om.range;
        p = put_f32(p, om.r);
        p = put_f32(p, om.rel_offset);
    } else {
        return capmeter_report(buf, max);
    }
    return (int)(p - buf);
}
