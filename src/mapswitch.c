#include "mapswitch.h"

#include <stddef.h>
#include <string.h>

void switch_sticks_reset(switch_sticks *c)
{
    int i;

    c->set = 0;
    for (i = 0; i < 4; i++) {
        c->centre[i] = 2048;
        c->reach[i] = SWITCH_STICK_REACH;
    }
}

/* A 12-bit stick value to 0..255, 128 at the centre. `up` flips it (the
 * DualSense's y grows downwards). */
static uint8_t axis(switch_sticks *c, int i, int v, int up)
{
    int d = up ? c->centre[i] - v : v - c->centre[i], a = d < 0 ? -d : d, out;

    if (a > c->reach[i]) c->reach[i] = a;
    out = d < 0 ? 128 - (a * 128 + c->reach[i] / 2) / c->reach[i] : 128 + (a * 127 + c->reach[i] / 2) / c->reach[i];
    return (uint8_t)(out < 0 ? 0 : out > 255 ? 255 : out);
}

int switch_parse(const unsigned char *r, int n, switch_sticks *cal, pad_state *st)
{
    static const struct { int byte; uint8_t bit; uint32_t pad; } map[] = {
        { 3, 0x01, PAD_SQUARE },  { 3, 0x02, PAD_TRIANGLE }, { 3, 0x04, PAD_CROSS },  { 3, 0x08, PAD_CIRCLE },
        { 3, 0x40, PAD_R1 },      { 3, 0x80, PAD_R2 },       { 4, 0x01, PAD_CREATE }, { 4, 0x02, PAD_OPTIONS },
        { 4, 0x04, PAD_R3 },      { 4, 0x08, PAD_L3 },       { 4, 0x10, PAD_PS },     { 4, 0x20, PAD_TOUCHPAD },
        { 5, 0x01, PAD_DOWN },    { 5, 0x02, PAD_UP },       { 5, 0x04, PAD_RIGHT },  { 5, 0x08, PAD_LEFT },
        { 5, 0x40, PAD_L1 },      { 5, 0x80, PAD_L2 },
    };
    int v[4];
    size_t i;

    if (n < 12 || (r[0] != 0x30 && r[0] != 0x21)) return 0;
    v[0] = r[6] | (r[7] & 0x0F) << 8;
    v[1] = r[7] >> 4 | r[8] << 4;
    v[2] = r[9] | (r[10] & 0x0F) << 8;
    v[3] = r[10] >> 4 | r[11] << 4;
    if (!cal->set) {
        for (i = 0; i < 4; i++) cal->centre[i] = v[i];
        cal->set = 1;
    }
    pad_state_reset(st);
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (r[map[i].byte] & map[i].bit) st->buttons |= map[i].pad;
    st->l2 = st->buttons & PAD_L2 ? 255 : 0;      /* ZL and ZR are digital */
    st->r2 = st->buttons & PAD_R2 ? 255 : 0;
    st->lx = axis(cal, 0, v[0], 0);
    st->ly = axis(cal, 1, v[1], 1);
    st->rx = axis(cal, 2, v[2], 0);
    st->ry = axis(cal, 3, v[3], 1);
    return 1;
}

/* SDL's tables (SDL_hidapi_switch.c, zlib licence), after dekuNukem's
 * rumble_data_table.md: an amplitude 0..65535 to the high band's byte and
 * the low band's 9-bit value. */
static uint8_t high_amp(uint16_t a)
{
    static const uint16_t t[101][2] = {
        { 0, 0x0 }, { 514, 0x2 }, { 775, 0x4 }, { 921, 0x6 }, { 1096, 0x8 }, { 1303, 0x0a }, { 1550, 0x0c },
        { 1843, 0x0e }, { 2192, 0x10 }, { 2606, 0x12 }, { 3100, 0x14 }, { 3686, 0x16 }, { 4383, 0x18 },
        { 5213, 0x1a }, { 6199, 0x1c }, { 7372, 0x1e }, { 7698, 0x20 }, { 8039, 0x22 }, { 8395, 0x24 },
        { 8767, 0x26 }, { 9155, 0x28 }, { 9560, 0x2a }, { 9984, 0x2c }, { 10426, 0x2e }, { 10887, 0x30 },
        { 11369, 0x32 }, { 11873, 0x34 }, { 12398, 0x36 }, { 12947, 0x38 }, { 13520, 0x3a }, { 14119, 0x3c },
        { 14744, 0x3e }, { 15067, 0x40 }, { 15397, 0x42 }, { 15734, 0x44 }, { 16079, 0x46 }, { 16431, 0x48 },
        { 16790, 0x4a }, { 17158, 0x4c }, { 17534, 0x4e }, { 17918, 0x50 }, { 18310, 0x52 }, { 18711, 0x54 },
        { 19121, 0x56 }, { 19540, 0x58 }, { 19967, 0x5a }, { 20405, 0x5c }, { 20851, 0x5e }, { 21308, 0x60 },
        { 21775, 0x62 }, { 22251, 0x64 }, { 22739, 0x66 }, { 23236, 0x68 }, { 23745, 0x6a }, { 24265, 0x6c },
        { 24797, 0x6e }, { 25340, 0x70 }, { 25894, 0x72 }, { 26462, 0x74 }, { 27041, 0x76 }, { 27633, 0x78 },
        { 28238, 0x7a }, { 28856, 0x7c }, { 29488, 0x7e }, { 30134, 0x80 }, { 30794, 0x82 }, { 31468, 0x84 },
        { 32157, 0x86 }, { 32861, 0x88 }, { 33581, 0x8a }, { 34316, 0x8c }, { 35068, 0x8e }, { 35836, 0x90 },
        { 36620, 0x92 }, { 37422, 0x94 }, { 38242, 0x96 }, { 39079, 0x98 }, { 39935, 0x9a }, { 40809, 0x9c },
        { 41703, 0x9e }, { 42616, 0xa0 }, { 43549, 0xa2 }, { 44503, 0xa4 }, { 45477, 0xa6 }, { 46473, 0xa8 },
        { 47491, 0xaa }, { 48531, 0xac }, { 49593, 0xae }, { 50679, 0xb0 }, { 51789, 0xb2 }, { 52923, 0xb4 },
        { 54082, 0xb6 }, { 55266, 0xb8 }, { 56476, 0xba }, { 57713, 0xbc }, { 58977, 0xbe }, { 60268, 0xc0 },
        { 61588, 0xc2 }, { 62936, 0xc4 }, { 64315, 0xc6 }, { 65535, 0xc8 },
    };
    int i;

    for (i = 0; i < 101; i++)
        if (a <= t[i][0]) return (uint8_t)t[i][1];
    return (uint8_t)t[100][1];
}

