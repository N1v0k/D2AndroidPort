#ifndef FORMATS_H
#define FORMATS_H

/* Lectores de los formatos clásicos de Diablo II (los que el Resurrected
   todavía trae en data/global). Todas las funciones reciben el archivo ya
   cargado en memoria y devuelven 0 si salió bien. */

#include <stddef.h>
#include <stdint.h>

/* ---- Paleta: pal.dat, 256 colores en orden B, G, R ---- */

int pal_load(const uint8_t *data, size_t size, uint32_t out_argb[256]);

/* ---- Sprites: DC6 (interfaz, objetos del inventario) y DCC (personajes, monstruos) ---- */

typedef struct {
    int      width, height;
    int      off_x, off_y; /* esquina superior izquierda relativa al punto de anclaje */
    uint8_t *pixels;       /* índices de paleta, 0 = transparente */
    uint8_t *spans;        /* los tramos opacos de cada fila (ver spans_build) */
} SpriteFrame;

/* Tramos opacos de una imagen, para dibujarla copiando de a tramos en vez
   de mirar la transparencia píxel por píxel: (alto + 1) desplazamientos de
   32 bits y después, por fila, pares de 16 bits (x, largo). NULL sin memoria
   o sin imagen; se libera con free(). */
uint8_t *spans_build(const uint8_t *px, int w, int h);

typedef struct {
    int          directions;
    int          frames_per_dir;
    SpriteFrame *frames; /* directions * frames_per_dir */
} Sprite;

int  dc6_load(const uint8_t *data, size_t size, Sprite *out);
int  dcc_load(const uint8_t *data, size_t size, Sprite *out);
/* Un .d2rw: los cuadros en crudo, el formato de `d2fmt raw` (un dibujo
   editado que se carga en lugar del DCC) */
int  raw_load(const uint8_t *data, size_t size, Sprite *out);
void sprite_free(Sprite *s);

static inline SpriteFrame *sprite_frame(const Sprite *s, int dir, int frame)
{
    return &s->frames[dir * s->frames_per_dir + frame];
}

/* ---- COF: cómo se arma una animación con capas (cabeza, torso, piernas...) ---- */

enum {
    COMP_HD, COMP_TR, COMP_LG, COMP_RA, COMP_LA, COMP_RH, COMP_LH, COMP_SH,
    COMP_S1, COMP_S2, COMP_S3, COMP_S4, COMP_S5, COMP_S6, COMP_S7, COMP_S8,
    COMP_COUNT
};

extern const char *const k_comp_names[COMP_COUNT];

typedef struct {
    uint8_t component;
    uint8_t shadow;
    uint8_t selectable;
    uint8_t transparent;
    uint8_t draw_effect;
    char    weapon_class[4]; /* "hth", "1hs", ... (terminado en 0) */
} CofLayer;

typedef struct {
    int       num_layers;
    int       frames_per_dir;
    int       directions;
    CofLayer  layers[COMP_COUNT];
    uint8_t  *keyframes; /* frames_per_dir: 0 nada, 1 golpe, 2 proyectil, 3 sonido, 4 habilidad */
    uint8_t  *priority;  /* [dir][frame][orden] = componente a dibujar */
} Cof;

int  cof_load(const uint8_t *data, size_t size, Cof *out);
void cof_free(Cof *c);

/* ---- Tablas .txt de data/global/excel ---- */

typedef struct {
    int    rows, cols; /* la fila 0 son los nombres de las columnas */
    char **cells;
    char  *buf;
    int   *col_hash;   /* nombre de columna -> columna + 1 (0 = libre); no cambia después de cargar */
    int    col_mask;
} TxtTable;

int         txt_load(const uint8_t *data, size_t size, TxtTable *t);
int         txt_col(const TxtTable *t, const char *name);
int         txt_find_row(const TxtTable *t, int col, const char *value);
const char *txt_get(const TxtTable *t, int row, int col);
int         txt_get_int(const TxtTable *t, int row, const char *col_name, int fallback);
void        txt_free(TxtTable *t);

