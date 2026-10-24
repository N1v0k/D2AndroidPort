#include "cube.h"

#include "fmt/formats.h"
#include "fmt/fs.h"
#include "strings.h"
#include "lang.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lo que pide cada entrada: un objeto ("gcv"), un tipo ("hpot", "weap"),
   cualquiera ("any") o un único por su nombre ("The Stone of Jordan"), con
   la cantidad y la calidad ("mag", "rar", "nos"...) */
enum {
    IN_MAG = 1 << 0, IN_RAR = 1 << 1, IN_SET = 1 << 2, IN_UNI = 1 << 3, IN_CRF = 1 << 4, IN_NOR = 1 << 5,
    IN_LOW = 1 << 6, IN_HIQ = 1 << 7, IN_NOS = 1 << 8, IN_SOCK = 1 << 9, IN_BAS = 1 << 10, IN_EXC = 1 << 11,
    IN_ELI = 1 << 12, IN_ETH = 1 << 13,
};

typedef struct {
    char     code[48];
    int      qty;
    unsigned flags;
} CubeIn;

enum {
    OUT_MAG = 1 << 0, OUT_RAR = 1 << 1, OUT_CRF = 1 << 2, OUT_NOR = 1 << 3, OUT_UNS = 1 << 4, OUT_EXC = 1 << 5,
    OUT_ELI = 1 << 6, OUT_REP = 1 << 7,
};

typedef struct {
    char code[24];
    char par[24];
    int  chance, min, max;
} CubeMod;

typedef struct {
    char     code[48]; /* objeto, tipo, "usetype" (uno nuevo del tipo del primero) o "useitem" (el primero, cambiado) */
    unsigned flags;
    int      pre, suf, sock, qty;
    int      lvl, plvl, ilvl;
    int      nmods;
    CubeMod  mod[5];
} CubeOut;

#define MAX_RECIPES 200
typedef struct {
    char    desc[112];
    int     cat;        /* CUBE_CAT_*: dónde va en la guía */
    int     portal;     /* solo para la guía: el portal a las vacas (lo abre game.c, cows.inc) */
    int     nin, total; /* entradas y cuántos objetos en total */
    CubeIn  in[7];
    int     nout;
    CubeOut out[3];
} Recipe;

static Recipe s_rec[MAX_RECIPES];
static int    s_nrec;
static uint32_t s_rng = 0xC0BE;

static int rnd_range(int lo, int hi)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return hi > lo ? lo + (int)((s_rng >> 8) % (uint32_t)(hi - lo + 1)) : lo;
}

/* Las celdas vienen entre comillas cuando tienen comas */
static void unquote(char *dst, size_t cap, const char *src)
{
    while (*src == '"' || *src == ' ')
        src++;
    snprintf(dst, cap, "%s", src);
    size_t n = strlen(dst);
    while (n > 0 && (dst[n - 1] == '"' || dst[n - 1] == ' '))
        dst[--n] = '\0';
}

/* ¿El código es un objeto, un tipo o un único que conocemos? */
static int code_known(const char *code)
{
    if (!strcmp(code, "any") || items_find(code) || items_itype_exists(code))
        return 1;
    return 0;
}

static int parse_in(CubeIn *c, const char *cell)
{
    char buf[96];
    unquote(buf, sizeof buf, cell);
    memset(c, 0, sizeof *c);
    c->qty = 1;
    char *tok = strtok(buf, ",");
    if (!tok)
        return 0;
    snprintf(c->code, sizeof c->code, "%s", tok);
    while ((tok = strtok(NULL, ",")) != NULL) {
        static const struct { const char *name; unsigned flag; } k_flags[] = {
            { "mag", IN_MAG }, { "rar", IN_RAR }, { "set", IN_SET }, { "uni", IN_UNI }, { "crf", IN_CRF },
            { "nor", IN_NOR }, { "low", IN_LOW }, { "hiq", IN_HIQ }, { "nos", IN_NOS }, { "sock", IN_SOCK },
            { "bas", IN_BAS }, { "exc", IN_EXC }, { "eli", IN_ELI }, { "eth", IN_ETH },
        };
        if (!strncmp(tok, "qty=", 4)) {
            c->qty = atoi(tok + 4) > 0 ? atoi(tok + 4) : 1;
            continue;
        }
        for (unsigned k = 0; k < sizeof k_flags / sizeof k_flags[0]; k++)
            if (!strcmp(tok, k_flags[k].name))
                c->flags |= k_flags[k].flag;
        /* noe, upg, nru: no hay etéreos ni nada que mejorar: no cambian nada */
    }
    return 1;
}

