# pad2c

**Use an 8BitDo Ultimate 2C (2.4G dongle) as a second controller on a jailbroken PS5, next to your DualSense.**

pad2c is a payload for a jailbroken PS5. It reads the 8BitDo 2.4G dongle plugged into the console's USB port and
shows it to the system and to games as a **virtual DualSense**. No Bluetooth is involved, so it never touches your
DualSense's pairing.

## Status

| | |
|---|---|
| Tested on | PS5 Slim (CFI-2114), firmware **13.60**, elfldr 0.26, kstuff-lite 1.11, Payload Manager 0.5.2 |
| Controller | 8BitDo Ultimate 2C Wireless, 2.4G dongle (`2dc8:301c`) |
| Works | all buttons, sticks, analog triggers, d-pad, Home = PS, console menus and games, sleep and wake, the console's "who is using this controller" screen, remembers its user, "turn off" from the console's menu |
| Not yet | vibration, light bar, motion sensors, audio |

Other firmwares and console models are untested.

Notes:

- The 8BitDo has no touchpad: its extra **L4 / R4** buttons press the touchpad.
- "Turn off" in the console's menu disconnects the controller from the console, as with a DualSense, and Home
  connects it again. The 8BitDo itself stays on until it goes to sleep by itself (the dongle's commands to switch
  it off are not known).
- Vibration: PS5 games send DualSense haptics as audio to the controller, and a virtual DualSense has no known way
  to receive them yet.

## Button map

| 8BitDo | DualSense |
|---|---|
| A / B / X / Y | ✕ / ○ / □ / △ (by position) |
| LB / RB / LT / RT | L1 / R1 / L2 / R2 (analog) |
| View / Menu | Create / Options |
| Home | PS |
| L3 / R3 | L3 / R3 |
| L4 / R4 | touchpad click |

## Install

1. Plug the dongle into a USB port of the PS5 and switch the controller on in 2.4G mode.
2. Send `pad2c-<version>.elf` to your payload loader (elfldr listens on port 9021), or add it to Payload Manager
   and to its autoload list, so that it starts with the console.
3. A notification says it is running. Press **Home** on the 8BitDo: the console asks who is using the controller.
   For a second player, pick a **second user** (a game takes one controller per user). pad2c remembers the choice
   and gives the controller back to that user next time, as a DualSense does.

Files, all in `/data/pad2c/`:

| File | |
|---|---|
| `pad2c.log` | the log |
| `stop` | create it to stop pad2c (it also stops by itself before rest mode) |
| `owner` | the user the controller was last given to (delete it to forget) |
| `bind_user` | optional: `main`, `other` or a hexadecimal user id, to give the controller to that user at once |

## How it works

- `src/usbpad.c` opens the dongle's `/dev/ugenB.A` node and reads its interrupt IN endpoint. The console's own HID
  driver holds the interface but ignores the device. The internal Bluetooth chip (`ugen0.2`) is never touched.
- With no controller the dongle shows up as "8BitDo IDLE". When the controller connects, the dongle enumerates again
  and sends report 1. `src/map8bitdo.c` turns that report into DualSense buttons (`make test` checks it against
  reports captured on a console). When the controller sleeps, the dongle enumerates again and the open device ends.
- `src/ps5_vpad.c` creates the virtual DualSense (`scePadVirtualDevice*`), finds its device id in the kernel log,
  gives it to a user (`sceMbusBindDeviceWithUserId`) and feeds it. It follows the kernel log while running: the
  console's choice of user is remembered, and its "turn off" (`sceMbusDisconnectDevice`) removes the virtual pad.
- `src/probe.c` is a read-only diagnostic payload: it dumps the USB descriptors and the reports of whatever is
  plugged in. Useful to add support for another USB controller.

## Build

Needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and clang, lld and llvm (Linux or WSL):

```
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make test     # the button map, on the PC
make          # dist/pad2c-<version>.elf and dist/pad2c-probe.elf
```

## Credits

- The virtual DualSense code (`ps5_vpad.c`) and the helpers `log.c`, `util.c`, `lock.c`, `ps5_power.c` and `pad.h`
  come from **AnyPad PS5** by **@elmonomalvad0** (sinfiltros), GPL-3.0-or-later, adapted here.
- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) by John Törnblom.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

No Sony code, keys or firmware are part of this project. pad2c does not jailbreak anything: it needs a console
that already runs a payload loader.
