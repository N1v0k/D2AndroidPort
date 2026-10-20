/* ---- Las habilidades de la Asesina (se incluye desde game.c) ----

   Como en el original (SkillAss.cpp de D2MOO), con las tablas de D2R:
   - Artes marciales de carga (Golpe del tigre, de la cobra y real, Puños
     de fuego, Garras del trueno, Cuchillas de hielo): cada golpe que acierta
     suma una carga (hasta 3; duran auralencalc, 15 segundos) y +par4% de
     puntería por carga. Las de garra usan la secuencia de Garra del dragón:
     con dos garras pegan con las dos. Un golpe final que acierta (Talón,
     Garra, Cola o Vuelo del dragón) libera todas las cargas sobre el
     monstruo: el tigre suma calc1% de daño por carga a ese golpe; la cobra
     roba vida (y con 2, maná; con 3, el doble); fuego, rayo y hielo suman
     su daño elemental, con 2 una explosión alrededor (la del rayo, una nova)
     y con 3 fuego en el piso, hielo que congela o centellas; el golpe real,
     según las cargas, un meteoro, rayos en ronda o hielo alrededor.
   - Golpes finales: patadas con el daño de las botas (Talón del dragón da
     lvl/6+1 y empuja con la última), Garra del dragón con las dos garras,
     Cola del dragón explota en fuego (calc1% del daño físico) y Vuelo del
     dragón salta al lado del monstruo y patea.
   - Trampas (hasta 5): centinelas que disparan sus tiros y desaparecen
     (centellas, estela de fuego, relámpagos, llamas, y el de la muerte, que
     hace explotar cadáveres); Trauma de fuego y Campo de choque se tiran;
     el Centinela de cuchillas va y viene entre ella y el monstruo; Furia de
     cuchillas las tira seguidas y el Escudo de cuchillas corta a los que
     están al lado.
   - Sombras: Martillo psíquico (empuja), Rapidez y Desvanecerse
     (velocidad, resistencias), Manto de sombras (ciega y baja la defensa),
     Explosión mental (aturde y a veces convierte), Veneno en los golpes, y
     el Guerrero y el Maestro de las sombras: un clon oscuro con su equipo. */

#define ASS_TRAPS 5
enum { PRG_TIGER, PRG_COBRA, PRG_FIRE, PRG_THUNDER, PRG_ICE, PRG_ROYAL, PRG_COUNT };

static int ass_kind(int kind)
{
    return kind >= SK_FIRE_TRAUMA && kind <= SK_ROYAL_STRIKE;
}

static int prg_index(int kind)
{
    switch (kind) {
    case SK_TIGER_STRIKE:   return PRG_TIGER;
    case SK_COBRA_STRIKE:   return PRG_COBRA;
    case SK_FISTS_OF_FIRE:  return PRG_FIRE;
    case SK_CLAWS_THUNDER:  return PRG_THUNDER;
    case SK_BLADES_ICE:     return PRG_ICE;
    case SK_ROYAL_STRIKE:   return PRG_ROYAL;
    default:                return -1;
    }
}

static int ass_finisher(int kind)
{
    return kind == SK_DRAGON_TALON || kind == SK_DRAGON_CLAW || kind == SK_DRAGON_TAIL || kind == SK_DRAGON_FLIGHT;
}

static int ass_kick(int kind)
{
    return kind == SK_DRAGON_TALON || kind == SK_DRAGON_TAIL || kind == SK_DRAGON_FLIGHT;
}

static int ass_sentry(int kind)
{
    return kind == SK_CB_SENTRY || kind == SK_WAKE_OF_FIRE || kind == SK_LIGHTNING_SENTRY || kind == SK_INFERNO_SENTRY ||
           kind == SK_DEATH_SENTRY;
}

/* Daño de una habilidad a un monstruo (con su resistencia); la experiencia es de ella */
static void ass_damage(Player *p, int unit, int elem, int amount)
{
    if (!units_alive_enemy(unit) || amount <= 0)
        return;
    int exp = units_damage(unit, s_one_hit ? 1 << 20 : spell_resist((int)(p - s_pl), unit, elem, amount));
    if (exp > 0)
        award_kill(p, unit, exp);
}

/* Lo físico de las cuchillas: lo suyo (MinDam..MaxDam) y SrcDam/128 del arma */
static void blade_damage(const Player *p, const SkillDef *s, int lvl, int *lo, int *hi)
{
    SkillCtx c = pctx(p);
    int wlo, whi;
    skill_phys_syn(s, lvl, &c, lo, hi);
    weapon_damage(p, 0, &wlo, &whi);
    int src = s->srcdam > 0 ? s->srcdam : 128;
    *lo += wlo * src / 128;
    *hi += whi * src / 128;
    if (*hi < *lo)
        *hi = *lo;
}

/* ---- Las cargas ---- */

/* Su dibujo sobre la Asesina (prgoverlay o pgsvoverlay, con 1, 2 o 3) */
static const char *const k_prg_overlay[PRG_COUNT] = { "tigerstrike", "cobrastrike", "progressive_fire_",
                                                      "progressive_lightning_", "progressive_cold_",
                                                      "progressive_other_" };

static void prg_overlay(Player *p, int k)
{
    int me = (int)(p - s_pl);
    overlays_remove(k_prg_overlay[k], OV_AT_PLAYER, me);
    if (p->prg_charges[k] > 0) {
        char name[40];
        snprintf(name, sizeof name, "%s%d", k_prg_overlay[k], p->prg_charges[k]);
        overlays_add(name, OV_AT_PLAYER, me, p->x, p->y, p->prg_ticks[k]);
    }
}

