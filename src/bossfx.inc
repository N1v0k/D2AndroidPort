/* ---- La muerte de los jefes que en el original tiene función propia ----
   (se incluye desde game.c)

   Blood Raven (brdeathcontrol de missiles.txt): unos 15 segundos de rayos
   (brdeathlightningbolt: no se ven, solo alumbran) que salen del cuerpo
   hacia cualquier lado, recorren hasta sus 20 cuadros a Vel 32 (unos 20
   subtiles) o hasta chocar con una pared (ClientCol, LastCollide), y caen
   donde terminan (brdeathlightninghit): por todo el cementerio, no solo
   sobre ella; con su chasquido y truenos, mientras suben el humo y los espíritus
   (brdeathsmoke* y brdeathspirit*, encadenados: empieza, sigue y termina,
   con los sonidos de la niebla).
   Andariel (andycontrol0): unos 14 segundos de columnas de fuego sobre el
   cuerpo, paredes de fuego que salen hacia afuera y piedras que caen del
   techo de las catacumbas.
   Los demás jefes del Acto 1 mueren sin nada especial, como en el original.

   Y las Piedras Cairn (cairnstones, 300 cuadros): al tocar la quinta en
   orden la escena se oscurece en 40 cuadros (Fade Frames), corren rayos por
   el piso de cada piedra a la segunda de al lado, una estrella que se cruza
   como un pentagrama (cairnstonesground, que deja su rastro de
   lightninghit), y caen rayos grandes (cairn stones bolt) cada 2 cuadros por
   todos lados, a menos de 17 subtiles del centro (Param3), del cuadro 40 al
   260. El portal rojo se abre a los 160: el contador del original baja desde
   300 y lo abre al llegar a Param1 + Param5 = 140. Al final la escena vuelve
   a aclararse en otros 40.
   La Guarida del Mal al limpiarla (QUESTS_TriggerFX del cliente): haces de
   luz que bajan del techo alrededor (denofevillight: LightBeams, 12
   segundos) mientras la cueva se ilumina. */

enum { BFX_NONE, BFX_RAVEN, BFX_ANDY, BFX_CAIRN, BFX_DEN };

#define BFX_MAX        4
#define BFX_CAIRN_TIME 300  /* Range de cairnstones */
#define BFX_CAIRN_FADE 40   /* Fade Frames: se oscurece y al final se aclara */
#define BFX_CAIRN_OPEN 160  /* a los 6,4 s se abre el portal */
#define BFX_CAIRN_SKY  17.0f /* Param3: hasta dónde caen los rayos grandes */
#define BFX_CAIRN_DARK 15   /* cuánto se oscurece (de los 31 niveles de luz) */
#define BFX_GROUND_SPEED (20.0f * MISSILE_VEL_PER_SEC / TICK_RATE) /* Vel de cairnstonesground */
#define BFX_DEN_TIME   60   /* los haces de la Guarida aparecen en 2,4 s (y duran su Range: 300) */
#define BFX_DEN_EVERY  5
#define BFX_RAYS       6
#define BFX_RAVEN_TIME 375 /* Range de brdeathcontrol */
#define BFX_ANDY_TIME  350 /* Range de andycontrol0 */
#define BFX_RAY_SPEED  (8.0f * MISSILE_VEL_PER_SEC / TICK_RATE) /* Vel de andyfirewallmaker */
#define BFX_RAY_TICKS  50                                /* su Range */
#define BFX_BOLTS      16 /* los rayos de Blood Raven que van hacia el cuerpo */
#define BFX_BOLT_SPEED (32.0f * MISSILE_VEL_PER_SEC / TICK_RATE) /* Vel de brdeathlightningbolt */

