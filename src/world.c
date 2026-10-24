#include "world.h"

#include <SDL.h>

#include "fmt/fs.h"
#include "iso.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* El Acto 1 en un solo mundo, en tiles. Arriba de todo el Monasterio y los
   Barracones; abajo la fila de zonas exteriores (de 80x80, celdas de 8x8):

     [Barracones][Claustro Exterior]
                 [Puerta del Monasterio]
   [pueblo][Blood Moor][Cold Plains][Stony Field][Dark Wood][Black Marsh][Tamoe]
                       [cementerio]

   Las exteriores se unen por pasillos entre los bosques (bord4o con bord2o),
   salvo el Stony Field y el Dark Wood, que se unen por el Pasaje
   Subterráneo como en el original. Más abajo, separados por vacío, los
   calabozos: cada uno se llega por su entrada (cuevas, escaleras, el
   portal de las Piedras Cairn).

   Cada zona ocupa sus propios pedazos del mapa (alineados a 8 tiles) y
   estampa solo ahí: lo que una pieza del borde se pasa hacia la vecina lo
   pone la vecina con su propio borde. */
#define CELL       8
#define WILD_CELLS 10
#define WILD       (WILD_CELLS * CELL)
#define OUT_Y      120 /* fila de las zonas exteriores */
#define TOWN_W     56
#define TOWN_H     40
#define TOWN_CELL0 2
#define TOWN_BRIDGE_Y (OUT_Y + TOWN_CELL0 * CELL + 17) /* fila del puente de la salida del pueblo (towne1.ds1) */
#define GRAVE_CW   5
#define GRAVE_CH   6
#define PACK_W     560 /* ancho de las filas de calabozos */
#define PACK_GAP   8

#define TILES        "data/global/tiles/act1/"
#define TOWN_DS1     TILES "town/towne1.ds1"
#define TRANS_DS1    TILES "town/townetrans.ds1"
#define GRAVE_DS1    TILES "graveyard/gravey.ds1"
#define WAYPOINT_DS1 TILES "outdoors/waypoint.ds1"
#define BISHI_DS1    TILES "outdoors/fallcmpbish.ds1"
#define CAIRN_DS1    TILES "outdoors/cairn2.ds1"
#define INIFUSS_DS1  TILES "outdoors/funktree.ds1"
#define TOWER_DS1    TILES "outdoors/tower1.ds1"
#define TOME_DS1     TILES "outdoors/tome.ds1"
#define FACADE_DS1   TILES "monastry/facade1.ds1"
#define COURT_DS1    TILES "court/courtw.ds1"
#define INNER_DS1    TILES "court/cat_court.ds1"
#define CATHEDRAL_DS1 TILES "cathedrl/cathy3.ds1"
#define TRISTRAM_DS1 TILES "tristram/tri_town4.ds1"
#define TOWER2_DS1   TILES "outdoors/tower2.ds1"
#define ANDY_DS1     TILES "catacomb/andy3.ds1"
/* El Acto 2 */
#define TILES2       "data/global/tiles/act2/"
#define LUT_DS1      TILES2 "town/lutw.ds1" /* Lut Gholein (lvlprest.txt: "Act 2 - Town"): la salida al oeste */
#define LUTN_DS1     TILES2 "town/lutn.ds1" /* o al norte */
#define LUT_W        57
#define LUT_H        57
#define LUT_SIZE     56 /* lo que ocupa en el mundo (levels.txt; el DS1 tiene una fila más) */
#define DESERT_DT1   TILES2 "town/ground.dt1" /* la arena: estilo 0, secuencia 1 */
#define DESERT_SEQ   1
#define DESERT_W     80 /* Rocky Waste, Dry Hills, Far Oasis, Lost City y el Canyon */
#define VALLEY_W     32 /* Valley of Snakes */
/* El Acto 3 */
#define TILES3       "data/global/tiles/act3/"
#define DOCK_W       65 /* el DS1 (levels.txt dice 64x48: tiene una fila y una columna más) */
#define DOCK_H       49
#define GRASS_DT1    TILES "town/floor.dt1"
#define GRASS_STYLE  0 /* pasto caminable (el estilo 5 es el decorado bloqueado de afuera) */
#define GRASS_SEQ    0

/* Medidas de las piezas prearmadas */
#define FACADE_W 65
#define FACADE_H 19
#define COURT_W  57
#define COURT_H  41
#define CATH_W   29
#define CATH_H   35
#define INNER_W  19
#define INNER_H  21

#define MAX_LEVELS 48
#define MAX_ZONES  MAP_ZONES
#define MAX_WARPS  128 /* (los del mapa, los de las misiones y los portales al pueblo) */
#define MAX_LINKS  32
#define MAX_CELLS  12
#define MARKER_STYLE 8 /* los especiales con este estilo o más son marcas, no saltos */

enum { EX_N = 1, EX_S = 2, EX_E = 4, EX_W = 8 };

/* Cómo se arma cada zona */
enum {
    ZK_TOWN, ZK_MOOR, ZK_PLAINS, ZK_GRAVES, ZK_DEN, ZK_STONY, ZK_PASSAGE, ZK_DARKWOOD, ZK_MARSH, ZK_TAMOE,
    ZK_MONASTERY, ZK_BARRACKS, ZK_JAIL, ZK_CLOISTER, ZK_CATACOMBS, ZK_ANDARIEL, ZK_TRISTRAM, ZK_TOWER, ZK_CELLAR,
    ZK_COUNTESS,
    ZK_LUT, ZK_DESERT, ZK_CANYON, /* el Acto 2 */
    ZK_SEWER, ZK_TOMB, ZK_LAIR, ZK_SERPENT, /* sus calabozos (Zone.floor = el Id de levels.txt) */
    ZK_HAREM1, ZK_PALACE, ZK_ARCANE, ZK_DURIEL, /* el palacio, el Santuario Arcano y la guarida de Duriel */
    ZK_DOCK, ZK_JUNGLE, ZK_KURAST, ZK_CAUSEWAY, ZK_TRAVINCAL, /* el Acto 3 (world3.inc: Zone.floor de la selva y Kurast = cuál) */
    ZK_A3MAZE, ZK_A3PRESET, /* sus calabozos de salas y los de una sola pieza (Zone.floor = el Id de levels.txt) */
    ZK_FORTRESS, ZK_MESA, ZK_RIVER, ZK_SANCTUM, /* el Acto 4 (world4.inc: Zone.floor de las mesetas = cuál) */
    ZK_HARROGATH, ZK_SIEGE, ZK_BARRICADE, /* el Acto 5 (world5.inc: Zone.floor de las barricadas = BARR_*) */
    ZK_COWS, /* la Granja Muu Muu del Acto 1 (solo por el portal del cubo) */
    ZK_UBERDEN, /* Matron's Den, del evento Pandemonium (world5.inc: los cuatro, solo en Infierno) */
};

/* Las zonas del desierto (Zone.floor de ZK_DESERT), en el orden en que se encadenan */
enum { DZ_ROCKY, DZ_DRY, DZ_OASIS, DZ_LOST, DZ_VALLEY, DZ_COUNT };
#define DZ_SANDS (DZ_COUNT + 1) /* las Forgotten Sands del evento Pandemonium (en el mundo del Acto 5) */

typedef struct {
    int         kind;
    int         floor;  /* piso (Cárcel 1..3, Catacumbas 1..3, Sótano 1..4) */
    uint32_t    seed;   /* la forma de la zona: igual cada vez que se arma */
    int         loaded;
    int         x0, y0, x1, y1; /* pedazos que ocupa, en tiles (alineado a 8) */
    const char *name;   /* el de su primer nivel (para los mensajes) */
    int8_t      open[4][2]; /* el desierto: la primera celda de cada paso (de dos) hacia una vecina, por lado N, E, S, O (-1 = ninguno) */
} Zone;

static Level     s_levels[MAX_LEVELS];
static int       s_num_levels;
static Zone      s_zones[MAX_ZONES];
static int       s_num_zones;
static Warp      s_warps[MAX_WARPS];
static int       s_num_warps;
static WorldLink s_links[MAX_LINKS];
static int       s_num_links;
/* Qué zona lleva a cuál por un salto (aunque no estén armadas): para
   buscar el camino de una zona a otra (las pruebas automáticas) */
static int       s_jumps[MAX_WARPS][2];
static int       s_num_jumps;
static TxtTable  s_levels_txt; /* levels.txt: los nombres de los monstruos apuntan acá */
static TxtTable  s_lvlwarp_txt; /* lvlwarp.txt: qué saltos son trampillas */
static uint32_t  s_seed;
static int       s_act = 1; /* el acto armado (un mundo por acto) */
static int       s_world_diff; /* la dificultad (world_set_difficulty): los monstruos de los niveles */
static int       s_world_arena; /* el Eco del Odio (world_set_arena): Uber Tristram y la Colossal Summit en cualquier dificultad */

/* Lo que decide world_layout y usan las zonas al armarse */
static int s_moor_row, s_plains_row, s_dark_row, s_marsh_row, s_tamoe_col, s_grave_c0;
static int s_court_x, s_court_y;
static int s_pack_x, s_pack_y, s_pack_h;

/* Zonas de cada tipo (para unirlas con saltos) */
static int z_town, z_moor, z_plains, z_graves, z_den, z_stony, z_passage, z_dark, z_marsh, z_tamoe, z_monastery,
    z_barracks, z_jail[4], z_cloister, z_cat[5], z_tristram, z_tower, z_cellar[6], z_cows = -1;
static int z_desert[DZ_COUNT], z_canyon;
static int s_staff_tomb, s_boss_tomb; /* las tumbas de Tal Rasha (Id 66..72): la verdadera y la de Kaa */
static int s_lut_north; /* Lut Gholein con la salida al norte (el Rocky Waste arriba); si no, al oeste */

static uint32_t s_rng;

static uint32_t rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

/* Las salas de la última zona armada: en los calabozos, cada pieza de la
   grilla (como las salas del original, donde se reparten los objetos) */
#define MAX_ROOMS 400
static int s_rooms[MAX_ROOMS][4], s_num_rooms, s_rooms_zone = -1;

static void room_add(int x, int y, int w, int h)
{
    if (s_num_rooms < MAX_ROOMS) {
        s_rooms[s_num_rooms][0] = x;
        s_rooms[s_num_rooms][1] = y;
        s_rooms[s_num_rooms][2] = w;
        s_rooms[s_num_rooms][3] = h;
        s_num_rooms++;
    }
}

static int file_exists(const char *path)
{
    FILE *f = fs_open(path);
    if (f)
        fclose(f);
    return f != NULL;
}

/* Elige una variante al azar entre los archivos de la pieza que existan
   (por ejemplo bord1, bord1b, bord1c) */
static void pick_variant(char *out, size_t cap, const char *dir, const char *const *names, int count)
{
    char path[128];
    for (int tries = 0; tries < 8; tries++) {
        snprintf(path, sizeof path, TILES "%s/%s.ds1", dir, names[rnd() % (uint32_t)count]);
        if (file_exists(path)) {
            snprintf(out, cap, "%s", path);
            return;
        }
    }
    snprintf(out, cap, TILES "%s/%s.ds1", dir, names[0]);
}

static void stamp_border(Map *m, int ox, int oy, const char *base)
{
    char b[16], c[16];
    snprintf(b, sizeof b, "%sb", base);
    snprintf(c, sizeof c, "%sc", base);
    const char *names[3] = { base, b, c };
    char path[128];
    pick_variant(path, sizeof path, "outdoors", names, 3);
    map_stamp(m, path, ox, oy);
}

/* ---- Saltos ---- */

/* Un salto con los tiles especiales (de salto, no las marcas) que trajo una
   pieza recién estampada en el rectángulo de tiles. Con `style` >= 0, solo
   los de ese estilo (la torre tiene uno para salir y otro para bajar). */
static int add_warp(Map *m, int first, int x0, int y0, int w, int h, int style, int from, int to)
{
    if (from < 0 || to < 0 || s_num_warps >= MAX_WARPS)
        return -1;
    int tx0 = 1 << 30, ty0 = 1 << 30, tx1 = -1, ty1 = -1;
    for (int i = first; i < m->num_specials; i++) {
        const MapSpecial *s = &m->specials[i];
        if (s->x < x0 || s->x >= x0 + w || s->y < y0 || s->y >= y0 + h || s->style >= MARKER_STYLE ||
            (style >= 0 && s->style != style))
            continue;
        if (s->x < tx0) tx0 = s->x;
        if (s->y < ty0) ty0 = s->y;
        if (s->x > tx1) tx1 = s->x;
        if (s->y > ty1) ty1 = s->y;
    }
    if (tx1 < 0) {
        if (!m->pieces_skipped) /* (en el armado en partes, su pieza va en otra pasada) */
            fprintf(stderr, "no encontré el salto de %s a %s\n", s_zones[from].name, s_zones[to].name);
        return -1;
    }
    Warp *wp = &s_warps[s_num_warps++];
    memset(wp, 0, sizeof *wp);
    wp->x0 = (float)(tx0 * TILE_SUB);
    wp->y0 = (float)(ty0 * TILE_SUB);
    wp->x1 = (float)((tx1 + 1) * TILE_SUB);
    wp->y1 = (float)((ty1 + 1) * TILE_SUB);
    wp->from = from;
    wp->to = to;
    return 0;
}

/* La zona del nivel con ese Id de levels.txt, -1 = no está en este acto */
static int zone_of_level_id(int id)
{
    for (int i = 0; id > 0 && i < s_num_levels; i++)
        if (s_levels[i].id == id)
            return s_levels[i].zone;
    return -1;
}

/* El primer nivel de la zona */
static const Level *zone_level(int z)
{
    for (int i = 0; i < s_num_levels; i++)
        if (s_levels[i].zone == z)
            return &s_levels[i];
    return NULL;
}

/* Los saltos de una pieza recién estampada, como el original: el tile
   especial de estilo k (< 8) lleva al nivel Vis[k] de levels.txt. `pair` es
   cuántos estilos anteriores van a ese mismo nivel, y se llega al salto del
   mismo número del otro lado (las dos bocas de las cloacas en Lut Gholein:
   los estilos 2 y 3 llegan a los estilos 0 y 1 de la cloaca). Las
   trampillas (NoInteract en lvlwarp.txt) se usan con A, no al pisarlas. */
static void vis_warps(Map *m, int first, int x0, int y0, int w, int h, int z)
{
    const Level *l = zone_level(z);
    unsigned seen = 0;
    for (int i = first; l && i < m->num_specials; i++) {
        const MapSpecial *s = &m->specials[i];
        if (s->style >= MARKER_STYLE || s->x < x0 || s->x >= x0 + w || s->y < y0 || s->y >= y0 + h ||
            (seen >> s->style & 1u))
            continue;
        seen |= 1u << s->style;
        int to = zone_of_level_id(l->vis[s->style]), pair = 0;
        if (to < 0)
            continue;
        for (int k = 0; k < s->style; k++)
            pair += l->vis[k] == l->vis[s->style];
        if (add_warp(m, first, x0, y0, w, h, s->style, z, to) != 0)
            continue;
        Warp *wp = &s_warps[s_num_warps - 1];
        wp->pair = pair;
        if (l->noint >> s->style & 1) {
            wp->margin = 2.0f;
            wp->fixed_margin = 1;
        }
    }
}

#define POCKET_R     60   /* hasta dónde se mira alrededor de la llegada (en subtiles) */
#define POCKET_CELLS 3000 /* menos que una sala de 10x10 tiles: la llegada queda encerrada */

static int foot_free(const Map *m, int x, int y)
{
    return !map_blocked(m, x, y) && !map_blocked(m, x - 1, y) && !map_blocked(m, x + 1, y) &&
           !map_blocked(m, x, y - 1) && !map_blocked(m, x, y + 1);
}

/* ¿Al llegar por la boca w (al lado, como use_warp) se queda encerrado si
   pisarla lleva afuera? La sala de llegada del Maggot Lair se deja por
   encima de la boca de la cueva, que en el original se usa con un clic. */
static int warp_traps_arrival(const Map *m, const Warp *w, int zone)
{
    const Zone *zn = &s_zones[zone];
    int zx0 = zn->x0 * TILE_SUB, zy0 = zn->y0 * TILE_SUB, zx1 = zn->x1 * TILE_SUB, zy1 = zn->y1 * TILE_SUB;
    float cx = (w->x0 + w->x1) / 2.0f, cy = (w->y0 + w->y1) / 2.0f, pad = WARP_TOUCH + 1.5f;
    int sx = -1, sy = -1;
    for (int r = 1; r < 12 && sx < 0; r++)
        for (int a = 0; a < 16 && sx < 0; a++) {
            float x = cx + cosf((float)a * 0.3926991f) * (float)r, y = cy + sinf((float)a * 0.3926991f) * (float)r;
            if ((x >= w->x0 - pad && x < w->x1 + pad && y >= w->y0 - pad && y < w->y1 + pad) || x < zx0 || y < zy0 ||
                x >= zx1 || y >= zy1 || !foot_free(m, (int)x, (int)y))
                continue;
            sx = (int)x;
            sy = (int)y;
        }
    if (sx < 0)
        return 0;
    /* Desde ahí, sin acercarse a la boca: ¿hay más que un rincón? */
    enum { N = 2 * POCKET_R + 1 };
    static uint8_t seen[N * N];
    static int queue[POCKET_CELLS];
    memset(seen, 0, sizeof seen);
    int qh = 0, qt = 0;
    seen[POCKET_R * N + POCKET_R] = 1;
    queue[qt++] = POCKET_R * N + POCKET_R;
    while (qh < qt) {
        int c = queue[qh++], lx = c % N, ly = c / N;
        for (int d = 0; d < 8; d++) {
            static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            int nx = lx + dx[d], ny = ly + dy[d], x = sx - POCKET_R + nx, y = sy - POCKET_R + ny;
            if (nx < 0 || ny < 0 || nx >= N || ny >= N || x <= zx0 || y <= zy0 || x >= zx1 - 1 || y >= zy1 - 1)
                return 0; /* llegó lejos o al borde de la zona (un pasillo): no está encerrado */
            if (seen[ny * N + nx] || !foot_free(m, x, y))
                continue;
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            if (px >= w->x0 - WARP_TOUCH && px < w->x1 + WARP_TOUCH && py >= w->y0 - WARP_TOUCH && py < w->y1 + WARP_TOUCH)
                continue; /* ahí se pisa la boca */
            for (int k = 0; k < s_num_warps; k++) { /* otra salida de la zona: no está encerrado */
                const Warp *o = &s_warps[k];
                float mg = o->margin > 0.0f ? o->margin : WARP_TOUCH;
                if (o != w && o->from == zone && px >= o->x0 - mg && px < o->x1 + mg && py >= o->y0 - mg && py < o->y1 + mg)
                    return 0;
            }
            if (qt == POCKET_CELLS)
                return 0;
            seen[ny * N + nx] = 1;
            queue[qt++] = ny * N + nx;
        }
    }
    return 1;
}

/* Las escaleras quedan dentro de la pared: esas se toman desde un poco
   antes (si casi todo el rectángulo está bloqueado). Las bocas que se pisan
   y dejarían encerrado al que llega por ellas, también con A (como todas en
   el original) */
static void warp_margins(const Map *m, int zone)
{
    for (int i = 0; i < s_num_warps; i++) {
        Warp *w = &s_warps[i];
        if (w->from != zone || w->portal || w->fixed_margin)
            continue;
        int blocked = 0, total = 0;
        for (int y = (int)w->y0; y < (int)w->y1; y++)
            for (int x = (int)w->x0; x < (int)w->x1; x++, total++)
                blocked += map_blocked(m, x, y);
        w->margin = blocked * 2 > total ? 2.5f : 0.0f;
        if (w->margin == 0.0f && warp_traps_arrival(m, w, zone)) {
            w->margin = 2.5f;
            if (getenv("D2_ZONES"))
                printf("  la boca de %s a %s se usa con A (al llegar se quedaba encerrado)\n", zone_level(zone) ? zone_level(zone)->name : "?",
                       zone_level(w->to) ? zone_level(w->to)->name : "?");
        }
    }
}

