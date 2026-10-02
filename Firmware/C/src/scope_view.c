#include "scope_view.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "acquisition.h"
#include "buttons.h"
#include "gfx.h"
#include "pico/time.h"
#include "settings.h"

// Layout
#define PLOT_TOP 10
#define PLOT_BOTTOM 118  // inclusive
#define Y_ZERO 64
#define PX_PER_DIV 16
#define DIV_SAMPLES 20   // horizontal grid every 20 samples
#define BAR_Y 120

static int cursor = ACQ_PRETRIGGER;
static uint32_t last_overruns;
static absolute_time_t overrun_until;

void scope_view_init(void) { cursor = ACQ_PRETRIGGER; }

bool scope_view_update(void) {
    bool changed = false;
    bool fast_l, fast_r;
    int l = button_take(BTN_LEFT, &fast_l);
    int r = button_take(BTN_RIGHT, &fast_r);
    if (l || r) {
        int step = r * (fast_r ? 5 : 1) - l * (fast_l ? 5 : 1);
        cursor = ((cursor + step) % ACQ_POINTS + ACQ_POINTS) % ACQ_POINTS;
        changed = true;
    }
    // SELECT re-arms a single capture.
    if (button_take(BTN_SELECT, NULL) && settings.cap_mode == CAP_SINGLE) {
        acq_rearm();
        changed = true;
    }
    uint32_t ov = acq_overruns();
    if (ov != last_overruns) {
        last_overruns = ov;
        overrun_until = make_timeout_time_ms(1000);
        changed = true;
    }
    return changed;
}

static int uv_to_y(int32_t uv) {
    int64_t y = Y_ZERO - ((int64_t)uv * PX_PER_DIV) / (int64_t)settings_vdiv_uv();
    if (y < -1000) y = -1000;
    if (y > 1000) y = 1000;
    return (int)y;
}

static void draw_grid(bool show_trigger) {
    for (int y = Y_ZERO % PX_PER_DIV; y <= PLOT_BOTTOM; y += PX_PER_DIV) {
        if (y < PLOT_TOP || y == Y_ZERO) continue;
        for (int x = 0; x < 160; x += 4) gfx_pixel(x, y, COL_DKGRAY);
    }
    for (int x = DIV_SAMPLES; x < 160; x += DIV_SAMPLES)
        for (int y = PLOT_TOP; y <= PLOT_BOTTOM; y += 4) gfx_pixel(x, y, COL_DKGRAY);
    // 0 V axis with half-division ticks on the left edge
    for (int x = 0; x < 160; x += 2) gfx_pixel(x, Y_ZERO, COL_GRAY);
    for (int y = Y_ZERO % (PX_PER_DIV / 2); y <= PLOT_BOTTOM; y += PX_PER_DIV / 2)
        if (y >= PLOT_TOP) gfx_hline(0, y, (y - Y_ZERO) % PX_PER_DIV ? 2 : 4, COL_GRAY);

    if (!show_trigger) return;
    // Trigger position marker (top)
    gfx_hline(ACQ_PRETRIGGER - 2, PLOT_TOP, 5, COL_YELLOW);
    gfx_hline(ACQ_PRETRIGGER - 1, PLOT_TOP + 1, 3, COL_YELLOW);
    gfx_pixel(ACQ_PRETRIGGER, PLOT_TOP + 2, COL_YELLOW);
    // Trigger level marker (left edge arrow)
    if (settings.trig_mode == TRIG_RISING || settings.trig_mode == TRIG_FALLING) {
        int y = uv_to_y(settings_trig_level_uv());
        if (y >= PLOT_TOP + 2 && y <= PLOT_BOTTOM - 2) {
            gfx_vline(0, y - 2, 5, COL_YELLOW);
            gfx_vline(1, y - 1, 3, COL_YELLOW);
            gfx_pixel(2, y, COL_YELLOW);
            for (int x = 4; x < 160; x += 6) gfx_pixel(x, y, COL_YELLOW);
        }
    }
}

