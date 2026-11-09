#!/bin/sh
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

if [ "${1:-}" = "--help" ]; then
    printf '%s\n' \
        'Usage: nix run .#a10cuba-native [-- --link]' \
        '       nix run .#a10cuba-native -- IMAGE [DLL] [--link]' \
        'Default: inspect local extracted A10Cuba.exe and A10Sim.dll.' \
        'Use --link to report missing Windows imports (not playable yet).' \
        'Set A10_CUBA_DATA_DIR for another extracted game directory.'
    exit 0
fi

game_dir=${A10_CUBA_DATA_DIR:-./data/disc/A-10Cuba}
use_default=0
if [ "$#" -eq 0 ]; then
    set -- "$game_dir/A10Cuba.exe" "$game_dir/A10Sim.dll"
    use_default=1
elif [ "$#" -eq 1 ] && [ "$1" = "--link" ]; then
    set -- "$game_dir/A10Cuba.exe" "$game_dir/A10Sim.dll" --link
    use_default=1
fi

if [ "$use_default" -eq 1 ] &&
   { [ ! -r "$game_dir/A10Cuba.exe" ] || [ ! -r "$game_dir/A10Sim.dll" ]; }; then
    printf '%s\n' \
        "a10cuba-native: original game binaries not found in $game_dir" \
        'Extract your own disc with scripts/extract-disc.sh or set A10_CUBA_DATA_DIR.' >&2
    exit 1
fi

exec "${A10_PE32_NATIVE_BIN:-./build/a10-pe32-native}" "$@"
