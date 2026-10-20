#!/bin/bash
# bench.sh: cuánto tarda en dibujarse un cuadro en varias escenas del Acto 1
# (sin ventana: mide el dibujo, no la subida a la GPU), con el desglose por
# etapa y, en la Pi, la frecuencia del procesador mientras corre cada escena.
# Se corre desde la carpeta del juego; en la Pi:
#   cd /userdata/roms/ports/.data/d2port && ./bench.sh
# Las escenas salen de la semilla 7 (las coordenadas son de ese mapa).
cd "$(dirname "$(readlink -f "$0")")" || exit 1
bin=./d2port
[ -x "$bin" ] || bin=./build/d2port
cpu=/sys/devices/system/cpu/cpu0/cpufreq

run() { # nombre jugadores [variables...] -- [opciones...]
    local name=$1 players=$2
    shift 2
    local vars=() opts=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do vars+=("$1"); shift; done
    [ "$1" = "--" ] && shift
    opts=("$@")
    local tmp
    tmp=$(mktemp)
    env "${vars[@]}" D2_SEED=7 D2_GOD=1 D2_NOSOUND=1 D2_BENCH_PARTS=1 "$bin" --headless --players "$players" \
        --frames 60 --bench 300 "${opts[@]}" > "$tmp" 2>/dev/null &
    local pid=$! lo=0 hi=0 f
    while kill -0 "$pid" 2>/dev/null; do # la frecuencia mientras corre
        f=0
        if command -v vcgencmd > /dev/null; then # la real (el firmware la baja si falta voltaje)
            f=$(vcgencmd measure_clock arm | sed 's/.*=//')
            f=$((f / 1000000))
        elif [ -r "$cpu/scaling_cur_freq" ]; then
            f=$(($(cat "$cpu/scaling_cur_freq") / 1000))
        fi
        if [ "$f" -gt 0 ]; then
            [ "$lo" -eq 0 ] || [ "$f" -lt "$lo" ] && lo=$f
            [ "$f" -gt "$hi" ] && hi=$f
        fi
        sleep 1
    done
    local out parts
    out=$(grep "dibujo:" "$tmp")
    parts=$(grep "partes:" "$tmp")
    rm -f "$tmp"
    printf "%-36s %s\n" "$name" "${out#dibujo: }"
    printf "    %s" "${parts#partes: }"
    [ "$hi" -gt 0 ] && printf " (CPU %s-%s MHz)" "$lo" "$hi"
    echo
}

echo "== $(uname -m), $(date '+%Y-%m-%d %H:%M')"
"$bin" --headless --players 1 --frames 1 2>/dev/null | grep "^NEON"
if [ -r "$cpu/scaling_governor" ]; then
    echo "CPU: $(cat $cpu/scaling_governor), $(($(cat $cpu/scaling_min_freq) / 1000))-$(($(cat $cpu/scaling_max_freq) / 1000)) MHz"
fi
run "Campamento, 1 jugador" 1
run "Campamento, 2 jugadores" 2
run "Blood Moor (campo), 2 jugadores" 2 D2_START=398,862
run "Pantalla partida (campo)" 2 D2_START=398,862 -- --demo 0 --frames 150
run "Guarida del Mal (oscura), 2 jug." 2 D2_START=344,1686
run "Catacumbas 4 (oscuras), 2 jug." 2 D2_START=850,2006
# La oscuridad aplicada aparte, no al convertir el cuadro (para comparar)
run "Guarida, oscuridad inmediata" 2 D2_START=344,1686 D2_DARK_DEFER=0
run "Catacumbas, oscuridad inmediata" 2 D2_START=850,2006 D2_DARK_DEFER=0
[ -r /sys/class/thermal/thermal_zone0/temp ] && echo "Temperatura: $(($(cat /sys/class/thermal/thermal_zone0/temp) / 1000)) °C"
command -v vcgencmd > /dev/null && echo "vcgencmd: $(vcgencmd get_throttled) $(vcgencmd measure_clock arm)"
exit 0
