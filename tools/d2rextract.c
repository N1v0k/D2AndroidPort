/*
 * d2rextract: lista o extrae archivos del almacenamiento CASC de
 * Diablo II: Resurrected usando CascLib.
 *
 *   d2rextract --list <carpeta D2R> <salida.txt>
 *   d2rextract <carpeta D2R> <carpeta destino> <prefijo> [prefijo...]
 *
 * Los nombres se guardan en minúsculas y sin el prefijo "data:" del CASC,
 * así el motor puede buscarlos sin preocuparse por mayúsculas.
 */
#include <CascLib.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void normalize(const char *in, char *out, size_t cap)
{
    const char *colon = strchr(in, ':');
    if (colon)
        in = colon + 1;
    size_t i = 0;
    for (; *in && i + 1 < cap; in++, i++)
        out[i] = *in == '\\' ? '/' : (char)tolower((unsigned char)*in);
    out[i] = '\0';
}

static int mkdirs_for(const char *path)
{
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return 0;
}

static int matches(const char *name, char **prefixes, int count)
{
    for (int i = 0; i < count; i++)
        if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0)
            return 1;
    return 0;
}

static int extract_one(HANDLE storage, const char *casc_name, const char *out_path)
{
    HANDLE file;
    if (!CascOpenFile(storage, casc_name, 0, CASC_OPEN_BY_NAME, &file))
        return -1;

    ULONGLONG size = 0;
    if (!CascGetFileSize64(file, &size) || size > 0x7FFFFFFF) {
        CascCloseFile(file);
        return -1;
    }
    unsigned char *buf = malloc(size ? (size_t)size : 1);
    DWORD got = 0;
    int ok = buf && CascReadFile(file, buf, (DWORD)size, &got) && got == size;
    CascCloseFile(file);

    if (ok && mkdirs_for(out_path) == 0) {
        FILE *f = fopen(out_path, "wb");
        ok = f && fwrite(buf, 1, (size_t)size, f) == size;
        if (f)
            fclose(f);
    } else {
        ok = 0;
    }
    free(buf);
    return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    int list_mode = argc == 4 && strcmp(argv[1], "--list") == 0;
    if (!list_mode && argc < 4) {
        fprintf(stderr,
                "uso: %s --list <carpeta D2R> <salida.txt>\n"
                "     %s <carpeta D2R> <carpeta destino> <prefijo> [prefijo...]\n",
                argv[0], argv[0]);
        return 2;
    }
    const char *game_dir = list_mode ? argv[2] : argv[1];

    HANDLE storage;
    if (!CascOpenStorage(game_dir, CASC_LOCALE_ALL, &storage)) {
        fprintf(stderr, "no pude abrir el CASC en %s (error %d)\n", game_dir, GetCascError());
        return 1;
    }

    FILE *list_out = NULL;
    if (list_mode && !(list_out = fopen(argv[3], "w"))) {
        fprintf(stderr, "no pude crear %s\n", argv[3]);
        return 1;
    }

    CASC_FIND_DATA fd;
    HANDLE find = CascFindFirstFile(storage, "*", &fd, NULL);
    if (find == INVALID_HANDLE_VALUE || find == NULL) {
        fprintf(stderr, "no se pudieron enumerar archivos (error %d)\n", GetCascError());
        return 1;
    }

    long total = 0, done = 0, missing = 0, failed = 0;
    char name[1024], out_path[2048];
    do {
        normalize(fd.szFileName, name, sizeof name);
        total++;
        if (list_mode) {
            fprintf(list_out, "%s\t%llu\t%s\n", name, (unsigned long long)fd.FileSize,
                    fd.bFileAvailable ? "local" : "falta");
            continue;
        }
        if (!matches(name, argv + 3, argc - 3))
            continue;
        if (!fd.bFileAvailable) {
            missing++;
            continue;
        }
        snprintf(out_path, sizeof out_path, "%s/%s", argv[2], name);
        if (extract_one(storage, fd.szFileName, out_path) == 0) {
            if (++done % 500 == 0)
                printf("%ld archivos...\n", done);
        } else {
            failed++;
            fprintf(stderr, "falló: %s\n", fd.szFileName);
        }
    } while (CascFindNextFile(find, &fd));

    CascFindClose(find);
    CascCloseStorage(storage);
    if (list_out) {
        fclose(list_out);
        printf("%ld archivos listados en %s\n", total, argv[3]);
    } else {
        printf("extraídos: %ld, no descargados todavía: %ld, con error: %ld\n", done, missing, failed);
    }
    return failed ? 1 : 0;
}
