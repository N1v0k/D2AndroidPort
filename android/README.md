# d2port for Android (arm64)

An unofficial Android build of d2port: the same C99 + SDL2 engine, packaged as an APK.

## Playing

1. Install the APK (allow installing from unknown sources).
2. Have your own Diablo II: Resurrected folder (Battle.net: the one with `.build.info` and `Data`; Steam: the one
   with `data/.build.config`) somewhere in the
   phone's storage, e.g. `Download/Diablo II Resurrected` (installed by Battle.net on a PC, or with a PC game
   launcher on the phone: copy or move it out of that app's private folder first, since Android does not let
   one app read another app's folder).
3. Open the app: the import screen asks for "All files access", finds the folder (or lets you choose it),
   and copies the files the port uses (about 1.6 GB, in the language you choose) to `d2port/` in the phone's
   storage, with the importer (`tools/d2port-importar.c`, built as `libd2import.so`). No cinematics: those
   need ffmpeg. Saves and `d2port.log` go to `d2port/` too, so they stay if the app is uninstalled.
   (Without that permission the game also looks in its own folder, `Android/data/com.d2port.android/files`,
   e.g. for a `data` folder made by the PC importer and copied over USB.)

Controls: a Bluetooth or USB gamepad works like on the PC. Without one, on-screen controls laid out like
D2Android's: a floating stick in the left half, the attack button (A) at the bottom right with B, X and Y on an
arc around it (each showing its skill's icon), the healing and mana potions at the right edge, a menu row at the
top right and a small d-pad at the top left. Touching the picture elsewhere walks there (like holding the left
mouse button). They hide when a real gamepad is used and come back when the screen is touched; while a panel
(inventory, shop, ...) is open, the round buttons are fainter. The Back gesture/button is Esc. The character
runs from the start, as with R in the original ("CORRER SIEMPRE" in the pause menu, or RUN in the touch menu row;
with it on, the right trigger walks).

Widescreen (on by default on Android; "PANTALLA ANCHA" / "BREITBILD" in the pause menu, or `D2_WIDE=1` on the
PC): the game picture is as wide as the screen (still 600 high: you see more at the sides, e.g. 1328 x 600 on a
2688 x 1216 phone), up to 1600 wide. The control bar, the panels, dialogs and tooltips stay in the 800 in the
middle, where they are in 800 x 600 (beside the bar the world shows down to the bottom); the menus, the controls help and the Horadric Cube guide stay 800 x 600.
What the game simulates does not change: monsters "see" a player within the original 800 x 600, so local
network games between different screens stay in step.

Speeds: as in the original game. The original moves a path by velocity / 4096 subtiles per frame (25 frames a
second), so characters and monsters go 25/16 subtiles a second per point of Velocity (walking 9.4, running 14.1)
and missiles 75/64 per point of Vel; upstream d2port used 5/6 for all of them (about half as fast) and sped the
characters up by 10-20 %. `UNIT_VEL_PER_SEC` and `MISSILE_VEL_PER_SEC` in `src/missiles.h`.

## Building (no Google SDK/NDK download needed)

`build.sh` (see its header for the packages and sources): a bionic sysroot generated from AOSP sources
(`toolchain/`), host clang/lld, SDL 2.32 (`build_sdl.sh`), the game (`build_game.sh`), the importer with
CascLib (`build_importer.sh`) and the APK with
aapt, dx, zipalign and apksigner (`build_apk.sh`). Android-specific code: `src/touch.c` (touch controls), `android/java` (with the import screen),
the `__ANDROID__` parts of `src/compat.c`, `src/main.c` and `tools/d2port-importar.c`. Widescreen: `SCREEN_W` is
the width of the frame being drawn (`video_set_width` in `src/video.c`; `SCREEN_W_BASE` 800, `SCREEN_W_MAX` 1600),
`HUD_OX`, `UI_X0` and `UI_X1` in `src/game.c` place the bar and the panels.

Signing: `android/keystore/d2port.p12` (password and alias `d2port`, see `build_apk.sh`). Keep it: Android only
installs an update over the app if it is signed with the same key.
