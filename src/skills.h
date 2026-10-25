#ifndef SKILLS_H
#define SKILLS_H

#include <stddef.h>

/* Habilidades de cada clase (skills.txt y skilldesc.txt): dónde
   van en el árbol, requisitos, maná y las fórmulas de cada nivel. Lo que
   hace cada una lo resuelve game.c según `kind`. */

#define MAX_CLASS_SKILLS 30
#define SKILL_MAX_LEVEL  20
#define SKILL_TABS       3

/* Las que el motor sabe usar; el resto se ve en el árbol pero no se puede aprender */
enum {
    SK_NONE,
    /* Bárbaro */
    SK_BASH,          /* Porrazo: más daño y empuja */
    SK_STUN,          /* Aturdir */
    SK_CONCENTRATE,   /* Concentración */
    SK_HOWL,          /* Aullido: los monstruos huyen */
    SK_SHOUT,         /* Grito: más defensa para el grupo */
    SK_TAUNT,         /* Provocar: un monstruo pega menos */
    SK_BATTLE_CRY,    /* Grito de batalla: menos defensa y daño de los monstruos */
    SK_FIND_POTION,   /* Buscar poción en un cadáver */
    SK_FIND_ITEM,     /* Buscar objeto en un cadáver */
    SK_MASTERY,       /* Dominios de armas (pasivas) */
    SK_INC_STAMINA,   /* Aumentar estamina (pasiva) */
    SK_IRON_SKIN,     /* Piel de hierro (pasiva) */
    /* Amazona */
    SK_JAB,           /* Estocada: tres golpes seguidos */
    SK_IMPALE,        /* Empalar */
    SK_POWER_STRIKE,  /* Golpe poderoso: daño de rayo */
    SK_INNER_SIGHT,   /* Percepción: baja la defensa de los monstruos */
    SK_CRITICAL_STRIKE, /* Golpe crítico (pasiva) */
    SK_DODGE,         /* Esquivar (pasiva) */
    SK_PENETRATE,     /* Penetrar (pasiva) */
    /* Amazona, con proyectiles */
    SK_MAGIC_ARROW,   /* Flecha mágica: no gasta flechas */
    SK_FIRE_ARROW,
    SK_COLD_ARROW,    /* enfría: el monstruo va más lento */
    SK_MULTIPLE_SHOT, /* varias flechas en abanico */
    SK_EXPLODING_ARROW, /* explota con daño de fuego en área */
    SK_ICE_ARROW,     /* congela */
    SK_GUIDED_ARROW,  /* busca al monstruo y no falla */
    SK_POISON_JAVELIN, /* deja una estela de veneno */
    SK_LIGHTNING_BOLT, /* la jabalina se vuelve un rayo que atraviesa */
    SK_PLAGUE_JAVELIN, /* nube de veneno al chocar */
    /* Bárbaro: las que faltaban */
    SK_LEAP,          /* Salto: cae lejos y empuja a los de alrededor */
    SK_DOUBLE_SWING,  /* Doble golpe: con dos armas, un golpe con cada una */
    SK_DOUBLE_THROW,  /* Doble lanzamiento: lanza las dos armas */
    SK_LEAP_ATTACK,   /* Salto de ataque: cae sobre un monstruo y le pega */
    SK_FRENZY,        /* Frenesí: dos golpes; cada acierto lo acelera */
    SK_WHIRLWIND,     /* Torbellino: gira hasta un lugar pegándole a todos */
    SK_BERSERK,       /* Furia: daño mágico, la defensa queda en 0 un rato */
    SK_INC_SPEED,     /* Aumentar velocidad (pasiva) */
    SK_NAT_RESIST,    /* Resistencia natural (pasiva) */
    SK_BATTLE_ORDERS, /* Órdenes de batalla: más vida, maná y estamina al grupo */
    SK_GRIM_WARD,     /* Guardia siniestra: un tótem en un cadáver que espanta */
    SK_WAR_CRY,       /* Grito de guerra: daño y aturde a los de alrededor */
    SK_BATTLE_COMMAND, /* Comando de batalla: +1 a las habilidades del grupo */
    /* Amazona: las que faltaban */
    SK_CHARGED_STRIKE, /* Golpe cargado: suelta rayos cargados */
    SK_FEND,          /* Rechazo: le pega a todos los de alrededor */
    SK_LIGHTNING_STRIKE, /* Golpe de rayo: rayo en cadena */
    SK_LIGHTNING_FURY, /* Furia de rayo: la jabalina suelta rayos al chocar */
    SK_SLOW_MISSILES, /* Ralentizar proyectiles */
    SK_AVOID,         /* Evitar: esquiva proyectiles (pasiva) */
    SK_DECOY,         /* Señuelo: una copia que distrae a los monstruos */
    SK_EVADE,         /* Evadir: esquiva al moverse (pasiva) */
    SK_VALKYRIE,      /* Valquiria: invoca una guerrera */
    SK_PIERCE,        /* Perforar: las flechas atraviesan (pasiva) */
    SK_STRAFE,        /* Ametrallar: una flecha a cada monstruo cercano */
    SK_IMMOLATION_ARROW, /* Flecha de inmolación: explota y deja fuego */
    SK_FREEZING_ARROW, /* Flecha congelante: explota y congela */
    /* Hechicera: fuego */
    SK_FIRE_BOLT,     /* Descarga ígnea */
    SK_WARMTH,        /* Calidez: el maná se recupera más rápido (pasiva) */
    SK_INFERNO,       /* Infierno: un chorro de llamas */
    SK_BLAZE,         /* Llamarada: deja fuego por donde camina */
    SK_FIRE_BALL,     /* Bola de fuego: explota */
    SK_FIRE_WALL,     /* Muro de fuego */
    SK_ENCHANT,       /* Encantar: daño de fuego en los golpes (ella o un compañero) */
    SK_METEOR,        /* Meteoro: cae del cielo y deja fuego */
    SK_FIRE_MASTERY,  /* Dominio del fuego: más daño de fuego (pasiva) */
    SK_HYDRA,         /* Hidra: tres cabezas que escupen fuego */
    /* Hechicera: rayos */
    SK_CHARGED_BOLT,  /* Descarga cargada: chispas que zigzaguean */
    SK_STATIC_FIELD,  /* Campo estático: le saca un % de la vida a los de alrededor */
    SK_TELEKINESIS,   /* Telequinesis: empuja y daña, o levanta cosas de lejos */
    SK_NOVA,          /* Nova: un anillo de rayos */
    SK_LIGHTNING,     /* Relámpago: un rayo que atraviesa */
    SK_CHAIN_LIGHTNING, /* Rayo en cadena: salta de un monstruo a otro */
    SK_TELEPORT,      /* Teletransporte */
    SK_THUNDER_STORM, /* Tormenta: un rayo cae sobre un monstruo cercano cada tanto */
    SK_ENERGY_SHIELD, /* Escudo de energía: el maná absorbe parte del daño */
    SK_LIGHTNING_MASTERY, /* Dominio del rayo: más daño de rayo (pasiva) */
    /* Hechicera: frío */
    SK_ICE_BOLT,      /* Descarga de hielo: enfría */
    SK_FROZEN_ARMOR,  /* Armadura helada: congela al que le pega */
    SK_FROST_NOVA,    /* Nova de escarcha */
    SK_ICE_BLAST,     /* Explosión de hielo: congela */
    SK_SHIVER_ARMOR,  /* Armadura de escalofrío: daña y enfría al que le pega */
    SK_GLACIAL_SPIKE, /* Púa glacial: explota y congela alrededor */
    SK_BLIZZARD,      /* Ventisca: caen trozos de hielo en una zona */
    SK_CHILLING_ARMOR, /* Armadura gélida: devuelve un proyectil de hielo al que dispara */
    SK_FROZEN_ORB,    /* Orbe congelado: escupe descargas de hielo y termina en una nova */
    SK_COLD_MASTERY,  /* Dominio del frío: baja la resistencia al frío de los monstruos (pasiva) */
    /* Paladín: combate */
    SK_SACRIFICE,     /* Sacrificio: más daño, pero se lastima un poco */
    SK_SMITE,         /* Castigar: golpe con el escudo que no falla y aturde */
    SK_HOLY_BOLT,     /* Rayo sagrado: daña a no muertos y demonios, cura a los aliados */
    SK_ZEAL,          /* Celo: varios golpes rápidos */
    SK_CHARGE,        /* Carga: corre hasta el monstruo y le pega */
    SK_VENGEANCE,     /* Venganza: daño de fuego, frío y rayo en el golpe */
    SK_BLESSED_HAMMER, /* Martillo bendito: un martillo que gira en espiral */
    SK_CONVERSION,    /* Conversión: el monstruo pelea de su lado un rato */
    SK_HOLY_SHIELD,   /* Escudo sagrado: más defensa, bloqueo y daño de Castigar */
    SK_FIST_HEAVENS,  /* Puño de los cielos: un rayo del cielo y rayos sagrados */
    /* Paladín: auras ofensivas */
    SK_MIGHT,         /* Poder: más daño */
    SK_HOLY_FIRE,     /* Fuego sagrado: quema a los de alrededor y suma fuego a los golpes */
    SK_THORNS,        /* Espinas: el que pega recibe parte del daño */
    SK_BLESSED_AIM,   /* Puntería bendita */
    SK_CONCENTRATION, /* Concentración: más daño (y el del Martillo) */
    SK_HOLY_FREEZE,   /* Congelación sagrada: frío y frena a los de alrededor */
    SK_HOLY_SHOCK,    /* Descarga sagrada: rayo a los de alrededor */
    SK_SANCTUARY,     /* Santuario: daña y empuja a los no muertos */
    SK_FANATICISM,    /* Fanatismo: más velocidad de ataque, puntería y daño */
    SK_CONVICTION,    /* Convicción: baja la defensa y las resistencias de los monstruos */
    /* Paladín: auras defensivas */
    SK_PRAYER,        /* Plegaria: cura */
    SK_RESIST_FIRE,
    SK_DEFIANCE,      /* Desafío: más defensa */
    SK_RESIST_COLD,
    SK_CLEANSING,     /* Purificación: el veneno y las maldiciones duran menos */
    SK_RESIST_LIGHTNING,
    SK_VIGOR,         /* Vigor: más velocidad y estamina */
    SK_MEDITATION,    /* Meditación: el maná se recupera más rápido */
    SK_REDEMPTION,    /* Redención: los cadáveres dan vida y maná */
    SK_SALVATION,     /* Salvación: todas las resistencias */
    /* Nigromante: maldiciones, huesos y veneno, invocaciones */
    SK_AMPLIFY_DAMAGE,    /* Amplificar daño: los monstruos reciben el doble de daño físico */
    SK_DIM_VISION,        /* Visión reducida: no ven más allá de lo que tienen al lado */
    SK_WEAKEN,            /* Debilitar: pegan menos */
    SK_IRON_MAIDEN,       /* Doncella de hierro: el daño que hacen se les vuelve */
    SK_TERROR,            /* Terror: huyen */
    SK_CONFUSE,           /* Confundir: se atacan entre ellos */
    SK_LIFE_TAP,          /* Robar vida: el que les pega se cura */
    SK_ATTRACT,           /* Atraer: los demás atacan a ese */
    SK_DECREPIFY,         /* Decrepitud: lentos, débiles y reciben más daño */
    SK_LOWER_RESIST,      /* Bajar resistencias */
    SK_TEETH,             /* Dientes: un abanico de dientes mágicos */
    SK_BONE_ARMOR,        /* Armadura de hueso: absorbe el daño cuerpo a cuerpo */
    SK_POISON_DAGGER,     /* Daga venenosa (con daga) */
    SK_CORPSE_EXPLOSION,  /* Explosión de cadáver */
    SK_BONE_WALL,         /* Muro de hueso */
    SK_POISON_EXPLOSION,  /* Explosión venenosa: un cadáver suelta veneno */
    SK_BONE_SPEAR,        /* Lanza de hueso: atraviesa */
    SK_BONE_PRISON,       /* Prisión de hueso: un muro alrededor del monstruo */
    SK_POISON_NOVA,       /* Nova venenosa */
    SK_BONE_SPIRIT,       /* Espíritu de hueso: busca al monstruo */
    SK_SKELETON_MASTERY,  /* Dominio de esqueletos (pasiva) */
    SK_RAISE_SKELETON,    /* Levantar esqueleto (de un cadáver) */
    SK_CLAY_GOLEM,        /* Gólem de arcilla: frena a los que golpea */
    SK_GOLEM_MASTERY,     /* Dominio de gólems (pasiva) */
    SK_RAISE_MAGE,        /* Levantar mago esqueleto */
    SK_BLOOD_GOLEM,       /* Gólem de sangre: roba vida y le pasa una parte */
    SK_SUMMON_RESIST,     /* Resistir invocaciones (pasiva) */
    SK_IRON_GOLEM,        /* Gólem de hierro: de un objeto de metal; devuelve el daño */
    SK_FIRE_GOLEM,        /* Gólem de fuego: con Fuego sagrado */
    SK_REVIVE,            /* Revivir: el monstruo muerto pelea de su lado un rato */
    /* Asesina: artes marciales (cargas y golpes finales), sombras, trampas */
    SK_FIRE_TRAUMA,       /* Trauma de fuego: una bomba que explota */
    SK_CLAW_MASTERY,      /* Dominio de garras (pasiva) */
    SK_PSYCHIC_HAMMER,    /* Martillo psíquico: daño mágico y empuja */
    SK_TIGER_STRIKE,      /* Golpe del tigre: carga daño */
    SK_DRAGON_TALON,      /* Talón del dragón: patadas (golpe final) */
    SK_SHOCK_FIELD,       /* Campo de choque: rayos en el piso */
    SK_BLADE_SENTINEL,    /* Centinela de cuchillas: va y viene */
    SK_QUICKNESS,         /* Rapidez: corre y pega más rápido */
    SK_FISTS_OF_FIRE,     /* Puños de fuego: carga fuego */
    SK_DRAGON_CLAW,       /* Garra del dragón: dos garras (golpe final) */
    SK_CB_SENTRY,         /* Centinela de centellas (trampa) */
    SK_WAKE_OF_FIRE,      /* Centinela de estela de fuego (trampa) */
    SK_WEAPON_BLOCK,      /* Bloqueo con armas (pasiva) */
    SK_CLOAK_SHADOWS,     /* Manto de sombras: los ciega */
    SK_COBRA_STRIKE,      /* Golpe de la cobra: carga robo de vida y maná */
    SK_BLADE_FURY,        /* Furia de cuchillas: las tira seguidas */
    SK_FADE,              /* Desvanecerse: resistencias */
    SK_SHADOW_WARRIOR,    /* Guerrero de las sombras */
    SK_CLAWS_THUNDER,     /* Garras del trueno: carga rayo */
    SK_DRAGON_TAIL,       /* Cola del dragón: patada que explota (golpe final) */
    SK_LIGHTNING_SENTRY,  /* Centinela de relámpagos (trampa) */
    SK_INFERNO_SENTRY,    /* Centinela infernal (trampa) */
    SK_MIND_BLAST,        /* Explosión mental: aturde y convierte */
    SK_BLADES_ICE,        /* Cuchillas de hielo: carga frío */
    SK_DRAGON_FLIGHT,     /* Vuelo del dragón: salta y patea (golpe final) */
    SK_DEATH_SENTRY,      /* Centinela de la muerte (trampa) */
    SK_BLADE_SHIELD,      /* Escudo de cuchillas */
    SK_VENOM,             /* Veneno en los golpes */
    SK_SHADOW_MASTER,     /* Maestro de las sombras */
    SK_ROYAL_STRIKE,      /* Golpe real: carga meteoro, rayos y hielo */
    /* Druida: invocaciones, formas (lobo y oso) y elementales */
    SK_RAVEN,             /* Cuervo: pájaros que picotean y ciegan */
    SK_POISON_CREEPER,    /* Enredadera venenosa */
    SK_WEREWOLF,          /* Hombre lobo: se transforma */
    SK_LYCANTHROPY,       /* Licantropía: más vida y duración de las formas (pasiva) */
    SK_FIRESTORM,         /* Tormenta de fuego: llamas por el piso */
    SK_OAK_SAGE,          /* Sabio roble: espíritu que da vida */
    SK_SPIRIT_WOLF,       /* Lobo espiritual */
    SK_WEREBEAR,          /* Hombre oso: se transforma */
    SK_MOLTEN_BOULDER,    /* Roca fundida: rueda, empuja y deja fuego */
    SK_ARCTIC_BLAST,      /* Ráfaga ártica: un chorro de frío */
    SK_CARRION_VINE,      /* Enredadera carroñera: come cadáveres y la cura */
    SK_FERAL_RAGE,        /* Furia salvaje: cada golpe la acelera y roba vida */
    SK_MAUL,              /* Vapulear: cada golpe suma daño y aturde */
    SK_FISSURE,           /* Fisura: grietas de fuego */
    SK_CYCLONE_ARMOR,     /* Armadura de ciclón: absorbe fuego, frío y rayo */
    SK_HEART_WOLVERINE,   /* Corazón de lobezno: espíritu que da daño y puntería */
    SK_DIRE_WOLF,         /* Lobo terrible */
    SK_RABIES,            /* Rabia: un mordisco venenoso que contagia */
    SK_FIRE_CLAWS,        /* Garras de fuego */
    SK_TWISTER,           /* Remolino: aturde */
    SK_SOLAR_CREEPER,     /* Enredadera solar: come cadáveres y le da maná */
    SK_HUNGER,            /* Hambre: un mordisco que roba vida y maná */
    SK_SHOCK_WAVE,        /* Onda de choque: aturde a los de adelante */
    SK_VOLCANO,           /* Volcán: escupe rocas de fuego */
    SK_TORNADO,           /* Tornado */
    SK_SPIRIT_BARBS,      /* Espíritu de púas: devuelve el daño */
    SK_GRIZZLY,           /* Oso pardo */
    SK_FURY,              /* Furia: varios golpes seguidos */
    SK_ARMAGEDDON,        /* Armagedón: caen rocas de fuego alrededor */
    SK_HURRICANE,         /* Huracán: frío alrededor */
    /* Brujo (D2R): demonios, maleficios del arma y magia del caos */
    SK_SUMMON_GOATMAN,    /* Invocar hombre cabra */
    SK_DEMONIC_MASTERY,   /* Dominio demoníaco (pasiva) */
    SK_DEATH_MARK,        /* Marca de la muerte: el demonio salta al enemigo */
    SK_SUMMON_TAINTED,    /* Invocar mancillado: tira bolas de fuego */
    SK_SUMMON_DEFILER,    /* Invocar profanador: une almas */
    SK_BLOOD_OATH,        /* Juramento de sangre (pasiva): el demonio recibe parte del daño */
    SK_ENGORGE,           /* Atiborrarse: el demonio come un cadáver */
    SK_BLOOD_BOIL,        /* Sangre hirviente: los demonios explotan */
    SK_CONSUME,           /* Consumir: sacrifica un demonio */
    SK_BIND_DEMON,        /* Vincular demonio: uno de los enemigos pasa a ser suyo */
    SK_LEVITATE,          /* Dominio de la levitación (pasiva) */
    SK_ELDRITCH_BLAST,    /* Explosión sobrenatural: a los embrujados */
    SK_HEX_BANE,          /* Maleficio: flagelo */
    SK_HEX_SIPHON,        /* Maleficio: extracción */
    SK_PSYCHIC_WARD,      /* Resguardo psíquico: absorbe daño */
    SK_ECHOING_STRIKE,    /* Golpe de eco: ecos del arma que atraviesan */
    SK_HEX_PURGE,         /* Maleficio: purga */
    SK_BLADE_WARP,        /* Hoja de distorsión: se teletransporta donde pega */
    SK_CLEAVE,            /* Tajo: a todos en un arco */
    SK_MIRRORED_BLADES,   /* Hojas reflejadas */
    SK_SIGIL_LETHARGY,    /* Sigilo: letargo */
    SK_RING_OF_FIRE,      /* Anillo de fuego */
    SK_MIASMA_BOLT,       /* Descarga de miasma */
    SK_SIGIL_RANCOR,      /* Sigilo: rencor */
    SK_ENHANCED_ENTROPY,  /* Entropía mejorada (pasiva) */
    SK_FLAME_WAVE,        /* Ola de llamas */
    SK_MIASMA_CHAINS,     /* Cadena de miasma */
    SK_SIGIL_DEATH,       /* Sigilo: muerte */
    SK_APOCALYPSE,        /* Hecatombe */
    SK_ABYSS,             /* Abismo */
    SK_KIND_COUNT
};

