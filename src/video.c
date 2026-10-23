#include "video.h"

#include "fmt/formats.h"
#include "fmt/fs.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Framebuffer g_fb;

static SDL_Window   *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture  *s_tex;
static int           s_vsync;
static int           s_565; /* D2_RGB565: textura de 16 bits (la mitad para subir a la GPU) */
static int           s_hide_cursor;  /* pantalla completa: sin puntero del mouse */
static SDL_Cursor   *s_blank_cursor; /* transparente: aunque algo lo muestre, no se ve */
static int           s_presents;

/* En la tele (Batocera) no hay mouse. Con KMSDRM, SDL pone el puntero al
   crear la ventana y algunas versiones lo vuelven a mostrar con los primeros
   cuadros: se esconde y además se cambia por uno transparente. */
static void hide_cursor(void)
{
    if (!s_blank_cursor) {
        static const Uint8 none[8] = { 0 };
        s_blank_cursor = SDL_CreateCursor(none, none, 8, 8, 0, 0);
    }
    if (s_blank_cursor)
        SDL_SetCursor(s_blank_cursor);
    SDL_ShowCursor(SDL_DISABLE);
}

/* Rango visible de cada fila; una fila vacía tiene x0 > x1 */
static int16_t s_clip_x0[SCREEN_H];
static int16_t s_clip_x1[SCREEN_H];

/* Dónde dibujan las primitivas: la pantalla, o el búfer de una capa mientras
   se dibuja lo que le falta (fb_layer_update) */
static uint8_t *s_px = g_fb.px;

static int neon_check(void);

