# Modo de juego "Eco del Odio" (24/10/2026)

Lo que pidió el usuario: un modo aparte del modo historia, inspirado en el
"Echoing Hatred" de Diablo IV (oleadas sin fin en una arena, cada vez más
difíciles), y que "cada terminada cantidad de oleadas aparecen bosses ...
bosses de la historia con determinado nivel acorde a la oleada".

Lo que eligió (preguntas del 23/10):
- **Entrada:** desde el menú principal, aparte de la historia. Se elige un
  personaje que ya existe; juega con su equipo, y lo que gana (experiencia,
  oro, objetos) vuelve a su partida guardada. La historia no cambia.
- **Arena:** una al azar entre niveles cerrados que ya tenemos: el Trono de
  la Destrucción, la Cumbre del Arreat, Uber Tristram y la Colossal Summit.
- **Muerte:** se termina la corrida (con el cofre del final según la oleada
  a la que se llegó, y lo que se juntó).
- **Dificultad:** del nivel del personaje. La oleada 1 tiene monstruos de su
  nivel y cada oleada sube.

No es una copia del de Diablo IV (sus reglas exactas son de ese juego): es
la idea llevada a Diablo II, con lo que el port ya tiene (monstruos de todos
los actos, sus jefes, los santuarios, los cofres y las IA del original).

**Estado (24/10/2026): hecho** (`src/eco.inc`; menú en `menu.c`; los
cazadores y su mapa de pasos en `units.c`; `--autotest eco`). Probado en
la PC con el bot en las cuatro arenas, con uno y dos jugadores, hasta la
oleada 50 (los jefes uber); falta medirlo en la Pi (estaba apagada).

Lo que cambió al probarlo:
- Diablo vuelve a su lugar si el jugador se aleja más de 85 (`a4_diablo`):
  los jefes de acto salen fuera de la pantalla a 22..30, no en la otra punta.
- Los cadáveres de las oleadas se van a los 30 segundos (como mucho 150):
  en la arena caen miles y llenaban las 4096 unidades (y los gráficos).
- El equipo común sin engarces que nadie levanta se va a los 2 minutos.
- El cofre se pone antes que los jugadores (encima de uno lo encerraba) y el
  portal a 4..6 del cofre.

## Las reglas

**La corrida.** Todo en el mundo del Acto 5 (`world_set_arena`: con Uber
Tristram y la Colossal Summit en cualquier dificultad). Los jugadores
aparecen en el lugar más abierto de la arena; los saltos del nivel (las
escaleras del Trono, la entrada de la cumbre), los pergaminos de portal y
los waypoints no andan. No hay monstruos del nivel: los de su DS1 se borran
al armarlo y no salen sus grupos al azar. Ni misiones, ni zonas de terror, ni
Diablo clon.

**La dificultad de las tablas** (las columnas Normal / (N) / (H) de
monstats, las inmunidades de Infierno y el castigo a las resistencias):
según el nivel promedio de los personajes, como los rangos de las áreas del
original: hasta 39 Normal, de 40 a 64 Pesadilla, 65 o más Infierno. Se elige
al empezar y no cambia en la corrida.

**Las oleadas.** Cada oleada es un grado: el nivel de los monstruos es el
del personaje (el promedio, en dos jugadores) más la oleada menos uno; los
jefes de cada oleada, como siempre, 3 más. Pasado el 99 ya no sube el nivel:
cada oleada de más les da +10 % de vida y +5 % de daño. Una oleada se pasa
matando su cuota (20 + 3 por oleada; con dos jugadores, una vez y media);
entre oleada y oleada hay 4 segundos de respiro (lo que quedó vivo sigue).

Los monstruos salen en manadas (de 3 a 6 del mismo tipo) a 14..24 subtiles de
un jugador, fuera de su pantalla si se puede, y van a buscarlo (no esperan a
verlo: siguen un mapa de distancias de la arena, así no se traban en las
casas de Tristram ni en las columnas del Trono). Con la oleada sube la
probabilidad de campeones (5 % + 1 % por oleada) y de jefes al azar con
escolta (2 % + 0,5 % por oleada).

