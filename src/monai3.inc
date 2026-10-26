/* ---- Las IA de los monstruos del Acto 3 (se incluye desde monai.inc) ----

   Como AiThink.cpp de D2MOO (AITHINK_Fn011_Baboon, Fn024_Mosquito,
   Fn025_Willowisp, Fn026_Arach, Fn027_ThornHulk, Fn028_Vampire,
   Fn030_Fetish, Fn048_ZakarumZealot, Fn049_ZakarumPriest, Fn050_Mephisto,
   Fn052_FrogDemon, Fn065_FetishShaman, Fn085_HighPriest y
   Fn096_FetishBlowgun), con sus parámetros aip de monstats.txt (Normal).
   Los hechizos son las habilidades de monstats.txt (Skill1..4, su modo y su
   nivel) con el proyectil de skills.txt y su daño de missiles.txt.

   shortcut: sin las órdenes del chamán a sus fetiches ni el ritual de los
   fuegos fatuos; los hechizos de área (la Ventisca de los sacerdotes, el
   Meteoro y el Muro de fuego de los vampiros, la Nova venenosa de Mefisto)
   salen al empezar la animación, no en su cuadro de golpe; Frenesí no
   acelera al Thorn Hulk; la telaraña de las arañas no frena. */

/* El proyectil de una habilidad de skills.txt (srvmissile, o srvmissilea) */
static const char *skill_missile(const char *skill)
{
    if (!s_skilltab.rows)
        load_table(&s_skilltab, "skills");
    int row = skill[0] && s_skilltab.rows ? txt_find_row(&s_skilltab, txt_col(&s_skilltab, "skill"), skill) : -1;
    if (row < 0)
        return "";
    const char *mis = txt_get(&s_skilltab, row, txt_col(&s_skilltab, "srvmissile"));
    return mis[0] ? mis : txt_get(&s_skilltab, row, txt_col(&s_skilltab, "srvmissilea"));
}

static int ai_sklvl(const Unit *u, int k)
{
    return u->sk_lvl[k] > 0 ? u->sk_lvl[k] : 1;
}

/* UseSkill con un proyectil: la habilidad k en su modo, con el proyectil de
   skills.txt y su daño en el nivel de la habilidad (si el proyectil no trae
   daño, el del ataque de esa animación). shots > 1: varios en abanico (o en
   anillo, con ring). 0 si le falta la animación o el proyectil. */
static int ai_cast(Unit *u, int t, int k, int shots, int ring, int flags)
{
    const char *mis = skill_missile(u->skill[k]);
    if (u->comp_s3 > 0 && strcmp(u->skill[k], "DoomKnightMissile") == 0) /* la de lo que lleva en la mano */
        mis = missiles_after(mis, u->comp_s3);
    int anim = u->sk_anim[k];
    if (t < 0 || !mis[0] || !missiles_exists(mis) || anim < 0 || !u->gfx->anim[anim].ok)
        return 0;
    int elem, lo, hi, dmin, dmax;
    missiles_base_damage(mis, &elem, &lo, &hi, &dmin, &dmax);
    missiles_level_damage(mis, ai_sklvl(u, k), &lo, &hi);
    if (!lo && !hi && !dmin && !dmax) { /* el de la habilidad (la Nova helada de Izual: EType y EMin/EMax de skills.txt) */
        static const char *const k_types[] = { "fire", "cold", "ltng", "pois", "mag" };
        int row = txt_find_row(&s_skilltab, txt_col(&s_skilltab, "skill"), u->skill[k]);
        const char *type = row >= 0 ? txt_get(&s_skilltab, row, txt_col(&s_skilltab, "EType")) : "";
        for (int e = 0; e < 5 && type[0]; e++)
            if (strcmp(type, k_types[e]) == 0) {
                elem = e + 1;
                skill_elem_damage(u->skill[k], ai_sklvl(u, k), &lo, &hi);
            }
    }
    if (!lo && !hi && !dmin && !dmax) { /* (los rayos cargados de Mefisto) */
        dmin = anim == ANIM_ATTACK2 ? u->a2_min : u->dmg_min;
        dmax = anim == ANIM_ATTACK2 ? u->a2_max : u->dmg_max;
    }
    snprintf(u->missile, sizeof u->missile, "%s", mis);
    u->missile2[0] = '\0';
    u->spell_elem = elem;
    u->spell_min = lo;
    u->spell_max = hi;
    u->spell_dmin = dmin;
    u->spell_dmax = dmax > 0 ? dmax : 0;
    u->spell_flags = flags;
    u->ring = ring ? shots : 0;
    u->spray = ring ? 0 : shots;
    u->ranged = 1;
    u->ranged_anim = anim;
    ai_attack(u, t, anim);
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s lanza %s (%s x%d, %d-%d)\n", u->name, u->skill[k], mis, shots, lo, hi);
    return 1;
}

