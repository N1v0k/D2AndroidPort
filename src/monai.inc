/* ---- Las IA de los monstruos del original (se incluye desde units.c) ----

   Como AiThink.cpp de D2MOO: los que tienen la suya deciden qué hacer cada
   vez que terminan lo anterior, tirando contra sus parámetros (aip1..8 de
   monstats.txt, los de Normal): pegar con su primer o segundo ataque,
   quedarse quietos un rato (IdleInNeutralMode), acercarse (a veces más
   despacio), dar una vuelta alrededor del jugador, reunirse con los de su
   manada, alejarse, o usar su habilidad, que es una secuencia de
   monseq.txt (el salto, la carga de la víbora, la estocada, enterrarse...).
   Los que no están acá siguen con la del motor: acercarse y pegar, o
   disparar desde lejos.

   shortcut: sin las órdenes de los jefes de grupo (el escarabajo que manda
   a cargar a los suyos), ni Bestow (la momia mayor que cura), ni el color
   de la carga del jinete y la víbora; el buitre que se va volando no se
   dibuja mientras vuela; los murciélagos demonio no se posan a descansar
   (el motor no tiene sus modos S2..S4): herido, se alejan hasta tres veces. */

/* AIRollChanceParam: el dado contra el parámetro k (aip1 = 0) */
static int ai_roll(Unit *u, int k)
{
    return (int)(rnd(u) % 100) < u->aip[k];
}

/* IdleInNeutralMode: quieto unos ticks */
static void ai_stall(Unit *u, int ticks)
{
    u->ai_wait = ticks > 1 ? ticks : 1;
    u->ai_move = 0;
    u->walking = 0;
    u->running = 0;
}

/* Ir hacia (x, y) unos ticks, a `pct` de su velocidad; con `follow`, detrás
   del jugador (WalkToTargetUnit) */
static void ai_go(Unit *u, float x, float y, int ticks, int pct, int follow)
{
    u->ai_tx = x;
    u->ai_ty = y;
    u->ai_move = ticks;
    u->ai_spd = pct;
    u->ai_follow = follow;
    u->ai_run = 0;
}

/* Lo mismo corriendo (RunToTargetUnit: el modo RN y la velocidad Run de
   monstats.txt; sin RN, camina) */
static void ai_run_to(Unit *u, float x, float y, int ticks, int pct, int follow)
{
    ai_go(u, x, y, ticks, pct, follow);
    u->ai_run = u->run_speed > 0.0f && u->gfx->anim[ANIM_RUN].ok;
}

/* AITACTICS_WalkCloseToUnit: a un lugar al azar a `n` subtiles de donde está */
static void ai_wander(Unit *u, int n, int pct)
{
    int ox, oy;
    if (rnd(u) & 1) {
        ox = n;
        oy = (int)(rnd(u) % (uint32_t)n);
    } else {
        ox = (int)(rnd(u) % (uint32_t)n);
        oy = n;
    }
    if (rnd(u) & 1)
        ox = -ox;
    if (rnd(u) & 1)
        oy = -oy;
    ai_go(u, u->x + (float)ox, u->y + (float)oy, 15, pct, 0);
}

/* sub_6FCD0E80: una vuelta alrededor del jugador a `r` subtiles */
static void ai_circle(Unit *u, const UnitTarget *t, float r)
{
    float a = atan2f(u->y - t->y, u->x - t->x) + (rnd(u) % 2 ? 1.1f : -1.1f);
    r = r < 2.0f ? 2.0f : r;
    ai_go(u, t->x + cosf(a) * r, t->y + sinf(a) * r, 15, 100, 0);
}

/* D2GAME_AICORE_Escape: lejos del jugador */
static void ai_escape(Unit *u, const UnitTarget *t, float dist)
{
    float dx = u->x - t->x, dy = u->y - t->y, l = sqrtf(dx * dx + dy * dy);
    l = l > 0.01f ? l : 1.0f;
    ai_go(u, u->x + dx / l * dist, u->y + dy / l * dist, 20, 100, 0);
}

/* ChangeModeAndTargetUnit: pegarle (o dispararle, si ese ataque dispara) */
static void ai_attack(Unit *u, int t, int anim)
{
    if (!u->gfx->anim[anim].ok)
        anim = ANIM_ATTACK;
    u->target = t;
    u->attack_anim = anim;
    u->ranged_attack = u->ranged && anim == u->ranged_anim;
    u->res_target = -1;
    u->mm_attack = 0;
    u->ai_move = 0;
    set_state(u, ST_ATTACK);
    u->hit_done = 0;
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s ataca %s\n", u->name, anim == ANIM_ATTACK2 ? "A2" : anim == ANIM_SKILL ? "S1" : anim == ANIM_CAST ? "SC" : "A1");
}

