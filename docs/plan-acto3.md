# Plan: el Acto 3 completo

El Acto 3 entero, como el original:
- el muelle de Kurast y sus NPC;
- la selva (el Spider Forest, el Great Marsh y el Flayer Jungle);
- Kurast (la baja, el bazar, la alta y el Causeway) y Travincal;
- las cuevas, los calabozos, las cloacas y los seis templos;
- el Durance of Hate y Mefisto;
- sus seis misiones, sus monstruos y jefes, sus tiendas y mercenarios.

La colisión es la del original (las máscaras de 16 bits de la fase 1 del Acto 2).

## Cómo voy a trabajar

Igual que en el Acto 2 ([plan-acto2.md](plan-acto2.md)):

- **Ponytail:**
  - leer antes de tocar;
  - el cambio completo más chico, reusando lo que el motor ya tiene;
  - una prueba chica para cada lógica con ramas;
  - `shortcut:` en los atajos;
  - cada informe termina con lo que no se verificó.
- **El original primero:** D2MOO (`D:\tools\D2MOO`) y las tablas `.txt`. Ghidra solo para lo que no esté en D2MOO.
- **Colisión:** antes de dar por buena una zona, revisarla con las herramientas que ya usamos:
  - `D2_ZONETEST=1` (o `=2`, con el dibujo en caracteres): se llega caminando a los jefes, objetos y salidas;
  - `D2_COLLDUMP` (la colisión de un rectángulo) y `D2_COLL_DEBUG` (los bits de cada subtile en pantalla);
  - `D2_DS1_GRID` (las celdas de un DS1);
  - los bots de `--autotest`.

  Lo nuevo de este acto: el agua y los puentes de la selva, los canales y muros de Kurast, y las escaleras y el puente de sangre del Durance.
- **Rendimiento:** las reglas de [rendimiento.md](rendimiento.md):
  - nada se carga del disco en el hilo del juego;
  - nada recorre el mapa entero por cuadro;
  - armar una zona tiene que tardar menos de 100 ms en la Pi.

  Al terminar, la demo del Acto 3 se mide en la Pi contra la del Acto 1, con el mismo binario.
- **Cada fase cierra con:**
  - el Acto 1 (68/68) y el Acto 2 (63/63) enteros;
  - las pruebas de habilidades;
  - ASan;
  - la prueba nueva de la fase.

## Qué tenemos y qué falta (19/10/2026)

| | Estado |
|---|---|
| Mapas del Acto 3 (521 `.ds1`) y sus baldosas (36 `.dt1`) | Están |
| Los 28 niveles (Id 75 a 102) en `levels.txt` | Están |
| Dibujos de los 57 monstruos de los niveles del acto | Están |
| **Música del Acto 3** | **Falta**: se saca de D2R con `tools/d2rextract` |
| **Voces del Acto 3** (Ormus, Alkor, Asheara, Hratli, Natalya, Meshif, Caín y las frases de las clases) | **Faltan**: las de español latino, de la misma forma |
| La generación del original | En D2MOO: `DrlgOutJung.cpp` (la selva), `DrlgOutPlace.cpp` (Kurast, el Causeway, Travincal y cómo se unen), y las misiones en `QUESTS/ACT3/A3Q0..A3Q7.cpp` |

**Las IA que faltan:**
- Fetish (cerbatana y chamán);
- FrogDemon, Mosquito, ThornHulk, Tentacle y TentacleHead;
- VileMother/VileDog, WillOWisp y BloodHawk;
- ZakarumZealot y ZakarumPriest, HighPriest (el Consejo) y Mefisto;
- Regurgitator, FingerMage, Arach y Baboon.

Las del Acto 1 (Zombie, Brute, CorruptRogue/Archer, Wraith) se revisan en la fase 4.

## Fase 0: datos

- Extraer de D2R (`D:\Battle.net\Diablo II Resurrected`):
  - `data/global/music/act3`;
  - las voces en español latino de `locales/audio/esmx/data/local/sfx/act3`.
