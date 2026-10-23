/* ---- Las IA de los monstruos del Acto 4 (se incluye desde monai.inc) ----

   Como AiThink.cpp de D2MOO (AITHINK_Fn004_Bighead, Fn033_HellMeteor,
   Fn051_Diablo, Fn055_Izual, Fn068_VileMother, Fn069_VileDog,
   Fn070_FingerMage, Fn071_Regurgitator, Fn072_DoomKnight,
   Fn073_AbyssKnight, Fn074_OblivionKnight, Fn089_Megademon, Fn098_Smith y
   Fn099_TrappedSoul), con sus parámetros aip de monstats.txt (Normal), y lo
   que hacen sus habilidades en SkillMonst.cpp: el nido de la madre
   (SrvDo091), el aliento sostenido (SrvSt53/SrvDo095 y SrvDo152), lo que
   come el Corpulento (SrvDo108), el toque frío (SrvDo100), el muro (SrvDo102),
   la carga (SrvDo103) y la prisión de Diablo (SrvDo104).

   shortcut: Diablo no le encierra el portal al pueblo al jugador ni tiene
   AI_CheckSpecialSkills (cuenta como si nadie se teletransportara). */

/* Lo que hace en el cuadro de golpe de su animación (Unit.a4_hit; las del Acto 5 siguen en monai5.inc) */
enum { A4H_NONE, A4H_NEST, A4H_EAT, A4H_ARMOR, A4H_INFERNO, A4H_COLD, A4H_PRISON, A4H_CURSE };
static void a5_hit_frame(Unit *u, UnitTarget *pl);
static int  ai_think5(Unit *u, const Map *m, UnitTarget *pl, int t, float d, int hurt);

/* UseSkill: la animación de su habilidad k con lo que hace al golpear (0 si le falta la animación) */
static int a4_use(Unit *u, int t, int k, int what)
{
    int anim = u->sk_anim[k];
    if (!u->skill[k][0] || anim < 0 || !u->gfx->anim[anim].ok)
        return 0;
    ai_attack(u, t, anim);
    u->ranged_attack = 0;
    u->a4_hit = (int8_t)what;
    u->a4_k = (int8_t)k;
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s usa %s\n", u->name, u->skill[k]);
    return 1;
}

/* Los vivos de su cría a menos de 25 subtiles (sub_6FCF1E80 con nLastInClass) */
static int a4_children(const Unit *u)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o->enemy && o->state < ST_DYING && strcmp(o->name, u->minion) == 0 && dist_to(u, o->x, o->y) < 25.0f)
            n++;
    }
    return n;
}

/* AITHINK_TargetCallback_OblivionKnight: el Caballero de la Perdición vivo más cercano a menos de 50 */
static int a4_doomknight(const Unit *u)
{
    int best = -1;
    float bd = 50.0f;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == u || !o->enemy || o->state >= ST_DYING || o->hidden || strcmp(o->base, "doomknight1") != 0)
            continue;
        float d = dist_to(u, o->x, o->y);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* AITHINK_TargetCallback_Regurgitator: el cadáver blando de un monstruo
   (no un mercenario ni un no muerto) más cercano a menos de `range` */
static int a4_food(const Unit *u, float range)
{
    int best = -1;
    float bd = range;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == u || o->state != ST_DEAD || o->hidden || !o->soft || o->undead || o->merc || o->zone == ZONE_FREE)
            continue;
        float d = dist_to(u, o->x, o->y);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* El aliento sostenido (SrvSt53_MonInferno y SrvDo095/152): dura calc2
   cuadros, quieto en el cuadro InfernoAnim de monstats2.txt, y cada calc3
   cuadros sale una llama del largo de calc1 (si es 0, Param2 del proyectil
   más el nivel - 1) hacia el jugador */
