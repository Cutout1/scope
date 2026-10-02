// Full-screen settings menu.
//   LEFT / RIGHT  change the highlighted setting
//   SELECT        move to the next setting (past the last one closes)
//   MODE          close
#pragma once

#include <stdbool.h>

void menu_open(void);
// Feeds button events; returns false when the menu has closed.
bool menu_update(void);
void menu_render(void);
