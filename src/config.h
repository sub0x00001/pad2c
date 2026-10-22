/* pad2c's settings, /data/pad2c/config: a line per setting, set from the
 * web page (web.c) or by hand.
 *
 *   swap <controller name>    A/B and X/Y exchanged for that controller
 *
 * A controller is known by its name (usbpad_name), the same in all its
 * modes: through the 8BitDo USB Wireless Adapter 2 that is the adapter's, so
 * a swap set there holds for whatever controller is connected to it (a
 * Nintendo-layout pad such as the GameSir G8 in Switch mode needs it). Lines
 * it does not know are kept as they are. */
#ifndef PAD2C_CONFIG_H
#define PAD2C_CONFIG_H

#include "pad.h"

void config_load(const char *path);
int  config_swap(const char *name);
int  config_set_swap(const char *name, int on);     /* saved at once; 0 if it could not be */

/* Cross with circle, square with triangle. */
void config_apply_swap(pad_state *st);

#endif
