# Rendimiento en la Pi: cómo medir y qué cuidar en lo nuevo

La Raspberry Pi 3 (Cortex-A53, y a 600 MHz cuando le falta voltaje) es
mucho más lenta que la PC. En la demo del Acto 2 el dibujo tarda 12 veces
más (12,7 ms contra 1,1) y la lógica 25 veces más (1,1 ms contra 0,04).
Algo que en la PC no se nota, en la Pi traba el juego. Esta guía junta lo
que se aprendió al arreglar el Acto 2, que "quedó pesado", para no
repetirlo en los actos que siguen.

## Cómo medir

Medir siempre en la Pi y con lo que ve el jugador, no solo con escenas
quietas.

- **La demo en movimiento** es la medida que cuenta: dos bots peleando con
  todas sus habilidades, la cámara moviéndose y las zonas armándose.

  ```bash
  cd /userdata/roms/ports/.data/d2port
  D2_ASYNC=1 D2_AUTOTEST_DEMO=1 D2_DEMO_ACT=2 D2_AT_PERF=250 D2_RENDER_EVERY=1 D2_NOSOUND=1 \
      D2_SEED=7 D2_SAVE_DIR=/tmp/sv /tmp/d2port --headless --players 2 --autotest act2 --frames 5000
  ```

  - `D2_DEMO_ACT=2`: el acto de la demo lo elige esta variable (sin ella,
    el 5, el de la demostración del menú); el nombre de `--autotest` no
    cuenta con `D2_AUTOTEST_DEMO`.

  - `D2_ASYNC=1`: carga en el hilo de trabajos, como el juego. Sin eso, las
    pruebas sin ventana hacen cada trabajo en el momento (para que den
    siempre igual): sirve para comparar dos binarios, pero lo que se mandó
    al otro hilo se sigue midiendo en el tick. Con `D2_ASYNC` el hilo
    arranca antes de `game_init`, como con ventana. `--autotest` usa siempre el
    hilo (`main.c`, desde que termina de cargar) y dos corridas con la misma semilla se separan (el
    19/10/2026 también sin ASLR y con las variables locales en cero: lo
    más probable es que dependa de cuándo termina de cargar cada gráfico).
    Un paso que falla se revisa con varias semillas y varias corridas.
  - `D2_AT_PERF=N`: cada N ticks, la lógica y el dibujo de ese tramo
    (promedio y peor), el nivel y las unidades.
  - `D2_RENDER_EVERY=1`: dibuja en cada tick, como el juego.
  - `--frames N`: corta la prueba a los N ticks y sale normal (así `gprof`
    escribe su archivo).
  - El binario de prueba se copia a `/tmp` (no se toca el del juego) y se
    corre desde la carpeta del juego, que tiene `data/`. Antes, `pgrep -x
    d2port`: no correr pruebas con el juego abierto.
- **Comparar actos con el mismo binario**: la misma demo con
  `D2_DEMO_ACT=1 ... --autotest act1` y con el Acto 2. Así se sabe si lo
  pesado es el acto nuevo o la demo.
- **Los tirones** salen solos en el log: un tick de más de 50 ms escribe
  `tick N de X ms: zonas … jugadores … monstruos … proyectiles …` (en qué
  parte se fue el tiempo). `D2_ZONES=1` agrega lo que tardó cada zona en
  armarse (mapa y unidades).
- **`scripts/bench.sh`** mide escenas quietas del Acto 1 (dibujo y
  conversión). Sirve para ver si el dibujo empeoró, pero con la cámara
  quieta el piso sale de la capa guardada: no muestra lo que cuesta
  moverse.
- **`gprof` en la Pi**: un binario compilado con `-pg` (en WSL,
  `~/build-pi-prof`, con `-mcpu=cortex-a53 -pg -g -fno-omit-frame-pointer`)
  corriendo la demo de arriba con `GMON_OUT_PREFIX=/tmp/gmon` (el archivo va
  a `/tmp`, no a la carpeta del juego). Se analiza en la PC con
  `aarch64-linux-gnu-gprof -b -p` (por función), `-q` (quién llama a quién) y
  `-l` (por línea). `scripts/prof.sh` hace lo mismo con tres escenas quietas.
  Cuidado:
  - cuenta también el hilo de trabajos (decodificar gráficos ahí no traba);
  - pone mal el nombre de algunas funciones `static`: usar `-l` (por línea)
    para ver dónde está de verdad;
  - no ve el tiempo de libc (`memcpy`, `malloc`, `free`) ni la espera de
    disco. Si el reloj dice mucho más que el perfil, el tiempo está ahí:
    medir con relojes alrededor de cada etapa.
- **La imagen tiene que dar igual**: antes y después de optimizar,
  `scripts/cmpshots.sh VIEJO NUEVO` saca capturas de 17 escenas (los dos
  actos, oscuridad, pantalla partida, HUD, inventario, mapa) con los dos
  binarios y las compara byte por byte. La pausa cambia sola de una corrida
  a otra: esa no cuenta.

## Lo que pasaba en el Acto 2 (19/10/2026)

La demo del Acto 2 en la Pi, comparada con la del Acto 1 con el mismo
binario: el dibujo costaba 13,4 ms por cuadro contra 8,9, y había tirones de
hasta 600 ms. Las causas, de la más grave a la menos:

