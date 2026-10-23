/* View + Menu (Create + Options) pressed together as the touchpad click, for
 * pads that have no touchpad and no spare button to stand for it: XInput
 * pads (the Ultimate 2C in XInput mode sends no L4/R4; Star and Mapping never
 * reach the console in either mode).
 *
 * Nobody presses two buttons in the same millisecond: View or Menu alone is
 * held back for COMBO_WINDOW_MS, so that the console does not see a Create
 * press on the way to the touchpad (it would open the Create menu). Held
 * longer, or with the other one late, they go through as they are; a tap
 * shorter than the window is sent when it ends, for COMBO_TAP_MS. Once the
 * touchpad is on, it stays until both are released. */
#ifndef PADBRIDGE_COMBO_H
#define PADBRIDGE_COMBO_H

#include "pad.h"

#define COMBO_WINDOW_MS 60
#define COMBO_TAP_MS    60

typedef struct {
    long since;                 /* ms: View or Menu alone since then, -1 if not */
    uint32_t held;              /* which of them is being held back */
    long tap_until;             /* ms: a short tap being sent until then */
    uint32_t tap;
    int on;                     /* the touchpad is on */
} combo;

void     combo_reset(combo *c);
uint32_t combo_filter(combo *c, uint32_t buttons, long now);

#endif
