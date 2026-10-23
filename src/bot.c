#include "bot.h"

#include "iso.h"
#include "world.h"

#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_AVOID     64
#define MAX_EXPAND    800000 /* tope de subtiles explorados por búsqueda */
#define SEARCH_BUDGET 6000   /* subtiles por tick: una búsqueda larga se reparte en varios ticks */
#define REPLAN_TICKS  125    /* el camino se rehace cada 5 segundos (los monstruos se mueven) */
#define DIRECT_TICKS  50     /* si directo no se llega, se va por el pasillo 4 veces esto */
#define LOOKAHEAD     12     /* hasta cuántos subtiles del camino se mira en línea recta */
#define BOX_PAD       10     /* margen de la caja de búsqueda alrededor de las zonas */
#define AVOID_PAD     0.6f   /* más allá de donde salta: el cuerpo y un paso (si no, roza el borde y salta) */
#define MAX_JUMPS     128    /* almohadillas por búsqueda (el Santuario Arcano tiene unas 50) */
#define JUMP_COST     4.0f   /* usar una almohadilla, en subtiles de caminar */
#define JUMP_MARGIN   0.5f   /* el subtile desde donde se usa, así de adentro del alcance (el bot no para justo en el centro) */

typedef struct {
    float r[MAX_AVOID][4]; /* x0, y0, x1, y1 */
    int   n;
} AvoidList;

struct BotNav {
    /* El tramo actual: un rectángulo destino y el camino hasta él */
    float     gx0, gy0, gx1, gy1;
    float     ex0, ey0, ex1, ey1; /* el que usó la búsqueda (agrandado si no se podía pisar) */
    int       have_goal;
    int      *path; /* subtiles (y * ancho + x) desde el inicio hasta el destino */
    int       path_len, path_cap, path_pos;
    int       partial; /* el camino llega a lo más cerca que se puede, no al destino */
    AvoidList path_avoid; /* lo que esquiva el camino (para seguirlo en línea recta) */
    uint32_t  planned_tick;
    uint64_t  zones;   /* zonas armadas cuando se buscó */
    int       plan_zone, plan_dest; /* la zona donde se buscó y el destino final de ese pedido */
    float     plan_x, plan_y;
    /* El destino final y si se llega directo (sin rodear por otras zonas) */
    float     fx, fy;
    int       fzone, direct_ok;
    uint32_t  direct_tick;
    /* A*: buffers del tamaño de la caja de búsqueda */
    int       bx0, by0, bw, bh, cap;
    float    *g;
    int      *came;
    uint8_t  *state; /* 0 sin ver, 1 abierto, 2 cerrado */
    uint8_t  *okc;   /* si entra el cuerpo: 0 sin ver, 1 sí, 2 no (cada subtile se mira una vez) */
    int      *heap;
    float    *heap_f;
    int       heap_n, heap_cap;
    AvoidList avoid; /* lo que esquiva la búsqueda en curso */
    BotJump   jumps[MAX_JUMPS]; /* y las almohadillas que puede usar */
    int       njumps;
    /* La búsqueda en curso (puede llevar varios ticks) */
    int       searching;
    float     sg[4];  /* el destino pedido */
    float     sG[4];  /* el que usa (agrandado si hizo falta) */
    int       s_best;
    float     s_best_h;
    long      s_expanded;
    /* Destrabarse */
    float     last_x, last_y, jx, jy;
    int       still, jitter;
    float     osc_x, osc_y; /* dónde estaba hace un rato, caminando (para ver si va y viene) */
    uint32_t  osc_tick;
    int       moving;     /* el último paso fue caminar */
    uint32_t  rng, tick;
    /* Para medir */
    int       plans;
    long      expanded;
    double    plan_ms, plan_ms_max;
};

BotNav *bot_nav_new(uint32_t seed)
{
    BotNav *n = calloc(1, sizeof *n);
    if (n)
        n->rng = seed | 1u;
    return n;
}

void bot_nav_free(BotNav *n)
{
    if (!n)
        return;
    free(n->path);
    free(n->g);
    free(n->came);
    free(n->state);
    free(n->okc);
    free(n->heap);
    free(n->heap_f);
    free(n);
}

static void search_cancel(BotNav *n)
{
    if (n->searching)
        n->expanded += n->s_expanded;
    n->searching = 0;
}

void bot_nav_reset(BotNav *n)
{
    search_cancel(n);
    n->have_goal = 0;
    n->path_len = n->path_pos = 0;
    n->direct_tick = 0;
    n->fzone = -1;
}

void bot_nav_stats(const BotNav *n, int *plans, long *expanded)
{
    *plans = n->plans;
    *expanded = n->expanded;
}

void bot_nav_timing(const BotNav *n, double *total_ms, double *worst_ms)
{
    *total_ms = n->plan_ms;
    *worst_ms = n->plan_ms_max;
}

static uint32_t nav_rnd(BotNav *n)
{
    n->rng = n->rng * 1664525u + 1013904223u;
    return n->rng >> 8;
}

static int (*s_breakable)(int sx, int sy);

void bot_nav_set_breakable(int (*fn)(int sx, int sy))
{
    s_breakable = fn;
}

static int (*s_jumps)(int x0, int y0, int x1, int y1, BotJump *out, int max);

