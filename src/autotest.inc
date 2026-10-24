/* Prueba automática del Acto 1 (--autotest act1) y del Acto 2 (--autotest
   act2, que arranca en Lut Gholein con el Acto 1 hecho). Es parte de game.c
   (se incluye al final) para poder mirar todo el estado del juego.

   Un "bot" juega el acto entero con los controles, como una persona: busca
   el camino por el mapa (cruza pasillos, cuevas, escaleras, el portal de
   Tristram y los waypoints), pelea, habla con los NPC, toca los objetos de
   las misiones, contrata una mercenaria e imbuye un objeto. Después de cada
   paso verifica el estado (las misiones, los waypoints) y al final muestra
   un informe con lo que tardó cada paso, la memoria y cuántas zonas hubo
   armadas. Con 2 jugadores el segundo sigue al primero (y usa los waypoints
   con él). Los jugadores no reciben daño y matan de un golpe (D2_GOD y
   D2_ONEHIT), para que la prueba no dependa de la suerte. */

typedef enum {
    AT_TALK,        /* hablar con un NPC y elegir una opción (num = OPT_*) */
    AT_GOTO_LEVEL,  /* llegar a un nivel caminando (arg = nombre de levels.txt) */
    AT_ACTIVATE_WP, /* pasar por el waypoint del nivel donde está (todos los jugadores) */
    AT_WAYPOINT,    /* viajar por waypoint al número `num` (todos los jugadores) */
    AT_KILL,        /* matar a un jefe (arg = nombre de la unidad) */
    AT_CLEAR,       /* matar a todos los monstruos del nivel (arg) */
    AT_TOUCH,       /* tocar un objeto de misión (arg = clase de objects.txt) */
    AT_STONES,      /* tocar las Piedras Cairn en el orden del pergamino */
    AT_CHECK,       /* solo verificar */
    AT_SAVE,        /* guardar y revisar el archivo */
    AT_USE,         /* usar un objeto de la mochila (arg = código); si está en el piso, levantarlo antes */
    AT_CUBE,        /* armar el Báculo Horádrico en el cubo */
    AT_WAVES,       /* las oleadas del Trono: matar lo que salga en el nivel (arg) hasta que se cumpla la verificación */
    AT_ECO,         /* el Eco del Odio (eco.inc): pelear con lo que salga en la arena hasta que se cumpla la verificación */
    AT_ECO_QUIT,    /* TERMINAR EL ECO, en la pausa */
    AT_ECO_PORTAL,  /* entrar al portal de vuelta del final */
} AtKind;

typedef struct {
    AtKind      kind;
    const char *arg;
    int         num;
    const char *check; /* qué tiene que ser cierto al terminar ("cain=3", "raven_done"; varias con '+') */
    const char *label;
    int         optional; /* no es de una misión: la demostración lo saltea */
} AtStep;

static const AtStep k_act1_steps[] = {
    { AT_TALK, "akara", OPT_TALK, "den_started", "Akara pide limpiar la Guarida del Mal", 0 },
    { AT_TALK, "charsi", OPT_TALK, NULL, "Charsi se presenta", 1 },
    { AT_TALK, "gheed", OPT_TALK, NULL, "Gheed se presenta", 1 },
    { AT_TALK, "warriv1", OPT_TALK, NULL, "Warriv se presenta", 1 },
    { AT_GOTO_LEVEL, "Den of Evil", 0, NULL, "Ir a la Guarida del Mal", 0 },
    { AT_CLEAR, "Den of Evil", 0, "den_done", "Limpiar la Guarida del Mal", 0 },
    { AT_GOTO_LEVEL, "Rogue Encampment", 0, NULL, "Volver al campamento caminando", 0 },
    { AT_TALK, "akara", OPT_TALK, "den_rewarded", "Recompensa de Akara (punto de habilidad)", 0 },
    { AT_TALK, "kashya", OPT_TALK, "raven_started", "Kashya pide matar a Blood Raven", 0 },
    { AT_TALK, "akara", OPT_TALK, "cain=1", "Akara pide buscar a Caín", 0 },
    { AT_GOTO_LEVEL, "Cold Plains", 0, NULL, "Ir a las Cold Plains", 0 },
    { AT_ACTIVATE_WP, NULL, 1, "wp=1", "Activar el waypoint de las Cold Plains", 0 },
    { AT_GOTO_LEVEL, "Burial Grounds", 0, NULL, "Ir al Cementerio", 0 },
    { AT_KILL, "bloodraven", 0, "raven_done", "Matar a Blood Raven", 0 },
    { AT_WAYPOINT, NULL, 0, NULL, "Waypoint al campamento", 0 },
    { AT_TALK, "kashya", OPT_TALK, "raven_rewarded", "Recompensa de Kashya", 0 },
    { AT_TALK, "kashya", OPT_HIRE, "merc", "Contratar una mercenaria gratis", 0 },
    { AT_WAYPOINT, NULL, 1, NULL, "Waypoint a las Cold Plains", 0 },
    { AT_GOTO_LEVEL, "Stony Field", 0, NULL, "Ir al Stony Field", 0 },
    { AT_ACTIVATE_WP, NULL, 2, "wp=2", "Activar el waypoint del Stony Field", 0 },
    { AT_GOTO_LEVEL, "Underground Passage Level 1", 0, NULL, "Entrar al Pasaje Subterráneo", 0 },
    { AT_GOTO_LEVEL, "Dark Wood", 0, NULL, "Cruzar al Dark Wood", 0 },
    { AT_ACTIVATE_WP, NULL, 3, "wp=3", "Activar el waypoint del Dark Wood", 0 },
    { AT_TOUCH, "InifussTree", 0, "cain=2", "Tomar el Pergamino de Inifuss", 0 },
    { AT_WAYPOINT, NULL, 0, NULL, "Waypoint al campamento", 0 },
    { AT_TALK, "akara", OPT_TALK, "cain=3", "Akara traduce el pergamino", 0 },
    { AT_WAYPOINT, NULL, 2, NULL, "Waypoint al Stony Field", 0 },
    { AT_STONES, NULL, 0, "cain=4", "Tocar las Piedras Cairn en orden", 0 },
    { AT_GOTO_LEVEL, "Tristram", 0, NULL, "Entrar al portal de Tristram", 0 },
    { AT_TOUCH, "Gibbet", 0, "cain=5", "Liberar a Caín", 0 },
    { AT_GOTO_LEVEL, "Stony Field", 0, NULL, "Volver por el portal", 0 },
    { AT_WAYPOINT, NULL, 0, NULL, "Waypoint al campamento", 0 },
    { AT_TALK, "akara", OPT_TALK, "cain=6", "Agradecimiento de Akara", 0 },
    { AT_TALK, "cain1", OPT_TALK, "andy=1", "Caín pide matar a Andariel", 0 },
    { AT_WAYPOINT, NULL, 3, NULL, "Waypoint al Dark Wood", 0 },
    { AT_GOTO_LEVEL, "Black Marsh", 0, NULL, "Ir al Black Marsh", 0 },
    { AT_ACTIVATE_WP, NULL, 4, "wp=4", "Activar el waypoint del Black Marsh", 0 },
    { AT_TOUCH, "TowerTome", 0, "tower=1", "Leer el tomo de la torre", 0 },
    { AT_GOTO_LEVEL, "Tower Cellar Level 5", 0, NULL, "Bajar al quinto sótano de la torre", 0 },
    { AT_KILL, "The Countess", 0, "tower=2", "Matar a la Condesa", 0 },
    { AT_GOTO_LEVEL, "Black Marsh", 0, NULL, "Subir de la torre caminando", 0 },
    { AT_WAYPOINT, NULL, 0, NULL, "Waypoint al campamento", 0 },
    { AT_TALK, "akara", OPT_TALK, "tower=3", "Contarle a Akara lo de la Condesa", 0 },
    { AT_TALK, "charsi", OPT_TALK, "malus=1", "Charsi pide el Mazo", 0 },
    { AT_WAYPOINT, NULL, 4, NULL, "Waypoint al Black Marsh", 0 },
    { AT_GOTO_LEVEL, "Monastery Gate", 0, NULL, "Ir al Monasterio por el Tamoe Highland", 0 },
    { AT_GOTO_LEVEL, "Outer Cloister", 0, NULL, "Entrar al Claustro Exterior", 0 },
    { AT_ACTIVATE_WP, NULL, 5, "wp=5", "Activar el waypoint del Claustro Exterior", 0 },
    { AT_GOTO_LEVEL, "Barracks", 0, NULL, "Entrar a los Barracones", 0 },
    { AT_KILL, "The Smith", 0, NULL, "Matar al Herrero", 0 },
    { AT_TOUCH, "Malus", 0, "malus=2", "Tomar el Mazo de Horadric", 0 },
    { AT_GOTO_LEVEL, "Jail Level 1", 0, NULL, "Bajar a la Cárcel", 0 },
    { AT_ACTIVATE_WP, NULL, 6, "wp=6", "Activar el waypoint de la Cárcel", 0 },
    { AT_WAYPOINT, NULL, 0, NULL, "Waypoint al campamento", 0 },
    { AT_TALK, "charsi", OPT_TALK, "malus=3", "Devolverle el Mazo a Charsi", 0 },
    { AT_TALK, "charsi", OPT_IMBUE, "rare", "Charsi imbuye un objeto", 0 },
    { AT_WAYPOINT, NULL, 6, NULL, "Waypoint a la Cárcel", 0 },
    { AT_GOTO_LEVEL, "Inner Cloister", 0, NULL, "Bajar la Cárcel hasta el Claustro Interior", 0 },
    { AT_ACTIVATE_WP, NULL, 7, "wp=7", "Activar el waypoint del Claustro Interior", 0 },
    { AT_GOTO_LEVEL, "Cathedral", 0, NULL, "Entrar a la Catedral", 0 },
    { AT_GOTO_LEVEL, "Catacombs Level 2", 0, NULL, "Bajar a las Catacumbas", 0 },
    { AT_ACTIVATE_WP, NULL, 8, "wp=8", "Activar el waypoint de las Catacumbas", 0 },
    { AT_GOTO_LEVEL, "Catacombs Level 4", 0, NULL, "Bajar a la guarida de Andariel", 0 },
    { AT_KILL, "andariel", 0, "andy=2", "Matar a Andariel", 0 },
    { AT_GOTO_LEVEL, "Catacombs Level 2", 0, NULL, "Subir hasta el waypoint", 0 },
    { AT_WAYPOINT, NULL, 0, NULL, "Waypoint al campamento", 0 },
    { AT_TALK, "warriv1", OPT_TRAVEL, "act=2", "Viajar al este con Warriv (a Lut Gholein)", 0 },
    { AT_SAVE, NULL, 0, "saved", "Guardar y revisar la partida", 0 },
};

/* El Acto 2, en el orden en que lo juega una persona: las cloacas, las
   piezas del Báculo por el desierto, el palacio y el Santuario, el Canyon y
   la tumba verdadera ("#tomb": la del símbolo), Duriel y el final */
static const AtStep k_act2_steps[] = {
    { AT_TALK, "atma", OPT_TALK, "rada=1", "Atma pide matar a Radament", 0 },
    { AT_TALK, "greiz", OPT_HIRE, "merc", "Contratar un guerrero del desierto con Greiz", 1 },
    { AT_TALK, "elzix", OPT_GAMBLE, "gambled", "Apostar con Elzix", 1 },
    { AT_GOTO_LEVEL, "Sewers Level 1", 0, NULL, "Bajar a las cloacas", 0 },
    { AT_GOTO_LEVEL, "Sewers Level 2", 0, NULL, "Bajar al segundo piso de las cloacas", 0 },
    { AT_ACTIVATE_WP, NULL, 10, "wp=10", "Activar el waypoint de las cloacas", 0 },
    { AT_GOTO_LEVEL, "Sewers Level 3", 0, NULL, "Bajar a la guarida de Radament", 0 },
    { AT_KILL, "Radament", 0, "rada=2", "Matar a Radament", 0 },
    { AT_USE, "ass", 0, "book", "Leer el Libro de Habilidad", 0 },
    { AT_TOUCH, "HoradricScrollChest", 0, "has=tr1", "Abrir el cofre del pergamino horádrico", 0 },
    { AT_WAYPOINT, NULL, 9, NULL, "Waypoint a Lut Gholein", 0 },
    { AT_TALK, "atma", OPT_TALK, "rada=3", "Contarle a Atma lo de Radament", 0 },
    { AT_TALK, "cain2", OPT_TALK, "staff=1", "Mostrarle el pergamino a Caín", 0 },
    { AT_GOTO_LEVEL, "Rocky Waste", 0, NULL, "Salir al Rocky Waste", 0 },
    { AT_GOTO_LEVEL, "Dry Hills", 0, NULL, "Ir a las Dry Hills", 0 },
    { AT_ACTIVATE_WP, NULL, 11, "wp=11", "Activar el waypoint de las Dry Hills", 0 },
    { AT_GOTO_LEVEL, "Halls of the Dead Level 1", 0, NULL, "Entrar a los Halls of the Dead", 0 },
    { AT_GOTO_LEVEL, "Halls of the Dead Level 2", 0, NULL, "Bajar al segundo piso de los Halls", 0 },
    { AT_ACTIVATE_WP, NULL, 12, "wp=12", "Activar el waypoint de los Halls of the Dead", 0 },
    { AT_GOTO_LEVEL, "Halls of the Dead Level 3", 0, NULL, "Bajar al tercer piso de los Halls", 0 },
    { AT_TOUCH, "HoradricCubeChest", 0, "has=box", "Abrir el cofre del Cubo Horádrico", 0 },
    { AT_WAYPOINT, NULL, 11, NULL, "Waypoint a las Dry Hills", 0 },
    { AT_GOTO_LEVEL, "Far Oasis", 0, NULL, "Ir al Far Oasis", 0 },
    { AT_ACTIVATE_WP, NULL, 13, "wp=13", "Activar el waypoint del Far Oasis", 0 },
    { AT_GOTO_LEVEL, "Maggot Lair Level 1", 0, NULL, "Entrar al Maggot Lair", 0 },
    { AT_GOTO_LEVEL, "Maggot Lair Level 3", 0, NULL, "Bajar al fondo del Maggot Lair", 0 },
    { AT_TOUCH, "StaffOfKingsChest", 0, "has=msf", "Tomar el Bastón de los Reyes", 0 },
    { AT_WAYPOINT, NULL, 13, NULL, "Waypoint al Far Oasis", 0 },
    { AT_GOTO_LEVEL, "Lost City", 0, NULL, "Ir a la Lost City", 0 },
    { AT_ACTIVATE_WP, NULL, 14, "wp=14", "Activar el waypoint de la Lost City", 0 },
    { AT_GOTO_LEVEL, "Valley of Snakes", 0, NULL, "Ir al Valley of Snakes", 0 },
    { AT_GOTO_LEVEL, "Claw Viper Temple Level 1", 0, "sun=1", "Entrar al Claw Viper Temple (ya sin sol)", 0 },
    { AT_GOTO_LEVEL, "Claw Viper Temple Level 2", 0, NULL, "Bajar al altar del templo", 0 },
    { AT_TOUCH, "TaintedSunShrine", 0, "sun=3+has=vip", "Romper el altar y tomar el Amuleto de la Víbora", 0 },
    { AT_WAYPOINT, NULL, 9, NULL, "Waypoint a Lut Gholein", 0 },
    { AT_TALK, "drognan", OPT_TALK, "sun=4", "Contarle a Drognan que volvió el sol", 0 },
    { AT_CUBE, NULL, 0, "has=hst", "Armar el Báculo Horádrico en el cubo", 0 },
    { AT_TALK, "cain2", OPT_TALK, "staff=2", "Mostrarle el Báculo a Caín", 0 },
    { AT_TALK, "drognan", OPT_TALK, "arcane=1", "Drognan manda a ver a Jerhyn", 0 },
    { AT_TALK, "jerhyn", OPT_TALK, "arcane=2", "Jerhyn abre el palacio", 0 },
    { AT_GOTO_LEVEL, "Harem Level 1", 0, NULL, "Entrar al palacio", 0 },
    { AT_GOTO_LEVEL, "Palace Cellar Level 1", 0, NULL, "Bajar a los sótanos del palacio", 0 },
    { AT_ACTIVATE_WP, NULL, 15, "wp=15", "Activar el waypoint de los sótanos", 0 },
    { AT_GOTO_LEVEL, "Palace Cellar Level 3", 0, NULL, "Bajar al tercer sótano", 0 },
    { AT_GOTO_LEVEL, "Arcane Sanctuary", 0, "arcane=3", "Entrar por el portal al Arcane Sanctuary", 0 },
    { AT_ACTIVATE_WP, NULL, 16, "wp=16", "Activar el waypoint del Santuario", 0 },
    { AT_KILL, "summoner", 0, "summ=2", "Matar al Invocador", 0 },
    { AT_TOUCH, "ArcaneTome", 0, "arcane=4", "Leer el diario de Horazon", 0 },
    { AT_CHECK, NULL, 0, "symbol", "El símbolo de la tumba verdadera en el registro", 0 },
    { AT_GOTO_LEVEL, "Canyon of the Magi", 0, "tombs=1", "Cruzar el portal rojo al Canyon", 0 },
    { AT_ACTIVATE_WP, NULL, 17, "wp=17", "Activar el waypoint del Canyon", 0 },
    { AT_WAYPOINT, NULL, 9, NULL, "Waypoint a Lut Gholein", 0 },
    { AT_TALK, "atma", OPT_TALK, "summ=3", "Contarle a Atma lo del Invocador", 0 },
    { AT_WAYPOINT, NULL, 17, NULL, "Waypoint al Canyon", 0 },
    { AT_GOTO_LEVEL, "#tomb", 0, NULL, "Entrar a la tumba del símbolo", 0 },
    { AT_TOUCH, "SevenTombsReceptacle", 0, "staff=3", "Poner el Báculo en el Orificio", 0 },
    { AT_GOTO_LEVEL, "Duriel's Lair", 0, NULL, "Entrar por el portal a la guarida de Duriel", 0 },
    { AT_KILL, "duriel", 0, "tombs=3", "Matar a Duriel", 0 },
    { AT_TALK, "tyrael1", OPT_TALK, "tombs=4", "Hablar con Tyrael", 0 },
    { AT_GOTO_LEVEL, "Lut Gholein", 0, NULL, "Volver al pueblo por el portal de Tyrael", 0 },
    { AT_TALK, "jerhyn", OPT_TALK, "tombs=5", "Jerhyn manda a ver a Meshif", 0 },
    { AT_TALK, "meshif1", OPT_TALK, "tombs=6", "Meshif: el Acto 2 terminó", 0 },
    { AT_SAVE, NULL, 0, "saved", "Guardar y revisar la partida", 0 },
};
/* El Acto 3, en el orden en que lo juega una persona: el Spider Forest y
   el ojo de Khalim, la selva hasta el Flayer Jungle (la Gidbinn y el
   cerebro), el pueblo, Kurast (el tomo, la palanca y el corazón), el
   Consejo, la Voluntad y el Orbe, Mefisto y el Pájaro Dorado */
