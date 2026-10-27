#ifndef COMPAT_H
#define COMPAT_H

#include <stdio.h>

/* Lo que cambia entre Linux (la Pi, la PC) y Windows */

int   compat_mkdir(const char *path);                   /* 0 = la creó o ya estaba */
int   compat_replace(const char *from, const char *to); /* renombra pisando el destino (0 = bien) */
FILE *compat_tmpfile(void);                             /* para leer y escribir, binario; se borra al cerrarlo */

/* Al arrancar: en Windows, el juego trabaja en la carpeta del .exe (data/, save/) y, si no tiene
   consola (doble clic), lo que escribe va a d2port.log, como en la Pi */
void compat_start(void);

/* Volver a arrancar el juego (otro idioma): en Android lo hace la actividad (0 = quedó pedido); en el resto,
   -1 y el juego se vuelve a ejecutar solo */
int compat_restart(void);

/* Android: si faltan los archivos del juego, un cartel con dónde copiarlos (-1); 0 si están (o no es Android) */
int compat_check_data(void);

#endif
