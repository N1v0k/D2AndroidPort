#ifndef ITEMSTATS_H
#define ITEMSTATS_H

#include <stddef.h>
#include <stdint.h>

/* Las propiedades de los objetos como en el original: los números son las
   filas de ItemStatCost.txt (*ID) y cada propiedad de las tablas (los
   afijos, los únicos, los conjuntos, las runas, las gemas) se convierte en
   esas con las funciones de Properties.txt (ITEMMODS_AssignProperty de
   D2MOO). Los textos salen de las claves de ItemStatCost (descfunc,
   descstrpos, dgrp) con los de item-modifiers.json. */

#define ISC_MAX 400

/* Una propiedad puesta: la estadística, su parámetro (la clase, la solapa,
   la habilidad) y el valor */
typedef struct {
    uint16_t stat;
    int16_t  param;
    int32_t  value;
} ItemProp;

struct ItemType;

int  itemstats_init(void);
int  itemstats_find(const char *name);     /* *ID por el nombre ("fireresist"); -1 si no está */
int  itemstats_has_property(const char *code); /* el código existe en Properties.txt */

/* Aplica una propiedad de las tablas (código, parámetro, mínimo y máximo)
   a un objeto de ese tipo y nivel: agrega las estadísticas a out[] (sumando
   las que ya estaban) y devuelve cuántas hay ahora. `sockets` recibe los
   engarces de la propiedad "sock" (o queda igual). */
int  itemstats_apply(const struct ItemType *t, int ilvl, const char *code, const char *par, int min, int max,
                     ItemProp *out, int count, int cap, int *sockets);
/* Cómo se tiran los valores de itemstats_apply: -1 el mínimo, 1 el máximo y
   0 al azar (lo normal). Para mostrar los rangos de una palabra rúnica. */
void itemstats_roll_mode(int mode);
/* Suma una estadística (o la agrega); devuelve la cantidad nueva */
int  itemstats_add(ItemProp *out, int count, int cap, int stat, int param, int value);

/* Las que suben con el nivel del personaje (op 2, 4 y 5 de ItemStatCost):
   a qué estadística van (*ID; 16 y 17 = % de defensa y de daño) y cuánto
   suman con ese nivel en `amount`; -1 si no es de esas */
int  itemstats_per_level(int stat, int value, int clvl, int *amount);

/* Las líneas que se muestran, en el orden del juego (descpriority), con
   las que van juntas (el daño de un elemento, todas las resistencias...).
   clvl: el nivel del personaje (las que suben con el nivel). */
int  itemstats_describe(const ItemProp *p, int n, int clvl, char lines[][112], int max_lines);

/* El formato de los textos del juego: %d, %+d, %i, %s, %% y los
   posicionales del español (%0, %1...). args: enteros o textos. */
typedef struct {
    int         i;
    const char *s;
} FmtArg;
void itemstats_format(char *buf, size_t cap, const char *fmt, const FmtArg *args, int nargs);

/* Las clases del original en las propiedades (item_addclassskills,
   item_addskill_tab): 0 Amazona, 1 Hechicera, 2 Nigromante, 3 Paladín, 4
   Bárbaro, 5 Druida, 6 Asesina */
int  itemstats_class_from_d2(int d2cls); /* -> la del motor (0 Bárbaro, 1 Amazona...); -1 si no */
int  itemstats_class_to_d2(int cls);

#endif
