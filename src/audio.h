#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* Sonido: los efectos de sounds.txt (FLAC de data/global/sfx), la música
   de cada zona y el ambiente de soundenviron.txt. Los efectos con posición
   suenan más fuerte cerca de los jugadores y del lado de la pantalla en
   que están. */

#define AUDIO_RATE  44100
#define AUDIO_NOPOS (-1.0e9f) /* sin posición: interfaz, voces del jugador */

enum {
    AUDIO_OFF,    /* no suena nada (pruebas sin ventana) */
    AUDIO_DEVICE, /* la placa de sonido, con SDL */
    AUDIO_MANUAL, /* se mezcla con audio_render (para grabar un WAV en las pruebas) */
};
int  audio_init(int mode); /* 0 si salió bien; si falla, el juego sigue sin sonido */
void audio_shutdown(void);

int  audio_find(const char *name); /* fila de sounds.txt, o -1 */
void audio_preload(int sound);     /* decodifica antes (el grupo entero) para no trabar al jugar */
int  audio_loops(int sound);       /* 1 si se repite hasta que se lo corta (Loop) */

/* Toca un sonido en (x, y) (subtiles) o en AUDIO_NOPOS. Si el sonido es un
   grupo (Group Size), elige uno al azar. Devuelve un número para
   audio_stop/audio_move, o 0 si no sonó. */
int  audio_play(int sound, float x, float y);
int  audio_play_vol(int sound, float x, float y, int volume); /* volumen 0-255 en vez del de la tabla */
int  audio_play_name(const char *name, float x, float y);
void audio_stop(int handle); /* con el Fade Out de la tabla */
void audio_stop_all(void);   /* corta todos los efectos (al volver al menú) */
void audio_move(int handle, float x, float y);

/* Posición de los jugadores (se oye desde el más cercano al sonido) */
void audio_listeners(int n, const float *x, const float *y);

/* Música y ambiente de una zona: fila de soundenviron.txt (0 = silencio) */
void audio_environment(int env);
void audio_music(const char *name);       /* música suelta (el menú); NULL = silencio */
void audio_music_event(const char *name); /* tema de una misión: suena una vez y vuelve la de la zona */

/* Voz de un NPC (vo.json): se lee de a pedazos. Devuelve cuánto dura en
   ticks, o 0 si no está el archivo. Una nueva corta a la anterior. */
int  audio_voice(const char *path);
void audio_voice_stop(void);

void audio_tick(void); /* una vez por tick: fundidos, ambiente y volumen según la distancia */
size_t audio_memory(void); /* bytes en sonidos decodificados (se guardan hasta 24 MB) */

void audio_set_volume(int sfx, int music); /* de 0 a 10 */
void audio_get_volume(int *sfx, int *music);

void audio_render(int16_t *out, int frames); /* estéreo intercalado; solo en AUDIO_MANUAL */

/* El sonido de una cinemática (cinema.c): mientras está abierto lo demás
   queda en pausa. Se le pasa estéreo intercalado (-1..1) a AUDIO_RATE;
   devuelve cuántos cuadros entraron (el resto, la próxima vez).
   audio_movie_begin devuelve 1 si hay placa de sonido. */
int  audio_movie_begin(void);
int  audio_movie_push(const float *lr, int frames);
int  audio_movie_queued(void); /* cuadros que faltan sonar */
void audio_movie_end(void);

#endif
