#ifndef GAME_H
#define GAME_H

#include "input.h"

#include <stddef.h>

/* La lógica del original corre a 25 cuadros por segundo; el dibujo interpola. */
#define TICK_RATE 25

enum { GAME_CONTINUE, GAME_EXIT };

int  game_init(void); /* carga los archivos del juego; 0 si salió bien */
void game_start(int num_players); /* pruebas: Bárbaro en la partida 1 y Amazona en la 2 (D2_CLASSES="2,1" otras) */
/* Arranca con la partida `slots[i]` (1..6) de cada jugador; si está vacía,
   crea un personaje nuevo de la clase `classes[i]` (0 = Bárbaro, 1 = Amazona) */
void game_start_slots(int num_players, const int *slots, const int *classes);
/* La dificultad de la partida que sigue (0 Normal, 1 Pesadilla, 2 Infierno),
   antes de game_start_slots */
void game_set_difficulty(int diff);
void game_set_terror_zones(int on); /* las zonas de terror (con Baal muerto en esa dificultad) */
void game_set_mode(int mode); /* lo que se juega con game_start_slots: 0 la historia, 1 el Eco del Odio */
void game_set_language(const char *code); /* el idioma elegido en el menú (opciones.txt; se usa al arrancar) */
int  game_difficulty(void);
int  game_tick(const InputState *in);
void game_render(float alpha);
void game_bench_parts(char *out, size_t cap); /* el desglose del dibujo desde la última vez (y lo reinicia) */
/* Rendimiento medido por main (cada segundo), para la opción VER RENDIMIENTO */
void game_set_perf(float fps, float logic_ms, float worst_ms, float draw_ms, float upload_ms, float present_ms);
int  game_wants_rgb565(void); /* la opción COLOR 16 BITS de la pausa */
/* La cinemática de fin de acto que toca pasar ("act02start"...) o NULL;
   la deja en NULL. Las pasa main.c (cinema.c), no los bots ni las pruebas. */
const char *game_take_movie(void);
void game_print_status(void); /* depuración */

/* Prueba automática ("act1"): un bot juega el acto con los controles.
   game_autotest_input llena los controles de cada tick y devuelve 1
   mientras sigue, 0 si terminó bien y -1 si falló. */
int  game_autotest_start(const char *name, int players);
int  game_autotest_input(InputState *in, double last_tick_ms);
void game_autotest_render_time(double ms);
int  game_debug_fx(int *kind); /* depuración: cuánto lleva el efecto de un jefe (-1 = ninguno) */
int  game_debug_obj(int *kind); /* depuración: hace cuánto el J1 usó un cofre, barril... (-1 = nada); kind = su OperateFn */
int  game_debug_round_tick(void); /* depuración: el tick de la ronda de la prueba de habilidades (-1 = no hay) */
void game_autotest_report(void);

/* Demostración (el menú quieto un rato): el bot de la prueba juega el Acto 1
   con un personaje nuevo de la clase `cls` que no se guarda; no muere, pero
   pelea de verdad. Los controles los pone game_autotest_input. Lo que le
   pasa queda en el log, para ver errores. */
void game_demo_start(int cls);
void game_demo_start_pair(int cls1, int cls2); /* con esas dos clases (la segunda acompaña) */
void game_demo_stop(void);
int  game_demo_acts(void); /* los actos que juega (la campaña: 15; con D2_DEMO_ACT, 1) */
/* La rotación de la demostración del menú: la pareja número `i` (vuelve a
   empezar sola): primero el Brujo y el Paladín, después cada clase con la que sigue */
void game_demo_rotation(int i, int *cls1, int *cls2);
int  game_demo_rotation_count(void);

/* Partidas guardadas: seis lugares (slot1.sav ... slot6.sav) en `dir`
   (NULL o "" = no se guarda) */
#define SAVE_SLOTS  6
#define NUM_CLASSES 8 /* 0 Bárbaro, 1 Amazona, 2 Hechicera, 3 Paladín, 4 Nigromante, 5 Asesina, 6 Druida, 7 Brujo */
typedef struct {
    int used;
    int cls;
    int level;
    int gold;
    int den_done;
    int diff_max; /* la dificultad más alta abierta (0 Normal, 1 Pesadilla, 2 Infierno) */
    int eco_best; /* la mejor oleada en el Eco del Odio (0 = nunca jugó) */
} SlotInfo;

void        game_set_save_dir(const char *dir);
void        game_save(void);                     /* guarda a los que están jugando */
int         game_slot_info(int slot, SlotInfo *info); /* 1 si hay un personaje */
void        game_delete_slot(int slot);
const char *game_class_name(int cls);
const char *game_class_voice(int cls); /* "barbarian", "amazon", "sorceress": sus sonidos */
/* El personaje de la partida (con su equipo) o el de la clase, quieto en (x, y) */
void        game_draw_slot_hero(int slot, int x, int y, int tick);
void        game_draw_class_hero(int cls, int x, int y, int tick);
void        game_previews_changed(void); /* se borró o se creó una partida */

#endif
