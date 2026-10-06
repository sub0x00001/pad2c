# Changelog

## 0.2.1
- "Turn off" in the console's menu works: the console calls `sceMbusDisconnectDevice` on the pad, which does nothing
  to a virtual one (it stayed on, and the menu then showed it as a generic "Accessories" device). pad2c now removes
  the virtual pad, as a DualSense disconnects, and Home brings it back to its user.
- All messages are in English.
- Confirmed on the console: the pad goes back to the user who last used it (0.2.0).

## 0.2.0
- Remembers the user the controller was given to (the console's "who is using this controller" screen) and gives it
  back to that user when it connects again, if signed in, as a DualSense does. `/data/pad2c/owner`.
- When the console removes the virtual pad (turned off), Home brings it back; that press is not passed on.
- The kernel log is followed while running; everything the console says about our pad is logged.

## 0.1.2
- No more "silence" timeout: the dongle only reports changes, so a controller held still looked switched off after 5 s.
  The controller sleeping or switching off is seen when the dongle enumerates again as "8BitDo IDLE".

## 0.1.1
- The dongle was taken for gone every second (`USB_GET_DEVICEINFO` fails on it).
- The virtual pad is left unassigned by default, so the console asks who uses it.

## 0.1.0
- First version: the 8BitDo Ultimate 2C 2.4G dongle as a virtual DualSense, read over USB. Tested on a PS5 Slim, 13.60.
