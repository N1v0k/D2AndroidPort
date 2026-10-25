/* ---- El endgame (incluido en game.c, después de cows.inc) ----

   El evento Pandemonium, como el original (D2MOO, D2_VERSION_HAS_UBERS):
   - Las llaves (la del Terror, del Odio y de la Destrucción) las sueltan la
     Condesa, el Invocador y Nihlathak en Infierno (sus TC).
   - Las tres en el cubo, en Harrogath y en Infierno (si no, "imposible"),
     abren un portal rojo a uno de los tres mapas, al azar entre los que no
     se abrieron en esta partida (ACT1Q4_CreateUberRunPortal): Matron's Den
     (Lilith, en el lugar del cofre brillante), las Forgotten Sands (Duriel
     uber, en el objeto número semilla % 3 + 3 del mapa) y el Furnace of Pain
     (Izual uber, en el del cofre brillante: SUNIT_SpawnPresetUnit). Cada uno
     suelta su órgano (TC Uber Andariel, Duriel e Izual).
   - Los tres órganos en el cubo, ahí mismo, abren el de Uber Tristram (uno
     por partida: ACT1Q4_CreateUberFinalePortal), con Mefisto, Diablo y Baal
     uber juntos (monaiu.inc).
   - Al morir los tres, el último suelta la Antorcha de fuego infernal y un
     Estandarte de los héroes por jugador (QUESTSFX_UberPrimeEvil).
   Lo que se abrió y quién salió es de la partida (no se guarda), como el
   original. Los niveles solo existen en el mundo del Acto 5 en Infierno
   (world5.inc). */

#define PORTAL_UBER(k)      (24 + 2 * (k)) /* k: 0..2 los tres mapas, 3 Uber Tristram, 4 la Colossal Summit */
#define PORTAL_UBER_BACK(k) (25 + 2 * (k))
#define UBER_TOWN    109 /* Harrogath */
#define UBER_FIRST   133 /* Matron's Den; 134 las Forgotten Sands, 135 el Furnace of Pain, 136 Uber Tristram, 137 la Colossal Summit */
#define UBER_MAPS    5
#define UBER_LEVEL   110 /* el de los Uber (Level(H) de monstats.txt) */

static int   s_uber_open[UBER_MAPS];   /* el portal ya se abrió en esta partida */
static int   s_uber_linked[UBER_MAPS]; /* está en los saltos del mundo del Acto 5 armado */
static float s_uber_x[UBER_MAPS], s_uber_y[UBER_MAPS]; /* dónde está (en Harrogath; el de la cumbre, donde se usó el cubo) */
static int   s_uber_boss[UBER_MAPS];   /* el jefe de ese mapa (o los tres de Tristram, o los Ancianos) ya salió */
static int   s_uber_from[UBER_MAPS];   /* la zona del portal (la del pueblo, salvo el de la cumbre) */
static int   s_col_ids[3] = { -1, -1, -1 }, s_col_last = -1, s_col_dropped; /* los Ancianos colosales */
static char  s_col_tc[3][32];
static int   s_uber_torch;             /* la Antorcha ya cayó */

static void uber_reset(void)
{
    for (int k = 0; k < UBER_MAPS; k++) {
        s_uber_open[k] = s_uber_linked[k] = s_uber_boss[k] = 0;
        world_portal(PORTAL_UBER(k), -1, 0, 0, -1, 0, 0);
        world_portal(PORTAL_UBER_BACK(k), -1, 0, 0, -1, 0, 0);
    }
    s_uber_torch = 0;
    s_col_last = -1;
    s_col_dropped = 0;
    for (int c = 0; c < 3; c++)
        s_col_ids[c] = -1;
}

/* Dónde se llega: una baldosa de llegada (la de los infiernos), si no la primera especial del
   mapa (las escaleras de la cripta, el KingWarp), si no el medio */
static void uber_spot(int k, float *x, float *y)
{
    int x0, y0, x1, y1, z = world_level_zone(UBER_FIRST + k), best = -1;
    world_zone_rect(z, &x0, &y0, &x1, &y1);
    *x = (x0 + x1) * TILE_SUB / 2.0f;
    *y = (y0 + y1) * TILE_SUB / 2.0f;
    if (!world_zone_loaded(z))
        return;
    for (int i = 0; i < s_map.num_specials && k < 3; i++) {
        const MapSpecial *s = &s_map.specials[i];
        if (s->x < x0 || s->x >= x1 || s->y < y0 || s->y >= y1)
            continue;
        if (a5_arrival_tile(s, x0, y0, x1, y1)) {
            best = i;
            break;
        }
        if (best < 0)
            best = i;
    }
    if (best >= 0) {
        *x = (float)s_map.specials[best].x * TILE_SUB + TILE_SUB / 2.0f;
        *y = (float)s_map.specials[best].y * TILE_SUB + TILE_SUB / 2.0f;
    }
    find_free_spot(x, y);
}

/* Los dos saltos de un portal abierto (otra vez cada vez que se arma una de sus zonas) */
static void uber_link(int k)
{
    int town = k == 4 ? s_uber_from[4] : world_level_zone(UBER_TOWN), dz = world_level_zone(UBER_FIRST + k);
    if (!s_uber_open[k] || town < 0 || dz < 0)
        return;
    float ax, ay;
    uber_spot(k, &ax, &ay);
    world_portal(PORTAL_UBER(k), town, s_uber_x[k], s_uber_y[k], dz, ax + 2.0f, ay + 2.0f);
    world_portal(PORTAL_UBER_BACK(k), dz, ax, ay, town, s_uber_x[k] + 2.0f, s_uber_y[k] + 2.0f);
    s_uber_linked[k] = 1;
    if (world_zone_loaded(town))
        a2_portal_object(town, "PortalPermanent", s_uber_x[k], s_uber_y[k]);
    if (world_zone_loaded(dz))
        a2_portal_object(dz, "PortalPermanent", ax, ay);
}

