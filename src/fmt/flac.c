#include "flac.h"
#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 8

struct Flac {
    /* Fuente: un archivo que se va cargando en `buf`, o memoria */
    FILE    *file;
    uint8_t *buf;
    int      own_buf;
    size_t   cap, len, pos;
    long     buf_off;     /* posición en el archivo de buf[0] */
    long     first_frame; /* dónde empieza el audio */
    size_t   mem_first;

    int      rate, channels, bps;
    uint64_t total;
    int      max_block;
    uint8_t  md5[16];

    int32_t *ch[MAX_CHANNELS]; /* el bloque decodificado */
    int      ch_cap;
    int      block, block_pos;
    int      block_bps;
    int      broken;
};

/* ---- Lectura de bits (de a 64 por vez, el primero es el más alto) ---- */

typedef struct {
    const uint8_t *p, *end;
    uint64_t cache;
    int      bits; /* bits válidos en cache */
    int      over; /* bytes inventados después del final */
} Bits;

static inline void br_fill(Bits *b)
{
    while (b->bits <= 56) {
        uint64_t byte = 0;
        if (b->p < b->end)
            byte = *b->p++;
        else
            b->over++;
        b->cache |= byte << (56 - b->bits);
        b->bits += 8;
    }
}

static inline uint32_t br_read(Bits *b, int n) /* n de 0 a 32 */
{
    if (n == 0)
        return 0;
    if (b->bits < n)
        br_fill(b);
    uint32_t v = (uint32_t)(b->cache >> (64 - n));
    b->cache <<= n;
    b->bits -= n;
    return v;
}

static inline int32_t br_sread(Bits *b, int n)
{
    if (n == 0)
        return 0;
    uint32_t v = br_read(b, n);
    return (int32_t)(v << (32 - n)) >> (32 - n);
}

/* Cuenta los ceros hasta el próximo 1 (y lo consume) */
static inline uint32_t br_unary(Bits *b)
{
    uint32_t n = 0;
    for (;;) {
        if (b->cache) {
            int z = __builtin_clzll(b->cache);
            b->cache <<= z;
            b->cache <<= 1;
            b->bits -= z + 1;
            return n + (uint32_t)z;
        }
        n += (uint32_t)b->bits;
        b->bits = 0;
        br_fill(b);
        if (b->over > 16)
            return n; /* roto: no hay más datos */
    }
}

static inline void br_align(Bits *b)
{
    br_read(b, b->bits & 7);
}

/* Bytes consumidos desde `start` (después de alinear) */
static inline size_t br_consumed(const Bits *b, const uint8_t *start)
{
    return (size_t)(b->p - start) + (size_t)b->over - (size_t)(b->bits / 8);
}

/* ---- Subcuadros ---- */

