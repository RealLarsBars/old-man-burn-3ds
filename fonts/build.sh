#!/bin/sh
# Turns the menu fonts (the web game's DM Mono and Dela Gothic One, both SIL Open Font License) into the console's
# .bcfnt format in data/, keeping only the characters the game draws. Needs mkbcfnt from devkitPro.
set -e
cd "$(dirname "$0")"
MK=${MKBCFNT:-mkbcfnt}
ascii=$(seq 32 126 | tr '\n' ' ')
extra=$(printf '%s' '·—–’‘“”…→←‹›×✓■□▸▶◀▲▼•°' | python3 -c 'import sys; print(" ".join(str(ord(c)) for c in sys.stdin.read()))')
kana=$(printf '%s' 'いさじんイカガサジドレンー激突' | python3 -c 'import sys; print(" ".join(str(ord(c)) for c in sys.stdin.read()))')
echo "$ascii $extra" > /tmp/omb-mono.txt
echo "$ascii $extra $kana" > /tmp/omb-disp.txt
$MK -s 14 -w /tmp/omb-mono.txt -o ../data/mono.bin DMMono-Regular.ttf
$MK -s 28 -w /tmp/omb-disp.txt -o ../data/disp.bin DelaGothicOne-Regular.ttf
