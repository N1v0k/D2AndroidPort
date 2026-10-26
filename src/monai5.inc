/* ---- Las IA de los monstruos del Acto 5 (se incluye desde monai.inc) ----

   Como AiThink.cpp y AiBaal.cpp de D2MOO (AITHINK_Fn076_EvilHole,
   Fn113_SiegeTower, Fn114_ReanimatedHorde, Fn115_SiegeBeast, Fn116_Minion,
   Fn117_SuicideMinion, Fn118_Succubus, Fn119_SuccubusWitch, Fn120_Overseer,
   Fn121_MinionSpawner, Fn122_Imp, Fn123_Catapult, Fn124_FrozenHorror,
   Fn125_BloodLord, Fn126_CatapultSpotter, Fn128_Nihlathak, Fn129_GenericSpawner,
   Fn130_DeathMauler, Fn132_AncientStatue, Fn133_Ancient, Fn134_BaalThrone,
   Fn135_BaalCrab, Fn136_BaalTaunt, Fn137_PutridDefiler, Fn138_BaalToStairs,
   Fn139_BaalTentacle, Fn140_BaalCrabClone y Fn141_BaalMinion), con sus
   parámetros aip de monstats.txt (Normal). Las habilidades: las de
   proyectil con ai_cast, las de aliento sostenido (el Arctic Blast de los
   horrores y de Nihlathak, el Inferno de Baal) como el rayo de Diablo
   (monai4.inc), las maldiciones que tiene el jugador (Amplify Damage,
   Weaken, Decrepify) y lo suyo en el cuadro de golpe (a5_hit_frame): las
   crías de los generadores, el pisotón de la bestia de asedio, la explosión
   de cadáver de Nihlathak, los tentáculos de Baal y su burla (los rayos y
   el veneno donde se quedó quieto el jugador).

   También: las maldiciones de las súcubos y de Baal (Defense Curse y Blood
   Mana), el capataz (Cry Help, Healing Vortex, Overseer Whip), la madre
   pútrida que preña, los diablillos que montan las bestias de asedio, los
   tiros de las catapultas que caen del cielo, los misiles de frío de Baal
   (el que va dejando estelas a los costados), el Grito de Madawc, Baal que
   baja caminando a su portal y la explosión de los esbirros suicidas. */

/* Lo que hacen en el cuadro de golpe (sigue a A4H_* de monai4.inc) */
enum { A5H_SPAWN = A4H_CURSE + 1, A5H_STOMP, A5H_CORPSE, A5H_TENTACLE, A5H_TAUNT, A5H_CRYHELP, A5H_HEAL, A5H_WHIP, A5H_IMPREGNATE,
       A5H_SHOW, A5H_CATAPULT, A5H_COLDMAKER, A5H_SHOUT, A5H_TELEPORT };

/* La burla de Baal (SKILLS_SrvDo028: "baal taunt control" donde está el
   jugador). El control no se ve ni se mueve: dura su Range (75 cuadros) y,
   desde el cuadro 25 (Param1), en cada uno elige al azar los rayos o el
   veneno; si el cuadro es múltiplo de su demora (Param2 3, Param3 45)
   suelta ese control, que dispara dos veces (al nacer y al acabarse su
   Range de 1: AlwaysExplode): dos rayos o la rueda de nubes de veneno.
   MISSMODE_SrvDo34 y SrvHit58/59. */
#define A5_TAUNT_MAX 4
static struct {
    float    x, y;
    int      age, life, lvl, src;
    int      quiet; /* hasta qué tick los rayos no dañan (abajo) */
    uint32_t rng;
} s_taunt[A5_TAUNT_MAX];

static uint32_t a5_taunt_rnd(int k)
{
    s_taunt[k].rng = s_taunt[k].rng * 1664525u + 1013904223u;
    return s_taunt[k].rng >> 8;
}

static void a5_taunt_add(Unit *u, float x, float y)
{
    for (int k = 0; k < A5_TAUNT_MAX; k++)
        if (s_taunt[k].life <= 0) {
            s_taunt[k].x = x;
            s_taunt[k].y = y;
            s_taunt[k].age = 0;
            s_taunt[k].lvl = ai_sklvl(u, 0);
            s_taunt[k].life = missiles_range("baal taunt control", s_taunt[k].lvl);
            s_taunt[k].src = (int)(u - s_units);
            s_taunt[k].quiet = 0;
            s_taunt[k].rng = rnd(u);
            return;
        }
}

/* Lo de un proyectil de un monstruo que no sale de él (la burla, las catapultas) */
static void a5_owner(Missile *mi, int src)
{
    const Unit *s = src >= 0 && src < s_num_units ? &s_units[src] : NULL;
    mi->owner = -1;
    mi->source = src;
    mi->ar = s ? s->ar : 100;
    mi->level = s ? s->level : 1;
    mi->dmin = mi->dmax = 0;
}

static void a5_taunt_owner(Missile *mi, int k)
{
    a5_owner(mi, s_taunt[k].src);
}

/* MISSMODE_CreatePoisonCloudHitSubmissiles: nubes de veneno que se abren
   desde (x, y), cada `main` de las 16 direcciones a Param1 y, con `sub`,
   cada `sub` de las de entre medio a Param2 (en medias unidades de Vel) */
static void a5_clouds(const char *name, float x, float y, int src, int lvl, int main, int sub)
{
    static const int k_dx[16] = { 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1 };
    static const int k_dy[16] = { 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2 };
    int emin, emax;
    missiles_level_damage(name, lvl, &emin, &emax);
    missiles_poison_total(name, &emin, &emax);
    for (int pass = 0; pass < 2; pass++) {
        int step = pass ? sub : main > 0 ? main : 1;
        for (int i = 0; step > 0 && i < (pass ? 15 : 16); i += step) {
            int j = pass ? i + 1 : i;
            Missile *c = missiles_spawn(name, x, y, 0.0f, 0.0f);
            if (!c)
                return;
            float l = sqrtf((float)(k_dx[j] * k_dx[j] + k_dy[j] * k_dy[j]));
            float sp = (float)(pass ? missiles_param2(name) : missiles_param1(name)) * 0.5f * missiles_vel_scale();
            a5_owner(c, src);
            c->flags = MF_CLOUD; /* (sin MF_EFFECT: se mueve) */
            c->vx = (float)k_dx[j] / l * sp;
            c->vy = (float)k_dy[j] / l * sp;
            c->elem = ELEM_POIS;
            c->emin = emin;
            c->emax = emax;
            c->elen = missiles_elen(name);
            c->radius10 = 12;
        }
    }
}

/* SrvHit58: un rayo hacia un punto al azar a ±sHitPar1 (10) subtiles. Sin
   ToHit en missiles.txt no falla. NextDelay 10: al que le pega un rayo no
   le pega otro en 10 cuadros; acá, los de una misma burla dañan de a uno
   cada 10 cuadros (salen todos de donde está el jugador). Sale medio paso
   atrás: el motor mira el choque después de moverlo y el original, con
   Size 2, le pega al que está donde nace. */
static void a5_taunt_bolt(int k)
{
    const char *name = "baal taunt lightning";
    float dx = (float)((int)(a5_taunt_rnd(k) % 21) - 10), dy = (float)((int)(a5_taunt_rnd(k) % 21) - 10);
    Missile *mi = missiles_spawn(name, s_taunt[k].x, s_taunt[k].y, dx == 0.0f && dy == 0.0f ? 1.0f : dx, dy);
    if (!mi)
        return;
    mi->x -= mi->vx * 0.5f;
    mi->y -= mi->vy * 0.5f;
    a5_taunt_owner(mi, k);
    mi->life = missiles_range(name, s_taunt[k].lvl);
    mi->elem = ELEM_LTNG;
    missiles_level_damage(name, s_taunt[k].lvl, &mi->emin, &mi->emax);
    mi->flags |= MF_ALWAYS_HIT;
    if ((int)s_unit_ticks < s_taunt[k].quiet)
        mi->flags |= MF_NOHIT;
    else
        s_taunt[k].quiet = (int)s_unit_ticks + 10;
}

/* SrvHit59 (con pasos 2 y 2): dieciséis nubes desde el control */
static void a5_taunt_clouds(int k)
{
    a5_clouds("baal taunt poison", s_taunt[k].x, s_taunt[k].y, s_taunt[k].src, s_taunt[k].lvl, 2, 2);
}

/* La burla empieza: el control y la risa de Baal (prgsound de skills.txt) */
static void a5_taunt_start(Unit *u, float x, float y)
{
    a5_taunt_add(u, x, y);
    audio_play_name("monster_baal_taunt_1", x, y);
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s se burla en (%.0f, %.0f) en el tick %u\n", u->name, x, y, s_unit_ticks);
}

/* sub_6FCBDFE0: al lugar libre más cerca del jugador, sin efecto (no se ve) */
static int a5_hop(Unit *u, const Map *m, const UnitTarget *tp)
{
    for (int k = 0; k < 9; k++) {
        float x = tp->x + (float)((k + 4) % 9 % 3 - 1), y = tp->y + (float)((k + 4) % 9 / 3 - 1);
        if (m && !wall_foot(m, x, y, u->foot, u->move_mask)) {
            u->x = u->prev_x = x;
            u->y = u->prev_y = y;
            grid_moved(u);
            return 1;
        }
    }
    return 0;
}

static void a5_taunt_tick(void)
{
    for (int k = 0; k < A5_TAUNT_MAX; k++) {
        if (s_taunt[k].life <= 0)
            continue;
        s_taunt[k].life--;
        int f = s_taunt[k].age++;
        if (f < 25)
            continue;
        int pois = (int)(a5_taunt_rnd(k) % 2);
        if (f % (pois ? 45 : 3) != 0)
            continue;
        if (pois) { /* (las dos ruedas saldrían iguales y el veneno no se suma: una) */
            a5_taunt_clouds(k);
        } else {
            a5_taunt_bolt(k);
            a5_taunt_bolt(k);
        }
    }
}

/* ---- Las catapultas (Fn123 y Fn126) ----
   El vigía pide el tiro para un lugar cerca del jugador: la bola nace ahí,
   en el aire, y cae 16 píxeles por cuadro (cltcalc1) los 25 cuadros de su
   Range; al tocar el piso hace lo de su pSrvHitFunc: 1 y 14 dañan a
   sHitPar1 (1) subtiles (MF_LAND), 38 suelta rayos cargados, 40 púas de
   fuego que quedan en el piso, 2 nubes de veneno y 14, además, fuego en el
   piso. Lo que pasa al caer va en s_fall, con el tick en que cae. */
enum { A5F_BOLTS = 1, A5F_SPIKES, A5F_SPIKE, A5F_CLOUDS, A5F_FIRES, A5F_ICE };
#define A5_FALL_MAX 128
static struct {
    int   at, kind, src, lvl;
    float x, y, dx, dy;
} s_fall[A5_FALL_MAX];

static void a5_fall_add(int at, int kind, int src, int lvl, float x, float y, float dx, float dy)
{
    for (int k = 0; k < A5_FALL_MAX; k++)
        if (!s_fall[k].kind) {
            s_fall[k].at = at;
            s_fall[k].kind = kind;
            s_fall[k].src = src;
            s_fall[k].lvl = lvl;
            s_fall[k].x = x;
            s_fall[k].y = y;
            s_fall[k].dx = dx;
            s_fall[k].dy = dy;
            return;
        }
}

