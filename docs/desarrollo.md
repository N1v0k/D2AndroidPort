# Notas de desarrollo

Lo que sigue es el cuaderno del proyecto: cómo está hecha cada parte, de
dónde sale cada dato del original, las pruebas y las herramientas. Para
instalarlo, jugarlo o compilarlo, empezá por el [README](../README.md).

Los planes de cada acto están en [plan-acto2.md](plan-acto2.md) y
[plan-acto3.md](plan-acto3.md), y las mediciones en la Pi en
[rendimiento.md](rendimiento.md).

## Estado

**Listo:**

- Framebuffer de 8 bits con paleta a 800x600, como el original, con las tablas
  de luz de `pal.pl2` para las sombras.
- Lectores de paletas, DC6, DCC, COF, DT1, DS1 y `animdata.d2`.
- El Rogue Encampment real (`towne1.ds1`): pisos, sombras, paredes, techos y
  colisiones de los tiles.
- El Bárbaro (jugador 1) y la Amazona (jugador 2), armados por capas, con las
  animaciones de quieto, caminar y correr del pueblo.
- Menú inicial para elegir 1 o 2 jugadores.
- Con 2 jugadores la cámara es compartida mientras están cerca. Si se alejan,
  la pantalla se parte con una línea perpendicular a la recta que los une, y se
  vuelve a unir cuando se juntan.
- Lógica fija a 25 ticks por segundo como el original, con interpolación al
  dibujar.

- NPCs, animales y objetos del campamento, tomados de los presets del DS1:
  - `monpreset.txt` y `objpreset.txt` traducen los ids del mapa.
  - Los gráficos salen de `monstats2.txt` y `objects.txt`.
  - Los NPC pasean cerca de su lugar, las pícaras de guardia se quedan quietas
    y la fogata, las antorchas y el waypoint están encendidos.
  - No se puede atravesar a los NPC ni a los objetos con colisión; si uno
    corta el paso, el personaje lo rodea.
- Los jugadores arrancan junto a la fogata de las pícaras.
- Orden de dibujo entre paredes y personajes según de qué lado de cada pared
  están (las paredes ocupan los bordes de atrás del tile).
- El Blood Moor y las Cold Plains (Llanuras Heladas), pegados al este del
  campamento en un solo mapa (sin pantallas de carga). Se generan al azar en
  cada partida con las piezas del original (`world.c`):
  - Celdas de 8x8 tiles sobre pasto caminable (`floor.dt1`, estilo 0; el
    estilo 5 es el decorado bloqueado de afuera del mapa).
  - Bordes de bosque `bord1` a `bord8` y la transición `townetrans`. Las dos
    zonas se unen por un pasillo entre los bosques (las piezas con abertura
    `bord4o` y `bord2o`, en una fila al azar).
  - Rellenos de piedras, cercas y pantanos, y unos pocos campamentos de
    Fallen por zona.
  - Sendas de tierra como las del original: salen del puente del
    campamento y de cada pasillo del borde y llevan a la boca de las cuevas
    (la Guarida del Mal, el Pasaje Subterráneo), al waypoint y a los lugares
    de las misiones (Bishibosh, las Piedras Cairn, el Árbol de Inifuss, la
    Torre y el Tomo). Se trazan sobre el pasto libre, serpenteando, y cada
    tile lleva la pieza de `floor.dt1` (estilo 0) que une sus vecinos: recta,
    curva, T, cruce o final, la misma tabla que usan las piezas de Blizzard.
  - En las Cold Plains: el waypoint, el campamento de Bishibosh y el camino al
    cementerio.
- Las Burial Grounds (Cementerio): el cementerio del original (`gravey.ds1`)
  rodeado de bosque, debajo de las Cold Plains.
- Waypoints: el del pueblo y el de las Cold Plains. Pasar por uno apagado lo
  enciende y abre el panel del original (`menu/waygatebackground`) con los
  nueve del Acto I; A viaja al elegido. Cada personaje guarda los suyos.
- Zonas en memoria (para que todo el acto entre en el 1 GB de la Pi):
  - Al empezar solo se decide dónde va cada zona; cada una se arma recién al
    entrar o al acercarse a un pasillo o a la boca de una cueva que lleva a
    ella (así al cruzar ya está lista). Su forma sale siempre igual en la
    misma partida (cada zona tiene su semilla).
  - Como mucho 6 zonas armadas a la vez, sin contar el pueblo (que queda
    siempre). Si hace falta otra, se borra la que hace más tiempo que nadie
    pisa; nunca una con un jugador o una mercenaria adentro o al lado.
  - Una zona vacía durante 10 minutos se borra. Al volver está como nueva:
    vuelven los monstruos (y los jefes), como en una partida nueva del
    original. Las misiones cumplidas siguen cumplidas y los objetos tirados
    en el suelo siguen ahí.
  - El mapa se guarda en pedazos de 8x8 tiles de cada zona. Los gráficos de
    los monstruos y los tilesets cuentan quién los usa; los que quedan sin
    uso se guardan hasta un tope (24 MB cada uno) por si se vuelve y después
    se liberan.
  - Los gráficos y los sonidos de los monstruos de una zona nueva se
    decodifican en otro hilo (`jobs.c`): el juego no se traba al cruzar de
    zona. Los cuadros de las animaciones se recortan a lo que tienen
    dibujado (ocupan la mitad).
- Monstruos de cada zona como en `levels.txt`: en el Blood Moor Fallen,
  zombis, ratas de púas y chamanes; en las Cold Plains además bestias,
  pícaras corruptas con espada y con lanza, y Bishibosh; en el cementerio
  zombis y esqueletos. Los que están lejos de todos los jugadores no hacen
  nada hasta que alguien se acerca, como las salas inactivas del original.
  - Vida, daño y experiencia como en el original: porcentajes de
    `monstats.txt` sobre `monlvl.txt`.
  - Te ven apenas están en tu pantalla (el rectángulo de 800x600 centrado
    en el jugador; antes, a 9 subtiles), te persiguen hasta que salís un
    cuarto más allá del borde y pegan en el cuadro del golpe que marca
    `animdata.d2`.
  - Se resienten con golpes fuertes y dejan cadáver.
- Combate:
  - El ataque apunta solo al monstruo más cercano.
  - Si el golpe saca 1/12 de la vida o más, el personaje se resiente.
  - Al morir se reaparece en el pueblo a los 3 segundos.
  - En el pueblo no se pelea, la vida se recupera sola y no se gasta
    estamina.
- Experiencia y niveles con `experience.txt`. Con 2 jugadores, los dos suman
  la experiencia si están cerca.
- Interfaz:
  - Los orbes de vida y maná se vacían.
  - Nivel y barra de experiencia.
  - Nombre y vida del último monstruo golpeado.
  - Nombre de la zona al entrar.
- Textos en español latino tomados de los `.json` del Resurrected
  ("Páramo Sangriento", "Caído", "Zombi", "Hacha de mano"), con las fuentes
  originales (`data/local/font/latin`, con acentos) y los colores de texto
  de `pal.pl2`.
- Botín del original:
  - `treasureclassex.txt`, resuelto en cadena. Los grupos `weapN`/`armoN`
    eligen al azar entre los objetos de nivel N-2..N según su rareza.
  - Un Fallen no suelta nada el 68% de las veces y oro el 15%.
  - Caen oro, pociones, armas, armaduras, anillos, amuletos, joyas,
    talismanes, gemas y runas, con su animación de caída y el nombre encima.
    Los nombres se acomodan para no pisarse.
  - Salen un 35% menos de pociones que en el original: cada una tiene tres
    tragos.
- Objetos del jugador:
  - El oro se junta solo y las pociones van al cinturón (4 casillas).
  - Las pociones curan como en `misc.txt` (vida o maná con el tiempo,
    rejuvenecimiento al instante, estamina 30 s sin cansarse). Cada una tiene
    3 tragos: el número del cinturón dice cuántos quedan y el líquido del
    dibujo baja un tercio por trago. Cuando se termina, baja de la mochila
    otra igual (si no, una de la misma clase); si en el cinturón no hay de la
    que se pide, se toma de la mochila.
  - El equipo se pone solo si es mejor que el que se tiene; si no, va a la
    mochila de 10x4 casillas. Los objetos de misión (el Mazo, el Bastón de
    los Reyes, el Amuleto de la Víbora) van siempre a la mochila.
  - Como en el original, los objetos de otra clase (garras, orbes, pieles
    de Druida, cascos de Bárbaro para la Amazona, escudos de Paladín,
    cabezas de Nigromante, las jabalinas y arcos de Amazona para el Bárbaro)
    no se pueden poner ("Es de otra clase"): no tienen dibujo para estas
    clases.
  - Los arcos se dibujan en la mano izquierda (así están los dibujos del
    original). Si a una pieza del equipo le falta el dibujo de un modo, se usa
    la misma pieza con otra clase de arma o no se dibuja; antes se tomaba
    cualquier otro objeto de la carpeta y se veía un escudo o un arma
    distinto según lo que hacía el personaje.
  - Equipo inicial del original: hacha de mano + broquel para el Bárbaro,
    jabalina + broquel para la Amazona.
  - Daño del arma más el bonus de fuerza. El % de daño de las habilidades
    (Porrazo, Concentración, Empalar) y de los dominios se suma al de la
    fuerza, como en el original (antes se multiplicaba y pegaban de más).
  - Defensa y puntería con las fórmulas del original: tanto el jugador como
    los monstruos pueden errar el golpe.
  - Bloqueo con escudo, como el original (`UNITS_GetBlockRate` y
    `SUNITDMG_ApplyBlockOrDodge`): (el bloqueo del escudo + el de la clase,
    `BlockFactor` de `charstats.txt` + el del Escudo sagrado) * (destreza -
    15) / (nivel * 2), hasta 75%; corriendo, un tercio. Cada golpe y cada
    proyectil que acierta se puede bloquear; al bloquear suena y se ve la
    animación de bloqueo (`BL`, no más de una cada 15 cuadros). Sin escudo
    (dos armas, el carcaj) no se bloquea. Los monstruos todavía no bloquean:
    las piezas de los monstruos son siempre la primera opción de
    `monstats2.txt` y para los que pueden llevar escudo (Caídos, esqueletos)
    esa es "nil".
  - Si un modo no existe con el arma del monstruo se usa el sin arma
    (`hth`): los cabra (Clan Lunar y los demás clanes) mueren y quedan como
    cadáver con `hth` aunque peleen con `2hs` (antes quedaban quietos al
    morir), y los esqueletos bloquean y hacen el segundo ataque con `hth`.
  - Cada monstruo tiene el tamaño del original (`SizeX` de `monstats2.txt`:
    chico, normal o grande como Andariel o los brutos): no se apilan, no se
    meten encima del jugador, pegan desde más lejos cuanto más grandes y los
    golpes y proyectiles se miden al borde del cuerpo.
  - Cerca del jugador cada monstruo busca un lugar libre a su alrededor (del
    lado de donde viene, probando de a 30 grados): lo rodean en vez de hacer
    fila detrás del primero.
  - Los monstruos que tienen segundo ataque (zombis, Caídos, esqueletos,
    brutos) lo usan con su daño, y el golpe llega en el cuadro del impacto de
    la animación (no al empezarla).
  - Jefes como en el original: los de nombre propio (Corpsefire, Bishibosh,
    Rakanishu, Coldcrow, Treehead, Griswold, la Condesa, el Herrero...) y los
    jefes al azar de los grupos tienen 3 niveles más que la zona (más daño,
    vida y puntería: antes casi no acertaban) y sus modificadores de
    `superuniques.txt` (`monumod.txt`); los de los grupos sacan uno al azar:
    - Muy fuerte: el doble de daño. Muy rápido: camina y ataca más seguido.
    - Encantado de fuego / de frío: suma daño de fuego o frío a cada golpe
      (y a sus flechas) y al morir explota.
    - Encantado de rayo: cuando le pegan suelta chispas (Carga eléctrica).
    - Maldición: el que recibe el golpe queda 5 s recibiendo el doble de
      daño físico. Golpe espectral: daño de un elemento al azar.
    - Quemar maná, Piel de piedra (más defensa), Resistente a la magia (el
      veneno le hace menos) y Disparo múltiple (los arqueros tiran tres).
  - Andariel envenena con el golpe además de su abanico de veneno; Blood
    Raven alterna flechas comunes y de fuego (con daño de fuego) y levanta
    zombis.
- Pantalla de inventario con atributos, equipo y mochila. Con un jugador ocupa
  la mitad derecha y la cámara se corre; con dos, cada uno abre el suyo en su
  mitad.

- La Den of Evil (Guarida del Mal):
  - La entrada es un montículo con la boca de la cueva en el Blood Moor
    (`cavedr1`/`cavedr2`).
  - Adentro hay un laberinto de unas 10 salas de 24x24 (`cave*.ds1`), elegidas
    según sus salidas, con la sala de entrada (`Prev`) y la de Corpsefire
    (`Spec`) en las puntas.
  - Los tiles especiales de las piezas marcan los saltos de ida y vuelta.
  - Está en el mismo mapa, separada por vacío, así que la pantalla partida
    sirve aunque un jugador esté adentro y el otro afuera.
  - Es oscura salvo un radio de luz alrededor de cada jugador.
  - Corpsefire sale de `superuniques.txt`: más vida, daño y experiencia,
    mejor botín y escolta.
  - Al matar a todos aparece "¡Guarida del Mal purificada!" y la cueva se
    ilumina, como en el original: la luz crece en dos segundos y después no
    queda oscuridad (también al volver, si ya se cumplió).
- El equipo se ve en el personaje:
  - El arma en la mano, con su clase de animación (`1hs`, `1ht`, `stf`...).
  - El escudo, el casco y las piezas de la armadura (liviana, media o
    pesada).
- Inventario manejado a mano: con el inventario abierto, la cruceta o el stick
  recorren equipo, mochila y cinturón. A equipa, se saca o toma la poción y X
  la tira al suelo.
- NPCs del campamento: al acercarse aparece arriba de él "A: Hablar con
  ..." (y "A: Usar el traslocador" en los waypoints). Con A se abre su menú
  (Hablar, Comerciar, Salir).
  - Dicen los textos del juego (`vo.json`, los subtítulos de las voces) con
    la voz en español (`data/local/sfx`, por `metadata-audio-vo.json`): la
    presentación (la de la clase si hay), el pedido de cada misión, lo que
    comentan mientras está en curso y al terminarla, y charla suelta. El
    texto sube al ritmo de la voz.
  - Akara cura al hablarle, pide limpiar la Den of Evil y da la recompensa.
  - Kashya pide matar a Blood Raven (Cementerio de las Hermanas).
  - Akara, Charsi y Gheed venden lo que marcan las columnas `AkaraMin/Max`,
    `CharsiMin/Max` y `GheedMin/Max` de las tablas. Se compra al precio de
    `cost` y se vende a un cuarto.
