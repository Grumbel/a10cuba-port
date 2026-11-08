#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
iso=${1:-"$root/../A-10 Cuba! (1996)(Activision).iso"}
destination="$root/data/disc"

if [[ ! -f "$iso" ]]; then
  printf 'Disc image not found: %s\n' "$iso" >&2
  exit 1
fi
if [[ -e "$destination" ]]; then
  printf 'Destination already exists: %s\nMove or remove it before extracting.\n' "$destination" >&2
  exit 1
fi
mkdir -p "$root/data"
temporary=$(mktemp -d "$root/data/.disc.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT
7z x -y "-o$temporary" -- "$iso"
for path in A-10Cuba/A10Cuba.exe A-10Cuba/A10Sim.dll A-10Cuba/Missions/CubaTerrain.db; do
  if [[ ! -f "$temporary/$path" ]]; then
    printf 'Expected disc file missing: %s\n' "$path" >&2
    exit 1
  fi
done
mv -T -- "$temporary" "$destination"
printf 'Extracted disc to %s\n' "$destination"
