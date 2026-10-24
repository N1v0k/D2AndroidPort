/* ---- Las habilidades del Druida (se incluye desde game.c) ----

   Como en el original (SkillDruid.cpp y MissMode.cpp de D2MOO), con las
   tablas de D2R:
   - Formas: Hombre lobo y Hombre oso. Se ve como el lobo o el oso de
     monsters/40 y monsters/tg y suman lo de sus aurastat (vida, daño,
     defensa, puntería, velocidad de ataque, estamina) mientras duran
     (auralencalc, más Licantropía). Usar la misma otra vez la deshace. Las
     de restrict 2 (Furia salvaje, Rabia, Furia: lobo; Vapulear, Onda de
     choque: oso; Garras de fuego y Hambre: cualquiera) transforman solas;
     las de restrict vacío (los hechizos) la deshacen; las invocaciones se
     usan transformado.
   - Furia salvaje y Vapulear suman una carga por golpe que acierta (hasta
     calc2) y sus aurastat se calculan con el número de cargas: velocidad y
     robo de vida, o daño, aturdir y velocidad de ataque. Rabia envenena y
     contagia a los de alrededor; Garras de fuego, fuego; Hambre roba vida y
     maná; Furia, varios golpes seguidos (como Celo).
   - Invocaciones: cuervos (se van después de ln56 golpes), lobos
     espirituales (frío), lobos terribles, el oso pardo, las enredaderas (la
     venenosa pelea; la carroñera y la solar comen cadáveres y le dan vida o
     maná) y los espíritus (Sabio roble: vida; Corazón de lobezno: daño y
     puntería; Espíritu de púas: devuelve el daño), que se quedan al lado y
     dan lo suyo como un aura a los de alrededor.
   - Fuego: Tormenta de fuego (tres llamas que dejan fuego), Roca fundida
     (rueda, empuja y deja fuego), Fisura (grietas de fuego), Volcán (escupe
     rocas) y Armagedón (caen rocas alrededor un rato).
   - Viento: Ráfaga ártica (un chorro de frío), Armadura de ciclón (absorbe
     fuego, frío y rayo), Remolino (aturde), Tornado y Huracán (frío
     alrededor un rato). */

enum { PK_RAVEN = 10, PK_VINE, PK_SWOLF, PK_DWOLF, PK_GRIZZLY, PK_SPIRIT };
#define DRU_SPIRIT_FOLLOW 12.0f /* el espíritu se acerca si el Druida se aleja más */
#define DRU_VINE_EAT      (2 * TICK_RATE) /* la enredadera come un cadáver cada tanto */

static int dru_kind(int kind)
{
    return kind >= SK_RAVEN && kind <= SK_HURRICANE;
}

/* La forma que nombra el estado de skills.txt (State1, State2) */
static int dru_form_of(const char *state)
{
    return !strcmp(state, "wolf") ? FORM_WOLF : !strcmp(state, "bear") ? FORM_BEAR : FORM_NONE;
}

static void dru_damage(Player *p, int unit, int elem, int amount)
{
    if (!units_alive_enemy(unit) || amount <= 0)
        return;
    int exp = units_damage(unit, s_one_hit ? 1 << 20 : elem == ELEM_NONE ? units_resist(unit, ELEM_NONE, amount)
                                                                           : spell_resist((int)(p - s_pl), unit, elem, amount));
    if (exp > 0)
        award_kill(p, unit, exp);
}

/* ---- Las formas ---- */

/* Lo de la forma y las cargas: se recalcula al cambiar */
static void dru_refresh(Player *p)
{
    SkillCtx c = pctx(p);
    p->form_life = p->form_stam = 0;
    p->dru_dmg_pct = p->dru_def_pct = p->dru_ar_pct = p->dru_ias = p->dru_speed = p->dru_steal = p->dru_stun = 0;
    if (p->form != FORM_NONE && p->form_skill >= 0) {
        const SkillDef *s = skills_get(p->cls, p->form_skill);
        int lvl = eff_lvl(p, p->form_skill);
        for (int k = 0; k < 4; k++) {
            const char *stat = s->aurastat[k];
            int v = skill_calc(s, k < 3 ? SC_AURASTAT1 + k : SC_AURASTAT4, lvl, &c);
            if (!strcmp(stat, "skill_staminapercent"))
                p->form_stam += v;
            else if (!strcmp(stat, "attackrate"))
                p->dru_ias += v;
            else if (!strcmp(stat, "item_tohit_percent"))
                p->dru_ar_pct += v;
            else if (!strcmp(stat, "item_maxhp_percent"))
                p->form_life += v;
            else if (!strcmp(stat, "damagepercent"))
                p->dru_dmg_pct += v;
            else if (!strcmp(stat, "skill_armor_percent"))
                p->dru_def_pct += v;
        }
    }
    /* Furia salvaje y Vapulear: sus aurastat con el número de cargas como nivel */
    if (p->feral_charges > 0 && p->feral_skill >= 0) {
        const SkillDef *s = skills_get(p->cls, p->feral_skill);
        p->dru_speed += skill_calc(s, SC_AURASTAT1, p->feral_charges, &c);
        p->dru_steal += skill_calc(s, SC_AURASTAT2, p->feral_charges, &c);
    }
    if (p->maul_charges > 0 && p->maul_skill >= 0) {
        const SkillDef *s = skills_get(p->cls, p->maul_skill);
        p->dru_dmg_pct += skill_calc(s, SC_AURASTAT1, p->maul_charges, &c);
        p->dru_stun = skill_calc(s, SC_AURASTAT2, p->maul_charges, &c);
        p->dru_ias += skill_calc(s, SC_AURASTAT3, p->maul_charges, &c);
    }
}