/* El jefe de cada mapa, la primera vez que se arma en la partida */
static void uber_spawn_boss(int k, int z)
{
    int ids[64], n;
    float x, y;
    const Level *lv = NULL;
    int x0, y0, x1, y1;
    world_zone_rect(z, &x0, &y0, &x1, &y1);
    lv = world_level_at((x0 + x1) * TILE_SUB / 2.0f, (y0 + y1) * TILE_SUB / 2.0f);
    int area = lv ? lv->mon_level : 83;
    switch (k) {
    case 0:   /* Lilith, en lugar del cofre brillante */
    case 2: { /* Izual uber, ídem */
        n = units_find_all("ConsolationChest", ids, 8);
        int found = -1;
        for (int i = 0; i < n && found < 0; i++)
            if (units_zone(ids[i]) == z)
                found = ids[i];
        if (found >= 0) {
            units_position(found, 1.0f, &x, &y);
            units_remove(found);
        } else { /* (si el cofre no está, en el medio del mapa) */
            x = (x0 + x1) * TILE_SUB / 2.0f;
            y = (y0 + y1) * TILE_SUB / 2.0f;
        }
        find_free_spot(&x, &y);
        units_zone_begin(z);
        units_spawn_boss_at(k == 0 ? "uberandariel" : "uberizual", x, y, UBER_LEVEL, area);
        units_zone_end();
        break;
    }
    case 1: { /* Duriel uber: en el objeto número semilla % 3 + 3 (el contador del original) */
        n = units_zone_objects(z, ids, 64);
        if (n <= 0)
            return;
        int pick = (int)(s_world_seed % 3u) + 2; /* (el tercero, el cuarto o el quinto) */
        units_position(ids[pick < n ? pick : n - 1], 1.0f, &x, &y);
        find_free_spot(&x, &y);
        units_zone_begin(z);
        units_spawn_boss_at("uberduriel", x, y, UBER_LEVEL, area);
        units_zone_end();
        break;
    }
    case 4: { /* la Colossal Summit: los tres Ancianos colosales (donde están las estatuas de los Antiguos, o en el medio) */
        static const char *const k_col[3] = { "colossal1", "colossal2", "colossal3" };
        units_zone_begin(z);
        for (int c = 0; c < 3; c++) {
            x = (x0 + x1) * TILE_SUB / 2.0f + (float)(c - 1) * 4.0f;
            y = (y0 + y1) * TILE_SUB / 2.0f;
            find_free_spot(&x, &y);
            int id = units_spawn_boss_at(k_col[c], x, y, UBER_LEVEL, area);
            s_col_ids[c] = id;
            if (id >= 0) { /* (solo el último que muere suelta su Joya: endgame) */
                snprintf(s_col_tc[c], sizeof s_col_tc[c], "%s", units_treasure_class(id));
                units_set_treasure_class(id, "");
            }
        }
        units_zone_end();
        s_col_last = -1;
        s_col_dropped = 0;
        break;
    }
    default: { /* Uber Tristram: los tres juntos */
        static const char *const k_evils[3] = { "ubermephisto", "uberdiablo", "uberbaal" };
        units_zone_begin(z);
        for (int e = 0; e < 3; e++) {
            x = (x0 + x1) * TILE_SUB / 2.0f + (float)(e - 1) * 3.0f;
            y = (y0 + y1) * TILE_SUB / 2.0f + 2.0f;
            find_free_spot(&x, &y);
            units_spawn_boss_at(k_evils[e], x, y, UBER_LEVEL, area);
        }
        units_zone_end();
        break;
    }
    }
    s_uber_boss[k] = 1;
}

static void uber_zone_loaded(int z)
{
    for (int k = 0; k < UBER_MAPS; k++) {
        int dz = world_level_zone(UBER_FIRST + k);
        if (dz < 0)
            continue;
        if (z == dz && !s_uber_boss[k])
            uber_spawn_boss(k, z);
        if (s_uber_open[k] && (z == dz || z == (k == 4 ? s_uber_from[4] : world_level_zone(UBER_TOWN))))
            uber_link(k);
    }
}

/* ¿Hay en el cubo justo esos tres? */
static int uber_cube_has(const Player *p, const char *a, const char *b, const char *c)
{
    const char *want[3] = { a, b, c };
    if (p->cube_count != 3)
        return 0;
    for (int w = 0; w < 3; w++) {
        int found = 0;
        for (int k = 0; k < 3 && !found; k++)
            found = p->cube[k].type && strcmp(p->cube[k].type->code, want[w]) == 0;
        if (!found)
            return 0;
    }
    return 1;
}

/* El botón Transmutar con las llaves o los órganos: el portal (o "imposible"). 0 = no es esta receta */
/* Las cinco estatuas de los Ancianos en el cubo, en el Acto 5 en Infierno: el portal rojo a la
   Colossal Summit (cubemain.txt: Uber Ancient Summon Material..., op 28, nivel 137), donde se usó.
   Cada vez que se usan, los Ancianos vuelven a salir. 0 = no es esta receta */
static int colossal_recipe(Player *p)
{
    static const char *const k_ua[5] = { "ua1", "ua2", "ua3", "ua4", "ua5" };
    if (p->cube_count != 5)
        return 0;
    for (int w = 0; w < 5; w++) {
        int found = 0;
        for (int k = 0; k < 5 && !found; k++)
            found = p->cube[k].type && strcmp(p->cube[k].type->code, k_ua[w]) == 0;
        if (!found)
            return 0;
    }
    if (s_act != 5 || s_diff != 2 || world_level_zone(UBER_FIRST + 4) < 0) {
        show_message(p, "Solo se abre en el Acto 5, en Infierno");
        player_say(p, "impossible_1");
        return 1;
    }
    float x = p->x + 2.0f, y = p->y + 1.0f;
    find_free_spot(&x, &y);
    if (world_level_at(x, y) != p->level) {
        x = p->x;
        y = p->y;
    }
    p->cube_count = 0;
    s_uber_open[4] = 1;
    s_uber_x[4] = x;
    s_uber_y[4] = y;
    s_uber_from[4] = zone_at(x, y);
    s_uber_boss[4] = 0; /* (vuelven a salir) */
    int dz = world_level_zone(UBER_FIRST + 4);
    if (world_zone_loaded(dz))
        uber_spawn_boss(4, dz);
    uber_link(4);
    audio_play_name("cursor_convert_item", AUDIO_NOPOS, AUDIO_NOPOS);
    audio_play_name("object_xportalopen", x, y);
    char msg[128];
    int x0, y0, x1, y1;
    world_zone_rect(dz, &x0, &y0, &x1, &y1);
    const Level *lv = world_level_at((x0 + x1) * TILE_SUB / 2.0f, (y0 + y1) * TILE_SUB / 2.0f);
    snprintf(msg, sizeof msg, tr("Se abrió un portal a %s"), lv ? str_get(lv->name) : "?");
    show_message(p, msg);
    save_character(p);
    return 1;
}

