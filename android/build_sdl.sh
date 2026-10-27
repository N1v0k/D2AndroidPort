#!/bin/bash
# Builds libSDL2.so (SDL 2.32) for arm64-v8a with the sysroot from toolchain/build_sysroot.sh.
# Same sources as SDL's Android.mk, without HIDAPI (C++), OpenSL ES and AAudio (audio goes through
# SDL's Java AudioTrack driver) and without the GLES1 renderer and Vulkan.
# usage: build_sdl.sh <SDL source> <build dir>
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
. "$HERE/env.sh"
SDL=$(realpath "$1"); B=$(realpath -m "$2")
mkdir -p "$B/obj" "$B/include"
cp "$SDL"/include/*.h "$B/include/"
# config: drop the drivers we do not build
sed -i -E '/#define (SDL_AUDIO_DRIVER_OPENSLES|SDL_AUDIO_DRIVER_AAUDIO|SDL_JOYSTICK_HIDAPI|SDL_VIDEO_OPENGL_ES|SDL_VIDEO_RENDER_OGL_ES|SDL_VIDEO_VULKAN)[[:space:]]/d' \
    "$B/include/SDL_config_android.h"
sed -i 's|#define SDL_FILESYSTEM_ANDROID   1|&\n#define SDL_HIDAPI_DISABLED 1|' "$B/include/SDL_config_android.h"
grep -q SDL_HIDAPI_DISABLED "$B/include/SDL_config_android.h"

DIRS="src src/audio src/audio/android src/audio/dummy src/core/android src/cpuinfo src/dynapi src/events
 src/file src/haptic src/haptic/android src/joystick src/joystick/android src/joystick/dummy src/joystick/virtual
 src/joystick/hidapi src/hidapi
 src/loadso/dlopen src/locale src/locale/android src/misc src/misc/android src/power src/power/android
 src/filesystem/android src/sensor src/sensor/android src/render src/render/opengles2 src/render/software
 src/stdlib src/thread src/thread/pthread src/timer src/timer/unix src/video src/video/android src/video/yuv2rgb"
SRCS=""
for d in $DIRS; do SRCS="$SRCS $(ls "$SDL"/$d/*.c)"; done
SRCS="$SRCS $SDL/src/atomic/SDL_atomic.c $SDL/src/atomic/SDL_spinlock.c"

CF="$CFLAGS_COMMON -I$B/include -I$SDL/src/video/khronos -DGL_GLEXT_PROTOTYPES -Wno-unused-parameter -Wno-sign-compare -w"
objs=()
pids=()
n=0
for s in $SRCS; do
    rel=${s#$SDL/}; o="$B/obj/${rel//\//_}.o"; objs+=("$o")
    if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
        $CC $CF -c "$s" -o "$o" &
        pids+=($!); n=$((n+1))
        if [ $((n % 8)) -eq 0 ]; then for p in "${pids[@]}"; do wait $p; done; pids=(); fi
    fi
done
for p in "${pids[@]}"; do wait $p; done
link_so "$B/libSDL2.so" libSDL2.so "${objs[@]}"
echo "built $B/libSDL2.so"