**Los temas.** Las oleadas van de a cinco por acto, como la campaña: la 1 a
la 5 con monstruos del Acto 1 (sus niveles de levels.txt), la 6 a la 10 del
Acto 2, y así hasta la 25; después vuelve a empezar. Cada oleada elige 3 o 4
tipos del acto (así no se cargan los gráficos de todos a la vez, que en la
Pi pesa).

**Los jefes (lo que pidió el usuario).**
- **Cada 5 oleadas, un jefe de acto**, en el orden de la campaña: la 5
  Andariel, la 10 Duriel, la 15 Mefisto, la 20 Diablo y la 25 Baal.
- **De la 26 a la 50, los del evento Pandemonium:** la 30 Lilith, la 35 Duriel
  uber, la 40 Izual uber, la 45 el Diablo uber (el clon) y la 50 los tres de
  Uber Tristram juntos. Después se repiten los dos ciclos, con el nivel
  siguiendo la oleada.
- **En la tercera de cada cinco (3, 8, 13, 18, 23...), un jefe con nombre
  propio de ese acto** (los de superuniques.txt: Blood Raven, la Condesa,
  Radament, el Invocador, Izual, Nihlathak...), con su escolta. Sale más
  cerca (algunos no se mueven de su lugar, como el Invocador en su
  plataforma) y no hace falta matarlo para pasar la oleada.
- El nivel del jefe es el de la oleada más 3 (como los jefes del original
  sobre su área). El de acto sale lejos y viene caminando, con un cartel. En
  una oleada de jefe de acto los monstruos comunes siguen saliendo, pero con
  un tercio del tope; la oleada se pasa cuando mueren los jefes (no con la
  cuota).
- Sueltan lo de su clase de tesoro del jefe de la historia en la dificultad
  de la corrida (sin lo de misión). Los uber sueltan lo del jefe de su acto:
  la Antorcha, los órganos, las llaves y Annihilus siguen siendo de sus
  eventos.

**El desborde.** Una barra que se llena con los monstruos vivos (el tope:
40 con un jugador, 50 con dos). Llena, empieza una cuenta de 10 segundos;
si no se baja del 90 % antes, se termina la corrida ("Te desbordaron").

