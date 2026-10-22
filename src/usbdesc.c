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
static int is_pad_hid(const iface *f)
{
    return f->cls == 0x03 && !(f->sub == 0x01 && (f->proto == 0x01 || f->proto == 0x02));
}

static int pick(const iface *f, int kind, usbdesc_pick *out)
{
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
        if (is_xinput(&ifs[i]) && ifs[i].ep_in) return pick(&ifs[i], USBPAD_XINPUT, out);
    if (vid == VID_8BITDO)
        for (i = 0; i < n; i++)
            if (is_pad_hid(&ifs[i]) && ifs[i].ep_in) return pick(&ifs[i], USBPAD_8BITDO, out);
    return USBPAD_NONE;
}
