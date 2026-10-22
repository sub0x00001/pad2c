/* The status JSON of the settings page. Runs on the PC. */
#include "web.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    web_pad p[2];
    char out[2048];

    memset(p, 0, sizeof p);
    pad_state_reset(&p[0].st);
    pad_state_reset(&p[1].st);
    web_status_json(p, 2, out, sizeof out);
    CHECK(strstr(out, "\"pads\":[]}") != NULL, "no pads: %s", out);

    p[1].present = 1;
    p[1].connected = 1;
    p[1].swap = 1;
    strcpy(p[1].name, "Pad \"X\" \\ 2");
    strcpy(p[1].mode, "XInput");
    strcpy(p[1].user, "Sub");
    p[1].st.buttons = 0x4000;
    web_status_json(p, 2, out, sizeof out);
    CHECK(strstr(out, "\"slot\":1,\"name\":\"Pad \\\"X\\\" \\\\ 2\"") != NULL, "escaping: %s", out);
    CHECK(strstr(out, "\"connected\":1,\"user\":\"Sub\",\"swap\":1,\"buttons\":16384") != NULL, "fields: %s", out);
    CHECK(strstr(out, "\"slot\":0") == NULL, "an empty slot listed: %s", out);
    CHECK(web_status_json(p, 2, out, 40) < 40 && strlen(out) < 40, "a small buffer overflowed");

    printf("%s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
