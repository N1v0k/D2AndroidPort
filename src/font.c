#include "font.h"

#include "fmt/formats.h"
#include "fmt/fs.h"
#include "lang.h"
#include "video.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char    ch;
    uint8_t rows[7]; /* 5 bits por fila, el bit 4 es la columna izquierda */
} Glyph;

static const Glyph k_glyphs[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
};

static const Glyph *find_glyph(char c)
{
    c = (char)toupper((unsigned char)c);
    for (unsigned i = 0; i < sizeof k_glyphs / sizeof k_glyphs[0]; i++)
        if (k_glyphs[i].ch == c)
            return &k_glyphs[i];
    return NULL;
}

/* Lee un carácter UTF-8 y lo devuelve sin acento (la fuente provisoria
   solo tiene letras básicas): "Páramo" -> P, A, R, A, M, O */
static char next_char(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (p[0] < 0x80) {
        *s += 1;
        return (char)p[0];
    }
    if (p[0] == 0xC3 && p[1]) {
        static const char k_latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        *s += 2;
        unsigned idx = p[1] - 0x80u;
        return idx < sizeof k_latin1 - 1 ? k_latin1[idx] : '?';
    }
    /* Otros caracteres: saltar la secuencia completa */
    int len = p[0] >= 0xF0 ? 4 : p[0] >= 0xE0 ? 3 : 2;
    for (int i = 0; i < len && **s; i++)
        (*s)++;
    return p[0] == 0xC2 ? (p[1] == 0xA1 ? '!' : p[1] == 0xBF ? '?' : ' ') : ' ';
}

/* ---- Fuentes del juego (data/local/font/latin, o latin2 en polaco) ---- */

typedef struct {
    int      ok;
    Sprite   sheet;      /* un cuadro por letra, todos del mismo tamaño */
    uint8_t  width[256]; /* cuánto avanza cada letra (de la tabla .tbl) */
    uint16_t frame[256];
    uint8_t  has[256];   /* la tabla trae esa letra */
    int      next;       /* las de código 256 o más (las del polaco): ordenadas por código */
    uint16_t xcode[256], xframe[256];
    uint8_t  xwidth[256];
    int      height;
    int      outlined;   /* las letras ya traen su relleno oscuro y su borde (Exocet): sin sombra */
    int      base, cap;  /* la línea base y el alto de la "A" (para ubicar las letras de la .ttf) */
} D2Font;

enum { F8, F16, F24, F30, F42, FEXOCET10, FEXOCET8, FFORMAL12, FFORMAL10, F6, F_COUNT };
static const char *const k_font_files[F_COUNT] = { "font8",        "font16",       "font24",       "font30",
                                                   "font42",       "fontexocet10", "fontexocet8",  "fontformal12",
                                                   "fontformal10", "font6" };
static D2Font s_fonts[F_COUNT];
static D2Font s_extra[F_COUNT]; /* las del idioma (latin2 en polaco), para las letras que las de siempre no tienen */
static int    s_have_d2;

/* La tabla .tbl: "Woo!" + 8 bytes, y las letras de 14 bytes con el código
   (Latin-1, o Unicode en la de latin2), el ancho en el byte 3 y el cuadro
   del DC6 en los bytes 8-9 */
static int load_tbl(D2Font *f, const uint8_t *d, size_t n)
{
    if (n < 12 + 14 || memcmp(d, "Woo!", 4) != 0)
        return -1;
    int count = (int)((n - 12) / 14);
    f->next = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t *e = d + 12 + i * 14;
        int code = e[0] | (e[1] << 8);
        if (code < 256) {
            f->width[code] = e[3];
            f->frame[code] = (uint16_t)(e[8] | (e[9] << 8));
            f->has[code] = code < 0x80 || code >= 0xA0; /* (0x80..0x9F no son letras en Unicode) */
        } else if (f->next < 256) {
            int k = f->next++;
            while (k > 0 && f->xcode[k - 1] > code) { /* en orden, para buscarlas */
                f->xcode[k] = f->xcode[k - 1];
                f->xframe[k] = f->xframe[k - 1];
                f->xwidth[k] = f->xwidth[k - 1];
                k--;
            }
            f->xcode[k] = (uint16_t)code;
            f->xframe[k] = (uint16_t)(e[8] | (e[9] << 8));
            f->xwidth[k] = e[3];
        }
    }
    return 0;
}

