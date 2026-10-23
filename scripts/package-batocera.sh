#!/bin/bash
# Arma dist/ports/ para copiar a \\BATOCERA\share\roms\ports en la Pi.
# Antes: cmake -S . -B build-pi -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
#        cmake --build build-pi -j
# OUT=carpeta: otra salida (en vez de dist/ports). SIN_DATOS=1: sin los archivos
# del juego (el paquete público: los pone cada uno con el importador, que lee
# la lista de .data/d2port/archivos.txt)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${OUT:-$root/dist/ports}"
game="$out/.data/d2port"

[ -n "$out" ] && [ "$out" != "/" ] || exit 1
rm -rf "$out"
mkdir -p "$game"
cp "$root/build-pi/d2port" "$game/"

# Solo los archivos del juego que usa el motor por ahora
assets=(
    data/global/animdata.d2
    data/global/excel/armor.txt
    data/global/excel/charstats.txt
    data/global/excel/experience.txt
    data/global/excel/hireling.txt
    data/global/excel/itemtypes.txt
    data/global/excel/levels.txt
    data/global/excel/lvlwarp.txt
    data/global/excel/lvlprest.txt
    data/global/excel/lvlsub.txt
    data/global/excel/lvltypes.txt
    data/global/excel/rareprefix.txt
    data/global/excel/raresuffix.txt
    data/global/excel/magicprefix.txt
    data/global/excel/magicsuffix.txt
    data/global/excel/automagic.txt
    data/global/excel/monpet.txt
    data/global/excel/gamble.txt
    data/global/excel/difficultylevels.txt
    data/global/excel/monumod.txt
    data/global/excel/monseq.txt
    data/global/excel/itemstatcost.txt
    data/global/excel/properties.txt
    data/global/excel/uniqueitems.txt
    data/global/excel/setitems.txt
    data/global/excel/sets.txt
    data/global/excel/runes.txt
    data/global/excel/gems.txt
    data/global/excel/itemratio.txt
    data/global/excel/cubemain.txt
    data/global/excel/misc.txt
    data/global/excel/missiles.txt
    data/global/excel/overlay.txt
    data/global/overlays
    data/global/excel/monsounds.txt
    data/global/excel/skills.txt
    data/global/excel/sounds.txt
    data/global/excel/soundenviron.txt
    data/global/excel/skilldesc.txt
    data/global/ui/spells
    data/global/ui/panel
    data/global/ui/cursor
    data/global/ui/charselect
    data/global/ui/frontend/gameselectscreenexp.dc6
    data/global/ui/frontend/widebuttonblank.dc6
    data/global/ui/frontend/cinematicsselectionexp.dc6
    data/global/ui/loading/loadingscreen_blank.dc6
    data/global/ui/frontend/d2logoblackleft.dc6
    data/global/ui/frontend/d2logoblackright.dc6
    data/global/ui/frontend/d2logofireleft.dc6
    data/global/ui/frontend/d2logofireright.dc6
    data/global/ui/menu/waygatebackground.dc6
    data/global/ui/menu/waygateicons.dc6
    data/global/ui/menu/waygatetabs.dc6
    data/global/ui/menu/questbackground.dc6
    data/global/ui/menu/expquesttabs.dc6
    data/global/ui/menu/questsockets.dc6
    data/global/ui/menu/a1q1.dc6
    data/global/ui/menu/a1q2.dc6
    data/global/ui/menu/a1q3.dc6
    data/global/ui/menu/a1q4.dc6
    data/global/ui/menu/a1q5.dc6
    data/global/ui/menu/a1q6.dc6
    data/global/ui/menu/a2q1.dc6
    data/global/ui/menu/a2q2.dc6
    data/global/ui/menu/a2q3.dc6
    data/global/ui/menu/a2q4.dc6
    data/global/ui/menu/a2q5.dc6
    data/global/ui/menu/a2q6.dc6
    data/global/ui/menu/a3q1.dc6
    data/global/ui/menu/a3q2.dc6
    data/global/ui/menu/a3q3.dc6
    data/global/ui/menu/a3q4.dc6
    data/global/ui/menu/a3q5.dc6
    data/global/ui/menu/a3q6.dc6
    data/global/ui/menu/a4q1.dc6
    data/global/ui/menu/a4q2.dc6
    data/global/ui/menu/a4q3.dc6
    data/global/ui/menu/a5q1.dc6
    data/global/ui/menu/a5q2.dc6
    data/global/ui/menu/a5q3.dc6
    data/global/ui/menu/a5q4.dc6
    data/global/ui/menu/a5q5.dc6
    data/global/ui/menu/a5q6.dc6
    data/global/ui/hireables/rogueicon.dc6
    data/global/ui/hireables/act2hireableicon.dc6
    data/global/ui/hireables/act3hireableicon.dc6
    data/global/ui/hireables/barbhirable_icon.dc6
    data/global/excel/superuniques.txt
    data/global/excel/treasureclassex.txt
    data/global/excel/weapons.txt
    data/global/items
    data/local/font/latin
    data/global/excel/monlvl.txt
    data/global/excel/monequip.txt
    data/global/excel/monpreset.txt
    data/global/excel/monstats.txt
    data/global/excel/monstats2.txt
    data/global/excel/objects.txt
    data/global/excel/objpreset.txt
    data/global/excel/objgroup.txt
    data/global/excel/shrines.txt
    data/global/palette
    data/global/tiles/act1
    data/global/tiles/act2
    data/global/tiles/act3
    data/global/tiles/act4
    data/global/tiles/expansion
    data/global/chars/ba
    data/global/chars/am
    data/global/chars/so
    data/global/chars/pa
    data/global/chars/ne
    data/global/chars/ai
    data/global/chars/dz
    data/global/chars/wk
    data/global/objects
)
# Proyectiles: todos (son 20 MB y los usan los monstruos de todo el acto)
assets+=(data/global/missiles)
# NPCs y animales del campamento, y los monstruos que pueden salir en el Acto 1
# (tools/act1monsters.py los saca de levels.txt y superuniques.txt)
mapfile -t act1_tokens < <(python3 "$root/tools/act1monsters.py" "$root" tokens)
# (y la Hidra de la Hechicera: monsters/hx; los esqueletos y magos del
# Nigromante son monsters/sk, los gólems g1..g4 y los muros de hueso bw; las
# trampas de la Asesina lg y e9, su centinela de cuchillas b8 y el clon k9;
# el Druida: el lobo 40 y el oso tg de sus formas, el cuervo hk, los lobos
# wf, el oso pardo b7, los espíritus x3 x4 xw y las enredaderas, también k9)
# el Brujo: el hombre cabra GM, el mancillado BH y el profanador 45
# y los del Acto 2 (Lut Gholein, sus niveles y sus jefes)
mapfile -t act2_tokens < <(python3 "$root/tools/act1monsters.py" "$root" tokens 2)
mapfile -t act3_tokens < <(python3 "$root/tools/act1monsters.py" "$root" tokens 3) # y los del Acto 3
mapfile -t act4_tokens < <(python3 "$root/tools/act1monsters.py" "$root" tokens 4) # y los del Acto 4
mapfile -t act5_tokens < <(python3 "$root/tools/act1monsters.py" "$root" tokens 5) # y los del Acto 5
# (y la cimitarra voladora st: sale de los barriles y ataúdes de los Actos 2 y 3)
for m in ps gh rc ci wa ck cw hx sk g1 g2 g3 g4 bw lg e9 b8 k9 40 tg hk wf b7 x3 x4 xw gm bh 45 st "${act1_tokens[@]}" \
         "${act2_tokens[@]}" "${act3_tokens[@]}" "${act4_tokens[@]}" "${act5_tokens[@]}"; do
    assets+=("data/global/monsters/$m")