/* Las fórmulas de skills.txt tal cual (las evalúa skill_calc) */
enum {
    SC_CALC1, SC_CALC2, SC_CALC3, SC_CALC4,
    SC_AURALEN,     /* duración del estado (auralencalc) */
    SC_AURARANGE,   /* alcance (aurarangecalc) */
    SC_AURASTAT1, SC_AURASTAT2, SC_AURASTAT3,
    SC_PASSIVE1, SC_PASSIVE2, SC_PASSIVE3,
    SC_EDMGSYM,     /* % más de daño elemental por las sinergias */
    SC_ELENSYM,     /* % más de duración del frío o el aturdimiento */
    SC_DMGSYM,      /* % más del daño propio */
    SC_PASSIVE4, SC_PASSIVE5, /* las de los gólems (sinergias) */
    SC_AURASTAT4,   /* el Hombre lobo: la vida (aurastatcalc4) */
    SC_COUNT
};

typedef struct {
    char        key[32];   /* nombre en skills.txt ("Bash") */
    const char *name;      /* traducido ("Porrazo") */
    const char *flavor;    /* descripción del juego, con saltos de línea */
    int         kind;
    int         passive;
    int         page, row, col; /* solapa 1..3, fila 1..6 y columna 1..3 del árbol */
    int         icon;      /* cuadro en <clase>skillicon.dc6 */
    int         reqlevel;
    int         req[3];    /* habilidades previas (índice en la clase), -1 = ninguna */
    int         mana, lvlmana, manashift, minmana;
    int         tohit, levtohit;
    int         emin, emax, eminlev[5], emaxlev[5];
    int         hitshift;  /* el daño elemental está en 1/256 << hitshift */
    int         elen, elevlen[3]; /* duración del frío o el veneno, en ticks */
    int         mindam, maxdam, minlevdam[5], maxlevdam[5]; /* daño propio (Flecha mágica) */
    int         srcdam;    /* % del daño del arma, en 1/128 (Disparo múltiple: 96 = 75%) */
    int         decquant;  /* gasta una flecha o una jabalina */
    char        missile[32]; /* proyectil que tira (missiles.txt) */
    char        missile_b[32], missile_c[32]; /* srvmissileb y srvmissilec */
    char        formula[SC_COUNT][128];
    char        aurastat[4][40], passivestat[5][40]; /* qué cambian los estados y las pasivas */
    int         restrict_form; /* el Druida: 1 = se usa transformado, 2 = solo en la forma de state1/state2 */
    char        state1[12], state2[12]; /* "wolf", "bear" */
    int         aura;      /* es un aura del Paladín (aura de skills.txt) */
    int         passive_always; /* sus pasivas valen siempre (passivestate), no solo con el aura puesta */
    int         perdelay;  /* auras: cada cuántos cuadros hacen lo suyo */
    char        anim[4];   /* modo de la animación (A1, SC, TH, SQ...) */
    int         seqnum;    /* secuencia del original (sequences.h), 0 = ninguna */
    char        castoverlay[32];
    char        srvoverlay[32]; /* el efecto sobre el que golpea (Concentración, Salto ofensivo) */
    char        summon[24];
    char        petmax[128];  /* cuántos puede tener (fórmula) */
    char        sumskcalc[64]; /* el nivel de la habilidad del esbirro (sumsk1calc) */
    int         intown;    /* se puede usar en el pueblo */
    int         delay;     /* localdelay: cuadros hasta poder usarla otra vez */
    int         gdelay;    /* globaldelay: la misma demora para todas las que la tienen (las transformaciones del Druida) */
    int         param[15]; /* Param1..Param14 (el 0 no se usa; del 9 en adelante, los del Brujo de D2R) */
    char        passive_itype[8];  /* pasivas que dependen del arma ("axe", "blde") */
    char        weapon_itype[3][8]; /* armas con las que se puede usar ("" = cualquiera) */
    char        weapon_itypeb[8];   /* lo que pide en la otra mano (Garra del dragón: otra garra) */
    char        etype[5];  /* el elemento (EType): "fire", "cold"... (las de "+N a las habilidades de fuego") */
    int         global_id; /* su fila en skills.txt (*Id): la de las propiedades de los objetos */
    /* Sonidos al lanzarla (sounds.txt, -1 = nada): el de la habilidad, el
       de la voz de la clase y si además suena el arma */
    int         st_sound, st_sound_class, weapon_sound;
} SkillDef;

