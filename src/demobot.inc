/* El bot de la demostración (el menú quieto, o --autotest act1 con
   D2_AUTOTEST_DEMO). Es parte de game.c: se incluye al final de autotest.inc
   y usa sus pasos, pero además juega como una persona:

   - Reparte los puntos de atributo según lo que pide el equipo que quiere
     usar (y si no, con la proporción de su clase) y los de habilidad con un
     plan por clase. Pone las habilidades en los botones (B la de pegar, X el
     grito o Percepción, Y Grito de batalla o la jabalina de lejos) y las usa.
   - Pelea con lo que se le cruza, levanta lo que le sirve (un arma o una
     armadura mejor que pueda usar, pociones, oro) y se lo pone. En el pueblo
     vende lo que no usa, compra pociones y las mejoras que le alcancen.
   - Antes de ir por cada jefe revisa si está listo (nivel y vida). Si no,
     farmea en la zona más conveniente de las que conoce (la de monstruos de
     más nivel que todavía le convengan) hasta estarlo, con un tope de tiempo.
   - Con poca vida toma pociones; sin pociones vuelve al pueblo a curarse.

   En la demostración los jugadores no mueren (la vida no baja de 1) y cada
   cosa que deciden queda en el log, para encontrar lo que anda mal.

   La del menú (s_demo_all) empieza con nivel 30 a 60 (según el acto) y todas
   las habilidades, y las va usando una por una (ver "Todas las habilidades", más abajo). */

#define DEMO_IGNORE      16
#define DEMO_FARM_CAP    (15 * 60 * TICK_RATE) /* tope de farmeo por jefe */
#define DEMO_ENGAGE      7.0f                  /* pelea con lo que tenga a esta distancia */
#define DEMO_LOOT_RANGE  12.0f
#define DEMO_LEVELS      64

typedef struct {
    int      id;
    uint32_t until;
} DemoIgnore;

typedef struct {
    int         retreat;        /* 0 no, 1 yendo al pueblo, 2 curándose, 3 volviendo */
    int         ret_wp;         /* waypoint al que vuelve después de curarse */
    const Level *farm;          /* nivel donde farmea (NULL = ninguno) */
    int         farm_need;      /* para qué jefe (k_demo_needs), -1 = ninguno */
    uint32_t    farm_start;
    uint32_t    farm_ticks;     /* total farmeando, para el informe */
    int         gave_up;        /* jefes para los que dejó de farmear (un bit cada uno) */
    int         ready_told;     /* jefes para los que ya dijo que está listo */
    uint64_t    seen_levels;    /* niveles por los que pasó (bit = índice de world_level) */
    uint64_t    barren;         /* niveles donde farmeó sin ganar experiencia: no vuelve */
    uint32_t    farm_exp;       /* experiencia al llegar al nivel donde farmea */
    int         farm_in;        /* ya llegó (para medir lo que ganó ahí) */
    int         routing;        /* viajando por waypoint al destino del paso */
    uint32_t    cleared[DEMO_LEVELS]; /* cuándo limpió cada nivel (tick), 0 = nunca */
    int         target, target_hp; /* el monstruo con el que pelea y su vida hace un rato */
    uint32_t    target_since;
    DemoIgnore  ignore[DEMO_IGNORE]; /* monstruos a los que no llega */
    int         hunt;           /* monstruo al que va (para ver si se acerca), -1 = ninguno */
    float       hunt_best;      /* lo más cerca que llegó */
    uint32_t    hunt_since;     /* desde cuándo no se acerca */
    float       stuck_dx, stuck_dy; /* hacia dónde se trabó la última vez */
    uint32_t    stuck_until;    /* hasta cuándo evita esa dirección */
    uint32_t    special_at;     /* cuándo puede volver a usar Aturdir o Empalar */
    uint32_t    taunt_at, howl_at, search_at;
    int         item;           /* objeto del suelo que va a buscar, -1 = ninguno */
    char        item_name[64];
    float       item_x, item_y;
    uint32_t    item_since;
    DemoIgnore  ignore_item[DEMO_IGNORE];
    uint32_t    shopped;        /* última vez que pasó por las tiendas */
    uint32_t    cry_at, cry2_at; /* cuándo puede volver a gritar (X e Y) */
    uint32_t    potion_at;      /* cuándo puede tomar otra poción */
    int         saves_seen;     /* veces que la vida no bajó de 1 (ya contadas) */
    /* Para el informe */
    int         potions, equipped, bought, sold, retreats, near_death, kills_start;
    int         uses[MAX_CLASS_SKILLS];
    uint32_t    boss_start;
    int         boss_hp_min, boss_potions;
    /* Todas las habilidades, una por una (s_demo_all) */
    int         show_list[MAX_CLASS_SKILLS], show_count, show_cur;
    int         show_presses;   /* veces que usó la de ahora */
    uint32_t    show_ticks;     /* cuánto peleó con la de ahora */
    uint32_t    show_until;     /* la de apoyo ya usada: cuándo pasa a la siguiente */
    uint32_t    ward_at;        /* cuándo puede volver a poner el Tótem lúgubre */
    uint32_t    equip_at;       /* no se pudo cambiar (la mochila llena): cuándo prueba de nuevo */
    int         escapes;        /* intentos de salir de un hueco cerrado (demo_escape) */
} DemoBrain;

static DemoBrain s_brain[MAX_PLAYERS];
static int       s_demo_all;     /* nivel 30 y todas las habilidades, una por una */
static int       s_show_resume[NUM_CLASSES]; /* por clase: la próxima demostración sigue desde ahí */

/* Lo que hace falta para ir por cada jefe: nivel y vida. Se revisa al
   empezar el paso que va hasta su nivel. */
typedef struct {
    const char *level; /* el paso AT_GOTO_LEVEL que lleva hasta él */
    const char *boss;  /* para el log */
    int         clvl, life;
} DemoNeed;

static const DemoNeed k_demo_needs[] = {
    { "Den of Evil", "Corpsefire", 3, 0 },
    { "Burial Grounds", "Blood Raven", 5, 80 },
    { "Tower Cellar Level 5", "la Condesa", 8, 110 },
    { "Barracks", "el Herrero", 8, 110 },
    { "Catacombs Level 4", "Andariel", 12, 150 },
    { "Sewers Level 3", "Radament", 16, 200 },
    { "Arcane Sanctuary", "el Invocador", 20, 250 },
    { "Duriel's Lair", "Duriel", 24, 300 },
};
#define DEMO_NEEDS ((int)(sizeof k_demo_needs / sizeof k_demo_needs[0]))

/* Plan de habilidades: en orden, hasta qué nivel subir cada una (las que
   todavía no se pueden aprender se saltean hasta que se pueda).
   "@mastery" es el dominio del arma que tenga puesta. */
typedef struct {
    const char *key;
    int         level;
} DemoSkillGoal;

/* El Bárbaro sube primero el daño (Porrazo y el dominio de su arma); Gritar
   (que pide Aullido) para la defensa, y Concentración y Grito de batalla
   cuando llega al nivel */
static const DemoSkillGoal k_barb_plan[] = {
    { "Bash", 1 }, { "@mastery", 1 }, { "Bash", 3 }, { "Howl", 1 }, { "Shout", 1 }, { "Stun", 1 },
    { "Find Potion", 1 }, { "Taunt", 1 }, { "@mastery", 3 }, { "Bash", 5 }, { "@mastery", 5 }, { "Bash", 8 },
    { "Iron Skin", 1 }, { "Find Item", 1 }, { "@mastery", 8 },
    { "Concentrate", 1 }, { "Battle Cry", 1 }, { "Concentrate", 8 }, { "Shout", 4 },
    { "@mastery", 14 }, { "Concentrate", 20 }, { "@mastery", 20 }, { "Battle Cry", 8 }, { "Shout", 20 },
    { "Iron Skin", 20 },
};
static const DemoSkillGoal k_ama_plan[] = {
    { "Jab", 1 }, { "Inner Sight", 1 }, { "Critical Strike", 1 }, { "Jab", 2 }, { "Power Strike", 1 },
    { "Dodge", 1 }, { "Poison Javelin", 1 }, { "Power Strike", 4 }, { "Lightning Bolt", 1 },
    { "Critical Strike", 4 }, { "Impale", 1 }, { "Power Strike", 8 }, { "Lightning Bolt", 5 },
    { "Plague Javelin", 1 }, { "Power Strike", 15 }, { "Lightning Bolt", 12 }, { "Critical Strike", 10 },
    { "Power Strike", 20 }, { "Lightning Bolt", 20 }, { "Critical Strike", 20 },
};

/* La Hechicera: Descarga ígnea y Armadura helada al principio, después
   Bola de fuego (con sus sinergias), Campo estático para los jefes y
   Teletransporte; Meteoro y el dominio cuando llega */
static const DemoSkillGoal k_sor_plan[] = {
    { "Fire Bolt", 1 }, { "Frozen Armor", 1 }, { "Fire Bolt", 4 }, { "Charged Bolt", 1 }, { "Static Field", 1 },
    { "Frost Nova", 1 }, { "Warmth", 1 }, { "Fire Ball", 1 }, { "Fire Bolt", 6 }, { "Fire Ball", 6 },
    { "Telekinesis", 1 }, { "Teleport", 1 }, { "Fire Ball", 12 }, { "Shiver Armor", 1 }, { "Energy Shield", 1 },
    { "Meteor", 1 }, { "Fire Mastery", 1 }, { "Fire Ball", 20 }, { "Meteor", 20 }, { "Fire Mastery", 20 },
    { "Fire Bolt", 20 },
};

/* El Paladín: Poder y Castigar al principio, Plegaria, después Fuego sagrado
   (con Resistir el fuego, su sinergia) y Celo; Carga para llegar */
static const DemoSkillGoal k_pal_plan[] = {
    { "Might", 1 }, { "Smite", 1 }, { "Sacrifice", 1 }, { "Prayer", 1 }, { "Might", 3 }, { "Holy Fire", 1 },
    { "Resist Fire", 1 }, { "Holy Fire", 4 }, { "Smite", 3 }, { "Zeal", 1 }, { "Holy Fire", 8 }, { "Zeal", 4 },
    { "Charge", 1 }, { "Holy Fire", 12 }, { "Zeal", 10 }, { "Holy Fire", 20 }, { "Zeal", 20 }, { "Might", 20 },
};

/* El Nigromante: Dientes para pegar de lejos y Explosión de cadáver para los
   grupos (lo que más mata al principio), Amplificar daño, un par de
   esqueletos y el Gólem de arcilla para que lo cubran, Armadura de hueso;
   después Lanza de hueso */
static const DemoSkillGoal k_nec_plan[] = {
    { "Teeth", 1 }, { "Raise Skeleton", 1 }, { "Amplify Damage", 1 }, { "Teeth", 3 }, { "Bone Armor", 1 },
    { "Corpse Explosion", 1 }, { "Clay Golem", 1 }, { "Teeth", 6 }, { "Corpse Explosion", 3 },
    { "Skeleton Mastery", 1 }, { "Raise Skeleton", 3 }, { "Teeth", 10 }, { "Corpse Explosion", 6 },
    { "Bone Spear", 1 }, { "Bone Spear", 10 }, { "Teeth", 15 }, { "Corpse Explosion", 12 }, { "Bone Spear", 20 },
    { "Raise Skeleton", 8 }, { "Skeleton Mastery", 8 }, { "Corpse Explosion", 20 }, { "Teeth", 20 },
};

/* La Asesina de trampas: Trauma de fuego para pegar de lejos, Campo de
   choque, Rapidez, la Estela de fuego y el Centinela de centellas; el
   Manto de sombras para los grupos y los jefes */
static const DemoSkillGoal k_ass_plan[] = {
    { "Fire Trauma", 1 }, { "Claw Mastery", 1 }, { "Fire Trauma", 4 }, { "Shock Field", 1 }, { "Quickness", 1 },
    { "Fire Trauma", 7 }, { "Charged Bolt Sentry", 1 }, { "Wake of Fire Sentry", 1 }, { "Wake of Fire Sentry", 5 },
    { "Fire Trauma", 10 }, { "Cloak of Shadows", 1 }, { "Wake of Fire Sentry", 10 }, { "Charged Bolt Sentry", 5 },
    { "Fire Trauma", 15 }, { "Wake of Fire Sentry", 20 }, { "Lightning Sentry", 1 }, { "Lightning Sentry", 20 },
    { "Fire Trauma", 20 },
};

/* El Druida elemental: Tormenta de fuego para pegar de lejos, cuervos y
   lobos espirituales que lo cubren, el Sabio roble; Roca fundida, Fisura
   para los grupos, la Armadura de ciclón y después el Volcán */
static const DemoSkillGoal k_dru_plan[] = {
    { "Firestorm", 1 }, { "Raven", 1 }, { "Firestorm", 4 }, { "Summon Spirit Wolf", 1 }, { "Oak Sage", 1 },
    { "Molten Boulder", 1 }, { "Firestorm", 7 }, { "Summon Spirit Wolf", 3 }, { "Molten Boulder", 4 },
    { "Eruption", 1 }, { "Cyclone Armor", 1 }, { "Eruption", 6 }, { "Firestorm", 12 }, { "Eruption", 12 },
    { "Volcano", 1 }, { "Eruption", 20 }, { "Firestorm", 20 }, { "Volcano", 10 }, { "Molten Boulder", 20 },
};

/* El Brujo del caos con demonios: Descarga de miasma para pegar de lejos y
   el hombre cabra que lo cubre; Anillo de fuego para los que se acercan, el
   Sigilo de letargo, el Dominio demoníaco (más demonios), Cadena de miasma
   para los grupos, el mancillado y la Ola de llamas; Hecatombe y Abismo
   cuando llega */
static const DemoSkillGoal k_war_plan[] = {
    { "Miasma Bolt", 1 }, { "Summon Goatman", 1 }, { "Miasma Bolt", 4 }, { "Ring of Fire", 1 },
    { "Sigil Lethargy", 1 }, { "Demonic Mastery", 1 }, { "Miasma Bolt", 7 }, { "Ring of Fire", 4 },
    { "Miasma Chains", 1 }, { "Summon Tainted", 1 }, { "Demonic Mastery", 5 }, { "Miasma Chains", 6 },
    { "Flame Wave", 1 }, { "Miasma Chains", 12 }, { "Flame Wave", 8 }, { "Apocalypse", 1 }, { "Abyss", 1 },
    { "Miasma Chains", 20 }, { "Flame Wave", 20 }, { "Apocalypse", 10 }, { "Demonic Mastery", 10 },
    { "Ring of Fire", 20 },
};

static const char *const k_attr_names[4] = { "fuerza", "destreza", "vitalidad", "energía" };

/* ---- Lo que sabe de sí mismo ---- */

static int demo_level_index(const Level *lv)
{
    for (int k = 0; k < world_level_count() && k < DEMO_LEVELS; k++)
        if (world_level(k) == lv)
            return k;
    return -1;
}

/* Zonas que hay que cruzar caminando de una a otra (-1 = no se llega) */
static int demo_hops(int from, int to)
{
    int hops = 0;
    while (from != to && from >= 0 && hops < 64) {
        from = world_route_next(from, to);
        hops++;
    }
    return from == to ? hops : -1;
}

