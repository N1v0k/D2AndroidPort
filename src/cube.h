#ifndef CUBE_MODULE_H
#define CUBE_MODULE_H

#include <stddef.h>

#include "items.h"

/* El Cubo Horádrico: las recetas de cubemain.txt (las gemas y las runas que
   suben de calidad, las pociones, los engarces, los mágicos que se vuelven
   a tirar, la artesanía...). Las del original que abren portales o son de
   misiones de otros actos no están (la del portal a las vacas sale en la
   guía, pero la hace game.c: cows.inc). */

#define CUBE_W 3
#define CUBE_H 4

int cube_init(void); /* carga cubemain.txt (después de items_init); cuántas recetas sirven */

/* Transmuta lo que hay en el cubo (in, n objetos) con el nivel del
   personaje: si es una receta, deja el resultado en out (devuelve cuántos;
   lo de entrada se consume entero) y su descripción en `what`; 0 si no es
   ninguna receta. */
int cube_transmute(const Item *in, int n, int clvl, Item *out, int max, char *what, size_t cap);

/* La guía de las recetas (Opciones, Guía del cubo): por categoría, cada
   receta con lo que lleva y lo que da, en palabras y con el objeto que se
   dibuja para cada cosa */
/* CUBE_CAT_RUNEWORDS no son recetas: las palabras rúnicas (se engarzan, no
   van al cubo), con lo de items_runeword_* */
enum { CUBE_CAT_HOWTO, CUBE_CAT_GEMS, CUBE_CAT_RUNES, CUBE_CAT_RUNEWORDS, CUBE_CAT_POTIONS, CUBE_CAT_SOCKETS,
       CUBE_CAT_MAGIC, CUBE_CAT_CRAFT, CUBE_CAT_UPGRADE, CUBE_CAT_OTHER, CUBE_CATS };
typedef struct {
    const ItemType *icon; /* NULL: cualquier objeto */
    int             qty;
    char            text[96];
} CubeLine;
const char *cube_category_name(int cat);
int         cube_recipe_count(void);
int         cube_recipe_category(int i);
/* Lo que lleva (devuelve cuántas líneas) y lo que da (out) */
int         cube_recipe_info(int i, CubeLine *in, int max_in, CubeLine *out);

#endif