/* La puntería que suman las cargas (progressive_tohit: par4% por carga) */
static void prg_refresh(Player *p)
{
    SkillCtx c = pctx(p);
    int ar = 0;
    for (int k = 0; k < PRG_COUNT; k++)
        if (p->prg_charges[k] > 0)
            ar += p->prg_charges[k] * skill_calc(skills_get(p->cls, p->prg_skill[k]), SC_AURASTAT2, p->prg_lvl[k], &c);
    p->prg_ar = ar;
}

static void prg_add(Player *p, const SkillDef *s, int lvl)
{
    int k = prg_index(s->kind);
    if (k < 0)
        return;
    SkillCtx c = pctx(p);
    if (p->prg_charges[k] < 3)
        p->prg_charges[k]++;
    int len = skill_calc(s, SC_AURALEN, lvl, &c);
    p->prg_ticks[k] = len > 0 ? len : 375;
    p->prg_lvl[k] = lvl;
    p->prg_skill[k] = (int)(s - skills_get(p->cls, 0));
    prg_overlay(p, k);
    prg_refresh(p);
    if (k == PRG_TIGER)
        audio_play_name("assassin_chargeup_tiger_1", p->x, p->y);
    else if (k == PRG_COBRA)
        audio_play_name("assassin_chargeup_cobra_1", p->x, p->y);
}

/* El % de daño de más de un golpe final (y lo del tigre, que se suma a ese golpe) */
static int ass_strike_pct(const Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int pct;
    switch (s->kind) {
    case SK_DRAGON_TALON: /* "On a hit, damage percent is dwParam[0] + (nSkillLevel - 1) * dwParam[1]" */
        pct = s->param[1] + (lvl - 1) * s->param[2];
        break;
    case SK_DRAGON_CLAW:
    case SK_DRAGON_TAIL:
    case SK_DRAGON_FLIGHT:
        pct = skill_calc(s, SC_CALC1, lvl, &c);
        break;
    default:
        return 0;
    }
    if (p->prg_charges[PRG_TIGER] > 0)
        pct += p->prg_charges[PRG_TIGER] *
               skill_calc(skills_get(p->cls, p->prg_skill[PRG_TIGER]), SC_CALC1, p->prg_lvl[PRG_TIGER], &c);
    return pct;
}

/* Las patadas: el daño de las botas (su kick damage) con lo que suma la fuerza */
static void kick_damage(const Player *p, int extra_pct, int *dmin, int *dmax)
{
    const ItemType *b = p->equip[SLOT_BOOTS].type;
    int lo = b && b->smite_max > 0 ? b->smite_min : 1, hi = b && b->smite_max > 0 ? b->smite_max : 2;
    int bonus = b && b->str_bonus ? player_str(p) * b->str_bonus / 100 : player_str(p);
    int pct = 100 + bonus + p->au_dmg_pct + extra_pct;
    if (pct < 1)
        pct = 1;
    *dmin = lo * pct / 100;
    *dmax = hi * pct / 100;
    if (*dmax < *dmin)
        *dmax = *dmin;
}

/* Proyectiles en ronda desde un lugar, `n` en total */
static void ass_ring(Player *p, const char *name, float x, float y, int n, int elem, int lo, int hi, int flags)
{
    int group = hit_group_new();
    n = n < 1 ? 1 : n > 64 ? 64 : n;
    for (int k = 0; k < n; k++) {
        float a = (float)k * 2.0f * PI_F / (float)n;
        Missile *mi = spell_missile(p, name, x, y, cosf(a), sinf(a), elem, lo, hi, 0);
        if (!mi)
            break;
        mi->flags |= MF_GROUP | flags;
        mi->group = group;
    }
}

