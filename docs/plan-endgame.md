# El endgame del original: qué hay y qué haría falta (investigación, 23/10/2026)

Lo que pidió el usuario: investigar bien el endgame de Diablo II (el Diablo
clon y la Antorcha, "y todo eso") antes de la próxima actualización. Esto es
la investigación; todavía no hay nada hecho.

Fuentes: el código de D2MOO (`D:\tools\D2MOO`, lo marcado
`D2_VERSION_HAS_UBERS`), las tablas de **nuestra** copia del Resurrected
(`data/global/excel`) y guías de la expansión del Brujo (al final). Lo que
viene solo de guías está marcado así.

Hay **cuatro** cosas distintas que la gente llama "endgame":

| | De dónde es | Qué da | ¿Está la lógica del original? |
|---|---|---|---|
| 1. El evento Pandemonium (Uber Tristram) | LoD 1.11 | la **Antorcha de fuego infernal** | **Sí**, entera en D2MOO |
| 2. El Diablo clon (Uber Diablo) | LoD 1.10, solo en Battle.net | **Annihilus** | No: lo dispara el servidor del reino |
| 3. Las zonas de terror | Resurrected 2.5 | los amuletos Sunder, más experiencia | No (es del Resurrected) |
| 4. Los Ancianos colosales (Colossal Summit) | Resurrected, expansión del Brujo | las **Joyas colosales** | No (es del Resurrected) |

Y uno chico que va con el 1: el **Token of Absolution** (reasignar puntos).

---

## 1. El evento Pandemonium: las llaves, los órganos y la Antorcha

Todo esto está en D2MOO y en nuestras tablas.

**Las llaves** (solo en Infierno; `treasureclassex.txt`):
- Llave del Terror (`pk1`): la Condesa (`Countess Item (H)`, 1 en ~19 por tirada, 5 tiradas);
- Llave del Odio (`pk2`): el Invocador (`Summoner (H)`);
- Llave de la Destrucción (`pk3`): Nihlathak (`Nihlathak (H)`).

**Abrir los portales** (`cubemain.txt` op 28 "Pandemonium Portal";
`ACT1Q4_CreateUberRunPortal`):
- Las tres llaves en el cubo, en **Harrogath** y en **Infierno** (en otro lado
  no hace nada: suena el error).
- Abre un portal rojo a uno de los tres mapas, al azar entre los que todavía
  no se abrieron en esa partida. Cada mapa, una vez por partida.

| Nivel | Mapa | Cómo se arma | El jefe | Suelta |
|---|---|---|---|---|
| 133 Matron's Den | laberinto (`DrlgType` 1, como el Mausoleo) | DrlgMaze, con la excepción de `LEVEL_PANDEMONIUMRUN1` | **Lilith** (`uberandariel`, IA de Andariel: veneno) en el preset del cofre brillante (`OBJECT_SPARKLY_CHEST`) | el Cuerno de Diablo (`dhn`) |
| 134 Forgotten Sands | exterior de desierto (`DrlgType` 3) | `DrlgOutDesr.cpp` | **Duriel uber** (`uberduriel`: carga, Congelación sagrada), en el objeto número (semilla % 3) + 3 del mapa | el Ojo de Baal (`bey`) |
| 135 Furnace of Pain | laberinto de lava | DrlgMaze | **Izual uber** (`uberizual`, `AITHINK_Fn144_UberIzual`: Nova helada, Armadura escalofriante, teletransporte) en el cofre brillante | el Cerebro de Mefisto (`mbr`) |

Cada órgano sale siempre (TC `Uber Andariel`, `Uber Duriel` y `Uber Izual`:
1 de 1).

**Uber Tristram** (nivel 136, `Act1/Tristram/Tri_Town4.ds1`, que está en
nuestros datos):
- Los tres órganos en el cubo, en Harrogath y en Infierno: un portal a
  Tristram, uno por partida (`ACT1Q4_CreateUberFinalePortal`).
- Al armar el nivel salen juntos **Uber Mefisto, Uber Diablo y Uber Baal**
  (`SUNIT_SpawnPresetUnit` con `bSpawnPrimeEvils`).
- Cada uno llama esbirros (`STATE_UBERMINION`, sin experiencia):
  - Mefisto (`AITHINK_Fn146_UberMephisto`): esqueletos, arqueros y magos esqueleto;
  - Diablo (`Fn147`): agujeros de demonio (`demonhole`) que largan Señores del abismo;
  - Baal (`AIBAAL_SpawnUberBaalMinion`): espectros y vampiros.