/* Las de área: la animación y lo que deja en el lugar del jugador */
static int ai_area(Unit *u, int t, int k, const UnitTarget *tp)
{
    int anim = u->sk_anim[k], lvl = ai_sklvl(u, k), src = (int)(u - s_units);
    const char *sk = u->skill[k];
    if (t < 0 || anim < 0 || !u->gfx->anim[anim].ok)
        return 0;
    Missile *mi = NULL;
    if (strcmp(sk, "MonBlizzard") == 0 || (u->colossal && strcmp(sk, "Blizzard") == 0)) { /* el centro: cada tanto cae un trozo de hielo (monbliz1..4) */
        mi = missiles_effect("monblizcenter", tp->x, tp->y);
        if (mi) {
            mi->flags |= MF_EFFECT | MF_NOHIT | MF_BLIZZARD;
            mi->elem = ELEM_COLD;
            missiles_level_damage("monbliz1", lvl, &mi->emin, &mi->emax);
            mi->elen = 2 * TICK_RATE;
            mi->count = 8 - lvl / 4 > 3 ? 8 - lvl / 4 : 3; /* Param1: 8, menos con el nivel (al menos 3) */
            mi->radius10 = (5 + lvl / 4) * 10;             /* Param2: 5, más con el nivel */
            mi->life = 25 + lvl * 15;                      /* Range */
        }
    } else if (strcmp(sk, "VampireMeteor") == 0) { /* la marca, y a los 60 cuadros cae */
        missiles_effect("vampiremeteorcenter", tp->x, tp->y);
        mi = missiles_effect("vampiremeteor", tp->x, tp->y);
        if (mi) {
            int flo, fhi;
            mi->flags |= MF_EFFECT | MF_LAND | MF_METEOR;
            mi->life = 60;
            missiles_meteor(mi, "vampiremeteorcenter"); /* cae en diagonal en sus últimos cuadros, con la cola */
            mi->elem = ELEM_FIRE;
            skill_elem_damage(sk, lvl, &mi->emin, &mi->emax);
            mi->radius10 = 30;
            mi->trail = "vampiremeteorfire";
            missiles_level_damage("vampiremeteorfire", lvl, &flo, &fhi); /* el fuego que deja (por segundo) */
            mi->burn_min = (flo << 3) * TICK_RATE / 256 + 1;
            mi->burn_max = (fhi << 3) * TICK_RATE / 256 + 1;
            mi->burn_life = 90;
        }
    } else if (strcmp(sk, "VampireFirewall") == 0 || strcmp(sk, "PrimeFirewall") == 0) {
        /* dos que avanzan de costado, dejando fuego (el de Diablo usa los mismos proyectiles) */
        float dx = u->x - tp->x, dy = u->y - tp->y, l = sqrtf(dx * dx + dy * dy);
        int flo, fhi;
        l = l > 0.01f ? l : 1.0f;
        missiles_level_damage("vampirefirewall", lvl, &flo, &fhi);
        for (int side = -1; side <= 1; side += 2) {
            Missile *w = missiles_spawn("vampirefirewallmaker", tp->x, tp->y, -dy / l * (float)side, dx / l * (float)side);
            if (!w)
                continue;
            w->flags |= MF_MAKER | MF_NOHIT;
            w->trail = "vampirefirewall";
            w->burn_min = (flo << 3) * TICK_RATE / 256 + 1;
            w->burn_max = (fhi << 3) * TICK_RATE / 256 + 1;
            w->burn_life = 90;
            w->mx = w->my = -1000.0f;
            mi = w;
        }
    } else if (strcmp(sk, "HellMeteor") == 0) { /* el meteoro del río: cae donde eligió (SrvDo028) */
        mi = missiles_effect("hellmeteordown", tp->x, tp->y);
        if (mi) {
            mi->flags |= MF_EFFECT | MF_LAND;
            mi->life = 25;
            mi->elem = ELEM_FIRE;
            missiles_level_damage("hellmeteordown", lvl, &mi->emin, &mi->emax);
            mi->radius10 = 30;
        }
    } else if (strcmp(sk, "DiabWall") == 0) { /* SrvDo102: tantos como su nivel hacia el jugador, dejando fuego; casi todos en zigzag */
        float dx = tp->x - u->x, dy = tp->y - u->y, l = sqrtf(dx * dx + dy * dy);
        int flo, fhi;
        l = l > 0.01f ? l : 1.0f;
        missiles_level_damage("diabwall", lvl, &flo, &fhi);
        for (int s = 0; s < lvl; s++) {
            Missile *w = missiles_spawn("diabwallmaker", u->x, u->y, dx / l, dy / l);
            if (!w)
                continue;
            w->flags |= MF_MAKER | MF_NOHIT | (rnd(u) % 100 >= 20 ? MF_CHARGED : 0);
            w->life = w->life < 77 ? w->life : 77;
            w->trail = "diabwall";
            w->burn_min = (flo << 3) * TICK_RATE / 256 + 1;
            w->burn_max = (fhi << 3) * TICK_RATE / 256 + 1;
            w->burn_life = 36;
            w->mx = w->my = -1000.0f;
            mi = w;
        }
    } else if (strcmp(sk, "PrimePoisonNova") == 0) { /* un anillo de nubes de veneno que se abre */
        for (int s = 0; s < 16; s++) {
            float a = (float)s * PI_F / 8.0f;
            Missile *c = missiles_spawn("primepoisoncloud", u->x, u->y, cosf(a), sinf(a));
            if (!c)
                break;
            c->vx = cosf(a) * 0.35f;
            c->vy = sinf(a) * 0.35f;
            c->flags |= MF_CLOUD;
            c->elem = ELEM_POIS;
            missiles_level_damage("primepoisoncloud", lvl, &c->emin, &c->emax);
            c->elen = 3 * TICK_RATE;
            c->radius10 = 20;
            c->life = 40;
            mi = c;
        }
    }
    if (!mi)
        return 0;
    mi->owner = -1;
    mi->source = src;
    mi->ar = u->ar;
    mi->level = u->level;
    ai_attack(u, t, anim);
    u->ranged_attack = 0; /* lo del área ya salió */
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s lanza %s\n", u->name, sk);
    return 1;
}

