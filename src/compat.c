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

FILE *compat_tmpfile(void)
{
    return tmpfile();
}

void compat_start(void)
{
}
#endif
