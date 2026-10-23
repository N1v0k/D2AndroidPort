# Plan: el Acto 5 completo

El Acto 5 entero, como el original (la expansión):
- Harrogath y sus NPC: Larzuk (herrero, engarces), Malah (curandera),
  Qual-Kehk (los bárbaros mercenarios), Anya (cuando la rescatan),
  Nihlathak (hasta que traiciona) y Caín;
- las afueras: Bloody Foothills, Frigid Highlands, Arreat Plateau y Frozen
  Tundra, con las catapultas, los prisioneros y las barricadas;
- las cuevas de hielo: Crystalline Passage, Frozen River, Glacial Trail,
  Drifter Cavern, Ancients' Way e Icy Cellar;
- la cumbre del Arreat (Arreat Summit) y los tres Antiguos;
- el templo de Nihlathak: la entrada y los Halls of Anguish, Pain y Vaught;
- la Fortaleza de la Piedra del Mundo (Worldstone Keep, niveles 1 a 3), el
  Trono de la Destrucción y la Cámara de la Piedra del Mundo, con Baal;
- los tres infiernos de los portales rojos (Abaddon, Pit of Acheron e
  Infernal Pit);
- sus seis misiones, sus monstruos y jefes, sus tiendas y las cinemáticas.

La colisión es la del original (las máscaras de 16 bits) y el grosor de las
paredes es el de siempre (`COLL_VOLUME`).

## Cómo voy a trabajar

Igual que en los Actos 2 a 4 ([plan-acto4.md](plan-acto4.md)):

- **Ponytail:**
  - leer antes de tocar;
  - el cambio completo más chico, reusando lo que el motor ya tiene;
  - una prueba chica para cada lógica con ramas;
  - `shortcut:` en los atajos;
  - cada informe termina con lo que no se verificó.
- **El original primero:** D2MOO (`D:\tools\D2MOO`) y las tablas `.txt`.
- **Colisión:** antes de dar por buena una zona, revisarla con:
  - `D2_ZONETEST=1` (o `=2`, con el dibujo en caracteres);
  - `D2_COLLDUMP`, `D2_COLL_DEBUG` y `D2_DS1_GRID`;
  - `D2_COLLHASH` cuando se toque algo de la colisión;
  - los bots de `--autotest`.

  Lo nuevo de este acto: los bordes de acantilado de las barricadas (con su
  versión con nieve), las cuevas de hielo y los pasillos del templo.
- **Rendimiento:** las reglas de [rendimiento.md](rendimiento.md):
  - nada se carga del disco en el hilo del juego;
  - nada recorre el mapa entero por cuadro;
  - armar una zona tiene que tardar menos de 100 ms en la Pi.
- **Cada fase cierra con:**
  - los Actos 1 (68/68), 2 (63/63), 3 (74/74) y 4 (36/36) enteros;
  - las pruebas de habilidades y la de los objetos;
  - ASan;
  - la prueba nueva de la fase.

## Qué tenemos y qué falta (21/10/2026)

| | Estado |
|---|---|
| Mapas del Acto 5 (480 `.ds1`) y sus baldosas (56 `.dt1`, en `tiles/expansion`) | Están en la PC; **faltan en la lista del importador** (`tools/archivos-del-juego.txt`) y en el paquete de la Pi |
| Los niveles (Id 109 a 132) en `levels.txt` | Están (los de 133 a 137 son los de los Ubers y uno de D2R: quedan afuera) |
| Dibujos de los monstruos y NPC del acto (`xx`, `m5`, `os`, `ip`, `ox`, `io`, `f0`, `0B`, `L3`, `re`, `45`, `0D`, `42`, `43`, `XR`, `XT`, `XV`, `XS`, `0J`, `1D`, `0A`, `0G`, `UM`, `xy`, `K9`...) | Están en la PC; faltan en la lista del importador |
| **Música del Acto 5** | **Falta**: se saca de D2R con `build/d2rextract` (la instalación de Battle.net está en `D:\Battle.net\Diablo II Resurrected`, con su `Data`) |
| **Voces del Acto 5** (Larzuk, Malah, Qual-Kehk, Anya, Nihlathak, Caín, los Antiguos, Baal, Tyrael y las frases de las clases) | **Faltan**: las de español latino, de la misma forma |
| **Cinemáticas** `d2x_intro` y `d2x_out` | **Faltan** en el importador (`tools/d2port-importar.c` ya lo anota) |
| La generación del original | En D2MOO: `DRLGOUTSIEGE_InitAct5OutdoorLevel` y lo demás de `DrlgOutSiege.cpp` (las afueras, las barricadas, los prisioneros, las cuevas), `gAct5OutdoorDrlgLink` y `gAct5TundraDrlgLink` (`DrlgOutPlace.cpp`), y en `DrlgMaze.cpp`: `DRLGMAZE_PlaceAct5IceStuff` (las cuevas de hielo), `DRLGMAZE_PlaceAct5TempleStuff` (el templo), `DRLGMAZE_PlaceAct5BaalStuff` (la Fortaleza) y `DRLGMAZE_PlaceAct5LavaPresets` (los infiernos) |
| Las misiones | `QUESTS/ACT5/A5Intro.cpp` y `A5Q1..A5Q6.cpp` |

