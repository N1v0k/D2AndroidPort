#ifndef CONTROLS_H
#define CONTROLS_H

#include "input.h"

/* La pantalla de controles (desde el menú principal y la pausa): en una
   solapa el joystick dibujado con lo que hace cada botón, en la otra el
   teclado de los dos jugadores. Izquierda / derecha (o LB / RB) cambian de
   solapa; B, Esc o Start la cierran. */
void controls_open(void);
void controls_open_at(int tab); /* 0 el joystick, 1 el teclado */
int  controls_tick(const InputState *in); /* 0 cuando se cierra */
void controls_draw(void);                 /* encima de lo que haya (lo oscurece) */

#endif