static int parse_out(CubeOut *o, const char *cell)
{
    char buf[96];
    unquote(buf, sizeof buf, cell);
    memset(o, 0, sizeof *o);
    char *tok = strtok(buf, ",");
    if (!tok)
        return 0;
    snprintf(o->code, sizeof o->code, "%s", tok);
    while ((tok = strtok(NULL, ",")) != NULL) {
        if (!strcmp(tok, "mag")) o->flags |= OUT_MAG;
        else if (!strcmp(tok, "rar")) o->flags |= OUT_RAR;
        else if (!strcmp(tok, "crf")) o->flags |= OUT_CRF;
        else if (!strcmp(tok, "nor")) o->flags |= OUT_NOR;
        else if (!strcmp(tok, "uns")) o->flags |= OUT_UNS;
        else if (!strcmp(tok, "exc")) o->flags |= OUT_EXC;
        else if (!strcmp(tok, "eli")) o->flags |= OUT_ELI;
        else if (!strcmp(tok, "rep") || !strcmp(tok, "rch")) o->flags |= OUT_REP;
        else if (!strncmp(tok, "pre=", 4)) o->pre = atoi(tok + 4);
        else if (!strncmp(tok, "suf=", 4)) o->suf = atoi(tok + 4);
        else if (!strncmp(tok, "sock=", 5)) o->sock = atoi(tok + 5);
        else if (!strncmp(tok, "qty=", 4)) o->qty = atoi(tok + 4);
    }
    return 1;
}

static void read_mods(CubeOut *o, const TxtTable *t, int r, const char *prefix)
{
    for (int k = 1; k <= 5; k++) {
        char c[24], ch[32], p[32], mn[32], mx[32];
        snprintf(c, sizeof c, "%smod %d", prefix, k);
        snprintf(ch, sizeof ch, "%smod %d chance", prefix, k);
        snprintf(p, sizeof p, "%smod %d param", prefix, k);
        snprintf(mn, sizeof mn, "%smod %d min", prefix, k);
        snprintf(mx, sizeof mx, "%smod %d max", prefix, k);
        const char *code = txt_get(t, r, txt_col(t, c));
        if (!code[0])
            continue;
        CubeMod *m = &o->mod[o->nmods++];
        snprintf(m->code, sizeof m->code, "%s", code);
        snprintf(m->par, sizeof m->par, "%s", txt_get(t, r, txt_col(t, p)));
        m->chance = txt_get_int(t, r, ch, 0);
        m->min = txt_get_int(t, r, mn, 0);
        m->max = txt_get_int(t, r, mx, m->min);
    }
}

/* La categoría de la guía: por lo que da */
static int recipe_category(const Recipe *rc)
{
    const CubeOut *o = &rc->out[0];
    const ItemType *t = items_find(o->code);
    int sock = o->sock > 0 || (o->flags & OUT_UNS);
    for (int k = 0; k < o->nmods; k++)
        sock |= !strcmp(o->mod[k].code, "sock") && strcmp(o->code, "usetype") != 0;
    if (o->flags & OUT_CRF)
        return CUBE_CAT_CRAFT;
    if (sock)
        return CUBE_CAT_SOCKETS;
    if (o->flags & (OUT_EXC | OUT_ELI) || (!strcmp(o->code, "usetype") && (o->flags & OUT_NOR)))
        return CUBE_CAT_UPGRADE;
    if (t && items_type_is(t, "rune"))
        return CUBE_CAT_RUNES;
    if (t && items_type_is(t, "gem"))
        return CUBE_CAT_GEMS;
    if (t && (items_is_potion(t) || items_type_is(t, "poti")))
        return CUBE_CAT_POTIONS;
    if (o->flags & (OUT_MAG | OUT_RAR))
        return CUBE_CAT_MAGIC;
    return CUBE_CAT_OTHER;
}

