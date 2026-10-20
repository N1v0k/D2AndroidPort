#ifndef MAP_H
#define MAP_H

#include "fmt/formats.h"
#include "iso.h"

#include <stdint.h>

#define MAP_MAX_DT1 96
#define MAP_FLOORS  2
#define MAP_WALLS   4
#define MAP_ZONES   64 /* zonas distintas que pueden usar los tilesets (una marca por zona) */
/* Tilesets que ya no usa ninguna zona: se guardan hasta este total por si
   se vuelve (las zonas de afuera comparten casi todos) */
#define MAP_TILE_CACHE (24u << 20)

/* El mapa se guarda en pedazos de 8x8 tiles (las celdas de las zonas
   exteriores). Cada pedazo es de una zona y se libera con ella; donde no hay
   pedazo es vacío (no se camina ni se dibuja). */
#define MAP_CHUNK      8
#define MAP_CHUNK_SUB  (MAP_CHUNK * TILE_SUB)
#define MAP_CHUNK_TILES (MAP_CHUNK * MAP_CHUNK)

/* La colisión de cada subtile, como la del original (D2Collision.h de
   D2MOO): los bits de las marcas de las baldosas y lo que ocupa cada cosa.
   Cada uno mira solo los bits que le importan (las máscaras de abajo). */
enum {
    COLL_WALL = 0x0001,            /* paredes, acantilados, el vacío del Santuario: no se pasa */
    COLL_VISIBLE = 0x0002,         /* obstáculos por encima de los que no se dispara */
    COLL_MISSILE_BARRIER = 0x0004, /* frena proyectiles y lo que vuela */
    COLL_NOPLAYER = 0x0008,
    COLL_PRESET = 0x0010,          /* salidas, puertas y caminos de las piezas prearmadas */
    COLL_BLANK = 0x0020,           /* fuera del mapa */
    COLL_MISSILE = 0x0040,
    COLL_PLAYER = 0x0080,
    COLL_MONSTER = 0x0100,
    COLL_ITEM = 0x0200,
    COLL_OBJECT = 0x0400,          /* cofres, barriles, altares */
    COLL_DOOR = 0x0800,            /* puertas cerradas */
    COLL_NO_PATH = 0x1000,
    COLL_PET = 0x2000,
    COLL_CORPSE = 0x8000,          /* cadáveres y portales */
    COLL_MASK_INVALID = COLL_BLANK | COLL_MISSILE_BARRIER | COLL_VISIBLE | COLL_WALL,
    /* Por dónde camina cada uno (Path.cpp y D2Collision.h) */
    COLL_MASK_PLAYER_PATH = COLL_WALL | COLL_NOPLAYER | COLL_OBJECT | COLL_DOOR | COLL_NO_PATH,
    COLL_MASK_MONSTER_PATH = COLL_WALL | COLL_OBJECT | COLL_NO_PATH | COLL_PET | COLL_DOOR,
    COLL_MASK_FLYING = COLL_MISSILE_BARRIER | COLL_DOOR | COLL_NO_PATH,  /* murciélagos, buitres */
    COLL_MASK_RADIAL_BARRIER = COLL_DOOR | COLL_MISSILE_BARRIER | COLL_WALL,
};

/* Un monstruo u objeto que un DS1 trae ubicado, ya en coordenadas del mapa */
typedef struct {
    int   type; /* 1 = monstruo/NPC, 2 = objeto */
    int   id;
    int   act;
    float x, y; /* en subtiles */
} MapObject;

/* Tile especial invisible (orientación 10 u 11): marca, por ejemplo, la boca
   de una cueva o una escalera que lleva a otro nivel */
typedef struct {
    int x, y; /* en tiles del mapa */
    int orientation, style, sequence;
} MapSpecial;

typedef struct {
    int            zone;
    const Dt1Tile *floor[MAP_FLOORS][MAP_CHUNK_TILES];
    const Dt1Tile *wall[MAP_WALLS][MAP_CHUNK_TILES];
    const Dt1Tile *wall_extra[MAP_WALLS][MAP_CHUNK_TILES]; /* esquina norte: la parte izquierda */
    /* Baldosas ocultas del DS1: no se dibujan pero cuentan para la colisión,
       como en el original (las barreras invisibles alrededor del campamento,
       el vacío del Santuario Arcano) */
    const Dt1Tile *hidden_floor[MAP_FLOORS][MAP_CHUNK_TILES];
    const Dt1Tile *hidden_wall[MAP_WALLS][MAP_CHUNK_TILES];
    /* Las marcas de la celda de cada capa (COLL_WALL, COLL_MISSILE_BARRIER,
       COLL_PRESET): valen para sus 25 subtiles */
    uint8_t        floor_coll[MAP_FLOORS][MAP_CHUNK_TILES];
    uint8_t        wall_coll[MAP_WALLS][MAP_CHUNK_TILES];
    const Dt1Tile *shadow[MAP_CHUNK_TILES];
    uint16_t       coll[MAP_CHUNK_SUB * MAP_CHUNK_SUB]; /* colisión por subtile (COLL_*) */
} MapChunk;

