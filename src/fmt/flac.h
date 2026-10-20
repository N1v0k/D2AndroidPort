#ifndef FLAC_H
#define FLAC_H

/* Decodificador de FLAC (los sonidos y la música del Resurrected).
   Soporta todo lo que usa el formato: bloques fijos y variables,
   predictores fijos y LPC, Rice con escape y estéreo izquierda/lado,
   lado/derecha y medio/lado. No verifica los CRC. */

#include <stddef.h>
#include <stdint.h>

typedef struct Flac Flac;

/* Abre un archivo para leerlo de a pedazos (la música). La ruta es como la
   de fs_read: relativa a la carpeta de los datos. NULL si no se pudo. */
Flac *flac_open(const char *path);
/* Desde memoria; `data` tiene que vivir hasta flac_close */
Flac *flac_open_mem(const uint8_t *data, size_t size);
void  flac_close(Flac *f);

int      flac_rate(const Flac *f);
int      flac_channels(const Flac *f);
uint64_t flac_total(const Flac *f); /* cuadros (muestras por canal); 0 si no se sabe */
const uint8_t *flac_md5(const Flac *f); /* MD5 del audio según STREAMINFO */

/* Lee hasta `frames` cuadros en 16 bits intercalados (L, R, L, R...).
   Devuelve cuántos leyó: 0 al final del archivo o si está roto. */
int flac_read(Flac *f, int16_t *out, int frames);
int flac_rewind(Flac *f); /* vuelve al principio; 0 si salió bien */

/* Decodifica un archivo entero en memoria. Devuelve las muestras
   intercaladas (liberar con free) o NULL. */
int16_t *flac_decode(const uint8_t *data, size_t size, int *frames, int *channels, int *rate);

#endif