static int uber_recipe(Player *p)
{
    if (colossal_recipe(p))
        return 1;
    int keys = uber_cube_has(p, "pk1", "pk2", "pk3"), organs = uber_cube_has(p, "dhn", "bey", "mbr");
    if (!keys && !organs)
        return 0;
    int k = 3;
    if (keys) { /* uno de los tres al azar; si ese ya se abrió, el que sigue */
        int r = (int)(rnd() % 3u);
        k = -1;
        for (int i = 0; i < 3 && k < 0; i++)
            if (!s_uber_open[(r + i) % 3])
                k = (r + i) % 3;
    }
    const char *why = NULL;
    if (!p->level || p->level->id != UBER_TOWN || s_diff != 2)
        why = "Solo se abre en Harrogath, en Infierno";
    else if (k < 0)
        why = "Los tres portales ya se abrieron en esta partida";
    else if (k == 3 && s_uber_open[3])
        why = "El portal a Tristram ya se abrió en esta partida";
    if (why) {
        show_message(p, why);
        player_say(p, "impossible_1");
        return 1;
    }
    /* Al lado del jugador (QUESTS_GetFreePosition, a 3), en Harrogath */
    float x = p->x + 2.0f, y = p->y + 1.0f;
    find_free_spot(&x, &y);
    if (world_level_at(x, y) != p->level) {
        x = p->x;
        y = p->y;
    }
    p->cube_count = 0;
    s_uber_open[k] = 1;
    s_uber_x[k] = x;
    s_uber_y[k] = y;
    uber_link(k);
    audio_play_name("cursor_convert_item", AUDIO_NOPOS, AUDIO_NOPOS);
    audio_play_name("object_xportalopen", x, y);
    char msg[128]; /* (con el nombre del juego del destino) */
    int dz = world_level_zone(UBER_FIRST + k), x0, y0, x1, y1;
    world_zone_rect(dz, &x0, &y0, &x1, &y1);
    const Level *lv = world_level_at((x0 + x1) * TILE_SUB / 2.0f, (y0 + y1) * TILE_SUB / 2.0f);
    snprintf(msg, sizeof msg, tr("Se abrió un portal a %s"), lv ? str_get(lv->name) : "?");
    show_message(p, msg);
    save_character(p);
    return 1;
}

/* Cada tick: los portales en el mundo del Acto 5 y la muerte de los tres de Tristram (se busca
   solo con alguien ahí) */
/* Los Ancianos colosales: cuál murió último, y su Joya cuando mueren los tres (la que suelta
   el último: TC Uber Talic, Madawc o Korlic) */
static void colossal_drops(void)
{
    if (s_col_dropped || s_col_ids[0] < 0)
        return;
    static const char *const k_col[3] = { "colossal1", "colossal2", "colossal3" };
    int alive = 0, which = -1;
    for (int c = 0; c < 3; c++) {
        int id = s_col_ids[c];
        if (id < 0 || strcmp(units_id(id), k_col[c]) != 0) /* (se borró la zona) */
            return;
        if (!units_is_corpse(id)) {
            alive++;
            which = c;
        }
    }
    if (alive == 1) /* el que queda es el que muere último */
        s_col_last = which;
    if (alive > 0)
        return;
    if (s_col_last < 0) /* (murieron juntos) */
        s_col_last = 0;
    s_col_dropped = 1;
    float x, y;
    units_position(s_col_ids[s_col_last], 1.0f, &x, &y);
    Item drops[4];
    int n = items_roll_drop(s_col_tc[s_col_last], UBER_LEVEL, 0, drops, 4);
    for (int i = 0; i < n; i++)
        items_ground_add(&drops[i], x + 0.6f * (float)i, y);
}

static void uber_tick(void)
{
    if (s_act != 5) {
        for (int k = 0; k < UBER_MAPS; k++)
            s_uber_linked[k] = 0;
        return;
    }
    for (int k = 0; k < UBER_MAPS; k++)
        if (s_uber_open[k] && !s_uber_linked[k])
            uber_link(k);
    colossal_drops();
    if (s_uber_torch || !s_uber_boss[3])
        return;
    int inside = 0;
    for (int i = 0; i < s_num_players; i++)
        inside |= s_pl[i].level && s_pl[i].level->id == UBER_FIRST + 3;
    if (!inside)
        return;
    static const char *const k_evils[3] = { "ubermephisto", "uberdiablo", "uberbaal" };
    int last = -1;
    for (int e = 0; e < 3; e++) {
        int id;
        if (units_find_all(k_evils[e], &id, 1) != 1 || !units_is_corpse(id))
            return;
        last = id;
    }
    s_uber_torch = 1;
    float x, y;
    units_position(last, 1.0f, &x, &y);
    const ItemType *t = items_find("cm2");
    if (t) { /* la Antorcha: el único amuleto grande único de nivel 110 */
        Item it = items_make(t);
        if (items_make_unique(&it, UBER_LEVEL))
            items_ground_add(&it, x, y);
    }
    for (int i = 0; i < s_num_players; i++) /* y un Estandarte de los héroes por jugador */
        drop_at("std", 0, x + 0.8f * (float)(i + 1), y + 0.5f);
    audio_play_name("object_xportalopen", x, y);
}

/* ---- Reasignar los puntos: el Token of Absolution y la recompensa de Akara (1.13) ----
   Las cuatro esencias (las sueltan Andariel y Duriel, Mefisto, Diablo y Baal en Infierno) en
   el cubo dan el Token (la receta de cubemain.txt); al usarlo, o con la opción de Akara
   (una vez por dificultad, después de la Den of Evil), vuelven todos los puntos de atributo
   y de habilidad para repartirlos de nuevo. Lo que dan los objetos queda. */
static void player_respec(Player *p)
{
    for (int k = 0; k < 4; k++) {
        p->stat_points += p->stat_add[k];
        p->stat_add[k] = 0;
    }
    for (int i = 0; i < skills_count(p->cls); i++) {
        p->skill_points += p->skill_lvl[i];
        p->skill_lvl[i] = 0;
    }
    for (int b = 0; b < NUM_BUTTONS; b++) /* (las habilidades ya no están: los botones, como al empezar) */
        if (p->buttons[b] >= 0)
            p->buttons[b] = b == 0 ? BTN_ATTACK : BTN_EMPTY;
    p->aura = -1;
    refresh_stats(p);
    if (p->hp > p->hp_max)
        p->hp = p->hp_max;
    if (p->mana > p->mana_max)
        p->mana = p->mana_max;
    audio_play_name("item_scroll", AUDIO_NOPOS, AUDIO_NOPOS);
    char msg[96];
    snprintf(msg, sizeof msg, tr("%d puntos de atributo y %d de habilidad para repartir"), p->stat_points, p->skill_points);
    show_message(p, msg);
}

static int tz_use_shard(Player *p, int i);

/* Usar un objeto del endgame (X en la mochila): el Token y las astillas. 0 = no es de estos */
static int endgame_use_item(Player *p, int i)
{
    if (tz_use_shard(p, i))
        return 1;
    if (strcmp(p->bag[i].type->code, "toa") != 0)
        return 0;
    bag_remove(p, i);
    player_respec(p);
    save_character(p);
    return 1;
}

