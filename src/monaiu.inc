/* ---- Los Uber del evento Pandemonium (se incluye desde monai.inc) ----

   Como AiThink.cpp de D2MOO (D2_VERSION_HAS_UBERS: AITHINK_Fn144_UberIzual,
   Fn145_UberBaal, Fn146_UberMephisto y Fn147_UberDiablo, la parte "UBER
   TWEAK"): usan la IA del jefe de su acto (ai_alias) y antes hacen lo suyo:
   - Mefisto llama esqueletos, arqueros y magos esqueleto al lado del jugador
     (AITHINK_SpawnUberMephistoMinion) mientras haya menos de 15 monstruos
     cerca, o de 30 si está solo;
   - Diablo abre agujeros de demonios (demonhole: largan Señores del abismo) y
     se cubre con Diablogeddon (la lluvia de fuego del estado armageddon);
   - Baal llama espectros y vampiros y se pone la Armadura escalofriante;
   - Izual se pone la Armadura escalofriante y se teletransporta al jugador
     si no lo ve.
   "Solo" es sin los otros dos cerca (del jugador y de él). Los esbirros no dan
   experiencia ni botín (STATE_UBERMINION).

   shortcut: la Armadura escalofriante solo se ve (no devuelve los proyectiles). */

#define UBER_NEAR 30.0f /* "cerca": las salas de alrededor (sub_6FCF1E80) */

/* AIUTIL_TargetCallback_Ubers: los que hay alrededor de (x, y) */
typedef struct { int meph, diablo, baal, holes, pitlords, bminions, others; } UberCount;

static void uber_count(float x, float y, UberCount *c)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (!o->enemy || o->state >= ST_DYING || o->hidden || o->zone == ZONE_FREE)
            continue;
        float dx = o->x - x, dy = o->y - y;
        if (dx * dx + dy * dy > UBER_NEAR * UBER_NEAR)
            continue;
        if (o->uber == UB_MEPH)
            c->meph++;
        else if (o->uber == UB_DIABLO)
            c->diablo++;
        else if (o->uber == UB_BAAL)
            c->baal++;
        else if (strcmp(o->name, "demonhole") == 0)
            c->holes++;
        else if (strcmp(o->name, "megademon6") == 0)
            c->pitlords++;
        else if (strcmp(o->name, "wraith9") == 0 || strcmp(o->name, "vampire9") == 0 || strcmp(o->name, "willowisp8") == 0)
            c->bminions++;
        else
            c->others++;
    }
}

/* Un esbirro (sub_6FC68D70: a `r` del lugar, en uno libre), sin experiencia ni botín, del nivel del área */
static Unit *uber_minion(Unit *u, const char *id, float x, float y, float r)
{
    float a = (float)(rnd(u) % 628) / 100.0f, dist = r > 0.0f ? (float)(rnd(u) % (uint32_t)(r * 10.0f + 1.0f)) / 10.0f : 0.0f;
    float mx = x + cosf(a) * dist, my = y + sinf(a) * dist;
    if (s_map && wall_foot(s_map, mx, my, 1, COLL_MASK_MONSTER_PATH)) {
        mx = x;
        my = y;
    }
    int saved = s_spawn_zone;
    s_spawn_zone = u->zone;
    Unit *c = create_monster(id, mx, my, u->minion_level > 0 ? u->minion_level : u->level, 1);
    s_spawn_zone = saved;
    if (c) {
        c->exp = 0;
        c->tc[0] = '\0';
        c->searched = 1;
        c->sector = (int)(rnd(c) % 16);
        if (c->ai == AI_EVILHOLE || c->ai == AI_GENSPAWNER) /* (lo que larga el agujero: spawn de monstats.txt) */
            snprintf(c->minion, sizeof c->minion, "%s", txt_get(&s_monstats, mon_row(&s_monstats, id), txt_col(&s_monstats, "spawn")));
        if (getenv("D2_AI_DEBUG"))
            printf("IA: %s llama a %s\n", u->name, id);
    }
    return c;
}

/* Un estado de la habilidad k que dura mientras *ticks > 0 (la Armadura escalofriante, Diablogeddon):
   si no lo tiene, la usa (su animación) y empieza a contar su auralencalc */
