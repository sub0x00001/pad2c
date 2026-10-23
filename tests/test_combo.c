/* View + Menu as the touchpad, for pads without one (XInput). Runs on the PC. */
#include "combo.h"

#include <stdio.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define V PAD_CREATE
#define M PAD_OPTIONS
#define T PAD_TOUCHPAD

int main(void)
{
    combo c;
    uint32_t o;
    long t;

    /* both at once: the touchpad, neither of them */
    combo_reset(&c);
    CHECK(combo_filter(&c, V | M | PAD_CROSS, 0) == (T | PAD_CROSS), "both at once");
    CHECK(combo_filter(&c, V | M, 30) == T, "both held");
    CHECK(combo_filter(&c, V, 40) == T, "one let go: still the touchpad until both are");
    CHECK(combo_filter(&c, 0, 50) == 0, "both let go");
    CHECK(combo_filter(&c, V, 200) == 0, "View again: held back");

    /* View first, Menu within the window: the touchpad, no View on the way */
    combo_reset(&c);
    CHECK(combo_filter(&c, V, 0) == 0, "View alone at first: held back");
    CHECK(combo_filter(&c, V, 30) == 0, "View alone, 30 ms: held back");
    CHECK(combo_filter(&c, V | M, 45) == T, "Menu joins: touchpad");
    CHECK(combo_filter(&c, 0, 120) == 0, "released");

    /* View held alone past the window: View, as usual */
    combo_reset(&c);
    combo_filter(&c, V, 0);
    CHECK(combo_filter(&c, V, COMBO_WINDOW_MS) == V, "View held past the window");
    CHECK(combo_filter(&c, V, 500) == V, "View still held");
    CHECK(combo_filter(&c, V | M, 520) == (V | M), "Menu late: both as they are");
    CHECK(combo_filter(&c, 0, 600) == 0, "released");

    /* a quick tap of Menu, shorter than the window, is not lost */
    combo_reset(&c);
    CHECK(combo_filter(&c, M, 1000) == 0, "tap: held back");
    o = combo_filter(&c, 0, 1030);
    CHECK(o == M, "tap released early: sent now (%#x)", (unsigned)o);
    for (t = 1031; t < 1030 + COMBO_TAP_MS; t += 10)
        if (combo_filter(&c, 0, t) != M) { CHECK(0, "tap not held for its length at %ld", t); break; }
    CHECK(combo_filter(&c, 0, 1030 + COMBO_TAP_MS) == 0, "tap over");

    /* other buttons go through untouched */
    combo_reset(&c);
    CHECK(combo_filter(&c, PAD_CROSS | PAD_L1, 0) == (PAD_CROSS | PAD_L1), "others");

    printf("%s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