typedef struct {
    int   kind, age;
    float x, y;
    float ray_x[BFX_RAYS], ray_y[BFX_RAYS], ray_dx[BFX_RAYS], ray_dy[BFX_RAYS];
    int   ray_on[BFX_RAYS], ray_age;
    float stone_x[5], stone_y[5]; /* las Piedras Cairn */
    int   ring[5];                    /* (en orden alrededor del centro: la estrella une cada una con la segunda de al lado) */
    int   bolt_at[BFX_BOLTS];         /* Blood Raven: cuándo cae cada rayo (0 = libre) */
    float bolt_x[BFX_BOLTS], bolt_y[BFX_BOLTS]; /* y dónde */
    int   zone, who;
} BossFx;

static void cairn_portal_open(int zone); /* game.c: abre el portal a Tristram */

static BossFx   s_bfx[BFX_MAX];
static int      s_flash;      /* ticks que le quedan al destello de un rayo */
static uint32_t s_bfx_rng = 12345u; /* aparte del azar del juego: no cambia las partidas */

static float bfx_rand(void)
{
    s_bfx_rng = s_bfx_rng * 1664525u + 1013904223u;
    return (float)((s_bfx_rng >> 8) & 0xFFFF) / 65536.0f;
}

static void bossfx_clear(void)
{
    memset(s_bfx, 0, sizeof s_bfx);
    s_flash = 0;
    s_bfx_rng = 12345u; /* (cada partida igual: en red, las dos máquinas tiran los mismos rayos) */
}

static Missile *bfx_effect(const char *name, float x, float y, int chain)
{
    Missile *m = missiles_effect(name, x, y);
    if (m && chain)
        m->flags |= MF_CHAIN;
    return m;
}

/* Las paredes de fuego: salen del cuerpo hacia afuera, en direcciones al azar */
static void bfx_rays(BossFx *fx)
{
    float turn = bfx_rand() * 6.2832f;
    for (int k = 0; k < BFX_RAYS; k++) {
        float a = turn + (float)k * 6.2832f / BFX_RAYS;
        fx->ray_x[k] = fx->x;
        fx->ray_y[k] = fx->y;
        fx->ray_dx[k] = cosf(a) * BFX_RAY_SPEED;
        fx->ray_dy[k] = sinf(a) * BFX_RAY_SPEED;
        fx->ray_on[k] = 1;
    }
    fx->ray_age = 0;
}

static void bossfx_start(int kind, float x, float y)
{
    BossFx *fx = NULL;
    for (int i = 0; i < BFX_MAX && !fx; i++)
        if (s_bfx[i].kind == BFX_NONE)
            fx = &s_bfx[i];
    if (!fx)
        return;
    memset(fx, 0, sizeof *fx);
    fx->kind = kind;
    fx->x = x;
    fx->y = y;
    if (kind == BFX_CAIRN) {
        audio_play_name("event_thunder_1", AUDIO_NOPOS, AUDIO_NOPOS);
        s_flash = 4;
    } else if (kind == BFX_DEN) {
        /* (los haces aparecen en bfx_den_tick) */
    } else if (kind == BFX_RAVEN) {
        bfx_effect("brdeathsmokes1", x, y, 1);
        bfx_effect("brdeathspirits1", x, y, 1);
        audio_play_name("event_thunder_1", AUDIO_NOPOS, AUDIO_NOPOS);
        s_flash = 4; /* los truenos destellan más */
    } else {
        bfx_rays(fx);
    }
}

/* ¿Lo ve alguno de los jugadores? (para el destello) */
static int bfx_seen(const BossFx *fx)
{
    for (int i = 0; i < s_num_players; i++)
        if (fabsf(s_pl[i].x - fx->x) < 30.0f && fabsf(s_pl[i].y - fx->y) < 30.0f)
            return 1;
    return 0;
}

