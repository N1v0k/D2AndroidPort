/* Fuentes TrueType (las .ttf del Resurrected, data/hd/ui/fonts): lo justo para
   dibujar las letras que las fuentes del original no tienen (ruso, japonés,
   coreano, chino). Lee las tablas cmap (formatos 4 y 12), loca, glyf, hmtx,
   hhea y head; los contornos simples y compuestos, sin hinting ni kerning.
   Cada letra se rellena con la regla del original de TrueType (non-zero),
   con 4 subfilas por píxel y el ancho cubierto exacto en cada una. */
#include "formats.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned u16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static int s16(const uint8_t *p)
{
    return (int16_t)(p[0] << 8 | p[1]);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int ttf_open(Ttf *t, const uint8_t *data, size_t size)
{
    memset(t, 0, sizeof *t);
    if (size < 12)
        return -1;
    t->data = data;
    t->size = size;
    unsigned n = u16(data + 4);
    if (12 + (size_t)n * 16 > size)
        return -1;
    uint32_t cmap = 0, head = 0, hhea = 0, maxp = 0;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *e = data + 12 + i * 16;
        uint32_t off = u32(e + 8), len = u32(e + 12);
        if ((size_t)off + len > size)
            continue;
        if (!memcmp(e, "cmap", 4)) cmap = off;
        else if (!memcmp(e, "head", 4)) head = off;
        else if (!memcmp(e, "hhea", 4)) hhea = off;
        else if (!memcmp(e, "hmtx", 4)) t->hmtx = off;
        else if (!memcmp(e, "loca", 4)) t->loca = off;
        else if (!memcmp(e, "glyf", 4)) t->glyf = off;
        else if (!memcmp(e, "maxp", 4)) maxp = off;
    }
    if (!cmap || !head || !hhea || !t->hmtx || !t->loca || !t->glyf || !maxp)
        return -1;
    t->upem = (int)u16(data + head + 18);
    t->loc_long = s16(data + head + 50) != 0;
    t->ascent = s16(data + hhea + 4);
    t->descent = s16(data + hhea + 6);
    t->num_hmetrics = (int)u16(data + hhea + 34);
    t->num_glyphs = (int)u16(data + maxp + 4);
    /* La tabla de Unicode: la de todo Unicode (formato 12) si está, si no la de BMP (4) */
    unsigned nt = u16(data + cmap + 2);
    for (unsigned i = 0; i < nt; i++) {
        const uint8_t *e = data + cmap + 4 + i * 8;
        unsigned pid = u16(e), eid = u16(e + 2);
        uint32_t sub = cmap + u32(e + 4);
        if (sub + 4 > size || !(pid == 0 || (pid == 3 && (eid == 1 || eid == 10))))
            continue;
        unsigned fmt = u16(data + sub);
        if (fmt == 12 || (fmt == 4 && t->cmap_fmt != 12)) {
            t->cmap = sub;
            t->cmap_fmt = (int)fmt;
        }
    }
    return t->cmap && t->upem > 0 ? 0 : -1;
}

int ttf_glyph_index(const Ttf *t, uint32_t cp)
{
    const uint8_t *d = t->data, *s = d + t->cmap;
    if (t->cmap_fmt == 12) {
        uint32_t lo = 0, hi = u32(s + 12);
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            const uint8_t *g = s + 16 + mid * 12;
            if (cp < u32(g))
                hi = mid;
            else if (cp > u32(g + 4))
                lo = mid + 1;
            else
                return (int)(u32(g + 8) + (cp - u32(g)));
        }
        return 0;
    }
    if (t->cmap_fmt != 4 || cp > 0xFFFF)
        return 0;
    unsigned segs = u16(s + 6) / 2;
    const uint8_t *ends = s + 14, *starts = ends + segs * 2 + 2, *deltas = starts + segs * 2, *ranges = deltas + segs * 2;
    unsigned lo = 0, hi = segs;
    while (lo < hi) { /* el primer tramo que termina en cp o después */
        unsigned mid = (lo + hi) / 2;
        if (u16(ends + mid * 2) < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= segs || u16(starts + lo * 2) > cp)
        return 0;
    unsigned range = u16(ranges + lo * 2), delta = u16(deltas + lo * 2);
    if (!range)
        return (int)((cp + delta) & 0xFFFF);
    const uint8_t *g = ranges + lo * 2 + range + (cp - u16(starts + lo * 2)) * 2;
    if ((size_t)(g - d) + 2 > t->size)
        return 0;
    unsigned gi = u16(g);
    return gi ? (int)((gi + delta) & 0xFFFF) : 0;
}

