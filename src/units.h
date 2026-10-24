#ifndef UNITS_H
#define UNITS_H

#include "charanim.h"
#include "map.h"

/* NPCs, animales, monstruos y objetos del mapa (fogatas, antorchas,
   waypoint, cofre...). Los que trae cada DS1 se identifican con
   monpreset.txt y objpreset.txt; los gráficos salen de monstats2.txt y
   objects.txt, y la vida/daño/experiencia de monstats.txt y monlvl.txt. */

/* Radio del cuerpo de un jugador para chocar con los monstruos y los NPC
   (con las paredes usa uno más chico, para pasar por las puertas) */
#define UNIT_PLAYER_BODY 0.45f

/* Un jugador visto por los monstruos. Los monstruos suman en `damage` el
   daño que le hacen en el tick. */
typedef struct {
    float x, y;
    int   alive;
    int   in_town; /* en el pueblo no se los ataca */
    int   defense; /* para la probabilidad de que le peguen */
    int   level;
    int   amplified; /* maldecido: % de más del daño físico que recibe (lo de Griswold, 100; Decrepitud, 50) */
    /* Lo que le hacen en el tick */
    int   damage;
    int   curse_ticks;               /* lo maldicen por tantos ticks */
    int   poison_total, poison_ticks; /* veneno (el golpe de Andariel) */
    int   mana_burn;                 /* maná que le queman */
    int   attacker;                  /* el último monstruo que le pegó cuerpo a cuerpo (-1 = ninguno) */
    int   block;                     /* % de bloquear con el escudo (cada golpe que acierta) */
    int   blocked;                   /* golpes que bloqueó en el tick */
    int   res[4];                    /* sus resistencias (fuego, frío, rayo, veneno): para los jefes encantados */
    int   chill_ticks;               /* lo enfriaron (un golpe de frío): más lento */
    int   aura_slow;                 /* % que lo frena un aura (la Congelación sagrada de Duriel) */
    int   aura_conv_def, aura_conv_res; /* la Convicción de un jefe con aura: % de defensa y de resistencias (negativos) */
    int   aura_pierce[3];            /* el aura de un Anciano colosal: % de resistencia al fuego, frío o rayo que le quita */
    float body;                      /* lo que su cuerpo es más ancho que el de un jugador (los gólems) */
    int   life_pct;                  /* % de vida que le queda (los fetiches se alejan si está entero) */
    /* Los del Acto 4 (monai4.inc) */
    int   freeze_ticks;              /* lo congelan (el toque frío de Diablo): ni se mueve ni ataca */
    int   curse, curse_lvl, curse_len; /* A4C_*: lo maldice un Caballero del Olvido (y la que ya tiene) */
    int   mana_drained;              /* le frenan el maná (el abrazo de los Gropers): ticks que le quedan */
    int   still;                     /* un jugador quieto (PLRMODE_NEUTRAL: ni camina ni ataca): la burla de Baal */
    int   more_mana;                 /* un jugador con tanto maná como vida o más (las súcubos le echan Blood Mana) */
    int   player;                    /* 1 = un jugador (los blancos que siguen son mercenarias, señuelos y esbirros) */
    int   shout;                     /* tiene el Grito (Madawc no grita si el jugador ya lo tiene) */
} UnitTarget;

/* Las maldiciones de los monstruos sobre los jugadores (MonCurseCast: las
   seis del Nigromante; y las dos de las súcubos) */
enum { A4C_NONE, A4C_AMPLIFY, A4C_WEAKEN, A4C_IRONMAIDEN, A4C_LIFETAP, A4C_DECREPIFY, A4C_LOWERRES, A4C_DEFENSE, A4C_BLOODMANA, A4C_COUNT };

/* Lo que le dan las auras del Paladín a un aliado (la mercenaria, la
   Valquiria, los convertidos): se pone en cada tick */
typedef struct {
    int dmg_pct, ar_pct, def_pct, ias, speed;
    int res[3], res_max[3]; /* fuego, frío y rayo */
    int thorns, thorns_flat;
} AllyAura;

