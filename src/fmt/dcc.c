/*
 * Decodificador DCC (animaciones de personajes y monstruos).
 *
 * Cada dirección es un flujo de bits. La imagen se divide en celdas de
 * 4x4 y cada celda guarda hasta 4 colores; de un cuadro al siguiente una
 * celda puede repetirse, cambiar algunos colores o leerse de nuevo. Los
 * datos están repartidos en varios sub-flujos de bits que se leen en
 * paralelo. Implementado a partir de la documentación de Paul Siramy
 * (The Phrozen Keep).
 */
#include "formats.h"

#include "bytes.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const int k_bit_widths[16] = { 0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 26, 28, 30, 32 };
static const int k_popcount4[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

typedef struct {
    int w, h, x, y;
    int last_w, last_h, last_x, last_y;
} Cell;

typedef struct {
    uint8_t val[4];
    int     frame;
    int     frame_cell;
} PixelEntry;

typedef struct {
    int   width, height, left, top;
    int   hcells, vcells;
    Cell *cells;
} FrameInfo;

static void frame_cells(FrameInfo *f, int dir_left, int dir_top)
{
    int w0 = 4 - ((f->left - dir_left) % 4);
    if (f->width - w0 <= 1) {
        f->hcells = 1;
    } else {
        int tmp = f->width - w0 - 1;
        f->hcells = 2 + tmp / 4;
        if (tmp % 4 == 0)
            f->hcells--;
    }
    int h0 = 4 - ((f->top - dir_top) % 4);
    if (f->height - h0 <= 1) {
        f->vcells = 1;
    } else {
        int tmp = f->height - h0 - 1;
        f->vcells = 2 + tmp / 4;
        if (tmp % 4 == 0)
            f->vcells--;
    }

    f->cells = calloc((size_t)(f->hcells * f->vcells), sizeof *f->cells);
    int oy = f->top - dir_top;
    for (int y = 0; y < f->vcells; y++) {
        int ch = f->vcells == 1 ? f->height
               : y == 0 ? h0
               : y == f->vcells - 1 ? f->height - h0 - 4 * (f->vcells - 2)
               : 4;
        int ox = f->left - dir_left;
        for (int x = 0; x < f->hcells; x++) {
            int cw = f->hcells == 1 ? f->width
                   : x == 0 ? w0
                   : x == f->hcells - 1 ? f->width - w0 - 4 * (f->hcells - 2)
                   : 4;
            Cell *c = &f->cells[y * f->hcells + x];
            c->w = cw;
            c->h = ch;
            c->x = ox;
            c->y = oy;
            ox += cw;
        }
        oy += ch;
    }
}

/* Los cuadros salen del tamaño de la caja de toda la dirección: se recortan
   a lo que tienen dibujado (ahorra más de la mitad de la memoria). Un
   cuadro vacío queda de 0x0 sin píxeles. */
static void crop_frame(SpriteFrame *f)
{
    int x0 = f->width, y0 = f->height, x1 = -1, y1 = -1;
    for (int y = 0; y < f->height; y++) {
        const uint8_t *row = &f->pixels[y * f->width];
        for (int x = 0; x < f->width; x++)
            if (row[x]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                y1 = y;
            }
    }
    if (x1 < 0) {
        free(f->pixels);
        f->pixels = NULL;
        f->width = f->height = 0;
        return;
    }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w == f->width && h == f->height) {
        f->spans = spans_build(f->pixels, w, h);
        return;
    }
    uint8_t *px = malloc((size_t)(w * h));
    if (!px)
        return;
    for (int y = 0; y < h; y++)
        memcpy(&px[y * w], &f->pixels[(y0 + y) * f->width + x0], (size_t)w);
    free(f->pixels);
    f->pixels = px;
    f->off_x += x0;
    f->off_y += y0;
    f->width = w;
    f->height = h;
    f->spans = spans_build(px, w, h);
}

