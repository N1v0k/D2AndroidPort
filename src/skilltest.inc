/* ---- Prueba de habilidades contra Andariel (se incluye desde game.c) ----

   Los dos bots aparecen al lado de Andariel con nivel 99 y todas las
   habilidades de su clase al máximo (20), y la matan una vez con cada
   habilidad activa: en cada ronda el Bárbaro y la Amazona usan la suya de
   esa ronda, y Andariel vuelve a aparecer. Tiene el triple de vida para que
   no muera tan rápido. Las que no hacen daño (gritos, invocaciones, Señuelo)
   se usan cada tanto y se pelea con el ataque común. Es `--autotest skills`
   y la demostración del menú con D2_DEMO_MODE=habilidades. */

#define ST_ROUND_TICKS (120 * TICK_RATE) /* si no la mata en dos minutos, se pasa a la siguiente */
#define ST_WAIT_TICKS  (5 * TICK_RATE)   /* entre rondas: se ve la muerte de Andariel */
#define ST_HP_PCT      300               /* el triple de vida */

typedef struct {
    int      active, demo;
    int      round, rounds, wait, ticks, done;
    int      andy, alevel, zone;
    float    ax, ay;
    int      list[MAX_PLAYERS][MAX_CLASS_SKILLS], count[MAX_PLAYERS];
    int      uses[MAX_PLAYERS], pulse[MAX_PLAYERS], recast[MAX_PLAYERS];
    int      killed, timeouts, debug, solo, tick0;
    int      pet_lost;  /* veces que una invocación viva no salía en la barra de vida */
    double   seconds_sum;
} SkillTest;
static SkillTest s_st;

/* Cada invocación viva tiene que salir en la barra de vida (si su monstruo
   no coincide con el "summon" de ninguna habilidad, quedaría afuera) */
static void st_check_pet_bar(void)
{
    for (int i = 0; i < s_num_players; i++) {
        PetGroup g[PET_GROUPS];
        int lost;
        pet_groups(&s_pl[i], g, &lost);
        if (lost && !s_st.pet_lost++)
            printf("  MAL: J%d tiene %d invocaciones que no salen en la barra de vida\n", i + 1, lost);
    }
}

static int skilltest_on(void)
{
    return s_st.active;
}

/* Las que no son ataques: se usan cada tanto y se pelea con el ataque común
   (el Salto tampoco hace daño en el original: solo empuja al caer) */
static int st_support(int kind)
{
    switch (kind) {
    case SK_SHOUT: case SK_BATTLE_ORDERS: case SK_BATTLE_COMMAND: case SK_TAUNT: case SK_BATTLE_CRY:
    case SK_HOWL: case SK_FIND_POTION: case SK_FIND_ITEM: case SK_GRIM_WARD: case SK_INNER_SIGHT:
    case SK_SLOW_MISSILES: case SK_DECOY: case SK_VALKYRIE: case SK_LEAP:
    case SK_TELEPORT: case SK_ENERGY_SHIELD: case SK_THUNDER_STORM: case SK_ENCHANT: case SK_FROZEN_ARMOR:
    case SK_SHIVER_ARMOR: case SK_CHILLING_ARMOR: case SK_BLAZE:
    case SK_HOLY_SHIELD: case SK_CONVERSION: /* (a Andariel no se la convierte) */
    case SK_BONE_ARMOR: case SK_BONE_WALL: case SK_BONE_PRISON:
    case SK_QUICKNESS: case SK_FADE: case SK_VENOM: case SK_CLOAK_SHADOWS: case SK_BLADE_SHIELD:
    case SK_SHADOW_WARRIOR: case SK_SHADOW_MASTER:
    /* El Druida: las formas (pelea transformado), la Armadura de ciclón, los
       espíritus y las lianas (Andariel resiste el 80% del veneno: la
       venenosa sola casi no la lastima) */
    case SK_WEREWOLF: case SK_WEREBEAR: case SK_CYCLONE_ARMOR: case SK_OAK_SAGE: case SK_HEART_WOLVERINE:
    case SK_SPIRIT_BARBS: case SK_CARRION_VINE: case SK_SOLAR_CREEPER: case SK_POISON_CREEPER:
    /* El Brujo: los maleficios, el Resguardo, lo de los demonios, Vincular
       (Andariel no se deja) y los sigilos que no dañan */
    case SK_HEX_BANE: case SK_HEX_SIPHON: case SK_HEX_PURGE: case SK_PSYCHIC_WARD: case SK_ELDRITCH_BLAST:
    case SK_DEATH_MARK: case SK_ENGORGE: case SK_BLOOD_BOIL: case SK_CONSUME: case SK_BIND_DEMON:
    case SK_SIGIL_LETHARGY: case SK_SIGIL_RANCOR: case SK_SIGIL_DEATH:
        return 1;
    default:
        return (kind >= SK_MIGHT && kind <= SK_SALVATION) || /* las auras: se elige y se pelea */
               (kind >= SK_AMPLIFY_DAMAGE && kind <= SK_LOWER_RESIST); /* las maldiciones: con Lanza de hueso */
    }
}

