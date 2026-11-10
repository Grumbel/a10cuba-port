#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the freestanding i386 DirectSound mixer test and run it under qemu.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-/tmp/a10cuba-test-dsound}
command -v qemu-i386 >/dev/null 2>&1 || { echo "qemu-i386 not found"; exit 77; }
"${CC:-gcc}" -m32 -std=gnu11 -O1 -ffreestanding -fno-builtin -fno-stack-protector \
    -fno-pie -fno-pic -mincoming-stack-boundary=2 -nostdlib -static -no-pie \
    -I"$root/src" "$root/tests/test_pe32_dsound.c" "$root/src/pe32_dsound.c" \
    "$root/src/pe32_stubs.c" "$root/src/pe32_stubs.S" -o "$out"
exec qemu-i386 "$out"