// Draws samples [from, to) of one channel, one per column.
static void draw_span(const int32_t *uv, int from, int to, uint16_t color) {
    if (from < 0) from = 0;
    if (to - from <= 0) return;
    int prev = uv_to_y(uv[from]);
    if (to - from == 1) gfx_line(from, prev, from, prev, color);
    for (int i = from + 1; i < to; i++) {
        int y = uv_to_y(uv[i]);
        gfx_line(i - 1, prev, i, y, color);
        prev = y;
    }
}

static void draw_trace(const acq_frame_t *f, int ch, uint16_t color) {
    draw_span(f->uv[ch], 0, f->count, color);
}

// Sweep display: at slow rates frames arrive while still filling. The new
// trace is drawn over the last complete frame, which stays visible ahead of
// the write position (with a small gap), like an analog scope's sweep.
#define SWEEP_GAP 4
static acq_frame_t last_full;
static bool have_full;

static void remember_full(const acq_frame_t *f) {
    if (f && !f->roll && f->count == ACQ_POINTS) {
        memcpy(&last_full, (const void *)f, sizeof last_full);
        have_full = true;
    }
}

static bool sweeping(const acq_frame_t *f) {
    return f && !f->roll && f->count < ACQ_POINTS && have_full && last_full.rate_hz == f->rate_hz &&
           last_full.has_c2 == f->has_c2;
}

// Sample shown at column i: the new frame, or the old one ahead of the sweep.
static bool shown_sample(const acq_frame_t *f, int ch, int i, int32_t *uv) {
    if (!f || (ch == 1 && !f->has_c2)) return false;
    if (i < f->count) {
        *uv = f->uv[ch][i];
        return true;
    }
    if (sweeping(f) && i >= f->count + SWEEP_GAP) {
        *uv = last_full.uv[ch][i];
        return true;
    }
    return false;
}

static void fmt_volts(char *buf, int len, const acq_frame_t *f, int ch) {
    int32_t uv;
    if (!shown_sample(f, ch, cursor, &uv)) {
        snprintf(buf, len, "  ---");
        return;
    }
    float v = uv * 1e-6f;
    if (settings_vdiv_uv() < 500000) snprintf(buf, len, "%+.3fV", (double)v);
    else snprintf(buf, len, "%+.2fV", (double)v);
}

static void state_label(const acq_frame_t *f, const char **st, uint16_t *col) {
    acq_state_t s = acq_state();
    if (absolute_time_diff_us(get_absolute_time(), overrun_until) > 0) {
        *st = "OVR";
        *col = COL_RED;
    } else if (s == ACQ_ROLLING) {
        *st = "ROLL";
        *col = COL_CYAN;
    } else if (s == ACQ_STOPPED) {
        *st = "STOP";
        *col = COL_RED;
    } else if (s == ACQ_CAPTURING) {
        *st = "TRIG";
        *col = COL_GREEN;
    } else if (settings.cap_mode == CAP_SINGLE || settings.trig_mode == TRIG_SWITCH) {
        *st = "ARMED";
        *col = COL_CYAN;
    } else if (!f) {
        *st = "WAIT";
        *col = COL_GRAY;
    } else if (f->triggered) {
        *st = "TRIG";
        *col = COL_GREEN;
    } else {
        *st = "AUTO";
        *col = COL_YELLOW;
    }
}

// ---------------------------------------------------------------------------
// Automatic measurements
// ---------------------------------------------------------------------------
typedef struct {
    float vpp, vrms, freq;
    bool have_freq;
} meas_t;

