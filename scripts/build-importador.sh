#!/bin/bash
# Compila el importador (tools/d2port-importar.c) para Linux, la Pi (aarch64)
# y Windows, cada uno con su CascLib estática (con la zlib que trae CascLib),
# para que no dependa de nada instalado. Se salta los que no tienen compilador:
#   Pi:      sudo apt install g++-aarch64-linux-gnu
#   Windows: sudo apt install mingw-w64
# Salida: $SALIDA/<linux|pi|windows>/d2port-importar[.exe]
# (por defecto ~/build-importador/bin). CASCLIB: la carpeta de CascLib.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
casc="${CASCLIB:-$HOME/src/CascLib}"
work="$HOME/build-importador"
out="${SALIDA:-$work/bin}"
[ -f "$casc/src/CascLib.h" ] || { echo "falta CascLib en $casc"; exit 1; }

build() { # destino prefijo-del-compilador sistema-cmake
    local name=$1 pre=$2 sys=$3 exe=d2port-importar
    if ! command -v "${pre}g++" >/dev/null; then
        echo "== $name: no está ${pre}g++, me lo salto"
        return
    fi
    [ "$name" = windows ] && exe=d2port-importar.exe
    local lib="$work/casc-$name"
    local cross=()
    [ -n "$sys" ] && cross=(-DCMAKE_SYSTEM_NAME="$sys" -DCMAKE_SYSTEM_PROCESSOR="${pre%%-*}")
    cmake -S "$casc" -B "$lib" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="${pre}gcc" -DCMAKE_CXX_COMPILER="${pre}g++" "${cross[@]}" \
        -DCASC_BUILD_SHARED_LIB=OFF -DCASC_BUILD_STATIC_LIB=ON -DCASC_BUILD_TESTS=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON >/dev/null
    cmake --build "$lib" -j8 >/dev/null
    mkdir -p "$out/$name"
    local defs=(-DCASCLIB_NO_AUTO_LINK_LIBRARY -DCASCLIB_NODEBUG)
    [ "$name" = windows ] && defs+=(-DCASCLIB_DETECT_UNICODE_MISMATCHES)
    "${pre}gcc" -O2 -Wall -Wextra "${defs[@]}" -I"$casc/src" -c "$root/tools/d2port-importar.c" \
        -o "$lib/d2port-importar.o"
    local libs=()
    [ "$name" = windows ] && libs+=(-lws2_32) # (los sockets de CascLib, que el importador no usa)
    "${pre}g++" -static -s "$lib/d2port-importar.o" "$lib/libcasc.a" -o "$out/$name/$exe" -pthread "${libs[@]}"
    echo "== $name: $out/$name/$exe"
}

build linux "" ""
build pi aarch64-linux-gnu- Linux
build windows x86_64-w64-mingw32- Windows
