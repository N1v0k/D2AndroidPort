#include "items.h"

#include "audio.h"
#include "fmt/formats.h"
#include "fmt/fs.h"
#include "font.h"
#include "jobs.h"
#include "strings.h"
#include "video.h"
#include "lang.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TYPES  1200
#define MAX_GROUND 2048 /* (con los cofres cae mucho más; lleno, se pisa el más viejo) */
#define MAX_GFX    640
#define POTION_DROP_CUT 35 /* % de las pociones del botín que no salen */
#define POTION_NECK     35 /* % de arriba del dibujo de una poción que es el cuello (sin líquido) */
#define POTION_EMPTY_LIGHT 9 /* qué tan oscuro queda lo que ya se tomó (tabla de luz) */

static uint8_t text_col(int c);
static void    rare_names_init(void);

typedef struct {
    Item     item;
    float    x, y;
    int      anim_acc;
    int      used;
    uint32_t born; /* cuándo cayó (lleno, se pisa el más viejo) */
    uint32_t since; /* el tick de items_ground_tick en que cayó (items_ground_age) */
} GroundItem;

/* Gráficos de objetos (animación de caída e inventario), cargados al usarlos */
typedef struct {
    char   name[16];
    Sprite sprite;
    int    state; /* 0 sin pedir, 2 cargándose, 1 listo, -1 no está (job_flag_get) */
} ItemGfx;

#define MAX_ITYPES 160
#define MAX_AFFIX  1600

/* itemtypes.txt: cada tipo puede ser a la vez otro más general
   ("axe" -> "mele" -> "weap") */
typedef struct {
    char code[5], equiv[2][5];
    char cls[4]; /* Class de itemtypes.txt: solo esa clase lo puede usar ("ama", "bar"...) */
    int  normal, magic, rare; /* Normal, Magic y Rare: siempre normal, al menos mágico, puede ser raro */
    int  max_sock[3], sock_lvl[2];
    int  var_inv;
    char invgfx[6][16];
} IType;

/* Una propiedad de las tablas: código de Properties.txt, parámetro, mínimo y máximo */
typedef struct {
    char code[24];
    char par[24];
    int  min, max;
} PropRow;

/* Prefijo o sufijo mágico (magicprefix.txt / magicsuffix.txt); suffix 2 = de
   fábrica (automagic.txt, que tiene las mismas columnas) */
typedef struct {
    const char *key; /* clave del nombre en item-nameaffixes */
    int         suffix, row; /* sufijo; su fila en la tabla (como la cuenta cubemain.txt) */
    int         level, maxlevel, freq, group, rare; /* rare: también en los raros (automagic.txt) */
    int         nmods;
    PropRow     mod[3];
    char        itype[7][5], etype[5][5];
    short       itype_ix[7], etype_ix[5]; /* esos tipos en s_itypes (itype_index): -1 vacío o desconocido */
} Affix;

/* Un único (uniqueitems.txt) */
typedef struct {
    char    key[48];  /* el nombre (la clave en item-names) */
    char    code[5];  /* el objeto base */
    int     lvl, lvl_req, rarity;
    int     tc_only;  /* spawnable vacío (Resurrected: las Joyas colosales, los Sunder): solo si un TC lo nombra */
    char    invfile[16];
    int     nprop;
    PropRow prop[12];
} UniqueDef;

/* Una pieza de un conjunto (setitems.txt) */
typedef struct {
    char    key[48], set_key[48];
    int     set;      /* su conjunto en s_sets */
    char    code[5];
    int     lvl, lvl_req, rarity;
    char    invfile[16];
    int     nprop;
    PropRow prop[9];
    PropRow aprop[5][2]; /* las verdes: con 2, 3, 4, 5 y 6 piezas puestas */
} SetItemDef;

/* Un conjunto (sets.txt) */
typedef struct {
    char    key[48];
    PropRow partial[4][2]; /* con 2, 3, 4 y 5 piezas */
    int     nfull;
    PropRow full[8];       /* con todas */
    int     nitems;
} SetDef;

/* Una palabra rúnica (runes.txt) */
typedef struct {
    char    key[24];  /* "Runeword1" */
    char    itype[6][5], etype[3][5];
    char    rune[6][5];
    int     nrunes;
    int     nprop;
    PropRow prop[7];
} RunewordDef;

/* Lo que da una gema o runa engarzada (gems.txt): en un arma, en un casco o
   armadura y en un escudo */
typedef struct {
    char    code[5];
    PropRow mod[3][3];
} GemDef;

/* ItemRatio.txt (las filas de la expansión): normal, excepcional/élite, de clase */
typedef struct {
    int unique, unique_div, unique_min;
    int rare, rare_div, rare_min;
    int set, set_div, set_min;
    int magic, magic_div, magic_min;
    int hiq, hiq_div, normal, normal_div;
} Ratio;

#define MAX_UNIQUES  520
#define MAX_SETITEMS 160
#define MAX_SETS     40
#define MAX_RUNEWORDS 200
#define MAX_GEMS     80

static ItemType    s_types[MAX_TYPES];
static int         s_num_types;
static IType       s_itypes[MAX_ITYPES];
static int         s_num_itypes;
/* De cada tipo, un bit por cada tipo que es (él y de los que desciende, lo
   que contesta type_is): los afijos lo preguntan miles de veces por objeto */
static uint8_t     s_itype_isa[MAX_ITYPES][(MAX_ITYPES + 7) / 8];
static Affix       s_affix[MAX_AFFIX];
static int         s_num_affix;
static UniqueDef   s_uniques[MAX_UNIQUES];
static int         s_num_uniques;
static SetItemDef  s_setitems[MAX_SETITEMS];
static int         s_num_setitems;
static SetDef      s_sets[MAX_SETS];
static int         s_num_sets;
static RunewordDef s_runewords[MAX_RUNEWORDS];
static int         s_num_runewords;
static GemDef      s_gems[MAX_GEMS];
static int         s_num_gems;
static Ratio       s_ratio[2][2]; /* [uber][de clase] */
static TxtTable    s_tc;
static GroundItem  s_ground[MAX_GROUND];
static uint32_t    s_ground_now; /* los ticks de items_ground_tick */
static int         s_ground_top; /* uno más que el último lugar usado: los recorridos llegan hasta acá */
static ItemGfx     s_gfx[MAX_GFX];
static int         s_num_gfx;
static uint32_t    s_rng = 0xC0FFEE;

static uint32_t rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