/* AITACTICS_ChangeModeAndTargetUnitToAttack1Or2 */
static void ai_attack12(Unit *u, int t, int k)
{
    ai_attack(u, t, u->gfx->anim[ANIM_ATTACK2].ok && ai_roll(u, k) ? ANIM_ATTACK2 : ANIM_ATTACK);
}

/* UseSkill: la secuencia de su habilidad k (0 si no tiene o le falta un modo) */
static int ai_skill(Unit *u, int t, int k)
{
    const MonSeq *q = u->seqs[k];
    if (!q)
        return 0;
    for (int s = 0; s < q->n; s++)
        if (!u->gfx->anim[q->anim[s]].ok)
            return 0;
    u->target = t;
    u->seq = q;
    u->seq_skill = k;
    u->attack_anim = q->anim[0];
    u->ranged_attack = 0;
    u->mm_attack = 0;
    u->ai_move = 0;
    set_state(u, ST_ATTACK);
    u->jab = 1;
    u->hit_done = 0;
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s usa %s\n", u->name, u->skill[k]);
    return 1;
}

/* El más cercano de los suyos (el mismo BaseId de monstats.txt), vivo */
static int ai_packmate(const Unit *u, float *dist)
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == u || !o->enemy || o->state >= ST_DYING || o->hidden || strcmp(o->base, u->base) != 0)
            continue;
        float d = dist_to(u, o->x, o->y);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    *dist = bd;
    return best;
}

/* La momia mayor (AITHINK_TargetCallback_GreaterMummy): un no muerto caído
   para revivir y cuántos no muertos de los suyos tiene cerca */
static int ai_dead_undead(const Unit *u, float range, int *allies)
{
    int best = -1;
    *allies = 0;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == u || !o->enemy || !o->undead || o->unique || dist_to(u, o->x, o->y) > range)
            continue;
        (*allies)++;
        if (o->state == ST_DEAD && !o->hidden && o->revivable && !o->searched)
            best = i;
    }
    return best;
}

/* El salto del saltador (MonLeap): hasta al lado del jugador, si ahí se puede pisar */
static int ai_leap(Unit *u, const Map *m, int t, const UnitTarget *tp)
{
    float dx = tp->x - u->x, dy = tp->y - u->y, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.5f)
        return 0;
    float back = u->reach * 0.8f, lx = tp->x - dx / l * back, ly = tp->y - dy / l * back;
    if (m && wall_foot(m, lx, ly, u->foot, u->move_mask))
        return 0;
    if (!ai_skill(u, t, 0))
        return 0;
    u->leap_x0 = u->x;
    u->leap_y0 = u->y;
    u->leap_x1 = lx;
    u->leap_y1 = ly;
    u->sector = sector_from_world(dx, dy);
    return 1;
}

/* El Invocador lanza uno de sus hechizos (el de su habilidad k, con el
   nivel de monstats.txt Sk<k>lvl): la Púa glacial, la Nova helada (en
   anillo) o la Bola de fuego. 0 si no tiene la animación. */
enum { SUMM_SPIKE, SUMM_NOVA, SUMM_FIREBALL };
static int summoner_cast(Unit *u, int t, int what)
{
    static const struct { const char *skill, *missile; int k, elem, ring; } k_spells[3] = {
        { "Glacial Spike", "glacialspike", 0, ELEM_COLD, 0 },
        { "Frost Nova", "frostnova", 1, ELEM_COLD, 16 },
        { "Fire Ball", "fireball", 2, ELEM_FIRE, 0 },
    };
    if (!u->gfx->anim[ANIM_CAST].ok || !missiles_exists(k_spells[what].missile))
        return 0;
    int ms = txt_find_row(&s_monstats, txt_col(&s_monstats, "Id"), u->base);
    char col[24];
    snprintf(col, sizeof col, "Sk%dlvl", k_spells[what].k + 1);
    int lvl = ms >= 0 ? txt_get_int(&s_monstats, ms, col, 1) : 1;
    skill_elem_damage(k_spells[what].skill, lvl > 0 ? lvl : 1, &u->spell_min, &u->spell_max);
    snprintf(u->missile, sizeof u->missile, "%s", k_spells[what].missile);
    u->missile2[0] = '\0';
    u->spell_elem = k_spells[what].elem;
    u->ring = k_spells[what].ring;
    u->ranged = 1;
    u->ranged_anim = ANIM_CAST;
    ai_attack(u, t, ANIM_CAST);
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s lanza %s (nivel %d, %d-%d)\n", u->name, k_spells[what].skill, lvl, u->spell_min, u->spell_max);
    return 1;
}