static void a4_inferno_start(Unit *u)
{
    int k = u->a4_k, lvl = ai_sklvl(u, k), ms2 = mon_row(&s_monstats2, u->name);
    if (ms2 < 0) /* (un jefe con nombre, como el Infector de Almas: el de su clase) */
        ms2 = mon_row(&s_monstats2, u->base);
    if (!s_skilltab.rows)
        load_table(&s_skilltab, "skills");
    int row = txt_find_row(&s_skilltab, txt_col(&s_skilltab, "skill"), u->skill[k]);
    int par1 = row >= 0 ? txt_get_int(&s_skilltab, row, "Param1", 15) : 15;
    const char *mis = skill_missile(u->skill[k]);
    u->inferno_end = (int)s_unit_ticks + (par1 > 1 ? par1 : 1);
    u->inferno_frame = ms2 >= 0 ? txt_get_int(&s_monstats2, ms2, "InfernoAnim", 0) : 0;
    u->inferno_len = missiles_param2(mis) + lvl - 1;
    u->inferno_len = u->inferno_len < 1 ? 1 : u->inferno_len > 255 ? 255 : u->inferno_len;
}

static void a4_inferno_tick(Unit *u, UnitTarget *pl)
{
    if (((int)s_unit_ticks - u->inferno_end) % 2 != 0 || u->target < 0) /* (calc3: una llama cada 2 cuadros) */
        return;
    const UnitTarget *t = &pl[u->target];
    const char *mis = skill_missile(u->skill[u->a4_k]);
    float dx = t->x - u->x, dy = t->y - u->y, l = sqrtf(dx * dx + dy * dy);
    l = l > 0.01f ? l : 1.0f;
    Missile *mi = missiles_spawn(mis, u->x + dx / l * 0.6f, u->y + dy / l * 0.6f, dx / l, dy / l);
    if (!mi)
        return;
    int elem, dmin, dmax;
    missiles_base_damage(mis, &elem, &mi->emin, &mi->emax, &dmin, &dmax);
    missiles_level_damage(mis, ai_sklvl(u, u->a4_k), &mi->emin, &mi->emax);
    mi->owner = -1;
    mi->source = (int)(u - s_units);
    mi->ar = u->ar;
    mi->level = u->level;
    mi->elem = elem;
    mi->dmin = dmin;
    mi->dmax = dmax;
    mi->life = u->inferno_len;
}

/* La prisión de hueso de Diablo alrededor del jugador (SrvDo104, los 12 lugares del Nigromante).
   Como el original, cada pieza es un monstruo boneprison1 del bando enemigo que se rompe a golpes
   (killable, 193 a 385 de vida en Normal) y no se termina sola (DiabPrison no tiene duración en
   skills.txt): se sale rompiéndola. No da experiencia ni botín y su cadáver no sirve (neverCount).
   Antes salían neutrales: nadie las podía romper y la jaula quedaba para siempre. */
static void a4_prison(Unit *u, const UnitTarget *t)
{
    static const float k_ring[12][2] = { { 0, -2 }, { 1, -2 }, { 2, -1 }, { 2, 0 },  { 2, 1 },  { 1, 2 },
                                         { 0, 2 },  { -1, 2 }, { -2, 1 }, { -2, 0 }, { -2, -1 }, { -1, -2 } };
    if (t->in_town)
        return;
    int saved = s_spawn_zone;
    s_spawn_zone = u->zone;
    for (int k = 0; k < 12; k++) {
        float x = t->x + k_ring[k][0], y = t->y + k_ring[k][1];
        if (s_map && wall_foot(s_map, x, y, 1, COLL_MASK_MONSTER_PATH))
            continue;
        /* Donde ya hay alguien (una pieza de la jaula anterior, el propio Diablo) no sale: en el
           original el lugar ocupado hace fallar al monstruo; si no, cada vez se apilaban 12 más */
        if (units_blocking_at(x, y, 0.3f, NULL, NULL))
            continue;
        Unit *b = create_monster("boneprison1", x, y, u->level, 1);
        if (b) {
            b->exp = 0;
            b->tc[0] = '\0';
            b->searched = 1;
        }
    }
    s_spawn_zone = saved;
}

