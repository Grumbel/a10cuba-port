#!/usr/bin/env bash
set -euo pipefail

usage() {
  printf 'Usage: a10-cube-wine [--disc DIRECTORY | --iso IMAGE]\n'
  printf 'First run: use an extracted disc or ISO to prepare a writable game copy.\n'
}

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_kind=auto
source_path=
case $# in
  0) ;;
  1) if [[ $1 == --help || $1 == -h ]]; then usage; exit 0; else usage >&2; exit 2; fi ;;
  2)
    case $1 in
      --disc) source_kind=disc ;;
      --iso) source_kind=iso ;;
      *) usage >&2; exit 2 ;;
    esac
    source_path=$2
    ;;
  *) usage >&2; exit 2 ;;
esac

if [[ ${XDG_STATE_HOME:-} == /* ]]; then
  state_home=$XDG_STATE_HOME
elif [[ ${HOME:-} == /* ]]; then
  state_home=$HOME/.local/state
else
  printf 'Set HOME or an absolute XDG_STATE_HOME to use this launcher.\n' >&2
  exit 1
fi
if [[ ${XDG_DATA_HOME:-} == /* ]]; then
  data_home=$XDG_DATA_HOME
elif [[ ${HOME:-} == /* ]]; then
  data_home=$HOME/.local/share
else
  printf 'Set HOME or an absolute XDG_DATA_HOME to use this launcher.\n' >&2
  exit 1
fi
state="$state_home/a10-cuba-wine"
data="$data_home/a10-cuba-wine"
installed_game="$data/game"
game="$state/game"
mkdir -p -- "$state" "$data"
exec 9>"$state/.setup.lock"
flock 9
exec 8>"$data/.setup.lock"
flock 8

if [[ ! -e "$installed_game" && -f "$game/A10Cuba.exe" && -f "$game/A10Sim.dll" ]]; then
  # Preserve an older installation and every unknown save file in place.
  temporary=$(mktemp -d "$data/.setup.XXXXXXXX")
  trap 'rm -rf -- "$temporary"' EXIT
  mkdir "$temporary/game"
  for path in A10Cuba.exe A10Sim.dll A10CUBA.TIP CTL3D32.DLL Media Missions LoadOuts; do
    if [[ -e "$game/$path" ]]; then
      cp -RL -- "$game/$path" "$temporary/game/"
    fi
  done
  mv -T -- "$temporary/game" "$installed_game"
  rm -rf -- "$temporary"
  trap - EXIT
  printf 'Copied existing game files to %s; kept existing working files in %s\n' "$installed_game" "$game"
fi

if [[ ! -e "$installed_game" ]]; then
  if [[ $source_kind == auto ]]; then
    if [[ -f "$PWD/data/disc/A-10Cuba/A10Cuba.exe" ]]; then
      source_kind=disc
      source_path="$PWD/data/disc"
    elif [[ -f "$PWD/../A-10 Cuba! (1996)(Activision).iso" ]]; then
      source_kind=iso
      source_path="$PWD/../A-10 Cuba! (1996)(Activision).iso"
    else
      printf 'Game disc not found. Run from the port directory or pass --disc DIRECTORY or --iso IMAGE.\n' >&2
      exit 1
    fi
  fi
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
  for path in A-10Cuba/A10Cuba.exe A-10Cuba/A10Sim.dll A-10Cuba/Missions/CubaTerrain.db CTL3D32.DLL; do
    if [[ ! -f "$source_path/$path" ]]; then
      printf 'Required disc file not found: %s\n' "$source_path/$path" >&2
      exit 1
    fi
  done
  mkdir -p "$temporary/game"
  cp -R -- "$source_path/A-10Cuba/." "$temporary/game/"
  cp -- "$source_path/CTL3D32.DLL" "$temporary/game/"
  chmod -R u+w "$temporary/game"
  mv -T -- "$temporary/game" "$installed_game"
  rm -rf -- "$temporary"
  trap - EXIT
  printf 'Prepared game files in %s\n' "$installed_game"
fi
if [[ ! -e "$game" ]]; then
  temporary=$(mktemp -d "$state/.setup.XXXXXXXX")
  trap 'rm -rf -- "$temporary"' EXIT
  mkdir "$temporary/game"
  cp -as -- "$installed_game/." "$temporary/game/"
  # Loadouts are player-editable, so keep their writable copies in state.
  if [[ -d "$installed_game/LoadOuts" ]]; then
    rm -rf -- "$temporary/game/LoadOuts"
    cp -R -- "$installed_game/LoadOuts" "$temporary/game/LoadOuts"
    chmod -R u+w "$temporary/game/LoadOuts"
  fi
  mv -T -- "$temporary/game" "$game"
  rm -rf -- "$temporary"
  trap - EXIT
fi
if [[ ! -f "$game/A10Cuba.exe" || ! -f "$game/A10Sim.dll" ]]; then
  printf 'Existing game copy is incomplete: %s\n' "$game" >&2
  exit 1
fi
flock -u 8
exec 8>&-
# Wine can link its Documents and other folders to $HOME through XDG user dirs.
# Give it a private home and XDG locations so game writes stay in the state tree.
if [[ -z ${XAUTHORITY:-} && -f ${HOME:-}/.Xauthority ]]; then
  export XAUTHORITY="$HOME/.Xauthority"
fi
export WINEPREFIX="$state/prefix"
export HOME="$state/home"
export XDG_CONFIG_HOME="$state/xdg/config"
export XDG_DATA_HOME="$state/xdg/data"
export XDG_CACHE_HOME="$state/xdg/cache"
mkdir -p -- "$HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
unset WINEARCH
# The game does not use .NET or embedded Internet Explorer; skip optional installers.
export WINEDLLOVERRIDES="mscoree,mshtml=;${WINEDLLOVERRIDES:-}"
cd "$game"
# Register missing event sounds only in this prefix; never replace existing choices.
sound_map=${A10_CUBA_SOUND_MAP:-"$script_dir/../share/sound-events.txt"}
sound_marker="$state/.sound-events-v1"
if [[ ! -f "$sound_marker" ]]; then
  if [[ ! -f "$sound_map" ]]; then
    printf 'Sound event mapping file not found: %s\n' "$sound_map" >&2
    exit 1
  fi
  missing_media=0
  windows_media=$(winepath -w "$game/Media")
  windows_media=${windows_media//$'\r'/}
  while read -r event filename; do
    [[ -n $event && -n $filename ]] || continue
    if [[ ! -f "$game/Media/$filename" ]]; then
      printf 'Sound file missing, skipping %s: %s\n' "$event" "$filename" >&2
      missing_media=1
      continue
    fi
    registry_key="HKCU\\AppEvents\\Schemes\\Apps\\A10Cuba\\$event\\.Current"
    if ! wine reg query "$registry_key" /ve >/dev/null 2>&1; then
      windows_path="$windows_media\\$filename"
      wine reg add "$registry_key" /ve /t REG_SZ /d "$windows_path" /f >/dev/null
    fi
  done < "$sound_map"
  if (( missing_media == 0 )); then
    : > "$sound_marker"
  fi
fi
flock -u 9
exec 9>&-
desktop_size=${A10_CUBA_DESKTOP_SIZE:-1024x768}
if [[ ! $desktop_size =~ ^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$ ]]; then
  printf 'Invalid A10_CUBA_DESKTOP_SIZE: %s (expected WIDTHxHEIGHT, e.g. 1024x768)\n' "$desktop_size" >&2
  exit 1
fi
exec wine explorer "/desktop=A10Cuba,$desktop_size" A10Cuba.exe
