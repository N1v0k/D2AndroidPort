#!/bin/bash
# Builds libmain.so (the game, SDL_main) for arm64-v8a against libSDL2.so from build_sdl.sh.
# Same flags that matter as CMakeLists.txt (-ffp-contract=off -fsigned-char: the local network needs every
# machine to compute the same numbers) and the same version string (the git commit).
# usage: build_game.sh <d2port source> <SDL build dir> <build dir>
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
. "$HERE/env.sh"
SRC=$(realpath "$1"); SDLB=$(realpath "$2"); B=$(realpath -m "$3")
rm -rf "$B/obj"; mkdir -p "$B/obj"
# (-android: the stick aim, D2_STICK_AIM, changes what the game computes, so the local network only joins it with
# other Android builds; the number after it goes up when the Android changes do that again: 2, the original speeds)
VER=$(git -C "$SRC" rev-parse --short=12 HEAD 2>/dev/null || echo dev)-android2
CF="$CFLAGS_COMMON -O3 -DNDEBUG -std=gnu99 -ffp-contract=off -fsigned-char -Wall -Wextra -I$SDLB/include -I$SRC/src
 -DD2_VERSION=\"$VER\" -DD2_STICK_AIM"
FMT="fs sprite animdata txt dcc cof dt1 ds1 flac ttf"
GAME="main video input touch font ui charanim map world jobs bot strings lang items itemstats cube skills sequences
 missiles overlays audio cinema units menu controls game net compat dmath"
objs=()
for f in $FMT; do objs+=("$B/obj/fmt_$f.o"); $CC $CF -c "$SRC/src/fmt/$f.c" -o "$B/obj/fmt_$f.o" & done
wait
n=0
for f in $GAME; do
    objs+=("$B/obj/$f.o")
    $CC $CF -c "$SRC/src/$f.c" -o "$B/obj/$f.o" &
    n=$((n+1)); if [ $((n % 4)) -eq 0 ]; then wait; fi
done
wait
for o in "${objs[@]}"; do [ -f "$o" ] || { echo "missing $o"; exit 1; }; done
link_so "$B/libmain.so" libmain.so "${objs[@]}" -L"$SDLB" -lSDL2
echo "built $B/libmain.so (version $VER)"
