#!/bin/bash
# Builds the importer (tools/d2port-importar.c + CascLib with its zlib) as an arm64 Android executable.
# It goes into the APK as lib/arm64-v8a/libd2import.so (the app runs it from its native library folder).
# CascLib is C++ but needs no C++ library: -fno-exceptions -fno-rtti and toolchain/cxx (new/delete).
# usage: build_importer.sh <d2port source> <CascLib source> <build dir>
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
. "$HERE/env.sh"
SRC=$(realpath "$1"); CASC=$(realpath "$2"); B=$(realpath -m "$3")
rm -rf "$B/obj"; mkdir -p "$B/obj"
CXX="clang++ --target=$TRIPLE$API --sysroot=$SYSROOT -isystem $SYSROOT/usr/include/$TRIPLE -nostdinc++ -isystem $HERE/toolchain/cxx"
# (the executable is PIE: same flags as the libraries, without -fPIC's cost mattering)
CF="${CFLAGS_COMMON/-fPIC/-fPIE} -DCASCLIB_NO_AUTO_LINK_LIBRARY -DCASCLIB_NODEBUG -w"
CXXF="$CF -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics"
srcs=$(cd "$CASC" && ls src/common/*.cpp src/hashes/md5.cpp src/hashes/sha1.cpp src/overwatch/apm.cpp src/overwatch/cmf.cpp src/overwatch/aes.cpp src/Casc*.cpp src/jenkins/lookup3.c \
    src/zlib/adler32.c src/zlib/crc32.c src/zlib/inffast.c src/zlib/inflate.c src/zlib/inftrees.c src/zlib/zutil.c)
objs=()
n=0
for s in $srcs; do
    o="$B/obj/$(echo "$s" | tr / _).o"; objs+=("$o")
    case $s in *.cpp) $CXX $CXXF -c "$CASC/$s" -o "$o" & ;; *) $CC $CF -c "$CASC/$s" -o "$o" & ;; esac
    n=$((n+1)); if [ $((n % 6)) -eq 0 ]; then wait; fi
done
$CXX $CXXF -c "$HERE/toolchain/cxx/cxxrt.cpp" -o "$B/obj/cxxrt.o"; objs+=("$B/obj/cxxrt.o")
$CC $CF -I"$CASC/src" -c "$SRC/tools/d2port-importar.c" -o "$B/obj/importar.o"; objs+=("$B/obj/importar.o")
wait
for o in "${objs[@]}"; do [ -f "$o" ] || { echo "missing $o"; exit 1; }; done
$CC -fuse-ld=lld -nostdlib -pie -Wl,-dynamic-linker,/system/bin/linker64 -Wl,--no-undefined \
    -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--hash-style=both \
    -Wl,--build-id=sha1 -Wl,--gc-sections -Wl,--as-needed \
    -o "$B/libd2import.so" "$LIBDIR/crtbegin_dynamic.o" "${objs[@]}" -L"$LIBDIR" -lc -lm -ldl "$LIBGCC" \
    "$LIBDIR/crtend_android.o"
echo "built $B/libd2import.so"