static int rnd_range(int lo, int hi)
{
    return hi > lo ? lo + (int)(rnd() % (uint32_t)(hi - lo + 1)) : lo;
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

static void copy_lower(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static ItemSlot armor_slot(const char *type)
{
    static const struct { const char *type; ItemSlot slot; } k_map[] = {
        { "tors", SLOT_BODY },   { "helm", SLOT_HELM },   { "phlm", SLOT_HELM },   { "pelt", SLOT_HELM },
        { "circ", SLOT_HELM },   { "shie", SLOT_SHIELD }, { "ashd", SLOT_SHIELD }, { "head", SLOT_SHIELD },
        { "glov", SLOT_GLOVES }, { "boot", SLOT_BOOTS },  { "belt", SLOT_BELT },  { "grim", SLOT_SHIELD },
    };
    for (unsigned i = 0; i < sizeof k_map / sizeof k_map[0]; i++)
        if (strcmp(type, k_map[i].type) == 0)
            return k_map[i].slot;
    return SLOT_NONE;
}

static void load_kind(const TxtTable *t, ItemKind kind)
{
    for (int r = 1; r < t->rows && s_num_types < MAX_TYPES; r++) {
        const char *code = txt_get(t, r, txt_col(t, "code"));
        if (!code[0] || strcmp(code, "xxx") == 0)
            continue;
        ItemType *it = &s_types[s_num_types++];
        memset(it, 0, sizeof *it);
        snprintf(it->code, sizeof it->code, "%s", code);
        it->kind = kind;
        const char *namestr = txt_get(t, r, txt_col(t, "namestr"));
        /* sin texto, str_get devuelve la clave: es de la tabla, que se suelta al terminar (una copia) */
        it->name = str_exists(namestr) ? str_get(namestr) : strdup(namestr);
        it->gender = str_gender(namestr);
        copy_lower(it->type, sizeof it->type, txt_get(t, r, txt_col(t, "type")));
        copy_lower(it->type2, sizeof it->type2, txt_get(t, r, txt_col(t, "type2")));
        it->level = txt_get_int(t, r, "level", 0);
        it->level_req = txt_get_int(t, r, "levelreq", 0);
        it->rarity = txt_get_int(t, r, "rarity", 1);
        it->spawnable = txt_get_int(t, r, "spawnable", 0);
        it->req_str = txt_get_int(t, r, "reqstr", 0);
        it->req_dex = txt_get_int(t, r, "reqdex", 0);
        it->inv_w = txt_get_int(t, r, "invwidth", 1);
        it->inv_h = txt_get_int(t, r, "invheight", 1);
        copy_lower(it->flippy, sizeof it->flippy, txt_get(t, r, txt_col(t, "flippyfile")));
        copy_lower(it->invfile, sizeof it->invfile, txt_get(t, r, txt_col(t, "invfile")));
        copy_lower(it->gfx, sizeof it->gfx, txt_get(t, r, txt_col(t, "alternategfx")));
        if (!it->gfx[0]) {
            memcpy(it->gfx, it->code, sizeof it->gfx - 1);
            it->gfx[sizeof it->gfx - 1] = '\0';
        }
        it->cost = txt_get_int(t, r, "cost", 1);
        if (txt_col(t, "BetterGem") >= 0) { /* misc.txt: la gema que sigue (el santuario de gema) */
            copy_lower(it->better_gem, sizeof it->better_gem, txt_get(t, r, txt_col(t, "BetterGem")));
            if (strcmp(it->better_gem, "non") == 0)
                it->better_gem[0] = '\0';
        }
        it->stackable = txt_get_int(t, r, "stackable", 0) == 1;
        it->max_stack = txt_get_int(t, r, "maxstack", 0);
        it->spawn_stack = txt_get_int(t, r, "spawnstack", it->max_stack);
        if (strcmp(code, "key") == 0) /* pedido del usuario: de a 20 por casilla (el original, 12) */
            it->max_stack = KEY_STACK;
        it->min_mis_dmg = txt_get_int(t, r, "minmisdam", 0);
        it->max_mis_dmg = txt_get_int(t, r, "maxmisdam", 0);
        it->str_bonus = txt_get_int(t, r, "StrBonus", 0);
        it->dex_bonus = txt_get_int(t, r, "DexBonus", 0);
        it->gemsockets = txt_col(t, "gemsockets") >= 0 ? txt_get_int(t, r, "gemsockets", 0) : 0;
        it->gamble_cost = txt_col(t, "gamble cost") >= 0 ? txt_get_int(t, r, "gamble cost", 0) : 0;
        it->auto_group = txt_col(t, "auto prefix") >= 0 ? txt_get_int(t, r, "auto prefix", 0) : 0;
        { /* excepcional o de élite: el código no es el normal */
            const char *norm = txt_col(t, "normcode") >= 0 ? txt_get(t, r, txt_col(t, "normcode")) : "";
            const char *ub = txt_col(t, "ubercode") >= 0 ? txt_get(t, r, txt_col(t, "ubercode")) : "";
            const char *ul = txt_col(t, "ultracode") >= 0 ? txt_get(t, r, txt_col(t, "ultracode")) : "";
            it->uber = norm[0] && strcmp(norm, code) != 0;
            it->tier = ul[0] && strcmp(ul, code) == 0 ? 2 : ub[0] && strcmp(ub, code) == 0 ? 1 : 0;
            snprintf(it->ubercode, sizeof it->ubercode, "%s", ub);
            snprintf(it->ultracode, sizeof it->ultracode, "%s", ul);
            const char *nu = txt_col(t, "NightmareUpgrade") >= 0 ? txt_get(t, r, txt_col(t, "NightmareUpgrade")) : "";
            const char *hu = txt_col(t, "HellUpgrade") >= 0 ? txt_get(t, r, txt_col(t, "HellUpgrade")) : "";
            snprintf(it->nm_upgrade, sizeof it->nm_upgrade, "%s", strcmp(nu, "xxx") ? nu : "");
            snprintf(it->hell_upgrade, sizeof it->hell_upgrade, "%s", strcmp(hu, "xxx") ? hu : "");
        }
        it->drop_sound = audio_find(txt_get(t, r, txt_col(t, "dropsound")));
        it->use_sound = audio_find(txt_get(t, r, txt_col(t, "usesound")));
        it->drop_frame = txt_get_int(t, r, "dropsfxframe", 0);
        audio_preload(it->drop_sound);
        static const char *const vendor_cols[VENDOR_COUNT][2] = {
            { "AkaraMin", "AkaraMax" }, { "CharsiMin", "CharsiMax" }, { "GheedMin", "GheedMax" },
            { "FaraMin", "FaraMax" }, { "LysanderMin", "LysanderMax" }, { "DrognanMin", "DrognanMax" },
            { "ElzixMin", "ElzixMax" }, { "OrmusMin", "OrmusMax" }, { "AlkorMin", "AlkorMax" },
            { "AshearaMin", "AshearaMax" }, { "HratliMin", "HratliMax" }, { "HalbuMin", "HalbuMax" },
            { "JamellaMin", "JamellaMax" }, { "MalahMin", "MalahMax" }, { "LarzukMin", "LarzukMax" },
            { "AnyaMin", "AnyaMax" },
        };
        for (int v = 0; v < VENDOR_COUNT; v++) {
            int lo = txt_get_int(t, r, vendor_cols[v][0], 0), hi = txt_get_int(t, r, vendor_cols[v][1], 0);
            it->vendor_min[v] = (int8_t)(lo < 0 ? 0 : lo > 20 ? 20 : lo);
            it->vendor_max[v] = (int8_t)(hi < lo ? it->vendor_min[v] : hi > 20 ? 20 : hi);
        }
        static const char *const part_cols[6] = { "rArm", "lArm", "Torso", "Legs", "rSPad", "lSPad" };
        for (int k = 0; k < 6; k++) {
            const char *v = kind == ITEM_ARMOR ? txt_get(t, r, txt_col(t, part_cols[k])) : "";
            it->body_parts[k] = (int8_t)(v[0] ? atoi(v) : -1);
        }

        if (kind == ITEM_WEAPON) {
            it->slot = SLOT_WEAPON;
            it->two_handed = txt_get_int(t, r, "2handed", 0) == 1;
            it->min_dmg = txt_get_int(t, r, "mindam", 0);
            it->max_dmg = txt_get_int(t, r, "maxdam", 0);
            if (it->max_dmg == 0) {
                it->min_dmg = txt_get_int(t, r, "2handmindam", 1);
                it->max_dmg = txt_get_int(t, r, "2handmaxdam", 2);
            }
            copy_lower(it->wclass, sizeof it->wclass,
                       txt_get(t, r, txt_col(t, it->two_handed ? "2handedwclass" : "wclass")));
            static const char *const k_hit_classes[] = { "", "hth", "1hss", "1hsl", "2hss", "2hsl", "1ht",
                                                         "2ht", "club", "staf", "bow", "xbow", "claw" };
            const char *hc = txt_get(t, r, txt_col(t, "hit class"));
            for (int k = 1; k < (int)(sizeof k_hit_classes / sizeof k_hit_classes[0]); k++)
                if (strcmp(hc, k_hit_classes[k]) == 0)
                    it->hit_class = k;
        } else if (kind == ITEM_ARMOR) {
            it->slot = armor_slot(it->type);
            it->min_ac = txt_get_int(t, r, "minac", 0);
            it->max_ac = txt_get_int(t, r, "maxac", 0);
            it->smite_min = txt_get_int(t, r, "mindam", 0); /* escudos: el daño de Castigar */
            it->smite_max = txt_get_int(t, r, "maxdam", 0);
            it->block = txt_get_int(t, r, "block", 0);
        } else if (strcmp(it->type, "bowq") == 0 || strcmp(it->type, "xboq") == 0) {
            /* Carcaj de flechas o de saetas: va en la mano del escudo */
            it->slot = SLOT_SHIELD;
            it->ammo = it->type[0] == 'b' ? 1 : 2;
        } else if (strcmp(it->type, "ring") == 0) {
            it->slot = SLOT_RING1;
        } else if (strcmp(it->type, "amul") == 0) {
            it->slot = SLOT_AMULET;
        } else {
            it->slot = SLOT_NONE;
            /* Pociones: "hpregen 30 en 192 ticks", "hitpoints 35 %", etc. */
            const char *stat1 = txt_get(t, r, txt_col(t, "stat1"));
            int calc1 = txt_get_int(t, r, "calc1", 0), len = txt_get_int(t, r, "len", 0);
            if (strcmp(stat1, "hpregen") == 0) {
                it->heal_life = calc1;
                it->heal_ticks = len > 0 ? len : 1;
            } else if (strcmp(stat1, "manarecovery") == 0) {
                it->heal_mana = calc1;
                it->heal_ticks = len > 0 ? len : 1;
            } else if (strcmp(stat1, "hitpoints") == 0) {
                it->heal_pct = calc1; /* rejuvenecimiento: % de vida y maná al instante */
            } else if (strcmp(stat1, "staminarecoverybonus") == 0) {
                it->stamina_ticks = len;
            } else if (strcmp(stat1, "poisonresist") == 0) {
                it->antidote_ticks = len;
            } else if (strcmp(stat1, "coldresist") == 0) {
                it->thaw_ticks = len;
            }
        }
    }
}

static void load_itypes(const TxtTable *t)
{
    s_num_itypes = 0;
    for (int r = 1; r < t->rows && s_num_itypes < MAX_ITYPES; r++) {
        const char *code = txt_get(t, r, txt_col(t, "Code"));
        if (!code[0])
            continue;
        IType *it = &s_itypes[s_num_itypes++];
        memset(it, 0, sizeof *it);
        copy_lower(it->code, sizeof it->code, code);
        copy_lower(it->equiv[0], sizeof it->equiv[0], txt_get(t, r, txt_col(t, "Equiv1")));
        copy_lower(it->equiv[1], sizeof it->equiv[1], txt_get(t, r, txt_col(t, "Equiv2")));
        copy_lower(it->cls, sizeof it->cls, txt_col(t, "Class") >= 0 ? txt_get(t, r, txt_col(t, "Class")) : "");
        it->normal = txt_get_int(t, r, "Normal", 0);
        it->magic = txt_get_int(t, r, "Magic", 0);
        it->rare = txt_get_int(t, r, "Rare", 0);
        it->max_sock[0] = txt_get_int(t, r, "MaxSockets1", 0);
        it->max_sock[1] = txt_get_int(t, r, "MaxSockets2", 0);
        it->max_sock[2] = txt_get_int(t, r, "MaxSockets3", 0);
        it->sock_lvl[0] = txt_get_int(t, r, "MaxSocketsLevelThreshold1", 25);
        it->sock_lvl[1] = txt_get_int(t, r, "MaxSocketsLevelThreshold2", 40);
        it->var_inv = txt_get_int(t, r, "VarInvGfx", 0);
        for (int k = 0; k < 6; k++) {
            char col[12];
            snprintf(col, sizeof col, "InvGfx%d", k + 1);
            copy_lower(it->invgfx[k], sizeof it->invgfx[k], txt_get(t, r, txt_col(t, col)));
        }
    }
}

static const IType *itype_of(const char *code)
{
    for (int i = 0; i < s_num_itypes; i++)
        if (strcmp(s_itypes[i].code, code) == 0)
            return &s_itypes[i];
    return NULL;
}

/* La clase que puede usar un tipo (o alguno del que desciende), "" = todas */
static const char *type_class(const char *type, int depth)
{
    if (!type[0] || depth > 8)
        return "";
    for (int i = 0; i < s_num_itypes; i++)
        if (strcmp(s_itypes[i].code, type) == 0) {
            if (s_itypes[i].cls[0])
                return s_itypes[i].cls;
            const char *c = type_class(s_itypes[i].equiv[0], depth + 1);
            return c[0] ? c : type_class(s_itypes[i].equiv[1], depth + 1);
        }
    return "";
}

const char *items_class(const ItemType *t)
{
    return t ? type_class(t->type, 0) : "";
}

/* ¿El tipo `type` es (o desciende de) `want`? */
static int type_is(const char *type, const char *want, int depth)
{
    if (!type[0] || depth > 8)
        return 0;
    if (strcmp(type, want) == 0)
        return 1;
    for (int i = 0; i < s_num_itypes; i++)
        if (strcmp(s_itypes[i].code, type) == 0)
            return type_is(s_itypes[i].equiv[0], want, depth + 1) || type_is(s_itypes[i].equiv[1], want, depth + 1);
    return 0;
}

/* El primer tipo con ese código (el que mira type_is), -1 si no hay */
static int itype_index(const char *code)
{
    for (int i = 0; i < s_num_itypes; i++)
        if (strcmp(s_itypes[i].code, code) == 0)
            return i;
    return -1;
}

/* Marca en s_itype_isa[root] los tipos que type_is encuentra desde `type`
   (por el mismo camino y hasta la misma profundidad) */
static void isa_mark(int root, const char *type, int depth)
{
    if (!type[0] || depth > 8)
        return;
    for (int j = 0; j < s_num_itypes; j++)
        if (strcmp(s_itypes[j].code, type) == 0)
            s_itype_isa[root][j / 8] |= (uint8_t)(1u << (j % 8));
    int i = itype_index(type);
    if (i >= 0) {
        isa_mark(root, s_itypes[i].equiv[0], depth + 1);
        isa_mark(root, s_itypes[i].equiv[1], depth + 1);
    }
}

static void build_isa(void)
{
    memset(s_itype_isa, 0, sizeof s_itype_isa);
    for (int i = 0; i < s_num_itypes; i++)
        isa_mark(i, s_itypes[i].code, 0);
}

/* type_is con los tipos ya buscados (itype_index); si alguno no está en
   itemtypes.txt, compara los textos como type_is */
static int type_is_ix(const char *type, int tix, const char *want, int wix)
{
    if (tix < 0 || wix < 0)
        return type_is(type, want, 0);
    return (s_itype_isa[tix][wix / 8] >> (wix % 8)) & 1;
}

int items_itype_exists(const char *type)
{
    return type && type[0] && itype_of(type) != NULL;
}

int items_type_is(const ItemType *t, const char *type)
{
    return t && type && type[0] && (type_is(t->type, type, 0) || type_is(t->type2, type, 0));
}

int items_ammo_for(const ItemType *weapon)
{
    if (!weapon)
        return 0;
    return items_type_is(weapon, "bow") ? 1 : items_type_is(weapon, "xbow") ? 2 : 0;
}

int items_can_throw(const ItemType *weapon)
{
    return weapon && weapon->stackable && weapon->max_mis_dmg > 0 && items_type_is(weapon, "thro");
}

int items_offhand_ok(const ItemType *weapon, const ItemType *offhand)
{
    /* el grimorio del Brujo también con un arma a dos manos (charstats.txt:
       TwoHandedOffHandRestrictItemType = grim; solo él puede llevarlos). Se ve
       solo el arma: las animaciones a dos manos no tienen la capa del escudo */
    return !weapon || !offhand || !weapon->two_handed || offhand->ammo || type_is(offhand->type, "grim", 0);
}

int items_slot_ok(const ItemType *t, int slot)
{
    return t && (t->slot == (ItemSlot)slot || (t->slot == SLOT_RING1 && slot == SLOT_RING2));
}

/* Lo que dice itemtypes.txt del tipo de cada objeto (y de los tipos de los que desciende) */
static void resolve_types(void)
{
    for (int i = 0; i < s_num_types; i++) {
        ItemType *t = &s_types[i];
        const IType *it = itype_of(t->type);
        if (!it)
            continue;
        t->always_normal = it->normal;
        t->always_magic = it->magic;
        t->can_rare = it->rare;
        memcpy(t->max_sock, it->max_sock, sizeof t->max_sock);
        memcpy(t->sock_lvl, it->sock_lvl, sizeof t->sock_lvl);
        t->var_inv = it->var_inv > 6 ? 6 : it->var_inv;
        memcpy(t->invgfx, it->invgfx, sizeof t->invgfx);
        t->class_only = type_class(t->type, 0)[0] != '\0';
        t->socket_filler = type_is(t->type, "sock", 0);
        t->charm = type_is(t->type, "char", 0);
    }
}

/* ---- Las tablas de las propiedades ---- */

static void prop_row(PropRow *p, const TxtTable *t, int r, const char *code_col, const char *par_col,
                     const char *min_col, const char *max_col)
{
    memset(p, 0, sizeof *p);
    snprintf(p->code, sizeof p->code, "%s", txt_get(t, r, txt_col(t, code_col)));
    snprintf(p->par, sizeof p->par, "%s", par_col ? txt_get(t, r, txt_col(t, par_col)) : "");
    p->min = txt_get_int(t, r, min_col, 0);
    p->max = txt_get_int(t, r, max_col, p->min);
}

/* Los afijos que aparecen en el juego (frecuencia > 0) */
static void load_affixes(const TxtTable *t, int suffix)
{
    for (int r = 1; r < t->rows && s_num_affix < MAX_AFFIX; r++) {
        /* los que no salen solos (frecuencia 0) quedan para las recetas del cubo */
        if (!txt_get(t, r, txt_col(t, "Name"))[0] || !txt_get(t, r, txt_col(t, "mod1code"))[0])
            continue;
        Affix a;
        memset(&a, 0, sizeof a);
        a.suffix = suffix;
        a.row = r - 1;
        a.level = txt_get_int(t, r, "level", 0);
        a.maxlevel = txt_get_int(t, r, "maxlevel", 0);
        a.freq = txt_get_int(t, r, "spawnable", 0) == 1 ? txt_get_int(t, r, "frequency", 0) : 0; /* 0: solo el cubo */
        a.group = txt_get_int(t, r, "group", 0);
        a.rare = txt_get_int(t, r, "rare", 0);
        for (int k = 1; k <= 3; k++) {
            char c[16], pc[16], mn[16], mx[16];
            snprintf(c, sizeof c, "mod%dcode", k);
            snprintf(pc, sizeof pc, "mod%dparam", k);
            snprintf(mn, sizeof mn, "mod%dmin", k);
            snprintf(mx, sizeof mx, "mod%dmax", k);
            PropRow p;
            prop_row(&p, t, r, c, pc, mn, mx);
            if (itemstats_has_property(p.code))
                a.mod[a.nmods++] = p;
        }
        if (a.nmods == 0)
            continue;
        for (int k = 0; k < 7; k++) {
            char col[8];
            snprintf(col, sizeof col, "itype%d", k + 1);
            copy_lower(a.itype[k], sizeof a.itype[k], txt_get(t, r, txt_col(t, col)));
            a.itype_ix[k] = (short)itype_index(a.itype[k]);
        }
        for (int k = 0; k < 5; k++) {
            char col[8];
            snprintf(col, sizeof col, "etype%d", k + 1);
            copy_lower(a.etype[k], sizeof a.etype[k], txt_get(t, r, txt_col(t, col)));
            a.etype_ix[k] = (short)itype_index(a.etype[k]);
        }
        a.key = strdup(txt_get(t, r, txt_col(t, "Name")));
        s_affix[s_num_affix++] = a;
    }
}

/* Solo los de siempre: no los de una temporada de la escalera */
static int ladder_only(const TxtTable *t, int r)
{
    return txt_col(t, "lastLadderSeason") >= 0 && txt_get(t, r, txt_col(t, "lastLadderSeason"))[0];
}

static void load_uniques(void)
{
    TxtTable t = { 0 };
    s_num_uniques = 0;
    if (load_table(&t, "uniqueitems") != 0)
        return;
    for (int r = 1; r < t.rows && s_num_uniques < MAX_UNIQUES; r++) {
        const char *code = txt_get(&t, r, txt_col(&t, "code"));
        int tc_only = txt_get(&t, r, txt_col(&t, "spawnable"))[0] == '\0';
        if (!code[0] || (!tc_only && txt_get_int(&t, r, "spawnable", 0) != 1) || txt_get_int(&t, r, "disabled", 0) ||
            ladder_only(&t, r) || !items_find(code))
            continue;
        UniqueDef *u = &s_uniques[s_num_uniques++];
        memset(u, 0, sizeof *u);
        u->tc_only = tc_only;
        snprintf(u->key, sizeof u->key, "%s", txt_get(&t, r, txt_col(&t, "index")));
        snprintf(u->code, sizeof u->code, "%s", code);
        u->lvl = txt_get_int(&t, r, "lvl", 1);
        u->lvl_req = txt_get_int(&t, r, "lvl req", 1);
        u->rarity = txt_get_int(&t, r, "rarity", 1);
        copy_lower(u->invfile, sizeof u->invfile, txt_get(&t, r, txt_col(&t, "invfile")));
        for (int k = 1; k <= 12; k++) {
            char c[8], p[8], mn[8], mx[8];
            snprintf(c, sizeof c, "prop%d", k);
            snprintf(p, sizeof p, "par%d", k);
            snprintf(mn, sizeof mn, "min%d", k);
            snprintf(mx, sizeof mx, "max%d", k);
            PropRow pr;
            prop_row(&pr, &t, r, c, p, mn, mx);
            if (itemstats_has_property(pr.code))
                u->prop[u->nprop++] = pr;
        }
    }
    txt_free(&t);
}

static void load_sets(void)
{
    TxtTable s = { 0 }, t = { 0 };
    s_num_sets = s_num_setitems = 0;
    if (load_table(&s, "sets") != 0 || load_table(&t, "setitems") != 0) {
        txt_free(&s);
        txt_free(&t);
        return;
    }
    for (int r = 1; r < s.rows && s_num_sets < MAX_SETS; r++) {
        const char *name = txt_get(&s, r, txt_col(&s, "index"));
        if (!name[0])
            continue;
        SetDef *d = &s_sets[s_num_sets++];
        memset(d, 0, sizeof *d);
        snprintf(d->key, sizeof d->key, "%s", txt_get(&s, r, txt_col(&s, "name")));
        if (!d->key[0])
            snprintf(d->key, sizeof d->key, "%s", name);
        for (int n = 2; n <= 5; n++)
            for (int ab = 0; ab < 2; ab++) {
                char c[12], p[12], mn[12], mx[12];
                snprintf(c, sizeof c, "PCode%d%c", n, 'a' + ab);
                snprintf(p, sizeof p, "PParam%d%c", n, 'a' + ab);
                snprintf(mn, sizeof mn, "PMin%d%c", n, 'a' + ab);
                snprintf(mx, sizeof mx, "PMax%d%c", n, 'a' + ab);
                prop_row(&d->partial[n - 2][ab], &s, r, c, p, mn, mx);
            }
        for (int k = 1; k <= 8; k++) {
            char c[8], p[8], mn[8], mx[8];
            snprintf(c, sizeof c, "FCode%d", k);
            snprintf(p, sizeof p, "FParam%d", k);
            snprintf(mn, sizeof mn, "FMin%d", k);
            snprintf(mx, sizeof mx, "FMax%d", k);
            PropRow pr;
            prop_row(&pr, &s, r, c, p, mn, mx);
            if (itemstats_has_property(pr.code))
                d->full[d->nfull++] = pr;
        }
    }
    for (int r = 1; r < t.rows && s_num_setitems < MAX_SETITEMS; r++) {
        const char *code = txt_get(&t, r, txt_col(&t, "item"));
        const char *set = txt_get(&t, r, txt_col(&t, "set"));
        if (!code[0] || !set[0] || txt_get_int(&t, r, "disabled", 0) || !items_find(code))
            continue;
        SetItemDef *d = &s_setitems[s_num_setitems++];
        memset(d, 0, sizeof *d);
        snprintf(d->key, sizeof d->key, "%s", txt_get(&t, r, txt_col(&t, "index")));
        snprintf(d->set_key, sizeof d->set_key, "%s", set);
        snprintf(d->code, sizeof d->code, "%s", code);
        d->set = -1;
        TxtTable *st = &s;
        for (int k = 0, sr = 1; sr < st->rows && d->set < 0; sr++)
            if (txt_get(st, sr, txt_col(st, "index"))[0] && (k++, strcmp(txt_get(st, sr, txt_col(st, "index")), set) == 0))
                d->set = k - 1;
        d->lvl = txt_get_int(&t, r, "lvl", 1);
        d->lvl_req = txt_get_int(&t, r, "lvl req", 1);
        d->rarity = txt_get_int(&t, r, "rarity", 1);
        copy_lower(d->invfile, sizeof d->invfile, txt_get(&t, r, txt_col(&t, "invfile")));
        for (int k = 1; k <= 9; k++) {
            char c[8], p[8], mn[8], mx[8];
            snprintf(c, sizeof c, "prop%d", k);
            snprintf(p, sizeof p, "par%d", k);
            snprintf(mn, sizeof mn, "min%d", k);
            snprintf(mx, sizeof mx, "max%d", k);
            PropRow pr;
            prop_row(&pr, &t, r, c, p, mn, mx);
            if (itemstats_has_property(pr.code))
                d->prop[d->nprop++] = pr;
        }
        for (int n = 1; n <= 5; n++)
            for (int ab = 0; ab < 2; ab++) {
                char c[12], p[12], mn[12], mx[12];
                snprintf(c, sizeof c, "aprop%d%c", n, 'a' + ab);
                snprintf(p, sizeof p, "apar%d%c", n, 'a' + ab);
                snprintf(mn, sizeof mn, "amin%d%c", n, 'a' + ab);
                snprintf(mx, sizeof mx, "amax%d%c", n, 'a' + ab);
                prop_row(&d->aprop[n - 1][ab], &t, r, c, p, mn, mx);
            }
        if (d->set >= 0)
            s_sets[d->set].nitems++;
    }
    txt_free(&s);
    txt_free(&t);
}

static void load_runewords(void)
{
    TxtTable t = { 0 };
    s_num_runewords = 0;
    if (load_table(&t, "runes") != 0)
        return;
    for (int r = 1; r < t.rows && s_num_runewords < MAX_RUNEWORDS; r++) {
        if (txt_get_int(&t, r, "complete", 0) != 1)
            continue;
        RunewordDef *w = &s_runewords[s_num_runewords];
        memset(w, 0, sizeof *w);
        snprintf(w->key, sizeof w->key, "%s", txt_get(&t, r, txt_col(&t, "Name")));
        for (int k = 0; k < 6; k++) {
            char col[8];
            snprintf(col, sizeof col, "itype%d", k + 1);
            copy_lower(w->itype[k], sizeof w->itype[k], txt_get(&t, r, txt_col(&t, col)));
            snprintf(col, sizeof col, "Rune%d", k + 1);
            snprintf(w->rune[k], sizeof w->rune[k], "%s", txt_get(&t, r, txt_col(&t, col)));
            if (w->rune[k][0])
                w->nrunes = k + 1;
        }
        for (int k = 0; k < 3; k++) {
            char col[8];
            snprintf(col, sizeof col, "etype%d", k + 1);
            copy_lower(w->etype[k], sizeof w->etype[k], txt_get(&t, r, txt_col(&t, col)));
        }
        for (int k = 1; k <= 7; k++) {
            char c[12], p[12], mn[12], mx[12];
            snprintf(c, sizeof c, "T1Code%d", k);
            snprintf(p, sizeof p, "T1Param%d", k);
            snprintf(mn, sizeof mn, "T1Min%d", k);
            snprintf(mx, sizeof mx, "T1Max%d", k);
            PropRow pr;
            prop_row(&pr, &t, r, c, p, mn, mx);
            if (itemstats_has_property(pr.code))
                w->prop[w->nprop++] = pr;
        }
        if (w->nrunes > 0)
            s_num_runewords++;
    }
    txt_free(&t);
}

static void load_gems(void)
{
    TxtTable t = { 0 };
    s_num_gems = 0;
    if (load_table(&t, "gems") != 0)
        return;
    static const char *const k_where[3] = { "weapon", "helm", "shield" };
    for (int r = 1; r < t.rows && s_num_gems < MAX_GEMS; r++) {
        const char *code = txt_get(&t, r, txt_col(&t, "code"));
        if (!code[0])
            continue;
        GemDef *g = &s_gems[s_num_gems++];
        memset(g, 0, sizeof *g);
        snprintf(g->code, sizeof g->code, "%s", code);
        for (int w = 0; w < 3; w++)
            for (int k = 1; k <= 3; k++) {
                char c[24], p[24], mn[24], mx[24];
                snprintf(c, sizeof c, "%sMod%dCode", k_where[w], k);
                snprintf(p, sizeof p, "%sMod%dParam", k_where[w], k);
                snprintf(mn, sizeof mn, "%sMod%dMin", k_where[w], k);
                snprintf(mx, sizeof mx, "%sMod%dMax", k_where[w], k);
                prop_row(&g->mod[w][k - 1], &t, r, c, p, mn, mx);
            }
    }
    txt_free(&t);
}

static void load_ratio(void)
{
    TxtTable t = { 0 };
    memset(s_ratio, 0, sizeof s_ratio);
    if (load_table(&t, "itemratio") != 0)
        return;
    for (int r = 1; r < t.rows; r++) {
        if (txt_get_int(&t, r, "Version", 0) != 1)
            continue; /* las de la expansión */
        int uber = txt_get_int(&t, r, "Uber", 0) != 0, cls = txt_get_int(&t, r, "Class Specific", 0) != 0;
        Ratio *q = &s_ratio[uber][cls];
        q->unique = txt_get_int(&t, r, "Unique", 400);
        q->unique_div = txt_get_int(&t, r, "UniqueDivisor", 1);
        q->unique_min = txt_get_int(&t, r, "UniqueMin", 6400);
        q->rare = txt_get_int(&t, r, "Rare", 100);
        q->rare_div = txt_get_int(&t, r, "RareDivisor", 2);
        q->rare_min = txt_get_int(&t, r, "RareMin", 3200);
        q->set = txt_get_int(&t, r, "Set", 160);
        q->set_div = txt_get_int(&t, r, "SetDivisor", 2);
        q->set_min = txt_get_int(&t, r, "SetMin", 5600);
        q->magic = txt_get_int(&t, r, "Magic", 34);
        q->magic_div = txt_get_int(&t, r, "MagicDivisor", 3);
        q->magic_min = txt_get_int(&t, r, "MagicMin", 192);
        q->hiq = txt_get_int(&t, r, "HiQuality", 12);
        q->hiq_div = txt_get_int(&t, r, "HiQualityDivisor", 8);
        q->normal = txt_get_int(&t, r, "Normal", 2);
        q->normal_div = txt_get_int(&t, r, "NormalDivisor", 2);
    }
    txt_free(&t);
}

int items_init(void)
{
    TxtTable w = { 0 }, a = { 0 }, m = { 0 }, ty = { 0 }, pre = { 0 }, suf = { 0 }, am = { 0 };
    itemstats_init();
    int r = load_table(&w, "weapons") | load_table(&a, "armor") | load_table(&m, "misc") |
            load_table(&s_tc, "treasureclassex");
    s_num_types = 0;
    load_kind(&w, ITEM_WEAPON);
    load_kind(&a, ITEM_ARMOR);
    load_kind(&m, ITEM_MISC);
    txt_free(&w);
    txt_free(&a);
    txt_free(&m);

    /* Objetos mágicos: si faltan las tablas, todo sale normal */
    s_num_affix = 0;
    if (load_table(&ty, "itemtypes") == 0)
        load_itypes(&ty);
    build_isa();
    resolve_types();
    if (load_table(&pre, "magicprefix") == 0)
        load_affixes(&pre, 0);
    if (load_table(&suf, "magicsuffix") == 0)
        load_affixes(&suf, 1);
    if (load_table(&am, "automagic") == 0)
        load_affixes(&am, 2);
    txt_free(&ty);
    txt_free(&pre);
    txt_free(&suf);
    txt_free(&am);
    load_uniques();
    load_sets();
    load_runewords();
    load_gems();
    load_ratio();
    rare_names_init();
    return r ? -1 : 0;
}

const ItemType *items_find(const char *code)
{
    for (int i = 0; i < s_num_types; i++)
        if (strcmp(s_types[i].code, code) == 0)
            return &s_types[i];
    return NULL;
}

int items_is_potion(const ItemType *t)
{
    return t && (t->heal_life || t->heal_mana || t->heal_pct || t->stamina_ticks);
}

Item items_make(const ItemType *t)
{
    Item it;
    memset(&it, 0, sizeof it);
    it.type = t;
    it.unique = -1;
    it.ilvl = 1;
    if (t && t->kind == ITEM_ARMOR)
        it.ac = rnd_range(t->min_ac, t->max_ac);
    if (t && t->stackable) /* sale con la cantidad de spawnstack */
        it.amount = t->spawn_stack > 0 ? t->spawn_stack : t->max_stack;
    if (items_is_potion(t)) /* las pociones tienen varios tragos */
        it.amount = POTION_CHARGES;
    if (t && strcmp(t->code, "tsc") == 0) /* un Pergamino de Portal (se apilan: amount es cuántos) */
        it.amount = 1;
    if (t && t->var_inv > 0) /* anillos, amuletos, joyas y talismanes: uno de sus dibujos */
        it.gfx = (uint8_t)(1 + rnd() % (uint32_t)t->var_inv);
    return it;
}

/* ---- Las propiedades del objeto ---- */

static int item_props(const Item *it, ItemProp *p)
{
    for (int i = 0; i < it->num_stats; i++) {
        p[i].stat = it->stat[i];
        p[i].param = it->param[i];
        p[i].value = it->value[i];
    }
    return it->num_stats;
}

static void item_set_props(Item *it, const ItemProp *p, int n)
{
    it->num_stats = (uint8_t)(n > ITEM_MAX_STATS ? ITEM_MAX_STATS : n);
    for (int i = 0; i < it->num_stats; i++) {
        it->stat[i] = p[i].stat;
        it->param[i] = p[i].param;
        it->value[i] = p[i].value;
    }
}

/* Suma una propiedad de las tablas al objeto */
static void apply_row(Item *it, const PropRow *r)
{
    if (!r->code[0])
        return;
    ItemProp p[ITEM_MAX_STATS];
    int n = item_props(it, p), sockets = -1;
    n = itemstats_apply(it->type, it->ilvl, r->code, r->par, r->min, r->max, p, n, ITEM_MAX_STATS, &sockets);
    item_set_props(it, p, n);
    if (sockets > 0) { /* "sock": los engarces (hasta lo que entra en el objeto) */
        int most = it->type->inv_w * it->type->inv_h, cap = it->type->gemsockets > 0 ? it->type->gemsockets : most;
        sockets = sockets > cap ? cap : sockets > most ? most : sockets;
        if (sockets > it->sockets)
            it->sockets = (uint8_t)(sockets > ITEM_MAX_SOCKETS ? ITEM_MAX_SOCKETS : sockets);
    }
}

/* ---- Objetos mágicos ---- */

/* tix: el tipo del objeto en s_itypes (itype_index(t->type)) */
static int affix_fits(const Affix *a, const ItemType *t, int tix, int ilvl)
{
    if (a->level > ilvl || (a->maxlevel > 0 && ilvl > a->maxlevel))
        return 0;
    int any = 0;
    for (int k = 0; k < 7 && !any; k++)
        any = a->itype[k][0] && type_is_ix(t->type, tix, a->itype[k], a->itype_ix[k]);
    for (int k = 0; k < 5 && any; k++)
        if (a->etype[k][0] && type_is_ix(t->type, tix, a->etype[k], a->etype_ix[k]))
            any = 0;
    return any;
}

/* Un afijo al azar según su frecuencia (sin repetir el grupo de los que ya
   tiene: used_groups); -1 si ninguno sirve */
static int pick_affix(const ItemType *t, int ilvl, int suffix, const int *used_groups, int nused)
{
    int total = 0, tix = itype_index(t->type);
    for (int i = 0; i < s_num_affix; i++) {
        const Affix *a = &s_affix[i];
        int clash = 0;
        for (int k = 0; k < nused && !clash; k++)
            clash = a->group && a->group == used_groups[k];
        if (a->suffix == suffix && !clash && a->freq > 0 && affix_fits(a, t, tix, ilvl))
            total += a->freq;
    }
    if (total == 0)
        return -1;
    int pick = (int)(rnd() % (uint32_t)total);
    for (int i = 0; i < s_num_affix; i++) {
        const Affix *a = &s_affix[i];
        int clash = 0;
        for (int k = 0; k < nused && !clash; k++)
            clash = a->group && a->group == used_groups[k];
        if (a->suffix == suffix && !clash && a->freq > 0 && affix_fits(a, t, tix, ilvl)) {
            pick -= a->freq;
            if (pick < 0)
                return i;
        }
    }
    return -1;
}

static void apply_affix(Item *it, const Affix *a)
{
    for (int k = 0; k < a->nmods; k++)
        apply_row(it, &a->mod[k]);
}

void items_add_automagic(Item *it, int ilvl)
{
    const ItemType *t = it->type;
    if (!t || !t->auto_group || it->quality == QUALITY_UNIQUE || it->quality == QUALITY_SET ||
        it->quality == QUALITY_RUNEWORD)
        return;
    int rare = it->quality == QUALITY_RARE || it->quality == QUALITY_CRAFTED, total = 0;
    for (int pass = 0; pass < 2; pass++) { /* uno del grupo, por su frecuencia y su nivel */
        int pick = pass ? (int)(rnd() % (uint32_t)total) : 0;
        for (int i = 0; i < s_num_affix; i++) {
            const Affix *a = &s_affix[i];
            if (a->suffix != 2 || a->group != t->auto_group || a->freq <= 0 || a->level > ilvl ||
                (a->maxlevel > 0 && ilvl > a->maxlevel) || (rare && !a->rare))
                continue;
            if (!pass) {
                total += a->freq;
            } else if ((pick -= a->freq) < 0) {
                apply_affix(it, a);
                return;
            }
        }
        if (total == 0)
            return;
    }
}

static int can_be_magic(const ItemType *t)
{
    return t && (t->kind == ITEM_WEAPON || t->kind == ITEM_ARMOR || t->slot == SLOT_RING1 || t->slot == SLOT_AMULET ||
                 t->charm || type_is(t->type, "jewl", 0));
}

/* Nombres de los raros: el sustantivo (raresuffix.txt, con su género) y
   un adjetivo (rareprefix.txt) */
#define MAX_RARE_NAMES 200
static char *s_rare_pre[MAX_RARE_NAMES], *s_rare_suf[MAX_RARE_NAMES];
static int   s_num_rare_pre, s_num_rare_suf;

static void load_rare_names(const char *table, char **out, int *count)
{
    TxtTable t = { 0 };
    if (load_table(&t, table) != 0)
        return;
    for (int r = 1; r < t.rows && *count < MAX_RARE_NAMES; r++) {
        const char *name = txt_get(&t, r, txt_col(&t, "name"));
        if (name[0] && txt_get_int(&t, r, "version", 0) <= 100)
            out[(*count)++] = strdup(name);
    }
    txt_free(&t);
}

/* Al empezar (items_init): no se lee nada del disco jugando */
static void rare_names_init(void)
{
    load_rare_names("rareprefix", s_rare_pre, &s_num_rare_pre);
    load_rare_names("raresuffix", s_rare_suf, &s_num_rare_suf);
}

int items_make_rare(Item *it, int ilvl)
{
    const ItemType *t = it->type;
    if (!can_be_magic(t))
        return 0;
    Item save = *it;
    int want = 3 + (int)(rnd() % 4), got = 0, npre = 0, nsuf = 0, groups[8], ngroups = 0;
    it->num_stats = 0;
    it->ilvl = (uint8_t)(ilvl > 0 && ilvl < 256 ? ilvl : 1);
    for (int tries = 0; tries < 60 && got < want; tries++) {
        int suffix = npre >= 3 ? 1 : nsuf >= 3 ? 0 : (int)(rnd() % 2);
        int k = pick_affix(t, ilvl, suffix, groups, ngroups);
        if (k < 0)
            continue;
        apply_affix(it, &s_affix[k]);
        if (s_affix[k].group && ngroups < 8)
            groups[ngroups++] = s_affix[k].group;
        got++;
        npre += !suffix;
        nsuf += suffix;
    }
    if (got < 2 || !s_num_rare_pre || !s_num_rare_suf) {
        *it = save;
        return 0;
    }
    it->quality = QUALITY_RARE;
    const char *noun = s_rare_suf[rnd() % (uint32_t)s_num_rare_suf];
    const char *adj = s_rare_pre[rnd() % (uint32_t)s_num_rare_pre];
    str_compose(it->name, sizeof it->name, "RareFormat", str_get_gender(adj, str_gender(noun)), str_get(noun), NULL);
    return 1;
}

int items_make_magic(Item *it, int ilvl)
{
    const ItemType *t = it->type;
    if (!can_be_magic(t))
        return 0;
    int kind = (int)(rnd() % 4); /* 0 = solo prefijo, 1 = solo sufijo, 2-3 = los dos */
    int pre = kind != 1 ? pick_affix(t, ilvl, 0, NULL, 0) : -1;
    int suf = kind != 0 ? pick_affix(t, ilvl, 1, NULL, 0) : -1;
    if (pre < 0 && suf < 0) {
        pre = pick_affix(t, ilvl, 0, NULL, 0);
        if (pre < 0)
            suf = pick_affix(t, ilvl, 1, NULL, 0);
    }
    if (pre < 0 && suf < 0)
        return 0;
    it->quality = QUALITY_MAGIC;
    it->num_stats = 0;
    it->ilvl = (uint8_t)(ilvl > 0 && ilvl < 256 ? ilvl : 1);
    if (pre >= 0)
        apply_affix(it, &s_affix[pre]);
    if (suf >= 0)
        apply_affix(it, &s_affix[suf]);
    str_compose(it->name, sizeof it->name, "MagicFormat", pre >= 0 ? str_get_gender(s_affix[pre].key, t->gender) : NULL,
                t->name, suf >= 0 ? str_get(s_affix[suf].key) : NULL);
    return 1;
}

static int affix_by_row(int suffix, int row)
{
    for (int i = 0; i < s_num_affix; i++)
        if (s_affix[i].suffix == suffix && s_affix[i].row == row)
            return i;
    return -1;
}

int items_make_magic_affix(Item *it, int ilvl, int prefix_row, int suffix_row)
{
    int pre = prefix_row > 0 ? affix_by_row(0, prefix_row) : -1, suf = suffix_row > 0 ? affix_by_row(1, suffix_row) : -1;
    if (!items_make_magic(it, ilvl))
        return 0;
    if (pre < 0 && suf < 0)
        return 1;
    /* Otra vez con el fijo: el que se tiró al azar se reemplaza */
    const ItemType *t = it->type;
    if (pre < 0 && rnd() % 2)
        pre = pick_affix(t, ilvl, 0, NULL, 0);
    if (suf < 0 && rnd() % 2)
        suf = pick_affix(t, ilvl, 1, NULL, 0);
    it->num_stats = 0;
    if (pre >= 0)
        apply_affix(it, &s_affix[pre]);
    if (suf >= 0)
        apply_affix(it, &s_affix[suf]);
    str_compose(it->name, sizeof it->name, "MagicFormat", pre >= 0 ? str_get_gender(s_affix[pre].key, t->gender) : NULL,
                t->name, suf >= 0 ? str_get(s_affix[suf].key) : NULL);
    return 1;
}

void items_apply_prop(Item *it, const char *code, const char *par, int min, int max)
{
    PropRow r;
    memset(&r, 0, sizeof r);
    snprintf(r.code, sizeof r.code, "%s", code ? code : "");
    snprintf(r.par, sizeof r.par, "%s", par ? par : "");
    r.min = min;
    r.max = max > min ? max : min;
    if (it->type && itemstats_has_property(r.code))
        apply_row(it, &r);
}

int items_upgrade(Item *it, int elite)
{
    const ItemType *t = it->type;
    if (!t)
        return 0;
    const ItemType *to = items_find(elite ? t->ultracode : t->ubercode);
    if (!to || to == t)
        return 0;
    it->type = to;
    if (to->kind == ITEM_ARMOR)
        it->ac = rnd_range(to->min_ac, to->max_ac);
    return 1;
}

const char *items_affix_name(int suffix, int row)
{
    int k = affix_by_row(suffix, row);
    return k >= 0 ? str_get(s_affix[k].key) : "";
}

const char *items_unique_key(const Item *it)
{
    return it->quality == QUALITY_UNIQUE && it->unique >= 0 && it->unique < s_num_uniques ? s_uniques[it->unique].key : "";
}

int items_make_crafted(Item *it, int ilvl)
{
    /* Como el original: de 1 a 4 afijos (lo fijo de la receta va aparte) y nombre de raro */
    if (!items_make_rare(it, ilvl))
        return 0;
    while (it->num_stats > 4)
        it->num_stats--;
    it->quality = QUALITY_CRAFTED;
    return 1;
}

int items_make_unique_named(Item *it, const char *key)
{
    for (int u = 0; u < s_num_uniques && it->type; u++)
        if (strcmp(s_uniques[u].key, key) == 0 && strcmp(s_uniques[u].code, it->type->code) == 0) {
            it->quality = QUALITY_UNIQUE;
            it->unique = (int16_t)u;
            it->num_stats = 0;
            it->ilvl = (uint8_t)(s_uniques[u].lvl > 0 && s_uniques[u].lvl < 256 ? s_uniques[u].lvl : 1);
            for (int k = 0; k < s_uniques[u].nprop; k++)
                apply_row(it, &s_uniques[u].prop[k]);
            snprintf(it->name, sizeof it->name, "%s", str_get(s_uniques[u].key));
            return 1;
        }
    return 0;
}

int items_make_unique(Item *it, int ilvl)
{
    if (!it->type)
        return 0;
    int total = 0;
    for (int i = 0; i < s_num_uniques; i++)
        if (!s_uniques[i].tc_only && strcmp(s_uniques[i].code, it->type->code) == 0 && s_uniques[i].lvl <= ilvl)
            total += s_uniques[i].rarity > 0 ? s_uniques[i].rarity : 1;
    if (total == 0)
        return 0;
    int pick = (int)(rnd() % (uint32_t)total), u = -1;
    for (int i = 0; i < s_num_uniques && u < 0; i++)
        if (!s_uniques[i].tc_only && strcmp(s_uniques[i].code, it->type->code) == 0 && s_uniques[i].lvl <= ilvl &&
            (pick -= s_uniques[i].rarity > 0 ? s_uniques[i].rarity : 1) < 0)
            u = i;
    if (u < 0)
        return 0;
    it->quality = QUALITY_UNIQUE;
    it->unique = (int16_t)u;
    it->num_stats = 0;
    it->ilvl = (uint8_t)(ilvl > 0 && ilvl < 256 ? ilvl : 1);
    for (int k = 0; k < s_uniques[u].nprop; k++)
        apply_row(it, &s_uniques[u].prop[k]);
    snprintf(it->name, sizeof it->name, "%s", str_get(s_uniques[u].key));
    return 1;
}

int items_quest_unique(Item *it)
{
    static const char *const k_codes[] = { "vip", "msf", "hst", "qf1", "qf2", "hfh" };
    for (unsigned k = 0; it->type && k < sizeof k_codes / sizeof k_codes[0]; k++)
        if (strcmp(it->type->code, k_codes[k]) == 0) {
            int lvl = it->ilvl;
            int ok = items_make_unique(it, 99);
            it->ilvl = (uint8_t)(lvl > 0 ? lvl : 1);
            return ok;
        }
    return 0;
}

/* Lo que suelta una vaca (ITEMDROPFLAG_HELLBOVINE): el conjunto del Rey Vaca
   solo cae de ellas (ITEMS_RollSetItem: nSetId 29) */
static int s_bovine_drop;

void items_drop_from_bovine(int on)
{
    s_bovine_drop = on;
}

static int set_item_ok(const SetItemDef *d, const ItemType *t, int ilvl)
{
    return strcmp(d->code, t->code) == 0 && d->lvl <= ilvl &&
           (s_bovine_drop || strcmp(d->set_key, "Cow King's Leathers") != 0);
}

int items_make_set(Item *it, int ilvl)
{
    if (!it->type)
        return 0;
    int total = 0;
    for (int i = 0; i < s_num_setitems; i++)
        if (set_item_ok(&s_setitems[i], it->type, ilvl))
            total += s_setitems[i].rarity > 0 ? s_setitems[i].rarity : 1;
    if (total == 0)
        return 0;
    int pick = (int)(rnd() % (uint32_t)total), u = -1;
    for (int i = 0; i < s_num_setitems && u < 0; i++)
        if (set_item_ok(&s_setitems[i], it->type, ilvl) && (pick -= s_setitems[i].rarity > 0 ? s_setitems[i].rarity : 1) < 0)
            u = i;
    if (u < 0)
        return 0;
    it->quality = QUALITY_SET;
    it->unique = (int16_t)u;
    it->num_stats = 0;
    it->ilvl = (uint8_t)(ilvl > 0 && ilvl < 256 ? ilvl : 1);
    for (int k = 0; k < s_setitems[u].nprop; k++)
        apply_row(it, &s_setitems[u].prop[k]);
    snprintf(it->name, sizeof it->name, "%s", str_get(s_setitems[u].key));
    return 1;
}

/* Cuántos engarces puede tener según su nivel (itemtypes.txt) y el objeto */
static int max_sockets(const ItemType *t, int ilvl)
{
    int k = ilvl <= t->sock_lvl[0] ? 0 : ilvl <= t->sock_lvl[1] ? 1 : 2;
    int most = t->max_sock[k];
    if (t->gemsockets < most)
        most = t->gemsockets;
    if (t->inv_w * t->inv_h < most)
        most = t->inv_w * t->inv_h;
    return most > ITEM_MAX_SOCKETS ? ITEM_MAX_SOCKETS : most;
}

void items_add_sockets(Item *it, int ilvl)
{
    if (!it->type || (it->type->kind != ITEM_WEAPON && it->type->kind != ITEM_ARMOR) || it->type->stackable)
        return;
    int most = max_sockets(it->type, ilvl);
    most = most > 3 ? 3 : most; /* en Normal, hasta 3 (como el original) */
    if (most > 0)
        it->sockets = (uint8_t)(1 + rnd() % (uint32_t)most);
}

/* La recompensa de Larzuk (A5Q1): los comunes y superiores, todos los
   engarces que admiten según su nivel; los demás, uno. 0 si no admite */
int items_larzuk_sockets(Item *it)
{
    if (!it->type || (it->type->kind != ITEM_WEAPON && it->type->kind != ITEM_ARMOR) || it->type->stackable || it->sockets)
        return 0;
    int most = max_sockets(it->type, it->ilvl);
    if (most <= 0)
        return 0;
    int plain = it->quality == QUALITY_NORMAL || it->quality == QUALITY_SUPERIOR || it->quality == QUALITY_LOW;
    it->sockets = (uint8_t)(plain ? most : 1);
    return it->sockets;
}

/* "Superior": un poco más de daño o de defensa */
static void make_superior(Item *it)
{
    if (it->type->kind == ITEM_WEAPON) {
        int v = rnd_range(5, 15);
        ItemProp p[ITEM_MAX_STATS];
        int n = item_props(it, p);
        n = itemstats_add(p, n, ITEM_MAX_STATS, STAT_DMG_PCT, 0, v);
        n = itemstats_add(p, n, ITEM_MAX_STATS, STAT_DMG_MIN_PCT, 0, v);
        item_set_props(it, p, n);
    } else if (it->type->kind == ITEM_ARMOR) {
        ItemProp p[ITEM_MAX_STATS];
        int n = item_props(it, p);
        n = itemstats_add(p, n, ITEM_MAX_STATS, STAT_AC_PCT, 0, rnd_range(5, 15));
        item_set_props(it, p, n);
    } else {
        return;
    }
    it->quality = QUALITY_SUPERIOR;
}

/* ---- Engarzar ---- */

static const GemDef *gem_of(const char *code)
{
    for (int i = 0; i < s_num_gems; i++)
        if (strcmp(s_gems[i].code, code) == 0)
            return &s_gems[i];
    return NULL;
}

int items_can_socket(const Item *host, const Item *filler)
{
    if (!host->type || !filler->type || !filler->type->socket_filler || host->nsocketed >= host->sockets ||
        host->sockets == 0)
        return 0;
    /* Las joyas (con sus propiedades) y lo que está en gems.txt */
    return type_is(filler->type->type, "jewl", 0) || gem_of(filler->type->code) != NULL;
}

/* ¿Lo engarzado arma una palabra rúnica en este objeto? */
static int match_runeword(const Item *it)
{
    if (it->quality != QUALITY_NORMAL && it->quality != QUALITY_SUPERIOR && it->quality != QUALITY_LOW) /* como el original: no los mágicos ni mejores */
        return -1;
    for (int w = 0; w < s_num_runewords; w++) {
        const RunewordDef *d = &s_runewords[w];
        if (d->nrunes != it->sockets || d->nrunes != it->nsocketed)
            continue;
        int ok = 1;
        for (int k = 0; k < d->nrunes && ok; k++)
            ok = strcmp(d->rune[k], it->socketed[k]) == 0;
        if (!ok)
            continue;
        int type_ok = 0;
        for (int k = 0; k < 6 && !type_ok; k++)
            type_ok = d->itype[k][0] && type_is(it->type->type, d->itype[k], 0);
        for (int k = 0; k < 3 && type_ok; k++)
            if (d->etype[k][0] && type_is(it->type->type, d->etype[k], 0))
                type_ok = 0;
        if (type_ok)
            return w;
    }
    return -1;
}

int items_socket(Item *host, const Item *filler)
{
    if (!items_can_socket(host, filler))
        return 0;
    snprintf(host->socketed[host->nsocketed], sizeof host->socketed[0], "%.4s", filler->type->code);
    host->nsocketed++;
    if (type_is(filler->type->type, "jewl", 0)) { /* la joya: sus propiedades */
        ItemProp p[ITEM_MAX_STATS];
        int n = item_props(host, p);
        for (int i = 0; i < filler->num_stats; i++)
            n = itemstats_add(p, n, ITEM_MAX_STATS, filler->stat[i], filler->param[i], filler->value[i]);
        item_set_props(host, p, n);
    } else { /* gema o runa: lo de su lugar (arma, escudo, casco o armadura) */
        const GemDef *g = gem_of(filler->type->code);
        int w = host->type->kind == ITEM_WEAPON ? 0 : type_is(host->type->type, "shld", 0) ? 2 : 1;
        for (int k = 0; g && k < 3; k++)
            apply_row(host, &g->mod[w][k]);
    }
    int rw = match_runeword(host);
    if (rw >= 0) { /* la palabra rúnica: su nombre y sus propiedades */
        for (int k = 0; k < s_runewords[rw].nprop; k++)
            apply_row(host, &s_runewords[rw].prop[k]);
        host->quality = QUALITY_RUNEWORD;
        host->unique = (int16_t)rw;
        snprintf(host->name, sizeof host->name, "%s", str_get(s_runewords[rw].key));
    }
    return 1;
}

/* ---- Las palabras rúnicas, para la guía ---- */

int items_runeword_count(void)
{
    return s_num_runewords;
}

const char *items_runeword_name(int w)
{
    return w >= 0 && w < s_num_runewords ? str_get(s_runewords[w].key) : "";
}

int items_runeword_runes(int w, const ItemType **out)
{
    const RunewordDef *d = &s_runewords[w];
    for (int k = 0; k < d->nrunes; k++)
        out[k] = items_find(d->rune[k]);
    return d->nrunes;
}

int items_runeword_level(int w)
{
    int lvl = 0;
    for (int k = 0; k < s_runewords[w].nrunes; k++) {
        const ItemType *r = items_find(s_runewords[w].rune[k]);
        lvl = r && r->level_req > lvl ? r->level_req : lvl;
    }
    return lvl;
}

/* El nombre en plural de los tipos de objeto de runes.txt (si no, su código) */
static const char *runeword_type_name(const char *code)
{
    static const char *const k_names[][2] = {
        { "weap", "armas" },           { "mele", "armas cuerpo a cuerpo" }, { "miss", "arcos y ballestas" },
        { "swor", "espadas" },         { "axe", "hachas" },                 { "mace", "mazas" },
        { "club", "porras" },          { "hamm", "martillos" },             { "scep", "cetros" },
        { "knif", "dagas" },           { "spea", "lanzas" },                { "pole", "armas de asta" },
        { "staf", "bastones" },        { "wand", "varitas" },               { "h2h", "garras" },
        { "tors", "armaduras" },       { "helm", "cascos" },                { "shld", "escudos" },
        { "ashd", "escudos de paladín" }, { "pala", "escudos de paladín" }, { "head", "cabezas de nigromante" },
        { "grim", "grimorios" },
    };
    for (unsigned k = 0; k < sizeof k_names / sizeof k_names[0]; k++)
        if (strcmp(k_names[k][0], code) == 0)
            return k_names[k][1];
    return code;
}

void items_runeword_where(int w, char *buf, size_t cap)
{
    const RunewordDef *d = &s_runewords[w];
    size_t n = 0;
    buf[0] = '\0';
    for (int k = 0; k < 6; k++) {
        if (!d->itype[k][0] || n >= cap)
            continue;
        const char *name = runeword_type_name(d->itype[k]);
        int dup = 0; /* "ashd" y "pala" dicen lo mismo */
        for (int j = 0; j < k; j++)
            dup |= d->itype[j][0] && strcmp(runeword_type_name(d->itype[j]), name) == 0;
        if (!dup)
            n += (size_t)snprintf(buf + n, cap - n, "%s%s", n ? ", " : "", name);
    }
    for (int k = 0; k < 3; k++)
        if (d->etype[k][0] && n < cap)
            n += (size_t)snprintf(buf + n, cap - n, tr(" (no %s)"), runeword_type_name(d->etype[k]));
    if (buf[0] >= 'a' && buf[0] <= 'z')
        buf[0] = (char)(buf[0] - 'a' + 'A');
}

static int supported(const ItemType *t);

/* La palabra rúnica armada de verdad (las runas engarzadas en orden) sobre
   la primera base común que la acepta, con las tiradas en `mode`
   (itemstats_roll_mode) */
static int runeword_sample(int w, int mode, Item *out)
{
    const RunewordDef *d = &s_runewords[w];
    for (int i = 0; i < s_num_types; i++) {
        const ItemType *t = &s_types[i];
        if (!supported(t) || t->gemsockets < d->nrunes || t->inv_w * t->inv_h < d->nrunes)
            continue;
        Item it = items_make(t);
        it.quality = QUALITY_NORMAL;
        it.sockets = (uint8_t)d->nrunes;
        it.nsocketed = 0;
        it.num_stats = 0;
        itemstats_roll_mode(mode);
        for (int k = 0; k < d->nrunes; k++) {
            const ItemType *rt = items_find(d->rune[k]);
            Item r = rt ? items_make(rt) : (Item){ 0 };
            if (!rt || !items_socket(&it, &r))
                break;
        }
        itemstats_roll_mode(0);
        if (it.quality == QUALITY_RUNEWORD && it.unique == w) {
            *out = it;
            return 1;
        }
    }
    return 0;
}

/* Junta una línea con los mínimos y otra con los máximos: donde los números
   cambian queda "mín-máx" */
static void merge_range(const char *lo, const char *hi, char *out, size_t cap)
{
    size_t n = 0;
    while (*lo && n + 1 < cap) {
        if (isdigit((unsigned char)*lo) && isdigit((unsigned char)*hi)) {
            char *el, *eh;
            long a = strtol(lo, &el, 10), b = strtol(hi, &eh, 10);
            n += (size_t)snprintf(out + n, cap - n, a == b ? "%ld" : "%ld-%ld", a, b);
            lo = el;
            hi = eh;
            continue;
        }
        if (*lo != *hi) { /* cambió otra cosa: queda la de los mínimos */
            snprintf(out + n, cap - n, "%s", lo);
            return;
        }
        out[n++] = *lo++;
        hi++;
    }
    out[n < cap ? n : cap - 1] = '\0';
}

int items_runeword_describe(int w, int clvl, char lines[][112], int max_lines)
{
    Item lo, hi;
    if (w < 0 || w >= s_num_runewords || !runeword_sample(w, -1, &lo) || !runeword_sample(w, 1, &hi))
        return 0;
    char hl[24][112];
    int n = items_describe(&lo, clvl, lines, max_lines), m = items_describe(&hi, clvl, hl, 24);
    for (int k = 0; k < n && k < m; k++) {
        char merged[112];
        merge_range(lines[k], hl[k], merged, sizeof merged);
        snprintf(lines[k], 112, "%s", merged);
    }
    return n;
}

/* ---- Conjuntos ---- */

int items_set_of(const Item *it)
{
    return it->quality == QUALITY_SET && it->unique >= 0 && it->unique < s_num_setitems ? s_setitems[it->unique].set
                                                                                         : -1;
}

static int set_worn(int set, const Item *const *worn, int n)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (!worn[i] || items_set_of(worn[i]) != set)
            continue;
        int dup = 0; /* la misma pieza dos veces (dos anillos iguales) cuenta una */
        for (int k = 0; k < i && !dup; k++)
            dup = worn[k] && worn[k]->quality == QUALITY_SET && worn[k]->unique == worn[i]->unique;
        count += !dup;
    }
    return count;
}

