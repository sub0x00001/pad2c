#include "usbpad.h"
#include "log.h"
#include "usbdesc.h"
#include "util.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N_RD        4           /* reads kept pending on the IN endpoint */
#define EP_OUT      N_RD        /* transfer index of the OUT endpoint, if any */
#define RD_MAX      64
#define T_ALIVE     1000        /* ms between checks that the device is still there */
#define N_BUS       4
#define N_ADDR      16

/* Per /dev/ugenB.A: held by an open usbpad, or looked at and not a pad. A
 * node not a pad is not opened again until it changes (a device enumerating
 * anew gets a new node), so the scan can run often: the USB Wireless
 * Adapter 2 shows its XInput identity for under a second. */
static unsigned char g_held[N_BUS][N_ADDR];
static struct { ino_t ino; long ctime, seen; unsigned char set; } g_not_pad[N_BUS][N_ADDR];
static char g_reject[96];           /* what try_node last found not to be a pad */
static char g_logged[N_BUS][N_ADDR][96];    /* per node: the last one logged (a Switch Pro pad
                                               not set up enumerates again every few seconds) */
#define T_NOT_PAD   2000        /* ms a node found not to be a pad is left alone, unless it changes */

struct usbpad {
    int fd;
    int bus, addr;
    char path[24];
    char name[64];
    int kind;                   /* USBPAD_* (usbdesc.h) */
    uint8_t ep_in, ep_out;
    uint16_t mps, mps_out;
    struct usb_fs_endpoint ep[N_RD + 1];
    void *bufp[N_RD + 1][1];
    uint32_t lenp[N_RD + 1][1];
    unsigned char buf[N_RD + 1][RD_MAX];
    unsigned char busy[N_RD + 1];
    long t_alive;
    int logged_first;
    unsigned char rumble[2], rumble_sent[2];    /* large, small motor: wanted, last sent */
    int rumble_pending;
};

static int dead_errno(int e)
{
    return e == ENXIO || e == ENODEV || e == EIO || e == EBADF || e == ENOENT;
}

/* The interface to read, from the full configuration descriptor. */
static int choose_interface(int fd, uint16_t vid, usbdesc_pick *p)
{
    struct usb_gen_descriptor gd;
    unsigned char cfg[1024];
    int len;

    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = sizeof cfg;
    gd.ugd_config_index = 0xFF;
    if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0) return USBPAD_NONE;
    len = gd.ugd_actlen < (int)sizeof cfg ? gd.ugd_actlen : (int)sizeof cfg;
    return usbdesc_choose(cfg, len, vid, p);
}

/* Sends one interrupt OUT transfer and waits for it, a short while. */
static int send_out(usbpad *u, const unsigned char *data, int n)
{
    struct usb_fs_start st;
    struct usb_fs_complete done;
    long t0;

    if (!u->ep_out || n > u->mps_out || n > RD_MAX) return 0;
    memcpy(u->buf[EP_OUT], data, (size_t)n);
    u->lenp[EP_OUT][0] = (uint32_t)n;
    u->ep[EP_OUT].nFrames = 1;
    u->ep[EP_OUT].aFrames = 0;
    u->ep[EP_OUT].status = 0;
    memset(&st, 0, sizeof st);
    st.ep_index = EP_OUT;
    if (ioctl(u->fd, USB_FS_START, &st) != 0) return 0;
    u->busy[EP_OUT] = 1;
    for (t0 = now_ms(); now_ms() - t0 < 100; usleep(1000)) {
        memset(&done, 0, sizeof done);
        if (ioctl(u->fd, USB_FS_COMPLETE, &done) != 0) continue;
        u->busy[done.ep_index] = 0;
        if (done.ep_index == EP_OUT) return u->ep[EP_OUT].status == 0;
    }
    return 0;
}

