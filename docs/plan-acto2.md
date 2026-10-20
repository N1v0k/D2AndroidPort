# Plan: el Acto 2 completo

El Acto 2 entero, como el original: Lut Gholein, el desierto, las cloacas, el
palacio, las tumbas, el Santuario Arcano, el Cañón de los Magos y Duriel, con
sus seis misiones, sus monstruos y jefes, sus personajes, tiendas y
mercenarios, y la colisión del mapa como la del juego original.

## Cómo voy a trabajar

- **Ponytail**, para escribir y probar el código:
  - Antes de tocar, leer el código que se toca y anotar todo lo que el cambio
    tiene que alcanzar.
  - El cambio más chico que lo resuelva entero, reusando lo que el motor ya
    tiene: laberintos, piezas prearmadas, misiones, tiendas, auras.
  - Sin opciones ni abstracciones que nadie pidió.
  - Cada lógica con ramas, bucles o lectura de datos lleva una prueba chica.
  - Los atajos a propósito quedan marcados con `shortcut:` y el límite.
  - Cada informe termina con lo que no se verificó y los riesgos.
- **El original primero:**
  - D2MOO, el código descompilado de la versión 1.10, que cubre misiones, IA,
    generación de mapas y colisión.
  - Las tablas `.txt` y los textos del juego.
- **Ghidra**, para lo que D2MOO no tiene: lo que D2R cambió respecto de la
  1.10 (por ejemplo los mercenarios desde la 2.4) y el Brujo. También para
  confirmar puntos dudosos.
- **Fases que se pueden jugar y probar una por una.** Cada fase cierra con:
  - compilar y pasar la prueba del Acto 1 entero (68/68);
  - las pruebas de habilidades de todas las clases;
  - las demos y ASan;
  - la prueba nueva de esa fase;
  - y, si querés, subirla a la Pi.

## Qué tenemos y qué falta (revisado)

| | Estado |
|---|---|
| Mapas del Acto 2 (446 `.ds1`) y sus baldosas | Están todos, y no falta ninguna baldosa |
| Dibujos de los 58 monstruos del Acto 2, los jefes y los personajes del pueblo | Están |
| Dibujos de los 84 objetos (cofres, orificio, portales, altares, sarcófagos…) | Están |
| Sonidos de los monstruos (momias, gusanos, Duriel, el Invocador…) | Están |
| **Música del Acto 2** | **Falta** (solo se extrajo la del Acto 1) |
| **Voces de los personajes de Lut Gholein** (Atma, Drognan, Jerhyn, Greiz…) | **Faltan** (solo hay 4 sonidos sueltos) |
| Ejecutable para analizar con Ghidra | **No hay**: en `D:\Diablo II Resurrected` solo quedan las herramientas de mods |
| Ghidra y Java | **No están instalados** |

La música y las voces solo se pueden sacar del juego instalado. Si se
reinstala D2R desde Battle.net, con eso alcanza para todo:
`tools/d2rextract` las saca de la instalación, y `D2R.exe` sirve para Ghidra.
Sin reinstalarlo, el Acto 2 se juega sin música y con los diálogos solo en
texto.

## Fase 0: herramientas

1. **D2MOO en la PC**: clonarlo para buscar en el código sin depender de la
   web.
2. **Ghidra 11 y Java 21** (si das el visto bueno a las descargas):
   - **Uso:** el análisis automático de Ghidra (`analyzeHeadless`) desde la
     línea de comandos, con scripts que exportan las funciones que me
     interesan a texto.
   - **Qué analizar:** `D2R.exe`, para comparar con D2MOO lo que D2R cambió.
3. **Si se reinstala D2R:** extraer la música del Acto 2 (`music/act2`) y
   las voces de `local/sfx/act2`, y agregarlas al paquete.

## Fase 1: la colisión como el original (para todo el juego)

Hoy cada baldosa marca solo "se camina o no" en cada subtile. El original
guarda en cada subtile una **máscara de 16 bits**, y cada cosa mira los bits
que le importan. Lo saqué de `D2Collision.h` y `D2Collision.cpp` de D2MOO.

**Los bits:**

