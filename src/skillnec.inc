/* ---- Las habilidades del Nigromante (se incluye desde game.c) ----

   Como en el original (SkillNec.cpp de D2MOO):
   - Levantar esqueleto y mago, y Revivir, usan un cadáver (el más cercano;
     Revivir, uno de un monstruo que se pueda revivir). Los esbirros son
     monstruos de monstats.txt de nivel habilidad + 3/4 del del jugador, con
     lo que les suman la habilidad (calc1 es el % de vida, las aurastat y
     passivestat: daño, puntería, defensa, vida y daño de Dominio de
     esqueletos) y Resistir invocaciones. Si ya tiene el máximo (petmax), el
     más viejo se deshace. Los magos tiran uno de cuatro proyectiles al azar
     (veneno, frío, fuego o rayo); los revividos duran 3 minutos.
   - Un gólem a la vez: arcilla (frena), sangre (roba vida y le pasa una
     parte al Nigromante), hierro (de un arma o armadura del piso; devuelve
     el daño) y fuego (pega con fuego y tiene Fuego sagrado).
   - Las maldiciones van al monstruo más cercano y a los de alrededor
     (aurarangecalc), por auralencalc cuadros; una a la vez por monstruo.
   - Muro y Prisión de hueso: "monstruos" quietos que tapan el paso y que
     los monstruos rompen a golpes; duran 24 segundos.
   - Explosión de cadáver: del 70 al 120% de la vida del muerto, mitad
     fuego, a los de alrededor. Armadura de hueso: absorbe el daño cuerpo a
     cuerpo hasta que se gasta. */

#define NEC_CORPSE_RANGE 10.0f  /* busca cadáveres hasta acá */
#define NEC_GOLEM_PULSE 50      /* el Fuego sagrado del gólem de fuego (perdelay) */
enum { PK_NONE, PK_SKELETON, PK_MAGE, PK_REVIVE, PK_GOLEM, PK_WALL };

static int nec_kind(int kind)
{
    return kind >= SK_AMPLIFY_DAMAGE && kind <= SK_REVIVE;
}

static int nec_curse_kind(int kind)
{
    return kind >= SK_AMPLIFY_DAMAGE && kind <= SK_LOWER_RESIST;
}

/* La maldición: su tipo, su dibujo (el overlay1 de su estado), el efecto al
   lanzarla (cltmissilea) y lo que suena (onsound) */
typedef struct {
    int         kind, nc;
    const char *overlay, *missile, *sound;
} CurseLook;
static const CurseLook k_curse_looks[] = {
    { SK_AMPLIFY_DAMAGE, NC_AMPLIFY, "curseamplifydamage", "curseamplifydamage", "necromancer_amplifydamage" },
    { SK_DIM_VISION, NC_DIM, "cursedimvision", "cursedimvision", "necromancer_dimvision" },
    { SK_WEAKEN, NC_WEAKEN, "curseweaken", "curseweaken", "necromancer_weaken" },
    { SK_IRON_MAIDEN, NC_IRONMAIDEN, "curseironmaiden", "curseironmaiden", "necromancer_ironmaiden" },
    { SK_TERROR, NC_TERROR, "curseterror", "curseterror", "necromancer_taint" },
    { SK_CONFUSE, NC_CONFUSE, "curseconfuse", "curseconfuse", "necromancer_confuse" },
    { SK_LIFE_TAP, NC_LIFETAP, "cursereversevampire", "cursereversevampire", "necromancer_reversevampire" },
    { SK_ATTRACT, NC_ATTRACT, "curseattract", "curseattract", "necromancer_attract" },
    { SK_DECREPIFY, NC_DECREPIFY, "cursedecrepify", "cursedecrepify", "necromancer_decrepify" },
    { SK_LOWER_RESIST, NC_LOWERRES, "curselowerresist", "curselowerresist", NULL },
};

static const CurseLook *curse_look(int kind)
{
    for (unsigned k = 0; k < sizeof k_curse_looks / sizeof k_curse_looks[0]; k++)
        if (k_curse_looks[k].kind == kind)
            return &k_curse_looks[k];
    return NULL;
}

