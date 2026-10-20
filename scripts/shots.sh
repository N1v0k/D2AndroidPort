#!/bin/bash
# Compila y genera capturas de prueba sin ventana en build/shots/.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build >/dev/null
cmake --build build -j 2>&1 | grep -Ei 'warning|error' || true
mkdir -p build/shots
./build/d2port --headless --screenshot build/shots/menu.bmp
./build/d2port --headless --players 2 --demo 20 --frames 8 --screenshot build/shots/juntos.bmp
./build/d2port --headless --players 2 --demo 20 --frames 45 --screenshot build/shots/separados.bmp
./build/d2port --headless --players 2 --demo 75 --frames 45 --screenshot build/shots/separados_vertical.bmp