| Problema | Dónde | Costo en la Pi | Arreglo |
|---|---|---|---|
| Decodificar todas las baldosas de cada DT1 al armar una zona, aunque se usaran pocas | `fmt/dt1.c` | el Rocky Waste: 280 ms de 351 | `dt1_load_lazy`: al armar se leen las cabeceras; cada imagen se decodifica la primera vez que se dibuja (`dt1_tile_ready`). 351 → 72 ms |
| El gráfico de un proyectil se leía en el hilo del juego la primera vez que se disparaba | `missiles.c` | hasta 595 ms | se lee en el hilo de trabajos; mientras, no se dibuja |
| Lo mismo con los efectos (`overlays.c`) | `overlays.c` | según el efecto | igual; mientras se carga, el efecto espera sin avanzar |
| Soltar el aspecto viejo del personaje al cambiar de arma (miles de `free`) | `look_tick` en `game.c` | 36 a 80 ms | se suelta en el hilo de trabajos (`drop_anims`) |
| Las filas visibles se calculaban restando el ancho entero del mapa: en el mapa del Acto 2 eran unas 1650 por vista en vez de 60 | `visible_rows` en `game.c` | 0,5 ms por cuadro | el rango exacto con los dos límites de `visible_cols` |
| El objeto más cercano en el piso recorría los 512 lugares en cada cuadro | `items_ground_nearest` | 0,4 ms por cuadro | hasta el último usado (`s_ground_top`) |
| Los íconos escalados del HUD dividían en cada píxel | `fb_blit_scaled` | 0,3 ms por cuadro | la tabla de columnas, una vez por llamada |
| Los recuadros oscuros del HUD de a un píxel | `fb_shade_rect` | 0,7 ms por cuadro | `darken_span` (NEON, de a 16) |

También se agregó `fs_prefetch_dir`: al empezar un acto se le pide al
sistema que lea por adelantado sus baldosas (`posix_fadvise`). Con la
tarjeta SD fría evita esperar el disco al armar la primera zona de cada
tipo (con los archivos ya en caché no cambia nada).

Los costos por cuadro son de `gprof` en la Pi, con la demo a pantalla
partida: el tiempo de la función dividido por los cuadros.

Resultado en la Pi, con los mismos 5000 ticks de la demo:

| | Antes | Después |
|---|---|---|
| Tick más largo | 401 ms (606 en la primera corrida, con la tarjeta fría) | 115 ms |
| Tirón del primer proyectil | 595 ms | no aparece |
| Tirones al cambiar de arma | 54 a 79 ms | no aparecen |
| Armar el Rocky Waste (mapa) | 351 ms | 72 ms |
| Armar Lut Gholein (mapa) | 520 ms | 80 ms |
| Armar las Cloacas 1 / el Harén 1 | 76 / 66 ms | 27 / 12 ms |
| Dibujo promedio | 13,4 ms | 12,7 ms (el Acto 1: 8,9) |
| Lógica promedio | 1,37 ms | 1,09 ms (el Acto 1: 1,23) |

Con `D2_ASYNC=1` (la carga en el otro hilo, como el juego) dio lo mismo:
lógica 1,10 ms, dibujo 12,66 ms, peor tick 115 ms. Las capturas de las 16
escenas que no cambian solas dieron iguales byte por byte. Lo que va al otro
hilo se probó con ThreadSanitizer (`~/build-tsan`; en WSL corre con
`setarch -R`, si no se cae por el mapeo de memoria): 12.000 ticks de la demo
del Acto 2 con `D2_ASYNC=1`, sin avisos.

### Lo que queda

- **El primer cuadro de una zona** decodifica de una vez las baldosas que se
  ven: en Lut Gholein ese cuadro tarda 103 ms. Se podría repartir en varios
  cuadros o adelantar en el hilo de trabajos.
- **Más tarde en la demo** había zonas de 225 ms (ticks 7366 y 8067, más
  allá de los 5000 medidos); no las volví a medir con los arreglos.
- **El dibujo del Acto 2 sigue más caro que el del Acto 1** (12,7 contra
  8,9 ms). La diferencia es lo que hay en pantalla: los calabozos oscuros
  con pantalla partida y la demo de nivel 40, con muchos efectos.
  - Las luces de los monstruos en los niveles oscuros: el bench de las
    Catacumbas 4 da hoy 19,3 ms, contra 14,0 el 15/10. La diferencia es
    sobre todo esa luz, que suma del 25 al 35 % a la conversión del cuadro.
  - Efectos translúcidos sobre la oscuridad (`dark_resolve_slow`): del 7 al
    13 % del perfil.
  - El HUD: unos 3 ms por cuadro.
- **Fuera del juego**, en la Pi con poco voltaje: subir la imagen a la
  pantalla cuesta 8,4 ms por cuadro y mostrarla 3,5 ms. La opción COLOR 16
  BITS achica lo primero.
- **Al empezar la demo**, el campamento del Acto 1 tarda 532 ms en armar
  sus unidades. Pasa en la carga, no jugando.

## El Acto 3 (19/10/2026)

La demo de cada acto en la Pi (con poco voltaje, a 600 MHz), con el mismo
binario, `D2_ASYNC=1`, 2 jugadores y 5000 ticks:

| | Acto 1 | Acto 2 | Acto 3 |
|---|---|---|---|
| lógica por tick | 0,82 ms | 0,91 ms | 1,07 ms |
| dibujo por cuadro | 7,75 ms | 11,40 ms | 9,72 ms |

El promedio estaba bien desde el principio. Lo que trababa era **armar
una zona**: la primera medición dio 247 ms el Spider Forest y 273 ms el
Great Marsh (también el Acto 1: 197 ms el Blood Moor). gprof no servía
para verlo: mezcla el hilo de trabajos (decodificar gráficos, que no traba)
y no ve el tiempo de libc (`strcasecmp`). Lo que sí sirvió fue una copia
del código con relojes alrededor de cada parte del armado, compilada para
la Pi y corrida ahí. Había cinco cosas, todas en el hilo del juego:

