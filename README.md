# Diablo II for Raspberry Pi (Batocera) fan-made

A Diablo II–style engine **written from scratch** in C99 + SDL2, to play on
**Batocera on a Raspberry Pi 3B** with a gamepad, alone or two players on
the same screen. It uses the graphics, maps, tables, sounds and voices of
**your own copy of Diablo II: Resurrected**, which still ships the original
game's 2D files.

> **This repository does not contain any Blizzard file.** An importer copies
> from your Diablo II: Resurrected installation only the files the port
> uses.

> **This is a work in progress.** Acts 1, 2 and 3 are fully playable, but
> **there are still bugs to fix** (see
> [Known bugs and what is missing](#known-bugs-and-what-is-missing)). If you
> find one, please open an issue.

**Contents**

1. [Installing on Batocera](#installing-on-batocera)
2. [What is implemented](#what-is-implemented)
3. [What was written from scratch and what was remade](#what-was-written-from-scratch-and-what-was-remade)
4. [Known bugs and what is missing](#known-bugs-and-what-is-missing)
5. [Using the source code: build and play on a PC](#using-the-source-code-build-and-play-on-a-pc)
6. [Controls](#controls)
7. [Credits and legal notice](#credits-and-legal-notice) (MIT license)

---

## Installing on Batocera

For now: **Linux and Batocera**. A Windows importer will come later.

### Requirements

- A Raspberry Pi 3B or newer with **64-bit Batocera, v33 or newer**.
- **Diablo II: Resurrected** installed with Battle.net. On Linux it is
  installed with Wine, Lutris, Bottles or Steam (Proton); the game folder
  copied from a Windows PC works too.
- 1.7 GB free on the Pi (the port plus the imported files).

**The whole game is not copied:** only the files the port uses (about
30,000 of them, 1.6 GB of Resurrected's roughly 40 GB), the ones listed in
`archivos.txt`. Your installation is not modified: it is only read.

### 1. Download the port

From the [**Releases**](https://github.com/rodrireddev/d2port/releases)
page, download `diablo2-port.zip` and unzip it. Inside:

| Folder | What it is |
|---|---|
| `batocera/ports/` | the port: it goes in Batocera's ports folder |
| `importador/linux/` | the importer for Linux (PC) |

`batocera/ports/` has a hidden folder, `.data/`, with the game: always copy
it together with the rest (in the file manager, Ctrl+H shows hidden files).

### 2. Import the game files

#### From a Linux PC

1. Close Diablo II: Resurrected.
2. Run the importer:

   ```bash
   cd diablo2-port/importador/linux
   ./d2port-importar
   ```

3. Choose the language (1 = Spanish, 2 = English). It looks for the game in
   the usual places (`~/.wine`, Lutris' `~/Games/*`, Bottles and Steam
   prefixes): if it cannot find it, it asks for the folder (you can drag it
   to the terminal). It copies the files to the same package's
   `batocera/ports`, in under a minute.
4. Copy the contents of `batocera/ports` to Batocera's ports folder,
   `/userdata/roms/ports`. For example, over the network:

   ```bash
   scp -r batocera/ports/. root@batocera.local:/userdata/roms/ports/
   ```

   (it asks for Batocera's root password), or from the file manager at
   `smb://batocera/share/roms/ports`.

You can also import straight to the Pi, if you already copied the port and
have Batocera's share mounted:

```bash
./d2port-importar --idioma en --destino /path/to/share/roms/ports
```

All the options: `--idioma es|en` (language), `--juego <game folder>`,
`--destino <ports folder>` and `--sin-pausa` (do not wait for Enter at the
end).

#### On the Pi itself (coming soon)

The next release will also bring the importer for the Pi: you copy the
whole Diablo II Resurrected folder to a USB drive, plug it into the Pi and,
in the **Ports** menu, choose **Import Diablo II (English)** or **Importar
Diablo II (Español)**. It is already written but not tested on a Pi yet.

### 3. Play

In Batocera's **Ports** menu, **Diablo II** (update the game lists if it
does not show up). It is played with a gamepad, alone or two players: the
first gamepad used is player 1's. See [Controls](#controls).

### Language

It is chosen when importing: **English** or **Spanish** (with Latin
American Spanish voices). To change it, import again with the other
language: the voices are replaced and the language is written to
`.data/d2port/idioma.txt`.

### The Warlock patch (optional)

There is an optional **personal skin for the Warlock** (it changes how his
head, torso, arms, legs and shoulders look), distributed separately from
this repository. Without it, the Warlock looks as in Resurrected. It is
injected after importing, with its `instalar.sh`:

```bash
./instalar.sh                                # on the Pi, over SSH: uses /userdata/roms/ports
./instalar.sh /path/to/share/roms/ports      # from Linux, with the share mounted
./instalar.sh --quitar /userdata/roms/ports  # to remove it
```

### Saved games, updating and uninstalling

- **Saved games** are kept in `/userdata/saves/ports/d2port`: updating the
  port or importing again does not touch them.
- **Updating:** copy the new version's `batocera/ports` over the old one.
  If the new version uses game files that were not needed before, import
  again.
- **Uninstalling:** from `/userdata/roms/ports` delete `Diablo II.sh`, the
  import entries and the `.data/d2port` folder.

### Troubleshooting

- **The game does not start:** look at `/userdata/system/logs/d2port.log`.
  If it says "no encontré data/global/palette/act1", the game files are
  missing: import them (step 2).
- **The importer says files are missing:** the list is in
  `.data/d2port/importar.log`. It is usually an unfinished game install:
  open Battle.net, let it finish installing or updating, close it and
  import again.
- **"Could not open the game files":** the game is running, the folder is
  not the Diablo II Resurrected one (it must have `.build.info` and the
  `Data` folder), or the copy is incomplete.

---

## What is implemented

**Index**

- [The acts](#the-acts)
- [Classes and skills](#classes-and-skills)
- [Monsters](#monsters)
- [Combat](#combat)
- [Items](#items)
- [The Horadric Cube](#the-horadric-cube)
- [NPCs, shops and mercenaries](#npcs-shops-and-mercenaries)
- [The world](#the-world)
- [The interface](#the-interface)
- [Two players](#two-players)
- [Sound, music and voices](#sound-music-and-voices)
- [Languages](#languages)
- [Saved games](#saved-games)
- [Demo and automated tests](#demo-and-automated-tests)
- [Performance on the Raspberry Pi](#performance-on-the-raspberry-pi)

### The acts

The first three acts, complete, on Normal difficulty.

- **Act 1:** the Rogue Encampment, the Blood Moor, the Cold Plains, the
  Stony Field, the Dark Wood, the Black Marsh, the Tamoe Highland, the
  Monastery, the Jail, the Cathedral and the Catacombs, with their caves
  and cellars, Tristram and the Forgotten Tower. The six quests: Den of
  Evil, Sisters' Burial Grounds (Blood Raven), The Search for Cain (the
  Inifuss tree, the Cairn Stones and Tristram), The Forgotten Tower (the
  Countess), Tools of the Trade (the Horadric Malus and Charsi) and Sisters
  to the Slaughter (Andariel).
- **Act 2:** Lut Gholein, the Sewers, the desert (Rocky Waste, Dry Hills,
  Far Oasis, Lost City, Valley of Snakes and the Canyon of the Magi), the
  Halls of the Dead, the caves, the Claw Viper Temple, the Palace, the
  Arcane Sanctuary and the seven tombs. The six quests: Radament's Lair,
  The Horadric Staff, The Tainted Sun, The Arcane Sanctuary, The Summoner
  and The Seven Tombs (Duriel and Tyrael).
- **Act 3:** the Kurast Docks (Meshif sails there and back), the jungles
  (Spider Forest, Great Marsh and Flayer Jungle), Kurast (Lower Kurast,
  the Bazaar and Upper Kurast), the Causeway, Travincal, the sewers, the
  six temples and the Durance of Hate. The six quests: The Golden Bird,
  Blade of the Old Religion (the Gidbinn), Khalim's Will, Lam Esen's Tome,
  The Blackened Temple (the High Council) and The Guardian (Mephisto).
- The original waypoints of each act, with a tab per act, and the travel
  between acts (Warriv, Meshif).
- Quests follow the original logic (D2MOO's `QUESTS`): their texts, voices,
  rewards and details, like the Gidbinn altar decoy or the jade figurine
  carried by the first boss of the act.
- A quest log with the state of each quest (in the panels and in the pause
  menu).

### Classes and skills

- **The seven original classes** (Barbarian, Amazon, Sorceress, Paladin,
  Necromancer, Assassin and Druid) and **Resurrected's Warlock**.
- **All their skills**, with the values of `skills.txt` and
  `skilldesc.txt`, their synergies, animations and effects, and a
  description of what each level does: Paladin auras, Necromancer curses
  and minions (skeletons, mages, golems, revives), Druid wolf and bear
  forms, Assassin traps, charges and shadows, Warlock demons and sigils,
  and all the rest.
- Attributes (5 points per level), skill points, experience and levels from
  `experience.txt`, stamina as in the original.
- The original skill tree, and a **Buttons** page to choose what each
  gamepad button does.

### Monsters

- The monsters of each level as in `levels.txt`, with their graphics,
  animations and sounds.
- **The original AI** (from D2MOO) for the monsters of the three acts:
  skeletons, zombies, ravens, fetishes, spiders, vampires, the High
  Council, the river tentacles (they submerge and come out) and the act
  bosses (Andariel, Duriel, Mephisto…).
- **Random bosses** as in the original: `MonUMin` to `MonUMax` per level;
  one in five is a pack of **champions** (champion, ghostly, fanatic,
  possessed or berserker) and the rest are **uniques** with an escort (3 to
  6) and their modifiers.
- The named bosses (Corpsefire, Bishibosh, Rakanishu, the Countess,
  Radament, the Summoner…) and the original's special boss deaths.

### Combat

- Melee and ranged attacks with the original formulas: attack rating
  against defense, block, critical strikes, elemental damage (fire, cold,
  lightning, poison and magic), resistances, life and mana steal.
- Missiles from `missiles.txt` (skills' and monsters'), with their
  graphics, sounds and area damage.
- States: poison, frozen, stunned, curses and auras.
- Death and resurrection in town.

### Items

- **The original loot:** treasure classes, magic find and each monster's
  level.
- **All qualities:** normal, low quality, superior, magic (blue), rare
  (yellow), unique (gold), set (green, with their partial set bonuses) and
  runewords.
- **Sockets:** gems, runes and jewels; runes in the right order make a
  runeword.
- Rings, amulets, charms, arrows and bolts, throwing weapons, potions
  (healing, mana, rejuvenation, stamina, antidote, thawing) and scrolls.
- Strength, dexterity, level and class requirements; durability; the fixed
  properties of class items.
- Inventory, equipment (two hands, shield and quiver) and belt as in the
  original, handled with the gamepad. The equipment shows on the character.

### The Horadric Cube

- It comes from the chest on the third level of the Halls of the Dead, as
  in the original.
- The `cubemain.txt` recipes: gems, runes, potions, sockets, upgrades,
  magic items and crafting.
- A **cube guide** in the pause menu, with the recipes by category and what
  each one gives (runewords included).

### NPCs, shops and mercenaries

- The NPCs of the three towns, with their dialogs and voices in the
  original order.
- **Shops** (buy and sell), **gambling** (Gheed, Elzix, Alkor), Akara and
  Fara heal, **Charsi imbues** (makes an item rare) and Cain.
- **Mercenaries:** Kashya's rogues (Act 1), Greiz's desert warriors (Act 2)
  and Asheara's Iron Wolves (Act 3), with their skills and auras; you can
  give them equipment and resurrect them.

### The world

- The original maps (`.ds1`), and the mazes and outdoor areas generated at
  random like the original does (D2MOO's `DRLG`).
- **No loading screens between areas:** each act is a single map, and areas
  are built and released one by one as you walk.
- The original collision (the 16-bit masks of each tile) and 8-direction
  pathing.
- The original lighting, day and night in the towns, and the roof of the
  building you are in is not drawn (as in the original).
- **Automap** with the explored areas, the players, the mercenary, the
  waypoints and the exits.

### The interface

- The original interface (life and mana orbs, experience bar, belt,
  monster name and life), at 800x600 like the original.
- Inventory, skills, buttons, character, mercenary and quest panels,
  designed for the gamepad (Select opens, Y goes to the next one).
- A does the right thing next to something: talk, pick up, use stairs, a
  portal or a quest object.
- A pause menu with the volume, "always run", the cube guide, the quests
  and save and exit.
- The original chat phrases (Help!, Follow me, Thanks…) from a menu.

### Two players

- Two players on the same screen, each with a gamepad (or the keyboard).
- A single camera while they are close; when they move apart, the screen
  splits along a line perpendicular to the one joining them, and joins
  again when they get together.
- Quests, bosses and experience count for both.

### Sound, music and voices

- The original sounds (`sounds.txt`, `monsounds.txt`), the music of each
  area and the ambient effects.
- The NPC voices and the class lines, in **English** or **Latin American
  Spanish**.

### Languages

- **English** or **Spanish** (Latin American), chosen when importing: the
  game texts come from Resurrected in that language, and the engine's own
  texts (menus, help, descriptions) are translated.
- Magic, rare and champion names are built in each language's word order,
  with the original's name formats.

### Saved games

- Six saved games, with the character, equipment, gold, mercenary, quests,
  waypoints and act.

### Demo and automated tests

- **Demo:** if the main menu is left alone for 10 seconds, two bots create
  characters and play by themselves.
- **Automated tests** (`--autotest act1`, `act2`, `act3`): two bots play
  each act from start to end, every quest step, with 1 and 2 players.
  Today all three pass (68, 63 and 74 steps), in English and in Spanish, as
  does the skills test (24 of 24).

### Performance on the Raspberry Pi

- The logic runs at 25 ticks per second like the original, with
  interpolation when drawing.
- On a Pi 3B: the logic takes 0.8 to 1.2 ms per tick and drawing 8 to 11 ms
  per frame. Areas are prepared on a worker thread so walking does not
  stall.
- A whole act fits in the Pi's 1 GB of memory.

---

## What was written from scratch and what was remade

### Written from scratch

**All the code is new**, in C99 + SDL2: it does not use any Blizzard code.
The engine reads the game's formats (palettes, DC6, DCC, COF, DT1, DS1,
`animdata.d2`, the `.txt` tables, Resurrected's CASC storage) and does
everything else: 8-bit palette rendering with light tables, map
generation, collision, AI, combat, items, interface, sound, saving and the
bots.

### What comes from the original

From your copy of Resurrected: the 2D graphics, the maps, the game tables,
the sounds, the music, the texts and the voices.

### Remade to be faithful to the original

Several parts were remade following **D2MOO** (the reconstruction of the
original game's code) so that they behave the same:

- Random map generation (mazes, the desert, the jungles and Kurast).
- Collision, copying the 16-bit masks of each tile.
- The AI of the monsters of the three acts.
- How bosses, champions and their escorts spawn.
- The quests, with each of their steps.
- Loot, gambling and item generation.
- The drawing order of walls, roofs and characters.

### Remade for the Pi and the gamepad (different from the original)

- **Resurrected's HD graphics: discarded.** In Resurrected the world is 3D
  (models, textures and skeletons, about 30 GB): it does not fit in a
  Pi 3B. The port uses the original 2D graphics.
- **A gamepad interface:** the Buttons page, A does the right thing, the
  panels are cycled with Select and Y, the inventory is handled with the
  d-pad, the cube guide, the chat phrases from a menu.
- **Two players with a split screen**, which the original does not have.
- **No loading screens** between the areas of an act.
- **Deliberate gameplay changes**, to play comfortably with a gamepad:
  - walking is 10% and running 20% faster;
  - belt potions have 3 sips and drop 35% less often;
  - when a belt potion runs out, one from the backpack takes its place;
  - each belt slot is used with the d-pad;
  - RB drops the item under the inventory cursor to the ground and LB
    sorts the backpack;
  - an "always run" option;
  - the life and mana numbers above the orbs;
  - the mercenary follows closer and at your pace.
- **The Warlock** is Resurrected's new class; his personal skin is optional
  (see [the patch](#the-warlock-patch-optional)).

---

## Known bugs and what is missing

**There are still bugs to fix.** The automated tests pass, but not many
people have played the game yet: there are surely more bugs than the ones
listed here. If you find one, please open an issue with what happened and,
if you can, the `d2port.log`.

**Known bugs:**

- On a Pi 3B there can be a stutter when entering the biggest areas of
  Act 3 (Spider Forest, Great Marsh): building them takes almost 100 ms.
- The cube guide still says Cain gives you the cube: it comes from the
  Halls of the Dead chest, as in the original.
- The cold Iron Wolves do not cast Chilling Armor.
- Some character lines are silent in Spanish: Resurrected does not ship
  that audio in Latin American Spanish.
- Many engine messages and the developer notes are written in Spanish
  first; the English translation of the engine texts may still miss some.

**What is missing:**

- **Acts 4 and 5** (after Mephisto's portal, the game says Act 4 is not
  there yet).
- **Nightmare and Hell** difficulties (only Normal).
- **Chests, urns, barrels, shrines and wells** you can use, and the random
  objects of each level (today only the quest objects are there); locked
  chests.
- In Act 2, the desert shrines and the secondary border of some areas.
- The **importer for the Pi** (written, not tested yet) and for **Windows**.
- Network play: not planned (the two players share one screen).

---

## Using the source code: build and play on a PC

The same importer injects the game files into the source tree's `data/`
folder, which git ignores. It only copies the files listed in
`tools/archivos-del-juego.txt`, not the whole game.

```bash
# Linux (Ubuntu/Debian): the compiler, SDL2 and zlib
sudo apt install build-essential cmake git libsdl2-dev zlib1g-dev
# CascLib, to read Resurrected's files
git clone https://github.com/ladislav-zezula/CascLib ~/src/CascLib
cmake -S ~/src/CascLib -B ~/src/CascLib/build -DCMAKE_BUILD_TYPE=Release \
      -DCASC_BUILD_SHARED_LIB=OFF -DCASC_BUILD_STATIC_LIB=ON
cmake --build ~/src/CascLib/build -j
# The port and the importer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/d2port-importar      # choose the language; it finds the game and copies to data/
./build/d2port
```

The importer looks for Resurrected by itself (in Wine, Lutris, Bottles or
Steam prefixes; under WSL, the Windows one) and asks if it cannot find it.
The importer from the release package works too, with
`--destino <source folder>`. The Warlock skin is added with its patch's
`instalar.sh`, pointing at the same folder.

**For the Pi** (from Linux or WSL, with the cross compiler;
`scripts/setup-wsl.sh` installs it):

```bash
cmake -S . -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake --build build-pi -j
bash scripts/package-batocera.sh     # dist/ports, with the game files (for your own use)
bash scripts/build-importador.sh     # the importer for Linux and the Pi
bash scripts/package-release.sh      # publicar/: the package without Blizzard files
```

How each part works, the tools, the tests and the debug variables are in
the [developer notes](docs/desarrollo.md) (in Spanish, like the code
comments). The plans of each act are in
[docs/plan-acto2.md](docs/plan-acto2.md) and
[docs/plan-acto3.md](docs/plan-acto3.md); the measurements on the Pi, in
[docs/rendimiento.md](docs/rendimiento.md).

---

## Controls

| Action | Player 1 | Player 2 |
|---|---|---|
| Move (walking) | Left stick, WASD or holding the left mouse button | Left stick or arrow keys |
| Run (hold; with "Always run" on, it walks) | RT / R2, or left Shift | RT / R2, or right Shift |
| A button: attack or skill, pick up an item, talk | A or Space | A or Enter |
| B, X and Y buttons: skills (or Throw) | B, X, Y or E, R, F | B, X, Y or J, L, O |
| Drink the potion of a belt slot (each slot shows its arrow) | D-pad ◀ ▲ ▶ ▼, or 1 to 4 | D-pad, or 7 to 0 |
| Healing / mana potion (the first one in the belt) | LB / LT | LB / LT |
| Stamina potion | Press the left stick (L3) | L3 |
| Automap | RB / R1, or Tab | RB / R1 |
| Phrases (Help!, Follow me, Thanks…): open the list / choose / say / close | Press the right stick (R3) or C / d-pad ▲ ▼ / A / B (you can keep walking) | R3 or N / same |
| Inventory | Select or I | Select or P |
| Character (spend attribute points) | The red cross at the bottom left of the inventory, or Y twice in the inventory | Same |
| Panels: inventory, skills, buttons, character, mercenary, quests | Select opens; Y goes to the next one (or T to the skill tree) | Select opens; Y goes to the next one (or U) |
| In the skill tree: learn / put on a button | A / X opens "Put on:" (the d-pad chooses the button, A assigns, B cancels) | Same |
| Buttons page: change what a button does | The d-pad chooses the button, A shows what it can have, A assigns it | Same |
| Talk to an NPC | A next to them (B closes) | A next to them (B closes) |
| Use stairs or a portal | A next to it (if no monster is right next to you) | A next to it |
| Inventory: pick up / drop / use | A / X or A / X | A / X or A / X |
| Inventory: drop what you hold / put it away / sort | Y / B / LB | Y / B / LB |
| Socket a gem, rune or jewel | Pick it up with A and drop it with A on the item | Same |
| Horadric Cube: open / move items / transmute / close | X on the cube / X / A or X on Transmute / B | Same |
| Mercenary: give an item from the backpack / take it off | A (and A in the list) / X | A (and A in the list) / X |
| Character: add a point to the selected attribute | A | A |
| Pause (resume, volume, always run, cube guide, save and exit) | Start or Esc | Start |
| Cube guide: category / recipe / back | Left and right (or LB and RB) / up and down / B | Same |
| In the pause menu: change the volume or "Always run" | Left / right | Left / right |
| Quit the game | Select + Start | |

With a single player, any controller moves the character. The first
gamepad used is player 1's and the second one is player 2's (the order in
which they were connected does not matter).

---

## Credits and legal notice

- **License:** the port's code and documentation are free software under
  the [MIT license](LICENSE).
- **Diablo, Diablo II and Diablo II: Resurrected** are trademarks of
  Blizzard Entertainment. This project is not affiliated with Blizzard. The
  port does not include any game file: the importer copies them from your
  own installation, for your personal use. Do not share them.
- [**D2MOO**](https://github.com/ThePhrozenKeep/D2MOO), the reconstruction
  of the original game's code, used to keep the logic faithful (MIT; some
  tables come from it, see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)).
- [**CascLib**](https://github.com/ladislav-zezula/CascLib), to read
  Resurrected's files (MIT; linked into the importer).
- [**SDL2**](https://www.libsdl.org/), for the window, sound and gamepads
  (zlib license).
