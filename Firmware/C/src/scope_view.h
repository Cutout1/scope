// Main oscilloscope screen.
#pragma once

#include <stdbool.h>

void scope_view_init(void);
// Handles LEFT/RIGHT (cursor). Returns true if the view needs redrawing.
bool scope_view_update(void);
void scope_view_render(void);
