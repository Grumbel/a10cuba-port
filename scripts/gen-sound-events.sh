#!/bin/sh
# Regenerate src/pe32_sound_events.h from share/sound-events.txt.
cd "$(dirname "$0")/.." || exit 1
{
    echo '/* Generated from share/sound-events.txt by scripts/gen-sound-events.sh. */'
    echo 'static const char *const sound_events[][2] = {'
    awk 'NF==2{printf "    {\"%s\", \"Media\\\\%s\"},\n",$1,$2}' share/sound-events.txt
    echo '};'
} > src/pe32_sound_events.h
