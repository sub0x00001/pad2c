#include "log.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The log is kept under LOG_MAX bytes: past it, it becomes path.1 (the
 * one before is dropped) and a new one starts. */
#define LOG_MAX (1L << 20)

static FILE *g_log;
static char g_path[256];

int log_open(const char *path)
{
    if (!path) return 1;
    snprintf(g_path, sizeof g_path, "%s", path);
    g_log = fopen(path, "a");
    return g_log != NULL;
}

void log_close(void)
{
    if (g_log) fclose(g_log);
    g_log = NULL;
}

static void rotate(void)
{
    char old[sizeof g_path + 2];

    if (!g_log || ftell(g_log) < LOG_MAX) return;
    fclose(g_log);
    snprintf(old, sizeof old, "%s.1", g_path);
    remove(old);
    rename(g_path, old);
    g_log = fopen(g_path, "a");
}

void log_line(const char *fmt, ...)
{
    FILE *out;
    va_list ap;

    rotate();
    out = g_log ? g_log : stderr;
    fprintf(out, "[%8ld] ", now_ms());
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}