/* El cuadro y el ancho de una letra; 0 = la fuente no la tiene */
static int d2_glyph(const D2Font *f, uint32_t cp, int *frame, int *width)
{
    if (cp < 256) {
        if (!f->has[cp])
            return 0;
        *frame = f->frame[cp];
        *width = f->width[cp];
        return *frame < f->sheet.frames_per_dir;
    }
    int lo = 0, hi = f->next;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (f->xcode[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= f->next || f->xcode[lo] != cp)
        return 0;
    *frame = f->xframe[lo];
    *width = f->xwidth[lo];
    return *frame < f->sheet.frames_per_dir;
}

/* Dónde apoya la "A" y cuánto mide: para poner las otras letras al mismo alto */
static void measure_cap(D2Font *f)
{
    int fr, w;
    f->base = f->height * 3 / 4;
    f->cap = f->height / 2;
    if (!d2_glyph(f, 'A', &fr, &w))
        return;
    const SpriteFrame *g = &f->sheet.frames[fr];
    int top = -1, bottom = -1;
    for (int y = 0; y < g->height; y++)
        for (int x = 0; x < g->width; x++)
            if (g->pixels[y * g->width + x]) {
                if (top < 0)
                    top = y;
                bottom = y;
                break;
            }
    if (top >= 0) {
        f->base = bottom + 1;
        f->cap = bottom + 1 - top;
    }
}

static int load_font(D2Font *f, const char *dir, int i)
{
    char path[128];
    size_t n;
    snprintf(path, sizeof path, "data/local/font/%s/%s.dc6", dir, k_font_files[i]);
    uint8_t *d = fs_read(path, &n);
    int ok = d && dc6_load(d, n, &f->sheet) == 0;
    free(d);
    snprintf(path, sizeof path, "data/local/font/%s/%s.tbl", dir, k_font_files[i]);
    d = ok ? fs_read(path, &n) : NULL;
    ok = ok && d && load_tbl(f, d, n) == 0;
    free(d);
    f->ok = ok;
    f->height = ok ? f->sheet.frames[0].height : 0;
    f->outlined = i == FEXOCET10 || i == FEXOCET8;
    if (ok)
        measure_cap(f);
    return ok;
}

/* Las de siempre (latin) y, en polaco, las de latin2 para sus letras (en
   esas, lo demás no es igual: los signos < y > son comillas) */
int font_init_d2(void)
{
    int extra = strcmp(lang_font_dir(), "latin") != 0;
    for (int i = 0; i < F_COUNT; i++) {
        load_font(&s_fonts[i], "latin", i);
        if (extra)
            load_font(&s_extra[i], lang_font_dir(), i);
    }
    s_have_d2 = s_fonts[F16].ok;
    return s_have_d2 ? 0 : -1;
}

static int font_index(int scale)
{
    int i = scale <= 1 ? F8 : scale == 2 ? F16 : scale == 3 ? F24 : scale <= 5 ? F30 : scale <= 9 ? F42
          : scale == FONT_EXOCET || scale == FONT_EXOCET_ENGRAVED ? FEXOCET10 : scale == FONT_EXOCET_SMALL ? FEXOCET8
          : scale == FONT_FORMAL ? FFORMAL12 : scale == FONT_FORMAL_SMALL ? FFORMAL10 : F6;
    return s_fonts[i].ok ? i : F16;
}

/* UTF-8 -> código Unicode (lo que no se entiende, '?') */
static uint32_t next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (p[0] < 0x80) {
        *s += 1;
        return p[0];
    }
    int len = p[0] >= 0xF0 ? 4 : p[0] >= 0xE0 ? 3 : p[0] >= 0xC0 ? 2 : 1;
    uint32_t cp = len == 4 ? p[0] & 0x07u : len == 3 ? p[0] & 0x0Fu : p[0] & 0x1Fu;
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += i;
            return '?';
        }
        cp = cp << 6 | (p[i] & 0x3Fu);
    }
    *s += len;
    return len == 1 ? '?' : cp == 0xA0 ? ' ' : cp; /* (el espacio que no se corta, un espacio) */
}

/* Lo que se parece, para cuando no hay con qué dibujar la letra */
static uint32_t plain_cp(uint32_t cp)
{
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2026: return '.';
    case 0x0152: return 'O';
    case 0x0153: return 'o';
    default: return '?';
    }
}

/* ---- Las letras de la .ttf del Resurrected (las que las fuentes del original no tienen) ---- */

typedef struct {
    uint32_t    key;    /* (código << 4 | fuente) + 1; 0 = libre */
    int8_t      x0, y0; /* desde la línea base */
    uint8_t     adv;
    int8_t      none;   /* la .ttf no la tiene */
    SpriteFrame f;      /* pixels NULL: no tiene dibujo (el espacio) */
} TGlyph;

