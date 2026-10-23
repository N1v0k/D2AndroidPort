# Plan: el Acto 4 completo

El Acto 4 entero, como el original:
- la Fortaleza del Pandemónium y sus NPC (Tyrael, Jamella, Halbu y Caín);
- las tres mesetas (Outer Steppes, Plains of Despair y City of the Damned);
- el River of Flame con la Forja Infernal;
- el Santuario del Caos, sus cinco sellos y Diablo;
- sus tres misiones, sus monstruos y jefes, sus tiendas y la cinemática del final.

La colisión es la del original (las máscaras de 16 bits) y el grosor de las
paredes es el de siempre (`COLL_VOLUME`).

## Cómo voy a trabajar

Igual que en los Actos 2 y 3 ([plan-acto2.md](plan-acto2.md), [plan-acto3.md](plan-acto3.md)):

- **Ponytail:**
  - leer antes de tocar;
  - el cambio completo más chico, reusando lo que el motor ya tiene;
  - una prueba chica para cada lógica con ramas;
  - `shortcut:` en los atajos;
  - cada informe termina con lo que no se verificó.
- **El original primero:** D2MOO (`D:\tools\D2MOO`) y las tablas `.txt`.
- **Colisión:** antes de dar por buena una zona, revisarla con:
  - `D2_ZONETEST=1` (o `=2`, con el dibujo en caracteres): se llega caminando a los jefes, objetos y salidas;
  - `D2_COLLDUMP` y `D2_COLL_DEBUG`;
  - `D2_DS1_GRID`;
  - `D2_COLLHASH` cuando se toque algo de la colisión;
  - los bots de `--autotest`.

  Lo nuevo de este acto: los bordes de las mesetas (precipicios), los pozos
  (`PITS`), los ríos de lava y los puentes, y el Santuario (una grilla de
  5x5 piezas).
- **Rendimiento:** las reglas de [rendimiento.md](rendimiento.md):
  - nada se carga del disco en el hilo del juego;
  - nada recorre el mapa entero por cuadro;
  - armar una zona tiene que tardar menos de 100 ms en la Pi.
- **Cada fase cierra con:**
  - los Actos 1 (68/68), 2 (63/63) y 3 (74/74) enteros;
  - las pruebas de habilidades y la de los objetos;
  - ASan;
  - la prueba nueva de la fase.

## Qué tenemos y qué falta (20/10/2026)

| | Estado |
|---|---|
| Mapas del Acto 4 (220 `.ds1`) y sus baldosas (25 `.dt1`) | Están en la PC; **faltan en la lista del importador** (`tools/archivos-del-juego.txt`) |
| Los 6 niveles (Id 103 a 108) en `levels.txt` | Están |
| Dibujos de los monstruos y NPC del acto (`um`, `dm`, `di`, `22`, `17`, `5p`, `ty`, `dc`, `ja`, `20`, `36`...) | Están en la PC; faltan en la lista del importador |
| **Música del Acto 4** | **Falta**: se saca de D2R con `tools/d2rextract` |
| **Voces del Acto 4** (Tyrael, Jamella, Halbu, Caín, Hadriel, el fantasma de Izual y las frases de las clases) | **Faltan**: las de español latino, de la misma forma |
| La generación del original | En D2MOO: `DRLGOUTDOORS_InitAct4OutdoorLevel` (`DrlgOutdoors.cpp`: las mesetas y el Santuario), `gAct4OutdoorDrlgLink` y `DRLGOUTPLACE_LinkAct4*` (`DrlgOutPlace.cpp`: cómo se unen), `DRLGMAZE_PlaceAct4Lava` (`DrlgMaze.cpp`: el río) |
| Las misiones | `QUESTS/ACT4/A4Q0..A4Q4.cpp` |
| Las cinemáticas | `act04start` ya suena al entrar al portal rojo; `act04end` (el epílogo) suena al seguir viaje después de Diablo (`packet61` 5 en `A4Q2.cpp`) |

**Las IA que faltan** (`AiThink.cpp`):
- las del acto: DoomKnight, AbyssKnight y OblivionKnight (los caballeros: proyectil, Armadura y Espíritu de hueso, maldiciones), Megademon (Balrog, Señor del Abismo, Señor del Veneno: el Infierno), Izual, Smith (Hephasto) y Diablo;
- las que hoy usan la IA genérica y en este acto aparecen mucho: VileMother y VileDog, FingerMage, Regurgitator y Bighead.

## Cómo va (20/10/2026)