**Lo que el motor tiene que soportar (fase 1):**
- **Los waypoints:** el Acto 5 tiene nueve (los números 30 a 38 de
  `levels.txt`, después de los tres del Acto 4), y `Player.waypoints` era un
  `int` de 32 bits (`WP_MAX` 30, `WP_ACTS` 4). Hay que pasarlo a 64 bits sin
  romper las partidas guardadas.
- `act_switch(5)`, `s_act` hasta 5, la solapa V del registro de misiones y
  de los waypoints, la línea `act5` de la partida (el cofre ya tiene una
  pestaña por acto: `STASH_ACTS` 5).
- **Un mercenario nuevo:** los bárbaros de Qual-Kehk (`hireling.txt`).

**Las IA que faltan** (`AiThink.cpp`): Minion, DeathMauler, Overseer, Imp,
SiegeBeast, FrozenHorror, Succubus, SuccubusWitch, BloodLord,
ReanimatedHorde, PutridDefiler, SuicideMinion, Catapult y CatapultSpotter,
Ancient (los tres Antiguos), Nihlathak, BaalCrab (Baal), BaalCrabClone,
BaalMinion (los jefes de las oleadas), BaalTentacle, NpcBarb y Wussie (los
prisioneros). La de los yetis (`Brute`) ya está.

**Las misiones** (`A5Q1..A5Q6`):
1. El sitio de Harrogath: Shenk el Capataz, en el Bloody Foothills; Larzuk
   engarza un objeto.
2. Rescate en el monte Arreat: liberar a los 15 bárbaros presos del Frigid
   Highlands; Qual-Kehk da runas.
3. La prisión de hielo: Anya congelada en el Frozen River; Malah da la
   poción para descongelarla; Anya da un pergamino de resistencias.
4. La traición de Harrogath: Nihlathak en los Halls of Vaught; Anya
   personaliza un objeto. El portal rojo de Anya lleva al templo.
5. El rito de iniciación: los tres Antiguos en la cumbre (las estatuas).
6. La víspera de la destrucción: las cinco oleadas del Trono (Colenzo,
   Achmel, Bartuc, Ventar y Lister), Baal y la Cámara; Tyrael y el final
   (`d2x_out`).

## Las fases

| Fase | Qué |
|---|---|
| 0: datos | La música y las voces del acto, sacadas de D2R; los archivos del acto en el paquete de la Pi y en la lista del importador; `d2x_intro` y `d2x_out` en el importador |
| 1: el viaje y Harrogath | Los waypoints a 64 bits; el portal del final del Acto 4 lleva a Harrogath (con su cinemática); Harrogath con sus NPC, sus tiendas y los mercenarios bárbaros; la solapa V de los waypoints y del registro; `act5` en la partida |
| 2: las afueras y las cuevas | Las cuatro afueras (`DrlgOutSiege.cpp`), las cuevas de hielo (laberinto), la cumbre y la entrada del templo |
| 3: el templo y la Fortaleza | Los pasillos del templo, la Fortaleza (laberinto), el Trono, la Cámara y los tres infiernos |
| 4: los monstruos | Las IA de la lista, Baal con sus habilidades y los jefes de las oleadas |
| 5: las misiones | Las seis, con lo que dicen los NPC y sus recompensas |
| 6: pruebas | `D2_ZONETEST` en todas las zonas, `D2_A5QTEST=1` de punta a punta, `--autotest act5` con 1 y 2 jugadores, la demostración del menú en el Acto 5 y la Pi |

