#include "units.h"

#include "audio.h"
#include "charanim.h"
#include "fmt/fs.h"
#include "game.h"
#include "iso.h"
#include "jobs.h"
#include "missiles.h"
#include "overlays.h"
#include "skills.h"
#include "strings.h"
#include "video.h"

#include <SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UNITS   4096 /* con todo el Acto 2 armado (las pruebas) pasa de 2048 */
#define MON_SKILLS  8    /* Skill1..Skill8 de monstats.txt */
#define RIDE_DY     48   /* el diablillo montado: cuántos píxeles más arriba (el lomo de la bestia de asedio) */
#define MAX_GFX     192 /* el Acto 5 entero armado (D2_ZONETEST) pasaba de 128 y los últimos objetos no salían */
/* Gráficos que ya no usa nadie: se guardan hasta este total por si se
   vuelve a la zona (más allá se libera el que hace más que no se usa) */
#define GFX_CACHE_BYTES (24u << 20)
#define ZONE_FREE   (-2) /* lugar libre del arreglo de unidades */
#define PI_F        3.14159265f
#define UNIT_RADIUS 0.45f /* tamaño de un NPC, animal o monstruo, en subtiles */

/* Velocidad de monstats.txt -> subtiles por segundo (misma escala que los jugadores) */
#define SPEED_PER_VELOCITY (5.0f / 6.0f)
/* Como los jugadores (WALK_BONUS y RUN_BONUS de game.c), más ágiles que en el
   original: caminando un 10% y corriendo un 20% más rápido, y la animación al
   mismo paso (el original la ata a la velocidad: UNITS_UpdateRunWalkAnimRateAndVelocity) */
#define MON_WALK_BONUS 1.10f
#define MON_RUN_BONUS  1.20f
/* Las invocaciones corren un 40% más rápido de lo que caminan */
#define PET_RUN_PACE (1.4f * MON_RUN_BONUS / MON_WALK_BONUS)

/* Un monstruo te ve apenas está en tu pantalla (el rectángulo de 800x600
   centrado en el jugador) y te sigue hasta que salís un cuarto más allá del
   borde. En el original no hay una distancia única (aidist está vacía en
   monstats: cada IA mira la suya) y los monstruos piensan cuando su sala
   está activa, más o menos lo que entra en pantalla. Antes eran 9 subtiles
   para verte y 18 para seguirte. */
#define CHASE_SCREEN 1.25f
#define CHASE_RANGE  18.0f /* (lo mínimo para seguir: los que vuelven a su lugar) */
#define MELEE_REACH 1.4f  /* distancia entre centros para poder pegar (los de tamaño 1) */

/* Modificadores de los jefes (Mod1..Mod3 de superuniques.txt, números de
   monumod.txt): un bit cada uno en Unit.mods */
enum {
    MOD_STRONG = 5, MOD_FAST = 6, MOD_CURSE = 7, MOD_RESIST = 8, MOD_FIRE = 9, MOD_LIGHTNING = 17,
    MOD_COLD = 18, MOD_MANAHIT = 25, MOD_SPECTRAL = 27, MOD_STONESKIN = 28, MOD_MULTISHOT = 29, MOD_AURA = 30,
};
#define HAS_MOD(u, m) (((u)->mods >> (m)) & 1u)
#define UNIQUE_LEVELS 3 /* los jefes tienen 3 niveles más que la zona (el original, en Normal) */
#define SURROUND_NEAR 6.0f /* a esta distancia del jugador busca un lugar a su alrededor */

enum { ANIM_IDLE, ANIM_WALK, ANIM_ATTACK, ANIM_HIT, ANIM_DEATH, ANIM_DEAD, ANIM_ATTACK2, ANIM_SKILL, ANIM_RUN,
       ANIM_CAST, ANIM_SKILL2, ANIM_SKILL3, ANIM_SKILL4, ANIM_COUNT };

#define RANGED_MAX   9.0f /* a esta distancia los que tiran proyectiles empiezan a disparar */
#define MAX_KILLS    32

/* ST_RISE: sale del suelo (los zombis que levanta Blood Raven) */
enum { ST_IDLE, ST_CHASE, ST_ATTACK, ST_HIT, ST_RISE, ST_DYING, ST_DEAD };

#define SLEEP_RANGE 40.0f /* más lejos de todos los jugadores, los monstruos no hacen nada */
#define MAX_MINIONS 6     /* zombis de Blood Raven a la vez */
#define RAISE_TICKS 175   /* entre una invocación y la siguiente */

/* Mercenarias: a qué distancia (en subtiles) siguen a su dueño, a cuál
   pelean y cuándo aparecen a su lado */
#define MERC_FOLLOW  3.5f /* empieza a seguirlo a esta distancia */
#define MERC_STOP    2.5f /* y se para a esta (si no, arranca y frena a cada rato) */
#define MERC_CATCHUP 1.25f /* corriendo para alcanzar al dueño, más rápido que su tabla */
#define MERC_RUN     7.0f
#define MERC_WARP    28.0f
#define MERC_RANGE   10.0f
#define MERC_LEASH   14.0f
#define MERC_COOLDOWN 10

/* Gráficos compartidos entre unidades iguales (por ejemplo, todos los
   Fallen). En el juego se decodifican en otro hilo (la Pi tiene 4 núcleos):
   mientras tanto la unidad existe pero no se mueve ni se dibuja. */
enum { GFX_EMPTY, GFX_LOADING, GFX_READY, GFX_FAILED };
typedef struct {
    char         key[32]; /* "" = lugar libre */
    CharAnim     anim[ANIM_COUNT];
    int          refs;    /* unidades que lo usan: con 0 queda guardado un rato */
    int          state;   /* GFX_* (job_flag_get/set): el hilo que carga lo pone en READY o FAILED */
    CharLook     look;
    char         wclass[8];
    const char  *modes[ANIM_COUNT]; /* los que se piden (NULL = no cargar); apuntan a mode_buf */
    char         mode_buf[ANIM_COUNT][4]; /* copia: el hilo de carga los lee después */
    size_t       bytes;
    uint32_t     idle_since;        /* cuándo dejó de usarlo la última unidad */
} UnitGfx;

/* Una de las tablas de colores de palshift.dat de un tipo de monstruo
   (mon_palshift), leída en el hilo de trabajos */
#define PALSHIFT_MAX 512
enum { PS_LOADING, PS_SAME, PS_READY }; /* PS_SAME: no está o no cambia nada */
typedef struct {
    char    token[8];
    int     idx;      /* cuál de las 8 */
    int     state;    /* PS_* (job_flag_get/set) */
    uint8_t tab[256];
} PalShift;

static void palshift_job(void *ptr, int arg)
{
    (void)arg;
    PalShift *p = ptr;
    char path[96];
    snprintf(path, sizeof path, "data/global/monsters/%s/cof/palshift.dat", p->token);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int state = PS_SAME;
    if (d && n >= 8 * 256) {
        memcpy(p->tab, d + p->idx * 256, 256);
        for (int c = 0; c < 256 && state == PS_SAME; c++)
            if (p->tab[c] != c)
                state = PS_READY;
    }
    free(d);
    job_flag_set(&p->state, state);
}

/* Sonidos de un tipo de monstruo (monsounds.txt). Las demoras están en
   cuadros de 25 por segundo, como los ticks. */
typedef struct {
    int attack[2], weapon[2], att_del[2], wea_del[2], att_prb[2], wea_vol[2];
    int hit, death, hit_del, death_del;
    int skill[4];
    int footstep, fs_cnt, fs_off, fs_prb;
    int neutral, neu_time;
    int flee;
    int init, taunt[2]; /* al ver al jugador por primera vez y frases de los jefes ("Taunt" y su _2) */
    int cast;           /* lanzar (Andariel escupe veneno) */
} MonSounds;

#define MAX_MON_SOUNDS 256
#define SOUND_PENDING  2

typedef struct {
    UnitGfx    *gfx;
    int         zone;     /* zona a la que pertenece (-1 = ninguna: mercenarias), ZONE_FREE = lugar libre */
    char        name[32]; /* clase del objeto o Id del monstruo */
    const char *display;  /* nombre traducido */
    float       x, y, prev_x, prev_y;
    float       home_x, home_y, target_x, target_y;
    float       speed;      /* subtiles por segundo al caminar */
    float       wander;     /* radio en el que se pasea; 0 = quieto */
    int         anim_speed; /* objetos: FrameDelta de objects.txt; 0 = la de la animación */
    int         sector;
    int         walking;
    int         timer;
    int         anim_acc;
    int         shadow;
    int         solid; /* no se lo puede atravesar */
    int         killable; /* killable de monstats.txt: con 0 no le llega daño ni es enemigo para los
                             jugadores (SUNITDMG_ApplyDamage): Baal en su trono, los vigías de las catapultas */
    float       radius; /* tamaño (SizeX de monstats2.txt): 0.5, 0.75 o 1 subtile */
    int         foot;   /* lo que ocupa contra las paredes: 1 subtile, 2 cruz, 3 cuadrado de 3x3 */
    uint16_t    move_mask; /* los COLL_* con los que choca al moverse (Path.cpp: caminando o volando) */
    float       reach;  /* distancia entre centros para pegarle al jugador */
    float       slot_x, slot_y; /* lugar alrededor del jugador al que va (para rodearlo) */
    int         has_slot;
    int         npc;   /* NPC del pueblo con el que se puede hablar */
    int         talk_ticks; /* mientras le hablan se queda quieto */
    uint32_t    rng;

    /* Monstruos enemigos */
    int enemy;
    int state;
    int hp, max_hp;
    int dmg_min, dmg_max;
    int exp;
    int level;
    int ac, ar;   /* defensa y puntería (monlvl.txt x porcentajes de monstats.txt) */
    int res[6];   /* resistencias por elemento (ELEM_NONE es la física): ResDm, ResFi... de monstats.txt */
    char tc[32];  /* clase de tesoro */
    int unique;   /* jefe con nombre propio (superuniques.txt) */
    int attack_delay; /* ticks entre ataques (aidel de monstats.txt) */
    int cooldown;
    int target;
    int hit_done;
    const struct MonSeq *seq; /* su secuencia de ataque (la Estocada) o la de la habilidad que está usando */
    int jab;           /* este ataque es la secuencia */
    int merc_spell;    /* el Lobo de Hierro: qué hechizo lanza ahora (0 o 1) */
    const struct MonSeq *seqs[MON_SKILLS]; /* las de sus habilidades (Sk1mode..Sk8mode de monstats.txt) */
    char skill[MON_SKILLS][24];            /* Skill1..Skill8 (Diablo tiene 7) */
    int8_t  sk_anim[MON_SKILLS];           /* el modo de cada una (ANIM_*, -1 = una secuencia o ninguno) */
    int16_t sk_lvl[MON_SKILLS];            /* y su nivel (Sk1lvl..Sk8lvl) */
    int  seq_skill;               /* cuál está usando */
    char base[24];                /* BaseId de monstats.txt (la manada) */
    int  s1_min, s1_max;          /* el daño de su habilidad (S1MinD, S1MaxD) */
    /* El1..El3 de monstats.txt: el daño elemental de cada ataque (anim
       ANIM_*, -1 = ninguno), su probabilidad y su duración */
    struct { int8_t anim, elem; int16_t pct, min, max, dur; } el[3];
    /* La IA del original (monai.inc) */
    uint8_t ai;
    int16_t aip[8];         /* aip1..8 de monstats.txt */
    int     ai_wait, ai_move, ai_follow, ai_spd, ai_run; /* ai_run: corriendo (RunToTargetUnit) */
    float   ai_tx, ai_ty;
    float   melee_bonus;    /* se le pega desde más lejos (los tentáculos, en el agua: UNITS_IsInMeleeRange) */
    int     ai_p[3];        /* lo que recuerda cada IA (dwAiParam) */
    int     hurt;           /* le pegaron desde que la IA pensó la última vez */
    int     spawn_boss;     /* salió como jefe, al azar o de superuniques.txt (D2GAME_SpawnMonster_6FC6F220):
                               de estos sale el del Pájaro Dorado */
    int8_t  champion;       /* CHAMP_*: campeón, y de qué tipo (0 = no) */
    char    display_buf[48]; /* el nombre del campeón ("Caído campeador") */
    int     heal_target, heal_pct; /* el sacerdote cura a ese aliado en el cuadro del lanzamiento */
    float   leap_x0, leap_y0, leap_x1, leap_y1; /* el salto: de dónde a dónde */
    /* Los hechizos del Acto 4 (monai4.inc) */
    int8_t  a4_hit;         /* A4H_*: lo que hace en el cuadro de golpe de la animación en curso */
    int8_t  a4_k;           /* con su habilidad k (Skill1 = 0) */
    float   a4_x, a4_y;     /* dónde (el nido de la madre, la comida del Corpulento) */
    int     a4_corpse;      /* el cadáver que come */
    int     inferno_end;    /* el aliento sostenido (MonInferno, DiabLight): hasta ese tick (s_unit_ticks) */
    int     inferno_frame;  /* en ese cuadro de la animación (monstats2.txt InfernoAnim) */
    int     inferno_len;    /* y lo que dura cada llama (cuadros) */
    int     bone_armor;     /* lo que le queda a la Armadura de hueso (MonBoneArmor) */
    int8_t  a4_curse;       /* A4C_*: la maldición que tira en el cuadro de golpe */
    uint8_t soft;           /* su cadáver se come (monstats2.txt soft: el Corpulento) */

    /* Efectos de las habilidades de los jugadores */
    int flee_ticks;  /* Aullido: escapa de los jugadores */
    int stun_ticks;  /* Aturdir: no hace nada */
    int curse_ticks; /* Percepción / Grito de batalla */
    int curse_def_flat, curse_def_pct, curse_dmg_pct, curse_ar_pct;
    int searched;    /* el cadáver ya se revisó (Buscar poción / objeto) */
    int chill_ticks; /* enfriado: hace todo a la mitad de velocidad */
    int slow_ticks, slow_pct, slow_acc; /* frenado (Empalar): se pierde ese % de los ticks */
    int slowmis_ticks, slowmis_pct;     /* Ralentizar proyectiles: sus proyectiles van a ese % */
    int poison_ticks, poison_owner;
    float poison_rate, poison_acc; /* vida que pierde por tick */
    int bleed_ticks, bleed_owner;  /* heridas abiertas (los objetos) */
    float bleed_rate, bleed_acc;
    int link_group, link_pct, link_owner, link_ticks; /* almas unidas (el Profanador del Brujo) */
    int haste_ticks, haste_pct, haste_acc; /* más rápido: un tick de más cada tanto (Frenesí, Sigilo de rencor) */
    int mark_ticks, mark_flat;     /* Marca de la muerte: más daño en cada golpe */
    int pet_ias, pet_pdr;          /* el Brujo: velocidad de ataque y daño físico que no recibe */
    int war_goat, berserk_ticks;   /* el hombre cabra: su nivel; Furia (sin defensa mientras dura) */
    int buff_ticks, buff_pdr, buff_def, buff_regen, buff_steal; /* Atiborrarse */
    int enrage_ticks, enrage_dmg;  /* Sigilo de rencor: enfurecido */
    int lust_ticks;                /* el látigo del capataz (bloodlust): +50 % de defensa (lo demás, con haste y enrage) */
    int pregnant;                  /* preñado por una madre pútrida (Impregnate): al morir sale un gusano */
    int ride, rider;               /* el diablillo montado: su bestia de asedio + 1; la bestia: su diablillo + 1 */
    int shout_ticks, shout_pct;    /* el Grito de Madawc: más defensa (skill_armor_percent) */
    int goal;                      /* un neutral que va hacia (goal_x, goal_y): los presos del Acto 5 a su portal */
    float goal_x, goal_y;
    int no_bind;                   /* jefe de misión o de nombre propio: no se lo vincula */
    float buff_regen_acc;
    int conv_ticks, conv_def_pct, conv_res; /* Convicción: menos defensa y resistencias */
    int convert_ticks;  /* Conversión: pelea para un jugador hasta que se termina */
    AllyAura au;        /* aliados: lo que les dan las auras del Paladín */
    /* Los esbirros del Nigromante */
    int   pet_ticks;          /* le queda (Revivir, los muros de hueso); 0 = siempre */
    int   pet_static;         /* el Muro y la Prisión de hueso: no se mueve ni pelea */
    int   barb;               /* aliado sin dueño: los bárbaros del Acto 5 (barb_spawn) */
    int   pet_slow;           /* gólem de arcilla: % que frena al que golpea */
    int   pet_steal, pet_steal_owner; /* gólem de sangre */
    int   pet_thorns;         /* gólem de hierro */
    int   pet_fire_min, pet_fire_max; /* gólem de fuego */
    int   pet_elem, pet_emin, pet_emax, pet_elen; /* el Druida: frío de los lobos, veneno de la enredadera */
    int   pet_hits;           /* los cuervos: golpes que les quedan (0 = sin límite) */
    int   revivable;          /* revive de monstats2.txt */
    int   cloak_ticks, cloak_def, cloak_blind; /* Manto de sombras: defensa que les baja y si los ciega */
    int   dark;               /* el Guerrero de las sombras: se dibuja oscuro */
    const PalShift *pshift;   /* sus colores (palshift.dat de su carpeta, unit_palshift); NULL = los de siempre */
    int   light;              /* radio de su luz (monstats2.txt Light): Duriel, el Invocador, los magos... */
    /* Las maldiciones del Nigromante */
    int   nc_kind, nc_ticks, nc_owner;
    int   nc_phys, nc_dmg, nc_res, nc_im, nc_lt;
    int   mm_target, mm_ticks, mm_attack; /* Confusión y Atraer: ataca a otro monstruo */
    int undead, demon;  /* lUndead/hUndead y demon de monstats.txt (Rayo sagrado, Santuario) */

    /* Ataques a distancia (ratas de púas, chamanes) */
    char missile[24];
    int  ranged;      /* tira proyectiles */
    int  ranged_anim; /* con qué animación */
    int  attack_anim; /* animación del ataque en curso */
    int  ranged_attack;
    int  a2_min, a2_max;
    int  resurrect;   /* chamán: revive a los Caídos */
    int  spray;       /* proyectiles en abanico por disparo (Andariel) */
    uint32_t mods;    /* modificadores de jefe (MOD_*) */
    int  boom;        /* murió encantado: explota en el próximo tick */
    int  poison_hit;  /* su golpe envenena (Andariel) */
    /* Mercenaria: si va siguiendo al dueño y a qué velocidad va él */
    int   following, close; /* close: pegada al dueño (no camina hasta separarse un poco) */
    float own_px, own_py, own_v;
    int  res_target;
    char missile2[24]; /* el otro proyectil (la flecha de fuego de Blood Raven) */
    int  spell_elem, spell_min, spell_max; /* el hechizo que lanza ahora (el Invocador): su daño, no el del ataque */
    int  spell_dmin, spell_dmax, spell_flags; /* (y lo físico y cómo vuela: los rayos cargados de Mefisto) */
    int  ring;                             /* ... y en anillo (la Nova helada): cuántos */
    int  raiser;       /* Blood Raven: levanta zombis */
    int  seen;         /* ya vio a un jugador (los jefes lo saludan) */
    int  taunt_cd;     /* ticks hasta la próxima frase del jefe */
    char minion[24];
    int  minion_level; /* los zombis salen con el nivel de la zona, no el de ella */
    int  raise_cd;
    int  master;       /* quién lo levantó (-1 = nadie) */
    UnitGfx *gfx_on, *gfx_off; /* waypoints: encendido y apagado */
    UnitGfx *gfx_parts;        /* las manos de los caballeros que no son las de gfx: se dibujan encima */
    int8_t   comp_s3;          /* cuál de las opciones de S3 tiene (el proyectil de DoomKnightMissile) */
    const SkillDef *aura_sk;   /* el aura de un jefe encantado (MOD_AURA): la del Paladín, a aura_lvl */
    int      aura_lvl, aura_t;
    int      ba_ticks, ba_dmg, ba_ar, ba_ias; /* lo que le da el aura de un jefe de su lado (Poder, Puntería bendita, Fanatismo) */
    int      obj_row;          /* objetos: su fila de objects.txt (para cambiarles el modo) */
    char     obj_next[4];      /* modo que sigue cuando termina el actual ("op" -> "on") */
    /* Qué cuadros del COF muestra el modo actual (FrameCnt, Start y
       CycleAnim de objects.txt): las Piedras Cairn traen apagada, prendiéndose
       y prendida en los mismos 4 cuadros, y sin esto titilaban */
    int      obj_first, obj_frames, obj_cycle;
    int      obj_mode;         /* el modo de ahora (0 nu, 1 op, 2 on...: k_obj_modes) */
    int      obj_state;        /* lo que guarda objects.inc: trampa, cerrado, santuario, cargas del pozo */

    /* Mercenaria de un jugador (y la Valquiria, `pet`: pelea cuerpo a cuerpo) */
    int       merc, pet, owner, hidden;
    int       turret, shot_delay; /* la Hidra: dispara quieta cada shot_delay ticks */
    int       running;
    float     run_speed;
    MercStats ms;
    int       regen_acc;

    /* Sonidos */
    const MonSounds *snd;
    int  snd_id[SOUND_PENDING], snd_delay[SOUND_PENDING], snd_vol[SOUND_PENDING]; /* por sonar */
    int  neutral_timer;
    int  step_frame;
} Unit;

/* Monstruos que murieron por veneno: la experiencia la reparte game.c */
static struct { int unit, owner, exp; } s_kills[MAX_KILLS];
static int s_num_kills;

static TxtTable   s_objpreset, s_monpreset, s_objects, s_monstats, s_monstats2, s_monlvl, s_superuniques;
static TxtTable   s_monsounds, s_monequip, s_skilltab, s_monumod, s_difflv;

/* La dificultad (docs/plan-dificultades.md): las columnas de monstats.txt
   y superuniques.txt con "(N)" o "(H)", las de la expansión de monlvl.txt
   ("L-HP(N)") y la fila de difficultylevels.txt */
static int s_diff;
static int s_cold_div = 1, s_freeze_div = 1; /* MonsterColdDivisor y MonsterFreezeDivisor */

static int diff_int(const char *col, int def);

void units_set_difficulty(int diff)
{
    s_diff = diff < 0 ? 0 : diff > 2 ? 2 : diff;
    s_cold_div = diff_int("MonsterColdDivisor", 1);
    s_freeze_div = diff_int("MonsterFreezeDivisor", 1);
    s_cold_div = s_cold_div > 0 ? s_cold_div : 1;
    s_freeze_div = s_freeze_div > 0 ? s_freeze_div : 1;
}

int units_difficulty_int(const char *col, int def)
{
    return diff_int(col, def);
}

/* El nombre de la columna en esta dificultad ("minHP" -> "minHP(N)"), si
   la tabla la tiene; si no, la de siempre */
static const char *dcol(const TxtTable *t, const char *col)
{
    static char ring[8][40];
    static unsigned next;
    if (s_diff == 0)
        return col;
    char *b = ring[next++ & 7];
    snprintf(b, sizeof ring[0], "%s(%c)", col, s_diff == 1 ? 'N' : 'H');
    return txt_col(t, b) >= 0 ? b : col;
}

/* La columna de monlvl.txt: en Normal la de siempre (igual a la de la
   expansión); en las otras, la de la expansión ("L-DM(H)") */
static const char *lvcol(const char *col)
{
    static char ring[8][16];
    static unsigned next;
    if (s_diff == 0)
        return col;
    char *b = ring[next++ & 7];
    snprintf(b, sizeof ring[0], "L-%s(%c)", col, s_diff == 1 ? 'N' : 'H');
    return b;
}

/* Una columna de difficultylevels.txt en esta dificultad */
static int diff_int(const char *col, int def)
{
    return s_difflv.rows > s_diff + 1 ? txt_get_int(&s_difflv, s_diff + 1, col, def) : def;
}

/* "constants" de la fila `id` de monumod.txt (la vida de más de los jefes, por dificultad) */
static int umod_const(int id, int def)
{
    return s_monumod.rows > id + 1 ? txt_get_int(&s_monumod, id + 1, "constants", def) : def;
}
static MonSounds  s_mon_sounds[MAX_MON_SOUNDS];
static uint8_t    s_mon_sounds_ready[MAX_MON_SOUNDS];
static UnitGfx    s_gfx[MAX_GFX];
static int        s_num_gfx;
static uint32_t   s_gfx_clock;


/* Alineado a 64 (una línea de caché; Unit mide 25): si no, queda donde caiga
   según lo que haya antes y en la Pi la lógica por tick variaba un 11 % (al
   agregar 24 bytes de otra variable en map.c pasó de 0,93 a 1,04 ms) */
static Unit       s_units[MAX_UNITS] __attribute__((aligned(64)));
/* Antes de este lugar no hay ninguno libre: buscar desde el principio
   recorría miles de unidades por cada una nueva (al armar una zona, en la Pi,
   decenas de ms) */
static int        s_free_from;
static int        s_num_units;
static const Map *s_map;
static uint32_t   s_spawn_rng = 12345;
static uint32_t   s_unit_ticks;                 /* ticks de units_tick (los tiempos de las IA) */
static int      (*s_level_id_at)(float x, float y); /* el Id de levels.txt de ese lugar (lo da el juego) */

void units_set_level_query(int (*level_id_at)(float x, float y))
{
    s_level_id_at = level_id_at;
}
static int        s_spawn_zone = -1; /* zona de las unidades que se crean ahora */
static float dist_to(const Unit *u, float x, float y);
static void  set_state(Unit *u, int state);
static int   in_radius(const Unit *u, float x, float y, float radius);
static const char *(*s_picker)(float x, float y); /* un monstruo de la zona para los grupos de los DS1 */

static int load_table(TxtTable *t, const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "data/global/excel/%s.txt", name);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int r = d ? txt_load(d, n, t) : -1;
    free(d);
    if (r != 0)
        fprintf(stderr, "falta %s\n", path);
    return r;
}

/* Las secuencias de monseq.txt (la Estocada, el salto del saltador, la
   carga de la víbora, el gusano que se entierra...): qué cuadro de qué modo
   se ve en cada paso y en cuáles pega (event 1) */
#define SEQ_STEPS 32
typedef struct MonSeq {
    char    name[32];
    int     n;
    uint8_t frame[SEQ_STEPS], hit[SEQ_STEPS];
    int8_t  anim[SEQ_STEPS]; /* ANIM_* */
} MonSeq;
static MonSeq s_seqs[32];
static int    s_num_seqs;
static TxtTable s_monseq; /* monseq.txt, leída en units_init */

static const MonSeq *monseq_get(const char *name)
{
    static const char *const k_modes[] = { "NU", "WL", "A1", "GH", "DT", "DD", "A2", "S1", "RN", "SC", "S2", "S3", "S4" }; /* ANIM_* */
    for (int i = 0; i < s_num_seqs; i++)
        if (strcmp(s_seqs[i].name, name) == 0)
            return s_seqs[i].n > 0 ? &s_seqs[i] : NULL;
    const TxtTable *t = &s_monseq;
    if (s_num_seqs >= (int)(sizeof s_seqs / sizeof s_seqs[0]) || !t->rows)
        return NULL;
    MonSeq *q = &s_seqs[s_num_seqs++]; /* también las que no están (n = 0): no se vuelve a buscar */
    memset(q, 0, sizeof *q);
    snprintf(q->name, sizeof q->name, "%s", name);
    int cs = txt_col(t, "sequence"), cm = txt_col(t, "mode");
    for (int r = 1; r < t->rows && q->n < SEQ_STEPS; r++) {
        if (strcmp(txt_get(t, r, cs), name) != 0)
            continue;
        const char *mode = txt_get(t, r, cm);
        int a = ANIM_ATTACK; /* (un modo que no está: con el primer ataque) */
        for (int k = 0; k < (int)(sizeof k_modes / sizeof k_modes[0]); k++)
            if (strcmp(k_modes[k], mode) == 0)
                a = k;
        q->anim[q->n] = (int8_t)a;
        q->frame[q->n] = (uint8_t)txt_get_int(t, r, "frame", 0);
        q->hit[q->n++] = txt_get_int(t, r, "event", 0) == 1;
    }
    return q->n > 0 ? q : NULL;
}

/* Los fanáticos huyen de los jugadores después de que muere el Consejo (A3Q5) */
static int s_zealots_flee;

void units_zealots_flee(int on)
{
    s_zealots_flee = on;
}

/* Las IA del original que tiene el motor (monai.inc): la de cada monstruo sale de la columna AI de monstats.txt */
enum {
    AI_NONE, AI_SANDLEAPER, AI_PANTHER, AI_PANTHERJAV, AI_SCARAB, AI_VULTURE, AI_MAGGOT, AI_MAGGOTEGG,
    AI_MAGGOTLARVA, AI_SWARM, AI_CLAWVIPER, AI_MUMMY, AI_GMUMMY, AI_SANDRAIDER, AI_MAGGOTQUEEN, AI_BATDEMON,
    AI_DURIEL, AI_SUMMONER, AI_PINHEAD,
    AI_FETISH, AI_FETISHBLOW, AI_FETISHSHAMAN, AI_BABOON, AI_THORNHULK, AI_MOSQUITO, AI_WISP, AI_ARACH, AI_FROG,
    AI_ZEALOT, AI_PRIEST, AI_VAMPIRE, AI_HIGHPRIEST, AI_MEPHISTO, AI_TENTACLE, AI_TENTHEAD, /* el Acto 3 (monai3.inc) */
    AI_SKELETON, AI_ZOMBIE, AI_BLOODHAWK, AI_BRUTE, AI_WRAITH, AI_CROGUE, AI_CARCHER, /* del Acto 1 (monai1.inc) */
    AI_VILEMOTHER, AI_VILEDOG, AI_FINGERMAGE, AI_REGURG, AI_DOOMKNIGHT, AI_ABYSSKNIGHT, AI_OBLIVKNIGHT,
    AI_MEGADEMON, AI_BIGHEAD, AI_IZUAL, AI_SMITH, AI_HELLMETEOR, AI_TRAPPEDSOUL, AI_DIABLO, /* el Acto 4 (monai4.inc) */
    AI_MINION, AI_SUICIDEMINION, AI_SUCCUBUS, AI_SUCCWITCH, AI_IMP, AI_BLOODLORD, AI_REANIMATED, AI_OVERSEER,
    AI_DEATHMAULER, AI_SIEGEBEAST, AI_PUTRID, AI_FROZENHORROR, AI_CATAPULT, AI_SPOTTER, AI_MINIONSPAWNER,
    AI_EVILHOLE, AI_GENSPAWNER, AI_NIHLATHAK, AI_ANCIENT, AI_ANCSTATUE, AI_BAAL, AI_BAALCLONE, AI_BAALTHRONE,
    AI_BAALTENTACLE, AI_BAALMINION, AI_BAALSTAIRS, AI_BAALTAUNT, AI_SIEGETOWER, AI_NPCBARB, AI_WUSSIE, /* el Acto 5 (monai5.inc) */
};

static int ai_kind(const char *name)
{
    static const struct { const char *name; int kind; } k_ai[] = {
        { "SandLeaper", AI_SANDLEAPER },   { "PantherWoman", AI_PANTHER },  { "PantherJavelin", AI_PANTHERJAV },
        { "Scarab", AI_SCARAB },           { "Vulture", AI_VULTURE },       { "SandMaggot", AI_MAGGOT },
        { "MaggotEgg", AI_MAGGOTEGG },     { "MaggotLarva", AI_MAGGOTLARVA }, { "Swarm", AI_SWARM },
        { "ClawViper", AI_CLAWVIPER },     { "Mummy", AI_MUMMY },           { "GreaterMummy", AI_GMUMMY },
        { "SandRaider", AI_SANDRAIDER },   { "SandMaggotQueen", AI_MAGGOTQUEEN }, { "BatDemon", AI_BATDEMON },
        { "Duriel", AI_DURIEL },           { "Summoner", AI_SUMMONER },     { "PinHead", AI_PINHEAD },
        { "Fetish", AI_FETISH },           { "FetishBlowgun", AI_FETISHBLOW }, { "FetishShaman", AI_FETISHSHAMAN },
        { "Baboon", AI_BABOON },           { "ThornHulk", AI_THORNHULK },   { "Mosquito", AI_MOSQUITO },
        { "WillOWisp", AI_WISP },          { "Arach", AI_ARACH },           { "FrogDemon", AI_FROG },
        { "ZakarumZealot", AI_ZEALOT },    { "ZakarumPriest", AI_PRIEST },  { "Vampire", AI_VAMPIRE },
        { "HighPriest", AI_HIGHPRIEST },   { "Mephisto", AI_MEPHISTO },     { "Tentacle", AI_TENTACLE },
        { "TentacleHead", AI_TENTHEAD },   { "Skeleton", AI_SKELETON },     { "Zombie", AI_ZOMBIE },
        { "BloodHawk", AI_BLOODHAWK },     { "Brute", AI_BRUTE },           { "Wraith", AI_WRAITH },
        { "CorruptRogue", AI_CROGUE },     { "CorruptArcher", AI_CARCHER },
        { "VileMother", AI_VILEMOTHER },  { "VileDog", AI_VILEDOG },      { "FingerMage", AI_FINGERMAGE },
        { "Regurgitator", AI_REGURG },    { "DoomKnight", AI_DOOMKNIGHT }, { "AbyssKnight", AI_ABYSSKNIGHT },
        { "OblivionKnight", AI_OBLIVKNIGHT }, { "Megademon", AI_MEGADEMON }, { "Bighead", AI_BIGHEAD },
        { "Izual", AI_IZUAL },            { "Smith", AI_SMITH },          { "HellMeteor", AI_HELLMETEOR },
        { "TrappedSoul", AI_TRAPPEDSOUL }, { "Diablo", AI_DIABLO },
        { "Minion", AI_MINION },          { "SuicideMinion", AI_SUICIDEMINION }, { "Succubus", AI_SUCCUBUS },
        { "SuccubusWitch", AI_SUCCWITCH }, { "Imp", AI_IMP },             { "BloodLord", AI_BLOODLORD },
        { "ReanimatedHorde", AI_REANIMATED }, { "Overseer", AI_OVERSEER }, { "DeathMauler", AI_DEATHMAULER },
        { "SiegeBeast", AI_SIEGEBEAST },  { "PutridDefiler", AI_PUTRID }, { "FrozenHorror", AI_FROZENHORROR },
        { "Catapult", AI_CATAPULT },      { "CatapultSpotter", AI_SPOTTER }, { "MinionSpawner", AI_MINIONSPAWNER },
        { "EvilHole", AI_EVILHOLE },      { "GenericSpawner", AI_GENSPAWNER }, { "Nihlathak", AI_NIHLATHAK },
        { "Ancient", AI_ANCIENT },        { "AncientStatue", AI_ANCSTATUE }, { "BaalCrab", AI_BAAL },
        { "BaalCrabClone", AI_BAALCLONE }, { "BaalThrone", AI_BAALTHRONE }, { "BaalTentacle", AI_BAALTENTACLE },
        { "BaalMinion", AI_BAALMINION },  { "BaalToStairs", AI_BAALSTAIRS }, { "BaalTaunt", AI_BAALTAUNT },
        { "SiegeTower", AI_SIEGETOWER },  { "NpcBarb", AI_NPCBARB },     { "Wussie", AI_WUSSIE },
    };
    for (unsigned i = 0; i < sizeof k_ai / sizeof k_ai[0]; i++)
        if (strcmp(k_ai[i].name, name) == 0)
            return k_ai[i].kind;
    return AI_NONE;
}


int units_init(void)
{
    int r = 0;
    r |= load_table(&s_objpreset, "objpreset");
    r |= load_table(&s_monpreset, "monpreset");
    r |= load_table(&s_objects, "objects");
    r |= load_table(&s_monstats, "monstats");
    r |= load_table(&s_monstats2, "monstats2");
    r |= load_table(&s_monlvl, "monlvl");
    r |= load_table(&s_superuniques, "superuniques");
    load_table(&s_monsounds, "monsounds"); /* sin ella, los monstruos no suenan */
    load_table(&s_monequip, "monequip");   /* sin ella, la Valquiria va sin equipo */
    /* Las que antes se leían al usarse por primera vez, en medio del juego
       (en la Pi, skills.txt eran unos 20 ms en un tick) */
    load_table(&s_monseq, "monseq");
    load_table(&s_skilltab, "skills");
    load_table(&s_monumod, "monumod");          /* sin ella, los jefes con la vida de Normal */
    load_table(&s_difflv, "difficultylevels"); /* sin ella, todo como en Normal */
    return r;
}

