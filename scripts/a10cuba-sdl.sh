#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

if [ "${1:-}" = "--help" ]; then
    printf '%s\n' \
        'Usage: scripts/a10cuba-sdl.sh [a10-sdl-host options]' \
        'Builds the i386 runner and SDL host, then plays the original game from' \
        'A10_CUBA_DATA_DIR (default ./data/disc/A-10Cuba) in an SDL window.' \
        'Options: --scale N, --max-frames N, --snapshot OUT.bmp, --no-qemu.'
    exit 0
fi

./scripts/build-native-pe32.sh >/dev/null
cc -std=gnu11 -O2 -Wall -Wextra src/sdl_host.c -o build/a10-sdl-host \
    $(pkg-config --cflags --libs sdl3)
exec ./build/a10-sdl-host "$@"