/* Los monstruos maldecidos: al morir (o al terminarse) la maldición deja de verse */
#define NEC_CURSED 128
static int s_cursed[NEC_CURSED], s_num_cursed;

static void nec_cursed_add(int unit)
{
    for (int k = 0; k < s_num_cursed; k++)
        if (s_cursed[k] == unit)
            return;
    if (s_num_cursed < NEC_CURSED)
        s_cursed[s_num_cursed++] = unit;
}

static void nec_cursed_tick(void)
{
    for (int k = 0; k < s_num_cursed;) {
        if (units_nec_cursed(s_cursed[k])) {
            k++;
            continue;
        }
        overlays_remove("curse", OV_AT_UNIT, s_cursed[k]);
        s_cursed[k] = s_cursed[--s_num_cursed];
    }
}

/* ---- Los esbirros ---- */

static int nec_pet_count(const Player *p, int kind)
{
    int n = 0;
    for (int k = 0; k < NEC_PETS; k++)
        n += p->pet_kind[k] == kind && units_pet_alive(p->pets[k]);
    return n;
}

/* Saca el esbirro (al momento) */
static void nec_pet_drop(Player *p, int k)
{
    if (p->pet_kind[k] != PK_NONE)
        units_pet_remove(p->pets[k]);
    p->pet_kind[k] = PK_NONE;
    p->pet_dead[k] = 0;
}

/* Lugar para uno más de ese tipo: si ya tiene `most`, el más viejo se deshace */
static int nec_pet_slot(Player *p, int kind, int most)
{
    int alive = 0, oldest = -1, free_slot = -1;
    for (int k = 0; k < NEC_PETS; k++) {
        if (p->pet_kind[k] == PK_NONE) {
            if (free_slot < 0)
                free_slot = k;
            continue;
        }
        if (p->pet_kind[k] != kind)
            continue;
        if (!units_pet_alive(p->pets[k])) { /* muerto: el lugar sirve */
            nec_pet_drop(p, k);
            if (free_slot < 0 || k < free_slot)
                free_slot = k;
            continue;
        }
        alive++;
        if (oldest < 0 || p->pet_born[k] < p->pet_born[oldest])
            oldest = k;
    }
    if (most > 0 && alive >= most && oldest >= 0) {
        nec_pet_drop(p, oldest);
        return oldest;
    }
    return free_slot;
}

static void nec_pet_keep(Player *p, int slot, int unit, int kind)
{
    p->pets[slot] = unit;
    p->pet_kind[slot] = kind;
    p->pet_dead[slot] = 0;
    p->pet_born[slot] = s_ticks;
}

/* Los esbirros que murieron se sacan a los 2 segundos (ya se vio su muerte) */
static void nec_pets_tick(Player *p)
{
    for (int k = 0; k < NEC_PETS; k++)
        if (p->pet_kind[k] != PK_NONE && !units_pet_alive(p->pets[k]) && ++p->pet_dead[k] > 2 * TICK_RATE)
            nec_pet_drop(p, k);
}

/* Termina todo (al empezar una ronda de la prueba) */
static void nec_clear(Player *p)
{
    for (int k = 0; k < NEC_PETS; k++)
        if (p->pet_kind[k] != PK_NONE)
            nec_pet_drop(p, k);
    p->bone_armor = p->bone_armor_max = 0;
}

/* Lo que le suma una propiedad de la habilidad al esbirro (los nombres de itemstatcost.txt) */
static void pet_stat(PetSpec *ps, const char *stat, int v)
{
    if (!strcmp(stat, "damagepercent"))
        ps->dmg_pct += v;
    else if (!strcmp(stat, "tohit"))
        ps->ar_add += v;
    else if (!strcmp(stat, "armorclass"))
        ps->def_add += v;
    else if (!strcmp(stat, "maxhp"))
        ps->hp_add += v / 256; /* en 1/256 de punto */
    else if (!strcmp(stat, "item_normaldamage"))
        ps->dmg_add += v;
    else if (!strcmp(stat, "velocitypercent"))
        ps->speed_pct += v;
    else if (!strcmp(stat, "item_slow"))
        ps->slow = v;
    else if (!strcmp(stat, "item_attackertakesdamage"))
        ps->thorns = v;
    else if (!strcmp(stat, "firemindam"))
        ps->fire_min = v;
    else if (!strcmp(stat, "firemaxdam"))
        ps->fire_max = v;
}