#include "monai1.inc"
#include "monai3.inc"
#include "monai4.inc"
#include "monai5.inc"
#include "monaiu.inc"

/* Lo que decide cada IA cuando terminó lo anterior. t: el jugador que ve
   (-1 = ninguno) a d subtiles. 0 = que siga la del motor. */
static int ai_think(Unit *u, const Map *m, UnitTarget *pl, int t, float d)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    int combat = tp && d <= u->reach;
    float md;
    if (tp)
        u->sector = sector_from_world(tp->x - u->x, tp->y - u->y);
    if (u->uber && uber_think(u, m, pl, t)) /* lo de los Uber, antes de la IA de su jefe */
        return 1;
    if (u->colossal && colossal_think(u, pl, t)) /* y lo de los Ancianos colosales, antes de la de los Antiguos */
        return 1;
    switch (u->ai) {
    case AI_SANDLEAPER: /* aip: salto, ataque, acercarse, rodear */
        if (!tp)
            return 0;
        if (d < 5.0f && ai_roll(u, 0) && ai_leap(u, m, t, tp))
            return 1;
        if (combat) {
            if (ai_roll(u, 1)) {
                ai_attack(u, t, ANIM_ATTACK2);
                return 1;
            }
        } else if (d > 10.0f) {
            ai_go(u, tp->x, tp->y, 10, 75, 1);
            return 1;
        } else if (ai_roll(u, 2)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        } else if (ai_roll(u, 3)) {
            ai_circle(u, tp, 4.0f);
            return 1;
        }
        ai_stall(u, 10);
        return 1;
    case AI_PANTHER: { /* aip: acercarse, ataque, distancia a la manada, quieto */
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 1))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[3]);
            return 1;
        }
        if (ai_roll(u, 0)) {
            ai_go(u, tp->x, tp->y, 10, 75, 1);
            return 1;
        }
        int mate = ai_packmate(u, &md);
        if (mate >= 0 && md > u->aip[2])
            ai_go(u, s_units[mate].x, s_units[mate].y, 10, 75, 0);
        else if (rnd(u) % 100 < 25)
            ai_circle(u, tp, 3.0f);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    }
    case AI_PANTHERJAV: { /* aip: acercarse, tirar, distancia a la manada, alejarse, quieto, distancia de tiro */
        if (!tp)
            return 0;
        int range = u->aip[5] > 0 ? u->aip[5] : 20;
        if (d < 8.0f && ai_roll(u, 3)) {
            ai_escape(u, tp, 16.0f);
            return 1;
        }
        if (d > range - 6 && ai_roll(u, 0)) {
            ai_go(u, tp->x, tp->y, 8, 100, 1);
            return 1;
        }
        if (d >= range) {
            int mate = ai_packmate(u, &md);
            if (mate >= 0 && md > u->aip[2])
                ai_go(u, s_units[mate].x, s_units[mate].y, 10, 100, 0);
            else
                ai_stall(u, u->aip[4]);
            return 1;
        }
        if (ai_roll(u, 1))
            ai_attack(u, t, ANIM_ATTACK);
        else
            ai_stall(u, u->aip[4]);
        return 1;
    }
    case AI_SCARAB: /* aip: ataque, A1 o A2, quieto, estocada, (órdenes) */
        if (!tp)
            return 0;
        if (!combat) { /* una vuelta, después derecho al jugador */
            if (u->ai_p[0]) {
                ai_go(u, tp->x, tp->y, 10, 100, 1);
                if (rnd(u) % 100 > 10)
                    u->ai_p[0] = 0;
            } else {
                ai_circle(u, tp, 2.0f);
                u->ai_p[0] = 1;
            }
            return 1;
        }
        if (ai_roll(u, 0)) {
            if (!(u->seqs[0] && ai_roll(u, 3) && ai_skill(u, t, 0)))
                ai_attack12(u, t, 1);
            return 1;
        }
        ai_stall(u, u->aip[2]);
        return 1;
    case AI_VULTURE: /* aip: ataque, quieto, herido, rodear, moverse */
        if (u->ai_p[0] > 0) { /* volando: baja al lado del jugador */
            if (!tp || --u->ai_p[0] <= 0 || dist_to(u, u->ai_tx, u->ai_ty) < 1.0f) {
                u->ai_p[0] = 0;
                u->hidden = 0;
                u->solid = 1;
                ai_stall(u, 12);
                return 1;
            }
            ai_go(u, u->ai_tx, u->ai_ty, 4, 250, 0);
            return 1;
        }
        if (!tp)
            return 0;
        if (d > 12.0f && rnd(u) % 100 < 60) { /* se va volando y baja cerca del jugador */
            float a = rndf(u) * 2.0f * PI_F, r = 3.0f + rndf(u) * 3.0f;
            float lx = tp->x + cosf(a) * r, ly = tp->y + sinf(a) * r;
            if (!m || !wall_foot(m, lx, ly, u->foot, u->move_mask)) {
                u->hidden = 1;
                u->solid = 0;
                u->ai_p[0] = 24 + (int)(rnd(u) % 8);
                ai_go(u, lx, ly, 4, 250, 0);
                return 1;
            }
        }
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[1]);
            return 1;
        }
        { /* herido, va más rápido (como el original: 100 - % de vida, entre 0 y 60) */
            int life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
            life = life < 40 ? 40 : life > 100 ? 100 : life;
            ai_go(u, tp->x, tp->y, 10, 100 + (100 - life), 1);
        }
        return 1;
    case AI_MAGGOT: { /* aip: escupir, poner huevos, cuántos huevos, morder, tiempo mínimo arriba o abajo */
        if (u->ai_p[1] > 0)
            u->ai_p[1]--;
        if (u->ai_p[0] == 3) { /* enterrado: sale cuando hay alguien cerca */
            if (tp && d < 16.0f && u->ai_p[1] <= 0 && u->seqs[0]) {
                u->hidden = 0;
                u->solid = 1;
                if (ai_skill(u, t, 0)) {
                    u->ai_p[0] = 1;
                    u->ai_p[1] = u->aip[4];
                    return 1;
                }
            }
            ai_stall(u, 20);
            return 1;
        }
        int hurt = u->max_hp > 0 && u->hp * 4 < u->max_hp;
        if (((!tp || d > 10.0f) && u->ai_p[1] <= 0) || (tp && hurt && d < 7.0f && u->ai_p[1] <= 0 && rnd(u) % 100 < 20)) {
            if (u->seqs[1] && ai_skill(u, t, 1)) { /* se entierra */
                u->ai_p[0] = 3;
                u->ai_p[1] = u->aip[4];
                return 1;
            }
        }
        if (!tp) {
            ai_stall(u, 20);
            return 1;
        }
        if (combat && ai_roll(u, 3)) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (d < 15.0f && ai_roll(u, 0) && u->ranged) {
            ai_attack(u, t, ANIM_ATTACK2); /* escupe (goospit) */
            return 1;
        }
        if (rnd(u) % 100 < 20) {
            ai_circle(u, tp, 6.0f);
            return 1;
        }
        if (u->ai_p[2] >= u->aip[2] || !ai_roll(u, 1)) {
            ai_stall(u, 12);
            return 1;
        }
        if (u->ai_p[0] == 2 && u->seqs[2] && ai_skill(u, t, 2)) { /* pone un huevo */
            u->ai_p[0] = 1;
            u->ai_p[2]++;
            return 1;
        }
        ai_circle(u, tp, 6.0f);
        u->ai_p[0] = 2;
        return 1;
    }
    case AI_MAGGOTEGG: /* aip: quieto, nacer */
        if (u->ai_p[0] == 0 && ai_roll(u, 1) && u->gfx->anim[ANIM_SKILL].ok) {
            u->ai_p[0] = 1; /* se abre: sale la larva y el huevo se termina */
            ai_attack(u, t, ANIM_SKILL);
            return 1;
        }
        ai_stall(u, u->aip[0] > 0 ? u->aip[0] : 50);
        return 1;
    case AI_MAGGOTLARVA: /* aip: ataque, descanso después de morder, acercarse, quieto */
        if (!tp)
            return 0;
        if (combat) {
            if (u->ai_p[0] || !ai_roll(u, 0)) {
                u->ai_p[0] = 0;
                ai_stall(u, u->aip[1]);
            } else {
                u->ai_p[0] = 1;
                ai_attack(u, t, ANIM_ATTACK);
            }
            return 1;
        }
        u->ai_p[0] = 0;
        if (ai_roll(u, 2))
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    case AI_SWARM: /* aip: acercarse, quieto, ataque (Goatman_Swarm) */
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 2)) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
        } else if (ai_roll(u, 0)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        ai_stall(u, u->aip[1]);
        return 1;
    case AI_CLAWVIPER: /* aip: carga, distancia de carga, ataque, A1 o A2, quieto */
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 2))
                ai_attack12(u, t, 3);
            else
                ai_stall(u, u->aip[4]);
            return 1;
        }
        if (u->seqs[0] && d < u->aip[1] && ai_roll(u, 0) && ai_skill(u, t, 0))
            return 1;
        if (rnd(u) % 100 < 50)
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, u->aip[4]);
        return 1;
    case AI_MUMMY: /* aip: distancia a la que se despierta, acercarse, ataque, A1 o A2, quieto */
        if (!tp)
            return 0;
        if (d > u->aip[0]) {
            if (ai_roll(u, 1))
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            else
                ai_stall(u, u->aip[4]);
            return 1;
        }
        if (!combat) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (!ai_roll(u, 2)) {
            ai_stall(u, u->aip[4]);
            return 1;
        }
        ai_attack12(u, t, 3);
        return 1;
    case AI_GMUMMY: { /* aip: pegar o echar el aliento, revivir, (curar), (disparar), distancia para revivir */
        if (!tp)
            return 0;
        if (combat && ai_roll(u, 0)) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        if (d < 5.0f && ai_roll(u, 0) && u->ranged) {
            ai_attack(u, t, ANIM_ATTACK2); /* el aliento venenoso */
            return 1;
        }
        int allies, dead = ai_dead_undead(u, (float)(u->aip[4] > 0 ? u->aip[4] : 10), &allies);
        if (dead >= 0 && ai_roll(u, 1) && ai_skill(u, t, 0)) {
            u->res_target = dead;
            return 1;
        }
        if (allies <= 0)
            ai_go(u, tp->x, tp->y, 10, 50, 1);
        else if (rnd(u) % 100 < 50)
            ai_circle(u, tp, 3.0f);
        else
            ai_stall(u, 6);
        return 1;
    }
    case AI_SANDRAIDER: { /* aip: herido, rodear, ataque, acercarse, cuánto tarda en cargar, (color), A2 o A1 */
        if (!tp)
            return 0;
        u->ai_p[0]++;
        if (u->ai_p[0] > u->aip[4])
            u->ai_p[1] = 1; /* cargado: el próximo golpe es el de fuego */
        if (u->ai_p[2] < 7 && u->max_hp > 0 && u->hp * 100 / u->max_hp < u->aip[0]) {
            int mate = ai_packmate(u, &md);
            if (mate >= 0) { /* herido: se va con los suyos */
                ai_go(u, s_units[mate].x, s_units[mate].y, 10, 100, 0);
                u->ai_p[2]++;
                return 1;
            }
        }
        if (d > 4.0f && !u->ai_p[1] && ai_roll(u, 1)) {
            ai_circle(u, tp, 3.0f);
            return 1;
        }
        if (combat) {
            if (u->ai_p[1] && u->seqs[0] && ai_skill(u, t, 0)) {
                u->ai_p[0] = u->ai_p[1] = 0;
                return 1;
            }
            if (ai_roll(u, 2)) {
                ai_attack(u, t, ai_roll(u, 6) ? ANIM_ATTACK2 : ANIM_ATTACK);
                return 1;
            }
        } else if (u->ai_p[1] || ai_roll(u, 3)) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        int wait = 24 - u->aip[4] > 6 ? 24 - u->aip[4] : 6;
        if (u->ai_p[0] > wait + u->aip[4])
            u->ai_p[0] = u->ai_p[1] = 0;
        ai_stall(u, 15);
        return 1;
    }
    case AI_DURIEL: /* aip: nivel de la Congelación sagrada (holy_freeze), Smite, Jab, A2, carga */
        if (!tp)
            return 0;
        if (combat) {
            if (u->seqs[2] && ai_roll(u, 1) && ai_skill(u, t, 2)) /* Smite */
                return 1;
            if (u->seqs[1] && ai_roll(u, 2) && ai_skill(u, t, 1)) /* Jab */
                return 1;
            ai_attack(u, t, ai_roll(u, 3) ? ANIM_ATTACK2 : ANIM_ATTACK);
            return 1;
        }
        if (u->seqs[0] && ai_roll(u, 4) && ai_skill(u, t, 0)) /* la carga */
            return 1;
        ai_go(u, tp->x, tp->y, 10, 130, 1); /* AITACTICS_SetVelocity 13 */
        return 1;
    case AI_PINHEAD: /* AITHINK_Fn039 (los Blunderbore): aip ataque, quieto después, acercarse, quieto, Smite */
        if (!tp)
            return 0;
        if (combat) {
            if (u->ai_p[0] && !ai_roll(u, 0)) {
                ai_stall(u, u->aip[1]);
                return 1;
            }
            u->ai_p[0] = 1;
            if (u->seqs[0] && ai_roll(u, 4) && ai_skill(u, t, 0)) /* Smite */
                return 1;
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        u->ai_p[0] = 0;
        if (ai_roll(u, 2))
            ai_go(u, tp->x, tp->y, 10, 100, 1);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    case AI_SUMMONER: { /* AITHINK_Fn053: aip lanzar, Debilitar, elemento preferido, demora de la Nova y del
                           muro, alejarse, distancia de la Nova, de los proyectiles */
        if (!tp)
            return 0;
        if (d < 5.0f && ai_roll(u, 5)) {
            ai_escape(u, tp, 6.0f);
            return 1;
        }
        if (ai_roll(u, 0)) {
            int cold = tp->res[0] >= tp->res[1]; /* el que menos resiste, casi siempre */
            if ((int)(rnd(u) % 100) > u->aip[2])
                cold = !cold;
            if (cold && u->ai_p[0] <= 0 && d < u->aip[6] && summoner_cast(u, t, SUMM_NOVA)) {
                u->ai_p[0] = u->aip[3];
                return 1;
            }
            if (cold && d < u->aip[7] && summoner_cast(u, t, SUMM_SPIKE))
                return 1;
            /* shortcut: el muro de fuego (VampireFirewall) es una Bola de fuego: el motor no tiene
               fuego en el piso de los monstruos */
            if (u->ai_p[1] <= 0 && d < u->aip[7] && summoner_cast(u, t, SUMM_FIREBALL)) {
                u->ai_p[1] = u->aip[4];
                return 1;
            }
            if (d < u->aip[7] && summoner_cast(u, t, SUMM_FIREBALL))
                return 1;
            if (!cold && u->ai_p[0] <= 0 && d < u->aip[6] && summoner_cast(u, t, SUMM_NOVA)) {
                u->ai_p[0] = u->aip[3];
                return 1;
            }
            /* shortcut: Debilitar no tiene efecto (el jugador no tiene ese estado) */
        }
        ai_circle(u, tp, d > 4.0f ? d : 4.0f); /* WalkCloseToUnit */
        return 1;
    }
    case AI_MAGGOTQUEEN: /* aip: cuántos gusanos pare, demora. No ataca ni camina */
        if (u->ai_p[1]) { /* recién parió: quieta (IdleInNeutralMode, en segundos) */
            u->ai_p[1] = 0;
            ai_stall(u, 25 * (u->aip[1] > 0 ? u->aip[1] : 12));
            return 1;
        }
        if (tp && u->ai_p[0] < u->aip[0] && u->gfx->anim[ANIM_SKILL].ok && u->minion[0]) {
            ai_attack(u, t, ANIM_SKILL); /* el gusano sale en el cuadro de golpe */
            u->ai_p[1] = 1;
            return 1;
        }
        ai_stall(u, 25);
        return 1;
    case AI_BATDEMON: { /* aip: herido (%), alejarse al recibir un golpe, ataque, A2 (el rayo) o A1, (posado) */
        if (!tp)
            return 0;
        int hit = u->ai_p[2], life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
        u->ai_p[2] = 0;
        if (life < u->aip[0] && u->ai_p[1] < 3) {
            u->ai_p[1]++;
            u->ai_p[0] = 2;
            ai_escape(u, tp, 15.0f);
            return 1;
        }
        if (u->ai_p[0] == 2) { /* alejado: vuelve, da una vuelta o espera */
            int r = (int)(rnd(u) % 100);
            if (r < 33) {
                u->ai_p[0] = 0;
                ai_go(u, tp->x, tp->y, 10, 100, 1);
            } else if (r < 43) {
                ai_circle(u, tp, 6.0f);
            } else {
                ai_stall(u, 10);
            }
            return 1;
        }
        if (!combat) {
            ai_go(u, tp->x, tp->y, 10, 100, 1);
            return 1;
        }
        if (hit && ai_roll(u, 1)) {
            u->ai_p[0] = 2;
            ai_escape(u, tp, 12.0f);
            return 1;
        }
        if (!ai_roll(u, 2)) {
            ai_stall(u, 10);
            return 1;
        }
        ai_attack(u, t, ai_roll(u, 3) ? ANIM_ATTACK2 : ANIM_ATTACK);
        return 1;
    }
    default:
        return ai_think3(u, m, pl, t, d);
    }
}

