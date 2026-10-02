// Calibration values persisted in the last sector of flash (replaces the
// MicroPython firmware's calibration.txt).
#pragma once

#include <stdbool.h>

// Averaged 16-bit ADC readings (MicroPython read_u16 scale) with all four
// inputs tied to GND and to +3.3 V.
typedef struct {
    float c1ref_0, c1ref_3v3;
    float c1_0, c1_3v3;
    float c2ref_0, c2ref_3v3;
    float c2_0, c2_3v3;
} calibration_t;

// The ADC runs at 48 MHz (in spec) for sample rates up to 100 kS/s and in
// the meters, and at 192 MHz above that; each clock has its own calibration.
typedef struct {
    calibration_t adc48;
    calibration_t adc192;
} calibration_set_t;

// Returns true and fills *cal if a valid calibration is stored.
bool calibration_load(calibration_set_t *cal);
void calibration_save(const calibration_set_t *cal);
