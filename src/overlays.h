#ifndef OVERLAYS_H
#define OVERLAYS_H

/* Efectos que se dibujan encima de algo (overlay.txt, gráficos de
   data/global/overlays): el destello de Porrazo, las estrellas de Aturdir,
   el aura de Concentración, la marca de Grito de batalla... Siguen a un
   monstruo o a un jugador, o quedan en un punto. */

enum { OV_AT_POINT, OV_AT_UNIT, OV_AT_PLAYER };

int  overlays_init(void);               /* lee overlay.txt; 0 si salió bien */
void overlays_preload(const char *name); /* carga el gráfico ya (para no trabar al usarlo) */

/* Uno de overlay.txt sobre `at` (OV_AT_UNIT: el monstruo `id`, OV_AT_PLAYER:
   el jugador `id`, OV_AT_POINT: el punto x, y). Con `ticks` > 0 se repite
   ese tiempo (el de un estado: si ya estaba, se alarga sin reiniciarse); con
   0 se ve la animación una vez. Los de 1ofN eligen uno de los N siguientes. */
void overlays_add(const char *name, int at, int id, float x, float y, int ticks);
void overlays_tick(void);
/* Saca los que empiezan con `prefix` de ese monstruo o jugador (la maldición anterior) */
void overlays_remove(const char *prefix, int at, int id);
void overlays_clear(void);

/* Dónde está ahora lo que sigue cada uno: 0 si ya no está (se saca) */
typedef int (*OverlayPos)(int at, int id, float *x, float *y);
/* Los que alumbran (Radius de overlay.txt), en el mundo: cuántos puso */
int  overlays_lights(float *x, float *y, int *radius, int max, OverlayPos pos);
/* Los dibuja con la cámara en (cam_x, cam_y) de la pantalla isométrica;
   los de atrás (PreDraw: el círculo de las auras) van sobre el piso, antes
   que los personajes y las paredes */
void overlays_draw(float cam_x, float cam_y, OverlayPos pos);
void overlays_draw_back(float cam_x, float cam_y, OverlayPos pos);

#endif