/* ---- Las uniones entre zonas vecinas ----
   El original no las tiene: las zonas de afuera son niveles pegados y se
   camina de uno al otro por donde se pueda. Las nuestras son para armar la
   vecina a tiempo y para que los bots sepan por dónde cruzar, y se ponen al
   armar el mundo, sin colisión: en la selva del Acto 3 las del río van en
   el medio del borde, sobre el agua, y cada orilla puede seguir del otro
   lado o no (semilla 43: la orilla sur del Great Marsh entra 5 subtiles al
   Spider Forest y ahí hay árboles; el bot iba a ese rincón y volvía). Con
   las dos zonas armadas, el punto se corre al cruce que más entra a las dos
   (sin salir de cada una; a la par, el más cercano). Solo mira baldosas: no
   cambia con el orden en que se arman las zonas ni con barriles rotos. */

/* A cada lado del punto, hasta dónde se busca el cruce a lo largo del borde;
   hasta cuántos subtiles se cuenta lo que alcanza cada lado sin salir de su
   zona (los rincones vistos: 117 en la semilla 43, 3199 en la 6, donde la
   orilla este del río del Spider Forest solo se une por el Flayer Jungle; el
   muelle entero alcanza unos 8700) */
#define CROSS_SPAN  80
#define CROSS_REACH 6000
#define CROSS_RUNS  6 /* los cruces que se prueban, de más cerca a más lejos */

/* ¿Entra ahí el pie en cruz del jugador, con solo las baldosas? (los
   barriles que se rompen y las puertas que se abren no cierran un cruce) */
static int cross_stand(const Map *m, int x, int y)
{
    static const int dx[5] = { 0, -1, 1, 0, 0 }, dy[5] = { 0, 0, 0, -1, 1 };
    for (int k = 0; k < 5; k++)
        if (map_coll(m, x + dx[k], y + dy[k]) & (COLL_WALL | COLL_NOPLAYER))
            return 0;
    return 1;
}

/* Cuántos subtiles se alcanzan desde (x, y) sin salir de la zona z, hasta
   CROSS_REACH (-1 sin memoria) */
static int cross_open(const Map *m, int z, int x, int y)
{
    const Zone *zz = &s_zones[z];
    int x0 = zz->x0 * TILE_SUB, y0 = zz->y0 * TILE_SUB, w = (zz->x1 - zz->x0) * TILE_SUB, h = (zz->y1 - zz->y0) * TILE_SUB;
    if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h || !cross_stand(m, x, y))
        return 0;
    uint8_t *seen = calloc((size_t)w * (size_t)h / 8 + 1, 1); /* (también los que no se pisan: se miran una vez) */
    int *queue = malloc(CROSS_REACH * sizeof *queue);
    if (!seen || !queue) {
        free(seen);
        free(queue);
        return -1;
    }
    int qh = 0, qt = 0, i0 = (y - y0) * w + (x - x0);
    seen[i0 >> 3] |= (uint8_t)(1u << (i0 & 7));
    queue[qt++] = i0;
    while (qh < qt && qt < CROSS_REACH) {
        int c = queue[qh++], cx = c % w, cy = c / w;
        for (int dy = -1; dy <= 1 && qt < CROSS_REACH; dy++)
            for (int dx = -1; dx <= 1 && qt < CROSS_REACH; dx++) {
                int nx = cx + dx, ny = cy + dy, n = ny * w + nx;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h || (seen[n >> 3] >> (n & 7) & 1))
                    continue;
                seen[n >> 3] |= (uint8_t)(1u << (n & 7));
                if (cross_stand(m, x0 + nx, y0 + ny))
                    queue[qt++] = n;
            }
    }
    free(seen);
    free(queue);
    return qt;
}

static void link_settle(const Map *m, WorldLink *wl)
{
    double t_start = map_now_ms();
    const Zone *za = &s_zones[wl->a], *zb = &s_zones[wl->b];
    /* El borde que comparten y que pasa por el punto: vertical (x = bord) u
       horizontal (y = bord); `lo` la zona de antes (oeste o norte) */
    int vert = -1, bord = 0, lo = -1;
    if (za->x1 == zb->x0 || zb->x1 == za->x0) {
        lo = za->x1 == zb->x0 ? wl->a : wl->b;
        bord = s_zones[lo].x1 * TILE_SUB;
        vert = fabsf(wl->x - (float)bord) <= 2.0f ? 1 : -1;
    }
    if (vert < 0 && (za->y1 == zb->y0 || zb->y1 == za->y0)) {
        lo = za->y1 == zb->y0 ? wl->a : wl->b;
        bord = s_zones[lo].y1 * TILE_SUB;
        vert = fabsf(wl->y - (float)bord) <= 2.0f ? 0 : -1;
    }
    wl->cross = -2;
    if (vert < 0)
        return; /* (no está sobre un borde compartido: queda como está) */
    int hi = lo == wl->a ? wl->b : wl->a;
    /* Lo que tienen en común a lo largo del borde (en subtiles), y en eso los
       cruces: tramos seguidos donde el pie entra a los dos lados (el de `lo`
       en bord - 1, el otro en bord) */
    int t_lo = (vert ? (za->y0 > zb->y0 ? za->y0 : zb->y0) : (za->x0 > zb->x0 ? za->x0 : zb->x0)) * TILE_SUB;
    int t_hi = (vert ? (za->y1 < zb->y1 ? za->y1 : zb->y1) : (za->x1 < zb->x1 ? za->x1 : zb->x1)) * TILE_SUB;
    int t0 = (int)(vert ? wl->y : wl->x), from = t0 - CROSS_SPAN > t_lo ? t0 - CROSS_SPAN : t_lo;
    int to = t0 + CROSS_SPAN < t_hi - 1 ? t0 + CROSS_SPAN : t_hi - 1;
    int run0[2 * CROSS_SPAN + 1], run1[2 * CROSS_SPAN + 1], nruns = 0;
    for (int t = from; t <= to; t++) {
        int ok = vert ? cross_stand(m, bord - 1, t) && cross_stand(m, bord, t) : cross_stand(m, t, bord - 1) && cross_stand(m, t, bord);
        if (ok && nruns > 0 && run1[nruns - 1] == t - 1)
            run1[nruns - 1] = t;
        else if (ok) {
            run0[nruns] = run1[nruns] = t;
            nruns++;
        }
    }
    /* De más cerca a más lejos del punto, el que más alcanza del lado que
       menos alcanza (un rincón pierde contra el paso de verdad; a la par, el
       más cercano). Si los dos lados llegan al tope, ese. */
    int best = -1, best_reach = 0;
    uint8_t tried[2 * CROSS_SPAN + 1] = { 0 };
    for (int k = 0; k < CROSS_RUNS && best_reach < CROSS_REACH; k++) {
        int pick = -1, pd = 1 << 30;
        for (int r = 0; r < nruns; r++) {
            int d = t0 < run0[r] ? run0[r] - t0 : t0 > run1[r] ? t0 - run1[r] : 0;
            if (!tried[r] && d < pd)
                pick = r, pd = d;
        }
        if (pick < 0)
            break;
        tried[pick] = 1;
        int t = t0 < run0[pick] ? run0[pick] : t0 > run1[pick] ? run1[pick] : t0;
        int a = cross_open(m, lo, vert ? bord - 1 : t, vert ? t : bord - 1);
        int b = a > best_reach ? cross_open(m, hi, vert ? bord : t, vert ? t : bord) : 0;
        if (a < 0 || b < 0) { /* sin memoria: se vuelve a mirar la próxima vez */
            wl->cross = 0;
            return;
        }
        if ((a < b ? a : b) > best_reach) {
            best = pick;
            best_reach = a < b ? a : b;
        }
    }
    if (best >= 0 && (t0 < run0[best] || t0 > run1[best])) { /* al medio de ese cruce */
        float t = (float)(run0[best] + run1[best]) / 2.0f + 0.5f;
        if (getenv("D2_ZONES") || getenv("D2_WARPS"))
            printf("  unión %s - %s: el cruce de (%.0f, %.0f) pasa a (%.0f, %.0f)\n", zone_level(wl->a) ? zone_level(wl->a)->name : "?",
                   zone_level(wl->b) ? zone_level(wl->b)->name : "?", wl->x, wl->y, vert ? (float)bord : t,
                   vert ? t : (float)bord);
        wl->x = vert ? (float)bord : t;
        wl->y = vert ? t : (float)bord;
    }
    wl->cross = best >= 0 ? 1 : -1;
    if (best < 0 && (getenv("D2_ZONES") || getenv("D2_WARPS")))
        printf("  unión %s - %s en (%.0f, %.0f): sin cruce que entre a las dos\n", zone_level(wl->a) ? zone_level(wl->a)->name : "?",
               zone_level(wl->b) ? zone_level(wl->b)->name : "?", wl->x, wl->y);
    if (getenv("D2_ZONES"))
        printf("  unión %s - %s: cruce mirado en %.2f ms\n", zone_level(wl->a) ? zone_level(wl->a)->name : "?",
               zone_level(wl->b) ? zone_level(wl->b)->name : "?", map_now_ms() - t_start);
}

/* Las uniones de la zona recién armada con las vecinas que ya lo están */
static void links_settle(const Map *m, int z)
{
    for (int i = 0; i < s_num_links; i++) {
        WorldLink *wl = &s_links[i];
        int other = wl->a == z ? wl->b : wl->b == z ? wl->a : -1;
        if (other >= 0 && wl->cross == 0 && s_zones[other].loaded)
            link_settle(m, wl);
    }
}

/* ---- Zonas exteriores ---- */

/* Una zona exterior de celdas de 8x8: borde de bosque alrededor (con
   aberturas hacia las vecinas) y el interior para llenar */
typedef struct {
    int     x0, y0, cw, ch;              /* esquina en tiles y tamaño en celdas */
    int     open_n, open_s, open_e, open_w; /* celda del borde con pasillo (-1 = ninguna) */
    int     skip_w0, skip_w1;            /* filas del borde oeste sin bosque (el pueblo) */
    uint8_t used[MAX_CELLS][MAX_CELLS];
    int     last_x, last_y, last_w, last_h; /* lo último que puso wild_place (en tiles) */
    int     poi_x[8], poi_y[8], npoi;       /* lugares a los que llegan los caminos */
    int     gate_x[6], gate_y[6], ngate;    /* entradas de la zona (pasillos, el puente del pueblo) */
} Wild;

static void wild_init(Wild *w, const Zone *z)
{
    memset(w, 0, sizeof *w);
    w->x0 = z->x0;
    w->y0 = z->y0;
    w->cw = (z->x1 - z->x0) / CELL;
    w->ch = (z->y1 - z->y0) / CELL;
    w->open_n = w->open_s = w->open_e = w->open_w = -1;
    w->skip_w0 = w->skip_w1 = -1;
}

/* Pasto en toda la zona y el bosque del borde. Las celdas pegadas a un
   pasillo quedan libres para que nada lo tape. */
static void wild_borders(Map *m, Wild *w)
{
    map_fill_floor(m, GRASS_DT1, GRASS_STYLE, GRASS_SEQ, w->x0, w->y0, w->cw * CELL, w->ch * CELL);
    for (int cy = 0; cy < w->ch; cy++)
        for (int cx = 0; cx < w->cw; cx++) {
            int west = cx == 0, east = cx == w->cw - 1, north = cy == 0, south = cy == w->ch - 1;
            if (west && cy >= w->skip_w0 && cy <= w->skip_w1) {
                w->used[cy][cx] = 1;
                continue;
            }
            const char *piece = NULL;
            int open = 0;
            if (north && west)      piece = "bord6";
            else if (north && east) piece = "bord7";
            else if (south && west) piece = "bord5";
            else if (south && east) piece = "bord8";
            else if (north)         piece = (open = cx == w->open_n) ? "bord3o" : "bord3";
            else if (south)         piece = (open = cx == w->open_s) ? "bord1o" : "bord1";
            else if (west)          piece = (open = cy == w->open_w) ? "bord2o" : "bord2";
            else if (east)          piece = (open = cy == w->open_e) ? "bord4o" : "bord4";
            if (!piece)
                continue;
            if (open) { /* los pasillos no tienen variantes */
                char path[128];
                snprintf(path, sizeof path, TILES "outdoors/%s.ds1", piece);
                map_stamp(m, path, w->x0 + cx * CELL, w->y0 + cy * CELL);
            } else {
                stamp_border(m, w->x0 + cx * CELL, w->y0 + cy * CELL, piece);
            }
            w->used[cy][cx] = 1;
        }
    /* Las entradas, para los caminos: el borde de adentro de cada pasillo */
    if (w->open_w >= 0 && w->ngate < 6) {
        w->gate_x[w->ngate] = w->x0 + CELL;
        w->gate_y[w->ngate++] = w->y0 + w->open_w * CELL + CELL / 2;
    }
    if (w->open_e >= 0 && w->ngate < 6) {
        w->gate_x[w->ngate] = w->x0 + (w->cw - 1) * CELL - 1;
        w->gate_y[w->ngate++] = w->y0 + w->open_e * CELL + CELL / 2;
    }
    if (w->open_n >= 0 && w->ngate < 6) {
        w->gate_x[w->ngate] = w->x0 + w->open_n * CELL + CELL / 2;
        w->gate_y[w->ngate++] = w->y0 + CELL;
    }
    if (w->open_s >= 0 && w->ngate < 6) {
        w->gate_x[w->ngate] = w->x0 + w->open_s * CELL + CELL / 2;
        w->gate_y[w->ngate++] = w->y0 + (w->ch - 1) * CELL - 1;
    }
    if (w->open_n >= 0) w->used[1][w->open_n] = 1;
    if (w->open_s >= 0) w->used[w->ch - 2][w->open_s] = 1;
    if (w->open_w >= 0) w->used[w->open_w][1] = 1;
    if (w->open_e >= 0) w->used[w->open_e][w->cw - 2] = 1;
    for (int cy = w->skip_w0; cy >= 0 && cy <= w->skip_w1 && cy < MAX_CELLS; cy++)
        w->used[cy][1] = 1;
}

static int wild_free(const Wild *w, int cx, int cy, int cells_w, int cells_h)
{
    if (cx < 1 || cy < 1 || cx + cells_w > w->cw - 1 || cy + cells_h > w->ch - 1)
        return 0;
    for (int y = cy; y < cy + cells_h; y++)
        for (int x = cx; x < cx + cells_w; x++)
            if (w->used[y][x])
                return 0;
    return 1;
}

/* Estampa una pieza de varias celdas en un lugar libre al azar (con la
   columna mínima `min_cx`, para alejarla del pueblo). Si `to` >= 0, sus
   tiles especiales son un salto a esa zona. Devuelve 0, o -1 si no entró. */
static int wild_place(Map *m, Wild *w, const char *path, int cells_w, int cells_h, int min_cx, int from, int to)
{
    for (int tries = 0; tries < 120; tries++) {
        int cx = min_cx + (int)(rnd() % (uint32_t)(w->cw - min_cx - cells_w > 0 ? w->cw - min_cx - cells_w : 1));
        int cy = 1 + (int)(rnd() % (uint32_t)(w->ch - 1 - cells_h > 0 ? w->ch - 1 - cells_h : 1));
        if (!wild_free(w, cx, cy, cells_w, cells_h))
            continue;
        int first = m->num_specials, ox = w->x0 + cx * CELL, oy = w->y0 + cy * CELL;
        map_stamp(m, path, ox, oy);
        w->last_x = ox;
        w->last_y = oy;
        w->last_w = cells_w * CELL;
        w->last_h = cells_h * CELL;
        for (int y = cy; y < cy + cells_h; y++)
            for (int x = cx; x < cx + cells_w; x++)
                w->used[y][x] = 1;
        if (to >= 0)
            add_warp(m, first, ox, oy, cells_w * CELL + 1, cells_h * CELL + 1, -1, from, to);
        return 0;
    }
    fprintf(stderr, "no entró %s\n", path);
    return -1;
}

/* Piezas de relleno del interior (de lvlprest.txt). Los campamentos de
   Fallen traen sus marcas de monstruos: se ponen unos pocos por zona. */
static const char *const k_stones[] = { "circle", "xrock", "6rock", "clover", "arrow", "4rock", "wild5" };
static const char *const k_swamp[] = { "swamp3", "swamp4", "pond1", "puddle1" };
static const char *const k_rural[] = { "cott1", "cott2", "cott3", "cott4", "cott5", "cott6", "cott7", "corral1",
                                       "corral2", "camp" };
static const char *const k_camps[] = {
    "fallcmp", "fallcmp2", "fallcmp3", "fallcmp4", /* Fallen Camp 1 */
    "fallcmp5", "fallcmp6", "fallcmp7", "fallcmp8", /* Fallen Camp 2 */
};
static const char *const k_fences[] = { "wild1", "wild2", "wild3", "wild4" };
static const char *const k_marsh_big[] = { "swamp5" };
static const char *const k_trees[] = { "trees2", "trees3", "ruin2" };

#define COUNT(a) ((int)(sizeof a / sizeof a[0]))

typedef struct {
    const char *const *small; int nsmall; /* rellenos de 8x8 */
    const char *const *big;   int nbig;   /* rellenos de 16x16 */
    int big_tries;
    int small_one_in;                     /* uno de cada N lugares libres */
} WildFill;

static const char *pick(const char *const *names, int n)
{
    return names[rnd() % (uint32_t)n];
}

/* Rellenos grandes en algunos bloques de 2x2 celdas libres, hasta `camps`
   campamentos de Fallen y rellenos chicos en parte de las celdas que quedan */
static void wild_fill(Map *m, Wild *w, int camps, const WildFill *f)
{
    char path[128];
    for (int k = 0; k < camps; k++) {
        snprintf(path, sizeof path, TILES "outdoors/%s.ds1", pick(k_camps, COUNT(k_camps)));
        wild_place(m, w, path, 1, 1, 2, -1, -1);
    }
    for (int tries = 0; tries < f->big_tries; tries++) {
        int cx = 1 + (int)(rnd() % (uint32_t)(w->cw - 3)), cy = 1 + (int)(rnd() % (uint32_t)(w->ch - 3));
        if (!wild_free(w, cx, cy, 2, 2))
            continue;
        snprintf(path, sizeof path, TILES "outdoors/%s.ds1", pick(f->big, f->nbig));
        map_stamp(m, path, w->x0 + cx * CELL, w->y0 + cy * CELL);
        w->used[cy][cx] = w->used[cy][cx + 1] = w->used[cy + 1][cx] = w->used[cy + 1][cx + 1] = 1;
    }
    for (int cy = 1; cy < w->ch - 1; cy++)
        for (int cx = 1; cx < w->cw - 1; cx++) {
            if (w->used[cy][cx] || rnd() % (uint32_t)f->small_one_in != 0)
                continue;
            snprintf(path, sizeof path, TILES "outdoors/%s.ds1", pick(f->small, f->nsmall));
            map_stamp(m, path, w->x0 + cx * CELL, w->y0 + cy * CELL);
            w->used[cy][cx] = 1;
        }
}

/* Lo último que puso wild_place es un lugar al que tiene que llegar un
   camino (si es una cueva, a su boca: el salto que se acaba de agregar) */
static void wild_poi(Wild *w, int placed, int warp)
{
    if (placed != 0 || w->npoi >= 8)
        return;
    int x = w->last_x + w->last_w / 2, y = w->last_y + w->last_h / 2;
    if (warp && s_num_warps > 0) {
        const Warp *wp = &s_warps[s_num_warps - 1];
        x = (int)((wp->x0 + wp->x1) / 2) / TILE_SUB;
        y = (int)((wp->y0 + wp->y1) / 2) / TILE_SUB;
    }
    w->poi_x[w->npoi] = x;
    w->poi_y[w->npoi] = y;
    w->npoi++;
}

/* ---- Caminos de tierra ----
   Como el generador del original: sendas angostas de tierra (town/floor.dt1,
   estilo 0) que unen las entradas de la zona (las puntas de camino que traen
   los pasillos del borde y la salida del pueblo) con la cueva, el waypoint y
   los lugares de las misiones. Cada pieza une los medios de algunos de sus
   cuatro bordes; los bits son NE (y - 1), SE (x + 1), SO (y + 1) y NO (x - 1).
   Qué secuencia es cada combinación sale de cómo están armadas las piezas de
   Blizzard (D2_DS1_FLOORS con --ds1-info). */