/* ---- El Diablo clon (Uber Diablo) y la Annihilus ----
   En el original solo existía en Battle.net: el servidor del reino contaba las Piedras de
   Jordán vendidas a los mercaderes ("%d Stones of Jordan Sold to Merchants") y, al llegar a la
   cantidad, avisaba "Diablo Walks the Earth"; en una partida en Infierno, el próximo jefe con
   nombre (superunique) que aparecía era el Diablo clon, que suelta la Annihilus. Sin conexión
   nunca existió. Regla propia del port (como los mods para un jugador): vender DCLONE_SOJS
   Piedras de Jordán en Infierno lo despierta, y el próximo superunique de Infierno que se arme
   es el clon (es de la partida: no se guarda). */
#define DCLONE_SOJS 1

static int s_dclone_sold;    /* las vendidas en esta partida */
static int s_dclone_walks;   /* ya camina: espera el próximo superunique */
static int s_dclone_dropped; /* la Annihilus del clon de esta partida ya cayó */

static void dclone_reset(void)
{
    s_dclone_sold = s_dclone_walks = s_dclone_dropped = 0;
    units_clone_next_superunique(0);
}

static void dclone_sold(Player *p, const Item *it)
{
    if (s_diff != 2 || it->quality != QUALITY_UNIQUE || strcmp(items_unique_key(it), "The Stone of Jordan") != 0)
        return;
    char msg[128];
    snprintf(msg, sizeof msg, str_get("SOJSold"), ++s_dclone_sold);
    show_message(p, msg);
    if (s_dclone_walks || s_dclone_sold < DCLONE_SOJS)
        return;
    s_dclone_walks = 1;
    s_dclone_dropped = 0;
    units_clone_next_superunique(1);
    for (int i = 0; i < s_num_players; i++)
        show_message(&s_pl[i], str_get("DiabloWalksEarth"));
    audio_play_name("monster_diablo_taunt_1", AUDIO_NOPOS, AUDIO_NOPOS);
}

/* Cada tick: si su zona se borró sin matarlo, sale en el próximo superunique. La Annihilus la
   suelta por su clase de tesoro (ROP (H): el único por su nombre) */
static void dclone_tick(void)
{
    int id = units_clone_unit();
    if (!s_dclone_walks || s_dclone_dropped || id < 0)
        return;
    if (strcmp(units_id(id), "diabloclone") != 0) { /* (se borró su zona sin matarlo: sale en el próximo) */
        units_clone_next_superunique(1);
        return;
    }
    if (!units_is_corpse(id))
        return;
    s_dclone_dropped = 1;
    s_dclone_walks = 0;
}

/* ---- Las zonas de terror (Resurrected 2.5; las astillas y los Heraldos, de la expansión del Brujo) ----
   Los datos salen de data/hd/global/excel/desecratedzones.json (lo trae el importador): las 34
   zonas, los cinco grupos de un acto entero, los límites de nivel de cada dificultad y los Heraldos
   de Infierno. La lógica es del servidor de Blizzard (no está en D2MOO); sale de las notas de los
   parches y de las guías:
   - se abren al matar a Baal en esa dificultad, con el personaje del jugador 1 (el que "crea" la
     partida); el menú de la dificultad las puede apagar;
   - cada zone_duration_minutes (30) de hora real se aterroriza otra zona. El orden lo sortea el
     servidor del original: acá sale de la semilla del json y del número de turno, así que no
     coincide con el de Battle.net;
   - los monstruos: el nivel del jugador 1 más boost_level (2), entre bound_incl_min y
     bound_incl_max de la dificultad, o el suyo si era más; los campeones y los únicos suman lo de
     siempre arriba de eso (el +4 y +5 de las notas). Más boost_experience_percent (25) de
     experiencia, las clases de tesoro "Desecrated" y, en Infierno, un modificador más para los
     únicos (always_unique_mod_pool, con el aura de unique_mod_chance_boosts);
   - una astilla de la Piedra del Mundo (xa1..xa5) aterroriza su acto entero el resto de la partida
     (manual_zones), en lugar de la rotación;
   - los Heraldos (solo Infierno): cada muerte en la zona sube la probabilidad de que uno salga a
     cazarte (la curva de herald_tiers: asymptote / (1 + e^(-slope (muertes - midpoint))) +
     vertical_shift, en %). Avisa, cinco segundos de rayos y sale cerca; cada uno muerto sube el
     grado del próximo, hasta 5, y suelta lo de su TreasureClassHerald (de ahí los amuletos Sunder). */

#define TZ_ZONES  40
#define TZ_LEVELS 40
#define TZ_TIERS  5
#define TZ_IDS    140 /* los Id de levels.txt */
#define TZ_HERALD_WAIT (5 * TICK_RATE)

typedef struct {
    int n, lv[TZ_LEVELS];
    int nwp, wp[TZ_LEVELS]; /* waypoint_level_id: los waypoints que se marcan */
} TzZone;

typedef struct {
    float slope, mid, asym, shift; /* la probabilidad por muerte en la zona */
    int   hp, dmg, tc, mhp, mdmg, nmin, nmax, umin, umax;
} TzTier;

static TzZone    s_tz_zone[TZ_ZONES], s_tz_act[5];
static int       s_tz_nzones, s_tz_loaded; /* 1 cargado, -1 no está el json */
static struct { int min, max, boost, xp; } s_tz_diff[3];
static TzTier    s_tz_tier[TZ_TIERS];
static int       s_tz_ntiers; /* max_herald_tiers de Infierno (0 = sin Heraldos) */
static int       s_tz_pool_mod[16], s_tz_pool_w[16], s_tz_npool, s_tz_boost_mod, s_tz_boost;
static unsigned long long s_tz_seed;
static int       s_tz_minutes = 30;
static long long s_tz_start; /* start_time_utc, en segundos desde 1970 */
static char      s_tz_lvname[TZ_IDS][40]; /* la clave de levels.json de cada Id */

static int       s_tz_on = -1;    /* -1 = falta ver (el primer tick de la partida) */
static int       s_tz_menu = 1;   /* la opción del menú de la dificultad */
static long long s_tz_turn = -1;  /* el turno de la rotación */
static int       s_tz_cur = -1;   /* la zona del turno */
static int       s_tz_shard = -1; /* el acto (0..4) de la astilla que se usó */
static int       s_tz_warned;     /* los avisos de los últimos minutos que ya se dieron (0..3) */
static int       s_tz_kills;      /* muertes en la zona aterrorizada, en este turno */
static int       s_tz_tier_next;  /* el grado del próximo Heraldo, 0..4 */
static int       s_tz_herald = -1, s_tz_herald_ticks; /* el Heraldo que caza, o lo que falta para que salga */
static float     s_tz_hx, s_tz_hy;
static int       s_tz_baal_seen;  /* el jugador 1 ya había matado a Baal en esta dificultad */

static int bots_playing(void);

/* ---- Un lector de JSON chico: llama a tz_value con la ruta de cada valor simple
   ("desecrated_zones/0/zones/3/levels/1/level_id") ---- */