/* El esbirro de esa habilidad: su nivel, el % de vida (calc1), las aurastat
   y passivestat, y Resistir invocaciones */
static void pet_spec(const Player *p, const SkillDef *s, int lvl, PetSpec *ps)
{
    SkillCtx c = pctx(p);
    memset(ps, 0, sizeof *ps);
    int pl = lvl + 3 * p->level_num / 4;
    ps->level = pl < 1 ? 1 : pl > p->level_num ? p->level_num : pl;
    ps->hp_pct = skill_calc(s, SC_CALC1, lvl, &c);
    for (int k = 0; k < 5; k++) {
        if (k < 3 && s->aurastat[k][0])
            pet_stat(ps, s->aurastat[k], skill_calc(s, SC_AURASTAT1 + k, lvl, &c));
        if (s->passivestat[k][0])
            pet_stat(ps, s->passivestat[k], skill_calc(s, k < 3 ? SC_PASSIVE1 + k : SC_PASSIVE4 + k - 3, lvl, &c));
    }
    int sr = skills_find(p->cls, "Summon Resist");
    if (sr >= 0 && eff_lvl(p, sr) > 0)
        ps->res = skill_calc(skills_get(p->cls, sr), SC_PASSIVE1, eff_lvl(p, sr), &c);
}

/* El cadáver más cercano (para Revivir, uno que se pueda revivir); con un
   monstruo vivo cerca, el más cercano a él (Explosión de cadáver) */
static int nec_corpse(const Player *p, int near_enemy, int revive)
{
    float x = p->x, y = p->y;
    int e = near_enemy ? units_find_enemy(p->x, p->y, AIM_RANGE) : -1;
    if (e >= 0)
        units_position(e, 1.0f, &x, &y);
    int ids[64], n = units_corpses_near(p->x, p->y, NEC_CORPSE_RANGE, ids, 64), best = -1;
    float best_d = 0.0f;
    for (int k = 0; k < n; k++) {
        if (revive && !units_revivable(ids[k]))
            continue;
        float cx, cy;
        units_position(ids[k], 1.0f, &cx, &cy);
        float d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        if (best < 0 || d < best_d) {
            best = ids[k];
            best_d = d;
        }
    }
    return best;
}

/* El cadáver sigue ahí (otro no lo usó mientras se lanzaba) */
static int nec_corpse_ok(int c)
{
    return c >= 0 && units_is_corpse(c) && units_visible(c);
}

/* ---- Al lanzarla ---- */

/* 1 si arrancó (gasta el maná); si falta el cadáver o el objeto, avisa */
static int nec_start(Player *p, int skill, int cost)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int in_town = p->level && p->level->town;
    p->cast_target = -1;
    switch (s->kind) {
    case SK_POISON_DAGGER:
        start_attack(p, skill);
        p->mana -= cost;
        return 1;
    case SK_RAISE_SKELETON:
    case SK_RAISE_MAGE:
    case SK_REVIVE:
    case SK_CORPSE_EXPLOSION:
    case SK_POISON_EXPLOSION: {
        int c = nec_corpse(p, s->kind == SK_CORPSE_EXPLOSION || s->kind == SK_POISON_EXPLOSION, s->kind == SK_REVIVE);
        if (c < 0 || in_town) {
            show_message(p, s->kind == SK_REVIVE ? "No hay nada que revivir" : "No hay ningún cadáver cerca");
            return 0;
        }
        p->cast_target = c;
        face_unit(p, c);
        break;
    }
    case SK_IRON_GOLEM: { /* de un arma o armadura del piso */
        int it = -1;
        float best = 10.0f * 10.0f, ix, iy;
        for (int k = 0; k < items_ground_count(); k++) {
            const Item *g = items_ground_item(k, &ix, &iy);
            if (!g || !g->type || (g->type->kind != ITEM_WEAPON && g->type->kind != ITEM_ARMOR) || is_quest_item(g->type))
                continue;
            float d = (ix - p->x) * (ix - p->x) + (iy - p->y) * (iy - p->y);
            if (d < best) {
                best = d;
                it = k;
            }
        }
        if (it < 0) {
            show_message(p, "Hace falta un arma o armadura en el piso");
            return 0;
        }
        p->cast_target = it;
        break;
    }
    case SK_BONE_PRISON:
        pal_aim(p, AIM_RANGE);
        if (p->target < 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        break;
    default:
        pal_aim(p, AIM_RANGE);
        if (nec_curse_kind(s->kind) && p->target < 0 && !in_town) {
            show_message(p, "No hay a quién");
            return 0;
        }
        break;
    }
    if (in_town && !s->intown) {
        show_message(p, "En el pueblo no se pelea");
        return 0;
    }
    p->attack_skill = skill;
    p->hits_done = 0;
    play_skill_sound(p, s);
    p->mana -= cost;
    if (!player_anim(p, MODE_SC)->ok) {
        cast_effect(p);
        return 1;
    }
    set_player_state(p, PS_CAST, MODE_SC);
    return 1;
}

