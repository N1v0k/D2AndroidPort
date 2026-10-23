#include "formats.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

/* Las versiones viejas de DS1 guardan la orientación con otra numeración */
static const uint8_t k_old_orientation[25] = {
    0x00, 0x01, 0x02, 0x01, 0x02, 0x03, 0x03, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x14,
};

enum { L_WALL, L_ORIENTATION, L_FLOOR, L_SHADOW, L_SUBST };

static void unpack_cell(Ds1Cell *c, uint32_t dw)
{
    c->prop1 = (uint8_t)(dw & 0xFF);
    c->sequence = (uint8_t)((dw >> 8) & 0x3F);
    c->style = (uint8_t)((dw >> 20) & 0x3F);
    c->hidden = (uint8_t)(dw >> 31);
    c->flags = (uint8_t)(((dw >> 16) & 1u ? DS1F_FILL_LOS : 0) | ((dw >> 17) & 1u ? DS1F_UNWALKABLE : 0) |
                         ((dw >> 28) & 1u ? DS1F_LINKAGE : 0));
}

int ds1_load(const uint8_t *data, size_t size, Ds1 *out)
{
    memset(out, 0, sizeof *out);
    ByteReader r = rb_make(data, size);

    out->version = rb_i32(&r);
    out->width = rb_i32(&r) + 1;
    out->height = rb_i32(&r) + 1;
    out->act = 1;
    if (out->version >= 8) {
        out->act = rb_i32(&r) + 1;
        if (out->act > 5)
            out->act = 5;
    }
    int has_subst = 0;
    if (out->version >= 10) {
        out->subst_type = rb_i32(&r);
        has_subst = out->subst_type == 1 || out->subst_type == 2;
    }
    if (r.err || out->width <= 0 || out->height <= 0 || out->width > 1024 || out->height > 1024)
        return -1;

    if (out->version >= 3) {
        out->num_files = rb_i32(&r);
        if (r.err || out->num_files < 0 || out->num_files > 256)
            return -1;
        out->files = calloc((size_t)(out->num_files ? out->num_files : 1), sizeof *out->files);
        for (int i = 0; i < out->num_files; i++) {
            size_t start = r.pos;
            while (!r.err && rb_u8(&r) != 0) {}
            if (r.err) {
                ds1_free(out);
                return -1;
            }
            size_t len = r.pos - start - 1;
            out->files[i] = malloc(len + 1);
            memcpy(out->files[i], data + start, len);
            out->files[i][len] = '\0';
        }
    }
    if (out->version >= 9 && out->version <= 13)
        rb_skip(&r, 8);

    out->num_walls = 1;
    out->num_floors = 1;
    if (out->version >= 4) {
        out->num_walls = rb_i32(&r);
        out->num_floors = out->version >= 16 ? rb_i32(&r) : 1;
    }
    if (r.err || out->num_walls < 0 || out->num_walls > 4 || out->num_floors < 0 || out->num_floors > 2) {
        ds1_free(out);
        return -1;
    }

    size_t cells = (size_t)(out->width * out->height);
    out->walls = calloc(cells * (size_t)(out->num_walls ? out->num_walls : 1), sizeof *out->walls);
    out->floors = calloc(cells * (size_t)(out->num_floors ? out->num_floors : 1), sizeof *out->floors);
    out->shadows = calloc(cells, sizeof *out->shadows);
    if (!out->walls || !out->floors || !out->shadows) {
        ds1_free(out);
        return -1;
    }

    /* Orden de las capas en el archivo */
    int stream[16][2], ns = 0;
    if (out->version < 4) {
        int old[5][2] = { {L_WALL, 0}, {L_FLOOR, 0}, {L_ORIENTATION, 0}, {L_SUBST, 0}, {L_SHADOW, 0} };
        memcpy(stream, old, sizeof old);
        ns = 5;
    } else {
        for (int i = 0; i < out->num_walls; i++) {
            stream[ns][0] = L_WALL;        stream[ns++][1] = i;
            stream[ns][0] = L_ORIENTATION; stream[ns++][1] = i;
        }
        for (int i = 0; i < out->num_floors; i++) {
            stream[ns][0] = L_FLOOR; stream[ns++][1] = i;
        }
        stream[ns][0] = L_SHADOW; stream[ns++][1] = 0;
        if (has_subst) {
            stream[ns][0] = L_SUBST; stream[ns++][1] = 0;
        }
    }

    for (int s = 0; s < ns; s++) {
        int kind = stream[s][0], layer = stream[s][1];
        for (int y = 0; y < out->height; y++) {
            for (int x = 0; x < out->width; x++) {
                uint32_t dw = rb_u32(&r);
                switch (kind) {
                case L_WALL:
                    unpack_cell(ds1_wall(out, layer, x, y), dw);
                    break;
                case L_ORIENTATION: {
                    uint32_t o = dw & 0xFF;
                    if (out->version < 7 && o < sizeof k_old_orientation)
                        o = k_old_orientation[o];
                    ds1_wall(out, layer, x, y)->type = (uint8_t)o;
                    break;
                }
                case L_FLOOR:
                    unpack_cell(ds1_floor(out, layer, x, y), dw);
                    ds1_floor(out, layer, x, y)->type = ORI_FLOOR;
                    break;
                case L_SHADOW:
                    unpack_cell(&out->shadows[y * out->width + x], dw);
                    out->shadows[y * out->width + x].type = ORI_SHADOW;
                    break;
                default:
                    break; /* capa de sustitución: no la usamos todavía */
                }
            }
        }
    }
    if (r.err) {
        ds1_free(out);
        return -1;
    }

    if (out->version >= 2) {
        int32_t n = rb_i32(&r);
        if (!r.err && n > 0 && n < 100000) {
            out->objects = calloc((size_t)n, sizeof *out->objects);
            for (int i = 0; i < n && !r.err; i++) {
                Ds1Object *o = &out->objects[i];
                o->type = rb_i32(&r);
                o->id = rb_i32(&r);
                o->x = rb_i32(&r);
                o->y = rb_i32(&r);
                o->flags = out->version > 5 ? rb_i32(&r) : 0;
                if (!r.err)
                    out->num_objects = i + 1;
            }
        }
    }
    /* Los grupos de sustitución (DRLGPRESET_ParseDS1File) */
    if (out->version >= 12 && has_subst && !r.err) {
        if (out->version >= 18)
            rb_skip(&r, 4);
        int32_t n = rb_i32(&r);
        if (!r.err && n > 0 && n < 4096) {
            out->groups = calloc((size_t)n, sizeof *out->groups);
            for (int i = 0; out->groups && i < n && !r.err; i++) {
                Ds1Group *g = &out->groups[i];
                g->x = rb_i32(&r);
                g->y = rb_i32(&r);
                g->w = rb_i32(&r);
                g->h = rb_i32(&r);
                g->alts = out->version >= 13 ? rb_i32(&r) : 0;
                if (!r.err)
                    out->num_groups = i + 1;
            }
        }
    }
    return 0;
}

void ds1_free(Ds1 *m)
{
    if (m->files)
        for (int i = 0; i < m->num_files; i++)
            free(m->files[i]);
    free(m->files);
    free(m->walls);
    free(m->floors);
    free(m->shadows);
    free(m->objects);
    free(m->groups);
    memset(m, 0, sizeof *m);
}
