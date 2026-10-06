/* pad2c probe -- read-only look at the console's USB devices.
 *
 * For every /dev/ugenB.A that is not a hub and not the internal Bluetooth
 * chip (ugen0.2, left alone on purpose): device descriptor, name, full
 * configuration descriptor, which kernel driver holds each interface, the
 * HID report descriptor of HID interfaces, and then 20 seconds of whatever
 * its interrupt IN endpoints send (press buttons and move the sticks then).
 * Also copies what /dev/klog holds, to see why another payload died.
 *
 * Everything goes to stdout (the loader's socket) and /data/pad2c/probe.log.
 * Nothing is written to any device except standard GET_DESCRIPTOR requests.
 */
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include <ps5/kernel.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DIR_PATH "/data/pad2c"
#define LOG_PATH DIR_PATH "/probe.log"
#define KLOG_PATH DIR_PATH "/klog.txt"
#ifndef READ_SECONDS
#define READ_SECONDS 60
#endif
#define MAX_EPS 8

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request;
int sceKernelSendNotificationRequest(int, notify_request *, size_t, int);

static FILE *g_log;

static void out(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fprintf(g_log, "\n");
        fflush(g_log);
    }
}

static void notify(const char *msg)
{
    notify_request req;

    memset(&req, 0, sizeof req);
    snprintf(req.message, sizeof req.message, "%s", msg);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void hexdump(const char *tag, const uint8_t *p, int n)
{
    char line[3 * 32 + 1];
    int i, k;

    for (i = 0; i < n; i += 32) {
        int m = n - i < 32 ? n - i : 32;
        for (k = 0; k < m; k++) snprintf(line + 3 * k, 4, "%02x ", p[i + k]);
        line[3 * m] = '\0';
        out("%s[%03d] %s", tag, i, line);
    }
}

/* ---- /dev/klog ------------------------------------------------------------- */

static void copy_klog(void)
{
    char buf[4096];
    int fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
    FILE *f;
    long total = 0;
    ssize_t n;

    if (fd < 0) {
        out("klog: cannot open /dev/klog (errno %d)", errno);
        return;
    }
    f = fopen(KLOG_PATH, "w");
    while ((n = read(fd, buf, sizeof buf)) > 0 && total < 4 * 1024 * 1024) {
        if (f) fwrite(buf, 1, (size_t)n, f);
        total += n;
    }
    if (f) fclose(f);
    close(fd);
    out("klog: %ld bytes copied to %s", total, KLOG_PATH);
}

/* ---- one USB device ---------------------------------------------------------- */

typedef struct {
    uint8_t addr;
    uint16_t mps;
    int iface;
} ep_in;

static int parse_config(const uint8_t *cfg, int len, ep_in *eps, int *hid_iface, int *hid_len, int nhid_max,
                        int *nhid_out)
{
    int off = 0, n = 0, iface = -1, iface_class = 0, nhid = 0;

    while (off + 2 <= len) {
        int dl = cfg[off];
        const uint8_t *d = cfg + off;

        if (dl < 2 || off + dl > len) break;
        if (d[1] == 0x04 && dl >= 9) {                  /* interface */
            iface = d[2];
            iface_class = d[5];
            out("  interface %d alt %d: class %02x/%02x/%02x, %d endpoint(s)", d[2], d[3], d[5], d[6], d[7], d[4]);
        } else if (d[1] == 0x21 && dl >= 9 && iface_class == 0x03) {   /* HID descriptor */
            int rlen = d[7] | d[8] << 8;
            out("  HID descriptor: report descriptor of %d bytes", rlen);
            if (nhid < nhid_max) {
                hid_iface[nhid] = iface;
                hid_len[nhid] = rlen;
                nhid++;
            }
        } else if (d[1] == 0x05 && dl >= 7) {           /* endpoint */
            static const char *const type[] = { "control", "isochronous", "bulk", "interrupt" };
            uint16_t mps = (uint16_t)(d[4] | d[5] << 8);
            out("  endpoint %#04x %s %s, %u bytes, interval %u", d[2], d[2] & 0x80 ? "IN" : "OUT",
                type[d[3] & 3], mps, d[6]);
            if ((d[2] & 0x80) && (d[3] & 3) == 3 && n < MAX_EPS) {
                eps[n].addr = d[2];
                eps[n].mps = mps;
                eps[n].iface = iface;
                n++;
            }
        } else {
            out("  descriptor type %#04x, %d bytes", d[1], dl);
        }
        off += dl;
    }
    *nhid_out = nhid;
    return n;
}

static void get_report_descriptor(int fd, int iface, int len)
{
    struct usb_ctl_request req;
    uint8_t buf[1024];

    if (len <= 0 || len > (int)sizeof buf) len = sizeof buf;
    memset(&req, 0, sizeof req);
    memset(buf, 0, sizeof buf);
    req.ucr_data = buf;
    req.ucr_flags = 0x0004;                     /* USB_SHORT_XFER_OK (usbdi.h, kernel-only header) */
    req.ucr_request.bmRequestType = UT_READ_INTERFACE;
    req.ucr_request.bRequest = UR_GET_DESCRIPTOR;
    USETW(req.ucr_request.wValue, 0x2200);
    USETW(req.ucr_request.wIndex, iface);
    USETW(req.ucr_request.wLength, len);
    if (ioctl(fd, USB_DO_REQUEST, &req) != 0) {
        out("  report descriptor of interface %d: errno %d", iface, errno);
        return;
    }
    out("  report descriptor of interface %d, %d bytes:", iface, req.ucr_actlen);
    hexdump("  rdesc", buf, req.ucr_actlen);
}

/* Reads every interrupt IN endpoint for a while, logging each report that
 * differs from the previous one on that endpoint. */
static void read_reports(int fd, const ep_in *eps, int n)
{
    struct usb_fs_endpoint ep[MAX_EPS];
    static uint8_t buf[MAX_EPS][512], last[MAX_EPS][512];
    void *bufp[MAX_EPS][1];
    uint32_t lenp[MAX_EPS][1];
    int lastlen[MAX_EPS], opened[MAX_EPS], busy[MAX_EPS], count[MAX_EPS];
    struct usb_fs_init init;
    long end;
    int i;

    memset(ep, 0, sizeof ep);
    memset(lastlen, 0, sizeof lastlen);
    memset(opened, 0, sizeof opened);
    memset(busy, 0, sizeof busy);
    memset(count, 0, sizeof count);
    for (i = 0; i < n; i++) {
        bufp[i][0] = buf[i];
        lenp[i][0] = eps[i].mps;
        ep[i].ppBuffer = bufp[i];
        ep[i].pLength = lenp[i];
        ep[i].nFrames = 1;
        ep[i].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
    }
    memset(&init, 0, sizeof init);
    init.pEndpoints = ep;
    init.ep_index_max = n;
    if (ioctl(fd, USB_FS_INIT, &init) != 0) {
        out("  reports: USB_FS_INIT errno %d", errno);
        return;
    }
    for (i = 0; i < n; i++) {
        struct usb_fs_open op;
        memset(&op, 0, sizeof op);
        op.ep_index = (uint8_t)i;
        op.ep_no = eps[i].addr;
        op.max_bufsize = eps[i].mps > 512 ? 512 : eps[i].mps;
        op.max_frames = 1;
        opened[i] = ioctl(fd, USB_FS_OPEN, &op) == 0;
        out("  reports: open endpoint %#04x (interface %d): %s (errno %d)", eps[i].addr, eps[i].iface,
            opened[i] ? "ok" : "FAILED", opened[i] ? 0 : errno);
    }

    out(">>> PRESS BUTTONS, TRIGGERS AND MOVE BOTH STICKS NOW (%d s) <<<", READ_SECONDS);
    notify("pad2c probe: press every button, one at a time (60 s)");
    end = now_ms() + READ_SECONDS * 1000;
    while (now_ms() < end) {
        struct usb_fs_complete done;
        int any = 0;

        for (i = 0; i < n; i++) {
            if (!opened[i] || busy[i]) continue;
            struct usb_fs_start st;
            lenp[i][0] = eps[i].mps > 512 ? 512 : eps[i].mps;
            ep[i].nFrames = 1;
            ep[i].aFrames = 0;
            ep[i].status = 0;
            memset(&st, 0, sizeof st);
            st.ep_index = (uint8_t)i;
            if (ioctl(fd, USB_FS_START, &st) == 0) busy[i] = 1;
            else {
                out("  reports: start %#04x errno %d, giving up on it", eps[i].addr, errno);
                opened[i] = 0;
            }
        }
        for (;;) {
            memset(&done, 0, sizeof done);
            if (ioctl(fd, USB_FS_COMPLETE, &done) != 0) break;
            any = 1;
            i = done.ep_index;
            if (i >= n) continue;
            busy[i] = 0;
            if (ep[i].status) {
                out("  reports: %#04x finished with status %u", eps[i].addr, (unsigned)ep[i].status);
                continue;
            }
            if (ep[i].aFrames) {
                int len = (int)lenp[i][0];
                count[i]++;
                if (len != lastlen[i] || memcmp(buf[i], last[i], (size_t)len) != 0) {
                    char tag[48];
                    memcpy(last[i], buf[i], (size_t)len);
                    lastlen[i] = len;
                    snprintf(tag, sizeof tag, "  ep%02x t=%05ld", eps[i].addr, READ_SECONDS * 1000 - (end - now_ms()));
                    hexdump(tag, buf[i], len);
                }
            }
        }
        if (!any) usleep(1000);
    }
    for (i = 0; i < n; i++) out("  reports: endpoint %#04x delivered %d report(s)", eps[i].addr, count[i]);
    {
        struct usb_fs_uninit un;
        memset(&un, 0, sizeof un);
        ioctl(fd, USB_FS_UNINIT, &un);
    }
}

static void probe_device(int bus, int addr)
{
    char path[32];
    struct usb_device_descriptor dd;
    struct usb_device_info di;
    struct usb_gen_descriptor gd;
    static uint8_t cfg[2048];
    ep_in eps[MAX_EPS];
    int hid_iface[4], hid_len[4], nhid = 0, neps = 0;
    int fd, i, total = 0;

    snprintf(path, sizeof path, "/dev/ugen%d.%d", bus, addr);
    if (access(path, F_OK) != 0) return;
    if (bus == 0 && addr == 2) {
        out("%s: internal Bluetooth chip, not touched", path);
        return;
    }
    fd = open(path, O_RDWR);
    if (fd < 0) {
        out("%s: open errno %d", path, errno);
        return;
    }
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
        out("%s: device descriptor errno %d", path, errno);
        close(fd);
        return;
    }
    out("%s: %04x:%04x class %02x/%02x/%02x USB %04x", path, UGETW(dd.idVendor), UGETW(dd.idProduct),
        dd.bDeviceClass, dd.bDeviceSubClass, dd.bDeviceProtocol, UGETW(dd.bcdUSB));
    if (dd.bDeviceClass == 0x09) {
        out("  a hub, skipped");
        close(fd);
        return;
    }
    memset(&di, 0, sizeof di);
    if (ioctl(fd, USB_GET_DEVICEINFO, &di) == 0)
        out("  \"%.60s\" by \"%.60s\", speed %u, config %u", di.udi_product, di.udi_vendor, di.udi_speed,
            di.udi_config_no);

    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = sizeof cfg;
    gd.ugd_config_index = 0xFF;
    if (ioctl(fd, USB_GET_FULL_DESC, &gd) == 0) {
        total = gd.ugd_actlen < (int)sizeof cfg ? gd.ugd_actlen : (int)sizeof cfg;
        out("  configuration descriptor, %d bytes", total);
        hexdump("  cfg", cfg, total);
        neps = parse_config(cfg, total, eps, hid_iface, hid_len, 4, &nhid);
    } else {
        out("  configuration descriptor errno %d", errno);
    }
    for (i = 0; i < 8; i++) {
        char name[64];
        memset(&gd, 0, sizeof gd);
        memset(name, 0, sizeof name);
        gd.ugd_data = name;
        gd.ugd_maxlen = sizeof name - 1;
        gd.ugd_iface_index = (uint8_t)i;
        if (ioctl(fd, USB_GET_IFACE_DRIVER, &gd) == 0) out("  interface %d held by driver \"%s\"", i, name);
    }
    for (i = 0; i < nhid; i++) get_report_descriptor(fd, hid_iface[i], hid_len[i]);
    if (neps > 0) read_reports(fd, eps, neps);
    else out("  no interrupt IN endpoint");
    close(fd);
}

int main(void)
{
    uint32_t fw;
    int bus, addr;

    mkdir(DIR_PATH, 0777);
    g_log = fopen(LOG_PATH, "w");
    fw = kernel_get_fw_version();
    out("pad2c probe 1, firmware %x.%02x (raw %#x), pid %d", (unsigned)(fw >> 24) & 0xFF,
        (unsigned)(fw >> 16) & 0xFF, (unsigned)fw, (int)getpid());
    notify("pad2c probe: starting");
    copy_klog();
    for (bus = 0; bus < 4; bus++)
        for (addr = 1; addr < 10; addr++) probe_device(bus, addr);
    out("done");
    notify("pad2c probe: done");
    if (g_log) fclose(g_log);
    return 0;
}