/* El waypoint activado más cerca caminando (-1 = ninguno) */
static int demo_nearest_wp(const Player *p, int *hops_out)
{
    int best = -1, best_hops = 1 << 20, z = zone_at(p->x, p->y);
    for (int k = 0; k < WP_MAX; k++) {
        if (!((p->waypoints >> k) & 1) || world_waypoint_zone(k) < 0)
            continue;
        int h = demo_hops(z, world_waypoint_zone(k));
        if (h >= 0 && h < best_hops) {
            best = k;
            best_hops = h;
        }
    }
    if (hops_out)
        *hops_out = best_hops;
    return best;
}

static int demo_ignored(const DemoIgnore *list, int id)
{
    for (int k = 0; k < DEMO_IGNORE; k++)
        if (list[k].id == id && list[k].until > s_ticks)
            return 1;
    return 0;
}

static void demo_ignore(DemoIgnore *list, int id, int seconds);

/* Lo que dejó en el piso para hacer lugar a lo de la misión: no lo levanta
   de nuevo por un rato */
static void demo_forget_item(const Player *p, int ground)
{
    if (ground >= 0)
        demo_ignore(s_brain[p - s_pl].ignore_item, ground, 600);
}

static void demo_ignore(DemoIgnore *list, int id, int seconds)
{
    int slot = 0;
    for (int k = 0; k < DEMO_IGNORE; k++)
        if (list[k].until <= s_ticks) {
            slot = k;
            break;
        } else if (list[k].until < list[slot].until) {
            slot = k;
        }
    list[slot].id = id;
    list[slot].until = s_ticks + (uint32_t)(seconds * TICK_RATE);
}

/* ¿Le sirve esta arma con sus habilidades? La Amazona pelea con jabalinas y
   lanzas (Estocada y Golpe poderoso piden eso); el Bárbaro con cualquier
   arma cuerpo a cuerpo que no sea para tirar, el Paladín de una mano (con
   escudo); la Hechicera con
   bastones, orbes y varitas (los hechizos no piden arma). */
static int demo_weapon_ok(const Player *p, const ItemType *t)
{
    if (p->cls == 1)
        return items_type_is(t, "jave") || items_type_is(t, "spea");
    if (p->cls == 2)
        return items_type_is(t, "staf") || items_type_is(t, "orb") || items_type_is(t, "wand");
    if (p->cls == 3) /* el Paladín no deja el escudo (Escarmiento, Escudo sagrado, bloquear) */
        return items_type_is(t, "mele") && !items_can_throw(t) && !t->two_handed;
    if (p->cls == 4) /* el Nigromante: varitas y dagas (Daga venenosa), con escudo */
        return (items_type_is(t, "wand") || items_type_is(t, "knif")) && !items_can_throw(t);
    if (p->cls == 5) /* la Asesina: garras */
        return items_type_is(t, "h2h");
    if (p->cls == 6 || p->cls == 7) /* el Druida y el Brujo: de una mano, con escudo o grimorio */
        return items_type_is(t, "mele") && !items_can_throw(t) && !t->two_handed;
    return items_type_is(t, "mele") && !items_can_throw(t);
}

/* Cuánto le sirve un objeto (0 = nada): las armas por el daño promedio que
   le harían (con lo que suma la fuerza o la destreza), lo demás por la
   defensa, la vida y los atributos que dan */
static int demo_item_value(const Player *p, const Item *it)
{
    const ItemType *t = it->type;
    if (!t || t->slot == SLOT_NONE || !items_supported(t) || t->ammo || is_quest_item(t))
        return 0;
    if (s_demo_all && (t->slot == SLOT_WEAPON || t->slot == SLOT_SHIELD))
        return 0; /* las armas las elige la habilidad que está usando */
    int v = items_stat(it, STAT_LIFE) * 2 + (items_stat(it, STAT_STR) + items_stat(it, STAT_DEX)) * 2 +
            items_stat(it, STAT_RED_DMG) * 4 + items_stat(it, STAT_LIFESTEAL) * 3 + items_stat(it, STAT_REGEN);
    /* Las resistencias, los + a las habilidades y los "más rápido" */
    v += (items_stat(it, STAT_RES_FIRE) + items_stat(it, STAT_RES_COLD) + items_stat(it, STAT_RES_LTNG) +
          items_stat(it, STAT_RES_POIS)) / 2;
    v += items_stat(it, STAT_ALL_SKILLS) * 25 + items_stat(it, STAT_VIT) * 2 + items_stat(it, STAT_MANA) +
         (items_stat(it, STAT_FCR) + items_stat(it, STAT_FHR)) / 3 + items_stat(it, STAT_MAGIC_FIND) / 5;
    for (int k = 0; k < it->num_stats; k++)
        if (it->stat[k] == STAT_CLASS_SKILLS && it->param[k] == itemstats_class_to_d2(p->cls))
            v += it->value[k] * 20;
    if (t->kind == ITEM_WEAPON) {
        if (!demo_weapon_ok(p, t))
            return 0;
        int lo, hi;
        items_damage(it, &lo, &hi);
        int bonus = t->str_bonus || t->dex_bonus ? player_str(p) * t->str_bonus / 100 + player_dex(p) * t->dex_bonus / 100
                                                 : player_str(p);
        static const int k_elem[8] = { STAT_FIRE_MIN, STAT_FIRE_MAX, STAT_LTNG_MIN, STAT_LTNG_MAX,
                                       STAT_COLD_MIN, STAT_COLD_MAX, STAT_MAG_MIN, STAT_MAG_MAX };
        int elem = 0;
        for (int e = 0; e < 8; e++)
            elem += items_stat(it, k_elem[e]);
        /* Los dominios del Bárbaro (más daño con ese tipo de arma) */
        int mastery = 0;
        for (int s = 0; p->cls == 0 && s < skills_count(p->cls); s++) {
            const SkillDef *d = skills_get(p->cls, s);
            if (d->kind == SK_MASTERY && p->skill_lvl[s] > 0 && d->passive_itype[0] && items_type_is(t, d->passive_itype))
                mastery = 25 + 10 * p->skill_lvl[s];
        }
        v += (lo + hi) * (100 + bonus + mastery) / 20 + elem * 5 + items_stat(it, STAT_IAS) / 2;
        if (p->cls == 1 && items_can_throw(t))
            v += v / 4; /* además se puede lanzar (Rayo, Jabalina venenosa) */
        return v > 0 ? v : 1;
    }
    v += items_defense(it) + items_stat(it, STAT_AC);
    return v > 0 ? v : 1;
}

/* Lo que vale lo que tiene puesto en ese lugar (con un arma a dos manos el
   escudo no se puede usar: vale 0) */
static int demo_slot_value(const Player *p, int slot)
{
    if (slot == SLOT_SHIELD && !items_offhand_ok(p->equip[SLOT_WEAPON].type, p->equip[SLOT_SHIELD].type))
        return 0;
    return demo_item_value(p, &p->equip[slot]);
}

/* ¿Es mejor que lo que tiene puesto? (un arma a dos manos pierde el escudo) */
static int demo_upgrade(const Player *p, const Item *it)
{
    const ItemType *t = it->type;
    if (is_quest_item(t)) /* el bastón y el amuleto van al cubo, no se ponen */
        return 0;
    int v = demo_item_value(p, it);
    if (v <= 0)
        return 0;
    if (t->slot == SLOT_SHIELD && !items_offhand_ok(p->equip[SLOT_WEAPON].type, t))
        return 0;
    int cur = demo_slot_value(p, equip_slot_for(p, t)); /* los anillos: la mano libre */
    if (t->slot == SLOT_WEAPON && t->two_handed && p->equip[SLOT_SHIELD].type && !p->equip[SLOT_SHIELD].type->ammo)
        cur += demo_slot_value(p, SLOT_SHIELD);
    return v > cur + cur / 10 + 1;
}

/* ¿Le alcanzan (o le van a alcanzar pronto) la fuerza y la destreza? */
static int demo_reqs_near(const Player *p, const ItemType *t)
{
    return t->req_str <= player_str(p) + 15 && t->req_dex <= player_dex(p) + 15;
}

static int demo_potion_kind(const ItemType *t, int kind)
{
    return kind == 0 ? (t->heal_life || t->heal_pct) : (t->heal_mana || t->heal_pct);
}

static int demo_belt_has(const Player *p, int kind)
{
    for (int k = 0; k < BELT_SLOTS; k++)
        if (p->belt[k].type && demo_potion_kind(p->belt[k].type, kind))
            return 1;
    return 0;
}

static int demo_antidotes(const Player *p)
{
    int n = 0;
    for (int k = 0; k < p->bag_count; k++)
        n += p->bag[k].type->antidote_ticks > 0 ? stack_count(&p->bag[k]) : 0;
    return n;
}

/* (las pilas cuentan por lo que tienen) */
static int demo_bag_potions(const Player *p)
{
    int n = 0;
    for (int k = 0; k < p->bag_count; k++)
        n += is_potion(p->bag[k].type) ? stack_count(&p->bag[k]) : 0;
    return n;
}

/* Cuántos Pergaminos de Portal tiene */
static int demo_tp_charges(const Player *p)
{
    int n = 0;
    for (int k = 0; k < p->bag_count; k++)
        n += strcmp(p->bag[k].type->code, "tsc") == 0 ? stack_count(&p->bag[k]) : 0;
    return n;
}

/* Lo de este pueblo con ese código (los pergaminos), o NULL */
static const Item *demo_town_stock(const char *code)
{
    for (int v = 0; v < VENDOR_COUNT; v++) {
        const Shop *sh = &s_shops[v];
        for (int k = 0; sh->act == s_act && k < sh->count; k++)
            if (strcmp(sh->stock[k].type->code, code) == 0)
                return &sh->stock[k];
    }
    return NULL;
}

/* El portal al pueblo, con lo que sobra después de las pociones: pergaminos
   hasta tener 10 (van todos en una pila) */
static int demo_buy_tp(Player *p)
{
    int bought = 0;
    const Item *tsc = demo_town_stock("tsc");
    while (tsc && demo_tp_charges(p) < 10 && items_price(tsc, 0) + 50 <= p->gold && bag_add(p, tsc)) {
        p->gold -= items_price(tsc, 0);
        bought++;
    }
    return bought;
}

/* ¿Vale la pena levantarlo? */
static int demo_wants(const Player *p, const Item *it)
{
    const ItemType *t = it->type;
    if (strcmp(t->code, "gld") == 0)
        return 1;
    if (t->heal_life || t->heal_mana || t->heal_pct) {
        int free_belt = 0;
        for (int k = 0; k < BELT_SLOTS; k++)
            free_belt += !p->belt[k].type;
        return free_belt > 0 || (demo_bag_potions(p) < 4 && p->bag_count < BAG_MAX - 4);
    }
    /* Un objeto de misión que quedó en el piso (con la mochila llena al
       tocar el Mazo): lo levanta el primero, que es el que hace las misiones */
    if (is_quest_item(t))
        return p == &s_pl[0] && bag_index_of(p, t->code) < 0;
    if (t->slot == SLOT_NONE || p->bag_count >= BAG_MAX - 4 || !at_bag_room(p, t))
        return 0; /* (si no entra en la mochila, ni lo intenta: se quedaba levantándolo para siempre) */
    return demo_upgrade(p, it) && demo_reqs_near(p, t) && items_level_req(it) <= p->level_num + 2;
}

/* ¿Lo guarda? (una mejora que todavía no se puede poner: por fuerza,
   destreza o nivel, que le van a alcanzar pronto) */
static int demo_keep(const Player *p, const Item *it)
{
    return demo_upgrade(p, it) && demo_reqs_near(p, it->type) && items_level_req(it) <= p->level_num + 2;
}

/* ---- Atributos, habilidades y botones ---- */

static void demo_spend_points(int i)
{
    Player *p = &s_pl[i];
    int added[4] = { 0 };
    while (p->stat_points > 0) {
        /* Lo que pide lo que quiere ponerse y todavía no puede */
        int need_str = 0, need_dex = 0;
        for (int k = 0; k < p->bag_count; k++) {
            const ItemType *t = p->bag[k].type;
            if (t->slot == SLOT_NONE || meets_reqs(p, t) || !demo_keep(p, &p->bag[k]))
                continue;
            if (t->req_str > need_str)
                need_str = t->req_str;
            if (t->req_dex > need_dex)
                need_dex = t->req_dex;
        }
        int a;
        if (player_str(p) < need_str)
            a = ATTR_STR;
        else if (player_dex(p) < need_dex)
            a = ATTR_DEX;
        else {
            /* Su proporción: el Bárbaro 2 de fuerza cada 3 de vitalidad, la
               Amazona 2 de destreza cada 3 de vitalidad */
            int main = p->cls == 0 || p->cls == 3 ? ATTR_STR : p->cls == 1 ? ATTR_DEX : ATTR_ENE;
            a = p->stat_add[main] * 3 < p->stat_add[ATTR_VIT] * 2 ? main : ATTR_VIT;
        }
        p->stat_add[a]++;
        added[a]++;
        p->stat_points--;
    }
    refresh_stats(p);
    printf("  demo J%d (nivel %d) reparte puntos:", i + 1, p->level_num);
    for (int a = 0; a < 4; a++)
        if (added[a])
            printf(" +%d %s", added[a], k_attr_names[a]);
    printf(" -> vida %d, fuerza %d, destreza %d\n", p->hp_max, player_str(p), player_dex(p));
}

/* El dominio que sube el Bárbaro: el que ya empezó (para no repartir los
   puntos si cambia de arma) o, si no tiene, el del arma que tiene puesta.
   -1 si no hay. */
static int demo_mastery(const Player *p)
{
    int best = -1;
    for (int s = 0; s < skills_count(p->cls); s++)
        if (skills_get(p->cls, s)->kind == SK_MASTERY && p->skill_lvl[s] > 0 &&
            (best < 0 || p->skill_lvl[s] > p->skill_lvl[best]))
            best = s;
    if (best >= 0)
        return best;
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    for (int s = 0; w && s < skills_count(p->cls); s++) {
        const SkillDef *d = skills_get(p->cls, s);
        if (d->kind == SK_MASTERY && d->passive_itype[0] && items_type_is(w, d->passive_itype))
            return s;
    }
    return -1;
}

static void demo_learn(int i)
{
    Player *p = &s_pl[i];
    const DemoSkillGoal *plan = p->cls == 0 ? k_barb_plan : p->cls == 1 ? k_ama_plan : p->cls == 2 ? k_sor_plan
                              : p->cls == 3 ? k_pal_plan : p->cls == 4 ? k_nec_plan : p->cls == 5 ? k_ass_plan
                              : p->cls == 6 ? k_dru_plan                            : k_war_plan;
    int n = p->cls == 0   ? (int)(sizeof k_barb_plan / sizeof k_barb_plan[0])
            : p->cls == 1 ? (int)(sizeof k_ama_plan / sizeof k_ama_plan[0])
            : p->cls == 2 ? (int)(sizeof k_sor_plan / sizeof k_sor_plan[0])
            : p->cls == 3 ? (int)(sizeof k_pal_plan / sizeof k_pal_plan[0])
            : p->cls == 4 ? (int)(sizeof k_nec_plan / sizeof k_nec_plan[0])
            : p->cls == 5 ? (int)(sizeof k_ass_plan / sizeof k_ass_plan[0])
            : p->cls == 6 ? (int)(sizeof k_dru_plan / sizeof k_dru_plan[0])
                          : (int)(sizeof k_war_plan / sizeof k_war_plan[0]);
    while (p->skill_points > 0) {
        int learned = -1;
        for (int g = 0; g < n && learned < 0; g++) {
            int s = strcmp(plan[g].key, "@mastery") == 0 ? demo_mastery(p) : skills_find(p->cls, plan[g].key);
            char why[48];
            if (s >= 0 && p->skill_lvl[s] < plan[g].level && can_learn(p, s, why, sizeof why))
                learned = s;
        }
        if (learned < 0)
            return; /* nada que convenga ahora: guarda el punto para cuando pueda */
        learn_skill(p, learned);
        printf("  demo J%d (nivel %d) aprende %s (nivel %d)\n", i + 1, p->level_num, skills_get(p->cls, learned)->name,
               p->skill_lvl[learned]);
    }
}

