// Acquisition engine (runs on core 1).
//
// The ADC converts continuously, clocked at 48 MHz (in spec, 500k
// conversions/s) for rates up to 100 kS/s, or from pll_sys at 192 MHz
// (out of spec, 2M conversions/s) above that. DMA streams raw samples into a RAM ring buffer; core 1 reduces
// them to the selected sample rate, converts to microvolts, triggers, and
// assembles display frames.
//
//   <= 500 kS/s : C1+, C1-, C2+, C2- sampled; C1 = C1+ - C1-, C2 = C2+ - C2-
//      1 MS/s   : C1+ and C2+ only (relative to GND)
//      2 MS/s   : C1+ only (relative to GND)
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "calibration.h"

#define ACQ_POINTS 160      // samples per frame (one per display column)
#define ACQ_PRETRIGGER 20   // samples kept before the trigger point

// TRIG_WAVE triggers at phase 0 of the waveform generator output.
typedef enum { TRIG_RISING, TRIG_FALLING, TRIG_SWITCH, TRIG_WAVE } trig_mode_t;
typedef enum { CAP_REPEAT, CAP_SINGLE, CAP_ROLL } cap_mode_t;

typedef struct {
    uint32_t rate_hz;
    trig_mode_t trig;
    int32_t trig_level_uv;
    int32_t trig_hyst_uv;
    cap_mode_t cap;
    bool c1_single_ended;  // C1 = C1+ relative to GND even at <= 500 kS/s
} acq_config_t;

typedef struct {
    int32_t uv[2][ACQ_POINTS];  // [0] = C1, [1] = C2, microvolts
    volatile uint16_t count;    // valid samples (grows while a slow frame fills)
    bool triggered;             // false = auto trigger / roll snapshot
    bool has_c2;                // false at 2 MS/s
    bool roll;                  // roll snapshot: newest sample is last
    bool diff;                  // true when referenced to C1-/C2-
    uint32_t rate_hz;
    uint32_t seq;
} acq_frame_t;

typedef struct {
    uint32_t rate_hz;
    uint32_t adc_clock_hz; // 48 MHz (in spec) up to 100 kS/s, else 192 MHz
    uint32_t channels;    // 4, 2 or 1 ADC inputs in the round robin
    uint32_t adc_period;  // ADC clock cycles per conversion (>= 96)
    uint32_t oversample;  // raw readings averaged per output sample
} acq_timing_t;

typedef enum {
    ACQ_WAITING,    // looking for a trigger
    ACQ_CAPTURING,  // filling a frame
    ACQ_STOPPED,    // single capture complete
    ACQ_ROLLING,
} acq_state_t;

bool acq_timing_for_rate(uint32_t rate_hz, acq_timing_t *t);

// Starts core 1. Call once after the system clock is set.
void acq_init(const acq_config_t *cfg);
void acq_configure(const acq_config_t *cfg);
void acq_set_calibration(const calibration_set_t *cal);

// Single mode: arm for another capture.
void acq_rearm(void);
// Switch trigger mode: fire the trigger (READY -> START transition).
void acq_switch_trigger(void);

acq_state_t acq_state(void);
uint32_t acq_overruns(void);

// Averages the raw inputs over `ms` milliseconds (4-channel rates only),
// at whichever ADC clock the current rate uses.
// Results are in MicroPython read_u16() units, indexed by ADC channel.
void acq_raw_average(float out_u16[4], uint32_t ms);
// Non-blocking version: start, then poll until it returns true.
void acq_raw_average_start(uint32_t ms);
bool acq_raw_average_poll(float out_u16[4]);

// Parks core 1 in RAM (e.g. while writing flash) and resumes it.
void acq_pause(void);
void acq_resume(void);

// Latest displayable frame (NULL if none yet). The frame is protected from
// being overwritten until acq_release_frame() is called.
const acq_frame_t *acq_acquire_frame(void);
void acq_release_frame(void);

// Changes whenever the displayable frame or acquisition state changes.
uint32_t acq_version(void);

// ---------------------------------------------------------------------------
// Deep capture: one long record using the current rate and trigger, with up
// to 200 pre-trigger samples. Samples are int16 in ACQ_DEEP_LSB_UV units.
// ---------------------------------------------------------------------------
#define ACQ_DEEP_MAX 24000
#define ACQ_DEEP_LSB_UV 256

typedef enum { ACQ_DEEP_IDLE, ACQ_DEEP_ARMED, ACQ_DEEP_CAPTURING, ACQ_DEEP_DONE } acq_deep_state_t;

typedef struct {
    uint32_t count;    // samples per channel
    uint32_t rate_hz;
    uint32_t pre;      // samples before the trigger point
    bool has_c2;
    bool diff;
    bool triggered;
} acq_deep_info_t;

bool acq_deep_start(uint32_t n);  // false if one is already armed/running
void acq_deep_cancel(void);
acq_deep_state_t acq_deep_state(uint32_t *progress);
// Valid once the state is ACQ_DEEP_DONE, until acq_deep_release().
const int16_t *acq_deep_data(int ch, acq_deep_info_t *info);
void acq_deep_release(void);

// Capture now (as if triggered), also re-arming a stopped Single capture.
void acq_force_trigger(void);

// Logging stream: every output sample (rates up to ACQ_STREAM_MAX_RATE,
// 4-input paths), packed as (uint16)C1 | (uint16)C2 << 16 in ACQ_DEEP_LSB_UV.
#define ACQ_STREAM_MAX_RATE 50000
void acq_stream_enable(bool on);
bool acq_stream_active(void);
uint32_t acq_stream_read(uint32_t *dst, uint32_t max);
uint32_t acq_stream_dropped(void);
