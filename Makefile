# pad2c: the 8BitDo dongle as a second DualSense on a jailbroken PS5.
#
#   make test       the mapping test, on this machine
#   make            the payloads into dist/ (needs PS5_PAYLOAD_SDK)

VERSION := $(shell sed -n 's/^\#define PAD2C_VERSION "\(.*\)"/\1/p' src/version.h)

all: dist/pad2c-$(VERSION).elf dist/pad2c-probe.elf

test:
	@mkdir -p build
	cc -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_map src/map8bitdo.c tests/test_map.c
	./build/test_map

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

CFLAGS := -std=gnu11 -Wall -Wextra -O2 -Isrc
LDLIBS := -lScePad -lSceUserService -ldl

SRCS := src/main.c src/usbpad.c src/map8bitdo.c src/ps5_vpad.c src/ps5_power.c src/lock.c src/log.c src/util.c

dist/pad2c-$(VERSION).elf: $(SRCS) $(wildcard src/*.h)
	@mkdir -p dist
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

dist/pad2c-probe.elf: src/probe.c
	@mkdir -p dist
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf dist build

.PHONY: all test clean
