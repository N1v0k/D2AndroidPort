/* ---- Las habilidades del Paladín (se incluye desde game.c) ----

   Como en el original (SkillPal.cpp de D2MOO). Las auras se eligen con su
   botón y quedan puestas (también en el pueblo) hasta que se elige otra.
   Lo que dan a los jugadores (aurastat de skills.txt; al paladín, además,
   sus pasivas: el fuego de Fuego sagrado en los golpes, el daño entero de
   Fanatismo) vale para los que están a su alcance (aurarangecalc, en
   subtiles) y se recalcula en cada tick. Cada perdelay cuadros (2 segundos)
   hacen lo demás: Plegaria, Purificación y Meditación curan, Fuego,
   Descarga y Congelación sagrados dañan a los monstruos de alrededor (la
   Congelación además los frena), Santuario daña y empuja a los no muertos,
   Convicción les baja la defensa y las resistencias y Redención se lleva
   los cadáveres a cambio de vida y maná. Plegaria y Santuario gastan maná en
   cada pulso que hacen algo, y mientras tanto el maná no se recupera
   (STATE_NOMANAREGEN). Dos paladines con la misma aura no se suman: vale la
   más fuerte. Las de grupo también les llegan a los aliados (la mercenaria,
   la Valquiria, los convertidos), como el aurafilter del original. */

#define CHARGE_TICKS (3 * TICK_RATE) /* Carga: corre hasta esto como mucho */
#define FIST_DELAY   10              /* fistoftheheavensdelay: el rayo cae a los 10 cuadros */
/* El Martillo bendecido, como PATH_ComputePathBlessedHammer del original:
   la espiral sale siempre hacia el mismo lado (ángulo 0 del mapa), con 32
   puntos por vuelta que se alejan 9600/65536 de subtile cada uno, y se
   recorre a la velocidad de missiles.txt (Vel 18 que sube hasta MaxVel 30) */
#define HAMMER_STEP  (2.0f * PI_F / 32.0f)      /* radianes entre punto y punto */
#define HAMMER_PITCH (9600.0f / 65536.0f / HAMMER_STEP) /* subtiles que se aleja por radián */
#define HAMMER_END   (77.0f * HAMMER_STEP)      /* los puntos del camino (MAXPATHLEN - 1) */
#define AURA_SHOW    4               /* ticks que se renueva el dibujo del aura */

static int s_aura_debug = -1; /* D2_AURA_DEBUG: cada 5 s, lo que dan las auras y los bloqueos */

static int pal_kind(int kind)
{
    return kind >= SK_SACRIFICE && kind <= SK_SALVATION;
}

/* Las que les dan algo a los jugadores (SrvDo065 del original); las otras
   actúan sobre los monstruos o los cadáveres */
static int party_aura(int kind)
{
    switch (kind) {
    case SK_HOLY_FIRE:
    case SK_HOLY_FREEZE:
    case SK_HOLY_SHOCK:
    case SK_SANCTUARY:
    case SK_CONVICTION:
    case SK_REDEMPTION:
        return 0;
    default:
        return 1;
    }
}

/* Cómo se ve cada aura (los overlay de su estado en states.txt) y lo que suena al elegirla */
typedef struct {
    int         kind;
    const char *front, *back, *sound;
} AuraLook;
static const AuraLook k_aura_looks[] = {
    { SK_MIGHT, "aura_might_front", "aura_might_back", "paladin_aura_might" },
    { SK_HOLY_FIRE, "aura_holyfire_front", "aura_holyfire_back", "paladin_aura_holyfire" },
    { SK_THORNS, "aura_thorns_front", "aura_thorns_back", "paladin_aura_thorns" },
    { SK_BLESSED_AIM, "blessedaimfront", "blessedaimback", "paladin_aura_blessedaim" },
    { SK_CONCENTRATION, "concentrationfront", "concentrationback", "paladin_aura_concentration" },
    { SK_HOLY_FREEZE, NULL, "holyfreeze", "paladin_aura_holywind" },
    { SK_HOLY_SHOCK, "holyshockfront", "holyshockback", "paladin_aura_holylight" },
    { SK_SANCTUARY, "sanctuaryfront", "sanctuaryback", "paladin_aura_sanctuary" },
    { SK_FANATICISM, "fanaticismfront", "fanaticismback", "paladin_aura_fanatacism" },
    { SK_CONVICTION, NULL, "convictionback", "paladin_aura_purification" },
    { SK_PRAYER, "aura_prayer_front", "aura_prayer_back", "paladin_aura_prayer" },
    { SK_RESIST_FIRE, NULL, "aura_resistfire", "paladin_aura_resistfire" },
    { SK_DEFIANCE, "aura_defiance_front", "aura_defiance_back", "paladin_aura_defiance" },
    { SK_RESIST_COLD, NULL, "aura_resistcold", "paladin_aura_resistcold" },
    { SK_CLEANSING, "cleansingfront", "cleansingback", "paladin_aura_cleansing" },
    { SK_RESIST_LIGHTNING, NULL, "aura_resistlight", "paladin_aura_resistlightning" },
    { SK_VIGOR, "staminafront", "staminaback", "paladin_aura_stamina" },
    { SK_MEDITATION, "meditationfront", NULL, "paladin_aura_meditation" },
    { SK_REDEMPTION, NULL, "redemptionback", "paladin_aura_redemption" },
    { SK_SALVATION, "aura_resistall_front", "aura_resistall_back", "paladin_aura_salvation" },
};

static const AuraLook *aura_look(int kind)
{
    for (unsigned k = 0; k < sizeof k_aura_looks / sizeof k_aura_looks[0]; k++)
        if (k_aura_looks[k].kind == kind)
            return &k_aura_looks[k];
    return NULL;
}

/* Lo que cuesta cada pulso: el maná de skills.txt sin el mínimo (Poder no gasta; Plegaria sí) */
static int aura_mana(const SkillDef *s, int lvl)
{
    return ((s->mana + s->lvlmana * (lvl - 1)) << s->manashift) >> 8;
}