/* Un golpe final acertó: se liberan todas las cargas sobre el monstruo */
static void prg_release(Player *p, int target, int phys)
{
    float ux, uy;
    units_position(target, 1.0f, &ux, &uy);
    SkillCtx c = pctx(p);
    for (int k = 0; k < PRG_COUNT; k++) {
        int n = p->prg_charges[k];
        if (n <= 0)
            continue;
        const SkillDef *s = skills_get(p->cls, p->prg_skill[k]);
        int lvl = p->prg_lvl[k], lo, hi;
        if (s_debug)
            printf("  J%d libera %d carga%s de %s\n", (int)(p - s_pl) + 1, n, n > 1 ? "s" : "", s->key);
        switch (k) {
        case PRG_TIGER: /* (ya sumó al daño del golpe) */
            overlays_add("assassinfootimpact", OV_AT_UNIT, target, ux, uy, 0);
            break;
        case PRG_COBRA: { /* roba vida; con 2, también maná; con 3, el doble */
            int heal = phys * (s->param[1] + (lvl - 1) * s->param[2]) / 100;
            if (n == 3)
                heal *= 2;
            p->hp += heal;
            if (n >= 2)
                p->mana += heal;
            break;
        }
        case PRG_FIRE:
        case PRG_ICE:
        case PRG_THUNDER: {
            int elem = k == PRG_FIRE ? ELEM_FIRE : k == PRG_ICE ? ELEM_COLD : ELEM_LTNG;
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            int conv = k == PRG_FIRE ? skill_calc(s, SC_CALC1, lvl, &c) : 0; /* Puños: un % del golpe pasa a fuego */
            conv = conv < 0 ? 0 : conv > 100 ? 100 : conv;
            ass_damage(p, target, elem, roll(lo, hi) + phys * conv / 100);
            int elen = skill_elen(s, lvl);
            if (k == PRG_ICE && units_alive_enemy(target) && elen > 0) {
                if (n == 3)
                    units_freeze(target, elen); /* con 3, congela */
                else
                    units_chill(target, elen);
            }
            if (k == PRG_THUNDER) {
                if (n >= 2) { /* una nova desde el monstruo */
                    missiles_level_damage("clawsofthundernova", lvl, &lo, &hi);
                    ass_ring(p, "clawsofthundernova", ux, uy, 32, ELEM_LTNG, lo, hi, MF_PIERCE);
                }
                if (n >= 3) { /* centellas en ronda: 64 / par1 */
                    missiles_level_damage("clawsofthunderbolt", lvl, &lo, &hi);
                    ass_ring(p, "clawsofthunderbolt", ux, uy, 64 / (s->param[1] > 0 ? s->param[1] : 4), ELEM_LTNG, lo,
                             hi, MF_CHARGED);
                }
                break;
            }
            if (n >= 2) { /* una explosión alrededor (radio prgcalc2 = par1): físico y elemental */
                float r = (float)s->param[1] / 2.0f + 1.0f;
                missiles_effect(k == PRG_FIRE ? "fistsoffireexplode" : "bladesoficeexplode", ux, uy);
                int ids[32], m = units_enemies_near(ux, uy, r, ids, 32);
                for (int i = 0; i < m; i++) {
                    if (ids[i] == target)
                        continue;
                    ass_damage(p, ids[i], ELEM_NONE, phys / 2);
                    ass_damage(p, ids[i], elem, roll(lo, hi));
                    if (k == PRG_ICE && units_alive_enemy(ids[i]) && elen > 0)
                        units_chill(ids[i], elen);
                }
            }
            if (n >= 3) { /* fuego en el piso o hielo que congela, al azar en el radio prgcalc3 = par2 */
                float r = (float)s->param[2] / 2.0f + 1.0f;
                for (int i = 0; i < 8; i++) {
                    float a = (float)(rnd() % 628) / 100.0f, d = r * (float)(rnd() % 100) / 100.0f;
                    float x = ux + cosf(a) * d, y = uy + sinf(a) * d;
                    Missile *f = missiles_effect(k == PRG_FIRE ? "fistsoffirefirewall" : "bladesoficecubes", x, y);
                    if (!f)
                        break;
                    if (k == PRG_FIRE) { /* quema al que lo pisa (por segundo) */
                        f->flags |= MF_BURN;
                        f->owner = (int)(p - s_pl);
                        f->emin = lo;
                        f->emax = hi;
                        f->radius10 = 8;
                    }
                }
                if (k == PRG_ICE) {
                    int ids[32], m = units_enemies_near(ux, uy, r, ids, 32);
                    for (int i = 0; i < m; i++)
                        units_freeze(ids[i], elen > 0 ? elen : 50);
                }
            }
            break;
        }
        case PRG_ROYAL:
            if (n == 1) { /* un meteoro */
                missiles_level_damage("royalstrikemeteor", lvl, &lo, &hi);
                missiles_effect("royalstrikemeteorcenter", ux, uy);
                Missile *m = spell_missile(p, "royalstrikemeteor", ux, uy, 0.0f, 0.0f, ELEM_FIRE, lo, hi, 0);
                if (m) {
                    m->flags |= MF_EFFECT | MF_LAND | MF_NOHIT;
                    m->radius10 = 25;
                }
            } else if (n == 2) { /* rayos en ronda: 64 / aurarange */
                missiles_level_damage("royalstrikechainlightning", lvl, &lo, &hi);
                int r = skill_calc(s, SC_AURARANGE, lvl, &c);
                ass_ring(p, "royalstrikechainlightning", ux, uy, r > 0 ? 64 / r : 8, ELEM_LTNG, lo, hi, MF_PIERCE);
            } else { /* hielo alrededor: prgcalc3 = par5 */
                missiles_level_damage("royalstrikechaosice", lvl, &lo, &hi);
                int count = s->param[5] > 0 ? s->param[5] : 16;
                for (int i = 0; i < count && i < 24; i++) {
                    float a = (float)(rnd() % 628) / 100.0f, b = (float)(rnd() % 628) / 100.0f;
                    float x = ux + cosf(a) * 2.0f, y = uy + sinf(a) * 2.0f;
                    if (!spell_missile(p, "royalstrikechaosice", x, y, cosf(b), sinf(b), ELEM_COLD, lo, hi, 25))
                        break;
                }
            }
            break;
        }
        p->prg_charges[k] = 0;
        p->prg_ticks[k] = 0;
        prg_overlay(p, k);
    }
    if (p->hp > p->hp_max)
        p->hp = p->hp_max;
    if (p->mana > p->mana_max)
        p->mana = p->mana_max;
    prg_refresh(p);
}

static int prg_any(const Player *p)
{
    for (int k = 0; k < PRG_COUNT; k++)
        if (p->prg_charges[k] > 0)
            return 1;
    return 0;
}

/* ---- Trampas ---- */

static void trap_drop(Player *p, int k)
{
    if (p->trap_unit[k] >= 0) {
        float x, y;
        units_position(p->trap_unit[k], 1.0f, &x, &y);
        missiles_effect("shadowwarriordeath", x, y);
        units_pet_remove(p->trap_unit[k]);
    }
    p->trap_unit[k] = -1;
}

/* Un lugar para una trampa más: si ya hay 5, se va la más vieja */
static int trap_slot(Player *p)
{
    int oldest = 0;
    for (int k = 0; k < ASS_TRAPS; k++) {
        if (p->trap_unit[k] < 0)
            return k;
        if (!units_pet_alive(p->trap_unit[k])) {
            trap_drop(p, k);
            return k;
        }
        if (p->trap_born[k] < p->trap_born[oldest])
            oldest = k;
    }
    trap_drop(p, oldest);
    return oldest;
}