/* A control transfer from the device; the bytes read, -1 if it failed. */
static int ctrl_in(int fd, int type, int req, int value, int index, unsigned char *buf, int len)
{
    struct usb_ctl_request r;

    memset(&r, 0, sizeof r);
    r.ucr_data = buf;
    r.ucr_flags = 0x0004;                       /* USB_SHORT_XFER_OK (usbdi.h, kernel-only header) */
    r.ucr_request.bmRequestType = (uint8_t)type;
    r.ucr_request.bRequest = (uint8_t)req;
    USETW(r.ucr_request.wValue, value);
    USETW(r.ucr_request.wIndex, index);
    USETW(r.ucr_request.wLength, len);
    if (ioctl(fd, USB_DO_REQUEST, &r) != 0) return -1;
    return r.ucr_actlen;
}

/* What Windows asks of an XInput device when it arrives: the Microsoft OS
 * string descriptor (0xEE) and the XUSB driver's vendor request. The USB
 * Wireless Adapter 2 picks its mode from the host it sees ("automatic mode
 * switch supports Windows and Switch"): answered like this, it stays in
 * XInput on its own. Pads that do not know them refuse, which is harmless. */
static void xinput_windows_hello(usbpad *u)
{
    unsigned char buf[64];
    int os, xusb;

    memset(buf, 0, sizeof buf);
    os = ctrl_in(u->fd, UT_READ_DEVICE, UR_GET_DESCRIPTOR, 0x03EE, 0, buf, 18);
    memset(buf, 0, sizeof buf);
    xusb = ctrl_in(u->fd, 0xC1, 0x01, 0x0100, 0x0000, buf, 20);
    log_line("usb: %s: Windows hello: OS string %d, XUSB request %d", u->path, os, xusb);
}

/* String descriptor `index` (US English), as ASCII. A standard GET_DESCRIPTOR. */
static int read_string(int fd, int index, char *out, size_t size)
{
    struct usb_ctl_request req;
    unsigned char buf[255];

    if (index == 0) return 0;
    memset(&req, 0, sizeof req);
    memset(buf, 0, sizeof buf);
    req.ucr_data = buf;
    req.ucr_flags = 0x0004;                     /* USB_SHORT_XFER_OK (usbdi.h, kernel-only header) */
    req.ucr_request.bmRequestType = UT_READ_DEVICE;
    req.ucr_request.bRequest = UR_GET_DESCRIPTOR;
    USETW(req.ucr_request.wValue, 0x0300 | index);
    USETW(req.ucr_request.wIndex, 0x0409);
    USETW(req.ucr_request.wLength, sizeof buf);
    if (ioctl(fd, USB_DO_REQUEST, &req) != 0) return 0;
    return usbdesc_string(buf, req.ucr_actlen, out, size);
}

static int start_read(usbpad *u, int i)
{
    struct usb_fs_start st;

    u->lenp[i][0] = u->mps;
    u->ep[i].nFrames = 1;
    u->ep[i].aFrames = 0;
    u->ep[i].status = 0;
    memset(&st, 0, sizeof st);
    st.ep_index = (uint8_t)i;
    if (ioctl(u->fd, USB_FS_START, &st) != 0) return dead_errno(errno) ? -1 : 0;
    u->busy[i] = 1;
    return 1;
}

/* *not_pad: set when the device answered and is not a pad (as opposed to a
 * failure on the way, which is tried again). */