static int skill_index(const Player *p, const SkillDef *s)
{
    return (int)(s - skills_get(p->cls, 0));
}

/* ---- Las auras ---- */

static void aura_select(Player *p, int skill)
{
    const SkillDef *s = skills_get(p->cls, skill);
    if (p->aura != skill) {
        p->aura = skill;
        p->aura_next = 1; /* el primer pulso enseguida */
        const AuraLook *l = aura_look(s->kind);
        if (l && l->sound)
            audio_play_name(l->sound, p->x, p->y);
    }
    char msg[48];
    snprintf(msg, sizeof msg, "Aura: %s", s->name);
    show_message(p, msg);
}

static void aura_reset(Player *o)
{
    o->au_dmg_pct = o->au_ar_pct = o->au_def_pct = o->au_ias = o->au_speed = o->au_conc = 0;
    o->au_mana_regen = o->au_stam_regen = o->au_stam_pct = o->au_thorns = o->au_thorns_flat = 0;
    o->au_undead_dmg = o->au_undead_ar = 0;
    o->au_life_pct = 0;
    o->au_got_n = 0;
    memset(o->au_res, 0, sizeof o->au_res);
    memset(o->au_res_max, 0, sizeof o->au_res_max);
    memset(o->au_elem_min, 0, sizeof o->au_elem_min);
    memset(o->au_elem_max, 0, sizeof o->au_elem_max);
}

/* Una propiedad de un aura sobre un jugador (los nombres de itemstatcost.txt) */
static void aura_stat(Player *o, const SkillDef *s, const char *stat, int v)
{
    static const char *const k_res[3] = { "fireresist", "coldresist", "lightresist" };
    static const char *const k_res_max[3] = { "maxfireresist", "maxcoldresist", "maxlightresist" };
    /* En el orden de elemental_damage: fuego, rayo y frío */
    static const char *const k_min[3] = { "firemindam", "lightmindam", "coldmindam" };
    static const char *const k_max[3] = { "firemaxdam", "lightmaxdam", "coldmaxdam" };
    for (int e = 0; e < 3; e++) {
        if (!strcmp(stat, k_res[e]))
            o->au_res[e] += v;
        else if (!strcmp(stat, k_res_max[e]))
            o->au_res_max[e] += v;
        else if (!strcmp(stat, k_min[e]))
            o->au_elem_min[e] += v;
        else if (!strcmp(stat, k_max[e]))
            o->au_elem_max[e] += v;
    }
    if (!strcmp(stat, "damagepercent")) {
        o->au_dmg_pct += v;
        if (s->kind == SK_CONCENTRATION) /* también le suma al Martillo bendito */
            o->au_conc += v;
    } else if (!strcmp(stat, "item_tohit_percent")) {
        o->au_ar_pct += v;
    } else if (!strcmp(stat, "skill_armor_percent")) {
        o->au_def_pct += v;
    } else if (!strcmp(stat, "attackrate")) {
        o->au_ias += v;
    } else if (!strcmp(stat, "velocitypercent")) {
        o->au_speed += v;
    } else if (!strcmp(stat, "staminarecoverybonus")) {
        o->au_stam_regen += v;
    } else if (!strcmp(stat, "skill_staminapercent")) {
        o->au_stam_pct += v;
    } else if (!strcmp(stat, "manarecoverybonus")) {
        o->au_mana_regen += v;
    } else if (!strcmp(stat, "thorns_percent")) {
        o->au_thorns += v;
    } else if (!strcmp(stat, "item_attackertakesdamage")) {
        o->au_thorns_flat += v;
    } else if (!strcmp(stat, "item_undeaddamage_percent")) {
        o->au_undead_dmg += v;
    } else if (!strcmp(stat, "item_undead_tohit")) {
        o->au_undead_ar += v;
    }
}

/* Lo que le da el aura de `c` a `o`: las aurastat (las de grupo) y, al
   paladín, sus pasivas, que reemplazan a la aurastat del mismo nombre (como
   SKILLS_SrvDo065_BasicAura: Fanatismo le da todo el daño a él y la mitad al grupo) */
static void aura_give(Player *o, const Player *c, const SkillDef *s, int lvl)
{
    SkillCtx x = pctx(c);
    int self = o == c, own = self && !s->passive_always;
    if (o->au_got_n < 4) { /* (su ícono arriba de la barra) */
        o->au_got_cls[o->au_got_n] = (int8_t)c->cls;
        o->au_got_skill[o->au_got_n++] = (int16_t)(s - skills_get(c->cls, 0));
    }
    if (party_aura(s->kind))
        for (int k = 0; k < 3; k++) {
            const char *stat = s->aurastat[k];
            if (!stat[0] || !strcmp(stat, "hitpoints") || !strcmp(stat, "item_poisonlengthresist"))
                continue; /* esas son del pulso */
            int v = skill_calc(s, SC_AURASTAT1 + k, lvl, &x);
            for (int j = 0; own && j < 3; j++)
                if (!strcmp(s->passivestat[j], stat))
                    v = skill_calc(s, SC_PASSIVE1 + j, lvl, &x);
            aura_stat(o, s, stat, v);
        }
    for (int j = 0; own && j < 3; j++) {
        const char *stat = s->passivestat[j];
        int dup = 0;
        for (int k = 0; k < 3 && party_aura(s->kind); k++)
            dup |= !strcmp(s->aurastat[k], stat);
        if (stat[0] && !dup)
            aura_stat(o, s, stat, skill_calc(s, SC_PASSIVE1 + j, lvl, &x));
    }
}

/* Los que llevan un aura en este tick: los jugadores y los guerreros del
   desierto (los mercenarios de Greiz). El del mercenario es como un paladín
   parado donde está él, con el aura al nivel de hireling.txt, sin
   sinergias y sin gastar maná. */
static Player  s_merc_caster[MAX_PLAYERS];
static Player *s_casters[MAX_PLAYERS * 2];
static int     s_num_casters;