static int trap_count(const Player *p)
{
    int n = 0;
    for (int k = 0; k < ASS_TRAPS; k++)
        n += p->trap_unit[k] >= 0 && units_pet_alive(p->trap_unit[k]);
    return n;
}

/* Adónde va la trampa: entre ella y el monstruo, o adelante */
static void trap_spot(const Player *p, float *x, float *y)
{
    float tx = p->cast_x, ty = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    *x = p->x + (tx - p->x) * 0.6f;
    *y = p->y + (ty - p->y) * 0.6f;
    find_free_spot(x, y);
}

static int ass_place_sentry(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    float x, y;
    trap_spot(p, &x, &y);
    int k = trap_slot(p);
    int u = units_pet_spawn((int)(p - s_pl), s->summon, x, y, p->level_num, 0, 0, 0);
    if (u < 0)
        return 0;
    /* Cada cuánto dispara y cuántas veces (Param1; las centellas, calc4) */
    int cool = s->kind == SK_INFERNO_SENTRY ? 10 : s->kind == SK_LIGHTNING_SENTRY ? 20 : 25;
    units_pet_turret(u, cool);
    int shots = s->kind == SK_CB_SENTRY ? skill_calc(s, SC_CALC4, lvl, &c) : s->param[1];
    if (s->kind == SK_DEATH_SENTRY) { /* + Trauma de fuego / 3 */
        int ft = skills_find(p->cls, "Fire Trauma");
        shots += ft >= 0 ? p->skill_lvl[ft] / 3 : 0;
    }
    p->trap_unit[k] = u;
    p->trap_skill[k] = (int)(s - skills_get(p->cls, 0));
    p->trap_shots[k] = shots > 0 ? shots : 5;
    p->trap_lvl[k] = lvl;
    p->trap_born[k] = s_ticks;
    missiles_effect("shadowwarriorappear", x, y);
    return 1;
}

/* Un tiro de una trampa (la cola de units_pop_shot); 0 si no es una trampa */
static int ass_trap_shot(int unit, int target)
{
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        for (int k = 0; k < ASS_TRAPS; k++) {
            if (p->trap_unit[k] != unit)
                continue;
            const SkillDef *s = skills_get(p->cls, p->trap_skill[k]);
            int lvl = p->trap_lvl[k], lo, hi;
            SkillCtx c = pctx(p);
            float x, y, tx, ty;
            units_position(unit, 1.0f, &x, &y);
            units_position(target, 1.0f, &tx, &ty);
            float dx = tx - x, dy = ty - y, base = atan2f(dy, dx);
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            switch (s->kind) {
            case SK_CB_SENTRY: { /* centellas: ln12 + Campo de choque / par6 */
                int sf = skills_find(p->cls, "Shock Field");
                int n = 5 + (sf >= 0 && s->param[6] > 0 ? p->skill_lvl[sf] / s->param[6] : 0);
                for (int b = 0; b < n && b < 12; b++) {
                    float a = base + ((float)(rnd() % 100) / 100.0f - 0.5f) * 1.2f;
                    Missile *mi = spell_missile(p, "sentrychargedbolt", x, y, cosf(a), sinf(a), ELEM_LTNG, lo, hi, 0);
                    if (!mi)
                        break;
                    mi->flags |= MF_CHARGED;
                    mi->life = 40;
                }
                break;
            }
            case SK_LIGHTNING_SENTRY: { /* un relámpago que atraviesa */
                Missile *mi = spell_missile(p, "chainlightning", x, y, dx, dy, ELEM_LTNG, lo, hi, 0);
                if (mi)
                    mi->flags |= MF_PIERCE;
                break;
            }
            case SK_WAKE_OF_FIRE: { /* una ola de fuego en abanico */
                int group = hit_group_new();
                for (int b = -3; b <= 3; b++) {
                    float a = base + (float)b * 0.15f;
                    Missile *mi = spell_missile(p, "wake of destruction", x, y, cosf(a), sinf(a), ELEM_FIRE, lo, hi, 0);
                    if (!mi)
                        break;
                    mi->flags |= MF_GROUP | MF_PIERCE;
                    mi->group = group;
                }
                break;
            }
            case SK_INFERNO_SENTRY: { /* llamas: el daño por segundo de un tercio de segundo */
                skill_elem_rate(s, lvl, &c, 8, &lo, &hi);
                int group = hit_group_new();
                for (int b = 0; b < 3; b++) {
                    Missile *mi = spell_missile(p, b % 2 ? "inferno sentry 2" : "inferno sentry 1", x, y, dx, dy, ELEM_FIRE,
                                                lo, hi, 0);
                    if (!mi)
                        break;
                    mi->flags |= MF_GROUP | MF_PIERCE;
                    mi->group = group;
                    mi->life = 14 + b * 3;
                }
                break;
            }
            case SK_DEATH_SENTRY: { /* hace explotar los cadáveres de alrededor y tira un relámpago */
                int ids[16], n = units_corpses_near(x, y, 10.0f, ids, 16);
                for (int b = 0; b < n && b < 3; b++) {
                    float cx, cy;
                    int hp, mhp;
                    units_position(ids[b], 1.0f, &cx, &cy);
                    units_health(ids[b], &hp, &mhp);
                    int dmg = mhp * roll(40, 80) / 100; /* calc1..calc2 de mon death sentry, mitad fuego */
                    units_consume_corpse(ids[b]);
                    missiles_effect("deathsentryexplode", cx, cy);
                    audio_play_name("necromancer_corpseexp_1", cx, cy);
                    int en[32], m = units_enemies_near(cx, cy, 3.0f, en, 32);
                    for (int e = 0; e < m; e++) {
                        ass_damage(p, en[e], ELEM_NONE, dmg / 2);
                        ass_damage(p, en[e], ELEM_FIRE, dmg - dmg / 2);
                    }
                }
                Missile *mi = spell_missile(p, "chainlightning", x, y, dx, dy, ELEM_LTNG, lo, hi, 0);
                if (mi)
                    mi->flags |= MF_PIERCE;
                break;
            }
            default:
                break;
            }
            if (--p->trap_shots[k] <= 0)
                trap_drop(p, k);
            return 1;
        }
    }
    return 0;
}

