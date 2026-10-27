#!/bin/bash
# Whole Android build, without the Google SDK/NDK downloads: sysroot from AOSP sources, SDL2, the game, the APK.
#   sudo apt install clang lld llvm aapt zipalign apksigner dalvik-exchange default-jdk-headless \
#       android-platform-frameworks-native-headers android-liblog-dev android-libnativehelper-dev \
#       gcc-aarch64-linux-gnu python3 git zip
#   git clone --depth 1 -b android-14.0.0_r9 https://github.com/aosp-mirror/platform_bionic $SRC/bionic
#   (sparse) aosp-mirror/platform_frameworks_base at android-14.0.0_r9: native/android/libandroid.map.txt
#   (sparse) aosp-mirror/platform_system_core at android-11.0.0_r48: liblog/liblog.map.txt
#   git clone --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL $SRC/SDL2
#   git clone --depth 1 https://github.com/ladislav-zezula/CascLib $SRC/CascLib
#   an android.jar of API 34 (compile-time only), e.g. from github.com/Sable/android-platforms
# usage: build.sh [out.apk]     (SRC, BUILD, SYSROOT, ANDROID_JAR can be set)
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
SRC=${SRC:-/root/src}
BUILD=${BUILD:-/root/build}
export SYSROOT=${SYSROOT:-/root/ndk-sysroot}
ANDROID_JAR=${ANDROID_JAR:-$SRC/andplat/android-34/android.jar}
OUT=${1:-$BUILD/out/d2port.apk}
if [ ! -f "$SYSROOT/usr/lib/aarch64-linux-android/29/libc.so" ]; then
    bash "$HERE/toolchain/build_sysroot.sh" "$SRC/bionic" "$SRC/fbase/native/android/libandroid.map.txt" \
        "$SRC/score/liblog/liblog.map.txt" "$SYSROOT" 29
fi
bash "$HERE/build_sdl.sh" "$SRC/SDL2" "$BUILD/sdl"
bash "$HERE/build_game.sh" "$(dirname "$HERE")" "$BUILD/sdl" "$BUILD/game"
bash "$HERE/build_importer.sh" "$(dirname "$HERE")" "$SRC/CascLib" "$BUILD/importer"
mkdir -p "$(dirname "$OUT")"
bash "$HERE/build_apk.sh" "$SRC/SDL2" "$BUILD/sdl" "$BUILD/game" "$ANDROID_JAR" "$OUT" "$BUILD/importer"