1. **Buscar una columna de una tabla .txt** (`txt_col`) comparaba el nombre
   con todas las columnas (monstats.txt tiene casi 300), y se busca miles de
   veces por zona. Ahora cada tabla arma al cargarse un índice de los
   nombres (`fmt/txt.c`; después no cambia, así que se puede leer desde
   cualquier hilo).
2. **Buscar cada baldosa** (`find_tile`, `map.c`) en los ~30 DT1 de una
   pieza del Acto 3, dos veces cada uno. Ahora cada DT1 guarda su primera y
   su última clave: los que no pueden tenerla ni se buscan, y los demás una
   sola vez.
3. **Buscar un lugar libre para cada unidad nueva** (`new_unit`) recorría
   desde el principio las miles que ya hay; en la Pi, un fallo de caché por
   cada una. Ahora se recuerda desde dónde puede haber uno libre (el mismo
   lugar que antes, así que todo da igual).
4. **Buscar la fila de un monstruo** en monstats.txt por su Id, unas cuatro
   veces por monstruo: ~700 filas separadas en memoria. Ahora se recuerda
   (`mon_row`, `units.c`).
5. **Leer los DT1 que la zona usa por primera vez**: la primera selva carga
   ~8 MB (unos 30 ms). Ahora, cuando un jugador está a 180 subtiles de un paso
   (la zona se arma a 90), el hilo de trabajos lee y ordena los DT1 que va a
   pedir esa zona (`world_zone_dt1`, `map_prepare_dt1`), y al armarla
   `get_dt1` se queda con los que ya están listos. Si alguno todavía no
   está, lo lee en el momento como antes (nunca espera) y el del hilo se
   descarta. Por ahora sabe la lista de las zonas del Acto 3 (las de piezas
   de lvltypes.txt); `D2_ZONES=1` dice cuántos llegaron preparados.

Armar una zona en la Pi, antes y después:

| zona | antes | después |
|---|---|---|
| Blood Moor | 197 ms | 80 ms |
| Den of Evil | 134 ms | 93 ms |
| Lut Gholein (al empezar) | 1406 ms | 56 ms |
| Rocky Waste | 174 ms | 69 ms |
| Spider Forest | 247 ms | 93 ms |
| Great Marsh | 273 ms | 97 ms |
| Spider Cave | 86 ms | 17 ms |

Después se sumaron los jefes y campeones al azar de cada nivel (con sus
escoltas), las IA del original de los monstruos del Acto 1 y los
tentáculos. Medido otra vez en la Pi con la misma demo:

| | Acto 1 | Acto 2 | Acto 3 |
|---|---|---|---|
| lógica por tick | 0,81 ms | 0,92 ms | 1,24 ms |
| dibujo por cuadro | 7,89 ms | 11,09 ms | 10,17 ms |
| peor tick | 71 ms (Blood Moor) | 72 ms (Rocky Waste) | 99 ms (Spider Forest) |

El Spider Forest quedó justo debajo de los 100 ms: sus unidades pasaron de
29 a 35 ms por los jefes y sus escoltas.

Las capturas de las 16 escenas de `cmpshots.sh` que no cambian solas y de
seis del Acto 3 (el muelle, el Great Marsh, el Flayer Jungle, el bazar,
Travincal y el Durance) dieron iguales byte por byte, y `D2_ZONETEST` de los
tres actos, igual línea por línea. Lo del hilo, con ThreadSanitizer (30.000
ticks de la demo del Acto 3 con `D2_ASYNC=1`, con DT1 preparados y
descartados) y ASan: sin avisos.

### Lo que queda del Acto 3

- **El Spider Forest y el Great Marsh quedan justo abajo de los 100 ms**
  (99 y 88 con los jefes al azar): lo que más pesa ahora es estampar las
  piezas (~40 ms), las unidades (~35 a 45) y la colisión (10). Si algo
  más los empuja arriba, lo próximo es repartir el armado de las unidades
  en dos ticks.
- Los DT1 de las zonas de los Actos 1 y 2 no se preparan antes (salen de la
  lista de cada DS1, que se elige al armar); hoy ninguna pasa de 100 ms.
- **El primer cuadro en el muelle** tarda 118 ms en dibujarse (el mismo
  pendiente del primer cuadro de una zona, de arriba).
- El campamento del Acto 1 sigue tardando ~510 ms en armar sus unidades al
  empezar (con los gráficos que todavía se cargan sin el otro hilo).
- En la demo del Acto 2 salió un tick de 57 ms en "jugadores" (no en las
  zonas), en el tick 4472 en dos corridas; con las IA nuevas la demo cambió
  y en la última medición no apareció. Falta ver qué era.

## Los objetos que se usan y el grosor de las paredes (20/10/2026)

Medido en la Pi con la fuente floja (`throttled=0x50005`, el procesador a
600 MHz, la mitad de lo normal), la demo de cada acto con `D2_ZONES=1`
(que ahora dice "de eso objetos N ms"), dos vueltas de cada binario
intercaladas y con la caché de disco caliente. Total de armar la zona (mapa
+ unidades), en ms:

