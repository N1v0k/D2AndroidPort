# Shared settings for the Android build scripts (source it)
API=${API:-29}
TRIPLE=aarch64-linux-android
SYSROOT=${SYSROOT:-/root/ndk-sysroot}
LIBDIR=$SYSROOT/usr/lib/$TRIPLE/$API
LIBGCC=${LIBGCC:-$(ls /usr/lib/gcc-cross/aarch64-linux-gnu/*/libgcc.a | sort -V | tail -1)}
CC="clang --target=$TRIPLE$API --sysroot=$SYSROOT -isystem $SYSROOT/usr/include/$TRIPLE"
# -mno-outline-atomics: the LSE helpers would come from libgcc, which looks for glibc's __getauxval
CFLAGS_COMMON="-fPIC -O2 -g0 -DANDROID -fno-emulated-tls -mno-outline-atomics -fstack-protector-strong -fdata-sections -ffunction-sections"
# 16 KB page alignment (Android 15 devices may use 16 KB pages), lazy-free relro, no undefined symbols
LDFLAGS_COMMON="-fuse-ld=lld -nostdlib -shared -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now \
 -Wl,-z,noexecstack -Wl,--hash-style=both -Wl,--build-id=sha1 -Wl,--gc-sections -Wl,--as-needed"
link_so() { # out soname objs... (then -l flags)
    local out=$1 soname=$2; shift 2
    $CC $LDFLAGS_COMMON -Wl,-soname,$soname -o "$out" "$LIBDIR/crtbegin_so.o" "$@" \
        -L"$LIBDIR" -lc -lm -ldl -llog -landroid -lGLESv2 "$LIBGCC" "$LIBDIR/crtend_so.o"
}
