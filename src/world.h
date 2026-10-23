#ifndef WORLD_H
#define WORLD_H

#include "map.h"

#include <stdint.h>

#define LEVEL_MAX_MONSTERS 10

/* Un área del mundo con nombre (pueblo, zona exterior, piso de un
   calabozo), en tiles. El nombre es la clave de levels.json; se traduce al
   mostrarlo. Sus datos salen de levels.txt. */
typedef struct {
    const char *name;
    int         index;     /* número de nivel */
    int         zone;      /* zona que lo arma (ver abajo) */
    int         x0, y0, x1, y1;
    int         town;      /* en el pueblo no hay monstruos ni se gasta estamina */
    int         dark;      /* calabozos: solo se ve alrededor de los jugadores */
    /* Lo de la dificultad (world_set_difficulty) */
    int         mon_level; /* nivel de los monstruos (levels.txt, MonLvl; MonLvlEx(N) en Pesadilla) */
    const char *monsters[LEVEL_MAX_MONSTERS]; /* Id de monstats.txt (mon1..mon5; nmon1..nmon10 en Pesadilla e Infierno) */
    int         num_mon;   /* cuántos de esos salen en cada partida (NumMon) */
    const char *umons[10]; /* los que pueden salir de jefe al azar (umon1..umon10 en Normal; en las otras, los de monsters) */
    int         num_umon;
    int         umin, umax; /* cuántos jefes tiene el nivel (MonUMin..MonUMax de la dificultad) */
    int         sound_env; /* música y ambiente (levels.txt SoundEnv -> soundenviron.txt) */
    int         groups;    /* grupos de monstruos al azar */
    int         waypoint;  /* número del waypoint del nivel (levels.txt Waypoint), -1 = no tiene */
    int         pal;       /* su paleta (Pal de levels.txt: 0..4 = act1..act5; los infiernos del Acto 5 usan la 3) */
    int         id;        /* levels.txt Id */
    int         vis[8];    /* adónde lleva cada estilo de salto (levels.txt Vis0..7, Id de nivel; 0 = nada) */
    int         noint;     /* los estilos cuyo salto es una trampilla (lvlwarp.txt NoInteract): se usan con A */
    uint8_t     objgrp[8], objprb[8]; /* los grupos de objetos al azar (ObjGrp0..7 de objgroup.txt) y su % por sala */
} Level;

/* Una zona es lo que se arma y se borra junto (ver game.c): casi siempre un
   nivel, pero el Monasterio con el Claustro Exterior y el Claustro Interior
   con la Catedral van juntos porque se pasa caminando de uno al otro. La
   forma de cada zona sale siempre igual en la misma partida. */

/* Un salto entre zonas: acercarse al rectángulo (la boca de la cueva, una
   escalera) lleva a la entrada que le corresponde en la otra. Los de pisar
   (margin 0) se toman al tocarlos: a WARP_TOUCH subtiles del rectángulo. */
#define WARP_TOUCH 1.0f
typedef struct {
    float x0, y0, x1, y1; /* en subtiles */
    float margin;         /* las escaleras están en la pared: se toman desde un poco antes (y con A) */
    int   from, to;       /* zonas */
    int   portal;         /* portal rojo (lo abre una misión): llega a (ax, ay) */
    int   fixed_margin;   /* el margen lo puso quien armó la zona (la puerta de la torre) */
    float ax, ay;
    int   pair;           /* cuál de los saltos de su zona a la misma otra es (se llega al del mismo número) */
} Warp;

/* Un paso caminando entre dos zonas vecinas (el pasillo entre los bosques):
   al acercarse hay que tener armada la otra */
typedef struct {
    int   a, b;
    float x, y;  /* el medio del pasillo, en subtiles */
    int   cross; /* con las dos armadas: 1 el punto está sobre un cruce que entra a las dos, -1 no lo hay,
                    -2 no está sobre un borde que compartan (no se mira); 0 falta mirar */
} WorldLink;

/* El Acto 1 en un solo mundo. world_layout solo decide dónde va cada zona
   (no carga nada). */
int          world_layout(Map *m, uint32_t seed, int act); /* el mundo de ese acto (1 o 2) */
int          world_act(void);
int          world_staff_tomb(void);   /* el Id (66..72) de la tumba verdadera de Tal Rasha en esta partida */
int          world_level_zone(int id); /* la zona del nivel con ese Id de levels.txt, -1 = no está */
int          world_act2_spot(int what, float *x, float *y); /* 0 portal del sótano 3, 1 portal del Santuario, 2 llegada al Canyon */
int          world_cow_arrival(float *x, float *y); /* donde llega el portal a la Granja Muu Muu (-1 = no es el Acto 1) */
void         world_set_difficulty(int diff); /* los monstruos de los niveles (0 Normal, 1 Pesadilla, 2 Infierno) */
int          world_zone_count(void);
int          world_zone_build(Map *m, int zone); /* deja en m->objects lo que traen sus DS1 */
/* Lo mismo de a partes: estampa las piezas que entren en budget_ms (0 =
   todas) y, en la llamada que sigue a la última, la colisión. 1 = armada,
   0 = falta, -1 = error. Una zona a la vez: pedir otra termina la de antes. */
int          world_zone_build_step(Map *m, int zone, double budget_ms);
int          world_zone_building(void); /* la que está a medio armar, o -1 */
void         world_zone_free(Map *m, int zone);
int          world_zone_loaded(int zone);
/* Los DT1 que puede pedir la zona al armarse (las del Acto 3, de piezas de
   lvltypes.txt), para prepararlos antes; 0 = no se sabe */
int          world_zone_dt1(int zone, char paths[34][96]);
int          world_zone_is_town(int zone);
/* Las salas de una zona recién armada, en tiles (x, y, ancho, alto): en los
   calabozos, cada pieza de la grilla; afuera, celdas de 8x8. Ahí se reparten
   los objetos (cofres, barriles) como OBJECTS_PopulationHandler. */
int          world_zone_rooms(int zone, int (*rect)[4], int max);
const char  *world_zone_name(int zone);
int          world_zone_at(const Map *m, float x, float y); /* dueña del lugar (armada o no), -1 = vacío */

const Level *world_level_at(float x, float y); /* en subtiles; NULL fuera de todo */
int          world_level_count(void);
const Level *world_level(int i);
const Level *world_level_named(const char *name);
int          world_waypoint_zone(int wp);       /* zona de ese waypoint, -1 = no está */

const Warp  *world_warp_at(float x, float y);   /* solo los de zonas armadas */
int          world_warp_count(void);
const Warp  *world_warp(int i);
int          world_warp_arrival(const Warp *w, float *x, float *y); /* 0 si encontró la llegada */
/* El portal o la escalera (los que se usan con el botón) que se usaría desde
   (x, y): el más cercano entre los que están al alcance, o NULL */
const Warp  *world_button_warp_near(float x, float y);
/* Portal de una misión (el de las Piedras Cairn): de (x, y) en `from` a
   (ax, ay) en `to`; `id` para reemplazarlo */
void         world_portal(int id, int from, float x, float y, int to, float ax, float ay);
int          world_link_count(void);
int          world_route_next(int from, int to); /* zona que sigue para ir de una a otra, -1 = no hay camino */
void         world_zone_rect(int zone, int *x0, int *y0, int *x1, int *y1); /* en tiles */
const WorldLink *world_link(int i);

#endif
