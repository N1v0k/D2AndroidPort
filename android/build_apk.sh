#!/bin/bash
# Packages the APK: SDL's Java (without HIDAPI) + D2Activity, the manifest and icon, libSDL2.so and libmain.so,
# then zipalign and apksigner (v2/v3). Tools: javac (JDK), dalvik-exchange (dx), aapt, zipalign, apksigner
# (Ubuntu packages) and an android.jar of API 34 to compile against.
# usage: build_apk.sh <SDL source> <SDL build dir> <game build dir> <android.jar> <out.apk> <importer build dir>
#   KEYSTORE (default android/keystore/d2port.p12, made the first time), KS_PASS (default d2port)
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
SDL=$(realpath "$1"); SDLB=$(realpath "$2"); GAMEB=$(realpath "$3"); JAR=$(realpath "$4"); OUT=$(realpath -m "$5"); IMPB=$(realpath "$6")
KEYSTORE=${KEYSTORE:-$HERE/keystore/d2port.p12}; KS_PASS=${KS_PASS:-d2port}
W=$(dirname "$OUT")/apk-work
rm -rf "$W"; mkdir -p "$W/java" "$W/classes" "$W/gen" "$W/pkg/lib/arm64-v8a"

# Java: SDL's activity (patched: no HIDAPI, no lambdas) and ours
cp -r "$SDL/android-project/app/src/main/java/org" "$W/java/"
python3 -I "$HERE/tools/patch_sdl_java.py" "$W/java/org/libsdl/app"
cp -r "$HERE/java/com" "$W/java/"

SRC=$(dirname "$HERE")
VER=$(git -C "$SRC" rev-parse --short=12 HEAD 2>/dev/null || echo dev)
sed "s/@VERSION@/1.7-$VER/" "$HERE/AndroidManifest.xml" > "$W/AndroidManifest.xml"

aapt package -f -m -J "$W/gen" -M "$W/AndroidManifest.xml" -S "$HERE/res" -I "$JAR"
javac -source 8 -target 8 -encoding UTF-8 -nowarn -Xlint:-options -bootclasspath "$JAR" -d "$W/classes" \
    $(find "$W/java" "$W/gen" -name '*.java') 2>&1 | grep -v "^Picked up JAVA_TOOL_OPTIONS" || true
[ -f "$W/classes/com/d2port/android/D2Activity.class" ]
dalvik-exchange --dex --min-sdk-version=26 --output="$W/pkg/classes.dex" "$W/classes" 2>&1 | grep -v "^Picked up" || true
[ -s "$W/pkg/classes.dex" ]

# the list of the files the port uses (the import screen gives it to the importer)
mkdir -p "$W/assets"
cp "$SRC/tools/archivos-del-juego.txt" "$W/assets/archivos.txt"
aapt package -f -M "$W/AndroidManifest.xml" -S "$HERE/res" -A "$W/assets" -I "$JAR" -F "$W/unsigned.apk"
llvm-strip --strip-unneeded -o "$W/pkg/lib/arm64-v8a/libSDL2.so" "$SDLB/libSDL2.so"
llvm-strip --strip-unneeded -o "$W/pkg/lib/arm64-v8a/libmain.so" "$GAMEB/libmain.so"
# the importer is an executable; named lib*.so so Android installs it with the libraries (where it may run)
llvm-strip --strip-unneeded -o "$W/pkg/lib/arm64-v8a/libd2import.so" "$IMPB/libd2import.so"
(cd "$W/pkg" && zip -q -r "$W/unsigned.apk" classes.dex lib)
zipalign -f -p 4 "$W/unsigned.apk" "$W/aligned.apk"

if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -keystore "$KEYSTORE" -storetype PKCS12 -storepass "$KS_PASS" -keypass "$KS_PASS" \
        -alias d2port -keyalg RSA -keysize 3072 -validity 20000 -dname "CN=d2port Android build" 2>&1 | grep -v "^Picked up" || true
fi
apksigner sign --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true --ks "$KEYSTORE" --ks-pass "pass:$KS_PASS" --ks-key-alias d2port --out "$OUT" "$W/aligned.apk" 2>&1 | grep -v "^Picked up" || true
apksigner verify --verbose "$OUT" 2>&1 | grep -E "Verifie|DOES NOT|ERROR" || true
ls -la "$OUT"