static int msnd(int row, const char *col)
{
    return audio_find(txt_get(&s_monsounds, row, txt_col(&s_monsounds, col)));
}

/* Sonidos de la fila de monsounds.txt con ese Id (decodificados de una vez) */
static const MonSounds *mon_sounds(const char *id)
{
    int row = id[0] ? txt_find_row(&s_monsounds, txt_col(&s_monsounds, "Id"), id) : -1;
    if (row < 0 || row >= MAX_MON_SOUNDS)
        return NULL;
    MonSounds *s = &s_mon_sounds[row];
    if (s_mon_sounds_ready[row])
        return s;
    s_mon_sounds_ready[row] = 1;
    const TxtTable *t = &s_monsounds;
    static const char *const atk[2][6] = {
        { "Attack1", "Weapon1", "Att1Del", "Wea1Del", "Att1Prb", "Wea1Vol" },
        { "Attack2", "Weapon2", "Att2Del", "Wea2Del", "Att2Prb", "Wea2Vol" },
    };
    for (int a = 0; a < 2; a++) {
        s->attack[a] = msnd(row, atk[a][0]);
        s->weapon[a] = msnd(row, atk[a][1]);
        s->att_del[a] = txt_get_int(t, row, atk[a][2], 0);
        s->wea_del[a] = txt_get_int(t, row, atk[a][3], 0);
        s->att_prb[a] = txt_get_int(t, row, atk[a][4], 100);
        s->wea_vol[a] = txt_get_int(t, row, atk[a][5], 255);
    }
    s->hit = msnd(row, "HitSound");
    s->death = msnd(row, "DeathSound");
    s->hit_del = txt_get_int(t, row, "HitDelay", 0);
    s->death_del = txt_get_int(t, row, "DeaDelay", 0);
    static const char *const skills[4] = { "Skill1", "Skill2", "Skill3", "Skill4" };
    for (int k = 0; k < 4; k++)
        s->skill[k] = msnd(row, skills[k]);
    s->footstep = msnd(row, "Footstep");
    s->fs_cnt = txt_get_int(t, row, "FsCnt", 0);
    s->fs_off = txt_get_int(t, row, "FsOff", 0);
    s->fs_prb = txt_get_int(t, row, "FsPrb", 100);
    s->neutral = msnd(row, "Neutral");
    s->neu_time = txt_get_int(t, row, "NeuTime", 0);
    s->flee = msnd(row, "Flee");
    s->init = msnd(row, "Init");
    s->taunt[0] = msnd(row, "Taunt");
    s->taunt[1] = -1;
    const char *taunt = txt_get(&s_monsounds, row, txt_col(&s_monsounds, "Taunt"));
    size_t tl = taunt ? strlen(taunt) : 0;
    if (tl > 2 && strcmp(taunt + tl - 2, "_1") == 0) { /* monster_andariel_taunt_1 -> _2 */
        char other[64];
        snprintf(other, sizeof other, "%.*s_2", (int)(tl - 2), taunt);
        s->taunt[1] = audio_find(other);
    }
    /* Andariel: su lanzamiento (AndyPoisonBolt no trae sonido en skills.txt) */
    char cast[64];
    snprintf(cast, sizeof cast, "%s_cast_small", id);
    s->cast = audio_find(cast);
    const int all[] = { s->attack[0], s->attack[1], s->weapon[0], s->weapon[1], s->hit, s->death, s->skill[0],
                        s->skill[1], s->footstep, s->neutral, s->flee, s->init, s->taunt[0], s->taunt[1], s->cast };
    for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++)
        audio_preload(all[k]);
    return s;
}

/* Toca un sonido del monstruo ahora o dentro de `delay` ticks */
static void unit_sound(Unit *u, int id, int delay, int vol)
{
    if (id < 0)
        return;
    if (delay > 0)
        for (int k = 0; k < SOUND_PENDING; k++)
            if (u->snd_delay[k] <= 0) {
                u->snd_id[k] = id;
                u->snd_delay[k] = delay;
                u->snd_vol[k] = vol;
                return;
            }
    audio_play_vol(id, u->x, u->y, vol);
}

static uint32_t rnd(Unit *u)
{
    u->rng = u->rng * 1664525u + 1013904223u;
    return u->rng >> 8;
}

static float rndf(Unit *u)
{
    return (float)(rnd(u) & 0xFFFF) / 65535.0f;
}

static uint32_t spawn_rnd(void)
{
    s_spawn_rng = s_spawn_rng * 1664525u + 1013904223u;
    return s_spawn_rng >> 8;
}

