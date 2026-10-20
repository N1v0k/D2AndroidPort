#!/bin/bash
# Arma publicar/: lo que se puede subir a GitHub, sin ningún archivo de Blizzard.
#
#   publicar/diablo2-port/      el port compilado para Batocera (la Pi) y el
#                               importador para Linux: cada uno saca de su
#                               propio Diablo II: Resurrected los archivos que
#                               usa el port (los de archivos.txt, no el juego
#                               entero). Windows: más adelante
#   publicar/parche-brujo/      la skin personal del Brujo, aparte
#   publicar/*.zip              lo mismo comprimido, para las descargas
#
# Antes: compilar build-pi (ver package-batocera.sh) y los importadores
# (scripts/build-importador.sh). Los que falten se avisan y se saltan.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
pub="$root/publicar"
port="$pub/diablo2-port"
patch="$pub/parche-brujo"
imp="${IMPORTADORES:-$HOME/build-importador/bin}"

[ -d "$pub" ] && rm -rf "$pub"
mkdir -p "$port" "$patch"

# El paquete de Batocera sin los archivos del juego (con la lista que usa el importador)
OUT="$port/batocera/ports" SIN_DATOS=1 bash "$root/scripts/package-batocera.sh" >/dev/null
game="$port/batocera/ports/.data/d2port"

# Los importadores: el de Linux para la PC; el de la Pi va adentro del
# paquete, con dos entradas en el menú de ports para importar desde la Pi
# (el de Windows, cuando esté probado)
missing=()
copy_imp() { # sistema destino
    local src="$imp/$1/d2port-importar"
    [ "$1" = windows ] && src="$src.exe"
    if [ -f "$src" ]; then
        mkdir -p "$2"
        cp "$src" "$2/"
    else
        missing+=("$1")
    fi
}
copy_imp linux "$port/importador/linux"
copy_imp pi "$game"
if [ -f "$game/d2port-importar" ]; then
    for lang in es en; do
        if [ $lang = es ]; then name="Importar Diablo II (Español).sh"; else name="Import Diablo II (English).sh"; fi
        cat > "$port/batocera/ports/$name" <<EOF
#!/bin/bash
# Copia los archivos del juego desde Diablo II: Resurrected (una carpeta del
# juego en un USB, en /userdata o en roms/ports) al port
DIR="\$(dirname "\$(readlink -f "\$0")")"
"\$DIR/.data/d2port/d2port-importar" --idioma $lang --sin-pausa > /userdata/system/logs/d2port-importar.log 2>&1
EOF
        chmod +x "$port/batocera/ports/$name"
    done
fi
cp "$root/README.md" "$root/LICENSE" "$root/THIRD-PARTY-NOTICES.md" "$port/" # las instrucciones (en inglés) y las licencias

# El parche del Brujo: sus dibujos editados (.d2rw), con las carpetas. Los
# íconos de habilidades (waskillicon.d2rw) no van: salen de los del
# Resurrected (tools/hd_icons.py) y sin ellos el port arma los suyos
( cd "$root" && find data/global/chars/wk -name '*.d2rw' | sort ) > "$patch/archivos.txt"
tar -C "$root" -cf - -T "$patch/archivos.txt" | tar -C "$patch" -xf -
rm "$patch/archivos.txt"
cp "$root/docs/publicar/README-warlock.md" "$patch/README.md"
cp "$root/docs/publicar/instalar-brujo.sh" "$patch/instalar.sh"
chmod +x "$patch/instalar.sh"

# Que no se cuele nada del juego (solo los .d2rw del parche, que son dibujos)
leaks=$(cd "$pub" && find . -type f \( -path '*/data/*' ! -name '*.d2rw' \) | head -5)
if [ -n "$leaks" ]; then
    echo "ERROR: hay archivos del juego en publicar/:"
    echo "$leaks"
    exit 1
fi

# Los .zip (los permisos de ejecución quedan en el zip)
( cd "$pub" && python3 - <<'EOF'
import os, zipfile
for name in ('diablo2-port', 'parche-brujo'):
    with zipfile.ZipFile(name + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for base, dirs, files in os.walk(name):
            dirs.sort()
            for f in sorted(files):
                path = os.path.join(base, f)
                info = zipfile.ZipInfo.from_file(path)
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, 'rb') as fh:
                    z.writestr(info, fh.read())
EOF
)
du -sh "$port" "$patch" "$pub"/*.zip
[ ${#missing[@]} -eq 0 ] || echo "Faltan importadores (compilar con scripts/build-importador.sh): ${missing[*]}"
echo "Listo: $pub"