/* Una maldición en el monstruo más cercano y los de alrededor */
static int nec_curse(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    const CurseLook *look = curse_look(s->kind);
    if (!look)
        return 0;
    float cx = p->cast_x, cy = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &cx, &cy);
    float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
    NecCurse nc;
    memset(&nc, 0, sizeof nc);
    nc.kind = look->nc;
    nc.ticks = skill_calc(s, SC_AURALEN, lvl, &c);
    nc.owner = (int)(p - s_pl);
    switch (s->kind) {
    case SK_AMPLIFY_DAMAGE:
        nc.phys = -skill_calc(s, SC_AURASTAT1, lvl, &c); /* damageresist -100 */
        break;
    case SK_WEAKEN:
        nc.dmg = skill_calc(s, SC_AURASTAT1, lvl, &c);
        break;
    case SK_IRON_MAIDEN:
        nc.im = skill_calc(s, SC_CALC1, lvl, &c);
        break;
    case SK_LIFE_TAP:
        nc.lt = skill_calc(s, SC_CALC1, lvl, &c);
        break;
    case SK_DECREPIFY: /* -50% de velocidad, de daño, de ataque y de resistencia física */
        nc.dmg = skill_calc(s, SC_AURASTAT2, lvl, &c);
        nc.phys = -skill_calc(s, SC_AURASTAT3, lvl, &c);
        nc.slow = -skill_calc(s, SC_AURASTAT1, lvl, &c);
        break;
    case SK_LOWER_RESIST:
        nc.res = -skill_calc(s, SC_AURASTAT1, lvl, &c);
        break;
    default:
        break;
    }
    missiles_effect(look->missile, cx, cy);
    if (look->sound)
        audio_play_name(look->sound, cx, cy);
    int ids[64], n = units_enemies_near(cx, cy, r, ids, 64), done = 0;
    if (s->kind == SK_ATTRACT) { /* el más cercano atrae a los demás */
        if (!units_alive_enemy(p->target) || !units_nec_curse(p->target, &nc))
            return 0;
        units_nec_attract(p->target, r, nc.ticks);
        n = 1;
        ids[0] = p->target;
    }
    for (int k = 0; k < n; k++) {
        if (s->kind != SK_ATTRACT && !units_nec_curse(ids[k], &nc))
            continue;
        float ux, uy;
        units_position(ids[k], 1.0f, &ux, &uy);
        overlays_remove("curse", OV_AT_UNIT, ids[k]); /* la anterior */
        overlays_add(look->overlay, OV_AT_UNIT, ids[k], ux, uy, nc.ticks);
        overlays_add("curse_hit", OV_AT_UNIT, ids[k], ux, uy, 0);
        nec_cursed_add(ids[k]);
        done++;
    }
    return done;
}

