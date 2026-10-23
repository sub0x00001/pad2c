# Changelog

## 1.3.0
- **Vibration in PS4 games**, on controllers in XInput mode and Switch Pro controllers: padbridge reads what the game asks of the virtual
  DualSense (`scePadVirtualDeviceGetRemoteSetting`, large and small motors as on a DualShock 4) every 16 ms and sends
  it as XInput rumble. Tested with Crash Bandicoot N. Sane Trilogy: the Ultimate 2C in XInput mode, and a DualSense
  through the USB Wireless Adapter 2 in XInput mode. Rumble stops
  when the console turns a controller off and when padbridge stops. The log says when a controller's rumble starts
  and stops.
- **Switch Pro Controller over USB** (`057e:2009`): the USB Wireless Adapter 2 in its **Switch mode** (- + L), with
  **vibration** for Switch-type controllers paired with it (tested: GameSir G8 Plus Galileo in Switch mode). The
  adapter passes rumble in XInput mode to PlayStation controllers only. padbridge does the USB handshake and
  subcommands at connection, reads report 0x30 by position (Capture as the touchpad), and sends HD Rumble encoded
  as SDL does.
- **View + Menu together as the touchpad** on XInput controllers, which have no touchpad and, for the Ultimate 2C,
  no L4/R4 in that mode. View or Menu alone waits 60 ms for the other, so the touchpad never opens the Create menu.
- Not possible, found out: PS5 games do not rumble (the console turns their requests into DualSense haptic audio
  before the virtual DualSense sees them); the Ultimate 2C does not rumble in DInput mode (8BitDo documents rumble
  for XInput only); its Star and Mapping buttons never reach the console.

## 1.2.0
- **pad2c is now padbridge**: it is no longer about one controller. The payloads are `padbridge-<version>.elf` and
  `padbridge-probe.elf`, notifications read "padbridge by Sub", and its files live in `/data/padbridge`. On its first
  start it asks a running pad2c to stop and moves `/data/pad2c` there, remembered users and settings included.
- The log names, once per device, every USB device plugged in that padbridge does not read: a controller in another
  mode shows up there (the GameSir G8 on its cable in Switch mode is `057e:2009` "Pro Controller").
- Tested: the Ultimate 2C on a USB cable (XInput, `2dc8:310a`), in the USB-A and USB-C ports.
- README rewritten around what is supported and tested: the Ultimate 2C dongle, the USB Wireless Adapter 2 and the
  controllers tested through it, controllers on a USB cable, and the settings page (with a screenshot).

## 1.1.0
- The **8BitDo USB Wireless Adapter 2** (#1): any controller it pairs with (tested: DualSense, Ultimate 2C, GameSir G8
  Plus Galileo) becomes a virtual DualSense. Plugged in, the adapter tries several identities and keeps the one whose
  host answers like Windows: pad2c now looks for new pads every 100 ms and sends XInput pads what Windows sends (the
  Microsoft OS string and the XUSB request) before the LED, so the adapter settles in XInput by itself instead of its
  DualShock 4 mode, which stops sending after a moment. DInput (View + Left) works too.
- **Several controllers at once**, up to four, each its own virtual DualSense and its own remembered user
  (`/data/pad2c/owner`, `owner.1`, ...).
- The keyboard and mouse the adapter (and the 2C receiver in XInput mode) add beside the pad are detached from the
  console's driver: the console lists only the controller among its accessories.
- **Settings page** on port 8090 (address in the start notification): the controllers live, and a **Nintendo
  layout** switch per controller (A/B and X/Y swapped, kept in `/data/pad2c/config`) for pads such as the G8 in
  Switch mode.
- DInput: a DualSense's touchpad click through the adapter.
- Wired XInput controllers (tested: GameSir G8 on its cable) work as well.
- The adapter is named "8BitDo USB Wireless Adapter 2" in the notifications.
- `pad2c-probe`: the product and manufacturer names, the device release, 90 s of reports.

## 1.0.0
- The Ultimate 2C receiver in XInput mode works (issue #2). When the controller connects, that receiver enumerates
  as `2dc8:310a` with an XInput interface (`ff/5d/01`), a keyboard and a vendor HID; 0.2.1 read the keyboard. pad2c
  now picks the interface from the descriptor: XInput first, from any vendor, then on an 8BitDo the first HID that is
  not a boot keyboard or mouse. XInput reports are mapped like the DualSense layout, and the player LED is set as a
  PC does. L4/R4 send nothing in XInput mode.
- Notifications: every one starts with "pad2c by Sub", then what happened, all in English, e.g.
  "8BitDo Ultimate 2C Wireless Controller (XInput) connected". The name is the controller's own, read from its USB
  string descriptor (`USB_GET_DEVICEINFO` fails on the dongle), and the mode is DInput or XInput.
- `tools/setup-macos.sh`: builds on macOS with Homebrew LLVM.

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