void bot_nav_set_jumps(int (*fn)(int x0, int y0, int x1, int y1, BotJump *out, int max))
{
    s_jumps = fn;
}

/* ¿Del subtile k del camino al k + 1 se salta (una almohadilla)? */
static int path_jump(const BotNav *n, int W, int k)
{
    int a = n->path[k], b = n->path[k + 1];
    return abs(a % W - b % W) > 1 || abs(a / W - b / W) > 1;
}

/* Tapado, salvo por algo que se rompe o se abre para pasar */
static int sub_blocked(const Map *m, int sx, int sy)
{
    return map_blocked(m, sx, sy) && !(s_breakable && s_breakable(sx, sy));
}

/* ¿El cuerpo de un jugador choca en el subtile (sx, sy)? Como en el juego
   (y en el original): ocupa su subtile y los cuatro de al lado, en cruz */
static int foot_blocked(const Map *m, int sx, int sy)
{
    return sub_blocked(m, sx, sy) || sub_blocked(m, sx - 1, sy) || sub_blocked(m, sx + 1, sy) ||
           sub_blocked(m, sx, sy - 1) || sub_blocked(m, sx, sy + 1);
}

/* Y medio subtile de aire con las paredes y su grosor dibujado, como
   blocked_body del juego */
static int body_blocked(const Map *m, float x, float y)
{
    return foot_blocked(m, (int)floorf(x), (int)floorf(y)) || map_too_close(m, x, y);
}

static int avoided(const AvoidList *a, float x, float y)
{
    for (int i = 0; i < a->n; i++)
        if (x >= a->r[i][0] && x < a->r[i][2] && y >= a->r[i][1] && y < a->r[i][3])
            return 1;
    return 0;
}

/* ¿Entra el cuerpo parado en ese subtile? (su cruz libre, y en su centro
   lejos del grosor de las paredes) */
static int cell_ok(const BotNav *n, const Map *m, int x, int y)
{
    return !body_blocked(m, x + 0.5f, y + 0.5f) && !avoided(&n->avoid, x + 0.5f, y + 0.5f);
}

/* Lo mismo, guardado durante la búsqueda (A* mira cada subtile hasta 16 veces) */
static int cell_ok_box(BotNav *n, const Map *m, int x, int y)
{
    if (x < n->bx0 || y < n->by0 || x >= n->bx0 + n->bw || y >= n->by0 + n->bh)
        return cell_ok(n, m, x, y);
    int i = (y - n->by0) * n->bw + (x - n->bx0);
    if (!n->okc[i])
        n->okc[i] = cell_ok(n, m, x, y) ? 1 : 2;
    return n->okc[i] == 1;
}

/* ¿Se da el paso en diagonal de (cx, cy) a (cx + ddx, cy + ddy)? Sin cortar
   esquinas; con los dos costados tapados sí (la escalera angosta del
   Santuario: el jugador da el paso entero) si el cuerpo llega a la esquina:
   con el grosor de una pared en un costado no se acerca (player_blocked) y
   quedaba trabado entre una pared y un cadáver del Kurast Bazaar */
static int diag_ok(BotNav *n, const Map *m, int cx, int cy, int ddx, int ddy)
{
    int side = cell_ok_box(n, m, cx + ddx, cy);
    return side == cell_ok_box(n, m, cx, cy + ddy) &&
           (side || !map_too_close(m, (float)(cx + (ddx > 0)), (float)(cy + (ddy > 0))));
}

int bot_selftest(void)
{
    Map m;
    BotNav *n = bot_nav_new(1);
    if (!n || map_create(&m, 8, 8) != 0 || map_begin_zone(&m, 0, 0, 0, 8, 8) != 0) {
        bot_nav_free(n);
        return 1;
    }
    for (int y = 0; y < 20; y++) /* un piso libre */
        for (int x = 0; x < 20; x++)
            map_clear_coll(&m, x, y, 0xFFFF);
    /* De (10, 11) a (9, 10): en el costado (9, 11) no entra la cruz (pared en
       (8, 11)) y en el (10, 10) tampoco (pared en (10, 9)) */
    map_set_coll(&m, 8, 11, COLL_WALL);
    map_set_coll(&m, 10, 9, COLL_WALL);
    int ok[3];
    ok[0] = diag_ok(n, &m, 10, 11, -1, -1);
    map_set_coll(&m, 10, 10, COLL_VOLUME); /* el grosor de la pared en un costado: no llega a la esquina */
    ok[1] = !diag_ok(n, &m, 10, 11, -1, -1);
    map_clear_coll(&m, 10, 10, COLL_VOLUME);
    map_clear_coll(&m, 10, 9, COLL_WALL); /* un costado libre: se va por él, sin cortar la esquina */
    ok[2] = !diag_ok(n, &m, 10, 11, -1, -1);
    static const char *const k_what[3] = { "entre dos costados tapados, el paso en diagonal",
                                           "con el grosor de una pared en un costado, sin paso en diagonal",
                                           "con un costado libre, sin cortar la esquina" };
    int fails = 0;
    for (int k = 0; k < 3; k++)
        if (!ok[k]) {
            printf("  FALLA: %s\n", k_what[k]);
            fails++;
        }
    bot_nav_free(n);
    map_free(&m);
    return fails;
}

