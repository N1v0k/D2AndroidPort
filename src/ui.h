#ifndef UI_H
#define UI_H

#include <stdint.h>

/* Gráficos de la interfaz del juego original (data/global/ui/...). Los
   fondos y paneles grandes vienen partidos en pedazos de 256x256 que se
   arman en grilla (como los arma el juego). Todo se carga la primera vez. */

/* Dibuja el cuadro `frame` de ui/<path>.dc6 con su esquina en (x, y) */
void ui_frame(const char *path, int frame, int x, int y);
/* Igual, mezclado con el fondo (BLEND_* de video.h) */
void ui_frame_blend(const char *path, int frame, int x, int y, int blend);
/* Igual, achicado o agrandado a w x h; level < 31 lo oscurece */
void ui_frame_scaled(const char *path, int frame, int x, int y, int w, int h, int level);
/* Achicado a size x size y girado `angle` radianes en el plano, centrado en (cx, cy) */
void ui_frame_rotated(const char *path, int frame, int cx, int cy, int size, float angle);
/* Recorte de un cuadro: solo las filas desde `from_y` (para los orbes que se vacían) */
void ui_frame_rows(const char *path, int frame, int x, int y, int from_y);
/* Solo el rectángulo (sx, sy, w, h) del cuadro, dibujado con su esquina en (x, y) */
void ui_frame_part(const char *path, int frame, int x, int y, int sx, int sy, int w, int h);
/* `count` cuadros desde `first`, puestos en una grilla de `cols` columnas */
void ui_grid(const char *path, int first, int count, int cols, int x, int y);
int  ui_frame_size(const char *path, int frame, int *w, int *h); /* 0 si no existe */
int  ui_frame_count(const char *path);

/* El cuadro con su desplazamiento propio respecto de (ax, ay) (como el logo
   del menú); blend < 0 = sin mezcla */
void ui_frame_anchor(const char *path, int frame, int ax, int ay, int blend);

/* Cuadro negro translúcido con borde dorado fino, como los carteles del juego */
void ui_box(int x, int y, int w, int h);
/* Cuadro de descripción de un objeto: fondo negro semitransparente */
void ui_dark_box(int x, int y, int w, int h);

/* La pantalla de carga del original (la puerta que se abre, con su paleta)
   en el medio de la pantalla negra, y se muestra ya: `step` de `steps` */
void ui_loading(int step, int steps);

#endif
