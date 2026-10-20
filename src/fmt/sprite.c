#include "formats.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

int pal_load(const uint8_t *data, size_t size, uint32_t out_argb[256])
{
    if (size < 768)
        return -1;
    for (int i = 0; i < 256; i++) {
        uint32_t b = data[i * 3], g = data[i * 3 + 1], r = data[i * 3 + 2];
        out_argb[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return 0;
}

void sprite_free(Sprite *s)
{
    if (s->frames) {
        for (int i = 0; i < s->directions * s->frames_per_dir; i++) {
            free(s->frames[i].pixels);
            free(s->frames[i].spans);
        }
        free(s->frames);
    }
    memset(s, 0, sizeof *s);
}

uint8_t *spans_build(const uint8_t *px, int w, int h)
{
    if (!px || w <= 0 || h <= 0 || w > 65535)
        return NULL;
    size_t runs = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            runs += px[y * w + x] && (x == 0 || !px[y * w + x - 1]);
    size_t head = (size_t)(h + 1) * 4;
    uint8_t *b = malloc(head + runs * 4);
    if (!b)
        return NULL;
    uint32_t *ofs = (uint32_t *)b;
    uint16_t *r = (uint16_t *)(b + head);
    size_t k = 0;
    for (int y = 0; y < h; y++) {
        ofs[y] = (uint32_t)(head + k * 4);
        const uint8_t *row = px + y * w;
        for (int x = 0; x < w;) {
            if (!row[x]) {
                x++;
                continue;
            }
            int x0 = x;
            while (x < w && row[x])
                x++;
            r[2 * k] = (uint16_t)x0;
            r[2 * k + 1] = (uint16_t)(x - x0);
            k++;
        }
    }
    ofs[h] = (uint32_t)(head + k * 4);
    return b;
}

/* DC6: cada cuadro es un RLE simple que se dibuja de abajo hacia arriba.
   0x80 = fin de línea, bit 7 prendido = salto transparente, si no = N píxeles. */
static int dc6_decode_frame(ByteReader *r, SpriteFrame *f)
{
    int32_t flip = rb_i32(r);
    int32_t w = rb_i32(r), h = rb_i32(r);
    int32_t ox = rb_i32(r), oy = rb_i32(r);
    rb_skip(r, 8); /* desconocido, puntero al siguiente bloque */
    int32_t len = rb_i32(r);
    if (r->err || w < 0 || h < 0 || w > 4096 || h > 4096 || len < 0 || !rb_has(r, (size_t)len))
        return -1;

    f->width = w;
    f->height = h;
    f->off_x = ox;
    f->off_y = oy - h; /* en DC6 el offset Y marca el borde inferior */
    f->pixels = calloc((size_t)(w * h) + 1, 1);
    if (!f->pixels)
        return -1;

    const uint8_t *d = r->p + r->pos;
    int x = 0, y = flip ? 0 : h - 1;
    for (int i = 0; i < len;) {
        uint8_t b = d[i++];
        if (b == 0x80) {
            x = 0;
            y += flip ? 1 : -1;
        } else if (b & 0x80) {
            x += b & 0x7F;
        } else {
            for (int k = 0; k < b && i < len; k++, i++, x++)
                if (x < w && y >= 0 && y < h)
                    f->pixels[y * w + x] = d[i];
        }
    }
    f->spans = spans_build(f->pixels, w, h);
    return 0;
}

/* "D2RW", direcciones y cuadros (int32); por fila (las direcciones en
   sentido horario desde el sur) su dirección en el DCC; después cada cuadro:
   ancho, alto, x e y desde el punto de anclaje (int32) y sus índices de
   paleta (0 = transparente) */
int raw_load(const uint8_t *data, size_t size, Sprite *out)
{
    memset(out, 0, sizeof *out);
    if (size < 12 || memcmp(data, "D2RW", 4) != 0)
        return -1;
    ByteReader r = rb_make(data, size);
    rb_skip(&r, 4);
    int32_t dirs = rb_i32(&r), fpd = rb_i32(&r);
    if (r.err || dirs <= 0 || fpd <= 0 || dirs > 64 || fpd > 1024)
        return -1;
    int map[64];
    for (int i = 0; i < dirs; i++) {
        map[i] = rb_i32(&r);
        if (r.err || map[i] < 0 || map[i] >= dirs)
            return -1;
    }
    out->directions = dirs;
    out->frames_per_dir = fpd;
    out->frames = calloc((size_t)dirs * (size_t)fpd, sizeof *out->frames);
    if (!out->frames)
        return -1;
    for (int row = 0; row < dirs; row++)
        for (int k = 0; k < fpd; k++) {
            int32_t w = rb_i32(&r), h = rb_i32(&r), ox = rb_i32(&r), oy = rb_i32(&r);
            SpriteFrame *f = sprite_frame(out, map[row], k);
            if (r.err || w < 0 || h < 0 || w > 4096 || h > 4096 || (f->pixels != NULL)) {
                sprite_free(out);
                return -1;
            }
            f->width = w;
            f->height = h;
            f->off_x = ox;
            f->off_y = oy;
            size_t n = (size_t)w * (size_t)h;
            if (n == 0)
                continue;
            if (!rb_has(&r, n) || !(f->pixels = malloc(n))) {
                sprite_free(out);
                return -1;
            }
            memcpy(f->pixels, r.p + r.pos, n);
            r.pos += n;
            f->spans = spans_build(f->pixels, w, h);
        }
    return 0;
}

int dc6_load(const uint8_t *data, size_t size, Sprite *out)
{
    memset(out, 0, sizeof *out);
    ByteReader r = rb_make(data, size);
    int32_t version = rb_i32(&r);
    rb_skip(&r, 12); /* flags, codificación, terminador */
    int32_t dirs = rb_i32(&r), fpd = rb_i32(&r);
    if (r.err || version != 6 || dirs <= 0 || fpd <= 0 || dirs > 64 || fpd > 1024)
        return -1;

    int n = dirs * fpd;
    out->directions = dirs;
    out->frames_per_dir = fpd;
    out->frames = calloc((size_t)n, sizeof *out->frames);
    if (!out->frames)
        return -1;

    for (int i = 0; i < n; i++) {
        rb_seek(&r, 24 + (size_t)i * 4);
        uint32_t ptr = rb_u32(&r);
        rb_seek(&r, ptr);
        if (r.err || dc6_decode_frame(&r, &out->frames[i]) != 0) {
            sprite_free(out);
            return -1;
        }
    }
    return 0;
}
