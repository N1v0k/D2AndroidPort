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
  D2_ASYNC=1 D2_AUTOTEST_DEMO=1 D2_AT_PERF=250 D2_RENDER_EVERY=1 D2_NOSOUND=1 \
      D2_SEED=7 D2_SAVE_DIR=/tmp/sv /tmp/d2port --headless --players 2 --autotest act2 --frames 5000
  ```

  - `D2_ASYNC=1`: carga en el hilo de trabajos, como el juego. Sin eso, las
    pruebas sin ventana hacen cada trabajo en el momento (para que den
    siempre igual): sirve para comparar dos binarios, pero lo que se mandó
    al otro hilo se sigue midiendo en el tick. `--autotest` usa siempre el
    hilo (`main.c`) y dos corridas con la misma semilla se separan (el
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

## Reglas para lo nuevo (Acto 3 en adelante)

1. **Nada se lee ni se decodifica del disco en el hilo del juego mientras
   se juega.** Gráficos, sonidos y tablas nuevas: con `jobs_push` (como
   `gfx_job` de las unidades, `def_sprite_load` de proyectiles y efectos,
   `look_job` del personaje) o al usarse por primera vez y de a poco (como
   las baldosas). Lo que no está listo se salta un cuadro; no se espera.
   Las banderas entre hilos, con `job_flag_get` / `job_flag_set`.
2. **Soltar también cuesta**: liberar un aspecto entero o una zona son
   miles de `free`. Lo grande se suelta en el hilo de trabajos.
3. **Ningún recorrido por cuadro que dependa del tamaño del mapa o del
   máximo de algo** (todas las unidades, los 512 lugares del piso, todas las
   filas del mapa). El mapa de cada acto es más grande que el anterior: lo
   que en el Acto 1 no se notaba, en el 2 sí. Recorrer solo lo usado o lo
   visible.
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
   recordar lo que ya se buscó.