/* La vida cambia con la forma: se mantiene el mismo porcentaje */
static void dru_restat(Player *p)
{
    int old_max = p->hp_max > 0 ? p->hp_max : 1, old_hp = p->hp;
    dru_refresh(p);
    refresh_stats(p);
    if (p->state < PS_DYING) {
        p->hp = (int)((int64_t)old_hp * p->hp_max / old_max);
        if (p->hp < 1 && old_hp > 0)
            p->hp = 1;
        if (p->hp > p->hp_max)
            p->hp = p->hp_max;
    }
}

static void charge_overlay(Player *p, const char *name, int n, int ticks)
{
    int me = (int)(p - s_pl);
    overlays_remove(name, OV_AT_PLAYER, me);
    if (n > 0) {
        char ov[24];
        snprintf(ov, sizeof ov, "%s%d", name, n > 5 ? 5 : n);
        overlays_add(ov, OV_AT_PLAYER, me, p->x, p->y, ticks);
    }
}

static void dru_unmorph(Player *p)
{
    if (p->form == FORM_NONE)
        return;
    int me = (int)(p - s_pl);
    overlays_add(p->form == FORM_WOLF ? "wolf_undo" : "bear_undo", OV_AT_PLAYER, me, p->x, p->y, 0);
    audio_play_name("druid_morph", p->x, p->y);
    p->form = FORM_NONE;
    p->form_ticks = 0;
    p->feral_charges = p->maul_charges = 0;
    charge_overlay(p, "feralrage", 0, 0);
    charge_overlay(p, "maul", 0, 0);
    dru_restat(p);
    start_look(p);
}

/* Se transforma con esa habilidad (Hombre lobo u Hombre oso); si ya es esa
   forma, vuelve a ser humano */
static int dru_morph(Player *p, int skill)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int form = s->kind == SK_WEREWOLF ? FORM_WOLF : FORM_BEAR, me = (int)(p - s_pl);
    if (p->form == form) {
        dru_unmorph(p);
        return 1;
    }
    if (p->form != FORM_NONE) { /* de una forma a la otra: las cargas se pierden */
        p->feral_charges = p->maul_charges = 0;
        charge_overlay(p, "feralrage", 0, 0);
        charge_overlay(p, "maul", 0, 0);
    }
    SkillCtx c = pctx(p);
    p->form = form;
    p->form_skill = skill;
    p->form_ticks = skill_calc(s, SC_AURALEN, eff_lvl(p, skill), &c);
    if (p->form_ticks <= 0)
        p->form_ticks = 40 * TICK_RATE;
    overlays_add(form == FORM_WOLF ? "wolf_into" : "bear_into", OV_AT_PLAYER, me, p->x, p->y, 0);
    audio_play_name("druid_morph", p->x, p->y);
    dru_restat(p);
    start_look(p);
    return 1;
}

/* ¿Puede usarla en la forma en que está? (restrict 2 de skills.txt) */
static int dru_form_ok(const Player *p, const SkillDef *s)
{
    if (s->restrict_form != 2)
        return 1;
    return p->form != FORM_NONE && (dru_form_of(s->state1) == p->form || dru_form_of(s->state2) == p->form);
}

/* Las de restrict 2 en otra forma: se transforma sola (gasta el maná de la
   forma); 0 si no aprendió ninguna que sirva */
static int dru_auto_morph(Player *p, const SkillDef *s)
{
    int forms[2] = { dru_form_of(s->state1), dru_form_of(s->state2) };
    for (int k = 0; k < 2; k++) {
        if (forms[k] == FORM_NONE)
            continue;
        int fs = skills_find(p->cls, forms[k] == FORM_WOLF ? "Wearwolf" : "Wearbear");
        if (fs < 0 || eff_lvl(p, fs) <= 0)
            continue;
        int cost = skill_mana(skills_get(p->cls, fs), eff_lvl(p, fs));
        if (p->mana < cost)
            continue;
        p->mana -= cost;
        return dru_morph(p, fs);
    }
    show_message(p, forms[0] == FORM_BEAR ? "Necesitás la forma de oso" : "Necesitás la forma de lobo");
    return 0;
}

/* El % de daño de más de los golpes del Druida (calc1 de cada una) */
static int dru_strike_pct(const Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    switch (s->kind) {
    case SK_FERAL_RAGE: /* ln56 */
    case SK_HUNGER:     /* par5: -75% */
        return skill_calc(s, SC_CALC1, lvl, &c);
    case SK_FURY:       /* ln34 */
        return skill_calc(s, SC_CALC2, lvl, &c);
    default:
        return 0;
    }
}

/* ---- Las invocaciones ---- */

/* Lo que la habilidad le suma al esbirro (sus passivestat y aurastat) */
static void dru_pet_stat(PetSpec *ps, const char *stat, int v)
{
    if (!strcmp(stat, "item_armor_percent"))
        ps->def_pct += v;
    else if (!strcmp(stat, "tohit"))
        ps->ar_add += v;
    else if (!strcmp(stat, "damagepercent"))
        ps->dmg_pct += v;
    else if (!strcmp(stat, "coldmindam"))
        ps->hit_emin = v;
    else if (!strcmp(stat, "coldmaxdam"))
        ps->hit_emax = v;
    else if (!strcmp(stat, "coldlength"))
        ps->hit_elen = v;
    else if (!strcmp(stat, "fireresist") && (ps->res == 0 || v < ps->res))
        ps->res = v;
}

