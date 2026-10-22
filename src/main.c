/* pad2c -- the 8BitDo Ultimate 2C 2.4G dongle as a second DualSense on a
 * jailbroken PS5, read over USB (no Bluetooth involved).
 *
 *   usbpad.c    the dongle, read from its /dev/ugen node
 *   ps5_vpad.c  the virtual DualSense it drives (from AnyPad PS5, GPL-3.0)
 *
 * A virtual pad exists only while the controller is connected to the dongle:
 * it is made on the first controller report and removed when the dongle goes
 * back to "IDLE" (controller off or asleep) or is unplugged.
 *
 * "Turn off" from the console's menu removes the virtual pad, as a DualSense
 * disconnects; Home (PS) brings it back, as it switches a DualSense on.
 *
 * Stop: create /data/pad2c/stop. It also stops by itself before rest mode.
 * Log: /data/pad2c/pad2c.log
 */
#include "host.h"
#include "lock.h"
#include "log.h"
#include "ps5_power.h"
#include "config.h"
#include "ps5_vpad.h"
#include "usbpad.h"
#include "util.h"
#include "version.h"
#include "web.h"

#include <sys/stat.h>
#include <sys/syscall.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define STATE_DIR  "/data/pad2c"
#define LOG_PATH   STATE_DIR "/pad2c.log"
#define LOCK_PATH  STATE_DIR "/pad2c.lock"
#define STOP_FLAG  STATE_DIR "/stop"
#define CONFIG_PATH STATE_DIR "/config"
#define T_SCAN     100          /* ms between looks for new pads (the USB Wireless Adapter 2 shows
                                   its XInput identity for under a second) */
#define T_RESEND   50           /* ms: the last state is sent again at least this often */
/* No silence timeout: the dongle only reports changes, so a pad held still is
 * silent. When the controller sleeps or switches off, the dongle enumerates
 * again as "8BitDo IDLE", which ends the open device (usbpad_poll -> -1). */

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* One USB pad and the virtual DualSense it drives: slot i of ps5_vpad.c. */
typedef struct {
    usbpad *pad;
    pad_state st;
    long last_sent;
    int vpad_made;              /* a virtual pad was asked for and not removed since */
    int turned_off;             /* the console turned it off: Home brings it back */
    int ps_latch;               /* PS (Home) held since before: not a new press */
    char model[80];             /* "<name> (<mode>)", for the notifications */
    int swap;                   /* A/B and X/Y exchanged (config.h) */
    char user[40];              /* the console user's name, for the settings page */
    long user_at;               /* ms: when it was last looked up */
} ctl;

static ctl g_ctl[HOST_MAX_PADS];

/* From the settings page. */
static void set_swap(int slot, int on)
{
    ctl *c = &g_ctl[slot];
    const char *name;

    if (!c->pad) return;
    name = usbpad_name(c->pad);
    c->swap = on;
    log_line("slot %d: A/B and X/Y %s for \"%s\"%s", slot, on ? "swapped" : "as they are", name,
             config_set_swap(name, on) ? "" : " (not saved)");
}

/* What the settings page shows. */
static void web_fill(web_pad *w, long now)
{
    int i;

    for (i = 0; i < HOST_MAX_PADS; i++) {
        ctl *c = &g_ctl[i];
        memset(&w[i], 0, sizeof w[i]);
        if (!c->pad) continue;
        if (now - c->user_at >= 2000) {
            c->user_at = now;
            if (!c->vpad_made || !vpad_user_name(vpad_user(i), c->user, sizeof c->user)) c->user[0] = 0;
        }
        w[i].present = 1;
        w[i].connected = c->vpad_made;
        snprintf(w[i].name, sizeof w[i].name, "%s", usbpad_name(c->pad));
        snprintf(w[i].mode, sizeof w[i].mode, "%s", usbpad_mode(c->pad));
        snprintf(w[i].user, sizeof w[i].user, "%s", c->user);
        w[i].swap = c->swap;
        w[i].st = c->st;
    }
}

