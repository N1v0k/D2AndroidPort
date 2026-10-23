#ifndef ITEMS_H
#define ITEMS_H

#include <stddef.h>
#include <stdint.h>

#include "itemstats.h"

/* Objetos del juego (weapons.txt, armor.txt, misc.txt), botín de los
   monstruos (treasureclassex.txt) y objetos tirados en el suelo. Las
   propiedades son las de ItemStatCost.txt (itemstats.h): los afijos
   mágicos, los únicos (uniqueitems.txt), los de conjunto (setitems.txt y
   sets.txt), las gemas y las runas engarzadas (gems.txt) y las palabras
   rúnicas (runes.txt). */

typedef enum { ITEM_WEAPON, ITEM_ARMOR, ITEM_MISC } ItemKind;

/* Dónde se equipa cada objeto (el amuleto y los anillos van al final: las
   partidas guardadas usan estos números) */
typedef enum {
    SLOT_NONE = -1,
    SLOT_WEAPON,
    SLOT_SHIELD,
    SLOT_BODY,
    SLOT_HELM,
    SLOT_GLOVES,
    SLOT_BOOTS,
    SLOT_BELT,
    SLOT_AMULET,
    SLOT_RING1,
    SLOT_RING2,
    SLOT_COUNT
} ItemSlot;

typedef struct ItemType {
    char        code[5];
    const char *name; /* traducido */
    ItemKind    kind;
    char        type[5]; /* tipo de itemtypes.txt: "axe", "tors", "helm", "hpot", "gold"... */
    char        type2[5]; /* otro tipo (las gemas: "gema" y "gem0", la calidad) */
    ItemSlot    slot;
    int         level, level_req, rarity, spawnable;
    int         min_dmg, max_dmg; /* armas */
    int         min_ac, max_ac;   /* armaduras */
    int         smite_min, smite_max, block; /* escudos: daño de Castigar y % de bloqueo */
    int         req_str, req_dex;
    int         two_handed;
    char        wclass[4]; /* clase de animación del arma ("1hs", "stf"...) */
    char        gfx[4];    /* cómo se dibuja en el personaje (alternategfx: "hax", "buc", "cap") */
    /* Armaduras de cuerpo: pieza en brazo der., brazo izq., torso, piernas,
       hombrera der. e izq. (0 = liviana, 1 = media, 2 = pesada, -1 = no aplica) */
    int8_t      body_parts[6];
    int         inv_w, inv_h;
    char        flippy[16]; /* animación al caer al suelo */
    char        invfile[16]; /* dibujo en el inventario */
    /* Pociones (misc.txt): vida o maná que dan y en cuántos ticks */
    int         heal_life, heal_mana, heal_ticks, heal_pct;
    int         stamina_ticks;
    int         antidote_ticks, thaw_ticks; /* antídoto (veneno a la mitad) y deshielo (frío a la mitad) */
    int         cost;           /* precio base */
    /* Lo que se apila: flechas, saetas y armas arrojadizas (amount = cantidad) */
    int         stackable, max_stack, spawn_stack;
    int         min_mis_dmg, max_mis_dmg; /* daño al lanzarla (jabalinas, hachas arrojadizas) */
    int         str_bonus, dex_bonus;     /* % de daño por punto de fuerza y de destreza (x100) */
    int         ammo;           /* carcaj: 1 = flechas, 2 = saetas */
    int         gender;         /* género del nombre, para los adjetivos de los mágicos */
    int8_t      vendor_min[16]; /* cuántos venden Akara, Charsi, Gheed, Fara, Lysander, Drognan, Elzix, Ormus, Alkor, Asheara, Hratli, Halbu, Jamella, Malah, Larzuk y Anya (VENDOR_*) */
    int8_t      vendor_max[16];
    /* Sonidos (sounds.txt): al caer o guardarlo, al usarlo (tomar la poción)
       y en qué cuadro de la caída suena */
    int         drop_sound, use_sound, drop_frame;
    int         hit_class;      /* armas: hitclass.txt (1 = mano, 2 = una mano chica...) */
    /* Lo que dice itemtypes.txt de su tipo */
    int         gemsockets;     /* cuántos engarces puede tener (gemsockets) */
    int         gamble_cost;    /* "gamble cost": lo que cuesta apostar por un anillo o un amuleto */
    int         auto_group;     /* "auto prefix": el grupo de automagic.txt que trae de fábrica (los de clase) */
    int         max_sock[3], sock_lvl[2]; /* MaxSockets1..3 según el nivel del objeto (umbrales) */
    int         always_normal, always_magic, can_rare; /* Normal, Magic y Rare de itemtypes.txt */
    int         uber;           /* excepcional o de élite (para ItemRatio.txt) */
    int         tier;           /* 0 normal, 1 excepcional, 2 élite */
    char        ubercode[5], ultracode[5]; /* la versión excepcional y la de élite */
    char        nm_upgrade[5], hell_upgrade[5]; /* lo que venden en su lugar en Pesadilla e Infierno ("" = lo mismo) */
    int         class_only;     /* de una sola clase (Class de itemtypes.txt) */
    int         var_inv;        /* cuántos dibujos tiene (anillos, amuletos, joyas, talismanes) */
    char        invgfx[6][16];  /* InvGfx1..6 de su tipo */
    int         socket_filler;  /* gema, runa o joya: se engarza */
    char        better_gem[5];  /* la gema de la calidad que sigue (BetterGem de misc.txt; "" = ninguna) */
    int         charm;          /* talismán: sirve en la mochila */
} ItemType;