| Valor | Nombre | Para qué |
|---|---|---|
| 0x0001 | `WALL` | paredes, acantilados, el vacío del Santuario Arcano: no se pasa |
| 0x0002 | `VISIBLE` | obstáculos por encima de los que no se dispara (línea de visión) |
| 0x0004 | `MISSILE_BARRIER` | frena proyectiles y lo que vuela |
| 0x0008 | `NOPLAYER` | |
| 0x0010 | `PRESET` | algunos pisos de piezas prearmadas |
| 0x0020 | `BLANK` | fuera del mapa |
| 0x0040 | `MISSILE` | |
| 0x0080 | `PLAYER` | ocupado por un jugador |
| 0x00C0 | `WATER` | (`MISSILE` + `PLAYER`) |
| 0x0100 | `MONSTER` | ocupado por un monstruo |
| 0x0200 | `ITEM` | un objeto en el piso |
| 0x0400 | `OBJECT` | cofres, barriles, altares |
| 0x0800 | `DOOR` | puertas cerradas |
| 0x1000 | `NO_PATH` | centro de una unidad |
| 0x2000 | `PET` | esbirros |
| 0x8000 | `CORPSE` | cadáveres (y portales) |

**Cómo se arma** (como `COLLISION_AllocRoomCollisionGrid`):

- Por cada baldosa de piso, pared y techo, sus 25 marcas de subtile se
  suman (OR) a la máscara. Entran también las baldosas de las salas vecinas
  que caen encima.
- Las marcas de la baldosa entera también cuentan: `0x02` → `PRESET`,
  `0x40` → `WALL`, `0x80` → `MISSILE_BARRIER`.
- Donde no hay baldosa: `BLANK`.
- Cuando algo cambia (una puerta que se abre, una pared que se rompe), se
  saca con `&= ~` en lugar de rearmar todo.

**Lo que ocupan las unidades** (`COLLISION_SetMaskWithSize` / `...Pattern`):

- **Formas:** un punto, una cruz de 5 subtiles o un cuadrado de 3×3, según
  el tamaño de la unidad.
- **Presencia:** con el bit de jugador, monstruo o esbirro, y `NO_PATH` en
  el centro.
- **Movimiento:** al moverse se saca la huella vieja, se prueba la nueva y,
  si choca con una pared o barrera, se vuelve a poner la vieja
  (`COLLISION_TryMoveUnitCollisionMask`).

**Qué bits mira cada cosa:**

- Caminar (jugador, monstruo, esbirro).
- Los que vuelan (murciélagos, buitres).
- Proyectiles (su tipo de choque de `missiles.txt`).
- Línea de visión: el trazado por subtiles de `COLLISION_RayTrace`, tipo
  Bresenham.
- Teletransporte y salto: dónde se puede caer.
- Dónde caen los objetos y los cadáveres.
- Puertas y objetos: tamaño de `objects.txt`; los que frenan proyectiles.

Los caminos de los monstruos y de los bots pasan a usar estas máscaras. Los
modos de búsqueda de camino de D2 (`D2Common/Path`) los reviso en esta fase.

**Verificación:**

- Un modo de depuración que pinta la máscara de cada subtile con un color
  por bit, para comparar con capturas en lugares conocidos del Acto 1: el
  puente del pueblo, las vallas, el monasterio, las catacumbas, Tristram.
- Pruebas chicas del armado:
  - una baldosa sintética da la máscara esperada;
  - las tres formas de huella;
  - un rayo que choca y uno que pasa.
- Pruebas en el juego:
  - una pared no se cruza;
  - un proyectil no la atraviesa pero sí pasa por encima de lo bajo;
  - el teletransporte no cae adentro de una pared.
- El Acto 1 entero con los bots, demos y ASan: no se tiene que romper nada.
- Medir en la Pi: las máscaras ocupan el doble que hoy; con un solo acto en
  memoria, igual entra.

## Fase 2: cambio de acto y Lut Gholein

**Estado (19/10/2026): hecha.** Viaje con Warriv (los dos jugadores),
`inact` en la partida, waypoints con solapas, Lut Gholein con sus NPC y
voces, Jerhyn y Kaelan en sus marcadores (`A2Q4.cpp`), tiendas de Fara,
Lysander, Drognan y Elzix, la apuesta (Gheed y Elzix) y los guerreros del
desierto (Estocada de `monseq.txt` y el aura con el código del Paladín).
Quedan para las fases que siguen: las salidas de Lut Gholein (cloacas,
desierto, palacio), que Jerhyn entre al palacio después de hablarle, y que
Kaelan se corra y deje pasar cuando se cobra "El sol oscurecido" (A2Q3,
`bPalaceOpen` en `A2Q4.cpp`); Meshif solo ofrece viajar después de Duriel.