static usbpad *try_node(int bus, int addr, int *not_pad)
{
    static const char *kinds[] = { "", "8BitDo HID (DInput)", "XInput" };
    struct usb_device_descriptor dd;
    struct usb_device_info di;
    struct usb_fs_init init;
    usbdesc_pick pick;
    char maker[32] = "";
    const char *known;
    usbpad *u;
    int i;

    u = calloc(1, sizeof *u);
    if (!u) return NULL;
    u->bus = bus;
    u->addr = addr;
    snprintf(u->path, sizeof u->path, "/dev/ugen%d.%d", bus, addr);
    u->fd = open(u->path, O_RDWR);
    if (u->fd < 0) goto fail;
    if (ioctl(u->fd, USB_GET_DEVICE_DESC, &dd) != 0) goto fail;
    u->kind = choose_interface(u->fd, UGETW(dd.idVendor), &pick);
    if (u->kind == USBPAD_NONE) {
        char prod[48] = "";
        read_string(u->fd, dd.iProduct, prod, sizeof prod);
        snprintf(g_reject, sizeof g_reject, "%04x:%04x \"%s\"", UGETW(dd.idVendor), UGETW(dd.idProduct), prod);
        *not_pad = 1;
        goto fail;
    }
    u->ep_in = pick.ep_in;
    u->mps = pick.mps_in;
    if (u->mps == 0 || u->mps > RD_MAX) u->mps = RD_MAX;
    if (u->kind == USBPAD_XINPUT && pick.ep_out) {
        u->ep_out = pick.ep_out;
        u->mps_out = pick.mps_out ? pick.mps_out : 32;
        if (u->mps_out > RD_MAX) u->mps_out = RD_MAX;
    }

    /* The product name: USB_GET_DEVICEINFO fails on the 8BitDo dongle, the
     * string descriptor itself does not. A name known for its id wins. */
    memset(&di, 0, sizeof di);
    read_string(u->fd, dd.iManufacturer, maker, sizeof maker);
    if ((known = usbdesc_known_name(UGETW(dd.idVendor), UGETW(dd.idProduct), maker)))
        snprintf(u->name, sizeof u->name, "%s", known);
    else if (!read_string(u->fd, dd.iProduct, u->name, sizeof u->name)) {
        if (ioctl(u->fd, USB_GET_DEVICEINFO, &di) == 0 && di.udi_product[0])
            snprintf(u->name, sizeof u->name, "%.60s", di.udi_product);
        else
            snprintf(u->name, sizeof u->name, "%s", UGETW(dd.idVendor) == VID_8BITDO ? "8BitDo" : "USB controller");
    }

    for (i = 0; i <= N_RD; i++) {
        u->bufp[i][0] = u->buf[i];
        u->lenp[i][0] = i == EP_OUT ? u->mps_out : u->mps;
        u->ep[i].ppBuffer = u->bufp[i];
        u->ep[i].pLength = u->lenp[i];
        u->ep[i].nFrames = 1;
        u->ep[i].flags = i == EP_OUT ? 0 : USB_FS_FLAG_SINGLE_SHORT_OK;
    }
    memset(&init, 0, sizeof init);
    init.pEndpoints = u->ep;
    init.ep_index_max = N_RD + 1;
    if (ioctl(u->fd, USB_FS_INIT, &init) != 0) {
        log_line("usb: %s: init errno %d", u->path, errno);
        goto fail;
    }
    for (i = 0; i < N_RD; i++) {
        struct usb_fs_open op;
        memset(&op, 0, sizeof op);
        op.ep_index = (uint8_t)i;
        op.ep_no = u->ep_in;
        op.max_bufsize = u->mps;
        op.max_frames = 1;
        if (ioctl(u->fd, USB_FS_OPEN, &op) != 0) {
            log_line("usb: %s: open endpoint %#x errno %d", u->path, u->ep_in, errno);
            usbpad_close(u);
            return NULL;
        }
    }
    if (u->ep_out) {
        struct usb_fs_open op;
        memset(&op, 0, sizeof op);
        op.ep_index = EP_OUT;
        op.ep_no = u->ep_out;
        op.max_bufsize = u->mps_out;
        op.max_frames = 1;
        if (ioctl(u->fd, USB_FS_OPEN, &op) != 0) {
            log_line("usb: %s: open endpoint %#x errno %d (no output)", u->path, u->ep_out, errno);
            u->ep_out = 0;
        }
    }
    u->t_alive = now_ms();
    log_line("usb: %s %04x:%04x \"%s\", %s interface %u, reading endpoint %#x (%u bytes)", u->path,
             UGETW(dd.idVendor), UGETW(dd.idProduct), u->name, kinds[u->kind], pick.iface, u->ep_in, u->mps);
    /* A keyboard or mouse beside the pad (the USB Wireless Adapter 2's macros
     * in XInput mode): the console's own driver lets go of it, so the console
     * does not list a keyboard and a mouse among its accessories. */
    for (i = 0; i < 32; i++)
        if (pick.hide & (1u << i)) {
            int n = i;
            log_line("usb: %s: interface %d (keyboard/mouse) hidden from the console: %s", u->path, i,
                     ioctl(u->fd, USB_IFACE_DRIVER_DETACH, &n) == 0 ? "ok" : "failed");
        }
    if (u->kind == USBPAD_XINPUT) {
        xinput_windows_hello(u);
        /* The LED of player 1, as a PC sets it: some XInput pads report only after it. */
        static const unsigned char led[3] = { 0x01, 0x03, 0x06 };
        log_line("usb: %s: XInput LED %s", u->path,
                 !u->ep_out ? "not sent (no OUT endpoint)" : send_out(u, led, 3) ? "set" : "not acknowledged");
    }
    g_held[bus][addr] = 1;
    return u;

fail:
    if (u->fd >= 0) close(u->fd);
    free(u);
    return NULL;
}