static int uber_self_buff(Unit *u, int t, int k, int *ticks)
{
    if (*ticks > 0 || !u->skill[k][0] || u->sk_anim[k] < 0 || !u->gfx->anim[u->sk_anim[k]].ok)
        return 0;
    if (!s_skilltab.rows)
        load_table(&s_skilltab, "skills");
    int row = txt_find_row(&s_skilltab, txt_col(&s_skilltab, "skill"), u->skill[k]), lvl = ai_sklvl(u, k);
    /* ln12 (Diablogeddon: 1000 + 50 por nivel) o ln34 (la Armadura: 3600 + 300 por nivel), en cuadros */
    int armor = strcmp(u->skill[k], "Chilling Armor") == 0;
    int p1 = row >= 0 ? txt_get_int(&s_skilltab, row, armor ? "Param3" : "Param1", 1000) : 1000;
    int p2 = row >= 0 ? txt_get_int(&s_skilltab, row, armor ? "Param4" : "Param2", 50) : 50;
    *ticks = (p1 + p2 * (lvl - 1)) * TICK_RATE / 25;
    ai_attack(u, t >= 0 ? t : 0, u->sk_anim[k]);
    u->ranged_attack = 0;
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s usa %s\n", u->name, u->skill[k]);
    return 1;
}

/* Lo de cada Uber antes de su IA: 1 si ya hizo algo */
static int uber_think(Unit *u, const Map *m, UnitTarget *pl, int t)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    UberCount ct, cs;
    float cx = tp ? tp->x : u->x, cy = tp ? tp->y : u->y;
    uber_count(cx, cy, &ct);
    uber_count(u->x, u->y, &cs);
    switch (u->uber) {
    case UB_MEPH: {
        int alone = ct.baal == 0 && ct.diablo == 0 && (!tp || (cs.baal == 0 && cs.diablo == 0));
        if ((alone && ct.others < 30) || ct.others < 15) {
            static const char *const k_minions[8] = { "skeleton8",    "skeleton8",    "sk_archer11",  "sk_archer11",
                                                      "skmage_fire7", "skmage_ltng7", "skmage_cold6", "skmage_pois7" };
            uber_minion(u, k_minions[rnd(u) % 8], cx, cy, 4.0f);
        }
        return 0;
    }
    case UB_DIABLO: {
        int alone = ct.baal == 0 && ct.meph == 0 && (!tp || (cs.baal == 0 && cs.meph == 0));
        if ((alone && ct.pitlords < 10 && ct.holes < 2 && rnd(u) % 100 < 33) || (ct.pitlords < 4 && ct.holes == 0 && rnd(u) % 100 < 15))
            uber_minion(u, "demonhole", cx, cy, 4.0f);
        /* Diablogeddon (su habilidad 8), si no está echando el rayo rojo */
        return u->ai_p[0] != 5 && uber_self_buff(u, t, 7, &u->geddon_ticks);
    }
    case UB_BAAL: {
        int alone = ct.diablo == 0 && ct.meph == 0 && (!tp || (cs.diablo == 0 && cs.meph == 0));
        if ((alone && ct.bminions < 15 && rnd(u) % 100 < 45) || (ct.bminions < 7 && rnd(u) % 100 < 30))
            uber_minion(u, rnd(u) % 2 ? "vampire9" : "wraith9", u->x, u->y, 2.0f); /* (los fuegos fatuos, no: como el original) */
        return uber_self_buff(u, t, 7, &u->chill_armor);
    }
    case UB_IZUAL:
        if (uber_self_buff(u, t, 1, &u->chill_armor))
            return 1;
        if (tp && m && map_ray(m, (int)u->x, (int)u->y, (int)tp->x, (int)tp->y, COLL_MISSILE_BARRIER, NULL, NULL)) {
            float x = tp->x + 1.0f, y = tp->y; /* MonTeleport: al lado del jugador */
            if (!wall_foot(m, x, y, u->foot, COLL_MASK_MONSTER_PATH)) {
                missiles_effect("teleport", u->x, u->y);
                u->x = x;
                u->y = y;
                grid_moved(u);
                missiles_effect("teleport", u->x, u->y);
                ai_stall(u, 4);
                return 1;
            }
        }
        return 0;
    default:
        return 0;
    }
}

/* Diablogeddon mientras dura (SrvDo124: diablogeddoncontrol): cada tanto cae un meteoro a hasta
   par3 (8) subtiles de Diablo, con el fuego de la habilidad en su nivel */
static void uber_geddon_tick(Unit *u)
{
    if (u->geddon_ticks <= 0)
        return;
    u->geddon_ticks--;
    if (u->geddon_ticks % 6 != 0)
        return;
    int k = 7, lvl = ai_sklvl(u, k);
    float a = (float)(rnd(u) % 628) / 100.0f, r = (float)(rnd(u) % 80) / 10.0f;
    float x = u->x + cosf(a) * r, y = u->y + sinf(a) * r;
    missiles_effect("vampiremeteorcenter", x, y);
    Missile *mi = missiles_effect("vampiremeteor", x, y);
    if (!mi)
        return;
    mi->flags |= MF_EFFECT | MF_LAND | MF_METEOR;
    mi->life = 30;
    missiles_meteor(mi, "vampiremeteorcenter");
    mi->elem = ELEM_FIRE;
    skill_elem_damage(u->skill[k], lvl, &mi->emin, &mi->emax);
    mi->radius10 = 30;
    mi->owner = -1;
    mi->source = (int)(u - s_units);
    mi->ar = u->ar;
    mi->level = u->level;
}