/* El tiro k del vigía (SKILLS_SrvDo028: su srvmissilea nace en el lugar elegido) */
static void a5_catapult(Unit *u, int k, float x, float y)
{
    const char *name = skill_missile(u->skill[k]);
    int lvl = ai_sklvl(u, k), src = (int)(u - s_units);
    Missile *mi = name[0] ? missiles_spawn(name, x, y, 0.0f, 0.0f) : NULL;
    if (!mi)
        return;
    a5_owner(mi, src);
    mi->flags |= MF_EFFECT | MF_NOHIT;
    mi->life = missiles_range(name, lvl);
    mi->vz = 16.0f;
    mi->z = mi->vz * (float)mi->life;
    int at = (int)s_unit_ticks + mi->life, hit = missiles_hit_func(mi);
    if (hit == 1 || hit == 14) { /* el daño del proyectil alrededor (el frío dura su ELen) */
        mi->flags |= MF_LAND;
        mi->radius10 = 10;
        missiles_level_damage(name, lvl, &mi->emin, &mi->emax);
        mi->elem = hit == 1 ? ELEM_COLD : ELEM_FIRE;
        mi->elen = hit == 1 ? missiles_elen(name) : 0;
    }
    int kind = hit == 38 ? A5F_BOLTS : hit == 40 ? A5F_SPIKES : hit == 2 ? A5F_CLOUDS : hit == 14 ? A5F_FIRES : A5F_ICE;
    a5_fall_add(at, kind, src, lvl, x, y, 0.0f, 0.0f);
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s pide %s en (%.0f, %.0f)\n", u->name, name, x, y);
}

/* SrvHit38 (sub_6FD14260): los rayos cargados, hacia afuera con las
   direcciones del original (4 + 2 por nivel pedidos: con 4 salen 6) */
static void a5_fall_bolts(int src, int lvl, float x, float y)
{
    static const int k_sx[4] = { -1, 1, 1, -1 }, k_sy[4] = { -1, -1, 1, 1 };
    static const int k_o1[7] = { 18, 20, 17, 20, 15, 19, 18 }, k_o2[7] = { 8, 2, 11, 4, 13, 6, 9 };
    static const int k_xo[8] = { 20, -20, 0, 0, 14, -14, -14, 14 }, k_yo[8] = { 0, 0, 20, -20, 14, 14, -14, -14 };
    int dir[40][2], n = 0, left = 4 + 2 * (lvl - 1) - 1;
    dir[n][0] = 14, dir[n][1] = -14, n++;
    for (int j = 0; j < 7 && left > 0 && n < 37; j++) {
        for (int i = 0; i < 4 && left > 0 && n < 37; i++) {
            dir[n][0] = k_o1[j] * k_sx[i], dir[n][1] = k_o2[j] * k_sy[i], n++;
            dir[n][0] = k_o2[j] * k_sx[i], dir[n][1] = k_o1[j] * k_sy[i], n++;
            left -= 2;
        }
        dir[n][0] = k_xo[j], dir[n][1] = k_yo[j], n++;
        left--;
    }
    const char *name = "catapultchargedballbolt";
    for (int i = 0; i < n; i++) {
        Missile *b = missiles_spawn(name, x, y, (float)dir[i][0], (float)dir[i][1]);
        if (!b)
            return;
        a5_owner(b, src);
        b->elem = ELEM_LTNG;
        missiles_level_damage(name, lvl, &b->emin, &b->emax);
        b->flags |= MF_CHARGED | MF_ALWAYS_HIT;
    }
}

/* SrvHit40 (sub_6FCF8330): ln12 púas (20 + 5 por nivel) saltan a un lugar
   a ±n/4 (no a menos de 2) y quedan en el piso (SrvHit36) */
static void a5_fall_spikes(int src, int lvl, float x, float y)
{
    Unit *su = &s_units[src];
    int n = 20 + 5 * (lvl - 1), r = n / 4;
    missiles_effect("fireexplode", x, y);
    for (int i = 0; i < n; i++) {
        float dx = (float)((int)(rnd(su) % (uint32_t)(2 * r)) - r);
        float dy = (float)((int)(rnd(su) % (uint32_t)(2 * r)) - r);
        if (dx * dx + dy * dy < 4.0f)
            continue;
        Missile *s = missiles_spawn("catapult spike in air", x, y, dx, dy);
        if (!s)
            return;
        a5_owner(s, src);
        s->flags |= MF_NOHIT;
        float sp = missiles_speed(s), d = sqrtf(dx * dx + dy * dy);
        s->life = sp > 0.0f ? (int)ceilf(d / sp) : 1;
        a5_fall_add((int)s_unit_ticks + s->life, A5F_SPIKE, src, lvl, x + dx, y + dy, dx, dy);
    }
}

/* La púa en el piso: le pega al primero que la pisa y explota (CollideKill,
   ExplosionMissile); dura su Range */
static void a5_fall_spike(int src, int lvl, float x, float y, float dx, float dy)
{
    const char *name = "catapult spike on ground";
    Missile *g = missiles_spawn(name, x, y, 0.0f, 0.0f);
    if (!g)
        return;
    float l = sqrtf(dx * dx + dy * dy);
    a5_owner(g, src);
    g->flags &= ~MF_EFFECT; /* (quieta pero le pega al que la toca) */
    g->vx = l > 0.0f ? dx / l * 0.0001f : 0.0f; /* (solo para que mire hacia donde saltó) */
    g->vy = l > 0.0f ? dy / l * 0.0001f : 0.0f;
    g->elem = ELEM_FIRE;
    missiles_level_damage(name, lvl, &g->emin, &g->emax);
    g->flags |= MF_ALWAYS_HIT;
}

/* SrvHit14 (MISSMODE_CreateMeteor_MoltenBoulderSubmissiles, de a 1): 18
   fuegos alrededor que duran Param3 + Param4 por nivel (50 + 3) y queman
   lo de catapult meteor fire (por cuadro en 1/256 << 4) */
static void a5_fall_fires(int src, int lvl, float x, float y)
{
    static const int k_ox[18] = { 2, -2, 0, 0, -3, 0, 3, -1, 1, -1, 2, -4, -3, -1, 0, 1, 3, 4 };
    static const int k_oy[18] = { -2, -2, 2, 5, 3, 3, 3, 2, 1, -1, -1, -2, -2, -3, -4, -3, -3, -2 };
    const char *name = "catapult meteor fire";
    int lo, hi;
    missiles_level_damage(name, lvl, &lo, &hi);
    missiles_effect("moltenboulder-flyingrocks", x, y);
    for (int i = 0; i < 18; i++) {
        Missile *f = missiles_effect(name, x + (float)k_ox[i], y + (float)k_oy[i]);
        if (!f)
            return;
        f->flags |= MF_BURN;
        f->owner = -1;
        f->source = src;
        f->emin = (lo << 4) * TICK_RATE / 256 + 1; /* (el fuego del piso es por segundo) */
        f->emax = (hi << 4) * TICK_RATE / 256 + 1;
        f->radius10 = 10;
        f->life = 50 + 3 * (lvl - 1);
    }
}

static void a5_fall_tick(void)
{
    for (int k = 0; k < A5_FALL_MAX; k++) {
        if (!s_fall[k].kind || (int)s_unit_ticks < s_fall[k].at)
            continue;
        int kind = s_fall[k].kind, src = s_fall[k].src, lvl = s_fall[k].lvl;
        float x = s_fall[k].x, y = s_fall[k].y, dx = s_fall[k].dx, dy = s_fall[k].dy;
        s_fall[k].kind = 0;
        switch (kind) {
        case A5F_BOLTS:
            a5_fall_bolts(src, lvl, x, y);
            break;
        case A5F_SPIKES:
            a5_fall_spikes(src, lvl, x, y);
            break;
        case A5F_SPIKE:
            a5_fall_spike(src, lvl, x, y, dx, dy);
            break;
        case A5F_CLOUDS: /* SrvHit02 con sHitPar 2 y 2 */
            a5_clouds("catapult plague cloud", x, y, src, lvl, 2, 2);
            break;
        case A5F_FIRES:
            a5_fall_fires(src, lvl, x, y);
            break;
        case A5F_ICE:
            missiles_effect("catapult cold explosion", x, y);
            break;
        }
    }
}

/* ---- Los misiles de frío de Baal (SrvDo139 y MISSMODE_SrvDo31) ----
   "baal cold maker" va hacia el jugador y en cada subtile nuevo que pisa
   suelta dos "baal cold trail" a los costados (perpendiculares a su
   camino). Los dos pasan a través (sin CollideKill) y hacen el daño de la
   habilidad (frío, ELen 200 + 50 por nivel). */
#define A5_MAKER_MAX 8
static struct {
    const Missile *mi;
    int slot, age, cx, cy, src, emin, emax, elen;
    float px, py; /* hacia un costado */
} s_maker[A5_MAKER_MAX];

static void a5_cold_set(Missile *mi, int src, int emin, int emax, int elen)
{
    a5_owner(mi, src);
    mi->elem = ELEM_COLD;
    mi->emin = emin;
    mi->emax = emax;
    mi->elen = elen;
    mi->flags |= MF_ALWAYS_HIT;
}

static void a5_cold_maker(Unit *u, const UnitTarget *t, int k)
{
    float dx = t->x - u->x, dy = t->y - u->y;
    Missile *mi = missiles_spawn("baal cold maker", u->x, u->y, dx, dy);
    if (!mi)
        return;
    int lvl = ai_sklvl(u, k), lo, hi;
    skill_elem_damage(u->skill[k], lvl, &lo, &hi);
    a5_cold_set(mi, (int)(u - s_units), lo, hi, 200 + 50 * (lvl - 1));
    for (int j = 0; j < A5_MAKER_MAX; j++)
        if (!s_maker[j].mi) {
            for (int i = 0; i < missiles_capacity(); i++)
                if (missiles_get(i) == mi)
                    s_maker[j].slot = i;
            s_maker[j].mi = mi;
            s_maker[j].age = mi->age;
            s_maker[j].cx = (int)floorf(mi->x);
            s_maker[j].cy = (int)floorf(mi->y);
            s_maker[j].src = mi->source;
            s_maker[j].emin = lo;
            s_maker[j].emax = hi;
            s_maker[j].elen = mi->elen;
            s_maker[j].px = -dy;
            s_maker[j].py = dx;
            return;
        }
}

static void a5_maker_tick(void)
{
    for (int j = 0; j < A5_MAKER_MAX; j++) {
        const Missile *mi = s_maker[j].mi;
        if (!mi)
            continue;
        if (missiles_get(s_maker[j].slot) != mi || !missiles_is(mi, "baal cold maker") || mi->age < s_maker[j].age) {
            s_maker[j].mi = NULL; /* (ya terminó) */
            continue;
        }
        s_maker[j].age = mi->age;
        int cx = (int)floorf(mi->x), cy = (int)floorf(mi->y);
        if (cx == s_maker[j].cx && cy == s_maker[j].cy)
            continue;
        s_maker[j].cx = cx;
        s_maker[j].cy = cy;
        for (int side = -1; side <= 1; side += 2) {
            Missile *tr = missiles_spawn("baal cold trail", mi->x, mi->y, s_maker[j].px * (float)side, s_maker[j].py * (float)side);
            if (tr)
                a5_cold_set(tr, s_maker[j].src, s_maker[j].emin, s_maker[j].emax, s_maker[j].elen);
        }
    }
}

/* El modo de la habilidad k: el suyo o el primero de su secuencia */
static int a5_anim(const Unit *u, int k)
{
    if (u->sk_anim[k] >= 0)
        return u->sk_anim[k];
    return u->seqs[k] ? u->seqs[k]->anim[0] : -1;
}