typedef struct {
    const char *p, *end;
    char        path[256];
} TzJson;

static void tz_value(const char *path, const char *v);

static void js_ws(TzJson *j)
{
    while (j->p < j->end && (unsigned char)*j->p <= ' ')
        j->p++;
}

/* Un texto entre comillas (sin traducir los escapes: no hacen falta acá); -1 = no hay */
static int js_string(TzJson *j, char *out, size_t cap)
{
    if (j->p >= j->end || *j->p != '"')
        return -1;
    j->p++;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        if (*j->p == '\\' && j->p + 1 < j->end)
            j->p++;
        if (n + 1 < cap)
            out[n++] = *j->p;
        j->p++;
    }
    out[n] = '\0';
    if (j->p < j->end)
        j->p++;
    return (int)n;
}

static void js_value(TzJson *j, size_t plen, int depth)
{
    js_ws(j);
    if (j->p >= j->end || depth > 24)
        return;
    char c = *j->p;
    if (c == '{' || c == '[') {
        int index = 0;
        j->p++;
        for (;;) {
            js_ws(j);
            if (j->p >= j->end)
                return;
            if (*j->p == '}' || *j->p == ']') {
                j->p++;
                return;
            }
            if (*j->p == ',') {
                j->p++;
                continue;
            }
            char key[64];
            if (c == '{') {
                if (js_string(j, key, sizeof key) < 0) {
                    j->p++;
                    continue;
                }
                js_ws(j);
                if (j->p < j->end && *j->p == ':')
                    j->p++;
            } else {
                snprintf(key, sizeof key, "%d", index++);
            }
            int n = snprintf(j->path + plen, sizeof j->path - plen, "%s%s", plen ? "/" : "", key);
            size_t sub = n > 0 && plen + (size_t)n < sizeof j->path ? plen + (size_t)n : plen;
            const char *before = j->p;
            js_value(j, sub, depth + 1);
            j->path[plen] = '\0';
            if (j->p == before) /* (algo que no se entiende: seguir igual) */
                j->p++;
        }
    }
    char v[64];
    if (c == '"') {
        js_string(j, v, sizeof v);
    } else { /* número, true, false, null */
        const char *s = j->p;
        while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']' && (unsigned char)*j->p > ' ')
            j->p++;
        snprintf(v, sizeof v, "%.*s", (int)(j->p - s), s);
    }
    tz_value(j->path, v);
}

/* "2025-12-05 00:00:00" (UTC) en segundos desde 1970 */
static long long tz_epoch(const char *s)
{
    int y, mo, d, h = 0, mi = 0, se = 0;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3)
        return 0;
    y -= mo <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + se;
}

static void tz_add(TzZone *z, const char *field, int v)
{
    if (!strcmp(field, "level_id") && z->n < TZ_LEVELS) {
        z->lv[z->n++] = v;
    } else if (!strcmp(field, "waypoint_level_id") && z->nwp < TZ_LEVELS) {
        for (int k = 0; k < z->nwp; k++)
            if (z->wp[k] == v)
                return;
        z->wp[z->nwp++] = v;
    }
}

static void tz_tier_field(TzTier *t, const char *f, const char *v)
{
    static const struct { const char *name; int ofs, is_float; } k_fields[] = {
        { "zone_chance_slope", offsetof(TzTier, slope), 1 },
        { "zone_chance_midpoint", offsetof(TzTier, mid), 1 },
        { "zone_chance_asymptote", offsetof(TzTier, asym), 1 },
        { "zone_chance_vertical_shift", offsetof(TzTier, shift), 1 },
        { "herald_health_boost_percent", offsetof(TzTier, hp), 0 },
        { "herald_damage_boost_percent", offsetof(TzTier, dmg), 0 },
        { "herald_treasure_class_level_boost", offsetof(TzTier, tc), 0 },
        { "minion_health_boost_percent", offsetof(TzTier, mhp), 0 },
        { "minion_damage_boost_percent", offsetof(TzTier, mdmg), 0 },
        { "min_minions", offsetof(TzTier, nmin), 0 },
        { "max_minions", offsetof(TzTier, nmax), 0 },
        { "min_unique_mods", offsetof(TzTier, umin), 0 },
        { "max_unique_mods", offsetof(TzTier, umax), 0 },
    };
    for (unsigned k = 0; k < sizeof k_fields / sizeof k_fields[0]; k++)
        if (!strcmp(f, k_fields[k].name)) {
            if (k_fields[k].is_float)
                *(float *)((char *)t + k_fields[k].ofs) = (float)atof(v);
            else
                *(int *)((char *)t + k_fields[k].ofs) = atoi(v);
        }
}

static void tz_value(const char *path, const char *v)
{
    static const char pre[] = "desecrated_zones/0/";
    if (strncmp(path, pre, sizeof pre - 1) != 0)
        return;
    path += sizeof pre - 1;
    int a, b, k;
    char d[16], f[80];
    if (!strcmp(path, "start_time_utc"))
        s_tz_start = tz_epoch(v);
    else if (!strcmp(path, "seed"))
        s_tz_seed = strtoull(v, NULL, 10);
    else if (!strcmp(path, "zone_duration_minutes"))
        s_tz_minutes = atoi(v) > 0 ? atoi(v) : 30;
    else if (sscanf(path, "zones/%d/levels/%d/%79s", &a, &b, f) == 3 && a >= 0 && a < TZ_ZONES) {
        tz_add(&s_tz_zone[a], f, atoi(v));
        if (a + 1 > s_tz_nzones)
            s_tz_nzones = a + 1;
    } else if (sscanf(path, "manual_zones/%d/zones/0/levels/%d/%79s", &a, &b, f) == 3 && a >= 0 && a < 5) {
        tz_add(&s_tz_act[a], f, atoi(v));
    } else if (sscanf(path, "game_difficulties/rotw/%15[a-z]/%79s", d, f) == 2 &&
               (k = !strcmp(d, "normal") ? 0 : !strcmp(d, "nightmare") ? 1 : !strcmp(d, "hell") ? 2 : -1) >= 0) {
        char g[48];
        if (!strcmp(f, "defaults/bound_incl_min"))
            s_tz_diff[k].min = atoi(v);
        else if (!strcmp(f, "defaults/bound_incl_max"))
            s_tz_diff[k].max = atoi(v);
        else if (!strcmp(f, "defaults/boost_level"))
            s_tz_diff[k].boost = atoi(v);
        else if (!strcmp(f, "defaults/boost_experience_percent"))
            s_tz_diff[k].xp = atoi(v);
        else if (k == 2 && !strcmp(f, "max_herald_tiers"))
            s_tz_ntiers = atoi(v) < 0 ? 0 : atoi(v) > TZ_TIERS ? TZ_TIERS : atoi(v);
        else if (k == 2 && sscanf(f, "always_unique_mod_pool/%d/%47s", &a, g) == 2 && a >= 0 && a < 16) {
            if (!strcmp(g, "unique_mod"))
                s_tz_pool_mod[a] = atoi(v);
            else if (!strcmp(g, "chance"))
                s_tz_pool_w[a] = atoi(v);
            if (a + 1 > s_tz_npool)
                s_tz_npool = a + 1;
        } else if (k == 2 && sscanf(f, "defaults/unique_mod_chance_boosts/0/%47s", g) == 1) {
            if (!strcmp(g, "unique_mod"))
                s_tz_boost_mod = atoi(v);
            else if (!strcmp(g, "boost"))
                s_tz_boost = atoi(v);
        } else if (k == 2 && sscanf(f, "defaults/herald_tiers/%d/%47s", &a, g) == 2 && a >= 0 && a < TZ_TIERS) {
            tz_tier_field(&s_tz_tier[a], g, v);
        }
    }
}