void cube_reset_rng(uint32_t seed)
{
    s_rng = 0xC0BEu ^ (seed * 668265263u);
}

int cube_init(void)
{
    TxtTable t = { 0 };
    size_t n;
    uint8_t *d = fs_read("data/global/excel/cubemain.txt", &n);
    int r0 = d ? txt_load(d, n, &t) : -1;
    free(d);
    s_nrec = 0;
    if (r0 != 0) {
        fprintf(stderr, "falta cubemain.txt\n");
        return 0;
    }
    for (int r = 1; r < t.rows && s_nrec < MAX_RECIPES; r++) {
        /* La del portal a las vacas, solo para la guía (game.c la hace): el port
           no tiene el Tomo de Portal, así que lleva los pergaminos */
        if (strcmp(txt_get(&t, r, txt_col(&t, "output")), "Cow Portal") == 0 && txt_get_int(&t, r, "enabled", 0) == 1) {
            Recipe rc;
            memset(&rc, 0, sizeof rc);
            snprintf(rc.desc, sizeof rc.desc, "%s", txt_get(&t, r, txt_col(&t, "description")));
            rc.cat = CUBE_CAT_OTHER;
            rc.portal = 1;
            snprintf(rc.in[0].code, sizeof rc.in[0].code, "leg");
            snprintf(rc.in[1].code, sizeof rc.in[1].code, "tsc");
            rc.in[0].qty = rc.in[1].qty = 1;
            rc.nin = rc.total = 2;
            rc.nout = 1;
            s_rec[s_nrec++] = rc;
            continue;
        }
        /* Las de siempre, en Normal, sin condiciones (las de los portales y las misiones tienen op;
           de esas, solo el Báculo Horádrico: su op es que la misión no esté cumplida) */
        int staff = strcmp(txt_get(&t, r, txt_col(&t, "output")), "hst") == 0 ||
                    strcmp(txt_get(&t, r, txt_col(&t, "output")), "qf2") == 0; /* (y la Voluntad de Khalim) */
        if (txt_get_int(&t, r, "enabled", 0) != 1 || (txt_get_int(&t, r, "op", 0) != 0 && !staff) ||
            txt_get_int(&t, r, "min diff", 0) > 0 || txt_get(&t, r, txt_col(&t, "firstLadderSeason"))[0] ||
            txt_get(&t, r, txt_col(&t, "lastLadderSeason"))[0])
            continue;
        Recipe rc;
        memset(&rc, 0, sizeof rc);
        snprintf(rc.desc, sizeof rc.desc, "%s", txt_get(&t, r, txt_col(&t, "description")));
        int ok = 1;
        for (int k = 1; k <= 7 && ok; k++) {
            char col[12];
            snprintf(col, sizeof col, "input %d", k);
            const char *cell = txt_get(&t, r, txt_col(&t, col));
            if (!cell[0])
                continue;
            CubeIn *c = &rc.in[rc.nin];
            if (!parse_in(c, cell))
                continue;
            /* un único por su nombre también sirve ("The Stone of Jordan") */
            ok = code_known(c->code) || strchr(c->code, ' ') != NULL;
            rc.total += c->qty;
            rc.nin++;
        }
        static const char *const k_out[3][2] = { { "output", "" }, { "output b", "b " }, { "output c", "c " } };
        for (int k = 0; k < 3 && ok; k++) {
            const char *cell = txt_get(&t, r, txt_col(&t, k_out[k][0]));
            if (!cell[0])
                continue;
            CubeOut *o = &rc.out[rc.nout];
            parse_out(o, cell);
            if (strcmp(o->code, "usetype") && strcmp(o->code, "useitem") && !code_known(o->code)) {
                ok = k > 0; /* un portal o algo que no es un objeto: la receta no va */
                continue;
            }
            char lc[12], pc[12], ic[12];
            snprintf(lc, sizeof lc, "%slvl", k_out[k][1]);
            snprintf(pc, sizeof pc, "%splvl", k_out[k][1]);
            snprintf(ic, sizeof ic, "%silvl", k_out[k][1]);
            o->lvl = txt_get_int(&t, r, lc, 0);
            o->plvl = txt_get_int(&t, r, pc, 0);
            o->ilvl = txt_get_int(&t, r, ic, 0);
            read_mods(o, &t, r, k_out[k][1]);
            rc.nout++;
        }
        if (ok && rc.nin > 0 && rc.nout > 0) {
            rc.cat = recipe_category(&rc);
            s_rec[s_nrec++] = rc;
        }
    }
    txt_free(&t);
    return s_nrec;
}