/* a4_use con el modo de la secuencia si no tiene uno propio */
static int a5_use(Unit *u, int t, int k, int what)
{
    int anim = a5_anim(u, k);
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

/* La habilidad k según lo que es: maldición, aliento o proyectil (0 si no
   la sabe hacer el motor) */
static int a5_skill(Unit *u, int t, int k, int shots, int ring)
{
    const char *sk = u->skill[k];
    if (!sk[0] || t < 0)
        return 0;
    static const struct { const char *skill; int curse; } k_curses[] = {
        { "Amplify Damage", A4C_AMPLIFY }, { "Weaken", A4C_WEAKEN }, { "Decrepify", A4C_DECREPIFY },
        { "Defense Curse", A4C_DEFENSE }, { "Blood Mana", A4C_BLOODMANA },
    };
    for (int c = 0; c < (int)(sizeof k_curses / sizeof k_curses[0]); c++)
        if (strcmp(sk, k_curses[c].skill) == 0) {
            u->a4_curse = (int8_t)k_curses[c].curse;
            return a5_use(u, t, k, A4H_CURSE);
        }
    if (strcmp(sk, "Horror Arctic Blast") == 0 || strcmp(sk, "Baal Inferno") == 0 || strcmp(sk, "Imp Inferno") == 0)
        return a5_use(u, t, k, A4H_INFERNO);
    return ai_cast(u, t, k, shots, ring, 0);
}

/* El fuego que le llega a un jugador después de su resistencia */
static int a5_fire(const UnitTarget *t, int dmg)
{
    int r = t->res[0] > 75 ? 75 : t->res[0];
    return dmg * (100 - r) / 100;
}

/* Solo la animación (la catapulta que dispara, el diablillo montado): en
   su cuadro de golpe no pasa nada */
static void a5_show(Unit *u, int t, int anim)
{
    ai_attack(u, t, anim);
    u->ranged_attack = 0;
    u->a4_hit = A5H_SHOW;
}

/* La k cuyo nombre es ese (-1 = no la tiene) */
static int a5_find(const Unit *u, const char *skill)
{
    for (int k = 0; k < MON_SKILLS; k++)
        if (strcmp(u->skill[k], skill) == 0)
            return k;
    return -1;
}

/* aip de otro monstruo (los diablillos leen los de imp1..imp4) */
static int a5_aip(const char *id, int k)
{
    char col[8];
    snprintf(col, sizeof col, "aip%d", k + 1);
    int ms = mon_row(&s_monstats, id);
    return ms >= 0 ? txt_get_int(&s_monstats, ms, col, 0) : 0;
}

/* Los vivos de una manada (BaseId) a menos de r subtiles */
static int a5_count(const Unit *u, const char *base, float r)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        n += o != u && o->enemy && o->state < ST_DYING && !o->hidden && strcmp(o->base, base) == 0 && dist_to(u, o->x, o->y) < r;
    }
    return n;
}

/* Una cría (o lo que sea) al lado, del bando enemigo, sin experiencia ni botín */
static Unit *a5_spawn(Unit *u, const char *id, float x, float y)
{
    if (!id[0] || (s_map && wall_foot(s_map, x, y, 1, COLL_MASK_MONSTER_PATH)))
        return NULL;
    int saved = s_spawn_zone;
    s_spawn_zone = u->zone;
    Unit *c = create_monster(id, x, y, u->level, 1);
    s_spawn_zone = saved;
    if (c) {
        c->exp = 0;
        c->tc[0] = '\0';
        c->sector = u->sector;
    }
    return c;
}

/* ---- El capataz (Fn120) y sus esbirros ---- */

/* El daño físico de una habilidad en su nivel (MinDam, MaxDam y lo que
   suben, en 1/256 << HitShift) */
static void a5_skill_phys(const char *skill, int lvl, int *lo, int *hi)
{
    if (!s_skilltab.rows)
        load_table(&s_skilltab, "skills");
    *lo = *hi = 0;
    int row = s_skilltab.rows ? txt_find_row(&s_skilltab, txt_col(&s_skilltab, "skill"), skill) : -1;
    if (row < 0)
        return;
    static const int k_from[5] = { 2, 9, 17, 23, 29 }, k_to[5] = { 8, 16, 22, 28, 99 };
    int mn = txt_get_int(&s_skilltab, row, "MinDam", 0), mx = txt_get_int(&s_skilltab, row, "MaxDam", 0);
    for (int k = 0; k < 5; k++) {
        int n = (lvl < k_to[k] ? lvl : k_to[k]) - k_from[k] + 1;
        if (n <= 0)
            break;
        char col[16];
        snprintf(col, sizeof col, "MinLevDam%d", k + 1);
        mn += n * txt_get_int(&s_skilltab, row, col, 0);
        snprintf(col, sizeof col, "MaxLevDam%d", k + 1);
        mx += n * txt_get_int(&s_skilltab, row, col, 0);
    }
    int shift = txt_get_int(&s_skilltab, row, "HitShift", 8);
    *lo = (mn << shift) >> 8;
    *hi = (mx << shift) >> 8 > *lo ? (mx << shift) >> 8 : *lo;
}

/* El zarpazo bajo tierra del Death Mauler (SKILLS_SrvDo136_DeathMaul), en el cuadro de evento de su secuencia: las
   garras (death mauler) salen donde está el jugador. El original les deja Range * d / 12 cuadros (d >= 10), la mitad
   más Range * d / 24 (d < 10) o la mitad y un poco (al lado) y pegan en los últimos Range - Activate: más lejos, más
   demora. Lo que tarda en llegar, la tierra se va levantando de él hasta ahí (el rastro, death mauler trail: lo que
   dibuja el cliente; uno cada Param2 cuadros, y al terminar su polvo, CltHitSubMissile1). */
static void a5_death_maul(Unit *u, const UnitTarget *t)
{
    Missile *mi = missiles_spawn("death mauler", t->x, t->y, t->x - u->x, t->y - u->y);
    if (!mi)
        return;
    int dx = abs((int)floorf(t->x) - (int)floorf(u->x)), dy = abs((int)floorf(t->y) - (int)floorf(u->y));
    int d = dx <= dy ? (dx + 2 * dy) / 2 : (dy + 2 * dx) / 2; /* (UNITS_GetDistanceToCoordinates) */
    int total = mi->life, frames = d < 1 ? total / 2 + total / 24 : d >= 10 ? total * d / 12 : total / 2 + total * d / 24;
    missiles_set_frames(mi, frames);
    mi->flags &= ~MF_EFFECT; /* (no se mueve, pero pega) */
    mi->face_x = t->x - u->x;
    mi->face_y = t->y - u->y;
    mi->owner = -1;
    mi->source = (int)(u - s_units);
    mi->ar = u->ar;
    mi->level = u->level;
    a5_skill_phys("DeathMaul", ai_sklvl(u, u->seq_skill), &mi->dmin, &mi->dmax);
    int every = missiles_param("death mauler trail", 2);
    every = every > 0 ? every : 4;
    for (int k = every; k < mi->wake; k += every) {
        float f = (float)k / (float)mi->wake;
        Missile *p = missiles_effect("death mauler trail", u->x + (t->x - u->x) * f, u->y + (t->y - u->y) * f);
        if (!p)
            break;
        p->face_x = t->x - u->x;
        p->face_y = t->y - u->y;
        p->wake = k;
        p->life = k + 8; /* (sus 31 cuadros, de a 4 por tick: animrate 4096) */
        p->flags |= MF_NOHIT | MF_CHAIN;
    }
}

/* Los esbirros (BaseId minion1) vivos a menos de r del capataz */
static int a5_is_minion(const Unit *u, const Unit *o, float r)
{
    return o != u && o->enemy && o->state < ST_DYING && !o->hidden && strcmp(o->base, "minion1") == 0 && dist_to(u, o->x, o->y) < r;
}

/* Healing Vortex (SrvHit43): el proyectil va derecho y cura al primero de
   los suyos que toca (CollideFriend, CollideKill) lo del daño de la
   habilidad en su nivel; al que estaba herido le queda el destello
   (ProgOverlay healing) */
#define A5_HEAL_MAX 8
static struct {
    const Missile *mi;
    int slot, age, src, lo, hi;
} s_heal[A5_HEAL_MAX];

static void a5_heal_start(Unit *u, const Unit *o, int lvl)
{
    const char *name = "healing vortex";
    Missile *mi = missiles_spawn(name, u->x, u->y, o->x - u->x, o->y - u->y);
    if (!mi)
        return;
    mi->owner = -1;
    mi->source = (int)(u - s_units);
    mi->flags |= MF_NOHIT; /* (no les pega a los jugadores: cura a los suyos, acá abajo) */
    mi->life = missiles_range(name, lvl);
    for (int k = 0; k < A5_HEAL_MAX; k++)
        if (!s_heal[k].mi) {
            for (int i = 0; i < missiles_capacity(); i++)
                if (missiles_get(i) == mi)
                    s_heal[k].slot = i;
            s_heal[k].mi = mi;
            s_heal[k].age = mi->age;
            s_heal[k].src = mi->source;
            a5_skill_phys("Healing Vortex", lvl, &s_heal[k].lo, &s_heal[k].hi);
            return;
        }
}

static void a5_heal_tick(void)
{
    for (int k = 0; k < A5_HEAL_MAX; k++) {
        const Missile *mi = s_heal[k].mi;
        if (!mi)
            continue;
        if (missiles_get(s_heal[k].slot) != mi || !missiles_is(mi, "healing vortex") || mi->age < s_heal[k].age) {
            s_heal[k].mi = NULL; /* (ya terminó) */
            continue;
        }
        s_heal[k].age = mi->age;
        for (int i = 0; i < s_num_units; i++) {
            Unit *o = &s_units[i];
            if (i == s_heal[k].src || !o->enemy || o->state >= ST_DYING || o->hidden || o->zone == ZONE_FREE ||
                (o->x - mi->x) * (o->x - mi->x) + (o->y - mi->y) * (o->y - mi->y) > (o->radius + 0.5f) * (o->radius + 0.5f))
                continue;
            if (o->hp < o->max_hp)
                overlays_add("healing", OV_AT_UNIT, i, o->x, o->y, 0);
            int h = s_heal[k].lo + (int)(rnd(o) % (uint32_t)(s_heal[k].hi - s_heal[k].lo + 1));
            o->hp = o->hp + h < o->max_hp ? o->hp + h : o->max_hp;
            missiles_end(s_heal[k].slot);
            s_heal[k].mi = NULL;
            break;
        }
    }
}

/* Overseer Whip (SrvDo131) a un esbirro: si no tiene ya la sed de sangre,
   un 35 % de las veces (Param1: 65) se vuelve esbirro suicida (el de su
   mismo lugar en la cadena: minion3, suicideminion3) con su S1; si no, la
   sed de sangre (bloodlust) Param2 cuadros: +80 % de velocidad y de ataque,
   +50 % de defensa y +100 % de daño */
