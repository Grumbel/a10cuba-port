#!/usr/bin/env bash
set -euo pipefail

usage() {
  printf 'Usage: a10-cube-wine [--disc DIRECTORY | --iso IMAGE]\n'
  printf 'First run: use an extracted disc or ISO to prepare a writable game copy.\n'
}

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
state="$state_home/a10-cuba-wine"
game="$state/game"
mkdir -p -- "$state"
exec 9>"$state/.setup.lock"
flock 9

if [[ ! -e "$game" ]]; then
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
  temporary=$(mktemp -d "$state/.setup.XXXXXXXX")
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
  mv -T -- "$temporary/game" "$game"
  rm -rf -- "$temporary"
  trap - EXIT
  printf 'Prepared game files in %s\n' "$game"
fi
if [[ ! -f "$game/A10Cuba.exe" || ! -f "$game/A10Sim.dll" ]]; then
  printf 'Existing game copy is incomplete: %s\n' "$game" >&2
  exit 1
fi
flock -u 9
exec 9>&-
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
exec wine A10Cuba.exe