- Sumarlos al paquete de la Pi: `scripts/package-batocera.sh` y `tools/soundfiles.py`, con las frases del Acto 3 de cada clase y de los NPC.

## Fase 1: el viaje y el muelle de Kurast

**Estado (19/10/2026): hecha**, salvo la página del registro de misiones (va con la fase 5):
- **El viaje:** Meshif lleva al Acto 3 desde Lut Gholein (cuando terminó el Acto 2) y desde Kurast vuelve a Lut Gholein ("Navegar al oeste"). La partida guarda `inact 3` y una línea `act3` con las misiones.
- **El muelle:**
  - es `docktown3.ds1`, con sus NPC y objetos de `monpreset`/`objpreset` del acto 3;
  - el waypoint (`WaypointAct3`);
  - la solapa del Acto 3 (waypoints 18 a 26);
  - la paleta `act3`.
- **Los NPC:**
  - hablan Ormus, Alkor, Asheara, Hratli, Natalya, Meshif y Caín, con sus textos de `vo.json` (las claves `A3Q*`, `*Gossip*`) y sus voces;
  - Hratli espera en `HratliStart` y, después de presentarse, está en su fragua (`HratliEnd`), como `A3Q0`.
- **Las tiendas:**
  - venden Ormus, Alkor (pociones), Asheara y Hratli, con sus columnas de las tablas, hasta el nivel 28;
  - Alkor también apuesta.
- **Los Lobos de Hierro de Asheara** (`hireling.txt`, Id 15 a 17):
  - lanzan sus dos hechizos como la hechicera: Bola de fuego y Saeta de fuego; Púa glacial; Carga eléctrica y Relámpago;
  - shortcut: la Armadura escalofriante no la usan.

- **El viaje:** Meshif lleva a los dos jugadores al Acto 3 después de Duriel (hoy dice que no está). La partida guarda el acto.
- **El muelle** (`docktown`):
  - la pieza del pueblo con sus salidas;
  - el waypoint;
  - la solapa del Acto 3 en el panel de waypoints (9 waypoints).
- **NPC:**
  - Ormus, Alkor, Asheara, Hratli y Natalya;
  - Meshif y Caín del Acto 3;
  - sus textos, voces y marcadores, como en el original.
- **Tiendas:**
  - Ormus (magia);
  - Alkor (pociones y la apuesta);
  - Asheara (armas, y los mercenarios: los Lobos de Hierro de `hireling.txt`, con sus hechizos de fuego, frío o rayo);
  - Hratli (armas y armaduras, y repara).
- **El registro de misiones:** la página del Acto 3.

## Fase 2: la selva, Kurast y Travincal

**Estado (19/10/2026): hecha** (`src/world3.inc`, incluido desde `world.c`):
- **La selva:** `DRLG_GenerateJungles` entero (dónde va cada selva, el camino en zigzag, las uniones, los claros) y `DRLGOUTJUNG_BuildJungle` (la pieza del muelle abajo del Spider Forest, la de Kurast arriba del Flayer Jungle, el archivo de cada claro). Las piezas sin camino se llenan de barro (`DarkMud.dt1`), como el original.
- **Kurast:** la baja, el bazar y la alta con sus bordes y puertas (del lado que diga `bJungleInterlink`), el waypoint, las cloacas y los templos fijos, y el relleno al azar de 16x16, 8x16/16x8 y 8x8. El Causeway (`Bridge`) y Travincal (sus seis piezas).
- **Las piezas** salen de `lvlprest.txt` por su `Def` (los números de `LVLPREST_ACT3_*`).
- **Revisado:**
  - `D2_ZONETEST` en 8 semillas: cada zona llega a sus pasillos y su waypoint, y desde el muelle se llega caminando hasta Travincal;
  - el dibujo de la colisión (`D2_ZONETEST=2`) y los bits del agua (`D2_COLLDUMP`): el río tiene 0x0001 (no se camina) y los árboles 0x0006/0x0007, tal cual los DT1;
  - capturas de las 9 zonas;
  - armar cada zona en el PC (ext4): 8 a 20 ms el mapa y 4 a 17 ms las unidades;
  - regresiones: Acto 1 68/68, Acto 2 63/63 (1 y 2 jugadores), habilidades 24/24; ASan limpio.