- Llaman más cuando están solos (los otros dos lejos o muertos).
- Sus habilidades (`monstats.txt`):
  - Mefisto: rayo y nova venenosa "Prime", Ventisca;
  - Diablo: las de Diablo más **Diablogeddon**;
  - Baal: nova, infierno, tentáculos, Defense Curse, Blood Mana.

**La Antorcha** (`QUESTSFX_UberPrimeEvil`):
- Cuando mueren los tres, el último suelta la **Antorcha de fuego infernal**
  (`cm2` única: +3 a las habilidades de una clase al azar, 10-20 a todos los
  atributos y resistencias, cargas de Hidra).
- También suelta un **Estandarte de los héroes** (`std`) por jugador.

**Qué falta en el port:**
- **Que caigan las llaves y los órganos.** Hoy el port descarta esos objetos
  (los probé con `D2_DROPTEST`: la Condesa nunca suelta `pk1`), así que no hay
  objetos sueltos que no sirvan.
- **Las dos recetas del cubo** que abren el portal (op 28) y el portal rojo a
  esos niveles.
- **Los cuatro mapas:**
  - Tristram es un preset (fácil);
  - el desierto y los dos laberintos usan el generador que ya tenemos, con
    las excepciones del original.
- **Las IA de los Uber**: Izual, Mefisto, Diablo y Baal (D2MOO), sus
  esbirros, Diablogeddon y los "Prime".
- **Que los mapas se abran una vez por partida.**
- **La Antorcha al morir los tres** y el Estandarte.
- **Los archivos en la lista del importador**: los dibujos de `demonhole`, el
  nivel de Tristram en Infierno y los DS1 de los laberintos.

## 2. El Diablo clon y Annihilus

**Cómo era en el original:**
- Solo en Battle.net.
- El servidor del reino contaba los **Anillos de Jordan** que los jugadores
  vendían a los mercaderes. Esos mensajes del cliente están en D2MOO
  (`EVENTTYPE_JORDANSTONES`, "%d Stones of Jordan Sold to Merchants", y
  `EVENTTYPE_DIABLOCLONE`, "Diablo Walks the Earth"), pero la lógica del
  servidor no.
- Al llegar a la cantidad, la pantalla tiembla y sale "Diablo camina sobre la
  tierra".
- En una partida en **Infierno**, el próximo **jefe con nombre** (superunique)
  que aparece es reemplazado por el **Diablo clon** (`diabloclone`: las
  habilidades de Diablo más Diablogeddon).
- Al matarlo suelta **Annihilus** (`cm1` única: +1 a todas las habilidades,
  10-20 a todos los atributos y resistencias, 5-10 % de experiencia).
- En **Resurrected** (2.4) el contador es de toda la región, pero sigue
  siendo solo en línea. **Sin conexión no existe** (nunca existió en un
  jugador), salvo con mods como PlugY.

**Qué haría falta:** una regla propia, porque el original no tiene una para
jugar sin conexión. La propuesta, como hacen los mods:
- vender un Anillo de Jordan en Infierno suma uno a un contador guardado en
  la partida;
- al llegar a N (configurable; PlugY usa un número chico), sale el aviso;
- el próximo superunique de Infierno es el clon.

El clon usa la IA de Diablo, que ya tenemos, más Diablogeddon.

## 3. Las zonas de terror (Resurrected 2.5)

**Cómo funcionan** (de guías):
- Cada cierto tiempo un grupo de zonas queda "aterrorizado":
  - los monstruos suben hasta el nivel del personaje + 2 (con un tope por
    dificultad) y tienen un encantamiento más;
  - dan más experiencia;
  - sus jefes usan los TC "Desecrated".
- En la expansión del Brujo:
  - la rotación es cada 30 minutos;
  - las **Worldstone Shards** (`xa1`..`xa5`) aterrorizan un acto entero a
    elección;
  - aparecen los "Heralds".

**Qué hay en nuestros datos:**
- Los TC "Desecrated" (en `treasureclassex.txt`, por ejemplo `Countess Item
  (H) Desecrated A..F`);
- los TC "Terrorize Act Consumable", con las astillas;
- los seis amuletos **Sunder** (Cold Rupture, Flame Rift, Crack of the
  Heavens, Rotting Fissure, Bone Break, Black Cleft: rompen la inmunidad a
  ese elemento).

**Qué falta:**
- **La lista de zonas** (`desecratedzones.json`): no la importamos, habría
  que sumarla al importador.
- **Toda la lógica**: es del Resurrected y no está en D2MOO, así que habría
  que armarla con las guías y los datos.

## 4. Los Ancianos colosales (Colossal Summit, expansión del Brujo)

