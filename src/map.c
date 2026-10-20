#include "map.h"

#include "fmt/fs.h"
#include "iso.h"
#include "jobs.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static uint32_t tile_key(int orientation, int style, int seq)
{
    return ((uint32_t)orientation << 16) | ((uint32_t)(style & 0xFF) << 8) | (uint32_t)(seq & 0xFF);
}

/* "\d2\data\global\tiles\act1\town\floor.tg1" -> "data/global/tiles/act1/town/floor.dt1" */
static void dt1_path(const char *ds1_ref, char *out, size_t cap)
{
    char low[256];
    size_t i = 0;
    for (; ds1_ref[i] && i + 1 < sizeof low; i++)
        low[i] = ds1_ref[i] == '\\' ? '/' : (char)tolower((unsigned char)ds1_ref[i]);
    low[i] = '\0';

    const char *start = strstr(low, "data/");
    snprintf(out, cap, "%s", start ? start : low);
    size_t n = strlen(out);
    if (n > 4 && out[n - 4] == '.')
        strcpy(out + n - 4, ".dt1");
}

static uint32_t hash3(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static __thread const Dt1 *s_sort_dt1; /* (de cada hilo: también se ordena en el de trabajos) */

static int cmp_tile_index(const void *a, const void *b)
{
    const Dt1Tile *ta = &s_sort_dt1->tiles[*(const uint32_t *)a & 0xFFFF];
    const Dt1Tile *tb = &s_sort_dt1->tiles[*(const uint32_t *)b & 0xFFFF];
    uint32_t ka = tile_key(ta->orientation, ta->main_index, ta->sub_index);
    uint32_t kb = tile_key(tb->orientation, tb->main_index, tb->sub_index);
    return (ka > kb) - (ka < kb);
}

static void mark_user(Map *m, int f)
{
    if (m->zone >= 0 && m->zone < MAP_ZONES)
        m->dt1_users[f] |= 1ull << m->zone;
}

/* Lee un DT1 (solo las cabeceras: cada imagen se decodifica al dibujarla por
   primera vez, dt1_tile_ready) y arma su índice ordenado por clave, la
   primera y la última clave y lo que ocupa. Desde el juego o desde el hilo
   de trabajos. */
static int dt1_prepare(const char *path, Dt1 *t, uint32_t **keys_out, uint32_t *kmin, uint32_t *kmax, size_t *bytes)
{
    size_t n;
    uint8_t *d = fs_read(path, &n);
    if (!d || dt1_load_lazy(d, n, t) != 0) { /* (si anda, el DT1 se queda con d) */
        free(d);
        return -1;
    }
    uint32_t *keys = malloc((size_t)(t->num_tiles ? t->num_tiles : 1) * sizeof *keys);
    if (!keys) {
        dt1_free(t);
        return -1;
    }
    for (int i = 0; i < t->num_tiles; i++)
        keys[i] = (uint32_t)i;
    s_sort_dt1 = t;
    qsort(keys, (size_t)t->num_tiles, sizeof *keys, cmp_tile_index);
    *kmin = 1; /* (vacío: ninguna clave entra) */
    *kmax = 0;
    if (t->num_tiles > 0) {
        const Dt1Tile *lo = &t->tiles[keys[0]], *hi = &t->tiles[keys[t->num_tiles - 1]];
        *kmin = tile_key(lo->orientation, lo->main_index, lo->sub_index);
        *kmax = tile_key(hi->orientation, hi->main_index, hi->sub_index);
    }
    *bytes = 0;
    for (int i = 0; i < t->num_tiles; i++)
        *bytes += (size_t)(t->tiles[i].img_w * t->tiles[i].img_h) + sizeof(Dt1Tile);
    *keys_out = keys;
    return 0;
}

/* Los DT1 que se preparan en el hilo de trabajos antes de armar la zona que
   los va a pedir: en la Pi, leer y ordenar los ~8 MB de la selva del Acto 3
   eran ~30 ms en el tick que la armaba. get_dt1 se queda con el que ya está
   listo; si todavía no, lo carga como siempre (no se espera nunca). */
enum { PREP_FREE, PREP_LOADING, PREP_READY, PREP_CANCEL };
#define MAP_PREP 32 /* como mucho (si no hay lugar, se suelta el más viejo de los listos) */
typedef struct {
    int       state; /* PREP_*: de quién es el resto (FREE y READY, del juego; LOADING y CANCEL, del hilo) */
    char      path[96];
    Dt1       dt1;
    uint32_t *keys, kmin, kmax;
    size_t    bytes;
    unsigned  order; /* cuándo se pidió */
} PrepDt1;
static PrepDt1  s_prep[MAP_PREP];
static unsigned s_prep_order;
static int      s_dt1_ready, s_dt1_loaded; /* para D2_ZONES: los que llegaron preparados y los que se leyeron al armar */

void map_dt1_loads(int *ready, int *loaded)
{
    *ready = s_dt1_ready;
    *loaded = s_dt1_loaded;
    s_dt1_ready = s_dt1_loaded = 0;
}

static void prep_job(void *ptr, int arg)
{
    (void)arg;
    PrepDt1 *p = ptr;
    int ok = dt1_prepare(p->path, &p->dt1, &p->keys, &p->kmin, &p->kmax, &p->bytes) == 0, was = PREP_LOADING;
    if (ok && __atomic_compare_exchange_n(&p->state, &was, PREP_READY, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return;
    if (ok) { /* el juego ya no lo quería */
        dt1_free(&p->dt1);
        free(p->keys);
    }
    job_flag_set(&p->state, PREP_FREE);
}

/* El juego suelta uno: si el hilo todavía está con él, lo suelta el hilo */
static void prep_release(PrepDt1 *p)
{
    int was = PREP_LOADING;
    if (__atomic_compare_exchange_n(&p->state, &was, PREP_CANCEL, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) || was != PREP_READY)
        return;
    dt1_free(&p->dt1);
    free(p->keys);
    job_flag_set(&p->state, PREP_FREE);
}

void map_prepare_dt1(const Map *m, const char *dt1)
{
    if (!jobs_running()) /* (sin hilo se haría ahora) */
        return;
    char path[96];
    dt1_path(dt1, path, sizeof path); /* (como lo va a pedir map_stamp_lib) */
    for (int i = 0; i < m->num_dt1; i++)
        if (strcmp(m->dt1_path[i], path) == 0)
            return; /* ya está (en uso o guardado) */
    PrepDt1 *slot = NULL, *oldest = NULL;
    for (int k = 0; k < MAP_PREP; k++) {
        PrepDt1 *p = &s_prep[k];
        int st = job_flag_get(&p->state);
        if (st != PREP_FREE && strcmp(p->path, path) == 0)
            return;
        if (st == PREP_FREE && !slot)
            slot = p;
        if (st == PREP_READY && (!oldest || p->order < oldest->order))
            oldest = p;
    }
    if (!slot && oldest) {
        prep_release(oldest);
        slot = oldest;
    }
    if (!slot)
        return;
    snprintf(slot->path, sizeof slot->path, "%s", path);
    slot->order = ++s_prep_order;
    job_flag_set(&slot->state, PREP_LOADING);
    jobs_push(prep_job, slot, 0);
}

/* Si ese DT1 ya se preparó, queda en el lugar idx del mapa (0); si se está
   preparando, se descarta (lo carga el juego ahora) */
static int prep_take(Map *m, int idx, const char *path)
{
    for (int k = 0; k < MAP_PREP; k++) {
        PrepDt1 *p = &s_prep[k];
        int st = job_flag_get(&p->state);
        if (st == PREP_FREE || st == PREP_CANCEL || strcmp(p->path, path) != 0)
            continue;
        if (st != PREP_READY) {
            prep_release(p);
            return -1;
        }
        m->dt1[idx] = p->dt1;
        m->dt1_keys[idx] = p->keys;
        m->dt1_kmin[idx] = p->kmin;
        m->dt1_kmax[idx] = p->kmax;
        m->dt1_bytes[idx] = p->bytes;
        memset(&p->dt1, 0, sizeof p->dt1);
        p->keys = NULL;
        job_flag_set(&p->state, PREP_FREE);
        s_dt1_ready++;
        return 0;
    }
    return -1;
}

/* Carga un DT1 una sola vez (mientras alguna zona lo use) y arma su índice
   ordenado por clave */
static int get_dt1(Map *m, const char *path)
{
    int idx = -1;
    for (int i = 0; i < m->num_dt1; i++) {
        if (strcmp(m->dt1_path[i], path) == 0) {
            mark_user(m, i);
            return i;
        }
        if (idx < 0 && !m->dt1_path[i][0])
            idx = i; /* lugar que dejó uno liberado */
    }
    if (idx < 0) {
        if (m->num_dt1 == MAP_MAX_DT1)
            return -1;
        idx = m->num_dt1;
    }

    if (prep_take(m, idx, path) != 0) {
        if (dt1_prepare(path, &m->dt1[idx], &m->dt1_keys[idx], &m->dt1_kmin[idx], &m->dt1_kmax[idx], &m->dt1_bytes[idx]) != 0) {
            fprintf(stderr, "falta el tileset %s\n", path);
            return -1;
        }
        s_dt1_loaded++;
    }
    snprintf(m->dt1_path[idx], sizeof m->dt1_path[idx], "%s", path);
    m->dt1_users[idx] = 0;
    mark_user(m, idx);
    if (idx == m->num_dt1)
        m->num_dt1++;
    return idx;
}

static void drop_dt1(Map *m, int i)
{
    dt1_free(&m->dt1[i]);
    free(m->dt1_keys[i]);
    m->dt1_keys[i] = NULL;
    m->dt1_path[i][0] = '\0';
    m->dt1_users[i] = 0;
}

/* Recorre los tiles de un DT1 con esa clave (están contiguos en el índice) */
static int dt1_range(const Map *m, int f, uint32_t key, int *first)
{
    const Dt1 *t = &m->dt1[f];
    const uint32_t *keys = m->dt1_keys[f];
    int lo = 0, hi = t->num_tiles;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const Dt1Tile *mt = &t->tiles[keys[mid]];
        if (tile_key(mt->orientation, mt->main_index, mt->sub_index) < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    *first = lo;
    int count = 0;
    while (lo + count < t->num_tiles) {
        const Dt1Tile *ct = &t->tiles[keys[lo + count]];
        if (tile_key(ct->orientation, ct->main_index, ct->sub_index) != key)
            break;
        count++;
    }
    return count;
}

/* Entre los tiles con la misma orientación, estilo y secuencia (en los DT1
   que usa este DS1) elige uno según su rareza, fijo para cada posición.
   Cada DT1 se busca una vez, y nada si la clave está fuera de las suyas: las
   piezas del Acto 3 cargan ~30 DT1 y esto era lo que más tardaba al armar
   una zona en la Pi. */
static const Dt1Tile *find_tile(const Map *m, const int *dt1s, int ndt1, int orientation, int style, int seq,
                                uint32_t seed)
{
    uint32_t key = tile_key(orientation, style, seq);
    const Dt1Tile *first = NULL;
    int total = 0, start[MAP_MAX_DT1], count[MAP_MAX_DT1];
    for (int k = 0; k < ndt1; k++) {
        int f = dt1s[k];
        count[k] = key < m->dt1_kmin[f] || key > m->dt1_kmax[f] ? 0 : dt1_range(m, f, key, &start[k]);
        for (int i = 0; i < count[k]; i++) {
            const Dt1Tile *t = &m->dt1[f].tiles[m->dt1_keys[f][start[k] + i]];
            if (!first)
                first = t;
            total += t->rarity > 0 ? t->rarity : 0;
        }
    }
    if (!first || total == 0)
        return first;

    int pick = (int)(seed % (uint32_t)total);
    for (int k = 0; k < ndt1; k++)
        for (int i = 0; i < count[k]; i++) {
            const Dt1Tile *t = &m->dt1[dt1s[k]].tiles[m->dt1_keys[dt1s[k]][start[k] + i]];
            if (t->rarity > 0 && (pick -= t->rarity) < 0)
                return t;
        }
    return first;
}

static unsigned s_version = 1;

unsigned map_version(void)
{
    return s_version;
}

int map_create(Map *m, int width, int height)
{
    s_version++;
    memset(m, 0, sizeof *m);
    m->width = width;
    m->height = height;
    m->cw = (width + MAP_CHUNK - 1) / MAP_CHUNK;
    m->ch = (height + MAP_CHUNK - 1) / MAP_CHUNK;
    m->chunks = calloc((size_t)(m->cw * m->ch), sizeof *m->chunks);
    m->zone = -1;
    if (!m->chunks) {
        map_free(m);
        return -1;
    }
    return 0;
}

int map_begin_zone(Map *m, int zone, int x0, int y0, int x1, int y1)
{
    s_version++;
    m->zone = zone;
    m->num_objects = 0;
    m->num_specials = 0;
    for (int cy = y0 / MAP_CHUNK; cy * MAP_CHUNK < y1 && cy < m->ch; cy++)
        for (int cx = x0 / MAP_CHUNK; cx * MAP_CHUNK < x1 && cx < m->cw; cx++) {
            MapChunk **c = &m->chunks[cy * m->cw + cx];
            if (*c)
                continue; /* ya es de otra zona (no debería pasar: los rectángulos no se pisan) */
            *c = calloc(1, sizeof **c);
            if (!*c)
                return -1;
            (*c)->zone = zone;
        }
    return 0;
}

void map_free_zone(Map *m, int zone)
{
    s_version++;
    for (int i = 0; i < m->cw * m->ch; i++)
        if (m->chunks[i] && m->chunks[i]->zone == zone) {
            free(m->chunks[i]);
            m->chunks[i] = NULL;
        }
    if (zone >= 0 && zone < MAP_ZONES)
        for (int i = 0; i < m->num_dt1; i++)
            if (m->dt1_path[i][0] && (m->dt1_users[i] & (1ull << zone)) &&
                (m->dt1_users[i] &= ~(1ull << zone)) == 0)
                m->dt1_idle[i] = ++m->dt1_clock;
    /* Los que no usa nadie quedan guardados hasta MAP_TILE_CACHE */
    for (;;) {
        size_t idle = 0;
        int oldest = -1;
        for (int i = 0; i < m->num_dt1; i++) {
            if (!m->dt1_path[i][0] || m->dt1_users[i])
                continue;
            idle += m->dt1_bytes[i];
            if (oldest < 0 || m->dt1_idle[i] < m->dt1_idle[oldest])
                oldest = i;
        }
        if (oldest < 0 || idle <= MAP_TILE_CACHE)
            break;
        drop_dt1(m, oldest);
    }
    while (m->num_dt1 > 0 && !m->dt1_path[m->num_dt1 - 1][0])
        m->num_dt1--;
}

/* Las marcas de la celda del DS1 que valen para toda la baldosa, como
   sub_6FD413E0 de D2Collision.cpp: salida o puerta (MAPTILE_WALL_EXIT) ->
   PRESET, bUnwalkable -> WALL y bFillLOS -> MISSILE_BARRIER */
static uint8_t cell_coll(const Ds1Cell *cell, int exit)
{
    return (uint8_t)((exit || (cell->flags & DS1F_LINKAGE) ? COLL_PRESET : 0) |
                     (cell->flags & DS1F_UNWALKABLE ? COLL_WALL : 0) | (cell->flags & DS1F_FILL_LOS ? COLL_MISSILE_BARRIER : 0));
}

/* Depuración (D2_WALL_INFO="x0,y0,x1,y1", en tiles): cada pared que se
   estampa ahí, con cuántos subtiles bloquea y de qué DT1 sale. Sirve para
   buscar muros que se atraviesan o que tapan mal. */
static void wall_info(const Map *m, int mx, int my, int l, const Ds1Cell *cell, const Dt1Tile *t, int floor)
{
    static int r[4] = { -1, 0, 0, 0 };
    if (r[0] == -1) {
        r[0] = -2;
        const char *env = getenv("D2_WALL_INFO");
        if (env)
            sscanf(env, "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]);
    }
    if (r[0] < 0 || mx < r[0] || my < r[1] || mx >= r[2] || my >= r[3])
        return;
    const char *file = "?";
    for (int f = 0; t && f < m->num_dt1; f++)
        if (t >= m->dt1[f].tiles && t < m->dt1[f].tiles + m->dt1[f].num_tiles)
            file = strrchr(m->dt1_path[f], '/') ? strrchr(m->dt1_path[f], '/') + 1 : m->dt1_path[f];
    int blocks = 0;
    for (int k = 0; t && k < TILE_SUB * TILE_SUB; k++)
        blocks += t->subtile_flags[k] & COLL_WALL;
    printf("%s %d,%d capa %d: orientación %d estilo %d secuencia %d%s, bloquea %d subtiles, alto %d%s, %s\n",
           floor ? "piso" : "pared", mx, my, l, floor ? 0 : cell->type, cell->style, cell->sequence,
           cell->hidden ? " (oculta)" : "", blocks, t ? t->img_h : 0, t && !t->pixels && !t->pending ? " (sin dibujo)" : "",
           t ? file : "sin tile");
}

/* El pedazo del tile si es de la zona que se está armando */
static MapChunk *own_chunk(const Map *m, int tx, int ty)
{
    MapChunk *c = map_chunk(m, tx, ty);
    return c && (m->zone < 0 || c->zone == m->zone) ? c : NULL;
}

int map_stamp(Map *m, const char *ds1_path, int ox, int oy)
{
    return map_stamp_lib(m, ds1_path, ox, oy, NULL, 0);
}

int map_stamp_lib(Map *m, const char *ds1_path, int ox, int oy, const char *const *dt1_paths, int num_dt1)
{
    s_version++;
    size_t n;
    uint8_t *d = fs_read(ds1_path, &n);
    Ds1 ds1;
    if (!d || ds1_load(d, n, &ds1) != 0) {
        free(d);
        fprintf(stderr, "no pude leer el mapa %s\n", ds1_path);
        return -1;
    }
    free(d);

    int dt1s[MAP_MAX_DT1], ndt1 = 0;
    for (int i = 0; i < (dt1_paths ? num_dt1 : ds1.num_files); i++) {
        char path[256];
        dt1_path(dt1_paths ? dt1_paths[i] : ds1.files[i], path, sizeof path);
        /* warp.dt1 es del editor: el original usa los DT1 de lvltypes.txt y
           ninguno lo trae. Sus salidas invisibles le ganaban a la escalera de
           subida de los sótanos del palacio, que quedaba sin dibujar. */
        const char *base = strrchr(path, '/');
        if (strcmp(base ? base + 1 : path, "warp.dt1") == 0)
            continue;
        int f = get_dt1(m, path);
        if (f >= 0 && ndt1 < MAP_MAX_DT1)
            dt1s[ndt1++] = f;
    }

    int nf = ds1.num_floors < MAP_FLOORS ? ds1.num_floors : MAP_FLOORS;
    int nw = ds1.num_walls < MAP_WALLS ? ds1.num_walls : MAP_WALLS;
    if (getenv("D2_DS1_GRID") && strstr(ds1_path, getenv("D2_DS1_GRID"))) { /* depuración: piso (estilo.secuencia) y paredes de cada celda */
        printf("%s en el tile (%d, %d), %dx%d, %d pisos, %d paredes:\n", ds1_path, ox, oy, ds1.width, ds1.height, nf, nw);
        for (int y = 0; y < ds1.height; y++)
            for (int x = 0; x < ds1.width; x++) {
                printf("  celda %d %d:", x, y);
                for (int l = 0; l < nf; l++) {
                    const Ds1Cell *f = ds1_floor(&ds1, l, x, y);
                    printf(f->prop1 ? " piso %d.%d%s" : " piso -", f->style, f->sequence, f->hidden ? " oculto" : "");
                }
                for (int l = 0; l < nw; l++) {
                    const Ds1Cell *w = ds1_wall(&ds1, l, x, y);
                    printf(w->prop1 ? " pared %d:%d.%d%s" : " pared -", w->type, w->style, w->sequence, w->hidden ? " oculta" : "");
                }
                printf("\n");
            }
    }
    for (int y = 0; y < ds1.height; y++) {
        for (int x = 0; x < ds1.width; x++) {
            int mx = ox + x, my = oy + y;
            MapChunk *ch = own_chunk(m, mx, my);
            if (!ch)
                continue; /* fuera del mapa o de otra zona */
            int c = map_cell(mx, my);
            for (int l = 0; l < nf; l++) {
                const Ds1Cell *cell = ds1_floor(&ds1, l, x, y);
                if (cell->prop1 == 0)
                    continue;
                const Dt1Tile *t = find_tile(m, dt1s, ndt1, ORI_FLOOR, cell->style, cell->sequence, hash3(mx, my, l));
                wall_info(m, mx, my, l, cell, t, 1);
                if (!t)
                    continue;
                /* Oculto (o el piso en blanco, estilo 30, secuencia 0 o 1): no se
                   dibuja pero cuenta para la colisión (DRLGROOMTILE_LoadInitRoomTiles) */
                int hide = cell->hidden || (cell->style == 30 && cell->sequence <= 1);
                ch->floor[l][c] = hide ? NULL : t;
                ch->hidden_floor[l][c] = hide ? t : NULL;
                ch->floor_coll[l][c] = cell_coll(cell, 0);
            }
            for (int l = 0; l < nw; l++) {
                const Ds1Cell *cell = ds1_wall(&ds1, l, x, y);
                if (cell->prop1 != 0 && (cell->type == ORI_SPECIAL_1 || cell->type == ORI_SPECIAL_2)) {
                    MapSpecial *grown = realloc(m->specials, (size_t)(m->num_specials + 1) * sizeof *grown);
                    if (grown) {
                        m->specials = grown;
                        m->specials[m->num_specials++] = (MapSpecial){ mx, my, cell->type, cell->style, cell->sequence };
                    }
                }
                if (cell->prop1 == 0)
                    continue;
                int exit = cell->type == ORI_SPECIAL_1 || cell->type == ORI_SPECIAL_2;
                if (exit && cell->style >= 8)
                    continue; /* esas salidas no son baldosas (DRLGROOMTILE_LoadInitRoomTiles) */
                const Dt1Tile *t =
                    find_tile(m, dt1s, ndt1, cell->type, cell->style, cell->sequence, hash3(mx, my, 10 + l));
                wall_info(m, mx, my, l, cell, t, 0);
                if (!t)
                    continue;
                int door = cell->type == ORI_LEFT_DOOR || cell->type == ORI_RIGHT_DOOR;
                if (cell->hidden) { /* invisible: no se dibuja pero cuenta para la colisión */
                    if (exit)
                        continue; /* la salida oculta es solo el salto */
                    /* shortcut: la puerta oculta del original es un objeto puerta que se abre;
                       acá sigue como pared oculta hasta que existan esas puertas */
                    ch->hidden_wall[l][c] = t;
                    ch->wall_coll[l][c] = cell_coll(cell, door);
                    continue;
                }
                ch->hidden_wall[l][c] = NULL;
                ch->wall_coll[l][c] = cell_coll(cell, exit || door);
                ch->wall[l][c] = t;
                ch->wall_extra[l][c] =
                    cell->type == ORI_RIGHT_NORTH_CORNER
                        ? find_tile(m, dt1s, ndt1, ORI_LEFT_NORTH_CORNER, cell->style, cell->sequence,
                                    hash3(mx, my, 20 + l))
                        : NULL;
            }
            const Ds1Cell *sc = &ds1.shadows[y * ds1.width + x];
            if (sc->prop1 != 0 && !sc->hidden) {
                const Dt1Tile *t = find_tile(m, dt1s, ndt1, ORI_SHADOW, sc->style, sc->sequence, hash3(mx, my, 30));
                if (t)
                    ch->shadow[c] = t;
            }
        }
    }

    if (ds1.num_objects > 0) {
        MapObject *grown = realloc(m->objects, (size_t)(m->num_objects + ds1.num_objects) * sizeof *grown);
        if (grown) {
            m->objects = grown;
            for (int i = 0; i < ds1.num_objects; i++) {
                const Ds1Object *o = &ds1.objects[i];
                float x = ox * TILE_SUB + o->x + 0.5f, y = oy * TILE_SUB + o->y + 0.5f;
                if (!own_chunk(m, (int)(x / TILE_SUB), (int)(y / TILE_SUB)))
                    continue;
                MapObject *mo = &m->objects[m->num_objects++];
                mo->type = o->type;
                mo->id = o->id;
                mo->act = ds1.act;
                mo->x = x;
                mo->y = y;
            }
        }
    }
    ds1_free(&ds1);
    return 0;
}

void map_fill_floor(Map *m, const char *dt1_file, int style, int seq, int x0, int y0, int w, int h)
{
    s_version++;
    int f = get_dt1(m, dt1_file);
    if (f < 0)
        return;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) {
            MapChunk *ch = own_chunk(m, x, y);
            if (ch)
                ch->floor[0][map_cell(x, y)] = find_tile(m, &f, 1, ORI_FLOOR, style, seq, hash3(x, y, 0));
        }
}

/* Suma a la colisión de la baldosa (lx, ly) sus 25 marcas de subtile y las
   de su celda, como sub_6FD411F0 y sub_6FD413E0 de D2Collision.cpp (el DT1
   guarda las filas al revés) */
static void coll_add_tile(MapChunk *ch, int lx, int ly, const Dt1Tile *t, uint8_t whole)
{
    for (int sy = 0; sy < TILE_SUB; sy++) {
        uint16_t *row = &ch->coll[(ly * TILE_SUB + sy) * MAP_CHUNK_SUB + lx * TILE_SUB];
        for (int sx = 0; sx < TILE_SUB; sx++)
            row[sx] |= (uint16_t)((t ? t->subtile_flags[(TILE_SUB - 1 - sy) * TILE_SUB + sx] : 0) | whole);
    }
}

void map_build_collision(Map *m, int zone)
{
    for (int i = 0; i < m->cw * m->ch; i++) {
        MapChunk *ch = m->chunks[i];
        if (!ch || (zone >= 0 && ch->zone != zone))
            continue;
        memset(ch->coll, 0, sizeof ch->coll);
        for (int ly = 0; ly < MAP_CHUNK; ly++)
            for (int lx = 0; lx < MAP_CHUNK; lx++) {
                int c = ly * MAP_CHUNK + lx, has_floor = 0;
                /* Los pisos, las paredes, las dos partes de las esquinas y las
                   sombras (COLLISION_AllocRoomCollisionGrid), dibujadas u ocultas */
                for (int l = 0; l < MAP_FLOORS; l++) {
                    const Dt1Tile *t = ch->floor[l][c] ? ch->floor[l][c] : ch->hidden_floor[l][c];
                    if (t)
                        coll_add_tile(ch, lx, ly, t, ch->floor_coll[l][c]);
                    has_floor |= t != NULL;
                }
                int has_wall = 0;
                for (int l = 0; l < MAP_WALLS; l++) {
                    const Dt1Tile *t = ch->wall[l][c] ? ch->wall[l][c] : ch->hidden_wall[l][c];
                    if (t)
                        coll_add_tile(ch, lx, ly, t, ch->wall_coll[l][c]);
                    if (ch->wall_extra[l][c])
                        coll_add_tile(ch, lx, ly, ch->wall_extra[l][c], 0);
                    has_wall |= t != NULL;
                }
                if (ch->shadow[c])
                    coll_add_tile(ch, lx, ly, ch->shadow[c], 0);
                /* Sin piso ni pared es el vacío fuera del mapa: como un subtile
                   fuera de las salas del original (COLLIDE_MASK_INVALID). Con una
                   pared sobre el piso en blanco (estilo 30, que no está en los DT1
                   del calabozo) vale lo de la pared, como en el original, que
                   arranca la sala en 0 y le suma lo de cada baldosa: por ahí se
                   sale de la sala de llegada del Maggot Lair */
                if (!has_floor && !has_wall)
                    for (int sy = 0; sy < TILE_SUB; sy++)
                        for (int sx = 0; sx < TILE_SUB; sx++)
                            ch->coll[(ly * TILE_SUB + sy) * MAP_CHUNK_SUB + lx * TILE_SUB + sx] = COLL_MASK_INVALID;
            }
    }
}

int map_load(Map *m, const char *ds1_path)
{
    size_t n;
    uint8_t *d = fs_read(ds1_path, &n);
    Ds1 ds1;
    if (!d || ds1_load(d, n, &ds1) != 0) {
        free(d);
        fprintf(stderr, "no pude leer el mapa %s\n", ds1_path);
        return -1;
    }
    free(d);
    int w = ds1.width, h = ds1.height;
    ds1_free(&ds1);
    if (map_create(m, w, h) != 0 || map_begin_zone(m, 0, 0, 0, w, h) != 0 || map_stamp(m, ds1_path, 0, 0) != 0)
        return -1;
    map_build_collision(m, 0);
    return 0;
}

void map_free(Map *m)
{
    s_version++;
    for (int k = 0; k < MAP_PREP; k++) /* (lo preparado para este mapa) */
        prep_release(&s_prep[k]);
    for (int i = 0; i < m->num_dt1; i++)
        if (m->dt1_path[i][0])
            drop_dt1(m, i);
    for (int i = 0; m->chunks && i < m->cw * m->ch; i++)
        free(m->chunks[i]);
    free(m->chunks);
    free(m->objects);
    free(m->specials);
    memset(m, 0, sizeof *m);
}

uint16_t map_coll(const Map *m, int sx, int sy)
{
    if (sx < 0 || sy < 0)
        return COLL_MASK_INVALID;
    const MapChunk *c = map_chunk(m, sx / TILE_SUB, sy / TILE_SUB);
    if (!c)
        return COLL_MASK_INVALID;
    return c->coll[(sy % MAP_CHUNK_SUB) * MAP_CHUNK_SUB + sx % MAP_CHUNK_SUB];
}

int map_blocked(const Map *m, int sx, int sy)
{
    return (map_coll(m, sx, sy) & COLL_MASK_PLAYER_PATH) != 0;
}

void map_set_coll(Map *m, int sx, int sy, uint16_t bits)
{
    if (sx < 0 || sy < 0)
        return;
    MapChunk *c = map_chunk(m, sx / TILE_SUB, sy / TILE_SUB);
    if (c)
        c->coll[(sy % MAP_CHUNK_SUB) * MAP_CHUNK_SUB + sx % MAP_CHUNK_SUB] |= bits;
}

void map_clear_coll(Map *m, int sx, int sy, uint16_t bits)
{
    if (sx < 0 || sy < 0)
        return;
    MapChunk *c = map_chunk(m, sx / TILE_SUB, sy / TILE_SUB);
    if (c)
        c->coll[(sy % MAP_CHUNK_SUB) * MAP_CHUNK_SUB + sx % MAP_CHUNK_SUB] &= (uint16_t)~bits;
}

/* Una comprobación de map_selftest */
static int check(int ok, const char *what, int *fails)
{
    if (!ok) {
        printf("  FALLA: %s\n", what);
        (*fails)++;
    }
    return ok;
}

int map_selftest(void)
{
    int fails = 0;
    Map m;
    if (map_create(&m, 16, 8) != 0 || map_begin_zone(&m, 0, 0, 0, 16, 8) != 0)
        return 1;
    /* Un piso que solo bloquea el paso en la fila de abajo del DT1 (que es la
       de arriba en el mapa: el DT1 guarda las filas al revés), una pared que
       es pared de verdad (paso, vista y proyectiles) y una celda marcada
       "no se camina" en el DS1 */
    static Dt1Tile floor_low, wall;
    memset(&floor_low, 0, sizeof floor_low);
    memset(&wall, 0, sizeof wall);
    for (int sx = 0; sx < TILE_SUB; sx++)
        floor_low.subtile_flags[(TILE_SUB - 1) * TILE_SUB + sx] = COLL_WALL;
    for (int k = 0; k < TILE_SUB * TILE_SUB; k++)
        wall.subtile_flags[k] = COLL_WALL | COLL_VISIBLE | COLL_MISSILE_BARRIER;
    MapChunk *ch = m.chunks[0];
    for (int tx = 0; tx < 8; tx++)
        for (int ty = 0; ty < 8; ty++)
            ch->floor[0][map_cell(tx, ty)] = &floor_low; /* el pedazo 0 entero con piso */
    ch->floor_coll[0][map_cell(2, 0)] = COLL_WALL;                /* bUnwalkable */
    ch->wall[0][map_cell(4, 1)] = &wall;
    ch->hidden_wall[1][map_cell(5, 1)] = &wall;                   /* oculta: no se ve pero cuenta */
    map_build_collision(&m, 0);
    check(map_coll(&m, 0, 0) == COLL_WALL, "la fila 4 del DT1 es la de arriba en el mapa", &fails);
    check(map_coll(&m, 0, 1) == 0, "el resto del piso se camina", &fails);
    check(map_coll(&m, 2 * TILE_SUB + 2, 3) == COLL_WALL, "la marca de la celda vale para sus 25 subtiles", &fails);
    check((map_coll(&m, 4 * TILE_SUB + 1, TILE_SUB + 2) & COLL_MISSILE_BARRIER) != 0, "la pared frena proyectiles", &fails);
    check((map_coll(&m, 5 * TILE_SUB + 1, TILE_SUB + 2) & COLL_WALL) != 0, "la pared oculta bloquea", &fails);
    check(map_coll(&m, 9 * TILE_SUB, 2) == COLL_MASK_INVALID, "sin pedazo es el vacío", &fails);
    check(map_blocked(&m, 0, 0) && !map_blocked(&m, 0, 1), "map_blocked mira la máscara de caminar", &fails);
    /* El rayo: choca con la pared y se queda en el último subtile libre */
    int ex, ey;
    int hit = map_ray(&m, 1, TILE_SUB + 2, 35, TILE_SUB + 2, COLL_MISSILE_BARRIER, &ex, &ey);
    check(hit && ex == 4 * TILE_SUB - 1 && ey == TILE_SUB + 2, "el rayo para antes de la pared", &fails);
    check(!map_ray(&m, 1, 3, 30, 3, COLL_MISSILE_BARRIER, NULL, NULL), "el rayo pasa sobre lo que solo no se camina", &fails);
    map_free(&m);
    return fails;
}

int map_ray(const Map *m, int x0, int y0, int x1, int y1, uint16_t mask, int *ex, int *ey)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    int x = x0, y = y0, lx = x0, ly = y0;
    if (map_coll(m, x0, y0) & COLL_BLANK) { /* empieza afuera: choca ahí (como el original) */
        if (ex) *ex = x0;
        if (ey) *ey = y0;
        return 1;
    }
    for (;;) {
        if (map_coll(m, x, y) & mask) {
            if (ex) *ex = lx;
            if (ey) *ey = ly;
            return 1;
        }
        lx = x;
        ly = y;
        if (x == x1 && y == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
    if (ex) *ex = x1;
    if (ey) *ey = y1;
    return 0;
}

int map_zone_of(const Map *m, int tx, int ty)
{
    const MapChunk *c = map_chunk(m, tx, ty);
    return c ? c->zone : -1;
}

size_t map_memory(const Map *m)
{
    size_t total = 0;
    for (int i = 0; i < m->cw * m->ch; i++)
        total += m->chunks[i] ? sizeof *m->chunks[i] : 0;
    for (int i = 0; i < m->num_dt1; i++)
        total += m->dt1_path[i][0] ? m->dt1_bytes[i] : 0;
    return total;
}
