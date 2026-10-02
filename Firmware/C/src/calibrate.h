#pragma once

#include "calibration.h"

// Interactive GND / 3.3 V calibration. Each step is measured at both ADC
// clocks (48 MHz and 192 MHz). Requires the acquisition engine to be running;
// leaves it reconfigured, so the caller must restore its own configuration.
// Saves to flash and returns the new calibration.
void calibrate_run(calibration_set_t *cal);
