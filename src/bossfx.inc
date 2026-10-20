/* ---- La muerte de los jefes que en el original tiene función propia ----
   (se incluye desde game.c)

   Blood Raven (brdeathcontrol de missiles.txt): unos 15 segundos de rayos
   que salen de alrededor (brdeathlightningbolt: no se ven, solo alumbran),
   van hacia el cuerpo y caen sobre él (brdeathlightninghit), con su
   chasquido y truenos, mientras suben el humo y los espíritus
   (brdeathsmoke* y brdeathspirit*, encadenados: empieza, sigue y termina,
   con los sonidos de la niebla).
   Andariel (andycontrol0): unos 14 segundos de columnas de fuego sobre el
   cuerpo, paredes de fuego que salen hacia afuera y piedras que caen del
   techo de las catacumbas.
   Los demás jefes del Acto 1 mueren sin nada especial, como en el original.

   Y las Piedras Cairn (cairnstones): al tocar la quinta en orden caen rayos
   grandes sobre las piedras (cairn stones bolt) y corren rayos por el piso
   hacia el centro (cairnstonesground, que suelta lightninghit); recién
   después se abre el portal rojo a Tristram. */

enum { BFX_NONE, BFX_RAVEN, BFX_ANDY, BFX_CAIRN };

#define BFX_MAX        3
#define BFX_CAIRN_TIME 150 /* los rayos de las piedras (el original sigue con luz hasta 300) */
#define BFX_CAIRN_OPEN 60  /* a los 2,4 s se abre el portal */
#define BFX_RAYS       6
#define BFX_RAVEN_TIME 375 /* Range de brdeathcontrol */
#define BFX_ANDY_TIME  350 /* Range de andycontrol0 */
#define BFX_RAY_SPEED  (8.0f * 5.0f / 6.0f / TICK_RATE) /* Vel de andyfirewallmaker */
#define BFX_RAY_TICKS  50                                /* su Range */
#define BFX_BOLTS      16 /* los rayos de Blood Raven que van hacia el cuerpo */
#define BFX_BOLT_SPEED (32.0f * 5.0f / 6.0f / TICK_RATE) /* Vel de brdeathlightningbolt */

typedef struct {
    int   kind, age;
    float x, y;
    float ray_x[BFX_RAYS], ray_y[BFX_RAYS], ray_dx[BFX_RAYS], ray_dy[BFX_RAYS];
    int   ray_on[BFX_RAYS], ray_age;
    float stone_x[5], stone_y[5]; /* las Piedras Cairn */
    int   bolt_at[BFX_BOLTS];         /* Blood Raven: cuándo llega cada rayo al cuerpo (0 = libre) */
    int   zone;
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
    /* Un rayo cada 5 o 6 ticks al principio, después cada vez menos: sale de
       un lugar al azar alrededor y va hacia el cuerpo (como el original, que
       lo reapunta cada 3 cuadros); al llegar cae sobre el cuerpo */
    int chance = fx->age < 200 ? 18 : fx->age < 320 ? 9 : 0;
    if ((int)(bfx_rand() * 100.0f) < chance) {
        int k = 0;
        while (k < BFX_BOLTS && fx->bolt_at[k])
            k++;
        if (k < BFX_BOLTS) {
            float a = bfx_rand() * 6.2832f, r = 3.0f + bfx_rand() * 5.0f;
            float sx = fx->x + cosf(a) * r, sy = fx->y + sinf(a) * r;
            Missile *b = missiles_spawn("brdeathlightningbolt", sx, sy, fx->x - sx, fx->y - sy);
            if (b) { /* no se ve: alumbra al pasar */
                b->flags |= MF_NOHIT;
                b->owner = 0;
                b->life = (int)(r / BFX_BOLT_SPEED) + 1;
            }
            fx->bolt_at[k] = fx->age + (int)(r / BFX_BOLT_SPEED) + 1;
        }
    }
    for (int k = 0; k < BFX_BOLTS; k++) {
        if (!fx->bolt_at[k] || fx->age < fx->bolt_at[k])
            continue;
        fx->bolt_at[k] = 0;
        float sx = fx->x + (bfx_rand() - 0.5f) * 1.2f, sy = fx->y + (bfx_rand() - 0.5f) * 1.2f;
        bfx_effect("brdeathlightninghit", sx, sy, 0); /* (el chasquido es el del rayo al salir: su TravelSound) */
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

static void bfx_cairn_tick(BossFx *fx)
{
    /* Un rayo grande sobre una piedra al azar cada 5 ticks */
    if (fx->age % 5 == 0 && fx->age < 120) {
        int k = (int)(bfx_rand() * 5.0f) % 5;
        bfx_effect("cairn stones bolt", fx->stone_x[k], fx->stone_y[k], 0);
        if (bfx_seen(fx) && fx->age % 15 == 0)
            s_flash = 2;
    }
    /* Rayos por el piso de una piedra hacia el centro */
    if (fx->age % 7 == 3 && fx->age < 120) {
        int k = (int)(bfx_rand() * 5.0f) % 5;
        float t = 0.2f + bfx_rand() * 0.6f;
        bfx_effect("lightninghit", fx->stone_x[k] + (fx->x - fx->stone_x[k]) * t,
                   fx->stone_y[k] + (fx->y - fx->stone_y[k]) * t, 0);
    }
    if (fx->age == 40)
        audio_play_name("event_thunder_2", AUDIO_NOPOS, AUDIO_NOPOS);
    if (fx->age == BFX_CAIRN_OPEN) {
        bfx_effect("cairn stones bolt", fx->x, fx->y, 0); /* el último, en el centro */
        if (bfx_seen(fx))
            s_flash = 4;
        cairn_portal_open(fx->zone);
    }
}

/* Los rayos de las Piedras Cairn, con las cinco piedras y el centro */
static void bossfx_cairn(const float *sx, const float *sy, float cx, float cy, int zone)
{
    bossfx_start(BFX_CAIRN, cx, cy);
    for (int i = 0; i < BFX_MAX; i++)
        if (s_bfx[i].kind == BFX_CAIRN && s_bfx[i].age == 0) {
            memcpy(s_bfx[i].stone_x, sx, sizeof s_bfx[i].stone_x);
            memcpy(s_bfx[i].stone_y, sy, sizeof s_bfx[i].stone_y);
            s_bfx[i].zone = zone;
            return;
        }
    cairn_portal_open(zone); /* sin lugar para el efecto: igual se abre */
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
        else
            continue;
        int time = fx->kind == BFX_RAVEN ? BFX_RAVEN_TIME : fx->kind == BFX_ANDY ? BFX_ANDY_TIME : BFX_CAIRN_TIME;
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

/* El destello de los rayos (va debajo de la barra de control) */
static void bossfx_draw(void)
{
    if (s_flash > 0)
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
