cmake_minimum_required(VERSION 3.16)
project(d2port C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)

# En la Pi (Cortex-A53, que ejecuta en orden) la vectorización SLP de GCC
# arma los colores de la conversión del cuadro con NEON pasando por la pila:
# sin ella la conversión tarda 2,6 ms en vez de 3,6 y el resto no cambia
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
  add_compile_options(-fno-tree-slp-vectorize)
endif()

# Lectores de formatos de Diablo II, compartidos por el motor y las herramientas
add_library(d2fmtlib STATIC
  src/fmt/fs.c
  src/fmt/sprite.c
  src/fmt/animdata.c
  src/fmt/txt.c
  src/fmt/dcc.c
  src/fmt/cof.c
  src/fmt/dt1.c
  src/fmt/ds1.c
  src/fmt/flac.c
  src/fmt/ttf.c
)
target_compile_options(d2fmtlib PRIVATE -Wall -Wextra)

add_executable(d2port
  src/main.c
  src/video.c
  src/input.c
  src/font.c
  src/ui.c
  src/charanim.c
  src/map.c
  src/world.c
  src/jobs.c
  src/bot.c
  src/strings.c
  src/lang.c
  src/items.c
  src/itemstats.c
  src/cube.c
  src/skills.c
  src/sequences.c
  src/missiles.c
  src/overlays.c
  src/audio.c
  src/cinema.c
  src/units.c
  src/menu.c
  src/controls.c
  src/game.c
  src/net.c
)
target_link_libraries(d2port PRIVATE d2fmtlib SDL2::SDL2 m)
target_compile_options(d2port PRIVATE -Wall -Wextra)

add_executable(d2fmt tools/d2fmt.c)
target_link_libraries(d2fmt PRIVATE d2fmtlib)
target_compile_options(d2fmt PRIVATE -Wall -Wextra)

# Extractor del CASC del Resurrected (solo en la PC, necesita CascLib compilada)
set(CASCLIB_DIR "$ENV{HOME}/src/CascLib" CACHE PATH "Carpeta de CascLib compilada")
if(EXISTS "${CASCLIB_DIR}/build/libcasc.a" AND NOT CMAKE_CROSSCOMPILING)
  find_package(ZLIB REQUIRED)
  add_executable(d2rextract tools/d2rextract.c)
  target_include_directories(d2rextract PRIVATE "${CASCLIB_DIR}/src")
  target_link_libraries(d2rextract PRIVATE "${CASCLIB_DIR}/build/libcasc.a" ZLIB::ZLIB stdc++ pthread)
  # El importador: copia de tu Diablo II: Resurrected los archivos que usa el
  # port (tools/archivos-del-juego.txt) a data/ (./build/d2port-importar desde acá)
  add_executable(d2port-importar tools/d2port-importar.c)
  target_include_directories(d2port-importar PRIVATE "${CASCLIB_DIR}/src")
  target_compile_definitions(d2port-importar PRIVATE CASCLIB_NO_AUTO_LINK_LIBRARY CASCLIB_NODEBUG)
  target_compile_options(d2port-importar PRIVATE -Wall -Wextra)
  target_link_libraries(d2port-importar PRIVATE "${CASCLIB_DIR}/build/libcasc.a" ZLIB::ZLIB stdc++ pthread)
endif()