static void aura_casters(void)
{
    s_num_casters = 0;
    for (int i = 0; i < s_num_players; i++)
        s_casters[s_num_casters++] = &s_pl[i];
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        Player *m = &s_merc_caster[i];
        MercStats ms;
        int cls = 0, s = -1;
        if (!p->merc_name || p->merc_dead || !units_merc_alive(p->merc_unit))
            continue;
        merc_stats(p->merc_type, p->merc_level, &ms);
        for (int c = 0; ms.aura[0] && c < NUM_CLASSES && s < 0; c++)
            if ((s = skills_find(c, ms.aura)) >= 0)
                cls = c;
        if (s < 0)
            continue;
        if (m->aura != s || m->cls != cls) { /* otro mercenario: el primer pulso enseguida */
            memset(m, 0, sizeof *m);
            m->aura = s;
            m->cls = cls;
            m->aura_next = 1;
        }
        m->skill_lvl[s] = ms.aura_lvl;
        m->level_num = ms.level;
        m->level = p->level;
        m->mana = m->mana_max = 1 << 20;
        m->state = PS_NORMAL;
        units_position(p->merc_unit, 1.0f, &m->x, &m->y);
        s_casters[s_num_casters++] = m;
    }
}

/* ¿Hay otro (paladín o mercenario) con la misma aura, más fuerte, que también lo alcanza? */
static int aura_outranked(const Player *c, const SkillDef *s, int lvl, const Player *o)
{
    int me = 0;
    while (me < s_num_casters && s_casters[me] != c)
        me++;
    for (int i = 0; i < s_num_casters; i++) {
        const Player *d = s_casters[i];
        if (i == me || d->aura < 0 || d->state >= PS_DYING || d->cls != c->cls)
            continue;
        const SkillDef *t = skills_get(d->cls, d->aura);
        int dl = eff_lvl(d, d->aura);
        if (t->kind != s->kind || dl < lvl || (dl == lvl && i > me))
            continue;
        SkillCtx x = pctx(d);
        float r = (float)skill_calc(t, SC_AURARANGE, dl, &x), dx = o->x - d->x, dy = o->y - d->y;
        if (o == d || dx * dx + dy * dy <= r * r)
            return 1;
    }
    return 0;
}

/* El pulso: lo que hace cada perdelay cuadros */
static void aura_pulse(Player *c, const SkillDef *s, int lvl, float r, int cost)
{
    SkillCtx x = pctx(c);
    int n = 0, lo, hi, ids[64], m;
    int period = s->perdelay > 5 ? s->perdelay : 5;
    int in_town = c->level && c->level->town;
    if (c->mana < cost) {
        c->aura_nomana = 0;
        return;
    }
    switch (s->kind) {
    case SK_PRAYER:
    case SK_CLEANSING:
    case SK_MEDITATION: /* curan (hitpoints, en 1/256) y Purificación acorta el veneno y las maldiciones */
        for (int i = 0; i < s_num_players; i++) {
            Player *o = &s_pl[i];
            float dx = o->x - c->x, dy = o->y - c->y;
            if (o->state >= PS_DYING || dx * dx + dy * dy > r * r || (o != c && aura_outranked(c, s, lvl, o)))
                continue;
            for (int k = 0; k < 3; k++) {
                int v = skill_calc(s, SC_AURASTAT1 + k, lvl, &x);
                if (!strcmp(s->aurastat[k], "hitpoints") && v > 0 && o->hp < o->hp_max) {
                    o->hp += v >> 8 > 0 ? v >> 8 : 1;
                    if (o->hp > o->hp_max)
                        o->hp = o->hp_max;
                    n++;
                } else if (!strcmp(s->aurastat[k], "item_poisonlengthresist") && (o->poison_ticks > 0 || o->amp_ticks > 0)) {
                    o->poison_ticks = o->poison_ticks * v / 100;
                    o->amp_ticks = o->amp_ticks * v / 100;
                    n++;
                }
            }
        }
        m = units_allies(ids, 64); /* la mercenaria, la Valquiria, los convertidos */
        for (int a = 0; a < m; a++) {
            float ux, uy;
            units_position(ids[a], 1.0f, &ux, &uy);
            if ((ux - c->x) * (ux - c->x) + (uy - c->y) * (uy - c->y) > r * r)
                continue;
            for (int k = 0; k < 3; k++) {
                int v = skill_calc(s, SC_AURASTAT1 + k, lvl, &x);
                if (!strcmp(s->aurastat[k], "hitpoints") && v > 0)
                    n += units_ally_heal(ids[a], v >> 8 > 0 ? v >> 8 : 1);
            }
        }
        break;
    case SK_HOLY_FIRE:
    case SK_HOLY_SHOCK:
    case SK_HOLY_FREEZE: { /* daño a todos los de alrededor (la Congelación, además, los frena) */
        if (in_town)
            break;
        int elem = s->kind == SK_HOLY_FIRE ? ELEM_FIRE : s->kind == SK_HOLY_SHOCK ? ELEM_LTNG : ELEM_COLD;
        skill_elem_syn(s, lvl, &x, &lo, &hi);
        m = units_enemies_near(c->x, c->y, r, ids, 64);
        for (int k = 0; k < m; k++) {
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            if (s->kind == SK_HOLY_FREEZE) {
                if (units_resist(ids[k], ELEM_COLD, 100) <= 0)
                    continue; /* los inmunes al frío ni se enteran */
                units_slow(ids[k], -skill_calc(s, SC_AURASTAT1, lvl, &x), period + 1);
                missiles_effect("iceexplode", ux, uy);
            } else {
                overlays_add(s->kind == SK_HOLY_FIRE ? "hit_holyfire" : "holyshockhit", OV_AT_UNIT, ids[k], ux, uy, 0);
            }
            int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : units_resist(ids[k], elem, roll(lo, hi)));
            if (exp > 0)
                award_kill(c, ids[k], exp);
            n++;
        }
        break;
    }
    case SK_SANCTUARY: /* a los no muertos: daño mágico y los empuja */
        if (in_town)
            break;
        skill_elem_syn(s, lvl, &x, &lo, &hi);
        m = units_enemies_near(c->x, c->y, r, ids, 64);
        for (int k = 0; k < m; k++) {
            if (!units_is_undead(ids[k]))
                continue;
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            overlays_add("sanctuaryknockback", OV_AT_UNIT, ids[k], ux, uy, 0);
            int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : units_resist(ids[k], ELEM_MAG, roll(lo, hi)));
            if (exp > 0)
                award_kill(c, ids[k], exp);
            else
                units_knockback(ids[k], c->x, c->y, 2.0f);
            n++;
        }
        break;
    case SK_CONVICTION: /* menos defensa y resistencias (aurastat1 y 2..4 son negativas) */
        if (in_town)
            break;
        m = units_enemies_near(c->x, c->y, r, ids, 64);
        for (int k = 0; k < m; k++)
            units_conviction(ids[k], skill_calc(s, SC_AURASTAT1, lvl, &x), -skill_calc(s, SC_AURASTAT2, lvl, &x),
                             period + 1);
        n = m;
        break;
    case SK_REDEMPTION: /* cada cadáver: calc1 % de que se vaya y dé calc2 de vida y calc3 de maná */
        if (in_town)
            break;
        m = units_corpses_near(c->x, c->y, r, ids, 64);
        for (int k = 0; k < m; k++) {
            if (units_is_unique(ids[k]) || (int)(rnd() % 100) >= skill_calc(s, SC_CALC1, lvl, &x))
                continue;
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            units_consume_corpse(ids[k]);
            missiles_effect("redemption", ux, uy);
            audio_play_name("paladin_redeemed_soul", ux, uy);
            c->hp += skill_calc(s, SC_CALC2, lvl, &x);
            c->mana += skill_calc(s, SC_CALC3, lvl, &x);
            if (c->hp > c->hp_max)
                c->hp = c->hp_max;
            if (c->mana > c->mana_max)
                c->mana = c->mana_max;
            n++;
        }
        break;
    default:
        break;
    }
    if (cost > 0 && n > 0) {
        c->mana -= cost;
        c->aura_nomana = 1;
    } else {
        c->aura_nomana = 0;
    }
}