static int row_props(const PropRow *r, ItemProp *out, int n, int cap)
{
    if (!r->code[0])
        return n;
    return itemstats_apply(NULL, 1, r->code, r->par, r->min, r->max, out, n, cap, NULL);
}

int items_set_bonus(const Item *const *worn, int n, ItemProp *out, int cap)
{
    int count = 0, done_sets[MAX_SETS] = { 0 };
    for (int i = 0; i < n; i++) {
        int set = worn[i] ? items_set_of(worn[i]) : -1;
        if (set < 0)
            continue;
        int have = set_worn(set, worn, n);
        const SetItemDef *d = &s_setitems[worn[i]->unique];
        for (int k = 0; k < 5; k++) /* las verdes de la pieza: con k + 2 piezas puestas */
            if (have >= k + 2)
                for (int ab = 0; ab < 2; ab++)
                    count = row_props(&d->aprop[k][ab], out, count, cap);
        if (done_sets[set])
            continue;
        done_sets[set] = 1;
        const SetDef *s = &s_sets[set];
        for (int k = 0; k < 4; k++) /* las del conjunto con 2, 3, 4 y 5 piezas */
            if (have >= k + 2)
                for (int ab = 0; ab < 2; ab++)
                    count = row_props(&s->partial[k][ab], out, count, cap);
        if (have >= s->nitems && s->nitems > 0)
            for (int k = 0; k < s->nfull; k++)
                count = row_props(&s->full[k], out, count, cap);
    }
    return count;
}

