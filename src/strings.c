#include "strings.h"

#include "fmt/fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    char *text;                  /* primera forma, sin marcas */
    char *forms[GENDER_COUNT];   /* formas por género, si el texto las trae */
    int   gender;                /* género marcado adelante (sustantivos) */
    int   id;                    /* el "id" del .json (en vo.json lleva a la grabación) */
} Entry;

/* Grabaciones de las voces (metadata-audio-vo.json): por id, quién habla,
   el archivo y el acto */
typedef struct {
    char *speaker, *file, *act;
} VoFile;
static VoFile *s_vo;
static int     s_vo_count;

static Entry *s_entries;
static int    s_count, s_cap;
static int   *s_hash; /* índice por clave: posición en s_entries + 1 (0 = vacío) */
static int    s_hash_size;

static unsigned hash_key(const char *k)
{
    unsigned h = 2166136261u;
    while (*k)
        h = (h ^ (unsigned char)*k++) * 16777619u;
    return h;
}

/* Con claves repetidas queda la primera, como al buscar en orden */
static void build_index(void)
{
    free(s_hash);
    s_hash_size = 1;
    while (s_hash_size < s_count * 2)
        s_hash_size *= 2;
    s_hash = calloc((size_t)s_hash_size, sizeof *s_hash);
    for (int i = 0; i < s_count; i++) {
        unsigned h = hash_key(s_entries[i].key) & (unsigned)(s_hash_size - 1);
        while (s_hash[h] && strcmp(s_entries[s_hash[h] - 1].key, s_entries[i].key) != 0)
            h = (h + 1) & (unsigned)(s_hash_size - 1);
        if (!s_hash[h])
            s_hash[h] = i + 1;
    }
}

/* Lee un string JSON a partir de la comilla de apertura; devuelve una copia
   en UTF-8 (resuelve \" \\ \n y \uXXXX) y deja *p después del cierre. */
static char *json_string(const char **p)
{
    const char *s = *p + 1;
    /* Primero se busca el cierre: el texto sin escapes nunca es más largo */
    const char *end = s;
    while (*end && *end != '"')
        end += end[0] == '\\' && end[1] ? 2 : 1;
    char *out = malloc((size_t)(end - s) + 1), *o = out;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) {
            s++;
            if (*s == 'n') *o++ = '\n';
            else if (*s == 'u' && s[1] && s[2] && s[3] && s[4]) {
                unsigned cp = 0;
                sscanf(s + 1, "%4x", &cp);
                s += 4;
                if (cp < 0x80) *o++ = (char)cp;
                else if (cp < 0x800) {
                    *o++ = (char)(0xC0 | (cp >> 6));
                    *o++ = (char)(0x80 | (cp & 0x3F));
                } else {
                    *o++ = (char)(0xE0 | (cp >> 12));
                    *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                    *o++ = (char)(0x80 | (cp & 0x3F));
                }
            } else *o++ = *s;
            s++;
            continue;
        }
        *o++ = *s++;
    }
    *o = '\0';
    *p = *s ? s + 1 : s;
    return out;
}

static int gender_of_tag(const char *tag)
{
    static const char *const tags[GENDER_COUNT] = { "[ms]", "[fs]", "[mp]", "[fp]", "[ns]", "[pl]", "[nl]", "[p]" };
    for (int g = 0; g < GENDER_COUNT; g++)
        if (strncmp(tag, tags[g], strlen(tags[g])) == 0)
            return g;
    return -1;
}

static void add_entry(char *key, char *text, int id)
{
    if (s_count == s_cap) {
        s_cap = s_cap ? s_cap * 2 : 1024;
        s_entries = realloc(s_entries, (size_t)s_cap * sizeof *s_entries);
    }
    Entry *e = &s_entries[s_count++];
    memset(e, 0, sizeof *e);
    e->key = key;
    e->gender = GENDER_MS;
    e->id = id;

    /* Separar las formas: "[ms]roído[fs]roída..." -> forms[MS] = "roído", ... */
    int nforms = 0;
    char *p = text;
    while (*p == '[') {
        char *close = strchr(p, ']');
        if (!close)
            break;
        int g = gender_of_tag(p);
        char *start = close + 1, *next = strchr(start, '[');
        size_t len = next ? (size_t)(next - start) : strlen(start);
        if (g >= 0) {
            e->forms[g] = malloc(len + 1);
            memcpy(e->forms[g], start, len);
            e->forms[g][len] = '\0';
            if (nforms++ == 0)
                e->gender = g;
        }
        if (!next)
            break;
        p = next;
    }
    if (nforms == 0) {
        e->text = text;
        return;
    }
    /* Sustantivo con una sola marca: el texto es esa forma y su género */
    for (int g = 0; g < GENDER_COUNT; g++)
        if (e->forms[g]) {
            e->text = strdup(e->forms[g]);
            break;
        }
    if (nforms > 1)
        e->gender = GENDER_MS; /* adjetivo con varias formas: no tiene género propio */
    free(text);
}