static bool measure(const acq_frame_t *f, int ch, uint32_t rate, meas_t *m) {
    int n = f->count;
    if (n < 4 || (ch == 1 && !f->has_c2)) return false;
    float vmin = 1e9f, vmax = -1e9f;
    for (int i = 0; i < n; i++) {
        float v = f->uv[ch][i] * 1e-6f;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    m->vpp = vmax - vmin;

    // Rising crossings of the midpoint, with 10% hysteresis, interpolated
    // between samples. The period is the mean spacing of the crossings.
    float mid = 0.5f * (vmax + vmin), hyst = 0.1f * m->vpp;
    float first = -1, last = -1;
    int ncross = 0;
    bool low = false;
    m->have_freq = false;
    if (m->vpp > 0.02f) {
        for (int i = 1; i < n; i++) {
            float v0 = f->uv[ch][i - 1] * 1e-6f, v = f->uv[ch][i] * 1e-6f;
            if (v < mid - hyst) low = true;
            else if (low && v >= mid && v0 < mid) {
                float x = (float)(i - 1) + (mid - v0) / (v - v0);
                if (ncross == 0) first = x;
                last = x;
                ncross++;
                low = false;
            }
        }
    }
    int a = 0, b = n;
    if (ncross >= 2) {
        float period = (last - first) / (float)(ncross - 1);
        m->freq = (float)rate / period;
        m->have_freq = true;
        // RMS over whole periods only, so a partial cycle doesn't bias it.
        a = (int)ceilf(first);
        b = (int)ceilf(last);
    }
    double sumsq = 0;
    for (int i = a; i < b; i++) {
        double v = f->uv[ch][i] * 1e-6;
        sumsq += v * v;
    }
    m->vrms = (float)sqrt(sumsq / (double)(b - a));
    return true;
}

static void format_freq(char *buf, int len, float hz) {
    if (hz >= 1000.0f) {
        float k = hz / 1000.0f;
        snprintf(buf, len, k < 10.0f ? "%.2fkHz" : k < 100.0f ? "%.1fkHz" : "%.0fkHz", (double)k);
    } else {
        snprintf(buf, len, hz < 10.0f ? "%.2fHz" : hz < 100.0f ? "%.1fHz" : "%.0fHz", (double)hz);
    }
}

static void draw_measurements(const acq_frame_t *f, uint32_t rate) {
    static const uint16_t colors[2] = {COL_RED, COL_C2};
    int y = PLOT_BOTTOM - 17;
    for (int ch = 0; ch < 2; ch++, y += 9) {
        meas_t m;
        char fq[16], line[40];
        if (!f || !measure(f, ch, rate, &m)) continue;
        if (m.have_freq) format_freq(fq, sizeof fq, m.freq);
        else snprintf(fq, sizeof fq, "---");
        snprintf(line, sizeof line, "%-7s %.2fVpp %.2fVrms", fq, (double)m.vpp, (double)m.vrms);
        gfx_text_bg(0, y, line, colors[ch], COL_BLACK, 1);
    }
}

// ---------------------------------------------------------------------------
// XY display: X = C1, Y = C2, same V/div on both axes
// ---------------------------------------------------------------------------
#define XY_CX 80

static int uv_to_x(int32_t uv) {
    int64_t x = XY_CX + ((int64_t)uv * PX_PER_DIV) / (int64_t)settings_vdiv_uv();
    if (x < -1000) x = -1000;
    if (x > 1000) x = 1000;
    return (int)x;
}

static void draw_xy_grid(void) {
    for (int x = XY_CX % PX_PER_DIV; x < 160; x += PX_PER_DIV)
        for (int y = PLOT_TOP; y <= PLOT_BOTTOM; y += 4)
            gfx_pixel(x, y, x == XY_CX ? COL_GRAY : COL_DKGRAY);
    for (int y = Y_ZERO % PX_PER_DIV; y <= PLOT_BOTTOM; y += PX_PER_DIV) {
        if (y < PLOT_TOP) continue;
        for (int x = 0; x < 160; x += 4) gfx_pixel(x, y, y == Y_ZERO ? COL_GRAY : COL_DKGRAY);
    }
    for (int x = 0; x < 160; x += 2) gfx_pixel(x, Y_ZERO, COL_GRAY);
    for (int y = PLOT_TOP; y <= PLOT_BOTTOM; y += 2) gfx_pixel(XY_CX, y, COL_GRAY);
}

static void render_xy(const acq_frame_t *f) {
    draw_xy_grid();
    if (f && f->has_c2) {
        gfx_set_line_clip(0, PLOT_TOP, 159, PLOT_BOTTOM);
        int n = f->count;
        int px = uv_to_x(f->uv[0][0]), py = uv_to_y(f->uv[1][0]);
        for (int i = 1; i < n; i++) {
            int x = uv_to_x(f->uv[0][i]), y = uv_to_y(f->uv[1][i]);
            gfx_line(px, py, x, y, COL_YELLOW);
            px = x;
            py = y;
        }
        gfx_set_line_clip(0, 0, 159, 127);
    } else if (f) {
        gfx_text_bg(8, 50, "XY needs C2: use a", COL_WHITE, COL_BLACK, 1);
        gfx_text_bg(8, 60, "rate of 1MS/s or less", COL_WHITE, COL_BLACK, 1);
    }
    gfx_text(0, 0, "XY", COL_YELLOW, 1);
    gfx_text(24, 0, "X=C1", COL_RED, 1);
    gfx_text(60, 0, "Y=C2", COL_C2, 1);
    if (f && !f->diff) gfx_text(154, 0, "*", COL_GRAY, 1);
}

static void render_yt(const acq_frame_t *f, uint32_t rate) {
    char buf[24];
    bool roll = f ? f->roll : settings.cap_mode == CAP_ROLL;
    draw_grid(!roll);

    remember_full(f);
    bool sweep = sweeping(f);
    gfx_set_line_clip(0, PLOT_TOP, 159, PLOT_BOTTOM);
    if (sweep) {
        int from = f->count + SWEEP_GAP;
        if (last_full.has_c2) draw_span(last_full.uv[1], from, ACQ_POINTS, COL_C2);
        draw_span(last_full.uv[0], from, ACQ_POINTS, COL_RED);
    }
    if (f) {
        if (f->has_c2) draw_trace(f, 1, COL_C2);
        draw_trace(f, 0, COL_RED);
    }
    gfx_set_line_clip(0, 0, 159, 127);

    // Cursor
    for (int y = PLOT_TOP; y <= PLOT_BOTTOM; y += 2) gfx_pixel(cursor, y, COL_GREEN);

    // Top bar: cursor time (from trigger, or from newest sample in roll),
    // C1, C2 at the cursor. Without the references (1-2 MS/s) values are
    // relative to GND, shown with a '*' marker.
    int t0 = roll ? (f && f->count ? f->count - 1 : ACQ_POINTS - 1) : ACQ_PRETRIGGER;
    format_time(buf, sizeof buf, (float)(cursor - t0) / (float)rate, 1);
    gfx_text(0, 0, buf, COL_GREEN, 1);
    fmt_volts(buf, sizeof buf, f, 0);
    gfx_text(52, 0, buf, COL_RED, 1);
    fmt_volts(buf, sizeof buf, f, 1);
    gfx_text(106, 0, buf, COL_C2, 1);
    if (f && !f->diff) gfx_text(154, 0, "*", COL_GRAY, 1);

    // Measure complete frames only (the last one while a sweep is drawing).
    if (settings.measure) draw_measurements(sweep ? &last_full : f, rate);
}

static void render_bottom_bar(const acq_frame_t *f, uint32_t rate) {
    char buf[24];
    gfx_fill_rect(0, BAR_Y, 160, 8, COL_BLACK);
    format_rate(buf, sizeof buf, settings_rate_hz());
    int x = gfx_text(0, BAR_Y, buf, COL_YELLOW, 1) + 4;
    format_mv(buf, sizeof buf, (int32_t)(settings_vdiv_uv() / 1000), false);
    x = gfx_text(x, BAR_Y, buf, COL_WHITE, 1) + 4;
    if (settings.display == DISPLAY_YT) {
        format_time(buf, sizeof buf, (float)DIV_SAMPLES / (float)rate, 0);
        gfx_text(x, BAR_Y, buf, COL_GRAY, 1);
    }

    const char *st;
    uint16_t sc;
    state_label(f, &st, &sc);
    gfx_text(160 - gfx_text_width(st, 1), BAR_Y, st, sc, 1);
}

void scope_view_render(void) {
    gfx_clear(COL_BLACK);
    const acq_frame_t *f = acq_acquire_frame();
    uint32_t rate = f ? f->rate_hz : settings_rate_hz();
    if (settings.display == DISPLAY_XY) render_xy(f);
    else render_yt(f, rate);
    render_bottom_bar(f, rate);
    acq_release_frame();
}