done
# Sonidos y música: solo los que usa el motor (tools/soundfiles.py los saca de
# las tablas): objetos, habilidades de las clases, monstruos del Acto 1,
# proyectiles, el ambiente y la música del pueblo, el campo y la cueva, y los
# de la interfaz, los pasos y las armas
sound_specs=(
    items class:ama class:bar class:sor class:pal class:nec class:ass class:dru
    missile:raven1 missile:raven2 music_quest_bloodraven object_waypoint_open
    vo:Akara vo:Kashya vo:Charsi vo:Gheed vo:Warriv vo:Cain vokey:A1Q5InitQuestTome
    vo:Atma vo:Drognan vo:Elzix vo:Fara vo:Geglash vo:Griez vo:Jerhyn vo:Lysander vo:Meshif vo:WarrivAct2 vo:CainAct2 vo:PalaceGuard
    vo:Ormus vo:Alkor vo:Asheara vo:Hratli vo:Natalya vo:MeshifAct3 vo:CainAct3
    env:23 env:24 env:25 env:26 env:27 env:28 env:29 env:30 mon:act3hire ironwolf_command_1
    object_sewer_lever object_gidbinn_decoy object_stairs object_bridge_mephisto object_door_hellgate_open
    player_townportal_cast player_townportal_enter object_townportal
    compellingorb_death item_potion_drink
    missile:monsterlight missile:mephisto missile:chargedstrikebolt missile:primepoisoncloud missile:monbliz1
    missile:monbliz2 missile:monbliz3 missile:monbliz4 missile:vampiremeteor missile:vampiremeteorfire
    missile:vampirefirewall missile:firehead missile:fetishinferno1 missile:frogfire missile:frogpois missile:blowgun
    missile:willowisplightningbolt missile:highpriestlightning missile:spidergoolay missile:tentaclegoo
    env:12 env:13 env:14 env:15 env:16 env:17 env:18 env:19 env:20 env:21 env:22
    music_quest_andariel music_quest_forge music_quest_compelling
    cairn_stone_1 cairn_stone_2 cairn_stone_3 cairn_stone_4 cairn_stone_5 cairn_success
    object_gibbet_drop object_malus object_tome item_book object_chest_large object_taintedsunaltar
    missile:skmage3 missile:skmage4 missile:bighead1 missile:bighead2 missile:bighead4 missile:vampirefireball
    missile:andarielspray missile:cr_arrow1 missile:skbowarrow1 missile:spike2 missile:spike3 missile:spike4
    mon:roguehire rogue_confirm_a_1 rogue_mortal_danger hireable_female_thank_you missile:coldarrow missile:firearrow
    mon:act2hire hireable_male_thanks_1
    missile:buglightning missile:pantherjav1 missile:goospit1 missile:mummy1 missile:mummy2 missile:unholybolt1
    missile:arrow missile:bolt missile:javelin missile:throwaxe missile:throwknife missile:spike1
    missile:shafire1 missile:shafire2 missile:shafire3 missile:shafire4 missile:shafire5
    missile:poisonjavcloud missile:plaguejavcloud
    env:1 env:2 env:3 env:4 env:5 env:6 env:7 env:8 env:9 env:10 event_wild_day_1 music_options music_quest_den
    barbarian_hit_1 barbarian_death_1 amazon_hit_1 amazon_death_1 barbarian_findheart_1
    cursor_button_click cursor_level_up cursor_point_drop cursor_select cursor_questdone cursor_pass
    cursor_barbarian_select cursor_amazon_select item_pickup item_gold cursor_convert_item item_gem
    impact_poison_1 necromancer_taint barbarian_battlecry_state
    sorceress_hit_1 sorceress_death_1 cursor_sorceress_select
    missile:firewall missile:meteor missile:meteorfire missile:meteorexplode missile:frozenorbbolt
    missile:frozenorbnova missile:frozenorbexplode missile:blizzard1 missile:blizzard2 missile:blizzard3
    missile:blizzard4 missile:blizzardcenter missile:infernoflame2 missile:hydra missile:chillingarmorbolt
    missile:thunderstorm1 missile:teleport
    paladin_hit_1 paladin_death_1 cursor_paladin_select paladin_redeemed_soul paladin_thorns_hit paladin_conversion
    paladin_holybolt_impact_1 paladin_handofgod_bolt block_weapon_1 block_arrow_1
    missile:fistoftheheavensbolt missile:handofgod missile:redemption missile:iceexplode missile:fireexplode
    missile:lightninghit missile:blood1
    necromancer_hit_1 necromancer_death_1 cursor_necromancer_select necromancer_corpseexp_1
    necromancer_amplifydamage necromancer_dimvision necromancer_weaken necromancer_ironmaiden necromancer_taint
    necromancer_confuse necromancer_reversevampire necromancer_attract necromancer_decrepify necromancer_bonearmor
    missile:teeth missile:bonespear missile:bonespirit missile:poisonnova missile:corpseexplosion
    missile:poisoncorpseexplosion missile:poisonexplosioncloud missile:revivemedium
    missile:necromage1 missile:necromage2 missile:necromage3 missile:necromage4
    mon:necroskeleton mon:necromage mon:claygolem mon:bloodgolem mon:irongolem mon:firegolem mon:bonewall
    assassin_hit_1 assassin_death_1 cursor_assassin_select assassin_cloakofdarkness assassin_chargeup_tiger_1
    assassin_chargeup_cobra_1 mon:chargeboltsentry mon:lightningsentry mon:wakeofdestruction mon:infernosentry
    mon:deathsentry mon:bladecreeper mon:shadowwarrior mon:shadowmaster
    druid_hit_1 druid_death_1 cursor_druid_select druid_morph druid_hurricane
    missile:firestorm missile:firestormmaker missile:moltenboulder missile:moltenboulderemerge
    missile:moltenboulderfirepath "missile:erruption crack 1" missile:volcano "missile:volcano debris 2"
    missile:armageddonrock missile:armageddonexplosion missile:hurricaneswoosh missile:twister missile:tornado
    missile:shockwave missile:arcticblast1 missile:arcticblast2
    mon:druidhawk mon:spiritwolf mon:fenris mon:druidbear mon:oaksage mon:heartofwolverine mon:spiritofbarbs
    mon:plaguepoppy mon:cycleoflife mon:vinecreature
    class:war warlock_hit_1 warlock_death_1 mon:wargoatman mon:warbighead mon:warputriddefiler
    # los cofres, barriles, santuarios y pozos (src/objects.inc)
    object_chest_small object_casket object_bed object_grave object_barrel_explode object_rockpile object_stone_small
    object_wood_break_1 object_wood_break_2 object_wood_break_3 object_wood_break_4 object_wood_break_5
    object_urn_break_1 object_urn_break_2 object_urn_break_3 object_urn_break_4 object_urn_break_5
    object_basket_1 object_basket_2 object_basket_3 object_basket_4 object_basket_5
    object_glass_break_1 object_glass_break_2 object_glass_break_3 object_cocoon_1 object_cocoon_2 object_cocoon_3
    object_ratnest_1 object_ratnest_2 object_ratnest_3 object_goopile_1 object_goopile_2 object_goopile_3
    object_skullpile_1 object_skullpile_2 object_skullpile_3 object_skullpile_4 object_skullpile_5
    object_stash_jungle_1 object_stash_jungle_2 object_stash_jungle_3 object_stash_jungle_4
    object_stash_mephisto_1 object_stash_mephisto_2 object_armorstand object_weaponrack object_bookshelf object_well
    object_corpse_loot object_trap_trigger object_fire_loop_brazier item_key_used
    shrine_refill shrine_recharge shrine_exchange shrine_armorboost shrine_combatboost shrine_resistfire
    shrine_resistcold shrine_resistlightning shrine_resistpoison shrine_skill shrine_experience shrine_ofenirhs
    shrine_portal shrine_gemupgrade shrine_storm shrine_warping shrine_exploding shrine_poison
    missile:trapfirebolt missile:trapnova missile:chainlightning missile:fireball
    missile:miasmaboltcloud missile:miasmachainscloud missile:miasmapuff missile:flamewavelingerfire missile:flamewave
    missile:bladewarpexplode missile:ringoffireexplode missile:abyssexplode missile:hexpurgeexplode
    missile:bloodboilexplode missile:engorgecorpseeffect missile:taintedfireball missile:echoingstrike_detonate
    missile:sigillethargymedium missile:sigillethargylarge missile:sigilrancormedium missile:sigilrancorlarge
    missile:sigildeathmedium missile:sigildeathlarge
    # el Acto 4: sus NPC, la música y el ambiente, las misiones y los hechizos de sus monstruos
    vo:Tyrael vo:TyraelAct4 vo:CainAct4 vo:Izual vo:HellsAngel env:31 env:32 env:33 env:34
    music_quest_izual music_quest_diablo object_diabloseal quest_hellforge_place quest_hellforge_smash
    quest_hellforge_flash_1 quest_hellforge_flash_2
    missile:diablight missile:diabfire missile:diabwall missile:diabwallmaker missile:megademoninferno
    missile:undeadmissile1 missile:monbonespirit missile:fingermagespider missile:hellmeteordown missile:frostnova
    # el Acto 5: la música y el ambiente de sus zonas y las voces de Harrogath (lo de las misiones, cuando estén)
    env:36 env:37 env:38 env:39 env:40 env:41 env:42 env:43 env:44 env:45 env:46 env:47 env:48 env:49
    vo:Larzuk vo:Malah vo:QualKehk vo:Nihlathak vo:CainAct5 vo:Anya
    music_quest_shenk music_quest_nihlathak music_quest_baal # (src/a5quest.inc)
    monster_baal_taunt_1 # la risa de Baal cuando se burla (BaalTaunt, src/monai5.inc)
    # los proyectiles de los monstruos del Acto 5 que tira src/monai5.inc
    "missile:healing vortex" "missile:catapultchargedball" "missile:catapult spike ball" "missile:catapult cold ball"
    "missile:catapult plague ball" "missile:catapult meteor ball" missile:suicidefireexplode missile:suicideiceexplode
    missile:suicidecorpseexplode "missile:baal cold maker" "missile:baal taunt lightning" "missile:baal taunt poison"
    # y los que encontró la demostración del Acto 5 jugando con los archivos del paquete
    barricadedoor_1 barricadedoor_2 barricadedoor_3 creature_chicken_1 creature_chicken_2 creature_chicken_3
    creature_chicken_4 deathmauler_spikes_1 deathmauler_spikes_2 deathmauler_spikes_3 deathmauler_spikes_4
    deathmauler_spikes_5 imp_missile_1 impact_fire_1 impact_fire_2 impact_fire_3 succubus_msissile_1
    succubus_msissile_2 succubus_msissile_3 succubus_msissile_impact
)
# Las auras del Paladín (el onsound de su estado en states.txt)
for a in might prayer resistfire holyfire thorns defiance resistcold blessedaim cleansing resistlightning \
         concentration holywind stamina holylight sanctuary meditation fanatacism purification redemption salvation; do
    sound_specs+=("paladin_aura_$a")