/* Las invocaciones del Nigromante: las pone (y repone las que mueren) y
   ellas pelean; él mira */
static int st_summon(int kind)
{
    return kind == SK_RAISE_SKELETON || kind == SK_RAISE_MAGE || kind == SK_REVIVE || kind == SK_CLAY_GOLEM ||
           kind == SK_BLOOD_GOLEM || kind == SK_IRON_GOLEM || kind == SK_FIRE_GOLEM ||
           /* (las del Druida que pelean y los demonios del Brujo) */
           kind == SK_RAVEN || kind == SK_SPIRIT_WOLF || kind == SK_DIRE_WOLF || kind == SK_GRIZZLY ||
           kind == SK_SUMMON_GOATMAN || kind == SK_SUMMON_TAINTED || kind == SK_SUMMON_DEFILER;
}

/* El tipo de esbirro de una invocación (para contar los que tiene) */
static int st_pet_kind(int kind)
{
    if (dru_kind(kind))
        return dru_pet_kind(kind);
    if (war_kind(kind))
        return kind == SK_SUMMON_GOATMAN ? PK_GOATMAN : kind == SK_SUMMON_TAINTED ? PK_TAINTED : PK_DEFILER;
    return kind == SK_RAISE_SKELETON ? PK_SKELETON : kind == SK_RAISE_MAGE ? PK_MAGE : kind == SK_REVIVE ? PK_REVIVE
                                                                                                          : PK_GOLEM;
}

/* Las trampas de la Asesina: las pone (hasta 5, y las repone) y ellas pelean */
static int st_trap(int kind)
{
    return kind == SK_CB_SENTRY || kind == SK_WAKE_OF_FIRE || kind == SK_LIGHTNING_SENTRY || kind == SK_INFERNO_SENTRY ||
           kind == SK_DEATH_SENTRY || kind == SK_BLADE_SENTINEL;
}

/* Las que necesitan cadáveres */
static int st_corpses(int kind)
{
    return kind == SK_RAISE_SKELETON || kind == SK_RAISE_MAGE || kind == SK_REVIVE || kind == SK_CORPSE_EXPLOSION ||
           kind == SK_POISON_EXPLOSION;
}

