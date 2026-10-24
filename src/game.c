#include "game.h"

#include "audio.h"
#include "bot.h"
#include "charanim.h"
#include "fmt/fs.h"
#include "font.h"
#include "iso.h"
#include "cube.h"
#include "itemstats.h"
#include "items.h"
#include "jobs.h"
#include "map.h"
#include "missiles.h"
#include "overlays.h"
#include "skills.h"
#include "controls.h"
#include "sequences.h"
#include "ui.h"
#include "strings.h"
#include "units.h"
#include "video.h"
#include "world.h"
#include "lang.h"
#include "net.h"

#include <SDL.h>

#include <ctype.h>
#include <math.h>
#include "dmath.h" /* (sinf, atan2f... iguales en todas las máquinas: la red) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "compat.h"
#ifdef __GLIBC__
#include <malloc.h> /* malloc_trim: devolverle al sistema lo que liberó una zona */
#endif

/* Caminar a 5 subtiles por segundo equivale a la WalkVelocity 6 de
   charstats.txt; correr se escala igual (RunVelocity 9 = 1,5 veces). */
#define SPEED_PER_VELOCITY (5.0f / 6.0f)
#define WALK_BONUS         1.10f /* más ágil que el original: caminando un 10% más rápido */
#define RUN_BONUS          1.20f /* y corriendo un 20% */
#define RUN_ANIM_PACE      0.75f /* las piernas al correr, un 25% más lentas (si no, parece un error) */
#define PLAYER_RADIUS      0.3f
#define PI_F               3.14159265f

/* Correr gasta RunDrain/256 de estamina por tick, como el original (unos 2
   puntos por segundo). Al agotarse, la barra tarda esto en llenarse y
   recién ahí se puede volver a correr. */
#define STAMINA_REFILL_SECONDS 15.0f

/* Pantalla partida: cada jugador queda a esta fracción del camino entre el
   centro y el borde de la pantalla, en la dirección del otro jugador. */
#define SPLIT_FACTOR 0.5f

#define MELEE_RANGE       2.2f  /* distancia (subtiles) para elegir a quién pegar */
#define RESPAWN_SECONDS   3     /* después de morir, se reaparece en el pueblo */
#define TOWN_HEAL_SECONDS 5.0f  /* en el pueblo la vida se llena en este tiempo */
#define EXP_SHARE_RANGE   30.0f /* los compañeros a esta distancia también suman experiencia */

/* Animaciones del jugador: pueblo (TN, TW), afuera (NU, WL), correr y combate */
enum { MODE_TN, MODE_TW, MODE_RN, MODE_NU, MODE_WL, MODE_A1, MODE_GH, MODE_DT, MODE_DD, MODE_SC, MODE_S1, MODE_TH,
       MODE_A2, MODE_S3, MODE_BL, MODE_KK, MODE_S2, MODE_S4, MODE_COUNT };
static const char *const k_mode_codes[MODE_COUNT] = { "tn", "tw", "rn", "nu", "wl", "a1", "gh", "dt", "dd", "sc", "s1",
                                                      "th", "a2", "s3", "bl", "kk", "s2", "s4" };

/* Cómo es el ataque en curso: cuerpo a cuerpo, con arco o ballesta, o lanzando el arma */
enum { ATK_MELEE, ATK_SHOOT, ATK_THROW };

/* PS_CAST: lanzando una habilidad que no es un ataque (gritos, Percepción...) */
enum { PS_NORMAL, PS_ATTACK, PS_HIT, PS_CAST, PS_DYING, PS_DEAD };

/* Las formas del Druida */
enum { FORM_NONE, FORM_WOLF, FORM_BEAR };

#define HYDRA_MAX 6 /* cabezas de la Hidra de cada jugador */
#define ASS_TRAPS_MAX 5 /* trampas de la Asesina */
#define NEC_PETS  64 /* esbirros del Nigromante (los pedazos de los muros también) */
/* Los esbirros que los monstruos pueden atacar: los de los dos Nigromantes,
   las Valquirias y los convertidos */
#define MAX_PET_TARGETS (MAX_PLAYERS * NEC_PETS + 16)

/* Casilleros de los botones A, B, X e Y: una habilidad, el ataque normal o nada */
#define NUM_BUTTONS 4
enum { BTN_THROW = -3, BTN_EMPTY = -2, BTN_ATTACK = -1 };
static const char *const k_button_names[NUM_BUTTONS] = { "A", "B", "X", "Y" };

typedef struct {
    const char *token;
    const char *name; /* fila de charstats.txt y columna de experience.txt */
    CharAnim    anim[MODE_COUNT];
    float       stamina;   /* estamina inicial */
    float       run_drain; /* gasto por tick corriendo */
    float       walk_speed, run_speed; /* subtiles por segundo */
    int         life, mana;
    int         str, dex;
    int         to_hit;           /* ToHitFactor de charstats.txt */
    int         life_per_level;   /* vida que gana por nivel (sin contar la vitalidad) */
    int         vit, energy;      /* atributos con los que arranca */
    int         life_per_vit4, stamina_per_level4, stamina_per_vit4, mana_per_energy4; /* en cuartos */
    int         mana_per_level4;  /* maná por nivel, en cuartos de punto */
    float       stamina_per_level;
    float       mana_regen;       /* segundos en llenar el maná */
    uint32_t    exp_next[100];    /* experiencia total para pasar del nivel N al N+1 (sin signo: el 99 pide 3.520 millones) */
    const char *start_items[3];   /* equipo inicial, como en el original */
    const char *code;             /* "bar": la columna de clase de itemtypes.txt */
    const char *abbr;             /* "Bar": en los textos de los NPC ("CharsiIntroBarGossip1") */
    const char *voice;            /* "barbarian": sus sonidos ("barbarian_needmana_1") */
    const char *title;            /* "Bárbaro" */
    const char *tree;             /* fondo del árbol de habilidades */
    int         light;            /* pisa liviano (los pasos) */
    int         block_factor;     /* BlockFactor de charstats.txt: lo que suma al bloqueo con escudo */
    int         snd_hit, snd_death;   /* sounds.txt: quejido al recibir un golpe y al morir */
    int         snd_step[2][2];       /* pasos: [cueva][corriendo] */
} CharClass;

/* Mercenarios (hireling.txt, el número es su Id): las pícaras de Kashya de
   flecha de fuego (0) y de flecha fría (1), y los guerreros del desierto de
   Greiz con Plegaria (6), Desafío (7) o Puntería bendecida (8). Cada
   jugador puede tener uno. */
#define MERC_TYPES  39 /* los Id de hireling.txt: 0..38, de las tres dificultades (en Normal, los Lobos de Hierro son 15, 16 y 17; los bárbaros de Qual-Kehk, 24, 25 y 36) */
#define MERC_ROWS   4
#define MERC_OFFERS 4
#define MERC_NAMES  41 /* merc01 .. merc41 de mercenaries.json (si falta hireling.txt) */
/* Lugares de su equipo en el panel (npcinv): la pícara usa arco, casco y
   armadura; el escudo no */
enum { MSLOT_WEAPON, MSLOT_HEAD, MSLOT_TORSO, MSLOT_SHIELD, MERC_SLOTS };
typedef struct {
    int type, level, name;
} MercOffer;

#define BAG_CELLS   40 /* la mochila del original: 10x4 casillas */
#define BAG_W       10
#define BAG_H       4
#define BAG_MAX     40
/* El cofre (pedido del usuario): pestañas de 10x6 en el lugar del equipo,
   como el cubo (así entra también con dos jugadores). Una por cada acto al
   que se llegó y las que se compran: la primera STASH_PRICE y cada una el
   triple que la anterior. */
#define STASH_W       10
#define STASH_H       6
#define STASH_CELLS   (STASH_W * STASH_H)
#define STASH_ACTS    5  /* una por acto */
#define STASH_BUY_MAX 5  /* y hasta 5 compradas */
#define STASH_TABS    (STASH_ACTS + STASH_BUY_MAX)
#define STASH_PRICE   50000
#define BELT_SLOTS  4  /* sin cinturón equipado: una fila de 4 */
#define GAMBLE_ITEMS 14 /* lo que hay para apostar (D2GAME_STORES_FillGamble) */
#define DIFF_RAW    4096 /* lo de otra dificultad en la partida guardada (las líneas tal cual) */
#define NPC_TALKERS 37 /* los que hablan (k_npc_vo): los 6 del campamento, los 12 de Lut Gholein, Tyrael, los 7 de Kurast, 4 del Acto 4, 6 de Harrogath y Tyrael en la Cámara */
#define QUEST_LINES 65 /* comentarios de las etapas de las misiones (k_talk_lines) */
/* Las piezas del Báculo que Caín ya comentó (Player.staff_seen) y lo demás del Acto 2 (Player.a2_flags) */
enum { A2_SEEN_SCROLL = 1, A2_SEEN_AMULET = 2, A2_SEEN_STAFF = 4, A2_SEEN_CUBE = 8, A2_SEEN_HORADRIC = 16 };
enum { A2F_SEWERS = 1, A2F_BOOK = 2, A2F_TOMBS_TOLD = 4, A2F_SAILED = 8 }; /* (el 4: Jerhyn ya contó lo de las Siete Tumbas;
                                                                  el 8: ya navegó al este, el A2COMPLETED del original) */
/* Player.a3_flags (las misiones del Acto 3, a3quest.inc) */
enum {
    A3F_HRATLI = 1, /* Hratli ya se presentó en el muelle (A3Q0) y está en su fragua */
    A3F_LK = 2, A3F_GM = 4, A3F_FJ = 8, A3F_CAUSEWAY = 16, /* el grupo llegó ahí: ya se puede pedir la misión */
    A3F_SEEN_EYE = 32, A3F_SEEN_BRAIN = 64, A3F_SEEN_HEART = 128, A3F_SEEN_FLAIL = 256, /* las que Caín comentó */
    A3F_LIFE = 512,     /* ya tomó la Poción de la vida */
    A3F_HIRED = 1024,   /* ya usó el mercenario gratis de Asheara */
    A3F_HELLGATE = 2048, /* ya cruzó el portal rojo: el A3COMPLETED del original (su cinemática) */
};
/* Player.a4_flags (las misiones del Acto 4, a4quest.inc) */
enum {
    A4F_STONE = 1,        /* la Piedra de Mefisto ya está sobre la Forja */
    A4F_Q1_TOLD = 2,      /* Tyrael y Caín ya comentaron El Ángel Caído (después del pedido) */
    A4F_Q1_EARLY = 4,     /* ... y que volvió sin matar a Izual */
    A4F_Q2_TOLD = 8,      /* ... El Final del Terror */
    A4F_PORTAL = 16,      /* Tyrael ya abrió el portal del final */
    A4F_CAIN_IZUAL = 32,  /* Caín ya comentó lo de Izual */
    A4F_CAIN_DIABLO = 64, /* ... y lo de Diablo */
    A4F_END = 128,        /* ya vio el epílogo */
};
/* Player.a5_flags (las misiones del Acto 5, a5quest.inc) */
enum {
    A5F_RESIST = 1,      /* leyó el Pergamino de Resistencia (+10 a todas) */
    A5F_ANYA_TOLD = 2,   /* Anya ya agradeció (después del rescate) */
    A5F_PORTAL = 4,      /* Anya ya abrió el portal al templo */
    A5F_END = 8,         /* ya vio el final (d2x_out) */
    A5F_ANCIENTS = 16,   /* los Antiguos ya hablaron (empieza la víspera de la destrucción) */
};
#define GOSSIP_MAX  15 /* frases sueltas por NPC ("AkaraGossip1".."AkaraGossip15") */

typedef struct {
    float   x, y; /* posición en subtiles */
    float   prev_x, prev_y;
    float   face_x, face_y; /* hacia dónde mira, en pantalla */
    int     sector;         /* 0..15 en sentido horario desde el sur */
    int     moving;
    int     running;        /* corrió en este tick */
    float   stamina, stamina_max;
    int     exhausted;      /* se quedó sin estamina: solo camina hasta llenarla */
    const Level *level;     /* zona donde está (para avisar al entrar a otra) */
    int     banner_ticks;   /* cuánto falta mostrar el nombre de la zona */
    int     mode;
    int     anim_acc;       /* avance de la animación en 1/256 de cuadro */
    int     cls;
    int     slot;           /* partida guardada (1..6), 0 = ninguna */
    uint8_t label;          /* rampa de color del cartel "J1" / "J2" */

    /* Combate */
    int     state;
    int     hp, hp_max, mana, mana_max;
    int     hp_base, mana_base; /* sin contar el equipo */
    int     stat_add[4];        /* puntos puestos en fuerza, destreza, vitalidad y energía */
    int     stat_points;        /* puntos de atributo sin repartir (5 por nivel) */
    int     hp_extra;           /* vida de recompensas de versiones viejas */
    int     stat_sel;           /* atributo marcado en el panel del personaje */
    int     bonus[STAT_COUNT];  /* suma de las propiedades mágicas del equipo puesto, los conjuntos y los talismanes */
    int8_t  item_sk[MAX_CLASS_SKILLS]; /* + a cada habilidad por los objetos (todas, de la clase, de la solapa...) */
    int     lvl_ac_pct, lvl_dmg_pct;   /* % de defensa y de daño que suben con el nivel */
    int     stats_dirty;               /* entró o salió un talismán de la mochila: recalcular */
    float   heal_acc;
    float   replenish_acc;      /* regeneración de vida de los objetos */
    int     level_num;
    uint32_t exp; /* (sin signo, como el original: el nivel 99 pide 3.520 millones) */
    int     target;         /* monstruo al que está atacando */
    int     attack_skill;   /* habilidad del ataque o lanzamiento en curso (-1 = ataque normal) */
    int     hits_total, hits_done; /* golpes del ataque en curso (Estocada da 3) */
    int     cast_target;    /* cadáver para Buscar poción / objeto */
    int     attack_kind;    /* ATK_* */
    float   aim_x, aim_y;   /* hacia dónde dispara si no hay a quién apuntar */
    int     poison_ticks;   /* envenenado (púas de las ratas en Pesadilla, etc.) */
    int     chill_ticks;    /* enfriado (un golpe de frío): la mitad de rápido */
    int     aura_slow;      /* lo que lo frena un aura de un monstruo (la Congelación sagrada de Duriel), este tick */
    int     conv_def, conv_res; /* la Convicción de un jefe con aura, este tick: % de defensa y de resistencias (negativos) */
    int     pierce[3];          /* lo que le quita el aura de un Anciano colosal (fuego, frío, rayo), este tick */
    int     antidote_ticks; /* poción de antídoto: el veneno hace la mitad */
    int     amp_ticks;      /* maldecido por un jefe: recibe el doble de daño físico */
    int     curse, curse_ticks, curse_lvl; /* A4C_*: la maldición de un Caballero del Olvido (Amplificar va en amp_ticks) */
    int     frozen_ticks;   /* congelado (el toque frío de Diablo): ni se mueve ni ataca */
    int     drain_ticks, drain_lvl; /* el abrazo de los Gropers (fingermagecurse): menos regeneración de maná */
    int     thaw_ticks;     /* poción de deshielo: el frío hace la mitad */
    int     map_open;       /* el automapa a la vista */
    float   poison_rate, poison_acc;
    int     dead_ticks;
    int     gold_lost;      /* el oro que perdió al morir (se avisa al reaparecer) */
    int     levelup_ticks;

    /* Objetos */
    Item    equip[SLOT_COUNT];
    Item    bag[BAG_MAX];
    uint8_t bag_x[BAG_MAX], bag_y[BAG_MAX]; /* casilla de arriba a la izquierda de cada objeto */
    int     bag_count;
    Item    held;                   /* objeto levantado en el inventario (en la mano) */
    Item    belt[BELT_SLOTS];
    Item    cube[CUBE_W * CUBE_H];  /* lo que hay en el Cubo Horádrico (una grilla como la mochila) */
    uint8_t cube_x[CUBE_W * CUBE_H], cube_y[CUBE_W * CUBE_H];
    int     cube_count;
    int     cube_open;              /* el cubo abierto en el inventario (en lugar del equipo) */
    /* El cofre: cada pestaña, una grilla como la mochila; y el oro guardado
       (al morir no se pierde) */
    Item    stash[STASH_TABS][STASH_CELLS];
    uint8_t stash_x[STASH_TABS][STASH_CELLS], stash_y[STASH_TABS][STASH_CELLS];
    int     stash_count[STASH_TABS];
    int     stash_bought;           /* pestañas compradas */
    int     stash_gold;
    int     stash_open, stash_tab;  /* abierto en el inventario (en lugar del equipo) y la pestaña que se ve */
    int     stash_confirm;          /* A en la pestaña "+": con otra A se compra */
    int     act_max;                /* el acto más alto al que llegó, en cualquier dificultad (una pestaña del cofre por cada uno) */
    int     act_top;                /* el más alto en esta dificultad (las solapas de los waypoints y del registro) */
    int     gold;
    float   regen_life, regen_mana; /* cuánto curan las pociones por tick */
    int     regen_life_ticks, regen_mana_ticks;
    float   regen_life_acc, regen_mana_acc;
    int     stamina_boost_ticks;    /* poción de estamina: correr no cansa */
    int     shrine_kind, shrine_ticks, shrine_arg0, shrine_arg1; /* el santuario que dura (Code y Arg de shrines.txt; uno a la vez) */
    int     inv_open;
    int     inv_sel;                /* objeto elegido en el inventario */
    int     dialog_npc;             /* NPC con el que habla (-1 = ninguno) */
    int     dialog_sel;
    int     shop_open, shop_tab, shop_sel; /* tienda: la solapa de la mercadería (armaduras, armas, varios) y lo elegido */
    int     shop_selling;                  /* vendiendo: el cursor está en la mochila del inventario (inv_sel) */
    int     gambling;                       /* la tienda es la apuesta (Gheed, Elzix) */
    Item    gamble[GAMBLE_ITEMS];           /* lo que hay para apostar (ya tirado: se ve como el objeto base) */
    int     gamble_count;
    const char *talk_text;          /* lo que dice el NPC (los textos del juego, vo.json) */
    int     talk_ticks;             /* desde que empezó a hablar: el texto se va desplazando */
    int     talk_voice;             /* cuánto dura la voz de lo que dice (ticks), 0 = sin voz */
    int     said;                   /* frases que ya dijo en esta partida (al entrar a la cueva...) */
    int     chat_open, chat_sel;    /* la lista de frases (R3) y la elegida */
    int     den_done;               /* limpió la Den of Evil (falta hablar con Akara) */
    int     den_rewarded;           /* ya cobró la recompensa de la Den of Evil */
    int     respec_done;            /* ya usó la de Akara en esta dificultad (endgame.inc) */
    int     den_started;            /* Akara ya le pidió limpiar la cueva */
    int     raven_started, raven_done, raven_rewarded; /* Cementerio de las Hermanas (Kashya) */
    uint64_t npc_met;               /* NPCs que ya se presentaron (un bit por NPC de k_npc_vo) */
    uint64_t talk_heard[NPC_TALKERS][2]; /* comentarios de misión que ya dijo cada NPC (bit por k_talk_lines) */
    uint8_t talk_next[NPC_TALKERS]; /* la próxima frase suelta de cada NPC */
    uint8_t talk_away[NPC_TALKERS]; /* salió del campamento desde que ese NPC habló de las misiones */
    uint64_t waypoints;             /* waypoints activados (bit = número de levels.txt; 0 = el pueblo) */
    int     wp_open, wp_sel;        /* panel de waypoints */
    /* Misiones 3 a 6 (ver "Misiones 3 a 6 del Acto 1") */
    int     cain_q;   /* Caín: 1 Akara la pidió, 2 tiene el pergamino, 3 traducido, 4 portal abierto, 5 rescatado, 6 recompensa */
    int     malus_q;  /* Herramientas: 1 Charsi la pidió, 2 tiene el Mazo, 3 lo devolvió (imbuir), 4 ya imbuyó */
    int     tower_q;  /* Torre Olvidada: 1 leyó el tomo, 2 murió la Condesa, 3 se lo contó a Akara */
    int     andy_q;   /* Hermanas: 1 en curso, 2 murió Andariel, 3 viajó al este */
    /* Acto 2 (ver "Las misiones del Acto 2") */
    int     rada_q;     /* Radament: 1 Atma la pidió, 2 murió Radament, 3 se lo contó a Atma */
    int     staff_q;    /* Báculo Horádrico: 1 Caín leyó el pergamino, 2 le mostró el Báculo, 3 lo puso en el orificio */
    int     staff_seen; /* las piezas que Caín ya comentó (A2_SEEN_*) */
    int     sun_q;      /* Sol Mancillado: 1 se oscureció, 2 Drognan lo explicó, 3 rompió el altar, 4 se lo contó a alguien */
    int     arcane_q, summ_q, tombs_q; /* Santuario Arcano, el Invocador y las Siete Tumbas (fase 5) */
    int     a2_flags;   /* A2F_*: bajó a las cloacas, tiene un Libro de Habilidad por leer */
    /* Acto 3 (docs/plan-acto3.md, fase 5) */
    int     bird_q, blade_q, will_q, tome_q, temple_q, meph_q; /* A3Q4, A3Q3, A3Q2, A3Q1, A3Q5 y A3Q6 */
    int     a3_flags;   /* A3F_* */
    /* Acto 4 (docs/plan-acto4.md, fase 5; los estados en a4quest.inc) */
    int     izual_q, forge_q, diablo_q; /* A4Q1, A4Q3 y A4Q2 */
    int     a4_flags;   /* A4F_* */
    /* Acto 5 (docs/plan-acto5.md, fase 5; los estados en a5quest.inc) */
    int     shenk_q, rescue_q, anya_q, nihl_q, ancients_q, baal_q; /* A5Q1..A5Q6 */
    int     a5_flags;   /* A5F_* */
    int     rescued;    /* los bárbaros liberados (de 15) */
    int     cow_king;   /* mató al Rey Vaca: ya no abre el portal a la granja (A1Q4 QFLAG_CUSTOM6, cows.inc) */
    /* Las dificultades: la más alta abierta (mató a Baal en la anterior) y,
       de las que no se juegan ahora, lo suyo de la partida guardada como se
       leyó (las misiones de arriba son las de s_diff: save_diff) */
    int     diff_max;
    char    diff_raw[3][DIFF_RAW];
    int     eco_best, eco_runs; /* el Eco del Odio: la mejor oleada y cuántas corridas (la línea "eco") */
    int     imbue_mode; /* la lista de Charsi: 0 imbuir, 1 engarzar (Larzuk), 2 personalizar (Anya) */
    int     act;      /* el acto en el que está (se guarda: al volver a jugar se sigue ahí) */
    uint32_t quest_seen; /* misiones cumplidas que ya vio en el registro (bit (acto - 1) * 6 + misión): arden una vez */
    int     qlog_act, qlog_sel; /* el registro de misiones del panel: acto (0 = elegir al abrirlo) y misión */
    uint32_t qlog_anim;          /* s_anim_ticks al abrirlo (para el fuego de las recién cumplidas) */
    int     imbue_open, imbue_sel;  /* lista de Charsi para imbuir */
    const char *reading;            /* texto del tomo que está leyendo (NULL = nada) */

    /* Mercenaria */
    int     merc_name;              /* 1..41 (mercNN), 0 = no tiene */
    int     merc_type, merc_level, merc_hp, merc_dead;
    int     merc_unit;              /* su unidad en el mapa, -1 = ninguna */
    int     merc_warned;            /* ya avisó que está por morir */
    int     free_hire;              /* la recompensa de Blood Raven: la próxima es gratis */
    int     hire_open, hire_sel;    /* lista de Kashya */
    MercOffer offers[MERC_OFFERS];
    int     num_offers, offers_level, offers_act;
    Item    merc_equip[MERC_SLOTS];
    int     merc_sel;               /* lugar marcado en su panel */
    int     merc_pick, pick_sel;    /* lista de la mochila para ponerle algo */

    /* Habilidades */
    int     skill_lvl[MAX_CLASS_SKILLS];
    int     skill_points;
    int     buttons[NUM_BUTTONS];   /* qué tira cada botón (BTN_* o una habilidad) */
    int     inv_page;               /* panel: 0 = inventario, 1 = árbol de habilidades */
    int     tree_sel;               /* habilidad marcada en el árbol */
    int     tree_page;              /* solapa del árbol que se ve (1..3) */
    int     assigning;              /* el recuadro "poner en un botón" del árbol, abierto */
    int     assign_sel;             /* el botón marcado en ese recuadro (0 A, 1 B, 2 X, 3 Y) */
    int     btn_sel, btn_pick, btn_list_sel; /* página Botones: el botón, si elige habilidad y cuál */
    float   mana_acc;
    int     shout_ticks, shout_pct; /* Grito: defensa extra */
    /* Pasivas, recalculadas con el equipo (algunas dependen del arma) */
    int     sk_dmg_pct, sk_ar_pct, sk_crit, sk_def_pct, sk_stam_pct, sk_dodge;
    int     sk_avoid, sk_evade, sk_pierce, sk_speed; /* Evitar, Evadir, Perforar, Aumentar velocidad */
    int     sk_throw_ar, sk_throw_dmg, sk_throw_crit; /* Dominio de armas arrojadizas (al lanzar) */
    int     resist[3];              /* fuego, frío y rayo (Resistencia natural) */
    /* Habilidades en curso: la secuencia del original que está jugando
       (sequences.h; NULL = la animación normal del modo) */
    const SeqFrame *seq;
    int     seq_n, seq_loops, seq_hits, seq_pos; /* seq_pos: pasos ya jugados (sus golpes ya se dieron) */
    int     jump_f0, jump_f1;       /* Salto: los pasos de la secuencia donde salta y donde cae */
    int     valk_dead;              /* la Valquiria murió hace tantos ticks */
    int     hand;                   /* el golpe en curso: 0 la mano derecha, 1 la izquierda (dos armas) */
    int     targets[16], num_targets, target_k; /* Ametrallar y Rechazo: a quién le toca */
    float   jump_x0, jump_y0, jump_x1, jump_y1; /* Salto: de dónde a dónde */
    int     jumping;                /* en el aire */
    float   whirl_x, whirl_y;       /* Torbellino: hasta dónde */
    int     whirl_hit[24], whirl_nhit;
    /* Efectos que duran (ticks que les quedan) */
    int     bo_ticks, bo_pct;       /* Órdenes de batalla: % de vida, maná y estamina */
    int     bc_ticks, bc_bonus;     /* Comando de batalla: + a las habilidades */
    int     frenzy_ticks, frenzy_hits; /* Frenesí: cada acierto acelera */
    int     berserk_ticks;          /* Furia: la defensa queda en 0 */
    int     decoy_ticks, decoy_hp, decoy_hp_max, decoy_sector; /* Señuelo */
    float   decoy_x, decoy_y;
    int     valk_unit;              /* la Valquiria (-1 = ninguna) */
    /* Hechicera */
    int     sk_fire_pct, sk_ltng_pct, sk_cold_pierce, sk_mana_regen; /* los dominios y Calidez (pasivas) */
    float   cast_x, cast_y;         /* adónde va el hechizo: el monstruo o adelante */
    int     armor_kind, armor_ticks, armor_pct, armor_skill; /* la armadura de hielo puesta */
    int     es_ticks, es_skill;     /* Escudo de energía */
    int     enchant_ticks, enchant_min, enchant_max; /* Encantar: fuego en los golpes */
    int     blaze_ticks, blaze_skill; /* Llamarada */
    float   blaze_x, blaze_y;
    int     storm_ticks, storm_skill, storm_next; /* Tormenta */
    int     inferno_ticks, inferno_skill; /* Infierno: ticks de llamas que quedan */
    int     hydra_unit[HYDRA_MAX], hydra_ticks[HYDRA_MAX], hydra_lo, hydra_hi; /* las cabezas de la Hidra */
    /* Paladín */
    int     aura;                   /* el aura puesta (índice de la habilidad, -1 = ninguna) */
    int     aura_next;              /* ticks hasta su próximo pulso */
    int     aura_nomana;            /* Plegaria o Santuario están gastando: el maná no se recupera */
    /* Lo que le dan las auras de los paladines cercanos (se recalcula cada tick) */
    int     au_dmg_pct, au_ar_pct, au_def_pct, au_ias, au_speed, au_conc;
    int     au_mana_regen, au_stam_regen, au_stam_pct, au_stam_seen, au_thorns, au_thorns_flat;
    int     au_res[3], au_res_max[3];         /* fuego, frío y rayo */
    int     au_elem_min[3], au_elem_max[3];   /* fuego, rayo y frío en los golpes (Fuego, Descarga y Congelación sagrados) */
    int     au_undead_dmg, au_undead_ar;      /* Santuario: contra los no muertos */
    int     au_got_n;                         /* las auras que lo alcanzan (para sus íconos) */
    int8_t  au_got_cls[4];
    int16_t au_got_skill[4];
    int     res_max[3];             /* + al máximo de resistencia (las pasivas de las auras de resistencia) */
    int     hs_ticks, hs_skill, hs_def, hs_block; /* Escudo sagrado */
    int     charge_ticks;           /* Carga: corriendo hacia el monstruo */
    int     fist_ticks, fist_target, fist_skill; /* Puño de los cielos: el rayo cae a los 10 cuadros */
    float   fist_x, fist_y;
    /* Nigromante */
    int     pets[NEC_PETS], pet_kind[NEC_PETS], pet_dead[NEC_PETS]; /* los esbirros (unidades), de qué son (PK_*) */
    uint32_t pet_born[NEC_PETS];    /* cuándo salió cada uno (el más viejo se va primero) */
    int     bone_armor, bone_armor_max; /* Armadura de hueso: lo que le queda por absorber */
    int     golem_fire, golem_next; /* el gólem de fuego: nivel de la habilidad y su próximo Fuego sagrado */
    /* Asesina */
    int     prg_charges[6], prg_ticks[6], prg_lvl[6], prg_skill[6]; /* las cargas de cada arte marcial */
    int     prg_ar;                 /* % de puntería que suman las cargas */
    int     kicks_left, fury_left;  /* Talón del dragón: patadas que quedan; Furia de cuchillas: las que tira */
    int     quick_ticks, quick_speed, quick_ias; /* Rapidez */
    int     fade_ticks, fade_res;   /* Desvanecerse */
    int     venom_ticks, venom_lo, venom_hi, venom_len; /* Veneno */
    int     bshield_ticks, bshield_next, bshield_skill; /* Escudo de cuchillas */
    int     sk_wblock;              /* Bloqueo con armas (pasiva, con dos garras) */
    int     trap_unit[ASS_TRAPS_MAX], trap_skill[ASS_TRAPS_MAX], trap_shots[ASS_TRAPS_MAX], trap_lvl[ASS_TRAPS_MAX];
    uint32_t trap_born[ASS_TRAPS_MAX];
    int     sent_unit, sent_ticks, sent_dir, sent_lo, sent_hi; /* Centinela de cuchillas */
    float   sent_x0, sent_y0, sent_x1, sent_y1, sent_t;
    int     shadow_unit, shadow_dead; /* el Guerrero o el Maestro de las sombras */
    /* Druida */
    int     form, form_ticks, form_skill; /* FORM_*: la forma, lo que le queda y la habilidad (Hombre lobo u oso) */
    int     form_life, form_stam;   /* % de vida y de estamina de la forma */
    /* Lo que suman la forma y las cargas: daño, defensa, puntería, velocidad
       de ataque y al moverse, robo de vida y lo que aturden los golpes */
    int     dru_dmg_pct, dru_def_pct, dru_ar_pct, dru_ias, dru_speed, dru_steal, dru_stun;
    int     feral_charges, feral_ticks, feral_skill; /* Furia salvaje */
    int     maul_charges, maul_ticks, maul_skill;    /* Vapulear */
    int     cyclone, cyclone_max;   /* Armadura de ciclón: lo que le queda por absorber */
    int     arctic_ticks, arctic_skill; /* Ráfaga ártica */
    int     fiss_ticks, fiss_next, fiss_skill; /* Fisura */
    float   fiss_x, fiss_y;
    int     volc_ticks, volc_next, volc_skill; /* Volcán */
    float   volc_x, volc_y;
    int     arma_ticks, arma_next, arma_skill; /* Armagedón */
    int     hurr_ticks, hurr_next, hurr_skill; /* Huracán */
    int     vine_skill, vine_next;  /* la enredadera (una a la vez) */
    int     vine_draw;              /* ticks hasta renovar su rastro de lianas (lo que se ve) */
    float   vine_tx, vine_ty;       /* dónde dejó el último */
    int     spirit_skill, spirit_unit; /* el espíritu (uno a la vez) */
    int     au_life_pct, au_life_seen; /* Sabio roble: % de vida */
    /* El Brujo (skillwar.inc) */
    int     hex_skill, hex_ticks;            /* el maleficio del arma (uno a la vez) */
    int     ward, ward_max, ward_skill;      /* Resguardo psíquico: lo que absorbe todavía */
    int     eldritch_skill, eldritch_ticks, eldritch_next; /* Explosión sobrenatural */
    int     consume_ticks, consume_life, consume_speed;    /* Consumir */
    int     bind_unit;                       /* el demonio vinculado (-1 = ninguno) */
    int     abyss_skill, abyss_ticks;        /* el Abismo abierto */
    float   abyss_x, abyss_y;
    int     war_cleaving;                    /* Tajo: los golpes de más no vuelven a partir */
    int     bind_ch_skill, bind_ch_target, bind_ch_button, bind_ch_frames, bind_ch_acc; /* Vincular demonio: se canaliza */
    int     bind_aura;                       /* el aura del vinculado desde el nivel 20 (del Paladín; -1 = ninguna) */
    int     consume_n, consume_stat[5], consume_val[5]; /* Consumir: lo que da el demonio sacrificado (monpet.txt) */
    int     war_req_pct;                     /* Dominio de la levitación: % menos de requisitos de las armas */
    int     held_mask, last_button;          /* los botones mantenidos ahora y el último que usó (para canalizar) */
    int     war_cd[MAX_CLASS_SKILLS];        /* lo que falta para poder usar cada una otra vez (localdelay) */
    CharAnim anim[MODE_COUNT];      /* animaciones con el equipo que lleva puesto (para dibujarlo: se cargan en otro hilo) */
    char    look_key[64];           /* para no recargarlas si el aspecto no cambió */
    char    look_want[64];          /* el aspecto del equipo actual (se carga en otro hilo) */
    CharAnim meta[MODE_COUNT];      /* las del equipo actual, solo lo de la lógica (COF: cuadros, velocidad, golpe): enseguida */
    char    meta_key[64];
    int     warp_lock;              /* recién saltó de nivel: espera a que salga del salto de llegada */
    int     lock_from, lock_to;     /* ese salto (zonas) */
    char    message[80];            /* avisos cortos ("Inventario lleno", lo que dice un santuario) */
    int     message_ticks;
    int     step_frame;             /* último cuadro de la caminata en que sonó un paso */
    uint32_t last_block;            /* tick del último bloqueo (la animación no se repite antes de 15 cuadros) */
} Player;

enum { DRAW_WALL, DRAW_PLAYER, DRAW_UNIT, DRAW_ITEM, DRAW_MISSILE, DRAW_DECOY };

/* Atributos que se reparten con los puntos de cada nivel */
enum { ATTR_STR, ATTR_DEX, ATTR_VIT, ATTR_ENE };

/* Paneles de 320x432 (inventario, personaje, árbol) y sus páginas */
#define PANEL_W 320
#define PANEL_H 432
#define PANEL_Y 60
#define CELL    29
#define INV_CELL 29
enum { PAGE_INVENTORY, PAGE_SKILLS, PAGE_BUTTONS, PAGE_STATS, PAGE_MERC, PAGE_QUESTS, PAGE_COUNT };

/* Lugares que recorre el cursor del inventario: el equipo, cada casilla de
   la mochila, el cinturón y el botón Ordenar */
#define INV_CELL0   SLOT_COUNT
#define INV_BELT0   (INV_CELL0 + BAG_CELLS)
#define INV_SORT    (INV_BELT0 + BELT_SLOTS)
#define INV_STATS   (INV_SORT + 1) /* el botón de la cruz roja: abre la página de atributos */
#define INV_CUBE0   (INV_STATS + 1) /* las casillas del cubo abierto */
#define INV_TRANSMUTE (INV_CUBE0 + CUBE_W * CUBE_H) /* el botón Transmutar */
#define INV_STASH0  (INV_TRANSMUTE + 1)        /* las casillas del cofre abierto */
#define INV_STAB0   (INV_STASH0 + STASH_CELLS) /* sus pestañas (y la de comprar otra) */
#define INV_GOLD_IN (INV_STAB0 + STASH_TABS)   /* guardar el oro */
#define INV_GOLD_OUT (INV_GOLD_IN + 1)         /* sacarlo */
#define INV_ENTRIES (INV_GOLD_OUT + 1)
#define CUBE_X      119 /* la grilla del cubo en el panel, donde va el equipo */
#define CUBE_Y      52
#define STASH_GX    19  /* la grilla del cofre, debajo de sus pestañas */
#define STASH_GY    34

/* Solapas de la tienda, como el original (armaduras, armas, varios); la
   cuarta, Vender, pasa el cursor a la mochila del inventario */
enum { SHOP_ARMOR, SHOP_WEAPONS, SHOP_MISC, SHOP_SELL, SHOP_TABS };

/* Barra de control de abajo (panel/800ctrlpnl7) */
#define CTRL_PANEL   "panel/800ctrlpnl7"
#define PANEL_TOP    (SCREEN_H - 104)
#define MIDDLE_TOP   (SCREEN_H - 55)
#define ORB_SIZE     80
#define SKILL_SIZE   48
#define BELT_STEP    31
#define STRIP_FROM   103 /* con dos jugadores, lo que se usa de la barra del medio */
#define STRIP_TO     381


typedef struct {
    float   depth;
    int16_t tx, ty; /* tile de la pared */
    int8_t  layer;  /* capa de la pared */
    int8_t  kind;
    int16_t index;  /* jugador o unidad */
} Drawable;

static Map       s_map;
static AnimData  s_animdata;
static CharClass s_classes[NUM_CLASSES] = {
    /* Equipo inicial del original (charstats.txt): hacha de mano y broquel,
       jabalina y broquel, bastón corto */
    { .token = "ba", .name = "Barbarian", .start_items = { "hax", "buc", "hp1" }, .code = "bar", .abbr = "Bar",
      .voice = "barbarian", .title = "Bárbaro", .tree = "spells/skltree_b_back" },
    { .token = "am", .name = "Amazon", .start_items = { "jav", "buc", "hp1" }, .code = "ama", .abbr = "Ama",
      .voice = "amazon", .title = "Amazona", .tree = "spells/skltree_a_back", .light = 1 },
    { .token = "so", .name = "Sorceress", .start_items = { "sst", "hp1", NULL }, .code = "sor", .abbr = "Sor",
      .voice = "sorceress", .title = "Hechicera", .tree = "spells/skltree_s_back", .light = 1 },
    /* El Paladín: espada corta y broquel */
    { .token = "pa", .name = "Paladin", .start_items = { "ssd", "buc", "hp1" }, .code = "pal", .abbr = "Pal",
      .voice = "paladin", .title = "Paladín", .tree = "spells/skltree_p_back" },
    /* El Nigromante: varita */
    { .token = "ne", .name = "Necromancer", .start_items = { "wnd", "hp1", NULL }, .code = "nec", .abbr = "Nec",
      .voice = "necromancer", .title = "Nigromante", .tree = "spells/skltree_n_back" },
    /* La Asesina: katar y broquel */
    { .token = "ai", .name = "Assassin", .start_items = { "ktr", "buc", "hp1" }, .code = "ass", .abbr = "Ass",
      .voice = "assassin", .title = "Asesina", .tree = "spells/skltree_i_back", .light = 1 },
    /* El Druida: garrote y broquel */
    { .token = "dz", .name = "Druid", .start_items = { "clb", "buc", "hp1" }, .code = "dru", .abbr = "Dru",
      .voice = "druid", .title = "Druida", .tree = "spells/skltree_d_back" },
    /* El Brujo (D2R): daga y un libro viejo (su "escudo", los grimorios) */
    { .token = "wk", .name = "Warlock", .start_items = { "dgr", "wa1", "hp1" }, .code = "war", .abbr = "War",
      .voice = "warlock", .title = "Brujo", .tree = "spells/skltree_w_back" },
};
static Player    s_pl[MAX_PLAYERS];
/* Los jugadores de esta máquina: los que manejan sus joysticks y se dibujan (uno, o dos con la
   pantalla partida). Sin red son los primeros de la partida; en red, los que le tocan */
static int  s_local[MAX_LOCAL] = { 0, 1 };
static int  s_num_local = 1;
static int  s_net_local; /* 1 = s_local lo puso la red */
static int  s_net_world; /* en red: el mundo se arma de nuevo con la semilla del anfitrión (game_start_slots) */
static int  s_zb_wait_zone = -1; /* la zona que espera a sus DT1 para empezar a armarse (zones_tick) */
static int       s_num_players;
static uint32_t  s_ticks;
static int       s_debug;
static int       s_one_hit; /* D2_ONEHIT: cada golpe de un jugador mata (pruebas) */
static int       s_god;     /* D2_GOD: los jugadores no reciben daño (pruebas); 2 = sí, pero no mueren */
static int       s_god_saves[MAX_PLAYERS]; /* con s_god 2: veces que la vida no bajó de 1 */
static float     s_start_x, s_start_y; /* donde arrancan los jugadores */
static float     s_town_x, s_town_y;   /* donde reaparecen al morir: la fogata */
static uint32_t  s_rng = 1;
static uint32_t  s_world_seed;   /* la semilla del mundo (cada acto se arma con ella) */
static int       s_act = 1;      /* el acto armado: los dos jugadores están en el mismo */

/* Misión de la Den of Evil: se cumple al matar a todos los monstruos de la cueva */
static int       s_den_cleared;
static int       s_den_light; /* ticks desde que se limpió la Den of Evil (0 = no en esta partida) */
static int       s_quest_ticks;
static char      s_quest_banner[96]; /* "¡Guarida del Mal purificada!" */
static int       s_raven_dead;       /* Cementerio de las Hermanas: murió Blood Raven */
static int       s_voice_owner = -1; /* jugador cuyo NPC está hablando */

/* Zonas en memoria: se arman al entrar (o al acercarse a un paso hacia
   ellas) y se borran cuando nadie las usa. Como mucho ZONE_MAX_LOADED a la
   vez sin contar el pueblo (que queda siempre): si hace falta otra, se
   borra la que hace más tiempo que nadie pisa. Una zona sin nadie durante
   ZONE_RESET_SECONDS también se borra, y al volver está como nueva (los
   monstruos reaparecen; el mapa sale igual en toda la partida). */
#define ZONE_MAX_LOADED    6
#define ZONE_RESET_SECONDS 600
#define ZONE_NEAR          90.0f /* a esta distancia (subtiles) de un paso se arma la zona de al lado */
#define ZONE_SOON          180.0f /* y a esta, se preparan sus DT1 en el hilo de trabajos */
#define ZONE_URGENT        30.0f  /* y a esta, si todavía se está armando, se termina de una */
#define ZONE_BUDGET        20.0   /* ms por tick para armarla en partes (D2_ZONE_BUDGET; 0 = de una) */
#define ZONE_PREP_WAIT     (2 * TICK_RATE) /* lo que se esperan sus DT1 antes de empezar */
#define ZONE_DT1_WAIT      (5 * TICK_RATE) /* y después, lo que una pieza espera los suyos (si no, los lee el juego) */
#define ZONE_SYNC_PIECES   16     /* sin el hilo de trabajos, piezas por tick en vez de ZONE_BUDGET (D2_ZONE_PIECES) */
#define ZONE_SYNC_PRESETS  64     /* y unidades de los DS1, salas con objetos al azar e intentos de grupos de monstruos */
#define ZONE_SYNC_ROOMS    8
#define ZONE_SYNC_GROUPS   16
#define ZONE_BITS          64
static uint32_t s_zone_seen[ZONE_BITS]; /* último tick con alguien adentro o cerca */
static uint64_t s_zone_pinned;          /* en uso en este tick: no se pueden borrar */
static uint64_t s_zone_prepared;        /* las de al lado que ya pidieron sus DT1 (ZONE_SOON) */
static int      s_zone_max = ZONE_MAX_LOADED;
static uint32_t s_zone_reset_ticks = ZONE_RESET_SECONDS * TICK_RATE;
static double   s_zone_budget = ZONE_BUDGET;
static int      zone_load(int z);
static int      zone_at(float x, float y);
static void     zones_unload_all(void);
static void     zones_load_all(void);
static void     depth_audit(void);
static void     skillfx_clear(void); /* skillfx.inc: sin tótems de otra partida */
static void     preload_ui(void);

/* Waypoints (el número es la columna Waypoint de levels.txt: 0..8 el Acto
   1, 9..17 el 2, 18..26 el 3, 27..29 el 4 y 30..38 el 5) y la unidad de cada
   uno en el mapa (-1 = esa zona no está armada, o es de otro acto) */
#define WP_MAX   39
#define WP_ACTS  5
#define WP_RANGE 3.5f /* a esta distancia (en subtiles) se activa y se usa */
/* El primero de cada acto (el del pueblo) y, al final, WP_MAX */
static const int k_wp_first[WP_ACTS + 1] = { 0, 9, 18, 27, 30, WP_MAX };
static int wp_first(int act)
{
    return k_wp_first[(act >= 1 && act <= WP_ACTS ? act : 1) - 1];
}
/* El acto de ese waypoint */
static int wp_act(int k)
{
    int act = 1;
    while (act < WP_ACTS && k >= k_wp_first[act])
        act++;
    return act;
}
static const char *const k_wp_levels[WP_MAX] = {
    "Rogue Encampment", "Cold Plains", "Stony Field", "Dark Wood", "Black Marsh",
    "Outer Cloister", "Jail Level 1", "Inner Cloister", "Catacombs Level 2",
    "Lut Gholein", "Sewers Level 2", "Dry Hills", "Halls of the Dead Level 2", "Far Oasis",
    "Lost City", "Palace Cellar Level 1", "Arcane Sanctuary", "Canyon of the Magi",
    "Kurast Docktown", "Spider Forest", "Great Marsh", "Flayer Jungle", "Lower Kurast",
    "Kurast Bazaar", "Upper Kurast", "Travincal", "Durance of Hate Level 2",
    "The Pandemonium Fortress", "City of the Damned", "River of Flame",
    "Harrogath", "Rigid Highlands", "Arreat Plateau", "Crystalized Cavern Level 1", "Crystalized Cavern Level 2",
    "Halls of Death's Calling", "Tundra Wastelands", "Glacial Caves Level 1", "The Worldstone Keep Level 2",
};
static int s_wp_unit[WP_MAX];
/* Los objetos waypoint: los de cada acto se llaman distinto (objpreset.txt) */
static const char *const k_wp_objects[] = { "WaypointOutsideAct1", "WaypointInsideAct1", "WaypointAct2", "WaypointCellar",
                                            "SewerWaypoint", "WaypointDesertValley", "WaypointAct3", "TravincalWaypoint",
                                            "WaypointAct4Town", "WaypointOutsideAct4", "WaypointExp", "WaypointWilderness",
                                            "WaypointIceCave", "TempleWaypoint", "WaypointBaal" };

/* Diálogos: renglones a la vista y cuánto tarda en subir cada uno */
#define TALK_LINES      9
#define TALK_WAIT       (3 * TICK_RATE)
#define TALK_LINE_TICKS (TICK_RATE * 5 / 2)

/* Pausa: el menú de Start (volver al juego, volumen, guardar y salir) */
enum { PAUSE_RESUME, PAUSE_SFX, PAUSE_MUSIC, PAUSE_RUN, PAUSE_POS, PAUSE_PERF, PAUSE_COLOR16, PAUSE_CONTROLS, PAUSE_GUIDE,
       PAUSE_QUIT, PAUSE_COUNT };
static int s_paused, s_pause_sel;
/* El registro de misiones de la pausa: de qué jugador, qué acto y qué misión */
static int      s_pause_qp, s_pause_qact, s_pause_qsel;
static uint32_t s_pause_qanim;
static int s_guide_open, s_guide_cat, s_guide_sel, s_guide_top; /* la Guía del cubo (en la pausa) */
static int s_controls_open; /* la pantalla de controles (en la pausa) */
static int s_pal_cur = -1;  /* la paleta cargada (0..4: act1..act5) */

/* La paleta del nivel donde está el jugador 1 (Pal de levels.txt): como el original, los
   infiernos de los portales rojos del Acto 5 usan la del Acto 4, y los mapas del evento
   Pandemonium las de sus actos (las baldosas de la cripta, del desierto, de Tristram) */
static void level_palette(void)
{
    const Level *lv = s_pl[s_local[0]].level;
    int want = lv && lv->pal >= 0 && lv->pal <= 4 ? lv->pal : s_act - 1;
    if (want == s_pal_cur || s_pal_cur < 0)
        return;
    char pal[16];
    snprintf(pal, sizeof pal, "act%d", want + 1);
    if (pal_load_d2(pal) == 0)
        s_pal_cur = want;
}
static int s_always_run; /* opción: corre siempre (el gatillo hace caminar) */
static int s_show_pos;   /* opción: la zona y la posición en las esquinas (para avisar errores) */
static int s_show_perf;  /* opción: cuadros por segundo y lo que tarda cada parte */
static int s_color16 = -1; /* opción: textura de 16 bits (-1 = lo que diga D2_RGB565) */
static float s_perf[6];    /* fps, lógica, la peor lógica, dibujo, subida y presentación (ms) */

void game_set_perf(float fps, float logic_ms, float worst_ms, float draw_ms, float upload_ms, float present_ms)
{
    s_perf[0] = fps;
    s_perf[1] = logic_ms;
    s_perf[2] = worst_ms;
    s_perf[3] = draw_ms;
    s_perf[4] = upload_ms;
    s_perf[5] = present_ms;
}

int game_wants_rgb565(void)
{
    return s_color16 < 0 ? -1 : s_color16;
}
static int s_demo;       /* demostración: un bot juega y no se guarda nada */
/* La campaña de la demostración del menú: los mismos personajes juegan los
   Actos 1 a 5 en Normal, después en Pesadilla y en Infierno (con
   D2_DEMO_ACT=N, solo ese acto). Al pasar de dificultad cada personaje
   sigue como en una partida guardada que se carga en la otra dificultad
   (s_carry, en memoria: no se escribe nada) */
static struct {
    int  on, act, first_diff, first_act, mid;
    int  result[3][5]; /* 1 bien, -1 mal, 0 no se jugó */
    long ticks[3][5];
} s_camp;
static char  *s_carry[MAX_PLAYERS];
static size_t s_carry_len[MAX_PLAYERS];
static void   demo_act_start(int act);
static int    demo_campaign_next(int r);
static void   demo_campaign_report(void);
static int    demo_campaign_ok(void);
/* La dificultad de la partida (pGame->nDifficulty: 0 Normal, 1 Pesadilla, 2
   Infierno; docs/plan-dificultades.md). La elige el menú (game_set_difficulty);
   D2_DIFF=1|2 para las pruebas */
static int s_diff;
static const char *const k_diff_names[3] = { "Normal", "Pesadilla", "Infierno" };
/* Lo de difficultylevels.txt para el jugador (game_set_difficulty) */
static int s_dl_resist, s_dl_res_floor = -100, s_dl_life_div = 1, s_dl_mana_div = 1, s_dl_static_min, s_dl_curse_div = 1;
static uint32_t s_anim_ticks; /* para animar el menú de pausa (el juego no avanza) */
static void draw_pentagram(int x, int y, int size, int dir);
static void draw_frame(int x, int y, int w, int h, uint8_t c);

/* Partidas guardadas: un archivo por personaje en esta carpeta (NULL = no se guarda) */
static char      s_save_dir[256];
static char      s_lang_pref[8]; /* el idioma elegido en el menú ("" = el que se importó) */
#define AUTOSAVE_SECONDS 30

/* Último monstruo golpeado: su nombre y vida se muestran arriba */
static int       s_shown_target = -1;
static int       s_shown_ticks;

/* ¿La pantalla está partida? y hacia dónde (del jugador 1 al 2) */
static int   s_split;
static float s_split_nx, s_split_ny;

/* Dónde quedó cada jugador en pantalla en el último cuadro (para el mouse) */
static float s_view_x[MAX_PLAYERS], s_view_y[MAX_PLAYERS];

static int  blocked_body(float x, float y);
static int  foot_blocked(int sx, int sy);
static void find_free_spot(float *x, float *y);
static void give_start_items(Player *p);
static void free_look(Player *p);
static void look_job_drop(int i);
static void automap_reset(void);
static void automap_reveal(const Player *p);
static void draw_button(const Player *p, int b, int x, int y, int size);
static void draw_stack_count(const Item *it, int x, int y, int w, int h);
static void refresh_look(Player *p);
static void refresh_stats(Player *p);
static int  pick_up(Player *p, const Item *it);
static int  meets_reqs(const Player *p, const ItemType *t);
static int  meets_item(const Player *p, const Item *it);
static int  equip_slot_for(const Player *p, const ItemType *t);
static int  panel_x(const Player *p);
static int  stash_side(const Player *p);
static int  nav_nearest(int count, int sel, int dx, int dy, void (*rect)(const void *, int, int *, int *, int *, int *),
                        const void *ctx);
static void inv_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h);
static void inv_entry_rect(const Player *p, int i, int *x, int *y, int *w, int *h);
static void tree_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h);
static void stats_tick(Player *p, const PlayerInput *pi);
static void half_range(const Player *p, int *x0, int *x1);
static int  wrap_text(const char *text, int max_w, int font, char lines[][128], int max_lines);
static void quest_zone_loaded(int z);
static int  hratli_moved(void);
static int  a3_npc_quest(Player *p, int who, char *key, size_t cap); /* a3quest.inc */
static int  a3_npc_news(const Player *p, int who);
static int  a3_warp_open(Player *p, const Warp *w);
static const char *a3_warp_closed(const Warp *w);
static const char *quest_action(const Player *p, int *unit);
static int  a3_use_item(Player *p, int i);
static void a3_zone_loaded(int z);
static int  a4_npc_quest(Player *p, int who, char *key, size_t cap); /* a4quest.inc */
static int  a4_npc_news(const Player *p, int who);
static void a4_zone_loaded(int z);
static int  a4_seals_left(void);
static void a5_zone_loaded(int z); /* a5quest.inc */
static void a5_portal_opened(const Player *p);
static void a5_player_died(const Player *p);
static void a5_quest_reset(void);
static int  a5_use_item(Player *p, int i);
static const char *a5_warp_closed(const Warp *w);
static int  a5_npc_quest(Player *p, int who, char *key, size_t cap);
static int  a5_npc_news(const Player *p, int who);
static void a5_dialog_closed(const Player *p, int npc);
static void a3_zone_test_prep(void);
static int  obj_passable(int u, int *x0, int *y0, int *w, int *h); /* objects.inc */
static void cow_zone_loaded(int z); /* cows.inc */
static int  cow_recipe(Player *p);
static void uber_zone_loaded(int z); /* endgame.inc */
static int  uber_recipe(Player *p);
static void player_respec(Player *p);
static void dclone_sold(Player *p, const Item *it);
static int  endgame_use_item(Player *p, int i);
static int  tz_mon_level(const Level *lv);   /* endgame.inc: las zonas de terror */
static void tz_kill(const Player *p, int unit);
static int  tz_level_on(const Level *lv);
static int  tz_waypoint_marked(const char *level_name);
static void tz_pause_line(char *out, size_t cap, int max_w);
static int  s_tz_menu;
/* Todo lo que cambia la partida sale de los ticks y de los controles, nunca de cuánto tardó algo
   (el hilo de carga, el reloj): la red (todas las máquinas calculan lo mismo) y D2_LOCKSTEP=1 en
   las pruebas. Las zonas se arman en partes por cantidad de piezas, no por milisegundos. */
static int  s_lockstep;
static const PlayerInput *s_frame; /* en red: los controles de todos para este tick (game_set_frame) */
static int  s_hash_every; /* D2_HASH=N: imprime el resumen del estado cada N ticks (game_state_hash) */
static long long s_epoch;  /* en red, los segundos (desde 1970) al empezar la partida: las zonas de terror rotan con los ticks */
static int  s_net_epoch;   /* 1 = s_epoch lo puso la red */
static int  s_eco;       /* eco.inc: el modo (game_set_mode): una corrida del Eco del Odio */
static int  s_eco_world; /* el mundo armado es el de las arenas (world_set_arena) */
static int  s_eco_exit;  /* se usó el portal del final: al menú */
static void eco_zone_loaded(int z);
static int  eco_chest_open(Player *p, int u);
static void eco_loot(const char **tc, int *lvl);
static void eco_enter(void);
static void eco_leave(void);
static int  eco_difficulty(void);
static void eco_tick(void);
static void eco_draw(void);
static int  eco_pause_quit(void);
static void eco_pause_line(char *out, size_t cap);
static int  eco_running(void);
static int  is_quest_item(const ItemType *t);
static int  bag_index_of(const Player *p, const char *code);
static int  war_kind(int kind);              /* skillwar.inc: las del Brujo */
static void war_passives(Player *p);
static int  war_take_damage(Player *p, int dmg);
static void war_consume_stats(Player *p);
static int  war_monpet_row(const char *id);
static void give_quest_item(Player *p, const char *code);
static void quest_done(const char *quest_key, const char *music);
static void quest_banner(const char *text, const char *music);
static int  palace_open(void);
static void a2_open_portals(int z);
static int  a2_npc_quest(Player *p, int who, char *key, size_t cap);
static int  a2_npc_news(const Player *p, int who);
static int  level_dark(const Level *lv);
static void party_raise(size_t field, int value);
static int  party_lacks(size_t field, int bit);
static double wall_ms(void);
static void queue_movie(const char *name);
static const char *s_pending_movie; /* la cinemática del fin de acto que toca (game_take_movie) */
static void save_party(void);
static void quest_reset(void);
static int  quest_interact(Player *p);
static void imbue_tick(Player *p, const PlayerInput *pi);
static void reading_tick(Player *p, const PlayerInput *pi);
static void quest_page_tick(Player *p, const PlayerInput *pi);
static void draw_panel_header(const Player *p, const char *title, const char *help);
static void use_warp(Player *p, const Warp *warp);
static const Warp *portal_near(const Player *p);
static int  tp_cast(Player *p, int bag_index);
static void tp_reset(void);
static void tp_zone_loaded(int z);
static void tp_used(const Player *p, const Warp *w);
static void tp_test(void);
static int  quest_first(const Player *p);
static int  warp_closed(const Warp *w);
static int  tp_open(Player *p);
static int  shrine_props(const Player *p, ItemProp *pr, int n, int cap); /* objects.inc */
static int  obj_interact(Player *p);
static int  obj_first(const Player *p);
static int  obj_pad_jumps(int x0, int y0, int x1, int y1, BotJump *out, int max);
static const char *obj_action(const Player *p, int *unit);
static int  s_stones_lit; /* Piedras Cairn tocadas en orden */

typedef struct {
    const Player *p;
    int           page;
} TreeCtx;
static int  bag_add(Player *p, const Item *it);
static int  stack_fits(const Item *into, const Item *it);
static void close_ui(Player *p);
static int  stack_cap(const ItemType *t);
static void bag_remove(Player *p, int i);
static void generate_shops(void);
static int  load_character(Player *p);
static void save_character(const Player *p);
static void act_switch(int act); /* de un acto al otro (los dos jugadores) */
static void spread_players(void);
static void build_look(const Player *p, CharLook *look, char *wclass, size_t wcap);
static void migrate_old_saves(void);
static void waypoints_find(void);
static void waypoints_light(void);
static void load_hirelings(void);
static const char *merc_monster(int type);
static void merc_apply(Player *p, int hp);
static void merc_page_tick(Player *p, const PlayerInput *pi);
static void merc_tick_players(void);

/* Las baldosas del acto, leídas por adelantado en el otro hilo
   (fs_prefetch_dir): al armar una zona nueva ya están en memoria y no se
   espera a la tarjeta SD */
static void prefetch_job(void *ptr, int act)
{
    (void)ptr;
    char dir[32];
    if (act == 5) /* (las del Acto 5 son las de la expansión) */
        snprintf(dir, sizeof dir, "data/global/tiles/expansion");
    else
        snprintf(dir, sizeof dir, "data/global/tiles/act%d", act);
    fs_prefetch_dir(dir);
}

static void prefetch_act_tiles(int act)
{
    jobs_push(prefetch_job, NULL, act);
}

static uint32_t rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

#include "bossfx.inc"

/* Valores de la clase en charstats.txt y experience.txt */
static void load_class_stats(CharClass *c, const TxtTable *cs, const TxtTable *xp)
{
    int row = txt_find_row(cs, txt_col(cs, "class"), c->name);
    c->stamina = (float)txt_get_int(cs, row, "stamina", 84);
    c->run_drain = txt_get_int(cs, row, "RunDrain", 20) / 256.0f;
    c->walk_speed = txt_get_int(cs, row, "WalkVelocity", 6) * SPEED_PER_VELOCITY;
    c->run_speed = txt_get_int(cs, row, "RunVelocity", 9) * SPEED_PER_VELOCITY;
    c->life = txt_get_int(cs, row, "vit", 20) + txt_get_int(cs, row, "hpadd", 30);
    c->mana = txt_get_int(cs, row, "int", 10);
    c->str = txt_get_int(cs, row, "str", 20);
    c->dex = txt_get_int(cs, row, "dex", 20);
    c->to_hit = txt_get_int(cs, row, "ToHitFactor", 0);
    /* Los "por nivel" y "por punto" están en cuartos */
    c->vit = txt_get_int(cs, row, "vit", 20);
    c->energy = txt_get_int(cs, row, "int", 10);
    c->life_per_level = txt_get_int(cs, row, "LifePerLevel", 8) / 4;
    c->life_per_vit4 = txt_get_int(cs, row, "LifePerVitality", 12);
    c->mana_per_level4 = txt_get_int(cs, row, "ManaPerLevel", 4);
    c->mana_per_energy4 = txt_get_int(cs, row, "ManaPerMagic", 4);
    c->stamina_per_level4 = txt_get_int(cs, row, "StaminaPerLevel", 4);
    c->stamina_per_vit4 = txt_get_int(cs, row, "StaminaPerVitality", 4);
    c->stamina_per_level = c->stamina_per_level4 / 4.0f;
    c->mana_regen = (float)txt_get_int(cs, row, "ManaRegen", 120);
    c->block_factor = txt_get_int(cs, row, "BlockFactor", 20);

    int col = txt_col(xp, c->name);
    for (int lv = 0; lv < 100; lv++)
        c->exp_next[lv] = 0xFFFFFFFFu;
    for (int r = 1; r < xp->rows; r++) {
        const char *lv = txt_get(xp, r, 0);
        if (lv[0] < '0' || lv[0] > '9')
            continue; /* la fila "MaxLvl" */
        int l = atoi(lv);
        if (l >= 0 && l < 100)
            c->exp_next[l] = (uint32_t)strtoul(txt_get(xp, r, col), NULL, 10);
    }
}

static int load_class(CharClass *c)
{
    CharLook look;
    memset(&look, 0, sizeof look);
    snprintf(look.token, sizeof look.token, "%s", c->token);
    static const int lit[] = { COMP_HD, COMP_TR, COMP_LG, COMP_RA, COMP_LA, COMP_S1, COMP_S2 };
    for (unsigned i = 0; i < sizeof lit / sizeof lit[0]; i++)
        strcpy(look.part[lit[i]], "lit");
    for (int m = 0; m < MODE_COUNT; m++)
        if (m != MODE_A2 && m != MODE_BL && /* el segundo ataque solo existe con arma; bloquear, con escudo */
            charanim_load(&c->anim[m], &look, k_mode_codes[m], "hth") != 0 && m <= MODE_WL)
            return -1;

    /* "barbarian_hit_1", "amazon_death_1"... y pasos (la Amazona pisa más liviano) */
    char key[48], cls[16];
    size_t k = 0;
    for (; c->name[k] && k + 1 < sizeof cls; k++)
        cls[k] = (char)(c->name[k] | 0x20);
    cls[k] = '\0';
    snprintf(key, sizeof key, "%s_hit_1", cls);
    c->snd_hit = audio_find(key);
    snprintf(key, sizeof key, "%s_death_1", cls);
    c->snd_death = audio_find(key);
    const char *weight = c->light ? "light" : "medium";
    for (int cave = 0; cave < 2; cave++)
        for (int run = 0; run < 2; run++) {
            snprintf(key, sizeof key, "%s_%s_%s_1", weight, run ? "run" : "walk", cave ? "istone" : "dirt");
            c->snd_step[cave][run] = audio_find(key);
            audio_preload(c->snd_step[cave][run]);
        }
    audio_preload(c->snd_hit);
    audio_preload(c->snd_death);
    return 0;
}

static int monster_level_at(float x, float y)
{
    return tz_mon_level(world_level_at(x, y)); /* (el de la zona de terror, si lo está) */
}

static int level_monster_count(const Level *lv)
{
    int n = 0;
    while (n < LEVEL_MAX_MONSTERS && lv->monsters[n])
        n++;
    return n;
}

/* Un monstruo cualquiera de la lista del nivel de ese lugar (para los
   grupos que marcan los DS1) */
static const char *pick_level_monster(float x, float y)
{
    const Level *lv = world_level_at(x, y);
    int n = lv ? level_monster_count(lv) : 0;
    return n > 0 ? lv->monsters[rnd() % (uint32_t)n] : NULL;
}

/* Grupos de monstruos al azar en un nivel recién armado. Como en el
   original, de la lista de levels.txt salen solo NumMon tipos cada vez. Va
   de a partes (zone_monsters_step): el nivel que se está poblando, sus tipos
   mezclados y por qué grupo e intento va. */
static struct {
    const Level *lv; /* NULL = ninguno */
    const char  *kinds[LEVEL_MAX_MONSTERS];
    int          nkinds, bosses, g, tries;
} s_zm;

static void zone_monsters_begin(const Level *lv)
{
    int total = level_monster_count(lv);
    s_zm.lv = NULL;
    if (lv->town || total == 0 || lv->num_mon == 0)
        return;
    const char **kinds = s_zm.kinds;
    for (int k = 0; k < total; k++)
        kinds[k] = lv->monsters[k];
    for (int k = 0; k < total - 1; k++) { /* mezclar y quedarse con los primeros */
        int j = k + (int)(rnd() % (uint32_t)(total - k));
        const char *t = kinds[k];
        kinds[k] = kinds[j];
        kinds[j] = t;
    }
    s_zm.lv = lv;
    s_zm.nkinds = lv->num_mon < total ? lv->num_mon : total;
    /* Los jefes al azar del nivel, MonUMin..MonUMax de levels.txt contando los
       de nombre que ya puso (MONSTERCHOOSE_GetBossSpawnType): el mínimo se
       completa a medida que se puebla, y hasta el máximo, con un 6% en cada grupo */
    s_zm.bosses = units_bosses_in((float)(lv->x0 * TILE_SUB), (float)(lv->y0 * TILE_SUB), (float)(lv->x1 * TILE_SUB),
                                  (float)(lv->y1 * TILE_SUB));
    s_zm.g = s_zm.tries = 0;
}

/* Un intento de poner un grupo; 0 = el nivel ya está */
static int zone_monsters_try(void)
{
    const Level *lv = s_zm.lv;
    if (s_zm.g >= lv->groups || s_zm.tries >= 600) {
        if (s_debug || getenv("D2_ZONES"))
            printf("  %s: %d jefes (MonUMin..MonUMax %d..%d)\n", lv->name, s_zm.bosses, lv->umin, lv->umax);
        s_zm.lv = NULL;
        return 0;
    }
    s_zm.tries++;
    float x = (lv->x0 + 3 + (int)(rnd() % (uint32_t)(lv->x1 - lv->x0 - 6))) * (float)TILE_SUB;
    float y = (lv->y0 + 3 + (int)(rnd() % (uint32_t)(lv->y1 - lv->y0 - 6))) * (float)TILE_SUB;
    if (blocked_body(x + 0.5f, y + 0.5f))
        return 1;
    int g = ++s_zm.g, nkinds = s_zm.nkinds;
    const char *const *kinds = s_zm.kinds;
    if (lv->num_umon > 0 && ((s_zm.bosses < lv->umin && (int)(rnd() % 100) < 100 * g / lv->groups) ||
                             (s_zm.bosses < lv->umax && rnd() % 100 <= 5))) {
        /* (en Normal, de los umon1..umon10 del nivel) */
        s_zm.bosses += units_spawn_level_boss(lv->umons[rnd() % (uint32_t)lv->num_umon], x + 0.5f, y + 0.5f, tz_mon_level(lv));
        return 1;
    }
    const char *id = kinds[rnd() % (uint32_t)nkinds];
    /* Los Caídos andan en banda y el chamán va con los suyos (el de
       Carvers con Carvers); los nidos de cuervos, solos */
    int shaman = strncmp(id, "fallenshaman", 12) == 0;
    int band = shaman || strncmp(id, "fallen", 6) == 0;
    char minion[24];
    snprintf(minion, sizeof minion, "fallen%s", shaman ? id + 12 : "1");
    int count = band ? 3 + (int)(rnd() % 3) : strncmp(id, "crownest", 8) == 0 ? 1 : 1 + (int)(rnd() % 3);
    int herd = strcmp(id, "hellbovine") == 0, spread = 5;
    if (herd) { /* las vacas, en manada: MinGrp..MaxGrp de monstats (5 a 10), como en el original */
        int lo = units_monstats_int(id, "MinGrp", 5), hi = units_monstats_int(id, "MaxGrp", 10);
        count = lo + (hi > lo ? (int)(rnd() % (uint32_t)(hi - lo + 1)) : 0);
        spread = 7;
    }
    for (int k = 0; k < count; k++) {
        float mx = x + (float)((int)(rnd() % (uint32_t)spread) - spread / 2),
              my = y + (float)((int)(rnd() % (uint32_t)spread) - spread / 2);
        if (!blocked_body(mx + 0.5f, my + 0.5f) && world_level_at(mx, my) == lv)
            units_spawn_monster(shaman && k > 0 ? minion : id, mx + 0.5f, my + 0.5f, tz_mon_level(lv));
    }
    return 1;
}

/* Los monstruos de los niveles de la zona z, de a partes: `begin` empieza;
   hasta `cap` intentos (0 = sin tope) o hasta pasarse `ms` milisegundos (0
   = sin hora), por lo menos uno. 1 = terminó. */
static int zone_monsters_step(int z, int begin, int cap, double ms)
{
    static int next; /* el nivel que sigue */
    if (begin) {
        next = 0;
        s_zm.lv = NULL;
    }
    if (s_eco) /* (en el Eco, los monstruos son los de las oleadas) */
        return 1;
    double until = ms > 0 ? wall_ms() + ms : 0;
    for (int done = 0;;) {
        if (!s_zm.lv) {
            while (next < world_level_count() && world_level(next)->zone != z)
                next++;
            if (next >= world_level_count())
                return 1;
            zone_monsters_begin(world_level(next++));
            continue;
        }
        if (done > 0 && ((cap > 0 && done >= cap) || (until > 0 && wall_ms() > until)))
            return 0;
        done += zone_monsters_try();
    }
}

/* El Id de levels.txt de un lugar (para las IA: los zombis del Burial
   Grounds, los tentáculos de cada nivel) */
static int level_id_at(float x, float y)
{
    const Level *lv = world_level_at(x, y);
    return lv ? lv->id : 0;
}

/* D2_ZONETEST: los subtiles de los barriles, urnas y cestas que se rompen o
   se abren (obj_passable): se pasa por ahí rompiéndolos */
static uint8_t *s_walk_open;
static int      s_walk_zone = -1; /* walk_bfs sin salir de esa zona (-1 = por todo el mapa) */

/* Como foot_blocked, pero sin lo que tapan los de s_walk_open */
static int walk_foot_blocked(int sx, int sy)
{
    static const int dx[5] = { 0, -1, 1, 0, 0 }, dy[5] = { 0, 0, 0, -1, 1 };
    int w = s_map.width * TILE_SUB, h = s_map.height * TILE_SUB;
    for (int k = 0; k < 5; k++) {
        int x = sx + dx[k], y = sy + dy[k];
        uint16_t c = map_coll(&s_map, x, y);
        if (s_walk_open && x >= 0 && y >= 0 && x < w && y < h && s_walk_open[(size_t)y * (size_t)w + (size_t)x])
            c &= (uint16_t)~COLL_OBJECT;
        if (c & COLL_MASK_PLAYER_PATH)
            return 1;
    }
    return 0;
}

/* Distancia caminando (en subtiles) desde (fx, fy) a todo el mapa, o NULL
   sin memoria. Solo para pruebas (recorre todo el mapa). */
static int *walk_bfs(float fx, float fy)
{
    int w = s_map.width * TILE_SUB, h = s_map.height * TILE_SUB;
    int *dist = malloc((size_t)w * (size_t)h * sizeof *dist), *queue = malloc((size_t)w * (size_t)h * sizeof *queue);
    if (!dist || !queue) {
        free(dist);
        free(queue);
        return NULL;
    }
    for (int k = 0; k < w * h; k++)
        dist[k] = -1;
    int qh = 0, qt = 0, start = (int)fy * w + (int)fx;
    if (fx >= 0 && fy >= 0 && fx < w && fy < h) {
        dist[start] = 0;
        queue[qt++] = start;
    }
    while (qh < qt) {
        int c = queue[qh++], cx = c % w, cy = c / w;
        /* también en diagonal, como el camino del original */
        static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        for (int d = 0; d < 8; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || dist[ny * w + nx] >= 0 || walk_foot_blocked(nx, ny) ||
                (s_walk_zone >= 0 && zone_at(nx + 0.5f, ny + 0.5f) != s_walk_zone))
                continue;
            dist[ny * w + nx] = dist[c] + 1;
            queue[qt++] = ny * w + nx;
        }
    }
    free(queue);
    return dist;
}

/* La menor distancia a algún subtile a menos de `r` de (tx, ty), o -1. El
   destino puede estar pisando algo bloqueado: alcanza con llegar al lado. */
static int walk_dist(const int *dist, float tx, float ty, int r)
{
    int w = s_map.width * TILE_SUB, h = s_map.height * TILE_SUB, best = -1;
    for (int yy = (int)ty - r; yy <= (int)ty + r; yy++)
        for (int xx = (int)tx - r; xx <= (int)tx + r; xx++)
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && dist[yy * w + xx] >= 0 && (best < 0 || dist[yy * w + xx] < best))
                best = dist[yy * w + xx];
    return best;
}

/* La menor distancia a algún subtile libre dentro del rectángulo, o -1 */
static int walk_rect(const int *dist, float x0, float y0, float x1, float y1)
{
    int w = s_map.width * TILE_SUB, h = s_map.height * TILE_SUB, best = -1;
    for (int yy = (int)floorf(y0); yy < (int)ceilf(y1); yy++)
        for (int xx = (int)floorf(x0); xx < (int)ceilf(x1); xx++)
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && dist[yy * w + xx] >= 0 && (best < 0 || dist[yy * w + xx] < best))
                best = dist[yy * w + xx];
    return best;
}

/* Largo del camino más corto caminando entre dos puntos, o -1 si no se llega */
static int path_length(float fx, float fy, float tx, float ty)
{
    int *dist = walk_bfs(fx, fy);
    int best = dist ? walk_dist(dist, tx, ty, 3) : -1;
    free(dist);
    return best;
}

/* D2_ZONETEST: arma todas las zonas y, en cada una, prueba que desde la
   primera entrada se llegue caminando a las demás entradas y salidas, a
   los pasillos, al waypoint y a los jefes y objetos de las misiones */
/* ¿Hay un waypoint (de cualquier acto) en la zona? */
static int waypoint_in_zone(int z)
{
    for (unsigned k = 0; k < sizeof k_wp_objects / sizeof k_wp_objects[0]; k++) {
        int ids[8], n = units_find_all(k_wp_objects[k], ids, 8);
        for (int i = 0; i < n; i++)
            if (units_zone(ids[i]) == z)
                return 1;
    }
    return 0;
}

/* Adonde llega el portal del orificio en la guarida de Duriel: la marca de duriel.ds1 */
static void lair_arrival(int lair, float *x, float *y)
{
    int x0, y0, x1, y1;
    world_zone_rect(lair, &x0, &y0, &x1, &y1);
    *x = (float)((x0 + 27) * TILE_SUB) + 2.5f;
    *y = (float)((y0 + 38) * TILE_SUB) + 2.5f;
}

/* D2_NAVCHECK="x,y" (con D2_ZONETEST): desde ese punto, lo que se alcanza
   con el pie solo (lo que mira la prueba de zonas) y con el aire de las
   paredes (lo que mira el movimiento de verdad, player_blocked, y el de los
   bots); los primeros lugares donde el pie pasa y el aire no, al borde de
   lo que se alcanza de verdad */
/* ¿Se llega caminando de (fx, fy) a (tx, ty) en el mapa? (con el pie del
   jugador y las almohadillas del Santuario Arcano, sin mirar las unidades:
   para decir en el informe de la prueba si un monstruo al que el bot no
   llegó está en un rincón cerrado o es cosa del bot). *region: los subtiles
   a los que se llega desde el principio */
static int map_reach(float fx, float fy, float tx, float ty, int *region)
{
    int w = s_map.width * TILE_SUB, h = s_map.height * TILE_SUB, found = 0, qh = 0, qt = 0;
    int sx = (int)fx, sy = (int)fy, gx = (int)tx, gy = (int)ty;
    *region = 0;
    if (sx < 0 || sy < 0 || sx >= w || sy >= h)
        return 0;
    uint8_t *seen = calloc((size_t)w * (size_t)h, 1);
    int *queue = malloc((size_t)w * (size_t)h * sizeof *queue);
    if (!seen || !queue) {
        free(seen);
        free(queue);
        return 0;
    }
    static BotJump jumps[128];
    int nj = obj_pad_jumps(0, 0, w, h, jumps, 128);
    static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    queue[qt++] = sy * w + sx;
    seen[queue[0]] = 1;
    while (qh < qt) {
        int c = queue[qh++], cx = c % w, cy = c / w;
        found |= abs(cx - gx) <= 1 && abs(cy - gy) <= 1; /* (al lado alcanza: el monstruo puede estar donde no pisa el jugador) */
        for (int d = 0; d < 8 + nj; d++) {
            int nx = cx + dx[d < 8 ? d : 0], ny = cy + dy[d < 8 ? d : 0];
            if (d >= 8) { /* al alcance de una almohadilla: adonde lleva */
                const BotJump *j = &jumps[d - 8];
                float ox = cx + 0.5f < j->x0 ? j->x0 - cx - 0.5f : cx + 0.5f > j->x1 ? cx + 0.5f - j->x1 : 0.0f;
                float oy = cy + 0.5f < j->y0 ? j->y0 - cy - 0.5f : cy + 0.5f > j->y1 ? cy + 0.5f - j->y1 : 0.0f;
                if (ox * ox + oy * oy > j->reach * j->reach)
                    continue;
                nx = (int)j->lx, ny = (int)j->ly;
            }
            int i = ny * w + nx;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[i] || walk_foot_blocked(nx, ny))
                continue;
            seen[i] = 1;
            queue[qt++] = i;
        }
    }
    *region = qt;
    free(seen);
    free(queue);
    return found;
}

/* ¿(x, y) está a un subtile o menos de algo marcado `v` en seen? (al lado
   alcanza, como en map_reach) */
static int nav_near(const uint8_t *seen, int w, int h, int x, int y, uint8_t v)
{
    for (int yy = y - 1; yy <= y + 1; yy++)
        for (int xx = x - 1; xx <= x + 1; xx++)
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && seen[(size_t)yy * (size_t)w + (size_t)xx] == v)
                return 1;
    return 0;
}

/* D2_NAVZONE=1: solo dentro de la zona del punto de partida (¿se llega sin
   pasar por la vecina?). D2_NAVDRAW=r: el dibujo de r subtiles alrededor del
   destino (o de la partida): # no se camina, v grosor, o se llega (con el
   aire), : solo con el pie, p el hueco del destino, . libre sin llegar */
static void nav_check(void)
{
    float fx, fy, tx = -1, ty = -1;
    if (!getenv("D2_NAVCHECK") || sscanf(getenv("D2_NAVCHECK"), "%f,%f,%f,%f", &fx, &fy, &tx, &ty) < 2)
        return;
    int w = s_map.width * TILE_SUB, h = s_map.height * TILE_SUB;
    if (fx < 0 || fy < 0 || fx >= w || fy >= h) /* (otro acto: el mapa es más chico) */
        return;
    int zone = getenv("D2_NAVZONE") && atoi(getenv("D2_NAVZONE")) ? zone_at(fx, fy) : -2;
    uint8_t *seen = calloc((size_t)w * (size_t)h, 1); /* 1 con el pie, 2 también con el aire, 3 el hueco del destino */
    int *queue = malloc((size_t)w * (size_t)h * sizeof *queue);
    if (!seen || !queue) {
        free(seen);
        free(queue);
        return;
    }
    static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int dest_in = tx >= 0 && ty >= 0 && tx < w && ty < h;
    for (int pass = 1; pass <= 2; pass++) {
        int qh = 0, qt = 0, foot = 0, shown = 0;
        queue[qt++] = (int)fy * w + (int)fx;
        seen[queue[0]] = (uint8_t)pass;
        while (qh < qt) {
            int c = queue[qh++], cx = c % w, cy = c / w;
            foot++;
            for (int d = 0; d < 8; d++) {
                int nx = cx + dx[d], ny = cy + dy[d], i = ny * w + nx;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[i] >= pass || walk_foot_blocked(nx, ny) ||
                    (zone != -2 && zone_at(nx + 0.5f, ny + 0.5f) != zone))
                    continue;
                if (pass == 2 && map_too_close(&s_map, nx + 0.5f, ny + 0.5f)) {
                    if (shown++ < 12 && seen[i] == 1)
                        printf("NAV: el aire cierra (%d, %d), al lado de (%d, %d)\n", nx, ny, cx, cy);
                    continue;
                }
                seen[i] = (uint8_t)pass;
                queue[qt++] = i;
            }
        }
        printf("NAV: desde (%.0f, %.0f) %s%s: %d subtiles%s\n", fx, fy, pass == 1 ? "con el pie" : "con el aire",
               zone != -2 ? " sin salir de la zona" : "", foot,
               !dest_in                                       ? ""
               : nav_near(seen, w, h, (int)tx, (int)ty, (uint8_t)pass) ? ", llega al destino"
                                                              : ", NO llega al destino");
    }
    /* Si no llega: el hueco del destino (lo que se alcanza desde el subtile
       libre más cerca de él), cuánto mide y dónde está */
    if (dest_in && !nav_near(seen, w, h, (int)tx, (int)ty, 1)) {
        int sx = -1, sy = -1;
        for (int r = 0; r <= 3 && sx < 0; r++)
            for (int yy = (int)ty - r; yy <= (int)ty + r && sx < 0; yy++)
                for (int xx = (int)tx - r; xx <= (int)tx + r && sx < 0; xx++)
                    if (xx >= 0 && yy >= 0 && xx < w && yy < h && !walk_foot_blocked(xx, yy) && !seen[yy * w + xx])
                        sx = xx, sy = yy;
        if (sx < 0) {
            printf("NAV: el destino no tiene dónde pararse a 3 subtiles\n");
        } else {
            int qh = 0, qt = 0, x0 = sx, y0 = sy, x1 = sx, y1 = sy, touch = 0;
            queue[qt++] = sy * w + sx;
            seen[queue[0]] = 3;
            while (qh < qt && qt < 4000000) {
                int c = queue[qh++], cx = c % w, cy = c / w;
                x0 = cx < x0 ? cx : x0, y0 = cy < y0 ? cy : y0, x1 = cx > x1 ? cx : x1, y1 = cy > y1 ? cy : y1;
                for (int d = 0; d < 8; d++) {
                    int nx = cx + dx[d], ny = cy + dy[d], i = ny * w + nx;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h || walk_foot_blocked(nx, ny))
                        continue;
                    touch |= seen[i] == 1 || seen[i] == 2; /* (solo con D2_NAVZONE: se une por otra zona) */
                    if (seen[i])
                        continue;
                    seen[i] = 3;
                    queue[qt++] = i;
                }
            }
            printf("NAV: el destino está en un hueco de %d subtiles, de (%d, %d) a (%d, %d)%s\n", qt, x0, y0, x1, y1,
                   touch ? " que toca lo alcanzado (por otra zona)" : "");
        }
    }
    int r = getenv("D2_NAVDRAW") ? atoi(getenv("D2_NAVDRAW")) : 0;
    int ax = dest_in ? (int)tx : (int)fx, ay = dest_in ? (int)ty : (int)fy;
    for (int y = ay - r; r > 0 && y <= ay + r; y++) {
        printf("NAV y=%4d ", y);
        for (int x = ax - r; x <= ax + r; x++) {
            uint8_t s = x >= 0 && y >= 0 && x < w && y < h ? seen[y * w + x] : 0;
            putchar(x == (int)fx && y == (int)fy               ? 'S'
                    : dest_in && x == (int)tx && y == (int)ty ? 'D'
                    : map_blocked(&s_map, x, y)                ? '#'
                    : s == 2                                   ? 'o'
                    : s == 1                                   ? ':'
                    : s == 3                                   ? 'p'
                    : map_coll(&s_map, x, y) & COLL_VOLUME     ? 'v'
                                                               : '.');
        }
        putchar('\n');
    }
    /* Los que se rompen o se abren y tocan lo que se alcanza: los que pueden cerrar el paso */
    for (int u = 0; u < units_count(); u++) {
        int ox, oy, ow, oh, touch = 0;
        if (!obj_passable(u, &ox, &oy, &ow, &oh))
            continue;
        for (int y = oy - 2; y < oy + oh + 2 && !touch; y++)
            for (int x = ox - 2; x < ox + ow + 2 && !touch; x++)
                touch = x >= 0 && y >= 0 && x < w && y < h && seen[(size_t)y * (size_t)w + (size_t)x] == 2;
        if (touch)
            printf("NAV: al borde, %s en (%d, %d)\n", units_id(u), ox, oy);
    }
    free(seen);
    free(queue);
}

static void zone_test(void)
{
    if (s_act == 3) /* con el puente de Mefisto arriba: también se prueba el portal rojo */
        a3_zone_test_prep();
    zones_load_all();
    nav_check();
    static const char *const targets[] = {
        "WaypointOutsideAct1", "WaypointInsideAct1", "Corpsefire", "bloodraven", "Rakanishu", "StoneAlpha",
        "StoneBeta", "StoneGamma", "StoneDelta", "StoneLambda", "Treehead WoodFist", "InifussTree", "TowerTome",
        "The Countess", "The Smith", "Malus", "Griswold", "Gibbet", "andariel", "Pitspawn Fouldog", "Boneash",
        "WaypointAct2", "WaypointDesertValley", "WaypointCellar", "SewerWaypoint", "Beetleburst", "Dark Elder",
        "Radament", "HoradricScrollChest", "Bloodwitch the Wild", "HoradricCubeChest", "Fangskin", "TaintedSunShrine",
        "Coldworm the Burrower", "StaffOfKingsChest", "Leatherarm", "ConsolationChest", "WaypointAct3",
        "TravincalWaypoint", "Sszark the Burning", "Witch Doctor Endugu", "Stormtree", "Sarina the Battlemaid",
        "Icehawk Riftwing", "Ismail Vilehand", "Geleb Flamefinger", "Bremm Sparkfist", "Toorc Icefist",
        "Wyand Voidfinger", "Maffer Dragonhand", "mephisto", "LamesEnsTome", "KhalimHeartChest", "KhalimEyeChest",
        "KhalimBrainChest", "TownAct3GidBInnAltar", "BeneathTheCityLever", "HellGate", "WaypointAct4Town",
        "WaypointOutsideAct4", "tyrael2", "jamella", "halbu", "cain4", "Bank", "izual", "The Feature Creep",
        "SoulStoneForge", "DiabloSeal1", "DiabloSeal2", "DiabloSeal3", "DiabloSeal4", "DiabloSeal5", "DiabloStart",
        "WaypointExp", "WaypointWilderness", "WaypointIceCave", "TempleWaypoint", "WaypointBaal", "prisondoor",
        "PortalPermanent", "AncientsAltar", "AncientsGateway", "larzuk", "malah", "qual-kehk", "nihlathak", "cain6",
        "The Cow King", "Wirt",
    };
    int bad = 0, mw = s_map.width * TILE_SUB, mh = s_map.height * TILE_SUB;
    s_walk_open = calloc((size_t)mw * (size_t)mh, 1);
    for (int u = 0; s_walk_open && u < units_count(); u++) {
        int ox, oy, ow, oh;
        if (obj_passable(u, &ox, &oy, &ow, &oh))
            for (int y = oy; y < oy + oh; y++)
                for (int x = ox; x < ox + ow; x++)
                    if (x >= 0 && y >= 0 && x < mw && y < mh)
                        s_walk_open[(size_t)y * (size_t)mw + (size_t)x] = 1;
    }
    for (int z = 0; z < world_zone_count(); z++) {
        float sx = -1, sy = -1, x, y;
        /* La entrada: la llegada de un salto o un pasillo hacia esta zona */
        for (int i = 0; i < world_warp_count() && sx < 0; i++)
            if (world_warp(i)->to == z && world_warp_arrival(world_warp(i), &x, &y) == 0) {
                find_free_spot(&x, &y);
                sx = x;
                sy = y;
            }
        if (sx < 0 && world_zone_is_town(z) && units_find("RogueBonfire", &sx, &sy) == 0)
            sy += 5.0f;
        if (sx < 0 && s_act == 2 && z == world_level_zone(73)) { /* la guarida: solo por el portal del orificio */
            lair_arrival(z, &sx, &sy);
            find_free_spot(&sx, &sy);
        }
        if (sx < 0 && s_act == 1 && z == world_level_zone(39) && world_cow_arrival(&sx, &sy) == 0) /* la granja: por el portal del cubo */
            find_free_spot(&sx, &sy);
        for (int l = 0; l < world_link_count() && sx < 0; l++) {
            const WorldLink *wl = world_link(l);
            for (int r = 0; r < 60 && sx < 0; r++)
                for (int a = 0; a < 16 && sx < 0; a++) {
                    x = wl->x + cosf(a * PI_F / 8) * r;
                    y = wl->y + sinf(a * PI_F / 8) * r;
                    if ((wl->a == z || wl->b == z) && zone_at(x, y) == z && !blocked_body(x, y))
                        sx = x, sy = y;
                }
        }
        if (sx < 0) {
            printf("zona %s: sin entrada\n", world_zone_name(z));
            bad++;
            continue;
        }
        int *dist = walk_bfs(sx, sy);
        if (!dist) {
            free(s_walk_open);
            s_walk_open = NULL;
            return;
        }
        char problems[512] = "";
        size_t len = 0;
        int checks = 0;
        for (int i = 0; i < world_warp_count(); i++) {
            const Warp *wp = world_warp(i);
            if (wp->from == z) {
                checks++;
                if (walk_rect(dist, wp->x0 - wp->margin, wp->y0 - wp->margin, wp->x1 + wp->margin, wp->y1 + wp->margin) < 0)
                    len += (size_t)snprintf(problems + len, sizeof problems - len, " salida a %s;", world_zone_name(wp->to));
            }
            if (wp->to == z && world_warp_arrival(wp, &x, &y) == 0) {
                checks++;
                find_free_spot(&x, &y);
                if (walk_dist(dist, x, y, 2) < 0)
                    len += (size_t)snprintf(problems + len, sizeof problems - len, " llegada de %s;", world_zone_name(wp->from));
            }
            if (len > sizeof problems - 64)
                break;
        }
        /* Cada unión: que se llegue y que esté sobre un cruce que entra a las
           dos zonas (link_settle; en la semilla 43 del Acto 3 el punto en el
           medio del río mandaba al bot a un rincón del Spider Forest y volvía).
           Sin salir de la zona, solo un aviso: en la selva hay claros que el
           armado cuelga solo de la selva vecina (semilla 54: el claro de arriba
           del Spider Forest se une al resto por el Flayer Jungle) */
        int *zdist = NULL;
        for (int l = 0; l < world_link_count() && len < sizeof problems - 64; l++) {
            const WorldLink *wl = world_link(l);
            if (wl->a != z && wl->b != z)
                continue;
            checks++;
            if (!zdist) {
                s_walk_zone = z;
                zdist = walk_bfs(sx, sy);
                s_walk_zone = -1;
            }
            if (walk_dist(dist, wl->x, wl->y, 8) < 0 || wl->cross == -1)
                len += (size_t)snprintf(problems + len, sizeof problems - len, " pasillo a %s%s;",
                                        world_zone_name(wl->a == z ? wl->b : wl->a), wl->cross == -1 ? " (sin cruce)" : "");
            else if (zdist && walk_dist(zdist, wl->x, wl->y, wl->cross > 0 ? 4 : 8) < 0)
                printf("  aviso: en %s el pasillo a %s se alcanza solo pasando por otra zona\n", world_zone_name(z),
                       world_zone_name(wl->a == z ? wl->b : wl->a));
        }
        free(zdist);
        for (unsigned t = 0; t < sizeof targets / sizeof targets[0] && len < sizeof problems - 64; t++) {
            int ids[8], n = units_find_all(targets[t], ids, 8);
            for (int k = 0; k < n; k++) {
                if (units_zone(ids[k]) != z)
                    continue;
                checks++;
                units_position(ids[k], 1.0f, &x, &y);
                if (getenv("D2_WARPS")) /* (con la lista de saltos: también los jefes y objetos que encontró) */
                    printf("  %s en %s\n", targets[t], world_zone_name(z));
                if (walk_dist(dist, x, y, 4) < 0)
                    len += (size_t)snprintf(problems + len, sizeof problems - len, " %s;", targets[t]);
            }
        }
        for (int i = 0; i < world_level_count() && len < sizeof problems - 64; i++) { /* el waypoint de cada nivel que tiene */
            const Level *lv = world_level(i);
            if (lv->zone == z && lv->waypoint >= 0 && !waypoint_in_zone(z))
                len += (size_t)snprintf(problems + len, sizeof problems - len, " sin el waypoint de %s;", lv->name);
        }
        if (world_zone_is_town(z)) { /* desde el pueblo, caminando: los pasillos de todo el acto */
            int seen = 0;
            for (int l = 0; l < world_link_count(); l++) {
                const WorldLink *wl = world_link(l);
                if (walk_dist(dist, wl->x, wl->y, 8) >= 0)
                    seen++;
                else
                    printf("  desde el pueblo no se llega caminando al pasillo de %s a %s\n", world_zone_name(wl->a),
                           world_zone_name(wl->b));
            }
            printf("  desde el pueblo se llega caminando a %d de %d pasillos\n", seen, world_link_count());
            /* Y a los waypoints de las zonas unidas al pueblo por pasillos (el
               de los pasillos mira a 8 subtiles: ve los dos lados de una pared fina) */
            int linked[64] = { 0 }, more = 1;
            linked[z] = 1;
            while (more) {
                more = 0;
                for (int l = 0; l < world_link_count(); l++) {
                    const WorldLink *wl = world_link(l);
                    if (wl->a < 64 && wl->b < 64 && linked[wl->a] != linked[wl->b])
                        linked[wl->a] = linked[wl->b] = more = 1;
                }
            }
            for (int w = 0; w < WP_MAX; w++) {
                float wx, wy;
                int wz = world_waypoint_zone(w);
                if (wz < 0 || wz >= 64 || !linked[wz] || wz == z || s_wp_unit[w] < 0)
                    continue;
                units_position(s_wp_unit[w], 1.0f, &wx, &wy);
                if (walk_dist(dist, wx, wy, 4) < 0) {
                    printf("  desde el pueblo no se llega caminando al waypoint de %s\n", world_zone_name(wz));
                    len += (size_t)snprintf(problems + len, sizeof problems - len, " waypoint de %s;", world_zone_name(wz));
                }
            }
        }
        { /* Los tiles a los que se llega y no tienen nada dibujado, o solo
             paredes (sin piso: el vacío debajo de un arco), como dato (en el
             original, FillBlanks los tapa) */
            int x0, y0, x1, y1, bare = 0, nofloor = 0, bx = 0, by = 0, nx = 0, ny = 0;
            world_zone_rect(z, &x0, &y0, &x1, &y1);
            for (int ty = y0; ty < y1; ty++)
                for (int tx = x0; tx < x1; tx++) {
                    int reach = 0, floor = 0, wall = 0;
                    for (int k = 0; k < 25 && !reach; k++)
                        reach = dist[(size_t)(ty * TILE_SUB + k / 5) * (size_t)(s_map.width * TILE_SUB) +
                                     (size_t)(tx * TILE_SUB + k % 5)] >= 0;
                    if (!reach)
                        continue;
                    for (int l = 0; l < MAP_FLOORS; l++)
                        floor |= map_floor(&s_map, l, tx, ty) != NULL;
                    for (int l = 0; l < MAP_WALLS; l++)
                        wall |= map_wall(&s_map, l, tx, ty) != NULL;
                    if (!floor && !wall && !bare++)
                        bx = tx, by = ty;
                    if (!floor && wall && !nofloor++)
                        nx = tx, ny = ty;
                }
            if (bare || nofloor)
                printf("  %s: %d tiles se pisan sin nada dibujado (el primero en %d, %d) y %d con pared y sin piso (%d, %d)\n",
                       world_zone_name(z), bare, bx, by, nofloor, nx, ny);
            if (getenv("D2_ZONEREACH")) { /* D2_ZONEREACH=1: cuántos subtiles de la zona se alcanzan desde la entrada */
                long reached = 0;
                for (int sy2 = y0 * TILE_SUB; sy2 < y1 * TILE_SUB; sy2++)
                    for (int sx2 = x0 * TILE_SUB; sx2 < x1 * TILE_SUB; sx2++)
                        reached += dist[(size_t)sy2 * (size_t)(s_map.width * TILE_SUB) + (size_t)sx2] >= 0;
                printf("  alcance %s: %ld\n", world_zone_name(z), reached);
            }
        }
        if (atoi(getenv("D2_ZONETEST")) == 2) { /* la zona dibujada: # bloqueado, . se llega, o no se llega, E la entrada */
            int x0, y0, x1, y1;
            world_zone_rect(z, &x0, &y0, &x1, &y1);
            for (int ty = y0; ty < y1; ty++) {
                for (int tx = x0; tx < x1; tx++) {
                    int blocked = 0, reach = 0;
                    for (int k = 0; k < 25; k++) {
                        blocked += map_blocked(&s_map, tx * TILE_SUB + k % 5, ty * TILE_SUB + k / 5);
                        reach |= walk_dist(dist, (float)(tx * TILE_SUB + k % 5), (float)(ty * TILE_SUB + k / 5), 0) >= 0;
                    }
                    int entry = (int)(sx / TILE_SUB) == tx && (int)(sy / TILE_SUB) == ty;
                    putchar(entry ? 'E' : blocked >= 13 ? '#' : reach ? '.' : 'o');
                }
                putchar('\n');
            }
        }
        free(dist);
        printf("zona %-28s %s (%d puntos)%s\n", world_zone_name(z), len ? "MAL:" : "bien", checks, problems);
        bad += len > 0;
    }
    printf("zonas con problemas: %d\n", bad);
    float rx0, ry0, rx1, ry1; /* D2_REACH="x,y,x2,y2" (en subtiles): ¿se llega caminando de uno al otro? */
    if (getenv("D2_REACH") && sscanf(getenv("D2_REACH"), "%f,%f,%f,%f", &rx0, &ry0, &rx1, &ry1) == 4) {
        find_free_spot(&rx0, &ry0);
        int *dist = walk_bfs(rx0, ry0);
        if (dist)
            printf("de (%.0f, %.0f) a (%.0f, %.0f): %d\n", rx0, ry0, rx1, ry1, walk_dist(dist, rx1, ry1, 3));
        free(dist);
    }
    free(s_walk_open);
    s_walk_open = NULL;
    for (int i = 0; getenv("D2_WARPS") && i < world_warp_count(); i++) { /* D2_WARPS: cada salto armado, de dónde a dónde */
        const Warp *w = world_warp(i);
        printf("salto %-26s -> %-26s (el %d) en (%.0f, %.0f)\n", world_zone_name(w->from), world_zone_name(w->to), w->pair,
               w->x0, w->y0);
    }
    for (int l = 0; getenv("D2_WARPS") && l < world_link_count(); l++) { /* y cada unión, con su cruce */
        const WorldLink *wl = world_link(l);
        printf("unión %-26s - %-26s en (%.1f, %.1f)%s\n", world_zone_name(wl->a), world_zone_name(wl->b), wl->x, wl->y,
               wl->cross > 0     ? ""
               : wl->cross == -1 ? ", sin cruce que entre a las dos"
               : wl->cross == -2 ? ", fuera de un borde"
                                 : ", sin mirar");
    }
    /* D2_TILEDUMP="tx,ty,ancho,alto" (depuración, en tiles del mapa): las
       baldosas de cada tile, orientación.estilo.secuencia de pisos y paredes */
    int dtx, dty, dtw, dth;
    if (getenv("D2_TILEDUMP") && sscanf(getenv("D2_TILEDUMP"), "%d,%d,%d,%d", &dtx, &dty, &dtw, &dth) == 4)
        for (int ty = dty; ty < dty + dth; ty++)
            for (int tx = dtx; tx < dtx + dtw; tx++) {
                printf("tile (%d, %d):", tx, ty);
                for (int l = 0; l < MAP_FLOORS; l++) {
                    const Dt1Tile *t = map_floor(&s_map, l, tx, ty);
                    if (t)
                        printf(" piso%d %d.%d.%d", l, t->orientation, t->main_index, t->sub_index);
                }
                for (int l = 0; l < MAP_WALLS; l++) {
                    const Dt1Tile *t = map_wall(&s_map, l, tx, ty);
                    if (t)
                        printf(" pared%d %d.%d.%d (alto %d)", l, t->orientation, t->main_index, t->sub_index, -t->img_y);
                }
                printf("\n");
            }
    /* D2_COLLDUMP="zona,tx,ty,ancho,alto" (depuración): la colisión de ese
       rectángulo de la zona (en tiles), un carácter por subtile: . libre,
       # pared, o objeto, ~ vacío, ? otra cosa */
    int cz, ctx, cty, ctw, cth;
    if (getenv("D2_COLLDUMP") && sscanf(getenv("D2_COLLDUMP"), "%d,%d,%d,%d,%d", &cz, &ctx, &cty, &ctw, &cth) == 5) {
        int x0, y0, x1, y1;
        world_zone_rect(cz, &x0, &y0, &x1, &y1);
        printf("colisión de %s desde el tile (%d, %d) (el subtile (%d, %d) del mapa):\n", world_zone_name(cz), ctx, cty,
               (x0 + ctx) * TILE_SUB, (y0 + cty) * TILE_SUB);
        for (int sy = (y0 + cty) * TILE_SUB; sy < (y0 + cty + cth) * TILE_SUB; sy++) {
            for (int sx = (x0 + ctx) * TILE_SUB; sx < (x0 + ctx + ctw) * TILE_SUB; sx++) {
                uint16_t c = map_coll(&s_map, sx, sy);
                putchar(c == 0 ? '.' : c == COLL_MASK_INVALID ? '~' : (c & COLL_WALL) ? '#' : (c & COLL_OBJECT) ? 'o' : '?');
            }
            putchar('\n');
        }
        uint16_t seen[16];
        int ns = 0;
        for (int sy = (y0 + cty) * TILE_SUB; sy < (y0 + cty + cth) * TILE_SUB; sy++)
            for (int sx = (x0 + ctx) * TILE_SUB; sx < (x0 + ctx + ctw) * TILE_SUB; sx++) {
                uint16_t c = map_coll(&s_map, sx, sy);
                int k = 0;
                while (k < ns && seen[k] != c)
                    k++;
                if (k == ns && ns < 16)
                    seen[ns++] = c;
            }
        printf("marcas:");
        for (int k = 0; k < ns; k++)
            printf(" %04x", seen[k]);
        printf(" (camina el jugador si no tiene %04x)\n", COLL_MASK_PLAYER_PATH);
    }
}

/* Milisegundos de reloj (no de CPU: una espera del disco también cuenta) */
static double wall_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/* Para el banco de pruebas (D2_BENCH_PARTS): cuánto se va en cada etapa del dibujo */
static double s_part_ms[4]; /* mundo, oscuridad, HUD, todo el cuadro */
static int    s_part_frames;

void game_bench_parts(char *out, size_t cap)
{
    if (out && cap) {
        int f = s_part_frames > 0 ? s_part_frames : 1;
        double rest = s_part_ms[3] - s_part_ms[0] - s_part_ms[1] - s_part_ms[2];
        snprintf(out, cap, "mundo %.2f, oscuridad %.2f, HUD %.2f, resto %.2f ms", s_part_ms[0] / f, s_part_ms[1] / f,
                 s_part_ms[2] / f, rest / f);
    }
    memset(s_part_ms, 0, sizeof s_part_ms);
    s_part_frames = 0;
}

/* D2_MEM: memoria usada en cada paso del arranque y al armar o borrar
   zonas (para la Pi, que tiene 1 GB) */
static void mem_report(const char *step)
{
    if (!getenv("D2_MEM"))
        return;
    FILE *f = fopen("/proc/self/status", "r");
    char line[128];
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, "VmRSS:", 6) == 0) {
            line[strcspn(line, "\n")] = '\0';
            printf("memoria después de %s:%s (mapa %.1f MB, %d gráficos de unidades %.1f MB, sonidos %.1f MB)\n",
                   step, line + 6, map_memory(&s_map) / 1048576.0, units_gfx_loaded(), units_gfx_bytes() / 1048576.0,
                   audio_memory() / 1048576.0);
        }
    if (f)
        fclose(f);
}

int game_init(void)
{
    s_debug = getenv("D2_DEBUG") != NULL;
    s_one_hit = getenv("D2_ONEHIT") != NULL;
    s_god = getenv("D2_GOD") != NULL;
    s_lockstep = getenv("D2_LOCKSTEP") != NULL;
    units_set_lockstep(s_lockstep);
    s_hash_every = getenv("D2_HASH") ? atoi(getenv("D2_HASH")) : 0;
    pal_init_placeholder();
    if (pal_load_d2("act1") != 0) {
        fprintf(stderr, "no encontré data/global/palette/act1: falta extraer los archivos del juego\n");
        return -1;
    }
    lang_init(s_lang_pref);
    strings_load(lang_strings_code());
    ui_loading(1, 6);
    if (font_init_d2() != 0)
        fprintf(stderr, "sin fuentes del juego: uso la fuente incorporada\n");
    mem_report("las fuentes");
    if (items_init() != 0)
        return -1;
    cube_init();
    missiles_init();
    if (overlays_init() == 0) { /* los efectos de las habilidades, cargados ya */
        static const char *const fx[] = { "bash", "stun", "lightning", "concentrationfront", "battlecry", "shoutstart",
                                          "taunt", "innersight", "cast_innersight" };
        for (unsigned k = 0; k < sizeof fx / sizeof fx[0]; k++)
            overlays_preload(fx[k]);
    }
    load_hirelings();
    if (skills_init() != 0)
        fprintf(stderr, "sin tablas de habilidades: no se van a poder aprender\n");
    war_monpet_row(NULL); /* monpet.txt (Consumir del Brujo) ya leída: no en medio del juego */
    ui_loading(2, 6);

    size_t n;
    uint8_t *d = fs_read("data/global/animdata.d2", &n);
    if (d && animdata_load(d, n, &s_animdata) == 0)
        charanim_set_animdata(&s_animdata);
    free(d);

    /* El Blood Moor cambia en cada partida; D2_SEED fija la semilla para pruebas */
    uint32_t seed = getenv("D2_SEED") ? (uint32_t)strtoul(getenv("D2_SEED"), NULL, 10) : (uint32_t)time(NULL);
    s_rng = seed * 2654435761u + 1;
    mem_report("objetos y habilidades");
    s_world_seed = seed;
    units_set_level_query(level_id_at);
    if (world_layout(&s_map, seed, 1) != 0 || units_init() != 0)
        return -1;
    prefetch_act_tiles(1);
    if (getenv("D2_ANIM_CHECK")) /* depuración: las animaciones que faltan */
        units_check_anims();
    ui_loading(3, 6);
    units_set_picker(pick_level_monster);
    if (getenv("D2_ZONE_MAX")) /* pruebas: menos zonas a la vez */
        s_zone_max = atoi(getenv("D2_ZONE_MAX"));
    if (getenv("D2_ZONE_RESET")) /* pruebas: segundos hasta borrar una zona vacía */
        s_zone_reset_ticks = (uint32_t)(atof(getenv("D2_ZONE_RESET")) * TICK_RATE);
    if (getenv("D2_ZONE_BUDGET")) /* ms por tick para armar una zona en partes (0 = de una, como antes) */
        s_zone_budget = atof(getenv("D2_ZONE_BUDGET"));
    zone_load(0); /* el pueblo queda siempre */
    ui_loading(5, 6);
    mem_report("el pueblo");

    /* D2_PATHTEST: ¿se llega caminando desde la entrada de la cueva hasta Corpsefire? */
    /* D2_PATHTEST: ¿se llega caminando desde el pueblo al waypoint de las Cold
       Plains y a Blood Raven, y desde la entrada de la cueva hasta Corpsefire? */
    if (getenv("D2_ZONETEST"))
        zone_test();
    if (getenv("D2_PATHTEST")) {
        zones_load_all();
        float tx, ty, bx, by;
        int wps[4], nwp = units_find_all("WaypointOutsideAct1", wps, 4);
        if (units_find("RogueBonfire", &tx, &ty) == 0) {
            ty += 5.0f;
            for (int i = 0; i < nwp; i++) {
                units_position(wps[i], 1.0f, &bx, &by);
                const Level *lv = world_level_at(bx, by);
                printf("del pueblo al waypoint de %s: %d subtiles\n", lv ? lv->name : "?", path_length(tx, ty, bx, by));
            }
            if (units_find("bloodraven", &bx, &by) == 0)
                printf("del pueblo a Blood Raven: %d subtiles\n", path_length(tx, ty, bx, by));
        }
        for (int i = 0; i < world_warp_count(); i++) {
            const Warp *warp = world_warp(i);
            float ax, ay;
            if (strcmp(world_zone_name(warp->to), "Den of Evil") != 0 || world_warp_arrival(warp, &ax, &ay) != 0 ||
                units_find("Corpsefire", &bx, &by) != 0)
                continue;
            find_free_spot(&ax, &ay);
            printf("de la entrada de la cueva a Corpsefire: %d subtiles\n", path_length(ax, ay, bx, by));
            break;
        }
    }

    /* D2_ITEMTEST=N: N botines de Andariel guardados y leídos de vuelta, una
       palabra rúnica, una gema engarzada y algunas recetas del cubo */
    if (getenv("D2_ITEMTEST")) {
        int n = atoi(getenv("D2_ITEMTEST")), bad = 0, total = 0;
        for (int k = 0; k < n; k++) {
            Item drops[6];
            int got = items_roll_drop("Andariel", 5 + k % 25, 300, drops, 6);
            for (int j = 0; j < got; j++, total++) {
                char text[1024];
                Item back;
                const Item *a = &drops[j];
                items_to_text(a, text, sizeof text);
                int same = items_from_text(text, &back) && back.type == a->type && back.quality == a->quality &&
                           back.num_stats == a->num_stats && back.sockets == a->sockets && back.unique == a->unique &&
                           back.amount == a->amount && back.ilvl == a->ilvl && strcmp(back.name, a->name) == 0;
                for (int s = 0; same && s < a->num_stats; s++)
                    same = back.stat[s] == a->stat[s] && back.param[s] == a->param[s] && back.value[s] == a->value[s];
                if (!same && bad++ < 5)
                    printf("  MAL al leer: %s\n", text);
            }
        }
        printf("objetos guardados y leídos: %d de %d iguales\n", total - bad, total);
        /* Lo de fábrica de los objetos de clase (automagic.txt) y el grimorio con un arma a dos manos */
        static const char *const k_auto[] = { "wa1", "wa5", "ob1", "ne1", "pa1", "am1", "am3" };
        for (unsigned k = 0; k < sizeof k_auto / sizeof k_auto[0]; k++) {
            const ItemType *at = items_find(k_auto[k]);
            if (!at)
                continue;
            char al[24][112];
            Item ai = items_make(at);
            items_add_automagic(&ai, 30);
            int nl = items_describe(&ai, 30, al, 24);
            printf("de fábrica: %s (%s):", at->name, k_auto[k]);
            for (int l = 0; l < nl; l++)
                printf(" | %s", al[l]);
            printf("\n");
        }
        printf("grimorio con arma a dos manos: %s; escudo con arma a dos manos: %s\n",
               items_offhand_ok(items_find("2hs"), items_find("wa1")) ? "se puede" : "NO",
               items_offhand_ok(items_find("2hs"), items_find("buc")) ? "SE PUEDE (mal)" : "no");
        /* Palabra rúnica Acero (Tir + El) en una espada larga con 2 engarces */
        char lines[24][112], text[1024];
        Item sw = items_make(items_find("lsd")), r3 = items_make(items_find("r03")), r1 = items_make(items_find("r01"));
        sw.sockets = 2;
        int rw = items_socket(&sw, &r3) && items_socket(&sw, &r1) && sw.quality == QUALITY_RUNEWORD;
        printf("palabra rúnica: %s %s\n", rw ? "BIEN" : "MAL", sw.name);
        int nl = items_describe(&sw, 10, lines, 24);
        for (int k = 0; k < nl; k++)
            printf("      %s\n", lines[k]);
        items_to_text(&sw, text, sizeof text);
        Item back;
        printf("  guardada: %s\n", items_from_text(text, &back) && back.quality == QUALITY_RUNEWORD ? "BIEN" : "MAL");
        Item cap = items_make(items_find("cap")), gem = items_make(items_find("gcr"));
        cap.sockets = 1;
        printf("rubí mellado en un gorro: %s\n", items_socket(&cap, &gem) && items_stat(&cap, STAT_LIFE) > 0 ? "BIEN" : "MAL");
        /* El cubo */
        static const struct { const char *in[8]; const char *want; } k_recipes[] = {
            { { "gcv", "gcv", "gcv" }, "gfv" },
            { { "r01", "r01", "r01" }, "r02" },
            { { "hp1", "hp2", "hp1", "mp1", "mp3", "mp1", "gcb" }, "rvs" },
            { { "rvs", "rvs", "rvs" }, "rvl" },
            { { "aqv", "aqv" }, "cqv" },
            { { "axe", "dgr" }, "tax" },
            { { "r07", "r10", "gpy", "qui" }, "qui" },
        };
        for (unsigned r = 0; r < sizeof k_recipes / sizeof k_recipes[0]; r++) {
            Item in[8], out[3];
            int m = 0;
            for (int k = 0; k < 8 && k_recipes[r].in[k]; k++)
                if (items_find(k_recipes[r].in[k]))
                    in[m++] = items_make(items_find(k_recipes[r].in[k]));
            char what[112];
            int got = cube_transmute(in, m, 20, out, 3, what, sizeof what);
            int ok = got > 0 && strcmp(out[0].type->code, k_recipes[r].want) == 0;
            printf("cubo %-4s: %s %s (%s)%s\n", k_recipes[r].want, ok ? "BIEN" : "MAL", got ? out[0].type->name : "-",
                   what, got && out[0].sockets ? " con engarces" : "");
        }
        Item ring[3], out[3];
        for (int k = 0; k < 3; k++) {
            ring[k] = items_make(items_find("rin"));
            items_make_magic(&ring[k], 20);
        }
        char what[112];
        int got = cube_transmute(ring, 3, 20, out, 3, what, sizeof what);
        printf("cubo 3 anillos mágicos: %s %s\n", got && out[0].quality == QUALITY_MAGIC && !strcmp(out[0].type->code, "amu")
                                                ? "BIEN" : "MAL", got ? out[0].name : "-");
    }

    /* D2_DROPTEST=N: tira N veces el botín de un Fallen y muestra qué sale
       (D2_DROPTC: otra clase de tesoro, D2_DROPLVL: el nivel del monstruo,
       D2_DROPMF: búsqueda de magia, D2_DROPNAMES: los nombres y propiedades) */
    if (getenv("D2_DROPTEST")) {
        int n = atoi(getenv("D2_DROPTEST")), empty = 0, total = 0;
        int dlvl = getenv("D2_DROPLVL") ? atoi(getenv("D2_DROPLVL")) : 1;
        int dmf = getenv("D2_DROPMF") ? atoi(getenv("D2_DROPMF")) : 0;
        static const char *const k_qual[8] = { "", "(mágico)", "(raro)", "(conjunto)", "(único)", "(palabra rúnica)",
                                               "(superior)", "(inferior)" };
        char names[64][48];
        int counts[64], kinds = 0;
        for (int k = 0; k < n; k++) {
            Item drops[6];
            int got = items_roll_drop(getenv("D2_DROPTC") ? getenv("D2_DROPTC") : "Act 1 H2H A", dlvl, dmf, drops, 6);
            empty += got == 0;
            for (int j = 0; j < got; j++, total++) {
                char label[64];
                int q = drops[j].quality < 8 ? drops[j].quality : 0;
                const char *name = strcmp(drops[j].type->code, "gld") == 0 ? "oro"
                                 : q != QUALITY_NORMAL ? k_qual[q] : drops[j].sockets ? "(con engarces)" : drops[j].type->name;
                if ((q != QUALITY_NORMAL || drops[j].sockets) && getenv("D2_DROPNAMES")) {
                    char lines[24][112];
                    printf("  %s %s [%s, nivel %d]\n", k_qual[q], items_label(&drops[j], label, sizeof label),
                           drops[j].type->code, drops[j].ilvl);
                    int nl = items_describe(&drops[j], 10, lines, 24);
                    for (int k = 0; k < nl; k++)
                        printf("      %s\n", lines[k]);
                    if (drops[j].sockets)
                        printf("      engarces: %d\n", drops[j].sockets);
                }
                snprintf(label, sizeof label, "%s", name);
                int f = 0;
                while (f < kinds && strcmp(names[f], label) != 0)
                    f++;
                if (f == kinds && kinds < 64) {
                    snprintf(names[kinds], sizeof names[kinds], "%.47s", label);
                    counts[kinds++] = 0;
                }
                if (f < 64)
                    counts[f]++;
            }
        }
        printf("botín de %d Fallen: %d sin nada, %d objetos\n", n, empty, total);
        for (int f = 0; f < kinds; f++)
            printf("  %-28s %d\n", names[f], counts[f]);
    }
    printf("unidades en el mapa: %d (monstruos: %d)\n", units_count(), units_count_enemies());
    for (int i = 0; s_debug && i < world_level_count(); i++) {
        const Level *lv = world_level(i);
        printf("  %s: %d monstruos\n", lv->name,
               units_count_enemies_in(lv->x0 * (float)TILE_SUB, lv->y0 * (float)TILE_SUB, lv->x1 * (float)TILE_SUB,
                                      lv->y1 * (float)TILE_SUB));
        const char *names[32];
        int counts[32], kinds = 0;
        for (int u = 0; u < units_count(); u++) {
            float ux, uy;
            units_position(u, 1.0f, &ux, &uy);
            if (!units_alive_enemy(u) || world_level_at(ux, uy) != lv)
                continue;
            int k = 0;
            while (k < kinds && strcmp(names[k], units_id(u)) != 0)
                k++;
            if (k == kinds && kinds < 32) {
                names[kinds] = units_id(u);
                counts[kinds++] = 0;
            }
            if (k < 32)
                counts[k]++;
        }
        for (int k = 0; k < kinds; k++)
            printf("    %s: %d\n", names[k], counts[k]);
    }

    TxtTable charstats = { 0 }, experience = { 0 };
    d = fs_read("data/global/excel/charstats.txt", &n);
    if (d)
        txt_load(d, n, &charstats);
    free(d);
    d = fs_read("data/global/excel/experience.txt", &n);
    if (d)
        txt_load(d, n, &experience);
    free(d);
    int ok = 0;
    for (int i = 0; i < NUM_CLASSES && ok == 0; i++) {
        load_class_stats(&s_classes[i], &charstats, &experience);
        ok = load_class(&s_classes[i]);
    }
    txt_free(&charstats);
    txt_free(&experience);
    return ok;
}

static void gain_exp(Player *p, uint32_t exp);
static void auto_assign(Player *p, int skill);

/* Depuración: D2_LEVEL=N sube al personaje a ese nivel y D2_SKILLS="Bash:3,Howl"
   le da esas habilidades (y las pone en los botones) */
static void debug_skills(Player *p)
{
    const CharClass *c = &s_classes[p->cls];
    const char *lv = getenv("D2_LEVEL");
    if (lv && atoi(lv) > p->level_num && atoi(lv) <= 99)
        gain_exp(p, c->exp_next[atoi(lv) - 1] - p->exp);
    const char *list = getenv("D2_SKILLS");
    while (list && *list) {
        char item[48];
        size_t len = strcspn(list, ",");
        snprintf(item, sizeof item, "%.*s", (int)(len < sizeof item ? len : sizeof item - 1), list);
        list += len + (list[len] == ',');
        char *colon = strchr(item, ':');
        int level = colon ? atoi(colon + 1) : 1;
        if (colon)
            *colon = '\0';
        int s = skills_find(p->cls, item);
        if (s >= 0 && level > 0) {
            p->skill_lvl[s] = level > SKILL_MAX_LEVEL ? SKILL_MAX_LEVEL : level;
            if (!skills_get(p->cls, s)->passive)
                auto_assign(p, s);
        }
    }
}

static void place_at(Player *p, int i, float x, float y)
{
    p->x = x + i * 3.0f;
    p->y = y - i * 1.0f;
    if (!getenv("D2_START_EXACT")) /* depuración: no mover el punto de inicio */
        find_free_spot(&p->x, &p->y);
    p->prev_x = p->x;
    p->prev_y = p->y;
}

void game_start(int num_players)
{
    int slots[MAX_PLAYERS], classes[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        slots[i] = i + 1 <= SAVE_SLOTS ? i + 1 : 0; /* (del séptimo en adelante, personajes que no se guardan) */
        classes[i] = i % NUM_CLASSES;
    }
    const char *env = getenv("D2_CLASSES"); /* pruebas: "2,1" = Hechicera y Amazona (hasta ocho) */
    s_eco = getenv("D2_ECO") != NULL; /* pruebas: una corrida del Eco del Odio */
    if (env)
        for (int i = 0; i < MAX_PLAYERS && *env; i++) {
            classes[i] = atoi(env);
            env += strcspn(env, ",");
            env += *env == ',';
        }
    game_start_slots(num_players, slots, classes);
}

void game_set_terror_zones(int on)
{
    s_tz_menu = on != 0;
}

void game_set_mode(int mode)
{
    s_eco = mode == 1;
}

void game_set_difficulty(int diff)
{
    s_diff = diff < 0 ? 0 : diff > 2 ? 2 : diff;
    world_set_difficulty(s_diff); /* los monstruos de cada nivel */
    units_set_difficulty(s_diff); /* y sus números */
    s_dl_resist = units_difficulty_int("ResistPenalty", 0);
    s_dl_res_floor = units_difficulty_int("ResistFloor", -100);
    s_dl_life_div = units_difficulty_int("LifeStealDivisor", 1);
    s_dl_mana_div = units_difficulty_int("ManaStealDivisor", 1);
    s_dl_static_min = units_difficulty_int("StaticFieldMin", 0);
    s_dl_curse_div = units_difficulty_int("AiCurseDivisor", 1);
    s_dl_life_div = s_dl_life_div > 0 ? s_dl_life_div : 1;
    s_dl_mana_div = s_dl_mana_div > 0 ? s_dl_mana_div : 1;
    s_dl_curse_div = s_dl_curse_div > 0 ? s_dl_curse_div : 1;
}

int game_difficulty(void)
{
    return s_diff;
}

void game_start_slots(int num_players, const int *slots, const int *classes)
{
    if (getenv("D2_DIFF") && !s_carry[0]) /* pruebas: en Pesadilla (1) o Infierno (2) (la campaña sigue sola) */
        game_set_difficulty(atoi(getenv("D2_DIFF")));
    ui_loading(4, 6); /* armar a los personajes tarda un poco (más en la Pi) */
    automap_reset();
    overlays_clear();
    int map_open = getenv("D2_MAP_OPEN") != NULL; /* depuración: con el automapa abierto */
    pal_load_d2("act1"); /* los menús usan otra paleta */
    s_paused = getenv("D2_PAUSE") != NULL; /* depuración: arrancar con la pausa abierta */
    if (getenv("D2_CONTROLS")) { /* depuración: los controles (0 el joystick, 1 el teclado) */
        s_paused = s_controls_open = 1;
        controls_open_at(atoi(getenv("D2_CONTROLS")));
    }
    if (getenv("D2_GUIDE")) { /* depuración: la Guía del cubo en "categoría,receta" */
        s_paused = s_guide_open = 1;
        sscanf(getenv("D2_GUIDE"), "%d,%d", &s_guide_cat, &s_guide_sel);
    }
    s_num_players = num_players < 1 ? 1 : num_players > MAX_PLAYERS ? MAX_PLAYERS : num_players;
    units_set_player_count(s_num_players);
    if (!s_net_local) { /* (sin red, los de esta máquina son los primeros) */
        s_num_local = s_num_players < MAX_LOCAL ? s_num_players : MAX_LOCAL;
        for (int k = 0; k < MAX_LOCAL; k++)
            s_local[k] = k;
    }
    s_ticks = 0;
    if (!s_net_epoch) /* (en red lo pone el anfitrión) */
        s_epoch = (long long)time(NULL);
    s_shown_target = -1;
    s_den_cleared = 0;
    s_den_light = 0;
    bossfx_clear();
    skillfx_clear();
    s_quest_ticks = 0;
    s_raven_dead = 0;
    items_ground_clear();
    missiles_clear();
    zones_unload_all(); /* partida nueva: las zonas se arman de nuevo al entrar */
    quest_reset();
    generate_shops();

    static const uint8_t label[MAX_PLAYERS] = { R_GOLD, R_BLUE, R_GRASS, R_RED, R_CLOTH, R_SKIN, R_WOOD, R_GRAY };
    /* Como en el original, se arranca junto a la fogata de las pícaras */
    s_start_x = s_map.width * TILE_SUB / 2.0f;
    s_start_y = s_map.height * TILE_SUB / 2.0f;
    if (units_find("RogueBonfire", &s_start_x, &s_start_y) == 0)
        s_start_y += 5.0f;
    s_town_x = s_start_x;
    s_town_y = s_start_y;
    const char *dbg = getenv("D2_START"); /* depuración: "x,y" en subtiles */
    if (dbg && sscanf(dbg, "%f,%f", &s_start_x, &s_start_y) == 2) {
        zone_load(world_zone_at(&s_map, s_start_x, s_start_y));
    }
    const char *near = getenv("D2_START_NEAR"); /* depuración: al lado de un monstruo, ej. "fallen1" */
    if (near) {
        zones_load_all();
        if (units_find(near, &s_start_x, &s_start_y) == 0)
            s_start_x += 2.5f;
    }

    /* D2_SPAWN_ITEMS: deja algunos objetos al lado del inicio (depuración) */
    if (getenv("D2_SPAWN_ITEMS")) {
        static const char *const codes[] = { "gld", "hp1", "mp1", "ssd", "lea", "cap", "vps", "hp1" };
        for (unsigned k = 0; k < sizeof codes / sizeof codes[0]; k++) {
            const ItemType *t = items_find(codes[k]);
            if (!t)
                continue;
            Item it = items_make(t);
            if (strcmp(codes[k], "gld") == 0)
                it.amount = 37;
            items_ground_add(&it, s_start_x + 2.0f + (float)(k % 4) * 1.5f, s_start_y + 2.0f + (float)(k / 4) * 1.5f);
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s_pl[i];
        int cls = i < s_num_players ? classes[i] : i;
        cls = cls < 0 || cls >= NUM_CLASSES ? 0 : cls;
        SlotInfo info;
        int slot = i < s_num_players ? slots[i] : 0;
        if (slot > 0 && game_slot_info(slot, &info))
            cls = info.cls; /* la clase la dice la partida */
        const CharClass *c = &s_classes[cls];
        look_job_drop(i);
        free_look(p);
        memset(p, 0, sizeof *p);
        place_at(p, i, s_start_x, s_start_y);
        p->valk_unit = -1;
        for (int t = 0; t < ASS_TRAPS_MAX; t++)
            p->trap_unit[t] = -1;
        p->sent_unit = p->shadow_unit = -1;
        p->form_skill = p->feral_skill = p->maul_skill = p->vine_skill = p->spirit_skill = p->spirit_unit = -1;
        p->hex_skill = p->ward_skill = p->eldritch_skill = p->bind_unit = p->abyss_skill = -1;
        p->bind_ch_skill = p->bind_aura = p->last_button = -1;
        for (int h = 0; h < HYDRA_MAX; h++)
            p->hydra_unit[h] = -1;
        p->aura = -1;
        p->face_y = 1.0f;
        p->map_open = map_open;
        p->cls = cls;
        p->slot = slot;
        p->stamina = p->stamina_max = c->stamina;
        p->hp = p->hp_max = p->hp_base = c->life;
        p->mana = p->mana_max = p->mana_base = c->mana;
        p->level_num = 1;
        p->target = -1;
        p->dialog_npc = -1;
        p->waypoints = 1; /* el del pueblo, siempre */
        p->act = p->act_max = p->act_top = 1;
        p->label = label[i];
        p->attack_skill = -1;
        p->buttons[0] = BTN_ATTACK;
        p->buttons[1] = p->buttons[2] = p->buttons[3] = BTN_EMPTY;
        if (c->start_items[0] && strcmp(c->start_items[0], "jav") == 0)
            p->buttons[1] = BTN_THROW; /* la Amazona arranca con jabalinas: B las lanza */
        if (i >= s_num_players) /* (un lugar sin jugador: sin personaje ni dibujos que cargar) */
            continue;
        int loaded = load_character(p);
        if (getenv("D2_WAYPOINTS")) /* depuración: waypoints activados (máscara de bits) */
            p->waypoints |= (uint64_t)strtoull(getenv("D2_WAYPOINTS"), NULL, 0);
        if (!loaded)
            give_start_items(p);
        debug_skills(p);
        /* D2_EQUIP="ltp,hlm": se pone esos objetos al empezar (depuración) */
        const char *equip = getenv("D2_EQUIP");
        while (equip && *equip) {
            char code[8] = "";
            int len = 0;
            while (equip[len] && equip[len] != ',' && len < 7)
                code[len] = equip[len], len++;
            code[len] = '\0';
            const ItemType *t = items_find(code);
            if (t && t->slot != SLOT_NONE) { /* se lo pone aunque no sea mejor */
                Item it = items_make(t), old = p->equip[t->slot];
                p->equip[t->slot] = it;
                if (old.type)
                    bag_add(p, &old);
                if (t->slot == SLOT_WEAPON && !items_offhand_ok(t, p->equip[SLOT_SHIELD].type) &&
                    bag_add(p, &p->equip[SLOT_SHIELD]))
                    p->equip[SLOT_SHIELD].type = NULL;
            } else if (t) {
                Item it = items_make(t);
                pick_up(p, &it);
            }
            equip += len + (equip[len] == ',');
        }
        /* D2_BAG="sbw,lea,hp1": esos objetos a la mochila (depuración) */
        for (const char *bag = getenv("D2_BAG"); bag && *bag;) {
            char code[8];
            size_t len = strcspn(bag, ",");
            snprintf(code, sizeof code, "%.*s", (int)(len < 7 ? len : 7), bag);
            const ItemType *t = items_find(code);
            if (t) {
                Item it = items_make(t);
                bag_add(p, &it);
            }
            bag += len + (bag[len] == ',');
        }
        /* D2_MAGIC=N: todo lo que se pone sale mágico con objetos de nivel N (depuración) */
        if (getenv("D2_MAGIC"))
            for (int s = 0; s < SLOT_COUNT; s++)
                if (p->equip[s].type)
                    items_make_magic(&p->equip[s], atoi(getenv("D2_MAGIC")));
        refresh_look(p);
        p->hp = p->hp_max;
        p->mana = p->mana_max;
        p->stamina = p->stamina_max;
        p->inv_open = getenv("D2_INV_OPEN") != NULL; /* depuración: inventario abierto */
        if (p->inv_open && getenv("D2_INV_PAGE")) /* y en esa página (1 = habilidades) */
            p->inv_page = atoi(getenv("D2_INV_PAGE"));
        if (getenv("D2_GOLD")) /* depuración: arrancar con oro */
            p->gold = atoi(getenv("D2_GOLD"));
        if (!loaded && i < s_num_players)
            save_character(p); /* personaje nuevo: ya ocupa su lugar */
        s_view_x[i] = SCREEN_W / 2.0f;
        s_view_y[i] = SCREEN_H / 2.0f;
    }
    if (s_diff > 0) { /* las tiendas otra vez, con el nivel de los personajes; y el cartel */
        generate_shops();
        s_quest_ticks = 4 * TICK_RATE;
        snprintf(s_quest_banner, sizeof s_quest_banner, "%s", tr(k_diff_names[s_diff]));
    }
    /* El acto: el más avanzado de los dos (juegan en el mismo mundo);
       D2_ACT=2 arranca en Lut Gholein (pruebas) */
    int act = getenv("D2_ACT") ? atoi(getenv("D2_ACT")) : 1;
    for (int i = 0; i < s_num_players; i++)
        if (s_pl[i].act > act)
            act = s_pl[i].act;
    act = act >= 1 && act <= 5 ? act : 1;
    if (!s_eco) /* (la historia: nada de la arena de antes) */
        eco_leave();
    if (s_eco) { /* el Eco del Odio: la dificultad del nivel de los personajes, en el Acto 5 con las arenas */
        game_set_difficulty(eco_difficulty());
        world_set_arena(1);
        s_eco_world = 1;
        act_switch(5);
        eco_enter();
    } else if (act != s_act || s_eco_world || s_net_world) {
        if (s_eco_world) { /* (la partida de antes fue del Eco: el mundo sin las arenas) */
            world_set_arena(0);
            s_eco_world = 0;
        }
        s_net_world = 0;
        act_switch(act);
        if (getenv("D2_ZONETEST")) /* las del otro acto */
            zone_test();
        float nx, ny; /* D2_START_NEAR: también al lado de un monstruo del otro acto */
        if (getenv("D2_START") && sscanf(getenv("D2_START"), "%f,%f", &nx, &ny) == 2) { /* y D2_START, en subtiles */
            zone_load(world_zone_at(&s_map, nx, ny));
            for (int i = 0; i < s_num_players; i++)
                place_at(&s_pl[i], i, nx + (float)i, ny);
        }
        if (getenv("D2_START_NEAR")) {
            zones_load_all();
            if (units_find(getenv("D2_START_NEAR"), &nx, &ny) == 0)
                for (int i = 0; i < s_num_players; i++)
                    place_at(&s_pl[i], i, nx + 3.0f, ny);
        }
    }
    /* D2_SPAWN="scarab1,4": esos monstruos alrededor del jugador 1 (depuración: para ver sus IA) */
    char spawn_id[32];
    int spawn_n = 1;
    if (getenv("D2_SPAWN") && sscanf(getenv("D2_SPAWN"), "%31[^,],%d", spawn_id, &spawn_n) >= 1) {
        int z = zone_at(s_pl[0].x, s_pl[0].y);
        units_zone_begin(z);
        for (int k = 0; k < spawn_n; k++) {
            float a = (float)k * 2.0f * PI_F / (float)(spawn_n > 0 ? spawn_n : 1);
            float sx = s_pl[0].x + cosf(a) * 7.0f, sy = s_pl[0].y + sinf(a) * 7.0f;
            if (units_spawn_monster(spawn_id, sx, sy, 15) < 0) /* (o un jefe con nombre: "Lord De Seis") */
                units_spawn_superunique(spawn_id, sx, sy, 15);
        }
        units_zone_end();
    }
    for (int i = 0; i < MAX_PLAYERS; i++)
        s_pl[i].act = s_act;
    /* Mercenarias: la de cada jugador al lado suyo (las de otra partida se esconden) */
    units_merc_hide_all();
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        p->merc_unit = -1;
        /* D2_MERC="tipo[,nivel]": contrata una al empezar (pruebas; 0 = fuego, 1 = frío) */
        int mtype = 0, mlevel = 0;
        if (getenv("D2_MERC") && !p->merc_name && sscanf(getenv("D2_MERC"), "%d,%d", &mtype, &mlevel) >= 1) {
            p->merc_name = 1 + i;
            p->merc_type = mtype > 0 && mtype < MERC_TYPES ? mtype : 0;
            p->merc_level = mlevel > 0 ? mlevel : p->level_num;
            p->merc_hp = -1;
        }
        if (!p->merc_name)
            continue;
        p->merc_unit = units_merc_spawn(i, merc_monster(p->merc_type), p->x, p->y);
        merc_apply(p, p->merc_hp > 0 ? p->merc_hp : -1);
        if (p->merc_dead)
            units_merc_hide(p->merc_unit);
    }
    waypoints_find();
    waypoints_light();
    preload_ui();
    if (getenv("D2_SPREAD")) /* pruebas: cada jugador en otro nivel del acto (el peor caso para medir) */
        spread_players();
    if (getenv("D2_DEPTH_AUDIT")) { /* depuración: el orden de dibujo en todo el mapa */
        zones_load_all();
        depth_audit();
    }
    /* Depuración (D2_BLOCK_DUMP="x,y,radio", en subtiles): qué se bloquea
       alrededor ('#'), con el jugador 1 en '@' */
    int bx, by, br;
    if (getenv("D2_BLOCK_DUMP") && sscanf(getenv("D2_BLOCK_DUMP"), "%d,%d,%d", &bx, &by, &br) == 3)
        for (int y = by - br; y <= by + br; y++) {
            printf("%5d ", y);
            for (int x = bx - br; x <= bx + br; x++)
                putchar(x == (int)s_pl[0].x && y == (int)s_pl[0].y ? '@' : map_blocked(&s_map, x, y) ? '#' : '.');
            putchar('\n');
        }
}

/* Como en el original (D2Collision, tamaño "chico" del jugador): ocupa su
   subtile y los cuatro de al lado, en cruz. Así queda a un subtile de las
   paredes, las piedras y la carpa de Akara en vez de pararse encima de su
   dibujo, y no pasa por huecos de menos de tres subtiles. Devuelve cuántos
   de los cinco están bloqueados. */
static int foot_blocked(int sx, int sy)
{
    return (map_blocked(&s_map, sx, sy) != 0) + (map_blocked(&s_map, sx - 1, sy) != 0) +
           (map_blocked(&s_map, sx + 1, sy) != 0) + (map_blocked(&s_map, sx, sy - 1) != 0) +
           (map_blocked(&s_map, sx, sy + 1) != 0);
}

/* La cruz y, además (no está en el original), medio subtile de aire con las
   paredes y su grosor dibujado (map_too_close): las bases de los bloques de
   las cloacas del Acto 3 y las puntas de sus esquinas se dibujan un subtile
   más allá de lo que bloquean. El original para a los personajes en el
   centro de los subtiles; acá se camina en continuo, y con solo la cruz se
   llegaba hasta la esquina de la pared y se pisaba el dibujo. */
static int blocked_body(float x, float y)
{
    return foot_blocked((int)floorf(x), (int)floorf(y)) + map_too_close(&s_map, x, y);
}

/* Busca, en anillos cada vez más grandes, un lugar sin paredes ni NPCs */
static void find_free_spot(float *x, float *y)
{
    for (int r = 0; r < 40; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r)
                    continue;
                float cx = floorf(*x) + dx + 0.5f, cy = floorf(*y) + dy + 0.5f;
                if (!blocked_body(cx, cy) && !units_blocking(cx, cy, UNIT_PLAYER_BODY + 0.15f)) {
                    *x = cx;
                    *y = cy;
                    return;
                }
            }
}

/* El lugar libre más cerca de (*x, *y) entre los que se llega caminando
   desde (fx, fy), hasta 8 subtiles (si no hay ninguno, find_free_spot).
   find_free_spot toma el más cerca, que en un pasillo angosto puede quedar
   del otro lado de la pared: al volver por el portal al pueblo los bots
   quedaban encerrados en un hueco del Maggot Lair */
static void reach_free_spot(float fx, float fy, float *x, float *y)
{
    enum { R = 8, W = 2 * R + 1 };
    static const int k_dx[4] = { 1, -1, 0, 0 }, k_dy[4] = { 0, 0, 1, -1 };
    uint8_t seen[W * W];
    int queue[W * W], qh = 0, qt = 0, ox = (int)floorf(fx) - R, oy = (int)floorf(fy) - R;
    float best = 1e9f, bx = fx, by = fy;
    memset(seen, 0, sizeof seen);
    seen[R * W + R] = 1;
    queue[qt++] = R * W + R;
    while (qh < qt) {
        int c = queue[qh++], cx = c % W, cy = c / W;
        float mx = (float)(ox + cx) + 0.5f, my = (float)(oy + cy) + 0.5f;
        float d = (mx - *x) * (mx - *x) + (my - *y) * (my - *y);
        if (d < best && !blocked_body(mx, my) && !units_blocking(mx, my, UNIT_PLAYER_BODY + 0.15f)) {
            best = d;
            bx = mx;
            by = my;
        }
        for (int k = 0; k < 4; k++) {
            int nx = cx + k_dx[k], ny = cy + k_dy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= W || seen[ny * W + nx] ||
                blocked_body((float)(ox + nx) + 0.5f, (float)(oy + ny) + 0.5f))
                continue;
            seen[ny * W + nx] = 1;
            queue[qt++] = ny * W + nx;
        }
    }
    if (best < 1e9f) {
        *x = bx;
        *y = by;
    } else {
        *x = fx;
        *y = fy;
        find_free_spot(x, y);
    }
}

/* Paredes, objetos, NPCs y monstruos. Si ya estaba encimado a alguien (por
   ejemplo al aparecer) lo deja salir en vez de quedar trabado. */
static int player_blocked(float x, float y, float from_x, float from_y)
{
    int to = foot_blocked((int)floorf(x), (int)floorf(y));
    if (to) {
        /* Encimado a una pared (al aparecer o al llegar por un salto): puede
           salir con el centro libre, sin meterse más */
        int from = foot_blocked((int)floorf(from_x), (int)floorf(from_y));
        if (!from || to > from || map_blocked(&s_map, (int)floorf(x), (int)floorf(y)))
            return 1;
    }
    /* El aire con las paredes (blocked_body): si ya estaba más cerca, se
       mueve sin acercarse más; un lugar para caer (x, y = de dónde) no */
    float c2 = map_clearance2(&s_map, x, y);
    if (c2 < MAP_BODY_CLEAR * MAP_BODY_CLEAR - 0.001f &&
        ((x == from_x && y == from_y) || c2 < map_clearance2(&s_map, from_x, from_y) - 0.0001f))
        return 1;
    return units_blocking(x, y, UNIT_PLAYER_BODY) && !units_blocking(from_x, from_y, UNIT_PLAYER_BODY);
}

static int sector_from_screen(float sx, float sy)
{
    int s = (int)lroundf((atan2f(sy, sx) - PI_F / 2.0f) / (PI_F / 8.0f));
    return ((s % 16) + 16) % 16;
}

/* Frases del personaje con su voz ("No tengo suficiente maná"): las de
   sounds.txt que empiezan con la clase, como "barbarian_needmana_1" */
static void player_say(const Player *p, const char *what)
{
    char name[64];
    snprintf(name, sizeof name, "%s_%s", s_classes[p->cls].voice, what);
    audio_play_name(name, p->x, p->y);
}

/* Sonido del arma al cortar el aire y al pegar, según su HitClass
   (hitclass.txt); sin arma, puños */
static const char *const k_swing_sounds[] = {
    "weapon_punch_1", "weapon_punch_1", "weapon_1hs_small_1", "weapon_1hs_large_1", "weapon_2hs_small_1",
    "weapon_2hs_large_1", "weapon_1ht_1", "weapon_2ht_1", "weapon_1hs_large_1", "weapon_staff_1",
    "weapon_bow_1", "weapon_xbow_1", "weapon_punch_1",
};
static const char *const k_impact_sounds[] = {
    "impact_punch_1", "impact_punch_1", "impact_blade_swing_1", "impact_blade_swing_1", "impact_blade_swing_1",
    "impact_blade_swing_1", "impact_blade_thrust_1", "impact_blade_thrust_1", "impact_blunt_1", "impact_blunt_1",
    "impact_arrow_1", "impact_arrow_1", "impact_claw_1",
};

static int weapon_hit_class(const Player *p)
{
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    int hc = w ? w->hit_class : 1;
    return hc > 0 && hc < (int)(sizeof k_swing_sounds / sizeof k_swing_sounds[0]) ? hc : 1;
}

/* Lo que suena al usar una habilidad: el suyo y la voz de la clase (gritos del Bárbaro) */
static void play_skill_sound(const Player *p, const SkillDef *s)
{
    if (!s)
        return;
    audio_play(s->st_sound, p->x, p->y);
    audio_play(s->st_sound_class, p->x, p->y);
}

static void dru_unmorph(Player *p);

static void set_player_state(Player *p, int state, int mode)
{
    p->seq = NULL;
    p->jumping = 0;
    if (state == PS_HIT && mode == MODE_GH) {
        audio_play(s_classes[p->cls].snd_hit, p->x, p->y);
    } else if (state == PS_HIT && mode == MODE_S1) { /* Esquivar */
        int dodge = skills_find(p->cls, "Dodge");
        if (dodge >= 0)
            audio_play(skills_get(p->cls, dodge)->st_sound, p->x, p->y);
    }
    int died = state == PS_DYING && p->state != PS_DYING;
    if (died) {
        audio_play(s_classes[p->cls].snd_death, p->x, p->y);
        if (p->form != FORM_NONE) /* el Druida muere como humano */
            dru_unmorph(p);
        /* PLAYER_ApplyDeathPenalty: el nivel % (hasta 20) del oro. El
           original lo cuenta también con el del cofre y tira el resto del
           que lleva en el cadáver; acá (pedido del usuario) solo se pierde
           esa parte del que lleva: el equipo y el cofre no se tocan */
        int pct = p->level_num < 20 ? p->level_num : 20;
        p->gold_lost = s_eco ? 0 : (int)((int64_t)p->gold * pct / 100); /* (en el Eco no se pierde) */
        p->gold -= p->gold_lost;
    }
    p->state = state;
    p->mode = mode;
    if (died && s_act == 5 && !s_eco)
        a5_player_died(p); /* (en la cumbre: los Antiguos) */
    p->anim_acc = 0;
    p->charge_ticks = 0;
}

static void gain_exp(Player *p, uint32_t exp)
{
    const CharClass *c = &s_classes[p->cls];
    p->exp = exp > 0xFFFFFFFFu - p->exp ? 0xFFFFFFFFu : p->exp + exp; /* (sin pasarse de 32 bits) */
    while (p->level_num < 99 && p->exp >= c->exp_next[p->level_num]) {
        p->level_num++;
        p->skill_points++;
        p->stat_points += 5; /* StatPerLevel */
        refresh_stats(p);
        p->hp = p->hp_max;
        p->levelup_ticks = 3 * TICK_RATE;
        /* La mercenaria sube con su dueño (nunca lo pasa) */
        if (p->merc_name && !p->merc_dead && p->merc_level < p->level_num) {
            p->merc_level++;
            merc_apply(p, -1);
        }
        audio_play_name("cursor_level_up", AUDIO_NOPOS, AUDIO_NOPOS);
        save_character(p);
    }
}

/* Experiencia de una muerte: el que mató y los compañeros cercanos */
static void share_exp(const Player *killer, int exp)
{
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        float dx = p->x - killer->x, dy = p->y - killer->y;
        if (p->state < PS_DYING && dx * dx + dy * dy <= EXP_SHARE_RANGE * EXP_SHARE_RANGE) /* (con el santuario de experiencia) */
            gain_exp(p, p->shrine_ticks > 0 && p->shrine_kind == 15 ? exp * (100 + p->shrine_arg0) / 100 : exp);
    }
}

/* Animación del modo con el equipo puesto, o la de la clase sin equipo */
/* La animación para la lógica: la del equipo que lleva ahora (su COF se lee al cambiarlo), así la
   partida no depende de cuándo termina de cargarse el dibujo (en red, todas las máquinas tienen que
   calcular lo mismo) */
static const CharAnim *player_anim(const Player *p, int mode)
{
    return p->meta[mode].ok ? &p->meta[mode] : &s_classes[p->cls].anim[mode];
}

/* La que se dibuja: la que ya se cargó (hasta que llega la del equipo nuevo, la de antes) */
static const CharAnim *player_anim_draw(const Player *p, int mode)
{
    return p->anim[mode].ok ? &p->anim[mode] : &s_classes[p->cls].anim[mode];
}

/* Arma el aspecto con el equipo: el arma en la mano derecha (y su clase de
   animación), escudo, casco y las piezas de la armadura de cuerpo */
static void build_look(const Player *p, CharLook *look, char *wclass, size_t wcap)
{
    static const char *const weight[3] = { "lit", "med", "hvy" };
    memset(look, 0, sizeof *look);
    if (p->form != FORM_NONE && units_monster_look(p->form == FORM_WOLF ? "wolf" : "bear", look, wclass, wcap) == 0)
        return; /* el Druida transformado: el lobo o el oso (sin el equipo) */
    memset(look, 0, sizeof *look);
    snprintf(look->token, sizeof look->token, "%s", s_classes[p->cls].token);
    const ItemType *body = p->equip[SLOT_BODY].type, *helm = p->equip[SLOT_HELM].type;
    const ItemType *weapon = p->equip[SLOT_WEAPON].type, *shield = p->equip[SLOT_SHIELD].type;
    static const int comps[6] = { COMP_RA, COMP_LA, COMP_TR, COMP_LG, COMP_S1, COMP_S2 };
    for (int k = 0; k < 6; k++) {
        int w = body && body->body_parts[k] >= 0 && body->body_parts[k] <= 2 ? body->body_parts[k] : 0;
        snprintf(look->part[comps[k]], sizeof look->part[comps[k]], "%s", weight[w]);
    }
    snprintf(look->part[COMP_HD], sizeof look->part[COMP_HD], "%s", helm ? helm->gfx : "lit");
    /* Los arcos se llevan en la mano izquierda (así están los dibujos del
       original); el resto de las armas, en la derecha */
    if (weapon && items_type_is(weapon, "bow"))
        snprintf(look->part[COMP_LH], sizeof look->part[COMP_LH], "%s", weapon->gfx);
    else if (weapon)
        snprintf(look->part[COMP_RH], sizeof look->part[COMP_RH], "%s", weapon->gfx);
    snprintf(wclass, wcap, "%s", weapon && weapon->wclass[0] ? weapon->wclass : "hth");
    if (shield && shield->kind == ITEM_WEAPON && items_type_is(shield, "h2h")) {
        /* Dos garras (la Asesina): la segunda en la mano izquierda, ht2 */
        snprintf(look->part[COMP_LH], sizeof look->part[COMP_LH], "%s", shield->gfx);
        snprintf(wclass, wcap, "ht2");
    } else if (shield && shield->kind == ITEM_WEAPON) {
        /* Dos armas (el Bárbaro): la segunda va en la mano izquierda y la
           clase de animación es la doble del original (D2Composit): 1ss
           corte y corte, 1js corte y estocada, 1jt dos estocadas, 1st
           estocada y corte */
        snprintf(look->part[COMP_LH], sizeof look->part[COMP_LH], "%s", shield->gfx);
        int a = weapon && strcmp(weapon->wclass, "1ht") == 0, b = strcmp(shield->wclass, "1ht") == 0;
        snprintf(wclass, wcap, "%s", !a ? (b ? "1js" : "1ss") : (b ? "1jt" : "1st"));
    } else if (shield) {
        snprintf(look->part[COMP_SH], sizeof look->part[COMP_SH], "%s", shield->gfx);
    }
}

/* El aspecto nuevo (al cambiar el equipo) se decodifica en el hilo de
   trabajos: son todas las animaciones del personaje y en la Pi tardan más
   de un segundo. Mientras tanto se lo ve con el equipo de antes. */
typedef struct {
    int          state; /* LOOK_FREE, LOOK_LOADING, LOOK_READY (job_flag_get/set) */
    CharAnim     anim[MODE_COUNT];
    CharLook     look;
    char         wclass[4];
    char         key[64];
} LookJob;
enum { LOOK_FREE, LOOK_LOADING, LOOK_READY };
static LookJob s_look_job[MAX_PLAYERS];

/* Con meta, solo lo de la lógica (los COF) */
static int look_load(CharAnim *a, const CharLook *look, const char *mode, const char *wclass, int meta)
{
    return meta ? charanim_load_meta(a, look, mode, wclass) : charanim_load(a, look, mode, wclass);
}

static void load_look_anims_as(CharAnim anim[MODE_COUNT], const CharLook *look, const char *wclass, int meta);

static void load_look_anims(CharAnim anim[MODE_COUNT], const CharLook *look, const char *wclass)
{
    load_look_anims_as(anim, look, wclass, 0);
}

static void load_look_anims_as(CharAnim anim[MODE_COUNT], const CharLook *look, const char *wclass, int meta)
{
    if (strcmp(look->base, "monsters") == 0) { /* el lobo o el oso del Druida: sus modos (el pueblo, como afuera) */
        static const char *const k_beast[MODE_COUNT] = {
            [MODE_TN] = "nu", [MODE_TW] = "wl", [MODE_RN] = "rn", [MODE_NU] = "nu", [MODE_WL] = "wl", [MODE_A1] = "a1",
            [MODE_GH] = "gh", [MODE_SC] = "sc", [MODE_S1] = "s1", [MODE_A2] = "a2", [MODE_S3] = "s3", [MODE_BL] = "bl",
            [MODE_S4] = "s4",
        };
        for (int m = 0; m < MODE_COUNT; m++)
            if (k_beast[m])
                look_load(&anim[m], look, k_beast[m], wclass, meta);
        return;
    }
    for (int m = 0; m < MODE_COUNT; m++) {
        const char *wc = (m == MODE_DT || m == MODE_DD) ? "hth" : wclass; /* la muerte solo existe sin arma */
        /* El segundo ataque solo existe con arma (sin arma, el juego trae el
           COF de la Amazona pero no sus partes): si falta, se usa el primero */
        if (m == MODE_A2 && strcmp(wc, "hth") == 0)
            continue;
        /* Bloquear: solo con escudo (o con dos garras: Bloqueo con armas) */
        if (m == MODE_BL) {
            if (look->part[COMP_SH][0] || strcmp(wc, "ht2") == 0)
                look_load(&anim[m], look, k_mode_codes[m], wc, meta);
            continue;
        }
        /* Patada, trampas y el segundo golpe con dos garras: solo la Asesina */
        if (m == MODE_KK || m == MODE_S2 || m == MODE_S4) {
            if (strcmp(look->token, "ai") == 0 && (m != MODE_S4 || strcmp(wc, "ht2") == 0))
                look_load(&anim[m], look, k_mode_codes[m], wc, meta);
            continue;
        }
        /* El segundo golpe de Doble golpe y Frenesí: solo con dos armas */
        if (m == MODE_S3) {
            if (wc[0] == '1' && (wc[1] == 's' || wc[1] == 'j'))
                look_load(&anim[m], look, k_mode_codes[m], wc, meta);
            continue;
        }
        /* Lanzar tampoco: con un arma que no tiene esa animación (una lanza)
           se usa la del ataque, y la de sin arma no trae las partes del arma */
        if (look_load(&anim[m], look, k_mode_codes[m], wc, meta) != 0 && m != MODE_A2 && m != MODE_TH)
            look_load(&anim[m], look, k_mode_codes[m], "hth", meta);
    }
}

static void look_job(void *ptr, int arg)
{
    (void)arg;
    LookJob *j = ptr;
    load_look_anims(j->anim, &j->look, j->wclass);
    job_flag_set(&j->state, LOOK_READY);
}

/* Que no quede un aspecto cargándose para un jugador que se borra */
static void look_job_drop(int i)
{
    LookJob *j = &s_look_job[i];
    if (job_flag_get(&j->state) == LOOK_LOADING)
        jobs_wait(10000);
    if (job_flag_get(&j->state) == LOOK_READY)
        for (int m = 0; m < MODE_COUNT; m++)
            charanim_free(&j->anim[m]);
    memset(j->anim, 0, sizeof j->anim);
    job_flag_set(&j->state, LOOK_FREE);
}

static void free_look(Player *p)
{
    for (int m = 0; m < MODE_COUNT; m++) {
        charanim_free(&p->anim[m]);
        charanim_free(&p->meta[m]);
    }
    p->look_key[0] = p->meta_key[0] = '\0';
}

/* Las fórmulas de skills.txt con los puntos puestos (para las sinergias) */
static SkillCtx pctx(const Player *p)
{
    SkillCtx c = { p->cls, p->skill_lvl, p->level_num };
    return c;
}

/* El nivel con el que se usa: los puntos más Comando de batalla */
static int eff_lvl(const Player *p, int skill)
{
    int l = skill >= 0 ? p->skill_lvl[skill] : 0;
    return l > 0 ? l + p->bc_bonus + p->item_sk[skill] : 0; /* y los + de los objetos (como el original, si tiene puntos) */
}

/* ¿Lleva dos armas? (el Bárbaro: la segunda va en el lugar del escudo) */
static int dual_wield(const Player *p)
{
    const ItemType *o = p->equip[SLOT_SHIELD].type;
    return o && o->kind == ITEM_WEAPON;
}

/* ¿Puede ir esa arma en la mano izquierda? Como en el original, solo el
   Bárbaro y con armas de una mano (las arrojadizas también) */
static int offhand_weapon_ok(const Player *p, const ItemType *t)
{
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    if (p->cls == 5) /* la Asesina: una garra en cada mano */
        return t && t->kind == ITEM_WEAPON && items_type_is(t, "h2h") && (!w || items_type_is(w, "h2h"));
    return p->cls == 0 && t && t->kind == ITEM_WEAPON && !t->two_handed && !items_ammo_for(t) &&
           (!w || !w->two_handed);
}

/* Recarga las animaciones si cambió el equipo que se ve. La muerte solo
   existe sin arma ("hth"); si falta algún modo con el arma, se usa sin arma. */
/* ¿El arma que tiene puesta sirve para la habilidad? (Estocada pide lanza o
   jabalina; Porrazo, un arma cuerpo a cuerpo, que pueden ser los puños) */
static int skill_has_passive(const SkillDef *s)
{
    if (war_kind(s->kind)) /* el Brujo: sus passivestat son lo de los maleficios y los demonios */
        return s->passive;
    return s->passivestat[0][0] && !s->summon[0] && s->kind != SK_BLAZE && s->kind != SK_CLOAK_SHADOWS &&
           s->kind != SK_FADE;
}

static int weapon_allows(const Player *p, const SkillDef *s)
{
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    if (!s->weapon_itype[0][0])
        return 1;
    for (int k = 0; k < 3; k++) {
        const char *t = s->weapon_itype[k];
        if (strcmp(t, "shld") == 0) { /* Castigar y Escudo sagrado: un escudo en la otra mano */
            const ItemType *sh = p->equip[SLOT_SHIELD].type;
            if (sh && sh->kind == ITEM_ARMOR && items_type_is(sh, "shld"))
                return 1;
            continue;
        }
        if (t[0] && (w ? items_type_is(w, t) : strcmp(t, "mele") == 0)) {
            if (s->weapon_itypeb[0]) { /* y en la otra mano (Garra del dragón: otra garra) */
                const ItemType *o = p->equip[SLOT_SHIELD].type;
                return o && o->kind == ITEM_WEAPON && items_type_is(o, s->weapon_itypeb);
            }
            return 1;
        }
    }
    return 0;
}

/* Bonus de las habilidades pasivas aprendidas */
static void refresh_passives(Player *p)
{
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    SkillCtx ctx = pctx(p);
    int no_crit = 100, no_tcrit = 100; /* las probabilidades de crítico se combinan */
    p->sk_dmg_pct = p->sk_ar_pct = p->sk_def_pct = p->sk_stam_pct = p->sk_dodge = 0;
    p->sk_avoid = p->sk_evade = p->sk_pierce = p->sk_speed = 0;
    p->sk_throw_ar = p->sk_throw_dmg = p->sk_throw_crit = 0;
    p->sk_fire_pct = p->sk_ltng_pct = p->sk_cold_pierce = p->sk_mana_regen = 0;
    p->sk_wblock = 0;
    memset(p->resist, 0, sizeof p->resist);
    memset(p->res_max, 0, sizeof p->res_max);
    for (int i = 0; i < skills_count(p->cls); i++) {
        int lvl = eff_lvl(p, i);
        const SkillDef *s = skills_get(p->cls, i);
        if (lvl <= 0 || !skill_has_passive(s) || war_kind(s->kind))
            continue;
        if (s->aura && !s->passive_always) /* las del aura puesta las suma pal_auras_tick */
            continue;
        /* Los dominios, con su tipo de arma; Golpe crítico, Penetrar y
           Perforar, con las armas de la habilidad */
        if (s->passive_itype[0] ? !(w && items_type_is(w, s->passive_itype))
                                : s->weapon_itype[0][0] && !(w && weapon_allows(p, s)))
            continue;
        for (int k = 0; k < 3; k++) {
            const char *stat = s->passivestat[k];
            if (!stat[0])
                continue;
            int v = skill_calc(s, SC_PASSIVE1 + k, lvl, &ctx);
            if (!strcmp(stat, "passive_mastery_melee_th") || !strcmp(stat, "item_tohit_percent"))
                p->sk_ar_pct += v;
            else if (!strcmp(stat, "passive_mastery_melee_dmg"))
                p->sk_dmg_pct += v;
            else if (!strcmp(stat, "passive_mastery_melee_crit") || !strcmp(stat, "passive_critical_strike"))
                no_crit = no_crit * (100 - v) / 100;
            else if (!strcmp(stat, "passive_mastery_throw_th"))
                p->sk_throw_ar += v;
            else if (!strcmp(stat, "passive_mastery_throw_dmg"))
                p->sk_throw_dmg += v;
            else if (!strcmp(stat, "passive_mastery_throw_crit"))
                no_tcrit = no_tcrit * (100 - v) / 100;
            else if (!strcmp(stat, "passive_dodge"))
                p->sk_dodge = v;
            else if (!strcmp(stat, "passive_avoid"))
                p->sk_avoid = v;
            else if (!strcmp(stat, "passive_evade"))
                p->sk_evade = v;
            else if (!strcmp(stat, "skill_armor_percent"))
                p->sk_def_pct += v;
            else if (!strcmp(stat, "skill_passive_staminapercent"))
                p->sk_stam_pct += v;
            else if (!strcmp(stat, "velocitypercent"))
                p->sk_speed += v;
            else if (!strcmp(stat, "fireresist"))
                p->resist[0] += v;
            else if (!strcmp(stat, "coldresist"))
                p->resist[1] += v;
            else if (!strcmp(stat, "lightresist"))
                p->resist[2] += v;
            else if (!strcmp(stat, "skill_pierce"))
                p->sk_pierce += v;
            else if (!strcmp(stat, "manarecoverybonus"))
                p->sk_mana_regen += v;
            else if (!strcmp(stat, "passive_fire_mastery"))
                p->sk_fire_pct += v;
            else if (!strcmp(stat, "passive_ltng_mastery"))
                p->sk_ltng_pct += v;
            else if (!strcmp(stat, "passive_cold_pierce"))
                p->sk_cold_pierce += v;
            else if (!strcmp(stat, "maxfireresist"))
                p->res_max[0] += v;
            else if (!strcmp(stat, "maxcoldresist"))
                p->res_max[1] += v;
            else if (!strcmp(stat, "maxlightresist"))
                p->res_max[2] += v;
            else if (!strcmp(stat, "passive_weaponblock"))
                p->sk_wblock += v;
        }
    }
    if (p->fade_ticks > 0) /* Desvanecerse */
        for (int k = 0; k < 3; k++)
            p->resist[k] += p->fade_res;
    p->sk_stam_pct += p->form_stam; /* el Hombre lobo */
    p->sk_crit = 100 - no_crit;
    p->sk_throw_crit = 100 - no_tcrit;
    if (p->cls == 7) /* el Brujo: la Levitación, el maleficio puesto, Consumir */
        war_passives(p);
}

/* Las propiedades de lo que lleva: el equipo puesto, lo que dan los
   conjuntos y los talismanes de la mochila */
#define MAX_PLAYER_PROPS 192
static int player_props(const Player *p, ItemProp *out, int cap)
{
    const Item *worn[SLOT_COUNT];
    int n = 0, nworn = 0;
    for (int s = 0; s < SLOT_COUNT; s++) {
        const Item *it = &p->equip[s];
        if (!it->type)
            continue;
        worn[nworn++] = it;
        for (int k = 0; k < it->num_stats; k++)
            n = itemstats_add(out, n, cap, it->stat[k], it->param[k], it->value[k]);
    }
    for (int i = 0; i < p->bag_count; i++) {
        const Item *it = &p->bag[i];
        if (it->type && it->type->charm)
            for (int k = 0; k < it->num_stats; k++)
                n = itemstats_add(out, n, cap, it->stat[k], it->param[k], it->value[k]);
    }
    ItemProp set[64];
    int ns = items_set_bonus(worn, nworn, set, 64);
    for (int k = 0; k < ns; k++)
        n = itemstats_add(out, n, cap, set[k].stat, set[k].param, set[k].value);
    return n;
}

/* Los + a las habilidades de los objetos: a todas, a las de una clase
   (las clases del original), a una solapa, a una sola y a las de fuego */
static void item_skill_bonus(Player *p, const ItemProp *pr, int n)
{
    static const char *const k_elem[6] = { "", "fire", "ltng", "mag", "cold", "pois" };
    int d2 = itemstats_class_to_d2(p->cls), count = skills_count(p->cls);
    int add[MAX_CLASS_SKILLS] = { 0 };
    for (int i = 0; i < n; i++) {
        int v = pr[i].value, par = pr[i].param, all = 0;
        switch (pr[i].stat) {
        case STAT_ALL_SKILLS:
            all = v;
            break;
        case STAT_CLASS_SKILLS:
            all = par == d2 ? v : 0;
            break;
        case STAT_SKILL_TAB:
            for (int k = 0; (par >> 3) == d2 && k < count; k++)
                if (skills_get(p->cls, k)->page == (par & 7) + 1)
                    add[k] += v;
            break;
        case STAT_SINGLE_SKILL:
        case STAT_OSKILL: {
            int k = skills_global_class(par) == p->cls ? skills_global_local(par) : -1;
            if (k >= 0 && k < count)
                add[k] += v;
            break;
        }
        case STAT_ELEM_SKILLS:
            for (int k = 0; par > 0 && par < 6 && k < count; k++)
                if (strcmp(skills_get(p->cls, k)->etype, k_elem[par]) == 0)
                    add[k] += v;
            break;
        default:
            break;
        }
        for (int k = 0; all && k < count; k++)
            add[k] += all;
    }
    for (int k = 0; k < MAX_CLASS_SKILLS; k++)
        p->item_sk[k] = (int8_t)(add[k] > 60 ? 60 : add[k] < -60 ? -60 : add[k]);
}

/* Suma las propiedades mágicas del equipo puesto (y los conjuntos y los
   talismanes) y recalcula la vida, el maná y la estamina máximos (sin
   pasarse de los nuevos máximos) */
static void refresh_stats(Player *p)
{
    const CharClass *c = &s_classes[p->cls];
    memset(p->bonus, 0, sizeof p->bonus);
    ItemProp pr[MAX_PLAYER_PROPS];
    int np = player_props(p, pr, MAX_PLAYER_PROPS);
    np = shrine_props(p, pr, np, MAX_PLAYER_PROPS); /* el santuario que dura */
    item_skill_bonus(p, pr, np);
    p->lvl_ac_pct = p->lvl_dmg_pct = 0;
    p->stats_dirty = 0;
    for (int k = 0; k < np; k++) {
        int st = pr[k].stat, amount = 0, to = itemstats_per_level(st, pr[k].value, p->level_num, &amount);
        if (to == STAT_AC_PCT) /* las que suben con el nivel van a la suya */
            p->lvl_ac_pct += amount;
        else if (to == STAT_DMG_PCT)
            p->lvl_dmg_pct += amount;
        else if (to >= 0 && to < STAT_COUNT)
            p->bonus[to] += amount;
        else if (st < STAT_COUNT)
            p->bonus[st] += pr[k].value;
    }
    if (p->cls == 7)
        war_consume_stats(p); /* Consumir: lo del demonio sacrificado, como de los objetos */
    refresh_passives(p);
    p->sk_fire_pct += p->bonus[STAT_FIRE_MASTERY]; /* los dominios de los objetos (Pellejo del Víbora...) */
    p->sk_ltng_pct += p->bonus[STAT_LTNG_MASTERY];
    p->sk_mana_regen += p->bonus[STAT_MANA_REGEN];
    if (p->enchant_ticks > 0) { /* Encantar */
        p->bonus[STAT_FIRE_MIN] += p->enchant_min;
        p->bonus[STAT_FIRE_MAX] += p->enchant_max;
    }
    /* Vida, maná y estamina: lo de la clase, lo que da cada nivel y lo que
       da cada punto de vitalidad o energía (charstats.txt) */
    int lv = p->level_num - 1, vit = p->stat_add[ATTR_VIT], ene = p->stat_add[ATTR_ENE];
    int ivit = p->bonus[STAT_VIT], iene = p->bonus[STAT_ENERGY]; /* la vitalidad y la energía de los objetos */
    p->hp_base = c->life + lv * c->life_per_level + vit * c->life_per_vit4 / 4 + p->hp_extra;
    p->mana_base = c->mana + lv * c->mana_per_level4 / 4 + ene * c->mana_per_energy4 / 4;
    int bo = p->bo_ticks > 0 ? p->bo_pct : 0; /* Órdenes de batalla */
    /* Órdenes de batalla, la forma del Druida (con Licantropía), el Sabio roble y el % de los objetos */
    p->hp_max = (p->hp_base + ivit * c->life_per_vit4 / 4 + p->bonus[STAT_LIFE]) *
                (100 + bo + p->form_life + p->au_life_pct + p->bonus[STAT_LIFE_PCT]) / 100;
    p->mana_max = (p->mana_base + iene * c->mana_per_energy4 / 4 + p->bonus[STAT_MANA]) *
                  (100 + bo + p->bonus[STAT_MANA_PCT]) / 100;
    p->stamina_max = (c->stamina + lv * c->stamina_per_level4 / 4.0f + (vit + ivit) * c->stamina_per_vit4 / 4.0f +
                      (float)p->bonus[STAT_STAMINA]) * (100 + p->sk_stam_pct + p->au_stam_pct + bo) / 100.0f;
    if (p->hp_max < 1) p->hp_max = 1;
    if (p->mana_max < 0) p->mana_max = 0;
    if (p->stamina_max < 1.0f) p->stamina_max = 1.0f;
    if (p->hp > p->hp_max) p->hp = p->hp_max;
    if (p->mana > p->mana_max) p->mana = p->mana_max;
    if (p->stamina > p->stamina_max) p->stamina = p->stamina_max;
}

/* Carga el aspecto del equipo actual si cambió: en otro hilo si anda (y no
   hay otro cargándose; al terminar ese se mira si sigue siendo el que va) */
static void start_look(Player *p)
{
    CharLook look;
    char wclass[4], key[64];
    build_look(p, &look, wclass, sizeof wclass);
    snprintf(key, sizeof key, "%s %s %s %s %s %s %s %s %s %s", look.token, wclass, look.part[COMP_HD],
             look.part[COMP_TR], look.part[COMP_LG], look.part[COMP_RA], look.part[COMP_LA], look.part[COMP_S1],
             look.part[COMP_RH], look.part[COMP_SH]);
    snprintf(p->look_want, sizeof p->look_want, "%s", key);
    if (strcmp(key, p->meta_key) != 0) { /* lo de la lógica, ya (los COF son chicos) */
        for (int m = 0; m < MODE_COUNT; m++)
            charanim_free(&p->meta[m]);
        load_look_anims_as(p->meta, &look, wclass, 1);
        snprintf(p->meta_key, sizeof p->meta_key, "%s", key);
    }
    if (strcmp(key, p->look_key) == 0)
        return;
    LookJob *j = &s_look_job[p - s_pl];
    if (!jobs_running() || !p->look_key[0]) { /* sin hilo, o al empezar (todavía no tiene ninguno) */
        for (int m = 0; m < MODE_COUNT; m++) /* (solo los dibujos: lo de la lógica ya se leyó arriba) */
            charanim_free(&p->anim[m]);
        load_look_anims(p->anim, &look, wclass);
        snprintf(p->look_key, sizeof p->look_key, "%s", key);
        return;
    }
    if (job_flag_get(&j->state) != LOOK_FREE)
        return;
    j->look = look;
    snprintf(j->wclass, sizeof j->wclass, "%s", wclass);
    snprintf(j->key, sizeof j->key, "%s", key);
    memset(j->anim, 0, sizeof j->anim);
    job_flag_set(&j->state, LOOK_LOADING);
    jobs_push(look_job, j, 0);
}

/* Cada tick: si el aspecto nuevo terminó de cargarse, se lo pone */
/* Soltar un aspecto son miles de free() (cada capa, cuadro y dirección): en
   la Pi tardaba de 36 a 80 ms y trababa el juego al cambiar de arma. Se
   suelta en el otro hilo (sin hilo, en el momento). */
typedef struct {
    CharAnim anim[MODE_COUNT];
} OldLook;

static void old_look_job(void *ptr, int arg)
{
    (void)arg;
    OldLook *o = ptr;
    for (int m = 0; m < MODE_COUNT; m++)
        charanim_free(&o->anim[m]);
    free(o);
}

static void drop_anims(CharAnim *anim)
{
    OldLook *o = jobs_running() ? malloc(sizeof *o) : NULL;
    if (o) {
        memcpy(o->anim, anim, sizeof o->anim);
        jobs_push(old_look_job, o, 0);
    } else {
        for (int m = 0; m < MODE_COUNT; m++)
            charanim_free(&anim[m]);
    }
    memset(anim, 0, sizeof(CharAnim) * MODE_COUNT);
}

static void look_tick(Player *p)
{
    LookJob *j = &s_look_job[p - s_pl];
    if (job_flag_get(&j->state) != LOOK_READY)
        return;
    if (strcmp(j->key, p->look_want) == 0) {
        drop_anims(p->anim);
        memcpy(p->anim, j->anim, sizeof p->anim);
        snprintf(p->look_key, sizeof p->look_key, "%s", j->key);
    } else { /* mientras se cargaba se cambió el equipo otra vez */
        drop_anims(j->anim);
    }
    memset(j->anim, 0, sizeof j->anim);
    job_flag_set(&j->state, LOOK_FREE);
    if (strcmp(p->look_want, p->look_key) != 0)
        start_look(p);
}

static void refresh_look(Player *p)
{
    refresh_stats(p); /* cambió el equipo: también los atributos */
    start_look(p);
}

/* ---- Objetos del jugador ---- */

static void show_message(Player *p, const char *text)
{
    snprintf(p->message, sizeof p->message, "%.*s", (int)sizeof p->message - 1, tr(text));
    p->message_ticks = 2 * TICK_RATE;
}

static void inventory_full(Player *p)
{
    show_message(p, "Inventario lleno");
    player_say(p, "cantcarry_1"); /* "No puedo cargar más" */
}

/* ---- Mochila de 10x4 casillas: cada objeto en su lugar, como el original ---- */

/* Objeto que ocupa la casilla (x, y), o -1 */
static int bag_at(const Player *p, int x, int y)
{
    for (int i = 0; i < p->bag_count; i++) {
        const ItemType *t = p->bag[i].type;
        if (x >= p->bag_x[i] && x < p->bag_x[i] + t->inv_w && y >= p->bag_y[i] && y < p->bag_y[i] + t->inv_h)
            return i;
    }
    return -1;
}

/* ¿Entra un objeto de ese tamaño con la esquina en (x, y)? (sin contar `ignore`) */
static int bag_fits(const Player *p, const ItemType *t, int x, int y, int ignore)
{
    if (x < 0 || y < 0 || x + t->inv_w > BAG_W || y + t->inv_h > BAG_H)
        return 0;
    for (int a = 0; a < t->inv_h; a++)
        for (int b = 0; b < t->inv_w; b++) {
            int k = bag_at(p, x + b, y + a);
            if (k >= 0 && k != ignore)
                return 0;
        }
    return 1;
}

static int bag_add_at(Player *p, const Item *it, int x, int y)
{
    if (p->bag_count >= BAG_MAX || !bag_fits(p, it->type, x, y, -1))
        return 0;
    p->bag[p->bag_count] = *it;
    p->bag_x[p->bag_count] = (uint8_t)x;
    p->bag_y[p->bag_count] = (uint8_t)y;
    p->bag_count++;
    if (it->type->charm) /* los talismanes dan lo suyo desde la mochila */
        p->stats_dirty = 1;
    return 1;
}

/* ---- El Cubo Horádrico: una grilla de 3 x 4, como la mochila ---- */

static int cube_at(const Player *p, int x, int y)
{
    for (int i = 0; i < p->cube_count; i++) {
        const ItemType *t = p->cube[i].type;
        if (x >= p->cube_x[i] && x < p->cube_x[i] + t->inv_w && y >= p->cube_y[i] && y < p->cube_y[i] + t->inv_h)
            return i;
    }
    return -1;
}

static int cube_add_at(Player *p, const Item *it, int x, int y)
{
    const ItemType *t = it->type;
    if (p->cube_count >= CUBE_W * CUBE_H || x < 0 || y < 0 || x + t->inv_w > CUBE_W || y + t->inv_h > CUBE_H ||
        strcmp(t->code, "box") == 0)
        return 0;
    for (int a = 0; a < t->inv_h; a++)
        for (int b = 0; b < t->inv_w; b++)
            if (cube_at(p, x + b, y + a) >= 0)
                return 0;
    p->cube[p->cube_count] = *it;
    p->cube_x[p->cube_count] = (uint8_t)x;
    p->cube_y[p->cube_count] = (uint8_t)y;
    p->cube_count++;
    return 1;
}

static int cube_add(Player *p, const Item *it)
{
    for (int y = 0; y < CUBE_H; y++)
        for (int x = 0; x < CUBE_W; x++)
            if (cube_add_at(p, it, x, y))
                return 1;
    return 0;
}

static void cube_remove(Player *p, int i)
{
    for (int k = i; k < p->cube_count - 1; k++) {
        p->cube[k] = p->cube[k + 1];
        p->cube_x[k] = p->cube_x[k + 1];
        p->cube_y[k] = p->cube_y[k + 1];
    }
    p->cube_count--;
}

/* El primer lugar libre recorriendo por columnas, de izquierda a derecha */
static int bag_add(Player *p, const Item *it)
{
    /* Primero en una pila igual donde entre entero (pociones, llaves) */
    for (int k = 0; k < p->bag_count; k++)
        if (stack_fits(&p->bag[k], it)) {
            p->bag[k].amount += it->amount > 0 ? it->amount : 1;
            return 1;
        }
    /* Las llaves y los pergaminos: se llena la pila que tenga lugar y los
       que sobran van a otro (solo si hay dónde: si no, no se levanta nada) */
    for (int k = 0; k < p->bag_count && stack_cap(it->type) > 0 && !items_is_potion(it->type); k++) {
        Item *b = &p->bag[k];
        int room = stack_cap(b->type) - b->amount;
        if (b->type != it->type || room <= 0)
            continue;
        for (int x = 0; x + it->type->inv_w <= BAG_W; x++)
            for (int y = 0; y + it->type->inv_h <= BAG_H; y++)
                if (bag_fits(p, it->type, x, y, -1)) {
                    Item rest = *it;
                    rest.amount -= room;
                    b->amount += room;
                    return bag_add_at(p, &rest, x, y);
                }
        break;
    }
    for (int x = 0; x + it->type->inv_w <= BAG_W; x++)
        for (int y = 0; y + it->type->inv_h <= BAG_H; y++)
            if (bag_add_at(p, it, x, y))
                return 1;
    return 0;
}

/* Ordenar: primero lo más grande, después por tipo; todo bien apretado */
static int cmp_bag_items(const void *a, const void *b)
{
    const Item *x = a, *y = b;
    int ax = x->type->inv_w * x->type->inv_h, ay = y->type->inv_w * y->type->inv_h;
    if (ax != ay)
        return ay - ax;
    if (x->type->inv_h != y->type->inv_h)
        return y->type->inv_h - x->type->inv_h;
    if (x->type->kind != y->type->kind)
        return (int)x->type->kind - (int)y->type->kind;
    return strcmp(x->type->code, y->type->code);
}

/* Ordenar de forma estable (los iguales quedan como estaban): el qsort de cada sistema deja los
   empates en otro orden, y en red todas las máquinas tienen que ordenar igual */
static void sort_items(Item *items, int n)
{
    for (int i = 1; i < n; i++) {
        Item it = items[i];
        int j = i;
        for (; j > 0 && cmp_bag_items(&items[j - 1], &it) > 0; j--)
            items[j] = items[j - 1];
        items[j] = it;
    }
}

static void bag_sort(Player *p)
{
    Item items[BAG_MAX];
    int n = p->bag_count;
    memcpy(items, p->bag, sizeof(Item) * (size_t)n);
    sort_items(items, n);
    p->bag_count = 0;
    for (int i = 0; i < n; i++)
        if (!bag_add(p, &items[i])) /* no debería pasar: lo que no entra cae al suelo */
            items_ground_add(&items[i], p->x + 0.8f, p->y + 0.8f);
}

static int is_potion(const ItemType *t)
{
    return items_is_potion(t);
}

/* ---- Pilas en la mochila ----
   Las pociones se apilan hasta POTION_STACK por casilla, las llaves hasta
   KEY_STACK y los Pergaminos de Portal hasta TP_STACK (pedido del usuario:
   el original no apila pociones, las llaves van de a 12 y los pergaminos se
   guardan en el tomo, que acá no hay). En una pila de pociones `amount` son
   los tragos de todas (POTION_CHARGES cada una; la de arriba es la
   empezada), así el guardado no cambia; en las llaves y los pergaminos, son
   cuántos hay. Al cinturón y al cubo pasan de a una. */

/* Cuántas hay en la casilla */
static int stack_count(const Item *it)
{
    if (!it->type)
        return 0;
    if (is_potion(it->type))
        return it->amount > POTION_CHARGES ? (it->amount + POTION_CHARGES - 1) / POTION_CHARGES : 1;
    if (strcmp(it->type->code, "key") == 0 || strcmp(it->type->code, "tsc") == 0)
        return it->amount > 0 ? it->amount : 1;
    return 1;
}

/* Lo que entra en una casilla, en unidades de amount (0 = no se apila) */
static int stack_cap(const ItemType *t)
{
    if (is_potion(t))
        return POTION_STACK * POTION_CHARGES;
    if (strcmp(t->code, "key") == 0)
        return t->max_stack;
    if (strcmp(t->code, "tsc") == 0)
        return TP_STACK;
    return 0;
}

/* ¿Entra `it` entero en la pila `into`? (dos pociones empezadas no se
   juntan: los tragos de cada una se perderían) */
static int stack_fits(const Item *into, const Item *it)
{
    int cap = stack_cap(it->type), a = it->amount > 0 ? it->amount : 1;
    if (!cap || into->type != it->type || into->quality != it->quality || into->amount + a > cap)
        return 0;
    return !is_potion(it->type) || into->amount % POTION_CHARGES == 0 || a % POTION_CHARGES == 0;
}

/* Saca una poción de la pila: la de arriba (la empezada, si hay); la pila
   puede quedar vacía (amount 0) */
static Item potion_take_one(Item *stack)
{
    Item one = *stack;
    int top = stack->amount % POTION_CHARGES;
    one.amount = top ? top : stack->amount > 0 ? POTION_CHARGES : 0;
    stack->amount -= one.amount;
    return one;
}

/* ---- El cofre (stash) ----
   Pestañas de STASH_W x STASH_H, cada objeto en su lugar como en la mochila.
   Se abre con A en el cofre de cada pueblo (el objeto Bank, OperateFn 32) y
   ocupa en el inventario el lugar del equipo, como el cubo. */

/* Las pestañas que tiene: una por acto al que llegó y las compradas */
static int stash_tabs(const Player *p)
{
    int acts = p->act_max < 1 ? 1 : p->act_max > STASH_ACTS ? STASH_ACTS : p->act_max;
    int n = acts + p->stash_bought;
    return n < STASH_TABS ? n : STASH_TABS;
}

/* Lo que cuesta la próxima: el triple de la anterior */
static int stash_price(const Player *p)
{
    int64_t price = STASH_PRICE;
    for (int k = 0; k < p->stash_bought; k++)
        price *= 3;
    return price > 2000000000 ? 2000000000 : (int)price;
}

static int stash_can_buy(const Player *p)
{
    return p->stash_bought < STASH_BUY_MAX && stash_tabs(p) < STASH_TABS;
}

/* Objeto de la pestaña que ocupa la casilla (x, y), o -1 */
static int stash_at(const Player *p, int tab, int x, int y)
{
    for (int i = 0; i < p->stash_count[tab]; i++) {
        const ItemType *t = p->stash[tab][i].type;
        if (x >= p->stash_x[tab][i] && x < p->stash_x[tab][i] + t->inv_w && y >= p->stash_y[tab][i] &&
            y < p->stash_y[tab][i] + t->inv_h)
            return i;
    }
    return -1;
}

static int stash_add_at(Player *p, int tab, const Item *it, int x, int y)
{
    const ItemType *t = it->type;
    if (p->stash_count[tab] >= STASH_CELLS || x < 0 || y < 0 || x + t->inv_w > STASH_W || y + t->inv_h > STASH_H)
        return 0;
    for (int a = 0; a < t->inv_h; a++)
        for (int b = 0; b < t->inv_w; b++)
            if (stash_at(p, tab, x + b, y + a) >= 0)
                return 0;
    int n = p->stash_count[tab]++;
    p->stash[tab][n] = *it;
    p->stash_x[tab][n] = (uint8_t)x;
    p->stash_y[tab][n] = (uint8_t)y;
    return 1;
}

/* En una pila igual donde entre entera, o en el primer lugar libre (por
   columnas, como la mochila) */
static int stash_add(Player *p, int tab, const Item *it)
{
    for (int k = 0; k < p->stash_count[tab]; k++)
        if (stack_fits(&p->stash[tab][k], it)) {
            p->stash[tab][k].amount += it->amount > 0 ? it->amount : 1;
            return 1;
        }
    for (int x = 0; x + it->type->inv_w <= STASH_W; x++)
        for (int y = 0; y + it->type->inv_h <= STASH_H; y++)
            if (stash_add_at(p, tab, it, x, y))
                return 1;
    return 0;
}

static void stash_remove(Player *p, int tab, int i)
{
    for (int k = i; k < p->stash_count[tab] - 1; k++) {
        p->stash[tab][k] = p->stash[tab][k + 1];
        p->stash_x[tab][k] = p->stash_x[tab][k + 1];
        p->stash_y[tab][k] = p->stash_y[tab][k + 1];
    }
    p->stash_count[tab]--;
}

/* Ordenar la pestaña, como la mochila */
static void stash_sort(Player *p, int tab)
{
    static Item items[STASH_CELLS];
    int n = p->stash_count[tab];
    memcpy(items, p->stash[tab], sizeof(Item) * (size_t)n);
    sort_items(items, n);
    p->stash_count[tab] = 0;
    for (int i = 0; i < n; i++)
        if (!stash_add(p, tab, &items[i]) && !bag_add(p, &items[i])) /* no debería pasar */
            items_ground_add(&items[i], p->x + 0.8f, p->y + 0.8f);
}

/* El oro: todo al cofre o todo a la mochila */
static void stash_move_gold(Player *p, int to_stash)
{
    int *from = to_stash ? &p->gold : &p->stash_gold, *to = to_stash ? &p->stash_gold : &p->gold;
    if (*from <= 0) {
        show_message(p, to_stash ? "No llevás oro" : "No hay oro en el arcón");
        return;
    }
    int64_t sum = (int64_t)*to + *from;
    int moved = sum > 2000000000 ? 2000000000 - *to : *from;
    *to += moved;
    *from -= moved;
    audio_play_name("item_gold", AUDIO_NOPOS, AUDIO_NOPOS);
}

/* Comprar la próxima pestaña: con el oro que lleva y, si no alcanza, con el
   del cofre (como las compras del original) */
static void stash_buy(Player *p)
{
    int price = stash_price(p);
    if (!stash_can_buy(p))
        return;
    if ((int64_t)p->gold + p->stash_gold < price) {
        char msg[64];
        snprintf(msg, sizeof msg, tr("No te alcanza: cuesta %d"), price);
        show_message(p, msg);
        return;
    }
    int from_bag = p->gold < price ? p->gold : price;
    p->gold -= from_bag;
    p->stash_gold -= price - from_bag;
    p->stash_bought++;
    p->stash_tab = stash_tabs(p) - 1;
    audio_play_name("item_gold", AUDIO_NOPOS, AUDIO_NOPOS);
    char msg[64];
    snprintf(msg, sizeof msg, tr("Pestaña %d del arcón"), p->stash_tab + 1);
    show_message(p, msg);
}

/* Cuánto vale un objeto para comparar con lo que tiene puesto */
static int item_score(const Item *it)
{
    if (!it->type)
        return -1;
    if (it->type->kind == ITEM_WEAPON) {
        int lo, hi;
        items_damage(it, &lo, &hi);
        return lo + hi;
    }
    return items_defense(it);
}

/* Levanta un objeto: las pociones van al cinturón, el equipo se pone solo si
   es mejor que el que tiene (y lo anterior va a la mochila) */
static int pick_up(Player *p, const Item *it)
{
    const ItemType *t = it->type;
    if (strcmp(t->code, "gld") == 0) {
        p->gold += it->amount * (100 + p->bonus[STAT_GOLD_PCT]) / 100;
        return 1;
    }
    if (is_potion(t)) {
        for (int i = 0; i < BELT_SLOTS; i++)
            if (!p->belt[i].type) {
                Item rest = *it, one = potion_take_one(&rest);
                if (rest.amount > 0 && !bag_add(p, &rest))
                    break; /* (una pila que no entra: entera a la mochila o queda en el suelo) */
                p->belt[i] = one;
                return 1;
            }
        return bag_add(p, it);
    }
    if (t->slot != SLOT_NONE && !is_quest_item(t)) { /* el bastón y el amuleto del Báculo, a la mochila (van al cubo) */
        Item *cur = &p->equip[equip_slot_for(p, t)];
        /* Un arma de dos manos no va con escudo (sí con carcaj); el carcaj se
           pone solo si el arma usa esa munición y la mano está libre */
        const ItemType *weapon = p->equip[SLOT_WEAPON].type;
        int blocked = t->slot == SLOT_SHIELD && !items_offhand_ok(weapon, t);
        if (t->ammo)
            blocked = items_ammo_for(weapon) != t->ammo || cur->type;
        if (!meets_item(p, it))
            blocked = 1;
        if (is_quest_item(cur->type))
            blocked = 1; /* la Voluntad de Khalim en la mano (para el Orbe) no se cambia sola por un arma mejor */
        const ItemType *off = p->equip[SLOT_SHIELD].type;
        if (t->slot == SLOT_WEAPON && items_ammo_for(t) && !(off && off->ammo == items_ammo_for(t)))
            blocked = 1; /* un arco o una ballesta sin su carcaj no se pone solo: no podría tirar */
        if (!blocked && (t->ammo || item_score(it) > item_score(cur))) {
            if (cur->type && !bag_add(p, cur))
                return 0;
            *cur = *it;
            if (t->slot == SLOT_WEAPON && !items_offhand_ok(t, p->equip[SLOT_SHIELD].type) &&
                bag_add(p, &p->equip[SLOT_SHIELD]))
                p->equip[SLOT_SHIELD].type = NULL;
            return 1;
        }
    }
    return bag_add(p, it);
}

static int player_str(const Player *p)
{
    return s_classes[p->cls].str + p->stat_add[ATTR_STR] + p->bonus[STAT_STR];
}

static int player_dex(const Player *p)
{
    return s_classes[p->cls].dex + p->stat_add[ATTR_DEX] + p->bonus[STAT_DEX];
}

static int player_vit(const Player *p)
{
    return s_classes[p->cls].vit + p->stat_add[ATTR_VIT] + p->bonus[STAT_VIT];
}

static int player_energy(const Player *p)
{
    return s_classes[p->cls].energy + p->stat_add[ATTR_ENE] + p->bonus[STAT_ENERGY];
}

/* ¿Es de otra clase? (las garras de la Asesina, los cascos del Bárbaro, los
   escudos del Paladín...: no se ven ni se pueden usar con las demás) */
static int other_class(const Player *p, const ItemType *t)
{
    const char *c = items_class(t);
    return t && c[0] && strcmp(c, s_classes[p->cls].code) != 0;
}

/* ¿Le alcanza la fuerza y la destreza para usarlo? (y es de su clase) */
/* La fuerza y la destreza que pide: con el "Requisitos -N%" del objeto (it
   puede ser NULL) y, en las armas, el Dominio de la levitación del Brujo */
static void item_reqs(const Player *p, const ItemType *t, const Item *it, int *str, int *dex)
{
    int pct = it ? items_stat(it, STAT_REQ_PCT) : 0;
    if (p && t && p->war_req_pct > 0 && t->kind == ITEM_WEAPON)
        pct -= p->war_req_pct;
    pct = pct < -100 ? -100 : pct;
    *str = t ? t->req_str * (100 + pct) / 100 : 0;
    *dex = t ? t->req_dex * (100 + pct) / 100 : 0;
}

static int meets_reqs(const Player *p, const ItemType *t)
{
    int rs, rd;
    item_reqs(p, t, NULL, &rs, &rd);
    return !t || (!other_class(p, t) && player_str(p) >= rs && player_dex(p) >= rd);
}

/* Lo mismo con el nivel que pide ese objeto (los únicos, los de conjunto...) */
static int meets_item(const Player *p, const Item *it)
{
    int rs, rd;
    item_reqs(p, it->type, it, &rs, &rd);
    return it->type && !other_class(p, it->type) && player_str(p) >= rs && player_dex(p) >= rd &&
           p->level_num >= items_level_req(it);
}

/* Por qué no se lo puede poner */
static const char *reqs_message(const Player *p, const Item *it)
{
    const ItemType *t = it->type;
    int rs, rd;
    item_reqs(p, t, it, &rs, &rd);
    return other_class(p, t) ? tr("Es de otra clase") : player_str(p) < rs ? tr("Te falta fuerza")
         : player_dex(p) < rd ? tr("Te falta destreza") : tr("Te falta nivel");
}

/* Dónde va al ponérselo: los anillos, en la mano libre (o en la primera) */
static int equip_slot_for(const Player *p, const ItemType *t)
{
    if (t->slot == SLOT_RING1 && p->equip[SLOT_RING1].type && !p->equip[SLOT_RING2].type)
        return SLOT_RING2;
    return t->slot;
}

/* Daño del arma (o los puños) con sus propiedades, más el daño agregado por
   el resto del equipo. El bonus sale de StrBonus y DexBonus del arma: los
   arcos suben con la destreza, las jabalinas con las dos y el resto con la
   fuerza (+1% por punto). `thrown` usa el daño al lanzarla. */
static void weapon_damage_pct(const Player *p, int thrown, int extra_pct, int *dmin, int *dmax);

static void weapon_damage(const Player *p, int thrown, int *dmin, int *dmax)
{
    weapon_damage_pct(p, thrown, 0, dmin, dmax);
}

/* Una propiedad de todo lo que lleva menos las armas que tiene en la mano
   (esas ya la cuentan en su daño) */
static int bonus_off_weapon(const Player *p, int stat)
{
    int v = p->bonus[stat] - items_stat(&p->equip[SLOT_WEAPON], stat);
    if (dual_wield(p))
        v -= items_stat(&p->equip[SLOT_SHIELD], stat);
    return v;
}

/* Lo mismo con un % de daño de la habilidad (Porrazo, Concentración...):
   como en el original, se suma al de la fuerza y los dominios */
static void weapon_damage_pct(const Player *p, int thrown, int extra_pct, int *dmin, int *dmax)
{
    /* Con dos armas, la del golpe en curso (p->hand) */
    int dual = dual_wield(p);
    const Item *w = dual && p->hand ? &p->equip[SLOT_SHIELD] : &p->equip[SLOT_WEAPON];
    int lo = 1, hi = 2, bonus;
    if (w->type) {
        if (thrown)
            items_thrown_damage(w, &lo, &hi);
        else
            items_damage(w, &lo, &hi);
    }
    /* Lo que suma el resto (los anillos, los conjuntos, los talismanes) */
    lo += bonus_off_weapon(p, STAT_DMG_MIN) + bonus_off_weapon(p, STAT_DMG_FLAT);
    hi += bonus_off_weapon(p, STAT_DMG_MAX) + bonus_off_weapon(p, STAT_DMG_FLAT);
    if (hi < lo)
        hi = lo;
    if (w->type && (w->type->str_bonus || w->type->dex_bonus))
        bonus = player_str(p) * w->type->str_bonus / 100 + player_dex(p) * w->type->dex_bonus / 100;
    else
        bonus = player_str(p);
    /* y las auras (Poder, Fanatismo...), el oso y los % de daño de los objetos que no son el arma */
    int pct = 100 + bonus + p->sk_dmg_pct + p->au_dmg_pct + p->dru_dmg_pct + extra_pct + p->bonus[STAT_DMG_PCT_ALL] +
              p->lvl_dmg_pct + bonus_off_weapon(p, STAT_DMG_PCT) -
              (p->curse == A4C_WEAKEN || p->curse == A4C_DECREPIFY ? 50 : 0); /* maldito: Debilitar, Decrepitud */
    if (pct < 1)
        pct = 1;
    *dmin = lo * pct / 100;
    *dmax = hi * pct / 100;
    if (*dmax < *dmin)
        *dmax = *dmin;
}

static void player_damage(const Player *p, int *dmin, int *dmax)
{
    weapon_damage(p, 0, dmin, dmax);
}

/* El daño de fuego, rayo, frío y mágico del equipo (y de las auras) contra
   ese monstruo, cada uno con su resistencia; el frío lo enfría y el veneno
   lo envenena (como el original, en su duración) */
static int spell_resist(int owner, int unit, int elem, int amount);

static int elemental_damage(const Player *p, int unit)
{
    static const int k_elems[4] = { ELEM_FIRE, ELEM_LTNG, ELEM_COLD, ELEM_MAG }; /* las auras: los tres primeros */
    static const int k_min[4] = { STAT_FIRE_MIN, STAT_LTNG_MIN, STAT_COLD_MIN, STAT_MAG_MIN };
    int dmg = 0, owner = (int)(p - s_pl);
    for (int e = 0; e < 4; e++) {
        int lo = p->bonus[k_min[e]] + (e < 3 ? p->au_elem_min[e] : 0);
        int hi = p->bonus[k_min[e] + 1] + (e < 3 ? p->au_elem_max[e] : 0);
        if (hi < lo)
            hi = lo;
        if (hi > 0)
            dmg += spell_resist(owner, unit, k_elems[e], lo + (int)(rnd() % (uint32_t)(hi - lo + 1)));
    }
    if (p->bonus[STAT_COLD_MAX] > 0 && p->bonus[STAT_COLD_LEN] > 0) /* duración en cuadros de 25 por segundo */
        units_chill(unit, p->bonus[STAT_COLD_LEN] * TICK_RATE / 25);
    int plo = p->bonus[STAT_POIS_MIN], phi = p->bonus[STAT_POIS_MAX], plen = p->bonus[STAT_POIS_LEN];
    if (phi > 0 && plen > 0) { /* por cuadro, en 1/256: lo de toda la duración */
        int v = plo + (int)(rnd() % (uint32_t)(phi - plo + 1 > 0 ? phi - plo + 1 : 1));
        units_poison(unit, v * plen / 256, plen * TICK_RATE / 25, owner);
    }
    return dmg;
}

/* Defensa: la suma de la armadura más destreza/4, como el original */
static int player_defense(const Player *p)
{
    if (p->berserk_ticks > 0) /* Furia */
        return 0;
    int def = player_dex(p) / 4 + p->bonus[STAT_AC]; /* la defensa de más de todos los objetos */
    for (int s = 0; s < SLOT_COUNT; s++)
        if (p->equip[s].type && p->equip[s].type->kind == ITEM_ARMOR) /* la de cada armadura, con su % */
            def += p->equip[s].ac * (100 + items_stat(&p->equip[s], STAT_AC_PCT)) / 100;
    /* Piel de hierro, Grito, las armaduras de hielo, Desafío, Escudo sagrado, lo que sube con el nivel, el
       santuario de armadura y la Defense Curse de las súcubos (skill_armor_percent -ln56: 50 + 5 por nivel) */
    int pct = 100 + p->sk_def_pct + p->shout_pct + (p->armor_ticks > 0 ? p->armor_pct : 0) + p->au_def_pct +
              (p->hs_ticks > 0 ? p->hs_def : 0) + p->dru_def_pct + p->lvl_ac_pct +
              (p->shrine_ticks > 0 && p->shrine_kind == 6 ? p->shrine_arg0 : 0) -
              (p->curse == A4C_DEFENSE ? 50 + 5 * (p->curse_lvl - 1) : 0) + p->conv_def;
    return def * (pct > 0 ? pct : 0) / 100;
}

/* Puntería (attack rating) del original: (destreza - 7) * 5 + ToHitFactor */
static int player_attack_rating(const Player *p)
{
    const CharClass *c = &s_classes[p->cls];
    return ((player_dex(p) - 7) * 5 + c->to_hit + p->bonus[STAT_AR]) *
           (100 + p->sk_ar_pct + p->au_ar_pct + p->prg_ar + p->dru_ar_pct + p->bonus[STAT_AR_PCT]) / 100;
}

/* La resistencia (0 fuego, 1 frío, 2 rayo, 3 veneno): la de las
   habilidades, las auras y los objetos, menos la de la dificultad (-40 en
   Pesadilla, -100 en Infierno: ResistPenalty, SUNITDMG), hasta el máximo
   (75 más lo que lo suben, nunca más de 95) y nunca menos de -100 */
static int player_resist(const Player *p, int ri)
{
    static const int k_res[4] = { STAT_RES_FIRE, STAT_RES_COLD, STAT_RES_LTNG, STAT_RES_POIS };
    int r = p->bonus[k_res[ri]] + (ri < 3 ? p->resist[ri] + p->au_res[ri] : 0) + (p->a5_flags & A5F_RESIST ? 10 : 0) +
            s_dl_resist;
    if (p->curse == A4C_LOWERRES) /* Bajar resistencias (dm56: de 25 a 70 %) */
        r -= 25 + (70 - 25) * (110 * p->curse_lvl / (p->curse_lvl + 6)) / 100;
    if (ri < 3) /* la Convicción de un jefe con aura y la de un Anciano colosal */
        r += p->conv_res - p->pierce[ri];
    int most = 75 + p->bonus[k_res[ri] + 1] + (ri < 3 ? p->res_max[ri] + p->au_res_max[ri] : 0);
    most = most > 95 ? 95 : most;
    return r > most ? most : r < s_dl_res_floor ? s_dl_res_floor : r;
}

/* El daño de un elemento que le llega: su resistencia, la reducción de
   daño mágico y lo que absorbe (cura) */
static int player_elem_hit(Player *p, int elem, int amount)
{
    static const int k_abs[3][2] = { { STAT_ABS_FIRE_PCT, STAT_ABS_FIRE },
                                     { STAT_ABS_COLD_PCT, STAT_ABS_COLD },
                                     { STAT_ABS_LTNG_PCT, STAT_ABS_LTNG } };
    int ri = elem == ELEM_FIRE ? 0 : elem == ELEM_COLD ? 1 : elem == ELEM_LTNG ? 2 : elem == ELEM_POIS ? 3 : -1;
    if (amount <= 0)
        return 0;
    if (ri >= 0)
        amount = amount * (100 - player_resist(p, ri)) / 100;
    if (ri != 3)
        amount -= p->bonus[STAT_RED_MAG];
    if (amount <= 0)
        return 0;
    if (ri >= 0 && ri < 3) {
        int pct = p->bonus[k_abs[ri][0]];
        int a = amount * (pct > 40 ? 40 : pct) / 100 + p->bonus[k_abs[ri][1]];
        a = a > amount ? amount : a;
        if (a > 0) {
            amount -= a;
            p->hp = p->hp + a > p->hp_max ? p->hp_max : p->hp + a;
        }
    }
    return amount;
}

static void give_start_items(Player *p)
{
    const CharClass *c = &s_classes[p->cls];
    for (int i = 0; i < 3; i++) {
        const ItemType *t = c->start_items[i] ? items_find(c->start_items[i]) : NULL;
        if (t) {
            Item it = items_make(t);
            pick_up(p, &it);
        }
    }
    const ItemType *tp = items_find("tsc"); /* y un Pergamino de Portal, como en el original */
    if (tp) {
        Item it = items_make(tp);
        bag_add(p, &it);
    }
}

/* Aplica el efecto de una poción (sin sacarla de ningún lado) */
static void apply_potion(Player *p, const ItemType *t)
{
    audio_play(t->use_sound, p->x, p->y);
    if (t->heal_life) {
        p->regen_life = (float)t->heal_life / t->heal_ticks;
        p->regen_life_ticks = t->heal_ticks;
    }
    if (t->heal_mana) {
        p->regen_mana = (float)t->heal_mana / t->heal_ticks;
        p->regen_mana_ticks = t->heal_ticks;
    }
    if (t->heal_pct) { /* rejuvenecimiento: al instante */
        p->hp += p->hp_max * t->heal_pct / 100;
        p->mana += p->mana_max * t->heal_pct / 100;
    }
    if (t->stamina_ticks) {
        p->stamina = p->stamina_max;
        p->exhausted = 0;
        p->stamina_boost_ticks = t->stamina_ticks;
    }
    if (t->antidote_ticks) { /* cura el veneno y lo deja a la mitad un rato */
        p->poison_ticks = 0;
        p->antidote_ticks = t->antidote_ticks;
        show_message(p, "Resistencia al veneno");
    }
    if (t->thaw_ticks) { /* y deja de estar enfriado */
        p->thaw_ticks = t->thaw_ticks;
        p->chill_ticks = 0;
        show_message(p, "Resistencia al frío");
    }
    if (p->hp > p->hp_max) p->hp = p->hp_max;
    if (p->mana > p->mana_max) p->mana = p->mana_max;
}

/* 0 vida, 1 maná, 2 rejuvenecimiento, 3 estamina */
static int potion_kind(const ItemType *t)
{
    return t->heal_pct ? 2 : t->heal_life ? 0 : t->heal_mana ? 1 : 3;
}

/* Una casilla del cinturón quedó vacía: baja una poción de la mochila,
   primero una igual a la que había, si no una de la misma clase (vida,
   maná...) y si no cualquiera */
static void belt_refill(Player *p, int i, const ItemType *like)
{
    int best = -1, best_score = 0;
    for (int k = 0; k < p->bag_count; k++) {
        const ItemType *t = p->bag[k].type;
        if (!t || !is_potion(t))
            continue;
        int score = !like ? 1 : t == like ? 3 : potion_kind(t) == potion_kind(like) ? 2 : 1;
        if (score > best_score) {
            best_score = score;
            best = k;
        }
    }
    if (best >= 0) {
        p->belt[i] = potion_take_one(&p->bag[best]);
        if (p->bag[best].amount <= 0)
            bag_remove(p, best);
    }
}

/* Un trago de una poción (cada una tiene POTION_CHARGES); 1 si se terminó */
static int potion_sip(Player *p, Item *it)
{
    apply_potion(p, it->type);
    if (it->amount <= 0)
        it->amount = POTION_CHARGES;
    return --it->amount <= 0; /* (en una pila, de la de arriba) */
}

/* Toma de la poción de una casilla del cinturón. Cuando se termina, otra
   poción de la mochila baja a ocupar esa casilla (como en el original). */
static void drink_slot(Player *p, int i)
{
    if (i < 0 || i >= BELT_SLOTS || !p->belt[i].type)
        return;
    const ItemType *t = p->belt[i].type;
    if (!potion_sip(p, &p->belt[i]))
        return;
    memset(&p->belt[i], 0, sizeof p->belt[i]);
    belt_refill(p, i, t);
}

static int potion_match(const ItemType *t, int kind)
{
    return kind == 0 ? (t->heal_life || t->heal_pct) : kind == 1 ? (t->heal_mana || t->heal_pct) : t->stamina_ticks > 0;
}

/* Toma la primera poción del cinturón que sirva: vida, maná o estamina. Si
   en el cinturón no hay de esa clase pero en la mochila sí, toma de esa
   (y si hay lugar en el cinturón, la pone ahí) */
static void drink(Player *p, int kind)
{
    for (int i = 0; i < BELT_SLOTS; i++) {
        const ItemType *t = p->belt[i].type;
        if (t && potion_match(t, kind)) {
            drink_slot(p, i);
            return;
        }
    }
    for (int k = 0; k < p->bag_count; k++) {
        const ItemType *t = p->bag[k].type;
        if (!t || !is_potion(t) || !potion_match(t, kind))
            continue;
        for (int i = 0; i < BELT_SLOTS; i++)
            if (!p->belt[i].type) {
                p->belt[i] = potion_take_one(&p->bag[k]);
                if (p->bag[k].amount <= 0)
                    bag_remove(p, k);
                drink_slot(p, i);
                return;
            }
        if (potion_sip(p, &p->bag[k]))
            bag_remove(p, k);
        return;
    }
}

/* ---- Inventario manejado a mano ----
   El cursor recorre en orden: los lugares del equipo, los objetos de la
   mochila y las casillas del cinturón. */

static void bag_remove(Player *p, int i)
{
    if (i >= 0 && i < p->bag_count && p->bag[i].type && p->bag[i].type->charm)
        p->stats_dirty = 1;
    for (int k = i; k < p->bag_count - 1; k++) {
        p->bag[k] = p->bag[k + 1];
        p->bag_x[k] = p->bag_x[k + 1];
        p->bag_y[k] = p->bag_y[k + 1];
    }
    p->bag_count--;
}

/* Saca un objeto de la mochila y se lo pone; lo que tenía va a la mochila */
static void equip_from_bag(Player *p, int i)
{
    Item it = p->bag[i];
    const ItemType *t = it.type;
    if (t->slot == SLOT_NONE)
        return;
    /* El Bárbaro con un arma de una mano y la izquierda libre: la segunda
       arma va a la izquierda (dos armas) */
    if (t->slot == SLOT_WEAPON && offhand_weapon_ok(p, t) && p->equip[SLOT_WEAPON].type &&
        !p->equip[SLOT_SHIELD].type && meets_reqs(p, t)) {
        bag_remove(p, i);
        p->equip[SLOT_SHIELD] = it;
        return;
    }
    if (t->slot == SLOT_SHIELD && !items_offhand_ok(p->equip[SLOT_WEAPON].type, t)) {
        show_message(p, "Con un arma a dos manos no va escudo");
        return;
    }
    if (!meets_item(p, &it)) {
        show_message(p, reqs_message(p, &it));
        player_say(p, "cantuseyet"); /* "Aún no puedo usar esto" */
        return;
    }
    bag_remove(p, i);
    int slot = equip_slot_for(p, t);
    Item old = p->equip[slot];
    p->equip[slot] = it;
    if (old.type && !bag_add(p, &old)) {
        /* No entra: se deshace el cambio */
        p->equip[slot] = old;
        bag_add(p, &it);
        inventory_full(p);
        return;
    }
    if (t->slot == SLOT_WEAPON && !items_offhand_ok(t, p->equip[SLOT_SHIELD].type)) {
        if (bag_add(p, &p->equip[SLOT_SHIELD]))
            p->equip[SLOT_SHIELD].type = NULL;
    }
}

/* Objeto bajo el cursor (NULL si no hay) */
static Item *cursor_item(Player *p)
{
    int sel = p->inv_sel;
    if (sel >= INV_CUBE0 && sel < INV_TRANSMUTE) {
        int k = cube_at(p, (sel - INV_CUBE0) % CUBE_W, (sel - INV_CUBE0) / CUBE_W);
        return k >= 0 ? &p->cube[k] : NULL;
    }
    if (sel >= INV_STASH0 && sel < INV_STAB0) {
        int k = stash_at(p, p->stash_tab, (sel - INV_STASH0) % STASH_W, (sel - INV_STASH0) / STASH_W);
        return k >= 0 ? &p->stash[p->stash_tab][k] : NULL;
    }
    if (sel >= INV_TRANSMUTE)
        return NULL;
    if (sel < SLOT_COUNT)
        return p->equip[sel].type ? &p->equip[sel] : NULL;
    if (sel < INV_BELT0) {
        int k = bag_at(p, (sel - INV_CELL0) % BAG_W, (sel - INV_CELL0) / BAG_W);
        return k >= 0 ? &p->bag[k] : NULL;
    }
    if (sel < INV_SORT)
        return p->belt[sel - INV_BELT0].type ? &p->belt[sel - INV_BELT0] : NULL;
    return NULL;
}

/* X sin nada en la mano: usar lo marcado (ponérselo, sacárselo, tomar la
   poción o pasarla al cinturón) */
/* El botón Transmutar: si lo que hay en el cubo es una receta, se convierte */
static void cube_transmute_now(Player *p)
{
    if (p->cube_count == 0) {
        show_message(p, "El cubo está vacío");
        return;
    }
    if (cow_recipe(p)) /* la pierna de Wirt: el portal a las vacas (no es un objeto) */
        return;
    if (uber_recipe(p)) /* las llaves y los órganos: los portales del evento Pandemonium */
        return;
    Item out[3];
    char what[112];
    int n = cube_transmute(p->cube, p->cube_count, p->level_num, out, 3, what, sizeof what);
    if (n == 0) {
        show_message(p, "Eso no es ninguna receta");
        player_say(p, "impossible_1"); /* "Imposible" o "No puedo" (grupo de 2) */
        return;
    }
    p->cube_count = 0;
    for (int k = 0; k < n; k++)
        if (!cube_add(p, &out[k]) && !bag_add(p, &out[k]))
            items_ground_add(&out[k], p->x + 0.6f, p->y + 0.6f);
    audio_play_name("cursor_convert_item", AUDIO_NOPOS, AUDIO_NOPOS);
    show_message(p, "¡Transmutado!");
    if (getenv("D2_CUBE_DEBUG"))
        printf("  cubo J%d: %s\n", (int)(p - s_pl) + 1, what);
}

/* Abre o cierra el cubo (el cursor va a la primera casilla o vuelve a la mochila) */
static void cube_toggle(Player *p)
{
    p->cube_open = !p->cube_open;
    p->inv_sel = p->cube_open ? INV_CUBE0 : INV_CELL0;
    audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
}

/* Lo que no es una casilla del cofre: las pestañas y los botones del oro */
static void stash_button(Player *p, int sel)
{
    if (sel >= INV_STAB0 && sel < INV_GOLD_IN) {
        int k = sel - INV_STAB0;
        if (k < stash_tabs(p)) { /* otra pestaña (también con algo en la mano) */
            p->stash_tab = k;
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        } else if (!p->stash_confirm) { /* "+": la primera A pregunta */
            p->stash_confirm = 1;
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        } else {
            p->stash_confirm = 0;
            stash_buy(p);
            save_character(p);
        }
    } else if (sel == INV_GOLD_IN || sel == INV_GOLD_OUT) {
        stash_move_gold(p, sel == INV_GOLD_IN);
    }
}

static void inventory_use(Player *p)
{
    int sel = p->inv_sel;
    if (sel >= INV_STAB0) {
        stash_button(p, sel);
        return;
    }
    if (sel >= INV_STASH0) { /* del cofre a la mochila */
        Item *it = cursor_item(p);
        if (it) {
            if (bag_add(p, it))
                stash_remove(p, p->stash_tab, (int)(it - p->stash[p->stash_tab]));
            else
                inventory_full(p);
        }
        return;
    }
    if (p->stash_open && sel >= INV_CELL0 && sel < INV_BELT0) { /* de la mochila al cofre, entero */
        int i = bag_at(p, (sel - INV_CELL0) % BAG_W, (sel - INV_CELL0) / BAG_W);
        if (i < 0)
            return;
        if (stash_add(p, p->stash_tab, &p->bag[i]))
            bag_remove(p, i);
        else
            show_message(p, "No entra en esta pestaña");
        refresh_look(p);
        return;
    }
    if (sel >= INV_CUBE0 && sel < INV_TRANSMUTE) { /* del cubo a la mochila */
        Item *it = cursor_item(p);
        if (it) {
            if (bag_add(p, it))
                cube_remove(p, (int)(it - p->cube));
            else
                inventory_full(p);
        }
        return;
    }
    if (sel == INV_TRANSMUTE) {
        cube_transmute_now(p);
        return;
    }
    if (sel < SLOT_COUNT) {
        Item *it = &p->equip[sel];
        if (it->type) {
            audio_play(it->type->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
            if (bag_add(p, it))
                it->type = NULL;
            else
                inventory_full(p);
        }
    } else if (sel < INV_BELT0) {
        int i = bag_at(p, (sel - INV_CELL0) % BAG_W, (sel - INV_CELL0) / BAG_W);
        if (i < 0)
            return;
        const ItemType *t = p->bag[i].type;
        if (strcmp(t->code, "box") == 0) { /* el cubo: se abre en el lugar del equipo */
            cube_toggle(p);
            return;
        }
        if (a3_use_item(p, i) || a5_use_item(p, i) || endgame_use_item(p, i)) /* la Poción de la vida, el Pergamino de Resistencia, el Token */
            return;
        if (strcmp(t->code, "ass") == 0) { /* el Libro de Habilidad de Radament: un punto, una vez */
            if (!(p->a2_flags & A2F_BOOK)) {
                show_message(p, "Ya aprendiste lo que enseña este libro");
                return;
            }
            p->a2_flags &= ~A2F_BOOK;
            p->skill_points++;
            bag_remove(p, i);
            audio_play_name("object_tome", AUDIO_NOPOS, AUDIO_NOPOS);
            show_message(p, "Ganaste un punto de habilidad");
            save_character(p);
            return;
        }
        if (p->cube_open) { /* con el cubo abierto, X lo pasa al cubo (las pociones, de a una) */
            Item rest = p->bag[i], one = is_potion(t) ? potion_take_one(&rest) : rest;
            if (!cube_add(p, &one))
                show_message(p, "No entra en el cubo");
            else if (is_potion(t) && rest.amount > 0)
                p->bag[i] = rest;
            else
                bag_remove(p, i);
            return;
        }
        if (t->slot != SLOT_NONE) {
            equip_from_bag(p, i);
            audio_play(t->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
        } else if (is_potion(t)) {
            int free_slot = -1;
            for (int k = 0; k < BELT_SLOTS && free_slot < 0; k++)
                if (!p->belt[k].type)
                    free_slot = k;
            if (free_slot >= 0) {
                p->belt[free_slot] = potion_take_one(&p->bag[i]);
                if (p->bag[i].amount <= 0)
                    bag_remove(p, i);
            } else if (potion_sip(p, &p->bag[i])) {
                bag_remove(p, i);
            }
        } else if (strcmp(t->code, "tsc") == 0) {
            if (tp_cast(p, i)) /* el portal se abre al lado: se cierra el inventario */
                close_ui(p);
        } else if (strcmp(t->code, "key") == 0) {
            show_message(p, "Las llaves abren los cofres cerrados");
        }
    } else if (sel < INV_SORT) {
        drink_slot(p, sel - INV_BELT0);
    } else {
        bag_sort(p);
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
    }
    refresh_look(p);
}

/* A: levantar lo marcado (queda "en la mano") */
static void inventory_lift(Player *p)
{
    int sel = p->inv_sel;
    if (sel == INV_SORT) {
        bag_sort(p);
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        return;
    }
    if (sel == INV_TRANSMUTE) {
        cube_transmute_now(p);
        return;
    }
    if (sel == INV_STATS) { /* a repartir los puntos */
        if (p->stash_open)
            return; /* (con el cofre abierto se queda en el inventario) */
        p->inv_page = PAGE_STATS;
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        return;
    }
    if (sel >= INV_STAB0) {
        stash_button(p, sel);
        return;
    }
    Item *it = cursor_item(p);
    if (!it)
        return;
    audio_play_name("item_pickup", AUDIO_NOPOS, AUDIO_NOPOS);
    p->held = *it;
    if (sel >= INV_CELL0 && sel < INV_BELT0)
        bag_remove(p, (int)(it - p->bag));
    else if (sel >= INV_CUBE0 && sel < INV_TRANSMUTE)
        cube_remove(p, (int)(it - p->cube));
    else if (sel >= INV_STASH0 && sel < INV_STAB0)
        stash_remove(p, p->stash_tab, (int)(it - p->stash[p->stash_tab]));
    else
        memset(it, 0, sizeof *it);
    refresh_look(p);
}

/* Soltar lo que tiene en la mano donde está el cursor. Si ahí hay otro
   objeto, se intercambian (el otro queda en la mano). */
static void inventory_place(Player *p)
{
    int sel = p->inv_sel;
    Item held = p->held;
    const ItemType *t = held.type;
    Item *target = cursor_item(p);
    if (target && t->socket_filler && items_can_socket(target, &held)) {
        /* Una gema, runa o joya sobre un objeto con engarces libres: se engarza */
        items_socket(target, &held);
        memset(&p->held, 0, sizeof p->held);
        audio_play_name("item_gem", AUDIO_NOPOS, AUDIO_NOPOS);
        if (target->quality == QUALITY_RUNEWORD) {
            char msg[48];
            snprintf(msg, sizeof msg, "¡%.44s!", target->name);
            show_message(p, msg);
        }
        refresh_look(p);
        return;
    }
    if (sel < SLOT_COUNT) {
        if (!items_slot_ok(t, sel) && !(sel == SLOT_SHIELD && offhand_weapon_ok(p, t))) {
            show_message(p, "Eso no va ahí");
            return;
        }
        if (!meets_item(p, &held)) {
            show_message(p, reqs_message(p, &held));
            player_say(p, "cantuseyet");
            return;
        }
        if (sel == SLOT_SHIELD && !items_offhand_ok(p->equip[SLOT_WEAPON].type, t)) {
            show_message(p, "Con un arma a dos manos no va escudo");
            return;
        }
        if (sel == SLOT_WEAPON && !items_offhand_ok(t, p->equip[SLOT_SHIELD].type)) {
            /* Arma a dos manos: el escudo va a la mochila */
            if (!bag_add(p, &p->equip[SLOT_SHIELD])) {
                show_message(p, "No hay lugar para el escudo");
                return;
            }
            memset(&p->equip[SLOT_SHIELD], 0, sizeof(Item));
        }
        p->held = p->equip[sel];
        p->equip[sel] = held;
    } else if (sel < INV_BELT0) {
        int cx = (sel - INV_CELL0) % BAG_W, cy = (sel - INV_CELL0) / BAG_W;
        if (cx + t->inv_w > BAG_W) cx = BAG_W - t->inv_w;
        if (cy + t->inv_h > BAG_H) cy = BAG_H - t->inv_h;
        int under = -1, many = 0;
        for (int a = 0; a < t->inv_h; a++)
            for (int b = 0; b < t->inv_w; b++) {
                int k = bag_at(p, cx + b, cy + a);
                if (k >= 0 && under >= 0 && k != under)
                    many = 1;
                if (k >= 0)
                    under = k;
            }
        if (many) {
            show_message(p, "No entra ahí");
            return;
        }
        if (under >= 0 && stack_fits(&p->bag[under], &held)) { /* encima de una pila igual: se juntan */
            p->bag[under].amount += held.amount > 0 ? held.amount : 1;
            memset(&p->held, 0, sizeof p->held);
            audio_play(t->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
            return;
        }
        memset(&p->held, 0, sizeof p->held);
        if (under >= 0) {
            p->held = p->bag[under];
            bag_remove(p, under);
        }
        bag_add_at(p, &held, cx, cy);
    } else if (sel < INV_SORT) {
        if (!is_potion(t)) {
            show_message(p, "En el cinturón van pociones");
            return;
        }
        Item *slot = &p->belt[sel - INV_BELT0];
        if (stack_count(&held) > 1) { /* de una pila, una sola */
            if (slot->type) {
                show_message(p, "Ese lugar del cinturón está ocupado");
                return;
            }
            *slot = potion_take_one(&p->held);
        } else {
            p->held = *slot;
            *slot = held;
        }
    } else if (sel >= INV_CUBE0 && sel < INV_TRANSMUTE) {
        int cx = (sel - INV_CUBE0) % CUBE_W, cy = (sel - INV_CUBE0) / CUBE_W;
        if (cx + t->inv_w > CUBE_W) cx = CUBE_W - t->inv_w;
        if (cy + t->inv_h > CUBE_H) cy = CUBE_H - t->inv_h;
        if (cx < 0 || cy < 0 || strcmp(t->code, "box") == 0) {
            show_message(p, "No entra en el cubo");
            return;
        }
        int under = -1, many = 0;
        for (int a = 0; a < t->inv_h; a++)
            for (int b = 0; b < t->inv_w; b++) {
                int k = cube_at(p, cx + b, cy + a);
                if (k >= 0 && under >= 0 && k != under)
                    many = 1;
                if (k >= 0)
                    under = k;
            }
        if (many) {
            show_message(p, "No entra ahí");
            return;
        }
        if (is_potion(t) && stack_count(&held) > 1) { /* al cubo, de a una */
            if (under >= 0) {
                show_message(p, "Ahí ya hay algo");
                return;
            }
            Item one = potion_take_one(&p->held);
            cube_add_at(p, &one, cx, cy);
            audio_play(t->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
            return;
        }
        memset(&p->held, 0, sizeof p->held);
        if (under >= 0) {
            p->held = p->cube[under];
            cube_remove(p, under);
        }
        cube_add_at(p, &held, cx, cy);
    } else if (sel >= INV_STASH0 && sel < INV_STAB0) { /* en el cofre: como en la mochila */
        int tab = p->stash_tab, cx = (sel - INV_STASH0) % STASH_W, cy = (sel - INV_STASH0) / STASH_W;
        if (cx + t->inv_w > STASH_W) cx = STASH_W - t->inv_w;
        if (cy + t->inv_h > STASH_H) cy = STASH_H - t->inv_h;
        int under = -1, many = 0;
        for (int a = 0; a < t->inv_h; a++)
            for (int b = 0; b < t->inv_w; b++) {
                int k = stash_at(p, tab, cx + b, cy + a);
                if (k >= 0 && under >= 0 && k != under)
                    many = 1;
                if (k >= 0)
                    under = k;
            }
        if (many) {
            show_message(p, "No entra ahí");
            return;
        }
        if (under >= 0 && stack_fits(&p->stash[tab][under], &held)) {
            p->stash[tab][under].amount += held.amount > 0 ? held.amount : 1;
            memset(&p->held, 0, sizeof p->held);
            audio_play(t->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
            return;
        }
        memset(&p->held, 0, sizeof p->held);
        if (under >= 0) {
            p->held = p->stash[tab][under];
            stash_remove(p, tab, under);
        }
        stash_add_at(p, tab, &held, cx, cy);
    } else if (sel >= INV_STAB0) { /* una pestaña: se ve esa, con lo de la mano */
        stash_button(p, sel);
        return;
    } else {
        show_message(p, "Soltalo en la mochila");
        return;
    }
    audio_play(t->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
    refresh_look(p);
}

/* Lo que tiene en la mano vuelve a la mochila (o al suelo si no entra) */
static void inventory_stow(Player *p)
{
    if (!p->held.type)
        return;
    if (!bag_add(p, &p->held))
        items_ground_add(&p->held, p->x + 0.8f, p->y + 0.8f);
    memset(&p->held, 0, sizeof p->held);
}

/* Tira al suelo, a sus pies, lo que tiene en la mano o (si no) lo elegido */
static void inventory_drop(Player *p)
{
    if (!p->held.type)
        inventory_lift(p);
    if (!p->held.type)
        return;
    items_ground_add(&p->held, p->x + 0.8f, p->y + 0.8f);
    memset(&p->held, 0, sizeof p->held);
}

/* El objeto del lugar `sel` (para que el cursor salte de uno a otro): el de
   la mochila o 1000 + el del cofre; -1 = ninguno */
static int inv_item_at(const Player *p, int sel)
{
    if (sel >= INV_CELL0 && sel < INV_BELT0)
        return bag_at(p, (sel - INV_CELL0) % BAG_W, (sel - INV_CELL0) / BAG_W);
    if (sel >= INV_STASH0 && sel < INV_STAB0 && p->stash_open) {
        int k = stash_at(p, p->stash_tab, (sel - INV_STASH0) % STASH_W, (sel - INV_STASH0) / STASH_W);
        return k >= 0 ? 1000 + k : -1;
    }
    return -1;
}

/* A en el cofre del pueblo: el inventario con el cofre en el lugar del equipo */
static void stash_show(Player *p)
{
    p->inv_open = 1;
    p->inv_page = PAGE_INVENTORY;
    p->assigning = 0;
    p->cube_open = 0;
    p->stash_open = 1;
    p->stash_confirm = 0;
    if (p->stash_tab < 0 || p->stash_tab >= stash_tabs(p))
        p->stash_tab = 0;
    p->inv_sel = INV_STASH0;
    audio_play_name("object_chest_large", AUDIO_NOPOS, AUDIO_NOPOS);
}

/* B: cierra el inventario; con el cofre, también el cofre (se abrió en el
   pueblo) y se guarda la partida */
static void inventory_close(Player *p)
{
    if (p->stash_open) {
        p->stash_open = 0;
        p->stash_confirm = 0;
        p->inv_sel = INV_CELL0;
        save_character(p);
    }
    p->inv_open = 0;
}

static void inventory_tick(Player *p, const PlayerInput *pi)
{
    int holding = p->held.type != NULL;
    if (pi->automap) { /* RB: al suelo (lo de la mano o lo elegido) */
        inventory_drop(p);
        return;
    }
    if (pi->button[3]) { /* Y: con algo en la mano lo tira; si no, siguiente panel (no con el cofre) */
        if (holding)
            inventory_drop(p);
        else if (!p->stash_open)
            p->inv_page = PAGE_SKILLS;
        return;
    }
    if (p->stash_tab < 0 || p->stash_tab >= stash_tabs(p))
        p->stash_tab = 0;
    /* Sin el cubo en la mochila (lo tiró o lo vendió) no hay nada que mostrar */
    if (p->cube_open && bag_index_of(p, "box") < 0 && !(holding && strcmp(p->held.type->code, "box") == 0)) {
        p->cube_open = 0;
        p->inv_sel = INV_CELL0;
    }
    if (pi->back) { /* B: con algo en la mano lo guarda; si no, cierra el cubo, el cofre o el inventario */
        if (holding)
            inventory_stow(p);
        else if (p->cube_open)
            cube_toggle(p);
        else
            inventory_close(p);
        return;
    }
    if (pi->potion_life) { /* LB: ordenar (la pestaña del cofre si el cursor está ahí) */
        if (p->inv_sel >= INV_STASH0 && p->inv_sel < INV_STAB0)
            stash_sort(p, p->stash_tab);
        else
            bag_sort(p);
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        return;
    }
    int dx = pi->nav_right - pi->nav_left, dy = pi->nav_down - pi->nav_up;
    if (p->inv_sel < 0 || p->inv_sel >= INV_ENTRIES)
        p->inv_sel = 0;
    if (dx || dy) {
        int from = p->inv_sel, next = nav_nearest(INV_ENTRIES, from, dx, dy, inv_rect_cb, p);
        /* Sin nada en la mano el cursor salta de objeto en objeto */
        int item = inv_item_at(p, from);
        while (!holding && item >= 0 && inv_item_at(p, next) == item) {
            int after = nav_nearest(INV_ENTRIES, next, dx, dy, inv_rect_cb, p);
            if (after == next)
                break;
            next = after;
        }
        p->inv_sel = next;
        p->stash_confirm = 0;
    }
    if (holding && (pi->action || pi->drop))
        inventory_place(p);
    else if (!holding && pi->action)
        inventory_lift(p);
    else if (!holding && pi->drop)
        inventory_use(p);
}

/* ---- Árbol de habilidades ----
   Las tres solapas van una al lado de la otra: a la derecha de la última
   columna de una solapa sigue la primera de la siguiente. */

/* Además de las 30 habilidades de la clase, el árbol tiene una cuarta
   solapa "Generales" con el ataque normal y Lanzar, que también se pueden
   poner en cualquier botón */
#define GENERIC_PAGE  4
#define ICON_THROW    6 /* skillicon.dc6, según skilldesc.txt */

static int tree_count(int cls)
{
    return skills_count(cls) + 2;
}

/* Qué va en el botón si se elige esa entrada */
static int tree_button_value(int cls, int i)
{
    int n = skills_count(cls);
    return i < n ? i : i == n ? BTN_ATTACK : BTN_THROW;
}

/* ¿Se puede aprender (o subir) ahora? Si no, deja el motivo en `why` */
static int can_learn(const Player *p, int skill, char *why, size_t cap)
{
    if (skill >= skills_count(p->cls)) {
        snprintf(why, cap, "%s", tr("No hace falta aprenderla"));
        return 0;
    }
    const SkillDef *s = skills_get(p->cls, skill);
    if (s->kind == SK_NONE)
        snprintf(why, cap, "%s", tr("Todavía no está en este port"));
    else if (p->skill_lvl[skill] >= SKILL_MAX_LEVEL)
        snprintf(why, cap, "%s", tr("Ya está al máximo"));
    else if (p->level_num < s->reqlevel)
        snprintf(why, cap, tr("Requiere nivel %d"), s->reqlevel);
    else {
        for (int k = 0; k < 3; k++)
            if (s->req[k] >= 0 && p->skill_lvl[s->req[k]] <= 0) {
                snprintf(why, cap, tr("Requiere %s"), skills_get(p->cls, s->req[k])->name);
                return 0;
            }
        return 1;
    }
    return 0;
}

/* Pone la habilidad en el primer botón libre (X, Y, B) */
static void auto_assign(Player *p, int skill)
{
    static const int order[3] = { 2, 3, 1 };
    for (int b = 0; b < NUM_BUTTONS; b++)
        if (p->buttons[b] == skill)
            return;
    for (int k = 0; k < 3; k++)
        if (p->buttons[order[k]] == BTN_EMPTY) {
            p->buttons[order[k]] = skill;
            return;
        }
}

static void learn_skill(Player *p, int skill)
{
    char why[48];
    if (!can_learn(p, skill, why, sizeof why)) {
        show_message(p, why);
        return;
    }
    if (p->skill_points <= 0) {
        show_message(p, "No te quedan puntos de habilidad");
        return;
    }
    p->skill_lvl[skill]++;
    p->skill_points--;
    audio_play_name("cursor_point_drop", AUDIO_NOPOS, AUDIO_NOPOS);
    if (p->skill_lvl[skill] == 1 && !skills_get(p->cls, skill)->passive)
        auto_assign(p, skill);
    refresh_stats(p);
    save_character(p);
}

/* La misma habilidad en el mismo botón lo vacía (A vuelve al ataque normal) */
static void assign_button(Player *p, int b, int skill)
{
    audio_play_name("cursor_select", AUDIO_NOPOS, AUDIO_NOPOS);
    if (p->buttons[b] == skill)
        p->buttons[b] = b == 0 ? BTN_ATTACK : BTN_EMPTY;
    else
        p->buttons[b] = skill;
}

static void tree_tick(Player *p, const PlayerInput *pi)
{
    int n = skills_count(p->cls);
    if (n == 0) {
        p->inv_page = 0;
        return;
    }
    if (p->tree_sel < 0 || p->tree_sel >= tree_count(p->cls))
        p->tree_sel = 0;
    if (p->assigning) {
        /* El recuadro "Poner en un botón": la cruceta elige, A pone, B cancela */
        if (pi->nav_up)
            p->assign_sel = (p->assign_sel + NUM_BUTTONS - 1) % NUM_BUTTONS;
        if (pi->nav_down)
            p->assign_sel = (p->assign_sel + 1) % NUM_BUTTONS;
        if (pi->action) {
            assign_button(p, p->assign_sel, tree_button_value(p->cls, p->tree_sel));
            p->assigning = 0;
        } else if (pi->back) {
            p->assigning = 0;
        }
        return;
    }
    if (pi->button[3]) { /* Y: siguiente panel */
        p->inv_page = PAGE_BUTTONS;
        p->btn_pick = 0;
        return;
    }
    if (pi->back) { /* B: cerrar */
        p->inv_open = 0;
        return;
    }
    int dx = pi->nav_right - pi->nav_left, dy = pi->nav_down - pi->nav_up;
    if (p->tree_page < 1 || p->tree_page > 3)
        p->tree_page = 1;
    if (dx || dy) {
        TreeCtx ctx = { p, p->tree_page };
        int next = nav_nearest(tree_count(p->cls), p->tree_sel, dx, dy, tree_rect_cb, &ctx);
        if (next == p->tree_sel && dx) {
            /* Pasado el borde: otra solapa (la de abajo es la primera página) */
            p->tree_page = (p->tree_page + 2 + (dx > 0 ? 1 : -1)) % 3 + 1;
            int best = -1, row = p->tree_sel < n ? skills_get(p->cls, p->tree_sel)->row : 1;
            for (int i = 0; i < n; i++) {
                const SkillDef *s = skills_get(p->cls, i);
                if (s->page == p->tree_page &&
                    (best < 0 || abs(s->row - row) < abs(skills_get(p->cls, best)->row - row)))
                    best = i;
            }
            next = best >= 0 ? best : p->tree_sel;
        }
        p->tree_sel = next;
        if (p->tree_sel < n)
            p->tree_page = skills_get(p->cls, p->tree_sel)->page;
    }
    if (pi->action)
        learn_skill(p, p->tree_sel);
    if (pi->drop && p->tree_sel >= n) { /* Ataque y Lanzar: siempre se pueden poner */
        p->assigning = 1;
        p->assign_sel = 0;
    } else if (pi->drop) { /* X: asignarla a un botón */
        const SkillDef *s = skills_get(p->cls, p->tree_sel);
        if (p->skill_lvl[p->tree_sel] <= 0)
            show_message(p, "Primero tenés que aprenderla");
        else if (s->passive)
            show_message(p, "Las pasivas no van en un botón");
        else {
            p->assigning = 1;
            p->assign_sel = 0;
        }
    }
}

/* ---- Página Botones ----
   Los cuatro botones con lo que tiene cada uno; A en uno muestra lo que se
   le puede poner (el ataque, lanzar y las habilidades aprendidas que no son
   pasivas) y A pone la elegida. */

static const char *button_skill_name(const Player *p, int what)
{
    return what == BTN_ATTACK ? tr("Ataque") : what == BTN_THROW ? "Lanzar" : what >= 0 ? skills_get(p->cls, what)->name : tr("(vacío)");
}

static int button_candidates(const Player *p, int *out, int max)
{
    int n = 0;
    out[n++] = BTN_ATTACK;
    out[n++] = BTN_THROW;
    for (int i = 0; i < skills_count(p->cls) && n < max; i++) {
        const SkillDef *s = skills_get(p->cls, i);
        if (p->skill_lvl[i] > 0 && !s->passive && s->kind != SK_NONE)
            out[n++] = i;
    }
    return n;
}

static void buttons_tick(Player *p, const PlayerInput *pi)
{
    int cand[64], n = button_candidates(p, cand, 64);
    if (p->btn_sel < 0 || p->btn_sel >= NUM_BUTTONS)
        p->btn_sel = 0;
    if (p->btn_pick) { /* eligiendo qué poner en el botón */
        if (pi->back) {
            p->btn_pick = 0;
            return;
        }
        if (pi->nav_up)
            p->btn_list_sel = (p->btn_list_sel + n - 1) % n;
        if (pi->nav_down)
            p->btn_list_sel = (p->btn_list_sel + 1) % n;
        if (pi->action) {
            p->buttons[p->btn_sel] = cand[p->btn_list_sel < n ? p->btn_list_sel : 0];
            p->btn_pick = 0;
            audio_play_name("cursor_select", AUDIO_NOPOS, AUDIO_NOPOS);
        }
        return;
    }
    if (pi->button[3]) { /* Y: siguiente panel */
        p->inv_page = PAGE_STATS;
        return;
    }
    if (pi->back) {
        p->inv_open = 0;
        return;
    }
    if (pi->nav_up)
        p->btn_sel = (p->btn_sel + NUM_BUTTONS - 1) % NUM_BUTTONS;
    if (pi->nav_down)
        p->btn_sel = (p->btn_sel + 1) % NUM_BUTTONS;
    if (pi->action) {
        p->btn_pick = 1;
        p->btn_list_sel = 0;
        for (int k = 0; k < n; k++)
            if (cand[k] == p->buttons[p->btn_sel])
                p->btn_list_sel = k;
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
    }
}

/* Datos de un objeto, para mostrar abajo del inventario o de la tienda (el
   nombre va aparte, con su color) */
static void item_info_text(const Item *it, char *buf, size_t cap)
{
    const ItemType *t = it->type;
    if (!t) {
        snprintf(buf, cap, "%s", tr("(vacío)"));
    } else if (t->kind == ITEM_WEAPON && items_can_throw(t)) {
        int lo, hi, tlo, thi;
        items_damage(it, &lo, &hi);
        items_thrown_damage(it, &tlo, &thi);
        snprintf(buf, cap, tr("Daño %d-%d, al lanzarla %d-%d, quedan %d"), lo, hi, tlo, thi, it->amount);
    } else if (t->kind == ITEM_WEAPON) {
        int lo, hi;
        items_damage(it, &lo, &hi);
        snprintf(buf, cap, tr("Daño %d-%d%s, fuerza %d"), lo, hi, t->two_handed ? tr(" (dos manos)") : "", t->req_str);
    } else if (t->ammo) {
        snprintf(buf, cap, tr("Cantidad: %d (para %s)"), it->amount, t->ammo == 1 ? "arcos" : "ballestas");
    } else if (t->kind == ITEM_ARMOR) {
        snprintf(buf, cap, tr("Defensa %d, fuerza %d"), items_defense(it), t->req_str);
    } else if (t->heal_life) {
        snprintf(buf, cap, tr("Cura %d de vida"), t->heal_life);
    } else if (t->heal_mana) {
        snprintf(buf, cap, tr("Recupera %d de maná"), t->heal_mana);
    } else if (t->heal_pct) {
        snprintf(buf, cap, tr("Recupera %d%% de vida y maná"), t->heal_pct);
    } else if (t->stamina_ticks) {
        snprintf(buf, cap, "%s", tr("Recupera la estamina"));
    } else if (strcmp(t->code, "key") == 0) {
        snprintf(buf, cap, tr("Abren los cofres cerrados (%d)"), it->amount);
    } else if (strcmp(t->code, "tsc") == 0) {
        if (stack_count(it) > 1)
            snprintf(buf, cap, tr("Abren un portal al pueblo (%d)"), stack_count(it));
        else
            snprintf(buf, cap, "%s", tr("Abre un portal al pueblo"));
    } else {
        buf[0] = '\0';
    }
}

/* ---- NPCs del pueblo: diálogos y tiendas ---- */

#define SHOP_MAX 60
#define SHOP_MAGIC_PCT   20 /* armas y armaduras que salen mágicas en las tiendas */
/* El nivel más alto de lo que venden en Normal en cada acto (npcLevels de
   D2GAME_NPC_FillStoreInventory: 12, 20, 28...) y el de sus afijos mágicos */
static const int k_shop_max_level[5] = { 12, 20, 28, 36, 44 };
static const int k_shop_affix_level[5] = { 6, 15, 24, 33, 42 }; /* shortcut: el original usa el nivel del personaje + 5 con ese tope (el Acto 5 sigue la serie) */

typedef struct {
    const char *npc; /* Id de monstats.txt */
    int         vendor;
    int         act;
    Item        stock[SHOP_MAX];
    int         count;
} Shop;

static Shop s_shops[VENDOR_COUNT] = {
    { "akara", VENDOR_AKARA, 1, { { 0 } }, 0 },
    { "charsi", VENDOR_CHARSI, 1, { { 0 } }, 0 },
    { "gheed", VENDOR_GHEED, 1, { { 0 } }, 0 },
    { "fara", VENDOR_FARA, 2, { { 0 } }, 0 },
    { "lysander", VENDOR_LYSANDER, 2, { { 0 } }, 0 },
    { "drognan", VENDOR_DROGNAN, 2, { { 0 } }, 0 },
    { "elzix", VENDOR_ELZIX, 2, { { 0 } }, 0 },
    { "ormus", VENDOR_ORMUS, 3, { { 0 } }, 0 },
    { "alkor", VENDOR_ALKOR, 3, { { 0 } }, 0 },
    { "asheara", VENDOR_ASHEARA, 3, { { 0 } }, 0 },
    { "hratli", VENDOR_HRATLI, 3, { { 0 } }, 0 },
    { "halbu", VENDOR_HALBU, 4, { { 0 } }, 0 },
    { "jamella", VENDOR_JAMELLA, 4, { { 0 } }, 0 },
    { "malah", VENDOR_MALAH, 5, { { 0 } }, 0 },
    { "larzuk", VENDOR_LARZUK, 5, { { 0 } }, 0 },
    { "drehya", VENDOR_ANYA, 5, { { 0 } }, 0 }, /* (y Nihlathak, hasta que se va: shop_of) */
};

enum { OPT_TALK, OPT_TRADE, OPT_HIRE, OPT_REVIVE, OPT_IMBUE, OPT_TRAVEL, OPT_GAMBLE, OPT_LEAVE, OPT_SOCKET, OPT_PERSONAL,
       OPT_RESPEC };

/* D2GAME_NPC_GenerateStoreItem: en Pesadilla e Infierno, con el personaje
   de nivel 26 o más, en lugar de lo de la tabla la versión excepcional o de
   élite (más probable cuanto más nivel) o la de NightmareUpgrade / HellUpgrade
   (las pociones grandes) */
static const ItemType *shop_upgrade(const ItemType *t, int ilvl, int clvl)
{
    if (s_diff == 0 || clvl <= 25)
        return t;
    const ItemType *u = NULL;
    if (s_diff == 1) {
        if ((int)(rnd() % 100000) < (ilvl << 6) + 4000 && t->ubercode[0])
            u = items_find(t->ubercode);
        else if (t->nm_upgrade[0])
            u = items_find(t->nm_upgrade);
    } else {
        int r = (int)(rnd() % 100000);
        if (r < 16 * ilvl + 1000 && t->ultracode[0])
            u = items_find(t->ultracode);
        else if (r < (ilvl << 7) + 5000 && t->ubercode[0])
            u = items_find(t->ubercode);
        if (t->hell_upgrade[0])
            u = items_find(t->hell_upgrade);
    }
    return u && items_supported(u) ? u : t;
}

/* Mercadería de cada vendedor según sus columnas (AkaraMin/Max, FaraMin/Max...)
   de las tablas de objetos, hasta el nivel de su acto. En Pesadilla e
   Infierno sin ese tope: el nivel del personaje + 5 (el más alto de los dos;
   D2GAME_NPC_FillStoreInventory) */
static void generate_shops(void)
{
    int clvl = 1;
    for (int i = 0; i < s_num_players; i++)
        clvl = s_pl[i].level_num > clvl ? s_pl[i].level_num : clvl;
    for (int v = 0; v < VENDOR_COUNT; v++) {
        Shop *sh = &s_shops[v];
        int max_level = k_shop_max_level[sh->act - 1], affix = k_shop_affix_level[sh->act - 1];
        if (s_diff > 0)
            max_level = affix = clvl + 5 < 99 ? clvl + 5 : 99;
        sh->count = 0;
        for (int i = 0; i < items_type_count() && sh->count < SHOP_MAX; i++) {
            const ItemType *t = items_type(i);
            if (!items_supported(t) || t->level > max_level || t->vendor_max[sh->vendor] <= 0)
                continue;
            int n = t->vendor_min[sh->vendor] +
                    (int)(rnd() % (uint32_t)(t->vendor_max[sh->vendor] - t->vendor_min[sh->vendor] + 1));
            if (n < 1)
                n = 1;
            for (int k = 0; k < n && sh->count < SHOP_MAX; k++) {
                Item it = items_make(shop_upgrade(t, max_level, clvl));
                if (rnd() % 100 < SHOP_MAGIC_PCT)
                    items_make_magic(&it, affix);
                items_add_automagic(&it, affix); /* los objetos de clase (automagic.txt) */
                sh->stock[sh->count++] = it;
            }
        }
    }
}

static int shop_unlimited(const ItemType *t)
{
    return is_potion(t) || strcmp(t->code, "key") == 0 || strcmp(t->code, "tsc") == 0;
}

static Shop *shop_of(const char *npc)
{
    if (strcmp(npc, "nihlathak") == 0) /* en Harrogath vende lo de Anya hasta que ella vuelve */
        npc = "drehya";
    for (int v = 0; v < VENDOR_COUNT; v++)
        if (strcmp(s_shops[v].npc, npc) == 0)
            return &s_shops[v];
    return NULL;
}

/* Opciones del menú de cada NPC: Kashya contrata mercenarias y revive la
   que murió */
static int npc_options(const Player *p, const char *npc, int *opts)
{
    int n = 0;
    if (strcmp(npc, "halbu") != 0 && strcmp(npc, "jamella") != 0) /* (los del Acto 4 no tienen nada que contar) */
        opts[n++] = OPT_TALK;
    if (shop_of(npc))
        opts[n++] = OPT_TRADE;
    if (strcmp(npc, "gheed") == 0 || strcmp(npc, "elzix") == 0 || strcmp(npc, "alkor") == 0 ||
        strcmp(npc, "jamella") == 0 || strcmp(npc, "nihlathak") == 0 || strcmp(npc, "drehya") == 0) /* los que apuestan */
        opts[n++] = OPT_GAMBLE;
    if (strcmp(npc, "tyrael2") == 0 && p->merc_name && p->merc_dead) /* en el Acto 4 no se contrata: Tyrael solo resucita */
        opts[n++] = OPT_REVIVE;
    if (strcmp(npc, "kashya") == 0 || strcmp(npc, "greiz") == 0 || strcmp(npc, "asheara") == 0 ||
        strcmp(npc, "qual-kehk") == 0) {
        opts[n++] = OPT_HIRE;
        if (p->merc_name && p->merc_dead)
            opts[n++] = OPT_REVIVE;
    }
    if (strcmp(npc, "charsi") == 0 && p->malus_q == 3) /* la recompensa del Mazo */
        opts[n++] = OPT_IMBUE;
    if (strcmp(npc, "larzuk") == 0 && p->shenk_q == 3) /* la de Shenk: un engarce */
        opts[n++] = OPT_SOCKET;
    if (strcmp(npc, "drehya") == 0 && p->nihl_q == 4) /* la de Nihlathak: personalizar un objeto */
        opts[n++] = OPT_PERSONAL;
    if (strcmp(npc, "akara") == 0 && p->den_rewarded && !p->respec_done) /* después de la Den of Evil: una vez por dificultad */
        opts[n++] = OPT_RESPEC;
    if (strncmp(npc, "warriv", 6) == 0 && p->andy_q >= 2) /* después de Andariel */
        opts[n++] = OPT_TRAVEL;
    if (strcmp(npc, "meshif1") == 0 && p->tombs_q >= 6) /* después de Duriel: el barco a Kurast */
        opts[n++] = OPT_TRAVEL;
    if (strcmp(npc, "meshif2") == 0) /* en Kurast: de vuelta a Lut Gholein */
        opts[n++] = OPT_TRAVEL;
    opts[n++] = OPT_LEAVE;
    return n;
}

static const char *const k_option_names[] = { "Hablar", "Comerciar", "Contratar", "Resucitar", "Imbuir",
                                              "Viajar al este", "Apostar", "Salir", "Engarzar", "Personalizar",
                                              "Restablecer puntos" };

/* ---- Mercenarias ---- */

typedef struct {
    int  level, gold, hp, hp_lvl, def, def_lvl, ar, ar_lvl, dmin, dmax, dmg_lvl8;
    int  str, str_lvl8, dex, dex_lvl8, res, res_lvl4;
    struct { /* Skill1 y Skill2: Percepción y su flecha; la Estocada y su aura */
        char name[24];
        int  chance, chance_lvl4, lvl, lvl_lvl32;
    } sk[2];
} HireRow;

/* Lo de cada tipo: de qué acto es, lo que dice la lista de Kashya o Greiz
   (HireDesc) y sus nombres (NameFirst..NameLast: "merc01".."merc41") */
static struct {
    int  act;
    int  diff;    /* Difficulty: los que se contratan en esa dificultad (1 Normal, 2 Pesadilla, 3 Infierno) */
    int  cls;     /* Class: el número de monstats.txt (los bárbaros: 561 el de dos manos, 560 el de una) */
    char desc[24];
    char names[16]; /* sin los dos números */
    int  num_names;
} s_hire_type[MERC_TYPES];

/* Lo de cada acto: el monstruo de monstats.txt, su retrato y su voz (el
   guerrero del desierto no avisa que está por morir) */
static const struct {
    const char *monster, *icon, *hired, *thanks, *danger, *title;
} k_merc_act[4] = {
    { "roguehire", "hireables/rogueicon", "rogue_confirm_a_1", "hireable_female_thank_you", "rogue_mortal_danger",
      "Mercenaria" },
    { "act2hire", "hireables/act2hireableicon", "hireable_male_thanks_1", "hireable_male_thanks_1", NULL, "Mercenario" },
    { "act3hire", "hireables/act3hireableicon", "ironwolf_command_1", "hireable_male_thanks_1", NULL, "Mercenario" },
    { "act5hire2", "hireables/barbhirable_icon", "hireable_male_thanks_1", "hireable_male_thanks_1", NULL, "Mercenario" },
};
#define MERC_ACT(type)                                                                                                  \
    k_merc_act[s_hire_type[type].act == 5 ? 3 : s_hire_type[type].act >= 2 && s_hire_type[type].act <= 3 ? s_hire_type[type].act - 1 : 0]

static const char *merc_monster(int type)
{
    if (s_hire_type[type].act == 5 && s_hire_type[type].cls == 560) /* el de una mano (Frenesí) */
        return "act5hire1";
    return MERC_ACT(type).monster;
}

static HireRow s_hire[MERC_TYPES][MERC_ROWS];
static int     s_hire_rows[MERC_TYPES];
static int     s_res_mult = 15, s_res_div = 2, s_res_max = 50000;

/* Las filas de la expansión (Version 100) de los Actos 1 y 2 en Normal: una
   por tramo de nivel, con lo que suma cada nivel (el daño en octavos, la
   probabilidad de las habilidades en cuartos y su nivel en 1/32) */
static void load_hirelings(void)
{
    size_t n;
    uint8_t *d = fs_read("data/global/excel/hireling.txt", &n);
    TxtTable t = { 0 };
    if (!d || txt_load(d, n, &t) != 0) {
        free(d);
        fprintf(stderr, "falta hireling.txt: Kashya no va a tener mercenarias\n");
        return;
    }
    free(d);
    for (int r = 1; r < t.rows; r++) {
        int id = txt_get_int(&t, r, "Id", -1), act = txt_get_int(&t, r, "Act", 0);
        int diff = txt_get_int(&t, r, "Difficulty", 0);
        if (txt_get_int(&t, r, "Version", 0) != 100 || act < 1 || act > 5 || diff < 1 || diff > 3 || id < 0 ||
            id >= MERC_TYPES || s_hire_rows[id] >= MERC_ROWS)
            continue;
        s_hire_type[id].diff = diff;
        const char *first = txt_get(&t, r, txt_col(&t, "NameFirst")), *last = txt_get(&t, r, txt_col(&t, "NameLast"));
        size_t len = strlen(first);
        s_hire_type[id].act = act;
        s_hire_type[id].cls = txt_get_int(&t, r, "Class", 0);
        snprintf(s_hire_type[id].desc, sizeof s_hire_type[id].desc, "%s", txt_get(&t, r, txt_col(&t, "HireDesc")));
        snprintf(s_hire_type[id].names, sizeof s_hire_type[id].names, "%.*s", len > 2 ? (int)len - 2 : 0, first);
        s_hire_type[id].num_names = strlen(last) > 2 ? atoi(last + strlen(last) - 2) : MERC_NAMES;
        HireRow *h = &s_hire[id][s_hire_rows[id]++];
        h->level = txt_get_int(&t, r, "Level", 1);
        h->gold = txt_get_int(&t, r, "Gold", 100);
        h->hp = txt_get_int(&t, r, "HP", 40);
        h->hp_lvl = txt_get_int(&t, r, "HP/Lvl", 8);
        h->def = txt_get_int(&t, r, "Defense", 10);
        h->def_lvl = txt_get_int(&t, r, "Def/Lvl", 5);
        h->ar = txt_get_int(&t, r, "AR", 10);
        h->ar_lvl = txt_get_int(&t, r, "AR/Lvl", 10);
        h->dmin = txt_get_int(&t, r, "Dmg-Min", 1);
        h->dmax = txt_get_int(&t, r, "Dmg-Max", 3);
        h->dmg_lvl8 = txt_get_int(&t, r, "Dmg/Lvl", 2);
        for (int k = 0; k < 2; k++) {
            char col[16];
            snprintf(col, sizeof col, "Skill%d", k + 1);
            snprintf(h->sk[k].name, sizeof h->sk[k].name, "%s", txt_get(&t, r, txt_col(&t, col)));
            snprintf(col, sizeof col, "Chance%d", k + 1);
            h->sk[k].chance = txt_get_int(&t, r, col, 25);
            snprintf(col, sizeof col, "ChancePerLvl%d", k + 1);
            h->sk[k].chance_lvl4 = txt_get_int(&t, r, col, 0);
            snprintf(col, sizeof col, "Level%d", k + 1);
            h->sk[k].lvl = txt_get_int(&t, r, col, 1);
            snprintf(col, sizeof col, "LvlPerLvl%d", k + 1);
            h->sk[k].lvl_lvl32 = txt_get_int(&t, r, col, 0);
        }
        h->str = txt_get_int(&t, r, "Str", 35);
        h->str_lvl8 = txt_get_int(&t, r, "Str/Lvl", 10);
        h->dex = txt_get_int(&t, r, "Dex", 45);
        h->dex_lvl8 = txt_get_int(&t, r, "Dex/Lvl", 16);
        h->res = txt_get_int(&t, r, "ResistFire", 0);
        h->res_lvl4 = txt_get_int(&t, r, "ResistFire/Lvl", 8);
        s_res_mult = txt_get_int(&t, r, "resurrectcostmultiplier", s_res_mult);
        s_res_div = txt_get_int(&t, r, "resurrectcostdivisor", s_res_div);
        s_res_max = txt_get_int(&t, r, "resurrectcostmax", s_res_max);
    }
    txt_free(&t);
}

/* El tramo de la tabla para ese nivel (la última fila que no lo pasa) */
static const HireRow *merc_row(int type, int level)
{
    const HireRow *h = &s_hire[type][0];
    for (int k = 1; k < s_hire_rows[type]; k++)
        if (s_hire[type][k].level <= level)
            h = &s_hire[type][k];
    return h;
}

static void merc_stats(int type, int level, MercStats *ms)
{
    memset(ms, 0, sizeof *ms);
    ms->level = level;
    if (s_hire_rows[type] == 0) {
        ms->hp_max = 40;
        ms->dmin = 1;
        ms->dmax = 3;
        return;
    }
    const HireRow *h = merc_row(type, level);
    int d = level - h->level;
    ms->hp_max = h->hp + h->hp_lvl * d > 1 ? h->hp + h->hp_lvl * d : 1;
    ms->defense = h->def + h->def_lvl * d > 0 ? h->def + h->def_lvl * d : 0;
    ms->ar = h->ar + h->ar_lvl * d > 5 ? h->ar + h->ar_lvl * d : 5;
    ms->str = h->str + h->str_lvl8 * d / 8;
    ms->dex = h->dex + h->dex_lvl8 * d / 8;
    ms->resist = h->res + h->res_lvl4 * d / 4;
    ms->resist = ms->resist < 0 ? 0 : ms->resist > 75 ? 75 : ms->resist;
    ms->dmin = h->dmin + h->dmg_lvl8 * d / 8 > 1 ? h->dmin + h->dmg_lvl8 * d / 8 : 1;
    ms->dmax = h->dmax + h->dmg_lvl8 * d / 8 > ms->dmin ? h->dmax + h->dmg_lvl8 * d / 8 : ms->dmin;
    /* Sus habilidades son las de los personajes (skills.txt): la pícara usa
       Percepción y su flecha; el guerrero del desierto la Estocada y su aura */
    for (int k = 0; k < 2; k++) {
        const SkillDef *def = NULL;
        int cls = 0, lvl = h->sk[k].lvl + h->sk[k].lvl_lvl32 * d / 32;
        int chance = h->sk[k].chance + h->sk[k].chance_lvl4 * d / 4;
        for (int c = 0; c < NUM_CLASSES && !def; c++) {
            int s = skills_find(c, h->sk[k].name);
            if (s >= 0) {
                def = skills_get(c, s);
                cls = c;
            }
        }
        lvl = lvl < 1 ? 1 : lvl;
        chance = chance > 95 ? 95 : chance;
        if (!def)
            continue;
        if (s_hire_type[type].act == 3) { /* el Lobo de Hierro: lanza hechizos, como la hechicera (skillsor.inc) */
            SkillCtx x = { cls, NULL, level };
            static const struct { int kind; const char *missile; int elem, flags; } k_spells[] = {
                { SK_FIRE_BOLT, "firebolt", ELEM_FIRE, 0 },        { SK_FIRE_BALL, "fireball", ELEM_FIRE, MF_EXPLODE },
                { SK_GLACIAL_SPIKE, "glacialspike", ELEM_COLD, MF_FREEZE_BURST },
                { SK_CHARGED_BOLT, "chargedbolt", ELEM_LTNG, MF_CHARGED }, { SK_LIGHTNING, "lightningbolt", ELEM_LTNG, MF_PIERCE },
            };
            /* shortcut: la Armadura escalofriante no la usa (no hay armaduras en los aliados) */
            for (unsigned sp = 0; sp < sizeof k_spells / sizeof k_spells[0]; sp++) {
                if (k_spells[sp].kind != def->kind)
                    continue;
                ms->caster = 1;
                snprintf(ms->spell_missile[k], sizeof ms->spell_missile[k], "%s", k_spells[sp].missile);
                ms->spell_chance[k] = chance;
                ms->spell_elem[k] = k_spells[sp].elem;
                ms->spell_flags[k] = k_spells[sp].flags;
                skill_elem(def, lvl, &ms->spell_min[k], &ms->spell_max[k]);
                ms->spell_count[k] = def->kind == SK_CHARGED_BOLT ? skill_calc(def, SC_CALC1, lvl, &x) : 1;
                if (def->kind == SK_FIRE_BALL)
                    ms->spell_radius10[k] = 30;
                if (def->kind == SK_GLACIAL_SPIKE) {
                    ms->spell_radius10[k] = skill_calc(def, SC_AURARANGE, lvl, &x) * 10;
                    ms->spell_len[k] = skill_calc(def, SC_AURALEN, lvl, &x);
                }
            }
            continue;
        }
        if (def->aura) {
            snprintf(ms->aura, sizeof ms->aura, "%s", h->sk[k].name);
            ms->aura_lvl = lvl;
        } else if (def->kind == SK_JAB) {
            SkillCtx x = { cls, NULL, level };
            ms->melee = 1;
            ms->jab_chance = chance;
            ms->jab_dmg_pct = skill_calc(def, SC_CALC1, lvl, &x);
            ms->jab_ar_pct = skill_tohit(def, lvl);
        } else if (strcmp(h->sk[k].name, "Inner Sight") == 0) {
            int hi;
            skill_elem(def, lvl, &ms->sight_def, &hi);
            ms->sight_ticks = skill_ln(def, 3, 4, lvl);
        } else if (def->missile[0]) {
            ms->skill_chance = chance;
            snprintf(ms->skill_missile, sizeof ms->skill_missile, "%s", def->missile);
            skill_elem(def, lvl, &ms->emin, &ms->emax);
            ms->elem = def->kind == SK_COLD_ARROW ? ELEM_COLD : ELEM_FIRE;
            if (ms->elem == ELEM_COLD)
                ms->elen = skill_elen(def, lvl);
        }
    }
}

/* Precio de contratarla: el oro de la tabla, repartido entre un tramo y el
   siguiente según el nivel */
static int merc_cost(int type, int level)
{
    if (s_hire_rows[type] == 0)
        return 100 * level;
    const HireRow *h = merc_row(type, level), *next = NULL;
    for (int k = 0; k < s_hire_rows[type]; k++)
        if (s_hire[type][k].level > h->level && (!next || s_hire[type][k].level < next->level))
            next = &s_hire[type][k];
    int d = level - h->level;
    if (d < 0) /* más abajo que la primera fila: proporcional */
        return h->gold * level / (h->level > 0 ? h->level : 1) > 1 ? h->gold * level / h->level : 1;
    if (!next)
        return h->gold + h->gold * d / 10;
    return h->gold + (next->gold - h->gold) * d / (next->level - h->level);
}

/* Resucitarla: multiplicador x nivel² / divisor, con tope (hireling.txt) */
static int merc_revive_cost(const Player *p)
{
    long cost = (long)s_res_mult * p->merc_level * p->merc_level / (s_res_div > 0 ? s_res_div : 1);
    return cost > s_res_max ? s_res_max : (int)cost;
}

static const char *merc_name_of(int type, int name)
{
    /* str_get devuelve la clave si no la encuentra: que no sea una variable local */
    static char key[24];
    snprintf(key, sizeof key, "%s%02d", s_hire_type[type].names[0] ? s_hire_type[type].names : "merc", name);
    return str_get(key);
}

/* Sus valores con el equipo puesto: el arco suma su daño (con la destreza,
   como el del jugador) y lo elemental que traiga; casco y armadura su
   defensa; y las propiedades de vida, puntería, fuerza y destreza */
static void merc_stats_gear(const Player *p, MercStats *ms)
{
    merc_stats(p->merc_type, p->merc_level, ms);
    for (int s = 0; s < MERC_SLOTS; s++) {
        const Item *it = &p->merc_equip[s];
        if (!it->type)
            continue;
        ms->str += items_stat(it, STAT_STR);
        ms->dex += items_stat(it, STAT_DEX);
        ms->hp_max += items_stat(it, STAT_LIFE);
        ms->ar += items_stat(it, STAT_AR);
        if (it->type->kind == ITEM_ARMOR)
            ms->defense += items_defense(it);
    }
    const Item *w = &p->merc_equip[MSLOT_WEAPON];
    if (w->type) {
        int lo, hi, pct = 100 + ms->dex * w->type->dex_bonus / 100 + ms->str * w->type->str_bonus / 100;
        items_damage(w, &lo, &hi);
        ms->dmin += lo * pct / 100 + items_stat(w, STAT_FIRE_MIN) + items_stat(w, STAT_COLD_MIN) +
                    items_stat(w, STAT_LTNG_MIN);
        ms->dmax += hi * pct / 100 + items_stat(w, STAT_FIRE_MAX) + items_stat(w, STAT_COLD_MAX) +
                    items_stat(w, STAT_LTNG_MAX);
    }
}

/* Le pone a la unidad los valores de su tipo, nivel y equipo (hp < 0 = llena) */
static void merc_apply(Player *p, int hp)
{
    if (p->merc_unit < 0)
        return;
    MercStats ms;
    merc_stats_gear(p, &ms);
    units_merc_set(p->merc_unit, &ms, hp, merc_name_of(p->merc_type, p->merc_name));
}

/* ¿Va en ese lugar? Arcos para la pícara, lanzas y armas de asta para el
   guerrero del desierto (no las de la Amazona ni las jabalinas), cascos (no
   los de clase) y armaduras */
static int merc_slot_fits(const Player *p, int slot, const ItemType *t)
{
    switch (slot) {
    case MSLOT_WEAPON:
        if (s_hire_type[p->merc_type].act == 2)
            return (items_type_is(t, "spea") || items_type_is(t, "pole")) && !items_type_is(t, "aspe") &&
                   !items_type_is(t, "jave");
        if (s_hire_type[p->merc_type].act == 5) /* el bárbaro de Qual-Kehk: espadas */
            return items_type_is(t, "swor");
        return items_type_is(t, "bow") && !items_type_is(t, "abow");
    case MSLOT_HEAD:   return t->slot == SLOT_HELM && !items_type_is(t, "phlm") && !items_type_is(t, "pelt");
    case MSLOT_TORSO:  return t->slot == SLOT_BODY;
    default:           return 0;
    }
}

/* ¿Lo puede usar? Fuerza, destreza y nivel de ella; si no, el motivo */
static int merc_can_use(const Player *p, const ItemType *t, const char **why)
{
    MercStats ms;
    merc_stats_gear(p, &ms);
    *why = ms.str < t->req_str ? tr("Le falta fuerza") : ms.dex < t->req_dex ? tr("Le falta destreza")
         : p->merc_level < t->level_req ? tr("Le falta nivel") : NULL;
    return *why == NULL;
}

/* Lo que tenía puesto vuelve a la mochila (o al suelo si no entra) */
static void merc_strip(Player *p)
{
    for (int s = 0; s < MERC_SLOTS; s++)
        if (p->merc_equip[s].type) {
            if (!bag_add(p, &p->merc_equip[s]))
                items_ground_add(&p->merc_equip[s], p->x + 0.8f, p->y + 0.8f);
            memset(&p->merc_equip[s], 0, sizeof(Item));
        }
}

/* Los que ofrecen Kashya o Greiz (los de su acto): cerca del nivel del
   jugador, con tipo y nombre al azar */
static void merc_offers(Player *p)
{
    if (p->num_offers > 0 && p->offers_level == p->level_num && p->offers_act == s_act)
        return;
    int types[MERC_TYPES], nt = 0;
    for (int t = 0; t < MERC_TYPES; t++) /* (los de su dificultad: cada una tiene sus filas, y los que se contrataron siguen con las suyas) */
        if (s_hire_rows[t] > 0 && s_hire_type[t].act == s_act && s_hire_type[t].diff == s_diff + 1)
            types[nt++] = t;
    if (nt == 0 && s_act == 1) /* sin hireling.txt: las dos pícaras */
        types[nt++] = 0, types[nt++] = 1;
    p->num_offers = nt > 0 ? MERC_OFFERS : 0;
    p->offers_level = p->level_num;
    p->offers_act = s_act;
    for (int k = 0; k < p->num_offers; k++) {
        MercOffer *o = &p->offers[k];
        o->type = types[rnd() % (uint32_t)nt];
        o->level = p->level_num - 1 + (int)(rnd() % 3);
        o->level = o->level < 1 ? 1 : o->level;
        if (o->level < s_hire[o->type][0].level) /* (no menos que su primera fila: los bárbaros empiezan en el 28) */
            o->level = s_hire[o->type][0].level;
        int names = s_hire_type[o->type].num_names > 0 ? s_hire_type[o->type].num_names : MERC_NAMES;
        for (int tries = 0; tries < 20; tries++) {
            o->name = 1 + (int)(rnd() % (uint32_t)names);
            int dup = o->name == p->merc_name && s_hire_type[p->merc_type].act == s_act;
            for (int j = 0; j < k; j++)
                dup |= p->offers[j].name == o->name;
            if (!dup)
                break;
        }
    }
}

static void merc_hire(Player *p)
{
    if (p->hire_sel < 0 || p->hire_sel >= p->num_offers)
        return;
    MercOffer o = p->offers[p->hire_sel];
    int cost = p->free_hire ? 0 : merc_cost(o.type, o.level);
    if (p->gold < cost) {
        show_message(p, "No tenés suficiente oro");
        return;
    }
    int unit = units_merc_spawn((int)(p - s_pl), merc_monster(o.type), p->x, p->y);
    if (unit < 0)
        return;
    if (p->merc_name) /* la anterior se va: su equipo queda con el jugador */
        merc_strip(p);
    p->gold -= cost;
    p->free_hire = 0;
    p->merc_unit = unit;
    p->merc_name = o.name;
    p->merc_type = o.type;
    p->merc_level = o.level;
    p->merc_dead = 0;
    p->merc_warned = 0;
    merc_apply(p, -1);
    for (int k = p->hire_sel; k < p->num_offers - 1; k++)
        p->offers[k] = p->offers[k + 1];
    p->num_offers--;
    p->hire_open = 0;
    float mx, my;
    units_position(unit, 1.0f, &mx, &my);
    audio_play_name(MERC_ACT(o.type).hired, mx, my); /* "Como ordenes" */
    char msg[48];
    snprintf(msg, sizeof msg, tr("Contrataste a %s"), merc_name_of(o.type, o.name));
    show_message(p, msg);
    save_character(p);
}

static void merc_revive(Player *p)
{
    int cost = merc_revive_cost(p);
    if (p->gold < cost) {
        show_message(p, "No tenés suficiente oro");
        return;
    }
    if (p->merc_unit < 0)
        p->merc_unit = units_merc_spawn((int)(p - s_pl), merc_monster(p->merc_type), p->x, p->y);
    if (p->merc_unit < 0)
        return;
    p->gold -= cost;
    p->merc_dead = 0;
    p->merc_warned = 0;
    units_merc_warp(p->merc_unit, p->x, p->y);
    merc_apply(p, -1);
    float mx, my;
    units_position(p->merc_unit, 1.0f, &mx, &my);
    audio_play_name(MERC_ACT(p->merc_type).hired, mx, my);
    p->dialog_sel = 0;
    save_character(p);
}

/* Lista de Kashya: arriba y abajo eligen, A contrata y B vuelve */
static void hire_tick(Player *p, const PlayerInput *pi)
{
    units_talk(p->dialog_npc, p->x, p->y);
    if (pi->back || pi->inventory || p->num_offers == 0) {
        p->hire_open = 0;
        return;
    }
    if (pi->nav_up)
        p->hire_sel = (p->hire_sel + p->num_offers - 1) % p->num_offers;
    if (pi->nav_down)
        p->hire_sel = (p->hire_sel + 1) % p->num_offers;
    if (pi->action)
        merc_hire(p);
}

/* Lo que dicen los NPC: los textos del juego (vo.json, los subtítulos de
   las voces). Cada NPC tiene su nombre en las claves ("KashyaGossip3"). */
static const struct { const char *id, *vo; } k_npc_vo[NPC_TALKERS] = {
    { "akara", "Akara" }, { "kashya", "Kashya" }, { "charsi", "Charsi" }, { "gheed", "Gheed" }, { "warriv", "Warriv" },
    { "cain1", "Cain" },
    /* Lut Gholein (al final: las partidas guardan el número de cada uno). Greiz
       se llama "Griez" en las claves; Warriv y Caín tienen las suyas del Acto 2 */
    { "atma", "Atma" }, { "drognan", "Drognan" }, { "elzix", "Elzix" }, { "fara", "Fara" }, { "geglash", "Geglash" },
    { "greiz", "Griez" }, { "jerhyn", "Jerhyn" }, { "lysander", "Lysander" }, { "meshif1", "Meshif" },
    { "warriv2", "WarrivAct2" }, { "cain2", "CainAct2" }, { "act2guard2", "PalaceGuard" }, /* Kaelan */
    { "tyrael1", "Tyrael" }, /* en la cámara de Tal Rasha */
    /* el muelle de Kurast: Meshif y Caín tienen las suyas del Acto 3 */
    { "ormus", "Ormus" }, { "alkor", "Alkor" }, { "asheara", "Asheara" }, { "hratli", "Hratli" },
    { "natalya", "Natalya" }, { "meshif2", "MeshifAct3" }, { "cain3", "CainAct3" },
    /* el Acto 4: la Fortaleza (Halbu y Jamella no hablan), el espíritu de Izual y Hadriel */
    { "tyrael2", "TyraelAct4" }, { "cain4", "CainAct4" }, { "izualghost", "Izual" }, { "malachai", "HellsAngel" },
    /* Harrogath: Anya es "drehya" en monstats */
    { "larzuk", "Larzuk" }, { "malah", "Malah" }, { "qual-kehk", "QualKehk" }, { "nihlathak", "Nihlathak" },
    { "cain6", "CainAct5" }, { "drehya", "Anya" },
    { "tyrael3", "TyraelAct5" }, /* en la Cámara de la Piedra del Mundo, muerto Baal (a5quest.inc) */
};
#define NPC_VO_COUNT ((int)(sizeof k_npc_vo / sizeof k_npc_vo[0]))

/* El número de un NPC por su nombre en las claves ("CainAct5"); -1 si no está */
static int npc_vo_index_by_vo(const char *vo)
{
    for (int i = 0; i < NPC_VO_COUNT; i++)
        if (strcmp(vo, k_npc_vo[i].vo) == 0)
            return i;
    return -1;
}

static int npc_vo_index(const char *npc)
{
    for (int i = 0; i < NPC_VO_COUNT; i++) /* primero el nombre exacto ("warriv2" no es el del Acto 1) */
        if (strcmp(npc, k_npc_vo[i].id) == 0)
            return i;
    for (int i = 0; i < NPC_VO_COUNT; i++)
        if (strncmp(npc, k_npc_vo[i].id, strlen(k_npc_vo[i].id)) == 0)
            return i;
    return -1;
}

/* Caín no se presenta (ya lo conocemos) */
static int npc_is_cain(int who)
{
    return who >= 0 && strncmp(k_npc_vo[who].id, "cain", 4) == 0;
}

/* Algunos comentarios le hablan a otra clase ("No creas que puedes engañarme,
   nigromante"): esos no van */
static int speaks_to_other_class(const Player *p, const char *text)
{
    /* Solo las clases que no se juegan, como palabra entera ("asesinados" no
       cuenta). Las que nombran a las amazonas o a los bárbaros hablan de ellos
       en general (los padres de Charsi), no al jugador: esas van. */
    static const char *const others_es[] = { "nigromante", "paladín", "hechicera", "druida", "asesina" };
    static const char *const others_en[] = { "necromancer", "paladin", "sorceress", "druid", "assassin",
                                             "Necromancer", "Paladin", "Sorceress", "Druid", "Assassin" };
    /* En los otros idiomas, los nombres de las clases del juego (y con minúscula) */
    static const char *const keys[] = { "partycharnec", "partycharpal", "partycharsor", "partychardru", "partycharass" };
    static char lower[5][48];
    const char *list[10];
    unsigned num = 0;
    int l = lang_current();
    if (l == LANG_EN) {
        for (unsigned i = 0; i < sizeof others_en / sizeof others_en[0]; i++)
            list[num++] = others_en[i];
    } else if (l == LANG_ES || l == LANG_ES_ES) {
        for (unsigned i = 0; i < sizeof others_es / sizeof others_es[0]; i++)
            list[num++] = others_es[i];
    } else {
        for (unsigned i = 0; i < 5; i++) {
            list[num++] = str_get(keys[i]);
            snprintf(lower[i], sizeof lower[i], "%s", str_get(keys[i]));
            unsigned char *c = (unsigned char *)lower[i];
            if (c[0] < 0x80)
                c[0] = (unsigned char)tolower(c[0]);
            else if (c[0] == 0xC3 && c[1] >= 0x80 && c[1] <= 0x9E) /* À..Þ */
                c[1] += 0x20;
            else if (c[0] == 0xD0 && c[1] >= 0x90 && c[1] <= 0x9F) /* А..П (ruso) */
                c[1] += 0x20;
            else if (c[0] == 0xD0 && c[1] >= 0xA0 && c[1] <= 0xAF) /* Р..Я */
                c[0] = 0xD1, c[1] -= 0x20;
            list[num++] = lower[i];
        }
    }
    /* (en chino y japonés no hay espacios: alcanza con que esté) */
    int loose = l == LANG_JA || l == LANG_ZH_CN || l == LANG_ZH_TW;
    (void)p;
    for (unsigned i = 0; i < num; i++) {
        size_t n = strlen(list[i]);
        if (!n)
            continue;
        for (const char *s = strstr(text, list[i]); s; s = strstr(s + 1, list[i])) {
            unsigned char before = s > text ? (unsigned char)s[-1] : ' ', after = (unsigned char)s[n];
            if (loose || (!isalpha(before) && before < 0x80 && !isalpha(after) && after < 0x80))
                return 1;
        }
    }
    return 0;
}

/* ---- Lo que dice cada NPC ----
   Como en el original: la primera vez que se abre el diálogo se presenta
   solo; "Hablar" da primero las recompensas y los pedidos de misión, después
   los comentarios de cada etapa de las misiones (una vez cada uno, en orden)
   y por último la charla suelta, también en orden. Algunas frases sueltas
   recién tienen sentido cuando avanza la historia (las que hablan de Caín o
   de Andariel): esas se desbloquean con las misiones. */

/* Los comentarios de las etapas de las misiones (con el nombre del NPC al
   final: "A1Q1AfterInitCharsi"), en el orden en que se dicen */
static const char *const k_talk_lines[QUEST_LINES] = {
    "A1Q1AfterInit", "A1Q1EarlyReturn", "A1Q1Successful",
    "A1Q2AfterInit", "A1Q2EarlyReturn", "A1Q2Successful",
    "A1Q4EarlyReturn", "A1Q4AfterInitScroll", "A1Q4SuccessfulScroll", "A1Q4EarlyReturnS", "A1Q4QuestSuccessful",
    "A1Q5AfterInit", "A1Q5EarlyReturn", "A1Q5Successful",
    "A1Q3AfterInit", "A1Q3EarlyReturn", "A1Q3Successful",
    "A1Q6AfterInit", "A1Q6EarlyReturn", "A1Q6Successful",
    /* Acto 2 (el Báculo Horádrico solo lo comenta Caín, pieza por pieza: cain_piece) */
    "A2Q1AfterInit", "A2Q1EarlyReturn", "A2Q1Successful",
    "A2Q3AfterInit", "A2Q3EarlyReturn", "A2Q3Successful",
    "A2Q4AfterInit", "A2Q4EarlyReturn", "A2Q4Successful",
    "A2Q5EarlyReturn", "A2Q5Successful",
    "A2Q6AfterInit", "A2Q6Successful",
    /* Acto 3 (la Voluntad de Khalim solo la comenta Caín, parte por parte: a3quest.inc) */
    "A3Q1AfterInit", "A3Q1EarlyReturn", "A3Q1Successful",
    "A3Q3AfterInit", "A3Q3EarlyReturn", "A3Q3Successful",
    "A3Q4AfterInit", "A3Q4Successful",
    "A3Q5AfterInit", "A3Q5EarlyReturn", "A3Q5Successful",
    "A3Q6AfterInit", "A3Q6EarlyReturn", "A3Q6Successful",
    /* Acto 5 (gpAct5Q1NpcMessages...: los de Harrogath, desde el 47) */
    "A5Q1AfterInit", "A5Q1EarlyReturn", "A5Q1Successful",
    "A5Q2AfterInit", "A5Q2EarlyReturn", "A5Q2Successful",
    "A5Q3AfterInit", "A5Q3EarlyReturn", "A5Q3FoundAnya", "A5Q3Successful",
    "A5Q4AfterInit", "A5Q4EarlyReturn", "A5Q4Successful",
    "A5Q5AfterInit", "A5Q5EarlyReturn", "A5Q5Successful",
    "A5Q6EarlyReturn", "A5Q6Successful",
};
#define TALK_ACT5 47 /* (el primero del Acto 5) */
#define NPC_ACT5  30 /* (el primero de Harrogath en k_npc_vo) */

static int talk_heard_get(const Player *p, int who, int id)
{
    return (int)((p->talk_heard[who][id >> 6] >> (id & 63)) & 1);
}

static void talk_heard_set(Player *p, int who, int id)
{
    p->talk_heard[who][id >> 6] |= (uint64_t)1 << (id & 63);
}

/* Los "volviste sin terminar": solo si salió del campamento desde la última vez */
static int talk_line_is_return(int id)
{
    return id == 1 || id == 4 || id == 6 || id == 9 || id == 12 || id == 15 || id == 18 || id == 21 || id == 24 ||
           id == 27 || id == 29 || id == 34 || id == 37 || id == 42 || id == 45 || id == 48 || id == 51 || id == 54 ||
           id == 58 || id == 61 || id == 63;
}

/* ¿Toca ahora el comentario `id`? (según cómo va la misión) */
static int talk_line_due(const Player *p, int id)
{
    switch (id) {
    case 0: case 1:   return p->den_started && !p->den_done;
    case 2:           return p->den_done;
    case 3: case 4:   return p->raven_started && !p->raven_done;
    case 5:           return p->raven_done;
    case 6:           return p->cain_q == 1;
    case 7: case 8:   return p->cain_q == 2 || p->cain_q == 3;
    case 9:           return p->cain_q == 4;
    case 10:          return p->cain_q >= 5;
    case 11: case 12: return p->tower_q == 1;
    case 13:          return p->tower_q >= 2;
    case 14: case 15: return p->malus_q == 1 || p->malus_q == 2;
    case 16:          return p->malus_q >= 3;
    case 17: case 18: return p->andy_q == 1;
    case 19:          return p->andy_q >= 2;
    case 20: case 21: return p->rada_q == 1;
    case 22:          return p->rada_q >= 2;
    case 23:          return p->sun_q == 1 || p->sun_q == 2;
    case 24:          return p->sun_q == 2;
    case 25:          return p->sun_q >= 3;
    case 26:          return p->arcane_q == 1 || p->arcane_q == 2;
    case 27:          return p->arcane_q == 3;
    case 28:          return p->arcane_q >= 4;
    case 29:          return p->summ_q == 1;
    case 30:          return p->summ_q >= 2;
    case 31:          return p->tombs_q >= 1 && p->tombs_q <= 2;
    case 32:          return p->tombs_q >= 4;
    case 33: case 34: return p->tome_q == 1;
    case 35:          return p->tome_q >= 3;
    case 36: case 37: return p->blade_q == 1;
    case 38:          return p->blade_q >= 2;
    case 39:          return p->bird_q >= 3 && p->bird_q <= 5;
    case 40:          return p->bird_q >= 6;
    case 41: case 42: return p->temple_q == 1 || p->temple_q == 2;
    case 43:          return p->temple_q >= 3;
    case 44: case 45: return p->meph_q == 1;
    case 46:          return p->meph_q >= 2;
    case 47: case 48: return p->shenk_q == 1;
    case 49:          return p->shenk_q >= 2;
    case 50: case 51: return p->rescue_q == 1;
    case 52:          return p->rescue_q >= 2;
    case 53: case 54: return p->anya_q == 1;
    case 55:          return p->anya_q == 2 || p->anya_q == 3;
    case 56:          return p->anya_q >= 4;
    case 57: case 58: return p->nihl_q == 1 || p->nihl_q == 2;
    case 59:          return p->nihl_q >= 3;
    case 60: case 61: return p->ancients_q == 1 || p->ancients_q == 2;
    case 62:          return p->ancients_q >= 3;
    case 63:          return p->baal_q >= 1 && p->baal_q <= 3;
    case 64:          return p->baal_q >= 4;
    default:          return 0;
    }
}

/* La clave del comentario para ese NPC ("A1Q1AfterInitCharsi" o "...Main"), o 0 si no tiene */
static int talk_line_key(const Player *p, int id, const char *name, char *key, size_t cap)
{
    /* Los del Acto 5 solo los dicen los de Harrogath (el Caín del Acto 1 también se llama "Cain") */
    int who = npc_vo_index_by_vo(name);
    if (id >= TALK_ACT5 && who < NPC_ACT5)
        return 0;
    /* Caín y Greiz se llaman distinto en las del Acto 2 ("A2Q1AfterInitCain", "A2Q1AfterInitGreiz") y
       Caín en las del 5 ("A5Q1AfterInitCain") */
    const char *alt = strcmp(name, "CainAct2") == 0 || strcmp(name, "CainAct5") == 0 ? "Cain"
                      : strcmp(name, "Griez") == 0                                   ? "Greiz"
                                                                                     : NULL;
    for (int k = 0; k < (alt ? 2 : 1); k++) {
        const char *nm = k ? alt : name;
        snprintf(key, cap, "%s%s", k_talk_lines[id], nm);
        if (!str_exists(key))
            snprintf(key, cap, "%s%sMain", k_talk_lines[id], nm);
        if (str_exists(key))
            return !speaks_to_other_class(p, str_get(key));
    }
    return 0;
}

/* Desde cuándo tiene sentido cada frase suelta (las que no están, siempre) */
enum { G_CAIN_KNOWN = 1, G_CAIN_HERE, G_ANDARIEL, G_STONES };
static const struct { const char *key; int when; } k_gossip_gate[] = {
    { "AkaraGossip2", G_CAIN_KNOWN },  { "AkaraGossip11", G_ANDARIEL },
    { "KashyaGossip2", G_CAIN_KNOWN }, { "KashyaGossip7", G_ANDARIEL }, { "KashyaGossip9", G_ANDARIEL },
    { "CharsiGossip2", G_CAIN_HERE },  { "CharsiGossip7", G_ANDARIEL },
    { "GheedGossip2", G_CAIN_HERE },   { "GheedGossip6", G_ANDARIEL },
    { "WarrivGossip2", G_CAIN_HERE },  { "WarrivGossip9", G_STONES },
};

static int gossip_unlocked(const Player *p, const char *key)
{
    for (unsigned i = 0; i < sizeof k_gossip_gate / sizeof k_gossip_gate[0]; i++) {
        if (strcmp(k_gossip_gate[i].key, key) != 0)
            continue;
        switch (k_gossip_gate[i].when) {
        case G_CAIN_KNOWN: return p->cain_q >= 1;               /* Akara ya habló de él */
        case G_CAIN_HERE:  return p->cain_q >= 5;               /* ya está en el campamento */
        case G_ANDARIEL:   return p->raven_started || p->andy_q >= 1; /* Kashya la nombró */
        case G_STONES:     return p->cain_q >= 2 || ((p->waypoints >> 2) & 1);
        default:           return 1;
        }
    }
    return 1;
}

/* Si la clave es un comentario de misión de ese NPC, queda como dicho */
static void talk_mark_heard(Player *p, int who, const char *name, const char *key)
{
    char k[64];
    for (int id = 0; id < QUEST_LINES; id++)
        if (talk_line_key(p, id, name, k, sizeof k) && strcmp(k, key) == 0)
            talk_heard_set(p, who, id);
}

/* La presentación del NPC (la de la clase si hay: "CharsiIntroBarGossip1") */
static int npc_intro_key(const Player *p, const char *name, char *key, size_t cap)
{
    const char *cls = s_classes[p->cls].abbr;
    if (strcmp(name, "TyraelAct4") == 0) { /* la de la Fortaleza (A4Q0) no lleva el número del acto */
        snprintf(key, cap, "TyraelActIntroGossip1");
        return 1;
    }
    static const char *const forms[] = { "%sIntro%sGossip1", "%sAct1Intro%sGossip1", "%sActIntro%sGossip1",
                                         "%sAct5Intro%sGossip1", "%sIntroGossip1", "%sAct1IntroGossip1",
                                         "%sActIntroGossip1", "%sAct5IntroGossip1" }; /* Hratli: "HratliActIntro..." */
    for (unsigned f = 0; f < sizeof forms / sizeof forms[0]; f++) {
        snprintf(key, cap, forms[f], name, cls);
        if (str_exists(key))
            return 1;
    }
    key[0] = '\0';
    return 0;
}

/* Que se oiga y se lea lo que dice */
static char s_last_talk[64]; /* la última clave que se dijo (para D2_TALKTEST) */

static void npc_say(Player *p, const char *key)
{
    snprintf(s_last_talk, sizeof s_last_talk, "%s", key);
    p->talk_text = key[0] && str_exists(key) ? str_get(key) : "...";
    char voice[160];
    p->talk_voice = key[0] && str_voice_path(key, voice, sizeof voice) == 0 ? audio_voice(voice) : 0;
    s_voice_owner = p->talk_voice > 0 ? (int)(p - s_pl) : -1;
    p->talk_ticks = 0;
}

/* ¿El NPC tiene algo nuevo de las misiones para ese jugador? (una
   recompensa, un pedido, algo que hacer con un objeto de misión, el viaje al
   este): lo mismo que da npc_talk, sin cambiar nada. Se marca con un "!"
   arriba de la cabeza, como en el original. */
/* El Mazo de Horadric: su lugar en la mochila, -2 si lo tiene en la mano
   (es un arma: se puede poner) o -1 si no lo tiene */
static int malus_slot(const Player *p)
{
    int k = bag_index_of(p, "hdm");
    if (k >= 0)
        return k;
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    return w && strcmp(w->code, "hdm") == 0 ? -2 : -1;
}

/* ¿Lo tiene encima (mochila, puesto, cubo o en la mano)? */
static int player_has(const Player *p, const char *code)
{
    if (bag_index_of(p, code) >= 0 || (p->held.type && strcmp(p->held.type->code, code) == 0))
        return 1;
    for (int s = 0; s < SLOT_COUNT; s++)
        if (p->equip[s].type && strcmp(p->equip[s].type->code, code) == 0)
            return 1;
    for (int k = 0; k < p->cube_count; k++)
        if (p->cube[k].type && strcmp(p->cube[k].type->code, code) == 0)
            return 1;
    return 0;
}

/* Le saca uno (el pergamino, que se queda Caín) */
static void take_item(Player *p, const char *code)
{
    int k = bag_index_of(p, code);
    if (k >= 0) {
        bag_remove(p, k);
        return;
    }
    if (p->held.type && strcmp(p->held.type->code, code) == 0) {
        memset(&p->held, 0, sizeof p->held);
        return;
    }
    for (int c = 0; c < p->cube_count; c++)
        if (p->cube[c].type && strcmp(p->cube[c].type->code, code) == 0) {
            cube_remove(p, c);
            return;
        }
}

/* Lo que Caín dice de las piezas del Báculo Horádrico que tiene: la primera
   que todavía no comentó, en el orden del original (el Báculo, el cubo, el
   pergamino, el amuleto, la vara; ACT2Q2_CheckItemsAndState). NULL si nada. */
static const char *cain_piece(const Player *p, int *bit)
{
    static const struct { const char *code, *key; int bit; } k_pieces[] = {
        { "hst", "A2Q2SuccessfulStaffCain", A2_SEEN_HORADRIC }, { "box", "A2Q2EarlyReturnCubeCain", A2_SEEN_CUBE },
        { "tr1", "A2Q2EarlyReturnScrollCain", A2_SEEN_SCROLL }, { "vip", "A2Q2EarlyReturnCapCain", A2_SEEN_AMULET },
        { "msf", "A2Q2EarlyReturnStaveCain", A2_SEEN_STAFF },
    };
    for (unsigned k = 0; k < sizeof k_pieces / sizeof k_pieces[0]; k++)
        if (!(p->staff_seen & k_pieces[k].bit) && player_has(p, k_pieces[k].code)) {
            *bit = k_pieces[k].bit;
            return k_pieces[k].key;
        }
    return NULL;
}

static int npc_has_news(const Player *p, const char *npc)
{
    int who = npc_vo_index(npc), bit;
    char k[64];
    if (a2_npc_news(p, who) || a3_npc_news(p, who) || a4_npc_news(p, who) || a5_npc_news(p, who))
        return 1;
    if (who == 6 && ((p->rada_q == 0 && s_act == 2) || p->rada_q == 2)) /* Atma: Radament */
        return 1;
    if (who == 16 && cain_piece(p, &bit)) /* Caín: una pieza del Báculo */
        return 1;
    if (who == 7 && p->sun_q == 1) /* Drognan: el eclipse */
        return 1;
    if (who >= 6 && p->sun_q == 3 && talk_line_key(p, 25, k_npc_vo[who].vo, k, sizeof k)) /* volvió el sol */
        return 1;
    if (who == 0) /* Akara */
        return (p->den_done && !p->den_rewarded) || bag_index_of(p, "bks") >= 0 || p->cain_q == 5 || p->tower_q == 2 ||
               (!p->den_started && !p->den_done) ||
               (p->cain_q == 0 && (p->raven_done || p->den_done || ((p->waypoints >> 2) & 1)));
    if (who == 1) /* Kashya */
        return (p->raven_done && !p->raven_rewarded) || (!p->raven_started && !p->raven_done && p->den_done);
    if (who == 2) /* Charsi */
        return malus_slot(p) != -1 || (p->malus_q == 0 && (p->waypoints >> 4) != 0);
    if (who == 4) /* Warriv: el viaje al este */
        return p->andy_q == 2;
    if (who == 5) /* Caín */
        return p->andy_q == 0;
    return 0;
}

static void npc_talk(Player *p, const char *npc)
{
    int who = npc_vo_index(npc);
    if (who < 0) {
        p->talk_text = "...";
        p->talk_ticks = 0;
        return;
    }
    const char *name = k_npc_vo[who].vo;
    char key[64] = "";
    int akara = who == 0, kashya = who == 1, charsi = who == 2, cain = who == 5;
    int scroll = bag_index_of(p, "bks"), malus = malus_slot(p), bit = 0;
    /* Lo del Acto 2: la pieza que Caín comenta y si con este se cumple el Sol Mancillado */
    const char *piece = who == 16 ? cain_piece(p, &bit) : NULL;
    char sun_key[64];
    int sun_done = who >= 6 && p->sun_q == 3 && talk_line_key(p, 25, name, sun_key, sizeof sun_key);
    if (!(p->npc_met & (1ull << who)) && !npc_is_cain(who)) {
        /* Todavía no se presentó (si se habló sin abrir el diálogo; Kaelan no tiene presentación) */
        p->npc_met |= 1ull << who;
        npc_intro_key(p, name, key, sizeof key);
    }
    if (key[0]) {
        /* la presentación */
    } else if (akara && p->den_done && !p->den_rewarded) {
        /* Como en el original, la recompensa es un punto de habilidad */
        p->den_rewarded = 1;
        p->skill_points++;
        audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
        snprintf(key, sizeof key, "A1Q1SuccessfulAkara");
    } else if (kashya && p->raven_done && !p->raven_rewarded) {
        p->raven_rewarded = 1;
        p->free_hire = 1; /* como en el original: una mercenaria gratis */
        audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
        snprintf(key, sizeof key, "A1Q2SuccessfulKashya");
    } else if (akara && scroll >= 0) {
        /* Akara traduce el pergamino: ahora es la llave de las piedras */
        bag_remove(p, scroll);
        give_quest_item(p, "bkd");
        p->cain_q = 3;
        snprintf(key, sizeof key, "A1Q4InstructionsAkara");
    } else if (akara && p->cain_q == 5) {
        p->cain_q = 6;
        quest_done("qstsa1q4", NULL);
        snprintf(key, sizeof key, "A1Q4QuestSuccessfulAkara");
    } else if (charsi && malus != -1) {
        /* Devolver el Mazo (de la mochila o de la mano): Charsi ofrece imbuir un objeto */
        if (malus >= 0) {
            bag_remove(p, malus);
        } else {
            memset(&p->equip[SLOT_WEAPON], 0, sizeof p->equip[SLOT_WEAPON]);
            refresh_look(p);
        }
        p->malus_q = 3;
        quest_done("qstsa1q3", "music_quest_forge");
        player_say(p, "act1_complete_tools");
        snprintf(key, sizeof key, "A1Q3SuccessfulCharsi");
    } else if (akara && p->tower_q == 2) {
        p->tower_q = 3;
        snprintf(key, sizeof key, "A1Q5SuccessfulAkara");
    } else if (akara && !p->den_started && !p->den_done) {
        party_raise(offsetof(Player, den_started), 1); /* los pedidos son de los dos jugadores */
        snprintf(key, sizeof key, "A1Q1InitAkara");
    } else if (kashya && !p->raven_started && !p->raven_done && p->den_done) {
        /* Como en el original: después de limpiar la Guarida del Mal */
        party_raise(offsetof(Player, raven_started), 1);
        snprintf(key, sizeof key, "A1Q2InitKashya");
    } else if (akara && p->cain_q == 0 && (p->raven_done || p->den_done || (p->waypoints >> 2) & 1)) {
        party_raise(offsetof(Player, cain_q), 1); /* después de la cueva o el cementerio (o de llegar al Stony Field) */
        snprintf(key, sizeof key, "A1Q4InitAkara");
    } else if (charsi && p->malus_q == 0 && (p->waypoints >> 4) != 0) {
        party_raise(offsetof(Player, malus_q), 1); /* cuando ya se llegó al Black Marsh */
        snprintf(key, sizeof key, "A1Q3InitCharsi");
    } else if (cain && p->andy_q == 0) {
        party_raise(offsetof(Player, andy_q), 1);
        snprintf(key, sizeof key, "A1Q6InitCain");
    } else if (who == 6 && p->rada_q == 2) { /* Atma: Radament murió */
        p->rada_q = 3;
        quest_done("qstsa2q1", NULL);
        snprintf(key, sizeof key, "A2Q1SuccessfulAtma");
    } else if (who == 6 && p->rada_q == 0) {
        party_raise(offsetof(Player, rada_q), 1);
        snprintf(key, sizeof key, "A2Q1InitAtma");
    } else if (piece) { /* Caín y las piezas del Báculo: el pergamino se queda con él */
        p->staff_seen |= bit;
        if (bit == A2_SEEN_SCROLL) {
            take_item(p, "tr1");
            if (p->staff_q < 1)
                p->staff_q = 1;
        }
        if (bit == A2_SEEN_HORADRIC && p->staff_q < 2)
            p->staff_q = 2;
        snprintf(key, sizeof key, "%s", piece);
    } else if (who == 7 && p->sun_q == 1) { /* Drognan explica el eclipse */
        party_raise(offsetof(Player, sun_q), 2);
        snprintf(key, sizeof key, "A2Q3AfterInitDrognan");
    } else if (sun_done) { /* volvió el sol: cualquiera de Lut Gholein lo agradece */
        p->sun_q = 4;
        quest_done("qstsa2q3", NULL);
        snprintf(key, sizeof key, "%s", sun_key);
    } else if (a3_npc_quest(p, who, key, sizeof key)) {
        /* el muelle de Kurast (a3quest.inc) */
    } else if (a4_npc_quest(p, who, key, sizeof key)) {
        /* la Fortaleza (a4quest.inc) */
    } else if (a5_npc_quest(p, who, key, sizeof key)) {
        /* Harrogath (a5quest.inc) */
    } else if (a2_npc_quest(p, who, key, sizeof key)) {
        /* el Santuario, el Invocador y las Siete Tumbas */
    } else {
        /* Los comentarios de las misiones que todavía no dijo, en orden (así
           "después del pedido" va antes de "volviste sin terminar") */
        char k[64];
        for (int id = 0; id < QUEST_LINES && !key[0]; id++)
            if (talk_line_due(p, id) && !talk_heard_get(p, who, id) &&
                (!talk_line_is_return(id) || p->talk_away[who]) && talk_line_key(p, id, name, k, sizeof k))
                snprintf(key, sizeof key, "%s", k);
        /* Si no, la siguiente frase suelta que ya tenga sentido (en orden, y vuelve a empezar) */
        for (int step = 0; step < GOSSIP_MAX && !key[0]; step++) {
            int g = (p->talk_next[who] + step) % GOSSIP_MAX;
            snprintf(k, sizeof k, "%sGossip%d", name, g + 1);
            if (str_exists(k) && gossip_unlocked(p, k) && !speaks_to_other_class(p, str_get(k))) {
                snprintf(key, sizeof key, "%s", k);
                p->talk_next[who] = (uint8_t)((g + 1) % GOSSIP_MAX);
            }
        }
        /* Si no tiene nada más, lo que corresponda a la misión en curso */
        for (int id = 0; id < QUEST_LINES && !key[0]; id++)
            if (talk_line_due(p, id) && talk_line_key(p, id, name, k, sizeof k))
                snprintf(key, sizeof key, "%s", k);
        if (!key[0])
            npc_intro_key(p, name, key, sizeof key);
    }
    uint64_t before0 = p->talk_heard[who][0], before1 = p->talk_heard[who][1];
    talk_mark_heard(p, who, name, key);
    if (p->talk_heard[who][0] != before0 || p->talk_heard[who][1] != before1)
        p->talk_away[who] = 0; /* habló de las misiones: el próximo "volviste" es cuando vuelva */
    npc_say(p, key);
    save_character(p);
}

static void open_dialog(Player *p, int npc)
{
    p->dialog_npc = npc;
    p->dialog_sel = 0;
    p->talk_text = NULL;
    p->inv_open = 0;
    p->wp_open = 0;
    /* La primera vez se presenta solo, como en el original */
    int who = npc_vo_index(units_id(npc));
    char key[64];
    if (who >= 0 && !npc_is_cain(who) && !(p->npc_met & (1ull << who)) && npc_intro_key(p, k_npc_vo[who].vo, key, sizeof key)) {
        p->npc_met |= 1ull << who;
        npc_say(p, key);
        save_character(p);
    }
    /* Hablar con Akara (con Fara en Lut Gholein, con Jamella en la Fortaleza, con Malah en Harrogath) cura, como en el original */
    if (strcmp(units_id(npc), "akara") == 0 || strcmp(units_id(npc), "fara") == 0 || strcmp(units_id(npc), "jamella") == 0 ||
        strcmp(units_id(npc), "malah") == 0) {
        p->hp = p->hp_max;
        p->mana = p->mana_max;
    }
}

/* ---- Waypoints ---- */

static void waypoints_find(void)
{
    for (int k = 0; k < WP_MAX; k++)
        s_wp_unit[k] = -1;
    int ids[24], n = 0;
    for (unsigned k = 0; k < sizeof k_wp_objects / sizeof k_wp_objects[0]; k++)
        n += units_find_all(k_wp_objects[k], ids + n, 24 - n);
    for (int i = 0; i < n; i++) {
        float x, y;
        units_position(ids[i], 1.0f, &x, &y);
        const Level *lv = world_level_at(x, y);
        if (lv && lv->waypoint >= 0 && lv->waypoint < WP_MAX)
            s_wp_unit[lv->waypoint] = ids[i];
        if (s_debug)
            printf("waypoint de %s en (%.0f, %.0f)\n", lv ? lv->name : "?", x, y);
    }
}

/* Encendido si algún jugador lo activó */
static void waypoints_light(void)
{
    for (int k = 0; k < WP_MAX; k++) {
        if (s_wp_unit[k] < 0)
            continue;
        int lit = 0;
        for (int i = 0; i < s_num_players; i++)
            lit |= (s_pl[i].waypoints >> k) & 1;
        units_set_lit(s_wp_unit[k], lit);
    }
}

/* Número del waypoint que el jugador tiene al lado, o -1 */
static int waypoint_near(const Player *p)
{
    for (int k = 0; k < WP_MAX; k++) {
        if (s_wp_unit[k] < 0)
            continue;
        float x, y;
        units_position(s_wp_unit[k], 1.0f, &x, &y);
        if ((x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < WP_RANGE * WP_RANGE)
            return k;
    }
    return -1;
}

static void open_waypoints(Player *p, int here)
{
    p->wp_open = 1;
    p->wp_sel = here >= 0 ? here : wp_first(s_act);
    p->inv_open = 0;
}

/* ¿Puede ver la solapa de ese acto? (la de cada acto al que llegó: el II
   desde que viajó al este, el III desde que navegó a Kurast, el IV desde
   que entró al portal rojo) */
static int wp_act_open(const Player *p, int act)
{
    return act == 1 || act <= s_act || act <= p->act || act <= p->act_top || (act == 2 && p->andy_q >= 3);
}

/* Cuántos waypoints tiene la solapa (la del Acto 4, tres) */
static int wp_rows(int tab)
{
    return k_wp_first[tab + 1] - k_wp_first[tab];
}

/* Panel abierto: arriba y abajo eligen, A viaja y B cierra */
static void waypoint_tick(Player *p, const PlayerInput *pi)
{
    if (pi->back || pi->inventory) {
        p->wp_open = 0;
        return;
    }
    /* Arriba y abajo dentro de la solapa; a los costados, la solapa abierta
       que sigue o la de antes */
    int tab = wp_act(p->wp_sel) - 1, row = p->wp_sel - k_wp_first[tab], rows = wp_rows(tab);
    if (pi->nav_up)
        row = (row + rows - 1) % rows;
    if (pi->nav_down)
        row = (row + 1) % rows;
    for (int t = 1; t < WP_ACTS && (pi->nav_left || pi->nav_right); t++) {
        int nt = (tab + (pi->nav_right ? t : WP_ACTS - t)) % WP_ACTS;
        if (wp_act_open(p, nt + 1)) {
            tab = nt;
            break;
        }
    }
    rows = wp_rows(tab);
    p->wp_sel = k_wp_first[tab] + (row < rows ? row : rows - 1);
    if (!pi->action)
        return;
    int k = p->wp_sel, act = wp_act(k);
    if (!((p->waypoints >> k) & 1) || (act == s_act && world_waypoint_zone(k) < 0)) {
        show_message(p, "Todavía no lo activaste");
        return;
    }
    p->wp_open = 0;
    if (k == waypoint_near(p))
        return;
    if (act != s_act) /* el waypoint es del otro acto: viajan los dos */
        act_switch(act);
    zone_load(world_waypoint_zone(k)); /* si estaba borrada, se arma */
    if (s_wp_unit[k] < 0)
        return;
    float x, y;
    units_position(s_wp_unit[k], 1.0f, &x, &y);
    p->x = x + 2.0f;
    p->y = y + 2.0f;
    find_free_spot(&p->x, &p->y);
    p->prev_x = p->x;
    p->prev_y = p->y;
    audio_play_name("object_waypoint_open", p->x, p->y);
}

static void close_ui(Player *p)
{
    /* Hratli, después de presentarse en el muelle, se va a su fragua (A3Q0).
       shortcut: aparece allá en vez de caminar */
    int id;
    float x, y;
    if (p->dialog_npc >= 0 && strcmp(units_id(p->dialog_npc), "hratli") == 0 && !hratli_moved() &&
        units_find_all("HratliEnd", &id, 1) == 1) {
        for (int i = 0; i < s_num_players; i++)
            s_pl[i].a3_flags |= A3F_HRATLI;
        units_position(id, 1.0f, &x, &y);
        units_place(p->dialog_npc, x, y);
    }
    a5_dialog_closed(p, p->dialog_npc); /* Tyrael en la Cámara: el portal del final */
    p->dialog_npc = -1;
    p->shop_open = p->gambling = 0;
    p->hire_open = 0;
    p->imbue_open = 0;
    if (s_voice_owner == (int)(p - s_pl)) { /* se calla al cerrar */
        audio_voice_stop();
        s_voice_owner = -1;
    }
}

static void dialog_tick(Player *p, const PlayerInput *pi)
{
    const char *npc = units_id(p->dialog_npc);
    units_talk(p->dialog_npc, p->x, p->y);
    p->talk_ticks++;
    int opts[8], n = npc_options(p, npc, opts);
    if (p->dialog_sel >= n)
        p->dialog_sel = n - 1;
    if (pi->nav_up)
        p->dialog_sel = (p->dialog_sel + n - 1) % n;
    if (pi->nav_down)
        p->dialog_sel = (p->dialog_sel + 1) % n;
    if (pi->back || pi->inventory) {
        close_ui(p);
        return;
    }
    if (!pi->action)
        return;
    switch (opts[p->dialog_sel]) {
    case OPT_TALK:
        npc_talk(p, npc);
        break;
    case OPT_TRADE:
        p->shop_open = 1;
        p->gambling = p->shop_selling = 0;
        p->shop_tab = SHOP_ARMOR;
        p->shop_sel = 0;
        break;
    case OPT_GAMBLE: /* una lista nueva cada vez que se abre, según su nivel */
        p->gamble_count = 0;
        for (int k = 0; k < GAMBLE_ITEMS; k++)
            p->gamble_count += items_gamble_roll(k, p->level_num, &p->gamble[p->gamble_count]);
        p->shop_open = p->gambling = 1;
        p->shop_sel = p->shop_selling = 0;
        break;
    case OPT_HIRE:
        merc_offers(p);
        p->hire_open = 1;
        p->hire_sel = 0;
        break;
    case OPT_REVIVE:
        merc_revive(p);
        break;
    case OPT_RESPEC: /* la de Akara (1.13): todos los puntos de nuevo, una vez por dificultad */
        player_respec(p);
        p->respec_done = 1;
        close_ui(p);
        save_character(p);
        break;
    case OPT_IMBUE:
    case OPT_SOCKET:
    case OPT_PERSONAL: /* (la misma lista: imbuir, engarzar o personalizar) */
        p->imbue_open = 1;
        p->imbue_sel = 0;
        p->imbue_mode = opts[p->dialog_sel] == OPT_SOCKET ? 1 : opts[p->dialog_sel] == OPT_PERSONAL ? 2 : 0;
        break;
    case OPT_TRAVEL: { /* la caravana de Warriv (al este o de vuelta al oeste) y el barco de Meshif */
        int meshif = strcmp(units_id(p->dialog_npc), "meshif1") == 0 ? 3 : strcmp(units_id(p->dialog_npc), "meshif2") == 0 ? 2 : 0;
        close_ui(p);
        /* La primera vez al este, la cinemática del fin del acto, como
           QUESTS_ActChange (A1COMPLETED y A2COMPLETED) */
        if (meshif) { /* a Kurast, o de vuelta a Lut Gholein */
            if (meshif == 3 && party_lacks(offsetof(Player, a2_flags), A2F_SAILED))
                queue_movie("act03start");
            if (meshif == 3)
                for (int i = 0; i < s_num_players; i++)
                    s_pl[i].a2_flags |= A2F_SAILED;
            act_switch(meshif);
            break;
        }
        if (s_act == 1) {
            for (int i = 0; i < s_num_players; i++)
                if (s_pl[i].andy_q < 3) {
                    queue_movie("act02start");
                    break;
                }
            party_raise(offsetof(Player, andy_q), 3);
        }
        act_switch(s_act == 1 ? 2 : 1);
        break;
    }
    default:
        close_ui(p);
        break;
    }
}

/* Las solapas (en la última, Vender, el cursor va a la mochila del inventario) */
static const char *const k_shop_tabs[SHOP_TABS] = { "Armadura", "Armas", "Varios", "Vender" };

static int shop_tab_has(int tab, const ItemType *t)
{
    return tab == SHOP_ARMOR ? t->kind == ITEM_ARMOR : tab == SHOP_WEAPONS ? t->kind == ITEM_WEAPON
                                                                          : t->kind == ITEM_MISC;
}

/* Objetos que se ven en la solapa: punteros a la mercadería (o a lo de la apuesta) */
static int shop_list(Player *p, Item **out, int max)
{
    Shop *sh = shop_of(units_id(p->dialog_npc));
    int n = 0;
    if (p->gambling) {
        for (int i = 0; i < p->gamble_count && n < max; i++)
            out[n++] = &p->gamble[i];
    } else if (sh) {
        for (int i = 0; i < sh->count && n < max; i++)
            if (shop_tab_has(p->shop_tab, sh->stock[i].type))
                out[n++] = &sh->stock[i];
    }
    return n;
}

/* Grilla de 10x10 de la tienda (vendorpanellayout.json): por columnas */
#define SHOP_COLS 10
#define SHOP_ROWS 10
static void shop_layout(Item *const *items, int n, int *col, int *row)
{
    uint8_t used[SHOP_ROWS][SHOP_COLS] = { { 0 } };
    for (int i = 0; i < n; i++) {
        const ItemType *t = items[i]->type;
        col[i] = row[i] = -1;
        for (int c = 0; c + t->inv_w <= SHOP_COLS && col[i] < 0; c++)
            for (int r = 0; r + t->inv_h <= SHOP_ROWS && col[i] < 0; r++) {
                int free = 1;
                for (int a = 0; a < t->inv_h && free; a++)
                    for (int b = 0; b < t->inv_w && free; b++)
                        free = !used[r + a][c + b];
                if (!free)
                    continue;
                for (int a = 0; a < t->inv_h; a++)
                    for (int b = 0; b < t->inv_w; b++)
                        used[r + a][c + b] = 1;
                col[i] = c;
                row[i] = r;
            }
    }
}

typedef struct {
    int px, py;
    Item *const *items;
    const int *col, *row;
} ShopCtx;

static void shop_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h)
{
    const ShopCtx *c = ctx;
    const ItemType *t = c->items[i]->type;
    *x = c->px + 16 + (c->col[i] < 0 ? 0 : c->col[i]) * CELL;
    *y = c->py + 63 + (c->row[i] < 0 ? 0 : c->row[i]) * CELL;
    *w = c->col[i] < 0 ? 0 : t->inv_w * CELL;
    *h = t->inv_h * CELL;
}

#define SHOP_LIST_MAX (SHOP_MAX + BAG_MAX)

/* Dónde va el panel de la tienda: con un solo jugador a la izquierda, con el
   inventario a la derecha (como el original y el arcón); con dos, en su
   mitad (y mientras vende se ve el inventario en su lugar) */
static int shop_px(const Player *p)
{
    return s_num_local == 1 ? 0 : panel_x(p);
}

/* Vendiendo, el cursor recorre las casillas de la mochila */
static void sell_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h)
{
    inv_entry_rect((const Player *)ctx, INV_CELL0 + i, x, y, w, h);
}

/* La casilla siguiente en esa dirección, de objeto en objeto como en el
   inventario; -1 = pasado el borde */
static int sell_step(const Player *p, int cell, int dx, int dy)
{
    int item = bag_at(p, cell % BAG_W, cell / BAG_W), next = cell;
    for (;;) {
        int after = nav_nearest(BAG_CELLS, next, dx, dy, sell_rect_cb, p);
        if (after == next)
            return -1;
        next = after;
        if (item < 0 || bag_at(p, next % BAG_W, next / BAG_W) != item)
            return next;
    }
}

/* Otra solapa: las de la mercadería, o Vender (el cursor va a la mochila: al
   primer objeto que se puede vender si no estaba ahí) */
static void shop_set_tab(Player *p, int tab)
{
    tab = (tab + SHOP_TABS) % SHOP_TABS;
    p->shop_selling = tab == SHOP_SELL;
    if (!p->shop_selling) {
        if (tab != p->shop_tab)
            p->shop_sel = 0;
        p->shop_tab = tab;
        return;
    }
    if (p->inv_sel >= INV_CELL0 && p->inv_sel < INV_BELT0)
        return;
    p->inv_sel = INV_CELL0;
    for (int c = 0; c < BAG_CELLS; c++) {
        int k = bag_at(p, c % BAG_W, c / BAG_W);
        if (k >= 0 && !is_quest_item(p->bag[k].type)) {
            p->inv_sel = INV_CELL0 + c;
            break;
        }
    }
}

/* Cuántos faltan para llenar una pila de ese tipo en la mochila (la primera
   que tenga lugar, o una nueva); 0 = no se apila */
static int shop_fill_count(const Player *p, const ItemType *t)
{
    int cap = stack_cap(t);
    if (cap <= 0 || is_potion(t))
        return 0;
    for (int k = 0; k < p->bag_count; k++)
        if (p->bag[k].type == t && stack_count(&p->bag[k]) < cap)
            return cap - stack_count(&p->bag[k]);
    return cap;
}

/* Vendiendo: la cruceta recorre la mochila (pasado el borde izquierdo vuelve
   a la mercadería) y A vende lo elegido, que el vendedor pone a la venta */
static void sell_tick(Player *p, Shop *sh, const PlayerInput *pi, int dx, int dy)
{
    int cell = p->inv_sel - INV_CELL0;
    if (cell < 0 || cell >= BAG_CELLS)
        cell = 0;
    if (dx || dy) {
        int next = sell_step(p, cell, dx, dy);
        if (next < 0 && dx < 0) {
            shop_set_tab(p, p->shop_tab);
            return;
        }
        if (next >= 0)
            cell = next;
    }
    p->inv_sel = INV_CELL0 + cell;
    int k = bag_at(p, cell % BAG_W, cell / BAG_W);
    if (!pi->action || k < 0)
        return;
    Item it = p->bag[k];
    if (is_quest_item(it.type)) {
        show_message(p, "Eso no se vende");
        return;
    }
    p->gold += items_price(&it, 1);
    audio_play_name("item_gold", AUDIO_NOPOS, AUDIO_NOPOS);
    bag_remove(p, k);
    dclone_sold(p, &it); /* (una Piedra de Jordán en Infierno: el Diablo clon) */
    if (sh->count < SHOP_MAX && !shop_unlimited(it.type))
        sh->stock[sh->count++] = it;
}

static void shop_tick(Player *p, const PlayerInput *pi)
{
    units_talk(p->dialog_npc, p->x, p->y);
    Shop *sh = shop_of(units_id(p->dialog_npc));
    if (pi->back || (pi->inventory && p->gambling) || (!sh && !p->gambling)) {
        p->shop_open = p->gambling = p->shop_selling = 0; /* vuelve al menú del NPC */
        return;
    }
    if (p->shop_tab < 0 || p->shop_tab >= SHOP_SELL)
        p->shop_tab = 0;
    if (!p->gambling) { /* LB / RB: la solapa de al lado; Select: de comprar a vender y al revés */
        if (pi->potion_life || pi->automap) {
            shop_set_tab(p, (p->shop_selling ? SHOP_SELL : p->shop_tab) + (pi->automap ? 1 : -1));
            return;
        }
        if (pi->inventory) {
            shop_set_tab(p, p->shop_selling ? p->shop_tab : SHOP_SELL);
            return;
        }
    }
    int dx = pi->nav_right - pi->nav_left, dy = pi->nav_down - pi->nav_up;
    if (p->shop_selling) {
        sell_tick(p, sh, pi, dx, dy);
        return;
    }
    Item *items[SHOP_LIST_MAX];
    int col[SHOP_LIST_MAX], row[SHOP_LIST_MAX];
    int n = shop_list(p, items, SHOP_LIST_MAX);
    shop_layout(items, n, col, row);
    if (p->shop_sel >= n)
        p->shop_sel = n > 0 ? n - 1 : 0;

    if (dx || dy) {
        ShopCtx ctx = { shop_px(p), PANEL_Y, items, col, row };
        int next = n > 0 ? nav_nearest(n, p->shop_sel, dx, dy, shop_rect_cb, &ctx) : 0;
        if ((n == 0 || next == p->shop_sel) && dx && !p->gambling) { /* pasado el borde: otra solapa (después de Varios, Vender) */
            shop_set_tab(p, p->shop_tab + dx);
            return;
        }
        p->shop_sel = next;
    }
    /* X sobre los Pergaminos de Portal: los que faltan para llenar una pila
       (comprarlos de a uno con el control eran veinte veces A) */
    if (pi->drop && n > 0 && !p->gambling && strcmp(items[p->shop_sel]->type->code, "tsc") == 0) {
        Item one = *items[p->shop_sel];
        int want = shop_fill_count(p, one.type), price = items_price(&one, 0), got = 0;
        while (got < want && p->gold >= price && bag_add(p, &one)) {
            p->gold -= price;
            got++;
        }
        if (got)
            audio_play(one.type->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
        else if (p->gold < price)
            show_message(p, "No tenés suficiente oro");
        else
            inventory_full(p);
        return;
    }
    if (!pi->action || n == 0)
        return;

    Item it = *items[p->shop_sel];
    if (p->gambling) { /* recién al comprarlo se sabe qué salió */
        int price = items_gamble_price(it.type, p->level_num);
        if (p->gold < price) {
            show_message(p, "No tenés suficiente oro");
        } else if (!bag_add(p, &it)) {
            inventory_full(p);
        } else {
            p->gold -= price;
            audio_play(it.type->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
            int k = (int)(items[p->shop_sel] - p->gamble);
            p->gamble[k] = p->gamble[--p->gamble_count];
            char label[64], msg[96];
            snprintf(msg, sizeof msg, tr("Salió: %s"), items_label(&it, label, sizeof label));
            show_message(p, msg);
        }
        if (p->shop_sel >= p->gamble_count)
            p->shop_sel = p->gamble_count > 0 ? p->gamble_count - 1 : 0;
        return;
    }
    int price = items_price(&it, 0);
    if (p->gold < price) {
        show_message(p, "No tenés suficiente oro");
    } else if (!bag_add(p, &it)) {
        inventory_full(p);
    } else {
        p->gold -= price;
        audio_play(it.type->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
        /* Las pociones, las llaves y los portales no se acaban; el resto
           sale de la mercadería */
        if (!shop_unlimited(it.type)) {
            int k = (int)(items[p->shop_sel] - sh->stock);
            sh->stock[k] = sh->stock[--sh->count];
        }
    }
    if (p->shop_sel >= shop_list(p, items, SHOP_LIST_MAX))
        p->shop_sel = p->shop_sel > 0 ? p->shop_sel - 1 : 0;
}

static void tick_potions(Player *p)
{
    if (p->regen_life_ticks > 0) {
        p->regen_life_ticks--;
        p->regen_life_acc += p->regen_life;
        while (p->regen_life_acc >= 1.0f) {
            p->regen_life_acc -= 1.0f;
            if (p->hp < p->hp_max)
                p->hp++;
        }
    }
    if (p->regen_mana_ticks > 0) {
        p->regen_mana_ticks--;
        p->regen_mana_acc += p->regen_mana;
        while (p->regen_mana_acc >= 1.0f) {
            p->regen_mana_acc -= 1.0f;
            if (p->mana < p->mana_max)
                p->mana++;
        }
    }
    /* Regeneración de vida de los objetos: como el original, N/256 de
       vida por cuadro de 25 por segundo */
    if (p->bonus[STAT_REGEN] > 0 && p->state < PS_DYING && p->hp < p->hp_max) {
        p->replenish_acc += p->bonus[STAT_REGEN] / 256.0f;
        while (p->replenish_acc >= 1.0f) {
            p->replenish_acc -= 1.0f;
            if (p->hp < p->hp_max)
                p->hp++;
        }
    }
    /* El maná se llena solo en ManaRegen segundos (charstats.txt) */
    if (p->state < PS_DYING && p->mana < p->mana_max && !p->aura_nomana) {
        p->mana_acc += p->mana_max / (s_classes[p->cls].mana_regen * TICK_RATE) *
                       (100 + p->sk_mana_regen + p->au_mana_regen - (p->drain_ticks > 0 ? 24 * p->drain_lvl : 0)) / 100.0f;
        while (p->mana_acc >= 1.0f && p->mana < p->mana_max) {
            p->mana_acc -= 1.0f;
            p->mana++;
        }
    }
    if (p->shout_ticks > 0 && --p->shout_ticks == 0)
        p->shout_pct = 0;
    if (p->antidote_ticks > 0)
        p->antidote_ticks--;
    if (p->amp_ticks > 0)
        p->amp_ticks--;
    if (p->curse_ticks > 0 && --p->curse_ticks == 0)
        p->curse = A4C_NONE;
    if (p->drain_ticks > 0)
        p->drain_ticks--;
    if (p->thaw_ticks > 0)
        p->thaw_ticks--;
    if (p->chill_ticks > 0)
        p->chill_ticks--;
    if (p->poison_ticks > 0 && p->state < PS_DYING) {
        p->poison_ticks--;
        p->poison_acc += p->poison_rate;
        while (p->poison_acc >= 1.0f && p->hp > 0) {
            p->poison_acc -= 1.0f;
            p->hp--;
        }
        if (p->hp <= 0 && s_god) {
            p->hp = 1;
            s_god_saves[p - s_pl]++;
        }
        if (p->hp <= 0) {
            p->hp = 0;
            set_player_state(p, PS_DYING, MODE_DT);
        }
    }
    if (p->stamina_boost_ticks > 0)
        p->stamina_boost_ticks--;
    if (p->shrine_ticks > 0 && --p->shrine_ticks == 0)
        refresh_stats(p); /* se terminó el santuario */
    if (p->message_ticks > 0)
        p->message_ticks--;
}

/* Lo que suelta un monstruo al morir, desparramado alrededor. Las armas y
   armaduras pueden salir mágicas (más seguido de los únicos y campeones, y
   con la "búsqueda de magia" del que lo mató). */
static void drop_loot(int monster, const Player *killer)
{
    Item drops[6];
    /* (el Rey Vaca y su escolta son "cowking" en monstats, con BaseId hellbovine) */
    const char *mid = units_id(monster);
    items_drop_from_bovine(strcmp(mid, "hellbovine") == 0 || strcmp(mid, "cowking") == 0 || strcmp(mid, "The Cow King") == 0);
    /* Con más jugadores (el original): los vivos en el mismo nivel que el que lo mató cuentan enteros,
       los demás la mitad; no más que los que había cuando salió */
    int party = 0;
    for (int i = 0; i < s_num_players; i++)
        party += killer && s_pl[i].state < PS_DYING && s_pl[i].level == killer->level;
    party = party > 0 ? party : 1;
    int drop_players = (s_num_players - party) / 2 + party, cap = units_player_count_of(monster);
    items_set_drop_players(drop_players < cap ? drop_players : cap);
    const char *tc = units_treasure_class(monster);
    int lvl = units_level(monster);
    if (s_eco) /* el Eco: lo que cae es del nivel de los personajes */
        eco_loot(&tc, &lvl);
    int n = items_roll_drop(tc, lvl, killer ? killer->bonus[STAT_MAGIC_FIND] : 0, drops, 6);
    items_set_drop_players(1);
    items_drop_from_bovine(0);
    float x, y;
    units_position(monster, 1.0f, &x, &y);
    for (int i = 0; i < n; i++) {
        float dx = (float)((int)(rnd() % 30) - 15) / 10.0f, dy = (float)((int)(rnd() % 30) - 15) / 10.0f;
        float px = x + dx, py = y + dy;
        if (map_blocked(&s_map, (int)px, (int)py)) {
            px = x;
            py = y;
        }
        items_ground_add(&drops[i], px, py);
    }
}

static int energy_shield(Player *p, int dmg);

/* Bloqueo con escudo, como UNITS_GetBlockRate del original: (el bloqueo del
   escudo + el de la clase, BlockFactor + el del Escudo sagrado) * (destreza
   - 15) / (nivel * 2), hasta 75%; corriendo, un tercio
   (SUNITDMG_ApplyBlockOrDodge). Sin escudo (dos armas, carcaj) no bloquea. */
static int player_block_chance(const Player *p)
{
    const ItemType *sh = p->equip[SLOT_SHIELD].type, *w = p->equip[SLOT_WEAPON].type;
    int lvl = p->level_num > 1 ? p->level_num : 1;
    if (p->state < PS_DYING && p->sk_wblock > 0 && w && sh && sh->kind == ITEM_WEAPON && items_type_is(w, "h2h") &&
        items_type_is(sh, "h2h")) { /* Bloqueo con armas: con dos garras */
        int chance = p->sk_wblock * (player_dex(p) - 15) / (2 * lvl);
        chance = chance > 75 ? 75 : chance;
        return p->running ? chance / 3 : chance > 0 ? chance : 0;
    }
    if (!sh || sh->kind != ITEM_ARMOR || p->state >= PS_DYING)
        return 0;
    int chance = (sh->block + items_stat(&p->equip[SLOT_SHIELD], STAT_BLOCK) + s_classes[p->cls].block_factor +
                  (p->hs_ticks > 0 ? p->hs_block : 0)) * (player_dex(p) - 15) / (2 * lvl);
    if (chance > 75)
        chance = 75;
    if (p->running)
        chance /= 3;
    return chance > 0 ? chance : 0;
}

static int s_blocks[MAX_PLAYERS]; /* bloqueos de cada jugador (para D2_AURA_DEBUG) */

/* Bloqueó: la animación y el ruido, como el original (no más de una cada 15 cuadros) */
static void player_blocked_hit(Player *p, int missile)
{
    s_blocks[p - s_pl]++;
    if (s_ticks - p->last_block <= 15 && p->last_block)
        return;
    p->last_block = s_ticks ? s_ticks : 1;
    audio_play_name(missile ? "block_arrow_1" : "block_weapon_1", p->x, p->y);
    int busy = p->state == PS_ATTACK && (p->seq || p->charge_ticks > 0); /* Torbellino, Salto, Carga: no se cortan */
    if (!busy && p->state < PS_DYING && player_anim(p, MODE_BL)->ok)
        set_player_state(p, PS_HIT, MODE_BL);
}

static int bone_armor_absorb(Player *p, int dmg);

static void take_damage(Player *p, int dmg, int missile)
{
    if (dmg <= 0 || p->state >= PS_DYING || s_god == 1)
        return;
    /* La Amazona: quieta o atacando esquiva los golpes (Esquivar) y los
       proyectiles (Evitar); caminando, los dos (Evadir) */
    int still = p->state == PS_ATTACK || (p->state == PS_NORMAL && !p->moving);
    int dodge = still ? (missile ? p->sk_avoid : p->sk_dodge) : p->moving ? p->sk_evade : 0;
    if (dodge > 0 && !p->seq && (int)(rnd() % 100) < dodge) {
        if (still && player_anim(p, MODE_S1)->ok)
            set_player_state(p, PS_HIT, MODE_S1);
        return;
    }
    if (p->bonus[STAT_RED_DMG_PCT] > 0) /* "% de daño reducido", hasta la mitad como el original */
        dmg = dmg * (100 - (p->bonus[STAT_RED_DMG_PCT] > 50 ? 50 : p->bonus[STAT_RED_DMG_PCT])) / 100;
    if (p->bonus[STAT_RED_DMG] > 0) /* "reduce en N el daño", sin bajar de 1 */
        dmg = dmg - p->bonus[STAT_RED_DMG] < 1 ? 1 : dmg - p->bonus[STAT_RED_DMG];
    if (p->bonus[STAT_DMG_TO_MANA] > 0) { /* "% del daño recibido va al maná" */
        p->mana += dmg * p->bonus[STAT_DMG_TO_MANA] / 100;
        if (p->mana > p->mana_max)
            p->mana = p->mana_max;
    }
    if (p->cls == 7) /* el Brujo: el Resguardo psíquico y el Juramento de sangre */
        dmg = war_take_damage(p, dmg);
    if (!missile) /* el Nigromante: la Armadura de hueso se lleva los golpes */
        dmg = bone_armor_absorb(p, dmg);
    if (dmg <= 0)
        return;
    dmg = energy_shield(p, dmg); /* la Hechicera: el maná se lleva una parte */
    if (dmg <= 0)
        return;
    p->hp -= dmg;
    if (p->hp <= 0 && s_god) { /* la demostración: no muere */
        p->hp = 1;
        s_god_saves[p - s_pl]++;
    }
    if (p->hp <= 0) {
        p->hp = 0;
        set_player_state(p, PS_DYING, MODE_DT);
        return;
    }
    /* Como en el original: si el golpe saca 1/12 de la vida o más, se
       resiente; el Torbellino, los Saltos y Concentración no se cortan */
    int kind = p->state == PS_ATTACK && p->attack_skill >= 0 ? skills_get(p->cls, p->attack_skill)->kind : SK_NONE;
    if (kind == SK_WHIRLWIND || kind == SK_LEAP || kind == SK_LEAP_ATTACK || kind == SK_CONCENTRATE)
        return;
    if (dmg * 12 >= p->hp_max && player_anim(p, MODE_GH)->ok)
        set_player_state(p, PS_HIT, MODE_GH);
}

static void face_unit(Player *p, int unit)
{
    float ux, uy;
    units_position(unit, 1.0f, &ux, &uy);
    float dx = ux - p->x, dy = uy - p->y;
    if (dx * dx + dy * dy > 0.01f)
        p->sector = sector_from_screen(dx - dy, (dx + dy) * 0.5f);
}

/* Ataque cuerpo a cuerpo, normal (skill = -1) o con una habilidad */
static void start_attack(Player *p, int skill)
{
    p->target = units_find_melee(p->x, p->y, MELEE_RANGE);
    if (p->target >= 0)
        face_unit(p, p->target);
    p->attack_skill = skill;
    p->attack_kind = ATK_MELEE;
    /* Como en el original, el ataque (y las habilidades que lo usan, como
       Porrazo) alterna entre las dos animaciones; las que tienen secuencia
       (Estocada, Empalar...) van por start_seq_skill */
    int a2 = (rnd() & 1) && player_anim(p, MODE_A2)->ok;
    set_player_state(p, PS_ATTACK, a2 ? MODE_A2 : MODE_A1);
    p->attack_skill = skill;
    p->attack_kind = ATK_MELEE;
    p->hits_total = 1;
    p->hits_done = 0;
    p->hand = 0;
    /* Las habilidades con su propio sonido (Estocada, Empalar) no suman el del arma */
    const SkillDef *s = skill >= 0 ? skills_get(p->cls, skill) : NULL;
    play_skill_sound(p, s);
    if (!s || s->weapon_sound || (s->st_sound < 0 && s->st_sound_class < 0))
        audio_play_name(k_swing_sounds[weapon_hit_class(p)], p->x, p->y);
}

/* ---- Ataques a distancia ---- */

#define AIM_RANGE          14.0f /* apunta solo al monstruo más cercano hasta esta distancia */
/* Contra los proyectiles los jugadores ocupan una cruz (COLLISION_UNIT_SIZE_SMALL
   del original); lo que toca cada proyectil depende de su Size (missiles_touches) */
#define PLAYER_FOOT   2
#define MISSILE_CELLS 16

/* Munición del ataque: el carcaj si dispara, el arma si la lanza */
static Item *ammo_item(Player *p, int kind)
{
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    if (kind == ATK_THROW)
        return items_can_throw(w) ? &p->equip[SLOT_WEAPON] : NULL;
    int need = items_ammo_for(w);
    const ItemType *q = p->equip[SLOT_SHIELD].type;
    return need && q && q->ammo == need ? &p->equip[SLOT_SHIELD] : NULL;
}

/* Gasta una flecha, saeta o jabalina. Si se termina, pone otra igual de la
   mochila si hay. */
static void consume_ammo(Player *p, int kind)
{
    Item *ammo = ammo_item(p, kind);
    if (!ammo || --ammo->amount > 0)
        return;
    const ItemType *t = ammo->type;
    memset(ammo, 0, sizeof *ammo);
    for (int i = 0; i < p->bag_count; i++)
        if (p->bag[i].type == t) {
            *ammo = p->bag[i];
            bag_remove(p, i);
            show_message(p, "Sacaste más de la mochila");
            refresh_look(p);
            return;
        }
    show_message(p, kind == ATK_THROW ? "No te queda nada para lanzar" : "Se terminó la munición");
    refresh_look(p);
}

/* Las habilidades de jabalina se lanzan; las de arco se disparan */
static int skill_attack_kind(const SkillDef *s)
{
    switch (s->kind) {
    case SK_POISON_JAVELIN:
    case SK_LIGHTNING_BOLT:
    case SK_PLAGUE_JAVELIN:
    case SK_LIGHTNING_FURY:
        return ATK_THROW;
    case SK_IMMOLATION_ARROW:
    case SK_FREEZING_ARROW:
    case SK_MAGIC_ARROW:
    case SK_FIRE_ARROW:
    case SK_COLD_ARROW:
    case SK_MULTIPLE_SHOT:
    case SK_EXPLODING_ARROW:
    case SK_ICE_ARROW:
    case SK_GUIDED_ARROW:
        return ATK_SHOOT;
    default:
        return ATK_MELEE;
    }
}

/* Arranca un disparo o un lanzamiento: apunta al monstruo más cercano o, si
   no hay ninguno, hacia donde mira. 0 si no tiene munición. */
static int start_ranged(Player *p, int skill, int kind)
{
    const SkillDef *s = skill >= 0 ? skills_get(p->cls, skill) : NULL;
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    if (kind == ATK_SHOOT && !items_ammo_for(w)) {
        show_message(p, "Necesitás un arco o una ballesta");
        return 0;
    }
    int needs_ammo = kind == ATK_THROW || !s || s->decquant;
    Item *ammo = ammo_item(p, kind);
    if (needs_ammo && (!ammo || ammo->amount <= 0)) {
        show_message(p, kind == ATK_THROW ? "No tenés nada para lanzar"
                        : items_ammo_for(w) == 2 ? "Te faltan saetas (carcaj en la otra mano)"
                                                  : "Te faltan flechas (carcaj en la otra mano)");
        return 0;
    }
    p->target = units_find_enemy(p->x, p->y, AIM_RANGE);
    if (p->target >= 0) {
        float ux, uy;
        units_position(p->target, 1.0f, &ux, &uy);
        p->aim_x = ux - p->x;
        p->aim_y = uy - p->y;
        face_unit(p, p->target);
    } else {
        screen_dir_to_world(p->face_x, p->face_y, &p->aim_x, &p->aim_y);
    }
    p->attack_skill = skill;
    p->attack_kind = kind;
    p->hits_total = 1;
    p->hits_done = 0;
    int mode = kind == ATK_THROW && player_anim(p, MODE_TH)->ok ? MODE_TH : MODE_A1;
    set_player_state(p, PS_ATTACK, mode);
    return 1;
}

/* Suelta los proyectiles en el cuadro del disparo */
static void fire_ranged(Player *p)
{
    const SkillDef *s = p->attack_skill >= 0 ? skills_get(p->cls, p->attack_skill) : NULL;
    int lvl = s ? eff_lvl(p, p->attack_skill) : 0;
    SkillCtx ctx = pctx(p);
    int thrown = p->attack_kind == ATK_THROW;
    const ItemType *w = p->equip[SLOT_WEAPON].type;
    if (!w)
        return;

    /* Apunta de nuevo: el monstruo se pudo haber movido */
    float ax = p->aim_x, ay = p->aim_y;
    if (units_alive_enemy(p->target)) {
        float ux, uy;
        units_position(p->target, 1.0f, &ux, &uy);
        ax = ux - p->x;
        ay = uy - p->y;
    }
    int dmin, dmax;
    weapon_damage_pct(p, thrown, thrown ? p->sk_throw_dmg : 0, &dmin, &dmax); /* Dominio de armas arrojadizas */
    const char *name = s && s->missile[0] ? s->missile
                     : thrown ? (items_type_is(w, "taxe") ? "throwaxe" : items_type_is(w, "tkni") ? "throwknife" : "javelin")
                     : items_ammo_for(w) == 2 ? "bolt" : "arrow";
    int n = s ? skill_missiles(s, lvl) : 1;
    int dmg_pct = 0, flags = 0, elem = ELEM_NONE, emin = 0, emax = 0, elen = 0, radius10 = 0, count = 0;
    int ar_pct = (s ? skill_tohit(s, lvl) : 0) + (thrown ? p->sk_throw_ar : 0);
    if (s) {
        if (s->srcdam > 0 && s->srcdam != 128) { /* Disparo múltiple y Rayo: 75% del daño del arma */
            dmin = dmin * s->srcdam / 128;
            dmax = dmax * s->srcdam / 128;
        }
        switch (s->kind) {
        case SK_MAGIC_ARROW:
            skill_phys(s, lvl, &emin, &emax);
            elem = ELEM_MAG;
            break;
        case SK_FIRE_ARROW:
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_FIRE;
            break;
        case SK_ICE_ARROW:
            flags |= MF_FREEZE;
            /* fall through */
        case SK_COLD_ARROW:
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_COLD;
            elen = skill_elen_syn(s, lvl, &ctx);
            break;
        case SK_EXPLODING_ARROW:
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_FIRE;
            flags |= MF_EXPLODE;
            radius10 = 27;
            break;
        case SK_IMMOLATION_ARROW: /* explota y deja el piso ardiendo */
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_FIRE;
            flags |= MF_IMMOLATE;
            radius10 = s->param[1] * 10;
            count = lvl;
            break;
        case SK_FREEZING_ARROW: /* explota y congela a los de alrededor */
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_COLD;
            elen = skill_elen_syn(s, lvl, &ctx);
            flags |= MF_FREEZE_BURST;
            radius10 = s->param[1] * 10;
            break;
        case SK_GUIDED_ARROW:
            flags |= MF_HOMING | MF_ALWAYS_HIT;
            dmg_pct = skill_calc(s, SC_CALC1, lvl, &ctx);
            break;
        case SK_POISON_JAVELIN:
        case SK_PLAGUE_JAVELIN:
            skill_poison_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_POIS;
            elen = skill_elen(s, lvl);
            flags |= s->kind == SK_POISON_JAVELIN ? MF_POISON_TRAIL : MF_POISON_BURST;
            radius10 = s->kind == SK_POISON_JAVELIN ? 15 : 33;
            break;
        case SK_LIGHTNING_BOLT:
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_LTNG;
            flags |= MF_PIERCE;
            /* lightningjavelin (DmgCalc1 dl12, dParam1 100): todo el daño del
               arma pasa a ser de rayo */
            emin += dmin;
            emax += dmax;
            dmin = dmax = 0;
            break;
        case SK_LIGHTNING_FURY: /* al pegar suelta rayos a los de alrededor */
            skill_elem_syn(s, lvl, &ctx, &emin, &emax);
            elem = ELEM_LTNG;
            flags |= MF_FURY;
            count = skill_calc(s, SC_CALC1, lvl, &ctx);
            break;
        default:
            break;
        }
    }
    dmin = dmin * (100 + dmg_pct) / 100;
    dmax = dmax * (100 + dmg_pct) / 100;

    /* Varias flechas salen en abanico */
    float base = atan2f(ay, ax), step = n > 1 ? fminf(0.12f, 1.2f / (float)(n - 1)) : 0.0f;
    for (int k = 0; k < n; k++) {
        float a = base + ((float)k - (float)(n - 1) / 2.0f) * step;
        Missile *mi = missiles_spawn(name, p->x, p->y, cosf(a), sinf(a));
        if (!mi)
            break;
        mi->owner = (int)(p - s_pl);
        mi->ar = player_attack_rating(p) * (100 + ar_pct) / 100;
        mi->level = p->level_num;
        mi->dmin = dmin;
        mi->dmax = dmax;
        mi->elem = elem;
        mi->emin = emin;
        mi->emax = emax;
        mi->elen = elen;
        mi->radius10 = radius10;
        mi->count = count;
        mi->flags |= flags;
    }
    if (thrown || !s || s->decquant)
        consume_ammo(p, p->attack_kind);
}

static int roll(int lo, int hi)
{
    return hi > lo ? lo + (int)(rnd() % (uint32_t)(hi - lo + 1)) : lo;
}

static void award_kill(Player *p, int unit, int exp)
{
    p->mana += p->bonus[STAT_MANA_KILL];
    if (p->mana > p->mana_max)
        p->mana = p->mana_max;
    p->hp += p->bonus[STAT_HEAL_KILL]; /* "vida por cada monstruo muerto" */
    if (p->hp > p->hp_max)
        p->hp = p->hp_max;
    share_exp(p, exp);
    tz_kill(p, unit);
    drop_loot(unit, p);
}

/* Golpe mortal (los objetos) y golpe crítico (las habilidades): duplican el
   daño físico; juntos, como el original, no se suman */
static int crit_chance(const Player *p)
{
    int ds = p->bonus[STAT_DEADLY] > 100 ? 100 : p->bonus[STAT_DEADLY];
    return 100 - (100 - p->sk_crit) * (100 - ds) / 100;
}

/* "El atacante recibe N de daño" (y de rayo) */
static void item_thorns(Player *p, int attacker)
{
    if ((p->bonus[STAT_THORNS] <= 0 && p->bonus[STAT_LTNG_THORNS] <= 0) || !units_alive_enemy(attacker))
        return;
    int dmg = units_resist(attacker, ELEM_NONE, p->bonus[STAT_THORNS]) +
              units_resist(attacker, ELEM_LTNG, p->bonus[STAT_LTNG_THORNS]);
    if (dmg <= 0)
        return;
    int exp = units_damage(attacker, s_one_hit ? 1 << 20 : dmg);
    if (exp > 0)
        award_kill(p, attacker, exp);
}

/* Lo que hacen los objetos con cada golpe que acierta: golpe aplastante,
   heridas abiertas, empujar, frenar, congelar y hacer huir (como el
   original; a distancia el aplastante es la mitad) */
static void item_hit_effects(Player *p, int target, int killed, int ranged)
{
    if (killed || !units_alive_enemy(target))
        return;
    int cb = p->bonus[STAT_CRUSH], ow = p->bonus[STAT_OPEN_WOUNDS], boss = units_is_unique(target);
    if (cb > 0 && (int)(rnd() % 100) < (ranged ? cb / 2 : cb)) { /* un cuarto de la vida que le queda (los jefes, un octavo) */
        int hp, mx;
        units_health(target, &hp, &mx);
        int dmg = units_resist(target, ELEM_NONE, hp / (boss ? 8 : 4));
        if (dmg > 0) {
            int exp = units_damage(target, dmg);
            if (exp > 0) {
                award_kill(p, target, exp);
                return;
            }
        }
    }
    if (ow > 0 && (int)(rnd() % 100) < ow) { /* sangra 8 segundos (lo del original por cuadro, en 1/256) */
        int lvl = p->level_num, per = lvl < 15 ? 9 * lvl + 31 : lvl < 30 ? 18 * lvl - 104 : 27 * lvl - 374;
        int total = per * 8 * 25 / 256;
        units_bleed(target, boss ? total / 2 : total, 8 * TICK_RATE, (int)(p - s_pl));
    }
    if (p->bonus[STAT_KNOCKBACK] > 0 && !boss)
        units_knockback(target, p->x, p->y, 1.0f);
    if (p->bonus[STAT_SLOW] > 0)
        units_slow(target, p->bonus[STAT_SLOW] > 50 ? 50 : p->bonus[STAT_SLOW], 3 * TICK_RATE);
    if (p->bonus[STAT_FREEZE] > 0)
        units_freeze(target, TICK_RATE / 2 + p->bonus[STAT_FREEZE] * TICK_RATE / 4);
    if (p->bonus[STAT_HOWL] > 0 && !boss && (int)(rnd() % 128) < p->bonus[STAT_HOWL]) {
        float ux, uy;
        units_position(target, 1.0f, &ux, &uy);
        units_scare(ux, uy, 0.6f, 3 * TICK_RATE);
    }
}

#define WARCRY_RADIUS 9.0f  /* subtiles */
#define SHOUT_RADIUS  15.0f

static int  sorc_kind(int kind);
static int  sorc_effect(Player *p, const SkillDef *s, int lvl);
static void sorc_missile_end(Missile *mi);
static void sorc_missile_tick(Missile *mi);
static int  spell_resist(int owner, int unit, int elem, int amount);
static int  hit_group_first(int id, int unit);
static void chain_jump(const Missile *mi, int hit);
static void armor_shot(Player *p, int shooter);
static void enemy_area(Missile *mi, float r);
static void sorc_tick(Player *p);
static void cast_effect(Player *p);
static int  pal_kind(int kind);
static void smite_damage(const Player *p, int extra_pct, int *dmin, int *dmax);
static int  vengeance_damage(Player *p, const SkillDef *s, int lvl, int target, float ux, float uy);
static void pal_after_hit(Player *p, const SkillDef *s, int lvl, int target, float ux, float uy, int phys, int killed);
static void pal_tick(Player *p);
static int  nec_kind(int kind);
static void nec_after_hit(Player *p, const SkillDef *s, int lvl, int target, int killed);
static void nec_tick(Player *p);
static int  ass_kind(int kind);
static int  ass_kick(int kind);
static int  ass_strike_pct(const Player *p, const SkillDef *s, int lvl);
static void kick_damage(const Player *p, int extra_pct, int *dmin, int *dmax);
static void ass_after_hit(Player *p, const SkillDef *s, int lvl, int target, int phys, int killed);
static void ass_venom(Player *p, int target, int killed);
static void ass_seq_hit(Player *p, const SkillDef *s, int lvl);
static int  ass_seq_again(Player *p, const SkillDef *s);
static void ass_tick(Player *p);
static int  dru_kind(int kind);
static int  dru_strike_pct(const Player *p, const SkillDef *s, int lvl);
static void dru_after_hit(Player *p, const SkillDef *s, int lvl, int target, int phys, int killed);
static void dru_hit_effects(Player *p, int target, int phys, int killed);
static void dru_seq_hit(Player *p, const SkillDef *s, int lvl);
static void dru_tick(Player *p);
static void dru_spirit_give(Player *o);
static int  cyclone_absorb(Player *p, int dmg);
static void dru_orbit_tick(Missile *mi);
static int  war_kind(int kind);
static int  war_start(Player *p, int skill, int cost);
static int  war_effect(Player *p, const SkillDef *s, int lvl);
static int  war_strike_pct(const Player *p, const SkillDef *s, int lvl);
static void war_after_hit(Player *p, const SkillDef *s, int lvl, int target, int phys, int killed);
static void war_hit(Player *p, int target, int killed);
static void war_missile_hit(Missile *mi, int unit, int killed);
static void war_missile_end(Missile *mi);
static int  war_take_damage(Player *p, int dmg);
static void war_struck(Player *p, int attacker);
static void war_passives(Player *p);
static void war_tick(Player *p);
static void war_world_tick(void);
static void war_clear(Player *p);
static void war_aura_give(Player *o);
static int  war_ias(const Player *p);
static int  war_missile_pct(const Missile *mi);
static int  already_hit(Missile *mi, int id);
#include "skillfx.inc"
#include "skillsor.inc"
#include "skillpal.inc"
#include "skillnec.inc"
#include "skillass.inc"
#include "skilldru.inc"
#include "skillwar.inc"

/* Proyectil de un jugador que alcanza a un monstruo; 0 si no le pegó (sigue de largo) */
static int missile_hit_unit(Missile *mi, int unit)
{
    Player *p = &s_pl[mi->owner];
    if ((mi->flags & MF_HOLY) && !units_is_undead(unit) && !units_is_demon(unit))
        return 0; /* Rayo sagrado: a los demás los atraviesa sin dañarlos */
    if (!(mi->flags & MF_ALWAYS_HIT)) {
        int ac = units_defense(unit), mlvl = units_level(unit);
        int chance = (int)(200LL * mi->ar / (mi->ar + ac > 0 ? mi->ar + ac : 1) * mi->level / (mi->level + mlvl));
        chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
        if ((int)(rnd() % 100) >= chance)
            return 0;
    }
    int phys = roll(mi->dmin, mi->dmax), merc = (mi->flags & MF_MERC) != 0;
    if (!merc && phys > 0 && (int)(rnd() % 100) < crit_chance(p))
        phys *= 2;
    /* Cada parte con su resistencia; las explosiones (también la de Inmolación y
       la congelante, que incluyen al blanco) y el veneno se aplican aparte */
    int dmg = units_resist(unit, ELEM_NONE, phys);
    if (mi->elem != ELEM_POIS && !(mi->flags & (MF_EXPLODE | MF_IMMOLATE | MF_FREEZE_BURST))) {
        int e = roll(mi->emin, mi->emax);
        if ((mi->flags & MF_HAMMER) && (units_is_undead(unit) || units_is_demon(unit)))
            e = e * 3 / 2; /* el Martillo: 50% más a los no muertos y los demonios (missiles.txt) */
        if (!merc && p->cls == 7)
            e = e * war_missile_pct(mi) / 100; /* el Anillo de fuego: más cuanto más lejos llegó */
        dmg += spell_resist(merc ? -1 : mi->owner, unit, mi->elem, e);
    }
    missiles_impact_sound(mi);
    if (s_one_hit)
        dmg = 1 << 20;
    int exp = units_damage(unit, dmg);
    s_shown_target = unit;
    s_shown_ticks = 3 * TICK_RATE;
    const char *prog = missiles_prog_overlay(mi); /* ProgOverlay: el destello del rayo */
    if (prog) {
        float ux, uy;
        units_position(unit, 1.0f, &ux, &uy);
        overlays_add(prog, OV_AT_UNIT, unit, ux, uy, 0);
    }
    if (!merc) { /* lo que roba el equipo es del jugador, no de su mercenaria */
        /* (en Pesadilla la mitad y en Infierno un tercio: LifeStealDivisor y ManaStealDivisor) */
        p->hp += phys * p->bonus[STAT_LIFESTEAL] / 100 / s_dl_life_div;
        p->mana += phys * p->bonus[STAT_MANASTEAL] / 100 / s_dl_mana_div;
    }
    if (p->hp > p->hp_max) p->hp = p->hp_max;
    if (p->mana > p->mana_max) p->mana = p->mana_max;
    if ((mi->flags & MF_FURY) && !merc)
        fury_bolts(mi, unit);
    if (exp <= 0 && (mi->flags & MF_KNOCK) && (rnd() & 1)) /* Roca fundida */
        units_knockback(unit, mi->x - mi->vx * 4.0f, mi->y - mi->vy * 4.0f, 1.5f);
    if (exp <= 0 && (mi->flags & MF_STUN) && mi->count > 0) { /* Remolino, Onda de choque */
        float ux, uy;
        units_stun(unit, mi->count);
        units_position(unit, 1.0f, &ux, &uy);
        overlays_add("stun", OV_AT_UNIT, unit, ux, uy, mi->count);
    }
    if ((mi->flags & MF_CHAIN_LTNG) && !merc)
        chain_jump(mi, unit);
    if (!merc && mi->dmax > 0) /* flechas y jabalinas: lo de los objetos */
        item_hit_effects(p, unit, exp > 0, 1);
    if (!merc && p->cls == 7) /* el Brujo: el maleficio también con sus proyectiles */
        war_missile_hit(mi, unit, exp > 0);
    if (exp > 0) {
        award_kill(p, unit, exp);
    } else if (mi->elem == ELEM_COLD && mi->elen > 0) {
        if (mi->flags & MF_FREEZE)
            units_stun(unit, mi->elen);
        else
            units_chill(unit, mi->elen);
    } else if (mi->elem == ELEM_POIS) {
        if (units_poison(unit, roll(mi->emin, mi->emax), mi->elen, mi->owner))
            poison_fx(unit);
    }
    return 1;
}

/* Un veneno nuevo (SUNITDMG_ApplyPoisonDamage): si el que ya tiene hace
   más por cuadro, sigue ese; si no, va el nuevo con su duración (menos con
   "duración del veneno reducida", la Plen del original) */
static void player_poison(Player *p, float rate, int len)
{
    int cut = p->bonus[STAT_POIS_LEN_RES] > 75 ? 75 : p->bonus[STAT_POIS_LEN_RES];
    len = len * (100 - cut) / 100;
    if (p->antidote_ticks > 0)
        rate *= 0.5f;
    if (p->poison_ticks > 0 && rate < p->poison_rate)
        return;
    p->poison_ticks = len > 1 ? len : 1;
    p->poison_rate = rate;
}

/* Proyectil de un monstruo que alcanza a un jugador */
static int missile_hit_player(Missile *mi, Player *p)
{
    int def = player_defense(p);
    int chance = (int)(200LL * mi->ar / (mi->ar + def > 0 ? mi->ar + def : 1) * mi->level /
                       (mi->level + p->level_num > 0 ? mi->level + p->level_num : 1));
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    if (!(mi->flags & MF_ALWAYS_HIT) && (int)(rnd() % 100) >= chance) /* (lo de área no tira puntería) */
        return 0;
    if (!(mi->flags & MF_ALWAYS_HIT) && p->state < PS_DYING && (int)(rnd() % 100) < player_block_chance(p)) { /* lo paró el escudo */
        player_blocked_hit(p, 1);
        return 1;
    }
    int dmg = roll(mi->dmin, mi->dmax);
    if (p->amp_ticks > 0 || p->curse == A4C_DECREPIFY) /* maldecido: el doble (Decrepitud: la mitad más) */
        dmg = dmg * (100 + (p->amp_ticks > 0 ? 100 : 0) + (p->curse == A4C_DECREPIFY ? 50 : 0)) / 100;
    missiles_impact_sound(mi);
    if (mi->elem == ELEM_POIS && mi->elen > 0) { /* (con la resistencia al veneno) */
        player_poison(p, (float)player_elem_hit(p, ELEM_POIS, roll(mi->emin, mi->emax)) / (float)mi->elen, mi->elen);
    } else {
        int elem = roll(mi->emin, mi->emax);
        if (mi->elem == ELEM_COLD && p->thaw_ticks > 0)
            elem /= 2;
        /* como en el original, hasta 75% (más lo que suben el máximo las auras y los objetos) */
        elem = player_elem_hit(p, mi->elem, elem);
        if (mi->elem == ELEM_FIRE || mi->elem == ELEM_COLD || mi->elem == ELEM_LTNG)
            elem = cyclone_absorb(p, elem); /* la Armadura de ciclón del Druida */
        dmg += elem;
        if (mi->elem == ELEM_COLD && elem > 0) { /* el frío enfría (la resistencia lo acorta; el deshielo, a la mitad) */
            int res = player_resist(p, 1), len = (mi->elen > 0 ? mi->elen : 2 * TICK_RATE) * (100 - (res > 0 ? res : 0)) / 100;
            if (p->thaw_ticks > 0)
                len /= 2;
            if (len > p->chill_ticks)
                p->chill_ticks = len;
        }
    }
    if (missiles_is(mi, "fingermagespider")) { /* el abrazo de los Gropers (fingermagecurse): -24 % de maná por nivel, 30 + 10 por nivel cuadros */
        p->drain_lvl = 3; /* shortcut: el Sk1lvl de los tres Gropers de monstats.txt */
        p->drain_ticks = 30 + 10 * p->drain_lvl;
    }
    take_damage(p, dmg > 0 ? dmg : 1, 1);
    armor_shot(p, mi->source); /* Armadura gélida: le devuelve uno */
    return 1;
}

/* Nubes de veneno: envenenan a los monstruos que las tocan */
static void spawn_cloud(const Missile *src, const char *name, float x, float y)
{
    Missile *c = missiles_spawn(name, x, y, 0.0f, 0.0f);
    if (!c)
        return;
    c->flags |= MF_EFFECT | MF_CLOUD;
    c->owner = src->owner;
    c->emin = src->emin;
    c->emax = src->emax;
    c->elen = src->elen;
    c->radius10 = src->radius10 > 0 ? src->radius10 : 15;
}

/* La poción de gas de las felinas (MISSMODE_SrvHit25_PantherPotGreen): 8
   nubes (HitSubMissile1) que se abren alrededor y envenenan */
static void gas_clouds(const Missile *src)
{
    static const int k_dx[8] = { 0, 2, 2, 2, 0, -2, -2, -2 }, k_dy[8] = { 2, 2, 0, -2, -2, -2, 0, 2 };
    const char *name = missiles_hit_sub(src);
    int elem, emin, emax, dmin, dmax;
    missiles_base_damage(name, &elem, &emin, &emax, &dmin, &dmax);
    missiles_poison_total(name, &emin, &emax);
    /* la velocidad: Param1 << 7 (media unidad de Vel por cada uno) */
    float sp = (float)missiles_param1(name) * 0.5f * missiles_vel_scale();
    for (int k = 0; k < 8; k++) {
        Missile *c = missiles_spawn(name, src->x, src->y, 0.0f, 0.0f);
        if (!c)
            break;
        float l = sqrtf((float)(k_dx[k] * k_dx[k] + k_dy[k] * k_dy[k]));
        c->flags = MF_CLOUD; /* (sin MF_EFFECT: se mueve) */
        c->vx = (float)k_dx[k] / l * sp;
        c->vy = (float)k_dy[k] / l * sp;
        c->owner = -1;
        c->source = src->source;
        c->ar = src->ar;
        c->level = src->level;
        c->elem = ELEM_POIS;
        c->emin = emin;
        c->emax = emax;
        c->elen = missiles_elen(name);
        c->radius10 = 12;
    }
}

/* Fin del proyectil (pegó, chocó o se le acabó el alcance): explosión de
   fuego, nube de veneno y la animación de su explosión */
static void missile_impact(int i)
{
    Missile *mi = missiles_get(i);
    sorc_missile_end(mi);
    war_missile_end(mi);
    if (mi->owner < 0) { /* de un monstruo: lo que hace al pegar o caer */
        if (getenv("D2_MISS_DEBUG") && (missiles_hit_func(mi) == 24 || missiles_hit_func(mi) == 25))
            printf("proyectil: cae la poción en (%.1f, %.1f) a los %d ticks\n", mi->x, mi->y, mi->age);
        if (missiles_hit_func(mi) == 24) /* la poción de fuego de las felinas: a todos alrededor (radio 1) */
            enemy_area(mi, 1.5f);
        else if (missiles_hit_func(mi) == 25 && missiles_hit_sub(mi)) /* la de gas */
            gas_clouds(mi);
        /* lo que se ve al pegar (la explosión de la bola de fuego de los
           vampiros), si no tiene ExplosionMissile y no es lo que ya soltó */
        const char *clt = missiles_clt_hit_sub(mi);
        if (clt && !missiles_explosion(mi) && !(missiles_hit_sub(mi) && strcmp(clt, missiles_hit_sub(mi)) == 0))
            missiles_effect(clt, mi->x, mi->y);
    }
    if (mi->owner >= 0 && mi->owner < s_num_players && (mi->flags & (MF_IMMOLATE | MF_FREEZE_BURST)))
        burst_explode(&s_pl[mi->owner], mi);
    if (mi->owner >= 0 && mi->owner < s_num_players && (mi->flags & MF_EXPLODE)) {
        Player *p = &s_pl[mi->owner];
        float r = mi->radius10 / 10.0f;
        int ids[64], n = units_enemies_around(mi->x, mi->y, r, ids, 64);
        for (int k = 0; k < (n >= 0 ? n : units_count()); k++) {
            int u = n >= 0 ? ids[k] : k;
            float ux, uy;
            if (!units_alive_enemy(u))
                continue;
            units_position(u, 1.0f, &ux, &uy);
            if ((ux - mi->x) * (ux - mi->x) + (uy - mi->y) * (uy - mi->y) > r * r)
                continue;
            int exp = units_damage(u, s_one_hit ? 1 << 20 : spell_resist(mi->owner, u, mi->elem, roll(mi->emin, mi->emax)));
            if (exp > 0)
                award_kill(p, u, exp);
        }
    }
    if ((mi->flags & MF_EXPLODE) && !missiles_explosion(mi) && missiles_chain_next(mi)) {
        /* sin explosión propia, lo que suelta al caer (la bomba de Trauma de fuego) */
        Missile *b = missiles_effect(missiles_chain_next(mi), mi->x, mi->y);
        if (b)
            b->flags |= MF_CHAIN;
    }
    if (mi->owner >= 0 && (mi->flags & MF_POISON_BURST)) {
        float r = mi->radius10 / 20.0f;
        for (int k = 0; k < 6; k++)
            spawn_cloud(mi, "plaguejavcloud", mi->x + r * cosf((float)k * 1.047f), mi->y + r * sinf((float)k * 1.047f));
    }
    missiles_end(i);
}

static int already_hit(Missile *mi, int id)
{
    for (int k = 0; k < mi->num_hit; k++)
        if (mi->hit[k] == id)
            return 1;
    if (mi->num_hit < MISSILE_HITS)
        mi->hit[mi->num_hit++] = id;
    return 0;
}

/* Proyectil de un monstruo que alcanza a una mercenaria */
static int missile_hit_merc(Missile *mi, const Player *owner)
{
    int def = units_defense(owner->merc_unit);
    int chance = (int)(200LL * mi->ar / (mi->ar + def > 0 ? mi->ar + def : 1) * mi->level /
                       (mi->level + owner->merc_level > 0 ? mi->level + owner->merc_level : 1));
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    if (!(mi->flags & MF_ALWAYS_HIT) && (int)(rnd() % 100) >= chance)
        return 0;
    missiles_impact_sound(mi);
    int e = roll(mi->emin, mi->emax), ri = mi->elem == ELEM_FIRE ? 0 : mi->elem == ELEM_COLD ? 1 : mi->elem == ELEM_LTNG ? 2 : -1;
    if (mi->elem == ELEM_POIS && mi->elen > 0 && (mi->flags & MF_CLOUD))
        e = e * 8 / mi->elen; /* una nube le pega cada 8 ticks: el veneno, repartido */
    if (ri >= 0)
        e = e * (100 - units_ally_resist(owner->merc_unit, ri)) / 100;
    units_merc_damage(owner->merc_unit, roll(mi->dmin, mi->dmax) + e);
    return 1;
}

/* Proyectil de un monstruo que alcanza a un esbirro (la Valquiria, los del
   Nigromante y sus muros de hueso) */
static int missile_hit_pet(Missile *mi, int unit)
{
    int def = units_defense(unit), lvl = units_level(unit);
    int chance = (int)(200LL * mi->ar / (mi->ar + def > 0 ? mi->ar + def : 1) * mi->level /
                       (mi->level + lvl > 0 ? mi->level + lvl : 1));
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    if ((int)(rnd() % 100) >= chance)
        return 0;
    missiles_impact_sound(mi);
    int dmg = units_resist(unit, ELEM_NONE, roll(mi->dmin, mi->dmax));
    if (mi->emax > 0)
        dmg += units_resist(unit, mi->elem, roll(mi->emin, mi->emax));
    units_pet_damage(unit, dmg);
    return 1;
}

/* Lo de área de un monstruo (la Ventisca de los sacerdotes, el Meteoro de los
   vampiros): a los jugadores y sus mercenarias a menos de r, sin tirar puntería */
static void enemy_area(Missile *mi, float r)
{
    int flags = mi->flags;
    mi->flags |= MF_ALWAYS_HIT;
    for (int k = 0; k < s_num_players; k++) {
        Player *p = &s_pl[k];
        float dx = p->x - mi->x, dy = p->y - mi->y, mx, my;
        if (p->state < PS_DYING && !(p->level && p->level->town) && dx * dx + dy * dy <= r * r)
            missile_hit_player(mi, p);
        if (units_merc_alive(p->merc_unit)) {
            units_position(p->merc_unit, 1.0f, &mx, &my);
            if ((mx - mi->x) * (mx - mi->x) + (my - mi->y) * (my - mi->y) <= r * r)
                missile_hit_merc(mi, p);
        }
    }
    mi->flags = flags;
}

/* La mercenaria de cada jugador: si murió (se puede resucitar con Kashya) o
   si está por morir ("¡Peligro mortal!") */
static void merc_tick_players(void)
{
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        if (!p->merc_name || p->merc_dead || p->merc_unit < 0)
            continue;
        if (!units_merc_alive(p->merc_unit)) {
            p->merc_dead = 1;
            char msg[48];
            snprintf(msg, sizeof msg, tr("%s murió"), merc_name_of(p->merc_type, p->merc_name));
            show_message(p, msg);
            save_character(p);
            continue;
        }
        int hp, max_hp;
        float mx, my;
        units_merc_health(p->merc_unit, &hp, &max_hp);
        if (hp * 4 < max_hp && !p->merc_warned) {
            p->merc_warned = 1;
            units_position(p->merc_unit, 1.0f, &mx, &my);
            if (MERC_ACT(p->merc_type).danger)
                audio_play_name(MERC_ACT(p->merc_type).danger, mx, my);
        } else if (hp * 2 > max_hp) {
            p->merc_warned = 0;
        }
    }
}

/* El camino de los rayos cargados (PATH_ComputePathChargedBolt): tramos de
   2 subtiles en la dirección principal (la de las 8 más cerca de adonde se
   tiraron) o a 45 grados de un lado u otro, al azar en cada tramo y sin
   acumularse; así se abren en abanico pero siguen hacia allá. Duran hasta
   77 cuadros (SKILLS_MissileInit_ChargedBolt). En `ang` va la dirección
   principal y en (mx, my) dónde empezó el tramo. */
static void charged_step(Missile *mi)
{
    /* -1, 0 o +1 con la secuencia del original (le falta un 0: "arreglarla
       haría que se abran menos") */
    static const int k_turn[32] = { -1, 0, 1, -1, 0, 1, -1, 0, 1, -1, 0, 1, -1, 0, 1, -1,
                                    0,  1, -1, 0, 1, -1, 0, 1, -1, 0, 1, -1, 0, 1, -1, 1 };
    static const int k_dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, k_dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    float sp = sqrtf(mi->vx * mi->vx + mi->vy * mi->vy);
    if (sp <= 0.0f)
        return;
    int turn = 0;
    if (mi->age == 0) {
        mi->ang = (float)(((int)lroundf(atan2f(mi->vy, mi->vx) / (PI_F / 4.0f)) + 8) & 7);
        if (mi->life > 77)
            mi->life = 77;
        turn = 1;
    } else {
        float dx = mi->x - mi->mx, dy = mi->y - mi->my, seg = mi->vx != 0.0f && mi->vy != 0.0f ? 2.83f : 2.0f;
        turn = dx * dx + dy * dy >= seg * seg;
    }
    if (!turn)
        return;
    int d = ((int)mi->ang + k_turn[rnd() & 31]) & 7;
    float l = d & 1 ? 1.41421356f : 1.0f;
    mi->vx = (float)k_dx[d] / l * sp;
    mi->vy = (float)k_dy[d] / l * sp;
    mi->mx = mi->x;
    mi->my = mi->y;
}

/* Los subtiles que recorrió en este tick (los puntos del camino que mira el
   original en MISSMODE_HandleMissileCollision): del que dejó al que llegó,
   sin el de partida salvo en su primer tick, y siempre el de ahora */
static int missile_cells(int i, const Missile *mi, int (*cells)[2])
{
    float px, py;
    missiles_position(i, 0.0f, &px, &py);
    float dx = mi->x - px, dy = mi->y - py, d = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    int steps = (int)ceilf(d * 2.0f), n = 0, lx = (int)floorf(px), ly = (int)floorf(py);
    steps = steps < 1 ? 1 : steps > MISSILE_CELLS ? MISSILE_CELLS : steps;
    if (mi->age <= 1) {
        cells[n][0] = lx;
        cells[n++][1] = ly;
    }
    for (int s = 1; s <= steps && n < MISSILE_CELLS; s++) {
        int cx = (int)floorf(px + dx * (float)s / (float)steps), cy = (int)floorf(py + dy * (float)s / (float)steps);
        if (cx == lx && cy == ly)
            continue;
        cells[n][0] = lx = cx;
        cells[n++][1] = ly = cy;
    }
    if (n == 0) { /* no cambió de subtile */
        cells[0][0] = lx;
        cells[n++][1] = ly;
    }
    return n;
}

static void tick_missiles(void)
{
    int pets[MAX_PET_TARGETS], npets = -1; /* los esbirros (se buscan con el primer proyectil de un monstruo) */
    for (int i = 0; i < missiles_capacity(); i++) {
        Missile *mi = missiles_get(i);
        if (!mi)
            continue;
        /* El Espíritu de hueso de los caballeros y el abrazo de los Gropers: van hacia el jugador más cercano */
        if ((mi->flags & MF_HOMING) && mi->owner < 0) {
            float best = 30.0f, bx = 0.0f, by = 0.0f;
            for (int k = 0; k < s_num_players; k++) {
                const Player *p = &s_pl[k];
                float d = sqrtf((p->x - mi->x) * (p->x - mi->x) + (p->y - mi->y) * (p->y - mi->y));
                if (p->state < PS_DYING && d < best) {
                    best = d;
                    bx = p->x - mi->x;
                    by = p->y - mi->y;
                }
            }
            float sp = sqrtf(mi->vx * mi->vx + mi->vy * mi->vy);
            if (best < 30.0f && best > 0.01f) {
                mi->vx = bx / best * sp;
                mi->vy = by / best * sp;
            }
        }
        /* Flecha guiada: gira hacia el monstruo más cercano */
        if ((mi->flags & MF_HOMING) && mi->owner >= 0) {
            int t = units_find_enemy(mi->x, mi->y, 10.0f);
            if (t >= 0) {
                float ux, uy, sp = sqrtf(mi->vx * mi->vx + mi->vy * mi->vy);
                units_position(t, 1.0f, &ux, &uy);
                float dx = ux - mi->x, dy = uy - mi->y, l = sqrtf(dx * dx + dy * dy);
                if (l > 0.01f) {
                    mi->vx = dx / l * sp;
                    mi->vy = dy / l * sp;
                }
            }
        }
        if (mi->flags & (MF_ORB | MF_MAKER | MF_BLIZZARD)) /* lo que va dejando la Hechicera */
            sorc_missile_tick(mi);
        if (mi->flags & MF_HAMMER) /* el Martillo bendito gira en espiral */
            hammer_tick(mi);
        if (mi->flags & MF_ORBIT) /* lo que vuela alrededor del Druida en el Huracán */
            dru_orbit_tick(mi);
        if ((mi->flags & MF_WOBBLE) && mi->age % 4 == 0) { /* el Tornado va dando vueltas */
            float a = ((float)(rnd() % 100) - 50.0f) / 120.0f, ca = cosf(a), sa = sinf(a);
            float vx = mi->vx * ca - mi->vy * sa, vy = mi->vx * sa + mi->vy * ca;
            mi->vx = vx;
            mi->vy = vy;
        }
        if (mi->flags & MF_CHARGED) /* los rayos cargados: de a tramos, al azar hacia donde iban */
            charged_step(mi);
        int r = missiles_step(i, &s_map);
        mi = missiles_get(i);
        if (!mi)
            continue;
        missiles_trail(i); /* los rayos: el rastro que se ve como un rayo largo */
        if (mi->flags & MF_BURN)
            burn_tick(mi);
        if (mi->flags & MF_CLOUD) {
            float rad = mi->radius10 / 10.0f;
            if (mi->owner < 0 && mi->age % 8 == 0) /* de un monstruo (la Nova venenosa de Mefisto) */
                enemy_area(mi, rad);
            int ids[64], n = mi->owner >= 0 ? units_enemies_around(mi->x, mi->y, rad, ids, 64) : 0;
            if (mi->owner >= 0)
                for (int k = 0; k < (n >= 0 ? n : units_count()); k++) {
                    int u = n >= 0 ? ids[k] : k;
                    float ux, uy;
                    if (!units_alive_enemy(u))
                        continue;
                    units_position(u, 1.0f, &ux, &uy);
                    if ((ux - mi->x) * (ux - mi->x) + (uy - mi->y) * (uy - mi->y) <= rad * rad &&
                        units_poison(u, roll(mi->emin, mi->emax), mi->elen, mi->owner))
                        poison_fx(u);
                }
            if (r)
                missiles_remove(i);
            continue;
        }
        if (mi->flags & MF_EFFECT) {
            if (r && (mi->flags & MF_LAND)) /* la Ventisca y el Meteoro: dañan donde caen */
                sorc_missile_end(mi);
            if (r) {
                /* Los encadenados (el humo y los espíritus de Blood Raven,
                   las piedras que caen) siguen con el que viene */
                char next[32] = "";
                float ex = mi->x, ey = mi->y;
                if ((mi->flags & MF_CHAIN) && missiles_chain_next(mi))
                    snprintf(next, sizeof next, "%s", missiles_chain_next(mi));
                missiles_remove(i);
                Missile *nm = next[0] ? missiles_effect(next, ex, ey) : NULL;
                if (nm)
                    nm->flags |= MF_CHAIN;
            }
            continue;
        }
        if ((mi->flags & MF_POISON_TRAIL) && mi->age % 3 == 0)
            spawn_cloud(mi, "poisonjavcloud", mi->x, mi->y);
        else if ((mi->flags & MF_POISON_BURST) && mi->owner >= 0 && mi->age % 3 == 0)
            missiles_effect("plaguejavcloud", mi->x, mi->y); /* la pestilente: el rastro solo se ve */
        if (r) {
            missile_impact(i);
            continue;
        }
        if (mi->flags & MF_NOHIT) /* solo se ve (las llamas de Infierno, el Orbe) */
            continue;
        int cells[MISSILE_CELLS][2], nc = missile_cells(i, mi, cells), ms = missiles_size(mi);
        if (mi->owner >= 0) {
            if (mi->flags & MF_HEAL) /* Rayo sagrado: cura al compañero */
                holy_heal(mi, cells, nc);
            /* De un jugador: le pega al primer monstruo que toca en el camino
               (de una nova, uno solo por monstruo) */
            int u = units_find_enemy_path(cells, nc, ms, mi->hit, mi->num_hit);
            if (u >= 0 && !already_hit(mi, u) && (!(mi->flags & MF_GROUP) || hit_group_first(mi->group, u)) &&
                missile_hit_unit(mi, u) && !(mi->flags & MF_PIERCE) && !pierce_roll(mi))
                missile_impact(i);
        } else {
            /* De un monstruo: a los jugadores vivos y a sus mercenarias. La
               poción en arco va por el aire: cae donde estaba el blanco. Los
               que no mueren al chocar (CollideKill 0: el aliento de las
               momias, las llamas de los chamanes, el rayo del Consejo) le
               pegan a cada uno una vez y siguen. */
            if (mi->lob_total > 0)
                continue;
            int hit = 0, kill = missiles_collide_kill(mi);
            for (int c = 0; c < nc && !hit; c++) {
                int cx = cells[c][0], cy = cells[c][1];
                for (int k = 0; k < s_num_players && !hit; k++) {
                    Player *p = &s_pl[k];
                    if (p->state >= PS_DYING || (p->level && p->level->town) ||
                        !missiles_touches(ms, cx, cy, p->x, p->y, PLAYER_FOOT) || already_hit(mi, -1 - k))
                        continue;
                    if ((mi->flags & MF_GROUP) && mi->group < 0) /* una nova de un monstruo: a cada jugador le pega uno solo */
                        for (int j = 0; j < missiles_capacity(); j++) {
                            Missile *o = missiles_get(j);
                            if (o && o != mi && o->group == mi->group)
                                already_hit(o, -1 - k);
                        }
                    if (missile_hit_player(mi, p) && kill) {
                        missile_impact(i);
                        hit = 1;
                    }
                }
                for (int k = 0; k < s_num_players && !hit; k++) {
                    const Player *p = &s_pl[k];
                    float mx, my;
                    if (!units_merc_alive(p->merc_unit))
                        continue;
                    units_position(p->merc_unit, 1.0f, &mx, &my);
                    if (!missiles_touches(ms, cx, cy, mx, my, units_foot(p->merc_unit)) || already_hit(mi, -10 - k))
                        continue;
                    if (missile_hit_merc(mi, p) && kill) {
                        missile_impact(i);
                        hit = 1;
                    }
                }
                if (!hit && npets < 0)
                    npets = units_pet_targets(pets, MAX_PET_TARGETS);
                for (int k = 0; k < npets && !hit; k++) {
                    float ux, uy;
                    if (!units_pet_alive(pets[k]))
                        continue;
                    units_position(pets[k], 1.0f, &ux, &uy);
                    if (!missiles_touches(ms, cx, cy, ux, uy, units_foot(pets[k])) || already_hit(mi, -1000 - pets[k]))
                        continue;
                    if (missile_hit_pet(mi, pets[k]) && kill) {
                        missile_impact(i);
                        hit = 1;
                    }
                }
            }
        }
    }
}

/* Un golpe del ataque en curso, con lo que agrega la habilidad */
static void melee_hit(Player *p)
{
    if (p->cls == 7 && p->hits_done > 1 && !units_alive_enemy(p->target)) /* las copias de Hojas reflejadas */
        p->target = units_find_melee(p->x, p->y, MELEE_RANGE);
    melee_strike(p, p->target);
}

/* Buscar poción / Buscar objeto: lo que sale del cadáver */
static void find_on_corpse(Player *p, int corpse, int item)
{
    float x, y;
    units_position(corpse, 1.0f, &x, &y);
    if (item) {
        Item drops[6];
        int n = items_roll_drop(units_treasure_class(corpse), units_level(corpse), p->bonus[STAT_MAGIC_FIND], drops, 6);
        for (int i = 0; i < n; i++)
            items_ground_add(&drops[i], x + 0.4f * (float)(i % 3), y + 0.4f * (float)(i / 3));
        return;
    }
    /* Vida, maná o rejuvenecimiento, más fuertes con el nivel */
    int tier = 1 + p->level_num / 6, r = (int)(rnd() % 100);
    char code[16];
    if (r < 15)
        snprintf(code, sizeof code, "rvs");
    else
        snprintf(code, sizeof code, "%s%d", r < 60 ? "hp" : "mp", tier > 5 ? 5 : tier);
    const ItemType *t = items_find(code);
    if (t) {
        Item it = items_make(t);
        items_ground_add(&it, x, y);
    }
}

/* Efecto de las habilidades que no son ataques, en el cuadro del lanzamiento */
/* La marca del grito (Grito de batalla, Percepción) en los monstruos que alcanzó */
static void mark_cursed(const Player *p, const char *overlay, int ticks)
{
    int ids[64], n = units_enemies_near(p->x, p->y, WARCRY_RADIUS, ids, 64);
    for (int k = 0; k < n; k++) {
        float x, y;
        units_position(ids[k], 1.0f, &x, &y);
        overlays_add(overlay, OV_AT_UNIT, ids[k], x, y, ticks);
    }
}

static void cast_effect(Player *p)
{
    if (p->attack_skill < 0)
        return;
    const SkillDef *s = skills_get(p->cls, p->attack_skill);
    int lvl = eff_lvl(p, p->attack_skill), lo, hi, n = 0;
    SkillCtx c = pctx(p);
#define CALC(k) skill_calc(s, (k), lvl, &c)
    UnitCurse curse;
    memset(&curse, 0, sizeof curse);
    if (s->castoverlay[0]) /* el destello al lanzarla (Percepción, Grito de guerra...) */
        overlays_add(s->castoverlay, OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
    switch (s->kind) {
    case SK_HOWL: /* huyen (calc3 ticks); el radio sale de calc2 */
        n = units_scare(p->x, p->y, (float)CALC(SC_CALC2) * 2.0f / 5.0f, CALC(SC_CALC3));
        missiles_effect("howl", p->x, p->y); /* la onda */
        /* El estado terror: el destello, la marca mientras huyen y su sonido */
        if (mark_status(p->x, p->y, (float)CALC(SC_CALC2) * 2.0f / 5.0f, UNIT_SCARED, "curseterror", "curse_hit",
                        CALC(SC_CALC3)))
            audio_play_name("necromancer_taint", p->x, p->y);
        break;
    case SK_SHOUT:
        for (int i = 0; i < s_num_players; i++) {
            Player *o = &s_pl[i];
            float dx = o->x - p->x, dy = o->y - p->y;
            if (o->state >= PS_DYING || dx * dx + dy * dy > SHOUT_RADIUS * SHOUT_RADIUS)
                continue;
            int pct = CALC(SC_AURASTAT1);
            if (pct >= o->shout_pct)
                o->shout_pct = pct;
            o->shout_ticks = CALC(SC_AURALEN); /* con las sinergias de Órdenes y Comando de batalla */
            overlays_add("shoutstart", OV_AT_PLAYER, i, o->x, o->y, 0);
            overlays_add("shout", OV_AT_PLAYER, i, o->x, o->y, o->shout_ticks); /* mientras dura */
        }
        missiles_effect("shout", p->x, p->y);
        break;
    case SK_BATTLE_ORDERS:
        party_buff(p, SK_BATTLE_ORDERS, CALC(SC_AURASTAT1), CALC(SC_AURALEN), "battleorderscast", "battleorders");
        missiles_effect("battleorders", p->x, p->y);
        break;
    case SK_BATTLE_COMMAND:
        party_buff(p, SK_BATTLE_COMMAND, CALC(SC_AURASTAT1), CALC(SC_AURALEN), "battlecommandcast",
                   "battlecommand");
        missiles_effect("battlecommand", p->x, p->y);
        break;
    case SK_TAUNT:
        curse.ar_pct = CALC(SC_AURASTAT1);
        curse.dmg_pct = CALC(SC_AURASTAT2);
        curse.ticks = 10 * TICK_RATE;
        curse.single = 1;
        if (p->target >= 0) {
            n = units_curse(p->x, p->y, WARCRY_RADIUS, &curse);
            float tx, ty;
            units_position(p->target, 1.0f, &tx, &ty);
            overlays_add("taunt", OV_AT_UNIT, p->target, tx, ty, curse.ticks);
            audio_play_name("barbarian_battlecry_state", tx, ty); /* onsound del estado */
        }
        break;
    case SK_BATTLE_CRY:
        curse.def_pct = CALC(SC_AURASTAT1);
        curse.dmg_pct = CALC(SC_AURASTAT2);
        curse.ticks = CALC(SC_AURALEN);
        n = units_curse(p->x, p->y, WARCRY_RADIUS, &curse);
        missiles_effect("battlecry", p->x, p->y);
        mark_cursed(p, "battlecry", curse.ticks);
        if (n > 0)
            audio_play_name("barbarian_battlecry_state", p->x, p->y); /* onsound del estado */
        break;
    case SK_WAR_CRY: { /* daño a los de alrededor y quedan aturdidos */
        skill_phys_syn(s, lvl, &c, &lo, &hi);
        int stun = CALC(SC_CALC4), ids[64];
        n = units_enemies_near(p->x, p->y, WARCRY_RADIUS, ids, 64);
        for (int k = 0; k < n; k++) {
            float x, y;
            units_position(ids[k], 1.0f, &x, &y);
            int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : units_resist(ids[k], ELEM_NONE, roll(lo, hi)));
            overlays_add("doubledamage1", OV_AT_UNIT, ids[k], x, y, 0); /* ProgOverlay de warcry */
            if (exp > 0) {
                award_kill(p, ids[k], exp);
            } else {
                units_stun(ids[k], stun);
                overlays_add("stun", OV_AT_UNIT, ids[k], x, y, stun);
            }
        }
        missiles_effect("warcry", p->x, p->y);
        break;
    }
    case SK_GRIM_WARD:
        if (p->cast_target >= 0) {
            float x, y;
            units_mark_searched(p->cast_target);
            units_position(p->cast_target, 1.0f, &x, &y);
            ward_spawn(x, y, CALC(SC_CALC1), CALC(SC_CALC2), CALC(SC_AURALEN));
            n = 1;
        }
        break;
    case SK_INNER_SIGHT:
        skill_elem(s, lvl, &lo, &hi);
        curse.def_flat = lo;
        curse.ticks = CALC(SC_AURALEN);
        n = units_curse(p->x, p->y, (float)CALC(SC_AURARANGE) / 2.0f, &curse);
        mark_cursed(p, "innersight", curse.ticks);
        break;
    case SK_SLOW_MISSILES:
        n = units_slow_missiles(p->x, p->y, (float)CALC(SC_AURARANGE) / 2.0f, CALC(SC_AURASTAT1), CALC(SC_AURALEN));
        /* El estado slowmissiles se ve como el de Percepción (overlay1 innersight) */
        mark_status(p->x, p->y, (float)CALC(SC_AURARANGE) / 2.0f, UNIT_SLOWMIS, "innersight", NULL, CALC(SC_AURALEN));
        break;
    case SK_DECOY:
        decoy_spawn(p, p->hp_max * (100 + CALC(SC_CALC1)) / 100, CALC(SC_CALC2));
        n = 1;
        break;
    case SK_VALKYRIE:
        valkyrie_spawn(p, s, lvl);
        n = p->valk_unit >= 0;
        break;
    case SK_FIND_POTION:
    case SK_FIND_ITEM:
        if (p->cast_target >= 0) {
            units_mark_searched(p->cast_target);
            n = (int)(rnd() % 100) < CALC(SC_CALC1);
            if (n) {
                find_on_corpse(p, p->cast_target, s->kind == SK_FIND_ITEM);
                audio_play_name("barbarian_findheart_1", p->x, p->y);
            }
        }
        break;
    default:
        if (sorc_kind(s->kind))
            n = sorc_effect(p, s, lvl);
        else if (pal_kind(s->kind))
            n = pal_effect(p, s, lvl);
        else if (nec_kind(s->kind))
            n = nec_effect(p, s, lvl);
        else if (ass_kind(s->kind))
            n = ass_effect(p, s, lvl);
        else if (dru_kind(s->kind))
            n = dru_effect(p, s, lvl);
        else if (war_kind(s->kind))
            n = war_effect(p, s, lvl);
        break;
    }
#undef CALC
    if (s_debug)
        printf("  J%d usa %s (nivel %d): afecta a %d\n", (int)(p - s_pl) + 1, s->key, lvl, n);
}

/* Usa una habilidad aprendida: gasta el maná y arranca el ataque o el
   lanzamiento. Devuelve 1 si el personaje quedó ocupado. */
static int use_skill_mana(Player *p, int skill)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    if (lvl <= 0 || s->passive || s->kind == SK_NONE)
        return 0;
    if (s->aura) { /* el Paladín: el aura se elige y queda puesta */
        aura_select(p, skill);
        return 0;
    }
    if (!weapon_allows(p, s)) {
        show_message(p, s->weapon_itype[0][0] && !strcmp(s->weapon_itype[0], "shld") ? "Necesitás un escudo"
                                                                                       : "No se puede con esta arma");
        return 0;
    }
    int cost = skill_mana(s, lvl);
    if (p->mana < cost) {
        show_message(p, "No tenés suficiente maná");
        player_say(p, "needmana_1");
        return 0;
    }
    if (sorc_kind(s->kind)) { /* los hechizos de la Hechicera */
        if (!sorc_start(p, skill))
            return 0;
        p->mana -= cost;
        return 1;
    }
    if (pal_kind(s->kind)) /* las del Paladín (gasta el maná si arranca) */
        return pal_start(p, skill, cost);
    if (nec_kind(s->kind)) /* las del Nigromante (gasta el maná si arranca) */
        return nec_start(p, skill, cost);
    if (ass_kind(s->kind)) /* las de la Asesina (gasta el maná si arranca) */
        return ass_start(p, skill, cost);
    if (dru_kind(s->kind)) /* las del Druida (gasta el maná si arranca) */
        return dru_start(p, skill, cost);
    if (war_kind(s->kind)) /* las del Brujo (gasta el maná si arranca) */
        return war_start(p, skill, cost);
    int kind = skill_attack_kind(s);
    if (kind != ATK_MELEE) {
        if (!start_ranged(p, skill, kind))
            return 0;
        p->mana -= cost;
        play_skill_sound(p, s);
        return 1;
    }
    /* Las que tienen secuencia del original (y las que se mueven) */
    switch (s->kind) {
    case SK_JAB:
    case SK_IMPALE:
    case SK_FEND:
    case SK_DOUBLE_SWING:
    case SK_FRENZY:
    case SK_DOUBLE_THROW:
    case SK_LEAP:
    case SK_LEAP_ATTACK:
    case SK_WHIRLWIND:
    case SK_STRAFE:
        if (!start_seq_skill(p, skill))
            return 0;
        p->mana -= cost;
        return 1;
    default:
        break;
    }
    int corpse = -1;
    if (s->kind == SK_FIND_POTION || s->kind == SK_FIND_ITEM || s->kind == SK_GRIM_WARD) {
        corpse = units_find_corpse(p->x, p->y, 3.0f);
        if (corpse < 0) {
            show_message(p, "No hay ningún cadáver cerca");
            return 0;
        }
    }
    p->mana -= cost;
    switch (s->kind) {
    case SK_BASH:
    case SK_STUN:
    case SK_CONCENTRATE:
    case SK_POWER_STRIKE:
    case SK_CHARGED_STRIKE:
    case SK_LIGHTNING_STRIKE:
    case SK_BERSERK:
        start_attack(p, skill);
        return 1;
    default:
        break;
    }
    p->attack_skill = skill;
    p->cast_target = corpse;
    p->target = s->kind == SK_TAUNT ? units_find_enemy(p->x, p->y, WARCRY_RADIUS) : -1;
    if (corpse >= 0)
        face_unit(p, corpse);
    else if (p->target >= 0)
        face_unit(p, p->target);
    p->hits_done = 0;
    play_skill_sound(p, s);
    if (!player_anim(p, MODE_SC)->ok) {
        cast_effect(p);
        return 0;
    }
    set_player_state(p, PS_CAST, MODE_SC);
    return 1;
}

/* Con Blood Mana (la maldición de las súcubos, D2GAME_SKILLS_BloodMana) lo
   que cuesta la habilidad sale de la vida: si no alcanza, se le va la
   maldición, queda con 1 de vida y no la usa; si le quedan menos de 40
   (Param5), se le va */
static int use_skill(Player *p, int skill)
{
    int lvl = eff_lvl(p, skill), cost = lvl > 0 ? skill_mana(skills_get(p->cls, skill), lvl) : 0;
    if (p->curse != A4C_BLOODMANA || cost <= 0)
        return use_skill_mana(p, skill);
    if (p->hp < cost) {
        p->curse = A4C_NONE;
        p->curse_ticks = 0;
        p->hp = 1;
        return 0;
    }
    int mana = p->mana;
    p->mana = mana + cost; /* (lo que gaste sale de acá) */
    int busy = use_skill_mana(p, skill), paid = mana + cost - p->mana;
    p->mana = mana;
    if (paid > 0) {
        p->hp = p->hp - paid > 1 ? p->hp - paid : 1;
        if (p->hp < 40) {
            p->curse = A4C_NONE;
            p->curse_ticks = 0;
        }
    }
    return busy;
}

/* Lo que hace el botón A, B, X o Y fuera de los menús */
static int use_button(Player *p, int b, int in_town)
{
    int what = p->buttons[b];
    p->last_button = b;
    if (what == BTN_EMPTY)
        return 0;
    if (in_town) {
        if (what >= 0 && skills_get(p->cls, what)->aura && p->skill_lvl[what] > 0)
            aura_select(p, what); /* las auras se pueden elegir en el pueblo */
        else if (what >= 0) {
            show_message(p, "En el pueblo no se pelea");
            player_say(p, "not_in_town_1"); /* "Aquí no" o "No en el poblado" (grupo de 2) */
        }
        return 0;
    }
    if (what == BTN_THROW) {
        if (!items_can_throw(p->equip[SLOT_WEAPON].type)) {
            show_message(p, "Necesitás un arma para lanzar");
            return 0;
        }
        return start_ranged(p, -1, ATK_THROW);
    }
    if (what == BTN_ATTACK) {
        if (!player_anim(p, MODE_A1)->ok)
            return 0;
        if (items_ammo_for(p->equip[SLOT_WEAPON].type) && p->form == FORM_NONE) /* con arco o ballesta, dispara (no transformado) */
            return start_ranged(p, -1, ATK_SHOOT);
        start_attack(p, -1);
        return 1;
    }
    return use_skill(p, what);
}

/* Cuánto lo frena el frío (%): enfriado, la mitad; el aura de Duriel, lo suyo */
static int cold_slow(const Player *p)
{
    int s = p->chill_ticks > 0 || p->curse == A4C_DECREPIFY ? 50 : 0; /* (Decrepitud: -50 % de velocidad) */
    return p->aura_slow > s ? p->aura_slow : s;
}

static void player_move(Player *p, float sx, float sy, int sprint)
{
    const CharClass *c = &s_classes[p->cls];
    float slen = sqrtf(sx * sx + sy * sy);
    if (slen <= 0.2f)
        return;
    p->running = sprint && !p->exhausted && p->stamina > 0.0f;
    float wx, wy;
    screen_dir_to_world(sx, sy, &wx, &wy);
    float wl = sqrtf(wx * wx + wy * wy);
    float step = (p->running ? c->run_speed * RUN_BONUS : c->walk_speed * WALK_BONUS) / TICK_RATE *
                 (100 + p->bonus[STAT_MOVE] + p->sk_speed + p->au_speed + frenzy_bonus(p, 0) + p->quick_speed + p->dru_speed) /
                 100.0f * (float)(100 - cold_slow(p)) / 100.0f;
    wx = wx / wl * step;
    wy = wy / wl * step;

    /* Probar cada eje por separado para deslizar contra las paredes */
    float ox = p->x, oy = p->y;
    if (!player_blocked(p->x + wx, p->y, p->x, p->y))
        p->x += wx;
    if (!player_blocked(p->x, p->y + wy, p->x, p->y))
        p->y += wy;
    /* Si ningún eje solo entra, el paso entero: el camino del original avanza
       en diagonal y se baja así por las escaleras angostas (las del Santuario) */
    if (p->x == ox && p->y == oy && !player_blocked(ox + wx, oy + wy, ox, oy)) {
        p->x = ox + wx;
        p->y = oy + wy;
    }

    /* Si lo frena un NPC, rodearlo por el costado que tenga más cerca */
    float ux, uy;
    if (p->x == p->prev_x && p->y == p->prev_y && units_blocking_at(p->x + wx, p->y + wy, UNIT_PLAYER_BODY, &ux, &uy)) {
        float px = -wy, py = wx; /* perpendicular a la marcha */
        if ((p->x - ux) * px + (p->y - uy) * py < 0.0f) {
            px = -px;
            py = -py;
        }
        float nx = p->x + px + wx * 0.3f, ny = p->y + py + wy * 0.3f;
        if (!player_blocked(nx, ny, p->x, p->y)) {
            p->x = nx;
            p->y = ny;
        }
    }
    p->moving = p->x != p->prev_x || p->y != p->prev_y;
    p->face_x = sx / slen;
    p->face_y = sy / slen;
    p->sector = sector_from_screen(sx, sy);
}

static int anim_done(const Player *p)
{
    return (p->anim_acc >> 8) >= charanim_frames(player_anim(p, p->mode));
}

/* Los "más rápido" de los objetos rinden menos cuanto más hay (como los
   cuadros de menos del original): 120 * x / (120 + x), hasta 75 */
static int fast_pct(int x)
{
    int e = x > 0 ? 120 * x / (120 + x) : 0;
    return e > 75 ? 75 : e;
}

/* El NPC con el que habla A: en el pueblo cualquiera; afuera, Tyrael (en la
   cámara de Tal Rasha) */
/* ¿Hay un objeto de misión para usar más cerca que ese NPC? */
static int quest_nearer(const Player *p, int npc)
{
    int u;
    if (!quest_action(p, &u) || u < 0)
        return 0;
    float ux, uy, nx, ny;
    units_position(u, 1.0f, &ux, &uy);
    units_position(npc, 1.0f, &nx, &ny);
    return (ux - p->x) * (ux - p->x) + (uy - p->y) * (uy - p->y) < (nx - p->x) * (nx - p->x) + (ny - p->y) * (ny - p->y);
}

/* En el pueblo, con A y el cartel: ¿el portal de al lado antes que el NPC?
   Si está más cerca (Anya camina junto a su portal rojo al templo y A le
   hablaba a ella: el bot se quedaba ahí) */
static int portal_first(const Player *p, int npc)
{
    const Warp *w = portal_near(p);
    if (!w || npc < 0 || warp_closed(w))
        return 0;
    float nx, ny, wx = (w->x0 + w->x1) / 2, wy = (w->y0 + w->y1) / 2;
    units_position(npc, 1.0f, &nx, &ny);
    return (wx - p->x) * (wx - p->x) + (wy - p->y) * (wy - p->y) < (nx - p->x) * (nx - p->x) + (ny - p->y) * (ny - p->y);
}

static int talk_npc_near(const Player *p, int in_town)
{
    int npc = units_find_npc(p->x, p->y, 4.0f);
    /* afuera solo los que están ahí para hablar: Tyrael encadenado y en la Cámara, el espíritu de Izual y Hadriel en el río */
    const char *id = npc >= 0 ? units_id(npc) : "";
    return npc >= 0 && (in_town || !strcmp(id, "tyrael1") || !strcmp(id, "tyrael3") || !strcmp(id, "izualghost") ||
                        !strcmp(id, "malachai"))
               ? npc
               : -1;
}

/* Las frases de chat del original (chat_a a chat_h de cada clase, cada una
   con dos variantes que salen al azar): R3 abre la lista, la cruceta elige,
   A la dice y B la cierra. Mientras tanto se puede caminar. */
static const char *const k_chat_names[8] = {
    "¡Ayuda!", "Sígueme", "Esto es tuyo", "Gracias", "Lo siento", "Adiós", "¡Muere!", "¡Huye!",
};

static void chat_tick(Player *p, const PlayerInput *pi)
{
    if (pi->nav_up)
        p->chat_sel = (p->chat_sel + 7) % 8;
    if (pi->nav_down)
        p->chat_sel = (p->chat_sel + 1) % 8;
    if (pi->action) {
        char what[16];
        snprintf(what, sizeof what, "chat_%c_1", 'a' + p->chat_sel);
        player_say(p, what);
    }
    if (pi->action || pi->back)
        p->chat_open = 0;
}

static void player_tick(Player *p, float sx, float sy, int sprint, const PlayerInput *pi)
{
    const CharClass *c = &s_classes[p->cls];
    if (p->stats_dirty)
        refresh_stats(p);
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->moving = 0;
    p->running = 0;

    look_tick(p);
    const Level *lv = world_level_at(p->x, p->y);
    int in_town = lv && lv->town;
    if (lv && !in_town)
        memset(p->talk_away, 1, sizeof p->talk_away);
    if (lv && lv != p->level) {
        p->level = lv;
        p->banner_ticks = 3 * TICK_RATE;
        int bit = strcmp(lv->name, "Den of Evil") == 0 ? 1 : strcmp(lv->name, "Burial Grounds") == 0 ? 2 : 0;
        if (bit && !(p->said & bit)) {
            p->said |= bit;
            player_say(p, bit == 1 ? "act1_find_den" : "act1_find_burial");
        }
    }
    if (p->banner_ticks > 0)
        p->banner_ticks--;
    if (p->levelup_ticks > 0)
        p->levelup_ticks--;

    p->held_mask = 0;
    for (int b = 0; b < 4; b++)
        p->held_mask |= (pi->held[b] ? 1 : 0) << b;
    if (p->act > p->act_max && !s_eco) /* llegó a otro acto: una pestaña más del cofre (la arena no cuenta) */
        p->act_max = p->act;
    if (p->act > p->act_top && !s_eco)
        p->act_top = p->act;
    tick_potions(p);
    skills_tick(p);
    PlayerInput none;
    memset(&none, 0, sizeof none);
    if (p->state >= PS_DYING || p->dialog_npc >= 0 || p->reading || p->wp_open || p->inv_open)
        p->chat_open = 0; /* la lista de frases, solo jugando */
    if (p->state < PS_DYING) {
        if (pi->chat && p->dialog_npc < 0 && !p->reading && !p->wp_open && !p->inv_open)
            p->chat_open = !p->chat_open;
        if (pi->inventory && p->dialog_npc < 0 && !p->wp_open) {
            inventory_stow(p);
            if (p->inv_open)
                inventory_close(p);
            else
                p->inv_open = 1;
            p->inv_page = 0;
            p->assigning = 0;
            if (p->cube_open) { /* el inventario se abre con el equipo */
                p->cube_open = 0;
                p->inv_sel = INV_CELL0;
            }
        }
        if (p->stash_open && !p->inv_open) /* se cerró por otro lado */
            inventory_close(p);
        /* Directo al árbol de habilidades o a los atributos (Select + Y o X; teclado: T / U, G / H); la
           misma página otra vez la cierra */
        int page = pi->skill_tree ? PAGE_SKILLS : pi->stats_page ? PAGE_STATS : -1;
        if (page >= 0 && p->dialog_npc < 0 && !p->wp_open) {
            int shut = p->inv_open && p->inv_page == page;
            inventory_stow(p);
            if (p->stash_open)
                inventory_close(p);
            if (p->cube_open) {
                p->cube_open = 0;
                p->inv_sel = INV_CELL0;
            }
            p->inv_open = !shut;
            p->inv_page = page;
            p->assigning = 0;
            p->btn_pick = 0;
        }
        /* Con un diálogo, una tienda o el inventario abierto, los controles
           manejan eso y el personaje se queda quieto */
        if (p->dialog_npc >= 0) {
            if (p->shop_open)
                shop_tick(p, pi);
            else if (p->hire_open)
                hire_tick(p, pi);
            else if (p->imbue_open)
                imbue_tick(p, pi);
            else
                dialog_tick(p, pi);
            pi = &none;
            sx = sy = 0.0f;
        } else if (p->reading) {
            reading_tick(p, pi);
            pi = &none;
            sx = sy = 0.0f;
        } else if (p->wp_open) {
            waypoint_tick(p, pi);
            pi = &none;
            sx = sy = 0.0f;
        } else if (p->inv_open && !pi->inventory && !pi->skill_tree && !pi->stats_page) {
            if (p->inv_page == PAGE_SKILLS)
                tree_tick(p, pi);
            else if (p->inv_page == PAGE_BUTTONS)
                buttons_tick(p, pi);
            else if (p->inv_page == PAGE_STATS)
                stats_tick(p, pi);
            else if (p->inv_page == PAGE_MERC)
                merc_page_tick(p, pi);
            else if (p->inv_page == PAGE_QUESTS)
                quest_page_tick(p, pi);
            else
                inventory_tick(p, pi);
            pi = &none;
            sx = sy = 0.0f;
        } else if (p->chat_open) {
            chat_tick(p, pi);
            pi = &none; /* la cruceta y A son de la lista; el stick sigue caminando */
        }
        if (pi->potion_life)
            drink(p, 0);
        if (pi->potion_mana)
            drink(p, 1);
        if (pi->potion_stamina)
            drink(p, 2);
        if (pi->belt >= 1 && pi->belt <= BELT_SLOTS) /* la cruceta: esa casilla, sea la poción que sea */
            drink_slot(p, pi->belt - 1);
        if (pi->automap)
            p->map_open = !p->map_open;
    }
    if (p->frozen_ticks > 0) { /* congelado: puede tomar pociones, pero ni se mueve ni ataca */
        p->frozen_ticks--;
        if (p->state < PS_DYING) {
            pi = &none;
            sx = sy = 0.0f;
        }
    }

    const CharAnim *a = player_anim(p, p->mode);
    switch (p->state) {
    case PS_DEAD:
        if (s_eco) { /* (en el Eco no se reaparece en el pueblo: eco.inc) */
            p->dead_ticks++;
            return;
        }
        if (++p->dead_ticks >= RESPAWN_SECONDS * TICK_RATE) {
            /* Reaparece en el pueblo con la vida llena */
            place_at(p, (int)(p - s_pl), s_town_x, s_town_y);
            p->hp = p->hp_max;
            p->dead_ticks = 0;
            set_player_state(p, PS_NORMAL, MODE_TN);
            if (p->gold_lost > 0) {
                char msg[64];
                snprintf(msg, sizeof msg, tr("Perdiste %d de oro"), p->gold_lost);
                show_message(p, msg);
                p->gold_lost = 0;
            }
        }
        return;
    case PS_DYING:
        p->anim_acc += a->speed;
        if (anim_done(p)) {
            set_player_state(p, PS_DEAD, player_anim(p, MODE_DD)->ok ? MODE_DD : MODE_DT);
            p->dead_ticks = 0;
        }
        return;
    case PS_HIT: /* con la recuperación de golpes y el bloqueo más rápidos de los objetos */
        p->anim_acc += a->speed * (100 + fast_pct(p->bonus[p->mode == MODE_BL ? STAT_FBR : STAT_FHR])) / 100;
        if (anim_done(p))
            set_player_state(p, PS_NORMAL, MODE_NU);
        return;
    case PS_ATTACK: {
        if (p->charge_ticks > 0) { /* Carga: corriendo hacia el monstruo */
            charge_tick(p);
            return;
        }
        if (p->seq) { /* las habilidades con secuencia del original */
            seq_tick(p);
            return;
        }
        if (zealing(p)) { /* Celo: varios golpes rebobinando la animación */
            zeal_tick(p, a);
            if (anim_done(p))
                set_player_state(p, PS_NORMAL, MODE_NU);
            return;
        }
        if (strafing(p)) {
            strafe_tick(p, a);
            if (anim_done(p))
                set_player_state(p, PS_NORMAL, MODE_NU);
            return;
        }
        int speed = a->speed * (100 + attack_ias(p)) / 100 * (100 - cold_slow(p)) / 100;
        p->anim_acc += speed;
        /* El golpe va en el cuadro que marca animdata.d2 */
        int frames = charanim_frames(a);
        while (p->hits_done < p->hits_total) {
            int hf = a->hit_frame + (frames - a->hit_frame) * p->hits_done / p->hits_total;
            if ((p->anim_acc >> 8) < hf)
                break;
            p->hits_done++;
            if (p->attack_kind == ATK_MELEE)
                melee_hit(p);
            else
                fire_ranged(p);
        }
        if (anim_done(p))
            set_player_state(p, PS_NORMAL, MODE_NU);
        return;
    }
    case PS_CAST:
        p->anim_acc += a->speed * (100 + fast_pct(p->bonus[STAT_FCR])) / 100 * (100 - cold_slow(p)) / 100; /* lanzamiento más rápido */
        if (!p->hits_done && (p->anim_acc >> 8) >= (a->hit_frame > 0 ? a->hit_frame : charanim_frames(a) / 2)) {
            p->hits_done = 1;
            cast_effect(p);
        }
        if (anim_done(p))
            set_player_state(p, PS_NORMAL, MODE_NU);
        return;
    default:
        break;
    }

    /* Pasar por un waypoint apagado lo activa y abre el panel, como en el original */
    int wp = waypoint_near(p);
    if (wp >= 0 && !((p->waypoints >> wp) & 1)) {
        p->waypoints |= 1ull << wp;
        waypoints_light();
        audio_play_name("object_waypoint_open", p->x, p->y);
        save_character(p);
        open_waypoints(p, wp);
        return;
    }

    /* El oro se junta solo al pasar por encima */
    int gold = items_ground_nearest(p->x, p->y, 1.0f, 1);
    if (gold >= 0) {
        const Item *g = items_ground_item(gold, NULL, NULL);
        audio_play(g->type->drop_sound, p->x, p->y);
        pick_up(p, g);
        items_ground_remove(gold);
    }

    /* El botón de acción levanta un objeto si hay uno al lado y ningún
       monstruo cerca; si no, ataca. En el pueblo no se pelea. */
    if (pi->action) {
        int it = items_ground_nearest(p->x, p->y, 1.8f, 0);
        int npc = talk_npc_near(p, in_town);
        /* Con la mochila llena el objeto se queda y A sigue con lo demás (si
           no, un objeto al lado de una Piedra Cairn no dejaba tocarla) */
        int took = 0, full = 0;
        if (it >= 0 && units_find_enemy(p->x, p->y, MELEE_RANGE) < 0) {
            /* Lo más cercano; si no entra, lo que siga más cerca que sí entre
               (con el joystick no se elige cuál: en un montón, lo que no
               entraba tapaba lo demás, también la Gidbinn) */
            int tried[16], nt = 0;
            for (int k = it; k >= 0 && !took && nt < 16;) {
                const Item *g = items_ground_item(k, NULL, NULL);
                if (pick_up(p, g)) {
                    audio_play(g->type->drop_sound, p->x, p->y);
                    items_ground_remove(k);
                    refresh_look(p);
                    took = 1;
                    break;
                }
                tried[nt++] = k;
                float bd = 1.8f * 1.8f;
                k = -1;
                for (int i = 0; i < items_ground_count(); i++) {
                    float gx, gy;
                    int seen = 0;
                    if (!items_ground_item(i, &gx, &gy))
                        continue;
                    for (int t = 0; t < nt; t++)
                        seen |= tried[t] == i;
                    float d = (gx - p->x) * (gx - p->x) + (gy - p->y) * (gy - p->y);
                    if (!seen && d <= bd) {
                        bd = d;
                        k = i;
                    }
                }
            }
            if (!took) {
                inventory_full(p);
                full = 1; /* no ataca al aire */
            }
        }
        /* Con un monstruo al lado, A pega antes que abrir el waypoint o hablar
           con un NPC de afuera (como con los cofres y las escaleras): al lado
           del waypoint del River of Flame o de Hadriel no se le podía pegar al
           que atacaba (A abría y cerraba el panel o el diálogo) */
        int foe = units_find_enemy(p->x, p->y, MELEE_RANGE) >= 0;
        if (took) {
        } else if (wp >= 0 && !foe) {
            open_waypoints(p, wp);
        } else if (npc >= 0 && !foe && (in_town ? !portal_first(p, npc) : !portal_near(p)) && !quest_nearer(p, npc)) {
            /* afuera, el portal que abrió Tyrael va antes; y un objeto de misión más cerca que el NPC (el portal del final, al
               lado de Tyrael) */
            open_dialog(p, npc);
        } else if (quest_first(p) && quest_interact(p)) {
            return;
        } else if (obj_first(p) && !foe && obj_interact(p)) {
            return; /* un cofre, un barril, un santuario (con un monstruo al lado, A pega) */
        } else if (portal_near(p) && !foe) {
            /* La escalera o el portal (con un monstruo al lado, A pega) */
            use_warp(p, portal_near(p));
            return;
        } else if (quest_interact(p)) { /* se usan una sola vez: aunque haya monstruos cerca */
            return;
        } else if (!full && use_button(p, 0, in_town)) {
            return;
        }
    } else {
        for (int b = 1; b < NUM_BUTTONS; b++)
            if (pi->button[b] && use_button(p, b, in_town))
                return;
    }

    /* En el pueblo la vida se recupera sola */
    if (in_town && p->hp < p->hp_max) {
        p->heal_acc += p->hp_max / (TOWN_HEAL_SECONDS * TICK_RATE);
        while (p->heal_acc >= 1.0f && p->hp < p->hp_max) {
            p->hp++;
            p->heal_acc -= 1.0f;
        }
    }

    player_move(p, sx, sy, pi->always_run ? !sprint : sprint); /* con "correr siempre", el gatillo camina */

    /* Pisar la boca de la cueva (o su salida) lleva al otro lado; si esa
       zona no estaba armada, se arma ahora */
    /* Los de pisar (no los portales ni las escaleras, que van con A); el de
       llegada no cuenta hasta salir de él */
    const Warp *warp = NULL;
    int on_lock = 0;
    float warp_d = 1e9f;
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        if (w->portal || w->margin > 0 || !world_zone_loaded(w->from) || p->x < w->x0 - WARP_TOUCH ||
            p->x >= w->x1 + WARP_TOUCH || p->y < w->y0 - WARP_TOUCH || p->y >= w->y1 + WARP_TOUCH)
            continue;
        if (p->warp_lock && w->from == p->lock_from && w->to == p->lock_to) {
            on_lock = 1;
            continue;
        }
        /* Si toca dos (las escaleras pegadas de la torre), la más cercana */
        float dx = p->x < w->x0 ? w->x0 - p->x : p->x > w->x1 ? p->x - w->x1 : 0.0f;
        float dy = p->y < w->y0 ? w->y0 - p->y : p->y > w->y1 ? p->y - w->y1 : 0.0f;
        if (dx * dx + dy * dy < warp_d) {
            warp_d = dx * dx + dy * dy;
            warp = w;
        }
    }
    if (warp && on_lock) {
        /* Recién llegado por un salto pegado a otro: hasta salir del de llegada no se toma otro */
        warp = NULL;
    }
    if (!on_lock)
        p->warp_lock = 0;
    if (warp)
        use_warp(p, warp);

    /* Correr gasta estamina (en el pueblo no, como en el original); caminar o
       quedarse quieto la recupera de a poco. Si se agota, no se puede volver
       a correr hasta que se llene. */
    p->running = p->running && p->moving;
    if (p->running && !in_town && p->stamina_boost_ticks == 0) {
        p->stamina -= c->run_drain;
        if (p->stamina <= 0.0f) {
            p->stamina = 0.0f;
            p->exhausted = 1;
        }
    } else if (!p->running && p->stamina < p->stamina_max) {
        p->stamina += p->stamina_max / (STAMINA_REFILL_SECONDS * TICK_RATE) * (100 + p->bonus[STAT_STAM_REGEN] + p->au_stam_regen) / 100.0f;
        if (p->stamina >= p->stamina_max) {
            p->stamina = p->stamina_max;
            p->exhausted = 0;
        }
    }

    int mode = !p->moving  ? (in_town ? MODE_TN : MODE_NU)
             : p->running  ? MODE_RN
             : in_town     ? MODE_TW
                           : MODE_WL;
    if (!player_anim(p, mode)->ok)
        mode = p->moving ? MODE_TW : MODE_TN;
    if (mode != p->mode) {
        p->mode = mode;
        p->anim_acc = 0;
        p->step_frame = -1;
    }
    /* Las piernas, al ritmo de la velocidad (más rápida que la del original) */
    float pace = mode == MODE_RN ? RUN_BONUS * RUN_ANIM_PACE : mode == MODE_WL || mode == MODE_TW ? WALK_BONUS : 1.0f;
    p->anim_acc += (int)(player_anim(p, mode)->speed * pace * (float)(100 - cold_slow(p)) / 100.0f);

    /* Dos pasos por vuelta de la caminata (piedra en las cuevas) */
    if (p->moving) {
        int frames = charanim_frames(player_anim(p, mode));
        int frame = frames > 0 ? (p->anim_acc >> 8) % frames : 0;
        if (frame != p->step_frame && (frame == 0 || frame == frames / 2))
            audio_play(c->snd_step[lv && lv->dark][p->running], p->x, p->y);
        p->step_frame = frame;
    }
}

void game_print_status(void)
{
    /* "x,y": colisión alrededor de ese subtile: # bloquea, v grosor de pared,
       u alguien, + no entra la cruz, - el centro queda pegado a una pared */
    const char *area = getenv("D2_DUMP_BLOCKS");
    int ax, ay;
    if (area && sscanf(area, "%d,%d", &ax, &ay) == 2) {
        int r = getenv("D2_DUMP_R") ? atoi(getenv("D2_DUMP_R")) : 5; /* radio del dibujo */
        for (int y = ay - r; y <= ay + r; y++) {
            printf("    y=%3d ", y);
            for (int x = ax - r; x <= ax + r; x++)
                putchar(map_blocked(&s_map, x, y)                        ? '#'
                        : map_coll(&s_map, x, y) & COLL_VOLUME             ? 'v'
                        : units_blocking(x + 0.5f, y + 0.5f, 0.0f)         ? 'u'
                        : foot_blocked(x, y)                               ? '+'
                        : map_too_close(&s_map, x + 0.5f, y + 0.5f)        ? '-'
                                                                           : '.');
            putchar('\n');
        }
    }
    /* D2_DUMP_TILES="x0,y0,x1,y1": colisión de cada tile de esa zona (# casi
       todo bloqueado, + algo) para buscar lugares encerrados */
    const char *tiles = getenv("D2_DUMP_TILES");
    int tx0, ty0, tx1, ty1;
    if (tiles && sscanf(tiles, "%d,%d,%d,%d", &tx0, &ty0, &tx1, &ty1) == 4) {
        for (int ty = ty0; ty < ty1; ty++) {
            printf("    %3d ", ty);
            for (int tx = tx0; tx < tx1; tx++) {
                int b = 0;
                for (int sy = 0; sy < TILE_SUB; sy++)
                    for (int sx = 0; sx < TILE_SUB; sx++)
                        b += map_blocked(&s_map, tx * TILE_SUB + sx, ty * TILE_SUB + sy);
                putchar(b >= 13 ? '#' : b > 0 ? '+' : '.');
            }
            putchar('\n');
        }
    }
    static const char *const states[] = { "normal", "atacando", "golpeado", "habilidad", "muriendo", "muerto" };
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        printf("  J%d: (%.0f, %.0f) %s, vida %d/%d, maná %d/%d, nivel %d (exp %u), %s, estamina %.1f/%.0f%s%s\n",
               i + 1, p->x, p->y, p->level ? p->level->name : "?", p->hp, p->hp_max, p->mana, p->mana_max,
               p->level_num, (unsigned)p->exp, states[p->state], p->stamina, p->stamina_max,
               p->running ? " corriendo" : p->moving ? " caminando" : "", p->exhausted ? " (agotado)" : "");
        if (p->message_ticks > 0)
            printf("  J%d dice: %s\n", i + 1, p->message);
        if (p->inv_open)
            printf("  J%d inventario: cursor %d, en la mano %s, mochila %d\n", i + 1, p->inv_sel,
                   p->held.type ? p->held.type->code : "-", p->bag_count);
        if (p->merc_name && p->merc_unit >= 0) {
            int hp, max_hp;
            float mx, my;
            units_merc_health(p->merc_unit, &hp, &max_hp);
            units_position(p->merc_unit, 1.0f, &mx, &my);
            printf("  J%d mercenaria: %s, nivel %d, (%.0f, %.0f), vida %d/%d%s\n", i + 1,
                   merc_name_of(p->merc_type, p->merc_name),
                   p->merc_level, mx, my, hp, max_hp, p->merc_dead ? ", muerta" : "");
        }
    }
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        int belt = 0;
        for (int k = 0; k < BELT_SLOTS; k++)
            belt += p->belt[k].type != NULL;
        char weapon[64], shield[64];
        printf("  J%d: oro %d, arma %s, escudo %s, mochila %d, cinturón %d/%d\n", i + 1, p->gold,
               p->equip[SLOT_WEAPON].type ? items_label(&p->equip[SLOT_WEAPON], weapon, sizeof weapon) : "-",
               p->equip[SLOT_SHIELD].type ? items_label(&p->equip[SLOT_SHIELD], shield, sizeof shield) : "-",
               p->bag_count, belt, BELT_SLOTS);
        printf("  J%d misiones: cueva %d/%d cementerio %d/%d caín %d mazo %d torre %d andariel %d; piedras %d\n",
               i + 1, p->den_done, p->den_rewarded, p->raven_done, p->raven_rewarded, p->cain_q, p->malus_q,
               p->tower_q, p->andy_q, s_stones_lit);
    }
    int on_ground = 0;
    for (int i = 0; i < items_ground_count(); i++)
        on_ground += items_ground_item(i, NULL, NULL) != NULL;
    printf("  objetos en el suelo: %d\n", on_ground);
    printf("  monstruos vivos: %d\n", units_count_enemies());
    if (s_shown_target >= 0)
        printf("  último golpeado: '%s'\n", units_display_name(s_shown_target));
    for (int i = 0; i < s_num_players; i++) {
        int npc = units_find_npc(s_pl[i].x, s_pl[i].y, 10.0f);
        float nx = 0, ny = 0;
        if (npc >= 0)
            units_position(npc, 1.0f, &nx, &ny);
        printf("  J%d: NPC cercano %s a %.1f subtiles, diálogo %d, tienda %d\n", i + 1, npc >= 0 ? units_id(npc) : "-",
               npc >= 0 ? sqrtf((nx - s_pl[i].x) * (nx - s_pl[i].x) + (ny - s_pl[i].y) * (ny - s_pl[i].y)) : 0.0f,
               s_pl[i].dialog_npc, s_pl[i].shop_open);
    }
}

/* ---- Partidas guardadas ----
   Un archivo de texto por personaje (ba.sav, am.sav): nivel, experiencia,
   vida y maná base, oro, misiones y todos los objetos. Al volver a jugar se
   arranca en el pueblo, como en el original. */

#define SAVE_MAGIC "D2PORT-SAVE 1"

/* Opciones (el volumen) en opciones.txt, junto a las partidas */
static void load_options(void)
{
    char path[300];
    snprintf(path, sizeof path, "%s/opciones.txt", s_save_dir);
    FILE *f = s_save_dir[0] ? fopen(path, "r") : NULL;
    if (!f)
        return;
    int sfx, music;
    audio_get_volume(&sfx, &music);
    char line[64];
    while (fgets(line, sizeof line, f)) {
        sscanf(line, "efectos %d", &sfx);
        sscanf(line, "musica %d", &music);
        if (sscanf(line, "correr %d", &s_always_run) == 1)
            s_always_run = s_always_run != 0;
        if (sscanf(line, "posicion %d", &s_show_pos) == 1)
            s_show_pos = s_show_pos != 0;
        if (sscanf(line, "rendimiento %d", &s_show_perf) == 1)
            s_show_perf = s_show_perf != 0;
        if (sscanf(line, "color16 %d", &s_color16) == 1)
            s_color16 = s_color16 != 0;
        sscanf(line, "idioma %7s", s_lang_pref);
    }
    fclose(f);
    audio_set_volume(sfx, music);
}

static void save_options(void)
{
    char path[300];
    snprintf(path, sizeof path, "%s/opciones.txt", s_save_dir);
    FILE *f = s_save_dir[0] ? fopen(path, "w") : NULL;
    if (!f)
        return;
    int sfx, music;
    audio_get_volume(&sfx, &music);
    fprintf(f, "efectos %d\nmusica %d\ncorrer %d\nposicion %d\nrendimiento %d\n", sfx, music, s_always_run, s_show_pos,
            s_show_perf);
    if (s_color16 >= 0)
        fprintf(f, "color16 %d\n", s_color16);
    if (s_lang_pref[0])
        fprintf(f, "idioma %s\n", s_lang_pref);
    fclose(f);
}

void game_set_language(const char *code)
{
    snprintf(s_lang_pref, sizeof s_lang_pref, "%s", code ? code : "");
    save_options();
}

void game_set_save_dir(const char *dir)
{
    snprintf(s_save_dir, sizeof s_save_dir, "%s", dir ? dir : "");
    if (s_save_dir[0]) {
        compat_mkdir(s_save_dir);
        migrate_old_saves();
        load_options();
    }
}

static int save_path(int slot, char *buf, size_t cap)
{
    if (!s_save_dir[0] || slot < 1 || slot > SAVE_SLOTS)
        return 0;
    snprintf(buf, cap, "%s/slot%d.sav", s_save_dir, slot);
    return 1;
}

/* Lo que es de cada dificultad (pQuestData[nDifficulty] del original): las
   misiones, las presentaciones de los NPC, los waypoints, el acto en el que
   quedó, el mercenario gratis de Kashya y el Rey Vaca. Las de Normal van
   sin prefijo (como antes de las dificultades) y las de Pesadilla e
   Infierno con "d1 " y "d2 " */
static const char *const k_diff_pre[3] = { "", "d1 ", "d2 " };

/* La dificultad de una línea de la partida guardada (-1 = es del personaje)
   y, en *rest, la línea sin el prefijo */
static int save_line_diff(const char *line, const char **rest)
{
    static const char *const k_keys[] = { "den ", "quests ", "waypoints ", "talk ", "act1 ", "act2 ", "act3 ",
                                          "act4 ", "act5 ", "cowking ", "inact ", "qseen ", "freehire ", "acttop ",
                                          "respec " };
    int d = 0;
    if (line[0] == 'd' && (line[1] == '1' || line[1] == '2') && line[2] == ' ') {
        d = line[1] - '0';
        line += 3;
    }
    for (unsigned k = 0; k < sizeof k_keys / sizeof k_keys[0]; k++)
        if (strncmp(line, k_keys[k], strlen(k_keys[k])) == 0) {
            *rest = line;
            return d;
        }
    return -1;
}

static void save_diff(FILE *f, const Player *p, int d)
{
    const char *pre = k_diff_pre[d];
    fprintf(f, "%sden %d %d\n", pre, p->den_done, p->den_rewarded);
    fprintf(f, "%squests %d %d %d %d %llu\n", pre, p->den_started, p->raven_started, p->raven_done, p->raven_rewarded,
            (unsigned long long)p->npc_met);
    fprintf(f, "%swaypoints %llu\n", pre, (unsigned long long)p->waypoints);
    for (int k = 0; k < NPC_TALKERS; k++)
        if (p->talk_heard[k][0] || p->talk_heard[k][1] || p->talk_next[k]) /* (el segundo número, desde el Acto 5) */
            fprintf(f, "%stalk %d %d %llu %llu\n", pre, k, p->talk_next[k], (unsigned long long)p->talk_heard[k][0],
                    (unsigned long long)p->talk_heard[k][1]);
    fprintf(f, "%sact1 %d %d %d %d\n", pre, p->cain_q, p->malus_q, p->tower_q, p->andy_q);
    fprintf(f, "%sact2 %d %d %d %d %d %d %d %d\n", pre, p->rada_q, p->staff_q, p->staff_seen, p->sun_q, p->arcane_q,
            p->summ_q, p->tombs_q, p->a2_flags);
    fprintf(f, "%sact3 %d %d %d %d %d %d %d\n", pre, p->bird_q, p->blade_q, p->will_q, p->tome_q, p->temple_q,
            p->meph_q, p->a3_flags);
    fprintf(f, "%sact4 %d %d %d %d\n", pre, p->izual_q, p->forge_q, p->diablo_q, p->a4_flags);
    fprintf(f, "%sact5 %d %d %d %d %d %d %d %d\n", pre, p->shenk_q, p->rescue_q, p->anya_q, p->nihl_q, p->ancients_q,
            p->baal_q, p->a5_flags, p->rescued);
    if (p->cow_king)
        fprintf(f, "%scowking 1\n", pre);
    if (p->respec_done)
        fprintf(f, "%srespec 1\n", pre);
    fprintf(f, "%sinact %d\n", pre, p->act);
    fprintf(f, "%sqseen %u\n", pre, (unsigned)p->quest_seen);
    fprintf(f, "%sfreehire %d\n", pre, p->free_hire);
    fprintf(f, "%sacttop %d\n", pre, p->act_top);
}

/* El personaje como queda en la partida guardada */
static void save_character_to(FILE *f, const Player *p)
{
    char line[1024];
    fprintf(f, "%s\nclass %s\nlevel %d\nexp %u\nlife %d\nmana %d\ngold %d\n", SAVE_MAGIC, s_classes[p->cls].token,
            p->level_num, (unsigned)p->exp, p->hp_base, p->mana_base, p->gold);
    for (int s = 0; s < SLOT_COUNT; s++)
        if (p->equip[s].type) {
            items_to_text(&p->equip[s], line, sizeof line);
            fprintf(f, "equip %d %s\n", s, line);
        }
    for (int i = 0; i < p->bag_count; i++) {
        items_to_text(&p->bag[i], line, sizeof line);
        fprintf(f, "bagat %d %d %s\n", p->bag_x[i], p->bag_y[i], line);
    }
    for (int i = 0; i < BELT_SLOTS; i++)
        if (p->belt[i].type) {
            items_to_text(&p->belt[i], line, sizeof line);
            fprintf(f, "belt %d %s\n", i, line);
        }
    for (int i = 0; i < p->cube_count; i++) { /* lo del Cubo Horádrico */
        items_to_text(&p->cube[i], line, sizeof line);
        fprintf(f, "cubeat %d %d %s\n", p->cube_x[i], p->cube_y[i], line);
    }
    /* El cofre: las pestañas compradas, el oro, el acto más alto y la que se
       veía; después cada objeto con su pestaña y lugar */
    fprintf(f, "stash %d %d %d %d\n", p->stash_bought, p->stash_gold, p->act_max, p->stash_tab);
    for (int t = 0; t < STASH_TABS; t++)
        for (int i = 0; i < p->stash_count[t]; i++) {
            items_to_text(&p->stash[t][i], line, sizeof line);
            fprintf(f, "stashat %d %d %d %s\n", t, p->stash_x[t][i], p->stash_y[t][i], line);
        }
    fprintf(f, "stats %d %d %d %d %d %d\n", p->stat_add[0], p->stat_add[1], p->stat_add[2], p->stat_add[3],
            p->stat_points, p->hp_extra);
    fprintf(f, "skillpoints %d\n", p->skill_points);
    for (int i = 0; i < skills_count(p->cls); i++)
        if (p->skill_lvl[i] > 0)
            fprintf(f, "skill %d %s\n", p->skill_lvl[i], skills_get(p->cls, i)->key);
    for (int b = 0; b < NUM_BUTTONS; b++)
        fprintf(f, "button %d %s\n", b, p->buttons[b] == BTN_ATTACK ? "*attack"
                                        : p->buttons[b] == BTN_EMPTY ? "*empty"
                                        : p->buttons[b] == BTN_THROW ? "*throw"
                                                                     : skills_get(p->cls, p->buttons[b])->key);
    for (int d = 0; d < 3; d++) /* lo de cada dificultad: la que se juega, de los campos; las otras (y todas en el Eco), como se leyeron */
        if (d == s_diff && !s_eco)
            save_diff(f, p, d);
        else
            fputs(p->diff_raw[d], f);
    fprintf(f, "diffmax %d\n", p->diff_max);
    if (p->eco_best > 0 || p->eco_runs > 0)
        fprintf(f, "eco %d %d\n", p->eco_best, p->eco_runs);
    if (p->merc_name) {
        int hp = p->merc_hp, max_hp;
        if (units_merc_alive(p->merc_unit))
            units_merc_health(p->merc_unit, &hp, &max_hp);
        fprintf(f, "merc %d %d %d %d %d\n", p->merc_type, p->merc_level, hp, p->merc_name, p->merc_dead);
    }
    if (p->aura >= 0) /* el aura puesta (como la habilidad derecha del original) */
        fprintf(f, "aura %s\n", skills_get(p->cls, p->aura)->key);
    for (int s = 0; s < MERC_SLOTS; s++)
        if (p->merc_equip[s].type) {
            items_to_text(&p->merc_equip[s], line, sizeof line);
            fprintf(f, "mercitem %d %s\n", s, line);
        }
}

static void save_character(const Player *p)
{
    char path[300], tmp[310];
    if (s_demo)
        return; /* el personaje de la demostración no se guarda */
    if (!save_path(p->slot, path, sizeof path))
        return;
    compat_mkdir(s_save_dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        fprintf(stderr, "no pude guardar en %s\n", tmp);
        return;
    }
    save_character_to(f, p);
    /* Se escribe aparte y se reemplaza al final: si se corta la luz a la
       mitad, queda la partida anterior entera */
    int ok = fflush(f) == 0;
    ok &= fclose(f) == 0;
    if (!ok || compat_replace(tmp, path) != 0)
        fprintf(stderr, "no pude guardar %s\n", path);
}

/* Partidas de antes de que se sacara el Tomo de Portal: sus cargas pasan a
   ser pergaminos (en la mochila, apilados; en el cubo, en su lugar) */
static void tomes_to_scrolls(Player *p)
{
    const ItemType *tsc = items_find("tsc");
    for (int k = p->bag_count - 1; k >= 0; k--) {
        if (strcmp(p->bag[k].type->code, "tbk") != 0)
            continue;
        int n = p->bag[k].amount;
        bag_remove(p, k);
        while (tsc && n > 0) {
            Item it = items_make(tsc);
            it.amount = n < TP_STACK ? n : TP_STACK;
            n -= it.amount;
            if (!bag_add(p, &it))
                break; /* (no debería pasar: el tomo dejó su lugar) */
        }
    }
    for (int k = p->cube_count - 1; k >= 0; k--) {
        if (strcmp(p->cube[k].type->code, "tbk") != 0)
            continue;
        if (tsc && p->cube[k].amount > 0) {
            Item it = items_make(tsc);
            it.amount = p->cube[k].amount < TP_STACK ? p->cube[k].amount : TP_STACK;
            p->cube[k] = it;
        } else {
            cube_remove(p, k);
        }
    }
}

/* Lee el personaje de `f` (y lo cierra); `path` es para los mensajes */
static int load_character_from(FILE *f, Player *p, const char *path)
{
    char line[1024];
    if (!fgets(line, sizeof line, f) || strncmp(line, SAVE_MAGIC, strlen(SAVE_MAGIC)) != 0) {
        fclose(f);
        fprintf(stderr, "%s no es una partida guardada\n", path);
        return 0;
    }
    const CharClass *c = &s_classes[p->cls];
    int got_points = 0, got_stats = 0, saved_life = 0, got_talk = 0, got_stash = 0, got_top = 0, baal_done[3] = { 0, 0, 0 };
    (void)c;
    for (int d = 0; d < 3; d++)
        p->diff_raw[d][0] = '\0';
    while (fgets(line, sizeof line, f)) {
        int a, b, used = 0, q[8];
        unsigned u;
        unsigned long long talk_mask, talk_mask2 = 0, wp_mask;
        long long met;
        Item it;
        line[strcspn(line, "\r\n")] = '\0';
        /* Lo de cada dificultad: el de las otras queda como está (save_diff) */
        const char *rest;
        int ld = save_line_diff(line, &rest);
        if (ld >= 0 && sscanf(rest, "act5 %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5]) == 6)
            baal_done[ld] = q[5] >= 4; /* (las de antes de las dificultades no dicen cuál está abierta) */
        if (ld >= 0 && (ld != s_diff || s_eco)) {
            size_t n = strlen(p->diff_raw[ld]), len = strlen(line);
            if (n + len + 2 <= sizeof p->diff_raw[ld])
                snprintf(p->diff_raw[ld] + n, sizeof p->diff_raw[ld] - n, "%s\n", line);
            else
                fprintf(stderr, "partida %s: no entra lo de la dificultad %d\n", path, ld);
            continue;
        }
        if (ld > 0)
            memmove(line, rest, strlen(rest) + 1);
        if (sscanf(line, "diffmax %d", &a) == 1) {
            p->diff_max = a < 0 ? 0 : a > 2 ? 2 : a;
            continue;
        }
        if (sscanf(line, "eco %d %d", &a, &b) == 2) {
            p->eco_best = a > 0 ? a : 0;
            p->eco_runs = b > 0 ? b : 0;
            continue;
        }
        if (sscanf(line, "acttop %d", &a) == 1) {
            p->act_top = a < 1 ? 1 : a > 5 ? 5 : a;
            got_top = 1;
            continue;
        }
        char tok[8];
        if (sscanf(line, "class %7s", tok) == 1) {
            for (int k = 0; k < NUM_CLASSES; k++)
                if (strcmp(tok, s_classes[k].token) == 0)
                    p->cls = k;
            c = &s_classes[p->cls];
            continue;
        }
        int st[6];
        if (sscanf(line, "stats %d %d %d %d %d %d", &st[0], &st[1], &st[2], &st[3], &st[4], &st[5]) == 6) {
            for (int k = 0; k < 4; k++)
                p->stat_add[k] = st[k] > 0 ? st[k] : 0;
            p->stat_points = st[4] > 0 ? st[4] : 0;
            p->hp_extra = st[5];
            got_stats = 1;
            continue;
        }
        if (sscanf(line, "skillpoints %d", &a) == 1) {
            p->skill_points = a > 0 ? a : 0;
            got_points = 1;
            continue;
        }
        if (sscanf(line, "skill %d %n", &a, &used) == 1 && used > 0) {
            int s = skills_find(p->cls, line + used);
            if (s >= 0 && a > 0)
                p->skill_lvl[s] = a > SKILL_MAX_LEVEL ? SKILL_MAX_LEVEL : a;
            continue;
        }
        if (sscanf(line, "button %d %n", &a, &used) == 1 && used > 0 && a >= 0 && a < NUM_BUTTONS) {
            const char *v = line + used;
            p->buttons[a] = strcmp(v, "*attack") == 0 ? BTN_ATTACK
                          : strcmp(v, "*empty") == 0  ? BTN_EMPTY
                          : strcmp(v, "*throw") == 0  ? BTN_THROW
                                                      : skills_find(p->cls, v);
            if (p->buttons[a] == -1 && strcmp(v, "*attack") != 0)
                p->buttons[a] = BTN_EMPTY; /* habilidad que ya no existe */
            continue;
        }
        if (sscanf(line, "level %d", &a) == 1)
            p->level_num = a < 1 ? 1 : a > 99 ? 99 : a;
        else if (sscanf(line, "exp %u", &u) == 1)
            p->exp = u;
        else if (sscanf(line, "life %d", &a) == 1)
            saved_life = a;
        else if (sscanf(line, "mana %d", &a) == 1)
            p->mana_base = a >= 0 ? a : c->mana;
        else if (sscanf(line, "gold %d", &a) == 1)
            p->gold = a > 0 ? a : 0;
        else if (sscanf(line, "den %d %d", &a, &b) == 2) {
            p->den_done = a;
            p->den_rewarded = b;
        } else if (sscanf(line, "merc %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4]) == 5) {
            p->merc_type = q[0] >= 0 && q[0] < MERC_TYPES && s_hire_rows[q[0]] > 0 ? q[0] : 0;
            p->merc_level = q[1] < 1 ? 1 : q[1] > 99 ? 99 : q[1];
            p->merc_hp = q[2];
            int names = s_hire_type[p->merc_type].num_names > 0 ? s_hire_type[p->merc_type].num_names : MERC_NAMES;
            p->merc_name = q[3] >= 1 && q[3] <= names ? q[3] : 1;
            p->merc_dead = q[4] != 0;
        } else if (sscanf(line, "mercitem %d %n", &a, &used) == 1 && used > 0 && a >= 0 && a < MERC_SLOTS &&
                   items_from_text(line + used, &it)) {
            p->merc_equip[a] = it;
        } else if (strncmp(line, "aura ", 5) == 0) {
            int s = skills_find(p->cls, line + 5);
            p->aura = s >= 0 && skills_get(p->cls, s)->aura ? s : -1;
        } else if (sscanf(line, "freehire %d", &a) == 1) {
            p->free_hire = a != 0;
        } else if (sscanf(line, "act2 %d %d %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6], &q[7]) == 8) {
            p->rada_q = q[0];
            p->staff_q = q[1];
            p->staff_seen = q[2];
            p->sun_q = q[3];
            p->arcane_q = q[4];
            p->summ_q = q[5];
            p->tombs_q = q[6];
            p->a2_flags = q[7];
        } else if (sscanf(line, "act3 %d %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6]) == 7) {
            p->bird_q = q[0];
            p->blade_q = q[1];
            p->will_q = q[2];
            p->tome_q = q[3];
            p->temple_q = q[4];
            p->meph_q = q[5];
            p->a3_flags = q[6];
        } else if (sscanf(line, "act4 %d %d %d %d", &q[0], &q[1], &q[2], &q[3]) == 4) {
            p->izual_q = q[0];
            p->forge_q = q[1];
            p->diablo_q = q[2];
            p->a4_flags = q[3];
        } else if (sscanf(line, "act5 %d %d %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6], &q[7]) == 8) {
            p->shenk_q = q[0];
            p->rescue_q = q[1];
            p->anya_q = q[2];
            p->nihl_q = q[3];
            p->ancients_q = q[4];
            p->baal_q = q[5];
            p->a5_flags = q[6];
            p->rescued = q[7];
        } else if (sscanf(line, "cowking %d", &a) == 1) {
            p->cow_king = a != 0;
        } else if (sscanf(line, "respec %d", &a) == 1) {
            p->respec_done = a != 0;
        } else if (sscanf(line, "talk %d %d %llu %llu", &a, &b, &talk_mask, &talk_mask2) >= 3 && a >= 0 && a < NPC_TALKERS) {
            p->talk_next[a] = (uint8_t)(b >= 0 && b < GOSSIP_MAX ? b : 0);
            p->talk_heard[a][0] = talk_mask;
            p->talk_heard[a][1] = talk_mask2; /* (las de antes no lo tienen: 0) */
            talk_mask2 = 0;
            got_talk = 1;
        } else if (sscanf(line, "waypoints %llu", &wp_mask) == 1) {
            p->waypoints = wp_mask | 1;
        } else if (sscanf(line, "inact %d", &a) == 1) {
            p->act = a >= 1 && a <= 5 ? a : 1;
        } else if (sscanf(line, "qseen %u", &u) == 1) {
            p->quest_seen = u;
        } else if (sscanf(line, "act1 %d %d %d %d", &q[0], &q[1], &q[2], &q[3]) == 4) {
            p->cain_q = q[0];
            p->malus_q = q[1];
            p->tower_q = q[2];
            p->andy_q = q[3];
        } else if (sscanf(line, "quests %d %d %d %d %lld", &q[0], &q[1], &q[2], &q[3], &met) == 5) {
            p->den_started = q[0];
            p->raven_started = q[1];
            p->raven_done = q[2];
            p->raven_rewarded = q[3];
            /* Las de antes lo guardaban como int: con Malah (el bit 31) quedaba negativo */
            p->npc_met = met < 0 ? (uint32_t)met : (uint64_t)met;
        } else if (sscanf(line, "stash %d %d %d %d", &q[0], &q[1], &q[2], &q[3]) == 4) {
            p->stash_bought = q[0] < 0 ? 0 : q[0] > STASH_BUY_MAX ? STASH_BUY_MAX : q[0];
            p->stash_gold = q[1] > 0 ? q[1] : 0;
            p->act_max = q[2] < 1 ? 1 : q[2] > STASH_ACTS ? STASH_ACTS : q[2];
            p->stash_tab = q[3];
            got_stash = 1;
        } else if (sscanf(line, "stashat %d %d %d %n", &q[0], &a, &b, &used) == 3 && used > 0 &&
                   items_from_text(line + used, &it)) {
            int t = q[0] < 0 ? 0 : q[0] >= STASH_TABS ? STASH_TABS - 1 : q[0], done = stash_add_at(p, t, &it, a, b);
            for (int k = 0; k < STASH_TABS && !done; k++) /* (no debería pasar) */
                done = stash_add(p, k, &it);
            if (!done)
                bag_add(p, &it);
        } else if (sscanf(line, "equip %d %n", &a, &used) == 1 && used > 0 && items_from_text(line + used, &it)) {
            if (a >= 0 && a < SLOT_COUNT && (items_slot_ok(it.type, a) || (a == SLOT_SHIELD && it.type->kind == ITEM_WEAPON)))
                p->equip[a] = it;
        } else if (sscanf(line, "cubeat %d %d %n", &a, &b, &used) == 2 && used > 0 && items_from_text(line + used, &it)) {
            if (!cube_add_at(p, &it, a, b) && !cube_add(p, &it))
                bag_add(p, &it);
        } else if (sscanf(line, "bagat %d %d %n", &a, &b, &used) == 2 && used > 0 && items_from_text(line + used, &it)) {
            if (!bag_add_at(p, &it, a, b))
                bag_add(p, &it);
        } else if (strncmp(line, "bag ", 4) == 0 && items_from_text(line + 4, &it)) {
            bag_add(p, &it);
        } else if (sscanf(line, "belt %d %n", &a, &used) == 1 && used > 0 && items_from_text(line + used, &it)) {
            if (a >= 0 && a < BELT_SLOTS)
                p->belt[a] = it;
        }
    }
    fclose(f);
    /* Partidas de antes de las habilidades: un punto por cada nivel subido */
    if (!got_points)
        p->skill_points = p->level_num - 1;
    /* Partidas de antes de los atributos: los 5 puntos por nivel estaban en
       vitalidad, y la vida de más (la recompensa vieja de Akara) queda aparte */
    if (!got_stats) {
        p->stat_add[ATTR_VIT] = 5 * (p->level_num - 1);
        p->hp_extra = 0;
        refresh_stats(p);
        if (saved_life > p->hp_base)
            p->hp_extra = saved_life - p->hp_base;
    }
    /* La dificultad que abre cada Baal muerto (CLIENTS_UpdateCharacterProgression) */
    for (int d = 0; d < 2; d++)
        if (baal_done[d] && p->diff_max < d + 1)
            p->diff_max = d + 1;
    /* Partidas de antes de que los NPC recordaran lo que dijeron: lo de las
       misiones que ya pasaron cuenta como dicho (si no, lo recitan todo junto) */
    if (!got_talk && s_diff == 0)
        for (int k = 0; k < NPC_TALKERS; k++)
            for (int id = 0; id < QUEST_LINES; id++)
                if (talk_line_due(p, id))
                    talk_heard_set(p, k, id);
    /* Partidas de antes del cofre: el acto más alto, por las misiones */
    if (!got_stash)
        p->act_max = (p->a3_flags & A3F_HELLGATE) ? 4 : (p->a2_flags & A2F_SAILED) ? 3 : p->andy_q >= 3 ? 2 : 1;
    if (p->act > p->act_max)
        p->act_max = p->act;
    if (!got_top) /* (las de antes de las dificultades: en Normal, el más alto que tenían) */
        p->act_top = s_diff == 0 ? p->act_max : p->act;
    if (p->act > p->act_top)
        p->act_top = p->act;
    tomes_to_scrolls(p);
    (void)c;
    printf("partida cargada: %s (nivel %d)\n", path, p->level_num);
    return 1;
}

/* Lo que save_character_to escribe, en memoria (malloc, con su largo; NULL = no se pudo): la campaña de
   la demostración y la prueba de la red. Con open_memstream; en Windows, que no lo tiene, por un temporal */
static char *character_text(const Player *p, size_t *len)
{
    *len = 0;
#ifdef _WIN32
    FILE *f = compat_tmpfile();
    if (!f)
        return NULL;
    save_character_to(f, p);
    long n = ftell(f);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    rewind(f);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = '\0';
        *len = (size_t)n;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
#else
    char *buf = NULL;
    FILE *f = open_memstream(&buf, len);
    if (!f)
        return NULL;
    save_character_to(f, p);
    fclose(f);
    return buf;
#endif
}

/* Un FILE para leer un texto que está en memoria (fmemopen; en Windows, por un temporal) */
static FILE *mem_reader(const char *buf, size_t len)
{
#ifdef _WIN32
    FILE *f = compat_tmpfile();
    if (f) {
        fwrite(buf, 1, len, f);
        rewind(f);
    }
    return f;
#else
    return fmemopen((void *)buf, len, "r");
#endif
}

static int load_character(Player *p)
{
    char path[300];
    int i = (int)(p - s_pl);
    if (i >= 0 && i < MAX_PLAYERS && s_carry[i]) { /* la campaña de la demostración: el de la dificultad anterior */
        FILE *f = mem_reader(s_carry[i], s_carry_len[i]);
        return f ? load_character_from(f, p, "el personaje de la demostración") : 0;
    }
    if (!save_path(p->slot, path, sizeof path))
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    return load_character_from(f, p, path);
}

void game_save(void)
{
    for (int i = 0; i < s_num_players; i++)
        save_character(&s_pl[i]);
}

/* Lo que muestra la pantalla de selección, sin cargar todo */
int game_slot_info(int slot, SlotInfo *info)
{
    char path[300], line[1024];
    memset(info, 0, sizeof *info);
    if (!save_path(slot, path, sizeof path))
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    if (fgets(line, sizeof line, f) && strncmp(line, SAVE_MAGIC, strlen(SAVE_MAGIC)) == 0) {
        info->used = 1;
        info->level = 1;
        char tok[8];
        int a, b;
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "class %7s", tok) == 1) {
                for (int k = 0; k < NUM_CLASSES; k++)
                    if (strcmp(tok, s_classes[k].token) == 0)
                        info->cls = k;
            } else if (sscanf(line, "level %d", &a) == 1) {
                info->level = a;
            } else if (sscanf(line, "gold %d", &a) == 1) {
                info->gold = a;
            } else if (sscanf(line, "den %d %d", &a, &b) == 2) {
                info->den_done = a;
            } else if (sscanf(line, "diffmax %d", &a) == 1) {
                info->diff_max = a < 0 ? 0 : a > 2 ? 2 : a > info->diff_max ? a : info->diff_max;
            } else if (sscanf(line, "eco %d", &a) == 1) {
                info->eco_best = a;
            } else {
                /* (las de antes de las dificultades: Baal muerto en Normal abre Pesadilla) */
                int q[6], d = line[0] == 'd' && line[1] == '1' ? 1 : 0;
                if (sscanf(line + (d ? 3 : 0), "act5 %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5]) == 6 &&
                    q[5] >= 4 && info->diff_max < d + 1)
                    info->diff_max = d + 1;
            }
        }
    }
    fclose(f);
    return info->used;
}

void game_delete_slot(int slot)
{
    char path[300];
    if (save_path(slot, path, sizeof path))
        remove(path);
    game_previews_changed();
}

const char *game_class_name(int cls)
{
    return tr(s_classes[cls >= 0 && cls < NUM_CLASSES ? cls : 0].title);
}

const char *game_class_voice(int cls)
{
    return s_classes[cls >= 0 && cls < NUM_CLASSES ? cls : 0].voice;
}

/* Las partidas de antes de los lugares (ba.sav, am.sav) pasan a los lugares
   1 y 2, con su clase; los archivos viejos quedan como .viejo */
static void migrate_old_saves(void)
{
    char path[300], old[300], line[1024];
    for (int k = 1; k <= SAVE_SLOTS; k++)
        if (save_path(k, path, sizeof path)) {
            FILE *f = fopen(path, "r");
            if (f) {
                fclose(f);
                return; /* ya hay lugares en uso */
            }
        }
    for (int cls = 0; cls < NUM_CLASSES; cls++) {
        snprintf(old, sizeof old, "%s/%s.sav", s_save_dir, s_classes[cls].token);
        FILE *in = fopen(old, "r");
        if (!in || !save_path(cls + 1, path, sizeof path)) {
            if (in)
                fclose(in);
            continue;
        }
        FILE *out = fopen(path, "w");
        if (out) {
            int first = 1;
            while (fgets(line, sizeof line, in)) {
                fputs(line, out);
                if (first)
                    fprintf(out, "class %s\n", s_classes[cls].token);
                first = 0;
            }
            fclose(out);
        }
        fclose(in);
        char renamed[310];
        snprintf(renamed, sizeof renamed, "%s.viejo", old);
        rename(old, renamed);
        printf("partida vieja %s -> %s\n", old, path);
    }
}

/* ---- Personajes para la pantalla de selección ----
   Se carga la partida en un jugador de prueba y solo la animación de estar
   parado en el pueblo, con su equipo. */
#define PREVIEWS (SAVE_SLOTS + 1 + NUM_CLASSES)
static CharAnim s_preview[PREVIEWS];
static int      s_preview_state[PREVIEWS]; /* 0 = sin cargar, 1 = bien, -1 = no hay */

void game_previews_changed(void)
{
    for (int k = 0; k < PREVIEWS; k++) {
        if (s_preview_state[k] > 0)
            charanim_free(&s_preview[k]);
        s_preview_state[k] = 0;
    }
}

static void load_preview(int k, int slot, int cls)
{
    static Player tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.cls = cls;
    tmp.slot = slot;
    tmp.dialog_npc = -1;
    if (slot > 0) {
        if (!load_character(&tmp)) {
            s_preview_state[k] = -1;
            return;
        }
    } else {
        give_start_items(&tmp);
    }
    CharLook look;
    char wclass[4];
    build_look(&tmp, &look, wclass, sizeof wclass);
    s_preview_state[k] = charanim_load(&s_preview[k], &look, "tn", wclass) == 0 ||
                         charanim_load(&s_preview[k], &look, "tn", "hth") == 0 ? 1 : -1;
}

static void draw_preview(int k, int x, int y, int tick)
{
    if (s_preview_state[k] <= 0)
        return;
    const CharAnim *a = &s_preview[k];
    int frames = charanim_frames(a);
    int frame = frames > 0 ? (tick * (a->speed > 0 ? a->speed : 256) / 256) % frames : 0;
    charanim_draw(a, 0, frame, x, y);
}

void game_draw_slot_hero(int slot, int x, int y, int tick)
{
    if (slot < 1 || slot > SAVE_SLOTS)
        return;
    if (s_preview_state[slot] == 0) {
        SlotInfo info;
        if (game_slot_info(slot, &info))
            load_preview(slot, slot, info.cls);
        else
            s_preview_state[slot] = -1;
    }
    draw_preview(slot, x, y, tick);
}

void game_draw_class_hero(int cls, int x, int y, int tick)
{
    if (cls < 0 || cls >= NUM_CLASSES)
        return;
    int k = SAVE_SLOTS + 1 + cls;
    if (s_preview_state[k] == 0)
        load_preview(k, 0, cls);
    draw_preview(k, x, y, tick);
}

/* ---- Misiones 3 a 6 del Acto 1 ----

   La Búsqueda de Caín (Akara): el Pergamino de Inifuss en el árbol del
   Dark Wood, Akara lo traduce, las Piedras Cairn del Stony Field se tocan en
   el orden del pergamino y abren un portal a Tristram, donde Caín espera en
   la jaula. Herramientas del Oficio (Charsi): el Mazo de Horadric en la
   forja de los Barracones; al devolverlo, Charsi vuelve raro un objeto. La
   Torre Olvidada: el tomo del Black Marsh y la Condesa en el quinto sótano.
   Hermanas de la Masacre (Caín): matar a Andariel y viajar al este con
   Warriv. Los jefes y los objetos cuentan para todos los jugadores; los
   objetos de misión, para el que los levanta. */

static const char *const k_stones[5] = { "StoneAlpha", "StoneBeta", "StoneGamma", "StoneDelta", "StoneLambda" };
static int   s_stone_order[5];         /* el orden del pergamino (al azar en cada partida) */
static int   s_stones_lit;             /* cuántas se tocaron en orden */
static float s_cairn_x = -1, s_cairn_y; /* el centro de las piedras: ahí aparece el portal */
static int   s_countess_dead, s_andariel_dead;
static int   s_cain_unit = -1;         /* Caín en el campamento, después de rescatarlo */
#define PORTAL_TRISTRAM 0
#define PORTAL_CAIRN    1
/* El Acto 2: del sótano 3 al Santuario y de vuelta (siempre abiertos), del
   diario de Horazon al Canyon y de vuelta, del orificio a la guarida de
   Duriel y el de Tyrael al pueblo */
#define PORTAL_ARCANE_IN   2
#define PORTAL_ARCANE_OUT  3
#define PORTAL_CANYON      4
#define PORTAL_CANYON_BACK 5
#define PORTAL_DURIEL      6
#define PORTAL_TYRAEL      7

/* ---- El portal al pueblo ----
   El Pergamino de Portal (o una carga del tomo) abre un portal azul al lado
   del jugador y otro en el pueblo, unidos. Como en el original, cada jugador
   tiene uno (abrir otro cierra el anterior), cualquiera puede usarlo, y se
   cierra cuando el que lo abrió vuelve por él desde el pueblo. No se guarda
   con la partida (el original tampoco: es de esa partida). */
#define PORTAL_TP_FIELD(i) (16 + 2 * (i)) /* (los ids de world_portal de cada jugador) */
#define PORTAL_TP_TOWN(i)  (17 + 2 * (i))
typedef struct {
    int   open;
    float fx, fy; /* el de afuera */
    float tx, ty; /* el del pueblo */
} TownPortal;
static TownPortal s_tp[MAX_PLAYERS];

/* Pone (o saca) el dibujo del portal en (x, y), si su zona está armada */
static void tp_object(float x, float y, int on)
{
    int ids[16], n = units_find_all("TownPortal", ids, 16);
    for (int k = 0; k < n; k++) {
        float ux, uy;
        units_position(ids[k], 1.0f, &ux, &uy);
        if (fabsf(ux - x) < 0.5f && fabsf(uy - y) < 0.5f) {
            if (!on)
                units_remove(ids[k]);
            return;
        }
    }
    int z = world_zone_at(&s_map, x, y);
    if (!on || z < 0 || !world_zone_loaded(z))
        return;
    units_zone_begin(z);
    units_spawn_object("TownPortal", x, y, "on");
    units_zone_end();
}

static void tp_close(int i)
{
    if (!s_tp[i].open)
        return;
    tp_object(s_tp[i].fx, s_tp[i].fy, 0);
    tp_object(s_tp[i].tx, s_tp[i].ty, 0);
    world_portal(PORTAL_TP_FIELD(i), -1, 0, 0, -1, 0, 0);
    world_portal(PORTAL_TP_TOWN(i), -1, 0, 0, -1, 0, 0);
    s_tp[i].open = 0;
}

static void tp_reset(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        s_tp[i].open = 1; /* (para que tp_close saque los saltos) */
        tp_close(i);
    }
}

/* Una zona que se arma de nuevo: los dibujos de los portales que caen ahí */
static void tp_zone_loaded(int z)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (s_tp[i].open) {
            if (world_zone_at(&s_map, s_tp[i].fx, s_tp[i].fy) == z)
                tp_object(s_tp[i].fx, s_tp[i].fy, 1);
            if (world_zone_at(&s_map, s_tp[i].tx, s_tp[i].ty) == z)
                tp_object(s_tp[i].tx, s_tp[i].ty, 1);
        }
}

/* Alguien cruzó un portal: si era el que lo abrió volviendo desde el pueblo, se cierra */
static void tp_used(const Player *p, const Warp *w)
{
    int i = (int)(p - s_pl);
    if (i >= 0 && i < MAX_PLAYERS && s_tp[i].open && w->portal == PORTAL_TP_TOWN(i) + 1)
        tp_close(i);
}

/* Prueba: D2_TPTEST=1 (con D2_START afuera del pueblo) abre un portal con
   un pergamino, entra, llega al pueblo y vuelve por el mismo, que se cierra */
static int  tp_find(const Player *p);
static int  tp_portal_objects(void)
{
    int ids[16];
    return units_find_all("TownPortal", ids, 16);
}

static const Warp *tp_warp(int id)
{
    for (int k = 0; k < world_warp_count(); k++)
        if (world_warp(k)->portal == id + 1)
            return world_warp(k);
    return NULL;
}

static void tp_test(void)
{
    static int step;
    if (!getenv("D2_TPTEST") || step < 0)
        return;
    Player *p = &s_pl[0];
    const Warp *w;
    switch (step) {
    case 0: {
        if (s_ticks < 10)
            return;
        const ItemType *t = items_find("tsc");
        Item it = items_make(t);
        bag_add(p, &it);
        int ok = tp_cast(p, tp_find(p));
        printf("TP: en %s, abrir: %s; portal de afuera (%.1f, %.1f), del pueblo (%.1f, %.1f); dibujos: %d\n",
               p->level ? p->level->name : "?", ok ? "sí" : "no", s_tp[0].fx, s_tp[0].fy, s_tp[0].tx, s_tp[0].ty,
               tp_portal_objects());
        step = ok ? 1 : -1;
        return;
    }
    case 1:
        if (s_ticks < 20 || !(w = tp_warp(PORTAL_TP_FIELD(0))))
            return;
        use_warp(p, w);
        printf("TP: entró: está en %s (%.1f, %.1f), abierto %d\n", p->level ? p->level->name : "?", p->x, p->y,
               s_tp[0].open);
        step = 2;
        return;
    case 2:
        if (s_ticks < 30 || !(w = tp_warp(PORTAL_TP_TOWN(0))))
            return;
        use_warp(p, w);
        printf("TP: volvió: está en %s (%.1f, %.1f), abierto %d, saltos %s, dibujos %d\n",
               p->level ? p->level->name : "?", p->x, p->y, s_tp[0].open,
               tp_warp(PORTAL_TP_FIELD(0)) || tp_warp(PORTAL_TP_TOWN(0)) ? "quedaron" : "cerrados", tp_portal_objects());
        step = -1;
        return;
    }
}

/* El lugar de la mochila con Pergaminos de Portal (la pila más chica, así se
   gasta primero), o -1 */
static int tp_find(const Player *p)
{
    int best = -1;
    for (int k = 0; k < p->bag_count; k++)
        if (strcmp(p->bag[k].type->code, "tsc") == 0 && (best < 0 || p->bag[k].amount < p->bag[best].amount))
            best = k;
    return best;
}

/* El del pueblo: cerca de donde se aparece, en un lugar libre lejos de los
   NPC (con A se les habla: en Kurast caía al lado de Hratli y no se podía
   entrar) y del portal del otro jugador */
static void tp_town_spot(int i, int town, float *x, float *y)
{
    for (int r = 6; r < 18; r++) /* (empezando hacia abajo en la pantalla: lejos de la fogata del campamento) */
        for (int a = 0; a < 16; a++) {
            float ang = PI_F / 4 + (float)((a + 1) / 2) * (a % 2 ? 1.0f : -1.0f) * PI_F / 8 + (float)i * 0.6f,
                  cx = s_town_x + cosf(ang) * r,
                  cy = s_town_y + sinf(ang) * r;
            const TownPortal *o = &s_tp[i ^ 1];
            int open = 1; /* lugar abierto alrededor (que no tape la fogata ni una pared) */
            for (int dy = -3; dy <= 3 && open; dy++)
                for (int dx = -3; dx <= 3 && open; dx++)
                    open = !map_blocked(&s_map, (int)floorf(cx) + dx, (int)floorf(cy) + dy);
            if (!open || world_zone_at(&s_map, cx, cy) != town || blocked_body(cx, cy) || units_blocking(cx, cy, 4.0f) ||
                (MAX_PLAYERS > 1 && o->open && fabsf(o->tx - cx) < 3.0f && fabsf(o->ty - cy) < 3.0f))
                continue;
            *x = cx;
            *y = cy;
            return;
        }
    *x = s_town_x + 4.0f + 3.0f * i; /* (no debería pasar) */
    *y = s_town_y - 3.0f;
    find_free_spot(x, y);
}

/* Abre el portal con lo que hay en ese lugar de la mochila. 1 si se abrió. */
static int tp_cast(Player *p, int bag_index)
{
    if (bag_index < 0 || bag_index >= p->bag_count)
        return 0;
    Item *it = &p->bag[bag_index];
    if (s_eco) {
        show_message(p, "En el Eco del Odio no hay portales");
        return 0;
    }
    if (!p->level || p->level->town) {
        show_message(p, "En el pueblo no hace falta");
        return 0;
    }
    int town = world_zone_at(&s_map, s_town_x, s_town_y), zone = world_zone_at(&s_map, p->x, p->y);
    if (town < 0 || zone < 0)
        return 0;
    if (it->amount > 1) /* de la pila, uno */
        it->amount--;
    else
        bag_remove(p, bag_index);
    return tp_open(p);
}

/* El portal al pueblo al lado del jugador (el pergamino, el tomo, el
   santuario de portal). 1 si se abrió. */
static int tp_open(Player *p)
{
    int i = (int)(p - s_pl);
    int town = world_zone_at(&s_map, s_town_x, s_town_y), zone = world_zone_at(&s_map, p->x, p->y);
    if (s_eco || town < 0 || zone < 0 || !p->level || p->level->town)
        return 0;
    tp_close(i);
    /* El de afuera, al lado (en la misma zona); el del pueblo, cerca de donde
       se aparece, uno al lado del otro */
    float fx = p->x + 1.0f, fy = p->y + 1.0f, tx, ty;
    reach_free_spot(p->x, p->y, &fx, &fy); /* (del mismo lado de las paredes que el jugador) */
    if (world_zone_at(&s_map, fx, fy) != zone) {
        fx = p->x;
        fy = p->y;
    }
    float ax = fx + 1.5f, ay = fy + 1.5f; /* adónde se vuelve: al lado del portal, y se llega caminando hasta él */
    reach_free_spot(fx, fy, &ax, &ay);
    if (world_zone_at(&s_map, ax, ay) != zone) {
        ax = fx;
        ay = fy;
    }
    tp_town_spot(i, town, &tx, &ty);
    world_portal(PORTAL_TP_FIELD(i), zone, fx, fy, town, tx + 1.5f, ty + 1.5f);
    world_portal(PORTAL_TP_TOWN(i), town, tx, ty, zone, ax, ay);
    s_tp[i].open = 1;
    s_tp[i].fx = fx;
    s_tp[i].fy = fy;
    s_tp[i].tx = tx;
    s_tp[i].ty = ty;
    tp_object(fx, fy, 1);
    tp_object(tx, ty, 1);
    audio_play_name("player_townportal_cast", p->x, p->y);
    audio_play_name("object_townportal", fx, fy);
    if (s_act == 5)
        a5_portal_opened(p); /* (en la cumbre: los Antiguos) */
    return 1;
}
#define QUEST_RANGE     4.0f /* a esta distancia (subtiles) se tocan los objetos de las misiones */

static const char *const k_quest_objects[] = {
    "InifussTree", "StoneAlpha", "StoneBeta", "StoneGamma", "StoneDelta", "StoneLambda", "Gibbet", "Malus", "TowerTome",
    "HoradricScrollChest", "HoradricCubeChest", "StaffOfKingsChest", "TaintedSunShrine", "ArcaneTome",
    "SevenTombsReceptacle",
    "LamesEnsTome", "GidBInnDecoy", "KhalimEyeChest", "KhalimBrainChest", "KhalimHeartChest", "BeneathTheCityLever",
    "CompellingOrb", "HellGate", /* el Acto 3 (a3quest.inc) */
    "DiabloSeal1", "DiabloSeal2", "DiabloSeal3", "DiabloSeal4", "DiabloSeal5", "SoulStoneForge",
    "ReallyTheLastPortal", /* el Acto 4 (a4quest.inc) */
    "AnyaYetAgain", "AncientsAltar", "NotKiddingLastPortal", /* el Acto 5 (a5quest.inc) */
};

/* El Acto 2: los cofres de las piezas del Báculo y el altar del Sol
   Mancillado (se abren una vez por partida: s_a2_opened) */
static const struct { const char *cls; int bit; } k_a2_objects[] = {
    { "HoradricScrollChest", 1 }, { "HoradricCubeChest", 2 }, { "StaffOfKingsChest", 4 }, { "TaintedSunShrine", 8 },
};
static int   s_a2_opened;    /* los de k_a2_objects que ya se abrieron */
static int   s_summ_dead, s_duriel_dead; /* el Invocador y Duriel murieron en esta partida */
static float s_tome_x = -1, s_tome_y, s_orifice_x = -1, s_orifice_y, s_tyrael_x = -1, s_tyrael_y;
static float s_tyr_portal_x = -1, s_tyr_portal_y; /* el portal de Tyrael: donde estaba el que le habló */
static int   s_tyr_told;     /* 1: Tyrael abrió el portal en esta partida; 2: ya dijeron la frase de las Tumbas */
static int   s_rada_dead;    /* Radament murió en esta partida */
static int   s_sun_timer;    /* ticks hasta que se oscurece el sol (0 = no corre) */

static int a2_object_bit(const char *cls)
{
    for (unsigned k = 0; k < sizeof k_a2_objects / sizeof k_a2_objects[0]; k++)
        if (strcmp(cls, k_a2_objects[k].cls) == 0)
            return k_a2_objects[k].bit;
    return 0;
}

/* El estado más avanzado de una misión entre los jugadores */
/* main.c pasa la cinemática del fin de acto después del tick */
static void queue_movie(const char *name)
{
    s_pending_movie = name;
    if (s_debug || getenv("D2_AT_TRACE"))
        printf("cinemática pedida: %s\n", name);
}

const char *game_take_movie(void)
{
    const char *m = s_pending_movie;
    s_pending_movie = NULL;
    return m;
}

/* ¿Alguno de los jugadores no tiene esa marca? (la primera vez de algo) */
static int party_lacks(size_t field, int bit)
{
    for (int i = 0; i < s_num_players; i++)
        if (!(*(const int *)((const char *)&s_pl[i] + field) & bit))
            return 1;
    return 0;
}

static int party_max(size_t field)
{
    int m = 0;
    for (int i = 0; i < s_num_players; i++) {
        int v = *(const int *)((const char *)&s_pl[i] + field);
        if (v > m)
            m = v;
    }
    return m;
}

static void party_raise(size_t field, int value)
{
    for (int i = 0; i < s_num_players; i++) {
        int *v = (int *)((char *)&s_pl[i] + field);
        if (*v < value)
            *v = value;
    }
}

static int is_quest_item(const ItemType *t)
{
    static const char *const k_codes[] = { "bks", "bkd", "hdm", "box", "tr1", "ass", "msf", "vip", "hst",
                                           "j34", "g34", "bbb", "xyz", "g33", "qey", "qhr", "qbr", "qf1", "qf2",
                                           "mss", "hfh", /* (los del Acto 4: la Piedra de Mefisto y el Martillo) */
                                           "ice", "tr2" }; /* (y del 5: la poción de Malah y el Pergamino de Resistencia) */
    for (unsigned k = 0; t && k < sizeof k_codes / sizeof k_codes[0]; k++)
        if (strcmp(t->code, k_codes[k]) == 0)
            return 1;
    return 0;
}

static int bag_index_of(const Player *p, const char *code)
{
    for (int i = 0; i < p->bag_count; i++)
        if (p->bag[i].type && strcmp(p->bag[i].type->code, code) == 0)
            return i;
    return -1;
}

/* Le da un objeto de misión; si no le entra, queda en el suelo a sus pies */
static void give_quest_item(Player *p, const char *code)
{
    const ItemType *t = items_find(code);
    if (!t)
        return;
    Item it = items_make(t);
    items_quest_unique(&it);
    if (!bag_add(p, &it)) {
        items_ground_add(&it, p->x + 0.5f, p->y + 0.5f);
        inventory_full(p);
    }
    audio_play(t->drop_sound, p->x, p->y);
}

/* Deja en el suelo un objeto de misión (o el oro de un cofre: amount) */
static void drop_at(const char *code, int amount, float x, float y)
{
    const ItemType *t = items_find(code);
    if (!t)
        return;
    Item it = items_make(t);
    items_quest_unique(&it);
    if (amount > 0)
        it.amount = amount;
    items_ground_add(&it, x, y);
}

/* Lo que sale al abrir los cofres del Acto 2 o romper el altar (como las
   OperateFunction del original: la pieza para cada uno que todavía la
   necesita, y oro); el altar además devuelve el sol */
static void a2_object_open(int bit, float x, float y)
{
    static const struct { int bit; const char *code; } k_drops[] = {
        { 1, "tr1" }, { 2, "box" }, { 4, "msf" }, { 8, "vip" },
    };
    int k = 0;
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        for (unsigned d = 0; d < sizeof k_drops / sizeof k_drops[0]; d++) {
            if (k_drops[d].bit != bit || player_has(p, k_drops[d].code))
                continue;
            int need = bit == 1   ? p->staff_q == 0 && !(p->staff_seen & A2_SEEN_SCROLL)
                       : bit == 2 ? 1
                                  : p->staff_q < 3 && !player_has(p, "hst");
            if (need)
                drop_at(k_drops[d].code, 0, x + 0.8f * (float)k++, y + 0.6f);
        }
    }
    for (int g = 0; g < 2 + (int)(rnd() % 3u); g++) /* el oro ('dlg' del original) */
        drop_at("gld", 20 + (int)(rnd() % 80u), x - 1.0f + 0.7f * (float)g, y - 0.7f);
    if (bit == 8) { /* el altar: vuelve la luz y la misión queda por contar en el pueblo */
        int any = 0;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].sun_q < 3) {
                s_pl[i].sun_q = 3;
                any = 1;
            }
        if (any) {
            quest_banner("Volvió el sol: contáselo a la gente de Lut Gholein", "music_quest_compelling");
            for (int i = 0; i < s_num_players; i++)
                player_say(&s_pl[i], "act2_complete_tainted");
        }
    }
    if (bit == 4) /* el Bastón de los Reyes, en la guarida de la reina de los gusanos */
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act2_complete_tightspot");
}

static void quest_banner(const char *text, const char *music)
{
    s_quest_ticks = 5 * TICK_RATE;
    snprintf(s_quest_banner, sizeof s_quest_banner, "%s", tr(text));
    audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
    if (music)
        audio_music_event(music);
}

static void quest_done(const char *quest_key, const char *music)
{
    char text[96];
    snprintf(text, sizeof text, tr("%s: ¡misión cumplida!"), str_get(quest_key));
    quest_banner(text, music);
}

/* Una frase con su voz (vo.json), como cuando habla un NPC, pero sin
   diálogo: el tomo, Caín en la jaula */
static void play_vo(Player *p, const char *key)
{
    char voice[160];
    if (str_voice_path(key, voice, sizeof voice) == 0 && audio_voice(voice) > 0)
        s_voice_owner = (int)(p - s_pl);
}

/* Donde se llega a Tristram: la marca del original, cerca del pueblo quemado */
static int tristram_arrival(float *x, float *y)
{
    const Level *t = world_level_named("Tristram");
    if (!t)
        return -1;
    *x = (t->x0 + 34) * TILE_SUB + 2.5f;
    *y = (t->y0 + 16) * TILE_SUB + 2.5f;
    return 0;
}

/* Los dos portales rojos: de las piedras a Tristram y de vuelta */
static void open_tristram_portals(void)
{
    float tx, ty;
    if (s_cairn_x < 0 || tristram_arrival(&tx, &ty) != 0)
        return;
    int stony = world_zone_at(&s_map, s_cairn_x, s_cairn_y), tri = world_zone_at(&s_map, tx, ty);
    world_portal(PORTAL_TRISTRAM, stony, s_cairn_x, s_cairn_y, tri, tx + 3.0f, ty + 3.0f);
    world_portal(PORTAL_CAIRN, tri, tx, ty, stony, s_cairn_x + 3.0f, s_cairn_y + 3.0f);
}

static void spawn_portal_objects(int z);

/* Después de los rayos de las Piedras Cairn: el portal rojo a Tristram */
static void cairn_portal_open(int zone)
{
    open_tristram_portals();
    spawn_portal_objects(zone);
    audio_play_name("object_xportalopen", s_cairn_x, s_cairn_y);
    quest_banner("Se abrió un portal a Tristram", NULL);
}

/* El dibujo del portal en la zona z, si el portal está abierto */
static void spawn_portal_objects(int z)
{
    float tx, ty;
    if (party_max(offsetof(Player, cain_q)) < 4 || s_cairn_x < 0 || tristram_arrival(&tx, &ty) != 0)
        return;
    units_zone_begin(z);
    if (world_zone_at(&s_map, s_cairn_x, s_cairn_y) == z)
        units_spawn_object("PortalPermanent", s_cairn_x, s_cairn_y, "on");
    if (world_zone_at(&s_map, tx, ty) == z)
        units_spawn_object("PortalPermanent", tx, ty, "on");
    units_zone_end();
}

/* El palacio se abre al terminar el Sol Mancillado (o cuando Drognan da el pedido) */
static int palace_open(void)
{
    return party_max(offsetof(Player, sun_q)) >= 4 || party_max(offsetof(Player, arcane_q)) >= 1;
}

/* La tumba verdadera de Tal Rasha de esta partida: su glifo (los objetos
   ArcaneRune1..7, objects/7a..7g). ACT2Q6_GetObjectIdForArcaneThing: de la
   tumba 1 a la 7 son los objetos 313, 312, 308, 310, 311, 309 y 307 */
static int tomb_rune(int tomb_id)
{
    static const int k_rune[7] = { 7, 6, 2, 4, 5, 3, 1 };
    return tomb_id >= 66 && tomb_id <= 72 ? k_rune[tomb_id - 66] : 1;
}

/* Los portales del Acto 2 que corresponden a cómo van las misiones, y sus
   dibujos en la zona z (-1 = en las que estén armadas) */
static void a2_portal_object(int z, const char *cls, float x, float y)
{
    if (z >= 0 && world_zone_at(&s_map, x, y) != z)
        return;
    int zz = world_zone_at(&s_map, x, y);
    if (!world_zone_loaded(zz))
        return;
    /* Ya está (a2_open_portals se llama en cada paso de las misiones y
       volvía a poner los portales abiertos de las zonas armadas, uno encima
       del otro) */
    int ids[16], n = units_find_all(cls, ids, 16);
    for (int k = 0; k < n; k++) {
        float ux, uy;
        units_position(ids[k], 1.0f, &ux, &uy);
        if (fabsf(ux - x) < 0.5f && fabsf(uy - y) < 0.5f)
            return;
    }
    units_zone_begin(zz);
    units_spawn_object(cls, x, y, "on");
    units_zone_end();
}

static void a2_open_portals(int z)
{
    if (s_act != 2)
        return;
    float cx, cy, sx, sy, kx, ky;
    int cellar = world_level_zone(54), arcane = world_level_zone(74), canyon = world_level_zone(46);
    int lair = world_level_zone(73), staff = world_level_zone(world_staff_tomb());
    if (world_act2_spot(0, &cx, &cy) == 0 && world_act2_spot(1, &sx, &sy) == 0) {
        world_portal(PORTAL_ARCANE_IN, cellar, cx, cy, arcane, sx + 2.0f, sy + 3.0f);
        world_portal(PORTAL_ARCANE_OUT, arcane, sx, sy, cellar, cx - 3.0f, cy + 2.0f);
    }
    /* dónde quedaron los objetos (se saben al armar su zona) */
    int id;
    if (units_find_all("ArcaneTome", &id, 1) == 1 && units_zone(id) == arcane)
        units_position(id, 1.0f, &s_tome_x, &s_tome_y);
    if (units_find_all("SevenTombsReceptacle", &id, 1) == 1 && units_zone(id) == staff)
        units_position(id, 1.0f, &s_orifice_x, &s_orifice_y);
    if (units_find_all("tyrael1", &id, 1) == 1 && units_zone(id) == lair)
        units_position(id, 1.0f, &s_tyrael_x, &s_tyrael_y);
    /* el del diario al Canyon (rojo, de los dos lados) */
    if (party_max(offsetof(Player, arcane_q)) >= 4 && s_tome_x >= 0 && world_act2_spot(2, &kx, &ky) == 0) {
        world_portal(PORTAL_CANYON, arcane, s_tome_x + 3.0f, s_tome_y + 3.0f, canyon, kx + 3.0f, ky + 2.0f);
        world_portal(PORTAL_CANYON_BACK, canyon, kx, ky, arcane, s_tome_x + 4.5f, s_tome_y + 1.5f);
        if (z != -2) {
            a2_portal_object(z, "PortalPermanent", s_tome_x + 3.0f, s_tome_y + 3.0f);
            a2_portal_object(z, "PortalPermanent", kx, ky);
        }
    }
    /* el del orificio a la guarida (OBJECT_PORTAL_TO_DURIELS_LAIR, 13 al oeste y 3 al sur) */
    if (party_max(offsetof(Player, staff_q)) >= 3 && s_orifice_x >= 0 && lair >= 0) {
        float ax, ay;
        lair_arrival(lair, &ax, &ay);
        world_portal(PORTAL_DURIEL, staff, s_orifice_x - 13.0f, s_orifice_y + 3.0f, lair, ax, ay);
        if (z != -2)
            a2_portal_object(z, "DurielPortal", s_orifice_x - 13.0f, s_orifice_y + 3.0f);
    }
    /* el de Tyrael al pueblo: al lado del que le habló (en una partida
       cargada, en el lugar libre más cerca de Tyrael) */
    if (party_max(offsetof(Player, tombs_q)) >= 4 && s_tyrael_x >= 0) {
        if (s_tyr_portal_x < 0 && world_zone_loaded(lair)) {
            s_tyr_portal_x = s_tyrael_x;
            s_tyr_portal_y = s_tyrael_y + 1.5f;
            find_free_spot(&s_tyr_portal_x, &s_tyr_portal_y);
        }
        world_portal(PORTAL_TYRAEL, lair, s_tyr_portal_x, s_tyr_portal_y, world_zone_at(&s_map, s_town_x, s_town_y),
                     s_town_x, s_town_y + 4.0f);
        if (z != -2)
            a2_portal_object(z, "TownPortal", s_tyr_portal_x, s_tyr_portal_y);
    }
}

/* Junto al Invocador flotan los glifos de las otras seis tumbas, en los
   lugares de PlaceArcaneThingamajig (la verdadera es la que falta) */
static void arcane_runes(int z)
{
    int spots[8], n = units_find_all("PlaceArcaneThingamajig", spots, 8), k = 0;
    for (int t = 66; t <= 72; t++) {
        if (t == world_staff_tomb())
            continue;
        while (k < n && units_zone(spots[k]) != z)
            k++;
        if (k >= n)
            return;
        float x, y;
        char cls[16];
        units_position(spots[k++], 1.0f, &x, &y);
        snprintf(cls, sizeof cls, "ArcaneRune%d", tomb_rune(t));
        units_zone_begin(z);
        units_spawn_object(cls, x, y, NULL);
        units_zone_end();
    }
}

static void object_done(int z, const char *cls, int done)
{
    int ids[4], n = units_find_all(cls, ids, 4);
    for (int k = 0; k < n; k++)
        if (done && units_zone(ids[k]) == z)
            units_object_mode(ids[k], "on", NULL);
}

/* Una zona recién armada muestra cómo van las misiones: el árbol vacío, las
   piedras encendidas y el portal, la jaula abierta, el Mazo sacado, el
   tomo leído */
/* Hratli ya se presentó (A3Q0): alguno de los dos lo escuchó */
static int hratli_moved(void)
{
    int moved = 0;
    for (int i = 0; i < s_num_players; i++)
        moved |= s_pl[i].a3_flags & A3F_HRATLI;
    return moved;
}

static void quest_zone_loaded(int z)
{
    if (s_eco) { /* el Eco del Odio: ni misiones ni portales; la arena, sin sus monstruos */
        eco_zone_loaded(z);
        return;
    }
    if (s_act == 3)
        a3_zone_loaded(z);
    if (s_act == 4)
        a4_zone_loaded(z);
    if (s_act == 5)
        a5_zone_loaded(z);
    if (s_act == 1)
        cow_zone_loaded(z);
    if (s_act == 5)
        uber_zone_loaded(z);
    int cain = party_max(offsetof(Player, cain_q));
    float sx = 0, sy = 0;
    int found = 0;
    for (int k = 0; k < 5; k++) {
        int id;
        float x, y;
        if (units_find_all(k_stones[k], &id, 1) != 1 || units_zone(id) != z)
            continue;
        units_position(id, 1.0f, &x, &y);
        sx += x;
        sy += y;
        found++;
        if (cain >= 4)
            units_object_mode(id, "on", NULL);
    }
    if (found == 5) {
        s_cairn_x = sx / 5;
        s_cairn_y = sy / 5;
        if (cain < 4)
            s_stones_lit = 0;
    }
    if (cain >= 4) {
        open_tristram_portals();
        spawn_portal_objects(z);
    }
    tp_zone_loaded(z); /* los portales al pueblo que caen ahí */
    object_done(z, "InifussTree", cain >= 2);
    object_done(z, "Gibbet", cain >= 5);
    object_done(z, "Malus", party_max(offsetof(Player, malus_q)) >= 2);
    object_done(z, "TowerTome", party_max(offsetof(Player, tower_q)) >= 1);
    for (unsigned k = 0; k < sizeof k_a2_objects / sizeof k_a2_objects[0]; k++)
        object_done(z, k_a2_objects[k].cls, (s_a2_opened & k_a2_objects[k].bit) != 0);
    if (s_act == 2) {
        a2_open_portals(z);
        if (z == world_level_zone(74))
            arcane_runes(z);
        object_done(z, "SevenTombsReceptacle", party_max(offsetof(Player, staff_q)) >= 3);
        int door; /* la puerta de Tyrael: abierta si Duriel ya murió en esta partida */
        if (s_duriel_dead && units_find_all("DurielsDoor", &door, 1) == 1 && units_zone(door) == z) {
            units_object_mode(door, "on", NULL);
            units_object_unblock(&s_map, door);
        }
    }
    /* Lut Gholein, como ACT2Q4_InitializeJerhyn*Object: Jerhyn espera en su
       marcador del pueblo y Kaelan cuida la entrada del palacio, al lado del
       otro, hasta que muere Duriel.
       shortcut: Jerhyn no entra al palacio después de hablarle (se queda en su marcador) */
    static const struct { const char *mark, *npc; float dx; } k_marks[] = {
        { "JerhynPlaceholder1", "jerhyn", 0.0f }, { "JerhynPlaceholder2", "act2guard2", 1.0f },
    };
    for (unsigned k = 0; k < sizeof k_marks / sizeof k_marks[0]; k++) {
        int id, npc;
        float x, y;
        if (units_find_all(k_marks[k].mark, &id, 1) != 1 || units_zone(id) != z ||
            units_find_all(k_marks[k].npc, &npc, 1) == 1)
            continue;
        units_position(id, 1.0f, &x, &y);
        units_zone_begin(z);
        units_spawn_npc(k_marks[k].npc, x + k_marks[k].dx, y);
        units_zone_end();
    }
    /* El muelle de Kurast, como OBJECTS_InitFunction49/50 (A3Q0): Hratli
       espera en el muelle hasta que se presenta y después está en su fragua */
    int id, npc;
    float hx, hy;
    if (s_act == 3 && units_find_all(hratli_moved() ? "HratliEnd" : "HratliStart", &id, 1) == 1 && units_zone(id) == z &&
        units_find_all("hratli", &npc, 1) != 1) {
        units_position(id, 1.0f, &hx, &hy);
        units_zone_begin(z);
        units_spawn_npc("hratli", hx, hy);
        units_zone_end();
    }
}

#include "a3quest.inc"
#include "a4quest.inc"
#include "a5quest.inc"
static int bots_playing(void); /* la demostración o una prueba automática (autotest.inc) */
#include "objects.inc" /* los cofres, barriles, santuarios y pozos */
#include "cows.inc"    /* el nivel de las vacas */
#include "endgame.inc" /* el evento Pandemonium */
#include "eco.inc"     /* el modo Eco del Odio */

/* El objeto de misión más cercano al jugador (o -1) y qué se puede hacer
   con él; NULL si nada (ya está hecho o todavía no toca) */
static const char *quest_action(const Player *p, int *unit)
{
    int best = -1;
    float best_d = QUEST_RANGE * QUEST_RANGE;
    for (unsigned k = 0; k < sizeof k_quest_objects / sizeof k_quest_objects[0]; k++) {
        int ids[4], n = units_find_all(k_quest_objects[k], ids, 4);
        for (int i = 0; i < n; i++) {
            float x, y;
            units_position(ids[i], 1.0f, &x, &y);
            float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
            if (d < best_d) {
                best_d = d;
                best = ids[i];
            }
        }
    }
    *unit = best;
    if (best < 0)
        return NULL;
    const char *cls = units_id(best);
    int cain = party_max(offsetof(Player, cain_q));
    static char text[80];
    if (s_act == 3)
        return a3_action(p, cls);
    if (s_act == 4)
        return a4_action(p, cls);
    if (s_act == 5)
        return a5_action(p, cls);
    if (strcmp(cls, "InifussTree") == 0 && cain < 2) {
        const ItemType *t = items_find("bks");
        snprintf(text, sizeof text, tr("A: Tomar el %s"), t ? t->name : "pergamino");
        return text;
    }
    if (strncmp(cls, "Stone", 5) == 0 && cain == 3 && s_stones_lit < 5 &&
        strcmp(cls, k_stones[s_stone_order[s_stones_lit]]) == 0) {
        snprintf(text, sizeof text, tr("A: Tocar la piedra (%d de 5)"), s_stones_lit + 1);
        return text;
    }
    if (strcmp(cls, "Gibbet") == 0 && cain == 4)
        return tr("A: Liberar a Deckard Caín");
    if (strcmp(cls, "Malus") == 0 && party_max(offsetof(Player, malus_q)) < 2) {
        const ItemType *t = items_find("hdm");
        snprintf(text, sizeof text, tr("A: Tomar el %s"), t ? t->name : tr("Mazo"));
        return text;
    }
    if (strcmp(cls, "TowerTome") == 0 && party_max(offsetof(Player, tower_q)) < 1)
        return tr("A: Leer el tomo");
    int bit = a2_object_bit(cls);
    if (bit && !(s_a2_opened & bit))
        return bit == 8 ? tr("A: Romper el altar") : tr("A: Abrir el cofre");
    if (strcmp(cls, "ArcaneTome") == 0)
        return tr("A: Leer el diario de Horazon");
    if (strcmp(cls, "SevenTombsReceptacle") == 0 && party_max(offsetof(Player, staff_q)) < 3)
        return player_has(p, "hst") ? tr("A: Poner el Báculo Horádrico") : tr("Hace falta el Báculo Horádrico");
    return NULL;
}

static void save_party(void)
{
    for (int i = 0; i < s_num_players; i++)
        save_character(&s_pl[i]);
}

/* A cerca de un objeto de misión: devuelve 1 si hizo algo */
static int quest_interact(Player *p)
{
    int u;
    if (!quest_action(p, &u))
        return 0;
    const char *cls = units_id(u);
    float ux, uy;
    units_position(u, 1.0f, &ux, &uy);
    if (s_act == 3 && a3_interact(p, cls, u, ux, uy)) {
        save_party();
        return 1;
    }
    if (s_act == 4 && a4_interact(p, cls, u, ux, uy)) {
        save_party();
        return 1;
    }
    if (s_act == 5 && a5_interact(p, cls, u, ux, uy)) {
        save_party();
        return 1;
    }
    if (strcmp(cls, "InifussTree") == 0) {
        units_object_mode(u, "op", "on");
        give_quest_item(p, "bks");
        if (p->cain_q < 2)
            p->cain_q = 2;
    } else if (strncmp(cls, "Stone", 5) == 0) {
        units_object_mode(u, "op", "on"); /* se prende y queda prendida */
        char snd[24];
        snprintf(snd, sizeof snd, "cairn_stone_%d", s_stones_lit + 1);
        audio_play_name(snd, ux, uy);
        if (++s_stones_lit == 5) {
            /* Se abre el portal a Tristram: el pergamino ya cumplió */
            party_raise(offsetof(Player, cain_q), 4);
            for (int i = 0; i < s_num_players; i++) {
                int k = bag_index_of(&s_pl[i], "bkd");
                if (k >= 0)
                    bag_remove(&s_pl[i], k);
            }
            audio_play_name("cairn_success", ux, uy);
            /* Primero los rayos sobre las piedras; el portal se abre después
               (cairn_portal_open) */
            float px[5], py[5];
            for (int k = 0; k < 5; k++) {
                int id;
                px[k] = s_cairn_x;
                py[k] = s_cairn_y;
                if (units_find_all(k_stones[k], &id, 1) == 1)
                    units_position(id, 1.0f, &px[k], &py[k]);
            }
            bossfx_cairn(px, py, s_cairn_x, s_cairn_y, units_zone(u));
        }
    } else if (strcmp(cls, "Gibbet") == 0) {
        units_object_mode(u, "op", "on");
        audio_play_name("object_gibbet_drop", ux, uy);
        party_raise(offsetof(Player, cain_q), 5);
        play_vo(p, "A1Q4RescuedByHeroCain");
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act1_rescue_cain");
        quest_banner("Rescataste a Deckard Caín: te espera en el campamento", "music_quest_compelling");
    } else if (strcmp(cls, "Malus") == 0) {
        units_object_mode(u, "on", NULL);
        audio_play_name("object_malus", ux, uy);
        give_quest_item(p, "hdm");
        if (p->malus_q < 2)
            p->malus_q = 2;
    } else if (strcmp(cls, "TowerTome") == 0) {
        units_object_mode(u, "op", "on");
        audio_play_name("object_tome", ux, uy);
        party_raise(offsetof(Player, tower_q), 1);
        /* Se lee como lo que dice un NPC: el texto en el recuadro y la voz */
        p->reading = str_get("A1Q5InitQuestTome");
        p->talk_ticks = 0;
        play_vo(p, "A1Q5InitQuestTome");
    } else if (strcmp(cls, "ArcaneTome") == 0) {
        /* El diario de Horazon: el narrador lee el texto (habla de los glifos)
           y se abre el portal rojo al Canyon */
        units_object_mode(u, "op", "on");
        audio_play_name("object_tome", ux, uy);
        p->reading = str_get("A2Q4SuccessfulNarrator");
        p->talk_ticks = 0;
        play_vo(p, "A2Q4SuccessfulNarrator");
        if (party_max(offsetof(Player, arcane_q)) < 4) {
            for (int i = 0; i < s_num_players; i++) {
                if (s_pl[i].arcane_q < 4)
                    s_pl[i].arcane_q = 4;
                if (s_pl[i].tombs_q < 1)
                    s_pl[i].tombs_q = 1;
            }
            quest_done("qstsa2q4", NULL);
            a2_open_portals(-1);
            audio_play_name("object_xportalopen", ux, uy);
        }
    } else if (strcmp(cls, "SevenTombsReceptacle") == 0) {
        /* El orificio: el Báculo (y lo que quede de sus piezas) se queda, se
           cumple el Báculo Horádrico y se abre el portal a la guarida */
        if (!player_has(p, "hst"))
            return 0;
        units_object_mode(u, "op", "on");
        for (int i = 0; i < s_num_players; i++) {
            Player *o = &s_pl[i];
            take_item(o, "hst");
            take_item(o, "vip");
            take_item(o, "msf");
            for (int s = 0; s < SLOT_COUNT; s++) /* también si lo tenía en la mano */
                if (o->equip[s].type && (!strcmp(o->equip[s].type->code, "hst") || !strcmp(o->equip[s].type->code, "vip") ||
                                         !strcmp(o->equip[s].type->code, "msf"))) {
                    memset(&o->equip[s], 0, sizeof o->equip[s]);
                    refresh_stats(o);
                    refresh_look(o);
                }
            if (o->staff_q < 3)
                o->staff_q = 3;
            if (o->tombs_q < 2)
                o->tombs_q = 2;
        }
        quest_done("qstsa2q2", "music_quest_compelling");
        a2_open_portals(-1);
        audio_play_name("object_xportalopen", ux, uy);
    } else if (a2_object_bit(cls)) {
        int bit = a2_object_bit(cls);
        s_a2_opened |= bit;
        units_object_mode(u, "op", "on");
        audio_play_name(bit == 8 ? "object_taintedsunaltar" : "object_chest_large", ux, uy);
        a2_object_open(bit, ux, uy);
    }
    save_party();
    return 1;
}

/* Cada tick: los jefes de las misiones, Caín en el campamento y las frases
   al llegar a los lugares de las misiones */
static void quest_tick(void)
{
    int id;
    if (!s_countess_dead && units_find_all("The Countess", &id, 1) == 1 && units_is_corpse(id)) {
        s_countess_dead = 1;
        party_raise(offsetof(Player, tower_q), 2);
        quest_done("qstsa1q5", NULL);
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act1_complete_tower");
        save_party();
    }
    if (!s_andariel_dead && units_find_all("andariel", &id, 1) == 1 && units_is_corpse(id)) {
        s_andariel_dead = 1;
        float ax, ay;
        units_position(id, 1.0f, &ax, &ay);
        bossfx_start(BFX_ANDY, ax, ay);
        party_raise(offsetof(Player, andy_q), 2);
        quest_done("qstsa1q6", "music_quest_andariel");
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act1_complete_andariel");
        save_party();
    }
    /* Radament: suelta un Libro de Habilidad para cada uno que no la cumplió */
    if (!s_rada_dead && units_find_all("Radament", &id, 1) == 1 && units_is_corpse(id)) {
        s_rada_dead = 1;
        float rx, ry;
        units_position(id, 1.0f, &rx, &ry);
        int books = 0;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].rada_q < 2) {
                s_pl[i].rada_q = 2;
                s_pl[i].a2_flags |= A2F_BOOK;
                drop_at("ass", 0, rx + 0.8f * (float)books++, ry);
            }
        if (books) {
            quest_banner("Radament murió: volvé con Atma", NULL);
            for (int i = 0; i < s_num_players; i++)
                player_say(&s_pl[i], "act2_complete_radament");
        }
        save_party();
    }
    /* El Invocador: el Santuario queda por contar en el pueblo */
    if (!s_summ_dead && units_find_all("summoner", &id, 1) == 1 && units_is_corpse(id)) {
        s_summ_dead = 1;
        for (int i = 0; i < s_num_players; i++) {
            if (s_pl[i].summ_q < 2)
                s_pl[i].summ_q = 2;
            if (s_pl[i].level && s_pl[i].level->id == 74) /* solo los que están en el Santuario (ACT2Q5) */
                player_say(&s_pl[i], "act2_complete_summoner");
        }
        quest_banner("El Invocador murió: leé el diario de Horazon", NULL);
        save_party();
    }
    /* Duriel: se abre la puerta de Tyrael */
    if (!s_duriel_dead && units_find_all("duriel", &id, 1) == 1 && units_is_corpse(id)) {
        s_duriel_dead = 1;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].tombs_q < 3)
                s_pl[i].tombs_q = 3;
        int door;
        if (units_find_all("DurielsDoor", &door, 1) == 1) {
            units_object_mode(door, "op", "on");
            units_object_unblock(&s_map, door);
        }
        quest_banner("Duriel murió: hablá con Tyrael", "music_quest_andariel");
        save_party();
    }
    /* "He fallado, Diablo liberó a su hermano": cuando Tyrael ya abrió el
       portal y todos se alejaron de él, a más de 12 (el AI de Tyrael con
       ACT2Q6_IsInRangeOfTyrael) */
    if (s_tyr_told == 1 && s_tyrael_x >= 0) {
        int near = 0;
        for (int i = 0; i < s_num_players; i++)
            near |= s_pl[i].state < PS_DYING && hypotf(s_pl[i].x - s_tyrael_x, s_pl[i].y - s_tyrael_y) < 12.0f;
        if (!near) {
            s_tyr_told = 2;
            for (int i = 0; i < s_num_players; i++)
                if (s_pl[i].tombs_q >= 3)
                    player_say(&s_pl[i], "act2_complete_tombs");
        }
    }
    /* Sol Mancillado: un rato después de llegar a la Lost City o al Valley
       of Snakes se oscurece todo el acto (ACT2Q3_DarkenEnvironment) */
    if (s_act == 2 && party_max(offsetof(Player, sun_q)) == 0) {
        for (int i = 0; i < s_num_players && !s_sun_timer; i++)
            if (s_pl[i].level && (strcmp(s_pl[i].level->name, "Lost City") == 0 ||
                                  strcmp(s_pl[i].level->name, "Valley of Snakes") == 0))
                s_sun_timer = (15 + (int)(rnd() % 2u)) * TICK_RATE;
        if (s_sun_timer > 0 && --s_sun_timer == 0) {
            party_raise(offsetof(Player, sun_q), 1);
            quest_banner("El sol se oscureció", NULL);
            for (int i = 0; i < s_num_players; i++)
                player_say(&s_pl[i], "act2_begin_tainted");
            save_party();
        }
    }
    /* Caín aparece junto a la fogata cuando alguien lo rescató */
    if (s_cain_unit < 0 && party_max(offsetof(Player, cain_q)) >= 5) {
        units_zone_begin(world_zone_at(&s_map, s_town_x, s_town_y));
        s_cain_unit = units_spawn_npc("cain1", s_town_x + 5.0f, s_town_y - 4.0f);
        units_zone_end();
    }
    /* Frases al encontrar los lugares de las misiones (una vez por partida).
       Del Acto 2 el original dice 7 por clase (las que tienen texto en
       vo.json). Acá van también las otras de sounds.txt: la guarida de
       Radament, el Invocador, las tumbas y el Maggot Lair. D2R no las
       grabó en español latino salvo algunas del Brujo, así que las demás
       clases callan si falta el archivo. trigger_field no se sabe cuándo va. */
    static const struct { const char *level, *line; int bit; } k_finds[] = {
        { "Blood Moor", "act1_find_wilderness", 1024 }, /* al salir del campamento por primera vez */
        { "Tristram", "act1_find_tristram", 4 },     { "Monastery Gate", "act1_find_monastery", 8 },
        { "Jail Level 1", "act1_find_jail", 16 },   { "Catacombs Level 1", "act1_find_catacombs", 32 },
        { "Forgotten Tower", "act1_find_tower", 64 },
        { "Claw Viper Temple Level 1", "act2_find_clawviper", 512 }, { "Arcane Sanctuary", "act2_find_arcane", 4096 },
        { "Maggot Lair Level 3", "act2_near_tightspot", 32768 },
    };
    static const struct { const char *object, *line; int bit; } k_sights[] = {
        { "StoneAlpha", "act1_find_cairn", 128 }, { "InifussTree", "act1_find_tree", 256 },
        { "Radament", "act2_find_radament", 2048 }, { "summoner", "act2_find_summoner", 8192 },
    };
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        if (!p->level || p->state >= PS_DYING)
            continue;
        for (unsigned k = 0; k < sizeof k_finds / sizeof k_finds[0]; k++)
            if (!(p->said & k_finds[k].bit) && strcmp(p->level->name, k_finds[k].level) == 0) {
                p->said |= k_finds[k].bit;
                player_say(p, k_finds[k].line);
            }
        for (unsigned k = 0; k < sizeof k_sights / sizeof k_sights[0]; k++) {
            float x, y;
            if (!(p->said & k_sights[k].bit) && units_find(k_sights[k].object, &x, &y) == 0 &&
                (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < 15.0f * 15.0f) {
                p->said |= k_sights[k].bit;
                player_say(p, k_sights[k].line);
            }
        }
        /* Hermanas de la Masacre: se anota al llegar a las Catacumbas */
        if (p->andy_q == 0 && strncmp(p->level->name, "Catacombs", 9) == 0)
            p->andy_q = 1;
        if (strncmp(p->level->name, "Sewers", 6) == 0) /* Radament: ya bajó a buscarlo */
            p->a2_flags |= A2F_SEWERS;
        if (p->level->id == 74) { /* el Santuario Arcano: hay que buscar el diario (y el Invocador está ahí) */
            if (p->arcane_q < 3)
                p->arcane_q = 3;
            if (p->summ_q < 1)
                p->summ_q = 1;
        }
        if (p->level->id == 46 && p->tombs_q < 1) /* el Canyon: las Siete Tumbas */
            p->tombs_q = 1;
        if (p->level->id >= 66 && p->level->id <= 72 && !(p->said & 16384)) { /* la verdadera o una falsa */
            p->said |= 16384;
            player_say(p, p->level->id == world_staff_tomb() ? "act2_find_tomb_true" : "act2_find_tomb_decoy");
        }
    }
    if (s_act == 3)
        a3_quest_tick();
    if (s_act == 4)
        a4_quest_tick();
    if (s_act == 5)
        a5_quest_tick();
}

/* Al empezar una partida: las piedras en otro orden y los jefes vivos */
static void quest_reset(void)
{
    for (int k = 0; k < 5; k++)
        s_stone_order[k] = k;
    for (int k = 4; k > 0; k--) {
        int j = (int)(rnd() % (uint32_t)(k + 1)), t = s_stone_order[k];
        s_stone_order[k] = s_stone_order[j];
        s_stone_order[j] = t;
    }
    s_stones_lit = 0;
    s_countess_dead = s_andariel_dead = 0;
    a3_quest_reset();
    a4_quest_reset();
    a5_quest_reset();
    s_a2_opened = s_rada_dead = s_sun_timer = s_summ_dead = s_duriel_dead = s_tyr_told = 0;
    s_tome_x = s_orifice_x = s_tyrael_x = s_tyr_portal_x = -1;
    for (int k = PORTAL_ARCANE_IN; k <= PORTAL_TYRAEL; k++)
        world_portal(k, -1, 0, 0, -1, 0, 0);
    if (s_cain_unit >= 0)
        units_remove(s_cain_unit);
    s_cain_unit = -1;
    world_portal(PORTAL_TRISTRAM, -1, 0, 0, -1, 0, 0);
    world_portal(PORTAL_CAIRN, -1, 0, 0, -1, 0, 0);
    tp_reset();
    obj_reset();
    cow_reset();
    uber_reset();
    dclone_reset();
    tz_reset();
}

/* ---- Imbuir (la recompensa de Charsi) ---- */

/* ¿Sirve para la lista? Imbuir (Charsi): armas y armaduras comunes;
   engarzar (Larzuk): armas y armaduras sin engarces; personalizar (Anya):
   las que no son comunes ni están personalizadas */
static int imbue_fits(const Player *p, const Item *it)
{
    if (!it->type || is_quest_item(it->type) || (it->type->kind != ITEM_WEAPON && it->type->kind != ITEM_ARMOR) || it->type->stackable)
        return 0;
    if (p->imbue_mode == 1)
        return it->sockets == 0;
    if (p->imbue_mode == 2)
        return it->quality != QUALITY_NORMAL && it->quality != QUALITY_SUPERIOR && it->quality != QUALITY_LOW &&
               !strstr(it->name, s_classes[p->cls].title);
    return it->quality == QUALITY_NORMAL;
}

/* Lo que se puede imbuir (o engarzar, o personalizar): puestas (-(lugar + 1))
   o en la mochila (su índice) */
static int imbue_candidates(const Player *p, int *out)
{
    int n = 0;
    for (int s = 0; s < SLOT_COUNT; s++)
        if (imbue_fits(p, &p->equip[s]))
            out[n++] = -(s + 1);
    for (int i = 0; i < p->bag_count; i++)
        if (imbue_fits(p, &p->bag[i]))
            out[n++] = i;
    return n;
}

static Item *imbue_item(Player *p, int c)
{
    return c < 0 ? &p->equip[-c - 1] : &p->bag[c];
}

static void imbue_tick(Player *p, const PlayerInput *pi)
{
    units_talk(p->dialog_npc, p->x, p->y);
    int list[BAG_MAX + SLOT_COUNT], n = imbue_candidates(p, list);
    if (pi->back || pi->inventory || n == 0) {
        if (n == 0)
            show_message(p, p->imbue_mode == 1   ? "No tenés armas ni armaduras sin engarces"
                            : p->imbue_mode == 2 ? "No tenés objetos mágicos, raros o únicos para personalizar"
                                                 : "No tenés armas ni armaduras comunes");
        p->imbue_open = 0;
        return;
    }
    if (pi->nav_up)
        p->imbue_sel = (p->imbue_sel + n - 1) % n;
    if (pi->nav_down)
        p->imbue_sel = (p->imbue_sel + 1) % n;
    if (p->imbue_sel >= n)
        p->imbue_sel = n - 1;
    if (!pi->action)
        return;
    Item *it = imbue_item(p, list[p->imbue_sel]), copy = *it;
    if (p->imbue_mode == 1) { /* Larzuk (A5Q1): los engarces que puede tener */
        if (!items_larzuk_sockets(&copy)) {
            show_message(p, "Ese objeto no admite engarces");
            return;
        }
        p->shenk_q = 4;
    } else if (p->imbue_mode == 2) { /* Anya (A5Q4): con el nombre del personaje */
        char label[64];
        items_label(&copy, label, sizeof label);
        snprintf(copy.name, sizeof copy.name, "%.38s de %.20s", label, s_classes[p->cls].title);
        p->nihl_q = 5;
    } else if (!items_make_rare(&copy, p->level_num + 4)) { /* Como en el original: un raro de nivel del personaje + 4 */
        show_message(p, "No se pudo imbuir");
        return;
    } else {
        p->malus_q = 4;
    }
    *it = copy;
    p->imbue_open = 0;
    audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
    char label[64];
    show_message(p, items_label(it, label, sizeof label));
    refresh_stats(p);
    refresh_look(p);
    save_character(p);
}

static void draw_imbue(const Player *p)
{
    int x0, x1;
    half_range(p, &x0, &x1);
    int list[BAG_MAX + SLOT_COUNT], n = imbue_candidates(p, list);
    int shown = n < 10 ? n : 10, first = p->imbue_sel >= shown ? p->imbue_sel - shown + 1 : 0;
    int w = x1 - x0 - 40 > 420 ? 420 : x1 - x0 - 40, x = (x0 + x1) / 2 - w / 2, y = 60, lh = 26;
    int h = 64 + shown * lh + 30;
    ui_dark_box(x, y, w, h);
    font_draw_centered(x + w / 2, y + 10, p->imbue_mode == 1 ? "Engarzar" : p->imbue_mode == 2 ? "Personalizar" : "Imbuir", 3,
                       COL(R_GOLD, 15));
    for (int k = 0; k < shown; k++) {
        int c = list[first + k], chosen = first + k == p->imbue_sel, ty = y + 52 + k * lh;
        char label[64];
        items_label(imbue_item((Player *)p, c), label, sizeof label);
        font_draw(x + 34, ty, label, 2, chosen ? COL(R_GOLD, 15) : COL(R_GRAY, 15));
        if (c < 0)
            font_draw(x + w - 16 - font_width("(puesto)", 1), ty + 2, "(puesto)", 1, COL(R_STONE, 12));
        if (chosen)
            draw_pentagram(x + 8, ty - 4, 20, 1);
    }
    font_draw_centered(x + w / 2, y + h - 24,
                       p->imbue_mode == 1   ? "Larzuk le hace los engarces que admite"
                       : p->imbue_mode == 2 ? "Anya le pone tu nombre"
                                            : "Charsi lo vuelve raro: de 3 a 6 propiedades",
                       1, COL(R_STONE, 12));
    font_draw_centered(x + w / 2, y + h + 4,
                       p->imbue_mode == 1 ? "A: engarzar   B: volver" : p->imbue_mode == 2 ? "A: personalizar   B: volver" : "A: imbuir   B: volver",
                       1, COL(R_STONE, 12));
}

/* ---- Registro de misiones (una página más del panel) ---- */

/* El objetivo del momento de cada misión (los textos del registro del original) */
static const char *quest_goal(const Player *p, int q, int *state)
{
    /* state: 0 sin empezar, 1 en curso, 2 cumplida */
    switch (q) {
    case 0:
        *state = p->den_rewarded ? 2 : p->den_started || p->den_done ? 1 : 0;
        return p->den_rewarded ? NULL : p->den_done ? "qstsa1q15" : p->den_started ? "qstsa1q12" : "qstsa1q11";
    case 1:
        *state = p->raven_rewarded ? 2 : p->raven_started || p->raven_done ? 1 : 0;
        return p->raven_rewarded ? NULL : p->raven_done ? "qstsa1q23" : p->raven_started ? "qstsa1q22" : "qstsa1q21";
    case 2: {
        static const char *const k[] = { NULL, "qstsa1q31", "qstsa1q32", "qstsa1q32b", NULL };
        *state = p->malus_q >= 3 ? 2 : p->malus_q > 0 ? 1 : 0;
        return p->malus_q == 3 ? "qstsa1q32b" : k[p->malus_q < 5 ? p->malus_q : 4];
    }
    case 3: {
        static const char *const k[] = { NULL, "qstsa1q41", "qstsa1q42", "qstsa1q43", "qstsa1q44", "qstsa1q46b", NULL };
        *state = p->cain_q >= 6 ? 2 : p->cain_q > 0 ? 1 : 0;
        return k[p->cain_q < 7 ? p->cain_q : 6];
    }
    case 4:
        *state = p->tower_q >= 2 ? 2 : p->tower_q > 0 ? 1 : 0;
        return p->tower_q == 1 ? "qstsa1q52" : p->tower_q == 0 ? "qstsa1q51" : NULL;
    default:
        *state = p->andy_q >= 3 ? 2 : p->andy_q > 0 ? 1 : 0;
        return p->andy_q == 2 ? "qstsa1q62b" : p->andy_q == 1 ? "qstsa1q62" : p->andy_q == 0 ? "qstsa1q61" : NULL;
    }
}

#define QUEST_ACTS 5 /* los actos con registro (las solapas) */
#define QUEST_FIRE 48 /* cuadros que arde una misión recién cumplida (24 del ícono, de a 2) */

/* ¿Llegó a ese acto? (si no, su solapa no está) */
static int quest_act_reached(const Player *p, int act)
{
    return act == 1 || (act == 2 && (p->act >= 2 || p->andy_q >= 3)) || (act == 3 && (p->act >= 3 || p->tombs_q >= 6)) ||
           (act == 4 && (p->act >= 4 || p->act_top >= 4)) || (act == 5 && (p->act >= 5 || p->act_top >= 5));
}

/* Cuántas misiones tiene el acto (el IV, tres) */
static int quest_slots(int act)
{
    return act == 4 ? 3 : 6;
}

/* Las del Acto 2 (los textos del registro del original, qstsa2q11...) */
static const char *quest_goal2(const Player *p, int q, int *state)
{
    switch (q) {
    case 0: /* Radament: buscarlo en las cloacas, matarlo, volver con Atma */
        *state = p->rada_q >= 3 ? 2 : p->rada_q > 0 ? 1 : 0;
        return p->rada_q >= 3 ? NULL : p->rada_q == 2 ? "qstsa2q13" : (p->a2_flags & A2F_SEWERS) ? "qstsa2q12" : "qstsa2q11";
    case 1: { /* el Báculo: el pergamino a Caín, las piezas, el cubo, a la tumba */
        int has = player_has(p, "hst"), piece = 0;
        cain_piece(p, &piece);
        *state = p->staff_q >= 3 ? 2 : p->staff_q > 0 || player_has(p, "tr1") ? 1 : 0;
        if (p->staff_q >= 3)
            return NULL;
        if (p->staff_q == 0)
            return "qstsa2q21";
        if (has || p->staff_q == 2)
            return "qstsa2q24";
        if (piece && piece != A2_SEEN_CUBE)
            return "qstsa2q25";
        return player_has(p, "msf") && player_has(p, "vip") ? "qstsa2q23" : "qstsa2q22";
    }
    case 2: /* el Sol Mancillado */
        *state = p->sun_q >= 4 ? 2 : p->sun_q > 0 ? 1 : 0;
        return p->sun_q >= 4 ? NULL : p->sun_q == 3 ? "qstsa2q33" : p->sun_q == 2 ? "qstsa2q32"
                                    : p->sun_q == 1 ? "qstsa2q31a" : "qstsa2q31";
    case 3: /* el Santuario Arcano: Drognan, Jerhyn, el palacio, el diario */
        *state = p->arcane_q >= 4 ? 2 : p->arcane_q > 0 || p->sun_q >= 4 ? 1 : 0;
        return p->arcane_q >= 4 ? NULL : p->arcane_q == 3 ? "qstsa2q42" : p->arcane_q >= 1 ? "qstsa2q41"
                                    : p->sun_q >= 4 ? "qstsa2q41a" : "qstsa2q41";
    case 4: /* el Invocador */
        *state = p->summ_q >= 3 ? 2 : p->summ_q > 0 ? 1 : 0;
        return p->summ_q >= 3 ? NULL : p->summ_q == 2 ? "qstsa2q53" : p->summ_q == 1 ? "qstsa2q52" : "qstsa2q51";
    default: /* las Siete Tumbas (con el símbolo de la verdadera, en draw_quest_log, después del diario) */
        *state = p->tombs_q >= 6 ? 2 : p->tombs_q > 0 ? 1 : 0;
        if (p->tombs_q >= 6)
            return NULL;
        if (p->tombs_q <= 1)
            return p->arcane_q >= 4 ? "qstsa2q61a" : "qstsa2q61";
        return p->tombs_q == 2 ? "qstsa2q62" : p->tombs_q == 3 ? "qstsa2q63" : p->tombs_q == 4 ? "qstsa2q64" : "qstsa2q65";
    }
}

/* Las de los NPC del Acto 2 que no son de una sola misión: el Santuario
   (Drognan, Jerhyn), el Invocador y las Siete Tumbas (Jerhyn, Tyrael,
   Meshif); 1 si dijo algo de eso */
static int a2_npc_quest(Player *p, int who, char *key, size_t cap)
{
    const char *name = who >= 0 ? k_npc_vo[who].vo : "";
    char k[64];
    if (who == 17) { /* Kaelan, en la puerta del palacio: cerrado "Alto", abierto "Puedes entrar" */
        snprintf(key, cap, "PalaceGuardGossip%d", palace_open() ? 3 + (int)(rnd() % 3u) : 1 + (int)(rnd() % 2u));
        return 1;
    }
    if (who == 7 && p->arcane_q == 0 && p->sun_q >= 4) { /* Drognan: el Santuario, en el palacio */
        party_raise(offsetof(Player, arcane_q), 1);
        snprintf(key, cap, "A2Q4InitDrognan");
        return 1;
    }
    if (who == 12 && p->arcane_q == 1) { /* Jerhyn deja entrar al palacio */
        party_raise(offsetof(Player, arcane_q), 2);
        snprintf(key, cap, "A2Q4AfterInitJerhyn");
        return 1;
    }
    if (who == 18) { /* Tyrael: abre el portal al pueblo */
        if (p->tombs_q == 3) {
            party_raise(offsetof(Player, tombs_q), 4);
            s_tyr_told = 1;
            s_tyr_portal_x = p->x + 1.0f; /* como el original: al lado del que le habló */
            s_tyr_portal_y = p->y + 1.0f;
            find_free_spot(&s_tyr_portal_x, &s_tyr_portal_y);
            a2_open_portals(-1);
            audio_play_name("object_xportalopen", p->x, p->y);
        }
        snprintf(key, cap, "TyraelGossip1");
        return 1;
    }
    /* Jerhyn: las Siete Tumbas (también si ya leyó el diario o llegó al
       Canyon, que suben las Tumbas a 1 antes de hablarle) */
    if (who == 12 && p->tombs_q <= 1 && p->summ_q >= 2 && !(p->a2_flags & A2F_TOMBS_TOLD)) {
        party_raise(offsetof(Player, tombs_q), 1);
        p->a2_flags |= A2F_TOMBS_TOLD;
        snprintf(key, cap, "A2Q6InitJerhyn");
        return 1;
    }
    if (who == 12 && p->tombs_q == 4) { /* Jerhyn, después de Duriel: que hable con Meshif */
        p->tombs_q = 5;
        snprintf(key, cap, "A2Q6SuccessfulJerhyn");
        return 1;
    }
    if (who == 14 && p->tombs_q == 5) { /* Meshif: el barco al este; el Acto 2 terminó */
        p->tombs_q = 6;
        quest_done("qstsa2q6", "music_quest_andariel");
        snprintf(key, cap, "A2Q6SuccessfulMeshif");
        return 1;
    }
    /* el Invocador murió: cualquiera de Lut Gholein lo agradece */
    if (who >= 6 && who <= 16 && p->summ_q == 2 && talk_line_key(p, 30, name, k, sizeof k)) {
        p->summ_q = 3;
        quest_done("qstsa2q5", NULL);
        snprintf(key, cap, "%s", k);
        return 1;
    }
    /* el diario: cualquiera lo comenta (la misión ya está cumplida) */
    if (who >= 6 && who <= 16 && p->arcane_q == 4 && talk_line_key(p, 28, name, k, sizeof k)) {
        p->arcane_q = 5;
        snprintf(key, cap, "%s", k);
        return 1;
    }
    return 0;
}

/* Lo mismo, sin cambiar nada: si tiene algo nuevo que decir (el "!") */
static int a2_npc_news(const Player *p, int who)
{
    const char *name = who >= 0 ? k_npc_vo[who].vo : "";
    char k[64];
    return (who == 7 && p->arcane_q == 0 && p->sun_q >= 4) || (who == 12 && p->arcane_q == 1) ||
           (who == 18 && p->tombs_q == 3) || (who == 12 && p->tombs_q <= 1 && p->summ_q >= 2 && !(p->a2_flags & A2F_TOMBS_TOLD)) ||
           (who == 12 && p->tombs_q == 4) || (who == 14 && p->tombs_q == 5) ||
           (who >= 6 && who <= 16 && p->summ_q == 2 && talk_line_key(p, 30, name, k, sizeof k));
}

/* La misión q (0..5) de un acto: su estado (0 pendiente, 1 en curso, 2
   cumplida) y la clave del objetivo del momento (NULL = ninguno) */
static const char *quest_info(const Player *p, int act, int q, int *state)
{
    return act == 1   ? quest_goal(p, q, state)
           : act == 2 ? quest_goal2(p, q, state)
           : act == 3 ? quest_goal3(p, q, state)
           : act == 4 ? quest_goal4(p, q, state)
                      : quest_goal5(p, q, state);
}

/* La que se muestra al abrirlo: la primera en curso del acto en que está; si
   no hay, la primera pendiente */
static void quest_log_pick(const Player *p, int *act, int *sel)
{
    int state, pending = -1;
    *act = p->act >= 1 && p->act <= QUEST_ACTS && quest_act_reached(p, p->act) ? p->act : 1;
    for (int q = 0; q < quest_slots(*act); q++) {
        quest_info(p, *act, q, &state);
        if (state == 1) {
            *sel = q;
            return;
        }
        if (state == 0 && pending < 0)
            pending = q;
    }
    *sel = pending >= 0 ? pending : 0;
}

/* La de al lado (dir +1 o -1), pasando a la solapa del acto vecino */
static void quest_log_step(const Player *p, int *act, int *sel, int dir)
{
    int acts = 1;
    while (acts < QUEST_ACTS && quest_act_reached(p, acts + 1))
        acts++;
    int i = (*act - 1) * 6 + *sel;
    do /* (en el Acto 4 los lugares 3..5 están vacíos) */
        i = (i + dir + acts * 6) % (acts * 6);
    while (i % 6 >= quest_slots(i / 6 + 1));
    *act = i / 6 + 1;
    *sel = i % 6;
    audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS);
}

/* Las cumplidas ya se vieron: no vuelven a arder */
static void quest_log_seen(Player *p)
{
    for (int a = 1; a <= QUEST_ACTS; a++)
        for (int q = 0; q < quest_slots(a); q++) {
            int state;
            quest_info(p, a, q, &state);
            if (state == 2)
                p->quest_seen |= 1u << ((a - 1) * 6 + q);
        }
}

/* El glifo de la tumba verdadera de Tal Rasha, centrado en (cx, cy): el
   objeto ArcaneRune de esa tumba (objects/7a..7g), de frente y con su brillo
   (como el registro del original, después de leer el diario de Horazon) */
static int draw_tomb_symbol(int cx, int cy)
{
    static Sprite s_rune[8];
    static uint8_t s_tried[8];
    if (world_act() != 2)
        return 0;
    int r = tomb_rune(world_staff_tomb());
    if (!s_tried[r]) {
        s_tried[r] = 1;
        char path[96];
        snprintf(path, sizeof path, "data/global/objects/7%c/tr/7%ctrlitnuhth.dcc", 'a' + r - 1, 'a' + r - 1);
        size_t n;
        uint8_t *d = fs_read(path, &n);
        if (d && dcc_load(d, n, &s_rune[r]) != 0)
            memset(&s_rune[r], 0, sizeof s_rune[r]);
        free(d);
    }
    if (!s_rune[r].frames)
        return 0;
    /* el glifo gira: el cuadro más ancho es el que se ve de frente */
    const SpriteFrame *fr = sprite_frame(&s_rune[r], 0, 0);
    for (int f = 1; f < s_rune[r].frames_per_dir; f++)
        if (sprite_frame(&s_rune[r], 0, f)->width > fr->width)
            fr = sprite_frame(&s_rune[r], 0, f);
    fb_blit_blend_spans(fr->pixels, fr->spans, fr->width, fr->height, cx - fr->width / 2, cy - fr->height / 2, BLEND_ADD);
    return 1;
}

/* El registro de misiones del original (questbackground, de 320x432): las
   solapas de los actos a los que llegó, las seis misiones del acto en sus
   huecos (oscura la pendiente, con su color la que está en curso y gris la
   cumplida; la recién cumplida arde una vez, como en el juego) y abajo la
   elegida: su nombre, cómo va y lo que hay que hacer */
static void draw_quest_log(const Player *p, int x, int y, int act, int sel, uint32_t anim_start, const char *help1,
                           const char *help2)
{
    ui_grid("menu/questbackground", 0, 4, 2, x, y);
    for (int a = 1; a <= QUEST_ACTS; a++)
        if (quest_act_reached(p, a) && a != act)
            ui_frame("menu/expquesttabs", (a - 1) * 2 + 1, x + 5 + (a - 1) * 62, y + 2);
    ui_frame("menu/expquesttabs", (act - 1) * 2, x + 5 + (act - 1) * 62, y + 2); /* la elegida adelante */
    uint32_t t = s_anim_ticks - anim_start;
    for (int q = 0; q < quest_slots(act); q++) {
        int state, sx = x + 20 + (q % 3) * 100, sy = y + 36 + (q / 3) * 98;
        quest_info(p, act, q, &state);
        /* a1q1.dc6: 0 en curso, 1..23 el fuego, 24 cumplida, 26 todavía no */
        int frame = state == 0 ? 26 : state == 1 ? 0 : 24;
        if (state == 2 && !((p->quest_seen >> ((act - 1) * 6 + q)) & 1) && t < QUEST_FIRE)
            frame = (int)(t / 2);
        char icon[24];
        snprintf(icon, sizeof icon, "menu/a%dq%d", act, q + 1);
        ui_frame("menu/questsockets", q == sel, sx, sy);
        ui_frame(icon, frame, sx + 4, sy + 6);
    }
    int state;
    const char *goal = quest_info(p, act, sel, &state);
    static const char *const k_state[3] = { "Pendiente", "En curso", "Cumplida" };
    char key[24], lines[6][128];
    snprintf(key, sizeof key, "qstsa%dq%d", act, act == 4 ? k_a4_quest_key[sel] : sel + 1);
    font_draw_centered(x + PANEL_W / 2, y + 264, str_get(key), 2, COL(R_GOLD, 15));
    font_draw_centered(x + PANEL_W / 2, y + 284, k_state[state], 1,
                       state == 2 ? COL(R_GOLD, 12) : state == 1 ? COL(R_GRAY, 15) : COL(R_STONE, 12));
    char goal_buf[160] = "";
    if (goal && strcmp(goal, "qstsa5q22") == 0) { /* "Rescata a %d soldados más" */
        snprintf(goal_buf, sizeof goal_buf, str_get(goal), 15 - p->rescued > 0 ? 15 - p->rescued : 0);
        goal = NULL;
    }
    if (goal && strcmp(goal, "qstsa4q23") == 0) { /* "Rompe los %d sellos restantes" (el último: qstsa4q24) */
        int left = a4_seals_left();
        if (left == 1)
            goal = "qstsa4q24";
        else {
            snprintf(goal_buf, sizeof goal_buf, str_get(goal), left);
            goal = NULL;
        }
    }
    int n = state == 2 || (!goal && !goal_buf[0]) ? 0
          : wrap_text(goal ? str_get(goal) : goal_buf, PANEL_W - 44, FONT_FORMAL, lines, 5);
    for (int k = 0; k < n; k++)
        font_draw_centered(x + PANEL_W / 2, y + 302 + k * 15, lines[k], FONT_FORMAL, COL(R_GRAY, 14));
    /* Las Siete Tumbas: debajo, el símbolo de la tumba verdadera */
    if (act == 2 && sel == 5 && goal && strcmp(goal, "qstsa2q61a") == 0)
        draw_tomb_symbol(x + PANEL_W / 2, y + 302 + n * 15 + 22);
    /* Los botones de abajo, en sus huecos (questlogpaneloriginal.json: Speech y Cancel) */
    ui_frame("menu/questlast", 0, x + 226, y + 392); /* (repetir lo último que se dijo) */
    ui_frame("panel/buysellbtn", 10, x + 278, y + 390); /* cerrar (B) */
    /* Las ayudas, en la piedra de abajo */
    font_draw(x + 14, y + 394, help1, 1, COL(R_GOLD, 15));
    if (help2)
        font_draw(x + 14, y + 408, help2, 1, COL(R_GOLD, 15));
}

static void draw_quest_page(const Player *p)
{
    draw_panel_header(p, "Misiones", "Cruceta: elegir   Y: inventario   B: cerrar");
    draw_quest_log(p, panel_x(p), PANEL_Y, p->qlog_act > 0 ? p->qlog_act : 1, p->qlog_sel, p->qlog_anim,
                   "Cruceta: elegir misión", quest_act_reached(p, 2) ? "LB / RB: otro acto" : NULL);
}

/* La cruceta elige la misión (de costado pasa al otro acto), LB y RB
   cambian de acto, Y vuelve al inventario y B cierra */
static void quest_page_tick(Player *p, const PlayerInput *pi)
{
    if (p->qlog_act <= 0) {
        quest_log_pick(p, &p->qlog_act, &p->qlog_sel);
        p->qlog_anim = s_anim_ticks;
    }
    if (s_anim_ticks - p->qlog_anim > QUEST_FIRE)
        quest_log_seen(p);
    if (pi->button[3] || pi->back) {
        quest_log_seen(p);
        p->qlog_act = 0;
        if (pi->button[3])
            p->inv_page = PAGE_INVENTORY;
        else
            p->inv_open = 0;
        return;
    }
    if (pi->nav_left || pi->nav_right)
        quest_log_step(p, &p->qlog_act, &p->qlog_sel, pi->nav_right - pi->nav_left);
    if ((pi->nav_up || pi->nav_down) && quest_slots(p->qlog_act) == 6) {
        p->qlog_sel = (p->qlog_sel + 3) % 6;
        audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS);
    }
    int da = (pi->automap ? 1 : 0) - (pi->potion_life ? 1 : 0);
    if (da && quest_act_reached(p, p->qlog_act + da) && p->qlog_act + da >= 1 && p->qlog_act + da <= QUEST_ACTS) {
        p->qlog_act += da;
        if (p->qlog_sel >= quest_slots(p->qlog_act))
            p->qlog_sel = quest_slots(p->qlog_act) - 1;
        audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS);
    }
}

/* El tomo: el texto en un recuadro (B o A lo cierran) */
static void reading_tick(Player *p, const PlayerInput *pi)
{
    p->talk_ticks++;
    if (pi->back || pi->action || pi->inventory) {
        p->reading = NULL;
        if (s_voice_owner == (int)(p - s_pl)) {
            audio_voice_stop();
            s_voice_owner = -1;
        }
    }
}

static void draw_reading(const Player *p)
{
    int x0, x1;
    half_range(p, &x0, &x1);
    int w = x1 - x0 - 40 > 520 ? 520 : x1 - x0 - 40, x = (x0 + x1) / 2 - w / 2, y = 70;
    char lines[TALK_LINES * 4][128];
    int n = wrap_text(p->reading, w - 40, FONT_FORMAL, lines, TALK_LINES * 4);
    int first = p->talk_ticks > TALK_WAIT ? (p->talk_ticks - TALK_WAIT) / TALK_LINE_TICKS : 0;
    if (first > n - TALK_LINES)
        first = n - TALK_LINES > 0 ? n - TALK_LINES : 0;
    int h = 30 + TALK_LINES * 22 + 20;
    ui_dark_box(x, y, w, h);
    for (int k = 0; k < TALK_LINES && first + k < n; k++)
        font_draw_centered(x + w / 2, y + 16 + k * 22, lines[first + k], FONT_FORMAL, COL(R_GRAY, 15));
    font_draw_centered(x + w / 2, y + h + 4, "B: cerrar", 1, COL(R_STONE, 12));
}

/* La lista de frases (R3), en el medio de su mitad de la pantalla */
static void draw_chat(const Player *p)
{
    int x0, x1, lh = 22, w = 180, h = 34 + 8 * lh;
    half_range(p, &x0, &x1);
    int x = (x0 + x1) / 2 - w / 2, y = (PANEL_TOP - h) / 2;
    ui_dark_box(x, y, w, h);
    draw_frame(x, y, w, h, COL(R_GOLD, 11));
    font_draw_centered(x + w / 2, y + 6, "Frases", 2, COL(R_GOLD, 15));
    for (int k = 0; k < 8; k++) {
        int ry = y + 30 + k * lh;
        font_draw(x + 34, ry, k_chat_names[k], 2, k == p->chat_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 15));
        if (k == p->chat_sel)
            draw_pentagram(x + 8, ry - 3, 20, 1);
    }
    font_draw_centered(x + w / 2, y + h + 4, "A: decir   B: cerrar", 1, COL(R_STONE, 12));
}

/* El final del acto: el cartel de la caravana */
/* ---- Zonas en memoria ---- */

static int zone_at(float x, float y)
{
    return world_zone_at(&s_map, x, y);
}

static int zone_at_loaded(float x, float y)
{
    int z = zone_at(x, y);
    return world_zone_loaded(z) ? z : -1;
}

static int zones_loaded_count(void)
{
    int n = 0;
    for (int z = 0; z < world_zone_count(); z++)
        n += world_zone_loaded(z) && !world_zone_is_town(z);
    return n;
}

/* Hay un jugador o una mercenaria adentro */
static int zone_occupied(int z)
{
    for (int i = 0; i < s_num_players; i++) {
        float mx, my;
        if (zone_at(s_pl[i].x, s_pl[i].y) == z)
            return 1;
        if (units_merc_alive(s_pl[i].merc_unit)) {
            units_position(s_pl[i].merc_unit, 1.0f, &mx, &my);
            if (zone_at(mx, my) == z)
                return 1;
        }
    }
    return 0;
}

static int s_act_leaving; /* cambiando de acto: zone_unload también suelta el pueblo */

/* El armado de una zona en partes (zone_build): la zona a medias y por qué parte va */
enum { ZB_MAP, ZB_PRESETS, ZB_OBJECTS, ZB_MONSTERS };
static int      s_zb_zone = -1, s_zb_stage, s_zb_ticks;
static int      s_zb_part, s_zb_next;   /* cuántas veces va la parte de ahora y la unidad de los DS1 que sigue */
static double   s_zb_ms[4], s_zb_worst; /* D2_ZONES: lo de cada parte y el tick que más tardó */
static int      s_zb_worst_stage;       /* (y en qué parte) */
static int      s_trim_pending;         /* una zona borrada: lo liberado vuelve al sistema en otro tick (zones_tick) */
static uint32_t s_zb_wait;              /* desde cuándo espera los DT1 del hilo de trabajos */
static uint32_t s_zb_begin;             /* cuándo empezó (las piezas esperan sus DT1 hasta ZONE_DT1_WAIT) */

static void zone_unload(int z)
{
    if ((!world_zone_loaded(z) && z != s_zb_zone) || (world_zone_is_town(z) && !s_act_leaving))
        return;
    if (z == s_zb_zone) /* a medio armar: se tira lo que haya */
        s_zb_zone = -1;
    double t0 = wall_ms();
    /* Que nadie siga apuntando a los monstruos que se van */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s_pl[i];
        if (p->target >= 0 && units_zone(p->target) == z)
            p->target = -1;
        if (p->cast_target >= 0 && units_zone(p->cast_target) == z)
            p->cast_target = -1;
    }
    if (s_shown_target >= 0 && units_zone(s_shown_target) == z)
        s_shown_target = -1;
    int freed = units_free_zone(z, zone_at_loaded);
    for (int i = 0; i < missiles_capacity(); i++) {
        const Missile *mi = missiles_get(i);
        if (mi && zone_at(mi->x, mi->y) == z)
            missiles_remove(i);
    }
    world_zone_free(&s_map, z);
    waypoints_find();
    waypoints_light();
    s_trim_pending = 1;
    if (s_debug || getenv("D2_ZONES"))
        printf("zona borrada: %s (%d unidades, %.0f ms)\n", world_zone_name(z), freed, wall_ms() - t0);
    mem_report("borrar la zona");
}

/* El armado de una zona en partes (zones_tick, ZONE_BUDGET ms por tick
   mientras los jugadores se acercan; en la Pi una zona entera tardaba de 30
   a 400 ms de una vez): las piezas del mapa de a poco (world_zone_build_step),
   la colisión, las unidades que traen las piezas, los objetos al azar de las
   salas y los monstruos del nivel, cada cosa en un tick. Una zona a la vez. */

/* Arma la zona z: con budget 0 entera ahora (y antes la que estaba a
   medias); si no, la parte que sigue. Si ya hay demasiadas, al empezar
   borra la que hace más tiempo que nadie usa (nunca una con alguien adentro
   o al lado). 1 = armada, 0 = falta, -1 = error. */
static int zone_build(int z, double budget)
{
    if (z < 0 || z >= world_zone_count())
        return -1;
    if (z < ZONE_BITS)
        s_zone_seen[z] = s_ticks;
    if (s_zb_zone != z && world_zone_loaded(z))
        return 1;
    if (s_zb_zone >= 0 && s_zb_zone != z) {
        if (budget > 0)
            return 0; /* otra a medias: después */
        if (zone_build(s_zb_zone, 0) < 0)
            return -1;
        if (world_zone_loaded(z))
            return 1;
    }
    if (s_zb_zone < 0) {
        int evicted = 0;
        while (zones_loaded_count() >= s_zone_max) {
            int old = -1;
            for (int k = 0; k < world_zone_count() && k < ZONE_BITS; k++) {
                if (!world_zone_loaded(k) || world_zone_is_town(k) || ((s_zone_pinned >> k) & 1) || zone_occupied(k))
                    continue;
                if (old < 0 || s_ticks - s_zone_seen[k] > s_ticks - s_zone_seen[old])
                    old = k;
            }
            if (old < 0)
                break; /* todas en uso: se pasa del límite un rato */
            zone_unload(old);
            evicted = 1;
        }
        s_zb_zone = z;
        s_zb_stage = ZB_MAP;
        s_zb_part = 0;
        s_zb_ticks = 0;
        s_zb_begin = s_ticks;
        s_zb_worst = 0;
        s_zb_worst_stage = ZB_MAP;
        memset(s_zb_ms, 0, sizeof s_zb_ms);
        /* Borrar la vieja ya fue bastante para este tick (en la Pi, hasta 30
           ms): se empieza a armar en el que viene */
        if (evicted && budget > 0)
            return 0;
    }
    /* D2_ZONE_PARTS=1: también la que se arma entera va de a una pieza por
       pasada (para probar que en partes sale igual) */
    static int parts = -1;
    if (parts < 0)
        parts = getenv("D2_ZONE_PARTS") != NULL;
    double t_tick = wall_ms();
    int last = s_zb_stage;
    s_zb_ticks++;
    for (;;) {
        double t = wall_ms();
        int stage = last = s_zb_stage;
        if (stage == ZB_MAP) {
            /* Sin el hilo de trabajos (las pruebas sin gráficos) o en red el
               tope es de piezas y no de tiempo: así salen siempre iguales */
            int sync = budget > 0 && (!jobs_running() || s_lockstep);
            static int cap = -1;
            if (cap < 0)
                cap = getenv("D2_ZONE_PIECES") ? atoi(getenv("D2_ZONE_PIECES")) : ZONE_SYNC_PIECES;
            s_map.dt1_defer = budget > 0 && !s_lockstep && s_ticks - s_zb_begin < ZONE_DT1_WAIT; /* (en red no se espera al hilo) */
            s_map.piece_cap = sync ? cap : 0;
            int r = world_zone_build_step(&s_map, z, budget > 0 ? (sync ? 1e9 : budget) : parts ? 1e-6 : 0);
            s_map.dt1_defer = s_map.piece_cap = 0;
            s_zb_ms[ZB_MAP] += wall_ms() - t;
            if (r < 0) {
                s_zb_zone = -1;
                return -1;
            }
            if (r > 0) {
                s_zb_stage = ZB_PRESETS;
                s_zb_part = 0;
                if (getenv("D2_COLLHASH")) { /* depuración: la colisión de la zona recién armada (para comparar optimizaciones) */
                    uint32_t h = 2166136261u;
                    int vol = 0;
                    for (int i = 0; i < s_map.cw * s_map.ch; i++) {
                        const MapChunk *ch = s_map.chunks[i];
                        if (!ch || ch->zone != z)
                            continue;
                        for (int k = 0; k < MAP_CHUNK_SUB * MAP_CHUNK_SUB; k++) {
                            h = (h ^ ch->coll[k]) * 16777619u;
                            vol += (ch->coll[k] & COLL_VOLUME) != 0;
                        }
                    }
                    printf("colisión de %s: %08x (%d con grosor)\n", world_zone_name(z), h, vol);
                }
            }
        } else {
            /* Las unidades, de a partes también (las barricadas del Acto 5
               traen unas 700: en la Pi eran 160 ms de una): lo que queda de
               ZONE_BUDGET o, sin el hilo de trabajos, un tope de cada cosa */
            int sync = budget > 0 && (!jobs_running() || s_lockstep), begin = s_zb_part++ == 0, done;
            double left = budget > 0 && !sync ? budget - (wall_ms() - t_tick) : 0;
            if (budget > 0 && !sync && left < 0.5)
                left = 0.5;
            units_zone_begin(z);
            if (stage == ZB_PRESETS) {
                s_zb_next = units_spawn_presets(&s_map, monster_level_at, begin ? 0 : s_zb_next, sync ? ZONE_SYNC_PRESETS : 0,
                                                left);
                done = s_zb_next >= s_map.num_objects;
            } else if (stage == ZB_OBJECTS) {
                /* los cofres y barriles al azar de sus salas, y lo que ya se abrió */
                done = obj_zone_step(z, begin, sync ? ZONE_SYNC_ROOMS : 0, left);
                if (done && getenv("D2_OBJ_TEST"))
                    obj_report(z);
            } else {
                done = zone_monsters_step(z, begin, sync ? ZONE_SYNC_GROUPS : 0, left);
            }
            units_zone_end();
            if (done) {
                s_zb_stage++;
                s_zb_part = 0;
            }
            s_zb_ms[stage] += wall_ms() - t;
        }
        if (s_zb_stage > ZB_MONSTERS)
            break;
        if (budget > 0) {
            double took = wall_ms() - t_tick;
            if (took > s_zb_worst) {
                s_zb_worst = took;
                s_zb_worst_stage = stage;
            }
            return 0;
        }
    }
    quest_zone_loaded(z);
    waypoints_find();
    waypoints_light();
    double took = wall_ms() - t_tick;
    if (took > s_zb_worst) {
        s_zb_worst = took;
        s_zb_worst_stage = last;
    }
    s_zb_zone = -1;
    int dt1_ready, dt1_loaded, dt1_waits;
    map_dt1_loads(&dt1_ready, &dt1_loaded, &dt1_waits);
    double c_tiles, c_vol, c_cuts, s_all, s_ds1, s_dt1;
    map_collision_times(&c_tiles, &c_vol, &c_cuts);
    map_stamp_times(&s_all, &s_ds1, &s_dt1);
    if (s_debug || getenv("D2_ZONES"))
        printf("zona armada: %s (mapa %.0f ms, unidades %.0f ms, de eso objetos %.0f ms; %d zonas; DT1: %d preparados, "
               "%d leídos al armar, %d piezas los esperaron; colisión %.1f + grosor %.1f + recorte %.1f ms; estampar %.1f, de eso DS1 %.1f y DT1 "
               "%.1f; unidades: presets %.1f, monstruos %.1f; en %d ticks, el peor %.1f ms, %s)\n",
               world_zone_name(z), s_zb_ms[ZB_MAP], s_zb_ms[ZB_PRESETS] + s_zb_ms[ZB_OBJECTS] + s_zb_ms[ZB_MONSTERS],
               s_zb_ms[ZB_OBJECTS], zones_loaded_count(), dt1_ready, dt1_loaded, dt1_waits, c_tiles, c_vol, c_cuts, s_all, s_ds1,
               s_dt1, s_zb_ms[ZB_PRESETS], s_zb_ms[ZB_MONSTERS], s_zb_ticks, s_zb_worst,
               s_zb_ticks == 1 ? "entera" : (const char *const[]){ "mapa", "presets", "objetos", "monstruos" }[s_zb_worst_stage]);
    mem_report("armar la zona");
    return 1;
}

/* La zona entera, ahora (0 = bien, -1 = error) */
static int zone_load(int z)
{
    return zone_build(z, 0) < 0 ? -1 : 0;
}

/* ¿Hay que terminar ya de armar la zona z? Si alguien (o su mercenario)
   está adentro o a menos de ZONE_URGENT de un paso hacia ella */
static int zone_needed_now(int z)
{
    for (int i = 0; i < s_num_players; i++) {
        float px[2] = { s_pl[i].x, 0 }, py[2] = { s_pl[i].y, 0 };
        int n = 1;
        if (units_merc_alive(s_pl[i].merc_unit)) {
            units_position(s_pl[i].merc_unit, 1.0f, &px[1], &py[1]);
            n = 2;
        }
        for (int k = 0; k < n; k++) {
            int here = zone_at(px[k], py[k]);
            if (here == z)
                return 1;
            for (int l = 0; l < world_link_count(); l++) {
                const WorldLink *wl = world_link(l);
                float dx = wl->x - px[k], dy = wl->y - py[k];
                if (((wl->a == here && wl->b == z) || (wl->b == here && wl->a == z)) &&
                    dx * dx + dy * dy < ZONE_URGENT * ZONE_URGENT)
                    return 1;
            }
            for (int w = 0; w < world_warp_count(); w++) {
                const Warp *wp = world_warp(w);
                float dx = (wp->x0 + wp->x1) / 2 - px[k], dy = (wp->y0 + wp->y1) / 2 - py[k];
                if (wp->from == here && wp->to == z && dx * dx + dy * dy < ZONE_URGENT * ZONE_URGENT)
                    return 1;
            }
        }
    }
    return 0;
}

/* Cada tick: arma las zonas donde hay alguien y las de al lado de los pasos
   cercanos, y borra las que pasaron mucho tiempo vacías */
static void zones_tick(void)
{
    uint64_t pinned = 0, soon = 0;
    for (int i = 0; i < s_num_players; i++) {
        float px[2] = { s_pl[i].x, 0 }, py[2] = { s_pl[i].y, 0 };
        int n = 1;
        if (units_merc_alive(s_pl[i].merc_unit)) {
            units_position(s_pl[i].merc_unit, 1.0f, &px[1], &py[1]);
            n = 2;
        }
        for (int k = 0; k < n; k++) {
            int here = zone_at(px[k], py[k]);
            if (here < 0 || here >= ZONE_BITS)
                continue;
            pinned |= 1ull << here;
            for (int l = 0; l < world_link_count(); l++) {
                const WorldLink *wl = world_link(l);
                float dx = wl->x - px[k], dy = wl->y - py[k];
                if ((wl->a == here || wl->b == here) && dx * dx + dy * dy < ZONE_NEAR * ZONE_NEAR)
                    pinned |= 1ull << (wl->a == here ? wl->b : wl->a);
                if ((wl->a == here || wl->b == here) && dx * dx + dy * dy < ZONE_SOON * ZONE_SOON)
                    soon |= 1ull << (wl->a == here ? wl->b : wl->a);
            }
            /* Cerca de la boca de una cueva o de una escalera: la otra zona se
               va armando antes de entrar */
            for (int w = 0; w < world_warp_count(); w++) {
                const Warp *wp = world_warp(w);
                float dx = (wp->x0 + wp->x1) / 2 - px[k], dy = (wp->y0 + wp->y1) / 2 - py[k];
                if (wp->from == here && wp->to < ZONE_BITS && dx * dx + dy * dy < ZONE_NEAR * ZONE_NEAR)
                    pinned |= 1ull << wp->to;
                if (wp->from == here && wp->to < ZONE_BITS && dx * dx + dy * dy < ZONE_SOON * ZONE_SOON)
                    soon |= 1ull << wp->to;
            }
        }
    }
    s_zone_pinned = pinned;
    /* Las que se van a armar pronto: sus DT1, en el hilo de trabajos (armar la
       zona después no tiene que leerlos). Van adelante de los que esperan (al
       llegar al acto, los gráficos del pueblo: jobs_push_first). */
    s_zone_prepared &= soon;
    if (jobs_running())
        for (int z = 0; z < world_zone_count() && z < ZONE_BITS; z++)
            if (((soon & ~s_zone_prepared) >> z & 1) && !world_zone_loaded(z)) {
                char paths[34][96];
                int n = world_zone_dt1(z, paths);
                for (int k = 0; k < n; k++)
                    map_prepare_dt1(&s_map, paths[k]);
                s_zone_prepared |= 1ull << z;
                if (s_debug || getenv("D2_ZONES"))
                    printf("zona por armar: %s (se preparan %d DT1; tick %u)\n", world_zone_name(z), n, s_ticks);
            }
    /* Las ya armadas solo anotan que se vieron. Las que faltan, de a una y
       en partes (zone_build): en la Pi armar una entera tardaba de 30 a 400
       ms, y así son unos ZONE_BUDGET por tick mientras uno se acerca. Si
       alguien ya está encima, se termina de una. Antes de empezar, se
       esperan un poco los DT1 que el hilo de trabajos está leyendo. */
    int next = s_zb_zone;
    for (int z = 0; z < world_zone_count() && z < ZONE_BITS; z++)
        if ((pinned >> z) & 1) {
            if (world_zone_loaded(z) && z != s_zb_zone)
                s_zone_seen[z] = s_ticks;
            else if (next < 0)
                next = z;
        }
    /* Lo que liberó una zona borrada vuelve al sistema (malloc_trim) en un
       tick sin armar nada más: en la Pi tarda de 5 a 25 ms */
    int trimmed = s_trim_pending;
    if (s_trim_pending) {
        s_trim_pending = 0;
#ifdef __GLIBC__
        malloc_trim(0);
#endif
    }
    if (next >= 0) {
        int urgent = s_zone_budget <= 0 || zone_needed_now(next);
        if (next != s_zb_wait_zone) {
            s_zb_wait_zone = next;
            s_zb_wait = s_ticks;
        }
        /* (en red no se mira si el hilo terminó de preparar los DT1: se empieza a los ZONE_PREP_WAIT ticks) */
        if (urgent || (!trimmed && (s_zb_zone >= 0 || (!s_lockstep && !map_prep_busy()) || s_ticks - s_zb_wait >= ZONE_PREP_WAIT)))
            zone_build(next, urgent ? 0 : s_zone_budget);
    }
    /* Si alguna vez hubo que pasarse del límite (todas en uso), se vuelve a
       él apenas se desocupa una */
    while (zones_loaded_count() > s_zone_max) {
        int old = -1;
        for (int z = 0; z < world_zone_count() && z < ZONE_BITS; z++)
            if (world_zone_loaded(z) && !world_zone_is_town(z) && !((pinned >> z) & 1) && !zone_occupied(z) &&
                (old < 0 || s_ticks - s_zone_seen[z] > s_ticks - s_zone_seen[old]))
                old = z;
        if (old < 0)
            break;
        zone_unload(old);
    }
    for (int z = 0; z < world_zone_count() && z < ZONE_BITS; z++)
        if (world_zone_loaded(z) && !world_zone_is_town(z) && !((pinned >> z) & 1) &&
            s_ticks - s_zone_seen[z] >= s_zone_reset_ticks)
            zone_unload(z);
}

static void zones_unload_all(void)
{
    s_zone_pinned = s_zone_prepared = 0;
    s_zb_wait_zone = -1;
    for (int z = 0; z < world_zone_count(); z++)
        zone_unload(z);
    memset(s_zone_seen, 0, sizeof s_zone_seen);
}

/* Cambia de acto: suelta todo el mundo de este (también el pueblo) y arma el
   del otro con su paleta; los dos jugadores aparecen en el pueblo nuevo
   (al lado de Warriv, que es el que lleva) con sus mercenarias. Los
   esbirros, las trampas y las formas no viajan: se vuelven a hacer allá. */
static void act_switch(int act)
{
    ui_loading(4, 6);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &s_pl[i];
        close_ui(p);
        p->inv_open = p->wp_open = p->map_open = 0;
        if (i < s_num_players) {
            sorc_clear(p);
            pal_clear(p);
            nec_clear(p);
            ass_clear(p);
            dru_clear(p);
            war_clear(p);
            if (p->valk_unit >= 0)
                units_pet_remove(p->valk_unit);
            p->valk_unit = -1;
            if (units_merc_alive(p->merc_unit)) { /* la mercenaria viaja con su vida */
                int max_hp;
                units_merc_health(p->merc_unit, &p->merc_hp, &max_hp);
            }
        }
        p->target = p->cast_target = -1;
        p->level = NULL;
    }
    tp_reset(); /* los portales al pueblo quedan en el acto que se deja */
    s_act_leaving = 1;
    zones_unload_all();
    s_act_leaving = 0;
    items_ground_clear();
    missiles_clear();
    overlays_clear();
    bossfx_clear();
    skillfx_clear();
    automap_reset();
    map_free(&s_map);
    s_act = act >= 1 && act <= 5 ? act : 1;
    if (world_layout(&s_map, s_world_seed, s_act) != 0)
        fprintf(stderr, "no pude armar el acto %d\n", s_act);
    prefetch_act_tiles(s_act);
    char pal[8];
    snprintf(pal, sizeof pal, "act%d", s_act);
    pal_load_d2(pal);
    s_pal_cur = s_act - 1;
    zone_load(0); /* el pueblo */
    /* Dónde se aparece: en el campamento, junto a la fogata; en Lut Gholein,
       al lado de Warriv */
    float x = s_map.width * TILE_SUB / 2.0f, y = s_map.height * TILE_SUB / 2.0f;
    if (s_act == 1 && units_find("RogueBonfire", &x, &y) == 0)
        y += 5.0f;
    else if (s_act == 2 && units_find("warriv2", &x, &y) == 0)
        x += 3.0f;
    else if (s_act == 3 && units_find("meshif2", &x, &y) == 0) /* en Kurast: al bajar del barco de Meshif */
        x += 3.0f;
    else if (s_act == 4 && units_find("WaypointAct4Town", &x, &y) == 0) /* en la Fortaleza: junto al waypoint */
        y += 4.0f;
    else if (s_act == 5 && units_find("WaypointExp", &x, &y) == 0) /* en Harrogath: junto al waypoint */
        y += 4.0f;
    s_start_x = s_town_x = x;
    s_start_y = s_town_y = y;
    units_merc_hide_all();
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        place_at(p, i, x, y);
        set_player_state(p, PS_NORMAL, MODE_TN);
        p->act = s_act;
        p->waypoints |= 1ull << wp_first(s_act); /* el del pueblo */
        if (p->merc_name) {
            p->merc_unit = units_merc_spawn(i, merc_monster(p->merc_type), p->x, p->y);
            merc_apply(p, p->merc_hp > 0 ? p->merc_hp : -1);
            if (p->merc_dead)
                units_merc_hide(p->merc_unit);
        }
        save_character(p);
    }
    waypoints_find();
    waypoints_light();
    printf("acto %d: %s\n", s_act,
           s_act == 5   ? "Harrogath"
           : s_act == 4 ? "la Fortaleza del Pandemónium"
           : s_act == 3 ? "el muelle de Kurast"
           : s_act == 2 ? "Lut Gholein"
                        : "el campamento de las pícaras");
}

/* Pruebas: todas las zonas a la vez, sin límite */
static void zones_load_all(void)
{
    int saved = s_zone_max;
    s_zone_max = 1 << 20;
    for (int z = 0; z < world_zone_count(); z++)
        zone_load(z);
    s_zone_max = saved;
}

/* Un lugar libre del nivel, lo más cerca posible de su centro (pruebas) */
static int level_free_spot(const Level *lv, float *ox, float *oy)
{
    float cx = (lv->x0 + lv->x1) * TILE_SUB / 2.0f, cy = (lv->y0 + lv->y1) * TILE_SUB / 2.0f;
    for (int r = 0; r < 300; r++)
        for (int a = 0; a < 16; a++) {
            float x = cx + cosf(a * PI_F / 8) * r, y = cy + sinf(a * PI_F / 8) * r;
            if (!blocked_body(x, y) && world_level_at(x, y) == lv) {
                *ox = x;
                *oy = y;
                return 0;
            }
        }
    return -1;
}

/* D2_SPREAD: el jugador i (desde el segundo) en el nivel de afuera número i * 2 del acto, con su
   zona armada (para medir el peor caso con muchos jugadores: cada uno con su zona y sus monstruos) */
static void spread_players(void)
{
    int outdoor[64], n = 0;
    for (int i = 0; i < world_level_count() && n < 64; i++)
        if (!world_level(i)->town && world_level(i)->groups > 0)
            outdoor[n++] = i;
    for (int i = 1; i < s_num_players && n > 0; i++) {
        const Level *lv = world_level(outdoor[(i * 2) % n]);
        float x, y;
        if (zone_load(lv->zone) != 0 || level_free_spot(lv, &x, &y) != 0)
            continue;
        place_at(&s_pl[i], 0, x, y);
        printf("reparto: J%d en %s\n", i + 1, lv->name);
    }
}

/* El reporte de D2_TICK_PERF: cuántas zonas armadas hay y las unidades */
void game_perf_line(char *out, size_t cap)
{
    snprintf(out, cap, "%d zonas armadas, %d unidades, %d jugadores", zones_loaded_count(), units_count(), s_num_players);
}

/* Pruebas: D2_TOUR="Stony Field,Dark Wood" (o números de zona) lleva al
   jugador 1 a esos niveles, a uno cada D2_TOUR_EVERY ticks (por defecto
   100), para ver cómo se arman y se borran */
/* Prueba: D2_DIFFTEST=1 (en el Acto 1) muestra en cada dificultad lo de
   algunos niveles (el nivel de los monstruos, cuáles salen y cuántos jefes)
   y los números de algunos monstruos que hace salir: comunes, un jefe de
   misión, uno de nombre propio, uno al azar con su escolta y campeones
   (docs/plan-dificultades.md) */
static void diff_test(void)
{
    static int done;
    if (done || s_ticks < 5 || !getenv("D2_DIFFTEST") || s_act != 1)
        return;
    done = 1;
    int start = s_diff;
    static const char *const k_levels[] = { "Blood Moor", "Cold Plains", "Catacombs Level 4", "Moo Moo Farm" };
    float x = s_pl[0].x + 12.0f, y = s_pl[0].y;
    for (int d = 0; d < 3; d++) {
        game_set_difficulty(d);
        printf("DIF: === dificultad %d\n", d);
        for (unsigned k = 0; k < sizeof k_levels / sizeof k_levels[0]; k++) {
            const Level *lv = world_level_named(k_levels[k]);
            if (!lv)
                continue;
            printf("DIF: %-18s nivel %d, jefes %d..%d, salen %d de:", lv->name, lv->mon_level, lv->umin, lv->umax, lv->num_mon);
            for (int m = 0; m < LEVEL_MAX_MONSTERS && lv->monsters[m]; m++)
                printf(" %s", lv->monsters[m]);
            printf(" (jefes de:");
            for (int m = 0; m < lv->num_umon; m++)
                printf(" %s", lv->umons[m]);
            printf(")\n");
        }
        const Level *moor = world_level_named("Blood Moor"), *cat = world_level_named("Catacombs Level 4");
        int ml = moor ? moor->mon_level : 1;
        struct { const char *what, *id; int level; } k_mon[] = {
            { "común", "zombie1", ml },
            { "común", "fallen1", ml },
            { "de misión", "andariel", cat ? cat->mon_level : 1 },
        };
        for (unsigned k = 0; k < sizeof k_mon / sizeof k_mon[0]; k++) {
            units_zone_begin(zone_at(x, y));
            int u = units_spawn_monster(k_mon[k].id, x, y, k_mon[k].level);
            units_zone_end();
            if (u < 0)
                continue;
            char buf[256];
            units_debug_stats(u, buf, sizeof buf);
            printf("DIF: %-10s %-10s %s\n", k_mon[k].what, k_mon[k].id, buf);
            units_remove(u);
        }
        int ids[16];
        units_zone_begin(zone_at(x, y));
        int b = units_spawn_superunique("Bishibosh", x, y, ml);
        units_zone_end();
        if (b >= 0) {
            char buf[256];
            units_debug_stats(b, buf, sizeof buf);
            printf("DIF: %-10s %-10s %s\n", "de nombre", "Bishibosh", buf);
        }
        int n = units_find_all("fallenshaman1", ids, 16); /* (su escolta) */
        for (int k = 0; k < n; k++)
            units_remove(ids[k]);
        if (b >= 0)
            units_remove(b);
        units_zone_begin(zone_at(x, y));
        b = units_spawn_boss_pack("zombie1", x, y, ml);
        units_zone_end();
        n = units_find_all("zombie1", ids, 16);
        for (int k = 0; k < n; k++) {
            char buf[256];
            units_debug_stats(ids[k], buf, sizeof buf);
            printf("DIF: %-10s %-10s %s\n", ids[k] == b ? "jefe azar" : "escolta", "zombie1", buf);
            units_remove(ids[k]);
        }
    }
    game_set_difficulty(start);
}

/* Prueba: D2_TALKTEST=N abre el diálogo de cada NPC del campamento y
   aprieta "Hablar" N veces, mostrando qué dice cada vez (con las misiones
   como estén: D2_AUTOTEST_STATE no sirve acá, pero sí una partida guardada) */
static void talk_test(void)
{
    static int done;
    if (done || !getenv("D2_TALKTEST"))
        return;
    done = 1;
    int times = atoi(getenv("D2_TALKTEST"));
    static const char *const npcs[] = { "akara", "kashya", "charsi", "gheed", "warriv1", "cain1",
                                        "atma", "drognan", "elzix", "fara", "geglash", "greiz",
                                        "jerhyn", "lysander", "meshif1", "warriv2", "cain2", "act2guard2",
                                        "ormus", "alkor", "asheara", "hratli", "natalya", "meshif2", "cain3",
                                        "tyrael2", "cain4", "halbu", "jamella",
                                        "larzuk", "malah", "qual-kehk", "nihlathak", "cain6", "drehya" };
    Player *p = &s_pl[0];
    for (unsigned k = 0; k < sizeof npcs / sizeof npcs[0]; k++) {
        int u = -1;
        units_find_all(npcs[k], &u, 1);
        if (u < 0)
            continue;
        s_last_talk[0] = '\0';
        open_dialog(p, u);
        if (s_last_talk[0])
            printf("%s (al abrir): %s\n", npcs[k], s_last_talk);
        for (int t = 0; t < times; t++) {
            npc_talk(p, npcs[k]);
            printf("%s %d: %s\n", npcs[k], t + 1, s_last_talk);
        }
        int opts[8], n = npc_options(p, npcs[k], opts);
        for (int o = 0; o < n; o++)
            if (opts[o] == OPT_GAMBLE) { /* la lista de la apuesta: el objeto común, lo que es y el precio */
                static const char *const k_q[] = { "normal", "mágico", "raro", "de conjunto", "único" };
                p->dialog_sel = o;
                PlayerInput in = { 0 };
                in.action = 1;
                dialog_tick(p, &in);
                for (int i = 0; i < p->gamble_count; i++) {
                    const Item *it = &p->gamble[i];
                    printf("%s apuesta %d: %s (%s) nivel %d, %s, %d de oro\n", npcs[k], i + 1, it->type->name, it->type->code,
                           it->ilvl, it->quality < 5 ? k_q[it->quality] : "?", items_gamble_price(it->type, p->level_num));
                }
                int gold = p->gold, before = p->bag_count; /* y comprar el primero */
                p->gold = 10000000;
                p->shop_sel = 0;
                shop_tick(p, &in);
                if (p->bag_count > before)
                    printf("%s apuesta comprada: %s, quedan %d en la lista, pagó %d\n", npcs[k],
                           p->bag[p->bag_count - 1].type->name, p->gamble_count, 10000000 - p->gold);
                p->gold = gold;
            } else if (opts[o] == OPT_TRADE) { /* la tienda: cuántos objetos y los primeros */
                const Shop *sh = shop_of(npcs[k]);
                printf("%s tienda: %d objetos", npcs[k], sh ? sh->count : 0);
                for (int i = 0; sh && i < sh->count && i < 6; i++)
                    printf("%s %s", i ? "," : ":", sh->stock[i].type->name);
                printf("\n");
            } else if (opts[o] == OPT_HIRE) { /* los que ofrece (los de su acto) */
                merc_offers(p);
                for (int i = 0; i < p->num_offers; i++) {
                    MercStats ms;
                    merc_stats(p->offers[i].type, p->offers[i].level, &ms);
                    printf("%s contratar %d: %s, %s, nivel %d, %d de oro, vida %d, daño %d-%d, estocada %d%% (%+d%% daño, "
                           "%+d%% puntería), aura %s %d\n",
                           npcs[k], i + 1, merc_name_of(p->offers[i].type, p->offers[i].name),
                           str_get(s_hire_type[p->offers[i].type].desc), p->offers[i].level,
                           merc_cost(p->offers[i].type, p->offers[i].level), ms.hp_max, ms.dmin, ms.dmax, ms.jab_chance,
                           ms.jab_dmg_pct, ms.jab_ar_pct, ms.aura[0] ? ms.aura : "-", ms.aura_lvl);
                    for (int sp = 0; sp < 2 && ms.caster; sp++) /* los Lobos de Hierro */
                        if (ms.spell_missile[sp][0])
                            printf("%s contratar %d:   hechizo %s, %d%%, %d-%d x%d\n", npcs[k], i + 1, ms.spell_missile[sp],
                                   ms.spell_chance[sp], ms.spell_min[sp], ms.spell_max[sp], ms.spell_count[sp]);
                }
            } else if (opts[o] == OPT_TRADE && shop_of(npcs[k])) {
                printf("%s vende %d objetos\n", npcs[k], shop_of(npcs[k])->count);
            }
        close_ui(p);
    }
}

/* Prueba: D2_A2QTEST=1 (con D2_ACT=2) recorre las seis misiones del Acto 2
   sin caminar: habla con los NPC, mata a Radament, abre los cofres, rompe el
   altar, arma el Báculo en el cubo, entra al palacio y al Santuario por sus
   portales, mata al Invocador, lee el diario, pone el Báculo en el
   orificio, mata a Duriel y habla con Tyrael, Jerhyn y Meshif; muestra el
   registro en cada paso */
static void a2t_log(const Player *p, const char *what)
{
    const Level *lv = world_level_at(p->x, p->y);
    printf("A2: %-22s| %-22s| dijo %-28s| rada %d bastón %d (vio %2d) sol %d santuario %d invocador %d tumbas %d libro %d "
           "puntos %d oscuro %d |", what, lv ? lv->name : "?", s_last_talk, p->rada_q, p->staff_q, p->staff_seen, p->sun_q,
           p->arcane_q, p->summ_q, p->tombs_q, (p->a2_flags & A2F_BOOK) != 0, p->skill_points,
           level_dark(world_level_named("Lut Gholein")));
    for (int q = 0; q < 6; q++) {
        int st;
        const char *g = quest_info(p, 2, q, &st);
        printf(" q%d=%d %s", q + 1, st, g ? g : "-");
    }
    printf("\n");
    s_last_talk[0] = '\0';
}

static void a2t_talk(Player *p, const char *npc)
{
    int u = -1;
    if (units_find_all(npc, &u, 1) != 1)
        return;
    open_dialog(p, u);
    npc_talk(p, npc);
    close_ui(p);
}

/* Abre el objeto de misión de esa clase (lo arma si hace falta) */
static void a2t_object(Player *p, const char *cls, const char *level)
{
    const Level *lv = world_level_named(level);
    if (lv)
        zone_load(lv->zone);
    float x, y;
    if (units_find(cls, &x, &y) != 0) {
        printf("A2: no está %s\n", cls);
        return;
    }
    p->x = x + 1.0f;
    p->y = y + 1.0f;
    quest_interact(p);
    /* lo que cayó, a la mochila */
    for (int k = 0; k < 8; k++) {
        float ix, iy;
        int g = items_ground_nearest(p->x, p->y, 4.0f, 0);
        if (g < 0)
            break;
        Item it = *items_ground_item(g, &ix, &iy);
        items_ground_remove(g);
        if (strcmp(it.type->code, "gld") == 0)
            p->gold += it.amount;
        else if (!bag_add(p, &it))
            printf("A2: no entra %s\n", it.type->name);
        else
            printf("A2: levantó %s%s\n", it.name[0] ? it.name : it.type->name,
                   it.quality == QUALITY_UNIQUE ? " (único)" : "");
    }
}

/* El portal (de una misión) de una zona a la otra, NULL si no está abierto */
static const Warp *a2t_portal(int from, int to)
{
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        if (w->portal && w->from == from && w->to == to)
            return w;
    }
    return NULL;
}

/* Cruza ese portal (y avisa si no estaba) */
static void a2t_cross(Player *p, int from, int to, const char *what)
{
    const Warp *w = a2t_portal(from, to);
    if (!w) {
        printf("A2: no hay portal %s\n", what);
        return;
    }
    use_warp(p, w);
    printf("A2: cruzó el portal %s\n", what);
}

/* Mata al de ese nombre (armando su zona) */
static void a2t_kill(int zone, const char *name)
{
    int id;
    if (zone_load(zone) == 0 && units_find_all(name, &id, 1) == 1) {
        units_damage(id, 1000000);
        printf("A2: %s herido de muerte\n", name);
    } else {
        printf("A2: no está %s\n", name);
    }
}

static void a2_quest_test(void)
{
    static int step, wait;
    if (!getenv("D2_A2QTEST") || s_act != 2 || step < 0)
        return;
    Player *p = &s_pl[0];
    float tx = s_town_x, ty = s_town_y;
    switch (step) {
    case 0:
        a2t_log(p, "inicio");
        a2t_talk(p, "atma");
        a2t_log(p, "Atma");
        a2t_talk(p, "atma");
        a2t_log(p, "Atma otra vez");
        a2t_talk(p, "fara");
        a2t_log(p, "Fara");
        p->a2_flags |= A2F_SEWERS;
        a2t_log(p, "bajó a las cloacas");
        {
            const Level *lv = world_level_named("Sewers Level 3");
            int id;
            if (lv && zone_load(lv->zone) == 0 && units_find_all("Radament", &id, 1) == 1) {
                units_damage(id, 1000000);
                printf("A2: Radament herido de muerte\n");
            }
        }
        step = 1;
        break;
    case 1:
        if (!s_rada_dead && s_ticks < 600)
            return;
        a2t_log(p, "murió Radament");
        give_quest_item(p, "ass");
        for (int i = 0; i < p->bag_count; i++)
            if (strcmp(p->bag[i].type->code, "ass") == 0) {
                p->inv_sel = INV_CELL0 + p->bag_y[i] * BAG_W + p->bag_x[i];
                inventory_use(p);
                break;
            }
        a2t_log(p, "leyó el libro");
        a2t_talk(p, "atma");
        a2t_log(p, "Atma (recompensa)");
        a2t_object(p, "HoradricScrollChest", "Sewers Level 3");
        a2t_log(p, "cofre del pergamino");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain2");
        a2t_log(p, "Caín (pergamino)");
        a2t_object(p, "HoradricCubeChest", "Halls of the Dead Level 3");
        a2t_object(p, "StaffOfKingsChest", "Maggot Lair Level 3");
        a2t_log(p, "cubo y vara");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain2");
        a2t_log(p, "Caín");
        a2t_talk(p, "cain2");
        a2t_log(p, "Caín");
        { /* el eclipse: a la Lost City */
            const Level *lc = world_level_named("Lost City");
            if (lc && zone_load(lc->zone) == 0)
                place_at(p, 0, (lc->x0 + lc->x1) * TILE_SUB / 2.0f, (lc->y0 + lc->y1) * TILE_SUB / 2.0f);
        }
        step = 2;
        break;
    case 2:
        if (party_max(offsetof(Player, sun_q)) == 0 && s_ticks < 3000)
            return;
        a2t_log(p, "se oscureció");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "drognan");
        a2t_log(p, "Drognan");
        a2t_object(p, "TaintedSunShrine", "Claw Viper Temple Level 2");
        a2t_log(p, "rompió el altar");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain2");
        a2t_log(p, "Caín (amuleto)");
        a2t_talk(p, "greiz");
        a2t_log(p, "Greiz");
        a2t_talk(p, "drognan");
        a2t_log(p, "Drognan");
        /* el cubo: la vara y el amuleto adentro */
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < p->bag_count; i++)
                if (strcmp(p->bag[i].type->code, k ? "vip" : "msf") == 0) {
                    if (cube_add(p, &p->bag[i]))
                        bag_remove(p, i);
                    break;
                }
        cube_transmute_now(p);
        for (int c = 0; c < p->cube_count; c++)
            printf("A2: en el cubo %s%s\n", p->cube[c].name[0] ? p->cube[c].name : p->cube[c].type->name,
                   p->cube[c].quality == QUALITY_UNIQUE ? " (único)" : "");
        a2t_log(p, "transmutó");
        a2t_talk(p, "cain2");
        a2t_log(p, "Caín (Báculo)");
        /* el palacio: Kaelan y la puerta */
        a2t_talk(p, "act2guard2");
        a2t_log(p, "Kaelan");
        a2t_talk(p, "drognan");
        a2t_log(p, "Drognan (palacio)");
        a2t_talk(p, "jerhyn");
        a2t_log(p, "Jerhyn (palacio)");
        { /* la puerta del palacio: el salto de Lut Gholein al harén */
            int town = world_zone_at(&s_map, tx, ty), harem = world_level_zone(50);
            for (int i = 0; i < world_warp_count(); i++)
                if (!world_warp(i)->portal && world_warp(i)->from == town && world_warp(i)->to == harem) {
                    use_warp(p, world_warp(i));
                    break;
                }
        }
        wait = (int)s_ticks + 3;
        step = 3;
        break;
    case 3: /* del harén, por el portal del sótano 3, al Santuario */
        if ((int)s_ticks < wait)
            return;
        a2t_log(p, "puerta del palacio");
        zone_load(world_level_zone(54));
        a2t_cross(p, world_level_zone(54), world_level_zone(74), "del sótano 3 al Santuario");
        wait = (int)s_ticks + 3;
        step = 4;
        break;
    case 4:
        if ((int)s_ticks < wait)
            return;
        a2t_log(p, "en el Santuario");
        wait = (int)s_ticks;
        step = 41;
        break;
    case 41: { /* del portal al waypoint caminando (la escalera angosta en diagonal) */
        static BotNav *nav;
        int ids[4], n = units_find_all("WaypointDesertValley", ids, 4), wp = -1;
        for (int k = 0; k < n; k++)
            if (units_zone(ids[k]) == world_level_zone(74))
                wp = ids[k];
        float wx = 0, wy = 0, dx, dy;
        if (wp >= 0)
            units_position(wp, 1.0f, &wx, &wy);
        if (!nav)
            nav = bot_nav_new(12345u);
        int r = wp >= 0 ? bot_nav_step(nav, &s_map, s_ticks, p->x, p->y, -1, wx, wy, 3.0f, &dx, &dy) : BOT_NO_PATH;
        if (r == BOT_MOVING) {
            player_move(p, dx - dy, (dx + dy) * 0.5f, 1);
            if ((int)s_ticks - wait < 25 * TICK_RATE)
                return;
        }
        printf("A2: del portal al waypoint del Santuario: %s en %d ticks (a %.1f subtiles)\n",
               r == BOT_ARRIVED ? "llegó" : r == BOT_NO_PATH ? "sin camino" : "no llegó", (int)s_ticks - wait,
               sqrtf((p->x - wx) * (p->x - wx) + (p->y - wy) * (p->y - wy)));
        a2t_kill(world_level_zone(74), "summoner");
        step = 5;
        break;
    }
    case 5:
        if (!s_summ_dead && s_ticks < 4000)
            return;
        a2t_log(p, "murió el Invocador");
        {
            int ids[8], n = units_find_all("PlaceArcaneThingamajig", ids, 8), runes = 0;
            for (int r = 1; r <= 7; r++) {
                char cls[16];
                int id;
                snprintf(cls, sizeof cls, "ArcaneRune%d", r);
                if (units_find_all(cls, &id, 1) == 1) {
                    runes++;
                    printf("A2: flota el glifo %d\n", r);
                }
            }
            printf("A2: %d lugares para glifos, %d glifos; la tumba verdadera es la %d (glifo %d)\n", n, runes,
                   world_staff_tomb() - 65, tomb_rune(world_staff_tomb()));
        }
        a2t_object(p, "ArcaneTome", "Arcane Sanctuary");
        a2t_log(p, "leyó el diario");
        a2t_cross(p, world_level_zone(74), world_level_zone(46), "del diario al Canyon");
        wait = (int)s_ticks + 3;
        step = 6;
        break;
    case 6:
        if ((int)s_ticks < wait)
            return;
        a2t_log(p, "en el Canyon");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "atma");
        a2t_log(p, "Atma (Invocador)");
        a2t_talk(p, "jerhyn");
        a2t_log(p, "Jerhyn");
        { /* el orificio de la tumba verdadera, con el Báculo */
            int z = world_level_zone(world_staff_tomb());
            float x, y;
            int id;
            if (zone_load(z) == 0 && units_find_all("SevenTombsReceptacle", &id, 1) == 1) {
                units_position(id, 1.0f, &x, &y);
                p->x = x + 1.0f;
                p->y = y + 1.0f;
                int u2;
                const char *act = quest_action(p, &u2);
                printf("A2: en el orificio: %s\n", act ? act : "(nada)");
                quest_interact(p);
                a2t_log(p, "puso el Báculo");
                a2t_cross(p, z, world_level_zone(73), "del orificio a la guarida");
            } else {
                printf("A2: no está el orificio\n");
            }
        }
        wait = (int)s_ticks + 3;
        step = 7;
        break;
    case 7:
        if ((int)s_ticks < wait)
            return;
        a2t_log(p, "en la guarida");
        a2t_kill(world_level_zone(73), "duriel");
        step = 8;
        break;
    case 8:
        if (!s_duriel_dead && s_ticks < 6000)
            return;
        a2t_log(p, "murió Duriel");
        a2t_talk(p, "tyrael1");
        a2t_log(p, "Tyrael");
        a2t_cross(p, world_level_zone(73), world_zone_at(&s_map, tx, ty), "de Tyrael al pueblo");
        wait = (int)s_ticks + 3;
        step = 9;
        break;
    case 9:
        if ((int)s_ticks < wait)
            return;
        a2t_log(p, "en el pueblo");
        a2t_talk(p, "jerhyn");
        a2t_log(p, "Jerhyn (final)");
        a2t_talk(p, "meshif1");
        a2t_log(p, "Meshif");
        {
            int opts[8], n = npc_options(p, "meshif1", opts), sail = -1, u = -1;
            for (int k = 0; k < n; k++)
                if (opts[k] == OPT_TRAVEL)
                    sail = k;
            printf("A2: Meshif ofrece navegar al este: %s\n", sail >= 0 ? "sí" : "no");
            if (sail >= 0 && units_find_all("meshif1", &u, 1) == 1) { /* y navega: el muelle de Kurast */
                PlayerInput pi;
                memset(&pi, 0, sizeof pi);
                pi.action = 1;
                open_dialog(p, u);
                p->dialog_sel = sail;
                dialog_tick(p, &pi);
                const Level *lv = world_level_at(p->x, p->y);
                printf("A2: navegó con Meshif: acto %d, %s\n", s_act, lv ? lv->name : "?");
            }
        }
        step = -1;
        break;
    }
}

#include "a3qtest.inc"
#include "a4qtest.inc"
#include "cowtest.inc"
#include "ubertest.inc"
#include "tztest.inc"

static void debug_tour(void)
{
    const char *tour = getenv("D2_TOUR");
    int every = getenv("D2_TOUR_EVERY") ? atoi(getenv("D2_TOUR_EVERY")) : 100;
    if (!tour || every <= 0 || s_ticks % (uint32_t)every != 0)
        return;
    int step = (int)(s_ticks / (uint32_t)every);
    char item[64] = "";
    for (int k = 0; k <= step; k++) {
        if (!*tour)
            return; /* se terminó el recorrido */
        size_t len = strcspn(tour, ",");
        snprintf(item, sizeof item, "%.*s", (int)(len < sizeof item ? len : sizeof item - 1), tour);
        tour += len + (tour[len] == ',');
    }
    const Level *lv = world_level_named(item);
    for (int i = 0; !lv && item[0] >= '0' && item[0] <= '9' && i < world_level_count(); i++)
        if (world_level(i)->zone == atoi(item))
            lv = world_level(i);
    if (!lv || zone_load(lv->zone) != 0)
        return;
    Player *p = &s_pl[0];
    float x, y;
    if (level_free_spot(lv, &x, &y) == 0) {
        p->x = p->prev_x = x;
        p->y = p->prev_y = y;
        printf("recorrido: jugador 1 a %s (%.0f, %.0f)\n", lv->name, x, y);
        for (int i = 1; i < s_num_players; i++) { /* los demás, al lado */
            Player *o = &s_pl[i];
            o->x = x + 2.0f * i;
            o->y = y;
            find_free_spot(&o->x, &o->y);
            o->prev_x = o->x;
            o->prev_y = o->y;
        }
    }
}

/* ---- Las invocaciones de un jugador, para la barra de vida ----
   Se agrupan por la habilidad que las invocó: la que tiene su monstruo en
   "summon" de skills.txt (Revivir y Vincular demonio, por cómo salieron).
   Los muros de hueso, las trampas y la Hidra no van: no tienen vida que
   mostrar. La prueba de habilidades revisa que ninguna quede afuera. */

#define PET_GROUPS 8
typedef struct { int skill, count, hp, max; } PetGroup;

static int pet_skill(const Player *p, int unit, int kind)
{
    if (unit == p->bind_unit)
        return skills_find(p->cls, "Bind Demon");
    const char *id = kind == PK_REVIVE ? "revive" : units_id(unit);
    for (int i = 0; i < skills_count(p->cls); i++)
        if (strcmp(skills_get(p->cls, i)->summon, id) == 0)
            return i;
    return -1;
}

/* Suma la unidad a su grupo; los grupos quedan en el orden del árbol */
static void pet_group_add(const Player *p, PetGroup *g, int *ng, int unit, int kind, int *lost)
{
    int s = pet_skill(p, unit, kind), k = 0, hp, max;
    if (s < 0) {
        (*lost)++;
        return;
    }
    while (k < *ng && g[k].skill < s)
        k++;
    if (k == *ng || g[k].skill != s) {
        if (*ng == PET_GROUPS) {
            (*lost)++;
            return;
        }
        memmove(&g[k + 1], &g[k], (size_t)(*ng - k) * sizeof *g);
        g[k] = (PetGroup){ s, 0, 0, 0 };
        (*ng)++;
    }
    units_health(unit, &hp, &max);
    g[k].count++;
    g[k].hp += hp > 0 ? hp : 0;
    g[k].max += max;
}

/* Los grupos de invocaciones vivas; *lost: las que no tienen casillero */
static int pet_groups(const Player *p, PetGroup *g, int *lost)
{
    int ng = 0;
    *lost = 0;
    for (int k = 0; k < NEC_PETS; k++)
        if (p->pet_kind[k] != PK_NONE && p->pet_kind[k] != PK_WALL && units_pet_alive(p->pets[k]))
            pet_group_add(p, g, &ng, p->pets[k], p->pet_kind[k], lost);
    if (units_pet_alive(p->valk_unit))
        pet_group_add(p, g, &ng, p->valk_unit, PK_NONE, lost);
    if (units_pet_alive(p->shadow_unit))
        pet_group_add(p, g, &ng, p->shadow_unit, PK_NONE, lost);
    if (p->bind_unit >= 0 && units_converted(p->bind_unit) && units_pet_alive(p->bind_unit))
        pet_group_add(p, g, &ng, p->bind_unit, PK_NONE, lost);
    return ng;
}

#include "autotest.inc"
#include "skilltest.inc"

static int bots_playing(void)
{
    return s_demo || s_at.active;
}

/* D2_STASH_TEST=1: prueba sin gráficos del cofre, los Pergaminos de Portal
   apilados y el oro al morir, con el J1 (después lo deja como estaba). Dice
   BIEN o MAL por cada cosa. */
static int stash_check(int ok, const char *what, int *bad)
{
    printf("  cofre: %s %s\n", ok ? "BIEN" : "MAL ", what);
    *bad += !ok;
    return ok;
}

/* Pruebas: D2_SHOP_TEST=1 vende desde el inventario con controles de mentira (las solapas con LB / RB,
   Select, la cruceta pasando los bordes, A vende y el vendedor lo pone a la venta); 2 deja abierta la
   tienda de Charsi para una captura (3: vendiendo), para los de esta máquina */
static void shop_test(void)
{
    static int done;
    if (done || s_ticks < 5 || !getenv("D2_SHOP_TEST"))
        return;
    done = 1;
    int mode = atoi(getenv("D2_SHOP_TEST")), u = -1;
    units_find_all("charsi", &u, 1);
    if (u < 0) {
        printf("prueba de la tienda: no está Charsi (es en el Acto 1)\n");
        return;
    }
    static const char *const k_bag[] = { "lsd", "cap", "rin", "hp1", "tsc", "bks" };
    for (int i = 0; i < s_num_local; i++) {
        Player *p = &s_pl[s_local[i]];
        p->bag_count = 0;
        for (unsigned k = 0; k < sizeof k_bag / sizeof k_bag[0]; k++) {
            Item it = items_make(items_find(k_bag[k]));
            bag_add(p, &it);
        }
        p->gold = 5000;
        open_dialog(p, u);
        p->shop_open = 1;
        p->gambling = p->shop_selling = 0;
        p->shop_tab = SHOP_WEAPONS;
        p->shop_sel = 0;
        p->inv_sel = 0;
        if (mode == 3)
            shop_set_tab(p, SHOP_SELL);
    }
    if (mode != 1)
        return;
    Player *p = &s_pl[s_local[0]];
    Shop *sh = shop_of("charsi");
    PlayerInput in;
    int bad = 0;
#define SHOP_PRESS(field) (memset(&in, 0, sizeof in), in.field = 1, shop_tick(p, &in))
    printf("prueba de la tienda (Charsi, %d objetos a la venta)\n", sh->count);
    SHOP_PRESS(automap);
    stash_check(!p->shop_selling && p->shop_tab == SHOP_MISC, "RB: de Armas a Varios", &bad);
    SHOP_PRESS(automap);
    stash_check(p->shop_selling && p->inv_sel >= INV_CELL0 && p->inv_sel < INV_BELT0,
                "RB otra vez: Vender, con el cursor en la mochila", &bad);
    int cell = p->inv_sel - INV_CELL0, k = bag_at(p, cell % BAG_W, cell / BAG_W);
    stash_check(k >= 0 && !is_quest_item(p->bag[k].type), "el cursor está sobre algo que se puede vender", &bad);
    if (k < 0)
        return;
    Item it = p->bag[k];
    int gold = p->gold, bag = p->bag_count, stock = sh->count, price = items_price(&it, 1);
    SHOP_PRESS(action);
    stash_check(price > 0 && p->gold == gold + price && p->bag_count == bag - 1, "A: lo vende y le pagan lo que decía", &bad);
    stash_check(shop_unlimited(it.type) ? sh->count == stock
                                        : sh->count == stock + 1 && sh->stock[sh->count - 1].type == it.type,
                "el vendedor lo pone a la venta", &bad);
    int from = p->inv_sel, item = -1;
    for (int t = 0; t < BAG_CELLS && item < 0; t++) { /* derecha hasta el próximo objeto */
        SHOP_PRESS(nav_right);
        cell = p->inv_sel - INV_CELL0;
        item = bag_at(p, cell % BAG_W, cell / BAG_W);
        if (!p->shop_selling)
            break;
    }
    stash_check(p->shop_selling && p->inv_sel != from && item >= 0, "la cruceta recorre la mochila", &bad);
    int quest = -1;
    for (int c = 0; c < BAG_CELLS && quest < 0; c++) {
        int q = bag_at(p, c % BAG_W, c / BAG_W);
        if (q >= 0 && is_quest_item(p->bag[q].type))
            quest = c;
    }
    if (quest >= 0) {
        p->inv_sel = INV_CELL0 + quest;
        bag = p->bag_count;
        gold = p->gold;
        SHOP_PRESS(action);
        stash_check(p->bag_count == bag && p->gold == gold, "lo de las misiones no se vende", &bad);
    }
    for (int t = 0; t < BAG_W + 2 && p->shop_selling; t++)
        SHOP_PRESS(nav_left);
    stash_check(!p->shop_selling && p->shop_tab == SHOP_MISC, "pasado el borde izquierdo: vuelve a la mercadería", &bad);
    SHOP_PRESS(inventory);
    stash_check(p->shop_selling && p->shop_open, "Select: a vender (sin cerrar la tienda)", &bad);
    SHOP_PRESS(inventory);
    stash_check(!p->shop_selling && p->shop_tab == SHOP_MISC, "Select otra vez: a la solapa de antes", &bad);
    SHOP_PRESS(potion_life);
    stash_check(!p->shop_selling && p->shop_tab == SHOP_WEAPONS, "LB: Armas", &bad);
    SHOP_PRESS(potion_life);
    SHOP_PRESS(potion_life);
    stash_check(p->shop_selling, "LB desde Armadura: Vender (da la vuelta)", &bad);
    SHOP_PRESS(automap);
    stash_check(!p->shop_selling && p->shop_tab == SHOP_ARMOR, "RB desde Vender: Armadura", &bad);
    p->gold = 1000000;
    p->shop_sel = 0;
    gold = p->gold;
    bag = p->bag_count;
    stock = sh->count;
    Item *list[SHOP_LIST_MAX];
    int n = shop_list(p, list, SHOP_LIST_MAX), cost = n > 0 ? items_price(list[0], 0) : 0;
    SHOP_PRESS(action);
    stash_check(n > 0 && p->gold == gold - cost && p->bag_count == bag + 1, "A en la mercadería: compra", &bad);
    SHOP_PRESS(back);
    stash_check(!p->shop_open && p->dialog_npc == u, "B: vuelve al menú de Charsi", &bad);
#undef SHOP_PRESS
    close_ui(p);
    printf("prueba de la tienda: %s (%d mal)\n", bad ? "FALLA" : "BIEN", bad);
}

static void stash_test(void)
{
    static int done;
    if (done || s_ticks < 5 || !getenv("D2_STASH_TEST"))
        return;
    done = 1;
    Player *p = &s_pl[0];
    const ItemType *tsc = items_find("tsc"), *key = items_find("key"), *tbk = items_find("tbk");
    int mode = atoi(getenv("D2_STASH_TEST"));
    if (mode >= 2) { /* para las capturas: el cofre abierto con cosas (2: la pestaña "+", 3: un objeto, 4: en la mano) */
        p->act_max = 3;
        p->stash_bought = 1;
        p->stash_gold = 12345;
        p->gold = 70000;
        const char *codes[] = { "qui", "key", "tsc", "rin", "hp1", "lsd", "amu", "cap" };
        for (unsigned k = 0; k < sizeof codes / sizeof codes[0]; k++) {
            const ItemType *t = items_find(codes[k]);
            if (!t)
                continue;
            Item it = items_make(t);
            if (t == key || t == tsc)
                it.amount = t == key ? 12 : 17;
            stash_add(p, 0, &it);
        }
        stash_show(p);
        p->inv_sel = mode == 2 ? INV_STAB0 + stash_tabs(p) : INV_STASH0;
        if (mode == 4) {
            inventory_lift(p);
            p->inv_sel = INV_STASH0 + 4 + STASH_W * 2;
        }
        return;
    }
    static Player saved; /* (para dejarlo como estaba) */
    saved = *p;
    int bad = 0;
    printf("prueba del cofre (J1 en %s, acto %d, el más alto %d)\n", p->level ? p->level->name : "?", p->act, p->act_max);

    /* Los pergaminos se apilan de a TP_STACK; el tomo ya no se vende ni cae */
    p->bag_count = 0;
    for (int k = 0; k < 25; k++) {
        Item it = items_make(tsc);
        bag_add(p, &it);
    }
    stash_check(p->bag_count == 2 && stack_count(&p->bag[0]) + stack_count(&p->bag[1]) == 25 &&
                    (stack_count(&p->bag[0]) == TP_STACK || stack_count(&p->bag[1]) == TP_STACK),
                "25 pergaminos: una pila de 20 y otra de 5", &bad);
    stash_check(tp_find(p) >= 0 && stack_count(&p->bag[tp_find(p)]) == 5, "se gasta primero la pila chica", &bad);
    stash_check(!items_supported(tbk), "el Tomo de Portal no se vende ni cae", &bad);
    Item stack = p->bag[0];
    stash_check(items_price(&stack, 0) == items_price(&(Item){ .type = tsc, .amount = 1 }, 0) * stack_count(&stack),
                "una pila vale lo que sus pergaminos", &bad);
    /* Una partida vieja con un tomo de 7 cargas: 7 pergaminos más */
    p->bag_count = 0;
    Item one = items_make(tsc), tome = items_make(tbk);
    tome.amount = 7;
    bag_add(p, &one);
    bag_add_at(p, &tome, 5, 0);
    tomes_to_scrolls(p);
    stash_check(p->bag_count == 1 && stack_count(&p->bag[0]) == 8, "el tomo viejo pasa a ser 7 pergaminos", &bad);
    stash_check(shop_fill_count(p, tsc) == TP_STACK - 8, "X en la tienda: los 12 que faltan para la pila", &bad);

    /* Las pestañas: una por acto, más las compradas al triple cada vez */
    int act_max = p->act_max;
    p->stash_bought = 0;
    p->act_max = 1;
    stash_check(stash_tabs(p) == 1, "en el Acto 1, una pestaña", &bad);
    p->act_max = 3;
    stash_check(stash_tabs(p) == 3, "llegó al Acto 3: tres", &bad);
    p->gold = 60000;
    p->stash_gold = 0;
    stash_buy(p);
    stash_check(p->stash_bought == 1 && stash_tabs(p) == 4 && p->gold == 10000 && stash_price(p) == 3 * STASH_PRICE,
                "compró la cuarta (50000) y la que sigue cuesta el triple", &bad);
    p->gold = 100000;
    stash_buy(p);
    stash_check(p->stash_bought == 1 && p->gold == 100000, "sin oro suficiente no se compra", &bad);
    p->stash_gold = 60000;
    stash_buy(p);
    stash_check(p->stash_bought == 2 && p->gold == 0 && p->stash_gold == 10000, "paga con lo que lleva y el resto del cofre",
                &bad);
    for (int k = 0; k < 10; k++) {
        p->gold = p->stash_gold = 1000000000;
        stash_buy(p);
    }
    stash_check(p->stash_bought == STASH_BUY_MAX && !stash_can_buy(p), "no se compran más de 5", &bad);

    /* El oro: todo al cofre y de vuelta */
    p->gold = 1234;
    p->stash_gold = 100;
    stash_move_gold(p, 1);
    stash_check(p->gold == 0 && p->stash_gold == 1334, "guarda todo el oro", &bad);
    stash_move_gold(p, 0);
    stash_check(p->gold == 1334 && p->stash_gold == 0, "lo saca todo", &bad);

    /* Los objetos: X de la mochila al cofre y vuelta, las pilas se juntan */
    for (int t = 0; t < STASH_TABS; t++)
        p->stash_count[t] = 0;
    p->bag_count = 0;
    Item keys = items_make(key);
    keys.amount = 5;
    bag_add(p, &keys);
    stash_show(p);
    stash_check(p->inv_open && p->stash_open && p->inv_sel == INV_STASH0, "A en el cofre lo abre en el inventario", &bad);
    p->stash_tab = 2;
    p->inv_sel = INV_CELL0;
    inventory_use(p);
    stash_check(p->bag_count == 0 && p->stash_count[2] == 1 && p->stash[2][0].amount == 5, "X pasa las llaves a la pestaña 3",
                &bad);
    bag_add(p, &keys);
    inventory_use(p);
    stash_check(p->stash_count[2] == 1 && p->stash[2][0].amount == 10, "y otras 5 se juntan con esas", &bad);
    const ItemType *big = items_find("qui"); /* (una armadura de 2x3) */
    int fit = 0;
    for (int k = 0; big && k < 40; k++) {
        Item it = items_make(big);
        fit += stash_add(p, 0, &it);
    }
    stash_check(big && fit == (STASH_W / big->inv_w) * (STASH_H / big->inv_h), "la pestaña entera de armaduras", &bad);
    p->inv_sel = INV_STASH0;
    p->stash_tab = 0;
    inventory_use(p);
    stash_check(p->stash_count[0] == fit - 1 && p->bag_count == 1, "X del cofre a la mochila", &bad);
    p->inv_sel = INV_STASH0 + 2; /* (la de x = 2, arriba) */
    inventory_lift(p);
    stash_check(p->held.type == big && p->stash_count[0] == fit - 2, "A levanta del cofre", &bad);
    p->inv_sel = INV_STAB0 + 1;
    inventory_place(p);
    stash_check(p->stash_tab == 1 && p->held.type == big, "A en otra pestaña la muestra, con lo de la mano", &bad);
    p->inv_sel = INV_STASH0;
    inventory_place(p);
    stash_check(!p->held.type && p->stash_count[1] == 1, "y lo suelta ahí", &bad);

    /* Se guarda y se vuelve a cargar igual */
    p->stash_gold = 4321;
    int slot = p->slot;
    if (!slot)
        p->slot = SAVE_SLOTS;
    save_character(p);
    static Player back;
    memset(&back, 0, sizeof back);
    back.cls = p->cls;
    back.slot = p->slot;
    int loaded = load_character(&back);
    int same = loaded && back.stash_gold == 4321 && back.stash_bought == p->stash_bought && back.act_max == p->act_max;
    for (int t = 0; t < STASH_TABS; t++)
        same &= back.stash_count[t] == p->stash_count[t];
    same &= back.stash[2][0].type == key && back.stash[2][0].amount == 10 && back.stash_x[1][0] == p->stash_x[1][0];
    stash_check(same, "se guarda y se carga igual (pestañas, lugares, pilas, oro)", &bad);
    inventory_close(p);
    stash_check(!p->inv_open && !p->stash_open, "B lo cierra", &bad);

    /* A al lado del cofre del pueblo (en las pruebas automáticas no: los bots no lo usan) */
    int bank;
    if (units_find_all("Bank", &bank, 1) == 1) {
        float bx, by;
        units_position(bank, 1.0f, &bx, &by);
        place_at(p, 0, bx + 1.5f, by + 1.5f);
        int opened = obj_interact(p) && p->stash_open;
        stash_check(bots_playing() ? !opened : opened,
                    bots_playing() ? "los bots no abren el cofre del pueblo" : "A al lado del cofre del pueblo lo abre", &bad);
        inventory_close(p);
    } else {
        stash_check(0, "hay un cofre en el pueblo", &bad);
    }

    /* Al morir: el nivel % (hasta 20) del oro que lleva; el del cofre no */
    *p = saved;
    int lvl = p->level_num;
    p->level_num = 10;
    p->gold = 10000;
    p->stash_gold = 5000;
    set_player_state(p, PS_DYING, MODE_DT);
    stash_check(p->gold == 9000 && p->gold_lost == 1000 && p->stash_gold == 5000, "al morir en el nivel 10 pierde el 10%", &bad);
    *p = saved;
    p->level_num = lvl;
    if (!slot) {
        char path[300];
        if (save_path(SAVE_SLOTS, path, sizeof path))
            remove(path);
    } else {
        save_character(p);
    }
    (void)act_max;
    printf("prueba del cofre: %s (%d mal)\n", bad ? "MAL" : "BIEN", bad);
}

static const int k_demo_rotation[][2] = {
    { 7, 3 }, /* primero el Brujo con el Paladín */
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 0 },
};

int game_demo_rotation_count(void)
{
    return (int)(sizeof k_demo_rotation / sizeof k_demo_rotation[0]);
}

void game_demo_rotation(int i, int *cls1, int *cls2)
{
    int n = game_demo_rotation_count();
    i = ((i % n) + n) % n;
    *cls1 = k_demo_rotation[i][0];
    *cls2 = k_demo_rotation[i][1];
}

void game_demo_start(int cls)
{
    cls = cls >= 0 && cls < NUM_CLASSES ? cls : 0;
    game_demo_start_pair(cls, (cls + 1) % NUM_CLASSES);
}

void game_demo_start_pair(int cls, int cls2)
{
    static const int slots[MAX_PLAYERS] = { 0, 0 }; /* lugar 0: personaje nuevo que no se guarda */
    cls = cls >= 0 && cls < NUM_CLASSES ? cls : 0;
    cls2 = cls2 >= 0 && cls2 < NUM_CLASSES ? cls2 : (cls + 1) % NUM_CLASSES;
    int classes[MAX_PLAYERS] = { cls, cls2 };
    /* D2_DEMO_PLAYERS=1: uno solo (por defecto juegan los dos, cada uno con su bot) */
    int players = getenv("D2_DEMO_PLAYERS") ? atoi(getenv("D2_DEMO_PLAYERS")) : 2; /* (1, o hasta 8 para probar) */
    players = players < 1 ? 1 : players > MAX_PLAYERS ? MAX_PLAYERS : players;
    s_demo = 1;
    s_eco = 0; /* (la demostración es la historia) */
    game_set_difficulty(0); /* (en Normal, aunque la última partida fuera en otra; D2_DIFF la cambia) */
    game_start_slots(players, slots, classes);
    /* Por defecto, la campaña: los Actos 1 a 5 en Normal, Pesadilla e
       Infierno, con todas las habilidades (las va usando una por una) y el
       nivel de cada acto (demo_show_setup: 30, 40, 50, 60 y 70, y 25 más en
       Pesadilla y 50 en Infierno); con D2_DEMO_ACT=1..5, solo ese acto;
       D2_DEMO_MODE=nuevo, desde el nivel 1 (sube y elige sus habilidades), y
       D2_DEMO_MODE=habilidades, la prueba contra Andariel */
    memset(&s_camp, 0, sizeof s_camp);
    const char *mode = getenv("D2_DEMO_MODE");
    if (mode && (strcmp(mode, "habilidades") == 0 || strcmp(mode, "skills") == 0) && skilltest_start() == 0) {
        s_st.demo = 1;
        s_one_hit = 0;
        s_god = 2;
        printf("demostración: habilidades contra Andariel\n");
        return;
    }
    int dact = getenv("D2_DEMO_ACT") ? atoi(getenv("D2_DEMO_ACT")) : 0;
    int from = getenv("D2_DEMO_FROM") ? atoi(getenv("D2_DEMO_FROM")) : 1; /* la campaña desde ese acto */
    s_camp.on = dact < 1 || dact > 5;
    s_camp.act = s_camp.first_act = !s_camp.on ? dact : from >= 1 && from <= 5 ? from : 1;
    s_camp.first_diff = s_diff;
    s_demo_all = !(mode && strcmp(mode, "nuevo") == 0);
    printf("demostración: %s%s%s%s%s\n", game_class_name(cls), players > 1 ? " y " : "",
           players > 1 ? game_class_name(classes[1]) : "", s_demo_all ? ", con todas las habilidades" : "",
           s_camp.on ? "; la campaña: los cinco actos en cada dificultad" : "");
    demo_act_start(s_camp.act);
}

/* Un acto de la demostración: la prueba de ese acto con los bots, desde
   donde están (con el estado de los actos anteriores) */
static void demo_act_start(int act)
{
    static const char *const k_names[5] = { "act1", "act2", "act3", "act4", "act5" };
    /* En la campaña, lo de misión que quedó de antes (los Libros de habilidad
       sin leer, la estatuilla o el Mangual de un acto que se cortó, el
       Martillo) ya no sirve, menos el Cubo: se tira. Si no, se junta de un
       acto a otro y de una dificultad a otra hasta llenar la mochila (en el
       Acto 4 de Pesadilla no entraba el Martillo de la Forja) */
    for (int i = 0; s_camp.on && i < s_num_players; i++) {
        Player *p = &s_pl[i];
        int gone = 0, box = bag_index_of(p, "box"); /* (uno solo: cada dificultad da otro en el Acto 2) */
        for (int k = p->bag_count - 1; k >= 0; k--)
            if (is_quest_item(p->bag[k].type) && k != box) {
                bag_remove(p, k);
                box -= box > k;
                gone++;
            }
        for (int k = p->cube_count - 1; k >= 0; k--)
            if (is_quest_item(p->cube[k].type)) {
                cube_remove(p, k);
                gone++;
            }
        for (int s = 0; s < SLOT_COUNT; s++)
            if (p->equip[s].type && is_quest_item(p->equip[s].type)) {
                memset(&p->equip[s], 0, sizeof p->equip[s]);
                gone++;
            }
        if (gone) {
            refresh_look(p);
            refresh_stats(p);
            printf("  demo J%d tira %d objetos de misión de antes\n", i + 1, gone);
        }
    }
    game_autotest_start(k_names[act - 1], s_num_players);
    demo_start();
    s_one_hit = getenv("D2_ONEHIT") != NULL; /* pelea de verdad (la prueba mata de un golpe) */
    s_god = 2;                               /* recibe daño pero no muere */
    for (int i = 0; s_demo_all && i < s_num_players; i++)
        demo_show_setup(i);
    printf("demostración: el Acto %d en %s\n", act, k_diff_names[s_diff]);
}

/* Terminó un acto de la campaña (r: 0 bien, -1 mal; si falló se sigue igual,
   con lo de ese acto dado por hecho): el que sigue o, después del Acto 5, el
   Acto 1 de la dificultad que sigue, con los mismos personajes. 0 = se
   terminó la campaña */
static int demo_campaign_next(int r)
{
    if (!s_demo || !s_camp.on || s_camp.act < 1 || s_camp.act > 5)
        return 0;
    s_camp.result[s_diff][s_camp.act - 1] = r == 0 && at_all_ok() ? 1 : -1;
    s_camp.ticks[s_diff][s_camp.act - 1] = s_at.ticks;
    if (s_camp.act == 5 && s_diff == 2)
        return 0; /* (el informe de este acto y el de la campaña: game_autotest_report) */
    s_camp.mid = 1;
    game_autotest_report(); /* el de este acto */
    s_camp.mid = 0;
    if (s_camp.act < 5) {
        s_camp.act++;
    } else { /* como una partida nueva en la otra dificultad, con el personaje guardado */
        int slots[MAX_PLAYERS] = { 0 }, classes[MAX_PLAYERS] = { 0 };
        for (int i = 0; i < s_num_players; i++) {
            classes[i] = s_pl[i].cls;
            s_carry[i] = character_text(&s_pl[i], &s_carry_len[i]);
        }
        game_set_difficulty(s_diff + 1);
        game_start_slots(s_num_players, slots, classes);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            free(s_carry[i]);
            s_carry[i] = NULL;
        }
        s_camp.act = 1;
    }
    printf("\nla campaña sigue: ");
    demo_act_start(s_camp.act);
    return 1;
}

/* Cómo fue cada acto de la campaña (al terminar o al cortarla) */
static void demo_campaign_report(void)
{
    int ok = 1;
    printf("\n== La campaña de la demostración ==\n");
    for (int d = s_camp.first_diff; d < 3; d++) {
        printf("%s:", k_diff_names[d]);
        for (int a = d == s_camp.first_diff ? s_camp.first_act - 1 : 0; a < 5; a++) {
            int res = s_camp.result[d][a];
            ok &= res == 1;
            printf(" Acto %d %s", a + 1, res > 0 ? "bien" : res < 0 ? "MAL" : "no se jugó");
            if (res)
                printf(" (%.0f min)", s_camp.ticks[d][a] / (double)TICK_RATE / 60.0);
            printf(a < 4 ? "," : "\n");
        }
    }
    printf("RESULTADO: %s\n", ok ? "BIEN" : "MAL");
}

static int demo_campaign_ok(void)
{
    for (int d = s_camp.first_diff; d < 3; d++)
        for (int a = d == s_camp.first_diff ? s_camp.first_act - 1 : 0; a < 5; a++)
            if (s_camp.result[d][a] != 1)
                return 0;
    return 1;
}

int game_demo_acts(void)
{
    return s_demo && s_camp.on ? 5 * (3 - s_camp.first_diff) - (s_camp.first_act - 1) : 1;
}

void game_demo_stop(void)
{
    if (!s_demo)
        return;
    game_autotest_report(); /* en el log: hasta dónde llegó y qué falló */
    s_at.active = 0;
    s_st.active = 0;
    s_demo = 0;
    s_demo_all = 0;
    s_one_hit = getenv("D2_ONEHIT") != NULL;
    s_god = getenv("D2_GOD") != NULL;
}

/* El cartel de la demostración, arriba y titilando */
static void draw_demo_banner(void)
{
    const char *text = tr("APRETÁ START PARA COMENZAR");
    int w = font_width(text, 3);
    ui_dark_box(SCREEN_W / 2 - w / 2 - 16, 70, w + 32, 52);
    if ((s_anim_ticks / 25) % 3 != 2)
        font_draw_centered(SCREEN_W / 2, 78, text, 3, COL(R_GOLD, 15));
    char sub[96];
    snprintf(sub, sizeof sub, tr("Demostración: el bot juega solo (Acto %d, %s)"), s_at.act, tr(k_diff_names[s_diff]));
    font_draw_centered(SCREEN_W / 2, 104, s_at.active && s_at.act >= 1 ? sub : tr("Demostración: el bot juega solo"), 1,
                       COL(R_GRAY, 13));
}

/* Dónde está cada jugador (la zona y los subtiles), para poder decir
   exactamente dónde se ve algo mal: el J1 en la esquina de arriba a la
   izquierda y el J2 en la de la derecha, al lado del retrato de la
   mercenaria. En la demostración siempre; jugando, con "VER POSICIÓN" en
   la pausa (o D2_SHOW_POS=1). */
static void draw_positions(void)
{
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        char zone[64], pos[48];
        snprintf(zone, sizeof zone, "J%d: %s", i + 1, p->level ? str_get(p->level->name) : "?");
        snprintf(pos, sizeof pos, tr("posición %d, %d"), (int)p->x, (int)p->y);
        int zw = font_width(zone, 1), pw = font_width(pos, 1), w = zw > pw ? zw : pw, h = font_height(1);
        int merc = p->merc_name && p->merc_unit >= 0 ? 54 : 0;
        int x = i == 0 ? 10 + merc : SCREEN_W - 10 - merc - w - 12;
        ui_dark_box(x, 10, w + 12, h * 2 + 10);
        font_draw(x + 6, 13, zone, 1, COL(R_GOLD, 14));
        font_draw(x + 6, 16 + h, pos, 1, COL(R_STONE, 13));
    }
}

/* Un tick lento (más de 50 ms, una trabada que se nota): en qué se fue el
   tiempo. Lo muestra con D2_DEBUG o en la prueba automática. */
enum { PROF_ZONES, PROF_PLAYERS, PROF_UNITS, PROF_MISSILES, PROF_QUESTS, PROF_REST, PROF_COUNT };
static Uint64 s_prof_t[PROF_COUNT + 1];

static void prof_mark(int section)
{
    s_prof_t[section + 1] = SDL_GetPerformanceCounter();
}

static void prof_report(void)
{
    static const char *const names[PROF_COUNT] = { "zonas", "jugadores", "monstruos", "proyectiles", "misiones", "resto" };
    double f = 1000.0 / (double)SDL_GetPerformanceFrequency();
    double total = (double)(s_prof_t[PROF_COUNT] - s_prof_t[0]) * f;
    if (total < 50.0 || !(s_debug || s_at.active))
        return;
    printf("  tick %u de %.0f ms:", s_ticks, total);
    for (int k = 0; k < PROF_COUNT; k++)
        printf(" %s %.1f", names[k], (double)(s_prof_t[k + 1] - s_prof_t[k]) * f);
    printf("\n");
}

/* Las recetas de la guía que son de una categoría: la k-ésima (-1 si no hay) y cuántas */
static int guide_recipe(int cat, int k)
{
    for (int i = 0; i < cube_recipe_count(); i++)
        if (cube_recipe_category(i) == cat && k-- == 0)
            return i;
    return -1;
}

static int guide_count(int cat)
{
    if (cat == CUBE_CAT_RUNEWORDS)
        return items_runeword_count();
    int n = 0;
    for (int i = 0; i < cube_recipe_count(); i++)
        n += cube_recipe_category(i) == cat;
    return n;
}

#define GUIDE_ROWS 8 /* recetas que se ven a la vez */

/* Las palabras rúnicas dejan más lugar abajo (lo que dan es largo) */
static int guide_rows(int cat)
{
    return cat == CUBE_CAT_RUNEWORDS ? 6 : GUIDE_ROWS;
}

/* La k-ésima palabra rúnica de la guía: de menor a mayor nivel (como las
   listas de los jugadores), y por nombre */
static int guide_runeword(int k)
{
    static int order[256], n = -1;
    if (n < 0) {
        n = items_runeword_count() < 256 ? items_runeword_count() : 256;
        for (int i = 0; i < n; i++) {
            int j = i, lvl = items_runeword_level(i);
            while (j > 0 && (items_runeword_level(order[j - 1]) > lvl ||
                             (items_runeword_level(order[j - 1]) == lvl &&
                              strcmp(items_runeword_name(order[j - 1]), items_runeword_name(i)) > 0))) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = i;
        }
    }
    return k >= 0 && k < n ? order[k] : -1;
}

/* La Guía del cubo: izquierda y derecha cambian la categoría, arriba y
   abajo eligen la receta, B (o Start) vuelve a la pausa */
static void guide_tick(const InputState *in)
{
    int back = in->player[0].back || in->player[1].back;
    if (back || in->escape || in->start) {
        s_guide_open = 0;
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        return;
    }
    int lb = in->player[0].potion_life || in->player[1].potion_life;
    int rb = in->player[0].potion_mana || in->player[1].potion_mana;
    int dc = (in->menu_right || rb) - (in->menu_left || lb);
    if (dc) {
        s_guide_cat = (s_guide_cat + CUBE_CATS + dc) % CUBE_CATS;
        s_guide_sel = s_guide_top = 0;
        audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS);
    }
    int n = guide_count(s_guide_cat), dy = in->menu_down - in->menu_up;
    if (dy && n > 0) {
        s_guide_sel = (s_guide_sel + n + dy) % n;
        audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS);
    }
    if (s_guide_sel < s_guide_top)
        s_guide_top = s_guide_sel;
    if (s_guide_sel >= s_guide_top + guide_rows(s_guide_cat))
        s_guide_top = s_guide_sel - guide_rows(s_guide_cat) + 1;
}

/* D2_INVSHOW=modo (depuración, para las capturas): el J1 con anillos,
   amuleto, piezas de un conjunto, una palabra rúnica, talismanes, gemas,
   runas y el cubo; 0 = el cartel del conjunto, 1 = el cubo abierto con tres
   amatistas, 2 = el cartel de un talismán, 3 = el de la palabra rúnica,
   4 = la página de atributos (las resistencias) */
static void inv_show_setup(int mode)
{
    Player *p = &s_pl[0];
    if (mode == 8) { /* 8: el registro de misiones del panel */
        p->inv_open = 1;
        p->inv_page = PAGE_QUESTS;
        p->qlog_act = getenv("D2_QLOG") ? 2 : 0; /* D2_QLOG=n: el Acto 2, la misión n */
        p->qlog_sel = getenv("D2_QLOG") ? atoi(getenv("D2_QLOG")) : 0;
        p->qlog_anim = s_anim_ticks;
        return;
    }
    if (mode >= 5 && mode < 8) { /* 5..7: el árbol de habilidades en esa solapa (con lo que ya tiene) */
        p->inv_open = 1;
        p->inv_page = PAGE_SKILLS;
        p->tree_page = mode - 4;
        return;
    }
    static const char *const k_bag[] = { "box", "gcv", "gcv", "gcv", "r03", "r01", "jew", "cm1", "cm2", "hp3", "mp3", "skc" };
    memset(p->equip, 0, sizeof p->equip);
    p->bag_count = p->cube_count = 0;
    p->level_num = p->level_num > 20 ? p->level_num : 20;
    p->stat_add[ATTR_STR] = p->stat_add[ATTR_DEX] = 60;
    Item it = items_make(items_find("lgl"));
    if (items_make_set(&it, 40)) p->equip[SLOT_GLOVES] = it;
    it = items_make(items_find("lbl"));
    if (items_make_set(&it, 40)) p->equip[SLOT_BELT] = it;
    it = items_make(items_find("rin"));
    if (items_make_unique(&it, 40)) p->equip[SLOT_RING1] = it;
    it = items_make(items_find("rin"));
    items_make_magic(&it, 30);
    p->equip[SLOT_RING2] = it;
    it = items_make(items_find("amu"));
    items_make_rare(&it, 30);
    p->equip[SLOT_AMULET] = it;
    it = items_make(items_find("lsd"));
    it.sockets = 2;
    Item r3 = items_make(items_find("r03")), r1 = items_make(items_find("r01"));
    items_socket(&it, &r3);
    items_socket(&it, &r1);
    p->equip[SLOT_WEAPON] = it;
    it = items_make(items_find("cap"));
    if (items_make_unique(&it, 30)) p->equip[SLOT_HELM] = it;
    it = items_make(items_find("qui"));
    it.sockets = 2;
    p->equip[SLOT_BODY] = it;
    for (unsigned k = 0; k < sizeof k_bag / sizeof k_bag[0]; k++) {
        it = items_make(items_find(k_bag[k]));
        if (it.type->charm || !strcmp(k_bag[k], "jew"))
            items_make_magic(&it, 30);
        if (!strcmp(k_bag[k], "hp3"))
            it.amount = 2;
        if (!strcmp(k_bag[k], "mp3"))
            it.amount = 1;
        bag_add(p, &it);
    }
    p->inv_open = 1;
    p->inv_page = mode == 4 ? PAGE_STATS : PAGE_INVENTORY;
    if (mode == 0)
        p->inv_sel = SLOT_GLOVES;
    if (mode == 3)
        p->inv_sel = SLOT_WEAPON;
    if (mode == 1) {
        for (int k = 0; k < 3; k++) {
            int i = bag_index_of(p, "gcv");
            if (i >= 0 && cube_add(p, &p->bag[i]))
                bag_remove(p, i);
        }
        p->cube_open = 1;
        p->inv_sel = INV_TRANSMUTE;
    }
    if (mode == 2) {
        int i = bag_index_of(p, "cm2");
        p->inv_sel = i >= 0 ? INV_CELL0 + p->bag_y[i] * BAG_W + p->bag_x[i] : INV_CELL0;
    }
    refresh_look(p);
}

/* El resumen del estado (para la red: si dos máquinas no dan lo mismo, se separaron; y para probar
   que la partida no dependa del tiempo): el azar, los jugadores y las unidades */
uint64_t game_state_hash(void)
{
    uint64_t h = 1469598103934665603ull;
#define HMIX(v) (h = (h ^ (uint64_t)(uint32_t)(v)) * 1099511628211ull)
    HMIX(s_rng);
    HMIX(s_ticks);
    HMIX(s_act);
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        uint32_t bx, by;
        memcpy(&bx, &p->x, 4);
        memcpy(&by, &p->y, 4);
        HMIX(bx);
        HMIX(by);
        HMIX(p->hp);
        HMIX(p->mana);
        HMIX(p->exp);
        HMIX(p->state);
        HMIX(p->gold);
        HMIX(p->bag_count);
    }
    for (int u = 0; u < units_count(); u++) {
        float x, y;
        int hp, mx;
        units_position(u, 1.0f, &x, &y);
        units_health(u, &hp, &mx);
        uint32_t bx, by;
        memcpy(&bx, &x, 4);
        memcpy(&by, &y, 4);
        HMIX(bx);
        HMIX(by);
        HMIX(hp);
    }
    HMIX(items_ground_count());
#undef HMIX
    return h;
}

/* Los controles del jugador local k (0 o 1) con lo de esta máquina: con un solo jugador, cualquier
   joystick lo maneja; el mouse mueve al primero; y la opción "correr siempre" */
static void local_input(const InputState *in, int k, PlayerInput *out)
{
    PlayerInput pi = in->player[k];
    float sx = pi.move_x, sy = pi.move_y;
    if (s_num_local == 1) {
        const PlayerInput *o = &in->player[1];
        sx += o->move_x;
        sy += o->move_y;
        pi.sprint |= o->sprint;
        pi.action |= o->action;
        pi.potion_life |= o->potion_life;
        pi.potion_mana |= o->potion_mana;
        pi.potion_stamina |= o->potion_stamina;
        if (!pi.belt)
            pi.belt = o->belt;
        pi.automap |= o->automap;
        pi.inventory |= o->inventory;
        pi.skill_tree |= o->skill_tree;
        pi.stats_page |= o->stats_page;
        pi.back |= o->back;
        pi.drop |= o->drop;
        pi.nav_up |= o->nav_up;
        pi.nav_down |= o->nav_down;
        pi.nav_left |= o->nav_left;
        pi.nav_right |= o->nav_right;
        pi.chat |= o->chat;
        for (int b = 0; b < NUM_BUTTONS; b++)
            pi.button[b] |= o->button[b];
        for (int b = 0; b < 4; b++)
            pi.held[b] |= o->held[b];
    }
    if (k == 0 && in->mouse_down) {
        float dx = in->mouse_x - s_view_x[s_local[0]], dy = in->mouse_y - s_view_y[s_local[0]];
        float l = sqrtf(dx * dx + dy * dy);
        if (l > 12.0f) {
            sx = dx / l;
            sy = dy / l;
        }
    }
    pi.move_x = sx;
    pi.move_y = sy;
    pi.always_run = s_always_run;
    *out = pi;
}

/* En red: los controles de los de esta máquina para mandar (con la pausa abierta, quietos) */
int game_local_inputs(const InputState *in, PlayerInput *out)
{
    for (int k = 0; k < s_num_local; k++) {
        local_input(in, k, &out[k]);
        if (s_paused) {
            int run = out[k].always_run;
            memset(&out[k], 0, sizeof out[k]);
            out[k].always_run = run;
        }
    }
    return s_num_local;
}

void game_set_frame(const PlayerInput *all)
{
    s_frame = all;
}

/* Una partida en red, con lo que mandó el anfitrión: los jugadores de esta máquina, los personajes
   de todos (de su partida guardada, que viaja) y el mismo punto de partida en todas las máquinas
   (la semilla, el azar de cada módulo, las unidades y el reloj). local_slots: la partida guardada de
   cada jugador de esta máquina, en orden; llena slots y classes para game_start_slots. */
void game_net_begin(const NetStart *ns, const int *local_slots, int *slots, int *classes)
{
    s_net_local = 1;
    s_num_local = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        slots[i] = 0;
        classes[i] = i < ns->num_players ? ns->cls[i] : 0;
        free(s_carry[i]);
        s_carry[i] = NULL;
        s_carry_len[i] = 0;
        if (i >= ns->num_players)
            continue;
        if (ns->owner[i] == ns->me && s_num_local < MAX_LOCAL) {
            slots[i] = local_slots[s_num_local]; /* (los de acá se guardan en su lugar) */
            s_local[s_num_local++] = i;
        }
        if (ns->save_len[i] > 0) {
            s_carry[i] = malloc(ns->save_len[i] + 1);
            if (s_carry[i]) {
                memcpy(s_carry[i], ns->save[i], ns->save_len[i]);
                s_carry[i][ns->save_len[i]] = '\0';
                s_carry_len[i] = ns->save_len[i];
            }
        }
    }
    if (s_num_local == 0)
        s_local[s_num_local++] = 0;
    s_lockstep = 1;
    units_set_lockstep(1);
    s_epoch = ns->epoch;
    s_net_epoch = 1;
    s_world_seed = ns->seed;
    s_rng = ns->seed * 747796405u + 2891336453u;
    units_reset_game(ns->seed);
    items_reset_rng(ns->seed);
    itemstats_reset_rng(ns->seed);
    cube_reset_rng(ns->seed);
    s_net_world = 1;
    s_paused = 0;
}

/* Después de game_start_slots: las partidas que viajaron ya se leyeron */
void game_net_started(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        free(s_carry[i]);
        s_carry[i] = NULL;
        s_carry_len[i] = 0;
    }
}

/* Se terminó la partida en red: lo de esta máquina como siempre */
void game_net_end(void)
{
    s_net_local = s_net_epoch = 0;
    s_frame = NULL;
    s_lockstep = getenv("D2_LOCKSTEP") != NULL;
    units_set_lockstep(s_lockstep);
    game_net_started();
}

int game_local_player(int k)
{
    return k >= 0 && k < s_num_local ? s_local[k] : -1;
}

int game_num_local(void)
{
    return s_num_local;
}

/* El texto de una partida guardada (para mandarlo por la red); NULL si no hay */
char *game_slot_text(int slot, size_t *len)
{
    char path[300];
    *len = 0;
    if (!save_path(slot, path, sizeof path))
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = NULL;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n + 4096 > cap) {
            cap = cap ? cap * 2 : 8192;
            char *nb = realloc(buf, cap);
            if (!nb)
                break;
            buf = nb;
        }
        size_t k = fread(buf + n, 1, cap - n, f);
        n += k;
        if (k == 0)
            break;
    }
    fclose(f);
    *len = n;
    return buf;
}

/* La pausa, la Guía del cubo y los controles: 0 = no está abierta (sigue el juego), 1 = la usó este
   tick, 2 = guardar y salir. Sin red el juego se detiene mientras está abierta; en red no (el resto
   sigue jugando: game_local_inputs manda los controles de los de acá quietos) */
static int pause_tick(const InputState *in)
{
    if (s_paused && s_guide_open) {
        guide_tick(in);
        return 1;
    }
    if (s_paused && s_controls_open) {
        s_controls_open = controls_tick(in);
        return 1;
    }
    int back = in->player[0].back || in->player[1].back;
    if (in->escape || in->start || (s_paused && back)) {
        s_paused = !s_paused;
        s_pause_sel = 0;
        if (s_paused) { /* las misiones del jugador 1, en la que está haciendo */
            s_pause_qp = 0;
            s_pause_qact = 0;
        }
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        return 1;
    }
    if (s_paused) {
        /* Las misiones: LB y RB recorren las del registro, X muestra las del otro jugador */
        Player *qp = &s_pl[s_local[s_pause_qp < s_num_local ? s_pause_qp : 0]];
        if (s_pause_qact <= 0) {
            quest_log_pick(qp, &s_pause_qact, &s_pause_qsel);
            s_pause_qanim = s_anim_ticks;
        }
        if (s_anim_ticks - s_pause_qanim > QUEST_FIRE)
            quest_log_seen(qp);
        int lb = in->player[0].potion_life || in->player[1].potion_life;
        int rb = in->player[0].automap || in->player[1].automap;
        if (lb != rb)
            quest_log_step(qp, &s_pause_qact, &s_pause_qsel, rb - lb);
        if (in->menu_delete && s_num_local > 1) {
            s_pause_qp = !s_pause_qp;
            s_pause_qact = 0;
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
            return 1;
        }
        if (in->menu_up)
            s_pause_sel = (s_pause_sel + PAUSE_COUNT - 1) % PAUSE_COUNT;
        if (in->menu_down)
            s_pause_sel = (s_pause_sel + 1) % PAUSE_COUNT;
        int change = in->menu_right - in->menu_left;
        if (change && (s_pause_sel == PAUSE_SFX || s_pause_sel == PAUSE_MUSIC)) {
            int sfx, music;
            audio_get_volume(&sfx, &music);
            if (s_pause_sel == PAUSE_SFX)
                sfx += change;
            else
                music += change;
            audio_set_volume(sfx, music);
            audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS); /* para oír cómo quedó */
            save_options();
        }
        if (s_pause_sel == PAUSE_POS && (change || in->confirm)) {
            s_show_pos = !s_show_pos;
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
            save_options();
            return 1;
        }
        if (s_pause_sel == PAUSE_PERF && (change || in->confirm)) {
            s_show_perf = !s_show_perf;
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
            save_options();
            return 1;
        }
        if (s_pause_sel == PAUSE_COLOR16 && (change || in->confirm)) {
            s_color16 = !(s_color16 < 0 ? video_is_565() : s_color16);
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
            save_options();
            return 1;
        }
        if (s_pause_sel == PAUSE_RUN && (change || in->confirm)) {
            s_always_run = !s_always_run;
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
            save_options();
            return 1;
        }
        if (in->confirm) {
            audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
            if (s_pause_sel == PAUSE_RESUME) {
                s_paused = 0;
            } else if (s_pause_sel == PAUSE_CONTROLS) {
                s_controls_open = 1;
                controls_open();
            } else if (s_pause_sel == PAUSE_GUIDE) {
                s_guide_open = 1;
                s_guide_sel = s_guide_top = 0;
            } else if (s_pause_sel == PAUSE_QUIT) {
                if (eco_pause_quit()) { /* TERMINAR EL ECO: el cofre y el portal */
                    s_paused = 0;
                    return 1;
                }
                game_save();
                s_paused = 0;
                return 2;
            }
        }
        return 1;
    }
    return 0;
}

int game_tick(const InputState *in)
{
    if (s_eco_exit) { /* el portal del final del Eco: guardar y al menú */
        s_eco_exit = 0;
        s_paused = 0;
        game_save();
        return GAME_EXIT;
    }
    if (s_ticks == 3 && getenv("D2_INVSHOW"))
        inv_show_setup(atoi(getenv("D2_INVSHOW")));
    /* D2_A2STATE="rada,bastón,sol,santuario,invocador,tumbas" (pruebas): cómo van las misiones del Acto 2 */
    if (s_ticks == 2 && getenv("D2_A2STATE")) {
        int q[6] = { 0 };
        if (sscanf(getenv("D2_A2STATE"), "%d,%d,%d,%d,%d,%d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5]) == 6)
            for (int i = 0; i < s_num_players; i++) {
                s_pl[i].rada_q = q[0];
                s_pl[i].staff_q = q[1];
                s_pl[i].sun_q = q[2];
                s_pl[i].arcane_q = q[3];
                s_pl[i].summ_q = q[4];
                s_pl[i].tombs_q = q[5];
            }
    }
    int ui = pause_tick(in);
    if (ui == 2)
        return GAME_EXIT;
    if (ui == 1 && !s_frame)
        return GAME_CONTINUE;
    s_prof_t[0] = SDL_GetPerformanceCounter();
    if (s_ticks % 4 == 0)
        for (int i = 0; i < s_num_players; i++)
            automap_reveal(&s_pl[i]);
    debug_tour();
    talk_test();
    diff_test();
    a2_quest_test();
    a3_quest_test();
    a4_quest_test();
    cow_test();
    uber_test();
    tz_test();
    if (s_ticks == 3 && getenv("D2_WP_OPEN")) { /* depuración: los waypoints abiertos, en el acto de ahora */
        s_pl[0].wp_open = 1;
        s_pl[0].wp_sel = wp_first(s_act);
    }
    tp_test();
    stash_test();
    shop_test();
    zones_tick();
    prof_mark(PROF_ZONES);
    for (int i = 0; i < s_num_players; i++) {
        PlayerInput pi;
        if (s_frame) /* en red: los de la partida */
            pi = s_frame[i];
        else if (i < s_num_local && s_local[i] == i) /* los joysticks de esta máquina */
            local_input(in, i, &pi);
        else { /* (las pruebas con más jugadores: los bots) */
            pi = in->player[i];
            pi.always_run = s_always_run;
        }
        player_tick(&s_pl[i], pi.move_x, pi.move_y, pi.sprint, &pi);
    }

    /* Los monstruos actúan y después se aplica el daño que hicieron */
    /* Los jugadores primero (las mercenarias siguen a targets[dueño]) y
       después las mercenarias vivas, que también son blanco */
    UnitTarget targets[MAX_PLAYERS * 4 + 8 + MAX_PET_TARGETS];
    int merc_of[MAX_PLAYERS * 4 + 8 + MAX_PET_TARGETS], ally_kind[MAX_PLAYERS * 4 + 8 + MAX_PET_TARGETS];
    int ally_unit[MAX_PLAYERS * 4 + 8 + MAX_PET_TARGETS];
    int num_targets = s_num_players;
    memset(targets, 0, sizeof targets);
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        targets[i].x = p->x;
        targets[i].y = p->y;
        targets[i].alive = p->state < PS_DYING;
        targets[i].in_town = p->level && p->level->town;
        targets[i].defense = player_defense(p);
        targets[i].level = p->level_num;
        targets[i].amplified = (p->amp_ticks > 0 ? 100 : 0) + (p->curse == A4C_DECREPIFY ? 50 : 0);
        targets[i].curse = p->curse;
        targets[i].curse_len = targets[i].freeze_ticks = 0;
        targets[i].mana_drained = p->drain_ticks;
        targets[i].damage = targets[i].curse_ticks = targets[i].mana_burn = 0;
        targets[i].poison_total = targets[i].poison_ticks = 0;
        targets[i].attacker = -1;
        targets[i].block = player_block_chance(p);
        targets[i].life_pct = p->hp_max > 0 ? p->hp * 100 / p->hp_max : 100;
        targets[i].still = p->state == PS_NORMAL && !p->moving;
        targets[i].more_mana = p->mana_max >= p->hp_max;
        targets[i].player = 1;
        targets[i].shout = p->shout_ticks > 0;
        for (int k = 0; k < 4; k++) /* para el fuego y el frío de los jefes */
            targets[i].res[k] = player_resist(p, k);
    }
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        if (!units_merc_alive(p->merc_unit))
            continue;
        UnitTarget *t = &targets[num_targets];
        units_position(p->merc_unit, 1.0f, &t->x, &t->y);
        const Level *lv = world_level_at(t->x, t->y);
        t->alive = 1;
        t->in_town = lv && lv->town;
        t->defense = units_defense(p->merc_unit);
        t->level = p->merc_level;
        t->amplified = 0;
        t->damage = t->curse_ticks = t->mana_burn = t->poison_total = t->poison_ticks = 0;
        t->attacker = -1;
        t->life_pct = 100;
        ally_kind[num_targets] = 0;
        ally_unit[num_targets] = p->merc_unit;
        merc_of[num_targets++] = i;
    }
    /* El Señuelo también es blanco (para eso está) */
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        if (p->decoy_ticks <= 0)
            continue;
        UnitTarget *t = &targets[num_targets];
        t->x = p->decoy_x;
        t->y = p->decoy_y;
        t->defense = player_defense(p);
        t->alive = 1;
        t->level = p->level_num;
        t->attacker = -1;
        t->life_pct = 100;
        ally_kind[num_targets] = 2;
        ally_unit[num_targets] = -1;
        merc_of[num_targets++] = i;
    }
    /* Los esbirros (la Valquiria, los convertidos, los del Nigromante y sus
       muros de hueso): los otros monstruos los atacan */
    int pets[MAX_PET_TARGETS], npets = units_pet_targets(pets, MAX_PET_TARGETS);
    for (int k = 0; k < npets; k++) {
        UnitTarget *t = &targets[num_targets];
        units_position(pets[k], 1.0f, &t->x, &t->y);
        t->alive = 1;
        t->defense = units_defense(pets[k]);
        t->level = units_level(pets[k]);
        t->attacker = -1;
        t->life_pct = 100;
        t->body = units_radius(pets[k]) > UNIT_PLAYER_BODY ? units_radius(pets[k]) - UNIT_PLAYER_BODY : 0.0f;
        ally_kind[num_targets] = 1;
        ally_unit[num_targets] = pets[k];
        int owner = units_ally_owner(pets[k]);
        merc_of[num_targets++] = owner >= 0 && owner < s_num_players ? owner : 0;
    }
    prof_mark(PROF_PLAYERS);
    pal_auras_tick(); /* lo que dan las auras del Paladín */
    {   /* el aura de Duriel se ve (el estado holywind: el círculo de escarcha bajo sus pies) */
        int ids[4], nd = units_find_all("duriel", ids, 4);
        for (int k = 0; k < nd; k++)
            if (units_alive_enemy(ids[k])) {
                float dx, dy;
                units_position(ids[k], 1.0f, &dx, &dy);
                overlays_add("holyfreeze", OV_AT_UNIT, ids[k], dx, dy, TICK_RATE);
            }
        /* y la de los jefes con aura (Lord De Seis, Hefasto...), con el dibujo de la del Paladín */
        int kinds[8];
        nd = s_ticks % AURA_SHOW == 0 ? units_aura_bosses(ids, kinds, 4) : 0;
        for (int k = 0; k < nd; k++) {
            const AuraLook *l = aura_look(kinds[k]);
            float dx, dy;
            units_position(ids[k], 1.0f, &dx, &dy);
            if (l && l->back)
                overlays_add(l->back, OV_AT_UNIT, ids[k], dx, dy, AURA_SHOW);
            if (l && l->front)
                overlays_add(l->front, OV_AT_UNIT, ids[k], dx, dy, AURA_SHOW);
        }
    }
    units_tick(&s_map, targets, num_targets);
    for (int k = s_num_players; k < num_targets; k++) {
        Player *o = &s_pl[merc_of[k]];
        int dmg = targets[k].damage, attacker = targets[k].attacker;
        if (ally_kind[k] == 0)
            units_merc_damage(o->merc_unit, dmg);
        else if (ally_kind[k] == 1)
            units_pet_damage(ally_unit[k], dmg);
        else
            o->decoy_hp -= dmg;
        if (ally_kind[k] == 1 && dmg > 0 && units_converted(ally_unit[k]) && getenv("D2_AURA_DEBUG"))
            printf("  auras: el convertido %d (%s) recibe %d de daño\n", ally_unit[k], units_display_name(ally_unit[k]),
                   dmg);
        if (ally_unit[k] < 0 || dmg <= 0 || units_ally_owner(ally_unit[k]) < 0) /* (los bárbaros del Acto 5 no tienen dueño) */
            continue;
        ally_thorns(o, ally_unit[k], attacker, dmg); /* Espinas sobre la mercenaria o un esbirro */
        int thorns = units_pet_thorns(ally_unit[k]); /* el gólem de hierro */
        if (thorns > 0 && units_alive_enemy(attacker)) {
            int exp = units_damage(attacker, s_one_hit ? 1 << 20 : units_resist(attacker, ELEM_NONE, thorns));
            if (exp > 0)
                award_kill(o, attacker, exp);
        }
    }
    merc_tick_players();
    prof_mark(PROF_UNITS);
    tick_missiles();
    wards_tick();
    war_world_tick(); /* los sigilos y las marcas de la Purga del Brujo */
    overlays_tick();
    prof_mark(PROF_MISSILES);

    /* Se oye desde cada jugador; la música y el ambiente son los de la zona
       del primero (con la pantalla partida no se mezclan dos músicas) */
    float lx[MAX_LOCAL], ly[MAX_LOCAL];
    for (int i = 0; i < s_num_local; i++) {
        lx[i] = s_pl[s_local[i]].x;
        ly[i] = s_pl[s_local[i]].y;
    }
    audio_listeners(s_num_local, lx, ly);
    if (s_pl[s_local[0]].level)
        audio_environment(s_pl[s_local[0]].level->sound_env);
    int shot_unit, shot_target;
    while (units_pop_shot(&shot_unit, &shot_target))
        if (!ass_trap_shot(shot_unit, shot_target)) /* las trampas de la Asesina, o la Hidra */
            hydra_shot(shot_unit, shot_target);
    int kill_unit, kill_owner, kill_exp;
    while (units_pop_kill(&kill_unit, &kill_owner, &kill_exp))
        if (kill_owner >= 0 && kill_owner < s_num_players)
            award_kill(&s_pl[kill_owner], kill_unit, kill_exp);
        else if (kill_owner == UNITS_KILL_ALLY) /* lo mató un bárbaro del Acto 5: suelta lo suyo, sin experiencia */
            drop_loot(kill_unit, NULL);
    int heal_owner, heal_amount; /* el gólem de sangre le pasa vida a su dueño */
    while (units_pop_owner_heal(&heal_owner, &heal_amount))
        if (heal_owner >= 0 && heal_owner < s_num_players && s_pl[heal_owner].state < PS_DYING) {
            Player *hp = &s_pl[heal_owner];
            hp->hp = hp->hp + heal_amount > hp->hp_max ? hp->hp_max : hp->hp + heal_amount;
        }
    items_ground_tick();
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        const UnitTarget *t = &targets[i];
        if (t->blocked > 0) /* paró algún golpe con el escudo */
            player_blocked_hit(p, 0);
        take_damage(p, t->damage, 0);
        if (t->damage > 0 && t->attacker >= 0) {
            armor_struck(p, t->attacker); /* las armaduras de hielo */
            pal_thorns(p, t->attacker, t->damage); /* Espinas */
            item_thorns(p, t->attacker); /* "el atacante recibe N de daño" */
            if (p->cls == 7)
                war_struck(p, t->attacker); /* el Resguardo psíquico aturde */
        }
        /* Lo que hacen los modificadores de los jefes y el golpe de Andariel */
        if (t->curse_ticks > p->amp_ticks)
            p->amp_ticks = t->curse_ticks;
        /* El Acto 4: el toque frío de Diablo y las maldiciones de los Caballeros del Olvido (una a la vez) */
        if (t->freeze_ticks > p->frozen_ticks && p->state < PS_DYING)
            p->frozen_ticks = p->thaw_ticks > 0 ? t->freeze_ticks / 2 : t->freeze_ticks;
        if (t->curse_len > 0 && p->state < PS_DYING) {
            if (t->curse == A4C_AMPLIFY) {
                p->amp_ticks = t->curse_len > p->amp_ticks ? t->curse_len : p->amp_ticks;
            } else {
                p->curse = t->curse;
                p->curse_ticks = t->curse_len;
                p->curse_lvl = t->curse_lvl;
            }
        }
        if (p->curse == A4C_LIFETAP && t->damage > 0 && t->attacker >= 0) /* Robar vida: el que le pega se cura la mitad */
            units_heal(t->attacker, t->damage / 2);
        if (t->mana_burn > 0)
            p->mana = p->mana > t->mana_burn ? p->mana - t->mana_burn : 0;
        if (t->chill_ticks > 0 && p->state < PS_DYING) { /* la poción de deshielo lo acorta a la mitad */
            int len = p->thaw_ticks > 0 ? t->chill_ticks / 2 : t->chill_ticks;
            if (len > p->chill_ticks)
                p->chill_ticks = len;
        }
        p->aura_slow = p->state < PS_DYING ? t->aura_slow : 0;
        p->conv_def = p->state < PS_DYING ? t->aura_conv_def : 0;
        p->conv_res = p->state < PS_DYING ? t->aura_conv_res : 0;
        for (int k = 0; k < 3; k++)
            p->pierce[k] = p->state < PS_DYING ? t->aura_pierce[k] : 0;
        if (t->poison_total > 0 && p->state < PS_DYING)
            player_poison(p, (float)t->poison_total / (float)t->poison_ticks, t->poison_ticks);
    }

    if (s_shown_ticks > 0 && --s_shown_ticks == 0)
        s_shown_target = -1;

    /* Depuración (D2_KILL="ticks,nombre"): a los tantos ticks mata a ese
       monstruo (para ver la muerte de un jefe); "all", a todos los cargados */
    static int kill_at = -1;
    static char kill_name[32];
    if (kill_at == -1)
        kill_at = getenv("D2_KILL") && sscanf(getenv("D2_KILL"), "%d,%31s", &kill_at, kill_name) == 2 ? kill_at : -2;
    int victim;
    if (kill_at >= 0 && s_ticks == (uint32_t)kill_at && strcmp(kill_name, "all") == 0) {
        for (int u = 0; u < units_count(); u++)
            if (units_alive_enemy(u))
                units_damage(u, 1000000);
    } else if (kill_at >= 0 && s_ticks == (uint32_t)kill_at && units_find_all(kill_name, &victim, 1) == 1) {
        units_damage(victim, 1000000);
    }

    /* Cementerio de las Hermanas: se cumple cuando muere Blood Raven */
    int raven;
    if (!s_eco && !s_raven_dead && units_find_all("bloodraven", &raven, 1) == 1 && units_is_corpse(raven)) {
        s_raven_dead = 1;
        float rx, ry;
        units_position(raven, 1.0f, &rx, &ry);
        bossfx_start(BFX_RAVEN, rx, ry);
        s_quest_ticks = 5 * TICK_RATE;
        snprintf(s_quest_banner, sizeof s_quest_banner, tr("%s: ¡misión cumplida!"), str_get("qstsa1q2"));
        audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
        audio_music_event("music_quest_bloodraven");
        for (int k = 0; k < s_num_players; k++) {
            s_pl[k].raven_done = 1;
            player_say(&s_pl[k], "act1_complete_burial");
            save_character(&s_pl[k]);
        }
    }

    if (s_eco) {
        eco_tick();
    } else {
        quest_tick();
        cow_tick();
        uber_tick();
        dclone_tick();
        tz_tick();
    }
    level_palette();
    obj_tick();
    bossfx_tick();
    if (s_den_light > 0 && s_den_light < 1000)
        s_den_light++;
    prof_mark(PROF_QUESTS);

    /* ¿Quedan monstruos en la Den of Evil? (solo se mira con alguien adentro) */
    if (s_quest_ticks > 0)
        s_quest_ticks--;
    for (int i = 0; i < s_num_players && !s_den_cleared; i++) {
        const Level *lv = s_pl[i].level;
        if (lv && strcmp(lv->name, "Den of Evil") == 0 &&
            units_count_enemies_in(lv->x0 * (float)TILE_SUB, lv->y0 * (float)TILE_SUB, lv->x1 * (float)TILE_SUB,
                                   lv->y1 * (float)TILE_SUB) == 0) {
            s_den_cleared = 1;
            s_den_light = 1; /* la cueva se ilumina */
            s_quest_ticks = 5 * TICK_RATE;
            snprintf(s_quest_banner, sizeof s_quest_banner, tr("¡%s purificada!"), str_get("Den of Evil"));
            audio_play_name("cursor_questdone", AUDIO_NOPOS, AUDIO_NOPOS);
            audio_music_event("music_quest_den");
            for (int k = 0; k < s_num_players; k++) {
                s_pl[k].den_done = 1;
                player_say(&s_pl[k], "act1_complete_den");
                save_character(&s_pl[k]);
            }
        }
    }
    static int dump_at = -2; /* D2_HASH_DUMP=T: en ese tick, cada unidad y cada jugador (para encontrar dónde se separan dos corridas) */
    if (dump_at == -2)
        dump_at = getenv("D2_HASH_DUMP") ? atoi(getenv("D2_HASH_DUMP")) : -1;
    if (dump_at >= 0 && s_ticks == (uint32_t)dump_at) {
        for (int i = 0; i < s_num_players; i++)
            printf("DUMP J%d %a %a vida %d maná %d estado %d modo %d anim %d exp %u oro %d mochila %d\n", i + 1, s_pl[i].x, s_pl[i].y,
                   s_pl[i].hp, s_pl[i].mana, s_pl[i].state, s_pl[i].mode, s_pl[i].anim_acc, (unsigned)s_pl[i].exp, s_pl[i].gold,
                   s_pl[i].bag_count);
        printf("DUMP suelo %d\n", items_ground_count());
        for (int u = 0; u < units_count(); u++) {
            float x, y;
            int hp, mx;
            units_position(u, 1.0f, &x, &y);
            units_health(u, &hp, &mx);
            printf("DUMP u%d %s %a %a vida %d/%d\n", u, units_id(u), x, y, hp, mx);
        }
    }
    if (s_hash_every > 0 && s_ticks % (uint32_t)s_hash_every == 0) /* D2_HASH=N: el resumen del estado cada N ticks */
        printf("HASH t%u %016llx (%d unidades, rng %08x)\n", s_ticks, (unsigned long long)game_state_hash(), units_count(), s_rng);
    s_ticks++;
    if (s_ticks % (AUTOSAVE_SECONDS * TICK_RATE) == 0)
        game_save();
    prof_mark(PROF_REST);
    prof_report();
    return GAME_CONTINUE;
}

/* Dónde está lo que sigue un efecto (overlays.c): el monstruo o el jugador */
static int overlay_pos(int at, int id, float *x, float *y)
{
    if (at == OV_AT_UNIT) {
        if (id < 0 || id >= units_count() || !units_visible(id))
            return 0;
        units_position(id, 1.0f, x, y);
        return 1;
    }
    if (at == OV_AT_PLAYER) {
        if (id < 0 || id >= s_num_players || s_pl[id].state >= PS_DEAD)
            return 0;
        *x = s_pl[id].x;
        *y = s_pl[id].y;
        return 1;
    }
    return 0;
}

/* El "!" arriba de los NPC que tienen algo nuevo de las misiones */
static void draw_npc_news(float cam_x, float cam_y)
{
    int bob = (int)(s_anim_ticks / 8) % 4;
    bob = bob == 3 ? 1 : bob; /* sube y baja de a un píxel */
    /* Solo los NPC del pueblo: si nadie está ahí, no hay nada que buscar */
    int in_town = 0;
    for (int i = 0; i < s_num_players; i++)
        in_town |= s_pl[i].level && s_pl[i].level->town;
    if (!in_town)
        return;
    for (int u = 0; u < units_count(); u++) {
        if (!units_visible(u))
            continue;
        /* Primero lo barato: si cae en pantalla; después, los nombres */
        float ux, uy, sx, sy;
        units_position(u, 1.0f, &ux, &uy);
        iso_to_screen(ux, uy, &sx, &sy);
        int x = SCREEN_W / 2 + (int)lroundf(sx - cam_x), y = SCREEN_H / 2 + (int)lroundf(sy - cam_y) - 160 - bob;
        if (x < -10 || x > SCREEN_W + 10 || y < -40 || y > SCREEN_H)
            continue;
        if (npc_vo_index(units_id(u)) < 0)
            continue;
        int news = 0;
        for (int i = 0; i < s_num_players && !news; i++)
            news = npc_has_news(&s_pl[i], units_id(u));
        if (!news)
            continue;
        const Level *lv = world_level_at(ux, uy);
        if (!lv || !lv->town)
            continue;
        /* Un "!" grueso: borde negro, dorado y un brillo a la izquierda */
        fb_fill_rect(x - 4, y - 1, 9, 22, COL(R_GRAY, 0));
        fb_fill_rect(x - 4, y + 23, 9, 9, COL(R_GRAY, 0));
        fb_fill_rect(x - 3, y, 7, 20, COL(R_GOLD, 13));
        fb_fill_rect(x - 3, y + 24, 7, 7, COL(R_GOLD, 13));
        fb_fill_rect(x - 3, y, 2, 20, COL(R_GOLD, 15));
        fb_fill_rect(x - 3, y + 24, 2, 7, COL(R_GOLD, 15));
    }
}

/* ---- Automapa ----
   Como el del original: las paredes de lo que ya se recorrió, encima del
   juego (R3 o Tab). Por baldosa (5 x 5 subtiles) se guarda qué subtiles son
   borde de pared la primera vez que alguien pasa cerca, así lo recorrido
   sigue en el mapa aunque la zona se borre de memoria. */
#define AUTOMAP_REVEAL 6           /* baldosas que se descubren alrededor de cada jugador */
#define AUTOMAP_RANGE  22          /* baldosas que se dibujan alrededor del centro */
#define AUTOMAP_SEEN   (1u << 31)  /* la baldosa ya se vio (los otros bits: los bordes) */
static uint32_t *s_automap;
static int       s_automap_w, s_automap_h;

static void automap_reset(void)
{
    free(s_automap);
    s_automap_w = s_map.width;
    s_automap_h = s_map.height;
    s_automap = calloc((size_t)s_automap_w * (size_t)s_automap_h, sizeof *s_automap);
}

static void automap_reveal(const Player *p)
{
    if (!s_automap)
        return;
    int ptx = (int)(p->x / TILE_SUB), pty = (int)(p->y / TILE_SUB);
    for (int ty = pty - AUTOMAP_REVEAL; ty <= pty + AUTOMAP_REVEAL; ty++)
        for (int tx = ptx - AUTOMAP_REVEAL; tx <= ptx + AUTOMAP_REVEAL; tx++) {
            if (tx < 0 || ty < 0 || tx >= s_automap_w || ty >= s_automap_h)
                continue;
            uint32_t *cell = &s_automap[ty * s_automap_w + tx];
            if (*cell & AUTOMAP_SEEN || !map_chunk(&s_map, tx, ty))
                continue; /* ya vista, o su zona no está armada */
            uint32_t mask = AUTOMAP_SEEN;
            for (int k = 0; k < TILE_SUB * TILE_SUB; k++) {
                int sx = tx * TILE_SUB + k % TILE_SUB, sy = ty * TILE_SUB + k / TILE_SUB;
                if (map_blocked(&s_map, sx, sy) &&
                    (!map_blocked(&s_map, sx - 1, sy) || !map_blocked(&s_map, sx + 1, sy) ||
                     !map_blocked(&s_map, sx, sy - 1) || !map_blocked(&s_map, sx, sy + 1)))
                    mask |= 1u << k;
            }
            *cell = mask;
        }
}

/* Un punto del automapa: de subtiles a la pantalla (un octavo de la vista) */
static void automap_point(float wx, float wy, float cx, float cy, int ox, int oy, int *x, int *y)
{
    *x = ox + (int)lroundf(((wx - cx) - (wy - cy)) * 2.0f);
    *y = oy + (int)lroundf((wx - cx) + (wy - cy));
}

static void automap_mark(float wx, float wy, float cx, float cy, int ox, int oy, int size, uint8_t color)
{
    int x, y;
    automap_point(wx, wy, cx, cy, ox, oy, &x, &y);
    fb_fill_rect(x - size / 2, y - size / 2, size, size, color);
}

/* El automapa centrado en (cx, cy) del mundo, con ese punto en (ox, oy) de la pantalla */
static void draw_automap(float cx, float cy, int ox, int oy)
{
    if (!s_automap)
        return;
    fb_shade_rect(0, 0, SCREEN_W, SCREEN_H, 18); /* el juego, más oscuro detrás */
    uint8_t wall = COL(R_STONE, 15);
    int ctx = (int)(cx / TILE_SUB), cty = (int)(cy / TILE_SUB);
    for (int ty = cty - AUTOMAP_RANGE; ty <= cty + AUTOMAP_RANGE; ty++)
        for (int tx = ctx - AUTOMAP_RANGE; tx <= ctx + AUTOMAP_RANGE; tx++) {
            if (tx < 0 || ty < 0 || tx >= s_automap_w || ty >= s_automap_h)
                continue;
            uint32_t mask = s_automap[ty * s_automap_w + tx] & ~AUTOMAP_SEEN;
            for (int k = 0; mask; k++, mask >>= 1)
                if (mask & 1) {
                    int x, y;
                    automap_point(tx * TILE_SUB + k % TILE_SUB + 0.5f, ty * TILE_SUB + k / TILE_SUB + 0.5f, cx, cy, ox, oy,
                                  &x, &y);
                    fb_fill_rect(x, y, 2, 1, wall);
                }
        }
    /* Las salidas (escaleras, bocas de cueva, portales) en rojo y los waypoints en azul */
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        float wx = (w->x0 + w->x1) / 2, wy = (w->y0 + w->y1) / 2;
        int tx = (int)(wx / TILE_SUB), ty = (int)(wy / TILE_SUB);
        if (tx >= 0 && ty >= 0 && tx < s_automap_w && ty < s_automap_h && (s_automap[ty * s_automap_w + tx] & AUTOMAP_SEEN))
            automap_mark(wx, wy, cx, cy, ox, oy, 4, COL(R_RED, 15));
    }
    for (int k = 0; k < WP_MAX; k++)
        if (s_wp_unit[k] >= 0) {
            float wx, wy;
            units_position(s_wp_unit[k], 1.0f, &wx, &wy);
            int tx = (int)(wx / TILE_SUB), ty = (int)(wy / TILE_SUB);
            if (tx >= 0 && ty >= 0 && tx < s_automap_w && ty < s_automap_h && (s_automap[ty * s_automap_w + tx] & AUTOMAP_SEEN))
                automap_mark(wx, wy, cx, cy, ox, oy, 4, COL(R_BLUE, 15));
        }
    /* Las mercenarias en verde y los jugadores con su color */
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        if (units_merc_alive(p->merc_unit)) {
            float mx, my;
            units_position(p->merc_unit, 1.0f, &mx, &my);
            automap_mark(mx, my, cx, cy, ox, oy, 3, COL(R_GRASS, 15));
        }
        automap_mark(p->x, p->y, cx, cy, ox, oy, 4, COL(p->label, 15));
    }
}

/* El automapa de la vista: el de un jugador que lo tenga abierto */
static void draw_view_automap(float cam_x, float cam_y, int who)
{
    int open = who >= 0 ? s_pl[who].map_open : 0;
    for (int i = 0; who < 0 && i < s_num_local; i++)
        open |= s_pl[s_local[i]].map_open;
    if (!open)
        return;
    /* El centro de la vista en el mundo (al revés de iso_to_screen) */
    float wx = (cam_x / 16.0f + cam_y / 8.0f) * 0.5f, wy = (cam_y / 8.0f - cam_x / 16.0f) * 0.5f;
    draw_automap(wx, wy, SCREEN_W / 2, SCREEN_H / 2);
}

/* ---- Dibujo ---- */

/* El pentagrama que marca lo elegido: el de frente (cuadro 0 de pentspin),
   plano, girando sobre sí mismo como en el original; dir = sentido */
static void draw_pentagram(int x, int y, int size, int dir)
{
    float a = (float)SDL_GetTicks() * 0.0015f * (dir < 0 ? -1.0f : 1.0f); /* una vuelta cada 4 segundos */
    ui_frame_rotated("cursor/pentspin", 0, x + size / 2, y + size / 2, size, a);
}

static int cmp_drawable(const void *a, const void *b)
{
    float d = ((const Drawable *)a)->depth - ((const Drawable *)b)->depth;
    return (d > 0.0f) - (d < 0.0f);
}

static void player_world_pos(const Player *p, float alpha, float *x, float *y)
{
    *x = p->prev_x + (p->x - p->prev_x) * alpha;
    *y = p->prev_y + (p->y - p->prev_y) * alpha;
}

static void draw_player(int x, int y, int index)
{
    const Player *p = &s_pl[index];
    const CharAnim *a = player_anim_draw(p, p->mode);
    int frame = p->anim_acc >> 8, frames = charanim_frames(a), smode;

    /* Ataque, golpe y muerte se juegan una vez y quedan en su último cuadro */
    if (p->state == PS_DEAD)
        frame = p->mode == MODE_DD ? 0 : frames - 1;
    else if (p->state != PS_NORMAL && frame >= frames)
        frame = frames - 1;
    /* Las secuencias: el cuadro de cada paso (de A1, A2, S1, S3...) */
    if (seq_frame(p, &smode, &frame)) {
        a = player_anim_draw(p, smode);
        frames = charanim_frames(a);
    }
    if (frame >= frames) /* (el dibujo del equipo de antes puede tener menos cuadros que el que calcula la lógica) */
        frame = p->state == PS_NORMAL && frames > 0 ? frame % frames : frames - 1;
    /* El Salto: en el aire se lo ve más arriba */
    if (p->jumping) {
        float t = ((float)p->anim_acc / 256.0f - (float)p->jump_f0) / (float)(p->jump_f1 - p->jump_f0 > 0 ? p->jump_f1 - p->jump_f0 : 1);
        y -= (int)(48.0f * sinf((t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t) * PI_F));
    }

    if (p->state < PS_DYING)
        fb_shade_ellipse(x, y, 18, 8, 14);
    /* Envenenado: verde (el colorshift 104 del estado poison de states.txt) */
    charanim_draw_tint(a, p->sector, frame, x, y,
                       p->state >= PS_DYING             ? NULL
                       : cold_slow(p) > 0               ? fb_hue(108) /* el frío le gana al veneno, como en los monstruos */
                       : p->poison_ticks > 0            ? fb_hue(104)
                                                        : NULL);
    /* Hojas reflejadas: copias del arma (translúcidas) que pegan a los costados, un poco después */
    if (p->cls == 7 && p->state == PS_ATTACK && p->attack_skill >= 0 &&
        skills_get(p->cls, p->attack_skill)->kind == SK_MIRRORED_BLADES && p->hits_total > 1) {
        float face = (float)p->sector * PI_F / 8.0f + PI_F / 2.0f;
        int span = frames - a->hit_frame > 1 ? frames - a->hit_frame : 2;
        for (int k = 1; k < p->hits_total; k++) {
            int f = frame - span * k / p->hits_total;
            if (f < 0)
                continue;
            float side = (k & 1) ? 1.0f : -1.0f, d = 26.0f + 10.0f * (float)((k - 1) / 2);
            int gx = x + (int)(cosf(face + side * PI_F / 2.0f) * d - cosf(face) * 6.0f);
            int gy = y + (int)((sinf(face + side * PI_F / 2.0f) * d - sinf(face) * 6.0f) * 0.5f);
            for (int pass = 0; pass < 2; pass++) /* (dos veces: brillan como espectros) */
                charanim_draw_ghost(a, p->sector, f, gx, gy, (1u << COMP_RH) | (1u << COMP_LH), BLEND_ADD);
        }
    }

    if (s_num_players > 1) {
        char tag[8];
        snprintf(tag, sizeof tag, "J%d", index + 1);
        font_draw_centered(x, y - 104, tag, 2, COL(p->label, 14));
    }
}

/* Los tiles especiales (orientación 10 y 11) que no se dibujan: las marcas
   invisibles (warp.dt1, el inicio del pueblo), que no bloquean nada. Las
   escaleras de las Catacumbas, la Catedral y los sótanos de la torre también
   son especiales, pero tienen dibujo y son sólidas: esas sí se ven. */
static int is_special(const Dt1Tile *t)
{
    if (t->orientation != ORI_SPECIAL_1 && t->orientation != ORI_SPECIAL_2)
        return 0;
    for (int k = 0; k < TILE_SUB * TILE_SUB; k++)
        if (t->subtile_flags[k] & 1)
            return 0;
    return 1;
}

/* Profundidad de una pared para el orden de dibujo: el centro del tile */
static float wall_depth(int tx, int ty)
{
    return (float)((tx + ty) * TILE_SUB + TILE_SUB);
}

/* Las paredes "hacia abajo" (orientación 16 a 19: los arcos y bordes que
   cuelgan debajo de las plataformas del Santuario Arcano) están por debajo
   del piso: van en la capa del piso, antes que los pisos, y no cuentan para
   ordenar a los personajes. Antes se ordenaban como paredes y los arcos de
   una plataforma tapaban las escaleras que bajan por delante (y las
   oscurecían, como si fueran sombras). */
static int is_lower_wall(const Dt1Tile *t)
{
    return t->orientation >= ORI_LOWER_LEFT && t->orientation <= ORI_LOWER_SOUTH;
}
/* Cuánto más arriba del recorte se buscan, porque cuelgan de su tile.
   shortcut: alcanza para el Santuario (las más largas bajan 304 px); las del
   Acto 5 (siege, baallair) bajan hasta 976: subirlo cuando lleguen. */
#define LOWER_WALL_REACH 200

/* Las paredes ocupan los bordes de atrás del tile: las "izquierdas" la
   primera columna de subtiles (x = X0) y las "derechas" la primera fila
   (y = Y0). Con un solo número por pared no alcanza para ordenarlas contra
   los personajes, así que cada personaje revisa las paredes cercanas y se
   acomoda: después de las que tiene detrás y antes de las que tiene delante. */
/* ¿Algo que no se camina entre (sx, sy) y `steps` subtiles hacia (dx, dy)?
   Si no hay nada, el personaje está al pie de la pared y no detrás: pasa en
   el borde del puente, cuya baranda se dibuja como pared pero no bloquea (lo
   que bloquea es la fila de atrás), y lo tapaba entero. */
static int blocked_toward(const Map *m, int sx, int sy, int dx, int dy, int steps)
{
    for (int i = 1; i <= steps; i++)
        if (map_blocked(m, sx + dx * i, sy + dy * i))
            return 1;
    return 0;
}

static float unit_depth(float ux, float uy)
{
    const Map *m = &s_map;
    int usx = (int)floorf(ux), usy = (int)floorf(uy);
    float key = ux + uy, lo = -1e9f, hi = 1e9f, slo = -1e9f, shi = 1e9f;
    int ctx = (int)floorf(ux / TILE_SUB), cty = (int)floorf(uy / TILE_SUB);

    for (int ty = cty - 1; ty <= cty + 1; ty++) {
        for (int tx = ctx - 1; tx <= ctx + 1; tx++) {
            if (tx < 0 || ty < 0 || tx >= m->width || ty >= m->height)
                continue;
            float x0 = (float)(tx * TILE_SUB), y0 = (float)(ty * TILE_SUB);
            int along_y = uy > y0 - 0.5f && uy < y0 + TILE_SUB + 0.5f;
            int along_x = ux > x0 - 0.5f && ux < x0 + TILE_SUB + 0.5f;
            /* Las paredes rectas: lo que importa es si se superponen en
               pantalla (x - y, con el ancho del dibujo del personaje). Una
               pared recta separa adelante y atrás en toda su línea; si no se
               mira así, al pasar por una puerta el tramo de al lado (ordenado
               por el centro de su tile) lo tapaba como si atravesara la pared. */
            float sx = ux - uy;
            int over_right = sx > x0 - y0 - 2.0f && sx < x0 + TILE_SUB - y0 + 2.0f; /* y = y0, x de x0 a x0 + 5 */
            int over_left = sx > x0 - y0 - TILE_SUB - 2.0f && sx < x0 - y0 + 2.0f;  /* x = x0, y de y0 a y0 + 5 */
            for (int l = 0; l < MAP_WALLS; l++) {
                const Dt1Tile *t = map_wall(m, l, tx, ty);
                if (!t || is_lower_wall(t))
                    continue; /* (las de abajo van antes que todo) */
                int rel = 0; /* 1 = el personaje está delante, -1 = detrás */
                /* Las piezas sólidas (paredes, columnas, esquinas, rejas, la
                   carpa de Akara): por lo que ocupan de verdad. Detrás solo si
                   alguna parte sólida que se superpone en pantalla con el
                   personaje está delante de él; si ninguna se superpone, no
                   importa. Antes se usaban la línea de cada pared y el centro
                   del tile, y al pasar junto a una columna o el final de una
                   pared se dibujaba del lado equivocado (parecía atravesarla). */
                int solid = 0, any = 0, front = 0, strict_front = 0, strict_behind = 0;
                for (int by = 0; by < TILE_SUB; by++)
                    for (int bx = 0; bx < TILE_SUB; bx++) {
                        if (!(t->subtile_flags[(TILE_SUB - 1 - by) * TILE_SUB + bx] & 1))
                            continue;
                        solid = 1;
                        /* En su misma columna de pantalla y delante (o detrás)
                           en los dos ejes: eso no admite duda */
                        int ci = (int)x0 + bx, cj = (int)y0 + by;
                        if (abs((ci - cj) - (usx - usy)) <= 1) {
                            strict_front |= ci >= usx && cj >= usy && ci + cj > usx + usy;
                            strict_behind |= ci <= usx && cj <= usy && ci + cj < usx + usy;
                        }
                        float cx = x0 + bx + 0.5f, cy = y0 + by + 0.5f;
                        if (fabsf((cx - cy) - sx) > 2.0f)
                            continue;
                        any = 1;
                        /* Delante solo si no queda detrás en ningún eje: una
                           columna pegada a la pared, al costado y apenas más
                           al este, no tapa al que está del lado de adelante */
                        front |= cx > ux - 0.5f && cy > uy - 0.5f && cx + cy > ux + uy;
                    }
                if (solid)
                    rel = !any ? 0 : front ? -1 : 1;
                /* En el hueco de una puerta, el arco y los postes lo tapan hasta
                   que la cruza (como al pasar por debajo de un arco) */
                if (solid && t->orientation == ORI_RIGHT_DOOR && along_x)
                    rel = uy >= y0 + 0.5f ? 1 : -1;
                else if (solid && t->orientation == ORI_LEFT_DOOR && along_y)
                    rel = ux >= x0 + 0.5f ? 1 : -1;
                /* Las que no bloquean (la baranda del puente): por su línea */
                switch (solid ? -1 : t->orientation) {
                case ORI_LEFT_WALL:
                case ORI_LEFT_END:
                    /* Detrás solo si algo lo separa de la pared (o si está lejos) */
                    if (over_left)
                        rel = ux >= x0 + 0.5f ||
                                      (ux >= x0 - 1.5f && !blocked_toward(m, usx, usy, 1, 0, (int)x0 + TILE_SUB - 1 - usx))
                                  ? 1
                                  : -1;
                    break;
                case ORI_LEFT_DOOR: /* las puertas tapan al que pasa por debajo */
                    if (over_left)
                        rel = ux >= x0 + 0.5f ? 1 : -1;
                    break;
                case ORI_RIGHT_WALL:
                case ORI_RIGHT_END:
                    if (over_right)
                        rel = uy >= y0 + 0.5f ||
                                      (uy >= y0 - 1.5f && !blocked_toward(m, usx, usy, 0, 1, (int)y0 + TILE_SUB - 1 - usy))
                                  ? 1
                                  : -1;
                    break;
                case ORI_RIGHT_DOOR:
                    if (over_right)
                        rel = uy >= y0 + 0.5f ? 1 : -1;
                    break;
                case ORI_RIGHT_NORTH_CORNER:
                case ORI_LEFT_NORTH_CORNER:
                    if (along_x && along_y)
                        rel = (ux >= x0 + 0.5f && uy >= y0 + 0.5f) ? 1 : -1;
                    break;
                default:
                    break;
                }
                /* Lo que no admite duda manda sobre las demás reglas (la de
                   las puertas, las líneas, lo que se superpone de costado):
                   antes, si dos piezas pedían cosas opuestas ganaba estar
                   delante, y quedaba dibujado encima de una columna o de un
                   poste que tenía delante en su misma columna de pantalla */
                int strong = 0;
                if (strict_front != strict_behind) {
                    rel = strict_front ? -1 : 1;
                    strong = 1;
                }
                float kw = wall_depth(tx, ty);
                float *plo = strong ? &slo : &lo, *phi = strong ? &shi : &hi;
                if (rel > 0 && kw + 0.01f > *plo)
                    *plo = kw + 0.01f;
                else if (rel < 0 && kw - 0.01f < *phi)
                    *phi = kw - 0.01f;
            }
        }
    }
    if (key > hi)
        key = hi;
    if (key < lo)
        key = lo; /* si hay conflicto, gana estar delante */
    if (key > shi)
        key = shi;
    if (key < slo)
        key = slo; /* los seguros mandan (y entre ellos, estar delante) */
    return key;
}

/* Depuración (D2_DEPTH_AUDIT=1): en todos los niveles, cada subtile donde
   puede pararse el jugador (con su cruz libre) contra las paredes de
   alrededor. Cuenta los errores que no admiten duda: dibujado encima de una
   pieza que en su misma columna de pantalla está delante en los dos ejes, o
   tapado por una que está detrás en los dos. */
static void depth_audit(void)
{
    const Map *m = &s_map;
    long checked = 0;
    int over = 0, under = 0;
    for (int sy = 0; sy < m->height * TILE_SUB; sy++)
        for (int sx = 0; sx < m->width * TILE_SUB; sx++) {
            if (!map_chunk(m, sx / TILE_SUB, sy / TILE_SUB) || foot_blocked(sx, sy))
                continue;
            checked++;
            float ux = sx + 0.5f, uy = sy + 0.5f, key = -1e9f;
            int ctx = sx / TILE_SUB, cty = sy / TILE_SUB;
            for (int ty = cty - 1; ty <= cty + 2; ty++)
                for (int tx = ctx - 1; tx <= ctx + 2; tx++)
                    for (int l = 0; l < MAP_WALLS; l++) {
                        const Dt1Tile *t = map_wall(m, l, tx, ty);
                        if (!t || t->orientation == ORI_ROOF || is_special(t) || !(t->pixels || t->pending))
                            continue; /* (la que tiene imagen, aunque todavía no se haya decodificado) */
                        if (is_lower_wall(t))
                            continue; /* van debajo de todo */
                        int front = 0, behind = 0;
                        for (int by = 0; by < TILE_SUB; by++)
                            for (int bx = 0; bx < TILE_SUB; bx++) {
                                if (!(t->subtile_flags[(TILE_SUB - 1 - by) * TILE_SUB + bx] & 1))
                                    continue;
                                int cx = tx * TILE_SUB + bx, cy = ty * TILE_SUB + by;
                                if (abs((cx - cy) - (sx - sy)) > 1)
                                    continue;
                                front |= cx >= sx && cy >= sy && cx + cy > sx + sy;
                                behind |= cx <= sx && cy <= sy && cx + cy < sx + sy;
                            }
                        if (front == behind)
                            continue; /* nada en su columna, o lo rodea */
                        if (key < -1e8f)
                            key = unit_depth(ux, uy);
                        float kw = wall_depth(tx, ty);
                        int bad = front ? key > kw : key < kw;
                        if (!bad)
                            continue;
                        int n = front ? ++over : ++under;
                        if (n <= 25) {
                            const Level *lv = world_level_at(ux, uy);
                            printf("orden: %s en %d,%d %s la pieza %d,%d capa %d (orientación %d)\n",
                                   lv ? lv->name : "?", sx, sy, front ? "encima de" : "tapado por", tx, ty, l,
                                   t->orientation);
                        }
                    }
        }
    printf("orden: %ld lugares revisados, %d encima de algo de adelante, %d tapado por algo de atrás\n", checked,
           over, under);
}

static void draw_wall_tile(const Dt1Tile *t, int sx, int sy)
{
    if (t && !is_special(t))
        t = dt1_tile_ready(t); /* la imagen se decodifica la primera vez que se dibuja */
    if (t && t->pixels && !is_special(t))
        fb_blit_spans(t->pixels, t->spans, t->img_w, t->img_h, sx - TILE_W / 2, sy + TILE_H + t->img_y);
}

/* Filas de tiles que pueden verse: la pantalla en coordenadas de tiles es
   un rombo, así que primero se acotan las filas y después las columnas. */
/* Lo que se ve: el rectángulo del recorte (con la pantalla partida, el de
   cada mitad), así cada mitad recorre solo sus tiles */
static int s_vis_x0, s_vis_y0, s_vis_x1 = SCREEN_W - 1, s_vis_y1 = SCREEN_H - 1;

/* Filas del mapa con algún tile que puede caer en ese rectángulo: el tile
   (tx, ty) está en y = (tx + ty) * TILE_H / 2 + oy, con tx entre 0 y el ancho */
static void visible_rows(const Map *m, int ox, int oy, int *ty0, int *ty1)
{
    /* Las filas que tienen alguna columna visible: visible_cols pide
       tx - ty en [a0, a1] (horizontal) y tx + ty en [b0, b1] (vertical, con
       sus márgenes), así que ty va de (b0 - a1) / 2 a (b1 - a0) / 2. Antes
       se restaba el ancho entero del mapa y en el del Acto 2 se recorrían
       unas 1650 filas por vista en vez de 60 */
    const float hw = TILE_W / 2, hh = TILE_H / 2;
    float a0 = (s_vis_x0 - TILE_W - ox) / hw, a1 = (s_vis_x1 + TILE_W - ox) / hw;
    float b0 = (s_vis_y0 - TILE_H - 40 - oy) / hh, b1 = (s_vis_y1 + 340 - oy) / hh;
    int lo = (int)floorf((b0 - a1) / 2.0f) - 1, hi = (int)ceilf((b1 - a0) / 2.0f) + 1;
    *ty0 = lo < 0 ? 0 : lo;
    *ty1 = hi >= m->height ? m->height - 1 : hi;
}

/* Columnas visibles de la fila ty: el tile tiene que caer en el rectángulo
   visible horizontalmente (con margen de un tile) y verticalmente (con margen
   arriba para las paredes altas). */
static void visible_cols(const Map *m, int ox, int oy, int ty, int *tx0, int *tx1)
{
    const float hw = TILE_W / 2, hh = TILE_H / 2;
    float a0 = ty + (s_vis_x0 - TILE_W - ox) / hw, a1 = ty + (s_vis_x1 + TILE_W - ox) / hw;
    float b0 = (s_vis_y0 - TILE_H - 40 - oy) / hh - ty, b1 = (s_vis_y1 + 340 - oy) / hh - ty;
    int lo = (int)floorf(a0 > b0 ? a0 : b0), hi = (int)ceilf(a1 < b1 ? a1 : b1);
    *tx0 = lo < 0 ? 0 : lo;
    *tx1 = hi >= m->width ? m->width - 1 : hi;
}

/* El piso (y las sombras) de cada vista se guarda entre cuadros: al moverse
   la cámara solo se dibuja la franja que entra. D2_FLOOR_CACHE=0 lo dibuja
   entero en cada cuadro, como antes. */
static FbLayer  s_floor_layer[2];
static unsigned s_floor_version[2];
static int      s_world_view; /* qué vista del cuadro se está dibujando */

typedef struct {
    const Map *m;
    int        ox, oy;
} FloorDraw;

/* Los pisos y las sombras de los tiles que caen en el recorte */
static void draw_floors(void *ctx)
{
    const FloorDraw *f = ctx;
    const Map *m = f->m;
    if (!clip_bounds(&s_vis_x0, &s_vis_y0, &s_vis_x1, &s_vis_y1))
        return;
    /* Primero lo que cuelga debajo del piso, así los pisos de adelante lo
       tapan; cuelga de su tile, así que se buscan filas más arriba */
    int ty0, ty1, vis_y0 = s_vis_y0;
    s_vis_y0 -= LOWER_WALL_REACH;
    visible_rows(m, f->ox, f->oy, &ty0, &ty1);
    for (int ty = ty0; ty <= ty1; ty++) {
        int tx0, tx1;
        visible_cols(m, f->ox, f->oy, ty, &tx0, &tx1);
        for (int tx = tx0; tx <= tx1; tx++) {
            const MapChunk *ch = map_chunk(m, tx, ty);
            if (!ch)
                continue;
            int c = map_cell(tx, ty);
            for (int l = 0; l < MAP_WALLS; l++)
                if (ch->wall[l][c] && is_lower_wall(ch->wall[l][c]))
                    draw_wall_tile(ch->wall[l][c], (tx - ty) * (TILE_W / 2) + f->ox, (tx + ty) * (TILE_H / 2) + f->oy);
        }
    }
    s_vis_y0 = vis_y0;
    visible_rows(m, f->ox, f->oy, &ty0, &ty1);
    for (int ty = ty0; ty <= ty1; ty++) {
        int tx0, tx1;
        visible_cols(m, f->ox, f->oy, ty, &tx0, &tx1);
        for (int tx = tx0; tx <= tx1; tx++) {
            const MapChunk *ch = map_chunk(m, tx, ty);
            if (!ch)
                continue; /* vacío o zona sin armar */
            int sx = (tx - ty) * (TILE_W / 2) + f->ox, sy = (tx + ty) * (TILE_H / 2) + f->oy;
            int c = map_cell(tx, ty);
            for (int l = 0; l < MAP_FLOORS; l++) {
                const Dt1Tile *t = dt1_tile_ready(ch->floor[l][c]);
                if (t && t->pixels)
                    fb_blit_spans(t->pixels, t->spans, t->img_w, t->img_h, sx - TILE_W / 2, sy + t->img_y);
            }
            const Dt1Tile *sh = dt1_tile_ready(ch->shadow[c]);
            if (sh && sh->pixels)
                fb_blit_shade_spans(sh->pixels, sh->spans, sh->img_w, sh->img_h, sx - TILE_W / 2,
                                    sy + TILE_H + sh->img_y, 14);
        }
    }
}

/* Dibuja el mundo visto desde una cámara (en coordenadas de pantalla
   absolutas) dentro del recorte actual. */
/* Lista de lo que se dibuja ordenado: crece cuando hace falta */
static Drawable *s_draw_list;
static int       s_draw_cap;

static int draw_reserve(int n)
{
    if (n <= s_draw_cap)
        return 0;
    int cap = s_draw_cap ? s_draw_cap : 4096;
    while (cap < n)
        cap *= 2;
    Drawable *grown = realloc(s_draw_list, (size_t)cap * sizeof *grown);
    if (!grown)
        return -1;
    s_draw_list = grown;
    s_draw_cap = cap;
    return 0;
}

/* El techo de la casa donde está un jugador no se dibuja, como el original
   (en Kurast o Lut Gholein se entra a las casas y si no, el techo lo tapa):
   las baldosas de techo unidas a la que tiene encima, en una ventana de
   ROOF_WIN tiles alrededor */
#define ROOF_WIN 48
typedef struct {
    int     x0, y0, any, walls;
    uint8_t in[ROOF_WIN][ROOF_WIN];
    uint8_t wall_in[ROOF_WIN][ROOF_WIN]; /* las paredes-techo (roof_wall) */
} RoofHide;

static int has_roof(const Map *m, int tx, int ty)
{
    for (int l = 0; l < MAP_WALLS; l++) {
        const Dt1Tile *t = map_wall(m, l, tx, ty);
        if (t && t->orientation == ORI_ROOF)
            return 1;
    }
    return 0;
}

/* Hasta cuántos tiles se busca techo para cada lado desde una baldosa
   adentro de la casa que no tiene (las de algunas paredes de adentro) */
#define ROOF_HOLE 8

/* Los techos de paja de las chozas del muelle del Acto 3 (las de Alkor y
   Asheara) no son baldosas de techo: son paredes sin colisión en una capa
   de arriba, encima de las paredes de la choza */
static int roof_wall(const Map *m, int l, int tx, int ty)
{
    const Dt1Tile *t = l > 0 ? map_wall(m, l, tx, ty) : NULL;
    if (!t || !map_wall(m, 0, tx, ty) || t->orientation < ORI_LEFT_WALL || t->orientation > ORI_SOUTH_CORNER)
        return 0;
    for (int k = 0; k < TILE_SUB * TILE_SUB; k++)
        if (t->subtile_flags[k] & 1)
            return 0;
    return 1;
}

/* ¿Hay una pared-techo de frente (row: las de la fila, 2 y 6; si no las de
   la columna, 1 y 5) en ese tile? */
static int roof_wall_front(const Map *m, int tx, int ty, int row)
{
    for (int l = 1; l < MAP_WALLS; l++)
        if (roof_wall(m, l, tx, ty)) {
            int o = map_wall(m, l, tx, ty)->orientation;
            if (row ? o == ORI_RIGHT_WALL || o == ORI_RIGHT_END : o == ORI_LEFT_WALL || o == ORI_LEFT_END)
                return 1;
        }
    return 0;
}

/* Adentro de una choza: con su pared de delante en la misma columna y la
   del costado en la misma fila (las dos paredes que tapan desde la cámara);
   así se esconden las paredes-techo unidas a esas */
static void roof_walls_build(const Map *m, int ptx, int pty, RoofHide *h)
{
    static int16_t q[ROOF_WIN * ROOF_WIN][2];
    int fr = 0, fc = 0, n = 0;
    for (int k = 1; k <= ROOF_HOLE && !fr; k++)
        fr = roof_wall_front(m, ptx, pty + k, 1) ? k : 0;
    for (int k = 1; k <= ROOF_HOLE && !fc; k++)
        fc = roof_wall_front(m, ptx + k, pty, 0) ? k : 0;
    h->walls = fr && fc;
    if (!h->walls)
        return;
    memset(h->wall_in, 0, sizeof h->wall_in);
    h->wall_in[ROOF_WIN / 2 + fr][ROOF_WIN / 2] = 1;
    q[n][0] = ROOF_WIN / 2;
    q[n++][1] = (int16_t)(ROOF_WIN / 2 + fr);
    for (int k = 0; k < n; k++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = q[k][0] + dx, y = q[k][1] + dy, any = 0;
                if (x < 0 || y < 0 || x >= ROOF_WIN || y >= ROOF_WIN || h->wall_in[y][x])
                    continue;
                for (int l = 1; l < MAP_WALLS && !any; l++)
                    any = roof_wall(m, l, h->x0 + x, h->y0 + y);
                if (!any)
                    continue;
                h->wall_in[y][x] = 1;
                q[n][0] = (int16_t)x;
                q[n++][1] = (int16_t)y;
            }
}

static void roof_hide_build(const Map *m, float px, float py, RoofHide *h)
{
    static int16_t q[ROOF_WIN * ROOF_WIN][2];
    static const int8_t d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int ptx = (int)floorf(px / TILE_SUB), pty = (int)floorf(py / TILE_SUB), n = 0;
    h->x0 = ptx - ROOF_WIN / 2;
    h->y0 = pty - ROOF_WIN / 2;
    h->any = 0;
    roof_walls_build(m, ptx, pty, h);
    /* Sin techo encima: adentro igual si para los cuatro lados hay techo de
       la misma casa. En las de Kurast hay baldosas de adentro sin techo (las
       de algunas paredes) y parado ahí el techo lo tapaba. */
    int hit[4] = { 0, 0, 0, 0 };
    if (!has_roof(m, ptx, pty)) {
        for (int j = 0; j < 4; j++) {
            for (int s = 1; s <= ROOF_HOLE && !hit[j]; s++)
                if (has_roof(m, ptx + d[j][0] * s, pty + d[j][1] * s))
                    hit[j] = s;
            if (!hit[j])
                return;
        }
    }
    memset(h->in, 0, sizeof h->in);
    int sx = ROOF_WIN / 2 + d[0][0] * hit[0], sy = ROOF_WIN / 2;
    h->in[sy][sx] = 1;
    q[n][0] = (int16_t)sx;
    q[n++][1] = (int16_t)sy;
    for (int k = 0; k < n; k++) {
        for (int j = 0; j < 4; j++) {
            int x = q[k][0] + d[j][0], y = q[k][1] + d[j][1];
            if (x < 0 || y < 0 || x >= ROOF_WIN || y >= ROOF_WIN || h->in[y][x] || !has_roof(m, h->x0 + x, h->y0 + y))
                continue;
            h->in[y][x] = 1;
            q[n][0] = (int16_t)x;
            q[n++][1] = (int16_t)y;
        }
    }
    /* (de la misma: entre dos casas vecinas no) */
    h->any = 1;
    for (int j = 1; j < 4 && hit[0]; j++)
        if (!h->in[ROOF_WIN / 2 + d[j][1] * hit[j]][ROOF_WIN / 2 + d[j][0] * hit[j]])
            h->any = 0;
}

static int roof_hidden(const RoofHide *h, int tx, int ty)
{
    int x = tx - h->x0, y = ty - h->y0;
    return h->any && x >= 0 && y >= 0 && x < ROOF_WIN && y < ROOF_WIN && h->in[y][x];
}

static RoofHide s_roof_hide[MAX_PLAYERS];

/* ¿Esa pared es un techo de choza escondido por algún jugador? */
static int roof_wall_hidden(const Map *m, int l, int tx, int ty)
{
    for (int i = 0; i < s_num_players; i++) {
        const RoofHide *h = &s_roof_hide[i];
        int x = tx - h->x0, y = ty - h->y0;
        if (h->walls && x >= 0 && y >= 0 && x < ROOF_WIN && y < ROOF_WIN && h->wall_in[y][x] && roof_wall(m, l, tx, ty))
            return 1;
    }
    return 0;
}

static void render_world_body(float cam_x, float cam_y, float alpha)
{
    const Map *m = &s_map;
    if (draw_reserve(4096 + MAX_PLAYERS * 2 + units_count() + items_ground_count() + missiles_capacity()) != 0)
        return;
    Drawable *list = s_draw_list;

    int ox = SCREEN_W / 2 - (int)lroundf(cam_x);
    int oy = SCREEN_H / 2 - (int)lroundf(cam_y);
    if (!clip_bounds(&s_vis_x0, &s_vis_y0, &s_vis_x1, &s_vis_y1))
        return;

    /* Pisos y sombras: de la capa de esta vista, o enteros */
    static int floor_cache = -1;
    if (floor_cache < 0)
        floor_cache = !(getenv("D2_FLOOR_CACHE") && getenv("D2_FLOOR_CACHE")[0] == '0');
    FloorDraw fd = { m, ox, oy };
    int view = s_world_view++ & 1;
    if (floor_cache) {
        unsigned ver = map_version();
        fb_layer_update(&s_floor_layer[view], -ox, -oy, ver != s_floor_version[view], draw_floors, &fd);
        s_floor_version[view] = ver;
    } else {
        fb_fill_clip(0);
        draw_floors(&fd);
    }
    clip_bounds(&s_vis_x0, &s_vis_y0, &s_vis_x1, &s_vis_y1);
    overlays_draw_back(cam_x, cam_y, overlay_pos); /* el círculo de las auras, debajo de todo */

    /* Las paredes de los tiles que se ven, para ordenarlas con lo demás */
    int n = 0;
    int ty0, ty1;
    visible_rows(m, ox, oy, &ty0, &ty1);
    for (int ty = ty0; ty <= ty1; ty++) {
        int tx0, tx1;
        visible_cols(m, ox, oy, ty, &tx0, &tx1);
        for (int tx = tx0; tx <= tx1; tx++) {
            const MapChunk *ch = map_chunk(m, tx, ty);
            if (!ch)
                continue;
            int c = map_cell(tx, ty);
            for (int l = 0; l < MAP_WALLS; l++) {
                const Dt1Tile *t = ch->wall[l][c];
                if (!t || t->orientation == ORI_ROOF || is_lower_wall(t))
                    continue; /* (las de abajo van con el piso) */
                if (n + 4 > s_draw_cap - (MAX_PLAYERS * 2 + units_count() + items_ground_count() + missiles_capacity())) {
                    if (draw_reserve(s_draw_cap * 2) != 0)
                        return;
                    list = s_draw_list;
                }
                list[n++] = (Drawable){ wall_depth(tx, ty), (int16_t)tx, (int16_t)ty, (int8_t)l, DRAW_WALL, 0 };
            }
        }
    }

    /* Paredes, jugadores, NPCs y objetos, ordenados por profundidad */
    for (int i = 0; i < s_num_players; i++) {
        float px, py;
        player_world_pos(&s_pl[i], alpha, &px, &py);
        list[n++] = (Drawable){ unit_depth(px, py), 0, 0, 0, DRAW_PLAYER, (int16_t)i };
        if (s_pl[i].decoy_ticks > 0)
            list[n++] = (Drawable){ unit_depth(s_pl[i].decoy_x, s_pl[i].decoy_y), 0, 0, 0, DRAW_DECOY, (int16_t)i };
    }
    for (int i = 0; i < units_count(); i++) {
        float ux, uy, sx, sy;
        if (!units_visible(i))
            continue;
        units_position(i, alpha, &ux, &uy);
        iso_to_screen(ux, uy, &sx, &sy);
        if (ox + sx < s_vis_x0 - 200 || ox + sx > s_vis_x1 + 200 || oy + sy < s_vis_y0 - 100 || oy + sy > s_vis_y1 + 300)
            continue;
        list[n++] = (Drawable){ unit_depth(ux, uy) - (units_is_corpse(i) ? 3.0f : 0.0f) + (units_riding(i) ? 0.05f : 0.0f), 0, 0, 0,
                                DRAW_UNIT, (int16_t)i };
    }
    /* Objetos en el suelo: van debajo de los que están parados en el mismo lugar */
    for (int i = 0; i < items_ground_count(); i++) {
        float ix, iy, sx, sy;
        if (!items_ground_item(i, &ix, &iy))
            continue;
        iso_to_screen(ix, iy, &sx, &sy);
        if (ox + sx < s_vis_x0 - 100 || ox + sx > s_vis_x1 + 100 || oy + sy < s_vis_y0 - 100 || oy + sy > s_vis_y1 + 100)
            continue;
        list[n++] = (Drawable){ ix + iy - 2.5f, 0, 0, 0, DRAW_ITEM, (int16_t)i };
    }
    for (int i = 0; i < missiles_capacity(); i++) {
        float mx, my, sx, sy;
        if (!missiles_get(i))
            continue;
        missiles_position(i, alpha, &mx, &my);
        iso_to_screen(mx, my, &sx, &sy);
        if (ox + sx < s_vis_x0 - 150 || ox + sx > s_vis_x1 + 150 || oy + sy < s_vis_y0 - 150 || oy + sy > s_vis_y1 + 150)
            continue;
        list[n++] = (Drawable){ unit_depth(mx, my) + 0.5f, 0, 0, 0, DRAW_MISSILE, (int16_t)i };
    }
    qsort(list, (size_t)n, sizeof list[0], cmp_drawable);

    /* Los techos que no se dibujan (los de la casa donde hay un jugador):
       antes, por los de las chozas, que van con las paredes */
    for (int i = 0; i < s_num_players; i++) {
        float px, py;
        player_world_pos(&s_pl[i], alpha, &px, &py);
        roof_hide_build(m, px, py, &s_roof_hide[i]);
    }
    for (int i = 0; i < n; i++) {
        const Drawable *dr = &list[i];
        if (dr->kind == DRAW_DECOY) {
            const Player *p = &s_pl[dr->index];
            float sx, sy;
            iso_to_screen(p->decoy_x, p->decoy_y, &sx, &sy);
            const CharAnim *a = player_anim_draw(p, MODE_NU);
            fb_shade_ellipse(ox + (int)lroundf(sx), oy + (int)lroundf(sy), 18, 8, 14);
            charanim_draw(a, p->decoy_sector, (s_anim_ticks / 2) % (charanim_frames(a) > 0 ? charanim_frames(a) : 1),
                          ox + (int)lroundf(sx), oy + (int)lroundf(sy));
            continue;
        }
        if (dr->kind == DRAW_PLAYER) {
            float px, py, sx, sy;
            player_world_pos(&s_pl[dr->index], alpha, &px, &py);
            iso_to_screen(px, py, &sx, &sy);
            draw_player(ox + (int)lroundf(sx), oy + (int)lroundf(sy), dr->index);
            continue;
        }
        if (dr->kind == DRAW_ITEM) {
            float ix, iy, sx, sy;
            items_ground_item(dr->index, &ix, &iy);
            iso_to_screen(ix, iy, &sx, &sy);
            items_ground_draw(dr->index, ox + (int)lroundf(sx), oy + (int)lroundf(sy));
            continue;
        }
        if (dr->kind == DRAW_MISSILE) {
            float mx, my, sx, sy;
            missiles_position(dr->index, alpha, &mx, &my);
            iso_to_screen(mx, my, &sx, &sy);
            missiles_draw(dr->index, ox + (int)lroundf(sx), oy + (int)lroundf(sy));
            continue;
        }
        if (dr->kind == DRAW_UNIT) {
            float ux, uy, sx, sy;
            units_position(dr->index, alpha, &ux, &uy);
            iso_to_screen(ux, uy, &sx, &sy);
            units_draw(dr->index, ox + (int)lroundf(sx), oy + (int)lroundf(sy));
            continue;
        }
        int sx = (dr->tx - dr->ty) * (TILE_W / 2) + ox, sy = (dr->tx + dr->ty) * (TILE_H / 2) + oy;
        draw_wall_tile(map_wall_extra(m, dr->layer, dr->tx, dr->ty), sx, sy);
        if (!dr->layer || !roof_wall_hidden(m, dr->layer, dr->tx, dr->ty))
            draw_wall_tile(map_wall(m, dr->layer, dr->tx, dr->ty), sx, sy);
    }

    /* Techos (sin los de la casa donde hay un jugador) */
    for (int ty = ty0; ty <= ty1; ty++) {
        int tx0, tx1;
        visible_cols(m, ox, oy, ty, &tx0, &tx1);
        for (int tx = tx0; tx <= tx1; tx++) {
            int hidden = 0;
            for (int i = 0; i < s_num_players && !hidden; i++)
                hidden = roof_hidden(&s_roof_hide[i], tx, ty);
            for (int l = 0; l < MAP_WALLS && !hidden; l++) {
                const Dt1Tile *t = map_wall(m, l, tx, ty);
                if (!t || t->orientation != ORI_ROOF || !dt1_tile_ready(t)->pixels)
                    continue;
                int sx = (tx - ty) * (TILE_W / 2) + ox, sy = (tx + ty) * (TILE_H / 2) + oy;
                fb_blit_spans(t->pixels, t->spans, t->img_w, t->img_h, sx - TILE_W / 2, sy - t->roof_height + t->img_y);
            }
        }
    }

    if (s_debug) {
        /* Marca los subtiles que bloquean el paso */
        for (int ty = ty0; ty <= ty1; ty++) {
            int tx0, tx1;
            visible_cols(m, ox, oy, ty, &tx0, &tx1);
            for (int y = ty * TILE_SUB; y < (ty + 1) * TILE_SUB; y++)
                for (int x = tx0 * TILE_SUB; x < (tx1 + 1) * TILE_SUB; x++)
                    if (map_blocked(m, x, y)) {
                        float sx, sy;
                        iso_to_screen(x + 0.5f, y + 0.5f, &sx, &sy);
                        fb_fill_rect(ox + (int)sx - 1, oy + (int)sy - 1, 3, 3, COL(R_RED, 15));
                    }
        }
    }
}

static void render_world(float cam_x, float cam_y, float alpha)
{
    double t0 = wall_ms();
    render_world_body(cam_x, cam_y, alpha);
    s_part_ms[0] += wall_ms() - t0;
}

/* Línea divisoria: aparece de a poco a medida que las cámaras se separan */
static void draw_divider(float nx, float ny, float amount)
{
    float tx = -ny, ty = nx; /* dirección de la línea */
    int x0 = (int)(SCREEN_W / 2 - tx * 1000.0f), y0 = (int)(SCREEN_H / 2 - ty * 1000.0f);
    int x1 = (int)(SCREEN_W / 2 + tx * 1000.0f), y1 = (int)(SCREEN_H / 2 + ty * 1000.0f);
    int half = amount < 4.0f ? 0 : amount < 12.0f ? 1 : 2;
    for (int k = -half - 1; k <= half + 1; k++) {
        uint8_t c = (k < -half || k > half) ? COL(R_GOLD, 6) : COL(R_GRAY, 0);
        int dx = (int)lroundf(nx * k), dy = (int)lroundf(ny * k);
        fb_line(x0 + dx, y0 + dy, x1 + dx, y1 + dy, c);
    }
}


/* Textos arriba de la pantalla: nombre de la zona al entrar, subidas de
   nivel y muertes */
/* Un cartel centrado en cx: en tamaño 3 si entra en max_w, si no en 2 */
static int draw_banner_line(int cx, int y, int max_w, const char *text, int scale, uint8_t color)
{
    scale = font_fit(text, scale, max_w); /* (si no entra, más chico) */
    font_draw_centered(cx, y, text, scale, color);
    return font_height(scale) + 8;
}

/* Carteles de un jugador (zona, nivel, muerte), apilados desde y */
static void draw_player_banners(const Player *p, int cx, int y, int max_w)
{
    char text[96];
    if (p->banner_ticks > 0 && p->level) {
        y += draw_banner_line(cx, y, max_w, str_get(p->level->name), 3, COL(R_GOLD, 14));
        if (tz_level_on(p->level)) /* (EnteringDesecratedLevelNotification) */
            y += draw_banner_line(cx, y, max_w, str_get("EnteringDesecratedLevelNotification"), 2, g_text_shift[11][COL(R_GRAY, 15)]);
    }
    if (p->levelup_ticks > 0) {
        snprintf(text, sizeof text, tr("SUBISTE AL NIVEL %d"), p->level_num);
        y += draw_banner_line(cx, y, max_w, text, 3, COL(R_GOLD, 15));
        y += draw_banner_line(cx, y, max_w, "Select + X: atributos   Select + Y: habilidades", 1, COL(R_GRAY, 15));
    }
    if (p->state >= PS_DYING)
        draw_banner_line(cx, y, max_w, "MORISTE", 4, COL(R_RED, 13));
}

/* Con dos jugadores, cada uno ve sus carteles de su lado: en columnas si la
   cámara es compartida, o dentro de su mitad si la pantalla está partida */
static void draw_banners(void)
{
    eco_draw(); /* (arriba a la derecha) */
    int y = 36;
    if (s_quest_ticks > 0) {
        font_draw_centered(SCREEN_W / 2, y, s_quest_banner, 3, COL(R_GOLD, 15));
        y += 30;
    }
    if (s_num_local == 1) {
        /* En lo que queda libre: con un panel a la derecha, la mitad izquierda; con la tienda o el arcón
           (un panel de cada lado), el medio (si no, tapaban el título del panel) */
        const Player *p = &s_pl[s_local[0]];
        int shop = p->dialog_npc >= 0 && p->shop_open;
        int right = shop || p->inv_open || p->wp_open, left = shop || stash_side(p);
        int cx = left ? SCREEN_W / 2 : right ? (SCREEN_W - PANEL_W) / 2 : SCREEN_W / 2;
        int w = left && right ? SCREEN_W - 2 * PANEL_W - 8 : right ? SCREEN_W - PANEL_W - 20 : SCREEN_W - 40;
        draw_player_banners(p, cx, y, w);
        return;
    }
    if (!s_split) {
        draw_player_banners(&s_pl[s_local[0]], SCREEN_W / 4, y, SCREEN_W / 2 - 16);
        draw_player_banners(&s_pl[s_local[1]], SCREEN_W * 3 / 4, y, SCREEN_W / 2 - 16);
        return;
    }
    for (int i = 0; i < 2; i++) {
        /* El lugar más alto de su mitad donde entra el bloque de carteles
           entero (300 x 80 píxeles), lo más cerca posible de su costado */
        float side = i == 0 ? -1.0f : 1.0f;
        float want = SCREEN_W / 2.0f + side * s_split_nx * 190.0f;
        int best_x = (int)want, best_y = y, found = 0;
        for (int by = y; by < SCREEN_H - 160 && !found; by += 8) {
            float best_d = 1e9f;
            for (int bx = 160; bx <= SCREEN_W - 160; bx += 20) {
                int inside = 1;
                for (int c = 0; c < 4 && inside; c++) {
                    float px = (float)bx + (c % 2 ? 150.0f : -150.0f), py = (float)by + (c / 2 ? 80.0f : 0.0f);
                    float dist = ((px - SCREEN_W / 2.0f) * s_split_nx + (py - SCREEN_H / 2.0f) * s_split_ny) * side;
                    inside = dist > 10.0f;
                }
                if (inside && fabsf((float)bx - want) < best_d) {
                    best_d = fabsf((float)bx - want);
                    best_x = bx;
                    best_y = by;
                    found = 1;
                }
            }
        }
        clip_half_plane(SCREEN_W / 2.0f, SCREEN_H / 2.0f, s_split_nx, s_split_ny, i == 0 ? -1 : +1);
        draw_player_banners(&s_pl[s_local[i]], best_x, best_y, 300);
        clip_reset();
    }
}

/* Nombre y vida del último monstruo golpeado, arriba al centro */
/* Vida del último monstruo golpeado, arriba al centro como en el juego:
   barra roja translúcida con el nombre encima (dorado si es un jefe) */
static void draw_target_bar(void)
{
    if (getenv("D2_SHOW_TARGET")) { /* depuración: ese monstruo (por su Id o el nombre del jefe) */
        int id;
        if (units_find_all(getenv("D2_SHOW_TARGET"), &id, 1) == 1)
            s_shown_target = id;
    }
    if (s_shown_target < 0)
        return;
    int hp, hp_max;
    units_health(s_shown_target, &hp, &hp_max);
    const char *name = units_display_name(s_shown_target);
    int w = font_width(name, 2) + 40;
    if (w < 140)
        w = 140;
    int x = SCREEN_W / 2 - w / 2, y = 8, h = 20;
    fb_blend_rect(x, y, w, h, COL(R_GRAY, 0), BLEND_25);
    fb_blend_rect(x, y, hp_max > 0 ? w * hp / hp_max : 0, h, COL(R_RED, 6), BLEND_50);
    font_draw_centered(SCREEN_W / 2, y + (h - font_height(2)) / 2, name, 2,
                       units_is_unique(s_shown_target) ? COL(R_GOLD, 15) : units_is_champion(s_shown_target) ? COL(R_BLUE, 15)
                                                                                           : COL(R_GRAY, 15));
    /* Debajo, como el original: no muerto o demonio, lo del jefe o del campeón y las inmunidades
       (en dos renglones si no entra) */
    char desc[256];
    units_describe(s_shown_target, desc, sizeof desc);
    if (!desc[0])
        return;
    int dy = y + h + 2, most = SCREEN_W / 2 - 20;
    char *line = desc;
    for (int row = 0; row < 2 && *line; row++) {
        char *cut = NULL;
        if (font_width(line, 1) > most) /* corta en la última coma que entra */
            for (char *c = strchr(line, ','); c; c = strchr(c + 1, ',')) {
                *c = '\0';
                int fits = font_width(line, 1) <= most;
                *c = ',';
                if (!fits)
                    break;
                cut = c;
            }
        if (cut)
            *cut = '\0';
        int tw = font_width(line, 1);
        fb_blend_rect(SCREEN_W / 2 - tw / 2 - 4, dy - 1, tw + 8, font_height(1) + 2, COL(R_GRAY, 0), BLEND_50);
        font_draw_centered(SCREEN_W / 2, dy, line, 1, COL(R_GRAY, 15));
        dy += font_height(1) + 2;
        line = cut ? cut + 2 : line + strlen(line);
    }
}


static void draw_frame(int x, int y, int w, int h, uint8_t c)
{
    fb_hline(x, x + w - 1, y, c);
    fb_hline(x, x + w - 1, y + h - 1, c);
    fb_vline(x, y, y + h - 1, c);
    fb_vline(x + w - 1, y, y + h - 1, c);
}

/* ---- Íconos de habilidades ----
   <clase>skillicon.dc6 trae dos cuadros por habilidad; skillicon.dc6 tiene
   los genéricos (el 2 es el ataque normal, según skilldesc.txt). Se leen en
   el hilo de trabajos: los de los jugadores al empezar (preload_ui), los
   otros (los efectos de otra clase) la primera vez que se dibujan. */

#define ICONS_GENERIC NUM_CLASSES
#define ICON_ATTACK   2
enum { ICONS_NONE, ICONS_LOADING, ICONS_READY, ICONS_MISSING };
static Sprite s_icons[NUM_CLASSES + 1];
static int    s_icons_state[NUM_CLASSES + 1]; /* ICONS_* (job_flag_get/set) */

static void icons_job(void *ptr, int set)
{
    (void)ptr;
    char path[96];
    snprintf(path, sizeof path, "data/global/ui/spells/%s.dc6", set < NUM_CLASSES ? skills_icon_file(set) : "skillicon");
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int ok = d && dc6_load(d, n, &s_icons[set]) == 0;
    free(d);
    job_flag_set(&s_icons_state[set], ok ? ICONS_READY : ICONS_MISSING);
}

/* Que se lean: en su turno o (`first`) adelante de lo que espera */
static void skill_icons_load(int set, int first)
{
    if (job_flag_get(&s_icons_state[set]) != ICONS_NONE)
        return;
    if (set == 7) { /* el Brujo: los arma con sus efectos (skillwar.inc) al dibujarlos, con la paleta del acto */
        if (first)
            job_flag_set(&s_icons_state[set], war_icons_build(&s_icons[set]) ? ICONS_READY : ICONS_MISSING);
        return;
    }
    job_flag_set(&s_icons_state[set], ICONS_LOADING);
    if (!first)
        jobs_push(icons_job, NULL, set);
    else if (!jobs_push_first(icons_job, NULL, set))
        icons_job(NULL, set);
}

static const Sprite *skill_icons(int set)
{
    skill_icons_load(set, 1);
    return job_flag_get(&s_icons_state[set]) == ICONS_READY ? &s_icons[set] : NULL;
}

/* level: tabla de luz (LIGHT_LEVELS - 1 = normal, menos = más oscuro) */
static void draw_icon(int set, int frame, int x, int y, int size, int level)
{
    const Sprite *sp = skill_icons(set);
    if (!sp || frame < 0 || frame >= sp->frames_per_dir * sp->directions)
        return;
    const SpriteFrame *f = &sp->frames[frame];
    fb_blit_scaled(f->pixels, f->width, f->height, x, y, size, size, level);
}

/* Los cuatro botones con lo que tira cada uno; la letra va con el color
   del botón del joystick */
static const uint8_t k_button_ramp[NUM_BUTTONS] = { R_GRASS, R_RED, R_BLUE, R_GOLD };




#define TREE_TOP 88
#define TREE_ROW 52



/* ---- Paneles (320x432, como en el original) ----
   Con un jugador van a la derecha y la cámara se corre; con dos, cada uno
   contra el borde de su mitad. Arriba de cada panel va un cartelito con su
   nombre y los botones. Las posiciones salen de los diseños del juego
   (playerinventoryoriginallayout.json, characterstatspanel.json,
   skillstreepanel.json). */


static int player_index(const Player *p)
{
    return (int)(p - s_pl);
}

/* Su lugar en esta máquina (0 o 1: con dos, la mitad izquierda o la derecha), -1 = no es de acá */
static int local_slot(const Player *p)
{
    for (int k = 0; k < s_num_local; k++)
        if (s_local[k] == player_index(p))
            return k;
    return -1;
}

static int panel_x(const Player *p)
{
    return s_num_local == 1 || local_slot(p) == 1 ? SCREEN_W - PANEL_W : 0;
}

/* Mitad de la pantalla de ese jugador (con uno solo, toda) */
static void half_range(const Player *p, int *x0, int *x1)
{
    *x0 = s_num_local == 1 || local_slot(p) == 0 ? 0 : SCREEN_W / 2;
    *x1 = s_num_local == 1 || local_slot(p) == 1 ? SCREEN_W : SCREEN_W / 2;
}

/* El arcón (el cofre del pueblo) con un solo jugador va en su propio panel a
   la izquierda, con el inventario entero a la derecha, como el original. Con
   dos cada uno tiene su mitad de la pantalla y no entran dos paneles: el
   arcón va en el panel del inventario, en el lugar del equipo (arriba el
   arcón, abajo la mochila). Sus casillas, pestañas y botones son los mismos:
   cambia su esquina. */
static int stash_side(const Player *p)
{
    return p->stash_open && s_num_local == 1;
}

static int stash_px(const Player *p)
{
    return stash_side(p) ? 0 : panel_x(p);
}

/* Cartel arriba del panel: nombre y botones */
static void draw_panel_header_at(int px, const char *title, const char *help)
{
    /* Si la ayuda (en el idioma de la pantalla) no entra en el panel, con
       menos espacio entre los botones; y si tampoco, el cartel se estira
       hacia el lado de la pantalla (los idiomas de palabras largas) */
    char fit[160];
    snprintf(fit, sizeof fit, "%s", lang_screen(help));
    for (char *d; font_width(fit, 1) > PANEL_W - 8 && (d = strstr(fit, "  ")) != NULL;)
        memmove(d, d + 1, strlen(d));
    int w = font_width(fit, 1) + 12 > PANEL_W ? font_width(fit, 1) + 12 : PANEL_W;
    int bx = px + PANEL_W / 2 < SCREEN_W / 2 ? px : px + PANEL_W - w;
    bx = bx < 0 ? 0 : bx + w > SCREEN_W ? SCREEN_W - w : bx;
    ui_dark_box(bx, PANEL_Y - 40, w, 38);
    font_draw_centered(px + PANEL_W / 2, PANEL_Y - 37, title, 2, COL(R_GOLD, 15));
    font_draw_centered(bx + w / 2, PANEL_Y - 17, fit, 1, COL(R_STONE, 12));
}

static void draw_panel_header(const Player *p, const char *title, const char *help)
{
    draw_panel_header_at(panel_x(p), title, help);
}

/* Texto centrado dentro de un rectángulo del panel */
static void text_in(int x, int y, int w, int h, const char *text, int scale, uint8_t color)
{
    char cut[160];
    scale = font_fit(text, scale, w - 2); /* (si no entra, más chico; y si ni así, abreviado) */
    text = font_shorten(text, scale, w - 2, cut, sizeof cut);
    font_draw_centered(x + w / 2, y + (h - font_height(scale)) / 2, text, scale, color);
}

/* ---- Descripción de objetos y habilidades (cuadro negro, como en el juego) ---- */

typedef struct {
    char    text[96];
    uint8_t color;
    int     scale;
} TipLine;

#define TIP_MAX 30
#define TIP_WHITE COL(R_GRAY, 15)
#define TIP_BLUE  COL(R_BLUE, 15)
#define TIP_RED   COL(R_RED, 13)
#define TIP_GOLD  COL(R_GOLD, 15)

static int tip_add(TipLine *l, int n, uint8_t color, const char *text)
{
    if (n >= TIP_MAX)
        return n;
    snprintf(l[n].text, sizeof l[n].text, "%.95s", tr(text));
    l[n].color = color;
    l[n].scale = 2;
    return n + 1;
}

/* Cerca del rectángulo (ax, ay, aw, ah): arriba si entra, si no abajo,
   siempre dentro de la mitad del jugador y arriba de la barra */
static void draw_tip(const TipLine *l, int n, int ax, int ay, int aw, int ah, int x0, int x1)
{
    if (n == 0)
        return;
    int w = 0, h = 8;
    for (int i = 0; i < n; i++) {
        int lw = font_width(l[i].text, l[i].scale);
        if (lw > w)
            w = lw;
        h += font_height(l[i].scale) + 2;
    }
    w += 16;
    if (w > x1 - x0 - 4)
        w = x1 - x0 - 4;
    int x = ax + aw / 2 - w / 2;
    if (x + w > x1 - 2) x = x1 - 2 - w;
    if (x < x0 + 2) x = x0 + 2;
    int y = ay - h - 4;
    if (y < PANEL_Y) /* (debajo, si arriba tapa el cartel del panel: los de la fila de arriba del arcón) */
        y = ay + ah + 4;
    if (y + h > PANEL_TOP)
        y = PANEL_TOP - h;
    ui_dark_box(x, y, w, h);
    int ty = y + 4;
    for (int i = 0; i < n; i++) {
        font_draw_centered(x + w / 2, ty, l[i].text, l[i].scale, l[i].color);
        ty += font_height(l[i].scale) + 2;
    }
}

/* Como en el juego: nombre (blanco o azul si es mágico), daño o defensa,
   requisitos (en rojo si no le alcanzan) y las propiedades mágicas */
static int item_tip_req(const Item *it, TipLine *l, int str, int dex, int level);
static const Player *s_tip_owner; /* de quién es el equipo puesto (las piezas de los conjuntos) */

static int item_tip(const Player *p, const Item *it, TipLine *l)
{
    s_tip_owner = p;
    int n = item_tip_req(it, l, player_str(p), player_dex(p), p->level_num);
    s_tip_owner = NULL;
    return n;
}

/* La descripción, con los requisitos en rojo si no le alcanzan a quien lo
   usa (el jugador o su mercenaria; level < 0 = sin nivel) */
static int item_tip_req(const Item *it, TipLine *l, int str, int dex, int level)
{
    const ItemType *t = it->type;
    char buf[96], name[64];
    int n = 0;
    if (!t)
        return 0;
    n = tip_add(l, n, items_label_color(it), items_label(it, name, sizeof name));
    if (it->quality == QUALITY_UNIQUE || it->quality == QUALITY_SET || it->quality == QUALITY_RARE ||
        it->quality == QUALITY_RUNEWORD) /* y abajo, el objeto que es */
        n = tip_add(l, n, it->quality == QUALITY_RUNEWORD ? COL(R_GRAY, 11) : items_label_color(it), t->name);
    if (t->kind == ITEM_WEAPON) {
        int lo, hi;
        items_damage(it, &lo, &hi);
        snprintf(buf, sizeof buf, tr("Daño %s: %d a %d"), t->two_handed ? tr("a dos manos") : tr("a una mano"), lo, hi);
        n = tip_add(l, n, TIP_WHITE, buf);
        if (items_can_throw(t)) {
            items_thrown_damage(it, &lo, &hi);
            snprintf(buf, sizeof buf, tr("Daño al lanzar: %d a %d"), lo, hi);
            n = tip_add(l, n, TIP_WHITE, buf);
            snprintf(buf, sizeof buf, tr("Cantidad: %d"), it->amount);
            n = tip_add(l, n, TIP_WHITE, buf);
        }
    } else if (t->kind == ITEM_ARMOR) {
        snprintf(buf, sizeof buf, tr("Defensa: %d"), items_defense(it));
        n = tip_add(l, n, items_stat(it, STAT_AC_PCT) || items_stat(it, STAT_AC) ? TIP_BLUE : TIP_WHITE, buf);
        if (t->slot == SLOT_SHIELD && t->block > 0) {
            snprintf(buf, sizeof buf, tr("Probabilidad de bloquear: %d%%"), t->block + items_stat(it, STAT_BLOCK));
            n = tip_add(l, n, items_stat(it, STAT_BLOCK) ? TIP_BLUE : TIP_WHITE, buf);
        }
    } else if (t->ammo) {
        snprintf(buf, sizeof buf, tr("Cantidad: %d"), it->amount);
        n = tip_add(l, n, TIP_WHITE, buf);
        n = tip_add(l, n, TIP_WHITE, t->ammo == 1 ? "Para arcos" : "Para ballestas");
    } else if (strcmp(t->code, "gld") != 0) {
        item_info_text(it, buf, sizeof buf);
        if (buf[0])
            n = tip_add(l, n, TIP_WHITE, buf);
        if (is_potion(t)) {
            int top = it->amount % POTION_CHARGES ? it->amount % POTION_CHARGES : POTION_CHARGES;
            if (stack_count(it) > 1) {
                snprintf(buf, sizeof buf, tr("Cantidad: %d"), stack_count(it));
                n = tip_add(l, n, TIP_WHITE, buf);
            }
            snprintf(buf, sizeof buf, tr("Tragos: %d de %d"), top, POTION_CHARGES);
            n = tip_add(l, n, TIP_WHITE, buf);
        }
        if (strcmp(t->code, "tsc") == 0)
            n = tip_add(l, n, TIP_WHITE, "X: abrir el portal");
        if (strncmp(t->code, "xa", 2) == 0 && t->code[2] >= '1' && t->code[2] <= '5' && !t->code[3]) { /* (UseTerrorTokenAct1..5) */
            snprintf(buf, sizeof buf, tr("X: aterrorizar el Acto %d"), t->code[2] - '0');
            n = tip_add(l, n, TIP_WHITE, buf);
        }
        if (strcmp(t->code, "toa") == 0)
            n = tip_add(l, n, TIP_WHITE, "X: reasignar los puntos");
    }
    int rstr, rdex;
    item_reqs(s_tip_owner, t, it, &rstr, &rdex);
    if (rdex > 0) {
        snprintf(buf, sizeof buf, tr("Destreza necesaria: %d"), rdex);
        n = tip_add(l, n, dex >= rdex ? TIP_WHITE : TIP_RED, buf);
    }
    if (rstr > 0) {
        snprintf(buf, sizeof buf, tr("Fuerza necesaria: %d"), rstr);
        n = tip_add(l, n, str >= rstr ? TIP_WHITE : TIP_RED, buf);
    }
    int lreq = items_level_req(it);
    if (level >= 0 && lreq > 1) {
        snprintf(buf, sizeof buf, tr("Nivel necesario: %d"), lreq);
        n = tip_add(l, n, level >= lreq ? TIP_WHITE : TIP_RED, buf);
    }
    /* Las propiedades (las que suben con el nivel, con el de quien lo ve) y
       lo de abajo: engarces, gemas, talismanes y los conjuntos */
    char lines[TIP_MAX][112];
    uint8_t colors[TIP_MAX];
    int clvl = level > 0 ? level : 1, nl = items_describe(it, clvl, lines, TIP_MAX);
    for (int k = 0; k < nl && n < TIP_MAX; k++)
        n = tip_add(l, n, TIP_BLUE, lines[k]);
    const Item *worn[SLOT_COUNT];
    int nworn = 0;
    for (int s = 0; s_tip_owner && s < SLOT_COUNT; s++)
        if (s_tip_owner->equip[s].type)
            worn[nworn++] = &s_tip_owner->equip[s];
    nl = items_describe_extra(it, worn, nworn, clvl, lines, colors, TIP_MAX);
    for (int k = 0; k < nl && n < TIP_MAX; k++)
        n = tip_add(l, n, colors[k], lines[k]);
    return n;
}

/* Lugares del equipo en el panel (playerinventoryoriginallayout.json) */
static const int k_slot_rect[SLOT_COUNT][4] = {
    [SLOT_WEAPON] = { 20, 47, 55, 112 },  [SLOT_SHIELD] = { 251, 47, 55, 112 },
    [SLOT_BODY] = { 133, 77, 56, 82 },    [SLOT_HELM] = { 135, 8, 54, 51 },
    [SLOT_GLOVES] = { 21, 181, 54, 53 },  [SLOT_BOOTS] = { 252, 182, 54, 52 },
    [SLOT_BELT] = { 136, 179, 52, 25 },   [SLOT_AMULET] = { 209, 35, 23, 24 },
    [SLOT_RING1] = { 95, 180, 23, 24 },   [SLOT_RING2] = { 209, 180, 23, 24 },
};

/* x del primer lugar del cinturón en la barra de abajo */
static int belt_x(const Player *p)
{
    if (s_num_local == 1)
        return 422;
    int right = local_slot(p) == 1;
    int strip = right ? SCREEN_W - 117 - (STRIP_TO - STRIP_FROM) : 117;
    return strip - STRIP_FROM + 257;
}

/* Rectángulo en pantalla de cada lugar que recorre el cursor: equipo,
   mochila y cinturón */
static void inv_entry_rect(const Player *p, int i, int *x, int *y, int *w, int *h)
{
    int px = panel_x(p), py = PANEL_Y;
    *x = *y = *w = *h = 0;
    if (i < SLOT_COUNT && (p->cube_open || (p->stash_open && !stash_side(p))))
        return; /* el cubo o el arcón (con dos jugadores) tapan el equipo */
    if (i >= INV_CUBE0 && i <= INV_TRANSMUTE && !p->cube_open)
        return;
    if (i >= INV_STASH0) { /* el arcón: sus casillas, las pestañas (y "+") y los botones del oro */
        if (!p->stash_open)
            return;
        px = stash_px(p);
        if (i < INV_STAB0) {
            *x = px + STASH_GX + (i - INV_STASH0) % STASH_W * CELL;
            *y = py + STASH_GY + (i - INV_STASH0) / STASH_W * CELL;
            *w = *h = CELL;
        } else if (i < INV_GOLD_IN) {
            int k = i - INV_STAB0, n = stash_tabs(p);
            if (k > n || (k == n && !stash_can_buy(p)))
                return;
            *x = px + STASH_GX + k * CELL;
            *y = py + 8;
            *w = CELL - 2;
            *h = 22;
        } else { /* los del oro, a la derecha debajo de la grilla, del ancho de su texto (en cada idioma) */
            int win = font_width("Guardar oro", 1) + 14, wout = font_width("Sacar oro", 1) + 14;
            int right = px + STASH_GX + STASH_W * CELL;
            *x = i == INV_GOLD_OUT ? right - wout : right - wout - 4 - win;
            *y = py + STASH_GY + STASH_H * CELL + 5;
            *w = i == INV_GOLD_OUT ? wout : win;
            *h = 24;
        }
        return;
    }
    if (i >= INV_CUBE0 && i < INV_TRANSMUTE) {
        *x = px + CUBE_X + (i - INV_CUBE0) % CUBE_W * CELL;
        *y = py + CUBE_Y + (i - INV_CUBE0) / CUBE_W * CELL;
        *w = *h = CELL;
        return;
    }
    if (i == INV_TRANSMUTE) {
        *x = px + CUBE_X + CUBE_W * CELL / 2 - 56;
        *y = py + CUBE_Y + CUBE_H * CELL + 10;
        *w = 112;
        *h = 26;
        return;
    }
    if (i < SLOT_COUNT) {
        *x = px + k_slot_rect[i][0];
        *y = py + k_slot_rect[i][1];
        *w = k_slot_rect[i][2];
        *h = k_slot_rect[i][3];
    } else if (i < INV_BELT0) {
        *x = px + 19 + (i - INV_CELL0) % BAG_W * CELL;
        *y = py + 255 + (i - INV_CELL0) / BAG_W * CELL;
        *w = *h = CELL;
    } else if (i < INV_SORT) {
        *x = belt_x(p) + (i - INV_BELT0) * BELT_STEP;
        *y = PANEL_TOP + 64;
        *w = *h = 29;
    } else if (i == INV_SORT) { /* botón Ordenar */
        *x = px + 206;
        *y = py + 386;
        *w = 96;
        *h = 26;
    } else { /* botón Personaje: el recuadro de abajo a la izquierda */
        *x = px + 18;
        *y = py + 385;
        *w = 32;
        *h = 32;
    }
}

/* El lugar más cercano en esa dirección (por la posición en pantalla) */
static int nav_nearest(int count, int sel, int dx, int dy, void (*rect)(const void *, int, int *, int *, int *, int *),
                       const void *ctx)
{
    int x, y, w, h;
    rect(ctx, sel, &x, &y, &w, &h);
    float cx = x + w / 2.0f, cy = y + h / 2.0f;
    int best = sel;
    float best_score = 1e9f;
    for (int i = 0; i < count; i++) {
        if (i == sel)
            continue;
        rect(ctx, i, &x, &y, &w, &h);
        if (w <= 0)
            continue;
        float ox = x + w / 2.0f - cx, oy = y + h / 2.0f - cy;
        float along = dx ? ox * dx : oy * dy, across = dx ? fabsf(oy) : fabsf(ox);
        if (along <= 4.0f)
            continue;
        float score = along + across * 2.0f;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static void inv_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h)
{
    inv_entry_rect((const Player *)ctx, i, x, y, w, h);
}

/* El cubo abierto: tapa el equipo con su grilla de 3 x 4 y el botón Transmutar */
static void draw_cube(const Player *p, int px, int py)
{
    ui_dark_box(px + 14, py + 4, 296, 238);
    const ItemType *box = items_find("box");
    if (box) { /* el dibujo del cubo, a la izquierda */
        int bw, bh;
        items_inv_size(box, &bw, &bh);
        items_draw_inv(box, px + 30 + (58 - bw) / 2, py + CUBE_Y + 20);
    }
    text_in(px + 14, py + 14, 296, 20, str_get("box"), 2, COL(R_GOLD, 14));
    int gx = px + CUBE_X, gy = py + CUBE_Y;
    fb_blend_rect(gx, gy, CUBE_W * CELL, CUBE_H * CELL, COL(R_STONE, 3), BLEND_50);
    for (int c = 0; c < CUBE_W * CUBE_H; c++)
        draw_frame(gx + c % CUBE_W * CELL, gy + c / CUBE_W * CELL, CELL, CELL, COL(R_STONE, 7));
    for (int i = 0; i < p->cube_count; i++) {
        const ItemType *t = p->cube[i].type;
        int x = gx + p->cube_x[i] * CELL, y = gy + p->cube_y[i] * CELL, w, h;
        fb_blend_rect(x, y, t->inv_w * CELL, t->inv_h * CELL, COL(R_BLUE, 4), BLEND_50);
        items_inv_size(t, &w, &h);
        items_draw_item(&p->cube[i], x + (t->inv_w * CELL - w) / 2, y + (t->inv_h * CELL - h) / 2);
        draw_stack_count(&p->cube[i], x, y, t->inv_w * CELL, t->inv_h * CELL);
    }
    int x, y, w, h;
    inv_entry_rect(p, INV_TRANSMUTE, &x, &y, &w, &h);
    ui_box(x, y, w, h);
    text_in(x, y, w, h, str_get("strUiMenu2"), 2, p->inv_sel == INV_TRANSMUTE ? COL(R_GOLD, 15) : COL(R_GRAY, 14));
    text_in(px + 14, y + h + 6, 296, 14, "Las recetas: Opciones, Guía del cubo", 1, COL(R_GRAY, 11));
}

/* El cofre abierto: tapa el equipo con sus pestañas, la grilla de la que se
   ve y el oro guardado */
static void draw_stash(const Player *p, int px, int py)
{
    fb_shade_rect(px + 14, py + 4, 296, 238, 1); /* (casi negro: que no se vean los lugares del equipo detrás) */
    int n = stash_tabs(p), x, y, w, h;
    for (int k = 0; k <= n && k < STASH_TABS; k++) {
        inv_entry_rect(p, INV_STAB0 + k, &x, &y, &w, &h);
        if (w <= 0)
            continue;
        ui_box(x, y, w, h);
        if (k == p->stash_tab)
            fb_blend_rect(x + 1, y + 1, w - 2, h - 2, COL(R_GOLD, 6), BLEND_50);
        if (k < n) {
            char label[12];
            snprintf(label, sizeof label, "%d", k + 1);
            text_in(x, y, w, h, label, 2, k == p->stash_tab ? COL(R_GOLD, 15) : COL(R_GRAY, 13));
        } else { /* "+" (la letra grande no lo tiene): dos rayas */
            int cx = x + w / 2, cy = y + h / 2;
            uint8_t c = p->inv_sel == INV_STAB0 + k ? COL(R_GOLD, 15) : COL(R_GRASS, 13);
            for (int d = -1; d <= 0; d++) {
                fb_hline(cx - 5, cx + 5, cy + d, c);
                fb_vline(cx + d, cy - 5, cy + 5, c);
            }
        }
    }
    int gx = px + STASH_GX, gy = py + STASH_GY, tab = p->stash_tab;
    fb_blend_rect(gx, gy, STASH_W * CELL, STASH_H * CELL, COL(R_STONE, 3), BLEND_50);
    for (int c = 0; c < STASH_CELLS; c++)
        draw_frame(gx + c % STASH_W * CELL, gy + c / STASH_W * CELL, CELL, CELL, COL(R_STONE, 7));
    for (int i = 0; i < p->stash_count[tab]; i++) {
        const Item *it = &p->stash[tab][i];
        const ItemType *t = it->type;
        int ix = gx + p->stash_x[tab][i] * CELL, iy = gy + p->stash_y[tab][i] * CELL;
        fb_blend_rect(ix, iy, t->inv_w * CELL, t->inv_h * CELL, meets_reqs(p, t) ? COL(R_BLUE, 4) : COL(R_RED, 8),
                      BLEND_50);
        items_inv_size(t, &w, &h);
        items_draw_item(it, ix + (t->inv_w * CELL - w) / 2, iy + (t->inv_h * CELL - h) / 2);
        draw_stack_count(it, ix, iy, t->inv_w * CELL, t->inv_h * CELL);
    }
    /* El oro guardado en dos renglones, a la izquierda de los botones */
    char text[48];
    snprintf(text, sizeof text, "%d", p->stash_gold);
    inv_entry_rect(p, INV_GOLD_IN, &x, &y, &w, &h);
    font_draw(px + STASH_GX, y + h / 2 - font_height(1), str_get("strGoldInStash"), 1, COL(R_GOLD, 14));
    font_draw(px + STASH_GX, y + h / 2 + 1, text, 1, COL(R_GOLD, 15));
    for (int b = INV_GOLD_IN; b <= INV_GOLD_OUT; b++) {
        inv_entry_rect(p, b, &x, &y, &w, &h);
        ui_box(x, y, w, h);
        text_in(x, y, w, h, b == INV_GOLD_IN ? "Guardar oro" : "Sacar oro", 1,
                p->inv_sel == b ? COL(R_GOLD, 15) : COL(R_GRAY, 14));
    }
}

/* El arcón en su propio panel, a la izquierda (con un solo jugador): sus
   pestañas, la grilla y el oro, y abajo cómo se usa */
static void draw_stash_side(const Player *p)
{
    int sx = stash_px(p), sy = PANEL_Y;
    draw_panel_header_at(sx, str_get("stash"), p->held.type ? "A o X: soltar acá   B: guardar en la mochila"
                                                             : "A: levantar   X: a la mochila   B: cerrar");
    ui_box(sx, sy, PANEL_W, 432);
    draw_stash(p, sx, sy);
    static const char *const k_legend[] = {
        "A: levantar y soltar donde quieras",
        "X: pasar entre la mochila y el arcón",
        "LB: ordenar esta pestaña",
        "Pestañas: una por acto; + compra otra",
        "El oro del arcón no se pierde al morir",
    };
    int y = sy + 262, room = PANEL_W - 2 * STASH_GX;
    for (size_t k = 0; k < sizeof k_legend / sizeof k_legend[0]; k++, y += 8) {
        /* en el idioma de la pantalla; lo que no entra, en otro renglón (cortando en un espacio) */
        const char *s = lang_screen(k_legend[k]);
        uint8_t col = k <= 1 ? COL(R_GRAY, 15) : COL(R_STONE, 13);
        while (*s) {
            char line[160];
            int len = font_break(s, 1, room);
            snprintf(line, sizeof line, "%.*s", len < 159 ? len : 159, s);
            font_draw(sx + STASH_GX, y, line, 1, col);
            y += font_height(1) + 2;
            for (s += len; *s == ' ' || *s == '\n'; s++) {
            }
        }
    }
}

/* Cuántas hay en una pila (pociones, llaves, pergaminos), abajo a la derecha
   de su lugar */
static void draw_stack_count(const Item *it, int x, int y, int w, int h)
{
    int n = stack_count(it);
    if (n <= 1)
        return;
    char q[12];
    snprintf(q, sizeof q, "%d", n);
    int qw = font_width(q, 1);
    fb_blend_rect(x + w - qw - 3, y + h - 11, qw + 2, 10, COL(R_GRAY, 0), BLEND_50);
    font_draw(x + w - qw - 2, y + h - 11, q, 1, COL(R_GRAY, 15));
}

static void draw_inventory(const Player *p)
{
    int px = panel_x(p), py = PANEL_Y, sx = stash_px(p), x0, x1;
    half_range(p, &x0, &x1);
    int side = stash_side(p), covered = p->cube_open || (p->stash_open && !side); /* (el equipo, tapado) */
    int shop = p->dialog_npc >= 0 && p->shop_open; /* al lado de la tienda (o en su lugar, vendiendo) */
    if (shop)
        draw_panel_header(p, p->shop_selling ? "Vender" : "Inventario",
                          p->gambling       ? ""
                          : p->shop_selling ? "A: vender   Select: comprar   B: volver"
                                            : "Select: vender lo de la mochila");
    else
        draw_panel_header(p, p->cube_open ? "Cubo Horádrico" : p->stash_open && !side ? str_get("stash") : "Inventario",
                          p->held.type ? "A o X: soltar acá   RB o Y: tirar al suelo   B: guardar"
                          : p->cube_open ? "A: levantar   X: pasar   RB: tirar al suelo   B: cerrar"
                          : side         ? "A: levantar   X: al arcón   LB: ordenar   B: cerrar"
                          : p->stash_open ? "A: levantar   X: pasar   LB: ordenar   B: cerrar"
                                          : "A: levantar  X: usar  RB: tirar  LB: ordenar  Y: más");
    ui_grid("panel/invchar6", 4, 4, 2, px, py);

    if (p->cube_open)
        draw_cube(p, px, py);
    else if (side)
        draw_stash_side(p);
    else if (p->stash_open)
        draw_stash(p, px, py);
    /* Equipo: centrado en su lugar; en rojo si no le alcanzan los requisitos */
    for (int s = 0; s < SLOT_COUNT && !covered; s++) {
        const Item *it = &p->equip[s];
        if (!it->type)
            continue;
        int x = px + k_slot_rect[s][0], y = py + k_slot_rect[s][1], w, h;
        items_inv_size(it->type, &w, &h);
        if (!meets_reqs(p, it->type))
            fb_blend_rect(x, y, k_slot_rect[s][2], k_slot_rect[s][3], COL(R_RED, 8), BLEND_50);
        items_draw_item(it, x + (k_slot_rect[s][2] - w) / 2, y + (k_slot_rect[s][3] - h) / 2);
    }
    /* Mochila: cada objeto en su lugar */
    for (int i = 0; i < p->bag_count; i++) {
        const ItemType *t = p->bag[i].type;
        int x = px + 19 + p->bag_x[i] * CELL, y = py + 255 + p->bag_y[i] * CELL, w, h;
        fb_blend_rect(x, y, t->inv_w * CELL, t->inv_h * CELL,
                      meets_reqs(p, t) ? COL(R_BLUE, 4) : COL(R_RED, 8), BLEND_50);
        items_inv_size(t, &w, &h);
        items_draw_item(&p->bag[i], x + (t->inv_w * CELL - w) / 2, y + (t->inv_h * CELL - h) / 2);
        draw_stack_count(&p->bag[i], x, y, t->inv_w * CELL, t->inv_h * CELL);
    }
    char text[32];
    snprintf(text, sizeof text, "%d", p->gold);
    font_draw(px + 108, py + 394, text, 2, COL(R_GRAY, 15));

    /* Botón Ordenar */
    int x, y, w, h;
    inv_entry_rect(p, INV_SORT, &x, &y, &w, &h);
    ui_box(x, y, w, h);
    text_in(x, y, w, h, "Ordenar", 2, p->inv_sel == INV_SORT ? COL(R_GOLD, 15) : COL(R_GRAY, 14));

    /* Botón Personaje: la cruz roja del original (titila si hay puntos sin repartir) */
    inv_entry_rect(p, INV_STATS, &x, &y, &w, &h);
    ui_frame("panel/level", p->stat_points > 0 ? (int)(s_ticks / 10) % 2 : 0, x + (w - 30) / 2, y + (h - 30) / 2);
    if (shop && !p->shop_selling) /* (el cursor está en la tienda) */
        return;

    /* Cursor: marca el objeto entero, o el lugar donde caería lo de la mano */
    inv_entry_rect(p, p->inv_sel, &x, &y, &w, &h);
    const Item *under = cursor_item((Player *)p);
    TipLine tip[TIP_MAX];
    int n = 0;
    if (p->held.type) {
        const ItemType *t = p->held.type;
        int gx = x, gy = y, gw = w, gh = h, ok = 1;
        if (p->inv_sel >= INV_CELL0 && p->inv_sel < INV_BELT0) {
            int cx = (p->inv_sel - INV_CELL0) % BAG_W, cy = (p->inv_sel - INV_CELL0) / BAG_W;
            if (cx + t->inv_w > BAG_W) cx = BAG_W - t->inv_w;
            if (cy + t->inv_h > BAG_H) cy = BAG_H - t->inv_h;
            gx = px + 19 + cx * CELL;
            gy = py + 255 + cy * CELL;
            gw = t->inv_w * CELL;
            gh = t->inv_h * CELL;
            int first = -1;
            for (int a = 0; a < t->inv_h; a++)
                for (int b = 0; b < t->inv_w; b++) {
                    int k = bag_at(p, cx + b, cy + a);
                    if (k >= 0 && first >= 0 && k != first)
                        ok = 0;
                    if (k >= 0)
                        first = k;
                }
        } else if (p->inv_sel >= INV_CUBE0 && p->inv_sel < INV_TRANSMUTE) {
            int cx = (p->inv_sel - INV_CUBE0) % CUBE_W, cy = (p->inv_sel - INV_CUBE0) / CUBE_W;
            if (cx + t->inv_w > CUBE_W) cx = CUBE_W - t->inv_w;
            if (cy + t->inv_h > CUBE_H) cy = CUBE_H - t->inv_h;
            ok = cx >= 0 && cy >= 0 && strcmp(t->code, "box") != 0;
            cx = cx < 0 ? 0 : cx;
            cy = cy < 0 ? 0 : cy;
            gx = px + CUBE_X + cx * CELL;
            gy = py + CUBE_Y + cy * CELL;
            gw = t->inv_w * CELL;
            gh = t->inv_h * CELL;
            int first = -1;
            for (int a = 0; a < t->inv_h && ok; a++)
                for (int b = 0; b < t->inv_w; b++) {
                    int k = cube_at(p, cx + b, cy + a);
                    if (k >= 0 && first >= 0 && k != first)
                        ok = 0;
                    if (k >= 0)
                        first = k;
                }
        } else if (p->inv_sel >= INV_STASH0 && p->inv_sel < INV_STAB0) {
            int cx = (p->inv_sel - INV_STASH0) % STASH_W, cy = (p->inv_sel - INV_STASH0) / STASH_W;
            if (cx + t->inv_w > STASH_W) cx = STASH_W - t->inv_w;
            if (cy + t->inv_h > STASH_H) cy = STASH_H - t->inv_h;
            gx = sx + STASH_GX + cx * CELL;
            gy = py + STASH_GY + cy * CELL;
            gw = t->inv_w * CELL;
            gh = t->inv_h * CELL;
            int first = -1;
            for (int a = 0; a < t->inv_h; a++)
                for (int b = 0; b < t->inv_w; b++) {
                    int k = stash_at(p, p->stash_tab, cx + b, cy + a);
                    if (k >= 0 && first >= 0 && k != first)
                        ok = 0;
                    if (k >= 0)
                        first = k;
                }
        } else if (p->inv_sel >= INV_STAB0 && p->inv_sel < INV_GOLD_IN) {
            ok = p->inv_sel - INV_STAB0 < stash_tabs(p); /* A: se ve esa pestaña, con lo de la mano */
        } else if (p->inv_sel < SLOT_COUNT) {
            const Item *on = &p->equip[p->inv_sel];
            ok = (items_slot_ok(t, p->inv_sel) && meets_item(p, &p->held)) || (on->type && items_can_socket(on, &p->held));
        } else if (p->inv_sel < INV_SORT) {
            ok = is_potion(t);
        } else {
            ok = 0;
        }
        fb_blend_rect(gx, gy, gw, gh, ok ? COL(R_GRASS, 8) : COL(R_RED, 8), BLEND_50);
        int iw, ih;
        items_inv_size(t, &iw, &ih);
        items_draw_item(&p->held, gx + (gw - iw) / 2, gy + (gh - ih) / 2);
        draw_stack_count(&p->held, gx, gy, gw, gh);
        draw_frame(gx - 1, gy - 1, gw + 2, gh + 2, COL(R_GOLD, 15));
        n = item_tip(p, &p->held, tip);
        draw_tip(tip, n, gx, gy, gw, gh, x0, x1);
        return;
    }
    if (under && p->inv_sel >= INV_CELL0 && p->inv_sel < INV_BELT0) { /* el objeto entero */
        int k = (int)(under - p->bag);
        x = px + 19 + p->bag_x[k] * CELL;
        y = py + 255 + p->bag_y[k] * CELL;
        w = under->type->inv_w * CELL;
        h = under->type->inv_h * CELL;
    } else if (under && p->inv_sel >= INV_CUBE0 && p->inv_sel < INV_TRANSMUTE) {
        int k = (int)(under - p->cube);
        x = px + CUBE_X + p->cube_x[k] * CELL;
        y = py + CUBE_Y + p->cube_y[k] * CELL;
        w = under->type->inv_w * CELL;
        h = under->type->inv_h * CELL;
    } else if (under && p->inv_sel >= INV_STASH0 && p->inv_sel < INV_STAB0) {
        int k = (int)(under - p->stash[p->stash_tab]);
        x = sx + STASH_GX + p->stash_x[p->stash_tab][k] * CELL;
        y = py + STASH_GY + p->stash_y[p->stash_tab][k] * CELL;
        w = under->type->inv_w * CELL;
        h = under->type->inv_h * CELL;
    }
    draw_frame(x - 1, y - 1, w + 2, h + 2, COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    if (under) {
        n = item_tip(p, under, tip);
        if (shop) { /* vendiendo: lo que le pagan (lo de las misiones no se vende) */
            char buf[64];
            int quest = is_quest_item(under->type);
            snprintf(buf, sizeof buf, "%s: %d", tr("Te pagan"), items_price(under, 1));
            n = tip_add(tip, n, quest ? TIP_RED : TIP_GOLD, quest ? "Eso no se vende" : buf);
        }
    } else if (p->inv_sel < SLOT_COUNT) {
        static const char *const names[SLOT_COUNT] = { "Arma", "Escudo", "Armadura", "Casco", "Guantes", "Botas",
                                                       "Cinturón", "Amuleto", "Anillo", "Anillo" };
        n = tip_add(tip, 0, COL(R_STONE, 12), names[p->inv_sel]);
    } else if (p->inv_sel == INV_TRANSMUTE) {
        n = tip_add(tip, 0, COL(R_GOLD, 15), str_get("strUiMenu2"));
        n = tip_add(tip, n, COL(R_GRAY, 15), "Combina lo que hay en el cubo");
        n = tip_add(tip, n, COL(R_GRAY, 15), "si es una de las recetas (Opciones, Guía del cubo)");
    } else if (p->inv_sel >= INV_CUBE0 && p->inv_sel < INV_TRANSMUTE) {
        n = tip_add(tip, 0, COL(R_GRAY, 15), "X sobre algo de la mochila lo pasa al cubo");
    } else if (p->inv_sel >= INV_STASH0 && p->inv_sel < INV_STAB0) {
        n = tip_add(tip, 0, COL(R_GRAY, 15), "X sobre algo de la mochila lo pasa al arcón");
    } else if (p->inv_sel >= INV_STAB0 && p->inv_sel < INV_GOLD_IN) {
        char buf[64];
        int k = p->inv_sel - INV_STAB0;
        if (k < stash_tabs(p)) {
            snprintf(buf, sizeof buf, tr("Pestaña %d del arcón"), k + 1);
            n = tip_add(tip, 0, COL(R_GOLD, 15), buf);
            n = tip_add(tip, n, COL(R_GRAY, 15), "Una por cada acto al que llegás");
        } else {
            n = tip_add(tip, 0, COL(R_GOLD, 15), "Comprar otra pestaña");
            snprintf(buf, sizeof buf, tr("Cuesta %d de oro"), stash_price(p));
            n = tip_add(tip, n, (int64_t)p->gold + p->stash_gold >= stash_price(p) ? COL(R_GRAY, 15) : TIP_RED, buf);
            n = tip_add(tip, n, COL(R_GRAY, 15), "Cada una cuesta el triple que la anterior");
            snprintf(buf, sizeof buf, tr("Se pueden comprar %d más"), STASH_BUY_MAX - p->stash_bought);
            n = tip_add(tip, n, COL(R_GRAY, 15), buf);
            n = tip_add(tip, n, COL(R_BLUE, 15), p->stash_confirm ? "A otra vez: comprarla" : "A: comprarla");
        }
    } else if (p->inv_sel == INV_GOLD_IN || p->inv_sel == INV_GOLD_OUT) {
        n = tip_add(tip, 0, COL(R_GOLD, 15), p->inv_sel == INV_GOLD_IN ? "Guardar todo el oro que llevás" : "Sacar todo el oro del arcón");
        n = tip_add(tip, n, COL(R_GRAY, 15), "Al morir se pierde parte del oro que llevás,");
        n = tip_add(tip, n, COL(R_GRAY, 15), "el del arcón no");
    } else if (p->inv_sel == INV_SORT) {
        n = tip_add(tip, 0, COL(R_GRAY, 15), "Acomoda la mochila (también con LB)");
    } else if (p->inv_sel == INV_STATS) {
        n = tip_add(tip, 0, COL(R_GOLD, 15), "Personaje");
        n = tip_add(tip, n, COL(R_GRAY, 15), "A: repartir los puntos de atributos");
        if (p->stat_points > 0) {
            char pts[48];
            snprintf(pts, sizeof pts, tr("%d punto%s sin repartir"), p->stat_points, p->stat_points == 1 ? "" : "s");
            n = tip_add(tip, n, COL(R_BLUE, 15), pts);
        }
    }
    draw_tip(tip, n, x, y, w, h, x0, x1);
}

/* ---- Personaje (atributos) ---- */

static const char *const k_attr_keys[4] = { "strchrstr", "strchrdex", "strchrvit", "strchreng" };
static const int k_attr_y[4] = { 83, 145, 231, 292 };

static void draw_stats_page(const Player *p)
{
    const CharClass *c = &s_classes[p->cls];
    int px = panel_x(p), py = PANEL_Y;
    char text[64];
    draw_panel_header(p, "Personaje", p->stat_points > 0 ? "A: sumar un punto   Y: inventario   B: cerrar"
                                                           : "Y: inventario   B: cerrar");
    ui_grid("panel/invchar6", 0, 4, 2, px, py);
    const uint8_t white = COL(R_GRAY, 15), gold = COL(R_GOLD, 14);

    text_in(px + 10, py + 9, 171, 15, s_classes[p->cls].title, 2, gold);
    text_in(px + 194, py + 9, 116, 15, str_get(s_classes[p->cls].name), 2, gold);
    text_in(px + 9, py + 31, 45, 12, str_get("strchrlvl"), 1, white);
    snprintf(text, sizeof text, "%d", p->level_num);
    text_in(px + 11, py + 45, 45, 20, text, 2, white);
    text_in(px + 65, py + 31, 116, 12, str_get("strchrexp"), 1, white);
    snprintf(text, sizeof text, "%u", (unsigned)p->exp);
    text_in(px + 66, py + 45, 116, 20, text, 2, white);
    text_in(px + 193, py + 31, 116, 12, str_get("strchrnxtlvl"), 1, white);
    snprintf(text, sizeof text, "%u", (unsigned)c->exp_next[p->level_num]);
    text_in(px + 194, py + 45, 116, 20, text, 2, white);

    /* Atributos; con puntos para repartir, el botón "+" al lado de cada uno */
    int base[4] = { player_str(p), player_dex(p), player_vit(p), player_energy(p) };
    for (int a = 0; a < 4; a++) {
        int y = py + k_attr_y[a];
        text_in(px + 10, y, 65, 17, str_get(k_attr_keys[a]), 2, white);
        snprintf(text, sizeof text, "%d", base[a]);
        text_in(px + 75, y, 40, 17, text, 2, white);
        if (p->stat_points > 0) {
            ui_frame("panel/levelsocket", 0, px + 122, y - 9);
            ui_frame("panel/level", a == p->stat_sel ? 1 : 0, px + 125, y - 7);
        }
        if (a == p->stat_sel)
            draw_frame(px + 8, y - 4, 112, 25, COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    }
    if (p->stat_points > 0) {
        ui_frame("panel/skillpoints", 0, px + 3, py + 341);
        text_in(px + 6, py + 341, 84, 14, "Puntos", 1, white);
        text_in(px + 6, py + 353, 84, 14, "restantes", 1, white);
        snprintf(text, sizeof text, "%d", p->stat_points);
        text_in(px + 91, py + 343, 40, 20, text, 2, white);
    }

    /* Daño, puntería, defensa, vigor, vida, maná y resistencias */
    int dmin, dmax;
    player_damage(p, &dmin, &dmax);
    text_in(px + 161, py + 83, 100, 20, "Daño", 2, white);
    snprintf(text, sizeof text, "%d-%d", dmin, dmax);
    text_in(px + 261, py + 81, 50, 20, text, 2, white);
    text_in(px + 161, py + 145, 110, 20, "Puntería", 2, white);
    snprintf(text, sizeof text, "%d", player_attack_rating(p));
    text_in(px + 271, py + 145, 40, 20, text, 2, white);
    text_in(px + 162, py + 192, 110, 20, str_get("strchrdef"), 2, white);
    snprintf(text, sizeof text, "%d", player_defense(p));
    text_in(px + 272, py + 191, 37, 20, text, 2, white);
    static const char *const keys[3] = { "strchrstm", "strchrlif", "strchrman" };
    float cur[3] = { p->stamina, (float)p->hp, (float)p->mana }, max[3] = { p->stamina_max, (float)p->hp_max, (float)p->mana_max };
    static const int ys[3] = { 231, 255, 293 };
    for (int k = 0; k < 3; k++) {
        text_in(px + 162, py + ys[k], 68, 20, str_get(keys[k]), 1, white);
        snprintf(text, sizeof text, "%.0f", max[k]);
        text_in(px + 232, py + ys[k] - 2, 40, 20, text, 2, white);
        snprintf(text, sizeof text, "%.0f", cur[k]);
        text_in(px + 272, py + ys[k] - 2, 40, 20, text, 2, white);
    }
    static const char *const res[4] = { "Res. al fuego", "Res. al frío", "Res. al rayo", "Res. al veneno" };
    static const int order[4] = { 0, 1, 2, 3 }; /* player_resist: fuego, frío, rayo, veneno */
    for (int k = 0; k < 4; k++) {
        int r = player_resist(p, order[k]);
        text_in(px + 175, py + 333 + k * 24, 95, 19, res[k], 1, white);
        snprintf(text, sizeof text, "%d%%", r);
        text_in(px + 271, py + 330 + k * 24, 42, 19, text, 2, r < 0 ? COL(R_RED, 13) : r >= 75 ? gold : white);
    }
}

/* ---- Árbol de habilidades (spells/skltree_X_back) ----
   Cuatro cuadros de base (la columna de las solapas) y cuatro por solapa:
   la de arriba es la tercera página, la del medio la segunda y la de abajo
   la primera. Íconos en las columnas 15, 84, 153 y las filas de
   skillstreepanel.json. Ataque y Lanzar van en un cuadrito al costado. */

static const int k_tree_col[3] = { 15, 84, 153 };
static const int k_tree_row[6] = { 14, 82, 150, 218, 287, 355 };

static const char *tree_file(int cls)
{
    return s_classes[cls].tree;
}

/* Al empezar la partida, los paneles y los íconos que se van a dibujar, en
   el hilo de trabajos (en la Pi, abrir los waypoints por primera vez eran
   18 ms en el hilo del juego, y los íconos de una clase de 8 a 11). Sin el
   hilo no: se leen al dibujarlos, como siempre. */
static void preload_ui(void)
{
    static const char *const k_panels[] = {
        CTRL_PANEL, "panel/hlthmana", "panel/overlap", "panel/menubutton", "panel/runbutton", "panel/level",
        "panel/levelsocket", "panel/skillpoints", "panel/invchar6", "panel/buysell", "panel/buysellbtn",
        "panel/buyselltabs", "panel/npcinv", "menu/waygatebackground", "menu/waygatetabs", "menu/waygateicons",
        "menu/questbackground", "menu/expquesttabs", "menu/questsockets", "menu/questlast", "menu/expwaygatetabs", "cursor/pentspin",
    };
    if (!jobs_running())
        return;
    for (unsigned k = 0; k < sizeof k_panels / sizeof k_panels[0]; k++)
        ui_preload(k_panels[k]);
    skill_icons_load(ICONS_GENERIC, 0);
    for (int i = 0; i < s_num_players; i++) {
        ui_preload(tree_file(s_pl[i].cls));
        skill_icons_load(s_pl[i].cls, 0);
    }
    for (unsigned k = 0; k < sizeof k_merc_act / sizeof k_merc_act[0]; k++)
        ui_preload(k_merc_act[k].icon);
    for (int a = 1; a <= QUEST_ACTS; a++)
        for (int q = 0; q < quest_slots(a); q++) {
            char icon[24];
            snprintf(icon, sizeof icon, "menu/a%dq%d", a, q + 1);
            ui_preload(icon);
        }
}

/* Cuadrito de Ataque y Lanzar: del lado de adentro de la pantalla */
static void generic_box(const Player *p, int *bx, int *by)
{
    int px = panel_x(p);
    *bx = px == 0 ? PANEL_W + 6 : px - 66;
    *by = PANEL_Y + 14;
}

/* Rectángulo de una entrada del árbol (0 si no se ve en esta solapa) */
static void tree_entry_rect(const Player *p, int page, int i, int *x, int *y, int *w, int *h)
{
    int n = skills_count(p->cls);
    *x = *y = *w = *h = 0;
    if (i >= n) {
        int bx, by;
        generic_box(p, &bx, &by);
        *x = bx + 6;
        *y = by + 8 + (i - n) * 64;
        *w = *h = 48;
        return;
    }
    const SkillDef *s = skills_get(p->cls, i);
    if (s->page != page)
        return;
    *x = panel_x(p) + k_tree_col[s->col - 1];
    *y = PANEL_Y + k_tree_row[s->row - 1];
    *w = *h = 48;
}


static void tree_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h)
{
    const TreeCtx *t = ctx;
    tree_entry_rect(t->p, t->page, i, x, y, w, h);
}

/* Descripción de la habilidad marcada, como el cartel del juego */
static int skill_tip(const Player *p, int sel, TipLine *l)
{
    int n = skills_count(p->cls), k = 0;
    char buf[96], eff[96];
    if (sel >= n) {
        k = tip_add(l, k, TIP_GOLD, sel == n ? "Ataque" : "Lanzar");
        k = tip_add(l, k, TIP_WHITE, sel == n ? "El ataque normal con el arma que tenés:" : "Lanza el arma que tenés en la mano");
        k = tip_add(l, k, TIP_WHITE, sel == n ? "cuerpo a cuerpo, o un disparo con arco" : "(jabalinas, cuchillos, hachas arrojadizas)");
        return k;
    }
    const SkillDef *s = skills_get(p->cls, sel);
    int lvl = p->skill_lvl[sel];
    k = tip_add(l, k, TIP_GOLD, s->name);
    /* La descripción del juego trae sus saltos de línea */
    const char *f = s->flavor;
    while (*f && k < TIP_MAX - 5) {
        size_t len = strcspn(f, "\n");
        snprintf(buf, sizeof buf, "%.*s", (int)(len < sizeof buf ? len : sizeof buf - 1), f);
        k = tip_add(l, k, TIP_WHITE, buf);
        f += len + (f[len] == '\n');
    }
    if (s->kind == SK_NONE)
        return tip_add(l, k, TIP_RED, "Todavía no está en este port");
    if (lvl > 0) {
        snprintf(buf, sizeof buf, tr("Nivel actual: %d"), lvl);
        k = tip_add(l, k, TIP_WHITE, buf);
        SkillCtx ctx = pctx(p);
        k = tip_add(l, k, TIP_WHITE, skill_effect_text(s, lvl, &ctx, eff, sizeof eff));
    }
    if (lvl < SKILL_MAX_LEVEL) {
        k = tip_add(l, k, TIP_BLUE, lvl > 0 ? "Siguiente nivel:" : "Primer nivel:");
        SkillCtx ctx = pctx(p);
        k = tip_add(l, k, TIP_BLUE, skill_effect_text(s, lvl + 1, &ctx, eff, sizeof eff));
    }
    if (!s->passive) {
        snprintf(buf, sizeof buf, tr("Costo de maná: %d"), skill_mana(s, lvl > 0 ? lvl : 1));
        k = tip_add(l, k, TIP_BLUE, buf);
    }
    if (!can_learn(p, sel, buf, sizeof buf))
        k = tip_add(l, k, TIP_RED, buf);
    for (int i = 0; i < k; i++) /* letra más chica para los textos largos */
        if (font_width(l[i].text, 2) > 360)
            l[i].scale = 1;
    return k;
}

/* El recuadro "Poner <habilidad> en:" con los cuatro botones y lo que tiene
   cada uno; el marcado, con el borde dorado */
static void draw_assign_box(const Player *p, int what)
{
    int px = panel_x(p), x = px + 30, y = PANEL_Y + 110, w = PANEL_W - 60, h = 40 + NUM_BUTTONS * 38 + 26;
    ui_box(x, y, w, h);
    char title[96];
    snprintf(title, sizeof title, tr("Poner %s en:"), button_skill_name(p, what));
    font_draw_centered(x + w / 2, y + 10, title, 2, COL(R_GOLD, 15));
    for (int b = 0; b < NUM_BUTTONS; b++) {
        int ry = y + 36 + b * 38;
        draw_button(p, b, x + 14, ry, 32);
        font_draw(x + 56, ry + 9, button_skill_name(p, p->buttons[b]), 2,
                  b == p->assign_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 14));
        if (b == p->assign_sel)
            draw_frame(x + 8, ry - 3, w - 16, 38, COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    }
    font_draw_centered(x + w / 2, y + h - 18, "Cruceta: elegir   A: poner   B: cancelar", 1, COL(R_STONE, 12));
}

/* Página Botones: los cuatro botones, y lo que se le puede poner al elegido */
static void draw_buttons_page(const Player *p)
{
    int px = panel_x(p), py = PANEL_Y;
    draw_panel_header(p, "Botones", p->btn_pick ? "Cruceta: elegir   A: ponerla   B: volver"
                                               : "A: cambiar   Y: personaje   B: cerrar");
    ui_box(px, py, PANEL_W, 432);
    font_draw_centered(px + PANEL_W / 2, py + 10, "Qué hace cada botón", 2, COL(R_GOLD, 15));
    for (int b = 0; b < NUM_BUTTONS; b++) {
        int ry = py + 40 + b * 52;
        draw_button(p, b, px + 16, ry, 44);
        font_draw(px + 70, ry + 14, button_skill_name(p, p->buttons[b]), 2,
                  b == p->btn_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 14));
        if (b == p->btn_sel)
            draw_frame(px + 10, ry - 4, PANEL_W - 20, 52, COL(R_GOLD, p->btn_pick || (s_ticks / 6) % 2 ? 15 : 11));
    }
    if (!p->btn_pick) {
        font_draw_centered(px + PANEL_W / 2, py + 260, "Elegí un botón y apretá A", 2, COL(R_STONE, 13));
        font_draw_centered(px + PANEL_W / 2, py + 284, "para cambiar lo que hace.", 2, COL(R_STONE, 13));
        font_draw_centered(px + PANEL_W / 2, py + 316, "También desde el árbol de", 1, COL(R_STONE, 12));
        font_draw_centered(px + PANEL_W / 2, py + 330, "habilidades: X en una habilidad.", 1, COL(R_STONE, 12));
        return;
    }
    /* La lista de lo que se puede poner (con desplazamiento si son muchas) */
    int cand[64], n = button_candidates(p, cand, 64), rows = 6;
    int first = p->btn_list_sel - rows / 2;
    first = first > n - rows ? n - rows : first;
    first = first < 0 ? 0 : first;
    int ly = py + 252;
    fb_fill_rect(px + 10, ly - 6, PANEL_W - 20, 1, COL(R_GOLD, 9));
    for (int k = first; k < n && k < first + rows; k++) {
        int ry = ly + (k - first) * 29;
        int what = cand[k], set = what == BTN_ATTACK ? ICONS_GENERIC : what == BTN_THROW ? ICONS_GENERIC : p->cls;
        int icon = what == BTN_ATTACK ? ICON_ATTACK : what == BTN_THROW ? ICON_THROW : skills_get(p->cls, what)->icon;
        draw_icon(set, icon, px + 20, ry, 26, LIGHT_LEVELS - 1);
        uint8_t c = k == p->btn_list_sel ? COL(R_GOLD, 15) : what == p->buttons[p->btn_sel] ? COL(R_GRASS, 14) : COL(R_GRAY, 14);
        font_draw(px + 54, ry + 6, button_skill_name(p, what), 2, c);
        if (k == p->btn_list_sel)
            draw_frame(px + 16, ry - 2, PANEL_W - 32, 30, COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    }
}

static void draw_skill_tree(const Player *p)
{
    int px = panel_x(p), py = PANEL_Y, x0, x1;
    half_range(p, &x0, &x1);
    int n = skills_count(p->cls);
    draw_panel_header(p, "Habilidades", p->assigning ? "Cruceta: elegir el botón   A: poner   B: cancelar"
                                                      : "A: aprender   X: poner en un botón   Y: botones   B: cerrar");
    if (n == 0)
        return;
    int sel = p->tree_sel >= 0 && p->tree_sel < tree_count(p->cls) ? p->tree_sel : 0;
    int page = p->tree_page >= 1 && p->tree_page <= 3 ? p->tree_page : 1;
    const char *file = tree_file(p->cls);
    ui_grid(file, 0, 4, 2, px, py);
    ui_grid(file, page == 3 ? 12 : page == 2 ? 8 : 4, 4, 2, px, py);

    /* Nombres de las solapas, de arriba hacia abajo: páginas 3, 2 y 1 */
    char text[96];
    for (int t = 0; t < 3; t++) {
        int tp = 3 - t, ty = py + 110 + t * 107;
        snprintf(text, sizeof text, "%s", skills_tab_name(p->cls, tp));
        /* (los del Resurrected en alemán traen su propio corte: "Kampf-\nfertigkeiten") */
        char *nl = strchr(text, '\n');
        int lines = nl || font_width(text, 2) > 86 ? 2 : 1;
        if (nl) {
            uint8_t c = tp == page ? COL(R_GOLD, 15) : COL(R_STONE, 11);
            *nl = '\0';
            font_draw_centered(px + 274, ty + 34, text, 2, c);
            font_draw_centered(px + 274, ty + 52, nl + 1, 2, c);
        } else if (lines == 1) {
            text_in(px + 227, ty, 94, 103, text, 2, tp == page ? COL(R_GOLD, 15) : COL(R_STONE, 11));
        } else { /* en dos renglones, cortando en el espacio del medio */
            char *mid = strchr(text + strlen(text) / 2 - 2, ' ');
            if (!mid)
                mid = strrchr(text, ' ');
            uint8_t c = tp == page ? COL(R_GOLD, 15) : COL(R_STONE, 11);
            if (!mid) { /* una palabra sola (en alemán): más chica */
                text_in(px + 227, ty, 94, 103, text, 1, c);
                continue;
            }
            *mid = '\0';
            font_draw_centered(px + 274, ty + 34, text, 2, c);
            font_draw_centered(px + 274, ty + 52, mid + 1, 2, c);
        }
    }
    text_in(px + 235, py + 8, 85, 26, "Puntos de", 1, COL(R_GRAY, 15));
    text_in(px + 235, py + 22, 85, 26, "habilidad", 1, COL(R_GRAY, 15));
    snprintf(text, sizeof text, "%d", p->skill_points);
    text_in(px + 260, py + 60, 28, 20, text, 2, p->skill_points > 0 ? COL(R_GOLD, 15) : COL(R_GRAY, 15));

    for (int i = 0; i < n; i++) {
        const SkillDef *s = skills_get(p->cls, i);
        if (s->page != page)
            continue;
        int x = px + k_tree_col[s->col - 1], y = py + k_tree_row[s->row - 1];
        char why[48];
        int lvl = p->skill_lvl[i];
        int light = lvl > 0 ? LIGHT_LEVELS - 1 : can_learn(p, i, why, sizeof why) ? 18 : 8;
        draw_icon(p->cls, s->icon, x, y, 48, light);
        if (lvl > 0) {
            snprintf(text, sizeof text, "%d", lvl);
            text_in(x + 41, y + 35, 23, 16, text, 2, COL(R_GRAY, 15));
        }
        for (int b = 0, k = 0; b < NUM_BUTTONS; b++)
            if (p->buttons[b] == i) {
                font_draw(x + 2 + k * 10, y + 1, k_button_names[b], 1, COL(k_button_ramp[b], 15));
                k++;
            }
    }

    /* Ataque y Lanzar */
    int bx, by;
    generic_box(p, &bx, &by);
    ui_box(bx, by, 60, 132);
    static const int icons[2] = { ICON_ATTACK, ICON_THROW };
    for (int k = 0; k < 2; k++) {
        int value = k == 0 ? BTN_ATTACK : BTN_THROW;
        draw_icon(ICONS_GENERIC, icons[k], bx + 6, by + 8 + k * 64, 48, LIGHT_LEVELS - 1);
        for (int b = 0, j = 0; b < NUM_BUTTONS; b++)
            if (p->buttons[b] == value) {
                font_draw(bx + 8 + j * 10, by + 9 + k * 64, k_button_names[b], 1, COL(k_button_ramp[b], 15));
                j++;
            }
    }

    int x, y, w, h;
    tree_entry_rect(p, page, sel, &x, &y, &w, &h);
    draw_frame(x - 2, y - 2, w + 4, h + 4, p->assigning ? COL(R_BLUE, 15) : COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    if (p->assigning)
        return; /* el recuadro de los botones va encima: sin la descripción */
    TipLine tip[TIP_MAX];
    int k = skill_tip(p, sel, tip);
    draw_tip(tip, k, x, y, w, h, x0, x1);
}

static void stats_tick(Player *p, const PlayerInput *pi)
{
    if (pi->button[3]) { /* Y: siguiente panel (la mercenaria, si tiene, y las misiones) */
        p->inv_page = p->merc_name ? PAGE_MERC : PAGE_QUESTS;
        p->qlog_act = 0;
        p->merc_pick = 0;
        return;
    }
    if (pi->back) {
        p->inv_open = 0;
        return;
    }
    if (pi->nav_up)
        p->stat_sel = (p->stat_sel + 3) % 4;
    if (pi->nav_down)
        p->stat_sel = (p->stat_sel + 1) % 4;
    if (pi->action) {
        if (p->stat_points <= 0) {
            show_message(p, "No te quedan puntos de atributo");
            return;
        }
        p->stat_add[p->stat_sel]++;
        p->stat_points--;
        refresh_stats(p);
        save_character(p);
    }
}

/* Texto con cortes de línea para que entre en `max_w` píxeles */
/* Parte un texto en renglones de `max_w` píxeles (los saltos de línea del
   texto se respetan). Devuelve cuántos renglones quedaron. */
static int wrap_text(const char *text, int max_w, int font, char lines[][128], int max_lines)
{
    int n = 0;
    const char *p = lang_screen(text); /* (en el idioma de la pantalla: se corta lo que se ve) */
    while (*p && n < max_lines) {
        /* hasta donde entra: en un espacio o, en chino, japonés y coreano, entre dos letras */
        int len = font_break(p, font, max_w);
        if (len > 127) { /* (sin cortar una letra en el medio) */
            len = 127;
            while (len > 0 && ((unsigned char)p[len] & 0xC0) == 0x80)
                len--;
        }
        snprintf(lines[n++], 128, "%.*s", len, p);
        p += len;
        while (*p == ' ')
            p++;
        if (*p == '\n')
            p++;
    }
    return n;
}

/* Menú del NPC: su nombre, las opciones y lo que dice */
/* Menú del NPC, como el del juego: su nombre, las opciones y lo que dice,
   en un cuadro oscuro arriba de su mitad de la pantalla */
static void draw_dialog(const Player *p)
{
    int x0, x1;
    half_range(p, &x0, &x1);
    int w = x1 - x0 - 60 > 440 ? 440 : x1 - x0 - 60, x = (x0 + x1) / 2 - w / 2, y = 50;
    int opts[8], n = npc_options(p, units_id(p->dialog_npc), opts);
    /* Lo que dice: los textos son largos, así que se ven TALK_LINES renglones
       que van subiendo solos, como en el original */
    static char lines[64][128];
    int total = p->talk_text ? wrap_text(p->talk_text, w - 40, FONT_FORMAL, lines, 64) : 0;
    int shown = total < TALK_LINES ? total : TALK_LINES, lh = font_height(FONT_FORMAL) + 2;
    int first = 0;
    if (total > TALK_LINES) {
        int per_line = TALK_LINE_TICKS;
        if (p->talk_voice > 0)
            per_line = (p->talk_voice - TALK_WAIT) / (total - TALK_LINES + 1);
        first = (p->talk_ticks - TALK_WAIT) / (per_line > 1 ? per_line : 1);
        first = first < 0 ? 0 : first > total - TALK_LINES ? total - TALK_LINES : first;
    }
    int h = 64 + n * 24 + (shown ? shown * lh + 12 : 0);
    ui_dark_box(x, y, w, h);
    font_draw_centered(x + w / 2, y + 10, units_display_name(p->dialog_npc), 3, COL(R_GOLD, 15));
    for (int i = 0; i < n; i++) {
        int chosen = i == p->dialog_sel;
        int ty = y + 50 + i * 24;
        char label[64];
        if (opts[i] == OPT_REVIVE) /* "Resucitar a Aliza: 187" */
            snprintf(label, sizeof label, str_get("hireresurrect2"), merc_name_of(p->merc_type, p->merc_name),
                     merc_revive_cost(p));
        else if (opts[i] == OPT_TRAVEL && strcmp(units_id(p->dialog_npc), "meshif1") == 0)
            snprintf(label, sizeof label, "%s", str_get("MeshifMenuEast"));
        else if (opts[i] == OPT_TRAVEL && strcmp(units_id(p->dialog_npc), "meshif2") == 0)
            snprintf(label, sizeof label, "%s", str_get("MeshifMenuWest"));
        else if (opts[i] == OPT_TRAVEL && s_act == 2) /* Warriv del Acto 2: de vuelta al campamento */
            snprintf(label, sizeof label, "%s", tr("Viajar al oeste"));
        else
            snprintf(label, sizeof label, "%s", k_option_names[opts[i]]);
        font_draw_centered(x + w / 2, ty, label, 2, chosen ? COL(R_GOLD, 15) : COL(R_GRAY, 15));
        if (chosen)
            draw_pentagram(x + w / 2 - font_width(label, 2) / 2 - 30, ty - 4, 22, 1);
    }
    for (int i = 0; i < shown; i++)
        font_draw(x + 20, y + 56 + n * 24 + i * lh, lines[first + i], FONT_FORMAL, COL(R_GRAY, 15));
    if (first + shown < total) /* sigue más abajo */
        font_draw_centered(x + w / 2, y + h - 12, "...", 1, COL(R_STONE, 12));
    font_draw_centered(x + w / 2, y + h + 4, "A: elegir   B: salir", 1, COL(R_STONE, 12));
}

/* Lista de mercenarios de Kashya o Greiz: nombre, habilidad (o aura), nivel
   y precio, y abajo la vida, defensa y daño del marcado */
static void draw_hire(const Player *p)
{
    int x0, x1;
    half_range(p, &x0, &x1);
    int w = x1 - x0 - 40 > 470 ? 470 : x1 - x0 - 40, x = (x0 + x1) / 2 - w / 2, y = 60, lh = 30;
    int h = 64 + p->num_offers * lh + 96;
    ui_dark_box(x, y, w, h);
    font_draw_centered(x + w / 2, y + 10, "Contratar", 3, COL(R_GOLD, 15));
    /* Columnas según lo que miden los textos (con dos jugadores hay menos lugar) */
    int name_w = 0, type_w = 0;
    for (int k = 0; k < p->num_offers; k++) {
        const MercOffer *o = &p->offers[k];
        int nw = font_width(merc_name_of(o->type, o->name), 2), tw = font_width(str_get(s_hire_type[o->type].desc), 2);
        name_w = nw > name_w ? nw : name_w;
        type_w = tw > type_w ? tw : type_w;
    }
    int type_x = x + 34 + name_w + 12, lvl_x = type_x + type_w + 12;
    for (int k = 0; k < p->num_offers; k++) {
        const MercOffer *o = &p->offers[k];
        int ty = y + 52 + k * lh, chosen = k == p->hire_sel;
        uint8_t c = chosen ? COL(R_GOLD, 15) : COL(R_GRAY, 15);
        char lvl[24], cost[24];
        snprintf(lvl, sizeof lvl, tr("Nv %d"), o->level);
        if (p->free_hire)
            snprintf(cost, sizeof cost, "%s", tr("Gratis"));
        else
            snprintf(cost, sizeof cost, "%d", merc_cost(o->type, o->level));
        font_draw(x + 34, ty, merc_name_of(o->type, o->name), 2, c);
        font_draw(type_x, ty, str_get(s_hire_type[o->type].desc), 2, c);
        font_draw(lvl_x, ty, lvl, 2, c);
        font_draw(x + w - 16 - font_width(cost, 2), ty, cost, 2, p->free_hire ? COL(R_GOLD, 15) : c);
        if (chosen)
            draw_pentagram(x + 8, ty - 4, 20, 1);
    }
    if (p->num_offers > 0) {
        const MercOffer *o = &p->offers[p->hire_sel < p->num_offers ? p->hire_sel : 0];
        MercStats ms;
        merc_stats(o->type, o->level, &ms);
        char line[96];
        int ty = y + 58 + p->num_offers * lh;
        snprintf(line, sizeof line, tr("Vida %d   Defensa %d   Daño %d-%d"), ms.hp_max, ms.defense, ms.dmin, ms.dmax);
        font_draw_centered(x + w / 2, ty, line, 2, COL(R_GRAY, 15));
        snprintf(line, sizeof line, tr("Tu oro: %d"), p->gold);
        font_draw_centered(x + w / 2, ty + 20, line, 2, COL(R_GOLD, 15));
        if (p->free_hire)
            font_draw_centered(x + w / 2, ty + 40, "La primera es gratis: tu recompensa", 2, COL(R_GOLD, 13));
        else if (p->merc_name && !p->merc_dead)
            font_draw_centered(x + w / 2, ty + 40, "Reemplaza al que tenés", 2, COL(R_STONE, 12));
    }
    font_draw_centered(x + w / 2, y + h + 4, "A: contratar   B: volver", 1, COL(R_STONE, 12));
}

/* Barra de vida: verde con más de la mitad, dorada con más de un cuarto, roja */
static void draw_life_bar(int x, int y, int w, int h, int hp, int max_hp)
{
    int fill = max_hp > 0 ? w * hp / max_hp : 0;
    uint8_t c = hp * 2 > max_hp ? COL(R_GRASS, 13) : hp * 4 > max_hp ? COL(R_GOLD, 13) : COL(R_RED, 13);
    fb_fill_rect(x, y, w, h, COL(R_GRAY, 0));
    fb_fill_rect(x, y, fill, h, c);
}

/* Retrato de la mercenaria con su vida, arriba del lado de su dueño */
static void draw_merc_portrait(const Player *p, int x, int y)
{
    if (!p->merc_name || p->merc_unit < 0)
        return;
    int hp, max_hp, w = 46;
    units_merc_health(p->merc_unit, &hp, &max_hp);
    ui_frame(MERC_ACT(p->merc_type).icon, 0, x, y);
    if (p->merc_dead) {
        fb_shade_rect(x, y, w, 41, 8);
        font_draw_centered(x + w / 2, y + 12, "X", 3, COL(R_RED, 13));
        return;
    }
    draw_life_bar(x, y + 43, w, 5, hp, max_hp);
}

/* Los compañeros de las otras máquinas (en red, hasta 8 en la partida): una fila chica por cada
   uno, arriba a la izquierda debajo del retrato del mercenario, con su color, su número, la clase,
   el nivel y una barra de vida finita. Ocupan poco a propósito: con la pantalla partida no tienen
   que tapar el juego (con 8 jugadores son 6 filas, unos 80 píxeles de alto) */
#define PARTY_ROW 13
#define PARTY_W   112
static void draw_party_list(void)
{
    int y = 64, x = 6;
    for (int i = 0; i < s_num_players; i++) {
        const Player *o = &s_pl[i];
        if (local_slot(o) >= 0)
            continue;
        int dead = o->state >= PS_DYING;
        fb_shade_rect(x, y, PARTY_W, PARTY_ROW - 1, 10);
        fb_fill_rect(x, y, 3, PARTY_ROW - 1, COL(o->label, 14)); /* su color (el de su nombre y del mapa) */
        char text[48];
        snprintf(text, sizeof text, "J%d %s %d", i + 1, game_class_name(o->cls), o->level_num);
        if (font_width(text, 1) > PARTY_W - 8) /* (los nombres largos de algunos idiomas: la abreviatura) */
            snprintf(text, sizeof text, "J%d %s %d", i + 1, s_classes[o->cls].abbr, o->level_num);
        font_draw(x + 5, y - 2, text, 1, dead ? COL(R_STONE, 9) : COL(R_GRAY, 15));
        if (!dead)
            draw_life_bar(x + 5, y + PARTY_ROW - 4, PARTY_W - 8, 2, o->hp, o->hp_max);
        y += PARTY_ROW;
    }
}

/* La vida de las invocaciones. En D2R se ven arriba a la izquierda; acá van
   en una fila justo arriba de los botones de su dueño: un casillero por
   habilidad con su ícono, cuántas hay y la barra de vida.
   shortcut: la barra de un grupo es la vida sumada de todas (no se ve cuál
   esqueleto está por caer); una barra por unidad si hace falta verlo. */
#define PET_ICON 24
#define PET_ROW  (PET_ICON + 5) /* el ícono, un píxel y la barra de 4 */

/* Desde x hacia la derecha; con `right`, terminando en x (el J2, contra su borde) */
static void draw_pet_bar(const Player *p, int x, int y, int right)
{
    PetGroup g[PET_GROUPS];
    int lost, n = pet_groups(p, g, &lost), step = PET_ICON + 2;
    if (right)
        x -= n * step - 2;
    for (int k = 0; k < n; k++, x += step) {
        draw_icon(p->cls, skills_get(p->cls, g[k].skill)->icon, x, y, PET_ICON, LIGHT_LEVELS - 1);
        if (g[k].count > 1) { /* cuántas, en la esquina sobre un fondo oscuro: los íconos son claros */
            char q[12];
            snprintf(q, sizeof q, "%d", g[k].count);
            int w = font_width(q, 1);
            ui_dark_box(x + PET_ICON - w - 3, y + 6, w + 3, 10); /* las cifras de la fuente chica: filas 7 a 14 */
            font_draw(x + PET_ICON - w - 1, y + 1, q, 1, COL(R_GRAY, 15));
        }
        draw_life_bar(x, y + PET_ICON + 1, PET_ICON, 4, g[k].hp, g[k].max);
    }
}

/* Tienda: el panel del vendedor del original (panel/buysell) con sus
   solapas, la grilla y el oro; el precio va en la descripción */
static void draw_shop_panel(Player *p)
{
    int px = shop_px(p), py = PANEL_Y, x0, x1, tab = p->shop_selling ? SHOP_SELL : p->shop_tab;
    half_range(p, &x0, &x1);
    draw_panel_header_at(px, units_display_name(p->dialog_npc),
                         p->gambling       ? "A: apostar   B: volver"
                         : p->shop_selling ? "LB/RB: solapas   B: volver"
                                           : "A: comprar   LB/RB: solapas   B: volver");
    ui_grid("panel/buysell", 0, 4, 2, px, py);
    if (p->gambling) {
        ui_frame("panel/buyselltabs", 0, px + 1, py + 1);
        text_in(px + 1, py + 2, 79, 24, "Apostar", 2, COL(R_GOLD, 15));
    }
    for (int t = 0; t < SHOP_TABS && !p->gambling; t++) {
        int tx = px + 1 + t * 80;
        ui_frame("panel/buyselltabs", t == tab ? t % 4 : 4 + t % 4, tx, py + 1);
        text_in(tx, py + 2, 79, 24, k_shop_tabs[t], 2, t == tab ? COL(R_GOLD, 15) : COL(R_STONE, 11));
    }

    Item *items[SHOP_LIST_MAX];
    int col[SHOP_LIST_MAX], row[SHOP_LIST_MAX];
    int n = shop_list(p, items, SHOP_LIST_MAX);
    shop_layout(items, n, col, row);
    for (int i = 0; i < n; i++) {
        if (col[i] < 0)
            continue;
        const ItemType *t = items[i]->type;
        int x = px + 16 + col[i] * CELL, y = py + 63 + row[i] * CELL, w, h;
        fb_blend_rect(x, y, t->inv_w * CELL, t->inv_h * CELL,
                      meets_reqs(p, t) ? COL(R_BLUE, 4) : COL(R_RED, 8), BLEND_50);
        items_inv_size(t, &w, &h);
        Item base = items_make(t); /* lo de la apuesta se ve como el objeto común */
        items_draw_item(p->gambling ? &base : items[i], x + (t->inv_w * CELL - w) / 2, y + (t->inv_h * CELL - h) / 2);
        if (!p->gambling)
            draw_stack_count(items[i], x, y, t->inv_w * CELL, t->inv_h * CELL);
    }
    char text[48];
    text_in(px + 21, py + 358, 55, 16, "Oro", 2, COL(R_GOLD, 14));
    snprintf(text, sizeof text, "%d", p->gold);
    text_in(px + 78, py + 358, 120, 16, text, 2, COL(R_GRAY, 15));

    if (n == 0) {
        text_in(px + 16, py + 180, 290, 20, p->gambling ? "No queda nada" : "No queda mercadería", 2, COL(R_STONE, 11));
        return;
    }
    if (p->shop_selling) /* (el cursor está en la mochila) */
        return;
    if (p->shop_sel >= n)
        p->shop_sel = n - 1;
    ShopCtx ctx = { px, py, items, col, row };
    int x, y, w, h;
    shop_rect_cb(&ctx, p->shop_sel, &x, &y, &w, &h);
    draw_frame(x - 1, y - 1, w + 2, h + 2, COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    TipLine tip[TIP_MAX];
    if (p->gambling) { /* no se sabe qué es: el objeto común y lo que cuesta */
        Item base = items_make(items[p->shop_sel]->type);
        int k = item_tip(p, &base, tip);
        int price = items_gamble_price(base.type, p->level_num);
        snprintf(text, sizeof text, tr("Apostar: %d"), price);
        k = tip_add(tip, k, p->gold < price ? TIP_RED : TIP_GOLD, text);
        draw_tip(tip, k, x, y, w, h, x0, x1);
        return;
    }
    int k = item_tip(p, items[p->shop_sel], tip);
    int price = items_price(items[p->shop_sel], 0);
    snprintf(text, sizeof text, "%s: %d", tr("Costo"), price);
    k = tip_add(tip, k, p->gold < price ? TIP_RED : TIP_GOLD, text);
    if (strcmp(items[p->shop_sel]->type->code, "tsc") == 0) {
        int fill = shop_fill_count(p, items[p->shop_sel]->type);
        snprintf(text, sizeof text, tr("X: llenar la pila (%d por %d)"), fill, fill * price);
        k = tip_add(tip, k, TIP_WHITE, text);
    }
    draw_tip(tip, k, x, y, w, h, x0, x1);
}

/* La tienda: con un solo jugador, su panel con el inventario al lado; con
   dos, uno u otro (el inventario mientras vende) */
static void draw_shop(Player *p)
{
    if (s_num_local == 1 || !p->shop_selling)
        draw_shop_panel(p);
    if (s_num_local == 1 || p->shop_selling)
        draw_inventory(p);
}

/* ---- Barra de control del juego (panel/800ctrlpnl7) ----
   En 800x600 son seis pedazos: la estatua del ángel con el orbe de vida, la
   barra del medio (estamina, experiencia, cinturón) y el demonio con el
   orbe de maná. Entre las estatuas y el medio quedan los dos huecos de 48
   píxeles de los botones de habilidad. Posiciones de hudpanel.json. */


/* Orbe que se vacía de arriba hacia abajo. `half`: -1 = solo la mitad
   izquierda, +1 = solo la derecha, 0 = entero */
static void draw_orb(int frame, int x, int y, float fill, int half)
{
    if (fill < 0.0f) fill = 0.0f;
    if (fill > 1.0f) fill = 1.0f;
    int top = ORB_SIZE - (int)lroundf(fill * ORB_SIZE);
    int sx = half > 0 ? ORB_SIZE / 2 : 0, w = half ? ORB_SIZE / 2 : ORB_SIZE;
    ui_frame_part("panel/hlthmana", frame, x + sx, y + top, sx, top, w, ORB_SIZE - top);
}

/* Ícono del botón con su letra en el color del botón del joystick */

static void draw_button(const Player *p, int b, int x, int y, int size)
{
    int what = p->buttons[b];
    const Item *ammo = NULL;
    fb_fill_rect(x, y, size, size, COL(R_GRAY, 0));
    if (what == BTN_ATTACK) {
        draw_icon(ICONS_GENERIC, ICON_ATTACK, x, y, size, LIGHT_LEVELS - 1);
        if (items_ammo_for(p->equip[SLOT_WEAPON].type))
            ammo = ammo_item((Player *)p, ATK_SHOOT);
    } else if (what == BTN_THROW) {
        int can = items_can_throw(p->equip[SLOT_WEAPON].type);
        draw_icon(ICONS_GENERIC, ICON_THROW, x, y, size, can ? LIGHT_LEVELS - 1 : 10);
        ammo = can ? &p->equip[SLOT_WEAPON] : NULL;
    } else if (what >= 0) {
        const SkillDef *s = skills_get(p->cls, what);
        int usable = s->aura || (p->mana >= skill_mana(s, p->skill_lvl[what]) && weapon_allows(p, s));
        draw_icon(p->cls, s->icon, x, y, size, usable ? LIGHT_LEVELS - 1 : 10);
        if (s->aura && p->aura == what) /* el aura puesta */
            draw_frame(x, y, size, size, COL(R_GOLD, 15));
        int kind = skill_attack_kind(s);
        if (kind != ATK_MELEE && (s->decquant || kind == ATK_THROW))
            ammo = ammo_item((Player *)p, kind);
    } else {
        draw_frame(x, y, size, size, COL(R_STONE, 5));
    }
    if (ammo && ammo->type) { /* flechas o jabalinas que quedan */
        char q[8];
        snprintf(q, sizeof q, "%d", ammo->amount);
        font_draw(x + size - font_width(q, 1) - 2, y + 2, q, 1, COL(R_GRAY, 15));
    }
    font_draw(x + 3, y + size - 11, k_button_names[b], 2, COL(k_button_ramp[b], 15));
}

/* Barra de estamina (amarilla; roja y titilando si se agotó) y la de
   experiencia arriba, como en el original */
static void draw_stamina_exp(const Player *p, int sx, int sy, int ex, int ey)
{
    int fill = (int)(102 * p->stamina / p->stamina_max + 0.5f);
    uint8_t c = p->exhausted ? COL(R_RED, (s_ticks / 5) % 2 ? 12 : 8)
              : p->stamina_boost_ticks > 0 ? COL(R_GRASS, 11) : COL(R_GOLD, 11);
    fb_fill_rect(sx, sy, fill, 18, c);
    fb_hline(sx, sx + fill - 1, sy, COL(R_GOLD, 14));
    const CharClass *cl = &s_classes[p->cls];
    long long lo = p->level_num > 1 ? cl->exp_next[p->level_num - 1] : 0, hi = cl->exp_next[p->level_num];
    int xp_w = hi > lo ? (int)(119LL * ((long long)p->exp - lo) / (hi - lo)) : 0;
    fb_fill_rect(ex, ey, xp_w < 0 ? 0 : xp_w > 119 ? 119 : xp_w, 2, COL(R_GRAY, 14));
}

/* Un triangulito relleno que apunta hacia `dir` (0 ◀, 1 ▲, 2 ▶, 3 ▼): la
   dirección de la cruceta que toma esa casilla del cinturón. Ocupa 4 x 7
   (o 7 x 4) con una sombra negra para que se lea sobre la poción. */
static void draw_arrow(int x, int y, int dir, uint8_t color)
{
    for (int pass = 0; pass < 2; pass++) {
        int sx = x + (pass == 0), sy = y + (pass == 0);
        uint8_t c = pass == 0 ? COL(R_GRAY, 0) : color;
        for (int k = 0; k < 4; k++) { /* desde la punta: filas de 1, 3, 5 y 7 */
            int across = 3 - k, len = 1 + 2 * k;
            if (dir == 0) fb_fill_rect(sx + k, sy + across, 1, len, c);
            if (dir == 2) fb_fill_rect(sx + 3 - k, sy + across, 1, len, c);
            if (dir == 1) fb_fill_rect(sx + across, sy + k, len, 1, c);
            if (dir == 3) fb_fill_rect(sx + across, sy + 3 - k, len, 1, c);
        }
    }
}

static void draw_belt_slots(const Player *p, int x, int y)
{
    for (int i = 0; i < BELT_SLOTS; i++) {
        int cx = x + i * BELT_STEP;
        if (p->belt[i].type) {
            items_draw_item(&p->belt[i], cx, y);
            if (is_potion(p->belt[i].type)) { /* los tragos que le quedan */
                char q[8];
                snprintf(q, sizeof q, "%d", p->belt[i].amount);
                font_draw(cx + 27 - font_width(q, 1), y + 18, q, 1, COL(R_GRAY, 15));
            }
        }
        draw_arrow(cx + 1, y + 1, i, COL(R_GOLD, p->belt[i].type ? 15 : 9));
        if (p->inv_open && p->inv_page == 0 && p->inv_sel == INV_BELT0 + i)
            draw_frame(cx - 1, y - 1, 30, 30, COL(R_GOLD, 15));
    }
}

/* El "+" de puntos de habilidad para repartir */
static void draw_skill_up(const Player *p, int x, int y)
{
    if (p->skill_points <= 0)
        return;
    ui_frame("panel/level", (s_ticks / 10) % 2, x, y);
}

/* Avisos cortos ("Inventario lleno", "No hay ningún cadáver cerca"), arriba
   de la barra. Van al final del HUD, encima de todo (los paneles y los
   números de los orbes los tapaban), sobre un fondo oscuro. Con dos
   jugadores, a la derecha de los botones de cada mitad: al centro quedaban
   debajo de los números del orbe. */
static void draw_player_messages(void)
{
    for (int i = 0; i < s_num_local; i++) {
        const Player *p = &s_pl[s_local[i]];
        if (p->message_ticks <= 0)
            continue;
        /* con dos jugadores, más abajo: con un panel abierto los botones se corren arriba del aviso */
        int w = font_width(p->message, 2), h = font_height(2), y = s_num_local == 1 ? PANEL_TOP - 24 : PANEL_TOP - 19;
        int cx = s_num_local == 1 ? SCREEN_W / 2 : i == 0 ? 130 + w / 2 : SCREEN_W - 130 - w / 2;
        ui_dark_box(cx - w / 2 - 6, y - 3, w + 12, h + 6);
        font_draw_centered(cx, y, p->message, 2, COL(R_RED, 13));
    }
}

/* Un número arriba de un orbe: más arriba que las garras de la estatua y
   sobre un fondo oscuro, así se lee aunque haya pasto o piedra detrás */
static void draw_orb_text(int cx, int y, const char *text)
{
    int w = font_width(text, 1), h = font_height(1);
    ui_dark_box(cx - w / 2 - 4, y - 2, w + 8, h + 4);
    font_draw_centered(cx, y, text, 1, COL(R_GRAY, 15));
}

static void draw_value(int cx, int y, int value, int max)
{
    char text[24];
    snprintf(text, sizeof text, "%d/%d", value, max);
    draw_orb_text(cx, y, text);
}

/* Un jugador: la barra original completa. Vida a la izquierda y maná a la
   derecha; A y B en los huecos, X e Y encima de ellos. */
static void draw_hud_single(const Player *p)
{
    fb_fill_rect(0, SCREEN_H - 48, SCREEN_W, 48, COL(R_GRAY, 0));
    ui_frame(CTRL_PANEL, 0, 0, PANEL_TOP);
    ui_frame(CTRL_PANEL, 1, 165, MIDDLE_TOP);
    ui_frame(CTRL_PANEL, 2, 293, MIDDLE_TOP);
    ui_frame(CTRL_PANEL, 3, 421, MIDDLE_TOP);
    ui_frame(CTRL_PANEL, 4, 549, MIDDLE_TOP);
    ui_frame(CTRL_PANEL, 5, 683, PANEL_TOP);

    draw_orb(p->poison_ticks > 0 ? 2 : 0, 29, PANEL_TOP + 11, (float)p->hp / p->hp_max, 0);
    ui_frame("panel/overlap", 0, 28, PANEL_TOP + 11);
    draw_orb(1, 689, PANEL_TOP + 11, p->mana_max > 0 ? (float)p->mana / p->mana_max : 0.0f, 0);
    ui_frame("panel/overlap", 1, 690, PANEL_TOP + 7);

    draw_stamina_exp(p, 273, PANEL_TOP + 77, 256, PANEL_TOP + 65);
    ui_frame("panel/runbutton", p->running ? 2 : 0, 255, PANEL_TOP + 74);
    ui_frame("panel/menubutton", 0, 392, PANEL_TOP + 64);
    draw_belt_slots(p, 422, PANEL_TOP + 64);
    draw_skill_up(p, 563, PANEL_TOP + 66);
    if (p->stat_points > 0)
        ui_frame("panel/level", (s_ticks / 10) % 2, 206, PANEL_TOP + 66);

    draw_button(p, 0, 117, PANEL_TOP + 56, SKILL_SIZE);
    draw_button(p, 2, 117, PANEL_TOP + 4, SKILL_SIZE);
    draw_button(p, 1, 635, PANEL_TOP + 56, SKILL_SIZE);
    draw_button(p, 3, 635, PANEL_TOP + 4, SKILL_SIZE);
    draw_pet_bar(p, 117, PANEL_TOP + 2 - PET_ROW, 0); /* arriba de X y A */
}

/* Dos jugadores: cada mitad lleva su estatua con un orbe partido (vida a la
   izquierda, maná a la derecha), la estamina y el cinturón (un recorte de
   la barra del medio) y los cuatro botones arriba de la estatua. */

static void draw_middle_strip(int x)
{
    static const int start[4] = { 0, 128, 256, 384 }, width[4] = { 128, 128, 128, 86 };
    for (int k = 0; k < 4; k++) {
        int a = start[k] > STRIP_FROM ? start[k] : STRIP_FROM;
        int b = start[k] + width[k] < STRIP_TO ? start[k] + width[k] : STRIP_TO;
        if (b > a)
            ui_frame_part(CTRL_PANEL, 1 + k, x + a - STRIP_FROM, MIDDLE_TOP, a - start[k], 0, b - a, 55);
    }
}

static void draw_hud_half(const Player *p, int right)
{
    int housing = right ? SCREEN_W - 117 : 0;
    int strip = right ? housing - (STRIP_TO - STRIP_FROM) : 117;
    int orb_x = right ? housing + 6 : 29;
    ui_frame(CTRL_PANEL, right ? 5 : 0, housing, PANEL_TOP);
    draw_middle_strip(strip);

    draw_orb(p->poison_ticks > 0 ? 2 : 0, orb_x, PANEL_TOP + 11, (float)p->hp / p->hp_max, -1);
    draw_orb(1, orb_x, PANEL_TOP + 11, p->mana_max > 0 ? (float)p->mana / p->mana_max : 0.0f, +1);
    ui_frame("panel/overlap", right ? 1 : 0, orb_x - (right ? -1 : 1), PANEL_TOP + (right ? 7 : 11));

    /* Posiciones de la barra del medio, corridas por el recorte */
    int dx = strip - STRIP_FROM;
    draw_stamina_exp(p, dx + 108, PANEL_TOP + 77, dx + STRIP_FROM + 2, PANEL_TOP + 65);
    ui_frame("panel/menubutton", 0, dx + 227, PANEL_TOP + 64);
    draw_belt_slots(p, dx + 257, PANEL_TOP + 64);
    font_draw(right ? housing - 20 : 121, PANEL_TOP + 44, right ? "J2" : "J1", 2, COL(p->label, 14));

    /* Botones en cuadrado, como en el joystick: X e Y arriba, A y B abajo */
    int size = 36, bx = right ? SCREEN_W - 2 * size - 12 : 8, by = PANEL_TOP - 2 * size - 22;
    /* Solo se corren si un panel del costado los taparía (los diálogos van arriba) */
    if (p->inv_open || p->wp_open || (p->dialog_npc >= 0 && p->shop_open))
        bx = right ? SCREEN_W - PANEL_W - 2 * size - 12 : PANEL_W + 8;
    draw_button(p, 2, bx, by, size);
    draw_button(p, 3, bx + size + 4, by, size);
    draw_button(p, 0, bx, by + size + 4, size);
    draw_button(p, 1, bx + size + 4, by + size + 4, size);
    draw_pet_bar(p, right ? bx + 2 * size + 4 : bx, by - 2 - PET_ROW, right);
    draw_skill_up(p, right ? bx - 34 : bx + 2 * size + 8, by + size + 8);
    if (p->stat_points > 0)
        ui_frame("panel/level", (s_ticks / 10) % 2, right ? bx - 34 : bx + 2 * size + 8, by);
}

/* ---- Lo que lo afecta (pedido del usuario) ----
   Las mejoras y los males que duran, con su ícono, arriba de la barra entre
   la experiencia y el cinturón (con dos jugadores, arriba de la franja de
   cada uno). El original no los muestra: son los íconos de la habilidad que
   lo hace (para lo que no es una habilidad, la que más se parece) o el de la
   poción; abajo, los segundos que le quedan, y titila al terminarse. */
typedef struct {
    int set, icon;    /* draw_icon (la clase o ICONS_GENERIC); set < 0: el objeto `item` */
    const char *item; /* las pociones: su ícono de la mochila */
    int ticks;        /* lo que le queda (-1: mientras dure, como las auras) */
    int bad;          /* un mal (veneno, frío, maldición): marco rojo */
} Effect;

#define EFFECTS_MAX  16
#define EFFECT_ICON  28
#define EFFECT_STEP  31

static int effect_idx(Effect *e, int n, int cls, int skill, int ticks, int bad)
{
    if (n >= EFFECTS_MAX || ticks == 0 || cls < 0 || cls >= NUM_CLASSES || skill < 0 || skill >= skills_count(cls))
        return n;
    e[n] = (Effect){ cls, skills_get(cls, skill)->icon, NULL, ticks, bad };
    return n + 1;
}

static int effect_skill(Effect *e, int n, int cls, const char *skill, int ticks, int bad)
{
    return ticks ? effect_idx(e, n, cls, skills_find(cls, skill), ticks, bad) : n;
}

static int effect_item(Effect *e, int n, const char *code, int ticks)
{
    if (n >= EFFECTS_MAX || ticks <= 0 || !items_find(code))
        return n;
    e[n] = (Effect){ -1, 0, code, ticks, 0 };
    return n + 1;
}

static int player_effects(const Player *p, Effect *e)
{
    enum { BAR = 0, AMA = 1, SOR = 2, PAL = 3, NEC = 4, ASS = 5, DRU = 6, WAR = 7 };
    int n = 0;
    /* Las mejoras de las habilidades (propias o de un compañero) */
    n = effect_skill(e, n, BAR, "Shout", p->shout_ticks, 0);
    n = effect_skill(e, n, BAR, "Battle Orders", p->bo_ticks, 0);
    n = effect_skill(e, n, BAR, "Battle Command", p->bc_ticks, 0);
    n = effect_skill(e, n, BAR, "Frenzy", p->frenzy_ticks, 0);
    n = effect_skill(e, n, BAR, "Berserk", p->berserk_ticks, 0);
    if (p->armor_ticks > 0)
        n = effect_idx(e, n, p->cls, p->armor_skill, p->armor_ticks, 0);
    if (p->es_ticks > 0)
        n = effect_idx(e, n, p->cls, p->es_skill, p->es_ticks, 0);
    n = effect_skill(e, n, SOR, "Enchant", p->enchant_ticks, 0);
    if (p->blaze_ticks > 0)
        n = effect_idx(e, n, p->cls, p->blaze_skill, p->blaze_ticks, 0);
    if (p->storm_ticks > 0)
        n = effect_idx(e, n, p->cls, p->storm_skill, p->storm_ticks, 0);
    if (p->hs_ticks > 0)
        n = effect_idx(e, n, PAL, p->hs_skill, p->hs_ticks, 0);
    for (int k = 0; k < p->au_got_n; k++) /* las auras que lo alcanzan (la suya y las de otros) */
        n = effect_idx(e, n, p->au_got_cls[k], p->au_got_skill[k], -1, 0);
    n = effect_skill(e, n, NEC, "Bone Armor", p->bone_armor > 0 ? -1 : 0, 0);
    n = effect_skill(e, n, ASS, "Burst of Speed", p->quick_ticks, 0);
    n = effect_skill(e, n, ASS, "Fade", p->fade_ticks, 0);
    n = effect_skill(e, n, ASS, "Venom", p->venom_ticks, 0);
    if (p->bshield_ticks > 0)
        n = effect_idx(e, n, ASS, p->bshield_skill, p->bshield_ticks, 0);
    if (p->form != FORM_NONE)
        n = effect_idx(e, n, DRU, p->form_skill, p->form_ticks > 0 ? p->form_ticks : -1, 0);
    n = effect_skill(e, n, DRU, "Cyclone Armor", p->cyclone > 0 ? -1 : 0, 0);
    if (p->hurr_ticks > 0)
        n = effect_idx(e, n, DRU, p->hurr_skill, p->hurr_ticks, 0);
    if (p->arma_ticks > 0)
        n = effect_idx(e, n, DRU, p->arma_skill, p->arma_ticks, 0);
    if (p->hex_ticks > 0)
        n = effect_idx(e, n, WAR, p->hex_skill, p->hex_ticks, 0);
    if (p->ward > 0)
        n = effect_idx(e, n, WAR, p->ward_skill, -1, 0);
    /* El santuario que dura: el ícono de la habilidad que hace algo parecido */
    if (p->shrine_ticks > 0) {
        static const struct { int code, cls; const char *skill; } k_shrine[] = {
            { 6, PAL, "Defiance" },        { 7, PAL, "Might" },       { 8, PAL, "Resist Fire" },
            { 9, PAL, "Resist Cold" },     { 10, PAL, "Resist Lightning" }, { 11, PAL, "Cleansing" },
            { 12, BAR, "Battle Command" }, { 13, SOR, "Warmth" },     { 14, PAL, "Vigor" },
            { 15, PAL, "Meditation" },
        };
        for (unsigned k = 0; k < sizeof k_shrine / sizeof k_shrine[0]; k++)
            if (k_shrine[k].code == p->shrine_kind)
                n = effect_skill(e, n, k_shrine[k].cls, k_shrine[k].skill, p->shrine_ticks, 0);
    }
    /* Las pociones que duran */
    n = effect_item(e, n, "hp3", p->regen_life_ticks);
    n = effect_item(e, n, "mp3", p->regen_mana_ticks);
    n = effect_item(e, n, "vps", p->stamina_boost_ticks);
    n = effect_item(e, n, "yps", p->antidote_ticks);
    n = effect_item(e, n, "wms", p->thaw_ticks);
    /* Los males */
    n = effect_skill(e, n, NEC, "Poison Nova", p->poison_ticks, 1);
    n = effect_skill(e, n, SOR, "Frost Nova", p->chill_ticks, 1);
    n = effect_skill(e, n, NEC, "Amplify Damage", p->amp_ticks, 1);
    n = effect_skill(e, n, PAL, "Holy Freeze", p->aura_slow > 0 ? -1 : 0, 1);
    n = effect_skill(e, n, PAL, "Conviction", p->conv_res < 0 || p->conv_def < 0 ? -1 : 0, 1);
    /* los del Acto 4: las maldiciones de los Caballeros del Olvido, el toque frío de Diablo y el abrazo de los Gropers */
    /* (y las de las súcubos: Defense Curse con el ícono de Convicción, Blood Mana con el del Gólem de sangre) */
    static const struct { int cls; const char *skill; } k_curses[A4C_COUNT] = {
        { 0, NULL }, { 0, NULL }, { NEC, "Weaken" }, { NEC, "Iron Maiden" }, { NEC, "Life Tap" },
        { NEC, "Decrepify" }, { NEC, "Lower Resist" }, { PAL, "Conviction" }, { NEC, "Blood Golem" },
    };
    if (p->curse > A4C_AMPLIFY && p->curse < A4C_COUNT)
        n = effect_skill(e, n, k_curses[p->curse].cls, k_curses[p->curse].skill, p->curse_ticks, 1);
    n = effect_skill(e, n, SOR, "Frozen Armor", p->frozen_ticks, 1);
    n = effect_skill(e, n, NEC, "Dim Vision", p->drain_ticks, 1);
    return n;
}

/* La fila de íconos centrada en `cx`, con la base en `y` (dos filas si no entran en `width`) */
static void draw_effects(const Player *p, int cx, int y, int width)
{
    Effect e[EFFECTS_MAX];
    int n = player_effects(p, e);
    if (n <= 0 || p->state >= PS_DEAD)
        return;
    int per_row = width / EFFECT_STEP > 0 ? width / EFFECT_STEP : 1;
    for (int k = 0; k < n; k++) {
        int row = k / per_row, in_row = n - row * per_row < per_row ? n - row * per_row : per_row;
        int x = cx - (in_row * EFFECT_STEP - (EFFECT_STEP - EFFECT_ICON)) / 2 + (k % per_row) * EFFECT_STEP;
        int top = y - EFFECT_ICON - row * (EFFECT_ICON + 4);
        if (e[k].ticks > 0 && e[k].ticks < 3 * TICK_RATE && (s_anim_ticks / 6) % 2) /* se está por terminar */
            continue;
        fb_fill_rect(x, top, EFFECT_ICON, EFFECT_ICON, COL(R_GRAY, 0));
        if (e[k].set >= 0)
            draw_icon(e[k].set, e[k].icon, x, top, EFFECT_ICON, LIGHT_LEVELS - 1);
        else
            items_draw_inv(items_find(e[k].item), x, top);
        draw_frame(x - 1, top - 1, EFFECT_ICON + 2, EFFECT_ICON + 2, e[k].bad ? COL(R_RED, 12) : COL(R_GOLD, 11));
        if (e[k].ticks > 0) {
            char t[12];
            snprintf(t, sizeof t, "%d", (e[k].ticks + TICK_RATE - 1) / TICK_RATE);
            int tw = font_width(t, 1);
            fb_blend_rect(x + EFFECT_ICON - tw - 3, top + EFFECT_ICON - 11, tw + 2, 10, COL(R_GRAY, 0), BLEND_50);
            font_draw(x + EFFECT_ICON - tw - 2, top + EFFECT_ICON - 11, t, 1, COL(R_GRAY, 15));
        }
    }
}

/* ---- Equipo de la mercenaria ----
   Con el joystick no hace falta arrastrar nada: se marca un lugar, A muestra
   lo de la mochila que va ahí y A se lo pone (lo que tenía vuelve a la
   mochila); X se lo saca. */

/* Lugares del panel npcinv (hirelinginventorypanel.json) */
static const int k_merc_slot_rect[MERC_SLOTS][4] = {
    [MSLOT_WEAPON] = { 20, 45, 55, 112 }, [MSLOT_HEAD] = { 135, 5, 54, 51 },
    [MSLOT_TORSO] = { 133, 77, 56, 82 },  [MSLOT_SHIELD] = { 250, 45, 55, 112 },
};

static void merc_slot_rect_cb(const void *ctx, int i, int *x, int *y, int *w, int *h)
{
    const Player *p = ctx;
    *x = panel_x(p) + k_merc_slot_rect[i][0];
    *y = PANEL_Y + k_merc_slot_rect[i][1];
    *w = k_merc_slot_rect[i][2];
    *h = k_merc_slot_rect[i][3];
}

/* Los objetos de la mochila que van en ese lugar (los pueda usar o no) */
static int merc_candidates(const Player *p, int slot, int *out)
{
    int n = 0;
    for (int i = 0; i < p->bag_count; i++)
        if (merc_slot_fits(p, slot, p->bag[i].type))
            out[n++] = i;
    return n;
}

static int merc_current_hp(const Player *p)
{
    int hp = -1, max_hp;
    if (units_merc_alive(p->merc_unit))
        units_merc_health(p->merc_unit, &hp, &max_hp);
    return hp;
}

static void merc_thanks(const Player *p)
{
    float mx, my;
    units_position(p->merc_unit, 1.0f, &mx, &my);
    audio_play_name(MERC_ACT(p->merc_type).thanks, mx, my); /* "Gracias" */
}

/* Le pone el objeto `k` de la mochila; lo que tenía vuelve a la mochila */
static void merc_equip_from_bag(Player *p, int slot, int k)
{
    Item it = p->bag[k];
    const char *why;
    if (!merc_can_use(p, it.type, &why)) {
        show_message(p, why);
        return;
    }
    int hp = merc_current_hp(p);
    bag_remove(p, k);
    Item old = p->merc_equip[slot];
    p->merc_equip[slot] = it;
    if (old.type && !bag_add(p, &old)) { /* lo viejo no entra: se deshace */
        p->merc_equip[slot] = old;
        bag_add(p, &it);
        inventory_full(p);
        return;
    }
    audio_play(it.type->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
    merc_thanks(p);
    p->merc_pick = 0;
    merc_apply(p, hp);
    save_character(p);
}

static void merc_page_tick(Player *p, const PlayerInput *pi)
{
    if (!p->merc_name) {
        p->inv_page = PAGE_INVENTORY;
        return;
    }
    if (p->merc_sel < 0 || p->merc_sel >= MERC_SLOTS)
        p->merc_sel = 0;
    if (p->merc_pick) { /* la lista de la mochila */
        int list[BAG_MAX], n = merc_candidates(p, p->merc_sel, list);
        if (pi->back || n == 0) {
            p->merc_pick = 0;
            return;
        }
        if (pi->nav_up)
            p->pick_sel = (p->pick_sel + n - 1) % n;
        if (pi->nav_down)
            p->pick_sel = (p->pick_sel + 1) % n;
        if (p->pick_sel >= n)
            p->pick_sel = n - 1;
        if (pi->action)
            merc_equip_from_bag(p, p->merc_sel, list[p->pick_sel]);
        return;
    }
    if (pi->button[3]) { /* Y: siguiente panel */
        p->inv_page = PAGE_QUESTS;
        p->qlog_act = 0;
        return;
    }
    if (pi->back) {
        p->inv_open = 0;
        return;
    }
    int dx = pi->nav_right - pi->nav_left, dy = pi->nav_down - pi->nav_up;
    if (dx || dy)
        p->merc_sel = nav_nearest(MERC_SLOTS, p->merc_sel, dx, dy, merc_slot_rect_cb, p);
    if (!pi->action && !pi->drop)
        return;
    if (p->merc_sel == MSLOT_SHIELD) {
        show_message(p, "La pícara no usa escudo");
        return;
    }
    if (p->merc_dead) {
        show_message(p, "Está muerta: resucitala con Kashya");
        return;
    }
    if (pi->action) { /* A: elegir de la mochila */
        int list[BAG_MAX];
        if (merc_candidates(p, p->merc_sel, list) == 0) {
            show_message(p, "No tenés nada para ponerle ahí");
            return;
        }
        p->merc_pick = 1;
        p->pick_sel = 0;
        return;
    }
    Item *it = &p->merc_equip[p->merc_sel]; /* X: sacárselo */
    if (!it->type)
        return;
    int hp = merc_current_hp(p);
    if (!bag_add(p, it)) {
        inventory_full(p);
        return;
    }
    audio_play(it->type->drop_sound, AUDIO_NOPOS, AUDIO_NOPOS);
    memset(it, 0, sizeof *it);
    merc_apply(p, hp);
    save_character(p);
}

/* El panel de la mercenaria del original: su equipo, vida, nivel, fuerza,
   destreza, daño, defensa y resistencias; con la lista de la mochila
   encima cuando se elige qué ponerle */
static void draw_merc_page(const Player *p)
{
    int px = panel_x(p), py = PANEL_Y, x0, x1;
    half_range(p, &x0, &x1);
    char text[96];
    snprintf(text, sizeof text, "%s: %s", MERC_ACT(p->merc_type).title, merc_name_of(p->merc_type, p->merc_name));
    draw_panel_header(p, text, p->merc_pick ? "A: ponérselo   B: volver"
                                            : "A: elegir de la mochila   X: sacar   Y: inventario");
    ui_grid("panel/npcinv", 0, 4, 2, px, py);
    MercStats ms;
    merc_stats_gear(p, &ms);
    uint8_t white = COL(R_GRAY, 15), gold = COL(R_GOLD, 15);

    /* Equipo; el escudo no lo usa */
    for (int sl = 0; sl < MERC_SLOTS; sl++) {
        int x, y, w, h;
        merc_slot_rect_cb(p, sl, &x, &y, &w, &h);
        const Item *it = &p->merc_equip[sl];
        const char *why;
        if (sl == MSLOT_SHIELD)
            fb_shade_rect(x, y, w, h, 8);
        if (!it->type)
            continue;
        if (!merc_can_use(p, it->type, &why))
            fb_blend_rect(x, y, w, h, COL(R_RED, 8), BLEND_50);
        int iw, ih;
        items_inv_size(it->type, &iw, &ih);
        items_draw_item(it, x + (w - iw) / 2, y + (h - ih) / 2);
    }

    /* Datos */
    int hp = ms.hp_max, max_hp = ms.hp_max;
    if (p->merc_unit >= 0)
        units_merc_health(p->merc_unit, &hp, &max_hp);
    if (p->merc_dead)
        hp = 0;
    text_in(px + 3, py + 197, 154, 20, merc_name_of(p->merc_type, p->merc_name), 2, gold);
    snprintf(text, sizeof text, tr("Vida: %d / %d"), hp, max_hp);
    text_in(px + 161, py + 197, 155, 20, text, 2, p->merc_dead ? COL(R_RED, 13) : white);
    text_in(px + 5, py + 222, 124, 35, str_get(s_hire_type[p->merc_type].desc), 2, white);
    snprintf(text, sizeof text, tr("Nivel %d"), p->merc_level);
    text_in(px + 134, py + 222, 52, 35, text, 1, white);
    snprintf(text, sizeof text, tr("Puntería %d"), ms.ar);
    text_in(px + 190, py + 222, 125, 35, text, 2, white);
    static const char *const left[4] = { "strchrstr", "strchrdex", NULL, "strchrdef" };
    static const char *const right[4] = { "Res. fuego", "Res. frío", "Res. rayo", "Res. veneno" };
    for (int r = 0; r < 4; r++) {
        int y = py + 265 + r * 24;
        text_in(px + 5, y, 100, 18, left[r] ? str_get(left[r]) : "Daño", 1, white);
        if (r == 0) snprintf(text, sizeof text, "%d", ms.str);
        else if (r == 1) snprintf(text, sizeof text, "%d", ms.dex);
        else if (r == 2) snprintf(text, sizeof text, "%d-%d", ms.dmin, ms.dmax);
        else snprintf(text, sizeof text, "%d", ms.defense);
        text_in(px + 106, y, 51, 18, text, 2, white);
        text_in(px + 163, y, 99, 18, right[r], 1, white);
        snprintf(text, sizeof text, "%d%%", ms.resist);
        text_in(px + 263, y, 52, 18, text, 2, white);
    }
    if (p->merc_dead)
        text_in(px + 5, py + 372, PANEL_W - 10, 20, "Muerta: resucitala con Kashya", 2, COL(R_RED, 13));

    /* Cursor y descripción */
    int x, y, w, h;
    merc_slot_rect_cb(p, p->merc_sel, &x, &y, &w, &h);
    draw_frame(x - 1, y - 1, w + 2, h + 2, COL(R_GOLD, (s_ticks / 6) % 2 ? 15 : 11));
    TipLine tip[TIP_MAX];
    int n = 0;
    if (!p->merc_pick) {
        const Item *it = &p->merc_equip[p->merc_sel];
        static const char *const names[MERC_SLOTS] = { "Arco", "Casco", "Armadura", "Escudo (la pícara no usa)" };
        if (it->type)
            n = item_tip_req(it, tip, ms.str, ms.dex, p->merc_level);
        else
            n = tip_add(tip, 0, COL(R_STONE, 12), names[p->merc_sel]);
        draw_tip(tip, n, x, y, w, h, x0, x1);
        return;
    }

    /* La lista de la mochila, arriba de los datos */
    int list[BAG_MAX], count = merc_candidates(p, p->merc_sel, list), rows = 8, lh = 24;
    int bx = px + 6, by = py + 194, bw = PANEL_W - 12, bh = 30 + rows * lh;
    fb_fill_rect(bx, by, bw, bh, COL(R_GRAY, 0)); /* tapa los datos de abajo */
    draw_frame(bx, by, bw, bh, COL(R_GOLD, 11));
    font_draw_centered(px + PANEL_W / 2, by + 6, "Elegí qué ponerle", 2, gold);
    int first = p->pick_sel - rows / 2;
    first = first > count - rows ? count - rows : first;
    first = first < 0 ? 0 : first;
    for (int k = first; k < count && k < first + rows; k++) {
        const Item *it = &p->bag[list[k]];
        const char *why;
        char name[64];
        int ry = by + 28 + (k - first) * lh;
        uint8_t c = merc_can_use(p, it->type, &why) ? items_label_color(it) : COL(R_RED, 13);
        font_draw(bx + 30, ry, items_label(it, name, sizeof name), 2, c);
        if (k == p->pick_sel) {
            draw_pentagram(bx + 6, ry - 3, 20, 1);
            n = item_tip_req(it, tip, ms.str, ms.dex, p->merc_level);
            if (!merc_can_use(p, it->type, &why))
                n = tip_add(tip, n, TIP_RED, why);
            draw_tip(tip, n, bx, by, bw, bh, x0, x1); /* arriba de la lista, sobre el equipo */
        }
    }
}

static void draw_panel_page(const Player *p)
{
    if (p->inv_page == PAGE_SKILLS) {
        draw_skill_tree(p);
        if (p->assigning) {
            int sel = p->tree_sel >= 0 && p->tree_sel < tree_count(p->cls) ? p->tree_sel : 0;
            draw_assign_box(p, tree_button_value(p->cls, sel));
        }
    }
    else if (p->inv_page == PAGE_BUTTONS)
        draw_buttons_page(p);
    else if (p->inv_page == PAGE_STATS)
        draw_stats_page(p);
    else if (p->inv_page == PAGE_MERC)
        draw_merc_page(p);
    else if (p->inv_page == PAGE_QUESTS)
        draw_quest_page(p);
    else
        draw_inventory(p);
}

/* Panel de waypoints del original (menu/waygatebackground): la solapa del
   Acto I y los nueve waypoints; los activados con el portal azul, el de
   donde está con el gris y los que faltan apagados */
static void draw_waypoints(const Player *p)
{
    int px = panel_x(p), py = PANEL_Y, here = waypoint_near(p), tab = wp_act(p->wp_sel) - 1;
    ui_grid("menu/waygatebackground", 0, 4, 2, px, py);
    /* Las solapas de los cinco actos (las de la expansión, waypointspanelexpansion.json): la elegida adelante */
    for (int t = WP_ACTS - 1; t >= 0; t--)
        if (t != tab)
            ui_frame("menu/expwaygatetabs", t * 2 + 1, px + 5 + t * 62, py + 3);
    ui_frame("menu/expwaygatetabs", tab * 2, px + 5 + tab * 62, py + 3);
    font_draw_centered(px + PANEL_W / 2, py + 33, str_get("waypointsheader"), 2, COL(R_GOLD, 15));
    for (int k = k_wp_first[tab]; k < k_wp_first[tab + 1]; k++) {
        int by = py + 61 + (k - k_wp_first[tab]) * 36;
        int active = (p->waypoints >> k) & 1 && (wp_act(k) != s_act || world_waypoint_zone(k) >= 0);
        int icon = k == here ? 2 : active ? (k == p->wp_sel ? 1 : 0) : 3;
        ui_frame("menu/waygateicons", icon, px + 17, by);
        uint8_t color = k == here ? COL(R_BLUE, 15) : active ? COL(R_GRAY, 15) : COL(R_STONE, 9);
        if (active && k != here && tz_waypoint_marked(k_wp_levels[k])) /* el más cerca de la zona de terror */
            color = g_text_shift[11][COL(R_GRAY, 15)];
        font_draw(px + 17 + 44, by - 6, str_get(k_wp_levels[k]), FONT_FORMAL, color);
        if (k == p->wp_sel)
            draw_pentagram(px + 17 + 52 + font_width(str_get(k_wp_levels[k]), FONT_FORMAL), by + 3, 20, 1);
    }
    ui_frame("panel/buysellbtn", 10, px + 272, py + 386); /* cerrar (B) */
    font_draw(px + 24, py + 396, wp_act_open(p, 2) || wp_act_open(p, 3) || wp_act_open(p, 4) ? "A: viajar   B: cerrar   <>: acto" : "A: viajar   B: cerrar", 1,
              COL(R_STONE, 12));
}

/* Paneles abiertos y barra de control */
/* Primero la barra de control y los botones de habilidad; arriba de todo,
   los paneles (inventario, árbol, tienda) con sus descripciones */
/* Los números de vida y maná arriba de los orbes. Van al final, encima de
   todo: con el inventario o una tienda abiertos, el panel los tapaba. */
static void draw_orb_numbers(void)
{
    if (s_num_local == 1) {
        const Player *p = &s_pl[s_local[0]];
        draw_value(69, PANEL_TOP - 16, p->hp, p->hp_max);
        draw_value(729, PANEL_TOP - 16, p->mana, p->mana_max);
        return;
    }
    for (int right = 0; right < 2; right++) {
        const Player *p = &s_pl[s_local[right]];
        int orb_x = right ? SCREEN_W - 117 + 6 : 29;
        char text[24];
        snprintf(text, sizeof text, "%d / %d", p->hp, p->mana);
        draw_orb_text(orb_x + ORB_SIZE / 2, PANEL_TOP - 16, text);
    }
}

static void draw_hud_body(void)
{
    const Player *p1 = &s_pl[s_local[0]], *p2 = &s_pl[s_local[s_num_local > 1 ? 1 : 0]];
    draw_target_bar();
    if (s_num_local == 1) {
        draw_hud_single(p1);
        draw_effects(p1, SCREEN_W / 2, MIDDLE_TOP - 3, 683 - 165);
    } else {
        fb_fill_rect(0, SCREEN_H - 48, SCREEN_W, 48, COL(R_GRAY, 0));
        draw_hud_half(p1, 0);
        draw_hud_half(p2, 1);
        int half = (STRIP_TO - STRIP_FROM) / 2; /* arriba de la franja del medio de cada uno */
        draw_effects(p1, 117 + half, MIDDLE_TOP - 3, STRIP_TO - STRIP_FROM);
        draw_effects(p2, SCREEN_W - 117 - half, MIDDLE_TOP - 3, STRIP_TO - STRIP_FROM);
    }
    draw_merc_portrait(p1, 10, 10);
    if (s_num_local > 1)
        draw_merc_portrait(p2, SCREEN_W - 56, 10);
    if (s_num_players > s_num_local)
        draw_party_list();
    for (int k = 0; k < s_num_local; k++) { /* (los paneles de los de esta máquina) */
        Player *pk = &s_pl[s_local[k]];
        if (pk->dialog_npc >= 0 && pk->shop_open)
            draw_shop(pk);
        else if (pk->dialog_npc >= 0 && pk->hire_open)
            draw_hire(pk);
        else if (pk->dialog_npc >= 0 && pk->imbue_open)
            draw_imbue(pk);
        else if (pk->reading)
            draw_reading(pk);
        else if (pk->dialog_npc >= 0)
            draw_dialog(pk);
        else if (pk->wp_open)
            draw_waypoints(pk);
        else if (pk->inv_open)
            draw_panel_page(pk);
        else if (pk->chat_open)
            draw_chat(pk);
    }
    draw_orb_numbers();
    draw_player_messages();
}

static void draw_hud(void)
{
    double t0 = wall_ms();
    draw_hud_body();
    s_part_ms[2] += wall_ms() - t0;
}

/* Nombres de los objetos cercanos a algún jugador. Van después de la
   oscuridad de las cuevas: en el original siempre se leen. */
/* D2_COLL_DEBUG: la colisión de cada subtile cerca de la cámara, un rombo por
   subtile: rojo pared que frena proyectiles, amarillo solo no se camina (agua,
   cosas bajas), naranja solo frena proyectiles, celeste solo tapa la vista,
   azul puerta, violeta objeto, verde salida o camino de pieza prearmada,
   gris el grosor de una pared (COLL_VOLUME) */
static void draw_coll_debug(float cam_x, float cam_y)
{
    static int on = -1;
    if (on < 0)
        on = getenv("D2_COLL_DEBUG") != NULL;
    if (!on)
        return;
    int ox = SCREEN_W / 2 - (int)lroundf(cam_x), oy = SCREEN_H / 2 - (int)lroundf(cam_y);
    float wx, wy;
    screen_dir_to_world(cam_x, cam_y, &wx, &wy); /* (es lineal: sirve para el punto) */
    for (int y = (int)wy - 28; y <= (int)wy + 28; y++)
        for (int x = (int)wx - 28; x <= (int)wx + 28; x++) {
            uint16_t c = map_coll(&s_map, x, y);
            if (!c || (c & COLL_BLANK))
                continue;
            uint8_t col = c & COLL_DOOR                                 ? COL(R_BLUE, 12)
                        : c & COLL_OBJECT                               ? COL(R_CLOTH, 12)
                        : (c & COLL_WALL) && (c & COLL_MISSILE_BARRIER) ? COL(R_RED, 11)
                        : c & COLL_WALL                                 ? COL(R_GOLD, 14)
                        : c & COLL_MISSILE_BARRIER                      ? COL(R_WOOD, 12)
                        : c & COLL_VISIBLE                              ? COL(R_BLUE, 15)
                        : c == COLL_VOLUME                              ? COL(R_GRAY, 13)
                                                                        : COL(R_GRASS, 12);
            float sx, sy;
            iso_to_screen((float)x + 0.5f, (float)y + 0.5f, &sx, &sy);
            fb_diamond(ox + (int)lroundf(sx), oy + (int)lroundf(sy), 5, 3, col, col);
        }
}

static void draw_item_labels(float cam_x, float cam_y)
{
    int ox = SCREEN_W / 2 - (int)lroundf(cam_x);
    int oy = SCREEN_H / 2 - (int)lroundf(cam_y);
    /* Si un cartel se pisa con otro sube hasta quedar libre, como en el original */
    enum { MAX_LABELS = 48 };
    struct { int x, y, w, h, item; uint8_t color; char text[48]; } labels[MAX_LABELS];
    int nl = 0;
    for (int i = 0; i < items_ground_count() && nl < MAX_LABELS; i++) {
        float ix, iy, sx, sy;
        if (!items_ground_item(i, &ix, &iy))
            continue;
        int near = 0;
        for (int k = 0; k < s_num_players; k++) {
            float dx = s_pl[k].x - ix, dy = s_pl[k].y - iy;
            near |= dx * dx + dy * dy < 14.0f * 14.0f;
        }
        if (!near || !items_ground_label(i, labels[nl].text, sizeof labels[nl].text, &labels[nl].color))
            continue;
        iso_to_screen(ix, iy, &sx, &sy);
        labels[nl].w = font_width(labels[nl].text, 2) + 6;
        labels[nl].h = font_height(2) + 2;
        labels[nl].x = ox + (int)lroundf(sx) - labels[nl].w / 2;
        labels[nl].y = oy + (int)lroundf(sy) - 30;
        labels[nl].item = i;
        nl++;
    }
    for (int i = 0; i < nl; i++) {
        for (int moved = 1, tries = 0; moved && tries < 20; tries++) {
            moved = 0;
            for (int k = 0; k < i; k++)
                if (labels[i].x < labels[k].x + labels[k].w && labels[k].x < labels[i].x + labels[i].w &&
                    labels[i].y < labels[k].y + labels[k].h && labels[k].y < labels[i].y + labels[i].h) {
                    labels[i].y = labels[k].y - labels[i].h - 1;
                    moved = 1;
                }
        }
        /* Como en el juego: el nombre sobre un cuadro negro translúcido */
        fb_blend_rect(labels[i].x, labels[i].y, labels[i].w, labels[i].h, COL(R_GRAY, 0), BLEND_25);
        font_draw(labels[i].x + 3, labels[i].y + 1, labels[i].text, 2, labels[i].color);
    }

}

/* Arriba de lo que se puede usar al lado de un jugador (un NPC o un
   waypoint), con qué botón: "A: Hablar con Kashya". No sale si hay un
   objeto para levantar (ese tiene su cartel) o un panel abierto. */
/* Salta a la otra zona por el salto o el portal (si no estaba armada, se arma) */
static void use_warp(Player *p, const Warp *warp)
{
    if (s_eco) /* (la arena no tiene salidas) */
        return;
    Warp w = *warp; /* armar la otra zona puede mover la lista de saltos */
    float ax, ay;
    /* La puerta del palacio: Kaelan no deja pasar hasta que se abre (ACT2Q4: bPalaceOpen) */
    if (s_act == 2 && w.to == world_level_zone(50) && !palace_open()) {
        static uint32_t said[MAX_PLAYERS];
        int i = (int)(p - s_pl);
        if (s_ticks - said[i] > 3 * TICK_RATE || said[i] == 0) {
            said[i] = s_ticks;
            show_message(p, str_get("PalaceGuardGossip2"));
            play_vo(p, "PalaceGuardGossip2");
        }
        return;
    }
    if (!a3_warp_open(p, &w))
        return;
    int loaded = zone_load(w.to) == 0;
    for (int i = 0; loaded && w.portal && i < world_warp_count(); i++) /* (armar la zona puede cambiar adónde lleva el portal) */
        if (world_warp(i)->portal == w.portal)
            w = *world_warp(i);
    if (loaded && world_warp_arrival(&w, &ax, &ay) == 0) {
        p->x = ax;
        p->y = ay;
        if (w.portal) /* (por un portal: del mismo lado de las paredes que donde se llega) */
            reach_free_spot(ax, ay, &p->x, &p->y);
        else
            find_free_spot(&p->x, &p->y);
        /* Las de pisar (la boca de la cueva): se llega al lado, no encima, así
           se puede volver a entrar caminando */
        for (int i = 0; i < world_warp_count(); i++) {
            const Warp *o = world_warp(i);
            if (o->from != w.to || o->to != w.from || o->portal)
                continue;
            /* el de llegada (con dos bocas entre las mismas zonas, la que le corresponde) */
            if (ax < o->x0 - 0.5f || ax > o->x1 + 0.5f || ay < o->y0 - 0.5f || ay > o->y1 + 0.5f)
                continue;
            float best = 1e9f, bx = p->x, by = p->y;
            for (int r = 1; r < 12; r++)
                for (int a = 0; a < 16; a++) {
                    float nx = ax + cosf(a * PI_F / 8) * r, ny = ay + sinf(a * PI_F / 8) * r;
                    float pad = (o->margin > 0 ? o->margin : WARP_TOUCH) + 1.5f; /* fuera de donde se usa */
                    int inside = nx >= o->x0 - pad && nx < o->x1 + pad && ny >= o->y0 - pad && ny < o->y1 + pad;
                    float d = (nx - ax) * (nx - ax) + (ny - ay) * (ny - ay);
                    if (!inside && d < best && !blocked_body(nx, ny) && zone_at(nx, ny) == w.to) {
                        best = d;
                        bx = nx;
                        by = ny;
                    }
                }
            p->x = bx;
            p->y = by;
            break;
        }
        /* Que no quede parado sobre otro salto (la torre tiene las dos
           escaleras pegadas): el lugar libre más cerca fuera de ellos */
        for (int r = 1; r < 12; r++) {
            const Warp *other = NULL;
            for (int i = 0; i < world_warp_count() && !other; i++) {
                const Warp *o = world_warp(i);
                if (o->from == w.to && o->to != w.from && !o->portal && p->x >= o->x0 && p->x < o->x1 &&
                    p->y >= o->y0 && p->y < o->y1)
                    other = o;
            }
            if (!other)
                break;
            float a = (float)r * 1.3f, nx = ax + cosf(a) * r, ny = ay + sinf(a) * r;
            if (!blocked_body(nx, ny)) {
                p->x = nx;
                p->y = ny;
            }
        }
        p->prev_x = p->x;
        p->prev_y = p->y;
        if (getenv("D2_WARP_DEBUG"))
            printf("salto de %s a %s: llega a (%.1f, %.1f), tick %u\n", world_zone_name(w.from), world_zone_name(w.to),
                   p->x, p->y, s_ticks);
        p->warp_lock = 1; /* el salto por el que llegó no cuenta hasta salir de él */
        p->lock_from = w.to;
        p->lock_to = w.from;
        if (w.portal)
            audio_play_name("player_townportal_enter", p->x, p->y);
        tp_used(p, &w);
    }
}

/* El portal rojo o la escalera que tiene al lado (los que se usan con A): el
   más cercano, que en la torre hay dos escaleras pegadas. NULL si nada. */
static const Warp *portal_near(const Player *p)
{
    return world_button_warp_near(p->x, p->y);
}

/* ¿Ese salto está cerrado por ahora? (sin avisar): la puerta del palacio,
   la escalera al Durance, la reja de las cloacas */
static int warp_closed(const Warp *w)
{
    return (s_act == 2 && w->to == world_level_zone(50) && !palace_open()) || a3_warp_closed(w);
}

/* Con A y el cartel: ¿el objeto de misión antes que el salto de al lado? Si
   el salto está cerrado (la palanca de las cloacas está junto a la reja) o el
   objeto está más cerca (el diario del Santuario, junto al portal rojo) */
static int quest_first(const Player *p)
{
    int u;
    const Warp *w = portal_near(p);
    if (!quest_action(p, &u))
        return 0;
    if (!w || warp_closed(w))
        return 1;
    float ux, uy, wx = (w->x0 + w->x1) / 2, wy = (w->y0 + w->y1) / 2;
    units_position(u, 1.0f, &ux, &uy);
    return (ux - p->x) * (ux - p->x) + (uy - p->y) * (uy - p->y) <= (wx - p->x) * (wx - p->x) + (wy - p->y) * (wy - p->y);
}

/* Cerca de una escalera, la boca de una cueva o un portal: a dónde lleva,
   como el cartel del original ("A las Catacumbas Nivel 2"). Se entra
   caminando hacia ahí. */
static void draw_warp_label(const Player *p, int ox, int oy)
{
    const Warp *best = NULL;
    float best_d = 9.0f * 9.0f;
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        if (!world_zone_loaded(w->from))
            continue;
        float cx = (w->x0 + w->x1) / 2, cy = (w->y0 + w->y1) / 2, dx = cx - p->x, dy = cy - p->y;
        if (dx * dx + dy * dy < best_d) {
            best_d = dx * dx + dy * dy;
            best = w;
        }
    }
    if (!best)
        return;
    float ax, ay;
    const Level *dest = world_warp_arrival(best, &ax, &ay) == 0 ? world_level_at(ax, ay) : NULL;
    const char *name = dest ? dest->name : world_zone_name(best->to);
    char key[64], text[96];
    snprintf(key, sizeof key, "To The %s", name);
    if (!str_exists(key))
        snprintf(key, sizeof key, "To %s", name);
    snprintf(text, sizeof text, "%s%s", best->portal || best->margin > 0 ? "A: " : "",
             str_exists(key) ? str_get(key) : str_get(name));
    float sx, sy;
    iso_to_screen((best->x0 + best->x1) / 2, (best->y0 + best->y1) / 2, &sx, &sy);
    int w = font_width(text, 2) + 12, h = font_height(2) + 6;
    int x = ox + (int)lroundf(sx) - w / 2, y = oy + (int)lroundf(sy) - 60;
    ui_box(x, y, w, h);
    font_draw(x + 6, y + 3, text, 2, COL(R_GOLD, 15));
}

static void draw_prompts(float cam_x, float cam_y)
{
    int ox = SCREEN_W / 2 - (int)lroundf(cam_x), oy = SCREEN_H / 2 - (int)lroundf(cam_y);
    int shown[MAX_PLAYERS], num_shown = 0;
    for (int k = 0; k < s_num_local && !s_paused; k++) {
        const Player *p = &s_pl[s_local[k]];
        if (p->state >= PS_DYING || p->dialog_npc >= 0 || p->inv_open || p->wp_open ||
            items_ground_nearest(p->x, p->y, 1.8f, 0) >= 0)
            continue;
        char text[80];
        int target = -1, lift = 0, wp = waypoint_near(p), town = p->level && p->level->town;
        int npc = talk_npc_near(p, town);
        if (wp >= 0) {
            target = s_wp_unit[wp];
            snprintf(text, sizeof text, tr("A: Usar el %s"), str_get("Waypoint"));
            lift = 70;
        } else if (npc >= 0 && (town ? !portal_first(p, npc) : !portal_near(p))) {
            /* (en el pueblo sin nadie al lado, sigue: el cofre también tiene su cartel) */
            target = npc;
            snprintf(text, sizeof text, tr("A: Hablar con %s"), target >= 0 ? units_display_name(target) : "");
            lift = 118;
        } else {
            const char *act = quest_action(p, &target);
            if (act && (!portal_near(p) || quest_first(p)))
                snprintf(text, sizeof text, "%s", act);
            else
                target = -1;
            lift = 90;
            /* si no, el cofre, el barril o el santuario de al lado */
            int obj = -1;
            const char *oact = target < 0 ? obj_action(p, &obj) : NULL;
            if (oact && (!portal_near(p) || obj_first(p))) {
                snprintf(text, sizeof text, "%s", oact);
                target = obj;
                lift = 60;
            }
        }
        if (target < 0)
            draw_warp_label(p, ox, oy);
        int dup = 0;
        for (int i = 0; i < num_shown; i++)
            dup |= shown[i] == target;
        if (target < 0 || dup)
            continue;
        shown[num_shown++] = target;
        float ux, uy, sx, sy;
        units_position(target, 1.0f, &ux, &uy);
        iso_to_screen(ux, uy, &sx, &sy);
        int w = font_width(text, 2) + 12, h = font_height(2) + 6;
        int x = ox + (int)lroundf(sx) - w / 2, y = oy + (int)lroundf(sy) - lift;
        ui_box(x, y, w, h);
        font_draw(x + 6, y + 3, text, 2, COL(R_GOLD, 15));
    }
}

/* En las cuevas solo se ve alrededor de los jugadores (radio de luz 13 de
   charstats.txt, más o menos 280 píxeles a lo ancho) */
#define LIGHT_RADIUS_PX    280
#define LIGHT_PX_PER_POINT 20
#define LIGHT_MAX_PX       500
#define LIGHT_PX_PER_UNIT  21  /* Light de missiles.txt y Radius de overlay.txt: el 13 de un jugador da 280 */
#define MAX_LIGHTS         16
#define DEN_LIGHT_TICKS    (2 * TICK_RATE)

/* La Guarida del Mal se ilumina al limpiarla, como en el original: la luz
   crece durante dos segundos y después la cueva queda sin oscuridad (también
   al volver, si ya se cumplió). `grow` es cuánto lleva creciendo. */
static int den_lit(const Level *lv, int *grow)
{
    if (!lv || strcmp(lv->name, "Den of Evil") != 0)
        return 0;
    if (s_den_light > 0 && s_den_light < DEN_LIGHT_TICKS) {
        if (s_den_light > *grow)
            *grow = s_den_light;
        return 0;
    }
    if (s_den_light > 0)
        return 1;
    for (int i = 0; i < s_num_players; i++)
        if (s_pl[i].den_done)
            return 1;
    return 0;
}

/* ¿Está oscuro? Los calabozos siempre; el desierto y Lut Gholein durante el
   eclipse del Sol Mancillado (ENVIRONMENT_TaintedSunBegin) */
static int level_dark(const Level *lv)
{
    int sun = party_max(offsetof(Player, sun_q));
    return lv && (lv->dark || (s_act == 2 && (sun == 1 || sun == 2)));
}

static void draw_darkness_body(float cam_x, float cam_y, const float *sx, const float *sy, int owner)
{
    /* Con la pantalla partida, cada mitad depende de dónde está su jugador */
    int grow = 0;
    if (owner >= 0 && (!s_pl[owner].level || !level_dark(s_pl[owner].level) || den_lit(s_pl[owner].level, &grow)))
        return;
    int lx[MAX_LIGHTS], ly[MAX_LIGHTS], lr[MAX_LIGHTS], n = 0, extra = 0;
    for (int i = 0; i < s_num_players; i++) {
        if (!s_pl[i].level || !level_dark(s_pl[i].level) || den_lit(s_pl[i].level, &grow))
            continue;
        if (s_pl[i].bonus[STAT_LIGHT] > extra) /* objetos con radio de luz */
            extra = s_pl[i].bonus[STAT_LIGHT];
        lx[n] = SCREEN_W / 2 + (int)lroundf(sx[i] - cam_x);
        ly[n] = SCREEN_H / 2 + (int)lroundf(sy[i] - cam_y) - 40; /* centro del cuerpo */
        n++;
    }
    /* El fuego de la muerte de Andariel también alumbra */
    float fx, fy, fsx, fsy;
    if (n > 0 && bossfx_light(&fx, &fy)) {
        iso_to_screen(fx, fy, &fsx, &fsy);
        lx[n] = SCREEN_W / 2 + (int)lroundf(fsx - cam_x);
        ly[n] = SCREEN_H / 2 + (int)lroundf(fsy - cam_y) - 40;
        n++;
    }
    int radius = LIGHT_RADIUS_PX + extra * LIGHT_PX_PER_POINT;
    radius += (LIGHT_MAX_PX - radius) * grow / DEN_LIGHT_TICKS;
    for (int k = 0; k < n; k++)
        lr[k] = radius > LIGHT_MAX_PX ? LIGHT_MAX_PX : radius;
    if (n == 0)
        return;
    /* Los monstruos que tienen luz (Duriel, el Invocador, los magos
       esqueleto, las mercenarias): los que se ven en esta pantalla */
    {
        float ux[64], uy[64];
        int ur[64], nu = units_lights(ux, uy, ur, 64);
        for (int k = 0; k < nu && n < MAX_LIGHTS - 4; k++) {
            float usx, usy;
            iso_to_screen(ux[k], uy[k], &usx, &usy);
            int sx2 = SCREEN_W / 2 + (int)lroundf(usx - cam_x), sy2 = SCREEN_H / 2 + (int)lroundf(usy - cam_y) - 40;
            int rr = ur[k] * LIGHT_PX_PER_UNIT;
            if (sx2 < -rr || sx2 > SCREEN_W + rr || sy2 < -rr || sy2 > SCREEN_H + rr)
                continue;
            lx[n] = sx2;
            ly[n] = sy2;
            lr[n] = rr;
            n++;
        }
    }
    /* Lo que alumbra, como en el original: los proyectiles (Light de
       missiles.txt: las flechas de fuego, las jabalinas de rayo...) y los
       efectos (Radius de overlay.txt: el destello del rayo, la Percepción) */
    for (int i = 0; i < missiles_capacity() && n < MAX_LIGHTS; i++) {
        const Missile *mi = missiles_get(i);
        int l = mi ? missiles_light(mi) : 0;
        if (l <= 0)
            continue;
        float msx, msy;
        iso_to_screen(mi->x, mi->y, &msx, &msy);
        lx[n] = SCREEN_W / 2 + (int)lroundf(msx - cam_x);
        ly[n] = SCREEN_H / 2 + (int)lroundf(msy - cam_y) - 20 - (int)mi->z;
        lr[n] = l * LIGHT_PX_PER_UNIT;
        n++;
    }
    float ox[MAX_LIGHTS], oy[MAX_LIGHTS];
    int orad[MAX_LIGHTS], no = overlays_lights(ox, oy, orad, MAX_LIGHTS - n, overlay_pos);
    for (int k = 0; k < no; k++) {
        float osx, osy;
        iso_to_screen(ox[k], oy[k], &osx, &osy);
        lx[n] = SCREEN_W / 2 + (int)lroundf(osx - cam_x);
        ly[n] = SCREEN_H / 2 + (int)lroundf(osy - cam_y) - 40;
        lr[n] = orad[k] * LIGHT_PX_PER_UNIT;
        n++;
    }
    fb_light_mask(lx, ly, lr, n, SCREEN_H - 49);
}

static void draw_darkness(float cam_x, float cam_y, const float *sx, const float *sy, int owner)
{
    double t0 = wall_ms();
    draw_darkness_body(cam_x, cam_y, sx, sy, owner);
    s_part_ms[1] += wall_ms() - t0;
}

/* VER RENDIMIENTO: arriba al medio, lo que midió main en el último segundo.
   La presentación incluye esperar la sincronía vertical. */
static void draw_perf(void)
{
    char text[128];
    snprintf(text, sizeof text, tr("%.0f fps   lógica %.1f ms (peor %.0f)   dibujo %.1f   subir %.1f   presentar %.1f"),
             s_perf[0], s_perf[1], s_perf[2], s_perf[3], s_perf[4], s_perf[5]);
    int w = font_width(text, 2);
    ui_dark_box(SCREEN_W / 2 - w / 2 - 6, 2, w + 12, font_height(2) + 6);
    font_draw_centered(SCREEN_W / 2, 5, text, 2, COL(R_GOLD, 15));
}

/* Menú de pausa: letras grandes y el pentagrama girando a los costados */
/* Un texto que no se pase de `w` (si no entra, se corta con "...") */
static void draw_text_fit(int x, int y, int w, const char *text, int scale, uint8_t color)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", lang_screen(text)); /* (se corta lo que se ve, ya traducido) */
    if (font_width(buf, scale) > w) {
        size_t n = strlen(buf);
        while (n > 0 && font_width(buf, scale) + font_width("...", scale) > w) {
            buf[--n] = '\0';
            while (n > 0 && ((unsigned char)buf[n - 1] & 0xC0) == 0x80) /* sin cortar una letra con tilde */
                buf[--n] = '\0';
            if (n > 0 && ((unsigned char)buf[n - 1] & 0xC0) == 0xC0)
                buf[--n] = '\0';
        }
        snprintf(buf + n, sizeof buf - n, "...");
    }
    font_draw(x, y, buf, scale, color);
}

/* Un ícono de la guía con su cantidad ("3") abajo a la derecha */
static void draw_guide_icon(const CubeLine *l, int x, int y, int size)
{
    fb_blend_rect(x, y, size, size, COL(R_BLUE, 3), BLEND_50);
    draw_frame(x, y, size, size, COL(R_STONE, 7));
    if (l->icon)
        items_draw_icon(l->icon, x + 1, y + 1, size - 2);
    else
        font_draw_centered(x + size / 2, y + size / 2 - 7, "?", 2, COL(R_GOLD, 15)); /* cualquier objeto */
    if (l->qty > 1) {
        char q[12];
        snprintf(q, sizeof q, "%d", l->qty);
        font_draw(x + size - font_width(q, 2) - 2, y + size - 15, q, 2, COL(R_GOLD, 15));
    }
}

/* La solapa de las palabras rúnicas: cada una con sus runas en el orden en
   que se engarzan, la cantidad justa de engarces, dónde va y el nivel que
   pide; abajo, lo que da la elegida (con los rangos del original) */
static void draw_guide_runewords(void)
{
    const uint8_t white = COL(R_GRAY, 15), gray = COL(R_GRAY, 12), gold = COL(R_GOLD, 15);
    int n = items_runeword_count(), rows = guide_rows(CUBE_CAT_RUNEWORDS), name_x = 40 + 6 * 52 + 8;
    for (int r = 0; r < rows && s_guide_top + r < n; r++) {
        int w = guide_runeword(s_guide_top + r), y = 100 + r * 46, sel = s_guide_top + r == s_guide_sel;
        if (sel) {
            fb_blend_rect(30, y - 3, SCREEN_W - 60, 44, COL(R_GOLD, 3), BLEND_50);
            draw_frame(30, y - 3, SCREEN_W - 60, 44, COL(R_GOLD, 15));
        }
        const ItemType *runes[6];
        int nr = items_runeword_runes(w, runes), ix = 40;
        for (int k = 0; k < nr; k++) {
            if (k > 0) { /* "+" (la fuente del juego no lo tiene) */
                fb_fill_rect(ix - 11, y + 18, 9, 2, gray);
                fb_fill_rect(ix - 7, y + 14, 2, 10, gray);
            }
            CubeLine l = { runes[k], 1, "" };
            draw_guide_icon(&l, ix, y, 38);
            ix += 52;
        }
        char lvl[16], where[96], info[128];
        snprintf(lvl, sizeof lvl, tr("Nivel %d"), items_runeword_level(w));
        items_runeword_where(w, where, sizeof where);
        snprintf(info, sizeof info, tr("%d engarces: %s"), nr, where);
        font_draw(name_x, y + 3, items_runeword_name(w), 2, sel ? gold : white);
        font_draw(SCREEN_W - 44 - font_width(lvl, 1), y + 6, lvl, 1, sel ? gold : gray);
        draw_text_fit(name_x, y + 24, SCREEN_W - 44 - name_x, info, 1, gray);
    }
    if (n > rows) {
        char more[32];
        snprintf(more, sizeof more, tr("%d de %d"), s_guide_sel + 1, n);
        font_draw(SCREEN_W - 40 - font_width(more, 1), 374, more, 1, gray);
    }
    int w = guide_runeword(s_guide_sel);
    if (w < 0)
        return;
    static char lines[18][112]; /* lo de la elegida (se arma una vez, no en cada cuadro) */
    static int  nl, shown = -1;
    char title[96];
    if (w != shown) {
        nl = items_runeword_describe(w, s_pl[s_local[0]].level_num, lines, 18);
        shown = w;
    }
    ui_dark_box(30, 386, SCREEN_W - 60, 190);
    snprintf(title, sizeof title, tr("%s: lo que da"), items_runeword_name(w));
    font_draw(42, 392, title, 2, gold);
    for (int k = 0; k < nl; k++)
        draw_text_fit(52 + (k / 9) * 360, 414 + (k % 9) * 17, 350, lines[k], 1, COL(R_BLUE, 15));
}

/* La Guía del cubo: arriba las categorías; en cada una, las recetas con los
   íconos de lo que llevan y lo que dan, y abajo la elegida en palabras */
static void draw_guide(void)
{
    fb_shade_rect(0, 0, SCREEN_W, SCREEN_H, 13);
    font_draw_centered(SCREEN_W / 2, 14, "GUÍA DEL CUBO HORÁDRICO", 6, COL(R_GOLD, 15));
    /* Las categorías */
    int total = 0, gap;
    for (int c = 0; c < CUBE_CATS; c++)
        total += font_width(cube_category_name(c), 2);
    gap = (SCREEN_W - 24 - total) / (CUBE_CATS - 1); /* que entren todas */
    gap = gap > 16 ? 16 : gap < 6 ? 6 : gap;
    total += gap * (CUBE_CATS - 1);
    int x = (SCREEN_W - total) / 2;
    for (int c = 0; c < CUBE_CATS; c++) {
        const char *name = cube_category_name(c);
        int w = font_width(name, 2);
        if (c == s_guide_cat) {
            fb_blend_rect(x - 6, 64, w + 12, 22, COL(R_GOLD, 4), BLEND_50);
            draw_frame(x - 6, 64, w + 12, 22, COL(R_GOLD, 15));
        }
        font_draw(x, 68, name, 2, c == s_guide_cat ? COL(R_GOLD, 15) : COL(R_GRAY, 11));
        x += w + gap;
    }
    const uint8_t white = COL(R_GRAY, 15), gray = COL(R_GRAY, 12), gold = COL(R_GOLD, 15);
    if (s_guide_cat == CUBE_CAT_HOWTO) {
        static const char *const k_steps[] = {
            "1. El Cubo Horádrico está en un cofre de las Salas de los Muertos (Acto 2).",
            "2. Abrí el inventario y apretá X sobre el cubo: se abre en el lugar del equipo.",
            "3. Con el cubo abierto, X sobre algo de la mochila lo mete en el cubo,",
            "    y X sobre algo del cubo lo devuelve a la mochila.",
            "4. Poné justo lo que pide una receta (ni más ni menos cosas).",
            "5. Bajá hasta TRANSMUTAR y apretá A o X: el resultado queda en el cubo.",
            "",
            "Con A también se puede levantar algo y acomodarlo a mano, como en la mochila.",
            "Una gema, runa o joya en la mano, soltada sobre un objeto con engarces libres,",
            "se engarza. Las runas en el orden justo forman una palabra rúnica (su solapa).",
            "",
            "Las recetas están en las otras solapas: izquierda y derecha (o LB y RB).",
        };
        const ItemType *box = items_find("box");
        if (box)
            items_draw_icon(box, SCREEN_W / 2 - 32, 104, 64);
        /* (lo que no entra, en otro renglón: en otros idiomas son más largos) */
        int y = 182;
        for (unsigned k = 0; k < sizeof k_steps / sizeof k_steps[0]; k++) {
            const char *t = lang_screen(k_steps[k]);
            uint8_t col = k_steps[k][0] >= '1' && k_steps[k][0] <= '9' ? white : gray;
            if (!t[0]) {
                y += 12;
                continue;
            }
            for (int first = 1; *t; first = 0) {
                char line[200];
                int len = font_break(t, 2, SCREEN_W - 140 - (first ? 0 : 20));
                snprintf(line, sizeof line, "%.*s", len < 199 ? len : 199, t);
                font_draw(first ? 70 : 90, y, line, 2, col);
                y += first ? 24 : 20;
                for (t += len; *t == ' '; t++) {
                }
            }
        }
    } else if (s_guide_cat == CUBE_CAT_RUNEWORDS) {
        draw_guide_runewords();
    } else {
        int n = guide_count(s_guide_cat);
        CubeLine in[7], out;
        for (int r = 0; r < GUIDE_ROWS && s_guide_top + r < n; r++) {
            int idx = guide_recipe(s_guide_cat, s_guide_top + r), y = 100 + r * 46, sel = s_guide_top + r == s_guide_sel;
            int nin = cube_recipe_info(idx, in, 7, &out);
            if (sel) {
                fb_blend_rect(30, y - 3, SCREEN_W - 60, 44, COL(R_GOLD, 3), BLEND_50);
                draw_frame(30, y - 3, SCREEN_W - 60, 44, COL(R_GOLD, 15));
            }
            int ix = 40;
            for (int k = 0; k < nin; k++) {
                if (k > 0) { /* "+" (la fuente del juego no lo tiene) */
                    fb_fill_rect(ix - 11, y + 18, 9, 2, gray);
                    fb_fill_rect(ix - 7, y + 14, 2, 10, gray);
                }
                draw_guide_icon(&in[k], ix, y, 38);
                ix += 52;
            }
            /* la flecha hacia lo que sale */
            fb_fill_rect(ix - 10, y + 18, 12, 3, gold);
            for (int k = 0; k < 5; k++)
                fb_fill_rect(ix + 2 + k, y + 14 + k, 1, 11 - 2 * k, gold);
            draw_guide_icon(&out, ix + 12, y, 38);
            draw_text_fit(ix + 60, y + 11, SCREEN_W - 50 - (ix + 60), out.text, 2, sel ? gold : white);
        }
        if (n > GUIDE_ROWS) { /* hay más arriba o abajo */
            char more[32];
            snprintf(more, sizeof more, tr("%d de %d"), s_guide_sel + 1, n);
            font_draw(SCREEN_W - 40 - font_width(more, 1), 470, more, 1, gray);
        }
        /* La elegida en palabras */
        int idx = guide_recipe(s_guide_cat, s_guide_sel);
        if (idx >= 0) {
            int nin = cube_recipe_info(idx, in, 7, &out);
            ui_dark_box(30, 482, SCREEN_W - 60, 92);
            font_draw(42, 488, "Poné en el cubo:", 2, gold);
            for (int k = 0; k < nin; k++) {
                char line[112];
                if (in[k].qty > 1)
                    snprintf(line, sizeof line, "%d x %s", in[k].qty, in[k].text);
                else
                    snprintf(line, sizeof line, "%s", in[k].text);
                int col = k < 4 ? 0 : 1, row = k < 4 ? k : k - 4;
                draw_text_fit(52 + col * 360, 506 + row * 15, 350, line, 1, white);
            }
            char res[112];
            snprintf(res, sizeof res, tr("Sale: %s"), out.text);
            draw_text_fit(42, 568 - 2, SCREEN_W - 100, res, 1, gold);
        }
    }
    font_draw_centered(SCREEN_W / 2, 584, "Izquierda / derecha: categoría    Arriba / abajo: receta    B: volver", 1,
                       COL(R_STONE, 12));
}

/* La pausa: a la izquierda el registro de misiones del original (las que
   hizo, la que está haciendo y las que faltan), a la derecha las opciones
   con los pentagramas del menú del juego */
static void draw_pause(void)
{
    if (s_guide_open) {
        draw_guide();
        return;
    }
    if (s_controls_open) {
        controls_draw();
        return;
    }
    int sfx, music;
    audio_get_volume(&sfx, &music);
    static const char *const k_labels[PAUSE_COUNT] = {
        [PAUSE_RESUME] = "VOLVER AL JUEGO", [PAUSE_SFX] = "EFECTOS",         [PAUSE_MUSIC] = "MÚSICA",
        [PAUSE_RUN] = "CORRER SIEMPRE",     [PAUSE_POS] = "VER POSICIÓN",    [PAUSE_PERF] = "RENDIMIENTO",
        [PAUSE_COLOR16] = "COLOR 16 BITS",  [PAUSE_CONTROLS] = "CONTROLES",  [PAUSE_GUIDE] = "GUÍA DEL CUBO",
        [PAUSE_QUIT] = "GUARDAR Y SALIR",
    };
    char values[PAUSE_COUNT][16] = { { 0 } };
    snprintf(values[PAUSE_SFX], sizeof values[0], "< %d >", sfx);
    snprintf(values[PAUSE_MUSIC], sizeof values[0], "< %d >", music);
    snprintf(values[PAUSE_RUN], sizeof values[0], "< %s >", s_always_run ? tr("SÍ") : tr("NO"));
    snprintf(values[PAUSE_POS], sizeof values[0], "< %s >", s_show_pos ? tr("SÍ") : tr("NO"));
    snprintf(values[PAUSE_PERF], sizeof values[0], "< %s >", s_show_perf ? tr("SÍ") : tr("NO"));
    snprintf(values[PAUSE_COLOR16], sizeof values[0], "< %s >", (s_color16 < 0 ? video_is_565() : s_color16) ? tr("SÍ") : tr("NO"));
    fb_shade_rect(0, 0, SCREEN_W, SCREEN_H, 6);

    const Player *qp = &s_pl[s_local[s_pause_qp < s_num_local ? s_pause_qp : 0]];
    int qx = 28, qy = 84, act = s_pause_qact, sel = s_pause_qsel;
    if (act <= 0) /* recién abierta (antes del primer tick) */
        quest_log_pick(qp, &act, &sel);
    char title[64];
    if (s_num_players > 1)
        snprintf(title, sizeof title, tr("Misiones de J%d: %s"), player_index(qp) + 1, tr(s_classes[qp->cls].title));
    else
        snprintf(title, sizeof title, tr("Misiones: %s"), tr(s_classes[qp->cls].title));
    font_draw_centered(qx + PANEL_W / 2, qy - 26, title, 2, COL(R_GOLD, 15));
    draw_quest_log(qp, qx, qy, act, sel, s_pause_qanim, "LB / RB: otra misión", s_num_local > 1 ? "X: el otro jugador" : NULL);

    int ox = qx + PANEL_W + 24, ow = SCREEN_W - 28 - ox, oy = qy;
    ui_dark_box(ox, oy, ow, PANEL_H);
    font_draw_centered(ox + ow / 2, oy + 12, "PAUSA", 6, COL(R_GOLD, 15));
    char diff[48], tz[192];
    snprintf(diff, sizeof diff, tr("Dificultad: %s"), tr(k_diff_names[s_diff]));
    tz_pause_line(tz, sizeof tz, ow - 16); /* y la zona de terror, abajo, en violeta (en el Eco, la oleada, en rojo) */
    if (s_eco)
        eco_pause_line(tz, sizeof tz);
    font_draw_centered(ox + ow / 2, oy + (tz[0] ? 44 : 52), diff, 2, s_diff ? COL(R_RED, 14) : COL(R_GRAY, 13));
    if (tz[0])
        font_draw_centered(ox + ow / 2, oy + 61, tz, 1, s_eco ? COL(R_RED, 14) : g_text_shift[11][COL(R_GRAY, 15)]);
    const char *quit_label = eco_running() ? "TERMINAR EL ECO" : k_labels[PAUSE_QUIT];
    for (int i = 0; i < PAUSE_COUNT; i++) {
        int y = oy + 76 + i * 33, chosen = i == s_pause_sel;
        uint8_t c = chosen ? COL(R_GOLD, 15) : COL(R_GRAY, 13);
        /* (en los idiomas de palabras largas, si no entra antes del valor, más chico) */
        int room = ow - 18 - 52 - (values[i][0] ? font_width(values[i], 3) + 10 : 0);
        const char *label = i == PAUSE_QUIT ? quit_label : k_labels[i];
        if (font_width(label, 3) > room)
            font_draw(ox + 52, y + 4, label, 2, c);
        else
            font_draw(ox + 52, y, label, 3, c);
        if (values[i][0])
            font_draw(ox + ow - 18 - font_width(values[i], 3), y, values[i], 3, c);
        if (chosen)
            draw_pentagram(ox + 8, y - 6, 36, 1);
    }
    font_draw_centered(ox + ow / 2, oy + PANEL_H - 26, "A: elegir   Izq. / der.: cambiar   B: volver", 1,
                       COL(R_STONE, 12));
}

static void render_frame(float alpha)
{
    fb_frame_begin();
    s_world_view = 0;
    s_anim_ticks++;
    float sx[MAX_PLAYERS], sy[MAX_PLAYERS];
    for (int i = 0; i < s_num_players; i++) {
        float px, py;
        player_world_pos(&s_pl[i], alpha, &px, &py);
        iso_to_screen(px, py, &sx[i], &sy[i]);
    }

    clip_reset();
    int la = s_local[0], lb = s_local[s_num_local > 1 ? 1 : 0]; /* los de esta máquina */
    if (s_num_local == 1) {
        /* Con el inventario abierto (mitad derecha) el personaje queda en el
           centro de la mitad izquierda, como en el original; con la tienda
           (a la izquierda, con el inventario a la derecha), en el medio */
        float shift = s_pl[la].dialog_npc >= 0 && s_pl[la].shop_open ? 0.0f : s_pl[la].inv_open ? PANEL_W / 2.0f : 0.0f;
        render_world(sx[la] + shift, sy[la], alpha);
        draw_darkness(sx[la] + shift, sy[la], sx, sy, la);
        overlays_draw(sx[la] + shift, sy[la], overlay_pos);
        draw_coll_debug(sx[la] + shift, sy[la]);
        draw_item_labels(sx[la] + shift, sy[la]);
        draw_prompts(sx[la] + shift, sy[la]);
        draw_npc_news(sx[la] + shift, sy[la]);
        draw_view_automap(sx[la] + shift, sy[la], la);
        s_view_x[la] = SCREEN_W / 2.0f - shift;
        s_view_y[la] = SCREEN_H / 2.0f;
        bossfx_draw();
        draw_hud();
        draw_banners();
        if (s_demo && !s_eco)
            draw_demo_banner();
        skilltest_draw();
        if ((s_demo && !s_eco) || s_show_pos || getenv("D2_SHOW_POS"))
            draw_positions();
        if (s_paused)
            draw_pause();
        return;
    }

    /* Cámara compartida mientras los dos entran en pantalla; si se alejan,
       la pantalla se parte con una línea perpendicular a la recta que los une.
       En el punto de corte las dos cámaras coinciden, así no hay salto. */
    float dx = sx[lb] - sx[la], dy = sy[lb] - sy[la];
    float dist = sqrtf(dx * dx + dy * dy);
    float nx = 1.0f, ny = 0.0f, k = 0.0f;
    if (dist > 0.001f) {
        nx = dx / dist;
        ny = dy / dist;
        float ex = fabsf(nx) > 1e-4f ? (SCREEN_W * 0.5f) / fabsf(nx) : 1e9f;
        float ey = fabsf(ny) > 1e-4f ? (SCREEN_H * 0.5f) / fabsf(ny) : 1e9f;
        k = fminf(ex, ey) * SPLIT_FACTOR;
    }

    s_split = dist * 0.5f > k;
    s_split_nx = nx;
    s_split_ny = ny;
    if (dist * 0.5f <= k) {
        float cx = (sx[la] + sx[lb]) * 0.5f, cy = (sy[la] + sy[lb]) * 0.5f;
        render_world(cx, cy, alpha);
        draw_darkness(cx, cy, sx, sy, -1);
        overlays_draw(cx, cy, overlay_pos);
        draw_coll_debug(cx, cy);
        draw_item_labels(cx, cy);
        draw_prompts(cx, cy);
        draw_npc_news(cx, cy);
        draw_view_automap(cx, cy, -1);
        for (int i = 0; i < s_num_players; i++) {
            s_view_x[i] = SCREEN_W / 2.0f + sx[i] - cx;
            s_view_y[i] = SCREEN_H / 2.0f + sy[i] - cy;
        }
    } else {
        float c1x = sx[la] + nx * k, c1y = sy[la] + ny * k;
        float c2x = sx[lb] - nx * k, c2y = sy[lb] - ny * k;
        clip_half_plane(SCREEN_W / 2.0f, SCREEN_H / 2.0f, nx, ny, -1);
        render_world(c1x, c1y, alpha);
        draw_darkness(c1x, c1y, sx, sy, la);
        overlays_draw(c1x, c1y, overlay_pos);
        draw_coll_debug(c1x, c1y);
        draw_item_labels(c1x, c1y);
        draw_prompts(c1x, c1y);
        draw_npc_news(c1x, c1y);
        draw_view_automap(c1x, c1y, la);
        clip_half_plane(SCREEN_W / 2.0f, SCREEN_H / 2.0f, nx, ny, +1);
        render_world(c2x, c2y, alpha);
        draw_darkness(c2x, c2y, sx, sy, lb);
        overlays_draw(c2x, c2y, overlay_pos);
        draw_coll_debug(c2x, c2y);
        draw_item_labels(c2x, c2y);
        draw_prompts(c2x, c2y);
        draw_npc_news(c2x, c2y);
        draw_view_automap(c2x, c2y, lb);
        clip_reset();
        draw_divider(nx, ny, dist * 0.5f - k);
        s_view_x[la] = SCREEN_W / 2.0f - nx * k;
        s_view_y[la] = SCREEN_H / 2.0f - ny * k;
        s_view_x[lb] = SCREEN_W / 2.0f + nx * k;
        s_view_y[lb] = SCREEN_H / 2.0f + ny * k;
    }
    bossfx_draw();
    draw_hud();
    draw_banners();
    if (s_demo && !s_eco) /* (los bots en el Eco, D2_ECO_DEMO: sin el cartel ni las posiciones, que tapan las oleadas) */
        draw_demo_banner();
    skilltest_draw();
    if ((s_demo && !s_eco) || s_show_pos || getenv("D2_SHOW_POS"))
        draw_positions();
    if (s_show_perf)
        draw_perf();
    if (s_paused)
        draw_pause();
}

void game_render(float alpha)
{
    double t0 = wall_ms();
    render_frame(alpha);
    s_part_ms[3] += wall_ms() - t0;
    s_part_frames++;
}
