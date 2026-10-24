#!/bin/bash
# Arma publicar/: lo que se puede subir a GitHub, sin ningún archivo de Blizzard.
#
#   publicar/diablo2-port/      el port compilado para Batocera (la Pi) y el
#                               importador para Linux: cada uno saca de su
#                               propio Diablo II: Resurrected los archivos que
#                               usa el port (los de archivos.txt, no el juego
#                               entero)
#   publicar/diablo2-port-windows/  el port para Windows (d2port.exe, SDL2.dll),
#                               su importador y dos .bat para importar
#   publicar/*.zip              lo mismo comprimido, para las descargas
#
# La skin personal del Brujo (sus dibujos editados, .d2rw) no se publica.
#
# Antes: compilar build-pi (ver package-batocera.sh), build-win (Windows:
# cmake/mingw-w64.cmake; SDL2_MINGW la carpeta de SDL2 para MinGW) y los
# importadores (scripts/build-importador.sh). Los que falten se avisan y se saltan.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
pub="$root/publicar"
port="$pub/diablo2-port"
imp="${IMPORTADORES:-$HOME/build-importador/bin}"

[ -d "$pub" ] && rm -rf "$pub"
mkdir -p "$port"

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

# Windows: el juego, SDL2.dll, el importador, la lista y dos .bat para importar (en una sola carpeta:
# el importador copia a su data/ y el juego guarda en su save/)
win="$pub/diablo2-port-windows"
sdl="${SDL2_MINGW:-$HOME/sdl2-mingw/SDL2-2.30.12/x86_64-w64-mingw32}"
if [ -f "$root/build-win/d2port.exe" ] && [ -f "$sdl/bin/SDL2.dll" ] && [ -f "$imp/windows/d2port-importar.exe" ]; then
    mkdir -p "$win"
    cp "$root/build-win/d2port.exe" "$sdl/bin/SDL2.dll" "$imp/windows/d2port-importar.exe" "$win/"
    cp "$game/archivos.txt" "$win/"
    for lang in es en; do
        if [ $lang = es ]; then name="Importar Diablo II (Español).bat" code=esMX; else name="Import Diablo II (English).bat" code=enUS; fi
        # (con la ruta entera: con NoDefaultCurrentDirectoryInExePath, cmd no busca en la carpeta actual)
        printf '@echo off\r\nrem Copia los archivos del juego desde tu Diablo II: Resurrected a esta carpeta\r\ncd /d "%%~dp0"\r\n"%%~dp0d2port-importar.exe" --idioma %s\r\n' \
            "$code" > "$win/$name"
    done
    cp "$root/README.md" "$root/LICENSE" "$root/THIRD-PARTY-NOTICES.md" "$win/"
else
    missing+=(windows-juego)
fi

# Que la Pi y Windows sean de la misma versión del código (la red local junta máquinas de la misma:
# compilar después del último commit, volviendo a configurar: cmake -S . -B build-pi / build-win)
ver=$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo dev)
for b in "$game/d2port" "$pub/diablo2-port-windows/d2port.exe"; do
    if [ -f "$b" ] && ! grep -q "D2PN2 $ver" "$b"; then
        echo "ERROR: $b no es de la versión $ver: volvé a configurar y compilar (cmake -S . -B build-pi o build-win)"
        exit 1
    fi
done

# Que no se cuele nada del juego ni dibujos editados
leaks=$(cd "$pub" && find . -type f \( -path '*/data/*' -o -name '*.d2rw' \) | head -5)
if [ -n "$leaks" ]; then
    echo "ERROR: hay archivos del juego en publicar/:"
    echo "$leaks"
    exit 1
fi

# Los .zip (los permisos de ejecución quedan en el zip)
( cd "$pub" && python3 - <<'EOF'
import os, zipfile
for name in [n for n in ('diablo2-port', 'diablo2-port-windows') if os.path.isdir(n)]:
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
du -sh "$port" "$pub"/*.zip
[ -d "$win" ] && du -sh "$win"
[ ${#missing[@]} -eq 0 ] || echo "Faltan importadores (compilar con scripts/build-importador.sh): ${missing[*]}"
echo "Listo: $pub"