/* El mundo armado con DS1 estampados en una grilla de tiles */
typedef struct {
    int             width, height; /* en tiles */
    int             cw, ch;        /* en pedazos */
    MapChunk      **chunks;        /* [cy][cx], NULL = vacío */

    int             num_dt1;
    Dt1             dt1[MAP_MAX_DT1];
    char            dt1_path[MAP_MAX_DT1][96]; /* "" = lugar libre */
    uint32_t       *dt1_keys[MAP_MAX_DT1];     /* índice ordenado para buscar tiles rápido */
    uint32_t        dt1_kmin[MAP_MAX_DT1], dt1_kmax[MAP_MAX_DT1]; /* su primera y su última clave */
    uint64_t        dt1_users[MAP_MAX_DT1];    /* zonas que usan cada tileset (un bit por zona) */
    size_t          dt1_bytes[MAP_MAX_DT1];
    uint32_t        dt1_idle[MAP_MAX_DT1];     /* cuándo lo dejó de usar la última zona */
    uint32_t        dt1_clock;

    int             zone; /* zona que se está armando: se estampa solo en sus pedazos */

    /* Lo que trajeron los DS1 estampados desde map_begin_zone */
    int             num_objects;
    MapObject      *objects;
    int             num_specials;
    MapSpecial     *specials;
} Map;

/* Cambia cada vez que se arma, estampa o suelta algo del mapa (de cualquier
   mapa): lo guardado de un dibujo anterior del piso ya no vale */
unsigned map_version(void);

int  map_create(Map *m, int width, int height); /* el mundo vacío */

/* Pedazos de una zona: se crean para el rectángulo (en tiles, alineado a 8)
   y desde ahí se estampa en ellos. map_free_zone los suelta junto con los
   tilesets que ya no use nadie. */
int  map_begin_zone(Map *m, int zone, int x0, int y0, int x1, int y1);
void map_free_zone(Map *m, int zone);

/* Copia un DS1 en el mapa con su esquina en el tile (ox, oy). Las celdas
   vacías del DS1 no pisan lo que ya había. */
int  map_stamp(Map *m, const char *ds1_path, int ox, int oy);
/* Igual, pero con los tiles de esos DT1 en vez de los que lista el DS1 (como
   el original: los de lvltypes.txt que pide el Dt1Mask de la pieza) */
int  map_stamp_lib(Map *m, const char *ds1_path, int ox, int oy, const char *const *dt1_paths, int num_dt1);

/* Lee ese DT1 en el hilo de trabajos para la zona que se va a armar pronto
   (sin hilo, nada: lo carga la zona al armarse, como siempre) */
void map_prepare_dt1(const Map *m, const char *path);
void map_dt1_loads(int *ready, int *loaded); /* desde la última vez: los que llegaron preparados y los leídos al armar */

/* Rellena un rectángulo con un piso (de un DT1 y un estilo/secuencia) */
void map_fill_floor(Map *m, const char *dt1_path, int style, int seq, int x0, int y0, int w, int h);

/* Calcula las colisiones de los pedazos de la zona; va después de estampar */
void map_build_collision(Map *m, int zone);

/* Atajo: un mapa de un solo DS1 */
int  map_load(Map *m, const char *ds1_path);

void map_free(Map *m);
uint16_t map_coll(const Map *m, int sx, int sy);        /* los COLL_* del subtile (fuera del mapa: COLL_MASK_INVALID) */
int  map_blocked(const Map *m, int sx, int sy);          /* para caminar (COLL_MASK_PLAYER_PATH) */
void map_set_coll(Map *m, int sx, int sy, uint16_t bits); /* lo que ocupan los objetos y las puertas */
void map_clear_coll(Map *m, int sx, int sy, uint16_t bits);
/* El trazado de una línea por subtiles, como COLLISION_RayTrace (Bresenham):
   1 si choca con `mask` antes de llegar; en (*ex, *ey) el último subtile libre */
int  map_ray(const Map *m, int x0, int y0, int x1, int y1, uint16_t mask, int *ex, int *ey);
int  map_selftest(void); /* prueba de la colisión con baldosas inventadas: cuántas fallaron */
int  map_zone_of(const Map *m, int tx, int ty); /* dueña del pedazo, -1 = vacío */
size_t map_memory(const Map *m);                /* bytes en pedazos y tilesets (para D2_MEM) */

static inline MapChunk *map_chunk(const Map *m, int tx, int ty)
{
    if ((unsigned)tx >= (unsigned)m->width || (unsigned)ty >= (unsigned)m->height)
        return NULL;
    return m->chunks[(ty / MAP_CHUNK) * m->cw + tx / MAP_CHUNK];
}

static inline int map_cell(int tx, int ty)
{
    return (ty % MAP_CHUNK) * MAP_CHUNK + tx % MAP_CHUNK;
}

static inline const Dt1Tile *map_floor(const Map *m, int layer, int tx, int ty)
{
    const MapChunk *c = map_chunk(m, tx, ty);
    return c ? c->floor[layer][map_cell(tx, ty)] : NULL;
}

static inline const Dt1Tile *map_wall(const Map *m, int layer, int tx, int ty)
{
    const MapChunk *c = map_chunk(m, tx, ty);
    return c ? c->wall[layer][map_cell(tx, ty)] : NULL;
}

static inline const Dt1Tile *map_wall_extra(const Map *m, int layer, int tx, int ty)
{
    const MapChunk *c = map_chunk(m, tx, ty);
    return c ? c->wall_extra[layer][map_cell(tx, ty)] : NULL;
}

static inline const Dt1Tile *map_shadow(const Map *m, int tx, int ty)
{
    const MapChunk *c = map_chunk(m, tx, ty);
    return c ? c->shadow[map_cell(tx, ty)] : NULL;
}

#endif