int ttf_advance(const Ttf *t, int glyph)
{
    int i = glyph < t->num_hmetrics ? glyph : t->num_hmetrics - 1;
    return i >= 0 ? (int)u16(t->data + t->hmtx + (size_t)i * 4) : 0;
}

/* Dónde empieza la letra en glyf y cuánto ocupa (0 = vacía: el espacio) */
static uint32_t glyph_off(const Ttf *t, int g, uint32_t *len)
{
    if (g < 0 || g >= t->num_glyphs)
        return *len = 0;
    const uint8_t *l = t->data + t->loca;
    uint32_t a = t->loc_long ? u32(l + g * 4) : u16(l + g * 2) * 2u;
    uint32_t b = t->loc_long ? u32(l + g * 4 + 4) : u16(l + g * 2 + 2) * 2u;
    *len = b > a ? b - a : 0;
    return t->glyf + a;
}

int ttf_glyph_top(const Ttf *t, int glyph)
{
    uint32_t len, off = glyph_off(t, glyph, &len);
    return len >= 10 && off + 10 <= t->size ? s16(t->data + off + 8) : 0;
}

/* ---- Los contornos, como segmentos rectos en píxeles ---- */

typedef struct {
    float x0, y0, x1, y1;
} Edge;

typedef struct {
    Edge *e;
    int   n, cap;
    float scale; /* píxeles por unidad */
    int   steps; /* tramos por curva */
} Path;

static void add_edge(Path *p, float x0, float y0, float x1, float y1)
{
    if (y0 == y1)
        return; /* los horizontales no cruzan ninguna fila */
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 128;
        Edge *e = realloc(p->e, (size_t)cap * sizeof *e);
        if (!e)
            return;
        p->e = e;
        p->cap = cap;
    }
    p->e[p->n++] = (Edge){ x0, y0, x1, y1 };
}

/* Una curva cuadrática (en unidades de la fuente, y hacia arriba) en `steps` rectas */
static void add_quad(Path *p, float x0, float y0, float cx, float cy, float x1, float y1)
{
    float s = p->scale, px = x0, py = y0;
    for (int i = 1; i <= p->steps; i++) {
        float t = (float)i / (float)p->steps, u = 1.0f - t;
        float x = u * u * x0 + 2 * u * t * cx + t * t * x1, y = u * u * y0 + 2 * u * t * cy + t * t * y1;
        add_edge(p, px * s, -py * s, x * s, -y * s);
        px = x;
        py = y;
    }
}

