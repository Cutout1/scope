// User settings and their option lists.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "acquisition.h"
#include "wavegen.h"

typedef enum { MODE_SCOPE, MODE_VOLT, MODE_OHM, MODE_CAP } app_mode_t;
typedef enum { DISPLAY_YT, DISPLAY_XY } display_mode_t;

typedef struct {
    int mode;           // app_mode_t
    int rate_idx;       // sample_rates[]
    int vdiv_idx;       // vdiv_mv[]
    int display;        // display_mode_t
    int measure;        // automatic measurements: 0 off, 1 on
    int trig_mode;      // trig_mode_t
    int trig_level_idx; // TRIG_LEVEL_* steps
    int cap_mode;       // cap_mode_t
    int wave_type;      // wave_t
    int wave_amp_idx;   // wave_amp_mv[]
    int dc_level_idx;   // DC_LEVEL_* steps
    int wave_freq_idx;  // wave_freq_mhz[]
} settings_t;

extern settings_t settings;

extern const uint32_t sample_rates[];
extern const int sample_rate_count;
extern const uint32_t vdiv_mv[];
extern const int vdiv_count;
extern const uint32_t wave_amp_mv[];
extern const int wave_amp_count;
extern const uint32_t wave_freq_mhz[];
extern const int wave_freq_count;

// Trigger level: -3.0 V .. +3.0 V in 0.1 V steps
#define TRIG_LEVEL_MIN_MV -3000
#define TRIG_LEVEL_STEP_MV 100
#define TRIG_LEVEL_COUNT 61

// DC output: -3.3 V .. +3.3 V in 0.1 V steps
#define DC_LEVEL_MIN_MV -3300
#define DC_LEVEL_STEP_MV 100
#define DC_LEVEL_COUNT 67

extern const char *const trig_mode_names[];
extern const char *const display_names[];
extern const char *const onoff_names[];
extern const char *const cap_mode_names[];
extern const char *const wave_names[];
extern const char *const mode_names[];

// Setting descriptors, shared by the on-device menu and the USB protocol.
// Order is the protocol's setting id.
typedef struct {
    const char *key;    // stable identifier for the companion app
    const char *label;  // menu label
    int *value;         // option index
    int count;          // number of options
    void (*format)(int idx, char *buf, int len);
    int32_t (*numeric)(int idx);  // value in base units (see unit), or NULL
    const char *unit;             // "Hz", "uV", "mV", "mHz" or NULL
    bool (*enabled)(void);        // NULL = always relevant (otherwise greyed out)
    bool (*visible)(void);        // NULL = always shown
    bool clamp;                   // stop at the ends instead of wrapping
} setting_item_t;

extern const setting_item_t setting_items[];
extern const int setting_count;

static inline bool setting_visible(int i) { return !setting_items[i].visible || setting_items[i].visible(); }
static inline bool setting_enabled(int i) { return !setting_items[i].enabled || setting_items[i].enabled(); }

uint32_t settings_rate_hz(void);
uint32_t settings_vdiv_uv(void);
int32_t settings_trig_level_uv(void);
void settings_acq_config(acq_config_t *c);
void settings_apply_wave(void);

// "500kS/s", "2MS/s", "100S/s"
void format_rate(char *buf, int len, uint32_t hz);
// Engineering format of a time in seconds: "+1.23ms", "40us", "1.6s"
void format_time(char *buf, int len, float seconds, int show_sign);
// "0.5V", "2V", "100mV"
void format_mv(char *buf, int len, int32_t mv, bool show_sign);
