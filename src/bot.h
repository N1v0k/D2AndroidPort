#ifndef BOT_H
#define BOT_H

#include "map.h"

#include <stdint.h>

/* Navegación para los personajes que maneja la máquina: la prueba
   automática y lo que venga (compañeros, jugadores de prueba...). Busca el
   camino con A* por subtiles donde entra el cuerpo (con el radio de un
   jugador, como choca al caminar), solo dentro de las zonas que hacen falta,
   lo sigue en línea recta mientras se pueda, pasa de una zona a otra
   (pasillos, bocas de cueva, escaleras y portales) y se destraba solo.

   Uso: una BotNav por personaje; en cada tick bot_nav_step con su posición
   y el destino, y caminar en la dirección que devuelve. Una búsqueda larga
   se reparte en varios ticks (unos miles de subtiles por tick), así que
   varios bots a la vez no traban el juego. */

#define BOT_RADIUS 0.3f

typedef struct BotNav BotNav;

enum {
    BOT_ARRIVED, /* ya llegó (a menos de `reach` del destino) */
    BOT_MOVING,  /* caminar hacia *dx, *dy (dirección del mundo, sin normalizar) */
    BOT_USE,     /* está al lado de una escalera o un portal: apretar el botón de acción */
    BOT_NO_PATH, /* no hay camino (ni por otras zonas) */
};

BotNav *bot_nav_new(uint32_t seed);
void    bot_nav_free(BotNav *n);
void    bot_nav_reset(BotNav *n); /* olvida el camino (por ejemplo, después de saltar de zona) */

/* Un paso hacia (x, y) de la zona `zone` (-1 = la de ese punto). `tick` es
   el reloj del juego: el camino se rehace si cambia el destino, las zonas
   armadas, si se desvía o cada tanto. */
int bot_nav_step(BotNav *n, const Map *m, uint32_t tick, float px, float py, int zone, float x, float y, float reach,
                 float *dx, float *dy);

/* Para medir: cuántas búsquedas hizo, cuántos subtiles exploró en total y
   cuánto tardaron (en total y lo más que tardó en un solo tick, en ms) */
void bot_nav_stats(const BotNav *n, int *plans, long *expanded);
void bot_nav_timing(const BotNav *n, double *total_ms, double *worst_ms);

#endif
