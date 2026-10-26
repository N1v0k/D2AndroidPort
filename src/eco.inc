/* ---- El Eco del Odio (incluido en game.c, después de endgame.inc) ----

   Un modo aparte de la historia (docs/plan-eco-del-odio.md), con la idea
   del "Echoing Hatred" de Diablo IV: oleadas sin fin en una arena cerrada
   elegida al azar, cada una más difícil, y los jefes de la historia cada
   cinco. Se entra desde el menú principal con un personaje que ya existe:
   la experiencia, el oro y los objetos vuelven a su partida, y lo de la
   historia de las tres dificultades queda como estaba (load_character y
   save_character lo leen y lo escriben tal cual con s_eco).

   - La arena: uno de los niveles de k_eco_arenas del mundo del Acto 5
     (world_set_arena: también Uber Tristram y la Colossal Summit fuera de
     Infierno), sin sus monstruos ni sus salidas. Se empieza en el lugar más
     abierto del pedazo más grande que se camina.
   - La oleada N: monstruos del nivel del personaje, que suben medio nivel
     por oleada (pasado el 99, vida y daño de más), de los niveles del acto
     del tema (de a diez oleadas por acto: ECO_ACT_WAVES). Salen en manadas
     fuera de la pantalla y cazan (units_set_hunt: van por el mapa de
     pasos). Cumplida la cuota no salen más, y se pasa cuando no queda
     ninguno vivo (26/10/2026, el usuario: "la oleada no termina hasta que
     matás a todos los monstruos").
   - Cada diez, un jefe de acto (Andariel... Baal; en la vuelta siguiente
     los del evento Pandemonium), del nivel de la oleada + 3; en la sexta
     de cada diez, uno con nombre propio de ese acto. Eran cinco por acto
     (26/10/2026, el usuario: "son pocas oleadas y no alcanza el tiempo
     para subir"): la dificultad (la cuota, las manadas, los campeones, el
     nivel de los monstruos) sube por acto lo mismo que antes (eco_stage).
   - Entre oleadas, 8 segundos con la cuenta en pantalla (para repartir puntos
     y equiparse tranquilo) y al terminar se borra todo lo que quedó en el
     piso.
   - Pociones: un 35 % más que en la historia (ECO_POTION_BONUS).
   - El desborde: con la arena llena (s_er.cap vivos) diez segundos, se
     termina. También al morir (los dos, con dos jugadores).
   - El ritmo según el nivel de los personajes (eco_pace): un personaje
     recién hecho mata despacio, así que en los niveles bajos las manadas
     salen más espaciadas, el desborde aguanta más y el respiro es más
     largo (de 5 en 5 niveles hasta el 20 y de 10 en 10 hasta el 40); del
     41 al 60 es lo de siempre y después se aprieta de 20 en 20 (para que
     alguien pueda jugar solo el Eco, sin la historia). En los niveles
     bajos, además, los monstruos suben más despacio (uno cada tres
     oleadas hasta el 15, cada dos hasta el 30; nunca más de uno por
     debajo de los personajes), los jefes tienen menos niveles de más y
     salen menos manadas de campeones.
   - Lo que cae (eco_loot y el cofre): de la clase de tesoro de su grupo
     que le toca al nivel de los personajes + 2, y de ese nivel como mucho
     (los monstruos del Acto 5 de una oleada baja no sueltan lo del 5).
   - Al final: el cofre del Eco (más objetos cuanto más lejos se llegó) y un
     portal que vuelve al menú; el récord de cada personaje se guarda. */

#define ECO_ARENAS  4
#define ECO_BREAK   (8 * TICK_RATE)  /* el respiro entre oleadas (con la cuenta; al final se borra el piso) */
#define ECO_ACT_WAVES  10 /* las oleadas de cada acto: la última con su jefe */
#define ECO_NAMED_WAVE 6  /* y la del jefe con nombre propio, dentro de las del acto */
#define ECO_POTION_BONUS 35 /* % más pociones que en la historia */
#define ECO_STUCK (120 * TICK_RATE) /* cumplida la cuota, si en esto no muere ninguno (trabado donde no se llega), se pasa */
#define ECO_OVERRUN (10 * TICK_RATE) /* lo que se aguanta la arena llena */
#define ECO_FLOW    10               /* cada cuántos ticks se rehace el mapa de pasos */
#define ECO_NOSTEP  0xffff
#define ECO_KINDS   4
#define ECO_BOSS_LEVELS 3 /* los jefes, sobre la oleada (como los del original sobre su área: UNIQUE_LEVELS) */
/* El ritmo por escalones de nivel (en % de los tiempos: 100 = lo de siempre): de 5 en 5 hasta el 20,
   de 10 en 10 hasta el 40 y de 20 en 20 después */
static const struct { int upto, pace; } k_eco_pace[] = {
    { 5, 250 }, { 10, 220 }, { 15, 190 }, { 20, 165 }, { 30, 140 }, { 40, 120 }, { 60, 100 }, { 80, 85 }, { 99, 70 },
};
#define ECO_PACE_BIG  85  /* con el ritmo en 85 o menos (desde el nivel 61), manadas de uno más */

/* Las arenas (Id de levels.txt): el Trono de la Destrucción, la Cumbre del Arreat, Uber Tristram y la Colossal Summit */
static const int k_eco_arenas[ECO_ARENAS] = { 131, 120, 136, 137 };

/* Los jefes con nombre propio de cada acto (superuniques.txt; los que empiezan con '=' son de monstats.txt) */
static const char *const k_eco_named[5][16] = {
    { "Corpsefire", "Bishibosh", "Bonebreak", "Coldcrow", "Rakanishu", "Treehead WoodFist", "Griswold", "The Countess",
      "Pitspawn Fouldog", "Flamespike the Crawler", "Boneash", "The Smith", "=bloodraven" },
    { "Radament", "Bloodwitch the Wild", "Fangskin", "Beetleburst", "Leatherarm", "Coldworm the Burrower", "Fire Eye",
      "Dark Elder", "The Summoner", "Ancient Kaa the Soulless" },
    { "Web Mage the Burning", "Witch Doctor Endugu", "Stormtree", "Sarina the Battlemaid", "Icehawk Riftwing",
      "Ismail Vilehand", "Geleb Flamefinger", "Bremm Sparkfist", "Toorc Icefist", "Wyand Voidfinger", "Maffer Dragonhand" },
    { "Winged Death", "The Tormentor", "Taintbreeder", "Riftwraith the Cannibal", "Infector of Souls", "Lord De Seis",
      "Grand Vizier of Chaos", "The Feature Creep", "=izual" },
    { "Siege Boss", "Axe Dweller", "Bonesaw Breaker", "Dac Farren", "Megaflow Rectifier", "Eyeback Unleashed", "Pindleskin",
      "Snapchip Shatter", "Anodized Elite", "Vinvear Molech", "Sharp Tooth Sayer", "Magma Torquer", "Blaze Ripper",
      "Frozenstein", "Nihlathak Boss" },
};

enum { EP_OFF, EP_START, EP_WAVE, EP_BREAK, EP_END, EP_LOOT };
enum { EE_NONE, EE_DIED, EE_OVERRUN, EE_QUIT };