static void lower_copy(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Valores como "\"sbw,lbw\"" (varias opciones): se queda con la primera */
static void first_option(char *dst, size_t cap, const char *src)
{
    while (*src == '"')
        src++;
    size_t i = 0;
    for (; src[i] && src[i] != ',' && src[i] != '"' && i + 1 < cap; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* La opción n (desde 0) de una lista "a,b,c" en dst; devuelve cuántas hay */
static int nth_option(char *dst, size_t cap, const char *src, int n)
{
    int count = 0;
    dst[0] = '\0';
    while (*src == '"')
        src++;
    while (*src && *src != '"') {
        if (count == n)
            first_option(dst, cap, src);
        count++;
        const char *comma = strchr(src, ',');
        if (!comma)
            break;
        src = comma + 1;
    }
    return count;
}

/* Las columnas de monstats2.txt con las opciones de cada pieza */
static const char *const k_part_cols[COMP_COUNT] = {
    "HDv", "TRv", "LGv", "Rav", "Lav", "RHv", "LHv", "SHv",
    "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v",
};

static int gfx_state(const UnitGfx *g)
{
    return g ? job_flag_get(&g->state) : GFX_FAILED;
}

static int gfx_ready(const UnitGfx *g)
{
    return gfx_state(g) == GFX_READY;
}

/* ¿Tiene esa animación? Mientras se carga, la que se pidió */
static int gfx_has(const UnitGfx *g, int a)
{
    return gfx_ready(g) ? g->anim[a].ok : g && g->modes[a] != NULL;
}

static size_t anim_bytes(const CharAnim *a)
{
    size_t n = 0;
    for (int c = 0; c < COMP_COUNT; c++) {
        const Sprite *s = &a->layer[c];
        for (int f = 0; s->frames && f < s->directions * s->frames_per_dir; f++)
            n += (size_t)(s->frames[f].width * s->frames[f].height) + sizeof(SpriteFrame);
    }
    return n;
}

/* Decodifica las animaciones (en el hilo de carga o, sin él, ahí mismo) */
static void gfx_decode(UnitGfx *g)
{
    int chars = g->look.base[0] == '\0' || strcmp(g->look.base, "chars") == 0;
    for (int a = 0; a < ANIM_COUNT; a++)
        if (g->modes[a]) /* los personajes mueren sin arma: no hay muerte con otra clase */
            charanim_load(&g->anim[a], &g->look, g->modes[a],
                          chars && (a == ANIM_DEATH || a == ANIM_DEAD) ? "hth" : g->wclass);
    if (strcmp(g->look.base, "objects") != 0) { /* caminan y corren al paso de su velocidad */
        g->anim[ANIM_WALK].speed = (int)((float)g->anim[ANIM_WALK].speed * MON_WALK_BONUS + 0.5f);
        g->anim[ANIM_RUN].speed = (int)((float)g->anim[ANIM_RUN].speed * MON_RUN_BONUS + 0.5f);
    }
    if (!g->anim[ANIM_IDLE].ok) {
        for (int a = 0; a < ANIM_COUNT; a++)
            charanim_free(&g->anim[a]);
        fprintf(stderr, "sin gráficos para %s\n", g->key);
        job_flag_set(&g->state, GFX_FAILED);
        return;
    }
    size_t bytes = 0;
    for (int a = 0; a < ANIM_COUNT; a++)
        bytes += anim_bytes(&g->anim[a]);
    g->bytes = bytes;
    job_flag_set(&g->state, GFX_READY);
}

static void gfx_job(void *ptr, int arg)
{
    (void)arg;
    gfx_decode(ptr);
}

int units_loading(void)
{
    int n = 0;
    for (int i = 0; i < s_num_gfx; i++)
        n += gfx_state(&s_gfx[i]) == GFX_LOADING;
    return n;
}

static void gfx_release(UnitGfx *g)
{
    for (int a = 0; a < ANIM_COUNT; a++)
        charanim_free(&g->anim[a]);
    if (getenv("D2_LIST_GFX"))
        printf("gráficos liberados: %s\n", g->key);
    memset(g, 0, sizeof *g);
}

/* Los que no usa nadie quedan guardados hasta GFX_CACHE_BYTES; los que no
   se pudieron cargar se sueltan enseguida */
static void gfx_gc(int keep)
{
    for (;;) {
        size_t idle = 0;
        int oldest = -1;
        for (int i = 0; i < s_num_gfx; i++) {
            UnitGfx *g = &s_gfx[i];
            if (!g->key[0] || g->refs > 0 || i == keep)
                continue;
            int st = gfx_state(g);
            if (st == GFX_FAILED) {
                gfx_release(g);
                continue;
            }
            if (st != GFX_READY)
                continue;
            idle += g->bytes;
            if (oldest < 0 || g->idle_since < s_gfx[oldest].idle_since)
                oldest = i;
        }
        if (oldest < 0 || idle <= GFX_CACHE_BYTES)
            break;
        gfx_release(&s_gfx[oldest]);
    }
    while (s_num_gfx > 0 && !s_gfx[s_num_gfx - 1].key[0])
        s_num_gfx--;
}

/* Las animaciones de los modos pedidos (NULL = no cargar), cargadas una sola
   vez: si ya estaban (aunque nadie las use) se reusan */
static UnitGfx *get_gfx(const char *key, const CharLook *look, const char *const modes[ANIM_COUNT],
                        const char *wclass)
{
    int slot = -1;
    for (int i = 0; i < s_num_gfx; i++) {
        if (s_gfx[i].key[0] && strcmp(s_gfx[i].key, key) == 0)
            return gfx_state(&s_gfx[i]) == GFX_FAILED ? NULL : &s_gfx[i];
        if (slot < 0 && !s_gfx[i].key[0])
            slot = i; /* lo dejó libre una zona que se borró */
    }
    if (slot < 0 && s_num_gfx < MAX_GFX)
        slot = s_num_gfx;
    if (slot < 0) { /* lleno: se libera el guardado más viejo */
        for (int i = 0; i < s_num_gfx; i++)
            if (s_gfx[i].refs == 0 && gfx_state(&s_gfx[i]) != GFX_LOADING &&
                (slot < 0 || s_gfx[i].idle_since < s_gfx[slot].idle_since))
                slot = i;
        if (slot < 0)
            return NULL;
        gfx_release(&s_gfx[slot]);
    }

    UnitGfx *g = &s_gfx[slot];
    memset(g, 0, sizeof *g);
    snprintf(g->key, sizeof g->key, "%s", key);
    g->look = *look;
    snprintf(g->wclass, sizeof g->wclass, "%s", wclass);
    for (int a = 0; a < ANIM_COUNT; a++) {
        g->modes[a] = NULL;
        if (modes[a]) {
            snprintf(g->mode_buf[a], sizeof g->mode_buf[a], "%s", modes[a]);
            g->modes[a] = g->mode_buf[a];
        }
    }
    if (slot == s_num_gfx)
        s_num_gfx++;
    if (getenv("D2_LIST_GFX"))
        printf("gráficos: %s\n", key);
    if (jobs_running()) { /* en otro hilo: la unidad espera quieta */
        job_flag_set(&g->state, GFX_LOADING);
        jobs_push(gfx_job, g, 0);
        return g;
    }
    gfx_decode(g);
    if (gfx_state(g) == GFX_FAILED) {
        gfx_release(g);
        return NULL;
    }
    return g;
}

static void gfx_ref(UnitGfx *g, int add)
{
    if (!g)
        return;
    g->refs += add;
    if (g->refs > 0)
        return;
    g->refs = 0;
    g->idle_since = s_gfx_clock++;
    gfx_gc(-1);
}

/* Los gráficos que usa la unidad (los waypoints tienen dos) */
static void unit_gfx_refs(Unit *u, int add)
{
    if (u->gfx_on || u->gfx_off) {
        gfx_ref(u->gfx_on, add);
        gfx_ref(u->gfx_off, add);
    } else {
        gfx_ref(u->gfx, add);
        gfx_ref(u->gfx_parts, add);
    }
}

/* La fila de monstats.txt o monstats2.txt de un Id. Se busca varias veces
   por monstruo al armar una zona, y recorrer ~700 filas (separadas en
   memoria) eran ~50 us por búsqueda en la Pi. Solo desde el hilo del juego. */
static int mon_row(const TxtTable *t, const char *id)
{
    static struct {
        const TxtTable *t;
        char *const    *cells; /* (si se cargó de nuevo, cambia) */
        char            id[32];
        int             row;
    } memo[128];
    unsigned h = 5381;
    for (const char *c = id; *c; c++)
        h = h * 33u ^ (unsigned char)*c;
    h = (h ^ (unsigned)(uintptr_t)t) & 127u;
    if (memo[h].t == t && memo[h].cells == t->cells && t->cells && strcmp(memo[h].id, id) == 0)
        return memo[h].row;
    int row = txt_find_row(t, txt_col(t, "Id"), id);
    if (strlen(id) < sizeof memo[h].id) {
        memo[h].t = t;
        memo[h].cells = t->cells;
        snprintf(memo[h].id, sizeof memo[h].id, "%s", id);
        memo[h].row = row;
    }
    return row;
}

/* La grilla de las búsquedas de monstruos (units_enemies_near, a quién le
   pega cada proyectil): cada unidad en la lista de la celda de 4x4 subtiles
   donde está, en un toro de 64x64 celdas (las que están a 256 subtiles
   comparten lista y se descartan por el lugar). El fuego del piso y las nubes
   miraban las ~2000 unidades del acto por proyectil y por tick: en la Pi, un
   fallo de caché por unidad, 45 ms con 250 fuegos. El original también busca
   cerca y no en todas (las salas y las máscaras de colisión:
   MISSMODE_HandleMissileCollision, D2Common_10407).
   Se arma en la primera búsqueda y después cada lugar que cambia u->x o u->y
   llama a grid_moved. D2_GRID_CHECK=1 compara cada búsqueda con todas las
   unidades y corta si difiere. */
#define GRID_SHIFT      2    /* celdas de 4 subtiles */
#define GRID_SIDE       64
#define GRID_CANDS      256  /* más que esto en el recuadro: se miran todas */
#define GRID_SLACK      0.05f /* lo que puede correrse units_position(i, 1) del lugar, y el redondeo */
#define UNIT_RADIUS_MAX 1.0f /* el de los grandes (SizeX 3: 0.25 + 0.25 * 3) */
static int   s_grid_ok;
static int   s_grid_head[GRID_SIDE * GRID_SIDE];
static int   s_grid_next[MAX_UNITS], s_grid_cell[MAX_UNITS]; /* -1: no está (libre) */
static float s_grid_x[MAX_UNITS], s_grid_y[MAX_UNITS];

static int grid_cell_of(float x, float y)
{
    return (((int)floorf(x) >> GRID_SHIFT) & (GRID_SIDE - 1)) + (((int)floorf(y) >> GRID_SHIFT) & (GRID_SIDE - 1)) * GRID_SIDE;
}

/* Después de cambiarle el lugar a una unidad (o de liberarla) */
static void grid_moved(const Unit *u)
{
    if (!s_grid_ok)
        return;
    int i = (int)(u - s_units), c = u->zone == ZONE_FREE ? -1 : grid_cell_of(u->x, u->y);
    s_grid_x[i] = u->x;
    s_grid_y[i] = u->y;
    if (c == s_grid_cell[i])
        return;
    if (s_grid_cell[i] >= 0) {
        int *p = &s_grid_head[s_grid_cell[i]];
        while (*p != i)
            p = &s_grid_next[*p];
        *p = s_grid_next[i];
    }
    s_grid_cell[i] = c;
    if (c >= 0) {
        s_grid_next[i] = s_grid_head[c];
        s_grid_head[c] = i;
    }
}

/* Las unidades (no libres) cuyo lugar cae en el recuadro, con los bordes, de
   menor a mayor (como las recorría el resto); -1 si son más de GRID_CANDS o
   el recuadro es muy grande: ahí el que llama mira todas */
static int grid_box(float x0, float y0, float x1, float y1, int *out)
{
    if (!(x1 - x0 < 64.0f && y1 - y0 < 64.0f)) /* (y si viene un NaN) */
        return -1;
    if (!s_grid_ok) {
        memset(s_grid_head, -1, sizeof s_grid_head);
        memset(s_grid_cell, -1, sizeof s_grid_cell);
        s_grid_ok = 1;
        for (int i = 0; i < s_num_units; i++)
            grid_moved(&s_units[i]);
    }
    int n = 0, cx0 = (int)floorf(x0) >> GRID_SHIFT, cx1 = (int)floorf(x1) >> GRID_SHIFT;
    int cy0 = (int)floorf(y0) >> GRID_SHIFT, cy1 = (int)floorf(y1) >> GRID_SHIFT;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
            for (int i = s_grid_head[(cx & (GRID_SIDE - 1)) + (cy & (GRID_SIDE - 1)) * GRID_SIDE]; i >= 0; i = s_grid_next[i]) {
                if (s_grid_x[i] < x0 || s_grid_x[i] > x1 || s_grid_y[i] < y0 || s_grid_y[i] > y1)
                    continue;
                if (n == GRID_CANDS)
                    return -1;
                int k = n++;
                for (; k > 0 && out[k - 1] > i; k--)
                    out[k] = out[k - 1];
                out[k] = i;
            }
    static int check = -1;
    if (check < 0)
        check = getenv("D2_GRID_CHECK") != NULL;
    for (int i = 0, m = 0; check && i <= s_num_units; i++) {
        const Unit *u = &s_units[i];
        if (i < s_num_units && (u->zone == ZONE_FREE || u->x < x0 || u->x > x1 || u->y < y0 || u->y > y1))
            continue;
        if (i == s_num_units ? m != n : m >= n || out[m] != i) {
            fprintf(stderr, "D2_GRID_CHECK: la grilla difiere en (%.2f, %.2f)-(%.2f, %.2f): unidad %d (%s)\n", x0, y0, x1, y1,
                    i, i < s_num_units ? u->name : "-");
            abort();
        }
        m++;
    }
    return n;
}

static void unit_freed(int i)
{
    if (i < s_free_from)
        s_free_from = i;
    grid_moved(&s_units[i]);
}

static Unit *new_unit(float x, float y)
{
    int i = s_free_from;
    while (i < s_num_units && s_units[i].zone != ZONE_FREE)
        i++;
    if (i == MAX_UNITS)
        return NULL;
    s_free_from = i + 1;
    if (i == s_num_units)
        s_num_units++;
    Unit *u = &s_units[i];
    memset(u, 0, sizeof *u);
    u->zone = s_spawn_zone;
    u->x = u->prev_x = u->home_x = x;
    u->y = u->prev_y = u->home_y = y;
    grid_moved(u);
    u->rng = 0x9E3779B9u * (uint32_t)s_num_units + spawn_rnd();
    u->sector = (int)(rnd(u) % 16);
    u->timer = 25 + (int)(rnd(u) % 75);
    u->res_target = -1;
    u->master = -1;
    u->mm_target = -1;
    u->radius = UNIT_RADIUS;
    u->reach = MELEE_REACH;
    u->foot = 1;
    u->move_mask = COLL_MASK_MONSTER_PATH;
    return u;
}

/* Vida, daño y experiencia: porcentajes de monstats.txt sobre monlvl.txt
   (DATATBLS_CalculateMonsterStatsByLevel), los de la dificultad. En
   Pesadilla e Infierno los jefes de misión (boss) y los noRatio tienen el
   nivel de monstats ("Level(N)"); los demás, el de la zona (MONSTER_InitMonster) */
static void monster_stats(Unit *u, int ms, int level)
{
    const TxtTable *t = &s_monstats;
    int no_ratio = txt_get_int(t, ms, "noRatio", 0);
    if (level <= 0 || (s_diff > 0 && (no_ratio || txt_get_int(t, ms, "boss", 0) == 1)))
        level = txt_get_int(t, ms, dcol(t, "Level"), 1);
    char lv[8];
    snprintf(lv, sizeof lv, "%d", level);
    int ml = txt_find_row(&s_monlvl, txt_col(&s_monlvl, "Level"), lv);
    int base_hp = txt_get_int(&s_monlvl, ml, lvcol("HP"), 7);
    int base_dm = txt_get_int(&s_monlvl, ml, lvcol("DM"), 2);
    int base_xp = txt_get_int(&s_monlvl, ml, lvcol("XP"), 30);
    int base_ac = txt_get_int(&s_monlvl, ml, lvcol("AC"), 6), base_th = txt_get_int(&s_monlvl, ml, lvcol("TH"), 8);
    if (no_ratio) /* los esbirros: lo de monstats.txt tal cual (sin monlvl.txt) */
        base_hp = base_dm = base_ac = base_th = 100;
    u->level = level;
    u->ac = base_ac * txt_get_int(t, ms, dcol(t, "AC"), 100) / 100;
    u->ar = base_th * txt_get_int(t, ms, dcol(t, "A1TH"), 100) / 100;
    snprintf(u->tc, sizeof u->tc, "%s", txt_get(t, ms, txt_col(t, dcol(t, "TreasureClass"))));
    /* En el orden de ELEM_NONE (física), ELEM_FIRE, ELEM_COLD, ELEM_LTNG, ELEM_POIS y ELEM_MAG */
    static const char *const k_res_cols[6] = { "ResDm", "ResFi", "ResCo", "ResLi", "ResPo", "ResMa" };
    for (int e = 0; e < 6; e++)
        u->res[e] = txt_get_int(t, ms, dcol(t, k_res_cols[e]), 0);

    int min_hp = txt_get_int(t, ms, dcol(t, "minHP"), 50), max_hp = txt_get_int(t, ms, dcol(t, "maxHP"), 100);
    int pct = min_hp + (max_hp > min_hp ? (int)(rnd(u) % (uint32_t)(max_hp - min_hp + 1)) : 0);
    u->max_hp = u->hp = base_hp * pct / 100 > 0 ? base_hp * pct / 100 : 1;
    int a2min = txt_get_int(t, ms, dcol(t, "A2MinD"), 0);
    u->a2_min = base_dm * a2min / 100;
    u->a2_max = base_dm * txt_get_int(t, ms, dcol(t, "A2MaxD"), a2min) / 100;
    if (u->a2_min < 1 && a2min > 0)
        u->a2_min = 1;
    if (u->a2_max < u->a2_min) u->a2_max = u->a2_min;
    u->dmg_min = base_dm * txt_get_int(t, ms, dcol(t, "A1MinD"), 50) / 100;
    u->dmg_max = base_dm * txt_get_int(t, ms, dcol(t, "A1MaxD"), 100) / 100;
    if (u->dmg_min < 1) u->dmg_min = 1;
    if (u->dmg_max < u->dmg_min) u->dmg_max = u->dmg_min;
    u->exp = base_xp * txt_get_int(t, ms, dcol(t, "Exp"), 100) / 100;
    u->attack_delay = txt_get_int(t, ms, dcol(t, "aidel"), 15);
    u->s1_min = base_dm * txt_get_int(t, ms, dcol(t, "S1MinD"), 0) / 100;
    u->s1_max = base_dm * txt_get_int(t, ms, dcol(t, "S1MaxD"), 0) / 100;
    /* El daño elemental de cada ataque, como MONSTERMODE del original: del
       DM de monlvl.txt por el porcentaje de la tabla */
    static const char *const k_el_types[] = { "fire", "cold", "ltng", "pois", "mag", "mana", "rand" };
    for (int k = 0; k < 3; k++) {
        char col[12];
        snprintf(col, sizeof col, "El%dMode", k + 1);
        const char *mode = txt_get(t, ms, txt_col(t, col));
        u->el[k].anim = (int8_t)(!strcmp(mode, "A1") ? ANIM_ATTACK : !strcmp(mode, "A2") ? ANIM_ATTACK2
                                 : !strcmp(mode, "S1") ? ANIM_SKILL : -1);
        snprintf(col, sizeof col, "El%dType", k + 1);
        const char *type = txt_get(t, ms, txt_col(t, col));
        u->el[k].elem = 0;
        for (int e = 0; e < (int)(sizeof k_el_types / sizeof k_el_types[0]); e++)
            if (strcmp(type, k_el_types[e]) == 0)
                u->el[k].elem = (int8_t)(e + 1);
        snprintf(col, sizeof col, "El%dPct", k + 1);
        u->el[k].pct = (int16_t)txt_get_int(t, ms, dcol(t, col), 0);
        snprintf(col, sizeof col, "El%dMinD", k + 1);
        u->el[k].min = (int16_t)(base_dm * txt_get_int(t, ms, dcol(t, col), 0) / 100);
        snprintf(col, sizeof col, "El%dMaxD", k + 1);
        u->el[k].max = (int16_t)(base_dm * txt_get_int(t, ms, dcol(t, col), 0) / 100);
        snprintf(col, sizeof col, "El%dDur", k + 1);
        u->el[k].dur = (int16_t)txt_get_int(t, ms, dcol(t, col), 0);
        if (u->el[k].max < u->el[k].min)
            u->el[k].max = u->el[k].min;
    }
}

/* Las piezas, la clase de arma y los modos que se cargan de un monstruo
   (ms y ms2: sus filas de monstats y monstats2) */
/* Las IA del Acto 5 que usan S1, RN, SC o S2..S4 (los Sk*mode de monstats.txt
   y sus secuencias de monseq.txt, si monstats2.txt tiene el modo): sin
   cargarlos, sus habilidades no salían (ANIM_SKILL2 vale por S2, S3 y S4) */
static int a5_needs_mode(const char *ai, int anim)
{
    static const struct { const char *ai; uint8_t s1, rn, sc, s234; } k_ai[] = {
        { "Ancient", 1, 0, 1, 0 },      { "BaalCrab", 1, 0, 0, 1 },       { "BaalCrabClone", 1, 0, 0, 1 },
        { "BaalThrone", 0, 0, 0, 1 },   { "EvilHole", 0, 0, 0, 1 },       { "GenericSpawner", 1, 0, 0, 0 },
        { "Imp", 1, 0, 1, 1 },          { "Overseer", 1, 0, 0, 1 },       { "PutridDefiler", 1, 0, 0, 0 },
        { "ReanimatedHorde", 1, 1, 0, 0 }, { "Succubus", 0, 0, 0, 1 },    { "SuccubusWitch", 0, 0, 0, 1 },
    };
    for (int i = 0; i < (int)(sizeof k_ai / sizeof k_ai[0]); i++)
        if (strcmp(ai, k_ai[i].ai) == 0)
            return anim == ANIM_SKILL ? k_ai[i].s1 : anim == ANIM_RUN ? k_ai[i].rn : anim == ANIM_CAST ? k_ai[i].sc : k_ai[i].s234;
    return 0;
}

static void monster_look(int ms, int ms2, int enemy, int merc, CharLook *out, char *wclass_out, const char **modes)
{
    CharLook look;
    memset(&look, 0, sizeof look);
    strcpy(look.base, "monsters");
    lower_copy(look.token, sizeof look.token, txt_get(&s_monstats, ms, txt_col(&s_monstats, "Code")));
    const char *id = txt_get(&s_monstats, ms, txt_col(&s_monstats, "Id"));

    for (int c = 0; c < COMP_COUNT; c++) {
        if (txt_get_int(&s_monstats2, ms2, k_comp_names[c], 0) != 1)
            continue;
        first_option(look.part[c], sizeof look.part[c],
                     txt_get(&s_monstats2, ms2, txt_col(&s_monstats2, k_part_cols[c])));
        if (!look.part[c][0])
            strcpy(look.part[c], "lit");
    }
    char wclass[8];
    first_option(wclass, sizeof wclass, txt_get(&s_monstats2, ms2, txt_col(&s_monstats2, "BaseW")));
    if (!wclass[0])
        strcpy(wclass, "hth");

    static const char *const mode_cols[ANIM_COUNT] = { "mNU", "mWL", "mA1", "mGH", "mDT", "mDD", "mA2", "mS1", "mRN", "mSC",
                                                       "mS2", "mS3", "mS4" };
    static const char *const mode_codes[ANIM_COUNT] = { "nu", "wl", "a1", "gh", "dt", "dd", "a2", "s1", "rn", "sc", "s2", "s3", "s4" };
    for (int a = 0; a < ANIM_COUNT; a++)
        modes[a] = NULL;
    modes[0] = "nu";
    /* S1 solo hace falta para invocar (Blood Raven) y para salir del suelo
       (sus zombis): cargarla en todos ocupa mucha memoria */
    const char *ai_name = txt_get(&s_monstats, ms, txt_col(&s_monstats, "AI"));
    int need_s1 = merc || (enemy && (strcmp(ai_name, "BloodRaven") == 0 || strcmp(id, "zombie2") == 0 ||
                                     strcmp(ai_name, "FoulCrowNest") == 0 || strcmp(ai_name, "SandMaggot") == 0 ||
                                     strcmp(ai_name, "MaggotEgg") == 0 || strcmp(ai_name, "Vulture") == 0 ||
                                     strcmp(ai_name, "SandMaggotQueen") == 0 || strcmp(ai_name, "ZakarumPriest") == 0 ||
                                     strcmp(ai_name, "HighPriest") == 0 || strcmp(ai_name, "FrogDemon") == 0 ||
                                     strcmp(ai_name, "Tentacle") == 0 || strcmp(ai_name, "TentacleHead") == 0 ||
                                     strcmp(ai_name, "VileMother") == 0 || strcmp(ai_name, "FingerMage") == 0 ||
                                     strcmp(ai_name, "Regurgitator") == 0 || strcmp(ai_name, "AbyssKnight") == 0 ||
                                     strcmp(ai_name, "OblivionKnight") == 0 || strcmp(ai_name, "Megademon") == 0 ||
                                     strcmp(ai_name, "Diablo") == 0 || strcmp(ai_name, "TrappedSoul") == 0 ||
                                     a5_needs_mode(ai_name, ANIM_SKILL)));
    /* RN: los que corren hacia el jugador (las pícaras corruptas, los zombis) */
    int need_rn = merc || (enemy && (strcmp(ai_name, "CorruptRogue") == 0 || strcmp(ai_name, "CorruptArcher") == 0 ||
                                     strcmp(ai_name, "Zombie") == 0 || strcmp(ai_name, "Diablo") == 0 ||
                                     a5_needs_mode(ai_name, ANIM_RUN)));
    /* SC: los que lanzan hechizos (Andariel, los Desterrados) y la momia mayor que revive */
    int need_sc = enemy && (strcmp(ai_name, "Andariel") == 0 || strcmp(ai_name, "Vampire") == 0 ||
                            strcmp(ai_name, "GreaterMummy") == 0 || strcmp(ai_name, "Summoner") == 0 ||
                            strcmp(ai_name, "WillOWisp") == 0 || strcmp(ai_name, "FetishShaman") == 0 ||
                            strcmp(ai_name, "Izual") == 0 || strcmp(ai_name, "Diablo") == 0 || a5_needs_mode(ai_name, ANIM_CAST));
    /* S2..S4: los hechizos de Diablo (el frío, los muros, la prisión), las maldiciones del Caballero del
       Olvido y las almas atrapadas */
    int need_s234 = enemy && (strcmp(ai_name, "Diablo") == 0 || strcmp(ai_name, "OblivionKnight") == 0 ||
                              strcmp(ai_name, "TrappedSoul") == 0 || a5_needs_mode(ai_name, ANIM_SKILL2));
    for (int a = 1; a < ANIM_COUNT; a++)
        if ((a == ANIM_WALK || enemy || merc) && (a != ANIM_SKILL || need_s1) && (a != ANIM_RUN || need_rn) &&
            (a != ANIM_CAST || need_sc) && (a < ANIM_SKILL2 || need_s234) &&
            txt_get_int(&s_monstats2, ms2, mode_cols[a], 0) == 1)
            modes[a] = mode_codes[a];
    *out = look;
    strcpy(wclass_out, wclass);
}

/* Depuración (D2_ANIM_CHECK=1): carga las animaciones de cada monstruo de
   monstats.txt que tiene gráficos y avisa las que no están (por ejemplo, la
   muerte de un jefe que lo dejaría quieto al morir) */
void units_check_anims(void)
{
    static const char *const names[ANIM_COUNT] = { "nu", "wl", "a1", "gh", "dt", "dd", "a2", "s1", "rn", "sc", "s2", "s3", "s4" };
    int checked = 0, missing = 0;
    for (int ms = 1; ms < s_monstats.rows; ms++) {
        const char *id = txt_get(&s_monstats, ms, txt_col(&s_monstats, "Id"));
        int ms2 = mon_row(&s_monstats2, id);
        if (!id[0] || ms2 < 0)
            continue;
        CharLook look;
        char wclass[8], path[160];
        const char *modes[ANIM_COUNT];
        monster_look(ms, ms2, 1, 0, &look, wclass, modes);
        snprintf(path, sizeof path, "data/global/monsters/%s/cof/%snu%s.cof", look.token, look.token, wclass);
        size_t n;
        uint8_t *d = fs_read(path, &n);
        if (!d) {
            snprintf(path, sizeof path, "data/global/monsters/%s/cof/%snuhth.cof", look.token, look.token);
            d = fs_read(path, &n);
        }
        if (!d)
            continue; /* sin gráficos en los datos (de otros actos) */
        free(d);
        checked++;
        for (int a = 0; a < ANIM_COUNT; a++) {
            if (!modes[a])
                continue;
            CharAnim anim;
            if (charanim_load(&anim, &look, modes[a], wclass) != 0) {
                printf("animaciones: a %s (%s, %s) le falta %s\n", id, look.token, wclass, names[a]);
                missing++;
            }
            charanim_free(&anim);
        }
    }
    printf("animaciones: %d monstruos revisados, %d animaciones faltan\n", checked, missing);
}

/* enemy: 0 = NPC o animal, 1 = monstruo, 2 = mercenaria */
/* Los colores de un monstruo, como el original: las 8 tablas de palshift.dat
   (en la carpeta cof de sus gráficos) cambian unos colores de la paleta por
   otros. Los comunes usan la de TransLvl + 2 (las variantes: el Caído
   rojo, el Tallador, el Diablillo...); los jefes, la de Utrans de su
   dificultad (Normal). Se lee en el hilo de trabajos, como los gráficos
   (en la Pi eran de 0,2 a 4 ms en el hilo del juego por cada tipo nuevo):
   hasta que está, el monstruo no se dibuja. NULL si no tiene. */
static const PalShift *mon_palshift(const char *token, int ms, int ms2)
{
    static PalShift s_ps[PALSHIFT_MAX]; /* (las unidades apuntan acá: no se mueven) */
    static int s_nps;
    const char *ut = txt_get(&s_monstats2, ms2, txt_col(&s_monstats2, "Utrans"));
    int idx = ut[0] ? atoi(ut) : txt_get_int(&s_monstats, ms, "TransLvl", 0) + 2;
    if (idx < 0 || idx >= 8)
        return NULL;
    for (int k = 0; k < s_nps; k++)
        if (s_ps[k].idx == idx && strcmp(s_ps[k].token, token) == 0)
            return &s_ps[k];
    if (s_nps >= PALSHIFT_MAX)
        return NULL;
    PalShift *p = &s_ps[s_nps++];
    snprintf(p->token, sizeof p->token, "%s", token);
    p->idx = idx;
    job_flag_set(&p->state, PS_LOADING);
    jobs_push(palshift_job, p, 0);
    return p;
}

/* La tabla de colores de la unidad, si ya se leyó y cambia algo */
static const uint8_t *unit_palshift(const Unit *u)
{
    return u->pshift && job_flag_get(&u->pshift->state) == PS_READY ? u->pshift->tab : NULL;
}

/* Las manos de los caballeros (S3 y S4): como el original (MONSTER_InitComponents),
   una de sus opciones al azar (veneno, fuego, frío o sacrílego), y la calavera que tira
   DoomKnightMissile es la de S3 (SrvDo148: undeadmissile1 + la opción). Los demás
   monstruos llevan siempre la primera de cada pieza. Solo las piezas distintas de las
   del dibujo compartido se cargan aparte (gfx_parts, poca memoria) y van encima. */
static void monster_parts(Unit *u, int ms, int ms2, const CharLook *look, const char *wclass,
                          const char *const modes[ANIM_COUNT], const char *suffix)
{
    int knight = 0;
    for (int k = 1; k <= 8 && !knight; k++) {
        char col[8];
        snprintf(col, sizeof col, "Skill%d", k);
        knight = strcmp(txt_get(&s_monstats, ms, txt_col(&s_monstats, col)), "DoomKnightMissile") == 0;
    }
    if (!knight)
        return;
    static const int k_comps[2] = { COMP_S3, COMP_S4 };
    CharLook over;
    memset(&over, 0, sizeof over);
    strcpy(over.base, look->base);
    strcpy(over.token, look->token);
    uint32_t parts = 2166136261u;
    int any = 0;
    for (int i = 0; i < 2; i++) {
        int c = k_comps[i];
        if (txt_get_int(&s_monstats2, ms2, k_comp_names[c], 0) != 1)
            continue;
        const char *opts = txt_get(&s_monstats2, ms2, txt_col(&s_monstats2, k_part_cols[c]));
        char tmp[8];
        int n = nth_option(tmp, sizeof tmp, opts, 0), pick = n > 1 ? (int)(rnd(u) % (uint32_t)n) : 0;
        if (c == COMP_S3)
            u->comp_s3 = (int8_t)pick;
        if (pick <= 0)
            continue;
        nth_option(over.part[c], sizeof over.part[c], opts, pick);
        for (const char *p = over.part[c]; *p; p++)
            parts = (parts ^ (uint8_t)*p) * 16777619u;
        parts = (parts ^ (uint8_t)c) * 16777619u;
        any = 1;
    }
    if (!any)
        return;
    char key[32];
    snprintf(key, sizeof key, "p/%s/%s/%08x%s", look->token, wclass, (unsigned)parts, suffix);
    u->gfx_parts = get_gfx(key, &over, modes, wclass);
    gfx_ref(u->gfx_parts, 1);
}

static Unit *create_monster(const char *id, float x, float y, int level, int enemy)
{
    int merc = enemy == 2;
    if (merc)
        enemy = 0;
    int ms = mon_row(&s_monstats, id);
    int ms2 = mon_row(&s_monstats2, id);
    if (ms < 0 || ms2 < 0)
        return NULL;
    CharLook look;
    char wclass[8];
    const char *modes[ANIM_COUNT];
    monster_look(ms, ms2, enemy, merc, &look, wclass, modes);

    char key[32];
    /* Los que comparten gráficos pero llevan otras piezas o armas (las pícaras
       corruptas con espada, lanza o arco) no pueden usar la misma animación */
    uint32_t parts = 2166136261u;
    for (int c = 0; c < COMP_COUNT; c++)
        for (const char *s = look.part[c]; *s; s++)
            parts = (parts ^ (uint8_t)*s) * 16777619u;
    char suffix[8];
    snprintf(suffix, sizeof suffix, "%s%s", enemy ? "/e" : merc ? "/m" : "", modes[ANIM_SKILL] ? "s" : "");
    snprintf(key, sizeof key, "m/%s/%s/%08x%s", look.token, wclass, (unsigned)parts, suffix);
    UnitGfx *g = get_gfx(key, &look, modes, wclass);
    Unit *u = g ? new_unit(x, y) : NULL;
    if (!u) {
        gfx_ref(g, 0); /* recién cargado y sin nadie que lo use */
        return NULL;
    }
    u->gfx = g;
    gfx_ref(g, 1);
    if (enemy)
        monster_parts(u, ms, ms2, &look, wclass, modes, suffix);
    snprintf(u->name, sizeof u->name, "%s", id);
    u->display = str_get(txt_get(&s_monstats, ms, txt_col(&s_monstats, "NameStr")));
    u->snd = enemy || merc ? mon_sounds(txt_get(&s_monstats, ms, txt_col(&s_monstats, "MonSound"))) : NULL;
    if (u->snd && u->snd->neu_time > 0)
        u->neutral_timer = u->snd->neu_time / 2 + (int)(rnd(u) % (uint32_t)u->snd->neu_time);
    u->shadow = txt_col(&s_monstats2, "Shadow") < 0 || txt_get_int(&s_monstats2, ms2, "Shadow", 0) == 1;
    u->pshift = mon_palshift(look.token, ms, ms2);
    u->light = txt_get_int(&s_monstats2, ms2, "Light", 0);
    u->solid = 1;
    u->killable = txt_col(&s_monstats, "killable") < 0 || txt_get_int(&s_monstats, ms, "killable", 0) == 1;
    {
        int fs = txt_get_int(&s_monstats2, ms2, "SizeX", 2);
        u->foot = fs < 1 ? 1 : fs > 3 ? 3 : fs;
        /* Como PATH_InitDynamicPath del original: el espectro (wraith1 y los que
           salen de él) atraviesa casi todo, los que vuelan solo chocan con las
           barreras de proyectiles y las puertas, y los que abren puertas no
           chocan con ellas */
        const char *base = txt_get(&s_monstats, ms, txt_col(&s_monstats, "BaseId"));
        if (strcmp(base, "wraith1") == 0)
            u->move_mask = COLL_MISSILE_BARRIER | COLL_DOOR;
        else if (txt_get_int(&s_monstats, ms, "flying", 0))
            u->move_mask = COLL_MASK_FLYING;
        else if (txt_get_int(&s_monstats, ms, "opendoors", 0))
            u->move_mask = COLL_MASK_MONSTER_PATH & (uint16_t)~COLL_DOOR;
        else
            u->move_mask = COLL_MASK_MONSTER_PATH;
    }
    u->speed = txt_get_int(&s_monstats, ms, "Velocity", 2) * SPEED_PER_VELOCITY * MON_WALK_BONUS;
    if (enemy) {
        /* El tamaño del original (1 chico, 2 normal, 3 grande como Andariel o
           los brutos): no se apilan, rodean al jugador y se los golpea en el
           borde. Pegan desde más lejos cuanto más grandes. */
        int size = txt_get_int(&s_monstats2, ms2, "SizeX", 2);
        size = size < 1 ? 1 : size > 3 ? 3 : size;
        u->radius = 0.25f + 0.25f * (float)size;
        /* y su MeleeRng de monstats2.txt (UNITS_GetMeleeRange): los tentáculos llegan a la orilla */
        u->reach = UNIT_PLAYER_BODY + u->radius + 0.45f + (float)txt_get_int(&s_monstats2, ms2, "MeleeRng", 0);
        u->run_speed = txt_get_int(&s_monstats, ms, "Run", 0) * SPEED_PER_VELOCITY * MON_RUN_BONUS;
        const char *base = txt_get(&s_monstats, ms, txt_col(&s_monstats, "BaseId"));
        if (strcmp(base, "tentacle1") == 0 || strcmp(base, "tentaclehead1") == 0)
            u->melee_bonus = 8.0f;
    }

    if (merc) {
        if (txt_get_int(&s_monstats, ms, "isMelee", 0)) { /* el guerrero del desierto: llega con la lanza */
            int size = txt_get_int(&s_monstats2, ms2, "SizeX", 2);
            size = size < 1 ? 1 : size > 3 ? 3 : size;
            u->radius = 0.25f + 0.25f * (float)size;
            u->reach = UNIT_PLAYER_BODY + u->radius + 0.45f;
            const char *seq = txt_get(&s_monstats, ms, txt_col(&s_monstats, "Sk1mode"));
            u->seq = strncmp(seq, "seq_", 4) == 0 ? monseq_get(seq) : NULL;
        }
        u->merc = 1;
        u->zone = -1; /* va con su dueño: no es de ninguna zona */
        u->solid = 0; /* no le tapa el paso a su dueño */
        u->state = ST_IDLE;
        u->run_speed = txt_get_int(&s_monstats, ms, "Run", 9) * SPEED_PER_VELOCITY * MON_RUN_BONUS;
        u->neutral_timer = 0;
        return u;
    }
    if (enemy) {
        u->enemy = 1;
        u->state = ST_IDLE;
        u->wander = gfx_has(g, ANIM_WALK) ? 3.0f : 0.0f;
        monster_stats(u, ms, level);
        /* Los que disparan: las ratas de púas con su segundo ataque (MissA2) y
           los chamanes con la bola de fuego (ShamanFire -> shafire1..5) */
        const char *ai = txt_get(&s_monstats, ms, txt_col(&s_monstats, "AI"));
        const char *miss_a1 = txt_get(&s_monstats, ms, txt_col(&s_monstats, "MissA1"));
        const char *miss_a2 = txt_get(&s_monstats, ms, txt_col(&s_monstats, "MissA2"));
        if (txt_get_int(&s_monstats, ms, "boss", 0) == 1)
            u->unique = u->no_bind = 1; /* jefes de misión (Blood Raven): no huyen y se los muestra en dorado */
        u->undead = txt_get_int(&s_monstats, ms, "lUndead", 0) == 1 || txt_get_int(&s_monstats, ms, "hUndead", 0) == 1;
        snprintf(u->base, sizeof u->base, "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, "BaseId")));
        u->ai = (uint8_t)ai_kind(ai);
        for (int k = 0; k < 8; k++) {
            char col[8];
            snprintf(col, sizeof col, "aip%d", k + 1);
            u->aip[k] = (int16_t)txt_get_int(&s_monstats, ms, dcol(&s_monstats, col), 0);
        }
        for (int k = 0; k < MON_SKILLS; k++) { /* sus habilidades, la secuencia o el modo de cada una y su nivel */
            static const char *const k_modes[] = { "NU", "WL", "A1", "GH", "DT", "DD", "A2", "S1", "RN", "SC", "S2", "S3", "S4" }; /* ANIM_* */
            char col[8];
            snprintf(col, sizeof col, "Skill%d", k + 1);
            snprintf(u->skill[k], sizeof u->skill[k], "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, col)));
            snprintf(col, sizeof col, "Sk%dmode", k + 1);
            const char *sq = txt_get(&s_monstats, ms, txt_col(&s_monstats, col));
            u->seqs[k] = strncmp(sq, "seq_", 4) == 0 ? monseq_get(sq) : NULL;
            u->sk_anim[k] = -1;
            for (int a = 0; a < (int)(sizeof k_modes / sizeof k_modes[0]); a++)
                if (strcmp(k_modes[a], sq) == 0)
                    u->sk_anim[k] = (int8_t)a;
            snprintf(col, sizeof col, "Sk%dlvl", k + 1); /* (más MonsterSkillBonus: +3 en Pesadilla y +7 en Infierno) */
            u->sk_lvl[k] = (int16_t)(txt_get_int(&s_monstats, ms, col, 1) + diff_int("MonsterSkillBonus", 0));
        }
        u->heal_target = -1;
        if (u->ai == AI_MAGGOT || u->ai == AI_MAGGOTEGG || u->ai == AI_MAGGOTQUEEN || u->ai == AI_VILEMOTHER) /* el huevo que pone, la larva que sale, los gusanos de la reina, las crías de la madre */
            snprintf(u->minion, sizeof u->minion, "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, "spawn")));
        if (u->ai && getenv("D2_AI_DEBUG"))
            printf("IA: aparece %s en (%.0f, %.0f) con IA %d y habilidades %s%s %s%s %s%s\n", id, x, y, u->ai, u->skill[0],
                   u->seqs[0] ? "*" : "", u->skill[1], u->seqs[1] ? "*" : "", u->skill[2], u->seqs[2] ? "*" : "");
        u->revivable = txt_get_int(&s_monstats2, ms2, "revive", 0) == 1;
        u->soft = (uint8_t)(txt_get_int(&s_monstats2, ms2, "soft", 0) == 1);
        u->a4_corpse = -1;
        u->demon = txt_get_int(&s_monstats, ms, "demon", 0) == 1;
        if (strcmp(ai, "BloodRaven") == 0) {
            /* Dispara flechas (y a veces de fuego, MissSQ) y levanta zombis */
            snprintf(u->missile, sizeof u->missile, "%s", miss_a1);
            snprintf(u->missile2, sizeof u->missile2, "%s",
                     txt_get(&s_monstats, ms, txt_col(&s_monstats, "MissSQ")));
            u->ranged = missiles_exists(u->missile);
            u->ranged_anim = ANIM_ATTACK;
            u->raiser = gfx_has(g, ANIM_SKILL);
            snprintf(u->minion, sizeof u->minion, "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, "spawn")));
            u->raise_cd = RAISE_TICKS / 3;
        } else if (strcmp(ai, "FoulCrowNest") == 0) {
            /* El nido larga cuervos cada tanto */
            u->raiser = gfx_has(g, ANIM_SKILL);
            snprintf(u->minion, sizeof u->minion, "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, "spawn")));
            u->raise_cd = RAISE_TICKS / 4;
            u->wander = 0.0f;
        } else if (strcmp(ai, "Andariel") == 0 && gfx_has(g, ANIM_CAST)) {
            /* Pega con veneno de cerca y, de lejos, escupe un abanico de veneno */
            snprintf(u->missile, sizeof u->missile, "andarielspray");
            u->ranged = missiles_exists(u->missile);
            u->ranged_anim = ANIM_CAST;
            u->spray = 7;
            u->poison_hit = 1; /* y su golpe también envenena */
        } else if (strcmp(ai, "Vampire") == 0 && gfx_has(g, ANIM_CAST)) {
            snprintf(u->missile, sizeof u->missile, "vampirefireball"); /* VampireFireball de skills.txt */
            u->ranged = missiles_exists(u->missile);
            u->ranged_anim = ANIM_CAST;
        } else if (miss_a2[0] && gfx_has(g, ANIM_ATTACK2) && missiles_exists(miss_a2)) {
            snprintf(u->missile, sizeof u->missile, "%s", miss_a2);
            u->ranged = 1;
            u->ranged_anim = ANIM_ATTACK2;
        } else if (strcmp(ai, "FallenShaman") == 0) {
            size_t len = strlen(id);
            snprintf(u->missile, sizeof u->missile, "shafire%c", len && isdigit((unsigned char)id[len - 1]) ? id[len - 1] : '1');
            u->ranged = missiles_exists(u->missile);
            u->ranged_anim = ANIM_ATTACK;
            u->resurrect = gfx_has(g, ANIM_ATTACK2);
        } else if (strcmp(ai, "WillOWisp") == 0 && gfx_has(g, ANIM_CAST)) { /* los fuegos fatuos: el rayo (MissC) */
            snprintf(u->missile, sizeof u->missile, "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, "MissC")));
            u->ranged = missiles_exists(u->missile);
            u->ranged_anim = ANIM_CAST;
        } else if (strcmp(ai, "HighPriest") == 0 && gfx_has(g, ANIM_SKILL)) { /* el Consejo: su rayo (MissS1) */
            snprintf(u->missile, sizeof u->missile, "%s", txt_get(&s_monstats, ms, txt_col(&s_monstats, "MissS1")));
            u->ranged = missiles_exists(u->missile);
            u->ranged_anim = ANIM_SKILL;
        } else if (miss_a1[0] && missiles_exists(miss_a1)) { /* arqueras */
            snprintf(u->missile, sizeof u->missile, "%s", miss_a1);
            u->ranged = 1;
            u->ranged_anim = ANIM_ATTACK;
        }
        return u;
    }

    /* Los NPC pasean cerca de su lugar, los animales un poco menos y las
       pícaras de guardia se quedan quietas */
    const char *ai = txt_get(&s_monstats, ms, txt_col(&s_monstats, "AI"));
    int npc = txt_get_int(&s_monstats, ms, "npc", 0) == 1;
    /* los que se quedan en su lugar (NpcStationary: Tyrael, también encadenado; el espíritu de Izual) y Hadriel (Buffy) también hablan */
    u->npc = npc && (strcmp(ai, "Npc") == 0 || strcmp(ai, "NpcStationary") == 0 || strcmp(ai, "Buffy") == 0);
    if (!gfx_has(g, ANIM_WALK))
        u->wander = 0.0f;
    else if (strcmp(ai, "Npc") == 0)
        u->wander = 6.0f;
    else if (strcmp(ai, "Idle") == 0 && !npc)
        u->wander = 3.0f;
    return u;
}

int units_spawn_monster(const char *id, float x, float y, int level)
{
    Unit *u = create_monster(id, x, y, level, 1);
    return u ? (int)(u - s_units) : -1;
}

/* Un grupo de monstruos alrededor de (x, y), en lugares libres */
static void spawn_group(const char *id, int count, float x, float y, int level)
{
    for (int k = 0; k < count; k++) {
        for (int tries = 0; tries < 10; tries++) {
            float a = (float)(spawn_rnd() % 628) / 100.0f, r = k == 0 ? 0.0f : 1.0f + (float)(spawn_rnd() % 200) / 100.0f;
            float px = x + cosf(a) * r, py = y + sinf(a) * r;
            if (s_map && map_blocked(s_map, (int)px, (int)py))
                continue;
            units_spawn_monster(id, px, py, level);
            break;
        }
    }
}

/* Lo que suman los modificadores a un golpe que pegó: daño de fuego, frío o
   espectral, la maldición, quemar maná y el veneno de Andariel */
static int mod_hit(Unit *u, UnitTarget *t, int dmg)
{
    int extra = 0, half = dmg / 2 + 1;
    if (HAS_MOD(u, MOD_FIRE)) /* con la resistencia del que recibe */
        extra += half * (100 - t->res[0]) / 100;
    if (HAS_MOD(u, MOD_COLD))
        extra += half * (100 - t->res[1]) / 100;
    if (HAS_MOD(u, MOD_SPECTRAL)) /* un elemento al azar */
        extra += (half + (int)(rnd(u) % (uint32_t)half)) * (100 - t->res[rnd(u) % 3]) / 100;
    if (HAS_MOD(u, MOD_MANAHIT))
        t->mana_burn += dmg * 2;
    if (HAS_MOD(u, MOD_CURSE))
        t->curse_ticks = 5 * TICK_RATE;
    if (u->poison_hit) { /* veneno: otro tanto como el golpe, en tres segundos */
        t->poison_total += dmg;
        t->poison_ticks = 3 * TICK_RATE;
    }
    return extra;
}

/* Un jefe: más vida (la de monumod.txt de su dificultad: "Unique HP % Bonus",
   300, 200 o 100 %), algo más de daño y puntería, y más experiencia
   (exp_mul). El nivel ya viene con UNIQUE_LEVELS de más. */
static void make_boss(Unit *u, int exp_mul)
{
    u->max_hp = u->hp = u->hp + u->hp * umod_const(7 + s_diff, 300) / 100;
    u->dmg_min = u->dmg_min * 5 / 4 + 1;
    u->dmg_max = u->dmg_max * 5 / 4 + 1;
    u->a2_min = u->a2_min * 5 / 4;
    u->a2_max = u->a2_max * 5 / 4;
    u->exp *= exp_mul;
    u->ar = u->ar * 3 / 2;
    u->unique = 1;
}

/* Las resistencias que suman los modificadores, como MONSTERUNIQUE_UMod8_Resistant
   del original: nada más si ya es inmune a dos cosas; algunas solo si no
   llegaba a `below` */
static void add_resist(Unit *u, int elem, int amount, int below)
{
    int immune = 0;
    for (int e = 0; e < 6; e++)
        immune += u->res[e] >= 100;
    if (immune >= 2 || (below > 0 && u->res[elem] >= below))
        return;
    u->res[elem] += amount;
}

/* Los modificadores que se ven en las estadísticas (los demás actúan al pegar,
   al recibir un golpe o al morir) */
static void apply_mods(Unit *u, uint32_t mods)
{
    u->mods = mods;
    if (HAS_MOD(u, MOD_STRONG)) { /* Muy fuerte: el doble de daño */
        u->dmg_min *= 2;
        u->dmg_max *= 2;
        u->a2_min *= 2;
        u->a2_max *= 2;
    }
    if (HAS_MOD(u, MOD_FAST)) { /* Muy rápido: camina y ataca más seguido */
        u->speed *= 1.5f;
        u->attack_delay = u->attack_delay * 2 / 3;
    }
    if (HAS_MOD(u, MOD_STONESKIN)) { /* Piel de piedra: el doble de defensa y 50% de resistencia física */
        u->ac *= 2;
        add_resist(u, ELEM_NONE, 50, 0);
    }
    if (HAS_MOD(u, MOD_RESIST)) { /* Resistente a la magia */
        add_resist(u, ELEM_COLD, 40, 0);
        add_resist(u, ELEM_FIRE, 40, 0);
        add_resist(u, ELEM_LTNG, 40, 0);
    }
    if (HAS_MOD(u, MOD_FIRE)) /* los encantados resisten su elemento */
        add_resist(u, ELEM_FIRE, 75, 0);
    if (HAS_MOD(u, MOD_COLD))
        add_resist(u, ELEM_COLD, 75, 0);
    if (HAS_MOD(u, MOD_LIGHTNING))
        add_resist(u, ELEM_LTNG, 75, 0);
    if (HAS_MOD(u, MOD_MANAHIT)) /* Quemadura de maná */
        add_resist(u, ELEM_MAG, 20, 0);
    if (HAS_MOD(u, MOD_SPECTRAL)) { /* Golpe espectral */
        add_resist(u, ELEM_COLD, 20, 75);
        add_resist(u, ELEM_FIRE, 20, 75);
        add_resist(u, ELEM_LTNG, 20, 75);
    }
    if (HAS_MOD(u, MOD_MULTISHOT) && u->ranged && u->spray < 3)
        u->spray = 3;
}

/* Los jefes con aura (para que se vea): sus números, revisados al pedirlos */
static int s_aura_ids[8], s_num_aura;

/* MONSTERUNIQUE_UMod30_AuraEnchanted: un aura del Paladín al azar entre las que da su nivel
   (Choque sagrado desde el 20; Espinas nunca), a nivel/divisor (1 a 99). Lord De Seis
   siempre lleva Fanatismo. */
static void aura_enchant(Unit *u, int lord_de_seis)
{
    static const struct { int min_level, div; const char *skill; } k_auras[] = {
        { 0, 6, "Might" },      { 0, 6, "Holy Fire" },  { 0, 5, "Blessed Aim" }, { 0, 7, "Holy Freeze" },
        { 0, 8, "Conviction" }, { 0, 8, "Fanaticism" }, { 20, 8, "Holy Shock" },
    };
    int lvl = u->level > 1 ? u->level : 1, n = 0;
    for (unsigned k = 0; k < sizeof k_auras / sizeof k_auras[0]; k++)
        n += k_auras[k].min_level <= lvl;
    int i = lord_de_seis ? 5 : (int)(spawn_rnd() % (uint32_t)(n > 0 ? n : 1));
    int id = skills_global_find(k_auras[i].skill), cls = skills_global_class(id), idx = skills_global_local(id);
    if (id < 0 || cls < 0 || idx < 0)
        return;
    u->aura_sk = skills_get(cls, idx);
    u->aura_lvl = lvl / k_auras[i].div;
    u->aura_lvl = u->aura_lvl < 1 ? 1 : u->aura_lvl > 99 ? 99 : u->aura_lvl;
    if (getenv("D2_AI_DEBUG"))
        printf("IA: %s lleva %s (nivel %d)\n", u->name, k_auras[i].skill, u->aura_lvl);
    int self = (int)(u - s_units);
    for (int k = 0; k < s_num_aura; k++)
        if (s_aura_ids[k] == self)
            return;
    if (s_num_aura < (int)(sizeof s_aura_ids / sizeof s_aura_ids[0]))
        s_aura_ids[s_num_aura++] = self;
}

int units_aura_bosses(int *ids, int *kinds, int max)
{
    int n = 0;
    for (int k = 0; k < s_num_aura; k++) {
        const Unit *u = &s_units[s_aura_ids[k]];
        if (u->zone == ZONE_FREE || !u->aura_sk) { /* se fue (o el lugar es de otro) */
            s_aura_ids[k--] = s_aura_ids[--s_num_aura];
            continue;
        }
        if (n < max && u->state < ST_DYING && !u->hidden) {
            ids[n] = s_aura_ids[k];
            kinds[n++] = u->aura_sk->kind;
        }
    }
    return n;
}

/* `n` modificadores al azar distintos (los únicos del original: uno en
   Normal, dos en Pesadilla y tres en Infierno; nDifficulty + 1) */
static uint32_t random_mods(int n)
{
    static const int pool[] = { MOD_STRONG, MOD_FAST, MOD_CURSE, MOD_FIRE, MOD_LIGHTNING,
                                MOD_COLD, MOD_MANAHIT, MOD_SPECTRAL, MOD_STONESKIN };
    uint32_t mods = 0;
    for (int k = 0; k < n; k++)
        for (int tries = 0; tries < 8; tries++) {
            int m = pool[spawn_rnd() % (sizeof pool / sizeof pool[0])];
            if (!(mods & (1u << m))) {
                mods |= 1u << m;
                break;
            }
        }
    return mods;
}

/* Un jefe al azar (más nivel, vida y experiencia, y sus modificadores al
   azar, como los únicos del original) */
static Unit *spawn_random_boss(const char *id, float x, float y, int level)
{
    Unit *boss = create_monster(id, x, y, level + UNIQUE_LEVELS, 1);
    if (!boss)
        return NULL;
    make_boss(boss, 5); /* +300% de vida en Normal y 5 veces la experiencia, como los de nombre (sub_6FC6E8D0) */
    boss->spawn_boss = 1;
    int ms = mon_row(&s_monstats, id);
    const char *utc = ms >= 0 ? txt_get(&s_monstats, ms, txt_col(&s_monstats, dcol(&s_monstats, "TreasureClassUnique"))) : "";
    if (utc[0])
        snprintf(boss->tc, sizeof boss->tc, "%s", utc);
    apply_mods(boss, random_mods(1 + s_diff) | (boss->ranged ? 1u << MOD_MULTISHOT : 0u));
    return boss;
}

/* Un lugar libre alrededor de (x, y) para el k-ésimo del grupo (el primero, ahí mismo) */
static int group_spot(float x, float y, int k, float *px, float *py)
{
    for (int tries = 0; tries < 10; tries++) {
        float a = (float)(spawn_rnd() % 628) / 100.0f, r = k == 0 ? 0.0f : 1.0f + (float)(spawn_rnd() % 200) / 100.0f;
        *px = x + cosf(a) * r;
        *py = y + sinf(a) * r;
        if (!s_map || !map_blocked(s_map, (int)*px, (int)*py))
            return 1;
    }
    return 0;
}

/* Un jefe al azar con su escolta (D2GAME_SpawnMinions_6FC6F440): 3 a 6 de su
   minion1 de monstats.txt (o de los suyos), con el doble de vida (monumod:
   rndname) y sus modificadores (los de monumod.txt con xfer). -1 si no salió. */
int units_spawn_boss_pack(const char *id, float x, float y, int level)
{
    Unit *boss = spawn_random_boss(id, x, y, level);
    if (!boss)
        return -1;
    int i = (int)(boss - s_units), ms = mon_row(&s_monstats, id);
    const char *m1 = txt_get(&s_monstats, ms, txt_col(&s_monstats, "minion1"));
    char minion[32];
    snprintf(minion, sizeof minion, "%s", m1[0] ? m1 : id);
    uint32_t mods = boss->mods;
    int n = 3 + (int)(spawn_rnd() % 4);
    for (int k = 1; k <= n; k++) {
        float px, py;
        Unit *mu = group_spot(x, y, k, &px, &py) ? create_monster(minion, px, py, level, 1) : NULL;
        if (!mu)
            continue;
        mu->max_hp = mu->hp = mu->hp + mu->hp * umod_const(1 + s_diff, 100) / 100; /* (100, 75 o 50 %) */
        apply_mods(mu, mods);
    }
    return i;
}

/* Los campeones (sub_6FC6E940, MONSTERUNIQUE_UMod16..39): el tipo, con el
   mismo peso cada uno (cpick de monumod.txt en Normal) */
enum { CHAMP_NONE, CHAMP_CHAMPION, CHAMP_GHOSTLY, CHAMP_FANATIC, CHAMP_POSSESSED, CHAMP_BERSERK };

static Unit *spawn_champion(const char *id, float x, float y, int level, int kind)
{
    /* los jefes, 3 niveles más; el campeón (UMod16), uno menos */
    Unit *u = create_monster(id, x, y, level + UNIQUE_LEVELS - (kind != CHAMP_BERSERK), 1);
    if (!u)
        return NULL;
    int ms = mon_row(&s_monstats, id), vel = txt_get_int(&s_monstats, ms, "Velocity", 0);
    u->max_hp = u->hp = u->hp + u->hp * umod_const(4 + s_diff, 200) / 100; /* +200, 150 o 100 % (monumod.txt: Champion HP % Bonus) */
    /* El daño de más: en Pesadilla e Infierno, por ChampionDamageBonus (75 y 66 %) */
    int bonus = s_diff ? diff_int("ChampionDamageBonus", 90) : 100;
    u->exp *= 5;                   /* UMod2 */
    u->champion = (int8_t)kind;
    u->spawn_boss = 1;
    if (kind != CHAMP_BERSERK) { /* UMod16: -40% de experiencia, +100% de daño, +75% de puntería y más rápido */
        u->exp -= u->exp * 2 / 5;
        u->dmg_min = u->dmg_min * (100 + bonus) / 100;
        u->dmg_max = u->dmg_max * (100 + bonus) / 100;
        u->a2_min = u->a2_min * (100 + bonus) / 100;
        u->a2_max = u->a2_max * (100 + bonus) / 100;
        u->ar = u->ar * (100 + 75 * bonus / 100) / 100;
        if (vel > 0 && kind != CHAMP_GHOSTLY) {
            int pct = kind == CHAMP_FANATIC ? 2048 / vel - 128 : 20;
            pct = kind == CHAMP_FANATIC ? (pct < 10 ? 10 : pct > 100 ? 100 : pct) : pct;
            u->speed = u->speed * (float)(100 + pct) / 100.0f;
            u->run_speed = u->run_speed * (float)(100 + pct) / 100.0f;
        }
    }
    if (kind == CHAMP_GHOSTLY) { /* UMod36: 80% menos de daño físico y golpe de frío del DM de monlvl.txt */
        if (u->res[ELEM_NONE] < 80)
            u->res[ELEM_NONE] = 80;
        char lv[12];
        snprintf(lv, sizeof lv, "%d", u->level);
        int ml = txt_find_row(&s_monlvl, txt_col(&s_monlvl, "Level"), lv), dm = txt_get_int(&s_monlvl, ml, "DM", 100);
        for (int k = 0; k < 3; k++)
            if (u->el[k].anim < 0 || !u->el[k].elem) {
                u->el[k].anim = ANIM_ATTACK;
                u->el[k].elem = ELEM_COLD;
                u->el[k].pct = 100;
                u->el[k].min = (int16_t)(dm * 33 / 100); /* monumod.txt: questcomplete y manahit */
                u->el[k].max = (int16_t)(dm * 50 / 100);
                u->el[k].dur = 150;
                break;
            }
    } else if (kind == CHAMP_FANATIC) { /* UMod37: -70% de defensa */
        u->ac = u->ac * 30 / 100;
    } else if (kind == CHAMP_POSSESSED) { /* UMod38: el doble de vida */
        u->max_hp = u->hp = u->hp * 2;
    } else if (kind == CHAMP_BERSERK) { /* UMod39: -75% de vida, +300% de daño y de puntería */
        u->max_hp = u->hp = u->hp / 4 > 1 ? u->hp / 4 : 1;
        u->dmg_min = u->dmg_min * (100 + 3 * bonus) / 100;
        u->dmg_max = u->dmg_max * (100 + 3 * bonus) / 100;
        u->a2_min = u->a2_min * (100 + 3 * bonus) / 100;
        u->a2_max = u->a2_max * (100 + 3 * bonus) / 100;
        u->ar = u->ar * (100 + 3 * bonus) / 100;
    }
    /* el nombre: el monstruo y el tipo, con su género (ChampionFormat: "%1 %0") */
    static const char *const k_keys[] = { "", "Champion", "Ghostly", "Fanatic", "Possessed", "Berserk" };
    const char *key = txt_get(&s_monstats, ms, txt_col(&s_monstats, "NameStr"));
    str_compose(u->display_buf, sizeof u->display_buf, "ChampionFormat", str_get_gender(k_keys[kind], str_gender(key)),
                str_get(key), NULL);
    u->display = u->display_buf;
    return u;
}

int units_spawn_level_boss(const char *id, float x, float y, int level)
{
    if (spawn_rnd() % 100 < 20) { /* el campeón (monumod.txt: constants de none), y 1 a 3 más como él */
        int kind = CHAMP_CHAMPION + (int)(spawn_rnd() % 5), n = 1 + (int)(spawn_rnd() % 3);
        if (!spawn_champion(id, x, y, level, kind))
            return 0;
        if (getenv("D2_AI_DEBUG"))
            printf("IA: campeones %s (tipo %d) en (%.0f, %.0f)\n", id, kind, x, y);
        for (int k = 1; k <= n; k++) {
            float px, py;
            if (group_spot(x, y, k, &px, &py))
                spawn_champion(id, px, py, level, kind);
        }
        return 1;
    }
    return units_spawn_boss_pack(id, x, y, level) >= 0;
}

int units_bosses_in(float x0, float y0, float x1, float y1)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        n += u->zone != ZONE_FREE && u->spawn_boss && u->x >= x0 && u->x < x1 && u->y >= y0 && u->y < y1;
    }
    return n;
}

int units_is_champion(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].champion;
}

/* Jefes con nombre propio (superuniques.txt), como Corpsefire: un monstruo
   común con mucha más vida, más daño y experiencia, mejor botín y escolta.
   Devuelve su unidad (-1 = no salió). */
static int spawn_superunique(const char *name, float x, float y, int level)
{
    int row = txt_find_row(&s_superuniques, 0, name);
    if (row < 0 || level <= 0)
        return -1;
    const char *cls = txt_get(&s_superuniques, row, txt_col(&s_superuniques, "Class"));
    Unit *u = create_monster(cls, x, y, level + UNIQUE_LEVELS, 1);
    if (!u)
        return -1;
    make_boss(u, 5);
    u->no_bind = u->spawn_boss = 1;
    static const char *const mod_cols[3] = { "Mod1", "Mod2", "Mod3" };
    uint32_t mods = 0;
    for (int k = 0; k < 3; k++) {
        int m = txt_get_int(&s_superuniques, row, mod_cols[k], 0);
        if (m > 0 && m < 32)
            mods |= 1u << m;
    }
    if (s_diff > 0) /* uno más al azar en Pesadilla y dos en Infierno */
        mods |= random_mods(s_diff);
    apply_mods(u, mods);
    if (HAS_MOD(u, MOD_AURA))
        aura_enchant(u, strcmp(name, "Lord De Seis") == 0);
    snprintf(u->name, sizeof u->name, "%s", name);
    u->display = str_get(txt_get(&s_superuniques, row, txt_col(&s_superuniques, "Name")));
    snprintf(u->tc, sizeof u->tc, "%s", txt_get(&s_superuniques, row, txt_col(&s_superuniques, dcol(&s_superuniques, "TC"))));
    char cls_copy[32];
    snprintf(cls_copy, sizeof cls_copy, "%s", cls);
    int lo = txt_get_int(&s_superuniques, row, "MinGrp", 0), hi = txt_get_int(&s_superuniques, row, "MaxGrp", 0);
    int escort = lo + (hi > lo ? (int)(spawn_rnd() % (uint32_t)(hi - lo + 1)) : 0);
    int self = (int)(u - s_units);
    spawn_group(cls_copy, escort, x + 1.5f, y + 1.5f, level);
    return self;
}

int units_spawn_superunique(const char *name, float x, float y, int level)
{
    return spawn_superunique(name, x, y, level);
}

int units_monstats_int(const char *id, const char *col, int def)
{
    int r = mon_row(&s_monstats, id);
    return r >= 0 ? txt_get_int(&s_monstats, r, col, def) : def;
}

static int wall_foot(const Map *m, float x, float y, int size, uint16_t mask);

/* La variante de un monstruo para el nivel de la zona: la más fuerte de
   base1..base9 (habilitadas) con Level de monstats.txt hasta ese nivel */
static int monster_variant(const char *base, int level, char *out, size_t cap)
{
    int best = -1, best_lvl = -1, low = -1, low_lvl = 1 << 30;
    for (int k = 1; k <= 9; k++) {
        char id[24];
        snprintf(id, sizeof id, "%s%d", base, k);
        int ms = mon_row(&s_monstats, id);
        if (ms < 0 || txt_get_int(&s_monstats, ms, "enabled", 0) != 1)
            continue;
        int lv = txt_get_int(&s_monstats, ms, "Level", 1);
        if (lv <= level && lv > best_lvl)
            best = k, best_lvl = lv;
        if (lv < low_lvl)
            low = k, low_lvl = lv;
    }
    if (best < 0)
        best = low;
    if (best < 0)
        return -1;
    snprintf(out, cap, "%s%d", base, best);
    return 0;
}

/* Un monstruo del lado de los jugadores que no es NPC (Align 1 en monstats:
   los bárbaros del Acto 5, IA NpcBarb): los jugadores no le pueden pegar,
   los monstruos lo atacan (el original lo anota en la lista de blancos 8,
   sub_6FC40280, al pensar por primera vez) y él pelea contra ellos
   (barb_think). No tiene dueño: es de su zona, como los demás. No tapa el
   paso (como los esbirros): si no, se traba el que pasa por la barricada */
static void barb_spawn(const char *id, int ms, float x, float y, int level)
{
    Unit *u = create_monster(id, x, y, level, 2);
    if (!u)
        return;
    u->merc = 0;
    u->pet = 1;
    u->barb = 1;
    u->owner = -1;
    u->target = -1;
    u->zone = s_spawn_zone;
    int ms2 = mon_row(&s_monstats2, id);
    int size = txt_get_int(&s_monstats2, ms2, "SizeX", 2);
    size = size < 1 ? 1 : size > 3 ? 3 : size;
    u->radius = 0.25f + 0.25f * (float)size;
    monster_stats(u, ms, level);
    memset(&u->ms, 0, sizeof u->ms);
    u->ms.level = level;
    u->ms.hp_max = u->max_hp;
    u->ms.defense = u->ac;
    u->ms.ar = u->ar;
    u->ms.dmin = u->dmg_min;
    u->ms.dmax = u->dmg_max;
    u->level = level;
    u->reach = MELEE_REACH + u->radius;
    for (int k = 0; k < 3; k++) {
        char col[8];
        snprintf(col, sizeof col, "aip%d", k + 1);
        u->aip[k] = (int16_t)txt_get_int(&s_monstats, ms, dcol(&s_monstats, col), 0);
    }
    u->timer = 1 + (int)(spawn_rnd() % 25u); /* (no piensan todos en el mismo tick) */
}

static void spawn_preset_monster(int act, int id, float x, float y, int level)
{
    /* monpreset.txt: el id es la posición dentro de las filas del acto */
    int col_act = txt_col(&s_monpreset, "Act"), col_place = txt_col(&s_monpreset, "Place");
    const char *place = NULL;
    for (int r = 1, k = 0; r < s_monpreset.rows; r++) {
        if (atoi(txt_get(&s_monpreset, r, col_act)) != act)
            continue;
        if (k++ == id) {
            place = txt_get(&s_monpreset, r, col_place);
            break;
        }
    }
    if (!place)
        return;

    /* Marcadores de grupos: en los campamentos salen Fallen (más fuertes en
       las zonas de más nivel: Carver, Devilkin, Dark One) y en los demás,
       monstruos de la zona */
    if (strncmp(place, "place_", 6) == 0) {
        if (level <= 0)
            return;
        int tier = level <= 3 ? 1 : level <= 6 ? 2 : level <= 9 ? 3 : 4;
        char fallen[16], shaman[24];
        snprintf(fallen, sizeof fallen, "fallen%d", tier);
        snprintf(shaman, sizeof shaman, "fallenshaman%d", tier);
        int chance = strncmp(place, "place_group", 11) == 0 ? atoi(place + 11) : 100;
        const char *other = s_picker ? s_picker(x, y) : NULL;
        if (strcmp(place, "place_fallen") == 0) {
            spawn_group(fallen, 3 + (int)(spawn_rnd() % 3), x, y, level);
        } else if ((strncmp(place, "place_group", 11) == 0 || strcmp(place, "place_champion") == 0) && other) {
            if ((int)(spawn_rnd() % 100) < chance)
                spawn_group(other, 2 + (int)(spawn_rnd() % 3), x, y, level);
        } else if (strcmp(place, "place_unique_pack") == 0 && other) {
            units_spawn_boss_pack(other, x, y, level);
        } else if (strcmp(place, "place_bloodraven") == 0) {
            /* Blood Raven, con su nivel de monstats.txt, y algunos de sus zombis */
            int ms = mon_row(&s_monstats, "bloodraven");
            int lvl = txt_get_int(&s_monstats, ms, "Level", level);
            Unit *br = create_monster("bloodraven", x, y, lvl > level ? lvl : level, 1);
            if (br)
                br->minion_level = level;
            spawn_group("zombie2", 3, x + 2.0f, y + 2.0f, level);
        } else if (strcmp(place, "place_fallenshaman") == 0) {
            units_spawn_monster(shaman, x, y, level);
            spawn_group(fallen, 2, x + 1.5f, y + 1.5f, level);
        } else if (act == 3) {
            /* El Acto 3: las ranas del pantano junto al agua, los Water Watchers
               de los canales de Kurast (la cabeza y sus tentáculos, a lo largo o
               a lo ancho del canal) y los fetiches (shortcut: los nidos de
               mosquitos, suckernest, están deshabilitados en monstats.txt) */
            char a[24], b[24];
            if (strcmp(place, "place_amphibian") == 0 && monster_variant("frogdemon", level, a, sizeof a) == 0) {
                spawn_group(a, 2 + (int)(spawn_rnd() % 3), x, y, level);
            } else if (strncmp(place, "place_tentacle_", 15) == 0) {
                /* D2GAME_SpawnPresetMonster (monplace 10 y 11): la cabeza según el nivel (en la
                   selva la segunda, en las cloacas la tercera) y PartyMin..PartyMax de sus
                   tentáculos alrededor, en las posiciones de sub_6FC6A8C0 (unas para _ns, otras
                   para _ew), con la cabeza de dueña */
                static const int8_t k_off[12][2] = { { -1, -4 }, { 1, 4 }, { 1, -3 }, { -1, 3 }, { 0, 2 },  { 0, -2 },
                                                     { -3, -1 }, { 3, 1 }, { 2, -1 }, { -2, 1 }, { 1, 0 }, { -1, 0 } };
                int lid = s_level_id_at ? s_level_id_at(x, y) : 0;
                const char *hid = lid >= 76 && lid <= 78 ? "tentaclehead2" : lid == 92 || lid == 93 ? "tentaclehead3" : "tentaclehead1";
                Unit *head = create_monster(hid, x, y, level, 1);
                int hms = mon_row(&s_monstats, hid);
                if (head && hms >= 0) {
                    int self = (int)(head - s_units), lo = txt_get_int(&s_monstats, hms, "PartyMin", 0);
                    int hi = txt_get_int(&s_monstats, hms, "PartyMax", 0);
                    int n = hi > lo ? lo + (int)(spawn_rnd() % (unsigned)(hi - lo + 1)) : lo;
                    int set = strcmp(place + 15, "ns") == 0 ? 6 : 0, k = (int)(spawn_rnd() % 6);
                    const char *limb = txt_get(&s_monstats, hms, txt_col(&s_monstats, "minion1"));
                    for (int j = 0; j < n && limb[0]; j++, k = (k + 5) % 6) {
                        Unit *t = create_monster(limb, x + k_off[set + k][0], y + k_off[set + k][1], level, 1);
                        if (t)
                            t->master = self;
                    }
                    if (getenv("D2_AI_DEBUG"))
                        printf("IA: %s en (%.0f, %.0f) con %d %s (nivel %d)\n", hid, x, y, n, limb, lid);
                }
            } else if ((strcmp(place, "place_fetish") == 0 || strcmp(place, "place_fetishnest") == 0) &&
                       monster_variant("fetish", level, a, sizeof a) == 0) {
                spawn_group(a, (strcmp(place, "place_fetishnest") == 0 ? 4 : 3) + (int)(spawn_rnd() % 3), x, y, level);
            } else if (strcmp(place, "place_fetishshaman") == 0 && monster_variant("fetishshaman", level, a, sizeof a) == 0 &&
                       monster_variant("fetish", level, b, sizeof b) == 0) {
                units_spawn_monster(a, x, y, level);
                spawn_group(b, 2 + (int)(spawn_rnd() % 2), x + 1.5f, y + 1.5f, level);
            }
        } else if (act == 5) {
            /* El Acto 5 (D2GAME_SpawnPresetMonster_6FC66560): en Normal, grupos (MinGrp..MaxGrp) de
               diablillos (en el Bloody Foothills, Death Maulers) y de esbirros; los cadáveres de un
               esbirro, de un diablillo (o Death Mauler) y de un bárbaro; la horda reanimada caída que
               se levanta a los 10 a 12 segundos. place_imp, place_minion y place_bloodlord no ponen nada. */
            int lid = s_level_id_at ? s_level_id_at(x, y) : 0;
            const char *base = strcmp(place, "place_impgroup") == 0 || strcmp(place, "place_deadimp") == 0
                                   ? (lid == 110 ? "deathmauler" : "imp")
                               : strcmp(place, "place_miniongroup") == 0 || strcmp(place, "place_deadminion") == 0 ? "minion"
                               : strcmp(place, "place_deadbarb") == 0                                            ? "act5barb"
                               : strcmp(place, "place_reanimateddead") == 0                                      ? "reanimatedhorde"
                                                                                                                 : NULL;
            char a[24];
            if (!base || monster_variant(base, level, a, sizeof a) != 0)
                return;
            if (strstr(place, "group")) {
                int ms = mon_row(&s_monstats, a), lo = txt_get_int(&s_monstats, ms, "MinGrp", 2), hi = txt_get_int(&s_monstats, ms, "MaxGrp", 4);
                spawn_group(a, lo + (hi > lo ? (int)(spawn_rnd() % (uint32_t)(hi - lo + 1)) : 0), x, y, level);
                return;
            }
            Unit *c = create_monster(strcmp(base, "act5barb") == 0 ? "act5barb1" : a, x, y, level, 1);
            if (!c)
                return;
            c->hp = 0;
            c->solid = 0;
            set_state(c, ST_DEAD);
            c->anim_acc = 0;
            if (strcmp(base, "reanimatedhorde") == 0) /* (a5_reanimate la levanta cuando ai_p[2] llega a 3 s) */
                c->ai_p[2] = 3 * TICK_RATE - (250 + (int)(spawn_rnd() % 50u)) * TICK_RATE / 25;
            else
                c->ai_p[1] = 1, c->exp = 0, c->tc[0] = '\0', c->searched = 1;
        }
        return;
    }
    if (spawn_superunique(place, x, y, level) >= 0)
        return;
    /* Fuera del pueblo, los que no son NPC son enemigos (Andariel), salvo los
       del lado de los jugadores (los bárbaros del Acto 5) */
    int ms = mon_row(&s_monstats, place);
    if (level > 0 && ms >= 0 && txt_get_int(&s_monstats, ms, "npc", 0) != 1 &&
        txt_get_int(&s_monstats, ms, "Align", 0) == 1) {
        barb_spawn(place, ms, x, y, level);
        return;
    }
    int enemy = level > 0 && ms >= 0 && txt_get_int(&s_monstats, ms, "npc", 0) != 1;
    Unit *u = create_monster(place, x, y, level, enemy);
    /* El NPC que queda sobre algo bloqueado (Tyrael, en la tarima de Tal
       Rasha) va al lugar libre más cercano, como pone el original a cada
       unidad (COLLISION_GetFreeCoordinates): si no, no se llega a hablarle */
    for (int r = 1; u && u->npc && s_map && wall_foot(s_map, u->x, u->y, u->foot, u->move_mask) > 0 && r <= 8; r++)
        for (int a = 0; a < 8 * r; a++) {
            float nx = x + cosf((float)a * PI_F / (4.0f * r)) * (float)r, ny = y + sinf((float)a * PI_F / (4.0f * r)) * (float)r;
            if (wall_foot(s_map, nx, ny, u->foot, u->move_mask) == 0) {
                u->x = u->prev_x = u->home_x = nx;
                u->y = u->prev_y = u->home_y = ny;
                grid_moved(u);
                break;
            }
        }
}

static const char *const k_obj_modes[8] = { "nu", "op", "on", "s1", "s2", "s3", "s4", "s5" };

static int obj_mode_index(const char *mode)
{
    for (int k = 0; k < 8; k++)
        if (strcmp(k_obj_modes[k], mode) == 0)
            return k;
    return 0;
}

/* Los cuadros de ese modo según objects.txt (0 cuadros = todos los del COF) */
static void object_frames(Unit *u, int mode)
{
    char col[16];
    snprintf(col, sizeof col, "FrameCnt%d", mode);
    u->obj_frames = txt_get_int(&s_objects, u->obj_row, col, 0);
    snprintf(col, sizeof col, "Start%d", mode);
    u->obj_first = txt_get_int(&s_objects, u->obj_row, col, 0);
    snprintf(col, sizeof col, "CycleAnim%d", mode);
    u->obj_cycle = txt_get_int(&s_objects, u->obj_row, col, 0);
}

static void object_look(int row, CharLook *look)
{
    memset(look, 0, sizeof *look);
    strcpy(look->base, "objects");
    /* El Token son 2 letras (DATATBLS_LoadObjectsTxt: TXTFIELD_ASCII de largo 2): el de RedLight es "ss " */
    lower_copy(look->token, 3, txt_get(&s_objects, row, txt_col(&s_objects, "Token")));
    for (int c = 0; c < COMP_COUNT; c++)
        if (txt_get_int(&s_objects, row, k_comp_names[c], 0) == 1)
            strcpy(look->part[c], "lit");
}

/* Los gráficos de un objeto de objects.txt en ese modo. Se recuerdan por
   fila y modo (los cofres y barriles de cada sala buscaban de nuevo en toda
   la tabla de gráficos): vale mientras ese lugar siga con la misma clave. */
static struct {
    UnitGfx *g;
    int      row, mode;
    char     key[32];
} s_obj_gfx_memo[256];

static UnitGfx *object_gfx(int row, const char *mode)
{
    int mi = obj_mode_index(mode);
    unsigned slot = ((unsigned)row * 8u + (unsigned)mi) & 255u;
    UnitGfx *m = s_obj_gfx_memo[slot].g;
    if (m && s_obj_gfx_memo[slot].row == row && s_obj_gfx_memo[slot].mode == mi && strcmp(k_obj_modes[mi], mode) == 0 &&
        strcmp(m->key, s_obj_gfx_memo[slot].key) == 0 && gfx_state(m) != GFX_FAILED)
        return m;
    CharLook look;
    object_look(row, &look);
    char key[32];
    snprintf(key, sizeof key, "objects/%s/%s", look.token, mode);
    const char *modes[ANIM_COUNT] = { mode };
    UnitGfx *g = get_gfx(key, &look, modes, "hth");
    s_obj_gfx_memo[slot].g = g;
    s_obj_gfx_memo[slot].row = row;
    s_obj_gfx_memo[slot].mode = mi;
    snprintf(s_obj_gfx_memo[slot].key, sizeof s_obj_gfx_memo[slot].key, "%s", key);
    return g;
}

static Unit *spawn_object_row(Map *m, int row, const char *cls, float x, float y, const char *force_mode);

/* Un objeto de objects.txt por su clase; `force_mode` NULL = el de siempre */
static Unit *spawn_object_class(Map *m, const char *cls, float x, float y, const char *force_mode)
{
    return spawn_object_row(m, txt_find_row(&s_objects, txt_col(&s_objects, "Class"), cls), cls, x, y, force_mode);
}

/* Lo mismo por su fila (el reparto de objects.inc ya la sabe). `cls` es el
   nombre que queda (el de objpreset.txt puede venir con otras mayúsculas:
   LamesEnsTome); NULL = el de objects.txt */
static Unit *spawn_object_row(Map *m, int row, const char *cls, float x, float y, const char *force_mode)
{
    if (!cls)
        cls = txt_get(&s_objects, row, txt_col(&s_objects, "Class"));
    /* El lugar de Larzuk en Harrogath: ahí sale él (OBJECTS_InitFunction71_LarzukStandard),
       o en el lugar libre más cercano */
    if (row >= 0 && txt_get_int(&s_objects, row, "InitFn", 0) == 71) {
        int i = units_spawn_npc("larzuk", x, y);
        /* (el marcador queda sobre su yunque: al lugar libre más cercano, como los NPC de los DS1) */
        Unit *u = i >= 0 ? &s_units[i] : NULL;
        for (int r = 1; u && m && wall_foot(m, u->x, u->y, u->foot, u->move_mask) > 0 && r <= 8; r++)
            for (int a = 0; a < 8 * r; a++) {
                float nx = x + cosf((float)a * PI_F / (4.0f * r)) * (float)r, ny = y + sinf((float)a * PI_F / (4.0f * r)) * (float)r;
                if (wall_foot(m, nx, ny, u->foot, u->move_mask) == 0) {
                    u->x = u->prev_x = u->home_x = nx;
                    u->y = u->prev_y = u->home_y = ny;
                    grid_moved(u);
                    break;
                }
            }
        return NULL;
    }
    /* marcadores invisibles (sonido ambiente, puntos de aparición); los de los
       glifos del Invocador sí (dibujo vacío ss, como los de Jerhyn): ahí flotan;
       y el lugar donde sale Diablo (a4quest.inc) */
    if (row < 0 || (txt_get_int(&s_objects, row, "Draw", 0) == 0 && strcmp(cls, "PlaceArcaneThingamajig") != 0 &&
                    strcmp(cls, "DiabloStart") != 0))
        return NULL;

    /* Fogatas, antorchas y el waypoint del pueblo arrancan encendidos */
    int mode = 0;
    if (txt_get_int(&s_objects, row, "Mode2", 0) == 1 && txt_get_int(&s_objects, row, "CycleAnim2", 0) == 1)
        mode = 2;
    if (force_mode)
        mode = obj_mode_index(force_mode);

    UnitGfx *g = object_gfx(row, k_obj_modes[mode]);
    Unit *u = g ? new_unit(x, y) : NULL;
    if (!u) {
        gfx_ref(g, 0);
        return NULL;
    }
    u->gfx = g;
    u->obj_row = row;
    u->obj_mode = mode;
    object_frames(u, mode);
    CharLook look;
    object_look(row, &look);
    char key[32];
    /* Los waypoints tienen además el dibujo apagado (hasta que alguien los toca) */
    if (strncmp(cls, "Waypoint", 8) == 0) {
        snprintf(key, sizeof key, "objects/%s/nu", look.token);
        const char *off[ANIM_COUNT] = { "nu" };
        u->gfx_on = g;
        u->gfx_off = get_gfx(key, &look, off, "hth");
    }
    unit_gfx_refs(u, 1);
    snprintf(u->name, sizeof u->name, "%s", cls);
    u->display = u->name;
    u->sector = 0;
    char col[16];
    snprintf(col, sizeof col, "FrameDelta%d", mode);
    u->anim_speed = txt_get_int(&s_objects, row, col, 256);
    u->anim_acc = (int)(rnd(u) % 4096); /* que las antorchas no titilen todas juntas */

    snprintf(col, sizeof col, "HasCollision%d", mode);
    if (m && txt_get_int(&s_objects, row, col, 0) == 1) {
        /* Lo que marca según objects.txt, como UNITS_GetCollisionMask del original */
        int door = txt_get_int(&s_objects, row, "IsDoor", 0), missile = txt_get_int(&s_objects, row, "BlockMissile", 0);
        uint16_t bits = door ? (txt_get_int(&s_objects, row, "BlocksVis", 0) ? COLL_DOOR | COLL_MISSILE_BARRIER | COLL_VISIBLE
                                : missile                                     ? COLL_DOOR | COLL_MISSILE_BARRIER
                                                                              : COLL_OBJECT)
                         : (txt_get_int(&s_objects, row, "SubClass", 0) & 0x04) ? COLL_CORPSE /* portal al pueblo */
                         : missile                                                ? COLL_OBJECT | COLL_MISSILE_BARRIER
                                                                                  : COLL_OBJECT;
        int sx = txt_get_int(&s_objects, row, "SizeX", 1), sy = txt_get_int(&s_objects, row, "SizeY", 1);
        for (int dy = 0; dy < sy; dy++)
            for (int dx = 0; dx < sx; dx++)
                map_set_coll(m, (int)x - sx / 2 + dx, (int)y - sy / 2 + dy, bits);
    }
    return u;
}

static void spawn_object(Map *m, int act, int id, float x, float y)
{
    int col_index = txt_col(&s_objpreset, "Index"), col_act = txt_col(&s_objpreset, "Act");
    for (int r = 1; r < s_objpreset.rows; r++)
        if (atoi(txt_get(&s_objpreset, r, col_index)) == id && atoi(txt_get(&s_objpreset, r, col_act)) == act) {
            spawn_object_class(m, txt_get(&s_objpreset, r, txt_col(&s_objpreset, "ObjectClass")), x, y, NULL);
            return;
        }
}

int units_spawn_object(const char *cls, float x, float y, const char *mode)
{
    Unit *u = spawn_object_class(NULL, cls, x, y, mode);
    return u ? (int)(u - s_units) : -1;
}

void units_object_unblock(Map *m, int i)
{
    if (!m || i < 0 || i >= s_num_units || s_units[i].obj_row <= 0)
        return;
    const Unit *u = &s_units[i];
    int sx = txt_get_int(&s_objects, u->obj_row, "SizeX", 1), sy = txt_get_int(&s_objects, u->obj_row, "SizeY", 1);
    for (int dy = 0; dy < sy; dy++)
        for (int dx = 0; dx < sx; dx++)
            map_clear_coll(m, (int)u->x - sx / 2 + dx, (int)u->y - sy / 2 + dy,
                           COLL_OBJECT | COLL_MISSILE_BARRIER | COLL_DOOR | COLL_VISIBLE);
}

void units_object_mode(int i, const char *mode, const char *next)
{
    if (i < 0 || i >= s_num_units)
        return;
    Unit *u = &s_units[i];
    if (u->obj_row <= 0 || u->gfx_on || u->gfx_off)
        return;
    UnitGfx *g = object_gfx(u->obj_row, mode);
    if (!g)
        return;
    gfx_ref(g, 1);
    gfx_ref(u->gfx, -1);
    u->gfx = g;
    u->anim_acc = 0;
    char col[16];
    snprintf(col, sizeof col, "FrameDelta%d", obj_mode_index(mode));
    u->anim_speed = txt_get_int(&s_objects, u->obj_row, col, 256);
    u->obj_mode = obj_mode_index(mode);
    object_frames(u, u->obj_mode);
    snprintf(u->obj_next, sizeof u->obj_next, "%s", next ? next : "");
}

int units_place_object(Map *m, const char *cls, float x, float y)
{
    Unit *u = spawn_object_class(m, cls, x, y, NULL);
    return u ? (int)(u - s_units) : -1;
}

int units_place_object_row(Map *m, int row, float x, float y)
{
    Unit *u = row > 0 && row < s_objects.rows ? spawn_object_row(m, row, NULL, x, y, NULL) : NULL;
    return u ? (int)(u - s_units) : -1;
}

int units_object_row(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].zone != ZONE_FREE && s_units[i].obj_row > 0 ? s_units[i].obj_row : 0;
}

int units_object_cur_mode(int i)
{
    return units_object_row(i) ? s_units[i].obj_mode : -1;
}

int units_object_state(int i)
{
    return units_object_row(i) ? s_units[i].obj_state : 0;
}

void units_object_set_state(int i, int state)
{
    if (units_object_row(i))
        s_units[i].obj_state = state;
}

/* La animación del modo de ahora, ya terminada (el cofre que ya estaba abierto) */
void units_object_finish(int i)
{
    if (!units_object_row(i))
        return;
    Unit *u = &s_units[i];
    if (u->obj_next[0]) {
        char next[4];
        snprintf(next, sizeof next, "%s", u->obj_next);
        units_object_mode(i, next, NULL);
    }
    u->anim_acc = (u->obj_frames > 0 ? u->obj_frames : 64) << 8;
}

int units_objects_find(const char *cls)
{
    return txt_find_row(&s_objects, txt_col(&s_objects, "Class"), cls);
}

int units_objects_row_of_id(int id)
{
    char key[12];
    snprintf(key, sizeof key, "%d", id);
    if (id + 1 > 0 && id + 1 < s_objects.rows && strcmp(txt_get(&s_objects, id + 1, txt_col(&s_objects, "*ID")), key) == 0)
        return id + 1;
    return txt_find_row(&s_objects, txt_col(&s_objects, "*ID"), key);
}

int units_objects_int(int row, const char *col, int def)
{
    return row > 0 && row < s_objects.rows ? txt_get_int(&s_objects, row, col, def) : def;
}

const char *units_objects_str(int row, const char *col)
{
    return row > 0 && row < s_objects.rows ? txt_get(&s_objects, row, txt_col(&s_objects, col)) : "";
}

void units_set_picker(const char *(*pick)(float x, float y))
{
    s_picker = pick;
}

/* Un monstruo que no es enemigo ni habla (los bárbaros presos del Acto 5): pasea en su lugar */
int units_spawn_neutral(const char *id, float x, float y, int level)
{
    Unit *u = create_monster(id, x, y, level, 0);
    if (!u)
        return -1;
    u->wander = 1.5f;
    return (int)(u - s_units);
}

void units_walk_to(int i, float x, float y)
{
    if (i < 0 || i >= s_num_units)
        return;
    Unit *u = &s_units[i];
    u->goal = 1;
    u->goal_x = x;
    u->goal_y = y;
}

int units_spawn_npc(const char *id, float x, float y)
{
    Unit *u = create_monster(id, x, y, 0, 0);
    if (!u)
        return -1;
    u->npc = 1;
    /* Los guardias (AI JarJar: Kaelan en la puerta del palacio) no se mueven de su lugar */
    int ms = mon_row(&s_monstats, id);
    int guard = ms >= 0 && strcmp(txt_get(&s_monstats, ms, txt_col(&s_monstats, "AI")), "JarJar") == 0;
    u->wander = gfx_has(u->gfx, ANIM_WALK) && !guard ? 4.0f : 0.0f;
    return (int)(u - s_units);
}

void units_zone_begin(int zone)
{
    s_spawn_zone = zone;
}

void units_zone_end(void)
{
    s_spawn_zone = -1;
}

/* Lo que sale de una baldosa de puerta (DRLGROOMTILE_AddTilePresetUnits, la
   parte del Acto 5): en Harrogath, el portón; en las barricadas (solo las
   que se ven), las torres, puertas y muros de las barricadas, las puertas
   de las prisiones y los portales rojos a los infiernos.
   shortcut: el portón sale abierto (el original arranca cerrado y se abre
   con un clic; acá todavía no hay puertas que se abren) */
static void spawn_tile_unit(Map *m, const MapObject *o, int level)
{
    static const struct {
        uint8_t     style, seq, right, obj;
        const char *id;
        int8_t      dx, dy;
    } k_tile[] = {
        { 3, 3, 0, 1, "Gate", -2, 4 },
        { 2, 1, 0, 0, "barricadetower", 1, 2 },  { 2, 1, 1, 0, "barricadetower", 2, 1 }, { 2, 6, 0, 0, "barricadetower", 1, 1 },
        { 2, 2, 0, 0, "barricadedoor2", 0, 1 },  { 2, 3, 1, 0, "barricadedoor1", 1, 0 }, { 26, 0, 0, 0, "prisondoor", 0, 1 },
        { 2, 4, 1, 0, "barricadewall1", 0, 0 },  { 2, 4, 0, 0, "barricadewall2", 0, 0 },
        { 29, 0, 1, 1, "PortalPermanent", 2, 0 }, { 29, 0, 0, 1, "PortalPermanent", 0, 2 },
    };
    int lid = s_level_id_at ? s_level_id_at(o->x, o->y) : 0, lo, hi;
    if (lid == 109) { /* Harrogath: el portón (shortcut: sin la torre, que acá sería un enemigo en el pueblo) */
        lo = 0;
        hi = 0;
    } else if ((lid == 111 || lid == 112 || lid == 117) && !(o->id >> 16 & 1)) {
        lo = 1;
        hi = (int)(sizeof k_tile / sizeof k_tile[0]) - 1;
    } else {
        return;
    }
    int style = o->id >> 8 & 0xFF, seq = o->id >> 1 & 0x7F, right = o->id & 1;
    for (int k = lo; k <= hi; k++) {
        if (k_tile[k].style != style || k_tile[k].seq != seq || k_tile[k].right != right)
            continue;
        float x = o->x + k_tile[k].dx + 0.5f, y = o->y + k_tile[k].dy + 0.5f;
        if (k_tile[k].obj)
            spawn_object_class(m, k_tile[k].id, x, y, k == 0 ? "on" : NULL);
        else
            create_monster(k_tile[k].id, x, y, level, 1);
        return;
    }
}

int units_spawn_presets(Map *m, int (*level_of)(float x, float y), int from, int cap, double ms)
{
    s_map = m;
    double until = ms > 0 ? map_now_ms() + ms : 0;
    int i = from > 0 ? from : 0;
    for (int done = 0; i < m->num_objects; i++, done++) {
        if (done > 0 && ((cap > 0 && done >= cap) || (until > 0 && map_now_ms() > until)))
            break;
        const MapObject *o = &m->objects[i];
        if (o->type == 3) {
            spawn_tile_unit(m, o, level_of ? level_of(o->x, o->y) : 1);
            continue;
        }
        if (o->type == 1 && o->act == 3 && o->id == 21) { /* el Orbe cautivante: un objeto (que se golpea) y el orbe que se ve (DRLGPRESET) */
            spawn_object_class(m, "CompellingOrb", o->x, o->y, NULL);
            create_monster("compellingorb", o->x, o->y, level_of ? level_of(o->x, o->y) : 1, 0);
        } else if (o->type == 1)
            spawn_preset_monster(o->act, o->id, o->x, o->y, level_of ? level_of(o->x, o->y) : 0);
        else if (o->type == 2)
            spawn_object(m, o->act, o->id, o->x, o->y);
    }
    return i;
}

/* ---- Movimiento e IA ---- */

static int sector_from_world(float dx, float dy)
{
    float sx = (dx - dy) * (SUBTILE_W / 2), sy = (dx + dy) * (SUBTILE_H / 2);
    int s = (int)lroundf((atan2f(sy, sx) - PI_F / 2.0f) / (PI_F / 8.0f));
    return ((s % 16) + 16) % 16;
}

static int current_anim(const Unit *u)
{
    const UnitGfx *g = u->gfx;
    int a;
    switch (u->state) {
    case ST_ATTACK: a = u->attack_anim ? u->attack_anim : ANIM_ATTACK; break;
    case ST_RISE:   a = ANIM_SKILL; break;
    case ST_HIT:    a = ANIM_HIT; break;
    case ST_DYING:  a = ANIM_DEATH; break;
    case ST_DEAD:   a = g->anim[ANIM_DEAD].ok ? ANIM_DEAD : ANIM_DEATH; break;
    default:        a = !u->walking ? ANIM_IDLE : u->running && g->anim[ANIM_RUN].ok ? ANIM_RUN : ANIM_WALK; break;
    }
    return g->anim[a].ok ? a : ANIM_IDLE;
}

static void set_state(Unit *u, int state)
{
    u->state = state;
    u->anim_acc = 0;
    u->walking = 0;
    const MonSounds *s = u->snd;
    if (!s)
        return;
    if (state == ST_ATTACK) {
        if (u->res_target >= 0 || u->attack_anim == ANIM_SKILL) { /* revive o invoca */
            unit_sound(u, s->skill[0], 0, -1);
            return;
        }
        if (u->attack_anim == ANIM_CAST && s->cast >= 0) { /* Andariel escupe veneno */
            unit_sound(u, s->cast, 0, -1);
            return;
        }
        int a = u->attack_anim == ANIM_ATTACK2 ? 1 : 0;
        if ((int)(rnd(u) % 100) < s->att_prb[a])
            unit_sound(u, s->attack[a], s->att_del[a], -1);
        unit_sound(u, s->weapon[a], s->wea_del[a], s->wea_vol[a]);
    } else if (state == ST_RISE) {
        unit_sound(u, s->skill[0], 0, -1); /* zombie_raise */
    } else if (state == ST_HIT) {
        unit_sound(u, s->hit, s->hit_del, -1);
    } else if (state == ST_DYING) {
        for (int k = 0; k < SOUND_PENDING; k++)
            u->snd_delay[k] = 0; /* se cortan los gritos del ataque */
        unit_sound(u, s->death, s->death_del, -1);
    }
    if (state == ST_DYING && (HAS_MOD(u, MOD_FIRE) || HAS_MOD(u, MOD_COLD)))
        u->boom = 1;
}

/* Sonidos por tick: los que estaban esperando, los pasos y los ruidos sueltos */
static void sound_tick(Unit *u)
{
    for (int k = 0; k < SOUND_PENDING; k++)
        if (u->snd_delay[k] > 0 && --u->snd_delay[k] == 0)
            audio_play_vol(u->snd_id[k], u->x, u->y, u->snd_vol[k]);
    const MonSounds *s = u->snd;
    if (!s || u->state >= ST_DYING)
        return;
    if (s->neutral >= 0 && s->neu_time > 0 && (u->state == ST_IDLE || u->state == ST_CHASE) && --u->neutral_timer <= 0) {
        audio_play(s->neutral, u->x, u->y);
        u->neutral_timer = s->neu_time + (int)(rnd(u) % (uint32_t)s->neu_time);
    }
    if (s->footstep >= 0 && s->fs_cnt > 0 && u->walking && u->state < ST_ATTACK) {
        int frames = charanim_frames(&u->gfx->anim[ANIM_WALK]);
        int frame = frames > 0 ? (u->anim_acc >> 8) % frames : 0;
        if (frame != u->step_frame) {
            u->step_frame = frame;
            for (int k = 0; k < s->fs_cnt; k++)
                if (frame == (s->fs_off + k * frames / s->fs_cnt) % (frames > 0 ? frames : 1) &&
                    (int)(rnd(u) % 100) < s->fs_prb)
                    audio_play(s->footstep, u->x, u->y);
        }
    }
}

/* Dónde están los jugadores (para no pisarlos al caminar). Es una copia: el
   empujón de Porrazo mueve monstruos fuera de units_tick, cuando la lista
   que llega a units_tick ya no existe. */
#define TICK_PLAYERS 8
static UnitTarget s_tick_pl[TICK_PLAYERS];
static int        s_tick_npl;

/* Otro monstruo o un jugador demasiado cerca (según el tamaño de cada uno):
   evita que se apilen. Si ya estaba encimado, lo deja alejarse. */
static int crowded(const Unit *self, float x, float y)
{
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == self || !o->solid)
            continue;
        float dx = o->x - x, dy = o->y - y, r = (self->radius + o->radius) * 0.9f;
        if (dx * dx + dy * dy < r * r) {
            float cx = o->x - self->x, cy = o->y - self->y;
            if (dx * dx + dy * dy < cx * cx + cy * cy - 0.0001f) /* se acerca más */
                return 1;
        }
    }
    for (int i = 0; self->enemy && i < s_tick_npl; i++) {
        if (!s_tick_pl[i].alive)
            continue;
        float dx = s_tick_pl[i].x - x, dy = s_tick_pl[i].y - y, r = UNIT_PLAYER_BODY + self->radius;
        if (dx * dx + dy * dy < r * r)
            return 1;
    }
    return 0;
}

/* Contra las paredes, como en el original (D2Collision): según su tamaño
   (SizeX de monstats2.txt) un monstruo ocupa un subtile, una cruz con los
   cuatro de al lado o un cuadrado de 3x3. Devuelve cuántos de esos subtiles
   están bloqueados (0 = entra). */
static int wall_foot(const Map *m, float x, float y, int size, uint16_t mask)
{
    int sx = (int)floorf(x), sy = (int)floorf(y), n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int ring = (dx != 0) + (dy != 0); /* 0 centro, 1 cruz, 2 esquinas */
            if (ring < size)
                n += (map_coll(m, sx + dx, sy + dy) & mask) != 0;
        }
    return n;
}

/* ¿Puede pasar a (x, y)? Si ya estaba encimado a una pared (recién
   aparecido o empujado), puede salir sin meterse más */
static int unit_step_ok(const Map *m, const Unit *u, float x, float y)
{
    int to = wall_foot(m, x, y, u->foot, u->move_mask);
    if (to == 0)
        return 1;
    int from = wall_foot(m, u->x, u->y, u->foot, u->move_mask);
    return from > 0 && to <= from && !(map_coll(m, (int)floorf(x), (int)floorf(y)) & u->move_mask);
}

static void move_towards(Unit *u, const Map *m, float tx, float ty)
{
    float dx = tx - u->x, dy = ty - u->y, d = sqrtf(dx * dx + dy * dy);
    if (d < 0.01f)
        return;
    float step = u->speed / TICK_RATE;
    if (step > d)
        step = d;
    float nx = u->x + dx / d * step, ny = u->y + dy / d * step, ox = u->x, oy = u->y;
    if (unit_step_ok(m, u, nx, u->y) && !crowded(u, nx, u->y))
        u->x = nx;
    if (unit_step_ok(m, u, u->x, ny) && !crowded(u, u->x, ny))
        u->y = ny;
    if (u->x == ox && u->y == oy && unit_step_ok(m, u, nx, ny) && !crowded(u, nx, ny)) { /* en diagonal (las escaleras angostas) */
        u->x = nx;
        u->y = ny;
    }
    grid_moved(u);
    u->walking = u->x != u->prev_x || u->y != u->prev_y;
    u->sector = sector_from_world(dx, dy);
}

/* ¿Ese lugar está libre para este monstruo? (sin otro encima ni el lugar
   que eligió otro para pegarle al jugador) */
static int spot_free(const Unit *self, float x, float y)
{
    for (int i = 0; i < s_num_units; i++) {
        const Unit *o = &s_units[i];
        if (o == self || !o->solid || !o->enemy || o->state >= ST_DYING)
            continue;
        float r = self->radius + o->radius;
        float dx = o->x - x, dy = o->y - y;
        if (dx * dx + dy * dy < r * r * 0.81f)
            return 0;
        dx = o->slot_x - x;
        dy = o->slot_y - y;
        if (o->has_slot && dx * dx + dy * dy < r * r * 0.81f)
            return 0;
    }
    return 1;
}

/* Un lugar libre alrededor del jugador para pegarle: primero del lado de
   donde viene y después probando de a 30 grados hacia los costados. Así
   varios lo rodean en vez de hacer fila. 0 = no hay. */
static int surround_spot(Unit *u, const Map *m, const UnitTarget *t, float *ox, float *oy)
{
    float ring = UNIT_PLAYER_BODY + u->radius + 0.15f;
    if (u->has_slot) { /* el que ya eligió, mientras siga libre y pegado al jugador */
        float dx = u->slot_x - t->x, dy = u->slot_y - t->y;
        if (fabsf(sqrtf(dx * dx + dy * dy) - ring) < 0.3f && spot_free(u, u->slot_x, u->slot_y)) {
            *ox = u->slot_x;
            *oy = u->slot_y;
            return 1;
        }
        u->has_slot = 0;
    }
    float base = atan2f(u->y - t->y, u->x - t->x);
    for (int k = 0; k < 12; k++) {
        float a = base + (float)((k + 1) / 2) * (k % 2 ? 1.0f : -1.0f) * (PI_F / 6.0f);
        float x = t->x + cosf(a) * ring, y = t->y + sinf(a) * ring;
        if (wall_foot(m, x, y, u->foot, u->move_mask) || !spot_free(u, x, y))
            continue;
        u->slot_x = *ox = x;
        u->slot_y = *oy = y;
        u->has_slot = 1;
        return 1;
    }
    return 0;
}

static int near_player(float x, float y, const UnitTarget *pl, int n)
{
    for (int i = 0; i < n; i++) {
        float dx = pl[i].x - x, dy = pl[i].y - y;
        if (dx * dx + dy * dy < 1.0f)
            return 1;
    }
    return 0;
}

/* Pasear cerca de su lugar, con pausas */
static void wander_tick(Unit *u, const Map *m, const UnitTarget *pl, int n)
{
    if (u->talk_ticks > 0) {
        u->talk_ticks--;
        u->walking = 0;
        return;
    }
    if (u->wander <= 0.0f)
        return;
    if (!u->walking) {
        if (--u->timer <= 0) {
            float a = rndf(u) * 2.0f * PI_F, r = 1.0f + rndf(u) * (u->wander - 1.0f);
            float tx = u->home_x + cosf(a) * r, ty = u->home_y + sinf(a) * r;
            if (!wall_foot(m, tx, ty, u->foot, u->move_mask)) {
                u->target_x = tx;
                u->target_y = ty;
                u->walking = 1;
                u->anim_acc = 0;
            }
            u->timer = 50 + (int)(rnd(u) % 100);
        }
        return;
    }
    float dx = u->target_x - u->x, dy = u->target_y - u->y;
    float d = sqrtf(dx * dx + dy * dy), step = u->speed / TICK_RATE;
    float nx = d > step ? u->x + dx / d * step : u->x;
    float ny = d > step ? u->y + dy / d * step : u->y;
    if (d <= step || !unit_step_ok(m, u, nx, ny) || near_player(nx, ny, pl, n)) {
        u->walking = 0;
        u->anim_acc = 0;
    } else {
        u->x = nx;
        u->y = ny;
        grid_moved(u);
        u->sector = sector_from_world(dx, dy);
    }
}

static int in_radius_xy(const Unit *u, float x, float y, float radius)
{
    float dx = u->x - x, dy = u->y - y;
    return dx * dx + dy * dy <= radius * radius;
}

/* El jugador más cercano de los que lo tienen en su pantalla (con `scale`
   > 1, un poco más allá del borde); -1 = ninguno */
static int nearest_player_seen(const Unit *u, const UnitTarget *pl, int n, float scale, float *dist)
{
    int best = -1;
    float best_d = 1e9f;
    for (int i = 0; i < n; i++) {
        if (!pl[i].alive || pl[i].in_town)
            continue;
        float sx, sy;
        iso_to_screen(pl[i].x - u->x, pl[i].y - u->y, &sx, &sy);
        if (fabsf(sx) > SCREEN_W * 0.5f * scale || fabsf(sy) > SCREEN_H * 0.5f * scale)
            continue;
        float dx = pl[i].x - u->x, dy = pl[i].y - u->y, d = sqrtf(dx * dx + dy * dy) - pl[i].body;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    *dist = best_d;
    return best;
}

static int nearest_player(const Unit *u, const UnitTarget *pl, int n, float range, float *dist)
{
    int best = -1;
    float best_d = range;
    for (int i = 0; i < n; i++) {
        if (!pl[i].alive || pl[i].in_town)
            continue;
        float dx = pl[i].x - u->x, dy = pl[i].y - u->y, d = sqrtf(dx * dx + dy * dy) - pl[i].body;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    *dist = best_d;
    return best;
}

/* Encantado de fuego (o de frío): al morir explota y lastima a los que
   estén cerca, como en el original */
static void death_boom(Unit *u, UnitTarget *pl, int n)
{
    int fire = HAS_MOD(u, MOD_FIRE);
    u->boom = 0;
    missiles_effect(fire ? "fireexplode" : "iceexplode", u->x, u->y);
    for (int k = 0; k < n; k++) {
        UnitTarget *t = &pl[k];
        float dx = t->x - u->x, dy = t->y - u->y, r = 2.5f + u->radius;
        if (!t->alive || t->in_town || dx * dx + dy * dy > r * r)
            continue;
        int lo = u->dmg_max, hi = u->dmg_max * 2;
        int dmg = lo + (int)(rnd(u) % (uint32_t)(hi - lo + 1));
        dmg = fire ? dmg : dmg / 2 + 1;
        t->damage += dmg * (100 - t->res[fire ? 0 : 1]) / 100;
    }
}

/* El daño elemental de una habilidad de skills.txt en ese nivel (EMin y
   EMax, más los EMinLev/EMaxLev de los tramos 2-8, 9-16, 17-22, 23-28 y 29+,
   con su HitShift), como lo calcula el original para los monstruos */
static void skill_elem_damage(const char *skill, int lvl, int *lo, int *hi)
{
    if (!s_skilltab.rows)
        load_table(&s_skilltab, "skills");
    *lo = *hi = 0;
    int row = s_skilltab.rows ? txt_find_row(&s_skilltab, txt_col(&s_skilltab, "skill"), skill) : -1;
    if (row < 0)
        return;
    static const int k_from[5] = { 2, 9, 17, 23, 29 }, k_to[5] = { 8, 16, 22, 28, 99 };
    int mn = txt_get_int(&s_skilltab, row, "EMin", 0), mx = txt_get_int(&s_skilltab, row, "EMax", 0);
    for (int k = 0; k < 5; k++) {
        int nlev = (lvl < k_to[k] ? lvl : k_to[k]) - k_from[k] + 1;
        if (nlev <= 0)
            break;
        char col[16];
        snprintf(col, sizeof col, "EMinLev%d", k + 1);
        mn += nlev * txt_get_int(&s_skilltab, row, col, 0);
        snprintf(col, sizeof col, "EMaxLev%d", k + 1);
        mx += nlev * txt_get_int(&s_skilltab, row, col, 0);
    }
    int shift = txt_get_int(&s_skilltab, row, "HitShift", 8);
    *lo = (mn << shift) >> 8;
    *hi = (mx << shift) >> 8;
    if (*hi < *lo)
        *hi = *lo;
}

#include "monai.inc"

/* El daño elemental del ataque `anim` (El1..El3): con la resistencia del
   que recibe; el veneno, por cuadro durante el doble de su duración (como
   el original: 10 x el valor en 1/256 por cuadro); el frío, además, lo
   enfría */
static int el_hit(Unit *u, UnitTarget *t, int anim)
{
    int extra = 0;
    for (int k = 0; k < 3; k++) {
        int elem = u->el[k].elem;
        if (u->el[k].anim != anim || !elem || u->el[k].pct <= 0 ||
            (u->el[k].pct < 100 && (int)(rnd(u) % 100) >= u->el[k].pct))
            continue;
        int v = u->el[k].min + (int)(rnd(u) % (uint32_t)(u->el[k].max - u->el[k].min + 1));
        int dur = u->el[k].dur;
        if (elem == 7) { /* rand: uno al azar */
            elem = 1 + (int)(rnd(u) % 5);
            dur = dur > 0 ? dur : 25;
        }
        switch (elem) {
        case 1: extra += v * (100 - t->res[0]) / 100; break;
        case 2: { /* el frío: además enfría (la resistencia acorta cuánto) */
            extra += v * (100 - t->res[1]) / 100;
            int len = (dur > 0 ? dur : 50) * (100 - (t->res[1] > 0 ? t->res[1] : 0)) / 100;
            if (len > t->chill_ticks)
                t->chill_ticks = len;
            break;
        }
        case 3: extra += v * (100 - t->res[2]) / 100; break;
        case 4: {
            int total = 10 * v * 2 * dur / 256 * (100 - t->res[3]) / 100;
            if (total > 0) {
                t->poison_total += total;
                t->poison_ticks = t->poison_ticks > 2 * dur ? t->poison_ticks : 2 * dur;
            }
            break;
        }
        case 5: extra += v; break;
        case 6: t->mana_burn += v; break;
        default: break;
        }
    }
    return extra;
}

/* Un golpe cuerpo a cuerpo al jugador con el ataque `anim` (A1, A2 o el de
   su habilidad, S1): la probabilidad de pegar del original, el bloqueo con
   el escudo, el daño de ese ataque (a `pct` %) con su elemento y lo de los
   jefes. 1 si se mató él mismo (la Doncella de hierro). */
static int enemy_melee_hit(Unit *u, UnitTarget *t, int anim, int pct, int ar_pct)
{
    float dx = t->x - u->x, dy = t->y - u->y;
    /* Probabilidad de pegar del original: 2 * AR / (AR + defensa) * nivel / (nivel + nivel del jugador) */
    int ar = u->curse_ticks > 0 ? u->ar * (100 + u->curse_ar_pct) / 100 : u->ar; /* Provocar */
    ar = ar * (100 + ar_pct + u->ba_ar) / 100; /* (y el aura de un jefe: Puntería bendita, Fanatismo) */
    int chance = (int)(200LL * ar / (ar + t->defense > 0 ? ar + t->defense : 1) * u->level /
                       (u->level + t->level > 0 ? u->level + t->level : 1));
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    if (!t->alive || t->in_town ||
        dx * dx + dy * dy > (u->reach + 0.6f + t->body) * (u->reach + 0.6f + t->body) || (int)(rnd(u) % 100) >= chance)
        return 0;
    /* El daño de ese ataque, como el original */
    int lo = anim == ANIM_ATTACK2 && u->a2_max > 0 ? u->a2_min : anim == ANIM_SKILL && u->s1_max > 0 ? u->s1_min : u->dmg_min;
    int hi = anim == ANIM_ATTACK2 && u->a2_max > 0 ? u->a2_max : anim == ANIM_SKILL && u->s1_max > 0 ? u->s1_max : u->dmg_max;
    if (hi < lo)
        hi = lo;
    int dmg = (lo + (int)(rnd(u) % (uint32_t)(hi - lo + 1))) * pct / 100 * (100 + u->ba_dmg) / 100; /* (Poder, Fanatismo) */
    if (u->curse_ticks > 0) /* Grito de batalla */
        dmg = dmg * (100 + u->curse_dmg_pct) / 100;
    if (t->amplified) /* maldecido: el doble (Decrepitud: la mitad más) */
        dmg = dmg * (100 + t->amplified) / 100;
    int ndmg = (u->nc_ticks > 0 ? u->nc_dmg : 0) + (u->enrage_ticks > 0 ? u->enrage_dmg : 0);
    if (ndmg) /* Debilitar, Decrepitud, el Sigilo de rencor */
        dmg = dmg * (100 + ndmg) / 100;
    if (t->block > 0 && (int)(rnd(u) % 100) < t->block) { /* lo bloqueó con el escudo */
        t->blocked++;
        return 0;
    }
    t->damage += (dmg > 0 ? dmg : 0) + mod_hit(u, t, dmg) + el_hit(u, t, anim);
    t->attacker = (int)(u - s_units);
    if (u->nc_ticks > 0 && u->nc_im > 0 && dmg > 0) { /* Doncella de hierro: se lo hace a sí mismo */
        int self = (int)(u - s_units), owner = u->nc_owner;
        int exp = units_damage(self, units_resist(self, ELEM_NONE, dmg * u->nc_im / 100));
        if (exp > 0) {
            if (owner >= 0 && s_num_kills < MAX_KILLS) {
                s_kills[s_num_kills].unit = self;
                s_kills[s_num_kills].owner = owner;
                s_kills[s_num_kills].exp = exp;
                s_num_kills++;
            }
            return 1;
        }
    }
    return 0;
}

/* La secuencia de una habilidad en curso: los cuadros de monseq.txt, el
   movimiento (el salto llega al lado del jugador; la víbora avanza mientras
   repta) y lo de cada paso que pega; al final, lo que hace la habilidad
   (enterrarse, revivir a la momia) */
static void seq_attack(Unit *u, const Map *m, UnitTarget *pl)
{
    const MonSeq *q = u->seq;
    const char *sk = u->skill[u->seq_skill];
    int step = u->anim_acc >> 8;
    step = step < q->n ? step : q->n - 1;
    u->anim_acc += u->gfx->anim[q->anim[step]].speed;
    int now = u->anim_acc >> 8;
    if (strcmp(sk, "MonLeap") == 0 || strcmp(sk, "MonLeapAttack") == 0) { /* del principio al final, por el aire (Korlic pega al caer) */
        float f = (float)(now < q->n ? now : q->n) / (float)q->n;
        u->x = u->leap_x0 + (u->leap_x1 - u->leap_x0) * f;
        u->y = u->leap_y0 + (u->leap_y1 - u->leap_y0) * f;
        grid_moved(u);
    } else if (strcmp(sk, "DiabRun") == 0 && u->target >= 0) {
        /* La carga de Diablo (SrvDo103): desde el cuadro 5 corre hasta donde estaba el jugador (velocidad
           calc1 = 20), repitiendo los cuadros 6..13; al llegar sigue en el 14, y pega en el 18 */
        if (now >= 5 && now <= 13) {
            float speed = u->speed;
            u->speed = speed * 3.0f;
            if (dist_to(u, u->leap_x1, u->leap_y1) > u->reach) {
                move_towards(u, m, u->leap_x1, u->leap_y1);
                if (now == 13 && u->ai_p[2]++ < 3) /* (hasta tres vueltas) */
                    u->anim_acc = 6 << 8;
            } else {
                u->anim_acc = 14 << 8;
            }
            u->speed = speed;
            u->walking = 0;
        } else if (now > 13) {
            u->ai_p[2] = 0;
        }
    } else if (strcmp(sk, "Whirlwind") == 0) { /* Talic gira hasta pasar al jugador, pegando en el camino */
        float speed = u->speed;
        u->speed = speed * 2.0f;
        if (dist_to(u, u->leap_x1, u->leap_y1) > 0.5f)
            move_towards(u, m, u->leap_x1, u->leap_y1);
        u->speed = speed;
        u->walking = 0;
    } else if ((strcmp(sk, "SerpentCharge") == 0 || strcmp(sk, "Charge") == 0) && q->anim[step] == ANIM_WALK && u->target >= 0) {
        float speed = u->speed; /* repta (o carga) rápido hasta el jugador */
        u->speed = (u->run_speed > speed ? u->run_speed : speed) * 2.0f;
        if (dist_to(u, pl[u->target].x, pl[u->target].y) > u->reach)
            move_towards(u, m, pl[u->target].x, pl[u->target].y);
        u->speed = speed;
        u->walking = 0;
    }
    if (u->a4_hit == A4H_NEST && now >= 10) /* el nido (seq_vileres): la cría sale en el cuadro 10 */
        a4_hit_frame(u, pl);
    for (; u->hit_done <= now && u->hit_done < q->n; u->hit_done++) {
        if (!q->hit[u->hit_done] || u->target < 0)
            continue;
        if (strcmp(sk, "MagottLay") == 0) { /* el huevo, al lado */
            if (u->minion[0]) {
                float a = rndf(u) * 2.0f * PI_F, ex = u->x + cosf(a) * 1.2f, ey = u->y + sinf(a) * 1.2f;
                int saved = s_spawn_zone;
                s_spawn_zone = u->zone;
                Unit *egg = m && wall_foot(m, ex, ey, 1, COLL_MASK_MONSTER_PATH) ? NULL
                                                                                  : create_monster(u->minion, ex, ey, u->level, 1);
                s_spawn_zone = saved;
                if (egg) {
                    egg->exp = 0;
                    egg->tc[0] = '\0';
                    egg->wander = 0.0f;
                }
            }
        } else if (strcmp(sk, "MonLeap") != 0 && strcmp(sk, "Submerge") != 0) { /* (sumergirse no pega) */
            /* La estocada y el golpe de fuego pegan con su modo (el de fuego, con el daño y el elemento de S1); la carga, más fuerte */
            int mode = strcmp(sk, "Fire Hit") == 0 ? ANIM_SKILL : q->anim[u->hit_done];
            int serpent = strcmp(sk, "SerpentCharge") == 0;
            if (enemy_melee_hit(u, &pl[u->target], mode, serpent ? 150 : 100, serpent ? 50 : 0))
                return;
        }
    }
    if (now < q->n)
        return;
    if (strcmp(sk, "MagottDown") == 0 || strcmp(sk, "Submerge") == 0) { /* enterrado o sumergido: no se ve ni se le pega */
        u->hidden = 1;
        u->solid = 0;
    }
    if (strcmp(sk, "Resurrect2") == 0 && u->res_target >= 0) { /* la momia mayor levanta al caído */
        Unit *c = &s_units[u->res_target];
        if (c->enemy && c->state == ST_DEAD && !c->hidden) {
            c->hp = c->max_hp;
            c->solid = 1;
            c->searched = 0;
            c->poison_ticks = c->chill_ticks = 0;
            set_state(c, ST_IDLE);
        }
        u->res_target = -1;
    }
    u->jab = 0;
    set_state(u, ST_CHASE);
    u->cooldown = u->attack_delay;
}

/* La Congelación sagrada de Duriel (MonHolyFreeze, del nivel de su aip1):
   frena a todos los que están en su radio (ln12: 6 + nivel - 1 subtiles) lo
   que da dm34 (de 25 a 60 %), y cada 2 segundos (perdelay 50) les hace el
   frío de la habilidad (EMin/EMax de skills.txt: 2-3 más 1 por nivel) */
static void holy_freeze(Unit *u, UnitTarget *pl, int n)
{
    int lvl = u->aip[0] > 0 ? u->aip[0] : 1;
    float r = 6.0f + (float)(lvl - 1);
    int slow = 25 + (60 - 25) * (110 * lvl / (lvl + 6)) / 100;
    int pulse = (u->ai_p[2] = (u->ai_p[2] + 1) % 50) == 0;
    for (int k = 0; k < n; k++) {
        UnitTarget *t = &pl[k];
        if (!t->alive || dist_to(u, t->x, t->y) > r)
            continue;
        if (slow > t->aura_slow)
            t->aura_slow = slow;
        if (pulse) {
            int v = 2 + (lvl - 1) + (int)(rnd(u) % 2u);
            t->damage += v * (100 - t->res[1]) / 100;
        }
    }
}

/* El aura de un jefe encantado, como la del Paladín (radio y valores de skills.txt a su nivel,
   sin sinergias): las de ataque a los jugadores que alcanza (la Congelación y la Convicción
   en cada tick; el fuego y el rayo en cada pulso, cada perdelay cuadros) y las de apoyo a los
   monstruos de su lado (en cada pulso, hasta el siguiente). Él lleva el daño entero de
   Fanatismo (passivestat); los demás, el del aura. */
static void boss_aura(Unit *u, UnitTarget *pl, int n)
{
    const SkillDef *s = u->aura_sk;
    SkillCtx x = { 0, NULL, u->level }; /* (sin sinergias) */
    int lvl = u->aura_lvl, period = s->perdelay > 5 ? s->perdelay : 5;
    int pulse = (u->aura_t = (u->aura_t + 1) % period) == 0;
    float r = (float)skill_calc(s, SC_AURARANGE, lvl, &x);
    switch (s->kind) {
    case SK_HOLY_FIRE:
    case SK_HOLY_SHOCK:
    case SK_HOLY_FREEZE: {
        int ri = s->kind == SK_HOLY_FIRE ? 0 : s->kind == SK_HOLY_FREEZE ? 1 : 2, lo, hi;
        skill_elem_syn(s, lvl, &x, &lo, &hi);
        for (int k = 0; k < n; k++) {
            UnitTarget *t = &pl[k];
            if (!t->alive || t->in_town || dist_to(u, t->x, t->y) > r)
                continue;
            if (s->kind == SK_HOLY_FREEZE && -skill_calc(s, SC_AURASTAT1, lvl, &x) > t->aura_slow)
                t->aura_slow = -skill_calc(s, SC_AURASTAT1, lvl, &x);
            if (pulse) {
                int v = lo + (int)(rnd(u) % (uint32_t)(hi - lo + 1 > 0 ? hi - lo + 1 : 1));
                t->damage += v * (100 - t->res[ri]) / 100;
            }
        }
        break;
    }
    case SK_CONVICTION:
        for (int k = 0; k < n; k++) {
            UnitTarget *t = &pl[k];
            if (!t->alive || t->in_town || dist_to(u, t->x, t->y) > r)
                continue;
            int def = skill_calc(s, SC_AURASTAT1, lvl, &x), res = skill_calc(s, SC_AURASTAT2, lvl, &x);
            t->aura_conv_def = def < t->aura_conv_def ? def : t->aura_conv_def;
            t->aura_conv_res = res < t->aura_conv_res ? res : t->aura_conv_res;
        }
        break;
    case SK_MIGHT:
    case SK_BLESSED_AIM:
    case SK_FANATICISM:
        if (!pulse)
            break;
        for (int i = 0; i < s_num_units; i++) {
            Unit *o = &s_units[i];
            if (!o->enemy || o->state >= ST_DYING || o->hidden || dist_to(u, o->x, o->y) > r)
                continue;
            o->ba_ticks = period + 1;
            o->ba_dmg = o->ba_ar = o->ba_ias = 0;
            for (int k = 0; k < 3; k++) {
                const char *st = s->aurastat[k];
                int v = skill_calc(s, SC_AURASTAT1 + k, lvl, &x);
                if (!strcmp(st, "damagepercent"))
                    o->ba_dmg = o == u && skill_has(s, SC_PASSIVE1) ? skill_calc(s, SC_PASSIVE1, lvl, &x) : v;
                else if (!strcmp(st, "item_tohit_percent"))
                    o->ba_ar = v;
                else if (!strcmp(st, "attackrate"))
                    o->ba_ias = v;
            }
        }
        break;
    default:
        break;
    }
}

static void enemy_tick(Unit *u, const Map *m, UnitTarget *pl, int n)
{
    const CharAnim *a = &u->gfx->anim[current_anim(u)];
    int frames = charanim_frames(a);
    if (u->ai == AI_DURIEL && u->state < ST_DYING)
        holy_freeze(u, pl, n);
    if (u->aura_sk && u->state < ST_DYING)
        boss_aura(u, pl, n);
    if (u->ba_ticks > 0 && --u->ba_ticks == 0)
        u->ba_dmg = u->ba_ar = u->ba_ias = 0;
    if (u->ai == AI_SUMMONER) { /* las demoras de su Nova y su muro */
        if (u->ai_p[0] > 0)
            u->ai_p[0]--;
        if (u->ai_p[1] > 0)
            u->ai_p[1]--;
    }

    switch (u->state) {
    case ST_DEAD:
        if (u->ai == AI_REANIMATED) /* la horda reanimada se levanta */
            a5_reanimate(u);
        return;
    case ST_DYING:
        if (u->boom) /* encantado de fuego o frío: explota al morir */
            death_boom(u, pl, n);
        if (u->pregnant) /* preñado: sale el gusano (monai5.inc) */
            a5_birth(u);
        if (u->ai == AI_SUICIDEMINION) /* el esbirro suicida explota al morir (monai5.inc) */
            a5_suicide_boom(u, pl, n);
        u->anim_acc += a->speed;
        if ((u->anim_acc >> 8) >= frames)
            set_state(u, ST_DEAD);
        return;
    case ST_HIT:
        if (u->ai == AI_SUICIDEMINION) { /* (MONSTERUNIQUE_SuicideModeChange: si se resiente, muere) */
            units_play_death((int)(u - s_units));
            return;
        }
        /* fall through */
    case ST_RISE:
        u->anim_acc += a->speed;
        if ((u->anim_acc >> 8) >= frames)
            set_state(u, ST_CHASE);
        return;
    case ST_ATTACK: {
        if (u->jab && u->seq) { /* la secuencia de su habilidad */
            seq_attack(u, m, pl);
            return;
        }
        u->anim_acc += a->speed * (100 + u->ba_ias) / 100; /* (el Fanatismo de un jefe) */
        int frame = u->anim_acc >> 8;
        int hit_frame = a->hit_frame > 0 ? a->hit_frame : frames / 2;
        if (u->inferno_end) { /* el aliento sostenido: quieto en su cuadro mientras dura */
            if ((int)s_unit_ticks < u->inferno_end && frame >= u->inferno_frame) {
                u->anim_acc = u->inferno_frame << 8;
                a4_inferno_tick(u, pl);
                return;
            }
            if ((int)s_unit_ticks >= u->inferno_end)
                u->inferno_end = 0;
        }
        if (!u->hit_done && frame >= hit_frame && u->a4_hit) { /* los hechizos del Acto 4 (monai4.inc) */
            u->hit_done = 1;
            a4_hit_frame(u, pl);
        } else if (!u->hit_done && frame >= hit_frame && u->ai == AI_MAGGOTEGG) {
            /* El huevo se abre: sale la larva (y el huevo se termina, sin experiencia ni botín) */
            u->hit_done = 1;
            int saved = s_spawn_zone;
            s_spawn_zone = u->zone;
            Unit *larva = u->minion[0] ? create_monster(u->minion, u->x, u->y, u->level, 1) : NULL;
            s_spawn_zone = saved;
            if (larva)
                larva->sector = (int)(rnd(larva) % 16);
        } else if (!u->hit_done && frame >= hit_frame && u->ai == AI_MAGGOTQUEEN) {
            /* La reina pare un gusano a su lado (MONSTERS_GetMinionSpawnInfo: 8
               subtiles al este; si ahí hay pared, alrededor), sin experiencia */
            u->hit_done = 1;
            float mx = u->x + 8.0f, my = u->y;
            for (int k = 0; k < 12 && s_map && wall_foot(s_map, mx, my, 2, COLL_MASK_MONSTER_PATH); k++) {
                float a2 = rndf(u) * 2.0f * PI_F, r = 3.0f + rndf(u) * 4.0f;
                mx = u->x + cosf(a2) * r;
                my = u->y + sinf(a2) * r;
            }
            int saved = s_spawn_zone;
            s_spawn_zone = u->zone;
            Unit *mg = u->minion[0] ? create_monster(u->minion, mx, my, u->level, 1) : NULL;
            s_spawn_zone = saved;
            if (mg) {
                mg->exp = 0;
                mg->sector = (int)(rnd(mg) % 16);
                set_state(mg, gfx_has(mg->gfx, ANIM_SKILL) ? ST_RISE : ST_CHASE);
                u->ai_p[0]++;
            }
        } else if (!u->hit_done && frame >= hit_frame && u->attack_anim == ANIM_SKILL && u->raiser) {
            /* Blood Raven: salen zombis del suelo a su alrededor */
            u->hit_done = 1;
            for (int k = 0; k < 2; k++) {
                float a2 = rndf(u) * 2.0f * PI_F, r = 1.5f + rndf(u) * 2.0f;
                float zx = u->x + cosf(a2) * r, zy = u->y + sinf(a2) * r;
                if (s_map && wall_foot(s_map, zx, zy, 2, COLL_MASK_MONSTER_PATH))
                    continue;
                int saved = s_spawn_zone;
                s_spawn_zone = u->zone;
                Unit *z = create_monster(u->minion, zx, zy, u->minion_level > 0 ? u->minion_level : u->level, 1);
                s_spawn_zone = saved;
                if (z) {
                    z->master = (int)(u - s_units);
                    z->wander = 0.0f;
                    set_state(z, gfx_has(z->gfx, ANIM_SKILL) ? ST_RISE : ST_CHASE);
                    z->sector = (int)(rnd(z) % 16);
                }
            }
        } else if (!u->hit_done && frame >= hit_frame && u->heal_target >= 0) {
            /* El sacerdote cura a un aliado (ZakarumHeal) */
            u->hit_done = 1;
            Unit *c = &s_units[u->heal_target];
            if (c->enemy && c->state < ST_DYING) {
                c->hp += c->max_hp * u->heal_pct / 100;
                c->hp = c->hp < c->max_hp ? c->hp : c->max_hp;
                overlays_add("heal", OV_AT_UNIT, u->heal_target, c->x, c->y, 0);
            }
            u->heal_target = -1;
        } else if (!u->hit_done && frame >= hit_frame && u->res_target >= 0) {
            /* El chamán revive al Caído */
            u->hit_done = 1;
            Unit *c = &s_units[u->res_target];
            if (c->enemy && c->state == ST_DEAD) {
                c->hp = c->max_hp;
                c->solid = 1;
                c->searched = 0;
                c->poison_ticks = c->chill_ticks = 0;
                set_state(c, ST_IDLE);
                if (c->snd)
                    unit_sound(c, c->snd->skill[0], 0, -1); /* se levanta */
            }
            u->res_target = -1;
        } else if (!u->hit_done && frame >= hit_frame && u->ranged_attack) {
            u->hit_done = 1;
            UnitTarget *t = &pl[u->target];
            const char *name = u->missile2[0] && missiles_exists(u->missile2) && rnd(u) % 3 == 0 ? u->missile2 : u->missile;
            int shots = u->ring > 0 ? u->ring : u->spray > 1 ? u->spray : 1;
            static int s_mon_group = -1; /* las novas de los monstruos: a cada jugador le pega uno solo (game.c) */
            int group = (u->spell_flags & MF_GROUP) ? s_mon_group-- : 0;
            for (int s = 0; s < shots; s++) {
            float a = u->ring > 0 ? (float)s * 2.0f * PI_F / (float)shots /* la Nova: alrededor */
                    : atan2f(t->y - u->y, t->x - u->x) + (shots > 1 ? (s - (shots - 1) / 2.0f) * 0.17f : 0.0f);
            Missile *mi = missiles_spawn(name, u->x, u->y, cosf(a), sinf(a));
            if (mi) {
                int elem, emin, emax, dmin, dmax;
                missiles_base_damage(name, &elem, &emin, &emax, &dmin, &dmax);
                if (u->slowmis_ticks > 0) { /* Ralentizar proyectiles */
                    mi->vx = mi->vx * (float)u->slowmis_pct / 100.0f;
                    mi->vy = mi->vy * (float)u->slowmis_pct / 100.0f;
                    mi->life = mi->life * 100 / (u->slowmis_pct > 10 ? u->slowmis_pct : 10);
                }
                mi->owner = -1;
                mi->source = (int)(u - s_units);
                mi->ar = u->ar;
                mi->level = u->level;
                /* Las púas y las flechas suman el daño del ataque del monstruo; las
                   bolas de fuego del chamán, solo el suyo */
                int shaman = strncmp(u->missile, "shafire", 7) == 0;
                mi->dmin = dmin + (u->ranged_anim == ANIM_ATTACK2 ? u->a2_min : shaman ? 0 : u->dmg_min);
                mi->dmax = dmax + (u->ranged_anim == ANIM_ATTACK2 ? u->a2_max : shaman ? 0 : u->dmg_max);
                mi->elem = elem;
                mi->emin = emin;
                mi->emax = emax;
                mi->flags |= u->spell_flags;
                mi->group = group;
                if (u->spell_elem || u->spell_dmax) { /* un hechizo: el daño de la habilidad en su nivel */
                    mi->dmin = u->spell_dmin;
                    mi->dmax = u->spell_dmax;
                    mi->elem = u->spell_elem;
                    mi->emin = u->spell_min;
                    mi->emax = u->spell_max;
                } else if (mi->elem == ELEM_NONE && strcmp(name, "firearrow") == 0) {
                    /* Las flechas de fuego de Blood Raven (el proyectil no trae
                       daño: en la Amazona lo pone la habilidad) */
                    mi->elem = ELEM_FIRE;
                    mi->emin = u->level / 2 + 1;
                    mi->emax = u->level * 3 / 2 + 2;
                } else if (mi->elem == ELEM_NONE && (HAS_MOD(u, MOD_FIRE) || HAS_MOD(u, MOD_COLD))) {
                    mi->elem = HAS_MOD(u, MOD_FIRE) ? ELEM_FIRE : ELEM_COLD;
                    mi->emin = mi->dmin / 2 + 1;
                    mi->emax = mi->dmax / 2 + 1;
                }
                /* El veneno de missiles.txt es por cuadro (en 1/256 << HitShift):
                   el total en sus ELen cuadros, como el original (antes pegaba
                   todo de una); el frío dura su ELen */
                int elen = missiles_elen(name);
                if (mi->elem == ELEM_POIS && elen > 0) {
                    missiles_poison_total(name, &mi->emin, &mi->emax);
                    mi->elen = elen;
                } else if (mi->elem == ELEM_COLD && elen > 0 && mi->elen <= 0) {
                    mi->elen = elen;
                }
                /* Las pociones de las felinas van en arco y caen donde estaba el blanco */
                if (missiles_is_lob(mi))
                    missiles_lob_to(mi, t->x, t->y);
                if (HAS_MOD(u, MOD_STRONG) && pl[u->target].amplified) {
                    mi->dmin *= 2;
                    mi->dmax *= 2;
                }
                if (u->curse_ticks > 0) { /* Grito de batalla, Provocar */
                    mi->dmin = mi->dmin * (100 + u->curse_dmg_pct) / 100;
                    mi->dmax = mi->dmax * (100 + u->curse_dmg_pct) / 100;
                }
                if (u->ba_ticks > 0) { /* el aura de un jefe: Poder y Fanatismo (daño), Puntería bendita */
                    mi->dmin = mi->dmin * (100 + u->ba_dmg) / 100;
                    mi->dmax = mi->dmax * (100 + u->ba_dmg) / 100;
                    mi->ar = mi->ar * (100 + u->ba_ar) / 100;
                }
                int ndmg = (u->nc_ticks > 0 ? u->nc_dmg : 0) + (u->enrage_ticks > 0 ? u->enrage_dmg : 0);
                if (ndmg) { /* Debilitar, Decrepitud, el Sigilo de rencor */
                    mi->dmin = mi->dmin * (100 + ndmg) / 100;
                    mi->dmax = mi->dmax * (100 + ndmg) / 100;
                    mi->emin = mi->emin * (100 + ndmg) / 100;
                    mi->emax = mi->emax * (100 + ndmg) / 100;
                }
            }
            }
        } else if (!u->hit_done && frame >= hit_frame && u->mm_attack) {
            /* Confundido o atraído: le pega a otro monstruo (la experiencia es del que lo maldijo) */
            u->hit_done = 1;
            Unit *e = u->mm_target >= 0 && u->mm_target < s_num_units ? &s_units[u->mm_target] : NULL;
            if (e && e->enemy && e->state < ST_DYING && dist_to(u, e->x, e->y) <= u->reach + e->radius + 0.3f) {
                int lo = u->dmg_min, hi = u->dmg_max > u->dmg_min ? u->dmg_max : u->dmg_min;
                int dmg = lo + (int)(rnd(u) % (uint32_t)(hi - lo + 1));
                int ndmg = (u->nc_ticks > 0 ? u->nc_dmg : 0) + (u->enrage_ticks > 0 ? u->enrage_dmg : 0);
                if (ndmg)
                    dmg = dmg * (100 + ndmg) / 100;
                int victim = u->mm_target, exp = units_damage(victim, units_resist(victim, ELEM_NONE, dmg));
                if (exp > 0 && u->nc_owner >= 0 && s_num_kills < MAX_KILLS) {
                    s_kills[s_num_kills].unit = victim;
                    s_kills[s_num_kills].owner = u->nc_owner;
                    s_kills[s_num_kills].exp = exp;
                    s_num_kills++;
                }
            }
        } else if (!u->hit_done && frame >= hit_frame) {
            u->hit_done = 1;
            if (enemy_melee_hit(u, &pl[u->target], u->attack_anim, 100, 0))
                return;
        }
        if (frame >= frames) {
            if (u->ai == AI_MAGGOTEGG && u->ai_p[0] == 1) { /* el huevo abierto se termina */
                u->hp = 0;
                u->solid = 0;
                set_state(u, ST_DYING);
                return;
            }
            set_state(u, ST_CHASE);
            u->cooldown = u->attack_delay;
        }
        return;
    }
    default:
        break;
    }

    if (u->cooldown > 0)
        u->cooldown--;
    if (u->stun_ticks > 0) { /* aturdido: quieto */
        u->stun_ticks--;
        u->walking = 0;
        u->anim_acc += u->gfx->anim[current_anim(u)].speed;
        return;
    }
    float d;
    if (u->flee_ticks > 0) { /* asustado: se aleja del jugador más cercano */
        u->flee_ticks--;
        int f = nearest_player(u, pl, n, CHASE_RANGE * 2.0f, &d);
        if (f >= 0) {
            float dx = u->x - pl[f].x, dy = u->y - pl[f].y, l = d > 0.01f ? d : 1.0f;
            move_towards(u, m, u->x + dx / l * 3.0f, u->y + dy / l * 3.0f);
        } else {
            u->walking = 0;
        }
        u->anim_acc += u->gfx->anim[current_anim(u)].speed;
        return;
    }
    /* Confundido (a cualquier otro) o atraído (al que tiene Atraer): ataca monstruos */
    if (u->mm_ticks > 0) {
        u->mm_ticks--;
        int self = (int)(u - s_units), e = u->mm_target;
        if (u->nc_kind == NC_CONFUSE && (e < 0 || !units_alive_enemy(e) || e == self || rnd(u) % 75 == 0)) {
            float bd = 10.0f;
            e = -1;
            for (int i = 0; i < s_num_units; i++) {
                float dd;
                if (i == self || !units_alive_enemy(i) || (dd = dist_to(u, s_units[i].x, s_units[i].y)) >= bd)
                    continue;
                bd = dd;
                e = i;
            }
            u->mm_target = e;
        }
        if (e >= 0 && e != self && units_alive_enemy(e) && u->gfx->anim[ANIM_ATTACK].ok) {
            const Unit *o = &s_units[e];
            float de = dist_to(u, o->x, o->y);
            u->state = ST_CHASE;
            u->sector = sector_from_world(o->x - u->x, o->y - u->y);
            if (de <= u->reach + o->radius) {
                u->walking = 0;
                if (u->cooldown == 0) {
                    u->attack_anim = ANIM_ATTACK;
                    u->ranged_attack = 0;
                    u->res_target = -1;
                    u->mm_attack = 1;
                    set_state(u, ST_ATTACK);
                    u->hit_done = 0;
                    return;
                }
            } else {
                move_towards(u, m, o->x, o->y);
            }
            u->anim_acc += u->gfx->anim[current_anim(u)].speed;
            return;
        }
    }
    u->mm_attack = 0;
    int t;
    if ((u->nc_ticks > 0 && u->nc_kind == NC_DIM) || (u->cloak_ticks > 0 && u->cloak_blind))
        t = nearest_player(u, pl, n, 1.5f, &d); /* Visión reducida y Manto de sombras: solo ve al que tiene al lado */
    else if ((t = nearest_player_seen(u, pl, n, u->state == ST_CHASE ? CHASE_SCREEN : 1.0f, &d)) < 0 &&
             u->state == ST_CHASE)
        t = nearest_player(u, pl, n, CHASE_RANGE, &d);
    if (t < 0) {
        u->state = ST_IDLE;
        if (u->ai && ai_tick(u, m, pl, -1, 1e9f)) /* el gusano se entierra, el huevo espera */
            return;
        wander_tick(u, m, pl, n);
        u->anim_acc += a->speed;
        return;
    }
    u->state = ST_CHASE;
    u->target = t;
    /* Los jefes hablan al verte (Andariel, Blood Raven, la Condesa, el
       Herrero) y cada tanto durante la pelea, como en el original */
    if (u->snd && (u->snd->init >= 0 || u->snd->taunt[0] >= 0)) {
        if (!u->seen) {
            u->seen = 1;
            unit_sound(u, u->snd->init >= 0 ? u->snd->init : u->snd->taunt[0], 0, -1);
            u->taunt_cd = (20 + (int)(rnd(u) % 20)) * TICK_RATE;
        } else if (--u->taunt_cd <= 0) {
            int k = u->snd->taunt[1] >= 0 && rnd(u) % 2 ? 1 : 0;
            unit_sound(u, u->snd->taunt[k] >= 0 ? u->snd->taunt[k] : u->snd->taunt[0], 0, -1);
            u->taunt_cd = (25 + (int)(rnd(u) % 25)) * TICK_RATE;
        }
    }
    if (u->ai && u->target >= 0 && ai_tick(u, m, pl, t, d)) /* la IA del original (monai.inc) */
        return;
    /* Blood Raven: cada tanto levanta zombis (hasta MAX_MINIONS vivos) */
    if (u->raiser && --u->raise_cd <= 0) {
        int alive = 0, self = (int)(u - s_units);
        for (int i = 0; i < s_num_units; i++)
            alive += s_units[i].master == self && s_units[i].state < ST_DYING;
        u->raise_cd = RAISE_TICKS / 4;
        if (alive < MAX_MINIONS && u->minion[0]) {
            u->walking = 0;
            u->attack_anim = ANIM_SKILL;
            u->ranged_attack = 0;
            u->res_target = -1;
            set_state(u, ST_ATTACK);
            u->hit_done = 0;
            u->raise_cd = RAISE_TICKS;
            return;
        }
    }
    /* Chamán: si hay un Caído muerto cerca, lo revive */
    if (u->resurrect && u->cooldown == 0) {
        for (int i = 0; i < s_num_units; i++) {
            Unit *c = &s_units[i];
            if (c->enemy && c->state == ST_DEAD && !c->unique && strncmp(c->name, "fallen", 6) == 0 &&
                strncmp(c->name, "fallenshaman", 12) != 0 && in_radius_xy(c, u->x, u->y, 7.0f)) {
                u->walking = 0;
                u->sector = sector_from_world(c->x - u->x, c->y - u->y);
                u->res_target = i;
                u->attack_anim = ANIM_ATTACK2;
                u->ranged_attack = 0;
                set_state(u, ST_ATTACK);
                u->hit_done = 0;
                return;
            }
        }
    }
    /* Los que disparan lo hacen desde lejos; las ratas pegan si están al lado */
    if (u->ranged && d <= RANGED_MAX && (d > u->reach + 0.6f || !u->gfx->anim[ANIM_ATTACK].ok ||
                                         u->ranged_anim == ANIM_ATTACK)) {
        u->walking = 0;
        u->sector = sector_from_world(pl[t].x - u->x, pl[t].y - u->y);
        if (u->cooldown == 0) {
            u->attack_anim = u->ranged_anim;
            u->ranged_attack = 1;
            u->res_target = -1;
            set_state(u, ST_ATTACK);
            u->hit_done = 0;
            return;
        }
        u->anim_acc += u->gfx->anim[current_anim(u)].speed;
        return;
    }
    if (d <= u->reach) {
        u->walking = 0;
        u->has_slot = 0;
        u->sector = sector_from_world(pl[t].x - u->x, pl[t].y - u->y);
        if (u->cooldown == 0 && u->gfx->anim[ANIM_ATTACK].ok) {
            u->mm_attack = 0;
            /* A veces el segundo ataque, si tiene (el zombi, el Caído...) */
            int a2 = !u->ranged && u->a2_max > 0 && u->gfx->anim[ANIM_ATTACK2].ok && rnd(u) % 3 == 0;
            u->attack_anim = a2 ? ANIM_ATTACK2 : ANIM_ATTACK;
            u->ranged_attack = 0;
            u->res_target = -1;
            set_state(u, ST_ATTACK);
            u->hit_done = 0;
            return;
        }
    } else {
        /* Cerca del jugador va a un lugar libre a su alrededor (para
           rodearlo en vez de hacer fila detrás de otro) */
        float sx, sy;
        if (d < SURROUND_NEAR && surround_spot(u, m, &pl[t], &sx, &sy))
            move_towards(u, m, sx, sy);
        else
            move_towards(u, m, pl[t].x, pl[t].y);
        if (u->x == u->prev_x && u->y == u->prev_y)
            u->has_slot = 0; /* trabado: el próximo tick busca otro lugar */
    }
    u->anim_acc += u->gfx->anim[current_anim(u)].speed;
}

/* Daño que va de a poco (veneno, heridas abiertas); si lo mata, la
   experiencia es de `owner` */
static void dot_tick(Unit *u, int *ticks, float rate, float *acc, int owner)
{
    if (*ticks <= 0 || u->state >= ST_DYING)
        return;
    (*ticks)--;
    *acc += rate;
    int dmg = (int)*acc;
    if (dmg <= 0)
        return;
    *acc -= (float)dmg;
    u->hp -= dmg;
    if (u->hp <= 0) {
        u->hp = 0;
        set_state(u, ST_DYING);
        u->solid = 0;
        if (s_num_kills < MAX_KILLS) {
            s_kills[s_num_kills].unit = (int)(u - s_units);
            s_kills[s_num_kills].owner = owner;
            s_kills[s_num_kills].exp = u->exp > 0 ? u->exp : 1;
            s_num_kills++;
        }
    }
}

static void poison_tick(Unit *u)
{
    dot_tick(u, &u->poison_ticks, u->poison_rate, &u->poison_acc, u->poison_owner);
    dot_tick(u, &u->bleed_ticks, u->bleed_rate, &u->bleed_acc, u->bleed_owner);
}

/* ---- Mercenarias ---- */

static float dist_to(const Unit *u, float x, float y)
{
    float dx = x - u->x, dy = y - u->y;
    return sqrtf(dx * dx + dy * dy);
}

/* ¿Le acierta un aliado? La puntería del original contra la defensa del
   monstruo (y las auras), con `ar_pct` de más (la Estocada) */
static int ally_hits(Unit *u, const Unit *t, int ar_pct)
{
    int ar = u->ms.ar * (100 + u->au.ar_pct + ar_pct) / 100, ac = t->ac * (100 + t->curse_def_pct) / 100 - t->curse_def_flat;
    int chance = (int)(200LL * ar / (ar + (ac > 0 ? ac : 0) > 0 ? ar + (ac > 0 ? ac : 0) : 1) *
                       u->ms.level / (u->ms.level + t->level));
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    return (int)(rnd(u) % 100) < chance;
}

/* Un golpe de lanza del guerrero del desierto (si el monstruo sigue a su
   alcance). D2_MERC_DEBUG: cada golpe */
static void merc_strike(Unit *u, int dmg_pct, int ar_pct)
{
    static int debug = -1;
    if (debug < 0)
        debug = getenv("D2_MERC_DEBUG") != NULL;
    Unit *t = u->target >= 0 && u->target < s_num_units ? &s_units[u->target] : NULL;
    if (!t || !t->enemy || t->state >= ST_DYING || dist_to(u, t->x, t->y) > u->reach + t->radius + 0.5f)
        return;
    if (!ally_hits(u, t, ar_pct)) {
        if (debug)
            printf("  mercenario de J%d: %s a %s, falló\n", u->owner + 1, u->jab ? "estocada" : "golpe", t->name);
        return;
    }
    int dmg = u->ms.dmin + (int)(rnd(u) % (uint32_t)(u->ms.dmax - u->ms.dmin + 1));
    dmg = dmg * (100 + u->au.dmg_pct + dmg_pct) / 100;
    int target = (int)(t - s_units);
    int dealt = units_resist(target, ELEM_NONE, dmg > 1 ? dmg : 1);
    int lifetap = units_lifetap(target);
    int exp = units_damage(target, dealt);
    if (debug)
        printf("  mercenario de J%d: %s a %s, %d de daño%s\n", u->owner + 1, u->jab ? "estocada" : "golpe", t->name, dealt,
               exp > 0 ? " (lo mató)" : "");
    if (exp > 0 && s_num_kills < MAX_KILLS) { /* la experiencia es del dueño */
        s_kills[s_num_kills].unit = target;
        s_kills[s_num_kills].owner = u->owner;
        s_kills[s_num_kills].exp = exp;
        s_num_kills++;
    }
    if (lifetap > 0)
        u->hp = u->hp + dealt * lifetap / 100 > u->max_hp ? u->max_hp : u->hp + dealt * lifetap / 100;
}

/* Sigue a su dueño y le dispara al monstruo más cercano sin alejarse de él;
   a veces usa la flecha de su habilidad o Percepción. El de lanza va hasta
   el monstruo y a veces usa la Estocada (dos golpes, monseq.txt) */
static void merc_tick(Unit *u, const Map *m, UnitTarget *pl, int n)
{
    if (u->hidden || u->owner < 0 || u->owner >= n)
        return;
    const UnitTarget *o = &pl[u->owner];
    const CharAnim *a = &u->gfx->anim[current_anim(u)];
    int frames = charanim_frames(a), self = (int)(u - s_units);
    switch (u->state) {
    case ST_DEAD:
        return;
    case ST_DYING:
        u->anim_acc += a->speed;
        if ((u->anim_acc >> 8) >= frames)
            set_state(u, ST_DEAD);
        return;
    case ST_HIT:
        u->anim_acc += a->speed;
        if ((u->anim_acc >> 8) >= frames)
            set_state(u, ST_IDLE);
        return;
    case ST_ATTACK: {
        u->anim_acc += a->speed * (100 + u->au.ias) / 100; /* Fanatismo */
        int frame = u->anim_acc >> 8, hit_frame = a->hit_frame > 0 ? a->hit_frame : frames / 2;
        if (u->ms.melee) { /* hit_done: los pasos ya jugados (la Estocada pega en dos) */
            int jab = u->jab && u->seq, len = jab ? u->seq->n : frames;
            for (; u->hit_done <= frame && u->hit_done < len; u->hit_done++)
                if (jab ? u->seq->hit[u->hit_done] : u->hit_done == hit_frame)
                    merc_strike(u, jab ? u->ms.jab_dmg_pct : 0, jab ? u->ms.jab_ar_pct : 0);
            if (frame >= len) {
                set_state(u, ST_IDLE);
                u->cooldown = 2;
            }
            return;
        }
        if (!u->hit_done && frame >= hit_frame) {
            u->hit_done = 1;
            const Unit *t = u->target >= 0 && u->target < s_num_units ? &s_units[u->target] : NULL;
            if (t && t->enemy && t->state < ST_DYING) {
                if (u->ms.caster) { /* sus hechizos: todo elemental y no fallan, como los de la hechicera */
                    int k = u->merc_spell, n = u->ms.spell_count[k] > 0 ? u->ms.spell_count[k] : 1;
                    float base = atan2f(t->y - u->y, t->x - u->x);
                    for (int b = 0; b < n; b++) {
                        float ang = n > 1 ? base + ((float)(rnd(u) % 1000) / 1000.0f - 0.5f) * 1.6f : base;
                        Missile *mi = missiles_spawn(u->ms.spell_missile[k], u->x, u->y, cosf(ang), sinf(ang));
                        if (!mi)
                            break;
                        mi->owner = u->owner;
                        mi->source = self;
                        mi->flags |= MF_MERC | MF_ALWAYS_HIT | u->ms.spell_flags[k];
                        mi->level = u->ms.level;
                        mi->elem = u->ms.spell_elem[k];
                        mi->emin = u->ms.spell_min[k] * (100 + u->au.dmg_pct) / 100;
                        mi->emax = u->ms.spell_max[k] * (100 + u->au.dmg_pct) / 100;
                        mi->elen = u->ms.spell_len[k];
                        mi->radius10 = u->ms.spell_radius10[k];
                    }
                } else if (u->attack_anim == ANIM_SKILL) { /* Percepción */
                    UnitCurse c = { u->ms.sight_def, 0, 0, 0, u->ms.sight_ticks, 0 };
                    units_curse(t->x, t->y, 6.0f, &c);
                } else {
                    int skill = u->ms.skill_missile[0] && (int)(rnd(u) % 100) < u->ms.skill_chance;
                    Missile *mi = missiles_spawn(skill ? u->ms.skill_missile : "arrow", u->x, u->y, t->x - u->x,
                                                 t->y - u->y);
                    if (mi) {
                        mi->owner = u->owner;
                        mi->source = self;
                        mi->flags |= MF_MERC;
                        mi->ar = u->ms.ar * (100 + u->au.ar_pct) / 100; /* las auras: Puntería bendecida, Fanatismo */
                        mi->level = u->ms.level;
                        mi->dmin = u->ms.dmin * (100 + u->au.dmg_pct) / 100; /* Poder, Concentración, Fanatismo */
                        mi->dmax = u->ms.dmax * (100 + u->au.dmg_pct) / 100;
                        if (skill) {
                            mi->elem = u->ms.elem;
                            mi->emin = u->ms.emin;
                            mi->emax = u->ms.emax;
                            mi->elen = u->ms.elen;
                        }
                    }
                }
            }
        }
        if (frame >= frames) {
            set_state(u, ST_IDLE);
            u->cooldown = MERC_COOLDOWN;
        }
        return;
    }
    default:
        break;
    }

    /* Se cura sola de a poco (en el pueblo, rápido) */
    if (u->hp < u->max_hp && ++u->regen_acc >= (o->in_town ? 2 : 50)) {
        u->regen_acc = 0;
        u->hp += o->in_town ? 1 + u->max_hp / 60 : 1;
        if (u->hp > u->max_hp)
            u->hp = u->max_hp;
    }
    if (u->cooldown > 0)
        u->cooldown--;
    float d_owner = dist_to(u, o->x, o->y);
    /* La velocidad del dueño (subtiles por segundo, suavizada) */
    float ov = sqrtf((o->x - u->own_px) * (o->x - u->own_px) + (o->y - u->own_py) * (o->y - u->own_py)) * TICK_RATE;
    if (ov > 30.0f)
        ov = 0.0f; /* saltó (waypoint, escalera) */
    u->own_v = u->own_v * 0.85f + ov * 0.15f;
    u->own_px = o->x;
    u->own_py = o->y;
    if (d_owner > MERC_WARP) { /* el dueño viajó: aparece a su lado */
        units_merc_warp(self, o->x, o->y);
        return;
    }

    /* Pelear: el monstruo más cercano, sin irse lejos del dueño */
    if (!o->in_town && o->alive) {
        int best = -1;
        float best_d = MERC_RANGE;
        for (int i = 0; i < s_num_units; i++) {
            const Unit *e = &s_units[i];
            if (!e->enemy || e->state >= ST_DYING || e->hidden)
                continue;
            float d = dist_to(u, e->x, e->y);
            if (d < best_d && dist_to(e, o->x, o->y) < MERC_LEASH) {
                best_d = d;
                best = i;
            }
        }
        if (best >= 0) {
            const Unit *e = &s_units[best];
            if (u->ms.melee && best_d > u->reach + e->radius) { /* el de lanza va hasta él */
                u->target = best;
                u->walking = 1;
                u->running = 1;
                move_towards(u, m, e->x, e->y);
                u->anim_acc += u->gfx->anim[current_anim(u)].speed;
                return;
            }
            u->walking = 0;
            u->running = 0;
            u->sector = sector_from_world(e->x - u->x, e->y - u->y);
            if (u->cooldown == 0) {
                u->target = best;
                u->attack_anim = u->ms.sight_def > 0 && rnd(u) % 10 == 0 && !(e->curse_ticks > 0) &&
                                         u->gfx->anim[ANIM_SKILL].ok
                                     ? ANIM_SKILL
                                     : ANIM_ATTACK;
                if (u->ms.caster) { /* el Lobo de Hierro: el segundo hechizo con su probabilidad; si no, el primero */
                    u->merc_spell = u->ms.spell_missile[1][0] &&
                                    (!u->ms.spell_missile[0][0] || (int)(rnd(u) % 100) < u->ms.spell_chance[1]);
                    u->attack_anim = u->gfx->anim[ANIM_CAST].ok ? ANIM_CAST : ANIM_ATTACK;
                }
                u->jab = u->ms.melee && u->seq && (int)(rnd(u) % 100) < u->ms.jab_chance;
                u->res_target = -1;
                set_state(u, ST_ATTACK);
                u->hit_done = 0;
                return;
            }
            u->anim_acc += u->gfx->anim[current_anim(u)].speed;
            return;
        }
    }

    /* Seguir al dueño: caminando, o corriendo si se quedó atrás o si él va
       más rápido de lo que ella camina. Con márgenes (empieza a seguirlo a
       una distancia y para a otra más corta; corre hasta alcanzarlo) para
       que no alterne correr y caminar a cada rato, que se veía trabado. */
    int owner_fast = u->own_v > u->speed * 1.1f;
    if (!u->following && d_owner > MERC_FOLLOW)
        u->following = 1;
    else if (u->following && d_owner < MERC_STOP && u->own_v < 0.5f)
        u->following = 0; /* se para recién cuando el dueño se paró */
    if (u->following) {
        if (!u->running && (d_owner > MERC_RUN || owner_fast))
            u->running = 1;
        else if (u->running && d_owner < MERC_FOLLOW && !owner_fast)
            u->running = 0;
        float speed = u->speed;
        /* Lejos alcanza más rápido; pegada, va al paso del dueño (sin frenar); Entereza la apura */
        float pace = (u->running ? (d_owner > MERC_FOLLOW + 1.5f ? u->run_speed * MERC_CATCHUP : u->run_speed) : u->speed) *
                     (100 + u->au.speed) / 100.0f;
        if (d_owner < MERC_STOP + 0.5f && u->own_v * 0.95f < pace)
            pace = u->own_v * 0.95f;
        u->speed = pace;
        u->close = u->close ? d_owner < MERC_STOP + 0.1f : d_owner < MERC_STOP * 0.8f;
        if (!u->close)
            move_towards(u, m, o->x, o->y);
        else
            u->walking = 0;
        u->speed = speed;
    } else {
        u->walking = 0;
        u->running = 0;
    }
    u->anim_acc += u->gfx->anim[current_anim(u)].speed;
}

/* ---- La Valquiria (y otros aliados que pelean cuerpo a cuerpo) ---- */

#define PET_RANGE 18.0f /* busca monstruos hasta esta distancia */
#define PET_LEASH 22.0f /* sin alejarse tanto del dueño */

/* Lo que el gólem de sangre le pasa a su dueño: lo aplica game.c */
#define MAX_HEALS 32
static struct {
    int owner, amount;
} s_heals[MAX_HEALS];
static int s_num_heals;

int units_pop_owner_heal(int *owner, int *amount)
{
    if (s_num_heals <= 0)
        return 0;
    s_num_heals--;
    *owner = s_heals[s_num_heals].owner;
    *amount = s_heals[s_num_heals].amount;
    return 1;
}

/* Un esbirro que dispara (el mago, un arquero revivido) le tira al monstruo */
static void pet_shoot(Unit *u, const Unit *t)
{
    const char *name = u->ms.skill_missile[0] ? u->ms.skill_missile : u->missile;
    Missile *mi = missiles_spawn(name, u->x, u->y, t->x - u->x, t->y - u->y);
    if (!mi)
        return;
    mi->owner = u->owner;
    mi->source = (int)(u - s_units);
    mi->flags |= MF_MERC; /* el daño es suyo, no el del equipo del jugador */
    mi->ar = u->ms.ar * (100 + u->au.ar_pct) / 100;
    mi->level = u->ms.level;
    if (u->ms.skill_missile[0]) { /* el mago: su elemento */
        mi->dmin = mi->dmax = 0;
        mi->elem = u->ms.elem;
        mi->emin = u->ms.emin;
        mi->emax = u->ms.emax;
        mi->elen = u->ms.elen;
        mi->flags |= MF_ALWAYS_HIT;
    } else { /* lo del proyectil más el daño del monstruo, como los de los enemigos */
        int elem, emin, emax, dmin, dmax;
        missiles_base_damage(name, &elem, &emin, &emax, &dmin, &dmax);
        mi->dmin = (dmin + u->ms.dmin) * (100 + u->au.dmg_pct) / 100;
        mi->dmax = (dmax + u->ms.dmax) * (100 + u->au.dmg_pct) / 100;
        mi->elem = elem;
        mi->emin = emin;
        mi->emax = emax;
    }
}

/* Disparos de las torretas: game.c crea el proyectil con el daño de la habilidad */
#define MAX_SHOTS 32
static struct {
    int unit, target;
} s_shots[MAX_SHOTS];
static int s_num_shots;

int units_pop_shot(int *unit, int *target)
{
    if (s_num_shots <= 0)
        return 0;
    s_num_shots--;
    *unit = s_shots[s_num_shots].unit;
    *target = s_shots[s_num_shots].target;
    return 1;
}

static void turret_tick(Unit *u)
{
    const CharAnim *a = &u->gfx->anim[current_anim(u)];
    int frames = charanim_frames(a);
    if (u->state == ST_DYING || u->state == ST_DEAD)
        return;
    if (u->state == ST_ATTACK) {
        u->anim_acc += a->speed;
        int frame = u->anim_acc >> 8, hit_frame = a->hit_frame > 0 ? a->hit_frame : frames / 2;
        if (!u->hit_done && frame >= hit_frame) {
            u->hit_done = 1;
            if (units_alive_enemy(u->target) && s_num_shots < MAX_SHOTS) {
                s_shots[s_num_shots].unit = (int)(u - s_units);
                s_shots[s_num_shots].target = u->target;
                s_num_shots++;
            }
        }
        if (frame >= frames) {
            set_state(u, ST_IDLE);
            u->cooldown = u->shot_delay;
        }
        return;
    }
    u->anim_acc += a->speed;
    if (u->cooldown > 0) {
        u->cooldown--;
        return;
    }
    int best = -1;
    float best_d = PET_RANGE;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *e = &s_units[i];
        if (!e->enemy || e->state >= ST_DYING || e->hidden)
            continue;
        float d = dist_to(u, e->x, e->y);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best < 0)
        return;
    u->sector = sector_from_world(s_units[best].x - u->x, s_units[best].y - u->y);
    u->target = best;
    u->attack_anim = ANIM_ATTACK;
    u->hit_done = 0;
    set_state(u, ST_ATTACK);
}

/* El monstruo que tiene más cerca a esa distancia (con los tamaños), o -1 */
static int barb_nearest(const Unit *u, float range, float *dist)
{
    int c[GRID_CANDS], best = -1;
    float h = range + UNIT_RADIUS_MAX + GRID_SLACK, best_d = range;
    int nc = grid_box(u->x - h, u->y - h, u->x + h, u->y + h, c);
    for (int k = 0; k < (nc >= 0 ? nc : s_num_units); k++) {
        int i = nc >= 0 ? c[k] : k;
        if (!units_alive_enemy(i))
            continue;
        const Unit *e = &s_units[i];
        float d = dist_to(u, e->x, e->y) - e->radius;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    *dist = best_d;
    return best;
}

/* Lo que piensa un bárbaro del Acto 5 (AITHINK_Fn127_NpcBarb): al monstruo
   que tiene al lado le pega y después espera aip1; al que está a menos de
   aip3, corre (aip2 %); si no, camina 20 a 40 hacia el oeste (si no puede,
   20 a 40 al norte o al sur) y si tampoco, espera 15. Mientras camina vuelve
   a mirar cada aip1 ticks (el original camina de a tramos y piensa al final
   de cada uno). Lejos de todos los jugadores no hace nada, como los monstruos
   (las salas inactivas del original) */
static void barb_think(Unit *u, const Map *m, const UnitTarget *pl, int n)
{
    int near = 0;
    for (int k = 0; k < n && !near; k++) {
        float dx = pl[k].x - u->x, dy = pl[k].y - u->y;
        near = pl[k].player && dx * dx + dy * dy < SLEEP_RANGE * SLEEP_RANGE;
    }
    if (!near) {
        u->walking = u->running = 0;
        return;
    }
    int stall = u->aip[0] > 0 ? u->aip[0] : 15;
    float chase = u->aip[2] > 0 ? (float)u->aip[2] : 15.0f;
    int walk = 0; /* sigue caminando a donde iba si no aparece nadie */
    if (u->running) { /* corre hacia el monstruo hasta llegar */
        const Unit *e = units_alive_enemy(u->target) ? &s_units[u->target] : NULL;
        float d = e ? dist_to(u, e->x, e->y) - e->radius : 0.0f;
        if (e && d > u->reach && d < chase + 4.0f) {
            float sp = u->speed;
            u->speed = u->run_speed > sp ? u->run_speed : sp;
            move_towards(u, m, e->x, e->y);
            u->speed = sp;
            if (u->walking)
                return;
        }
    } else if (u->walking) { /* camina a donde iba */
        move_towards(u, m, u->target_x, u->target_y);
        float dx = u->target_x - u->x, dy = u->target_y - u->y;
        int there = dx * dx + dy * dy < 0.25f;
        if (!u->walking && !there) { /* trabado: espera un poco */
            u->timer = 15;
            return;
        }
        walk = !there;
        if (walk && --u->timer > 0)
            return;
    } else if (--u->timer > 0) {
        return; /* espera */
    }
    u->walking = u->running = 0;
    float d;
    int best = barb_nearest(u, chase, &d);
    if (best >= 0) {
        u->target = best;
        u->sector = sector_from_world(s_units[best].x - u->x, s_units[best].y - u->y);
        if (d <= u->reach) { /* (y al terminar el golpe espera aip1) */
            u->attack_anim = ANIM_ATTACK;
            u->ranged_attack = 0;
            set_state(u, ST_ATTACK);
            u->hit_done = 0;
            u->timer = stall;
            return;
        }
        if ((int)(rnd(u) % 100u) < u->aip[1]) {
            u->running = u->walking = 1;
            return;
        }
    }
    u->target = -1;
    if (walk) {
        u->walking = 1;
        u->timer = stall;
        return;
    }
    /* 20 a 40 al oeste; si no, al norte o al sur (primero uno al azar) */
    int side = (int)(rnd(u) & 1u) ? 1 : -1;
    for (int k = 0; k < 3; k++) {
        float tx = u->x + (k == 0 ? -40.0f : -10.0f) + (float)(rnd(u) % 20u);
        float ty = u->y - 10.0f + (float)(rnd(u) % 20u) + (k == 0 ? 0.0f : k == 1 ? -30.0f * (float)side : 30.0f * (float)side);
        if (tx >= 0.0f && ty >= 0.0f && !wall_foot(m, tx, ty, u->foot, u->move_mask)) {
            u->target_x = tx;
            u->target_y = ty;
            u->walking = 1;
            u->timer = stall;
            return;
        }
    }
    u->timer = 15;
}

static void pet_tick(Unit *u, const Map *m, UnitTarget *pl, int n)
{
    if (u->hidden || (!u->barb && (u->owner < 0 || u->owner >= n)))
        return;
    if (u->turret) {
        turret_tick(u);
        return;
    }
    const UnitTarget *o = u->barb ? NULL : &pl[u->owner];
    const CharAnim *a = &u->gfx->anim[current_anim(u)];
    int frames = charanim_frames(a);
    if (u->pet_ticks > 0 && u->state < ST_DYING && --u->pet_ticks == 0) { /* se le terminó (Revivir, los muros) */
        u->hp = 0;
        u->solid = 0;
        set_state(u, ST_DYING);
        return;
    }
    switch (u->state) {
    case ST_DEAD:
        return;
    case ST_DYING:
        u->anim_acc += a->speed;
        if ((u->anim_acc >> 8) >= frames)
            set_state(u, ST_DEAD);
        return;
    case ST_HIT:
    case ST_RISE: /* sale del suelo (los esqueletos, los gólems, los muros) */
        u->anim_acc += a->speed;
        if ((u->anim_acc >> 8) >= frames)
            set_state(u, ST_IDLE);
        return;
    case ST_ATTACK: {
        u->anim_acc += a->speed * (100 + u->au.ias + u->pet_ias) / 100; /* Fanatismo (y el Dominio demoníaco) */
        int frame = u->anim_acc >> 8, hit_frame = a->hit_frame > 0 ? a->hit_frame : frames / 2;
        if (!u->hit_done && frame >= hit_frame && u->ranged_attack) {
            u->hit_done = 1;
            const Unit *t = u->target >= 0 && u->target < s_num_units ? &s_units[u->target] : NULL;
            if (t && t->enemy && t->state < ST_DYING)
                pet_shoot(u, t);
        } else if (!u->hit_done && frame >= hit_frame) {
            u->hit_done = 1;
            Unit *t = u->target >= 0 && u->target < s_num_units ? &s_units[u->target] : NULL;
            if (t && t->enemy && t->state < ST_DYING && dist_to(u, t->x, t->y) <= u->reach + t->radius) {
                if (ally_hits(u, t, 0)) {
                    int dmg = u->ms.dmin + (int)(rnd(u) % (uint32_t)(u->ms.dmax - u->ms.dmin + 1));
                    /* El hombre cabra del Brujo: según el nivel, aturde, se enfurece (golpe mágico
                       sin defensa), entra en frenesí o parte en arco (Goatman Stun, Berserk, Frenzy, Cleave) */
                    int goat_pct = 0, goat_stun = 0, goat_magic = 0, goat_cleave = 0;
                    if (u->war_goat > 0) {
                        int gl = u->war_goat, r = (int)(rnd(u) % 100);
                        if (gl >= 4 && u->haste_ticks <= 0 && r < 20) { /* Frenesí: dm34 de velocidad un rato */
                            u->haste_ticks = 200 + 25 * (gl - 1);
                            u->haste_pct = 30 + (110 - 30) * (110 * gl / (gl + 6)) / 100;
                        }
                        r = (int)(rnd(u) % 100);
                        if (gl >= 5 && r < 25) {
                            goat_cleave = 1;
                            goat_pct = 5 + (gl - 1);
                        } else if (gl >= 3 && r < 50) {
                            goat_magic = 1;
                            goat_pct = 105 + (gl - 1);
                            int len = 75 - (110 * gl / (gl + 6)) * (75 - 25) / 100;
                            u->berserk_ticks = len > 25 ? len : 25;
                        } else if (gl >= 2 && r < 75) {
                            goat_stun = 30 + 5 * (gl - 1);
                        }
                    }
                    dmg = dmg * (100 + u->au.dmg_pct + goat_pct) / 100;
                    int target = (int)(t - s_units);
                    int dealt = units_resist(target, goat_magic ? ELEM_MAG : ELEM_NONE, dmg);
                    if (HAS_MOD(u, MOD_SPECTRAL)) /* golpe espectral (el demonio vinculado): un elemento al azar */
                        dealt += units_resist(target, ELEM_FIRE + (int)(rnd(u) % 3), dmg / 2 + 1);
                    if (u->pet_fire_max > 0) /* el gólem de fuego */
                        dealt += units_resist(target, ELEM_FIRE,
                                              u->pet_fire_min + (int)(rnd(u) % (uint32_t)(u->pet_fire_max - u->pet_fire_min + 1)));
                    int edmg = u->pet_emax > 0 ? u->pet_emin + (int)(rnd(u) % (uint32_t)(u->pet_emax - u->pet_emin + 1)) : 0;
                    if (edmg > 0 && u->pet_elem == ELEM_COLD) /* los lobos del Druida: frío */
                        dealt += units_resist(target, ELEM_COLD, edmg);
                    int lifetap = units_lifetap(target);
                    int exp = units_damage(target, dealt);
                    if (exp > 0 && s_num_kills < MAX_KILLS) { /* la experiencia es del dueño */
                        s_kills[s_num_kills].unit = target;
                        s_kills[s_num_kills].owner = u->barb ? UNITS_KILL_ALLY : u->owner;
                        s_kills[s_num_kills].exp = exp;
                        s_num_kills++;
                    }
                    if (!exp && goat_stun > 0)
                        units_stun(target, goat_stun);
                    if (goat_cleave) /* el tajo: también a los de al lado */
                        for (int j = 0; j < s_num_units; j++) {
                            Unit *o = &s_units[j];
                            if (j == target || !o->enemy || o->state >= ST_DYING ||
                                dist_to(u, o->x, o->y) > u->reach + o->radius + 0.5f)
                                continue;
                            int e2 = units_damage(j, units_resist(j, ELEM_NONE, dmg));
                            if (e2 > 0 && s_num_kills < MAX_KILLS) {
                                s_kills[s_num_kills].unit = j;
                                s_kills[s_num_kills].owner = u->owner;
                                s_kills[s_num_kills].exp = e2;
                                s_num_kills++;
                            }
                        }
                    if (!exp && u->pet_slow > 0) /* el gólem de arcilla frena (a los jefes, hasta la mitad) */
                        units_slow(target, t->unique && u->pet_slow > 50 ? 50 : u->pet_slow > 90 ? 90 : u->pet_slow, 750);
                    if (!exp && edmg > 0 && u->pet_elem == ELEM_COLD && u->pet_elen > 0)
                        units_chill(target, u->pet_elen);
                    if (!exp && edmg > 0 && u->pet_elem == ELEM_POIS) /* la enredadera venenosa */
                        units_poison(target, edmg, u->pet_elen > 0 ? u->pet_elen : 100, u->owner);
                    if (u->pet_hits > 0 && --u->pet_hits == 0) { /* el cuervo se va */
                        u->hp = 0;
                        set_state(u, ST_DYING);
                        return;
                    }
                    int heal = dealt * (lifetap + (u->buff_ticks > 0 ? u->buff_steal : 0)) / 100;
                    if (u->pet_steal > 0) { /* el gólem de sangre: roba vida y le pasa una parte al dueño */
                        int steal = dealt * u->pet_steal / 100;
                        heal += steal;
                        if (s_num_heals < MAX_HEALS && steal * u->pet_steal_owner / 100 > 0) {
                            s_heals[s_num_heals].owner = u->owner;
                            s_heals[s_num_heals].amount = steal * u->pet_steal_owner / 100;
                            s_num_heals++;
                        }
                    }
                    if (heal > 0)
                        u->hp = u->hp + heal > u->max_hp ? u->max_hp : u->hp + heal;
                }
            }
        }
        if (frame >= frames) {
            set_state(u, ST_IDLE);
            u->cooldown = 2;
        }
        return;
    }
    default:
        break;
    }
    if (u->cooldown > 0)
        u->cooldown--;
    if (u->pet_static) { /* el muro y los espíritus del Druida: quietos */
        u->anim_acc += u->gfx->anim[current_anim(u)].speed;
        return;
    }
    if (u->barb) {
        barb_think(u, m, pl, n);
        if (u->state == ST_IDLE)
            u->anim_acc += u->gfx->anim[current_anim(u)].speed;
        return;
    }
    float d_owner = dist_to(u, o->x, o->y);
    if (d_owner > MERC_WARP) {
        units_merc_warp((int)(u - s_units), o->x, o->y);
        return;
    }
    /* Pelear: sigue con el que estaba peleando mientras pueda; si no, el
       monstruo más cercano al que llega sin alejarse del dueño */
    if (!o->in_town && o->alive) {
        int best = -1;
        float best_d = PET_RANGE;
        if (u->target >= 0 && u->target < s_num_units) {
            const Unit *e = &s_units[u->target];
            float d = dist_to(u, e->x, e->y);
            if (e->enemy && e->state < ST_DYING && !e->hidden && e->killable && d < PET_RANGE &&
                dist_to(e, o->x, o->y) < PET_LEASH) {
                best = u->target;
                best_d = d;
            }
        }
        for (int i = 0; best < 0 && i < s_num_units; i++) {
            const Unit *e = &s_units[i];
            if (!e->enemy || e->state >= ST_DYING || e->hidden || !e->killable)
                continue;
            float d = dist_to(u, e->x, e->y);
            if (d < best_d && dist_to(e, o->x, o->y) < PET_LEASH) {
                best_d = d;
                best = i;
            }
        }
        if (best >= 0) {
            const Unit *e = &s_units[best];
            u->sector = sector_from_world(e->x - u->x, e->y - u->y);
            int shoots = u->ms.skill_missile[0] || (u->ranged && u->missile[0]); /* el mago, un arquero revivido */
            if (shoots && best_d <= RANGED_MAX) {
                u->walking = u->running = 0;
                if (u->cooldown == 0) {
                    u->target = best;
                    u->attack_anim = u->ranged && !u->ms.skill_missile[0] ? u->ranged_anim : ANIM_ATTACK;
                    if (!u->gfx->anim[u->attack_anim].ok)
                        u->attack_anim = ANIM_ATTACK;
                    u->ranged_attack = 1;
                    set_state(u, ST_ATTACK);
                    u->hit_done = 0;
                    u->cooldown = 10;
                    return;
                }
                u->anim_acc += u->gfx->anim[current_anim(u)].speed;
                return;
            }
            if (best_d <= u->reach + e->radius) {
                u->walking = u->running = 0;
                if (u->cooldown == 0) {
                    u->target = best;
                    u->attack_anim = ANIM_ATTACK;
                    u->ranged_attack = 0;
                    set_state(u, ST_ATTACK);
                    u->hit_done = 0;
                    return;
                }
                u->anim_acc += u->gfx->anim[current_anim(u)].speed;
                return;
            }
            u->target = best; /* va por él (y no cambia de idea a cada paso) */
            u->walking = 1;
            u->running = 1;
            move_towards(u, m, e->x, e->y);
            u->anim_acc += u->gfx->anim[current_anim(u)].speed;
            return;
        }
    }
    u->target = -1;
    /* Si no, sigue al dueño */
    if (d_owner > 3.0f) {
        u->walking = 1;
        u->running = d_owner > 6.0f;
        move_towards(u, m, o->x, o->y);
    } else {
        u->walking = u->running = 0;
    }
    u->anim_acc += u->gfx->anim[current_anim(u)].speed;
}

int units_pet_spawn(int owner, const char *id, float x, float y, int level, int hp_pct, int ar_pct, int dmg_pct)
{
    int ms = mon_row(&s_monstats, id);
    Unit *u = ms >= 0 ? create_monster(id, x, y, level, 2) : NULL;
    if (!u)
        return -1;
    u->merc = 0;
    u->pet = 1;
    u->owner = owner;
    /* Lo de un monstruo de ese nivel (monstats y monlvl), más lo de la habilidad */
    monster_stats(u, ms, level);
    memset(&u->ms, 0, sizeof u->ms);
    u->ms.level = level;
    u->max_hp = u->hp = u->max_hp * (100 + hp_pct) / 100;
    u->ms.hp_max = u->max_hp;
    u->ms.defense = u->ac;
    u->ms.ar = u->ar * (100 + ar_pct) / 100;
    u->ms.dmin = u->dmg_min * (100 + dmg_pct) / 100;
    u->ms.dmax = u->dmg_max * (100 + dmg_pct) / 100;
    u->level = level;
    u->reach = MELEE_REACH + u->radius;
    int i = (int)(u - s_units);
    units_merc_warp(i, x, y);
    return i;
}

int units_pet_summon(int owner, const char *id, float x, float y, const PetSpec *ps)
{
    int ms = mon_row(&s_monstats, id);
    if (ms < 0)
        return -1;
    if (ps->wall && s_map && wall_foot(s_map, x, y, 1, COLL_MASK_MONSTER_PATH))
        return -1; /* el muro no se pone encima de una pared */
    Unit *u = create_monster(id, x, y, ps->level, 2);
    if (!u)
        return -1;
    u->merc = 0;
    u->pet = 1;
    { /* su tamaño, como el de los monstruos (los gólems son grandes) */
        int ms2 = mon_row(&s_monstats2, id);
        int size = txt_get_int(&s_monstats2, ms2, "SizeX", 2);
        size = size < 1 ? 1 : size > 3 ? 3 : size;
        u->radius = 0.25f + 0.25f * (float)size;
    }
    u->owner = owner;
    u->zone = -1; /* va con su dueño */
    monster_stats(u, ms, ps->level);
    memset(&u->ms, 0, sizeof u->ms);
    u->max_hp = u->max_hp * (100 + ps->hp_pct) / 100 + ps->hp_add;
    if (u->max_hp < 1)
        u->max_hp = 1;
    u->hp = u->max_hp;
    u->ac += ps->def_add;
    u->ms.level = u->level;
    u->ms.hp_max = u->max_hp;
    u->ms.defense = u->ac;
    u->ms.ar = (u->ar + ps->ar_add) * (100 + ps->ar_pct) / 100;
    u->ms.dmin = (u->dmg_min + ps->dmg_add) * (100 + ps->dmg_pct) / 100;
    u->ms.dmax = (u->dmg_max + ps->dmg_add) * (100 + ps->dmg_pct) / 100;
    if (u->ms.dmax < u->ms.dmin)
        u->ms.dmax = u->ms.dmin;
    for (int e = ELEM_FIRE; e <= ELEM_POIS; e++) /* (los inmunes siguen inmunes) */
        if (u->res[e] < 100)
            u->res[e] = u->res[e] + ps->res > 95 ? 95 : u->res[e] + ps->res;
    u->speed = u->speed * (100.0f + (float)ps->speed_pct) / 100.0f;
    u->run_speed = u->speed * PET_RUN_PACE;
    if (ps->missile[0]) {
        snprintf(u->ms.skill_missile, sizeof u->ms.skill_missile, "%s", ps->missile);
        u->ms.elem = ps->elem;
        u->ms.emin = ps->emin;
        u->ms.emax = ps->emax;
        u->ms.elen = ps->elen;
    }
    u->pet_ticks = ps->ticks;
    u->pet_slow = ps->slow;
    u->pet_ias = ps->ias;
    u->pet_pdr = ps->pdr;
    u->war_goat = ps->war_goat;
    u->pet_steal = ps->steal;
    u->pet_steal_owner = ps->steal_owner;
    u->pet_thorns = ps->thorns;
    u->pet_fire_min = ps->fire_min;
    u->pet_fire_max = ps->fire_max;
    /* Los del Druida: su daño, su defensa y su puntería con monlvl.txt (SetSummonBaseStats) */
    if (ps->monlvl) {
        char lv[8];
        snprintf(lv, sizeof lv, "%d", u->level);
        int ml = txt_find_row(&s_monlvl, txt_col(&s_monlvl, "Level"), lv);
        u->ac += txt_get_int(&s_monlvl, ml, "AC", 0);
        u->ms.ar += txt_get_int(&s_monlvl, ml, "TH", 0);
    }
    u->ac = u->ac * (100 + ps->def_pct) / 100;
    u->ms.defense = u->ac;
    if (ps->dmg_max > 0) {
        u->ms.dmin = ps->dmg_min > 0 ? ps->dmg_min : 1;
        u->ms.dmax = ps->dmg_max > u->ms.dmin ? ps->dmg_max : u->ms.dmin;
    }
    u->pet_elem = ps->hit_elem;
    u->pet_emin = ps->hit_emin;
    u->pet_emax = ps->hit_emax > ps->hit_emin ? ps->hit_emax : ps->hit_emin;
    u->pet_elen = ps->hit_elen;
    u->pet_hits = ps->hits;
    u->reach = MELEE_REACH + u->radius + ps->reach_add;
    int i = (int)(u - s_units);
    if (ps->wall) { /* quieto donde va, tapa el paso y lo atacan */
        u->pet_static = 1;
        u->solid = 1;
        u->wander = 0.0f;
    } else if (ps->still) { /* quieto donde va (al lado del dueño) y lo atacan */
        u->pet_static = 1;
        u->wander = 0.0f;
        units_merc_warp(i, x, y);
    } else {
        units_merc_warp(i, x, y);
    }
    if (gfx_has(u->gfx, ANIM_SKILL)) /* sale del suelo (S1) */
        set_state(u, ST_RISE);
    return i;
}

int units_revive(int i, int owner, const PetSpec *ps)
{
    if (i < 0 || i >= s_num_units)
        return 0;
    Unit *u = &s_units[i];
    if (!u->enemy || u->state != ST_DEAD || u->searched || u->hidden || u->unique || u->mods || !u->revivable ||
        u->zone == ZONE_FREE)
        return 0;
    u->enemy = 0;
    u->pet = 1;
    u->owner = owner;
    u->searched = 1;
    u->zone = -1; /* va con su dueño */
    u->solid = 0;
    u->boom = 0;
    u->flee_ticks = u->stun_ticks = u->curse_ticks = u->chill_ticks = u->slow_ticks = u->poison_ticks = 0;
    u->bleed_ticks = u->link_ticks = u->haste_ticks = u->mark_ticks = u->buff_ticks = u->berserk_ticks = 0;
    u->enrage_ticks = 0;
    u->conv_ticks = u->nc_ticks = u->mm_ticks = u->slowmis_ticks = 0;
    u->nc_kind = u->nc_phys = u->nc_dmg = u->nc_res = u->nc_im = u->nc_lt = 0;
    u->max_hp = u->max_hp * (100 + ps->hp_pct) / 100 + ps->hp_add;
    u->hp = u->max_hp;
    memset(&u->ms, 0, sizeof u->ms);
    u->ms.level = u->level;
    u->ms.hp_max = u->max_hp;
    u->ms.defense = u->ac;
    u->ms.ar = u->ar;
    u->ms.dmin = u->dmg_min * (100 + ps->dmg_pct) / 100;
    u->ms.dmax = u->dmg_max * (100 + ps->dmg_pct) / 100;
    for (int e = ELEM_FIRE; e <= ELEM_POIS; e++) /* (los inmunes siguen inmunes) */
        if (u->res[e] < 100)
            u->res[e] = u->res[e] + ps->res > 95 ? 95 : u->res[e] + ps->res;
    u->speed = u->speed * (100.0f + (float)ps->speed_pct) / 100.0f;
    u->run_speed = u->speed * PET_RUN_PACE;
    u->pet_ticks = ps->ticks;
    u->reach = MELEE_REACH + u->radius;
    u->target = -1;
    u->cooldown = 0;
    set_state(u, ST_IDLE);
    return 1;
}

int units_cloak(int i, int ticks, int def_pct)
{
    if (!units_alive_enemy(i) || ticks <= 0)
        return 0;
    Unit *u = &s_units[i];
    u->cloak_ticks = ticks;
    u->cloak_def = def_pct;
    u->cloak_blind = !u->unique; /* a los jefes no los ciega (la defensa sí) */
    return 1;
}

int units_cloaked(int i)
{
    return units_alive_enemy(i) && s_units[i].cloak_ticks > 0;
}

void units_pet_mods(int i, int strong, int fast, int spectral)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    Unit *u = &s_units[i];
    if (strong && !HAS_MOD(u, MOD_STRONG)) { /* muy fuerte: el doble de daño */
        u->ms.dmin *= 2;
        u->ms.dmax *= 2;
        u->mods |= 1u << MOD_STRONG;
    }
    if (fast && !HAS_MOD(u, MOD_FAST)) { /* muy rápido: camina y ataca más rápido */
        u->speed *= 1.5f;
        u->run_speed = u->speed * PET_RUN_PACE;
        u->pet_ias += 50;
        u->mods |= 1u << MOD_FAST;
    }
    if (spectral)
        u->mods |= 1u << MOD_SPECTRAL;
}

void units_pet_buff(int i, int ticks, int haste, int pdr, int def, int regen, int steal)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    Unit *u = &s_units[i];
    u->buff_ticks = ticks;
    u->buff_pdr = pdr;
    u->buff_def = def;
    u->buff_regen = regen;
    u->buff_steal = steal;
    if (haste > 0)
        units_haste(i, haste, ticks);
}

void units_haste(int i, int pct, int ticks)
{
    if (i < 0 || i >= s_num_units || s_units[i].state >= ST_DYING || pct <= 0)
        return;
    Unit *u = &s_units[i];
    if (pct >= u->haste_pct || u->haste_ticks <= 0)
        u->haste_pct = pct > 100 ? 100 : pct;
    if (ticks > u->haste_ticks)
        u->haste_ticks = ticks;
}

void units_enrage(int i, int dmg_pct, int ias_pct, int ticks)
{
    if (!units_alive_enemy(i))
        return;
    Unit *u = &s_units[i];
    u->enrage_dmg = dmg_pct;
    if (ticks > u->enrage_ticks)
        u->enrage_ticks = ticks;
    units_haste(i, ias_pct, ticks);
}

int units_bind_rank(int i)
{
    if (!units_alive_enemy(i))
        return -1;
    const Unit *u = &s_units[i];
    return u->no_bind ? -1 : u->unique ? 1 : 0;
}

int units_bind(int i, int owner)
{
    if (units_bind_rank(i) < 0)
        return 0;
    Unit *u = &s_units[i];
    int unique = u->unique;
    uint32_t mods = u->mods;
    u->unique = 0;
    u->mods = 0;
    int ok = units_convert(i, owner, 1 << 30);
    u->unique = unique;
    u->mods = mods;
    return ok;
}

void units_pet_bonus(int i, int ias, int pdr, int res, int def_add, int ar_add)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    Unit *u = &s_units[i];
    u->pet_ias = ias;
    u->pet_pdr = pdr;
    for (int e = 0; e < 6 && res > 0; e++)
        if (u->res[e] < 100) /* los inmunes siguen igual */
            u->res[e] = u->res[e] + res > 95 ? 95 : u->res[e] + res;
    u->ac += def_add;
    u->ms.defense = u->ac;
    u->ms.ar += ar_add;
}

void units_pet_cap_hp(int i, int cap)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet || cap <= 0)
        return;
    Unit *u = &s_units[i];
    if (u->max_hp > cap)
        u->max_hp = u->ms.hp_max = cap;
    if (u->hp > u->max_hp)
        u->hp = u->max_hp;
}