/* ZakarumHeal: cura al aliado `ally` (15 + 5 por nivel %) en el cuadro del lanzamiento */
static int ai_heal(Unit *u, int t, int k, int ally)
{
    int anim = u->sk_anim[k];
    if (ally < 0 || anim < 0 || !u->gfx->anim[anim].ok)
        return 0;
    ai_attack(u, t, anim);
    u->ranged_attack = 0;
    u->heal_target = ally;
    u->heal_pct = 15 + 5 * ai_sklvl(u, k);
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s cura a %s (%d%%)\n", u->name, s_units[ally].name, u->heal_pct);
    return 1;
}

/* El aliado más herido (con menos de `below` % de vida) de los que sirven, a menos de `range` */
static int ai_wounded(const Unit *u, float range, int below, int zakarum)
{
    int best = -1, best_pct = below;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (!o->enemy || o->state >= ST_DYING || o->hidden || o->max_hp <= 0 || dist_to(u, o->x, o->y) > range)
            continue;
        if (zakarum && strcmp(o->base, "zealot1") != 0 && strcmp(o->base, "cantor1") != 0)
            continue;
        int pct = o->hp * 100 / o->max_hp;
        if (pct < best_pct) {
            best_pct = pct;
            best = i;
        }
    }
    return best;
}

/* El fetiche caído más cercano para revivir (AITHINK_TargetCallback_FetishShaman) */
static int ai_dead_fetish(const Unit *u, float range)
{
    int best = -1;
    float bd = range;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == u || !o->enemy || o->state != ST_DEAD || o->hidden || !o->revivable || o->searched || o->unique ||
            (strcmp(o->base, "fetish1") != 0 && strcmp(o->base, "fetishblow1") != 0))
            continue;
        float dd = dist_to(u, o->x, o->y);
        if (dd < bd) {
            bd = dd;
            best = i;
        }
    }
    return best;
}

/* MonTeleport: aparece en (x, y) o cerca, si ahí se puede pisar (primero
   en el lugar mismo: antes empezaba por la esquina de arriba a 2 subtiles,
   que podía quedar del otro lado de una pared) */
static int teleport_spot(const Unit *u, const Map *m, float x, float y, float *ox, float *oy)
{
    static const int k_order[9] = { 4, 1, 3, 5, 7, 0, 2, 6, 8 }; /* el centro, la cruz y las esquinas */
    for (int k = 0; k < 9; k++) {
        int tries = k_order[k];
        float tx = x + (float)(tries % 3 - 1) * 2.0f, ty = y + (float)(tries / 3 - 1) * 2.0f;
        if (m && !wall_foot(m, tx, ty, u->foot, u->move_mask)) {
            *ox = tx;
            *oy = ty;
            return 1;
        }
    }
    return 0;
}

/* Pasa a (tx, ty), con el destello del teletransporte en los dos lugares */
static void teleport_move(Unit *u, float tx, float ty)
{
    missiles_effect("teleport", u->x, u->y);
    u->x = u->prev_x = tx;
    u->y = u->prev_y = ty;
    grid_moved(u);
    missiles_effect("teleport", tx, ty);
}

static int ai_teleport(Unit *u, const Map *m, float x, float y)
{
    float tx, ty;
    if (!teleport_spot(u, m, x, y, &tx, &ty))
        return 0;
    teleport_move(u, tx, ty);
    ai_stall(u, 6);
    return 1;
}

/* Las cabezas de la Hidra del Consejo (Hydra: tres alrededor del jugador,
   que escupen fuego mientras duran) */
#define EHYDRA_MAX 12
static struct {
    float x, y;
    int   ticks, cd, lo, hi, src;
} s_ehydra[EHYDRA_MAX];

static void ehydra_add(float x, float y, int life, int lo, int hi, int src)
{
    for (int k = 0; k < EHYDRA_MAX; k++)
        if (s_ehydra[k].ticks <= 0) {
            s_ehydra[k].x = x;
            s_ehydra[k].y = y;
            s_ehydra[k].ticks = life;
            s_ehydra[k].cd = 8;
            s_ehydra[k].lo = lo;
            s_ehydra[k].hi = hi;
            s_ehydra[k].src = src;
            overlays_add("hydra_start1", OV_AT_POINT, 0, x, y, 0);
            overlays_add("hydra_loop1", OV_AT_POINT, 0, x, y, life);
            return;
        }
}

static void ehydra_tick(const UnitTarget *pl, int n)
{
    for (int k = 0; k < EHYDRA_MAX; k++) {
        if (s_ehydra[k].ticks <= 0)
            continue;
        if (--s_ehydra[k].ticks == 0)
            overlays_add("hydra_end1", OV_AT_POINT, 0, s_ehydra[k].x, s_ehydra[k].y, 0);
        if (--s_ehydra[k].cd > 0)
            continue;
        int best = -1;
        float bd = 15.0f * 15.0f;
        for (int i = 0; i < n; i++) {
            float dx = pl[i].x - s_ehydra[k].x, dy = pl[i].y - s_ehydra[k].y;
            if (pl[i].alive && !pl[i].in_town && dx * dx + dy * dy < bd) {
                bd = dx * dx + dy * dy;
                best = i;
            }
        }
        if (best < 0)
            continue;
        s_ehydra[k].cd = 25;
        float dx = pl[best].x - s_ehydra[k].x, dy = pl[best].y - s_ehydra[k].y, l = sqrtf(bd);
        Missile *mi = missiles_spawn("hydra", s_ehydra[k].x, s_ehydra[k].y, dx / l, dy / l);
        if (!mi)
            continue;
        const Unit *s = s_ehydra[k].src >= 0 && s_ehydra[k].src < s_num_units ? &s_units[s_ehydra[k].src] : NULL;
        mi->owner = -1;
        mi->source = s_ehydra[k].src;
        mi->ar = s ? s->ar : 100;
        mi->level = s ? s->level : 1;
        mi->dmin = mi->dmax = 0;
        mi->elem = ELEM_FIRE;
        mi->emin = s_ehydra[k].lo;
        mi->emax = s_ehydra[k].hi;
    }
}

