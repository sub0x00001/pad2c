#include "usbpad.h"
#include "log.h"
#include "usbdesc.h"
#include "util.h"

#include <sys/ioctl.h>
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

struct usbpad {
    int fd;
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

static usbpad *try_node(int bus, int addr)
{
    static const char *kinds[] = { "", "8BitDo HID", "XInput" };
    struct usb_device_descriptor dd;
    struct usb_device_info di;
    struct usb_fs_init init;
    usbdesc_pick pick;
    usbpad *u;
    int i;

    u = calloc(1, sizeof *u);
    if (!u) return NULL;
    snprintf(u->path, sizeof u->path, "/dev/ugen%d.%d", bus, addr);
    u->fd = open(u->path, O_RDWR);
    if (u->fd < 0) goto fail;
    if (ioctl(u->fd, USB_GET_DEVICE_DESC, &dd) != 0) goto fail;
    u->kind = choose_interface(u->fd, UGETW(dd.idVendor), &pick);
    if (u->kind == USBPAD_NONE) goto fail;
    u->ep_in = pick.ep_in;
    u->mps = pick.mps_in;
    if (u->mps == 0 || u->mps > RD_MAX) u->mps = RD_MAX;
    if (u->kind == USBPAD_XINPUT && pick.ep_out) {
        u->ep_out = pick.ep_out;
        u->mps_out = pick.mps_out ? pick.mps_out : 32;
        if (u->mps_out > RD_MAX) u->mps_out = RD_MAX;
    }

    memset(&di, 0, sizeof di);
    if (ioctl(u->fd, USB_GET_DEVICEINFO, &di) == 0 && di.udi_product[0])
        snprintf(u->name, sizeof u->name, "%.60s", di.udi_product);
    else
        snprintf(u->name, sizeof u->name, "%s %04x:%04x", UGETW(dd.idVendor) == VID_8BITDO ? "8BitDo" : "USB pad",
                 UGETW(dd.idVendor), UGETW(dd.idProduct));

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
    if (u->kind == USBPAD_XINPUT) {
        /* The LED of player 1, as a PC sets it: some XInput pads report only after it. */
        static const unsigned char led[3] = { 0x01, 0x03, 0x06 };
        log_line("usb: %s: XInput LED %s", u->path,
                 !u->ep_out ? "not sent (no OUT endpoint)" : send_out(u, led, 3) ? "set" : "not acknowledged");
    }
    return u;

fail:
    if (u->fd >= 0) close(u->fd);
    free(u);
    return NULL;
}

usbpad *usbpad_open(void)
{
    int bus, addr;

    for (bus = 0; bus < 4; bus++)
        for (addr = 2; addr < 16; addr++) {
            char path[24];
            usbpad *u;
            if (bus == 0 && addr == 2) continue;        /* the internal Bluetooth chip: never touched */
            snprintf(path, sizeof path, "/dev/ugen%d.%d", bus, addr);
            if (access(path, F_OK) != 0) continue;
            if ((u = try_node(bus, addr))) return u;
        }
    return NULL;
}

const char *usbpad_name(const usbpad *u)
{
    return u && u->name[0] ? u->name : "USB pad";
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
    if (u->fd >= 0) {
        memset(&un, 0, sizeof un);
        ioctl(u->fd, USB_FS_UNINIT, &un);
        close(u->fd);
    }
    free(u);
}