enum { VENDOR_AKARA, VENDOR_CHARSI, VENDOR_GHEED, VENDOR_FARA, VENDOR_LYSANDER, VENDOR_DROGNAN, VENDOR_ELZIX,
       VENDOR_ORMUS, VENDOR_ALKOR, VENDOR_ASHEARA, VENDOR_HRATLI, VENDOR_HALBU, VENDOR_JAMELLA,
       VENDOR_MALAH, VENDOR_LARZUK, VENDOR_ANYA, /* (la de Anya la usa también Nihlathak, hasta que se va) */
       VENDOR_COUNT }; /* (los arreglos de ItemType son de 16) */

/* Las propiedades que usa el motor (los *ID de ItemStatCost.txt) */
enum {
    STAT_STR = 0,
    STAT_ENERGY = 1,
    STAT_DEX = 2,
    STAT_VIT = 3,
    STAT_LIFE = 7,
    STAT_MANA = 9,
    STAT_STAMINA = 11,
    STAT_AC_PCT = 16,        /* % de defensa de la armadura */
    STAT_DMG_PCT = 17,       /* % de daño del arma (el máximo; el mínimo es el 18) */
    STAT_DMG_MIN_PCT = 18,
    STAT_AR = 19,            /* puntería */
    STAT_BLOCK = 20,         /* % más de bloqueo */
    STAT_DMG_MIN = 21,
    STAT_DMG_MAX = 22,
    STAT_DMG_PCT_ALL = 25,   /* % de daño (damagepercent: de las pasivas y algunos objetos) */
    STAT_MANA_REGEN = 27,    /* % de regeneración de maná */
    STAT_STAM_REGEN = 28,    /* % más rápido se recupera la estamina */
    STAT_AC = 31,
    STAT_AC_MISSILE = 32,
    STAT_AC_HTH = 33,
    STAT_RED_DMG = 34,       /* daño que se descuenta de cada golpe recibido */
    STAT_RED_MAG = 35,       /* daño mágico que se descuenta */
    STAT_RED_DMG_PCT = 36,   /* % de daño físico que se descuenta */
    STAT_RES_MAG = 37,
    STAT_RES_FIRE = 39,
    STAT_RES_FIRE_MAX = 40,
    STAT_RES_LTNG = 41,
    STAT_RES_LTNG_MAX = 42,
    STAT_RES_COLD = 43,
    STAT_RES_COLD_MAX = 44,
    STAT_RES_POIS = 45,
    STAT_RES_POIS_MAX = 46,
    STAT_FIRE_MIN = 48,
    STAT_FIRE_MAX = 49,
    STAT_LTNG_MIN = 50,
    STAT_LTNG_MAX = 51,
    STAT_MAG_MIN = 52,
    STAT_MAG_MAX = 53,
    STAT_COLD_MIN = 54,
    STAT_COLD_MAX = 55,
    STAT_COLD_LEN = 56,
    STAT_POIS_MIN = 57,
    STAT_POIS_MAX = 58,
    STAT_POIS_LEN = 59,
    STAT_LIFESTEAL = 60,     /* % del daño que vuelve como vida */
    STAT_MANASTEAL = 62,
    STAT_REGEN = 74,         /* regeneración de vida */
    STAT_LIFE_PCT = 76,
    STAT_MANA_PCT = 77,
    STAT_THORNS = 78,        /* el atacante recibe daño */
    STAT_GOLD_PCT = 79,      /* % más de oro */
    STAT_MAGIC_FIND = 80,    /* % más de probabilidad de objetos mágicos */
    STAT_KNOCKBACK = 81,
    STAT_CLASS_SKILLS = 83,  /* + a las habilidades de una clase (param: la clase del original) */
    STAT_HEAL_KILL = 86,     /* vida por cada monstruo muerto */
    STAT_LIGHT = 89,         /* radio de luz */
    STAT_REQ_PCT = 91,       /* % de los requisitos */
    STAT_IAS = 93,           /* % de velocidad de ataque */
    STAT_MOVE = 96,          /* % de velocidad al caminar y correr */
    STAT_OSKILL = 97,        /* + a una habilidad de cualquier clase (param: la habilidad) */
    STAT_FHR = 99,           /* recuperación de golpes más rápida */
    STAT_FBR = 102,          /* bloqueo más rápido */
    STAT_FCR = 105,          /* lanzamiento más rápido */
    STAT_SINGLE_SKILL = 107, /* + a una habilidad de su clase (param: la habilidad) */
    STAT_POIS_LEN_RES = 110, /* % menos duración del veneno */
    STAT_DMG_FLAT = 111,     /* + de daño (al mínimo y al máximo) */
    STAT_HOWL = 112,         /* el golpe hace huir (en 1/128) */
    STAT_BLIND = 113,
    STAT_DMG_TO_MANA = 114,
    STAT_IGNORE_AC = 115,
    STAT_TARGET_AC = 116,    /* -% de defensa del objetivo */
    STAT_HALF_FREEZE = 118,
    STAT_AR_PCT = 119,
    STAT_DMG_DEMON = 121,
    STAT_DMG_UNDEAD = 122,
    STAT_AR_DEMON = 123,
    STAT_AR_UNDEAD = 124,
    STAT_ELEM_SKILLS = 126,  /* + a las habilidades de fuego (param 1) */
    STAT_ALL_SKILLS = 127,
    STAT_LTNG_THORNS = 128,
    STAT_FREEZE = 134,
    STAT_OPEN_WOUNDS = 135,
    STAT_CRUSH = 136,
    STAT_KICK = 137,
    STAT_MANA_KILL = 138,    /* maná por cada monstruo muerto */
    STAT_DEADLY = 141,
    STAT_ABS_FIRE_PCT = 142,
    STAT_ABS_FIRE = 143,
    STAT_ABS_LTNG_PCT = 144,
    STAT_ABS_LTNG = 145,
    STAT_ABS_MAG_PCT = 146,
    STAT_ABS_MAG = 147,
    STAT_ABS_COLD_PCT = 148,
    STAT_ABS_COLD = 149,
    STAT_SLOW = 150,
    STAT_AURA = 151,
    STAT_NOFREEZE = 153,
    STAT_PIERCE = 156,
    STAT_SKILL_TAB = 188,    /* + a una solapa (param: clase << 3 | solapa) */
    STAT_FIRE_MASTERY = 329, /* % de daño de fuego de las habilidades */
    STAT_LTNG_MASTERY = 330,
    STAT_COLD_MASTERY = 331,
    STAT_POIS_MASTERY = 332,
    STAT_FIRE_PIERCE = 333,  /* - a la resistencia al fuego de los monstruos */
    STAT_LTNG_PIERCE = 334,
    STAT_COLD_PIERCE = 335,
    STAT_POIS_PIERCE = 336,
    STAT_MAG_MASTERY = 357,  /* % de daño mágico de las habilidades (D2R) */
    STAT_MAG_PIERCE = 358,   /* - a la resistencia a la magia */
    STAT_COUNT = ISC_MAX
};