static int dru_pet_kind(int kind)
{
    switch (kind) {
    case SK_RAVEN:          return PK_RAVEN;
    case SK_POISON_CREEPER:
    case SK_CARRION_VINE:
    case SK_SOLAR_CREEPER:  return PK_VINE;
    case SK_SPIRIT_WOLF:    return PK_SWOLF;
    case SK_DIRE_WOLF:      return PK_DWOLF;
    case SK_GRIZZLY:        return PK_GRIZZLY;
    default:                return PK_SPIRIT; /* los espíritus: uno a la vez, de cualquier tipo */
    }
}

static int dru_summon(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl), kind = dru_pet_kind(s->kind);
    PetSpec ps;
    memset(&ps, 0, sizeof ps);
    /* El nivel: calc2 (el del Druida; los cuervos y las enredaderas, otro) */
    int pl = skill_has(s, SC_CALC2) ? skill_calc(s, SC_CALC2, lvl, &c) : p->level_num;
    ps.level = pl < 1 ? 1 : pl > 99 ? 99 : pl;
    ps.hp_pct = skill_calc(s, SC_CALC1, lvl, &c);
    ps.monlvl = kind == PK_SWOLF || kind == PK_DWOLF || kind == PK_GRIZZLY || kind == PK_RAVEN;
    for (int k = 0; k < 5; k++) {
        if (k < 3 && s->aurastat[k][0])
            dru_pet_stat(&ps, s->aurastat[k], skill_calc(s, SC_AURASTAT1 + k, lvl, &c));
        if (s->passivestat[k][0])
            dru_pet_stat(&ps, s->passivestat[k], skill_calc(s, k < 3 ? SC_PASSIVE1 + k : SC_PASSIVE4 + k - 3, lvl, &c));
    }
    if (ps.hit_emax > 0)
        ps.hit_elem = ELEM_COLD;
    /* Su daño: el de la habilidad (MinDam..MaxDam con sus niveles y sinergias) con el % de más */
    int lo = 0, hi = 0;
    skill_phys_syn(s, lvl, &c, &lo, &hi);
    if (hi > 0) {
        ps.dmg_min = lo * (100 + ps.dmg_pct) / 100;
        ps.dmg_max = hi * (100 + ps.dmg_pct) / 100;
    } else if (kind == PK_SWOLF) { /* el lobo espiritual: casi todo es el frío */
        ps.dmg_min = 1;
        ps.dmg_max = 3;
    }
    ps.ar_add += skill_tohit(s, lvl); /* la puntería de la habilidad (ToHit, LevToHit) */
    float x = p->x + 1.0f, y = p->y + 1.0f;
    switch (s->kind) {
    case SK_RAVEN: /* se van después de ln56 golpes */
        ps.hits = skill_ln(s, 5, 6, lvl);
        break;
    case SK_POISON_CREEPER: { /* veneno en sus golpes */
        int plo, phi;
        skill_poison_syn(s, lvl, &c, &plo, &phi);
        ps.hit_elem = ELEM_POIS;
        ps.hit_emin = plo;
        ps.hit_emax = phi;
        ps.hit_elen = skill_elen(s, lvl);
        ps.dmg_min = 1;
        ps.dmg_max = 2;
        ps.reach_add = 0.8f; /* MeleeRng 2 */
        break;
    }
    case SK_CARRION_VINE:
    case SK_SOLAR_CREEPER:
        ps.still = 1; /* no pelea: come cadáveres */
        break;
    case SK_OAK_SAGE:
    case SK_HEART_WOLVERINE:
    case SK_SPIRIT_BARBS:
        ps.still = 1;
        ps.res = 0;
        break;
    default:
        break;
    }
    find_free_spot(&x, &y);
    int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, &c) : 1;
    if (kind == PK_VINE || kind == PK_SPIRIT) /* una enredadera y un espíritu, de cualquier tipo */
        for (int k = 0; k < NEC_PETS; k++)
            if (p->pet_kind[k] == kind)
                nec_pet_drop(p, k);
    int slot = nec_pet_slot(p, kind, most);
    if (slot < 0)
        return 0;
    int u = units_pet_summon(me, s->summon, x, y, &ps);
    if (u < 0)
        return 0;
    nec_pet_keep(p, slot, u, kind);
    if (kind == PK_VINE)
        p->vine_skill = (int)(s - skills_get(p->cls, 0));
    if (kind == PK_SPIRIT) {
        p->spirit_skill = (int)(s - skills_get(p->cls, 0));
        p->spirit_unit = u;
    }
    return 1;
}

static int dru_pet(const Player *p, int kind)
{
    for (int k = 0; k < NEC_PETS; k++)
        if (p->pet_kind[k] == kind && units_pet_alive(p->pets[k]))
            return p->pets[k];
    return -1;
}