/* ---- Los Ancianos colosales (Colossal Summit, de Resurrected: endgame.inc) ----
   Su IA (ColossalWhirlwind, ColossalThrow y ColossalLeap) no está en D2MOO: se arma con la
   de los Antiguos (Fn133: Talic gira, Madawc grita y tira, Korlic salta) y lo que dicen sus
   habilidades de monstats.txt y las guías del juego:
   - se encantan (Enchant, Lightning Enchant, Cold Enchant: su elemento en cada golpe);
   - llaman a sus espíritus (Nest: el spawn de monstats.txt, hasta aip6 vivos, extra rápidos);
   - su aura Pierce le quita al jugador hasta el 100 % de la resistencia a su elemento a menos
     de 20 subtiles (-min(ln34, 100));
   - al 66 % de la vida (aip7) o cuando muere otro, su habilidad 7 (Fire Twisters, Charged Bolt
     Disk, Cold Fissure); al 33 % o cuando mueren los otros dos, además la grande (Colossal
     Volcano, Colossal Thunder Storm, Blizzard).
   shortcut: UberAncientsHeal no hace nada (no se sabe qué hace). */

static int col_find(const Unit *u, const char *part)
{
    for (int k = 0; k < MON_SKILLS; k++)
        if (u->skill[k][0] && strstr(u->skill[k], part))
            return k;
    return -1;
}

/* Su elemento: Talic fuego, Madawc rayo, Korlic frío (el índice de UnitTarget.res) */
static int col_res_index(const Unit *u)
{
    return u->colossal == 1 ? 0 : u->colossal == 2 ? 2 : 1;
}

/* Algo que cae en el lugar (la fisura, el volcán, el rayo de la tormenta): daño de área del
   elemento de la habilidad */
static void col_strike(Unit *u, int k, const char *missile, float x, float y, int elem, int radius10, int life)
{
    Missile *mi = missiles_effect(missile, x, y);
    if (!mi)
        return;
    mi->flags |= MF_EFFECT | MF_LAND;
    mi->life = life;
    mi->elem = elem;
    skill_elem_damage(u->skill[k], ai_sklvl(u, k), &mi->emin, &mi->emax);
    mi->radius10 = radius10;
    mi->owner = -1;
    mi->source = (int)(u - s_units);
    mi->ar = u->ar;
    mi->level = u->level;
}

/* Uno que sale de él hacia (dx, dy), con el daño de la habilidad k */
static void col_shoot(Unit *u, int k, const char *missile, float dx, float dy, int elem)
{
    Missile *mi = missiles_spawn(missile, u->x, u->y, dx, dy);
    if (!mi)
        return;
    mi->owner = -1;
    mi->source = (int)(u - s_units);
    mi->ar = u->ar;
    mi->level = u->level;
    mi->elem = elem;
    skill_elem_damage(u->skill[k], ai_sklvl(u, k), &mi->emin, &mi->emax);
}

/* Cada tick: el aura Pierce, la Tormenta colosal y la fase */
static void colossal_tick(Unit *u, UnitTarget *pl, int n)
{
    int life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100, alive = 0;
    for (int i = 0; i < s_num_units; i++)
        alive += s_units[i].colossal && s_units[i].state < ST_DYING && !s_units[i].hidden && s_units[i].zone == u->zone;
    int phase = life < 33 ? 2 : life < (u->aip[6] > 0 ? u->aip[6] : 66) ? 1 : 0;
    phase = 3 - alive > phase ? 3 - alive : phase;
    u->col_phase = (int8_t)(phase > 2 ? 2 : phase);
    int k = col_find(u, "Pierce"), ri = col_res_index(u);
    if (k >= 0) {
        int lvl = ai_sklvl(u, k), cut = 30 + 5 * (lvl - 1);
        cut = cut > 100 ? 100 : cut;
        for (int i = 0; i < n; i++) {
            UnitTarget *t = &pl[i];
            if (t->alive && !t->in_town && dist_to(u, t->x, t->y) <= 20.0f && cut > t->aura_pierce[ri])
                t->aura_pierce[ri] = cut;
        }
        if (s_unit_ticks % 4 == 0)
            overlays_add(u->colossal == 1 ? "talicfirepierce" : u->colossal == 2 ? "madawclightpierce" : "korliccoldpierce", OV_AT_UNIT,
                         (int)(u - s_units), u->x, u->y, 4);
    }
    if (u->col_storm > 0) { /* la Tormenta colosal: un rayo cada tanto al jugador más cerca */
        u->col_storm--;
        int ks = col_find(u, "Thunder Storm");
        if (ks >= 0 && u->col_storm % 40 == 0) {
            int best = -1;
            float bd = 25.0f;
            for (int i = 0; i < n; i++)
                if (pl[i].alive && !pl[i].in_town && dist_to(u, pl[i].x, pl[i].y) < bd) {
                    bd = dist_to(u, pl[i].x, pl[i].y);
                    best = i;
                }
            if (best >= 0)
                col_strike(u, ks, "colossal thunderstorm", pl[best].x, pl[best].y, ELEM_LTNG, 20, 2);
        }
    }
}

