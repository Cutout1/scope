// Capacitance meter (charge-curve method).
//
// The capacitor (between C1+ and GND) is charged from 3.3 V through one of
// the Thevenin sources (1M, 100k, 10k, 1k). While it charges, captures are
// taken and fitted to
//
//   V(t) = Vinf - A * exp(-t/tau)
//
// which doesn't depend on the starting voltage. Any resistive leakage to GND (the 10M bias network, the input
// protection, the capacitor's own leakage) lowers Vinf and the effective
// source resistance together:
//
//   R_eff = Rs || R_leak = Rs * Vinf / 3.3      C = tau / R_eff
//
// so leakage is compensated automatically. tau comes from a three-parameter
// exponential fit (golden-section search on tau, linear fit inside), seeded by
// the rise ratio of the two halves of the window.
//
// The search starts with the 1M
// source at 50 kS/s and moves to slower sample rates, then smaller source
// resistors, while the curve is too slow to fit. The source stays on
// throughout, so every capture continues the same charge.
//
// A measurement needs a discharged capacitor (the board can't discharge it),
// so it runs when SELECT is pressed.

#include "capmeter.h"

#include <math.h>
#include <stdio.h>

#include "acquisition.h"
#include "board.h"
#include "buttons.h"
#include "gfx.h"
#include "hardware/gpio.h"
#include "meters.h"
#include "pico/stdlib.h"
#include "settings.h"

#define V_SRC 3.3f
#define V_START_MAX 0.5f      // start nearly discharged: more of the curve to fit
#define MIN_RISE 0.1f         // a fit needs at least this much rise in the window
#define MAX_RATE_STEP 2       // slow down at most this many rate steps per capture
#define TAU_MAX_WINDOWS 1.5f  // slower than this: use a slower rate
#define TAU_TARGET_FRAC 3.0f  // aim for ~3 time constants per window
#define MAX_CAPTURES 24
#define MIN_DISPLAY_F 0.1e-9f  // smaller results are shown as "<0.1nF"

