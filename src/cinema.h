#ifndef CINEMA_H
#define CINEMA_H

/* Las cinemáticas del original, en calidad baja para la Pi: MPEG-1 de
   640x288 con sonido MP2 en data/global/video/<nombre>.mpg (las arma el
   importador a partir de los videos del Resurrected). Cuándo sale cada una,
   como en el original (QUESTS_ActChange y QUESTS_HellPortalActChange de
   D2Game, que le mandan al cliente el paquete 0x61 con el número):
   - new_bliz y bliznorth (los logos) y d2intro: al arrancar el juego.
   - act02start: la primera vez que se viaja al este con Warriv.
   - act03start: la primera vez que se navega al este con Meshif.
   - act04start: la primera vez que se cruza el portal rojo de Mefisto.
   - act04end: el último portal después de Diablo (A4Q2, el 5).
   - d2x_out: el portal de Tyrael después de Baal (A5Q6, el 7).
   - d2x_intro: nunca (el servidor no manda el 6); está siempre en el menú.
   El menú principal tiene "Cinemáticas" para volver a ver las que ya se
   vieron (en la carpeta de partidas, cinematicas.txt). */

void cinema_set_dir(const char *save_dir);

/* Pasa la cinemática entera, salvo que se apriete A, B, Start o Esc. 0 si
   terminó o se salteó, 1 si pidieron cerrar el juego, -1 si no está. */
int  cinema_play(const char *name);
int  cinema_exists(const char *name);

/* Las del menú, en orden */
int         cinema_count(void);
const char *cinema_name(int i);
const char *cinema_title(int i);
int         cinema_seen(int i);

/* D2_CINEMA_BENCH: decodifica una entera lo más rápido posible y dice
   cuántos cuadros por segundo da (para medir la Pi sin pantalla) */
int  cinema_bench(const char *name);

#endif