**En nuestros datos:**
- **Las estatuas** (`ua1`..`ua5`, "Uber Ancient Summon Material"):
  - salen de Andariel, Duriel, Mefisto, Diablo y Baal en Infierno (TC
    `Ancient Statue 1..5`, 1 o 2 en ~40 por tirada);
  - los TC "Desecrated" las dan más seguido. Según las guías, solo caen si el
    jefe está aterrorizado.
- **La receta del cubo:** las cinco estatuas en Infierno abren un portal rojo
  al **nivel 137** (`Garden/ColossalSummit/ColossalSummit.ds1`). El DS1 está
  en nuestros datos, pero no en la lista del importador.
- **Los monstruos:** `colossal1..3` (Talic, Korlic y Madawc), con IA nuevas
  del Resurrected (`ColossalWhirlwind`, `ColossalThrow`, `ColossalLeap`) **que
  no están en D2MOO**.
- **El botín:** las seis **Joyas colosales** (`cjw`: Defender's Fire/Bile,
  Protector's Frost/Stone, Guardian's Thunder/Light), según cuál Anciano
  muere último (TC `Uber Talic/Korlic/Madawc`).

**Cómo es la pelea** (de guías):
- los tres a la vez, cada uno inmune a su elemento y con una Convicción que
  baja 100 % esa resistencia;
- cada uno llama hasta 5 espíritus protectores;
- tienen fases al 50 % y al 25 % de vida, con habilidades nuevas (remolinos
  de fuego, fisuras de frío, rayos cargados, "Colossal Volcano" y "Colossal
  Thunderstorm");
- al morir uno, los otros se ponen más fuertes.

**Qué falta:** todo. Las IA habría que inventarlas mirando el juego, no
copiarlas, y además hacen falta las zonas de terror (punto 3).

## 5. El Token of Absolution (reasignar puntos)

- **Las esencias:**
  - Twisted Essence of Suffering (`tes`): Andariel y Duriel;
  - Charged Essence of Hatred (`ceh`): Mefisto;
  - Burning Essence of Terror (`bet`): Diablo;
  - Festering Essence of Destruction (`fed`): Baal;
  - todas en Infierno, en sus TC.
- **El cubo:** las cuatro esencias dan el **Token**; al usarlo se reparten
  de nuevo todos los puntos de habilidad y de atributos.
- **El port no tiene** ni el Token ni la reasignación de Akara.

---

## Recomendación

1. **El evento Pandemonium y la Antorcha**, primero: es lo clásico, está
   entero en D2MOO y casi todo en nuestros datos. Fases:
   - las llaves y los órganos caen;
   - las recetas y el portal rojo;
   - los cuatro mapas;
   - las IA de los Uber, con sus esbirros;
   - la Antorcha;
   - una prueba automática (como `D2_COWTEST`) y lo que necesita el bot.
2. **El Token of Absolution**, chico y útil (y la reasignación de Akara).
3. **El Diablo clon**, con la regla propia de vender Anillos de Jordan. Hay
   que decidir N y que esté claro en el README que no es del original para un
   jugador.
4. **Las zonas de terror** y **los Ancianos colosales**, al final: son del
   Resurrected, su lógica no está en ningún código y la pelea de los Ancianos
   habría que reconstruirla con guías.

Rendimiento en la Pi: Uber Tristram junta tres jefes con muchos esbirros y
efectos a la vez, así que hay que medirlo ahí como las otras peleas grandes
(`D2_AT_PERF`).

## Fuentes de las guías

- [Icy Veins: los Ancianos colosales](https://www.icy-veins.com/d2/news/diablo-2-resurrected-colossal-ancients-push-builds-to-their-limit/)
- [Icy Veins: cambios del endgame de la expansión del Brujo](https://www.icy-veins.com/d2/reign-of-the-warlock-endgame-changes-for-diablo-ii-resurrected)
- [Maxroll: la expansión del Brujo](https://maxroll.gg/d2/news/diablo-ii-resurrected-reign-of-the-warlock-expansion)
- [DiabloBytes: guía de los Ancianos colosales](https://diablobytes.com/d2-resurrected/guides/colossal-ancients/)
- [d2emu: endgame de la expansión del Brujo](https://d2emu.com/guides/reign-of-the-warlock-endgame)
- [diablo2.io: Colossal Summit](https://diablo2.io/areas/colossal-summit-t1674854.html)
- [Diablo Wiki: Über Diablo](https://diablo.fandom.com/wiki/%C3%9Cber_Diablo)
- [PureDiablo: Uber Diablo](https://www.purediablo.com/d2wiki/Uber_Diablo_Quest)