/* La mejor aprendida de una lista que se pueda usar con el arma (-1 = ninguna) */
static int demo_best_skill(const Player *p, const char *const *keys, int n)
{
    for (int k = 0; k < n; k++) {
        int s = skills_find(p->cls, keys[k]);
        if (s >= 0 && p->skill_lvl[s] > 0 && skills_get(p->cls, s)->kind != SK_NONE &&
            weapon_allows(p, skills_get(p->cls, s)))
            return s;
    }
    return -1;
}

/* Pone en cada botón lo que conviene: A el ataque (y hablar, levantar...),
   B la habilidad de pegar, X el grito o Percepción, Y la otra */
static void demo_buttons(int i)
{
    Player *p = &s_pl[i];
    int want[NUM_BUTTONS] = { BTN_ATTACK, BTN_EMPTY, BTN_EMPTY, BTN_EMPTY };
    if (p->cls == 0) {
        static const char *const hit[] = { "Concentrate", "Bash" }, *const buff[] = { "Shout" },
                                 *const other[] = { "Battle Cry" };
        want[1] = demo_best_skill(p, hit, 2);
        want[2] = demo_best_skill(p, buff, 1);
        want[3] = demo_best_skill(p, other, 1);
    } else if (p->cls == 2) {
        static const char *const hit[] = { "Fire Ball", "Fire Bolt", "Charged Bolt", "Ice Bolt" },
                                 *const buff[] = { "Chilling Armor", "Shiver Armor", "Frozen Armor" },
                                 *const close[] = { "Static Field", "Frost Nova" };
        want[1] = demo_best_skill(p, hit, 4);
        want[2] = demo_best_skill(p, buff, 3);
        want[3] = demo_best_skill(p, close, 2);
    } else if (p->cls == 7) { /* B la miasma o la Ola, X el Sigilo de letargo; Y (los demonios, el fuego) lo elige demo_special */
        static const char *const hit[] = { "Miasma Chains", "Miasma Bolt" }, *const buff[] = { "Sigil Lethargy" },
                                 *const other[] = { "Summon Goatman" };
        want[1] = demo_best_skill(p, hit, 2);
        want[2] = demo_best_skill(p, buff, 1);
        want[3] = demo_best_skill(p, other, 1);
    } else if (p->cls == 6) { /* B Tormenta de fuego, X la Armadura de ciclón; Y (las invocaciones) lo elige demo_special */
        static const char *const hit[] = { "Firestorm", "Molten Boulder" }, *const buff[] = { "Cyclone Armor" },
                                 *const other[] = { "Raven" };
        want[1] = demo_best_skill(p, hit, 2);
        want[2] = demo_best_skill(p, buff, 1);
        want[3] = demo_best_skill(p, other, 1);
    } else if (p->cls == 5) { /* B Trauma de fuego, X Rapidez; Y (las trampas, el Manto) lo elige demo_special */
        static const char *const hit[] = { "Fire Trauma" }, *const buff[] = { "Quickness" },
                                 *const other[] = { "Wake of Fire Sentry", "Charged Bolt Sentry" };
        want[1] = demo_best_skill(p, hit, 1);
        want[2] = demo_best_skill(p, buff, 1);
        want[3] = demo_best_skill(p, other, 2);
    } else if (p->cls == 4) { /* B Lanza de hueso o Dientes, X la Armadura de hueso; Y lo elige demo_special */
        static const char *const hit[] = { "Bone Spear", "Teeth" }, *const buff[] = { "Bone Armor" },
                                 *const other[] = { "Raise Skeleton" };
        want[1] = demo_best_skill(p, hit, 2);
        want[2] = demo_best_skill(p, buff, 1);
        want[3] = demo_best_skill(p, other, 1);
    } else if (p->cls == 3) { /* el aura en X; B y Y los elige demo_melee y demo_special */
        static const char *const hit[] = { "Zeal", "Sacrifice" }, *const aura[] = { "Holy Fire", "Might" },
                                 *const other[] = { "Smite" };
        want[1] = demo_best_skill(p, hit, 2);
        want[2] = demo_best_skill(p, aura, 2);
        want[3] = demo_best_skill(p, other, 1);
    } else {
        static const char *const hit[] = { "Power Strike", "Jab" }, *const buff[] = { "Inner Sight" },
                                 *const far[] = { "Lightning Bolt", "Plague Javelin", "Poison Javelin" };
        int ps = skills_find(p->cls, "Power Strike");
        /* Golpe poderoso recién aprendido pega menos que Estocada */
        want[1] = demo_best_skill(p, ps >= 0 && p->skill_lvl[ps] >= 3 ? hit : hit + 1, ps >= 0 && p->skill_lvl[ps] >= 3 ? 2 : 1);
        want[2] = demo_best_skill(p, buff, 1);
        want[3] = demo_best_skill(p, far, 3);
        if (want[3] < 0 && items_can_throw(p->equip[SLOT_WEAPON].type))
            want[3] = BTN_THROW; /* sin habilidades de lejos: tirar la jabalina */
    }
    for (int b = 0; b < NUM_BUTTONS; b++) {
        if (want[b] == -1 && b > 0)
            want[b] = BTN_EMPTY;
        /* B e Y los cambia en cada golpe según la situación (demo_button):
           acá solo se ponen si están vacíos o tienen algo que ya no sirve */
        if ((b == 1 || b == 3) && p->buttons[b] != BTN_EMPTY &&
            (p->buttons[b] < 0 || (p->skill_lvl[p->buttons[b]] > 0 && weapon_allows(p, skills_get(p->cls, p->buttons[b])))))
            continue;
        if (p->buttons[b] != want[b]) {
            p->buttons[b] = want[b];
            if (want[b] != BTN_EMPTY)
                printf("  demo J%d pone en %s: %s\n", i + 1, k_button_names[b], button_skill_name(p, p->buttons[b]));
        }
    }
}

/* ¿Puede usar ahora lo que tiene en ese botón? */
static int demo_can_use(const Player *p, int b)
{
    int what = p->buttons[b];
    if (what == BTN_THROW)
        return items_can_throw(p->equip[SLOT_WEAPON].type) && p->equip[SLOT_WEAPON].amount > 2;
    if (what < 0)
        return what == BTN_ATTACK;
    const SkillDef *s = skills_get(p->cls, what);
    if (p->skill_lvl[what] <= 0 || s->passive || s->kind == SK_NONE || !weapon_allows(p, s) ||
        p->mana < skill_mana(s, p->skill_lvl[what]))
        return 0;
    if (skill_attack_kind(s) == ATK_THROW) /* las jabalinas: que quede alguna para pegar */
        return items_can_throw(p->equip[SLOT_WEAPON].type) && p->equip[SLOT_WEAPON].amount > 2;
    return 1;
}

/* La habilidad (por su nombre en skills.txt) si la aprendió, se puede usar
   con el arma y le alcanza el maná (dejando `reserve`); -1 si no */
static int demo_skill(const Player *p, const char *key, int reserve)
{
    int s = skills_find(p->cls, key);
    if (s < 0 || p->skill_lvl[s] <= 0)
        return -1;
    const SkillDef *d = skills_get(p->cls, s);
    if (d->passive || d->kind == SK_NONE || !weapon_allows(p, d) || p->mana < skill_mana(d, p->skill_lvl[s]) + reserve)
        return -1;
    if (skill_attack_kind(d) == ATK_THROW &&
        !(items_can_throw(p->equip[SLOT_WEAPON].type) && p->equip[SLOT_WEAPON].amount > 2))
        return -1;
    return s;
}

/* Pone esa habilidad en el botón, como el jugador en la página Botones */
static void demo_assign(int i, int b, int skill)
{
    if (skill != BTN_EMPTY && s_pl[i].buttons[b] != skill)
        s_pl[i].buttons[b] = skill;
}

/* El Nigromante: ¿le entra uno más de esa invocación? (petmax) */
static int demo_nec_room(const Player *p, const char *key, int kind)
{
    int s = skills_find(p->cls, key);
    if (s < 0 || p->skill_lvl[s] <= 0)
        return 0;
    const SkillDef *d = skills_get(p->cls, s);
    SkillCtx c = pctx(p);
    int most = kind == PK_GOLEM ? 1 : d->petmax[0] ? skill_eval(d, d->petmax, eff_lvl(p, s), &c) : 1;
    return nec_pet_count(p, kind) < most;
}

/* El Brujo: ¿le entra un demonio más? (entre los tres, los que dice el Dominio demoníaco) */
static int demo_war_room(const Player *p, const char *key)
{
    int s = skills_find(p->cls, key);
    if (s < 0 || p->skill_lvl[s] <= 0)
        return 0;
    const SkillDef *d = skills_get(p->cls, s);
    SkillCtx c = pctx(p);
    int most = d->petmax[0] ? skill_eval(d, d->petmax, eff_lvl(p, s), &c) : 1, ids[8];
    int n = war_demons(p, ids, 8) - (p->bind_unit >= 0 && units_converted(p->bind_unit));
    return n < most;
}

/* El Druida: ¿le falta alguno de esa invocación? (se murió o no llenó los que puede tener) */
static int demo_dru_room(const Player *p, const char *key)
{
    int s = skills_find(p->cls, key);
    if (s < 0 || p->skill_lvl[s] <= 0)
        return 0;
    const SkillDef *d = skills_get(p->cls, s);
    int kind = dru_pet_kind(d->kind);
    if (kind == PK_VINE || kind == PK_SPIRIT) /* una enredadera y un espíritu: si no tiene */
        return dru_pet(p, kind) < 0;
    SkillCtx c = pctx(p);
    int most = d->petmax[0] ? skill_eval(d, d->petmax, eff_lvl(p, s), &c) : 1;
    return nec_pet_count(p, kind) < most;
}

/* Lo que va en Y según la situación: el Bárbaro grita (Aullido si lo rodean
   con poca vida, Grito de batalla contra varios o un jefe, Provocar a los que
   disparan de lejos); la Amazona tira la jabalina que convenga (Plaga a un
   grupo, Rayo si hay varios, Venenosa a uno solo). BTN_EMPTY = nada. */