| zona | antes (binario de las 05:11) | ahora | de eso, objetos |
|---|---|---|---|
| Kurast Docktown | 64 | 178 | 0 |
| Spider Forest | 140 | 219 | 12 |
| Great Marsh | 116 | 130 | 14 |
| Spider Cave | 30 | 58 | 1 |
| Lut Gholein | 66 | 234 | 0 |
| Sewers Level 1 | 40 | 69 | 1 |
| Rocky Waste | 76 | 106 | 8 |
| Dry Hills | 52 | 68 | 10 |
| Halls of the Dead Level 1 | 44 | 106 | 2 |

- El reparto de objetos (`obj_zone_loaded`: `ObjGrp` por sala, crearlos y
  ver cuáles ya se abrieron) cuesta de 0 a 14 ms a 600 MHz (unos 7 a la
  velocidad normal). Para eso se crea cada objeto por su fila de
  objects.txt (`units_place_object_row`, sin buscar la clase entre 580
  filas), los gráficos de cada fila y modo se recuerdan (`object_gfx`) y lo
  ya abierto se busca con un índice (`s_omem_hash`).
- Casi todo lo demás que subió es la parte del **mapa**: el grosor de las
  paredes (`COLL_VOLUME`, `dt1_volume` y `volume_cuts` en
  `map_build_collision`), de la misma fecha. Con gprof, `map_build_collision`
  es unos 18 ms por zona en la demo del Acto 2, y en la marcha
  `map_clearance2` se llama 5 a 7 millones de veces en 9000 ticks.
- A la velocidad normal (la mitad de estos números) el Spider Forest y Lut
  Gholein quedan arriba de los 100 ms de la regla 5: lo que queda por
  bajar está en el grosor de las paredes (se bajó esa misma tarde: ver
  abajo).
- En la marcha, lo de los objetos por tick (el cartel, A, el bot) suma unos
  0,03 ms.

### El grosor de las paredes, más barato (20/10/2026, a la tarde)

`D2_ZONES=1` ahora dice también "colisión X + grosor Y + recorte Z ms" (las
tres partes de `map_build_collision`). Lo que se cambió, con la colisión
**igual bit por bit** (`D2_COLLHASH=1` con `D2_ZONETEST`: el hash de los
`coll` de las 144 zonas de los tres actos, igual al de antes):

- **`dt1_volume`** (el grosor: era casi todo, 169 ms en Lut Gholein): primero
  saca de las marcas los subtiles que pueden tener grosor (libres, con una
  pared al lado); un tile sin ninguno no se decodifica, y uno con alguno
  decodifica solo los bloques de las filas que se miran (y = -79..15 desde el
  tile) y sin spans (`DECODE_BAND` en `decode_tile`).
- **El recorte** (`volume_cuts`): era recorrer una grilla de toda la zona
  (Arcane Sanctuary, 926x926, no tiene ni un subtile con grosor; Den of Evil,
  646x646 por 55). Ahora va pedazo por pedazo, solo los que recibieron
  grosor, cada uno con una grilla de 46x46 (el pedazo y 3 subtiles
  alrededor) copiada del mapa como va quedando.
- **`map_clearance2`** (cientos de veces por tick): cuando los 9 subtiles
  caen en el mismo pedazo (el 91 % de las veces) los lee directo. Con un
  binario que calculaba también la cuenta vieja y cortaba si diferían: 46
  millones de llamadas iguales en las pruebas de los Actos 2 y 3.
- **`s_units` alineado a 64 bytes.** Al medir apareció que la lógica por
  tick subía un 11 % (0,93 → 1,04 ms en el Acto 1) por agregar 24 bytes de
  una variable en map.c: corría `s_units` dentro de la línea de caché y cada
  `Unit` (1600 bytes, 25 líneas) quedaba partida distinto. Se encontró
  armando la fuente de antes y volviendo a poner los cambios de a uno; con
  otra alineación del código no cambiaba, con la de los datos sí. Alineado,
  ya no depende de lo que haya antes.

En la Pi (igual que arriba: 600 MHz, la demo de cada acto, dos vueltas
intercaladas del binario instalado y el nuevo), el total de armar la zona
(mapa + unidades) en ms y, del nuevo, las tres partes de la colisión:

| zona | antes | ahora | colisión + grosor + recorte |
|---|---|---|---|
| Rogue Encampment (solo el mapa) | 99 | 44 | 2,0 + 6,8 + 1,5 |
| Blood Moor | 112 | 90 | 6,4 + 6,9 + 3,2 |
| Lut Gholein | 239 | 84 | 3,8 + 16,6 + 2,1 |
| Sewers Level 1 | 68 | 48 | 5,1 + 6,8 + 2,2 |
| Harem Level 1 | 35 | 18 | 0,7 + 3,1 + 0,4 |
| Rocky Waste | 106 | 92 | 6,3 + 5,2 + 2,8 |
| Halls of the Dead Level 1 | 106 | 58 | 5,8 + 9,8 + 2,2 |
| Kurast Docktown | 184 | 73 | 3,6 + 8,5 + 1,8 |
| Spider Forest | 232 | 171 | 12,2 + 12,7 + 11,0 |
| Great Marsh | 128 | 130 | 11,6 + 6,4 + 9,2 |
| Spider Cave | 59 | 36 | 1,9 + 5,2 + 1,6 |

| | antes | ahora |
|---|---|---|
| lógica por tick, Acto 1 | 0,933 ms | 0,920 ms |
| lógica por tick, Acto 2 | 1,220 ms | 1,174 ms |
| lógica por tick, Acto 3 | 1,636 ms | 1,586 ms |
| peor tick, Acto 1 / 2 / 3 | 113 / 116 / 220-248 ms | 90 / 93 / 172 ms |