- Las bocas de las cuevas, las cloacas y los templos todavía no llevan a ningún lado (fase 3).

- **Primero, la colisión:**
  - armar cada tipo de pieza (selva, Kurast, Travincal) y mirarla con `D2_COLLDUMP` y `D2_COLL_DEBUG`, para ver el agua, los puentes, los muros y las escaleras;
  - comparar con capturas;
  - que los bits sean los del original (el agua de la selva frena el paso pero no los proyectiles).
- **La selva** (`DRLG_GenerateJungles`, `DrlgOutJung.cpp`): el Spider Forest, el Great Marsh y el Flayer Jungle, con sus piezas por dirección (`LVLPREST_ACT3_JUNGLE_*`), sus ríos y puentes, y las bocas de las cuevas.
- **Kurast** (`DRLGOUTPLACE_InitAct3OutdoorLevel`):
  - la Kurast baja, el bazar y la alta, con piezas al azar (suburbios, barrios y metrópolis de 16x16, 8x16 y 8x8);
  - las piezas fijas: el waypoint, las cloacas y los templos;
  - todo apilado hacia el norte desde la última selva, como en `DrlgOutPlace.cpp`.
- **El Causeway** (`ACT3_BRIDGE`) y **Travincal** (seis piezas).

## Fase 3: los calabozos

**Estado (19/10/2026): hecha**, salvo la palanca de las cloacas (va con la misión del corazón, fase 5):
- **Los de salas** (`build_act3_maze`, como `DRLGMAZE_GenerateLevel`): arrancan en ronda (2x2; las cloacas 1 en 5x5, con sus cuatro bocas en las esquinas), crecen al azar hasta las salas de `lvlmaze.txt` y después ponen las especiales (subida, waypoint, bajada, desagüe, cofre), cada una con una sola salida del lado que toque, como `DRLGMAZE_PlaceAct3*Stuff`. Las cuevas de las arañas son la ronda de 2x2 con su sala del cofre.
- **Los de una pieza:** Swampy Pit 3 y Flayer Dungeon 3 (una de cuatro), las cloacas 2, los seis templos y el Durance 3.
- **Los DT1 de cada pieza** son los del original: los de `lvltypes.txt` que pide su `Dt1Mask`, más Blank e InvisWal (`DRLGROOMTILE_LoadDT1FilesForRoom`), no los que lista el DS1. En la selva y Kurast da lo mismo (capturas y colisión idénticas).
- Las cloacas del Acto 3 se buscan en `levels.txt` por su Id (se llaman igual que las del Acto 2).
- **Revisado:** las 28 zonas del acto dan bien en `D2_ZONETEST` (3 semillas); `D2_WARPS=1` lista cada salto, y la red coincide con las `Vis` de `levels.txt`; capturas; cada calabozo se arma en menos de 20 ms en el PC.

- Las cuevas de las arañas (Spider Cave y Spider Cavern).
- El Swampy Pit (1 a 3) y el Flayer Dungeon (1 a 3).
- Las cloacas de Kurast (1 y 2).
- Los seis templos (piezas prearmadas): Ruined Temple, Disused Fane, Forgotten Reliquary, Forgotten Temple, Ruined Fane y Disused Reliquary.
- El Durance of Hate (1 a 3): el 3 con el puente de sangre de Mefisto y el portal rojo.

Para cada uno: el generador o la pieza del original (`lvlmaze.txt`, `lvlprest.txt`), sus salas especiales (cofres, jefes, objetos de misión), y la colisión revisada.

## Fase 4: los monstruos y los jefes