typedef struct {
    int          phase, ticks;
    int          arena, zone;
    const Level *lv;
    int          x0, y0, w, h; /* el rectángulo del nivel, en subtiles */
    uint8_t     *walk;         /* 1 = del pedazo más grande que se camina */
    uint8_t     *open;         /* subtiles hasta lo que no se camina (hasta 255) */
    uint16_t    *steps;        /* los pasos hasta un jugador (units_set_flow) */
    int         *queue;
    int          cells;        /* cuántos se caminan */
    float        cx, cy;       /* el lugar más abierto: el inicio, el pozo y el cofre */
    int          base;         /* el nivel de los personajes (el promedio) */
    int          wave, mlvl, quota, deaths0;
    int          act;          /* el tema de la oleada */
    const char  *kinds[ECO_KINDS];
    int          nkinds;
    int          boss[4], nboss; /* los jefes que hay que matar para pasar */
    int          boss_kind;      /* 0 ninguno, 1 de acto, 2 con nombre propio */
    int          boss_due;       /* el tick de la oleada en que salen (-1 = ya salieron) */
    int          spawn_cd, alive, cap;
    int          pace;           /* el ritmo según el nivel de los personajes (eco_set_pace), en % de los tiempos */
    int          grow12;         /* lo que subió el nivel de los monstruos desde el primero, en doceavos de nivel */
    int          over;           /* ticks con la arena llena */
    int          last_kill;      /* el tick de la oleada en que murió el último (la cuota cumplida, ECO_STUCK) */
    int          kills_seen;
    int          dead_ticks;     /* ticks con todos muertos */
    int          end;            /* EE_*: por qué terminó */
    int          best;           /* la oleada a la que se llegó */
    int          record;         /* algún personaje superó su récord */
    int          chest, shrines[2], well;
    float        px, py;         /* el portal de vuelta */
    int          exited;         /* se usó el portal (la prueba) */
} EcoRun;

static EcoRun s_er = { .zone = -1, .chest = -1, .shrines = { -1, -1 }, .well = -1 };

/* El ritmo de un nivel: los tiempos (entre manadas, el desborde y el respiro) en % de los de siempre;
   abajo más espaciado (un personaje de nivel bajo mata despacio), arriba más desafío */
static int eco_pace_for(int lvl)
{
    int n = (int)(sizeof k_eco_pace / sizeof k_eco_pace[0]), k = 0;
    while (k < n - 1 && lvl > k_eco_pace[k].upto)
        k++;
    return k_eco_pace[k].pace;
}

/* La etapa de la oleada w: la oleada que era con cinco por acto (la 1 y la 2 son la 1, ... la 9 y la 10 la 5): la
   dificultad (la cuota, las manadas, los campeones) sube por acto lo mismo que antes, en el doble de oleadas */
static int eco_stage(int w)
{
    return w > 0 ? (w - 1) * 5 / ECO_ACT_WAVES + 1 : 1;
}

/* El nivel de los personajes ahora (el promedio: suben jugando) */
static int eco_party_level(void)
{
    int sum = 0, n = s_num_players > 0 ? s_num_players : 1;
    for (int i = 0; i < n; i++)
        sum += s_pl[i].level_num;
    return sum / n > 0 ? sum / n : 1;
}

/* El de ahora, según el nivel de los personajes (se mira en cada oleada). D2_ECO_PACE=N (pruebas) lo fija */
static void eco_set_pace(void)
{
    int lvl = getenv("D2_ECO_LEVEL") ? atoi(getenv("D2_ECO_LEVEL")) : eco_party_level();
    s_er.pace = getenv("D2_ECO_PACE") ? atoi(getenv("D2_ECO_PACE")) : eco_pace_for(lvl);
    s_er.pace = s_er.pace < 10 ? 10 : s_er.pace;
}

/* Con personajes de nivel bajo los monstruos tienen menos vida y daño: -40 % con el ritmo en 250 (nivel 1
   a 5), menos de a poco hasta nada en el 100 (nivel 41) */
static int eco_soft_pct(void)
{
    return s_er.pace > 100 ? -(s_er.pace - 100) * 4 / 15 : 0;
}

/* Los niveles de más de los jefes: menos en los niveles bajos */
static int eco_boss_levels(void)
{
    return s_er.pace >= 190 ? 0 : s_er.pace >= 140 ? 1 : s_er.pace > 100 ? 2 : ECO_BOSS_LEVELS;
}

/* Lo que cae, acorde a los personajes: hasta su nivel + 2, y la clase de tesoro de su grupo de ese nivel
   (como el original sube la de un monstruo por su nivel, pero desde la primera del grupo) */
static int eco_loot_level(void)
{
    int l = eco_party_level() + 2;
    return l > 99 ? 99 : l;
}

static void eco_loot(const char **tc, int *lvl)
{
    items_set_potion_bonus(ECO_POTION_BONUS); /* (drop_loot lo vuelve a 0) */
    int want = eco_loot_level();
    *tc = items_tc_at_level(*tc, want);
    *lvl = *lvl < want ? *lvl : want;
}

/* Lo que aguanta la arena llena (como mucho dos veces y media lo de siempre) */
static int eco_overrun_ticks(void)
{
    return ECO_OVERRUN * (s_er.pace < 250 ? s_er.pace : 250) / 100;
}

static int eco_debug(void)
{
    return s_debug || getenv("D2_ECO_DEBUG") != NULL;
}

static int eco_running(void)
{
    return s_eco && s_er.phase >= EP_START && s_er.phase <= EP_BREAK;
}

/* La dificultad de las tablas: la de las áreas del original con ese nivel (hasta 39 Normal, de 40 a 64 Pesadilla) */
static int eco_difficulty(void)
{
    int sum = 0, n = s_num_players > 0 ? s_num_players : 1;
    for (int i = 0; i < n; i++)
        sum += s_pl[i].level_num;
    if (getenv("D2_ECO_LEVEL"))
        sum = atoi(getenv("D2_ECO_LEVEL")) * n;
    int avg = sum / n;
    if (getenv("D2_ECO_DIFF"))
        return atoi(getenv("D2_ECO_DIFF"));
    return avg < 40 ? 0 : avg < 65 ? 1 : 2;
}

static void eco_free(void)
{
    units_set_flow(NULL, 0, 0, 0, 0);
    units_set_hunt(-1, 0, 0);
    free(s_er.walk);
    free(s_er.open);
    free(s_er.steps);
    free(s_er.queue);
    s_er.walk = s_er.open = NULL;
    s_er.steps = NULL;
    s_er.queue = NULL;
}

/* Una partida de la historia: nada de la arena queda (ni el mapa de pasos ni los que cazan) */
static void eco_leave(void)
{
    eco_free();
    s_er.phase = EP_OFF;
    s_er.zone = -1;
}

static int eco_cell(float x, float y)
{
    int cx = (int)floorf(x) - s_er.x0, cy = (int)floorf(y) - s_er.y0;
    return cx >= 0 && cy >= 0 && cx < s_er.w && cy < s_er.h ? cy * s_er.w + cx : -1;
}

static int eco_walk(float x, float y)
{
    int c = eco_cell(x, y);
    return c >= 0 && s_er.walk[c];
}

/* El mapa de la arena: lo que se camina (el pedazo más grande, de a cuatro vecinos), lo lejos que
   está cada lugar de lo que no se camina, y el lugar más abierto (el más cercano al medio del pedazo
   si hay varios). 0 = bien. */