/* En el cuadro de golpe: lo de la habilidad en curso */
static void a4_hit_frame(Unit *u, UnitTarget *pl)
{
    UnitTarget *t = u->target >= 0 ? &pl[u->target] : NULL;
    int k = u->a4_k, lvl = ai_sklvl(u, k);
    switch (u->a4_hit) {
    case A4H_NEST: { /* sale una cría (sin experiencia ni botín) donde eligió */
        int saved = s_spawn_zone;
        s_spawn_zone = u->zone;
        Unit *c = u->minion[0] ? create_monster(u->minion, u->a4_x, u->a4_y, u->level, 1) : NULL;
        s_spawn_zone = saved;
        if (c) {
            c->exp = 0;
            c->tc[0] = '\0';
            c->sector = u->sector;
        }
        break;
    }
    case A4H_EAT: { /* el cadáver desaparece y cura el 33 % de la vida que tenía */
        Unit *c = u->a4_corpse >= 0 && u->a4_corpse < s_num_units ? &s_units[u->a4_corpse] : NULL;
        if (c && c->state == ST_DEAD && !c->hidden && dist_to(u, c->x, c->y) < 3.0f) {
            int heal = c->max_hp * 33 / 100;
            units_remove(u->a4_corpse);
            u->hp = u->hp + heal < u->max_hp ? u->hp + heal : u->max_hp;
        }
        u->a4_corpse = -1;
        break;
    }
    case A4H_ARMOR: /* la Armadura de hueso: absorbe el daño físico (Param1 + Param2 por nivel) */
        u->bone_armor = 20 + 10 * (lvl - 1);
        break;
    case A4H_INFERNO:
        a4_inferno_start(u);
        break;
    case A4H_COLD: /* el toque frío: daño mágico y lo congela (ELen 100 + 50 por nivel) */
        if (t && t->alive && !t->in_town) {
            int lo, hi;
            skill_elem_damage(u->skill[k], lvl, &lo, &hi);
            t->damage += lo + (int)(rnd(u) % (uint32_t)(hi - lo + 1 > 0 ? hi - lo + 1 : 1));
            int len = 100 + 50 * (lvl - 1);
            if (len > t->freeze_ticks)
                t->freeze_ticks = len;
        }
        break;
    case A4H_PRISON:
        if (t)
            a4_prison(u, t);
        break;
    case A4H_CURSE: /* la maldición (SrvDo030 y SrvDo112): lo que dura en su nivel (auralencalc) */
        if (t && t->alive && !t->in_town) {
            /* ln34 de cada maldición del Nigromante: Param3 + Param4 por nivel (cuadros) */
            static const int k_len[A4C_COUNT][2] = { { 0, 0 }, { 200, 75 }, { 350, 60 }, { 300, 60 }, { 400, 60 }, { 100, 15 }, { 500, 50 }, { 200, 75 }, { 200, 75 } };
            int c = u->a4_curse > 0 && u->a4_curse < A4C_COUNT ? u->a4_curse : A4C_DECREPIFY;
            t->curse = c;
            t->curse_lvl = lvl;
            t->curse_len = (k_len[c][0] + k_len[c][1] * (lvl - 1)) * TICK_RATE / 25;
        }
        break;
    default:
        a5_hit_frame(u, pl);
        break;
    }
    u->a4_hit = A4H_NONE;
}

/* Diablo (AITHINK_Fn051_Diablo): elige cada vez entre sus 16 cosas con
   pesos que dependen de si el jugador está al lado, lejos o detrás de algo
   que tapa los proyectiles (los de chanceArray del original, en Normal) */