/* ---- Los textos ---- */

int items_stat(const Item *it, int stat)
{
    int sum = 0;
    for (int i = 0; i < it->num_stats; i++)
        if (it->stat[i] == stat)
            sum += it->value[i];
    return sum;
}

void items_thrown_damage(const Item *it, int *dmin, int *dmax)
{
    const ItemType *t = it->type;
    int ed = items_stat(it, STAT_DMG_PCT), flat = items_stat(it, STAT_DMG_FLAT);
    *dmin = t ? t->min_mis_dmg * (100 + ed) / 100 + items_stat(it, STAT_DMG_MIN) + flat : 0;
    *dmax = t ? t->max_mis_dmg * (100 + ed) / 100 + items_stat(it, STAT_DMG_MAX) + flat : 0;
    if (*dmax < *dmin)
        *dmax = *dmin;
}

void items_damage(const Item *it, int *dmin, int *dmax)
{
    const ItemType *t = it->type;
    int ed = items_stat(it, STAT_DMG_PCT), flat = items_stat(it, STAT_DMG_FLAT);
    *dmin = t ? t->min_dmg * (100 + ed) / 100 + items_stat(it, STAT_DMG_MIN) + flat : 0;
    *dmax = t ? t->max_dmg * (100 + ed) / 100 + items_stat(it, STAT_DMG_MAX) + flat : 0;
    if (*dmax < *dmin)
        *dmax = *dmin;
}