- Objetos mágicos (nombre en azul):
  - Prefijo, sufijo o los dos, sacados de `magicprefix.txt` y
    `magicsuffix.txt` según el nivel del monstruo, el tipo de objeto
    (`itemtypes.txt`) y la frecuencia de cada uno.
  - El nombre en español concuerda con el género del objeto, como en el
    Resurrected: "Botas fornidas de destreza", "Hacha de mano roída del zorro".
  - La calidad de cada objeto que cae se tira como en el original (D2MOO,
    `ITEMS_RollQuality`): único, de conjunto, raro, mágico, superior o
    normal según `itemratio.txt`, la diferencia de niveles, lo que suma la
    clase de tesoro (las de los jefes y campeones dan más) y la búsqueda de
    magia del que lo mató (rinde menos cuanto más hay). Los blancos pueden
    salir con engarces. Las tiendas también venden mágicos.
  - Todas las propiedades son las de `itemstatcost.txt` y se arman con las
    funciones de `properties.txt` (`src/itemstats.c`): resistencias (y su
    máximo), bloqueo, absorción, reducción de daño y de daño mágico, + a
    todas las habilidades, a las de una clase, a una solapa o a una sola,
    velocidad de lanzamiento, de recuperación y de bloqueo, golpe aplastante,
    golpe mortal, heridas abiertas, empujar, frenar, congelar, hacer huir,
    daño y puntería contra no muertos y demonios, ignorar la defensa,
    vida/maná por asesinato, espinas, dominios y penetración de los
    elementos, veneno y frío con su duración, las que suben con el nivel...
  - Los textos de las propiedades son los del juego (`item-modifiers.json`),
    en el orden y con los grupos del original ("+15 a todas las
    resistencias", "Agrega 1-8 de daño de rayo").
- Habilidades (`skills.txt`, `skilldesc.txt`), con los nombres, íconos y
  descripciones del juego:
  - Un punto por nivel (desde el 2) y otro de Akara por la Den of Evil, como
    en el original.
  - Árbol con las tres solapas de cada clase: requisitos de nivel y de
    habilidades previas, y el efecto del nivel actual y el siguiente.
  - Maná, daño, duración y probabilidades con las fórmulas de las tablas
    (`ln12`, `dm12`, `manashift`, tramos de `EMinLev`).
  - El maná se llena solo en `ManaRegen` segundos y sube con el nivel.
  - Todas las del Bárbaro, la Amazona, la Hechicera, el Paladín, el Nigromante, la Asesina, el Druida y el Brujo, con el daño, la duración y las
    sinergias de las fórmulas de `skills.txt` (`calc1`..`calc4`,
    `aurastatcalc`, `passivecalc`, `EDmgSymPerCalc`, `skill('X'.blvl)`,
    `min`/`max`; las que tienen comas vienen entre comillas en la tabla).
  - Bárbaro: Porrazo (empuja), Aturdir, Concentración, Doble golpe y Frenesí
    (con dos armas: la segunda va en la mano izquierda y la animación es la
    doble del original, `1ss`/`1js`/`1jt`/`1st`), Doble lanzamiento, Salto
    (empuja al caer) y Salto ofensivo (no cruzan paredes ni puertas: como
    `SKILLS_FindLeapTargetPosition`, la línea hasta donde cae no choca con
    `COLL_MISSILE_BARRIER` ni `COLL_DOOR`), Torbellino, Vesania (daño mágico y la
    defensa queda en 0), los gritos (Aullido, Provocar, Grito, Órdenes
    bélicas, Orden de batalla, Grito de batalla, Grito de guerra), Buscar
    poción, Buscar objeto, Tótem lúgubre, los dominios, Aumentar estamina,
    Piel de hierro, Mayor velocidad y Resistencia natural.
  - Amazona: Estocada, Empalar (frena), Repeler (un golpe a cada uno de los
    que tiene alrededor), Golpe fulminante, Azote cargado (rayos que
    zigzaguean), Descarga fulminante (rayo en cadena), las jabalinas (Venenosa, Rayo,
    Pestilente, Furia de relámpagos), las flechas (mágica, de fuego, de frío,
    explosiva, de hielo, dirigida, de inmolación con el piso en llamas,
    glacial que congela alrededor), Disparo múltiple, Acribillar, Percepción,
    Proyectiles lentos, Señuelo, Valquiria y las pasivas (Golpe crítico,
    Esquivar, Evitar, Evadir, Penetrar, Perforar).
  - Hechicera (`src/skillsor.inc`, como `SkillSor.cpp` y `MissMode.cpp` de
    D2MOO): las descargas (ígnea, de hielo que enfría, Centella: sus
    chispas salen hacia el blanco y cada una va por el camino al azar del
    original, `charged_step` como `PATH_ComputePathChargedBolt`: tramos de 2
    subtiles hacia allá o a 45 grados, sin acumularse, hasta 77 cuadros),
    Bola de fuego (explota), Resplandor gélido (congela), Púa glacial
    (explota y congela alrededor), Rayo (`lightningbolt`, atraviesa) y Cadena
    de relámpagos (salta de un monstruo al más cercano): los dos, y los
    rayos de los monstruos (pCltDoFunc 8), dejan detrás pedazos de su dibujo
    (`missiles_trail`) y así se ven como el rayo largo del original; Orbe
    gélido (escupe
    descargas en espiral y termina en una nova), las novas (64 proyectiles
    en ronda: a cada monstruo le pega uno solo), Campo de estática (un % de
    la vida que le queda, sin matar), Telequinesis (daño y empuja),
    Teletransportar (hacia donde mira, al lugar libre más lejos), Muro de
    fuego, Llamarada (fuego por donde camina), Meteoro (la marca y a los 2,4
    segundos cae y deja fuego; la piedra y su cola, `meteortail`, aparecen
    los últimos 25 cuadros y bajan 15 píxeles por cuadro en diagonal desde
    arriba a la izquierda: `missiles_meteor`, con los CltParam de su centro;
    también el de los vampiros), Ventisca (cada 4 cuadros un trozo de hielo
    a hasta el radio menos uno, que cae desde 120 píxeles en sus 9 cuadros:
    `blizzard_fall`, como los CltParam de blizzard1; daña y explota al
    tocar el piso),
    Infierno (con la secuencia del original), Hidra (tres cabezas quietas
    que escupen fuego, 10 segundos), las armaduras de hielo (más defensa;
    la glacial congela al que le pega, la de escalofrío lo daña y enfría,
    la gélida le devuelve un proyectil de hielo al que le dispara),
    Escudo de energía (el maná se lleva parte del daño), Tormenta eléctrica
    (un rayo cada 1,6 segundos), Encantar (fuego en los golpes, de ella o
    del compañero más cercano) y las pasivas (Calor, los tres dominios: el
    del frío baja la resistencia de los monstruos, sin romper la inmunidad).
  - Paladín (`src/skillpal.inc`, como `SkillPal.cpp` de D2MOO). Combate:
    Sacrificio (más daño, pero se lastima un 8% de lo que pega; con la
    secuencia del original), Escarmiento (golpe con el escudo que no
    falla, aturde y empuja; su daño es el del escudo más el del Escudo
    sagrado), Fervor (varios golpes seguidos, a los que tenga pegados),
    Carga (corre hasta el monstruo y lo empuja), Venganza (fuego, frío y
    rayo en el golpe), Conversión (el monstruo pelea de su lado un rato y
    los otros monstruos lo atacan; los jefes no; mientras tanto no le tapa
    el paso a su dueño, como las otras mascotas), Descarga sagrada (atraviesa, daña a no muertos y
    demonios y cura al compañero), Martillo bendecido (la espiral del
    original, `PATH_ComputePathBlessedHammer`: sale siempre hacia el mismo
    lado y se abre 4,7 subtiles por vuelta hasta unos 11; más daño a no
    muertos y demonios, y Concentración le suma), Escudo sagrado
    (defensa, bloqueo con la fórmula del original y daño de Escarmiento) y Puño
    de los cielos (el rayo cae a los 10 cuadros y suelta rayos sagrados a
    los de alrededor). Las 20 auras se eligen con su botón y quedan puestas
    (también en el pueblo; la puesta tiene el marco dorado y se guarda en la
    partida): cada 2
    segundos (`perdelay`) hacen lo suyo a los que están a su alcance
    (`aurarangecalc`, en subtiles). Las de grupo le dan sus `aurastat` a los
    jugadores y a sus aliados (la mercenaria, la Valquiria, los convertidos:
    daño, puntería, defensa, velocidad, resistencias, Espinas; Plegaria también
    los cura) (Poder, Concentración y Fanatismo daño, Puntería
    bendecida, Dureza defensa, las resistencias y su máximo, Salvación,
    Entereza velocidad y estamina, Meditación maná, Espinas devuelve el golpe,
    Plegaria y Purificación curan, Purificación acorta el veneno); al
    paladín, además, sus pasivas (el fuego, el frío o el rayo que suman a
    sus golpes Fuego sagrado, Congelación sagrada y Choque sagrado, el daño
    entero de Fanatismo, lo de Santuario contra los no muertos). Fuego
    sagrado, Congelación sagrada (además frena) y Choque sagrado dañan a los monstruos de
    alrededor, Santuario daña y empuja a los no muertos, Convicción les baja
    la defensa y las resistencias (a los inmunes, un quinto) y Redención se
    lleva los cadáveres por vida y maná. Plegaria y Santuario gastan maná en
    cada pulso que hacen algo, y mientras tanto el maná no se recupera. Dos
    paladines con la misma aura no se suman. Cada aura se ve con su
    círculo en el piso y su brillo (las dos capas de `overlay.txt`: la de
    atrás, `PreDraw`, se dibuja sobre el piso antes que los personajes).
  - Nigromante (`src/skillnec.inc`, como `SkillNec.cpp` de D2MOO). Las
    maldiciones van al monstruo más cercano y a los de alrededor
    (`aurarangecalc`) por `auralencalc` cuadros, una a la vez por monstruo
    (la nueva reemplaza a la anterior y se ve su dibujo encima): Amplificar
    daño (el doble de daño físico), Visión reducida (solo ven al que tienen
    al lado), Debilitar (pegan menos), Doncella de hierro (el daño cuerpo a
    cuerpo que hacen se les vuelve), Terror (huyen), Confusión (se atacan
    entre ellos), Robar vida (el que les pega cuerpo a cuerpo se cura),
    Atraer (los de alrededor atacan al maldito), Decrepitud (lentos,
    débiles, con menos resistencia física) y Bajar resistencias (a los
    inmunes, un quinto). Terror, Confusión, Visión reducida y Atraer no
    les hacen nada a los jefes. Huesos y veneno: Dientes (un abanico que
    crece con el nivel), Armadura de hueso (absorbe el daño cuerpo a
    cuerpo hasta gastarse), Daga venenosa (con daga), Lanza de hueso
    (atraviesa), Espíritu de hueso (busca al monstruo), Nova venenosa,
    Explosión de cadáver (del 70 al 120% de la vida del muerto, mitad
    fuego, alrededor del cadáver), Explosión venenosa, Muro de hueso y
    Prisión de hueso (pedazos de hueso que tapan el paso, los monstruos
    los rompen a golpes y duran 24 segundos; la prisión, en los 12 lugares
    del original alrededor del monstruo). Invocaciones: Levantar esqueleto
    y Levantar mago esqueleto (de un cadáver; el mago tira uno de cuatro
    proyectiles al azar: veneno, frío, fuego o rayo), Revivir (el mismo
    monstruo del cadáver, con el doble de vida, 3 minutos; los jefes y los
    únicos no) y los gólems, uno a la vez: de arcilla (frena al que golpea),
    de sangre (roba vida y le pasa una parte al Nigromante), de hierro (de un
    arma o armadura del piso; devuelve daño al que lo golpea) y de fuego
    (pega con fuego y tiene Fuego sagrado). Los esbirros son monstruos de
    `monstats.txt` con lo de la habilidad (`calc1` es el % de vida, las
    `aurastat` y `passivestat`: daño, puntería, defensa, vida y las
    sinergias de los dominios) y Resistir invocaciones; si ya tiene el
    máximo (`petmax`), el más viejo se deshace. Siguen al Nigromante y
    pelean con ganas: van por cualquier monstruo hasta 18 subtiles (que no
    esté a más de 22 del Nigromante), siguen con el mismo hasta matarlo y
    solo los golpes fuertes (un cuarto de su vida) los frenan. Los monstruos
    los atacan (también con proyectiles).
  - Asesina (`src/skillass.inc`, como `SkillAss.cpp` de D2MOO, con las
    habilidades de D2R: Trauma de fuego, Campo de choque, Golpe real).
    Artes marciales de carga (Golpe del tigre, de la cobra y real, Puños
    de fuego, Garras del trueno, Cuchillas de hielo): cada golpe que acierta
    suma una carga (hasta 3, duran 15 segundos y dan +50% de puntería cada
    una; se ven sobre ella). Un golpe final que acierta (Talón, Garra, Cola
    o Vuelo del dragón) libera todas las cargas sobre el monstruo: el tigre
    suma daño a ese golpe, la cobra roba vida (con 2 también maná, con 3 el
    doble), fuego, rayo y hielo suman su daño (con 2, una explosión o una
    nova; con 3, fuego en el piso, centellas o hielo que congela) y el golpe
    real, según las cargas, un meteoro, rayos en ronda o hielo alrededor.
    Las de garra usan la secuencia de Garra del dragón: con dos garras (una
    en cada mano, se dibujan con la animación `ht2`) pegan con las dos.
    Patadas con el daño de las botas (Talón del dragón da lvl/6+1 y la
    última empuja; Cola del dragón explota en fuego; Vuelo del dragón salta
    al lado del monstruo). Trampas, hasta 5: los centinelas disparan sus
    tiros (centellas, estela de fuego, relámpagos, llamas; el de la muerte
    además hace explotar cadáveres) y desaparecen; Trauma de fuego es una
    bomba que explota donde cae, Campo de choque deja rayos en el piso, el
    Centinela de cuchillas va y viene entre ella y el monstruo, Furia de
    cuchillas las tira seguidas y el Escudo de cuchillas corta a los que
    tiene al lado. Sombras: Martillo psíquico (empuja), Rapidez (velocidad
    y velocidad de ataque), Desvanecerse (resistencias), Manto de sombras
    (ciega a los de alrededor, salvo a los jefes, y les baja la defensa),
    Explosión mental (aturde y a veces convierte), Veneno en los golpes,
    Dominio de garras, Bloqueo con armas (con dos garras, como un escudo) y
    el Guerrero y el Maestro de las sombras: un clon oscuro con su equipo
    que pelea cuerpo a cuerpo.
  - Brujo (`src/skillwar.inc`), la clase de "Reign of the Warlock" de D2R.
    No está en D2MOO: cada habilidad sale de `skills.txt` y `missiles.txt`
    de D2R (sus Param, con la descripción de cada columna, y sus fórmulas;
    `ln91`/`dm91` son Param9 y Param10, `ln21`/`ln43` Param11-12 y
    Param13-14, `pa10`..`pa14` los nuevos), de `monpet.txt` (lo de cada
    demonio al vincularlo o sacrificarlo) y de los textos del juego.
    Demonios: el hombre cabra (cuerpo a cuerpo; según el nivel de su
    invocación aturde, se enfurece con un golpe mágico quedándose sin
    defensa, entra en frenesí y parte en arco), el mancillado (bolas de
    fuego que suben un 20% por punto de Sangre hirviente, y el aura
    Resistir el fuego para todos los de alrededor) y el profanador (une
    las almas de los enemigos de alrededor: lo que recibe uno lo reciben
    los demás); entre los tres, hasta 1, 2 o 3 según el Dominio demoníaco,
    que les da velocidad, puntería, daño y velocidad de ataque. Juramento
    de sangre: parte del daño que recibe el Brujo va a su demonio, que
    resiste más y recibe menos daño físico. Marca de la muerte los lleva al
    lado del enemigo, le baja la defensa y cada golpe físico o mágico le
    hace más; Atiborrarse cura a uno con un cadáver y le da un rato
    velocidad de ataque, robo de vida, menos daño físico, defensa y
    regeneración (estas dos, por punto del Juramento); Sangre hirviente los
    hace estallar; Consumir sacrifica uno (también el vinculado) a cambio
    de vida, velocidad y lo de ese demonio (el hombre cabra: daño y golpe
    aplastante; el mancillado: dominio y penetración del fuego; el
    profanador: los de la magia; los demás demonios, su resistencia).
    Vincular demonio se canaliza: mientras se mantiene apretado gasta maná
    por cuadro y cada medio segundo prueba (más fácil cuanto más herido y
    con Marca de la muerte; los jefes al azar desde el nivel 15, los de
    misión y los de nombre propio nunca); el vinculado pelea con lo de la
    habilidad, no le tapa el paso al Brujo (como las otras mascotas: antes
    lo encerraba en un paso angosto) y es muy fuerte desde el nivel 5, muy rápido desde el 10,
    tiene golpe espectral desde el 15 y desde el 20 un aura del Paladín
    para los de alrededor. Maleficios
    del arma (uno a la vez, también con sus proyectiles): Flagelo (daño
    mágico; al golpeado le baja la defensa y la puntería), Extracción (vida
    y maná por muerte; el golpeado hace un tercio menos de daño) y Purga
    (velocidad de ataque; el golpeado puede estallar). Explosión
    sobrenatural daña un rato a los embrujados de alrededor, roba vida y
    maná y les renueva el maleficio; Resguardo psíquico absorbe daño y
    aturde a los que pegan de cerca; Dominio de la levitación da daño,
    puntería y golpe crítico con un arma y baja lo que piden las armas;
    Golpe de eco tira copias del arma; Hojas reflejadas, de cerca, pega
    con el arma y con copias de ella que golpean a los costados (de lejos
    las tira); Hoja de distorsión tira una que explota y lleva al Brujo
    hasta ahí; Tajo pega a todos en un arco con el segundo ataque. Las dos
    con velocidad de ataque de más. Caos: la Descarga y
    la Cadena de miasma (con nubes que dañan por segundo), Anillo de fuego
    (llamas alrededor que empujan y pegan más cuanto más lejos llegan), Ola
    de llamas (un frente de fuego que deja el piso ardiendo), los sigilos
    (letargo: frena y baja la defensa; rencor: los de adentro se enfurecen,
    pegan más y más rápido, y se pelean entre ellos; muerte: mata a los
    heridos y los hace estallar), Hecatombe (fuego alrededor que baja la
    resistencia) y Abismo (atrae a los enemigos y estalla). Las de
    `localdelay` tienen su espera. Sus íconos: el `waskillicon.dc6` de D2R
    es una copia del del Druida (el Brujo solo tiene íconos HD), así que se
    arman al vuelo con lo que se ve de cada habilidad (su proyectil, su
    efecto o su demonio). Empieza con daga y un libro viejo (los grimorios
    son su escudo, y van también con un arma a dos manos, como dice
    `charstats.txt`: se ve solo el arma, porque las animaciones a dos
    manos no tienen la capa del escudo).
  - Druida (`src/skilldru.inc`, como `SkillDruid.cpp` de D2MOO, con las
    tablas de D2R). Formas: Hombre lobo y Hombre oso. Se lo ve como el lobo
    o el oso del juego (`monsters/40` y `monsters/tg`, sin el equipo; en el
    pueblo, quieto o caminando) y suma lo de la forma mientras dura (con
    Licantropía, más vida y más tiempo): el lobo velocidad de ataque,
    puntería, vida y estamina; el oso daño, defensa y vida. La vida se
    mantiene en el mismo porcentaje al cambiar. Usar la misma forma otra vez
    la deshace; al morir vuelve a ser humano. Las de lobo (Furia salvaje,
    Rabia, Furia), las de oso (Vapulear, Onda de choque) y las de las dos
    (Garras de fuego, Hambre) lo transforman solas si hace falta (con el
    maná de la forma); los hechizos lo vuelven humano; las invocaciones y
    la Armadura de ciclón se usan transformado. Furia salvaje y Vapulear
    suman una carga por golpe (hasta lvl/2+3): velocidad y robo de vida, o
    daño, velocidad de ataque y aturdir en cada golpe (se ven sobre él).
    Rabia envenena y contagia a los de alrededor, Hambre roba mucha vida y
    maná con menos daño y Furia da varios golpes seguidos. Invocaciones:
    cuervos (se van después de unos golpes), lobos espirituales (frío),
    lobos terribles, el oso pardo, una liana (la venenosa pelea; la
    carroñera y la solar comen cadáveres y le devuelven vida o maná) y un
    espíritu (Savia de roble: vida; Corazón de lobezno: daño y puntería;
    Espíritu de púas: el que pega recibe daño), que se queda cerca y lo da
    como un aura a los de alrededor (el daño, la puntería y las púas
    también a la mercenaria y los esbirros). Fuego: Tormenta de fuego (tres llamas que dejan fuego),
    Roca fundida (rueda atravesando, empuja y deja fuego), Fisura (grietas
    de fuego), Volcán (escupe rocas) y Armagedón (caen rocas alrededor un
    rato). Viento: Explosión ártica (un chorro de frío), Armadura de ciclón
    (absorbe fuego, frío y rayo), Torbellino (aturde), Tornado y Huracán
    (frío alrededor un rato, con lo que vuela girando).
  - Los esbirros de todas las clases (la Valquiria, los del Nigromante)
    usan los valores de `monstats.txt` tal cual, como en el original
    (`noRatio`): la vida, el daño y la defensa no crecen con
    `monlvl.txt`, crecen con la habilidad.
  - Los monstruos resisten como en el original (`SUNITDMG_ApplyResistancesAndAbsorb`
    de D2MOO): cada parte del golpe (física, fuego, frío, rayo, veneno,
    mágico) baja en el porcentaje de `ResDm`, `ResFi`, `ResCo`, `ResLi`,
    `ResPo` y `ResMa` de `monstats.txt`; con 100 o más es inmune y las
    negativas suben el daño (Andariel recibe 50% más de fuego). Los únicos
    suman lo de `MONSTERUNIQUE_UMod8_Resistant`: +40 a fuego, frío y rayo
    los resistentes a la magia, +75 a su elemento los encantados, +50 de
    física y el doble de defensa la Piel de piedra.
  - Los botones A, B, X e Y son cuatro casilleros configurables: en el árbol,
    X y después el botón donde va. A arranca con el ataque normal y sigue
    levantando objetos y hablando con los NPC. Ataque y Lanzar están en un
    cuadrito al costado del árbol.
- Proyectiles (`missiles.txt`, gráficos de `data/global/missiles`):
  - Arcos y ballestas disparan con el ataque normal y gastan el carcaj
    (flechas o saetas, 250 por carcaj) que va en la mano del escudo.
  - Jabalinas, cuchillos y hachas arrojadizas se lanzan con Lanzar y se
    gastan de la pila; si hay otra igual en la mochila, se saca sola.
  - Los efectos de luz (fuego, hielo, explosiones) se suman al fondo con las
    tablas de mezcla de `pal.pl2`, como en el original.
  - Monstruos: las ratas de púas disparan púas y los chamanes Caídos tiran
    bolas de fuego y reviven a los Caídos muertos.
  - Frío (los monstruos van más lentos), congelamiento y veneno con daño en
    el tiempo, también para los jugadores (el orbe de vida se pone verde).
- Atributos: 5 puntos por nivel para fuerza, destreza, vitalidad y energía,
  con los valores por punto de `charstats.txt`. Las armas y armaduras piden
  fuerza y destreza. Las partidas de antes tienen esos puntos ya puestos en
  vitalidad (como se repartían solos).
- Interfaz con los gráficos del juego (posiciones de los diseños SD del
  Resurrected):
  - Barra de control con las estatuas, los orbes que se vacían, estamina,
    experiencia, cinturón y los botones de habilidad. Con 2 jugadores, cada
    uno tiene su estatua con un orbe partido (vida y maná), su estamina y su
    cinturón.
  - Paneles de inventario, personaje y árbol de habilidades; tienda con sus
    solapas; descripciones de objetos y habilidades en cuadros oscuros.
  - Inventario para joystick: A levanta el objeto, se lleva con el cursor y
    se suelta con X (o A) donde se quiera; si hay otro objeto ahí, se
    intercambian. Y lo tira al suelo y B lo guarda. RB tira al suelo de una
    vez lo que está marcado con el recuadro (sin levantarlo antes), o lo que
    se tiene en la mano. "Ordenar" (o LB) acomoda la mochila.
  - Menú principal con el logo en llamas, selección de personaje y menú de
    pausa (Start) con "Guardar y salir".
  - El menú de pausa: a la izquierda el registro de misiones del original
    (`questbackground`, las solapas de los actos a los que se llegó y los
    íconos `a1q1`..`a2q6` en sus huecos): oscura la pendiente, con su color
    la que se está haciendo y gris la cumplida (la recién cumplida arde una
    vez, como en el juego, y queda anotado en la partida); abajo el nombre,
    cómo va y el objetivo de la elegida. LB y RB recorren las misiones (de
    un acto al otro) y, con dos jugadores, X muestra las del otro. A la
    derecha, las opciones con los pentagramas del menú del juego. El mismo
    registro es la página de misiones del panel (Y), donde la cruceta elige
    la misión y LB / RB el acto.
  - Con 2 jugadores, los carteles (zona, nivel) van del lado de cada uno.
- Seis partidas guardadas (`slot1.sav` ... `slot6.sav`), elegidas en la
  pantalla de selección de personaje del original: cada una con su clase
  (Bárbaro o Amazona), nivel, experiencia, oro, misiones, atributos,
  habilidades, botones y objetos (con su lugar en la mochila). Con 2
  jugadores, cada uno elige la suya. Se guarda cada 30 segundos, al subir de
  nivel, al cumplir la misión y al salir. Las partidas viejas (`ba.sav`,
  `am.sav`) pasan solas a los lugares 1 y 2.
- Sonido y música del juego (`sounds.txt`, `monsounds.txt`,
  `soundenviron.txt`), con un decodificador de FLAC propio (`src/fmt/flac.c`)
  y un mezclador de 32 voces sobre SDL:
  - Música de cada zona (pueblo, campo, cueva) que cambia con un fundido al
    pasar de una a otra, la del menú y el tema de la Den of Evil al limpiarla.
  - Ambiente de cada zona (viento, pájaros, goteras en la cueva).
  - Monstruos: gritos al atacar, golpe del arma, quejidos, muertes, pasos y
    ruidos sueltos, con las demoras de `monsounds.txt`. Los chamanes suenan
    al revivir a los Caídos y los Caídos al levantarse o al huir.
  - Jefes: Andariel, Blood Raven, la Condesa y el Herrero hablan al verte
    (`Init` o `Taunt` de `monsounds.txt`) y cada tanto durante la pelea (con
    su segunda frase); Andariel suena al escupir su abanico de veneno.
  - El héroe habla al encontrar la Guarida, el Cementerio, las Piedras, el
    Árbol, Tristram, la Torre, el Monasterio, la Cárcel y las Catacumbas, al
    rescatar a Caín y al terminar cada misión.
  - Jugadores: el arma al cortar el aire y al pegar (según su `hit class`),
    quejidos, muerte, pasos (tierra afuera, piedra en la cueva), Esquivar y
    los gritos y habilidades de cada clase.
  - Proyectiles: el sonido de la flecha o la jabalina al salir, el de la bola
    de fuego y el Rayo mientras vuelan, el impacto y las explosiones.
  - Objetos: al caer al suelo, al levantarlos y guardarlos, al tomar una
    poción, el oro, comprar y vender; subir de nivel, puntos y misiones.
  - Se oye desde cada jugador: lo que está lejos suena más bajo y del lado de
    la pantalla en que está. Un limitador evita que se sature.
  - Volumen de efectos y de música en el menú de pausa, guardado en
    `opciones.txt` junto a las partidas (también "Correr siempre" y "Ver
    posición", que muestra la zona y la posición en las esquinas).
  - Voces en español de los NPC y frases de cada clase:
    - sin maná, sin lugar, "Aún no puedo usar esto" (al ponerse algo sin los
      requisitos), "No en el poblado" (una habilidad en el pueblo),
      "Imposible" (el cubo no reconoce la receta);
    - al entrar a cada lugar de las misiones y al cumplirlas;
    - las 8 de chat con R3. Cada frase tiene dos variantes que salen al azar.
  - En el Acto 2 se dicen las 14 de `sounds.txt` menos `trigger_field`
    (no se sabe cuándo va). El original dice 7, las que tienen texto en
    `vo.json`. Las otras (la guarida de Radament, ver al Invocador, la tumba
    verdadera o una falsa, el Maggot Lair) D2R no las grabó en español
    latino, salvo algunas del Brujo: sin el archivo, no suenan.
  - La del Invocador al matarlo la dicen solo los que están en el Santuario.
    "He fallado, Diablo liberó a su hermano" se dice después de hablar con
    Tyrael, cuando todos se alejan de él.
- Cementerio de las Hermanas: Blood Raven (`bloodraven` de `monstats.txt`,
  nivel 10) dispara flechas, a veces de fuego, y cada tanto levanta zombis
  que salen del suelo (hasta 6 a la vez). Al morir suena el tema de la misión
  y se puede cobrar con Kashya: como en el original, la próxima mercenaria
  sale gratis.
- Las muertes de jefe con efecto propio del original (`bossfx.inc`, con las
  piezas de `missiles.txt`):
  - Blood Raven (`brdeathcontrol`): unos 15 segundos de rayos que salen de
    alrededor (`brdeathlightningbolt`: no se ven, alumbran al pasar), van
    hacia el cuerpo y caen sobre él (`brdeathlightninghit`; antes caían al
    azar por todo el lugar), con su chasquido, truenos y un leve destello,
    mientras suben el humo y los espíritus (`brdeathsmoke*` y
    `brdeathspirit*`, encadenados: empiezan, siguen y terminan, con los
    sonidos de la niebla).
  - Andariel (`andycontrol0`): unos 14 segundos de columnas de fuego sobre el
    cuerpo, tres tandas de paredes de fuego que salen hacia afuera y piedras
    que caen del techo y se rompen contra el piso. El fuego alumbra la
    oscuridad de las catacumbas.
  - Los demás jefes del Acto 1 mueren con su animación y su sonido, sin nada
    especial, como en el original.
- Mercenarias de Kashya (`hireling.txt`, las filas de la expansión): cada
  jugador puede tener la suya, también con 2 jugadores.
  - "Contratar" en el menú de Kashya muestra cuatro pícaras cerca del nivel
    del jugador, de flecha de fuego o de flecha fría, con su nombre
    (`mercenaries.json`), vida, defensa, daño y precio.
  - Sigue a su dueño (corre si se queda atrás y aparece a su lado si viaja
    por un waypoint o entra a la cueva), le dispara al monstruo más cercano
    sin alejarse y a veces usa su flecha de fuego o de frío (con el daño de
    las habilidades de la Amazona) o Percepción.
  - Los monstruos también la atacan. Se cura sola de a poco (rápido en el
    pueblo), avisa con su voz cuando está por morir y, si muere, Kashya la
    resucita por `15 x nivel² / 2` de oro.
  - Sube de nivel con su dueño, sin pasarlo. Su retrato con la vida va
    arriba, del lado de cada jugador. Se guarda con la partida.
- Vida de las invocaciones (esqueletos, gólems, la Valquiria, las sombras,
  los animales del Druida, los demonios del Brujo): en una fila justo arriba
  de los botones de habilidad de su dueño, un casillero por habilidad con su
  ícono, cuántas hay y una barra con la vida sumada del grupo (los colores
  del retrato de la mercenaria). Los muros de hueso, las trampas y la Hidra
  no van. La prueba de habilidades falla si alguna invocación viva queda
  fuera de la barra.
  - Su equipo, en el panel del original (`panel/npcinv`), que es una página
    más del ciclo de paneles (Y: inventario, habilidades, personaje,
    mercenaria). Pensado para el joystick, sin arrastrar nada: la cruceta
    elige el lugar (arco, casco o armadura; el escudo no lo usa), A muestra
    lo de la mochila que va ahí (en rojo lo que no puede usar por fuerza,
    destreza o nivel, con el motivo) y A se lo pone; lo que tenía vuelve a
    la mochila. X se lo saca.
- Acto 2, entero (`docs/plan-acto2.md`), con su prueba automática
  (`--autotest act2`) y la demostración del menú:
  - Después de Andariel, Warriv ofrece "Viajar al este": los dos jugadores
    viajan juntos a Lut Gholein (`act2/town/lutw.ds1`, con su paleta) y
    Warriv los trae de vuelta ("Viajar al oeste"). El acto se guarda con la
    partida (`inact`) y se arranca en el más avanzado de los dos.
  - El panel de waypoints tiene las solapas de los dos actos (izquierda y
    derecha): un waypoint del otro acto también cambia de acto a los dos.
    Al llegar se activa el del pueblo.
  - Los NPC de Lut Gholein con sus textos y voces en español: Atma,
    Drognan, Elzix, Fara (además cura), Geglash, Greiz, Jerhyn, Lysander,
    Meshif, Warriv, Caín y Kaelan. Jerhyn y Kaelan aparecen en sus
    marcadores del mapa (`JerhynPlaceholder1/2`), como `A2Q4.cpp`: Kaelan
    se queda quieto en la entrada del palacio.
  - Tiendas de Fara, Lysander, Drognan y Elzix (sus columnas de las tablas
    de objetos, hasta nivel 20 como en Normal).
  - Apostar con Gheed y Elzix, como `D2GAME_STORES_FillGamble`: 14 objetos
    (un anillo, un amuleto y los de `gamble.txt` hasta el nivel del
    personaje ±5), que se ven como el objeto común; al comprarlo sale su
    calidad (único, de conjunto o raro con las probabilidades de
    `difficultylevels.txt`; si no, mágico) y a veces la versión excepcional
    o de élite. El precio es el del original (el "gamble cost" fijo de los
    anillos y amuletos).
- El desierto del Acto 2 (`world.c`), armado como el original
  (`DrlgOutPlace.cpp` y `DrlgOutDesr.cpp` de D2MOO):
  - Lut Gholein con la salida al oeste (`lutw.ds1`) o al norte (`lutn.ds1`) y
    el Rocky Waste de ese lado, con su pieza de transición; el Dry Hills, el
    Far Oasis y la Lost City pegados a la anterior por uno de sus ocho lados
    y corridos 48 baldosas (comparten un tramo de 32 con el paso en el
    medio), sin pisarse (si no entra, el lado siguiente; si ninguno, cambia
    la anterior); el Valley of Snakes pegado a la Lost City.
  - Cada zona: la arena (`town/ground.dt1`), el borde de meseta con las
    piezas `edge1..8` y los pasos de dos celdas hacia las vecinas
    (`edge9..12`); en el Dry Hills, el Far Oasis y la Lost City, el gran
    acantilado que las cruza con su escalera (las ocho formas de
    `PlaceCliffs`); la entrada a su calabozo (la Stony Tomb, los Halls of the
    Dead, el Maggot Lair, las Ancient Tunnels, el Claw Viper Temple), el
    waypoint y los rellenos de cada una (oasis, ruinas, huesos, cabezas,
    dunas, el carro...), como `InitAct2OutdoorLevel`.
  - El Canyon of the Magi: el acantilado con las siete tumbas y el waypoint
    en el medio (se llega por el portal rojo del diario del Invocador).
  - Los santuarios y los objetos al azar salen como en todo el juego (ver
    "Objetos que se usan"). Todavía no: el borde secundario de `lvlsub.txt`.
- Los monstruos del desierto con la IA del original (`monai.inc`, de
  `AiThink.cpp`): deciden tirando contra sus `aip` de `monstats.txt`
  (pegar, quedarse quietos, acercarse, rodear al jugador, juntarse con su
  manada) y usan sus habilidades como secuencias de `monseq.txt`: los
  saltadores saltan, las víboras cargan reptando, los escarabajos dan la
  estocada y al recibir un golpe sueltan rayos cargados a los cuatro lados,
  los gusanos escupen veneno, ponen huevos (de los que salen larvas) y se
  entierran, los jinetes de arena cargan su golpe especial, las lanzadoras
  tiran jabalinas y se alejan, las momias mayores echan su aliento y
  reviven a las caídas, los buitres se van volando y bajan al lado del
  jugador, los Blunderbore pegan y a veces dan su Smite
  (`seq_pinheadsmite`), y los murciélagos demonio pican y se alejan.
  - El Invocador (`AITHINK_Fn053`): lanza la Púa glacial, la Nova helada (en
    anillo, con su demora), la Bola de fuego y su muro, con el daño de
    `skills.txt` en el nivel `Sk<n>lvl` de `monstats.txt`. Elige casi
    siempre el elemento que el jugador menos resiste y se aleja si lo tiene
    encima. Atajos: el muro de fuego es otra Bola de fuego (el motor no tiene
    fuego en el piso de los monstruos) y Debilitar no hace nada.
  - Duriel: su aura Congelación sagrada (frena a los de alrededor y cada 2
    segundos les hace el frío de la habilidad), la Embestida y el Smite.
  - Los monstruos se ven con sus colores del original (las tablas de
    `palshift.dat` de su carpeta: los comunes con la de `TransLvl`, los
    jefes con la de `Utrans`) y los que tienen luz en `monstats2.txt`
    (Duriel, el Invocador, los magos esqueleto) iluminan alrededor.
- El daño elemental de los ataques de los monstruos (`El1..El3` de
  `monstats.txt`, para todos los actos): fuego, frío, rayo, veneno (por cuadro
  durante el doble de su duración, como el original), mágico y maná, con
  la resistencia del jugador. El frío, también el de los proyectiles,
  enfría al jugador: hace todo a la mitad de velocidad un rato, que la
  resistencia acorta y la poción de deshielo deja en la mitad.
- Guerreros del desierto de Greiz (`hireling.txt`, Acto 2): con Plegaria,
  Desafío o Puntería bendecida. Pelean cuerpo a cuerpo con lanza y a veces
  usan la Estocada, que pega dos veces siguiendo `seq_act2guardjab` de
  `monseq.txt`, con el daño y la puntería de la Estocada de `skills.txt`.
  Su aura les llega a los jugadores y aliados de alrededor como la de un
  paladín, al nivel de la tabla y sin gastar maná (dos auras iguales no se
  suman: vale la más fuerte). Usan lanzas y armas de asta (no las de la
  Amazona ni las jabalinas), casco y armadura. Contratar uno reemplaza al
  que se tenía, de cualquier acto.
  - El arco suma su daño (con la destreza de ella, como el del jugador) y lo
    elemental que traiga; casco y armadura su defensa; y cuentan las
    propiedades de vida, puntería, fuerza y destreza. Al contratar otra, lo
    que tenía puesto vuelve a la mochila.
- Los calabozos del Acto 2: las Cloacas (3 pisos, Radament en el último),
  las Ancient Tunnels, la Stony Tomb, los Halls of the Dead (el Cubo
  Horádrico en el cofre del nivel 3), el Claw Viper Temple (el altar con el
  Amuleto de la Víbora en el 2), el Maggot Lair (el Bastón de los Reyes en
  el último), el harén y los tres sótanos del palacio y las siete tumbas de
  Tal Rasha, con el generador de laberintos y las salas de cada tipo. El
  segundo piso del templo, la planta baja del palacio y la guarida de
  Duriel son piezas fijas, y el Arcane Sanctuary tiene su generador propio:
  las plataformas que salen del centro, unidas por caminos sobre el vacío,
  como `DRLGMAZE_PlaceArcaneSanctuary` (todas las salas vecinas unidas,
  piezas de 12x12, el piso en blanco que no se camina donde no hay piso) y
  con las almohadillas que unen las islas de las piezas de la variante 2
  (con A; los bots también las usan). Ver `plan-acto2.md`.
  La tumba verdadera y la de Ancient Kaa se eligen al azar en cada partida,
  como `DRLG_InitAct2`.
- Las seis misiones del Acto 2, como `QUESTS/ACT2` de D2MOO, con sus textos
  y voces, compartidas entre los dos jugadores y con su página en el
  registro de misiones:
  - La Guarida de Radament: matarlo, levantar el Libro de Habilidad (+1
    punto) y contárselo a Atma.
  - El Báculo Horádrico: el pergamino a Caín, el bastón, el amuleto y el
    cubo, y armarlo en el cubo.
  - El Sol Mancillado: el sol se oscurece al entrar a la Lost City, Drognan
    lo explica y vuelve al romper el altar.
  - El Santuario Arcano: Drognan manda a ver a Jerhyn, se abre el palacio
    y en el sótano 3 está el portal al Santuario.
  - El Invocador: al matarlo y leer el diario de Horazon se abre el portal
    rojo al Canyon.
  - Las Siete Tumbas: el símbolo de la tumba verdadera queda en la página
    de la misión (el glifo de esa tumba, debajo del texto), y los otros seis
    flotan al lado del Invocador. En la tumba verdadera, el Báculo en el
    Orificio abre el portal a Duriel; después Tyrael abre el portal al
    pueblo, y Jerhyn y Meshif terminan el acto. Meshif ofrece "Navegar al
    este": lleva al Acto 3.
- Para caminar como el original, que busca caminos en ocho direcciones: si
  no se puede avanzar por ninguno de los dos ejes, el jugador y los
  monstruos prueban la diagonal; la búsqueda de camino de los bots y la
  prueba de las zonas también van en diagonal. Sin esto no se subía la
  escalera angosta del Arcane Sanctuary.
- Si falta el `.dcc` de un gráfico se carga el `.dc6` del mismo nombre, y
  entran hasta 4096 unidades (con todo el Acto 2 armado pasaba de 2048).
- Dos detalles más de la colisión del original, que encontró la prueba del
  Acto 2:
  - Una pared sobre el piso en blanco (estilo 30, que no está en los DT1 del
    calabozo) bloquea solo lo suyo, como el original, que arranca cada sala
    en 0 y le suma cada baldosa (`map.c`). Antes la baldosa entera quedaba
    como vacío. **Corregido el 2026-10-23:** el original carga `Blank.dt1`
    en todas las salas (`DRLGROOMTILE_LoadDT1FilesForRoom`), así que el piso
    en blanco sí está y no se camina; ver "Las piezas de lvlprest.txt".
  - La boca de cueva que se pisa pero deja encerrado al que llega por ella
    se usa con A, como todas en el original (`world.c`): de la sala de
    llegada del Maggot Lair se sale por encima de la boca, y pisarla lo
    devolvía al Far Oasis.
- Tyrael: si sale sobre algo bloqueado (la tarima de Tal Rasha) va al lugar
  libre más cercano, como pone el original a cada unidad. Se le habla con A
  aunque no sea el pueblo, y su portal se abre al lado del que le habló
  (`D2GAME_CreatePortalObject` en la posición del jugador); con el portal al
  lado, A lo usa.
- Acto 3, entero (`docs/plan-acto3.md`), con su prueba automática
  (`--autotest act3`) y la demostración (`D2_DEMO_ACT=3`):
  - Meshif navega al muelle de Kurast y vuelve a Lut Gholein. Los NPC del
    muelle con sus textos y voces (Ormus, Alkor, Asheara, Hratli, Natalya,
    Meshif y Caín), sus tiendas, la apuesta con Alkor y los Lobos de Hierro
    de Asheara, que lanzan sus hechizos de fuego, frío o rayo.
  - El mundo como el original (`src/world3.inc`, de `DrlgOutJung.cpp` y
    `DrlgOutPlace.cpp`): la selva de tres niveles armada con piezas de 32x32
    por el camino en zigzag de `DRLG_GenerateJungles` (el Spider Forest
    arriba del muelle, el Great Marsh y el Flayer Jungle colgados de una
    anterior), Kurast apilada arriba de la última selva (la baja, el bazar y
    la alta con sus bordes, puertas y piezas al azar), el Causeway y
    Travincal. Las piezas salen de `lvlprest.txt` y cargan sus DT1 como el
    original (los de `lvltypes.txt` que pide su `Dt1Mask`). El paso entre
    dos selvas (el punto que usan los bots y la carga de zonas; el original
    no lo tiene) va en el borde de las dos piezas: entre dos claros, a 13
    tiles del lado de arriba o de la izquierda, por donde cruza su camino (en
    el medio es selva: `D2_ZONETEST` daba "pasillo" en 5 de 120 semillas);
    entre piezas de río, en el medio.
  - Los calabozos: las cuevas de las arañas, el Swampy Pit y el Flayer
    Dungeon, las cloacas (la ronda de 5x5 con sus cuatro bocas), los seis
    templos y el Durance of Hate, con las salas especiales del original.
  - Las IA de los monstruos del acto (`src/monai3.inc`): fetiches, chamanes,
    babuinos, Thorn Hulks, mosquitos, fuegos fatuos, arañas, ranas, fanáticos
    y sacerdotes, vampiros, el Consejo (con su Hidra), Mefisto y los
    tentáculos del río: la cabeza de cada nivel (en la selva la segunda, en
    las cloacas la tercera) con 4 a 6 tentáculos alrededor en las
    posiciones del original (`D2GAME_SpawnPresetMonster`, `sub_6FC6A8C0`);
    se sumergen y salen, se mueren si muere la cabeza, y se les pega desde
    8 subtiles más (`UNITS_IsInMeleeRange`). Lo de área
    de los monstruos (la Ventisca, el Meteoro, el Muro de fuego, la Nova
    venenosa) daña a los jugadores.
  - Las seis misiones (`src/a3quest.inc`): el Tomo de Lam Esen, la Voluntad
    de Khalim (sus cuatro partes, el cubo y el Orbe cautivante que abre el
    Durance), la Gidbinn, el Pájaro Dorado (hasta la Poción de la vida), el
    Templo Corrupto (el Consejo) y el Guardián (Mefisto, el puente de
    sangre y el portal rojo al Acto 4), con su página en el
    registro de misiones. Como en D2MOO:
    - el altar de la Gidbinn es un señuelo: sale un fetiche jefe
      (`fetish11`, con su escolta de `minion1`/`minion2`) y la Gidbinn la
      suelta él al morir (`ACT3Q3_SpawnGidbinnBoss`);
    - la estatuilla de jade la tiene el primer jefe (al azar o de nombre)
      que aparece en el acto, sin contar voladores ni ese fetiche
      (`QUESTS_SetJadeFigurineBoss`); si se suelta su zona, es el próximo;
    - del Consejo, el primero que muere suelta el Mangual y el siguiente un
      Cubo Horádrico a los que no tienen (`ACT3Q5_Callback08_MonsterKilled`).

- Acto 4, entero (`docs/plan-acto4.md`), con su prueba automática
  (`--autotest act4`), la de las misiones (`D2_A4QTEST=1`) y la
  demostración del menú (la que juega por defecto):
  - El portal rojo de Mefisto lleva a los dos a la Fortaleza del
    Pandemónium (la primera vez, con su cinemática). Tyrael (se presenta y
    resucita al mercenario), Caín, Halbu (armas y armaduras) y Jamella
    (magia, la apuesta y cura); la solapa IV de los waypoints y del registro.
  - El mundo como el original (`src/world4.inc`): la Fortaleza; las tres
    mesetas unidas como `gAct4OutdoorDrlgLink` (el Outer Steppes al este de
    la Fortaleza con la pieza de paso, el Plains of Despair y el City of the
    Damned pegados por un lado al azar), con sus bordes (abiertos en el
    medio del tramo que comparten), sus piezas y sus pozos en las cantidades
    de `DRLGOUTDOORS_InitAct4OutdoorLevel` y la variante de cada una en
    ronda; el River of Flame de `DRLGMAZE_PlaceAct4Lava` (seis salas, la
    subida al sur, la Forja, tres puentes al norte y la lava alrededor), que
    se planea al armar el mundo para pegar el Santuario del Caos (5x5) al
    último puente.
  - Las IA del acto (`src/monai4.inc`) con lo que hacen sus habilidades
    (`SkillMonst.cpp`): el aliento sostenido del Balrog y el rayo rojo de
    Diablo (quietos en el cuadro `InfernoAnim` mientras dura), el nido de las
    madres, el Corpulento que come cadáveres, la Armadura y el Espíritu de
    hueso, las maldiciones de los caballeros sobre el jugador (con su ícono)
    y Diablo con sus 16 elecciones pesadas y sus siete hechizos. La prisión
    de hueso (`DiabPrison`, a quien está lejos) son 12 piezas `boneprison1`
    del bando enemigo que se rompen a golpes y no se terminan solas, como el
    original; no sale una pieza donde ya hay otra (antes salían neutrales,
    no se podían romper y se apilaban de a 12 en cada lanzamiento).
  - Las misiones (`src/a4quest.inc`): Izual y su espíritu (sale donde cayó
    Izual o, si ahí no entra, lo más cerca que se pueda, como
    `ACT4Q1_SpawnIzualGhost`), la Forja (la
    piedra, el Martillo de Hephasto y las gemas), los cinco sellos con sus
    jefes, Diablo, Hadriel y el portal del final con el epílogo. Lo que sale
    en pleno juego (el espíritu, los jefes de los sellos con su escolta,
    Diablo y el portal) es de la zona donde sale, como el portal al pueblo:
    se borra con ella y al rearmarse sale de nuevo, uno de cada uno (antes
    quedaba sin zona: se duplicaba, pasaba a otro acto y el bot buscaba al
    espíritu en el pueblo); el portal vuelve a estar al lado de Tyrael al
    volver al acto o al cargar la partida.

- Acto 5, entero (`docs/plan-acto5.md`), con su prueba automática
  (`--autotest act5`, 62 pasos): el portal del final del Acto 4 lleva a
  Harrogath (`src/world5.inc`) con sus NPC (Larzuk, Malah, Qual-Kehk,
  Nihlathak y Caín), sus voces, sus tiendas, la apuesta, los bárbaros
  mercenarios y las solapas V del registro y de los waypoints (que ahora
  son de 64 bits: el Acto 5 usa los números 30 a 38). Harrogath es pueblo
  (`l->town`: ahí se habla con los NPC).
  - Las afueras (`src/world5.inc`, como `DrlgOutSiege.cpp`): el Bloody
    Foothills son las 15 tiras de 16x48 una al lado de la otra, sin grupos
    al azar (`MonDen` 0: sus monstruos son los de las tiras). El Frigid
    Highlands, el Arreat Plateau y la Frozen Tundra son barricadas de 64x160
    o 160x64 armadas en una grilla de celdas de 8x8: el borde de acantilado
    (y el de barranco en dos lados), la entrada y la salida hacia la vecina,
    las bocas de las cuevas, las barricadas que las cruzan (el borde
    secundario de `lvlsub.txt`: los grupos de sustitución de
    `4x3barricade_rules.ds1`, que ahora lee `ds1_load`), las tres prisiones
    del Frigid Highlands, las piezas especiales (el portal al infierno, el
    waypoint, las ruinas, los lagos) y el piso en lo que queda. La celda
    "vacía" de las reglas es la pieza `None` (0x0): no ocupa la celda.
    `D2_A5GRID=1` imprime la grilla de cada barricada al armarla.
  - La decoración de `lvlsub.txt` (`barr_decorate`) va solo en las celdas
    de relleno, como el original (la sustitución corre en las salas
    `DRLGTYPE_MAZE` de `DRLGOUTPLACE_CreateOutdoorRoomEx`, no en las de
    pieza). Antes caía también sobre el piso liso de las piezas, y un tronco
    tapaba la salida del hueco del portal rojo: la llegada desde Hell1 o
    Hell2 quedaba encerrada (8 de 120 semillas). Cambió la decoración de las
    tres barricadas en todas las semillas (lo demás, igual en `D2_COLLHASH`).
  - Las cuevas de hielo (Crystalline Passage, Glacial Trail y Ancients' Way)
    usan el generador de calabozos del Acto 3 (`build_act3_maze`, tipo
    `A3M_ICE`: salas de 16x16 con la subida, la siguiente, la bajada, el
    tema del Glacial Trail y el waypoint, como `DRLGMAZE_PlaceAct5IceStuff`);
    el Frozen River (River A o B), el Drifter Cavern, el Icy Cellar, la
    cumbre del Arreat y la entrada del templo son de una sola pieza.
  - El templo de Nihlathak (la ronda de 2x2 salas de 40x40), la Fortaleza
    de la Piedra del Mundo (la sala de la subida con tres salidas en el
    medio) y los tres infiernos (tres salas de lava en línea y la Lava X
    del Acto 4 alrededor) también salen de `build_act3_maze`; los Halls of
    Vaught, el Trono y la Cámara son de una sola pieza.
  - Si una sala especial no entra (todas las salas libres tienen ocupado
    ese lado), el original la deja afuera y el nivel queda sin su waypoint
    o su salida; acá `build_act3_maze` arma la grilla de nuevo con los
    números al azar que siguen (hasta 60 veces, como `maze_build`). Pasaba
    en 8 de 120 semillas del Acto 5 (la 29: el Glacial Trail sin waypoint);
    los mapas que ya salían bien no cambian (`D2_COLLHASH`).
  - Las baldosas de puerta (orientación 8 y 9) quedan en la lista de
    objetos del mapa como tipo 3, y `spawn_tile_unit` crea lo que el
    original pone ahí (`DRLGROOMTILE_AddTilePresetUnits`): en las
    barricadas, las torres, puertas y muros, las puertas de las prisiones y
    el portal rojo a su infierno (`a5quest.inc`); en Harrogath, el portón
    (abierto).
  - Los niveles del acto usan `MonLvlEx` de `levels.txt` (el nivel de los
    monstruos de la expansión; `MonLvl` es 0 en la Cámara y Baal salía
    del bando neutral).
  - Las IA del acto (`src/monai5.inc`, de `AiThink.cpp` y `AiBaal.cpp`):
    los generadores (el capataz, la madre pútrida, la madre vil) con sus
    crías en el cuadro de golpe, los diablillos que se teletransportan, las
    súcubos con sus maldiciones, el horror congelado con su aliento, la
    bestia de asedio con su pisotón, los muertos que se levantan
    (`ReanimatedHorde`), Nihlathak con la explosión de cadáver, los tres
    Antiguos (`units_set_ancients`) y Baal: en el Trono, las cinco oleadas
    (Colenzo, Achmel, Bartuc, Ventar y Lister con su escolta) cuando no
    queda nadie cerca; después se va a la Cámara (`units_baal_left`), donde
    pelea con las elecciones pesadas del original (el clon, el
    teletransporte, los tentáculos, la nova y los misiles de frío).
  - Las seis misiones (`src/a5quest.inc`): Shenk y el engarce de Larzuk
    (`items_larzuk_sockets`), los 15 presos de las tres prisiones del
    Frigid Highlands y las runas de Qual-Kehk, Anya congelada (la poción de
    Malah y el Pergamino de Resistencia), Nihlathak en los Halls of Vaught
    y la personalización de Anya (el panel de Larzuk y Anya es el de
    Charsi: `imbue_mode`), el altar de los Antiguos en la cumbre (desde el
    nivel 20) que abre la Fortaleza, y Baal: muerto, Tyrael aparece 5 al
    noroeste y, cuando se termina de hablar con él, abre el portal del
    final 5 al este del que le habló (`ACT5Q6_SpawnTyrael`,
    `ACT5Q6_Callback02_NpcDeactivate`; la cinemática `d2x_out`). Los dos
    van al lugar libre más cerca al que se llega caminando
    (`a5_free_spot`): el original usa `QUESTS_GetFreePosition`, que solo
    mira objetos porque su Cámara es una sala abierta; en la nuestra, con
    pasillos, el portal en el cadáver de Baal + 5 caía en la pared en el
    13 % de los lugares donde puede morir (en el 5 % ni se podía tocar).

- El resto del Acto 1, en el mismo mapa y con el sistema de zonas:
  - Afuera, en fila: Stony Field (con las Piedras Cairn y Rakanishu), Dark
    Wood (el Árbol de Inifuss y Treehead WoodFist), Black Marsh (la Torre
    Olvidada), Tamoe Highland y la puerta del Monasterio con el Claustro
    Exterior.
  - Abajo, separados por vacío: el Pasaje Subterráneo (del Stony Field al
    Dark Wood), Tristram (con Griswold y Caín en la jaula), los cinco
    sótanos de la torre con la Condesa, los Barracones con el Herrero, la
    Cárcel (3 niveles), el Claustro Interior con la Catedral y las
    Catacumbas (4 niveles, Andariel en el último).
  - Los laberintos (cuevas, sótanos, cárcel, catacumbas) se arman con un
    generador común que elige las salas del original según sus salidas.
  - Waypoints del original en el Stony Field, el Dark Wood, el Black Marsh,
    el Claustro Exterior, la Cárcel, el Claustro Interior y las Catacumbas.
  - Monstruos de cada nivel según `levels.txt`, más jefes y campeones.
    Andariel tira su abanico de veneno; los Desterrados, bolas de fuego.
    El daño de los proyectiles va en 1/256 corrido `HitShift` lugares, como
    en el original (el abanico de Andariel no tiene `HitShift`: 1792 son 7
    puntos). Los proyectiles de los monstruos también les pegan a los
    esbirros (la Valquiria, los del Nigromante, sus muros de hueso), y los
    monstruos llegan a pegarles cuerpo a cuerpo a los esbirros grandes (los
    gólems) teniendo en cuenta su tamaño.
    Como el original, en todos los actos:
    - **Jefes al azar** (`MONSTERCHOOSE_GetBossSpawnType`): cada nivel
      tiene de `MonUMin` a `MonUMax` (Normal) contando los de nombre, de su
      lista `umon1..umon10`. Uno de cada cinco es un grupo de **campeones**
      (`sub_6FC6E940`, con el mismo peso cada tipo): campeador, fantasmal
      (translúcido, 80% menos de daño físico, golpe de frío), fanático
      (menos defensa, más rápido), poseído (el doble de vida) o vesánico
      (menos vida, mucho más daño), con el nombre en azul. Los demás son
      **únicos** con vida ×4 y experiencia ×5, y de 3 a 6 de escolta con el
      doble de vida y sus modificadores (`D2GAME_SpawnMinions`).
    - **Las IA del original** de los monstruos del Acto 1 (esqueletos,
      zombis, cuervos, brutos, espectros, pícaras corruptas y arqueras;
      `src/monai1.inc`), también cuando aparecen en el Acto 3.
    - El alcance cuerpo a cuerpo de cada monstruo suma su `MeleeRng` de
      `monstats2.txt` (`UNITS_GetMeleeRange`): Duriel, Mefisto, los
      fanáticos, los lanceros y los tentáculos pegan desde más lejos.
    - A los que tienen `killable` en 0 en `monstats.txt` (Baal en su trono,
      los vigías de las catapultas, `baaltaunt`, los animales) no les llega
      daño (`SUNITDMG_ApplyDamage`) y no son enemigos para los jugadores ni
      para sus mascotas (antes la demostración mataba a Baal en el trono y
      no salía la quinta oleada).
  - El techo de la casa donde hay un jugador no se dibuja, como el original:
    se entra a las casas de Kurast y a los edificios de Lut Gholein y se ve
    al personaje adentro. También parado en una baldosa de adentro sin techo
    (las de algunas paredes en las casas de Kurast) si hay techo de la misma
    casa para los cuatro lados. En el muelle del Acto 3 las chozas (Alkor,
    Asheara) no tienen baldosas de techo: el de paja son paredes sin
    colisión en una capa de arriba; se esconden las unidas a las dos paredes
    de delante (la de su columna y la de su fila) del que está adentro.
  - Con A al lado de un objeto de misión y de una escalera o un portal, va
    primero el objeto si el paso está cerrado o si el objeto está más cerca
    (y el cartel dice lo mismo): la palanca de las cloacas está junto a la
    reja, y el diario del Santuario junto al portal rojo.
- Misiones 3 a 6 del Acto 1, con sus textos y voces:
  - **La Búsqueda de Caín** (Akara): el Pergamino de Inifuss en el árbol,
    Akara lo traduce, las cinco Piedras Cairn se tocan en el orden del
    pergamino y abren el portal rojo a Tristram, donde se libera a Caín.
    Las piedras están apagadas; cada una se prende al tocarla en orden (con
    su sonido) y queda prendida. Con la quinta caen rayos grandes sobre las
    piedras y corren rayos por el piso (`cairnstones`), y a los 2,4 segundos
    se abre el portal.
  - **Herramientas del Oficio** (Charsi): el Mazo de Horadric en la forja de
    los Barracones; al devolverlo, Charsi vuelve raro (amarillo) un objeto.
    El Mazo es un arma: Charsi lo acepta también si lo tenés en la mano, y
    no se puede vender.
  - **La Torre Olvidada**: el tomo del Black Marsh y la Condesa en el quinto
    sótano.
  - **Hermanas de la Masacre** (Caín): matar a Andariel y viajar al este con
    Warriv, que cierra el acto.
  - Con 2 jugadores las misiones arrancan para los dos; los jefes cuentan
    para todos y los objetos de misión, para el que los levanta.
  - Página de misiones en el ciclo de paneles (Y) con el estado de cada una.
- Objetos raros (nombre amarillo), con los nombres de `rareprefix.txt` y
  `raresuffix.txt`: de 3 a 6 afijos.
- Únicos (dorado, `uniqueitems.txt`) y de conjunto (verde, `setitems.txt` y
  `sets.txt`), con su dibujo propio. Las piezas de un conjunto suman lo suyo
  con 2, 3 o más puestas y el conjunto completo da lo de `sets.txt`; el
  cartel lista las piezas (en verde las puestas) y lo que ya dan. Si no hay
  un único de ese objeto, sale raro, como en el original.
- Anillos (dos) y amuleto, en sus lugares del inventario del original.
  Talismanes: dan lo suyo desde la mochila.
- Engarces, gemas, runas y joyas (`gems.txt`): una gema, runa o joya en la
  mano soltada sobre un objeto con engarces libres se engarza; da lo de un
  arma, un casco o armadura, o un escudo. Las runas en el orden justo en un
  objeto de calidad baja, común o superior (también etéreo; no mágico ni
  mejor, como `ITEMS_GetRunesTxtRecordFromItem` del original) con la
  cantidad justa de engarces forman la palabra rúnica de `runes.txt` (por
  ejemplo Acero: Tir + El en una espada). Están las 99 completas: desde el
  parche 2.4 de D2R todas se pueden hacer en todos los modos (ninguna está
  prohibida fuera de ladder).
- Requisitos: también el nivel (el de los únicos, los de conjunto y las
  runas engarzadas) y el "Requisitos -N%" de los objetos.
- Lo de fábrica de los objetos de clase (`automagic.txt`, por su "auto
  prefix"): salen con una propiedad fija aunque sean blancos (no los
  únicos, los de conjunto ni las palabras rúnicas): los arcos y las lanzas
  de la Amazona (+ a sus habilidades), los orbes de la Hechicera (vida o
  maná), los escudos del Paladín (resistencias o puntería y daño), las
  cabezas del Nigromante (veneno) y los grimorios del Brujo (daño mágico o
  de fuego). Elegida por su frecuencia y su nivel; en los raros, solo las
  que lo permiten.
- Cubo Horádrico (`src/cube.c`): sale del cofre del nivel 3 de los Halls of
  the Dead, como en el original (los personajes a los que se lo dio Caín
  antes lo conservan). X sobre el cubo en la mochila lo abre
  en el lugar del equipo (grilla de 3x4); con el cubo abierto, X pasa cosas
  de la mochila al cubo y del cubo a la mochila, y el botón Transmutar
  convierte lo que haya si es una receta de `cubemain.txt` (las de Normal sin
  condiciones: gemas y runas que suben, pociones, engarces, mágicos que se
  vuelven a tirar, anillos y amuletos, artesanía, mejorar a excepcional o
  élite, vaciar engarces, flechas y saetas...).
- Guía del cubo: en el menú de pausa (Start), "Guía del cubo". Las recetas
  por categoría (izquierda y derecha o LB y RB), con los íconos de lo que
  llevan y lo que dan y, abajo, la elegida en palabras; la primera solapa
  explica cómo se usa el cubo con el mando. La solapa "Palabras rúnicas"
  (no son recetas del cubo: se engarzan) las muestra de menor a mayor
  nivel, cada una con sus runas en orden, los engarces, dónde se puede
  hacer y el nivel que pide (el de la runa más alta); abajo, lo que da la
  elegida con los rangos del original ("+25-35% ..."): se arma de verdad
  sobre una base que la acepta, una vez con las tiradas mínimas y otra con
  las máximas (`itemstats_roll_mode`).
- Las escaleras y los portales se usan con A estando al lado (así no se cruza
  sin querer peleando); las bocas de las cuevas se cruzan caminando.
- Rendimiento: los sprites guardan sus filas como tramos opacos y se dibujan
  con `memcpy` (el dibujo bajó de 1,6 a 0,4 ms por cuadro con 2 jugadores).
  Los sonidos decodificados se guardan hasta 24 MB; los que hace rato que no
  suenan se sueltan. Al cambiar de equipo, las animaciones nuevas del
  personaje se cargan en otro hilo (antes trababan el juego unos 200 ms en
  la PC, más de un segundo en la Pi): mientras tanto se lo ve con el equipo de
  antes. Con `D2_DEBUG` (y en la prueba automática), un tick de más de 50 ms
  muestra en qué parte se fue el tiempo.
  - La oscuridad de las cuevas era más de la mitad del tiempo de dibujo
    (perfilado con gprof). Ahora la zona con luz plena no se toca (esa tabla
    de `pal.pl2` es la identidad), el degradé calcula el nivel de luz una
    vez cada 4 píxeles y lo oscuro se pasa por la tabla de a 4 píxeles por
    lectura. Con un solo radio (los jugadores) va por una tabla directa.
  - Con la pantalla partida, cada mitad recorre solo los tiles, monstruos y
    proyectiles de su rectángulo, y lo que cae entero del otro lado no se
    dibuja. Los marcos del HUD se copian de una vez (antes, fila por fila).
  - Los "!" de los NPC solo se buscan en el pueblo y entre los que están en
    pantalla.
  - Como mucho se arma una zona nueva por tick (en la Pi armar una tarda de
    30 a 300 ms): si quedan varias cerca, las otras se arman en los
    siguientes, antes de llegar.
  - El texto, los recortes del HUD (los orbes, la barra del medio), lo
    translúcido (fuegos, capas transparentes, efectos) y las sombras del piso
    se dibujan por sus tramos opacos, sin preguntar píxel por píxel. La
    tabla de relleno de la fuente Exocet se arma una vez por color.
  - En la Pi (aarch64), lo que pasa bytes por una tabla de luz (las
    sombras, lo oscuro con `D2_DARK_DEFER=0`) va de a 16 píxeles con NEON
    (`TBL`/`TBX` sobre la tabla de 256 en cuatro pedazos). Al arrancar se
    compara con la versión común sobre datos de prueba: si no da igual, no se
    usa ("NEON: ..." en el log). Para convertir el cuadro a colores no sirve:
    son 4 búsquedas por píxel y en el Cortex-A53 de la Pi 3 tardaba 13 ms.
  - La conversión del cuadro a 32 o 16 bits lee 8 píxeles de una vez y
    escribe de a 2 o 4. En la Pi se compila con `-fno-tree-slp-vectorize`:
    GCC armaba esos colores con NEON pasando por la pila (3,6 ms en vez de
    2,6).
  - La oscuridad de las cuevas se aplica al convertir el cuadro: cada píxel
    pasa por la tabla de luz de su nivel justo antes de la paleta, sin una
    pasada aparte por la pantalla. Lo que se dibuja después y mira lo que hay
    debajo (lo translúcido, las sombras) primero oscurece esos píxeles; lo
    opaco solo los marca. Cerca de cada luz (hasta el 55% del radio) la luz es
    plena y se convierte de corrido, sin calcular el nivel. Con
    `D2_DARK_DEFER=0`, la de antes (una pasada aparte).
  - El piso y las sombras de cada vista se guardan entre cuadros: al moverse
    la cámara solo se dibujan las franjas que entran, y la capa se copia a la
    pantalla. Si el mapa cambia (se arma o se suelta una zona) se dibuja
    entera. `D2_FLOOR_CACHE=0` la dibuja entera en cada cuadro.
  - En la Pi 3 a 600 MHz (`scripts/bench.sh`, dibujo + conversión, con la
    imagen igual): el campamento pasó de 17,6 a 5,6 ms por cuadro, la
    pantalla partida de 24,6 a 12,0, la Guarida del Mal de 21,9 a 10,6 y las
    Catacumbas 4 de 27,5 a 14,0. Ojo: si `vcgencmd get_throttled` no da 0x0
    a la Pi le falta voltaje (fuente o cable) y el procesador queda en 600
    MHz en vez de 1200.
  - El Acto 2 quedó pesado en la Pi y se arregló así (detalle, números y
    reglas para los próximos actos en [docs/rendimiento.md](rendimiento.md)):
    - Las baldosas de cada DT1 se decodifican la primera vez que se dibujan,
      no al armar la zona (el Rocky Waste pasó de 351 a 72 ms).
    - Los gráficos de proyectiles y efectos se cargan en otro hilo: el
      primer disparo trababa 595 ms.
    - El aspecto viejo del personaje se suelta en otro hilo: cada cambio de
      arma trababa de 36 a 80 ms.
    - Las filas visibles se calculan sin depender del ancho del mapa, y el
      objeto más cercano en el piso no recorre los 512 lugares.
    - Al empezar cada acto se piden por adelantado sus baldosas al sistema.
  - En el menú de pausa: VER RENDIMIENTO (arriba: cuadros por segundo y lo
    que tarda la lógica, el dibujo, la subida del cuadro a la GPU y la
    presentación; también va al log cada 5 segundos) y COLOR 16 BITS (sube
    el cuadro en RGB565: la mitad de bytes, que en la Pi es de lo más caro).
- Prueba automática del Acto 1 (ver más abajo), con bots que juegan solos.
- Los NPC con algo nuevo de las misiones (una recompensa, un pedido, algo
  que hacer con un objeto de misión, el viaje al este) tienen un "!" dorado
  arriba de la cabeza. Kashya pide matar a Blood Raven recién después de la
  Guarida del Mal, como en el original.
- Página Botones (entre las habilidades y el personaje): los cuatro botones
  con lo que hace cada uno; A en uno muestra el ataque, lanzar y las
  habilidades aprendidas que se le pueden poner. En el árbol, X sobre una
  habilidad abre el recuadro "Poner en:" con los cuatro botones.
- Diálogos en el orden del original: la primera vez que se abre un NPC se
  presenta solo; "Hablar" da las recompensas y los pedidos, después los
  comentarios de cada etapa de las misiones (una vez cada uno; los de
  "volviste sin terminar", recién al volver de afuera) y la charla suelta en
  orden. Las frases que hablan de Caín, de Andariel o de las Piedras Cairn se
  desbloquean cuando la historia llega ahí. Lo que ya dijo cada NPC se
  guarda en la partida.
- Jugabilidad:
  - Se camina un 10% y se corre un 20% más rápido que en el original. Las
    piernas al correr van un 25% más lentas que eso (si no, parecía un
    error).
  - La mercenaria empieza a seguirte a 3.5 subtiles y se para a 2.5 (recién
    cuando te paraste); si vas más rápido de lo que ella camina, corre a tu
    paso. Así no alterna correr y caminar a cada rato.
  - Las cuatro casillas del cinturón se toman con la cruceta (cada una tiene
    su flechita), sea la poción que sea.
  - Antídoto (cura el veneno y lo deja a la mitad 30 segundos) y deshielo
    (el frío hace la mitad 30 segundos).
  - Automapa del original: las paredes de lo que ya se recorrió, los
    jugadores, la mercenaria, los waypoints (azul) y las salidas (rojo).
  - Opción "Correr siempre" en la pausa, guardada con el volumen.
  - Botón para la página de personaje en el inventario.
  - Los números de vida y maná van arriba de los orbes, sobre un fondo
    oscuro, por encima de los paneles.
- Pantalla de carga del original (la puerta que se abre) al arrancar y al
  entrar a una partida; en pantalla completa no se ve el puntero del mouse.
- Animaciones y efectos de las habilidades, como en el original:
  - El ataque (y Porrazo, Aturdir, Concentración, Vesania y Golpe
    fulminante) alterna las dos animaciones de ataque (A1 y A2) cuando el
    arma tiene la segunda.
  - Estocada: tres puñaladas seguidas (vuelve el brazo atrás entre una y
    otra). Empalar, más lento.
  - Efectos de overlay.txt (`overlays.c`, todos; los de `1ofN` eligen uno
    de los que siguen), puestos como el original según las tablas:
    - `castoverlay` de skills.txt al lanzarla (Percepción, Proyectiles
      lentos, Grito de guerra) y `sumoverlay` al invocar (el Señuelo).
    - `srvoverlay` sobre el que golpea (como `SKILLS_SrvDo002`): el
      destello de Concentración y el de Salto ofensivo. Porrazo y Golpe
      fulminante no tienen.
    - `ProgOverlay` de missiles.txt sobre el que toca el proyectil: el
      destello del rayo de Rayo y Descarga fulminante, el impacto de Salto
      ofensivo y Grito de guerra.
    - Los estados de states.txt: su `castoverlay` al recibirlo, su
      `overlay1` mientras dura y su `onsound`. Grito, Órdenes bélicas y Orden
      de batalla en cada jugador del grupo; Frenesí, Vesania y Torbellino en
      el Bárbaro; el terror de Aullido y del Tótem lúgubre, Provocar, Grito
      de batalla, Percepción y Proyectiles lentos en los monstruos; las
      estrellas de Aturdir; y el veneno (`poisonhit`).
  - Los monstruos enfriados se ven azules y los envenenados verdes (y el
    jugador envenenado), con las tablas de tonos de `pal.pl2`
    (`HueVariations`) que dice el `colorshift` de esos estados.
  - En las cuevas alumbran los proyectiles (`Light` de missiles.txt: las
    flechas de fuego y de hielo, las jabalinas de rayo, las explosiones) y
    los efectos (`Radius` de overlay.txt), cada uno con su radio.
  - Furia de relámpagos como `MISSMODE_SrvHit20_LightningFury`: al pegar
    tira un rayo a cada monstruo del radio (también al que tocó) hasta
    `calc1`. Rayo vuelve rayo todo el daño del arma (`dl12` de
    `lightningjavelin`). La Jabalina pestilente deja el rastro de veneno a
    la vista y la Flecha glacial suelta el centro y las esquirlas.
  - Aullido, Grito y Grito de batalla sueltan su onda (`BAYellShockWave`).
  - Las habilidades con secuencia del original (`SequenceTbls.cpp` de D2MOO,
    pasadas a `src/sequences.c` por `tools/gen_sequences.py`): para cada
    clase de arma, qué cuadro de qué modo va en cada paso y en cuál cae el
    golpe (Estocada, Empalar, Repeler, Torbellino, Doble golpe, Frenesí,
    Salto, Salto ofensivo y Doble lanzamiento).
  - Acribillar no tiene secuencia: como `SKILLS_SrvDo012_Strafe`, en el
    cuadro del golpe dispara al próximo monstruo y la animación vuelve atrás
    el 50% (`Param6`) mientras le queden flechas (entre `calc3` y `calc1`,
    una por monstruo cercano).
  - La Valquiria se dibuja como en el original: la Amazona con el equipo que
    le da `monequip.txt` según el nivel de la habilidad (lanza, lanza de
    guerra o pica de guerra, armaduras de placas, del caos o sagrada); los
    gráficos de `monsters/vk` son de relleno. El Señuelo es una copia de la
    Amazona con su equipo.
- Demostración: con el menú principal quieto 10 segundos, dos bots crean
  dos personajes de clases distintas (primero el Brujo y el Paladín;
  después Bárbaro y Amazona, Amazona y Hechicera, Hechicera y Paladín,
  Paladín y Nigromante, Nigromante y Asesina, Asesina y Druida, Druida y
  Brujo, Brujo y Bárbaro, una vez cada pareja, y vuelve a empezar) y juegan
  la campaña: los Actos 1 a 5 en Normal, después en Pesadilla y en Infierno,
  siempre con los mismos dos personajes (unas 8 horas de juego). Cada acto
  es su prueba automática y sigue desde donde terminó el anterior (el viaje
  con Warriv, el barco de Meshif, el portal del final del Acto 4); después
  de Baal el personaje pasa a la dificultad siguiente como una partida
  guardada que se carga ahí (en memoria: la demostración no escribe nada),
  con las misiones y los waypoints de cero. Si un acto se traba, sigue con
  el que viene (con lo de ese acto dado por hecho). Al empezar cada acto
  tiran lo de misión que quedó de antes (los Libros de habilidad sin leer,
  la estatuilla de un acto cortado, el Martillo; se quedan con un Cubo: si
  no, se llenaba la mochila y en el Acto 4 de Pesadilla no entraba el
  Martillo de la Forja), suben al nivel de ese acto si no lo tienen (30, 40, 50, 60 y 70; 25 más en
  Pesadilla y 50 en Infierno, hasta el 99) y las habilidades (1, 6, 8, 10 y
  12; 8 y 16 más), con los atributos repartidos como uno nuevo de ese nivel
  (fuerza y destreza hasta lo que piden sus armas y el resto a vitalidad);
  en Pesadilla e Infierno, el equipo de esa dificultad. El
  cartel de arriba dice el acto y la dificultad; al terminar (o al apretar
  Start) el log tiene el informe de cada acto y el de la campaña. Con
  `D2_DEMO_ACT=5` juegan solo el Acto 5, de Shenk a Baal y el portal de
  Tyrael (unos 30 minutos), con nivel 70, todas las habilidades de su clase
  en 12 y los puntos que sobran en vitalidad; compran en Harrogath (Anya y
  Larzuk). Con `D2_DEMO_ACT=4`
  juegan el Acto 4, de Izual a la Forja Infernal, los sellos, Diablo y el
  portal del final (unos 10 minutos), con nivel 60 y las habilidades en 10.
  Con `D2_DEMO_ACT=3` juegan el Acto 3, del muelle de Kurast al
  Consejo y Mefisto (unos 35 minutos), con nivel 50 y las habilidades en 8.
  Con `D2_DEMO_ACT=2` juegan el Acto 2, de Radament a Duriel,
  Tyrael y Meshif (unos 40 minutos), con nivel 40 y las habilidades en 6.
  Con `D2_DEMO_ACT=1` juegan el Acto 1, de la Guarida
  del Mal a Andariel y el viaje con Warriv (unos 30 minutos), con nivel 30
  (lo que pide la última fila del árbol) y todas las habilidades en 1. Las
  van usando una por una:
  cada una de ataque unas 6 veces, cada una de apoyo (gritos, Percepción,
  Señuelo, la Valquiria, el Salto) una vez, y el Tótem lúgubre con los
  cadáveres al terminar de pelear. Cambian de arma según la habilidad (el
  Bárbaro dos hachas de guerra, o dos arrojadizas para Doble lanzamiento; la
  Amazona el arco para las flechas y glaives para las jabalinas y las de
  lanza, de a 4 habilidades por arma; la Hechicera un bastón largo; el
  Paladín espada de guerra y escudo de cometa; el Nigromante varita de hueso
  y escudo de cometa, o una daga para Daga venenosa; la Asesina dos garras;
  el Druida maza y escudo de cometa; el Brujo espada larga y un libro viejo)
  y las de flechas y jabalinas las usan
  de lejos. No se quedan sin maná. Como el acto entero no alcanza para todas,
  la demostración siguiente sigue desde donde quedó. Con
  `D2_DEMO_MODE=nuevo` empiezan en el nivel 1 y suben solos (como se
  describe abajo); con `D2_DEMO_MODE=habilidades` prueban las habilidades
  contra Andariel (`src/skilltest.inc`): aparecen a su lado en el Catacombs
  Level 4 con nivel 99 y todas al máximo, y la matan una vez con cada
  habilidad activa (Andariel con el triple de vida, otra vez en cada ronda).
  El Nigromante pelea con Lanza de hueso entre maldición y maldición; con
  las invocaciones solo las repone y pelean ellas; para las de cadáver
  aparecen Cazadoras oscuras muertas al lado de Andariel, y para el Gólem
  de hierro, espadas en el piso. La Asesina, con dos garras salvajes y
  botas de guerra, junta tres cargas y las libera con Garra del dragón,
  pone trampas hasta tener 5 y entre una mejora y otra pelea con Garra del
  dragón. El Druida pelea transformado con el ataque común en las rondas de
  las formas, y en las de los espíritus y las lianas que comen pelea él
  (los repone solo si se mueren).
  Arriba titila "Apretá START para comenzar"; Start (o cualquier botón)
  vuelve al menú. Esos personajes no se guardan.
  En las esquinas de arriba se ve la zona y la posición de cada uno (J1 a la
  izquierda, J2 a la derecha), para poder decir dónde se ve algo mal; jugando
  se muestra con "VER POSICIÓN" en el menú de pausa (o `D2_SHOW_POS=1`).
  El bot (`demobot.inc`) juega como una persona:
  - Va directo a las misiones (se saltea las presentaciones de los NPC).
  - Reparte los atributos según lo que pide el equipo que quiere usar (si
    no, 2 de fuerza o destreza cada 3 de vitalidad). Los puntos de
    habilidad siguen un plan por clase: Porrazo, el dominio del arma que
    usa, Gritar, Aturdir, Concentración y Grito de batalla para el Bárbaro;
    Estocada, Percepción, Golpe crítico, Golpe fulminante, las jabalinas y
    Esquivar para la Amazona.
  - Pone las habilidades en los botones (B la de pegar, X el grito o
    Percepción, Y Grito de batalla o la jabalina de lejos) y las usa.
  - Pelea con lo que se le cruza y levanta lo que le sirve: oro, pociones y
    armas o armaduras mejores que pueda usar (o que va a poder usar pronto).
    Se las pone y vende lo que no usa al volver al pueblo, donde compra
    pociones y las mejoras de las tiendas que le alcancen.
  - Antes de cada jefe revisa si está listo: nivel 3 para la Guarida del
    Mal, 6 (y 90 de vida) para Blood Raven, 9 (120) para la Condesa, 10 (130)
    para el Herrero y 13 (160) para Andariel. Si no, farmea en la zona más
    conveniente de las que conoce (la de monstruos de más nivel que todavía
    le convenga), por waypoint si queda lejos, hasta 15 minutos por jefe.
  - Con poca vida toma pociones; sin pociones vuelve al pueblo a curarse y
    regresa por el mismo waypoint. Si en el cinturón no le quedan de vida,
    pasa una de la mochila (o la cambia por una de maná); en el pueblo vende
    las de maná que levantó de más (deja una, y dos la Hechicera).
  - El Paladín usa armas de una mano: no deja el escudo (Escarmiento,
    Escudo sagrado, bloquear).
  - Si no puede cambiarse algo porque lo que se saca no entra en la mochila,
    no insiste (prueba de nuevo a los 30 segundos). Si un objeto de misión
    quedó en el piso (la mochila llena al tocar el Mazo o el árbol de
    Inifuss), el primero lo levanta antes de seguir; si no le entra, deja en
    el piso lo más barato de la mochila (y no lo vuelve a levantar). El que acompaña no aprieta A al lado de lo de una misión (un
    objeto de misión en el piso, el soporte del Mazo, el árbol de Inifuss):
    se lo llevaría él y la misión es del primero.
  - El Nigromante pelea de lejos con Dientes o Lanza de hueso, levanta
    esqueletos de los cadáveres que tiene al lado (hasta los que puede
    tener), le pone Amplificar daño a los jefes y a los grupos, hace
    explotar un cadáver si hay varios monstruos juntos, tiene siempre un
    Gólem de arcilla y renueva la Armadura de hueso cuando se gasta. Con
    todas las habilidades, las de cadáver solo las usa si hay uno cerca y
    el Gólem de hierro si hay un arma o armadura en el piso. Las
    invocaciones (y las trampas de la Asesina) solo las usa si le falta
    alguna o se le murió: no vuelve a levantar las que tiene vivas.
  - La Asesina pelea de lejos con Trauma de fuego (o Campo de choque), pone
    trampas (Estela de fuego, centellas, relámpagos) cuando hay varios
    monstruos, le pone el Manto de sombras a los jefes y a los grupos y
    renueva Rapidez cuando se termina.
  - El Brujo pelea de lejos con la Descarga de miasma (la Cadena a los
    grupos, la Ola de llamas si son muchos), pone el Sigilo de letargo,
    tira el Anillo de fuego a los que se le vienen encima y la Hecatombe o
    el Abismo a los grupos y los jefes, y solo invoca los demonios que le
    faltan.
  - El Druida pelea de lejos con Tormenta de fuego (a los jefes, Roca
    fundida cada tanto), tira Fisura o Volcán a los grupos y los jefes,
    renueva la Armadura de ciclón cuando se gasta y solo invoca lo que le
    falta (cuervos, lobos, el oso, el espíritu; la liana carroñera si le
    falta vida y hay cadáveres).
  - En la demostración los personajes reciben daño pero no mueren (la vida
    no baja de 1). Si un paso falla sigue con el que viene (hasta 3 seguidos).
  - Todo lo que decide queda en el log (`demo J1 ...`): niveles, puntos,
    habilidades, objetos, compras, farmeo, cuánta vida perdió con cada jefe y
    cuántas veces casi muere. Al final, un resumen por jugador con los usos
    de cada habilidad.

**Falta:** probarlo en la Pi.

### Sprites HD del Resurrected

Se investigó usarlos y se descartó. En el Resurrected el mundo es 3D:
modelos, texturas PBR y esqueletos, unos 30 GB entre todo. Lo único en 2D
son los `.sprite` de la interfaz (3,3 GB). No entra en la memoria ni en la
GPU de una Pi 3B.

### Notas del formato

- El COF numera las direcciones girando en sentido horario desde el sur. Los
  DCC usan otro orden: `4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15`
  para 16 direcciones y `4, 0, 5, 1, 6, 2, 7, 3` para 8.
- Las 25 marcas de colisión de cada tile DT1 están guardadas con las filas
  invertidas.
- Las baldosas ocultas del DS1 (bit 31 de la celda) no se dibujan pero
  cuentan para la colisión, pisos y paredes: son las barreras invisibles del
  original, como las que rodean el campamento. También el piso "en blanco"
  (estilo 30, secuencia 0 o 1). Las esquinas norte (orientación 3) traen una
  segunda pieza (la 4) que también cuenta.
- **Las piezas de lvlprest.txt** se estampan como
  `DRLGPRESET_AddPresetRoomMapTiles` (desde el 2026-10-23, `stamp_rules` en
  `world3.inc` y `map_set_stamp_rules` en `map.c`, para todo DS1 que esté en
  la tabla):
  - **KillEdge:** sin la última fila y columna del DS1, que de otro modo se
    encima con la pieza vecina.
  - **FillBlanks** (casi todas lo tienen): las celdas sin piso dentro de la
    pieza llevan oculto el piso en blanco de `Blank.dt1` (30.0, que no se
    camina ni deja pasar proyectiles; 30.1 en el Santuario Arcano, que solo
    no se camina). El original carga `Blank.dt1`, `InvisWal.dt1` y
    `Warp.dt1` en todas las salas, así que los pisos 30.x que traen los DS1
    también son ese.
  - El relleno va aparte (`MapChunk.blank`) y se suma a lo que estampe
    encima el borde de la vecina, como en el original, donde las dos salas
    suman sus baldosas.
  - Lo que cambió: en las cloacas del Acto 2 y Ancient Tunnels ya no se
    camina por los escombros y debajo de los arcos detrás de las paredes
    (eran celdas con pared y sin piso: un cuarto de lo que se alcanzaba).
    `D2_ZONETEST` cuenta, por zona, los tiles a los que se llega sin nada
    dibujado o con pared y sin piso; con esto son 0 en los cinco actos.
- La colisión es la del original (D2Collision de D2Common, según
  [D2MOO](https://github.com/ThePhrozenKeep/D2MOO)): **una máscara de 16 bits
  por subtile** (`COLL_*` en `src/map.h`) y cada cosa mira solo los bits que
  le importan:
  - **Los bits:**
    - pared 0x1, no se dispara por encima 0x2, barrera de proyectiles 0x4;
    - salida o camino de pieza prearmada 0x10, fuera del mapa 0x20;
    - objeto 0x400, puerta 0x800, cadáver o portal 0x8000…
  - **Cómo se arma:** cada subtile suma (OR) las 25 marcas de las baldosas
    de piso, pared, la segunda pieza de las esquinas y sombra, dibujadas u
    ocultas. Más las marcas de la celda del DS1 (bits 16, 17 y 28), que
    valen para toda la baldosa: "no se camina" → pared, "pared completa" →
    barrera de proyectiles, enlace → salida. Las paredes de salida y de
    puerta suman además la marca de salida.
  - **Saltos:** las salidas ocultas son solo el salto; las de estilo 8 o más
    no son baldosas.
  - **El vacío:** donde no hay piso es como afuera de las salas del
    original (`COLLIDE_MASK_INVALID`).
- **Las pilas de la mochila** (pedido del usuario; el original no apila
  pociones, las llaves van de a 12 y los pergaminos se guardan en el tomo):
  las pociones de a `POTION_STACK` (10), las llaves de a `KEY_STACK` (20) y
  los Pergaminos de Portal de a `TP_STACK` (20). En una pila de pociones
  `amount` son los tragos de todas (3 por poción; la de arriba es la
  empezada): el guardado no cambia y el precio sale de los tragos; en las
  llaves y los pergaminos son cuántos hay (una pila de pergaminos vale lo
  que todos). Al cinturón y al cubo pasan de a una (`potion_take_one`, la
  empezada primero); dos empezadas no se juntan. `bag_add` junta lo que
  entra entero en una pila (las llaves y los pergaminos llenan la que tenga
  lugar y el resto va aparte). El número se dibuja abajo a la derecha
  (`draw_stack_count`). En la tienda, X sobre los pergaminos compra los que
  faltan para llenar una pila (`shop_fill_count`).
- **No hay Tomo de Portal** (pedido del usuario): `tbk` no es de los que
  `items_supported`, así que no se vende ni cae; las bibliotecas sueltan un
  pergamino o una pila de 5 (las cargas con que salía el tomo). Las
  partidas con un tomo lo cambian al cargar por sus cargas en pergaminos
  (`tomes_to_scrolls`).
- **El portal al pueblo** (`tp_cast`): un Pergamino de Portal (`tsc`) de la
  pila más chica (`tp_find`), con X en el inventario. Abre el portal azul
  (`TownPortal`) al lado y otro en el pueblo, lejos de los NPC y en un lugar
  abierto (`tp_town_spot`), unidos con `world_portal` (`PORTAL_TP_FIELD` y
  `PORTAL_TP_TOWN` de cada jugador). Como el original: uno por jugador (otro
  cierra el anterior), cualquiera lo usa y se cierra cuando el que lo abrió
  vuelve desde el pueblo; no se guarda. Los dibujos vuelven al rearmar una
  zona (`tp_zone_loaded`). Los bots compran pergaminos hasta tener 10 y,
  con poca vida y sin pociones, vuelven al pueblo por su portal en vez de ir
  hasta un waypoint (`demo_retreat`). `D2_TPTEST=1` lo prueba.
- **Los íconos de lo que lo afecta** (pedido del usuario; el original no
  los tiene): `player_effects` junta las mejoras de las habilidades (las
  propias y las de un compañero: Grito, Órdenes de batalla, las armaduras,
  Encantar, las formas...), las auras que lo alcanzan (`aura_give` las anota
  en `au_got_*`), el santuario que dura (con el ícono de la habilidad que
  más se parece), las pociones que duran y los males (veneno, frío,
  Amplificar daño, Congelación sagrada). `draw_effects` los dibuja a 28
  píxeles arriba de la barra del medio (con dos jugadores, arriba de la
  franja de cada uno), con los segundos que quedan y titilando los últimos
  tres; los males con marco rojo.
- **El cofre (stash)** (pedido del usuario): A al lado del cofre del pueblo
  (el objeto `Bank` de objpreset.txt, OperateFn 32) abre el inventario con
  el cofre en el lugar del equipo, como el cubo (así entra igual con dos
  jugadores): pestañas de `STASH_W` x `STASH_H` (10x6), sus botones arriba
  y el oro guardado abajo, con la mochila debajo. Se empieza con una
  pestaña y se gana una por cada acto al que se llega (`act_max`, que en
  las partidas viejas sale de las misiones); se compran hasta
  `STASH_BUY_MAX` (5) más con la pestaña "+" (dos veces A), la primera a
  `STASH_PRICE` (50.000) y cada una al triple (`stash_price`), con el oro
  que lleva y, si no alcanza, el del cofre. X pasa de la mochila a la
  pestaña que se ve y al revés, A levanta y suelta (también sobre otra
  pestaña, con lo de la mano), LB ordena la pestaña si el cursor está ahí,
  y "Guardar oro"/"Sacar oro" mueven todo el oro. Se guarda en la partida
  (`stash` y `stashat`) y al cerrarlo. Los bots no lo abren
  (`bots_playing`): no sabrían cerrarlo.
  - Al lado (a 2,2 subtiles, `OBJ_REACH`) se ve el cartel "A: Tu Arcón
    privado" (el nombre del juego). Hasta el 2026-10-23 en el pueblo no
    salía: `draw_prompts` entraba siempre al cartel de hablar con un NPC,
    aunque no hubiera ninguno al lado.
  - Dónde se abre (2026-10-23, pedido del usuario: "un menú para colocar
    bien los objetos"): con **un jugador**, en su propio panel a la
    izquierda (`stash_side`, `draw_stash_side`), con el inventario entero a
    la derecha (el equipo también), como el original; abajo de la grilla,
    una leyenda de cómo se usa (lo que no entra va en otro renglón). Con
    **dos**, cada uno tiene su mitad de la pantalla y no entran dos paneles:
    va en el panel del inventario, en el lugar del equipo, con la mochila
    abajo. Las casillas, las pestañas y los botones son los mismos en los
    dos casos: cambia su esquina (`stash_px`); el cursor pasa de un panel
    al otro solo, porque se mueve por la posición en pantalla
    (`nav_nearest`). El nombre es el del juego: "Arcón" (`stash` de
    objects.json) y "Oro en Arcón:" (`strGoldInStash`).
  - El fondo de la grilla es casi negro (no se ven los lugares del equipo
    detrás), el oro guardado va en dos renglones y los botones del oro
    miden lo que su texto en el idioma de la pantalla. Si la ayuda de
    arriba de un panel no entra, se le achican los espacios
    (`draw_panel_header_at`). La descripción de un objeto va debajo de él
    si arriba taparía el cartel del panel (`draw_tip`).
  - `D2_STASH_TEST=1` guarda y carga una partida: con una carpeta de
    partidas vieja (sin `D2_SAVE_DIR`) puede dar MAL "se guarda y se carga
    igual" sin que sea un error del juego.
- **Al morir se pierde oro** (`set_player_state`, como
  `PLAYER_ApplyDeathPenalty`): el nivel % (hasta 20) del oro que lleva. El
  original lo cuenta con el del cofre y tira el resto en el cadáver; acá
  (pedido del usuario) no hay cadáver: el equipo y el cofre no se tocan. Se
  avisa al reaparecer.
- **Las cinemáticas** (`src/cinema.c`): el Resurrected trae las clásicas en
  `data/global/video/*.webm` (VP8 1920x1080 con franjas negras: el cuadro
  útil es 1920x864) y el sonido aparte, por idioma, en
  `data/local/video/*.flac` (inglés, 5.1) y
  `locales/audio/esmx/data/local/video/*.flac` (español latino, estéreo). El
  importador (`import_movies`) saca cada una de a pedazos y ffmpeg la pasa a
  MPEG-1 640x288 a 24 cuadros con sonido MP2 estéreo a 44100 Hz en
  `data/global/video/<nombre>.mpg` (`idioma.txt` dice de qué idioma es el
  sonido; si cambia, se vuelven a convertir). Los logos (`new_bliz`,
  `bliznorth`) son 640x480 sin sonido. Pesan 190 MB las siete; las de la
  expansión (`d2x_intro`, `d2x_out`) no se convierten todavía.
  - Las pasa [pl_mpeg](https://github.com/phoboslab/pl_mpeg)
    (`src/third/pl_mpeg.h`): en la PC decodifica unos 1500 cuadros por
    segundo. El cuadro va a la GPU como YUV (`video_movie_frame`) y se
    dibuja en toda la ventana, no en los 800x600 del juego.
  - El sonido va por el mezclador (`audio_movie_*`): mientras pasa, la
    música, los efectos y las voces quedan en pausa. El reloj es el sonido
    que ya salió por la placa (la placa no va exactamente a la par del reloj
    de la máquina y en los siete minutos de la presentación el video se
    despegaba); sin sonido, el de la máquina.
  - Cuándo sale cada una, como el original: el servidor le manda al cliente
    el paquete 0x61 con el número de la película en `QUESTS_ActChange`
    (Warriv: 2, Meshif: 3; Tyrael en el Acto 4: 5) y
    `QUESTS_HellPortalActChange` (el portal rojo del Durance: 4), la primera
    vez (A1COMPLETED, A2COMPLETED, A3COMPLETED). Acá: `andy_q` 3,
    `A2F_SAILED` y `A3F_HELLGATE`; el juego la pide (`game_take_movie`) y la
    pasa `main.c`, nunca los bots de la demo ni las pruebas. Al arrancar, los
    logos y la presentación (cada una se saltea con A, B, Start o Esc).
  - En el menú principal, **Cinemáticas** con el panel de la expansión
    (`cinematicsselectionexp`, siete filas en el orden de
    `cinematicsmodalhd.json`, que es el del número del paquete 0x61):
    las ya vistas (`cinematicas.txt` en la carpeta de partidas) se pueden
    volver a ver. Las dos presentaciones están siempre: `d2x_intro` (el 6)
    no la manda nunca el servidor.
- **El grosor de las paredes** (no está en el original): el original para a
  los personajes en el centro de los subtiles; acá se camina en continuo y
  con solo la cruz se llegaba hasta la esquina de una pared. En las cloacas
  del Acto 3 la base de los bloques y las puntas de sus esquinas se dibujan
  un subtile más allá de lo que bloquean y el personaje se paraba encima del
  dibujo. Ahora:
  - `dt1_volume` mira el dibujo de cada pared, esquina o columna (no puertas
    ni árboles) y marca los subtiles libres pegados por delante (x o y más
    grandes) a los que bloquea que el dibujo tapa en más de la mitad. Solo
    las paredes de verdad, las que frenan el paso y los proyectiles: con las
    lápidas del cementerio los bots no salían del Burial Grounds.
  - Esos subtiles llevan `COLL_VOLUME` (0x4000, nuestro) y el jugador y los
    bots quedan a medio subtile de ellos y de lo que bloquea
    (`map_too_close`). Si ya estaba más cerca (al aparecer, por un empujón)
    se mueve sin acercarse más.
  - Nunca cierra un paso que el original deja (`volume_cuts`): en diagonal
    por la esquina de un subtile con grosor (junto a un tótem del Great
    Marsh los bots no llegaban a la Flayer Jungle), ni delante de una punta
    si sus vecinos no siguen unidos sin pasar por ahí.
  - **Jugador y bots:** caminan por donde no hay pared, objeto ni puerta
    (`COLL_MASK_PLAYER_PATH`).
  - **Monstruos:** lo mismo (`COLL_MASK_MONSTER_PATH`).
    - Los que vuelan (columna `flying` de `monstats.txt`: murciélagos,
      buitres) solo chocan con las barreras de proyectiles y las puertas, así
      que cruzan el agua y lo bajo.
    - El espectro (`wraith1` y los que salen de él) atraviesa casi todo.
    - Los que abren puertas no chocan con ellas.
  - **Proyectiles:** según su `CollideType` de `missiles.txt`, con la tabla
    del original (gCollTable de la 1.10, descrita en la Phrozen Keep):
    - el 3 (casi todos) frena en las barreras de proyectiles: paredes y
      vallas; pasa sobre el agua, las lápidas y lo bajo;
    - el 8 (la Roca fundida, la Ola de llamas) frena también en el terreno
      bloqueado;
    - los que no tienen tipo (los efectos, el meteoro) atraviesan todo.
  - **Objetos** según `objects.txt`, como UNITS_GetCollisionMask:
    - las puertas: puerta, más barrera y vista si las bloquean;
    - los demás: objeto, más barrera si la bloquean;
    - el portal: como cadáver.
  - `map_ray` traza una línea por subtiles como COLLISION_RayTrace
    (Bresenham), para la línea de visión.
  - **Lo que todavía no:** las unidades no se marcan en la grilla (jugador,
    monstruo, esbirro y `NO_PATH`). El choque entre unidades sigue siendo el
    del motor.

  Y lo que ocupa cada uno:
  - El jugador es de tamaño "chico": ocupa su subtile y los cuatro de al
    lado, en cruz, y todos tienen que estar libres. Así queda a un subtile de
    las paredes, las estalagmitas, las piedras y la carpa de Akara (antes era
    un cuadrado de 0,6 subtiles y llegaba a pararse encima del dibujo), y no
    pasa por huecos de menos de tres subtiles. El bot busca caminos con la
    misma cruz.
  - Los monstruos, según `SizeX` de `monstats2.txt`: 1 un subtile, 2 la cruz
    y 3 un cuadrado de 3x3.
  - Si alguien aparece encimado a una pared (al llegar por un salto o al ser
    empujado), puede salir sin meterse más.
  - Las piedras sueltas del campo (`stones.dt1`) y los bordes de las cuevas
    son pisos: se dibujan debajo de los personajes, como en el original, y
    la cruz es lo que los mantiene a distancia.
- Orden de dibujo contra las paredes: cada pieza sólida (paredes, columnas,
  finales y esquinas, rejas, la carpa y el carro de Akara) se compara por lo
  que ocupa de verdad. El personaje va detrás solo si alguna parte sólida que
  se superpone con él en pantalla está delante: no más al norte ni más al
  oeste que él y con más profundidad (x + y). Antes contaban el centro del
  tile o la línea entera de la pared, y las columnas lo tapaban al pasar
  por el costado (parecía atravesarlas). En el hueco de una puerta, el arco
  y los postes lo tapan hasta que la cruza.
  - Lo que no admite duda manda: una parte sólida en su misma columna de
    pantalla que está delante (o detrás) en los dos ejes decide sobre las
    demás reglas. Antes, si dos piezas pedían órdenes opuestos ganaba estar
    delante, y en algunas columnas, postes de puertas y finales de pared el
    personaje (o un monstruo) quedaba dibujado encima de lo que tenía
    delante. `D2_DEPTH_AUDIT=1` revisa todo el mapa (un millón de lugares
    por partida): con tres semillas da 0 errores de ese tipo.
- Para ordenar a los personajes contra una pared que no bloquea (la baranda
  del puente del campamento), se mira si algo los separa de ella: si no hay
  nada, están al pie y van delante.
- Los objetos muestran de cada modo solo los cuadros que dice
  `objects.txt` (`FrameCnt`, `Start` y `CycleAnim`): las Piedras Cairn traen
  apagada, prendiéndose y prendida en los mismos 4 cuadros, y sin esto
  titilaban.
- Los tiles especiales (orientación 10 y 11) son las salidas. Como en el
  original, las visibles de estilo menor a 8 se dibujan: son las escaleras
  de las Catacumbas, la Catedral, la Cárcel, los sótanos de la torre y los
  del palacio. Las ocultas son solo el salto. `warp.dt1` no se carga: es del
  editor, y el original solo usa los DT1 de `lvltypes.txt`. Sus marcas
  invisibles tienen la misma orientación, estilo y secuencia que la
  escalera, y antes a veces le ganaban (en la subida de los sótanos del
  palacio y de la Cárcel quedaba un hueco negro).
- Las paredes y las sombras se dibujan desde el vértice inferior del tile (+80
  px). Los pisos se dibujan desde el vértice superior.
- Las paredes "hacia abajo" (orientación 16 a 19: los arcos que cuelgan
  debajo de las plataformas del Santuario Arcano) están por debajo del piso.
  Se dibujan con el piso y antes que él: los pisos de adelante (las
  escaleras) las tapan, y nunca tapan a un personaje. Antes se ordenaban
  como paredes y tapaban las escaleras. Solo las tienen el Santuario, una
  pared del Acto 4 y el Acto 5; las del Acto 5 cuelgan hasta 976 px, así que
  para ese acto hay que subir `LOWER_WALL_REACH`.

## Hoja de ruta

1. **Caminar por el Rogue Encampment.**
   - Extraer los assets del Resurrected (CASC → `data/global`).
   - Leer paletas, DT1, DS1, COF y DCC.
   - Dibujar el campamento real con el Bárbaro caminando.
   - Compilar para la Pi (aarch64) y lanzarlo desde Ports de Batocera.
2. **Combate en el Blood Moor** (listo). Fallen y zombis, IA básica, ataque
   cuerpo a cuerpo, vida, muerte y experiencia.
3. **Loot.** Objetos sacados de las tablas originales, inventario, equipo,
   cinturón de pociones y oro.
4. **Den of Evil, NPCs (Akara, Charsi) y guardado de partida** (listo).
5. **Objetos mágicos** (listo).
6. **Habilidades** (listo: todas las del Bárbaro, la Amazona, la Hechicera, el Paladín, el Nigromante, la Asesina, el Druida y el Brujo).
7. **Proyectiles, interfaz del original y seis partidas** (listo).
8. **Sonido y música** (listo).
9. **Cold Plains, waypoints, Burial Grounds y Blood Raven, con los diálogos y
   las voces en español** (listo).
10. **Mercenarias de Kashya** (listo) y guerreros del desierto de Greiz (listo).
11. **El resto del Acto 1**: todas las zonas del camino, misiones 3 a 6 y el
    final del acto (listo).
12. **Prueba automática del Acto 1** con 1 y 2 jugadores (lista).
13. **Más clases**: la Hechicera, el Paladín, el Nigromante, la Asesina y
    el Druida (listos): están las siete del juego; y el Brujo de D2R.
14. **Objetos del original** (listo): todas las propiedades, únicos, de
    conjunto, anillos y amuletos, talismanes, gemas, runas, engarces,
    palabras rúnicas y el Cubo Horádrico con su guía.
15. **Acto 2** (`docs/plan-acto2.md`): colisión como el original, Lut
    Gholein con sus NPC, tiendas, apuesta, guerreros del desierto y
    waypoints, el desierto, los calabozos, las seis misiones, el Arcane
    Sanctuary y Duriel, la prueba automática con bots y la demostración del
    menú en el Acto 2 (listos).
16. **Acto 3** (`docs/plan-acto3.md`, listo en la PC).
17. **Paquete público sin archivos de Blizzard** con su importador y el
    juego en español o en inglés (listo para Linux y Batocera; el
    importador de Windows, más adelante).

## Preparar WSL (una sola vez)

```bash
wsl -d Ubuntu -u root -- bash scripts/setup-wsl.sh
```

Instala el compilador, SDL2 y el compilador cruzado para la Pi (aarch64).
El extractor usa [CascLib](https://github.com/ladislav-zezula/CascLib)
compilada en `~/src/CascLib`.

## Compilar para la PC (WSL / Linux)

```bash
cmake -S . -B build
cmake --build build -j
./build/d2port
```

## Compilar para la Raspberry Pi 3B

```bash
cmake -S . -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake --build build-pi -j
bash scripts/package-batocera.sh
```

Después hay que copiar el contenido de `dist/ports` a `\\BATOCERA\share\roms\ports`.
Requiere Batocera de 64 bits v33 o más nueva (glibc 2.34).

En Batocera las partidas se guardan en `/userdata/saves/ports/d2port`, así no
se pierden al actualizar el port. En la PC van a `./save` (o a la carpeta de
`D2_SAVE_DIR`).

Si el juego se cae, deja en el log ("el juego se cayó (señal N)") la lista de
funciones donde estaba, como `d2port(+0x48916)`; con el mismo binario,
`addr2line -f -e d2port 0x48916` dice cuál es.

## Publicar sin archivos de Blizzard

```bash
cmake --build build-pi -j                # el port para la Pi (ver arriba)
bash scripts/build-importador.sh         # el importador para Linux y la Pi
bash scripts/package-release.sh          # arma publicar/
```

`publicar/` (que no va al repositorio) tiene lo que se sube como descarga
en *Releases*, sin ningún archivo del juego (el script lo revisa):

- `diablo2-port/`: el port compilado para Batocera
  (`batocera/ports/`) con `.data/d2port/archivos.txt` en lugar de los
  archivos del juego, el importador para Linux (`importador/linux/`) y
  `README.md` y `THIRD-PARTY-NOTICES.md` (copias de los del repositorio, en inglés). El
  importador de la Pi va adentro del port, con dos entradas en el menú de
  ports ("Importar Diablo II (Español)" e "Import Diablo II (English)") que
  buscan la carpeta del juego en un USB o en `/userdata`.
- `diablo2-port.zip`, para subir.

La skin personal del Brujo (sus `.d2rw`) y las herramientas con que se hizo
no se publican (desde el 2026-10-23, a pedido del usuario): no están en el
repositorio (ignoradas por git) ni en el release (`package-release.sh` arma
sin datos y falla si se cuela un `.d2rw` o algo de `data/`). Sí van en el
paquete local de `package-batocera.sh`, el de la Pi del usuario; el
importador (`archivos.txt`) no los pide, porque no son del juego.

### El importador

`tools/d2port-importar.c` (con [CascLib](https://github.com/ladislav-zezula/CascLib),
estática: no depende de nada instalado) abre el almacenamiento CASC del
Resurrected y copia **solo** los archivos de `archivos.txt`: los que usa el
port (unos 30.000, 1,6 GB de los cerca de 40 GB del juego). La lista la
arma `scripts/package-batocera.sh` con los mismos assets que el paquete
completo.

- Pide el idioma (uno de los trece del Resurrected): copia las voces y el
  sonido de las cinemáticas de `locales/audio/<código>/` (`esmx`, `dede`,
  `jajp`...) y, en inglés, los de `data/local/sfx/`; si el del idioma no
  está instalado (Battle.net solo baja las voces del idioma en que se juega),
  quedan las inglesas. Lo anota en `idioma.txt` (`esMX`, `deDE`...), que el
  juego lee al arrancar; el menú principal (IDIOMA) o `D2_LANG` lo cambian
  sin importar otra vez, salvo las voces.
- Busca solo el juego: en Linux en los prefijos de Wine, Lutris
  (`~/Games/*`), Bottles y Steam (Proton), y en WSL el de Windows; en
  Batocera en `/media` (los USB), `/userdata` y `roms/ports`, en una
  carpeta que diga "Diablo" y tenga `.build.info`. Si no lo encuentra, lo
  pregunta.
- El destino: la carpeta `ports` (o su `.data/d2port`) donde está
  `archivos.txt`, o la carpeta del código (la que tiene `CMakeLists.txt` y
  `tools/archivos-del-juego.txt`): ahí los archivos van a `data/` y el
  idioma a `idioma.txt`, los dos ignorados por git. Si no se le dice, el
  paquete en el que está, o el código si es el `build/d2port-importar`.
- Opciones: `--idioma <código>` (también `es` y `en`), `--juego <carpeta>`, `--destino <carpeta>`,
  `--sin-pausa`. Lo que falte queda en `importar.log`, al lado de la lista.
- **`tools/archivos-del-juego.txt` va en el repositorio** (son solo
  nombres): `package-batocera.sh` la actualiza cada vez que arma el paquete.
  Si el motor empieza a usar archivos nuevos del juego (se agregan en
  `package-batocera.sh`), hay que armar el paquete y subir la lista nueva;
  si no, el que clona el código no los tiene.
- Las carpetas de `data/global/monsters` las elige `tools/act1monsters.py`
  por acto: `levels.txt`, los jefes, lo que ponen los mapas por
  `monpreset.txt` y lo que el motor pone por nombre (su `EXTRA`: si el
  código empieza a crear un monstruo nuevo, va ahí; así faltaban las
  barricadas del Acto 5, que salen de las baldosas). Para ver si falta
  algo: una demo de cada acto con los datos del paquete y `grep "sin
  gráficos"` en el log.

Para compilar el de la Pi hace falta `g++-aarch64-linux-gnu` (lo instala
`scripts/setup-wsl.sh`, que también instala `mingw-w64` para el de Windows,
que todavía no se publica: falta probarlo).

### Los idiomas

Son los trece del Resurrected (`src/lang.c`). Los textos del Resurrected
salen de sus `.json` en el idioma elegido; los del motor están en español y
cada idioma tiene su tabla: `src/lang_en.inc` (la lista de todos, a mano) y
`src/lang_<idioma>.inc`, que arma `tools/lang_tables.py armar` desde
`tools/lang/<idioma>.txt` (una traducción por renglón, con el número del
texto en la lista: `tools/lang_tables.py lista`; lo que falta en una tabla
sale en inglés; la de España solo trae lo que cambia: tú en vez de vos y
los nombres del Resurrected en esES). Un texto nuevo del motor: en
`lang_en.inc`, y después su número en cada `tools/lang/*.txt`.

Las letras: los idiomas latinos usan las fuentes del original
(`data/local/font/latin`; el polaco suma `latin2` para sus letras, cuyas
tablas tienen códigos Unicode). Lo que esas no tienen (ruso, japonés,
coreano, chino, alguna comilla) sale de la `.ttf` del Resurrected del
idioma (`data/hd/ui/fonts`: la de Japón, la del chino tradicional, y
`blizzardglobal` para el resto), con `src/fmt/ttf.c` (contornos `glyf`,
sin hinting) y un caché de letras en `font.c`, del tamaño de las del
original. Los renglones se cortan con `font_break()`: en un espacio o
entre dos letras de China, Japón o Corea. `tr()`
traduce los que se arman con `snprintf` y los mensajes; lo que se dibuja
pasa por `lang_screen()`, así los textos fijos de las tablas (menús,
botones) salen traducidos sin tocar cada uso. Los nombres de los objetos
mágicos, raros y de los campeones se arman con los formatos del juego
(`MagicFormat`, `RareFormat`, `ChampionFormat`): cada idioma con su orden.

- `python3 tools/check_lang.py`: revisa que cada traducción al inglés tenga
  los mismos `%d`/`%s` que el original y que todo `tr("...")` esté en la
  tabla; `tools/lang_tables.py armar` revisa lo mismo en los otros idiomas.
- `D2_LANG=en D2_LANG_DUMP=archivo`: anota lo que se dibujó sin traducción
  (para encontrar textos que faltan).

## Herramientas

- `build/d2rextract`: lista o extrae archivos del CASC del Resurrected.

  ```bash
  ./build/d2rextract --list "/mnt/c/Program Files (x86)/Diablo II Resurrected" lista.txt
  ./build/d2rextract "/mnt/c/Program Files (x86)/Diablo II Resurrected" . \
      data/global/chars/ data/global/tiles/ data/global/monsters/ data/global/sfx/ data/global/ui/ \
      data/global/objects/ data/global/excel/ data/global/missiles/ data/global/overlays/ \
      data/global/palette/ data/global/items/ data/global/animdata.d2 data/local/lng/ data/local/font/ \
      data/global/music/act1/ data/global/music/act2/ data/global/music/act3/ data/global/music/common/
  ```

  La carpeta destino es `.`: los archivos ya traen `data/` en el nombre. De
  la música hacen falta la de los actos que hay y la del menú (cada tema pesa
  entre 1 y 40 MB). La instalación de Battle.net de esta PC está en
  `/mnt/d/Battle.net/Diablo II Resurrected`.

  Las voces en español latino (hay que tener ese idioma de voz instalado en
  Battle.net) vienen con otra ruta y van a `data/local/sfx`:

  ```bash
  ./build/d2rextract "/mnt/c/Program Files (x86)/Diablo II Resurrected" . \
      locales/audio/esmx/data/local/sfx/act1/ locales/audio/esmx/data/local/sfx/act2/ \
      locales/audio/esmx/data/local/sfx/act3/ locales/audio/esmx/data/local/sfx/common/
  mkdir -p data/local/sfx && cp -r locales/audio/esmx/data/local/sfx/. data/local/sfx/ && rm -rf locales
  ```

- `build/d2fmt`: decodifica DCC, DC6, DT1, DS1, COF y FLAC, y vuelca los gráficos a BMP
  para revisarlos (`d2fmt flac archivo.flac salida.raw` deja el audio en 16 bits para
  compararlo con el MD5 que trae el archivo).
- `tools/soundfiles.py`: lista los archivos de sonido y música que usa el motor (por
  monstruo, clase, proyectil y zona); el empaquetado para la Pi copia solo esos.
- `tools/dc6compose.py`: arma en un BMP los fondos y paneles que vienen partidos en
  pedazos de 256x256 (`python tools/dc6compose.py archivo.dc6 pal.dat columnas salida.bmp`).
- Dibujos editados: si al lado de un DCC hay un `.d2rw` (los cuadros en
  crudo, el formato de `d2fmt raw`), el motor lo carga en lugar del original
  (`charanim.c`, `load_sprite`). Ni los dibujos ni las herramientas para
  hacerlos se publican: ver más arriba.

| Opción | Qué hace |
|---|---|
| `--fullscreen` | Pantalla completa |
| `--scale N` | Ventana N veces más grande |
| `--players 1\|2` | Saltea el menú |
| `--demo [grados]` | Los personajes caminan solos (con 2, en direcciones opuestas) |
| `--frames N` | Sale después de N cuadros (o N ticks con `--headless`; con `--autotest`, corta la prueba ahí) |
| `--screenshot archivo.bmp` | Guarda una captura al salir |
| `--headless` | Simula sin ventana, para pruebas |
| `--bench N` | Con `--headless`, mide cuánto tarda en dibujar N cuadros |
| `--sprite-test` | Dibuja al Bárbaro y la Amazona en las 16 direcciones; `D2_SPRITE_TOKENS=wk,ne` y `D2_SPRITE_MODE=sc,1ht,8` eligen los personajes, la animación, la clase de arma y el cuadro; `D2_SPRITE_ARMOR=hvy` (o `med`), la armadura del cuerpo |
| `--autotest act1` | Con `--headless`, juega el Acto 1 solo y muestra el informe |
| `--autotest act2` | Lo mismo con el Acto 2 (63 pasos) |
| `--autotest act3` | Lo mismo con el Acto 3 (74 pasos; arranca en el muelle con los dos actos anteriores hechos) |
| `--autotest act4` | Lo mismo con el Acto 4 (36 pasos; arranca en la Fortaleza del Pandemónium) |
| `--autotest act5` | Lo mismo con el Acto 5 (62 pasos; arranca en Harrogath con los cuatro actos anteriores hechos y en el nivel 20) |
| `--autotest skills` | Con `--headless`, la prueba de habilidades contra Andariel: falla si alguna ronda no la mata en 2 minutos |

Variables de depuración:

| Variable | Qué hace |
|---|---|
| `D2_DEBUG=1` | Marca los subtiles que bloquean el paso y, con `--headless`, muestra el estado de los jugadores; en la prueba de habilidades, al empezar, lo que dice el árbol de cada habilidad en los niveles 1 y 20, y a cuántos afecta cada uso |
| `D2_SEED=N` | Fija la semilla del Blood Moor |
| `D2_START=x,y` | Elige dónde aparece el jugador (en subtiles) |
| `D2_START_NEAR=fallen1` | Aparece al lado de ese monstruo |
| `D2_WALK=x,y` | Con `--demo`, camina siempre en esa dirección de pantalla |
| `D2_ATTACK=1` | Con `--demo`, se queda quieto y ataca cada medio segundo |
| `D2_LIST_GFX=1` | Lista los gráficos de monstruos y objetos que se cargan |
| `D2_ACTION=1` | Con `--demo`, aprieta acción cada 6 ticks sin dejar de caminar |
| `D2_DROPTEST=N` | Tira N veces el botín de un Fallen y muestra qué sale |
| `D2_SPAWN_ITEMS=1` | Deja oro, pociones y equipo al lado del inicio |
| `D2_INV_OPEN=1` | Arranca con el inventario abierto |
| `D2_EQUIP=ltp,hlm` | Equipa esos objetos al empezar (aunque no sean mejores) |
| `D2_INV_SCRIPT=ddda` | Con `--demo`, una tecla cada 5 ticks para el jugador 1 (u/d/l/r, a/b/x/y, i = inventario, t = árbol, o = LB, c = R3 las frases, . = nada) |
| `D2_PATHTEST=1` | Verifica que se llegue caminando del pueblo a los waypoints y a Blood Raven, y hasta Corpsefire |
| `D2_DUMP_TILES=x0,y0,x1,y1` | Con `D2_DEBUG`, dibuja con caracteres la colisión de esos tiles |
| `D2_ONEHIT=1` | Cada golpe de un jugador mata (para probar las misiones) |
| `D2_MEM=1` | Muestra la memoria usada en cada paso del arranque y al armar o borrar zonas |
| `D2_ZONES=1` | Muestra cada zona que se arma o se borra y cuánto tardó (y cuántos DT1 llegaron preparados del hilo de trabajos y cuántos se leyeron al armarla; cuánto fue estampar, leer los DS1 y los DT1, la colisión y cada parte de las unidades; en cuántos ticks se armó y el que más tardó, y en qué parte: mapa, presets, objetos, monstruos o "entera"), y las que se van a armar pronto, cuyos DT1 se piden antes |
| `D2_ZONE_BUDGET=MS` | Milisegundos por tick para armar una zona en partes mientras uno se acerca (por defecto 20; 0 = entera de una vez, como antes) |
| `D2_ZONE_PIECES=N` | Sin el hilo de trabajos (las pruebas sin gráficos), cuántas piezas se estampan por tick al armar una zona en partes (por defecto 16; así las pruebas no dependen del reloj) |
| `D2_ZONE_PARTS=1` | Pruebas: también las zonas que se arman enteras (al llegar, `D2_ZONETEST`) van de a una pieza por pasada, para comparar con `D2_COLLHASH` y `D2_OBJ_TEST=1` que salen iguales |
| `D2_GRID_CHECK=1` | Pruebas: cada búsqueda de monstruos por la grilla de `units.c` (el fuego del piso, las nubes, a quién le pega cada proyectil) se compara con la de todas las unidades y el juego se corta si difiere (un lugar nuevo que mueve unidades sin llamar a `grid_moved`) |
| `D2_ZONE_MAX=N` | Cuántas zonas puede haber armadas a la vez sin contar el pueblo (por defecto 6) |
| `D2_ZONE_RESET=S` | Segundos sin nadie para borrar una zona (por defecto 600) |
| `D2_TOUR="Stony Field,4"` | Lleva al jugador 1 a esos niveles (por nombre o número de zona), uno cada `D2_TOUR_EVERY` ticks (100); los demás jugadores van a su lado |
| `D2_GOD=1` | Los jugadores no reciben daño |
| `D2_ZONETEST=1` | Arma cada zona y verifica que se llegue caminando a sus jefes, objetos y salidas (también las del acto en que arranca); `2`, además, dibuja cada zona con caracteres. Avisa, como dato, los tiles a los que se llega sin nada dibujado o con pared y sin piso |
| `D2_ZONEREACH=1` | Con `D2_ZONETEST`, cuántos subtiles de cada zona se alcanzan desde su entrada (para comparar dos versiones del armado) |
| `D2_NO_STAMP_RULES=1` | Estampa las piezas enteras, sin KillEdge ni FillBlanks (para ver qué cambia por eso) |
| `D2_A5GRID=1` | Al armar una barricada del Acto 5, imprime su grilla de celdas de 8x8 (la pieza en su celda, `+` lo que tapa, `.` vacía) |
| `D2_MAZEDUMP=1` | Al armar un calabozo de `world3.inc` (Actos 3 y 5), imprime cada sala: su lugar en la grilla, la pieza de `lvlprest.txt` y sus salidas |
| `D2_SPAWN="scarab1,4"` | Esos monstruos alrededor del jugador 1 al empezar (también un jefe con nombre: `D2_SPAWN="Lord De Seis"`, con su escolta) |
| `D2_AI_DEBUG=1` | Muestra los monstruos con IA del original que aparecen, sus habilidades y cada ataque o habilidad que usan |
| `D2_WAYPOINTS=0x3` | Waypoints activados al empezar (un bit por waypoint) |
| `D2_ASYNC=1` | Con `--headless`, carga los gráficos en otro hilo como en el juego |
| `D2_MERC="0,5"` | Cada jugador arranca con un mercenario de ese tipo (el Id de `hireling.txt`: 0 fuego, 1 frío; 6 Plegaria, 7 Desafío, 8 Puntería bendecida) y nivel |
| `D2_MERC_DEBUG=1` | Muestra cada golpe de los guerreros del desierto (golpe o estocada, daño, si falló) |
| `D2_ACT=2` | Arranca en ese acto (si la partida guardada está en uno más avanzado, en ese) |
| `D2_START_NEAR=Corpsefire` | También sirve con jefes (y con `D2_ACT=2`, con los del Acto 2) |
| `D2_A2STATE="3,3,4,5,3,1"` | Cómo arrancan las seis misiones del Acto 2 (Radament, el Báculo, el Sol, el Santuario, el Invocador y las Tumbas: el paso de cada una) |
| `D2_A2QTEST=1` | Con `D2_ACT=2`, recorre las seis misiones del Acto 2 con el J1 (cruzando las zonas, matando a los jefes y hablando con cada NPC) y muestra cómo queda cada misión en cada paso |
| `D2_A3QTEST=1` | Lo mismo con las seis del Acto 3 (con `D2_ACT=3`) |
| `D2_A4QTEST=1` | Lo mismo con las del Acto 4 (con `D2_ACT=4`): Izual, la Forja, los sellos, Diablo y el portal del final; además rearma el Santuario con los jefes vivos (tiene que haber uno de cada uno) y va al Acto 3 y vuelve (el portal no pasa al Acto 3 y sigue al lado de Tyrael) |
| `D2_DIFF=1` o `2` | La partida en Pesadilla o Infierno (sin pasar por el menú): los monstruos, los tesoros, las tiendas, los mercenarios y las misiones de esa dificultad (`docs/plan-dificultades.md`). Con la demostración (`D2_AUTOTEST_DEMO`), los personajes tienen 25 o 50 niveles más, las habilidades 8 o 16 más y el equipo de esa dificultad (`demo_diff_gear`: bases excepcionales o de élite raras, con resistencias que compensan la penalidad, vida, defensa y daño aumentado en las armas) |
| `D2_DIFFTEST=1` | En el Acto 1, muestra en cada dificultad el nivel de los monstruos de algunos niveles, cuáles salen y cuántos jefes, y los números (nivel, vida, daño, experiencia, defensa, puntería, resistencias, habilidad y tesoro) de algunos monstruos, un jefe de misión, uno de nombre, uno al azar con su escolta |
| `D2_UBERTEST=1` | Con `D2_DIFF=2` y `D2_ACT=5`, el evento Pandemonium (`src/endgame.inc`): que caigan las llaves, los órganos y las esencias, la receta donde no va, los tres portales en Harrogath (y que no haya un cuarto), cada mapa con su jefe y su órgano, Uber Tristram con los tres y sus esbirros (mide un tick al lado) y la Antorcha y los Estandartes. Dice `UBER: BIEN` o `UBER: MAL` en cada cosa |
| `D2_LANG=deDE` | El idioma (`esMX`, `enUS`, `esES`, `deDE`, `frFR`, `itIT`, `ptBR`, `plPL`, `ruRU`, `jaJP`, `koKR`, `zhCN`, `zhTW`; también `es` y `en`), antes que el del menú (`opciones.txt`) y el del importador (`idioma.txt`) |
| `D2_LANG_DUMP=archivo` | Anota cada texto que se dibujó sin traducción en el idioma elegido |
| `D2_RESTART_LANG=deDE` | Con ventana: a los pocos ticks en el menú, hace como si se eligiera ese idioma (lo guarda y el juego vuelve a arrancar) |
| `D2_TZTEST=1` | Las zonas de terror (`src/endgame.inc`, `src/tztest.inc`), con `D2_DIFF=2 D2_ACT=1 D2_TZ=1 D2_TZ_ZONE=2 D2_TZ_HERALD=1`: lo que se leyó de `desecratedzones.json`, la rotación, las clases de tesoro por nivel, los monstruos de las Cold Plains aterrorizadas (y los del Blood Moor, que no), un Heraldo que sale, muere y sube el grado, una astilla y un amuleto Sunder. Dice `TERROR: BIEN` o `TERROR: MAL` en cada cosa |
| `D2_ECO=1` | Arranca una corrida del Eco del Odio (`src/eco.inc`, `docs/plan-eco-del-odio.md`) en vez de la historia, con los personajes de las partidas 1 y 2. `D2_ECO_ARENA=0..3` la arena (el Trono, la Cumbre del Arreat, Uber Tristram, la Colossal Summit), `D2_ECO_WAVE=N` empezar en esa oleada, `D2_ECO_LEVEL=N` el nivel de los monstruos de la oleada 1 (si no, el de los personajes), `D2_ECO_DIFF=0..2` las tablas de esa dificultad, `D2_ECO_CAP=N` el tope del desborde, `D2_ECO_PACE=N` el ritmo en % de los tiempos (si no, el de los niveles de los personajes, por escalones), `D2_ECO_DEBUG=1` lo que hace cada oleada (y cada 5 segundos, cuántos vivos y muertos); `D2_ECO_PROBE=x,y` dibuja en la consola lo que se camina alrededor de ese lugar de la arena |
| `--autotest eco` | Con `D2_ECO=1`: el bot pelea las oleadas (sin daño y de un golpe, en área; `D2_ECO_REAL=1` de verdad, `=2` los golpes de verdad sin recibir daño, para las capturas; con `D2_ECO_DEMO=1` pelean los bots de la demostración, que reparten puntos, aprenden y usan habilidades y toman pociones: para ver hasta dónde llega un personaje nuevo) hasta la 25 con cada jefe, termina desde la pausa, abre el cofre y entra al portal. Con `D2_ECO_WAVE=29`, la segunda vuelta (los uber, hasta la 50). Los pasos de las oleadas altas pueden fallar por desborde (el bot no da abasto): `D2_ECO_CAP=150` |
| `D2_LOCKSTEP=1` | Como en la red: nada de la partida depende de cuánto tarda algo (las zonas se arman en partes por cantidad de piezas, los monstruos se mueven aunque sus dibujos no estén) |
| `D2_HASH=N` | Cada N ticks, `HASH tN` con el resumen del estado (`game_state_hash`): dos corridas iguales tienen que dar lo mismo. `D2_HASH_DUMP=T` vuelca en el tick T cada unidad y cada jugador, para encontrar dónde se separan |
| `D2_NO_JOBS=1` | (con `--autotest`) sin el hilo de carga: todo en el hilo del juego, para comparar. `D2_JOBS_JITTER=N`: hasta N ms de espera al azar antes de cada trabajo del hilo |
| `D2_NET_HOST=N` / `D2_NET_JOIN=ip` | La red sin ventana (`src/net.c`, `docs/plan-lan.md`): con `--autotest` crea la partida y empieza cuando hay N jugadores, o se une; `--players` dice cuántos son de esta máquina, `D2_NET_SLOTS="1,2"` sus partidas guardadas (sin, personajes nuevos de `D2_CLASSES`), `D2_NET_PORT` el puerto (6112), `D2_NET_PREPLAY=N` antes juega N ticks de otra partida; con `D2_ECO=1` (y `--autotest eco`), una corrida del Eco del Odio en red. Al final dice `red: sin separarse` o por qué se cortó |
| `D2_SPREAD=1` / `D2_TICK_PERF=N` | Para medir: cada jugador en otro nivel del acto (el peor caso), y cada N ticks lo que tarda la lógica, las zonas armadas y las unidades (sin `--autotest`) |
| `D2_TZ=1` o `0` | Zonas de terror sí o no, sin mirar si el jugador 1 mató a Baal en esa dificultad (sin la variable, en la demostración y las pruebas automáticas no hay) |
| `D2_TZ_ZONE=n` | La zona aterrorizada es la número n de `desecratedzones.json` (0 = Burial Grounds, 2 = Cold Plains...), en vez de la del turno |
| `D2_TZ_TIME=segundos` | La hora (desde 1970) con la que se calcula el turno de la rotación |
| `D2_TZ_HERALD=1` | La primera muerte en la zona aterrorizada ya despierta al Heraldo (en Infierno) |
| `D2_TZ_DEBUG=1` | Lo que se leyó del json, cada zona que se aterroriza y cada Heraldo |
| `D2_COWTEST=1` | El nivel de las vacas (`src/cows.inc`) en el Acto 1: el cuerpo de Wirt (la pierna y de 10 a 19 pilas de oro), la receta del cubo donde no va (sin Baal, fuera del campamento, otra vez en la misma partida, con el Rey Vaca muerto), el portal de ida y de vuelta, las vacas, el Rey Vaca (sus 8 pociones de aguante) y que se guarde; mide un tick entre las vacas (con `D2_GOD=1` no se muere). Dice `COW: BIEN` o `COW: MAL` en cada cosa |
| `D2_WARPS=1` | Con `D2_ZONETEST`, lista cada salto (de qué zona a cuál) y los jefes y objetos de misión que encontró en cada zona |
| `D2_AI_DEBUG=1` | Lo que decide la IA de cada monstruo (con sus hechizos y su daño) |
| `D2_COLLDUMP="zona,tx,ty,ancho,alto"` | Con `D2_ZONETEST`, dibuja con caracteres la colisión de ese rectángulo de la zona (en tiles desde su esquina; dice en qué subtile del mapa empieza): `.` libre, `#` pared, `o` objeto, `~` vacío, `?` otra cosa (0x10, una salida, se camina) |
| `D2_DS1_GRID=duriel.ds1` | Al estampar un DS1 que tenga eso en el nombre, lista cada celda: los pisos (estilo.secuencia), las paredes (orientación:estilo.secuencia) y si están ocultos |
| `D2_ZONES=1` | También dice qué bocas de cueva pasan a usarse con A (porque al llegar se quedaba encerrado) |
| `D2_GOLD=N` | Arranca con ese oro |
| `D2_MAGIC=N` | Vuelve mágico todo el equipo inicial, con afijos de nivel N |
| `D2_DROPNAMES=1` | Con `D2_DROPTEST`, lista los mágicos, raros, únicos y de conjunto con sus propiedades |
| `D2_DROPTC="Act 1 H2H A"` | Con `D2_DROPTEST`, la clase de tesoro a probar |
| `D2_DROPLVL=N` / `D2_DROPMF=N` | Con `D2_DROPTEST`, el nivel del monstruo y la búsqueda de magia |
| `D2_ITEMTEST=N` | Guarda y lee de vuelta N botines de Andariel, arma una palabra rúnica, engarza una gema y prueba recetas del cubo |
| `D2_INVSHOW=0..8` | El J1 con anillos, amuleto, conjunto, palabra rúnica, talismanes y el cubo (0 cartel del conjunto, 1 cubo abierto, 2 talismán, 3 palabra rúnica, 4 atributos, 5 a 7 el árbol de habilidades en esa solapa, 8 el registro de misiones) |
| `D2_QLOG=N` | Con `D2_INVSHOW=8`, el registro abierto en la misión N del Acto 2 (0 a 5) |
| `D2_GUIDE=cat,receta` | Arranca con la Guía del cubo abierta en esa categoría y receta (3 = las palabras rúnicas) |
| `D2_SHOW_TARGET=nombre` | Muestra arriba la barra de ese monstruo (su Id o el nombre del jefe), con lo que dice debajo (no muerto o demonio, modificadores, inmunidades) |
| `D2_CONTROLS=0` | Arranca con la pantalla de controles abierta (0 el joystick, 1 el teclado; `src/controls.c`, que repite lo de `input.c`: si cambia un control, cambiarlo ahí también) |
| `D2_CUBE_DEBUG=1` | Muestra qué receta se usó al transmutar |
| `D2_LEVEL=N` | Sube a los personajes a ese nivel |
| `D2_SKILLS="Bash:3,Howl"` | Les da esas habilidades y las pone en los botones |
| `D2_ATTACK=x` | Con `--demo`, usa el botón B, X o Y en vez del A |
| `D2_BAG="sbw,lea"` | Pone esos objetos en la mochila |
| (Windows) | `d2port.exe` se compila desde WSL con `cmake/mingw-w64.cmake` (`build-win/`; `SDL2_DIR` la de SDL2-devel-2.30.x-mingw). Lo que cambia con Linux está en `src/compat.c` (carpetas, reemplazar archivos, temporales, la carpeta del .exe y el registro `d2port.log` sin consola). La red local va con Winsock (en `net.c`, unas macros para lo que cambia: cerrar, no bloquear, los errores; la IP propia "conectando" un UDP afuera; la búsqueda pregunta a cada red de la máquina con GetAdaptersAddresses / getifaddrs, a 255.255.255.255 y a 127.0.0.1, y el anfitrión contesta con un número de la partida para no repetirla). `D2_NET_BIND=127.0.0.1` escucha solo en esa dirección: en Windows así no pregunta el firewall en las pruebas. En una sesión con `NoDefaultCurrentDirectoryInExePath`, cmd no busca programas en la carpeta actual: los .bat llaman con `%~dp0` |
| (red entre máquinas distintas) | La Pi, Linux y Windows tienen que calcular lo mismo bit por bit (`docs/plan-lan.md`, "La Pi, Linux y Windows juntas"): nunca dos sorteos en la misma expresión ni como argumentos de una misma llamada (el orden no está definido y GCC lo hace distinto en x86 y en ARM); `sinf`/`cosf`/`atan2f`/`expf`/`hypotf` son las de `src/dmath.c` en los archivos que incluyen `dmath.h` (un archivo nuevo de la simulación tiene que incluirlo); nada de `qsort` con empates en lo que se simula; `-ffp-contract=off -fsigned-char` en CMakeLists. Para comprobar: `D2_ZONETEST=1 D2_COLLHASH=1` (la colisión de cada zona) y las partidas con `D2_LOCKSTEP=1 D2_NO_JOBS=1 D2_HASH=50`, en x86 y en ARM con `qemu-aarch64-static -L /usr/aarch64-linux-gnu` |
| `D2_VIDEO=archivo.mp4` | (con `--autotest`) Graba la prueba: cada cuadro pasa a ffmpeg (`D2_FFMPEG`, si no `ffmpeg`; en WSL, `~/ffwrap/ffmpeg` llama al de Windows). `D2_VIDEO_EVERY=N` un cuadro cada N ticks (el video va N veces más rápido), `D2_VIDEO_CRF=N` la calidad (28; más alto, más chico) |
| `D2_MENU_SCRIPT="ada"` | Sin `--players`, aprieta botones en el menú (u/d/l/r, a, b, x); `D2_MENU_DELAY=ms` espera entre ticks (para ver la red: una máquina crea con `ddaaadaa` y otra la busca con `ddaada`) |
| `D2_SAVE_DIR=carpeta` | Dónde se guardan las partidas (con `--headless` solo se guarda si está) |
| `D2_NOSOUND=1` | Sin sonido |
| `D2_AUDIO_LOG=1` | Muestra cada sonido que arranca, los cambios de música y los archivos que faltan |
| `D2_AUDIO_DUMP=archivo.wav` | Con `--headless`, graba lo que suena |
| `D2_PAUSE=1` | Arranca con el menú de pausa abierto |
| `D2_RGB565=1` | Sube cada cuadro a la GPU en 16 bits en vez de 32 (para probar en la Pi si "presentar" tarda mucho) |
| `D2_TALKTEST=N` | Abre el diálogo de cada NPC del pueblo y aprieta "Hablar" N veces, mostrando qué dice cada vez (sirve con una partida guardada); con los que apuestan, la lista y una compra; con Kashya y Greiz, los mercenarios que ofrecen |
| `D2_MAP_OPEN=1` | Arranca con el automapa abierto |
| `D2_DEMO_SECONDS=N` | Segundos con el menú quieto hasta que arranca la demostración (por defecto 10; 0 = nunca) |
| `D2_DEMO_ACT=1` | La demostración del menú juega solo el Acto 1 con nivel 30; `2`, el Acto 2 con nivel 40 y las habilidades en 6; `3`, el Acto 3 con nivel 50 y las habilidades en 8; `4`, el Acto 4 con nivel 60 y las habilidades en 10; `5`, el Acto 5 con nivel 70 y las habilidades en 12 (por defecto, la campaña: los cinco actos en Normal, Pesadilla e Infierno con los mismos personajes; con `D2_DIFF`, desde esa dificultad) |
| `D2_DEMO_FROM=5` | La campaña de la demostración empieza en ese acto (y sigue con los demás y las otras dificultades): para ver pronto el paso de un acto o de una dificultad a la siguiente |
| `D2_AUTOTEST_DEMO=1` | Con `--autotest` y `--players 2`, juega como la demostración (la campaña entera, con un tope de 6 horas de juego por acto; con `D2_DEMO_ACT=1` a `5`, solo ese acto) (1: el Bárbaro es el primero y la Amazona lo acompaña, 2: la Amazona y la Hechicera, 3: la Hechicera y el Paladín, 4: el Paladín y el Nigromante, 5: el Nigromante y la Asesina, 6: la Asesina y el Druida, 7: el Druida y el Brujo, 8: el Brujo y el Bárbaro, 9: el Brujo y el Paladín, la primera de la rotación): peleas de verdad, sin guardar |
| `D2_DEMO_PLAYERS=1` | La demostración con un solo bot (por defecto juegan dos) |
| `D2_PATH_DEBUG=1` | Dibuja con caracteres los caminos de tierra de cada zona exterior que se arma (`o` camino nuevo, `E` entradas y lugares) |
| `D2_TILE_SHEET="act1/town/floor.dt1,0[,N]"` | Con `--sprite-test --screenshot`, los pisos de ese estilo con su secuencia (salteando N); `D2_TILE_NOLABEL=1` sin los números |
| `D2_DS1_FLOORS=1` | Con `--ds1-info`, lista cada piso del DS1 con su estilo, secuencia y DT1 |
| `D2_WALL_INFO=x0,y0,x1,y1` | Muestra cada pared y piso que se arma en esos tiles: orientación, si es oculto, cuántos subtiles bloquea, si tiene dibujo y de qué DT1 sale |
| `D2_SHOW_POS=1` | Muestra en las esquinas de arriba la zona y la posición (en subtiles) de cada jugador; en la demostración sale siempre (jugando, también con "VER POSICIÓN" en la pausa) |
| `D2_KILL=ticks,nombre` | A los tantos ticks mata a ese monstruo (`bloodraven`, `andariel`...) para ver su muerte; `all` mata a todos los cargados |
| `D2_ANIM_CHECK=1` | Al arrancar, carga las animaciones de cada monstruo con gráficos y avisa las que faltan |
| `D2_FX_SHOTS=prefijo` | Con `--autotest`, guarda capturas de los efectos de los jefes y de las Piedras Cairn (`prefijo_tipo_tick.bmp`) |
| `D2_BLOCK_DUMP=x,y,radio` | Al empezar, dibuja con caracteres qué subtiles bloquean alrededor de (x, y) (`#`), con el jugador 1 en `@` |
| `D2_DEPTH_AUDIT=1` | Al empezar, arma todas las zonas y revisa el orden de dibujo en cada lugar donde puede pararse el jugador: cuenta (y muestra los primeros) dónde quedaría encima de una pieza que tiene delante o tapado por una que tiene detrás |
| `D2_FX_DEBUG=1` | Muestra cada efecto de habilidad que se pide (overlay.txt) |
| `D2_SHOW_CURSOR=1` | En pantalla completa, deja ver el puntero del mouse |
| `D2_CLASSES=2,1` | Las clases de los jugadores en las pruebas (0 Bárbaro, 1 Amazona, 2 Hechicera, 3 Paladín, 4 Nigromante, 5 Asesina, 6 Druida, 7 Brujo; por defecto 0,1) |
| `D2_INVSHOW=5..7` | El árbol de habilidades del J1 en la solapa 1, 2 o 3 (para las capturas) |
| `D2_INVSHOW=8` | El registro de misiones del panel del J1 |
| `D2_INV_PAGE=N` | Con `D2_INV_OPEN`, abre esa página del panel (1 = el árbol de habilidades) |
| `D2_WP_OPEN=1` | Abre el panel de waypoints del jugador 1 al empezar (en el acto de `D2_ACT`) |
| `D2_DEMO_MODE=nuevo` | La demostración del menú juega desde el nivel 1 (por defecto, con nivel 50 y todas las habilidades en 8 en el Acto 3, 40 y en 6 en el Acto 2, o 30 y en 1 en el Acto 1); `habilidades`: la prueba contra Andariel |
| `D2_ST_ROUND=N` | En la prueba de habilidades, empieza en esa ronda |
| `D2_ST_ROUNDS=N` | En la prueba de habilidades, hace solo N rondas |
| `D2_ST_SOLO=1` | En la prueba de habilidades pelea uno solo (1 o 2; el otro mira de lejos), para medir cada habilidad sola |
| `D2_ST_MONSTER=skeleton1` | En la prueba de habilidades pelea contra ese monstruo (y dos más de compañía) en vez de Andariel |
| `D2_CONVERT_ALWAYS=1` | La Conversión del Paladín prende siempre y su golpe hace 1 de daño (para probarla) |
| `D2_AURA_DEBUG=1` | Cada 5 segundos muestra lo que les dan las auras a los jugadores y a los aliados, y los bloqueos |
| `D2_ST_DEBUG=1` | En la prueba de habilidades, cada 2 segundos muestra dónde está cada uno, su estado, la vida de Andariel, la Valquiria y los esbirros del Nigromante |
| `D2_ST_CALM=1` | En la prueba de habilidades Andariel no ataca (para las capturas) |
| `D2_RENDER_EVERY=N` | Con `--autotest`, dibuja cada N ticks (por defecto 25; 1 para perfilar el dibujo) |
| `D2_AT_PERF=N` | Con `--autotest`, cada N ticks muestra el nivel, la lógica y el dibujo de ese tramo (promedio y peor) y las unidades (ver [docs/rendimiento.md](rendimiento.md)) |
| `D2_SHOTS=prefijo,desde,hasta,cada` | Con `--autotest`, guarda capturas entre esos ticks (en la prueba de habilidades, los de la ronda): `prefijo_tick.bmp` |
| `D2_DARK_DEFER=0` | La oscuridad de las cuevas en una pasada aparte, no al convertir el cuadro (para comparar) |
| `D2_FLOOR_CACHE=0` | Dibuja el piso entero en cada cuadro, sin guardarlo entre cuadros (para comparar) |

`scripts/shots.sh` genera capturas de prueba y `scripts/bmp2png.ps1` las
pasa a PNG.

`scripts/bench.sh` mide cuánto tarda en dibujarse un cuadro en seis escenas
fijas (el campamento con 1 y 2 jugadores, el Blood Moor, la pantalla
partida, la Guarida del Mal y las Catacumbas 4, con la semilla 7; las cuevas
también con la oscuridad aparte), con lo que tarda cada parte (mundo,
oscuridad, HUD, la conversión del cuadro) y, en la Pi, la frecuencia real
del procesador y si le falta voltaje. Va en el paquete al lado del binario,
para correrlo en la Pi: `cd /userdata/roms/ports/.data/d2port && ./bench.sh`.
`scripts/prof.sh` perfila tres escenas en la Pi con un binario compilado con
`-pg` (`d2port-prof`). Con
`D2_RENDER_EVERY=1`, la prueba automática dibuja en cada tick (como el
juego) y sirve para perfilar. `scripts/cmpshots.sh VIEJO NUEVO` compara
byte por byte las capturas de 17 escenas con dos binarios (para ver que una
optimización no cambie la imagen). Cómo medir en la Pi con la demo en
movimiento: [docs/rendimiento.md](rendimiento.md).

### Prueba automática del Acto 1

```bash
D2_SAVE_DIR=/tmp/sv D2_SEED=7 ./build/d2port --headless --players 1 --autotest act1
D2_SAVE_DIR=/tmp/sv D2_SEED=7 ./build/d2port --headless --players 2 --autotest act1
```

Juega el acto entero solo, sin ventana, en 68 pasos: hablar con cada NPC,
ir caminando de zona en zona, usar los waypoints, matar a cada jefe, tocar
las piedras, levantar los objetos de misión, devolverlos, viajar con Warriv
y revisar la partida guardada. Después de cada paso revisa que la misión
haya quedado como corresponde para cada jugador. Con 2 jugadores, el
segundo sigue al primero y pelea al lado.

Para que no dependa de la suerte, los jugadores no reciben daño (`D2_GOD`) y
matan de un golpe (`D2_ONEHIT`). Al final muestra un informe: pasos que
salieron bien, tiempo de juego, lógica y dibujo por cuadro, memoria máxima
(y en qué se usaba), ticks de más de 50 ms, zonas armadas a la vez y lo que
trabajaron los bots. Sale con código 0 si pasaron los 68.

Los bots usan `src/bot.c`, un módulo aparte para reusarlo:

- A\* con montículo binario, en una caja limitada a las zonas del tramo (la
  actual y la siguiente), con los buffers reusados entre búsquedas. Cada
  subtile se mira una sola vez por búsqueda.
- Una búsqueda larga se reparte en varios ticks (6000 subtiles por tick como
  mucho): mientras tanto el bot sigue el camino que tenía. Así varios bots a
  la vez no traban un cuadro.
- Ruta entre zonas con `world_route_next`: pasillos, escaleras, bocas de
  cueva y portales. No pisa saltos que no son el destino.
- Sigue el camino en línea recta hasta donde se ve, rehace la búsqueda cada
  5 segundos o si se desvía, y se destraba solo si queda quieto (un rato
  para un costado al azar que no pise una boca de cueva o una subida: al pie
  de la escalera del River of Flame subía sin querer al City of the Damned).
- La llegada se calcula igual en la búsqueda y en el chequeo de "llegó" (el
  centro del subtile adentro del destino), y las bocas de cueva que no son
  el destino se esquivan con un margen del cuerpo más un paso.

En la prueba, si un monstruo lo traba, el bot pega un segundo y vuelve a
intentar caminar (trabado, también a lo que está a un paso: así rompe la
prisión de hueso de Diablo); si la pelea no se termina porque un chamán revive a los
Caídos, va primero por el chamán. En la demostración, si en 3 minutos no le
bajó la vida al jefe del paso, se lo saltea como a los inalcanzables y deja
en el log dónde estaba cada uno. Si le habló sin querer a un NPC de afuera
(pegando con A al lado del espíritu de Izual, A habla), cierra el diálogo
en los pasos que no son de hablar; el que acompaña, siempre. Teletransporte,
la Hoja de distorsión y el Salto cruzan las paredes que solo son `COLL_WALL`
(como en el original) y pueden dejarlo en un hueco cerrado de una cueva: si
la demostración se queda sin camino, sale igual (`demo_escape`: hacia el
compañero o hacia adonde va, girando 45 grados en cada intento).
Si falla un paso, el informe dice qué hacía la navegación de cada bot, qué
tenía al lado y los enemigos vivos cerca (`at_stuck_report`).
- El A\* va en ocho direcciones; corta una esquina en diagonal solo si los
  dos lados están libres o si están tapados los dos (una escalera angosta
  en diagonal), como el jugador, que prueba la diagonal cuando no puede por
  los ejes.
- `bot_nav_step` devuelve hacia dónde moverse, si hay que apretar A (una
  escalera o un portal), si llegó o si no hay camino.

| Variable | Qué hace |
|---|---|
| `D2_AUTOTEST_FROM=N` | Empieza en el paso N |
| `D2_AUTOTEST_STATE="den_done,cain=3,wp=0x1f"` | Misiones y waypoints que se dan por hechos al empezar en otro paso (en el Acto 2: `rada`, `staff`, `sun`, `arcane`, `summ`, `tombs`; con `D2_BAG` para las piezas del Báculo) |
| `D2_AUTOTEST_SHOTS=carpeta` | Guarda una captura al terminar cada paso |
| `D2_AT_TRACE=1` | Cada 2 segundos, dónde está cada jugador, su estado y el monstruo más cercano (`=2`: además cada paso de la navegación) |
| `D2_BOT_DEBUG=1` | Muestra cada búsqueda de camino |
| `D2_WARP_DEBUG=1` | Muestra cada salto entre zonas y dónde se llega |
| `D2_COLL_DEBUG=1` | Dibuja la colisión de cada subtile cerca de la cámara: rojo pared que frena proyectiles, amarillo solo no se camina, naranja solo frena proyectiles, celeste solo tapa la vista, azul puerta, violeta objeto, verde salida, gris el grosor de una pared |
| `D2_STASH_TEST=1` | Prueba sin gráficos del cofre (pestañas, compra, oro, pasar objetos, guardar y cargar), los pergaminos apilados, el tomo viejo y el oro al morir; `=2`, `=3` y `=4` dejan el cofre abierto para las capturas (la pestaña "+", un objeto, uno en la mano) |
| `D2_SHOP_TEST=1` | (Acto 1) Prueba sin gráficos de la tienda de Charsi con controles de mentira: LB / RB y Select cambian de solapa, la cruceta pasa de la mercadería a la mochila y vuelve, A vende (y el vendedor lo pone a la venta) o compra, lo de las misiones no se vende; `=2` deja la tienda abierta para una captura y `=3` vendiendo |
| `D2_TPTEST=1` | Con `D2_START` afuera del pueblo: abre un portal al pueblo, entra, vuelve y revisa que se cierre |
| `D2_NO_CINEMA=1` | Arranca sin los logos ni la presentación |
| `D2_CINEMA=nombre` | Pasa esa cinemática (`act02start`...) en la ventana y sale |
| `D2_CINEMA_BENCH=nombre` | Decodifica esa cinemática entera lo más rápido posible y dice cuántos cuadros por segundo da (sirve sin ventana, para medir la Pi) |
| `D2_CINEMA_SHOT=archivo.bmp,segundo` | Con `D2_CINEMA`, guarda el cuadro de ese segundo tal como lo dibuja la GPU |
| `D2_CINEMA_LOG=1` | Cada segundo de una cinemática, cuánto sonido hay en la cola |
| `D2_DUMP_BLOCKS=x,y` | Con `D2_DEBUG`, la colisión alrededor de ese subtile (radio `D2_DUMP_R`, 5): `#` bloquea, `v` grosor de pared, `u` alguien, `+` no entra la cruz, `-` el centro queda pegado a una pared |
| `--selftest` | Prueba la colisión con baldosas inventadas (armado de la máscara, filas del DT1, vacío, trazado de línea) y sale |

### Prueba automática del Acto 2

```bash
D2_SAVE_DIR=/tmp/sv D2_SEED=7 ./build/d2port --headless --players 1 --autotest act2
D2_SAVE_DIR=/tmp/sv D2_SEED=11 ./build/d2port --headless --players 2 --autotest act2
```

Arranca en Lut Gholein con el Acto 1 hecho (y oro) y juega el Acto 2
entero en 63 pasos, con los controles y los mismos bots que la del Acto 1:

- Atma, el guerrero de Greiz y una apuesta con Elzix.
- Las cloacas, Radament, el Libro de Habilidad y el cofre del pergamino.
- Por el desierto, las tres piezas del Báculo: el cubo en los Halls of the
  Dead, el bastón en el Maggot Lair y el amuleto en el altar del Claw Viper
  Temple, después de que se oscurece el sol.
- Armar el Báculo en el cubo (con X, como una persona: abrir el cubo,
  pasarle la vara y el amuleto, transmutar).
- Drognan, Jerhyn, el palacio y el portal al Arcane Sanctuary.
- El Invocador, el diario y el símbolo de la tumba verdadera en el registro.
- El Canyon, la tumba del símbolo, el Orificio, Duriel, Tyrael y su portal.
- Jerhyn, Meshif y revisar la partida guardada.

Usa los 9 waypoints del acto. El Acto 1 y el 2 comparten el informe (pasos,
tiempo, memoria, ticks lentos, zonas armadas, los bots). Con
`D2_AUTOTEST_DEMO`, la demostración juega estos mismos pasos peleando de
verdad (saltea el guerrero y la apuesta).

## Objetos que se usan (cofres, barriles, santuarios, pozos)

`src/objects.inc` (incluido en `game.c`), como `D2Game/src/OBJECTS` de D2MOO:

- **Al crearlos** (`OBJECTS_InitHandler` y el `InitFn` de `objects.txt`): los
  cofres tienen trampa con `MonLvl/8+5` % y los que se pueden cerrar
  (`Lockable`) salen cerrados con `MonLvl/2+8` %; las urnas y los cadáveres,
  trampa con el mismo %; el santuario sale de `shrines.txt` según `Parm0`
  (vida, maná, o 90% de los que duran y 10% de los mágicos, con su
  `LevelMin`); el pozo trae `2*Parm2` cargas; 1 de cada 14 de los
  `PreOperate` (urnas, barriles) ya está roto.
- **Con A** (las `OperateFn`, cartel "A: Arcón" con el nombre del juego):
  el cofre cerrado pide una llave y la gasta (la Asesina no la necesita; sin
  llave dice "necesito una llave"); el cerrado tira dos veces la clase de
  tesoro y el abierto una, con 25% de nada; la clase es `Act N Chest A/B/C`
  según el tercio de niveles de monstruos del acto en que cae el nivel. Los
  ataúdes sueltan algo seguro, las urnas y los barriles 21% (el barril se
  patea), los cadáveres y escondites una tirada; ataúdes y barriles sueltan
  un monstruo el 18% de las veces (`OBJRGN_GetTrapMonsterId`: zombis o
  momias, si no esqueletos, si no la cimitarra voladora, que en el Acto 1
  no sale). Los barriles que explotan les pegan a los de alrededor (de 1/32
  a 1/8 de la vida que les queda, `OBJEVAL_ApplyTrapObjectDamage`) y hacen
  explotar a los de al lado. Los estantes dan un arma o armadura superior
  del nivel de la zona menos uno; las bibliotecas, un pergamino o una pila
  de 5 (en lugar del tomo). Los cuerpos que arden de las mesetas del Acto 4
  (`JackInTheBox`, `OperateFunction48`) sueltan algo 9 de cada 10 veces; la
  otra se prenden, a los 20 cuadros queman a los que están a 2 y a los 25
  sueltan. Las almohadillas del Santuario Arcano (`TeleportPad1..4`,
  `OperateFunction27`) llevan al lado de la otra de su clase en la misma
  sala de 12x12 (o en una de al lado), siempre que se usen.
- **Trampas**: saltan a los 35 cuadros: rayo, saeta de fuego, nube de veneno,
  nova, fuego en el piso o monstruos.
- **Santuarios**: los de vida, maná y recarga llenan; los que duran (armadura,
  combate, resistencias, habilidad, recarga de maná, estamina, experiencia)
  valen uno a la vez (`shrine_kind` del jugador, por `shrine_props`); portal,
  gema, fuego, monstruoso, explosivo y veneno. Vuelven a andar a los minutos
  de `shrines.txt` (1200 cuadros cada uno). Los **pozos** curan la mitad de
  vida, maná y estamina, sacan el veneno y el frío, y recuperan una carga a
  los `Parm0` cuadros.
- **Reparto**: en cada sala (las piezas de los calabozos; afuera, celdas de
  8x8) salen los `ObjGrp0..7` de `levels.txt` con su `ObjPrb`, el objeto de
  `objgroup.txt` y su `PopulateFn` con la densidad (`DENSITY * (ancho*alto
  >> 7) >> 8`), en racimos los barriles y las urnas, hasta 10 santuarios y 4
  pozos por nivel, lejos entre sí. La sala del waypoint queda vacía. El
  `TrappedSoulPlaceholder` del Acto 4 no se ve: pone un racimo de almas
  atrapadas (`InitFunction46`: la primera en su lugar, las otras a saltos de
  10 a 18 subtiles), y el `JackInTheBox` sale uno por sala (`PopulateFn9`).
- **Lo que ya se usó** se recuerda en la partida por acto, clase y lugar: las
  tiradas salen de la semilla del mundo y de cada sala, así que al volver a
  armar una zona salen los mismos objetos, igual de abiertos (lo de fuera de
  la zona cuenta como ocupado al repartir, para que no dependa de la vecina).

Los bots abren al pasar cofres, ataúdes, urnas, barriles y cadáveres a menos
de 6 subtiles si no hay monstruos cerca (`at_open_objects`; los cerrados si
tienen llave; al que no llegan en 6 s lo dejan).

Un racimo de barriles o de cestas puede cerrar un pasillo (en el Jail, en
el Spider Forest): se ponen con las reglas del original
(`COLLIDE_MASK_PLACEMENT`) y hay que romperlos para pasar. `D2_ZONETEST`
camina a través de los que se rompen o se abren y dejan de tapar
(`obj_passable`).

Pruebas: `D2_OBJ_TEST=1` informa lo que hay en cada zona; `=2` además usa
cada uno en la zona del J1 y revisa la llave, que lo usado no se vuelva a
usar y que al armar la zona de nuevo siga igual (`objetos bien/MAL`); `=3`
con `D2_OBJ_SHOTS=prefijo` deja capturas de uno de cada clase (con el cartel
y abriéndose).

## El nivel de las vacas

`src/cows.inc` y la granja en `world.c` (`ZK_COWS`), como el original:

- **La pierna:** el cuerpo de Wirt en Tristram (`OperateFn` 33) suelta la
  Pierna de Wirt y, de a una cada 10 cuadros, de 10 a 19 pilas de oro
  (`OBJECTS_OperateFunction33_WirtsBody` y el caso `OBJECT_WIRTSBODY` de las
  misiones). Una vez por partida.
- **El portal:** la pierna con los pergaminos de portal en el cubo abre un
  portal rojo al lado del jugador (`ACT1Q4_CreateCowPortal`): solo en el
  Campamento de las Pícaras, con Baal muerto (`baal_q` 4 o más), si ese
  personaje no mató al Rey Vaca, y uno por partida. Si no, "imposible" y un
  mensaje que dice por qué (el original solo hace el sonido). El original
  pide el Tomo de Portal; el port no lo tiene (los pergaminos se apilan de a
  20), así que la receta usa la pila y gasta un pergamino. La receta sale en
  la guía del cubo ("Varios").
- **La granja** (Moo Moo Farm, Id 39): un páramo del Acto 1 de 80x80 con
  los bordes cerrados y sin caminos, río ni waypoint, con una pieza de cada
  relleno de `DRLGOUTWILD_SpawnSpecialPresets`: el vivac (`bivouac.ds1`, con
  el Rey Vaca en el medio por su marca de `monpreset.txt`), el estanque, un
  corral, dos de pantano y dos de piedras. El portal llega a una celda que
  sale de la semilla de la zona y queda libre. Las vacas andan en manada
  (`MinGrp`..`MaxGrp` de monstats: 5 a 10) y los grupos son los del Blood
  Moor por su `MonDen` (800 contra 520): unas 240 vacas, la misma escala del
  resto del port (el original pone unas cuatro veces más en todos los
  niveles).
- **El Rey Vaca:** al morir, los que están en la granja ya no pueden abrir
  el portal (se guarda: `cowking 1`) y deja 8 pociones de aguante
  (`ACT1Q4_UnitIterate_SetCustom6Flag`). El conjunto del Rey Vaca solo cae
  de las vacas (`ITEMDROPFLAG_HELLBOVINE`: `items_drop_from_bovine`).

Prueba: `D2_COWTEST=1` (ver la tabla de variables).

## Controles

La tabla completa está en el [README](../README.md#controles).

### Qué hace A

En orden: levanta el objeto del suelo que tenga al lado (si no hay un
monstruo a `MELEE_RANGE`), abre los waypoints, habla con un NPC, usa lo de
las misiones, abre un cofre o un barril, usa la escalera o el portal y, si
no, ataca (o usa la habilidad del botón). Con un monstruo pegado, A pega
antes que abrir los waypoints, hablar, abrir un cofre o tomar la escalera
(antes, al lado del waypoint del River of Flame o de Hadriel, A abría y
cerraba el panel o el diálogo y no se le podía pegar al que atacaba: los
bots de `--autotest act4` se quedaban ahí hasta que se pasaba el tiempo).
Lo de las misiones se usa aunque haya monstruos cerca. En el pueblo, si un
portal está más cerca que el NPC, A entra al portal y el cartel dice lo
mismo (`portal_first`: Anya camina junto a su portal rojo al templo y A le
hablaba a ella).

Los bots de las pruebas van primero por el que revive, levanta muertos o
pare crías (la madre vil mientras le queden: `units_find_reviver`) si la
pelea dura más de 3 segundos.

### Estamina

Correr gasta la barra de estamina al ritmo del original: `RunDrain`/256 por
tick, unos 2 puntos por segundo, y el Bárbaro arranca con 92. Si llega a
cero, el personaje queda agotado y solo puede caminar. La barra se llena de a
poco (`STAMINA_REFILL_SECONDS` en `game.c`) y recién cuando está completa se
puede volver a correr. Caminar o quedarse quieto también la recupera. Los
valores de cada clase salen de `charstats.txt`.