/* Mercenario de un jugador (las pícaras de Kashya, los guerreros del
   desierto de Greiz): sus valores los calcula game.c con hireling.txt y
   skills.txt */
typedef struct {
    int  level, hp_max, defense, ar;
    int  dmin, dmax;           /* las flechas comunes (o el golpe de lanza) */
    int  melee;                /* pelea cuerpo a cuerpo (el guerrero del desierto) */
    int  jab_chance, jab_dmg_pct, jab_ar_pct; /* la Estocada: % de usarla, daño y puntería que suma */
    char aura[24];             /* el aura de paladín que lleva ("Prayer"; la aplica game.c) */
    int  aura_lvl;
    char skill_missile[32];    /* la de su habilidad ("firearrow", "coldarrow") */
    int  skill_chance;         /* % de usarla */
    int  elem, emin, emax, elen; /* su daño elemental (y cuánto enfría) */
    int  sight_def, sight_ticks; /* Percepción: defensa que les baja a los monstruos */
    int  str, dex, resist;       /* para el equipo y el panel (las cuatro resistencias son iguales) */
    /* Los Lobos de Hierro de Asheara: dos hechizos de la hechicera (Skill1 y Skill2) */
    int  caster;
    char spell_missile[2][24];
    int  spell_chance[2], spell_elem[2], spell_min[2], spell_max[2], spell_len[2];
    int  spell_count[2], spell_flags[2], spell_radius10[2]; /* las chispas de Carga, la explosión, la helada */
} MercStats;

int  units_init(void); /* carga las tablas; 0 si salió bien */

/* Las unidades que se crean entre units_zone_begin y units_zone_end son de
   esa zona y se borran con units_free_zone (los gráficos que ya no use
   nadie se liberan). Las que se fueron caminando a otra zona armada pasan
   a ser de esa (`zone_at` dice de qué zona armada es un lugar, -1 = ninguna). */
void units_zone_begin(int zone);
void units_zone_end(void);
int  units_free_zone(int zone, int (*zone_at)(float x, float y)); /* devuelve cuántas borró */
void units_remove(int i);      /* borra una sola (Caín al empezar otra partida) */
int  units_zone(int i);
/* Los que alumbran (Light de monstats2.txt: Duriel, los magos, las
   mercenarias): posición en subtiles y radio; devuelve cuántos */
int  units_lights(float *x, float *y, int *r, int max);
int  units_visible(int i);     /* 0 = lugar libre o escondida (no se dibuja) */
int  units_gfx_loaded(void);   /* tipos de gráficos cargados (para D2_MEM) */
size_t units_gfx_bytes(void);  /* lo que ocupan */

/* Con el hilo de trabajos andando (jobs.h), los gráficos se decodifican
   ahí: la unidad existe pero no se mueve ni se dibuja hasta que están.
   units_loading dice cuántos tipos faltan. */
int  units_loading(void);

/* Crea las unidades que traen los DS1 recién estampados (m->objects) y
   marca sus colisiones. `level_of(x, y)` dice el nivel de monstruos de cada
   lugar (0 = pueblo). De a partes: desde la `from`, hasta `cap` (0 = sin
   tope) o hasta pasarse `ms` milisegundos (0 = sin hora), por lo menos una;
   devuelve la que sigue (m->num_objects: ya están todas). */
int  units_spawn_presets(Map *m, int (*level_of)(float x, float y), int from, int cap, double ms);

/* Crea un monstruo enemigo de monstats.txt (por ejemplo "fallen1") */
int  units_spawn_monster(const char *id, float x, float y, int level);
int  units_spawn_superunique(const char *name, float x, float y, int level); /* uno de superuniques.txt con su escolta: su unidad, -1 = no */
int  units_monstats_int(const char *id, const char *col, int def); /* una columna de monstats.txt (def si no está) */
void units_set_difficulty(int diff); /* los monstruos que salen de ahora en más (0 Normal, 1 Pesadilla, 2 Infierno) */
int  units_difficulty_int(const char *col, int def); /* una columna de difficultylevels.txt en esa dificultad */
int  units_spawn_npc(const char *id, float x, float y); /* un NPC con el que se habla (Caín en el campamento) */
int  units_spawn_neutral(const char *id, float x, float y, int level); /* ni enemigo ni NPC (los presos del Acto 5) */
void units_walk_to(int i, float x, float y); /* un neutral va hacia ahí (los presos, a su portal) en vez de pasear */

