#ifndef MENU_H
#define MENU_H

#include "input.h"

typedef enum {
    MENU_NONE,
    MENU_START, /* ya eligieron: menu_players / menu_slots / menu_classes */
    MENU_QUIT,
    MENU_MOVIE, /* ver la cinemática menu_movie() y volver al menú */
    MENU_RESTART /* otro idioma (menu_language): se guarda y el juego vuelve a arrancar */
} MenuResult;

void       menu_preload(void); /* al arrancar, con el hilo de trabajos */
void       menu_open(void);
MenuResult menu_tick(const InputState *in);
void       menu_render(void);
int        menu_players(void);
const int *menu_slots(void);   /* partida de cada jugador (1..6) */
const int *menu_classes(void); /* clase de los personajes nuevos */
int        menu_difficulty(void); /* la dificultad elegida (0 Normal, 1 Pesadilla, 2 Infierno) */
int        menu_terror_zones(void); /* las zonas de terror: 1 sí (se abren al vencer a Baal en esa dificultad) */
int        menu_is_main(void);   /* en la pantalla principal (para la demostración) */
const char *menu_movie(void);    /* la cinemática elegida (MENU_MOVIE) */
const char *menu_language(void); /* el idioma elegido (MENU_RESTART): "deDE"... */
int        menu_mode(void);      /* lo que se eligió jugar (MENU_START): 0 la historia, 1 el Eco del Odio */

#endif