void units_mark(int i, int flat, int ticks)
{
    if (!units_alive_enemy(i))
        return;
    s_units[i].mark_flat = flat;
    s_units[i].mark_ticks = ticks;
}

void units_pet_boost(int i, int hp_pct, int dmg_pct, int dmg_add)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    Unit *u = &s_units[i];
    u->max_hp = u->ms.hp_max = u->max_hp * (100 + hp_pct) / 100;
    u->hp = u->max_hp;
    u->ms.dmin = (u->ms.dmin + dmg_add) * (100 + dmg_pct) / 100;
    u->ms.dmax = (u->ms.dmax + dmg_add) * (100 + dmg_pct) / 100;
}

void units_pet_stats(int i, int hp, int dmin, int dmax, int ar, int def)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    Unit *u = &s_units[i];
    if (hp > 0)
        u->max_hp = u->hp = u->ms.hp_max = hp;
    u->ms.dmin = dmin > 0 ? dmin : 1;
    u->ms.dmax = dmax > u->ms.dmin ? dmax : u->ms.dmin;
    u->ms.ar = ar;
    u->ms.defense = u->ac = def;
}

int units_pet_hp(int i, int *max)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet) {
        *max = 1;
        return 0;
    }
    *max = s_units[i].max_hp;
    return s_units[i].hp;
}