static int eco_map(void)
{
    const Level *lv = s_er.lv;
    s_er.x0 = lv->x0 * TILE_SUB;
    s_er.y0 = lv->y0 * TILE_SUB;
    s_er.w = (lv->x1 - lv->x0) * TILE_SUB;
    s_er.h = (lv->y1 - lv->y0) * TILE_SUB;
    int w = s_er.w, h = s_er.h, n = w * h;
    if (n <= 0)
        return -1;
    s_er.walk = calloc((size_t)n, 1);
    s_er.open = calloc((size_t)n, 1);
    s_er.steps = malloc((size_t)n * sizeof *s_er.steps);
    s_er.queue = malloc((size_t)n * sizeof *s_er.queue);
    uint8_t *label = calloc((size_t)n, 1);
    if (!s_er.walk || !s_er.open || !s_er.steps || !s_er.queue || !label) {
        free(label);
        return -1;
    }
    for (int c = 0; c < n; c++) {
        float x = (float)(s_er.x0 + c % w) + 0.5f, y = (float)(s_er.y0 + c / w) + 0.5f;
        label[c] = !blocked_body(x, y) && world_level_at(x, y) == lv; /* 1 = se camina, sin pedazo todavía */
    }
    static const int k_d4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int best = -1, best_n = 0, *q = s_er.queue;
    for (int c = 0; c < n; c++) {
        if (label[c] != 1)
            continue;
        int qh = 0, qt = 0;
        q[qt++] = c;
        label[c] = 2;
        while (qh < qt) {
            int e = q[qh++], ex = e % w, ey = e / w;
            for (int k = 0; k < 4; k++) {
                int nx = ex + k_d4[k][0], ny = ey + k_d4[k][1];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h || label[ny * w + nx] != 1)
                    continue;
                label[ny * w + nx] = 2;
                q[qt++] = ny * w + nx;
            }
        }
        if (qt > best_n) { /* el más grande, marcado con 3 (los otros quedan en 2) */
            if (best >= 0)
                for (int e = 0; e < n; e++)
                    if (label[e] == 3)
                        label[e] = 2;
            for (int k = 0; k < qt; k++)
                label[q[k]] = 3;
            best = c;
            best_n = qt;
        }
    }
    s_er.cells = best_n;
    for (int c = 0; c < n; c++)
        s_er.walk[c] = label[c] == 3;
    free(label);
    if (best_n < 400) /* (menos que un cuarto chico: algo salió mal) */
        return -1;
    /* Lo lejos de lo que no se camina: desde todos los bordes a la vez */
    int qh = 0, qt = 0;
    for (int c = 0; c < n; c++) {
        s_er.steps[c] = ECO_NOSTEP;
        if (!s_er.walk[c]) {
            s_er.steps[c] = 0;
            q[qt++] = c;
        }
    }
    while (qh < qt) {
        int e = q[qh++], ex = e % w, ey = e / w;
        for (int k = 0; k < 4; k++) {
            int nx = ex + k_d4[k][0], ny = ey + k_d4[k][1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || s_er.steps[ny * w + nx] != ECO_NOSTEP)
                continue;
            s_er.steps[ny * w + nx] = (uint16_t)(s_er.steps[e] + 1);
            q[qt++] = ny * w + nx;
        }
    }
    double mx = 0, my = 0;
    for (int c = 0; c < n; c++) {
        unsigned v = s_er.steps[c] == ECO_NOSTEP ? 255 : s_er.steps[c];
        s_er.open[c] = (uint8_t)(v > 255 ? 255 : v);
        if (s_er.walk[c]) {
            mx += c % w;
            my += c / w;
        }
    }
    mx /= best_n;
    my /= best_n;
    int top = 0;
    double top_d = 1e18;
    for (int c = 0; c < n; c++) {
        if (!s_er.walk[c] || s_er.open[c] < top)
            continue;
        double d = (c % w - mx) * (c % w - mx) + (c / w - my) * (c / w - my);
        if (s_er.open[c] > top || d < top_d) {
            top = s_er.open[c];
            top_d = d;
            s_er.cx = (float)(s_er.x0 + c % w) + 0.5f;
            s_er.cy = (float)(s_er.y0 + c / w) + 0.5f;
        }
    }
    if (eco_debug())
        printf("ECO: %s: %dx%d subtiles, %d se caminan, el lugar más abierto (%.0f, %.0f) a %d de las paredes\n", lv->name, w, h,
               best_n, s_er.cx, s_er.cy, top);
    int px, py; /* D2_ECO_PROBE="x,y": lo que hay alrededor de ese lugar ('#' no se camina, '.' sí, 'o' del pedazo) */
    if (getenv("D2_ECO_PROBE") && sscanf(getenv("D2_ECO_PROBE"), "%d,%d", &px, &py) == 2)
        for (int y = py - 12; y <= py + 12; y++) {
            char row[32];
            for (int x = px - 12; x <= px + 12; x++) {
                int c = eco_cell((float)x, (float)y);
                row[x - px + 12] = x == px && y == py ? '@' : c < 0 ? ' ' : s_er.walk[c] ? 'o' : blocked_body((float)x + 0.5f, (float)y + 0.5f) ? '#' : '.';
            }
            row[25] = '\0';
            printf("ECO: %5d %s\n", y, row);
        }
    return 0;
}

/* El mapa de pasos hasta los jugadores vivos (en diagonal solo si los dos de al lado se caminan) */
static void eco_steps(void)
{
    int w = s_er.w, h = s_er.h, n = w * h, qh = 0, qt = 0, *q = s_er.queue;
    for (int c = 0; c < n; c++)
        s_er.steps[c] = ECO_NOSTEP;
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        if (p->state >= PS_DYING)
            continue;
        int c = -1;
        for (int r = 0; r <= 2 && c < 0; r++) /* (si está justo en un borde, el de al lado) */
            for (int dy = -r; dy <= r && c < 0; dy++)
                for (int dx = -r; dx <= r && c < 0; dx++) {
                    int e = eco_cell(p->x + (float)dx, p->y + (float)dy);
                    if (e >= 0 && s_er.walk[e])
                        c = e;
                }
        if (c >= 0 && s_er.steps[c]) {
            s_er.steps[c] = 0;
            q[qt++] = c;
        }
    }
    static const int k_d8[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    while (qh < qt) {
        int c = q[qh++], x = c % w, y = c / w;
        unsigned v = s_er.steps[c] + 1u;
        for (int k = 0; k < 8; k++) {
            int nx = x + k_d8[k][0], ny = y + k_d8[k][1], nc = ny * w + nx;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || !s_er.walk[nc] || s_er.steps[nc] <= v)
                continue;
            if (k >= 4 && (!s_er.walk[y * w + nx] || !s_er.walk[ny * w + x]))
                continue;
            s_er.steps[nc] = (uint16_t)v;
            q[qt++] = nc;
        }
    }
}

static int eco_on_screen(float x, float y)
{
    for (int i = 0; i < s_num_players; i++) {
        float sx, sy;
        iso_to_screen(x - s_pl[i].x, y - s_pl[i].y, &sx, &sy);
        if (fabsf(sx) < SCREEN_W / 2 + 40 && fabsf(sy) < SCREEN_H / 2 + 40)
            return 1;
    }
    return 0;
}

static const Player *eco_alive_player(void)
{
    int alive[MAX_PLAYERS], n = 0;
    for (int i = 0; i < s_num_players; i++)
        if (s_pl[i].state < PS_DYING)
            alive[n++] = i;
    return n ? &s_pl[alive[rnd() % (uint32_t)n]] : NULL;
}

