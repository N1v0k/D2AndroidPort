#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* Texto en pantalla. Con las fuentes del juego cargadas (font_init_d2) usa
   las DC6 originales con acentos y los colores de texto de pal.pl2; si no,
   una fuente de 5x7 incorporada, solo con mayúsculas y sin acentos.

   `scale` elige el tamaño: 1 = font8, 2 = font16, 3 = font24, 4-5 = font30,
   6 o más = font42. El color es un índice de la paleta; con las fuentes del
   juego se usa el color de texto del original más parecido. El texto va en
   UTF-8. */
/* Además de los tamaños, las fuentes de los menús (Exocet), la de los
   textos de los NPC (Formal) y la más chica. La Exocet trae letras con
   relleno oscuro y borde claro: sobre fondo oscuro se rellena del color del
   texto (si no, se ve solo el contorno); FONT_EXOCET_ENGRAVED la deja como
   viene, grabada, para los botones de piedra del menú principal. */
enum {
    FONT_EXOCET = 10, FONT_EXOCET_SMALL = 11, FONT_FORMAL = 12, FONT_FORMAL_SMALL = 13, FONT_TINY = 14,
    FONT_EXOCET_ENGRAVED = 15,
};

int  font_init_d2(void); /* 0 si cargó las fuentes del juego */
int  font_width(const char *s, int scale);
int  font_height(int scale);
void font_draw(int x, int y, const char *s, int scale, uint8_t color);
void font_draw_centered(int cx, int y, const char *s, int scale, uint8_t color);
/* Cuántos bytes del principio de s entran en max_w píxeles (sin pasar de un
   salto de línea): corta en el último espacio que entra o, en chino, japonés
   y coreano, entre dos letras. El texto ya tiene que estar en el idioma de
   la pantalla. */
int  font_break(const char *s, int scale, int max_w);

#endif
