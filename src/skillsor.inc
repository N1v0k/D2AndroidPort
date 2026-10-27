/* ---- Las habilidades de la Hechicera (se incluye desde game.c) ----

   Como en el original (SkillSor.cpp y MissMode.cpp de D2MOO): casi todas
   son proyectiles de missiles.txt con el daño de skills.txt (con sus
   sinergias) más los dominios. Las bolas, descargas y púas salen hacia el
   monstruo más cercano (o hacia adelante); las novas son 64 proyectiles en
   ronda y a cada monstruo le pega uno solo; el Muro de fuego, la Llamarada y
   el Meteoro dejan fuego en el piso; la Ventisca tira trozos de hielo en una
   zona; el Orbe congelado escupe descargas mientras avanza y termina en una
   nova; la Hidra son tres cabezas quietas que escupen fuego. Las armaduras
   de hielo, el Escudo de energía, la Tormenta, Encantar y la Llamarada son
   estados que duran. */

#define HYDRA_LIFE_MIN   25 /* por las dudas: que las cabezas duren al menos un segundo */
#define SPELL_AHEAD      6.0f /* sin monstruo: a qué distancia va lo que se tira a un lugar */
#define TELEPORT_RANGE   12.0f
#define HIT_GROUPS       16

/* Las novas: qué monstruos ya recibieron un proyectil de cada una */
typedef struct {
    int id, n;
    int units[64];
} HitGroup;
static HitGroup s_hit_groups[HIT_GROUPS];
static int      s_hit_group_next = 1;

static int hit_group_new(void)
{
    int id = s_hit_group_next++;
    HitGroup *g = &s_hit_groups[id % HIT_GROUPS];
    g->id = id;
    g->n = 0;
    return id;
}

/* 1 si es el primero de ese grupo que le pega a la unidad (y lo anota) */
static int hit_group_first(int id, int unit)
{
    HitGroup *g = &s_hit_groups[id % HIT_GROUPS];
    if (g->id != id)
        return 1; /* el grupo ya se reusó: no queda registro */
    for (int k = 0; k < g->n; k++)
        if (g->units[k] == unit)
            return 0;
    if (g->n < 64)
        g->units[g->n++] = unit;
    return 1;
}

static int sorc_kind(int kind)
{
    return kind >= SK_FIRE_BOLT && kind <= SK_COLD_MASTERY;
}

/* El daño con su resistencia: el frío de la Hechicera con Dominio del frío baja la resistencia */
static int spell_resist(int owner, int unit, int elem, int amount)
{
    int pierce = 0;
    if (owner >= 0 && owner < s_num_players) { /* Dominio del frío y los "-N% a la resistencia" de los objetos */
        const Player *o = &s_pl[owner];
        pierce = elem == ELEM_COLD   ? o->sk_cold_pierce + o->bonus[STAT_COLD_PIERCE]
               : elem == ELEM_FIRE   ? o->bonus[STAT_FIRE_PIERCE]
               : elem == ELEM_LTNG   ? o->bonus[STAT_LTNG_PIERCE]
               : elem == ELEM_POIS   ? o->bonus[STAT_POIS_PIERCE]
               : elem == ELEM_MAG    ? o->bonus[STAT_MAG_PIERCE] : 0;
    }
    return pierce > 0 ? units_resist_pierce(unit, elem, amount, pierce) : units_resist(unit, elem, amount);
}

/* El daño de la habilidad con sus sinergias y el dominio de su elemento */
static int mastery_pct(const Player *p, int elem)
{
    return elem == ELEM_FIRE ? p->sk_fire_pct : elem == ELEM_LTNG ? p->sk_ltng_pct
         : elem == ELEM_COLD ? p->bonus[STAT_COLD_MASTERY] : elem == ELEM_POIS ? p->bonus[STAT_POIS_MASTERY]
         : elem == ELEM_MAG ? p->bonus[STAT_MAG_MASTERY] : 0;
}

static void spell_damage(const Player *p, const SkillDef *s, int lvl, int elem, int *lo, int *hi)
{
    SkillCtx c = pctx(p);
    skill_elem_syn(s, lvl, &c, lo, hi);
    int pct = mastery_pct(p, elem);
    *lo = *lo * (100 + pct) / 100;
    *hi = *hi * (100 + pct) / 100;
    if (*hi < *lo)
        *hi = *lo;
}

/* El fuego que quema por segundo (Muro de fuego, Llamarada, Infierno) */
static void spell_burn(const Player *p, const SkillDef *s, int lvl, int *lo, int *hi)
{
    SkillCtx c = pctx(p);
    skill_elem_rate(s, lvl, &c, TICK_RATE, lo, hi);
    *lo = *lo * (100 + p->sk_fire_pct) / 100;
    *hi = *hi * (100 + p->sk_fire_pct) / 100;
    if (*lo < 1)
        *lo = 1;
    if (*hi < *lo)
        *hi = *lo;
}

/* Un proyectil del jugador con su daño (todo elemental) */
static Missile *spell_missile(Player *p, const char *name, float x, float y, float dx, float dy, int elem, int lo,
                              int hi, int elen)
{
    Missile *mi = missiles_spawn(name, x, y, dx, dy);
    if (!mi)
        return NULL;
    mi->owner = (int)(p - s_pl);
    mi->ar = player_attack_rating(p);
    mi->level = p->level_num;
    mi->elem = elem;
    mi->emin = lo;
    mi->emax = hi;
    mi->elen = elen;
    mi->flags |= MF_ALWAYS_HIT; /* los hechizos no fallan */
    return mi;
}