usbpad *usbpad_open(void)
{
    long now = now_ms();
    int bus, addr, not_pad;

    for (bus = 0; bus < N_BUS; bus++)
        for (addr = 2; addr < N_ADDR; addr++) {
            char path[24];
            struct stat sb;
            usbpad *u;
            if (bus == 0 && addr == 2) continue;        /* the internal Bluetooth chip: never touched */
            if (g_held[bus][addr]) continue;
            snprintf(path, sizeof path, "/dev/ugen%d.%d", bus, addr);
            if (stat(path, &sb) != 0) {
                g_not_pad[bus][addr].set = 0;
                continue;
            }
            if (g_not_pad[bus][addr].set && g_not_pad[bus][addr].ino == sb.st_ino &&
                g_not_pad[bus][addr].ctime == (long)sb.st_ctime && now - g_not_pad[bus][addr].seen < T_NOT_PAD)
                continue;                               /* looked at a moment ago, unchanged */
            not_pad = 0;
            if ((u = try_node(bus, addr, &not_pad))) {
                g_not_pad[bus][addr].set = 0;
                return u;
            }
            /* Logged once per device: what is plugged in and not read, to
             * tell why a controller does nothing (another mode, say). */
            if (not_pad && strcmp(g_logged[bus][addr], g_reject) != 0) {
                log_line("usb: %s %s: not a controller padbridge reads", path, g_reject);
                snprintf(g_logged[bus][addr], sizeof g_logged[bus][addr], "%s", g_reject);
            }
            g_not_pad[bus][addr].set = (unsigned char)not_pad;
            g_not_pad[bus][addr].ino = sb.st_ino;
            g_not_pad[bus][addr].ctime = (long)sb.st_ctime;
            g_not_pad[bus][addr].seen = now;
        }
    return NULL;
}

const char *usbpad_name(const usbpad *u)
{
    return u && u->name[0] ? u->name : "USB controller";
}

const char *usbpad_mode(const usbpad *u)
{
    return u && u->kind == USBPAD_XINPUT ? "XInput" : "DInput";
}

/* Starts an OUT transfer without waiting: its completion comes through
 * usbpad_poll. 0 if it could not start. */