int units_monster_look(const char *id, CharLook *look, char *wclass, size_t cap)
{
    int ms = mon_row(&s_monstats, id);
    int ms2 = mon_row(&s_monstats2, id);
    if (ms < 0 || ms2 < 0)
        return -1;
    char wc[8];
    const char *modes[ANIM_COUNT];
    monster_look(ms, ms2, 0, 0, look, wc, modes);
    snprintf(wclass, cap, "%s", wc);
    return 0;
}

void units_pet_shadow(int i)
{
    if (i >= 0 && i < s_num_units && s_units[i].pet)
        s_units[i].dark = 1;
}

void units_pet_place(int i, float x, float y)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    units_place(i, x, y);
}

void units_place(int i, float x, float y)
{
    if (i < 0 || i >= s_num_units)
        return;
    Unit *u = &s_units[i];
    if (x != u->x || y != u->y)
        u->sector = sector_from_world(x - u->x, y - u->y);
    u->x = x;
    u->y = y;
    grid_moved(u);
}

int units_revivable(int i)
{
    if (i < 0 || i >= s_num_units)
        return 0;
    const Unit *u = &s_units[i];
    return u->enemy && u->state == ST_DEAD && !u->searched && !u->hidden && !u->unique && !u->mods && u->revivable &&
           u->zone != ZONE_FREE;
}