static void a5_whip(Unit *u, int i)
{
    Unit *o = &s_units[i];
    if (!o->enemy || o->state >= ST_DYING || o->hidden)
        return;
    if ((int)(rnd(u) % 100) >= 65 && o->lust_ticks <= 0 && strncmp(o->name, "minion", 6) == 0) {
        char id[32];
        snprintf(id, sizeof id, "suicide%s", o->name);
        int saved = s_spawn_zone;
        s_spawn_zone = o->zone;
        Unit *c = mon_row(&s_monstats, id) >= 0 ? create_monster(id, o->x, o->y, o->level, 1) : NULL;
        s_spawn_zone = saved;
        if (c) {
            c->sector = o->sector;
            set_state(c, gfx_has(c->gfx, ANIM_SKILL) ? ST_RISE : ST_CHASE);
            o->hidden = 1; /* el esbirro ya no está: es el suicida */
            o->solid = 0;
            o->hp = 0;
            set_state(o, ST_DEAD);
            if (getenv("D2_AI_DEBUG"))
                printf("IA: %s vuelve suicida a %s\n", u->name, o->name);
            return;
        }
    }
    units_enrage(i, 100, 80, 250);
    o->lust_ticks = 250;
    overlays_add("bloodlust_state", OV_AT_UNIT, i, o->x, o->y, 250);
}

/* Cry Help (SrvDo128): los esbirros del grupo atacan al que le pegó ln12
   cuadros (100 + 20 por nivel), sin quedarse quietos (la orden 1 de
   Fn116_Minion); el que le pegó lleva el destello curseattract */
static void a5_cry_help(Unit *u, int target, const UnitTarget *t, int lvl)
{
    int until = (int)s_unit_ticks + 100 + 20 * (lvl - 1);
    for (int i = 0; i < s_num_units; i++)
        if (a5_is_minion(u, &s_units[i], 24.0f)) {
            s_units[i].ai_p[0] = until;
            s_units[i].ai_p[1] = target + 1;
        }
    if (t->player)
        overlays_add("curseattract", OV_AT_PLAYER, target, t->x, t->y, 0);
}

/* suicideminion_explode (MONSTERUNIQUE_CastSuicideExplodeMissile, el
   modificador que llevan todos los esbirros suicidas): al morir, su daño
   de A1 a los que están a aip4 o menos, físico y además del elemento que
   dice aip1 (1 fuego; 2 o más frío, que enfría esos cuadros; 0 nada) */
static void a5_suicide_boom(Unit *u, UnitTarget *pl, int n)
{
    if (u->ai_p[2]) /* (una vez) */
        return;
    u->ai_p[2] = 1;
    int dmg = u->dmg_min + (int)(rnd(u) % (uint32_t)(u->dmg_max - u->dmg_min + 1 > 0 ? u->dmg_max - u->dmg_min + 1 : 1));
    int kind = u->aip[0];
    float r = (float)u->aip[3];
    missiles_effect(kind == 1 ? "suicidefireexplode" : kind < 2 ? "suicidecorpseexplode" : "suicideiceexplode", u->x, u->y);
    for (int k = 0; k < n; k++) {
        UnitTarget *t = &pl[k];
        if (!t->alive || t->in_town || dist_to(u, t->x, t->y) > r + t->body)
            continue;
        int e = 0;
        if (kind == 1) {
            e = a5_fire(t, dmg);
        } else if (kind >= 2) {
            int res = t->res[1] > 75 ? 75 : t->res[1];
            e = dmg * (100 - res) / 100;
            if (kind > t->chill_ticks)
                t->chill_ticks = kind;
        }
        t->damage += dmg + e;
    }
}

/* El preso que va a su portal (Fn131_Wussie): 70 % camina hacia él, si no
   un 10 % da una vuelta a 4 subtiles y si no espera 10 cuadros (lo saca
   a5quest.inc al llegar) */
static void a5_goal_tick(Unit *u, const Map *m)
{
    if (u->ai_wait > 0) {
        u->ai_wait--;
        u->walking = 0;
        return;
    }
    if (u->ai_move > 0) {
        u->ai_move--;
        move_towards(u, m, u->ai_tx, u->ai_ty);
        if (!u->walking || dist_to(u, u->ai_tx, u->ai_ty) < 0.4f)
            u->ai_move = 0;
        return;
    }
    u->walking = 0;
    if (rnd(u) % 100 < 70)
        ai_go(u, u->goal_x, u->goal_y, 25, 100, 0);
    else if (rnd(u) % 100 < 10)
        ai_wander(u, 4, 100);
    else
        ai_stall(u, 10);
}

/* ---- La madre pútrida (Fn137) ---- */

/* A quién puede preñar (sub_6FD01910): un monstruo vivo de los enemigos que
   no sea otra madre ni un gusano ni esté ya preñado */
static int a5_can_impregnate(const Unit *o)
{
    return o->enemy && o->killable && o->state < ST_DYING && !o->hidden && o->zone != ZONE_FREE && !o->pregnant &&
           strcmp(o->base, "putriddefiler1") != 0 && strcmp(o->base, "painworm1") != 0;
}

/* El preñado murió (sub_6FD01B00): sale un gusano del dolor del nivel del lugar, en NU (summode) */
static void a5_birth(Unit *u)
{
    char id[24];
    int i = (int)(u - s_units);
    u->pregnant = 0;
    overlays_remove("impregnated", OV_AT_UNIT, i);
    if (monster_variant("painworm", u->level, id, sizeof id) != 0)
        return;
    int saved = s_spawn_zone;
    s_spawn_zone = u->zone;
    Unit *c = create_monster(id, u->x, u->y, u->level, 1);
    s_spawn_zone = saved;
    if (c) {
        c->sector = s_units[i].sector;
        if (getenv("D2_AI_DEBUG"))
            printf("IA: de %s sale %s\n", s_units[i].name, id);
    }
}

/* Lo del Acto 5 que sigue solo en cada tick (la burla de Baal, los proyectiles que curan) */
static void a5_tick(void)
{
    a5_taunt_tick();
    a5_heal_tick();
    a5_fall_tick();
    a5_maker_tick();
}

static void a5_reset(void)
{
    memset(s_taunt, 0, sizeof s_taunt);
    memset(s_heal, 0, sizeof s_heal);
    memset(s_fall, 0, sizeof s_fall);
    memset(s_maker, 0, sizeof s_maker);
}

/* Los Antiguos solo pelean con el rito empezado (ACT5Q5_IsActivatable: lo
   pone a5quest.inc) */
static int s_a5_ancients;
void units_set_ancients(int on)
{
    s_a5_ancients = on;
}

/* Baal se fue a la Cámara (BaalToStairs llegó al portal): lo lee a5quest.inc */
static int s_a5_baal_left;
int units_baal_left(void)
{
    int r = s_a5_baal_left;
    s_a5_baal_left = 0;
    return r;
}

/* En el cuadro de golpe: lo de la habilidad del Acto 5 en curso (lo llama a4_hit_frame) */
static void a5_hit_frame(Unit *u, UnitTarget *pl)
{
    UnitTarget *t = u->target >= 0 ? &pl[u->target] : NULL;
    switch (u->a4_hit) {
    case A5H_SPAWN: /* MinionSpawner y Nest: una cría al lado */
        a5_spawn(u, u->minion, u->a4_x, u->a4_y);
        break;
    case A5H_TELEPORT: /* el diablillo aparece en el lugar elegido (Imp Teleport, en su S1) */
        teleport_move(u, u->a4_x, u->a4_y);
        break;
    case A5H_STOMP: /* el pisotón (SrvDo134): su daño de A2 a todos a menos de 4 subtiles */
        for (int k = 0; k < MAX_PLAYERS; k++)
            if (pl[k].alive && !pl[k].in_town && dist_to(u, pl[k].x, pl[k].y) < 4.0f + pl[k].body)
                pl[k].damage += u->a2_min + (int)(rnd(u) % (uint32_t)(u->a2_max - u->a2_min + 1 > 0 ? u->a2_max - u->a2_min + 1 : 1));
        break;
    case A5H_CORPSE: { /* la explosión de cadáver (SrvDo055): 60 a 100 % de su vida a los de alrededor */
        Unit *c = u->a4_corpse >= 0 && u->a4_corpse < s_num_units ? &s_units[u->a4_corpse] : NULL;
        if (c && c->state == ST_DEAD && !c->hidden) {
            int dmg = c->max_hp * (60 + (int)(rnd(u) % 41)) / 100;
            for (int k = 0; k < MAX_PLAYERS; k++)
                if (pl[k].alive && !pl[k].in_town && (pl[k].x - c->x) * (pl[k].x - c->x) + (pl[k].y - c->y) * (pl[k].y - c->y) < 9.0f)
                    pl[k].damage += dmg / 2 + a5_fire(&pl[k], dmg / 2);
            missiles_effect("corpseexplosion", c->x, c->y);
            c->hidden = 1;
        }
        u->a4_corpse = -1;
        break;
    }
    case A5H_TENTACLE: /* los tentáculos de Baal (SrvDo140): cinco alrededor del jugador */
        if (t) {
            static const float k_ring[5][2] = { { 0, -3 }, { 2.8f, -1 }, { 1.8f, 2.4f }, { -1.8f, 2.4f }, { -2.8f, -1 } };
            int self = (int)(u - s_units);
            for (int i = 0; i < 5; i++) {
                Unit *c = a5_spawn(u, u->minion[0] ? u->minion : "baaltentacle1", t->x + k_ring[i][0], t->y + k_ring[i][1]);
                if (c)
                    c->master = self;
            }
        }
        break;
    case A5H_TAUNT: /* la burla de Baal (SrvDo028): donde está el jugador */
        if (t)
            a5_taunt_start(u, t->x, t->y);
        break;
    case A5H_SHOUT: { /* el Grito (SrvDo068): él y los suyos a 15 subtiles, Param1 + Param2 por nivel % de
                         defensa por Param3 + Param4 por nivel cuadros */
        int lvl = ai_sklvl(u, u->a4_k), pct = 100 + 10 * (lvl - 1), len = 750 + 250 * (lvl - 1);
        for (int i = 0; i < s_num_units; i++) {
            Unit *o = &s_units[i];
            if (!o->enemy || o->state >= ST_DYING || o->hidden || dist_to(u, o->x, o->y) > 15.0f)
                continue;
            o->shout_pct = pct > o->shout_pct || o->shout_ticks <= 0 ? pct : o->shout_pct;
            o->shout_ticks = len;
            overlays_add("shoutstart", OV_AT_UNIT, i, o->x, o->y, 0);
            overlays_add("shout", OV_AT_UNIT, i, o->x, o->y, len);
        }
        missiles_effect("shout", u->x, u->y);
        break;
    }
    case A5H_COLDMAKER:
        if (t)
            a5_cold_maker(u, t, u->a4_k);
        break;
    case A5H_CATAPULT: /* el tiro del vigía, en el lugar que eligió */
        a5_catapult(u, u->a4_k, u->a4_x, u->a4_y);
        break;
    case A5H_CRYHELP:
        if (t)
            a5_cry_help(u, u->target, t, ai_sklvl(u, u->a4_k));
        break;
    case A5H_HEAL: /* (a4_corpse: el monstruo que eligió) */
    case A5H_WHIP:
    case A5H_IMPREGNATE:
        if (u->a4_corpse >= 0 && u->a4_corpse < s_num_units) {
            Unit *o = &s_units[u->a4_corpse];
            if (u->a4_hit == A5H_HEAL) {
                a5_heal_start(u, o, ai_sklvl(u, u->a4_k));
            } else if (u->a4_hit == A5H_WHIP) {
                a5_whip(u, u->a4_corpse);
            } else if (a5_can_impregnate(o)) { /* Impregnate (SrvDo133): el estado pregnant hasta que muera */
                o->pregnant = 1;
                overlays_add("impregnated", OV_AT_UNIT, u->a4_corpse, o->x, o->y, 1 << 24);
            }
        }
        u->a4_corpse = -1;
        break;
    }
    (void)t;
}