/* Un esbirro de la habilidad en ese lugar (o de ese cadáver) */
static int nec_summon(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl), corpse = p->cast_target;
    PetSpec ps;
    pet_spec(p, s, lvl, &ps);
    float x = p->x, y = p->y;
    if ((s->kind == SK_RAISE_SKELETON || s->kind == SK_RAISE_MAGE || s->kind == SK_REVIVE) && corpse >= 0)
        units_position(corpse, 1.0f, &x, &y);
    if ((s->kind == SK_RAISE_SKELETON || s->kind == SK_RAISE_MAGE || s->kind == SK_REVIVE) && !nec_corpse_ok(corpse))
        return 0;
    int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, &c) : 1;
    int kind = s->kind == SK_RAISE_SKELETON ? PK_SKELETON : s->kind == SK_RAISE_MAGE ? PK_MAGE
             : s->kind == SK_REVIVE        ? PK_REVIVE   : PK_GOLEM;
    if (kind == PK_GOLEM) /* un gólem a la vez, de cualquier tipo */
        for (int k = 0; k < NEC_PETS; k++)
            if (p->pet_kind[k] == PK_GOLEM)
                nec_pet_drop(p, k);
    int slot = nec_pet_slot(p, kind, most);
    if (slot < 0)
        return 0;
    int u = -1;
    switch (s->kind) {
    case SK_RAISE_SKELETON:
        u = units_pet_summon(me, s->summon, x, y, &ps);
        break;
    case SK_RAISE_MAGE: { /* uno de los cuatro: veneno, frío, fuego o rayo; el nivel de su proyectil es sumsk1calc */
        static const char *const k_mis[4] = { "necromage1", "necromage2", "necromage3", "necromage4" };
        static const int k_elem[4] = { ELEM_POIS, ELEM_COLD, ELEM_FIRE, ELEM_LTNG };
        int e = (int)(rnd() % 4), mlvl = s->sumskcalc[0] ? skill_eval(s, s->sumskcalc, lvl, &c) : lvl;
        snprintf(ps.missile, sizeof ps.missile, "%s", k_mis[e]);
        ps.elem = k_elem[e];
        missiles_level_damage(k_mis[e], mlvl > 0 ? mlvl : 1, &ps.emin, &ps.emax);
        if (ps.emax < ps.emin)
            ps.emax = ps.emin;
        if (ps.elem == ELEM_POIS) { /* en 1/256 por cuadro durante 4 segundos */
            ps.elen = 100;
            ps.emin = ps.emin * ps.elen / 256 > 0 ? ps.emin * ps.elen / 256 : 1;
            ps.emax = ps.emax * ps.elen / 256 > ps.emin ? ps.emax * ps.elen / 256 : ps.emin;
        } else if (ps.elem == ELEM_COLD) {
            ps.elen = 25;
        }
        u = units_pet_summon(me, s->summon, x, y, &ps);
        break;
    }
    case SK_REVIVE: /* el mismo monstruo: con más vida, más daño y más rápido, por calc2 cuadros */
        ps.ticks = skill_calc(s, SC_CALC2, lvl, &c);
        if (!units_revive(corpse, me, &ps)) {
            show_message(p, "Ese no se puede revivir");
            return 0;
        }
        u = corpse;
        missiles_effect("revivemedium", x, y);
        break;
    case SK_BLOOD_GOLEM:
        ps.steal = skill_calc(s, SC_CALC2, lvl, &c);
        ps.steal_owner = skill_calc(s, SC_CALC3, lvl, &c);
        u = units_pet_summon(me, s->summon, x + 1.0f, y + 1.0f, &ps);
        break;
    case SK_IRON_GOLEM: { /* del arma o armadura del piso (Espinas: item_attackertakesdamage) */
        float ix, iy;
        if (p->cast_target < 0 || !items_ground_item(p->cast_target, &ix, &iy))
            return 0;
        items_ground_remove(p->cast_target);
        u = units_pet_summon(me, s->summon, ix, iy, &ps);
        break;
    }
    case SK_FIRE_GOLEM: /* sus golpes con el fuego de la habilidad (EMin..EMax con ELev) */
        skill_elem(s, lvl, &ps.fire_min, &ps.fire_max);
        u = units_pet_summon(me, s->summon, x + 1.0f, y + 1.0f, &ps);
        break;
    default: /* el gólem de arcilla */
        u = units_pet_summon(me, s->summon, x + 1.0f, y + 1.0f, &ps);
        break;
    }
    if (u < 0)
        return 0;
    if (s->kind == SK_RAISE_SKELETON || s->kind == SK_RAISE_MAGE) {
        units_consume_corpse(corpse);
        missiles_effect("corpseexplosion", x, y);
    }
    nec_pet_keep(p, slot, u, kind);
    if (kind == PK_GOLEM) {
        p->golem_fire = s->kind == SK_FIRE_GOLEM ? lvl : 0;
        p->golem_next = NEC_GOLEM_PULSE;
    }
    return 1;
}