/* El Centinela de cuchillas: va y viene entre donde se tiró y el monstruo */
static int ass_sentinel(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    float tx = p->cast_x, ty = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    if (p->sent_unit >= 0)
        units_pet_remove(p->sent_unit);
    int u = units_pet_spawn((int)(p - s_pl), s->summon, p->x, p->y, p->level_num, 0, 0, 0);
    if (u < 0)
        return 0;
    units_pet_turret(u, 1 << 20); /* no dispara: corta al pasar */
    p->sent_unit = u;
    p->sent_x0 = p->x;
    p->sent_y0 = p->y;
    p->sent_x1 = tx;
    p->sent_y1 = ty;
    p->sent_t = 0.0f;
    p->sent_dir = 1;
    int len = skill_calc(s, SC_CALC4, lvl, &c);
    p->sent_ticks = len > 0 ? len : 100;
    blade_damage(p, s, lvl, &p->sent_lo, &p->sent_hi);
    return 1;
}

static void sentinel_tick(Player *p)
{
    if (p->sent_unit < 0)
        return;
    if (--p->sent_ticks <= 0 || !units_pet_alive(p->sent_unit)) {
        units_pet_remove(p->sent_unit);
        p->sent_unit = -1;
        return;
    }
    float dx = p->sent_x1 - p->sent_x0, dy = p->sent_y1 - p->sent_y0, len = sqrtf(dx * dx + dy * dy);
    float step = len > 0.5f ? 0.3f / len : 1.0f;
    p->sent_t += step * (float)p->sent_dir;
    if (p->sent_t >= 1.0f || p->sent_t <= 0.0f) {
        p->sent_t = p->sent_t >= 1.0f ? 1.0f : 0.0f;
        p->sent_dir = -p->sent_dir;
    }
    float x = p->sent_x0 + dx * p->sent_t, y = p->sent_y0 + dy * p->sent_t;
    units_pet_place(p->sent_unit, x, y);
    if (s_ticks % 6 == 0) { /* corta a los que toca */
        int ids[8], n = units_enemies_near(x, y, 1.2f, ids, 8);
        for (int k = 0; k < n; k++)
            ass_damage(p, ids[k], ELEM_NONE, roll(p->sent_lo, p->sent_hi));
    }
}

/* ---- Al lanzarla ---- */