- Lo que queda arriba de 100 ms a 600 MHz es el **Spider Forest** (171) y el
  **Great Marsh** (130), por estampar las piezas y las unidades (42 y 58), no
  por la colisión (36 y 27 ms entre las tres partes). A la velocidad normal
  quedan en ~85 y ~65.
- La "colisión" de las baldosas (12,6 ms en Den of Evil) es la del original;
  no se tocó.
- Ojo al medir: el mismo programa con las variables estáticas corridas unos
  bytes puede cambiar un 10 % por tick en la Pi. Antes de culpar a un cambio
  que no corre por tick, probar con el arreglo sospechoso alineado.

## Armar las zonas en partes (21/10/2026)

Medido en la Pi con la fuente floja (600 MHz), `D2_ZONES=1` ahora dice cuánto
fue cada parte: estampar las piezas (y de eso leer los DS1 y los DT1), la
colisión, las unidades de las piezas, los objetos al azar y los monstruos,
en cuántos ticks se armó la zona y el que más tardó. Armando todas las
zonas de una vez (`D2_ZONETEST`), lo que más pesaba era **leer los DT1 en el
hilo del juego**: 298 de los 391 ms del Santuario del Caos, 206 de los 324
del Spider Forest, 180 de los 337 del Lower Kurast. Solo los Actos 3 y 4 los
pedían antes al hilo de trabajos, y al llegar a un acto el pedido quedaba
detrás de cientos de gráficos del pueblo. Lo demás (estampar, la colisión,
las unidades) era de 20 a 70 ms cada parte, y juntas pasaban los 100.

Lo que se cambió:

- **El mapa en pasadas** (`world_zone_build_step`, `map_piece_now`): cada
  pieza (un DS1 o un relleno de piso) cuenta. El generador no lee el mapa
  (decide con sus propias grillas), así que se lo corre otra vez en cada
  tick con la misma semilla: decide lo mismo y estampa solo las piezas que
  faltan y entran en `ZONE_BUDGET` (20 ms; `D2_ZONE_BUDGET`). La colisión va
  en dos ticks más (`map_build_collision_part`: las marcas de las baldosas, y
  el grosor con su recorte). Mientras tanto los pedazos son pared, y la zona
  no cuenta como armada hasta el final.
- **Las unidades, de a una parte por tick** (`zone_build` en `game.c`): las
  que traen las piezas, los objetos al azar de las salas y los monstruos del
  nivel; y cada parte, también de a pedazos (ver "Las unidades del Acto 5",
  abajo). Si alguien ya está adentro o a menos de 30 subtiles del paso
  (`ZONE_URGENT`), se termina todo de una; lo mismo al llegar con un
  waypoint, al cambiar de acto o en las pruebas (`zone_load`).
- **Los DT1 en el hilo de trabajos para todas las zonas** (las de los Actos 1
  y 2 piden los del tipo del nivel en `lvltypes.txt`), **adelante de los
  gráficos** (`jobs_push_first`). Una pieza que necesita uno que todavía no
  llegó no lo lee: lo pide y queda para el tick siguiente (`dt1_defer`, hasta
  5 segundos; después se lee como antes).
- **La fila de cada pieza en `lvlprest.txt`, por índice** (`prest_row`): con
  las pasadas se buscaba cada pieza muchas veces entre ~1100 filas y en las
  mesetas costaba más que estamparlas (el Plains of Despair pasó de 224 a 49
  ms de mapa). También el tipo de nivel de cada zona se recuerda.
- Sin el hilo de trabajos (las pruebas sin gráficos) el tope es de 16 piezas
  por tick (`D2_ZONE_PIECES`) y no de tiempo.

El mapa sale **igual bit por bit**: `D2_ZONETEST` con `D2_COLLHASH=1` y
`D2_OBJ_TEST=1` de los cuatro actos, armado entero y con `D2_ZONE_PARTS=1`
(una pieza por pasada), da las mismas líneas que el binario de antes.

En la Pi, con 2 jugadores y `D2_ASYNC=1`, el binario instalado y el nuevo
intercalados con la caché del disco caliente (las demos de los Actos 2 y 3 y
la prueba del Acto 4 desde la bajada al río), 9000 ticks:

| | peor tick antes | ahora | ticks de más de 50 ms (antes / ahora) | lógica por tick (antes / ahora) |
|---|---|---|---|---|
| Acto 2 | 130 ms | 44 ms | 4 / 0 | 1,22 / 1,24 ms |
| Acto 3 | 171 ms | 32 ms | 4 / 0 | 1,64 / 1,64 ms |
| Acto 4 (el río) | 166 ms | 38 ms | 4 / 0 | 1,66 / 1,65 ms |

En el Acto 1 (sin intercalar) el peor pasó de 105 a 39 ms. El promedio por
tick no cambia: correr el generador otra vez en cada pasada no se nota.

Cada zona se arma ahora en 6 a 8 ticks, el peor de 6 a 36 ms (el Spider
Forest 30, el River of Flame 36), con todos sus DT1 preparados. Lo que sigue
armándose de una vez es el pueblo al llegar al acto (con la pantalla de
carga).

## Las unidades del Acto 5, de a partes (21/10/2026)

En la Pi (600 MHz, 2 jugadores, `D2_ASYNC=1`, la demo del Acto 5 entera) las
zonas del Acto 5 seguían trabando, por las unidades: las de los DS1 salían
todas en un tick (las Colinas Sangrientas traen unas 740: 164 ms; las
Tierras Altas 84, la Meseta de Arreat 77, los salones del templo 44 a 51), y
los objetos al azar (hasta 37 ms) y los grupos de monstruos también iban de
una. Y con 6 zonas armadas, borrar la más vieja (20 a 51 ms: liberar sus
unidades y `malloc_trim`) caía en el mismo tick que el primer pedazo de la
que seguía.

