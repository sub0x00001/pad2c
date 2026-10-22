#include "usbdesc.h"

#include <string.h>

#define DT_INTERFACE 0x04
#define DT_ENDPOINT  0x05

typedef struct {
    uint8_t num, cls, sub, proto;
    uint8_t ep_in, ep_out;
    uint16_t mps_in, mps_out;
} iface;

static int is_xinput(const iface *f)
{
    return f->cls == 0xFF && f->sub == 0x5D && f->proto == 0x01;
}

/* HID, but not a boot keyboard (1/1) or mouse (1/2). */
static int is_boot_kbd_mouse(const iface *f)
{
    return f->cls == 0x03 && f->sub == 0x01 && (f->proto == 0x01 || f->proto == 0x02);
}

static int is_pad_hid(const iface *f)
{
    return f->cls == 0x03 && !is_boot_kbd_mouse(f);
}

static int pick(const iface *ifs, int n, const iface *f, int kind, usbdesc_pick *out)
{
    int i;

    for (i = 0; i < n; i++)
        if (is_boot_kbd_mouse(&ifs[i]) && ifs[i].num < 32) out->hide |= 1u << ifs[i].num;
    out->kind = kind;
    out->iface = f->num;
    out->ep_in = f->ep_in;
    out->mps_in = f->mps_in;
    out->ep_out = f->ep_out;
    out->mps_out = f->mps_out;
    return kind;
}

int usbdesc_choose(const unsigned char *cfg, int len, uint16_t vid, usbdesc_pick *out)
{
    iface ifs[16];
    int n = 0, off = 0, i;

    memset(out, 0, sizeof *out);
    while (off + 2 <= len) {
        const unsigned char *d = cfg + off;
        int dl = d[0];
        if (dl < 2 || off + dl > len) break;
        if (d[1] == DT_INTERFACE && dl >= 9) {
            if (n == (int)(sizeof ifs / sizeof ifs[0])) break;
            memset(&ifs[n], 0, sizeof ifs[n]);
            ifs[n].num = d[2];
            ifs[n].cls = d[5];
            ifs[n].sub = d[6];
            ifs[n].proto = d[7];
            n++;
        } else if (d[1] == DT_ENDPOINT && dl >= 7 && n > 0 && (d[3] & 3) == 3) {
            iface *f = &ifs[n - 1];
            uint16_t mps = (uint16_t)(d[4] | d[5] << 8);
            if ((d[2] & 0x80) && !f->ep_in) {
                f->ep_in = d[2];
                f->mps_in = mps;
            } else if (!(d[2] & 0x80) && !f->ep_out) {
                f->ep_out = d[2];
                f->mps_out = mps;
            }
        }
        off += dl;
    }

    for (i = 0; i < n; i++)
        if (is_xinput(&ifs[i]) && ifs[i].ep_in) return pick(ifs, n, &ifs[i], USBPAD_XINPUT, out);
    if (vid == VID_8BITDO)
        for (i = 0; i < n; i++)
            if (is_pad_hid(&ifs[i]) && ifs[i].ep_in) return pick(ifs, n, &ifs[i], USBPAD_8BITDO, out);
    return USBPAD_NONE;
}

int usbdesc_string(const unsigned char *d, int len, char *out, size_t size)
{
    size_t n = 0;
    int i;

    if (size == 0) return 0;
    out[0] = 0;
    if (len < 2 || d[1] != 0x03) return 0;
    if (d[0] < len) len = d[0];
    for (i = 2; i + 1 < len && n + 1 < size; i += 2) {
        unsigned c = d[i] | (unsigned)d[i + 1] << 8;
        out[n++] = (char)(c >= 0x20 && c < 0x7F ? c : '?');
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n > 0;
}

const char *usbdesc_known_name(uint16_t vid, uint16_t pid, const char *manufacturer)
{
    static const char adapter2[] = "8BitDo USB Wireless Adapter 2";
    int by_8bitdo = manufacturer && !strcmp(manufacturer, "8BitDo");

    if (vid == VID_8BITDO && (pid == 0x3105 || pid == 0x3106 || pid == 0x3107)) return adapter2;
    if (vid == 0x045E && pid == 0x028E && by_8bitdo) return adapter2;   /* its Xbox 360 identity */
    return NULL;
}
