# padbridge: the 8BitDo dongle as a second DualSense on a jailbroken PS5.
#
#   make test       the mapping test, on this machine
#   make            the payloads into dist/ (needs PS5_PAYLOAD_SDK)

VERSION := $(shell sed -n 's/^\#define PADBRIDGE_VERSION "\(.*\)"/\1/p' src/version.h)

all: dist/padbridge-$(VERSION).elf dist/padbridge-probe.elf

test:
	@mkdir -p build
	cc -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_map src/map8bitdo.c tests/test_map.c
	./build/test_map
	cc -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_usb src/usbdesc.c src/mapxinput.c tests/test_usb.c
	./build/test_usb
	cc -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_config src/config.c tests/test_config.c
	./build/test_config
	cc -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -Isrc -o build/test_web src/web.c src/log.c src/util.c tests/test_web.c
	./build/test_web
	cc -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_combo src/combo.c tests/test_combo.c
	./build/test_combo
	cc -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_switch src/mapswitch.c src/usbdesc.c tests/test_switch.c
	./build/test_switch

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

CFLAGS := -std=gnu11 -Wall -Wextra -O2 -Isrc
LDLIBS := -lScePad -lSceUserService -ldl

SRCS := src/main.c src/usbpad.c src/usbdesc.c src/map8bitdo.c src/mapxinput.c src/mapswitch.c src/config.c src/combo.c src/web.c src/ps5_vpad.c src/ps5_power.c src/lock.c src/log.c src/util.c

dist/padbridge-$(VERSION).elf: $(SRCS) $(wildcard src/*.h)
	@mkdir -p dist
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

dist/padbridge-probe.elf: src/probe.c src/usbdesc.c src/usbdesc.h
	@mkdir -p dist
	$(CC) $(CFLAGS) -o $@ src/probe.c src/usbdesc.c

clean:
	rm -rf dist build

.PHONY: all test clean
