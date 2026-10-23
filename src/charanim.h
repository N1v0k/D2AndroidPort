#ifndef CHARANIM_H
#define CHARANIM_H

#include "fmt/formats.h"

/* Aspecto de un personaje, monstruo u objeto: carpeta ("chars", "monsters"
   u "objects"; vacía = "chars"), código y qué pieza lleva en cada capa
   ("lit", "med", "hvy", código de casco o escudo; "" = sin esa capa). */
typedef struct {
    char base[12];
    char token[4];
    char part[COMP_COUNT][4];
} CharLook;

/* Una animación (un modo + clase de arma) armada con las capas del COF */
typedef struct {
    Cof     cof;
    Sprite  layer[COMP_COUNT];
    int     speed;     /* de animdata.d2: 1/256 de cuadro por tick */
    int     hit_frame; /* cuadro en el que el ataque pega (evento 1 de animdata) */
    int8_t  blend[COMP_COUNT]; /* capas transparentes (los fantasmas): modo BLEND_* + 1, 0 = opaca */
    int     ok;
} CharAnim;

void charanim_set_animdata(const AnimData *ad);
int  charanim_load(CharAnim *a, const CharLook *look, const char *mode, const char *wclass);
void charanim_free(CharAnim *a);
int  charanim_frames(const CharAnim *a);
int  charanim_dirs(const CharAnim *a);
/* `sector` es la dirección real: 0..15 en sentido horario desde el sur
   (abajo en pantalla). El COF numera las direcciones en ese orden, pero
   los DCC usan otro, así que cada uno se traduce por separado. */
void charanim_draw(const CharAnim *a, int sector, int frame, int x, int y);
/* Igual, pasando las capas por una tabla de tonos (el azul del frío, el verde del veneno) */
void charanim_draw_tint(const CharAnim *a, int sector, int frame, int x, int y, const uint8_t *lut);
/* Lo mismo, con las piezas que tenga `over` (la misma animación con otras piezas) en lugar de las suyas */
void charanim_draw_over(const CharAnim *a, const CharAnim *over, int sector, int frame, int x, int y, const uint8_t *lut);
/* Solo las capas de `mask` (bits 1 << COMP_*), translúcidas (BLEND_*): las copias del arma de Hojas reflejadas */
void charanim_draw_ghost(const CharAnim *a, int sector, int frame, int x, int y, unsigned mask, int mode);

#endif