static const AtStep k_act3_steps[] = {
    { AT_TALK, "hratli", OPT_TALK, "hratli", "Hratli se presenta (y va a su fragua)", 0 },
    { AT_TALK, "asheara", OPT_TALK, NULL, "Asheara se presenta", 1 },
    { AT_GOTO_LEVEL, "Spider Forest", 0, NULL, "Salir al Spider Forest", 0 },
    { AT_ACTIVATE_WP, NULL, 19, "wp=19", "Activar el waypoint del Spider Forest", 0 },
    { AT_KILL, "#jade", 0, NULL, "Matar al jefe que tiene la estatuilla de jade", 0 },
    { AT_GOTO_LEVEL, "Spider Cavern", 0, NULL, "Entrar a la Spider Cavern", 0 },
    { AT_TOUCH, "KhalimEyeChest", 0, "has=qey", "Abrir el cofre del Ojo de Khalim", 0 },
    { AT_GOTO_LEVEL, "Great Marsh", 0, NULL, "Ir al Great Marsh", 0 },
    { AT_ACTIVATE_WP, NULL, 20, "wp=20", "Activar el waypoint del Great Marsh", 0 },
    { AT_GOTO_LEVEL, "Flayer Jungle", 0, NULL, "Ir al Flayer Jungle", 0 },
    { AT_ACTIVATE_WP, NULL, 21, "wp=21", "Activar el waypoint del Flayer Jungle", 0 },
    { AT_TOUCH, "GidBInnDecoy", 0, "decoy", "Tocar el altar de la Gidbinn (es un señuelo)", 0 },
    { AT_KILL, "fetish11", 0, NULL, "Matar al fetiche que sale (tiene la Gidbinn)", 0 },
    { AT_USE, "g33", 0, "has=g33", "Levantar la Gidbinn", 0 },
    { AT_GOTO_LEVEL, "Flayer Dungeon Level 1", 0, NULL, "Entrar al Flayer Dungeon", 0 },
    { AT_GOTO_LEVEL, "Flayer Dungeon Level 3", 0, NULL, "Bajar al tercer piso del Flayer Dungeon", 0 },
    { AT_TOUCH, "KhalimBrainChest", 0, "has=qbr", "Abrir el cofre del Cerebro de Khalim", 0 },
    { AT_GOTO_LEVEL, "Flayer Jungle", 0, NULL, "Subir al Flayer Jungle caminando", 0 },
    { AT_WAYPOINT, NULL, 18, NULL, "Waypoint al muelle", 0 },
    { AT_TALK, "cain3", OPT_TALK, "will=1", "Caín pide la Voluntad de Khalim", 0 },
    { AT_TALK, "hratli", OPT_TALK, "blade=1", "Hratli pide la Gidbinn", 0 },
    { AT_TALK, "ormus", OPT_TALK, "blade=3", "Darle la Gidbinn a Ormus (el anillo)", 0 },
    { AT_TALK, "asheara", OPT_TALK, "freehire", "Asheara da un Lobo de Hierro gratis", 0 },
    { AT_TALK, "asheara", OPT_HIRE, "merc", "Contratar el Lobo de Hierro", 1 },
    { AT_WAYPOINT, NULL, 21, NULL, "Waypoint al Flayer Jungle", 0 },
    { AT_GOTO_LEVEL, "Lower Kurast", 0, NULL, "Ir a la Kurast baja", 0 },
    { AT_ACTIVATE_WP, NULL, 22, "wp=22", "Activar el waypoint de la Kurast baja", 0 },
    { AT_GOTO_LEVEL, "Kurast Bazaar", 0, NULL, "Ir al bazar", 0 },
    { AT_ACTIVATE_WP, NULL, 23, "wp=23", "Activar el waypoint del bazar", 0 },
    { AT_GOTO_LEVEL, "Ruined Temple", 0, NULL, "Entrar al Ruined Temple", 0 },
    { AT_TOUCH, "LamesEnsTome", 0, "has=bbb", "Tomar el Tomo de Lam Esen", 0 },
    { AT_GOTO_LEVEL, "Kurast Bazaar", 0, NULL, "Salir al bazar", 0 },
    { AT_GOTO_LEVEL, "Sewers Level 1", 0, NULL, "Bajar a las cloacas", 0 },
    { AT_TOUCH, "BeneathTheCityLever", 0, "lever", "Tirar de la palanca de las cloacas", 0 },
    { AT_GOTO_LEVEL, "Sewers Level 2", 0, NULL, "Bajar al segundo piso de las cloacas", 0 },
    { AT_TOUCH, "KhalimHeartChest", 0, "has=qhr", "Abrir el cofre del Corazón de Khalim", 0 },
    { AT_GOTO_LEVEL, "Kurast Bazaar", 0, NULL, "Volver al bazar caminando", 0 },
    { AT_WAYPOINT, NULL, 18, NULL, "Waypoint al muelle", 0 },
    { AT_TALK, "alkor", OPT_TALK, "tome=3", "Darle el tomo a Alkor (5 puntos de atributo)", 0 },
    { AT_TALK, "cain3", OPT_TALK, NULL, "Caín comenta las partes de Khalim", 1 },
    { AT_WAYPOINT, NULL, 23, NULL, "Waypoint al bazar", 0 },
    { AT_GOTO_LEVEL, "Upper Kurast", 0, NULL, "Ir a la Kurast alta", 0 },
    { AT_ACTIVATE_WP, NULL, 24, "wp=24", "Activar el waypoint de la Kurast alta", 0 },
    { AT_GOTO_LEVEL, "Kurast Causeway", 0, NULL, "Cruzar al Causeway", 0 },
    { AT_GOTO_LEVEL, "Travincal", 0, NULL, "Llegar a Travincal", 0 },
    { AT_ACTIVATE_WP, NULL, 25, "wp=25", "Activar el waypoint de Travincal", 0 },
    { AT_WAYPOINT, NULL, 18, NULL, "Waypoint al muelle", 0 },
    { AT_TALK, "ormus", OPT_TALK, "temple=1", "Ormus pide matar al Consejo", 0 },
    { AT_WAYPOINT, NULL, 25, NULL, "Waypoint a Travincal", 0 },
    { AT_KILL, "Ismail Vilehand", 0, NULL, "Matar a Ismail Vilehand", 0 },
    { AT_KILL, "Geleb Flamefinger", 0, NULL, "Matar a Geleb Flamefinger", 0 },
    { AT_KILL, "Toorc Icefist", 0, "temple=3", "Matar a Toorc Icefist (el Consejo)", 0 },
    { AT_USE, "qf1", 0, "has=qf1", "Levantar el Mangual de Khalim", 0 },
    { AT_CUBE, NULL, 0, "has=qf2", "Armar la Voluntad de Khalim en el cubo", 0 },
    { AT_USE, "qf2", 0, "wield=qf2", "Ponerse la Voluntad de Khalim en la mano", 0 },
    { AT_TOUCH, "CompellingOrb", 0, "will=3", "Romper el Orbe cautivante", 0 },
    { AT_GOTO_LEVEL, "Durance of Hate Level 1", 0, NULL, "Bajar al Durance of Hate", 0 },
    { AT_GOTO_LEVEL, "Durance of Hate Level 2", 0, NULL, "Bajar al segundo piso del Durance", 0 },
    { AT_ACTIVATE_WP, NULL, 26, "wp=26", "Activar el waypoint del Durance", 0 },
    { AT_GOTO_LEVEL, "Durance of Hate Level 3", 0, NULL, "Bajar a la guarida de Mefisto", 0 },
    { AT_KILL, "mephisto", 0, "meph=2", "Matar a Mefisto (sube el puente)", 0 },
    { AT_GOTO_LEVEL, "Durance of Hate Level 2", 0, NULL, "Subir hasta el waypoint", 0 },
    { AT_WAYPOINT, NULL, 18, NULL, "Waypoint al muelle", 0 },
    { AT_TALK, "ormus", OPT_TALK, "temple=4", "Contarle a Ormus lo del Consejo", 0 },
    { AT_CHECK, NULL, 0, "bird=1", "La estatuilla de jade (la soltó el primer jefe)", 0 },
    { AT_TALK, "cain3", OPT_TALK, "bird=2", "Mostrarle la estatuilla a Caín", 0 },
    { AT_TALK, "meshif2", OPT_TALK, "bird=3", "Meshif la cambia por el Pájaro Dorado", 0 },
    { AT_TALK, "cain3", OPT_TALK, "bird=4", "Caín: el pájaro, para Alkor", 0 },
    { AT_TALK, "alkor", OPT_TALK, "bird=5", "Darle el Pájaro Dorado a Alkor", 0 },
    { AT_WAYPOINT, NULL, 19, NULL, "Salir del muelle (waypoint al Spider Forest)", 0 },
    { AT_WAYPOINT, NULL, 18, NULL, "Waypoint al muelle", 0 },
    { AT_TALK, "alkor", OPT_TALK, "bird=6", "Alkor da la Poción de la vida", 0 },
    { AT_USE, "xyz", 0, "life", "Tomar la Poción de la vida", 0 },
    { AT_SAVE, NULL, 0, "saved", "Guardar y revisar la partida", 0 },
};
/* El Acto 4, en el orden en que lo juega una persona: Izual en el Plains
   of Despair (y su espíritu), el City of the Damned, la Forja Infernal en el
   River of Flame (Hephasto, la piedra y el Martillo), el Santuario del Caos
   (los cinco sellos, sus tres jefes y Diablo) y el portal del final */
static const AtStep k_act4_steps[] = {
    { AT_TALK, "tyrael2", OPT_TALK, "izual=1", "Tyrael se presenta y pide matar a Izual", 0 },
    { AT_TALK, "cain4", OPT_TALK, "forge=1", "Caín pide la Forja Infernal (y da la piedra)", 0 },
    { AT_GOTO_LEVEL, "Outer Steppes", 0, NULL, "Salir al Outer Steppes", 0 },
    { AT_GOTO_LEVEL, "Plains of Despair", 0, "izual=2", "Ir al Plains of Despair", 0 },
    { AT_KILL, "izual", 0, "izual=3", "Matar a Izual", 0 },
    { AT_TALK, "izualghost", OPT_TALK, "izual=4", "Hablar con el espíritu de Izual", 0 },
    { AT_GOTO_LEVEL, "City of the Damned", 0, NULL, "Ir al City of the Damned", 0 },
    { AT_ACTIVATE_WP, NULL, 28, "wp=28", "Activar el waypoint del City of the Damned", 0 },
    { AT_WAYPOINT, NULL, 27, NULL, "Waypoint a la Fortaleza", 0 },
    { AT_TALK, "tyrael2", OPT_TALK, "izual=5", "Recompensa de Tyrael (dos puntos de habilidad)", 0 },
    { AT_TALK, "tyrael2", OPT_TALK, "diablo=1", "Tyrael pide matar a Diablo", 0 },
    { AT_TALK, "jamella", OPT_TRADE, NULL, "Jamella: la tienda", 1 },
    { AT_WAYPOINT, NULL, 28, NULL, "Waypoint al City of the Damned", 0 },
    { AT_GOTO_LEVEL, "River of Flame", 0, NULL, "Bajar al River of Flame", 0 },
    { AT_KILL, "The Feature Creep", 0, NULL, "Matar a Hephasto el Armero", 0 },
    { AT_USE, "hfh", 0, "has=hfh", "Levantar el Martillo de la Forja Infernal", 0 },
    { AT_TOUCH, "SoulStoneForge", 0, "stone", "Poner la Piedra de Mefisto en la Forja", 0 },
    { AT_USE, "hfh", 0, "wield=hfh", "Ponerse el Martillo en la mano", 0 },
    { AT_TOUCH, "SoulStoneForge", 0, "forge=4", "Romper la piedra (tres golpes)", 0 },
    { AT_ACTIVATE_WP, NULL, 29, "wp=29", "Activar el waypoint del River of Flame", 0 },
    { AT_GOTO_LEVEL, "Chaos Sanctum", 0, "diablo=2", "Entrar al Santuario del Caos", 0 },
    { AT_TOUCH, "DiabloSeal2", 0, "seal=2", "Abrir el segundo sello", 0 },
    { AT_TOUCH, "DiabloSeal1", 0, "seal=1", "Abrir el primer sello (sale el Infector de Almas)", 0 },
    { AT_KILL, "Infector of Souls", 0, NULL, "Matar al Infector de Almas", 0 },
    { AT_TOUCH, "DiabloSeal3", 0, "seal=3", "Abrir el tercer sello (sale Lord De Seis)", 0 },
    { AT_KILL, "Lord De Seis", 0, NULL, "Matar a Lord De Seis", 0 },
    { AT_TOUCH, "DiabloSeal4", 0, "seal=4", "Abrir el cuarto sello", 0 },
    { AT_TOUCH, "DiabloSeal5", 0, "seal=5", "Abrir el quinto sello (sale el Gran Visir del Caos)", 0 },
    { AT_KILL, "Grand Vizier of Chaos", 0, NULL, "Matar al Gran Visir (sale Diablo)", 0 },
    { AT_KILL, "diablo", 0, "diablo=4", "Matar a Diablo", 0 },
    { AT_GOTO_LEVEL, "River of Flame", 0, NULL, "Volver al río caminando", 0 },
    { AT_WAYPOINT, NULL, 27, NULL, "Waypoint a la Fortaleza", 0 },
    { AT_TALK, "cain4", OPT_TALK, "forge=5", "Contarle a Caín lo de la Forja", 0 },
    { AT_TALK, "tyrael2", OPT_TALK, "portal", "Tyrael abre el portal del final", 0 },
    { AT_TOUCH, "ReallyTheLastPortal", 0, "end", "Entrar al portal: el epílogo", 0 },
    { AT_SAVE, NULL, 0, "saved", "Guardar y revisar la partida", 0 },
};
/* El Acto 5, en el orden en que lo juega una persona: Shenk en el Bloody
   Foothills, los presos del Frigid Highlands, Anya en el Frozen River,
   Nihlathak en su templo (por el portal de Anya), los Antiguos en la cumbre,
   las oleadas del Trono y Baal en la Cámara; el portal de Tyrael al final */
