#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/* Resolución de Lord of Destruction. Todo se dibuja en un framebuffer de
   8 bits con paleta, igual que el juego original: es barato para la Pi 3B. */
#define SCREEN_W 800
#define SCREEN_H 600

typedef struct {
    uint8_t  px[SCREEN_W * SCREEN_H];
    uint32_t pal[256]; /* ARGB8888 */
} Framebuffer;

extern Framebuffer g_fb;

/* Colores con nombre para la interfaz y los gráficos provisorios: 16 rampas
   de 16 tonos. Con la paleta de Diablo II cada uno se traduce al índice más
   parecido, así el mismo código sirve con las dos paletas. */
enum { R_GRAY, R_GRASS, R_DIRT, R_STONE, R_WOOD, R_SKIN, R_RED, R_BLUE, R_GOLD, R_CLOTH };
extern uint8_t g_col[256];
#define COL(r, s) (g_col[(r) * 16 + (s)])

/* Tablas de luz: g_light[nivel][color], nivel 0 = negro, 31 = sin cambio */
#define LIGHT_LEVELS 32
extern uint8_t g_light[LIGHT_LEVELS][256];

/* Colores de texto de pal.pl2: g_text_shift[color][píxel de la fuente].
   0 blanco, 1 rojo, 2 verde, 3 azul, 4 dorado, 5 gris, 6 negro, 7 beige,
   8 naranja, 9 amarillo, 10 verde oscuro, 11 violeta, 12 verde claro */
#define TEXT_COLORS 13
extern uint8_t  g_text_shift[TEXT_COLORS][256];

/* Mezclas de pal.pl2: g_blend[modo][sprite * 256 + fondo]. Los efectos de luz
   (explosiones, flechas de fuego) se suman al fondo; los paneles a medias
   dejan ver el juego. */
enum { BLEND_25, BLEND_50, BLEND_75, BLEND_ADD, BLEND_MUL, BLEND_COUNT };
extern uint8_t *g_blend[BLEND_COUNT];
extern uint32_t g_text_rgb[TEXT_COLORS];

int  video_init(int headless, int fullscreen, int scale);
void video_shutdown(void);
int  video_has_vsync(void);
void video_present(void);
double video_upload_ms(void); /* cuánto tardó la última subida del cuadro a la GPU */
void video_set_565(int on);    /* textura de 16 bits (más rápida en la Pi) o de 32 */
int  video_is_565(void);
int  video_save_bmp(const char *path);
void video_grab_argb(uint32_t *dst); /* el cuadro de ahora en ARGB (para grabar un video) */
void video_toggle_fullscreen(void);  /* F11 o Alt+Enter */

/* Cinemáticas: una textura YUV 4:2:0 de w x h que se dibuja en toda la
   ventana sin deformarse (video_movie_frame con y en NULL redibuja el
   último cuadro). video_movie_end vuelve a la pantalla del juego. */
int  video_movie_begin(int w, int h);
void video_movie_frame(const uint8_t *y, int ys, const uint8_t *u, int us, const uint8_t *v, int vs);
void video_movie_end(void);
void video_movie_shot(const char *path); /* guarda el próximo cuadro como BMP (D2_CINEMA_SHOT) */

void pal_init_placeholder(void);
int  pal_load_d2(const char *act); /* "act1".."act5"; 0 si cargó */
/* Los menús usan los colores de palette/units (sin tablas propias: las
   tablas de luz y de texto quedan las del acto) */
int  pal_load_menu(void);

/* Recorte por fila: cada línea de pantalla tiene un rango [x0, x1] visible.
   Sirve para partir la pantalla en dos con una línea en cualquier ángulo. */
void clip_reset(void);
void clip_half_plane(float cx, float cy, float nx, float ny, int side);
/* El rectángulo que ocupa el recorte actual; 0 si no queda nada visible */
int  clip_bounds(int *x0, int *y0, int *x1, int *y1);

void fb_clear(uint8_t c);
void fb_fill_clip(uint8_t c);
void fb_hline(int x0, int x1, int y, uint8_t c);
void fb_vline(int x, int y0, int y1, uint8_t c);
void fb_fill_rect(int x, int y, int w, int h, uint8_t c);
void fb_line(int x0, int y0, int x1, int y1, uint8_t c);
void fb_diamond(int cx, int cy, int hw, int hh, uint8_t fill, uint8_t edge);
void fb_block(int cx, int cy, int hw, int hh, int height, uint8_t top, uint8_t left, uint8_t right);
void fb_ellipse(int cx, int cy, int rx, int ry, uint8_t c);
void fb_shade_ellipse(int cx, int cy, int rx, int ry, int level);
void fb_shade_rect(int x, int y, int w, int h, int level); /* oscurece (fondos de paneles) */

