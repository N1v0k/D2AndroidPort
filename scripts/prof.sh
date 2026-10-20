#!/bin/bash
# prof.sh: perfila el dibujo en la Pi con el binario d2port-prof (compilado
# con -pg): tres escenas, un gmon-<escena>.out por cada una para analizarlo
# en la PC con aarch64-linux-gnu-gprof. Se corre desde la carpeta del juego:
#   cd /userdata/roms/ports/.data/d2port && ./prof.sh
cd "$(dirname "$(readlink -f "$0")")" || exit 1
run() { # escena [variables...]
    local name=$1
    shift
    rm -f gmon.out
    env "$@" D2_SEED=7 D2_GOD=1 D2_NOSOUND=1 ./d2port-prof --headless --players 2 --frames 60 --bench 1500 \
        2>/dev/null | grep "dibujo:" | sed "s/^/$name: /"
    mv -f gmon.out "gmon-$name.out"
}
run campamento
run campo D2_START=398,862
run catacumbas D2_START=850,2006
ls -la gmon-*.out