/* Hacia dónde: el monstruo (si sigue vivo) o lo que se eligió al lanzarla */
static void spell_aim(const Player *p, float *ax, float *ay)
{
    float tx = p->cast_x, ty = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    *ax = tx - p->x;
    *ay = ty - p->y;
    if (*ax * *ax + *ay * *ay < 0.01f)
        screen_dir_to_world(p->face_x, p->face_y, ax, ay);
}

/* 64 proyectiles en ronda (Nova, Nova de escarcha, lo que deja el Orbe) */
static void nova_ring(Player *p, const char *name, float x, float y, int step, int elem, int lo, int hi, int elen)
{
    int group = hit_group_new();
    for (int k = 0; k < 64; k += step) {
        float a = (float)k * 2.0f * PI_F / 64.0f;
        Missile *mi = spell_missile(p, name, x, y, cosf(a), sinf(a), elem, lo, hi, elen);
        if (!mi)
            break;
        mi->flags |= MF_GROUP | MF_PIERCE;
        mi->group = group;
    }
}

/* ---- Al lanzarla ---- */

/* Antes de la animación: a quién o adónde va. 0 si no se puede (con el aviso). */
static int sorc_start(Player *p, int skill)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    SkillCtx c = pctx(p);
    p->target = units_find_enemy(p->x, p->y, AIM_RANGE);
    float ax, ay;
    screen_dir_to_world(p->face_x, p->face_y, &ax, &ay);
    float l = sqrtf(ax * ax + ay * ay);
    p->cast_x = p->x + ax / (l > 0.0f ? l : 1.0f) * SPELL_AHEAD;
    p->cast_y = p->y + ay / (l > 0.0f ? l : 1.0f) * SPELL_AHEAD;
    if (p->target >= 0) {
        units_position(p->target, 1.0f, &p->cast_x, &p->cast_y);
        face_unit(p, p->target);
    }
    switch (s->kind) {
    case SK_TELEKINESIS:
        if (p->target < 0 ||
            (p->cast_x - p->x) * (p->cast_x - p->x) + (p->cast_y - p->y) * (p->cast_y - p->y) >
                (float)(skill_calc(s, SC_AURARANGE, lvl, &c) * skill_calc(s, SC_AURARANGE, lvl, &c))) {
            show_message(p, "No hay a quién");
            return 0;
        }
        break;
    case SK_TELEPORT: /* hacia donde mira (con el monstruo no: se iría encima) */
        p->cast_x = p->x + ax / (l > 0.0f ? l : 1.0f) * TELEPORT_RANGE;
        p->cast_y = p->y + ay / (l > 0.0f ? l : 1.0f) * TELEPORT_RANGE;
        break;
    case SK_HYDRA: /* adelante, entre ella y el monstruo (no encima) */
        if (p->target >= 0) {
            float dx = p->cast_x - p->x, dy = p->cast_y - p->y, d = sqrtf(dx * dx + dy * dy);
            if (d > 3.0f) {
                p->cast_x = p->x + dx / d * 3.0f;
                p->cast_y = p->y + dy / d * 3.0f;
            }
        }
        /* fall through */
    case SK_FIRE_WALL:
    case SK_METEOR:
    case SK_BLIZZARD:
        if (p->level && p->level->town) {
            show_message(p, "En el pueblo no se pelea");
            return 0;
        }
        break;
    default:
        break;
    }
    p->attack_skill = skill;
    p->cast_target = -1;
    p->hits_done = 0;
    /* Infierno, Relámpago y Rayo en cadena: con la secuencia del original */
    if (s->seqnum > 0 && start_seq(p, skill)) {
        if (s->castoverlay[0])
            overlays_add(s->castoverlay, OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
        play_skill_sound(p, s);
        return 1;
    }
    play_skill_sound(p, s);
    if (!player_anim(p, MODE_SC)->ok) {
        cast_effect(p);
        return 1;
    }
    set_player_state(p, PS_CAST, MODE_SC);
    return 1;
}

/* Teletransporte: al lugar libre más lejano hacia donde mira (pasa por encima de lo que haya) */
static int teleport(Player *p)
{
    float dx = p->cast_x - p->x, dy = p->cast_y - p->y, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.1f)
        return 0;
    for (float d = l; d >= 1.0f; d -= 0.5f) {
        float x = p->x + dx / l * d, y = p->y + dy / l * d;
        if (player_blocked(x, y, x, y) || world_level_at(x, y) != p->level)
            continue;
        missiles_effect("teleport", p->x, p->y);
        p->x = p->prev_x = x;
        p->y = p->prev_y = y;
        overlays_add("teleport", OV_AT_PLAYER, (int)(p - s_pl), x, y, 0);
        return 1;
    }
    return 0;
}

/* Telequinesis: daño de rayo y, a veces, lo empuja y aturde */
static int telekinesis(Player *p, const SkillDef *s, int lvl)
{
    if (!units_alive_enemy(p->target))
        return 0;
    int lo, hi, plo, phi;
    spell_damage(p, s, lvl, ELEM_LTNG, &lo, &hi);
    skill_phys(s, lvl, &plo, &phi);
    int u = p->target, dmg = spell_resist((int)(p - s_pl), u, ELEM_LTNG, roll(lo, hi)); /* (de a una tirada: el orden) */
    dmg += units_resist(u, ELEM_NONE, roll(plo, phi));
    float ux, uy;
    units_position(u, 1.0f, &ux, &uy);
    overlays_add("lightning", OV_AT_UNIT, u, ux, uy, 0);
    int exp = units_damage(u, s_one_hit ? 1 << 20 : dmg);
    if (exp > 0) {
        award_kill(p, u, exp);
    } else if ((int)(rnd() % 100) < s->param[2]) { /* Param2: el % de que lo empuje */
        units_knockback(u, p->x, p->y, 2.0f);
        units_stun(u, TICK_RATE / 2);
    }
    return 1;
}

