#include "itemstats.h"

#include "items.h"
#include "skills.h"
#include "strings.h"
#include "fmt/formats.h"
#include "fmt/fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char        name[40];
    int         descfunc, descval, priority;
    const char *pos, *neg, *str2; /* claves de los textos */
    int         dgrp;
    const char *dgrp_str;
    int         op, op_param, op_target; /* las que suben con el nivel (op 2, 4, 5) */
} IscDef;

#define MAX_PROPS 400
typedef struct {
    char code[28];
    int  func[7], stat[7], val[7];
} PropDef;

static IscDef  s_isc[ISC_MAX];
static PropDef s_props[MAX_PROPS];
static int     s_nprops;
static uint32_t s_rng = 0x51C0DE;
static int      s_roll_mode; /* itemstats_roll_mode */

void itemstats_roll_mode(int mode)
{
    s_roll_mode = mode;
}

static int rnd_range(int lo, int hi)
{
    if (hi < lo) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    if (s_roll_mode)
        return s_roll_mode < 0 ? lo : hi;
    s_rng = s_rng * 1664525u + 1013904223u;
    return hi > lo ? lo + (int)((s_rng >> 8) % (uint32_t)(hi - lo + 1)) : lo;
}

static int load_table(TxtTable *t, const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "data/global/excel/%s.txt", name);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int r = d ? txt_load(d, n, t) : -1;
    free(d);
    if (r != 0)
        fprintf(stderr, "falta %s\n", path);
    return r;
}

static const char *dup_or_null(const char *s)
{
    return s && s[0] ? strdup(s) : NULL;
}

int itemstats_find(const char *name)
{
    if (!name || !name[0])
        return -1;
    for (int i = 0; i < ISC_MAX; i++)
        if (s_isc[i].name[0] && strcmp(s_isc[i].name, name) == 0)
            return i;
    return -1;
}

int itemstats_init(void)
{
    TxtTable isc = { 0 }, pr = { 0 };
    if (load_table(&isc, "itemstatcost") != 0 || load_table(&pr, "properties") != 0) {
        txt_free(&isc);
        txt_free(&pr);
        return -1;
    }
    memset(s_isc, 0, sizeof s_isc);
    for (int r = 1; r < isc.rows; r++) {
        int id = txt_get_int(&isc, r, "*ID", -1);
        if (id < 0 || id >= ISC_MAX)
            continue;
        IscDef *d = &s_isc[id];
        snprintf(d->name, sizeof d->name, "%s", txt_get(&isc, r, txt_col(&isc, "Stat")));
        d->descfunc = txt_get_int(&isc, r, "descfunc", 0);
        d->descval = txt_get_int(&isc, r, "descval", 0);
        d->priority = txt_get_int(&isc, r, "descpriority", 0);
        d->pos = dup_or_null(txt_get(&isc, r, txt_col(&isc, "descstrpos")));
        d->neg = dup_or_null(txt_get(&isc, r, txt_col(&isc, "descstrneg")));
        d->str2 = dup_or_null(txt_get(&isc, r, txt_col(&isc, "descstr2")));
        d->dgrp = txt_get_int(&isc, r, "dgrp", 0);
        d->dgrp_str = dup_or_null(txt_get(&isc, r, txt_col(&isc, "dgrpstrpos")));
        d->op = txt_get_int(&isc, r, "op", 0);
        d->op_param = txt_get_int(&isc, r, "op param", 0);
    }
    /* El destino de las que suben con el nivel ("op stat1"): se resuelve al final */
    for (int r = 1; r < isc.rows; r++) {
        int id = txt_get_int(&isc, r, "*ID", -1);
        if (id >= 0 && id < ISC_MAX)
            s_isc[id].op_target = itemstats_find(txt_get(&isc, r, txt_col(&isc, "op stat1")));
    }
    s_nprops = 0;
    for (int r = 1; r < pr.rows && s_nprops < MAX_PROPS; r++) {
        const char *code = txt_get(&pr, r, txt_col(&pr, "code"));
        if (!code[0])
            continue;
        PropDef *p = &s_props[s_nprops++];
        memset(p, 0, sizeof *p);
        snprintf(p->code, sizeof p->code, "%s", code);
        for (int k = 0; k < 7; k++) {
            char col[8];
            snprintf(col, sizeof col, "func%d", k + 1);
            p->func[k] = txt_get_int(&pr, r, col, 0);
            snprintf(col, sizeof col, "stat%d", k + 1);
            p->stat[k] = itemstats_find(txt_get(&pr, r, txt_col(&pr, col)));
            snprintf(col, sizeof col, "val%d", k + 1);
            p->val[k] = txt_get_int(&pr, r, col, 0);
        }
    }
    txt_free(&isc);
    txt_free(&pr);
    return 0;
}