int units_pet_targets(int *out, int max)
{
    int n = 0;
    for (int i = 0; i < s_num_units && n < max; i++) {
        const Unit *u = &s_units[i];
        if (u->pet && !u->turret && !u->hidden && u->zone != ZONE_FREE && u->state < ST_DYING && u->owner >= 0)
            out[n++] = i;
    }
    /* y después los bárbaros del Acto 5 que tiene cerca algún jugador (los
       otros no hacen nada: así no se llena la lista ni la recorre cada monstruo) */
    for (int i = 0; i < s_num_units && n < max; i++) {
        const Unit *u = &s_units[i];
        if (!u->barb || u->hidden || u->zone == ZONE_FREE || u->state >= ST_DYING)
            continue;
        for (int k = 0; k < s_tick_npl; k++) {
            float dx = s_tick_pl[k].x - u->x, dy = s_tick_pl[k].y - u->y;
            if (s_tick_pl[k].player && dx * dx + dy * dy < SLEEP_RANGE * SLEEP_RANGE) {
                out[n++] = i;
                break;
            }
        }
    }
    return n;
}

int units_pet_thorns(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].pet ? s_units[i].pet_thorns : 0;
}

int units_pet_wall(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].pet && s_units[i].pet_static;
}

int units_nec_curse(int i, const NecCurse *c)
{
    if (!units_alive_enemy(i))
        return 0;
    Unit *u = &s_units[i];
    int ai = c->kind == NC_TERROR || c->kind == NC_CONFUSE || c->kind == NC_DIM || c->kind == NC_ATTRACT;
    if (ai && u->unique)
        return 0; /* a los jefes no se los asusta, confunde ni enceguece */
    u->nc_kind = c->kind;
    u->nc_ticks = c->ticks;
    u->nc_owner = c->owner;
    u->nc_phys = c->phys;
    u->nc_dmg = c->dmg;
    u->nc_res = c->res;
    u->nc_im = c->im;
    u->nc_lt = c->lt;
    u->mm_ticks = 0;
    if (c->slow > 0)
        units_slow(i, c->slow, c->ticks);
    if (c->kind == NC_TERROR) { /* huye */
        if (u->state == ST_ATTACK || u->state == ST_HIT)
            set_state(u, ST_CHASE);
        if (u->snd && u->flee_ticks <= 0)
            unit_sound(u, u->snd->flee, (int)(rnd(u) % 8), -1);
        u->flee_ticks = c->ticks;
    }
    if (c->kind == NC_CONFUSE) { /* ataca a cualquiera de los suyos */
        u->mm_target = -1;
        u->mm_ticks = c->ticks;
    }
    return 1;
}