/* Los espíritus: lo que dan a los jugadores de alrededor (como un aura) */
static void dru_spirit_give(Player *o)
{
    for (int ci = 0; ci < s_num_players; ci++) {
        const Player *c = &s_pl[ci];
        int u = c->spirit_unit;
        if (c->cls != 6 || !units_pet_alive(u) || c->spirit_skill < 0)
            continue;
        const SkillDef *s = skills_get(c->cls, c->spirit_skill);
        int lvl = eff_lvl(c, c->spirit_skill);
        SkillCtx x = pctx(c);
        float sx, sy;
        units_position(u, 1.0f, &sx, &sy);
        float r = (float)skill_ln(s, 7, 8, lvl), dx = o->x - sx, dy = o->y - sy;
        if (lvl <= 0 || dx * dx + dy * dy > r * r)
            continue;
        switch (s->kind) {
        case SK_OAK_SAGE: { /* + ln12 % de vida */
            int v = skill_ln(s, 1, 2, lvl);
            if (v > o->au_life_pct)
                o->au_life_pct = v;
            break;
        }
        case SK_HEART_WOLVERINE: /* + ln56 % de daño y + ln34 % de puntería */
            o->au_dmg_pct += skill_ln(s, 5, 6, lvl);
            o->au_ar_pct += skill_ln(s, 3, 4, lvl);
            break;
        case SK_SPIRIT_BARBS: { /* el que pega recibe pnma de daño */
            int lo, hi;
            skill_phys_syn(s, lvl, &x, &lo, &hi);
            o->au_thorns_flat += lo;
            break;
        }
        default:
            break;
        }
    }
}

/* ---- Al lanzarla ---- */