/* La horda reanimada se levanta sola una vez (Self-resurrect: SrvSt61), a
   los 3 segundos de caer, si su cadáver sigue ahí */
static void a5_reanimate(Unit *u)
{
    if (u->ai_p[1] || u->hidden || u->searched == 2)
        return;
    if (++u->ai_p[2] < 3 * TICK_RATE)
        return;
    u->ai_p[1] = 1;
    u->hp = u->max_hp;
    u->solid = 1;
    u->poison_ticks = u->chill_ticks = 0;
    set_state(u, u->gfx->anim[ANIM_SKILL].ok ? ST_RISE : ST_IDLE);
}

/* ---- Los diablillos montados (Imp Teleport, SKILLS_SrvDo129) ----
   La bestia de asedio sin jinete (Fn115) llama al diablillo libre más cerca
   a menos de aip1 (la torre de la barricada, a 20: Fn113). El diablillo
   camina hasta estar a imp2 aip1 de ella y se teletransporta encima: no se
   lo puede atacar y va con ella (PATH_UpdateRiderPath). Montado
   (SpecialState16) usa una de dos habilidades elegida al subir (Skill3 o
   Skill5); si el jugador se va a más de 24 o la bestia muere, se
   teletransporta a un lugar a imp1 aip2 y sigue a pie (con menos del 10 %
   de vida, muere). */

/* El diablillo libre (sin bestia elegida ni montado) más cerca de la bestia, a menos de r */
static void a5_call_imp(Unit *b, float r)
{
    int best = -1, self = (int)(b - s_units);
    float bd = r;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (!o->enemy || o->state >= ST_DYING || o->hidden || o->ride || o->ai_p[0] || strcmp(o->base, "imp1") != 0)
            continue;
        float d = dist_to(b, o->x, o->y);
        if (d <= bd) {
            bd = d;
            best = i;
        }
    }
    if (best >= 0)
        s_units[best].ai_p[0] = self + 1;
}

static void a5_mount(Unit *u, Unit *b)
{
    int skills[2] = { 2, 4 }, k = (int)(rnd(u) & 1);
    missiles_effect("teleport", u->x, u->y);
    u->x = u->prev_x = b->x;
    u->y = u->prev_y = b->y;
    grid_moved(u);
    u->ride = (int)(b - s_units) + 1;
    b->rider = (int)(u - s_units) + 1;
    u->killable = 0; /* (sin ISVALIDTARGET ni CANBEATTACKED) */
    u->solid = 0;
    u->ai_move = 0;
    u->ai_p[1] = u->skill[skills[k]][0] ? skills[k] : u->skill[skills[1 - k]][0] ? skills[1 - k] : 0;
    missiles_effect("teleport", u->x, u->y);
    ai_stall(u, 6);
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s monta a %s (usará %s)\n", u->name, b->name, u->ai_p[1] ? u->skill[u->ai_p[1]] : "nada");
}

/* Se baja: lejos de la bestia (sub_6FD01010); con menos del 10 % de vida, muere */
static void a5_dismount(Unit *u, const Map *m)
{
    int self = (int)(u - s_units);
    if (u->ride && s_units[u->ride - 1].rider == self + 1)
        s_units[u->ride - 1].rider = 0;
    u->ride = 0;
    u->ai_p[0] = u->ai_p[1] = 0;
    u->killable = 1;
    u->solid = 1;
    float r = (float)a5_aip("imp1", 1);
    float tx = u->x + (rndf(u) * 2.0f - 1.0f) * r, ty = u->y + (rndf(u) * 2.0f - 1.0f) * r; /* (de a uno: el orden) */
    ai_teleport(u, m, tx, ty);
    if (u->max_hp > 0 && u->hp * 10 < u->max_hp) {
        u->hp = 0;
        units_play_death(self);
    }
}

/* Después del tick: los montados en el lugar de su bestia */
static void a5_riders(void)
{
    for (int i = 0; i < s_num_units; i++) {
        Unit *u = &s_units[i];
        if (!u->ride)
            continue;
        Unit *b = &s_units[u->ride - 1];
        if (b->rider != i + 1 || b->zone == ZONE_FREE || u->state >= ST_DYING || u->hidden) { /* (se borró una de las dos) */
            if (b->rider == i + 1)
                b->rider = 0;
            u->ride = 0;
            u->killable = u->solid = 1;
            continue;
        }
        u->x = b->x;
        u->y = b->y;
        grid_moved(u);
        u->prev_x = b->prev_x;
        u->prev_y = b->prev_y;
    }
}

/* Lo del diablillo montado o yendo a montar: 1 si hizo algo */
static int a5_imp_ride(Unit *u, const Map *m, int t, const UnitTarget *tp, float d)
{
    if (u->ride) {
        const Unit *b = &s_units[u->ride - 1];
        if (d > 24.0f || b->state >= ST_DYING || b->hidden || !u->ai_p[1]) {
            a5_dismount(u, m);
            return 1;
        }
        if (d >= (float)a5_aip("imp2", 1) || (int)(rnd(u) % 100) >= a5_aip("imp2", 2)) {
            if (rnd(u) % 100 < 50 && u->gfx->anim[ANIM_SKILL].ok) { /* su S1, sin nada más */
                a5_show(u, t, ANIM_SKILL);
                return 1;
            }
        } else if (a5_skill(u, t, u->ai_p[1], 1, 0)) {
            return 1;
        }
        ai_stall(u, 20);
        return 1;
    }
    if (u->ai_p[0] > 0) {
        Unit *b = &s_units[u->ai_p[0] - 1];
        if (b->state >= ST_DYING || b->hidden || !b->enemy || b->rider || (b->ai != AI_SIEGEBEAST && b->ai != AI_SIEGETOWER)) {
            u->ai_p[0] = 0;
        } else if (dist_to(u, b->x, b->y) > (float)a5_aip("imp2", 0) + b->radius) {
            ai_go(u, b->x, b->y, 15, 100, 0);
            return 1;
        } else if (u->skill[0][0]) {
            a5_mount(u, b);
            return 1;
        }
    }
    (void)tp;
    return 0;
}

/* El teletransporte del diablillo (su habilidad k, Imp Teleport): como el original, con su animación (S1) y
   en el cuadro de golpe aparece en el lugar libre más cerca de (x, y); sin esa animación, de una */
static int a5_teleport(Unit *u, const Map *m, int t, int k, float x, float y)
{
    float tx, ty;
    if (!teleport_spot(u, m, x, y, &tx, &ty))
        return 0;
    if (a5_use(u, t, k, A5H_TELEPORT)) {
        u->a4_x = tx;
        u->a4_y = ty;
        return 1;
    }
    teleport_move(u, tx, ty);
    ai_stall(u, 6);
    return 1;
}

/* Los diablillos (Fn122): se teletransportan cerca, se alejan y disparan
   (con los parámetros de imp1..imp4, como el original) */
static int a5_imp(Unit *u, const Map *m, int t, const UnitTarget *tp, float d, int combat)
{
    if (a5_imp_ride(u, m, t, tp, d))
        return 1;
    int tele = a5_find(u, "Imp Teleport"), shot = 3; /* Skill4: el que tira (Imp Fire Missile...) */
    int life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
    float range = (float)a5_aip("imp1", 1);
    if (combat) {
        if (tele >= 0 && life < a5_aip("imp1", 0)) {
            float a = rndf(u) * 2.0f * PI_F;
            if (a5_teleport(u, m, t, tele, tp->x + cosf(a) * range, tp->y + sinf(a) * range))
                return 1;
        }
        if ((int)(rnd(u) % 100) < a5_aip("imp3", 1)) {
            ai_escape(u, tp, 5.0f);
            return 1;
        }
    }
    if (tele >= 0 && (int)(rnd(u) % 100) < a5_aip("imp1", 2)) {
        float a = rndf(u) * 2.0f * PI_F;
        if (a5_teleport(u, m, t, tele, tp->x + cosf(a) * range, tp->y + sinf(a) * range))
            return 1;
    }
    if (d < (float)a5_aip("imp3", 0) && (int)(rnd(u) % 100) < a5_aip("imp3", 1)) {
        ai_escape(u, tp, 5.0f);
        return 1;
    }
    if (u->skill[shot][0]) {
        int near = d < (float)a5_aip("imp3", 2), chance = near ? a5_aip("imp3", 3) : a5_aip("imp4", 3);
        if ((near || d < (float)a5_aip("imp4", 2)) && (int)(rnd(u) % 100) < chance && a5_skill(u, t, shot, 1, 0))
            return 1;
    }
    if ((int)(rnd(u) % 100) < 33) {
        ai_go(u, tp->x, tp->y, 15, 100, 1);
        return 1;
    }
    if ((int)(rnd(u) % 100) < 20) {
        ai_wander(u, 8, 100);
        return 1;
    }
    ai_stall(u, 10);
    return 1;
}

/* Las súcubos (Fn118 y Fn119): con el jugador sin maldición, a menos de
   aip4 y si sale aip3, una maldición: Skill1 si al jugador le queda al
   menos aip7 % de vida, Skill2 si a ella le queda aip8 % o menos, Skill3
   (Defense Curse) si el jugador tiene más vida que maná y si no Skill4
   (Blood Mana). Después pegar o acercarse. El proyectil (Skill5) solo con
   aip8 > 0: en Normal, solo la bruja. La bruja se aleja si sale aip3. */
static int a5_succubus(Unit *u, int t, const UnitTarget *tp, float d, int combat, int witch)
{
    int life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100;
    if (tp->curse == A4C_NONE && !tp->amplified && d < (float)u->aip[3] && ai_roll(u, 2)) {
        if (u->skill[0][0] && tp->life_pct >= u->aip[6] && a5_skill(u, t, 0, 1, 0))
            return 1;
        if (u->skill[1][0] && life <= u->aip[7] && a5_skill(u, t, 1, 1, 0))
            return 1;
        if (u->skill[2][0] && !tp->more_mana && a5_skill(u, t, 2, 1, 0))
            return 1;
        if (u->skill[3][0] && tp->player && a5_skill(u, t, 3, 1, 0))
            return 1;
    }
    if (combat) {
        if (witch && ai_roll(u, 2)) {
            ai_escape(u, tp, (float)u->aip[3]);
            return 1;
        }
        if (ai_roll(u, 0))
            ai_attack(u, t, ANIM_ATTACK);
        else
            ai_stall(u, witch ? u->aip[5] : u->aip[4]);
        return 1;
    }
    if (u->skill[4][0] && u->aip[7] > 0 && (!witch || ai_roll(u, 4)) && ai_roll(u, 7) && a5_skill(u, t, 4, 1, 0))
        return 1;
    if (witch && d < (float)u->aip[3] && ai_roll(u, 2)) {
        ai_escape(u, tp, (float)u->aip[3]);
        return 1;
    }
    if (ai_roll(u, 1)) {
        ai_go(u, tp->x, tp->y, 15, 100, 1);
        return 1;
    }
    if (witch && rnd(u) % 100 < 50)
        ai_circle(u, tp, 6.0f);
    else
        ai_stall(u, u->aip[5]);
    return 1;
}

/* ¿Se llega caminando de (fx, fy) hasta al lado de (tx, ty)? Se busca en el
   recuadro de los dos con 8 subtiles de margen (lejos, no se mira: 1) */
