#ifndef NET_H
#define NET_H

#include "input.h"

#include <stddef.h>
#include <stdint.h>

/* La red local (docs/plan-lan.md): cada máquina corre la partida entera y por la red solo van los
   controles de cada tick (lockstep). Una crea la partida (el anfitrión) y las otras se conectan por
   TCP; cada una le manda los controles de sus jugadores para el tick T + delay, y el anfitrión,
   cuando tiene los de todas, los reparte. Cada 25 ticks todas mandan el resumen del estado
   (game_state_hash): si alguno no coincide, se separaron y se corta. */

#define NET_PORT      6112 /* el del TCP/IP del original */
#define NET_MAX_PEERS 8    /* máquinas, el anfitrión incluido */
#define NET_DELAY     3    /* ticks de demora de los controles (120 ms) */

enum { NET_OFF, NET_HOST, NET_CLIENT };

/* Lo que manda el anfitrión al empezar: igual en todas las máquinas */
typedef struct NetStart {
    uint32_t  seed;     /* el mundo y el azar de todo */
    long long epoch;    /* el reloj de la partida (las zonas de terror) */
    int       diff, mode, terror, delay;
    int       num_players;
    int       owner[MAX_PLAYERS]; /* la máquina de cada jugador (0 = el anfitrión) */
    int       cls[MAX_PLAYERS];
    char     *save[MAX_PLAYERS];  /* su partida guardada (texto; vacío = personaje nuevo) */
    size_t    save_len[MAX_PLAYERS];
    int       me;                 /* esta máquina */
} NetStart;

/* Lo de esta máquina al crear o unirse: sus jugadores (1 o 2), la clase, el nivel y la partida
   guardada de cada uno */
typedef struct {
    int         num;
    int         mode; /* (el anfitrión) lo que anuncia a los que buscan: 0 la historia, 1 el Eco del Odio */
    int         cls[MAX_LOCAL], level[MAX_LOCAL], diffmax[MAX_LOCAL];
    const char *save[MAX_LOCAL];
    size_t      save_len[MAX_LOCAL];
} NetLocal;

int  net_host(int port, const NetLocal *mine);                  /* 0 = escuchando */
int  net_join(const char *host, int port, const NetLocal *mine); /* 0 = conectado (sigue en net_poll) */
void net_poll(void);   /* acepta, lee y manda lo pendiente, sin bloquear */
int  net_role(void);   /* NET_OFF, NET_HOST o NET_CLIENT */
/* Los jugadores que hay hasta ahora (el anfitrión: los que se unieron; los otros: los que avisó) */
int  net_lobby(int *cls, int *level, int *owner, int *diffmax, int max); /* (diffmax: la más alta abierta; NULL = no) */
int  net_start(int diff, int mode, int terror, uint32_t seed, long long epoch); /* el anfitrión: a jugar */
const NetStart *net_started(void); /* NULL hasta que llega START */
void net_send_input(uint32_t tick, const PlayerInput *in, int n); /* los de esta máquina para ese tick */
int  net_frame(uint32_t tick, PlayerInput *all);                  /* 1 = ya están los de todos para ese tick */
void net_send_hash(uint32_t tick, uint64_t hash);
const char *net_error(void); /* NULL = bien; si no, por qué se cortó */
void net_close(void);
/* Buscar partidas en la red local (UDP): manda la pregunta y junta las respuestas */
void net_discover_start(void);
int  net_discover_poll(char (*hosts)[64], int *players, int *modes, int max); /* cuántas encontró (la IP, los jugadores, el modo) */
void net_discover_stop(void);
/* Para la pantalla: la IP de esta máquina en la red local ("" si no sabe) */
const char *net_my_ip(void);

#endif