### El mundo por acto

- **Un mundo por acto:** el armado del mundo recibe el acto.
- **El Acto 1:** queda como está.
- **El Acto 2:** su propio armado, que reusa los generadores: laberintos,
  piezas prearmadas, zonas exteriores.
- **Memoria:** se tiene en memoria un solo acto; al viajar se libera el
  otro (por la memoria de la Pi).
- **Por acto:**
  - la paleta (`palette/act2`);
  - la música (si está);
  - la luz y el ambiente.

### Viajar

- **Warriv del Acto 1**, después de Andariel: "Ir al este" → Lut Gholein.
  Reemplaza el cartel de "El Acto II todavía no está disponible".
- **Warriv del Acto 2:** "Ir al oeste", vuelve al campamento.
- **Waypoints:** el panel con solapas por acto, como el original (Acto I /
  Acto II). Los 9 del Acto 2:
  - Lut Gholein
  - Cloacas 2
  - Dry Hills
  - Halls of the Dead 2
  - Far Oasis
  - Lost City
  - Palace Cellar 1
  - Arcane Sanctuary
  - Canyon of the Magi
- **Dos jugadores:** viajan juntos (es pantalla dividida, comparten el mundo).
- **La partida guardada:**
  - el acto en el que está;
  - los waypoints del Acto 2;
  - las 6 misiones;
  - los objetos de misión.
  
  Las partidas viejas siguen cargando, y si ya mataron a Andariel pueden
  viajar.

### Lut Gholein

**El mapa:** la pieza prearmada `LutW.ds1` (`lvlprest`). Con:

- la puerta del palacio (Kaelan no deja pasar hasta que Jerhyn lo permite);
- la entrada a las cloacas;
- la salida al desierto;
- el muelle de Meshif.

**Los personajes:**

| Personaje | Qué hace |
|---|---|
| **Atma** | la posada; da la misión de Radament |
| **Drognan** | magia: varas, bastones, cetros, pociones; pistas del Santuario y del sol |
| **Fara** | herrera: armas y armaduras, repara; la que reemplaza a Charsi |
| **Elzix** | algunos objetos y **la apuesta** |
| **Lysander** | pociones y llaves |
| **Greiz** | los mercenarios del desierto |
| **Jerhyn** | el palacio; abre el paso al Harén |
| **Geglash** | charla |
| **Meshif** | el barco: el final del acto |
| **Warriv** | la caravana al Acto 1 |
| **Caín** | identifica; la misión del Báculo Horádrico |
| **Kaelan** | guardia del palacio |

También están los guardias y la gente que camina por el pueblo.

**Las tiendas:** lo de cada vendedor según sus columnas de las tablas
(como Akara, Charsi y Gheed). Elzix vende y repara.

**La apuesta** (Elzix en el Acto 2; también la sumo a **Gheed** en el
Acto 1, porque es la misma función):

- objetos sin identificar a elección;
- con la probabilidad de único, de conjunto, raro o mágico del original;
- y el nivel que sale según el del personaje.

**Los diálogos:**

- los textos de cada uno según el estado de las misiones (como en el Acto 1);
- las charlas sueltas;
- las voces, si están.

**Los mercenarios del desierto** (`hireling.txt`, Acto 2 en Normal):

- lanceros con Jab y un aura, en tres tipos:
  - Combate: Plegaria;
  - Defensa: Desafío;
  - Ofensivo: Puntería bendecida.
- **El aura:** con el código de auras del Paladín.
- **Uno por jugador:** como ahora; si tiene el del Acto 1, se cambia.
- **Al morir:** Greiz los revive.

## Fase 3: el desierto y sus monstruos

**Estado (19/10/2026):** el desierto armado como `DrlgOutPlace.cpp` y
`DrlgOutDesr.cpp` (la cadena de zonas con sus lados al azar, los bordes y
los pasos, los acantilados, las salidas, los waypoints y los rellenos; el
Canyon con sus tumbas) y la IA del original de sus monstruos
(`src/monai.inc`), con el daño elemental de los ataques. Falta: los
santuarios y el borde secundario de `lvlsub.txt`.

