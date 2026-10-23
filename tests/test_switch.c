/* The Switch Pro Controller's report 0x30 and its rumble. Runs on the PC. */
#include "mapswitch.h"
#include "usbdesc.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* A report 0x30 with buttons (right, shared, left bytes) and raw 12-bit sticks. */
static void report(unsigned char r[64], int right, int shared, int left, int lx, int ly, int rx, int ry)
{
    memset(r, 0, 64);
    r[0] = 0x30;
    r[3] = (unsigned char)right;
    r[4] = (unsigned char)shared;
    r[5] = (unsigned char)left;
    r[6] = (unsigned char)(lx & 0xFF);
    r[7] = (unsigned char)(((lx >> 8) & 0x0F) | ((ly & 0x0F) << 4));
    r[8] = (unsigned char)(ly >> 4);
    r[9] = (unsigned char)(rx & 0xFF);
    r[10] = (unsigned char)(((rx >> 8) & 0x0F) | ((ry & 0x0F) << 4));
    r[11] = (unsigned char)(ry >> 4);
}

static uint32_t buttons(int right, int shared, int left)
{
    unsigned char r[64];
    switch_sticks cal;
    pad_state st;

    switch_sticks_reset(&cal);
    report(r, right, shared, left, 2048, 2048, 2048, 2048);
    if (!switch_parse(r, 64, &cal, &st)) return 0xFFFFFFFFu;
    return st.buttons;
}

static void test_descriptor(void)
{
    static const unsigned char pro[] = {
        9, 0x02, 41, 0, 1, 1, 0, 0xC0, 0xFA,
        9, 0x04, 0, 0, 2, 0x03, 0, 0, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0xCB, 0,
        7, 0x05, 0x81, 0x03, 64, 0, 4,
        7, 0x05, 0x02, 0x03, 64, 0, 8,
    };
    usbdesc_pick p;

    CHECK(usbdesc_choose(pro, sizeof pro, 0x057E, 0x2009, &p) == USBPAD_SWITCH, "Pro Controller not chosen");
    CHECK(p.ep_in == 0x81 && p.ep_out == 0x02 && p.mps_out == 64, "Pro endpoints %#x %#x", p.ep_in, p.ep_out);
    CHECK(usbdesc_choose(pro, sizeof pro, 0x057E, 0x2017, &p) == USBPAD_NONE, "another Nintendo pid taken");
}

static void test_buttons(void)
{
    /* by position, as on a DualSense: B bottom cross, A right circle, Y left square, X top triangle */
    CHECK(buttons(0x04, 0, 0) == PAD_CROSS, "B");
    CHECK(buttons(0x08, 0, 0) == PAD_CIRCLE, "A");
    CHECK(buttons(0x01, 0, 0) == PAD_SQUARE, "Y");
    CHECK(buttons(0x02, 0, 0) == PAD_TRIANGLE, "X");
    CHECK(buttons(0x40, 0, 0) == PAD_R1, "R");
    CHECK(buttons(0x80, 0, 0) == (PAD_R2), "ZR");
    CHECK(buttons(0, 0, 0x40) == PAD_L1, "L");
    CHECK(buttons(0, 0, 0x80) == PAD_L2, "ZL");
    CHECK(buttons(0, 0x01, 0) == PAD_CREATE, "Minus");
    CHECK(buttons(0, 0x02, 0) == PAD_OPTIONS, "Plus");
    CHECK(buttons(0, 0x04, 0) == PAD_R3, "R stick");
    CHECK(buttons(0, 0x08, 0) == PAD_L3, "L stick");
    CHECK(buttons(0, 0x10, 0) == PAD_PS, "Home");
    CHECK(buttons(0, 0x20, 0) == PAD_TOUCHPAD, "Capture");
    CHECK(buttons(0, 0, 0x01) == PAD_DOWN, "down");
    CHECK(buttons(0, 0, 0x02) == PAD_UP, "up");
    CHECK(buttons(0, 0, 0x04) == PAD_RIGHT, "right");
    CHECK(buttons(0, 0, 0x08) == PAD_LEFT, "left");
    CHECK(buttons(0x30, 0, 0x30) == 0, "SL/SR (Joy-Con rail) ignored");
}