int items_defense(const Item *it)
{
    return it->ac * (100 + items_stat(it, STAT_AC_PCT)) / 100 + items_stat(it, STAT_AC);
}

int items_describe(const Item *it, int clvl, char lines[][112], int max_lines)
{
    ItemProp p[ITEM_MAX_STATS];
    int n = item_props(it, p);
    return itemstats_describe(p, n, clvl, lines, max_lines);
}

int items_stat_line(const Item *it, int n, char *buf, size_t cap)
{
    char lines[24][112];
    int count = items_describe(it, 1, lines, 24);
    if (n < 0 || n >= count)
        return 0;
    snprintf(buf, cap, "%s", lines[n]);
    return 1;
}

int items_describe_extra(const Item *it, const Item *const *worn, int nworn, int clvl, char lines[][112],
                         uint8_t *colors, int max_lines)
{
    int n = 0;
    const uint8_t green = text_col(2), gold = text_col(4), gray = COL(R_GRAY, 11);
    if (it->type && it->type->charm && n < max_lines) { /* los talismanes sirven desde la mochila */
        snprintf(lines[n], 112, "%s", str_get("Charmdes"));
        colors[n++] = COL(R_GRAY, 15);
    }
    if (it->type && it->type->socket_filler) {
        if (n < max_lines) {
            snprintf(lines[n], 112, "%s", str_get("ExInsertSockets"));
            colors[n++] = COL(R_GRAY, 15);
        }
        /* Las gemas y las runas: lo que dan en un arma, en una armadura o casco y en un escudo */
        const GemDef *g = gem_of(it->type->code);
        static const char *const k_where[3] = { "GemXp3", "GemXp4", "GemXp2" };
        for (int w = 0; g && w < 3 && n < max_lines; w++) {
            ItemProp gp[8];
            int ng = 0;
            for (int k = 0; k < 3; k++)
                ng = row_props(&g->mod[w][k], gp, ng, 8);
            char tmp[4][112];
            int nt = itemstats_describe(gp, ng, clvl, tmp, 4);
            snprintf(lines[n], 112, "%s", str_get(k_where[w]));
            for (int k = 0; k < nt; k++) {
                size_t len = strlen(lines[n]);
                snprintf(lines[n] + len, 112 - len, "%s%.100s", k ? ", " : " ", tmp[k]);
            }
            colors[n++] = COL(R_GRAY, 15);
        }
    }
    if (it->sockets > 0 && n < max_lines) {
        FmtArg a[1] = { { it->sockets, NULL } };
        itemstats_format(lines[n], 112, str_get("Socketable"), a, 1);
        colors[n++] = COL(R_BLUE, 15);
    }
    int set = items_set_of(it);
    if (set < 0)
        return n;
    (void)clvl;
    int have = set_worn(set, worn, nworn);
    const SetItemDef *d = &s_setitems[it->unique];
    /* Las verdes de esta pieza que ya están activas */
    ItemProp p[ITEM_MAX_STATS];
    int np = 0;
    for (int k = 0; k < 5; k++)
        if (have >= k + 2)
            for (int ab = 0; ab < 2; ab++)
                np = row_props(&d->aprop[k][ab], p, np, ITEM_MAX_STATS);
    char tmp[16][112];
    int nt = itemstats_describe(p, np, clvl, tmp, 16);
    for (int k = 0; k < nt && n < max_lines; k++) {
        snprintf(lines[n], 112, "%.111s", tmp[k]);
        colors[n++] = green;
    }
    /* El conjunto: su nombre y las piezas (verdes las puestas) */
    if (n < max_lines) {
        snprintf(lines[n], 112, "%s", str_get(s_sets[set].key));
        colors[n++] = gold;
    }
    for (int i = 0; i < s_num_setitems && n < max_lines; i++) {
        if (s_setitems[i].set != set)
            continue;
        int on = 0;
        for (int k = 0; k < nworn && !on; k++)
            on = worn[k] && worn[k]->quality == QUALITY_SET && worn[k]->unique == i;
        snprintf(lines[n], 112, "%s", str_get(s_setitems[i].key));
        colors[n++] = on ? green : gray;
    }
    /* Lo que da el conjunto con las piezas puestas */
    np = 0;
    const SetDef *s = &s_sets[set];
    for (int k = 0; k < 4; k++)
        if (have >= k + 2)
            for (int ab = 0; ab < 2; ab++)
                np = row_props(&s->partial[k][ab], p, np, ITEM_MAX_STATS);
    if (have >= s->nitems && s->nitems > 0)
        for (int k = 0; k < s->nfull; k++)
            np = row_props(&s->full[k], p, np, ITEM_MAX_STATS);
    nt = itemstats_describe(p, np, clvl, tmp, 16);
    for (int k = 0; k < nt && n < max_lines; k++) {
        snprintf(lines[n], 112, "%.111s", tmp[k]);
        colors[n++] = gold;
    }
    return n;
}