### Las zonas exteriores

Como en el Acto 1, de 8×8 en 8×8 baldosas, con las piezas del desierto:

- bordes `edge*`;
- relleno `desert*`, `dune`, `rock*`, `palm`, `prickly`, `scrub`, `bone*`,
  `burm*`, `mesa`;
- los lugares fijos.

Antes de armarlo, estudio cómo lo hace D2 en el generador del desierto de
D2MOO, para copiar su forma: las filas, el camino y dónde va cada cosa.

| Zona | Lo que tiene |
|---|---|
| Rocky Waste | la salida del pueblo; la entrada a la Stony Tomb |
| Dry Hills | waypoint; la entrada a los Halls of the Dead |
| Far Oasis | waypoint; los oasis (`Oasis1..12`); la entrada al Maggot Lair; Beetleburst |
| Lost City | waypoint; las ruinas (`Ruin*`); Dark Elder (`RuinDarkElder`); la entrada a las Ancient Tunnels (`RuinSewr`); aquí se oscurece el sol |
| Valley of Snakes | la entrada al Claw Viper Temple (`viper*`) |
| Canyon of the Magi | waypoint; los acantilados (`bigcliff/kings*`, `tal*`); las 7 entradas a las tumbas (`tombent*`), con su símbolo |

También van los santuarios del desierto (`shrine*`).

### Los monstruos nuevos (14 tipos de IA)

Cada uno como su IA de D2MOO. Lo que dice la tabla es de memoria: lo
confirmo en D2MOO antes de hacer cada uno.

| IA | Monstruos | Qué hacen |
|---|---|---|
| SandLeaper | saltadores | se tiran encima desde lejos |
| PantherWoman | cazadoras, Bloodwitch | atacan en grupo, se esconden |
| PantherJavelin | | tiran jabalinas |
| Scarab | escarabajos, Beetleburst | al recibir un golpe sueltan un rayo |
| Vulture | buitres | vuelan en círculos y bajan a picar |
| SandMaggot | gusanos | se entierran y ponen huevos (que nacen) |
| Swarm | enjambres | |
| ClawViper | víboras, Fangskin | embisten |
| Mummy | momias | lentas; nube de veneno al morir |
| GreaterMummy | desenvolvedores, Radament, Ancient Kaa | reviven a los muertos, rayo profano |
| Baboon | bestias de las dunas | |
| BatDemon | murciélagos demonio | |
| PinHead | | |
| SandRaider | jinetes de arena, Fire Eye | |

En cada uno: lo que hace, sus proyectiles (`missiles.txt`), sus sonidos y
sus modificadores de jefe. También los únicos con nombre del Acto 2:

- Fangskin
- Beetleburst
- Leatherarm
- Coldworm
- Fire Eye
- Dark Elder
- Ancient Kaa
- Creeping Feature
- Bloodwitch

## Fase 4: los calabozos, Radament y el Báculo Horádrico

**Estado (19/10/2026): hecha.** Los calabozos de la tabla, el cubo en el
cofre de los Halls of the Dead 3 (Caín ya no lo da), Radament con el Libro
de Habilidad, las tres piezas del Báculo con la receta del cubo, el Sol
Mancillado y la puerta del palacio, que se abre al terminar el Sol
Mancillado o cuando Drognan manda a ver a Jerhyn (`bPalaceOpen` de
`A2Q4.cpp`).

### Los laberintos

Con el generador de laberintos de siempre, el tamaño y la cantidad de salas
de `lvlmaze.txt`, y las salas de cada tipo:

| Lugar | Pisos | Lo especial |
|---|---|---|
| Cloacas (`sewer`) | 3 | waypoint en el 2; Radament en el 3 (`sewerad`); salas temáticas |
| Ancient Tunnels | 1 | debajo de la Lost City |
| Harén y sótanos del palacio (`palace`) | 2 + 3 | Harén 1 sin monstruos; waypoint en el sótano 1; Fire Eye y el portal al Santuario en el 3 |
| Stony Tomb | 2 | Creeping Feature |
| Halls of the Dead | 3 | waypoint en el 2; Bloodwitch; el cofre con el **Cubo Horádrico** en el 3 |
| Claw Viper Temple | 2 | Fangskin; el **altar con el Amuleto de la Víbora** |
| Maggot Lair | 3 | Coldworm (`magqueen`); el cofre con el **Bastón de los Reyes** |

