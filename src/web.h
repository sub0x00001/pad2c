/* pad2c's settings page, http://<console>:8090 from any browser on the local
 * network (phone, computer, the console's own).
 *
 *   GET  /                      the page
 *   GET  /api/status            the pads now, as JSON (the page asks ~10 times a second)
 *   POST /api/swap?slot=N&on=1  A/B and X/Y exchanged for the pad in slot N (0 to undo)
 *
 * Never blocks: web_poll() takes what has arrived and answers what is
 * complete, so a slow browser does not delay the pads. */
#ifndef PAD2C_WEB_H
#define PAD2C_WEB_H

#include "pad.h"

#include <stdint.h>

#define WEB_PORT 8090

typedef struct {
    int present;                /* a pad is open in this slot */
    int connected;              /* its virtual DualSense exists */
    char name[64];
    char mode[8];               /* DInput, XInput */
    char user[40];              /* the console user it is given to, "" none */
    int swap;
    pad_state st;
} web_pad;

int  web_start(int port);       /* 0 if it cannot listen (logged) */
void web_poll(const web_pad *pads, int n, void (*set_swap)(int slot, int on));
void web_stop(void);

/* The console's address on the local network, for the notification. 0 if unknown. */
int  web_local_ip(char *out, int size);

/* The JSON for /api/status, into out (size bytes). Its length. */
int  web_status_json(const web_pad *pads, int n, char *out, int size);

#endif
