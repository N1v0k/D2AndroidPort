#ifndef MISSILES_H
#define MISSILES_H

#include "map.h"

/* Proyectiles (missiles.txt): flechas, jabalinas, bolas de fuego, púas y
   las explosiones. Este módulo los mueve y los dibuja; quién recibe el
   golpe y cuánto daño hace lo resuelve game.c. */

enum { ELEM_NONE, ELEM_FIRE, ELEM_COLD, ELEM_LTNG, ELEM_POIS, ELEM_MAG };

/* Comportamientos especiales */
enum {
    MF_EFFECT     = 1 << 0, /* solo se ve (explosiones, nubes): no choca */
    MF_PIERCE     = 1 << 1, /* atraviesa a todos (Rayo) */
    MF_ALWAYS_HIT = 1 << 2, /* no tira puntería (Flecha guiada) */
    MF_HOMING     = 1 << 3, /* busca al monstruo más cercano */
    MF_EXPLODE    = 1 << 4, /* al chocar hace daño de área (Flecha explosiva) */
    MF_POISON_TRAIL = 1 << 5, /* deja nubes de veneno (Jabalina venenosa) */
    MF_POISON_BURST = 1 << 6, /* al chocar suelta nubes de veneno (Jabalina de plaga) */
    MF_CLOUD      = 1 << 7, /* nube de veneno: envenena al que la toca */
    MF_FREEZE     = 1 << 8, /* el frío congela en vez de enfriar (Flecha de hielo) */
    MF_MERC       = 1 << 9, /* de una mercenaria: no usa las propiedades del dueño */
    MF_CHAIN      = 1 << 10, /* efecto que al terminar sigue con el siguiente (missiles_chain_next) */
    MF_FURY       = 1 << 11, /* Furia de rayo: al pegar suelta rayos a los de alrededor (count) */
    MF_IMMOLATE   = 1 << 12, /* Flecha de inmolación: explota y deja fuego en el piso */
    MF_FREEZE_BURST = 1 << 13, /* Flecha congelante: explota y congela */
    MF_BURN       = 1 << 14, /* fuego en el piso: quema a los monstruos que lo pisan (daño por segundo) */
    MF_CHARGED    = 1 << 15, /* rayo cargado: va zigzagueando */
    /* Los de la Hechicera */
    MF_GROUP      = 1 << 16, /* de un grupo (las novas): a cada monstruo le pega uno solo */
    MF_CHAIN_LTNG = 1 << 17, /* Rayo en cadena: al pegar salta a otro (count saltos más) */
    MF_ORB        = 1 << 18, /* Orbe congelado: escupe descargas y termina en una nova */
    MF_NOHIT      = 1 << 19, /* solo se ve: el daño lo hace la habilidad (las llamas de Infierno) */
    MF_LAND       = 1 << 20, /* efecto que al terminar daña alrededor (Ventisca, Meteoro) */
    MF_MAKER      = 1 << 21, /* va dejando fuego en el piso (Muro de fuego) */
    MF_METEOR     = 1 << 22, /* al caer deja fuego en el piso alrededor */
    MF_BLIZZARD   = 1 << 23, /* centro de la Ventisca: cada `count` cuadros cae un trozo de hielo */
    /* Los del Paladín */
    MF_HOLY       = 1 << 24, /* Rayo sagrado: solo daña a no muertos y demonios (a los demás los atraviesa) */
    MF_HEAL       = 1 << 25, /* cura a los aliados que toca (burn_min..burn_max) */
    MF_HAMMER     = 1 << 26, /* Martillo bendito: gira en espiral alrededor de (mx, my) */
    /* Los del Druida */
    MF_KNOCK      = 1 << 27, /* empuja al que golpea (la mitad de las veces: Roca fundida) */
    MF_STUN       = 1 << 28, /* aturde al que golpea por `count` cuadros (Remolino, Onda de choque) */
    MF_WOBBLE     = 1 << 29, /* va dando vueltas (el Tornado) */
    MF_ORBIT      = 1 << 30, /* gira alrededor de su jugador a `mx` de distancia (lo que vuela en el Huracán) */
};