int video_init(int headless, int fullscreen, int scale)
{
    clip_reset();
    neon_check(); /* en la Pi: probar NEON una vez (y dejarlo en el log) */
    if (headless)
        return 0;
    if (scale < 1)
        scale = 1;

    Uint32 flags = SDL_WINDOW_RESIZABLE;
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    s_hide_cursor = fullscreen && !getenv("D2_SHOW_CURSOR");
    if (s_hide_cursor)
        hide_cursor();
    s_win = SDL_CreateWindow("Diablo II (port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_W * scale, SCREEN_H * scale, flags);
    if (!s_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    if (s_hide_cursor)
        hide_cursor();

    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_ren)
        s_ren = SDL_CreateRenderer(s_win, -1, 0);
    if (!s_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(s_ren, &info) == 0) {
        s_vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
        printf("render: %s%s\n", info.name, s_vsync ? " (vsync)" : "");
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(s_ren, SCREEN_W, SCREEN_H);
    if (s_hide_cursor)
        hide_cursor();
    /* En la Pi subir el cuadro a la GPU es de lo más caro: con D2_RGB565=1 se
       sube en 16 bits (la mitad de bytes, con algo menos de precisión en los
       colores oscuros) */
    s_565 = getenv("D2_RGB565") && getenv("D2_RGB565")[0] == '1';
    s_tex = SDL_CreateTexture(s_ren, s_565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!s_tex && s_565) {
        s_565 = 0;
        s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    }
    if (s_565)
        printf("textura de 16 bits (RGB565)\n");
    if (!s_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

void video_shutdown(void)
{
    if (s_blank_cursor)
        SDL_FreeCursor(s_blank_cursor);
    s_blank_cursor = NULL;
    if (s_tex) SDL_DestroyTexture(s_tex);
    if (s_ren) SDL_DestroyRenderer(s_ren);
    if (s_win) SDL_DestroyWindow(s_win);
    s_tex = NULL;
    s_ren = NULL;
    s_win = NULL;
}

int video_has_vsync(void)
{
    return s_vsync;
}

/* ---- NEON (la Pi): pasar bytes por una tabla de 256 de a 16 por vez ----
   TBL busca en tablas de hasta 64 bytes: la de 256 va en cuatro pedazos y
   cada índice cae en uno solo (fuera de rango, TBX deja lo que había). Sirve
   para byte -> byte (la luz); para convertir a colores de 32 o 16 bits son 4 o
   2 búsquedas por píxel y en el Cortex-A53 de la Pi 3 eso es más lento que
   leer la paleta de a uno. Al arrancar se compara con la versión común sobre
   datos de prueba; si no da igual, no se usa (y queda en el log). */
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define HAVE_NEON 1
typedef struct {
    uint8x16x4_t q[4];
} NeonLut;

static void neon_lut_load(NeonLut *t, const uint8_t *lut)
{
    for (int k = 0; k < 4; k++)
        t->q[k] = vld1q_u8_x4(lut + 64 * k);
}

static inline uint8x16_t neon_lookup(const NeonLut *t, uint8x16_t v)
{
    uint8x16_t r = vqtbl4q_u8(t->q[0], v);
    r = vqtbx4q_u8(r, t->q[1], vsubq_u8(v, vdupq_n_u8(64)));
    r = vqtbx4q_u8(r, t->q[2], vsubq_u8(v, vdupq_n_u8(128)));
    r = vqtbx4q_u8(r, t->q[3], vsubq_u8(v, vdupq_n_u8(192)));
    return r;
}
#endif
static int s_neon = -1; /* -1 sin probar, 0 no se usa, 1 se usa */

/* Un tramo del cuadro a 32 bits (ARGB8888) o a 16 (RGB565): de a 8 píxeles
   por lectura y de a 2 o 4 por escritura (los bytes en el orden de la Pi y la
   PC). Cada escritura por separado: juntas en un arreglo, GCC las arma con
   NEON en la pila y en la Pi tarda casi el doble. */
static void span_to_argb(const uint8_t *src, uint32_t *dst, int n, const uint32_t *pal)
{
    int x = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    for (; x + 7 < n; x += 8) {
        uint64_t v;
        memcpy(&v, src + x, 8);
        uint64_t o0 = pal[v & 255] | (uint64_t)pal[(v >> 8) & 255] << 32;
        uint64_t o1 = pal[(v >> 16) & 255] | (uint64_t)pal[(v >> 24) & 255] << 32;
        uint64_t o2 = pal[(v >> 32) & 255] | (uint64_t)pal[(v >> 40) & 255] << 32;
        uint64_t o3 = pal[(v >> 48) & 255] | (uint64_t)pal[v >> 56] << 32;
        memcpy(dst + x, &o0, 8);
        memcpy(dst + x + 2, &o1, 8);
        memcpy(dst + x + 4, &o2, 8);
        memcpy(dst + x + 6, &o3, 8);
    }
#endif
    for (; x < n; x++)
        dst[x] = pal[src[x]];
}

static void span_to_565(const uint8_t *src, uint16_t *dst, int n, const uint16_t *pal)
{
    int x = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    for (; x + 7 < n; x += 8) {
        uint64_t v;
        memcpy(&v, src + x, 8);
        uint64_t o0 = pal[v & 255] | (uint64_t)pal[(v >> 8) & 255] << 16 | (uint64_t)pal[(v >> 16) & 255] << 32 |
                      (uint64_t)pal[(v >> 24) & 255] << 48;
        uint64_t o1 = pal[(v >> 32) & 255] | (uint64_t)pal[(v >> 40) & 255] << 16 |
                      (uint64_t)pal[(v >> 48) & 255] << 32 | (uint64_t)pal[v >> 56] << 48;
        memcpy(dst + x, &o0, 8);
        memcpy(dst + x + 4, &o1, 8);
    }
#endif
    for (; x < n; x++)
        dst[x] = pal[src[x]];
}

/* Una tabla de luz sobre un tramo de la fila, de a 16 (NEON) o de a 8 por lectura */
static void darken_span(uint8_t *row, int a, int b, const uint8_t *lut, int neon);

static int neon_check(void)
{
    if (s_neon >= 0)
        return s_neon;
    s_neon = 0;
#ifdef HAVE_NEON
    uint8_t lut[256], src[256 + 37], r1[256 + 37], r2[256 + 37];
    uint32_t seed = 12345u;
    for (int i = 0; i < 256; i++) {
        seed = seed * 1664525u + 1013904223u;
        lut[i] = (uint8_t)(seed >> 24);
    }
    for (int i = 0; i < 256 + 37; i++)
        src[i] = (uint8_t)(i < 256 ? i : i * 7);
    memcpy(r1, src, sizeof r1);
    memcpy(r2, src, sizeof r2);
    darken_span(r1, 3, 256 + 33, lut, 0);
    darken_span(r2, 3, 256 + 33, lut, 1);
    s_neon = memcmp(r1, r2, sizeof r1) == 0;
    printf("NEON: %s\n", s_neon ? "activo" : "desactivado (no dio igual que la versión común)");
#endif
    return s_neon;
}

/* Las paletas de la conversión: la del cuadro y la oscura (la del cuadro
   pasada por la tabla del nivel más oscuro), para aplicar la oscuridad
   pendiente al convertir. En lo iluminado cada píxel pasa por la tabla de su
   nivel (256 bytes) y después por la paleta: una paleta de 32 bits por nivel
   ocuparía los 32 KB de la caché L1 de la Pi y sería más lento. */
enum { CPAL_PLAIN, CPAL_DARK };
static uint32_t s_cpal32[2][256];
static uint16_t s_cpal16[2][256];

static void convert_row(int y, void *dst, int is16);

static void convert_frame(void *dst, int pitch_px, int is16);

static void convert_to_argb(uint32_t *dst, int dst_pitch_px)
{
    convert_frame(dst, dst_pitch_px, 0);
}

static void convert_to_565(uint16_t *dst, int dst_pitch_px)
{
    convert_frame(dst, dst_pitch_px, 1);
}

static double s_upload_ms; /* lo que tardó la última subida del cuadro (convertir y pasarlo a la GPU) */

double video_upload_ms(void)
{
    return s_upload_ms;
}

int video_is_565(void)
{
    return s_565;
}

/* Cambia la textura a 16 bits (RGB565) o 32 (ARGB8888); en la Pi subir el
   cuadro a la GPU es de lo más caro y en 16 bits son la mitad de bytes */
void video_set_565(int on)
{
    on = on != 0;
    if (!s_ren || on == s_565)
        return;
    SDL_Texture *t = SDL_CreateTexture(s_ren, on ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!t)
        return;
    if (s_tex)
        SDL_DestroyTexture(s_tex);
    s_tex = t;
    s_565 = on;
    printf("textura de %d bits\n", on ? 16 : 32);
}

void video_present(void)
{
    if (!s_tex)
        return;
    void *pixels;
    int pitch;
    Uint64 t0 = SDL_GetPerformanceCounter();
    if (SDL_LockTexture(s_tex, NULL, &pixels, &pitch) != 0)
        return;
    if (s_565)
        convert_to_565((uint16_t *)pixels, pitch / 2);
    else
        convert_to_argb((uint32_t *)pixels, pitch / 4);
    SDL_UnlockTexture(s_tex);
    s_upload_ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    fb_frame_begin();
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
    if (s_hide_cursor && s_presents < 300) { /* los primeros segundos, por las dudas */
        s_presents++;
        hide_cursor();
    }
}

/* ---- Cinemáticas (cinema.c) ----
   El cuadro llega en YUV 4:2:0 y lo convierte la GPU (en la Pi, sin pasar
   por el procesador). Se dibuja en toda la ventana, no en los 800x600 del
   juego: en una tele ancha se ve más grande. */
static SDL_Texture *s_movie;
static int          s_movie_w, s_movie_h;
static char         s_movie_shot[256]; /* guardar el próximo cuadro ahí (depuración) */

void video_movie_shot(const char *path)
{
    snprintf(s_movie_shot, sizeof s_movie_shot, "%s", path ? path : "");
}

int video_movie_begin(int w, int h)
{
    if (!s_ren)
        return -1;
    s_movie = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!s_movie) {
        fprintf(stderr, "cinemática: %s\n", SDL_GetError());
        return -1;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(s_movie, SDL_ScaleModeLinear);
#endif
    s_movie_w = w;
    s_movie_h = h;
    SDL_RenderSetLogicalSize(s_ren, 0, 0);
    return 0;
}

void video_movie_frame(const uint8_t *y, int ys, const uint8_t *u, int us, const uint8_t *v, int vs)
{
    if (!s_movie)
        return;
    if (y)
        SDL_UpdateYUVTexture(s_movie, NULL, y, ys, u, us, v, vs);
    int ww, wh;
    if (SDL_GetRendererOutputSize(s_ren, &ww, &wh) != 0 || ww <= 0 || wh <= 0)
        return;
    /* Lo más grande que entra sin deformarlo */
    SDL_Rect r;
    r.w = ww;
    r.h = (int)((long long)ww * s_movie_h / s_movie_w);
    if (r.h > wh) {
        r.h = wh;
        r.w = (int)((long long)wh * s_movie_w / s_movie_h);
    }
    r.x = (ww - r.w) / 2;
    r.y = (wh - r.h) / 2;
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_movie, NULL, &r);
    if (s_movie_shot[0]) { /* D2_CINEMA_SHOT: lo que se ve, como sale de la GPU */
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (sf && SDL_RenderReadPixels(s_ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0)
            SDL_SaveBMP(sf, s_movie_shot);
        SDL_FreeSurface(sf);
        s_movie_shot[0] = '\0';
    }
    SDL_RenderPresent(s_ren);
    if (s_hide_cursor)
        hide_cursor();
}

void video_movie_end(void)
{
    if (s_movie)
        SDL_DestroyTexture(s_movie);
    s_movie = NULL;
    if (s_ren)
        SDL_RenderSetLogicalSize(s_ren, SCREEN_W, SCREEN_H);
}

double video_convert_bench(void)
{
    static uint32_t *buf;
    if (!buf)
        buf = malloc((size_t)SCREEN_W * SCREEN_H * sizeof *buf);
    if (!buf)
        return 0.0;
    Uint64 t0 = SDL_GetPerformanceCounter();
    convert_to_argb(buf, SCREEN_W);
    return (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

int video_save_bmp(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s)
        return -1;
    convert_to_argb((uint32_t *)s->pixels, s->pitch / 4);
    int r = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return r;
}

/* ---- Paletas ---- */

uint8_t  g_col[256];
uint8_t  g_light[LIGHT_LEVELS][256];
uint8_t *g_blend[BLEND_COUNT];
uint8_t  g_text_shift[TEXT_COLORS][256];
uint32_t g_text_rgb[TEXT_COLORS];

/* Dónde están las tablas dentro de pal.pl2 */
#define PL2_LIGHT       1024
#define PL2_TEXT_COLORS 439808
#define PL2_TEXT_SHIFTS 439847
#define PL2_SIZE        443175
#define PL2_HUES        341248 /* HueVariations: 111 tablas de 256 */
#define PL2_NUM_HUES    111

static uint8_t g_hue[PL2_NUM_HUES][256];
static int     g_hue_ok;

const uint8_t *fb_hue(int i)
{
    return g_hue_ok && i >= 0 && i < PL2_NUM_HUES ? g_hue[i] : NULL;
}

static const uint8_t k_ramp_base[16][3] = {
    {255, 255, 255}, /* R_GRAY  */
    {112, 160,  64}, /* R_GRASS */
    {176, 136,  88}, /* R_DIRT  */
    {168, 172, 184}, /* R_STONE */
    {200, 128,  64}, /* R_WOOD  */
    {240, 192, 152}, /* R_SKIN  */
    {232,  48,  40}, /* R_RED   */
    { 64, 104, 240}, /* R_BLUE  */
    {248, 208,  72}, /* R_GOLD  */
    {216, 200, 160}, /* R_CLOTH */
};

/* Color de la rampa r, tono s: 0 = negro, 15 = color base, con una curva
   suave para que los oscuros no se empasten. */
static uint32_t ramp_color(int r, int s)
{
    float k = s == 0 ? 0.0f : powf((s + 1) / 16.0f, 0.8f);
    uint32_t cr = (uint32_t)(k_ramp_base[r][0] * k);
    uint32_t cg = (uint32_t)(k_ramp_base[r][1] * k);
    uint32_t cb = (uint32_t)(k_ramp_base[r][2] * k);
    return 0xFF000000u | (cr << 16) | (cg << 8) | cb;
}

void pal_init_placeholder(void)
{
    for (int i = 0; i < 256; i++) {
        g_fb.pal[i] = ramp_color(i / 16, i % 16);
        g_col[i] = (uint8_t)i;
        for (int l = 0; l < LIGHT_LEVELS; l++)
            g_light[l][i] = (uint8_t)((i & 0xF0) | ((i & 15) * l / (LIGHT_LEVELS - 1)));
        for (int c = 0; c < TEXT_COLORS; c++)
            g_text_shift[c][i] = (uint8_t)i;
    }
    for (int c = 0; c < TEXT_COLORS; c++)
        g_text_rgb[c] = 0xFFFFFFFFu;
}

static int nearest_color(uint32_t argb)
{
    int r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    int best = 0, best_d = 1 << 30;
    for (int i = 0; i < 256; i++) {
        int dr = r - (int)((g_fb.pal[i] >> 16) & 0xFF);
        int dg = g - (int)((g_fb.pal[i] >> 8) & 0xFF);
        int db = b - (int)(g_fb.pal[i] & 0xFF);
        int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

int pal_load_menu(void)
{
    size_t n;
    uint8_t *dat = fs_read("data/global/palette/units/pal.dat", &n);
    uint32_t pal[256];
    int ok = dat && pal_load(dat, n, pal) == 0;
    if (ok) {
        memcpy(g_fb.pal, pal, sizeof pal);
        for (int i = 0; i < 256; i++)
            g_col[i] = (uint8_t)nearest_color(ramp_color(i / 16, i % 16));
    }
    free(dat);
    return ok ? 0 : -1;
}

int pal_load_d2(const char *act)
{
    char path[128];
    size_t n, n2;
    snprintf(path, sizeof path, "data/global/palette/%s/pal.dat", act);
    uint8_t *dat = fs_read(path, &n);
    snprintf(path, sizeof path, "data/global/palette/%s/pal.pl2", act);
    uint8_t *pl2 = fs_read(path, &n2);

    uint32_t pal[256];
    int ok = dat && pl2 && pal_load(dat, n, pal) == 0 && n2 >= PL2_SIZE;
    if (ok) {
        memcpy(g_fb.pal, pal, sizeof pal);
        memcpy(g_light, pl2 + PL2_LIGHT, sizeof g_light);
        memcpy(g_text_shift, pl2 + PL2_TEXT_SHIFTS, sizeof g_text_shift);
        memcpy(g_hue, pl2 + PL2_HUES, sizeof g_hue);
        g_hue_ok = 1;
        /* Mezclas: 3 de transparencia (25, 50 y 75%), suma y multiplicación */
        static const size_t k_blend_at[BLEND_COUNT] = { 13568, 13568 + 65536, 13568 + 131072, 210176, 275712 };
        for (int b = 0; b < BLEND_COUNT; b++) {
            if (!g_blend[b])
                g_blend[b] = malloc(65536);
            if (g_blend[b])
                memcpy(g_blend[b], pl2 + k_blend_at[b], 65536);
        }
        for (int c = 0; c < TEXT_COLORS; c++) {
            const uint8_t *rgb = pl2 + PL2_TEXT_COLORS + c * 3;
            g_text_rgb[c] = 0xFF000000u | ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
        }
        for (int i = 0; i < 256; i++)
            g_col[i] = (uint8_t)nearest_color(ramp_color(i / 16, i % 16));
    }
    free(dat);
    free(pl2);
    return ok ? 0 : -1;
}

/* ---- Recorte ---- */

/* El rectángulo que ocupa el recorte (para no recorrer lo que queda afuera) */
static int s_clip_bx0, s_clip_by0, s_clip_bx1 = SCREEN_W - 1, s_clip_by1 = SCREEN_H - 1;

static void clip_update_bounds(void)
{
    s_clip_bx0 = SCREEN_W;
    s_clip_bx1 = -1;
    s_clip_by0 = SCREEN_H;
    s_clip_by1 = -1;
    for (int y = 0; y < SCREEN_H; y++) {
        if (s_clip_x0[y] > s_clip_x1[y])
            continue;
        if (y < s_clip_by0) s_clip_by0 = y;
        s_clip_by1 = y;
        if (s_clip_x0[y] < s_clip_bx0) s_clip_bx0 = s_clip_x0[y];
        if (s_clip_x1[y] > s_clip_bx1) s_clip_bx1 = s_clip_x1[y];
    }
}

int clip_bounds(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = s_clip_bx0;
    *y0 = s_clip_by0;
    *x1 = s_clip_bx1;
    *y1 = s_clip_by1;
    return s_clip_bx0 <= s_clip_bx1 && s_clip_by0 <= s_clip_by1;
}

void clip_reset(void)
{
    for (int y = 0; y < SCREEN_H; y++) {
        s_clip_x0[y] = 0;
        s_clip_x1[y] = SCREEN_W - 1;
    }
    clip_update_bounds();
}

/* Deja visible un lado de la recta que pasa por (cx, cy) con normal (nx, ny):
   side < 0 conserva donde (p - c)·n <= 0, side > 0 donde (p - c)·n >= 0. */
void clip_half_plane(float cx, float cy, float nx, float ny, int side)
{
    if (side > 0) {
        nx = -nx;
        ny = -ny;
    }
    for (int y = 0; y < SCREEN_H; y++) {
        float py = (float)y + 0.5f - cy;
        float x0 = 0.0f, x1 = (float)(SCREEN_W - 1);
        if (fabsf(nx) < 1e-6f) {
            if (py * ny > 0.0f) {
                x0 = 1.0f;
                x1 = 0.0f;
            }
        } else {
            float xb = cx - py * ny / nx - 0.5f;
            if (nx > 0.0f)
                x1 = floorf(xb);
            else
                x0 = ceilf(xb);
        }
        if (x0 < 0.0f) x0 = 0.0f;
        if (x0 > SCREEN_W) x0 = SCREEN_W;
        if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
        if (x1 < -1.0f) x1 = -1.0f;
        s_clip_x0[y] = (int16_t)x0;
        s_clip_x1[y] = (int16_t)x1;
    }
    clip_update_bounds();
}

static inline int clip_span(int y, int *x0, int *x1)
{
    if (y < 0 || y >= SCREEN_H)
        return 0;
    if (*x0 < s_clip_x0[y]) *x0 = s_clip_x0[y];
    if (*x1 > s_clip_x1[y]) *x1 = s_clip_x1[y];
    return *x0 <= *x1;
}

/* La oscuridad pendiente (más abajo): lo que se dibuja encima primero la
   resuelve, o solo la marca si tapa del todo esos píxeles */
static void dark_resolve_slow(int y, int a, int b);
static int s_ndark, s_dark_maxy = -1;
static inline void dark_resolve(int y, int a, int b);
static inline void dark_cover(int y, int a, int b);

static inline void put_px(int x, int y, uint8_t c)
{
    if (y >= 0 && y < SCREEN_H && x >= s_clip_x0[y] && x <= s_clip_x1[y]) {
        dark_cover(y, x, x);
        s_px[y * SCREEN_W + x] = c;
    }
}

/* ---- Primitivas ---- */

void fb_frame_begin(void);

void fb_clear(uint8_t c)
{
    fb_frame_begin(); /* todo pisado: no queda oscuridad pendiente */
    memset(g_fb.px, c, sizeof g_fb.px);
}

void fb_fill_clip(uint8_t c)
{
    for (int y = 0; y < SCREEN_H; y++)
        fb_hline(0, SCREEN_W - 1, y, c);
}

void fb_hline(int x0, int x1, int y, uint8_t c)
{
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (!clip_span(y, &x0, &x1))
        return;
    dark_cover(y, x0, x1);
    memset(&s_px[y * SCREEN_W + x0], c, (size_t)(x1 - x0 + 1));
}

void fb_vline(int x, int y0, int y1, uint8_t c)
{
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    if (y0 < 0) y0 = 0;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    for (int y = y0; y <= y1; y++)
        put_px(x, y, c);
}

void fb_fill_rect(int x, int y, int w, int h, uint8_t c)
{
    for (int i = 0; i < h; i++)
        fb_hline(x, x + w - 1, y + i, c);
}

void fb_line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Rombo isométrico centrado en (cx, cy), con media anchura hw y media altura hh. */
void fb_diamond(int cx, int cy, int hw, int hh, uint8_t fill, uint8_t edge)
{
    for (int i = 0; i < 2 * hh; i++) {
        int r = i < hh ? i : 2 * hh - 1 - i;
        int span = (r + 1) * hw / hh;
        int y = cy - hh + i;
        fb_hline(cx - span, cx + span - 1, y, fill);
        if (edge != fill) {
            put_px(cx - span, y, edge);
            put_px(cx + span - 1, y, edge);
        }
    }
}

/* Prisma isométrico: base en el rombo (cx, cy), elevado `height` píxeles. */
void fb_block(int cx, int cy, int hw, int hh, int height, uint8_t top, uint8_t left, uint8_t right)
{
    for (int x = cx - hw; x < cx; x++) {
        int yb = cy + (x - (cx - hw)) * hh / hw;
        fb_vline(x, yb - height, yb, left);
    }
    for (int x = cx; x < cx + hw; x++) {
        int yb = cy + hh - (x - cx) * hh / hw;
        fb_vline(x, yb - height, yb, right);
    }
    fb_diamond(cx, cy - height, hw, hh, top, top);
}

void fb_ellipse(int cx, int cy, int rx, int ry, uint8_t c)
{
    for (int dy = -ry; dy <= ry; dy++) {
        float t = (float)dy / (float)ry;
        int span = (int)(rx * sqrtf(1.0f - t * t) + 0.5f);
        fb_hline(cx - span, cx + span, cy + dy, c);
    }
}

/* Oscurece lo que ya está dibujado usando las tablas de luz */
void fb_shade_ellipse(int cx, int cy, int rx, int ry, int level)
{
    const uint8_t *lut = g_light[level < 0 ? 0 : level >= LIGHT_LEVELS ? LIGHT_LEVELS - 1 : level];
    for (int dy = -ry; dy <= ry; dy++) {
        int y = cy + dy;
        float t = (float)dy / (float)ry;
        int span = (int)(rx * sqrtf(1.0f - t * t) + 0.5f);
        int x0 = cx - span, x1 = cx + span;
        if (!clip_span(y, &x0, &x1))
            continue;
        dark_resolve(y, x0, x1);
        uint8_t *row = &s_px[y * SCREEN_W];
        for (int x = x0; x <= x1; x++)
            row[x] = lut[row[x]];
    }
}

void fb_shade_rect(int x, int y, int w, int h, int level)
{
    const uint8_t *lut = g_light[level < 0 ? 0 : level >= LIGHT_LEVELS ? LIGHT_LEVELS - 1 : level];
    int neon = neon_check();
    for (int j = 0; j < h; j++) {
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(y + j, &x0, &x1))
            continue;
        dark_resolve(y + j, x0, x1);
        darken_span(&s_px[(y + j) * SCREEN_W], x0, x1, lut, neon); /* de a 16 (los carteles del HUD) */
    }
}

/* Pasa un tramo de la fila por una tabla de luz: de a 16 con NEON, si no de
   a 4 píxeles por lectura (el orden de los bytes no importa: cada uno vuelve
   a su lugar) */
static void darken_span(uint8_t *row, int a, int b, const uint8_t *lut, int neon)
{
    int x = a;
#ifdef HAVE_NEON
    if (neon && b - a >= 15) { /* cargar la tabla son 16 lecturas: solo en tramos largos */
        NeonLut t;
        neon_lut_load(&t, lut);
        for (; x + 15 <= b; x += 16)
            vst1q_u8(row + x, neon_lookup(&t, vld1q_u8(row + x)));
    }
#else
    (void)neon;
#endif
    for (; x <= b && ((uintptr_t)(row + x) & 7); x++)
        row[x] = lut[row[x]];
    for (; x + 7 <= b; x += 8) {
        uint64_t v;
        memcpy(&v, row + x, 8);
        v = (uint64_t)lut[v & 255] | (uint64_t)lut[(v >> 8) & 255] << 8 | (uint64_t)lut[(v >> 16) & 255] << 16 |
            (uint64_t)lut[(v >> 24) & 255] << 24 | (uint64_t)lut[(v >> 32) & 255] << 32 |
            (uint64_t)lut[(v >> 40) & 255] << 40 | (uint64_t)lut[(v >> 48) & 255] << 48 | (uint64_t)lut[v >> 56] << 56;
        memcpy(row + x, &v, 8);
    }
    for (; x <= b; x++)
        row[x] = lut[row[x]];
}

/* ---- La oscuridad de las cuevas ----
   fb_light_mask solo anota el pedido (las luces y el recorte de ese momento).
   La oscuridad se aplica al convertir el cuadro para la GPU: cada píxel pasa
   por la tabla de luz de su nivel justo antes de la paleta, sin una pasada
   aparte por toda la pantalla. Todo lo que se dibuja después (efectos,
   carteles, el HUD) y mira lo que hay debajo primero oscurece esos píxeles;
   lo opaco solo los marca. Los marcados (en s_umask) se convierten tal cual:
   lo translúcido queda mezclado con el fondo ya oscuro, como si la oscuridad
   se hubiera aplicado antes. */
enum { DARK_LEVEL = 3, DARK_STEPS = 4096, DARK_MAXL = 16, DARK_JOBS = 4 };
typedef struct {
    int            n, same, max_y;
    int            full_d2;        /* con un solo radio: más cerca que esto (²) de una luz, luz plena */
    int            lx[DARK_MAXL], ly[DARK_MAXL], r2[DARK_MAXL];
    uint32_t       inv[DARK_MAXL]; /* (DARK_STEPS - 1) / radio² en 16.16 */
    const uint8_t *level_r;        /* con un solo radio: distancia² / 64 -> nivel */
    int16_t        x0[SCREEN_H], x1[SCREEN_H];
} DarkJob;
static DarkJob s_dark[DARK_JOBS];
static int     s_full_id;
static uint8_t s_umask[SCREEN_H][SCREEN_W / 8]; /* lo dibujado después de la oscuridad */
static int     s_umask_used;
static uint8_t s_level_of[DARK_STEPS];
static uint8_t s_level_r[DARK_JOBS][DARK_STEPS];
static int     s_level_r2[DARK_JOBS] = { -1, -1, -1, -1 };

void fb_frame_begin(void)
{
    s_ndark = 0;
    s_dark_maxy = -1;
    if (s_umask_used) {
        memset(s_umask, 0, sizeof s_umask);
        s_umask_used = 0;
    }
}

static void dark_apply_row(const DarkJob *j, int y, uint8_t *row, int neon, int xa, int xb);

void fb_light_mask(const int *in_x, const int *in_y, const int *in_r, int in_n, int max_y)
{
    if (s_ndark >= DARK_JOBS)
        return;
    if (max_y >= SCREEN_H)
        max_y = SCREEN_H - 1;
    /* Nivel de luz según la distancia (relativa al radio de cada luz): pleno
       hasta el 55% del radio, después baja hasta casi negro */
    static int built;
    if (!built) {
        for (int i = 0; i < DARK_STEPS; i++) {
            float d = sqrtf((float)i / (float)(DARK_STEPS - 1));
            int lvl = d <= 0.55f ? LIGHT_LEVELS - 1 : d >= 1.0f ? DARK_LEVEL
                    : (int)(DARK_LEVEL + (LIGHT_LEVELS - 1 - DARK_LEVEL) * (1.0f - (d - 0.55f) / 0.45f));
            s_level_of[i] = (uint8_t)lvl;
        }
        built = 1;
    }
    int q = s_ndark;
    DarkJob *j = &s_dark[q];
    /* Solo las luces que llegan a la pantalla: la de un jugador en otra zona
       (con la pantalla partida) puede estar a decenas de miles de píxeles, y
       la distancia al cuadrado no entra en un int */
    j->n = 0;
    for (int k = 0; k < in_n && j->n < DARK_MAXL; k++) {
        int r = in_r[k];
        if (r > 0 && in_x[k] > -r && in_x[k] < SCREEN_W + r && in_y[k] > -r && in_y[k] < SCREEN_H + r) {
            j->lx[j->n] = in_x[k];
            j->ly[j->n] = in_y[k];
            j->r2[j->n] = r * r;
            j->inv[j->n] = (uint32_t)(((uint64_t)(DARK_STEPS - 1) << 16) / (uint64_t)(r * r));
            j->n++;
        }
    }
    /* Lo común (solo los jugadores, todos con el mismo radio): la distancia
       al cuadrado más chica va directo a una tabla de ese radio */
    j->same = j->n > 0;
    for (int k = 1; k < j->n && j->same; k++)
        j->same = j->r2[k] == j->r2[0];
    if (j->same && j->r2[0] != s_level_r2[q] && j->r2[0] < DARK_STEPS * 64) {
        for (int i = 0; i < DARK_STEPS; i++) {
            int t = (int)((int64_t)i * 64 * (DARK_STEPS - 1) / j->r2[0]);
            s_level_r[q][i] = s_level_of[t < DARK_STEPS ? t : DARK_STEPS - 1];
        }
        s_level_r2[q] = j->r2[0];
    }
    j->full_d2 = 0;
    j->same = j->same && j->r2[0] == s_level_r2[q];
    j->level_r = s_level_r[q];
    j->max_y = max_y;
    memcpy(j->x0, s_clip_x0, sizeof j->x0);
    memcpy(j->x1, s_clip_x1, sizeof j->x1);
    /* La luz plena es la identidad (así viene en pal.pl2): esos bloques no se tocan */
    const uint8_t *full = g_light[LIGHT_LEVELS - 1];
    s_full_id = 1;
    for (int i = 0; i < 256 && s_full_id; i++)
        s_full_id = full[i] == i;
    /* Hasta dónde llega la luz plena (el nivel baja con la distancia) */
    if (j->same && s_full_id) {
        int t = 0;
        while (t < DARK_STEPS && j->level_r[t] == LIGHT_LEVELS - 1)
            t++;
        j->full_d2 = t * 64;
    }
    /* Diferida a la conversión del cuadro, o inmediata con D2_DARK_DEFER=0
       (una pasada por la pantalla; en la Pi tarda 0,5 ms más por cuadro) */
    static int defer = -1;
    if (defer < 0)
        defer = !(getenv("D2_DARK_DEFER") && getenv("D2_DARK_DEFER")[0] == '0');
    if (!defer) {
        int neon = neon_check();
        for (int y = 0; y <= max_y; y++)
            dark_apply_row(j, y, &g_fb.px[y * SCREEN_W], neon, 0, SCREEN_W - 1);
        return;
    }
    s_ndark++;
    if (max_y > s_dark_maxy)
        s_dark_maxy = max_y;
}

/* Los tramos de la fila que alcanza alguna luz, ordenados y unidos (dentro
   del recorte del pedido); dy2: la distancia vertical² a cada luz */
static int dark_intervals(const DarkJob *j, int y, int *dy2, int *ia, int *ib)
{
    int x0 = j->x0[y], x1 = j->x1[y], ni = 0;
    for (int k = 0; k < j->n; k++) {
        int dy = (y - j->ly[k]) * 2;
        dy2[k] = dy * dy;
        if (dy2[k] >= j->r2[k])
            continue;
        int half = (int)sqrtf((float)(j->r2[k] - dy2[k]));
        int a = j->lx[k] - half > x0 ? j->lx[k] - half : x0, b = j->lx[k] + half < x1 ? j->lx[k] + half : x1;
        if (a > b)
            continue;
        int i = ni++;
        while (i > 0 && ia[i - 1] > a) {
            ia[i] = ia[i - 1];
            ib[i] = ib[i - 1];
            i--;
        }
        ia[i] = a;
        ib[i] = b;
    }
    int m = 0;
    for (int i = 0; i < ni; i++) {
        if (m > 0 && ia[i] <= ib[m - 1] + 1) { /* se tocan: un solo tramo */
            if (ib[i] > ib[m - 1])
                ib[m - 1] = ib[i];
        } else {
            ia[m] = ia[i];
            ib[m] = ib[i];
            m++;
        }
    }
    return m;
}

static inline int dark_level(const DarkJob *j, int cx, const int *dy2)
{
    if (j->same) {
        int best = j->r2[0];
        for (int k = 0; k < j->n; k++) {
            int dx = cx - j->lx[k], d2 = dx * dx + dy2[k];
            if (d2 < best)
                best = d2;
        }
        int idx = best >> 6;
        return j->level_r[idx < DARK_STEPS ? idx : DARK_STEPS - 1];
    }
    uint32_t best = DARK_STEPS - 1;
    for (int k = 0; k < j->n; k++) {
        int dx = cx - j->lx[k], d2 = dx * dx + dy2[k];
        if (d2 < j->r2[k]) {
            uint32_t t = (uint32_t)(((uint64_t)(uint32_t)d2 * j->inv[k]) >> 16);
            if (t < best)
                best = t;
        }
    }
    return s_level_of[best];
}

/* Convierte las columnas a..b de la fila `src` con una de las paletas */
static inline void conv_span(const uint8_t *src, void *dst, int is16, int a, int b, int pal)
{
    if (a > b)
        return;
    if (is16)
        span_to_565(src + a, (uint16_t *)dst + a, b - a + 1, s_cpal16[pal]);
    else
        span_to_argb(src + a, (uint32_t *)dst + a, b - a + 1, s_cpal32[pal]);
}

/* Qué se hace con la oscuridad de una fila: oscurecer la fila en el lugar o
   convertirla (a 32 o 16 bits) con la paleta de cada nivel de luz */
typedef enum { DW_DARKEN, DW_ARGB, DW_565 } DarkOut;

#if defined(__GNUC__)
#define DW_INLINE static inline __attribute__((always_inline))
#else
#define DW_INLINE static inline
#endif

/* Los tramos de la fila con luz plena seguro (ordenados y unidos): donde la
   distancia² a alguna luz es menor que full_d2. dy2: como en dark_intervals. */
static int dark_cores(const DarkJob *j, const int *dy2, int *ca, int *cb)
{
    int nc = 0;
    for (int k = 0; k < j->n; k++) {
        int m = j->full_d2 - dy2[k]; /* dx² < m */
        if (m <= 0)
            continue;
        int h = (int)sqrtf((float)m);
        while (h > 0 && h * h >= m)
            h--;
        while ((h + 1) * (h + 1) < m)
            h++;
        int a = j->lx[k] - h, b = j->lx[k] + h, i = nc++;
        while (i > 0 && ca[i - 1] > a) {
            ca[i] = ca[i - 1];
            cb[i] = cb[i - 1];
            i--;
        }
        ca[i] = a;
        cb[i] = b;
    }
    int n = 0;
    for (int i = 0; i < nc; i++) {
        if (n > 0 && ca[i] <= cb[n - 1] + 1) {
            if (cb[i] > cb[n - 1])
                cb[n - 1] = cb[i];
        } else {
            ca[n] = ca[i];
            cb[n] = cb[i];
            n++;
        }
    }
    return n;
}

/* Un tramo con luz plena (que es la identidad): oscurecer no lo cambia */
DW_INLINE void dark_full(DarkOut out, const uint8_t *src, void *dst, int a, int b)
{
    if (out != DW_DARKEN)
        conv_span(src, dst, out == DW_565, a, b, CPAL_PLAIN);
}

/* Un bloque alcanzado por la luz (a..b, hasta 4 píxeles) con su nivel */
DW_INLINE void dark_block(DarkOut out, uint8_t *row, const uint8_t *src, void *dst, int a, int b, int lvl)
{
    if (out == DW_DARKEN) {
        if (lvl == LIGHT_LEVELS - 1 && s_full_id) /* la luz plena suele ser la identidad */
            return;
        const uint8_t *lut = g_light[lvl];
        if (b - a == 3) {
            uint32_t v;
            memcpy(&v, row + a, 4);
            v = (uint32_t)lut[v & 255] | (uint32_t)lut[(v >> 8) & 255] << 8 | (uint32_t)lut[(v >> 16) & 255] << 16 |
                (uint32_t)lut[v >> 24] << 24;
            memcpy(row + a, &v, 4);
        } else {
            for (int x = a; x <= b; x++)
                row[x] = lut[row[x]];
        }
        return;
    }
    /* Convertir: los 4 píxeles de una lectura y las búsquedas intercaladas
       (de a uno, cada píxel encadena tres lecturas y el procesador de la Pi,
       que ejecuta en orden, las espera una por una) */
    const uint8_t *lut = g_light[lvl];
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (b - a == 3) {
        uint32_t v;
        memcpy(&v, src + a, 4);
        unsigned c0 = lut[v & 255], c1 = lut[(v >> 8) & 255], c2 = lut[(v >> 16) & 255], c3 = lut[v >> 24];
        if (out == DW_ARGB) {
            const uint32_t *p = s_cpal32[CPAL_PLAIN];
            uint64_t o0 = p[c0] | (uint64_t)p[c1] << 32, o1 = p[c2] | (uint64_t)p[c3] << 32;
            memcpy((uint32_t *)dst + a, &o0, 8);
            memcpy((uint32_t *)dst + a + 2, &o1, 8);
        } else {
            const uint16_t *p = s_cpal16[CPAL_PLAIN];
            uint64_t o = p[c0] | (uint64_t)p[c1] << 16 | (uint64_t)p[c2] << 32 | (uint64_t)p[c3] << 48;
            memcpy((uint16_t *)dst + a, &o, sizeof o);
        }
        return;
    }
#endif
    if (out == DW_ARGB) {
        const uint32_t *p = s_cpal32[CPAL_PLAIN];
        for (int x = a; x <= b; x++)
            ((uint32_t *)dst)[x] = p[lut[src[x]]];
    } else {
        const uint16_t *p = s_cpal16[CPAL_PLAIN];
        for (int x = a; x <= b; x++)
            ((uint16_t *)dst)[x] = p[lut[src[x]]];
    }
}

/* Un tramo que ninguna luz alcanza: todo oscuro */
DW_INLINE void dark_gap(DarkOut out, uint8_t *row, int neon, const uint8_t *src, void *dst, int a, int b)
{
    if (out == DW_DARKEN)
        darken_span(row, a, b, g_light[DARK_LEVEL], neon);
    else
        conv_span(src, dst, out == DW_565, a, b, CPAL_DARK);
}

/* La oscuridad de un pedido en la fila y, solo en las columnas xa..xb. Lo que
   ninguna luz alcanza va oscuro de una; en lo alcanzado, el nivel se calcula
   una vez cada 4 píxeles (bloques alineados de a 4 desde el comienzo del
   tramo, en su centro): cambia de a un nivel cada 4 o 5 píxeles, no se nota.
   `row` es la fila a oscurecer (DW_DARKEN); src y dst, la fila a convertir. */
DW_INLINE void dark_walk(const DarkJob *j, int y, int xa, int xb, DarkOut out, uint8_t *row, int neon,
                         const uint8_t *src, void *dst)
{
    if (y > j->max_y || j->x0[y] > j->x1[y])
        return;
    int x0 = j->x0[y] > xa ? j->x0[y] : xa, x1 = j->x1[y] < xb ? j->x1[y] : xb;
    if (x0 > x1)
        return;
    int dy2[DARK_MAXL], ia[DARK_MAXL], ib[DARK_MAXL], ca[DARK_MAXL], cb[DARK_MAXL];
    int ni = dark_intervals(j, y, dy2, ia, ib);
    int nc = j->full_d2 ? dark_cores(j, dy2, ca, cb) : 0, ci = 0;
    int x = x0;
    for (int i = 0; i < ni && x <= x1; i++) {
        int a = ia[i], b = ib[i];
        if (b < x)
            continue;
        if (a > x)
            dark_gap(out, row, neon, src, dst, x, a - 1 < x1 ? a - 1 : x1);
        for (int bx = a; bx <= b && bx <= x1;) {
            int last = (bx | 3) <= b ? (bx | 3) : b;
            /* Bloques enteros dentro de un núcleo de luz plena: de corrido, sin
               calcular el nivel de cada uno (su centro está en el núcleo) */
            while (ci < nc && cb[ci] < last)
                ci++;
            if (ci < nc && ca[ci] <= bx) {
                int e = cb[ci] >= b ? b : ((cb[ci] + 1) & ~3) - 1; /* fin del último bloque entero adentro */
                int p0 = bx > x0 ? bx : x0, p1 = e < x1 ? e : x1;
                if (p0 <= p1)
                    dark_full(out, src, dst, p0, p1);
                bx = e + 1;
                continue;
            }
            if (last >= x0)
                dark_block(out, row, src, dst, bx > x0 ? bx : x0, last < x1 ? last : x1,
                           dark_level(j, (bx + last) / 2, dy2));
            bx = last + 1;
        }
        x = b + 1;
    }
    if (x <= x1)
        dark_gap(out, row, neon, src, dst, x, x1);
}

/* La oscuridad de un pedido sobre la fila `row` (la fila y de la pantalla o
   una copia), solo en las columnas xa..xb */
static void dark_apply_row(const DarkJob *j, int y, uint8_t *row, int neon, int xa, int xb)
{
    dark_walk(j, y, xa, xb, DW_DARKEN, row, neon, NULL, NULL);
}

/* Marca como ya resueltos los píxeles a..b de la fila y */
static void umark(int y, int a, int b)
{
    uint8_t *m = s_umask[y];
    int ba = a >> 3, bb = b >> 3;
    if (ba == bb) {
        m[ba] |= (uint8_t)((0xFF << (a & 7)) & (0xFF >> (7 - (b & 7))));
    } else {
        m[ba] |= (uint8_t)(0xFF << (a & 7));
        if (bb - ba > 1)
            memset(m + ba + 1, 0xFF, (size_t)(bb - ba - 1));
        m[bb] |= (uint8_t)(0xFF >> (7 - (b & 7)));
    }
    s_umask_used = 1;
}

/* Antes de dibujar encima de la oscuridad: los píxeles a..b de la fila y que
   todavía no la tienen la reciben ya (en la pantalla) y quedan marcados. Solo
   dentro de los pedidos: el mundo de la otra mitad de la pantalla partida se
   dibuja después del pedido de la primera y no tiene que quedar marcado. */
static void dark_resolve_slow(int y, int a, int b)
{
    if (a < 0)
        a = 0;
    if (b > SCREEN_W - 1)
        b = SCREEN_W - 1;
    if (a > b)
        return;
    uint8_t *row = &g_fb.px[y * SCREEN_W];
    const uint8_t *m = s_umask[y];
    int done = 1;
    for (int x = a; x <= b && done; x++)
        done = (m[x >> 3] >> (x & 7)) & 1;
    if (done)
        return;
    uint8_t tmp[SCREEN_W];
    for (int q = 0; q < s_ndark; q++) {
        const DarkJob *j = &s_dark[q];
        if (y > j->max_y)
            continue;
        int c0 = j->x0[y] > a ? j->x0[y] : a, c1 = j->x1[y] < b ? j->x1[y] : b;
        if (c0 > c1)
            continue;
        memcpy(tmp + c0, row + c0, (size_t)(c1 - c0 + 1));
        dark_apply_row(j, y, tmp, 0, c0, c1);
        for (int x = c0; x <= c1; x++)
            if (!((m[x >> 3] >> (x & 7)) & 1))
                row[x] = tmp[x];
        umark(y, c0, c1);
    }
}

static inline void dark_resolve(int y, int a, int b)
{
    if (s_ndark && y >= 0 && y <= s_dark_maxy && a <= b)
        dark_resolve_slow(y, a, b);
}

/* Lo opaco (que pisa los píxeles a..b sin mirar lo que había) no necesita la
   oscuridad debajo: solo se marca para que la conversión lo deje como está */
static void dark_cover_slow(int y, int a, int b)
{
    for (int q = 0; q < s_ndark; q++) {
        const DarkJob *j = &s_dark[q];
        if (y > j->max_y)
            continue;
        int c0 = j->x0[y] > a ? j->x0[y] : a, c1 = j->x1[y] < b ? j->x1[y] : b;
        if (c0 <= c1)
            umark(y, c0, c1);
    }
}

static inline void dark_cover(int y, int a, int b)
{
    if (s_ndark && y >= 0 && y <= s_dark_maxy && a <= b)
        dark_cover_slow(y, a, b);
}

/* La fila y del cuadro convertida, con la oscuridad pendiente: cada tramo
   de un pedido con la paleta de su nivel de luz; lo dibujado después de la
   oscuridad (marcado en s_umask) con la del cuadro */
static void convert_row(int y, void *dst, int is16)
{
    const uint8_t *src = g_fb.px + y * SCREEN_W;
    const DarkJob *jq[DARK_JOBS];
    int nq = 0;
    if (y <= s_dark_maxy)
        for (int q = 0; q < s_ndark; q++) {
            const DarkJob *j = &s_dark[q];
            if (y > j->max_y || j->x0[y] > j->x1[y])
                continue;
            int i = nq++; /* ordenados por dónde empiezan */
            while (i > 0 && jq[i - 1]->x0[y] > j->x0[y]) {
                jq[i] = jq[i - 1];
                i--;
            }
            jq[i] = j;
        }
    if (!nq) {
        conv_span(src, dst, is16, 0, SCREEN_W - 1, CPAL_PLAIN);
        return;
    }
    int apart = 0;
    for (int i = 1; i < nq; i++)
        apart |= jq[i]->x0[y] <= jq[i - 1]->x1[y];
    if (apart) { /* pedidos que se pisan (no debería pasar): la fila oscurecida aparte */
        uint8_t tmp[SCREEN_W];
        memcpy(tmp, src, SCREEN_W);
        for (int i = 0; i < nq; i++)
            dark_apply_row(jq[i], y, tmp, neon_check(), 0, SCREEN_W - 1);
        conv_span(tmp, dst, is16, 0, SCREEN_W - 1, CPAL_PLAIN);
    } else {
        int x = 0;
        for (int i = 0; i < nq; i++) {
            conv_span(src, dst, is16, x, jq[i]->x0[y] - 1, CPAL_PLAIN);
            if (is16)
                dark_walk(jq[i], y, 0, SCREEN_W - 1, DW_565, NULL, 0, src, dst);
            else
                dark_walk(jq[i], y, 0, SCREEN_W - 1, DW_ARGB, NULL, 0, src, dst);
            x = jq[i]->x1[y] + 1;
        }
        conv_span(src, dst, is16, x, SCREEN_W - 1, CPAL_PLAIN);
    }
    if (s_umask_used) { /* lo dibujado después de la oscuridad queda como está */
        const uint8_t *m = s_umask[y];
        for (int k = 0; k < SCREEN_W / 8; k++) {
            if (!(k & 7) && k + 8 <= SCREEN_W / 8) { /* de a 8 bytes de la máscara: casi todo vacío */
                uint64_t w;
                memcpy(&w, m + k, 8);
                if (!w) {
                    k += 7;
                    continue;
                }
            }
            if (!m[k])
                continue;
            if (m[k] == 0xFF) {
                int e = k;
                while (e + 1 < SCREEN_W / 8 && m[e + 1] == 0xFF)
                    e++;
                conv_span(src, dst, is16, k * 8, e * 8 + 7, CPAL_PLAIN);
                k = e;
                continue;
            }
            for (int bit = 0; bit < 8; bit++)
                if ((m[k] >> bit) & 1)
                    conv_span(src, dst, is16, k * 8 + bit, k * 8 + bit, CPAL_PLAIN);
        }
    }
}

static void convert_frame(void *dst, int pitch_px, int is16)
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = g_fb.pal[i];
        if (is16)
            s_cpal16[CPAL_PLAIN][i] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
        else
            s_cpal32[CPAL_PLAIN][i] = c;
    }
    if (s_ndark)
        for (int i = 0; i < 256; i++) {
            if (is16)
                s_cpal16[CPAL_DARK][i] = s_cpal16[CPAL_PLAIN][g_light[DARK_LEVEL][i]];
            else
                s_cpal32[CPAL_DARK][i] = s_cpal32[CPAL_PLAIN][g_light[DARK_LEVEL][i]];
        }
    for (int y = 0; y < SCREEN_H; y++)
        convert_row(y, is16 ? (void *)((uint16_t *)dst + (size_t)y * pitch_px) : (void *)((uint32_t *)dst + (size_t)y * pitch_px),
                    is16);
}

void fb_blit_shade(const uint8_t *px, int w, int h, int x, int y, int level)
{
    const uint8_t *lut = g_light[level < 0 ? 0 : level >= LIGHT_LEVELS ? LIGHT_LEVELS - 1 : level];
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        const uint8_t *src = px + j * w + (x0 - x);
        uint8_t *dst = &s_px[sy * SCREEN_W + x0];
        for (int i = 0; i <= x1 - x0; i++)
            if (src[i])
                dst[i] = lut[dst[i]];
    }
}

/* Las sombras de los tiles por tramos: cada tramo pasa por la tabla de luz
   como la oscuridad (de a 16 con NEON) */
void fb_blit_shade_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, int level)
{
    if (!spans) {
        fb_blit_shade(px, w, h, x, y, level);
        return;
    }
    if (x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    const uint8_t *lut = g_light[level < 0 ? 0 : level >= LIGHT_LEVELS ? LIGHT_LEVELS - 1 : level];
    int neon = neon_check();
    const uint32_t *ofs = (const uint32_t *)spans;
    int j0 = y < s_clip_by0 ? s_clip_by0 - y : 0, j1 = y + h > s_clip_by1 + 1 ? s_clip_by1 + 1 - y : h;
    for (int j = j0; j < j1; j++) {
        int sy = y + j, cx0 = s_clip_x0[sy], cx1 = s_clip_x1[sy];
        if (cx0 > cx1)
            continue;
        const uint16_t *r = (const uint16_t *)(spans + ofs[j]), *end = (const uint16_t *)(spans + ofs[j + 1]);
        uint8_t *row = &s_px[sy * SCREEN_W];
        for (; r < end; r += 2) {
            int a = x + r[0], b = a + r[1] - 1;
            if (a < cx0) a = cx0;
            if (b > cx1) b = cx1;
            if (a <= b) {
                dark_resolve(sy, a, b);
                darken_span(row, a, b, lut, neon);
            }
        }
    }
}

void fb_blit_remap(const uint8_t *px, int w, int h, int x, int y, const uint8_t lut[256])
{
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        const uint8_t *src = px + j * w + (x0 - x);
        uint8_t *dst = &s_px[sy * SCREEN_W + x0];
        for (int i = 0; i <= x1 - x0; i++)
            if (src[i])
                dst[i] = lut[src[i]];
    }
}

/* Las letras (y lo chico con tramos): solo lo opaco, sin preguntar píxel por píxel */
void fb_blit_mask_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, uint8_t color)
{
    if (!spans) {
        fb_blit_mask(px, w, h, x, y, color);
        return;
    }
    if (x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    const uint32_t *ofs = (const uint32_t *)spans;
    int j0 = y < s_clip_by0 ? s_clip_by0 - y : 0, j1 = y + h > s_clip_by1 + 1 ? s_clip_by1 + 1 - y : h;
    for (int j = j0; j < j1; j++) {
        int sy = y + j, cx0 = s_clip_x0[sy], cx1 = s_clip_x1[sy];
        if (cx0 > cx1)
            continue;
        const uint16_t *r = (const uint16_t *)(spans + ofs[j]), *end = (const uint16_t *)(spans + ofs[j + 1]);
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (; r < end; r += 2) {
            int a = x + r[0], b = a + r[1] - 1;
            if (a < cx0) a = cx0;
            if (b > cx1) b = cx1;
            if (a <= b) {
                dark_cover(sy, a, b);
                memset(dst + a, color, (size_t)(b - a + 1));
            }
        }
    }
}

void fb_blit_remap_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, const uint8_t lut[256])
{
    if (!spans) {
        fb_blit_remap(px, w, h, x, y, lut);
        return;
    }
    if (x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    const uint32_t *ofs = (const uint32_t *)spans;
    int j0 = y < s_clip_by0 ? s_clip_by0 - y : 0, j1 = y + h > s_clip_by1 + 1 ? s_clip_by1 + 1 - y : h;
    for (int j = j0; j < j1; j++) {
        int sy = y + j, cx0 = s_clip_x0[sy], cx1 = s_clip_x1[sy];
        if (cx0 > cx1)
            continue;
        const uint16_t *r = (const uint16_t *)(spans + ofs[j]), *end = (const uint16_t *)(spans + ofs[j + 1]);
        const uint8_t *src = px + j * w;
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (; r < end; r += 2) {
            int a = x + r[0], b = a + r[1] - 1;
            if (a < cx0) a = cx0;
            if (b > cx1) b = cx1;
            dark_cover(sy, a, b);
            for (int i = a; i <= b; i++)
                dst[i] = lut[src[i - x]];
        }
    }
}

void fb_blit_mask(const uint8_t *px, int w, int h, int x, int y, uint8_t color)
{
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        const uint8_t *src = px + j * w + (x0 - x);
        uint8_t *dst = &s_px[sy * SCREEN_W + x0];
        for (int i = 0; i <= x1 - x0; i++)
            if (src[i])
                dst[i] = color;
    }
}

void fb_blit_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y)
{
    if (!spans) {
        fb_blit(px, w, h, x, y);
        return;
    }
    /* Entero afuera del recorte (con la pantalla partida, lo de la otra mitad): nada */
    if (x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    const uint32_t *ofs = (const uint32_t *)spans;
    int j0 = y < s_clip_by0 ? s_clip_by0 - y : 0, j1 = y + h > s_clip_by1 + 1 ? s_clip_by1 + 1 - y : h;
    for (int j = j0; j < j1; j++) {
        int sy = y + j, cx0 = s_clip_x0[sy], cx1 = s_clip_x1[sy];
        if (cx0 > cx1)
            continue;
        const uint16_t *r = (const uint16_t *)(spans + ofs[j]), *end = (const uint16_t *)(spans + ofs[j + 1]);
        const uint8_t *src = px + j * w;
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (; r < end; r += 2) {
            int a = x + r[0], b = a + r[1] - 1;
            if (a < cx0)
                a = cx0;
            if (b > cx1)
                b = cx1;
            if (a <= b) {
                dark_cover(sy, a, b);
                memcpy(dst + a, src + (a - x), (size_t)(b - a + 1));
            }
        }
    }
}

void fb_blit(const uint8_t *px, int w, int h, int x, int y)
{
    fb_blit_stride(px, w, w, h, x, y);
}

void fb_blit_spans_part(const uint8_t *px, const uint8_t *spans, int fw, int sx, int sy, int w, int h, int x, int y)
{
    if (!spans) {
        fb_blit_stride(px + sy * fw + sx, fw, w, h, x, y);
        return;
    }
    if (w <= 0 || h <= 0 || x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    const uint32_t *ofs = (const uint32_t *)spans;
    int j0 = y < s_clip_by0 ? s_clip_by0 - y : 0, j1 = y + h > s_clip_by1 + 1 ? s_clip_by1 + 1 - y : h;
    for (int j = j0; j < j1; j++) {
        int dy = y + j, cx0 = s_clip_x0[dy], cx1 = s_clip_x1[dy], fr = sy + j;
        if (cx0 > cx1)
            continue;
        const uint16_t *r = (const uint16_t *)(spans + ofs[fr]), *end = (const uint16_t *)(spans + ofs[fr + 1]);
        const uint8_t *src = px + fr * fw;
        uint8_t *dst = &s_px[dy * SCREEN_W];
        for (; r < end; r += 2) {
            int s0 = r[0], s1 = r[0] + r[1] - 1; /* columnas del cuadro, recortadas al pedazo */
            if (s0 < sx)
                s0 = sx;
            if (s1 > sx + w - 1)
                s1 = sx + w - 1;
            int a = x + (s0 - sx), b = x + (s1 - sx);
            if (a < cx0) {
                s0 += cx0 - a;
                a = cx0;
            }
            if (b > cx1)
                b = cx1;
            if (a <= b) {
                dark_cover(dy, a, b);
                memcpy(dst + a, src + s0, (size_t)(b - a + 1));
            }
        }
    }
}

void fb_blit_stride(const uint8_t *px, int stride, int w, int h, int x, int y)
{
    if (x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        const uint8_t *src = px + j * stride + (x0 - x);
        uint8_t *dst = &s_px[sy * SCREEN_W + x0];
        for (int i = 0; i <= x1 - x0; i++)
            if (src[i])
                dst[i] = src[i];
    }
}

void fb_blit_scaled(const uint8_t *px, int w, int h, int x, int y, int dw, int dh, int level)
{
    if (dw <= 0 || dh <= 0)
        return;
    const uint8_t *lut = level >= 0 && level < LIGHT_LEVELS - 1 ? g_light[level] : NULL;
    /* De qué columna sale cada una, calculado una vez (dividir en cada píxel
       era lo caro de los botones del HUD en la Pi) */
    int cols[SCREEN_W];
    int ncol = dw < SCREEN_W ? dw : SCREEN_W;
    for (int i = 0; i < ncol; i++)
        cols[i] = i * w / dw;
    for (int j = 0; j < dh; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + dw - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        const uint8_t *src = px + (j * h / dh) * w;
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (int dx = x0; dx <= x1; dx++) {
            uint8_t c = src[dx - x < ncol ? cols[dx - x] : (dx - x) * w / dw];
            if (c)
                dst[dx] = lut ? lut[c] : c;
        }
    }
}

void fb_blit_rotated(const uint8_t *px, int w, int h, int cx, int cy, int size, float angle)
{
    if (size <= 0 || w <= 0 || h <= 0)
        return;
    /* Para cada píxel de destino, de qué punto del sprite sale (girando al revés) */
    float s = sinf(angle), c = cosf(angle), kx = (float)w / size, ky = (float)h / size;
    int r = (int)(size * 0.7072f) + 1; /* medio lado por raíz de 2: entra girado de cualquier forma */
    for (int j = -r; j <= r; j++) {
        int sy = cy + j;
        int x0 = cx - r, x1 = cx + r;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (int dx = x0; dx <= x1; dx++) {
            float ox = (float)(dx - cx) + 0.5f, oy = (float)j + 0.5f;
            int u = (int)floorf((ox * c + oy * s) * kx + w * 0.5f);
            int v = (int)floorf((-ox * s + oy * c) * ky + h * 0.5f);
            if (u < 0 || v < 0 || u >= w || v >= h)
                continue;
            uint8_t col = px[v * w + u];
            if (col)
                dst[dx] = col;
        }
    }
}

void fb_blit_blend(const uint8_t *px, int w, int h, int x, int y, int mode)
{
    const uint8_t *t = mode >= 0 && mode < BLEND_COUNT ? g_blend[mode] : NULL;
    if (!t) {
        fb_blit(px, w, h, x, y);
        return;
    }
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        const uint8_t *src = px + j * w + (x0 - x);
        uint8_t *dst = &s_px[sy * SCREEN_W + x0];
        for (int i = 0; i <= x1 - x0; i++)
            if (src[i])
                dst[i] = t[src[i] * 256 + dst[i]];
    }
}

void fb_blit_blend_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, int mode)
{
    const uint8_t *t = mode >= 0 && mode < BLEND_COUNT ? g_blend[mode] : NULL;
    if (!t) {
        fb_blit_spans(px, spans, w, h, x, y);
        return;
    }
    if (!spans) {
        fb_blit_blend(px, w, h, x, y, mode);
        return;
    }
    if (x > s_clip_bx1 || x + w - 1 < s_clip_bx0 || y > s_clip_by1 || y + h - 1 < s_clip_by0)
        return;
    const uint32_t *ofs = (const uint32_t *)spans;
    int j0 = y < s_clip_by0 ? s_clip_by0 - y : 0, j1 = y + h > s_clip_by1 + 1 ? s_clip_by1 + 1 - y : h;
    for (int j = j0; j < j1; j++) {
        int sy = y + j, cx0 = s_clip_x0[sy], cx1 = s_clip_x1[sy];
        if (cx0 > cx1)
            continue;
        const uint16_t *r = (const uint16_t *)(spans + ofs[j]), *end = (const uint16_t *)(spans + ofs[j + 1]);
        const uint8_t *src = px + j * w;
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (; r < end; r += 2) {
            int a = x + r[0], b = a + r[1] - 1;
            if (a < cx0) a = cx0;
            if (b > cx1) b = cx1;
            dark_resolve(sy, a, b);
            for (int i = a; i <= b; i++)
                dst[i] = t[src[i - x] * 256 + dst[i]];
        }
    }
}

