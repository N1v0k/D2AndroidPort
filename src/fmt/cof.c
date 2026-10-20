#include "formats.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

const char *const k_comp_names[COMP_COUNT] = {
    "hd", "tr", "lg", "ra", "la", "rh", "lh", "sh",
    "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8",
};

int cof_load(const uint8_t *data, size_t size, Cof *out)
{
    memset(out, 0, sizeof *out);
    ByteReader r = rb_make(data, size);
    out->num_layers = rb_u8(&r);
    out->frames_per_dir = rb_u8(&r);
    out->directions = rb_u8(&r);
    rb_skip(&r, 25); /* versión, caja de colisión, velocidad */
    if (r.err || out->num_layers > COMP_COUNT || out->frames_per_dir == 0 || out->directions == 0)
        return -1;

    for (int i = 0; i < out->num_layers; i++) {
        CofLayer *l = &out->layers[i];
        l->component = rb_u8(&r);
        l->shadow = rb_u8(&r);
        l->selectable = rb_u8(&r);
        l->transparent = rb_u8(&r);
        l->draw_effect = rb_u8(&r);
        for (int k = 0; k < 4; k++)
            l->weapon_class[k] = (char)rb_u8(&r);
        l->weapon_class[3] = '\0';
        if (l->component >= COMP_COUNT)
            return -1;
    }

    size_t nkeys = (size_t)out->frames_per_dir;
    size_t nprio = (size_t)out->directions * out->frames_per_dir * out->num_layers;
    if (r.err || !rb_has(&r, nkeys + nprio))
        return -1;

    out->keyframes = malloc(nkeys);
    out->priority = malloc(nprio ? nprio : 1);
    if (!out->keyframes || !out->priority) {
        cof_free(out);
        return -1;
    }
    memcpy(out->keyframes, r.p + r.pos, nkeys);
    memcpy(out->priority, r.p + r.pos + nkeys, nprio);
    return 0;
}

void cof_free(Cof *c)
{
    free(c->keyframes);
    free(c->priority);
    memset(c, 0, sizeof *c);
}