static const AtStep k_act5_steps[] = {
    { AT_TALK, "larzuk", OPT_TALK, "shenk=1", "Larzuk pide matar a Shenk", 0 },
    { AT_TALK, "qual-kehk", OPT_TALK, "rescue=1", "Qual-Kehk pide rescatar a los bárbaros", 0 },
    { AT_TALK, "malah", OPT_TALK, "anya=1", "Malah pide buscar a Anya", 0 },
    { AT_GOTO_LEVEL, "Bloody Foothills", 0, NULL, "Salir al Bloody Foothills", 0 },
    { AT_KILL, "Siege Boss", 0, "shenk=2", "Matar a Shenk el Capataz", 0 },
    { AT_GOTO_LEVEL, "Rigid Highlands", 0, NULL, "Ir al Frigid Highlands", 0 },
    { AT_ACTIVATE_WP, NULL, 31, "wp=31", "Activar el waypoint del Frigid Highlands", 0 },
    { AT_KILL, "prisondoor", 0, "rescued=15", "Romper las tres prisiones (15 bárbaros)", 0 },
    { AT_WAYPOINT, NULL, 30, NULL, "Waypoint a Harrogath", 0 },
    { AT_TALK, "larzuk", OPT_TALK, "shenk=3", "Larzuk se entera de Shenk", 0 },
    { AT_TALK, "larzuk", OPT_SOCKET, "socketed", "Larzuk engarza un objeto", 0 },
    { AT_TALK, "qual-kehk", OPT_TALK, "rescue=3", "Recompensa de Qual-Kehk (tres runas)", 0 },
    { AT_WAYPOINT, NULL, 31, NULL, "Waypoint al Frigid Highlands", 0 },
    { AT_GOTO_LEVEL, "Arreat Plateau", 0, NULL, "Ir al Arreat Plateau", 0 },
    { AT_ACTIVATE_WP, NULL, 32, "wp=32", "Activar el waypoint del Arreat Plateau", 0 },
    { AT_GOTO_LEVEL, "Crystalized Cavern Level 1", 0, NULL, "Entrar al Crystalline Passage", 0 },
    { AT_ACTIVATE_WP, NULL, 33, "wp=33", "Activar el waypoint del Crystalline Passage", 0 },
    { AT_GOTO_LEVEL, "Cellar of Pity", 0, NULL, "Bajar al Frozen River", 0 },
    { AT_TOUCH, "AnyaYetAgain", 0, "anya=2", "Encontrar a Anya congelada", 0 },
    { AT_WAYPOINT, NULL, 30, NULL, "Volver a Harrogath", 0 },
    { AT_TALK, "malah", OPT_TALK, "anya=3", "Malah da su poción", 0 },
    { AT_WAYPOINT, NULL, 33, NULL, "Waypoint al Crystalline Passage", 0 },
    { AT_GOTO_LEVEL, "Cellar of Pity", 0, NULL, "Volver al Frozen River", 0 },
    { AT_TOUCH, "AnyaYetAgain", 0, "anya=4", "Descongelar a Anya con la poción", 0 },
    { AT_WAYPOINT, NULL, 30, NULL, "Volver a Harrogath", 0 },
    { AT_TALK, "malah", OPT_TALK, "anya=5", "Recompensa de Malah (Pergamino de Resistencia)", 0 },
    { AT_USE, "tr2", 0, "resist", "Leer el Pergamino de Resistencia", 0 },
    { AT_TALK, "drehya", OPT_TALK, "anyatold", "Anya agradece el rescate", 0 },
    { AT_TALK, "drehya", OPT_TALK, "nihl=1", "Anya abre el portal al templo de Nihlathak", 0 },
    { AT_GOTO_LEVEL, "Nihlathaks Temple", 0, "nihl=2", "Entrar al templo por el portal de Anya", 0 },
    { AT_GOTO_LEVEL, "Halls of Anguish", 0, NULL, "Bajar a los Halls of Anguish", 0 },
    { AT_GOTO_LEVEL, "Halls of Death's Calling", 0, NULL, "Bajar a los Halls of Pain", 0 },
    { AT_ACTIVATE_WP, NULL, 35, "wp=35", "Activar el waypoint de los Halls of Pain", 0 },
    { AT_GOTO_LEVEL, "Halls of Vaught", 0, NULL, "Bajar a los Halls of Vaught", 0 },
    { AT_KILL, "Nihlathak Boss", 0, "nihl=3", "Matar a Nihlathak", 0 },
    { AT_WAYPOINT, NULL, 30, NULL, "Volver a Harrogath", 0 },
    { AT_TALK, "drehya", OPT_TALK, "nihl=4", "Anya se entera de Nihlathak", 0 },
    { AT_TALK, "drehya", OPT_PERSONAL, "personal", "Anya personaliza un objeto", 0 },
    { AT_TALK, "qual-kehk", OPT_TALK, "ancients=1", "Qual-Kehk pide el rito de iniciación", 0 },
    { AT_WAYPOINT, NULL, 33, NULL, "Waypoint al Crystalline Passage", 0 },
    { AT_GOTO_LEVEL, "Crystalized Cavern Level 2", 0, NULL, "Ir al Glacial Trail", 0 },
    { AT_ACTIVATE_WP, NULL, 34, "wp=34", "Activar el waypoint del Glacial Trail", 0 },
    { AT_GOTO_LEVEL, "Tundra Wastelands", 0, NULL, "Salir a la Frozen Tundra", 0 },
    { AT_ACTIVATE_WP, NULL, 36, "wp=36", "Activar el waypoint de la Frozen Tundra", 0 },
    { AT_GOTO_LEVEL, "Glacial Caves Level 1", 0, NULL, "Entrar al Ancients' Way", 0 },
    { AT_ACTIVATE_WP, NULL, 37, "wp=37", "Activar el waypoint del Ancients' Way", 0 },
    { AT_GOTO_LEVEL, "Rocky Summit", 0, NULL, "Subir a la cumbre del Arreat", 0 },
    { AT_TOUCH, "AncientsAltar", 0, "ancients=2", "Tocar el altar: despiertan los Antiguos", 0 },
    { AT_KILL, "Ancient Barbarian 1", 0, NULL, "Matar a Talic", 0 },
    { AT_KILL, "Ancient Barbarian 2", 0, NULL, "Matar a Madawc", 0 },
    { AT_KILL, "Ancient Barbarian 3", 0, "ancients=3", "Matar a Korlic", 0 },
    { AT_GOTO_LEVEL, "The Worldstone Keep Level 1", 0, NULL, "Entrar a la Fortaleza de la Piedra del Mundo", 0 },
    { AT_GOTO_LEVEL, "The Worldstone Keep Level 2", 0, NULL, "Bajar al nivel 2", 0 },
    { AT_ACTIVATE_WP, NULL, 38, "wp=38", "Activar el waypoint del nivel 2", 0 },
    { AT_GOTO_LEVEL, "The Worldstone Keep Level 3", 0, NULL, "Bajar al nivel 3", 0 },
    { AT_GOTO_LEVEL, "Throne of Destruction", 0, "baal=2", "Llegar al Trono de la Destrucción", 0 },
    { AT_WAVES, "Throne of Destruction", 0, "baal=3", "Las cinco oleadas: Baal se va a la Cámara", 0 },
    { AT_GOTO_LEVEL, "The Worldstone Chamber", 0, NULL, "Seguir a Baal a la Cámara", 0 },
    { AT_KILL, "baalcrab", 0, "baal=4", "Matar a Baal", 0 },
    { AT_TALK, "tyrael3", OPT_TALK, "baal=5", "Hablar con Tyrael: abre el portal", 0 },
    { AT_TOUCH, "NotKiddingLastPortal", 0, "end5", "Entrar al portal de Tyrael: el final", 0 },
    { AT_SAVE, NULL, 0, "saved", "Guardar y revisar la partida", 0 },
};
/* --autotest eco (con D2_ECO): las oleadas con sus jefes, el final, el cofre y el portal (D2_ECO_WAVE=26
   empieza en la segunda vuelta: los del evento Pandemonium) */
static const AtStep k_eco_steps[] = {
    { AT_ECO, NULL, 0, "eco=3", "Oleadas 1 y 2", 0 },
    { AT_ECO, NULL, 0, "eco=4", "Oleada 3: un jefe con nombre propio", 0 },
    { AT_ECO, NULL, 0, "eco=6", "Oleada 5: el jefe del Acto 1", 0 },
    { AT_ECO, NULL, 0, "eco=9", "Oleada 8: un jefe con nombre del Acto 2", 0 },
    { AT_ECO, NULL, 0, "eco=11", "Oleada 10: el jefe del Acto 2", 0 },
    { AT_ECO, NULL, 0, "eco=16", "Oleada 15: el jefe del Acto 3", 0 },
    { AT_ECO, NULL, 0, "eco=21", "Oleada 20: el jefe del Acto 4", 0 },
    { AT_ECO, NULL, 0, "eco=26", "Oleada 25: el jefe del Acto 5", 0 },
    /* (la segunda vuelta, solo si se empieza en ella: D2_ECO_WAVE=26 o más) */
    { AT_ECO, NULL, 0, "eco2=31", "Oleada 30: Lilith", 0 },
    { AT_ECO, NULL, 0, "eco2=36", "Oleada 35: Duriel uber", 0 },
    { AT_ECO, NULL, 0, "eco2=41", "Oleada 40: Izual uber", 0 },
    { AT_ECO, NULL, 0, "eco2=46", "Oleada 45: el Diablo uber", 0 },
    { AT_ECO, NULL, 0, "eco2=51", "Oleada 50: los tres de Uber Tristram", 0 },
    { AT_ECO_QUIT, NULL, 0, "ecoend", "Terminar el Eco desde la pausa", 0 },
    { AT_TOUCH, "ConsolationChest", 0, "ecochest", "Abrir el cofre del Eco", 0 },
    { AT_ECO_PORTAL, NULL, 0, "ecoexit", "Volver por el portal", 0 },
};
#define AT_MAX_STEPS 96 /* el más largo */

/* Un bot por jugador: su navegación (bot.c) y lo de los botones */
typedef struct {
    BotNav  *nav;
    int      pulse;   /* los botones se aprietan un tick sí y otro no */
    int      no_path; /* ticks seguidos sin camino */
    int      stuck;    /* en el último segundo avanzó menos de un subtile */
    int      fighting; /* ticks seguidos con un monstruo pegado */
    int      swinging; /* ticks pegando parado antes de volver a intentar caminar */
    float    anchor_x, anchor_y; /* dónde estaba hace un segundo */
    int      nav_r, nav_dest; /* lo último de la navegación (para el informe si se traba) */
    float    nav_x, nav_y, nav_dx, nav_dy;
    uint32_t ui_tick; /* el último tick en que apretó A, B o una flecha (at_ui_ready) */
} AtBot;

static struct {
    int      active, players;
    int      act;            /* el acto que juega (1 o 2) */
    const AtStep *steps;     /* sus pasos */
    int      nsteps;
    int      gold0;          /* el oro del J1 al empezar el paso (para ver si apostó) */
    int      step, phase, step_ticks, step_start;
    int      talks;          /* las veces que se le volvió a hablar al NPC del paso */
    int      failed;
    int      demo_fails; /* la demostración sigue después de un paso que falla (hasta 3 seguidos) */
    int      demo_side;  /* ticks del paso peleando o levantando cosas por su cuenta */
    char     fail_msg[160];
    AtBot    bot[MAX_PLAYERS];
    int      step_result[AT_MAX_STEPS];
    int      step_time[AT_MAX_STEPS];
    int      max_zones, zone_loads;
    int      unreachable; /* monstruos a los que no se pudo llegar caminando */
    int      kill_unit, kill_hp, kill_since; /* el jefe del paso, su vida más baja y desde cuándo no baja */
    int      clear_unit; /* el monstruo que va a matar para limpiar el nivel, + 1 (0: ninguno) */
    long     max_rss_kb;
    double   peak_map_mb, peak_gfx_mb; /* en el pico de memoria */
    int      peak_step, slow_ticks;
    double   tick_ms_sum, tick_ms_max;
    long     ticks;
    double   render_ms_sum, render_ms_max;
    double   iv_tick_sum, iv_tick_max, iv_render_sum, iv_render_max; /* el tramo de D2_AT_PERF */
    int      iv_ticks, iv_renders;
    int      renders;
    char     shots[200];
    int      trace;
} s_at;
#define AT_STEPS (s_at.nsteps)

/* El nivel de un paso: por su nombre, o "#tomb", la tumba verdadera de
   esta partida (la del símbolo del registro) */
static const Level *at_level(const char *name)
{
    if (name && strcmp(name, "#tomb") == 0) { /* las siete se llaman igual: por su Id */
        for (int i = 0; i < world_level_count(); i++)
            if (world_level(i)->id == world_staff_tomb())
                return world_level(i);
        return NULL;
    }
    return name ? world_level_named(name) : NULL;
}

static long at_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[128];
    long kb = 0;
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, "VmRSS:", 6) == 0)
            kb = atol(line + 6);
    if (f)
        fclose(f);
    return kb;
}

/* La demostración (demobot.inc, incluido al final) */
static int  demo_button(int i, int target, float d);
static void demo_press(int i, PlayerInput *in, int b);
static void demo_tick(void);
static int  demo_lead(const AtStep *st, PlayerInput *in);
static void demo_follow(int i, PlayerInput *in);
static void demo_potions(int i, PlayerInput *in);
static void demo_step_begin(const AtStep *st);
static void demo_step_end(const AtStep *st, int ok);
static void demo_report(void);
static int  demo_farming(void); /* farmeando o volviendo al pueblo a curarse */
static int  demo_escape(int i, PlayerInput *in, AtBot *b, float x, float y);