int items_level_req(const Item *it)
{
    if (!it->type)
        return 0;
    int lvl = it->type->level_req;
    if (it->quality == QUALITY_UNIQUE && it->unique >= 0 && it->unique < s_num_uniques &&
        s_uniques[it->unique].lvl_req > lvl)
        lvl = s_uniques[it->unique].lvl_req;
    if (it->quality == QUALITY_SET && it->unique >= 0 && it->unique < s_num_setitems &&
        s_setitems[it->unique].lvl_req > lvl)
        lvl = s_setitems[it->unique].lvl_req;
    for (int i = 0; i < it->nsocketed; i++) { /* lo engarzado (las runas piden nivel) */
        const ItemType *s = items_find(it->socketed[i]);
        if (s && s->level_req > lvl)
            lvl = s->level_req;
    }
    return lvl;
}

int items_type_count(void)
{
    return s_num_types;
}

const ItemType *items_type(int i)
{
    return &s_types[i];
}

/* Se compra al precio base y se vende a un cuarto, como mínimo 1 */
/* ---- La apuesta ---- */

static int supported(const ItemType *t);

static const ItemType *s_gamble[160]; /* gamble.txt, de menor a mayor nivel */
static int             s_num_gamble = -1;
static int             s_gamble_rare, s_gamble_set, s_gamble_uniq, s_gamble_uber, s_gamble_ultra;

static void gamble_load(void)
{
    s_num_gamble = 0;
    TxtTable g = { 0 }, d = { 0 };
    if (load_table(&g, "gamble") == 0) {
        for (int r = 1; r < g.rows && s_num_gamble < (int)(sizeof s_gamble / sizeof s_gamble[0]); r++) {
            const ItemType *t = items_find(txt_get(&g, r, txt_col(&g, "code")));
            if (!t || !supported(t))
                continue;
            int k = s_num_gamble++; /* ordenada por nivel (como pGambleSelection) */
            while (k > 0 && s_gamble[k - 1]->level > t->level) {
                s_gamble[k] = s_gamble[k - 1];
                k--;
            }
            s_gamble[k] = t;
        }
    }
    if (load_table(&d, "difficultylevels") == 0) { /* la fila de Normal */
        int r = txt_find_row(&d, txt_col(&d, "Name"), "Normal");
        s_gamble_rare = txt_get_int(&d, r, "GambleRare", 10000);
        s_gamble_set = txt_get_int(&d, r, "GambleSet", 100);
        s_gamble_uniq = txt_get_int(&d, r, "GambleUnique", 50);
        s_gamble_uber = txt_get_int(&d, r, "GambleUber", 90);
        s_gamble_ultra = txt_get_int(&d, r, "GambleUltra", 33);
    }
    txt_free(&g);
    txt_free(&d);
}

int items_gamble_roll(int k, int clvl, Item *out)
{
    if (s_num_gamble < 0)
        gamble_load();
    if (s_num_gamble == 0)
        return 0;
    int ilvl = clvl + (int)(rnd() % 10) - 5;
    ilvl = ilvl < 5 ? 5 : ilvl > 99 ? 99 : ilvl;
    int limit = 0; /* los de gamble.txt hasta ese nivel (pGambleChooseLimit) */
    while (limit < s_num_gamble && s_gamble[limit]->level <= ilvl)
        limit++;
    const ItemType *t = s_gamble[rnd() % (uint32_t)(limit > 0 ? limit : 1)];
    if (k == 0 && items_find("rin"))
        t = items_find("rin");
    else if (k == 1 && items_find("amu"))
        t = items_find("amu");
    /* La versión excepcional, o si no la de élite: más probable cuanto más
       nivel le sobra al objeto */
    const ItemType *uber = t->ubercode[0] ? items_find(t->ubercode) : NULL;
    const ItemType *ultra = t->ultracode[0] ? items_find(t->ultracode) : NULL;
    if (uber && supported(uber) && (int)(rnd() % 10000) < s_gamble_uber * (ilvl - uber->level) + 1)
        t = uber;
    else if (ultra && supported(ultra) && (int)(rnd() % 10000) < s_gamble_ultra * (ilvl - ultra->level) + 1)
        t = ultra;
    Item it = items_make(t);
    it.ilvl = (uint8_t)ilvl;
    int roll = (int)(rnd() % 100000);
    if (roll < s_gamble_uniq) {
        if (!items_make_unique(&it, ilvl) && !items_make_rare(&it, ilvl))
            items_make_magic(&it, ilvl);
    } else if (roll < s_gamble_uniq + s_gamble_set) {
        if (!items_make_set(&it, ilvl))
            items_make_magic(&it, ilvl);
    } else if (roll < s_gamble_uniq + s_gamble_set + s_gamble_rare) {
        if (!items_make_rare(&it, ilvl))
            items_make_magic(&it, ilvl);
    } else {
        items_make_magic(&it, ilvl);
    }
    items_add_automagic(&it, ilvl);
    *out = it;
    return 1;
}

int items_gamble_price(const ItemType *t, int clvl)
{
    if (!t)
        return 0;
    if (!strcmp(t->code, "rin") || !strcmp(t->code, "amu"))
        return t->gamble_cost > 0 ? t->gamble_cost : 1;
    /* Lo que puede salir mejor encarece el objeto (la mitad de los niveles
       que le sobran para la versión excepcional, un cuarto para la de élite) */
    const ItemType *uber = t->ubercode[0] ? items_find(t->ubercode) : NULL;
    const ItemType *ultra = t->ultracode[0] ? items_find(t->ultracode) : NULL;
    int64_t wu = uber ? 100 * (clvl - uber->level) / 2 + 1 : 0, wx = ultra ? 100 * (clvl - ultra->level) / 4 + 1 : 0;
    wu = wu < 0 ? 0 : wu;
    wx = wx < 0 ? 0 : wx;
    int stack = t->stackable ? (t->spawn_stack + t->max_stack) / 2 : 1;
    stack = stack < 1 ? 1 : stack;
    int64_t base = wu * (uber ? uber->cost : 0) + wx * (ultra ? ultra->cost : 0) + (int64_t)stack * t->cost * (10000 - wx - wu);
    int lvl = clvl < 5 ? 5 : clvl;
    int extra = t->level - 45 > 0 ? t->level - 45 : 0;
    int64_t cost = ((2 * lvl + 1) / 3 + 20) * (base / 10000 + (250 * (lvl + extra - t->level / 2)) / 3) / 15;
    return cost < 1 ? 1 : cost > 2000000000 ? 2000000000 : (int)cost;
}

int items_price(const Item *it, int selling)
{
    int price = it->type ? it->type->cost : 0;
    static const int k_mult[9] = { 1, 3, 5, 6, 8, 10, 2, 1, 5 };
    int q = it->quality < 9 ? it->quality : 0;
    price = price * k_mult[q] + (q ? 40 + 20 * q : 0) * it->num_stats;
    price += 50 * it->sockets;
    /* Pociones: por los tragos (una empezada vale lo que le queda; una pila,
       las que tiene). Llaves: por cuántas (el precio es el de 12, su pila del
       original) */
    if (items_is_potion(it->type) && it->amount > 0 && it->amount != POTION_CHARGES)
        price = price * it->amount / POTION_CHARGES;
    else if (it->type && strcmp(it->type->code, "key") == 0)
        price = price * it->amount / 12;
    else if (it->type && strcmp(it->type->code, "tsc") == 0 && it->amount > 1) /* una pila de pergaminos */
        price = price * it->amount;
    if (selling)
        price /= 4;
    return price > 0 ? price : 1;
}

/* Lo que cae: armas, armaduras, anillos, amuletos, joyas, talismanes, gemas,
   runas, pociones y munición */
static int supported(const ItemType *t);