/* Calidad: los números de las partidas viejas (0, 1, 2) no cambian */
enum { QUALITY_NORMAL, QUALITY_MAGIC, QUALITY_RARE, QUALITY_SET, QUALITY_UNIQUE, QUALITY_RUNEWORD,
       QUALITY_SUPERIOR, QUALITY_LOW, QUALITY_CRAFTED };

#define ITEM_MAX_STATS   32
#define ITEM_MAX_SOCKETS 6
#define POTION_CHARGES   3 /* tragos de cada poción (amount = los que le quedan) */
/* Pilas en la mochila (pedido del usuario; el original no apila pociones y
   las llaves van de a 12): en una pila de pociones amount son los tragos de
   todas (la de arriba es la empezada) */
#define POTION_STACK     10
#define KEY_STACK        20
/* Los Pergaminos de Portal también (pedido del usuario: no hay Tomo de
   Portal); en uno, amount es cuántos hay */
#define TP_STACK         20

typedef struct {
    const ItemType *type; /* NULL = vacío */
    int             ac;     /* defensa tirada (armaduras) */
    int             amount; /* cantidad de oro */
    uint8_t         quality;
    uint8_t         num_stats;
    uint8_t         sockets;    /* engarces (0 = ninguno) */
    uint8_t         nsocketed;  /* cuántos están ocupados */
    uint8_t         ilvl;       /* nivel del objeto (el del monstruo que lo soltó) */
    uint8_t         gfx;        /* cuál de los dibujos (anillos, amuletos...): 0 = el de siempre */
    int16_t         unique;     /* su fila de uniqueitems.txt, setitems.txt o runes.txt (-1 = ninguna) */
    uint16_t        stat[ITEM_MAX_STATS];
    int16_t         param[ITEM_MAX_STATS];
    int32_t         value[ITEM_MAX_STATS];
    char            socketed[ITEM_MAX_SOCKETS][5]; /* lo engarzado (código de misc.txt) */
    char            name[64]; /* nombre de los mágicos, raros, únicos...: "Hacha de mano roída del zorro" */
} Item;