static int a5_walk_reach(const Map *m, const Unit *u, float fx, float fy, float tx, float ty)
{
    enum { W = 72 };
    static const int k_dx[4] = { 1, -1, 0, 0 }, k_dy[4] = { 0, 0, 1, -1 };
    static uint8_t seen[W * W];
    static int queue[W * W];
    int x0 = (int)floorf(fminf(fx, tx)) - 8, y0 = (int)floorf(fminf(fy, ty)) - 8;
    int w = (int)fabsf(tx - fx) + 18, h = (int)fabsf(ty - fy) + 18, qh = 0, qt = 0;
    if (w > W || h > W)
        return 1;
    int gx = (int)floorf(tx) - x0, gy = (int)floorf(ty) - y0, s = ((int)floorf(fy) - y0) * W + ((int)floorf(fx) - x0);
    memset(seen, 0, sizeof seen);
    seen[s] = 1;
    queue[qt++] = s;
    while (qh < qt) {
        int c = queue[qh++], cx = c % W, cy = c / W;
        if (abs(cx - gx) <= 2 && abs(cy - gy) <= 2)
            return 1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + k_dx[k], ny = cy + k_dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[ny * W + nx] ||
                wall_foot(m, (float)(x0 + nx) + 0.5f, (float)(y0 + ny) + 0.5f, 1, u->move_mask))
                continue;
            seen[ny * W + nx] = 1;
            queue[qt++] = ny * W + nx;
        }
    }
    return 0;
}

/* Un cadáver para la explosión de Nihlathak: el más cercano al jugador, a menos de 10 subtiles de él */
static int a5_corpse_near(const UnitTarget *tp)
{
    int best = -1;
    float bd = 100.0f;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o->state != ST_DEAD || o->hidden || !o->enemy || o->zone == ZONE_FREE)
            continue;
        float dd = (o->x - tp->x) * (o->x - tp->x) + (o->y - tp->y) * (o->y - tp->y);
        if (dd < bd) {
            bd = dd;
            best = i;
        }
    }
    return best;
}

/* Cuál de los Antiguos es (los tres tienen BaseId ancientbarb1; el original
   los separa por la clase): "Ancient Barbarian 2" o "ancientbarb2" terminan
   en su número (1 Talic, 2 Madawc, 3 Korlic) */
static int a5_ancient_kind(const Unit *u)
{
    size_t n = strlen(u->name);
    char c = n > 0 ? u->name[n - 1] : '1';
    return c >= '1' && c <= '3' ? c - '0' : 1;
}

/* Los Antiguos (Fn133): Talic gira (Whirlwind hasta pasar al jugador),
   Madawc grita y tira, Korlic salta; sin el rito, quietos */
static int a5_ancient(Unit *u, const Map *m, int t, const UnitTarget *tp, float d, int combat)
{
    if ((!s_a5_ancients && !u->colossal) || !tp) { /* (los colosales no esperan el rito) */
        ai_stall(u, 25);
        return 1;
    }
    int kind = a5_ancient_kind(u);
    if (kind == 1) { /* Talic */
        if (u->skill[0][0] && d < (float)u->aip[0] && ai_roll(u, 1)) {
            float dx = tp->x - u->x, dy = tp->y - u->y, l = sqrtf(dx * dx + dy * dy);
            l = l > 0.01f ? l : 1.0f;
            u->leap_x1 = tp->x + dx / l * (float)u->aip[3];
            u->leap_y1 = tp->y + dy / l * (float)u->aip[3];
            if (ai_skill(u, t, 0))
                return 1;
        }
        if (combat) {
            if (ai_roll(u, 2))
                ai_attack(u, t, rnd(u) & 1 ? ANIM_ATTACK : ANIM_ATTACK2);
            else
                ai_stall(u, 25);
            return 1;
        }
        ai_go(u, tp->x, tp->y, 10, 100, 1);
        return 1;
    }
    if (kind == 2) { /* Madawc */
        if (combat && ai_roll(u, 3)) {
            ai_escape(u, tp, (float)u->aip[4]);
            return 1;
        }
        /* el Grito, si no lo tiene él ni el jugador */
        if (u->skill[0][0] && u->shout_ticks <= 0 && !tp->shout && ai_roll(u, 2) && a5_use(u, t, 0, A5H_SHOUT))
            return 1;
        if (d < (float)u->aip[0] && ai_roll(u, 1)) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        ai_go(u, tp->x, tp->y, 10, 100, 1);
        return 1;
    }
    /* Korlic: el salto con golpe */
    if (u->skill[0][0] && d < (float)u->aip[0] && ai_roll(u, 1) && ai_leap(u, m, t, tp))
        return 1;
    if (!combat) {
        ai_go(u, tp->x, tp->y, 10, 100, 1);
        return 1;
    }
    if (ai_roll(u, 2))
        ai_attack(u, t, rnd(u) & 1 ? ANIM_ATTACK : ANIM_ATTACK2);
    else
        ai_stall(u, 25);
    return 1;
}

/* Baal (Fn135 y Fn140): elige entre sus 16 cosas con los pesos del
   original según si el jugador está al lado, lejos de su lugar o detrás de
   algo que tapa los proyectiles (los de Normal) */
static int a5_baal(Unit *u, const Map *m, UnitTarget *pl, int t, float d, int clone)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    if (!u->ai_p[2]) { /* su lugar (la orden 10 del original) */
        u->ai_p[2] = 1;
        u->leap_x0 = u->x;
        u->leap_y0 = u->y;
    }
    int w[16] = { 0 }, act = 1;
    if (!tp) {
        act = rnd(u) % 100 < 8 ? 9 : 1;
    } else {
        int life = u->max_hp > 0 ? u->hp * 100 / u->max_hp : 100, count = 0;
        for (int i = 0; i < MAX_PLAYERS; i++)
            count += pl[i].alive && !pl[i].in_town;
        float hx = tp->x - u->leap_x0, hy = tp->y - u->leap_y0, home = sqrtf(hx * hx + hy * hy);
        int medium = home > 75.0f, far_away = home > 100.0f;
        int melee = d <= u->reach + 0.6f;
        int clear = !(m && map_ray(m, (int)u->x, (int)u->y, (int)tp->x, (int)tp->y, COLL_MISSILE_BARRIER, NULL, NULL));
        if (melee) {
            static const int k_melee[16] = { 0, 75, 0, 0, 0, 0, 0, 0, 20, 30, 150, 10, 10, 45, 40, 0 };
            memcpy(w, k_melee, sizeof w);
            if (tp->life_pct < 33)
                w[10] += 50;
            w[1] += 100 - life;
            if (tp->chill_ticks > 0)
                w[8] = w[13] = w[11] = 0;
            if (!clear)
                w[11] = w[13] = w[12] = 0;
        } else if (clear) {
            static const int k_open[16] = { 0, 0, 5, 5, 5, 0, 5, 0, 40, 40, 0, 70, 80, 40, 20, 0 };
            memcpy(w, k_open, sizeof w);
            w[15] = 10 * (2 - u->ai_p[1]);
            w[15] = w[15] < 0 ? 0 : w[15];
            if (d > 35.0f) {
                w[6] = 15;
                w[13] = 0;
            }
            w[1] = 100 - life + 125;
            if (d > 25.0f) {
                w[14] = 30;
                w[11] = 0;
            }
            if (count < 2)
                w[11] -= 10;
            w[8] = 0; /* (con menos de dos jugadores en Normal, sin maldiciones de Baal) */
            if (medium) {
                w[3] = 25;
                w[2] = 0;
                w[6] += 25;
                w[14] += 35;
            }
            if (far_away)
                w[5] = 60;
        } else {
            static const int k_wall[16] = { 0, 125, 20, 20, 20, 0, 20, 0, 80, 70, 0, 0, 0, 0, 0, 0 };
            memcpy(w, k_wall, sizeof w);
            if (count < 2) {
                w[2] = 45;
                w[10] = 25;
            }
            w[1] += 100 - life;
            if (medium) {
                w[2] = w[6] = 0;
                w[3] += 15;
                w[11] += 25;
                w[13] += 25;
            }
            if (far_away)
                w[5] += 60;
        }
        if (clone) /* (el clon no se clona) */
            w[15] = 0;
        int sum = 0;
        for (int i = 0; i < 16; i++)
            sum += w[i] > 0 ? w[i] : 0;
        if (sum > 0) {
            int r = (int)(rnd(u) % (uint32_t)sum);
            for (int i = 0; i < 16; i++) {
                int wi = w[i] > 0 ? w[i] : 0;
                if (r < wi) {
                    act = i;
                    break;
                }
                r -= wi;
            }
        }
    }
    switch (act) {
    case 1:
        ai_stall(u, 35);
        return 1;
    case 2:
    case 6:
        ai_go(u, tp->x, tp->y, 15, 100, 0);
        return 1;
    case 3:
        ai_circle(u, tp, 6.0f);
        return 1;
    case 4:
        ai_wander(u, 16, 100);
        return 1;
    case 5:
        ai_go(u, u->leap_x0, u->leap_y0, 40, 100, 0);
        return 1;
    case 8: /* sus maldiciones: Skill6 si el jugador tiene más vida que maná (o no es un jugador), si no Skill7 */
        if (tp && u->skill[5][0] && !tp->more_mana && a5_skill(u, t, 5, 1, 0))
            return 1;
        if (tp && u->skill[6][0] && tp->player && a5_skill(u, t, 6, 1, 0))
            return 1;
        ai_stall(u, 5);
        return 1;
    case 9: { /* los tentáculos */
        int k = a5_find(u, "Baal Tentacle");
        if (!tp || k < 0 || !a5_use(u, t, k, A5H_TENTACLE))
            ai_stall(u, 5);
        return 1;
    }
    case 10:
        ai_attack(u, t, ANIM_ATTACK2);
        return 1;
    case 11: { /* la nova */
        int k = a5_find(u, "Baal Nova");
        if (k < 0 || !a5_skill(u, t, k, 32, 1))
            ai_stall(u, 5);
        return 1;
    }
    case 12: { /* el aliento */
        int k = a5_find(u, "Baal Inferno");
        if (k < 0 || !a5_skill(u, t, k, 1, 0))
            ai_stall(u, 5);
        return 1;
    }
    case 13: { /* los misiles de frío */
        int k = a5_find(u, "Baal Cold Missiles");
        if (k < 0 || !a5_use(u, t, k, A5H_COLDMAKER))
            ai_stall(u, 5);
        return 1;
    }
    case 14: { /* se teletransporta lejos del jugador */
        float dx = u->x - tp->x, dy = u->y - tp->y, l = sqrtf(dx * dx + dy * dy);
        l = l > 1.0f ? l : 1.0f;
        if (!ai_teleport(u, m, u->x + dx / l * 25.0f, u->y + dy / l * 25.0f))
            ai_stall(u, 5);
        return 1;
    }
    case 15: { /* el clon, con un tercio de su vida (hasta dos) */
        int clones = 0;
        for (int i = 0; i < s_num_units; i++)
            clones += s_units[i].master == (int)(u - s_units) && s_units[i].state < ST_DYING && strcmp(s_units[i].base, "baalclone") == 0;
        if (!clones) {
            float x = tp->x + (float)((int)(rnd(u) % 24) - 12), y = tp->y + (float)((int)(rnd(u) % 24) - 12);
            Unit *c = a5_spawn(u, "baalclone", x, y);
            if (c) {
                c->max_hp = u->max_hp / 3;
                c->hp = u->hp / 3 > 0 ? u->hp / 3 : 1;
                c->master = (int)(u - s_units);
                u->ai_p[1]++;
            }
        }
        ai_stall(u, 5);
        return 1;
    }
    default:
        ai_stall(u, 5);
        return 1;
    }
}