static const PropDef *find_prop(const char *code)
{
    for (int i = 0; i < s_nprops; i++)
        if (strcmp(s_props[i].code, code) == 0)
            return &s_props[i];
    return NULL;
}

int itemstats_has_property(const char *code)
{
    return code && code[0] && find_prop(code) != NULL;
}

int itemstats_class_from_d2(int d2cls)
{
    if (d2cls == 7) /* el Brujo de D2R */
        return 7;
    static const int k_map[7] = { 1, 2, 4, 3, 0, 6, 5 };
    return d2cls >= 0 && d2cls < 7 ? k_map[d2cls] : -1;
}

int itemstats_class_to_d2(int cls)
{
    if (cls == 7)
        return 7;
    for (int k = 0; k < 7; k++)
        if (itemstats_class_from_d2(k) == cls)
            return k;
    return -1;
}

int itemstats_add(ItemProp *out, int count, int cap, int stat, int param, int value)
{
    if (stat < 0 || stat >= ISC_MAX || value == 0)
        return count;
    for (int i = 0; i < count; i++)
        if (out[i].stat == stat && out[i].param == param) {
            out[i].value += value;
            return count;
        }
    if (count >= cap)
        return count;
    out[count].stat = (uint16_t)stat;
    out[count].param = (int16_t)param;
    out[count].value = value;
    return count + 1;
}

/* El parámetro de la tabla: un número o el nombre de una habilidad */
static int par_value(const char *par, int skill)
{
    if (!par || !par[0])
        return 0;
    if (isdigit((unsigned char)par[0]) || par[0] == '-')
        return atoi(par);
    return skill ? skills_global_find(par) : 0;
}

/* El nivel de una habilidad de las cargas o de "probabilidad de lanzar":
   el de la tabla, o el que da el nivel del objeto (como el original) */
static int skill_level_for(int skill, int max, int ilvl)
{
    int req = skills_global_reqlevel(skill);
    if (max > 0)
        return max;
    if (max < 0) {
        int span = 99 - req;
        span = span < 1 ? 1 : span;
        int div = -(span / max);
        div = div < 1 ? 1 : div;
        int lvl = (ilvl - req) / div;
        return lvl < 1 ? 1 : lvl;
    }
    int lvl = (ilvl - req) / 4 + 1;
    return lvl < 1 ? 1 : lvl > 20 ? 20 : lvl;
}