/* Un lugar para que salga una manada: a rmin..rmax de un jugador vivo, que se camine, con `open`
   de espacio y al que se llegue; primero fuera de la pantalla. Si no hay (la arena es chica), uno
   a más de 12 pasos. */
static int eco_spot(float rmin, float rmax, int open, float *ox, float *oy)
{
    for (int t = 0; t < 80; t++) {
        const Player *p = eco_alive_player();
        if (!p)
            return 0;
        float a = (float)(rnd() % 6283) / 1000.0f, r = rmin + (float)(rnd() % 1000) / 1000.0f * (rmax - rmin);
        float x = p->x + cosf(a) * r, y = p->y + sinf(a) * r;
        int c = eco_cell(x, y);
        if (c < 0 || !s_er.walk[c] || s_er.open[c] < open || s_er.steps[c] == ECO_NOSTEP || (t < 50 && eco_on_screen(x, y)))
            continue;
        *ox = floorf(x) + 0.5f;
        *oy = floorf(y) + 0.5f;
        return 1;
    }
    for (int t = 0; t < 300; t++) {
        int c = (int)(rnd() % (uint32_t)(s_er.w * s_er.h));
        if (!s_er.walk[c] || s_er.open[c] < open || s_er.steps[c] == ECO_NOSTEP || s_er.steps[c] < 12)
            continue;
        *ox = (float)(s_er.x0 + c % s_er.w) + 0.5f;
        *oy = (float)(s_er.y0 + c / s_er.w) + 0.5f;
        return 1;
    }
    return 0;
}

/* Un lugar libre de la arena cerca de (x, y), a `r` como mucho */
static int eco_near(float x, float y, int r, int open, float *ox, float *oy)
{
    for (int k = 0; k < 60; k++) {
        float nx = x + (float)((int)(rnd() % (uint32_t)(2 * r + 1)) - r), ny = y + (float)((int)(rnd() % (uint32_t)(2 * r + 1)) - r);
        int c = eco_cell(nx, ny);
        if (c >= 0 && s_er.walk[c] && s_er.open[c] >= open && !units_blocking(floorf(nx) + 0.5f, floorf(ny) + 0.5f, 1.0f)) {
            *ox = floorf(nx) + 0.5f;
            *oy = floorf(ny) + 0.5f;
            return 1;
        }
    }
    return 0;
}

/* Lo de la arena al armarse: sin los monstruos de su DS1 (Baal en su trono, los jefes uber) */
static void eco_zone_loaded(int z)
{
    if (z != s_er.zone)
        return;
    int removed = 0;
    for (int u = units_count() - 1; u >= 0; u--)
        if (units_zone(u) == z && units_alive_enemy(u)) {
            units_remove(u);
            removed++;
        }
    if (eco_debug())
        printf("ECO: arena armada, %d monstruos de su DS1 borrados\n", removed);
}

/* Un objeto que se usa (el pozo, un santuario, el cofre), puesto como los de las salas */
static int eco_object(const char *cls, float x, float y)
{
    units_zone_begin(s_er.zone);
    int u = units_place_object(&s_map, cls, x, y);
    units_zone_end();
    if (u >= 0)
        obj_init(u);
    return u;
}

static int eco_shrine_id(int code)
{
    for (int k = 1; k < s_nshr; k++)
        if (s_shr[k].code == code)
            return k;
    return 0;
}

/* Cada cuatro oleadas, dos santuarios de los que duran (armadura, combate, habilidad, recarga de
   maná) o el explosivo, cerca de los jugadores */
static void eco_shrines(void)
{
    static const int k_codes[5] = { 6, 7, 12, 13, 21 };
    for (int k = 0; k < 2; k++) {
        const Player *p = eco_alive_player();
        float x, y;
        if (!p || !eco_near(p->x, p->y, 9, 3, &x, &y))
            continue;
        int u = eco_object("ShrineBaalMagic", x, y), id = eco_shrine_id(k_codes[rnd() % 5]);
        if (u >= 0 && id > 0)
            units_object_set_state(u, (units_object_state(u) & ~(0xff | OBJ_USED)) | id);
        s_er.shrines[k] = u;
    }
}

static void eco_shrines_clear(void)
{
    for (int k = 0; k < 2; k++) {
        int u = s_er.shrines[k];
        if (u >= 0 && units_object_row(u) && strcmp(units_id(u), "ShrineBaalMagic") == 0) {
            units_object_unblock(&s_map, u);
            units_remove(u);
        }
        s_er.shrines[k] = -1;
    }
}

/* Los que no sirven de oleada: los nidos de cuervos (quietos), los Antiguos, las bestias de asedio
   (llevan jinete) y las vacas */
static int eco_kind_ok(const char *id)
{
    static const char *const k_no[] = { "crownest", "ancientbarb", "siegebeast", "hellbovine" };
    for (unsigned k = 0; k < sizeof k_no / sizeof k_no[0]; k++)
        if (strncmp(id, k_no[k], strlen(k_no[k])) == 0)
            return 0;
    return units_monstats_int(id, "*hcIdx", -1) >= 0 && units_monstats_int(id, "killable", 1) == 1; /* (levels.txt trae algún "mon4" de relleno) */
}

/* Los tipos de la oleada: 3 (4 desde la décima) de los niveles del acto del tema */
static void eco_pick_kinds(void)
{
    const char *all[96], *ok[96];
    int n = world_act_monsters(s_er.act, all, 96), m = 0;
    for (int k = 0; k < n; k++)
        if (eco_kind_ok(all[k]))
            ok[m++] = all[k];
    int want = eco_stage(s_er.wave) >= 10 ? 4 : 3;
    s_er.nkinds = 0;
    for (int t = 0; t < 60 && s_er.nkinds < want && m > 0; t++) {
        const char *id = ok[rnd() % (uint32_t)m];
        int dup = 0;
        for (int k = 0; k < s_er.nkinds; k++)
            dup |= strcmp(s_er.kinds[k], id) == 0;
        if (!dup)
            s_er.kinds[s_er.nkinds++] = id;
    }
    if (s_er.nkinds == 0) /* (sin tablas: algo que siempre está) */
        s_er.kinds[s_er.nkinds++] = "zombie1";
}

/* Los jefes de acto de la oleada (múltiplo de cinco): en la primera vuelta (oleadas 5..25) los de la
   historia; en la segunda (30..50) los del evento Pandemonium, que sueltan lo del jefe de su acto */
static int eco_act_bosses(int wave, const char **ids, const char **tcs)
{
    static const char *const k_story[5] = { "andariel", "duriel", "mephisto", "diablo", "baalcrab" };
    static const char *const k_uber[4][2] = {
        { "uberandariel", "andariel" }, { "uberduriel", "duriel" }, { "uberizual", "izual" }, { "diabloclone", "diablo" }
    };
    int act = (wave - 1) / ECO_ACT_WAVES % 5, cycle = (wave - 1) / (5 * ECO_ACT_WAVES) % 2;
    if (!cycle) {
        ids[0] = k_story[act];
        tcs[0] = NULL;
        return 1;
    }
    if (act < 4) {
        ids[0] = k_uber[act][0];
        tcs[0] = k_uber[act][1];
        return 1;
    }
    static const char *const k_evil[3][2] = { { "ubermephisto", "mephisto" }, { "uberdiablo", "diablo" }, { "uberbaal", "baalcrab" } };
    for (int k = 0; k < 3; k++) {
        ids[k] = k_evil[k][0];
        tcs[k] = k_evil[k][1];
    }
    return 3;
}

