/* Interface choice and the XInput mapping. Runs on the PC.
 *
 * The 310a descriptor is the one in issue #2 (pad2c-probe on firmware 13.00):
 * the Ultimate 2C receiver in XInput mode. pad2c 0.2.1 read its keyboard. */
#include "usbdesc.h"
#include "usbpad.h"

#include <stdio.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* configuration, interface, HID, endpoint descriptors */
#define CFG(total, n)   9, 0x02, (total) & 0xFF, (total) >> 8, n, 1, 0, 0xA0, 0xFA
#define IFACE(i, n, c, s, p) 9, 0x04, i, 0, n, c, s, p, 0
#define HID             9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0x40, 0
#define EP(a, mps, iv)  7, 0x05, a, 0x03, (mps) & 0xFF, (mps) >> 8, iv

static const unsigned char desc_310a[] = {
    CFG(9 + 9 + 7 + 7 + 9 + 9 + 7 + 9 + 9 + 7 + 7, 3),
    IFACE(0, 2, 0xFF, 0x5D, 0x01), EP(0x84, 32, 1), EP(0x05, 32, 1),
    IFACE(1, 1, 0x03, 0x01, 0x01), HID, EP(0x82, 16, 8),
    IFACE(2, 2, 0x03, 0x00, 0x00), HID, EP(0x83, 64, 1), EP(0x06, 64, 1),
};

/* The receiver as 0.2.1 reads it: one HID interface. */
static const unsigned char desc_hid[] = {
    CFG(9 + 9 + 9 + 7 + 7, 1),
    IFACE(0, 2, 0x03, 0x00, 0x00), HID, EP(0x84, 64, 1), EP(0x03, 64, 1),
};

/* A boot keyboard and nothing else. */
static const unsigned char desc_kbd[] = {
    CFG(9 + 9 + 9 + 7, 1),
    IFACE(0, 1, 0x03, 0x01, 0x01), HID, EP(0x81, 8, 8),
};

static void test_choose(void)
{
    usbdesc_pick p;

    CHECK(usbdesc_choose(desc_310a, sizeof desc_310a, VID_8BITDO, &p) == USBPAD_XINPUT, "310a: not XInput");
    CHECK(p.iface == 0 && p.ep_in == 0x84 && p.mps_in == 32, "310a: in %#x/%u iface %u", p.ep_in, p.mps_in, p.iface);
    CHECK(p.ep_out == 0x05 && p.mps_out == 32, "310a: out %#x/%u", p.ep_out, p.mps_out);

    CHECK(usbdesc_choose(desc_310a, sizeof desc_310a, 0x045E, &p) == USBPAD_XINPUT, "XInput of another vendor");

    CHECK(usbdesc_choose(desc_hid, sizeof desc_hid, VID_8BITDO, &p) == USBPAD_8BITDO, "8BitDo HID: not chosen");
    CHECK(p.ep_in == 0x84 && p.mps_in == 64, "8BitDo HID: in %#x/%u", p.ep_in, p.mps_in);
    CHECK(usbdesc_choose(desc_hid, sizeof desc_hid, 0x1234, &p) == USBPAD_NONE, "HID of another vendor taken");

    CHECK(usbdesc_choose(desc_kbd, sizeof desc_kbd, VID_8BITDO, &p) == USBPAD_NONE, "boot keyboard taken");
    CHECK(usbdesc_choose(desc_310a, 20, VID_8BITDO, &p) == USBPAD_NONE, "truncated descriptor taken");
}

/* Xbox 360 report: 00 14, buttons, LT, RT, LX, LY, RX, RY (int16, y up) */
static void xreport(unsigned char r[20], unsigned b, int lt, int rt, int lx, int ly, int rx, int ry)
{
    int i;
    for (i = 0; i < 20; i++) r[i] = 0;
    r[0] = 0x00;
    r[1] = 0x14;
    r[2] = (unsigned char)b;
    r[3] = (unsigned char)(b >> 8);
    r[4] = (unsigned char)lt;
    r[5] = (unsigned char)rt;
    r[6] = (unsigned char)lx; r[7] = (unsigned char)(lx >> 8);
    r[8] = (unsigned char)ly; r[9] = (unsigned char)(ly >> 8);
    r[10] = (unsigned char)rx; r[11] = (unsigned char)(rx >> 8);
    r[12] = (unsigned char)ry; r[13] = (unsigned char)(ry >> 8);
}

static void xexpect(const char *what, unsigned b, uint32_t want)
{
    unsigned char r[20];
    pad_state st;

    xreport(r, b, 0, 0, 0, 0, 0, 0);
    if (!usbpad_parse_xinput(r, 20, &st)) {
        printf("FAIL %s: not parsed\n", what);
        fails++;
    } else if (st.buttons != want) {
        printf("FAIL %s: buttons %#x, want %#x\n", what, (unsigned)st.buttons, (unsigned)want);
        fails++;
    }
}

static void test_xinput(void)
{
    unsigned char r[20];
    pad_state st;

    xexpect("idle", 0, 0);
    xexpect("up", 0x0001, PAD_UP);
    xexpect("down", 0x0002, PAD_DOWN);
    xexpect("left", 0x0004, PAD_LEFT);
    xexpect("right", 0x0008, PAD_RIGHT);
    xexpect("Start", 0x0010, PAD_OPTIONS);
    xexpect("Back", 0x0020, PAD_CREATE);
    xexpect("L3", 0x0040, PAD_L3);
    xexpect("R3", 0x0080, PAD_R3);
    xexpect("LB", 0x0100, PAD_L1);
    xexpect("RB", 0x0200, PAD_R1);
    xexpect("Guide", 0x0400, PAD_PS);
    xexpect("A", 0x1000, PAD_CROSS);
    xexpect("B", 0x2000, PAD_CIRCLE);
    xexpect("X", 0x4000, PAD_SQUARE);
    xexpect("Y", 0x8000, PAD_TRIANGLE);

    xreport(r, 0, 0, 0, 0, 0, 0, 0);
    usbpad_parse_xinput(r, 20, &st);
    CHECK(st.lx == 128 && st.ly == 127 && st.rx == 128 && st.ry == 127, "centre %u %u %u %u", st.lx, st.ly, st.rx, st.ry);

    xreport(r, 0, 255, 30, -32768, 32767, 32767, -32768);
    usbpad_parse_xinput(r, 20, &st);
    CHECK(st.lx == 0 && st.ly == 0, "left stick up-left: %u %u", st.lx, st.ly);
    CHECK(st.rx == 255 && st.ry == 255, "right stick down-right: %u %u", st.rx, st.ry);
    CHECK(st.l2 == 255 && st.r2 == 30, "triggers %u %u", st.l2, st.r2);
    CHECK((st.buttons & (PAD_L2 | PAD_R2)) == (PAD_L2 | PAD_R2), "trigger buttons %#x", (unsigned)st.buttons);

    xreport(r, 0, 20, 0, 0, 0, 0, 0);
    usbpad_parse_xinput(r, 20, &st);
    CHECK(!(st.buttons & PAD_L2), "light LT pressed L2");

    r[0] = 0x01; r[1] = 0x03;           /* LED status, not input */
    CHECK(!usbpad_parse_xinput(r, 3, &st), "LED status taken as input");
    xreport(r, 0, 0, 0, 0, 0, 0, 0);
    CHECK(!usbpad_parse_xinput(r, 13, &st), "short report taken");
}

int main(void)
{
    test_choose();
    test_xinput();
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
