#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Lee un archivo del juego, por ejemplo "data/global/palette/act1/pal.dat".
   La ruta se pasa a minúsculas (así quedaron al extraerlas). Devuelve un
   buffer que hay que liberar con free(), o NULL si no existe. */
uint8_t *fs_read(const char *path, size_t *size);

/* Abre un archivo del juego para leerlo de a pedazos (con la misma ruta
   que fs_read). NULL si no existe. */
FILE *fs_open(const char *path);

/* Carpeta donde está "data/" (por defecto la carpeta actual) */
void fs_set_root(const char *root);

/* Busca en `dir` el primer archivo que termina en `suffix` y devuelve su
   ruta relativa (como la usa fs_read). 0 si encontró uno. */
int fs_find(const char *dir, const char *suffix, char *out, size_t cap);

/* Pide al sistema que lea por adelantado (a su caché, en segundo plano) los
   archivos de `dir` y sus subcarpetas: las baldosas de un acto. En la Pi
   leer de la tarjeta SD era lo que más tardaba al armar una zona nueva (el
   desierto, unos 350 ms). Solo en Linux; en otros sistemas no hace nada. */
void fs_prefetch_dir(const char *dir);

#endif
