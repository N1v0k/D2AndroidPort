#ifndef TOUCH_H
#define TOUCH_H

#include <SDL.h>
#include "input.h"

/* Controles táctiles (Android), como los de D2Android: un joystick virtual de SDL que se maneja con los dedos
   (el stick flotante en la mitad izquierda, A grande abajo a la derecha con B, X e Y en arco y el ícono de su
   habilidad, las pociones, una fila de menú arriba a la derecha y la cruceta chica arriba a la izquierda), así
   el resto del juego lo ve como un control más. Tocar el dibujo del juego camina hacia ahí, como mantener
   apretado el botón del mouse en la PC. Al usar un control de verdad se esconden; tocar la pantalla los vuelve a
   mostrar. En la PC, con D2_TOUCH=1 (para probarlos). */
void touch_init(void);
int  touch_enabled(void);
int  touch_event(InputState *in, const SDL_Event *e); /* los SDL_FINGER*: 1 si era de los controles táctiles */
void touch_update(InputState *in);                    /* después de los eventos: el joystick virtual y el mouse */
void touch_pad_used(SDL_JoystickID id);               /* se usó un joystick: si es de verdad, se esconden */
int  touch_is_pad(SDL_JoystickID id);                 /* ¿es el joystick virtual de los controles táctiles? */
void touch_draw(SDL_Renderer *r);                     /* encima del cuadro, antes de presentarlo */
void touch_shutdown(void);

#endif
