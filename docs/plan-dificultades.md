# Plan: Pesadilla e Infierno

El port juega solo en Normal. Este plan agrega las otras dos dificultades
como el original (la expansión, D2R): se elige al empezar la partida, se
abren al matar a Baal en la anterior, cada una tiene sus propias misiones y
waypoints, y cambian los monstruos, los tesoros, las tiendas, los
mercenarios y las resistencias del jugador.

## Cómo voy a trabajar

Como en los actos ([plan-acto5.md](plan-acto5.md)): Ponytail, el original
primero (D2MOO en `D:\tools\D2MOO` y las tablas), una prueba chica para cada
lógica, `shortcut:` en los atajos, y cada fase cierra con los cinco actos
enteros en Normal (que no cambie nada) más la prueba nueva.

## Lo que hace el original

### La partida y el personaje

- `pGame->nDifficulty` (0, 1, 2) se elige al crear la partida. Pesadilla se
  abre al terminar la Víspera de la Destrucción (Baal) en Normal, e
  Infierno al terminarla en Pesadilla (`CLIENTS_UpdateCharacterProgression`).
- Cada dificultad tiene sus propias misiones (`pQuestData[nDifficulty]`),
  con sus recompensas otra vez (los puntos de habilidad y de atributos, la
  vida del pájaro dorado, las resistencias de Anya...), sus presentaciones
  de los NPC, sus waypoints y el acto en el que quedó el personaje. El Rey
  Vaca también es por dificultad (`A1Q4 QFLAG_CUSTOM6`).
- El personaje (nivel, objetos, habilidades, mercenario, cofre) es el mismo
  en las tres.

### Los monstruos

- **Cuáles:** `levels.txt` `nmon1..nmon25` en Pesadilla e Infierno (en
  Normal `mon1..`); los jefes al azar de `umon` solo en Normal
  (`MONSTERCHOOSE_GetPresetMonsterId`). `MonDen(N/H)` y `MonUMin/Max(N/H)`.
- **El nivel:** el del área, `MonLvlEx(N)` / `MonLvlEx(H)`, salvo los
  `boss` y los `noRatio`, que usan `Level(N)` / `Level(H)` de monstats
  (`MONSTER_InitMonster`).
- **Vida, defensa, puntería, daño y experiencia:** los porcentajes de
  `monstats.txt` de esa dificultad (`MinHP(N)`, `AC(N)`, `A1TH(N)`,
  `A1MinD(N)`, `Exp(N)`, `El1MinD(N)`...) sobre `monlvl.txt` de la
  expansión (`L-HP(N)`, `L-AC(N)`, `L-TH(N)`, `L-DM(N)`, `L-XP(N)`)
  (`DATATBLS_CalculateMonsterStatsByLevel`). También por dificultad:
  resistencias (`ResFi(N)`...), bloqueo, `aidel`, `aip1..8`, `Drain`,
  `coldeffect` y el tesoro (`TreasureClass(N)`...).
- **Las habilidades:** `MonsterSkillBonus` de `difficultylevels.txt` (+3 en
  Pesadilla, +7 en Infierno) a cada habilidad de monstats.
- **Jefes y campeones** (`monumod.txt`, `constants` por dificultad): vida
  de más del único 300/200/100 %, del campeón 200/150/100 %, de la escolta
  100/75/50 %; `UniqueDamageBonus` y `ChampionDamageBonus` 90/75/66 %; los
  únicos al azar sacan 1, 2 o 3 modificadores (`nDifficulty + 1`) con los
  pesos `upick (N)` / `cpick (N)`; los de nombre propio suman uno más en
  Pesadilla y dos en Infierno; su tesoro `TC(N)` / `TC(H)`.

### El jugador

`difficultylevels.txt`:

| | Normal | Pesadilla | Infierno |
|---|---|---|---|
| Resistencias (`ResistPenalty`) | 0 | -40 | -100 |
| Experiencia al morir (`DeathExpPenalty`) | 0 | 5 % | 10 % |
| Robo de vida / maná (divisor) | 1 / 1 | 2 / 2 | 3 / 3 |
| Duración del frío y del congelamiento en monstruos (divisor) | 1 | 2 | 4 |
| Duración de las maldiciones en jefes (`AiCurseDivisor`) | 1 | 2 | 4 |
| Campo estático: hasta (`StaticFieldMin`) | 0 % | 33 % | 50 % |
| Daño del mercenario a los jefes | 50 % | 35 % | 25 % |
| Explosión de cadáver de los monstruos | 50 % | 35 % | 20 % |
| Explosión de los "encantado de fuego" | 75 % | 50 % | 13 % |

### Tesoros, tiendas y mercenarios