static void tz_load(void)
{
    size_t n;
    uint8_t *d = fs_read("data/hd/global/excel/desecratedzones.json", &n);
    if (!d) {
        s_tz_loaded = -1;
        fprintf(stderr, "falta desecratedzones.json: no hay zonas de terror\n");
        return;
    }
    TzJson j = { (const char *)d, (const char *)d + n, "" };
    js_value(&j, 0, 0);
    free(d);
    /* Los nombres de los niveles (levels.txt), para los avisos y los waypoints */
    TxtTable t = { 0 };
    d = fs_read("data/global/excel/levels.txt", &n);
    if (d && txt_load(d, n, &t) == 0) {
        for (int r = 1; r < t.rows; r++) {
            int id = txt_get_int(&t, r, "Id", -1);
            if (id >= 0 && id < TZ_IDS)
                snprintf(s_tz_lvname[id], sizeof s_tz_lvname[id], "%s", txt_get(&t, r, txt_col(&t, "LevelName")));
        }
        txt_free(&t);
    }
    free(d);
    /* El aura (unique_mod_chance_boosts) va con su peso entre los modificadores de más */
    if (s_tz_boost_mod > 0 && s_tz_boost > 0 && s_tz_npool < 16) {
        s_tz_pool_mod[s_tz_npool] = s_tz_boost_mod;
        s_tz_pool_w[s_tz_npool++] = s_tz_boost;
    }
    s_tz_loaded = s_tz_nzones > 0 ? 1 : -1;
    if (s_debug || getenv("D2_TZ_DEBUG"))
        printf("TERROR: %d zonas, %d min, semilla %llu, Infierno %d..%d, %d grados de Heraldo, %d modificadores\n", s_tz_nzones,
               s_tz_minutes, s_tz_seed, s_tz_diff[2].min, s_tz_diff[2].max, s_tz_ntiers, s_tz_npool);
}

static void tz_reset(void)
{
    if (!s_tz_loaded)
        tz_load();
    s_tz_on = -1;
    s_tz_turn = -1;
    s_tz_cur = s_tz_shard = s_tz_herald = -1;
    s_tz_warned = s_tz_kills = s_tz_tier_next = s_tz_herald_ticks = 0;
}

/* Lo aterrorizado ahora: el acto de la astilla o la zona del turno (NULL = nada) */
static const TzZone *tz_current(void)
{
    if (s_tz_on != 1)
        return NULL;
    if (s_tz_shard >= 0)
        return &s_tz_act[s_tz_shard];
    return s_tz_cur >= 0 ? &s_tz_zone[s_tz_cur] : NULL;
}

static int tz_level_on(const Level *lv)
{
    const TzZone *z = lv && !lv->town ? tz_current() : NULL;
    for (int k = 0; z && k < z->n; k++)
        if (z->lv[k] == lv->id)
            return 1;
    return 0;
}

/* El nivel del terror: el del jugador 1 + boost_level, entre los límites de la dificultad */
static int tz_base(void)
{
    int b = s_pl[0].level_num + s_tz_diff[s_diff].boost;
    return b < s_tz_diff[s_diff].min ? s_tz_diff[s_diff].min : b > s_tz_diff[s_diff].max ? s_tz_diff[s_diff].max : b;
}

/* El nivel de los monstruos de un nivel (el de levels.txt, o el del terror si es más) */
static int tz_mon_level(const Level *lv)
{
    if (!lv)
        return 0;
    if (!tz_level_on(lv))
        return lv->mon_level;
    int b = tz_base();
    return b > lv->mon_level ? b : lv->mon_level;
}

/* Para units.c: el nivel del terror en ese lugar (0 = no está aterrorizado) */
static int tz_at(float x, float y)
{
    const Level *lv = world_level_at(x, y);
    return tz_level_on(lv) ? tz_mon_level(lv) : 0;
}

static const char *tz_level_name(int id)
{
    return id >= 0 && id < TZ_IDS && s_tz_lvname[id][0] ? str_get(s_tz_lvname[id]) : "?";
}

/* Saca los colores del texto del juego ("ÿcM%sÿc0") */
static void tz_strip(char *s)
{
    char *o = s;
    for (const char *p = s; *p;) {
        if ((unsigned char)p[0] == 0xc3 && (unsigned char)p[1] == 0xbf && p[2] == 'c' && p[3]) {
            p += 4;
            continue;
        }
        *o++ = *p++;
    }
    *o = '\0';
}

/* Los nombres de lo aterrorizado: "Burial Grounds y Crypt", "Burial Grounds, Crypt +1 más", "Acto 2" */
static void tz_text(const TzZone *z, char *out, size_t cap)
{
    out[0] = '\0';
    if (!z)
        return;
    if (z >= s_tz_act && z < s_tz_act + 5) {
        snprintf(out, cap, tr("Acto %d"), (int)(z - s_tz_act) + 1);
        return;
    }
    char fmt[96];
    if (z->n == 1) {
        snprintf(out, cap, "%s", tz_level_name(z->lv[0]));
    } else if (z->n == 2) {
        snprintf(fmt, sizeof fmt, "%s", str_get("DesecratedTwoLevelsFormat"));
        tz_strip(fmt);
        snprintf(out, cap, strstr(fmt, "%s") ? fmt : "%s, %s", tz_level_name(z->lv[0]), tz_level_name(z->lv[1]));
    } else if (z->n > 2) {
        char more[32];
        snprintf(fmt, sizeof fmt, "%s", str_get("DesecratedHUDRemainder"));
        snprintf(more, sizeof more, strstr(fmt, "%d") ? fmt : "+%d", z->n - 2);
        snprintf(out, cap, "%s, %s %s", tz_level_name(z->lv[0]), tz_level_name(z->lv[1]), more);
    }
}