typedef struct MissileDef MissileDef;

#define MISSILE_HITS 16

typedef struct {
    const MissileDef *def;
    float x, y;       /* subtiles */
    float vx, vy;     /* subtiles por tick */
    int   life, age;  /* ticks que le quedan y que lleva */
    int   flags;
    int   owner;      /* jugador que lo tiró (0, 1...) o -1 si es de un monstruo */
    int   source;     /* monstruo que lo tiró */
    int   ar, level;  /* para la probabilidad de pegar */
    int   dmin, dmax; /* daño físico */
    int   elem, emin, emax, elen; /* daño elemental; elen = duración (frío, veneno) en ticks */
    int   radius10;   /* radio de la explosión o la nube, en décimas de subtile */
    int   hit[MISSILE_HITS], num_hit; /* a quiénes ya les pegó (los que atraviesan) */
    int   sound;      /* el sonido que hace al volar (para moverlo con él o cortarlo) */
    float z, vz;      /* altura en píxeles y cuánto baja por tick (lo que cae del techo) */
    int   count;      /* Furia de rayo: a cuántos les tira rayos */
    float burn_acc;   /* fuego en el piso: daño acumulado (es por segundo) */
    int   group;      /* MF_GROUP: el grupo (los que ya recibieron uno no reciben otro) */
    int   burn_min, burn_max, burn_life; /* MF_MAKER y MF_METEOR: el fuego que dejan */
    float mx, my;     /* MF_MAKER: dónde dejó el último fuego; MF_HAMMER: el centro de la espiral */
    float ang;        /* MF_HAMMER: el ángulo en la espiral */
    const char *trail; /* MF_MAKER: lo que va dejando (NULL = el fuego del Muro de fuego) */
    int   used;
} Missile;

int  missiles_init(void);
int  missiles_exists(const char *name);
/* Daño que trae el proyectil en missiles.txt (las púas, la bola de fuego del chamán) */
void missiles_base_damage(const char *name, int *elem, int *emin, int *emax, int *dmin, int *dmax);
/* El daño elemental con lo que sube por nivel de la habilidad (MinELev, MaxELev) */
void missiles_level_damage(const char *name, int lvl, int *emin, int *emax);

/* Crea uno de missiles.txt en (x, y) hacia (dx, dy) y lo devuelve para
   completar el daño y el dueño; NULL si no existe o no hay lugar */
Missile *missiles_spawn(const char *name, float x, float y, float dx, float dy);
Missile *missiles_effect(const char *name, float x, float y); /* explosión que solo se ve */
/* Lo que sigue a un efecto encadenado: su ExplosionMissile o, si no tiene,
   lo que suelta al tocar el piso (CltHitSubMissile1); NULL si nada */
const char *missiles_chain_next(const Missile *mi);
/* ¿Lo puede hacer atravesar Perforar? (columna Pierce de missiles.txt) */
int missiles_can_pierce(const Missile *mi);
/* Light de missiles.txt: cuánto alumbra en la oscuridad (0 = nada) */
int missiles_light(const Missile *mi);
/* ProgOverlay: el efecto que deja sobre el que toca (el destello del rayo); NULL si nada */
const char *missiles_prog_overlay(const Missile *mi);

int      missiles_capacity(void);
Missile *missiles_get(int i);  /* NULL si el lugar está libre */
void     missiles_remove(int i);
void     missiles_clear(void);

/* Avanza un tick. Devuelve 0 si sigue volando, 1 si chocó una pared y 2
   si se le terminó el alcance. */
int  missiles_step(int i, const Map *m);
void missiles_end(int i); /* deja su explosión (si tiene) y lo saca */

void missiles_position(int i, float alpha, float *x, float *y);
void missiles_draw(int i, int sx, int sy);
const char *missiles_explosion(const Missile *mi); /* nombre de la explosión, o NULL */

/* Sonido del golpe contra un monstruo o un jugador según su HitClass
   (flecha, hoja, punta); los que tienen HitSound suenan solos al terminar */
void missiles_impact_sound(const Missile *mi);

#endif