static uint64_t loaded_zones(void)
{
    uint64_t z = 0;
    for (int i = 0; i < world_zone_count() && i < 64; i++)
        if (world_zone_loaded(i))
            z |= 1ull << i;
    return z;
}

/* Los saltos de pisar que no son el destino: el camino no pasa por encima
   (si no, sale de la cueva sin querer), con un margen. Si ya está adentro
   (recién llegó por él: el juego no lo hace saltar hasta que sale), ese no
   cuenta; si está en el margen, el margen se achica hasta donde está. */
static void collect_avoid(AvoidList *a, float px, float py, float gx0, float gy0, float gx1, float gy1)
{
    a->n = 0;
    for (int i = 0; i < world_warp_count() && a->n < MAX_AVOID; i++) {
        const Warp *w = world_warp(i);
        if (w->portal || w->margin > 0 || !world_zone_loaded(w->from))
            continue;
        float cx = (w->x0 + w->x1) / 2, cy = (w->y0 + w->y1) / 2;
        if (cx >= gx0 && cx < gx1 && cy >= gy0 && cy < gy1)
            continue; /* es el destino (el de al lado no, aunque se toquen) */
        float ox = px < w->x0 ? w->x0 - px : px > w->x1 ? px - w->x1 : 0.0f;
        float oy = py < w->y0 ? w->y0 - py : py > w->y1 ? py - w->y1 : 0.0f;
        float out = ox > oy ? ox : oy, pad = WARP_TOUCH + AVOID_PAD;
        if (out < WARP_TOUCH)
            continue;
        if (out < pad)
            pad = out - 0.05f;
        a->r[a->n][0] = w->x0 - pad;
        a->r[a->n][1] = w->y0 - pad;
        a->r[a->n][2] = w->x1 + pad;
        a->r[a->n][3] = w->y1 + pad;
        a->n++;
    }
}

static int heap_push(BotNav *n, int c, float f)
{
    if (n->heap_n == n->heap_cap) {
        int cap = n->heap_cap ? n->heap_cap * 2 : 4096;
        int *h = realloc(n->heap, (size_t)cap * sizeof *h);
        if (!h)
            return -1;
        n->heap = h;
        float *hf = realloc(n->heap_f, (size_t)cap * sizeof *hf);
        if (!hf)
            return -1;
        n->heap_f = hf;
        n->heap_cap = cap;
    }
    int i = n->heap_n++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (n->heap_f[p] <= f)
            break;
        n->heap[i] = n->heap[p];
        n->heap_f[i] = n->heap_f[p];
        i = p;
    }
    n->heap[i] = c;
    n->heap_f[i] = f;
    return 0;
}

static int heap_pop(BotNav *n)
{
    int top = n->heap[0], last = n->heap[--n->heap_n];
    float lf = n->heap_f[n->heap_n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n->heap_n)
            break;
        if (c + 1 < n->heap_n && n->heap_f[c + 1] < n->heap_f[c])
            c++;
        if (n->heap_f[c] >= lf)
            break;
        n->heap[i] = n->heap[c];
        n->heap_f[i] = n->heap_f[c];
        i = c;
    }
    if (n->heap_n > 0) {
        n->heap[i] = last;
        n->heap_f[i] = lf;
    }
    return top;
}

/* Distancia octogonal del subtile (x, y) al rectángulo */
static float to_rect(float x, float y, float x0, float y0, float x1, float y1)
{
    float dx = x < x0 ? x0 - x : x > x1 ? x - x1 : 0.0f;
    float dy = y < y0 ? y0 - y : y > y1 ? y - y1 : 0.0f;
    return dx > dy ? dx + 0.4142f * dy : dy + 0.4142f * dx;
}

/* Arranca una búsqueda (A*) de (px, py) al rectángulo dentro de la caja.
   search_run la sigue de a pedazos. 0 si no pudo arrancar. */