/* m: la transformación de un componente (a b c d dx dy, sobre unidades) */
static void glyph_path(const Ttf *t, Path *p, int g, const float m[6], int depth)
{
    uint32_t len, off = glyph_off(t, g, &len);
    if (!len || depth > 6 || off + 10 > t->size)
        return;
    const uint8_t *d = t->data + off, *end = t->data + off + len;
    int nc = s16(d);
    if (nc < 0) { /* compuesta: varias letras, cada una con su lugar */
        const uint8_t *q = d + 10;
        unsigned flags;
        do {
            if (q + 4 > end)
                return;
            flags = u16(q);
            int sub = (int)u16(q + 2);
            q += 4;
            float dx = 0, dy = 0, a = 1, b = 0, c = 0, e = 1;
            if (flags & 1) {
                dx = (float)s16(q);
                dy = (float)s16(q + 2);
                q += 4;
            } else {
                dx = (float)(int8_t)q[0];
                dy = (float)(int8_t)q[1];
                q += 2;
            }
            if (!(flags & 2)) /* (puntos a enganchar: no hace falta) */
                dx = dy = 0;
            if (flags & 8) {
                a = e = (float)s16(q) / 16384.0f;
                q += 2;
            } else if (flags & 0x40) {
                a = (float)s16(q) / 16384.0f;
                e = (float)s16(q + 2) / 16384.0f;
                q += 4;
            } else if (flags & 0x80) {
                a = (float)s16(q) / 16384.0f;
                b = (float)s16(q + 2) / 16384.0f;
                c = (float)s16(q + 4) / 16384.0f;
                e = (float)s16(q + 6) / 16384.0f;
                q += 8;
            }
            float mm[6] = { m[0] * a + m[2] * b, m[1] * a + m[3] * b, m[0] * c + m[2] * e,
                            m[1] * c + m[3] * e, m[0] * dx + m[2] * dy + m[4], m[1] * dx + m[3] * dy + m[5] };
            glyph_path(t, p, sub, mm, depth + 1);
        } while (flags & 0x20);
        return;
    }
    /* Simple: los puntos de cada contorno (los de la curva y los de control) */
    const uint8_t *ep = d + 10;
    if (ep + nc * 2 + 2 > end)
        return;
    int npts = nc > 0 ? (int)u16(ep + (nc - 1) * 2) + 1 : 0;
    const uint8_t *q = ep + nc * 2;
    q += 2 + u16(q); /* las instrucciones */
    if (npts <= 0 || npts > 20000)
        return;
    uint8_t *fl = malloc((size_t)npts);
    float *xs = malloc((size_t)npts * 2 * sizeof *xs), *ys = xs ? xs + npts : NULL;
    if (!fl || !xs) {
        free(fl);
        free(xs);
        return;
    }
    for (int i = 0; i < npts && q < end;) {
        uint8_t f = *q++;
        int rep = 1;
        if (f & 8 && q < end)
            rep += *q++;
        while (rep-- > 0 && i < npts)
            fl[i++] = f;
    }
    int v = 0;
    for (int i = 0; i < npts; i++) { /* las x */
        uint8_t f = fl[i];
        if (f & 2) {
            int dlt = q < end ? *q++ : 0;
            v += f & 16 ? dlt : -dlt;
        } else if (!(f & 16)) {
            v += q + 1 < end ? s16(q) : 0;
            q += 2;
        }
        xs[i] = (float)v;
    }
    v = 0;
    for (int i = 0; i < npts; i++) { /* las y */
        uint8_t f = fl[i];
        if (f & 4) {
            int dlt = q < end ? *q++ : 0;
            v += f & 32 ? dlt : -dlt;
        } else if (!(f & 32)) {
            v += q + 1 < end ? s16(q) : 0;
            q += 2;
        }
        ys[i] = (float)v;
    }
    for (int i = 0; i < npts; i++) { /* con la transformación del componente */
        float x = xs[i], y = ys[i];
        xs[i] = m[0] * x + m[2] * y + m[4];
        ys[i] = m[1] * x + m[3] * y + m[5];
    }
    int start = 0;
    for (int c = 0; c < nc; c++) {
        int last = (int)u16(ep + c * 2);
        if (last < start || last >= npts)
            break;
        int count = last - start + 1;
        /* Arranca en un punto de la curva (o en el medio de dos de control) */
        float sx, sy;
        int first;
        if (fl[start] & 1) {
            sx = xs[start];
            sy = ys[start];
            first = 1;
        } else if (fl[last] & 1) {
            sx = xs[last];
            sy = ys[last];
            first = 0;
        } else {
            sx = (xs[start] + xs[last]) / 2;
            sy = (ys[start] + ys[last]) / 2;
            first = 0;
        }
        float px = sx, py = sy, cx = 0, cy = 0;
        int have_ctrl = 0;
        for (int k = first; k <= count; k++) {
            int i = start + (k % count);
            float x = k == count ? sx : xs[i], y = k == count ? sy : ys[i];
            int on = k == count ? 1 : fl[i] & 1;
            if (k == count && have_ctrl) {
                add_quad(p, px, py, cx, cy, sx, sy);
                have_ctrl = 0;
                px = sx; /* (cerrado: sin otra recta de vuelta al principio) */
                py = sy;
                break;
            }
            if (on) {
                if (have_ctrl)
                    add_quad(p, px, py, cx, cy, x, y);
                else
                    add_edge(p, px * p->scale, -py * p->scale, x * p->scale, -y * p->scale);
                px = x;
                py = y;
                have_ctrl = 0;
            } else if (have_ctrl) { /* dos de control seguidos: entre ellos hay uno de la curva */
                float mx = (cx + x) / 2, my = (cy + y) / 2;
                add_quad(p, px, py, cx, cy, mx, my);
                px = mx;
                py = my;
                cx = x;
                cy = y;
            } else {
                cx = x;
                cy = y;
                have_ctrl = 1;
            }
        }
        if (have_ctrl)
            add_quad(p, px, py, cx, cy, sx, sy);
        else if (px != sx || py != sy)
            add_edge(p, px * p->scale, -py * p->scale, sx * p->scale, -sy * p->scale);
        start = last + 1;
    }
    free(fl);
    free(xs);
}

