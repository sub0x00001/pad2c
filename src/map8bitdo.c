#include "usbpad.h"

#include <stddef.h>
#include <stdint.h>

/* 8BitDo Ultimate 2C dongle, report 1 (from its HID report descriptor and a
 * calibration on the console):
 *   [0] 01  [1..2] buttons 1-15  [3] hat (low nibble, 0 up clockwise, 15 none)
 *   [4] LX [5] LY [6] RX [7] RY (127 centre, up is smaller)  [8] RT [9] LT
 * Buttons: 1 A, 2 B, 3 L4, 4 X, 5 Y, 6 R4, 7 LB, 8 RB, 9 LT, 10 RT,
 *          11 View, 12 Menu, 13 Home, 14 L3, 15 R3.
 * Placed by position, as on a DualSense: A cross, B circle, X square, Y triangle. */
int usbpad_parse_8bitdo(const unsigned char *r, int n, pad_state *st)
{
    static const struct { uint16_t bit; uint32_t pad; } map[] = {
        { 1u << 0, PAD_CROSS },   { 1u << 1, PAD_CIRCLE }, { 1u << 2, PAD_TOUCHPAD },
        { 1u << 3, PAD_SQUARE },  { 1u << 4, PAD_TRIANGLE }, { 1u << 5, PAD_TOUCHPAD },
        { 1u << 6, PAD_L1 },      { 1u << 7, PAD_R1 },      { 1u << 8, PAD_L2 },
        { 1u << 9, PAD_R2 },      { 1u << 10, PAD_CREATE }, { 1u << 11, PAD_OPTIONS },
        { 1u << 12, PAD_PS },     { 1u << 13, PAD_L3 },     { 1u << 14, PAD_R3 },
    };
    static const uint32_t hat[8] = {
        PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_DOWN | PAD_RIGHT,
        PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_UP | PAD_LEFT,
    };
    unsigned b;
    size_t i;

    if (n < 10 || r[0] != 0x01) return 0;
    pad_state_reset(st);
    b = (unsigned)r[1] | (unsigned)r[2] << 8;
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (b & map[i].bit) st->buttons |= map[i].pad;
    if ((r[3] & 0x0F) < 8) st->buttons |= hat[r[3] & 0x0F];
    st->lx = r[4];
    st->ly = r[5];
    st->rx = r[6];
    st->ry = r[7];
    st->r2 = r[8];
    st->l2 = r[9];
    if (st->l2 > 24) st->buttons |= PAD_L2;
    if (st->r2 > 24) st->buttons |= PAD_R2;
    return 1;
}