/* Quién sale en los grupos que marcan los DS1 (place_group50, place_champion,
   place_unique_pack): un monstruo de la zona de ese lugar */
void units_set_picker(const char *(*pick)(float x, float y));

void units_tick(const Map *m, UnitTarget *players, int num_players);
int  units_count(void);
void units_position(int i, float alpha, float *x, float *y); /* en subtiles */
void units_draw(int i, int x, int y);                         /* (x, y) = pies en pantalla */
int  units_is_corpse(int i);
int  units_riding(int i); /* un diablillo montado en su bestia de asedio (se dibuja encima) */

/* 1 si en (x, y) hay un NPC, animal o monstruo vivo a menos de `radius`
   + su tamaño; units_blocking_at además devuelve dónde está */
int  units_blocking(float x, float y, float radius);
int  units_blocking_at(float x, float y, float radius, float *ux, float *uy);

/* Posición de la primera unidad con ese nombre (clase de objects.txt o Id
   de monstats.txt, por ejemplo "RogueBonfire" o "akara"); 0 si la encontró */
int  units_find(const char *name, float *x, float *y);
int  units_find_all(const char *name, int *out, int max); /* índices de todas las que se llaman así */
void units_set_lit(int i, int lit); /* waypoints: encendido o apagado */

/* Objetos de las misiones: crear uno por su clase de objects.txt (el
   portal rojo) y cambiarles el modo ("on" encendido, "op" abriéndose; con
   `next`, el modo que sigue cuando termina la animación) */
int  units_spawn_object(const char *cls, float x, float y, const char *mode);
void units_object_mode(int i, const char *mode, const char *next);
void units_object_unblock(Map *m, int i); /* deja de tapar el paso (la puerta de Tyrael cuando muere Duriel) */

/* Los objetos que se usan (cofres, barriles, santuarios: objects.inc). Se
   ponen como los de un DS1 (marcan su colisión); -1 si no se pudo. */
int  units_place_object(Map *m, const char *cls, float x, float y);
int  units_place_object_row(Map *m, int row, float x, float y); /* por su fila de objects.txt (sin buscar la clase) */
int  units_object_row(int i);      /* su fila de objects.txt (0 = no es un objeto) */
int  units_object_cur_mode(int i); /* el modo de ahora: 0 quieto (nu), 1 abriéndose (op), 2 abierto (on) */
int  units_object_state(int i);    /* lo que guarda objects.inc (InteractType del original) */
void units_object_set_state(int i, int state);
void units_object_finish(int i);   /* la animación del modo de ahora, ya terminada */
/* objects.txt: la fila de una clase o de un *ID, y sus columnas */
int         units_objects_find(const char *cls);
int         units_objects_row_of_id(int id);
int         units_objects_int(int row, const char *col, int def);
const char *units_objects_str(int row, const char *col);

/* Combate */
int         units_find_enemy(float x, float y, float range); /* el monstruo vivo más cercano, o -1 */
/* El primero que toca un proyectil de tamaño msize en los subtiles que
   recorrió (cells, en orden; missiles_touches), sin los de skip; -1 si nadie */