/* Aclara un rectángulo (el destello de un rayo): cada color pasa al de la
   paleta más parecido a él con un 15% (nivel 1) o un 30% (nivel 2) más de
   luz. Las tablas se arman la primera vez y de nuevo si cambia la paleta. */
void fb_brighten_rect(int x, int y, int w, int h, int level)
{
    static uint8_t table[2][256];
    static uint32_t built_for = 0;
    uint32_t sum = 1;
    for (int i = 0; i < 256; i++)
        sum = sum * 31u + g_fb.pal[i];
    if (sum != built_for) {
        for (int l = 0; l < 2; l++)
            for (int i = 0; i < 256; i++) {
                float k = l ? 1.30f : 1.15f;
                int add = l ? 24 : 12;
                uint32_t c = g_fb.pal[i];
                int r = (int)(((c >> 16) & 0xFF) * k) + add, g = (int)(((c >> 8) & 0xFF) * k) + add;
                int b = (int)((c & 0xFF) * k) + add;
                r = r > 255 ? 255 : r;
                g = g > 255 ? 255 : g;
                b = b > 255 ? 255 : b;
                table[l][i] = (uint8_t)nearest_color(0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
            }
        table[0][0] = table[1][0] = 0; /* el negro del fondo queda negro */
        built_for = sum;
    }
    const uint8_t *t = table[level >= 2 ? 1 : 0];
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (int i = x0; i <= x1; i++)
            dst[i] = t[dst[i]];
    }
}

