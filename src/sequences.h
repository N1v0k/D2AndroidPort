#ifndef SEQUENCES_H
#define SEQUENCES_H

#include <stdint.h>

/* Las secuencias de animación de las habilidades (Estocada, Torbellino,
   Doble golpe, Salto...): las del original, sacadas de D2MOO
   (tools/gen_sequences.py). Cada paso muestra un cuadro de un modo (el
   primer ataque, el segundo, lanzar, los especiales) y algunos marcan el
   golpe. */

enum { SEQ_A1, SEQ_A2, SEQ_SC, SEQ_TH, SEQ_S1, SEQ_S3, SEQ_KK, SEQ_S4 };
enum { SEQ_EV_NONE, SEQ_EV_HIT, SEQ_EV_SOUND };

typedef struct {
    uint8_t mode, frame, event;
} SeqFrame;

/* La secuencia `seqnum` de skills.txt con esa clase de arma ("hth", "1hs",
   "bow"...); NULL (y count 0) si con esa arma no hay */
const SeqFrame *seq_get(int seqnum, const char *wclass, int *count);

#endif