**El ritmo según el nivel** (pedido del usuario, 24/10/2026: "si alguien no
quiere jugar la historia y quiere jugar el Eco, que pueda"). Un personaje
de nivel bajo mata despacio, así que los tiempos van por escalones del
nivel (el promedio de los personajes, mirado en cada oleada: suben
jugando), de 5 en 5 hasta el 20, de 10 en 10 hasta el 40 y de 20 en 20
después (lo pidió así): el tiempo entre manadas, la cuenta del desborde y
el respiro entre oleadas son el 250 % de lo de siempre del 1 al 5, el 220 %
del 6 al 10, el 190 % del 11 al 15, el 165 % del 16 al 20, el 140 % del 21
al 30, el 120 % del 31 al 40, lo de siempre del 41 al 60, el 85 % del 61 al
80 y el 70 % del 81 en adelante; desde el 61, además, las manadas traen uno
más (`k_eco_pace` y `eco_set_pace` en `src/eco.inc`).

**Más fácil en niveles bajos** (24/10/2026: "así llegan a oleadas más
lejos"): con el ritmo de más (hasta el nivel 40) los monstruos tienen menos
vida y daño (`eco_soft_pct`: -40 % con 250, nada con 100; `hunt_power`
acepta porcentajes negativos), su nivel sube uno cada tres oleadas hasta el
15 y cada dos hasta el 30 (`grow6`, en sextos de nivel), nunca más de uno
por debajo de los personajes, los jefes tienen 0, 1 o 2 niveles de más en
lugar de 3 y salen menos manadas de campeones. Con los bots de la
demostración (`D2_ECO_DEMO=1`, que reparten puntos, aprenden y usan
habilidades y toman pociones), dos personajes de nivel 1 llegan a la
oleada 9 (Bárbaro y Hechicera) o 12 (Amazona y Paladín); antes, los bots de
la prueba caían en la 2 y los de la demostración en la 6.

**El botín, acorde al nivel** ("que los items que caigan sean acordes al
nivel del personaje"): lo que sueltan los monstruos y el cofre sale de la
clase de tesoro de su grupo que le toca al nivel de los personajes + 2
(`items_tc_at_level`: los grupos de TreasureClassEx van de "Act 1 ... A",
nivel 0, a "Act 5 (H) ... C", nivel 85, por todos los actos y dificultades)
y el nivel de los objetos no pasa de ese. Así un monstruo del Acto 5 en una
oleada baja suelta lo del Acto 1, y el cofre de un personaje de nivel 15 es
"Act 2 Chest B".

**Los santuarios.** Cada 4 oleadas aparecen dos santuarios cerca de los
jugadores (de los que duran del original: armadura, combate, habilidad,
recarga de maná, o el explosivo), y se van al terminar la oleada si nadie los
usó. Junto al lugar de inicio hay un pozo (se recarga como los del
original). Las mercenarias muertas vuelven al terminar cada oleada de jefe.

**La muerte y el final.** Con un jugador, morir termina la corrida. Con dos,
el que muere vuelve al empezar la oleada siguiente, al lado del otro; si
mueren los dos, se termina. No se pierde oro ni el cadáver. Al terminar
(muerte, desborde o "Terminar el Eco" en la pausa): se borran los
monstruos, los jugadores reviven con la vida llena en el medio de la arena,
aparece el **cofre del Eco** (el cofre brillante) y un portal de vuelta.

**El cofre del Eco.** Al abrirlo suelta 2 + oleada / 2 objetos (hasta 16)
de la clase de tesoro de cofre del acto 5 de la dificultad (la C), con
nivel de objeto el de la última oleada y el hallazgo mágico más un 5 % por
oleada; y oro (oleada x nivel x 20). El portal vuelve al menú (guardando).

**El récord.** La mejor oleada de cada personaje queda en su partida (una
línea `eco` aparte de las dificultades) y se ve al elegirlo; al final, el
cartel dice si es un récord nuevo.

**Dos jugadores.** Como la historia: los dos eligen personaje (ya creado) y
juegan en la misma arena.

**El guardado.** La corrida no se guarda (como el original, las partidas no
siguen). Lo del personaje sí: experiencia, oro, objetos, habilidades. Lo de
la historia de las tres dificultades (misiones, actos, waypoints) se lee y se
escribe tal cual estaba; las pestañas del cofre del Acto 5 no se abren por
estar en la arena.

## Cómo se arma (fases)

- **F1. El menú y el arranque.** `menu.c`: el botón ECO DEL ODIO (siete
  botones en el principal), elegir uno o dos jugadores, solo partidas que
  existen, sin pantalla de dificultad; `menu_mode()`. `main.c`:
  `game_set_mode()`. `game.c`: la carga en modo Eco (lo de cada dificultad
  queda crudo) y el guardado; `world_set_arena()`.
- **F2. La arena.** `src/eco.inc`: la elegida, armarla, borrar sus monstruos,
  el lugar más abierto (transformada de distancia), las celdas a las que se
  llega (para salir y para el mapa de distancias); sin saltos ni portales.
- **F3. Las oleadas.** Los grupos de cada tema (`world_act_monsters`), las
  manadas, los campeones y jefes, la cuota, el nivel, el desborde; en
  `units.c`, los que cazan (`units_set_hunt`) y el mapa de distancias
  (`units_set_flow`).
- **F4. Los jefes.** El calendario (de acto, de nombre propio, uber), su
  nivel, su tesoro, el cartel.
- **F5. El final.** La muerte en modo Eco, el desborde, el cofre, el portal,
  el récord, los santuarios, el pozo, la mercenaria.
- **F6. Lo que se ve y los idiomas.** La barra de arriba (oleada, nivel,
  cuota, desborde), los carteles, la pausa; los textos nuevos en los 13
  idiomas (al final de `lang_en.inc`).
- **F7. Las pruebas.** `D2_ECOTEST`: arranca una corrida y revisa cada parte
  (la arena sin salidas, que lleguen los monstruos, la cuota, un jefe por
  oleada, el desborde, el cofre, el guardado sin tocar la historia). Y una
  corrida con el bot (`--autotest eco`) para ver hasta qué oleada llega y el
  tiempo por tick en la Pi.

Variables para probar: `D2_ECO=1` (arranca una corrida sin menú),
`D2_ECO_ARENA=0..3`, `D2_ECO_WAVE=N` (empezar en esa oleada),
`D2_ECO_DEBUG=1` (lo que hace cada oleada en la consola), `D2_ECOTEST=1`.