/* Un lugar con un jugador o un monstruo encima */
static int nec_spot_taken(float x, float y)
{
    for (int i = 0; i < s_num_players; i++)
        if ((s_pl[i].x - x) * (s_pl[i].x - x) + (s_pl[i].y - y) * (s_pl[i].y - y) < 1.0f)
            return 1;
    int ids[2];
    return units_enemies_near(x, y, 1.0f, ids, 2) > 0;
}

/* Muro de hueso (de costado, delante) y Prisión de hueso (alrededor del monstruo) */
static int nec_bones(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    PetSpec ps;
    pet_spec(p, s, lvl, &ps);
    ps.level = 0; /* el de monstats.txt */
    ps.wall = 1;
    ps.ticks = s->param[2] > 0 ? s->param[2] : 600; /* Param2: la duración */
    float tx = p->cast_x, ty = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    float pos[24][2];
    int n = 0;
    if (s->kind == SK_BONE_PRISON) { /* los 12 lugares del original alrededor */
        static const int k_x[12] = { -1, 1, 3, 4, 4, 3, -1, 1, -3, -4, -4, -3 };
        static const int k_y[12] = { -4, -4, -3, -1, 1, 3, 4, 4, 3, -1, 1, -3 };
        for (int k = 0; k < 12; k++) {
            pos[n][0] = tx + (float)k_x[k];
            pos[n][1] = ty + (float)k_y[k];
            n++;
        }
    } else { /* un tramo de costado: el centro y calc2/2 de cada lado */
        if (!units_alive_enemy(p->target)) { /* sin monstruo, a 4 subtiles adelante */
            tx = p->x + (p->cast_x - p->x) * 4.0f / SPELL_AHEAD;
            ty = p->y + (p->cast_y - p->y) * 4.0f / SPELL_AHEAD;
        } else { /* entre él y el monstruo */
            tx = (tx + p->x) / 2.0f;
            ty = (ty + p->y) / 2.0f;
        }
        float dx = tx - p->x, dy = ty - p->y, l = sqrtf(dx * dx + dy * dy);
        if (l < 0.01f) {
            dx = 1.0f;
            l = 1.0f;
        }
        float sx = -dy / l, sy = dx / l;
        int side = skill_calc(s, SC_CALC2, lvl, &c) / 2;
        side = side < 1 ? 1 : side > 10 ? 10 : side;
        for (int k = -side; k <= side && n < 24; k++) {
            pos[n][0] = tx + sx * (float)k * 1.1f;
            pos[n][1] = ty + sy * (float)k * 1.1f;
            n++;
        }
    }
    int made = 0;
    for (int k = 0; k < n; k++) {
        if (nec_spot_taken(pos[k][0], pos[k][1])) /* ahí hay alguien: ese pedazo no va */
            continue;
        int slot = nec_pet_slot(p, PK_WALL, 0);
        if (slot < 0)
            break;
        int u = units_pet_summon((int)(p - s_pl), s->summon, pos[k][0], pos[k][1], &ps);
        if (u < 0)
            continue;
        nec_pet_keep(p, slot, u, PK_WALL);
        made++;
    }
    return made;
}

/* Explosión de cadáver: del calc1 al calc2 % de la vida del muerto (menos si
   el monstruo era de más nivel), calc3 % de fuego, a hasta aurarangecalc / 2 */