int units_nec_attract(int i, float radius, int ticks)
{
    if (!units_alive_enemy(i))
        return 0;
    int n = 0;
    for (int k = 0; k < s_num_units; k++) {
        Unit *u = &s_units[k];
        if (k == i || !units_alive_enemy(k) || u->unique || !in_radius(u, s_units[i].x, s_units[i].y, radius))
            continue;
        u->mm_target = i;
        u->mm_ticks = ticks;
        n++;
    }
    return n;
}

int units_nec_cursed(int i)
{
    return units_alive_enemy(i) && s_units[i].nc_ticks > 0 ? s_units[i].nc_kind : 0;
}

int units_lifetap(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].nc_ticks > 0 ? s_units[i].nc_lt : 0;
}

void units_pet_turret(int i, int cooldown)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    s_units[i].turret = 1;
    s_units[i].shot_delay = cooldown > 0 ? cooldown : 1;
    s_units[i].cooldown = (int)(rnd(&s_units[i]) % 8); /* que las tres no disparen juntas */
}

int units_pet_alive(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].pet && !s_units[i].hidden && s_units[i].state < ST_DYING;
}

void units_pet_damage(int i, int amount)
{
    if (!units_pet_alive(i) || amount <= 0)
        return;
    Unit *u = &s_units[i];
    int pdr = u->pet_pdr + (u->buff_ticks > 0 ? u->buff_pdr : 0); /* Juramento de sangre, Atiborrarse */
    if (pdr > 0)
        amount = amount * (100 - (pdr > 75 ? 75 : pdr)) / 100;
    u->hp -= amount;
    if (u->hp <= 0) {
        u->hp = 0;
        set_state(u, ST_DYING);
        if (u->convert_ticks > 0) /* convertido: muere como enemigo (su cadáver es de los otros) */
            u->convert_ticks = 1;
    } else if (amount * 4 >= u->max_hp && gfx_ready(u->gfx) && u->gfx->anim[ANIM_HIT].ok && u->state != ST_ATTACK) {
        set_state(u, ST_HIT); /* solo los golpes fuertes los frenan: si no, los esqueletos no llegan a pegar */
    }
}

void units_pet_set_look(int i, const CharLook *look, const char *wclass)
{
    if (i < 0 || i >= s_num_units || !s_units[i].pet)
        return;
    static const char *const modes[ANIM_COUNT] = { "nu", "wl", "a1", "gh", "dt", "dd", "a2", NULL, "rn", NULL };
    uint32_t parts = 2166136261u;
    for (int c = 0; c < COMP_COUNT; c++)
        for (const char *s = look->part[c]; *s; s++)
            parts = (parts ^ (uint8_t)*s) * 16777619u;
    char key[32];
    snprintf(key, sizeof key, "c/%s/%s/%08x", look->token, wclass, (unsigned)parts);
    UnitGfx *g = get_gfx(key, look, modes, wclass);
    if (!g)
        return;
    Unit *u = &s_units[i];
    unit_gfx_refs(u, -1);
    u->gfx = g;
    unit_gfx_refs(u, 1);
}

int units_monequip(const char *monster, int level, const char *loc, char *code, size_t cap)
{
    int cm = txt_col(&s_monequip, "monster");
    int best = -1, best_lvl = -1;
    for (int r = 1; cm >= 0 && r < s_monequip.rows; r++) {
        int lv = txt_get_int(&s_monequip, r, "level", 0);
        if (strcmp(txt_get(&s_monequip, r, cm), monster) != 0 || lv > level || lv <= best_lvl)
            continue;
        for (int k = 1; k <= 3; k++) {
            char col[8];
            snprintf(col, sizeof col, "loc%d", k);
            if (strcmp(txt_get(&s_monequip, r, txt_col(&s_monequip, col)), loc) == 0) {
                snprintf(col, sizeof col, "item%d", k);
                snprintf(code, cap, "%s", txt_get(&s_monequip, r, txt_col(&s_monequip, col)));
                best = r;
                best_lvl = lv;
            }
        }
    }
    return best >= 0 && code[0];
}

void units_pet_remove(int i)
{
    if (i >= 0 && i < s_num_units && s_units[i].pet)
        units_remove(i);
}

int units_merc_spawn(int owner, const char *id, float x, float y)
{
    for (int i = 0; i < s_num_units; i++)
        if (s_units[i].merc && s_units[i].owner == owner) {
            if (strcmp(s_units[i].name, id) != 0) { /* era de otro acto: se va */
                units_remove(i);
                break;
            }
            units_merc_warp(i, x, y);
            return i;
        }
    Unit *u = create_monster(id, x, y, 1, 2);
    if (!u)
        return -1;
    u->owner = owner;
    return (int)(u - s_units);
}

void units_merc_set(int i, const MercStats *s, int hp, const char *name)
{
    Unit *u = &s_units[i];
    u->ms = *s;
    u->level = s->level;
    u->max_hp = s->hp_max;
    u->hp = hp < 0 ? s->hp_max : hp > s->hp_max ? s->hp_max : hp;
    u->ac = s->defense;
    if (name)
        u->display = name;
}

void units_merc_hide(int i)
{
    if (i >= 0 && i < s_num_units && s_units[i].merc)
        s_units[i].hidden = 1;
}

void units_merc_hide_all(void)
{
    for (int i = 0; i < s_num_units; i++) {
        if (s_units[i].merc)
            s_units[i].hidden = 1;
        if (s_units[i].pet && !s_units[i].barb) /* la Valquiria de otra partida */
            units_remove(i);
    }
}

int units_merc_alive(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].merc && !s_units[i].hidden && s_units[i].state < ST_DYING;
}

void units_merc_health(int i, int *hp, int *max_hp)
{
    *hp = s_units[i].hp;
    *max_hp = s_units[i].max_hp;
}

void units_merc_damage(int i, int amount)
{
    if (!units_merc_alive(i) || amount <= 0)
        return;
    Unit *u = &s_units[i];
    u->hp -= amount;
    if (u->hp <= 0) {
        u->hp = 0;
        set_state(u, ST_DYING);
    } else if (amount * 12 >= u->max_hp && gfx_ready(u->gfx) && u->gfx->anim[ANIM_HIT].ok && u->state != ST_ATTACK) {
        set_state(u, ST_HIT);
    }
}

void units_merc_warp(int i, float x, float y)
{
    Unit *u = &s_units[i];
    u->hidden = 0;
    /* Al lado del dueño, en un lugar libre */
    for (int k = 0; k < 16; k++) {
        float a = (float)k * 0.785f, r = 1.5f + (float)(k / 8);
        float nx = x + cosf(a) * r, ny = y + sinf(a) * r;
        if (!s_map || !wall_foot(s_map, nx, ny, u->foot, u->move_mask)) {
            x = nx;
            y = ny;
            break;
        }
    }
    u->x = u->prev_x = x;
    u->y = u->prev_y = y;
    grid_moved(u);
    u->walking = u->running = 0;
    if (u->state >= ST_DYING) { /* revivida */
        u->hp = u->max_hp;
        set_state(u, ST_IDLE);
    }
}

void units_tick(const Map *m, UnitTarget *players, int num_players)
{
    s_map = m;
    s_unit_ticks++;
    s_tick_npl = num_players < TICK_PLAYERS ? num_players : TICK_PLAYERS;
    memcpy(s_tick_pl, players, (size_t)s_tick_npl * sizeof *players);
    ehydra_tick(players, num_players);
    a5_tick();
    for (int i = 0; i < s_num_units; i++) {
        Unit *u = &s_units[i];
        if (u->zone == ZONE_FREE)
            continue;
        u->prev_x = u->x;
        u->prev_y = u->y;
        int st = gfx_state(u->gfx);
        if (st == GFX_FAILED && !u->merc) { /* no hay con qué dibujarlo: no existe */
            unit_gfx_refs(u, -1);
            memset(u, 0, sizeof *u);
            u->zone = ZONE_FREE;
            u->hidden = 1;
            u->res_target = u->master = -1;
            unit_freed((int)(u - s_units));
            continue;
        }
        if (st != GFX_READY)
            continue; /* todavía se está cargando: quieto */
        if (u->enemy) {
            if (u->curse_ticks > 0 && --u->curse_ticks == 0)
                u->curse_def_flat = u->curse_def_pct = u->curse_dmg_pct = u->curse_ar_pct = 0;
            if (u->conv_ticks > 0 && --u->conv_ticks == 0)
                u->conv_def_pct = u->conv_res = 0;
            if (u->nc_ticks > 0 && --u->nc_ticks == 0) {
                u->nc_kind = u->nc_phys = u->nc_dmg = u->nc_res = u->nc_im = u->nc_lt = 0;
                u->mm_ticks = 0;
            }
            if (u->cloak_ticks > 0 && --u->cloak_ticks == 0)
                u->cloak_def = u->cloak_blind = 0;
            sound_tick(u);
            poison_tick(u);
            if (u->link_ticks > 0)
                u->link_ticks--;
            /* Lejos de todos: no piensa ni camina (como las salas inactivas del original) */
            if (u->state <= ST_CHASE && u->poison_ticks <= 0) {
                int near = 0;
                for (int k = 0; k < num_players && !near; k++) {
                    float dx = players[k].x - u->x, dy = players[k].y - u->y;
                    near = dx * dx + dy * dy < SLEEP_RANGE * SLEEP_RANGE;
                }
                if (!near) {
                    u->state = ST_IDLE;
                    u->walking = 0;
                    continue;
                }
            }
            if (u->slowmis_ticks > 0)
                u->slowmis_ticks--;
            /* Enfriado: solo actúa un tick de cada dos */
            if (u->chill_ticks > 0 && u->state < ST_DYING) {
                u->chill_ticks--;
                if (u->chill_ticks % 2)
                    continue;
            }
            /* Frenado (Empalar): pierde ese % de los ticks */
            if (u->slow_ticks > 0 && u->state < ST_DYING) {
                u->slow_ticks--;
                u->slow_acc += u->slow_pct;
                if (u->slow_acc >= 100) {
                    u->slow_acc -= 100;
                    continue;
                }
            }
            enemy_tick(u, m, players, num_players);
            if (u->mark_ticks > 0)
                u->mark_ticks--;
            if (u->enrage_ticks > 0)
                u->enrage_ticks--;
            if (u->lust_ticks > 0)
                u->lust_ticks--;
            if (u->shout_ticks > 0)
                u->shout_ticks--;
            if (u->haste_ticks > 0 && u->state < ST_DYING) { /* más rápido: a veces, un tick de más */
                u->haste_ticks--;
                u->haste_acc += u->haste_pct;
                if (u->haste_acc >= 100) {
                    u->haste_acc -= 100;
                    enemy_tick(u, m, players, num_players);
                }
            }
            continue;
        }
        if (u->pet && u->convert_ticks > 0 && --u->convert_ticks == 0) { /* se le pasó la Conversión */
            u->pet = 0;
            u->enemy = 1;
            u->owner = -1;
            u->target = -1;
            memset(&u->au, 0, sizeof u->au);
            if (u->state < ST_DYING) {
                u->solid = !u->hidden; /* monstruo otra vez: tapa el paso */
                set_state(u, ST_CHASE);
            }
        }
        if (u->pet) {
            sound_tick(u);
            pet_tick(u, m, players, num_players);
            if (u->berserk_ticks > 0)
                u->berserk_ticks--;
            if (u->buff_ticks > 0 && u->state < ST_DYING) { /* Atiborrarse: regenera mientras dura */
                u->buff_ticks--;
                u->buff_regen_acc += (float)u->buff_regen / 256.0f;
                int h = (int)u->buff_regen_acc;
                if (h > 0) {
                    u->buff_regen_acc -= (float)h;
                    u->hp = u->hp + h > u->max_hp ? u->max_hp : u->hp + h;
                }
            }
            if (u->haste_ticks > 0 && u->state < ST_DYING) {
                u->haste_ticks--;
                u->haste_acc += u->haste_pct;
                if (u->haste_acc >= 100) {
                    u->haste_acc -= 100;
                    pet_tick(u, m, players, num_players);
                }
            }
            continue;
        }
        if (u->merc) {
            sound_tick(u);
            merc_tick(u, m, players, num_players);
            continue;
        }
        if (u->goal) /* el preso que va a su portal (monai5.inc) */
            a5_goal_tick(u, m);
        else
            wander_tick(u, m, players, num_players);
        const CharAnim *a = &u->gfx->anim[current_anim(u)];
        u->anim_acc += u->anim_speed ? u->anim_speed : a->speed;
        /* La animación de abrirse (la jaula, el árbol) se juega una vez y sigue la de abierto */
        if (u->obj_next[0] && (u->anim_acc >> 8) >= (u->obj_frames > 0 ? u->obj_frames : charanim_frames(a))) {
            char next[4];
            snprintf(next, sizeof next, "%s", u->obj_next);
            units_object_mode(i, next, NULL);
        }
    }
    a5_riders(); /* los diablillos montados van con su bestia (después de que se movió) */
}

