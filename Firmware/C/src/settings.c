#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// 1-2-5 series. Every rate divides the ADC clock evenly (see
// acq_timing_for_rate). 1 MS/s samples C1+/C2+ only, 2 MS/s C1+ only.
const uint32_t sample_rates[] = {100,   200,    500,    1000,   2000,    5000,    10000,
                                 20000, 50000, 100000, 200000, 500000, 1000000, 2000000};
const int sample_rate_count = sizeof sample_rates / sizeof sample_rates[0];

const uint32_t vdiv_mv[] = {100, 200, 500, 1000, 2000};
const int vdiv_count = sizeof vdiv_mv / sizeof vdiv_mv[0];

const uint32_t wave_amp_mv[] = {500, 1000, 1500, 2000, 2500, 3000, 3300};
const int wave_amp_count = sizeof wave_amp_mv / sizeof wave_amp_mv[0];

const uint32_t wave_freq_mhz[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000};
const int wave_freq_count = sizeof wave_freq_mhz / sizeof wave_freq_mhz[0];

const char *const trig_mode_names[] = {"Rising", "Falling", "Switch", "Wave Out"};
const char *const display_names[] = {"Normal", "XY"};
const char *const onoff_names[] = {"Off", "On"};
const char *const cap_mode_names[] = {"Repeat", "Single", "Roll"};
const char *const wave_names[] = {"Sine", "Square", "Triangle", "Sawtooth", "DC"};
const char *const mode_names[] = {"Scope", "Volt", "Ohm", "Cap"};

settings_t settings = {
    .mode = MODE_SCOPE,
    .rate_idx = 0,  // 100 S/s shows the 1 Hz output nicely
    .vdiv_idx = 3,  // 1 V/div
    .display = DISPLAY_YT,
    .measure = 0,
    .trig_mode = TRIG_RISING,
    .trig_level_idx = 30,  // 0 V
    .cap_mode = CAP_REPEAT,
    .wave_type = WAVE_SINE,
    .wave_amp_idx = 6,   // 3.3 V
    .dc_level_idx = 33,  // 0 V
    .wave_freq_idx = 0,  // 1 Hz
};

uint32_t settings_rate_hz(void) { return sample_rates[settings.rate_idx]; }
uint32_t settings_vdiv_uv(void) { return vdiv_mv[settings.vdiv_idx] * 1000u; }

int32_t settings_trig_level_uv(void) {
    return (TRIG_LEVEL_MIN_MV + settings.trig_level_idx * TRIG_LEVEL_STEP_MV) * 1000;
}

void settings_acq_config(acq_config_t *c) {
    c->rate_hz = settings_rate_hz();
    c->trig = (trig_mode_t)settings.trig_mode;
    c->trig_level_uv = settings_trig_level_uv();
    // Hysteresis: a tenth of a division, at least 20 mV.
    int32_t hyst = (int32_t)settings_vdiv_uv() / 10;
    c->trig_hyst_uv = hyst < 20000 ? 20000 : hyst;
    c->cap = (cap_mode_t)settings.cap_mode;
    c->c1_single_ended = false;
}

void settings_apply_wave(void) {
    int32_t amp = settings.wave_type == WAVE_DC
                      ? DC_LEVEL_MIN_MV + settings.dc_level_idx * DC_LEVEL_STEP_MV
                      : (int32_t)wave_amp_mv[settings.wave_amp_idx];
    wavegen_configure((wave_t)settings.wave_type, amp, wave_freq_mhz[settings.wave_freq_idx]);
}

void format_rate(char *buf, int len, uint32_t hz) {
    if (hz >= 1000000 && hz % 1000000 == 0)
        snprintf(buf, len, "%luMS/s", (unsigned long)(hz / 1000000));
    else if (hz >= 1000 && hz % 1000 == 0)
        snprintf(buf, len, "%lukS/s", (unsigned long)(hz / 1000));
    else
        snprintf(buf, len, "%luS/s", (unsigned long)hz);
}

void format_time(char *buf, int len, float s, int show_sign) {
    const char *unit = "s";
    float v = s;
    float a = fabsf(s);
    if (a < 1e-3f) {
        v = s * 1e6f;
        unit = "us";
    } else if (a < 1.0f) {
        v = s * 1e3f;
        unit = "ms";
    }
    float av = fabsf(v);
    int decimals = av < 10.0f ? 2 : av < 100.0f ? 1 : 0;
    if (av < 0.005f) v = 0.0f;
    int n = snprintf(buf, len, show_sign ? "%+.*f" : "%.*f", decimals, (double)v);
    // Drop trailing zeros for unsigned labels ("40us", "0.5s").
    if (!show_sign && decimals > 0 && n > 0 && n < len) {
        while (n > 0 && buf[n - 1] == '0') buf[--n] = 0;
        if (n > 0 && buf[n - 1] == '.') buf[--n] = 0;
    }
    snprintf(buf + n, len - n, "%s", unit);
}