**Estado (19/10/2026): hecha** (`src/monai3.inc`), salvo el puente de Mefisto (va con su misión, fase 5):
- **14 IA del original** (`AiThink.cpp`): Fetish, FetishBlowgun, FetishShaman (el Infierno y revivir fetiches), Baboon, ThornHulk (Frenesí), Mosquito, WillOWisp, Arach (la telaraña), FrogDemon (se sumerge y sale), ZakarumZealot, ZakarumPriest (cura, Relámpago, Ventisca, se teletransporta herido), Vampire (Bola de fuego, Muro de fuego, Meteoro), HighPriest (el Consejo: Hidra, curar, su rayo) y Mefisto (sus cuatro hechizos, como el original en Normal).
- **Los hechizos de los monstruos:** cada monstruo tiene sus cuatro habilidades de `monstats.txt` con su modo y su nivel; `ai_cast` lanza el proyectil de `skills.txt` con el daño de `missiles.txt` en ese nivel.
- **Lo de área de los monstruos** ahora daña a los jugadores y sus mercenarias: la Ventisca (`monbliz1..4`), el Meteoro y el Muro de fuego (el fuego del piso quema), las nubes de veneno de la Nova de Mefisto. Las cabezas de la Hidra del Consejo escupen fuego mientras duran.
- Las del Acto 1 que aparecen en el acto (Skeleton, Zombie, BloodHawk, Brute, Wraith, CorruptRogue y CorruptArcher) ahora tienen la del original (`src/monai1.inc`, también en el Acto 1), y los tentáculos del río la suya (`Tentacle`, `TentacleHead`), puestos como `D2GAME_SpawnPresetMonster` (la cabeza de cada nivel y 4 a 6 tentáculos alrededor, con la cabeza de dueña).
- **Los jefes al azar de cada nivel** (`MonUMin..MonUMax`, de `umon1..umon10`), uno de cada cinco un grupo de campeones (campeador, fantasmal, fanático, poseído, vesánico); en todos los actos.
- El alcance cuerpo a cuerpo suma el `MeleeRng` de `monstats2.txt`.
- **Revisado:** cada IA con `D2_SPAWN` y `D2_AI_DEBUG`; la prueba de habilidades contra Mefisto, el Consejo, vampiros, sacerdotes y chamanes (también con ASan); capturas; los jefes y los objetos de misión del acto aparecen y se llega a ellos (`D2_ZONETEST` con `D2_WARPS=1`, que también los lista). Regresiones bien.
- Sszark the Burning no está en `superuniques.txt` de estos datos.

- Las IA que faltan (lista de arriba), de `AiThink.cpp` de D2MOO, como las del Acto 2 (`monai.inc`).
- **Los jefes con nombre:**
  - Sszark the Burning y Witch Doctor Endugu;
  - Stormtree y Battlemaid Sarina;
  - Icehawk Riftwing;
  - Bremm Sparkfist, Wyand Voidbringer y Maffer Dragonhand;
  - Ismail Vilehand, Geleb Flamefinger y Toorc Icefist (el Consejo).
- **Mefisto:**
  - su IA y sus hechizos;
  - el puente de sangre que se forma al morir;
  - el portal rojo (el Acto 4 todavía no está: lo dice como Meshif en el Acto 2);
  - la Piedra del Alma.

## Fase 5: las seis misiones (A3Q1 a A3Q6, más A3Q0 y A3Q7)