/* A qué distancia de Andariel se pone para usarla */
static float st_distance(int kind)
{
    switch (kind) {
    case SK_MAGIC_ARROW: case SK_FIRE_ARROW: case SK_COLD_ARROW: case SK_MULTIPLE_SHOT: case SK_EXPLODING_ARROW:
    case SK_ICE_ARROW: case SK_GUIDED_ARROW: case SK_STRAFE: case SK_IMMOLATION_ARROW: case SK_FREEZING_ARROW:
    case SK_POISON_JAVELIN: case SK_LIGHTNING_BOLT: case SK_PLAGUE_JAVELIN: case SK_LIGHTNING_FURY:
    case SK_DOUBLE_THROW:
        return 7.0f;
    case SK_LEAP_ATTACK:
        return 7.0f;
    case SK_WHIRLWIND:
        return 4.5f;
    /* La Hechicera: de lejos los proyectiles y lo que cae en un lugar; las
       novas, el Campo estático y el Infierno, más cerca */
    case SK_FIRE_BOLT: case SK_ICE_BOLT: case SK_CHARGED_BOLT: case SK_FIRE_BALL: case SK_ICE_BLAST:
    case SK_GLACIAL_SPIKE: case SK_LIGHTNING: case SK_CHAIN_LIGHTNING: case SK_FROZEN_ORB: case SK_TELEKINESIS:
    case SK_FIRE_WALL: case SK_METEOR: case SK_BLIZZARD: case SK_HYDRA:
        return 7.0f;
    case SK_FROST_NOVA: case SK_NOVA: case SK_STATIC_FIELD:
        return 3.0f;
    case SK_INFERNO:
        return 4.0f;
    /* El Paladín: Rayo sagrado, Puño de los cielos y Carga, de lejos */
    case SK_HOLY_BOLT: case SK_FIST_HEAVENS: case SK_CHARGE:
        return 7.0f;
    /* El Nigromante: los huesos de lejos; la Nova venenosa, cerca */
    case SK_TEETH: case SK_BONE_SPEAR: case SK_BONE_SPIRIT: case SK_CORPSE_EXPLOSION: case SK_POISON_EXPLOSION:
        return 6.0f;
    case SK_POISON_NOVA:
        return 3.0f;
    /* El Druida: lo que tira, de lejos; Armagedón y Huracán, al lado (son alrededor de él) */
    case SK_FIRESTORM: case SK_MOLTEN_BOULDER: case SK_FISSURE: case SK_VOLCANO: case SK_TWISTER: case SK_TORNADO:
        return 6.0f;
    case SK_ARCTIC_BLAST:
        return 4.0f;
    case SK_ARMAGEDDON: case SK_HURRICANE:
        return 2.5f;
    /* El Brujo: lo que tira, de lejos; el Anillo de fuego y la Hecatombe, cerca */
    case SK_MIASMA_BOLT: case SK_MIASMA_CHAINS: case SK_FLAME_WAVE: case SK_ABYSS: case SK_BLADE_WARP:
        return 6.0f;
    case SK_ECHOING_STRIKE: case SK_MIRRORED_BLADES:
        return 5.0f;
    case SK_RING_OF_FIRE:
        return 2.5f;
    case SK_APOCALYPSE:
        return 4.0f;
    case SK_SUMMON_GOATMAN: case SK_SUMMON_TAINTED: case SK_SUMMON_DEFILER:
        return 5.0f;
    case SK_RAVEN: case SK_SPIRIT_WOLF: case SK_DIRE_WOLF: case SK_GRIZZLY:
        return 5.0f;
    /* La Asesina: lo que tira, de lejos */
    case SK_FIRE_TRAUMA: case SK_SHOCK_FIELD: case SK_PSYCHIC_HAMMER: case SK_BLADE_FURY: case SK_MIND_BLAST:
    case SK_DRAGON_FLIGHT:
        return 6.0f;
    case SK_CB_SENTRY: case SK_WAKE_OF_FIRE: case SK_LIGHTNING_SENTRY: case SK_INFERNO_SENTRY: case SK_DEATH_SENTRY:
    case SK_BLADE_SENTINEL:
        return 5.0f;
    /* (las invocaciones: un poco atrás, que peleen ellas) */
    case SK_RAISE_SKELETON: case SK_RAISE_MAGE: case SK_REVIVE: case SK_CLAY_GOLEM: case SK_BLOOD_GOLEM:
    case SK_IRON_GOLEM: case SK_FIRE_GOLEM:
        return 5.0f;
    default:
        return 0.0f; /* cuerpo a cuerpo */
    }
}

/* Lo que lleva puesto para cada habilidad: el Bárbaro con dos hachas de
   guerra (o dos hachas arrojadizas para Doble lanzamiento); la Amazona con
   glaives (jabalinas, que sirven también para las de lanza) o con arco */
static void st_weapons(const Player *p, int kind, const char **main, const char **off)
{
    if (p->cls == 2) { /* la Hechicera: un bastón largo (los hechizos no piden arma) */
        *main = "lst";
        *off = NULL;
        return;
    }
    if (p->cls == 3) { /* el Paladín: espada de guerra y escudo de cometa (Castigar y Escudo sagrado lo piden) */
        *main = "wsd";
        *off = "kit";
        return;
    }
    if (p->cls == 4) { /* el Nigromante: varita de hueso (Daga venenosa: daga) y escudo de cometa */
        *main = kind == SK_POISON_DAGGER ? "dgr" : "bwn";
        *off = "kit";
        return;
    }
    if (p->cls == 5) { /* la Asesina: dos garras (Garra del dragón y el Bloqueo las piden): salvajes o katares de tijera */
        *main = *off = p->level_num >= 70 ? "7lw" : "skr";
        return;
    }
    if (p->cls == 6) { /* el Druida: espada de guerra (antes, maza) y escudo de cometa (transformado pega con el arma) */
        *main = p->level_num >= 70 ? "wsd" : "mac";
        *off = "kit";
        return;
    }
    if (p->cls == 7) { /* el Brujo: espada de guerra y un grimorio (los suyos) */
        *main = p->level_num >= 70 ? "wsd" : "lsd";
        *off = p->level_num >= 70 ? "wa5" : "wa1";
        return;
    }
    if (p->cls == 0) {
        *main = *off = kind == SK_DOUBLE_THROW ? "bal" : "wax";
    } else {
        /* En las de apoyo pelea con el arco (se queda lejos, como en las de flechas) */
        int bow = (st_distance(kind) > 0.0f || st_support(kind)) && kind != SK_POISON_JAVELIN &&
                  kind != SK_LIGHTNING_BOLT && kind != SK_PLAGUE_JAVELIN && kind != SK_LIGHTNING_FURY;
        *main = bow ? "lbw" : "glv";
        *off = bow ? "aqv" : NULL;
    }
}