static void eco_say(const char *text)
{
    s_quest_ticks = 4 * TICK_RATE;
    snprintf(s_quest_banner, sizeof s_quest_banner, "%s", text);
}

/* Salen los jefes de la oleada, lejos */
static void eco_spawn_bosses(void)
{
    float x, y;
    /* Los de acto, fuera de la pantalla pero no en la otra punta: Diablo vuelve a su lugar si el
       jugador está a más de 85 (a4_diablo); los con nombre, más cerca: algunos no se mueven de su
       lugar (el Invocador en su plataforma) */
    if (!(s_er.boss_kind == 1 ? eco_spot(22.0f, 30.0f, 3, &x, &y) : eco_spot(12.0f, 18.0f, 3, &x, &y))) {
        s_er.boss_kind = 0;
        return;
    }
    s_er.nboss = 0;
    units_zone_begin(s_er.zone);
    if (s_er.boss_kind == 1) {
        const char *ids[3], *tcs[3];
        int n = eco_act_bosses(s_er.wave, ids, tcs);
        for (int k = 0; k < n; k++) {
            float bx = x + (float)k * 3.0f - (float)(n - 1) * 1.5f, by = y;
            if (!eco_walk(bx, by))
                bx = x, by = y;
            int u = units_spawn_boss_at(ids[k], bx, by, s_er.mlvl + eco_boss_levels(), s_er.mlvl);
            if (u < 0)
                continue;
            if (tcs[k])
                units_set_tc_of(u, tcs[k]);
            s_er.boss[s_er.nboss++] = u;
        }
    } else {
        const char *const *list = k_eco_named[s_er.act - 1];
        int n = 0;
        while (n < 16 && list[n])
            n++;
        const char *name = n ? list[rnd() % (uint32_t)n] : "Bishibosh";
        int u = name[0] == '=' ? units_spawn_boss_at(name + 1, x, y, s_er.mlvl + eco_boss_levels(), s_er.mlvl)
                               : units_spawn_superunique(name, x, y, s_er.mlvl);
        if (u >= 0)
            s_er.boss[s_er.nboss++] = u;
    }
    units_zone_end();
    if (s_er.nboss == 0) {
        s_er.boss_kind = 0;
        return;
    }
    overlays_add("teleport", OV_AT_POINT, 0, x, y, 0);
    char msg[96];
    if (s_er.nboss > 1)
        snprintf(msg, sizeof msg, tr("¡%s y sus hermanos!"), units_display_name(s_er.boss[0]));
    else
        snprintf(msg, sizeof msg, tr("¡%s!"), units_display_name(s_er.boss[0]));
    eco_say(msg);
    if (eco_debug())
        for (int k = 0; k < s_er.nboss; k++)
            printf("ECO: oleada %d, sale %s (%s, nivel %d) en (%.0f, %.0f)\n", s_er.wave, units_display_name(s_er.boss[k]),
                   units_id(s_er.boss[k]), units_level(s_er.boss[k]), x, y);
}

static int eco_bosses_left(void)
{
    int n = 0;
    for (int k = 0; k < s_er.nboss; k++)
        n += units_alive_enemy(s_er.boss[k]) && units_is_hunter(s_er.boss[k]);
    return n;
}

/* ¿Se cumplió la oleada? (la de jefe de acto al matarlos; las otras con la cuota): ya no salen más y se pasa
   cuando no queda ninguno */
static int eco_goal(void)
{
    int kills = units_hunt_deaths() - s_er.deaths0;
    return s_er.boss_due < 0 &&
           (s_er.boss_kind == 1 ? eco_bosses_left() == 0 && (s_er.nboss > 0 || kills >= s_er.quota) : kills >= s_er.quota);
}

/* El primero de los jefes de la oleada que sigue vivo (-1 = ninguno) */
static int eco_boss_alive(void)
{
    for (int k = 0; k < s_er.nboss; k++)
        if (units_alive_enemy(s_er.boss[k]) && units_is_hunter(s_er.boss[k]))
            return s_er.boss[k];
    return -1;
}

/* Una manada (o un campeón o un jefe al azar con su grupo, cada vez más seguido) */
static void eco_spawn_pack(void)
{
    float x, y;
    if (!eco_spot(16.0f, 26.0f, 2, &x, &y))
        return;
    int k = (int)(rnd() % (uint32_t)s_er.nkinds), made = 0;
    const char *id = s_er.kinds[k];
    int stage = eco_stage(s_er.wave);
    int boss_pct = (4 + stage * 3 / 2) * 100 / (s_er.pace > 100 ? s_er.pace : 100); /* (menos en niveles bajos) */
    units_zone_begin(s_er.zone);
    if ((int)(rnd() % 100) < (boss_pct < 40 ? boss_pct : 40)) {
        made = units_spawn_level_boss(id, x, y, s_er.mlvl);
    } else {
        int n = 3 + (int)(rnd() % 3) + (stage >= 15) + (s_er.pace <= ECO_PACE_BIG);
        for (int i = 0; i < n; i++) {
            float mx = x, my = y;
            if (i > 0 && !eco_near(x, y, 2, 1, &mx, &my))
                continue;
            made += units_spawn_monster(id, mx, my, s_er.mlvl) >= 0;
        }
    }
    units_zone_end();
    if (!made) { /* (sin gráficos: ese tipo no sirve) */
        if (eco_debug())
            printf("ECO: %s no sale, se saca de la oleada\n", id);
        s_er.kinds[k] = s_er.kinds[--s_er.nkinds];
        if (s_er.nkinds == 0)
            s_er.kinds[s_er.nkinds++] = "zombie1";
        return;
    }
    overlays_add("teleport", OV_AT_POINT, 0, x, y, 0);
}

static void eco_revive(Player *p, float x, float y)
{
    place_at(p, (int)(p - s_pl), x, y);
    p->hp = p->hp_max;
    p->mana = p->mana_max;
    p->dead_ticks = 0;
    set_player_state(p, PS_NORMAL, MODE_TN);
}

/* La mercenaria muerta vuelve (después de cada oleada de jefe) */
static void eco_merc_back(Player *p)
{
    if (!p->merc_name || !p->merc_dead)
        return;
    if (p->merc_unit < 0)
        p->merc_unit = units_merc_spawn((int)(p - s_pl), merc_monster(p->merc_type), p->x, p->y);
    if (p->merc_unit < 0)
        return;
    p->merc_dead = 0;
    p->merc_warned = 0;
    units_merc_warp(p->merc_unit, p->x, p->y);
    merc_apply(p, -1);
}

