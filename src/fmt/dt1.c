#include "formats.h"

#include "bytes.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TILE_HEADER_SIZE  96
#define BLOCK_HEADER_SIZE 20

typedef struct {
    int16_t x, y;
    int16_t format; /* 1 = bloque isométrico de 32x15, si no RLE de 32x32 */
    int32_t length;
    int32_t offset;
} Block;

/* Copia un tramo de `n` píxeles en (x, y) del tile, recortado a la imagen:
   los bloques vienen en filas contiguas, así se copian de una vez */
static void put_run(Dt1Tile *t, int x, int y, const uint8_t *src, int n)
{
    if (y < 0 || y >= t->img_h || n <= 0)
        return;
    if (x < 0) {
        src -= x;
        n += x;
        x = 0;
    }
    if (x + n > t->img_w)
        n = t->img_w - x;
    if (n > 0)
        memcpy(t->pixels + y * t->img_w + x, src, (size_t)n);
}

static void decode_iso_block(Dt1Tile *t, const Block *b, const uint8_t *d)
{
    static const int k_xjump[15] = { 14, 12, 10, 8, 6, 4, 2, 0, 2, 4, 6, 8, 10, 12, 14 };
    static const int k_width[15] = { 4, 8, 12, 16, 20, 24, 28, 32, 28, 24, 20, 16, 12, 8, 4 };
    int p = 0;
    for (int row = 0; row < 15 && p < b->length; row++) {
        int n = k_width[row] < b->length - p ? k_width[row] : b->length - p;
        put_run(t, b->x + k_xjump[row], b->y - t->img_y + row, d + p, n);
        p += n;
    }
}

static void decode_rle_block(Dt1Tile *t, const Block *b, const uint8_t *d)
{
    int x = 0, y = 0, p = 0;
    while (p + 1 < b->length) {
        int skip = d[p++], count = d[p++];
        if (skip == 0 && count == 0) {
            x = 0;
            y++;
            continue;
        }
        x += skip;
        if (count > b->length - p)
            count = b->length - p;
        put_run(t, b->x + x, b->y - t->img_y + y, d + p, count);
        p += count;
        x += count;
    }
}

/* Con `pixels` en 0 solo calcula el tamaño de la imagen (img_y, img_h) */
/* `pixels`: 0 solo el tamaño, 1 la imagen y sus spans, DECODE_BAND solo los
   bloques que tocan las filas [band_lo, band_hi) (contadas desde el borde de
   arriba del tile, como Block.y) y sin spans: lo que mira dt1_volume */
#define DECODE_BAND 2

static int decode_tile(const uint8_t *data, size_t size, Dt1Tile *t, int32_t blocks_ptr, int32_t nblocks, int pixels,
                       int band_lo, int band_hi)
{
    if (nblocks <= 0)
        return 0;
    if (nblocks > 4096)
        return -1;

    Block *blocks = calloc((size_t)nblocks, sizeof *blocks);
    if (!blocks)
        return -1;

    ByteReader r = rb_make(data, size);
    int min_y = INT_MAX, max_y = INT_MIN;
    for (int i = 0; i < nblocks; i++) {
        rb_seek(&r, (size_t)blocks_ptr + (size_t)i * BLOCK_HEADER_SIZE);
        Block *b = &blocks[i];
        b->x = rb_i16(&r);
        b->y = rb_i16(&r);
        rb_skip(&r, 4); /* ceros, posición en la grilla */
        b->format = rb_i16(&r);
        b->length = rb_i32(&r);
        rb_skip(&r, 2);
        b->offset = rb_i32(&r);
        if (b->y < min_y) min_y = b->y;
        if (b->y + 32 > max_y) max_y = b->y + 32;
    }
    if (r.err) {
        free(blocks);
        return -1;
    }

    t->img_w = 160;
    t->img_y = min_y;
    t->img_h = max_y - min_y;
    if (!pixels) {
        free(blocks);
        return 0;
    }
    t->pixels = calloc((size_t)(t->img_w * t->img_h), 1);
    if (!t->pixels) {
        free(blocks);
        return -1;
    }

    for (int i = 0; i < nblocks; i++) {
        const Block *b = &blocks[i];
        size_t start = (size_t)blocks_ptr + (size_t)b->offset;
        if (b->length <= 0 || start + (size_t)b->length > size)
            continue;
        if (pixels == DECODE_BAND && (b->y + 32 <= band_lo || b->y >= band_hi))
            continue; /* (un bloque ocupa a lo sumo 32 filas desde su y) */
        if (b->format == 1)
            decode_iso_block(t, b, data + start);
        else
            decode_rle_block(t, b, data + start);
    }
    free(blocks);
    if (pixels != DECODE_BAND)
        t->spans = spans_build(t->pixels, t->img_w, t->img_h);
    return 0;
}