int             skills_init(void);
int             skills_count(int cls);           /* 0 = Bárbaro, 1 = Amazona, 2 = Hechicera, 3 = Paladín, 4 = Nigromante, 5 = Asesina, 6 = Druida, 7 = Brujo */
const SkillDef *skills_get(int cls, int i);
int             skills_find(int cls, const char *key); /* -1 si no está */
const char     *skills_tab_name(int cls, int page);    /* "Combate", "Gritos de guerra"... */
const char     *skills_icon_file(int cls);             /* "baskillicon" */

/* Todas las habilidades de skills.txt por su *Id (las de los objetos: "+3 a
   Teletransporte", las cargas, las auras), también las de los monstruos */
int         skills_global_find(const char *name); /* por el nombre ("Teleport") o el número; -1 si no está */
const char *skills_global_name(int id);           /* traducido ("Teletransporte"), "" si no hay */
int         skills_global_class(int id);          /* la clase del motor (0..6), -1 si no es de ninguna */
int         skills_global_reqlevel(int id);
int         skills_global_local(int id);          /* su índice en la clase (skills_get), -1 si no está en el árbol */

/* Fórmulas de skills.txt. lnXY = ParX + (nivel - 1) * ParY; dmXY crece cada
   vez menos con el nivel (de ParX hacia ParY). */