/* Baal en su trono (Fn134): las cinco oleadas (Baal Subject 1..5: Colenzo,
   Achmel, Bartuc, Ventar y Lister), cada una cuando no queda nadie de la
   anterior en la sala; después baja (baalcrabstairs) y se va a la Cámara */
static int a5_baal_throne(Unit *u, UnitTarget *pl, int t)
{
    static const char *const k_waves[5] = { "Baal Subject 1", "Baal Subject 2", "Baal Subject 3", "Baal Subject 4", "Baal Subject 5" };
    if (t < 0) {
        ai_stall(u, 10);
        return 1;
    }
    if ((int)s_unit_ticks < u->ai_p[2]) {
        ai_stall(u, 5);
        return 1;
    }
    /* los de la oleada que siguen vivos cerca (a 64 subtiles) */
    int alive = 0;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        alive += o != u && o->enemy && o->state < ST_DYING && !o->hidden && o->ai != AI_BAALTHRONE && dist_to(u, o->x, o->y) < 64.0f &&
                 o->zone == u->zone;
    }
    if (alive) { /* mientras tanto, a veces su maldición (Decrepify) */
        if (ai_roll(u, 0) && pl[t].curse == A4C_NONE && a5_skill(u, t, 0, 1, 0))
            return 1;
        ai_stall(u, 10);
        return 1;
    }
    if (u->ai_p[0] >= 5) { /* las cinco oleadas: se vuelve baalcrabstairs (MONSTER_Reinitialize), que baja caminando hasta su portal */
        int saved = s_spawn_zone;
        s_spawn_zone = u->zone;
        Unit *c = create_monster("baalcrabstairs", u->x, u->y, u->level, 1);
        s_spawn_zone = saved;
        if (!c) { /* (sin lugar: se va como antes) */
            missiles_effect("teleport", u->x, u->y);
            s_a5_baal_left = 1;
        } else {
            c->sector = u->sector;
            if (getenv("D2_AI_DEBUG"))
                printf("IA: Baal deja el trono y baja a su portal\n");
        }
        u->hidden = 1;
        u->solid = 0;
        u->hp = 0;
        set_state(u, ST_DEAD);
        return 1;
    }
    if (!(u->ai_p[1] & 1)) { /* antes de cada oleada, los cadáveres explotan (BaalCorpseExplode) y espera 10 segundos */
        u->ai_p[1] |= 1;
        u->ai_p[2] = (int)s_unit_ticks + 10 * TICK_RATE;
        if (u->gfx->anim[ANIM_SKILL3].ok)
            ai_attack(u, t, ANIM_SKILL3);
        else
            ai_stall(u, 10);
        for (int i = 0; i < s_num_units; i++)
            if (s_units[i].state == ST_DEAD && !s_units[i].hidden && s_units[i].zone == u->zone && dist_to(u, s_units[i].x, s_units[i].y) < 64.0f)
                s_units[i].hidden = 1;
        return 1;
    }
    int saved = s_spawn_zone;
    s_spawn_zone = u->zone;
    spawn_superunique(k_waves[u->ai_p[0]], u->x, u->y + 13.0f, u->level);
    s_spawn_zone = saved;
    u->ai_p[0]++;
    u->ai_p[1] &= ~1;
    u->ai_p[2] = (int)s_unit_ticks + 4 * TICK_RATE;
    ai_stall(u, 10);
    return 1;
}