/* 1 si arrancó (gasta el maná) */
static int ass_start(Player *p, int skill, int cost)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    SkillCtx c = pctx(p);
    int in_town = p->level && p->level->town;
    p->cast_target = -1;
    switch (s->kind) {
    case SK_TIGER_STRIKE:
    case SK_COBRA_STRIKE:
    case SK_ROYAL_STRIKE:
        start_attack(p, skill);
        p->mana -= cost;
        return 1;
    case SK_FISTS_OF_FIRE:
    case SK_CLAWS_THUNDER:
    case SK_BLADES_ICE:
    case SK_DRAGON_CLAW: /* la secuencia de Garra del dragón: con dos garras pega con las dos */
        p->target = units_find_enemy(p->x, p->y, MELEE_RANGE);
        if (p->target >= 0)
            face_unit(p, p->target);
        if (s->seqnum > 0 && start_seq(p, skill))
            play_skill_sound(p, s);
        else
            start_attack(p, skill);
        p->mana -= cost;
        return 1;
    case SK_DRAGON_TALON: /* lvl/6+1 patadas */
        p->target = units_find_enemy(p->x, p->y, MELEE_RANGE);
        if (p->target >= 0)
            face_unit(p, p->target);
        p->kicks_left = skill_calc(s, SC_CALC1, lvl, &c);
        if (p->kicks_left < 1)
            p->kicks_left = 1;
        if (s->seqnum > 0 && player_anim(p, MODE_KK)->ok && start_seq(p, skill)) {
            play_skill_sound(p, s);
        } else {
            start_attack(p, skill);
            p->kicks_left = 0;
        }
        p->mana -= cost;
        return 1;
    case SK_DRAGON_TAIL: /* una patada */
        start_attack(p, skill);
        if (player_anim(p, MODE_KK)->ok) {
            p->mode = MODE_KK;
            p->anim_acc = 0;
        }
        p->mana -= cost;
        return 1;
    case SK_DRAGON_FLIGHT: { /* salta al lado del monstruo (hasta aurarange) y patea */
        float range = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
        pal_aim(p, range > 0.0f && range < AIM_RANGE ? range : AIM_RANGE);
        if (p->target < 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        if (in_town) {
            show_message(p, "En el pueblo no se pelea");
            return 0;
        }
        if (!(s->seqnum > 0 && player_anim(p, MODE_KK)->ok && start_seq(p, skill))) {
            show_message(p, "No se puede con esta arma");
            return 0;
        }
        play_skill_sound(p, s);
        p->mana -= cost;
        return 1;
    }
    case SK_BLADE_FURY: /* las tira seguidas (prgcalc1 = par4) mientras alcance el maná */
        pal_aim(p, AIM_RANGE);
        if (in_town) {
            show_message(p, "En el pueblo no se pelea");
            return 0;
        }
        p->fury_left = s->param[4] > 0 ? s->param[4] : 5;
        if (!(s->seqnum > 0 && start_seq(p, skill))) {
            p->attack_skill = skill;
            set_player_state(p, PS_CAST, MODE_SC);
        }
        play_skill_sound(p, s);
        p->mana -= cost;
        return 1;
    case SK_PSYCHIC_HAMMER:
        pal_aim(p, AIM_RANGE);
        if (p->target < 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        break;
    default:
        pal_aim(p, AIM_RANGE);
        break;
    }
    int combat = s->kind != SK_QUICKNESS && s->kind != SK_FADE && s->kind != SK_VENOM && s->kind != SK_BLADE_SHIELD &&
                 s->kind != SK_SHADOW_WARRIOR && s->kind != SK_SHADOW_MASTER;
    if (in_town && combat) {
        show_message(p, "En el pueblo no se pelea");
        return 0;
    }
    p->attack_skill = skill;
    p->hits_done = 0;
    play_skill_sound(p, s);
    p->mana -= cost;
    int mode = strcmp(s->anim, "S2") == 0 && player_anim(p, MODE_S2)->ok ? MODE_S2 : MODE_SC;
    if (!player_anim(p, mode)->ok) {
        cast_effect(p);
        return 1;
    }
    set_player_state(p, PS_CAST, mode);
    return 1;
}

/* ---- El efecto (en el cuadro del lanzamiento) ---- */

static int ass_effect(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl), lo, hi;
    float tx = p->cast_x, ty = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    if (ass_sentry(s->kind))
        return ass_place_sentry(p, s, lvl);
    switch (s->kind) {
    case SK_FIRE_TRAUMA: { /* una bomba que cae donde está el monstruo y explota (radio aurarange) */
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        float dx = tx - p->x, dy = ty - p->y, d = sqrtf(dx * dx + dy * dy);
        Missile *mi = spell_missile(p, "bomb in air", p->x, p->y, dx, dy, ELEM_FIRE, lo, hi, 0);
        if (!mi)
            return 0;
        float v = sqrtf(mi->vx * mi->vx + mi->vy * mi->vy);
        mi->life = v > 0.0f ? (int)(d / v) + 1 : 1;
        mi->flags |= MF_NOHIT | MF_EXPLODE; /* pasa por arriba y explota donde cae */
        mi->radius10 = (skill_calc(s, SC_AURARANGE, lvl, &c) + 1) * 5;
        return 1;
    }
    case SK_SHOCK_FIELD: { /* rayos en el piso alrededor del monstruo: par1 + lvl/par2 + Trauma/3 */
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int ft = skills_find(p->cls, "Fire Trauma");
        int n = s->param[1] + (s->param[2] > 0 ? lvl / s->param[2] : 0) + (ft >= 0 ? p->skill_lvl[ft] / 3 : 0);
        float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
        r = r < 1.5f ? 1.5f : r > 5.0f ? 5.0f : r;
        Missile *in_air = missiles_effect("shock field in air", p->x, p->y);
        (void)in_air;
        for (int k = 0; k < n && k < 16; k++) {
            float a = (float)(rnd() % 628) / 100.0f, d = r * (float)(rnd() % 100) / 100.0f;
            Missile *f = missiles_effect("shock field on ground", tx + cosf(a) * d, ty + sinf(a) * d);
            if (!f)
                break;
            f->flags |= MF_BURN; /* daño por segundo al que lo pisa */
            f->elem = ELEM_LTNG;
            f->owner = me;
            f->emin = lo;
            f->emax = hi;
            f->radius10 = 8;
        }
        return n;
    }
    case SK_BLADE_SENTINEL:
        return ass_sentinel(p, s, lvl);
    case SK_PSYCHIC_HAMMER: { /* físico y mágico; empuja (calc1 a los comunes, calc2 a los únicos) */
        if (!units_alive_enemy(p->target))
            return 0;
        int plo, phi;
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        overlays_add("psychic_hammer_hit", OV_AT_UNIT, p->target, tx, ty, 0);
        int chance = units_is_unique(p->target) ? skill_calc(s, SC_CALC2, lvl, &c) : skill_calc(s, SC_CALC1, lvl, &c);
        if ((int)(rnd() % 100) < chance)
            units_knockback(p->target, p->x, p->y, 2.5f);
        ass_damage(p, p->target, ELEM_NONE, roll(plo, phi));
        ass_damage(p, p->target, ELEM_MAG, roll(lo, hi));
        return 1;
    }
    case SK_QUICKNESS: /* más velocidad (dm12) y velocidad de ataque (dm34) */
        p->quick_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->quick_speed = skill_calc(s, SC_AURASTAT1, lvl, &c);
        p->quick_ias = skill_calc(s, SC_AURASTAT2, lvl, &c);
        overlays_add("quickness", OV_AT_PLAYER, me, p->x, p->y, p->quick_ticks);
        return 1;
    case SK_FADE: /* resistencias (dm12) */
        p->fade_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->fade_res = skill_calc(s, SC_AURASTAT1, lvl, &c);
        overlays_add("fade", OV_AT_PLAYER, me, p->x, p->y, p->fade_ticks);
        refresh_stats(p);
        return 1;
    case SK_VENOM: /* veneno en los golpes (enms..exms en edma cuadros) */
        p->venom_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        skill_poison_syn(s, lvl, &c, &p->venom_lo, &p->venom_hi);
        p->venom_len = skill_elen(s, lvl) > 0 ? skill_elen(s, lvl) : 10;
        overlays_add("venomclaws", OV_AT_PLAYER, me, p->x, p->y, p->venom_ticks);
        return 1;
    case SK_BLADE_SHIELD:
        p->bshield_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->bshield_skill = (int)(s - skills_get(p->cls, 0));
        p->bshield_next = 0;
        overlays_add("bladeshield", OV_AT_PLAYER, me, p->x, p->y, p->bshield_ticks);
        return 1;
    case SK_CLOAK_SHADOWS: { /* ciega a los de alrededor (no a los jefes) y les baja la defensa */
        float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
        r = r > 20.0f ? 20.0f : r < 4.0f ? 4.0f : r;
        int ticks = skill_calc(s, SC_AURALEN, lvl, &c), def = skill_calc(s, SC_AURASTAT1, lvl, &c);
        int ids[64], n = units_enemies_near(p->x, p->y, r, ids, 64), done = 0;
        audio_play_name("assassin_cloakofdarkness", p->x, p->y);
        for (int k = 0; k < n; k++) {
            if (!units_cloak(ids[k], ticks, def))
                continue;
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            overlays_add("cloaked", OV_AT_UNIT, ids[k], ux, uy, ticks);
            done++;
        }
        return done;
    }
    case SK_MIND_BLAST: { /* aturde a los de alrededor del lugar y a algunos los convierte */
        float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
        r = r < 2.0f ? 2.0f : r;
        int plo, phi, stun = skill_elen(s, lvl), chance = skill_calc(s, SC_CALC1, lvl, &c);
        int conv = skill_calc(s, SC_CALC2, lvl, &c);
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        missiles_effect("mindblast center", tx, ty);
        int ids[32], n = units_enemies_near(tx, ty, r, ids, 32);
        for (int k = 0; k < n; k++) {
            ass_damage(p, ids[k], ELEM_NONE, roll(plo, phi));
            if (!units_alive_enemy(ids[k]))
                continue;
            if ((int)(rnd() % 100) < chance && units_convert(ids[k], me, conv > 0 ? conv : 150))
                continue;
            units_stun(ids[k], stun > 0 ? stun : 50);
        }
        return n;
    }
    case SK_SHADOW_WARRIOR:
    case SK_SHADOW_MASTER: { /* un clon oscuro con su equipo (uno a la vez) */
        if (p->shadow_unit >= 0)
            units_pet_remove(p->shadow_unit);
        float x = p->x + 1.0f, y = p->y + 1.0f;
        find_free_spot(&x, &y);
        int u = units_pet_spawn(me, s->summon, x, y, p->level_num, 0, 0, 0);
        if (u < 0)
            return 0;
        int hp, mhp, dlo, dhi;
        units_health(u, &hp, &mhp);
        mhp = mhp * (100 + s->param[5] + (lvl - 1) * s->param[6]) / 100; /* + ln56% de vida */
        weapon_damage(p, 0, &dlo, &dhi);
        int ar = player_attack_rating(p) + skill_calc(s, SC_AURASTAT1, lvl, &c);
        int def = player_defense(p) * (100 + (s->kind == SK_SHADOW_WARRIOR ? skill_calc(s, SC_AURASTAT2, lvl, &c) : 0)) / 100;
        units_pet_stats(u, mhp, dlo, dhi, ar, def);
        CharLook look;
        char wc[4];
        build_look(p, &look, wc, sizeof wc);
        units_pet_set_look(u, &look, wc);
        units_pet_shadow(u);
        overlays_add("shadowwarriorappear", OV_AT_UNIT, u, x, y, 0);
        p->shadow_unit = u;
        p->shadow_dead = 0;
        return 1;
    }
    default:
        return 0;
    }
}