int             items_init(void);
const ItemType *items_find(const char *code);
Item            items_make(const ItemType *t);
int             items_type_count(void);
const ItemType *items_type(int i);
int             items_supported(const ItemType *t); /* el motor sabe usarlo */
int             items_is_potion(const ItemType *t); /* de las del cinturón: vida, maná, rejuvenecimiento, estamina */
int             items_type_is(const ItemType *t, const char *type); /* "axe" es "mele" y "weap" */
int             items_itype_exists(const char *type); /* es un tipo de itemtypes.txt ("hpot", "gem0") */
const char     *items_class(const ItemType *t); /* "ama", "bar"...: solo esa clase lo usa; "" = todas */
int             items_ammo_for(const ItemType *weapon); /* 1 = arco (flechas), 2 = ballesta (saetas), 0 = nada */
int             items_can_throw(const ItemType *weapon); /* jabalinas, cuchillos y hachas arrojadizas */
/* ¿Puede ir `offhand` en la mano del escudo con ese arma? (los carcajes sí, aunque sea a dos manos) */
int             items_offhand_ok(const ItemType *weapon, const ItemType *offhand);
/* La propiedad que traen de fábrica los objetos de clase (automagic.txt, por
   su "auto prefix"): los arcos y las lanzas de la Amazona, los orbes de la
   Hechicera, los escudos del Paladín, las cabezas del Nigromante y los
   grimorios del Brujo. No en los únicos, los de conjunto ni las palabras rúnicas */