static void st_equip(Player *p, int kind)
{
    memset(&p->equip[SLOT_WEAPON], 0, sizeof(Item));
    memset(&p->equip[SLOT_SHIELD], 0, sizeof(Item));
    const char *main, *off;
    st_weapons(p, kind, &main, &off);
    const ItemType *t = items_find(main);
    if (t) {
        p->equip[SLOT_WEAPON] = items_make(t);
        if (p->equip[SLOT_WEAPON].amount > 0)
            p->equip[SLOT_WEAPON].amount = 500;
    }
    t = off ? items_find(off) : NULL;
    if (t) {
        p->equip[SLOT_SHIELD] = items_make(t);
        if (p->equip[SLOT_SHIELD].amount > 0)
            p->equip[SLOT_SHIELD].amount = 500;
    }
    if (p->cls == 5 && !p->equip[SLOT_BOOTS].type && items_find("xhb")) /* las patadas: botas de guerra */
        p->equip[SLOT_BOOTS] = items_make(items_find("xhb"));
    refresh_look(p);
    refresh_stats(p);
}

static const SkillDef *st_skill(int i)
{
    const Player *p = &s_pl[i];
    if (s_st.count[i] <= 0)
        return NULL;
    return skills_get(p->cls, s_st.list[i][s_st.round % s_st.count[i]]);
}

/* D2_ST_MONSTER=id: en vez de Andariel, ese monstruo y dos más de compañía
   (para probar la Conversión: los otros atacan al convertido) */
static const char *st_monster(void)
{
    const char *m = getenv("D2_ST_MONSTER");
    return m && m[0] ? m : "andariel";
}

/* Los cadáveres de las del Nigromante: muertos entre él y Andariel (cuando
   se gastan, otros). Revivir pide uno que se pueda revivir: Cazadoras oscuras */
static void st_corpse_supply(void)
{
    int need = 0;
    for (int i = 0; i < s_num_players; i++) {
        const SkillDef *s = st_skill(i);
        need |= s && st_corpses(s->kind) && !(s_st.solo && s_st.solo != i + 1);
    }
    if (!need || s_st.ticks % TICK_RATE)
        return;
    int ids[16], n = units_corpses_near(s_st.ax, s_st.ay, 9.0f, ids, 16), dead = 0;
    for (int k = 0; k < n; k++)
        dead += units_revivable(ids[k]);
    if (dead >= 3)
        return;
    units_zone_begin(s_st.zone);
    for (int k = 0; k < 3; k++) {
        float a = (float)(s_ticks % 628) / 100.0f + (float)k * 2.1f;
        float x = s_st.ax + 3.0f + 2.0f * cosf(a), y = s_st.ay + 1.0f + 2.0f * sinf(a);
        find_free_spot(&x, &y);
        int u = units_spawn_monster("corruptrogue1", x, y, s_st.alevel);
        if (u >= 0)
            units_damage(u, 1 << 20);
    }
    units_zone_end();
}

static void st_spawn_andariel(void)
{
    units_zone_begin(s_st.zone);
    s_st.andy = units_spawn_monster(st_monster(), s_st.ax, s_st.ay, s_st.alevel);
    if (getenv("D2_ST_MONSTER"))
        for (int k = 0; k < 2; k++)
            units_spawn_monster(st_monster(), s_st.ax - 2.0f - 1.5f * (float)k, s_st.ay - 1.0f, s_st.alevel);
    units_zone_end();
    if (s_st.andy >= 0)
        units_scale_hp(s_st.andy, ST_HP_PCT);
}

/* Los dos al lado de Andariel (después de un Salto pueden haber quedado lejos) */
static void st_place(Player *p, int i)
{
    p->x = s_st.ax + 4.0f + 2.0f * (float)i;
    p->y = s_st.ay + 1.0f;
    if (s_st.solo && s_st.solo != i + 1) /* el que mira, más lejos */
        p->x += 8.0f;
    find_free_spot(&p->x, &p->y);
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->level = world_level_at(p->x, p->y);
}