#define TCACHE 4096 /* letras guardadas (con más, se empieza de nuevo) */
static struct {
    int      state;       /* 0 sin cargar, 1 lista, -1 no está */
    uint8_t *data;
    Ttf      ttf;
    float    px[F_COUNT]; /* píxeles por cuadratín para cada fuente del original */
    TGlyph  *cache;
    int      used;
} s_tt;

static int tt_ready(void)
{
    if (s_tt.state)
        return s_tt.state > 0;
    size_t n;
    s_tt.state = -1;
    s_tt.data = fs_read(lang_ttf(), &n);
    if (!s_tt.data || ttf_open(&s_tt.ttf, s_tt.data, n) != 0) {
        free(s_tt.data);
        s_tt.data = NULL;
        return 0;
    }
    s_tt.cache = calloc(TCACHE, sizeof *s_tt.cache);
    if (!s_tt.cache)
        return 0;
    /* El tamaño: que su "A" mida lo que la de la fuente del original */
    int a = ttf_glyph_index(&s_tt.ttf, 'A'), top = a ? ttf_glyph_top(&s_tt.ttf, a) : 0;
    for (int i = 0; i < F_COUNT; i++) {
        int cap = s_fonts[i].ok ? s_fonts[i].cap : 10;
        s_tt.px[i] = top > 0 ? (float)cap * (float)s_tt.ttf.upem / (float)top : (float)cap * 1.4f;
    }
    s_tt.state = 1;
    return 1;
}

static void tt_flush(void)
{
    for (int i = 0; i < TCACHE; i++) {
        free(s_tt.cache[i].f.pixels);
        free(s_tt.cache[i].f.spans);
    }
    memset(s_tt.cache, 0, TCACHE * sizeof *s_tt.cache);
    s_tt.used = 0;
}

/* La letra cp de la .ttf al tamaño de la fuente fi; NULL = no la tiene */
static const TGlyph *tt_glyph(uint32_t cp, int fi)
{
    if (!tt_ready() || cp > 0xFFFFFFu)
        return NULL;
    uint32_t key = (cp << 4 | (uint32_t)fi) + 1u;
    unsigned h = (key * 2654435761u) & (TCACHE - 1);
    for (int n = 0; n < TCACHE && s_tt.cache[h].key; n++, h = (h + 1) & (TCACHE - 1))
        if (s_tt.cache[h].key == key)
            return s_tt.cache[h].none ? NULL : &s_tt.cache[h];
    if (s_tt.used >= TCACHE * 3 / 4) {
        tt_flush();
        h = (key * 2654435761u) & (TCACHE - 1);
    }
    TGlyph *g = &s_tt.cache[h];
    memset(g, 0, sizeof *g);
    g->key = key;
    s_tt.used++;
    int gi = ttf_glyph_index(&s_tt.ttf, cp);
    if (!gi) {
        g->none = 1; /* (así no se busca otra vez) */
        return NULL;
    }
    /* Las de China, Japón y Corea, más grandes que las demás: si no, en las fuentes chicas no se leen */
    float px = s_tt.px[fi];
    if (cp >= 0x2E80 && px < 12.0f)
        px = 12.0f;
    float adv = (float)ttf_advance(&s_tt.ttf, gi) * px / (float)s_tt.ttf.upem;
    g->adv = (uint8_t)(adv + 0.5f > 255 ? 255 : adv + 0.5f);
    int w, h2, x0, y0;
    uint8_t *cov = ttf_render(&s_tt.ttf, gi, px, &w, &h2, &x0, &y0);
    if (!cov)
        return g;
    /* Cuánto cubre -> un gris de la paleta (como los de las fuentes del original: se tiñen igual) */
    for (int i = 0; i < w * h2; i++)
        cov[i] = cov[i] < 72 ? 0 : COL(R_GRAY, 5 + cov[i] * 10 / 255);
    g->f.width = w;
    g->f.height = h2;
    g->f.pixels = cov;
    g->f.spans = spans_build(cov, w, h2);
    g->x0 = (int8_t)(x0 < -128 ? -128 : x0 > 127 ? 127 : x0);
    g->y0 = (int8_t)(y0 < -128 ? -128 : y0 > 127 ? 127 : y0);
    return g;
}

/* Una letra: de la fuente del original si la tiene, si no de la .ttf, si no lo que se parece */
typedef struct {
    const SpriteFrame *frame;
    int                dx, dy, adv;
    int                d2; /* de la fuente del original (las Exocet sin sombra) */
} Glyph2;