/* La IA del original en este tick: 1 si se ocupó (si no, sigue la del motor) */
static int ai_tick(Unit *u, const Map *m, UnitTarget *pl, int t, float d)
{
    if (u->ai_wait > 0) { /* quieto */
        u->ai_wait--;
        u->walking = 0;
        u->anim_acc += u->gfx->anim[current_anim(u)].speed;
        return 1;
    }
    if (u->ai_move > 0) {
        u->ai_move--;
        float tx = u->ai_follow && t >= 0 ? pl[t].x : u->ai_tx, ty = u->ai_follow && t >= 0 ? pl[t].y : u->ai_ty;
        float sx, sy; /* cerca del jugador, a un lugar libre a su alrededor (no detrás de otro) */
        if (u->ai_follow && t >= 0 && d < SURROUND_NEAR && surround_spot(u, m, &pl[t], &sx, &sy)) {
            tx = sx;
            ty = sy;
        }
        if ((u->ai_follow && t >= 0 && d <= u->reach) || (!u->ai_follow && dist_to(u, tx, ty) < 0.4f)) {
            u->ai_move = 0; /* llegó: decide de nuevo */
        } else {
            float speed = u->speed;
            u->running = u->ai_run;
            u->speed = (u->ai_run ? u->run_speed : speed) * (float)u->ai_spd / 100.0f;
            move_towards(u, m, tx, ty);
            u->speed = speed;
            if (u->x == u->prev_x && u->y == u->prev_y) { /* trabado */
                if (u->ai_follow && t >= 0 && wall_ahead(m, u, tx, ty, 3.0f)) {
                    u->detour = u->detour > TICK_RATE ? u->detour : TICK_RATE; /* una pared: sigue el camino (chase_steer) */
                    u->has_slot = 0;
                } else {
                    u->ai_move = 0;
                    if (u->ai_follow && t >= 0) { /* otro le tapa el paso: da un rodeo y busca otro lugar alrededor del jugador */
                        u->has_slot = 0;
                        ai_circle(u, &pl[t], d);
                    }
                }
            }
            u->anim_acc += u->gfx->anim[current_anim(u)].speed;
            return 1;
        }
    }
    u->running = 0;
    if (!ai_think(u, m, pl, t, d))
        return 0;
    if (u->state != ST_ATTACK)
        u->anim_acc += u->gfx->anim[current_anim(u)].speed;
    return 1;
}

/* Los escarabajos, al recibir un golpe o al morir, sueltan rayos cargados
   hacia los cuatro lados (MONSTERUNIQUE_CastBugLightningMissile: dos por
   lado, del nivel del monstruo) */
static void scarab_bolts(Unit *u)
{
    static const float k_dir[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s suelta rayos\n", u->name);
    int lo, hi;
    missiles_level_damage("buglightning", u->level > 0 ? u->level : 1, &lo, &hi);
    for (int k = 0; k < 8; k++) {
        float a = atan2f(k_dir[k / 2][1], k_dir[k / 2][0]) + (k % 2 ? 0.25f : -0.25f);
        Missile *mi = missiles_spawn("buglightning", u->x, u->y, cosf(a), sinf(a));
        if (!mi)
            break;
        mi->owner = -1;
        mi->source = (int)(u - s_units);
        mi->ar = u->ar;
        mi->level = u->level;
        mi->dmin = mi->dmax = 0;
        mi->elem = ELEM_LTNG;
        mi->emin = lo;
        mi->emax = hi > lo ? hi : lo;
        mi->flags |= MF_CHARGED;
    }
}
