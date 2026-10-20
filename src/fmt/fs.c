#include "fs.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static char s_root[512] = ".";

void fs_set_root(const char *root)
{
    snprintf(s_root, sizeof s_root, "%s", root);
}

int fs_find(const char *dir, const char *suffix, char *out, size_t cap)
{
    char full[1024];
    snprintf(full, sizeof full, "%s/%s", s_root, dir);
    DIR *d = opendir(full);
    if (!d)
        return -1;
    size_t sl = strlen(suffix);
    struct dirent *e;
    int found = -1;
    while ((e = readdir(d)) != NULL) {
        size_t nl = strlen(e->d_name);
        if (nl >= sl && strcmp(e->d_name + nl - sl, suffix) == 0) {
            snprintf(out, cap, "%s/%s", dir, e->d_name);
            found = 0;
            break;
        }
    }
    closedir(d);
    return found;
}

void fs_prefetch_dir(const char *dir)
{
#ifdef __linux__
    char full[1024];
    snprintf(full, sizeof full, "%s/%s", s_root, dir);
    DIR *d = opendir(full);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char sub[1024], path[1600];
        snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
        snprintf(path, sizeof path, "%s/%s", s_root, sub);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            fs_prefetch_dir(sub);
            continue;
        }
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
        close(fd);
    }
    closedir(d);
#else
    (void)dir;
#endif
}

FILE *fs_open(const char *path)
{
    char full[1024];
    int n = snprintf(full, sizeof full, "%s/", s_root);
    for (const char *p = path; *p && n + 1 < (int)sizeof full; p++)
        full[n++] = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
    full[n] = '\0';
    return fopen(full, "rb");
}

uint8_t *fs_read(const char *path, size_t *size)
{
    FILE *f = fs_open(path);
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = len >= 0 ? malloc((size_t)len + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf && size)
        *size = (size_t)len;
    return buf;
}