static void bfx_raven_tick(BossFx *fx)
{
    /* Un rayo cada 4 o 5 ticks al principio, después cada vez menos: sale del
       cuerpo hacia cualquier lado y cae donde termina, a sus 20 cuadros o
       donde choca con una pared (uno de cada cuatro, enseguida: sobre el
       cuerpo) */
    int chance = fx->age < 200 ? 24 : fx->age < 320 ? 12 : 0;
    if ((int)(bfx_rand() * 100.0f) < chance) {
        int k = 0;
        while (k < BFX_BOLTS && fx->bolt_at[k])
            k++;
        if (k < BFX_BOLTS) {
            float a = bfx_rand() * 6.2832f, near = bfx_rand();
            float r = near < 0.25f ? near * 4.0f : 3.0f + bfx_rand() * (BFX_BOLT_SPEED * 20.0f - 3.0f);
            float ca = cosf(a), sa = sinf(a), d = 0.0f;
            while (d + 0.5f <= r &&
                   !map_blocked(&s_map, (int)floorf(fx->x + ca * (d + 0.5f)), (int)floorf(fx->y + sa * (d + 0.5f))))
                d += 0.5f;
            Missile *b = missiles_spawn("brdeathlightningbolt", fx->x, fx->y, ca, sa);
            if (b) { /* no se ve: alumbra al pasar */
                b->flags |= MF_NOHIT;
                b->owner = 0;
                b->life = (int)(d / BFX_BOLT_SPEED) + 1;
            }
            fx->bolt_x[k] = fx->x + ca * d;
            fx->bolt_y[k] = fx->y + sa * d;
            fx->bolt_at[k] = fx->age + (int)(d / BFX_BOLT_SPEED) + 1;
        }
    }
    for (int k = 0; k < BFX_BOLTS; k++) {
        if (!fx->bolt_at[k] || fx->age < fx->bolt_at[k])
            continue;
        fx->bolt_at[k] = 0;
        bfx_effect("brdeathlightninghit", fx->bolt_x[k], fx->bolt_y[k], 0); /* (el chasquido es el del rayo al salir: su TravelSound) */
        if (bfx_seen(fx) && bfx_rand() < 0.25f && s_flash < 2)
            s_flash = 2; /* uno de cada cuatro rayos destella apenas */
    }
    if (fx->age == 110 || fx->age == 230) {
        audio_play_name(fx->age == 110 ? "event_thunder_2" : "event_thunder_3", AUDIO_NOPOS, AUDIO_NOPOS);
        if (bfx_seen(fx))
            s_flash = 4;
    }
}

static void bfx_andy_tick(BossFx *fx)
{
    /* Columnas de fuego sobre el cuerpo (suena una de cada tres, si no
       se encimaban demasiados fuegos) */
    if (fx->age % 12 == 0 && fx->age < 290) {
        float cx = fx->x + (bfx_rand() - 0.5f) * 2.0f, cy = fx->y + (bfx_rand() - 0.5f) * 2.0f;
        Missile *base = bfx_effect("andycolumnfirebase", cx, cy, 0);
        Missile *col = bfx_effect("andycolumnfire", cx, cy, 0);
        if (base && (fx->age / 12) % 3 != 0)
            audio_stop(base->sound);
        if (col)
            audio_stop(col->sound);
    }
    if (fx->age == 295)
        audio_play_name("andariel_fire_end", fx->x, fx->y);
    /* Tres tandas de paredes de fuego */
    if (fx->age == 120 || fx->age == 240)
        bfx_rays(fx);
    if (fx->ray_age < BFX_RAY_TICKS) {
        for (int k = 0; k < BFX_RAYS; k++) {
            if (!fx->ray_on[k])
                continue;
            fx->ray_x[k] += fx->ray_dx[k];
            fx->ray_y[k] += fx->ray_dy[k];
            if (map_blocked(&s_map, (int)floorf(fx->ray_x[k]), (int)floorf(fx->ray_y[k])))
                fx->ray_on[k] = 0;
            else if (fx->ray_age % 4 == 0)
                bfx_effect("andyfirewall", fx->ray_x[k], fx->ray_y[k], 0);
        }
        fx->ray_age++;
    }
    /* Piedras que caen del techo y se rompen contra el piso */
    if (fx->age % 7 == 0 && fx->age < 300) {
        static const char *const k_chunks[3] = { "andyfallingdebris1", "andyfallingdebris2", "andyfallingdebris3" };
        float a = bfx_rand() * 6.2832f, r = 2.0f + bfx_rand() * 7.0f;
        float dx = fx->x + cosf(a) * r, dy = fx->y + sinf(a) * r;
        if (!map_blocked(&s_map, (int)floorf(dx), (int)floorf(dy))) {
            Missile *m = bfx_effect(k_chunks[(int)(bfx_rand() * 3.0f) % 3], dx, dy, 1);
            if (m) { /* cae 200 píxeles en 25 ticks (CltParam1 y 2) */
                m->z = 200.0f;
                m->vz = 8.0f;
                m->life = 1000;
            }
        }
    }
}