int         units_find_enemy_path(const int (*cells)[2], int n, int msize, const int *skip, int nskip);
int         units_foot(int i);               /* su tamaño contra los proyectiles y las paredes (SizeX: 1..3) */
int         units_find_melee(float x, float y, float range); /* igual, para pegarle cuerpo a cuerpo (a los tentáculos, desde más lejos) */
void        units_set_level_query(int (*level_id_at)(float x, float y)); /* el Id de levels.txt de un lugar */
int         units_find_reviver(float x, float y, float range); /* el más cercano que revive, levanta muertos o pare crías */
void        units_set_ancients(int on); /* el rito de los Antiguos empezado: pelean (a5quest.inc) */
int         units_baal_left(void);      /* 1 una vez cuando Baal llegó al portal de la Cámara */
int         units_alive_enemy(int i);
int         units_damage(int i, int amount); /* devuelve la experiencia si lo mató */
void        units_check_anims(void);         /* depuración: avisa las animaciones que faltan */
const char *units_display_name(int i);       /* nombre traducido */
void        units_health(int i, int *hp, int *max_hp);
const char *units_treasure_class(int i);     /* para el botín (treasureclassex.txt) */
void        units_debug_stats(int i, char *buf, size_t cap); /* pruebas: nivel, vida, daño, experiencia, resistencias... */
int         units_level(int i);
float       units_radius(int i);             /* tamaño del cuerpo, en subtiles */
int         units_resist(int i, int elem, int amount); /* el daño que le llega después de su resistencia */
/* Lo mismo bajándole `pierce` puntos de resistencia (Dominio del frío): a
   los inmunes (100 o más) no les cambia nada */
int         units_resist_pierce(int i, int elem, int amount, int pierce);
int         units_defense(int i);
int         units_count_enemies(void);
int         units_count_enemies_in(float x0, float y0, float x1, float y1); /* en subtiles */
int         units_is_unique(int i);
/* Lo que el original muestra debajo del nombre: no muerto o demonio, la clase de campeón o los
   modificadores del jefe y las inmunidades, separados por coma ("" si nada) */
void        units_describe(int i, char *buf, size_t cap);
int         units_is_ranged(int i);          /* dispara de lejos (arqueras, chamanes, ratas de púas) */

/* Habilidades de los jugadores (radio en subtiles, duración en ticks) */
int  units_scare(float x, float y, float radius, int ticks); /* huyen; devuelve cuántos */
int  units_enemies_near(float x, float y, float radius, int *out, int max); /* los monstruos vivos a esa distancia */
/* Los monstruos vivos que pueden estar a menos de r de (x, y): los del
   cuadrado de lado 2r, de menor a mayor (la distancia la mide el que llama);
   -1 si son más de max: ahí hay que mirar todas las unidades */
int  units_enemies_around(float x, float y, float r, int *out, int max);
typedef struct {
    int def_flat; /* defensa que se le resta (Percepción) */
    int def_pct, dmg_pct, ar_pct; /* % de defensa, daño y puntería (negativos) */
    int ticks;
    int single;   /* solo al más cercano (Provocar) */
} UnitCurse;
int  units_curse(float x, float y, float radius, const UnitCurse *c);
int  units_cursed(int i);
void units_stun(int i, int ticks);
void units_slow(int i, int pct, int ticks);    /* Empalar: pierde ese % de velocidad y de ataque */
void units_scale_hp(int i, int pct);           /* la vida máxima al pct% (y llena): la prueba de habilidades */
void units_freeze(int i, int ticks);           /* congelado: quieto y azul */
void units_zealots_flee(int on);               /* el Consejo murió: los fanáticos huyen de los jugadores */
void units_play_death(int i);                  /* cae muerto sin dar nada (el Orbe cautivante) */
int  units_bird_boss_in_zone(int zone);        /* el jefe que puede tener la estatuilla de jade, -1 = ninguno */
int  units_spawn_boss_pack(const char *id, float x, float y, int level); /* jefe al azar y escolta, -1 = no */
int  units_spawn_level_boss(const char *id, float x, float y, int level); /* un campeón (con su grupo) o un único: 1 si salió */
int  units_bosses_in(float x0, float y0, float x1, float y1);  /* los jefes de ese rectángulo (en subtiles) */
int  units_is_champion(int i);
int  units_slow_missiles(float x, float y, float radius, int pct, int ticks); /* sus proyectiles al pct% */
void units_knockback(int i, float from_x, float from_y, float dist);
int  units_find_corpse(float x, float y, float range); /* cadáver sin revisar más cercano, o -1 */
int  units_corpses_near(float x, float y, float radius, int *out, int max); /* los cadáveres sin revisar */
void units_consume_corpse(int i);                       /* Redención: el cadáver desaparece */
int  units_is_undead(int i);
int  units_is_demon(int i);
/* Convicción: baja la defensa (def_pct negativo) y las resistencias al fuego,
   frío y rayo (`res` puntos; a los inmunes, un quinto) */