int itemstats_apply(const ItemType *t, int ilvl, const char *code, const char *par, int min, int max, ItemProp *out,
                    int count, int cap, int *sockets)
{
    const PropDef *p = find_prop(code);
    if (!p)
        return count;
    int prev = 0;
    for (int k = 0; k < 7; k++) {
        int f = p->func[k], st = p->stat[k], v = 0;
        if (!f)
            break;
        switch (f) {
        case 3: /* el mismo valor que la anterior (todas las resistencias) */
        case 4:
            v = prev ? prev : rnd_range(min, max);
            count = itemstats_add(out, count, cap, st, 0, v);
            break;
        case 5: /* daño mínimo y máximo (el del arma) */
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, 21, 0, v);
            break;
        case 6:
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, 22, 0, v);
            break;
        case 7: /* % de daño mejorado: el mínimo y el máximo */
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, 17, 0, v);
            count = itemstats_add(out, count, cap, 18, 0, v);
            break;
        case 10: { /* + a una solapa: el parámetro es clase * 3 + solapa */
            int tab = par_value(par, 0);
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, st, tab % 3 + 8 * (tab / 3), v);
            break;
        }
        case 11: { /* probabilidad de lanzar una habilidad (al atacar, al golpear...) */
            int skill = par_value(par, 1);
            if (skill < 0)
                break;
            int lvl = skill_level_for(skill, max, ilvl);
            v = min > 0 ? min : 5;
            count = itemstats_add(out, count, cap, st, (lvl & 63) | (skill << 6), v);
            break;
        }
        case 12: /* una habilidad al azar entre min y max, con el nivel del parámetro */
            v = par_value(par, 0);
            count = itemstats_add(out, count, cap, st, rnd_range(min, max), v > 0 ? v : 1);
            break;
        case 14: { /* engarces */
            int n = rnd_range(min, max);
            if (n <= 0)
                n = par_value(par, 0);
            if (sockets)
                *sockets = n < 1 ? 1 : n;
            v = n;
            break;
        }
        case 15:
            v = min;
            count = itemstats_add(out, count, cap, st, 0, v);
            break;
        case 16:
            v = max;
            count = itemstats_add(out, count, cap, st, 0, v);
            break;
        case 17: /* las que suben con el nivel: el valor es el parámetro (en octavos) */
            v = par_value(par, 0);
            if (!v)
                v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, st, 0, v);
            break;
        case 18: /* según la hora del día: se queda con el máximo */
            v = max;
            count = itemstats_add(out, count, cap, st, 0, v);
            break;
        case 19: { /* cargas de una habilidad */
            int skill = par_value(par, 1);
            if (skill < 0)
                break;
            int lvl = skill_level_for(skill, max, ilvl), charges = min;
            if (charges < 0)
                charges = lvl * -charges / 8 - charges;
            charges = charges <= 0 ? 5 : charges > 255 ? 255 : charges;
            v = charges;
            count = itemstats_add(out, count, cap, st, (lvl & 63) | (skill << 6), (charges << 8) | charges);
            break;
        }
        case 20: /* indestructible */
            v = 1;
            count = itemstats_add(out, count, cap, 152, 0, 1);
            break;
        case 21: /* + a las habilidades de una clase: la clase es val */
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, st, p->val[k], v);
            break;
        case 22: /* + a una habilidad, un aura */
        case 24: {
            int skill = par_value(par, f == 22);
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, st, skill, v);
            break;
        }
        case 23: /* etéreo: no hay durabilidad */
            break;
        case 36: /* randclassskill (la Antorcha): + val a las habilidades de una clase al azar entre min y max */
            v = p->val[k] ? p->val[k] : 3;
            count = itemstats_add(out, count, cap, st, rnd_range(min, max), v);
            break;
        default: /* 1, 2, 8, 13: el valor tirado */
            v = rnd_range(min, max);
            count = itemstats_add(out, count, cap, st, 0, v);
            break;
        }
        prev = v;
    }
    (void)t;
    return count;
}

/* ---- Los textos ---- */