**Estado (19/10/2026): hechas** (`src/a3quest.inc`; la prueba guionada en `src/a3qtest.inc`):
- Cada pedido sale cuando el grupo llegó al lugar del original: el Tomo (Alkor) al llegar a la Kurast baja, la Voluntad de Khalim (Caín) al Great Marsh, la Gidbinn (Hratli, después de presentarse) al Flayer Jungle, el Templo (Ormus) al Causeway, Mefisto (Ormus) cuando murió el Consejo.
- **Los objetos:** el atril del Tomo (en el Ruined Temple), el altar de la Gidbinn (un señuelo: sale el fetiche jefe `fetish11` con su escolta y la Gidbinn la suelta él, `ACT3Q3_SpawnGidbinnBoss`), los cofres del ojo, el cerebro y el corazón, el Mangual (lo suelta el primero del Consejo que muere, y el siguiente un Cubo Horádrico a los que no tienen, `ACT3Q5_Callback08_MonsterKilled`), la Voluntad en el cubo (la receta de `cubemain.txt`), el Orbe cautivante (dos golpes con la Voluntad en la mano: la escalera al Durance se abre), la palanca de las cloacas (la reja al nivel 2), el puente de sangre y el portal rojo al morir Mefisto (el Acto 4 todavía no está: lo avisa).
- **El Pájaro Dorado:** la estatuilla la suelta el primer jefe (al azar o de nombre) que aparece en el acto, sin voladores ni el fetiche de la Gidbinn (`QUESTS_SetJadeFigurineBoss`; si se suelta su zona, es el próximo); Caín, Meshif (el pájaro), Caín, Alkor y, al volver, la Poción de la vida (+20 de vida, una vez).
- **Recompensas:** 5 puntos de atributo (Alkor), un anillo mágico (Ormus) y un mercenario gratis (Asheara).
- Los NPC dicen sus textos de cada etapa (`A3Q*` de `vo.json`, con sus voces) y marcan "!" cuando tienen algo nuevo; las frases de las clases (`act3_find_*`, `act3_complete_*`) suenan si están grabadas.
- Después del Consejo, los fanáticos huyen de los jugadores (`ZakarumZealot` con `A3Q5`).
- **El registro de misiones** tiene la solapa del Acto 3 con sus seis misiones y los textos de cada paso (`qstsa3q*`).
- **Revisado:** `D2_A3QTEST=1` cumple las seis de punta a punta (también con ASan); `D2_ZONETEST` con el puente arriba llega al portal rojo.

1. **El Pájaro Dorado** (`A3Q4`): la estatuilla de jade de un jefe, Meshif, el pájaro dorado, Alkor y la Poción de vida.
2. **La Espada de la Antigua Religión** (`A3Q3`): el Gidbinn en el Flayer Jungle y Ormus (el anillo). Asheara regala un mercenario.
3. **La Voluntad de Khalim** (`A3Q2`):
   - el ojo (Spider Cavern), el cerebro (Flayer Dungeon 3), el corazón (cloacas 2) y el mangual (el Consejo);
   - la Voluntad en el cubo;
   - romper el Orbe que Obliga.
4. **El Tomo de Lam Esen** (`A3Q1`): en un templo; Alkor da 5 puntos de atributo.
5. **El Templo Ennegrecido** (`A3Q5`): matar al Consejo en Travincal.
6. **El Guardián** (`A3Q6`): Mefisto.

Además, los diálogos de `A3Q0` (Hratli) y `A3Q7` (el Vagabundo Oscuro), las frases de las clases al cumplir cada una y la música de misión.

## Fase 6: pruebas, bots, demos, rendimiento y la Pi

**Agregado después (19/10/2026), revisando lo que faltaba del original:**
- los tentáculos, las IA del Acto 1, los jefes y campeones al azar y el `MeleeRng` (fase 4);
- el techo de la casa donde hay un jugador no se dibuja (en Kurast el personaje quedaba tapado adentro de las casas; pasa lo mismo en Lut Gholein);
- con A, el objeto de misión antes que la escalera de al lado si está cerrada o si el objeto está más cerca (la palanca de las cloacas, junto a la reja: el bot y una persona probaban la reja; el diario del Santuario, junto al portal rojo);
- `--autotest act3` va a matar al jefe que tiene la estatuilla de jade (y la levanta aunque la haya soltado lejos).

