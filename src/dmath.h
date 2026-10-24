/* Las funciones de math.h que usa la simulación, calculadas igual en todas
   las máquinas.

   En red cada máquina simula la partida entera y tienen que dar los mismos
   bits: las de la biblioteca del sistema no los dan (sinf, cosf, atan2f y
   expf de Windows, y atan2f y expf de la Pi, difieren en el último bit de
   las de Linux). Estas solo suman, multiplican, dividen y sacan raíces en
   double, que redondean igual en todas (con -ffp-contract=off), y dan el
   float más cercano (sinf y cosf, salvo con ángulos de miles de millones de
   radianes). Están en dmath.c.

   Se incluye después de <math.h> en los archivos de la simulación: los
   nombres de siempre (sinf...) pasan a ser estos. */
#ifndef D2_DMATH_H
#define D2_DMATH_H

#include <math.h>

float d2_sinf(float x);
float d2_cosf(float x);
float d2_atan2f(float y, float x);
float d2_expf(float x);
float d2_hypotf(float x, float y);

#undef sinf
#undef cosf
#undef atan2f
#undef expf
#undef hypotf
#define sinf d2_sinf
#define cosf d2_cosf
#define atan2f d2_atan2f
#define expf d2_expf
#define hypotf d2_hypotf

#endif
