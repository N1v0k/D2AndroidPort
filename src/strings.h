#ifndef STRINGS_H
#define STRINGS_H

#include <stddef.h>

/* Textos del juego traducidos, de los .json de data/local/lng/strings del
   Resurrected (por ejemplo "Blood Moor" -> "Páramo Sangriento").

   El Resurrected marca el género gramatical: los sustantivos llevan su
   género adelante ("[fs]Hacha de mano") y los adjetivos traen las cuatro
   formas ("[ms]roído[fs]roída[mp]roídos[fp]roídas"). */

enum { GENDER_MS, GENDER_FS, GENDER_MP, GENDER_FP, GENDER_COUNT };

int         strings_load(const char *lang); /* "esMX", "enUS", ...; 0 si cargó algo */
const char *str_get(const char *key);       /* la traducción (sin marcas), o la clave si no hay */
int         str_exists(const char *key);    /* 1 si la clave tiene un texto */
/* Archivo con la voz de un texto de vo.json (data/local/sfx/act1/kashya/...);
   0 si tiene una */
int         str_voice_path(const char *key, char *out, size_t cap);
int         str_gender(const char *key);    /* género de un sustantivo (GENDER_MS si no dice) */
const char *str_get_gender(const char *key, int gender); /* la forma de un adjetivo para ese género */
/* Arma un nombre con un formato del juego (MagicFormat, RareFormat,
   ChampionFormat...): "%0 %1 %2" en inglés, "a0n1:%1 %0 %2" en español (el
   orden de cada idioma). Los vacíos no dejan espacios de más. */
void        str_compose(char *out, size_t cap, const char *fmt_key, const char *a0, const char *a1, const char *a2);

#endif
