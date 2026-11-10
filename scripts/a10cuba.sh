#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke
# SPDX-License-Identifier: GPL-3.0-or-later
# Native launcher: runs the original game in the i386 runner with an SDL host.
set -euo pipefail

usage() {
  printf '%s\n' \
    'Usage: a10cuba [--disc DIRECTORY | --iso IMAGE | --game-dir DIRECTORY]' \
    '               [--mission N] [--scale N] [--no-qemu] [-- host options]' \
    '--mission N starts list entry N (1-16: 4 training missions, then 12) and' \
    'skips the mission screen; without it the native mission screen is shown.' \
    'First run needs --iso or --disc (or run from the port directory) to copy' \
    'the game files into the XDG data directory (a10cuba-native/game). --game-dir uses an' \
    'existing directory containing A10Cuba.exe and A10Sim.dll instead.' \
    'Controls: keyboard and mouse as in the original; gamepad layers are' \
    'documented in docs/gamepad.md (LB/RB act as shift keys).'
}

source_kind=auto
source_path=
game_dir=${A10_CUBA_DATA_DIR:-}
host_args=()
while (($#)); do
  case $1 in
    --help|-h) usage; exit 0 ;;
    --disc|--iso|--game-dir)
      if (($# < 2)); then usage >&2; exit 2; fi
      case $1 in
        --disc) source_kind=disc; source_path=$2 ;;
        --iso) source_kind=iso; source_path=$2 ;;
        --game-dir) game_dir=$2 ;;
      esac
      shift 2
      ;;
    --) shift; host_args+=("$@"); break ;;
    *) host_args+=("$1"); shift ;;
  esac
done

if [[ -z $game_dir ]]; then
  if [[ ${XDG_DATA_HOME:-} == /* ]]; then
    data_home=$XDG_DATA_HOME
  elif [[ ${HOME:-} == /* ]]; then
    data_home=$HOME/.local/share
  else
    printf 'Set HOME or an absolute XDG_DATA_HOME to use this launcher.\n' >&2
    exit 1
  fi
  data="$data_home/a10cuba-native"
  game_dir="$data/game"
  if [[ ! -f "$game_dir/A10Cuba.exe" ]]; then
    if [[ $source_kind == auto ]]; then
      if [[ -f "$PWD/data/disc/A-10Cuba/A10Cuba.exe" ]]; then
        source_kind=disc
        source_path="$PWD/data/disc"
      elif [[ -f "$PWD/../A-10 Cuba! (1996)(Activision).iso" ]]; then
        source_kind=iso
        source_path="$PWD/../A-10 Cuba! (1996)(Activision).iso"
      else
        printf 'Game files not found. Pass --disc DIRECTORY or --iso IMAGE.\n' >&2
        exit 1
      fi
    fi
    mkdir -p -- "$data"
    temporary=$(mktemp -d "$data/.setup.XXXXXXXX")
    trap 'rm -rf -- "$temporary"' EXIT
    if [[ $source_kind == iso ]]; then
      if [[ ! -f $source_path ]]; then
        printf 'ISO not found: %s\n' "$source_path" >&2
        exit 1
      fi
      mkdir -p "$temporary/disc"
      7z x -y "-o$temporary/disc" -- "$source_path" 'A-10Cuba/*' CTL3D32.DLL >/dev/null
      source_path="$temporary/disc"
    fi
    for path in A-10Cuba/A10Cuba.exe A-10Cuba/A10Sim.dll A-10Cuba/Missions/CubaTerrain.db; do
      if [[ ! -f "$source_path/$path" ]]; then
        printf 'Required disc file not found: %s\n' "$source_path/$path" >&2
        exit 1
      fi
    done
    mkdir -p "$temporary/game"
    cp -R -- "$source_path/A-10Cuba/." "$temporary/game/"
    chmod -R u+w "$temporary/game"
    rm -rf -- "$game_dir"
    mv -T -- "$temporary/game" "$game_dir"
    trap - EXIT
    rm -rf -- "$temporary"
    printf 'Prepared game files in %s\n' "$game_dir"
  fi
fi
if [[ ! -f "$game_dir/A10Cuba.exe" || ! -f "$game_dir/A10Sim.dll" ]]; then
  printf 'Game directory is incomplete: %s\n' "$game_dir" >&2
  exit 1
fi

# Native screens (mission list ...) use bitmaps extracted from the exe once.
if [[ ${XDG_DATA_HOME:-} == /* ]]; then
  ui_dir=$XDG_DATA_HOME/a10cuba-native/ui
elif [[ ${HOME:-} == /* ]]; then
  ui_dir=$HOME/.local/share/a10cuba-native/ui
else
  ui_dir=
fi
ui_dir=${A10_CUBA_UI_DIR:-$ui_dir}
ui_tools=${A10_UI_TOOLS:-$(dirname -- "${BASH_SOURCE[0]}")/../tools}
ui_args=()
if [[ -n $ui_dir ]]; then
  if [[ ! -f "$ui_dir/bmp/142.bmp" ]]; then
    python3 "$ui_tools/extract_ui_assets.py" "$game_dir/A10Cuba.exe" --out "$ui_dir" >/dev/null ||
      printf 'Could not extract UI assets; using the original dialogs.\n' >&2
  fi
  if [[ -f "$ui_dir/bmp/142.bmp" ]]; then
    ui_args=(--ui-dir "$ui_dir")
  fi
fi

host=${A10_SDL_HOST:-./build/a10-sdl-host}
exec "$host" --game-dir "$game_dir" "${ui_args[@]}" "${host_args[@]}"
