#!/bin/bash
# cmpshots.sh VIEJO NUEVO: capturas de las mismas escenas con los dos binarios
# y si dan igual byte por byte. Corre en D2RUN (~/d2run por omisión), que
# tiene data/; los binarios, con ruta desde ahí. La pausa cambia sola.
cd "${D2RUN:-$HOME/d2run}" || exit 1
old=$1
new=$2
mkdir -p shots
fail=0
scene() { # nombre jugadores [variables...]
    local name=$1 players=$2
    shift 2
    for b in old new; do
        local bin=$old
        [ $b = new ] && bin=$new
        env "$@" D2_SEED=7 D2_GOD=1 D2_NOSOUND=1 D2_SAVE_DIR=sv-shot-$b-$$ "$bin" --headless --players "$players" \
            --frames 60 --screenshot shots/$name-$b.bmp > /dev/null 2>&1
        rm -rf sv-shot-$b-$$
    done
    if cmp -s shots/$name-old.bmp shots/$name-new.bmp; then
        echo "igual    $name"
    else
        echo "DISTINTA $name"
        fail=1
    fi
}
scene campamento 1
scene campamento2 2
scene bloodmoor 2 D2_START=398,862
scene partida 2 D2_START=398,862 D2_ACT=1
scene guarida 2 D2_START=344,1686
scene catacumbas 2 D2_START=850,2006
scene lut 2 D2_ACT=2
scene cloacas 2 D2_ACT=2 D2_START=1034,1645
scene halls 2 D2_ACT=2 D2_START=1245,1958
scene rocky 2 D2_ACT=2 D2_START=1003,527
scene santuario 1 D2_ACT=2 D2_START=495,3600
scene duriel 1 D2_ACT=2 D2_START=1038,3540
scene pausa 2 D2_PAUSE=1
scene inventario 1 D2_INV_OPEN=1
scene mapa 1 D2_MAP_OPEN=1
scene posiciones 2 D2_SHOW_POS=1 D2_ACT=2 D2_START=1034,1645
scene posiciones1 2 D2_SHOW_POS=1
exit $fail