enum { PD_NE = 1, PD_SE = 2, PD_SO = 4, PD_NO = 8 };
static const int k_pd_dx[4] = { 0, 1, 0, -1 }, k_pd_dy[4] = { -1, 0, 1, 0 };
static const int k_path_seq[16] = { 56, 15, 13, 3, 14, 8, 4, 9, 16, 21, 7, 35, 19, 10, 37, 33 };

/* Qué bordes une una pieza de camino que ya está en el mapa (-1 = no es) */
static int path_seq_mask(int seq)
{
    static const int alt[][2] = { { 54, 3 }, { 52, 6 }, { 53, 12 }, { 55, 9 }, { 5, 9 },  { 6, 12 }, { 36, 7 },
                                  { 42, 7 }, { 38, 13 }, { 40, 13 }, { 39, 11 }, { 41, 14 }, { 34, 15 } };
    for (int k = 1; k < 16; k++)
        if (k_path_seq[k] == seq)
            return k;
    for (unsigned k = 0; k < sizeof alt / sizeof alt[0]; k++)
        if (alt[k][0] == seq)
            return alt[k][1];
    return -1;
}

/* El piso de pasto o de camino (town/floor.dt1, estilo 0) de ese tile, o -1 */
static int grass_seq(const Map *m, int tx, int ty)
{
    const Dt1Tile *t = map_floor(m, 0, tx, ty);
    if (!t || t->orientation != 0 || t->main_index != GRASS_STYLE)
        return -1;
    for (int f = 0; f < m->num_dt1; f++)
        if (t >= m->dt1[f].tiles && t < m->dt1[f].tiles + m->dt1[f].num_tiles)
            return strcmp(m->dt1_path[f], GRASS_DT1) == 0 ? t->sub_index : -1;
    return -1;
}

/* ¿Pasto limpio, sin nada encima? (ahí puede ir el camino) */
static int path_free(const Map *m, int tx, int ty)
{
    if (grass_seq(m, tx, ty) != GRASS_SEQ || map_floor(m, 1, tx, ty))
        return 0;
    for (int l = 0; l < MAP_WALLS; l++)
        if (map_wall(m, l, tx, ty))
            return 0;
    return 1;
}

typedef struct {
    int x, y; /* tile (dentro de la zona) */
    int bit;  /* hacia dónde está la punta que continúa (0 = un lugar, no una punta) */
} PathEnd;

/* Une (sx, sy) con la red de caminos (net) por el camino más barato sobre
   pasto libre: un poco al azar para que serpentee y lejos de las cosas. Marca
   los tiles nuevos en `mark`. 0 si no llegó. */
static int path_route(const Wild *w, uint8_t *net, uint8_t *mark, const uint8_t *ok, int sx, int sy)
{
    int W = w->cw * CELL, H = w->ch * CELL, n = W * H;
    int *dist = malloc(sizeof(int) * (size_t)n), *prev = malloc(sizeof(int) * (size_t)n);
    int *heap = malloc(sizeof(int) * (size_t)n * 4), hn = 0, found = -1;
    if (!dist || !prev || !heap) {
        free(dist);
        free(prev);
        free(heap);
        return 0;
    }
    for (int i = 0; i < n; i++)
        dist[i] = 1 << 30;
    int s = sy * W + sx;
    dist[s] = 0;
    prev[s] = -1;
    heap[hn++] = s;
    while (hn > 0) {
        /* El de menor costo: montículo binario sobre dist (puede haber
           repetidos con un costo viejo: se saltean) */
        int c = heap[0];
        heap[0] = heap[--hn];
        for (int i = 0;;) {
            int l = 2 * i + 1, r = l + 1, s2 = i;
            if (l < hn && dist[heap[l]] < dist[heap[s2]])
                s2 = l;
            if (r < hn && dist[heap[r]] < dist[heap[s2]])
                s2 = r;
            if (s2 == i)
                break;
            int t = heap[i];
            heap[i] = heap[s2];
            heap[s2] = t;
            i = s2;
        }
        if (net[c]) {
            found = c;
            break;
        }
        int cx = c % W, cy = c / W;
        for (int d = 0; d < 4; d++) {
            int nx = cx + k_pd_dx[d], ny = cy + k_pd_dy[d];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H)
                continue;
            int nc = ny * W + nx;
            if (!ok[nc] && !net[nc])
                continue;
            /* Costo: 10 por tile, hasta 15 de más al azar (fijo por tile) y 8
               si está pegado a algo que no es pasto */
            uint32_t hsh = (uint32_t)(w->x0 + nx) * 73856093u ^ (uint32_t)(w->y0 + ny) * 19349663u;
            int cost = 10 + (int)(hsh % 16u);
            for (int e = 0; e < 4; e++) {
                int ex = nx + k_pd_dx[e], ey = ny + k_pd_dy[e];
                if (ex >= 0 && ey >= 0 && ex < W && ey < H && !ok[ey * W + ex] && !net[ey * W + ex])
                    cost += 8;
            }
            if (dist[c] + cost < dist[nc] && hn < n * 4) {
                dist[nc] = dist[c] + cost;
                prev[nc] = c;
                int i = hn++;
                heap[i] = nc;
                while (i > 0 && dist[heap[(i - 1) / 2]] > dist[heap[i]]) {
                    int t = heap[i];
                    heap[i] = heap[(i - 1) / 2];
                    heap[(i - 1) / 2] = t;
                    i = (i - 1) / 2;
                }
            }
        }
    }
    for (int c = found; c >= 0; c = prev[c])
        if (!net[c]) {
            net[c] = 1;
            mark[c] = 1;
        }
    free(dist);
    free(prev);
    free(heap);
    return found >= 0;
}

/* Traza los caminos de una zona exterior entre sus puntas y sus lugares */
static void wild_paths(Map *m, Wild *w)
{
    int W = w->cw * CELL, H = w->ch * CELL, n = W * H;
    uint8_t *ok = calloc((size_t)n, 1), *net = calloc((size_t)n, 1), *mark = calloc((size_t)n, 1);
    PathEnd ends[32];
    int nends = 0;
    if (!ok || !net || !mark)
        goto done;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            ok[y * W + x] = (uint8_t)path_free(m, w->x0 + x, w->y0 + y);
    /* Las puntas: las entradas de la zona y los lugares, en el pasto libre
       más cercano. Tienen que estar todas en la misma parte caminable (la
       de la primera entrada): si no, el camino no llega. */
    uint8_t *reach = calloc((size_t)n, 1);
    int *queue = malloc(sizeof(int) * (size_t)n);
    if (!reach || !queue) {
        free(reach);
        free(queue);
        goto done;
    }
    for (int k = 0; k < w->ngate + w->npoi && nends < 32; k++) {
        int px = (k < w->ngate ? w->gate_x[k] : w->poi_x[k - w->ngate]) - w->x0;
        int py = (k < w->ngate ? w->gate_y[k] : w->poi_y[k - w->ngate]) - w->y0;
        int best = -1, best_d = 1 << 30;
        for (int y = py - 16; y <= py + 16; y++)
            for (int x = px - 16; x <= px + 16; x++) {
                int d = (x - px) * (x - px) + (y - py) * (y - py);
                if (x >= 0 && y >= 0 && x < W && y < H && ok[y * W + x] && (nends == 0 || reach[y * W + x]) &&
                    d < best_d) {
                    best_d = d;
                    best = y * W + x;
                }
            }
        if (best < 0)
            continue;
        ends[nends++] = (PathEnd){ best % W, best / W, 0 };
        if (nends == 1) { /* lo que se alcanza desde la primera */
            int qh = 0, qt = 0;
            queue[qt++] = best;
            reach[best] = 1;
            while (qh < qt) {
                int c = queue[qh++], cx = c % W, cy = c / W;
                for (int d = 0; d < 4; d++) {
                    int nx = cx + k_pd_dx[d], ny = cy + k_pd_dy[d];
                    if (nx >= 0 && ny >= 0 && nx < W && ny < H && ok[ny * W + nx] && !reach[ny * W + nx]) {
                        reach[ny * W + nx] = 1;
                        queue[qt++] = ny * W + nx;
                    }
                }
            }
        }
    }
    free(reach);
    free(queue);
    if (nends < 2)
        goto done;
    /* La red empieza en la primera punta; las demás se le van uniendo, de la
       más cercana a la más lejana */
    net[ends[0].y * W + ends[0].x] = mark[ends[0].y * W + ends[0].x] = 1;
    for (int k = 1; k < nends; k++) {
        int best = k;
        for (int j = k + 1; j < nends; j++) {
            int dj = (ends[j].x - ends[0].x) * (ends[j].x - ends[0].x) + (ends[j].y - ends[0].y) * (ends[j].y - ends[0].y);
            int db = (ends[best].x - ends[0].x) * (ends[best].x - ends[0].x) +
                     (ends[best].y - ends[0].y) * (ends[best].y - ends[0].y);
            if (dj < db)
                best = j;
        }
        PathEnd tmp = ends[k];
        ends[k] = ends[best];
        ends[best] = tmp;
        path_route(w, net, mark, ok, ends[k].x, ends[k].y);
    }
    /* Cada tile nuevo con la pieza que une sus vecinos de camino (y la punta
       que continúa, si empezó en una) */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (!mark[y * W + x])
                continue;
            int mk = 0;
            for (int d = 0; d < 4; d++) {
                int nx = x + k_pd_dx[d], ny = y + k_pd_dy[d];
                if (nx >= 0 && ny >= 0 && nx < W && ny < H && mark[ny * W + nx])
                    mk |= 1 << d;
            }
            for (int k = 0; k < nends; k++)
                if (ends[k].x == x && ends[k].y == y)
                    mk |= ends[k].bit;
            map_fill_floor(m, GRASS_DT1, GRASS_STYLE, k_path_seq[mk], w->x0 + x, w->y0 + y, 1, 1);
        }
    if (getenv("D2_PATH_DEBUG")) {
        printf("caminos de la zona en (%d, %d): %d puntas y lugares\n", w->x0, w->y0, nends);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int e = 0;
                for (int k = 0; k < nends; k++)
                    e |= ends[k].x == x && ends[k].y == y;
                int seq = grass_seq(m, w->x0 + x, w->y0 + y);
                putchar(e ? 'E' : mark[y * W + x] ? 'o' : path_seq_mask(seq) >= 0 ? '=' : ok[y * W + x] ? '.' : '#');
            }
            putchar('\n');
        }
    }
done:
    free(ok);
    free(net);
    free(mark);
}

static const char *cave_door(void)
{
    return rnd() % 2 ? TILES "caves/cavedr1.ds1" : TILES "caves/cavedr2.ds1";
}

/* ---- Calabozos de salas (lvlmaze.txt) ---- */

/* Un tipo de sala: carpeta (dentro de data/global/tiles), prefijo, tamaño y
   cuántas variantes numeradas tienen las comunes ("catnsew", "catnsew2"...).
   Con `theme`, algunas comunes son la versión temática ("tombnetheme": con
   sarcófagos, estantes...), como DRLGMAZE_RollAct_1_2_3_BasicPresets. */
typedef struct {
    const char *dir, *base;
    int         rw, rh, variants;
    const char *theme;
} MazeStyle;

#define TILES_ROOT "data/global/tiles/"
#define ARC_N      15 /* el Santuario Arcano: 15x15 salas de 12x12 */
#define ARC_ROOM   12

static const MazeStyle k_caves = { "act1/caves", "cave", 24, 24, 2, NULL };
static const MazeStyle k_crypt = { "act1/crypt", "crypt", 8, 8, 2, NULL };
static const MazeStyle k_barracks = { "act1/barracks", "bar", 10, 14, 4, NULL };
static const MazeStyle k_jail = { "act1/barracks", "jail", 12, 12, 4, NULL };
static const MazeStyle k_cat = { "act1/catacomb", "cat", 12, 12, 4, NULL };
/* El Acto 2 (lvlmaze.txt y lvlprest.txt) */
static const MazeStyle k_sewer = { "act2/sewer", "sew", 12, 12, 2, "theme" };
static const MazeStyle k_tomb = { "act2/tomb", "tomb", 16, 16, 4, "theme" };
static const MazeStyle k_lair = { "act2/maggot", "mag", 10, 10, 6, NULL };

/* Una sala especial: el patrón de su nombre (%c = la letra de su salida,
   %s = la variante) y a qué zona lleva su salto (-1 = ninguna; VIS_WARP =
   los de levels.txt). Con `need`, va en una sala con exactamente esas
   salidas (el muelle de las cloacas, de paso de norte a sur; la guarida de
   la reina de los gusanos, abierta al sur); si no, en una punta. */
typedef struct {
    const char *pat;
    const char *first; /* nombre de la primera variante ("" o "1") */
    int         n;     /* cuántas variantes hay */
    int         to;
    int         need;
} MazeRoom;

#define VIS_WARP (-2)

#define MAZE_MAX   8
#define MAZE_ROOMS (MAZE_MAX * MAZE_MAX)

typedef struct {
    int     gw, gh, count;
    uint8_t exits[MAZE_MAX][MAZE_MAX];
    int8_t  room[MAZE_MAX][MAZE_MAX]; /* -1 = sin sala */
    int     dist[MAZE_MAX][MAZE_MAX];
    int     cx[MAZE_ROOMS], cy[MAZE_ROOMS];
} Maze;

static const int  k_dirs[4] = { EX_N, EX_S, EX_E, EX_W };
static const int  k_ddx[4] = { 0, 0, 1, -1 }, k_ddy[4] = { -1, 1, 0, 0 };
static const char k_letters[4] = { 'n', 's', 'e', 'w' };

static int maze_add(Maze *mz, int from, int d)
{
    int x = mz->cx[from], y = mz->cy[from], nx = x + k_ddx[d], ny = y + k_ddy[d];
    if (nx < 0 || ny < 0 || nx >= mz->gw || ny >= mz->gh || mz->room[ny][nx] >= 0 || mz->count >= MAZE_ROOMS)
        return -1;
    mz->exits[y][x] |= (uint8_t)k_dirs[d];
    mz->exits[ny][nx] |= (uint8_t)k_dirs[d ^ 1];
    mz->room[ny][nx] = (int8_t)mz->count;
    mz->dist[ny][nx] = mz->dist[y][x] + 1;
    mz->cx[mz->count] = nx;
    mz->cy[mz->count] = ny;
    return mz->count++;
}

/* Un árbol de salas desde (sx, sy). La primera tiene exactamente las
   salidas `start_exits` (0 = una sola, la que salga) y el resto crece al
   azar hasta `rooms` salas. */
static void maze_make(Maze *mz, int gw, int gh, int sx, int sy, int start_exits, int rooms)
{
    memset(mz, 0, sizeof *mz);
    memset(mz->room, -1, sizeof mz->room);
    mz->gw = gw;
    mz->gh = gh;
    mz->room[sy][sx] = 0;
    mz->cx[0] = sx;
    mz->cy[0] = sy;
    mz->count = 1;
    for (int d = 0; d < 4; d++)
        if (start_exits & k_dirs[d])
            maze_add(mz, 0, d);
    for (int tries = 0; mz->count < rooms && tries < 4000; tries++) {
        int k = !start_exits && mz->count == 1 ? 0 : 1 + (int)(rnd() % (uint32_t)(mz->count - 1));
        maze_add(mz, k, (int)(rnd() % 4));
    }
}

static char exit_letter(int ex)
{
    for (int d = 0; d < 4; d++)
        if (ex == k_dirs[d])
            return k_letters[d];
    return 'n';
}

/* Ruta de una sala especial (una variante al azar entre las que existan) */
static void special_path(char *out, size_t cap, const MazeStyle *st, const MazeRoom *r, int ex)
{
    char name[48];
    int has_c = strstr(r->pat, "%c") != NULL;
    for (int tries = 0; tries < 6; tries++) {
        int k = 1 + (int)(rnd() % (uint32_t)(r->n > 0 ? r->n : 1));
        char var[16];
        if (k == 1)
            snprintf(var, sizeof var, "%s", r->first);
        else
            snprintf(var, sizeof var, "%d", k);
        if (has_c)
            snprintf(name, sizeof name, r->pat, exit_letter(ex), var);
        else
            snprintf(name, sizeof name, r->pat, var);
        snprintf(out, cap, TILES_ROOT "%s/%s.ds1", st->dir, name);
        if (file_exists(out))
            return;
    }
}

/* Ruta de una sala común: el prefijo, sus salidas (en orden n, s, e, w) y
   una variante al azar entre las que existan */
static void exit_letters(char *letters, int ex)
{
    int len = 0;
    for (int d = 0; d < 4; d++)
        if (ex & k_dirs[d])
            letters[len++] = k_letters[d];
    letters[len] = '\0';
}

static void common_path(char *out, size_t cap, const MazeStyle *st, int ex)
{
    char letters[8];
    exit_letters(letters, ex);
    for (int tries = 0; tries < 6; tries++) {
        int k = 1 + (int)(rnd() % (uint32_t)st->variants);
        if (k == 1)
            snprintf(out, cap, TILES_ROOT "%s/%s%s.ds1", st->dir, st->base, letters);
        else
            snprintf(out, cap, TILES_ROOT "%s/%s%s%d.ds1", st->dir, st->base, letters, k);
        if (file_exists(out))
            return;
    }
    snprintf(out, cap, TILES_ROOT "%s/%s%s.ds1", st->dir, st->base, letters);
}

/* Las especiales en sus salas: la k en la sala más lejana de la entrada
   que le sirva (una punta, o la de las salidas que pide). 0 si no entran. */
static int maze_assign(const Maze *mz, const MazeRoom *ends, int nends, int *slot)
{
    uint8_t used[MAZE_ROOMS] = { 1 };
    for (int k = 0; k < nends; k++) {
        int best = -1;
        for (int i = 1; i < mz->count; i++) {
            int ex = mz->exits[mz->cy[i]][mz->cx[i]];
            if (used[i] || (ends[k].need ? ex != ends[k].need : (ex & (ex - 1)) != 0))
                continue;
            if (best < 0 || mz->dist[mz->cy[i]][mz->cx[i]] > mz->dist[mz->cy[best]][mz->cx[best]])
                best = i;
        }
        if (best < 0)
            return k;
        used[best] = 1;
        slot[k] = best;
    }
    return nends;
}

/* Arma un calabozo de salas con la esquina en (ox, oy): la primera sala
   (`start`, la entrada) en (sx, sy) y las especiales `ends` en las puntas
   (o en las salas con las salidas que piden), la primera en la más lejana. */
static void maze_build(Map *m, int zone, const MazeStyle *st, int ox, int oy, int gw, int gh, int rooms, int sx,
                       int sy, int start_exits, const MazeRoom *start, const MazeRoom *ends, int nends)
{
    Maze mz;
    int slot[8], placed = 0;
    nends = nends < 8 ? nends : 8;
    for (int attempt = 0; attempt < 60; attempt++) {
        maze_make(&mz, gw, gh, sx, sy, start_exits, rooms);
        placed = maze_assign(&mz, ends, nends, slot);
        if (placed == nends && mz.count >= rooms * 3 / 4)
            break;
    }
    if (placed < nends)
        fprintf(stderr, "%s: no entraron %d salas especiales\n", s_zones[zone].name, nends - placed);
    /* las temáticas: un quinto de las salas, por lo menos dos */
    uint8_t theme[MAZE_ROOMS] = { 0 };
    for (int n = st->theme ? (mz.count / 5 + 1 > 2 ? mz.count / 5 + 1 : 2) : 0, tries = 0; n > 0 && tries < 40; tries++) {
        int i = 1 + (int)(rnd() % (uint32_t)(mz.count > 1 ? mz.count - 1 : 1)), special = 0;
        for (int k = 0; k < placed; k++)
            special |= slot[k] == i;
        if (!special && !theme[i])
            theme[i] = 1, n--;
    }
    for (int i = 0; i < mz.count; i++) {
        int x = mz.cx[i], y = mz.cy[i], ex = mz.exits[y][x];
        const MazeRoom *sp = i == 0 ? start : NULL;
        for (int k = 0; k < placed; k++)
            if (slot[k] == i)
                sp = &ends[k];
        char path[128];
        if (sp) {
            special_path(path, sizeof path, st, sp, ex);
        } else {
            common_path(path, sizeof path, st, ex);
            if (theme[i] && st->theme) {
                char letters[8], tpath[128];
                exit_letters(letters, ex);
                snprintf(tpath, sizeof tpath, TILES_ROOT "%s/%s%s%s.ds1", st->dir, st->base, letters, st->theme);
                if (file_exists(tpath))
                    snprintf(path, sizeof path, "%s", tpath);
            }
        }
        int first = m->num_specials, rx = ox + x * st->rw, ry = oy + y * st->rh;
        map_stamp(m, path, rx, ry);
        room_add(rx, ry, st->rw, st->rh);
        if (sp && sp->to == VIS_WARP)
            vis_warps(m, first, rx, ry, st->rw + 1, st->rh + 1, zone);
        else if (sp && sp->to >= 0)
            add_warp(m, first, rx, ry, st->rw + 1, st->rh + 1, -1, zone, sp->to);
    }
}