static int search_start(BotNav *n, const Map *m, float px, float py, float gx0, float gy0, float gx1, float gy1,
                        int bx0, int by0, int bx1, int by1)
{
    search_cancel(n);
    int W = m->width * TILE_SUB, H = m->height * TILE_SUB;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > W) bx1 = W;
    if (by1 > H) by1 = H;
    int sx = (int)px, sy = (int)py;
    if (sx < bx0) bx0 = sx;
    if (sy < by0) by0 = sy;
    if (sx >= bx1) bx1 = sx + 1;
    if (sy >= by1) by1 = sy + 1;
    int bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0)
        return 0;
    size_t cells = (size_t)bw * (size_t)bh;
    if ((int)cells > n->cap) {
        float *g = realloc(n->g, cells * sizeof *g);
        if (g) n->g = g;
        int *came = realloc(n->came, cells * sizeof *came);
        if (came) n->came = came;
        uint8_t *st = realloc(n->state, cells);
        if (st) n->state = st;
        uint8_t *ok = realloc(n->okc, cells);
        if (ok) n->okc = ok;
        if (!g || !came || !st || !ok)
            return 0;
        n->cap = (int)cells;
    }
    n->bx0 = bx0;
    n->by0 = by0;
    n->bw = bw;
    n->bh = bh;
    memset(n->state, 0, cells);
    memset(n->okc, 0, cells);
    collect_avoid(&n->avoid, px, py, gx0, gy0, gx1, gy1);
    n->njumps = s_jumps ? s_jumps(bx0, by0, bx1, by1, n->jumps, MAX_JUMPS) : 0;

    /* Destino: los subtiles con el centro adentro del rectángulo (la misma
       cuenta que "llegó"). Si en ninguno entra el cuerpo (un árbol, una
       piedra) o es tan chico que no tiene centros, se agranda. */
    float G0x = gx0, G0y = gy0, G1x = gx1, G1y = gy1;
    for (int grow = 0; grow <= 6; grow++) {
        G0x = gx0 - grow, G0y = gy0 - grow, G1x = gx1 + grow, G1y = gy1 + grow;
        int any = 0;
        for (int y = (int)floorf(G0y - 0.5f); y <= (int)floorf(G1y) && !any; y++)
            for (int x = (int)floorf(G0x - 0.5f); x <= (int)floorf(G1x) && !any; x++)
                any = x >= bx0 && y >= by0 && x < bx1 && y < by1 && x + 0.5f >= G0x && x + 0.5f < G1x &&
                      y + 0.5f >= G0y && y + 0.5f < G1y && cell_ok_box(n, m, x, y);
        if (any)
            break;
    }
    n->sg[0] = gx0, n->sg[1] = gy0, n->sg[2] = gx1, n->sg[3] = gy1;
    n->sG[0] = G0x, n->sG[1] = G0y, n->sG[2] = G1x, n->sG[3] = G1y;

    int s = (sy - by0) * bw + (sx - bx0);
    n->heap_n = 0;
    n->g[s] = 0.0f;
    n->came[s] = -1;
    n->state[s] = 1;
    n->s_best = s;
    n->s_best_h = to_rect(sx + 0.5f, sy + 0.5f, G0x, G0y, G1x, G1y);
    n->s_expanded = 0;
    if (heap_push(n, s, n->s_best_h) != 0)
        return 0;
    n->searching = 1;
    return 1;
}

/* Sigue la búsqueda hasta `budget` subtiles. 1 = terminó y el camino quedó
   en n->path (hasta lo más cerca si no se llega); 0 = sigue en otro tick */
static int search_run(BotNav *n, const Map *m, long budget)
{
    static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    const int bx0 = n->bx0, by0 = n->by0, bw = n->bw, bx1 = bx0 + n->bw, by1 = by0 + n->bh;
    const float G0x = n->sG[0], G0y = n->sG[1], G1x = n->sG[2], G1y = n->sG[3];
    int found = -1;
    long done = 0;
    while (n->heap_n > 0 && n->s_expanded < MAX_EXPAND && done < budget) {
        int c = heap_pop(n);
        if (n->state[c] == 2)
            continue;
        n->state[c] = 2;
        n->s_expanded++;
        done++;
        int cx = c % bw + bx0, cy = c / bw + by0;
        if (cx + 0.5f >= G0x && cx + 0.5f < G1x && cy + 0.5f >= G0y && cy + 0.5f < G1y && cell_ok_box(n, m, cx, cy)) {
            found = c;
            break;
        }
        float hc = to_rect(cx + 0.5f, cy + 0.5f, G0x, G0y, G1x, G1y);
        if (hc < n->s_best_h) {
            n->s_best_h = hc;
            n->s_best = c;
        }
        for (int d = 0; d < 8; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < bx0 || ny < by0 || nx >= bx1 || ny >= by1)
                continue;
            int i = (ny - by0) * bw + (nx - bx0);
            if (n->state[i] == 2 || !cell_ok_box(n, m, nx, ny))
                continue;
            if (d >= 4 && !diag_ok(n, m, cx, cy, dx[d], dy[d]))
                continue;
            float ng = n->g[c] + (d >= 4 ? 1.4142f : 1.0f);
            if (n->state[i] == 1 && ng >= n->g[i])
                continue;
            n->g[i] = ng;
            n->came[i] = c;
            n->state[i] = 1;
            if (heap_push(n, i, ng + to_rect(nx + 0.5f, ny + 0.5f, G0x, G0y, G1x, G1y)) != 0)
                n->heap_n = 0; /* sin memoria: hasta lo más cerca que llegó */
        }
        for (int k = 0; k < n->njumps; k++) { /* al alcance de una almohadilla: adonde lleva */
            const BotJump *j = &n->jumps[k];
            float ox = cx + 0.5f < j->x0 ? j->x0 - cx - 0.5f : cx + 0.5f > j->x1 ? cx + 0.5f - j->x1 : 0.0f;
            float oy = cy + 0.5f < j->y0 ? j->y0 - cy - 0.5f : cy + 0.5f > j->y1 ? cy + 0.5f - j->y1 : 0.0f;
            float r = j->reach - JUMP_MARGIN;
            int nx = (int)floorf(j->lx), ny = (int)floorf(j->ly);
            if (ox * ox + oy * oy > r * r || nx < bx0 || ny < by0 || nx >= bx1 || ny >= by1)
                continue;
            int i = (ny - by0) * bw + (nx - bx0);
            float ng = n->g[c] + JUMP_COST;
            if (n->state[i] == 2 || !cell_ok_box(n, m, nx, ny) || (n->state[i] == 1 && ng >= n->g[i]))
                continue;
            n->g[i] = ng;
            n->came[i] = c;
            n->state[i] = 1;
            if (heap_push(n, i, ng + to_rect(nx + 0.5f, ny + 0.5f, G0x, G0y, G1x, G1y)) != 0)
                n->heap_n = 0;
        }
    }
    if (found < 0 && n->heap_n > 0 && n->s_expanded < MAX_EXPAND)
        return 0;

    /* Terminó: el camino nuevo reemplaza al que venía siguiendo */
    n->searching = 0;
    n->plans++;
    n->expanded += n->s_expanded;
    int end = found >= 0 ? found : n->s_best;
    n->partial = found < 0;
    n->ex0 = G0x, n->ey0 = G0y, n->ex1 = G1x, n->ey1 = G1y;
    n->path_avoid = n->avoid;
    if (getenv("D2_BOT_DEBUG")) {
        printf("  nav: a (%.0f..%.0f, %.0f..%.0f) en la caja %dx%d: %ld explorados%s\n", n->sg[0], n->sg[2], n->sg[1],
               n->sg[3], n->bw, n->bh, n->s_expanded, n->partial ? ", no se llega (hasta lo más cerca)" : "");
        for (int i = 0; i < n->avoid.n; i++)
            printf("    evita (%.1f..%.1f, %.1f..%.1f)\n", n->avoid.r[i][0], n->avoid.r[i][2], n->avoid.r[i][1],
                   n->avoid.r[i][3]);
    }
    int W = m->width * TILE_SUB, len = 0;
    for (int c = end; c >= 0; c = n->came[c])
        len++;
    if (len > n->path_cap) {
        int *p = realloc(n->path, (size_t)len * sizeof *p);
        if (!p) {
            n->path_len = n->path_pos = 0;
            return 1;
        }
        n->path = p;
        n->path_cap = len;
    }
    n->path_len = len;
    for (int c = end, k = len - 1; c >= 0; c = n->came[c], k--)
        n->path[k] = (c / bw + by0) * W + (c % bw + bx0);
    n->path_pos = 0;
    return 1;
}

