/* Which interface of a USB device padbridge reads, from its configuration
 * descriptor alone (no device needed, so it runs in the tests too). */
#ifndef PADBRIDGE_USBDESC_H
#define PADBRIDGE_USBDESC_H

#include <stddef.h>
#include <stdint.h>

#define VID_8BITDO 0x2DC8

enum {
    USBPAD_NONE = 0,
    USBPAD_8BITDO,      /* 8BitDo DInput: HID report 1 (map8bitdo.c) */
    USBPAD_XINPUT,      /* Xbox 360 protocol, vendor interface ff/5d/01 (mapxinput.c) */
    USBPAD_SWITCH,      /* Nintendo Switch Pro Controller, 057e:2009 (mapswitch.c) */
};

typedef struct {
    int kind;
    uint8_t iface;
    uint8_t ep_in, ep_out;          /* ep_out 0: none */
    uint16_t mps_in, mps_out;
    uint32_t hide;                  /* bit n: interface n is a boot keyboard or mouse beside the pad
                                       (the USB Wireless Adapter 2's macros), hidden from the console */
} usbdesc_pick;

/* Picks the interface to read:
 *   1. an XInput interface (class ff, subclass 5d, protocol 01), any vendor;
 *   2. a Switch Pro Controller (057e:2009): its HID interface;
 *   3. on an 8BitDo, the first HID interface that is not a boot keyboard or
 *      mouse (the 8BitDo 310a receiver also has a keyboard interface).
 * Each needs an interrupt IN endpoint. Returns the kind, USBPAD_NONE if none. */
int usbdesc_choose(const unsigned char *cfg, int len, uint16_t vid, uint16_t pid, usbdesc_pick *out);

/* A string descriptor (UTF-16LE) as ASCII, '?' for anything else, trailing
 * spaces dropped, cut to fit `size`. 0 if it is not one or is blank. */
int usbdesc_string(const unsigned char *d, int len, char *out, size_t size);

/* The product's name, where its own is not: the 8BitDo USB Wireless Adapter 2
 * calls itself "8BitDo Receiver" or "Controller". NULL if none is known. */
const char *usbdesc_known_name(uint16_t vid, uint16_t pid, const char *manufacturer);

#endif
