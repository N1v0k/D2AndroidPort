#include "formats.h"

#include "bytes.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* animdata.d2: 256 bloques; cada uno tiene un contador y registros de 160
   bytes: nombre del COF (8), cuadros por dirección (4), velocidad (4) y
   eventos por cuadro (144). La velocidad está en 1/256 de cuadro por tick. */
#define RECORD_SIZE 160

int animdata_load(const uint8_t *data, size_t size, AnimData *out)
{
    memset(out, 0, sizeof *out);
    ByteReader r = rb_make(data, size);
    int cap = 0;
    for (int block = 0; block < 256 && !r.err; block++) {
        int32_t n = rb_i32(&r);
        if (r.err || n < 0 || !rb_has(&r, (size_t)n * RECORD_SIZE))
            break;
        for (int i = 0; i < n; i++) {
            if (out->count == cap) {
                cap = cap ? cap * 2 : 1024;
                AnimRecord *grown = realloc(out->records, (size_t)cap * sizeof *grown);
                if (!grown) {
                    animdata_free(out);
                    return -1;
                }
                out->records = grown;
            }
            AnimRecord *rec = &out->records[out->count++];
            for (int k = 0; k < 8; k++)
                rec->name[k] = (char)tolower(r.p[r.pos + k]);
            rec->name[7] = '\0';
            r.pos += 8;
            rec->frames_per_dir = rb_i32(&r);
            rec->speed = rb_i32(&r);
            memcpy(rec->events, r.p + r.pos, sizeof rec->events);
            r.pos += sizeof rec->events;
        }
    }
    return out->count > 0 ? 0 : -1;
}

const AnimRecord *animdata_find(const AnimData *a, const char *name)
{
    char key[8] = { 0 };
    for (int k = 0; k < 7 && name[k]; k++)
        key[k] = (char)tolower((unsigned char)name[k]);
    for (int i = 0; i < a->count; i++)
        if (strcmp(a->records[i].name, key) == 0)
            return &a->records[i];
    return NULL;
}

void animdata_free(AnimData *a)
{
    free(a->records);
    memset(a, 0, sizeof *a);
}