void units_conviction(int i, int def_pct, int res, int ticks);
int  units_convicted(int i);
/* Conversión: el monstruo pelea para el jugador `owner` por `ticks`; 0 si no se pudo (jefes) */
int  units_convert(int i, int owner, int ticks);
int  units_converted(int i);
int  units_converted_list(int *out, int max); /* los que pelean para un jugador ahora */

/* Aliados (mercenarias y mascotas: la Valquiria, las Hidras, los convertidos) */
int  units_allies(int *out, int max);          /* los vivos que se ven */
int  units_ally_owner(int i);                  /* su jugador (-1 = ninguno) */
void units_ally_aura(int i, const AllyAura *a); /* lo que le dan las auras ahora */
/* Los jefes con aura (el modificador Aura Enchanted) vivos: su número y el SK_* de su aura */
int  units_aura_bosses(int *ids, int *kinds, int max);
/* Un jefe (los Uber del evento Pandemonium): del nivel `level`, y sus esbirros del del área */
int  units_spawn_boss_at(const char *id, float x, float y, int level, int area_level);
int  units_zone_objects(int zone, int *ids, int max); /* los objetos de la zona, en el orden en que se armaron */
void units_set_treasure_class(int i, const char *tc); /* "" = no suelta nada */
/* Las zonas de terror (endgame.inc): `at` da el nivel del terror en un lugar (0 = no está
   aterrorizado), con xp_pct de experiencia de más; el modificador de más de los únicos (mod, peso);
   los elementos (1 << ELEM_*) a los que los amuletos Sunder les rompen la inmunidad */
void units_set_terror(int (*at)(float x, float y), int xp_pct);
void units_terror_pool(const int *mods, const int *weights, int n);
void units_set_sunder(unsigned mask);
int  units_is_terror(int i);
int  units_herald_tier(int i); /* 0 = no es un Heraldo */
typedef struct {
    int tier;                          /* 1..5: HeraldName1..5 */
    int hp_pct, dmg_pct;               /* de más */
    int minions, minion_hp_pct, minion_dmg_pct;
    int mods;                          /* modificadores de jefe (más un aura, siempre) */
    int tc_boost;                      /* niveles de más para su clase de tesoro */
} UnitsHerald;
int  units_spawn_herald(const char *id, float x, float y, int level, const UnitsHerald *h);
/* El Eco del Odio (eco.inc): los monstruos que salen en la zona `zone` (-1 = ninguna) cazan a los
   jugadores (los buscan aunque no los vean, por el mapa de pasos `steps` de w x h subtiles desde
   (x0, y0): 0 donde hay un jugador, 0xffff donde no se llega), tienen el nivel que se les da (también
   los jefes de misión) y hp_pct / dmg_pct de vida y daño de más. units_hunt_deaths: cuántos murieron. */
void units_set_hunt(int zone, int hp_pct, int dmg_pct);
/* En red (y D2_LOCKSTEP): las unidades se mueven aunque sus dibujos no terminaron de cargarse (sin
   red esperan quietas: en la Pi la carga puede tardar medio segundo) */
void units_set_lockstep(int on);
void units_reset_game(uint32_t seed); /* en red: sin unidades de antes y con el mismo azar en todas las máquinas */
/* Los jugadores de la partida: con más, los monstruos que salen tienen más vida y dan más experiencia
   (el original); units_player_count_of: los que había cuando salió ese (para su botín) */