/* Campo estático: a cada uno de alrededor le saca un % de la vida que le
   queda (sin matarlo), como SKILLS_AuraCallback_StaticField */
static int static_field(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
    int pct = skill_calc(s, SC_CALC1, lvl, &c), ids[64], n = units_enemies_near(p->x, p->y, r, ids, 64);
    for (int k = 0; k < n; k++) {
        int hp, max;
        units_health(ids[k], &hp, &max);
        if (s_dl_static_min > 0 && hp <= max * s_dl_static_min / 100)
            continue; /* (en Pesadilla no baja de un tercio y en Infierno de la mitad: StaticFieldMin) */
        int dmg = hp * pct / 100;
        if (dmg > hp - 1)
            dmg = hp - 1;
        dmg = spell_resist((int)(p - s_pl), ids[k], ELEM_LTNG, dmg);
        float x, y;
        units_position(ids[k], 1.0f, &x, &y);
        overlays_add("lightning", OV_AT_UNIT, ids[k], x, y, 0);
        if (dmg > 0)
            units_damage(ids[k], s_one_hit ? 1 << 20 : dmg);
    }
    missiles_effect("nova", p->x, p->y); /* que se vea la descarga */
    return n;
}

/* Hidra: tres cabezas alrededor del lugar, que duran Param1 + nivel * Param2 */
static int hydra_spawn(Player *p, const SkillDef *s, int lvl)
{
    static const float k_dx[3] = { -1.0f, 0.0f, 1.0f }, k_dy[3] = { -1.0f, 0.5f, -1.0f };
    int life = s->param[1] + (lvl - 1) * s->param[2], made = 0;
    if (life < HYDRA_LIFE_MIN)
        life = HYDRA_LIFE_MIN;
    for (int k = 0; k < 3; k++) {
        /* El lugar más viejo si ya están todos ocupados */
        int slot = 0;
        for (int h = 0; h < HYDRA_MAX; h++) {
            if (p->hydra_unit[h] < 0) {
                slot = h;
                break;
            }
            if (p->hydra_ticks[h] < p->hydra_ticks[slot])
                slot = h;
        }
        if (p->hydra_unit[slot] >= 0)
            units_pet_remove(p->hydra_unit[slot]);
        float x = p->cast_x + k_dx[k] * 1.5f, y = p->cast_y + k_dy[k] * 1.5f;
        free_landing(p, &x, &y);
        int u = units_pet_spawn((int)(p - s_pl), "hydra1", x, y, p->level_num, 0, 0, 0);
        p->hydra_unit[slot] = u;
        p->hydra_ticks[slot] = life;
        if (u < 0)
            continue;
        units_pet_turret(u, TICK_RATE);
        overlays_add("hydra_start1", OV_AT_POINT, 0, x, y, 0);
        overlays_add("hydra_loop1", OV_AT_UNIT, u, x, y, life);
        made++;
    }
    spell_damage(p, s, lvl, ELEM_FIRE, &p->hydra_lo, &p->hydra_hi);
    return made;
}

/* Una cabeza de la Hidra dispara al monstruo */
static void hydra_shot(int unit, int target)
{
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        for (int h = 0; h < HYDRA_MAX; h++) {
            if (p->hydra_unit[h] != unit)
                continue;
            float x, y, tx, ty;
            units_position(unit, 1.0f, &x, &y);
            units_position(target, 1.0f, &tx, &ty);
            spell_missile(p, "hydra", x, y, tx - x, ty - y, ELEM_FIRE, p->hydra_lo, p->hydra_hi, 0);
            return;
        }
    }
}

/* Las armaduras de hielo: más defensa y lo que le hacen al que le pega */
static void ice_armor(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    p->armor_kind = s->kind;
    p->armor_skill = (int)(s - skills_get(p->cls, 0));
    p->armor_pct = skill_calc(s, SC_AURASTAT1, lvl, &c);
    p->armor_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
    static const char *const k_overlay[3] = { "frozenarmor", "shiverarmor", "chillarmor" };
    int k = s->kind == SK_FROZEN_ARMOR ? 0 : s->kind == SK_SHIVER_ARMOR ? 1 : 2;
    overlays_add(k_overlay[k], OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, p->armor_ticks);
}

/* Un monstruo le pegó cuerpo a cuerpo: Armadura helada lo congela, la de
   escalofrío lo daña y lo enfría */