/* ---- Las secuencias ---- */

/* Vuelo del dragón: aparece al lado del monstruo */
static void flight_jump(Player *p)
{
    if (!units_alive_enemy(p->target))
        return;
    float ux, uy;
    units_position(p->target, 1.0f, &ux, &uy);
    float dx = p->x - ux, dy = p->y - uy, d = sqrtf(dx * dx + dy * dy);
    if (d < 0.01f) {
        dx = 1.0f;
        d = 1.0f;
    }
    float r = units_radius(p->target) + 0.8f;
    float x = ux + dx / d * r, y = uy + dy / d * r;
    find_free_spot(&x, &y);
    overlays_add("dragonflight", OV_AT_UNIT, p->target, ux, uy, 0);
    p->x = p->prev_x = x;
    p->y = p->prev_y = y;
    p->level = world_level_at(p->x, p->y);
    face_unit(p, p->target);
}

/* Furia de cuchillas: una por golpe de la secuencia */
static void fury_throw(Player *p, const SkillDef *s, int lvl)
{
    float ax, ay;
    spell_aim(p, &ax, &ay);
    int lo, hi;
    blade_damage(p, s, lvl, &lo, &hi);
    Missile *mi = spell_missile(p, "bladefragment1", p->x, p->y, ax, ay, ELEM_NONE, 0, 0, 0);
    if (mi) {
        mi->dmin = lo;
        mi->dmax = hi;
    }
    p->fury_left--;
}

static void ass_seq_hit(Player *p, const SkillDef *s, int lvl)
{
    switch (s->kind) {
    case SK_DRAGON_TALON:
        if (p->kicks_left > 0) {
            p->kicks_left--;
            melee_strike(p, p->target);
        }
        break;
    case SK_DRAGON_FLIGHT:
        if (p->seq_hits == 0)
            flight_jump(p);
        else
            melee_strike(p, p->target);
        break;
    case SK_BLADE_FURY:
        fury_throw(p, s, lvl);
        break;
    default: /* Garra del dragón y las de carga con garra: el segundo golpe con la otra */
        p->hand = (p->seq_hits % 2) && dual_wield(p);
        melee_strike(p, p->target);
        p->hand = 0;
        break;
    }
}