done
# Frases de cada clase (las voces en español de data/local/sfx)
for c in barbarian amazon sorceress paladin necromancer assassin druid warlock; do
    for f in needmana_1 cantcarry_1 act1_find_den act1_find_burial act1_complete_den act1_complete_burial \
             act1_find_cairn act1_find_tree act1_find_tower act1_find_tristram act1_find_monastery act1_find_jail \
             act1_find_catacombs act1_complete_tools act1_complete_tower act1_complete_andariel act1_rescue_cain \
             act1_find_wilderness act2_begin_tainted act2_complete_radament act2_complete_summoner \
             act2_complete_tainted act2_complete_tombs act2_find_arcane act2_find_clawviper act2_find_summoner \
             act2_find_radament act2_find_tomb_decoy act2_find_tomb_true act2_near_tightspot act2_complete_tightspot \
             act3_complete_beneath act3_complete_blackened act3_complete_blade act3_complete_guardian \
             act3_complete_lamesen act3_find_beneath act3_find_blackened act3_find_guardian act3_find_jade \
             act3_find_lamesen act4_free_izual act4_kill_diablo \
             impossible_1 not_in_town_1 cantuseyet needkey_1 chat_a_1 chat_b_1 chat_c_1 chat_d_1 chat_e_1 chat_f_1 chat_g_1 \
             chat_h_1; do
        sound_specs+=("${c}_$f")
    done
