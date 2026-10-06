/* The 8BitDo mapping against reports captured on the console (calibration of
 * 5 October 2026: each button pressed alone, in order). Runs on the PC. */
#include "usbpad.h"

#include <stdio.h>

static int fails;

static void expect(const char *what, const unsigned char r[10], uint32_t buttons)
{
    pad_state st;

    if (!usbpad_parse_8bitdo(r, 10, &st)) {
        printf("FAIL %s: not parsed\n", what);
        fails++;
        return;
    }
    if (st.buttons != buttons) {
        printf("FAIL %s: buttons %#x, want %#x\n", what, (unsigned)st.buttons, (unsigned)buttons);
        fails++;
    }
}

#define R(b1, b2, hat, lx, ly, rx, ry, rt, lt) (const unsigned char[10]){ 1, b1, b2, hat, lx, ly, rx, ry, rt, lt }

int main(void)
{
    pad_state st;

    expect("idle",   R(0x00, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), 0);
    expect("A",      R(0x01, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_CROSS);
    expect("B",      R(0x02, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_CIRCLE);
    expect("X",      R(0x08, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_SQUARE);
    expect("Y",      R(0x10, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_TRIANGLE);
    expect("LB",     R(0x40, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_L1);
    expect("RB",     R(0x80, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_R1);
    expect("LT",     R(0x00, 0x01, 0x0f, 127, 127, 127, 127, 0, 255), PAD_L2);
    expect("RT",     R(0x00, 0x02, 0x0f, 127, 127, 127, 127, 255, 0), PAD_R2);
    expect("View",   R(0x00, 0x04, 0x0f, 127, 127, 127, 127, 0, 0), PAD_CREATE);
    expect("Menu",   R(0x00, 0x08, 0x0f, 127, 127, 127, 127, 0, 0), PAD_OPTIONS);
    expect("Home",   R(0x00, 0x10, 0x0f, 127, 127, 127, 127, 0, 0), PAD_PS);
    expect("L3",     R(0x00, 0x20, 0x0f, 127, 127, 127, 127, 0, 0), PAD_L3);
    expect("R3",     R(0x00, 0x40, 0x0f, 127, 127, 127, 127, 0, 0), PAD_R3);
    expect("up",     R(0x00, 0x00, 0x00, 127, 127, 127, 127, 0, 0), PAD_UP);
    expect("right",  R(0x00, 0x00, 0x02, 127, 127, 127, 127, 0, 0), PAD_RIGHT);
    expect("down",   R(0x00, 0x00, 0x04, 127, 127, 127, 127, 0, 0), PAD_DOWN);
    expect("left",   R(0x00, 0x00, 0x06, 127, 127, 127, 127, 0, 0), PAD_LEFT);
    expect("L4",     R(0x04, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_TOUCHPAD);
    expect("R4",     R(0x20, 0x00, 0x0f, 127, 127, 127, 127, 0, 0), PAD_TOUCHPAD);
    expect("LT half",R(0x00, 0x00, 0x0f, 127, 127, 127, 127, 0, 43), PAD_L2);

    usbpad_parse_8bitdo(R(0, 0, 0x0f, 126, 31, 224, 136, 7, 9), 10, &st);
    if (st.lx != 126 || st.ly != 31 || st.rx != 224 || st.ry != 136 || st.r2 != 7 || st.l2 != 9) {
        printf("FAIL sticks/triggers\n");
        fails++;
    }
    if (usbpad_parse_8bitdo((const unsigned char[10]){ 2 }, 10, &st)) {
        printf("FAIL: IDLE report 2 taken as a pad report\n");
        fails++;
    }
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