/* Pegar: A (el ataque) o, en la demostración, el botón que convenga */
static void at_swing(Player *p, PlayerInput *in)
{
    int b = 0;
    if (s_demo) {
        int e = units_find_enemy(p->x, p->y, 9.0f);
        float ex = p->x, ey = p->y;
        if (e >= 0)
            units_position(e, 1.0f, &ex, &ey);
        b = demo_button((int)(p - s_pl), e, sqrtf((ex - p->x) * (ex - p->x) + (ey - p->y) * (ey - p->y)));
    }
    if (s_demo) {
        demo_press((int)(p - s_pl), in, b);
        return;
    }
    /* El segundo no aprieta A con lo de la misión al lado: A también levanta,
       y se quedaba con el Mazo que tenía que tomar el primero */
    for (int k = 0; p != &s_pl[0] && k < items_ground_count(); k++) {
        float x, y;
        const Item *g = items_ground_item(k, &x, &y);
        if (g && is_quest_item(g->type) && (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < 2.5f * 2.5f)
            return;
    }
    in->action = in->button[0] = 1;
}

static void at_move(PlayerInput *in, float wx, float wy)
{
    /* Dirección del mundo -> dirección en pantalla (la que lee el juego) */
    in->move_x = wx - wy;
    in->move_y = (wx + wy) * 0.5f;
    in->sprint = 1;
}

/* En red lo que aprieta el bot se juega NET_DELAY ticks después (main.c net_step): en un
   diálogo, una lista o un portal, el botón que sigue espera a que se vea lo que hizo el
   anterior (si no, el segundo "abajo" pasaba de Contratar a Salir y Kashya no contrataba).
   Sin red no espera: se ve en el tick siguiente. */
static int at_ui_ready(const AtBot *b)
{
    return net_role() == NET_OFF || s_ticks - b->ui_tick > NET_DELAY;
}

/* Ir a (x, y) de la zona `dest` (-1 = la de ese punto), pasando por las
   zonas que hagan falta: 1 = llegó (a menos de `reach`), 0 = sigue, -1 = no
   hay camino (después de un rato intentando) */
static int at_goto(AtBot *b, Player *p, PlayerInput *in, int dest, float x, float y, float reach)
{
    if (!b->nav)
        b->nav = bot_nav_new(0x9E3779B9u * (uint32_t)(p - s_pl + 1));
    float dx, dy;
    int r = bot_nav_step(b->nav, &s_map, s_ticks, p->x, p->y, dest, x, y, reach, &dx, &dy);
    b->nav_r = r;
    b->nav_dest = dest;
    b->nav_x = x;
    b->nav_y = y;
    b->nav_dx = r == BOT_MOVING ? dx : 0.0f;
    b->nav_dy = r == BOT_MOVING ? dy : 0.0f;
    if (r != BOT_NO_PATH)
        b->no_path = 0;
    if (s_at.trace > 1) /* D2_AT_TRACE=2: cada paso de la navegación */
        printf("  J%d nav (%.2f, %.2f) zona %d -> zona %d (%.1f, %.1f): %d, dir (%.2f, %.2f), trabado %d\n",
               (int)(p - s_pl) + 1, p->x, p->y, zone_at(p->x, p->y), dest, x, y, r, dx, dy, b->stuck);
    switch (r) {
    case BOT_ARRIVED:
        return 1;
    case BOT_MOVING: {
        /* Trabado contra una urna o un barril que tapa el pasillo (el camino
           pasa por ahí: bot_breakable): se usa, como lo haría una persona */
        int u;
        if (b->stuck && p->state == PS_NORMAL && obj_action(p, &u) && u >= 0 && obj_breakable_unit(u)) {
            in->move_x = in->move_y = 0.0f;
            if (b->pulse++ % 3 == 0)
                in->action = in->button[0] = 1;
            return 0;
        }
        at_move(in, dx, dy);
        return 0;
    }
    case BOT_USE: /* la escalera, el portal o la almohadilla */
        if (b->pulse++ % 3 == 0 && at_ui_ready(b)) /* (en red, la A que llegaba ya del otro lado lo devolvía) */
            in->action = in->button[0] = 1;
        return 0;
    default: /* (la demostración, si cayó en un hueco cerrado, sale como entró) */
        if (s_demo && b->no_path > TICK_RATE / 2)
            demo_escape((int)(p - s_pl), in, b, x, y);
        return ++b->no_path > 3 * TICK_RATE ? -1 : 0;
    }
}

/* Para el informe de un paso que falló: qué hacía la navegación de cada bot
   y qué tenía alrededor (un atraso de carga no se repite con el rastro) */
static void at_stuck_report(void)
{
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        const AtBot *b = &s_at.bot[i];
        int a_unit;
        const char *a_does = quest_action(p, &a_unit);
        printf("  J%d bot: navegación %d hacia la zona %d (%.1f, %.1f), dir (%.2f, %.2f), trabado %d, peleando %d, "
               "pegando %d, sin camino %d; A: %s %s\n",
               i + 1, b->nav_r, b->nav_dest, b->nav_x, b->nav_y, b->nav_dx, b->nav_dy, b->stuck, b->fighting,
               b->swinging, b->no_path, a_does ? a_does : "nada", a_does && a_unit >= 0 ? units_id(a_unit) : "");
        if (b->stuck || b->no_path) { /* ¿un rincón cerrado o es cosa del bot? */
            int region, reach = map_reach(p->x, p->y, b->nav_x, b->nav_y, &region);
            printf("    en el mapa %s hasta ahí (%d subtiles a los que se llega)\n", reach ? "hay camino" : "no hay camino",
                   region);
            /* y las salidas que tiene anotadas su zona (si falta la escalera, el bot no tiene ruta) */
            int z = zone_at(p->x, p->y);
            printf("    saltos de la zona %d (%s), de %d:", z, world_zone_name(z), world_warp_count());
            for (int w = 0; w < world_warp_count(); w++) {
                const Warp *wp = world_warp(w);
                if (wp->from == z)
                    printf(" a %s en (%.0f, %.0f)%s;", world_zone_name(wp->to), (wp->x0 + wp->x1) / 2, (wp->y0 + wp->y1) / 2,
                           wp->portal ? " (portal)" : "");
            }
            printf(" armada %d\n", world_zone_loaded(z));
        }
        for (int u = 0; u < units_count(); u++) {
            float x, y;
            units_position(u, 1.0f, &x, &y);
            if (fabsf(x - p->x) < 4.0f && fabsf(y - p->y) < 4.0f)
                printf("    al lado: %s en (%.1f, %.1f)%s\n", units_id(u), x, y,
                       units_alive_enemy(u) ? ", enemigo vivo" : "");
        }
    }
    /* Los enemigos vivos a menos de 64 subtiles del primero (lo que cuenta el
       trono de Baal antes de cada oleada), hasta 8 */
    int shown = 0;
    for (int u = 0; u < units_count() && shown < 8; u++) {
        float x, y;
        units_position(u, 1.0f, &x, &y);
        if (!units_alive_enemy(u) || fabsf(x - s_pl[0].x) >= 64.0f || fabsf(y - s_pl[0].y) >= 64.0f)
            continue;
        const Level *lv = world_level_at(x, y);
        printf("    vivo cerca: %s en (%.1f, %.1f), %s, zona %d\n", units_id(u), x, y, lv ? lv->name : "?", zone_at(x, y));
        shown++;
    }
}

/* Golpear lo que tenga pegado (el juego le pega al más cercano) */
static int at_fight(Player *p, PlayerInput *in, AtBot *b, float range)
{
    int e = units_find_enemy(p->x, p->y, range);
    if (e < 0)
        return 0;
    in->move_x = in->move_y = 0.0f;
    if (b->pulse++ % 4 == 0)
        at_swing(p, in);
    return 1;
}

/* Le habló sin querer a un NPC de afuera: con el espíritu de Izual o Hadriel
   a menos de 4, A habla en lugar de pegar, levantar o abrir. Se cierra el
   diálogo; si no, se queda ahí hasta que se pasa el tiempo del paso (peleando
   al lado del espíritu de Izual). 1 = en eso */
static int at_close_dialog(Player *p, AtBot *b, PlayerInput *in)
{
    if (p->dialog_npc < 0 || p->state >= PS_DYING)
        return 0;
    if (b->pulse++ % 2 == 0)
        in->back = in->button[1] = 1;
    return 1;
}

/* Caminar hasta (x, y) peleando con lo que lo trabe (al bajar una escalera
   puede haber un montón de monstruos esperando): si no avanza con un
   monstruo pegado, pega un segundo y vuelve a intentar. Si la pelea no se
   termina nunca porque un chamán revive a los Caídos, va primero por él. */
static int at_travel(AtBot *b, Player *p, PlayerInput *in, int dest, float x, float y, float reach)
{
    /* (trabado, también lo que está a un paso: las piezas de la prisión de
       hueso de Diablo lo rodean a 2 subtiles y hay que romper una para salir) */
    float range = b->stuck || b->swinging > 0 ? 2.1f : 1.6f;
    int near = units_find_enemy(p->x, p->y, range) >= 0;
    b->fighting = near ? b->fighting + 1 : 0;
    int hunt = -1;
    if (b->fighting > 3 * TICK_RATE)
        hunt = units_find_reviver(p->x, p->y, 12.0f);
    if (hunt >= 0) {
        units_position(hunt, 1.0f, &x, &y);
        dest = zone_at(x, y);
        reach = 1.2f;
    }
    if (near && (b->stuck || b->swinging > 0)) {
        at_fight(p, in, b, range);
        if (++b->swinging > TICK_RATE) { /* a caminar de nuevo (un segundo de prueba) */
            b->swinging = b->stuck = 0;
            b->anchor_x = p->x;
            b->anchor_y = p->y;
        }
        return 0;
    }
    b->swinging = 0;
    if (hunt >= 0) { /* al lado del chamán: pegar (cae el más cercano, después él) */
        if (at_goto(b, p, in, dest, x, y, reach) == 1 && b->pulse++ % 4 == 0)
            at_swing(p, in);
        return 0;
    }
    return at_goto(b, p, in, dest, x, y, reach);
}

/* Un lugar para ir a la zona z (el primer nivel suyo) */
static void at_zone_spot(int z, float *x, float *y)
{
    for (int i = 0; i < world_level_count(); i++) {
        const Level *lv = world_level(i);
        if (lv->zone != z)
            continue;
        *x = (lv->x0 + lv->x1) * TILE_SUB / 2.0f;
        *y = (lv->y0 + lv->y1) * TILE_SUB / 2.0f;
        if (world_zone_loaded(z))
            level_free_spot(lv, x, y);
        return;
    }
}

static int at_waypoint_unit_in(int z)
{
    for (int k = 0; k < WP_MAX; k++)
        if (s_wp_unit[k] >= 0 && units_zone(s_wp_unit[k]) == z)
            return s_wp_unit[k];
    return -1;
}