static void ctl_poll(int slot, long now)
{
    ctl *c = &g_ctl[slot];
    int r = usbpad_poll(c->pad, &c->st);

    if (r > 0 && c->swap) config_apply_swap(&c->st);

    if (r < 0) {
        usbpad_close(c->pad);
        c->pad = NULL;
        if (c->vpad_made) {
            vpad_remove(slot);
            c->vpad_made = 0;
            log_line("slot %d: controller left", slot);
            notify("%s disconnected", c->model);
        }
        c->turned_off = 0;              /* switched off for real: it starts afresh */
        pad_state_reset(&c->st);
    } else if (r > 0) {
        int ps_new = (c->st.buttons & PAD_PS) && !c->ps_latch;

        if (!(c->st.buttons & PAD_PS)) c->ps_latch = 0;
        if (!c->vpad_made && c->turned_off) {
            if (ps_new) {               /* that press only switches it on: not passed on */
                c->vpad_made = vpad_add(slot);
                c->turned_off = 0;
                c->ps_latch = 1;
                log_line("slot %d: PS pressed: virtual pad back on", slot);
            }
        } else if (!c->vpad_made) {
            c->vpad_made = vpad_add(slot);
            snprintf(c->model, sizeof c->model, "%s (%s)", usbpad_name(c->pad), usbpad_mode(c->pad));
            log_line("slot %d: controller connected: %s, virtual pad %s", slot, c->model,
                     c->vpad_made ? "requested" : "refused");
            notify("%s connected", c->model);
        }
        if (c->ps_latch) c->st.buttons &= ~(uint32_t)PAD_PS;
        if (c->vpad_made) vpad_update(slot, &c->st);
        c->last_sent = now;
    } else if (c->vpad_made && now - c->last_sent >= T_RESEND) {
        vpad_update(slot, &c->st);      /* keep the pad fed while nothing changes */
        c->last_sent = now;
    }
}

int main(void)
{
    const char *why = "stop requested";
    long last_scan = -T_SCAN, last_power = 0, last_check = 0;
    int i;

    mkdir(STATE_DIR, 0777);
    log_open(LOG_PATH);
    log_line("pad2c %s", PAD2C_VERSION);
    if (!lock_take(LOCK_PATH)) {
        log_line("another instance is running");
        notify("Already running");
        log_close();
        return 1;
    }
    unlink(STOP_FLAG);
    syscall(SYS_thr_set_name, -1, "pad2c");
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);

    if (!vpad_init()) {
        log_line("virtual pads not available; stopping");
        notify("Cannot create a virtual DualSense\nSee %s", LOG_PATH);
        lock_release();
        log_close();
        return 1;
    }
    config_load(CONFIG_PATH);
    {
        char ip[32];
        if (web_start(WEB_PORT) && web_local_ip(ip, sizeof ip))
            notify("Version %s running\nSettings: http://%s:%d", PAD2C_VERSION, ip, WEB_PORT);
        else
            notify("Version %s running\nSwitch the controller on with its dongle plugged in", PAD2C_VERSION);
    }
    for (i = 0; i < HOST_MAX_PADS; i++) pad_state_reset(&g_ctl[i].st);

    while (!g_stop) {
        long now = now_ms();

        /* New pads, while a slot is free: each gets the first free one. */
        if (now - last_scan >= T_SCAN) {
            last_scan = now;
            for (i = 0; i < HOST_MAX_PADS; i++) {
                if (g_ctl[i].pad || g_ctl[i].vpad_made) continue;
                if ((g_ctl[i].pad = usbpad_open())) {
                    g_ctl[i].turned_off = 0;
                    g_ctl[i].swap = config_swap(usbpad_name(g_ctl[i].pad));
                    g_ctl[i].user_at = -10000;
                    log_line("slot %d: %s%s", i, usbpad_name(g_ctl[i].pad), g_ctl[i].swap ? ", A/B and X/Y swapped" : "");
                }
                break;
            }
        }
        for (i = 0; i < HOST_MAX_PADS; i++)
            if (g_ctl[i].pad) ctl_poll(i, now);
        vpad_poll(now);
        {
            web_pad w[HOST_MAX_PADS];
            web_fill(w, now);
            web_poll(w, HOST_MAX_PADS, set_swap);
        }

        /* Turned off from the console's menu (or the console dropped it): the
         * virtual pad goes away, as a DualSense disconnects. */
        for (i = 0; i < HOST_MAX_PADS; i++) {
            ctl *c = &g_ctl[i];
            if (!c->vpad_made || !vpad_off(i)) continue;
            vpad_remove(i);
            c->vpad_made = 0;
            c->turned_off = 1;
            c->ps_latch = (c->st.buttons & PAD_PS) != 0;
            log_line("slot %d: virtual pad turned off; waiting for PS (Home)", i);
            notify("%s turned off\nPress Home to turn it back on", c->model);
        }

        if (now - last_power >= 200) {
            int ps = power_state();
            last_power = now;
            if (ps == POWER_GOING_TO_REST || ps == POWER_IN_REST) {
                why = "rest mode";
                break;
            }
        }
        if (now - last_check >= 1000) {
            last_check = now;
            if (access(STOP_FLAG, F_OK) == 0) {
                unlink(STOP_FLAG);
                break;
            }
        }
        usleep(2000);
    }

    log_line("stopping: %s", why);
    for (i = 0; i < HOST_MAX_PADS; i++) {
        if (g_ctl[i].vpad_made) vpad_remove(i);
        usbpad_close(g_ctl[i].pad);
    }
    web_stop();
    vpad_shutdown();
    vpad_creds_restore();
    lock_release();
    log_line("stopped");
    log_close();
    notify("Stopped (%s)", why);
    return 0;
}