/* ---- animdata.d2: velocidad y eventos de cada animación ---- */

typedef struct {
    char    name[8];         /* nombre del COF en minúsculas, ej. "batwhth" */
    int32_t frames_per_dir;
    int32_t speed;           /* 1/256 de cuadro por tick (256 = un cuadro por tick) */
    uint8_t events[144];     /* evento en cada cuadro (golpe, sonido...) */
} AnimRecord;

typedef struct {
    int         count;
    AnimRecord *records;
} AnimData;

int               animdata_load(const uint8_t *data, size_t size, AnimData *out);
const AnimRecord *animdata_find(const AnimData *a, const char *name);
void              animdata_free(AnimData *a);

/* ---- DT1: tiles de piso, paredes, techos y sombras ---- */

enum {
    ORI_FLOOR = 0,
    ORI_LEFT_WALL = 1,
    ORI_RIGHT_WALL = 2,
    ORI_RIGHT_NORTH_CORNER = 3,
    ORI_LEFT_NORTH_CORNER = 4,
    ORI_LEFT_END = 5,
    ORI_RIGHT_END = 6,
    ORI_SOUTH_CORNER = 7,
    ORI_LEFT_DOOR = 8,
    ORI_RIGHT_DOOR = 9,
    ORI_SPECIAL_1 = 10,
    ORI_SPECIAL_2 = 11,
    ORI_PILLAR = 12,
    ORI_SHADOW = 13,
    ORI_TREE = 14,
    ORI_ROOF = 15,
    ORI_LOWER_LEFT = 16,
    ORI_LOWER_RIGHT = 17,
    ORI_LOWER_NORTH = 18,
    ORI_LOWER_SOUTH = 19
};

typedef struct {
    int32_t direction;
    int16_t roof_height;
    uint8_t sound;
    uint8_t animated;
    int32_t height;
    int32_t width;
    int32_t orientation;
    int32_t main_index;
    int32_t sub_index;
    int32_t rarity;
    uint8_t subtile_flags[25]; /* bit 0: bloquea el paso, ver docs de DT1 */

    /* Imagen decodificada, 160 px de ancho. img_y es la fila superior
       relativa al origen del tile (negativa en las paredes). */
    int      img_w, img_h, img_y;
    uint8_t *pixels;
    uint8_t *spans; /* sus tramos opacos (spans_build) */

    /* Con dt1_load_lazy, la imagen se arma la primera vez que se pide
       (dt1_tile_ready): de dónde sale en el archivo */
    const uint8_t *raw;
    size_t         raw_size;
    int32_t        blocks_ptr, nblocks;
    uint8_t        pending; /* 1 = todavía sin decodificar */

    /* dt1_volume, calculado la primera vez que se pide */
    uint8_t        volume_done;
    uint64_t       volume;
} Dt1Tile;

typedef struct {
    int      num_tiles;
    Dt1Tile *tiles;
    uint8_t *raw; /* el archivo, con dt1_load_lazy (se suelta en dt1_free) */
} Dt1;

/* Lee el DT1 y decodifica todas las imágenes */
int  dt1_load(const uint8_t *data, size_t size, Dt1 *out);
/* Lee solo las cabeceras (lo de la colisión y la búsqueda de baldosas, y el
   tamaño de cada imagen) y se queda con `data` (que se suelta en
   dt1_free): cada imagen se decodifica la primera vez que se pide con
   dt1_tile_ready. En la Pi decodificar todo al armar una zona del desierto
   tardaba unos 280 ms, la mayoría de baldosas que no se veían. */