static int load(const uint8_t *data, size_t size, Dt1 *out, int lazy)
{
    memset(out, 0, sizeof *out);
    ByteReader r = rb_make(data, size);
    int32_t v1 = rb_i32(&r), v2 = rb_i32(&r);
    rb_skip(&r, 260);
    int32_t n = rb_i32(&r), headers = rb_i32(&r);
    if (r.err || v1 != 7 || v2 != 6 || n < 0 || n > 65536)
        return -1;

    out->num_tiles = n;
    out->tiles = calloc((size_t)(n ? n : 1), sizeof *out->tiles);
    if (!out->tiles)
        return -1;

    for (int i = 0; i < n; i++) {
        Dt1Tile *t = &out->tiles[i];
        rb_seek(&r, (size_t)headers + (size_t)i * TILE_HEADER_SIZE);
        t->direction = rb_i32(&r);
        t->roof_height = rb_i16(&r);
        t->sound = rb_u8(&r);
        t->animated = rb_u8(&r);
        t->height = rb_i32(&r);
        t->width = rb_i32(&r);
        rb_skip(&r, 4);
        t->orientation = rb_i32(&r);
        t->main_index = rb_i32(&r);
        t->sub_index = rb_i32(&r);
        t->rarity = rb_i32(&r);
        rb_skip(&r, 4);
        for (int k = 0; k < 25; k++)
            t->subtile_flags[k] = rb_u8(&r);
        rb_skip(&r, 7);
        int32_t blocks_ptr = rb_i32(&r);
        rb_skip(&r, 4); /* largo total de los bloques */
        int32_t nblocks = rb_i32(&r);
        if (r.err || decode_tile(data, size, t, blocks_ptr, nblocks, !lazy, 0, 0) != 0) {
            dt1_free(out);
            return -1;
        }
        if (lazy && nblocks > 0) {
            t->raw = data;
            t->raw_size = size;
            t->blocks_ptr = blocks_ptr;
            t->nblocks = nblocks;
            t->pending = 1;
        }
    }
    return 0;
}

int dt1_load(const uint8_t *data, size_t size, Dt1 *out)
{
    return load(data, size, out, 0);
}

int dt1_load_lazy(uint8_t *data, size_t size, Dt1 *out)
{
    if (load(data, size, out, 1) != 0)
        return -1;
    out->raw = data;
    return 0;
}

const Dt1Tile *dt1_tile_ready(const Dt1Tile *t)
{
    if (t && t->pending) {
        Dt1Tile *w = (Dt1Tile *)t;
        w->pending = 0;
        if (decode_tile(w->raw, w->raw_size, w, w->blocks_ptr, w->nblocks, 1, 0, 0) != 0)
            w->pixels = NULL; /* (sin imagen: no se dibuja) */
    }
    return t;
}

/* Solo las paredes de verdad: las que frenan el paso y los proyectiles (bits
   0 y 2). Las cosas bajas que solo frenan el paso (las lápidas del
   cementerio, las plantas del pantano) se dibujan más altas que anchas, y
   con su "grosor" el cementerio quedaba un laberinto para los bots. */
static int blocks_sub(const Dt1Tile *t, int sx, int sy)
{
    return sx >= 0 && sy >= 0 && sx < 5 && sy < 5 && (t->subtile_flags[(4 - sy) * 5 + sx] & 5) == 5;
}

uint64_t dt1_volume(const Dt1Tile *t)
{
    Dt1Tile *w = (Dt1Tile *)t;
    if (w->volume_done)
        return w->volume;
    w->volume_done = 1;
    /* Los subtiles que pueden tener grosor (libres, con una pared al lado):
       sale de las marcas, sin mirar la imagen. Si no hay ninguno, no se
       decodifica nada (en la Pi era casi todo el armado de Lut Gholein). */
    uint64_t cand = 0;
    for (int sy = 0; sy <= 5; sy++)
        for (int sx = 0; sx <= 5; sx++)
            if (!blocks_sub(t, sx, sy) &&
                (blocks_sub(t, sx - 1, sy) || blocks_sub(t, sx, sy - 1) || blocks_sub(t, sx - 1, sy - 1)))
                cand |= 1ull << (sy * 6 + sx);
    w->volume = 0;
    if (!cand)
        return 0;
    /* Con la imagen sin decodificar (dt1_load_lazy), una copia que se suelta
       enseguida: solo los bloques de las filas que se miran (y = -79..15
       desde el tile) y sin spans */
    Dt1Tile tmp = *t;
    const Dt1Tile *img = t;
    if (t->pending) {
        tmp.pixels = NULL;
        tmp.spans = NULL;
        if (decode_tile(t->raw, t->raw_size, &tmp, t->blocks_ptr, t->nblocks, DECODE_BAND, -79, 16) != 0)
            return 0;
        img = &tmp;
    }
    uint64_t v = 0;
    for (int sy = 0; sy <= 5 && img->pixels; sy++)
        for (int sx = 0; sx <= 5; sx++) {
            if (!(cand >> (sy * 6 + sx) & 1))
                continue;
            /* El rombo del subtile: el dibujo de las paredes arranca en la
               esquina de abajo del tile (y = 80 desde la de arriba) */
            int cx = 80 + (sx - sy) * 16, cy = (sx + sy + 1) * 8, tot = 0, hit = 0;
            for (int dy = -7; dy <= 7; dy++) {
                int r = cy + dy - 80 - img->img_y, hw = 16 - 2 * (dy < 0 ? -dy : dy);
                for (int x = cx - hw; x < cx + hw; x++) {
                    tot++;
                    hit += r >= 0 && r < img->img_h && x >= 0 && x < img->img_w && img->pixels[r * img->img_w + x];
                }
            }
            if (hit * 2 > tot)
                v |= 1ull << (sy * 6 + sx);
        }
    if (img == &tmp) {
        free(tmp.pixels);
        free(tmp.spans);
    }
    w->volume = v;
    return v;
}

void dt1_free(Dt1 *t)
{
    if (t->tiles)
        for (int i = 0; i < t->num_tiles; i++) {
            free(t->tiles[i].pixels);
            free(t->tiles[i].spans);
        }
    free(t->tiles);
    free(t->raw);
    memset(t, 0, sizeof *t);
}