/* ---- Niveles y zonas ---- */

static int round_up(int v)
{
    return (v + CELL - 1) / CELL * CELL;
}

static int round_down(int v)
{
    return v / CELL * CELL;
}

static int add_zone(int kind, int floor, int x0, int y0, int x1, int y1)
{
    if (s_num_zones >= MAX_ZONES)
        return -1;
    int i = s_num_zones++;
    /* Una semilla por zona, mezclada con la de la partida */
    uint32_t h = s_seed ^ (uint32_t)(i + 1) * 0x9E3779B9u;
    h = (h ^ (h >> 15)) * 2246822519u;
    s_zones[i] = (Zone){ kind, floor, (h ^ (h >> 13)) | 1u, 0, round_down(x0), round_down(y0), round_up(x1),
                         round_up(y1), NULL, { { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } } };
    return i;
}

/* Un nivel de la zona, con sus datos de levels.txt (por LevelName) */
/* Lo de la dificultad de un nivel (su fila de levels.txt), como
   MONSTERREGION_InitializeAll: el nivel de los monstruos (MonLvlEx(N) y
   MonLvlEx(H) en Pesadilla e Infierno: la expansión), cuáles salen (nmon1..
   en esas dos) y los jefes al azar (umon solo en Normal: en las otras, de
   los mismos del nivel; MONSTERCHOOSE_GetPresetMonsterId) */
static void level_fill_diff(Level *l, int row)
{
    const TxtTable *t = &s_levels_txt;
    static const char *const k_suf[3] = { "", "(N)", "(H)" };
    const char *suf = k_suf[s_world_diff];
    char col[16];
    if (s_world_diff == 0) /* (los del Acto 5 solo existen en la expansión: su MonLvlEx; la Cámara tiene MonLvl 0) */
        l->mon_level = txt_get_int(t, row, l->id >= 109 ? "MonLvlEx" : "MonLvl", 1);
    else
        l->mon_level = txt_get_int(t, row, s_world_diff == 1 ? "MonLvlEx(N)" : "MonLvlEx(H)", 1);
    memset(l->monsters, 0, sizeof l->monsters);
    memset(l->umons, 0, sizeof l->umons);
    l->num_mon = l->num_umon = 0;
    /* (en Normal, de mon1 a mon5, como siempre en el port; las listas de
       Pesadilla e Infierno llegan a 10) */
    int max = s_world_diff ? LEVEL_MAX_MONSTERS : 5;
    for (int k = 0; k < max; k++) {
        snprintf(col, sizeof col, "%smon%d", s_world_diff ? "n" : "", k + 1);
        const char *id = txt_get(t, row, txt_col(t, col));
        if (id && id[0])
            l->monsters[l->num_mon++] = id;
    }
    for (int k = 0; k < 10; k++) {
        const char *id = NULL;
        if (s_world_diff == 0) {
            snprintf(col, sizeof col, "umon%d", k + 1);
            id = txt_get(t, row, txt_col(t, col));
        } else if (k < l->num_mon) {
            id = l->monsters[k];
        }
        if (id && id[0])
            l->umons[l->num_umon++] = id;
    }
    snprintf(col, sizeof col, "MonUMin%s", suf);
    l->umin = txt_get_int(t, row, col, 0);
    snprintf(col, sizeof col, "MonUMax%s", suf);
    l->umax = txt_get_int(t, row, col, 0);
    int pick_n = txt_get_int(t, row, "NumMon", 0);
    l->num_mon = pick_n > 0 && pick_n < l->num_mon ? pick_n : l->num_mon;
    if (l->town)
        l->num_mon = l->num_umon = l->umin = l->umax = 0;
}

static void add_level(const char *name, int zone, int x0, int y0, int x1, int y1, int groups, int dark)
{
    if (s_num_levels >= MAX_LEVELS || zone < 0)
        return;
    Level *l = &s_levels[s_num_levels];
    memset(l, 0, sizeof *l);
    l->name = name;
    l->index = s_num_levels++;
    l->zone = zone;
    l->x0 = x0;
    l->y0 = y0;
    l->x1 = x1;
    l->y1 = y1;
    l->groups = groups;
    l->dark = dark;
    l->waypoint = -1;
    const TxtTable *t = &s_levels_txt;
    int row = -1;
    if (name[0] == '#') { /* por el Id ("#66"): las siete tumbas de Tal Rasha se llaman igual */
        row = t->rows ? txt_find_row(t, txt_col(t, "Id"), name + 1) : -1;
        if (row >= 0)
            l->name = name = txt_get(t, row, txt_col(t, "LevelName"));
    } else {
        row = t->rows ? txt_find_row(t, txt_col(t, "LevelName"), name) : -1;
    }
    if (!s_zones[zone].name)
        s_zones[zone].name = name;
    if (row < 0) {
        fprintf(stderr, "levels.txt no tiene %s\n", name);
        return;
    }
    l->id = txt_get_int(t, row, "Id", 0);
    l->pal = txt_get_int(t, row, "Pal", s_act - 1);
    l->sound_env = txt_get_int(t, row, "SoundEnv", 2);
    for (int k = 0; k < 8; k++) {
        char col[8], wid[12];
        snprintf(col, sizeof col, "Vis%d", k);
        l->vis[k] = txt_get_int(t, row, col, 0);
        snprintf(col, sizeof col, "Warp%d", k);
        snprintf(wid, sizeof wid, "%d", txt_get_int(t, row, col, -1));
        const TxtTable *lw = &s_lvlwarp_txt;
        int wr = lw->rows ? txt_find_row(lw, txt_col(lw, "Id"), wid) : -1;
        if (wr >= 0 && txt_get_int(lw, wr, "NoInteract", 0))
            l->noint |= 1 << k;
    }
    int wp = txt_get_int(t, row, "Waypoint", 255);
    l->waypoint = wp == 255 ? -1 : wp;
    l->town = strcmp(name, "Rogue Encampment") == 0 || strcmp(name, "Lut Gholein") == 0 ||
              strcmp(name, "Kurast Docktown") == 0 || strcmp(name, "The Pandemonium Fortress") == 0 ||
              strcmp(name, "Harrogath") == 0;
    level_fill_diff(l, row);
    for (int k = 0; k < 8; k++) {
        char col[12];
        snprintf(col, sizeof col, "ObjGrp%d", k);
        l->objgrp[k] = (uint8_t)txt_get_int(t, row, col, 0);
        snprintf(col, sizeof col, "ObjPrb%d", k);
        l->objprb[k] = (uint8_t)txt_get_int(t, row, col, 0);
    }
}

/* Lo que cambia la dificultad en todos los niveles del mundo armado */
void world_set_difficulty(int diff)
{
    s_world_diff = diff < 0 ? 0 : diff > 2 ? 2 : diff;
    const TxtTable *t = &s_levels_txt;
    for (int i = 0; i < s_num_levels; i++) {
        char id[12];
        snprintf(id, sizeof id, "%d", s_levels[i].id);
        int row = t->rows && s_levels[i].id > 0 ? txt_find_row(t, txt_col(t, "Id"), id) : -1;
        if (row >= 0)
            level_fill_diff(&s_levels[i], row);
    }
}

static void add_jump(int a, int b)
{
    for (int i = 0; i < s_num_jumps; i++)
        if ((s_jumps[i][0] == a && s_jumps[i][1] == b) || (s_jumps[i][0] == b && s_jumps[i][1] == a))
            return;
    if (a >= 0 && b >= 0 && a != b && s_num_jumps < MAX_WARPS) {
        s_jumps[s_num_jumps][0] = a;
        s_jumps[s_num_jumps][1] = b;
        s_num_jumps++;
    }
}

static void add_link(int a, int b, float x, float y)
{
    if (a >= 0 && b >= 0 && s_num_links < MAX_LINKS)
        s_links[s_num_links++] = (WorldLink){ a, b, x, y, 0 };
}

/* Lugar para un calabozo en las filas de abajo */
static void pack(int w, int h, int *x, int *y)
{
    w = round_up(w);
    h = round_up(h);
    if (s_pack_x + w > PACK_W) {
        s_pack_x = 0;
        s_pack_y += s_pack_h + PACK_GAP;
        s_pack_h = 0;
    }
    *x = s_pack_x;
    *y = s_pack_y;
    s_pack_x += w + PACK_GAP;
    if (h > s_pack_h)
        s_pack_h = h;
}

/* Un calabozo de salas: su zona y su nivel, del tamaño de la grilla */
static int add_maze(int kind, int floor, const char *name, const MazeStyle *st, int gw, int gh, int groups)
{
    int x, y, w = gw * st->rw + 1, h = gh * st->rh + 1;
    pack(w, h, &x, &y);
    int z = add_zone(kind, floor, x, y, x + w, y + h);
    add_level(name, z, x, y, x + w, y + h, groups, 1);
    return z;
}

/* Un nivel de una sola pieza prearmada */
static int add_preset(int kind, const char *name, int w, int h, int groups, int dark)
{
    int x, y;
    pack(w, h, &x, &y);
    int z = add_zone(kind, 0, x, y, x + w, y + h);
    add_level(name, z, x, y, x + w, y + h, groups, dark);
    return z;
}

/* Una zona exterior de 80x80 en la columna `col` de la fila de afuera */
static int add_wild(int kind, const char *name, int col, int groups)
{
    int x = TOWN_W + col * WILD;
    int z = add_zone(kind, 0, x, OUT_Y, x + WILD, OUT_Y + WILD);
    add_level(name, z, x, OUT_Y, x + WILD, OUT_Y + WILD, groups, 0);
    return z;
}

/* Un pasillo entre dos zonas exteriores pegadas de costado, en la fila `row` */
static void link_side(int west, int east, int row)
{
    add_link(west, east, (float)(s_zones[east].x0 * TILE_SUB), (float)((OUT_Y + row * CELL + CELL / 2) * TILE_SUB));
}

int world_staff_tomb(void)
{
    return s_staff_tomb;
}

int world_level_zone(int id)
{
    return zone_of_level_id(id);
}

/* Lugares fijos del Acto 2, en subtiles: el portal del sótano 3, el del
   medio del Santuario y la llegada al Canyon (la marca de kingwarp.ds1) */
int world_act2_spot(int what, float *x, float *y)
{
    int z = what == 0 ? zone_of_level_id(54) : what == 1 ? zone_of_level_id(74) : z_canyon;
    if (s_act != 2 || z < 0)
        return -1;
    const Zone *zone = &s_zones[z];
    int tx = zone->x0, ty = zone->y0;
    float sx = 0, sy = 0;
    if (what == 0) { /* celse3.ds1: el portal en el subtile (73, 73) */
        sx = 73;
        sy = 73;
    } else if (what == 1) { /* sanctnsew4.ds1, la sala del medio: el portal en (7, 6) */
        tx += (ARC_N / 2) * ARC_ROOM;
        ty += (ARC_N / 2) * ARC_ROOM;
        sx = 7;
        sy = 6;
    } else { /* kingwarp.ds1 en la celda (4, 4): la marca en el tile (6, 6) */
        tx += 4 * CELL + 6;
        ty += 4 * CELL + 6;
        sx = sy = 2.5f;
    }
    *x = (float)(tx * TILE_SUB) + sx;
    *y = (float)(ty * TILE_SUB) + sy;
    return 0;
}

int world_act(void)
{
    return s_act;
}

/* ---- El desierto del Acto 2 ----
   Como DRLG del original (gAct2OutdoorDrlgLink y sus funciones de
   DrlgOutPlace.cpp): el Rocky Waste al oeste de Lut Gholein, alineado arriba
   (lutw.ds1), o al norte, alineado a la izquierda (lutn.ds1); el Dry Hills,
   el Far Oasis y la Lost City pegados a la anterior por uno de sus ocho
   lados y corridos 48 baldosas (comparten un tramo de 32 con el paso en el
   medio), sin pisar a las demás (si no entra, el lado siguiente; si ninguno,
   se cambia la anterior); el Valley of Snakes pegado a la Lost City sin
   correrse. El Canyon of the Magi va aparte (se llega por el portal del
   Arcane Sanctuary). */
typedef struct {
    int x, y, w, h;
} DRect;

/* sub_6FD815E0: dónde queda `b` pegado a `a` por el lado `side` (0..7),
   corrido la mitad de su ancho más 8 si `shift` */
static void desert_side(const DRect *a, DRect *b, int side, int shift)
{
    int sx = shift ? b->w / 2 + 8 : 0, sy = shift ? b->h / 2 + 8 : 0;
    switch (side) {
    case 0: b->x = a->x - sx; b->y = a->y + a->h; break;
    case 1: b->x = a->x + sx; b->y = a->y + a->h; break;
    case 2: b->x = a->x - b->w; b->y = a->y - sy; break;
    case 3: b->x = a->x - b->w; b->y = a->y + sy; break;
    case 4: b->x = a->x - sx; b->y = a->y - b->h; break;
    case 5: b->x = a->x + sx; b->y = a->y - b->h; break;
    case 6: b->x = a->x + a->w; b->y = a->y - sy; break;
    default: b->x = a->x + a->w; b->y = a->y + sy; break;
    }
}