static void armor_struck(Player *p, int attacker)
{
    if (p->armor_ticks <= 0 || !units_alive_enemy(attacker))
        return;
    const SkillDef *s = skills_get(p->cls, p->armor_skill);
    int lvl = eff_lvl(p, p->armor_skill);
    SkillCtx c = pctx(p);
    float x, y;
    units_position(attacker, 1.0f, &x, &y);
    if (p->armor_kind == SK_FROZEN_ARMOR) {
        units_freeze(attacker, skill_calc(s, SC_CALC1, lvl, &c));
        overlays_add("frozenarmor_hit", OV_AT_UNIT, attacker, x, y, 0);
    } else if (p->armor_kind == SK_SHIVER_ARMOR) {
        int lo, hi;
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        int exp = units_damage(attacker, spell_resist((int)(p - s_pl), attacker, ELEM_COLD, roll(lo, hi)));
        if (exp > 0)
            award_kill(p, attacker, exp);
        else
            units_chill(attacker, skill_elen_syn(s, lvl, &c));
        overlays_add("shiverarmor_hit", OV_AT_UNIT, attacker, x, y, 0);
    }
}

/* Le pegó un proyectil: Armadura gélida le devuelve uno de hielo al que lo tiró */
static void armor_shot(Player *p, int shooter)
{
    if (p->armor_ticks <= 0 || p->armor_kind != SK_CHILLING_ARMOR || !units_alive_enemy(shooter))
        return;
    const SkillDef *s = skills_get(p->cls, p->armor_skill);
    int lvl = eff_lvl(p, p->armor_skill), lo, hi;
    SkillCtx c = pctx(p);
    spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
    float x, y;
    units_position(shooter, 1.0f, &x, &y);
    spell_missile(p, "chillingarmorbolt", p->x, p->y, x - p->x, y - p->y, ELEM_COLD, lo, hi, skill_elen_syn(s, lvl, &c));
    overlays_add("chillarmor_hit", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
}

/* Escudo de energía: el maná se lleva calc1 % del daño, a calc2/16 de maná
   por punto (SKILLS_EventFunc24_EnergyShield). Devuelve lo que queda. */
static int energy_shield(Player *p, int dmg)
{
    if (p->es_ticks <= 0 || dmg <= 0)
        return dmg;
    const SkillDef *s = skills_get(p->cls, p->es_skill);
    int lvl = eff_lvl(p, p->es_skill);
    SkillCtx c = pctx(p);
    int pct = skill_calc(s, SC_CALC1, lvl, &c), div = skill_calc(s, SC_CALC2, lvl, &c);
    if (pct > 95)
        pct = 95;
    if (div < 1)
        div = 1;
    int absorb = dmg * pct / 100, most = p->mana * 16 / div;
    if (absorb > most)
        absorb = most;
    if (absorb <= 0)
        return dmg;
    p->mana -= (absorb * div + 15) / 16;
    if (p->mana <= 0) {
        p->mana = 0;
        p->es_ticks = 0; /* sin maná, se termina */
    }
    overlays_add("energyshieldhit0", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
    return dmg - absorb;
}

/* Encantar: el compañero más cercano que no lo tenga (o el que lo tenga
   hace más), si no ella; suma daño de fuego a los golpes */
static int enchant(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    Player *best = p;
    for (int i = 0; i < s_num_players; i++) {
        Player *o = &s_pl[i];
        float dx = o->x - p->x, dy = o->y - p->y;
        if (o == p || o->state >= PS_DYING || dx * dx + dy * dy > 12.0f * 12.0f)
            continue;
        if (o->enchant_ticks < best->enchant_ticks || (best == p && o->enchant_ticks <= p->enchant_ticks))
            best = o;
    }
    int lo, hi;
    spell_damage(p, s, lvl, ELEM_FIRE, &lo, &hi);
    best->enchant_min = lo;
    best->enchant_max = hi;
    best->enchant_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
    refresh_stats(best);
    overlays_add("enchant", OV_AT_PLAYER, (int)(best - s_pl), best->x, best->y, 0);
    return 1;
}

/* ---- El efecto (en el cuadro del hechizo) ---- */

static int sorc_effect(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int lo = 0, hi = 0, elen = 0;
    float ax, ay;
    spell_aim(p, &ax, &ay);
    switch (s->kind) {
    case SK_FIRE_BOLT:
        spell_damage(p, s, lvl, ELEM_FIRE, &lo, &hi);
        return spell_missile(p, "firebolt", p->x, p->y, ax, ay, ELEM_FIRE, lo, hi, 0) != NULL;
    case SK_ICE_BOLT:
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        return spell_missile(p, "icebolt", p->x, p->y, ax, ay, ELEM_COLD, lo, hi, skill_elen_syn(s, lvl, &c)) != NULL;
    case SK_FIRE_BALL: {
        spell_damage(p, s, lvl, ELEM_FIRE, &lo, &hi);
        Missile *mi = spell_missile(p, "fireball", p->x, p->y, ax, ay, ELEM_FIRE, lo, hi, 0);
        if (mi) {
            mi->flags |= MF_EXPLODE;
            mi->radius10 = 30;
        }
        return mi != NULL;
    }
    case SK_ICE_BLAST: {
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        Missile *mi = spell_missile(p, "iceblast", p->x, p->y, ax, ay, ELEM_COLD, lo, hi, skill_elen_syn(s, lvl, &c));
        if (mi)
            mi->flags |= MF_FREEZE;
        return mi != NULL;
    }
    case SK_GLACIAL_SPIKE: { /* explota y congela alrededor (aurarange) por auralen */
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        Missile *mi = spell_missile(p, "glacialspike", p->x, p->y, ax, ay, ELEM_COLD, lo, hi,
                                    skill_calc(s, SC_AURALEN, lvl, &c));
        if (mi) {
            mi->flags |= MF_FREEZE_BURST;
            mi->radius10 = skill_calc(s, SC_AURARANGE, lvl, &c) * 10;
        }
        return mi != NULL;
    }
    case SK_CHARGED_BOLT: { /* calc1 chispas hacia el blanco: cada una va por su camino al azar (charged_step) */
        spell_damage(p, s, lvl, ELEM_LTNG, &lo, &hi);
        int n = skill_calc(s, SC_CALC1, lvl, &c);
        for (int k = 0; k < n; k++) {
            Missile *mi = spell_missile(p, "chargedbolt", p->x, p->y, ax, ay, ELEM_LTNG, lo, hi, 0);
            if (!mi)
                break;
            mi->flags |= MF_CHARGED;
        }
        return n;
    }
    case SK_LIGHTNING: { /* un rayo que atraviesa a todos (lightningbolt, su srvmissile) */
        spell_damage(p, s, lvl, ELEM_LTNG, &lo, &hi);
        Missile *mi = spell_missile(p, "lightningbolt", p->x, p->y, ax, ay, ELEM_LTNG, lo, hi, 0);
        if (mi)
            mi->flags |= MF_PIERCE;
        return mi != NULL;
    }
    case SK_CHAIN_LIGHTNING: { /* salta calc1 veces */
        spell_damage(p, s, lvl, ELEM_LTNG, &lo, &hi);
        Missile *mi = spell_missile(p, "chainlightning", p->x, p->y, ax, ay, ELEM_LTNG, lo, hi, 0);
        if (mi) {
            mi->flags |= MF_CHAIN_LTNG;
            mi->count = skill_calc(s, SC_CALC1, lvl, &c);
            mi->radius10 = skill_calc(s, SC_AURARANGE, lvl, &c) * 10;
        }
        return mi != NULL;
    }
    case SK_FROZEN_ORB: {
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        Missile *mi = spell_missile(p, "frozenorb", p->x, p->y, ax, ay, ELEM_COLD, lo, hi, skill_elen_syn(s, lvl, &c));
        if (mi) {
            mi->flags |= MF_ORB | MF_NOHIT;
            mi->count = (int)(rnd() % 64);
        }
        return mi != NULL;
    }
    case SK_FROST_NOVA:
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        nova_ring(p, "frostnova", p->x, p->y, 1, ELEM_COLD, lo, hi, skill_elen_syn(s, lvl, &c));
        return 1;
    case SK_NOVA:
        spell_damage(p, s, lvl, ELEM_LTNG, &lo, &hi);
        nova_ring(p, "nova", p->x, p->y, 1, ELEM_LTNG, lo, hi, 0);
        return 1;
    case SK_STATIC_FIELD:
        return static_field(p, s, lvl);
    case SK_TELEKINESIS:
        return telekinesis(p, s, lvl);
    case SK_TELEPORT:
        return teleport(p);
    case SK_FIRE_WALL: { /* dos que avanzan de costado desde el lugar, dejando fuego */
        spell_burn(p, s, lvl, &lo, &hi);
        float tx = p->cast_x, ty = p->cast_y;
        if (units_alive_enemy(p->target))
            units_position(p->target, 1.0f, &tx, &ty);
        float dx = p->x - tx, dy = p->y - ty;
        int made = 0;
        for (int side = -1; side <= 1; side += 2) {
            Missile *mi = spell_missile(p, "firewallmaker", tx, ty, -dy * (float)side, dx * (float)side, ELEM_FIRE, 0, 0, 0);
            if (!mi)
                continue;
            mi->flags |= MF_MAKER | MF_NOHIT;
            mi->life = 7 + 2 * lvl; /* Range de firewallmaker: 7 + 2 por nivel */
            mi->burn_min = lo;
            mi->burn_max = hi;
            mi->burn_life = 90;
            mi->mx = mi->my = -1000.0f;
            made++;
        }
        return made;
    }
    case SK_BLAZE:
        p->blaze_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->blaze_skill = (int)(s - skills_get(p->cls, 0));
        p->blaze_x = p->blaze_y = -1000.0f;
        return 1;
    case SK_INFERNO: /* llamas mientras dure: calc1 es el alcance (cuadros) */
        p->inferno_ticks = TICK_RATE * 2 / 3;
        p->inferno_skill = (int)(s - skills_get(p->cls, 0));
        return 1;
    case SK_METEOR: { /* la marca en el piso, y a los 60 cuadros cae */
        float tx = p->cast_x, ty = p->cast_y;
        if (units_alive_enemy(p->target))
            units_position(p->target, 1.0f, &tx, &ty);
        spell_damage(p, s, lvl, ELEM_FIRE, &lo, &hi);
        Missile *mark = missiles_effect("meteorcenter", tx, ty);
        Missile *mi = spell_missile(p, "meteor", tx, ty, 0.0f, 0.0f, ELEM_FIRE, lo, hi, 0);
        if (!mi)
            return mark != NULL;
        mi->flags |= MF_EFFECT | MF_LAND | MF_METEOR;
        mi->life = 60;
        missiles_meteor(mi, "meteorcenter"); /* cae en diagonal en sus últimos cuadros, con la cola */
        mi->radius10 = skill_calc(s, SC_AURARANGE, lvl, &c) * 10;
        /* El fuego que deja (meteorfire): su daño y lo que dura */
        int flo, fhi, fdmin, fdmax, felem;
        missiles_base_damage("meteorfire", &felem, &flo, &fhi, &fdmin, &fdmax);
        mi->burn_min = (flo << 3) * TICK_RATE / 256 * (100 + p->sk_fire_pct) / 100 + 1;
        mi->burn_max = (fhi << 3) * TICK_RATE / 256 * (100 + p->sk_fire_pct) / 100 + 1;
        mi->burn_life = s->param[3] + (lvl - 1) * s->param[4];
        if (mi->burn_life < 30)
            mi->burn_life = 30;
        return 1;
    }
    case SK_BLIZZARD: { /* la zona: cada calc2 cuadros cae un trozo a hasta calc1 */
        float tx = p->cast_x, ty = p->cast_y;
        if (units_alive_enemy(p->target))
            units_position(p->target, 1.0f, &tx, &ty);
        spell_damage(p, s, lvl, ELEM_COLD, &lo, &hi);
        Missile *mi = spell_missile(p, "blizzardcenter", tx, ty, 0.0f, 0.0f, ELEM_COLD, lo, hi, skill_elen_syn(s, lvl, &c));
        if (!mi)
            return 0;
        mi->flags |= MF_EFFECT | MF_NOHIT | MF_BLIZZARD;
        mi->count = skill_calc(s, SC_CALC2, lvl, &c);     /* cada cuántos cuadros */
        mi->radius10 = skill_calc(s, SC_CALC1, lvl, &c) * 10; /* dónde caen */
        return 1;
    }
    case SK_HYDRA:
        return hydra_spawn(p, s, lvl);
    case SK_FROZEN_ARMOR:
    case SK_SHIVER_ARMOR:
    case SK_CHILLING_ARMOR:
        ice_armor(p, s, lvl);
        return 1;
    case SK_ENERGY_SHIELD:
        p->es_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->es_skill = (int)(s - skills_get(p->cls, 0));
        overlays_add("energyshield", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, p->es_ticks);
        return 1;
    case SK_THUNDER_STORM:
        p->storm_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->storm_skill = (int)(s - skills_get(p->cls, 0));
        p->storm_next = TICK_RATE;
        overlays_add("thunderstormcast", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
        overlays_add("thunderstormback", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, p->storm_ticks);
        return 1;
    case SK_ENCHANT:
        return enchant(p, s, lvl);
    default:
        break;
    }
    (void)elen;
    return 0;
}

/* ---- Lo que dura ---- */

/* Termina todo lo que dura (estados e Hidras) */
static void sorc_clear(Player *p)
{
    p->armor_ticks = p->es_ticks = p->blaze_ticks = p->storm_ticks = p->inferno_ticks = 0;
    if (p->enchant_ticks > 0) {
        p->enchant_ticks = 0;
        refresh_stats(p);
    }
    for (int h = 0; h < HYDRA_MAX; h++)
        if (p->hydra_unit[h] >= 0) {
            units_pet_remove(p->hydra_unit[h]);
            p->hydra_unit[h] = -1;
        }
}

static void sorc_tick(Player *p)
{
    int me = (int)(p - s_pl);
    if (p->armor_ticks > 0)
        p->armor_ticks--;
    if (p->es_ticks > 0)
        p->es_ticks--;
    if (p->enchant_ticks > 0 && --p->enchant_ticks == 0)
        refresh_stats(p);
    for (int h = 0; h < HYDRA_MAX; h++)
        if (p->hydra_unit[h] >= 0 && --p->hydra_ticks[h] <= 0) {
            float x, y;
            units_position(p->hydra_unit[h], 1.0f, &x, &y);
            overlays_add("hydra_end1", OV_AT_POINT, 0, x, y, 0);
            units_pet_remove(p->hydra_unit[h]);
            p->hydra_unit[h] = -1;
        }
    /* Llamarada: fuego por donde camina */
    if (p->blaze_ticks > 0) {
        p->blaze_ticks--;
        float dx = p->x - p->blaze_x, dy = p->y - p->blaze_y;
        if (dx * dx + dy * dy >= 1.0f && !(p->level && p->level->town)) {
            const SkillDef *s = skills_get(p->cls, p->blaze_skill);
            int lvl = eff_lvl(p, p->blaze_skill), lo, hi;
            spell_burn(p, s, lvl, &lo, &hi);
            Missile *f = missiles_effect("blaze", p->x, p->y);
            if (f) {
                f->flags |= MF_BURN;
                f->owner = me;
                f->emin = lo;
                f->emax = hi;
                f->radius10 = 10;
                f->life = 90 + 25 * lvl;
            }
            p->blaze_x = p->x;
            p->blaze_y = p->y;
        }
    }
    /* Infierno: un chorro de llamas hacia el monstruo o hacia adelante; daña
       por segundo a los que están en el chorro */
    if (p->inferno_ticks > 0) {
        p->inferno_ticks--;
        const SkillDef *s = skills_get(p->cls, p->inferno_skill);
        int lvl = eff_lvl(p, p->inferno_skill), lo, hi;
        SkillCtx c = pctx(p);
        spell_burn(p, s, lvl, &lo, &hi);
        float ax, ay;
        spell_aim(p, &ax, &ay);
        float l = sqrtf(ax * ax + ay * ay);
        ax /= l > 0.0f ? l : 1.0f;
        ay /= l > 0.0f ? l : 1.0f;
        int range = skill_calc(s, SC_CALC1, lvl, &c);
        Missile *f = missiles_spawn(p->inferno_ticks % 2 ? "infernoflame1" : "infernoflame2", p->x + ax * 0.6f,
                                    p->y + ay * 0.6f, ax, ay);
        if (f) {
            f->flags |= MF_NOHIT;
            f->owner = me;
            f->life = range > 4 ? range : 4;
        }
        float reach = 12.0f * (MISSILE_VEL_PER_SEC / TICK_RATE) * (float)(range > 4 ? range : 4); /* lo que avanza una llama */
        int ids[16], n = units_enemies_near(p->x + ax * reach / 2.0f, p->y + ay * reach / 2.0f, reach / 2.0f + 0.8f, ids, 16);
        for (int k = 0; k < n; k++) {
            int full = roll(lo, hi), d = full / TICK_RATE + ((int)(rnd() % TICK_RATE) < full % TICK_RATE);
            int exp = d > 0 ? units_damage(ids[k], s_one_hit ? 1 << 20 : units_resist(ids[k], ELEM_FIRE, d)) : 0;
            if (exp > 0)
                award_kill(p, ids[k], exp);
        }
    }
    /* Tormenta: cada tanto un rayo sobre un monstruo cercano */
    if (p->storm_ticks > 0) {
        p->storm_ticks--;
        if (--p->storm_next <= 0) {
            p->storm_next = TICK_RATE * 8 / 5;
            int u = units_find_enemy(p->x, p->y, 15.0f);
            if (u >= 0 && !(p->level && p->level->town)) {
                const SkillDef *s = skills_get(p->cls, p->storm_skill);
                int lvl = eff_lvl(p, p->storm_skill), lo, hi;
                spell_damage(p, s, lvl, ELEM_LTNG, &lo, &hi);
                float x, y;
                units_position(u, 1.0f, &x, &y);
                Missile *m = missiles_effect("thunderstorm1", x, y);
                if (m)
                    m->flags |= MF_NOHIT;
                overlays_add("lightning", OV_AT_UNIT, u, x, y, 0);
                int exp = units_damage(u, s_one_hit ? 1 << 20 : spell_resist(me, u, ELEM_LTNG, roll(lo, hi)));
                if (exp > 0)
                    award_kill(p, u, exp);
            }
        }
    }
}

/* ---- Los proyectiles de la Hechicera (cada tick, desde tick_missiles) ---- */

/* Rayo en cadena: desde el que tocó, al monstruo más cercano que no sea él */
static void chain_jump(const Missile *mi, int hit)
{
    if (mi->count <= 1 || mi->owner < 0 || mi->owner >= s_num_players)
        return;
    float r = mi->radius10 > 0 ? mi->radius10 / 10.0f : 13.0f;
    int ids[32], n = units_enemies_near(mi->x, mi->y, r, ids, 32), best = -1;
    float bd = 1e9f;
    for (int k = 0; k < n; k++) {
        if (ids[k] == hit)
            continue;
        float x, y;
        units_position(ids[k], 1.0f, &x, &y);
        float d = (x - mi->x) * (x - mi->x) + (y - mi->y) * (y - mi->y);
        if (d < bd) {
            bd = d;
            best = ids[k];
        }
    }
    if (best < 0)
        return;
    float x, y;
    units_position(best, 1.0f, &x, &y);
    Missile *nm = spell_missile(&s_pl[mi->owner], "chainlightning", mi->x, mi->y, x - mi->x, y - mi->y, ELEM_LTNG, mi->emin,
                                mi->emax, 0);
    if (!nm)
        return;
    nm->flags |= MF_CHAIN_LTNG;
    nm->count = mi->count - 1;
    nm->radius10 = mi->radius10;
    if (nm->num_hit < MISSILE_HITS) /* sale de encima del que tocó: no le vuelve a pegar */
        nm->hit[nm->num_hit++] = hit;
}

/* El Orbe congelado escupe descargas en espiral (cada cuadro, girando 19 de 64) */
static void orb_tick(Missile *mi)
{
    if (mi->owner < 0 || mi->owner >= s_num_players)
        return;
    float a = (float)mi->count * 2.0f * PI_F / 64.0f;
    mi->count = (mi->count + 19) % 64;
    spell_missile(&s_pl[mi->owner], "frozenorbbolt", mi->x, mi->y, cosf(a), sinf(a), ELEM_COLD, mi->emin, mi->emax, mi->elen);
}

/* Al terminar: el Orbe suelta su nova (32 descargas), la Ventisca y el Meteoro dañan donde caen */
static void sorc_missile_end(Missile *mi)
{
    if (mi->owner < 0) { /* de un monstruo: la Ventisca de los sacerdotes, el Meteoro de los vampiros */
        float r = mi->radius10 / 10.0f;
        if (mi->flags & MF_LAND)
            enemy_area(mi, r > 0.8f ? r : 0.8f);
        if (mi->flags & MF_METEOR) {
            missiles_effect("meteorexplode", mi->x, mi->y);
            for (int k = 0; k < 6; k++) {
                Missile *f = missiles_effect(mi->trail ? mi->trail : "meteorfire", mi->x + (float)(k % 3 - 1) * 1.4f,
                                             mi->y + (float)(k / 3) * 1.4f - 0.7f);
                if (!f)
                    break;
                f->flags |= MF_BURN;
                f->owner = -1;
                f->emin = mi->burn_min;
                f->emax = mi->burn_max;
                f->radius10 = 15;
                f->life = mi->burn_life;
            }
        }
        return;
    }
    if (mi->owner >= s_num_players)
        return;
    Player *p = &s_pl[mi->owner];
    if (mi->flags & MF_ORB) {
        nova_ring(p, "frozenorbnova", mi->x, mi->y, 2, ELEM_COLD, mi->emin, mi->emax, mi->elen);
        missiles_effect("frozenorbexplode", mi->x, mi->y);
    }
    if (mi->flags & MF_LAND) {
        float r = mi->radius10 / 10.0f;
        int ids[32], n = units_enemies_near(mi->x, mi->y, r > 0.8f ? r : 0.8f, ids, 32);
        for (int k = 0; k < n; k++) {
            int dmg = spell_resist(mi->owner, ids[k], mi->elem, roll(mi->emin, mi->emax));
            if (mi->dmax > 0) /* y lo físico (las rocas del Druida) */
                dmg += units_resist(ids[k], ELEM_NONE, roll(mi->dmin, mi->dmax));
            int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : dmg);
            if (exp > 0)
                award_kill(p, ids[k], exp);
            else if (mi->elem == ELEM_COLD && mi->elen > 0)
                units_chill(ids[k], mi->elen);
        }
    }
    if (mi->flags & MF_METEOR) { /* la explosión y el fuego alrededor */
        missiles_effect("meteorexplode", mi->x, mi->y);
        static const int k_ox[6] = { 2, -2, 0, -3, 3, 0 }, k_oy[6] = { -2, -2, 2, 2, 2, 0 };
        for (int k = 0; k < 6; k++) {
            Missile *f = missiles_effect("meteorfire", mi->x + (float)k_ox[k] * 0.7f, mi->y + (float)k_oy[k] * 0.7f);
            if (!f)
                break;
            f->flags |= MF_BURN;
            f->owner = mi->owner;
            f->emin = mi->burn_min;
            f->emax = mi->burn_max;
            f->radius10 = 15;
            f->life = mi->burn_life;
        }
    }
}

/* Un trozo de hielo de la Ventisca: el dibujo (icestormfall) es el carámbano
   quieto y el cliente lo baja desde CltParam1 (120 píxeles) hasta el piso en
   lo que dura (Range, 9 cuadros); al tocarlo daña y explota */
static void blizzard_fall(Missile *b)
{
    b->z = 120.0f;
    b->vz = b->life > 0 ? 120.0f / (float)b->life : 120.0f;
}

/* Lo de cada tick que no es chocar: el Orbe, la Ventisca y el Muro de fuego */
static void sorc_missile_tick(Missile *mi)
{
    if (mi->flags & MF_ORB)
        orb_tick(mi);
    if (mi->flags & MF_MAKER) { /* (también el de los vampiros) */
        float dx = mi->x - mi->mx, dy = mi->y - mi->my;
        if (dx * dx + dy * dy >= 1.0f) {
            Missile *f = missiles_effect(mi->trail ? mi->trail : "firewall", mi->x, mi->y);
            if (f) {
                f->flags |= MF_BURN;
                f->owner = mi->owner;
                f->emin = mi->burn_min;
                f->emax = mi->burn_max;
                f->radius10 = 6;
                f->life = mi->burn_life;
            }
            mi->mx = mi->x;
            mi->my = mi->y;
        }
    }
    /* El centro de la Ventisca de los sacerdotes (MonBlizzard): sus trozos de hielo (monbliz1..4) */
    if ((mi->flags & MF_BLIZZARD) && mi->owner < 0 && mi->count > 0 && mi->age % mi->count == 0) {
        float r = mi->radius10 / 10.0f - 1.0f; /* (MISSMODE_CreateMissileWithCollisionCheck: hasta el radio menos uno) */
        float x = mi->x + ((float)(rnd() % 1000) / 500.0f - 1.0f) * r, y = mi->y + ((float)(rnd() % 1000) / 500.0f - 1.0f) * r;
        char shard[16];
        snprintf(shard, sizeof shard, "monbliz%d", 1 + (int)(rnd() % 4));
        Missile *b = map_blocked(&s_map, (int)floorf(x), (int)floorf(y)) ? NULL : missiles_effect(shard, x, y);
        if (b) {
            b->flags |= MF_EFFECT | MF_LAND | MF_NOHIT | MF_CHAIN;
            b->owner = -1;
            b->source = mi->source;
            b->elem = ELEM_COLD;
            b->emin = mi->emin;
            b->emax = mi->emax;
            b->elen = mi->elen;
            b->radius10 = 20;
            blizzard_fall(b);
        }
    }
    /* El centro de la Ventisca: cada `count` cuadros un trozo de hielo en un lugar al azar */
    if ((mi->flags & MF_BLIZZARD) && mi->owner >= 0 && mi->count > 0 && mi->age % mi->count == 0) {
        float r = mi->radius10 / 10.0f - 1.0f; /* (MISSMODE_CreateMissileWithCollisionCheck: hasta el radio menos uno) */
        float x = mi->x + ((float)(rnd() % 1000) / 500.0f - 1.0f) * r, y = mi->y + ((float)(rnd() % 1000) / 500.0f - 1.0f) * r;
        if (!map_blocked(&s_map, (int)floorf(x), (int)floorf(y))) {
            static const char *const k_shards[4] = { "blizzard1", "blizzard2", "blizzard3", "blizzard4" };
            Missile *b = spell_missile(&s_pl[mi->owner], k_shards[rnd() % 4], x, y, 0.0f, 0.0f, ELEM_COLD, mi->emin, mi->emax, mi->elen);
            if (b) {
                b->flags |= MF_EFFECT | MF_LAND | MF_NOHIT | MF_CHAIN;
                b->radius10 = 20;
                blizzard_fall(b);
            }
        }
    }
}