static uint16_t low_amp(uint16_t a)
{
    static const uint16_t t[101][2] = {
        { 0, 0x0040 }, { 514, 0x8040 }, { 775, 0x0041 }, { 921, 0x8041 }, { 1096, 0x0042 }, { 1303, 0x8042 },
        { 1550, 0x0043 }, { 1843, 0x8043 }, { 2192, 0x0044 }, { 2606, 0x8044 }, { 3100, 0x0045 },
        { 3686, 0x8045 }, { 4383, 0x0046 }, { 5213, 0x8046 }, { 6199, 0x0047 }, { 7372, 0x8047 },
        { 7698, 0x0048 }, { 8039, 0x8048 }, { 8395, 0x0049 }, { 8767, 0x8049 }, { 9155, 0x004a },
        { 9560, 0x804a }, { 9984, 0x004b }, { 10426, 0x804b }, { 10887, 0x004c }, { 11369, 0x804c },
        { 11873, 0x004d }, { 12398, 0x804d }, { 12947, 0x004e }, { 13520, 0x804e }, { 14119, 0x004f },
        { 14744, 0x804f }, { 15067, 0x0050 }, { 15397, 0x8050 }, { 15734, 0x0051 }, { 16079, 0x8051 },
        { 16431, 0x0052 }, { 16790, 0x8052 }, { 17158, 0x0053 }, { 17534, 0x8053 }, { 17918, 0x0054 },
        { 18310, 0x8054 }, { 18711, 0x0055 }, { 19121, 0x8055 }, { 19540, 0x0056 }, { 19967, 0x8056 },
        { 20405, 0x0057 }, { 20851, 0x8057 }, { 21308, 0x0058 }, { 21775, 0x8058 }, { 22251, 0x0059 },
        { 22739, 0x8059 }, { 23236, 0x005a }, { 23745, 0x805a }, { 24265, 0x005b }, { 24797, 0x805b },
        { 25340, 0x005c }, { 25894, 0x805c }, { 26462, 0x005d }, { 27041, 0x805d }, { 27633, 0x005e },
        { 28238, 0x805e }, { 28856, 0x005f }, { 29488, 0x805f }, { 30134, 0x0060 }, { 30794, 0x8060 },
        { 31468, 0x0061 }, { 32157, 0x8061 }, { 32861, 0x0062 }, { 33581, 0x8062 }, { 34316, 0x0063 },
        { 35068, 0x8063 }, { 35836, 0x0064 }, { 36620, 0x8064 }, { 37422, 0x0065 }, { 38242, 0x8065 },
        { 39079, 0x0066 }, { 39935, 0x8066 }, { 40809, 0x0067 }, { 41703, 0x8067 }, { 42616, 0x0068 },
        { 43549, 0x8068 }, { 44503, 0x0069 }, { 45477, 0x8069 }, { 46473, 0x006a }, { 47491, 0x806a },
        { 48531, 0x006b }, { 49593, 0x806b }, { 50679, 0x006c }, { 51789, 0x806c }, { 52923, 0x006d },
        { 54082, 0x806d }, { 55266, 0x006e }, { 56476, 0x806e }, { 57713, 0x006f }, { 58977, 0x806f },
        { 60268, 0x0070 }, { 61588, 0x8070 }, { 62936, 0x0071 }, { 64315, 0x8071 }, { 65535, 0x0072 },
    };
    int i;

    for (i = 0; i < 101; i++)
        if (a <= t[i][0]) return t[i][1];
    return t[100][1];
}

void switch_rumble_encode(uint8_t large, uint8_t small, unsigned char out[8])
{
    static const unsigned char neutral[4] = { 0x00, 0x01, 0x40, 0x40 };
    /* SDL's fixed frequencies: the high band 0x0074, the low band 0x3d */
    const uint16_t hf = 0x0074;
    const uint8_t lf = 0x3D;
    uint8_t ha = high_amp((uint16_t)(small * 257));
    uint16_t la = low_amp((uint16_t)(large * 257));
    unsigned char d[4];

    if (!large && !small) {
        memcpy(out, neutral, 4);
        memcpy(out + 4, neutral, 4);
        return;
    }
    d[0] = (unsigned char)(hf & 0xFF);
    d[1] = (unsigned char)(ha | ((hf >> 8) & 0x01));
    d[2] = (unsigned char)(lf | ((la >> 8) & 0x80));
    d[3] = (unsigned char)(la & 0xFF);
    memcpy(out, d, 4);
    memcpy(out + 4, d, 4);
}