void itemstats_format(char *buf, size_t cap, const char *fmt, const FmtArg *args, int nargs)
{
    size_t o = 0;
    int next = 0;
    if (!fmt)
        fmt = "";
    for (const char *f = fmt; *f && o + 1 < cap; f++) {
        if (f[0] != '%') {
            buf[o++] = *f;
            continue;
        }
        if (f[1] == '%') {
            buf[o++] = '%';
            f++;
            continue;
        }
        int idx = -1, plus = 0;
        const char *e = f + 1;
        if (isdigit((unsigned char)*e)) { /* %0, %1...: el argumento N */
            idx = *e - '0';
            e++;
            f = e - 1;
        } else {
            if (*e == '+') {
                plus = 1;
                e++;
            }
            if (*e == 'd' || *e == 'i' || *e == 's') {
                idx = next++;
                f = e;
            } else { /* un % suelto */
                buf[o++] = '%';
                continue;
            }
        }
        if (idx < 0 || idx >= nargs)
            continue;
        const FmtArg *a = &args[idx];
        int w = a->s ? snprintf(buf + o, cap - o, "%s", a->s) : snprintf(buf + o, cap - o, plus ? "%+d" : "%d", a->i);
        o += w > 0 ? (size_t)w : 0;
        if (o >= cap)
            o = cap - 1;
    }
    buf[o] = '\0';
}

static const char *const k_class_skills_key[8] = { "ModStr3a", "ModStr3d", "ModStr3c", "ModStr3b",
                                                   "ModStr3e", "ModStre8a", "ModStre8b", "ModStrge9" };
static const char *const k_class_only_key[8] = { "AmaOnly", "SorOnly", "NecOnly", "PalOnly", "BarOnly", "DruOnly", "AssOnly",
                                                 "WarOnly" };
/* StrSklTabItemN de cada solapa (clase del original, página del árbol; el Brujo de D2R al final) */
static const int k_tab_str[8][3] = { { 3, 2, 1 }, { 15, 14, 13 }, { 8, 7, 9 }, { 6, 5, 4 },
                                     { 11, 12, 10 }, { 16, 17, 18 }, { 19, 20, 21 }, { 24, 22, 23 } };

static const char *skill_name(int id)
{
    const char *n = skills_global_name(id);
    return n && n[0] ? n : "?";
}

typedef struct {
    int  priority;
    char text[112];
} Line;

static int add_line(Line *l, int n, int max, int prio, const char *text)
{
    if (n >= max || !text[0])
        return n;
    l[n].priority = prio;
    snprintf(l[n].text, sizeof l[n].text, "%s", text);
    return n + 1;
}

static int find_stat(const ItemProp *p, int n, int stat)
{
    for (int i = 0; i < n; i++)
        if (p[i].stat == stat)
            return i;
    return -1;
}