### Los objetos de misión

- **Los tres pedazos del Báculo:**
  - el cofre con el bastón;
  - el altar con el amuleto, que al tomarlo devuelve el sol;
  - el cofre del cubo.
- **La receta del cubo:** bastón + amuleto → Báculo Horádrico (está en
  `cubemain.txt`).
- **El Libro de Habilidad:** lo suelta Radament y da un punto de habilidad.

## Fase 5: el Santuario, el Invocador, las tumbas y Duriel

**Estado (19/10/2026): hecha.** Se juegan las seis misiones de punta a
punta: `D2_A2QTEST=1` las recorre (semillas 7, 11 y 23) hasta que Meshif
ofrece el barco, y el símbolo de la tumba verdadera se ve en el registro.
También:

- **El Invocador** con su IA (`AITHINK_Fn053`): Púa glacial, Nova helada en
  anillo, Bola de fuego, alejarse; el daño de `skills.txt` en su nivel.
- **Duriel:** sus colores (`palshift.dat`), su luz, la Congelación sagrada
  (frena y enfría) y su IA.
- **El frío enfría al jugador**, el de los golpes y el de los proyectiles.
- **Los Blunderbore** (IA PinHead, `AITHINK_Fn039`) con su Smite.
- **Caminar en diagonal:** el jugador, los monstruos y los bots prueban la
  diagonal cuando no pueden por los ejes, como el camino de 8 direcciones
  del original; sin eso no se subía la escalera angosta del Santuario. La
  prueba de las zonas (`D2_ZONETEST`) también busca en 8 direcciones, y
  `D2_COLLDUMP` dibuja la colisión de un rectángulo.

Los atajos (marcados con `shortcut:` en el código):

- el muro de fuego del Invocador es otra Bola de fuego (no hay fuego en el
  piso para los monstruos);
- Debilitar no tiene efecto (el jugador no tiene ese estado);
- los modos S2 a S4 que el motor no carga (el nido de los murciélagos
  demonio) se ven con el primer ataque;
- Jerhyn no entra al palacio: se queda en su marcador;
- de las 14 frases de cada clase del Acto 2 en `sounds.txt`, el original
  dice 7: las que tienen texto en `vo.json` y voz en español latino (el sol,
  la guarida de Radament, el Templo, el Santuario, el Invocador y las
  Tumbas). Las otras 7 no tienen texto, nadie las dispara en D2MOO y D2R no
  las grabó en español latino (salvo 4 del Brujo); están en inglés y en los
  doblajes del juego original. Por pedido, el juego dispara 6 de esas 7
  (sin el archivo no suenan); `trigger_field` no, porque no se sabe cuándo
  va;
- Meshif ofrece el barco, pero el Acto III todavía no está.

**El Santuario Arcano:** un generador propio, como el del original:

- un centro con cuatro brazos de plataformas (`SanctN/S/E/W`, cruces
  `SanctNSEW`…), unidos por caminos sobre el vacío;
- el Invocador en la punta de uno de los brazos (`SummN/S/E/W`);
- waypoint en el centro.

**El Invocador:**

- IA "Summoner": Nova helada, bolas de fuego, teletransporte.
- Al lado está el **diario de Horazon**: leerlo abre el portal rojo al Cañón
  y muestra el **símbolo de la tumba verdadera**.

**Las 7 tumbas de Tal Rasha:**

- laberintos de 6 salas;
- una sola es la verdadera, elegida al azar en cada partida; tiene la sala
  `Tomb*Talrasha` con el **Orificio**;
- Ancient Kaa está en una de las falsas.

**El símbolo de la tumba verdadera** (pedido del usuario: "es importante"; sin
esto no hay forma de saber a qué tumba entrar). Cómo lo hace el original:

- La tumba verdadera (y la de Kaa, distinta) se tira al armar el acto en
  cada partida (`DrlgDrlg.cpp`: `nStaffTombLevel = 66 + rnd % 7`).
- Cada tumba tiene su glifo, uno de los objetos `ArcaneRune1..7`
  (`objects/7a..7g`, una animación azul que gira). La tabla de
  `ACT2Q6_GetObjectIdForArcaneThing`, de tumba 1..7 a objeto, es
  313, 312, 308, 310, 311, 309, 307; o sea 7g, 7f, 7b, 7d, 7e, 7c y 7a.