static int decode_direction(const uint8_t *data, size_t size, int nframes, SpriteFrame *out)
{
    BitReader b = bits_make(data, size);

    bits_get(&b, 32); /* tamaño codificado, no hace falta */
    int comp       = (int)bits_get(&b, 2);
    int var0_bits  = k_bit_widths[bits_get(&b, 4)];
    int w_bits     = k_bit_widths[bits_get(&b, 4)];
    int h_bits     = k_bit_widths[bits_get(&b, 4)];
    int x_bits     = k_bit_widths[bits_get(&b, 4)];
    int y_bits     = k_bit_widths[bits_get(&b, 4)];
    int opt_bits   = k_bit_widths[bits_get(&b, 4)];
    int coded_bits = k_bit_widths[bits_get(&b, 4)];

    FrameInfo *fr = calloc((size_t)nframes, sizeof *fr);
    if (!fr)
        return -1;

    int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
    size_t opt_total = 0;
    int bottom_up = 0;
    for (int f = 0; f < nframes; f++) {
        bits_get(&b, var0_bits);
        fr[f].width  = (int)bits_get(&b, w_bits);
        fr[f].height = (int)bits_get(&b, h_bits);
        int xo = bits_get_signed(&b, x_bits);
        int yo = bits_get_signed(&b, y_bits);
        opt_total += bits_get(&b, opt_bits);
        bits_get(&b, coded_bits);
        bottom_up |= (int)bits_get(&b, 1);

        fr[f].left = xo;
        fr[f].top  = yo - fr[f].height + 1;
        if (fr[f].left < minx) minx = fr[f].left;
        if (fr[f].top < miny) miny = fr[f].top;
        if (fr[f].left + fr[f].width > maxx) maxx = fr[f].left + fr[f].width;
        if (fr[f].top + fr[f].height > maxy) maxy = fr[f].top + fr[f].height;
    }

    int dir_w = maxx - minx, dir_h = maxy - miny;
    if (bottom_up || dir_w <= 0 || dir_h <= 0 || dir_w > 2048 || dir_h > 2048) {
        free(fr);
        return -1;
    }

    /* Datos opcionales: empiezan alineados a byte y no los usamos */
    if (opt_total) {
        b.pos = (b.pos + 7) & ~(size_t)7;
        b.pos += opt_total * 8;
    }

    size_t ec_size = (comp & 2) ? bits_get(&b, 20) : 0;
    size_t pm_size = bits_get(&b, 20);
    size_t et_size = 0, rp_size = 0;
    if (comp & 1) {
        et_size = bits_get(&b, 20);
        rp_size = bits_get(&b, 20);
    }

    uint8_t pal_map[256] = { 0 };
    for (int i = 0, n = 0; i < 256; i++)
        if (bits_get(&b, 1))
            pal_map[n++] = (uint8_t)i;

    /* Los sub-flujos están pegados a nivel de bit, uno detrás del otro */
    BitReader ec  = bits_split(&b, ec_size);
    BitReader pm  = bits_split(&b, pm_size);
    BitReader et  = bits_split(&b, et_size);
    BitReader rp  = bits_split(&b, rp_size);
    BitReader pcd = bits_split(&b, b.end - b.pos);

    /* Celdas de la dirección completa */
    int dhc = 1 + (dir_w - 1) / 4, dvc = 1 + (dir_h - 1) / 4;
    Cell *dcells = calloc((size_t)(dhc * dvc), sizeof *dcells);
    int  *cellbuf = malloc((size_t)(dhc * dvc) * sizeof *cellbuf);
    int total_cells = 0;
    for (int f = 0; f < nframes; f++) {
        frame_cells(&fr[f], minx, miny);
        total_cells += fr[f].hcells * fr[f].vcells;
    }
    PixelEntry *pb = calloc((size_t)total_cells + 1, sizeof *pb);
    uint8_t *dirpix = calloc((size_t)(dir_w * dir_h), 1);
    int ok = dcells && cellbuf && pb && dirpix;

    if (ok) {
        for (int y = 0; y < dvc; y++) {
            for (int x = 0; x < dhc; x++) {
                Cell *c = &dcells[y * dhc + x];
                c->w = x == dhc - 1 ? dir_w - 4 * (dhc - 1) : 4;
                c->h = y == dvc - 1 ? dir_h - 4 * (dvc - 1) : 4;
                c->x = 4 * x;
                c->y = 4 * y;
                c->last_w = c->last_h = -1;
            }
        }
        for (int i = 0; i < dhc * dvc; i++)
            cellbuf[i] = -1;
    }

    /* Paso 1: decodificar los colores de cada celda (el "pixel buffer") */
    int pb_last = -1;
    for (int f = 0; ok && f < nframes; f++) {
        int ocx = (fr[f].left - minx) / 4, ocy = (fr[f].top - miny) / 4;
        for (int cy = 0; cy < fr[f].vcells; cy++) {
            for (int cx = 0; cx < fr[f].hcells; cx++) {
                int cur = ocx + cx + (ocy + cy) * dhc;
                if (cur < 0 || cur >= dhc * dvc) {
                    ok = 0;
                    break;
                }
                uint32_t mask = 0xF;
                if (cellbuf[cur] >= 0) {
                    int same = ec_size > 0 ? (int)bits_get(&ec, 1) : 0;
                    if (same)
                        continue;
                    mask = bits_get(&pm, 4);
                }

                uint32_t stack[4] = { 0 }, last = 0;
                int npix = k_popcount4[mask];
                int raw = (npix && et_size > 0) ? (int)bits_get(&et, 1) : 0;
                int decoded = 0;
                for (int i = 0; i < npix; i++) {
                    if (raw) {
                        stack[i] = bits_get(&rp, 8);
                    } else {
                        uint32_t d;
                        stack[i] = last;
                        do {
                            d = bits_get(&pcd, 4);
                            stack[i] += d;
                        } while (d == 15);
                    }
                    if (stack[i] == last) {
                        stack[i] = 0;
                        break;
                    }
                    last = stack[i];
                    decoded++;
                }

                int old = cellbuf[cur];
                PixelEntry *e = &pb[++pb_last];
                int si = decoded - 1;
                for (int i = 0; i < 4; i++) {
                    if (mask & (1u << i))
                        e->val[i] = si >= 0 ? (uint8_t)stack[si--] : 0;
                    else
                        e->val[i] = pb[old].val[i];
                }
                e->frame = f;
                e->frame_cell = cx + cy * fr[f].hcells;
                cellbuf[cur] = pb_last;
            }
        }
    }
    for (int i = 0; ok && i <= pb_last; i++)
        for (int k = 0; k < 4; k++)
            pb[i].val[k] = pal_map[pb[i].val[k]];

    /* Paso 2: pintar cada cuadro celda por celda */
    int pbi = 0;
    for (int f = 0; ok && f < nframes; f++) {
        SpriteFrame *of = &out[f];
        of->width = dir_w;
        of->height = dir_h;
        of->off_x = minx;
        of->off_y = miny;
        of->pixels = calloc((size_t)(dir_w * dir_h), 1);
        if (!of->pixels) {
            ok = 0;
            break;
        }

        for (int c = 0; c < fr[f].hcells * fr[f].vcells; c++) {
            Cell *cell = &fr[f].cells[c];
            if (cell->x < 0 || cell->y < 0 || cell->x + cell->w > dir_w || cell->y + cell->h > dir_h) {
                ok = 0;
                break;
            }
            Cell *bc = &dcells[cell->x / 4 + (cell->y / 4) * dhc];
            PixelEntry *e = pbi <= pb_last ? &pb[pbi] : NULL;

            if (!e || e->frame != f || e->frame_cell != c) {
                /* Celda repetida: copiar la anterior o limpiarla si cambió de tamaño */
                if (cell->w != bc->last_w || cell->h != bc->last_h) {
                    for (int y = 0; y < cell->h; y++)
                        memset(&dirpix[(cell->y + y) * dir_w + cell->x], 0, (size_t)cell->w);
                } else {
                    for (int y = 0; y < cell->h; y++)
                        for (int x = 0; x < cell->w; x++)
                            dirpix[(cell->y + y) * dir_w + cell->x + x] =
                                dirpix[(bc->last_y + y) * dir_w + bc->last_x + x];
                    for (int y = 0; y < cell->h; y++)
                        memcpy(&of->pixels[(cell->y + y) * dir_w + cell->x],
                               &dirpix[(cell->y + y) * dir_w + cell->x], (size_t)cell->w);
                }
            } else {
                if (e->val[0] == e->val[1]) {
                    for (int y = 0; y < cell->h; y++)
                        memset(&dirpix[(cell->y + y) * dir_w + cell->x], e->val[0], (size_t)cell->w);
                } else {
                    int nb = e->val[1] != e->val[2] ? 2 : 1;
                    for (int y = 0; y < cell->h; y++)
                        for (int x = 0; x < cell->w; x++)
                            dirpix[(cell->y + y) * dir_w + cell->x + x] = e->val[bits_get(&pcd, nb)];
                }
                for (int y = 0; y < cell->h; y++)
                    memcpy(&of->pixels[(cell->y + y) * dir_w + cell->x],
                           &dirpix[(cell->y + y) * dir_w + cell->x], (size_t)cell->w);
                pbi++;
            }
            bc->last_w = cell->w;
            bc->last_h = cell->h;
            bc->last_x = cell->x;
            bc->last_y = cell->y;
        }
    }

    for (int f = 0; f < nframes; f++)
        free(fr[f].cells);
    free(fr);
    free(dcells);
    free(cellbuf);
    free(pb);
    free(dirpix);
    for (int f = 0; ok && f < nframes; f++)
        crop_frame(&out[f]);
    return ok ? 0 : -1;
}

int dcc_load(const uint8_t *data, size_t size, Sprite *out)
{
    memset(out, 0, sizeof *out);
    ByteReader r = rb_make(data, size);
    uint8_t sig = rb_u8(&r);
    rb_u8(&r); /* versión */
    int dirs = rb_u8(&r);
    int fpd = (int)rb_u32(&r);
    rb_skip(&r, 8); /* tag, tamaño final */
    if (r.err || sig != 0x74 || dirs <= 0 || fpd <= 0 || fpd > 1024)
        return -1;

    uint32_t offsets[256];
    for (int d = 0; d < dirs; d++)
        offsets[d] = rb_u32(&r);
    if (r.err)
        return -1;

    out->directions = dirs;
    out->frames_per_dir = fpd;
    out->frames = calloc((size_t)(dirs * fpd), sizeof *out->frames);
    if (!out->frames)
        return -1;

    for (int d = 0; d < dirs; d++) {
        size_t start = offsets[d];
        size_t end = d + 1 < dirs ? offsets[d + 1] : size;
        if (start >= end || end > size ||
            decode_direction(data + start, end - start, fpd, &out->frames[d * fpd]) != 0) {
            sprite_free(out);
            return -1;
        }
    }
    return 0;
}