static int ass_seq_again(Player *p, const SkillDef *s)
{
    if (s->kind == SK_DRAGON_TALON)
        return p->kicks_left > 0 && units_alive_enemy(p->target);
    if (s->kind == SK_BLADE_FURY) {
        int cost = skill_mana(s, eff_lvl(p, p->attack_skill));
        if (p->fury_left <= 0 || p->mana < cost || (!units_alive_enemy(p->target) && p->target >= 0))
            return 0;
        p->mana -= cost;
        return 1;
    }
    return 0;
}

/* ---- Después de un golpe cuerpo a cuerpo que acertó ---- */

static void ass_after_hit(Player *p, const SkillDef *s, int lvl, int target, int phys, int killed)
{
    if (prg_index(s->kind) >= 0) { /* suma una carga */
        prg_add(p, s, lvl);
        return;
    }
    if (!ass_finisher(s->kind))
        return;
    if (prg_any(p)) /* las libera todas */
        prg_release(p, target, phys);
    SkillCtx c = pctx(p);
    float ux, uy;
    units_position(target, 1.0f, &ux, &uy);
    switch (s->kind) {
    case SK_DRAGON_TALON: /* la última patada empuja (calc2 a los únicos y jefes) */
        if (p->kicks_left == 0 && !killed) {
            int chance = units_is_unique(target) ? skill_calc(s, SC_CALC2, lvl, &c) : 100;
            if ((int)(rnd() % 100) < chance)
                units_knockback(target, p->x, p->y, 2.0f);
        }
        break;
    case SK_DRAGON_TAIL: { /* explota en fuego: calc1% del daño físico, radio aurarange */
        int fire = phys * skill_calc(s, SC_CALC1, lvl, &c) / 100;
        float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c) / 2.0f;
        missiles_effect("dragontail missile", ux, uy);
        int ids[32], n = units_enemies_near(ux, uy, r > 1.5f ? r : 1.5f, ids, 32);
        for (int k = 0; k < n; k++)
            ass_damage(p, ids[k], ELEM_FIRE, fire);
        break;
    }
    default:
        break;
    }
}

/* Veneno: cada golpe cuerpo a cuerpo envenena */
static void ass_venom(Player *p, int target, int killed)
{
    if (p->venom_ticks <= 0 || killed || !units_alive_enemy(target))
        return;
    if (units_poison(target, roll(p->venom_lo, p->venom_hi), p->venom_len, (int)(p - s_pl)))
        poison_fx(target);
}

/* ---- Lo que dura ---- */

static void ass_tick(Player *p)
{
    int me = (int)(p - s_pl);
    for (int k = 0; k < PRG_COUNT; k++)
        if (p->prg_charges[k] > 0 && --p->prg_ticks[k] <= 0) {
            p->prg_charges[k] = 0;
            prg_overlay(p, k);
            prg_refresh(p);
        }
    if (p->quick_ticks > 0 && --p->quick_ticks == 0)
        p->quick_speed = p->quick_ias = 0;
    if (p->fade_ticks > 0 && --p->fade_ticks == 0) {
        p->fade_res = 0;
        refresh_stats(p);
    }
    if (p->venom_ticks > 0)
        p->venom_ticks--;
    /* Escudo de cuchillas: corta a los que están al lado */
    if (p->bshield_ticks > 0) {
        p->bshield_ticks--;
        if (--p->bshield_next <= 0 && !(p->level && p->level->town)) {
            p->bshield_next = 15;
            const SkillDef *s = skills_get(p->cls, p->bshield_skill);
            int lo, hi, ids[8], n = units_enemies_near(p->x, p->y, 2.5f, ids, 8);
            blade_damage(p, s, eff_lvl(p, p->bshield_skill), &lo, &hi);
            for (int k = 0; k < n; k++)
                ass_damage(p, ids[k], ELEM_NONE, roll(lo, hi));
        }
    }
    /* Las trampas: duran un minuto (o hasta gastar los tiros) */
    for (int k = 0; k < ASS_TRAPS; k++)
        if (p->trap_unit[k] >= 0 && (!units_pet_alive(p->trap_unit[k]) || s_ticks - p->trap_born[k] > 60 * TICK_RATE))
            trap_drop(p, k);
    sentinel_tick(p);
    /* El Guerrero de las sombras muerto se saca a los 2 segundos */
    if (p->shadow_unit >= 0 && !units_pet_alive(p->shadow_unit) && ++p->shadow_dead > 2 * TICK_RATE) {
        units_pet_remove(p->shadow_unit);
        p->shadow_unit = -1;
    }
    (void)me;
}

/* Termina todo (al empezar una ronda de la prueba) */
static void ass_clear(Player *p)
{
    for (int k = 0; k < PRG_COUNT; k++) {
        p->prg_charges[k] = 0;
        prg_overlay(p, k);
    }
    prg_refresh(p);
    for (int k = 0; k < ASS_TRAPS; k++)
        if (p->trap_unit[k] >= 0)
            trap_drop(p, k);
    if (p->sent_unit >= 0)
        units_pet_remove(p->sent_unit);
    p->sent_unit = -1;
    if (p->shadow_unit >= 0)
        units_pet_remove(p->shadow_unit);
    p->shadow_unit = -1;
    p->quick_ticks = p->quick_speed = p->quick_ias = 0;
    p->fade_ticks = p->fade_res = 0;
    p->venom_ticks = p->bshield_ticks = 0;
}