/* ---- El relleno ---- */

#define SUBROWS 4

static int cmp_float(const void *a, const void *b)
{
    float d = ((const float *)a)[0] - ((const float *)b)[0];
    return (d > 0) - (d < 0);
}

uint8_t *ttf_render(const Ttf *t, int glyph, float px_size, int *w, int *h, int *x0, int *y0)
{
    *w = *h = *x0 = *y0 = 0;
    Path p = { NULL, 0, 0, px_size / (float)t->upem, px_size < 20 ? 3 : 5 };
    static const float ident[6] = { 1, 0, 0, 1, 0, 0 };
    glyph_path(t, &p, glyph, ident, 0);
    if (p.n == 0) {
        free(p.e);
        return NULL;
    }
    float minx = p.e[0].x0, maxx = minx, miny = p.e[0].y0, maxy = miny;
    for (int i = 0; i < p.n; i++) {
        const Edge *e = &p.e[i];
        minx = fminf(minx, fminf(e->x0, e->x1));
        maxx = fmaxf(maxx, fmaxf(e->x0, e->x1));
        miny = fminf(miny, fminf(e->y0, e->y1));
        maxy = fmaxf(maxy, fmaxf(e->y0, e->y1));
    }
    int ix0 = (int)floorf(minx), iy0 = (int)floorf(miny), ix1 = (int)ceilf(maxx), iy1 = (int)ceilf(maxy);
    int bw = ix1 - ix0, bh = iy1 - iy0;
    if (bw <= 0 || bh <= 0 || bw > 512 || bh > 512) {
        free(p.e);
        return NULL;
    }
    float *acc = calloc((size_t)bw * (size_t)bh, sizeof *acc);
    float(*hits)[2] = malloc((size_t)p.n * sizeof *hits); /* x y sentido de cada cruce */
    uint8_t *out = calloc((size_t)bw * (size_t)bh, 1);
    if (!acc || !hits || !out) {
        free(acc);
        free(hits);
        free(out);
        free(p.e);
        return NULL;
    }
    for (int row = 0; row < bh; row++) {
        float *line = acc + (size_t)row * bw;
        for (int sr = 0; sr < SUBROWS; sr++) {
            float y = (float)(iy0 + row) + ((float)sr + 0.5f) / SUBROWS;
            int nh = 0;
            for (int i = 0; i < p.n; i++) {
                const Edge *e = &p.e[i];
                float ya = e->y0, yb = e->y1;
                int dir = 1;
                if (ya > yb) {
                    float tmp = ya;
                    ya = yb;
                    yb = tmp;
                    dir = -1;
                }
                if (y < ya || y >= yb)
                    continue;
                float x = e->x0 + (y - e->y0) * (e->x1 - e->x0) / (e->y1 - e->y0);
                hits[nh][0] = x - (float)ix0;
                hits[nh++][1] = (float)dir;
            }
            qsort(hits, (size_t)nh, sizeof *hits, cmp_float);
            int wind = 0;
            for (int k = 0; k + 1 < nh; k++) {
                wind += (int)hits[k][1];
                if (!wind)
                    continue;
                float xa = hits[k][0], xb = hits[k + 1][0]; /* lo de adentro: lo que cubre de cada píxel */
                if (xb <= xa)
                    continue;
                int pa = (int)xa, pb = (int)xb;
                pa = pa < 0 ? 0 : pa;
                for (int px = pa; px <= pb && px < bw; px++) {
                    float l = fmaxf(xa, (float)px), r = fminf(xb, (float)(px + 1));
                    if (r > l)
                        line[px] += (r - l) / SUBROWS;
                }
            }
        }
        for (int x = 0; x < bw; x++) {
            float c = line[x];
            out[(size_t)row * bw + x] = (uint8_t)(c >= 1.0f ? 255 : c <= 0 ? 0 : c * 255.0f);
        }
    }
    free(acc);
    free(hits);
    free(p.e);
    *w = bw;
    *h = bh;
    *x0 = ix0;
    *y0 = iy0;
    return out;
}