/* 1 si arrancó (gasta el maná) */
static int dru_start(Player *p, int skill, int cost)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    SkillCtx c = pctx(p);
    int in_town = p->level && p->level->town;
    p->cast_target = -1;
    /* Las formas: las de lobo u oso transforman solas; los hechizos la deshacen */
    if (s->restrict_form == 2 && !dru_form_ok(p, s) && !dru_auto_morph(p, s))
        return 0;
    if (p->mana < cost) { /* la forma se llevó el maná: queda transformado y listo */
        show_message(p, "No tenés suficiente maná");
        return 0;
    }
    if (s->restrict_form == 0 && p->form != FORM_NONE && !s->passive)
        dru_unmorph(p);
    switch (s->kind) {
    case SK_FERAL_RAGE:
    case SK_MAUL:
    case SK_FIRE_CLAWS:
        start_attack(p, skill);
        p->mana -= cost;
        return 1;
    case SK_RABIES: /* el mordisco (S3) */
    case SK_HUNGER:
        start_attack(p, skill);
        if (player_anim(p, MODE_S3)->ok) {
            p->mode = MODE_S3;
            p->anim_acc = 0;
        }
        p->mana -= cost;
        return 1;
    case SK_FURY: /* varios golpes rebobinando la animación (como Celo) */
        start_attack(p, skill);
        p->seq_loops = skill_calc(s, SC_CALC1, lvl, &c);
        p->seq_hits = 0;
        p->mana -= cost;
        return 1;
    case SK_ARCTIC_BLAST: /* con la secuencia del original (S1) */
        pal_aim(p, AIM_RANGE);
        if (in_town) {
            show_message(p, "En el pueblo no se pelea");
            return 0;
        }
        if (s->seqnum > 0 && start_seq(p, skill)) {
            play_skill_sound(p, s);
            p->mana -= cost;
            return 1;
        }
        break;
    default:
        pal_aim(p, AIM_RANGE);
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

/* ---- El efecto (en el cuadro del lanzamiento) ---- */

/* Varios proyectiles hacia el monstruo: el primero derecho y los demás un
   poco abiertos (como SKILLS_MissileInit_ChargedBolt) */
static int dru_spread(Player *p, const char *name, int n, int elem, int lo, int hi, int flags, float open, int count)
{
    float ax, ay;
    spell_aim(p, &ax, &ay);
    float base = atan2f(ay, ax);
    int made = 0, group = hit_group_new();
    for (int k = 0; k < n && k < 8; k++) {
        float a = k == 0 ? base : base + ((float)(rnd() % 200) / 100.0f - 1.0f) * open;
        Missile *mi = spell_missile(p, name, p->x, p->y, cosf(a), sinf(a), elem, lo, hi, 0);
        if (!mi)
            break;
        mi->flags |= flags;
        mi->count = count;
        if (flags & MF_GROUP)
            mi->group = group;
        made++;
    }
    return made;
}

/* Una roca que cae del cielo (Armagedón, el Volcán): daña al caer */
static void dru_rock(Player *p, const char *name, float x, float y, int dlo, int dhi, int lo, int hi, float r, float z)
{
    Missile *mi = spell_missile(p, name, x, y, 0.0f, 0.0f, ELEM_FIRE, lo, hi, 0);
    if (!mi)
        return;
    mi->flags |= MF_EFFECT | MF_LAND | MF_NOHIT | MF_CHAIN;
    mi->dmin = dlo;
    mi->dmax = dhi;
    mi->z = z;
    mi->vz = z / 12.0f;
    mi->life = 14;
    mi->radius10 = (int)(r * 10.0f);
}

static int dru_effect(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl), lo = 0, hi = 0, plo = 0, phi = 0;
    float tx = p->cast_x, ty = p->cast_y;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    if (s->summon[0])
        return dru_summon(p, s, lvl);
    switch (s->kind) {
    case SK_WEREWOLF:
    case SK_WEREBEAR:
        return dru_morph(p, (int)(s - skills_get(p->cls, 0)));
    case SK_FIRESTORM: { /* calc1 llamas que dejan fuego por el piso (firestorm, por segundo) */
        spell_burn(p, s, lvl, &lo, &hi);
        float ax, ay;
        spell_aim(p, &ax, &ay);
        float base = atan2f(ay, ax);
        int n = skill_calc(s, SC_CALC1, lvl, &c), made = 0;
        n = n < 1 ? 1 : n > 6 ? 6 : n;
        for (int k = 0; k < n; k++) {
            float a = k == 0 ? base : base + ((float)(rnd() % 200) / 100.0f - 1.0f) * 0.35f;
            Missile *mi = spell_missile(p, "firestormmaker", p->x, p->y, cosf(a), sinf(a), ELEM_FIRE, 0, 0, 0);
            if (!mi)
                break;
            mi->flags |= MF_MAKER | MF_NOHIT;
            mi->trail = "firestorm";
            mi->burn_min = lo;
            mi->burn_max = hi;
            mi->burn_life = 25;
            mi->mx = mi->my = -1000.0f;
            made++;
        }
        return made;
    }
    case SK_MOLTEN_BOULDER: { /* rueda atravesando, empuja y deja fuego (moltenboulderfirepath) */
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        float ax, ay;
        spell_aim(p, &ax, &ay);
        missiles_effect("moltenboulderemerge", p->x, p->y);
        Missile *mi = spell_missile(p, "moltenboulder", p->x, p->y, ax, ay, ELEM_FIRE, lo, hi, 0);
        if (!mi)
            return 0;
        mi->dmin = plo;
        mi->dmax = phi;
        mi->flags |= MF_PIERCE | MF_KNOCK | MF_MAKER;
        mi->trail = "moltenboulderfirepath";
        /* El fuego del camino: el de missiles.txt (HitShift 2) por segundo, con Tormenta de fuego */
        int flo, fhi, fs = skills_find(p->cls, "Firestorm");
        missiles_level_damage("moltenboulderfirepath", lvl, &flo, &fhi);
        int syn = fs >= 0 ? p->skill_lvl[fs] * 8 : 0;
        mi->burn_min = (flo << 2) * TICK_RATE / 256 * (100 + syn) / 100 + 1;
        mi->burn_max = (fhi << 2) * TICK_RATE / 256 * (100 + syn) / 100 + 1;
        mi->burn_life = 50;
        mi->mx = mi->my = -1000.0f;
        return 1;
    }
    case SK_FISSURE: /* grietas de fuego alrededor del lugar (calc1 de radio, cada calc2 cuadros) */
        p->fiss_ticks = 80;
        p->fiss_next = 0;
        p->fiss_x = tx;
        p->fiss_y = ty;
        p->fiss_skill = (int)(s - skills_get(p->cls, 0));
        return 1;
    case SK_VOLCANO: /* el volcán en el lugar: escupe rocas un rato */
        p->volc_ticks = 125;
        p->volc_next = 10;
        p->volc_x = tx;
        p->volc_y = ty;
        p->volc_skill = (int)(s - skills_get(p->cls, 0));
        missiles_effect("volcano", tx, ty);
        missiles_effect("volcano overlay fire", tx, ty);
        return 1;
    case SK_ARMAGEDDON:
    case SK_HURRICANE: { /* duran auralencalc; cada par4 cuadros, rocas o frío */
        int len = skill_calc(s, SC_AURALEN, lvl, &c);
        if (s->kind == SK_ARMAGEDDON) {
            p->arma_ticks = len > 0 ? len : 250;
            p->arma_skill = (int)(s - skills_get(p->cls, 0));
            p->arma_next = 0;
        } else {
            p->hurr_ticks = len > 0 ? len : 250;
            p->hurr_skill = (int)(s - skills_get(p->cls, 0));
            p->hurr_next = 0;
            audio_play_name("druid_hurricane", p->x, p->y);
        }
        return 1;
    }
    case SK_CYCLONE_ARMOR: /* absorbe fuego, frío y rayo hasta aurastat1 (en 1/256) */
        p->cyclone = p->cyclone_max = skill_calc(s, SC_AURASTAT1, lvl, &c) / 256;
        overlays_remove("cyclonearmor", OV_AT_PLAYER, me);
        return 1;
    case SK_TWISTER: { /* calc1 remolinos que atraviesan y aturden (calc2 cuadros) */
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        int stun = skill_calc(s, SC_CALC2, lvl, &c);
        return dru_spread(p, "twister", skill_calc(s, SC_CALC1, lvl, &c), ELEM_NONE, plo, phi, MF_PIERCE | MF_STUN, 0.3f,
                          stun > 0 ? stun : 10);
    }
    case SK_TORNADO: /* va dando vueltas y daña a los que toca */
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        return dru_spread(p, "tornado", 1, ELEM_NONE, plo, phi, MF_PIERCE | MF_WOBBLE, 0.0f, 0);
    case SK_SHOCK_WAVE: { /* una onda alrededor que aturde (calc4 cuadros) */
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        int group = hit_group_new(), stun = skill_calc(s, SC_CALC4, lvl, &c);
        for (int k = 0; k < 16; k++) {
            float a = (float)k * 2.0f * PI_F / 16.0f;
            Missile *mi = spell_missile(p, "shockwave", p->x, p->y, cosf(a), sinf(a), ELEM_NONE, plo, phi, 0);
            if (!mi)
                break;
            mi->flags |= MF_GROUP | MF_PIERCE | MF_STUN;
            mi->group = group;
            mi->count = stun > 0 ? stun : 25;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* Ráfaga ártica: el golpe de la secuencia arranca el chorro */
static void dru_seq_hit(Player *p, const SkillDef *s, int lvl)
{
    (void)lvl;
    if (s->kind != SK_ARCTIC_BLAST)
        return;
    p->arctic_ticks = TICK_RATE * 2 / 3;
    p->arctic_skill = (int)(s - skills_get(p->cls, 0));
    overlays_add("arctic_blast_cast", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
}

/* ---- Después de un golpe cuerpo a cuerpo que acertó ---- */

static void dru_after_hit(Player *p, const SkillDef *s, int lvl, int target, int phys, int killed)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl);
    switch (s->kind) {
    case SK_FERAL_RAGE:
    case SK_MAUL: { /* una carga más (hasta calc2): sus aurastat con las cargas como nivel */
        int most = skill_calc(s, SC_CALC2, lvl, &c), len = skill_calc(s, SC_AURALEN, lvl, &c);
        int *n = s->kind == SK_FERAL_RAGE ? &p->feral_charges : &p->maul_charges;
        if (*n < most)
            (*n)++;
        if (s->kind == SK_FERAL_RAGE) {
            p->feral_ticks = len > 0 ? len : 500;
            p->feral_skill = (int)(s - skills_get(p->cls, 0));
            charge_overlay(p, "feralrage", *n, p->feral_ticks);
        } else {
            p->maul_ticks = len > 0 ? len : 500;
            p->maul_skill = (int)(s - skills_get(p->cls, 0));
            charge_overlay(p, "maul", *n, p->maul_ticks);
        }
        dru_refresh(p);
        break;
    }
    case SK_RABIES: { /* veneno que contagia a los de alrededor */
        int lo, hi, len = skill_elen(s, lvl);
        skill_poison_syn(s, lvl, &c, &lo, &hi);
        float ux, uy;
        units_position(target, 1.0f, &ux, &uy);
        int ids[16], n = units_enemies_near(ux, uy, 4.0f, ids, 16);
        for (int k = 0; k < n; k++) {
            if (ids[k] == target && killed)
                continue;
            if (units_poison(ids[k], roll(lo, hi), len > 0 ? len : 100, me)) {
                float x, y;
                units_position(ids[k], 1.0f, &x, &y);
                overlays_add("rabiesplague", OV_AT_UNIT, ids[k], x, y, len > 0 ? len : 100);
            }
        }
        break;
    }
    case SK_HUNGER: /* roba dm12 % de vida y dm34 % de maná */
        p->hp += phys * skill_calc(s, SC_CALC2, lvl, &c) / 100;
        p->mana += phys * skill_calc(s, SC_CALC3, lvl, &c) / 100;
        break;
    default:
        break;
    }
}

/* Cada golpe cuerpo a cuerpo con las cargas puestas: Vapulear aturde y Furia salvaje roba vida */
static void dru_hit_effects(Player *p, int target, int phys, int killed)
{
    if (p->dru_steal > 0)
        p->hp += phys * p->dru_steal / 100;
    if (!killed && p->dru_stun > 0 && units_alive_enemy(target)) {
        float x, y;
        units_stun(target, p->dru_stun);
        units_position(target, 1.0f, &x, &y);
        overlays_add("stun", OV_AT_UNIT, target, x, y, p->dru_stun);
    }
}

/* ---- Lo que dura ---- */

/* Lo que vuela alrededor en el Huracán: gira alrededor del Druida */
static void dru_orbit_tick(Missile *mi)
{
    if (mi->owner < 0 || mi->owner >= s_num_players)
        return;
    const Player *p = &s_pl[mi->owner];
    float r = mi->mx > 0.5f ? mi->mx : 0.5f;
    mi->ang += 0.55f / r; /* unos 0,55 subtiles por tick */
    mi->vx = p->x + r * cosf(mi->ang) - mi->x;
    mi->vy = p->y + r * sinf(mi->ang) - mi->y;
}

/* La Armadura de ciclón se lleva el fuego, el frío y el rayo; devuelve lo que queda */
static int cyclone_absorb(Player *p, int dmg)
{
    if (p->cyclone <= 0 || dmg <= 0)
        return dmg;
    int take = dmg < p->cyclone ? dmg : p->cyclone;
    p->cyclone -= take;
    if (p->cyclone <= 0)
        overlays_remove("cyclonearmor", OV_AT_PLAYER, (int)(p - s_pl));
    return dmg - take;
}

static void dru_tick(Player *p)
{
    if (p->cls != 6)
        return;
    int me = (int)(p - s_pl), in_town = p->level && p->level->town;
    SkillCtx c = pctx(p);
    /* La forma se termina */
    if (p->form != FORM_NONE && --p->form_ticks <= 0)
        dru_unmorph(p);
    if (p->feral_charges > 0 && --p->feral_ticks <= 0) {
        p->feral_charges = 0;
        charge_overlay(p, "feralrage", 0, 0);
        dru_refresh(p);
    }
    if (p->maul_charges > 0 && --p->maul_ticks <= 0) {
        p->maul_charges = 0;
        charge_overlay(p, "maul", 0, 0);
        dru_refresh(p);
    }
    /* Armadura de ciclón: se ve según lo que le queda */
    if (p->cyclone > 0 && s_ticks % 4 == 0) {
        int k = p->cyclone * 3 > p->cyclone_max * 2 ? 1 : p->cyclone * 3 > p->cyclone_max ? 2 : 3;
        char ov[24];
        snprintf(ov, sizeof ov, "cyclonearmor%dfront", k);
        overlays_add(ov, OV_AT_PLAYER, me, p->x, p->y, 6);
    }
    /* Ráfaga ártica: un chorro de frío hacia el monstruo; daña por segundo y enfría */
    if (p->arctic_ticks > 0) {
        p->arctic_ticks--;
        const SkillDef *s = skills_get(p->cls, p->arctic_skill);
        int lvl = eff_lvl(p, p->arctic_skill), lo, hi;
        skill_elem_rate(s, lvl, &c, 12, &lo, &hi); /* enms*12/256 por segundo, como su descripción */
        float ax, ay;
        spell_aim(p, &ax, &ay);
        float l = sqrtf(ax * ax + ay * ay);
        ax /= l > 0.0f ? l : 1.0f;
        ay /= l > 0.0f ? l : 1.0f;
        float range = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
        range = range < 3.0f ? 3.0f : range > 12.0f ? 12.0f : range;
        Missile *f = missiles_spawn(p->arctic_ticks % 2 ? "arcticblast1" : "arcticblast2", p->x + ax * 0.6f,
                                    p->y + ay * 0.6f, ax, ay);
        if (f) {
            f->flags |= MF_NOHIT;
            f->owner = me;
            f->life = (int)(range / 0.4f);
        }
        int ids[16], n = units_enemies_near(p->x + ax * range / 2.0f, p->y + ay * range / 2.0f, range / 2.0f + 0.8f, ids, 16);
        int elen = skill_elen_syn(s, lvl, &c);
        for (int k = 0; k < n; k++) {
            int full = roll(lo, hi), d = full / TICK_RATE + ((int)(rnd() % TICK_RATE) < full % TICK_RATE);
            dru_damage(p, ids[k], ELEM_COLD, d);
            if (units_alive_enemy(ids[k]) && elen > 0)
                units_chill(ids[k], elen);
        }
    }
    /* Fisura: una grieta cada calc2 cuadros, a hasta calc1 del lugar */
    if (p->fiss_ticks > 0) {
        p->fiss_ticks--;
        if (--p->fiss_next <= 0) {
            const SkillDef *s = skills_get(p->cls, p->fiss_skill);
            int lvl = eff_lvl(p, p->fiss_skill), lo, hi, every = skill_calc(s, SC_CALC2, lvl, &c);
            float r = (float)skill_calc(s, SC_CALC1, lvl, &c) * 2.0f / 3.0f;
            p->fiss_next = every > 0 ? every : 6;
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            float a = (float)(rnd() % 628) / 100.0f, d = r * (float)(rnd() % 100) / 100.0f;
            float x = p->fiss_x + cosf(a) * d, y = p->fiss_y + sinf(a) * d;
            if (!map_blocked(&s_map, (int)floorf(x), (int)floorf(y))) {
                Missile *m = missiles_effect("erruption crack 1", x, y);
                if (m)
                    m->flags |= MF_NOHIT;
                int ids[16], n = units_enemies_near(x, y, 1.6f, ids, 16);
                for (int k = 0; k < n; k++) {
                    float ux, uy;
                    units_position(ids[k], 1.0f, &ux, &uy);
                    overlays_add("fire_hit", OV_AT_UNIT, ids[k], ux, uy, 0);
                    dru_damage(p, ids[k], ELEM_FIRE, roll(lo, hi));
                }
            }
        }
    }
    /* Volcán: escupe rocas que caen alrededor */
    if (p->volc_ticks > 0) {
        p->volc_ticks--;
        if (--p->volc_next <= 0) {
            p->volc_next = 6;
            const SkillDef *s = skills_get(p->cls, p->volc_skill);
            int lvl = eff_lvl(p, p->volc_skill), lo, hi, plo, phi;
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            skill_phys_syn(s, lvl, &c, &plo, &phi);
            float a = (float)(rnd() % 628) / 100.0f, d = 0.5f + 3.5f * (float)(rnd() % 100) / 100.0f;
            float x = p->volc_x + cosf(a) * d, y = p->volc_y + sinf(a) * d;
            if (!map_blocked(&s_map, (int)floorf(x), (int)floorf(y)))
                dru_rock(p, "volcano debris 2", x, y, plo, phi, lo, hi, 1.5f, 90.0f);
        }
    }
    /* Armagedón: cada par4 cuadros una roca al azar a hasta par3 */
    if (p->arma_ticks > 0) {
        p->arma_ticks--;
        if (in_town) {
            p->arma_ticks = 0;
        } else if (--p->arma_next <= 0) {
            const SkillDef *s = skills_get(p->cls, p->arma_skill);
            int lvl = eff_lvl(p, p->arma_skill), lo, hi, plo, phi;
            p->arma_next = s->param[4] > 0 ? s->param[4] : 6;
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            skill_phys_syn(s, lvl, &c, &plo, &phi);
            float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
            r = r < 3.0f ? 3.0f : r > 12.0f ? 12.0f : r;
            float x = p->x + ((float)(rnd() % 1000) / 500.0f - 1.0f) * r, y = p->y + ((float)(rnd() % 1000) / 500.0f - 1.0f) * r;
            if (!map_blocked(&s_map, (int)floorf(x), (int)floorf(y)))
                dru_rock(p, "armageddonrock", x, y, plo, phi, lo, hi, 2.5f, 160.0f);
        }
    }
    /* Huracán: cada par4 cuadros, frío a los de alrededor (a hasta par3) */
    if (p->hurr_ticks > 0) {
        p->hurr_ticks--;
        const SkillDef *s = skills_get(p->cls, p->hurr_skill);
        int lvl = eff_lvl(p, p->hurr_skill);
        float r = (float)skill_calc(s, SC_AURARANGE, lvl, &c);
        r = r < 3.0f ? 3.0f : r > 12.0f ? 12.0f : r;
        if (in_town) {
            p->hurr_ticks = 0;
        } else {
            if (s_ticks % 3 == 0) { /* lo que vuela alrededor */
                static const char *const k_bits[5] = { "hurricaneswoosh", "hurricanerock", "hurricanecart",
                                                       "hurricanesack", "hurricanetree" };
                float a = (float)(rnd() % 628) / 100.0f, d = 1.5f + (r - 1.5f) * (float)(rnd() % 100) / 100.0f;
                Missile *m = missiles_spawn(k_bits[rnd() % 8 < 5 ? 0 : 1 + rnd() % 4], p->x + cosf(a) * d,
                                            p->y + sinf(a) * d, -sinf(a), cosf(a));
                if (m) {
                    m->flags |= MF_NOHIT | MF_ORBIT;
                    m->owner = me;
                    m->mx = d;
                    m->ang = a;
                    m->life = 30;
                }
            }
            if (--p->hurr_next <= 0) {
                p->hurr_next = s->param[4] > 0 ? s->param[4] : 20;
                int lo, hi, elen = skill_elen(s, lvl);
                skill_elem_syn(s, lvl, &c, &lo, &hi);
                int ids[32], n = units_enemies_near(p->x, p->y, r, ids, 32);
                for (int k = 0; k < n; k++) {
                    dru_damage(p, ids[k], ELEM_COLD, roll(lo, hi));
                    if (units_alive_enemy(ids[k]) && elen > 0)
                        units_chill(ids[k], elen);
                }
            }
        }
    }
    /* El espíritu se queda cerca del Druida */
    if (p->spirit_unit >= 0) {
        if (!units_pet_alive(p->spirit_unit)) {
            p->spirit_unit = -1;
        } else {
            float sx, sy;
            units_position(p->spirit_unit, 1.0f, &sx, &sy);
            float dx = sx - p->x, dy = sy - p->y;
            if (dx * dx + dy * dy > DRU_SPIRIT_FOLLOW * DRU_SPIRIT_FOLLOW) {
                float x = p->x + 1.0f, y = p->y - 1.0f;
                find_free_spot(&x, &y);
                units_pet_place(p->spirit_unit, x, y);
            }
        }
    }
    /* Las lianas no tienen dibujo propio (los de monsters/k9 son cuadros
       vacíos): se ven por el rastro de lianas que dejan donde están, como en
       el original (vines trail, plague vines trail). Si no se movió, el
       rastro se renueva ya crecido */
    {
        int v = dru_pet(p, PK_VINE);
        if (v >= 0 && p->vine_skill >= 0) {
            float vx, vy;
            units_position(v, 1.0f, &vx, &vy);
            float dx = vx - p->vine_tx, dy = vy - p->vine_ty;
            int moved = dx * dx + dy * dy > 0.8f;
            if (moved || --p->vine_draw <= 0) {
                int poison = skills_get(p->cls, p->vine_skill)->kind == SK_POISON_CREEPER;
                Missile *m = missiles_effect(poison ? "plague vines trail" : "vines trail", vx, vy);
                if (m) {
                    m->flags |= MF_NOHIT;
                    m->life = 3 * TICK_RATE;
                    if (!moved)
                        m->age = 11; /* (12 cuadros: el último) */
                }
                p->vine_tx = vx;
                p->vine_ty = vy;
                p->vine_draw = 3 * TICK_RATE - 8;
            }
        }
    }
    /* Las enredaderas carroñera y solar: comen un cadáver de cerca si le falta vida o maná */
    if (--p->vine_next <= 0) {
        p->vine_next = DRU_VINE_EAT;
        int v = dru_pet(p, PK_VINE);
        const SkillDef *s = v >= 0 && p->vine_skill >= 0 ? skills_get(p->cls, p->vine_skill) : NULL;
        int mana = s && s->kind == SK_SOLAR_CREEPER;
        if (s && (s->kind == SK_CARRION_VINE || mana) && (mana ? p->mana < p->mana_max : p->hp < p->hp_max)) {
            float vx, vy;
            units_position(v, 1.0f, &vx, &vy);
            int ids[16], n = units_corpses_near(vx, vy, 10.0f, ids, 16);
            if (n > 0) {
                float cx, cy;
                units_position(ids[0], 1.0f, &cx, &cy);
                units_pet_place(v, cx, cy);
                units_consume_corpse(ids[0]);
                missiles_effect("corpseexplosion", cx, cy);
                int lvl = eff_lvl(p, p->vine_skill), pct = skill_ln(s, 5, 6, lvl);
                if (mana) {
                    p->mana += p->mana_max * pct / 100;
                    if (p->mana > p->mana_max)
                        p->mana = p->mana_max;
                } else {
                    p->hp += p->hp_max * pct / 100;
                    if (p->hp > p->hp_max)
                        p->hp = p->hp_max;
                }
            }
        }
    }
}

/* Termina todo (al empezar una ronda de la prueba) */
static void dru_clear(Player *p)
{
    if (p->form != FORM_NONE)
        dru_unmorph(p);
    p->feral_charges = p->maul_charges = 0;
    charge_overlay(p, "feralrage", 0, 0);
    charge_overlay(p, "maul", 0, 0);
    p->cyclone = p->cyclone_max = 0;
    overlays_remove("cyclonearmor", OV_AT_PLAYER, (int)(p - s_pl));
    p->arctic_ticks = p->fiss_ticks = p->volc_ticks = p->arma_ticks = p->hurr_ticks = 0;
    p->spirit_unit = -1;
    dru_refresh(p);
}