/* ---- Transmutar ---- */

static int in_matches(const CubeIn *c, const Item *it)
{
    const ItemType *t = it->type;
    if (!t)
        return 0;
    if (!strcmp(c->code, "any")) {
        if (strcmp(t->code, "gld") == 0 || strcmp(t->code, "box") == 0)
            return 0;
    } else if (items_itype_exists(c->code)) { /* un tipo ("axe" es el tipo, no el Hacha) */
        if (!items_type_is(t, c->code))
            return 0;
    } else if (items_find(c->code)) {
        if (strcmp(t->code, c->code) != 0)
            return 0;
    } else if (strcmp(items_unique_key(it), c->code) != 0) {
        return 0;
    }
    unsigned f = c->flags;
    int q = it->quality;
    if ((f & IN_MAG) && q != QUALITY_MAGIC) return 0;
    if ((f & IN_RAR) && q != QUALITY_RARE) return 0;
    if ((f & IN_SET) && q != QUALITY_SET) return 0;
    if ((f & IN_UNI) && q != QUALITY_UNIQUE) return 0;
    if ((f & IN_CRF) && q != QUALITY_CRAFTED) return 0;
    if ((f & IN_NOR) && q != QUALITY_NORMAL) return 0;
    if ((f & IN_LOW) && q != QUALITY_LOW) return 0;
    if ((f & IN_HIQ) && q != QUALITY_SUPERIOR) return 0;
    if ((f & IN_NOS) && it->sockets > 0) return 0;
    if ((f & IN_SOCK) && it->sockets == 0) return 0;
    if ((f & IN_BAS) && t->tier != 0) return 0;
    if ((f & IN_EXC) && t->tier != 1) return 0;
    if ((f & IN_ELI) && t->tier != 2) return 0;
    if (f & IN_ETH) return 0; /* no hay etéreos */
    return 1;
}

/* Reparte los objetos entre las entradas (cada una con su cantidad): todos
   tienen que ir a alguna. first: el primero que tomó la entrada 0. */
static int assign(const Recipe *rc, int spec, int got, const Item *in, int n, uint8_t *used, int *first)
{
    if (spec == rc->nin)
        return 1;
    const CubeIn *c = &rc->in[spec];
    if (got == c->qty)
        return assign(rc, spec + 1, 0, in, n, used, first);
    for (int i = 0; i < n; i++) {
        if (used[i] || !in_matches(c, &in[i]))
            continue;
        used[i] = 1;
        if (spec == 0 && got == 0)
            *first = i;
        if (assign(rc, spec, got + 1, in, n, used, first))
            return 1;
        used[i] = 0;
    }
    return 0;
}

/* Un objeto al azar de ese tipo, de nivel <= lvl ("pole": una de las armas de asta) */
static const ItemType *random_of_type(const char *type, int lvl)
{
    int total = 0;
    for (int i = 0; i < items_type_count(); i++) {
        const ItemType *t = items_type(i);
        if (t->spawnable && t->level <= lvl && items_supported(t) && items_type_is(t, type))
            total++;
    }
    if (total == 0)
        return NULL;
    int pick = rnd_range(0, total - 1);
    for (int i = 0; i < items_type_count(); i++) {
        const ItemType *t = items_type(i);
        if (t->spawnable && t->level <= lvl && items_supported(t) && items_type_is(t, type) && pick-- == 0)
            return t;
    }
    return NULL;
}

