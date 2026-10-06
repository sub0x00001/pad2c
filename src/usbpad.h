/* A USB gamepad read straight from its /dev/ugenB.A node, beside the
 * console's own HID driver (which holds the interface but does nothing with
 * a device it does not know).
 *
 * Today: the 8BitDo 2.4G dongle (2dc8:301c). With no controller it shows up
 * as "8BitDo IDLE" and sends nothing useful; when the controller connects it
 * enumerates again as "8BitDo Ultimate 2C Wireless Controller" and sends
 * report 1 (see usbpad.c). Every re-enumeration ends the open device, so the
 * caller closes it and looks again.
 */
#ifndef PAD2C_USBPAD_H
#define PAD2C_USBPAD_H

#include "pad.h"

typedef struct usbpad usbpad;

/* Looks for a supported pad. NULL if there is none right now. */
usbpad *usbpad_open(void);

/* Takes what came in. Returns 1 with *st updated when a controller report
 * arrived, 0 when nothing new, -1 when the device is gone (close it). */
int usbpad_poll(usbpad *u, pad_state *st);

void usbpad_close(usbpad *u);
const char *usbpad_name(const usbpad *u);

/* The 8BitDo report 1 to a DualSense state. Returns 0 if it is not one. */
int usbpad_parse_8bitdo(const unsigned char *r, int n, pad_state *st);

#endif
