#include "combo.h"

#define VM (PAD_CREATE | PAD_OPTIONS)

void combo_reset(combo *c)
{
    c->since = -1;
    c->held = 0;
    c->tap_until = 0;
    c->tap = 0;
    c->on = 0;
}

uint32_t combo_filter(combo *c, uint32_t buttons, long now)
{
    uint32_t vm = buttons & VM, rest = buttons & ~(uint32_t)VM;

    if (c->on) {
        if (!vm) c->on = 0;
        else return rest | PAD_TOUCHPAD;
    }
    if (vm == VM && c->since >= 0 && now - c->since < COMBO_WINDOW_MS) {
        c->on = 1;                              /* the second one came in time */
        c->since = -1;
        c->held = 0;
        return rest | PAD_TOUCHPAD;
    }
    if (vm == VM && c->since < 0 && !c->held && !c->tap) {
        c->on = 1;                              /* both in the same report */
        return rest | PAD_TOUCHPAD;
    }
    if (vm && vm != VM) {
        if (c->since < 0 && !c->held) {
            c->since = now;
            c->held = vm;
        }
        if (c->held && now - c->since < COMBO_WINDOW_MS) return rest;   /* held back */
        c->held = 0;                            /* held past the window: as it is */
        return buttons;
    }
    if (!vm && c->held) {                       /* a tap shorter than the window */
        c->tap = c->held;
        c->tap_until = now + COMBO_TAP_MS;
        c->held = 0;
        c->since = -1;
    }
    if (!vm) c->since = -1;
    if (c->tap) {
        if (now < c->tap_until) return rest | c->tap;
        c->tap = 0;
    }
    return buttons;
}