static void glyph_for(const D2Font *f, int fi, uint32_t cp, Glyph2 *out)
{
    int fr, w;
    memset(out, 0, sizeof *out);
    if (d2_glyph(f, cp, &fr, &w)) {
        out->frame = &f->sheet.frames[fr];
        out->adv = w;
        out->d2 = 1;
        return;
    }
    const D2Font *x = &s_extra[fi];
    if (cp >= 0x80 && x->ok && d2_glyph(x, cp, &fr, &w)) { /* (la letra del polaco, a la altura de las otras) */
        out->frame = &x->sheet.frames[fr];
        out->dy = f->base - x->base;
        out->adv = w;
        out->d2 = 1;
        return;
    }
    const TGlyph *g = tt_glyph(cp, fi);
    if (g) {
        out->frame = g->f.pixels ? &g->f : NULL;
        out->dx = g->x0;
        out->dy = f->base + g->y0;
        out->adv = g->adv;
        return;
    }
    uint32_t alt = plain_cp(cp);
    if (d2_glyph(f, alt, &fr, &w)) {
        out->frame = &f->sheet.frames[fr];
        out->adv = w;
        out->d2 = 1;
    }
}

/* El color de texto del original más parecido a un color de la paleta */
static int text_color_for(uint8_t col)
{
    uint32_t c = g_fb.pal[col];
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int best = 0, best_d = 1 << 30;
    for (int i = 0; i < TEXT_COLORS; i++) {
        if (i == 6)
            continue; /* negro: solo para sombras */
        int dr = r - (int)((g_text_rgb[i] >> 16) & 0xFF);
        int dg = g - (int)((g_text_rgb[i] >> 8) & 0xFF);
        int db = b - (int)(g_text_rgb[i] & 0xFF);
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static int d2_width(int fi, const char *s)
{
    const D2Font *f = &s_fonts[fi];
    int w = 0;
    Glyph2 g;
    while (*s) {
        glyph_for(f, fi, next_cp(&s), &g);
        w += g.adv;
    }
    return w;
}

static void d2_draw(int fi, int x, int y, const char *s, uint8_t color, int engraved)
{
    const D2Font *f = &s_fonts[fi];
    /* El blanco (color 0) no tiene tabla: la fuente va con sus colores propios */
    int tc = text_color_for(color);
    const uint8_t *lut = tc == 0 ? NULL : g_text_shift[tc], *plain = lut;
    /* Exocet rellena: lo oscuro de adentro de la letra va del color del texto
       y el borde con su tono (si no, sobre fondo oscuro se ve solo el contorno) */
    /* (la tabla se arma una vez por color y paleta: antes, en cada texto) */
    static uint8_t fill[256];
    static int fill_color = -1, fill_tc = -1;
    static uint32_t fill_pal;
    if (f->outlined && !engraved) {
        uint32_t pal_key = g_fb.pal[1] ^ (g_fb.pal[128] << 1) ^ (g_fb.pal[255] << 2);
        if (fill_color != color || fill_tc != tc || fill_pal != pal_key) {
            for (int i = 0; i < 256; i++) {
                uint32_t c = g_fb.pal[i];
                int lum = (int)(((c >> 16) & 0xFF) * 3 + ((c >> 8) & 0xFF) * 6 + (c & 0xFF)) / 10;
                fill[i] = lum < 64 ? color : lut ? lut[i] : (uint8_t)i;
            }
            fill_color = color;
            fill_tc = tc;
            fill_pal = pal_key;
        }
        lut = fill;
    }
    Glyph2 g;
    while (*s) {
        glyph_for(f, fi, next_cp(&s), &g);
        const SpriteFrame *fr = g.frame;
        if (fr) {
            int gx = x + g.dx, gy = y + g.dy;
            /* (las de la .ttf, con sombra y sin el relleno de la Exocet) */
            const uint8_t *use = g.d2 ? lut : plain;
            if (!g.d2 || !f->outlined || !engraved) /* sombra */
                fb_blit_mask_spans(fr->pixels, fr->spans, fr->width, fr->height, gx + 1, gy + 1, COL(R_GRAY, 0));
            if (use)
                fb_blit_remap_spans(fr->pixels, fr->spans, fr->width, fr->height, gx, gy, use);
            else
                fb_blit_spans(fr->pixels, fr->spans, fr->width, fr->height, gx, gy);
        }
        x += g.adv;
    }
}

/* ---- Fuente incorporada de 5x7 ---- */

static int builtin_width(const char *s, int scale)
{
    int n = 0;
    while (*s) {
        next_char(&s);
        n++;
    }
    return n > 0 ? (n * 6 - 1) * scale : 0;
}

static void draw_raw(int x, int y, const char *s, int scale, uint8_t color)
{
    while (*s) {
        const Glyph *g = find_glyph(next_char(&s));
        int gx = x;
        x += 6 * scale;
        if (!g)
            continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g->rows[row] & (0x10 >> col))
                    fb_fill_rect(gx + col * scale, y + row * scale, scale, scale, color);
    }
}

/* ---- Interfaz común ---- */

int font_width(const char *s, int scale)
{
    s = lang_screen(s);
    return s_have_d2 ? d2_width(font_index(scale), s) : builtin_width(s, scale);
}

int font_height(int scale)
{
    return s_have_d2 ? s_fonts[font_index(scale)].height : 7 * scale;
}

void font_draw(int x, int y, const char *s, int scale, uint8_t color)
{
    s = lang_screen(s);
    if (s_have_d2) {
        d2_draw(font_index(scale), x, y, s, color, scale == FONT_EXOCET_ENGRAVED);
        return;
    }
    int sh = scale > 1 ? scale / 2 : 1;
    draw_raw(x + sh, y + sh, s, scale, COL(R_GRAY, 0)); /* sombra */
    draw_raw(x, y, s, scale, color);
}

void font_draw_centered(int cx, int y, const char *s, int scale, uint8_t color)
{
    font_draw(cx - font_width(s, scale) / 2, y, s, scale, color);
}

/* El tamaño más chico que sigue en la misma familia de letra (-1 = no hay): los de tamaño de 42 a 8 y
   después la más chica; la Exocet y la Formal, su versión chica y después la de 8 */
static int smaller(int scale)
{
    switch (scale) {
    case FONT_EXOCET:
    case FONT_EXOCET_ENGRAVED: return FONT_EXOCET_SMALL;
    case FONT_EXOCET_SMALL: return 1;
    case FONT_FORMAL: return FONT_FORMAL_SMALL;
    case FONT_FORMAL_SMALL: return 1;
    case FONT_TINY: return -1;
    default: break;
    }
    if (scale >= 6)
        return 5;
    if (scale >= 4)
        return 3;
    return scale >= 2 ? scale - 1 : FONT_TINY;
}

int font_fit(const char *s, int scale, int max_w)
{
    int sc = scale;
    while (font_width(s, sc) > max_w && smaller(sc) >= 0)
        sc = smaller(sc);
    if (font_width(s, sc) > max_w && getenv("D2_TEXT_CHECK")) /* (pruebas: los que no entran ni así) */
        printf("texto que no entra: \"%s\" (%d px en %d)\n", lang_screen(s), font_width(s, sc), max_w);
    return sc;
}

const char *font_shorten(const char *s, int scale, int max_w, char *buf, size_t cap)
{
    s = lang_screen(s);
    if (font_width(s, scale) <= max_w || cap < 8)
        return s;
    size_t n = 0;
    const char *p = s;
    while (*p) { /* de a una letra entera (UTF-8) mientras entre con el punto */
        const char *q = p;
        next_cp(&q);
        size_t len = (size_t)(q - s);
        if (len + 2 > cap)
            break;
        char try_[256];
        snprintf(try_, sizeof try_, "%.*s.", (int)len, s);
        if (font_width(try_, scale) > max_w)
            break;
        n = len;
        p = q;
    }
    while (n > 0 && s[n - 1] == ' ') /* (sin el espacio antes del punto) */
        n--;
    snprintf(buf, cap, "%.*s.", (int)n, s);
    return buf;
}

/* Se puede cortar el renglón antes de una letra de China, Japón o Corea
   (esos textos no llevan espacios) */
static int cjk(uint32_t cp)
{
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

int font_break(const char *s, int scale, int max_w)
{
    const char *p = s, *t = s, *best = NULL;
    int w = 0;
    while (*p && *p != '\n') {
        const char *at = p, *q = p;
        uint32_t cp = next_cp(&q);
        char one[8];
        snprintf(one, sizeof one, "%.*s", (int)(q - at), at);
        int cw = font_width(one, scale);
        if (at > t && w + cw > max_w) /* no entra: hasta el último corte (o hasta acá) */
            return (int)((best ? best : at) - t);
        if (cp == ' ' || (cjk(cp) && at > t))
            best = at;
        w += cw;
        p = q;
    }
    return (int)(p - t);
}
