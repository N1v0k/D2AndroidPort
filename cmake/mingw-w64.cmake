# Compilación cruzada para Windows de 64 bits desde WSL/Linux (MinGW-w64):
#   sudo apt install mingw-w64
#   SDL2 para MinGW: SDL2-devel-2.30.x-mingw.tar.gz de github.com/libsdl-org/SDL (releases)
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
#         -DSDL2_DIR=$HOME/sdl2-mingw/SDL2-2.30.12/x86_64-w64-mingw32/lib/cmake/SDL2
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
