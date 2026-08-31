#!/bin/sh
set -eu

test_binary="${TMPDIR:-/tmp}/hidpilot-host-tests-$$"
trap 'rm -f "$test_binary"' EXIT INT TERM

cc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -pedantic \
  -DHIDPILOT_FIRMWARE_VERSION_MAJOR=1 \
  -DHIDPILOT_FIRMWARE_VERSION_MINOR=0 \
  -DHIDPILOT_FIRMWARE_VERSION_PATCH=0 \
  -Iinclude src/config.c src/executor.c src/flash_store.c src/protocol.c tests/test_firmware.c \
  -o "$test_binary"
"$test_binary"