static int a4_diablo(Unit *u, const Map *m, UnitTarget *pl, int t, float d)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    if (!tp) {
        ai_stall(u, 12);
        return 1;
    }
    if (!u->ai_p[1]) { /* su lugar: adonde vuelve si el jugador se lo lleva lejos */
        u->ai_p[1] = 1;
        u->ai_tx = u->x;
        u->ai_ty = u->y;
        u->leap_x0 = u->x;
        u->leap_y0 = u->y;
    }
    int act = 0;
    if (u->ai_p[0] == 5) { /* sigue con el rayo rojo */
        act = 5;
    } else {
        int w[17] = { 0 }, players = 0;
        for (int i = 0; i < 4; i++)
            players += pl[i].alive && dist_to(u, pl[i].x, pl[i].y) < 30.0f;
        int melee = d <= u->reach + 0.6f;
        int blocked = m && map_ray(m, (int)u->x, (int)u->y, (int)tp->x, (int)tp->y, COLL_MISSILE_BARRIER, NULL, NULL);
        float hx = tp->x - u->leap_x0, hy = tp->y - u->leap_y0, home = sqrtf(hx * hx + hy * hy);
        int far_away = home > 85.0f, further = home > 105.0f;
        int fire_res = tp->res[0], ltng_res = tp->res[2];
        if (melee) {
            w[2] = tp->life_pct < 20 ? 50 : 40;
            w[3] = 70;
            w[5] = 40;
            w[6] = 24 + (fire_res > ltng_res ? -10 : fire_res < ltng_res ? 10 : 0);
            w[7] = tp->chill_ticks > 0 ? 0 : 40;
            w[8] = tp->chill_ticks > 0 ? 0 : 15;
            if (blocked)
                w[5] = w[6] = 0;
        } else if (blocked) {
            w[4] = 5;
            w[6] = 25;
            w[8] = 25;
            w[9] = 40;
            w[12] = 25;
            if (players < 2) {
                w[6] = 0;
                w[1] = 25;
                w[8] -= 5;
                w[9] = 0;
            }
            if (far_away) {
                w[13] = 25;
                w[1] = 0;
                w[12] = 15;
                w[9] = w[9] ? w[9] : 20;
                if (players < 2)
                    w[9] -= 5;
            }
            if (further)
                w[14] = 60;
        } else {
            w[5] = 25;
            w[6] = 25;
            w[8] = 15;
            w[9] = 20;
            w[10] = 10;
            w[12] = 20;
            if (d > 25.0f) {
                w[10] = 20;
                w[5] = 0;
                w[8] -= 5;
            }
            w[6] += fire_res > ltng_res ? -10 : fire_res < ltng_res ? 10 : 0;
            w[8] += fire_res > ltng_res ? -10 : fire_res < ltng_res ? 10 : 0;
            w[6] += players < 2 ? -10 : players > 3 ? 5 : 0;
            if (players < 2) /* (en Normal con menos de dos jugadores, nunca la prisión desde lejos) */
                w[9] = 0;
            if (far_away) {
                w[1] = 0;
                w[12] = 10;
                w[10] = 0;
                w[13] = 15;
                w[9] = w[9] ? w[9] : 10;
            }
            if (further) {
                w[9] = 20;
                w[14] = 60;
            }
        }
        /* AI_GetRandomArrayIndex: uno al azar según los pesos; si no suman, el 11 (quieto) */
        int sum = 0;
        for (int i = 0; i < 17; i++)
            sum += w[i] > 0 ? w[i] : 0;
        act = 11;
        if (sum > 0) {
            int r = (int)(rnd(u) % (uint32_t)sum);
            for (int i = 0; i < 17; i++) {
                int wi = w[i] > 0 ? w[i] : 0;
                if (r < wi) {
                    act = i;
                    break;
                }
                r -= wi;
            }
        }
    }
    u->ai_p[0] = 0;
    switch (act) {
    case 1:
        ai_go(u, tp->x, tp->y, 15, 120, 0);
        return 1;
    case 2:
        ai_attack(u, t, ANIM_ATTACK);
        return 1;
    case 3:
        ai_attack(u, t, ANIM_ATTACK2);
        return 1;
    case 4:
        if (u->gfx->anim[ANIM_SKILL4].ok) /* (la burla) */
            ai_attack(u, t, ANIM_SKILL4);
        else
            ai_stall(u, 12);
        return 1;
    case 5: /* el rayo rojo (DiabLight): el aliento sostenido */
        if (!a4_use(u, t, 0, A4H_INFERNO))
            ai_stall(u, 2);
        return 1;
    case 6: /* el anillo de fuego (DiabFire: una nova de 64) */
        if (!ai_cast(u, t, 2, 64, 1, MF_GROUP))
            ai_stall(u, 2);
        return 1;
    case 7: /* el toque frío (DiabCold) */
        if (!a4_use(u, t, 1, A4H_COLD))
            ai_stall(u, 2);
        return 1;
    case 8: /* el muro de fuego en zigzag (DiabWall) */
        if (!ai_area(u, t, 3, tp))
            ai_stall(u, 2);
        return 1;
    case 9: /* la prisión de hueso (DiabPrison) */
        if (!a4_use(u, t, 6, A4H_PRISON))
            ai_stall(u, 2);
        return 1;
    case 10: /* la carga (DiabRun) */
        u->leap_x1 = tp->x;
        u->leap_y1 = tp->y;
        if (!ai_skill(u, t, 4))
            ai_stall(u, 2);
        return 1;
    case 12:
        ai_circle(u, tp, 4.0f);
        return 1;
    case 13: /* el muro de fuego (PrimeFirewall) */
        if (!ai_area(u, t, 5, tp))
            ai_stall(u, 2);
        return 1;
    case 14: /* el jugador se lo llevó lejos: vuelve a su lugar */
        ai_go(u, u->leap_x0, u->leap_y0, 40, 150, 0);
        return 1;
    case 16:
        ai_wander(u, 5, 120);
        return 1;
    default:
        ai_stall(u, 12);
        return 1;
    }
}