/* Oscuridad de cuevas: todo se oscurece salvo alrededor de cada luz (lx, ly),
   cada una con su radio lr en píxeles (elíptico como el piso isométrico): los
   jugadores y lo que alumbra (proyectiles con Light, efectos con Radius).
   Solo afecta las filas 0..max_y. */
void fb_light_mask(const int *lx, const int *ly, const int *lr, int n, int max_y);
/* Al empezar cada cuadro: sin oscuridad pendiente (se aplica al convertirlo) */
void fb_frame_begin(void);

/* Una capa del tamaño de la pantalla que se guarda entre cuadros (el piso del
   mapa): al moverse la cámara solo se dibuja lo que entra. wx, wy: el píxel
   del mundo que va en la esquina de arriba a la izquierda de la pantalla.
   fb_layer_update llama a draw(ctx) con el recorte puesto en cada franja
   nueva (y el fondo en 0), como si dibujara en la pantalla, y después copia
   la capa a la pantalla dentro del recorte actual. Con redraw (el mapa
   cambió) la dibuja entera. */
typedef struct {
    uint8_t *px; /* de a pedazos: el píxel del mundo (wx, wy) va en (wx mod ancho, wy mod alto) */
    int      valid, wx, wy;
} FbLayer;
void fb_layer_update(FbLayer *l, int wx, int wy, int redraw, void (*draw)(void *ctx), void *ctx);
/* Banco de pruebas: convierte el cuadro (con su oscuridad) como para la GPU; ms */
double video_convert_bench(void);

/* Una de las 111 tablas de tonos de pal.pl2 (HueVariations), la que dice el
   colorshift de states.txt: 108 el frío, 104 el veneno. NULL si no hay. */
const uint8_t *fb_hue(int i);

/* Copia una imagen de 8 bits; el índice 0 es transparente. Con stride: un
   pedazo de una imagen más ancha (las filas de origen están a `stride`) */
void fb_blit_stride(const uint8_t *px, int stride, int w, int h, int x, int y);
/* Un pedazo (sx, sy, w, h) de un cuadro de ancho fw, por sus tramos opacos */
void fb_blit_spans_part(const uint8_t *px, const uint8_t *spans, int fw, int sx, int sy, int w, int h, int x, int y);
void fb_blit(const uint8_t *px, int w, int h, int x, int y);
/* Igual, pero copiando los tramos opacos (spans_build): mucho más rápido.
   Sin tramos (NULL) dibuja como fb_blit. */
void fb_blit_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y);

/* Usa la imagen como máscara: oscurece lo que hay debajo (sombras) */
void fb_blit_shade(const uint8_t *px, int w, int h, int x, int y, int level);
/* Dibujo achicado o agrandado a dw x dh; con level < LIGHT_LEVELS - 1 sale
   oscurecido con esa tabla de luz (íconos que todavía no se pueden usar) */
void fb_blit_scaled(const uint8_t *px, int w, int h, int x, int y, int dw, int dh, int level);
/* Escalado a size x size y girado `angle` radianes en el plano, centrado en (cx, cy) */
void fb_blit_rotated(const uint8_t *px, int w, int h, int cx, int cy, int size, float angle);
void fb_blit_blend(const uint8_t *px, int w, int h, int x, int y, int mode); /* BLEND_* */
void fb_blit_shade_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, int level);
void fb_blit_blend_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, int mode); /* por tramos */
void fb_blend_rect(int x, int y, int w, int h, uint8_t color, int mode);       /* tinte translúcido */
void fb_brighten_rect(int x, int y, int w, int h, int level);                 /* aclara (1 o 2) */

/* Copia pasando cada píxel por una tabla (colores de texto, tintes) */
void fb_blit_remap(const uint8_t *px, int w, int h, int x, int y, const uint8_t lut[256]);

/* Dibuja la silueta de la imagen en un solo color */
void fb_blit_mask(const uint8_t *px, int w, int h, int x, int y, uint8_t color);
/* Lo mismo que fb_blit_mask y fb_blit_remap, recorriendo los tramos opacos (spans_build) */
void fb_blit_mask_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, uint8_t color);
void fb_blit_remap_spans(const uint8_t *px, const uint8_t *spans, int w, int h, int x, int y, const uint8_t lut[256]);

#endif