void units_set_player_count(int n);
int  units_player_count_of(int i);
void units_set_flow(const uint16_t *steps, int x0, int y0, int w, int h);
int  units_hunt_deaths(void);
int  units_is_hunter(int i);
int  units_hunters_alive(void);
int  units_hunt_corpses(int keep_ticks, int max_keep); /* sus cadáveres se van (los que tienen más de keep_ticks; tope max_keep) */
void units_hunters_kill(void); /* caen muertos sin dar nada (se terminó la corrida) */
void units_set_tc_of(int i, const char *id); /* la clase de tesoro de otro monstruo de monstats.txt */
/* El Diablo clon: con 1, el próximo superunique que se arme es él; el número del último que salió (-1 = ninguno) */
void units_clone_next_superunique(int on);
int  units_clone_unit(void);
const AllyAura *units_ally_aura_get(int i);
int  units_ally_heal(int i, int amount);       /* Plegaria: 1 si le faltaba vida */
/* La resistencia de la mercenaria a fuego (0), frío (1) o rayo (2), con las auras */
int  units_ally_resist(int i, int e);

/* ---- El Nigromante ---- */

/* Un esbirro: un monstruo de monstats.txt de ese nivel con lo que le suma
   la habilidad (Revivir usa el cadáver tal cual, con lo mismo) */
typedef struct {
    int  level;
    int  hp_pct, hp_add;       /* % de vida y vida de más */
    int  ar_add, ar_pct;       /* puntería */
    int  dmg_add, dmg_pct;     /* daño */
    int  def_add;              /* defensa */
    int  res;                  /* Resistir invocaciones: fuego, frío, rayo y veneno */
    int  speed_pct;            /* velocidad (Dominio de gólems, Revivir) */
    char missile[24];          /* lo que dispara (los magos) */
    int  elem, emin, emax, elen; /* su daño (el veneno, en total) */
    int  ticks;                /* cuánto dura (Revivir, los muros); 0 = siempre */
    int  slow;                 /* gólem de arcilla: % que frena al que golpea */
    int  steal, steal_owner;   /* gólem de sangre: % que roba y % de eso para el dueño */
    int  thorns;               /* gólem de hierro: daño que le devuelve al que lo golpea */
    int  fire_min, fire_max;   /* gólem de fuego: fuego en sus golpes */
    int  wall;                 /* el Muro y la Prisión de hueso: quieto, tapa el paso y lo atacan */
    /* Los del Druida */
    int  still;                /* quieto y sin pelear, pero lo atacan (los espíritus, las enredaderas que comen) */
    int  monlvl;               /* suma la defensa y la puntería de monlvl.txt de su nivel (los lobos, el oso) */
    int  def_pct;              /* % de defensa */
    int  dmg_min, dmg_max;     /* su daño (el de la habilidad): reemplaza el de monstats.txt */
    int  hit_elem, hit_emin, hit_emax, hit_elen; /* frío o veneno en sus golpes (el veneno, en total) */
    int  hits;                 /* golpes que da antes de irse (los cuervos); 0 = sin límite */
    float reach_add;           /* alcance de más (MeleeRng de monstats2: la enredadera venenosa) */
    /* Los del Brujo */
    int  ias;                  /* % de velocidad de ataque (Dominio demoníaco) */
    int  pdr;                  /* % menos de daño físico recibido (Juramento de sangre) */
    int  war_goat;             /* el hombre cabra: el nivel de su invocación (aturde desde el 2, furia 3, frenesí 4, tajo 5) */
} PetSpec;
int  units_pet_summon(int owner, const char *id, float x, float y, const PetSpec *ps); /* -1 si no se pudo */
int  units_revive(int corpse, int owner, const PetSpec *ps); /* 0 si ese cadáver no se puede revivir */
int  units_revivable(int i);    /* un cadáver que Revivir puede levantar */
/* Manto de sombras: les baja la defensa y los ciega (a los jefes no) */
int  units_cloak(int i, int ticks, int def_pct);
int  units_cloaked(int i);
/* El Guerrero de las sombras: su vida, daño, puntería y defensa (los de la
   Asesina), y que se dibuje oscuro */
