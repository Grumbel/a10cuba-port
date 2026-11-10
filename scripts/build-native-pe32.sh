#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
output=${1:-"$repo_dir/build/a10-pe32-native"}
mkdir -p "$(dirname -- "$output")"

# No 32-bit libc, dynamic loader, or SDL is needed for the first bootstrap.
# This is intentionally Linux/i386 only, not a 64-bit PE interpreter.
"${CC:-gcc}" -m32 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
    -fno-builtin -fno-stack-protector -fno-pie -fno-pic \
    -fomit-frame-pointer -nostdlib -static -no-pie \
    -Wl,-e,_start -Wl,--build-id=none \
    "$repo_dir/src/pe32_start.S" "$repo_dir/src/pe32_native.c" \
    "$repo_dir/src/pe32_link.c" "$repo_dir/src/pe32_win32.c" \
    "$repo_dir/src/pe32_heap.c" "$repo_dir/src/pe32_io.c" "$repo_dir/src/pe32_proc.c" "$repo_dir/src/pe32_text.c" "$repo_dir/src/pe32_res.c" -o "$output"
printf 'Built %s\n' "$output"