static int read_residual(Bits *b, int32_t *s, int block, int order)
{
    int method = (int)br_read(b, 2);
    if (method > 1)
        return -1;
    int pbits = method ? 5 : 4, esc = method ? 31 : 15;
    int porder = (int)br_read(b, 4);
    int psize = block >> porder;
    if ((psize << porder) != block || psize < order)
        return -1;
    int i = order;
    for (int p = 0; p < (1 << porder); p++) {
        int k = (int)br_read(b, pbits);
        int n = p == 0 ? psize - order : psize;
        if (k == esc) {
            int nb = (int)br_read(b, 5);
            for (int j = 0; j < n; j++)
                s[i++] = br_sread(b, nb);
        } else {
            for (int j = 0; j < n; j++) {
                uint32_t q = br_unary(b);
                uint32_t v = (q << k) | br_read(b, k);
                s[i++] = (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
            }
        }
        if (b->over > 16)
            return -1;
    }
    return 0;
}

static int read_subframe(Bits *b, int32_t *s, int block, int bps)
{
    if (br_read(b, 1) != 0)
        return -1;
    int type = (int)br_read(b, 6);
    int wasted = 0;
    if (br_read(b, 1))
        wasted = (int)br_unary(b) + 1;
    if (wasted >= bps)
        return -1;
    bps -= wasted;

    if (type == 0) { /* constante */
        int32_t v = br_sread(b, bps);
        for (int i = 0; i < block; i++)
            s[i] = v;
    } else if (type == 1) { /* sin comprimir */
        for (int i = 0; i < block; i++)
            s[i] = br_sread(b, bps);
    } else if (type >= 8 && type <= 12) { /* predictor fijo */
        int order = type - 8;
        if (order > block)
            return -1;
        for (int i = 0; i < order; i++)
            s[i] = br_sread(b, bps);
        if (read_residual(b, s, block, order) != 0)
            return -1;
        switch (order) {
        case 1:
            for (int i = 1; i < block; i++) s[i] += s[i - 1];
            break;
        case 2:
            for (int i = 2; i < block; i++) s[i] += 2 * s[i - 1] - s[i - 2];
            break;
        case 3:
            for (int i = 3; i < block; i++) s[i] += 3 * (s[i - 1] - s[i - 2]) + s[i - 3];
            break;
        case 4:
            for (int i = 4; i < block; i++) s[i] += 4 * (s[i - 1] + s[i - 3]) - 6 * s[i - 2] - s[i - 4];
            break;
        }
    } else if (type >= 32) { /* LPC */
        int order = type - 31;
        if (order > block)
            return -1;
        for (int i = 0; i < order; i++)
            s[i] = br_sread(b, bps);
        int prec = (int)br_read(b, 4) + 1;
        if (prec == 16)
            return -1;
        int shift = br_sread(b, 5);
        if (shift < 0)
            return -1;
        int32_t coef[32];
        for (int i = 0; i < order; i++)
            coef[i] = br_sread(b, prec);
        if (read_residual(b, s, block, order) != 0)
            return -1;
        for (int i = order; i < block; i++) {
            int64_t sum = 0;
            for (int j = 0; j < order; j++)
                sum += (int64_t)coef[j] * s[i - 1 - j];
            s[i] += (int32_t)(sum >> shift);
        }
    } else {
        return -1;
    }
    if (wasted)
        for (int i = 0; i < block; i++)
            s[i] = (int32_t)((uint32_t)s[i] << wasted);
    return 0;
}

/* ---- Cuadros ---- */

static int ensure_channels(Flac *f, int block)
{
    if (block <= f->ch_cap)
        return 0;
    for (int c = 0; c < MAX_CHANNELS; c++) {
        int32_t *n = realloc(f->ch[c], (size_t)block * sizeof(int32_t));
        if (!n)
            return -1;
        f->ch[c] = n;
    }
    f->ch_cap = block;
    return 0;
}

/* Decodifica un cuadro que empieza en `d`. Devuelve los bytes que ocupa,
   0 si no hay un cuadro válido ahí o -1 si faltan datos. */
static long decode_frame(Flac *f, const uint8_t *d, size_t n)
{
    if (n < 6 || d[0] != 0xFF || (d[1] & 0xFE) != 0xF8)
        return 0;
    Bits b = { d, d + n, 0, 0, 0 };
    br_read(&b, 16);
    int bs_code = (int)br_read(&b, 4);
    int sr_code = (int)br_read(&b, 4);
    int ch_code = (int)br_read(&b, 4);
    int ss_code = (int)br_read(&b, 3);
    if (br_read(&b, 1) != 0 || bs_code == 0 || sr_code == 15 || ch_code > 10 || ss_code == 3)
        return 0;
    /* Número de cuadro o de muestra, codificado como UTF-8 */
    uint32_t c0 = br_read(&b, 8);
    int extra = 0;
    while (extra < 7 && (c0 & (0x80u >> extra)))
        extra++;
    if (extra == 1 || extra == 7)
        return 0;
    for (int i = 1; i < extra; i++)
        if ((br_read(&b, 8) & 0xC0) != 0x80)
            return 0;

    int block;
    if (bs_code == 1) block = 192;
    else if (bs_code <= 5) block = 576 << (bs_code - 2);
    else if (bs_code == 6) block = (int)br_read(&b, 8) + 1;
    else if (bs_code == 7) block = (int)br_read(&b, 16) + 1;
    else block = 256 << (bs_code - 8);
    if (sr_code == 12) br_read(&b, 8);
    else if (sr_code == 13 || sr_code == 14) br_read(&b, 16);
    br_read(&b, 8); /* CRC-8 */

    static const int k_bps[8] = { 0, 8, 12, 0, 16, 20, 24, 32 };
    int bps = ss_code ? k_bps[ss_code] : f->bps;
    int channels = ch_code < 8 ? ch_code + 1 : 2;
    if (channels != f->channels || bps < 4 || bps > 32 || ensure_channels(f, block) != 0)
        return 0;

    for (int c = 0; c < channels; c++) {
        int cb = bps;
        if ((ch_code == 8 && c == 1) || (ch_code == 9 && c == 0) || (ch_code == 10 && c == 1))
            cb++; /* el canal "lado" tiene un bit más */
        if (read_subframe(&b, f->ch[c], block, cb) != 0)
            return b.over ? -1 : 0;
    }
    br_align(&b);
    br_read(&b, 16); /* CRC-16 */
    size_t used = br_consumed(&b, d);
    if (used > n)
        return -1;

    int32_t *l = f->ch[0], *r = f->ch[1];
    if (ch_code == 8) {
        for (int i = 0; i < block; i++) r[i] = l[i] - r[i];
    } else if (ch_code == 9) {
        for (int i = 0; i < block; i++) l[i] += r[i];
    } else if (ch_code == 10) {
        for (int i = 0; i < block; i++) {
            int32_t side = r[i], mid = (int32_t)((uint32_t)l[i] << 1) | (side & 1);
            l[i] = (mid + side) >> 1;
            r[i] = (mid - side) >> 1;
        }
    }
    f->block = block;
    f->block_pos = 0;
    f->block_bps = bps;
    return (long)used;
}

/* ---- Fuente ---- */

static void refill(Flac *f)
{
    if (!f->file)
        return;
    if (f->pos > 0) {
        memmove(f->buf, f->buf + f->pos, f->len - f->pos);
        f->buf_off += (long)f->pos;
        f->len -= f->pos;
        f->pos = 0;
    }
    if (f->len < f->cap)
        f->len += fread(f->buf + f->len, 1, f->cap - f->len, f->file);
}

static int skip_bytes(Flac *f, size_t n)
{
    if (n <= f->len - f->pos) {
        f->pos += n;
        return 0;
    }
    if (!f->file)
        return -1;
    n -= f->len - f->pos;
    f->buf_off += (long)f->len + (long)n;
    f->len = f->pos = 0;
    if (fseek(f->file, f->buf_off, SEEK_SET) != 0)
        return -1;
    refill(f);
    return 0;
}

static int next_block(Flac *f)
{
    while (!f->broken) {
        if (f->file && f->len - f->pos < f->cap / 2)
            refill(f);
        if (f->pos >= f->len)
            return -1;
        long used = decode_frame(f, f->buf + f->pos, f->len - f->pos);
        if (used > 0) {
            f->pos += (size_t)used;
            return 0;
        }
        if (used < 0 && f->file && !feof(f->file)) {
            size_t before = f->len - f->pos;
            refill(f); /* faltaban datos: se cargan más */
            if (f->len - f->pos > before)
                continue;
        }
        /* No hay un cuadro ahí: se busca la próxima marca de sincronismo */
        f->pos++;
        while (f->pos + 1 < f->len && !(f->buf[f->pos] == 0xFF && (f->buf[f->pos + 1] & 0xFE) == 0xF8))
            f->pos++;
        if (f->pos + 1 >= f->len && (!f->file || feof(f->file)))
            return -1;
    }
    return -1;
}

static int parse_header(Flac *f)
{
    if (f->len - f->pos < 4 || memcmp(f->buf + f->pos, "fLaC", 4) != 0)
        return -1;
    f->pos += 4;
    int last = 0, have_info = 0;
    while (!last) {
        if (f->file && f->len - f->pos < 64)
            refill(f);
        if (f->len - f->pos < 4)
            return -1;
        const uint8_t *h = f->buf + f->pos;
        last = h[0] >> 7;
        int type = h[0] & 0x7F;
        size_t size = ((size_t)h[1] << 16) | ((size_t)h[2] << 8) | h[3];
        f->pos += 4;
        if (type == 0 && size >= 34 && f->len - f->pos >= 34) {
            const uint8_t *s = f->buf + f->pos;
            f->max_block = (s[2] << 8) | s[3];
            int max_frame = (s[7] << 16) | (s[8] << 8) | s[9];
            f->rate = (s[10] << 12) | (s[11] << 4) | (s[12] >> 4);
            f->channels = ((s[12] >> 1) & 7) + 1;
            f->bps = (((s[12] & 1) << 4) | (s[13] >> 4)) + 1;
            f->total = ((uint64_t)(s[13] & 15) << 32) | ((uint64_t)s[14] << 24) | ((uint64_t)s[15] << 16) |
                       ((uint64_t)s[16] << 8) | s[17];
            memcpy(f->md5, s + 18, 16);
            have_info = 1;
            /* El buffer tiene que entrar al menos dos cuadros enteros */
            if (f->file) {
                size_t need = (size_t)(max_frame ? max_frame
                                                 : (f->max_block ? f->max_block : 65536) * f->channels * 4 + 64);
                if (need * 2 > f->cap) {
                    uint8_t *n = realloc(f->buf, need * 2);
                    if (!n)
                        return -1;
                    f->buf = n;
                    f->cap = need * 2;
                }
            }
        }
        if (skip_bytes(f, size) != 0)
            return -1;
    }
    if (!have_info || f->channels > MAX_CHANNELS || f->rate == 0)
        return -1;
    f->first_frame = f->buf_off + (long)f->pos;
    f->mem_first = f->pos;
    return ensure_channels(f, f->max_block ? f->max_block : 4096);
}

Flac *flac_open(const char *path)
{
    FILE *file = fs_open(path);
    if (!file)
        return NULL;
    Flac *f = calloc(1, sizeof *f);
    if (f) {
        f->file = file;
        f->cap = 1 << 16;
        f->buf = malloc(f->cap);
        f->own_buf = 1;
    }
    if (!f || !f->buf) {
        fclose(file);
        free(f);
        return NULL;
    }
    refill(f);
    if (parse_header(f) != 0) {
        flac_close(f);
        return NULL;
    }
    return f;
}

Flac *flac_open_mem(const uint8_t *data, size_t size)
{
    Flac *f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->buf = (uint8_t *)data;
    f->cap = f->len = size;
    if (parse_header(f) != 0) {
        flac_close(f);
        return NULL;
    }
    return f;
}

void flac_close(Flac *f)
{
    if (!f)
        return;
    if (f->file)
        fclose(f->file);
    if (f->own_buf)
        free(f->buf);
    for (int c = 0; c < MAX_CHANNELS; c++)
        free(f->ch[c]);
    free(f);
}

int      flac_rate(const Flac *f) { return f->rate; }
int      flac_channels(const Flac *f) { return f->channels; }
uint64_t flac_total(const Flac *f) { return f->total; }
const uint8_t *flac_md5(const Flac *f) { return f->md5; }

int flac_read(Flac *f, int16_t *out, int frames)
{
    int done = 0, chans = f->channels;
    while (done < frames) {
        if (f->block_pos >= f->block && next_block(f) != 0)
            break;
        int n = f->block - f->block_pos;
        if (n > frames - done)
            n = frames - done;
        int shift = f->block_bps - 16;
        for (int c = 0; c < chans; c++) {
            const int32_t *s = f->ch[c] + f->block_pos;
            int16_t *o = out + (size_t)done * chans + c;
            if (shift == 0)
                for (int i = 0; i < n; i++, o += chans) *o = (int16_t)s[i];
            else if (shift > 0)
                for (int i = 0; i < n; i++, o += chans) *o = (int16_t)(s[i] >> shift);
            else
                for (int i = 0; i < n; i++, o += chans) *o = (int16_t)(s[i] << -shift);
        }
        f->block_pos += n;
        done += n;
    }
    return done;
}

int flac_rewind(Flac *f)
{
    f->block = f->block_pos = 0;
    f->broken = 0;
    if (!f->file) {
        f->pos = f->mem_first;
        return 0;
    }
    if (fseek(f->file, f->first_frame, SEEK_SET) != 0)
        return -1;
    f->buf_off = f->first_frame;
    f->len = f->pos = 0;
    refill(f);
    return 0;
}

int16_t *flac_decode(const uint8_t *data, size_t size, int *frames, int *channels, int *rate)
{
    Flac *f = flac_open_mem(data, size);
    if (!f)
        return NULL;
    size_t cap = f->total ? (size_t)f->total : 65536, n = 0;
    int ch = f->channels;
    int16_t *out = malloc(cap * ch * sizeof(int16_t));
    while (out) {
        if (n == cap) {
            cap *= 2;
            int16_t *grown = realloc(out, cap * ch * sizeof(int16_t));
            if (!grown) {
                free(out);
                out = NULL;
                break;
            }
            out = grown;
        }
        int got = flac_read(f, out + n * ch, (int)(cap - n > 65536 ? 65536 : cap - n));
        if (got <= 0)
            break;
        n += (size_t)got;
    }
    if (out) {
        *frames = (int)n;
        *channels = ch;
        *rate = f->rate;
    }
    flac_close(f);
    return out;
}