static void st_start_round(void)
{
    s_st.ticks = 0;
    s_st.wait = 0;
    items_ground_clear(); /* el botín de la anterior: si no, el ataque común lo levantaba */
    printf("prueba de habilidades: ronda %d de %d\n", s_st.round + 1, s_st.rounds);
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        const SkillDef *s = st_skill(i);
        if (!s)
            continue;
        int idx = s_st.list[i][s_st.round % s_st.count[i]];
        st_equip(p, s->kind);
        sorc_clear(p); /* lo de la ronda anterior (la Tormenta, las Hidras) no sigue */
        pal_clear(p);  /* ni el aura */
        nec_clear(p);  /* ni los esbirros */
        ass_clear(p);  /* ni las trampas ni las cargas */
        dru_clear(p);  /* ni la forma del Druida */
        war_clear(p);  /* ni los demonios ni los maleficios del Brujo */
        if (s->kind == SK_SMITE) /* Castigar se usa con el Escudo sagrado (su daño) */
            pal_holy_shield_now(p);
        p->buttons[1] = idx;
        p->buttons[2] = p->buttons[3] = BTN_EMPTY;
        if (p->cls == 4 && st_support(s->kind)) /* el Nigromante pelea con Lanza de hueso */
            p->buttons[2] = skills_find(p->cls, "Bone Spear");
        if (p->cls == 5 && (st_support(s->kind) || prg_index(s->kind) >= 0)) /* la Asesina: Garra del dragón */
            p->buttons[2] = skills_find(p->cls, "Dragon Claw");
        if (s->kind == SK_IRON_GOLEM) /* el Gólem de hierro sale de un arma del piso */
            for (int k = 0; k < 4; k++) {
                const ItemType *t = items_find("lsd");
                if (t) {
                    Item it = items_make(t);
                    items_ground_add(&it, p->x + 1.0f, p->y + 1.0f);
                }
            }
        s_st.uses[i] = 0;
        s_st.recast[i] = 0;
        set_player_state(p, PS_NORMAL, MODE_NU);
        st_place(p, i);
        printf("  J%d (%s) usa %s (nivel %d)%s\n", i + 1, game_class_name(p->cls), s->name, eff_lvl(p, idx),
               st_support(s->kind) ? ", y pelea con el ataque común" : "");
    }
    st_corpse_supply(); /* los primeros cadáveres (sus gráficos se cargan antes de que empiece) */
}

static int skilltest_start(void)
{
    memset(&s_st, 0, sizeof s_st);
    const Level *lv = world_level_named("Catacombs Level 4");
    if (!lv || zone_load(lv->zone) != 0)
        return -1;
    int id;
    if (units_find_all("andariel", &id, 1) != 1)
        return -1;
    s_st.zone = lv->zone;
    s_st.andy = id;
    s_st.alevel = units_level(id);
    units_position(id, 1.0f, &s_st.ax, &s_st.ay);
    units_scale_hp(id, ST_HP_PCT);
    if (getenv("D2_ST_MONSTER")) { /* otro monstruo en su lugar */
        units_remove(id);
        st_spawn_andariel();
    }
    /* Nivel 99, atributos de un personaje armado para pelear y todas las
       habilidades al máximo */
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        p->level_num = 99;
        if (p->cls == 0) {
            p->stat_add[ATTR_STR] = 220;
            p->stat_add[ATTR_DEX] = 100;
            p->stat_add[ATTR_VIT] = 200;
        } else if (p->cls == 2) {
            p->stat_add[ATTR_STR] = 100;
            p->stat_add[ATTR_DEX] = 60;
            p->stat_add[ATTR_VIT] = 160;
        } else if (p->cls == 3) {
            p->stat_add[ATTR_STR] = 160;
            p->stat_add[ATTR_DEX] = 120;
            p->stat_add[ATTR_VIT] = 200;
        } else if (p->cls == 5) {
            p->stat_add[ATTR_STR] = 150;
            p->stat_add[ATTR_DEX] = 150;
            p->stat_add[ATTR_VIT] = 180;
        } else if (p->cls == 4) {
            p->stat_add[ATTR_STR] = 100;
            p->stat_add[ATTR_DEX] = 80;
            p->stat_add[ATTR_VIT] = 180;
        } else if (p->cls == 6) {
            p->stat_add[ATTR_STR] = 150;
            p->stat_add[ATTR_DEX] = 100;
            p->stat_add[ATTR_VIT] = 200;
        } else if (p->cls == 7) {
            p->stat_add[ATTR_STR] = 120;
            p->stat_add[ATTR_DEX] = 100;
            p->stat_add[ATTR_VIT] = 200;
        } else {
            p->stat_add[ATTR_STR] = 120;
            p->stat_add[ATTR_DEX] = 240;
            p->stat_add[ATTR_VIT] = 160;
        }
        p->stat_add[ATTR_ENE] = 30;
        s_st.count[i] = 0;
        for (int k = 0; k < skills_count(p->cls); k++) {
            const SkillDef *s = skills_get(p->cls, k);
            if (s->kind == SK_NONE)
                continue;
            p->skill_lvl[k] = SKILL_MAX_LEVEL;
            /* (la Llamarada no es pasiva: su passivestat es lo que da su estado) */
            if (!s->passive && (!skill_has_passive(s) || s->aura))
                s_st.list[i][s_st.count[i]++] = k;
        }
        refresh_stats(p);
        p->hp = p->hp_max;
        p->mana = p->mana_max;
        st_place(p, i);
        for (int k = 0; s_debug && k < skills_count(p->cls); k++) { /* D2_DEBUG: lo que dice el árbol de cada una */
            const SkillDef *s = skills_get(p->cls, k);
            SkillCtx c = pctx(p);
            char a[160], b[160];
            if (s->kind != SK_NONE)
                printf("  J%d %s: 1) %s | 20) %s\n", i + 1, s->name, skill_effect_text(s, 1, &c, a, sizeof a),
                       skill_effect_text(s, 20, &c, b, sizeof b));
        }
    }
    s_st.rounds = s_st.count[0] > s_st.count[1] ? s_st.count[0] : s_st.count[1];
    /* Para depurar: D2_ST_ROUND=n empieza en esa ronda y D2_ST_ROUNDS=k hace solo k */
    const char *env = getenv("D2_ST_ROUND");
    if (env && atoi(env) > 1 && atoi(env) <= s_st.rounds)
        s_st.round = atoi(env) - 1;
    env = getenv("D2_ST_ROUNDS");
    if (env && atoi(env) > 0 && s_st.round + atoi(env) < s_st.rounds)
        s_st.rounds = s_st.round + atoi(env);
    s_st.debug = getenv("D2_ST_DEBUG") != NULL;
    s_st.tick0 = s_ticks;
    /* D2_ST_SOLO=1|2: pelea uno solo (el otro mira), para medir cada habilidad sola */
    s_st.solo = getenv("D2_ST_SOLO") ? atoi(getenv("D2_ST_SOLO")) : 0;
    s_st.active = 1;
    printf("prueba de habilidades contra Andariel (vida x3): %d rondas\n", s_st.rounds);
    st_start_round();
    return 0;
}