static int ai_think4(Unit *u, const Map *m, UnitTarget *pl, int t, float d, int hurt)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    int combat = tp && d <= u->reach, life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
    switch (u->ai) {
    case AI_VILEMOTHER: { /* aip: crías en total, a la vez, % de parir, pegar, acercarse, rodear, quieto */
        if (!tp)
            return 0;
        if (u->ai_p[0] < u->aip[0] && ai_roll(u, 2) && a4_children(u) < u->aip[1]) {
            /* a 3 subtiles, hacia un costado del jugador (nIndices del original) */
            static const int k_turn[8] = { 6, 4, 2, 2, 2, 0, 6, 6 };
            int dir = (int)lroundf(atan2f(tp->y - u->y, tp->x - u->x) / (PI_F / 4.0f)) & 7;
            for (int i = 0; i < 8; i++, dir = (dir + 1) & 7) {
                float a = (float)k_turn[dir] * PI_F / 4.0f, x = u->x + cosf(a) * 3.0f, y = u->y + sinf(a) * 3.0f;
                if (m && wall_foot(m, x, y, 1, COLL_MASK_MONSTER_PATH))
                    continue;
                if (a4_use(u, t, 0, A4H_NONE) || ai_skill(u, t, 0)) {
                    u->a4_x = x;
                    u->a4_y = y;
                    u->a4_hit = A4H_NEST;
                    u->ai_p[0]++;
                    return 1;
                }
                break;
            }
        }
        if (combat) {
            if (ai_roll(u, 3))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[6]);
            return 1;
        }
        if (u->ai_p[0] >= u->aip[0] || ai_roll(u, 4)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (ai_roll(u, 5))
            ai_circle(u, tp, 4.0f);
        else
            ai_stall(u, 15);
        return 1;
    }
    case AI_VILEDOG: /* aip: pegar, quieto, acercarse (al nacer, un momento quieta) */
        if (!tp)
            return 0;
        if (!u->ai_p[0]) {
            u->ai_p[0] = 1;
            ai_stall(u, 5);
            return 1;
        }
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[1]);
            return 1;
        }
        if (ai_roll(u, 2))
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, 10);
        return 1;
    case AI_FINGERMAGE: { /* aip: pegar o rodear, lanzar, sano, herido, alcance, intentos de huida, lejos, quieto */
        if (!tp)
            return 0;
        if (hurt) { /* le pegaron: se olvida de huir y contesta */
            u->ai_p[0] = 0;
            if (combat) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
            if (d < u->aip[4] && ai_cast(u, t, 0, 1, 0, MF_HOMING))
                return 1;
        }
        if (u->ai_p[0] == 1) { /* huyendo */
            u->ai_p[1]++;
            if (life > u->aip[2] || rnd(u) % 100 < 25 || u->ai_p[1] > u->aip[5]) {
                u->ai_p[0] = 0;
            } else if (d < 14.0f) {
                ai_escape(u, tp, 14.0f);
                return 1;
            }
            ai_stall(u, 15);
            return 1;
        }
        if (u->master < 0 && life < u->aip[3]) {
            u->ai_p[0] = 1;
            u->ai_p[1] = 0;
            ai_escape(u, tp, 9.0f);
            return 1;
        }
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ANIM_ATTACK);
            else if (!ai_roll(u, 1) || !ai_cast(u, t, 0, 1, 0, MF_HOMING))
                ai_circle(u, tp, 5.0f);
            return 1;
        }
        if (d >= u->aip[4]) {
            if (d >= u->aip[6])
                ai_stall(u, 15);
            else
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (ai_roll(u, 1) && ai_cast(u, t, 0, 1, 0, MF_HOMING))
            return 1;
        if (tp->mana_drained > 0)
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, u->aip[7]);
        return 1;
    }
    case AI_REGURG: { /* aip: pegar, comer al lado, acercarse, buscar comida, buscar comida peleando, olfato */
        /* ai_p[0]: 0 nada, 2 yendo a la comida, 3 al lado, 4 comió (escupe), 5 se aleja */
        if (!tp)
            return 0;
        switch (u->ai_p[0]) {
        case 2: {
            const Unit *c = u->a4_corpse >= 0 ? &s_units[u->a4_corpse] : NULL;
            if (!c || c->state != ST_DEAD || c->hidden) {
                u->ai_p[0] = 0;
                ai_wander(u, 8, 100);
                return 1;
            }
            if (dist_to(u, c->x, c->y) > 2.0f) {
                if (++u->ai_p[2] >= 6) {
                    u->ai_p[0] = 0;
                    ai_wander(u, 8, 100);
                    return 1;
                }
                ai_go(u, c->x, c->y, 10, 100, 0);
                return 1;
            }
            u->ai_p[0] = 3;
            ai_stall(u, 8);
            return 1;
        }
        case 3: {
            const Unit *c = u->a4_corpse >= 0 ? &s_units[u->a4_corpse] : NULL;
            u->ai_p[0] = 0;
            if (c && c->state == ST_DEAD && !c->hidden && a4_use(u, t, 0, A4H_EAT)) {
                u->ai_p[0] = 4;
                return 1;
            }
            ai_wander(u, 8, 100);
            return 1;
        }
        case 4: /* lleno: escupe (A2) o se aleja */
            if (combat) {
                ai_escape(u, tp, 8.0f);
            } else {
                ai_attack(u, t, ANIM_ATTACK2);
                u->ai_p[0] = 5;
            }
            return 1;
        case 5:
            ai_escape(u, tp, 16.0f);
            u->ai_p[0] = 0;
            return 1;
        default:
            break;
        }
        float smell = (float)u->aip[5];
        int food = a4_food(u, smell);
        if (food >= 0 && ((dist_to(u, s_units[food].x, s_units[food].y) < 3.0f && ai_roll(u, 1)) || ai_roll(u, 4))) {
            u->a4_corpse = food;
            u->ai_p[0] = 2;
            u->ai_p[2] = 0;
            ai_go(u, s_units[food].x, s_units[food].y, 10, 100, 0);
            return 1;
        }
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, 15);
            return 1;
        }
        if (ai_roll(u, 2)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (food >= 0 && ai_roll(u, 3)) {
            u->a4_corpse = food;
            u->ai_p[0] = 2;
            u->ai_p[2] = 0;
            ai_go(u, s_units[food].x, s_units[food].y, 10, 100, 0);
            return 1;
        }
        ai_stall(u, 8);
        return 1;
    }
    case AI_DOOMKNIGHT: /* aip: pegar, quieto al lado, acercarse, quieto */
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[1]);
            return 1;
        }
        if (ai_roll(u, 2))
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    case AI_ABYSSKNIGHT: /* aip: vida para la armadura, armadura, pegar, quieto, no dispara a menos, recarga, acercarse, distancia */
        if (!tp)
            return 0;
        if (u->bone_armor <= 0 && life < u->aip[0] && ai_roll(u, 1) && a4_use(u, t, 1, A4H_ARMOR))
            return 1;
        if (combat) {
            if (ai_roll(u, 2))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[3]);
            return 1;
        }
        if (d < u->aip[4] && u->ai_p[0] <= 0)
            u->ai_p[0] = u->aip[5];
        if (!u->ai_p[0] && ai_cast(u, t, 0, 1, 0, 0)) {
            u->ai_p[0] = u->aip[5];
            return 1;
        }
        if (u->ai_p[0] > 0)
            u->ai_p[0]--;
        if (ai_roll(u, 6)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (d < u->aip[7])
            ai_circle(u, tp, 3.0f);
        else
            ai_stall(u, 15);
        return 1;
    case AI_OBLIVKNIGHT: { /* aip: huir a menos, pelear a menos, espera de la maldición, maldecir, disparar,
                              Espíritu de hueso, acercarse, distancia */
        if (!tp)
            return 0;
        int now = (int)s_unit_ticks;
        if (d < u->aip[0]) { /* muy cerca: lo maldice con Decrepitud, o se aleja */
            if (tp->curse != A4C_DECREPIFY && a4_use(u, t, 3, A4H_CURSE)) {
                u->a4_curse = A4C_DECREPIFY;
                u->ai_p[0] = now + u->aip[2];
                return 1;
            }
            /* detrás del Caballero de la Perdición más cercano (a menos de 50), si está más lejos del jugador */
            int dk = a4_doomknight(u);
            if (dk >= 0 && dist_to(&s_units[dk], tp->x, tp->y) > d) {
                ai_go(u, s_units[dk].x, s_units[dk].y, 10, 100, 0);
                return 1;
            }
            /* si no tiene adónde escapar (acorralado), el Espíritu de hueso */
            float dx = u->x - tp->x, dy = u->y - tp->y, l = sqrtf(dx * dx + dy * dy);
            l = l > 0.01f ? l : 1.0f;
            float ex = u->x + dx / l * 10.0f, ey = u->y + dy / l * 10.0f;
            if (m && (wall_foot(m, ex, ey, 1, COLL_MASK_MONSTER_PATH) ||
                      map_ray(m, (int)u->x, (int)u->y, (int)ex, (int)ey, COLL_MASK_MONSTER_PATH, NULL, NULL)) &&
                ai_cast(u, t, 2, 1, 0, MF_HOMING))
                return 1;
            ai_escape(u, tp, 10.0f);
            u->ai_spd = 150;
            return 1;
        }
        if (d < u->aip[1]) {
            if (now > u->ai_p[0] && ai_roll(u, 3) && a4_use(u, t, 5, A4H_CURSE)) { /* MonCurseCast: una al azar */
                u->a4_curse = (int8_t)(A4C_AMPLIFY + (int)(rnd(u) % 6u));
                u->ai_p[0] = now + u->aip[2];
                return 1;
            }
            if (ai_roll(u, 4)) {
                if (ai_roll(u, 5) && ai_cast(u, t, 2, 1, 0, MF_HOMING))
                    return 1;
                if (ai_cast(u, t, 0, 1, 0, 0))
                    return 1;
            }
        }
        if (d > u->aip[7] && ai_roll(u, 6)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (rnd(u) % 100 >= 70)
            ai_stall(u, 10);
        else
            ai_circle(u, tp, 3.0f);
        return 1;
    }
    case AI_MEGADEMON: { /* aip: aliento de lejos, aliento de cerca, pegar, acercarse, rodear, espera del aliento */
        if (!tp)
            return 0;
        int now = (int)s_unit_ticks, lvl = ai_sklvl(u, 0);
        if (!combat && d < (float)lvl) { /* a menos de su nivel de subtiles: el aliento */
            if (now > u->ai_p[0] && ai_roll(u, 0) && a4_use(u, t, 0, A4H_INFERNO)) {
                u->ai_p[0] = now + u->aip[5];
                return 1;
            }
        } else if (combat) {
            if (now > u->ai_p[0] && ai_roll(u, 1) && a4_use(u, t, 0, A4H_INFERNO)) {
                u->ai_p[0] = now + u->aip[5];
                return 1;
            }
            if (ai_roll(u, 2))
                ai_attack(u, t, ANIM_ATTACK);
            else if (ai_roll(u, 4))
                ai_circle(u, tp, 3.0f);
            else
                ai_stall(u, 5);
            return 1;
        }
        if (ai_roll(u, 3))
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, 10);
        return 1;
    }
    case AI_BIGHEAD: /* aip: herido, rodear, escupir sano, escupir herido */
        if (!tp)
            return 0;
        if (!combat && hurt) {
            ai_attack(u, t, ANIM_ATTACK2);
            return 1;
        }
        if (life >= u->aip[0]) {
            if (combat)
                ai_attack(u, t, ANIM_ATTACK);
            else if (d < 15.0f && ai_roll(u, 2))
                ai_attack(u, t, ANIM_ATTACK2);
            else
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (d >= 3.0f) {
            if (d > 15.0f)
                ai_go(u, tp->x, tp->y, 6, 100, 0);
            else if (ai_roll(u, 3))
                ai_attack(u, t, ANIM_ATTACK2);
            else if (ai_roll(u, 1))
                ai_circle(u, tp, 3.0f);
            else
                ai_stall(u, 10);
            return 1;
        }
        ai_escape(u, tp, 5.0f);
        u->ai_spd = 50;
        return 1;
    case AI_IZUAL: { /* aip: pegar, acercarse, nova de lejos, nova de cerca, quieto después, golpes después */
        if (!tp)
            return 0;
        if (!u->ai_p[0]) /* (ACT4Q1_OnIzualActivated: a4quest.inc lo ve por su cuenta) */
            u->ai_p[0] = 1;
        if (u->ai_p[1]) {
            ai_stall(u, u->ai_p[1]);
            u->ai_p[1] = 0;
            return 1;
        }
        if (!combat) {
            if (d < 10.0f) {
                if (u->ai_p[2] > 0) {
                    ai_go(u, tp->x, tp->y, 10, 100, 1);
                    return 1;
                }
                if (ai_roll(u, 2) && ai_cast(u, t, 0, 64, 1, MF_GROUP)) {
                    u->ai_p[1] = u->aip[4];
                    u->ai_p[2] = u->aip[5];
                    return 1;
                }
            }
            if (u->ai_p[2] <= 0 && !ai_roll(u, 1)) {
                if (d <= 10.0f)
                    ai_stall(u, u->attack_delay);
                else
                    ai_circle(u, tp, 7.0f);
                return 1;
            }
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (u->ai_p[2] <= 0 && !ai_roll(u, 0)) {
            u->ai_p[2] = 0;
            if (ai_roll(u, 3) && ai_cast(u, t, 0, 64, 1, MF_GROUP)) {
                u->ai_p[1] = u->aip[4];
                u->ai_p[2] = u->aip[5];
            } else {
                ai_stall(u, u->attack_delay);
            }
            return 1;
        }
        if (u->ai_p[2] > 0)
            u->ai_p[2]--;
        ai_attack(u, t, ANIM_ATTACK);
        return 1;
    }
    case AI_SMITH: /* pega; herido, se acerca más rápido */
        if (!tp)
            return 0;
        if (combat) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        ai_go(u, tp->x, tp->y, 10, 100 + (100 - (life < 0 ? 0 : life)) / 2, 1);
        return 1;
    case AI_HELLMETEOR: { /* aip: lanzar, quieto, alcance: los meteoros caen al azar alrededor (no se ve: sin animación) */
        if (ai_roll(u, 0)) {
            int r = u->aip[2] > 0 ? u->aip[2] : 10;
            float x = u->x + (float)((int)(rnd(u) % (uint32_t)(2 * r)) - r), y = u->y + (float)((int)(rnd(u) % (uint32_t)(2 * r)) - r);
            Missile *mi = missiles_effect("hellmeteordown", x, y);
            if (mi) {
                mi->flags |= MF_EFFECT | MF_LAND;
                mi->life = 25;
                mi->elem = ELEM_FIRE;
                missiles_level_damage("hellmeteordown", ai_sklvl(u, 0), &mi->emin, &mi->emax);
                mi->radius10 = 30;
                mi->owner = -1;
                mi->source = (int)(u - s_units);
                mi->ar = u->ar;
                mi->level = u->level;
            }
        }
        ai_stall(u, u->aip[1] > 0 ? u->aip[1] : 50);
        return 1;
    }
    case AI_TRAPPEDSOUL: { /* estira los brazos hacia el que pasa cerca */
        int now = (int)s_unit_ticks;
        if (tp && d < 5.0f) {
            if (!u->ai_p[0]) {
                u->ai_p[0] = 1;
                u->ai_p[1] = now;
                ai_attack(u, t, u->gfx->anim[ANIM_SKILL2].ok ? ANIM_SKILL2 : ANIM_ATTACK);
                return 1;
            }
            if (combat && now > u->ai_p[1]) {
                int right = u->x >= tp->x, front = u->y <= tp->y;
                int anim = right && front ? ANIM_ATTACK : (u->x > tp->x || u->y < tp->y) ? ANIM_SKILL : ANIM_ATTACK2;
                ai_attack(u, t, anim);
                u->ai_p[1] = now + (anim == ANIM_SKILL ? 5 : 35);
                return 1;
            }
            ai_attack(u, t, ANIM_SKILL);
            return 1;
        }
        if (u->ai_p[0] && tp)
            ai_attack(u, t, ANIM_SKILL);
        else
            ai_stall(u, 15);
        return 1;
    }
    case AI_DIABLO:
        return a4_diablo(u, m, pl, t, d);
    default:
        return ai_think5(u, m, pl, t, d, hurt);
    }
}
