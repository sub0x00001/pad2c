/* The Nintendo Switch Pro Controller over USB (057e:2009): what the 8BitDo USB
 * Wireless Adapter 2 shows in its Switch mode, and controllers wired in their
 * Switch mode. Protocol as documented by dekuNukem's Nintendo Switch reverse
 * engineering notes and implemented by SDL (zlib licence) and Linux's
 * hid-nintendo.
 *
 * Report 0x30 (and 0x21, a subcommand's reply, which starts the same):
 *   [3] Y X B A SR SL R ZR   [4] - + RS LS Home Capture   [5] down up right left SR SL L ZL
 *   [6..8] left stick, [9..11] right stick: two 12-bit values each, y grows upwards
 * Placed by position, as on a DualSense: B cross, A circle, Y square, X triangle;
 * Capture is the touchpad. */
#ifndef PADBRIDGE_MAPSWITCH_H
#define PADBRIDGE_MAPSWITCH_H

#include "pad.h"

#include <stdint.h>

/* Stick calibration, learnt as the pad is used: the centre from the first
 * report (sticks at rest), the reach from the furthest seen, at least
 * SWITCH_STICK_REACH. */
#define SWITCH_STICK_REACH 1500

typedef struct {
    int set;
    int centre[4], reach[4];        /* lx ly rx ry */
} switch_sticks;

void switch_sticks_reset(switch_sticks *c);

/* Report 0x30 or 0x21 to a DualSense state. 0 if it is neither. */
int switch_parse(const unsigned char *r, int n, switch_sticks *cal, pad_state *st);

/* The 8 rumble bytes (left and right, the same) for a large and a small motor
 * strength, 0..255: SDL's encoding and tables. The high band follows the small
 * motor; the low band the stronger of the two (third-party pads hardly feel
 * the high band). */
void switch_rumble_encode(uint8_t large, uint8_t small, unsigned char out[8]);

/* A motor strength lifted over the threshold of simple rumble motors: 0 stays
 * 0, anything else goes from SWITCH_RUMBLE_FLOOR to 255. A weak request (Crash
 * Bandicoot's spin asks 63) is otherwise not felt at all on a third-party pad
 * through the adapter. */
#define SWITCH_RUMBLE_FLOOR 100
uint8_t switch_rumble_boost(uint8_t v);

#endif