/* Las de grupo a los aliados: la mercenaria, la Valquiria y los convertidos
   (cada tick, con lo que les toca de cada paladín cercano) */
static void aura_allies(void)
{
    static Player tmp; /* lo que les darían a un jugador ahí */
    int ids[24], n = units_allies(ids, 24);
    for (int k = 0; k < n; k++) {
        aura_reset(&tmp);
        units_position(ids[k], 1.0f, &tmp.x, &tmp.y);
        for (int ci = 0; ci < s_num_casters; ci++) {
            const Player *c = s_casters[ci];
            if (c->aura < 0 || c->state >= PS_DYING)
                continue;
            const SkillDef *s = skills_get(c->cls, c->aura);
            int lvl = eff_lvl(c, c->aura);
            SkillCtx x = pctx(c);
            float r = (float)skill_calc(s, SC_AURARANGE, lvl, &x), dx = tmp.x - c->x, dy = tmp.y - c->y;
            if (lvl <= 0 || !party_aura(s->kind) || dx * dx + dy * dy > r * r || c->mana < aura_mana(s, lvl) ||
                aura_outranked(c, s, lvl, &tmp))
                continue;
            aura_give(&tmp, c, s, lvl);
            const AuraLook *look = aura_look(s->kind);
            if (look && look->front)
                overlays_add(look->front, OV_AT_UNIT, ids[k], tmp.x, tmp.y, AURA_SHOW);
            if (look && look->back)
                overlays_add(look->back, OV_AT_UNIT, ids[k], tmp.x, tmp.y, AURA_SHOW);
        }
        dru_spirit_give(&tmp); /* los espíritus del Druida */
        war_aura_give(&tmp);   /* y los demonios del Brujo */
        AllyAura a;
        memset(&a, 0, sizeof a);
        a.dmg_pct = tmp.au_dmg_pct;
        a.ar_pct = tmp.au_ar_pct;
        a.def_pct = tmp.au_def_pct;
        a.ias = tmp.au_ias;
        a.speed = tmp.au_speed;
        for (int e = 0; e < 3; e++) {
            a.res[e] = tmp.au_res[e];
            a.res_max[e] = tmp.au_res_max[e];
        }
        a.thorns = tmp.au_thorns;
        a.thorns_flat = tmp.au_thorns_flat;
        units_ally_aura(ids[k], &a);
        if (s_aura_debug && s_ticks % (5 * TICK_RATE) == 0) {
            int hp, max;
            units_health(ids[k], &hp, &max);
            printf("  auras: aliado %d (%s, de J%d%s) vida %d/%d: daño %+d%%, puntería %+d%%, defensa %+d%%, "
                   "ataque %+d%%, velocidad %+d%%, resist %d/%d/%d, espinas %d%%\n",
                   ids[k], units_display_name(ids[k]), units_ally_owner(ids[k]) + 1,
                   units_converted(ids[k]) ? ", convertido" : "", hp, max, a.dmg_pct, a.ar_pct, a.def_pct, a.ias,
                   a.speed, a.res[0], a.res[1], a.res[2], a.thorns);
        }
    }
}