void fb_blend_rect(int x, int y, int w, int h, uint8_t color, int mode)
{
    const uint8_t *t = mode >= 0 && mode < BLEND_COUNT ? g_blend[mode] : NULL;
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        int x0 = x, x1 = x + w - 1;
        if (!clip_span(sy, &x0, &x1))
            continue;
        dark_resolve(sy, x0, x1);
        uint8_t *dst = &s_px[sy * SCREEN_W];
        for (int i = x0; i <= x1; i++)
            dst[i] = t ? t[color * 256 + dst[i]] : color;
    }
}

/* ---- Capas guardadas entre cuadros ---- */

static uint8_t *s_layer_tmp; /* donde se dibujan las franjas nuevas antes de pasarlas a la capa */

static inline int wrap_mod(int v, int n)
{
    v %= n;
    return v < 0 ? v + n : v;
}

/* El tramo x0..x1 de la fila y de un búfer del tamaño de la pantalla, a la
   capa (to_layer) o de la capa al búfer: la fila de la capa da la vuelta */
static void layer_row(const FbLayer *l, uint8_t *buf, int y, int x0, int x1, int to_layer)
{
    uint8_t *lrow = l->px + wrap_mod(y + l->wy, SCREEN_H) * SCREEN_W;
    int lx = wrap_mod(x0 + l->wx, SCREEN_W), n = x1 - x0 + 1, first = SCREEN_W - lx < n ? SCREEN_W - lx : n;
    uint8_t *b = buf + y * SCREEN_W + x0;
    if (to_layer) {
        memcpy(lrow + lx, b, (size_t)first);
        if (n > first)
            memcpy(lrow, b + first, (size_t)(n - first));
    } else {
        memcpy(b, lrow + lx, (size_t)first);
        if (n > first)
            memcpy(b + first, lrow, (size_t)(n - first));
    }
}