Lo que se cambió:

- **Cada parte de las unidades, de a pedazos**, con lo que queda de
  `ZONE_BUDGET` en el tick: las de los DS1 (`units_spawn_presets(m, level_of,
  from, cap, ms)`), las salas de los objetos al azar (`obj_zone_step`; cada
  sala con su semilla, así que sale igual) y los intentos de grupos de
  monstruos (`zone_monsters_step`). Sin el hilo de trabajos, topes fijos: 64
  unidades, 8 salas y 16 intentos por tick. Armada entera (urgente, o
  `D2_ZONETEST`) sale igual que antes: `D2_ZONETEST` con `D2_COLLHASH=1` y
  `D2_OBJ_TEST=1` de los cinco actos, entero y con `D2_ZONE_PARTS=1`, da las
  mismas líneas, y jugando los objetos de cada zona salen iguales.
- **Borrar una zona para hacerle lugar a otra ocupa su propio tick**, y
  `malloc_trim` va en el siguiente sin armar nada más (salvo que sea urgente).
- `D2_ZONES` dice en qué parte fue el peor tick de cada zona (mapa, presets,
  objetos, monstruos, o "entera").

El peor tick de cada zona en la Pi, antes y ahora (la demo de la pareja 9,
semilla 7):

| Zona | antes | ahora |
|---|---|---|
| Bloody Foothills | 164 ms (134 la segunda vez) | 30 ms (22) |
| Frigid Highlands | 84 ms | 24 ms |
| Arreat Plateau | 77 ms | 23 ms |
| Halls of Anguish / Death's Calling | 51 / 44 ms | 21 / 20 ms |
| Tundra Wastelands | 41 ms | 28 ms |
| Infiernos, cuevas, Fortaleza, Trono | 19 a 38 ms | 17 a 24 ms |
| Borrar una zona (con el armado de la siguiente) | 44 a 66 ms | 3 a 26 ms, aparte |

Ticks de más de 50 ms en toda la demo: antes 16, de los que 10 eran de armar
o borrar zonas; ahora ninguno armando en partes. Los que quedan:

- **Llegar por un portal o un waypoint** arma la zona entera (alguien ya está
  adentro): el templo de Nihlathak 31 ms, la Cámara de Baal 38 (60 con la
  zona que se borra para hacerle lugar), las cuevas por waypoint 74 (88).
- **El primer uso de una habilidad** (arreglado): de 52 a 105 ms en un
  tick, en "jugadores", "proyectiles" o "misiones". No era el disco sino el
  botín del monstruo que mataba la habilidad nueva: ver "Los tirones del
  primer uso", abajo.
- **Rachas de proyectiles** (arreglado): con 100 a 350 proyectiles y ~2000
  unidades (las oleadas de Baal, el Frozen River), `tick_missiles` pasaba
  los 45 ms varios ticks seguidos en la Pi. Midiendo por tipo de proyectil
  y por parte, eran los fuegos que deja en el piso la Ola llameante del
  Brujo (`flamewavelingerfire`, de 200 a 600 a la vez): `burn_tick`
  buscaba con `units_enemies_near` en todas las unidades del acto, por
  fuego y por tick, y cada `Unit` (1600 bytes) es un fallo de caché en la
  Pi. En la PC eran 1,5 a 2,9 ms por tick, el 85 % del tiempo de los ticks
  lentos de proyectiles. Ahora las búsquedas de monstruos
  (`units_enemies_near`, `units_enemies_around`, `units_find_enemy`,
  `units_find_enemy_path`) miran solo las celdas de alrededor de una grilla
  de 4×4 subtiles (`grid_box` en units.c), que se actualiza en cada lugar
  que mueve una unidad (`grid_moved`). Dan las mismas unidades en el mismo
  orden: `D2_GRID_CHECK=1` lo compara en cada búsqueda y pasó los cinco
  actos, las habilidades de las 8 clases y las 9 demos. En la Pi (demo de
  la pareja 9, semilla 7, de a una corrida): antes, 158 ticks de
  `tick_missiles` de más de 20 ms, en rachas de hasta 52 ticks seguidos, el
  91 % en `burn_tick`, y 83 ms el peor; con la grilla, 277 fuegos cuestan
  0,24 ms de `burn_tick` por tick y quedaban 4 ticks sueltos en que un
  golpe mataba (el botín). Con el botín arreglado (`type_is` por tabla de
  bits), ningún tick de `tick_missiles` llega a 20 ms en toda la demo: el
  peor, 10 ms al matar a Baal. Los sigilos, las guardas y los efectos (lo
  demás de "proyectiles") no pasan de 5 ms.

## Los tirones del primer uso (21/10/2026)

En la demo del Acto 5 en la Pi (600 MHz, la pareja 9, semilla 7,
`D2_ASYNC=1`) había ticks de 52 a 105 ms justo después de "demo Jx usa ...
por primera vez", y en la PC el mismo tramo no pasaba de 4 ms. Parecía la
tarjeta SD, pero no: era **CPU, al generar el botín** del monstruo que la
habilidad nueva mataba.

Cómo se encontró, con una copia del código compilada para la Pi (no en el
repo): `fs_read` avisa cuando lo llama el hilo del juego (`gettid() ==
getpid()`) y cuánto tardó, y un hilo vigía que, cuando un tick pasa de N ms,
cada 3 ms le manda `SIGUSR2` al hilo del juego; el manejador escribe
`backtrace()` y las direcciones se traducen con `aarch64-linux-gnu-addr2line`
(binario con `-g -no-pie`). Así se ve qué hacía ese tick en particular, que
gprof promedia con todo lo demás.