done
for w in light medium; do
    for m in walk run; do
        sound_specs+=("${w}_${m}_dirt_1" "${w}_${m}_istone_1")
    done
done
for s in punch_1 1hs_small_1 1hs_large_1 2hs_small_1 2hs_large_1 1ht_1 2ht_1 staff_1 bow_1 xbow_1; do
    sound_specs+=("weapon_$s")
done
for s in punch_1 blade_swing_1 blade_thrust_1 blunt_1 arrow_1 claw_1; do
    sound_specs+=("impact_$s")
done
# Los sonidos de cada monstruo del acto
while read -r id; do
    sound_specs+=("mon:$id")
done < <(python3 "$root/tools/act1monsters.py" "$root" ids; python3 "$root/tools/act1monsters.py" "$root" ids 2;
         python3 "$root/tools/act1monsters.py" "$root" ids 3; python3 "$root/tools/act1monsters.py" "$root" ids 4;
         python3 "$root/tools/act1monsters.py" "$root" ids 5)
mapfile -t sound_files < <(python3 "$root/tools/soundfiles.py" "$root" "${sound_specs[@]}" 2>/dev/null)
assets+=("${sound_files[@]}")

# Textos en español (zonas, monstruos, NPCs, objetos, propiedades mágicas y habilidades)
for s in levels monsters npcs objects shrines ui item-names item-nameaffixes item-modifiers item-runes item-gems skills quests vo mercenaries; do
    assets+=("data/local/lng/strings/$s.json")