/* Un aviso para todos, un rato más largo que los otros */
static void tz_say(const char *text)
{
    for (int i = 0; i < s_num_players && i < MAX_PLAYERS; i++) {
        show_message(&s_pl[i], text);
        s_pl[i].message_ticks = 6 * TICK_RATE;
    }
}

static void tz_say_zone(const char *key, const TzZone *z)
{
    char names[160], fmt[96], msg[256];
    tz_text(z, names, sizeof names);
    snprintf(fmt, sizeof fmt, "%s", str_get(key));
    tz_strip(fmt);
    snprintf(msg, sizeof msg, strstr(fmt, "%s") ? fmt : "%s", names);
    tz_say(msg);
    if (s_debug || getenv("D2_TZ_DEBUG"))
        printf("TERROR: %s\n", msg);
}

/* ¿Ese waypoint (su nombre de levels.json) es de lo aterrorizado? (se ve en violeta) */
static int tz_waypoint_marked(const char *level_name)
{
    const TzZone *z = tz_current();
    for (int k = 0; z && k < z->nwp; k++)
        if (z->wp[k] >= 0 && z->wp[k] < TZ_IDS && !strcmp(s_tz_lvname[z->wp[k]], level_name))
            return 1;
    return 0;
}

/* La zona de un turno: cada ciclo de s_tz_nzones turnos pasa por todas, en un orden sorteado con la
   semilla del json (y la primera de un ciclo no repite la última del anterior) */
static uint64_t tz_mix(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void tz_perm(long long cycle, int *perm)
{
    int n = s_tz_nzones;
    uint64_t r = s_tz_seed ^ (uint64_t)cycle * 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < n; i++)
        perm[i] = i;
    for (int i = n - 1; i > 0; i--) {
        r = tz_mix(r + 0x9E3779B97F4A7C15ull);
        int j = (int)(r % (uint64_t)(i + 1)), t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }
}

static int tz_zone_of(long long turn)
{
    int n = s_tz_nzones, perm[TZ_ZONES], prev[TZ_ZONES];
    long long cycle = turn >= 0 ? turn / n : 0, k = turn >= 0 ? turn % n : 0;
    tz_perm(cycle, perm);
    if (n > 2) {
        tz_perm(cycle - 1, prev);
        if (perm[0] == prev[n - 1]) {
            int t = perm[0];
            perm[0] = perm[1];
            perm[1] = t;
        }
    }
    return perm[k];
}

static long long tz_now(void)
{
    const char *e = getenv("D2_TZ_TIME"); /* (para probar: los segundos desde 1970) */
    if (s_lockstep) /* (en red, el reloj de la partida: el del anfitrión al empezar más los ticks) */
        return (e ? atoll(e) : s_epoch) + (long long)(s_ticks / TICK_RATE);
    return e ? atoll(e) : (long long)time(NULL);
}

/* Lo que falta del turno, en segundos */
static int tz_left(void)
{
    long long len = s_tz_minutes * 60LL, since = tz_now() - s_tz_start;
    return (int)(len - (since >= 0 ? since % len : 0));
}

/* Usar una astilla (X en la mochila): su acto entero, el resto de la partida. 0 = no es una */
static int tz_use_shard(Player *p, int i)
{
    const char *code = p->bag[i].type->code;
    if (strncmp(code, "xa", 2) != 0 || code[2] < '1' || code[2] > '5' || code[3])
        return 0;
    if (s_tz_on != 1) { /* sin Baal muerto en esta dificultad (o apagadas) */
        show_message(p, str_get("DesecratedInactive"));
        return 1;
    }
    int act = code[2] - '1';
    if (s_tz_shard == act)
        return 1;
    bag_remove(p, i);
    s_tz_shard = act;
    s_tz_kills = 0;
    tz_say_zone("BaseDesecratedFormat", &s_tz_act[act]);
    save_character(p);
    return 1;
}

/* La probabilidad (en %) de que una muerte despierte al próximo Heraldo */
static float tz_herald_chance(int kills)
{
    const TzTier *t = &s_tz_tier[s_tz_tier_next];
    float c = t->asym / (1.0f + expf(-t->slope * ((float)kills - t->mid))) + t->shift;
    return c > 0.0f ? c : 0.0f;
}

/* Donde sale: libre, a unos pasos del jugador y en lo aterrorizado */
static int tz_herald_spot(const Player *p, float *x, float *y)
{
    for (int tries = 0; tries < 24; tries++) {
        float a = (float)(rnd() % 628) / 100.0f, r = 6.0f + (float)(rnd() % 400) / 100.0f;
        float hx = p->x + cosf(a) * r, hy = p->y + sinf(a) * r;
        if (!blocked_body(hx, hy) && tz_level_on(world_level_at(hx, hy))) {
            *x = hx;
            *y = hy;
            return 1;
        }
    }
    return 0;
}

/* Cada muerte de un monstruo aterrorizado (award_kill): en Infierno, quizás un Heraldo */
static void tz_kill(const Player *p, int unit)
{
    if (s_tz_on != 1 || s_diff != 2 || s_tz_ntiers <= 0 || !units_is_terror(unit) || units_herald_tier(unit))
        return;
    s_tz_kills++;
    if (s_tz_herald >= 0 || s_tz_herald_ticks > 0)
        return;
    float c = getenv("D2_TZ_HERALD") ? 100.0f : tz_herald_chance(s_tz_kills);
    if ((float)(rnd() % 10000) >= c * 100.0f || !tz_herald_spot(p, &s_tz_hx, &s_tz_hy))
        return;
    s_tz_herald_ticks = TZ_HERALD_WAIT;
    tz_say(str_get("DesecratedZoneUniqueTokenGained"));
    overlays_add("thunderstormcast", OV_AT_POINT, 0, s_tz_hx, s_tz_hy, TZ_HERALD_WAIT);
    if (s_debug || getenv("D2_TZ_DEBUG"))
        printf("TERROR: un Heraldo de grado %d a las %d muertes (%.2f %%)\n", s_tz_tier_next + 1, s_tz_kills, c);
}