static int make_output(const CubeOut *o, const Item *first, int clvl, Item *out)
{
    int ilvl = o->lvl + o->plvl * clvl / 100 + (first ? o->ilvl * first->ilvl / 100 : 0);
    if (ilvl < 1)
        ilvl = first && first->ilvl > 0 ? first->ilvl : clvl;
    if (ilvl > 99)
        ilvl = 99;
    Item it;
    if (!strcmp(o->code, "useitem")) {
        if (!first)
            return 0;
        it = *first;
        if (o->flags & OUT_UNS) { /* vaciar los engarces: vuelve a ser el objeto blanco */
            if (it.quality != QUALITY_NORMAL && it.quality != QUALITY_SUPERIOR && it.quality != QUALITY_RUNEWORD)
                return 0; /* lo de las gemas ya está mezclado con lo suyo: no se puede separar */
            it.num_stats = 0;
            it.nsocketed = 0;
            it.quality = QUALITY_NORMAL;
            it.unique = -1;
            it.name[0] = '\0';
        }
        if ((o->flags & (OUT_EXC | OUT_ELI)) && !items_upgrade(&it, (o->flags & OUT_ELI) != 0))
            return 0;
        if (o->sock > 0) { /* "agrega un engarce" */
            if (it.sockets >= ITEM_MAX_SOCKETS)
                return 0;
            it.sockets = (uint8_t)(it.sockets + o->sock);
        }
        if ((o->flags & OUT_REP) && it.type->stackable && o->qty > 0) /* reparar: las arrojadizas se llenan */
            it.amount = it.type->max_stack;
    } else {
        const ItemType *t;
        if (!strcmp(o->code, "usetype"))
            t = first ? first->type : NULL;
        else if (items_itype_exists(o->code) && !items_find(o->code))
            t = random_of_type(o->code, ilvl);
        else
            t = items_find(o->code);
        if (!t)
            return 0;
        it = items_make(t);
        it.ilvl = (uint8_t)ilvl;
        items_quest_unique(&it);
        if (o->qty > 0 && t->stackable)
            it.amount = o->qty > t->max_stack ? t->max_stack : o->qty;
        if (o->flags & OUT_MAG)
            items_make_magic_affix(&it, ilvl, o->pre, o->suf);
        else if (o->flags & OUT_RAR)
            items_make_rare(&it, ilvl);
        else if (o->flags & OUT_CRF)
            items_make_crafted(&it, ilvl);
    }
    for (int k = 0; k < o->nmods; k++) {
        const CubeMod *m = &o->mod[k];
        if (m->chance > 0 && rnd_range(1, 100) > m->chance)
            continue;
        if (!strcmp(m->code, "sock")) { /* engarces: entre min y max */
            int s = rnd_range(m->min > 0 ? m->min : 1, m->max > m->min ? m->max : m->min > 0 ? m->min : 1);
            int most = it.type->gemsockets > 0 ? it.type->gemsockets : 1;
            it.sockets = (uint8_t)(s > most ? most : s);
        } else if (strcmp(m->code, "levelreq") != 0) {
            items_apply_prop(&it, m->code, m->par, m->min, m->max);
        }
    }
    *out = it;
    return 1;
}

int cube_transmute(const Item *in, int n, int clvl, Item *out, int max, char *what, size_t cap)
{
    if (what && cap)
        what[0] = '\0';
    for (int r = 0; r < s_nrec; r++) {
        const Recipe *rc = &s_rec[r];
        if (rc->portal || rc->total != n)
            continue;
        uint8_t used[CUBE_W * CUBE_H * 2] = { 0 };
        int first = -1;
        if (n > (int)sizeof used || !assign(rc, 0, 0, in, n, used, &first))
            continue;
        int count = 0;
        for (int k = 0; k < rc->nout && count < max; k++)
            if (make_output(&rc->out[k], first >= 0 ? &in[first] : NULL, clvl, &out[count]))
                count++;
        if (count == 0)
            continue; /* esa no sirvió (un objeto que no se puede cambiar): otra */
        if (what)
            snprintf(what, cap, "%s", rc->desc);
        return count;
    }
    return 0;
}

/* ---- La guía ---- */