| Fase | Estado |
|---|---|
| 0: datos | **Hecha**: la música y las voces, sacadas de D2R; los archivos del acto, en el paquete de la Pi y en la lista del importador (`tools/archivos-del-juego.txt`) |
| 1: el viaje y la Fortaleza | **Hecha**: el portal rojo lleva a los dos (la cinemática, la primera vez); la Fortaleza con sus NPC; la solapa IV de los waypoints (27 a 29) y del registro; las tiendas de Halbu y Jamella (y su apuesta); Tyrael resucita al mercenario; Jamella cura; `act4` e `inact 4` en la partida |
| 2: las mesetas | **Hecha** (`src/world4.inc`): las tres, con sus bordes, sus pasos, sus piezas y sus pozos en las cantidades del original; Izual en el Plains; la bajada al río con su waypoint en el City |
| 3: el río y el Santuario | **Hecha**: el río de salas de lava (la subida, la Forja, los puentes, la lava alrededor) y el Santuario de 5x5 pegado al último puente |
| 4: los monstruos | **Hecha** (`src/monai4.inc`): las IA de las madres y sus crías, los Gropers, los Corpulentos (comen cadáveres), los tres caballeros (proyectil, Armadura y Espíritu de hueso, Decrepitud y las seis maldiciones de MonCurseCast), el Balrog (el aliento sostenido), los Damned, Izual, Hephasto, el meteoro del río, las almas atrapadas y Diablo (sus 16 elecciones con los pesos del original, el rayo rojo, el anillo de fuego, el toque frío que congela, el muro en zigzag, el muro de fuego, la carga y la prisión de hueso). Las maldiciones, el congelamiento y el abrazo de los Gropers tienen su ícono arriba de la barra |
| 5: las misiones | **Hecha** (`src/a4quest.inc`): la presentación de Tyrael, Izual y su espíritu (2 puntos de habilidad), la Forja (Caín da la piedra; Hephasto suelta el Martillo; las gemas: una perfecta, dos impecables y una común por jugador), los cinco sellos con sus tres jefes, Diablo, lo que dice Hadriel y el portal del final con el epílogo (`act04end`). Mefisto ahora suelta la Piedra de Mefisto |
| 6: pruebas | `D2_ZONETEST` pasa en las seis zonas; `D2_A4QTEST=1` recorre las misiones de punta a punta; `--autotest act4` pasa entero (36 de 36, con 1 y 2 jugadores: 13 a 14 minutos de juego, ningún tick de más de 50 ms; también con ASan); los Actos 1 a 3 siguen enteros, y las pruebas de habilidades (20 de 20) y de objetos (`objetos bien` en las cinco zonas de afuera). Los objetos del acto: el `TrappedSoulPlaceholder` pone sus racimos de almas atrapadas y el `JackInTheBox` sale uno por sala, como el original (`src/objects.inc`). En la Pi (21/10/2026, a 600 MHz por la baja tensión): las seis zonas bien; armarlas tarda de 125 a 146 ms las mesetas y la Fortaleza, 213 el río y 283 el Santuario (más que los 100 ms de la regla, como las grandes de los Actos 2 y 3); jugando, la lógica 1,3 ms por tick y el dibujo de 6 a 18 ms |

Lo que no es como el original (`shortcut:` en el código):
- sin el borde secundario de `lvlsub.txt` en las mesetas (como en el desierto);
- las variantes de las piezas que no entran gastan su turno de la ronda igual;
- el Acto 4 no tiene oscuridad (el original solo le baja la luz: `Intensity`);
- Diablo no le encierra el portal al pueblo al jugador, y sale y muere sin los rayos de luz del original;
- después de Diablo no se vuelve solo al pueblo a los 90 segundos: el final es el portal que abre Tyrael (el de la expansión);
- las almas atrapadas del `TrappedSoulPlaceholder` salen de nuevo al volver a armar la zona (como los otros monstruos del port; el original deja el objeto abierto para no repetirlas).

## Fase 0: datos

- Extraer de D2R (`D:\Battle.net\Diablo II Resurrected`):
  - `data/global/music/act4`;
  - las voces en español latino de `locales/audio/esmx/data/local/sfx/act4`.
- Sumar a la lista del importador y al paquete de la Pi (`tools/archivos-del-juego.txt`, `scripts/package-batocera.sh`, `tools/soundfiles.py`):
  - los mapas, las baldosas y la paleta del acto;
  - los monstruos, los objetos, los proyectiles y los efectos que usa;
  - las voces y la música.

## Fase 1: el viaje y la Fortaleza del Pandemónium

- **El viaje:** el portal rojo de Mefisto lleva a los dos jugadores al Acto 4, con la cinemática `act04start`, que ya suena. La partida guarda `inact 4` y una línea `act4` con las misiones. Se vuelve al Acto 3 por los waypoints.
- **La Fortaleza** (`LVLPREST_ACT4_FORTRESS`):
  - sus NPC y objetos de `monpreset`/`objpreset` del acto 4;
  - el waypoint;
  - la solapa del Acto 4 (waypoints 27 a 29);
  - la paleta `act4`.