/* El bot de cada jugador: se pone a la distancia de la habilidad y la usa */
static void st_bot(int i, PlayerInput *in)
{
    Player *p = &s_pl[i];
    const SkillDef *s = st_skill(i);
    memset(in, 0, sizeof *in);
    p->hp = p->hp_max; /* no se muere ni se queda sin maná: se prueban las habilidades */
    p->mana = p->mana_max;
    if (!s || !units_alive_enemy(s_st.andy) || p->state >= PS_DYING || (s_st.solo && s_st.solo != i + 1))
        return;
    float ax, ay;
    units_position(s_st.andy, 1.0f, &ax, &ay);
    float dx = ax - p->x, dy = ay - p->y, d = sqrtf(dx * dx + dy * dy);
    int support = st_support(s->kind), summon = st_summon(s->kind), trap = st_trap(s->kind);
    float want = trap ? 5.0f : support ? (p->cls == 1 ? 7.0f : p->cls == 4 ? 6.0f : 0.0f) /* la Amazona, con el arco */
               : summon  ? 5.0f
                         : st_distance(s->kind);
    /* El alcance del golpe: como units_find_enemy, desde el borde de su cuerpo */
    float reach = MELEE_RANGE + units_radius(s_st.andy) - 0.45f;
    float slack = 0.6f;
    if (want < reach) {
        want = reach - 0.6f;
        slack = 0.4f;
    }
    /* Acercarse (o alejarse un poco si dispara) */
    if (d > want + slack) {
        at_goto(&s_at.bot[i], p, in, zone_at(ax, ay), ax, ay, want);
        return;
    }
    if (want > 3.0f && d < want - 2.5f) {
        float sign = -1.0f;
        float sx = (dx - dy) * sign, sy = (dx + dy) * 0.5f * sign, l = sqrtf(sx * sx + sy * sy);
        in->move_x = l > 0.0f ? sx / l : 0.0f;
        in->move_y = l > 0.0f ? sy / l : 0.0f;
        in->sprint = 1;
        return;
    }
    /* Mirarla (el Salto y el Torbellino van hacia donde mira) */
    float l = sqrtf((dx - dy) * (dx - dy) + (dx + dy) * (dx + dy) * 0.25f);
    if (l > 0.01f) {
        p->face_x = (dx - dy) / l;
        p->face_y = (dx + dy) * 0.5f / l;
        p->sector = sector_from_screen(p->face_x, p->face_y);
    }
    if (s_st.debug && s_st.ticks % (2 * TICK_RATE) == 0) {
        int hp, max;
        units_health(s_st.andy, &hp, &max);
        printf("    [%d s] J%d (%.1f, %.1f) d=%.1f want=%.1f state=%d mode=%d skill=%d target=%d andy=%d/%d en "
               "(%.1f, %.1f) visible %d cargando %d %s\n",
               s_st.ticks / TICK_RATE, i + 1, p->x, p->y, d, want, p->state, p->mode, p->attack_skill, p->target,
               hp, max, ax, ay, units_visible(s_st.andy), units_loading(), p->message_ticks > 0 ? p->message : "");
        if (p->valk_unit >= 0) {
            float vx, vy;
            units_position(p->valk_unit, 1.0f, &vx, &vy);
            printf("      Valquiria %d en (%.1f, %.1f) viva %d visible %d\n", p->valk_unit, vx, vy,
                   units_pet_alive(p->valk_unit), units_visible(p->valk_unit));
        }
        if (p->cls == 6)
            printf("      forma %d (%d s) cargas %d/%d ciclón %d/%d\n", p->form, p->form_ticks / TICK_RATE,
                   p->feral_charges, p->maul_charges, p->cyclone, p->cyclone_max);
        for (int k = 0; k < NEC_PETS; k++) /* los esbirros del Nigromante (y del Druida) */
            if (p->pet_kind[k] != PK_NONE) {
                float vx, vy;
                int php, pmax;
                units_position(p->pets[k], 1.0f, &vx, &vy);
                units_health(p->pets[k], &php, &pmax);
                printf("      esbirro %d (%s) en (%.1f, %.1f) vida %d/%d viva %d\n", p->pets[k],
                       units_id(p->pets[k]), vx, vy, php, pmax, units_pet_alive(p->pets[k]));
            }
    }
    if (p->state != PS_NORMAL || s_st.pulse[i]++ % 2)
        return;
    if (trap) { /* las trampas: hasta 5 (el Centinela, uno), de a una por segundo */
        int full = s->kind == SK_BLADE_SENTINEL ? p->sent_unit >= 0 : trap_count(p) >= ASS_TRAPS;
        if (full || s_st.ticks < s_st.recast[i])
            return;
        s_st.recast[i] = s_st.ticks + TICK_RATE;
        in->button[1] = 1;
        s_st.uses[i]++;
        return;
    }
    if (prg_index(s->kind) >= 0 && p->prg_charges[prg_index(s->kind)] >= 3 && p->buttons[2] >= 0) {
        in->button[2] = 1; /* tres cargas: las libera con Garra del dragón */
        return;
    }
    if (summon) { /* repone los que faltan (de a uno por segundo) */
        int kind = st_pet_kind(s->kind);
        int lvl = eff_lvl(p, p->buttons[1]);
        SkillCtx c = pctx(p);
        int most = kind == PK_GOLEM ? 1 : s->petmax[0] ? skill_eval(s, s->petmax, lvl, &c) : 1;
        if (nec_pet_count(p, kind) >= most || s_st.ticks < s_st.recast[i])
            return;
        s_st.recast[i] = s_st.ticks + TICK_RATE;
        in->button[1] = 1;
        s_st.uses[i]++;
        return;
    }
    /* El Druida: la forma no se vuelve a usar (la desharía) y el espíritu o la
       enredadera, solo si no está */
    int keep = (s->kind == SK_WEREWOLF && p->form == FORM_WOLF) || (s->kind == SK_WEREBEAR && p->form == FORM_BEAR) ||
               (p->cls == 6 && support && s->summon[0] && dru_pet(p, dru_pet_kind(s->kind)) >= 0) ||
               /* el Brujo: el maleficio y el Resguardo, mientras duran */
               ((s->kind == SK_HEX_BANE || s->kind == SK_HEX_SIPHON || s->kind == SK_HEX_PURGE) && p->hex_ticks > 0 &&
                p->hex_skill == p->buttons[1]) ||
               (s->kind == SK_PSYCHIC_WARD && p->ward > 0) || (s->kind == SK_ELDRITCH_BLAST && p->eldritch_ticks > 0);
    if (keep && s_st.ticks >= s_st.recast[i])
        s_st.recast[i] = s_st.ticks + 8 * TICK_RATE;
    if (support && s_st.ticks < s_st.recast[i]) { /* entre uno y otro, el ataque común */
        if (p->buttons[2] >= 0)
            in->button[2] = 1; /* (el Nigromante, Lanza de hueso) */
        else
            in->action = in->button[0] = 1;
        return;
    }
    in->button[1] = 1;
    s_st.uses[i]++;
    if (support) /* la vuelve a usar a los 8 segundos (Vincular demonio, a los 2: cuando ya está herido) */
        s_st.recast[i] = s_st.ticks + (s->kind == SK_BIND_DEMON ? 2 : 8) * TICK_RATE;
}

