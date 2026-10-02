// Waveform generator: PWM "DAC" on GPIO20 fed by a self-restarting DMA chain.
// PWM level 128 = 0 V, full scale = +/-3.3 V at the output pin.
#pragma once

#include <stdint.h>

typedef enum { WAVE_SINE, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SAWTOOTH, WAVE_DC } wave_t;

void wavegen_init(void);
// amplitude_mv: peak amplitude (square: 0 V to +amplitude); for WAVE_DC the
//               output level (may be negative).
// freq_mhz:     frequency in millihertz (1000 .. 100000)
void wavegen_configure(wave_t wave, int32_t amplitude_mv, uint32_t freq_mhz);

// The table DMA channel's live TRANS_COUNT register. It counts down through
// each period and jumps back up when a new period starts (phase 0), which
// the acquisition engine uses for the "Wave" trigger.
const volatile uint32_t *wavegen_phase_counter(void);
