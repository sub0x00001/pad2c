#include "config.h"

#include <stdio.h>
#include <string.h>

#define MAX_LINES 64
#define LINE_LEN  96

static char g_path[128];
static char g_lines[MAX_LINES][LINE_LEN];      /* the file as read, "swap ..." lines included */
static int g_n;

void config_load(const char *path)
{
    FILE *f;
    char line[LINE_LEN];

    snprintf(g_path, sizeof g_path, "%s", path);
    g_n = 0;
    if (!(f = fopen(path, "r"))) return;
    while (g_n < MAX_LINES && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        snprintf(g_lines[g_n++], LINE_LEN, "%s", line);
    }
    fclose(f);
}

static int is_swap_of(const char *line, const char *name)
{
    return !strncmp(line, "swap ", 5) && !strcmp(line + 5, name);
}

int config_swap(const char *name)
{
    int i;

    for (i = 0; i < g_n; i++)
        if (is_swap_of(g_lines[i], name)) return 1;
    return 0;
}

static int save(void)
{
    char tmp[sizeof g_path + 4];
    FILE *f;
    int i;

    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    if (!(f = fopen(tmp, "w"))) return 0;
    for (i = 0; i < g_n; i++) fprintf(f, "%s\n", g_lines[i]);
    if (fclose(f) != 0) return 0;
    return rename(tmp, g_path) == 0;
}

int config_set_swap(const char *name, int on)
{
    int i;

    if (!name[0] || strlen(name) + 6 > LINE_LEN) return 0;
    if (on == config_swap(name)) return 1;
    if (on) {
        if (g_n == MAX_LINES) return 0;
        snprintf(g_lines[g_n++], LINE_LEN, "swap %s", name);
    } else {
        for (i = 0; i < g_n; i++)
            if (is_swap_of(g_lines[i], name)) {
                memmove(g_lines[i], g_lines[i + 1], (size_t)(g_n - i - 1) * LINE_LEN);
                g_n--;
                i--;
            }
    }
    return save();
}

void config_apply_swap(pad_state *st)
{
    uint32_t b = st->buttons, out = b & ~(uint32_t)(PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE);

    if (b & PAD_CROSS) out |= PAD_CIRCLE;
    if (b & PAD_CIRCLE) out |= PAD_CROSS;
    if (b & PAD_SQUARE) out |= PAD_TRIANGLE;
    if (b & PAD_TRIANGLE) out |= PAD_SQUARE;
    st->buttons = out;
}