int  dt1_load_lazy(uint8_t *data, size_t size, Dt1 *out);
/* La baldosa con su imagen (la decodifica si hace falta) */
const Dt1Tile *dt1_tile_ready(const Dt1Tile *t);
/* El grosor de la pared sobre el piso: los subtiles libres pegados por
   delante (x o y más grandes) a los que bloquea la baldosa y que su dibujo
   tapa en más de la mitad. Bit sy * 6 + sx, con sx y sy de 0 a 5 (la fila o
   columna 5 es de la baldosa de delante). En las cloacas del Acto 3 la base
   de los bloques y las puntas de las esquinas se dibujan un subtile más allá
   de lo que bloquean, y el personaje se paraba encima del dibujo. */
uint64_t dt1_volume(const Dt1Tile *t);
void dt1_free(Dt1 *t);

/* ---- DS1: mapas prearmados (el campamento, entradas de cuevas, etc.) ---- */

typedef struct {
    uint8_t prop1; /* 0 = celda vacía */
    uint8_t sequence;
    uint8_t style;
    uint8_t hidden;
    uint8_t type;  /* orientación: 0 piso, 13 sombra, 1..19 paredes */
    uint8_t flags; /* DS1F_*: las marcas de la celda que cuentan para la colisión */
} Ds1Cell;

/* Las marcas de una celda del DS1 que van a la colisión (los bits 16, 17 y
   28 del valor de la celda: bFillLOS, bUnwalkable y bLinkage de
   D2C_PackedTileInformation en D2MOO) */
enum { DS1F_FILL_LOS = 1, DS1F_UNWALKABLE = 2, DS1F_LINKAGE = 4 };

typedef struct {
    int32_t type, id, x, y, flags;
} Ds1Object;

/* Un grupo de sustitución (los archivos de reglas de lvlsub.txt): el
   rectángulo con el patrón y cuántas alternativas siguen a su derecha */
typedef struct {
    int32_t x, y, w, h, alts;
} Ds1Group;

typedef struct {
    int        version;
    int        width, height; /* en tiles */
    int        act;           /* 1..5 */
    int        subst_type;
    int        num_files;
    char     **files;         /* DT1 que usa el mapa */
    int        num_walls;
    int        num_floors;
    Ds1Cell   *walls;         /* [capa][y][x] */
    Ds1Cell   *floors;        /* [capa][y][x] */
    Ds1Cell   *shadows;       /* [y][x] */
    int        num_objects;
    Ds1Object *objects;
    int        num_groups;
    Ds1Group  *groups;        /* (los que tienen subst_type 1 o 2, desde la versión 12) */
} Ds1;

int  ds1_load(const uint8_t *data, size_t size, Ds1 *out);
void ds1_free(Ds1 *m);

static inline Ds1Cell *ds1_wall(const Ds1 *m, int layer, int x, int y)
{
    return &m->walls[(layer * m->height + y) * m->width + x];
}

static inline Ds1Cell *ds1_floor(const Ds1 *m, int layer, int x, int y)
{
    return &m->floors[(layer * m->height + y) * m->width + x];
}

/* ---- Fuentes TrueType (las .ttf del Resurrected), ttf.c ---- */

typedef struct {
    const uint8_t *data; /* el archivo entero (no se copia: tiene que seguir ahí) */
    size_t         size;
    uint32_t       cmap, hmtx, loca, glyf;
    int            cmap_fmt, loc_long, num_glyphs, num_hmetrics, upem, ascent, descent;
} Ttf;

int      ttf_open(Ttf *t, const uint8_t *data, size_t size);
int      ttf_glyph_index(const Ttf *t, uint32_t cp); /* 0 = no la tiene */
int      ttf_advance(const Ttf *t, int glyph);       /* en unidades (upem por cuadratín) */
int      ttf_glyph_top(const Ttf *t, int glyph);     /* lo más alto del dibujo, en unidades */
/* La letra a ese tamaño (px_size píxeles por cuadratín): cuánto cubre cada
   píxel (0..255), w x h, desde (x0, y0) respecto de la línea base (y hacia
   abajo). NULL si no tiene dibujo (el espacio). */
uint8_t *ttf_render(const Ttf *t, int glyph, float px_size, int *w, int *h, int *x0, int *y0);

#endif
