#include "lang.h"

#include "fmt/fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *es, *tr;
} Pair;

/* Cada tabla termina en { NULL, NULL } (así puede estar vacía) */
static const Pair k_en[] = {
#include "lang_en.inc"
    { NULL, NULL } };
static const Pair k_es_es[] = {
#include "lang_es_es.inc"
    { NULL, NULL } };
static const Pair k_de[] = {
#include "lang_de.inc"
    { NULL, NULL } };
static const Pair k_fr[] = {
#include "lang_fr.inc"
    { NULL, NULL } };
static const Pair k_it[] = {
#include "lang_it.inc"
    { NULL, NULL } };
static const Pair k_pt[] = {
#include "lang_pt.inc"
    { NULL, NULL } };
static const Pair k_pl[] = {
#include "lang_pl.inc"
    { NULL, NULL } };
static const Pair k_ru[] = {
#include "lang_ru.inc"
    { NULL, NULL } };
static const Pair k_ja[] = {
#include "lang_ja.inc"
    { NULL, NULL } };
static const Pair k_ko[] = {
#include "lang_ko.inc"
    { NULL, NULL } };
static const Pair k_zh_cn[] = {
#include "lang_zh_cn.inc"
    { NULL, NULL } };
static const Pair k_zh_tw[] = {
#include "lang_zh_tw.inc"
    { NULL, NULL } };

#define TTF_GLOBAL "data/hd/ui/fonts/blizzardglobal-v5_81.ttf"

static const struct {
    const char *code, *name;
    const Pair *table;      /* NULL: los textos del motor tal cual (en español) */
    int         to_english; /* lo que falta en la tabla, en inglés (si no, en español) */
    const char *font_dir, *ttf;
} k_langs[LANG_COUNT] = {
    [LANG_ES] = { "esMX", "Español (Latinoamérica)", NULL, 0, "latin", TTF_GLOBAL },
    [LANG_EN] = { "enUS", "English", k_en, 0, "latin", TTF_GLOBAL },
    [LANG_ES_ES] = { "esES", "Español (España)", k_es_es, 0, "latin", TTF_GLOBAL },
    [LANG_DE] = { "deDE", "Deutsch", k_de, 1, "latin", TTF_GLOBAL },
    [LANG_FR] = { "frFR", "Français", k_fr, 1, "latin", TTF_GLOBAL },
    [LANG_IT] = { "itIT", "Italiano", k_it, 1, "latin", TTF_GLOBAL },
    [LANG_PT] = { "ptBR", "Português (Brasil)", k_pt, 1, "latin", TTF_GLOBAL },
    [LANG_PL] = { "plPL", "Polski", k_pl, 1, "latin2", TTF_GLOBAL },
    [LANG_RU] = { "ruRU", "Русский", k_ru, 1, "latin", TTF_GLOBAL },
    [LANG_JA] = { "jaJP", "日本語", k_ja, 1, "latin", "data/hd/ui/fonts/bljap_v8_3.ttf" },
    [LANG_KO] = { "koKR", "한국어", k_ko, 1, "latin", TTF_GLOBAL },
    [LANG_ZH_CN] = { "zhCN", "简体中文", k_zh_cn, 1, "latin", TTF_GLOBAL },
    [LANG_ZH_TW] = { "zhTW", "繁體中文", k_zh_tw, 1, "latin", "data/hd/ui/fonts/blizzardglobaltcunicode.ttf" },
};

typedef struct {
    const Pair *pairs;
    short      *hash; /* índice + 1 en pairs; 0 = libre */
    int         size;
} Map;

static int   s_lang = LANG_ES;
static char  s_installed[8];
static Map   s_map, s_map_en; /* la del idioma y la del inglés (para lo que le falta) */
static FILE *s_dump;          /* D2_LANG_DUMP: lo que se dibujó sin traducción */

static unsigned hash_str(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static void map_build(Map *m, const Pair *pairs)
{
    free(m->hash);
    memset(m, 0, sizeof *m);
    if (!pairs)
        return;
    int n = 0;
    while (pairs[n].es)
        n++;
    m->pairs = pairs;
    m->size = 1;
    while (m->size < n * 2 + 2)
        m->size *= 2;
    m->hash = calloc((size_t)m->size, sizeof *m->hash);
    for (int i = 0; m->hash && i < n; i++) {
        unsigned h = hash_str(pairs[i].es) & (unsigned)(m->size - 1);
        while (m->hash[h])
            h = (h + 1) & (unsigned)(m->size - 1);
        m->hash[h] = (short)(i + 1);
    }
}

static const char *map_find(const Map *m, const char *es)
{
    if (!m->hash || !es)
        return NULL;
    unsigned h = hash_str(es) & (unsigned)(m->size - 1);
    for (; m->hash[h]; h = (h + 1) & (unsigned)(m->size - 1))
        if (strcmp(m->pairs[m->hash[h] - 1].es, es) == 0)
            return m->pairs[m->hash[h] - 1].tr;
    return NULL;
}

int lang_find(const char *code)
{
    if (!code || !code[0])
        return -1;
    if (!strcmp(code, "es"))
        return LANG_ES;
    if (!strcmp(code, "en"))
        return LANG_EN;
    for (int i = 0; i < LANG_COUNT; i++)
        if (!strncmp(code, k_langs[i].code, 4))
            return i;
    return -1;
}

void lang_init(const char *pref)
{
    size_t n;
    uint8_t *d = fs_read("idioma.txt", &n);
    s_installed[0] = '\0';
    if (d) {
        int i = 0;
        while ((size_t)i < n && i < 7 && (unsigned char)d[i] > ' ')
            s_installed[i] = (char)d[i], i++;
        s_installed[i] = '\0';
        free(d);
    }
    const char *env = getenv("D2_LANG");
    int l = lang_find(env);
    if (l < 0)
        l = lang_find(pref);
    if (l < 0)
        l = lang_find(s_installed);
    s_lang = l < 0 ? LANG_ES : l;
    map_build(&s_map, k_langs[s_lang].table);
    map_build(&s_map_en, k_langs[s_lang].to_english ? k_en : NULL);
    if (getenv("D2_LANG_DUMP") && !s_dump)
        s_dump = fopen(getenv("D2_LANG_DUMP"), "w");
}

int lang_current(void)
{
    return s_lang;
}

int lang_en(void)
{
    return s_lang == LANG_EN;
}

const char *lang_code(int lang)
{
    return lang >= 0 && lang < LANG_COUNT ? k_langs[lang].code : "esMX";
}

const char *lang_name(int lang)
{
    return lang >= 0 && lang < LANG_COUNT ? k_langs[lang].name : "";
}

const char *lang_strings_code(void)
{
    return k_langs[s_lang].code;
}

const char *lang_installed(void)
{
    int l = lang_find(s_installed);
    return l < 0 ? "" : k_langs[l].code;
}

const char *lang_font_dir(void)
{
    return k_langs[s_lang].font_dir;
}

const char *lang_ttf(void)
{
    return k_langs[s_lang].ttf;
}

static const char *find(const char *es)
{
    const char *t = map_find(&s_map, es);
    return t ? t : map_find(&s_map_en, es);
}

const char *tr(const char *es)
{
    if (s_lang == LANG_ES)
        return es;
    const char *t = find(es);
    return t ? t : es;
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
    if (s_lang == LANG_ES || !s[0])
        return s;
    const char *t = find(s);
    if (t)
        return t;
    if (s_dump && !map_find(&s_map, s))
        dump_missing(s);
    return s;
}