void            items_add_automagic(Item *it, int ilvl);
/* ¿Va en ese lugar? (los anillos van en cualquiera de los dos) */
int             items_slot_ok(const ItemType *t, int slot);
int             items_price(const Item *it, int selling); /* comprar o vender a un NPC */
/* La apuesta (Gheed, Elzix), como D2GAME_STORES_FillGamble: el objeto número
   `k` de la lista de 14 de un personaje de nivel `clvl` (el 0 un anillo, el 1
   un amuleto, los demás de gamble.txt hasta el nivel del objeto), con su
   calidad ya tirada; y lo que cuesta (ITEMS_CalculateTransactionCost) */
int             items_gamble_roll(int k, int clvl, Item *out);
int             items_gamble_price(const ItemType *t, int clvl);
int             items_level_req(const Item *it); /* nivel que pide (el del objeto, el del único, las runas...) */

/* Lo que suelta un monstruo según su clase de tesoro; devuelve cuántos. La
   calidad de cada uno se tira como en el original (ItemRatio.txt y lo que
   suma la clase de tesoro), con la búsqueda de magia `mf` (%). */
int items_roll_drop(const char *treasure_class, int monster_level, int mf, Item *out, int max);
/* Lo mismo con la calidad pedida (QUALITY_MAGIC, QUALITY_SUPERIOR...) en lo
   que la puede tener, como el nQuality de OBJMODE_DropFromChestTCWithQuality
   y el ITEMDROPFLAG_SUPERIOR de los estantes; el resto (oro, pociones) sale normal */
int items_roll_drop_quality(const char *treasure_class, int item_level, int mf, int quality, Item *out, int max);

/* Vuelve mágico un arma o armadura con prefijo y/o sufijo de nivel <= item_level */
int items_make_magic(Item *it, int item_level);
/* Lo vuelve raro: de 3 a 6 propiedades y un nombre de rareprefix.txt y
   raresuffix.txt ("Mordisco bestial"). Lo usa la imbuición de Charsi. */
int items_make_rare(Item *it, int item_level);
/* Único o de conjunto de ese objeto (si hay uno de nivel <= item_level); 0 si no */
int items_make_unique(Item *it, int item_level);
/* El Amuleto de la Víbora, el Bastón de los Reyes y el Báculo Horádrico son
   únicos (uniqueitems.txt): con su nombre y sus propiedades. 1 si lo era. */
int items_quest_unique(Item *it);
int items_make_set(Item *it, int item_level);
void items_drop_from_bovine(int on); /* lo que se tira ahora es de una vaca (el conjunto del Rey Vaca) */
/* Le pone engarces al azar (los blancos que caen con agujeros) */
void items_add_sockets(Item *it, int item_level);
int  items_larzuk_sockets(Item *it); /* la recompensa de Larzuk: los engarces que admite (o uno); 0 = no admite */

/* Para el Cubo Horádrico (cube.c): mágico con un prefijo y/o sufijo fijo
   (su fila de magicprefix.txt / magicsuffix.txt; 0 = al azar), una
   propiedad de las tablas, pasarlo a excepcional o élite, la clave del
   único ("The Stone of Jordan") y artesanal (raro con nombre propio) */
int         items_make_magic_affix(Item *it, int item_level, int prefix_row, int suffix_row);
void        items_apply_prop(Item *it, const char *code, const char *par, int min, int max);
int         items_upgrade(Item *it, int elite);
const char *items_unique_key(const Item *it);
int         items_make_crafted(Item *it, int item_level);
const char *items_affix_name(int suffix, int row); /* el nombre traducido de ese afijo ("" si no está) */
/* El dibujo del inventario achicado a size x size (centrado), para listas */
void        items_draw_icon(const ItemType *t, int x, int y, int size);