static int ai_think5(Unit *u, const Map *m, UnitTarget *pl, int t, float d, int hurt)
{
    const UnitTarget *tp = t >= 0 ? &pl[t] : NULL;
    int combat = tp && d <= u->reach;
    (void)hurt;
    switch (u->ai) {
    case AI_MINION: { /* (la probabilidad de A2 lee aip2: un error del original) */
        /* con la orden del capataz (Cry Help: ai_p[0] hasta qué tick, ai_p[1] el jugador + 1) va por
           ese jugador sin quedarse quieto */
        int mt = t, order = u->ai_p[1] > 0 && (int)s_unit_ticks < u->ai_p[0];
        if (order && pl[u->ai_p[1] - 1].alive && !pl[u->ai_p[1] - 1].in_town)
            mt = u->ai_p[1] - 1;
        else
            order = 0;
        if (mt < 0)
            return 0;
        const UnitTarget *mp = &pl[mt];
        if (dist_to(u, mp->x, mp->y) <= u->reach) {
            if (order || ai_roll(u, 0))
                ai_attack(u, mt, ai_roll(u, 1) && u->gfx->anim[ANIM_ATTACK2].ok ? ANIM_ATTACK2 : ANIM_ATTACK);
            else
                ai_stall(u, u->aip[1]);
            return 1;
        }
        if (order || ai_roll(u, 2))
            ai_go(u, mp->x, mp->y, 15, 100, mt == t);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    }
    case AI_SUICIDEMINION: /* al llegar al jugador espera aip5 (vacío: 0) y revienta */
        if (u->ai_p[0]) {
            if ((int)s_unit_ticks > u->ai_p[0]) {
                units_play_death((int)(u - s_units)); /* (sin experiencia; explota al morir: a5_suicide_boom) */
                return 1;
            }
            ai_stall(u, u->aip[1]);
            return 1;
        }
        if (!tp)
            return 0;
        if (combat) {
            u->ai_p[0] = (int)s_unit_ticks + u->aip[4]; /* (el parámetro 4 del original, no el 0 que dice MonAi.txt) */
            ai_stall(u, u->aip[1]);
            return 1;
        }
        if (ai_roll(u, 2))
            ai_go(u, tp->x, tp->y, 15, 100, 1);
        else
            ai_stall(u, u->aip[1]);
        return 1;
    case AI_SUCCUBUS:
    case AI_SUCCWITCH:
        if (!tp)
            return 0;
        return a5_succubus(u, t, tp, d, combat, u->ai == AI_SUCCWITCH);
    case AI_IMP:
        if (!tp) {
            if (!u->ride)
                return 0;
            ai_stall(u, 20); /* (montado, sin nadie a la vista: quieto en la bestia) */
            return 1;
        }
        return a5_imp(u, m, t, tp, d, combat);
    case AI_BLOODLORD:
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ai_roll(u, 2) && u->gfx->anim[ANIM_ATTACK2].ok ? ANIM_ATTACK2 : ANIM_ATTACK);
            else
                ai_stall(u, u->aip[3]);
            return 1;
        }
        if (ai_roll(u, 1))
            ai_go(u, tp->x, tp->y, 15, 100, 1);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    case AI_REANIMATED:
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[1]);
            return 1;
        }
        if (u->skill[1][0] && d < (float)u->aip[2] && d > 5.0f && ai_roll(u, 3) && ai_skill(u, t, 1)) /* la carga */
            return 1;
        if (ai_roll(u, 4)) {
            ai_go(u, tp->x, tp->y, 15, 100, 1);
            return 1;
        }
        if (ai_roll(u, 5)) {
            ai_wander(u, 4, 100);
            return 1;
        }
        ai_stall(u, u->aip[6]);
        return 1;
    case AI_OVERSEER: { /* (Fn120) */
        if (!tp)
            return 0;
        /* recién golpeado, cada aip1 cuadros: Cry Help (los suyos van por ese jugador) */
        if (hurt && tp->player && u->skill[0][0] && (int)s_unit_ticks > u->ai_p[0] && a5_use(u, t, 0, A5H_CRYHELP)) {
            u->ai_p[0] = (int)s_unit_ticks + u->aip[0];
            return 1;
        }
        if (combat && ai_roll(u, 5)) {
            ai_attack(u, t, ai_roll(u, 6) && u->gfx->anim[ANIM_ATTACK2].ok ? ANIM_ATTACK2 : ANIM_ATTACK);
            return 1;
        }
        /* el primer esbirro a 24 subtiles con menos de la mitad de la vida (lo cura) y el primero a 20
           que no es jefe ni tiene ya la sed de sangre (lo azota) */
        int heal = -1, whip = -1;
        for (int i = 0; i < s_num_units; i++) {
            const Unit *o = &s_units[i];
            if (!a5_is_minion(u, o, 24.0f) || o->lust_ticks > 0)
                continue;
            if (heal < 0 && o->hp * 2 < o->max_hp)
                heal = i;
            if (whip < 0 && dist_to(u, o->x, o->y) <= 20.0f && !o->unique)
                whip = i;
        }
        int sk = heal >= 0 && u->skill[1][0] && ai_roll(u, 1) ? 1 : whip >= 0 && u->skill[2][0] && ai_roll(u, 2) ? 2 : 0;
        if (sk && a5_use(u, t, sk, sk == 1 ? A5H_HEAL : A5H_WHIP)) {
            int pick = sk == 1 ? heal : whip;
            u->a4_corpse = pick;
            u->sector = sector_from_world(s_units[pick].x - u->x, s_units[pick].y - u->y);
            return 1;
        }
        if (a5_count(u, "minion1", 24.0f)) { /* con los suyos cerca, a su distancia */
            if (d < (float)(u->aip[3] - u->aip[4])) {
                ai_escape(u, tp, (float)u->aip[3] - d);
                return 1;
            }
            if (d > (float)(u->aip[4] + u->aip[3])) {
                ai_go(u, tp->x, tp->y, 15, 100, 1);
                return 1;
            }
            if (rnd(u) % 100 < 50) {
                ai_circle(u, tp, 5.0f);
                return 1;
            }
        } else if (rnd(u) % 100 < 60) {
            ai_go(u, tp->x, tp->y, 15, 100, 1);
            return 1;
        }
        ai_stall(u, 10);
        return 1;
    }
    case AI_DEATHMAULER:
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 0)) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
        } else {
            /* el zarpazo bajo tierra (su secuencia seq_deathmaulerdig; las garras salen en su cuadro de evento:
               a5_death_maul) */
            if (u->skill[0][0] && d < (float)u->aip[2] && ai_roll(u, 3) && missiles_exists("death mauler")) {
                if (!ai_skill(u, t, 0))
                    ai_stall(u, 15);
                return 1;
            }
            if (ai_roll(u, 1)) {
                ai_go(u, tp->x, tp->y, 15, 100, 1);
                return 1;
            }
        }
        ai_stall(u, 15);
        return 1;
    case AI_SIEGEBEAST: {
        if (!u->rider) /* sin jinete: llama a un diablillo (Fn115) */
            a5_call_imp(u, (float)u->aip[0]);
        if (!tp)
            return 0;
        int stomp = a5_find(u, "Siege Beast Stomp");
        if (combat) {
            if (stomp >= 0 && ai_roll(u, 2) && a5_use(u, t, stomp, A5H_STOMP))
                return 1;
            if (ai_roll(u, 1))
                ai_attack(u, t, ANIM_ATTACK);
            else
                ai_stall(u, u->aip[3]);
            return 1;
        }
        if (stomp >= 0 && d < 5.0f && ai_roll(u, 4) && a5_use(u, t, stomp, A5H_STOMP))
            return 1;
        ai_go(u, tp->x, tp->y, 15, ai_roll(u, 5) ? 100 + u->aip[6] : 100, 1);
        return 1;
    }
    case AI_PUTRID: { /* (Fn137) pega al de al lado; si no, preña al monstruo más cerca (a 25 o menos) */
        if (!tp)
            return 0;
        if (combat) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        int best = -1;
        float bd = 25.0f;
        for (int i = 0; i < s_num_units; i++) {
            const Unit *o = &s_units[i];
            float od = o != u && a5_can_impregnate(o) ? dist_to(u, o->x, o->y) - o->radius : 1e9f;
            if (od <= bd) {
                bd = od;
                best = i;
            }
        }
        if (best >= 0) {
            const Unit *o = &s_units[best];
            if (bd <= u->reach - UNIT_PLAYER_BODY && a5_use(u, t, 0, A5H_IMPREGNATE)) {
                u->a4_corpse = best;
                u->sector = sector_from_world(o->x - u->x, o->y - u->y);
            } else {
                ai_go(u, o->x, o->y, 15, 100, 0);
            }
            return 1;
        }
        if (d < (float)u->aip[0]) {
            ai_escape(u, tp, (float)u->aip[1]);
            return 1;
        }
        ai_stall(u, 25);
        return 1;
    }
    case AI_FROZENHORROR:
        if (!tp)
            return 0;
        if (u->skill[0][0] && d < (float)ai_sklvl(u, 0) + 6.0f && ai_roll(u, 2) && a5_skill(u, t, 0, 1, 0))
            return 1;
        if (combat) {
            if (ai_roll(u, 0)) {
                ai_attack(u, t, ANIM_ATTACK);
                return 1;
            }
        } else if (ai_roll(u, 1)) {
            ai_go(u, tp->x, tp->y, 15, 100, 1);
            return 1;
        }
        ai_stall(u, u->aip[3]);
        return 1;
    case AI_CATAPULT: /* (Fn123) solo se anima: su A1 con aip1 %, sin blanco (los tiros los pide el vigía) */
        if (ai_roll(u, 0) && u->gfx->anim[ANIM_ATTACK].ok)
            a5_show(u, -1, ANIM_ATTACK);
        else
            ai_stall(u, 15);
        return 1;
    case AI_SPOTTER: { /* el vigía (Fn126): pasados aip2 cuadros del último y si sale aip1, un tiro a ±aip4 del jugador */
        if (!tp || (int)s_unit_ticks < u->ai_p[2] + u->aip[1] || !ai_roll(u, 0)) {
            ai_stall(u, u->aip[1] > 0 ? u->aip[1] : 15);
            return 1;
        }
        if (u->ai_p[1] > 0) { /* el mismo tiro aip5 veces, después otro de los cinco */
            u->ai_p[1]--;
        } else {
            u->ai_p[0] = (int)(rnd(u) % 5u);
            u->ai_p[1] = u->aip[4];
        }
        int r = u->aip[3] > 0 ? u->aip[3] : 1;
        u->a4_x = tp->x + (float)((int)(rnd(u) % (uint32_t)(2 * r)) - r);
        u->a4_y = tp->y + (float)((int)(rnd(u) % (uint32_t)(2 * r)) - r);
        if ((!m || !wall_foot(m, u->a4_x, u->a4_y, 2, COLL_MASK_MONSTER_PATH)) && a5_use(u, t, u->ai_p[0], A5H_CATAPULT)) {
            u->ai_p[2] = (int)s_unit_ticks;
            return 1;
        }
        ai_stall(u, 15);
        return 1;
    }
    case AI_MINIONSPAWNER: /* el que larga esbirros (Fn121): aip1 en total, de a uno cada aip3 cuadros, si hay menos de aip2 */
        if (tp && u->ai_p[1] < u->aip[0] && d <= (float)u->aip[3] && (int)s_unit_ticks >= u->ai_p[0] &&
            a5_count(u, "minion1", 30.0f) + a5_count(u, "suicideminion1", 30.0f) < u->aip[1]) {
            u->ai_p[0] = (int)s_unit_ticks + u->aip[2];
            u->ai_p[1]++;
            u->a4_x = u->x + (float)((int)(rnd(u) % 5) - 2);
            u->a4_y = u->y + 2.0f;
            if (a5_use(u, t, 0, A5H_SPAWN))
                return 1;
        }
        ai_stall(u, u->aip[1] > 0 ? u->aip[1] : 25);
        return 1;
    case AI_EVILHOLE: /* el agujero (Fn076): larga aip2 crías, una cada aip1 cuadros, con el jugador a menos de 5 */
        if (!u->ai_p[0]) {
            u->ai_p[0] = (int)s_unit_ticks + u->aip[0];
            u->ai_p[1] = u->aip[1];
        }
        if (tp && d <= 5.0f && u->ai_p[1] > 0 && (int)s_unit_ticks > u->ai_p[0]) {
            u->ai_p[0] = (int)s_unit_ticks + u->aip[0];
            u->ai_p[1]--;
            a5_spawn(u, u->minion, u->x + 1.0f, u->y + 1.0f);
        }
        ai_stall(u, u->aip[0] > 0 ? u->aip[0] : 10);
        return 1;
    case AI_GENSPAWNER: /* la choza (Fn129): con el jugador a menos de 20, aip3 crías, una cada aip1 cuadros */
        if (tp && d <= 20.0f && u->ai_p[1] < u->aip[2] && (int)s_unit_ticks - u->ai_p[0] >= u->aip[0]) {
            u->ai_p[0] = (int)s_unit_ticks;
            u->ai_p[1]++;
            u->a4_x = u->x + (float)((int)(rnd(u) % 5) - 2);
            u->a4_y = u->y + 2.0f;
            if (a5_use(u, t, 0, A5H_SPAWN))
                return 1;
            a5_spawn(u, u->minion[0] ? u->minion : "minion1", u->a4_x, u->a4_y);
        }
        ai_stall(u, 20);
        return 1;
    case AI_NIHLATHAK: {
        int tele = a5_find(u, "Imp Teleport"), boom = a5_find(u, "NihlathakCorpseExplosion"), blast = a5_find(u, "Horror Arctic Blast");
        if (!tp) {
            ai_stall(u, 25);
            return 1;
        }
        /* Peleando, a un lugar al azar a ±aip2 de él mismo (AITACTICS_UseSkillInRange; antes iba
           alrededor del jugador). El original no mira si desde ahí se llega: en los Halls of Vaught
           hay un pasillo cerrado al lado de su sala, del otro lado de una pared de un subtile, y
           ahí quedaba sin que se lo pudiera alcanzar; esos lugares se descartan */
        if (tele >= 0 && combat && ai_roll(u, 0)) {
            int r = u->aip[1] > 0 ? u->aip[1] : 20;
            for (int k = 0; k < 4; k++) {
                float x = u->x + (float)((int)(rnd(u) % (uint32_t)(2 * r)) - r);
                float y = u->y + (float)((int)(rnd(u) % (uint32_t)(2 * r)) - r);
                if (!wall_foot(m, x, y, u->foot, u->move_mask) && a5_walk_reach(m, u, x, y, tp->x, tp->y) &&
                    ai_teleport(u, m, x, y))
                    return 1;
            }
        }
        if (d < (float)u->aip[4] && rnd(u) % 100 < 40) {
            ai_escape(u, tp, 5.0f);
            return 1;
        }
        if (boom >= 0 && ai_roll(u, 2)) {
            int c = a5_corpse_near(tp);
            if (c >= 0) {
                u->a4_corpse = c;
                if (a5_use(u, t, boom, A5H_CORPSE))
                    return 1;
            }
        }
        if (blast >= 0 && rnd(u) % 100 < 60 && d < 14.0f && a5_skill(u, t, blast, 1, 0))
            return 1;
        if (rnd(u) % 100 < 60) {
            ai_go(u, tp->x, tp->y, 15, 100, 1);
            return 1;
        }
        ai_stall(u, 5);
        return 1;
    }
    case AI_ANCIENT:
        return a5_ancient(u, m, t, tp, d, combat);
    case AI_BAAL:
    case AI_BAALCLONE:
        return a5_baal(u, m, pl, t, d, u->ai == AI_BAALCLONE);
    case AI_BAALTHRONE:
        return a5_baal_throne(u, pl, t);
    case AI_BAALTENTACLE: { /* vive de aip3 a 2*aip3 segundos, y muere con Baal */
        const Unit *o = u->master >= 0 && u->master < s_num_units ? &s_units[u->master] : NULL;
        if (!o || o->state >= ST_DYING) {
            units_play_death((int)(u - s_units));
            return 1;
        }
        if (!u->ai_p[2])
            u->ai_p[2] = (int)s_unit_ticks + TICK_RATE * (u->aip[2] + (int)(rnd(u) % (uint32_t)(u->aip[2] > 0 ? u->aip[2] : 1)));
        if ((int)s_unit_ticks > u->ai_p[2]) {
            units_play_death((int)(u - s_units));
            return 1;
        }
        if (combat && ai_roll(u, 0)) {
            ai_attack(u, t, ANIM_ATTACK);
            return 1;
        }
        ai_stall(u, u->aip[1] > 0 ? u->aip[1] : 10);
        return 1;
    }
    case AI_BAALMINION:
        if (!tp)
            return 0;
        if (combat) {
            if (ai_roll(u, 0))
                ai_attack(u, t, u->skill[0][0] && ai_roll(u, 2) && u->gfx->anim[ANIM_ATTACK2].ok ? ANIM_ATTACK2 : ANIM_ATTACK);
            else
                ai_stall(u, u->aip[2]);
            return 1;
        }
        if (ai_roll(u, 1))
            ai_go(u, tp->x, tp->y, 15, 100, 1);
        else
            ai_stall(u, u->aip[3]);
        return 1;
    case AI_BAALSTAIRS: { /* baja hasta el portal de la Cámara y se va (Fn138) */
        int id;
        float px, py;
        if (units_find_all("FinalPortal", &id, 1) != 1) {
            ai_stall(u, 25);
            return 1;
        }
        units_position(id, 1.0f, &px, &py);
        if (dist_to(u, px, py) > (float)(u->aip[0] > 0 ? u->aip[0] : 4)) {
            ai_go(u, px, py, 25, 100, 0);
            return 1;
        }
        s_a5_baal_left = 1;
        u->hidden = 1;
        u->solid = 0;
        u->hp = 0;
        set_state(u, ST_DEAD);
        return 1;
    }
    case AI_BAALTAUNT: /* (Fn136) invisible: va con el jugador y, si lo ve quieto más de aip2 veces seguidas, se burla */
        u->solid = 0; /* (SizeX 0 en monstats2.txt: no choca con nadie) */
        if (!tp) {
            ai_stall(u, 25);
            return 1;
        }
        if (!tp->still) {
            u->ai_p[0] = 0;
        } else if (++u->ai_p[0] > u->aip[1]) {
            u->ai_p[0] = 0;
            if (!a5_use(u, t, 0, A5H_TAUNT)) { /* (sin su animación: ya) */
                a5_taunt_start(u, tp->x, tp->y);
                ai_stall(u, 25);
            }
            return 1;
        }
        if ((d > (float)u->aip[2] && a5_hop(u, m, tp)) || d <= (float)u->aip[0])
            ai_stall(u, 25);
        else
            ai_go(u, tp->x, tp->y, 15, 100, 1);
        return 1;
    case AI_SIEGETOWER: /* (Fn113) sin jinete y con el jugador a la vista, llama a un diablillo a 20 o menos */
        if (!u->rider && tp)
            a5_call_imp(u, 20.0f);
        ai_stall(u, u->aip[0] > 0 ? u->aip[0] : 25);
        return 1;
    case AI_ANCSTATUE:
    case AI_NPCBARB:
    case AI_WUSSIE:
        ai_stall(u, 25);
        return 1;
    default:
        return ai_think1(u, m, pl, t, d, hurt);
    }
}