void units_pet_stats(int i, int hp, int dmin, int dmax, int ar, int def);
void units_pet_boost(int i, int hp_pct, int dmg_pct, int dmg_add); /* más vida y daño (el demonio vinculado) */
/* Los modificadores de jefe en un esbirro (el demonio vinculado): muy fuerte, muy rápido, golpe espectral */
void units_pet_mods(int i, int strong, int fast, int spectral);
/* Atiborrarse: por `ticks`, % de velocidad de ataque, % menos de daño físico, defensa de más,
   vida que regenera (en 1/256 por cuadro, como los objetos) y % de robo de vida */
void units_pet_buff(int i, int ticks, int haste, int pdr, int def, int regen, int steal);
/* Más rápido: actúa un tick de más cada 100/pct (Frenesí, el Sigilo de rencor) */
void units_haste(int i, int pct, int ticks);
/* Marca de la muerte: cada golpe físico o mágico le hace `flat` más */
void units_mark(int i, int flat, int ticks);
/* Enfurecido (Sigilo de rencor): pega un % más y más rápido */
void units_enrage(int i, int dmg_pct, int ias_pct, int ticks);
/* Vincular demonio: -1 si no se puede nunca (los jefes de misión y los de
   nombre propio), 1 si es un jefe (pide más nivel), 0 los comunes */
int  units_bind_rank(int i);
int  units_bind(int i, int owner);     /* como units_convert, pero también a los jefes */
void units_pet_cap_hp(int i, int cap); /* la vida máxima del vinculado, hasta `cap` */
/* Lo del Brujo en un esbirro que no invocó (el vinculado): velocidad de ataque,
   % menos de daño físico, resistencias, defensa y puntería de más */
void units_pet_bonus(int i, int ias, int pdr, int res, int def_add, int ar_add);
void units_pet_shadow(int i);
/* Lo pone en ese lugar (el Centinela de cuchillas va y viene solo) */
void units_pet_place(int i, float x, float y);
void units_place(int i, float x, float y); /* cualquiera (Hratli, al ir a su fragua) */
/* Los esbirros que los monstruos pueden atacar (la Valquiria, los
   convertidos, los del Nigromante): no las torretas */
int  units_pet_targets(int *out, int max);
int  units_pet_thorns(int i);  /* el gólem de hierro */
int  units_pet_wall(int i);
/* Lo que el gólem de sangre le pasa a su dueño (vida) */
int  units_pop_owner_heal(int *owner, int *amount);

/* Las maldiciones (una a la vez: la nueva reemplaza a la anterior) */
enum { NC_NONE, NC_AMPLIFY, NC_DIM, NC_WEAKEN, NC_IRONMAIDEN, NC_TERROR, NC_CONFUSE, NC_LIFETAP, NC_ATTRACT,
       NC_DECREPIFY, NC_LOWERRES };
typedef struct {
    int kind, ticks, owner;  /* owner: el jugador que la puso (la experiencia de lo que matan) */
    int phys;                /* damageresist que les baja (Amplificar daño 100, Decrepitud 50) */
    int dmg;                 /* % de daño (negativo: Debilitar, Decrepitud) */
    int slow;                /* % que los frena (Decrepitud) */
    int res;                 /* resistencias que les baja (Bajar resistencias) */
    int im;                  /* Doncella de hierro: % del daño que se les vuelve */
    int lt;                  /* Robar vida: % del daño que recibe que cura al que pega */
} NecCurse;
int  units_nec_curse(int i, const NecCurse *c); /* 1 si se la puso (los jefes no se asustan ni se confunden) */
/* Atraer: los de alrededor atacan a `i` */
int  units_nec_attract(int i, float radius, int ticks);
int  units_nec_cursed(int i);  /* NC_* (0 = ninguna) */
int  units_lifetap(int i);     /* % de Robar vida (0 = no) */
void units_chill(int i, int ticks);                     /* frío: va a la mitad de velocidad */
void units_heal(int i, int hp);                         /* le suma vida, hasta la que tiene de máximo */
int  units_poison(int i, int total, int ticks, int owner); /* veneno: `total` de daño en `ticks`; 1 si lo envenenó */
int  units_bleed(int i, int total, int ticks, int owner);  /* heridas abiertas: lo mismo, daño físico */
/* El Profanador del Brujo: une las almas de los monstruos de un grupo; lo
   que recibe uno, `pct`% lo reciben los demás del grupo (si los mata, la
   experiencia es de `owner`) */
