#include "menu.h"

#include <stdio.h>

#include "buttons.h"
#include "gfx.h"
#include "settings.h"


#define NITEMS setting_count
#define MAX_ITEMS 32

#define ROW_Y0 14
#define ROW_H 11
#define MAX_ROWS 9
#define VALUE_X 76

static int sel;     // index into setting_items[]
static int scroll;  // first visible row

static bool shown(int i) { return setting_visible(i); }

void menu_open(void) {
    sel = 0;
    scroll = 0;
    for (int b = 0; b < BTN_COUNT; b++) button_take((button_t)b, NULL);
}

bool menu_update(void) {
    const setting_item_t *it = &setting_items[sel];
    int steps = button_take(BTN_RIGHT, NULL) - button_take(BTN_LEFT, NULL);
    if (steps) {
        int n = it->count;
        int v = *it->value + steps;
        if (it->clamp) v = v < 0 ? 0 : v >= n ? n - 1 : v;
        else v = (v % n + n) % n;
        *it->value = v;
    }

    if (button_take(BTN_MODE, NULL)) return false;
    if (button_take(BTN_SELECT, NULL)) {
        do {
            if (++sel >= NITEMS) return false;
        } while (!shown(sel));
    }
    return true;
}

void menu_render(void) {
    gfx_clear(COL_BLACK);
    gfx_text(2, 2, "SETTINGS", COL_YELLOW, 1);
    gfx_text(160 - gfx_text_width("MODE:exit", 1) - 1, 2, "MODE:exit", COL_GRAY, 1);
    gfx_hline(0, 11, 160, COL_GRAY);

    // Visible items, and where the selection sits among them.
    int vis[MAX_ITEMS], nvis = 0, sel_row = 0;
    for (int i = 0; i < NITEMS; i++) {
        if (!shown(i)) continue;
        if (i == sel) sel_row = nvis;
        vis[nvis++] = i;
    }
    if (sel_row < scroll) scroll = sel_row;
    if (sel_row >= scroll + MAX_ROWS) scroll = sel_row - MAX_ROWS + 1;
    if (scroll > nvis - MAX_ROWS) scroll = nvis > MAX_ROWS ? nvis - MAX_ROWS : 0;

    char buf[24];
    for (int row = 0; row < MAX_ROWS && scroll + row < nvis; row++) {
        int i = vis[scroll + row];
        int y = ROW_Y0 + row * ROW_H;
        bool active = (i == sel);
        bool en = setting_enabled(i);
        if (active) gfx_fill_rect(0, y - 2, 160, ROW_H - 1, COL_DKGRAY);
        gfx_text(2, y, setting_items[i].label, en ? COL_WHITE : COL_GRAY, 1);
        setting_items[i].format(*setting_items[i].value, buf, sizeof buf);
        uint16_t vc = en ? COL_GREEN : COL_GRAY;
        int w = gfx_text_width(buf, 1);
        if (active) {
            gfx_text(VALUE_X, y, "<", COL_YELLOW, 1);
            gfx_text(VALUE_X + 10, y, buf, vc, 1);
            gfx_text(VALUE_X + 10 + w + 4, y, ">", COL_YELLOW, 1);
        } else {
            gfx_text(VALUE_X + 10, y, buf, vc, 1);
        }
    }
    // Scroll indicators
    if (scroll > 0) gfx_text(152, ROW_Y0, "^", COL_YELLOW, 1);
    if (scroll + MAX_ROWS < nvis) gfx_text(152, ROW_Y0 + (MAX_ROWS - 1) * ROW_H, "v", COL_YELLOW, 1);

    gfx_text(2, 120, "L/R:change   SEL:next", COL_GRAY, 1);
}