static int demo_special(int i, int target, float d)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    int ids[16], near = units_enemies_near(p->x, p->y, 6.0f, ids, 16);
    int close = units_enemies_near(p->x, p->y, 3.0f, ids, 16);
    int boss = target >= 0 && units_is_unique(target);
    int hp_pct = p->hp * 100 / (p->hp_max > 0 ? p->hp_max : 1), s;
    if (p->cls == 2) {
        if (close >= 3 && (s = demo_skill(p, "Frost Nova", 0)) >= 0)
            return s;
        if ((boss || near >= 3) && d < 4.0f && s_ticks >= br->special_at && (s = demo_skill(p, "Static Field", 0)) >= 0) {
            br->special_at = s_ticks + 2 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (p->cls == 7) {
        /* Los demonios: solo si le falta alguno (el mancillado si ya tiene un hombre cabra) */
        static const char *const k_dem[] = { "Summon Goatman", "Summon Tainted", "Summon Defiler" };
        int have_goat = 0;
        for (int k = 0; k < NEC_PETS; k++)
            have_goat |= p->pet_kind[k] == PK_GOATMAN && units_pet_alive(p->pets[k]);
        for (unsigned k = 0; k < sizeof k_dem / sizeof k_dem[0]; k++) {
            unsigned pick = have_goat && k == 0 ? 1 : k;
            if (s_ticks >= br->special_at && demo_war_room(p, k_dem[pick]) && (s = demo_skill(p, k_dem[pick], 0)) >= 0) {
                br->special_at = s_ticks + TICK_RATE;
                return s;
            }
        }
        /* El Anillo de fuego a los que se le vienen encima */
        if (close >= 2 && s_ticks >= br->howl_at && (s = demo_skill(p, "Ring of Fire", 0)) >= 0) {
            br->howl_at = s_ticks + TICK_RATE;
            return s;
        }
        /* Hecatombe y Abismo a un grupo o a un jefe */
        if (target >= 0 && d < 9.0f && (near >= 4 || boss) && s_ticks >= br->taunt_at &&
            ((s = demo_skill(p, "Apocalypse", 0)) >= 0 || (s = demo_skill(p, "Abyss", 0)) >= 0)) {
            br->taunt_at = s_ticks + 2 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (p->cls == 6) {
        /* Las invocaciones: solo las que le faltan (se murieron o no llegó a tenerlas) */
        static const char *const k_pets[] = { "Summon Grizzly", "Summon Fenris", "Summon Spirit Wolf", "Raven",
                                              "Heart of Wolverine", "Oak Sage" };
        for (unsigned k = 0; k < sizeof k_pets / sizeof k_pets[0]; k++)
            if (s_ticks >= br->special_at && demo_dru_room(p, k_pets[k]) && (s = demo_skill(p, k_pets[k], 0)) >= 0) {
                br->special_at = s_ticks + TICK_RATE;
                return s;
            }
        /* La enredadera carroñera si le falta vida (y hay cadáveres para que coma) */
        if (hp_pct < 70 && s_ticks >= br->howl_at && demo_dru_room(p, "Cycle of Life") &&
            units_corpses_near(p->x, p->y, 10.0f, ids, 16) > 0 && (s = demo_skill(p, "Cycle of Life", 0)) >= 0) {
            br->howl_at = s_ticks + 5 * TICK_RATE;
            return s;
        }
        /* Fisura y Volcán a un grupo o a un jefe */
        if (target >= 0 && d < 9.0f && (near >= 3 || boss) && s_ticks >= br->taunt_at &&
            ((s = demo_skill(p, "Volcano", 0)) >= 0 || (s = demo_skill(p, "Eruption", 0)) >= 0)) {
            br->taunt_at = s_ticks + 2 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (p->cls == 5) {
        /* Trampas cerca de los monstruos (hasta 5): la Estela de fuego o las centellas */
        if (target >= 0 && d < 9.0f && near >= 2 && trap_count(p) < ASS_TRAPS && s_ticks >= br->special_at &&
            ((s = demo_skill(p, "Lightning Sentry", 0)) >= 0 || (s = demo_skill(p, "Wake of Fire Sentry", 0)) >= 0 ||
             (s = demo_skill(p, "Charged Bolt Sentry", 0)) >= 0)) {
            br->special_at = s_ticks + TICK_RATE;
            return s;
        }
        /* El Manto de sombras a un jefe o a un grupo que no lo tenga */
        if (target >= 0 && d < 9.0f && (boss || near >= 3) && !units_cloaked(target) && s_ticks >= br->taunt_at &&
            (s = demo_skill(p, "Cloak of Shadows", 0)) >= 0) {
            br->taunt_at = s_ticks + 4 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (p->cls == 4) {
        /* Esqueletos de los cadáveres de al lado (hasta los que puede tener) */
        if (s_ticks >= br->special_at && nec_corpse(p, 0, 0) >= 0 && demo_nec_room(p, "Raise Skeleton", PK_SKELETON) &&
            (s = demo_skill(p, "Raise Skeleton", 0)) >= 0) {
            br->special_at = s_ticks + TICK_RATE;
            return s;
        }
        /* Amplificar daño a un jefe o a un grupo (si no lo tiene ya) */
        if (target >= 0 && d < 9.0f && (boss || near >= 3) && units_nec_cursed(target) != NC_AMPLIFY &&
            s_ticks >= br->taunt_at && (s = demo_skill(p, "Amplify Damage", 0)) >= 0) {
            br->taunt_at = s_ticks + 3 * TICK_RATE;
            return s;
        }
        /* Explosión de cadáver: un muerto con dos o más vivos a su alcance */
        int corpse = target >= 0 && d < 9.0f ? nec_corpse(p, 1, 0) : -1, ce = skills_find(p->cls, "Corpse Explosion");
        if (corpse >= 0 && ce >= 0 && p->skill_lvl[ce] > 0) {
            SkillCtx c = pctx(p);
            float cx, cy, r = (float)(skill_calc(skills_get(p->cls, ce), SC_AURARANGE, eff_lvl(p, ce), &c) + 1) / 2.0f;
            units_position(corpse, 1.0f, &cx, &cy);
            if (units_enemies_near(cx, cy, r, ids, 16) >= 2 && (s = demo_skill(p, "Corpse Explosion", 0)) >= 0)
                return s;
        }
        /* El gólem, si no tiene */
        if (target >= 0 && s_ticks >= br->howl_at && demo_nec_room(p, "Clay Golem", PK_GOLEM) &&
            (s = demo_skill(p, "Clay Golem", 0)) >= 0) {
            br->howl_at = s_ticks + 5 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (p->cls == 3) {
        if (boss && d < MELEE_RANGE * 0.9f && s_ticks >= br->special_at && !(units_status(target) & UNIT_STUNNED) &&
            (s = demo_skill(p, "Smite", 0)) >= 0) {
            br->special_at = s_ticks + 3 * TICK_RATE;
            return s;
        }
        if (target >= 0 && d > 4.0f && d < 9.0f && s_ticks >= br->taunt_at && (s = demo_skill(p, "Charge", 0)) >= 0) {
            br->taunt_at = s_ticks + 4 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (p->cls == 0) {
        if (hp_pct < 35 && close >= 2 && s_ticks >= br->howl_at && (s = demo_skill(p, "Howl", 0)) >= 0) {
            br->howl_at = s_ticks + 10 * TICK_RATE;
            return s;
        }
        if ((near >= 3 || boss) && s_ticks >= br->cry2_at && (s = demo_skill(p, "Battle Cry", 0)) >= 0) {
            br->cry2_at = s_ticks + 6 * TICK_RATE;
            return s;
        }
        if (target >= 0 && units_is_ranged(target) && d > 3.0f && d < 9.0f && s_ticks >= br->taunt_at &&
            (s = demo_skill(p, "Taunt", 0)) >= 0) {
            br->taunt_at = s_ticks + 5 * TICK_RATE;
            return s;
        }
        return BTN_EMPTY;
    }
    if (d <= 2.5f || d >= 9.0f || target < 0)
        return BTN_EMPTY;
    float tx, ty;
    units_position(target, 1.0f, &tx, &ty);
    int around = units_enemies_near(tx, ty, 3.0f, ids, 16);
    if (around >= 3 && (s = demo_skill(p, "Plague Javelin", 0)) >= 0)
        return s;
    if (near >= 2 && (s = demo_skill(p, "Lightning Bolt", 0)) >= 0)
        return s;
    if ((s = demo_skill(p, "Poison Javelin", 0)) >= 0)
        return s;
    if ((s = demo_skill(p, "Lightning Bolt", 0)) >= 0)
        return s;
    return items_can_throw(p->equip[SLOT_WEAPON].type) && p->equip[SLOT_WEAPON].amount > 2 ? BTN_THROW : BTN_EMPTY;
}

/* La de pegar al lado (B): a los jefes Aturdir o Empalar cada tanto;
   si no, Concentración o Porrazo, Golpe poderoso o Estocada. -1 = el
   ataque común (A). */
static int demo_melee(int i, int target, int reserve)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    int boss = target >= 0 && units_is_unique(target), s;
    if (p->cls == 2) {
        if ((s = demo_skill(p, "Fire Ball", reserve)) >= 0 || (s = demo_skill(p, "Fire Bolt", reserve)) >= 0 ||
            (s = demo_skill(p, "Charged Bolt", reserve)) >= 0 || (s = demo_skill(p, "Ice Bolt", reserve)) >= 0)
            return s;
        return -1;
    }
    if (p->cls == 3) {
        if ((s = demo_skill(p, "Zeal", reserve)) >= 0 || (s = demo_skill(p, "Sacrifice", reserve)) >= 0)
            return s;
        return -1;
    }
    if (p->cls == 4) {
        if ((s = demo_skill(p, "Bone Spear", reserve)) >= 0 || (s = demo_skill(p, "Teeth", reserve)) >= 0)
            return s;
        return -1;
    }
    if (p->cls == 5) {
        if ((s = demo_skill(p, "Fire Trauma", reserve)) >= 0 || (s = demo_skill(p, "Shock Field", reserve)) >= 0)
            return s;
        return -1;
    }
    if (p->cls == 7) { /* la Ola de llamas a los grupos; si no, la Cadena o la Descarga de miasma */
        int ids[16], near = units_enemies_near(p->x, p->y, 7.0f, ids, 16);
        if (near >= 3 && s_ticks >= br->special_at && (s = demo_skill(p, "Flame Wave", reserve)) >= 0) {
            br->special_at = s_ticks + TICK_RATE;
            return s;
        }
        if ((near >= 2 && (s = demo_skill(p, "Miasma Chains", reserve)) >= 0) ||
            (s = demo_skill(p, "Miasma Bolt", reserve)) >= 0 || (s = demo_skill(p, "Miasma Chains", reserve)) >= 0)
            return s;
        return -1;
    }
    if (p->cls == 6) { /* la Roca fundida a los jefes cada tanto; si no, Tormenta de fuego */
        if (boss && s_ticks >= br->special_at && (s = demo_skill(p, "Molten Boulder", reserve)) >= 0) {
            br->special_at = s_ticks + 2 * TICK_RATE;
            return s;
        }
        if ((s = demo_skill(p, "Firestorm", reserve)) >= 0 || (s = demo_skill(p, "Molten Boulder", reserve)) >= 0)
            return s;
        return -1;
    }
    if (p->cls == 0) {
        if (boss && s_ticks >= br->special_at && !(units_status(target) & UNIT_STUNNED) &&
            (s = demo_skill(p, "Stun", reserve)) >= 0) {
            br->special_at = s_ticks + 4 * TICK_RATE;
            return s;
        }
        if ((s = demo_skill(p, "Concentrate", reserve)) >= 0 || (s = demo_skill(p, "Bash", reserve)) >= 0)
            return s;
        return -1;
    }
    if (boss && s_ticks >= br->special_at && (s = demo_skill(p, "Impale", reserve)) >= 0) {
        br->special_at = s_ticks + 3 * TICK_RATE;
        return s;
    }
    int ps = skills_find(p->cls, "Power Strike");
    if (ps >= 0 && p->skill_lvl[ps] >= 3 && (s = demo_skill(p, "Power Strike", reserve)) >= 0)
        return s;
    if ((s = demo_skill(p, "Jab", reserve)) >= 0 || (s = demo_skill(p, "Power Strike", reserve)) >= 0)
        return s;
    return -1;
}

/* ---- Todas las habilidades (la demostración del menú) ----
   Los dos empiezan con nivel 30 (lo que pide la última fila del árbol) y
   todas las habilidades de su clase en 1, y usan una por una las que se
   usan: cada una de ataque unas cuantas veces, cada una de apoyo (gritos,
   Percepción, Señuelo, la Valquiria, Salto) una vez. La próxima
   demostración sigue desde donde quedó la anterior (el Acto 1 entero no
   alcanza para todas). Cambian de arma
   como en la prueba de habilidades: el Bárbaro dos hachas de guerra (dos
   arrojadizas para Doble lanzamiento), la Amazona el arco para las flechas y
   glaives para las de jabalina y lanza. Las de flechas y jabalinas se usan
   de lejos. No se queda sin maná: es para ver cómo anda cada una. */

static float st_distance(int kind);
static int   st_support(int kind);
static void  st_weapons(const Player *p, int kind, const char **main, const char **off);
static void  st_equip(Player *p, int kind);

#define DEMO_SHOW_USES  6                /* cuántas veces usa cada una de ataque */
#define DEMO_SHOW_TICKS (15 * TICK_RATE) /* o cuánto pelea con ella, lo que pase antes */
#define DEMO_SHOW_WAIT  (6 * TICK_RATE)  /* si la de apoyo no sale en este tiempo, sigue */

/* ---- Pesadilla e Infierno (docs/plan-dificultades.md) ----
   El equipo de un personaje de esa dificultad, para que los bots jueguen
   ahí como lo haría una persona: cada pieza en su versión excepcional
   (Pesadilla) o de élite (Infierno) si le alcanzan la fuerza, la destreza y
   el nivel, rara, con las resistencias que compensan la penalidad (-40 o
   -100: 10 o 20 a todas en cada una de las nueve piezas), vida y, la
   armadura, defensa; las armas, con daño aumentado; el amuleto, con
   habilidades */
static void demo_diff_item(const Player *p, Item *it)
{
    if (!it->type || s_diff == 0 || it->type->ammo)
        return;
    for (int up = s_diff; up >= 1; up--) { /* (la de élite, o si no la excepcional) */
        const ItemType *to = items_find(up == 2 ? it->type->ultracode : it->type->ubercode);
        if (to && to != it->type && items_supported(to) && to->req_str <= player_str(p) && to->req_dex <= player_dex(p) &&
            to->level_req <= p->level_num && items_upgrade(it, up == 2))
            break;
    }
    int amount = it->amount;
    items_make_rare(it, p->level_num);
    it->amount = amount;
    if (it->type->kind == ITEM_WEAPON) {
        items_apply_prop(it, "dmg%", "", 100 + 75 * s_diff, 100 + 75 * s_diff);
        /* y fuego y rayo: los inmunes al daño físico (los fantasmas en
           Infierno) también bajan, como con el equipo de una persona */
        items_apply_prop(it, "dmg-fire", "", 20 * s_diff * s_diff, 60 * s_diff * s_diff);
        items_apply_prop(it, "dmg-ltng", "", 10 * s_diff * s_diff, 100 * s_diff * s_diff);
        /* y robo de vida y maná (dividido por 2 o 3 en esas dificultades): sin
           eso volvían al pueblo cada pocos monstruos y se pasaba el tiempo */
        items_apply_prop(it, "lifesteal", "", 6 * s_diff, 6 * s_diff);
        items_apply_prop(it, "manasteal", "", 4 * s_diff, 4 * s_diff);
        return;
    }
    items_apply_prop(it, "res-all", "", 10 * s_diff, 10 * s_diff);
    items_apply_prop(it, "hp", "", 20 + 20 * s_diff, 20 + 20 * s_diff);
    if (it->type->kind == ITEM_ARMOR && it->type->max_ac > 0)
        items_apply_prop(it, "ac%", "", 100, 100);
    if (it->type->slot == SLOT_AMULET)
        items_apply_prop(it, "allskills", "", s_diff, s_diff);
}

/* La base de más defensa (o de más nivel, las joyas) que puede usar en ese lugar (las de clase no) */
static const ItemType *demo_diff_base(const Player *p, int slot)
{
    const ItemType *best = NULL;
    for (int k = 0; k < items_type_count(); k++) {
        const ItemType *t = items_type(k);
        if (t->slot != slot || !items_supported(t) || t->class_only || t->req_str > player_str(p) ||
            t->req_dex > player_dex(p) || t->level_req > p->level_num)
            continue;
        if (!best || t->max_ac > best->max_ac || (t->max_ac == best->max_ac && t->level > best->level))
            best = t;
    }
    return best;
}

static void demo_diff_gear(int i)
{
    Player *p = &s_pl[i];
    static const int k_slots[] = { SLOT_HELM, SLOT_BODY, SLOT_GLOVES, SLOT_BOOTS, SLOT_BELT, SLOT_AMULET, SLOT_RING1, SLOT_RING2 };
    for (unsigned k = 0; k < sizeof k_slots / sizeof k_slots[0]; k++) {
        const ItemType *t = demo_diff_base(p, k_slots[k] == SLOT_RING2 ? SLOT_RING1 : k_slots[k]);
        if (!t)
            continue;
        Item it = items_make(t);
        demo_diff_item(p, &it);
        p->equip[k_slots[k]] = it;
    }
    refresh_look(p);
    refresh_stats(p);
    printf("  demo J%d equipo de %s: %s, %s; vida %d, resistencias %d/%d/%d/%d\n", i + 1, k_diff_names[s_diff],
           p->equip[SLOT_BODY].type ? p->equip[SLOT_BODY].type->name : "-",
           p->equip[SLOT_HELM].type ? p->equip[SLOT_HELM].type->name : "-", p->hp_max, player_resist(p, 0),
           player_resist(p, 1), player_resist(p, 2), player_resist(p, 3));
}

static void demo_show_setup(int i)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    /* El Acto 1 con nivel 30; el 2, con nivel 40 y todas las habilidades en 6;
       el 3, con nivel 50 y en 8; el 4, con nivel 60 y en 10; el 5, con nivel 70 y en 12 */
    int lvl = s_at.act == 5 ? 70 : s_at.act == 4 ? 60 : s_at.act == 3 ? 50 : s_at.act == 2 ? 40 : 30;
    int base = s_at.act == 5 ? 12 : s_at.act == 4 ? 10 : s_at.act == 3 ? 8 : s_at.act == 2 ? 6 : 1;
    lvl = lvl + 25 * s_diff < 99 ? lvl + 25 * s_diff : 99; /* (en Pesadilla 25 niveles más y en Infierno 50, hasta el 99) */
    base += 8 * s_diff; /* (y las habilidades 8 y 16 más) */
    if (p->level_num < lvl)
        gain_exp(p, s_classes[p->cls].exp_next[lvl - 1] - p->exp);
    /* Los 145 puntos: fuerza y destreza para las armas que usa, el resto vitalidad */
    static const int add[NUM_CLASSES][4] = { { 40, 40, 65, 0 }, { 32, 60, 53, 0 }, { 15, 15, 70, 45 }, { 50, 30, 65, 0 },
                                             { 35, 15, 60, 35 }, { 40, 40, 65, 0 }, { 40, 20, 65, 20 },
                                             { 35, 20, 60, 30 } };
    /* (en la campaña se llama en cada acto: se reparten de nuevo todos, como
       en uno nuevo de ese nivel y esa dificultad; si no, el que venía de
       Normal llegaba a Pesadilla con todo en vitalidad y sin la fuerza para
       las armas de ahí) */
    for (int a = 0; a < 4; a++) {
        p->stat_points += p->stat_add[a];
        p->stat_add[a] = 0;
    }
    for (int a = 0; a < 4 && p->stat_points > 0; a++) {
        int n = add[p->cls][a] < p->stat_points ? add[p->cls][a] : p->stat_points;
        p->stat_add[a] += n;
        p->stat_points -= n;
    }
    if (s_diff > 0) { /* las bases mejores de Pesadilla e Infierno piden más fuerza y destreza */
        int str = p->stat_points * 35 / 100, dex = p->stat_points * 15 / 100;
        p->stat_add[0] += str;
        p->stat_add[1] += dex;
        p->stat_points -= str + dex;
    }
    p->stat_add[2] += p->stat_points; /* los del nivel 40, a vitalidad */
    p->stat_points = 0;
    /* Todas en 1 (los puntos de habilidad del nivel 30 alcanzan para eso) y la
       lista de las que se usan, de a 4 con la misma arma (para no cambiar a
       cada rato, pero que se vean las de todas las armas) */
    const char *sets[MAX_CLASS_SKILLS];
    int nsets = 0;
    br->show_count = 0;
    for (int s = 0; s < skills_count(p->cls); s++) {
        const SkillDef *d = skills_get(p->cls, s);
        if (d->kind == SK_NONE)
            continue;
        if (p->skill_lvl[s] < base)
            p->skill_lvl[s] = base;
        if (d->passive || (skill_has_passive(d) && !d->aura))
            continue;
        const char *main, *off;
        st_weapons(p, d->kind, &main, &off);
        int k = 0;
        while (k < nsets && strcmp(sets[k], main) != 0)
            k++;
        if (k == nsets)
            sets[nsets++] = main;
    }
    int next[MAX_CLASS_SKILLS] = { 0 }, added = 1; /* por arma: desde qué habilidad sigue */
    while (added) {
        added = 0;
        for (int k = 0; k < nsets; k++) {
            int chunk = 0;
            for (int s = next[k]; s < skills_count(p->cls) && chunk < 4; s++) {
                const SkillDef *d = skills_get(p->cls, s);
                const char *main, *off;
                next[k] = s + 1;
                if (d->kind == SK_NONE || d->passive ||
                    (skill_has_passive(d) && !d->aura))
                    continue;
                st_weapons(p, d->kind, &main, &off);
                if (strcmp(main, sets[k]) != 0)
                    continue;
                br->show_list[br->show_count++] = s;
                chunk++;
                added = 1;
            }
        }
    }
    p->skill_points = 0;
    refresh_stats(p);
    if (s_diff > 0)
        demo_diff_gear(i);
    p->hp = p->hp_max;
    p->mana = p->mana_max;
    br->show_cur = br->show_count > 0 ? s_show_resume[p->cls] % br->show_count : 0;
    br->show_ticks = br->show_until = 0;
    printf("  demo J%d (%s): nivel %d, todas las habilidades; usa %d una por una\n", i + 1, game_class_name(p->cls),
           p->level_num, br->show_count);
}

static void demo_show_next(int i)
{
    DemoBrain *br = &s_brain[i];
    if (br->show_count > 0)
        br->show_cur = (br->show_cur + 1) % br->show_count;
    s_show_resume[s_pl[i].cls] = br->show_cur;
    br->show_ticks = 0;
    br->show_until = 0;
    br->show_presses = 0;
}

/* Se pone el arma que pide esa habilidad si no la tiene (o le quedan pocas
   flechas o jabalinas). Las de apoyo, con lo que tenga si sirve. */
static void demo_show_weapons(Player *p, const SkillDef *d)
{
    if (st_support(d->kind) && weapon_allows(p, d))
        return;
    /* Con un objeto de misión en la mano (la Voluntad de Khalim, hasta romper
       el Orbe) no se cambia: el Bárbaro se ponía dos hachas para Frenesí y
       el Orbe ya no se podía romper */
    if (is_quest_item(p->equip[SLOT_WEAPON].type) || is_quest_item(p->equip[SLOT_SHIELD].type))
        return;
    const char *main, *off;
    st_weapons(p, d->kind, &main, &off);
    const Item *w = &p->equip[SLOT_WEAPON], *o = &p->equip[SLOT_SHIELD];
    int same = w->type && strcmp(w->type->code, main) == 0 &&
               (off ? o->type && strcmp(o->type->code, off) == 0 : !o->type);
    int low = (w->type && w->amount > 0 && w->amount < 50) || (o->type && o->amount > 0 && o->amount < 50);
    if (!same || low)
        st_equip(p, d->kind);
}

/* Un arma o armadura en el piso cerca (para el Gólem de hierro) */
static int demo_metal_near(const Player *p)
{
    for (int k = 0; k < items_ground_count(); k++) {
        float x, y;
        const Item *g = items_ground_item(k, &x, &y);
        if (g && g->type && (g->type->kind == ITEM_WEAPON || g->type->kind == ITEM_ARMOR) && !is_quest_item(g->type) &&
            (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < 9.0f * 9.0f)
            return 1;
    }
    return 0;
}

/* La habilidad de ahora, con su arma puesta. Las que no se pueden usar acá
   (sin un cadáver al lado) se saltean. -1 si ninguna. */
static int demo_show_skill(int i)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    for (int tries = 0; tries < br->show_count; tries++) {
        int s = br->show_list[br->show_cur];
        const SkillDef *d = skills_get(p->cls, s);
        int ok = !((d->kind == SK_FIND_POTION || d->kind == SK_FIND_ITEM || d->kind == SK_GRIM_WARD) &&
                   units_find_corpse(p->x, p->y, 3.0f) < 0);
        /* las del Nigromante que levantan o hacen explotar un cadáver, y el
           Gólem de hierro (de un arma o armadura del piso) */
        if ((d->kind == SK_RAISE_SKELETON || d->kind == SK_RAISE_MAGE || d->kind == SK_CORPSE_EXPLOSION ||
             d->kind == SK_POISON_EXPLOSION) && nec_corpse(p, 0, 0) < 0)
            ok = 0;
        if (d->kind == SK_REVIVE && nec_corpse(p, 0, 1) < 0)
            ok = 0;
        if (d->kind == SK_IRON_GOLEM && !demo_metal_near(p))
            ok = 0;
        if (ok) {
            demo_show_weapons(p, d);
            if (weapon_allows(p, d))
                return s;
        }
        demo_show_next(i);
    }
    return -1;
}

/* Las invocaciones del Nigromante y las trampas de la Asesina: 1 si hace
   falta otra (le queda lugar o se murió alguna), 0 si no, -1 si no es de
   esas. Las que no se llenan (los gólems, los clones) van una vez. */
static int demo_summon_need(const Player *p, const SkillDef *d, int presses)
{
    switch (d->kind) {
    case SK_RAISE_SKELETON:
        return nec_corpse(p, 0, 0) >= 0 && demo_nec_room(p, "Raise Skeleton", PK_SKELETON);
    case SK_RAISE_MAGE:
        return nec_corpse(p, 0, 0) >= 0 && demo_nec_room(p, "Raise Skeletal Mage", PK_MAGE);
    case SK_REVIVE:
        return nec_corpse(p, 0, 1) >= 0 && demo_nec_room(p, "Revive", PK_REVIVE) && presses < 6;
    case SK_CLAY_GOLEM:
    case SK_BLOOD_GOLEM:
    case SK_FIRE_GOLEM:
    case SK_IRON_GOLEM: /* uno a la vez: si ya está vivo ese mismo, no */
        for (int k = 0; k < NEC_PETS; k++)
            if (p->pet_kind[k] == PK_GOLEM && units_pet_alive(p->pets[k]) &&
                strcasecmp(units_id(p->pets[k]), d->summon) == 0)
                return 0;
        return presses == 0;
    case SK_CB_SENTRY:
    case SK_WAKE_OF_FIRE:
    case SK_LIGHTNING_SENTRY:
    case SK_INFERNO_SENTRY:
    case SK_DEATH_SENTRY: { /* hasta 3 de esa (la más vieja de las 5 se va, como en el juego) */
        int same = 0, idx = (int)(d - skills_get(p->cls, 0));
        for (int k = 0; k < ASS_TRAPS; k++)
            same += p->trap_unit[k] >= 0 && p->trap_skill[k] == idx && units_pet_alive(p->trap_unit[k]);
        return same < 3 && presses < 3;
    }
    case SK_BLADE_SENTINEL:
        return p->sent_unit < 0 && presses == 0;
    /* Las del Druida: las que le faltan; la enredadera y el espíritu, una vez */
    case SK_RAVEN:
    case SK_SPIRIT_WOLF:
    case SK_DIRE_WOLF:
    case SK_GRIZZLY:
        return demo_dru_room(p, d->key) && presses < 5;
    /* Los demonios del Brujo: hasta los que puede tener */
    case SK_SUMMON_GOATMAN:
    case SK_SUMMON_TAINTED:
    case SK_SUMMON_DEFILER:
        return demo_war_room(p, d->key) && presses < 3;
    case SK_POISON_CREEPER:
    case SK_CARRION_VINE:
    case SK_SOLAR_CREEPER:
    case SK_OAK_SAGE:
    case SK_HEART_WOLVERINE:
    case SK_SPIRIT_BARBS:
        return presses == 0;
    default:
        return -1;
    }
}

/* El botón para este golpe con la habilidad de ahora (como demo_button) */
static int demo_show_button(int i, int target, float d)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    int s = demo_show_skill(i);
    if (s < 0)
        return d < MELEE_RANGE * 0.9f ? 0 : -1;
    const SkillDef *sd = skills_get(p->cls, s);
    int need = demo_summon_need(p, sd, br->show_presses);
    if (need >= 0) { /* las invocaciones y las trampas: solo hasta llenar (o reponer las que se pierden) */
        if (!need) {
            demo_show_next(i);
            return d < MELEE_RANGE * 0.9f ? 0 : -1;
        }
        if (target < 0 || d > 8.0f)
            return -1;
        demo_assign(i, 1, s);
        if (!demo_can_use(p, 1)) {
            demo_show_next(i);
            return -1;
        }
        return 1;
    }
    if (st_support(sd->kind)) {
        if (br->show_until) { /* ya la usó: el ataque común hasta pasar a la siguiente */
            if (s_ticks >= br->show_until)
                demo_show_next(i);
            return d < MELEE_RANGE * 0.9f ? 0 : -1;
        }
        if (++br->show_ticks > DEMO_SHOW_WAIT) {
            demo_show_next(i);
            return demo_show_button(i, target, d);
        }
        /* Conversión es un golpe: hay que estar al lado */
        if (target < 0 || d > (sd->kind == SK_CONVERSION ? MELEE_RANGE * 0.9f : 8.0f))
            return -1;
        demo_assign(i, 1, s);
        if (!demo_can_use(p, 1)) {
            demo_show_next(i);
            return -1;
        }
        return 1; /* demo_press anota cuándo la usó */
    }
    if (br->show_presses >= DEMO_SHOW_USES || ++br->show_ticks > DEMO_SHOW_TICKS) {
        demo_show_next(i);
        return demo_show_button(i, target, d);
    }
    float want = st_distance(sd->kind);
    if (want > 0.0f ? d > want + 1.0f : d >= MELEE_RANGE * 0.9f)
        return -1; /* acercarse */
    demo_assign(i, 1, s);
    return demo_can_use(p, 1) ? 1 : 0;
}

/* El botón para este golpe contra `target` (a `d` subtiles): el grito si
   hace falta, la especial si conviene, la de pegar si está al lado. Va
   cambiando lo que tiene en B e Y. -1 = nada desde acá (acercarse). */
static int demo_button(int i, int target, float d)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    int ids[16], near = units_enemies_near(p->x, p->y, 6.0f, ids, 16);
    int boss = target >= 0 && units_is_unique(target);
    int b = -1;
    if (p->buttons[2] >= 0 && s_ticks >= br->cry_at && demo_can_use(p, 2)) {
        int kind = skills_get(p->cls, p->buttons[2])->kind;
        int armor = kind == SK_FROZEN_ARMOR || kind == SK_SHIVER_ARMOR || kind == SK_CHILLING_ARMOR;
        int aura = skills_get(p->cls, p->buttons[2])->aura;
        if (aura ? p->aura < 0 || (!s_demo_all && p->aura != p->buttons[2])
                 : kind == SK_SHOUT ? p->shout_ticks == 0 : armor ? p->armor_ticks == 0
                 : kind == SK_BONE_ARMOR ? p->bone_armor <= 0 : kind == SK_QUICKNESS ? p->quick_ticks <= 0
                 : kind == SK_CYCLONE_ARMOR ? p->cyclone <= 0
                 : (near >= 2 || boss)) {
            armor |= kind == SK_BONE_ARMOR || kind == SK_QUICKNESS || kind == SK_CYCLONE_ARMOR;
            br->cry_at = s_ticks + (kind == SK_SHOUT || armor ? 2 : 6) * TICK_RATE;
            b = 2;
        }
    }
    if (b < 0 && s_demo_all)
        return demo_show_button(i, target, d);
    if (b < 0) {
        int sp = demo_special(i, target, d);
        if (sp != BTN_EMPTY) {
            demo_assign(i, 3, sp);
            if (demo_can_use(p, 3))
                b = 3;
        }
    }
    if (b < 0 && (d < MELEE_RANGE * 0.9f || ((p->cls == 2 || p->cls == 4 || p->cls == 5 || p->cls == 6 || p->cls == 7) &&
                                              d < 7.0f))) {
        /* La de pegar (la Hechicera y el Nigromante, de lejos), guardando maná para el grito */
        int reserve = 0;
        if (p->buttons[2] >= 0)
            reserve = skill_mana(skills_get(p->cls, p->buttons[2]), p->skill_lvl[p->buttons[2]]);
        int sg = p->cls == 7 ? skills_find(p->cls, "Summon Goatman") : -1;
        if (sg >= 0 && p->skill_lvl[sg] > 0 && demo_war_room(p, "Summon Goatman")) /* el Brujo junta maná para su demonio */
            reserve += skill_mana(skills_get(p->cls, sg), p->skill_lvl[sg]);
        int m = demo_melee(i, target, reserve);
        if (m >= 0)
            demo_assign(i, 1, m);
        b = m >= 0 && demo_can_use(p, 1) ? 1 : 0;
    }
    (void)near;
    (void)boss;
    return b;
}

/* ¿Hay un objeto de misión en el suelo al lado (el Mazo, el pergamino...) o
   algo de una misión que A tocaría (el soporte del Mazo, el árbol de Inifuss)? */
static int demo_quest_item_near(const Player *p, float range)
{
    int obj;
    if (quest_action(p, &obj))
        return 1;
    for (int k = 0; k < items_ground_count(); k++) {
        float x, y;
        const Item *g = items_ground_item(k, &x, &y);
        if (g && is_quest_item(g->type) && (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < range * range)
            return 1;
    }
    return 0;
}

/* Aprieta el botón (A también es levantar y hablar: solo si no hay otro).
   El que acompaña no aprieta A al lado de un objeto de misión: lo levantaría
   él y la misión es del primero (el líder que pelea de lejos lo deja ahí) */
static void demo_press(int i, PlayerInput *in, int b)
{
    if (b <= 0) {
        if (i > 0 && demo_quest_item_near(&s_pl[i], 2.5f))
            return;
        in->action = in->button[0] = 1;
        return;
    }
    in->button[b] = 1;
    int s = s_pl[i].buttons[b];
    if (s >= 0 && s_pl[i].state == PS_NORMAL) {
        DemoBrain *br = &s_brain[i];
        if (br->uses[s]++ == 0)
            printf("  demo J%d usa %s por primera vez (en %s)\n", i + 1, skills_get(s_pl[i].cls, s)->name,
                   k_button_names[b]);
        /* La de la rotación: una vez más (la de apoyo, ya está: pasa a la siguiente en un rato) */
        if (s_demo_all && b == 1 && br->show_count > 0 && s == br->show_list[br->show_cur]) {
            br->show_presses++;
            if (!br->show_until && st_support(skills_get(s_pl[i].cls, s)->kind))
                br->show_until = s_ticks + 2 * TICK_RATE;
        }
    }
}

/* Sin camino a pie: Teletransporte, la Hoja de distorsión y el Salto cruzan
   las paredes que solo son COLL_WALL (como en el original) y lo dejaban en
   un hueco cerrado de una cueva, donde se quedaba hasta que se pasaba el
   tiempo. Sale de la misma forma: hacia el compañero si está en la misma
   zona, si no hacia adonde va, y cada intento gira 45 grados. 1 = en eso */
static int demo_escape(int i, PlayerInput *in, AtBot *b, float x, float y)
{
    static const char *const k_keys[] = { "Teleport", "Blade Warp", "Leap" };
    Player *p = &s_pl[i];
    int sk = -1;
    for (int k = 0; k < 3 && sk < 0; k++)
        sk = demo_skill(p, k_keys[k], 0);
    if (sk < 0)
        return 0;
    float dx = x - p->x, dy = y - p->y;
    for (int k = 0; k < s_num_players; k++)
        if (k != i && s_pl[k].state < PS_DYING && zone_at(s_pl[k].x, s_pl[k].y) == zone_at(p->x, p->y)) {
            dx = s_pl[k].x - p->x;
            dy = s_pl[k].y - p->y;
        }
    float a = atan2f(dy, dx) + (float)(s_brain[i].escapes % 8) * PI_F / 4.0f;
    at_move(in, cosf(a), sinf(a)); /* (mira hacia ahí) */
    if (b->pulse++ % 4 == 0 && p->state == PS_NORMAL) {
        demo_assign(i, 3, sk);
        demo_press(i, in, 3);
        if (s_brain[i].escapes++ == 0)
            printf("  demo J%d no tiene camino: prueba salir con %s\n", i + 1, skills_get(p->cls, sk)->name);
    }
    return 1;
}

/* ---- Equipo y tiendas ---- */

/* Se pone lo mejor que tenga en la mochila (un objeto por vez) */
static void demo_equip(int i)
{
    Player *p = &s_pl[i];
    if (s_ticks < s_brain[i].equip_at)
        return;
    for (int k = 0; k < p->bag_count; k++) {
        const Item *it = &p->bag[k];
        if (it->type->slot == SLOT_NONE || !meets_item(p, it) || !demo_upgrade(p, it))
            continue;
        char name[64];
        items_label(it, name, sizeof name);
        /* El lugar se guarda antes: al ponérselo la mochila se corre y `it`
           ya apunta a otra cosa (una poción no tiene lugar: equip[-1]) */
        int slot = equip_slot_for(p, it->type), before = demo_slot_value(p, slot);
        Item was[SLOT_COUNT];
        memcpy(was, p->equip, sizeof was);
        equip_from_bag(p, k);
        if (memcmp(was, p->equip, sizeof was) == 0) { /* no entra lo que se saca: no insiste */
            s_brain[i].equip_at = s_ticks + 30 * TICK_RATE;
            return;
        }
        refresh_look(p);
        s_brain[i].equipped++;
        printf("  demo J%d se pone %s (valor %d -> %d)\n", i + 1, name, before, demo_slot_value(p, slot));
        return;
    }
}

/* El que vende pociones en el pueblo de este acto */
static const Shop *demo_potion_shop(void)
{
    return &s_shops[s_act == 5   ? VENDOR_MALAH
                    : s_act == 4 ? VENDOR_JAMELLA
                    : s_act == 3 ? VENDOR_ALKOR
                    : s_act == 2 ? VENDOR_LYSANDER
                                 : VENDOR_AKARA];
}

/* La mejor poción de vida (kind 0) o maná (1) que vende y le alcanza */
static const Item *demo_shop_potion(int kind, int gold)
{
    const Shop *sh = demo_potion_shop();
    const Item *best = NULL;
    for (int k = 0; k < sh->count; k++) {
        const ItemType *t = sh->stock[k].type;
        if (!t || t->heal_pct || !(kind == 0 ? t->heal_life : t->heal_mana) || items_price(&sh->stock[k], 0) > gold)
            continue;
        if (!best || (kind == 0 ? t->heal_life > best->type->heal_life : t->heal_mana > best->type->heal_mana))
            best = &sh->stock[k];
    }
    return best;
}

/* En el pueblo: vende lo que no usa, compra mejoras y pociones. Va directo
   (no pasa por las pantallas de las tiendas, que ya prueba la prueba) */
static void demo_shop(int i)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    if (!p->level || !p->level->town || (br->shopped && s_ticks - br->shopped < 30 * TICK_RATE))
        return;
    br->shopped = s_ticks;
    int gold0 = p->gold, sold = 0, bought = 0;
    for (int k = p->bag_count - 1; k >= 0; k--) {
        const Item *it = &p->bag[k];
        /* las gemas, runas y joyas que levantó el jugador también se venden */
        if ((it->type->slot == SLOT_NONE && !it->type->socket_filler) || is_quest_item(it->type) || demo_keep(p, it))
            continue;
        p->gold += items_price(it, 1);
        bag_remove(p, k);
        sold++;
    }
    /* Mejoras: lo mejor que haya para cada lugar, guardando para pociones */
    const Item *pot = demo_shop_potion(0, 1 << 30);
    int reserve = pot ? items_price(pot, 0) * 6 : 200;
    for (int slot = 0; slot < SLOT_COUNT; slot++) {
        Shop *best_sh = NULL;
        int best_k = -1, best_v = 0;
        for (int v = 0; v < VENDOR_COUNT; v++) {
            Shop *sh = &s_shops[v];
            if (sh->act != s_act) /* los de este pueblo */
                continue;
            for (int k = 0; k < sh->count; k++) {
                const Item *it = &sh->stock[k];
                if (it->type->slot != slot || !meets_item(p, it) || !demo_upgrade(p, it) ||
                    items_price(it, 0) > p->gold - reserve)
                    continue;
                int val = demo_item_value(p, it);
                if (val > best_v) {
                    best_sh = sh;
                    best_k = k;
                    best_v = val;
                }
            }
        }
        if (!best_sh)
            continue;
        Item it = best_sh->stock[best_k];
        best_sh->stock[best_k] = best_sh->stock[--best_sh->count];
        p->gold -= items_price(&it, 0);
        Item old = p->equip[slot];
        p->equip[slot] = it;
        if (old.type && (!is_quest_item(old.type) || !bag_add(p, &old))) /* una pieza del Báculo no se vende */
            p->gold += items_price(&old, 1);
        if (slot == SLOT_WEAPON && !items_offhand_ok(it.type, p->equip[SLOT_SHIELD].type)) {
            p->gold += items_price(&p->equip[SLOT_SHIELD], 1);
            p->equip[SLOT_SHIELD].type = NULL;
        }
        refresh_look(p);
        char name[64];
        printf("  demo J%d compra %s en %s (%d de oro)\n", i + 1, items_label(&it, name, sizeof name), best_sh->npc,
               items_price(&it, 0));
        bought++;
        br->equipped++;
    }
    /* Pociones: el cinturón lleno (una de maná si usa habilidades) y dos de
       repuesto. Las de maná que levantó de más (en el cinturón y en la
       mochila) se venden: si no, el cinturón queda lleno de maná */
    int uses_mana = p->buttons[1] >= 0 || p->buttons[2] >= 0 || p->buttons[3] >= 0, mana_kept = 0;
    for (int k = 0; k < BELT_SLOTS; k++) {
        const ItemType *t = p->belt[k].type;
        if (!t || t->heal_life || t->heal_pct || !t->heal_mana)
            continue;
        if (mana_kept++ < (p->cls == 2 || p->cls == 4 || p->cls == 5 || p->cls == 6 || p->cls == 7 ? 2 : uses_mana ? 1 : 0)) /* los que usan hechizos, con dos */
            continue;
        p->gold += items_price(&p->belt[k], 1);
        memset(&p->belt[k], 0, sizeof p->belt[k]);
        sold++;
    }
    for (int k = p->bag_count - 1; k >= 0; k--) {
        const ItemType *t = p->bag[k].type;
        if (p->cls != 2 && p->cls != 4 && p->cls != 5 && p->cls != 6 && p->cls != 7 && t->heal_mana && !t->heal_life &&
            !t->heal_pct) {
            p->gold += items_price(&p->bag[k], 1);
            bag_remove(p, k);
            sold++;
        }
    }
    for (int k = 0; k < BELT_SLOTS + 2; k++) {
        int in_belt = k < BELT_SLOTS;
        if (in_belt && p->belt[k].type)
            continue;
        int kind = in_belt && k == BELT_SLOTS - 1 && uses_mana && !demo_belt_has(p, 1) ? 1 : 0;
        const Item *it = demo_shop_potion(kind, p->gold);
        if (!it)
            break;
        if (in_belt)
            p->belt[k] = *it;
        else if (demo_bag_potions(p) >= 2 || !bag_add(p, it))
            break;
        p->gold -= items_price(it, 0);
        bought++;
    }
    /* Antídotos para Andariel (y lo que envenena): dos en la mochila */
    if (p->level_num >= 8 || p->andy_q >= 1) {
        const Shop *ak = demo_potion_shop();
        for (int k = 0; k < ak->count && demo_antidotes(p) < 2; k++) {
            const Item *it = &ak->stock[k];
            if (!it->type->antidote_ticks || items_price(it, 0) > p->gold || !bag_add(p, it))
                continue;
            p->gold -= items_price(it, 0);
            bought++;
            k--; /* las pociones no se acaban: otra igual */
        }
    }
    bought += demo_buy_tp(p);
    br->sold += sold;
    br->bought += bought;
    if (sold || bought)
        printf("  demo J%d en el pueblo: vende %d, compra %d (oro %d -> %d, %d portales)\n", i + 1, sold, bought, gold0,
               p->gold, demo_tp_charges(p));
}

/* ---- Pociones y vuelta al pueblo ---- */

static void demo_potions(int i, PlayerInput *in)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    if (p->state >= PS_DYING || (p->level && p->level->town) || s_ticks < br->potion_at)
        return;
    /* Sin pociones de vida en el cinturón y alguna en la mochila: la pasa
       (como "usar" en el inventario); si no, ni la toma ni vuelve al pueblo */
    for (int k = 0; k < p->bag_count && !demo_belt_has(p, 0); k++) {
        if (!demo_potion_kind(p->bag[k].type, 0))
            continue;
        int slot = -1;
        for (int s = 0; s < BELT_SLOTS && slot < 0; s++)
            if (!p->belt[s].type)
                slot = s;
        if (slot < 0) { /* el cinturón lleno de otras (las de maná que levantó): cambia una */
            Item other = p->belt[0];
            p->belt[0] = potion_take_one(&p->bag[k]);
            if (p->bag[k].amount <= 0)
                bag_remove(p, k);
            if (!bag_add(p, &other))
                p->gold += items_price(&other, 1); /* (no entra: como si la vendiera) */
            break;
        }
        p->belt[slot] = potion_take_one(&p->bag[k]);
        if (p->bag[k].amount <= 0)
            bag_remove(p, k);
    }
    if (p->poison_ticks > 0 && p->antidote_ticks == 0) {
        for (int k = 0; k < p->bag_count; k++)
            if (p->bag[k].type->antidote_ticks > 0) {
                apply_potion(p, p->bag[k].type);
                if (is_potion(p->bag[k].type) && stack_count(&p->bag[k]) > 1)
                    potion_take_one(&p->bag[k]);
                else
                    bag_remove(p, k);
                br->potions++;
                br->potion_at = s_ticks + TICK_RATE / 2;
                printf("  demo J%d toma un antídoto\n", i + 1);
                return;
            }
    }
    if (p->hp * 100 < p->hp_max * 45 && (p->regen_life_ticks == 0 || p->hp * 100 < p->hp_max * 20) &&
        demo_belt_has(p, 0)) {
        in->potion_life = 1;
        br->potions++;
        br->boss_potions++;
        br->potion_at = s_ticks + TICK_RATE;
    } else if (p->mana * 100 < p->mana_max * 15 && p->regen_mana_ticks == 0 && demo_belt_has(p, 1) &&
               units_find_enemy(p->x, p->y, 8.0f) >= 0) {
        in->potion_mana = 1;
        br->potions++;
        br->potion_at = s_ticks + TICK_RATE;
    }
}

static int demo_has_heal(const Player *p)
{
    if (demo_belt_has(p, 0))
        return 1;
    for (int k = 0; k < p->bag_count; k++)
        if (demo_potion_kind(p->bag[k].type, 0))
            return 1;
    return 0;
}

/* Poca vida y sin pociones: al pueblo a curarse (y comprar). Con un
   pergamino o el tomo, por su portal al pueblo y de vuelta por el mismo
   (como un jugador); si no, por waypoint: el primero vuelve al mismo y el
   segundo a donde esté el primero. 1 = está en eso. */
static int demo_retreat(int i, PlayerInput *in)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    int town = p->level && p->level->town;
    AtStep st = { AT_WAYPOINT, NULL, wp_first(s_act), NULL, NULL, 0 }; /* el del pueblo de este acto */
    switch (br->retreat) {
    case 0:
        if (town || p->hp * 100 >= p->hp_max * 30 || demo_has_heal(p) || p->dialog_npc >= 0)
            return 0;
        /* (en la cumbre con el rito empezado, el portal haría volver a los Antiguos a sus estatuas: por waypoint) */
        int rite = p->level && p->level->id == 120 && p->ancients_q == 2;
        if (!rite && tp_find(p) >= 0 && tp_cast(p, tp_find(p))) {
            br->retreat = 10;
            br->retreats++;
            printf("  demo J%d (nivel %d) con %d/%d de vida y sin pociones: abre un portal al pueblo\n", i + 1,
                   p->level_num, p->hp, p->hp_max);
            return 1;
        }
        br->ret_wp = demo_nearest_wp(p, NULL);
        if (br->ret_wp < 0)
            return 0; /* ningún waypoint: sigue como pueda */
        br->retreat = 1;
        br->retreats++;
        printf("  demo J%d (nivel %d) con %d/%d de vida y sin pociones: vuelve al pueblo\n", i + 1, p->level_num, p->hp,
               p->hp_max);
        /* fallthrough */
    case 1:
        if (at_player_step(i, &st, in) == 1)
            br->retreat = 2;
        return 1;
    case 10: /* entra a su portal (se abrió al lado) */
    case 12: { /* curado: vuelve por el mismo, desde el pueblo */
        int id = br->retreat == 10 ? PORTAL_TP_FIELD(i) + 1 : PORTAL_TP_TOWN(i) + 1;
        const Warp *w = NULL;
        for (int k = 0; k < world_warp_count() && !w; k++)
            if (world_warp(k)->portal == id)
                w = world_warp(k);
        if (!w) { /* (se cerró: sigue como pueda) */
            br->retreat = 0;
            return 0;
        }
        float wx = (w->x0 + w->x1) / 2, wy = (w->y0 + w->y1) / 2;
        if ((wx - p->x) * (wx - p->x) + (wy - p->y) * (wy - p->y) > 6.0f * 6.0f) {
            br->retreat = 0;
            return 0;
        }
        int back = br->retreat == 12;
        use_warp(p, w);
        br->retreat = back ? 0 : 11;
        if (back)
            printf("  demo J%d vuelve por su portal (%d pociones)\n", i + 1, demo_bag_potions(p) + 4);
        return 1;
    }
    case 11: /* en el pueblo, por el portal: se cura, compra y vuelve */
        if (p->hp * 100 < p->hp_max * 95 || !br->shopped || s_ticks - br->shopped > 10 * TICK_RATE)
            return 1;
        br->retreat = 12;
        return 1;
    case 2: /* en el pueblo la vida se llena sola; las compras las hace demo_shop */
        if (p->hp * 100 < p->hp_max * 95 || !br->shopped || s_ticks - br->shopped > 10 * TICK_RATE)
            return 1;
        br->retreat = i == 0 && br->ret_wp > 0 ? 3 : 0;
        if (!br->retreat)
            printf("  demo J%d curado: vuelve con el otro\n", i + 1);
        return 1;
    default:
        st.num = br->ret_wp;
        if (at_player_step(i, &st, in) == 1) {
            br->retreat = 0;
            printf("  demo J%d vuelve curado (%d pociones)\n", i + 1, demo_bag_potions(p) + 4);
        }
        return 1;
    }
}

/* ---- Pelear y levantar cosas ---- */

/* Cuánto le "cuesta" ir hacia (x, y): la distancia, y bastante más si es la
   dirección en la que se acaba de trabar (así elige uno hacia otro lado) */
static float demo_target_cost(const DemoBrain *br, const Player *p, float x, float y)
{
    float dx = x - p->x, dy = y - p->y, d2 = dx * dx + dy * dy, l = sqrtf(d2);
    if (s_ticks < br->stuck_until && l > 0.01f) {
        float c = (dx * br->stuck_dx + dy * br->stuck_dy) / l;
        if (c > 0.3f)
            d2 *= 1.0f + 8.0f * c;
    }
    return d2;
}

/* ¿Se sigue acercando al monstruo? Si en `secs` segundos no se acercó al
   menos `step` subtiles (un mueble, una columna o una reja en el medio), lo
   deja un rato, anota hacia dónde estaba y devuelve 0. */
static int demo_progress(int i, int target, float tx, float ty, int secs, float step)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    float dx = tx - p->x, dy = ty - p->y, d = sqrtf(dx * dx + dy * dy);
    if (target != br->hunt) {
        br->hunt = target;
        br->hunt_best = d;
        br->hunt_since = s_ticks;
        return 1;
    }
    if (d < br->hunt_best - step) {
        br->hunt_best = d;
        br->hunt_since = s_ticks;
        return 1;
    }
    int boss = units_is_unique(target); /* a los jefes no se los deja así nomás */
    if (s_ticks - br->hunt_since < (uint32_t)((boss ? 12 : secs) * TICK_RATE))
        return 1;
    demo_ignore(br->ignore, target, boss ? 10 : 45);
    if (d > 0.01f) {
        br->stuck_dx = dx / d;
        br->stuck_dy = dy / d;
        br->stuck_until = s_ticks + 20 * TICK_RATE;
    }
    br->hunt = -1;
    br->target = -1;
    printf("  demo J%d no llega a %s (hay algo en el medio): busca otro hacia otro lado\n", i + 1,
           units_display_name(target));
    return 0;
}

/* Pelea con el monstruo más cercano (hasta `range`): se acerca y le pega con
   lo que convenga. Si no se le acerca o no le baja la vida (algo en el medio),
   lo deja y elige otro, mejor hacia otro lado. 1 = está peleando. */
static int demo_engage(int i, PlayerInput *in, float range)
{
    Player *p = &s_pl[i];
    AtBot *b = &s_at.bot[i];
    DemoBrain *br = &s_brain[i];
    if (!p->level || p->level->town)
        return 0;
    int ids[32], n = units_enemies_near(p->x, p->y, range, ids, 32), best = -1;
    float bd = 1e9f;
    for (int k = 0; k < n; k++) {
        float ux, uy;
        if (demo_ignored(br->ignore, ids[k]))
            continue;
        units_position(ids[k], 1.0f, &ux, &uy);
        float d = demo_target_cost(br, p, ux, uy);
        if (d < bd) {
            bd = d;
            best = ids[k];
        }
    }
    if (best < 0) {
        br->target = -1;
        return 0;
    }
    int hp, max_hp;
    units_health(best, &hp, &max_hp);
    if (best != br->target) {
        br->target = best;
        br->target_hp = hp;
        br->target_since = s_ticks;
    } else if (s_ticks - br->target_since > (uint32_t)(units_is_unique(best) ? 10 : 5) * TICK_RATE) {
        if (hp >= br->target_hp) { /* no le hace nada: algo en el medio */
            demo_ignore(br->ignore, best, units_is_unique(best) ? 10 : 40);
            float tx, ty;
            units_position(best, 1.0f, &tx, &ty);
            float dx = tx - p->x, dy = ty - p->y, l = sqrtf(dx * dx + dy * dy);
            if (l > 0.01f) {
                br->stuck_dx = dx / l;
                br->stuck_dy = dy / l;
                br->stuck_until = s_ticks + 20 * TICK_RATE;
            }
            br->target = -1;
            return 0;
        }
        br->target_hp = hp;
        br->target_since = s_ticks;
    }
    float ux, uy;
    units_position(best, 1.0f, &ux, &uy);
    float d = sqrtf((ux - p->x) * (ux - p->x) + (uy - p->y) * (uy - p->y));
    int btn = demo_button(i, best, d);
    if (btn >= 0) {
        in->move_x = in->move_y = 0.0f;
        if (s_demo_all && p->state == PS_NORMAL) { /* el Salto y el Torbellino van hacia donde mira */
            float dx = ux - p->x, dy = uy - p->y, l = sqrtf((dx - dy) * (dx - dy) + (dx + dy) * (dx + dy) * 0.25f);
            if (l > 0.01f) {
                p->face_x = (dx - dy) / l;
                p->face_y = (dx + dy) * 0.5f / l;
                p->sector = sector_from_screen(p->face_x, p->face_y);
            }
        }
        if (b->pulse++ % 4 == 0)
            demo_press(i, in, btn);
        return 1;
    }
    if (!demo_progress(i, best, ux, uy, 4, 1.0f))
        return 1;
    if (at_goto(b, p, in, zone_at(ux, uy), ux, uy, 1.2f) < 0) {
        demo_ignore(br->ignore, best, 60);
        br->target = -1;
        b->no_path = 0;
    }
    return 1;
}

/* Después de pelear revisa los cadáveres de al lado: Buscar poción si le
   faltan, si no Buscar objeto (Bárbaro); con todas las habilidades, cada
   tanto pone un Tótem lúgubre. 1 = está en eso. */
static int demo_search(int i, PlayerInput *in)
{
    Player *p = &s_pl[i];
    DemoBrain *br = &s_brain[i];
    if (p->cls != 0 || s_ticks < br->search_at || units_find_enemy(p->x, p->y, 6.0f) >= 0 ||
        units_find_corpse(p->x, p->y, 2.5f) < 0)
        return 0;
    int free_belt = 0;
    for (int k = 0; k < BELT_SLOTS; k++)
        free_belt += !p->belt[k].type;
    int s = -1;
    if (s_demo_all && s_ticks >= br->ward_at && (s = demo_skill(p, "Grim Ward", 0)) >= 0)
        br->ward_at = s_ticks + 40 * TICK_RATE;
    if (s < 0)
        s = free_belt > 0 || demo_bag_potions(p) < 2 ? demo_skill(p, "Find Potion", 0) : -1;
    if (s < 0)
        s = demo_skill(p, "Find Item", 0);
    if (s < 0)
        return 0;
    demo_assign(i, 3, s);
    in->move_x = in->move_y = 0.0f;
    if (s_at.bot[i].pulse++ % 4 == 0) {
        demo_press(i, in, 3);
        br->search_at = s_ticks + TICK_RATE; /* uno por segundo, como mucho */
    }
    return 1;
}

/* Va a levantar algo del suelo que le sirva (si no hay monstruos cerca).
   1 = está yendo. */
static int demo_loot(int i, PlayerInput *in, float range)
{
    Player *p = &s_pl[i];
    AtBot *b = &s_at.bot[i];
    DemoBrain *br = &s_brain[i];
    if (units_find_enemy(p->x, p->y, 5.0f) >= 0 || p->bag_count >= BAG_MAX)
        return 0;
    float x, y;
    const Item *it = br->item >= 0 ? items_ground_item(br->item, &x, &y) : NULL;
    if (br->item >= 0 && !it) { /* ya no está: lo levantó él (si estaba al lado) o alguien más */
        if ((br->item_x - p->x) * (br->item_x - p->x) + (br->item_y - p->y) * (br->item_y - p->y) < 2.2f * 2.2f)
            printf("  demo J%d levanta %s\n", i + 1, br->item_name);
        br->item = -1;
    }
    if (br->item >= 0 && s_ticks - br->item_since > 10 * TICK_RATE) { /* no llega */
        demo_ignore(br->ignore_item, br->item, 60);
        br->item = -1;
    }
    if (br->item < 0) {
        float bd = range * range;
        int z = zone_at(p->x, p->y);
        for (int k = 0; k < items_ground_count(); k++) {
            const Item *g = items_ground_item(k, &x, &y);
            if (!g || demo_ignored(br->ignore_item, k) || zone_at(x, y) != z || !demo_wants(p, g))
                continue;
            int taken = 0; /* el otro ya va a buscarlo */
            for (int o = 0; o < s_num_players; o++)
                taken |= o != i && s_brain[o].item == k;
            if (taken)
                continue;
            float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
            if (d < bd) {
                bd = d;
                br->item = k;
            }
        }
        if (br->item < 0)
            return 0;
        br->item_since = s_ticks;
        items_label(items_ground_item(br->item, NULL, NULL), br->item_name, sizeof br->item_name);
    }
    it = items_ground_item(br->item, &x, &y);
    br->item_x = x;
    br->item_y = y;
    float d = sqrtf((x - p->x) * (x - p->x) + (y - p->y) * (y - p->y));
    if (strcmp(it->type->code, "gld") != 0 && d < 1.4f) {
        if (i > 0 && demo_quest_item_near(p, 2.5f)) { /* A tocaría lo de la misión: lo deja */
            demo_ignore(br->ignore_item, br->item, 60);
            br->item = -1;
            return 0;
        }
        if (is_quest_item(it->type)) { /* lo de la misión: si no entra, deja algo (y no lo vuelve a levantar) */
            int dropped = at_make_room(p, it->type, x, y);
            if (dropped >= 0) {
                demo_ignore(br->ignore_item, dropped, 600);
                return 1;
            }
        }
        in->move_x = in->move_y = 0.0f;
        if (b->pulse++ % 3 == 0)
            in->action = in->button[0] = 1;
        return 1;
    }
    if (at_goto(b, p, in, zone_at(x, y), x, y, 0.5f) < 0) {
        demo_ignore(br->ignore_item, br->item, 60);
        br->item = -1;
        b->no_path = 0;
    }
    return 1;
}

/* ---- Farmear antes de los jefes ---- */

/* Ir a un nivel: por waypoint si lo tiene y caminando queda más lejos */
/* El waypoint que más acerca a la zona `goal` (contando las zonas que hay
   que cruzar caminando hasta el waypoint más cercano y desde el de
   llegada), o -1 si caminando es igual o mejor */
static int demo_best_wp(const Player *p, int goal)
{
    int z = zone_at(p->x, p->y), near_hops, best = -1;
    if (z == goal || demo_nearest_wp(p, &near_hops) < 0)
        return -1;
    int walk = demo_hops(z, goal), best_cost = walk < 0 ? 1 << 20 : walk;
    for (int k = 0; k < WP_MAX; k++) {
        if (!((p->waypoints >> k) & 1) || world_waypoint_zone(k) < 0 || world_waypoint_zone(k) == z)
            continue;
        int h = demo_hops(world_waypoint_zone(k), goal);
        if (h >= 0 && near_hops + 1 + h + 1 < best_cost) {
            best = k;
            best_cost = near_hops + 1 + h;
        }
    }
    return best;
}

static void demo_go_level(int i, PlayerInput *in, const Level *lv)
{
    Player *p = &s_pl[i];
    int z = zone_at(p->x, p->y), wp = demo_best_wp(p, lv->zone);
    if (wp >= 0) {
        AtStep st = { AT_WAYPOINT, NULL, wp, NULL, NULL, 0 };
        at_player_step(i, &st, in);
        return;
    }
    if (p->wp_open) { /* se abrió solo al pasar por uno nuevo */
        if (s_at.bot[i].pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return;
    }
    float x = (lv->x0 + lv->x1) * TILE_SUB / 2.0f, y = (lv->y0 + lv->y1) * TILE_SUB / 2.0f;
    if (world_zone_loaded(lv->zone) && z == lv->zone)
        level_free_spot(lv, &x, &y);
    at_travel(&s_at.bot[i], p, in, lv->zone, x, y, 1.5f);
}

/* ¿Está listo para el jefe? */
static int demo_ready(const Player *p, const DemoNeed *nd)
{
    return p->level_num >= nd->clvl && p->hp_max >= nd->life;
}

static int demo_is_need_level(const Level *lv)
{
    for (int n = 0; n < DEMO_NEEDS; n++)
        if (strcmp(lv->name, k_demo_needs[n].level) == 0)
            return 1;
    return 0;
}

/* La zona más conveniente para farmear: de las que conoce (o están de camino
   al jefe), la de monstruos de más nivel que no lo pasen por más de uno, y
   que no haya limpiado hace poco (tardan en volver). NULL = ninguna. */
static const Level *demo_farm_level(int i, const Level *goal)
{
    const Player *p = &s_pl[i];
    const DemoBrain *br = &s_brain[i];
    uint64_t can = br->seen_levels;
    /* Las vecinas (un paso caminando) de las que conoce */
    for (int k = 0; k < world_level_count() && k < DEMO_LEVELS; k++) {
        if (!((br->seen_levels >> k) & 1))
            continue;
        for (int t = 0; t < world_zone_count(); t++) {
            int next = world_route_next(world_level(k)->zone, t);
            for (int j = 0; next >= 0 && j < world_level_count() && j < DEMO_LEVELS; j++)
                if (world_level(j)->zone == next)
                    can |= 1ull << j;
        }
    }
    int z = zone_at(p->x, p->y);
    for (int hops = 0; goal && z != goal->zone && z >= 0 && hops < 64; hops++) {
        z = world_route_next(z, goal->zone);
        for (int k = 0; z >= 0 && k < world_level_count() && k < DEMO_LEVELS; k++)
            if (world_level(k)->zone == z)
                can |= 1ull << k;
    }
    const Level *best = NULL;
    int best_hops = 0, here = zone_at(p->x, p->y);
    for (int k = 0; k < world_level_count() && k < DEMO_LEVELS; k++) {
        const Level *lv = world_level(k);
        if (!((can >> k) & 1) || ((br->barren >> k) & 1) || lv->town || demo_is_need_level(lv) ||
            lv->mon_level > p->level_num + 1 ||
            (br->cleared[k] && s_ticks - br->cleared[k] < ZONE_RESET_SECONDS * TICK_RATE))
            continue;
        int hops = demo_hops(here, lv->zone);
        if (hops < 0)
            continue;
        if (!best || lv->mon_level > best->mon_level || (lv->mon_level == best->mon_level && hops < best_hops)) {
            best = lv;
            best_hops = hops;
        }
    }
    return best;
}

/* Farmea para el jefe `ni` mientras no esté listo. 1 = está en eso. */
static int demo_farm(PlayerInput *in, int ni, const Level *goal)
{
    Player *p = &s_pl[0];
    AtBot *b = &s_at.bot[0];
    DemoBrain *br = &s_brain[0];
    const DemoNeed *nd = &k_demo_needs[ni];
    if ((br->gave_up >> ni) & 1)
        return 0;
    if (demo_ready(p, nd)) {
        if (br->farm_need == ni) {
            printf("  demo J1 listo para %s: nivel %d, vida %d (farmeó %.1f minutos)\n", nd->boss, p->level_num,
                   p->hp_max, (s_ticks - br->farm_start) / (60.0 * TICK_RATE));
            br->farm = NULL;
            br->farm_need = -1;
        } else if (!((br->ready_told >> ni) & 1)) {
            printf("  demo J1 listo para %s: nivel %d, vida %d (pide nivel %d y vida %d)\n", nd->boss, p->level_num,
                   p->hp_max, nd->clvl, nd->life);
        }
        br->ready_told |= 1 << ni;
        return 0;
    }
    if (br->farm_need != ni) {
        br->farm_need = ni;
        br->farm_start = s_ticks;
        br->farm = NULL;
        int lo, hi;
        player_damage(p, &lo, &hi);
        printf("  demo J1 no está listo para %s: nivel %d, vida %d y daño %d-%d; pide nivel %d y vida %d\n", nd->boss,
               p->level_num, p->hp_max, lo, hi, nd->clvl, nd->life);
    }
    if (s_ticks - br->farm_start > DEMO_FARM_CAP) {
        printf("  demo J1 farmeó %d minutos y sigue sin estar listo para %s (nivel %d, vida %d): va igual\n",
               DEMO_FARM_CAP / (60 * TICK_RATE), nd->boss, p->level_num, p->hp_max);
        br->gave_up |= 1 << ni;
        br->farm = NULL;
        return 0;
    }
    if (!br->farm) {
        br->farm = demo_farm_level(0, goal);
        if (!br->farm) {
            printf("  demo J1 no tiene dónde farmear para %s: va igual\n", nd->boss);
            br->gave_up |= 1 << ni;
            return 0;
        }
        printf("  demo J1 (nivel %d) farmea en %s (monstruos de nivel %d)\n", p->level_num, br->farm->name,
               br->farm->mon_level);
        br->farm_in = 0;
    }
    br->farm_ticks++;
    if (p->level != br->farm) {
        if (demo_engage(0, in, DEMO_ENGAGE) || demo_loot(0, in, DEMO_LOOT_RANGE))
            return 1;
        demo_go_level(0, in, br->farm);
        return 1;
    }
    if (!br->farm_in) {
        br->farm_in = 1;
        br->farm_exp = p->exp;
    }
    if (demo_engage(0, in, DEMO_ENGAGE) || demo_loot(0, in, DEMO_LOOT_RANGE))
        return 1;
    /* El monstruo vivo más cercano del nivel */
    int target = -1;
    float bd = 1e9f, tx = 0.0f, ty = 0.0f;
    for (int u = 0; u < units_count(); u++) {
        float x, y;
        if (!units_alive_enemy(u) || demo_ignored(br->ignore, u))
            continue;
        units_position(u, 1.0f, &x, &y);
        if (world_level_at(x, y) != br->farm)
            continue;
        float d = demo_target_cost(br, p, x, y);
        if (d < bd) {
            bd = d;
            target = u;
            tx = x;
            ty = y;
        }
    }
    if (target < 0) {
        int k = demo_level_index(br->farm);
        if (k >= 0)
            br->cleared[k] = s_ticks;
        if (k >= 0 && p->exp == br->farm_exp)
            br->barren |= 1ull << k;
        printf("  demo J1 limpió %s (nivel %d, %u de experiencia)\n", br->farm->name, p->level_num, (unsigned)p->exp);
        br->farm = NULL; /* el próximo tick elige otra */
        return 1;
    }
    if (!demo_progress(0, target, tx, ty, 10, 2.0f))
        return 1;
    if (at_travel(b, p, in, zone_at(tx, ty), tx, ty, 1.2f) < 0) {
        demo_ignore(br->ignore, target, 120);
        b->no_path = 0;
    }
    return 1;
}

/* ---- Cada tick ---- */

/* Lo que no lleva controles: puntos, habilidades, botones, equipo, tiendas */
static void demo_tick(void)
{
    static const ItemType *s_quest_hand[MAX_PLAYERS]; /* lo de la misión que tenía en la mano */
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        DemoBrain *br = &s_brain[i];
        const ItemType *hand = is_quest_item(p->equip[SLOT_WEAPON].type)   ? p->equip[SLOT_WEAPON].type
                               : is_quest_item(p->equip[SLOT_SHIELD].type) ? p->equip[SLOT_SHIELD].type
                                                                           : NULL;
        if (s_quest_hand[i] && hand != s_quest_hand[i]) /* para ver quién se la saca */
            printf("  demo J%d ya no tiene %s en la mano: arma %s\n", i + 1, s_quest_hand[i]->name,
                   p->equip[SLOT_WEAPON].type ? p->equip[SLOT_WEAPON].type->name : "-");
        s_quest_hand[i] = hand;
        int k = p->level ? demo_level_index(p->level) : -1;
        if (k >= 0)
            br->seen_levels |= 1ull << k;
        if (s_god_saves[i] != br->saves_seen) { /* la vida llegó a 1: hubiera muerto */
            br->saves_seen = s_god_saves[i];
            if (br->near_death++ % 20 == 0) {
                int e = units_find_enemy(p->x, p->y, 10.0f);
                printf("  demo J%d (nivel %d, vida %d) casi muere en %s%s%s\n", i + 1, p->level_num, p->hp_max,
                       p->level ? p->level->name : "?", e >= 0 ? " contra " : "", e >= 0 ? units_display_name(e) : "");
            }
        }
        if (p->state >= PS_DYING)
            continue;
        if (s_demo_all)
            p->mana = p->mana_max;
        if (p->stat_points > 0)
            demo_spend_points(i);
        if (p->skill_points > 0)
            demo_learn(i);
        demo_buttons(i);
        if ((s_ticks + (uint32_t)i) % TICK_RATE == 0)
            demo_equip(i);
        if (p->level && p->level->town && p->dialog_npc < 0)
            demo_shop(i);
        if (p->hp * 100 / (p->hp_max > 0 ? p->hp_max : 1) < br->boss_hp_min)
            br->boss_hp_min = p->hp * 100 / (p->hp_max > 0 ? p->hp_max : 1);
    }
}

/* Al empezar un paso: los jefes se miden */
static void demo_step_begin(const AtStep *st)
{
    for (int i = 0; i < s_num_players; i++) {
        s_brain[i].boss_start = s_ticks;
        s_brain[i].boss_hp_min = 100;
        s_brain[i].boss_potions = 0;
    }
    (void)st;
}

static void demo_step_end(const AtStep *st, int ok)
{
    if (st->kind != AT_KILL && st->kind != AT_CLEAR)
        return;
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        const DemoBrain *br = &s_brain[i];
        printf("  demo J%d %s %s: nivel %d, vida %d, la vida bajó hasta %d%%, %d pociones, %.1f s\n", i + 1,
               ok > 0 ? "terminó" : "no pudo con", st->arg, p->level_num, p->hp_max, br->boss_hp_min, br->boss_potions,
               (s_ticks - br->boss_start) / (double)TICK_RATE);
    }
}

/* El jugador 1 en un paso que no es de waypoints: lo que decide por su
   cuenta antes del paso. 1 = hizo algo (el paso espera). */
static int demo_lead(const AtStep *st, PlayerInput *in)
{
    Player *p = &s_pl[0];
    DemoBrain *br = &s_brain[0];
    /* (con un diálogo abierto sin querer, at_lead_step lo cierra) */
    if (p->state >= PS_DYING || p->dialog_npc >= 0 || p->inv_open || p->reading || s_at.phase > 0)
        return 0;
    /* Farmeaba para un paso que ya pasó (llegó caminando mientras farmeaba) */
    if (br->farm && (st->kind != AT_GOTO_LEVEL || br->farm_need < 0 ||
                     strcmp(st->arg, k_demo_needs[br->farm_need].level) != 0))
        br->farm = NULL;
    if (demo_retreat(0, in))
        return 1;
    /* El panel de waypoints abierto sin estar viajando (se abrió solo al
       pasar por uno, o terminó de farmear a mitad de un viaje): cerrarlo */
    if (p->wp_open && !br->farm && !br->routing) {
        if (s_at.bot[0].pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return 1;
    }
    /* Antes de ir por un jefe: ¿está listo? */
    if (st->kind == AT_GOTO_LEVEL) {
        for (int n = 0; n < DEMO_NEEDS; n++)
            if (strcmp(st->arg, k_demo_needs[n].level) == 0) {
                const Level *goal = at_level(st->arg);
                if (p->level != goal && demo_farm(in, n, goal))
                    return 1;
            }
    }
    /* Lejos del destino del paso (volvió de farmear en otra punta del acto):
       por el waypoint que más lo acerque, no caminando todo */
    if (st->kind == AT_GOTO_LEVEL) {
        const Level *goal = at_level(st->arg);
        int z = zone_at(p->x, p->y), wp;
        if (goal && p->level != goal && (p->wp_open || demo_hops(z, goal->zone) > 3) &&
            (wp = demo_best_wp(p, goal->zone)) >= 0) {
            if (!br->routing)
                printf("  demo J1 lejos de %s: va por el waypoint %d\n", goal->name, wp);
            br->routing = 1;
            AtStep wst = { AT_WAYPOINT, NULL, wp, NULL, NULL, 0 };
            at_player_step(0, &wst, in);
            return 1;
        }
    }
    br->routing = 0;
    if (p->level && p->level->town)
        return 0;
    if (st->kind == AT_TALK) /* Tyrael: a hablarle, peleando solo con lo que tenga encima */
        return demo_engage(0, in, MELEE_RANGE * 2.0f);
    return demo_engage(0, in, DEMO_ENGAGE) || demo_search(0, in) || demo_loot(0, in, DEMO_LOOT_RANGE);
}

/* El jugador 2 (y los demás): sigue al primero, pelea a su lado y levanta
   lo que le sirva. Si el primero está lejos va por waypoint. */
static void demo_follow(int i, PlayerInput *in)
{
    Player *p = &s_pl[i], *lead = &s_pl[0];
    if (p->reading) { /* leyó algo sin querer (el diario, peleando al lado): cerrarlo */
        if (p->talk_ticks > 20 && s_at.bot[i].pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return;
    }
    if (at_close_dialog(p, &s_at.bot[i], in)) /* le habló sin querer a un NPC */
        return;
    if (p->state >= PS_DYING || p->inv_open)
        return;
    if (demo_retreat(i, in))
        return;
    int z = zone_at(p->x, p->y), lz = zone_at(lead->x, lead->y);
    if (p->wp_open && (z == lz || lead->wp_open)) {
        if (s_at.bot[i].pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return;
    }
    if (z != lz) {
        int wp = -1, near_hops;
        for (int k = 0; k < WP_MAX; k++)
            if (((p->waypoints >> k) & 1) && world_waypoint_zone(k) == lz)
                wp = k;
        int near = demo_nearest_wp(p, &near_hops), walk = demo_hops(z, lz);
        if (wp >= 0 && (p->wp_open || (near >= 0 && (walk < 0 || walk > near_hops + 1)))) {
            AtStep st = { AT_WAYPOINT, NULL, wp, NULL, NULL, 0 };
            at_player_step(i, &st, in);
            return;
        }
    }
    if (p->wp_open) {
        if (s_at.bot[i].pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return;
    }
    float dx = lead->x - p->x, dy = lead->y - p->y, d2 = dx * dx + dy * dy;
    if (z == lz && d2 < 10.0f * 10.0f) {
        /* Con todas las habilidades pelea también de lejos (flechas, jabalinas) */
        if (demo_engage(i, in, s_demo_all ? DEMO_ENGAGE : 5.0f) || demo_search(i, in))
            return;
        if (d2 < 8.0f * 8.0f && demo_loot(i, in, 6.0f))
            return;
    }
    if (z == lz && d2 < 3.0f * 3.0f)
        return;
    if (at_fight(p, in, &s_at.bot[i], 1.2f))
        return;
    at_goto(&s_at.bot[i], p, in, lz, lead->x, lead->y, 3.0f);
}

static void demo_start(void)
{
    memset(s_brain, 0, sizeof s_brain);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        s_brain[i].target = s_brain[i].item = s_brain[i].hunt = -1;
        s_brain[i].farm_need = -1;
        s_brain[i].boss_hp_min = 100;
        s_brain[i].saves_seen = s_god_saves[i];
    }
}

static void demo_report(void)
{
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        const DemoBrain *br = &s_brain[i];
        printf("demo J%d (%s): nivel %d, vida %d, fuerza %d, destreza %d, oro %d; farmeó %.1f minutos, "
               "%d pociones, %d objetos puestos, compró %d, vendió %d, volvió al pueblo %d veces, casi murió %d veces\n",
               i + 1, game_class_name(p->cls), p->level_num, p->hp_max, player_str(p), player_dex(p), p->gold,
               br->farm_ticks / (60.0 * TICK_RATE), br->potions, br->equipped, br->bought, br->sold, br->retreats,
               br->near_death);
        printf("demo J%d habilidades:", i + 1);
        for (int s = 0; s < skills_count(p->cls); s++)
            if (p->skill_lvl[s] > 0)
                printf(" %s %d (%d usos)", skills_get(p->cls, s)->name, p->skill_lvl[s], br->uses[s]);
        printf("\n");
    }
}

static int demo_farming(void)
{
    return s_brain[0].farm != NULL || s_brain[0].retreat != 0;
}