**Lo que queda del acto:**
- **Los objetos al azar de cada nivel** (`ObjGrp0..7` y `ObjPrb0..7` de `levels.txt`, `objgroup.txt`, `OBJECTS_PopulateFn1..9`): el motor todavía no tiene cofres, urnas, barriles, santuarios ni pozos que se usen (solo los de las misiones), así que primero va eso, para todos los actos. Es la tarea que sigue.
- shortcut: los Lobos de Hierro de frío no usan la Armadura escalofriante (los aliados no tienen armaduras de hechizo).

**Estado (19/10/2026): hecha, salvo subirlo a la Pi** (eso, cuando lo pida el usuario):
- **`--autotest act3`** (74 pasos): 8 de 8 corridas bien (semillas 1, 2, 5, 7 y 9 con 1 jugador; 3, 4 y 7 con 2), sin ticks de más de 50 ms. Lo que hubo que arreglar:
  - el bot en la selva: con las dos zonas armadas busca el camino al destino final en las dos (de a poco, como cualquier búsqueda); el camino puede cruzar el borde por otro lado que el pasillo, y por el pasillo quedaba yendo y viniendo. Si no se llega así, va por el pasillo un rato;
  - (2026-10-23, la demostración en la Pi trabada en "Ir al Flayer Jungle", en (300, 2931) y en la semilla 56 en (360, 2931)) si el camino que sigue para el mismo destino final pasa por una tercera zona, se sigue (`bot.c`, `plan_zone`): en el Spider Forest el camino más corto hacia la unión con el Flayer Jungle daba la vuelta por el Great Marsh, y al pisarlo la ruta por zonas mandaba de vuelta al Spider Forest; el bot quedaba una hora sobre el borde;
  - lo que suelta un monstruo en cualquier paso (la estatuilla, el Mangual) se levanta al verlo, y si quedó lejos se lo va a buscar;
  - con la mochila llena suelta lo que sobra del otro lado del objeto de misión y fuera del alcance de A (si no, A levantaba otra vez lo que soltó);
  - si el NPC contó otra cosa primero (Caín, la estatuilla antes que la Voluntad), se le vuelve a hablar.
- **Regresiones:** Acto 1 68/68 y Acto 2 63/63 con 1 y 2 jugadores, habilidades 24/24 (y 24/24 contra tres Mefistos con `D2_ST_MONSTER=mephisto`), `D2_A2QTEST` (termina navegando al Acto 3), `D2_A3QTEST` (las seis), ASan (73/73, sin errores) y TSan con `D2_ASYNC=1` (30.000 ticks, sin avisos).
- **`D2_ZONETEST`:** 0 problemas en los Actos 2 y 3 (la guarida de Duriel se prueba desde la llegada del portal del orificio). El Acto 1 sigue con su pasillo del Dark Wood al Black Marsh de siempre.
- **Rendimiento en la Pi** ([rendimiento.md](rendimiento.md#el-acto-3-19102026)): armar una selva tardaba 247 ms (Spider Forest) y 273 ms (Great Marsh). Ahora, 93 y 97 ms (los DT1 de la zona que viene se preparan en el hilo de trabajos), con la imagen y la colisión iguales byte por byte.
- Las dos corridas iguales de `--autotest` pueden separarse (el hilo de carga de gráficos): un paso que falla se revisa con varias semillas.

- **`--autotest act3`:** los dos bots juegan el acto entero, cada paso de las misiones, los waypoints y los jefes.
- **Las demos del menú** en el Acto 3 (o una de cada acto).
- **Pruebas:**
  - habilidades contra Mefisto (`D2_ST_MONSTER`);
  - regresiones de los Actos 1 y 2;
  - ASan y TSan con `D2_ASYNC=1`.
- **Rendimiento en la Pi**, con el método de [rendimiento.md](rendimiento.md):
  - la demo del Acto 3 con `D2_AT_PERF`, `D2_ZONES` y `D2_ASYNC=1`, contra la del Acto 1;
  - el armado de cada zona en menos de 100 ms;
  - los tirones del primer cuadro;
  - el dibujo en los niveles oscuros (el Durance).
- **Subirlo a la Pi** con el juego cerrado.
- El README, este plan y la memoria.
