#!/bin/bash
# Builds an NDK-like sysroot for aarch64-linux-android from AOSP sources, for use with
# the host clang/lld (no Google NDK download needed):
#   - headers: bionic (libc + kernel uapi), Android framework headers (Ubuntu
#     android-platform-frameworks-native-headers, android-liblog-dev, android-libnativehelper-dev)
#   - stub libraries (libc, libm, libdl, liblog, libandroid) generated from the AOSP .map.txt files
#   - crtbegin_so.o / crtend_so.o compiled from bionic
# usage: build_sysroot.sh <bionic checkout> <libandroid.map.txt> <liblog.map.txt> <out dir> [api]
set -euo pipefail
BIONIC=$(realpath "$1"); ANDROID_MAP=$(realpath "$2"); LOG_MAP=$(realpath "$3")
OUT=$(realpath -m "$4"); API=${5:-29}
HERE=$(dirname "$(realpath "$0")")
TRIPLE=aarch64-linux-android
INC=$OUT/usr/include
LIB=$OUT/usr/lib/$TRIPLE/$API
rm -rf "$OUT"; mkdir -p "$INC/$TRIPLE" "$LIB"

# --- headers ---
cp -r "$BIONIC"/libc/include/. "$INC/"
cp -r "$BIONIC"/libc/kernel/uapi/. "$INC/"
cp -r "$BIONIC"/libc/kernel/android/uapi/. "$INC/"
cp -r "$BIONIC"/libc/kernel/android/scsi "$INC/" 2>/dev/null || true
rm -rf "$INC"/asm-arm "$INC"/asm-arm64 "$INC"/asm-riscv "$INC"/asm-x86
cp -r "$BIONIC"/libc/kernel/uapi/asm-arm64/asm "$INC/$TRIPLE/asm"
# Android framework headers (NDK public API) from the Ubuntu packages
cp -r /usr/include/android/android/. "$INC/android/"
cp /usr/include/android/log.h "$INC/android/log.h"
cp /usr/include/android/jni.h "$INC/jni.h"
cp "$HERE/native_window.h" "$INC/android/native_window.h"
cp "$HERE/rect.h" "$INC/android/rect.h"
cp "$HERE/hardware_buffer.h" "$INC/android/hardware_buffer.h"
find "$INC" -name OWNERS -delete -o -name BUILD -delete -o -name '*.bp' -delete

# --- stub libraries ---
W=$(mktemp -d)
CC="clang --target=$TRIPLE$API"
stub() { # name map
    python3 "$HERE/gen_stubs.py" "$2" arm64 "$API" "$W/$1.c" "$W/$1.vs"
    $CC -fPIC -fno-builtin -w -nostdlib -shared -fuse-ld=lld -Wl,--version-script="$W/$1.vs" \
        -Wl,-soname,"$1.so" -o "$LIB/$1.so" "$W/$1.c"
}
stub libc "$BIONIC/libc/libc.map.txt"
stub libm "$BIONIC/libm/libm.map.txt"
stub libdl "$BIONIC/libdl/libdl.map.txt"
stub liblog "$LOG_MAP"
stub libandroid "$ANDROID_MAP"

# --- crt objects ---
CRTF="-fPIC -O2 -I$BIONIC/libc -isystem $INC -isystem $INC/$TRIPLE -DPLATFORM_SDK_VERSION=$API"
$CC $CRTF -nostdinc -isystem "$(clang -print-resource-dir)/include" -c "$BIONIC/libc/arch-common/bionic/crtbegin_so.c" -o "$W/crtbegin_so_c.o"
$CC $CRTF -nostdinc -isystem "$(clang -print-resource-dir)/include" -c "$BIONIC/libc/arch-common/bionic/crtbrand.S" -o "$W/crtbrand.o"
ld.lld -r -o "$LIB/crtbegin_so.o" "$W/crtbegin_so_c.o" "$W/crtbrand.o"
$CC $CRTF -nostdinc -isystem "$(clang -print-resource-dir)/include" -c "$BIONIC/libc/arch-common/bionic/crtend_so.S" -o "$LIB/crtend_so.o"
# for executables (the importer): crtbegin_dynamic.o (_start -> __libc_init) and crtend_android.o
$CC $CRTF -nostdinc -isystem "$(clang -print-resource-dir)/include" -c "$BIONIC/libc/arch-common/bionic/crtbegin.c" -o "$W/crtbegin_c.o"
ld.lld -r -o "$LIB/crtbegin_dynamic.o" "$W/crtbegin_c.o" "$W/crtbrand.o"
$CC $CRTF -nostdinc -isystem "$(clang -print-resource-dir)/include" -c "$BIONIC/libc/arch-common/bionic/crtend.S" -o "$LIB/crtend_android.o"
rm -rf "$W"
echo "sysroot ready: $OUT (API $API)"

# --- libGLESv2 (SDL's GLES2 renderer calls the GL ES 2.0 functions directly on Android) ---
# unversioned, like the device library's exports; the list is every GLES 2.0 core entry point
GL2H=${GL2H:-/root/src/SDL2/src/video/khronos/GLES2/gl2.h}
W=$(mktemp -d)
grep -oE 'GL_APIENTRY gl[A-Za-z0-9]+' "$GL2H" | awk '{print "void " $2 "(void) {}"}' | sort -u > "$W/gles2.c"
clang --target=$TRIPLE$API -fPIC -fno-builtin -w -nostdlib -shared -fuse-ld=lld -Wl,-soname,libGLESv2.so \
    -o "$LIB/libGLESv2.so" "$W/gles2.c"
echo "libGLESv2: $(wc -l < "$W/gles2.c") symbols"
rm -rf "$W"
