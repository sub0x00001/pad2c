/* One log line per call: to a file when opened with a path, else stderr. */
#ifndef PH_LOG_H
#define PH_LOG_H

int  log_open(const char *path);
void log_close(void);
void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