| Qué | Dónde | En la Pi | Arreglo |
|---|---|---|---|
| Elegir los afijos de un objeto mágico o raro: `type_is` recorría itemtypes.txt con `strcmp` (y recursivo) por cada uno de los ~1500 afijos | `pick_affix` en items.c | un raro 20 a 90 ms, un mágico 7 a 30 | `s_itype_isa` (un bit por cada tipo que es cada tipo, armado como `type_is`) y el índice de los tipos de cada afijo: un raro 1,1 a 1,6 ms, un mágico 0,3 |
| Decodificar un sonido la primera vez que suena | `audio_play` en audio.c | 5 a 40 ms cada uno; el ambiente de una zona nueva, 120 a 315 | se decodifica en el hilo de trabajos, adelante de la cola (`sample_ready`); la voz espera sin sonar y arranca apenas está (casi siempre al tick siguiente); una suelta que tarda más de `SOUND_LATE` ya no suena |
| skills.txt de las habilidades de los monstruos (la catapulta, el daño elemental) | units.c, monai*.inc | ~20 ms | se lee en `units_init` |
| monpet.txt (Consumir del Brujo) | skillwar.inc | 10 ms | en `game_init` |
| rareprefix y raresuffix | items.c | 5 ms | en `items_init` |
| monseq.txt, que se releía con cada secuencia nueva | units.c | | en `units_init`, y queda |
| Los gráficos de los objetos (la caída, el inventario): solo se dibujan | `get_gfx` en items.c | 0,2 a 5 ms cada uno, en el cuadro | en el hilo de trabajos, como los proyectiles |

Los objetos salen iguales: una copia que comparaba la cuenta vieja y la
nueva para cada tipo de objeto, cada afijo y cada código de itemtypes (2,4
millones de casos) no encontró ninguna diferencia.

La misma demo en la Pi, entera:

| | ticks de más de 50 ms antes | ahora |
|---|---|---|
| sin sonido (como se medía) | 50 (el botín 79 ms; el resto, rachas de proyectiles y zonas) | 4, todos de llegar por waypoint o portal (zona entera: 50 a 106 ms) |
| con sonido (`D2_AUDIO_DUMP=/dev/null`, como el juego) | 24 (los ambientes de 116 a 327 ms, sonidos de monstruos de 51 a 73) | 2 (el waypoint al Crystalline Passage 92 ms, la Cámara de Baal 66) |

Lo que quedaba, arreglado el 22/10/2026:

| Qué | Dónde | En la Pi | Arreglo |
|---|---|---|---|
| Los sonidos de las habilidades al arrancar, antes de que existiera el hilo de trabajos | `audio_preload` en `skills_init` | 325 sonidos, ~2 s de la carga | el hilo arranca antes de `game_init` (`main.c`; sin ventana, con `D2_ASYNC`); las precargas van a una cola de baja prioridad (`jobs_push_idle`: detrás de los gráficos, llena no se hace, `jobs_wait` descarta las que no empezaron) y cada sonido se pide una sola vez (`items_init` pedía el de caída unas 600 veces, una por objeto, y llenaba la cola) |
| palshift.dat de cada tipo de monstruo nuevo | `create_monster` en units.c | 0,1 a 4,6 ms, 41 en la demo | `palshift_job`: una entrada por tipo y tabla, en el hilo de trabajos; el monstruo no se dibuja hasta que está (como sus gráficos) |
| Los paneles la primera vez que se dibujan | `get` en ui.c | el de los waypoints 18 ms | en el hilo de trabajos (`get` lo pide adelante y no lo dibuja hasta que está); `preload_ui` (game.c) pide al empezar la partida los del HUD, el inventario, los waypoints, las misiones, la tienda y el árbol de cada jugador, y `menu_preload` los del menú mientras carga; la pantalla de carga se lee en el momento |
| Los íconos de habilidades de una clase | `skill_icons` en game.c | 8 a 11 ms | en el hilo de trabajos (los de los jugadores, en `preload_ui`). Los del Brujo se siguen armando al dibujarlos: usan las tablas de la paleta del acto |

En la Pi (demo del Acto 5, pareja 9, semilla 7, `D2_ASYNC=1`, de a una):

- **Arranque** (`--frames 1` con sonido): de 15,8 a 14,2 s (con la caché fría,
  de 21,1 a 15,7), y ningún sonido decodificado en el hilo del juego (antes
  325).
- **Lecturas del hilo del juego después de empezar**, con sonido y dibujando
  cada tick (`D2_RENDER_EVERY=1`): antes 41 `palshift.dat` dentro del tick
  y 7 paneles o íconos al dibujar; ahora ninguna fuera del armado de zonas
  (las baldosas, los DS1 y `lvlsub.txt` del Acto 5). Sin sonido, lo mismo.
- **Ticks de más de 50 ms**: 3 antes y 3 ahora con sonido, 4 sin sonido;
  todos de armar zonas (llegar por waypoint al Crystalline Passage arma la
  zona entera: 88 a 115 ms, contado en "jugadores" porque lo llama
  `waypoint_tick`).

Lo que queda: `quest_tick` cuesta 2,5 a 3 ms por tick en el Acto 5 (buscar
unidades por nombre entre todas).

## Las cuevas heladas del Acto 5 (26/10/2026)

