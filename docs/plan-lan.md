# La red local: hasta 8 jugadores en varias Pi (24/10/2026)

Lo que pidió el usuario: "conectar 2 rasberry pi 3b en red y jugar 4
jugadores, 2 split screens uno en cada rasberry"; como tiene una sola Pi,
probarlo con la suya y otra emulada en la PC; y después medir hasta cuántas
Pi y jugadores aguanta una partida.

**Estado (24/10/2026):** F1 a F4 hechas y medidas, sin commit. Falta
probarlo con dos Pi de verdad y con gente jugando (las pruebas fueron con
bots y una sola Pi real). Lo medido y la conclusión, al final.

## Cómo funciona: todos calculan lo mismo (lockstep)

Cada máquina corre la partida entera, con todos los jugadores; por la red
solo van los controles de cada tick. Una máquina crea la partida (el
anfitrión) y las otras se conectan a ella (TCP, puerto 6112, como el
TCP/IP del original): cada una le manda los controles de sus jugadores para
el tick T + 3 y el anfitrión, cuando tiene los de todos, los reparte. Nadie
simula el tick T hasta tener los controles de todos para ese tick. Cada
máquina dibuja solo a sus jugadores (uno, o dos con la pantalla partida).

Descartado: que el anfitrión mande la pantalla como video (medido: 155 KB
por cuadro con zlib, 14 a 32 Mbit/s, y comprimir cuesta 6,7 ms por cuadro en
la PC, 30 a 40 en la Pi 3B, más un segundo dibujo).

**La condición:** con los mismos controles, todas las máquinas tienen que
dar exactamente lo mismo, tick a tick. Medido con un resumen del estado cada
25 ticks (`D2_HASH`):
- dos corridas iguales sin el hilo de carga: idénticas 6000 ticks;
- con el hilo de carga (como se juega): se separan en el tick 150 (los
  monstruos esperan a que se carguen sus dibujos para moverse);
- la Pi real y una Pi emulada en la PC (el mismo ejecutable con
  `qemu-aarch64-static`): idénticas 3000 ticks. La PC (x86) y la Pi no
  (otros redondeos): en una partida van todas Pi, o todas PC.

## Fases

- **F1. Que la partida no dependa del tiempo** (sirve también sin red: las
  pruebas quedan repetibles).
  - Lo que la lógica usa de una animación (cuadros, velocidad, cuadro del
    golpe) sale del COF y de animdata.d2, que son chicos: se leen en el
    momento; los dibujos (DCC) siguen cargándose en el otro hilo y la unidad
    se mueve aunque todavía no se vea. Lo mismo con el aspecto de los
    jugadores al cambiar de equipo.
  - Las zonas se arman en partes por cantidad de piezas, no por
    milisegundos, y sin esperar a los DT1 del otro hilo.
  - Ni el reloj (las zonas de terror rotan con los ticks de la partida), ni
    las opciones de cada máquina ("correr siempre" va en los controles).
  - La prueba: `D2_HASH` con y sin hilo de carga, con el hilo frenado al
    azar, con sonido y sin sonido: todas iguales.
- **F2. Hasta 8 jugadores** (el máximo del original). Cada máquina tiene uno
  o dos locales; los arreglos y lo que nombra al jugador 2 pasan a "los
  locales". La vida, la experiencia y el botín de los monstruos según
  cuántos jugadores hay, como el original (D2MOO).
- **F3. La red.** `src/net.c`: crear la partida, buscarla en la red local
  (o la IP), pasar los personajes, los controles de cada tick, el resumen
  del estado cada segundo (si no coincide, se avisa y se guarda) y el corte.
  En red la pausa no frena el juego y no hay cinemáticas.
- **F4. Pruebas.** Dos juegos en la PC con bots; la Pi real como anfitrión
  y una o más Pi emuladas que se le conectan, con 2 a 8 jugadores: el tick
  de la Pi, la memoria, las zonas armadas y cuánto espera cada uno a los
  otros. De ahí sale hasta cuántas Pi y jugadores se puede.

## Lo medido (24/10/2026)

Un tick dura 40 ms (25 por segundo): en la Pi tienen que entrar la lógica
de todos los jugadores, el dibujo de los de esa máquina y la espera de los
controles de las otras.

**Que no se separen** (el resumen del estado cada 250 ticks, comparado entre
todas las máquinas): iguales en todas las pruebas.

| Prueba | Máquinas | Jugadores | Ticks | Resultado |
|---|---|---|---|---|
| PC, en la misma máquina | 4 (2 + 2 + 2 + 2) | 8 | 4000 | iguales |
| PC, en la misma máquina | 8 (1 cada una) | 8 | 4000 | iguales |
| PC, un cliente que antes jugó otra partida (`D2_NET_PREPLAY`) | 2 | 4 | 6000 | iguales |
| Pi real (anfitrión) + 1 Pi emulada en la PC, por Wi-Fi | 2 (2 + 2) | 4 | 6000 | iguales |
| Pi real (anfitrión) + 3 Pi emuladas en la PC, por Wi-Fi | 4 (2 + 2 + 2 + 2) | 8 | 6000 | iguales |

**La Pi en una partida en red** (la Pi de anfitrión con dos jugadores y la
pantalla partida; las otras emuladas en la PC; Acto 1, todos juntos con los
bots): la lógica incluye la espera de los controles de las otras máquinas.

