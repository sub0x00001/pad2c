/* Which interface of a USB device pad2c reads, from its configuration
 * descriptor alone (no device needed, so it runs in the tests too). */
#ifndef PAD2C_USBDESC_H
#define PAD2C_USBDESC_H

#include <stdint.h>

#define VID_8BITDO 0x2DC8

enum {
    USBPAD_NONE = 0,
    USBPAD_8BITDO,      /* 8BitDo DInput: HID report 1 (map8bitdo.c) */
    USBPAD_XINPUT,      /* Xbox 360 protocol, vendor interface ff/5d/01 (mapxinput.c) */
};

typedef struct {
    int kind;
    uint8_t iface;
    uint8_t ep_in, ep_out;          /* ep_out 0: none */
    uint16_t mps_in, mps_out;
} usbdesc_pick;

/* Picks the interface to read:
 *   1. an XInput interface (class ff, subclass 5d, protocol 01), any vendor;
 *   2. on an 8BitDo, the first HID interface that is not a boot keyboard or
 *      mouse (the 8BitDo 310a receiver also has a keyboard interface).
 * Each needs an interrupt IN endpoint. Returns the kind, USBPAD_NONE if none. */
int usbdesc_choose(const unsigned char *cfg, int len, uint16_t vid, usbdesc_pick *out);

#endif