/* Engarzar: ¿se puede poner `filler` (gema, runa, joya) en `host`? y
   ponerlo (suma sus propiedades; si completa una palabra rúnica, la arma) */
int items_can_socket(const Item *host, const Item *filler);
int items_socket(Item *host, const Item *filler);

/* Conjuntos: el conjunto del objeto (-1 = ninguno), y lo que dan las piezas
   puestas de cada conjunto (las verdes de cada pieza y las doradas del
   conjunto) */
int items_set_of(const Item *it);
int items_set_bonus(const Item *const *worn, int n, ItemProp *out, int cap);

const char *items_label(const Item *it, char *buf, size_t cap); /* "Hacha de mano", "15 de oro" */

/* Las palabras rúnicas de runes.txt, para la guía: cuántas, su nombre, sus
   runas en orden, dónde se pueden hacer ("Espadas, hachas"), el nivel que
   piden (el de la runa más alta) y sus propiedades con los rangos del
   original ("+25-35% ..."), armadas sobre una base que las acepta (0 si no
   hay ninguna) */
int         items_runeword_count(void);
const char *items_runeword_name(int w);
int         items_runeword_runes(int w, const ItemType **out); /* hasta 6 */
void        items_runeword_where(int w, char *buf, size_t cap);
int         items_runeword_level(int w);
int         items_runeword_describe(int w, int clvl, char lines[][112], int max_lines);
int         items_stat(const Item *it, int stat); /* suma de esa propiedad en el objeto */
void        items_damage(const Item *it, int *dmin, int *dmax); /* daño del arma con sus propiedades */
void        items_thrown_damage(const Item *it, int *dmin, int *dmax); /* lanzándola */
int         items_defense(const Item *it);                     /* defensa con sus propiedades */
/* Las líneas de las propiedades, como las muestra el juego (clvl: el nivel
   del personaje, para las que suben con el nivel) */
int         items_describe(const Item *it, int clvl, char lines[][112], int max_lines);
/* Lo de abajo del cartel: "Engarzado (2)", y en los de conjunto, el nombre
   del conjunto, las piezas (puestas o no) y lo que dan */
int         items_describe_extra(const Item *it, const Item *const *worn, int nworn, int clvl, char lines[][112],
                                 uint8_t *colors, int max_lines);
/* Texto de la propiedad N del objeto (lo mismo que items_describe); 0 si no hay más */
int         items_stat_line(const Item *it, int n, char *buf, size_t cap);
uint8_t     items_label_color(const Item *it);

/* Para guardar la partida: el objeto como una línea de texto y de vuelta. 0 si no se entiende. */
void        items_to_text(const Item *it, char *buf, size_t cap);
int         items_from_text(const char *text, Item *it);
void        items_draw_inv(const ItemType *t, int x, int y); /* dibujo del inventario, esquina sup. izq. */
void        items_draw_item(const Item *it, int x, int y);   /* el de ese objeto (los únicos y los anillos tienen el suyo) */
void        items_inv_size(const ItemType *t, int *w, int *h); /* tamaño de ese dibujo */

/* Objetos en el suelo */
int         items_ground_add(const Item *it, float x, float y);
void        items_ground_tick(void);
int         items_ground_count(void);
const Item *items_ground_item(int i, float *x, float *y); /* NULL si la casilla está libre */
void        items_ground_remove(int i);
int         items_ground_nearest(float x, float y, float range, int gold_only); /* -1 si no hay */
void        items_ground_draw(int i, int sx, int sy);
/* Texto y color del cartel con el nombre; 0 si todavía está cayendo */
int         items_ground_label(int i, char *buf, size_t cap, uint8_t *color);
void        items_ground_clear(void);

#endif