/* En vo.json, la mitad de las líneas en inglés, alemán, francés, italiano, polaco y español de España
   empiezan con un número pegado ("46I should add...", "95\nPareces noble..."; en el latino no está):
   no es parte de lo que se dice. Se saca si va pegado a una letra o con un salto de línea (un "3 días"
   con espacio queda) */
static void strip_vo_number(char *text)
{
    size_t d = 0;
    while (text[d] >= '0' && text[d] <= '9')
        d++;
    if (d == 0 || text[d] == '\0' || text[d] == ' ' || text[d] == '\t')
        return;
    while (text[d] == '\n' || text[d] == '\r')
        d++;
    memmove(text, text + d, strlen(text + d) + 1);
}

/* Los archivos son listas de objetos {"id": .., "Key": "..", "enUS": "..", ..., "esMX": ".."} */
static void parse_file(const char *json, const char *lang, int vo)
{
    char lang_field[16];
    snprintf(lang_field, sizeof lang_field, "\"%s\"", lang);
    const char *p = json;
    char *key = NULL;
    int id = -1;
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p, "\"id\"", 4) == 0) { /* "id": 81, */
            const char *colon = strchr(p + 4, ':');
            id = colon ? atoi(colon + 1) : -1;
            p += 4;
            continue;
        }
        if (strncmp(p, "\"Key\"", 5) == 0 || strncmp(p, lang_field, strlen(lang_field)) == 0) {
            int is_key = p[1] == 'K';
            p = strchr(p + 1, '"'); /* fin del nombre del campo */
            if (!p || !(p = strchr(p + 1, '"')))
                break;
            char *value = json_string(&p);
            if (is_key) {
                free(key);
                key = value;
            } else if (key) {
                if (vo)
                    strip_vo_number(value);
                add_entry(key, value, id);
                key = NULL;
            } else {
                free(value);
            }
            continue;
        }
        char *skip = json_string(&p); /* cualquier otro string */
        free(skip);
    }
    free(key);
}

/* metadata-audio-vo.json: [{"Id": 81, "Speaker": "Kashya", "AudioFileName":
   "Kas_act1_q2_init.wav", "ExtraMetadata": "Act1"}, ...] */
static void load_vo_metadata(void)
{
    size_t n;
    uint8_t *d = fs_read("data/local/lng/strings/metadata/metadata-audio-vo.json", &n);
    if (!d)
        return;
    d[n] = '\0';
    const char *p = (const char *)d;
    int id = -1;
    char *speaker = NULL, *file = NULL, *act = NULL;
    while ((p = strchr(p, '"')) != NULL) {
        char *name = json_string(&p);
        const char *colon = strchr(p, ':');
        if (!colon) {
            free(name);
            break;
        }
        p = colon + 1;
        while (*p == ' ')
            p++;
        if (strcmp(name, "Id") == 0) {
            id = atoi(p);
        } else if (*p == '"') {
            char *value = json_string(&p);
            char **slot = strcmp(name, "Speaker") == 0 ? &speaker
                        : strcmp(name, "AudioFileName") == 0 ? &file
                        : strcmp(name, "ExtraMetadata") == 0 ? &act : NULL;
            if (slot) {
                free(*slot);
                *slot = value;
            } else {
                free(value);
            }
        }
        free(name);
        /* Al terminar cada objeto se guarda lo juntado */
        const char *close = strchr(p, '}'), *next = strchr(p, '"');
        if (close && (!next || close < next)) {
            if (id >= 0 && id < 100000 && speaker && file) {
                if (id >= s_vo_count) {
                    VoFile *grown = realloc(s_vo, (size_t)(id + 1) * sizeof *s_vo);
                    if (grown) {
                        memset(grown + s_vo_count, 0, (size_t)(id + 1 - s_vo_count) * sizeof *s_vo);
                        s_vo = grown;
                        s_vo_count = id + 1;
                    }
                }
                if (id < s_vo_count) {
                    s_vo[id].speaker = speaker;
                    s_vo[id].file = file;
                    s_vo[id].act = act;
                    speaker = file = act = NULL;
                }
            }
            free(speaker);
            free(file);
            free(act);
            speaker = file = act = NULL;
            id = -1;
            p = close + 1;
        }
    }
    free(speaker);
    free(file);
    free(act);
    free(d);
}