static const uint32_t rates[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
#define NRATES ((int)(sizeof rates / sizeof rates[0]))
static const float src_r[4] = {1e3f, 1e4f, 1e5f, 1e6f};
static const unsigned src_pin[4] = {PIN_THEV_1K, PIN_THEV_10K, PIN_THEV_100K, PIN_THEV_1M};

typedef enum { CM_IDLE, CM_PENDING, CM_CAPTURE, CM_RESULT, CM_ERROR } cm_state_t;
typedef enum { FIT_OK, FIT_TOO_SLOW, FIT_TOO_FAST } fit_t;

static struct {
    cm_state_t state;
    // live C1+ reading while idle
    bool live_pending;
    bool have_live;
    float live_v;
    // search
    int src;
    int rate_idx;
    int captures;
    uint32_t last_seq;
    absolute_time_t timeout;
    // result
    float c;
    const char *error;
    bool rel;
    float rel_offset;
    bool have_result;
    bool last_was_error;  // what the screen shows: last result or last error
    // last capture, for the plot
    float curve[ACQ_POINTS];
    int ncurve;
} cm;

static bool remote_measure, remote_zero;  // pending actions from the companion app

static void sources_off(void) {
    for (int i = 0; i < 4; i++) gpio_put(src_pin[i], 1);
}

static void configure(void) {
    acq_config_t c = {
        .rate_hz = rates[cm.rate_idx],
        .trig = TRIG_SWITCH,
        .cap = CAP_SINGLE,
        .c1_single_ended = true,
    };
    acq_configure(&c);
}

static void drain_live(void) {
    float u[4];
    while (cm.live_pending && !acq_raw_average_poll(u)) tight_loop_contents();
    cm.live_pending = false;
}

void capmeter_enter(void) {
    sources_off();
    cm.state = CM_IDLE;
    cm.live_pending = false;
    cm.have_live = false;
    cm.have_result = false;
    cm.last_was_error = false;
    cm.rel = false;
    cm.ncurve = 0;
    cm.rate_idx = NRATES - 1;
    configure();
}

void capmeter_exit(void) {
    sources_off();
    drain_live();
}

// Arms a single capture. The engine fires as soon as its pre-trigger history
// is full; with the source on, the capture shows the ongoing charge.
static void capture(void) {
    const acq_frame_t *f = acq_acquire_frame();
    cm.last_seq = f ? f->seq : 0;
    acq_release_frame();
    if (acq_state() == ACQ_STOPPED) acq_rearm();
    acq_switch_trigger();
    cm.timeout = make_timeout_time_ms(1000u * ACQ_POINTS / rates[cm.rate_idx] + 1000);
    cm.captures++;
}

static void set_source(int s) {
    sources_off();
    cm.src = s;
    gpio_put(src_pin[s], 0);
}

static void finish(void) { sources_off(); }

static void finish_error(const char *msg) {
    cm.error = msg;
    cm.state = CM_ERROR;
    cm.last_was_error = true;
    finish();
}

static void start_measurement(void) {
    drain_live();  // captures and live readings share the engine
    cm.captures = 0;
    cm.rate_idx = NRATES - 1;  // 50 kS/s
    configure();
    set_source(3);  // 1M: least current, fastest for small C
    cm.state = CM_CAPTURE;
    capture();
}

// Least squares fit of V = a + b*exp(-i/tau) for a fixed tau. Returns the
// sum of squared residuals and the fitted a (= Vinf).
static float model_fit(const float *v, int m, float tau, float *a_out) {
    float se = 0, sv = 0, see = 0, sev = 0;
    float k = expf(-1.0f / tau), e = 1.0f;
    for (int i = 0; i < m; i++, e *= k) {
        se += e;
        sv += v[i];
        see += e * e;
        sev += e * v[i];
    }
    float den = m * see - se * se;
    if (!(den > 0)) return INFINITY;
    float b = (m * sev - se * sv) / den;
    float a = (sv - b * se) / m;
    float sse = 0;
    e = 1.0f;
    for (int i = 0; i < m; i++, e *= k) {
        float r = v[i] - a - b * e;
        sse += r * r;
    }
    if (a_out) *a_out = a;
    return sse;
}

// Fits the post-trigger part of a capture. tau_s is in samples.
static fit_t fit(const float *v, int m, float *tau_s, float *vinf) {
    // Rough tau from the rise over the first and second halves (averaged
    // end points, so it tolerates noise): r2/r1 = exp(-W / (2 tau)).
    const int h = 4;
    float s0 = 0, sm = 0, s1 = 0;
    for (int i = 0; i < h; i++) {
        s0 += v[i];
        sm += v[m / 2 - h / 2 + i];
        s1 += v[m - h + i];
    }
    s0 /= h;
    sm /= h;
    s1 /= h;
    float w = (float)(m - h);
    float r1 = sm - s0, r2 = s1 - sm, total = s1 - s0;
    *tau_s = INFINITY;

    if (total < MIN_RISE) {
        // Barely moving: too slow, unless it has already charged.
        return (V_SRC - s1) > 0.3f ? FIT_TOO_SLOW : FIT_TOO_FAST;
    }
    float q = r1 > 0 ? r2 / r1 : 1.0f;
    if (q >= 0.85f) {
        if (q < 1.0f) *tau_s = w / (2.0f * logf(1.0f / q));
        return FIT_TOO_SLOW;
    }
    if (q < 1e-4f) q = 1e-4f;
    float est = w / (2.0f * logf(1.0f / q));
    if (est < 5.0f) return FIT_TOO_FAST;  // finished within a few samples
    // Decide "too slow" from the rough estimate alone. The refinement below
    // costs ~10 ms of software float maths, during which the capacitor keeps
    // charging, so it only runs on the capture that produces the result.
    if (est > TAU_MAX_WINDOWS * m) {
        *tau_s = est;
        return FIT_TOO_SLOW;
    }

    // Refine with a golden-section search on the full exponential fit.
    float lo = est / 2.5f, hi = est * 2.5f;
    const float g = 0.618034f;
    float c = hi - g * (hi - lo), d = lo + g * (hi - lo);
    float fc = model_fit(v, m, c, NULL), fd = model_fit(v, m, d, NULL);
    for (int it = 0; it < 30; it++) {
        if (fc < fd) {
            hi = d;
            d = c;
            fd = fc;
            c = hi - g * (hi - lo);
            fc = model_fit(v, m, c, NULL);
        } else {
            lo = c;
            c = d;
            fc = fd;
            d = lo + g * (hi - lo);
            fd = model_fit(v, m, d, NULL);
        }
    }
    *tau_s = 0.5f * (lo + hi);
    if (*tau_s > TAU_MAX_WINDOWS * m) return FIT_TOO_SLOW;
    model_fit(v, m, *tau_s, vinf);
    return FIT_OK;
}

static int rate_for_tau(float tau_seconds, int m) {
    float want = (float)m / (TAU_TARGET_FRAC * tau_seconds);
    int best = 0;
    for (int i = 1; i < NRATES; i++)
        if (fabsf(logf((float)rates[i] / want)) < fabsf(logf((float)rates[best] / want))) best = i;
    return best;
}

static void handle_capture(const acq_frame_t *f) {
    int n = f->count;
    cm.ncurve = n;
    for (int i = 0; i < n; i++) cm.curve[i] = f->uv[0][i] * 1e-6f;

    const float *v = &cm.curve[ACQ_PRETRIGGER];
    int m = n - ACQ_PRETRIGGER;
    float tau_s = 0, vinf = 0;
    fit_t r = fit(v, m, &tau_s, &vinf);
    float rate = (float)rates[cm.rate_idx];

    if (r == FIT_OK) {
        if (vinf > V_SRC) vinf = V_SRC;
        float r_eff = src_r[cm.src] * vinf / V_SRC;
        cm.c = (tau_s / rate) / r_eff;
        cm.have_result = true;
        cm.last_was_error = false;
        cm.state = CM_RESULT;
        finish();
        return;
    }

    // On the first capture (1M, 50 kS/s), a part that charged before the
    // capture even started is far below range.
    bool first = cm.captures == 1 && cm.src == 3;
    if (first && (r == FIT_TOO_FAST || (r != FIT_OK && v[0] - cm.live_v > 0.5f))) {
        // Charged before the capture could follow it: far below range.
        cm.c = 0;
        cm.have_result = true;
        cm.last_was_error = false;
        cm.state = CM_RESULT;
        finish();
        return;
    }
    if (r == FIT_TOO_FAST) {
        finish_error("Charged too fast - retry");
        return;
    }

    // Too slow: move to a slower rate aimed at the estimated tau, or to a
    // smaller source resistor once the slowest rate isn't enough.
    if (cm.captures >= MAX_CAPTURES) {
        finish_error("No usable curve");
        return;
    }
    float tau_est = isfinite(tau_s) ? tau_s / rate : INFINITY;
    // Slow-curve tau estimates are rough, so step down gradually: going too
    // slow would let the capacitor finish charging between captures.
    int next = isfinite(tau_est) ? rate_for_tau(tau_est, m) : cm.rate_idx - MAX_RATE_STEP;
    if (next >= cm.rate_idx) next = cm.rate_idx - 1;  // always slow down
    if (next < cm.rate_idx - MAX_RATE_STEP) next = cm.rate_idx - MAX_RATE_STEP;
    if (next < 0) {
        if (cm.src == 0) {
            finish_error("Above range");
            return;
        }
        set_source(cm.src - 1);  // 10x less resistance: ~10x faster
        float t = isfinite(tau_est) ? tau_est / 10.0f : 1.0f;
        next = rate_for_tau(t, m);
    }
    cm.rate_idx = next;
    configure();
    capture();
}

// Runs the whole capture/fit/re-range sequence back to back. Captures
// continue one charge curve, so any gap between them (UI ticks, screen
// redraws) lets a fast capacitor finish charging before the search has found
// the right sample rate. Takes from a few ms up to a few seconds for very
// large capacitors; the UI is paused meanwhile.
static void run_search(void) {
    start_measurement();
    while (cm.state == CM_CAPTURE) {
        const acq_frame_t *f = NULL;
        while (!f) {
            if (time_reached(cm.timeout)) {
                finish_error("Capture timed out");
                return;
            }
            if (acq_state() != ACQ_STOPPED) continue;
            f = acq_acquire_frame();
            if (f && f->seq == cm.last_seq) {
                acq_release_frame();
                f = NULL;
            }
        }
        handle_capture(f);  // reconfigures and re-arms if another capture is needed
        acq_release_frame();
    }
}

bool capmeter_update(void) {
    bool dirty = false;
    bool sel = button_take(BTN_SELECT, NULL) > 0 || remote_measure;
    bool zero = button_take(BTN_RIGHT, NULL) > 0 || remote_zero;
    remote_measure = remote_zero = false;
    if (zero && cm.have_result) {
        // REL: subtract the last result (take it with nothing connected).
        if (cm.rel) cm.rel = false;
        else {
            cm.rel = true;
            cm.rel_offset = cm.c;
        }
        dirty = true;
    }
    button_take(BTN_LEFT, NULL);

    if (cm.state == CM_PENDING) {
        // "Measuring" has been drawn; now run the whole search.
        run_search();
        return true;
    }

    // Idle / showing a result: keep a live C1+ reading so the user can see
    // whether the capacitor is discharged.
    if (!cm.live_pending) {
        acq_raw_average_start(50);
        cm.live_pending = true;
    }
    float u[4];
    if (acq_raw_average_poll(u)) {
        cm.live_pending = false;
        cm.live_v = meters_c1_single_ended(u);
        cm.have_live = true;
        dirty = true;
    }

    if (sel) {
        if (!cm.have_live || cm.live_v > V_START_MAX) {
            finish_error("Discharge to <0.5V first");
        } else {
            cm.state = CM_PENDING;  // show "Measuring" before blocking
        }
        dirty = true;
    }
    return dirty;
}

// 3 significant digits with pF/nF/uF.
static void fmt_farads(char *buf, int len, float c) {
    const char *u = "pF";
    float v = c * 1e12f;
    if (fabsf(c) >= 1e-6f) {
        v = c * 1e6f;
        u = "uF";
    } else if (fabsf(c) >= 1e-9f) {
        v = c * 1e9f;
        u = "nF";
    }
    float a = fabsf(v);
    snprintf(buf, len, a < 10.0f ? "%.2f%s" : a < 100.0f ? "%.1f%s" : "%.0f%s", (double)v, u);
}

static void draw_curve(int top, int h) {
    gfx_hline(0, top + h, 160, COL_DKGRAY);
    int prev = 0;
    for (int i = 0; i < cm.ncurve; i++) {
        int y = top + h - (int)(cm.curve[i] / V_SRC * h);
        if (y < top) y = top;
        if (y > top + h) y = top + h;
        if (i) gfx_line(i - 1, prev, i, y, COL_RED);
        prev = y;
    }
}

void capmeter_render(void) {
    char buf[48], line[64];
    gfx_clear(COL_BLACK);
    gfx_text(2, 1, "CAPACITANCE", COL_YELLOW, 1);
    gfx_hline(0, 10, 160, COL_DKGRAY);

    if (cm.state == CM_CAPTURE || cm.state == CM_PENDING) {
        gfx_text(8, 20, "Measuring", COL_WHITE, 2);
    } else if (cm.last_was_error) {
        gfx_text(4, 18, cm.error, COL_RED, 1);
        gfx_text(4, 30, "Press Select to try again.", COL_GRAY, 1);
    } else if (cm.have_result) {
        float c = cm.c - (cm.rel ? cm.rel_offset : 0.0f);
        if (c < MIN_DISPLAY_F) snprintf(buf, sizeof buf, "<0.1nF");
        else fmt_farads(buf, sizeof buf, c);
        gfx_text((160 - gfx_text_width(buf, 3)) / 2, 20, buf, COL_WHITE, 3);
    } else {
        gfx_text(4, 16, "Connect C between C1+\nand GND, discharge it,\nthen press Select.", COL_WHITE, 1);
    }
    if (cm.rel) {
        fmt_farads(buf, sizeof buf, cm.rel_offset);
        snprintf(line, sizeof line, "Zero -%s", buf);
        gfx_text(160 - gfx_text_width(line, 1) - 2, 1, line, COL_CYAN, 1);
    }

    if (cm.ncurve) draw_curve(56, 34);

    if (cm.state != CM_CAPTURE && cm.state != CM_PENDING && cm.have_live) {
        bool high = cm.live_v > V_START_MAX;
        snprintf(line, sizeof line, "C1+ %.2fV", (double)cm.live_v);
        gfx_text(2, 98, line, high ? COL_RED : COL_GRAY, 1);
        if (high) gfx_text(68, 98, "discharge first", COL_RED, 1);
    }
    gfx_text(2, 109, "Range 0.1nF - 1000uF", COL_GRAY, 1);
    gfx_text(2, 120, "Select:Measure  Right:Zero", COL_GRAY, 1);
}

void capmeter_remote(int action) {
    if (action == METER_ACT_MEASURE) remote_measure = true;
    else if (action == METER_ACT_ZERO) remote_zero = true;
}

int capmeter_report(uint8_t *buf, int max) {
    uint8_t *p = buf;
    *p++ = METER_CAP;
    int st = cm.state == CM_PENDING || cm.state == CM_CAPTURE ? 1
             : cm.last_was_error                             ? 3
             : cm.have_result                                ? 2
                                                             : 0;
    *p++ = (uint8_t)st;
    *p++ = cm.have_result | cm.rel << 1 | cm.have_live << 2;
    p = put_f32(p, cm.c);
    p = put_f32(p, cm.rel_offset);
    p = put_f32(p, cm.live_v);
    const char *e = cm.last_was_error && cm.error ? cm.error : "";
    while (*e && p < buf + max - 1) *p++ = (uint8_t)*e++;
    *p++ = 0;
    return (int)(p - buf);
}