static int skilltest_input(InputState *in)
{
    /* Mientras se cargan los gráficos (Andariel no se ve ni se mueve), nadie pelea */
    if (units_loading() > 0) {
        for (int i = 0; i < s_num_players; i++)
            memset(&in->player[i], 0, sizeof in->player[i]);
        return 1;
    }
    if (getenv("D2_ST_CALM") && units_alive_enemy(s_st.andy)) /* para las capturas: no ataca */
        units_stun(s_st.andy, 2);
    if (s_st.debug && s_st.ticks == 0 && s_st.wait == 0)
        printf("    la ronda empieza en el tick %d de la prueba\n", s_ticks - s_st.tick0);
    s_st.ticks++;
    st_corpse_supply();
    if (units_alive_enemy(s_st.andy) && s_st.ticks < ST_ROUND_TICKS) {
        for (int i = 0; i < s_num_players; i++)
            st_bot(i, &in->player[i]);
        if (s_st.ticks % TICK_RATE == 0)
            st_check_pet_bar();
        return 1;
    }
    for (int i = 0; i < s_num_players; i++)
        memset(&in->player[i], 0, sizeof in->player[i]);
    /* Terminó la ronda: muerta (con su muerte del original) o sin tiempo */
    if (s_st.wait == 0) {
        float secs = (float)s_st.ticks / TICK_RATE;
        if (!units_alive_enemy(s_st.andy)) {
            s_st.killed++;
            s_st.seconds_sum += secs;
            if (s_st.round > 0) /* la primera vez ya la muestra la misión */
                bossfx_start(BFX_ANDY, s_st.ax, s_st.ay);
            printf("  ronda %d: Andariel murió en %.1f s (usos: J1 %d, J2 %d)\n", s_st.round + 1, secs, s_st.uses[0],
                   s_st.uses[1]);
        } else {
            s_st.timeouts++;
            int hp, max;
            units_health(s_st.andy, &hp, &max);
            printf("  ronda %d: no la mataron en %.0f s (le quedaba %d de %d de vida)\n", s_st.round + 1, secs, hp,
                   max);
            units_damage(s_st.andy, 1 << 20);
        }
    }
    if (++s_st.wait < ST_WAIT_TICKS)
        return 1;
    if (++s_st.round >= s_st.rounds) {
        s_st.done++;
        printf("prueba de habilidades: %d rondas, %d muertes (%.1f s de promedio), %d sin terminar\n", s_st.rounds,
               s_st.killed, s_st.killed ? s_st.seconds_sum / s_st.killed : 0.0, s_st.timeouts);
        if (s_st.pet_lost)
            printf("prueba de habilidades: %d veces hubo invocaciones fuera de la barra de vida\n", s_st.pet_lost);
        if (!s_st.demo)
            return s_st.timeouts > 0 || s_st.pet_lost > 0 ? -1 : 0;
        s_st.round = 0; /* la demostración vuelve a empezar */
    }
    st_spawn_andariel();
    st_start_round();
    return 1;
}

/* Para las capturas de D2_SHOTS: los ticks desde que empezó la ronda */
int game_debug_round_tick(void)
{
    return s_st.active && s_st.ticks > 0 ? s_st.ticks : -1;
}

/* Arriba: la ronda y qué habilidad usa cada uno */
static void skilltest_draw(void)
{
    if (!s_st.active)
        return;
    char text[160];
    int y = s_demo ? 128 : 60;
    snprintf(text, sizeof text, "Habilidades contra Andariel: ronda %d de %d", s_st.round + 1, s_st.rounds);
    int w = font_width(text, 2);
    ui_dark_box(SCREEN_W / 2 - w / 2 - 8, y - 4, w + 16, font_height(2) * (s_num_players + 1) + 12);
    font_draw_centered(SCREEN_W / 2, y, text, 2, COL(R_GOLD, 15));
    for (int i = 0; i < s_num_players; i++) {
        const SkillDef *s = st_skill(i);
        snprintf(text, sizeof text, "J%d: %s", i + 1, s ? s->name : "-");
        font_draw_centered(SCREEN_W / 2, y + font_height(2) * (i + 1) + 4, text, 2, COL(R_STONE, 14));
    }
}