- Cada monstruo y jefe tira de su tesoro de esa dificultad (`Act 1 (N) A`,
  `Andarielq (N)`...), con el nivel del monstruo.
- **Tiendas** (`D2GAME_NPC_GenerateStoreItem`): en Normal el nivel de los
  objetos tiene tope por acto (12, 20, 28, 36, 45); en Pesadilla e Infierno
  no (nivel del personaje + 5) y, con el personaje en nivel 26 o más, salen
  versiones excepcionales y de élite (`ubercode` / `ultracode`,
  `NightmareUpgrade` / `HellUpgrade`), y las pociones grandes.
- **Mercenarios:** las filas de `hireling.txt` de esa dificultad.

## Lo que cambia en el port

| Fase | Qué | Listo cuando |
|---|---|---|
| 1: la dificultad | `s_diff` en la partida; el menú la pide (solo las abiertas; con dos jugadores, las de los dos); la partida guarda aparte, por dificultad, las misiones, las presentaciones, los waypoints, el acto y el Rey Vaca (`d1 ...`, `d2 ...`: las de Normal sin prefijo, así las partidas viejas cargan igual); se abre la que sigue al matar a Baal; el nombre de la dificultad en pantalla; `D2_DIFF=1|2` para las pruebas | Una partida en Pesadilla arranca con las misiones vacías y la de Normal sigue igual; la prueba `D2_DIFFTEST` |
| 2: los monstruos | lo de arriba: lista, nivel, estadísticas, habilidades, jefes y campeones | `D2_DIFFTEST` compara con las cuentas del original algunos monstruos de cada dificultad |
| 3: el jugador | resistencias, robo, frío, maldiciones, campo estático, mercenario, explosiones | Las cuentas en la prueba; la hoja de personaje muestra las resistencias con la penalidad |
| 4: tesoros, tiendas y mercenarios | los tesoros de la dificultad, las tiendas sin tope y con las versiones mejores, los mercenarios de su fila | La prueba recorre las tiendas y los mercenarios de cada dificultad |
| 5: pruebas y Pi | los bots en Pesadilla e Infierno (con personajes del nivel que pide), la demo, la Pi | `--autotest` de algún acto en cada dificultad |

**Decisión del usuario que se mantiene:** al morir solo se pierde oro (no
experiencia), también en Pesadilla e Infierno.

## Estado (22/10/2026)

Hecho, sin commitear:

- **Fase 1:** `s_diff` en `game.c` (`game_set_difficulty`, que también la
  pasa a `world.c` y `units.c`); el menú pregunta la dificultad después de
  elegir los personajes si todos tienen otra abierta (arranca en la más
  alta; `menu_difficulty`); la partida guarda por dificultad `den`,
  `quests` (con las presentaciones), `waypoints`, `talk`, `act1`..`act5`,
  `cowking`, `inact`, `qseen` y `freehire` (`save_diff`; las de Normal sin
  prefijo, las otras con `d1 `/`d2 `, y las de las dificultades que no se
  juegan se escriben como se leyeron); `diffmax` (o, en las partidas de
  antes, Baal muerto) dice cuál está abierta; matar a Baal abre la que
  sigue; un cartel con el nombre al empezar y "Dificultad: ..." en la pausa.
- **Fase 2:** `level_fill_diff` en `world.c` (`MonLvlEx(N/H)`, `nmon1..10`,
  los jefes al azar de esa misma lista, `MonUMin/Max(N/H)`); en `units.c`
  las columnas `(N)`/`(H)` de monstats (`dcol`), las `L-..(N/H)` de
  monlvl (`lvcol`), el nivel de monstats para los `boss` y `noRatio`,
  `MonsterSkillBonus`, la vida de más de jefes, escoltas y campeones de
  `monumod.txt` (ahora en el paquete), el daño de los campeones por
  `ChampionDamageBonus`, 2 y 3 modificadores en los únicos al azar y 1 y 2
  más en los de nombre, y sus tesoros `TC(N/H)`. `D2_DIFFTEST` da los
  mismos números que las cuentas del original (zombi, caído y Andariel en
  las tres).
- **Fase 3:** `ResistPenalty` y el piso de -100 en `player_resist` (se ve
  en la hoja), los divisores de robo de vida y maná, del frío y del
  congelamiento en los monstruos (`units_chill`, `units_freeze`), de Visión
  turbia y Terror (`AiCurseDivisor`) y el mínimo del Campo estático.
- **Fase 4:** los cofres usan "Act N (N) Chest" de su acto (de paso: en los
  Actos 4 y 5 usaban los del Acto 1); las tiendas en Pesadilla e Infierno
  sin tope (nivel del personaje + 5) y con las versiones mejores
  (`shop_upgrade`: excepcional, élite, `NightmareUpgrade`/`HellUpgrade`);
  los mercenarios que se ofrecen son los de las filas de su dificultad (los
  que ya se tenían siguen con las suyas).