/* Un paso del bot para el jugador i: 1 = terminó, 0 = sigue, -1 = falló */
static int at_player_step(int i, const AtStep *st, PlayerInput *in)
{
    Player *p = &s_pl[i];
    AtBot *b = &s_at.bot[i];
    if (p->state >= PS_DYING)
        return 0;
    if (p->reading) { /* leyó algo sin querer (A peleando al lado del diario): cerrarlo */
        if (p->talk_ticks > 20 && b->pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return 0;
    }
    switch (st->kind) {
    case AT_GOTO_LEVEL: {
        const Level *lv = at_level(st->arg);
        if (!lv)
            return -1;
        if (p->level == lv)
            return 1;
        float x = (lv->x0 + lv->x1) * TILE_SUB / 2.0f, y = (lv->y0 + lv->y1) * TILE_SUB / 2.0f;
        if (world_zone_loaded(lv->zone) && zone_at(p->x, p->y) == lv->zone)
            level_free_spot(lv, &x, &y);
        return at_travel(b, p, in, lv->zone, x, y, 1.5f) < 0 ? -1 : 0;
    }
    case AT_ACTIVATE_WP: {
        if ((p->waypoints >> st->num) & 1) {
            if (p->wp_open && b->pulse++ % 2 == 0 && at_ui_ready(b))
                in->back = in->button[1] = 1; /* cerrar el panel que se abrió solo */
            return p->wp_open ? 0 : 1;
        }
        int z = world_waypoint_zone(st->num), u = at_waypoint_unit_in(z);
        float x = p->x, y = p->y;
        if (u < 0) { /* la zona no está armada: ir hasta ella */
            at_zone_spot(z, &x, &y);
            return at_travel(b, p, in, z, x, y, 1.5f) < 0 ? -1 : 0;
        }
        units_position(u, 1.0f, &x, &y);
        return at_travel(b, p, in, z, x, y, 2.0f) < 0 ? -1 : 0;
    }
    case AT_WAYPOINT: {
        int dest = world_waypoint_zone(st->num);
        if (zone_at(p->x, p->y) == dest && !p->wp_open)
            return 1;
        /* Al pueblo, con un portal si tiene (como un jugador después de una
           misión): lo abre al lado y entra, en vez de ir hasta un waypoint */
        /* (en la cumbre con el rito empezado no: el portal hace volver a los Antiguos a sus estatuas) */
        if (!p->wp_open && st->num == wp_first(s_act) && p->level && !p->level->town && tp_find(p) >= 0 &&
            p->dialog_npc < 0 && !(p->level->id == 120 && p->ancients_q == 2)) {
            int i = (int)(p - s_pl);
            if (!at_ui_ready(b)) /* (en red, la A de un golpe que llegaba ya en el pueblo, al lado del portal, lo devolvía) */
                return 0;
            if (tp_cast(p, tp_find(p)) && tp_warp(PORTAL_TP_FIELD(i))) {
                use_warp(p, tp_warp(PORTAL_TP_FIELD(i)));
                printf("  J%d vuelve al pueblo por su portal\n", i + 1);
            }
            return 0;
        }
        if (p->wp_open) { /* elegir el destino en el panel y viajar */
            if (b->pulse++ % 2 || !at_ui_ready(b))
                return 0;
            if (p->wp_sel != st->num)
                in->nav_down = 1;
            else
                in->action = in->button[0] = 1;
            return 0;
        }
        /* El waypoint activado más cerca (en zonas) */
        int best = -1, best_hops = 1 << 20;
        for (int k = 0; k < WP_MAX; k++) {
            if (!((p->waypoints >> k) & 1) || world_waypoint_zone(k) < 0)
                continue;
            int hops = 0, z = zone_at(p->x, p->y), wz = world_waypoint_zone(k);
            while (z != wz && z >= 0 && hops < 64) {
                z = world_route_next(z, wz);
                hops++;
            }
            if (z == wz && hops < best_hops) {
                best = k;
                best_hops = hops;
            }
        }
        if (best < 0)
            return -1;
        int wz = world_waypoint_zone(best), u = at_waypoint_unit_in(wz);
        float x = p->x, y = p->y;
        if (u < 0) {
            at_zone_spot(wz, &x, &y);
            return at_travel(b, p, in, wz, x, y, 1.5f) < 0 ? -1 : 0;
        }
        units_position(u, 1.0f, &x, &y);
        if (waypoint_near(p) == best) {
            if (b->pulse++ % 2 == 0 && at_ui_ready(b))
                in->action = in->button[0] = 1;
            return 0;
        }
        return at_travel(b, p, in, wz, x, y, 2.0f) < 0 ? -1 : 0;
    }
    default:
        return 1;
    }
}

/* El segundo jugador sigue al primero (y le pega a lo que tenga al lado) */
static void at_follow(int i, PlayerInput *in)
{
    Player *p = &s_pl[i], *lead = &s_pl[0];
    AtBot *b = &s_at.bot[i];
    if (p->reading) { /* leyó algo sin querer: cerrarlo */
        if (p->talk_ticks > 20 && b->pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return;
    }
    if (at_close_dialog(p, b, in)) /* (el que acompaña no habla con nadie) */
        return;
    if (p->state >= PS_DYING || p->inv_open)
        return;
    if (p->wp_open) {
        if (b->pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return;
    }
    float dx = lead->x - p->x, dy = lead->y - p->y;
    int same = zone_at(p->x, p->y) == zone_at(lead->x, lead->y);
    if (same && dx * dx + dy * dy < 5.0f * 5.0f) {
        at_fight(p, in, b, 1.6f);
        return;
    }
    if (at_fight(p, in, b, 1.2f))
        return;
    at_goto(b, p, in, zone_at(lead->x, lead->y), lead->x, lead->y, 3.0f);
}

/* ¿Se cumple la condición del paso? (varias, separadas por '+') */
static int at_check(const char *check, char *why, size_t cap)
{
    if (!check)
        return 1;
    char name[32];
    int want = 0;
    snprintf(name, sizeof name, "%.*s", (int)strcspn(check, "+"), check);
    if (check[strlen(name)] == '+' && !at_check(check + strlen(name) + 1, why, cap))
        return 0;
    const char *val = "";
    char *eq = strchr(name, '=');
    if (eq) {
        want = atoi(eq + 1);
        val = eq + 1;
        *eq = '\0';
    }
    if (strcmp(name, "eco") == 0 || strcmp(name, "eco2") == 0 || strcmp(name, "ecoend") == 0 || strcmp(name, "ecochest") == 0 ||
        strcmp(name, "ecoexit") == 0) {
        int second = getenv("D2_ECO_WAVE") && atoi(getenv("D2_ECO_WAVE")) >= 26;
        int ok = name[3] == 0 ? s_er.wave >= want
               : name[3] == '2' ? !second || s_er.wave >= want
               : name[3] == 'e' ? s_er.phase == EP_LOOT
               : name[3] == 'c' ? s_er.phase == EP_LOOT && s_er.chest < 0
                                : s_er.exited;
        if (!ok)
            snprintf(why, cap, "%s no se cumple (oleada %d, fase %d)", check, s_er.wave, s_er.phase);
        return ok;
    }
    for (int i = 0; i < s_num_players; i++) {
        const Player *p = &s_pl[i];
        int ok = 1, have = 0;
        if (strcmp(name, "den_started") == 0) ok = p->den_started;
        else if (strcmp(name, "den_done") == 0) ok = p->den_done;
        else if (strcmp(name, "den_rewarded") == 0) ok = p->den_rewarded || i > 0;
        else if (strcmp(name, "raven_started") == 0) ok = p->raven_started || i > 0;
        else if (strcmp(name, "raven_done") == 0) ok = p->raven_done;
        else if (strcmp(name, "raven_rewarded") == 0) ok = p->raven_rewarded || i > 0;
        else if (strcmp(name, "merc") == 0) ok = i > 0 || (p->merc_name && units_merc_alive(p->merc_unit));
        else if (strcmp(name, "wp") == 0) ok = (p->waypoints >> want) & 1;
        else if (strcmp(name, "cain") == 0) ok = (have = p->cain_q) >= want || (i > 0 && want != 4 && want != 5);
        else if (strcmp(name, "malus") == 0) ok = (have = p->malus_q) >= want || i > 0;
        else if (strcmp(name, "tower") == 0) ok = (have = p->tower_q) >= want || (i > 0 && want == 3);
        else if (strcmp(name, "andy") == 0) ok = (have = p->andy_q) >= want;
        /* Las del Acto 2: el que acompaña tiene las que son de los dos (lo
           que se cuenta en el pueblo lo cumple el que habla, y lo de llegar
           a un lugar, cuando llega) */
        else if (strcmp(name, "rada") == 0) ok = (have = p->rada_q) >= want || (i > 0 && want != 2);
        else if (strcmp(name, "staff") == 0) ok = (have = p->staff_q) >= want || (i > 0 && want != 3);
        else if (strcmp(name, "sun") == 0) ok = (have = p->sun_q) >= want || (i > 0 && want == 4);
        else if (strcmp(name, "arcane") == 0) ok = (have = p->arcane_q) >= want || (i > 0 && want == 3);
        else if (strcmp(name, "summ") == 0) ok = (have = p->summ_q) >= want || (i > 0 && want != 2);
        else if (strcmp(name, "tombs") == 0) ok = (have = p->tombs_q) >= want || (i > 0 && (want == 1 || want >= 5));
        /* Las del Acto 3: lo que pide un NPC y lo que pasa en el mundo es de
           los dos; las recompensas, del que habla */
        else if (strcmp(name, "hratli") == 0) ok = i > 0 || (p->a3_flags & A3F_HRATLI);
        else if (strcmp(name, "tome") == 0) ok = (have = p->tome_q) >= want || (i > 0 && want == 3);
        else if (strcmp(name, "will") == 0) ok = (have = p->will_q) >= want || (i > 0 && want == 2);
        else if (strcmp(name, "blade") == 0) /* (si el primero ya tenía la Gidbinn, Hratli no la pide) */
            ok = (have = p->blade_q) >= want || (i > 0 && want == 1);
        else if (strcmp(name, "bird") == 0) ok = (have = p->bird_q) >= want || i > 0; /* (su estatuilla queda en el piso) */
        else if (strcmp(name, "temple") == 0) ok = (have = p->temple_q) >= want || (i > 0 && want == 4);
        else if (strcmp(name, "meph") == 0) ok = (have = p->meph_q) >= want;
        else if (strcmp(name, "freehire") == 0) ok = i > 0 || (p->a3_flags & A3F_HIRED);
        else if (strcmp(name, "lever") == 0) ok = s_a3_lever;
        else if (strcmp(name, "decoy") == 0) /* el señuelo ya se tocó (peleando al lado, A) o alguien tiene la Gidbinn */
            ok = (s_a3_opened & a3_once_bit("GidBInnDecoy")) || s_a3_gid_boss >= 0 || player_has(&s_pl[0], "g33") ||
                 player_has(&s_pl[1], "g33") || p->blade_q >= 3;
        /* Las del Acto 4: los pedidos y lo que pasa en el mundo, de los dos; las recompensas, del que habla */
        else if (strcmp(name, "izual") == 0) ok = (have = p->izual_q) >= want || (i > 0 && (want == 2 || want == 5));
        else if (strcmp(name, "forge") == 0) ok = (have = p->forge_q) >= want || (i > 0 && want != 4);
        else if (strcmp(name, "diablo") == 0) ok = (have = p->diablo_q) >= want || (i > 0 && want == 2);
        else if (strcmp(name, "stone") == 0) ok = (p->a4_flags & A4F_STONE) != 0;
        else if (strcmp(name, "seal") == 0) ok = (s_a4_seals >> (want - 1)) & 1;
        else if (strcmp(name, "portal") == 0) ok = (p->a4_flags & A4F_PORTAL) != 0;
        else if (strcmp(name, "end") == 0) ok = (p->a4_flags & A4F_END) != 0;
        /* Las del Acto 5: igual (los pedidos y lo que pasa en el mundo, de los dos; las recompensas, del que habla) */
        else if (strcmp(name, "shenk") == 0) ok = (have = p->shenk_q) >= want || (i > 0 && want != 2);
        else if (strcmp(name, "rescue") == 0) ok = (have = p->rescue_q) >= want || (i > 0 && want == 3);
        else if (strcmp(name, "rescued") == 0) ok = (have = p->rescued) >= want;
        else if (strcmp(name, "anya") == 0) ok = (have = p->anya_q) >= want || (i > 0 && (want == 3 || want == 5));
        else if (strcmp(name, "nihl") == 0) ok = (have = p->nihl_q) >= want || (i > 0 && (want == 2 || want == 4));
        else if (strcmp(name, "ancients") == 0) ok = (have = p->ancients_q) >= want;
        else if (strcmp(name, "baal") == 0) ok = (have = p->baal_q) >= want || (i > 0 && want == 2);
        else if (strcmp(name, "socketed") == 0) ok = i > 0 || p->shenk_q >= 4;
        else if (strcmp(name, "personal") == 0) { /* (sin objetos mágicos no hay nada que personalizar) */
            ok = i > 0 || p->nihl_q >= 5;
            int saved = s_pl[i].imbue_mode, list[BAG_MAX + SLOT_COUNT];
            s_pl[i].imbue_mode = 2;
            ok |= imbue_candidates(p, list) == 0;
            s_pl[i].imbue_mode = saved;
        }
        else if (strcmp(name, "resist") == 0) ok = i > 0 || (p->a5_flags & A5F_RESIST);
        else if (strcmp(name, "anyatold") == 0) ok = i > 0 || (p->a5_flags & A5F_ANYA_TOLD);
        else if (strcmp(name, "end5") == 0) ok = (p->a5_flags & A5F_END) != 0;
        else if (strcmp(name, "wield") == 0) ok = i > 0 || (p->equip[SLOT_WEAPON].type && !strcmp(p->equip[SLOT_WEAPON].type->code, val));
        else if (strcmp(name, "life") == 0) ok = i > 0 || (p->a3_flags & A3F_LIFE);
        else if (strcmp(name, "has") == 0) ok = i > 0 || player_has(p, val); /* (cada uno tiene su pieza en el piso) */
        else if (strcmp(name, "book") == 0) ok = i > 0 || !(p->a2_flags & A2F_BOOK);
        else if (strcmp(name, "gambled") == 0) ok = i > 0 || p->gold < s_at.gold0;
        else if (strcmp(name, "symbol") == 0) { /* el registro: la página de las Siete Tumbas con el glifo */
            int st;
            const char *g = quest_info(p, 2, 5, &st);
            ok = g && strcmp(g, "qstsa2q61a") == 0 && tomb_rune(world_staff_tomb()) >= 1;
        }
        else if (strcmp(name, "act") == 0) { /* en el pueblo de ese acto (p->level se pone en el tick que sigue) */
            const Level *lv = world_level_at(p->x, p->y);
            ok = (have = p->act) >= want && s_act >= want && lv && lv->town;
        }
        else if (strcmp(name, "rare") == 0) {
            ok = i > 0;
            for (int s = 0; s < SLOT_COUNT; s++)
                ok |= p->equip[s].quality == QUALITY_RARE;
            for (int k = 0; k < p->bag_count; k++)
                ok |= p->bag[k].quality == QUALITY_RARE;
        } else if (strcmp(name, "saved") == 0) {
            char path[512], line[256], *mem = NULL;
            size_t mem_len = 0;
            ok = s_demo; /* la demostración no guarda */
            FILE *f = NULL;
            if (net_role() != NET_OFF) {
                /* En red cada máquina guarda solo los suyos (y la prueba, sin D2_NET_SLOTS, a
                   ninguno): se revisa lo que se guarda, igual en todas las máquinas */
                FILE *w = open_memstream(&mem, &mem_len);
                if (w) {
                    save_character_to(w, p);
                    fclose(w);
                }
                f = mem && mem_len > 0 ? fmemopen(mem, mem_len, "r") : NULL;
            } else if (save_path(p->slot, path, sizeof path)) {
                f = fopen(path, "r");
            }
            if (f) {
                int q[7];
                while (fgets(line, sizeof line, f))
                    if (s_at.act == 5) /* las seis misiones (con las recompensas del que habla y el final) */
                        ok |= sscanf(line, "act5 %d %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6]) == 7 &&
                              (i > 0 || (q[0] >= 4 && q[1] >= 3 && q[2] >= 5 && q[3] >= 4 && q[4] >= 3 && q[5] >= 6));
                    else if (s_at.act == 4) /* las tres misiones (Izual cobrado, la Forja contada a Caín, Diablo) */
                        ok |= sscanf(line, "act4 %d %d %d %d", &q[0], &q[1], &q[2], &q[3]) == 4 &&
                              (i > 0 || (q[0] >= 5 && q[1] >= 5 && q[2] >= 4));
                    else if (s_at.act == 3) /* las seis misiones (el pájaro, el Orbe, el tomo, el Consejo y Mefisto) */
                        ok |= sscanf(line, "act3 %d %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6]) == 7 &&
                              (i > 0 || (q[0] >= 6 && q[2] >= 3 && q[3] >= 3 && q[4] >= 4 && q[5] >= 2));
                    else if (s_at.act == 2) /* las seis misiones (Radament contada a Atma; Meshif) */
                        ok |= sscanf(line, "act2 %d %d %d %d %d %d %d", &q[0], &q[1], &q[2], &q[3], &q[4], &q[5], &q[6]) == 7 &&
                              (i > 0 || (q[0] >= 3 && q[6] >= 6));
                    else
                        ok |= strncmp(line, "act1 6 4 3 3", 12) == 0 || (i > 0 && strncmp(line, "act1 ", 5) == 0);
                fclose(f);
            }
            free(mem);
        }
        if (!ok) {
            snprintf(why, cap, "J%d: %s no se cumple (tiene %d)", i + 1, check, have);
            return 0;
        }
    }
    return 1;
}

static int at_step_timeout(const AtStep *st)
{
    /* En la demostración se pelea de verdad: los jefes tardan más */
    if (s_demo && (st->kind == AT_KILL || st->kind == AT_CLEAR))
        return 30 * 60 * TICK_RATE;
    switch (st->kind) {
    case AT_GOTO_LEVEL: return 60 * 60 * TICK_RATE; /* hasta una hora de juego caminando */
    case AT_CLEAR:      return 30 * 60 * TICK_RATE;
    case AT_WAYPOINT:   return 10 * 60 * TICK_RATE;
    case AT_ECO:        return 40 * 60 * TICK_RATE; /* (hasta cinco oleadas) */
    default:            return 8 * 60 * TICK_RATE;
    }
}

/* ¿Entra ese objeto en algún lugar de la mochila (sin contar el objeto
   `ignore`, el que sale de ella para ponerse)? */
static int at_bag_room_but(const Player *p, const ItemType *t, int ignore)
{
    for (int x = 0; x + t->inv_w <= BAG_W; x++)
        for (int y = 0; y + t->inv_h <= BAG_H; y++)
            if (bag_fits(p, t, x, y, ignore))
                return 1;
    return 0;
}

static int at_bag_room(const Player *p, const ItemType *t)
{
    return at_bag_room_but(p, t, -1);
}

/* Para que entre un objeto de misión: deja en el piso lo más barato de la
   mochila (no de misión ni pociones). Devuelve dónde quedó en el piso, -1 si
   no hacía falta y -2 si no hay nada que dejar */
static void demo_forget_item(const Player *p, int ground); /* (demobot.inc) */

static int at_make_room(Player *p, const ItemType *t, float qx, float qy)
{
    if (at_bag_room(p, t))
        return -1;
    int drop = -1, worst = 1 << 30;
    for (int k = 0; k < p->bag_count; k++) {
        const ItemType *bt = p->bag[k].type;
        if (is_quest_item(bt))
            continue;
        /* lo que menos sirve; las pociones al final (si no hay otra cosa: la
           Hechicera con la mochila de pociones no levantaba la Gidbinn) */
        int v = (bt->cost > 0 ? bt->cost : 1) + (is_potion(bt) ? 1 << 24 : 0);
        if (v < worst) {
            worst = v;
            drop = k;
        }
    }
    if (drop < 0) {
        static uint32_t s_told;
        if (s_ticks - s_told > 30 * TICK_RATE) { /* para ver qué tenía */
            s_told = s_ticks;
            printf("  J%d no tiene lugar para %s y no hay qué dejar:", (int)(p - s_pl) + 1, t->name);
            for (int k = 0; k < p->bag_count; k++)
                printf(" %s;", p->bag[k].type->name);
            printf("\n");
        }
        return -2;
    }
    Item it = p->bag[drop];
    bag_remove(p, drop);
    printf("  J%d deja en el piso %s para levantar lo de la misión\n", (int)(p - s_pl) + 1, it.type->name);
    /* Del otro lado del de la misión y lejos del alcance de A (1,8): si no, al
       apretar A levantaba de nuevo lo que soltó (más cerca) y no terminaba */
    float dx = p->x - qx, dy = p->y - qy, d = sqrtf(dx * dx + dy * dy);
    if (d < 0.1f)
        dx = dy = d = 1.0f;
    return items_ground_add(&it, p->x + dx / d * 2.2f, p->y + dy / d * 2.2f);
}

/* Un objeto de misión que quedó en el piso al lado (la mochila llena al
   tocar el árbol de Inifuss o el Mazo): lo levanta antes de seguir; si no le
   entra, deja en el piso lo que menos le sirve. 1 = está en eso. */
static int at_quest_pickup(Player *p, AtBot *b, PlayerInput *in)
{
    int best = -1;
    float bx = 0.0f, by = 0.0f, bd = 14.0f * 14.0f; /* (peleando al lado del cofre se aleja) */
    for (int k = 0; k < items_ground_count(); k++) {
        float x, y;
        const Item *g = items_ground_item(k, &x, &y);
        float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
        if (g && g->type && is_quest_item(g->type) && !player_has(p, g->type->code) && d < bd &&
            (strcmp(g->type->code, "ass") != 0 || (p->a2_flags & A2F_BOOK))) { /* el libro, si tiene uno por leer */
            best = k;
            bd = d;
            bx = x;
            by = y;
        }
    }
    if (best < 0) /* (con monstruos cerca también: si no, pasaba el paso y el Mazo quedaba en el piso) */
        return 0;
    const Item *g = items_ground_item(best, NULL, NULL);
    int room = at_make_room(p, g->type, bx, by);
    if (room == -2)
        return 0;
    if (room >= 0) {
        demo_forget_item(p, room); /* que la demo no lo vuelva a levantar (una poción) */
        return 1;
    }
    /* A levanta lo más cerca (a menos de 1,8): si hay otra cosa más cerca (el
       botín de la escolta del fetiche, al lado de la Gidbinn), se para encima */
    if (s_at.trace && s_ticks % 25 == 0) { /* D2_AT_TRACE: por qué no lo levanta */
        int nb = items_ground_nearest(p->x, p->y, 1.8f, 0);
        const Item *ng = nb >= 0 ? items_ground_item(nb, NULL, NULL) : NULL;
        printf("  J%d va por %s a %.2f; lo más cerca: %s; lugar %d; mochila %d\n", (int)(p - s_pl) + 1, g->type->name,
               sqrtf(bd), ng ? ng->type->name : "-", at_bag_room(p, g->type), p->bag_count);
    }
    /* (al lado alcanza: la estatuilla de jade cayó una vez en un subtile que el
       pie del jugador no pisa, con todo lo de alrededor libre, y pararse
       encima no se podía) */
    if (bd < 1.5f * 1.5f && (items_ground_nearest(p->x, p->y, 1.8f, 0) == best || bd < 0.15f * 0.15f)) {
        in->move_x = in->move_y = 0.0f;
        if (b->pulse++ % 3 == 0)
            in->action = in->button[0] = 1;
        return 1;
    }
    return at_travel(b, p, in, zone_at(bx, by), bx, by, bd < 1.5f * 1.5f ? 0.1f : 1.0f) >= 0; /* peleando con lo que lo trabe (Bloodwitch, al lado del cubo) */
}

/* ¿Hay en el piso, a menos de r, un objeto con ese código? */
static int at_ground_has(const Player *p, const char *code, float r)
{
    for (int k = 0; k < items_ground_count(); k++) {
        float x, y;
        const Item *g = items_ground_item(k, &x, &y);
        if (g && g->type && strcmp(g->type->code, code) == 0 && (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < r * r)
            return 1;
    }
    return 0;
}

/* El más cercano en el piso con ese código (en las zonas armadas); 0 = no hay */
static int at_ground_find(const Player *p, const char *code, float *gx, float *gy)
{
    float bd = -1.0f;
    for (int k = 0; k < items_ground_count(); k++) {
        float x, y;
        const Item *g = items_ground_item(k, &x, &y);
        float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
        if (g && g->type && strcmp(g->type->code, code) == 0 && (bd < 0 || d < bd)) {
            bd = d;
            *gx = x;
            *gy = y;
        }
    }
    return bd >= 0;
}

/* ¿Ya está en el cubo? */
static int at_cube_has(const Player *p, const char *code)
{
    for (int k = 0; k < p->cube_count; k++)
        if (p->cube[k].type && strcmp(p->cube[k].type->code, code) == 0)
            return 1;
    return 0;
}

/* En la demostración pudo haber vuelto al pueblo a curarse y la zona del
   jefe o del cofre se borró: volver a su nivel (el del paso anterior que
   iba a algún lado) y buscarlo de nuevo. 0 = yendo, -1 = no hay a dónde */
static int at_back_to_level(Player *p, AtBot *b, PlayerInput *in)
{
    for (int k = s_at.step - 1; s_demo && k >= 0; k--)
        if (s_at.steps[k].kind == AT_GOTO_LEVEL) {
            const Level *lv = at_level(s_at.steps[k].arg);
            if (!lv || p->level == lv)
                break;
            float lx = (lv->x0 + lv->x1) * TILE_SUB / 2.0f, ly = (lv->y0 + lv->y1) * TILE_SUB / 2.0f;
            if (world_zone_loaded(lv->zone) && zone_at(p->x, p->y) == lv->zone)
                level_free_spot(lv, &lx, &ly);
            return at_travel(b, p, in, lv->zone, lx, ly, 1.5f) < 0 ? -1 : 0;
        }
    return -1;
}

/* Como una persona, al pasar abre los cofres, ataúdes, urnas, barriles y
   cadáveres que tiene cerca (objects.inc), sin monstruos al lado ni un objeto
   de misión que A tocaría antes; al que no llega en 6 s lo deja. 1 = en eso. */
static int at_open_objects(Player *p, AtBot *b, PlayerInput *in)
{
    static int cur = -1;
    static uint32_t since, seen;
    int qobj;
    if (!p->level || p->level->town || p->state != PS_NORMAL || p->dialog_npc >= 0 || p->inv_open || p->wp_open ||
        units_find_enemy(p->x, p->y, 8.0f) >= 0 || quest_action(p, &qobj))
        return 0;
    int u = obj_bot_target(p, 6.0f);
    if (u < 0)
        return 0;
    if (u != cur || s_ticks - seen > TICK_RATE) { /* (un monstruo que se asoma un momento no lo reinicia) */
        cur = u;
        since = s_ticks;
    }
    seen = s_ticks;
    if (s_at.trace && s_ticks % 50 == 0) {
        float ux, uy;
        units_position(u, 1.0f, &ux, &uy);
        printf("  J1 va a abrir %s en (%.1f, %.1f), hace %u ticks\n", units_id(u), ux, uy, s_ticks - since);
    }
    if (s_ticks - since > 6 * TICK_RATE) { /* no llega (detrás de una pared) */
        if (s_at.trace)
            printf("  J1 deja %s: no llega\n", units_id(u));
        obj_bot_skip(u);
        cur = -1;
        return 0;
    }
    int near;
    if (obj_action(p, &near) && near == u) {
        in->move_x = in->move_y = 0.0f;
        if (b->pulse++ % 3 == 0)
            in->action = in->button[0] = 1;
        return 1;
    }
    float x, y;
    units_position(u, 1.0f, &x, &y);
    find_free_spot(&x, &y); /* al lado: lo que ocupa no se camina */
    if (at_travel(b, p, in, zone_at(x, y), x, y, 0.4f) < 0) {
        obj_bot_skip(u);
        cur = -1;
        return 0;
    }
    return 1;
}

/* Lo que hace el jugador 1 en el paso actual; 1 = terminó, -1 = falló */
static int at_lead_step(const AtStep *st, PlayerInput *in)
{
    Player *p = &s_pl[0];
    AtBot *b = &s_at.bot[0];
    if (p->reading && st->kind != AT_TOUCH && st->kind != AT_STONES) {
        /* Leyó sin querer (A peleando al lado del diario de Horazon): se cierra,
           si no se queda leyendo y no camina */
        if (p->talk_ticks > 20 && b->pulse++ % 2 == 0)
            in->back = in->button[1] = 1;
        return 0;
    }
    if (st->kind != AT_TALK && at_close_dialog(p, b, in))
        return 0;
    /* Lo que suelta un monstruo en el Acto 3 en cualquier paso (la estatuilla
       de jade, el primer jefe; el Mangual, el primer consejero): como una
       persona, se levanta al verlo */
    if (s_at.act == 3 && ((p->bird_q == 0 && at_ground_has(p, "j34", 14.0f)) ||
                          (a3_needs_part(p, "qf1") && at_ground_has(p, "qf1", 14.0f))) &&
        at_quest_pickup(p, b, in))
        return 0;
    if (st->kind != AT_TALK && at_open_objects(p, b, in))
        return 0;
    switch (st->kind) {
    case AT_TALK: {
        int npc = -1;
        float x, y;
        if (s_at.phase == 0) { /* ir hasta el NPC */
            if (p->dialog_npc >= 0) {
                s_at.phase = 1;
                return 0;
            }
            int town = world_zone_at(&s_map, s_town_x, s_town_y), id;
            if (units_find(st->arg, &x, &y) != 0) /* (el espíritu de Izual sale un momento después) */
                return s_at.step_ticks > 5 * TICK_RATE ? -1 : 0;
            if (units_find_all(st->arg, &id, 1) == 1 && units_zone(id) >= 0) /* Tyrael está en la guarida */
                town = units_zone(id);
            npc = units_find_npc(p->x, p->y, 4.0f);
            if (s_at.trace && s_ticks % 50 == 0)
                printf("  J1 va a %s en (%.1f, %.1f); al lado: %s\n", st->arg, x, y, npc >= 0 ? units_id(npc) : "nadie");
            if (npc >= 0 && strcmp(units_id(npc), st->arg) == 0) {
                if (b->pulse++ % 3 == 0 && at_ui_ready(b))
                    in->action = in->button[0] = 1;
                return 0;
            }
            return at_goto(b, p, in, town, x, y, 2.0f) < 0 ? -1 : 0;
        }
        if (p->dialog_npc < 0) { /* se cerró: listo (si contó otra cosa primero, se le habla de nuevo) */
            char why[160];
            if (st->check && !at_check(st->check, why, sizeof why) && s_at.talks < 3) {
                s_at.talks++;
                s_at.phase = 0;
                return 0;
            }
            /* lo que dio y no entró en la mochila quedó en el piso (la poción de
               Malah: sin ella, en el Frozen River no se descongelaba a Anya) */
            if (at_quest_pickup(p, b, in))
                return 0;
            return 1;
        }
        if (s_at.phase == 1) { /* elegir la opción */
            if (b->pulse++ % 2 || !at_ui_ready(b))
                return 0;
            int opts[8], n = npc_options(p, units_id(p->dialog_npc), opts), want = -1;
            for (int k = 0; k < n; k++)
                if (opts[k] == st->num)
                    want = k;
            if (want < 0) {
                snprintf(s_at.fail_msg, sizeof s_at.fail_msg, "%s no ofrece \"%s\"", st->arg, k_option_names[st->num]);
                return -1;
            }
            if (p->dialog_sel != want)
                in->nav_down = 1;
            else {
                in->action = in->button[0] = 1;
                s_at.phase = 2;
                s_at.step_start = s_at.step_ticks;
            }
            return 0;
        }
        int waited = s_at.step_ticks - s_at.step_start;
        if (!at_ui_ready(b))
            return 0;
        /* Lo que abre la opción: contratar la primera, imbuir o comprar el primero */
        if ((p->hire_open || p->imbue_open || p->gambling) && s_at.phase == 2 && waited > 5) {
            in->action = in->button[0] = 1;
            s_at.phase = 3;
            s_at.step_start = s_at.step_ticks;
            return 0;
        }
        if (waited < 12 || waited % 3)
            return 0;
        in->back = in->button[1] = 1; /* cerrar (la lista y después el diálogo) */
        return 0;
    }
    case AT_WAVES: { /* las oleadas: lo más cercano de lo que cuenta el trono; sin nadie, cerca del trono */
        char why[160];
        if (at_check(st->check, why, sizeof why))
            return 1;
        const Level *lv = world_level_named(st->arg);
        int target = -1;
        float best = 1e9f, x, y, bx, by;
        int has_throne = units_find("baalthrone", &bx, &by) == 0;
        for (int u = 0; u < units_count(); u++) {
            if (!units_alive_enemy(u)) /* (Baal en su trono no cuenta: no se lo puede matar, killable 0) */
                continue;
            units_position(u, 1.0f, &x, &y);
            /* Baal espera a que no quede nadie a 64 subtiles de él en su zona
               (monai5.inc): esos primero; el resto del nivel no lo frena (antes
               el bot recorría todo el Trono y el paso casi no entraba en el tope) */
            if (has_throne ? zone_at(x, y) != zone_at(bx, by) || (x - bx) * (x - bx) + (y - by) * (y - by) >= 64.0f * 64.0f
                           : world_level_at(x, y) != lv)
                continue;
            float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
            if (d < best) {
                best = d;
                target = u;
            }
        }
        if (target < 0) { /* sin nadie: cerca del trono, donde Baal ve al grupo */
            if (units_find("baalthrone", &x, &y) != 0)
                return 0;
            return at_travel(b, p, in, zone_at(x, y), x, y + 12.0f, 3.0f) < 0 ? -1 : 0;
        }
        units_position(target, 1.0f, &x, &y);
        if ((x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < MELEE_RANGE * MELEE_RANGE * 0.8f) {
            if (b->pulse++ % 4 == 0)
                at_swing(p, in);
            return 0;
        }
        if (at_fight(p, in, b, 1.4f))
            return 0;
        if (at_goto(b, p, in, zone_at(x, y), x, y, 1.2f) < 0) {
            units_damage(target, 1 << 20);
            s_at.unreachable++;
        }
        return 0;
    }
    case AT_KILL: case AT_CLEAR: {
        int target = -1;
        if (st->kind == AT_KILL && strcmp(st->arg, "#jade") == 0) {
            /* La estatuilla de jade: el jefe que la tiene (el primero que
               apareció en el acto), como una persona que lo termina cruzando, y
               levantarla (si la soltó lejos, porque lo mató otro, se va a buscar) */
            float gx = 0.0f, gy = 0.0f;
            if (p->bird_q > 0 || player_has(p, "j34") || player_has(p, "g34"))
                return 1;
            if (at_ground_find(p, "j34", &gx, &gy)) {
                if (at_quest_pickup(p, b, in) || at_travel(b, p, in, zone_at(gx, gy), gx, gy, 1.0f) >= 0)
                    return 0;
                /* Cayó donde no se llega (el jefe estaba en un rincón al que el
                   bot no llegó y se lo dio por muerto): como con él, se la da y
                   se anota */
                int region, reach = map_reach(p->x, p->y, gx, gy, &region);
                printf("  J1 no llega a la estatuilla en (%.1f, %.1f): en el mapa %s; se la da\n", gx, gy,
                       reach ? "hay camino (es cosa del bot)" : "no hay camino");
                for (int k = items_ground_count() - 1; k >= 0; k--) {
                    float ix, iy;
                    const Item *g = items_ground_item(k, &ix, &iy);
                    if (g && g->type && strcmp(g->type->code, "j34") == 0 && fabsf(ix - gx) < 2.0f && fabsf(iy - gy) < 2.0f)
                        items_ground_remove(k);
                }
                for (int i = 0; i < s_num_players; i++)
                    if (!player_has(&s_pl[i], "j34"))
                        give_quest_item(&s_pl[i], "j34");
                s_at.unreachable++;
                return 0;
            }
            if (!a3_bird_open() || s_a3_bird_boss < 0 || !units_alive_enemy(s_a3_bird_boss))
                return 1;
            target = s_a3_bird_boss;
            if (s_at.step_ticks > TICK_RATE * 120) { /* no llega: como los inalcanzables */
                units_damage(target, 1 << 20);
                s_at.unreachable++;
                return 0;
            }
        } else if (st->kind == AT_KILL && strncmp(st->arg, "Ancient Barbarian", 17) == 0 && p->ancients_q == 1) {
            /* el rito se reinició (un portal en la cumbre, o murieron todos allá): de nuevo el altar */
            static const AtStep k_altar = { AT_TOUCH, "AncientsAltar", 0, NULL, "Tocar el altar de nuevo", 0 };
            int r = at_lead_step(&k_altar, in);
            return r < 0 ? r : 0;
        } else if (st->kind == AT_KILL) {
            int ids[4], n = units_find_all(st->arg, ids, 4);
            for (int k = 0; k < n && target < 0; k++)
                if (units_alive_enemy(ids[k]))
                    target = ids[k];
            if (target < 0) {
                for (int k = 0; k < n; k++)
                    if (units_is_corpse(ids[k]))
                        return 1;
                if (n == 0 && s_at.step_ticks < 5 * TICK_RATE) /* (Diablo sale un momento después del último jefe) */
                    return 0;
                return at_back_to_level(p, b, in); /* no está en las zonas armadas */
            }
        } else {
            /* Limpiar el nivel: el más cercano, y después ese mientras viva. Si
               se elegía de nuevo a cada tick, con dos casi a la misma distancia
               y los caminos saliendo para el lado del otro iba y venía sin
               llegar a ninguno (la Guarida del Mal en la semilla 19) */
            const Level *lv = world_level_named(st->arg);
            float best = 1e9f, cx, cy;
            int keep = s_at.clear_unit - 1;
            if (units_alive_enemy(keep)) {
                units_position(keep, 1.0f, &cx, &cy);
                if (world_level_at(cx, cy) == lv)
                    target = keep;
            }
            for (int u = 0; target < 0 && u < units_count(); u++) {
                float x, y;
                if (!units_alive_enemy(u))
                    continue;
                units_position(u, 1.0f, &x, &y);
                if (world_level_at(x, y) != lv)
                    continue;
                float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
                if (d < best) {
                    best = d;
                    target = u;
                }
            }
            if (target < 0)
                return 1;
            s_at.clear_unit = target + 1;
        }
        float x, y;
        units_position(target, 1.0f, &x, &y);
        float dx = x - p->x, dy = y - p->y;
        if (getenv("D2_AUTOTEST_DEBUG") && s_ticks % 100 == 0)
            printf("  bot: (%.1f, %.1f) contra %s en (%.1f, %.1f), a %.1f\n", p->x, p->y, units_id(target), x, y,
                   sqrtf(dx * dx + dy * dy));
        /* En la demostración, si en 3 minutos no le bajó la vida, se lo saltea
           como a los inalcanzables (una vez, en 70 corridas, quedó media hora
           parado al lado del Gran Visir sin pegarle); el log dice cómo estaba */
        if (s_demo && st->kind == AT_KILL) {
            int hp, mx;
            units_health(target, &hp, &mx);
            if (target != s_at.kill_unit || hp < s_at.kill_hp || s_at.kill_since > s_at.step_ticks) {
                s_at.kill_unit = target;
                s_at.kill_hp = hp;
                s_at.kill_since = s_at.step_ticks;
            } else if (s_at.step_ticks - s_at.kill_since > 3 * 60 * TICK_RATE) {
                printf("  J1 en 3 minutos no le bajó la vida a %s (%d/%d): J1 en (%.1f, %.1f) estado %d, él en (%.1f, %.1f) "
                       "a %.1f, J2 en (%.1f, %.1f); se lo saltea\n",
                       units_id(target), hp, mx, p->x, p->y, p->state, x, y, sqrtf(dx * dx + dy * dy), s_pl[1].x, s_pl[1].y);
                units_damage(target, 1 << 20);
                s_at.unreachable++;
                s_at.kill_since = s_at.step_ticks;
                return 0;
            }
        }
        if (dx * dx + dy * dy < MELEE_RANGE * MELEE_RANGE * 0.8f) {
            if (b->pulse++ % 4 == 0)
                at_swing(p, in);
            return 0;
        }
        if (at_fight(p, in, b, 1.4f))
            return 0;
        int r = at_goto(b, p, in, zone_at(x, y), x, y, 1.2f);
        if (r < 0) { /* inalcanzable (dentro de una pared): se lo saltea y se anota */
            int region, reach = map_reach(p->x, p->y, x, y, &region);
            printf("  J1 no llega a %s en (%.1f, %.1f) desde (%.1f, %.1f): en el mapa %s (%d subtiles a los que se llega)\n",
                   units_id(target), x, y, p->x, p->y, reach ? "hay camino (es cosa del bot)" : "no hay camino", region);
            units_damage(target, 1 << 20);
            s_at.unreachable++;
            return 0;
        }
        return 0;
    }
    case AT_TOUCH: case AT_STONES: {
        if (p->reading) { /* el tomo: leerlo un momento y cerrarlo */
            if (p->talk_ticks > 20 && b->pulse++ % 2 == 0)
                in->back = in->button[1] = 1;
            return 0;
        }
        char why[160];
        if (st->check && at_check(st->check, why, sizeof why))
            return at_quest_pickup(p, b, in) ? 0 : 1; /* ya está (lo tomó al apretar A peleando al lado) */
        const char *cls = st->arg;
        if (st->kind == AT_STONES) {
            if (s_stones_lit >= 5)
                return 1;
            cls = k_stones[s_stone_order[s_stones_lit]];
        }
        int ids[4], n = units_find_all(cls, ids, 4), u = -1;
        for (int k = 0; k < n && u < 0; k++)
            if (units_visible(ids[k]))
                u = ids[k];
        if (u < 0)
            return st->kind == AT_TOUCH && s_at.phase > 0 ? (at_quest_pickup(p, b, in) ? 0 : 1) : at_back_to_level(p, b, in);
        int target;
        const char *act = quest_action(p, &target);
        if (s_at.trace && s_ticks % 50 == 0) {
            float ox, oy;
            units_position(u, 1.0f, &ox, &oy);
            printf("  J1 va a %s en (%.1f, %.1f), a %.1f: %s (fase %d)\n", cls, ox, oy,
                   sqrtf((ox - p->x) * (ox - p->x) + (oy - p->y) * (oy - p->y)), act ? act : "nada", s_at.phase);
        }
        /* Con un NPC al lado al que A le habla, A es suyo si está más cerca que
           el objeto (quest_nearer, la cuenta del juego): acercarse hasta el
           objeto. (Antes pedía estar a menos de 2, y la llegada a 1,5 por eje
           podía quedar en una esquina a 2,1: al lado de Tyrael no apretaba
           nunca.) */
        const Level *here = world_level_at(p->x, p->y);
        int npc = talk_npc_near(p, here && here->town), npc_near = npc >= 0;
        if (act && target == u && (!npc_near || quest_nearer(p, npc))) {
            if (b->pulse++ % 3 == 0) {
                in->action = in->button[0] = 1;
                s_at.phase = 1;
            }
            return 0;
        }
        if (s_at.phase > 0 && st->kind == AT_TOUCH && !act) /* abierto: lo que soltó, a la mochila */
            return at_quest_pickup(p, b, in) ? 0 : 1;
        float x, y;
        units_position(u, 1.0f, &x, &y);
        return at_travel(b, p, in, units_zone(u), x, y, npc_near ? 0.6f : 2.5f) < 0 ? -1 : 0;
    }
    case AT_USE: { /* el Libro de Habilidad: levantarlo y usarlo con X, como una persona */
        char why[160];
        if (at_check(st->check, why, sizeof why))
            return 1;
        int k = bag_index_of(p, st->arg);
        float gx = 0.0f, gy = 0.0f;
        if (k < 0 && at_quest_pickup(p, b, in))
            return 0;
        if (k < 0) /* lo soltó un monstruo lejos (el Mangual, el primer consejero que murió): ir a buscarlo */
            return at_ground_find(p, st->arg, &gx, &gy) && at_travel(b, p, in, zone_at(gx, gy), gx, gy, 1.0f) >= 0 ? 0 : -1;
        /* Ponérsela: el arma que tiene va a la mochila. Si no entra ni en el
           lugar que deja (un arco largo o una guadaña, 2x4, por la Voluntad de
           Khalim, 2x3), el juego deshace el cambio ("Inventario lleno") a cada
           intento: antes deja en el piso lo que menos le sirve */
        const ItemType *hand = p->equip[SLOT_WEAPON].type;
        if (hand && st->check && strncmp(st->check, "wield=", 6) == 0 && !at_bag_room_but(p, hand, k)) {
            int room = at_make_room(p, hand, p->x, p->y);
            if (room >= 0) {
                demo_forget_item(p, room); /* que la demo no lo vuelva a levantar */
                return 0;
            }
        }
        if (b->pulse++ % 3 == 0) {
            p->inv_sel = INV_CELL0 + p->bag_y[k] * BAG_W + p->bag_x[k];
            inventory_use(p);
        }
        return 0;
    }
    case AT_CUBE: { /* el Báculo (o la Voluntad de Khalim): abrir el cubo, pasarle las piezas con X y transmutar */
        static const char *const k_in2[] = { "msf", "vip" }, *const k_in3[] = { "qf1", "qey", "qbr", "qhr" };
        const char *const *pieces = s_at.act == 3 ? k_in3 : k_in2, *out = s_at.act == 3 ? "qf2" : "hst";
        int npieces = s_at.act == 3 ? 4 : 2;
        if (player_has(p, out)) {
            for (int c = 0; s_at.act == 3 && c < p->cube_count; c++) /* la Voluntad es un arma: del cubo a la mochila */
                if (strcmp(p->cube[c].type->code, out) == 0 && bag_add(p, &p->cube[c])) {
                    cube_remove(p, c);
                    break;
                }
            p->cube_open = p->inv_open = 0;
            return 1;
        }
        int k = bag_index_of(p, "box");
        if (k < 0)
            return -1;
        if (b->pulse++ % 3)
            return 0;
        if (s_at.trace) {
            printf("  J1 cubo: abierto %d, inventario %d, vara %d, amuleto %d; en el cubo:", p->cube_open, p->inv_open,
                   bag_index_of(p, "msf"), bag_index_of(p, "vip"));
            for (int c = 0; c < p->cube_count; c++)
                printf(" %s", p->cube[c].type->code);
            printf("; en la mochila:");
            for (int c = 0; c < p->bag_count; c++)
                printf(" %s", p->bag[c].type->code);
            printf("\n");
        }
        p->inv_open = 1;
        for (int s = 0; s < SLOT_COUNT; s++) /* si se puso una pieza (al levantarla), primero a la mochila */
            for (int q = 0; q < npieces; q++)
                if (p->equip[s].type && !strcmp(p->equip[s].type->code, pieces[q])) {
                    p->inv_sel = s;
                    inventory_use(p);
                    return 0;
                }
        if (!p->cube_open) {
            p->inv_sel = INV_CELL0 + p->bag_y[k] * BAG_W + p->bag_x[k];
        } else {
            int piece = -1; /* una de cada una */
            for (int q = 0; q < npieces && piece < 0; q++)
                if (!at_cube_has(p, pieces[q]) && bag_index_of(p, pieces[q]) >= 0)
                    piece = bag_index_of(p, pieces[q]);
            p->inv_sel = piece >= 0 ? INV_CELL0 + p->bag_y[piece] * BAG_W + p->bag_x[piece] : INV_TRANSMUTE;
        }
        inventory_use(p);
        return 0;
    }
    case AT_SAVE:
        game_save();
        return 1;
    case AT_ECO: { /* el cazador vivo más cercano; sin ninguno, al medio de la arena */
        char why[160];
        if (at_check(st->check, why, sizeof why))
            return 1;
        if (s_er.phase >= EP_END)
            return -1;
        int target = -1;
        float best = 1e9f, x, y;
        for (int u = 0; u < units_count(); u++) {
            if (!units_alive_enemy(u) || !units_is_hunter(u))
                continue;
            units_position(u, 1.0f, &x, &y);
            float d = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
            if (d < best) {
                best = d;
                target = u;
            }
        }
        if (target < 0)
            return at_travel(b, p, in, s_er.zone, s_er.cx, s_er.cy, 3.0f) < 0 ? -1 : 0;
        units_position(target, 1.0f, &x, &y);
        /* Sin D2_ECO_REAL, cada golpe es en área (a 7: como una Nova; los arqueros se quedan lejos): la prueba es de las oleadas, no
           de lo que tarda el bot en ir de uno en uno */
        if (!getenv("D2_ECO_REAL") && best < 49.0f && b->pulse % 4 == 0) {
            int ids[32], n = units_enemies_near(p->x, p->y, 7.0f, ids, 32);
            for (int k = 0; k < n; k++)
                if (units_is_hunter(ids[k]) && units_alive_enemy(ids[k])) {
                    int exp = units_damage(ids[k], 1 << 20);
                    if (exp > 0)
                        award_kill(p, ids[k], exp);
                }
        }
        if (best < MELEE_RANGE * MELEE_RANGE * 0.8f) {
            if (b->pulse++ % 4 == 0)
                at_swing(p, in);
            return 0;
        }
        if (at_fight(p, in, b, 1.4f))
            return 0;
        if (at_goto(b, p, in, s_er.zone, x, y, 1.2f) < 0) { /* (no llega: como en el Trono) */
            units_damage(target, 1 << 20);
            s_at.unreachable++;
        }
        return 0;
    }
    case AT_ECO_QUIT:
        if (s_er.phase >= EP_LOOT)
            return 1;
        if (eco_running())
            eco_pause_quit();
        return 0;
    case AT_ECO_PORTAL:
        if (s_er.exited)
            return 1;
        return at_travel(b, p, in, s_er.zone, s_er.px, s_er.py, 0.3f) < 0 ? -1 : 0;
    default:
        return at_player_step(0, st, in);
    }
}

/* skilltest.inc: la prueba de habilidades contra Andariel */
static int skilltest_start(void);
static int skilltest_input(InputState *in);
static int skilltest_on(void);

int game_autotest_start(const char *name, int players)
{
    bot_nav_set_breakable(obj_breakable_cell); /* (sus caminos pasan por las urnas y los barriles que se rompen) */
    bot_nav_set_jumps(obj_pad_jumps);          /* (y por las almohadillas del Santuario Arcano) */
    if (strcmp(name, "skills") == 0) { /* la prueba de habilidades contra Andariel */
        s_one_hit = 0;
        s_god = 1;
        return skilltest_start();
    }
    int eco = strcmp(name, "eco") == 0;
    if (strcmp(name, "act1") != 0 && strcmp(name, "act2") != 0 && strcmp(name, "act3") != 0 && strcmp(name, "act4") != 0 &&
        strcmp(name, "act5") != 0 && !eco)
        return -1;
    if (eco && !s_eco) {
        fprintf(stderr, "la prueba eco va con D2_ECO=1\n");
        return -1;
    }
    memset(&s_at, 0, sizeof s_at);
    s_at.active = 1;
    s_at.players = players;
    s_at.act = eco ? 5 : name[3] - '0';
    if (eco) {
        s_at.steps = k_eco_steps;
        s_at.nsteps = (int)(sizeof k_eco_steps / sizeof k_eco_steps[0]);
        /* D2_ECO_REAL=1: se pelea de verdad (si no, sin daño y de un golpe, para ver la mecánica) */
        s_one_hit = s_god = getenv("D2_ECO_REAL") ? 0 : 1;
        s_at.trace = getenv("D2_AT_TRACE") ? atoi(getenv("D2_AT_TRACE")) : 0;
        if (getenv("D2_AUTOTEST_SHOTS")) /* una captura por paso */
            snprintf(s_at.shots, sizeof s_at.shots, "%s", getenv("D2_AUTOTEST_SHOTS"));
        printf("prueba automática del Eco del Odio con %d jugador%s, %d pasos\n", players, players > 1 ? "es" : "", AT_STEPS);
        return 0;
    }
    s_at.steps = s_at.act == 5   ? k_act5_steps
                 : s_at.act == 4 ? k_act4_steps
                 : s_at.act == 3 ? k_act3_steps
                 : s_at.act == 2 ? k_act2_steps
                                 : k_act1_steps;
    s_at.nsteps = s_at.act == 5   ? (int)(sizeof k_act5_steps / sizeof k_act5_steps[0])
                  : s_at.act == 4 ? (int)(sizeof k_act4_steps / sizeof k_act4_steps[0])
                  : s_at.act == 3 ? (int)(sizeof k_act3_steps / sizeof k_act3_steps[0])
                  : s_at.act == 2 ? (int)(sizeof k_act2_steps / sizeof k_act2_steps[0])
                                  : (int)(sizeof k_act1_steps / sizeof k_act1_steps[0]);
    if (s_at.nsteps > AT_MAX_STEPS)
        return -1;
    s_one_hit = 1;
    s_god = 1;
    const char *shots = getenv("D2_AUTOTEST_SHOTS"); /* carpeta para una captura por paso */
    if (shots)
        snprintf(s_at.shots, sizeof s_at.shots, "%s", shots);
    s_at.trace = getenv("D2_AT_TRACE") ? atoi(getenv("D2_AT_TRACE")) : 0;
    printf("prueba automática del Acto %d con %d jugador%s, %d pasos\n", s_at.act, players, players > 1 ? "es" : "",
           AT_STEPS);
    /* D2_AUTOTEST_FROM=N: empezar en el paso N (para revisar uno que falló;
       las misiones de antes se dan por hechas según D2_AUTOTEST_STATE) */
    if (getenv("D2_AUTOTEST_FROM"))
        s_at.step = atoi(getenv("D2_AUTOTEST_FROM")) - 1;
    const char *state = getenv("D2_AUTOTEST_STATE"); /* "den_done,cain=3,wp=0x1f" */
    while (state && *state) {
        char item[32];
        size_t len = strcspn(state, ",");
        snprintf(item, sizeof item, "%.*s", (int)(len < sizeof item ? len : sizeof item - 1), state);
        state += len + (state[len] == ',');
        char *eq = strchr(item, '=');
        int v = eq ? (int)strtol(eq + 1, NULL, 0) : 1;
        unsigned long long v64 = eq ? strtoull(eq + 1, NULL, 0) : 1; /* (los waypoints del Acto 5 pasan de 32 bits) */
        if (eq)
            *eq = '\0';
        for (int i = 0; i < s_num_players; i++) {
            Player *p = &s_pl[i];
            if (!strcmp(item, "den_done")) p->den_started = p->den_done = 1;
            else if (!strcmp(item, "den_rewarded")) p->den_rewarded = 1;
            else if (!strcmp(item, "raven_done")) p->raven_started = p->raven_done = 1;
            else if (!strcmp(item, "raven_rewarded")) p->raven_rewarded = 1;
            else if (!strcmp(item, "cain")) p->cain_q = v;
            else if (!strcmp(item, "malus")) p->malus_q = v;
            else if (!strcmp(item, "tower")) p->tower_q = v;
            else if (!strcmp(item, "andy")) p->andy_q = v;
            else if (!strcmp(item, "wp")) p->waypoints |= v64;
            else if (!strcmp(item, "rada")) p->rada_q = v;
            else if (!strcmp(item, "staff")) p->staff_q = v;
            else if (!strcmp(item, "sun")) p->sun_q = v;
            else if (!strcmp(item, "arcane")) p->arcane_q = v;
            else if (!strcmp(item, "summ")) p->summ_q = v;
            else if (!strcmp(item, "tombs")) p->tombs_q = v;
            else if (!strcmp(item, "izual")) p->izual_q = v;
            else if (!strcmp(item, "forge")) p->forge_q = v;
            else if (!strcmp(item, "diablo")) p->diablo_q = v;
            else if (!strcmp(item, "a4f")) p->a4_flags |= v;
            else if (!strcmp(item, "shenk")) p->shenk_q = v;
            else if (!strcmp(item, "rescue")) p->rescue_q = v;
            else if (!strcmp(item, "rescued")) p->rescued = v;
            else if (!strcmp(item, "anya")) p->anya_q = v;
            else if (!strcmp(item, "nihl")) p->nihl_q = v;
            else if (!strcmp(item, "ancients")) p->ancients_q = v;
            else if (!strcmp(item, "baal")) p->baal_q = v;
            else if (!strcmp(item, "a5f")) p->a5_flags |= v;
        }
    }
    if (s_at.act >= 2) { /* el Acto 1 hecho (y el viaje con Warriv), con oro para el guerrero y la apuesta */
        for (int i = 0; i < s_num_players; i++) {
            Player *p = &s_pl[i];
            p->den_started = p->den_done = p->den_rewarded = 1;
            p->raven_started = p->raven_done = p->raven_rewarded = 1;
            p->cain_q = 6;
            p->malus_q = 4;
            p->tower_q = 3;
            p->andy_q = 3;
            p->waypoints |= 0x1ff;
            if (p->gold < 200000)
                p->gold = 200000;
            if (s_at.act >= 3) { /* y el Acto 2 (el viaje con Meshif), con el cubo */
                p->rada_q = 3;
                p->staff_q = 3;
                p->sun_q = 4;
                p->arcane_q = 5;
                p->summ_q = 3;
                p->tombs_q = 6;
                p->waypoints |= 0x3fe00;
                if (!player_has(p, "box"))
                    give_quest_item(p, "box");
            }
            if (s_at.act >= 4) { /* y el Acto 3 (hasta el portal rojo) */
                p->bird_q = 6;
                p->blade_q = p->will_q = p->tome_q = 3;
                p->temple_q = 4;
                p->meph_q = 2;
                p->a3_flags |= A3F_HRATLI | A3F_HELLGATE;
                p->waypoints |= 0x7fc0000;
                p->act_max = p->act_top = 4;
            }
            if (s_at.act >= 5) { /* y el Acto 4 (el portal del final), con el nivel del rito de los Antiguos */
                p->izual_q = 5;
                p->forge_q = 5;
                p->diablo_q = 4;
                p->a4_flags |= A4F_PORTAL | A4F_END | A4F_STONE;
                p->waypoints |= 7ull << 27;
                p->act_max = p->act_top = 5;
                if (p->level_num < 20)
                    gain_exp(p, s_classes[p->cls].exp_next[19] - p->exp);
            }
        }
        if (s_act != s_at.act)
            act_switch(s_at.act);
    }
    return 0;
}

static void at_shot(int step)
{
    if (!s_at.shots[0])
        return;
    char path[300];
    snprintf(path, sizeof path, "%s/paso%02d.bmp", s_at.shots, step + 1);
    game_render(1.0f);
    video_save_bmp(path);
}

static int autotest_input_fill(InputState *in, double last_tick_ms);

/* Llena los controles de este tick; 1 = sigue, 0 = terminó bien, -1 = falló.
   Los bots mantienen apretado el botón de lo que canalizan (Vincular demonio
   del Brujo), hasta 8 segundos */
int game_autotest_input(InputState *in, double last_tick_ms)
{
    int r = autotest_input_fill(in, last_tick_ms);
    if (r != 1 && s_demo && s_camp.on) { /* la campaña de la demostración: el acto que sigue */
        if (demo_campaign_next(r))
            return 1;
        r = demo_campaign_ok() ? 0 : -1;
    }
    for (int i = 0; i < s_num_players && i < MAX_PLAYERS; i++) {
        const Player *p = &s_pl[i];
        int hp = 0, mx = 0;
        if (p->bind_ch_skill >= 0)
            units_health(p->bind_ch_target, &hp, &mx);
        /* (sano no se deja: lo suelta y sigue peleando) */
        if (p->bind_ch_skill >= 0 && p->bind_ch_button >= 0 && p->bind_ch_button < 4 && p->bind_ch_frames < 8 * TICK_RATE &&
            mx > 0 && hp * 100 < mx * 99)
            in->player[i].held[p->bind_ch_button] = 1;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const PlayerInput *pi = &in->player[i];
        if (pi->action || pi->back || pi->nav_up || pi->nav_down || pi->nav_left || pi->nav_right)
            s_at.bot[i].ui_tick = s_ticks;
    }
    return r;
}

static int autotest_input_fill(InputState *in, double last_tick_ms)
{
    if (skilltest_on())
        return skilltest_input(in);
    if (!s_at.active)
        return 0;
    s_at.ticks++;
    s_at.tick_ms_sum += last_tick_ms;
    if (last_tick_ms > s_at.tick_ms_max)
        s_at.tick_ms_max = last_tick_ms;
    /* D2_AT_PERF=N: cada N ticks, lo que tardaron la lógica y el dibujo en ese
       tramo y dónde estaba (con D2_RENDER_EVERY=1 dibuja en cada tick, como el
       juego): para medir en la Pi qué parte de un acto pesa */
    static int perf_every = -1;
    if (perf_every < 0)
        perf_every = getenv("D2_AT_PERF") ? atoi(getenv("D2_AT_PERF")) : 0;
    s_at.iv_tick_sum += last_tick_ms;
    s_at.iv_ticks++;
    if (last_tick_ms > s_at.iv_tick_max)
        s_at.iv_tick_max = last_tick_ms;
    if (perf_every > 0 && s_at.iv_ticks >= perf_every) {
        const Player *p = &s_pl[0];
        printf("  rendimiento t%u %-26s lógica %.2f ms (peor %.1f), dibujo %.2f ms (peor %.1f), %d unidades, paso %d\n", s_ticks,
               p->level ? p->level->name : "?", s_at.iv_tick_sum / s_at.iv_ticks, s_at.iv_tick_max,
               s_at.iv_renders ? s_at.iv_render_sum / s_at.iv_renders : 0.0, s_at.iv_render_max, units_count(), s_at.step + 1);
        s_at.iv_tick_sum = s_at.iv_tick_max = s_at.iv_render_sum = s_at.iv_render_max = 0.0;
        s_at.iv_ticks = s_at.iv_renders = 0;
    }
    if (last_tick_ms > 50.0 && s_at.step < AT_STEPS) { /* una trabada que se notaría */
        s_at.slow_ticks++;
        if (s_at.slow_ticks <= 20)
            printf("  tick lento: %.0f ms en el paso %d (%s), %d zonas armadas\n", last_tick_ms, s_at.step + 1,
                   s_at.steps[s_at.step].label, zones_loaded_count());
    }
    int loaded = zones_loaded_count();
    if (loaded > s_at.max_zones)
        s_at.max_zones = loaded;
    if (s_at.ticks % 250 == 0) {
        long kb = at_rss_kb();
        if (kb > s_at.max_rss_kb) {
            s_at.max_rss_kb = kb;
            s_at.peak_map_mb = map_memory(&s_map) / 1048576.0;
            s_at.peak_gfx_mb = units_gfx_bytes() / 1048576.0;
            s_at.peak_step = s_at.step;
        }
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerInput *pi = &in->player[i];
        memset(pi, 0, sizeof *pi);
    }
    if (s_at.step >= AT_STEPS)
        return 0;
    const AtStep *st = &s_at.steps[s_at.step];
    s_at.step_ticks++;
    if (s_at.step_ticks == 1)
        s_at.gold0 = s_pl[0].gold;
    if (s_demo) {
        if (s_at.step_ticks == 1)
            demo_step_begin(st);
        demo_tick(); /* puntos, habilidades, botones, equipo y compras */
    }
    if (s_ticks % TICK_RATE == 0) /* quién está trabado: avanzó menos de un subtile en un segundo */
        for (int i = 0; i < s_num_players; i++) {
            AtBot *b = &s_at.bot[i];
            const Player *p = &s_pl[i];
            float dx = p->x - b->anchor_x, dy = p->y - b->anchor_y;
            b->stuck = dx * dx + dy * dy < 1.0f;
            b->anchor_x = p->x;
            b->anchor_y = p->y;
        }
    if (s_at.trace && s_ticks % 50 == 0) /* D2_AT_TRACE=1: cada 2 segundos, cómo está cada jugador */
        for (int i = 0; i < s_num_players; i++) {
            const Player *p = &s_pl[i];
            int e = units_find_enemy(p->x, p->y, 3.0f);
            float ex = p->x, ey = p->y;
            if (e >= 0)
                units_position(e, 1.0f, &ex, &ey);
            printf("  t%u J%d (%.1f, %.1f) %s, estado %d%s%s%s, monstruo %s a %.1f%s%s\n", s_ticks, i + 1, p->x, p->y,
                   p->level ? p->level->name : "?", p->state, p->wp_open ? ", waypoints abiertos" : "",
                   p->dialog_npc >= 0 ? ", hablando" : "", p->inv_open ? ", inventario" : "", e >= 0 ? units_id(e) : "-",
                   sqrtf((ex - p->x) * (ex - p->x) + (ey - p->y) * (ey - p->y)), p->message_ticks > 0 ? ", dice: " : "",
                   p->message_ticks > 0 ? p->message : "");
        }
    int r;
    if (s_demo && st->optional) {
        r = 1; /* no es de una misión: directo a la que sigue */
    } else if (st->kind == AT_ACTIVATE_WP || st->kind == AT_WAYPOINT) {
        /* Todos los jugadores (cada uno tiene sus waypoints) */
        r = 1;
        for (int i = 0; i < s_num_players; i++) {
            int ri = at_player_step(i, st, &in->player[i]);
            if (ri < 0)
                r = -1;
            else if (ri == 0 && r == 1)
                r = 0;
        }
    } else {
        if (s_demo && demo_lead(st, &in->player[0])) {
            r = 0;
            /* Farmear no cuenta para el tiempo del paso; pelear o levantar
               cosas, hasta 10 minutos (si no, algo que no alcanza lo frenaría
               para siempre) */
            if (demo_farming() || ++s_at.demo_side < 10 * 60 * TICK_RATE)
                s_at.step_ticks--;
        } else {
            r = at_lead_step(st, &in->player[0]);
        }
        for (int i = 1; i < s_num_players; i++) {
            if (s_demo)
                demo_follow(i, &in->player[i]);
            else
                at_follow(i, &in->player[i]);
            /* El que acompaña no toca los objetos de misión: al pelear al
               lado de la forja, el botón A tomaba el Mazo y el que guía se
               quedaba sin él */
            int obj;
            if ((st->kind == AT_TOUCH || st->kind == AT_STONES) && quest_action(&s_pl[i], &obj))
                in->player[i].action = in->player[i].button[0] = 0;
        }
    }
    if (s_demo)
        for (int i = 0; i < s_num_players; i++)
            demo_potions(i, &in->player[i]);
    char why[160] = "";
    if (r == 0 && s_at.step_ticks > at_step_timeout(st)) {
        r = -1;
        snprintf(why, sizeof why, "se pasó el tiempo");
    }
    if (r == 1 && !(s_demo && st->optional) && !at_check(st->check, why, sizeof why)) /* (el salteado no se revisa) */
        r = s_at.step_ticks > 3 * TICK_RATE ? -1 : 0; /* esperar un poco a que se actualice */
    if (r == 0)
        return 1;
    s_at.step_result[s_at.step] = r;
    s_at.step_time[s_at.step] = s_at.step_ticks;
    if (s_demo)
        printf("[%5.1f min] ", s_at.ticks / (60.0 * TICK_RATE));
    printf("%s paso %2d: %-48s %6.1f s de juego%s%s%s\n", r > 0 ? "BIEN" : "MAL ", s_at.step + 1, st->label,
           s_at.step_ticks / (double)TICK_RATE, r > 0 ? "" : " -> ", r > 0 ? "" : why[0] ? why : s_at.fail_msg[0] ? s_at.fail_msg : "falló",
           s_demo && st->optional ? " (salteado)" : "");
    fflush(stdout);
    at_shot(s_at.step);
    if (s_demo)
        demo_step_end(st, r);
    if (r < 0 && s_demo && ++s_at.demo_fails < 3) {
        /* La demostración sigue con el que viene (para ver qué más falla),
           sin lo que haya quedado abierto */
        s_at.failed = 1;
        game_print_status();
        at_stuck_report();
        for (int i = 0; i < s_num_players; i++) {
            Player *p = &s_pl[i];
            p->dialog_npc = -1;
            p->shop_open = p->hire_open = p->imbue_open = p->wp_open = p->inv_open = 0;
            p->reading = NULL;
        }
    } else if (r < 0) {
        s_at.failed = 1;
        game_print_status();
        at_stuck_report();
        return -1;
    } else {
        s_at.demo_fails = 0;
    }
    s_at.step++;
    s_at.phase = 0;
    s_at.talks = 0;
    s_at.step_ticks = 0;
    s_at.demo_side = 0;
    s_at.step_start = 0;
    s_at.fail_msg[0] = '\0';
    for (int i = 0; i < MAX_PLAYERS; i++)
        s_at.bot[i].stuck = s_at.bot[i].no_path = s_at.bot[i].fighting = s_at.bot[i].swinging = 0;
    return s_at.step >= AT_STEPS ? 0 : 1;
}

void game_autotest_render_time(double ms)
{
    s_at.render_ms_sum += ms;
    s_at.renders++;
    if (ms > s_at.render_ms_max)
        s_at.render_ms_max = ms;
    s_at.iv_render_sum += ms;
    s_at.iv_renders++;
    if (ms > s_at.iv_render_max)
        s_at.iv_render_max = ms;
}

/* ¿Salió bien cada paso? (la demostración sigue después de uno que falla) */
static int at_all_ok(void)
{
    int ok = 0;
    for (int k = 0; k < AT_STEPS; k++)
        ok += s_at.step_result[k] > 0;
    return !s_at.failed && ok == AT_STEPS;
}

void game_autotest_report(void)
{
    if (skilltest_on())
        return; /* la prueba de habilidades ya contó lo suyo */
    long kb = at_rss_kb();
    if (kb > s_at.max_rss_kb)
        s_at.max_rss_kb = kb;
    int ok = 0;
    for (int k = 0; k < AT_STEPS; k++)
        ok += s_at.step_result[k] > 0;
    printf("\n== Informe de la prueba del Acto %d (%d jugador%s) ==\n", s_at.act, s_num_players, s_num_players > 1 ? "es" : "");
    printf("pasos bien: %d de %d%s\n", ok, AT_STEPS, s_at.failed ? " (se cortó en el primero que falló)" : "");
    printf("tiempo de juego: %.1f minutos (%ld ticks)\n", s_at.ticks / (double)TICK_RATE / 60.0, s_at.ticks);
    printf("lógica por tick: %.3f ms de promedio, %.1f ms el peor\n", s_at.ticks ? s_at.tick_ms_sum / s_at.ticks : 0.0,
           s_at.tick_ms_max);
    if (s_at.renders)
        printf("dibujo por cuadro: %.2f ms de promedio, %.1f ms el peor (%d cuadros)\n",
               s_at.render_ms_sum / s_at.renders, s_at.render_ms_max, s_at.renders);
    printf("memoria: %.1f MB como máximo (en el paso %d: mapa %.1f MB, gráficos de unidades %.1f MB)\n",
           s_at.max_rss_kb / 1024.0, s_at.peak_step + 1, s_at.peak_map_mb, s_at.peak_gfx_mb);
    printf("ticks de más de 50 ms: %d\n", s_at.slow_ticks);
    printf("zonas armadas a la vez (sin el pueblo): %d como máximo (tope %d)\n", s_at.max_zones, s_zone_max);
    printf("monstruos a los que no se llegó caminando: %d\n", s_at.unreachable);
    printf("RESULTADO: %s\n", !s_at.failed && ok == AT_STEPS ? "BIEN" : "MAL");
    if (s_demo)
        demo_report();
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        int plans;
        long expanded;
        if (!s_at.bot[i].nav)
            continue;
        double total_ms, worst_ms;
        bot_nav_stats(s_at.bot[i].nav, &plans, &expanded);
        bot_nav_timing(s_at.bot[i].nav, &total_ms, &worst_ms);
        printf("bot J%d: %d búsquedas de camino, %.1f millones de subtiles explorados (%.0f por búsqueda), "
               "%.3f ms por búsqueda, lo más en un tick %.1f ms\n", i + 1, plans, expanded / 1e6,
               plans ? (double)expanded / plans : 0.0, plans ? total_ms / plans : 0.0, worst_ms);
        bot_nav_free(s_at.bot[i].nav);
        s_at.bot[i].nav = NULL;
    }
    if (s_demo && s_camp.on && !s_camp.mid)
        demo_campaign_report();
}

#include "demobot.inc"