int items_supported(const ItemType *t)
{
    return supported(t);
}

static int supported(const ItemType *t)
{
    if (t->kind == ITEM_WEAPON) /* sin pociones para arrojar todavía */
        return strcmp(t->type, "tpot") != 0;
    if (t->kind == ITEM_ARMOR)
        return t->slot != SLOT_NONE;
    if (t->slot == SLOT_RING1 || t->slot == SLOT_AMULET || t->charm || t->socket_filler)
        return 1;
    /* las llaves (para los cofres cerrados) y el Pergamino de Portal (el
       tomo no: pedido del usuario, los pergaminos se apilan de a TP_STACK) */
    if (strcmp(t->code, "key") == 0 || strcmp(t->code, "tsc") == 0)
        return 1;
    /* el endgame: las llaves y los órganos del evento Pandemonium, el Estandarte de los
       héroes, las esencias y el Token of Absolution, las estatuas de los Ancianos colosales y
       las astillas de la Piedra del Mundo (endgame.inc) */
    static const char *const k_endgame[] = { "pk1", "pk2", "pk3", "dhn", "bey", "mbr", "std", "tes", "ceh", "bet", "fed",
                                             "toa", "ua1", "ua2", "ua3", "ua4", "ua5", "xa1", "xa2", "xa3", "xa4", "xa5" };
    for (unsigned k = 0; k < sizeof k_endgame / sizeof k_endgame[0]; k++)
        if (strcmp(t->code, k_endgame[k]) == 0)
            return 1;
    return strcmp(t->type, "hpot") == 0 || strcmp(t->type, "mpot") == 0 || strcmp(t->type, "rpot") == 0 ||
           strcmp(t->type, "spot") == 0 || t->ammo || strcmp(t->code, "box") == 0;
}

/* "weap3", "armo6"...: un arma o armadura al azar de nivel N-2..N, según su rareza */
static const ItemType *pick_by_level(ItemKind kind, int lvl)
{
    int total = 0;
    for (int i = 0; i < s_num_types; i++) {
        const ItemType *t = &s_types[i];
        if (t->kind == kind && t->spawnable && t->level <= lvl && t->level > lvl - 3 && supported(t))
            total += t->rarity > 0 ? t->rarity : 1;
    }
    if (total == 0)
        return NULL;
    int pick = (int)(rnd() % (uint32_t)total);
    for (int i = 0; i < s_num_types; i++) {
        const ItemType *t = &s_types[i];
        if (t->kind == kind && t->spawnable && t->level <= lvl && t->level > lvl - 3 && supported(t)) {
            pick -= t->rarity > 0 ? t->rarity : 1;
            if (pick < 0)
                return t;
        }
    }
    return NULL;
}

/* Lo que suman las clases de tesoro del camino (Unique, Set, Rare, Magic): bajan la probabilidad en contra */
typedef struct {
    int unique, set, rare, magic;
} TcMods;

static int s_mf; /* búsqueda de magia de la tirada en curso */
static int s_force_quality; /* QUALITY_* que se pide en la tirada en curso (0 = la que salga) */

/* Una probabilidad de calidad como la del original: (ratio - diferencia de
   niveles / divisor) * 128, con la búsqueda de magia y lo de la clase de
   tesoro; 1 si sale */
static int quality_roll(int ratio, int div, int min, int diff, int mf_kind, int tcmod)
{
    int base = (ratio - diff / (div > 0 ? div : 1)) << 7;
    int mfd = s_mf + 100;
    if (s_mf != 0 && mfd > 0) {
        int d = mfd;
        if (mfd > 110) { /* lo que rinde la búsqueda de magia baja con cuánto hay */
            if (mf_kind == 0)
                d = 50 * (5 * mfd - 500) / (mfd + 150) + 100;
            else if (mf_kind == 1)
                d = 100 * (5 * mfd - 500) / (mfd + 400) + 100;
            else if (mf_kind == 2)
                d = 200 * (3 * mfd - 300) / (mfd + 500) + 100;
        }
        if (d)
            base = 12800 * (ratio - diff / (div > 0 ? div : 1)) / d;
    }
    if (base < min)
        base = min;
    int chance = base - base * tcmod / 1024;
    return chance <= 0 || (int)(rnd() % (uint32_t)chance) < 128;
}

static int roll_quality(const ItemType *t, int ilvl, const TcMods *m)
{
    if (t->always_normal || !can_be_magic(t))
        return QUALITY_NORMAL;
    const Ratio *r = &s_ratio[t->uber ? 1 : 0][t->class_only ? 1 : 0];
    int diff = ilvl - t->level;
    if (quality_roll(r->unique, r->unique_div, r->unique_min, diff, 0, m->unique))
        return QUALITY_UNIQUE;
    if (quality_roll(r->set, r->set_div, r->set_min, diff, 1, m->set))
        return QUALITY_SET;
    if (t->can_rare && quality_roll(r->rare, r->rare_div, r->rare_min, diff, 2, m->rare))
        return QUALITY_RARE;
    if (t->always_magic || quality_roll(r->magic, r->magic_div, r->magic_min, diff, 3, m->magic))
        return QUALITY_MAGIC;
    int hiq = (r->hiq - diff / (r->hiq_div > 0 ? r->hiq_div : 1)) << 7;
    if (hiq <= 0 || (int)(rnd() % (uint32_t)hiq) < 128)
        return QUALITY_SUPERIOR;
    return QUALITY_NORMAL;
}

static void add_drop(const char *code, int mlvl, const TcMods *m, Item *out, int *n, int max)
{
    if (*n >= max)
        return;
    const ItemType *t = NULL;
    if (strncmp(code, "gld", 3) == 0 && (!code[3] || code[3] == ',')) {
        t = items_find("gld");
        if (t) { /* "gld,mul=1280": de los jefes, más oro (en 1/256) */
            const char *mul = strstr(code, "mul=");
            Item it = items_make(t);
            it.amount = rnd_range(1, mlvl * 12 + 8);
            if (mul && atoi(mul + 4) > 0)
                it.amount = it.amount * atoi(mul + 4) / 256;
            out[(*n)++] = it;
        }
        return;
    }
    /* Resurrected nombra a veces un único directamente (las Joyas colosales: "Defender's Bile") */
    for (int u = 0; u < s_num_uniques; u++)
        if (strcmp(s_uniques[u].key, code) == 0) {
            const ItemType *ut = items_find(s_uniques[u].code);
            if (ut) {
                Item it = items_make(ut);
                if (items_make_unique_named(&it, code))
                    out[(*n)++] = it;
            }
            return;
        }
    if (strncmp(code, "weap", 4) == 0 && isdigit((unsigned char)code[4]))
        t = pick_by_level(ITEM_WEAPON, atoi(code + 4));
    else if (strncmp(code, "armo", 4) == 0 && isdigit((unsigned char)code[4]))
        t = pick_by_level(ITEM_ARMOR, atoi(code + 4));
    else
        t = items_find(code);
    if (!t || !supported(t))
        return;
    if (items_is_potion(t) && (int)(rnd() % 100) < POTION_DROP_CUT)
        return; /* menos pociones que el original (cada una tiene varios tragos) */
    Item it = items_make(t);
    it.ilvl = (uint8_t)(mlvl > 0 && mlvl < 256 ? mlvl : 1);
    /* la calidad pedida (los cofres brillantes, los estantes) si el objeto la puede tener */
    int q = s_force_quality && !t->always_normal && can_be_magic(t) ? s_force_quality : roll_quality(t, mlvl, m);
    switch (q) {
    case QUALITY_UNIQUE: /* si no hay un único de ese objeto, sale raro (como el original) */
        if (items_make_unique(&it, mlvl) || items_make_rare(&it, mlvl))
            break;
        items_make_magic(&it, mlvl);
        break;
    case QUALITY_SET:
        if (!items_make_set(&it, mlvl))
            items_make_magic(&it, mlvl);
        break;
    case QUALITY_RARE:
        if (!items_make_rare(&it, mlvl))
            items_make_magic(&it, mlvl);
        break;
    case QUALITY_MAGIC:
        items_make_magic(&it, mlvl);
        break;
    case QUALITY_SUPERIOR:
        make_superior(&it);
        /* también pueden salir con engarces */
        /* fall through */
    default:
        if ((t->kind == ITEM_WEAPON || t->kind == ITEM_ARMOR) && rnd() % 100 < 33)
            items_add_sockets(&it, mlvl);
        break;
    }
    items_add_automagic(&it, mlvl);
    out[(*n)++] = it;
}

/* Resuelve una clase de tesoro: cada opción puede ser otra clase (recursivo) */
static void roll(const char *name, int mlvl, TcMods m, Item *out, int *n, int max, int depth)
{
    if (!name[0] || depth > 10 || *n >= max)
        return;
    int row = txt_find_row(&s_tc, 0, name);
    if (row < 0) {
        add_drop(name, mlvl, &m, out, n, max);
        return;
    }
    /* Lo que suma esta clase a la calidad: el mayor del camino */
    static const char *const k_mods[4] = { "Unique", "Set", "Rare", "Magic" };
    int *mods[4] = { &m.unique, &m.set, &m.rare, &m.magic };
    for (int k = 0; k < 4; k++) {
        int v = txt_get_int(&s_tc, row, k_mods[k], 0);
        if (v > *mods[k])
            *mods[k] = v;
    }
    const char *items[10];
    int probs[10], count = 0, sum = 0;
    for (int i = 1; i <= 10; i++) {
        char col[16];
        snprintf(col, sizeof col, "Item%d", i);
        const char *item = txt_get(&s_tc, row, txt_col(&s_tc, col));
        snprintf(col, sizeof col, "Prob%d", i);
        int prob = txt_get_int(&s_tc, row, col, 0);
        if (item[0] && prob > 0) {
            items[count] = item;
            probs[count++] = prob;
            sum += prob;
        }
    }
    int picks = txt_get_int(&s_tc, row, "Picks", 1);
    int nodrop = txt_get_int(&s_tc, row, "NoDrop", 0);
    if (picks < 0) {
        /* Picks negativo: suelta cada opción tantas veces como su probabilidad */
        for (int i = 0; i < count; i++)
            for (int k = 0; k < probs[i]; k++)
                roll(items[i], mlvl, m, out, n, max, depth + 1);
        return;
    }
    for (int p = 0; p < picks; p++) {
        int total = nodrop + sum;
        if (total <= 0)
            break;
        int r = (int)(rnd() % (uint32_t)total);
        if (r < nodrop)
            continue;
        r -= nodrop;
        for (int i = 0; i < count; i++) {
            if (r < probs[i]) {
                roll(items[i], mlvl, m, out, n, max, depth + 1);
                break;
            }
            r -= probs[i];
        }
    }
}

/* La de su grupo (la columna "group") de nivel más alto que no pase `lvl`: las filas de un grupo van
   seguidas, de menor a mayor (DATATBLS_GetTreasureClassExRecordFromIdAndLevel) */
const char *items_tc_for_level(const char *name, int lvl)
{
    int row = name && name[0] && lvl > 0 ? txt_find_row(&s_tc, 0, name) : -1;
    int g = row >= 0 ? txt_get_int(&s_tc, row, "group", 0) : 0;
    if (!g)
        return name;
    while (row + 1 < s_tc.rows && txt_get_int(&s_tc, row + 1, "group", 0) == g && txt_get_int(&s_tc, row + 1, "level", 0) <= lvl)
        row++;
    return txt_get(&s_tc, row, 0);
}

int items_roll_drop(const char *treasure_class, int monster_level, int mf, Item *out, int max)
{
    int n = 0;
    TcMods m = { 0, 0, 0, 0 };
    s_mf = mf;
    if (treasure_class)
        roll(treasure_class, monster_level, m, out, &n, max, 0);
    return n;
}

int items_roll_drop_quality(const char *treasure_class, int item_level, int mf, int quality, Item *out, int max)
{
    s_force_quality = quality;
    int n = items_roll_drop(treasure_class, item_level, mf, out, max);
    s_force_quality = 0;
    return n;
}

const char *items_label(const Item *it, char *buf, size_t cap)
{
    if (!it->type)
        snprintf(buf, cap, "?");
    else if (strcmp(it->type->code, "gld") == 0)
        snprintf(buf, cap, tr("%d de oro"), it->amount);
    else if (it->quality == QUALITY_SUPERIOR)
        str_compose(buf, cap, "HiqualityFormat", str_get_gender("Hiquality", it->type->gender), it->type->name, NULL);
    else if (it->quality != QUALITY_NORMAL && it->name[0])
        snprintf(buf, cap, "%s", it->name);
    else
        snprintf(buf, cap, "%s", it->type->name);
    return buf;
}

/* Un color de texto del original (0 blanco, 2 verde, 3 azul, 4 dorado, 8 naranja, 9 amarillo...) */
static uint8_t text_col(int c)
{
    return g_text_shift[c][COL(R_GRAY, 15)];
}

uint8_t items_label_color(const Item *it)
{
    if (it->type && strcmp(it->type->code, "gld") == 0)
        return COL(R_GOLD, 14);
    switch (it->quality) {
    case QUALITY_MAGIC:    return COL(R_BLUE, 15); /* azul, como en el original */
    case QUALITY_RARE:     return text_col(9);      /* amarillo */
    case QUALITY_CRAFTED:  return text_col(8);      /* anaranjado */
    case QUALITY_SET:      return text_col(2);      /* verde */
    case QUALITY_UNIQUE:
    case QUALITY_RUNEWORD: return text_col(4);      /* dorado */
    default:               return it->sockets ? COL(R_GRAY, 11) : COL(R_GRAY, 15);
    }
}

/* Para guardar: "#2 código defensa cantidad calidad nivel dibujo fila
   engarces ocupados [códigos] cantidad (propiedad parámetro valor)... |nombre" */
void items_to_text(const Item *it, char *buf, size_t cap)
{
    size_t o = (size_t)snprintf(buf, cap, "#2 %s %d %d %d %d %d %d %d %d", it->type ? it->type->code : "-", it->ac,
                                it->amount, it->quality, it->ilvl, it->gfx, it->unique, it->sockets, it->nsocketed);
    for (int i = 0; i < it->nsocketed && o < cap; i++)
        o += (size_t)snprintf(buf + o, cap - o, " %s", it->socketed[i]);
    if (o < cap)
        o += (size_t)snprintf(buf + o, cap - o, " %d", it->num_stats);
    for (int i = 0; i < it->num_stats && o < cap; i++)
        o += (size_t)snprintf(buf + o, cap - o, " %d %d %d", it->stat[i], it->param[i], it->value[i]);
    if (o < cap)
        snprintf(buf + o, cap - o, " |%s", it->name);
}