static int start_out(usbpad *u, const unsigned char *data, int n)
{
    struct usb_fs_start st;

    if (!u->ep_out || u->busy[EP_OUT] || n > u->mps_out || n > RD_MAX) return 0;
    memcpy(u->buf[EP_OUT], data, (size_t)n);
    u->lenp[EP_OUT][0] = (uint32_t)n;
    u->ep[EP_OUT].nFrames = 1;
    u->ep[EP_OUT].aFrames = 0;
    u->ep[EP_OUT].status = 0;
    memset(&st, 0, sizeof st);
    st.ep_index = EP_OUT;
    if (ioctl(u->fd, USB_FS_START, &st) != 0) return 0;
    u->busy[EP_OUT] = 1;
    return 1;
}

int usbpad_can_rumble(const usbpad *u)
{
    return u && u->kind == USBPAD_XINPUT && u->ep_out;
}

void usbpad_rumble(usbpad *u, uint8_t large, uint8_t small)
{
    if (!usbpad_can_rumble(u)) return;
    u->rumble[0] = large;
    u->rumble[1] = small;
    u->rumble_pending = large != u->rumble_sent[0] || small != u->rumble_sent[1];
}

/* XInput rumble: 00 08 00 <large> <small> 00 00 00, sent when the OUT
 * endpoint is free; a newer value waiting replaces an older one. */
static void rumble_flush(usbpad *u)
{
    unsigned char r[8] = { 0x00, 0x08, 0x00, 0, 0, 0, 0, 0 };

    if (!u->rumble_pending || u->busy[EP_OUT]) return;
    r[3] = u->rumble[0];
    r[4] = u->rumble[1];
    if (start_out(u, r, sizeof r)) {
        u->rumble_sent[0] = u->rumble[0];
        u->rumble_sent[1] = u->rumble[1];
        u->rumble_pending = 0;
    }
}

int usbpad_poll(usbpad *u, pad_state *st)
{
    struct usb_fs_complete done;
    int i, got = 0;
    long now = now_ms();

    for (i = 0; i < N_RD; i++)
        if (!u->busy[i] && start_read(u, i) < 0) {
            log_line("usb: %s: gone (start errno %d)", u->path, errno);
            return -1;
        }
    for (;;) {
        memset(&done, 0, sizeof done);
        if (ioctl(u->fd, USB_FS_COMPLETE, &done) != 0) {
            if (errno != EBUSY && dead_errno(errno)) {
                log_line("usb: %s: gone (complete errno %d)", u->path, errno);
                return -1;
            }
            break;
        }
        i = done.ep_index;
        if (i > N_RD) continue;
        u->busy[i] = 0;
        if (i == EP_OUT) continue;
        if (u->ep[i].status) continue;              /* cancelled or stalled: read again */
        if (!u->ep[i].aFrames) continue;
        if (u->kind == USBPAD_XINPUT ? usbpad_parse_xinput(u->buf[i], (int)u->lenp[i][0], st)
                                     : usbpad_parse_8bitdo(u->buf[i], (int)u->lenp[i][0], st)) {
            if (!u->logged_first) {
                u->logged_first = 1;
                log_line("usb: first controller report");
            }
            got = 1;
        }
    }
    rumble_flush(u);
    /* A re-enumeration does not always fail the pending reads at once.
     * (USB_GET_DEVICEINFO fails on the 8BitDo dongle, so it is not used.) */
    if (now - u->t_alive >= T_ALIVE) {
        struct usb_device_descriptor dd;
        u->t_alive = now;
        if (access(u->path, F_OK) != 0 || ioctl(u->fd, USB_GET_DEVICE_DESC, &dd) != 0) {
            log_line("usb: %s: gone (errno %d)", u->path, errno);
            return -1;
        }
    }
    return got;
}

void usbpad_close(usbpad *u)
{
    struct usb_fs_uninit un;

    if (!u) return;
    if (u->bus >= 0 && u->bus < N_BUS && u->addr >= 0 && u->addr < N_ADDR) g_held[u->bus][u->addr] = 0;
    if (u->fd >= 0) {
        memset(&un, 0, sizeof un);
        ioctl(u->fd, USB_FS_UNINIT, &un);
        close(u->fd);
    }
    free(u);
}