void units_link(int i, int group, int pct, int owner, int ticks);
int  units_linked(int i); /* el grupo (0 = ninguno) */
/* Monstruos que mató el veneno, para darle la experiencia a `owner` (UNITS_KILL_ALLY:
   los mató un aliado sin dueño, un bárbaro del Acto 5: sueltan lo suyo y no hay experiencia) */
#define UNITS_KILL_ALLY (-2)
int  units_pop_kill(int *unit, int *owner, int *exp);
enum { UNIT_CHILLED = 1, UNIT_POISONED = 2, UNIT_STUNNED = 4, UNIT_CURSED = 8, UNIT_SCARED = 16, UNIT_SLOWMIS = 32 };
int  units_status(int i); /* UNIT_* (para teñirlos al dibujar) */
void units_mark_searched(int i);

/* NPCs del pueblo */
int         units_find_npc(float x, float y, float range); /* el más cercano, o -1 */
const char *units_id(int i);                 /* Id de monstats.txt ("akara") o clase del objeto */
void        units_talk(int i, float x, float y); /* se frena un rato y mira hacia (x, y) */

/* Mercenarias. Los jugadores van primero en el arreglo de units_tick, así
   que `owner` es también el índice de su dueño ahí. */
int  units_merc_spawn(int owner, const char *id, float x, float y); /* id de monstats: "roguehire", "act2hire" */ /* la del jugador (la crea o la reusa) */
void units_merc_set(int i, const MercStats *s, int hp, const char *name);
void units_merc_hide(int i);                  /* sin contratar: no está en el mapa */
void units_merc_hide_all(void);               /* al empezar una partida */
int  units_merc_alive(int i);
void units_merc_health(int i, int *hp, int *max_hp);
void units_merc_damage(int i, int amount);
void units_merc_warp(int i, float x, float y); /* aparece al lado (waypoint, revivir) */

/* La Valquiria: un aliado que sigue a su dueño y pelea cuerpo a cuerpo, con
   lo de un monstruo de monstats.txt de ese nivel más los % de la habilidad.
   Va en el arreglo de units_tick después de las mercenarias, como blanco. */
int  units_pet_spawn(int owner, const char *id, float x, float y, int level, int hp_pct, int ar_pct, int dmg_pct);
int  units_pet_alive(int i);
void units_pet_damage(int i, int amount);
void units_pet_remove(int i);
/* Dibujarla con un personaje: la Valquiria del original es la Amazona con
   su equipo (los gráficos de monsters/vk son de relleno) */
void units_pet_set_look(int i, const CharLook *look, const char *wclass);
/* Una torreta (la Hidra): no se mueve, no la atacan y en vez de pegar
   dispara; cada disparo queda en una cola que lee game.c (el proyectil y su
   daño son de la habilidad). cooldown: ticks entre disparos. */
void units_pet_turret(int i, int cooldown);
int  units_pop_shot(int *unit, int *target);
/* El aspecto de un monstruo de monstats.txt (las formas del Druida: "wolf",
   "bear"): sus piezas y su clase de arma. 0 si está. */
int  units_monster_look(const char *id, CharLook *look, char *wclass, size_t cap);
/* Lo que le queda de vida a un esbirro (las enredaderas que comen cadáveres: la del Druida) */
int  units_pet_hp(int i, int *max);
/* Lo que lleva ese monstruo en esa parte ("rarm", "tors", "head"...) a ese
   nivel según monequip.txt: la fila de nivel más alto que no lo pase. 0 si nada. */
int  units_monequip(const char *monster, int level, const char *loc, char *code, size_t cap);

void units_free(void);

#endif
