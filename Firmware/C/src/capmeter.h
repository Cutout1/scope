// Capacitance meter screen (see capmeter.c for the method).
#pragma once

#include <stdbool.h>
#include <stdint.h>

void capmeter_enter(void);  // configures the acquisition engine itself
void capmeter_exit(void);
bool capmeter_update(void);
void capmeter_render(void);
void capmeter_remote(int action);  // METER_ACT_MEASURE / METER_ACT_ZERO
int capmeter_report(uint8_t *buf, int max);