- **En el registro de misiones** (página de Las Siete Tumbas, después de
  leer el diario): el texto `qstsa2q61a` ("El símbolo de la verdadera
  Tumba de Tal Rasha.") y debajo, centrado en el recuadro del texto, el
  glifo de la tumba verdadera, de frente.
- **Junto al Invocador:** flotan los otros seis (`PlaceArcaneThingamajig`
  en la sala `Summ*`), detrás de la plataforma.
- **En el Cañón:** cada entrada (`talcor` = tumba 1, `talleft1..3` = 2..4,
  `talrght1..3` = 5..7, por el estilo de su baldosa especial) muestra su
  símbolo en las columnas (está en las baldosas; hay que verificarlo con
  una captura).

**El Orificio:** poner el Báculo abre el paso a la guarida de Duriel.

**Duriel** (`Duriel.ds1`):

- IA "Duriel": aura Congelación sagrada, embestida y golpes fuertes.
- Después, Tyrael (libera a Tal Rasha y abre el portal al pueblo).

**El final:** Jerhyn manda a Meshif → "¡Acto II completado!" (el Acto III no
existe todavía).

## Las 6 misiones, paso por paso

Los pasos, textos y recompensas salen de D2MOO (`QUESTS/ACT2`) y de los
textos del juego. Cada una con su página en el panel de misiones (solapa
Acto II) y compartida entre los dos jugadores, como en el Acto 1.

1. **La Guarida de Radament** (Atma):
   - bajar a las cloacas y matar a Radament;
   - levantar el Libro de Habilidad (+1 punto);
   - contárselo a Atma.
2. **El Báculo Horádrico** (Caín):
   - juntar el bastón, el amuleto y el cubo;
   - armar el Báculo en el cubo.
3. **El Sol Mancillado:**
   - al entrar a la Lost City se oscurece (menos luz en el desierto);
   - Drognan da la pista;
   - termina al tomar el amuleto del altar.
4. **El Santuario Arcano** (Drognan → Jerhyn):
   - Jerhyn deja entrar al palacio;
   - bajar por los sótanos;
   - entrar por el portal.
5. **El Invocador:**
   - matarlo y leer el diario;
   - se abre el portal al Cañón, con el símbolo.
6. **Las Siete Tumbas:**
   - encontrar la tumba verdadera;
   - poner el Báculo en el orificio;
   - matar a Duriel y hablar con Tyrael;
   - Jerhyn y Meshif: final.

## Fase 6: pruebas, bots, demos y la Pi

**Estado (19/10/2026):** `--autotest act2` (63 pasos) da 63/63 con 1
jugador (semillas 7 y 23) y con 2 (semillas 7, 11 y 31), también con ASan
(0 errores). La demostración del menú juega el Acto 2 con nivel 40 y todas
las habilidades en 6 (`D2_DEMO_ACT=1`, el Acto 1, sigue 68/68): las parejas
3, 5, 7 y 9 terminan el acto (63/63); la 1 se probó antes de los últimos
arreglos. La prueba encontró y se arregló:

- la sala de llegada del Maggot Lair: se salía por encima de la boca de la
  cueva, que devolvía al Far Oasis (ahora se usa con A, como en el original);
- las paredes sobre el piso en blanco (estilo 30) dejaban toda la baldosa
  como vacío;
- Tyrael: salía sobre la tarima bloqueada, A no hablaba fuera del pueblo y
  su portal quedaba dentro de la pared;
- el bastón y el amuleto se ponían solos al levantarlos (y la demostración
  después los vendía al comprar algo mejor): ahora van siempre a la mochila;
- los bots, peleando al lado del diario de Horazon, lo leían con A y se
  quedaban leyendo sin caminar: ahora lo cierran;
- en las escaleras angostas en diagonal del Santuario el bot no veía más
  allá de su subtile y apuntaba a su centro; con un paso largo (la Asesina
  con Ráfaga de velocidad) iba y volvía sobre él sin avanzar, y como se
  movía nunca se destrababa (el J2 de la pareja 5 se quedaba en el
  Santuario): ahora apunta al subtile siguiente del camino.

**En la Pi (19/10/2026):** subido con el juego cerrado (lo que faltaba:
112 archivos, 11 MB, más el binario). El selftest da BIEN y `D2_A2QTEST`
recorre las seis misiones hasta Meshif. Armar una zona (con la Pi frenada
por baja tensión, 0x50005): el mapa tarda hasta 0,9 s (la Lost City) y sus
unidades hasta 3,8 s (la guarida de Duriel) cargando los gráficos de una;
en el juego los gráficos van en el otro hilo, pero el mapa no.

**Rendimiento (19/10/2026):** jugando en la Pi el Acto 2 se sentía pesado.
Midiendo la demo del Acto 2 contra la del Acto 1 con el mismo binario, salieron
tirones de 400 a 600 ms y el dibujo más caro (13,4 ms contra 8,9). Se
arregló:

- las baldosas se decodifican al dibujarse por primera vez, no al armar la
  zona;
- los gráficos de proyectiles y efectos se cargan en el otro hilo;
- el aspecto viejo del personaje se suelta en el otro hilo;
- las filas visibles ya no dependen del ancho del mapa, que en el Acto 2 es
  enorme;
- el objeto más cercano en el piso ya no recorre los 512 lugares;
- dos partes del HUD ya no dividen por píxel.

El tick más largo bajó de 401 a 115 ms, el mapa del Rocky Waste de 351 a 72
y el de Lut Gholein de 520 a 80. Los números, cómo medir y las reglas para
el Acto 3 están en [rendimiento.md](rendimiento.md). La Lost City y la
guarida de Duriel no se volvieron a medir con estos cambios.

- **`--autotest act2`:** dos bots juegan el Acto 2 entero, como la prueba
  del Acto 1:
  - cada paso de las 6 misiones;
  - los 9 waypoints;
  - los jefes;
  - con tiempos, memoria y ticks lentos.
- **Demos:** la demostración del menú suma parejas que juegan el Acto 2
  (nivel 30, todas las habilidades).
- **Prueba de habilidades** contra Radament, el Invocador y Duriel
  (`D2_ST_MONSTER`).
- **Regresiones:**
  - el Acto 1 entero (68/68);
  - las demos 1 a 9;
  - todas las clases;
  - ASan.
- **La Pi:**
  - empaquetar los datos nuevos del Acto 2: unos 100 MB (38 de mapas, 41 de
    monstruos y personajes, más los objetos); en la Pi quedan 19 GB libres;
  - medir el desierto y el Santuario (lo más pesado);
  - subirlo con el juego cerrado.
- **El README y la memoria.**

## Después del Acto 2: bots que eligen bien sus objetos y usan el cubo

Lo que pediste para después:

- **Elegir objetos** según su clase y cómo pelea:
  - más a sus habilidades y a sus solapas;
  - lanzamiento más rápido para los que lanzan hechizos;
  - velocidad de ataque y daño para los de cuerpo a cuerpo;
  - vida, resistencias que le falten y defensa;
  - comparando con lo que tiene puesto.
- **Engarzar** gemas y runas donde convenga, y armar palabras rúnicas
  cuando tenga las runas y el objeto.
- **Usar el cubo cuando le conviene:**
  - subir gemas y runas;
  - pociones de rejuvenecimiento;
  - recetas de engarces y de reroll;
  - artesanías si tiene los materiales;
  - y el Báculo Horádrico.
  
  Se hace con las recetas del cubo que ya tenemos (`cube.c`).

## Lo que se decidió (19/10/2026)

1. **Herramientas, en `D:\tools`:**
   - Java 21 (Temurin 21.0.12) y Ghidra 12.1.4, con sus sumas de control
     verificadas;
   - D2MOO clonado;
   - el proyecto de Ghidra está en `D:\tools\re`, sobre una copia de
     `D2R.exe`.
2. **D2R está instalado** en `D:\Battle.net\Diablo II Resurrected`. De ahí
   saqué:
   - la música del Acto 2: 11 temas, 138 MB;
   - las voces del Acto 2 en español latino: 400, 159 MB.
3. **El Cubo Horádrico, como el original:** sale del cofre del nivel 3 de los
   Halls of the Dead. Caín deja de darlo cuando ese cofre exista (fase 4).
   Los personajes que ya lo tienen lo conservan.
4. **Dos jugadores:** viajan de acto juntos.