static void test_triggers_and_sticks(void)
{
    unsigned char r[64];
    switch_sticks cal;
    pad_state st;

    switch_sticks_reset(&cal);
    report(r, 0x80, 0, 0x80, 2048, 2048, 2048, 2048);
    switch_parse(r, 64, &cal, &st);
    CHECK(st.l2 == 255 && st.r2 == 255, "ZL/ZR analog %u %u", st.l2, st.r2);

    /* the first report sets the centre: here off 2048, as a real pad is */
    switch_sticks_reset(&cal);
    report(r, 0, 0, 0, 1990, 2110, 2010, 2070);
    switch_parse(r, 64, &cal, &st);
    CHECK(st.lx == 128 && st.ly == 128 && st.rx == 128 && st.ry == 128, "centre %u %u %u %u", st.lx, st.ly, st.rx, st.ry);

    /* full left and up (y grows upwards on a Switch pad) */
    report(r, 0, 0, 0, 1990 - 1500, 2110 + 1500, 2010 + 1500, 2070 - 1500);
    switch_parse(r, 64, &cal, &st);
    CHECK(st.lx == 0 && st.ly == 0, "left stick up-left %u %u", st.lx, st.ly);
    CHECK(st.rx == 255 && st.ry == 255, "right stick down-right %u %u", st.rx, st.ry);

    /* half way stays about half way */
    report(r, 0, 0, 0, 1990 + 650, 2110, 2010, 2070);
    switch_parse(r, 64, &cal, &st);
    CHECK(st.lx > 180 && st.lx < 210, "half right %u", st.lx);

    /* a reply to a subcommand (0x21) carries the same controller state */
    report(r, 0x04, 0, 0, 1990, 2110, 2010, 2070);
    r[0] = 0x21;
    CHECK(switch_parse(r, 64, &cal, &st) && st.buttons == PAD_CROSS, "0x21 state");
    r[0] = 0x81;
    CHECK(!switch_parse(r, 64, &cal, &st), "0x81 taken as state");
    CHECK(!switch_parse(r, 8, &cal, &st), "a short report taken");
}

static void test_rumble(void)
{
    unsigned char d[8];
    static const unsigned char neutral[8] = { 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40 };

    switch_rumble_encode(0, 0, d);
    CHECK(!memcmp(d, neutral, 8), "no rumble is the neutral value");

    switch_rumble_encode(255, 255, d);
    CHECK(!memcmp(d, d + 4, 4), "both sides the same");
    CHECK(d[0] == 0x74 && (d[1] & 0xFE) == 0xC8, "full high band: %02x %02x", d[0], d[1]);
    CHECK((d[2] & 0x7F) == 0x3D && d[3] == 0x72, "full low band: %02x %02x", d[2], d[3]);

    /* the small motor alone (Crash's spin: 0/63) must reach the low band too:
     * third-party pads (the GameSir G8 through the adapter) hardly feel the
     * high band */
    switch_rumble_encode(0, 63, d);
    CHECK((d[1] & 0xFE) != 0, "small motor: high band amplitude (%02x)", d[1]);
    CHECK(d[3] != 0x40 || (d[2] & 0x80), "small motor alone: low band silent (%02x %02x)", d[2], d[3]);

    /* weak requests are lifted over the motors' threshold: 63 (25%) ends up
     * well past the low band's quarter */
    switch_rumble_encode(0, 63, d);
    CHECK(((d[2] & 0x80) << 1 | d[3]) >= 0x0060, "a weak request stays below the threshold (%02x %02x)", d[2], d[3]);
    CHECK(switch_rumble_boost(0) == 0 && switch_rumble_boost(255) == 255, "boost ends");
    CHECK(switch_rumble_boost(1) >= SWITCH_RUMBLE_FLOOR, "boost floor");

    switch_rumble_encode(128, 0, d);
    CHECK(memcmp(d, neutral, 4) != 0, "the large motor alone rumbles");
    CHECK((d[1] & 0xFE) == 0x00, "large motor only: no high band amplitude (%02x)", d[1]);
}

int main(void)
{
    test_descriptor();
    test_buttons();
    test_triggers_and_sticks();
    test_rumble();
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