| Jugadores | Lógica por tick | Dibujo por cuadro (2 pantallas) | Memoria | Zonas armadas |
|---|---|---|---|---|
| 4 en 2 máquinas | 2,1 ms (peor 29) | 7,0 a 9,4 ms (peor 63, al empezar) | 239 MB | 2 |
| 8 en 4 máquinas | 2,8 ms (peor 46, al empezar; después 3,0 ms, peor 10) | 7,6 ms (peor 15,6; una vez 376 al empezar, en el pueblo) | 324 MB | 2 |

**El peor caso en la Pi** (`D2_SPREAD`: cada jugador en otro nivel del acto,
cada uno con su zona armada y sus monstruos; sin red ni dibujo, solo la
lógica de todos; 3000 ticks; el promedio y el peor de cada 500 ticks,
sin los primeros 500, en los que la prueba arma todas las zonas de golpe,
cosa que en una partida no pasa: cada zona se arma en partes cuando alguien
entra):

| Acto | Jugadores | Lógica por tick | Peor | Zonas armadas | Unidades | Memoria |
|---|---|---|---|---|---|---|
| 1 | 2 | 0,5 ms | 1,7 ms | 2 | 220 | 177 MB |
| 1 | 4 | 3,5 ms | 6,5 ms | 4 | 467 | 219 MB |
| 1 | 6 | 5,0 ms | 9,5 ms | 6 | 706 | 256 MB |
| 1 | 8 | 8,9 ms | 15,5 ms | 10 | 1183 | 303 MB |
| 3 | 2 | 0,9 ms | 3,8 ms | 1 | 347 | 199 MB |
| 3 | 4 | 7,1 ms | 12,3 ms | 8 | 1590 | 294 MB |
| 3 | 6 | 9,7 ms | 16,7 ms | 10 | 1713 | 343 MB |
| 3 | 8 | 12,0 ms | 22,6 ms | 11 | 1811 | 375 MB |
| 5 | 2 | 1,0 ms | 2,3 ms | 2 | 379 | 255 MB |
| 5 | 4 | 3,2 ms | 7,7 ms | 5 | 895 | 306 MB |
| 5 | 6 | 4,2 ms | 6,7 ms | 6 | 1009 | 358 MB |
| 5 | 8 | 7,5 ms | 13,4 ms | 9 | 1452 | 418 MB |

**La red:** cada tick va un mensaje de 906 bytes con los controles de los 8
jugadores (112 por jugador): unos 23 KB/s para cada máquina; el anfitrión
manda unos 70 KB/s con 4 máquinas y 160 KB/s con 8. Por Wi-Fi anduvo sin
cortes. Los controles llegan con 3 ticks de demora (120 ms); si una máquina
se atrasa, esperan todas.

## La conclusión: hasta cuántas Pi y jugadores

- **8 jugadores**, el máximo del original (y el del protocolo): en hasta
  8 máquinas, con 1 o 2 jugadores cada una. Por ejemplo 4 Pi con la
  pantalla partida (2 + 2 + 2 + 2), u 8 Pi con un jugador cada una, o
  cualquier mezcla (3 Pi: 2 + 2 + 1...).
- **La Pi 3B los aguanta, incluso en el peor caso.** Con los 8 repartidos
  en 8 niveles distintos (lo más caro: cada uno con su zona y sus
  monstruos), la lógica es 7,5 a 12 ms por tick (peor 22,6 en el Acto 3);
  con el dibujo de dos pantallas (7 a 9 ms, peor 15,6) queda en unos 20 ms
  de 40 y en el peor momento cerca de 38. La memoria llega a 418 MB (Acto 5)
  de unos 695 MB libres que deja Batocera. Jugando juntos, como se juega
  casi siempre, la Pi va sobrada: 3 ms de lógica con 8 jugadores.
- Más de 8 no: es el tope del original (la vida, la experiencia y el botín
  de los monstruos llegan hasta 8 jugadores) y del protocolo. La memoria
  sería lo primero en acabarse.
- Todas las máquinas tienen que ser Pi (o todas PC) con el mismo
  ejecutable: una PC y una Pi no redondean igual y se separan.
- Falta probarlo con dos Pi de verdad (acá fue una real y las otras
  emuladas en la PC) y con gente jugando. Quedó anotado un tirón de 376 ms
  en el dibujo, una sola vez al empezar, en el pueblo (no es de la red).

## Cómo correr las pruebas

- Que dos corridas den lo mismo: `D2_HASH=250` (y `D2_LOCKSTEP=1` sin red)
  en las dos y comparar las líneas `HASH`.
- La red sin ventana: el anfitrión con `D2_NET_HOST=8 D2_NET_PORT=7050
  D2_SEED=7 --headless --players 2 --autotest act1 --frames 6000`; cada
  cliente con `D2_NET_JOIN=<ip> D2_NET_PORT=7050 D2_CLASSES=2,3 --headless
  --players 2 --autotest act1 --frames 6000` (`D2_SAVE_DIR` distinto para
  cada uno). Al final cada uno dice `red: sin separarse` (el `RESULTADO:
  MAL` es porque `--frames` corta la prueba en el paso 6).
- Una Pi emulada en la PC (WSL): el mismo ejecutable de la Pi con
  `qemu-aarch64-static -L /usr/aarch64-linux-gnu ./d2port-arm ...` (sale
  igual que la Pi real; `qemu-user-static` de apt, sacado con `dpkg-deb -x`).
- El rendimiento en la Pi: `D2_AT_PERF=250 D2_RENDER_EVERY=1` en la prueba
  de red; el peor caso con `D2_SPREAD=1 D2_TICK_PERF=500 D2_ACT=N
  --headless --players N --frames 3000` (sin `--autotest`).