/* Lo que decide cada IA del Acto 3. 0 = que siga la del motor. */
static int ai_think4(Unit *u, const Map *m, UnitTarget *pl, int t, float d, int hurt); /* (monai4.inc) */

static int ai_think3(Unit *u, const Map *m, UnitTarget *pl, int t, float d)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    int combat = tp && d <= u->reach, life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
    int hurt = u->hurt;
    u->hurt = 0;
    switch (u->ai) {
    case AI_FETISH: /* aip: ataque, quieto, ataques antes de alejarse, vida del jugador para seguir */
        if (!tp)
            return 0;
        switch (u->ai_p[0]) {
        case 0:
            if (combat) {
                u->ai_p[0] = 1;
                u->ai_p[1] = 0;
                if (ai_roll(u, 0))
                    ai_attack(u, t, ANIM_ATTACK);
                else
                    ai_stall(u, u->aip[1]);
                return 1;
            }
            ai_go(u, tp->x, tp->y, 10, 150, 1); /* AITACTICS_SetVelocity +50 */
            return 1;
        case 1:
            if (++u->ai_p[1] > u->aip[2] && tp->life_pct > u->aip[3]) { /* pegó varias veces: se aleja */
                u->ai_p[0] = 2;
                u->ai_p[1] = 0;
                ai_escape(u, tp, 14.0f);
                u->ai_spd = 150;
                return 1;
            }
            if (combat) {
                if (ai_roll(u, 0))
                    ai_attack(u, t, ANIM_ATTACK);
                else
                    ai_stall(u, u->aip[1]);
                return 1;
            }
            ai_go(u, tp->x, tp->y, 10, 150, 1);
            return 1;
        default:
            if (d <= 12.0f) {
                ai_escape(u, tp, 14.0f);
                u->ai_spd = 150;
                return 1;
            }
            if (++u->ai_p[1] > 1)
                u->ai_p[0] = u->ai_p[1] = 0;
            if (rnd(u) % 100 >= 20)
                ai_stall(u, 10);
            else
                ai_circle(u, tp, 4.0f);
            return 1;
        }
    case AI_FETISHBLOW: /* aip: distancia de tiro, alejarse */
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 1))
                u->ai_p[0] = 2;
        } else if (d > u->aip[0]) {
            ai_go(u, tp->x, tp->y, 10, 150, 1);
            return 1;
        } else if (d < 6.0f && ai_roll(u, 1)) {
            u->ai_p[0] = 2;
        }
        switch (u->ai_p[0]) {
        case 0: /* tira unos dardos y después se mueve */
            if (++u->ai_p[1] > 3 + (int)(rnd(u) % 3u))
                u->ai_p[0] = 1, u->ai_p[1] = 0;
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        case 1:
            u->ai_p[0] = u->ai_p[1] = 0;
            ai_circle(u, tp, 6.0f);
            u->ai_spd = 150;
            return 1;
        default:
            if (d <= 12.0f) {
                ai_escape(u, tp, 14.0f);
                u->ai_spd = 150;
                u->ai_p[0] = u->ai_p[1] = 0;
                return 1;
            }
            u->ai_p[0] = u->ai_p[1] = 0;
            if (rnd(u) % 100 < 20)
                ai_circle(u, tp, 4.0f);
            else
                ai_stall(u, 10);
            return 1;
        }
    case AI_FETISHSHAMAN: { /* aip: revivir, (a quiénes), distancia para revivir, rodear, dónde buscar */
        if (!tp)
            return 0;
        int range = ai_sklvl(u, 0); /* el Infierno alcanza tantos subtiles como su nivel */
        if (d < (float)range && ai_cast(u, t, 0, 1, 0, 0))
            return 1;
        int dead = ai_dead_fetish(u, (float)(u->aip[4] > 0 ? u->aip[4] : 10));
        if (dead >= 0 && ai_roll(u, 0)) {
            if (dist_to(u, s_units[dead].x, s_units[dead].y) <= (float)u->aip[2] && ai_skill(u, t, 2)) {
                u->res_target = dead;
                return 1;
            }
            ai_go(u, s_units[dead].x, s_units[dead].y, 10, 100, 0);
            return 1;
        }
        if (ai_roll(u, 3))
            ai_circle(u, tp, 4.0f);
        else
            ai_stall(u, 10);
        return 1;
    }
    case AI_BABOON: /* aip: herido, rodear, ataque, A1 o A2, (regeneración) */
        if (!tp)
            return 0;
        if (u->ai_p[0] > 0) { /* herido: se aleja a regenerarse unas vueltas */
            u->ai_p[1] = 0;
            u->ai_p[0]--;
            if (combat && rnd(u) % 100 < 33) {
                ai_attack12(u, t, 3);
                return 1;
            }
            if (!combat && life > 75) {
                u->ai_p[0] = 0;
                ai_go(u, tp->x, tp->y, 10, 100, 1);
                return 1;
            }
            if (d >= 24.0f && !hurt) {
                if (rnd(u) % 100 < 33)
                    ai_circle(u, tp, 4.0f);
                else
                    ai_stall(u, 20);
                return 1;
            }
            ai_escape(u, tp, 15.0f);
            return 1;
        }
        if (!combat) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (hurt && life < u->aip[0] && rnd(u) % 100 < 50) {
            u->ai_p[0] = 2 + (int)(rnd(u) % 5u);
            ai_escape(u, tp, 15.0f);
            return 1;
        }
        if (!u->ai_p[1] || ai_roll(u, 2)) {
            u->ai_p[1] = 1;
            ai_attack12(u, t, 3);
            return 1;
        }
        if (ai_roll(u, 1)) {
            ai_circle(u, tp, 3.0f);
            u->ai_p[1] = 0;
            return 1;
        }
        ai_stall(u, 10);
        return 1;
    case AI_THORNHULK: /* aip: ataque, A2 o A1, rodear, Frenesí, (velocidad), golpes seguidos del Frenesí */
        if (!tp)
            return 0;
        if (!combat) {
            u->ai_p[0] = 0;
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (u->ai_p[0] > 0) { /* el Frenesí: varios A2 seguidos */
            ai_attack(u, t, ANIM_ATTACK2);
            if (--u->ai_p[0] <= 0)
                u->ai_p[1] = 3;
            return 1;
        }
        if (ai_roll(u, 0)) {
            if (u->ai_p[1] > 0 || !ai_roll(u, 3)) {
                u->ai_p[1]--;
                ai_attack(u, t, ai_roll(u, 1) ? ANIM_ATTACK2 : ANIM_ATTACK);
            } else {
                ai_attack(u, t, ANIM_ATTACK2);
                u->ai_p[0] = u->aip[5];
            }
            return 1;
        }
        if (ai_roll(u, 2))
            ai_circle(u, tp, 4.0f);
        else
            ai_stall(u, 15);
        return 1;
    case AI_MOSQUITO: /* aip: (chupar), ataque, chupar o morder, vueltas sin atacar */
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 2) || !u->ai_p[1]) {
                u->ai_p[1]++;
                if (!(u->seqs[0] && ai_roll(u, 3) && ai_skill(u, t, 0)))
                    ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
            if (rnd(u) % 100 > 20) {
                ai_stall(u, 15);
                return 1;
            }
            u->ai_p[0] = 2;
            u->ai_p[1] = 0;
        }
        switch (u->ai_p[0]) {
        case 0:
            ai_go(u, tp->x, tp->y, 10, 200, 1);
            return 1;
        case 1:
            ai_circle(u, tp, 4.0f);
            u->ai_spd = 150;
            if (++u->ai_p[1] > u->aip[4])
                u->ai_p[0] = u->ai_p[1] = 0;
            return 1;
        default:
            ai_escape(u, tp, 10.0f);
            u->ai_spd = 200;
            u->ai_p[0] = 1;
            u->ai_p[1] = 0;
            return 1;
        }
    case AI_WISP: /* aip: lanzar, pegar, acercarse */
        if (!tp)
            return 0;
        if (u->ai_p[0] == 1 && --u->ai_p[1] > 0) { /* moviéndose */
            if (ai_roll(u, 2))
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            else
                ai_circle(u, tp, 6.0f);
            return 1;
        }
        if (combat && ai_roll(u, 1)) {
            u->ai_p[0] = 0;
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (!combat && ai_roll(u, 0) && u->ranged) { /* el rayo (MissC) */
            u->ai_p[0] = 0;
            ai_attack(u, t, ANIM_CAST);
            return 1;
        }
        u->ai_p[0] = 1;
        u->ai_p[1] = 3;
        if (ai_roll(u, 2))
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_circle(u, tp, 4.0f);
        return 1;
    case AI_ARACH: /* aip: ataque, rodear, acercarse, distancia para huir, herido */
        if (!tp)
            return 0;
        if (u->ai_p[0] == 1) { /* herido: se aleja tirando telarañas */
            u->ai_p[1] = 0;
            if (life > 75) {
                u->ai_p[0] = 0;
                if (ai_roll(u, 2)) {
                    u->ai_p[0] = 2;
                    ai_go(u, tp->x, tp->y, 10, 100, 1);
                } else {
                    ai_circle(u, tp, 6.0f);
                }
                return 1;
            }
            if (combat && u->aip[0] > 25 && (int)(rnd(u) % 100) < u->aip[0] - 25) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
            if (d < u->aip[3] || hurt) {
                ai_escape(u, tp, 4.0f);
                return 1;
            }
            u->ai_p[0] = 0;
            ai_circle(u, tp, 12.0f);
            return 1;
        }
        if (!combat) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        u->ai_p[0] = 2;
        if (ai_roll(u, 0)) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (life >= u->aip[4]) {
            if (ai_roll(u, 1))
                ai_circle(u, tp, 4.0f);
            else
                ai_stall(u, 15);
            return 1;
        }
        u->ai_p[0] = 1;
        if (!ai_cast(u, t, 0, 1, 0, 0)) /* SpiderLay: la telaraña */
            ai_escape(u, tp, 8.0f);
        return 1;
    case AI_FROG: /* aip: pegar, escupir de cerca, rodear de cerca, rodear de lejos, escupir, distancia de tiro,
                     quieto, distancia para salir */
        if (u->ai_p[2] == 1) { /* sumergida: sale cuando el jugador se acerca */
            if (tp && (d < u->aip[7] || (d < 20.0f && ++u->ai_p[1] > 64)) && u->gfx->anim[ANIM_SKILL].ok) {
                u->hidden = 0;
                u->solid = 1;
                u->ai_p[2] = 2;
                u->ai_p[1] = 0;
                ai_attack(u, t, ANIM_SKILL); /* Emerge (S1) */
                u->ranged_attack = 0;
                return 1;
            }
            ai_stall(u, 12);
            return 1;
        }
        if (u->ai_p[2] == 0) { /* todavía no decidió: si el jugador está lejos, se sumerge */
            if (tp && d > 12.0f && u->seqs[0] && ai_skill(u, t, 0)) {
                u->ai_p[2] = 1;
                u->hidden = 1;
                u->solid = 0;
                return 1;
            }
            u->ai_p[2] = 2;
        }
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 1) && u->ranged) {
                ai_attack(u, t, ANIM_ATTACK2); /* frogfire o frogpois */
                return 1;
            }
            if (ai_roll(u, 0)) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
            if (ai_roll(u, 2))
                ai_circle(u, tp, 3.0f);
            else
                ai_stall(u, u->aip[6]);
            return 1;
        }
        if (d >= u->aip[5]) {
            if (ai_roll(u, 3))
                ai_circle(u, tp, 3.0f);
            else
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (ai_roll(u, 4) && u->ranged) {
            ai_attack(u, t, ANIM_ATTACK2);
            return 1;
        }
        if (ai_roll(u, 3))
            ai_circle(u, tp, 4.0f);
        else
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        return 1;
    case AI_ZEALOT: /* aip: ataque, A2, herido, correr */
        if (!tp)
            return 0;
        if (s_zealots_flee) { /* con el Consejo muerto (A3Q5), huyen de los jugadores */
            ai_escape(u, tp, 8.0f);
            return 1;
        }
        if (hurt && !u->ai_p[1] && life < u->aip[2]) {
            u->ai_p[0] = 0;
            u->ai_p[1] = 5;
            ai_escape(u, tp, 8.0f);
            return 1;
        }
        if (u->ai_p[1])
            u->ai_p[1]--;
        if (!combat) {
            u->ai_p[0] = 0;
            ai_go(u, tp->x, tp->y, 10, ai_roll(u, 3) ? 150 : 100, 1);
            return 1;
        }
        if (u->ai_p[0] && !ai_roll(u, 0)) {
            u->ai_p[0] = 0;
            if (rnd(u) % 100 >= 80)
                ai_circle(u, tp, 4.0f);
            else
                ai_stall(u, 10);
            return 1;
        }
        u->ai_p[0] = 1;
        ai_attack(u, t, ai_roll(u, 1) ? ANIM_ATTACK2 : ANIM_ATTACK);
        return 1;
    case AI_PRIEST: { /* aip: ataque, Ventisca, Relámpago, lanzar, demora de los hechizos, distancia para curar */
        if (!tp)
            return 0;
        if ((combat || hurt) && life < 33 && u->ai_p[0] <= 0) { /* herido: se teletransporta lejos */
            u->ai_p[0] = 4 * u->aip[4];
            float k = combat ? 4.0f : 1.0f;
            if (ai_teleport(u, m, tp->x + (tp->x - u->x) * k, tp->y + (tp->y - u->y) * k))
                return 1;
        }
        if (u->ai_p[0] > 0)
            u->ai_p[0] -= 10;
        if (combat && ai_roll(u, 0)) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        int ally = ai_wounded(u, (float)u->aip[5], 61, 1);
        if (ally >= 0 && rnd(u) % 100 < 25 && ai_heal(u, t, 0, ally))
            return 1;
        if (u->ai_p[1] > 0)
            u->ai_p[1] -= 10;
        if (u->ai_p[2] > 0)
            u->ai_p[2] -= 10;
        if (ai_roll(u, 3)) {
            if (u->ai_p[1] <= 0 && ai_roll(u, 1) && ai_area(u, t, 3, tp)) {
                u->ai_p[1] = u->aip[4];
                return 1;
            }
            if (u->ai_p[2] <= 0 && ai_roll(u, 2) && ai_cast(u, t, 1, 1, 0, 0)) {
                u->ai_p[2] = 20;
                return 1;
            }
        }
        if (rnd(u) % 100 >= 30)
            ai_stall(u, 20);
        else
            ai_circle(u, tp, 4.0f);
        return 1;
    }
    case AI_VAMPIRE: { /* aip: pegar, lanzar, distancia activa, hechizo mejor, (qué hechizos: 1 bola, 2 muro, 4 meteoro) */
        if (!tp)
            return 0;
        int spells = u->aip[4];
        if (u->ai_p[2] > 0)
            u->ai_p[2]--;
        if (hurt) {
            if (!u->ai_p[0])
                u->ai_p[0] = 1;
            if (combat) {
                if ((spells & 1) && rnd(u) % 100 <= 30 && ai_cast(u, t, rnd(u) % 2 ? 0 : 3, 1, 0, 0))
                    return 1;
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
        }
        if (u->ai_p[0] == 2) { /* herido: lanza desde lejos */
            if (life >= 75) {
                u->ai_p[0] = 1;
                ai_go(u, tp->x, tp->y, 10, 100, 1);
                return 1;
            }
            if (d < 14.0f) {
                ai_escape(u, tp, 8.0f);
                return 1;
            }
        } else if (life < 33) {
            u->ai_p[0] = 2;
            ai_escape(u, tp, 8.0f);
            return 1;
        } else if (combat) {
            u->ai_p[0] = 1;
            if (ai_roll(u, 0)) {
                if ((spells & 1) && rnd(u) % 100 <= 30 && ai_cast(u, t, rnd(u) % 2 ? 0 : 3, 1, 0, 0))
                    return 1;
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
            if (rnd(u) % 100 < 33)
                ai_circle(u, tp, 4.0f);
            else
                ai_stall(u, 10);
            return 1;
        }
        if (d >= u->aip[2]) {
            if (u->ai_p[0] == 1)
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            else
                ai_stall(u, 15);
            return 1;
        }
        if (!u->ai_p[0])
            u->ai_p[0] = 1;
        if (!ai_roll(u, 1)) {
            ai_stall(u, 15);
            return 1;
        }
        if ((spells & 2) && u->ai_p[2] <= 0 && ai_roll(u, 3) && ai_area(u, t, 1, tp)) { /* el Muro de fuego */
            u->ai_p[2] = 11;
            return 1;
        }
        if ((spells & 4) && u->ai_p[2] <= 0 && ai_roll(u, 3) && ai_area(u, t, 2, tp)) { /* el Meteoro */
            u->ai_p[2] = 11;
            return 1;
        }
        if ((spells & 1) && d <= 20.0f && ai_cast(u, t, rnd(u) % 2 ? 0 : 3, 1, 0, 0))
            return 1;
        ai_circle(u, tp, 4.0f);
        return 1;
    }
    case AI_HIGHPRIEST: { /* aip: pegar, curar, demora, Hidra, Relámpago, alejarse, Relámpago de cerca, distancia */
        if (!tp)
            return 0;
        if (u->ai_p[1] > 0)
            u->ai_p[1]--;
        if (!u->ai_p[0]) { /* a distancia */
            if (combat) {
                if (ai_roll(u, 0)) {
                    u->ai_p[0] = 1;
                    ai_attack(u, t, ANIM_ATTACK);
                } else {
                    ai_escape(u, tp, 6.0f);
                }
                return 1;
            }
            if (u->ai_p[1] <= 0 && ai_roll(u, 1)) {
                int ally = ai_wounded(u, 50.0f, 75, 0);
                if (ally >= 0 && ai_heal(u, t, 1, ally)) {
                    u->ai_p[1] = u->aip[2];
                    return 1;
                }
            }
            if (u->ai_p[1] <= 0 && d < u->aip[7] && ai_roll(u, 3) && u->gfx->anim[u->sk_anim[0] >= 0 ? u->sk_anim[0] : ANIM_SKILL].ok) {
                static const float k_off[4][2] = { { -5, -5 }, { 5, -5 }, { 5, 5 }, { -5, 5 } };
                int lo, hi, k = (int)(rnd(u) & 3u);
                skill_elem_damage("Hydra", ai_sklvl(u, 0), &lo, &hi);
                for (int h = 0; h < 3; h++) /* tres cabezas alrededor del lugar elegido */
                    ehydra_add(tp->x + k_off[k][0] + (float)(h - 1) * 1.5f, tp->y + k_off[k][1], 250, lo, hi, (int)(u - s_units));
                ai_attack(u, t, u->sk_anim[0] >= 0 ? u->sk_anim[0] : ANIM_SKILL);
                u->ranged_attack = 0;
                u->ai_p[1] = 100;
                if (getenv("D2_AI_DEBUG"))
                    printf("IA: %s lanza Hydra (%d-%d)\n", u->name, lo, hi);
                return 1;
            }
            if (u->aip[4] > 0 && d < 13.0f && ai_roll(u, 4) && u->ranged) {
                ai_attack(u, t, ANIM_SKILL); /* highpriestlightning (MissS1) */
                return 1;
            }
            if (rnd(u) % 100 < 80) {
                if (d <= u->aip[7])
                    ai_circle(u, tp, 3.0f);
                else
                    ai_go(u, tp->x, tp->y, 10, 100, 1);
                return 1;
            }
        }
        u->ai_p[0] = 1; /* peleando de cerca */
        if (combat) {
            if (ai_roll(u, 6) && u->ranged)
                ai_attack(u, t, ANIM_SKILL);
            else if (ai_roll(u, 5)) {
                ai_escape(u, tp, 6.0f);
                u->ai_p[0] = 0;
            } else if (rnd(u) % 100 >= 90)
                ai_stall(u, 10);
            else
                ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (d < 6.0f && ai_roll(u, 6) && u->ranged) {
            ai_attack(u, t, ANIM_SKILL);
            return 1;
        }
        if (ai_roll(u, 5)) {
            u->ai_p[0] = 0;
            ai_stall(u, 10);
            return 1;
        }
        ai_go(u, tp->x, tp->y, 10, 100, 1);
        return 1;
    }
    case AI_MEPHISTO: { /* aip: pelear de cerca o lanzar, (a quién), (a quién) */
        if (!tp)
            return 0;
        int lc = (100 - life) / 5 > 0 ? (100 - life) / 5 : 0, state = u->ai_p[2];
        if (combat) {
            if ((int)(rnd(u) % 100) <= u->aip[0]) {
                state = 2;
                u->ai_p[0] = 3 + (int)(rnd(u) % 3u);
            } else {
                state = 3;
            }
        } else if (d > 20.0f) {
            state = 4;
        } else if (!u->ai_p[2]) {
            if (life <= 20 && d < 5.0f && rnd(u) % 100 < 40) {
                state = 1;
            } else if ((int)(rnd(u) % 100) >= lc + 50) {
                if (rnd(u) % 100 >= 65)
                    ai_stall(u, 10);
                else
                    ai_circle(u, tp, 4.0f);
                u->ai_p[2] = 0;
                return 1;
            } else {
                state = 2;
                u->ai_p[0] = 3 + (int)(rnd(u) % 3u);
            }
        }
        switch (state) {
        case 0:
            ai_stall(u, 5);
            u->ai_p[2] = 4;
            return 1;
        case 1: /* casi muerto y acorralado: se escapa, o suelta la Nova venenosa */
            u->ai_p[2] = 0;
            if (!combat) {
                ai_escape(u, tp, 8.0f);
                u->ai_spd = 150;
                return 1;
            }
            if (!ai_area(u, t, 2, tp))
                ai_attack(u, t, ANIM_ATTACK);
            return 1;
        case 3:
            if ((int)(rnd(u) % 100) >= lc + 80)
                ai_circle(u, tp, 3.0f);
            else if ((int)(rnd(u) % 100) >= 80 - lc && ai_area(u, t, 2, tp))
                ;
            else
                ai_attack(u, t, ANIM_ATTACK);
            u->ai_p[2] = 0;
            return 1;
        case 4:
            ai_go(u, tp->x, tp->y, 10, 150, 1);
            u->ai_p[2] = 0;
            return 1;
        case 2:
            break;
        default:
            if (rnd(u) % 100 >= 65)
                ai_stall(u, 10);
            else
                ai_circle(u, tp, 4.0f);
            u->ai_p[2] = 0;
            return 1;
        }
        /* lanzando: unas vueltas de hechizos, moviéndose cada tanto */
        if (--u->ai_p[0] <= 0)
            state = 0;
        if (!u->ai_p[1]) {
            u->ai_p[1] = 2;
            ai_circle(u, tp, 3.0f);
            u->ai_p[2] = state;
            return 1;
        }
        u->ai_p[1]++;
        /* de a cuartos, como el original en Normal (sin las habilidades 5 y 6 de Pesadilla) */
        int r = (int)(rnd(u) % 100), cast = 0;
        if (r < 50) /* MephistoMissile */
            cast = ai_cast(u, t, 3, 1, 0, 0);
        else if (r < 75) /* PrimeBolt: nivel + 2 rayos cargados */
            cast = ai_cast(u, t, 1, ai_sklvl(u, 1) + 2, 0, MF_CHARGED);
        else
            cast = ai_cast(u, t, 0, 1, 0, 0); /* PrimeLightning */
        if (!cast)
            ai_attack(u, t, ANIM_ATTACK);
        if ((int)(rnd(u) % 100) < 50 - lc)
            u->ai_p[1] = 0;
        u->ai_p[2] = state;
        return 1;
    }
    case AI_TENTACLE: case AI_TENTHEAD: { /* aip: pegar, sumergirse, segundos abajo, segundos arriba,
                                             quieto, distancia a la que sale */
        /* AITHINK_Fn056_Tentacle / Fn057_TentacleHead: ai_p[2] 0 al empezar, 1 abajo, 2 arriba;
           ai_p[1], hasta cuándo (s_unit_ticks) */
        int head = u->ai == AI_TENTHEAD;
        const Unit *o = head ? NULL : u->master >= 0 && u->master < s_num_units ? &s_units[u->master] : NULL;
        if (!head && (!o || o->zone == ZONE_FREE || o->ai != AI_TENTHEAD)) { /* sin su cabeza */
            units_play_death((int)(u - s_units));
            return 1;
        }
        if (!head && o->state >= ST_DYING && rnd(u) % 100 < 40) {
            units_play_death((int)(u - s_units));
            return 1;
        }
        if (!tp) {
            ai_stall(u, 25);
            return 1;
        }
        float act = (float)u->aip[5];
        int now = (int)s_unit_ticks, o_down = o && o->ai_p[2] == 1;
        if (u->seqs[0]) { /* Submerge (S1 al revés) */
            int go_down = u->ai_p[2] == 0;
            if (u->ai_p[2] == 2 && now > u->ai_p[1])
                go_down = d > act || (!combat && ai_roll(u, 1)) || (!head && o_down && rnd(u) % 100 < 50);
            if (go_down && ai_skill(u, t, 0)) {
                u->ai_p[1] = now + 25 * u->aip[2];
                u->ai_p[2] = 1;
                return 1;
            }
        }
        if (u->ai_p[2] == 1) {
            /* Emerge (S1): cuando el jugador se acerca (el tentáculo, también a veces solo si la cabeza está arriba) */
            if (now > u->ai_p[1] && u->gfx->anim[ANIM_SKILL].ok &&
                (combat || d < act || (!head && !o_down && rnd(u) % 100 < 5))) {
                u->hidden = 0;
                u->solid = 1;
                ai_attack(u, t, ANIM_SKILL);
                u->ranged_attack = 0;
                u->hit_done = 1; /* (salir no pega) */
                u->ai_p[1] = now + 25 * u->aip[3];
                u->ai_p[2] = 2;
                return 1;
            }
            ai_stall(u, u->aip[4]);
            return 1;
        }
        if ((head || combat) && ai_roll(u, 0)) { /* la cabeza escupe de lejos (tentaclegoo) */
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        ai_stall(u, u->aip[4]);
        return 1;
    }
    default:
        return ai_think4(u, m, pl, t, d, hurt);
    }
}