int   skill_ln(const SkillDef *s, int a, int b, int lvl);
int   skill_dm(const SkillDef *s, int a, int b, int lvl);
int   skill_mana(const SkillDef *s, int lvl);   /* maná que cuesta */
int   skill_tohit(const SkillDef *s, int lvl);  /* % de puntería extra */
void  skill_elem(const SkillDef *s, int lvl, int *min, int *max); /* daño elemental (por tick si es veneno) */
void  skill_phys(const SkillDef *s, int lvl, int *min, int *max); /* daño propio de la habilidad */
int   skill_elen(const SkillDef *s, int lvl);  /* duración del frío o del veneno, en ticks */
void  skill_poison_total(const SkillDef *s, int lvl, int *min, int *max); /* veneno en toda la duración */
int   skill_missiles(const SkillDef *s, int lvl); /* cuántos proyectiles tira (Disparo múltiple) */

/* Las fórmulas del original, con las sinergias: skill('Bash'.blvl) son los
   puntos puestos en esa habilidad (`blvl`, por índice de la clase) */
typedef struct {
    int        cls;
    const int *blvl;
    int        ulvl;  /* el nivel del personaje (el de los esbirros del Druida) */
} SkillCtx;
int   skill_has(const SkillDef *s, int which);
int   skill_calc(const SkillDef *s, int which, int lvl, const SkillCtx *c); /* 0 si no tiene esa fórmula */
int   skill_eval(const SkillDef *s, const char *expr, int lvl, const SkillCtx *c);
/* Daño elemental, duración y daño propio con el % de las sinergias */
void  skill_elem_syn(const SkillDef *s, int lvl, const SkillCtx *c, int *min, int *max);
int   skill_elen_syn(const SkillDef *s, int lvl, const SkillCtx *c);
void  skill_phys_syn(const SkillDef *s, int lvl, const SkillCtx *c, int *min, int *max);
void  skill_poison_syn(const SkillDef *s, int lvl, const SkillCtx *c, int *min, int *max);
/* El daño elemental de las que queman (HitShift chico: por cuadro, en
   fracciones): lo que hacen en `frames` cuadros, con las sinergias */
void  skill_elem_rate(const SkillDef *s, int lvl, const SkillCtx *c, int frames, int *min, int *max);

/* Qué hace en ese nivel, en una línea ("+55% de daño, +2 de daño") */
const char *skill_effect_text(const SkillDef *s, int lvl, const SkillCtx *c, char *buf, size_t cap);

#endif
