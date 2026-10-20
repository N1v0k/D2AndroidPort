#include "lang.h"

#include "fmt/fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *es, *en;
} Pair;

static const Pair k_en[] = {
#include "lang_en.inc"
};

#define NUM_PAIRS (int)(sizeof k_en / sizeof k_en[0])

static int    s_en;
static short *s_hash; /* índice + 1 en k_en; 0 = libre */
static int    s_hash_size;
static FILE  *s_dump; /* D2_LANG_DUMP: lo que se dibujó sin traducción */

static unsigned hash_str(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

void lang_init(void)
{
    const char *env = getenv("D2_LANG");
    char buf[8] = "";
    if (env) {
        snprintf(buf, sizeof buf, "%s", env);
    } else {
        size_t n;
        uint8_t *d = fs_read("idioma.txt", &n);
        if (d) {
            snprintf(buf, sizeof buf, "%.*s", (int)(n < 7 ? n : 7), (const char *)d);
            free(d);
        }
    }
    s_en = buf[0] == 'e' && buf[1] == 'n';
    if (!s_en || s_hash)
        return;
    s_hash_size = 1;
    while (s_hash_size < NUM_PAIRS * 2)
        s_hash_size *= 2;
    s_hash = calloc((size_t)s_hash_size, sizeof *s_hash);
    for (int i = 0; i < NUM_PAIRS; i++) {
        unsigned h = hash_str(k_en[i].es) & (unsigned)(s_hash_size - 1);
        while (s_hash[h])
            h = (h + 1) & (unsigned)(s_hash_size - 1);
        s_hash[h] = (short)(i + 1);
    }
    if (getenv("D2_LANG_DUMP"))
        s_dump = fopen(getenv("D2_LANG_DUMP"), "w");
}

int lang_en(void)
{
    return s_en;
}

const char *lang_strings_code(void)
{
    return s_en ? "enUS" : "esMX";
}

static const char *find(const char *es)
{
    if (!s_hash || !es)
        return NULL;
    unsigned h = hash_str(es) & (unsigned)(s_hash_size - 1);
    for (; s_hash[h]; h = (h + 1) & (unsigned)(s_hash_size - 1))
        if (strcmp(k_en[s_hash[h] - 1].es, es) == 0)
            return k_en[s_hash[h] - 1].en;
    return NULL;
}

const char *tr(const char *es)
{
    if (!s_en)
        return es;
    const char *en = find(es);
    return en ? en : es;
}

/* Para la prueba: anota una vez cada texto que se dibujó sin traducción */
static void dump_missing(const char *s)
{
    static unsigned seen[8192];
    unsigned h = hash_str(s) | 1u, i = h & 8191u;
    for (int n = 0; n < 8192 && seen[i]; n++, i = (i + 1) & 8191u)
        if (seen[i] == h)
            return;
    seen[i] = h;
    fprintf(s_dump, "%s\n", s);
    fflush(s_dump);
}

const char *lang_screen(const char *s)
{
    if (!s_en || !s[0])
        return s;
    const char *en = find(s);
    if (en)
        return en;
    if (s_dump)
        dump_missing(s);
    return s;
}