static void eco_wave_begin(void)
{
    int w = ++s_er.wave;
    eco_set_pace();
    /* Los monstruos, un nivel más por etapa (medio por oleada); en los niveles bajos más despacio (uno cada
       tres etapas hasta el 15, cada dos hasta el 30), pero nunca más de uno por debajo de los personajes */
    if (w > 1)
        s_er.grow12 += (s_er.pace >= 190 ? 4 : s_er.pace >= 140 ? 6 : 12) * 5 / ECO_ACT_WAVES;
    int raw = s_er.base + s_er.grow12 / 12, floor = eco_party_level() - 1;
    raw = raw < floor ? floor : raw;
    int over = raw > 99 ? raw - 99 : 0;
    s_er.mlvl = raw > 99 ? 99 : raw < 1 ? 1 : raw;
    units_set_hunt(s_er.zone, over * 10 + eco_soft_pct(), over * 5 + eco_soft_pct());
    s_er.act = (w - 1) / ECO_ACT_WAVES % 5 + 1;
    s_er.quota = (20 + 3 * eco_stage(w)) * (s_num_players > 1 ? 3 : 2) / 2;
    s_er.deaths0 = units_hunt_deaths();
    s_er.kills_seen = 0;
    s_er.last_kill = 0;
    s_er.nboss = 0;
    s_er.boss_kind = w % ECO_ACT_WAVES == 0 ? 1 : w % ECO_ACT_WAVES == ECO_NAMED_WAVE ? 2 : 0;
    s_er.boss_due = s_er.boss_kind ? 3 * TICK_RATE : -1;
    s_er.phase = EP_WAVE;
    s_er.ticks = 0;
    s_er.spawn_cd = TICK_RATE;
    s_er.best = w > s_er.best ? w : s_er.best;
    eco_pick_kinds();
    const Player *alive = eco_alive_player(); /* (con dos jugadores, el que murió vuelve al lado del otro) */
    for (int i = 0; i < s_num_players && alive; i++)
        if (s_pl[i].state >= PS_DYING)
            eco_revive(&s_pl[i], alive->x + 1.5f, alive->y);
    if (w % 4 == 0)
        eco_shrines();
    char msg[64];
    snprintf(msg, sizeof msg, tr("Oleada %d"), w);
    eco_say(msg);
    if (eco_debug()) {
        printf("ECO: oleada %d (acto %d, nivel %d, cuota %d, vida %+d%%, daño %+d%%, ritmo %d%%):", w, s_er.act, s_er.mlvl,
               s_er.quota, over * 10 + eco_soft_pct(), over * 5 + eco_soft_pct(), s_er.pace);
        for (int k = 0; k < s_er.nkinds; k++)
            printf(" %s", s_er.kinds[k]);
        printf("%s\n", s_er.boss_kind == 1 ? " + jefe de acto" : s_er.boss_kind == 2 ? " + jefe con nombre" : "");
        printf("ECO: botín de nivel %d (los del Acto 5 sueltan de \"%s\")\n", eco_loot_level(),
               items_tc_at_level("Act 5 H2H C", eco_loot_level()));
    }
}

static void eco_wave_clear(void)
{
    s_er.phase = EP_BREAK;
    s_er.ticks = 0;
    units_hunters_kill(); /* (los que no se contaban: enterrados o volando; o trabados, ECO_STUCK) */
    s_er.alive = 0;
    s_er.over = 0;
    eco_set_pace(); /* (el respiro, con el nivel de después de la oleada) */
    eco_shrines_clear();
    char msg[64];
    snprintf(msg, sizeof msg, tr("¡Oleada %d superada!"), s_er.wave);
    eco_say(msg);
    audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
    if (s_er.boss_kind == 1)
        for (int i = 0; i < s_num_players; i++)
            eco_merc_back(&s_pl[i]);
    if (eco_debug())
        printf("ECO: oleada %d superada en el tick %u (%d vivos)\n", s_er.wave, s_ticks, s_er.alive);
}

/* Se termina la corrida: caen los monstruos, el récord y el cartel */
static void eco_finish(int why)
{
    if (s_er.phase >= EP_END || s_er.phase == EP_OFF)
        return;
    s_er.end = why;
    s_er.phase = EP_END;
    s_er.ticks = 0;
    units_hunters_kill();
    units_set_hunt(-1, 0, 0);
    eco_shrines_clear();
    s_er.record = 0;
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        p->eco_runs++;
        if (s_er.best > p->eco_best) {
            p->eco_best = s_er.best;
            s_er.record = 1;
        }
    }
    char msg[96];
    snprintf(msg, sizeof msg, tr(why == EE_OVERRUN ? "¡Te desbordaron! Oleada %d" : "Fin del Eco: oleada %d"), s_er.best);
    eco_say(msg);
    s_quest_ticks = 6 * TICK_RATE;
    if (eco_debug())
        printf("ECO: fin (%s) en la oleada %d, tick %u%s\n", why == EE_DIED ? "murieron" : why == EE_OVERRUN ? "desborde" : "se terminó",
               s_er.best, s_ticks, s_er.record ? ", récord nuevo" : "");
}

/* Después del final: todos vivos en el medio, el cofre del Eco y el portal de vuelta */
static void eco_loot_begin(void)
{
    /* (primero el cofre y el portal, después los jugadores en un lugar libre: el cofre puesto encima
       de uno lo dejaba encerrado) */
    float x = s_er.cx, y = s_er.cy;
    s_er.chest = eco_object("ConsolationChest", x, y);
    if (s_er.chest >= 0)
        units_object_set_state(s_er.chest, units_object_state(s_er.chest) & ~(OBJ_LOCKED | OBJ_TRAP_MASK | OBJ_USED));
    /* El portal, a 4..6 del cofre (pegado, el cofre y el otro jugador no dejaban pararse encima) */
    s_er.px = x - 5.0f;
    s_er.py = y;
    for (int t = 0; t < 40; t++) {
        float a = (float)(rnd() % 6283) / 1000.0f, r = 4.0f + (float)(rnd() % 200) / 100.0f;
        float nx = floorf(x + cosf(a) * r) + 0.5f, ny = floorf(y + sinf(a) * r) + 0.5f;
        int c = eco_cell(nx, ny);
        if (c >= 0 && s_er.walk[c] && s_er.open[c] >= 2 && s_er.steps[c] != ECO_NOSTEP) {
            s_er.px = nx;
            s_er.py = ny;
            break;
        }
    }
    a2_portal_object(s_er.zone, "PortalPermanent", s_er.px, s_er.py);
    for (int i = 0; i < s_num_players; i++)
        eco_revive(&s_pl[i], x + 2.5f, y + 2.5f);
    s_er.phase = EP_LOOT;
    s_er.ticks = 0;
    if (s_er.record)
        eco_say(tr("¡Récord nuevo!"));
    for (int i = 0; i < s_num_players; i++)
        save_character(&s_pl[i]);
}

/* El cofre del Eco: 2 + oleada / 2 tiradas (hasta 16) del cofre que le toca al nivel de los personajes
   (eco_loot_level: de los cofres de los actos y las dificultades, el de ese nivel), más hallazgo mágico
   por oleada y las primeras mágicas; y el oro */
static int eco_chest_open(Player *p, int u)
{
    if (!s_eco || u != s_er.chest || u < 0)
        return 0;
    int ilvl = eco_loot_level();
    const char *tc = items_tc_at_level("Act 1 Chest A", ilvl);
    int best = s_er.best, n = 2 + best / 2;
    n = n > 16 ? 16 : n;
    int mf = (p ? p->bonus[STAT_MAGIC_FIND] : 0) + best * 5, got = 0;
    items_set_potion_bonus(ECO_POTION_BONUS);
    for (int k = 0; k < n; k++) {
        Item out[6];
        int m = items_roll_drop_quality(tc, ilvl, mf, k < best / 5 ? QUALITY_MAGIC : 0, out, 6);
        obj_drop_items(u, out, m);
        got += m;
    }
    items_set_potion_bonus(0);
    const ItemType *gt = items_find("gld");
    for (int k = 0; gt && k < 3; k++) {
        Item g = items_make(gt);
        g.amount = (best > 0 ? best : 1) * ilvl * 20 / 3 + 1;
        obj_drop_items(u, &g, 1);
    }
    s_er.chest = -1;
    if (eco_debug())
        printf("ECO: el cofre suelta %d objetos (%s, nivel %d, %d tiradas)\n", got, tc, ilvl, n);
    return 1;
}