/* Lo de cada colosal antes de la IA de los Antiguos: 1 si ya hizo algo */
static int colossal_think(Unit *u, UnitTarget *pl, int t)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    if (!tp)
        return 0;
    int k = col_find(u, "Enchant");
    if (k >= 0 && !u->col_enchant) { /* se encanta al empezar */
        u->col_enchant = (int8_t)(col_res_index(u) + 1);
        ai_attack(u, t, u->gfx->anim[ANIM_CAST].ok ? ANIM_CAST : ANIM_ATTACK);
        overlays_add(u->colossal == 1 ? "enchant" : u->colossal == 2 ? "lightningenchant" : "coldenchant", OV_AT_UNIT, (int)(u - s_units),
                     u->x, u->y, 0);
        return 1;
    }
    /* sus espíritus (hasta aip6 vivos) */
    int max = u->aip[5] > 0 ? u->aip[5] : 5, have = 0;
    for (int i = 0; i < s_num_units; i++)
        have += s_units[i].state < ST_DYING && !s_units[i].hidden && u->minion[0] && strcmp(s_units[i].name, u->minion) == 0;
    if (u->minion[0] && have < max && rnd(u) % 100 < 20) {
        Unit *c = uber_minion(u, u->minion, u->x, u->y, 3.0f);
        if (c) {
            c->mods |= 1u << MOD_FAST;
            c->speed *= 1.5f;
        }
    }
    /* las de las fases */
    float dx = tp->x - u->x, dy = tp->y - u->y, l = sqrtf(dx * dx + dy * dy);
    l = l > 0.01f ? l : 1.0f;
    if (u->col_phase >= 2 && rnd(u) % 100 < 15) {
        if (u->colossal == 1 && (k = col_find(u, "Volcano")) >= 0) {
            float vx = tp->x + (float)((int)(rnd(u) % 5) - 2), vy = tp->y + (float)((int)(rnd(u) % 5) - 2);
            col_strike(u, k, "volcano", vx, vy, ELEM_FIRE, 30, 75);
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (u->colossal == 2 && u->col_storm <= 0 && (k = col_find(u, "Thunder Storm")) >= 0) {
            u->col_storm = (3600 + 600 * (ai_sklvl(u, k) - 1)) * TICK_RATE / 25;
            ai_attack(u, t, u->gfx->anim[ANIM_CAST].ok ? ANIM_CAST : ANIM_ATTACK);
            return 1;
        }
        if (u->colossal == 3 && (k = col_find(u, "Blizzard")) >= 0 && ai_area(u, t, k, tp))
            return 1;
    }
    if (u->col_phase >= 1 && rnd(u) % 100 < 20) {
        if (u->colossal == 1 && (k = col_find(u, "Twisters")) >= 0) { /* tres remolinos de fuego hacia el jugador */
            for (int i = -1; i <= 1; i++) {
                float a = atan2f(dy, dx) + (float)i * 0.35f;
                col_shoot(u, k, "firetwister", cosf(a), sinf(a), ELEM_FIRE);
            }
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (u->colossal == 2 && (k = col_find(u, "Bolt Disk")) >= 0) { /* un anillo de rayos cargados */
            for (int i = 0; i < 8; i++) {
                float a = (float)i * PI_F / 4.0f;
                col_shoot(u, k, "colossalchargedbolt", cosf(a), sinf(a), ELEM_LTNG);
            }
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (u->colossal == 3 && (k = col_find(u, "Fissure")) >= 0) {
            col_strike(u, k, "coldfissure center", tp->x, tp->y, ELEM_COLD, 30, 50);
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
    }
    return 0;
}