static int corpse_explosion(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int corpse = p->cast_target;
    if (!nec_corpse_ok(corpse))
        return 0;
    float x, y;
    units_position(corpse, 1.0f, &x, &y);
    int hp, max;
    units_health(corpse, &hp, &max);
    int lo = max * skill_calc(s, SC_CALC1, lvl, &c) / 100, hi = max * skill_calc(s, SC_CALC2, lvl, &c) / 100;
    int dmg = roll(lo, hi), mlvl = units_level(corpse);
    if (mlvl > p->level_num)
        dmg = dmg * p->level_num / mlvl;
    int fire_pct = skill_calc(s, SC_CALC3, lvl, &c);
    fire_pct = fire_pct < 0 ? 0 : fire_pct > 100 ? 100 : fire_pct;
    int fire = dmg * fire_pct / 100, phys = dmg - fire;
    units_consume_corpse(corpse);
    missiles_effect("corpseexplosion", x, y);
    audio_play_name("necromancer_corpseexp_1", x, y);
    float r = (float)(skill_calc(s, SC_AURARANGE, lvl, &c) + 1) / 2.0f;
    int ids[64], n = units_enemies_near(x, y, r, ids, 64);
    for (int k = 0; k < n; k++) {
        int d = units_resist(ids[k], ELEM_NONE, phys) + units_resist(ids[k], ELEM_FIRE, fire);
        int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : d);
        if (exp > 0)
            award_kill(p, ids[k], exp);
    }
    return n + 1;
}

/* Explosión venenosa: una nube de veneno en el cadáver */
static int poison_explosion(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int corpse = p->cast_target;
    if (!nec_corpse_ok(corpse))
        return 0;
    float x, y;
    units_position(corpse, 1.0f, &x, &y);
    int lo, hi;
    skill_poison_syn(s, lvl, &c, &lo, &hi);
    units_consume_corpse(corpse);
    missiles_effect("poisoncorpseexplosion", x, y);
    audio_play_name("necromancer_corpseexp_1", x, y);
    for (int k = 0; k < 8; k++) /* las nubes alrededor (poisonexplosioncloud) */
        missiles_effect("poisonexplosioncloud", x + 1.5f * cosf((float)k * 0.785f), y + 1.5f * sinf((float)k * 0.785f));
    int ids[64], n = units_enemies_near(x, y, 3.0f, ids, 64);
    for (int k = 0; k < n; k++)
        if (units_poison(ids[k], roll(lo, hi), skill_elen(s, lvl), (int)(p - s_pl)))
            poison_fx(ids[k]);
    return n + 1;
}

/* ---- El efecto (en el cuadro del lanzamiento) ---- */