done
assets+=(data/local/lng/strings/metadata/metadata-audio-vo.json) # qué grabación va con cada texto
# Los archivos, sin repetidos: una carpeta que aparecía dos veces (un monstruo
# de dos actos) se copiaba adentro de sí misma
( cd "$root" && for a in "${assets[@]}"; do
      if [ -d "$a" ]; then find "$a" -type f; elif [ -f "$a" ]; then echo "$a"; fi
  done ) | sort -u > "$game/archivos-todos.txt"
# La lista del importador: solo los del juego. Los .d2rw son dibujos editados
# por el usuario (la skin del Brujo y sus íconos): van en este paquete, el de
# su Pi, pero nunca en lo público (package-release.sh arma sin datos y lo revisa)
grep -v '\.d2rw$' "$game/archivos-todos.txt" > "$game/archivos.txt"
# y la misma lista en el código, para importar ahí (tools/archivos-del-juego.txt)
cmp -s "$game/archivos.txt" "$root/tools/archivos-del-juego.txt" || cp "$game/archivos.txt" "$root/tools/archivos-del-juego.txt"
# Las cinemáticas ya convertidas (no van en la lista: el importador las
# convierte aparte con ffmpeg, ver import_movies), también las de la expansión
for f in new_bliz.mpg bliznorth.mpg d2intro.mpg act02start.mpg act03start.mpg act04start.mpg act04end.mpg \
         d2x_intro.mpg d2x_out.mpg idioma.txt; do
    if [ -f "$root/data/global/video/$f" ]; then
        echo "data/global/video/$f" >> "$game/archivos-todos.txt"
    fi
done
if [ -z "${SIN_DATOS:-}" ]; then
    tar -C "$root" -cf - -T "$game/archivos-todos.txt" | tar -C "$game" -xf -
fi
rm "$game/archivos-todos.txt"

cat > "$out/Diablo II.sh" <<'EOF'
#!/bin/bash
DIR="$(dirname "$(readlink -f "$0")")"
cd "$DIR/.data/d2port" || exit 1
# Las partidas guardadas van aparte, así no se pisan al actualizar el port
export D2_SAVE_DIR=/userdata/saves/ports/d2port
mkdir -p "$D2_SAVE_DIR"
./d2port --fullscreen > /userdata/system/logs/d2port.log 2>&1
EOF
cp "$root/scripts/bench.sh" "$game/"
chmod +x "$out/Diablo II.sh" "$game/d2port" "$game/bench.sh"

du -sh "$out/.data/d2port"
echo "Listo: copiá el contenido de dist/ports a \\\\BATOCERA\\share\\roms\\ports"
