#ifndef ISO_H
#define ISO_H

/* Geometría de Diablo II: un tile de piso mide 160x80 px y se divide en
   5x5 subtiles de 32x16. Las coordenadas del mundo están en subtiles. */
#define SUBTILE_W 32
#define SUBTILE_H 16
#define TILE_SUB  5
#define TILE_W    (SUBTILE_W * TILE_SUB)
#define TILE_H    (SUBTILE_H * TILE_SUB)

static inline void iso_to_screen(float wx, float wy, float *sx, float *sy)
{
    *sx = (wx - wy) * (SUBTILE_W / 2);
    *sy = (wx + wy) * (SUBTILE_H / 2);
}

/* Dirección en pantalla (y hacia abajo) -> dirección en el mundo, sin normalizar. */
static inline void screen_dir_to_world(float sx, float sy, float *wx, float *wy)
{
    *wx = sx / SUBTILE_W + sy / SUBTILE_H;
    *wy = sy / SUBTILE_H - sx / SUBTILE_W;
}

#endif