static int nec_effect(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int lo, hi;
    float ax, ay;
    if (nec_curse_kind(s->kind))
        return nec_curse(p, s, lvl);
    switch (s->kind) {
    case SK_TEETH: { /* calc1 dientes en abanico */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int n = skill_calc(s, SC_CALC1, lvl, &c);
        n = n < 1 ? 1 : n > 24 ? 24 : n;
        float base = atan2f(ay, ax), spread = (float)n * 0.12f > 2.4f ? 2.4f : (float)n * 0.12f;
        for (int k = 0; k < n; k++) {
            float a = n > 1 ? base - spread / 2.0f + spread * (float)k / (float)(n - 1) : base;
            if (!spell_missile(p, "teeth", p->x, p->y, cosf(a), sinf(a), ELEM_MAG, lo, hi, 0))
                break;
        }
        return n;
    }
    case SK_BONE_SPEAR: { /* atraviesa */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        Missile *mi = spell_missile(p, "bonespear", p->x, p->y, ax, ay, ELEM_MAG, lo, hi, 0);
        if (mi)
            mi->flags |= MF_PIERCE;
        return mi != NULL;
    }
    case SK_BONE_SPIRIT: { /* busca al monstruo */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        Missile *mi = spell_missile(p, "bonespirit", p->x, p->y, ax, ay, ELEM_MAG, lo, hi, 0);
        if (mi)
            mi->flags |= MF_HOMING;
        return mi != NULL;
    }
    case SK_POISON_NOVA: /* 64 en ronda, cada monstruo se envenena una vez */
        skill_poison_syn(s, lvl, &c, &lo, &hi);
        nova_ring(p, "poisonnova", p->x, p->y, 1, ELEM_POIS, lo, hi, skill_elen(s, lvl));
        return 1;
    case SK_BONE_ARMOR: /* absorbe hasta aurastat1 (en 1/256) de daño cuerpo a cuerpo */
        p->bone_armor = p->bone_armor_max = skill_calc(s, SC_AURASTAT1, lvl, &c) / 256;
        overlays_add("bonearmor_cast", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
        return 1;
    case SK_CORPSE_EXPLOSION:
        return corpse_explosion(p, s, lvl);
    case SK_POISON_EXPLOSION:
        return poison_explosion(p, s, lvl);
    case SK_BONE_WALL:
    case SK_BONE_PRISON:
        return nec_bones(p, s, lvl);
    case SK_RAISE_SKELETON:
    case SK_RAISE_MAGE:
    case SK_REVIVE:
    case SK_CLAY_GOLEM:
    case SK_BLOOD_GOLEM:
    case SK_IRON_GOLEM:
    case SK_FIRE_GOLEM:
        return nec_summon(p, s, lvl);
    default:
        return 0;
    }
}

/* ---- Lo que dura ---- */

/* La Armadura de hueso se lleva el daño cuerpo a cuerpo; devuelve lo que queda */
static int bone_armor_absorb(Player *p, int dmg)
{
    if (p->bone_armor <= 0 || dmg <= 0)
        return dmg;
    int take = dmg < p->bone_armor ? dmg : p->bone_armor;
    p->bone_armor -= take;
    return dmg - take;
}

static void nec_tick(Player *p)
{
    int me = (int)(p - s_pl);
    if (me == 0)
        nec_cursed_tick();
    nec_pets_tick(p);
    if (p->bone_armor > 0) { /* se ve mientras le quede */
        overlays_add("bonearmor_front", OV_AT_PLAYER, me, p->x, p->y, 4);
        overlays_add("bonearmor_back", OV_AT_PLAYER, me, p->x, p->y, 4);
    }
    /* El gólem de fuego: Fuego sagrado (de nivel sumsk1calc) cada 2 segundos */
    if (p->golem_fire > 0 && --p->golem_next <= 0) {
        p->golem_next = NEC_GOLEM_PULSE;
        int g = -1;
        for (int k = 0; k < NEC_PETS && g < 0; k++)
            if (p->pet_kind[k] == PK_GOLEM && units_pet_alive(p->pets[k]))
                g = p->pets[k];
        int fg = skills_find(p->cls, "FireGolem"), hf = skills_find(3, "Holy Fire");
        if (g < 0 || fg < 0 || hf < 0) {
            p->golem_fire = 0;
            return;
        }
        SkillCtx c = pctx(p);
        const SkillDef *fs = skills_get(p->cls, fg), *hs = skills_get(3, hf);
        int alvl = fs->sumskcalc[0] ? skill_eval(fs, fs->sumskcalc, p->golem_fire, &c) : p->golem_fire, lo, hi;
        skill_elem(hs, alvl > 0 ? alvl : 1, &lo, &hi);
        float gx, gy, r = (float)(hs->param[1] + (alvl - 1) * hs->param[2]);
        units_position(g, 1.0f, &gx, &gy);
        int ids[32], n = units_enemies_near(gx, gy, r, ids, 32);
        for (int k = 0; k < n; k++) {
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            overlays_add("hit_holyfire", OV_AT_UNIT, ids[k], ux, uy, 0);
            int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : units_resist(ids[k], ELEM_FIRE, roll(lo, hi)));
            if (exp > 0)
                award_kill(p, ids[k], exp);
        }
        overlays_add("aura_holyfire_back", OV_AT_UNIT, g, gx, gy, NEC_GOLEM_PULSE + 2);
    }
}

/* Después de un golpe: Daga venenosa */
static void nec_after_hit(Player *p, const SkillDef *s, int lvl, int target, int killed)
{
    if (s->kind != SK_POISON_DAGGER || killed)
        return;
    SkillCtx c = pctx(p);
    int lo, hi;
    skill_poison_syn(s, lvl, &c, &lo, &hi);
    if (units_poison(target, roll(lo, hi), skill_elen(s, lvl), (int)(p - s_pl)))
        poison_fx(target);
}