/* Un rayo grande que cae (cairn stones bolt; con su chasquido si sound) */
static void bfx_bolt(const BossFx *fx, float x, float y, int sound)
{
    Missile *m = bfx_effect("cairn stones bolt", x, y, 0);
    if (m) {
        m->glow = 1;
        if (!sound)
            audio_stop(m->sound);
    }
    if (sound && bfx_seen(fx) && s_flash < 2 && bfx_rand() < 0.5f)
        s_flash = 2;
}

/* Un rayo que corre por el piso de (x0, y0) a (x1, y1) (cairnstonesground:
   sale de al lado de la piedra y llega a la otra) */
static void bfx_ground(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
    if (d < 3.0f)
        return;
    dx /= d;
    dy /= d;
    Missile *g = missiles_spawn("cairnstonesground", x0 + dx, y0 + dy, dx, dy);
    if (g) {
        g->flags |= MF_NOHIT;
        g->owner = 0;
        g->glow = 1;
        g->life = (int)((d - 2.0f) / BFX_GROUND_SPEED) + 1;
    }
}

static void bfx_cairn_tick(BossFx *fx)
{
    int t = fx->age;
    /* Los rayos por el piso: de cada piedra a la segunda de al lado, la
       estrella del pentagrama (con uno grande sobre una de las piedras),
       mientras se oscurece y hasta que se abre el portal en su centro */
    if (t >= 4 && t < BFX_CAIRN_OPEN - 10 && (t - 4) % 26 == 0) {
        for (int k = 0; k < 5; k++) {
            int a = fx->ring[k], b = fx->ring[(k + 2) % 5];
            bfx_ground(fx->stone_x[a], fx->stone_y[a], fx->stone_x[b], fx->stone_y[b]);
        }
        int k = fx->ring[((t - 4) / 26) % 5];
        bfx_bolt(fx, fx->stone_x[k], fx->stone_y[k], 1);
    }
    /* Los rayos grandes que caen por todos lados, cada 2 cuadros (donde se
       puede pisar: CreateMissileWithCollisionCheck) */
    if (t >= BFX_CAIRN_FADE && t < BFX_CAIRN_TIME - BFX_CAIRN_FADE && t % 2 == 0)
        for (int tries = 0; tries < 4; tries++) {
            float a = bfx_rand() * 6.2832f, r = sqrtf(bfx_rand()) * BFX_CAIRN_SKY;
            float x = fx->x + cosf(a) * r, y = fx->y + sinf(a) * r;
            if (!map_blocked(&s_map, (int)floorf(x), (int)floorf(y))) {
                bfx_bolt(fx, x, y, t % 8 == 0);
                break;
            }
        }
    if (t == BFX_CAIRN_FADE || t == 120 || t == 210)
        audio_play_name(t == 120 ? "event_thunder_3" : "event_thunder_2", AUDIO_NOPOS, AUDIO_NOPOS);
    if (t == BFX_CAIRN_OPEN) {
        bfx_bolt(fx, fx->x, fx->y, 1); /* el último, en el centro */
        if (bfx_seen(fx))
            s_flash = 4;
        if (fx->zone != -2) /* (-2: la prueba D2_FXTEST, sin portal) */
            cairn_portal_open(fx->zone);
    }
}

