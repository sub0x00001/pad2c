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
#include "lock.h"
#include "log.h"
#include "ps5_power.h"
#include "ps5_vpad.h"
#include "usbpad.h"
#include "util.h"
#include "version.h"

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
#define SLOT       0
#define T_SCAN     1000         /* ms between looks for the dongle */
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

int main(void)
{
    usbpad *pad = NULL;
    pad_state st;
    const char *why = "stop requested";
    long last_scan = -T_SCAN, last_power = 0, last_check = 0, last_sent = 0;
    int vpad_made = 0;          /* a virtual pad was asked for and not removed since */
    int turned_off = 0;         /* the console turned it off: Home brings it back */
    int ps_latch = 0;           /* PS (Home) held since before: not a new press */

    mkdir(STATE_DIR, 0777);
    log_open(LOG_PATH);
    log_line("pad2c %s", PAD2C_VERSION);
    if (!lock_take(LOCK_PATH)) {
        log_line("another instance is running");
        notify("pad2c is already running");
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
        notify("pad2c %s: cannot create a virtual DualSense (see %s)", PAD2C_VERSION, LOG_PATH);
        lock_release();
        log_close();
        return 1;
    }
    notify("pad2c %s running\nSwitch the 8BitDo on (2.4G mode) with its dongle plugged in", PAD2C_VERSION);
    pad_state_reset(&st);

    while (!g_stop) {
        long now = now_ms();
        int r = 0;

        if (!pad && now - last_scan >= T_SCAN) {
            last_scan = now;
            pad = usbpad_open();
        }
        if (pad) {
            r = usbpad_poll(pad, &st);
            if (r < 0) {
                usbpad_close(pad);
                pad = NULL;
                if (vpad_made) {
                    vpad_remove(SLOT);
                    vpad_made = 0;
                    log_line("controller left");
                    notify("pad2c: 8BitDo disconnected");
                }
                turned_off = 0;             /* switched off for real: it starts afresh */
                pad_state_reset(&st);
            } else if (r > 0) {
                int ps_new = (st.buttons & PAD_PS) && !ps_latch;

                if (!(st.buttons & PAD_PS)) ps_latch = 0;
                if (!vpad_made && turned_off) {
                    if (ps_new) {           /* that press only switches it on: not passed on */
                        vpad_made = vpad_add(SLOT);
                        turned_off = 0;
                        ps_latch = 1;
                        log_line("PS pressed: virtual pad back on");
                    }
                } else if (!vpad_made) {
                    vpad_made = vpad_add(SLOT);
                    log_line("controller connected (%s), virtual pad %s", usbpad_name(pad),
                             vpad_made ? "requested" : "refused");
                    notify("pad2c: 8BitDo connected");
                }
                if (ps_latch) st.buttons &= ~(uint32_t)PAD_PS;
                if (vpad_made) vpad_update(SLOT, &st);
                last_sent = now;
            } else if (vpad_made && now - last_sent >= T_RESEND) {
                vpad_update(SLOT, &st);     /* keep the pad fed while nothing changes */
                last_sent = now;
            }
        }
        vpad_poll(now);

        /* Turned off from the console's menu (or the console dropped it): the
         * virtual pad goes away, as a DualSense disconnects. */
        if (vpad_made && vpad_off(SLOT)) {
            vpad_remove(SLOT);
            vpad_made = 0;
            turned_off = 1;
            ps_latch = (st.buttons & PAD_PS) != 0;
            log_line("virtual pad turned off; waiting for PS (Home)");
            notify("pad2c: 8BitDo turned off\nPress Home on the controller to turn it back on");
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
        if (r <= 0) usleep(2000);
    }

    log_line("stopping: %s", why);
    if (vpad_made) vpad_remove(SLOT);
    usbpad_close(pad);
    vpad_shutdown();
    vpad_creds_restore();
    lock_release();
    log_line("stopped");
    log_close();
    notify("pad2c stopped (%s)", why);
    return 0;
}