/* search_run midiendo cuánto tarda (la peor vez es lo que se notaría en un cuadro) */
static int search_step(BotNav *n, const Map *m, long budget)
{
    Uint64 t0 = SDL_GetPerformanceCounter();
    int r = search_run(n, m, budget);
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    n->plan_ms += ms;
    if (ms > n->plan_ms_max)
        n->plan_ms_max = ms;
    return r;
}

/* ¿Se camina en línea recta de (x0, y0) a (x1, y1)? */
static int clear_line(const BotNav *n, const Map *m, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int steps = (int)(len / 0.25f) + 1;
    for (int k = 1; k <= steps; k++) {
        float t = (float)k / steps, x = x0 + dx * t, y = y0 + dy * t;
        if (body_blocked(m, x, y) || avoided(&n->path_avoid, x, y))
            return 0;
    }
    return 1;
}

/* Sigue el camino: lo más lejos que se vea en línea recta. 2 = está donde
   el camino salta por una almohadilla: usarla */
static int follow(BotNav *n, const Map *m, float px, float py, float *dx, float *dy)
{
    int W = m->width * TILE_SUB;
    if (n->path_len == 0)
        return 0;
    /* Avanzar hasta el subtile del camino más cerca (de los próximos; más
       allá de una almohadilla solo si ya saltó: al lado de donde lleva) */
    int best = n->path_pos;
    float best_d = 1e30f;
    for (int i = n->path_pos; i < n->path_len && i < n->path_pos + LOOKAHEAD; i++) {
        float cx = n->path[i] % W + 0.5f, cy = n->path[i] / W + 0.5f;
        float d = (cx - px) * (cx - px) + (cy - py) * (cy - py);
        if (i > n->path_pos && path_jump(n, W, i - 1) && d > 2.0f * 2.0f)
            break;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best_d > 3.0f * 3.0f)
        return 0; /* se desvió: hay que buscar de nuevo */
    n->path_pos = best;
    int target = n->path_pos;
    if (target + 1 < n->path_len && path_jump(n, W, target)) { /* desde acá, la almohadilla */
        *dx = n->path[target] % W + 0.5f - px;
        *dy = n->path[target] / W + 0.5f - py;
        return fabsf(*dx) < 0.45f && fabsf(*dy) < 0.45f ? 2 : 1;
    }
    for (int i = n->path_pos + 1; i < n->path_len && i <= n->path_pos + LOOKAHEAD && !path_jump(n, W, i - 1); i++)
        if (clear_line(n, m, px, py, n->path[i] % W + 0.5f, n->path[i] / W + 0.5f))
            target = i;
    /* Si no se ve más allá del subtile en el que ya está (en una escalera
       angosta en diagonal la recta roza la esquina), al siguiente del camino,
       que es vecino y se pisa: si no, apunta al centro del suyo y, con un paso
       largo (la Asesina con Ráfaga de velocidad), va y vuelve sin avanzar */
    if (target == n->path_pos && target + 1 < n->path_len &&
        fabsf(n->path[target] % W + 0.5f - px) < 0.75f && fabsf(n->path[target] / W + 0.5f - py) < 0.75f)
        target++;
    float tx = n->path[target] % W + 0.5f, ty = n->path[target] / W + 0.5f;
    if (target == n->path_len - 1 && fabsf(tx - px) + fabsf(ty - py) < 0.2f) {
        /* al final del camino: al punto más cerca adentro del destino */
        tx = px < n->ex0 + 0.1f ? n->ex0 + 0.1f : px > n->ex1 - 0.1f ? n->ex1 - 0.1f : px;
        ty = py < n->ey0 + 0.1f ? n->ey0 + 0.1f : py > n->ey1 - 0.1f ? n->ey1 - 0.1f : py;
    }
    *dx = tx - px;
    *dy = ty - py;
    return 1;
}