static int rects_overlap(const DRect *a, const DRect *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

/* Pone la zona k de la cadena (y las que siguen) probando sus ocho lados */
static int desert_chain(DRect *r, int k, int n)
{
    if (k >= n)
        return 1;
    int first = (int)(rnd() & 7);
    for (int t = 0; t < 8; t++) {
        desert_side(&r[k - 1], &r[k], (first + t) & 7, k != n - 1); /* el Valley no se corre */
        int clash = 0;
        for (int j = 0; j < k && !clash; j++)
            clash = rects_overlap(&r[j], &r[k]);
        if (!clash && desert_chain(r, k + 1, n))
            return 1;
    }
    return 0;
}

/* Anota un paso en un lado de la zona (puede tener dos vecinas del mismo lado) */
static void zone_add_opening(Zone *z, int side, int cell)
{
    int k = z->open[side][0] < 0 ? 0 : 1;
    z->open[side][k] = (int8_t)cell;
}

/* El paso entre dos zonas que se tocan: en el medio del tramo que comparten
   (como el original: en el desierto la celda del medio y la siguiente, en
   las mesetas del Acto 4 solo la del medio: `cells`), del lado que mira a
   la otra */
static void desert_openings(int za, int zb, int cells)
{
    Zone *a = &s_zones[za], *b = &s_zones[zb];
    int sa, sb, lo, hi, mid, oa;
    float px, py; /* por dónde se cruza caminando */
    if (a->x1 == b->x0 || b->x1 == a->x0) { /* de costado: el tramo es vertical */
        lo = a->y0 > b->y0 ? a->y0 : b->y0;
        hi = a->y1 < b->y1 ? a->y1 : b->y1;
        sa = a->x1 == b->x0 ? 1 : 3;
        sb = sa == 1 ? 3 : 1;
        mid = lo / CELL + (hi - lo) / CELL / 2;
        oa = mid - a->y0 / CELL;
        zone_add_opening(b, sb, mid - b->y0 / CELL);
        px = (float)((sa == 1 ? a->x1 : a->x0) * TILE_SUB);
        py = (float)((mid * CELL + cells * CELL / 2) * TILE_SUB);
    } else { /* arriba o abajo */
        lo = a->x0 > b->x0 ? a->x0 : b->x0;
        hi = a->x1 < b->x1 ? a->x1 : b->x1;
        sa = a->y1 == b->y0 ? 2 : 0;
        sb = sa == 2 ? 0 : 2;
        mid = lo / CELL + (hi - lo) / CELL / 2;
        oa = mid - a->x0 / CELL;
        zone_add_opening(b, sb, mid - b->x0 / CELL);
        px = (float)((mid * CELL + cells * CELL / 2) * TILE_SUB);
        py = (float)((sa == 2 ? a->y1 : a->y0) * TILE_SUB);
    }
    zone_add_opening(a, sa, oa);
    add_link(za, zb, px, py);
}

/* Los calabozos de salas del Acto 2: su Id de levels.txt, cómo se arman,
   la grilla, las salas (lvlmaze.txt; las cloacas 1 con lugar para sus dos
   entradas) y los grupos de monstruos */
static const struct {
    int              id, kind;
    const char      *name;
    const MazeStyle *st;
    int              grid, rooms, groups;
} k_act2_mazes[] = {
    { 47, ZK_SEWER, "Sewers Level 1", &k_sewer, 6, 12, 13 },
    { 48, ZK_SEWER, "Sewers Level 2", &k_sewer, 6, 12, 13 },
    { 49, ZK_SEWER, "Sewers Level 3", &k_sewer, 6, 14, 15 },
    { 65, ZK_SEWER, "Ancient Tunnels", &k_sewer, 5, 10, 11 },
    { 55, ZK_TOMB, "Stony Tomb Level 1", &k_tomb, 4, 4, 8 },
    { 59, ZK_TOMB, "Stony Tomb Level 2", &k_tomb, 4, 4, 8 },
    { 56, ZK_TOMB, "Halls of the Dead Level 1", &k_tomb, 5, 6, 12 },
    { 57, ZK_TOMB, "Halls of the Dead Level 2", &k_tomb, 5, 6, 12 },
    { 60, ZK_TOMB, "Halls of the Dead Level 3", &k_tomb, 5, 6, 12 },
    { 58, ZK_TOMB, "Claw Viper Temple Level 1", &k_tomb, 6, 12, 22 },
    { 62, ZK_LAIR, "Maggot Lair Level 1", &k_lair, 7, 18, 13 },
    { 63, ZK_LAIR, "Maggot Lair Level 2", &k_lair, 6, 14, 10 },
    { 64, ZK_LAIR, "Maggot Lair Level 3", &k_lair, 5, 10, 7 },
    /* las siete de Tal Rasha (la verdadera con el triple de salas y la de Kaa con el doble: act2_maze_size) */
    { 66, ZK_TOMB, "#66", &k_tomb, 5, 6, 12 }, { 67, ZK_TOMB, "#67", &k_tomb, 5, 6, 12 },
    { 68, ZK_TOMB, "#68", &k_tomb, 5, 6, 12 }, { 69, ZK_TOMB, "#69", &k_tomb, 5, 6, 12 },
    { 70, ZK_TOMB, "#70", &k_tomb, 5, 6, 12 }, { 71, ZK_TOMB, "#71", &k_tomb, 5, 6, 12 },
    { 72, ZK_TOMB, "#72", &k_tomb, 5, 6, 12 },
};

/* La grilla y las salas de un calabozo del Acto 2: la tumba verdadera tiene
   el triple de salas y la de Kaa el doble (DRLGMAZE_BuildBasicMaze) */
static void act2_maze_size(int k, int *grid, int *rooms)
{
    int id = k_act2_mazes[k].id;
    *grid = k_act2_mazes[k].grid;
    *rooms = k_act2_mazes[k].rooms;
    if (id == s_staff_tomb) {
        *grid = 7;
        *rooms *= 3;
    } else if (id == s_boss_tomb) {
        *grid = 6;
        *rooms *= 2;
    }
}

static int act2_maze(int id)
{
    for (int k = 0; k < (int)COUNT(k_act2_mazes); k++)
        if (k_act2_mazes[k].id == id)
            return k;
    return 0;
}

/* Un calabozo de salas del Acto 2, como DRLGMAZE_GenerateLevel: la entrada
   y las salas especiales de cada piso (los presets traen los jefes y los
   cofres de las misiones); los saltos salen de levels.txt.
   shortcut: la forma la arma el generador de siempre (no el orden de
   DRLGMAZE_ScanReplaceSpecialAct2SewersPresets) */
static void build_act2_maze(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    int id = zone->floor, k = act2_maze(id), g = k_act2_mazes[k].grid, rooms = k_act2_mazes[k].rooms;
    const MazeStyle *st = k_act2_mazes[k].st;
    MazeRoom e[4];
    int n = 0;
    act2_maze_size(k, &g, &rooms);
    if (zone->kind == ZK_TOMB) {
        /* la entrada con tres salidas (tombsewwarpprev...), lejos del borde */
        static char pat[32];
        char letters[8];
        int exits = (EX_N | EX_S | EX_E | EX_W) & ~k_dirs[rnd() % 4];
        exit_letters(letters, exits);
        snprintf(pat, sizeof pat, "tomb%swarpprev%%s", letters);
        MazeRoom s = { pat, "", 2, VIS_WARP, 0 };
        if (id == 55 || id == 56 || id == 57 || id == 58)
            e[n++] = (MazeRoom){ "tomb%cwarpnext%s", "", 2, VIS_WARP, 0 };
        if (id == 57)
            e[n++] = (MazeRoom){ "tomb%cwaypoint", "", 1, -1, 0 };
        if (id == 59) {
            e[n++] = (MazeRoom){ "tomb%cconsol", "", 1, -1, 0 };
            e[n++] = (MazeRoom){ "tomb%cleatherarm", "", 1, -1, 0 };
            e[n++] = (MazeRoom){ "tomb%ctreasure", "", 1, -1, 0 };
        }
        if (id == 60)
            e[n++] = (MazeRoom){ "tombcube%c", "", 1, -1, 0 };
        if (id >= 66 && id <= 72) /* la verdadera tiene el orificio; las demás, un cofre; en una está Kaa */
            e[n++] = id == s_staff_tomb ? (MazeRoom){ "tomb%ctalrasha", "", 1, -1, 0 } : (MazeRoom){ "tomb%cconsol", "", 1, -1, 0 };
        if (id == s_boss_tomb)
            e[n++] = (MazeRoom){ "tomb%ckaa", "", 1, -1, 0 };
        maze_build(m, z, st, zone->x0, zone->y0, g, g, rooms, 1 + (int)(rnd() % (uint32_t)(g - 2)),
                   1 + (int)(rnd() % (uint32_t)(g - 2)), exits, &s, e, n);
        return;
    }
    MazeRoom s = { zone->kind == ZK_SEWER ? "sew%cup" : "mag%cwarpprev", "", 1, VIS_WARP, 0 };
    switch (id) {
    case 47: /* la bajada y el muelle (la otra boca desde Lut Gholein), de paso de norte a sur */
        e[n++] = (MazeRoom){ "sew%cdown", "", 1, VIS_WARP, 0 };
        e[n++] = (MazeRoom){ "sewnsdock%s", "", 2, VIS_WARP, EX_N | EX_S };
        break;
    case 48:
        e[n++] = (MazeRoom){ "sew%cdown", "", 1, VIS_WARP, 0 };
        e[n++] = (MazeRoom){ "sew%cwaypoint", "", 1, -1, 0 };
        break;
    case 49: /* Radament con el cofre del pergamino */
        e[n++] = (MazeRoom){ "sew%crad", "", 1, -1, 0 };
        break;
    case 65:
        e[n++] = (MazeRoom){ "sew%ccon", "", 1, -1, 0 };
        break;
    case 62: case 63:
        e[n++] = (MazeRoom){ "mag%cwarpnext", "", 1, VIS_WARP, 0 };
        break;
    case 64: /* Coldworm con el cofre del Bastón de los Reyes, abierta al sur, y el cofre único */
        e[n++] = (MazeRoom){ "magqueen", "", 1, -1, EX_S };
        e[n++] = (MazeRoom){ "magw", "", 1, -1, EX_W };
        break;
    }
    maze_build(m, z, st, zone->x0, zone->y0, g, g, rooms, (int)(rnd() % (uint32_t)g), (int)(rnd() % (uint32_t)g), 0,
               &s, e, n);
}

/* El harén y los sótanos del palacio (DRLGMAZE_InitBasicMazeLayout con 2):
   cuatro salas de 16x16 en ronda, cada una con las dos salidas hacia sus
   vecinas ("subse", "celnw2"...). En el sótano 1 la del noroeste es la del
   waypoint; en el 3, la del noroeste y la del sudeste son las "3" (la del
   portal al Santuario y el callejón). */
static void build_palace(Map *m, int z)
{
    static const struct { int x, y; const char *ex; } k_ring[4] = {
        { 0, 0, "se" }, { 1, 0, "sw" }, { 0, 1, "ne" }, { 1, 1, "nw" },
    };
    const Zone *zone = &s_zones[z];
    const char *base = zone->floor == 51 ? "sub" : "cel";
    for (int k = 0; k < 4; k++) {
        const char *var = rnd() % 2 ? "2" : "";
        if (zone->floor == 52 && k == 3)
            var = "waypoint";
        if (zone->floor == 54 && (k == 0 || k == 3))
            var = "3";
        char path[96];
        snprintf(path, sizeof path, TILES2 "palace/%s%s%s.ds1", base, k_ring[k].ex, var);
        int rx = zone->x0 + k_ring[k].x * 16, ry = zone->y0 + k_ring[k].y * 16, first = m->num_specials;
        map_stamp(m, path, rx, ry);
        room_add(rx, ry, 16, 16);
        vis_warps(m, first, rx, ry, 17, 17, z);
    }
}

/* El Santuario Arcano (DRLGMAZE_PlaceArcaneSanctuary): la sala del medio con
   el waypoint y el portal, y cuatro brazos de 15 salas que dan la vuelta
   (adelante, a la izquierda, adelante cuatro, a la derecha, una punta,
   atrás...), cada brazo con su dibujo (sanctnew, sanctnew1...). Cada sala
   nueva se une con todas las vecinas que ya estaban (DRLGMAZE_MergeMazeRooms
   con el Merge 1000 de lvlmaze.txt: siempre); de cada pieza de 13x13 van
   12x12 y donde no tienen piso, el piso en blanco 30.1 que no se camina (el
   vacío debajo de los arcos), como toda pieza (stamp_rules). El
   Invocador (SummN/E/S/W) reemplaza la última sala armada que tenga una sola
   salida, hacia el lado que salió. */
static int  prest_lib_mask(uint32_t mask, int z, char paths[34][96]); /* world3.inc */
static void prest_tables_load(void);

static void build_arcane(Map *m, int z)
{
    static const int k_dx[4] = { -1, 0, 1, 0 }, k_dy[4] = { 0, -1, 0, 1 }; /* ALTDIR: oeste, norte, este, sur */
    static const int k_bit[4] = { EX_W, EX_N, EX_E, EX_S };
    const Zone *zone = &s_zones[z];
    uint8_t ex[ARC_N][ARC_N], side[ARC_N][ARC_N];
    int8_t br[ARC_N][ARC_N];
    int order[ARC_N * ARC_N], n = 0; /* las salas en el orden en que se arman */
    memset(ex, 0, sizeof ex);
    memset(side, 0, sizeof side);
    memset(br, -1, sizeof br);
    int nrand = (int)(rnd() & 3), c = ARC_N / 2;
    br[c][c] = 4;
    order[n++] = c * ARC_N + c;
    for (int b = 0; b < 4; b++) {
        int px = c, py = c;
        for (int i = 0; i < 15 && px >= 0; i++) {
            int d = i == 2 || i == 12 ? b + 3 : i == 7 || i == 9 ? b + 1 : i == 10 || i == 11 || i == 13 || i == 14 ? b + 2 : b;
            d %= 4;
            int nx = px + k_dx[d], ny = py + k_dy[d];
            if (nx < 0 || ny < 0 || nx >= ARC_N || ny >= ARC_N || br[ny][nx] != -1) {
                if (i != 8 && i != 12)
                    px = -1; /* (no pasa: los brazos no se tocan) */
                continue;
            }
            br[ny][nx] = (int8_t)b;
            side[ny][nx] = i == 8 || i == 12; /* (sin nPickedFile: Files es 0, el primer dibujo) */
            order[n++] = ny * ARC_N + nx;
            for (int k = 0; k < 4; k++) { /* la de atrás y las otras vecinas */
                int ax = nx + k_dx[k], ay = ny + k_dy[k];
                if (ax >= 0 && ay >= 0 && ax < ARC_N && ay < ARC_N && br[ay][ax] != -1) {
                    ex[ny][nx] |= (uint8_t)k_bit[k];
                    ex[ay][ax] |= (uint8_t)k_bit[(k + 2) % 4];
                }
            }
            if (i != 8 && i != 12) {
                px = nx;
                py = ny;
            }
        }
    }
    /* el Invocador (DRLGMAZE_ScanReplaceSpecialPreset con nAct2ArcaneSummonerIds):
       la sala abierta solo al norte, al este, al sur o al oeste (la lista del
       nivel empieza por la última armada); si no hay, una nueva pegada (sin
       unirse a las otras vecinas) a la última armada que tenga lugar */
    static const int k_summ[4] = { EX_N, EX_E, EX_S, EX_W };
    static const int k_summ_dir[4] = { 3, 0, 1, 2 }; /* de qué lado de esa sala va la nueva */
    int t = (int)(rnd() & 3), sx = -1, sy = -1;
    for (int k = n - 1; k >= 0 && sx < 0; k--)
        if (br[order[k] / ARC_N][order[k] % ARC_N] < 4 && ex[order[k] / ARC_N][order[k] % ARC_N] == k_summ[t])
            sx = order[k] % ARC_N, sy = order[k] / ARC_N;
    for (int k = n - 1; k >= 0 && sx < 0; k--) {
        int x = order[k] % ARC_N, y = order[k] / ARC_N, d = k_summ_dir[t];
        int nx = x + k_dx[d], ny = y + k_dy[d];
        if (nx < 0 || ny < 0 || nx >= ARC_N || ny >= ARC_N || br[ny][nx] != -1)
            continue;
        br[ny][nx] = br[y][x];
        ex[ny][nx] = (uint8_t)k_bit[(d + 2) % 4];
        ex[y][x] |= (uint8_t)k_bit[d];
        sx = nx, sy = ny;
    }
    /* Los DT1 de las piezas: los del Dt1Mask que tienen todas en lvlprest.txt
       (1: Sanctuary.dt1), Blank (el piso en blanco 30.1) e InvisWal */
    char lib[34][96];
    const char *libp[34];
    prest_tables_load();
    int nlib = prest_lib_mask(1u, z, lib);
    for (int k = 0; k < nlib; k++)
        libp[k] = lib[k];
    for (int y = 0; y < ARC_N; y++)
        for (int x = 0; x < ARC_N; x++) {
            if (br[y][x] < 0)
                continue;
            char letters[8], path[96];
            exit_letters(letters, ex[y][x]);
            if (x == sx && y == sy) {
                snprintf(path, sizeof path, TILES2 "arcane/summ%s.ds1", letters);
            } else if (br[y][x] == 4 && x == c && y == c) {
                snprintf(path, sizeof path, TILES2 "arcane/sanctnsew4.ds1");
            } else {
                int v = side[y][x] ? 0 : (nrand + br[y][x]) % 4, single = (ex[y][x] & (ex[y][x] - 1)) == 0;
                snprintf(path, sizeof path, TILES2 "arcane/sanct%s%s.ds1", letters, single || v == 0 ? "" : v == 1 ? "1" : v == 2 ? "2" : "3");
                if (!file_exists(path))
                    snprintf(path, sizeof path, TILES2 "arcane/sanct%s.ds1", letters);
            }
            map_stamp_lib(m, path, zone->x0 + x * ARC_ROOM, zone->y0 + y * ARC_ROOM, nlib ? libp : NULL, nlib);
            room_add(zone->x0 + x * ARC_ROOM, zone->y0 + y * ARC_ROOM, ARC_ROOM, ARC_ROOM);
        }
}

static int layout_act2(Map *m)
{
    static const char *const k_names[DZ_COUNT] = { "Rocky Waste", "Dry Hills", "Far Oasis", "Lost City",
                                                   "Valley of Snakes" };
    s_lut_north = (int)((rnd() >> 5) & 1);
    DRect r[1 + DZ_COUNT];
    r[0] = (DRect){ 0, 0, 64, 64 }; /* Lut Gholein (56, con lo que el DS1 se pasa redondeado) */
    for (int k = 1; k <= DZ_COUNT; k++)
        r[k].w = r[k].h = k == DZ_COUNT ? VALLEY_W : DESERT_W;
    if (s_lut_north)
        r[1].x = 0, r[1].y = -DESERT_W;
    else
        r[1].x = -DESERT_W, r[1].y = 0;
    if (!desert_chain(r, 2, 1 + DZ_COUNT)) {
        fprintf(stderr, "no pude armar el desierto\n");
        return -1;
    }
    /* Todo en coordenadas positivas, con un margen; el Canyon a la derecha */
    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (int k = 0; k <= DZ_COUNT; k++) {
        minx = r[k].x < minx ? r[k].x : minx;
        miny = r[k].y < miny ? r[k].y : miny;
        maxx = r[k].x + r[k].w > maxx ? r[k].x + r[k].w : maxx;
        maxy = r[k].y + r[k].h > maxy ? r[k].y + r[k].h : maxy;
    }
    int ox = CELL - minx, oy = CELL - miny;
    int lx = r[0].x + ox, ly = r[0].y + oy;
    z_town = add_zone(ZK_LUT, 0, lx, ly, lx + LUT_W, ly + LUT_H);
    add_level("Lut Gholein", z_town, lx, ly, lx + LUT_SIZE, ly + LUT_SIZE, 0, 0);
    for (int k = 0; k < DZ_COUNT; k++) {
        int x = r[k + 1].x + ox, y = r[k + 1].y + oy, w = r[k + 1].w;
        z_desert[k] = add_zone(ZK_DESERT, k, x, y, x + w, y + w);
        add_level(k_names[k], z_desert[k], x, y, x + w, y + w, k == DZ_VALLEY ? 6 : 24, 0);
    }
    int cx = maxx + ox + 2 * CELL, cy = CELL;
    z_canyon = add_zone(ZK_CANYON, 0, cx, cy, cx + DESERT_W, cy + DESERT_W);
    add_level("Canyon of the Magi", z_canyon, cx, cy, cx + DESERT_W, cy + DESERT_W, 24, 0);
    /* Los calabozos, en filas debajo */
    s_pack_x = 0;
    s_pack_y = (maxy + oy + CELL > cy + DESERT_W + CELL ? maxy + oy + CELL : cy + DESERT_W + CELL) + PACK_GAP;
    s_pack_h = 0;
    /* las tumbas de Tal Rasha: la verdadera y la de Kaa (DRLG_InitAct2: distintas, al azar en cada partida) */
    do {
        s_staff_tomb = 66 + (int)(rnd() % 7u);
        s_boss_tomb = 66 + (int)(rnd() % 7u);
    } while (s_staff_tomb == s_boss_tomb);
    for (int k = 0; k < (int)COUNT(k_act2_mazes); k++) {
        int g, rooms;
        act2_maze_size(k, &g, &rooms);
        add_maze(k_act2_mazes[k].kind, k_act2_mazes[k].id, k_act2_mazes[k].name, k_act2_mazes[k].st, g, g,
                 k_act2_mazes[k].groups * rooms / k_act2_mazes[k].rooms);
    }
    add_preset(ZK_SERPENT, "Claw Viper Temple Level 2", 17, 17, 6, 1);
    /* el palacio: la planta baja (sin monstruos), el harén y los tres sótanos */
    add_preset(ZK_HAREM1, "Harem Level 1", 17, 20, 0, 0);
    static const char *const k_palace[4] = { "Harem Level 2", "Palace Cellar Level 1", "Palace Cellar Level 2",
                                             "Palace Cellar Level 3" };
    for (int k = 0; k < 4; k++) {
        int x, y;
        pack(33, 33, &x, &y);
        int z = add_zone(ZK_PALACE, 51 + k, x, y, x + 33, y + 33);
        add_level(k_palace[k], z, x, y, x + 33, y + 33, 10, 1);
    }
    {
        int x, y, w = ARC_N * ARC_ROOM + 1;
        pack(w, w, &x, &y);
        int z = add_zone(ZK_ARCANE, 74, x, y, x + w, y + w);
        add_level("Arcane Sanctuary", z, x, y, x + w, y + w, 30, 1);
    }
    add_preset(ZK_DURIEL, "Duriel's Lair", 33, 48, 0, 1);
    /* Los saltos entre niveles, de las Vis de levels.txt (para buscar caminos), y
       los portales: el del sótano 3 al Santuario, el del diario al Canyon y el
       del orificio a la guarida de Duriel */
    for (int i = 0; i < s_num_levels; i++)
        for (int k = 0; k < 8; k++)
            add_jump(s_levels[i].zone, zone_of_level_id(s_levels[i].vis[k]));
    add_jump(zone_of_level_id(54), zone_of_level_id(74));
    add_jump(zone_of_level_id(74), z_canyon);
    add_jump(zone_of_level_id(s_staff_tomb), zone_of_level_id(73));
    int width = cx + DESERT_W + CELL > PACK_W ? cx + DESERT_W + CELL : PACK_W, height = s_pack_y + s_pack_h;
    if (map_create(m, width, height) != 0)
        return -1;
    /* Los pasos: del pueblo al Rocky Waste (la pieza de transición) y entre
       cada zona del desierto y la siguiente */
    if (s_lut_north) /* el hueco de la muralla: columnas 15 a 21 de lutn.ds1, filas 8 a 19 de lutw.ds1 */
        add_link(z_town, z_desert[DZ_ROCKY], (float)((lx + 18) * TILE_SUB), (float)(ly * TILE_SUB));
    else
        add_link(z_town, z_desert[DZ_ROCKY], (float)(lx * TILE_SUB), (float)((ly + 13) * TILE_SUB));
    for (int k = 1; k < DZ_COUNT; k++)
        desert_openings(z_desert[k - 1], z_desert[k], 2);
    if (getenv("D2_DEBUG")) {
        printf("mundo del Acto 2 de %dx%d tiles (Lut Gholein %s), %d zonas; la tumba verdadera es la %d y Kaa está en la %d\n",
               width, height, s_lut_north ? "con salida al norte" : "con salida al oeste", s_num_zones,
               s_staff_tomb - 65, s_boss_tomb - 65);
        for (int z = 0; z < s_num_zones; z++)
            printf("  zona %2d: %-20s (%d, %d)-(%d, %d) pasos N %d %d E %d %d S %d %d O %d %d\n", z, s_zones[z].name,
                   s_zones[z].x0, s_zones[z].y0, s_zones[z].x1, s_zones[z].y1, s_zones[z].open[0][0],
                   s_zones[z].open[0][1], s_zones[z].open[1][0], s_zones[z].open[1][1], s_zones[z].open[2][0],
                   s_zones[z].open[2][1], s_zones[z].open[3][0], s_zones[z].open[3][1]);
    }
    return 0;
}

#include "world3.inc"
#include "world4.inc"
#include "world5.inc"

void world_set_arena(int on)
{
    s_world_arena = on != 0;
}

/* Los monstruos de los niveles de un acto (mon1..mon5 de levels.txt, sin
   repetir), para las oleadas del Eco del Odio */
int world_act_monsters(int act, const char **out, int max)
{
    const TxtTable *t = &s_levels_txt;
    int n = 0, cact = txt_col(t, "Act");
    for (int r = 0; r < t->rows && n < max; r++) {
        if (txt_get_int(t, r, "Act", -1) != act - 1 || cact < 0)
            continue;
        for (int k = 0; k < 5 && n < max; k++) {
            char col[8];
            snprintf(col, sizeof col, "mon%d", k + 1);
            const char *id = txt_get(t, r, txt_col(t, col));
            int dup = !id || !id[0];
            for (int j = 0; j < n && !dup; j++)
                dup = strcmp(out[j], id) == 0;
            if (!dup)
                out[n++] = id;
        }
    }
    return n;
}

int world_layout(Map *m, uint32_t seed, int act)
{
    s_seed = seed ? seed : 1;
    s_rng = s_seed;
    s_act = act >= 1 && act <= 5 ? act : 1;
    s_num_levels = s_num_zones = s_num_warps = s_num_links = s_num_jumps = 0;
    map_set_stamp_rules(getenv("D2_NO_STAMP_RULES") ? NULL : stamp_rules); /* (depuración: sin KillEdge ni FillBlanks) */
    z_cows = -1;
    if (!s_levels_txt.rows) {
        size_t n;
        uint8_t *d = fs_read("data/global/excel/levels.txt", &n);
        if (!d || txt_load(d, n, &s_levels_txt) != 0)
            fprintf(stderr, "falta data/global/excel/levels.txt\n");
        free(d);
        d = fs_read("data/global/excel/lvlwarp.txt", &n);
        if (!d || txt_load(d, n, &s_lvlwarp_txt) != 0)
            fprintf(stderr, "falta data/global/excel/lvlwarp.txt\n");
        free(d);
    }
    if (s_act == 2)
        return layout_act2(m);
    if (s_act == 3)
        return layout_act3(m);
    if (s_act == 4)
        return layout_act4(m);
    if (s_act == 5)
        return layout_act5(m);

    /* Pasillos entre las zonas exteriores, en filas al azar (la del Blood
       Moor no puede ser la del pueblo) */
    s_moor_row = 1 + (int)(rnd() % (uint32_t)(WILD_CELLS - 2));
    s_plains_row = 1 + (int)(rnd() % (uint32_t)(WILD_CELLS - 2));
    s_dark_row = 1 + (int)(rnd() % (uint32_t)(WILD_CELLS - 2));
    s_marsh_row = 1 + (int)(rnd() % (uint32_t)(WILD_CELLS - 2));
    s_grave_c0 = 1 + (int)(rnd() % (uint32_t)(WILD_CELLS - GRAVE_CW - 1));
    s_tamoe_col = 2 + (int)(rnd() % 5u); /* la abertura hacia el monasterio, debajo de la vereda */

    /* El pueblo y las zonas exteriores */
    z_town = add_zone(ZK_TOWN, 0, 0, OUT_Y + TOWN_CELL0 * CELL, TOWN_W, OUT_Y + TOWN_CELL0 * CELL + TOWN_H);
    add_level("Rogue Encampment", z_town, 0, OUT_Y + TOWN_CELL0 * CELL, TOWN_W, OUT_Y + TOWN_CELL0 * CELL + TOWN_H, 0, 0);
    z_moor = add_wild(ZK_MOOR, "Blood Moor", 0, 22);
    z_plains = add_wild(ZK_PLAINS, "Cold Plains", 1, 24);
    z_stony = add_wild(ZK_STONY, "Stony Field", 2, 24);
    z_dark = add_wild(ZK_DARKWOOD, "Dark Wood", 3, 24);
    z_marsh = add_wild(ZK_MARSH, "Black Marsh", 4, 24);
    z_tamoe = add_wild(ZK_TAMOE, "Tamoe Highland", 5, 26);
    int gx = s_zones[z_plains].x0 + s_grave_c0 * CELL, gy = OUT_Y + WILD;
    z_graves = add_zone(ZK_GRAVES, 0, gx, gy, gx + GRAVE_CW * CELL, gy + GRAVE_CH * CELL);
    add_level("Burial Grounds", z_graves, gx, gy, gx + GRAVE_CW * CELL, gy + GRAVE_CH * CELL, 5, 0);

    /* El monasterio arriba del Tamoe Highland: la vereda de la fachada
       pegada al borde norte y el claustro arriba de la fachada (su puerta
       de abajo coincide con la del salón) */
    int fx = s_zones[z_tamoe].x0 + CELL, fy = OUT_Y - FACADE_H;
    s_court_x = fx;
    s_court_y = fy - (COURT_H - 1);
    z_monastery = add_zone(ZK_MONASTERY, 0, fx, s_court_y, fx + FACADE_W, OUT_Y);
    add_level("Monastery Gate", z_monastery, fx, fy, fx + FACADE_W, OUT_Y, 6, 0);
    add_level("Outer Cloister", z_monastery, s_court_x, s_court_y, s_court_x + COURT_W, fy, 10, 0);
    /* Los Barracones al oeste del claustro: la sala de unión (courtwb) en la
       columna del este de la grilla, con su puerta en la del claustro */
    {
        int conn_x = s_court_x - (k_barracks.rw), conn_y = s_court_y + 20;
        int gw = 5, gh = 5, row = 3;
        int bx = conn_x - (gw - 1) * k_barracks.rw, by = conn_y - row * k_barracks.rh;
        z_barracks = add_zone(ZK_BARRACKS, 0, bx, by, s_court_x, by + gh * k_barracks.rh + 1);
        add_level("Barracks", z_barracks, bx, by, s_court_x, by + gh * k_barracks.rh + 1, 14, 1);
    }

    /* Calabozos */
    s_pack_x = 0;
    s_pack_y = OUT_Y + WILD + GRAVE_CH * CELL + PACK_GAP;
    s_pack_h = 0;
    z_den = add_maze(ZK_DEN, 0, "Den of Evil", &k_caves, 5, 5, 22);
    z_passage = add_maze(ZK_PASSAGE, 0, "Underground Passage Level 1", &k_caves, 4, 4, 14);
    static const char *const jail_names[3] = { "Jail Level 1", "Jail Level 2", "Jail Level 3" };
    static const char *const cat_names[3] = { "Catacombs Level 1", "Catacombs Level 2", "Catacombs Level 3" };
    for (int k = 1; k <= 3; k++)
        z_jail[k] = add_maze(ZK_JAIL, k, jail_names[k - 1], &k_jail, k == 3 ? 6 : 5, k == 3 ? 6 : 5, 12 + 2 * k);
    for (int k = 1; k <= 3; k++)
        z_cat[k] = add_maze(ZK_CATACOMBS, k, cat_names[k - 1], &k_cat, k == 3 ? 6 : 5, k == 3 ? 6 : 5, 12 + 2 * k);
    z_cat[4] = add_preset(ZK_ANDARIEL, "Catacombs Level 4", 23, 32, 6, 1);
    /* El Claustro Interior debajo de la Catedral: la puerta de arriba del
       claustro coincide con la de abajo de la catedral */
    {
        int x, y;
        pack(CATH_W, CATH_H - 1 + INNER_H, &x, &y);
        z_cloister = add_zone(ZK_CLOISTER, 0, x, y, x + CATH_W, y + CATH_H - 1 + INNER_H);
        add_level("Cathedral", z_cloister, x, y, x + CATH_W, y + CATH_H - 1, 8, 1);
        add_level("Inner Cloister", z_cloister, x + 4, y + CATH_H - 1, x + 4 + INNER_W, y + CATH_H - 1 + INNER_H, 4, 0);
    }
    z_tristram = add_preset(ZK_TRISTRAM, "Tristram", 44, 49, 10, 0);
    z_tower = add_preset(ZK_TOWER, "Forgotten Tower", 9, 9, 0, 1);
    static const char *const cellar_names[5] = { "Tower Cellar Level 1", "Tower Cellar Level 2",
                                                 "Tower Cellar Level 3", "Tower Cellar Level 4", "Tower Cellar Level 5" };
    for (int k = 1; k <= 4; k++)
        z_cellar[k] = add_maze(ZK_CELLAR, k, cellar_names[k - 1], &k_crypt, 4, 4, 6);
    z_cellar[5] = add_preset(ZK_COUNTESS, cellar_names[4], 31, 31, 4, 1);
    /* La Granja Muu Muu (Id 39): un páramo cerrado de 80x80 al que solo se
       llega por el portal rojo del cubo (cows.inc). Los grupos, como los del
       Blood Moor por su MonDen (800 contra 520) */
    z_cows = add_preset(ZK_COWS, "Moo Moo Farm", WILD, WILD, 22 * 800 / 520, 0);

    int width = s_zones[z_tamoe].x1 > PACK_W ? s_zones[z_tamoe].x1 : PACK_W;
    int height = s_pack_y + s_pack_h;
    if (map_create(m, width, height) != 0)
        return -1;

    /* Pasos caminando */
    add_link(z_town, z_moor, (float)(s_zones[z_moor].x0 * TILE_SUB),
             (float)((OUT_Y + TOWN_CELL0 * CELL + TOWN_H / 2) * TILE_SUB));
    link_side(z_moor, z_plains, s_moor_row);
    link_side(z_plains, z_stony, s_plains_row);
    link_side(z_dark, z_marsh, s_dark_row);
    link_side(z_marsh, z_tamoe, s_marsh_row);
    add_link(z_plains, z_graves, (float)((gx + (GRAVE_CW / 2) * CELL + CELL / 2) * TILE_SUB), (float)(gy * TILE_SUB));
    add_link(z_tamoe, z_monastery, (float)((s_zones[z_tamoe].x0 + s_tamoe_col * CELL + CELL / 2) * TILE_SUB),
             (float)(OUT_Y * TILE_SUB));
    add_link(z_monastery, z_barracks, (float)(s_court_x * TILE_SUB), (float)((s_court_y + 27) * TILE_SUB + 2.5f));
    /* Los saltos que arman las zonas (cuevas, escaleras) */
    add_jump(z_moor, z_den);
    add_jump(z_stony, z_passage);
    add_jump(z_stony, z_tristram); /* el portal de las piedras (cuando se abre) */
    add_jump(z_passage, z_dark);
    add_jump(z_marsh, z_tower);
    add_jump(z_tower, z_cellar[1]);
    for (int k = 1; k < 5; k++)
        add_jump(z_cellar[k], z_cellar[k + 1]);
    add_jump(z_barracks, z_jail[1]);
    add_jump(z_jail[1], z_jail[2]);
    add_jump(z_jail[2], z_jail[3]);
    add_jump(z_jail[3], z_cloister);
    add_jump(z_cloister, z_cat[1]);
    for (int k = 1; k < 4; k++)
        add_jump(z_cat[k], z_cat[k + 1]);
    if (getenv("D2_DEBUG")) {
        printf("mundo de %dx%d tiles, %d zonas y %d niveles\n", width, height, s_num_zones, s_num_levels);
        for (int z = 0; z < s_num_zones; z++)
            printf("  zona %2d: %-28s (%d, %d)-(%d, %d)\n", z, s_zones[z].name, s_zones[z].x0, s_zones[z].y0,
                   s_zones[z].x1, s_zones[z].y1);
    }
    return 0;
}

/* ---- Armado de cada zona ---- */

static const WildFill k_fill_plain = { k_stones, COUNT(k_stones), k_fences, COUNT(k_fences), 6, 3 };
static const WildFill k_fill_stony = { k_stones, COUNT(k_stones), k_fences, COUNT(k_fences), 4, 2 };
static const WildFill k_fill_wood = { k_stones, COUNT(k_stones), k_trees, COUNT(k_trees), 14, 3 };
static const WildFill k_fill_marsh = { k_swamp, COUNT(k_swamp), k_marsh_big, COUNT(k_marsh_big), 8, 2 };
static const WildFill k_fill_rural = { k_rural, COUNT(k_rural), k_fences, COUNT(k_fences), 6, 3 };

/* La celda de la Granja Muu Muu donde llega el portal rojo: sale de la
   semilla de la zona (se sabe antes de armarla) y queda libre de piezas */
static void cow_arrival_cell(int *cx, int *cy)
{
    uint32_t h = s_zones[z_cows].seed * 2654435761u;
    *cx = 1 + (int)((h >> 8) % (uint32_t)(WILD_CELLS - 2));
    *cy = 1 + (int)((h >> 20) % (uint32_t)(WILD_CELLS - 2));
}

int world_cow_arrival(float *x, float *y)
{
    if (z_cows < 0)
        return -1;
    int cx, cy;
    cow_arrival_cell(&cx, &cy);
    *x = (float)((s_zones[z_cows].x0 + cx * CELL + CELL / 2) * TILE_SUB) + 0.5f;
    *y = (float)((s_zones[z_cows].y0 + cy * CELL + CELL / 2) * TILE_SUB) + 0.5f;
    return 0;
}

static void build_wild(Map *m, int z)
{
    Zone *zone = &s_zones[z];
    Wild w;
    wild_init(&w, zone);
    switch (zone->kind) {
    case ZK_MOOR:
        w.skip_w0 = TOWN_CELL0;
        w.skip_w1 = TOWN_CELL0 + TOWN_H / CELL - 1;
        w.open_e = s_moor_row;
        wild_borders(m, &w);
        map_stamp(m, TRANS_DS1, zone->x0, OUT_Y + TOWN_CELL0 * CELL);
        /* El camino sale del puente del pueblo (fila 153 del mapa del pueblo) */
        w.gate_x[w.ngate] = zone->x0 + CELL;
        w.gate_y[w.ngate++] = TOWN_BRIDGE_Y;
        /* La entrada de la Den of Evil: el montículo con la boca, lejos del pueblo */
        wild_poi(&w, wild_place(m, &w, cave_door(), 1, 1, 4, z, z_den), 1);
        wild_fill(m, &w, 4, &k_fill_plain);
        break;
    case ZK_PLAINS: /* el waypoint, el campamento de Bishibosh y el camino al cementerio */
        w.open_w = s_moor_row;
        w.open_e = s_plains_row;
        w.open_s = s_grave_c0 + GRAVE_CW / 2;
        wild_borders(m, &w);
        wild_poi(&w, wild_place(m, &w, WAYPOINT_DS1, 1, 1, 2, -1, -1), 0);
        wild_poi(&w, wild_place(m, &w, BISHI_DS1, 1, 1, 3, -1, -1), 0);
        wild_fill(m, &w, 3, &k_fill_plain);
        break;
    case ZK_STONY: /* el waypoint, las Piedras Cairn (con Rakanishu) y el Pasaje Subterráneo */
        w.open_w = s_plains_row;
        wild_borders(m, &w);
        wild_poi(&w, wild_place(m, &w, WAYPOINT_DS1, 1, 1, 1, -1, -1), 0);
        wild_poi(&w, wild_place(m, &w, CAIRN_DS1, 1, 1, 3, -1, -1), 0);
        wild_poi(&w, wild_place(m, &w, cave_door(), 1, 1, 5, z, z_passage), 1);
        wild_fill(m, &w, 0, &k_fill_stony);
        break;
    case ZK_DARKWOOD: /* el waypoint, el Árbol de Inifuss (con Treehead) y la salida del pasaje */
        w.open_e = s_dark_row;
        wild_borders(m, &w);
        wild_poi(&w, wild_place(m, &w, cave_door(), 1, 1, 1, z, z_passage), 1);
        wild_poi(&w, wild_place(m, &w, WAYPOINT_DS1, 1, 1, 2, -1, -1), 0);
        wild_poi(&w, wild_place(m, &w, INIFUSS_DS1, 1, 1, 4, -1, -1), 0);
        wild_fill(m, &w, 1, &k_fill_wood);
        break;
    case ZK_MARSH: /* el waypoint, la Torre Olvidada y el Tomo Mohoso */
        w.open_w = s_dark_row;
        w.open_e = s_marsh_row;
        wild_borders(m, &w);
        wild_poi(&w, wild_place(m, &w, WAYPOINT_DS1, 1, 1, 1, -1, -1), 0);
        if (wild_place(m, &w, TOWER_DS1, 1, 1, 3, z, z_tower) == 0 && s_num_warps > 0) {
            wild_poi(&w, 0, 0);
            /* El salto está en el medio de la torre, rodeado de paredes: se
               entra desde la puerta con A (y al salir se queda afuera) */
            s_warps[s_num_warps - 1].margin = 9.0f;
            s_warps[s_num_warps - 1].fixed_margin = 1;
        }
        wild_poi(&w, wild_place(m, &w, TOME_DS1, 1, 1, 2, -1, -1), 0);
        wild_fill(m, &w, 2, &k_fill_marsh);
        break;
    case ZK_TAMOE: /* el camino al monasterio */
        w.open_w = s_marsh_row;
        w.open_n = s_tamoe_col;
        wild_borders(m, &w);
        wild_fill(m, &w, 2, &k_fill_rural);
        break;
    case ZK_GRAVES: /* el cementerio ocupa el interior (3x4 celdas) */
        w.open_n = GRAVE_CW / 2;
        wild_borders(m, &w);
        map_stamp(m, GRAVE_DS1, zone->x0 + CELL, zone->y0 + CELL);
        break;
    case ZK_COWS: { /* DRLGOUTWILD_SpawnSpecialPresets: una pieza de cada uno, sin río, caminos ni waypoint */
        int acx, acy;
        cow_arrival_cell(&acx, &acy);
        wild_borders(m, &w);
        w.used[acy][acx] = 1; /* (ahí llega el portal) */
        wild_place(m, &w, TILES "outdoors/bivouac.ds1", 2, 2, 1, -1, -1); /* con el Rey Vaca en el medio */
        wild_place(m, &w, TILES "outdoors/pond1.ds1", 1, 1, 1, -1, -1);
        wild_place(m, &w, rnd() % 2 ? TILES "outdoors/corral1.ds1" : TILES "outdoors/corral2.ds1", 1, 1, 1, -1, -1);
        wild_place(m, &w, TILES "outdoors/swamp3.ds1", 1, 1, 1, -1, -1);
        wild_place(m, &w, TILES "outdoors/swamp5.ds1", 2, 2, 1, -1, -1);
        char path[128]; /* Stone Fill 1 y 2: los tres primeros y los tres siguientes de k_stones */
        snprintf(path, sizeof path, TILES "outdoors/%s.ds1", pick(k_stones, 3));
        wild_place(m, &w, path, 1, 1, 1, -1, -1);
        snprintf(path, sizeof path, TILES "outdoors/%s.ds1", pick(k_stones + 3, 3));
        wild_place(m, &w, path, 1, 1, 1, -1, -1);
        break;
    }
    }
    /* Los caminos de tierra entre las entradas y los lugares, como el original */
    if (zone->kind != ZK_GRAVES && zone->kind != ZK_COWS) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        wild_paths(m, &w);
        if (getenv("D2_PATH_DEBUG"))
            printf("caminos de %s: %.1f ms\n", zone->name,
                   (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
    }
}

static void build_maze(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    int x = zone->x0, y = zone->y0;
    switch (zone->kind) {
    case ZK_DEN: { /* la entrada y la sala de Corpsefire en dos puntas, bien lejos */
        static const MazeRoom start = { "cave%cpre%s", "1", 2, -1, 0 }, end = { "cave%cspec", "", 1, -1, 0 };
        MazeRoom s = start;
        s.to = z_moor;
        maze_build(m, z, &k_caves, x, y, 5, 5, 10, (int)(rnd() % 5), (int)(rnd() % 5), 0, &s, &end, 1);
        break;
    }
    case ZK_PASSAGE: {
        MazeRoom s = { "cave%cpre%s", "1", 2, z_stony, 0 }, e = { "cave%cnext%s", "1", 2, z_dark, 0 };
        maze_build(m, z, &k_caves, x, y, 4, 4, 6, (int)(rnd() % 4), (int)(rnd() % 4), 0, &s, &e, 1);
        break;
    }
    case ZK_BARRACKS: { /* desde la sala de unión con el claustro; la forja con el Mazo y la escalera a la Cárcel */
        MazeRoom s = { "courtwb", "", 1, -1, 0 };
        MazeRoom e[2] = { { "bar%cwarp", "", 1, z_jail[1], 0 }, { "bar%cforge", "", 1, -1, 0 } };
        maze_build(m, z, &k_barracks, s_court_x - 5 * k_barracks.rw, s_court_y + 20 - 3 * k_barracks.rh, 5, 5, 10,
                   4, 3, EX_W, &s, e, 2);
        break;
    }
    case ZK_JAIL: {
        int f = zone->floor, g = f == 3 ? 6 : 5;
        MazeRoom s = { "jail%cwarpprev", "", 1, f == 1 ? z_barracks : z_jail[f - 1], 0 };
        MazeRoom e[2] = { { "jail%cwarpnext", "", 1, f < 3 ? z_jail[f + 1] : -1, 0 }, { NULL, "", 1, -1, 0 } };
        if (f == 1) e[1] = (MazeRoom){ "jail%cway", "", 1, -1, 0 };
        if (f == 2) e[1] = (MazeRoom){ "jail%cpitspawn", "", 1, -1, 0 };
        if (f == 3) e[0] = (MazeRoom){ "jail%cwarpcath", "", 1, z_cloister, 0 };
        maze_build(m, z, &k_jail, x, y, g, g, 10 + 2 * f, (int)(rnd() % (uint32_t)g), (int)(rnd() % (uint32_t)g), 0,
                   &s, e, e[1].pat ? 2 : 1);
        break;
    }
    case ZK_CATACOMBS: { /* el primero empieza en la sala de la escalera a la catedral (cuatro salidas) */
        int f = zone->floor, g = f == 3 ? 6 : 5, ew = rnd() % 2;
        MazeRoom s = { ew ? "catewup%s" : "catnsup%s", "", 4, z_cat[f - 1], 0 };
        int exits = ew ? EX_E | EX_W : EX_N | EX_S;
        if (f == 1) {
            s = (MazeRoom){ "catnsewexit", "", 1, z_cloister, 0 };
            exits = EX_N | EX_S | EX_E | EX_W;
        }
        MazeRoom e[2] = { { "cat%cdown%s", "", 2, z_cat[f + 1], 0 }, { "cat%cway", "", 1, -1, 0 } };
        maze_build(m, z, &k_cat, x, y, g, g, 10 + 2 * f, 1 + (int)(rnd() % (uint32_t)(g - 2)),
                   1 + (int)(rnd() % (uint32_t)(g - 2)), exits, &s, e, f == 2 ? 2 : 1);
        break;
    }
    case ZK_UBERDEN: {
        /* Como el Mausoleo (DRLGMAZE, LVLTYPE_ACT1_CRYPT): salas de 8x8, 36 en Infierno (lvlmaze.txt), la de
           la llegada (CryptWarpPrev: ahí está el portal rojo de vuelta) y la del cofre brillante
           (CryptConsol: ahí sale Lilith, uber.inc) en la punta más lejana */
        MazeRoom s = { "crypt%cwarpprev", "", 1, -1, 0 };
        MazeRoom e = { "crypt%cconsol", "", 1, -1, 0 };
        maze_build(m, z, &k_crypt, x, y, MAZE_MAX, MAZE_MAX, 36, (int)(rnd() % MAZE_MAX), (int)(rnd() % MAZE_MAX), 0, &s,
                   &e, 1);
        break;
    }
    case ZK_CELLAR: {
        int f = zone->floor;
        MazeRoom s = { "crypt%cwarpprev", "", 1, f == 1 ? z_tower : z_cellar[f - 1], 0 };
        MazeRoom e = { "crypt%cwarpnext", "", 1, z_cellar[f + 1], 0 };
        maze_build(m, z, &k_crypt, x, y, 4, 4, 6, (int)(rnd() % 4), (int)(rnd() % 4), 0, &s, &e, 1);
        break;
    }
    }
}

/* ---- Armado de las zonas del desierto ---- */

#define OUT2 TILES2 "outdoors/"

static void stamp_preset(Map *m, int z, const char *path, int x, int y, int w, int h, int style, int to);

/* Una pieza de lvlprest.txt con sus archivos (se elige uno al azar) */
typedef struct {
    const char *files[6];
    int         n, cw, ch; /* cuántos archivos y el tamaño en celdas */
} DPreset;

static const DPreset k_oasis1 = { { OUT2 "oasis1.ds1", OUT2 "oasis2.ds1", OUT2 "oasis3.ds1", OUT2 "oasis4.ds1" }, 4, 1, 1 };
static const DPreset k_oasis2 = { { OUT2 "oasis5.ds1", OUT2 "oasis6.ds1", OUT2 "oasis7.ds1", OUT2 "oasis8.ds1" }, 4, 2, 2 };
static const DPreset k_oasis3 = { { OUT2 "oasis9.ds1", OUT2 "oasis10.ds1", OUT2 "oasis11.ds1", OUT2 "oasis12.ds1" }, 4, 2, 2 };
static const DPreset k_ruins8 = { { OUT2 "ruin4.ds1", OUT2 "ruin5.ds1", OUT2 "ruin6.ds1", OUT2 "ruin7.ds1" }, 4, 1, 1 };
static const DPreset k_bone1 = { { OUT2 "bone1.ds1" }, 1, 1, 1 };
static const DPreset k_bone2 = { { OUT2 "bone2.ds1" }, 1, 1, 1 };
static const DPreset k_head1 = { { OUT2 "head1.ds1" }, 1, 1, 1 };
static const DPreset k_head2 = { { OUT2 "head2.ds1" }, 1, 1, 1 };
static const DPreset k_mesa = { { OUT2 "mesarm.ds1" }, 1, 1, 1 };
static const DPreset k_wagon = { { OUT2 "wagon2.ds1" }, 1, 1, 1 };
static const DPreset k_berms1 = { { OUT2 "desert1.ds1", OUT2 "desert2.ds1", OUT2 "desert3.ds1", OUT2 "desert4.ds1",
                                    OUT2 "desert5.ds1" }, 5, 1, 1 };
static const DPreset k_berms2 = { { OUT2 "desert6.ds1", OUT2 "desert7.ds1", OUT2 "desert8.ds1", OUT2 "desert9.ds1",
                                    OUT2 "desert10.ds1" }, 5, 1, 1 };
static const DPreset k_berms3 = { { OUT2 "burm1.ds1", OUT2 "burm2.ds1", OUT2 "burm3.ds1", OUT2 "burm4.ds1" }, 4, 1, 1 };
static const DPreset k_berms4 = { { OUT2 "burm5.ds1", OUT2 "burm6.ds1", OUT2 "burm7.ds1" }, 3, 1, 1 };
static const DPreset k_ruins16 = { { OUT2 "ruin1.ds1" }, 1, 2, 2 };
static const DPreset k_ruins16x8 = { { OUT2 "ruin3.ds1" }, 1, 2, 1 };
static const DPreset k_ruins8x16 = { { OUT2 "ruin2.ds1" }, 1, 1, 2 };
static const DPreset k_elder = { { OUT2 "ruindarkelder.ds1" }, 1, 2, 2 };
static const DPreset k_vruin1 = { { TILES2 "ruin/ruin1.ds1", TILES2 "ruin/ruin2.ds1", TILES2 "ruin/ruin3.ds1",
                                    TILES2 "ruin/ruin4.ds1", TILES2 "ruin/ruin5.ds1", TILES2 "ruin/ruin6.ds1" }, 6, 1, 1 };
static const DPreset k_vruin2 = { { TILES2 "ruin/ruin7.ds1", TILES2 "ruin/ruin8.ds1", TILES2 "ruin/ruin9.ds1",
                                    TILES2 "ruin/ruin10.ds1", TILES2 "ruin/head3.ds1" }, 5, 1, 1 };

/* DRLGOUTDESR_PlacePresetVariants: una de cada (o todos sus archivos si
   `all`), empezando por una al azar */
static void desert_variants(Map *m, Wild *w, const DPreset *const *list, int n, int all)
{
    int first = (int)(rnd() % (uint32_t)n);
    for (int i = 0; i < n; i++) {
        const DPreset *p = list[(first + i) % n];
        if (all)
            for (int f = 0; f < p->n; f++)
                wild_place(m, w, p->files[f], p->cw, p->ch, 1, -1, -1);
        else
            wild_place(m, w, p->files[rnd() % (uint32_t)p->n], p->cw, p->ch, 1, -1, -1);
    }
}

/* Una pieza en una celda fija (los acantilados, las tumbas del Canyon), que
   la ocupa */
static void desert_fixed(Map *m, Wild *w, const char *path, int cx, int cy, int cw, int ch, int z, int to)
{
    stamp_preset(m, z, path, w->x0 + cx * CELL, w->y0 + cy * CELL, cw * CELL + 1, ch * CELL + 1, -1, to);
    for (int y = cy; y < cy + ch && y < MAX_CELLS; y++)
        for (int x = cx; x < cx + cw && x < MAX_CELLS; x++)
            w->used[y][x] = 1;
}

/* Los acantilados que cruzan el Dry Hills, el Far Oasis y la Lost City:
   las ocho formas de DRLGOUTDESR_PlaceCliffs (de derecha a izquierda en la
   fila 4, con un camino de escalones en una de sus partes; en ele desde el
   medio; o de arriba abajo en la columna 4). Si una punta cae en el paso
   hacia otra zona, la forma siguiente. */
static void desert_cliffs(Map *m, Wild *w, int z, const Zone *zone)
{
    enum { RE, RW, RP, LE, LW, LP, TOP };
    static const struct { int kind, file, x, y; } k_forms[8][5] = {
        { { RE, 1, 0, 4 }, { RP, -1, 2, 4 }, { RW, -1, 4, 4 }, { RW, -1, 6, 4 }, { RE, 2, 8, 4 } },
        { { RE, 1, 0, 4 }, { RW, -1, 2, 4 }, { RP, -1, 4, 4 }, { RW, -1, 6, 4 }, { RE, 2, 8, 4 } },
        { { RE, 1, 0, 4 }, { RW, -1, 2, 4 }, { RW, -1, 4, 4 }, { RP, -1, 6, 4 }, { RE, 2, 8, 4 } },
        { { RE, 2, 8, 4 }, { RW, -1, 6, 4 }, { TOP, -1, 4, 4 }, { LP, -1, 4, 6 }, { LE, 2, 4, 8 } },
        { { RE, 2, 8, 4 }, { RP, -1, 6, 4 }, { TOP, -1, 4, 4 }, { LW, -1, 4, 6 }, { LE, 2, 4, 8 } },
        { { LE, 1, 4, 0 }, { LP, -1, 4, 2 }, { LW, -1, 4, 4 }, { LW, -1, 4, 6 }, { LE, 2, 4, 8 } },
        { { LE, 1, 4, 0 }, { LW, -1, 4, 2 }, { LP, -1, 4, 4 }, { LW, -1, 4, 6 }, { LE, 2, 4, 8 } },
        { { LE, 1, 4, 0 }, { LW, -1, 4, 2 }, { LW, -1, 4, 4 }, { LP, -1, 4, 6 }, { LE, 2, 4, 8 } },
    };
    static const char *const k_ends_r[3] = { NULL, TILES2 "bigcliff/cliffru.ds1", TILES2 "bigcliff/cliffrl.ds1" };
    static const char *const k_ends_l[3] = { NULL, TILES2 "bigcliff/clifflu.ds1", TILES2 "bigcliff/cliffll.ds1" };
    static const char *const k_walls_r[2] = { TILES2 "bigcliff/cliffrght1.ds1", TILES2 "bigcliff/cliffrght2.ds1" };
    static const char *const k_walls_l[3] = { TILES2 "bigcliff/cliffleft1.ds1", TILES2 "bigcliff/cliffleft2.ds1",
                                              TILES2 "bigcliff/cliffleft3.ds1" };
    int form = (int)(rnd() & 7);
    for (int t = 0; t < 8; t++, form = (form + 1) & 7) { /* que no tape un paso */
        int clash = 0;
        for (int i = 0; i < 5; i++) {
            int x = k_forms[form][i].x, y = k_forms[form][i].y, k = k_forms[form][i].kind;
            int x1 = x + (k == RE || k == RW || k == RP || k == TOP ? 1 : 0), y1 = y + (k >= LE ? 1 : 0);
            for (int s = 0; s < 8; s++) {
                int o = zone->open[s / 2][s % 2], side = s / 2;
                if (o < 0)
                    continue;
                int ex = side == 1 ? w->cw - 1 : side == 3 ? 0 : -1, ey = side == 2 ? w->ch - 1 : side == 0 ? 0 : -1;
                for (int c = o; c <= o + 1; c++) {
                    int cx = ex >= 0 ? ex : c, cy = ey >= 0 ? ey : c;
                    clash |= cx >= x && cx <= x1 && cy >= y && cy <= y1;
                }
            }
        }
        if (!clash)
            break;
    }
    for (int i = 0; i < 5; i++) {
        int k = k_forms[form][i].kind, f = k_forms[form][i].file, x = k_forms[form][i].x, y = k_forms[form][i].y;
        const char *path = k == RE   ? k_ends_r[f]
                           : k == LE ? k_ends_l[f]
                           : k == RW ? k_walls_r[rnd() % 2]
                           : k == LW ? k_walls_l[rnd() % 3]
                           : k == RP ? TILES2 "bigcliff/cliffrp.ds1"
                           : k == LP ? TILES2 "bigcliff/clifflp.ds1"
                                     : TILES2 "bigcliff/cor.ds1";
        desert_fixed(m, w, path, x, y, k == TOP || k <= RP ? 2 : 1, k == TOP || k >= LE ? 2 : 1, z, -1);
    }
}

/* El borde de una zona del desierto (PlaceAct1245OutdoorBorders): la meseta
   alrededor con las piezas edge1..8 (las mismas posiciones que los bord del
   Acto 1: 1 abajo, 2 a la izquierda, 3 arriba, 4 a la derecha, 5..8 las
   esquinas) y los pasos de dos celdas hacia las vecinas (edge9..12, como
   nDesertBorderIds). Las celdas ya ocupadas (la transición al pueblo, las
   puntas de los acantilados) se saltean. */
static void desert_borders(Map *m, Wild *w, const Zone *zone)
{
    /* los pasos: arriba 10 11, derecha 11 12 (de arriba abajo), abajo 9 12, izquierda 10 9 */
    static const int k_pass[4][2] = { { 10, 11 }, { 11, 12 }, { 9, 12 }, { 10, 9 } };
    for (int cy = 0; cy < w->ch; cy++)
        for (int cx = 0; cx < w->cw; cx++) {
            int west = cx == 0, east = cx == w->cw - 1, north = cy == 0, south = cy == w->ch - 1;
            if ((!west && !east && !north && !south) || w->used[cy][cx])
                continue;
            int piece = north && west ? 6 : north && east ? 7 : south && west ? 5 : south && east ? 8
                      : north ? 3 : south ? 1 : west ? 2 : 4;
            for (int s = 0; s < 8; s++) {
                int side = s / 2, o = zone->open[side][s % 2], along = side == 0 || side == 2 ? cx : cy;
                int on = side == 0 ? north : side == 1 ? east : side == 2 ? south : west;
                if (o >= 0 && on && (along == o || along == o + 1))
                    piece = k_pass[side][along - o];
            }
            char path[96];
            snprintf(path, sizeof path, OUT2 "edge%d.ds1", piece);
            map_stamp(m, path, w->x0 + cx * CELL, w->y0 + cy * CELL);
            w->used[cy][cx] = 1;
        }
    /* nada adelante de los pasos (en el Valley of Snakes no hay lugar: ahí va la entrada del templo) */
    for (int s = 0; s < 8 && w->cw > 4; s++) {
        int side = s / 2, o = zone->open[side][s % 2];
        for (int c = o; o >= 0 && c <= o + 1; c++) {
            int cx = side == 1 ? w->cw - 2 : side == 3 ? 1 : c, cy = side == 2 ? w->ch - 2 : side == 0 ? 1 : c;
            if (cx >= 0 && cy >= 0 && cx < w->cw && cy < w->ch)
                w->used[cy][cx] = 1;
        }
    }
}

/* La entrada a un calabozo en un lugar libre, con sus saltos de levels.txt */
static void desert_exit(Map *m, Wild *w, const char *path, int cells, int z)
{
    int first = m->num_specials;
    if (wild_place(m, w, path, cells, cells, 1, z, -1) == 0)
        vis_warps(m, first, w->last_x, w->last_y, w->last_w + 1, w->last_h + 1, z);
}

/* Una zona del desierto como DRLGOUTDESR_InitAct2OutdoorLevel: la arena, la
   transición al pueblo (el Rocky Waste), los acantilados, el borde, la
   entrada de su calabozo, el waypoint y los rellenos de cada una.
   shortcut: sin los santuarios (el motor todavía no los tiene) ni el borde
   secundario de lvlsub.txt (DRLGTILESUB_AddSecondaryBorder) */
static void build_desert(Map *m, int z)
{
    Zone *zone = &s_zones[z];
    Wild w;
    wild_init(&w, zone);
    map_fill_floor(m, DESERT_DT1, 0, DESERT_SEQ, w.x0, w.y0, w.cw * CELL, w.ch * CELL);
    static const DPreset *const k_fill_rocky[] = { &k_oasis1, &k_ruins8, &k_bone1, &k_bone2, &k_head1, &k_mesa, &k_wagon };
    static const DPreset *const k_fill_dry[] = { &k_oasis1, &k_ruins8, &k_head2, &k_mesa };
    static const DPreset *const k_fill_dry_berms[] = { &k_berms1, &k_berms2, &k_berms3, &k_berms4 };
    static const DPreset *const k_fill_oasis_big[] = { &k_oasis2, &k_oasis3 };
    static const DPreset *const k_fill_oasis[] = { &k_ruins8, &k_head1, &k_mesa, &k_wagon };
    static const DPreset *const k_fill_oasis_small[] = { &k_oasis1 };
    static const DPreset *const k_fill_lost_ruins[] = { &k_elder, &k_ruins16, &k_ruins16x8, &k_ruins8x16 };
    static const DPreset *const k_fill_lost[] = { &k_oasis1, &k_head2, &k_mesa, &k_berms1, &k_berms2 };
    static const DPreset *const k_fill_lost_small[] = { &k_ruins8 };
    static const char *const k_tombs[4] = { OUT2 "tombent1.ds1", OUT2 "tombent2.ds1", OUT2 "tombent3.ds1", OUT2 "tombent4.ds1" };
    switch (zone->floor) {
    case DZ_ROCKY: /* la transición pegada al pueblo */
        if (s_lut_north)
            desert_fixed(m, &w, TILES2 "town/transn1.ds1", 0, w.ch - 1, LUT_SIZE / CELL, 1, z, -1);
        else
            desert_fixed(m, &w, TILES2 "town/transw2.ds1", w.cw - 1, 0, 1, LUT_SIZE / CELL, z, -1);
        desert_borders(m, &w, zone);
        desert_exit(m, &w, k_tombs[rnd() % 4], 1, z); /* la Stony Tomb */
        desert_variants(m, &w, k_fill_rocky, COUNT(k_fill_rocky), 0);
        break;
    case DZ_DRY:
        desert_cliffs(m, &w, z, zone);
        desert_borders(m, &w, zone);
        desert_exit(m, &w, k_tombs[rnd() % 4], 1, z); /* los Halls of the Dead */
        wild_place(m, &w, OUT2 "waypoint.ds1", 1, 1, 1, -1, -1);
        desert_variants(m, &w, k_fill_dry, COUNT(k_fill_dry), 0);
        desert_variants(m, &w, k_fill_dry_berms, COUNT(k_fill_dry_berms), 1);
        break;
    case DZ_OASIS:
        desert_cliffs(m, &w, z, zone);
        desert_borders(m, &w, zone);
        desert_exit(m, &w, TILES2 "maggot/hole1.ds1", 1, z); /* el Maggot Lair */
        desert_variants(m, &w, k_fill_oasis_big, COUNT(k_fill_oasis_big), 0);
        wild_place(m, &w, OUT2 "waypoint.ds1", 1, 1, 1, -1, -1);
        desert_variants(m, &w, k_fill_oasis, COUNT(k_fill_oasis), 0);
        desert_variants(m, &w, k_fill_oasis_small, 1, 1);
        desert_variants(m, &w, k_fill_oasis_small, 1, 1);
        break;
    case DZ_LOST:
        desert_cliffs(m, &w, z, zone);
        desert_borders(m, &w, zone);
        desert_exit(m, &w, OUT2 "ruinsewr.ds1", 1, z); /* las Ancient Tunnels (una trampilla) */
        desert_variants(m, &w, k_fill_lost_ruins, COUNT(k_fill_lost_ruins), 0);
        wild_place(m, &w, OUT2 "waypoint.ds1", 1, 1, 1, -1, -1);
        desert_variants(m, &w, k_fill_lost, COUNT(k_fill_lost), 0);
        desert_variants(m, &w, k_fill_lost_small, 1, 1);
        desert_variants(m, &w, k_fill_lost_small, 1, 1);
        break;
    case DZ_SANDS: /* DRLGOUTDESR, LEVEL_PANDEMONIUMRUN2: el KingWarp en la celda 4,4 (ahí se llega), el borde y los rellenos */
        desert_fixed(m, &w, OUT2 "kingwarp.ds1", 4, 4, 1, 1, z, -1);
        desert_borders(m, &w, zone);
        desert_variants(m, &w, k_fill_rocky, COUNT(k_fill_rocky), 0);
        desert_variants(m, &w, k_fill_lost, COUNT(k_fill_lost), 0);
        break;
    default: /* el Valley of Snakes: la entrada del Claw Viper Temple en el medio */
        desert_borders(m, &w, zone);
        desert_exit(m, &w, OUT2 "viper1.ds1", 2, z);
        break;
    }
}

/* El Canyon of the Magi (DRLGOUTDESR_PlaceTombEntriesInCanyon): el
   acantilado arriba y a la izquierda con las siete entradas a las tumbas
   (tres arriba, tres a la izquierda y una en la esquina), el waypoint en
   el medio, el borde y los rellenos. Se llega por el portal del diario de
   Horazon (al lado del waypoint) o caminando desde las tumbas. */
static void build_canyon(Map *m, int z)
{
    Zone *zone = &s_zones[z];
    Wild w;
    wild_init(&w, zone);
    map_fill_floor(m, DESERT_DT1, 0, DESERT_SEQ, w.x0, w.y0, w.cw * CELL, w.ch * CELL);
    static const struct { const char *path; int x, y, cw, ch; } k_tombs[9] = {
        { TILES2 "bigcliff/kingrghtmesa.ds1", 8, 0, 2, 1 }, { TILES2 "bigcliff/talrght3.ds1", 6, 0, 2, 1 },
        { TILES2 "bigcliff/talrght2.ds1", 4, 0, 2, 1 },     { TILES2 "bigcliff/talrght1.ds1", 2, 0, 2, 1 },
        { TILES2 "bigcliff/talcor.ds1", 0, 0, 2, 2 },       { TILES2 "bigcliff/talleft1.ds1", 0, 2, 1, 2 },
        { TILES2 "bigcliff/talleft2.ds1", 0, 4, 1, 2 },     { TILES2 "bigcliff/talleft3.ds1", 0, 6, 1, 2 },
        { TILES2 "bigcliff/kinglftmesa.ds1", 0, 8, 1, 2 },
    };
    for (int k = 0; k < 9; k++) { /* el estilo de cada entrada es su tumba (Vis1..7 del Canyon) */
        int first = m->num_specials;
        desert_fixed(m, &w, k_tombs[k].path, k_tombs[k].x, k_tombs[k].y, k_tombs[k].cw, k_tombs[k].ch, z, -1);
        vis_warps(m, first, w.x0 + k_tombs[k].x * CELL, w.y0 + k_tombs[k].y * CELL, k_tombs[k].cw * CELL + 1,
                  k_tombs[k].ch * CELL + 1, z);
    }
    desert_fixed(m, &w, OUT2 "kingwarp.ds1", 4, 4, 1, 1, z, -1);
    desert_borders(m, &w, zone);
    static const DPreset *const k_fill[] = { &k_bone1, &k_bone2, &k_berms3, &k_berms4, &k_wagon };
    static const DPreset *const k_ruins[] = { &k_vruin1, &k_vruin2 };
    desert_variants(m, &w, k_fill, COUNT(k_fill), 0);
    desert_variants(m, &w, k_ruins, COUNT(k_ruins), 1);
}

/* Estampa una pieza prearmada y, si `to` >= 0, hace un salto con sus
   tiles especiales del estilo `style` (-1 = cualquiera) */
static void stamp_preset(Map *m, int z, const char *path, int x, int y, int w, int h, int style, int to)
{
    int first = m->num_specials;
    map_stamp(m, path, x, y);
    if (to >= 0)
        add_warp(m, first, x, y, w, h, style, z, to);
}

/* El armado en partes: la zona a medias, cuántas piezas ya se estamparon y
   si ya están todas (1: falta la colisión, 2: falta el grosor de las paredes) */
static int s_bz_zone = -1, s_bz_done, s_bz_stamped;

/* Una pasada del generador de la zona: con la misma semilla decide lo
   mismo cada vez, y estampa las piezas que map_piece_now deja */
static void zone_generate(Map *m, int z)
{
    Zone *zone = &s_zones[z];
    s_rng = zone->seed;
    s_num_rooms = 0;
    s_rooms_zone = z;
    int x = zone->x0, y = zone->y0;
    switch (zone->kind) {
    case ZK_TOWN:
        map_stamp(m, TOWN_DS1, 0, OUT_Y + TOWN_CELL0 * CELL);
        break;
    case ZK_MOOR: case ZK_PLAINS: case ZK_STONY: case ZK_DARKWOOD: case ZK_MARSH: case ZK_TAMOE: case ZK_GRAVES:
    case ZK_COWS:
        build_wild(m, z);
        break;
    case ZK_DEN: case ZK_PASSAGE: case ZK_BARRACKS: case ZK_JAIL: case ZK_CATACOMBS: case ZK_CELLAR: case ZK_UBERDEN:
        build_maze(m, z);
        break;
    case ZK_MONASTERY: /* el claustro y la fachada encima (la fachada pisa la fila que comparten) */
        map_stamp(m, COURT_DS1, s_court_x, s_court_y);
        map_stamp(m, FACADE_DS1, s_court_x, OUT_Y - FACADE_H);
        break;
    case ZK_CLOISTER: /* la catedral con su escalera a las Catacumbas y el claustro con la de la Cárcel */
        stamp_preset(m, z, CATHEDRAL_DS1, x, y, CATH_W, CATH_H, -1, z_cat[1]);
        stamp_preset(m, z, INNER_DS1, x + 4, y + CATH_H - 1, INNER_W, INNER_H, -1, z_jail[3]);
        break;
    case ZK_ANDARIEL:
        stamp_preset(m, z, ANDY_DS1, x, y, 23, 32, -1, z_cat[3]);
        break;
    case ZK_TRISTRAM:
        map_stamp(m, TRISTRAM_DS1, x, y);
        break;
    case ZK_TOWER: { /* la planta baja: una escalera sale al pantano y la otra baja al sótano */
        int first = m->num_specials;
        map_stamp(m, TOWER2_DS1, x, y);
        add_warp(m, first, x, y, 9, 9, 0, z, z_marsh);
        add_warp(m, first, x, y, 9, 9, 1, z, z_cellar[1]);
        break;
    }
    case ZK_COUNTESS:
        stamp_preset(m, z, rnd() % 2 ? TILES "crypt/cryptcountess1.ds1" : TILES "crypt/cryptcountess2.ds1", x, y, 31, 31,
                     -1, z_cellar[4]);
        break;
    case ZK_LUT: { /* las dos bocas de las cloacas y la puerta del palacio */
        int first = m->num_specials;
        map_stamp(m, s_lut_north ? LUTN_DS1 : LUT_DS1, x, y);
        vis_warps(m, first, x, y, LUT_W, LUT_H, z);
        break;
    }
    case ZK_DESERT:
        build_desert(m, z);
        break;
    case ZK_CANYON:
        build_canyon(m, z);
        break;
    case ZK_SEWER: case ZK_TOMB: case ZK_LAIR:
        build_act2_maze(m, z);
        break;
    case ZK_HAREM1: { /* la planta baja del palacio: la puerta a Lut Gholein y las escaleras al harén */
        int first = m->num_specials;
        map_stamp(m, TILES2 "palace/harem2.ds1", x, y);
        vis_warps(m, first, x, y, 17, 20, z);
        break;
    }
    case ZK_PALACE:
        build_palace(m, z);
        break;
    case ZK_ARCANE:
        build_arcane(m, z);
        break;
    case ZK_DURIEL: /* se entra y se sale por portales (el orificio, Tyrael) */
        map_stamp(m, TILES2 "tomb/duriel.ds1", x, y);
        break;
    case ZK_DOCK:
        prest_stamp(m, z, LP_TOWN3, 0, x, y);
        break;
    case ZK_JUNGLE:
        build_jungle(m, z);
        break;
    case ZK_KURAST:
        build_kurast(m, z);
        break;
    case ZK_CAUSEWAY:
        prest_stamp(m, z, LP_BRIDGE, 0, x, y);
        break;
    case ZK_TRAVINCAL:
        build_travincal(m, z);
        break;
    case ZK_A3MAZE:
        build_act3_maze(m, z);
        break;
    case ZK_A3PRESET: {
        int def = k_act3_presets[act3_preset(zone->floor)].def;
        if (zone->floor == 114 && rnd() % 2 == 0) /* el Frozen River: River A o River B */
            def = 1039;
        prest_stamp(m, z, def, -1, x, y);
        break;
    }
    case ZK_FORTRESS:
        prest_stamp(m, z, LP_FORTRESS, 0, x, y);
        break;
    case ZK_HARROGATH:
        prest_stamp(m, z, LP_HARROGATH, 0, x, y);
        break;
    case ZK_SIEGE:
        build_siege(m, z);
        break;
    case ZK_BARRICADE:
        build_barricade(m, z);
        break;
    case ZK_MESA:
        build_mesa(m, z);
        break;
    case ZK_RIVER:
        build_river(m, z);
        break;
    case ZK_SANCTUM:
        build_sanctum(m, z);
        break;
    case ZK_SERPENT: { /* el templo de la víbora: una sola sala con el altar */
        int first = m->num_specials;
        map_stamp(m, TILES2 "tomb/serpent1.ds1", x, y);
        vis_warps(m, first, x, y, 17, 17, z);
        break;
    }
    }
}

int world_zone_build_step(Map *m, int z, double budget_ms)
{
    if (z < 0 || z >= s_num_zones)
        return -1;
    Zone *zone = &s_zones[z];
    if (zone->loaded)
        return 1;
    if (s_bz_zone >= 0 && s_bz_zone != z && world_zone_build_step(m, s_bz_zone, 0) < 0) /* (la de antes, entera) */
        return -1;
    if (s_bz_zone != z) {
        if (map_begin_zone(m, z, zone->x0, zone->y0, zone->x1, zone->y1) != 0)
            return -1;
        s_bz_zone = z;
        s_bz_done = s_bz_stamped = 0;
    }
    if (!s_bz_stamped) {
        m->piece = m->pieces_skipped = 0;
        m->piece_from = s_bz_done;
        m->piece_to = -1;
        m->piece_until = budget_ms > 0 ? map_now_ms() + budget_ms : 0;
        zone_generate(m, z);
        int stopped = m->piece_to >= 0;
        s_bz_done = stopped ? m->piece_to : m->piece;
        m->piece_from = m->pieces_skipped = 0;
        m->piece_to = -1;
        m->piece_until = 0;
        if (stopped)
            return 0;
        s_bz_stamped = 1;
        if (budget_ms > 0)
            return 0; /* la colisión, en las próximas dos */
    }
    if (s_bz_stamped == 1) {
        map_build_collision_part(m, z, 0);
        s_bz_stamped = 2;
        if (budget_ms > 0)
            return 0;
    }
    map_build_collision_part(m, z, 1);
    warp_margins(m, z);
    links_settle(m, z);
    zone->loaded = 1;
    s_bz_zone = -1;
    return 1;
}

int world_zone_build(Map *m, int z)
{
    return world_zone_build_step(m, z, 0) < 0 ? -1 : 0;
}

int world_zone_building(void)
{
    return s_bz_zone;
}

void world_zone_free(Map *m, int z)
{
    if (z >= 0 && z == s_bz_zone) { /* a medio armar: se tira lo que haya */
        s_bz_zone = -1;
        s_zones[z].loaded = 1;
    }
    if (z < 0 || z >= s_num_zones || !s_zones[z].loaded)
        return;
    map_free_zone(m, z);
    int n = 0;
    for (int i = 0; i < s_num_warps; i++)
        if (s_warps[i].from != z || s_warps[i].portal)
            s_warps[n++] = s_warps[i];
    s_num_warps = n;
    s_zones[z].loaded = 0;
}

void world_zone_rect(int z, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = *y0 = *x1 = *y1 = 0;
    if (z < 0 || z >= s_num_zones)
        return;
    *x0 = s_zones[z].x0;
    *y0 = s_zones[z].y0;
    *x1 = s_zones[z].x1;
    *y1 = s_zones[z].y1;
}

int world_zone_count(void)
{
    return s_num_zones;
}

int world_zone_loaded(int z)
{
    return z >= 0 && z < s_num_zones && s_zones[z].loaded;
}

int world_zone_is_town(int z)
{
    return z == z_town;
}

int world_zone_rooms(int zone, int (*rect)[4], int max)
{
    int n = 0;
    if (zone == s_rooms_zone && s_num_rooms > 0) {
        for (int i = 0; i < s_num_rooms && n < max; i++, n++)
            memcpy(rect[n], s_rooms[i], sizeof rect[n]);
        return n;
    }
    /* afuera (y los de una sola pieza), celdas de 8x8 como las salas del original */
    for (int i = 0; i < s_num_levels; i++) {
        const Level *lv = &s_levels[i];
        if (lv->zone != zone || lv->town)
            continue;
        for (int y = lv->y0; y < lv->y1; y += CELL)
            for (int x = lv->x0; x < lv->x1 && n < max; x += CELL, n++) {
                rect[n][0] = x;
                rect[n][1] = y;
                rect[n][2] = x + CELL <= lv->x1 ? CELL : lv->x1 - x;
                rect[n][3] = y + CELL <= lv->y1 ? CELL : lv->y1 - y;
            }
    }
    return n;
}

const char *world_zone_name(int z)
{
    return z >= 0 && z < s_num_zones && s_zones[z].name ? s_zones[z].name : "?";
}

int world_zone_at(const Map *m, float x, float y)
{
    int tx = (int)(x / TILE_SUB), ty = (int)(y / TILE_SUB);
    int z = x >= 0 && y >= 0 ? map_zone_of(m, tx, ty) : -1;
    if (z >= 0)
        return z;
    for (int i = 0; i < s_num_zones; i++) {
        const Zone *zz = &s_zones[i];
        if (tx >= zz->x0 && tx < zz->x1 && ty >= zz->y0 && ty < zz->y1)
            return i;
    }
    return -1;
}

const Level *world_level_at(float x, float y)
{
    int tx = (int)(x / TILE_SUB), ty = (int)(y / TILE_SUB);
    for (int i = 0; i < s_num_levels; i++) {
        const Level *l = &s_levels[i];
        if (tx >= l->x0 && tx < l->x1 && ty >= l->y0 && ty < l->y1)
            return l;
    }
    return NULL;
}

int world_level_count(void)
{
    return s_num_levels;
}

const Level *world_level(int i)
{
    return &s_levels[i];
}

const Level *world_level_named(const char *name)
{
    for (int i = 0; i < s_num_levels; i++)
        if (strcmp(s_levels[i].name, name) == 0)
            return &s_levels[i];
    return NULL;
}

int world_waypoint_zone(int wp)
{
    for (int i = 0; i < s_num_levels; i++)
        if (s_levels[i].waypoint == wp)
            return s_levels[i].zone;
    return -1;
}

const Warp *world_warp_at(float x, float y)
{
    for (int i = 0; i < s_num_warps; i++) {
        const Warp *w = &s_warps[i];
        if (!s_zones[w->from].loaded)
            continue;
        if (x >= w->x0 - w->margin && x < w->x1 + w->margin && y >= w->y0 - w->margin && y < w->y1 + w->margin)
            return w;
    }
    return NULL;
}

/* Distancia de (x, y) al rectángulo del salto */
static float warp_dist(const Warp *w, float x, float y)
{
    float dx = x < w->x0 ? w->x0 - x : x > w->x1 ? x - w->x1 : 0.0f;
    float dy = y < w->y0 ? w->y0 - y : y > w->y1 ? y - w->y1 : 0.0f;
    return sqrtf(dx * dx + dy * dy);
}

const Warp *world_button_warp_near(float x, float y)
{
    const Warp *best = NULL;
    float best_d = 1e9f;
    for (int i = 0; i < s_num_warps; i++) {
        const Warp *w = &s_warps[i];
        if (!s_zones[w->from].loaded || (!w->portal && w->margin <= 0))
            continue;
        float cx = (w->x0 + w->x1) / 2, cy = (w->y0 + w->y1) / 2, d;
        if (w->portal) { /* el portal: a 2,5 subtiles del centro */
            d = sqrtf((cx - x) * (cx - x) + (cy - y) * (cy - y));
            if (d >= 2.5f)
                continue;
        } else { /* la escalera: dentro de su margen */
            d = warp_dist(w, x, y);
            if (d > w->margin * 1.4143f || x < w->x0 - w->margin || x >= w->x1 + w->margin || y < w->y0 - w->margin ||
                y >= w->y1 + w->margin)
                continue;
        }
        if (d < best_d) {
            best_d = d;
            best = w;
        }
    }
    return best;
}

int world_warp_count(void)
{
    return s_num_warps;
}

const Warp *world_warp(int i)
{
    return &s_warps[i];
}

int world_warp_arrival(const Warp *w, float *x, float *y)
{
    if (w->portal) {
        *x = w->ax;
        *y = w->ay;
        return 0;
    }
    const Warp *any = NULL;
    for (int i = 0; i < s_num_warps; i++) {
        const Warp *o = &s_warps[i];
        if (o->from == w->to && o->to == w->from && !o->portal) {
            if (!any || (o->pair == w->pair && any->pair != w->pair))
                any = o;
        }
    }
    if (!any)
        return -1;
    *x = (any->x0 + any->x1) / 2.0f;
    *y = (any->y0 + any->y1) / 2.0f;
    return 0;
}

void world_portal(int id, int from, float x, float y, int to, float ax, float ay)
{
    int n = 0;
    for (int i = 0; i < s_num_warps; i++) /* el anterior con ese número se va */
        if (!(s_warps[i].portal == id + 1))
            s_warps[n++] = s_warps[i];
    s_num_warps = n;
    if (from < 0 || to < 0 || s_num_warps >= MAX_WARPS)
        return;
    Warp *w = &s_warps[s_num_warps++];
    memset(w, 0, sizeof *w);
    w->x0 = x - 1.0f;
    w->y0 = y - 1.0f;
    w->x1 = x + 1.0f;
    w->y1 = y + 1.0f;
    w->from = from;
    w->to = to;
    w->portal = id + 1;
    w->ax = ax;
    w->ay = ay;
}

/* La zona que sigue para ir de `from` a `to` (caminando, por saltos o por
   un portal abierto), o -1 si no hay camino */
int world_route_next(int from, int to)
{
    if (from < 0 || to < 0 || from >= s_num_zones || to >= s_num_zones)
        return -1;
    if (from == to)
        return to;
    int prev[MAX_ZONES], queue[MAX_ZONES], qh = 0, qt = 0;
    for (int i = 0; i < s_num_zones; i++)
        prev[i] = -2;
    prev[from] = -1;
    queue[qt++] = from;
    while (qh < qt) {
        int z = queue[qh++];
        int next[MAX_LINKS + 2 * MAX_WARPS], n = 0;
        for (int i = 0; i < s_num_links; i++)
            if (s_links[i].a == z || s_links[i].b == z)
                next[n++] = s_links[i].a == z ? s_links[i].b : s_links[i].a;
        for (int i = 0; i < s_num_jumps; i++)
            if (s_jumps[i][0] == z || s_jumps[i][1] == z)
                next[n++] = s_jumps[i][0] == z ? s_jumps[i][1] : s_jumps[i][0];
        for (int i = 0; i < s_num_warps; i++)
            if (s_warps[i].portal && s_warps[i].from == z)
                next[n++] = s_warps[i].to;
        for (int k = 0; k < n; k++) {
            if (prev[next[k]] != -2)
                continue;
            prev[next[k]] = z;
            if (next[k] == to) {
                int z2 = to;
                while (prev[z2] != from)
                    z2 = prev[z2];
                return z2;
            }
            queue[qt++] = next[k];
        }
    }
    return -1;
}

int world_link_count(void)
{
    return s_num_links;
}

const WorldLink *world_link(int i)
{
    return &s_links[i];
}