- **Fase 5 (los bots):** en la demostración de Pesadilla e Infierno los
  personajes tienen 25 o 50 niveles más (hasta el 99), las habilidades 8 o
  16 más, más fuerza y destreza, y el equipo de esa dificultad
  (`demo_diff_gear` y `demo_diff_item` en `demobot.inc`; las armas de cada
  habilidad, en `st_equip`): la base excepcional o de élite que pueden
  usar, rara, con 10 o 20 a todas las resistencias en cada pieza (llegan a
  75 en Infierno), vida, defensa y daño aumentado, más fuego y rayo en las
  armas (los fantasmas son inmunes al daño físico en Infierno: sin eso el
  Bárbaro se quedaba peleándolos en el sótano de la Torre). En Normal no
  cambia nada.
- **La experiencia, sin signo** (como el original): el nivel 99 pide
  3.520 millones, más que un `int`. Antes `experience.txt` se leía con
  `atoi` (del 94 en adelante el umbral daba negativo: al llegar ahí se
  saltaba al 99) y los bots de Infierno con nivel 99 quedaban en el 1.
- **El rito de los Antiguos** pide nivel 20, 40 o 60 y su experiencia llega
  a 1,4, 20 o 40 millones (`ACT5Q5_RewardPlayer`).

- **La campaña de la demostración** (22/10/2026): la demo del menú juega
  los Actos 1 a 5 en Normal, Pesadilla e Infierno con los mismos dos
  personajes (`s_camp` en `game.c`; `docs/desarrollo.md`). Lo que encontró
  y se arregló: lo de misión se juntaba de un acto a otro hasta llenar la
  mochila (ahora se tira al empezar cada acto, menos un Cubo); los
  atributos se reparten de nuevo en cada acto como en uno nuevo de ese
  nivel y dificultad; limpiar un nivel sigue con el monstruo elegido (iba y
  venía entre dos casi a la misma distancia: la Guarida del Mal); lo de
  misión se levanta desde al lado (la estatuilla de jade caía en subtiles
  que el pie no pisa); la poción de Malah y el Pergamino de Resistencia son
  de misión (no se vendían bien ni se iban a buscar si caían al piso); al
  cerrar el diálogo se levanta lo que dio el NPC si no entró en la mochila
  (la poción de Malah); el señuelo de la Gidbinn cuenta como tocado si ya
  se usó peleando al lado; en la cumbre con el rito empezado no se vuelve
  al pueblo por portal (los Antiguos vuelven a sus estatuas). Y un error
  del juego: el portal al pueblo y el lugar adonde se vuelve por él se
  eligen entre los que se llega caminando (`reach_free_spot`); antes
  `find_free_spot` tomaba el libre más cerca, que en los pasillos del
  Maggot Lair caía del otro lado de la pared y el jugador quedaba encerrado
  en un hueco (le pasaba a los bots en Pesadilla e Infierno, que vuelven
  más al pueblo). El informe de un paso trabado dice si en el mapa hay
  camino, qué haría A y los saltos de la zona. Probado: 9 campañas enteras
  con semillas nuevas, 133 de 135 actos bien (los dos que fallaron, la
  Gidbinn y la cumbre, ya arreglados y probados aparte), las pruebas de los
  cinco actos bien. Queda: en la selva del Acto 3 hay uniones entre zonas
  sin pasaje (semilla 43: del Spider Forest al Flayer Jungle) y jefes en
  rincones a los que no se llega (el de la estatuilla de jade: la prueba se
  la da y lo anota); Nihlathak dos veces a 4 o 5 subtiles sin camino.
  (23/10/2026: lo del mapa lo arregló otra sesión, `conectividad-mapa.md`; y
  Nihlathak se teletransporta como el original, a ±`aip2` de él mismo, sin
  los lugares desde los que no se llega caminando al jugador: en los Halls
  of Vaught hay un pasillo cerrado al lado de su sala y ahí quedaba.)

Se probó: los cinco actos en Normal siguen enteros (Actos 1, 3 y 5 con
`--autotest`, las demos 1 y 9, el nivel de las vacas); guardar y cargar
pasando por las tres dificultades deja Normal igual; el menú (captura).

`shortcut:` lo que queda como estaba en Normal:

- la explosión de los encantados de fuego y frío (el original usa un % de
  la vida del monstruo: `MonsterCEDamagePercent`; el port, su daño);
- el daño del mercenario a los jefes (el original: 50/35/25 %; el port no
  lo reduce ni en Normal);
- el tesoro de los campeones (`TreasureClassChamp`: el port usa el común).
