#ifndef INPUT_H
#define INPUT_H

/* Los jugadores de una partida (8, como el original: en red, de varias máquinas) y los de una
   máquina (los joysticks; con dos, la pantalla partida) */
#define MAX_PLAYERS 8
#define MAX_LOCAL   2

typedef struct {
    float move_x, move_y; /* dirección en pantalla (-1..1, y hacia abajo) */
    int   sprint;         /* botón de correr apretado (R2 / gatillo derecho) */

    /* Eventos de un solo disparo: los consume el siguiente tick de lógica */
    int action;         /* A: atacar, levantar un objeto o hablar */
    int button[4];      /* A, B, X e Y: cada uno tira la habilidad de su casillero */
    int skill_tree;     /* abrir el árbol de habilidades (Select + Y; teclado: T / U) */
    int stats_page;     /* abrir los atributos (Select + X; teclado: G / H) */
    int potion_life;    /* LB: la primera poción de vida del cinturón */
    int potion_mana;    /* LT: la primera de maná */
    int potion_stamina; /* apretar el stick izquierdo (L3) */
    int automap;        /* R1 (o Tab): abrir o cerrar el automapa */
    int chat;           /* R3, o C para el 1 y N para el 2: la lista de frases (¡Ayuda!, Gracias...) */
    int belt;           /* casilla del cinturón a tomar (1 a 4; 0 = ninguna): cruceta ← ↑ → ↓, o 1-4 / 7-0 */
    int inventory;      /* Select, o Tab / I para el 1 y P para el 2 */
    int nav_up, nav_down, nav_left, nav_right; /* moverse en el inventario */
    int drop;           /* X, o Q para el 1 y M para el 2: tirar el objeto elegido */
    int back;           /* B, o Retroceso para el 1 y Suprimir para el 2: cerrar diálogos y tiendas */
    int held[4];        /* A, B, X e Y mantenidos ahora (no son eventos): para canalizar (Vincular demonio) */
    int always_run;     /* la opción "correr siempre" de su máquina (en red viaja con los controles) */
} PlayerInput;

/* El primer joystick que se usa maneja al jugador 1 y el segundo al jugador
   2 (lo que estaba apretado al conectarlo no cuenta hasta soltarlo: así un
   joystick que se ve como dos, con un botón trabado, no molesta). En el
   joystick se camina con el stick izquierdo, se corre con el gatillo derecho
   (RT / R2), R1 abre el automapa y la cruceta toma las pociones del
   cinturón (← 1, ↑ 2, → 3, ↓ 4; en los paneles, la cruceta los recorre). En
   la PC el teclado sirve para los dos: WASD + Shift izquierdo y 1-4 para el
   1, flechas + Shift derecho y 7-0 para el 2. Los botones A, B, X e Y son
   cuatro casilleros de habilidad (teclado: Espacio, E, R, F para el 1 y
   Enter, J, L, O para el 2). */
typedef struct {
    PlayerInput player[MAX_PLAYERS]; /* los joysticks llenan los primeros MAX_LOCAL; en red, todos los de la partida */

    /* El mouse controla al jugador 1 */
    int mouse_x, mouse_y;
    int mouse_down;

    /* Eventos de menú: vienen de cualquier teclado o joystick */
    int menu_up, menu_down, menu_left, menu_right;
    int menu_back;   /* B, Retroceso */
    int menu_delete; /* X, Suprimir */
    int confirm; /* Enter, Espacio o botón A */
    int start;   /* botón Start */
    int escape;  /* tecla Esc */
    int quit;    /* cerrar la ventana o Select + Start */
} InputState;

void input_poll(InputState *in);
int  input_any(const InputState *in); /* ¿se apretó o se movió algo? */
void input_clear_events(InputState *in);
int  input_num_pads(void);
void input_shutdown(void);

#endif