- **Los NPC:** Tyrael, Jamella, Halbu y Caín, con sus textos (`A4Q*`, `*Gossip*` de `vo.json`) y sus voces.
- **Las tiendas:** Jamella (magia, pociones y la apuesta) y Halbu (armas y armaduras), hasta el nivel del acto.
- **El registro de misiones:** la solapa del Acto 4.

## Fase 2: las mesetas

`DRLGOUTDOORS_InitAct4OutdoorLevel`, con la misma maquinaria de exteriores que el desierto del Acto 2:
- las tres mesetas unidas como `gAct4OutdoorDrlgLink`: el Outer Steppes debajo de la Fortaleza, después el Plains of Despair y el City of the Damned;
- sus bordes (`MESA_BORDER_1..12`) y la pieza de paso a la Fortaleza (`FORTRESS_TRANSITION`);
- las piezas de cada meseta (`MESA_n_24X24`, `16X16`, `16X08`, `08X16` y `08X08`, en el orden y la cantidad del original) y los pozos (`PITS`);
- la pieza de Izual en el Plains of Despair (`MESA_2_IZUAL`);
- la bajada al río en el City of the Damned (`MESA_WARP`) y su waypoint.

Revisado con `D2_ZONETEST`, el dibujo de la colisión, capturas y el armado de cada zona.

## Fase 3: el River of Flame y el Santuario del Caos

- **El River of Flame** (`DRLGMAZE_PlaceAct4Lava`): el generador de salas de los calabozos (`build_act3_maze`) con las salas de lava de `lvlmaze.txt` y sus especiales:
  - la Forja Infernal (`LAVA_FORGE_W` o `_E`) con Hephasto;
  - la subida desde el City of the Damned y el waypoint;
  - los tres puentes (`BRIDGE_1`, `BRIDGE_2`) hacia el norte, hasta el Santuario.
- **El Santuario del Caos:** la grilla de 5x5 piezas (`LAVA_X`, los cuatro brazos, el corazón y la entrada), con los cinco sellos y el lugar de Diablo.

## Fase 4: los monstruos y los jefes

- Las IA que faltan (lista de arriba), de `AiThink.cpp`, con los hechizos de `monstats.txt` y `skills.txt` como en el Acto 3.
- Las maldiciones que tiran los caballeros sobre el jugador (Decrepitud y las de `MonCurseCast`), con su ícono arriba de la barra.
- **Los jefes con nombre:**
  - Izual;
  - Hephasto el Armero (`The Feature Creep` en `superuniques.txt`), con el Martillo de la Forja;
  - los de los sellos: el Gran Visir del Caos, Lord De Seis y el Infector de Almas, con sus escoltas;
  - Diablo.
- **Diablo:** su IA y sus siete hechizos (el rayo rojo, la nova de frío, el fuego, el muro de fuego, la carga, el anillo de fuego y la prisión de hueso).

## Fase 5: las misiones (A4Q1 a A4Q3, más A4Q0 y A4Q4)

1. **El Ángel Caído** (`A4Q1`): Tyrael la pide; Izual muere en el Plains of Despair, su fantasma habla, y Tyrael da 2 puntos de habilidad.
2. **La Forja Infernal** (`A4Q3`): Caín la pide; Hephasto suelta el Martillo; con la Piedra del Alma de Mefisto en la Forja se golpea y suelta las gemas de la recompensa (`ACT4Q3_CreateReward`).
3. **El Fin del Terror** (`A4Q2`): los cinco sellos del Santuario; al abrir los de cada brazo sale su jefe; con los cinco abiertos sale Diablo. Al morir, la misión se cumple; al seguir viaje suena el epílogo (`act04end`) y se avisa que el Acto 5 todavía no está.

Además, la presentación de Tyrael (`A4Q0`), lo que dice Hadriel (`A4Q4`), las frases de las clases al cumplir cada una y la música de misión.

## Fase 6: pruebas, bots, demos, rendimiento y la Pi

- **`--autotest act4`:** los dos bots juegan el acto entero.
- **`D2_A4QTEST=1`:** las misiones de punta a punta, guionadas.
- **Las pruebas de habilidades** contra Diablo y los jefes del acto (`D2_ST_MONSTER`).
- **Las regresiones** de los Actos 1, 2 y 3; ASan y TSan con `D2_ASYNC=1`.
- **Rendimiento en la Pi**, con el método de [rendimiento.md](rendimiento.md): el armado de cada zona y la demo del acto.
- **Subirlo a la Pi** cuando el usuario lo pida.
- El README, este plan y la memoria.