const char *cube_category_name(int cat)
{
    static const char *const k_names[CUBE_CATS] = { "Cómo se usa", "Gemas",   "Runas",     "Palabras rúnicas",
                                                    "Pociones",    "Engarces", "Mágicos",   "Artesanía",
                                                    "Mejoras",     "Varios" };
    return cat >= 0 && cat < CUBE_CATS ? k_names[cat] : "";
}

int cube_recipe_count(void)
{
    return s_nrec;
}

int cube_recipe_category(int i)
{
    return i >= 0 && i < s_nrec ? s_rec[i].cat : -1;
}

/* Los tipos que piden las recetas, en palabras */
static const char *type_text(const char *code)
{
    static const struct { const char *code, *text; } k_types[] = {
        { "any", "Cualquier objeto" },
        { "hpot", "Poción de vida (cualquiera)" },
        { "mpot", "Poción de maná (cualquiera)" },
        { "gem0", "Gema mellada (cualquiera)" },
        { "gem1", "Gema imperfecta (cualquiera)" },
        { "gem2", "Gema común (cualquiera)" },
        { "gem3", "Gema impecable (cualquiera)" },
        { "gem4", "Gema perfecta (cualquiera)" },
        { "gemr", "Rubí (cualquiera)" },
        { "gemd", "Diamante (cualquiera)" },
        { "gemz", "Cráneo (cualquiera)" },
        { "weap", "Arma" },
        { "armo", "Pieza de armadura" },
        { "tors", "Armadura de cuerpo" },
        { "helm", "Casco" },
        { "shld", "Escudo" },
        { "axe", "Hacha (cualquiera)" },
        { "knif", "Daga (cualquiera)" },
        { "spea", "Lanza (cualquiera)" },
        { "swor", "Espada" },
        { "staf", "Báculo" },
        { "belt", "Cinturón" },
        { "pole", "Arma de asta" },
        { "blun", "Arma contundente" },
        { "rod", "Vara" },
        { "ring", "Anillo" },
        { "amul", "Amuleto" },
    };
    for (unsigned k = 0; k < sizeof k_types / sizeof k_types[0]; k++)
        if (!strcmp(k_types[k].code, code))
            return k_types[k].text;
    return NULL;
}

/* El objeto que se dibuja para un código: ese objeto, o el primero de ese tipo */
static const ItemType *icon_of(const char *code)
{
    if (!strcmp(code, "any"))
        return NULL;
    if (!items_itype_exists(code)) {
        const ItemType *t = items_find(code);
        if (t)
            return t;
    }
    for (int i = 0; i < items_type_count(); i++) {
        const ItemType *t = items_type(i);
        if (items_supported(t) && items_type_is(t, code))
            return t;
    }
    return NULL;
}

static const char *code_text(const char *code)
{
    const char *tt = items_itype_exists(code) ? type_text(code) : NULL;
    if (tt)
        return tt;
    const ItemType *t = items_find(code);
    if (t)
        return t->name;
    tt = type_text(code);
    return tt ? tt : str_get(code); /* un único por su nombre ("The Stone of Jordan") */
}

static void add_text(char *buf, size_t cap, const char *more)
{
    size_t n = strlen(buf);
    if (n < cap)
        snprintf(buf + n, cap - n, "%s", tr(more));
}