/* ---- Consultas ---- */

int units_count(void)
{
    return s_num_units;
}

int units_visible(int i)
{
    return s_units[i].zone != ZONE_FREE && !s_units[i].hidden;
}

int units_zone(int i)
{
    return i >= 0 && i < s_num_units ? s_units[i].zone : ZONE_FREE;
}

/* Borra las unidades de la zona: sus lugares quedan libres y los gráficos
   que nadie más usa se liberan. Las que se fueron caminando a otra zona
   armada pasan a ser de esa. */
void units_heal(int i, int hp)
{
    if (i >= 0 && i < s_num_units && s_units[i].state < ST_DYING && hp > 0)
        s_units[i].hp = s_units[i].hp + hp < s_units[i].max_hp ? s_units[i].hp + hp : s_units[i].max_hp;
}

void units_remove(int i)
{
    if (i < 0 || i >= s_num_units || s_units[i].zone == ZONE_FREE)
        return;
    Unit *u = &s_units[i];
    unit_gfx_refs(u, -1);
    memset(u, 0, sizeof *u);
    u->zone = ZONE_FREE;
    u->hidden = 1;
    u->res_target = u->master = -1;
    unit_freed(i);
}

int units_free_zone(int zone, int (*zone_at)(float x, float y))
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        Unit *u = &s_units[i];
        if (u->zone != zone)
            continue;
        int other = zone_at ? zone_at(u->x, u->y) : -1;
        if (other >= 0 && other != zone && u->state < ST_DYING) {
            u->zone = other;
            continue;
        }
        unit_gfx_refs(u, -1);
        memset(u, 0, sizeof *u);
        u->zone = ZONE_FREE;
        u->hidden = 1;
        u->res_target = u->master = -1;
        unit_freed(i);
        n++;
    }
    for (int i = 0; i < s_num_units; i++) /* nadie apunta a los que se fueron */
        if (s_units[i].zone != ZONE_FREE) {
            if (s_units[i].res_target >= 0 && s_units[s_units[i].res_target].zone == ZONE_FREE)
                s_units[i].res_target = -1;
            if (s_units[i].master >= 0 && s_units[s_units[i].master].zone == ZONE_FREE)
                s_units[i].master = -1;
        }
    while (s_num_units > 0 && s_units[s_num_units - 1].zone == ZONE_FREE)
        s_num_units--;
    unit_freed(s_num_units);
    s_num_kills = 0;
    return n;
}

size_t units_gfx_bytes(void)
{
    size_t n = 0;
    for (int i = 0; i < s_num_gfx; i++)
        if (s_gfx[i].key[0] && gfx_state(&s_gfx[i]) == GFX_READY)
            n += s_gfx[i].bytes;
    return n;
}

int units_gfx_loaded(void)
{
    int n = 0;
    for (int i = 0; i < s_num_gfx; i++)
        n += s_gfx[i].key[0] != 0;
    return n;
}

void units_position(int i, float alpha, float *x, float *y)
{
    const Unit *u = &s_units[i];
    *x = u->prev_x + (u->x - u->prev_x) * alpha;
    *y = u->prev_y + (u->y - u->prev_y) * alpha;
}

/* Cae muerto sin dar nada (el orbe que se rompe) */
void units_play_death(int i)
{
    if (i < 0 || i >= s_num_units || s_units[i].state >= ST_DYING)
        return;
    Unit *u = &s_units[i];
    u->hp = 0;
    u->solid = 0;
    u->exp = 0;
    set_state(u, ST_DYING);
}

/* El primer jefe vivo de la zona (al azar o de nombre) que puede tener la
   estatuilla de jade (QUESTS_SetJadeFigurineBoss: ni voladores ni el
   fetiche de la Gidbinn), -1 si no hay */
int units_bird_boss_in_zone(int zone)
{
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        if (u->zone == zone && u->spawn_boss && u->enemy && u->state < ST_DYING && !u->pet && !u->merc &&
            u->move_mask != COLL_MASK_FLYING && strcmp(u->name, "fetish11") != 0)
            return i;
    }
    return -1;
}

int units_is_corpse(int i)
{
    return s_units[i].enemy && s_units[i].state >= ST_DYING;
}

int units_riding(int i)
{
    return s_units[i].ride != 0;
}

void units_draw(int i, int x, int y)
{
    const Unit *u = &s_units[i];
    if (u->hidden || !gfx_ready(u->gfx) || (u->pshift && job_flag_get(&u->pshift->state) == PS_LOADING))
        return;
    int anim = current_anim(u);
    const CharAnim *a = &u->gfx->anim[anim];
    int frame = u->anim_acc >> 8, frames = charanim_frames(a);
    if (u->state == ST_ATTACK && u->jab && u->seq) { /* la secuencia: el modo y el cuadro de cada paso de monseq.txt */
        int step = frame < u->seq->n ? frame : u->seq->n - 1;
        if (u->gfx->anim[u->seq->anim[step]].ok) {
            anim = u->seq->anim[step];
            a = &u->gfx->anim[anim];
            frames = charanim_frames(a);
        }
        frame = u->seq->frame[step];
    }

    /* Las animaciones de una sola vez se quedan en su último cuadro */
    if (u->state == ST_DEAD)
        frame = anim == ANIM_DEAD ? 0 : frames - 1;
    else if ((u->state == ST_DYING || u->state == ST_HIT || u->state == ST_ATTACK || u->state == ST_RISE) &&
             frame >= frames)
        frame = frames - 1;
    /* Los objetos: solo los cuadros de su modo (en ronda o quieto en el último) */
    if (u->obj_row > 0 && u->obj_frames > 0) {
        frame = u->obj_cycle ? frame % u->obj_frames : frame < u->obj_frames ? frame : u->obj_frames - 1;
        frame += u->obj_first;
        if (frame >= frames)
            frame = frames - 1;
    }

    if (u->ride) /* el diablillo montado, sobre el lomo de su bestia */
        y -= RIDE_DY;
    else if (u->shadow && u->state < ST_DYING)
        fb_shade_ellipse(x, y, 14, 6, 14);
    /* Los estados con color de states.txt: el frío (colorshift 108, prioridad
       100) le gana al veneno (104, prioridad 95) */
    const uint8_t *tint = NULL;
    if (u->state < ST_DYING)
        tint = u->chill_ticks > 0 ? fb_hue(108) : u->poison_ticks > 0 ? fb_hue(104) : NULL;
    if (u->dark) /* el Guerrero de las sombras: oscuro */
        tint = g_light[LIGHT_LEVELS / 4];
    const uint8_t *pshift = unit_palshift(u);
    if (pshift) { /* sus colores, y encima el del estado */
        static uint8_t combo[256];
        for (int c = 0; c < 256; c++)
            combo[c] = tint ? tint[pshift[c]] : pshift[c];
        tint = combo;
    }
    if (u->champion == CHAMP_GHOSTLY && u->state < ST_DYING) /* el campeón fantasmal, translúcido */
        charanim_draw_ghost(a, u->sector, frame, x, y, ~0u, BLEND_50);
    else
        charanim_draw_over(a, gfx_ready(u->gfx_parts) ? &u->gfx_parts->anim[anim] : NULL, u->sector, frame, x, y, tint);
}

int units_lights(float *x, float *y, int *r, int max)
{
    int n = 0;
    for (int i = 0; i < s_num_units && n < max; i++) {
        const Unit *u = &s_units[i];
        if (u->zone == ZONE_FREE || u->light <= 0 || u->hidden || u->state >= ST_DYING || !gfx_ready(u->gfx))
            continue;
        x[n] = u->x;
        y[n] = u->y;
        r[n] = u->light;
        n++;
    }
    return n;
}

int units_blocking_at(float x, float y, float radius, float *ux, float *uy)
{
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        float dx = u->x - x, dy = u->y - y, r = radius + u->radius;
        if (u->solid && dx * dx + dy * dy < r * r) {
            if (ux) *ux = u->x;
            if (uy) *uy = u->y;
            return 1;
        }
    }
    return 0;
}

int units_blocking(float x, float y, float radius)
{
    return units_blocking_at(x, y, radius, NULL, NULL);
}

int units_find_all(const char *name, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < s_num_units && n < max; i++)
        if (strcmp(s_units[i].name, name) == 0)
            out[n++] = i;
    return n;
}

void units_set_lit(int i, int lit)
{
    Unit *u = &s_units[i];
    if (u->gfx_on && u->gfx_off)
        u->gfx = lit ? u->gfx_on : u->gfx_off;
}

int units_find(const char *name, float *x, float *y)
{
    for (int i = 0; i < s_num_units; i++)
        if (strcmp(s_units[i].name, name) == 0) {
            *x = s_units[i].x;
            *y = s_units[i].y;
            return 0;
        }
    return -1;
}

int units_find_enemy(float x, float y, float range)
{
    int best = -1, c[GRID_CANDS];
    float best_d = range * range, h = fabsf(range) + UNIT_RADIUS_MAX - UNIT_RADIUS + GRID_SLACK;
    int nc = grid_box(x - h, y - h, x + h, y + h, c);
    for (int k = 0; k < (nc >= 0 ? nc : s_num_units); k++) {
        int i = nc >= 0 ? c[k] : k;
        const Unit *u = &s_units[i];
        if (!u->enemy || u->state >= ST_DYING || u->hidden || !u->killable)
            continue;
        float dx = u->x - x, dy = u->y - y, d = sqrtf(dx * dx + dy * dy) - (u->radius - UNIT_RADIUS);
        d = d > 0.0f ? d * d : 0.0f;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

int units_find_enemy_path(const int (*cells)[2], int n, int msize, const int *skip, int nskip)
{
    int best = -1, best_c = n;
    float best_d = 1e30f;
    if (n <= 0)
        return -1;
    int x0 = cells[0][0], x1 = x0, y0 = cells[0][1], y1 = y0;
    for (int c = 1; c < n; c++) {
        x0 = cells[c][0] < x0 ? cells[c][0] : x0;
        x1 = cells[c][0] > x1 ? cells[c][0] : x1;
        y0 = cells[c][1] < y0 ? cells[c][1] : y0;
        y1 = cells[c][1] > y1 ? cells[c][1] : y1;
    }
    int cand[GRID_CANDS], nc = grid_box((float)(x0 - 2), (float)(y0 - 2), (float)(x1 + 3), (float)(y1 + 3), cand);
    for (int j = 0; j < (nc >= 0 ? nc : s_num_units); j++) {
        int i = nc >= 0 ? cand[j] : j;
        const Unit *u = &s_units[i];
        if (!u->enemy || u->state >= ST_DYING || u->hidden || !u->killable || u->x < x0 - 2 || u->x >= x1 + 3 ||
            u->y < y0 - 2 || u->y >= y1 + 3)
            continue;
        int k = 0;
        while (k < nskip && skip[k] != i)
            k++;
        if (k < nskip)
            continue;
        for (int c = 0; c < best_c + 1 && c < n; c++) {
            if (!missiles_touches(msize, cells[c][0], cells[c][1], u->x, u->y, u->foot))
                continue;
            float dx = u->x - (cells[c][0] + 0.5f), dy = u->y - (cells[c][1] + 0.5f), d = dx * dx + dy * dy;
            if (c < best_c || d < best_d) {
                best = i;
                best_c = c;
                best_d = d;
            }
            break;
        }
    }
    return best;
}

int units_foot(int i)
{
    return s_units[i].foot;
}

int units_find_melee(float x, float y, float range)
{
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        if (!u->enemy || u->state >= ST_DYING || u->hidden || !u->killable)
            continue;
        float dx = u->x - x, dy = u->y - y, d = sqrtf(dx * dx + dy * dy) - (u->radius - UNIT_RADIUS);
        if (d < range + u->melee_bonus && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

int units_find_reviver(float x, float y, float range)
{
    int best = -1;
    float best_d = range * range;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        /* (también la madre vil mientras le queden crías por parir: si no,
           los bots mataban una cría tras otra sin llegar a ella) */
        int spawner = u->ai == AI_VILEMOTHER && u->ai_p[0] < u->aip[0];
        if (!u->enemy || u->state >= ST_DYING || !(u->resurrect || u->raiser || spawner))
            continue;
        float dx = u->x - x, dy = u->y - y, d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* Como SUNITDMG_ApplyResistancesAndAbsorb del original: el daño baja en el
   porcentaje de su resistencia (con 100 o más es inmune; las negativas
   suben el daño, hasta el doble). Los monstruos no tienen tope. */
/* La resistencia con Convicción: a los inmunes (100 o más) solo les baja un
   quinto, como en el original (pueden dejar de serlo) */
static int unit_res(const Unit *u, int elem)
{
    int r = u->res[elem];
    if (u->conv_ticks > 0 && (elem == ELEM_FIRE || elem == ELEM_COLD || elem == ELEM_LTNG))
        r -= r >= 100 ? u->conv_res / 5 : u->conv_res;
    if (u->nc_ticks > 0) { /* Amplificar daño y Decrepitud (lo físico), Bajar resistencias */
        int m = elem == ELEM_NONE ? u->nc_phys : elem != ELEM_MAG ? u->nc_res : 0;
        if (m > 0)
            r -= r >= 100 ? m / 5 : m;
    }
    return r;
}

int units_resist_pierce(int i, int elem, int amount, int pierce)
{
    if (i < 0 || i >= s_num_units || elem < 0 || elem >= 6 || amount <= 0)
        return amount;
    int r = unit_res(&s_units[i], elem);
    if (r < 100)
        r -= pierce;
    r = r > 100 ? 100 : r < -100 ? -100 : r;
    int dealt = (int)((int64_t)amount * (100 - r) / 100);
    const Unit *u = &s_units[i];
    if (u->mark_ticks > 0 && (elem == ELEM_NONE || elem == ELEM_MAG) && r < 100) /* Marca de la muerte */
        dealt += u->mark_flat;
    return dealt;
}

int units_resist(int i, int elem, int amount)
{
    if (i < 0 || i >= s_num_units || elem < 0 || elem >= 6 || amount <= 0)
        return amount;
    int r = unit_res(&s_units[i], elem);
    r = r > 100 ? 100 : r < -100 ? -100 : r;
    int dealt = (int)((int64_t)amount * (100 - r) / 100);
    const Unit *u = &s_units[i];
    if (u->mark_ticks > 0 && (elem == ELEM_NONE || elem == ELEM_MAG) && r < 100) /* Marca de la muerte */
        dealt += u->mark_flat;
    return dealt;
}

int units_alive_enemy(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].enemy && s_units[i].state < ST_DYING && !s_units[i].hidden &&
           s_units[i].killable;
}

/* Lo que reciben los otros del grupo de almas unidas */
static void link_share(int i, int amount)
{
    static int busy; /* lo compartido no se vuelve a compartir */
    const Unit *u = &s_units[i];
    if (busy || u->link_ticks <= 0 || u->link_group == 0 || amount <= 0)
        return;
    int share = amount * u->link_pct / 100;
    if (share <= 0)
        return;
    busy = 1;
    for (int j = 0; j < s_num_units; j++) {
        Unit *o = &s_units[j];
        if (j == i || o->link_ticks <= 0 || o->link_group != u->link_group || !units_alive_enemy(j))
            continue;
        int exp = units_damage(j, share);
        if (exp > 0 && s_num_kills < MAX_KILLS) {
            s_kills[s_num_kills].unit = j;
            s_kills[s_num_kills].owner = o->link_owner;
            s_kills[s_num_kills].exp = exp;
            s_num_kills++;
        }
    }
    busy = 0;
}

void units_link(int i, int group, int pct, int owner, int ticks)
{
    if (!units_alive_enemy(i))
        return;
    Unit *u = &s_units[i];
    u->link_group = group;
    u->link_pct = pct;
    u->link_owner = owner;
    u->link_ticks = ticks;
}

int units_linked(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].link_ticks > 0 ? s_units[i].link_group : 0;
}

int units_damage(int i, int amount)
{
    if (!units_alive_enemy(i))
        return 0;
    Unit *u = &s_units[i];
    u->hp -= amount;
    link_share(i, amount);
    if (HAS_MOD(u, MOD_LIGHTNING) && amount > 0 && rnd(u) % 2 == 0) {
        /* Encantado de rayo: suelta chispas (Carga eléctrica) en abanico */
        int bolts = 3 + (int)(rnd(u) % 3);
        float base = rndf(u) * 2.0f * PI_F;
        for (int k = 0; k < bolts; k++) {
            float a = base + (float)k * 2.0f * PI_F / (float)bolts;
            Missile *mi = missiles_spawn("chargedbolt", u->x, u->y, cosf(a), sinf(a));
            if (!mi)
                break;
            mi->owner = -1;
            mi->source = i;
            mi->ar = u->ar;
            mi->level = u->level;
            mi->dmin = mi->dmax = 0;
            mi->elem = ELEM_LTNG;
            mi->emin = 1 + u->level / 3;
            mi->emax = 2 + u->level;
        }
    }
    if (u->hp <= 0) {
        u->hp = 0;
        u->jab = 0;
        set_state(u, ST_DYING);
        u->solid = 0;
        if (u->ai == AI_SCARAB)
            scarab_bolts(u);
        return u->exp > 0 ? u->exp : 1;
    }
    /* Como en el original: si el golpe le saca 1/8 de la vida o más, se resiente */
    if (amount * 8 >= u->max_hp && gfx_ready(u->gfx) && u->gfx->anim[ANIM_HIT].ok && !(u->jab && u->ai == AI_SANDLEAPER)) {
        u->jab = 0;
        u->ai_move = u->ai_wait = 0;
        set_state(u, ST_HIT);
        if (u->ai == AI_SCARAB)
            scarab_bolts(u);
    }
    else if (u->state == ST_IDLE)
        u->state = ST_CHASE;
    if (u->ai == AI_BATDEMON && amount > 0)
        u->ai_p[2] = 1; /* le pegaron: puede alejarse */
    if (amount > 0)
        u->hurt = 1;
    return 0;
}

const char *units_display_name(int i)
{
    return s_units[i].display ? s_units[i].display : s_units[i].name;
}

void units_health(int i, int *hp, int *max_hp)
{
    *hp = s_units[i].hp;
    *max_hp = s_units[i].max_hp;
}

const char *units_treasure_class(int i)
{
    return s_units[i].tc;
}

void units_debug_stats(int i, char *buf, size_t cap)
{
    const Unit *u = &s_units[i];
    snprintf(buf, cap, "nivel %d vida %d daño %d-%d (%d-%d) exp %d def %d punt %d res %d/%d/%d/%d/%d/%d hab %s %d tc %s%s",
             u->level, u->max_hp, u->dmg_min, u->dmg_max, u->a2_min, u->a2_max, u->exp, u->ac, u->ar, u->res[0],
             u->res[1], u->res[2], u->res[3], u->res[4], u->res[5], u->skill[0][0] ? u->skill[0] : "-", u->sk_lvl[0],
             u->tc, u->mods ? " (con modificadores)" : "");
}

float units_radius(int i)
{
    return s_units[i].radius;
}

int units_level(int i)
{
    return s_units[i].level > 0 ? s_units[i].level : 1;
}

int units_defense(int i)
{
    const Unit *u = &s_units[i];
    int pct = (u->curse_ticks > 0 ? u->curse_def_pct : 0) + (u->conv_ticks > 0 ? u->conv_def_pct : 0) + u->au.def_pct +
              (u->cloak_ticks > 0 ? u->cloak_def : 0) + (u->lust_ticks > 0 ? 50 : 0) + (u->shout_ticks > 0 ? u->shout_pct : 0);
    if (u->berserk_ticks > 0)
        return 0; /* la Furia del hombre cabra: sin defensa mientras dura */
    int add = u->buff_ticks > 0 ? u->buff_def : 0; /* Atiborrarse */
    if (pct == 0 && u->curse_ticks <= 0)
        return u->ac + add;
    int ac = u->ac * (100 + pct) / 100 - (u->curse_ticks > 0 ? u->curse_def_flat : 0) + add;
    return ac > 0 ? ac : 0;
}

int units_is_undead(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].undead;
}

int units_is_demon(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].demon;
}

void units_conviction(int i, int def_pct, int res, int ticks)
{
    if (!units_alive_enemy(i))
        return;
    Unit *u = &s_units[i];
    if (u->conv_ticks <= 0 || def_pct < u->conv_def_pct)
        u->conv_def_pct = def_pct;
    if (u->conv_ticks <= 0 || res > u->conv_res)
        u->conv_res = res;
    if (ticks > u->conv_ticks)
        u->conv_ticks = ticks;
}

int units_convicted(int i)
{
    return units_alive_enemy(i) && s_units[i].conv_ticks > 0;
}

/* Conversión: pelea para `owner` (como la Valquiria, con lo suyo) hasta que
   se le pasa; los jefes no se convierten */
int units_convert(int i, int owner, int ticks)
{
    if (!units_alive_enemy(i) || s_units[i].unique || s_units[i].mods || ticks <= 0)
        return 0;
    Unit *u = &s_units[i];
    u->enemy = 0;
    u->pet = 1;
    u->owner = owner;
    u->solid = 0; /* como las otras mascotas, no le tapa el paso a su dueño (el demonio vinculado del Brujo lo
                     encerraba al pie de la escalera del River of Flame) */
    u->convert_ticks = ticks;
    u->flee_ticks = u->stun_ticks = 0;
    memset(&u->ms, 0, sizeof u->ms);
    u->ms.level = u->level;
    u->ms.hp_max = u->max_hp;
    u->ms.defense = u->ac;
    u->ms.ar = u->ar;
    u->ms.dmin = u->dmg_min;
    u->ms.dmax = u->dmg_max;
    u->cooldown = 0;
    set_state(u, ST_IDLE);
    return 1;
}

int units_converted(int i)
{
    return i >= 0 && i < s_num_units && s_units[i].pet && s_units[i].convert_ticks > 0;
}

int units_converted_list(int *out, int max)
{
    int n = 0;
    for (int i = 0; i < s_num_units && n < max; i++)
        if (units_converted(i) && s_units[i].state < ST_DYING && !s_units[i].hidden)
            out[n++] = i;
    return n;
}

int units_allies(int *out, int max)
{
    int n = 0;
    for (int i = 0; i < s_num_units && n < max; i++) {
        const Unit *u = &s_units[i];
        if ((u->merc || u->pet) && !u->hidden && u->zone != ZONE_FREE && u->state < ST_DYING && u->owner >= 0)
            out[n++] = i;
    }
    return n;
}

int units_ally_owner(int i)
{
    return i >= 0 && i < s_num_units && (s_units[i].merc || s_units[i].pet) ? s_units[i].owner : -1;
}

void units_ally_aura(int i, const AllyAura *a)
{
    if (i >= 0 && i < s_num_units && (s_units[i].merc || s_units[i].pet))
        s_units[i].au = *a;
}

const AllyAura *units_ally_aura_get(int i)
{
    static const AllyAura none;
    return i >= 0 && i < s_num_units ? &s_units[i].au : &none;
}

int units_ally_resist(int i, int e)
{
    if (i < 0 || i >= s_num_units || e < 0 || e > 2)
        return 0;
    const Unit *u = &s_units[i];
    int r = u->ms.resist + u->au.res[e], most = 75 + u->au.res_max[e];
    most = most > 95 ? 95 : most;
    return r > most ? most : r;
}

int units_ally_heal(int i, int amount)
{
    if (i < 0 || i >= s_num_units || !(s_units[i].merc || s_units[i].pet) || s_units[i].state >= ST_DYING ||
        s_units[i].turret || s_units[i].hp >= s_units[i].max_hp)
        return 0;
    Unit *u = &s_units[i];
    u->hp = u->hp + amount > u->max_hp ? u->max_hp : u->hp + amount;
    return 1;
}

/* ---- Efectos de las habilidades ---- */

/* Al borde del monstruo (los grandes se alcanzan desde más lejos) */
static int in_radius(const Unit *u, float x, float y, float radius)
{
    return in_radius_xy(u, x, y, radius + u->radius - UNIT_RADIUS);
}

void units_chill(int i, int ticks)
{
    if (s_cold_div > 1) /* en Pesadilla la mitad y en Infierno un cuarto (SUNITDMG: MonsterColdDivisor) */
        ticks = ticks / s_cold_div > 1 ? ticks / s_cold_div : 1;
    if (units_alive_enemy(i) && ticks > s_units[i].chill_ticks)
        s_units[i].chill_ticks = ticks;
}

/* Si ya estaba envenenado, se queda con el veneno más fuerte */
int units_poison(int i, int total, int ticks, int owner)
{
    if (!units_alive_enemy(i) || total <= 0 || ticks <= 0)
        return 0;
    total = units_resist(i, ELEM_POIS, total);
    if (total <= 0)
        return 0; /* inmune */
    Unit *u = &s_units[i];
    float rate = (float)total / (float)ticks;
    if (u->poison_ticks > 0 && u->poison_rate * u->poison_ticks >= (float)total)
        return 0;
    int fresh = u->poison_ticks <= 0;
    u->poison_ticks = ticks;
    u->poison_rate = rate;
    u->poison_owner = owner;
    return fresh;
}

int units_bleed(int i, int total, int ticks, int owner)
{
    if (!units_alive_enemy(i) || total <= 0 || ticks <= 0)
        return 0;
    total = units_resist(i, ELEM_NONE, total);
    if (total <= 0)
        return 0;
    Unit *u = &s_units[i];
    if (u->bleed_ticks > 0 && u->bleed_rate * u->bleed_ticks >= (float)total)
        return 0;
    u->bleed_ticks = ticks;
    u->bleed_rate = (float)total / (float)ticks;
    u->bleed_owner = owner;
    return 1;
}

int units_pop_kill(int *unit, int *owner, int *exp)
{
    if (s_num_kills == 0)
        return 0;
    s_num_kills--;
    *unit = s_kills[s_num_kills].unit;
    *owner = s_kills[s_num_kills].owner;
    *exp = s_kills[s_num_kills].exp;
    return 1;
}

int units_status(int i)
{
    const Unit *u = &s_units[i];
    if (!units_alive_enemy(i))
        return 0;
    return (u->chill_ticks > 0 ? UNIT_CHILLED : 0) | (u->poison_ticks > 0 ? UNIT_POISONED : 0) |
           (u->stun_ticks > 0 ? UNIT_STUNNED : 0) | (u->curse_ticks > 0 ? UNIT_CURSED : 0) |
           (u->flee_ticks > 0 ? UNIT_SCARED : 0) | (u->slowmis_ticks > 0 ? UNIT_SLOWMIS : 0);
}

int units_enemies_near(float x, float y, float radius, int *out, int max)
{
    int n = 0, c[GRID_CANDS];
    float h = fabsf(radius) + UNIT_RADIUS_MAX - UNIT_RADIUS + GRID_SLACK; /* (in_radius suma el tamaño) */
    int nc = grid_box(x - h, y - h, x + h, y + h, c);
    for (int k = 0; k < (nc >= 0 ? nc : s_num_units) && n < max; k++) {
        int i = nc >= 0 ? c[k] : k;
        if (units_alive_enemy(i) && in_radius(&s_units[i], x, y, radius))
            out[n++] = i;
    }
    return n;
}

int units_enemies_around(float x, float y, float r, int *out, int max)
{
    int n = 0, c[GRID_CANDS], nc = grid_box(x - r - GRID_SLACK, y - r - GRID_SLACK, x + r + GRID_SLACK, y + r + GRID_SLACK, c);
    for (int k = 0; k < nc; k++)
        if (units_alive_enemy(c[k])) {
            if (n == max)
                return -1;
            out[n++] = c[k];
        }
    return nc >= 0 ? n : -1;
}

int units_scare(float x, float y, float radius, int ticks)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        Unit *u = &s_units[i];
        /* Como en el original, los jefes no se asustan */
        if (!units_alive_enemy(i) || u->unique || !in_radius(u, x, y, radius))
            continue;
        if (u->state == ST_ATTACK || u->state == ST_HIT)
            set_state(u, ST_CHASE);
        if (u->snd && u->flee_ticks <= 0)
            unit_sound(u, u->snd->flee, (int)(rnd(u) % 8), -1);
        u->flee_ticks = ticks;
        n++;
    }
    return n;
}

/* Si ya tenía una maldición, se queda con lo peor de cada una */
int units_curse(float x, float y, float radius, const UnitCurse *c)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        Unit *u = &s_units[i];
        if (!units_alive_enemy(i) || !in_radius(u, x, y, radius))
            continue;
        if (u->curse_ticks <= 0)
            u->curse_def_flat = u->curse_def_pct = u->curse_dmg_pct = u->curse_ar_pct = 0;
        if (c->def_flat > u->curse_def_flat) u->curse_def_flat = c->def_flat;
        if (c->def_pct < u->curse_def_pct) u->curse_def_pct = c->def_pct;
        if (c->dmg_pct < u->curse_dmg_pct) u->curse_dmg_pct = c->dmg_pct;
        if (c->ar_pct < u->curse_ar_pct) u->curse_ar_pct = c->ar_pct;
        if (c->ticks > u->curse_ticks) u->curse_ticks = c->ticks;
        n++;
        if (c->single)
            break;
    }
    return n;
}

int units_cursed(int i)
{
    return units_alive_enemy(i) && s_units[i].curse_ticks > 0;
}

void units_scale_hp(int i, int pct)
{
    if (i < 0 || i >= s_num_units)
        return;
    Unit *u = &s_units[i];
    u->max_hp = u->max_hp * pct / 100;
    u->hp = u->max_hp;
}

/* Frenado: pierde `pct` de cada 100 ticks (velocidad y ataque) */
void units_slow(int i, int pct, int ticks)
{
    if (!units_alive_enemy(i))
        return;
    Unit *u = &s_units[i];
    if (u->slow_ticks <= 0)
        u->slow_pct = 0;
    if (ticks > u->slow_ticks)
        u->slow_ticks = ticks;
    if (pct > u->slow_pct)
        u->slow_pct = pct > 90 ? 90 : pct;
}

/* Congelado: no se mueve ni ataca y se ve azul */
void units_freeze(int i, int ticks)
{
    /* (MonsterFreezeDivisor: la mitad en Pesadilla, un cuarto en Infierno) */
    units_stun(i, s_freeze_div > 1 ? (ticks / s_freeze_div > 1 ? ticks / s_freeze_div : 1) : ticks);
    units_chill(i, ticks);
}

int units_slow_missiles(float x, float y, float radius, int pct, int ticks)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        Unit *u = &s_units[i];
        if (!units_alive_enemy(i) || dist_to(u, x, y) > radius)
            continue;
        u->slowmis_pct = pct;
        u->slowmis_ticks = ticks;
        n++;
    }
    return n;
}

void units_stun(int i, int ticks)
{
    if (!units_alive_enemy(i))
        return;
    Unit *u = &s_units[i];
    if (u->state == ST_ATTACK)
        set_state(u, ST_CHASE);
    if (ticks > u->stun_ticks)
        u->stun_ticks = ticks;
}

/* Empuja al monstruo `dist` subtiles alejándolo de (from_x, from_y), si no choca */
void units_knockback(int i, float from_x, float from_y, float dist)
{
    if (!units_alive_enemy(i))
        return;
    Unit *u = &s_units[i];
    float dx = u->x - from_x, dy = u->y - from_y, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f)
        return;
    for (float k = dist; k > 0.1f; k -= 0.25f) {
        float nx = u->x + dx / l * k, ny = u->y + dy / l * k;
        if (s_map && unit_step_ok(s_map, u, nx, ny) && !crowded(u, nx, ny)) {
            u->x = nx;
            u->y = ny;
            grid_moved(u);
            return;
        }
    }
}

int units_find_corpse(float x, float y, float range)
{
    int best = -1;
    float best_d = range * range;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        if (!u->enemy || u->state < ST_DYING || u->searched)
            continue;
        float dx = u->x - x, dy = u->y - y, d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void units_mark_searched(int i)
{
    s_units[i].searched = 1;
}

int units_corpses_near(float x, float y, float radius, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < s_num_units && n < max; i++) {
        const Unit *u = &s_units[i];
        if (!u->enemy || u->state < ST_DYING || u->searched || u->hidden || u->zone == ZONE_FREE)
            continue;
        float dx = u->x - x, dy = u->y - y;
        if (dx * dx + dy * dy <= radius * radius)
            out[n++] = i;
    }
    return n;
}

void units_consume_corpse(int i)
{
    if (i < 0 || i >= s_num_units)
        return;
    s_units[i].searched = 1;
    s_units[i].hidden = 1; /* Redención: el cadáver se va */
}

int units_find_npc(float x, float y, float range)
{
    int best = -1;
    float best_d = range * range;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        if (!u->npc)
            continue;
        float dx = u->x - x, dy = u->y - y, d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

const char *units_id(int i)
{
    return s_units[i].name;
}

void units_talk(int i, float x, float y)
{
    Unit *u = &s_units[i];
    u->talk_ticks = 2 * TICK_RATE;
    u->walking = 0;
    u->anim_acc = 0;
    u->sector = sector_from_world(x - u->x, y - u->y);
}

int units_is_ranged(int i)
{
    return s_units[i].ranged;
}

int units_is_unique(int i)
{
    return s_units[i].unique;
}

int units_count_enemies_in(float x0, float y0, float x1, float y1)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++) {
        const Unit *u = &s_units[i];
        n += units_alive_enemy(i) && u->x >= x0 && u->x < x1 && u->y >= y0 && u->y < y1;
    }
    return n;
}

int units_count_enemies(void)
{
    int n = 0;
    for (int i = 0; i < s_num_units; i++)
        n += units_alive_enemy(i);
    return n;
}

void units_free(void)
{
    memset(s_ehydra, 0, sizeof s_ehydra);
    a5_reset();
    for (int i = 0; i < s_num_gfx; i++)
        for (int k = 0; k < ANIM_COUNT; k++)
            charanim_free(&s_gfx[i].anim[k]);
    memset(s_gfx, 0, sizeof s_gfx);
    s_num_gfx = 0;
    s_num_units = s_free_from = 0;
    s_grid_ok = 0;
    txt_free(&s_objpreset);
    txt_free(&s_monpreset);
    txt_free(&s_objects);
    txt_free(&s_monstats);
    txt_free(&s_monstats2);
    txt_free(&s_monlvl);
    txt_free(&s_superuniques);
    txt_free(&s_monumod);
    txt_free(&s_difflv);
}
