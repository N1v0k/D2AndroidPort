#include "compat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>

int compat_mkdir(const char *path)
{
    return _mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
}

/* (el rename de Windows no pisa un archivo que ya está) */
int compat_replace(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}

/* (el tmpfile de Windows lo crea en la raíz del disco, donde a veces no se puede escribir) */
FILE *compat_tmpfile(void)
{
    char dir[MAX_PATH], name[MAX_PATH];
    if (!GetTempPathA(sizeof dir, dir) || !GetTempFileNameA(dir, "d2p", 0, name))
        return NULL;
    return fopen(name, "w+bTD"); /* T: de corta vida, D: se borra al cerrarlo */
}

void compat_start(void)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
    char *slash = n > 0 && n < sizeof exe ? strrchr(exe, '\\') : NULL;
    if (slash) {
        *slash = '\0';
        SetCurrentDirectoryA(exe);
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == NULL || out == INVALID_HANDLE_VALUE || GetFileType(out) == FILE_TYPE_UNKNOWN) {
        if (freopen("d2port.log", "w", stdout)) {
            setvbuf(stdout, NULL, _IONBF, 0);
            _dup2(_fileno(stdout), 2);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
    }
}

int compat_restart(void)
{
    return -1;
}

int compat_check_data(void)
{
    return 0;
}

#else
#include <sys/stat.h>

int compat_mkdir(const char *path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

int compat_replace(const char *from, const char *to)
{
    return rename(from, to);
}

#ifndef __ANDROID__
FILE *compat_tmpfile(void)
{
    return tmpfile();
}

void compat_start(void)
{
}

int compat_restart(void)
{
    return -1;
}

int compat_check_data(void)
{
    return 0;
}

#else /* Android: el juego vive en la carpeta de la app en el almacenamiento (Android/data/<paquete>/files) */
#include <SDL.h>
#include <jni.h>
#include <stdlib.h>
#include <unistd.h>

static char s_dir[512];
static SDL_LogOutputFunction s_log_prev;
static void *s_log_prev_data;

/* SDL_Log va al logcat y también a d2port.log (como en la Pi) */
static void log_both(void *data, int category, SDL_LogPriority priority, const char *message)
{
    (void)data;
    fprintf(stderr, "%s\n", message);
    if (s_log_prev)
        s_log_prev(s_log_prev_data, category, priority, message);
}

/* (tmpfile quiere escribir en /tmp, que en Android no hay: en la carpeta del juego) */
FILE *compat_tmpfile(void)
{
    char name[600];
    snprintf(name, sizeof name, "%s/d2tmpXXXXXX", s_dir[0] ? s_dir : ".");
    int fd = mkstemp(name);
    if (fd < 0)
        return NULL;
    unlink(name);
    return fdopen(fd, "w+b");
}

/* Dónde busca los archivos del juego: una carpeta "d2port" en el almacenamiento del teléfono (fácil de llenar
   por USB, y las partidas quedan aunque se desinstale la app; hace falta el permiso "acceso a todos los archivos")
   o la carpeta de la app (Android/data/<paquete>/files, sin permisos) */
static const char *const k_public[] = { "/storage/emulated/0/d2port", "/sdcard/d2port" };

static int has_data(const char *dir)
{
    char path[700];
    snprintf(path, sizeof path, "%s/data/global/palette/act1/pal.dat", dir);
    return access(path, R_OK) == 0;
}

void compat_start(void)
{
    const char *app = SDL_AndroidGetExternalStoragePath();
    if (!app)
        app = SDL_AndroidGetInternalStoragePath();
    const char *dir = app;
    for (size_t i = 0; i < sizeof k_public / sizeof k_public[0]; i++)
        if (has_data(k_public[i]) && access(k_public[i], W_OK) == 0) {
            dir = k_public[i];
            break;
        }
    if (dir) {
        snprintf(s_dir, sizeof s_dir, "%s", dir);
        mkdir(s_dir, 0755);
        if (chdir(s_dir) != 0)
            SDL_Log("no pude entrar en %s", s_dir);
    }
    if (freopen("d2port.log", "w", stdout)) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        dup2(fileno(stdout), 2);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    SDL_LogGetOutputFunction(&s_log_prev, &s_log_prev_data);
    SDL_LogSetOutputFunction(log_both, NULL);
    printf("d2port para Android: la carpeta del juego es %s\n", s_dir[0] ? s_dir : "?");
}

/* Un método sin argumentos de la actividad (D2Activity.java); 0 si estaba */
static int call_activity(const char *name)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act)
        return -1;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = cls ? (*env)->GetMethodID(env, cls, name, "()V") : NULL;
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (m)
        (*env)->CallVoidMethod(env, act, m);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (cls)
        (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return m ? 0 : -1;
}

/* Otro idioma: la actividad arranca la app de nuevo (en un proceso nuevo, con todo desde cero) */
int compat_restart(void)
{
    return call_activity("restartApp");
}

/* Sin los archivos del juego (la pantalla de importar de la app no se completó): un cartel y a salir */
int compat_check_data(void)
{
    if (access("data/global/palette/act1/pal.dat", R_OK) == 0)
        return 0;
    const char *msg =
        "Diablo II game files not found.\n\n"
        "Open the app again: its import screen copies them from your own Diablo II: Resurrected "
        "into the folder 'd2port' of the phone's storage.\n\n"
        "---\n"
        "Spieldateien nicht gefunden.\n\n"
        "Öffne die App noch einmal: im Import-Bildschirm werden sie aus deinem Diablo II: Resurrected "
        "in den Ordner 'd2port' im internen Speicher kopiert.";
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Diablo II (port)", msg, NULL);
    return -1;
}
#endif
#endif