/* La caja de búsqueda: las zonas a y b (en subtiles) con un margen */
static void zone_box(int a, int b, int *x0, int *y0, int *x1, int *y1)
{
    int ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
    world_zone_rect(a, &ax0, &ay0, &ax1, &ay1);
    world_zone_rect(b >= 0 ? b : a, &bx0, &by0, &bx1, &by1);
    *x0 = (ax0 < bx0 ? ax0 : bx0) * TILE_SUB - BOX_PAD;
    *y0 = (ay0 < by0 ? ay0 : by0) * TILE_SUB - BOX_PAD;
    *x1 = (ax1 > bx1 ? ax1 : bx1) * TILE_SUB + BOX_PAD;
    *y1 = (ay1 > by1 ? ay1 : by1) * TILE_SUB + BOX_PAD;
}

/* Un lugar libre de la zona `z` a unos pasos de (x, y): para cruzar un
   pasillo, bien adentro de la otra zona */
static int spot_in_zone(const Map *m, int z, float x, float y, float *ox, float *oy)
{
    for (int r = 6; r < 40; r++)
        for (int a = 0; a < 16; a++) {
            float sx = x + cosf(a * 0.3927f) * r, sy = y + sinf(a * 0.3927f) * r;
            if (world_zone_at(m, sx, sy) == z && !body_blocked(m, sx, sy)) {
                *ox = sx;
                *oy = sy;
                return 0;
            }
        }
    return -1;
}

static int same_rect(const float *a, float x0, float y0, float x1, float y1)
{
    return fabsf(a[0] - x0) <= 1.5f && fabsf(a[1] - y0) <= 1.5f && fabsf(a[2] - x1) <= 1.5f && fabsf(a[3] - y1) <= 1.5f;
}

static int goal_changed(const BotNav *n, float x0, float y0, float x1, float y1)
{
    const float g[4] = { n->gx0, n->gy0, n->gx1, n->gy1 };
    return !n->have_goal || !same_rect(g, x0, y0, x1, y1);
}

/* El camino de la búsqueda que terminó pasa a ser el que se sigue */
static void take_path(BotNav *n, uint32_t tick)
{
    n->gx0 = n->sg[0], n->gy0 = n->sg[1], n->gx1 = n->sg[2], n->gy1 = n->sg[3];
    n->have_goal = 1;
    n->planned_tick = tick;
    n->zones = loaded_zones();
}

/* ¿Yendo de (px, py) hacia (jx, jy) unos subtiles (lo que dura el rato para
   un costado) pisa un salto de pisar que ahora no toca? Al pie de la escalera
   del River of Flame (recién llegado, encima de la subida) el costado al azar
   salía de ella y la volvía a pisar: subía al City of the Damned, bajaba de
   nuevo y así para siempre. */
static int jitter_warps(float px, float py, float jx, float jy)
{
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        if (w->portal || w->margin > 0 || !world_zone_loaded(w->from))
            continue;
        float x0 = w->x0 - WARP_TOUCH, y0 = w->y0 - WARP_TOUCH, x1 = w->x1 + WARP_TOUCH, y1 = w->y1 + WARP_TOUCH;
        if (px >= x0 && px < x1 && py >= y0 && py < y1)
            continue; /* el de llegada: salir de él está bien */
        for (float d = 0.5f; d <= 3.5f; d += 0.5f)
            if (px + jx * d >= x0 && px + jx * d < x1 && py + jy * d >= y0 && py + jy * d < y1)
                return 1;
    }
    return 0;
}

/* Un rato para un costado (al azar, sin pisar saltos) y después se busca el
   camino de nuevo */
static void start_jitter(BotNav *n, float px, float py, float *dx, float *dy)
{
    for (int k = 0; k < 8; k++) {
        float a = (float)(nav_rnd(n) % 628) / 100.0f;
        n->jx = cosf(a);
        n->jy = sinf(a);
        if (!jitter_warps(px, py, n->jx, n->jy))
            break;
    }
    n->jitter = 8;
    n->still = 0;
    n->have_goal = 0;
    *dx = n->jx;
    *dy = n->jy;
}

static int nav_step(BotNav *n, const Map *m, uint32_t tick, float px, float py, int zone, float x, float y, float reach,
                    float *dx, float *dy);

#define OSC_TICKS 50 /* 2 segundos */

int bot_nav_step(BotNav *n, const Map *m, uint32_t tick, float px, float py, int zone, float x, float y, float reach,
                 float *dx, float *dy)
{
    /* Va y viene sin avanzar: dos puntos del camino que se turnan, con algo
       que no lo deja pasar (el demonio del Brujo pegado al costado, en el
       Spider Forest). No está quieto, así que `still` no lo ve: si caminando
       en 2 segundos no se alejó medio subtile, se destraba igual. */
    if (!n->moving || n->jitter > 0 || n->searching) {
        n->osc_x = px;
        n->osc_y = py;
        n->osc_tick = tick;
    } else if (tick - n->osc_tick >= OSC_TICKS) {
        float ox = px - n->osc_x, oy = py - n->osc_y;
        n->osc_x = px;
        n->osc_y = py;
        n->osc_tick = tick;
        if (ox * ox + oy * oy < 0.5f * 0.5f) {
            n->tick = tick;
            n->last_x = px;
            n->last_y = py;
            start_jitter(n, px, py, dx, dy);
            return BOT_MOVING;
        }
    }
    int r = nav_step(n, m, tick, px, py, zone, x, y, reach, dx, dy);
    n->moving = r == BOT_MOVING;
    return r;
}

