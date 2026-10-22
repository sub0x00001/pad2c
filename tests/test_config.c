/* The settings file and the A/B, X/Y swap. Runs on the PC. */
#include "config.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    static const char *path = "build/test_config.txt";
    pad_state st;
    FILE *f;

    remove(path);
    config_load(path);
    CHECK(!config_swap("8BitDo USB Wireless Adapter 2"), "swap on with no file");

    CHECK(config_set_swap("8BitDo USB Wireless Adapter 2", 1), "set");
    CHECK(config_swap("8BitDo USB Wireless Adapter 2"), "swap off after set");
    CHECK(!config_swap("8BitDo Ultimate 2C Wireless Controller"), "another pad swapped");

    config_load(path);                   /* read back from the file */
    CHECK(config_swap("8BitDo USB Wireless Adapter 2"), "swap lost on reload");

    CHECK(config_set_swap("8BitDo USB Wireless Adapter 2", 0), "unset");
    config_load(path);
    CHECK(!config_swap("8BitDo USB Wireless Adapter 2"), "swap kept after unset");

    /* lines it does not know are kept, a damaged file is no harm */
    f = fopen(path, "w");
    fputs("# padbridge settings\nfuture-option 7\nswap Some Pad\n\nswap\n", f);
    fclose(f);
    config_load(path);
    CHECK(config_swap("Some Pad"), "swap from a hand-written file");
    config_set_swap("Other Pad", 1);
    config_load(path);
    CHECK(config_swap("Some Pad") && config_swap("Other Pad"), "a swap lost on save");

    pad_state_reset(&st);
    st.buttons = PAD_CROSS | PAD_SQUARE | PAD_UP | PAD_L1;
    config_apply_swap(&st);
    CHECK(st.buttons == (PAD_CIRCLE | PAD_TRIANGLE | PAD_UP | PAD_L1), "swap: %#x", (unsigned)st.buttons);
    config_apply_swap(&st);
    CHECK(st.buttons == (PAD_CROSS | PAD_SQUARE | PAD_UP | PAD_L1), "swap twice: %#x", (unsigned)st.buttons);

    remove(path);
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
