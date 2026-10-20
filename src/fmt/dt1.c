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
static int decode_tile(const uint8_t *data, size_t size, Dt1Tile *t, int32_t blocks_ptr, int32_t nblocks, int pixels)
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
        if (b->format == 1)
            decode_iso_block(t, b, data + start);
        else
            decode_rle_block(t, b, data + start);
    }
    free(blocks);
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
        if (r.err || decode_tile(data, size, t, blocks_ptr, nblocks, !lazy) != 0) {
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
        if (decode_tile(w->raw, w->raw_size, w, w->blocks_ptr, w->nblocks, 1) != 0)
            w->pixels = NULL; /* (sin imagen: no se dibuja) */
    }
    return t;
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