/* El equipo común (blanco o de baja calidad, sin engarces) que nadie levantó en dos minutos se va:
   con cientos de muertos, el piso de la arena quedaba tapado de nombres */
static void eco_ground_sweep(void)
{
    int n = 0;
    for (int i = 0; i < items_ground_count(); i++) {
        float x, y;
        const Item *it = items_ground_item(i, &x, &y);
        if (!it || !it->type || it->type->slot == SLOT_NONE || it->sockets > 0 ||
            (it->quality != QUALITY_NORMAL && it->quality != QUALITY_LOW) || items_ground_age(i) < 120 * TICK_RATE ||
            !eco_walk(x, y))
            continue;
        items_ground_remove(i);
        n++;
    }
    if (n && eco_debug())
        printf("ECO: %d objetos comunes viejos se van del piso\n", n);
}

/* La corrida: los jugadores en el lugar más abierto de una arena al azar (después de act_switch(5)) */
static void eco_enter(void)
{
    eco_free();
    memset(&s_er, 0, sizeof s_er);
    s_er.zone = s_er.chest = s_er.well = -1;
    s_er.shrines[0] = s_er.shrines[1] = -1;
    units_set_terror(NULL, 0); /* (ni zonas de terror) */
    units_terror_pool(NULL, NULL, 0);
    int sum = 0;
    for (int i = 0; i < s_num_players; i++)
        sum += s_pl[i].level_num;
    s_er.base = s_num_players > 0 ? sum / s_num_players : 1;
    if (getenv("D2_ECO_LEVEL"))
        s_er.base = atoi(getenv("D2_ECO_LEVEL"));
    s_er.base = s_er.base < 1 ? 1 : s_er.base;
    s_er.cap = getenv("D2_ECO_CAP") ? atoi(getenv("D2_ECO_CAP")) : s_num_players > 1 ? 50 : 40;
    eco_set_pace();
    int first = getenv("D2_ECO_ARENA") ? atoi(getenv("D2_ECO_ARENA")) % ECO_ARENAS : (int)(rnd() % ECO_ARENAS);
    for (int k = 0; k < ECO_ARENAS && !s_er.lv; k++) {
        int a = (first + k) % ECO_ARENAS, z = world_level_zone(k_eco_arenas[a]);
        if (z < 0)
            continue;
        s_er.arena = a;
        s_er.zone = z;
        if (zone_load(z) != 0) {
            s_er.zone = -1;
            continue;
        }
        for (int i = 0; i < world_level_count(); i++)
            if (world_level(i)->id == k_eco_arenas[a])
                s_er.lv = world_level(i);
        if (s_er.lv && eco_map() != 0) {
            eco_free();
            s_er.lv = NULL;
        }
        if (!s_er.lv)
            s_er.zone = -1;
    }
    if (!s_er.lv) {
        fprintf(stderr, "Eco del Odio: no pude armar ninguna arena\n");
        return;
    }
    s_start_x = s_town_x = s_er.cx;
    s_start_y = s_town_y = s_er.cy;
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        place_at(p, i, s_er.cx, s_er.cy);
        set_player_state(p, PS_NORMAL, MODE_TN);
        p->hp = p->hp_max;
        p->mana = p->mana_max;
    }
    float wx, wy;
    if (eco_near(s_er.cx + 5.0f, s_er.cy - 3.0f, 3, 3, &wx, &wy))
        s_er.well = eco_object("WellBaal", wx, wy);
    eco_steps();
    units_set_flow(s_er.steps, s_er.x0, s_er.y0, s_er.w, s_er.h);
    units_set_hunt(s_er.zone, 0, 0);
    s_er.wave = getenv("D2_ECO_WAVE") ? atoi(getenv("D2_ECO_WAVE")) - 1 : 0;
    s_er.wave = s_er.wave < 0 ? 0 : s_er.wave;
    s_er.grow12 = s_er.wave * 12 * 5 / ECO_ACT_WAVES; /* (empezando más adelante, como si hubieran subido de a medio) */
    s_er.phase = EP_START;
    s_er.ticks = 0;
    char msg[96];
    snprintf(msg, sizeof msg, tr("Eco del Odio: %s"), str_get(s_er.lv->name));
    eco_say(msg);
    s_quest_ticks = 5 * TICK_RATE;
    printf("eco del odio: %s (dificultad %d, nivel %d)\n", s_er.lv->name, s_diff, s_er.base);
}

