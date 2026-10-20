#ifndef BYTES_H
#define BYTES_H

#include <stddef.h>
#include <stdint.h>

/* Lector de bytes little-endian con control de límites: si se pasa del
   final marca `err` y devuelve ceros, así un archivo dañado no rompe nada. */
typedef struct {
    const uint8_t *p;
    size_t         size;
    size_t         pos;
    int            err;
} ByteReader;

static inline ByteReader rb_make(const uint8_t *p, size_t size)
{
    ByteReader r = { p, size, 0, 0 };
    return r;
}

static inline int rb_has(ByteReader *r, size_t n)
{
    if (r->pos + n > r->size) {
        r->err = 1;
        return 0;
    }
    return 1;
}

static inline uint8_t rb_u8(ByteReader *r)
{
    return rb_has(r, 1) ? r->p[r->pos++] : 0;
}

static inline uint16_t rb_u16(ByteReader *r)
{
    if (!rb_has(r, 2))
        return 0;
    uint16_t v = (uint16_t)(r->p[r->pos] | (r->p[r->pos + 1] << 8));
    r->pos += 2;
    return v;
}

static inline uint32_t rb_u32(ByteReader *r)
{
    if (!rb_has(r, 4))
        return 0;
    const uint8_t *b = r->p + r->pos;
    r->pos += 4;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline int16_t rb_i16(ByteReader *r) { return (int16_t)rb_u16(r); }
static inline int32_t rb_i32(ByteReader *r) { return (int32_t)rb_u32(r); }

static inline void rb_skip(ByteReader *r, size_t n)
{
    if (rb_has(r, n))
        r->pos += n;
}

static inline void rb_seek(ByteReader *r, size_t pos)
{
    if (pos > r->size)
        r->err = 1;
    else
        r->pos = pos;
}

/* Lector de bits, del menos significativo al más significativo (como los
   usa el formato DCC). `end` es la posición en bits donde termina el flujo. */
typedef struct {
    const uint8_t *p;
    size_t         pos;
    size_t         end;
} BitReader;

static inline BitReader bits_make(const uint8_t *p, size_t size_bytes)
{
    BitReader b = { p, 0, size_bytes * 8 };
    return b;
}

static inline uint32_t bits_get(BitReader *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->end)
            return v;
        v |= (uint32_t)((b->p[b->pos >> 3] >> (b->pos & 7)) & 1) << i;
        b->pos++;
    }
    return v;
}

static inline int32_t bits_get_signed(BitReader *b, int n)
{
    if (n == 0)
        return 0;
    uint32_t v = bits_get(b, n);
    if (n < 32 && (v & (1u << (n - 1))))
        v |= ~((1u << n) - 1);
    return (int32_t)v;
}

/* Separa los próximos `n` bits como un flujo propio y avanza el original. */
static inline BitReader bits_split(BitReader *b, size_t n)
{
    BitReader s = { b->p, b->pos, b->pos + n };
    if (s.end > b->end)
        s.end = b->end;
    b->pos = s.end;
    return s;
}

#endif