static void tz_herald_spawn(void)
{
    const Level *lv = world_level_at(s_tz_hx, s_tz_hy);
    if (!tz_level_on(lv))
        return;
    const char *kinds[LEVEL_MAX_MONSTERS];
    int n = 0;
    for (int k = 0; k < LEVEL_MAX_MONSTERS && lv->monsters[k]; k++)
        if (units_monstats_int(lv->monsters[k], "CannotHerald", 0) != 1 && units_monstats_int(lv->monsters[k], "killable", 1) == 1)
            kinds[n++] = lv->monsters[k];
    if (n == 0)
        return;
    const TzTier *t = &s_tz_tier[s_tz_tier_next];
    UnitsHerald h;
    h.tier = s_tz_tier_next + 1;
    h.hp_pct = t->hp;
    h.dmg_pct = t->dmg;
    h.minions = t->nmin + (t->nmax > t->nmin ? (int)(rnd() % (uint32_t)(t->nmax - t->nmin + 1)) : 0);
    h.minion_hp_pct = t->mhp;
    h.minion_dmg_pct = t->mdmg;
    h.mods = t->umin + (t->umax > t->umin ? (int)(rnd() % (uint32_t)(t->umax - t->umin + 1)) : 0);
    h.tc_boost = t->tc;
    int z = world_zone_at(&s_map, s_tz_hx, s_tz_hy);
    if (z < 0 || !world_zone_loaded(z))
        return;
    units_zone_begin(z);
    s_tz_herald = units_spawn_herald(kinds[rnd() % (uint32_t)n], s_tz_hx, s_tz_hy, tz_mon_level(lv), &h);
    units_zone_end();
    if (s_tz_herald < 0)
        return;
    overlays_add("teleport", OV_AT_POINT, 0, s_tz_hx, s_tz_hy, 0);
    char fmt[96], msg[160];
    snprintf(fmt, sizeof fmt, "%s", str_get("DesecratedZoneUniqueSpawnedLevel"));
    tz_strip(fmt);
    snprintf(msg, sizeof msg, strstr(fmt, "%s") ? fmt : "%s", str_get(lv->name));
    tz_say(msg);
    if (s_debug || getenv("D2_TZ_DEBUG"))
        printf("TERROR: sale %s (%s, grado %d, nivel %d, %d esbirros)\n", units_display_name(s_tz_herald), units_id(s_tz_herald),
               h.tier, units_level(s_tz_herald), h.minions);
}

static void tz_herald_tick(void)
{
    if (s_tz_herald >= 0) {
        int tier = units_herald_tier(s_tz_herald);
        if (tier && units_is_corpse(s_tz_herald)) { /* muerto: el próximo, un grado más */
            if (s_tz_tier_next < s_tz_ntiers - 1)
                s_tz_tier_next++;
            s_tz_herald = -1;
        } else if (!tier) { /* se borró su zona sin matarlo */
            s_tz_herald = -1;
        }
        return;
    }
    if (s_tz_herald_ticks > 0 && --s_tz_herald_ticks == 0)
        tz_herald_spawn();
}

/* Los amuletos Sunder de todos los jugadores: los elementos con la inmunidad rota */
static void tz_sunder(void)
{
    static const int k_stat[6] = { STAT_SUNDER_PHYS, STAT_SUNDER_FIRE, STAT_SUNDER_COLD, STAT_SUNDER_LTNG, STAT_SUNDER_POIS, STAT_SUNDER_MAG };
    unsigned mask = 0; /* en el orden de ELEM_NONE, ELEM_FIRE, ELEM_COLD, ELEM_LTNG, ELEM_POIS y ELEM_MAG */
    for (int i = 0; i < s_num_players; i++)
        for (int e = 0; e < 6; e++)
            if (s_pl[i].bonus[k_stat[e]] > 0)
                mask |= 1u << e;
    units_set_sunder(mask);
}

/* Al empezar la partida (el primer tick, o la pausa si arrancó en pausa): ya están los personajes */
static void tz_begin(void)
{
    if (s_tz_on >= 0)
        return;
    const char *e = getenv("D2_TZ");
    s_tz_baal_seen = s_pl[0].baal_q >= 4;
    s_tz_on = s_tz_loaded == 1 && s_tz_menu && (e ? atoi(e) > 0 : s_tz_baal_seen && !bots_playing());
    units_set_terror(s_tz_on ? tz_at : NULL, s_tz_diff[s_diff].xp);
    units_terror_pool(s_tz_pool_mod, s_tz_pool_w, s_diff == 2 ? s_tz_npool : 0);
}

static void tz_tick(void)
{
    tz_sunder();
    tz_begin();
    if (s_tz_on != 1) {
        if (!s_tz_baal_seen && s_pl[0].baal_q >= 4 && s_tz_loaded == 1) { /* recién mató a Baal: la próxima partida */
            s_tz_baal_seen = 1;
            tz_say(str_get("DesecratedFeatureUnlocked"));
        }
        return;
    }
    if (s_tz_shard < 0) {
        long long len = s_tz_minutes * 60LL, since = tz_now() - s_tz_start, turn = since >= 0 ? since / len : 0;
        if (turn != s_tz_turn) {
            const char *force = getenv("D2_TZ_ZONE");
            int z = force ? atoi(force) % s_tz_nzones : tz_zone_of(turn);
            if (s_tz_turn >= 0 && s_tz_cur >= 0)
                tz_say_zone("BaseRemoveDesecratedFormat", &s_tz_zone[s_tz_cur]);
            s_tz_turn = turn;
            s_tz_cur = z;
            s_tz_kills = 0;
            int left = tz_left();
            s_tz_warned = left <= 60 ? 3 : left <= 120 ? 2 : left <= 180 ? 1 : 0;
            tz_say_zone("BaseDesecratedFormat", &s_tz_zone[z]);
        } else if (s_tz_warned < 3 && tz_left() <= (3 - s_tz_warned) * 60) { /* los avisos de los últimos tres minutos */
            char key[48];
            snprintf(key, sizeof key, "DesecratedWarningDeactivatingTier%d", ++s_tz_warned);
            tz_say(str_get(key));
        }
    }
    tz_herald_tick();
}

/* El renglón de la pausa: "Zona de terror: Burial Grounds y Crypt (12 min)"; "" = no hay. Si
   no entra en max_w, solo el primer nivel ("Cold Plains +2") */
static void tz_pause_line(char *out, size_t cap, int max_w)
{
    char names[160];
    tz_begin();
    if (s_tz_on == 1 && s_tz_shard < 0 && s_tz_cur < 0) /* (todavía no corrió un tick: el turno de ahora) */
        s_tz_cur = getenv("D2_TZ_ZONE") ? atoi(getenv("D2_TZ_ZONE")) % s_tz_nzones : tz_zone_of((tz_now() - s_tz_start) / (s_tz_minutes * 60LL));
    tz_text(tz_current(), names, sizeof names);
    if (!names[0])
        out[0] = '\0';
    else if (s_tz_shard >= 0)
        snprintf(out, cap, tr("Zona de terror: %s"), names);
    else
        snprintf(out, cap, tr("Zona de terror: %s (%d min)"), names, (tz_left() + 59) / 60);
    const TzZone *z = tz_current();
    if (out[0] && font_width(out, 1) > max_w && z && s_tz_shard < 0 && z->n > 1) {
        snprintf(names, sizeof names, "%s +%d", tz_level_name(z->lv[0]), z->n - 1);
        snprintf(out, cap, tr("Zona de terror: %s (%d min)"), names, (tz_left() + 59) / 60);
    }
}