int itemstats_describe(const ItemProp *p, int n, int clvl, char lines[][112], int max_lines)
{
    Line l[48];
    int nl = 0;
    uint8_t used[64] = { 0 };
    n = n > 64 ? 64 : n;
    char buf[112];
    FmtArg a[4];

    /* Daño mejorado: el mínimo y el máximo juntos */
    int ed = find_stat(p, n, 17), edmin = find_stat(p, n, 18);
    if (ed >= 0 && edmin >= 0 && p[ed].value == p[edmin].value) {
        a[0].i = p[ed].value;
        a[0].s = NULL;
        itemstats_format(buf, sizeof buf, str_get("strModEnhancedDamage"), a, 1);
        nl = add_line(l, nl, 48, s_isc[17].priority, buf);
        used[ed] = used[edmin] = 1;
    }
    /* Los rangos: daño, fuego, rayo, mágico y frío */
    static const struct { int lo, hi; const char *range, *single; } k_ranges[5] = {
        { 21, 22, "strModMinDamageRange", "strModMinDamage" },
        { 48, 49, "strModFireDamageRange", "strModFireDamage" },
        { 50, 51, "strModLightningDamageRange", "strModLightningDamage" },
        { 52, 53, "strModMagicDamageRange", "strModMagicDamage" },
        { 54, 55, "strModColdDamageRange", "strModColdDamage" },
    };
    for (int r = 0; r < 5; r++) {
        int lo = find_stat(p, n, k_ranges[r].lo), hi = find_stat(p, n, k_ranges[r].hi);
        if (lo < 0 || hi < 0)
            continue;
        a[0].s = a[1].s = NULL;
        a[0].i = p[lo].value;
        a[1].i = p[hi].value;
        if (p[lo].value == p[hi].value)
            itemstats_format(buf, sizeof buf, str_get(k_ranges[r].single), a, 1);
        else
            itemstats_format(buf, sizeof buf, str_get(k_ranges[r].range), a, 2);
        nl = add_line(l, nl, 48, s_isc[k_ranges[r].lo].priority, buf);
        used[lo] = used[hi] = 1;
    }
    /* Veneno: el total en los segundos que dura */
    int pmin = find_stat(p, n, 57), pmax = find_stat(p, n, 58), plen = find_stat(p, n, 59);
    if (pmin >= 0 && pmax >= 0) {
        int len = plen >= 0 ? p[plen].value : 50;
        a[0].s = a[1].s = NULL;
        a[0].i = (p[pmax].value * len + 128) / 256;
        a[1].i = (len + 12) / 25;
        itemstats_format(buf, sizeof buf, str_get("strModPoisonDamage"), a, 2);
        nl = add_line(l, nl, 48, s_isc[57].priority, buf);
        used[pmin] = used[pmax] = 1;
        if (plen >= 0)
            used[plen] = 1;
    }
    /* Grupos: todas las resistencias, todos los atributos */
    static const int k_groups[2][4] = { { 39, 41, 43, 45 }, { 0, 1, 2, 3 } };
    for (int g = 0; g < 2; g++) {
        int idx[4], same = 1;
        for (int k = 0; k < 4 && same; k++) {
            idx[k] = find_stat(p, n, k_groups[g][k]);
            same = idx[k] >= 0 && !used[idx[k]] && p[idx[k]].value == p[idx[0]].value;
        }
        if (!same)
            continue;
        a[0].s = NULL;
        a[0].i = p[idx[0]].value;
        itemstats_format(buf, sizeof buf, str_get(g == 0 ? "strModAllResistances" : "Moditem2allattrib"), a, 1);
        nl = add_line(l, nl, 48, s_isc[k_groups[g][0]].priority, buf);
        for (int k = 0; k < 4; k++)
            used[idx[k]] = 1;
    }

    for (int i = 0; i < n; i++) {
        if (used[i])
            continue;
        int st = p[i].stat, v = p[i].value, param = p[i].param;
        if (st >= ISC_MAX)
            continue;
        const IscDef *d = &s_isc[st];
        /* Las segundas (a dos manos, arrojándola) y los largos sueltos no se muestran */
        if (st == 23 || st == 24 || st == 159 || st == 160 || st == 56 || st == 59 || !d->descfunc)
            continue;
        memset(a, 0, sizeof a);
        buf[0] = '\0';
        const char *key = v < 0 && d->neg ? d->neg : d->pos;
        switch (d->descfunc) {
        case 13: { /* + a las habilidades de una clase */
            int c = param >= 0 && param < 8 ? param : 0;
            a[0].i = v;
            itemstats_format(buf, sizeof buf, str_get(k_class_skills_key[c]), a, 1);
            break;
        }
        case 14: { /* + a una solapa (solo esa clase) */
            int c = (param >> 3) & 7, t = param & 7;
            if (c > 7 || t > 2)
                break;
            char k2[24], tmp[96];
            snprintf(k2, sizeof k2, "StrSklTabItem%d", k_tab_str[c][t]);
            a[0].i = v;
            itemstats_format(tmp, sizeof tmp, str_get(k2), a, 1);
            snprintf(buf, sizeof buf, "%s %s", tmp, str_get(k_class_only_key[c]));
            break;
        }
        case 15: /* probabilidad de lanzar */
            a[0].i = v;
            a[1].i = param & 63;
            a[2].s = skill_name(param >> 6);
            itemstats_format(buf, sizeof buf, str_get(key), a, 3);
            break;
        case 16: /* aura */
            a[0].i = v;
            a[1].s = skill_name(param);
            itemstats_format(buf, sizeof buf, str_get(key), a, 2);
            break;
        case 24: /* cargas */
            a[0].i = param & 63;
            a[1].s = skill_name(param >> 6);
            a[2].i = v & 0xFF;
            a[3].i = (v >> 8) & 0xFF;
            itemstats_format(buf, sizeof buf, str_get(key), a, 4);
            break;
        case 27: { /* + a una habilidad (solo esa clase) */
            int cls = skills_global_class(param), d2 = itemstats_class_to_d2(cls);
            a[0].i = v;
            a[1].s = skill_name(param);
            a[2].s = d2 >= 0 ? str_get(k_class_only_key[d2]) : "";
            itemstats_format(buf, sizeof buf, str_get(key), a, 3);
            break;
        }
        case 28: /* + a una habilidad de cualquier clase */
            a[0].i = v;
            a[1].s = skill_name(param);
            itemstats_format(buf, sizeof buf, str_get(key), a, 2);
            break;
        case 12: /* congela, ciega: el texto solo (y +N si es más de 1) */
            if (v > 1)
                snprintf(buf, sizeof buf, "%s %+d", str_get(key), v);
            else
                snprintf(buf, sizeof buf, "%s", str_get(key));
            break;
        case 5: /* hace huir: el valor en 1/128 */
            a[0].i = v * 100 / 128;
            itemstats_format(buf, sizeof buf, str_get(key), a, 1);
            break;
        case 11: /* repara 1 de durabilidad cada 100 / valor segundos */
            a[0].i = 1;
            a[0].s = NULL;
            a[1].i = v > 0 ? 100 / v : 1;
            a[1].s = NULL;
            itemstats_format(buf, sizeof buf, str_get("ModStre9u"), a, 2);
            break;
        case 22: /* contra un tipo de monstruo: no se usa en el Acto 1 */
        case 23:
            break;
        default: { /* 19 y las demás: el texto con el valor */
            if (d->op >= 2 && d->op <= 5 && d->str2) { /* según el nivel del personaje (en octavos) */
                a[0].i = (v * (clvl > 0 ? clvl : 1)) >> (d->op_param > 0 ? d->op_param : 3);
                char tmp[96];
                itemstats_format(tmp, sizeof tmp, str_get(key), a, 1);
                snprintf(buf, sizeof buf, "%s %s", tmp, str_get(d->str2));
                break;
            }
            a[0].i = v;
            if (st == 152 || st == 153 || st == 115 || st == 108) /* sin número */
                snprintf(buf, sizeof buf, "%s", str_get(key));
            else
                itemstats_format(buf, sizeof buf, str_get(key), a, 1);
            break;
        }
        }
        nl = add_line(l, nl, 48, d->priority, buf);
    }
    /* Como el original: de mayor a menor descpriority */
    for (int i = 1; i < nl; i++)
        for (int j = i; j > 0 && l[j].priority > l[j - 1].priority; j--) {
            Line t = l[j];
            l[j] = l[j - 1];
            l[j - 1] = t;
        }
    int out = 0;
    for (int i = 0; i < nl && out < max_lines; i++)
        snprintf(lines[out++], 112, "%s", l[i].text);
    return out;
}

int itemstats_per_level(int stat, int value, int clvl, int *amount)
{
    if (stat < 0 || stat >= ISC_MAX)
        return -1;
    const IscDef *d = &s_isc[stat];
    if (d->op < 2 || d->op > 5 || d->op_target < 0)
        return -1;
    *amount = (value * (clvl > 0 ? clvl : 1)) >> (d->op_param > 0 ? d->op_param : 0);
    if (d->op == 5) /* un % de la defensa o del daño */
        return d->op_target == 31 ? 16 : d->op_target == 22 ? 17 : -1;
    return d->op_target;
}