/* Los rayos de las Piedras Cairn, con las cinco piedras y el centro */
static void bossfx_cairn(const float *sx, const float *sy, float cx, float cy, int zone)
{
    bossfx_start(BFX_CAIRN, cx, cy);
    for (int i = 0; i < BFX_MAX; i++)
        if (s_bfx[i].kind == BFX_CAIRN && s_bfx[i].age == 0) {
            BossFx *fx = &s_bfx[i];
            memcpy(fx->stone_x, sx, sizeof fx->stone_x);
            memcpy(fx->stone_y, sy, sizeof fx->stone_y);
            fx->zone = zone;
            /* las piedras en orden alrededor del centro (los rayos de una a la de al lado) */
            float ang[5];
            for (int k = 0; k < 5; k++) {
                ang[k] = atan2f(sy[k] - cy, sx[k] - cx);
                fx->ring[k] = k;
            }
            for (int a = 1; a < 5; a++)
                for (int b = a; b > 0 && ang[fx->ring[b]] < ang[fx->ring[b - 1]]; b--) {
                    int tmp = fx->ring[b];
                    fx->ring[b] = fx->ring[b - 1];
                    fx->ring[b - 1] = tmp;
                }
            return;
        }
    if (zone != -2)
        cairn_portal_open(zone); /* sin lugar para el efecto: igual se abre */
}

/* Que un efecto de 4 direcciones se dibuje con la d (dcc_dir: los sectores
   impares de 8; los efectos no se mueven, vx y vy son solo para el dibujo) */
static void bfx_face4(Missile *m, int d)
{
    float a = (float)(2 * d + 1) * 6.2832f / 8.0f, sx = -sinf(a), sy = cosf(a);
    m->vx = (sx + 2.0f * sy) * 0.5f;
    m->vy = (2.0f * sy - sx) * 0.5f;
}

/* Los haces de luz de la Guarida del Mal: uno cada 5 ticks alrededor de
   quien la limpió, donde se puede pisar */
static void bfx_den_tick(BossFx *fx)
{
    if (fx->age % BFX_DEN_EVERY != 0)
        return;
    const Player *p = &s_pl[fx->who];
    for (int tries = 0; tries < 6; tries++) {
        float x = p->x + (bfx_rand() - 0.5f) * 22.0f, y = p->y + (bfx_rand() - 0.5f) * 22.0f;
        if (map_blocked(&s_map, (int)floorf(x), (int)floorf(y)))
            continue;
        Missile *m = bfx_effect("denofevillight", x, y, 0);
        if (m) {
            m->unlit = 1;
            bfx_face4(m, (int)(bfx_rand() * 4.0f) & 3);
            m->age = (int)(bfx_rand() * 21.0f); /* (cada uno en otro momento de su titilar) */
        }
        break;
    }
}

static void bossfx_den(int who)
{
    bossfx_start(BFX_DEN, s_pl[who].x, s_pl[who].y);
    for (int i = 0; i < BFX_MAX; i++)
        if (s_bfx[i].kind == BFX_DEN && s_bfx[i].age == 0)
            s_bfx[i].who = who;
}

static void bossfx_tick(void)
{
    if (s_flash > 0)
        s_flash--;
    for (int i = 0; i < BFX_MAX; i++) {
        BossFx *fx = &s_bfx[i];
        if (fx->kind == BFX_RAVEN)
            bfx_raven_tick(fx);
        else if (fx->kind == BFX_ANDY)
            bfx_andy_tick(fx);
        else if (fx->kind == BFX_CAIRN)
            bfx_cairn_tick(fx);
        else if (fx->kind == BFX_DEN)
            bfx_den_tick(fx);
        else
            continue;
        int time = fx->kind == BFX_RAVEN ? BFX_RAVEN_TIME : fx->kind == BFX_ANDY ? BFX_ANDY_TIME
                 : fx->kind == BFX_DEN ? BFX_DEN_TIME : BFX_CAIRN_TIME;
        if (++fx->age >= time)
            fx->kind = BFX_NONE;
    }
}

