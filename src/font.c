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

/* ---- Fuentes del juego (data/local/font/latin) ---- */

typedef struct {
    int      ok;
    Sprite   sheet;      /* un cuadro por letra, todos del mismo tamaño */
    uint8_t  width[256]; /* cuánto avanza cada letra (de la tabla .tbl) */
    uint16_t frame[256];
    int      height;
    int      outlined; /* las letras ya traen su relleno oscuro y su borde (Exocet): sin sombra */
} D2Font;

enum { F8, F16, F24, F30, F42, FEXOCET10, FEXOCET8, FFORMAL12, FFORMAL10, F6, F_COUNT };
static const char *const k_font_files[F_COUNT] = { "font8",        "font16",       "font24",       "font30",
                                                   "font42",       "fontexocet10", "fontexocet8",  "fontformal12",
                                                   "fontformal10", "font6" };
static D2Font s_fonts[F_COUNT];
static int    s_have_d2;

/* La tabla .tbl: "Woo!" + 8 bytes, y 256 letras de 14 bytes con el código
   (Latin-1), el ancho en el byte 3 y el cuadro del DC6 en los bytes 8-9 */
static int load_tbl(D2Font *f, const uint8_t *d, size_t n)
{
    if (n < 12 + 256 * 14 || memcmp(d, "Woo!", 4) != 0)
        return -1;
    for (int i = 0; i < 256; i++) {
        const uint8_t *e = d + 12 + i * 14;
        int code = e[0] | (e[1] << 8);
        if (code < 256) {
            f->width[code] = e[3];
            f->frame[code] = (uint16_t)(e[8] | (e[9] << 8));
        }
    }
    return 0;
}

int font_init_d2(void)
{
    for (int i = 0; i < F_COUNT; i++) {
        D2Font *f = &s_fonts[i];
        char path[128];
        size_t n;
        snprintf(path, sizeof path, "data/local/font/latin/%s.dc6", k_font_files[i]);
        uint8_t *d = fs_read(path, &n);
        int ok = d && dc6_load(d, n, &f->sheet) == 0;
        free(d);
        snprintf(path, sizeof path, "data/local/font/latin/%s.tbl", k_font_files[i]);
        d = ok ? fs_read(path, &n) : NULL;
        ok = ok && d && load_tbl(f, d, n) == 0;
        free(d);
        f->ok = ok;
        f->height = ok ? f->sheet.frames[0].height : 0;
        f->outlined = i == FEXOCET10 || i == FEXOCET8;
    }
    s_have_d2 = s_fonts[F16].ok;
    return s_have_d2 ? 0 : -1;
}

static const D2Font *pick_font(int scale)
{
    int i = scale <= 1 ? F8 : scale == 2 ? F16 : scale == 3 ? F24 : scale <= 5 ? F30 : scale <= 9 ? F42
          : scale == FONT_EXOCET || scale == FONT_EXOCET_ENGRAVED ? FEXOCET10 : scale == FONT_EXOCET_SMALL ? FEXOCET8
          : scale == FONT_FORMAL ? FFORMAL12 : scale == FONT_FORMAL_SMALL ? FFORMAL10 : F6;
    return s_fonts[i].ok ? &s_fonts[i] : &s_fonts[F16];
}

/* UTF-8 -> código Latin-1 (los caracteres fuera de Latin-1 salen como '?') */
static int next_latin1(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (p[0] < 0x80) {
        *s += 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0 && p[1]) {
        *s += 2;
        int cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        if (cp == 0xA0)
            return ' '; /* espacio que no se corta */
        return cp < 256 ? cp : '?';
    }
    int len = p[0] >= 0xF0 ? 4 : 3;
    for (int i = 0; i < len && **s; i++)
        (*s)++;
    return '?';
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

static int d2_width(const D2Font *f, const char *s)
{
    int w = 0;
    while (*s)
        w += f->width[next_latin1(&s)];
    return w;
}

static void d2_draw(const D2Font *f, int x, int y, const char *s, uint8_t color, int engraved)
{
    /* El blanco (color 0) no tiene tabla: la fuente va con sus colores propios */
    int tc = text_color_for(color);
    const uint8_t *lut = tc == 0 ? NULL : g_text_shift[tc];
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
    while (*s) {
        int c = next_latin1(&s);
        int fr = f->frame[c];
        if (fr < f->sheet.frames_per_dir) {
            const SpriteFrame *g = &f->sheet.frames[fr];
            if (!f->outlined || !engraved) /* sombra */
                fb_blit_mask_spans(g->pixels, g->spans, g->width, g->height, x + 1, y + 1, COL(R_GRAY, 0));
            if (lut)
                fb_blit_remap_spans(g->pixels, g->spans, g->width, g->height, x, y, lut);
            else
                fb_blit_spans(g->pixels, g->spans, g->width, g->height, x, y);
        }
        x += f->width[c];
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
    return s_have_d2 ? d2_width(pick_font(scale), s) : builtin_width(s, scale);
}

int font_height(int scale)
{
    return s_have_d2 ? pick_font(scale)->height : 7 * scale;
}

void font_draw(int x, int y, const char *s, int scale, uint8_t color)
{
    s = lang_screen(s);
    if (s_have_d2) {
        d2_draw(pick_font(scale), x, y, s, color, scale == FONT_EXOCET_ENGRAVED);
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