int cube_recipe_info(int i, CubeLine *in, int max_in, CubeLine *out)
{
    if (i < 0 || i >= s_nrec)
        return 0;
    const Recipe *rc = &s_rec[i];
    int n = 0;
    for (int k = 0; k < rc->nin && n < max_in; k++, n++) {
        const CubeIn *c = &rc->in[k];
        CubeLine *l = &in[n];
        l->icon = icon_of(c->code);
        l->qty = c->qty;
        snprintf(l->text, sizeof l->text, "%s", code_text(c->code));
        unsigned f = c->flags;
        if (f & IN_MAG) add_text(l->text, sizeof l->text, " mágico");
        if (f & IN_RAR) add_text(l->text, sizeof l->text, " raro");
        if (f & IN_SET) add_text(l->text, sizeof l->text, " de conjunto");
        if (f & IN_UNI) add_text(l->text, sizeof l->text, " único");
        if (f & IN_CRF) add_text(l->text, sizeof l->text, " artesanal");
        if (f & IN_NOR) add_text(l->text, sizeof l->text, " común (blanco)");
        if (f & IN_LOW) add_text(l->text, sizeof l->text, " de baja calidad");
        if (f & IN_HIQ) add_text(l->text, sizeof l->text, " superior");
        if (f & IN_BAS) add_text(l->text, sizeof l->text, " (versión normal)");
        if (f & IN_EXC) add_text(l->text, sizeof l->text, " (versión excepcional)");
        if (f & IN_ELI) add_text(l->text, sizeof l->text, " (versión de élite)");
        if (f & IN_NOS) add_text(l->text, sizeof l->text, ", sin engarces");
        if (f & IN_SOCK) add_text(l->text, sizeof l->text, ", con engarces");
    }
    const CubeOut *o = &rc->out[0];
    memset(out, 0, sizeof *out);
    out->qty = 1;
    if (rc->portal) {
        snprintf(out->text, sizeof out->text, "%s", tr("Portal al nivel de las vacas (en el campamento, muerto Baal)"));
        return n;
    }
    int use_first = !strcmp(o->code, "usetype") || !strcmp(o->code, "useitem");
    out->icon = use_first ? icon_of(rc->in[0].code) : icon_of(o->code);
    if (!strcmp(o->code, "useitem"))
        snprintf(out->text, sizeof out->text, "%s", tr("El mismo objeto"));
    else if (!strcmp(o->code, "usetype")) /* uno nuevo como el primero ("Yelmo completo", "Arma") */
        snprintf(out->text, sizeof out->text, "%s", strcmp(rc->in[0].code, "any") ? code_text(rc->in[0].code) : tr("Otro igual"));
    else
        snprintf(out->text, sizeof out->text, "%s", code_text(o->code));
    if (o->flags & OUT_CRF) add_text(out->text, sizeof out->text, " artesanal");
    if (o->flags & OUT_MAG) add_text(out->text, sizeof out->text, " mágico");
    if (o->flags & OUT_RAR) add_text(out->text, sizeof out->text, " raro");
    if (o->flags & OUT_NOR) add_text(out->text, sizeof out->text, " común");
    if (o->flags & OUT_EXC) add_text(out->text, sizeof out->text, " en versión excepcional");
    if (o->flags & OUT_ELI) add_text(out->text, sizeof out->text, " en versión de élite");
    if (o->flags & OUT_UNS) add_text(out->text, sizeof out->text, " con los engarces vacíos");
    if (o->flags & OUT_REP) add_text(out->text, sizeof out->text, " reparado y recargado");
    if (o->pre > 0 && items_affix_name(0, o->pre)[0]) {
        add_text(out->text, sizeof out->text, " (");
        add_text(out->text, sizeof out->text, items_affix_name(0, o->pre));
        add_text(out->text, sizeof out->text, ")");
    }
    if (o->suf > 0 && items_affix_name(1, o->suf)[0]) {
        add_text(out->text, sizeof out->text, " (");
        add_text(out->text, sizeof out->text, items_affix_name(1, o->suf));
        add_text(out->text, sizeof out->text, ")");
    }
    if (o->sock > 0) {
        char more[32];
        snprintf(more, sizeof more, tr(o->sock > 1 ? " con %d engarces más" : " con %d engarce más"), o->sock);
        add_text(out->text, sizeof out->text, more);
    }
    for (int k = 0; k < o->nmods; k++) {
        static const struct { const char *code, *text; } k_craft[] = {
            { "sock", " con engarces" },
            { "gethit-skill", ": de golpe (Nova de escarcha al recibir golpes)" },
            { "lifesteal", ": de sangre (roba vida)" },
            { "regen-mana", ": de lanzador (regenera maná)" },
            { "red-dmg", ": de seguridad (reduce el daño)" },
        };
        for (unsigned j = 0; j < sizeof k_craft / sizeof k_craft[0]; j++)
            if (!strcmp(o->mod[k].code, k_craft[j].code))
                add_text(out->text, sizeof out->text, k_craft[j].text);
    }
    if (o->qty > 1 && o->qty < 10 && strcmp(o->code, "useitem") != 0)
        out->qty = o->qty;
    return n;
}