int game_debug_fx(int *kind)
{
    for (int i = 0; i < BFX_MAX; i++)
        if (s_bfx[i].kind != BFX_NONE) {
            *kind = s_bfx[i].kind;
            return s_bfx[i].age;
        }
    *kind = 0;
    return -1;
}

/* ¿Están cayendo los rayos de las Piedras Cairn? (entonces los rayos se
   dibujan después de oscurecer la escena: bossfx_shade) */
static int bossfx_glow_late(void)
{
    for (int i = 0; i < BFX_MAX; i++)
        if (s_bfx[i].kind == BFX_CAIRN)
            return 1;
    return 0;
}

/* Cuánto se oscurece la escena (de 0 a BFX_CAIRN_DARK niveles de luz), si
   el jugador owner está cerca: en 40 cuadros, y al final se aclara en otros
   40; el destello de un rayo la aclara un momento */
static int bossfx_dark(int owner)
{
    int dark = 0;
    for (int i = 0; i < BFX_MAX; i++) {
        const BossFx *fx = &s_bfx[i];
        if (fx->kind != BFX_CAIRN || owner < 0 || fabsf(s_pl[owner].x - fx->x) > 40.0f ||
            fabsf(s_pl[owner].y - fx->y) > 40.0f)
            continue;
        int t = fx->age, f = t < BFX_CAIRN_FADE ? t : t > BFX_CAIRN_TIME - BFX_CAIRN_FADE ? BFX_CAIRN_TIME - t : BFX_CAIRN_FADE;
        int d = BFX_CAIRN_DARK * f / BFX_CAIRN_FADE;
        if (d > dark)
            dark = d;
    }
    if (dark > 0 && s_flash > 0)
        dark -= s_flash * 3;
    return dark < 0 ? 0 : dark;
}

/* Después de dibujar el mundo de una vista (y su oscuridad): la escena
   oscurecida y encima, sin oscurecer, los rayos */
static void bossfx_shade(float cam_x, float cam_y, float alpha, int owner)
{
    if (!bossfx_glow_late())
        return;
    int dark = bossfx_dark(owner);
    if (dark > 0) /* (el mundo: la barra de abajo se dibuja encima) */
        fb_shade_rect(0, 0, SCREEN_W, WORLD_BOTTOM == SCREEN_H - 1 ? SCREEN_H : SCREEN_H - 49, LIGHT_LEVELS - 1 - dark);
    int ox = SCREEN_W / 2 - (int)lroundf(cam_x), oy = SCREEN_H / 2 - (int)lroundf(cam_y);
    for (int i = 0; i < missiles_capacity(); i++) {
        const Missile *mi = missiles_get(i);
        if (!mi || !mi->glow)
            continue;
        float mx, my, sx, sy;
        missiles_position(i, alpha, &mx, &my);
        iso_to_screen(mx, my, &sx, &sy);
        missiles_draw(i, ox + (int)lroundf(sx), oy + (int)lroundf(sy));
    }
}

/* El destello de los rayos (va debajo de la barra de control; con la escena
   oscurecida, en cambio, la aclara: bossfx_dark) */
static void bossfx_draw(void)
{
    if (s_flash > 0 && !bossfx_glow_late())
        fb_brighten_rect(0, 0, SCREEN_W, SCREEN_H, s_flash > 2 ? 2 : 1);
}

/* El fuego de Andariel ilumina la oscuridad de las catacumbas: dónde está
   (en subtiles), o 0 si no hay */
static int bossfx_light(float *x, float *y)
{
    for (int i = 0; i < BFX_MAX; i++)
        if (s_bfx[i].kind == BFX_ANDY && s_bfx[i].age < 320) {
            *x = s_bfx[i].x;
            *y = s_bfx[i].y;
            return 1;
        }
    return 0;
}