static int nav_step(BotNav *n, const Map *m, uint32_t tick, float px, float py, int zone, float x, float y, float reach,
                    float *dx, float *dy)
{
    *dx = *dy = 0.0f;
    n->tick = tick;
    /* Trabado contra algo (un NPC, un monstruo, una esquina): un rato para
       un costado y después se busca de nuevo */
    float moved = fabsf(px - n->last_x) + fabsf(py - n->last_y);
    n->last_x = px;
    n->last_y = py;
    n->still = moved < 0.01f && !n->searching ? n->still + 1 : 0; /* (buscando, quieto a propósito) */
    if (n->jitter > 0) {
        n->jitter--;
        *dx = n->jx;
        *dy = n->jy;
        return BOT_MOVING;
    }
    if (n->still > 15) {
        start_jitter(n, px, py, dx, dy);
        return BOT_MOVING;
    }

    long budget = SEARCH_BUDGET;
    int cur = world_zone_at(m, px, py), dest = zone >= 0 ? zone : world_zone_at(m, x, y);
    /* Si el camino que sigue (para este mismo destino final) pasa por otra
       zona, se sigue sin rehacer la ruta por zonas al cruzar el borde: en la
       selva del Acto 3 el camino más corto entre dos puntos del Spider Forest
       puede dar la vuelta por el Great Marsh, y al pisarlo la ruta mandaba de
       vuelta al Spider Forest; el bot quedaba yendo y viniendo sobre el borde
       (semilla 56, la demostración del Acto 3) */
    if (n->have_goal && !n->searching && !n->partial && cur != n->plan_zone && n->plan_dest == dest &&
        fabsf(n->plan_x - x) < 2.0f && fabsf(n->plan_y - y) < 2.0f && n->path_pos < n->path_len - 1 &&
        n->zones == loaded_zones() && tick - n->planned_tick <= REPLAN_TICKS) {
        int f = follow(n, m, px, py, dx, dy);
        if (f == 2)
            return BOT_USE;
        if (f == 1)
            return BOT_MOVING;
        n->have_goal = 0; /* se desvió: se busca de nuevo desde acá */
    }
    float g0x = x - reach, g0y = y - reach, g1x = x + reach, g1y = y + reach;
    int box_a = cur, box_b = dest, box_c = -1, use = 0, walk_in = 0;
    const Warp *use_warp = NULL;
    /* Cerca de un pasillo, la búsqueda también entra a la zona vecina: en la
       selva del Acto 3 el camino de un lado al otro del borde puede dar la
       vuelta por un tramo de la otra (y sin eso, sobre el borde, el bot
       cambiaba de zona a cada paso y no terminaba nunca de buscar) */
    for (int l = 0; cur >= 0 && l < world_link_count() && box_c < 0; l++) {
        const WorldLink *wl = world_link(l);
        int other = wl->a == cur ? wl->b : wl->b == cur ? wl->a : -1;
        if (other >= 0 && world_zone_loaded(other) && fabsf(px - wl->x) < 40.0f && fabsf(py - wl->y) < 40.0f)
            box_c = other;
    }
    if (cur >= 0 && dest >= 0 && cur != dest) {
        /* ¿Se llega directo? Solo si las zonas se tocan por un pasillo (si no,
           están separadas por vacío y hace falta un salto) y el destino está
           armado. Se mira con un tope: si está lejos, se va por el pasillo. */
        int touching = 0;
        for (int l = 0; l < world_link_count() && !touching; l++) {
            const WorldLink *wl = world_link(l);
            touching = (wl->a == cur && wl->b == dest) || (wl->b == cur && wl->a == dest);
        }
        /* Directo: con las dos armadas, se busca el camino al destino final en
           las dos (como cualquier búsqueda, de a poco). En la selva del Acto 3
           ese camino puede cruzar el borde por otro lado que el pasillo, o ir
           y volver por la otra zona: por el pasillo el bot quedaba yendo y
           viniendo. Si no se llega así, por el pasillo un rato. */
        n->direct_ok = touching && world_zone_loaded(dest) &&
                       !(n->fzone == dest && fabsf(n->fx - x) < 2.0f && fabsf(n->fy - y) < 2.0f &&
                         tick - n->direct_tick < 4 * DIRECT_TICKS);
        if (!n->direct_ok) {
            int next = world_route_next(cur, dest), found = 0;
            if (next < 0)
                return BOT_NO_PATH;
            for (int l = 0; l < world_link_count() && !found; l++) {
                const WorldLink *wl = world_link(l);
                if (!((wl->a == cur && wl->b == next) || (wl->b == cur && wl->a == next)))
                    continue;
                float sx, sy;
                if (world_zone_loaded(next) && spot_in_zone(m, next, wl->x, wl->y, &sx, &sy) == 0) {
                    g0x = sx - 1, g0y = sy - 1, g1x = sx + 1, g1y = sy + 1;
                } else {
                    g0x = wl->x - 2, g0y = wl->y - 2, g1x = wl->x + 2, g1y = wl->y + 2;
                }
                box_b = next;
                found = 1;
            }
            for (int i = 0; i < world_warp_count() && !found; i++) {
                const Warp *w = world_warp(i);
                if (w->from != cur || w->to != next)
                    continue;
                found = 1;
                box_b = cur;
                use_warp = w;
                if (w->portal) { /* el portal: pararse al lado y usarlo */
                    float cx = (w->x0 + w->x1) / 2, cy = (w->y0 + w->y1) / 2;
                    g0x = cx - 1.5f, g0y = cy - 1.5f, g1x = cx + 1.5f, g1y = cy + 1.5f;
                    use = 1;
                } else if (w->margin > 0) { /* la escalera: dentro de su margen y usarla */
                    float mg = w->margin - 0.3f;
                    g0x = w->x0 - mg, g0y = w->y0 - mg, g1x = w->x1 + mg, g1y = w->y1 + mg;
                    use = 1;
                } else { /* la boca de la cueva: entrar caminando */
                    float t = WARP_TOUCH * 0.5f;
                    g0x = w->x0 - t, g0y = w->y0 - t, g1x = w->x1 + t, g1y = w->y1 + t;
                    walk_in = 1;
                }
            }
            if (!found)
                return BOT_NO_PATH;
        }
    }

    int inside = px >= g0x && px < g1x && py >= g0y && py < g1y;
    if (!inside && n->have_goal && !goal_changed(n, g0x, g0y, g1x, g1y) && !n->partial)
        inside = px >= n->ex0 && px < n->ex1 && py >= n->ey0 && py < n->ey1;
    if (inside) {
        if (use && world_button_warp_near(px, py) == use_warp)
            return BOT_USE;
        if (use) { /* hay otra más cerca (las dos escaleras de la torre): acercarse a la que va */
            *dx = (use_warp->x0 + use_warp->x1) / 2 - px;
            *dy = (use_warp->y0 + use_warp->y1) / 2 - py;
            return BOT_MOVING;
        }
        if (!walk_in)
            return BOT_ARRIVED;
        /* Encima de la entrada y no saltó (recién llegó por ella): seguir al medio */
        *dx = (g0x + g1x) / 2 - px;
        *dy = (g0y + g1y) / 2 - py;
        return BOT_MOVING;
    }

    /* Buscar de nuevo si cambió el destino o las zonas, cada tanto, o si
       llegó al final del camino sin llegar. La búsqueda se reparte en
       varios ticks; mientras tanto sigue el camino que tenía si es para el
       mismo destino, y si no espera. */
    if (n->searching && !same_rect(n->sg, g0x, g0y, g1x, g1y))
        search_cancel(n);
    if (n->searching || goal_changed(n, g0x, g0y, g1x, g1y) || n->zones != loaded_zones() ||
        tick - n->planned_tick > REPLAN_TICKS || n->path_pos >= n->path_len - 1) {
        if (!n->searching) {
            int bx0, by0, bx1, by1;
            zone_box(box_a >= 0 ? box_a : box_b, box_b, &bx0, &by0, &bx1, &by1);
            if (box_c >= 0) {
                int cx0, cy0, cx1, cy1;
                zone_box(box_c, box_c, &cx0, &cy0, &cx1, &cy1);
                bx0 = cx0 < bx0 ? cx0 : bx0;
                by0 = cy0 < by0 ? cy0 : by0;
                bx1 = cx1 > bx1 ? cx1 : bx1;
                by1 = cy1 > by1 ? cy1 : by1;
            }
            if (!search_start(n, m, px, py, g0x, g0y, g1x, g1y, bx0, by0, bx1, by1))
                return BOT_NO_PATH;
        }
        if (search_step(n, m, budget > 500 ? budget : 500)) {
            take_path(n, tick);
            n->plan_zone = cur;
            n->plan_dest = dest;
            n->plan_x = x;
            n->plan_y = y;
            if (n->partial && n->direct_ok) { /* directo no se llega: por el pasillo */
                n->fzone = dest;
                n->fx = x;
                n->fy = y;
                n->direct_tick = tick;
                n->have_goal = 0;
            }
        } else if (goal_changed(n, g0x, g0y, g1x, g1y))
            return BOT_MOVING; /* buscando (con dx = dy = 0: quieto) */
    }
    int f = follow(n, m, px, py, dx, dy);
    if (f == 2)
        return BOT_USE; /* la almohadilla */
    if (!f) {
        n->have_goal = 0; /* se desvió: la próxima vez se busca de nuevo */
        *dx = (g0x + g1x) / 2 - px;
        *dy = (g0y + g1y) / 2 - py;
    }
    if (n->partial && n->path_pos >= n->path_len - 1) {
        int W = m->width * TILE_SUB, last = n->path[n->path_len - 1];
        float lx = last % W + 0.5f - px, ly = last / W + 0.5f - py;
        if (lx * lx + ly * ly < 1.0f)
            return BOT_NO_PATH; /* llegó a lo más cerca que se puede */
    }
    return BOT_MOVING;
}