int strings_load(const char *lang)
{
    static const char *const files[] = { "levels",     "monsters",         "npcs",           "objects",
                                         "ui",         "item-names",       "item-nameaffixes", "item-modifiers", "item-runes", "item-gems", "skills",
                                         "quests",     "vo",               "mercenaries",    "shrines" };
    int loaded = 0;
    for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++) {
        char path[128];
        snprintf(path, sizeof path, "data/local/lng/strings/%s.json", files[i]);
        size_t n;
        uint8_t *d = fs_read(path, &n);
        if (!d)
            continue;
        d[n] = '\0';
        parse_file((const char *)d, lang, strcmp(files[i], "vo") == 0);
        free(d);
        loaded++;
    }
    build_index();
    load_vo_metadata();
    return loaded ? 0 : -1;
}

static const Entry *find_entry(const char *key)
{
    if (!s_hash)
        return NULL;
    unsigned h = hash_key(key) & (unsigned)(s_hash_size - 1);
    while (s_hash[h]) {
        const Entry *e = &s_entries[s_hash[h] - 1];
        if (strcmp(e->key, key) == 0)
            return e;
        h = (h + 1) & (unsigned)(s_hash_size - 1);
    }
    return NULL;
}

static void lower_into(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

int str_voice_path(const char *key, char *out, size_t cap)
{
    const Entry *e = find_entry(key);
    if (!e || e->id < 0 || e->id >= s_vo_count || !s_vo[e->id].file)
        return -1;
    const VoFile *v = &s_vo[e->id];
    char speaker[32], file[64], act[16];
    lower_into(speaker, sizeof speaker, v->speaker);
    /* Algunas carpetas no se llaman como el que habla en la metadata */
    if (strcmp(speaker, "deckardcain") == 0)
        snprintf(speaker, sizeof speaker, "cain");
    else if (strcmp(speaker, "greiz") == 0) /* la carpeta es "griez", como sus claves */
        snprintf(speaker, sizeof speaker, "griez");
    else if (strcmp(speaker, "tyrael") == 0) /* la carpeta de D2R es "tyreal" (Actos 2 y 4) */
        snprintf(speaker, sizeof speaker, "tyreal");
    else if (strcmp(speaker, "drehya") == 0) /* Anya: su carpeta (Acto 5) */
        snprintf(speaker, sizeof speaker, "anya");
    else if (!strcmp(speaker, "andariel") || !strcmp(speaker, "bloodraven") || !strcmp(speaker, "the countess") ||
             !strcmp(speaker, "the smith") || !strcmp(speaker, "baal"))
        snprintf(speaker, sizeof speaker, "monster");
    lower_into(file, sizeof file, v->file);
    lower_into(act, sizeof act, v->act ? v->act : "act1");
    char *dot = strrchr(file, '.');
    if (dot)
        *dot = '\0';
    snprintf(out, cap, "data/local/sfx/%s/%s/%s.flac", act, speaker, file);
    return 0;
}

int str_exists(const char *key)
{
    const Entry *e = find_entry(key);
    return e && e->text && e->text[0];
}

const char *str_get(const char *key)
{
    const Entry *e = find_entry(key);
    return e ? e->text : key;
}

int str_gender(const char *key)
{
    const Entry *e = find_entry(key);
    return e ? e->gender : GENDER_MS;
}

void str_compose(char *out, size_t cap, const char *fmt_key, const char *a0, const char *a1, const char *a2)
{
    const char *fmt = str_exists(fmt_key) ? str_get(fmt_key) : "%0 %1 %2";
    const char *colon = strchr(fmt, ':'), *pct = strchr(fmt, '%');
    if (colon && (!pct || colon < pct)) /* "a0n1:": con quién concuerda cada uno */
        fmt = colon + 1;
    const char *args[3] = { a0 ? a0 : "", a1 ? a1 : "", a2 ? a2 : "" };
    size_t n = 0;
    if (!cap)
        return;
    for (const char *p = fmt; *p && n + 1 < cap; p++) {
        const char *put = NULL;
        char one[2] = { *p, '\0' };
        if (p[0] == '%' && p[1] >= '0' && p[1] <= '2')
            put = args[*++p - '0'];
        else
            put = one;
        for (; *put && n + 1 < cap; put++) {
            if (*put == ' ' && (n == 0 || out[n - 1] == ' '))
                continue;
            out[n++] = *put;
        }
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

const char *str_get_gender(const char *key, int gender)
{
    const Entry *e = find_entry(key);
    if (!e)
        return key;
    if (gender >= 0 && gender < GENDER_COUNT && e->forms[gender])
        return e->forms[gender];
    return e->text;
}