## Cómo va (21/10/2026)

| Fase | Estado |
|---|---|
| 0: datos | **Hecha**: la música del acto (8 temas) y sus voces en español latino (299), sacadas de D2R con `build/d2rextract`; `d2x_intro` y `d2x_out` en el importador y convertidas (`d2x_out.mpg`, 39 MB, se decodifica a 638 cuadros por segundo en la PC); en el paquete de la Pi y en la lista del importador: las baldosas de la expansión, los monstruos del acto (`tools/act1monsters.py ... tokens 5`), la música de sus zonas (`env:36` a `env:49`) y las dos cinemáticas. Las voces de los NPC entran al paquete en la fase 1, con cada NPC (como en los otros actos) |
| 1: el viaje y Harrogath | **Hecha**: los waypoints a 64 bits (`Player.waypoints`; en la partida, decimal sin signo: las viejas se leen igual) y por acto con `k_wp_first` (0, 9, 18, 27, 30: el Acto 5 tiene los números 30 a 38 de `levels.txt`, no 36..44); el portal del final del Acto 4 pasa el epílogo y lleva a Harrogath (`act_switch(5)`); Harrogath (`src/world5.inc`, la pieza 863 `townWest.ds1`) con su waypoint (`WaypointExp`) y sus NPC: Malah, Nihlathak, Qual-Kehk y Caín salen del DS1 y Larzuk de su marcador (`LarzukStandardPlace`, `OBJECTS_InitFunction71_LarzukStandard`); hablan con sus presentaciones (`LarzukAct5IntroGossip1`, la de la clase) y sus voces (`metadata-audio-vo.json`: Anya es "Drehya", Baal va en `monster`); las tiendas de Larzuk, Malah y Anya (Nihlathak usa la de Anya mientras está), la apuesta con Nihlathak, Malah cura, Qual-Kehk contrata a los bárbaros (`hireling.txt` 24, 25 y 36: espadas; desde el nivel 28 de su primera fila); la solapa V del registro (las seis pendientes, `src/a5quest.inc`) y la de los waypoints; Malah vende las pociones de la demostración. shortcut: los bárbaros no usan todavía Porrazo ni Aturdir (fase 4); Larzuk sale en el lugar del marcador, sin buscar uno libre; sin el saludo de Larzuk (`LarzukGreetingPlace`). Y (22/10/2026): los dos huecos negros del piso al lado de Qual-Kehk: `buildingses.dt1` repite las baldosas 16/20 y 16/21 con rareza 0, primero una versión casi vacía; con todas en rareza 0 ahora sale la última de ese DT1 (`find_tile`), como el original (la biblioteca de D2CMP las devuelve al revés y `DRLGROOMTILE_GetTileCache` toma la primera). Solo cambian Harrogath y una pared de la Tundra (`siege/snow.dt1` 38/3); los Actos 1 a 4, iguales |
| 2: las afueras y las cuevas | **Hecha**: el Bloody Foothills (las 15 tiras; sin grupos al azar, `MonDen` 0), las tres barricadas (`src/world5.inc`: la grilla de `DRLGOUTSIEGE_InitAct5OutdoorLevel` con el borde, el barranco, la entrada y la salida, las cuevas, las barricadas del borde secundario con los grupos de sustitución de `4x3barricade_rules.ds1` que ahora lee `ds1_load`, las prisiones y las piezas especiales); las cuevas de hielo con el generador de calabozos del Acto 3 (`A3M_ICE`) y las de una sola pieza (el Frozen River, el Drifter Cavern, el Icy Cellar, la cumbre y la entrada del templo). Los saltos son los de `levels.txt`: Plateau → Crystalline Passage → Glacial Trail → Tundra → Ancients' Way → cumbre, cada cueva con la de al lado. `D2_ZONETEST` bien en 8 semillas (la entrada del templo, "sin entrada": se llega por el portal de Anya, fase 5); ASan limpio; Actos 1 a 4 bien. La decoración al azar de `lvlsub.txt` en las barricadas (`barr_decorate`: los tipos 10 y 11, sala por sala). shortcut: la cumbre sin los Antiguos (fase 5) y sin el salto a la Fortaleza (fase 3) |
| 3: el templo y la Fortaleza | **Hecha**: con el generador de calabozos de `world3.inc`: los Halls of Anguish y of Pain (la ronda de 2x2 salas de 40x40 con la bajada y el waypoint, `DRLGMAZE_PlaceAct5TempleStuff`), la Fortaleza de la Piedra del Mundo 1 a 3 (la sala de la subida con tres salidas en el medio, la bajada y el waypoint del nivel 2: `...Act5BaalPrev` y `...Act5BaalStuff`) y los tres infiernos (la sala del medio, dos puntas y la Lava X del Acto 4 alrededor: `...Act5LavaPresets`); de una sola pieza, los Halls of Vaught, el Trono y la Cámara. Las baldosas de puerta de las barricadas y de Harrogath crean lo suyo (`DRLGROOMTILE_AddTilePresetUnits`: tipo 3 en `MapObject`): las torres, puertas y muros de las barricadas, las tres puertas de las prisiones, el portón de Harrogath y los portales rojos, que llevan a su infierno (`a5quest.inc`). `MAX_GFX` 192 (el acto entero armado pasaba de 128). `D2_ZONETEST` bien en 3 semillas salvo la Cámara (se entra por el portal de Baal, fase 5). En el infierno se llega a una de las baldosas de llegada de la Lava X (`a5_hell_spot`). shortcut: el portón sale abierto |
| 4: los monstruos | **Hecha** (`src/monai5.inc`, de `AiThink.cpp` y `AiBaal.cpp`, con los `aip` de `monstats.txt`): las IA de la lista más EvilHole, SiegeTower, MinionSpawner, GenericSpawner, AncientStatue, BaalThrone, BaalTaunt y BaalToStairs. Las habilidades: las de proyectil con `ai_cast`, las de aliento sostenido (el Arctic Blast de los horrores y de Nihlathak, el Inferno de Baal y de los diablillos) como el rayo de Diablo, las maldiciones que tiene el jugador y lo suyo en el cuadro de golpe: las crías de los generadores, el pisotón de la bestia de asedio, la explosión de cadáver de Nihlathak, los tentáculos de Baal y su burla (`BaalTaunt`, invisible, sigue al jugador; si lo ve quieto más de `aip2` veces seguidas, "baal taunt control" donde está: rayos y nubes de veneno durante 3 segundos, con la risa de Baal). Baal en el Trono: las cinco oleadas cuando no queda nadie a 64 subtiles, con la explosión de los cadáveres y 10 segundos antes de cada una; después se va a la Cámara. En la Cámara: el clon, el teletransporte, los tentáculos, la nova y los misiles de frío. Los niveles del acto usan `MonLvlEx` (`MonLvl` es 0 en la Cámara: Baal salía neutral). También (21/10/2026): Defense Curse y Blood Mana; el capataz (Cry Help, Healing Vortex, Overseer Whip); la madre pútrida que preña; los diablillos que montan las bestias; los tiros de las catapultas que caen del cielo; los misiles de frío de Baal con sus estelas; el Grito de Madawc; Baal baja caminando a su portal (`baalcrabstairs`); los esbirros suicidas explotan al morir. Las IA del acto cargan los modos S1, SC y S2..S4 que usan (`a5_needs_mode`): sin eso no salían sus habilidades. Y (22/10/2026): los bárbaros de las afueras (`act5barb1`, 32 en la semilla 7: Align 1 en `monstats.txt`, IA NpcBarb) están del lado de los jugadores, que no les pueden pegar (antes salían como enemigos quietos): pelean contra los monstruos (`barb_think`, de `AITHINK_Fn127_NpcBarb`: al que tienen al lado le pegan y esperan `aip1`, al que está a menos de `aip3` le corren el `aip2` %, si no caminan 20 a 40 hacia el oeste) y los monstruos los atacan a ellos (`units_pet_targets`, la lista de blancos 8 del original); lo que matan suelta lo suyo sin dar experiencia (`UNITS_KILL_ALLY`). Lejos de los jugadores no hacen nada, como los monstruos. shortcut: mientras caminan miran cada `aip1` ticks (el original piensa al final de cada tramo) y no tapan el paso. Y (23/10/2026): Nihlathak se teletransporta peleando a un punto al azar a ±`aip2` de él mismo (`AITACTICS_UseSkillInRange`; antes iba alrededor del jugador) y descarta los puntos desde los que no se llega caminando al jugador (`a5_walk_reach`; el original no lo mira): en los Halls of Vaught hay un pasillo cerrado, del otro lado de una pared de un subtile de su sala, y ahí quedaba (3 veces en unas 27 demos del Acto 5 en Infierno). `ai_teleport` prueba primero el punto mismo (empezaba por la esquina de arriba, a 2 subtiles) |
| 5: las misiones | **Hecha** (`src/a5quest.inc`, de `A5Q1..A5Q6`): Shenk y el engarce de Larzuk (`items_larzuk_sockets`: los normales y superiores según su nivel, los demás uno); los 15 presos de las tres prisiones (cinco `act5pow` por `CagedWussie1`) y las runas Ral, Ort y Tal; Anya congelada en el Frozen River, la poción de Malah, Anya de vuelta en Harrogath (Nihlathak se va) y el Pergamino de Resistencia (+10 a todas, una vez); el portal de Anya al templo, Nihlathak en los Halls of Vaught y la personalización (el panel de Charsi con `imbue_mode`: engarzar o personalizar); el altar de la cumbre (desde el nivel 20), los tres Antiguos donde están sus estatuas, la experiencia hasta el nivel siguiente y la puerta de la Fortaleza; Baal y el portal de Tyrael con `d2x_out`, de vuelta a Harrogath. Los portales rojos de las barricadas llevan a su infierno. También (21/10/2026): los demás NPC comentan cada etapa (las líneas A5Q* de `k_talk_lines`); los presos caminan hasta su portal; con un portal en la cumbre o todos muertos allá, los Antiguos vuelven a sus estatuas; su experiencia es la del original (lo que va de un nivel al siguiente, hasta 1.400.000). Y (22/10/2026): el final como el original, muerto Baal aparece Tyrael (`ACT5Q6_SpawnTyrael`, 5 al noroeste; también en una partida cargada) y se le habla (`A5Q6SuccessfulTyrael`, con su voz); al cerrar el diálogo abre el portal 5 al este del que le habló (`ACT5Q6_Callback02_NpcDeactivate`), los dos en el lugar libre más cerca al que se llega caminando (`a5_free_spot`). Antes el portal salía al morir Baal en su cadáver + 5, + 1, sin buscar lugar: en los pasillos de la Cámara caía en la pared (~8 % de las demostraciones se pasaban del tiempo en el último paso) |
| 6: pruebas | **Hecha**: `--autotest act5` (61 pasos, de Larzuk al portal de Tyrael, en el nivel 20 con los cuatro actos anteriores hechos) bien con 1 jugador en las semillas 7, 11, 23 y 31, con 2 en la 7, y 24 de 24 con 2 jugadores en 8 semillas corriendo las 24 a la vez (con carga); ASan limpio (el autotest y `D2_ZONETEST`); `D2_ZONETEST` bien en todas las zonas (la Cámara, "sin entrada": se entra por el portal de Baal); la demostración del menú con `D2_DEMO_ACT=5` (nivel 70, habilidades en 12): las 9 parejas, 61 de 61. Lo que encontraron los bots: Harrogath no era pueblo (no se hablaba con nadie), Larzuk salía encima de una pared, una torre de barricada enemiga en el pueblo, Baal neutral en la Cámara (`MonLvl` 0), los cadáveres de los `suicideminion` (y los tentáculos) que seguían tapando el paso (los bots quedaban trabados en la Fortaleza), A que le hablaba a Anya en vez de entrar a su portal (`portal_first`), el Salto que cruzaba paredes (ahora con el trazado del original), los huecos cerrados de las cuevas de hielo donde caían la Hechicera y el Brujo (la demostración sale con la misma habilidad: `demo_escape`) y Baal muerto en su trono por la demostración (ahora se respeta `killable` de `monstats.txt`). Los Actos 1 a 4, la prueba de misiones del Acto 4, la de habilidades (24 de 24) y las demostraciones de los Actos 3 y 4 siguen bien shortcut: sin `D2_A5QTEST` aparte (las seis misiones las recorre el autotest); la Pi, cuando lo pida el usuario |
