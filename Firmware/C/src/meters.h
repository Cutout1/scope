// Voltmeter and ohmmeter screens.
#pragma once

#include <stdbool.h>

#include <stdint.h>

#include "calibration.h"

enum { METER_VOLT = 1, METER_OHM = 2, METER_CAP = 3 };  // same values as MODE_*

void meters_set_calibration(const calibration_t *cal);
// C1+ relative to GND, in volts, from acq_raw_average() results (48 MHz).
float meters_c1_single_ended(const float u16[4]);
void meters_enter(int mode);
void meters_exit(int mode);
// Polls the measurement and buttons; returns true if the screen changed.
bool meters_update(int mode);
void meters_render(int mode);

// Remote control (companion app). Actions behave like the matching button.
enum { METER_ACT_RESET_MINMAX = 1, METER_ACT_ZERO = 2, METER_ACT_MEASURE = 3 };
void meters_remote(int mode, int action);
// Serialises the current reading for the USB protocol; returns its length.
int meters_report(int mode, uint8_t *buf, int max);

// Little-endian float into a byte buffer (shared by the report writers).
static inline uint8_t *put_f32(uint8_t *p, float f) {
    union { float f; uint8_t b[4]; } u = {.f = f};
    for (int i = 0; i < 4; i++) *p++ = u.b[i];
    return p;
}
