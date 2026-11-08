#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
disc="$root/data/disc"
reference="$root/data/wine-reference"
game="$reference/game"

if [[ ! -f "$disc/A-10Cuba/A10Cuba.exe" || ! -f "$disc/CTL3D32.DLL" ]]; then
  printf 'Extract the disc first: ./scripts/extract-disc.sh\n' >&2
  exit 1
fi
if ! command -v wine >/dev/null; then
  printf 'Wine is required for the reference run.\n' >&2
  exit 1
fi
mkdir -p "$reference"
if [[ ! -e "$game" ]]; then
  temporary=$(mktemp -d "$reference/.game.XXXXXX")
  trap 'rm -rf -- "$temporary"' EXIT
  cp -R -- "$disc/A-10Cuba/." "$temporary/"
  cp -- "$disc/CTL3D32.DLL" "$temporary/"
  chmod -R u+w "$temporary"
  mv -T -- "$temporary" "$game"
  trap - EXIT
fi
export WINEPREFIX="$reference/prefix"
unset WINEARCH
cd "$game"
exec wine A10Cuba.exe