static void eco_tick(void)
{
    if (!s_eco || s_er.phase == EP_OFF)
        return;
    tz_sunder(); /* (los amuletos Sunder sirven también acá) */
    s_er.ticks++;
    int running = eco_running();
    if (running && s_ticks % ECO_FLOW == 0)
        eco_steps();
    if (running && s_ticks % 5 == 0)
        s_er.alive = units_hunters_alive();
    if (s_ticks % TICK_RATE == 0) /* los cadáveres de las oleadas se van a los 30 segundos (150 como mucho) */
        units_hunt_corpses(30 * TICK_RATE, 150);
    if (s_ticks % (5 * TICK_RATE) == 0)
        eco_ground_sweep();
    switch (s_er.phase) {
    case EP_START: /* (cuando se terminaron de cargar los dibujos; en red, a los 6 segundos en todas las máquinas:
                      lo que tarda la carga no puede cambiar la partida) */
        if (s_lockstep ? s_er.ticks >= 6 * TICK_RATE : s_er.ticks >= 4 * TICK_RATE && units_loading() == 0)
            eco_wave_begin();
        break;
    case EP_WAVE: {
        if (s_er.boss_due >= 0 && s_er.ticks >= s_er.boss_due) {
            s_er.boss_due = -1;
            eco_spawn_bosses();
        }
        int kills = units_hunt_deaths() - s_er.deaths0;
        int cap = s_er.boss_kind == 1 ? s_er.cap / 3 : s_er.cap;
        if (eco_debug() && s_er.ticks % (5 * TICK_RATE) == 0)
            printf("ECO: t%u oleada %d: %d vivos, %d de %d muertos, %d unidades\n", s_ticks, s_er.wave, s_er.alive, kills, s_er.quota,
                   units_count());
        /* Cumplida ya no salen más, y se pasa cuando no queda ninguno vivo (también el jefe con nombre) */
        int goal = eco_goal();
        if (!goal && s_er.alive < cap && --s_er.spawn_cd <= 0) { /* (siguen saliendo: si no se los mata, la arena se llena) */
            int every = 120 - 3 * eco_stage(s_er.wave); /* (de casi 5 segundos a 1,6 desde la etapa 27; por el ritmo) */
            every = (every < 40 ? 40 : every) * s_er.pace / 100;
            s_er.spawn_cd = s_num_players > 1 ? every * 7 / 10 : every;
            eco_spawn_pack();
            s_er.alive = units_hunters_alive(); /* (contados ya: si no, con la cuota recién cumplida parecía vacía) */
        }
        if (kills != s_er.kills_seen) {
            s_er.kills_seen = kills;
            s_er.last_kill = s_er.ticks;
        }
        if (goal && (s_er.alive == 0 || s_er.ticks - s_er.last_kill > ECO_STUCK)) {
            if (s_er.alive > 0 && eco_debug())
                printf("ECO: oleada %d: %d no se dejan matar en dos minutos, se pasa igual\n", s_er.wave, s_er.alive);
            eco_wave_clear();
        }
        break;
    }
    case EP_BREAK:
        if (s_er.ticks >= ECO_BREAK) {
            int n = 0;
            for (int i = 0; i < items_ground_count(); i++)
                n += items_ground_item(i, NULL, NULL) != NULL;
            items_ground_clear(); /* (lo que no se levantó en el respiro se va) */
            if (eco_debug())
                printf("ECO: se borran %d objetos del piso\n", n);
            eco_wave_begin();
        }
        break;
    case EP_END:
        if (s_er.ticks >= 3 * TICK_RATE)
            eco_loot_begin();
        break;
    case EP_LOOT:
        for (int i = 0; i < s_num_players; i++) {
            float dx = s_pl[i].x - s_er.px, dy = s_pl[i].y - s_er.py;
            if (s_pl[i].state < PS_DYING && dx * dx + dy * dy < 1.5f * 1.5f && s_er.ticks > TICK_RATE && !s_er.exited) {
                s_eco_exit = 1;
                s_er.exited = 1;
                if (eco_debug())
                    printf("ECO: el jugador %d entra al portal: al menú\n", i + 1);
            }
        }
        break;
    default:
        break;
    }
    if (!eco_running())
        return;
    /* Muertos todos: se termina (con dos, el que murió vuelve en la oleada que sigue) */
    int alive = 0;
    for (int i = 0; i < s_num_players; i++)
        alive += s_pl[i].state < PS_DYING;
    s_er.dead_ticks = alive ? 0 : s_er.dead_ticks + 1;
    if (s_er.dead_ticks > 2 * TICK_RATE) {
        eco_finish(EE_DIED);
        return;
    }
    /* El desborde */
    if (s_er.alive >= s_er.cap) {
        if (++s_er.over >= eco_overrun_ticks())
            eco_finish(EE_OVERRUN);
    } else if (s_er.alive < s_er.cap * 9 / 10) {
        s_er.over = 0;
    }
}

/* "TERMINAR EL ECO" en la pausa: 1 si terminó la corrida (si no, la pausa guarda y sale) */
static int eco_pause_quit(void)
{
    if (!eco_running())
        return 0;
    eco_finish(EE_QUIT);
    return 1;
}

static void eco_pause_line(char *out, size_t cap)
{
    out[0] = '\0';
    if (!s_eco || s_er.phase == EP_OFF)
        return;
    int best = s_pl[0].eco_best;
    if (best > 0)
        snprintf(out, cap, tr("Eco del Odio: oleada %d (récord %d)"), s_er.wave, best);
    else
        snprintf(out, cap, tr("Eco del Odio: oleada %d"), s_er.wave);
}

/* Arriba a la derecha: la oleada y su nivel, lo que falta y la barra del desborde */
/* Un aviso abajo del panel: centrado en cx pero sin salirse de la pantalla, con fondo (si no, se mezcla
   con los nombres de los objetos del suelo) */
static void eco_note(int cx, int y, const char *text, int scale, uint8_t color)
{
    int tw = font_width(text, scale), x = cx - tw / 2;
    x = x + tw > SCREEN_W - 6 ? SCREEN_W - 6 - tw : x;
    x = x < 6 ? 6 : x;
    fb_blend_rect(x - 4, y - 2, tw + 8, font_height(scale) + 4, COL(R_GRAY, 0), BLEND_50);
    font_draw(x, y, text, scale, color);
}

static void eco_draw(void)
{
    if (!s_eco || s_er.phase == EP_OFF)
        return;
    int w = 220, h = 62, x = SCREEN_W - w - 8, y = 8;
    ui_dark_box(x, y, w, h);
    font_draw_centered(x + w / 2, y + 5, "ECO DEL ODIO", 1, COL(R_RED, 14));
    char t[96];
    if (s_er.wave > 0)
        snprintf(t, sizeof t, tr("Oleada %d - nivel %d"), s_er.wave, s_er.mlvl);
    else
        snprintf(t, sizeof t, "%s", tr("Prepárate"));
    font_draw_centered(x + w / 2, y + 16, t, 2, COL(R_GOLD, 15));
    int left = eco_bosses_left(), kills = units_hunt_deaths() - s_er.deaths0;
    if (s_er.phase >= EP_END)
        snprintf(t, sizeof t, tr("Llegaste a la oleada %d"), s_er.best);
    else if (s_er.phase == EP_BREAK)
        snprintf(t, sizeof t, "%s", tr("¡Oleada superada!"));
    else if (left > 0)
        snprintf(t, sizeof t, tr("Jefe: %s"), units_display_name(eco_boss_alive()));
    else if (s_er.phase == EP_WAVE && eco_goal()) /* (ya no salen: los que quedan) */
        snprintf(t, sizeof t, tr("Quedan: %d"), s_er.alive);
    else if (s_er.phase == EP_WAVE)
        snprintf(t, sizeof t, tr("Muertos: %d / %d"), kills < s_er.quota ? kills : s_er.quota, s_er.quota);
    else
        t[0] = '\0';
    font_draw_centered(x + w / 2, y + 36, t, 1, COL(R_GRAY, 15));
    int bx = x + 12, by = y + 50, bw = w - 24, a = s_er.alive < s_er.cap ? s_er.alive : s_er.cap;
    int fill = s_er.cap > 0 && s_er.phase <= EP_BREAK ? bw * a / s_er.cap : 0, blink = s_er.over > 0 && (s_anim_ticks / 6) % 2;
    fb_fill_rect(bx, by, bw, 5, COL(R_STONE, 5));
    fb_fill_rect(bx, by, fill, 5, blink ? COL(R_GOLD, 15) : COL(R_RED, 12));
    if (s_er.over > 0 && s_er.phase <= EP_BREAK) {
        snprintf(t, sizeof t, tr("¡Desborde! %d"), (eco_overrun_ticks() - s_er.over) / TICK_RATE + 1);
        eco_note(x + w / 2, y + h + 4, t, 2, COL(R_RED, 15));
    }
    if (s_er.phase == EP_BREAK) { /* la cuenta del respiro, grande en el medio de arriba */
        int left_s = (ECO_BREAK - s_er.ticks + TICK_RATE - 1) / TICK_RATE;
        snprintf(t, sizeof t, tr("Siguiente oleada en %d"), left_s > 0 ? left_s : 1);
        int ty = SCREEN_H / 4;
        eco_note(SCREEN_W / 2, ty, t, 3, COL(R_GOLD, 15));
        eco_note(SCREEN_W / 2, ty + font_height(3) + 8, tr("Lo que quede en el suelo se borra"), 1, COL(R_GRAY, 14));
    }
    if (s_er.phase == EP_LOOT)
        eco_note(x + w / 2, y + h + 4, "Abrí el cofre; el portal vuelve al menú", 1, COL(R_GRAY, 14));
}
