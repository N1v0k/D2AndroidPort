#ifndef MENU_H
#define MENU_H

#include "input.h"

typedef enum {
    MENU_NONE,
    MENU_START, /* ya eligieron: menu_players / menu_slots / menu_classes */
    MENU_QUIT
} MenuResult;

void       menu_open(void);
MenuResult menu_tick(const InputState *in);
void       menu_render(void);
int        menu_players(void);
const int *menu_slots(void);   /* partida de cada jugador (1..6) */
const int *menu_classes(void); /* clase de los personajes nuevos */
int        menu_is_main(void);   /* en la pantalla principal (para la demostración) */

#endif