void fb_layer_update(FbLayer *l, int wx, int wy, int redraw, void (*draw)(void *ctx), void *ctx)
{
    if (!l->px)
        l->px = malloc((size_t)SCREEN_W * SCREEN_H);
    if (!s_layer_tmp)
        s_layer_tmp = malloc((size_t)SCREEN_W * SCREEN_H);
    if (!l->px || !s_layer_tmp) { /* sin memoria: directo en la pantalla, como siempre */
        fb_fill_clip(0);
        draw(ctx);
        return;
    }
    /* Lo que entra desde el último cuadro: una franja de filas (arriba o
       abajo) y una de columnas (a un costado) en las filas que quedan */
    int rect[2][4], nr = 0, dx = wx - l->wx, dy = wy - l->wy;
    if (redraw || !l->valid || abs(dx) >= SCREEN_W || abs(dy) >= SCREEN_H) {
        rect[nr][0] = 0, rect[nr][1] = 0, rect[nr][2] = SCREEN_W - 1, rect[nr][3] = SCREEN_H - 1, nr++;
    } else {
        int ry0 = 0, ry1 = SCREEN_H - 1;
        if (dy > 0) {
            rect[nr][0] = 0, rect[nr][1] = SCREEN_H - dy, rect[nr][2] = SCREEN_W - 1, rect[nr][3] = SCREEN_H - 1, nr++;
            ry1 = SCREEN_H - dy - 1;
        } else if (dy < 0) {
            rect[nr][0] = 0, rect[nr][1] = 0, rect[nr][2] = SCREEN_W - 1, rect[nr][3] = -dy - 1, nr++;
            ry0 = -dy;
        }
        if (dx > 0)
            rect[nr][0] = SCREEN_W - dx, rect[nr][1] = ry0, rect[nr][2] = SCREEN_W - 1, rect[nr][3] = ry1, nr++;
        else if (dx < 0)
            rect[nr][0] = 0, rect[nr][1] = ry0, rect[nr][2] = -dx - 1, rect[nr][3] = ry1, nr++;
    }
    l->wx = wx;
    l->wy = wy;
    l->valid = 1;
    if (nr) {
        static int16_t keep_x0[SCREEN_H], keep_x1[SCREEN_H];
        memcpy(keep_x0, s_clip_x0, sizeof keep_x0);
        memcpy(keep_x1, s_clip_x1, sizeof keep_x1);
        uint8_t *keep_px = s_px;
        int keep_ndark = s_ndark;
        s_px = s_layer_tmp;
        s_ndark = 0; /* la oscuridad pendiente es de la pantalla, no de la capa */
        for (int r = 0; r < nr; r++) {
            const int *q = rect[r];
            for (int y = 0; y < SCREEN_H; y++) {
                int in = y >= q[1] && y <= q[3];
                s_clip_x0[y] = (int16_t)(in ? q[0] : 1);
                s_clip_x1[y] = (int16_t)(in ? q[2] : 0);
            }
            clip_update_bounds();
            fb_fill_clip(0);
            draw(ctx);
            for (int y = q[1]; y <= q[3]; y++)
                layer_row(l, s_layer_tmp, y, q[0], q[2], 1);
        }
        s_px = keep_px;
        s_ndark = keep_ndark;
        memcpy(s_clip_x0, keep_x0, sizeof keep_x0);
        memcpy(s_clip_x1, keep_x1, sizeof keep_x1);
        clip_update_bounds();
    }
    /* La capa en la pantalla, dentro del recorte */
    for (int y = s_clip_by0; y <= s_clip_by1; y++) {
        int x0 = s_clip_x0[y], x1 = s_clip_x1[y];
        if (x0 > x1)
            continue;
        dark_cover(y, x0, x1);
        layer_row(l, s_px, y, x0, x1, 0);
    }
}