void format_mv(char *buf, int len, int32_t mv, bool show_sign) {
    const char *sign = show_sign ? (mv > 0 ? "+" : mv < 0 ? "-" : " ") : (mv < 0 ? "-" : "");
    int32_t a = abs(mv);
    if (a < 1000 && a != 0 && !show_sign) snprintf(buf, len, "%s%ldmV", sign, (long)a);
    else if (a % 1000 == 0) snprintf(buf, len, "%s%ldV", sign, (long)(a / 1000));
    else if (a % 100 == 0) snprintf(buf, len, "%s%ld.%ldV", sign, (long)(a / 1000), (long)(a % 1000 / 100));
    else snprintf(buf, len, "%s%ld.%02ldV", sign, (long)(a / 1000), (long)(a % 1000 / 10));
}

// ---------------------------------------------------------------------------
// Setting descriptors
// ---------------------------------------------------------------------------
static void fmt_names(const char *const *names, int i, char *b, int n) { snprintf(b, n, "%s", names[i]); }
static void fmt_mode(int i, char *b, int n) { fmt_names(mode_names, i, b, n); }
static void fmt_rate(int i, char *b, int n) { format_rate(b, n, sample_rates[i]); }
static void fmt_vdiv(int i, char *b, int n) { format_mv(b, n, (int32_t)vdiv_mv[i], false); }
static void fmt_display(int i, char *b, int n) { fmt_names(display_names, i, b, n); }
static void fmt_onoff(int i, char *b, int n) { fmt_names(onoff_names, i, b, n); }
static void fmt_cap(int i, char *b, int n) { fmt_names(cap_mode_names, i, b, n); }
static void fmt_trig(int i, char *b, int n) { fmt_names(trig_mode_names, i, b, n); }
static void fmt_level(int i, char *b, int n) {
    format_mv(b, n, TRIG_LEVEL_MIN_MV + i * TRIG_LEVEL_STEP_MV, true);
}
static void fmt_wave(int i, char *b, int n) { fmt_names(wave_names, i, b, n); }
static void fmt_amp(int i, char *b, int n) { format_mv(b, n, (int32_t)wave_amp_mv[i], false); }
static void fmt_dc(int i, char *b, int n) { format_mv(b, n, DC_LEVEL_MIN_MV + i * DC_LEVEL_STEP_MV, true); }
static void fmt_freq(int i, char *b, int n) {
    uint32_t m = wave_freq_mhz[i];
    if (m % 1000) snprintf(b, n, "%lu.%luHz", (unsigned long)(m / 1000), (unsigned long)(m % 1000 / 100));
    else snprintf(b, n, "%luHz", (unsigned long)(m / 1000));
}

static int32_t num_rate(int i) { return (int32_t)sample_rates[i]; }
static int32_t num_vdiv(int i) { return (int32_t)vdiv_mv[i] * 1000; }
static int32_t num_level(int i) { return (TRIG_LEVEL_MIN_MV + i * TRIG_LEVEL_STEP_MV) * 1000; }
static int32_t num_amp(int i) { return (int32_t)wave_amp_mv[i]; }
static int32_t num_dc(int i) { return DC_LEVEL_MIN_MV + i * DC_LEVEL_STEP_MV; }
static int32_t num_freq(int i) { return (int32_t)wave_freq_mhz[i]; }

static bool scope_mode(void) { return settings.mode == MODE_SCOPE; }
static bool trig_used(void) { return settings.cap_mode != CAP_ROLL; }
static bool level_used(void) {
    return trig_used() && (settings.trig_mode == TRIG_RISING || settings.trig_mode == TRIG_FALLING);
}
static bool measure_used(void) { return settings.display == DISPLAY_YT; }
static bool wave_ac(void) { return settings.wave_type != WAVE_DC; }
static bool wave_dc(void) { return settings.wave_type == WAVE_DC; }

const setting_item_t setting_items[] = {
    {"mode", "Mode", &settings.mode, 4, fmt_mode, NULL, NULL, NULL, NULL, false},
    {"rate", "Sample rate", &settings.rate_idx, 14, fmt_rate, num_rate, "Hz", NULL, scope_mode, false},
    {"vdiv", "Volts/div", &settings.vdiv_idx, 5, fmt_vdiv, num_vdiv, "uV", NULL, scope_mode, false},
    {"display", "Display", &settings.display, 2, fmt_display, NULL, NULL, NULL, scope_mode, false},
    {"capture", "Capture", &settings.cap_mode, 3, fmt_cap, NULL, NULL, NULL, scope_mode, false},
    {"trigger", "Trigger", &settings.trig_mode, 4, fmt_trig, NULL, NULL, trig_used, scope_mode, false},
    {"trig_level", "Trig level", &settings.trig_level_idx, TRIG_LEVEL_COUNT, fmt_level, num_level, "uV", level_used, scope_mode, true},
    {"measure", "Measure", &settings.measure, 2, fmt_onoff, NULL, NULL, measure_used, scope_mode, false},
    {"wave", "Wave out", &settings.wave_type, 5, fmt_wave, NULL, NULL, NULL, NULL, false},
    {"wave_amp", "Wave amp", &settings.wave_amp_idx, 7, fmt_amp, num_amp, "mV", NULL, wave_ac, false},
    {"dc_level", "DC level", &settings.dc_level_idx, DC_LEVEL_COUNT, fmt_dc, num_dc, "mV", NULL, wave_dc, true},
    {"wave_freq", "Wave freq", &settings.wave_freq_idx, 7, fmt_freq, num_freq, "mHz", NULL, wave_ac, false},
};
const int setting_count = sizeof setting_items / sizeof setting_items[0];