/* Cada tick: lo que les dan las auras a los jugadores y a los aliados (y el pulso de cada una) */
static void pal_auras_tick(void)
{
    if (s_aura_debug < 0)
        s_aura_debug = getenv("D2_AURA_DEBUG") != NULL;
    for (int i = 0; i < s_num_players; i++)
        aura_reset(&s_pl[i]);
    aura_casters();
    for (int ci = 0; ci < s_num_casters; ci++) {
        Player *c = s_casters[ci];
        if (c->aura < 0)
            continue;
        const SkillDef *s = skills_get(c->cls, c->aura);
        int lvl = eff_lvl(c, c->aura);
        if (lvl <= 0 || !s->aura) {
            c->aura = -1;
            continue;
        }
        if (c->state >= PS_DYING) {
            c->aura_nomana = 0;
            continue;
        }
        SkillCtx x = pctx(c);
        float r = (float)skill_calc(s, SC_AURARANGE, lvl, &x);
        int cost = aura_mana(s, lvl);
        const AuraLook *look = aura_look(s->kind);
        for (int oi = 0; oi < s_num_players; oi++) {
            Player *o = &s_pl[oi];
            float dx = o->x - c->x, dy = o->y - c->y;
            if (o->state >= PS_DYING || (o != c && (!party_aura(s->kind) || dx * dx + dy * dy > r * r)) ||
                aura_outranked(c, s, lvl, o))
                continue;
            if (c->mana >= cost)
                aura_give(o, c, s, lvl);
            if (look && look->front)
                overlays_add(look->front, OV_AT_PLAYER, oi, o->x, o->y, AURA_SHOW);
            if (look && look->back)
                overlays_add(look->back, OV_AT_PLAYER, oi, o->x, o->y, AURA_SHOW);
        }
        if (--c->aura_next <= 0) {
            c->aura_next = s->perdelay > 5 ? s->perdelay : 5;
            aura_pulse(c, s, lvl, r, cost);
        }
    }
    for (int i = 0; i < s_num_players; i++) /* los espíritus del Druida */
        if (s_pl[i].state < PS_DYING) {
            dru_spirit_give(&s_pl[i]);
            war_aura_give(&s_pl[i]);
        }
    aura_allies();
    if (s_aura_debug && s_ticks % (5 * TICK_RATE) == 0)
        for (int i = 0; i < s_num_players; i++) {
            const Player *o = &s_pl[i];
            printf("  auras: J%d aura %s: daño %+d%%, puntería %+d%%, defensa %+d%%, ataque %+d%%, velocidad %+d%%, "
                   "resist %d/%d/%d, bloqueo %d%% (%d bloqueos)\n",
                   i + 1, o->aura >= 0 ? skills_get(o->cls, o->aura)->key : "-", o->au_dmg_pct, o->au_ar_pct,
                   o->au_def_pct, o->au_ias, o->au_speed, o->au_res[0], o->au_res[1], o->au_res[2],
                   player_block_chance(o), s_blocks[i]);
        }
    /* Vigor: cambia la estamina máxima */
    for (int i = 0; i < s_num_players; i++) {
        Player *o = &s_pl[i];
        if (o->au_stam_pct != o->au_stam_seen || o->au_life_pct != o->au_life_seen) { /* y el Sabio roble: la vida */
            o->au_stam_seen = o->au_stam_pct;
            o->au_life_seen = o->au_life_pct;
            refresh_stats(o);
        }
    }
}

/* Espinas: el monstruo que le pegó cuerpo a cuerpo recibe parte del daño */
static void pal_thorns(Player *p, int attacker, int dmg)
{
    if ((p->au_thorns <= 0 && p->au_thorns_flat <= 0) || dmg <= 0 || !units_alive_enemy(attacker))
        return;
    float x, y;
    units_position(attacker, 1.0f, &x, &y);
    overlays_add("hit_thorns", OV_AT_UNIT, attacker, x, y, 0);
    audio_play_name("paladin_thorns_hit", x, y);
    int back = units_resist(attacker, ELEM_NONE, dmg * p->au_thorns / 100 + p->au_thorns_flat);
    int exp = units_damage(attacker, s_one_hit ? 1 << 20 : back);
    if (exp > 0)
        award_kill(p, attacker, exp);
}

/* Espinas sobre un aliado (la mercenaria, la Valquiria): la experiencia es de su jugador */
static void ally_thorns(Player *owner, int unit, int attacker, int dmg)
{
    const AllyAura *a = units_ally_aura_get(unit);
    if ((a->thorns <= 0 && a->thorns_flat <= 0) || dmg <= 0 || !units_alive_enemy(attacker))
        return;
    float x, y;
    units_position(attacker, 1.0f, &x, &y);
    overlays_add("hit_thorns", OV_AT_UNIT, attacker, x, y, 0);
    int exp = units_damage(attacker, s_one_hit ? 1 << 20 : units_resist(attacker, ELEM_NONE, dmg * a->thorns / 100 + a->thorns_flat));
    if (exp > 0)
        award_kill(owner, attacker, exp);
}

/* ---- Al lanzarla ---- */

/* Hacia dónde van Rayo sagrado y el Martillo: el monstruo más cercano o adelante */
static void pal_aim(Player *p, float range)
{
    p->target = units_find_enemy(p->x, p->y, range);
    float ax, ay;
    screen_dir_to_world(p->face_x, p->face_y, &ax, &ay);
    float l = sqrtf(ax * ax + ay * ay);
    p->cast_x = p->x + ax / (l > 0.0f ? l : 1.0f) * SPELL_AHEAD;
    p->cast_y = p->y + ay / (l > 0.0f ? l : 1.0f) * SPELL_AHEAD;
    if (p->target >= 0) {
        units_position(p->target, 1.0f, &p->cast_x, &p->cast_y);
        face_unit(p, p->target);
    }
}

/* Las de combate y los hechizos (las auras se eligen y listo). 1 si quedó
   ocupado; gasta el maná si arrancó. */