En la Pi, en la Frozen River / Crystalline Passage con dos jugadores y la
pantalla partida, el dibujo pasaba de ~10 ms afuera a 21-24 ms. Medido con la
demo (`D2_AUTOTEST_DEMO=1 D2_DEMO_ACT=5 D2_AUTOTEST_FROM=16
D2_AUTOTEST_STATE=wp=0x1c0000000 D2_AT_PERF=250 D2_RENDER_EVERY=1 D2_ASYNC=1`,
`--players 2 --autotest act5 --frames 7000`) y el "resto" partido en efectos,
nombres, avisos, mapa, línea y carteles (`D2_AT_PERF` los muestra), más gprof
en la Pi:

- **Los avisos ("A: Hablar con...") eran 7 ms**: `quest_action` buscaba cada
  uno de los 33 objetos de misión por nombre entre todas las unidades (~880:
  29.000 `strcmp` por llamada), para cada jugador y cada mitad de la pantalla.
  `obj_near` (cofres, barriles) y `units_find_npc` también recorrían todas.
  Ahora los tres miran solo las unidades de alrededor (`units_near`, la
  grilla), con los mismos desempates: 7 ms -> 0,05-0,1 ms.
- **Oscurecer lo que se dibuja encima de la oscuridad** era ~20% del tiempo en
  la Pi (`dark_resolve_slow`, `dark_apply_row`, `umark`): un tercio de las filas
  eran los botones e íconos del HUD (`fb_blit_scaled`), que solo pisan píxeles.
  Los dibujos que no leen lo de abajo (`fb_blit_scaled`, `fb_blit_remap`,
  `fb_blit_rotated`) ahora marcan los tramos que pintan (`dark_cover`) en vez de
  oscurecer la fila entera; `dark_resolve_slow` oscurece directo en la fila
  cuando nada del tramo estaba marcado, y `umark` va en línea.

Con la pantalla partida en la cueva: dibujo 21,5-23 -> 14-15,5 ms, "resto"
11,6 -> 4,9 ms; con una sola vista, 13-16 -> 10-12 ms. La imagen, igual byte
por byte (`cmpdark.sh`: cuevas del Acto 5, catacumbas, cloacas y partidas de
bots, más `scripts/cmpshots.sh`).

Lo que queda: el mundo dos veces con la pantalla partida (7-8 ms), los efectos
de los estados sobre los monstruos (Visión interior, Grito de batalla: 1,5-2,5
ms, mezclados sobre la oscuridad), los carteles de la demo (el "APRETÁ START"
y las posiciones: 1,5-2,4 ms, solo en la demo) y el HUD (2 ms).

## Reglas para lo nuevo (Acto 3 en adelante)

1. **Nada se lee ni se decodifica del disco en el hilo del juego mientras
   se juega.** Gráficos, sonidos y tablas nuevas: con `jobs_push` (como
   `gfx_job` de las unidades, `def_sprite_load` de proyectiles y efectos,
   `look_job` del personaje, `sample_ready` de los sonidos, `palshift_job`
   de los colores de los monstruos, `ui_preload` de los paneles) o al usarse por
   primera vez y de a poco (como las baldosas). Las tablas .txt, al empezar. Lo que no está listo se salta un cuadro; no se espera.
   Lo que se puede saltear (precargar sonidos) va con `jobs_push_idle`,
   detrás de todo lo demás, y se pide una sola vez.
   Las banderas entre hilos, con `job_flag_get` / `job_flag_set`.
2. **Soltar también cuesta**: liberar un aspecto entero o una zona son
   miles de `free`. Lo grande se suelta en el hilo de trabajos.
3. **Ningún recorrido por cuadro que dependa del tamaño del mapa o del
   máximo de algo** (todas las unidades, los 512 lugares del piso, todas las
   filas del mapa). El mapa de cada acto es más grande que el anterior: lo
   que en el Acto 1 no se notaba, en el 2 sí. Recorrer solo lo usado o lo
   visible. Para los monstruos cerca de un punto, `units_enemies_near` o
   `units_enemies_around` (la grilla); y lo nuevo que mueva unidades, que
   llame a `grid_moved` (`D2_GRID_CHECK=1` lo verifica).
4. **Nada de dividir por píxel**: tablas precalculadas por llamada, y las
   tablas de luz de a 16 con `darken_span`.
5. **Armar una zona tiene que tardar menos de 100 ms en la Pi**: medirlo con
   `D2_ZONES=1` en la demo del acto nuevo. Si una pieza nueva usa DT1
   grandes, ya se decodifican de a poco; si algo más tarda, partirlo.
6. **Medir el acto nuevo con su demo en la Pi** (`D2_AT_PERF`) contra la
   del Acto 1, con el mismo binario, y revisar las líneas `tick N de X ms`.
7. **La imagen igual**: comparar capturas antes y después de cada
   optimización.
8. **Nada que recorra una tabla entera por cada cosa que se arma** (las
   filas de un .txt, todas las unidades, todos los DT1): en la PC no se
   nota y en la Pi son decenas de ms por zona. Buscar por índice o
   recordar lo que ya se buscó. Lo mismo con lo que pasa al matar (el botín:
   cada afijo preguntaba por itemtypes.txt entero).
9. **Lo que se busca "al lado del jugador" en cada cuadro, con la grilla**
   (`units_near`, `units_enemies_near`): los carteles se dibujan una vez por
   jugador y por mitad de la pantalla. Y lo que se dibuja después de la
   oscuridad sin leer lo de abajo, con `dark_cover` en lo pintado (no
   `dark_resolve` de la fila entera).
