#include "web.h"
#include "log.h"
#include "util.h"
#include "version.h"
#include "webpage.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N_CLIENTS 8
#define REQ_MAX   2048
#define T_CLIENT  3000          /* ms a connection may take to send its request */

static int g_listen = -1;
static struct { int fd; char buf[REQ_MAX]; int len; long t0; } g_cl[N_CLIENTS];

int web_start(int port)
{
    struct sockaddr_in sin;
    int one = 1, i;

    for (i = 0; i < N_CLIENTS; i++) g_cl[i].fd = -1;
    g_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen < 0) {
        log_line("web: socket errno %d", errno);
        return 0;
    }
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listen, (struct sockaddr *)&sin, sizeof sin) != 0 || listen(g_listen, 8) != 0) {
        log_line("web: port %d: errno %d, no settings page", port, errno);
        close(g_listen);
        g_listen = -1;
        return 0;
    }
    fcntl(g_listen, F_SETFL, fcntl(g_listen, F_GETFL) | O_NONBLOCK);
    log_line("web: settings page on port %d", port);
    return 1;
}

void web_stop(void)
{
    int i;

    for (i = 0; i < N_CLIENTS; i++)
        if (g_cl[i].fd >= 0) {
            close(g_cl[i].fd);
            g_cl[i].fd = -1;
        }
    if (g_listen >= 0) close(g_listen);
    g_listen = -1;
}

/* A JSON string, with what JSON escapes (control characters dropped). */
static int json_str(char *out, int size, const char *s)
{
    int n = 0;

    if (size < 3) return 0;
    out[n++] = '"';
    for (; *s && n < size - 3; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (n >= size - 4) break;
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c >= 0x20) {
            out[n++] = (char)c;
        }
    }
    out[n++] = '"';
    out[n] = 0;
    return n;
}

int web_status_json(const web_pad *pads, int n, char *out, int size)
{
    int len, i, first = 1;

    len = snprintf(out, (size_t)size, "{\"version\":\"%s\",\"pads\":[", PADBRIDGE_VERSION);
    for (i = 0; i < n && len < size; i++) {
        const web_pad *p = &pads[i];
        char name[160], user[100];
        if (!p->present) continue;
        json_str(name, sizeof name, p->name);
        json_str(user, sizeof user, p->user);
        len += snprintf(out + len, (size_t)(size - len),
                        "%s{\"slot\":%d,\"name\":%s,\"mode\":\"%s\",\"connected\":%d,\"user\":%s,\"swap\":%d,"
                        "\"buttons\":%u,\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"l2\":%u,\"r2\":%u}",
                        first ? "" : ",", i, name, p->mode, p->connected ? 1 : 0, user, p->swap ? 1 : 0,
                        (unsigned)p->st.buttons, p->st.lx, p->st.ly, p->st.rx, p->st.ry, p->st.l2, p->st.r2);
        first = 0;
    }
    if (len < size) len += snprintf(out + len, (size_t)(size - len), "]}");
    return len < size ? len : size - 1;
}

static void send_all(int fd, const char *p, int n)
{
    long t0 = now_ms();

    while (n > 0 && now_ms() - t0 < 500) {
        ssize_t w = send(fd, p, (size_t)n, 0);
        if (w > 0) {
            p += w;
            n -= (int)w;
        } else if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
            usleep(1000);
        } else {
            return;
        }
    }
}

static void respond(int fd, const char *status, const char *type, const char *body, int len)
{
    char head[256];
    int h = snprintf(head, sizeof head,
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, type, len);
    send_all(fd, head, h);
    send_all(fd, body, len);
}

/* The value of `key` in a query string, -1 if absent. */
static int query_int(const char *path, const char *key)
{
    const char *q = strchr(path, '?');
    size_t k = strlen(key);

    while (q) {
        q++;
        if (!strncmp(q, key, k) && q[k] == '=') return atoi(q + k + 1);
        q = strchr(q, '&');
    }
    return -1;
}

static void handle(int fd, char *req, const web_pad *pads, int n, void (*set_swap)(int slot, int on))
{
    char method[8], path[256];
    static char json[4096];

    if (sscanf(req, "%7s %255s", method, path) != 2) {
        respond(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return;
    }
    if (!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/index.html"))) {
        respond(fd, "200 OK", "text/html; charset=utf-8", WEB_PAGE, (int)sizeof WEB_PAGE - 1);
    } else if (!strcmp(method, "GET") && !strncmp(path, "/api/status", 11)) {
        int len = web_status_json(pads, n, json, sizeof json);
        respond(fd, "200 OK", "application/json", json, len);
    } else if (!strcmp(method, "POST") && !strncmp(path, "/api/swap", 9)) {
        int slot = query_int(path, "slot"), on = query_int(path, "on");
        if (slot < 0 || slot >= n || !pads[slot].present || (on != 0 && on != 1)) {
            respond(fd, "400 Bad Request", "application/json", "{\"ok\":false}", 12);
            return;
        }
        set_swap(slot, on);
        respond(fd, "200 OK", "application/json", "{\"ok\":true}", 11);
    } else {
        respond(fd, "404 Not Found", "text/plain", "not found\n", 10);
    }
}

void web_poll(const web_pad *pads, int n, void (*set_swap)(int slot, int on))
{
    long now;
    int i;

    if (g_listen < 0) return;
    now = now_ms();
    for (;;) {                                  /* new connections */
        int fd = accept(g_listen, NULL, NULL), slot = -1;
        if (fd < 0) break;
        for (i = 0; i < N_CLIENTS; i++)
            if (g_cl[i].fd < 0) {
                slot = i;
                break;
            }
        if (slot < 0) {
            close(fd);                          /* too many at once */
            continue;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        g_cl[slot].fd = fd;
        g_cl[slot].len = 0;
        g_cl[slot].buf[0] = 0;
        g_cl[slot].t0 = now;
    }
    for (i = 0; i < N_CLIENTS; i++) {           /* what they sent */
        ssize_t r;
        if (g_cl[i].fd < 0) continue;
        r = recv(g_cl[i].fd, g_cl[i].buf + g_cl[i].len, (size_t)(REQ_MAX - 1 - g_cl[i].len), 0);
        if (r > 0) {
            g_cl[i].len += (int)r;
            g_cl[i].buf[g_cl[i].len] = 0;
        }
        if (strstr(g_cl[i].buf, "\r\n\r\n") || g_cl[i].len >= REQ_MAX - 1) {
            handle(g_cl[i].fd, g_cl[i].buf, pads, n, set_swap);
        } else if (!(r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) && now - g_cl[i].t0 < T_CLIENT) {
            continue;                           /* still coming */
        }
        close(g_cl[i].fd);
        g_cl[i].fd = -1;
        g_cl[i].len = 0;
        g_cl[i].buf[0] = 0;
    }
}

int web_local_ip(char *out, int size)
{
    struct sockaddr_in sin;
    socklen_t len = sizeof sin;
    int fd = socket(AF_INET, SOCK_DGRAM, 0), ok = 0;

    out[0] = 0;
    if (fd < 0) return 0;
    /* A UDP "connection" only picks the route: nothing is sent. */
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(53);
    sin.sin_addr.s_addr = htonl(0x08080808);
    if (connect(fd, (struct sockaddr *)&sin, sizeof sin) == 0 &&
        getsockname(fd, (struct sockaddr *)&sin, &len) == 0 && sin.sin_addr.s_addr != 0) {
        ok = inet_ntop(AF_INET, &sin.sin_addr, out, (socklen_t)size) != NULL;
    }
    close(fd);
    return ok;
}