static int pal_start(Player *p, int skill, int cost)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    SkillCtx c = pctx(p);
    if (s->aura) {
        aura_select(p, skill);
        return 0;
    }
    switch (s->kind) {
    case SK_SACRIFICE: /* con la secuencia del original (sin arma, lanza o bastón); si no, el ataque */
        p->target = units_find_enemy(p->x, p->y, MELEE_RANGE);
        if (p->target >= 0)
            face_unit(p, p->target);
        if (s->seqnum > 0 && start_seq(p, skill))
            play_skill_sound(p, s);
        else
            start_attack(p, skill);
        break;
    case SK_SMITE: /* el golpe con el escudo (S1) */
        start_attack(p, skill);
        if (player_anim(p, MODE_S1)->ok) {
            p->mode = MODE_S1;
            p->anim_acc = 0;
        }
        break;
    case SK_ZEAL:
        start_attack(p, skill);
        p->seq_loops = skill_calc(s, SC_CALC1, lvl, &c); /* cuántos golpes */
        p->seq_hits = 0;
        break;
    case SK_VENGEANCE:
    case SK_CONVERSION:
        start_attack(p, skill);
        break;
    case SK_CHARGE: {
        int t = units_find_enemy(p->x, p->y, AIM_RANGE);
        if (t < 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        float ux, uy;
        units_position(t, 1.0f, &ux, &uy);
        float dx = ux - p->x, dy = uy - p->y;
        float reach = MELEE_RANGE + units_radius(t) - 0.45f;
        if (dx * dx + dy * dy <= reach * reach) { /* ya está al lado: le pega */
            start_attack(p, skill);
            break;
        }
        set_player_state(p, PS_ATTACK, player_anim(p, MODE_RN)->ok ? MODE_RN : MODE_WL);
        p->target = t;
        p->attack_skill = skill;
        p->attack_kind = ATK_MELEE;
        p->hits_total = 1;
        p->hits_done = 0;
        p->hand = 0;
        p->charge_ticks = CHARGE_TICKS;
        face_unit(p, t);
        play_skill_sound(p, s);
        break;
    }
    case SK_FIST_HEAVENS:
        pal_aim(p, AIM_RANGE);
        if (p->target < 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        /* fall through */
    case SK_HOLY_BOLT:
    case SK_BLESSED_HAMMER:
    case SK_HOLY_SHIELD:
        if (s->kind != SK_FIST_HEAVENS)
            pal_aim(p, AIM_RANGE);
        p->attack_skill = skill;
        p->cast_target = -1;
        p->hits_done = 0;
        play_skill_sound(p, s);
        p->mana -= cost;
        if (!player_anim(p, MODE_SC)->ok) {
            cast_effect(p);
            return 1;
        }
        set_player_state(p, PS_CAST, MODE_SC);
        return 1;
    default:
        return 0;
    }
    p->mana -= cost;
    return 1;
}

/* ---- El efecto de los hechizos (en el cuadro del lanzamiento) ---- */

static int pal_effect(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int lo, hi;
    float ax, ay;
    switch (s->kind) {
    case SK_HOLY_BOLT: { /* atraviesa: daña a los no muertos y demonios y cura a los aliados (calc1..calc2) */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        Missile *mi = spell_missile(p, "holybolt", p->x, p->y, ax, ay, ELEM_MAG, lo, hi, 0);
        if (!mi)
            return 0;
        mi->flags |= MF_HOLY | MF_HEAL | MF_PIERCE;
        mi->burn_min = skill_calc(s, SC_CALC1, lvl, &c);
        mi->burn_max = skill_calc(s, SC_CALC2, lvl, &c);
        return 1;
    }
    case SK_BLESSED_HAMMER: { /* gira en espiral alrededor de donde lo tiró; Concentración le suma */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        lo = lo * (100 + p->au_conc) / 100;
        hi = hi * (100 + p->au_conc) / 100;
        Missile *mi = spell_missile(p, "blessedhammer", p->x, p->y, ax, ay, ELEM_MAG, lo, hi, 0);
        if (!mi)
            return 0;
        mi->flags |= MF_HAMMER | MF_PIERCE;
        mi->mx = p->x;
        mi->my = p->y;
        mi->ang = HAMMER_STEP; /* el primer punto de la espiral */
        mi->x = mi->mx + HAMMER_PITCH * HAMMER_STEP * cosf(HAMMER_STEP);
        mi->y = mi->my + HAMMER_PITCH * HAMMER_STEP * sinf(HAMMER_STEP);
        return 1;
    }
    case SK_FIST_HEAVENS: /* el rayo cae a los 10 cuadros (pal_tick) */
        if (!units_alive_enemy(p->target))
            return 0;
        p->fist_target = p->target;
        p->fist_skill = skill_index(p, s);
        p->fist_ticks = FIST_DELAY;
        units_position(p->target, 1.0f, &p->fist_x, &p->fist_y);
        return 1;
    case SK_HOLY_SHIELD:
        p->hs_skill = skill_index(p, s);
        p->hs_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
        p->hs_def = skill_calc(s, SC_CALC1, lvl, &c);
        p->hs_block = skill_calc(s, SC_AURASTAT1, lvl, &c);
        return 1;
    default:
        return 0;
    }
}

/* ---- Lo que agregan al golpe cuerpo a cuerpo ---- */

/* Castigar: el daño del escudo (más el del Escudo sagrado) con los % de la
   habilidad y de las auras; como SKILLS_SrvDo150_Smite, la fuerza no suma
   (el escudo no tiene StrBonus) */
static void smite_damage(const Player *p, int extra_pct, int *dmin, int *dmax)
{
    const ItemType *sh = p->equip[SLOT_SHIELD].type;
    int lo = sh ? sh->smite_min : 1, hi = sh ? sh->smite_max : 2;
    if (p->hs_ticks > 0) {
        const SkillDef *hs = skills_get(p->cls, p->hs_skill);
        int a, b;
        skill_phys(hs, eff_lvl(p, p->hs_skill), &a, &b);
        lo += a;
        hi += b;
    }
    int pct = 100 + p->sk_dmg_pct + p->au_dmg_pct + extra_pct;
    *dmin = lo * pct / 100;
    *dmax = hi * pct / 100;
    if (*dmin < 1)
        *dmin = 1;
    if (*dmax < *dmin)
        *dmax = *dmin;
}

/* Venganza: calc1, calc2 y calc3 % del daño del arma como fuego, frío y rayo */
static int vengeance_damage(Player *p, const SkillDef *s, int lvl, int target, float ux, float uy)
{
    SkillCtx c = pctx(p);
    int lo = 1, hi = 2;
    if (p->equip[SLOT_WEAPON].type)
        items_damage(&p->equip[SLOT_WEAPON], &lo, &hi);
    int base = roll(lo, hi > lo ? hi : lo + 1), dmg = 0;
    static const int k_elem[3] = { ELEM_FIRE, ELEM_COLD, ELEM_LTNG };
    static const char *const k_fx[3] = { "fireexplode", "iceexplode", "lightninghit" };
    for (int e = 0; e < 3; e++) {
        int part = base * skill_calc(s, SC_CALC1 + e, lvl, &c) / 100;
        if (part <= 0)
            continue;
        dmg += units_resist(target, k_elem[e], part);
        missiles_effect(k_fx[e], ux, uy);
    }
    units_chill(target, skill_elen(s, lvl));
    return dmg;
}

/* Después de pegar */
static void pal_after_hit(Player *p, const SkillDef *s, int lvl, int target, float ux, float uy, int phys, int killed)
{
    SkillCtx c = pctx(p);
    switch (s->kind) {
    case SK_SACRIFICE: { /* se lastima calc2 % del daño (sin morirse) */
        int self = phys * skill_calc(s, SC_CALC2, lvl, &c) / 100;
        if (self > 0) {
            p->hp = p->hp - self > 1 ? p->hp - self : 1;
            Missile *b = missiles_effect("blood1", p->x, p->y);
            if (b)
                b->life = 16;
        }
        break;
    }
    case SK_SMITE: /* aturde calc2 cuadros y empuja */
        if (!killed) {
            int t = skill_calc(s, SC_CALC2, lvl, &c);
            units_stun(target, t);
            overlays_add("stun", OV_AT_UNIT, target, ux, uy, t);
            units_knockback(target, p->x, p->y, 1.2f);
        }
        break;
    case SK_CHARGE:
        overlays_add("charge", OV_AT_UNIT, target, ux, uy, 0);
        if (!killed)
            units_knockback(target, p->x, p->y, 2.0f);
        break;
    case SK_CONVERSION: /* calc1 % de que pelee de su lado (auralencalc cuadros) */
        /* (D2_CONVERT_ALWAYS: siempre, para probarla) */
        if (!killed && ((int)(rnd() % 100) < skill_calc(s, SC_CALC1, lvl, &c) || getenv("D2_CONVERT_ALWAYS"))) {
            int t = skill_calc(s, SC_AURALEN, lvl, &c);
            if (getenv("D2_AURA_DEBUG"))
                printf("  auras: Conversión sobre %d (%s): %d ticks, vivo %d\n", target, units_display_name(target), t,
                       units_alive_enemy(target));
            if (units_convert(target, (int)(p - s_pl), t)) {
                overlays_add("conversioncast", OV_AT_UNIT, target, ux, uy, 0);
                overlays_add("conversionaura", OV_AT_UNIT, target, ux, uy, t);
                audio_play_name("paladin_conversion", ux, uy);
            }
        }
        break;
    default:
        break;
    }
}

/* ---- Lo que dura ---- */

/* Celo: como Ametrallar, en el cuadro del golpe le pega al monstruo y, si le
   quedan golpes, rebobina la animación (Param2: el 100%) y sigue con otro
   que tenga pegado (o el mismo) */
static int zealing(const Player *p)
{
    return p->state == PS_ATTACK && !p->seq && p->charge_ticks <= 0 && p->attack_skill >= 0 &&
           (skills_get(p->cls, p->attack_skill)->kind == SK_ZEAL || skills_get(p->cls, p->attack_skill)->kind == SK_FURY);
}

static void zeal_tick(Player *p, const CharAnim *a)
{
    p->anim_acc += a->speed * (100 + attack_ias(p)) / 100;
    int hf = a->hit_frame > 0 ? a->hit_frame : charanim_frames(a) / 2;
    if (p->hits_done || (p->anim_acc >> 8) < hf)
        return;
    p->hits_done = 1;
    melee_strike(p, p->target);
    if (++p->seq_hits >= p->seq_loops)
        return;
    int ids[16], n = units_enemies_near(p->x, p->y, MELEE_RANGE + 0.5f, ids, 16), next = -1, others = 0;
    for (int k = 0; k < n; k++)
        if (ids[k] != p->target)
            others++;
    if (others > 0) {
        int pick = (int)(rnd() % (uint32_t)others);
        for (int k = 0; k < n && next < 0; k++)
            if (ids[k] != p->target && pick-- == 0)
                next = ids[k];
    } else if (units_alive_enemy(p->target)) {
        next = p->target;
    }
    if (next < 0)
        return;
    p->target = next;
    face_unit(p, next);
    p->anim_acc = 0;
    p->hits_done = 0;
}

/* Carga: corre hacia el monstruo a la velocidad de correr más Param1 % (y
   la de sus objetos); al llegar le pega */
static void charge_tick(Player *p)
{
    const CharClass *cl = &s_classes[p->cls];
    const SkillDef *s = skills_get(p->cls, p->attack_skill);
    if (!units_alive_enemy(p->target) || --p->charge_ticks <= 0) {
        p->charge_ticks = 0;
        set_player_state(p, PS_NORMAL, MODE_NU);
        return;
    }
    float ux, uy;
    units_position(p->target, 1.0f, &ux, &uy);
    float dx = ux - p->x, dy = uy - p->y, d = sqrtf(dx * dx + dy * dy);
    float reach = MELEE_RANGE + units_radius(p->target) - 0.45f;
    face_unit(p, p->target);
    if (d <= reach) { /* llegó: el golpe */
        int a2 = (rnd() & 1) && player_anim(p, MODE_A2)->ok;
        set_player_state(p, PS_ATTACK, a2 ? MODE_A2 : MODE_A1);
        p->hits_total = 1;
        p->hits_done = 0;
        audio_play_name(k_swing_sounds[weapon_hit_class(p)], p->x, p->y);
        return;
    }
    float step = cl->run_speed * RUN_BONUS / TICK_RATE * (100 + s->param[1] + p->bonus[STAT_MOVE] + p->au_speed) / 100.0f;
    if (step > d - reach + 0.05f)
        step = d - reach + 0.05f;
    /* De a medio subtile como mucho: a la velocidad del original la carga avanza más de uno por tick y si no
       atravesaba las paredes finas */
    int parts = (int)ceilf(step / 0.5f), moved = 0;
    parts = parts < 1 ? 1 : parts;
    for (int k = 0; k < parts; k++) {
        float nx = p->x + dx / d * step / parts, ny = p->y + dy / d * step / parts;
        int now = 0;
        if (!player_blocked(nx, p->y, p->x, p->y)) {
            p->x = nx;
            now = 1;
        }
        if (!player_blocked(p->x, ny, p->x, p->y)) {
            p->y = ny;
            now = 1;
        }
        if (!now)
            break;
        moved = 1;
    }
    if (!moved) { /* una pared o un monstruo en el camino: se termina */
        p->charge_ticks = 0;
        set_player_state(p, PS_NORMAL, MODE_NU);
        return;
    }
    p->moving = 1;
    p->anim_acc += (int)(player_anim(p, p->mode)->speed * RUN_BONUS * RUN_ANIM_PACE * 2.0f);
}

/* Escudo sagrado puesto ya (la prueba de Castigar: se usa con él) */
static void pal_holy_shield_now(Player *p)
{
    int hs = skills_find(p->cls, "Holy Shield");
    if (hs >= 0 && eff_lvl(p, hs) > 0)
        pal_effect(p, skills_get(p->cls, hs), eff_lvl(p, hs));
}

/* Termina todo lo que dura (al empezar una ronda de la prueba) */
static void pal_clear(Player *p)
{
    p->aura = -1;
    p->aura_nomana = 0;
    p->hs_ticks = p->fist_ticks = p->charge_ticks = 0;
    aura_reset(p);
}

static void pal_tick(Player *p)
{
    if (p->hs_ticks > 0)
        p->hs_ticks--;
    /* Puño de los cielos: el rayo sobre el monstruo y los rayos sagrados a
       los de alrededor (calc4, a hasta aurarangecalc subtiles) */
    if (p->fist_ticks > 0 && --p->fist_ticks == 0) {
        const SkillDef *s = skills_get(p->cls, p->fist_skill);
        int lvl = eff_lvl(p, p->fist_skill), lo, hi, t = p->fist_target;
        SkillCtx c = pctx(p);
        float x = p->fist_x, y = p->fist_y;
        if (units_alive_enemy(t))
            units_position(t, 1.0f, &x, &y);
        missiles_effect("handofgod", x, y);
        audio_play_name("paladin_handofgod_bolt", x, y);
        if (units_alive_enemy(t)) {
            overlays_add("handofgod", OV_AT_UNIT, t, x, y, 0);
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            int exp = units_damage(t, s_one_hit ? 1 << 20 : units_resist(t, ELEM_LTNG, roll(lo, hi)));
            if (exp > 0)
                award_kill(p, t, exp);
        }
        /* fistoftheheavensbolt: su daño por nivel y la sinergia de Rayo sagrado (15% por punto) */
        int hb = skills_find(p->cls, "Holy Bolt"), syn = hb >= 0 ? p->skill_lvl[hb] * 15 : 0;
        missiles_level_damage("fistoftheheavensbolt", lvl, &lo, &hi);
        lo = lo * (100 + syn) / 100;
        hi = hi * (100 + syn) / 100;
        int ids[32], n = units_enemies_near(x, y, (float)skill_calc(s, SC_AURARANGE, lvl, &c), ids, 32);
        int most = skill_calc(s, SC_CALC4, lvl, &c);
        for (int k = 0; k < n && k < most; k++) {
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            float dx = ux - x, dy = uy - y;
            if (dx * dx + dy * dy < 0.25f) { /* el mismo al que le cayó el rayo: hacia cualquier lado */
                float a = (float)(rnd() % 628) / 100.0f;
                dx = cosf(a);
                dy = sinf(a);
            }
            Missile *mi = spell_missile(p, "fistoftheheavensbolt", x, y, dx, dy, ELEM_MAG, lo, hi, 0);
            if (!mi)
                break;
            mi->flags |= MF_HOLY | MF_HEAL | MF_PIERCE;
            mi->burn_min = mi->burn_max = 0;
        }
    }
}

/* ---- Los proyectiles del Paladín (desde tick_missiles) ---- */

/* El Martillo: la próxima posición en la espiral (el centro queda donde lo tiró) */
static void hammer_tick(Missile *mi)
{
    float v = missiles_speed(mi); /* Vel 18 que acelera (Accel 250) hasta MaxVel 30 */
    float r = HAMMER_PITCH * mi->ang;
    mi->ang += v / sqrtf(r * r + HAMMER_PITCH * HAMMER_PITCH);
    if (mi->ang >= HAMMER_END) { /* terminó el camino */
        mi->ang = HAMMER_END;
        mi->life = 1;
    }
    r = HAMMER_PITCH * mi->ang;
    mi->vx = mi->mx + r * cosf(mi->ang) - mi->x;
    mi->vy = mi->my + r * sinf(mi->ang) - mi->y;
}

/* Rayo sagrado: cura a los otros jugadores que toca en su camino (una vez a cada uno) */
static void holy_heal(Missile *mi, const int (*cells)[2], int nc)
{
    if (mi->burn_max <= 0)
        return;
    for (int k = 0; k < s_num_players; k++) {
        Player *o = &s_pl[k];
        int c = 0;
        while (c < nc && !missiles_touches(missiles_size(mi), cells[c][0], cells[c][1], o->x, o->y, PLAYER_FOOT))
            c++;
        if (k == mi->owner || o->state >= PS_DYING || c == nc || already_hit(mi, -1 - k))
            continue;
        o->hp += roll(mi->burn_min, mi->burn_max);
        if (o->hp > o->hp_max)
            o->hp = o->hp_max;
        audio_play_name("paladin_holybolt_impact_1", o->x, o->y);
    }
}