/* Las propiedades de las partidas de antes (el orden de STAT_* viejo) */
static const int k_old_stats[28] = { 17, 21, 22, 16, 31, 7, 9, 0, 2, 19, 11, 89, 96, 28,
                                     60, 62, 74, 34, 79, 80, 93, 138, 48, 49, 50, 51, 54, 55 };

static int items_from_text_v2(const char *text, Item *it)
{
    char code[8];
    int ac, amount, quality, ilvl, gfx, unique, sockets, nsock, used;
    if (sscanf(text, "#2 %7s %d %d %d %d %d %d %d %d%n", code, &ac, &amount, &quality, &ilvl, &gfx, &unique, &sockets,
               &nsock, &used) != 9)
        return 0;
    const ItemType *t = items_find(code);
    if (!t || nsock < 0 || nsock > ITEM_MAX_SOCKETS || sockets < 0 || sockets > ITEM_MAX_SOCKETS)
        return 0;
    memset(it, 0, sizeof *it);
    it->type = t;
    it->ac = ac;
    it->amount = amount;
    it->quality = (uint8_t)(quality >= 0 && quality <= QUALITY_CRAFTED ? quality : QUALITY_NORMAL);
    it->ilvl = (uint8_t)(ilvl > 0 && ilvl < 256 ? ilvl : 1);
    it->gfx = (uint8_t)(gfx >= 0 && gfx <= 6 ? gfx : 0);
    it->unique = (int16_t)unique;
    it->sockets = (uint8_t)sockets;
    if (items_is_potion(t) && it->amount <= 0)
        it->amount = POTION_CHARGES;
    if (strcmp(t->code, "tsc") == 0 && it->amount <= 0) /* de antes de las pilas: uno */
        it->amount = 1;
    text += used;
    for (int i = 0; i < nsock; i++) {
        char sc[8];
        if (sscanf(text, "%7s%n", sc, &used) != 1)
            return 0;
        text += used;
        snprintf(it->socketed[it->nsocketed++], sizeof it->socketed[0], "%.4s", sc);
    }
    int n;
    if (sscanf(text, "%d%n", &n, &used) != 1 || n < 0 || n > ITEM_MAX_STATS)
        return 0;
    text += used;
    for (int i = 0; i < n; i++) {
        int st, par, v;
        if (sscanf(text, "%d %d %d%n", &st, &par, &v, &used) != 3)
            return 0;
        text += used;
        if (st < 0 || st >= ISC_MAX)
            continue;
        it->stat[it->num_stats] = (uint16_t)st;
        it->param[it->num_stats] = (int16_t)par;
        it->value[it->num_stats] = v;
        it->num_stats++;
    }
    const char *bar = strchr(text, '|');
    if (bar) {
        snprintf(it->name, sizeof it->name, "%s", bar + 1);
        it->name[strcspn(it->name, "\r\n")] = '\0';
    }
    /* Las filas de los únicos y los conjuntos pueden cambiar: se buscan por el nombre */
    if (it->quality == QUALITY_UNIQUE) {
        it->unique = -1;
        for (int i = 0; i < s_num_uniques && it->unique < 0; i++)
            if (strcmp(str_get(s_uniques[i].key), it->name) == 0)
                it->unique = (int16_t)i;
    } else if (it->quality == QUALITY_SET) {
        it->unique = -1;
        for (int i = 0; i < s_num_setitems && it->unique < 0; i++)
            if (strcmp(str_get(s_setitems[i].key), it->name) == 0)
                it->unique = (int16_t)i;
    }
    return 1;
}

int items_from_text(const char *text, Item *it)
{
    if (strncmp(text, "#2 ", 3) == 0)
        return items_from_text_v2(text, it);
    char code[8];
    int ac, amount, quality, n, used;
    if (sscanf(text, "%7s %d %d %d %d%n", code, &ac, &amount, &quality, &n, &used) != 5)
        return 0;
    const ItemType *t = items_find(code);
    if (!t || n < 0 || n > ITEM_MAX_STATS)
        return 0;
    memset(it, 0, sizeof *it);
    it->type = t;
    it->unique = -1;
    it->ilvl = 1;
    it->ac = ac;
    it->amount = amount;
    it->quality = (uint8_t)(quality == QUALITY_MAGIC || quality == QUALITY_RARE ? quality : QUALITY_NORMAL);
    if (items_is_potion(t) && it->amount <= 0) /* de antes de las cargas: llena */
        it->amount = POTION_CHARGES;
    if (strcmp(t->code, "tsc") == 0 && it->amount <= 0) /* de antes de las pilas: uno */
        it->amount = 1;
    text += used;
    for (int i = 0; i < n; i++) {
        int st, v;
        if (sscanf(text, "%d %d%n", &st, &v, &used) != 2)
            return 0;
        text += used;
        if (st < 0 || st >= 28)
            continue; /* propiedad de otra versión: se ignora */
        it->stat[it->num_stats] = (uint16_t)k_old_stats[st];
        it->value[it->num_stats] = v;
        it->num_stats++;
        if (st == 0) { /* el % de daño era uno solo: ahora van el máximo y el mínimo */
            it->stat[it->num_stats] = STAT_DMG_MIN_PCT;
            it->value[it->num_stats] = v;
            it->num_stats++;
        }
    }
    const char *bar = strchr(text, '|');
    if (bar) {
        snprintf(it->name, sizeof it->name, "%s", bar + 1);
        it->name[strcspn(it->name, "\r\n")] = '\0';
    }
    if (it->quality != QUALITY_NORMAL && !it->name[0])
        it->quality = QUALITY_NORMAL;
    return 1;
}

/* ---- Gráficos ---- */

static void gfx_load(void *ptr, int arg)
{
    (void)arg;
    ItemGfx *g = ptr;
    char path[96];
    snprintf(path, sizeof path, "data/global/items/%s.dc6", g->name);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    job_flag_set(&g->state, d && dc6_load(d, n, &g->sprite) == 0 ? 1 : -1);
    free(d);
}

/* Solo se dibujan: la primera vez se piden al hilo de trabajos (en la Pi
   leer cada uno de la tarjeta trababa el cuadro) y mientras no se ven */
static const Sprite *get_gfx(const char *name)
{
    if (!name[0])
        return NULL;
    ItemGfx *g = NULL;
    for (int i = 0; i < s_num_gfx && !g; i++)
        if (strcmp(s_gfx[i].name, name) == 0)
            g = &s_gfx[i];
    if (!g) {
        if (s_num_gfx == MAX_GFX)
            return NULL;
        g = &s_gfx[s_num_gfx++];
        snprintf(g->name, sizeof g->name, "%s", name);
        job_flag_set(&g->state, 2);
        jobs_push(gfx_load, g, 0);
    }
    return job_flag_get(&g->state) == 1 ? &g->sprite : NULL;
}

void items_inv_size(const ItemType *t, int *w, int *h)
{
    const Sprite *s = t ? get_gfx(t->invfile) : NULL;
    *w = s ? s->frames[0].width : (t ? t->inv_w * 29 : 29);
    *h = s ? s->frames[0].height : (t ? t->inv_h * 29 : 29);
}

void items_draw_inv(const ItemType *t, int x, int y)
{
    const Sprite *s = t ? get_gfx(t->invfile) : NULL;
    if (!s)
        return;
    const SpriteFrame *f = &s->frames[0];
    fb_blit_spans(f->pixels, f->spans, f->width, f->height, x, y);
}

void items_draw_icon(const ItemType *t, int x, int y, int size)
{
    const Sprite *s = t ? get_gfx(t->invfile) : NULL;
    if (!s)
        return;
    const SpriteFrame *f = &s->frames[0];
    int w = f->width, h = f->height;
    if (w > size || h > size) { /* se achica sin deformarlo */
        int big = w > h ? w : h;
        w = w * size / big;
        h = h * size / big;
    }
    fb_blit_scaled(f->pixels, f->width, f->height, x + (size - w) / 2, y + (size - h) / 2, w > 0 ? w : 1,
                   h > 0 ? h : 1, LIGHT_LEVELS - 1);
}

void items_draw_item(const Item *it, int x, int y)
{
    const ItemType *t = it->type;
    if (!t)
        return;
    const char *file = t->invfile;
    if (it->quality == QUALITY_UNIQUE && it->unique >= 0 && it->unique < s_num_uniques && s_uniques[it->unique].invfile[0])
        file = s_uniques[it->unique].invfile;
    else if (it->quality == QUALITY_SET && it->unique >= 0 && it->unique < s_num_setitems &&
             s_setitems[it->unique].invfile[0])
        file = s_setitems[it->unique].invfile;
    else if (it->gfx > 0 && it->gfx <= 6 && t->invgfx[it->gfx - 1][0])
        file = t->invgfx[it->gfx - 1];
    const Sprite *s = get_gfx(file);
    if (!s)
        s = get_gfx(t->invfile);
    if (!s)
        return;
    /* Centrado donde iría el dibujo de siempre */
    int bw, bh;
    items_inv_size(t, &bw, &bh);
    const SpriteFrame *f = &s->frames[0];
    int dx = x + (bw - f->width) / 2, dy = y + (bh - f->height) / 2;
    fb_blit_spans(f->pixels, f->spans, f->width, f->height, dx, dy);
    int top = it->amount % POTION_CHARGES; /* (en una pila, la de arriba) */
    if (items_is_potion(t) && it->amount > 0 && top > 0) {
        /* Empezada: la parte del líquido que ya se tomó (de arriba, debajo
           del cuello del frasco) queda oscura, un tercio por trago */
        int neck = f->height * POTION_NECK / 100, liquid = f->height - neck;
        int rows = liquid * (POTION_CHARGES - top) / POTION_CHARGES;
        if (rows > 0)
            fb_blit_remap(f->pixels + neck * f->width, f->width, rows, dx, dy + neck, g_light[POTION_EMPTY_LIGHT]);
    }
}

/* ---- Suelo ---- */

/* Hace cuántos ticks está en el piso (el Eco del Odio borra el equipo común viejo) */
int items_ground_age(int i)
{
    return i >= 0 && i < s_ground_top && s_ground[i].used ? (int)(s_ground_now - s_ground[i].since) : -1;
}

int items_ground_add(const Item *it, float x, float y)
{
    static uint32_t s_born;
    int slot = -1;
    for (int i = 0; i < MAX_GROUND; i++)
        if (!s_ground[i].used) {
            slot = i;
            break;
        }
    if (slot < 0) { /* lleno: se pisa el más viejo */
        slot = 0;
        for (int i = 1; i < MAX_GROUND; i++)
            if (s_ground[i].born < s_ground[slot].born)
                slot = i;
    }
    GroundItem *g = &s_ground[slot];
    g->item = *it;
    g->x = x;
    g->y = y;
    g->anim_acc = 0;
    g->used = 1;
    g->born = ++s_born;
    g->since = s_ground_now;
    if (slot >= s_ground_top)
        s_ground_top = slot + 1;
    return slot;
}

void items_ground_tick(void)
{
    s_ground_now++;
    for (int i = 0; i < s_ground_top; i++) {
        GroundItem *g = &s_ground[i];
        if (!g->used)
            continue;
        g->anim_acc++;
        /* El golpe contra el suelo, en el cuadro de la caída que dice la tabla */
        if (g->item.type && g->anim_acc == (g->item.type->drop_frame > 0 ? g->item.type->drop_frame : 1))
            audio_play(g->item.type->drop_sound, g->x, g->y);
    }
}

/* Hasta el último lugar usado (antes, los 512 aunque el piso esté vacío:
   en la Pi eran mil llamadas por cuadro) */
int items_ground_count(void)
{
    return s_ground_top;
}

const Item *items_ground_item(int i, float *x, float *y)
{
    if (i < 0 || i >= MAX_GROUND || !s_ground[i].used)
        return NULL;
    if (x) *x = s_ground[i].x;
    if (y) *y = s_ground[i].y;
    return &s_ground[i].item;
}

void items_ground_remove(int i)
{
    if (i >= 0 && i < MAX_GROUND)
        s_ground[i].used = 0;
    while (s_ground_top > 0 && !s_ground[s_ground_top - 1].used)
        s_ground_top--;
}

void items_ground_clear(void)
{
    memset(s_ground, 0, sizeof s_ground);
    s_ground_top = 0;
}

int items_ground_nearest(float x, float y, float range, int gold_only)
{
    int best = -1;
    float best_d = range * range;
    for (int i = 0; i < s_ground_top; i++) { /* (hasta el último usado: el HUD lo pide en cada cuadro) */
        const GroundItem *g = &s_ground[i];
        if (!g->used)
            continue;
        float dx = g->x - x, dy = g->y - y, d = dx * dx + dy * dy;
        if (d >= best_d)
            continue;
        if (gold_only && !(g->item.type && strcmp(g->item.type->code, "gld") == 0))
            continue;
        best_d = d;
        best = i;
    }
    return best;
}

/* La animación de caída se juega una vez (un cuadro por tick) y queda en el último */
void items_ground_draw(int i, int sx, int sy)
{
    const GroundItem *g = &s_ground[i];
    const Sprite *s = g->item.type ? get_gfx(g->item.type->flippy) : NULL;
    if (!s)
        return;
    int frame = g->anim_acc < s->frames_per_dir ? g->anim_acc : s->frames_per_dir - 1;
    const SpriteFrame *f = &s->frames[frame];
    fb_blit_spans(f->pixels, f->spans, f->width, f->height, sx + f->off_x - f->width / 2, sy + f->off_y);
}

int items_ground_label(int i, char *buf, size_t cap, uint8_t *color)
{
    const GroundItem *g = &s_ground[i];
    if (!g->used || g->anim_acc < 6)
        return 0; /* recién cae: el nombre aparece cuando termina de caer */
    items_label(&g->item, buf, cap);
    *color = items_label_color(&g->item);
    return 1;
}
