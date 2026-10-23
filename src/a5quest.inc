/* a5quest.inc: las misiones del Acto 5 (lo incluye game.c, después de
   a4quest.inc). Como el original (D2MOO, QUESTS/ACT5/A5Q1..A5Q6):
   - El sitio de Harrogath (A5Q1, shenk_q): Larzuk la pide; Shenk el
     Capataz (Siege Boss), en el Bloody Foothills; Larzuk engarza un objeto.
   - Rescate en el monte Arreat (A5Q2, rescue_q): Qual-Kehk la pide; en el
     Frigid Highlands, cada una de las tres prisiones tiene una puerta
     (prisondoor) y cinco presos (act5pow, del objeto CagedWussie1:
     ACT5Q2_SpawnCagedWussies); al romper la puerta, los de adentro se van
     al pueblo; con los 15, Qual-Kehk da las runas Ral, Ort y Tal.
   - La prisión de hielo (A5Q3, anya_q): Malah la pide; Anya está congelada
     en el Frozen River (AnyaYetAgain, en su marcador DrehyaOutsideTown);
     al encontrarla, Malah da su poción (ice), que la descongela; Anya vuelve
     a Harrogath (DrehyaInTown) y Nihlathak se va; Malah da el Pergamino de
     Resistencia (tr2: +10 a todas las resistencias, una vez).
   - La traición de Harrogath (A5Q4, nihl_q): Anya abre el portal rojo al
     templo de Nihlathak; Nihlathak sale en los Halls of Vaught (su marcador
     NihlathakOutsideTown); Anya personaliza un objeto.
   - El rito de iniciación (A5Q5, ancients_q): Qual-Kehk la pide; en la
     cumbre, el altar (AncientsAltar) despierta a los tres Antiguos donde
     están sus estatuas (desde el nivel 20); al morir los tres, la
     experiencia del original (hasta el nivel siguiente) y se abre la
     puerta de la Fortaleza (AncientsGateway).
   - La víspera de la destrucción (A5Q6, baal_q): las cinco oleadas del
     Trono (monai5.inc); Baal se va a la Cámara por su portal (FinalPortal);
     muerto Baal, aparece Tyrael (ACT5Q6_SpawnTyrael) y, cuando se termina
     de hablar con él (ACT5Q6_Callback02_NpcDeactivate), abre el portal del
     final (NotKiddingLastPortal) al lado del que le habló: la cinemática
     d2x_out y de vuelta a Harrogath.
   Los portales rojos de las barricadas llevan a su infierno.
   Los demás NPC de Harrogath comentan cada etapa (las líneas A5Q* de
   k_talk_lines en game.c, como gpAct5Q1NpcMessages...). Los presos caminan
   hasta el portal que sale junto a la puerta rota (Fn131_Wussie). Con un
   portal abierto en la cumbre o todos los de la cumbre muertos, los
   Antiguos vuelven a sus estatuas y hay que tocar el altar de nuevo
   (ACT5Q5_DeactivateAncientMonsters). */

enum { A5_LARZUK = 30, A5_MALAH = 31, A5_QUAL = 32, A5_NIHL = 33, A5_CAIN = 34, A5_ANYA = 35, A5_TYRAEL = 36 }; /* (k_npc_vo) */

/* Los comentarios después del pedido (Player.a5_flags, después de A5F_ANCIENTS) */
enum { A5F_Q1_TOLD = 32, A5F_Q2_TOLD = 64, A5F_Q3_TOLD = 128, A5F_Q5_TOLD = 256 };

/* ---- Los portales rojos a los infiernos ----
   El portal permanente de cada barricada (sale de su baldosa: units.c,
   spawn_tile_unit) lleva a su infierno y allá sale otro de vuelta
   (OBJECTS_InitFunction12 y sub_6FC74A40: el Frigid Highlands a Abaddon,
   el Arreat Plateau al Pit of Acheron y la Frozen Tundra al Infernal Pit).
   Se llega a una de las baldosas de llegada de la Lava X (a5_hell_spot). */
#define PORTAL_HELL(k)      (8 + 2 * (k))
#define PORTAL_HELL_BACK(k) (9 + 2 * (k))
#define PORTAL_ANYA         14 /* de Harrogath al templo de Nihlathak */
#define PORTAL_ANYA_BACK    15
#define PORTAL_CHAMBER      20 /* del Trono a la Cámara (el de Baal) */
#define PORTAL_CHAMBER_BACK 21
static float s_a5_hell_x[3] = { -1, -1, -1 }, s_a5_hell_y[3];

/* Lo de esta partida */
static float s_a5_doors[3][2]; /* las puertas de las prisiones ya rotas (su lugar) */
static int   s_a5_ndoors;
static int   s_a5_shenk = -1, s_a5_nihl = -1, s_a5_baal = -1; /* sus unidades (-1 = no se sabe) */
static int   s_a5_ancient[3] = { -1, -1, -1 };
static int   s_a5_chamber;   /* el portal de Baal a la Cámara está abierto */
static int   s_a5_tyrael = -1; /* Tyrael en la Cámara (-1 = todavía no salió en esta partida) */

static int a5_still(int u, const char *id)
{
    return u >= 0 && u < units_count() && strcmp(units_id(u), id) == 0;
}

/* Dónde sale el portal de vuelta en el infierno k: el medio de su zona (el
   lugar libre más cerca, si la zona está armada) */
/* Donde se llega al infierno k (D2GAME_CreateLinkPortal con la baldosa 11
   de DUNGEON_FindActSpawnLocationEx): una de las baldosas especiales de
   estilo 33 de sus piezas (las de la Lava X, con Scan en lvlprest.txt),
   al azar con la semilla del mundo. Sin la zona armada, el medio (se
   corrige al armarla: use_warp relee el portal) */
static int a5_arrival_tile(const MapSpecial *s, int x0, int y0, int x1, int y1)
{
    return s->style == 33 && (s->orientation == 10 || s->orientation == 11) && s->x >= x0 && s->x < x1 && s->y >= y0 && s->y < y1;
}

static void a5_hell_spot(int k, float *x, float *y)
{
    int x0, y0, x1, y1, hz = world_level_zone(125 + k), n = 0;
    world_zone_rect(hz, &x0, &y0, &x1, &y1);
    *x = (x0 + x1) * TILE_SUB / 2.0f;
    *y = (y0 + y1) * TILE_SUB / 2.0f;
    if (!world_zone_loaded(hz))
        return;
    for (int i = 0; i < s_map.num_specials; i++)
        n += a5_arrival_tile(&s_map.specials[i], x0, y0, x1, y1);
    int pick = n > 0 ? (int)((s_world_seed ^ (uint32_t)(125 + k) * 2654435761u) % (uint32_t)n) : -1;
    for (int i = 0, c = 0; pick >= 0 && i < s_map.num_specials; i++)
        if (a5_arrival_tile(&s_map.specials[i], x0, y0, x1, y1) && c++ == pick) {
            *x = (float)s_map.specials[i].x * TILE_SUB + TILE_SUB / 2.0f;
            *y = (float)s_map.specials[i].y * TILE_SUB + TILE_SUB / 2.0f;
            break;
        }
    find_free_spot(x, y);
    if (getenv("D2_WARP_DEBUG"))
        printf("infierno %d: se llega a (%.0f, %.0f), una de %d baldosas de llegada (zona de %d,%d a %d,%d)\n", k + 1, *x, *y, n,
               x0 * TILE_SUB, y0 * TILE_SUB, x1 * TILE_SUB, y1 * TILE_SUB);
}

/* El medio de la zona del nivel (para llegar por un portal) */
static void a5_level_spot(int id, float *x, float *y)
{
    int x0, y0, x1, y1, z = world_level_zone(id);
    world_zone_rect(z, &x0, &y0, &x1, &y1);
    *x = (x0 + x1) * TILE_SUB / 2.0f;
    *y = (y0 + y1) * TILE_SUB / 2.0f;
    if (world_zone_loaded(z))
        find_free_spot(x, y);
}

/* El lugar de lo que sale al morir Baal: Tyrael (ACT5Q6_SpawnTyrael: 5 al
   noroeste del cadáver) y el portal del final (ACT5Q6_UnitIterate_CreateLastPortal:
   5 al este del que habló con Tyrael). El original corre los dos con
   QUESTS_GetFreePosition, el lugar más cerca sin objetos (COLLIDE_OBJECT) en
   11x11, y le alcanza porque su Cámara es una sala abierta. La nuestra tiene
   pasillos de diez subtiles: a 5 del cadáver el portal quedaba adentro de la
   pared y el bot daba vueltas a 4 de él hasta que se pasaba el tiempo. Acá
   es el subtile más cerca de (*x, *y) donde entra el cuerpo y al que se llega
   caminando desde (fx, fy). Si no queda a más de 6 de ese, mejor sin un NPC
   a menos de 3 ni un portal a menos de warp_r (para que A no se confunda: a
   Tyrael se le habla a 4 y un portal se usa a 2,5) y después, como en el
   original, sin objetos en 11x11. Sin ninguno, (fx, fy). */
#define A5_SPOT_R 24 /* (lo que se mira alrededor de fx, fy) */
#define A5_SPOT_W (2 * A5_SPOT_R + 1)
static float s_a5_spot_d[A5_SPOT_W * A5_SPOT_W];

static int a5_spot_cmp(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    float da = s_a5_spot_d[ia], db = s_a5_spot_d[ib];
    return da < db ? -1 : da > db ? 1 : ia - ib;
}

/* 0: despejado; 1: hay objetos en 11x11; 2: un NPC o un portal cerca */
static int a5_spot_tier(float x, float y, float warp_r)
{
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        float cx = (w->x0 + w->x1) / 2 - x, cy = (w->y0 + w->y1) / 2 - y;
        if (w->portal && world_zone_loaded(w->from) && cx * cx + cy * cy < warp_r * warp_r)
            return 2;
    }
    if (units_find_npc(x, y, 3.0f) >= 0)
        return 2;
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++)
            if (map_coll(&s_map, (int)floorf(x) + dx, (int)floorf(y) + dy) & COLL_OBJECT)
                return 1;
    return 0;
}

static void a5_free_spot(float fx, float fy, float *x, float *y, float warp_r)
{
    static uint8_t seen[A5_SPOT_W * A5_SPOT_W];
    static int queue[A5_SPOT_W * A5_SPOT_W], cand[A5_SPOT_W * A5_SPOT_W];
    static const int k_dx[4] = { 1, -1, 0, 0 }, k_dy[4] = { 0, 0, 1, -1 };
    int ox = (int)floorf(fx) - A5_SPOT_R, oy = (int)floorf(fy) - A5_SPOT_R, qh = 0, qt = 0, nc = 0;
    float sx = fx, sy = fy; /* (parado contra una pared: desde el lugar libre de al lado) */
    if (blocked_body(floorf(fx) + 0.5f, floorf(fy) + 0.5f))
        find_free_spot(&sx, &sy);
    int s = ((int)floorf(sy) - oy) * A5_SPOT_W + ((int)floorf(sx) - ox);
    if (fabsf(sx - fx) >= A5_SPOT_R || fabsf(sy - fy) >= A5_SPOT_R)
        s = A5_SPOT_R * A5_SPOT_W + A5_SPOT_R;
    memset(seen, 0, sizeof seen);
    seen[s] = 1;
    queue[qt++] = s;
    while (qh < qt) { /* lo que se camina desde ahí (de a un subtile, sin diagonales) */
        int c = queue[qh++], cx = c % A5_SPOT_W, cy = c / A5_SPOT_W;
        float mx = ox + cx + 0.5f, my = oy + cy + 0.5f;
        if (!blocked_body(mx, my)) {
            s_a5_spot_d[c] = (mx - *x) * (mx - *x) + (my - *y) * (my - *y);
            cand[nc++] = c;
        }
        for (int k = 0; k < 4; k++) {
            int nx = cx + k_dx[k], ny = cy + k_dy[k];
            if (nx < 0 || ny < 0 || nx >= A5_SPOT_W || ny >= A5_SPOT_W || seen[ny * A5_SPOT_W + nx] ||
                blocked_body(ox + nx + 0.5f, oy + ny + 0.5f))
                continue;
            seen[ny * A5_SPOT_W + nx] = 1;
            queue[qt++] = ny * A5_SPOT_W + nx;
        }
    }
    qsort(cand, (size_t)nc, sizeof cand[0], a5_spot_cmp);
    int pick = -1, pick_tier = 3;
    float limit = 0.0f;
    for (int i = 0; i < nc && pick_tier > 0; i++) { /* el más cerca sin nadie encima; mejor si está despejado */
        float mx = ox + cand[i] % A5_SPOT_W + 0.5f, my = oy + cand[i] / A5_SPOT_W + 0.5f;
        if (pick >= 0 && s_a5_spot_d[cand[i]] > limit)
            break;
        if (units_blocking(mx, my, UNIT_PLAYER_BODY + 0.15f))
            continue;
        if (pick < 0)
            limit = (sqrtf(s_a5_spot_d[cand[i]]) + 6.0f) * (sqrtf(s_a5_spot_d[cand[i]]) + 6.0f);
        int tier = a5_spot_tier(mx, my, warp_r);
        if (tier < pick_tier) {
            pick = cand[i];
            pick_tier = tier;
        }
    }
    *x = pick >= 0 ? ox + pick % A5_SPOT_W + 0.5f : fx;
    *y = pick >= 0 ? oy + pick / A5_SPOT_W + 0.5f : fy;
}

/* El marcador (objeto invisible) cls de la zona z: 0 si está */
static int a5_marker(const char *cls, int z, float *x, float *y)
{
    int ids[4], n = units_find_all(cls, ids, 4);
    for (int i = 0; i < n; i++)
        if (units_zone(ids[i]) == z) {
            units_position(ids[i], 1.0f, x, y);
            return 0;
        }
    return -1;
}

/* ¿Ya rota esa puerta de prisión? */
static int a5_door_broken(float x, float y)
{
    for (int k = 0; k < s_a5_ndoors; k++)
        if (fabsf(s_a5_doors[k][0] - x) < 3.0f && fabsf(s_a5_doors[k][1] - y) < 3.0f)
            return 1;
    return 0;
}

/* El portal rojo de Anya (ACT5Q4_AnyaOpenPortal: a su lado) y el de vuelta en el templo */
static void a5_anya_portal(void)
{
    int town = world_level_zone(109), temple = world_level_zone(121);
    float ax, ay, tx, ty;
    if (town < 0 || temple < 0 || units_find("drehya", &ax, &ay) != 0)
        return;
    ax += 4.0f;
    ay += 2.0f;
    if (world_zone_loaded(town))
        find_free_spot(&ax, &ay);
    a5_level_spot(121, &tx, &ty);
    world_portal(PORTAL_ANYA, town, ax, ay, temple, tx + 2.0f, ty + 2.0f);
    world_portal(PORTAL_ANYA_BACK, temple, tx, ty, town, ax + 2.0f, ay + 2.0f);
    a2_portal_object(town, "PortalPermanent", ax, ay);
    a2_portal_object(temple, "PortalPermanent", tx, ty);
}

/* El portal de Baal del Trono a la Cámara (ACT5Q6_OpenWorldStoneChamber) */
static void a5_chamber_portal(void)
{
    int throne = world_level_zone(131), chamber = world_level_zone(132), id;
    float px, py, cx, cy;
    if (throne < 0 || chamber < 0 || units_find_all("FinalPortal", &id, 1) != 1)
        return;
    units_position(id, 1.0f, &px, &py);
    find_free_spot(&px, &py);
    a5_level_spot(132, &cx, &cy);
    world_portal(PORTAL_CHAMBER, throne, px, py, chamber, cx, cy + 4.0f);
    world_portal(PORTAL_CHAMBER_BACK, chamber, cx, cy, throne, px + 2.0f, py + 2.0f);
    units_object_mode(id, "on", NULL);
    a2_portal_object(chamber, "PortalPermanent", cx, cy);
}

/* Los Antiguos, donde están sus estatuas (las estatuas se van) */
static const char *const k_a5_statues[3] = { "ancientstatue1", "ancientstatue2", "ancientstatue3" };
static float s_a5_statue[3][3] = { { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 } }; /* dónde estaba cada estatua (x, y, zona; -1 = no se sabe) */

static void a5_wake_ancients(void)
{
    static const char *const k_names[3] = { "Ancient Barbarian 1", "Ancient Barbarian 2", "Ancient Barbarian 3" };
    for (int k = 0; k < 3; k++) {
        int id;
        float x, y;
        if (a5_still(s_a5_ancient[k], k_names[k]) || units_find_all(k_a5_statues[k], &id, 1) != 1)
            continue;
        units_position(id, 1.0f, &x, &y);
        int z = units_zone(id);
        s_a5_statue[k][0] = x;
        s_a5_statue[k][1] = y;
        s_a5_statue[k][2] = (float)z;
        units_remove(id);
        const Level *lv = world_level_at(x, y);
        units_zone_begin(z);
        s_a5_ancient[k] = units_spawn_superunique(k_names[k], x, y + 2.0f, lv ? lv->mon_level : 40);
        units_zone_end();
    }
    units_set_ancients(1);
}

/* Los Antiguos vuelven a ser estatuas (ACT5Q5_DeactivateAncientMonsters):
   con un portal abierto en la cumbre o todos los de la cumbre muertos. Los
   tres (también los que ya habían caído) y el altar quedan como antes: hay
   que tocarlo de nuevo */
static void a5_reset_ancients(void)
{
    static const char *const k_names[3] = { "Ancient Barbarian 1", "Ancient Barbarian 2", "Ancient Barbarian 3" };
    int id;
    for (int k = 0; k < 3; k++) {
        if (a5_still(s_a5_ancient[k], k_names[k]))
            units_remove(s_a5_ancient[k]);
        s_a5_ancient[k] = -1;
        int z = (int)s_a5_statue[k][2];
        if (z >= 0 && world_zone_loaded(z) && units_find_all(k_a5_statues[k], &id, 1) == 0) {
            const Level *lv = world_level_at(s_a5_statue[k][0], s_a5_statue[k][1]);
            units_zone_begin(z);
            units_spawn_monster(k_a5_statues[k], s_a5_statue[k][0], s_a5_statue[k][1], lv ? lv->mon_level : 40);
            units_zone_end();
        }
    }
    if (units_find_all("AncientsAltar", &id, 1) == 1)
        units_object_mode(id, "nu", NULL);
    units_set_ancients(0);
    for (int i = 0; i < s_num_players; i++)
        if (s_pl[i].ancients_q == 2)
            s_pl[i].ancients_q = 1;
    quest_banner("Los Antiguos volvieron a sus estatuas", NULL);
}

static void a5_portal_opened(const Player *p)
{
    if (p->level && p->level->id == 120 && party_max(offsetof(Player, ancients_q)) == 2)
        a5_reset_ancients();
}

static void a5_player_died(const Player *p)
{
    if (!p->level || p->level->id != 120 || party_max(offsetof(Player, ancients_q)) != 2)
        return;
    for (int i = 0; i < s_num_players; i++)
        if (s_pl[i].state < PS_DYING && s_pl[i].level && s_pl[i].level->id == 120)
            return; /* queda alguien vivo en la cumbre */
    a5_reset_ancients();
}

/* Los objetivos del registro (qstsa5q21...; el sitio no tiene los suyos) */
static const char *quest_goal5(const Player *p, int q, int *state)
{
    switch (q) {
    case 0: /* shenk_q: 1 Larzuk la pidió, 2 murió Shenk, 3 Larzuk lo supo (engarza), 4 engarzó */
        *state = p->shenk_q >= 3 ? 2 : p->shenk_q > 0 ? 1 : 0;
        return p->shenk_q >= 3 ? NULL : p->shenk_q == 2 ? tr("Hablá con Larzuk.") : tr("Matá a Shenk el Capataz en el Bloody Foothills.");
    case 1: /* rescue_q: 1 la pidió Qual-Kehk, 2 los 15 libres, 3 las runas */
        *state = p->rescue_q >= 3 ? 2 : p->rescue_q > 0 ? 1 : 0;
        return p->rescue_q >= 3 ? NULL : p->rescue_q == 2 ? "qstsa5q23" : p->rescued > 0 ? "qstsa5q22" : "qstsa5q21";
    case 2: /* anya_q: 1 Malah la pidió, 2 la encontró, 3 tiene la poción, 4 la descongeló, 5 Malah dio el pergamino */
        *state = p->anya_q >= 5 ? 2 : p->anya_q > 0 ? 1 : 0;
        return p->anya_q >= 5 ? NULL : p->anya_q == 4 ? "qstsa5q34" : p->anya_q == 3 ? "qstsa5q33" : p->anya_q == 2 ? "qstsa5q32" : "qstsa5q31";
    case 3: /* nihl_q: 1 Anya abrió el portal, 2 llegó al templo, 3 murió Nihlathak, 4 Anya lo supo (personaliza) */
        *state = p->nihl_q >= 4 ? 2 : p->nihl_q > 0 ? 1 : 0;
        return p->nihl_q >= 4 ? NULL : p->nihl_q == 3 ? "qstsa5q43a" : p->nihl_q == 2 ? "qstsa5q42" : "qstsa5q41";
    case 4: /* ancients_q: 1 la pidió Qual-Kehk, 2 despertó a los Antiguos, 3 los venció */
        *state = p->ancients_q >= 3 ? 2 : p->ancients_q > 0 ? 1 : 0;
        return p->ancients_q >= 3 ? NULL : p->ancients_q == 2 ? "qstsa5q53" : p->level && p->level->id == 120 ? "qstsa5q52" : "qstsa5q51";
    default: /* baal_q: 1 los Antiguos hablaron, 2 el Trono, 3 Baal se fue a la Cámara, 4 murió Baal, 5 Tyrael habló, 6 el final */
        *state = p->baal_q >= 6 ? 2 : p->baal_q > 0 ? 1 : 0;
        return p->baal_q >= 6 ? NULL : p->baal_q == 5 ? "qstsa5q64" : p->baal_q == 4 ? "qstsa5q63" : p->baal_q == 3 ? "qstsa5q62a"
                                       : p->baal_q == 2 ? "qstsa5q62b" : "qstsa5q61a";
    }
}

/* Lo que tiene para decir cada uno */
enum {
    A5N_NONE, A5N_Q1_ASK, A5N_Q1_TOLD, A5N_Q1_DONE, A5N_Q2_ASK, A5N_Q2_TOLD, A5N_Q2_DONE, A5N_Q3_ASK, A5N_Q3_TOLD,
    A5N_Q3_FOUND, A5N_Q3_DONE, A5N_ANYA_THANKS, A5N_Q4_ASK, A5N_Q4_DONE, A5N_Q5_ASK, A5N_Q5_TOLD, A5N_Q5_DONE, A5N_Q6_DONE,
    A5N_Q6_TYRAEL,
};

static int a5_npc_pick(const Player *p, int who)
{
    switch (who) {
    case A5_LARZUK:
        if (p->shenk_q == 0)
            return A5N_Q1_ASK;
        if (p->shenk_q == 2)
            return A5N_Q1_DONE;
        if (p->shenk_q == 1 && !(p->a5_flags & A5F_Q1_TOLD))
            return A5N_Q1_TOLD;
        return A5N_NONE;
    case A5_QUAL:
        if (p->rescue_q == 0)
            return A5N_Q2_ASK;
        if (p->rescue_q == 2)
            return A5N_Q2_DONE;
        if (p->rescue_q == 1 && !(p->a5_flags & A5F_Q2_TOLD))
            return A5N_Q2_TOLD;
        if (p->ancients_q == 0 && p->rescue_q >= 3)
            return A5N_Q5_ASK;
        if (p->ancients_q >= 3 && !(p->a5_flags & A5F_Q5_TOLD))
            return A5N_Q5_DONE;
        if (p->ancients_q == 1 && !(p->a5_flags & A5F_Q5_TOLD))
            return A5N_Q5_TOLD;
        return A5N_NONE;
    case A5_MALAH:
        if (p->anya_q == 0)
            return A5N_Q3_ASK;
        if (p->anya_q == 2)
            return A5N_Q3_FOUND;
        if (p->anya_q == 4)
            return A5N_Q3_DONE;
        if (p->anya_q == 1 && !(p->a5_flags & A5F_Q3_TOLD))
            return A5N_Q3_TOLD;
        return A5N_NONE;
    case A5_ANYA:
        if (p->anya_q >= 4 && !(p->a5_flags & A5F_ANYA_TOLD))
            return A5N_ANYA_THANKS;
        if ((p->a5_flags & A5F_ANYA_TOLD) && p->nihl_q == 0)
            return A5N_Q4_ASK;
        if (p->nihl_q == 3)
            return A5N_Q4_DONE;
        return A5N_NONE;
    case A5_CAIN:
        return p->baal_q >= 4 && p->baal_q < 6 ? A5N_Q6_DONE : A5N_NONE;
    case A5_TYRAEL: /* (siempre lo mismo, como el original) */
        return p->baal_q >= 4 ? A5N_Q6_TYRAEL : A5N_NONE;
    default:
        return A5N_NONE;
    }
}

static int a5_npc_news(const Player *p, int who)
{
    return a5_npc_pick(p, who) != A5N_NONE;
}

/* Lo dice y hace lo que corresponde; 1 si dijo algo */
static int a5_npc_quest(Player *p, int who, char *key, size_t cap)
{
    switch (a5_npc_pick(p, who)) {
    case A5N_Q1_ASK:
        party_raise(offsetof(Player, shenk_q), 1);
        snprintf(key, cap, "A5Q1InitLarzuk");
        return 1;
    case A5N_Q1_TOLD:
        p->a5_flags |= A5F_Q1_TOLD;
        snprintf(key, cap, "A5Q1AfterInitLarzuk");
        return 1;
    case A5N_Q1_DONE: /* la recompensa: Larzuk engarza un objeto (la opción Engarzar) */
        p->shenk_q = 3;
        quest_done("qstsa5q1", NULL);
        snprintf(key, cap, "A5Q1SuccessfulLarzuk");
        return 1;
    case A5N_Q2_ASK:
        party_raise(offsetof(Player, rescue_q), 1);
        snprintf(key, cap, "A5Q2InitQualKehk");
        return 1;
    case A5N_Q2_TOLD:
        p->a5_flags |= A5F_Q2_TOLD;
        snprintf(key, cap, "A5Q2AfterInitQualKehk");
        return 1;
    case A5N_Q2_DONE: { /* las runas Ral, Ort y Tal (gdwAct5Q2RuneCodes) */
        static const char *const k_runes[3] = { "r08", "r09", "r07" };
        p->rescue_q = 3;
        for (int k = 0; k < 3; k++)
            give_quest_item(p, k_runes[k]);
        quest_done("qstsa5q2", NULL);
        snprintf(key, cap, "A5Q2SuccessfulQualKehk");
        return 1;
    }
    case A5N_Q3_ASK:
        party_raise(offsetof(Player, anya_q), 1);
        snprintf(key, cap, "A5Q3InitMalah");
        return 1;
    case A5N_Q3_TOLD:
        p->a5_flags |= A5F_Q3_TOLD;
        snprintf(key, cap, "A5Q3AfterInitMalah");
        return 1;
    case A5N_Q3_FOUND: /* la poción de Malah (una por partida, si no la tiene) */
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].anya_q == 2)
                s_pl[i].anya_q = 3;
        if (!player_has(p, "ice"))
            give_quest_item(p, "ice");
        snprintf(key, cap, "A5Q3FoundAnyaMalah");
        return 1;
    case A5N_Q3_DONE: /* el Pergamino de Resistencia */
        p->anya_q = 5;
        give_quest_item(p, "tr2");
        quest_done("qstsa5q3", NULL);
        snprintf(key, cap, "A5Q3SuccessfulMalah");
        return 1;
    case A5N_ANYA_THANKS:
        p->a5_flags |= A5F_ANYA_TOLD;
        snprintf(key, cap, "A5Q3SuccessfulAnya");
        return 1;
    case A5N_Q4_ASK: /* y abre el portal rojo al templo */
        party_raise(offsetof(Player, nihl_q), 1);
        for (int i = 0; i < s_num_players; i++)
            s_pl[i].a5_flags |= A5F_PORTAL;
        a5_anya_portal();
        snprintf(key, cap, "A5Q4InitAnya");
        return 1;
    case A5N_Q4_DONE: /* la recompensa: personaliza un objeto (la opción Personalizar) */
        p->nihl_q = 4;
        quest_done("qstsa5q4", NULL);
        snprintf(key, cap, "A5Q4SuccessfulAnya");
        return 1;
    case A5N_Q5_ASK:
        party_raise(offsetof(Player, ancients_q), 1);
        snprintf(key, cap, "A5Q5InitQualKehk");
        return 1;
    case A5N_Q5_TOLD:
        p->a5_flags |= A5F_Q5_TOLD;
        snprintf(key, cap, "A5Q5AfterInitQualKehk");
        return 1;
    case A5N_Q5_DONE:
        p->a5_flags |= A5F_Q5_TOLD;
        snprintf(key, cap, "A5Q5SuccessfulQualKehk");
        return 1;
    case A5N_Q6_DONE:
        snprintf(key, cap, "A5Q6SuccessfulCain");
        return 1;
    case A5N_Q6_TYRAEL: /* el portal lo abre al cerrar el diálogo (a5_dialog_closed) */
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].baal_q == 4)
                s_pl[i].baal_q = 5;
        snprintf(key, cap, "A5Q6SuccessfulTyrael");
        return 1;
    default:
        return 0;
    }
}

/* Se terminó de hablar con un NPC: con Tyrael en la Cámara, el portal del
   final, 5 al este del que le habló (ACT5Q6_Callback02_NpcDeactivate y
   ACT5Q6_UnitIterate_CreateLastPortal; uno por partida) */
static void a5_dialog_closed(const Player *p, int npc)
{
    int id;
    if (npc < 0 || npc >= units_count() || strcmp(units_id(npc), "tyrael3") != 0 || p->baal_q < 5 ||
        units_find_all("NotKiddingLastPortal", &id, 1) > 0)
        return;
    float x = p->x + 5.0f, y = p->y;
    a5_free_spot(p->x, p->y, &x, &y, 4.0f);
    units_zone_begin(zone_at(x, y));
    units_spawn_object("NotKiddingLastPortal", x, y, "on");
    units_zone_end();
    audio_play_name("object_xportalopen", x, y);
}

/* El nivel que pide el rito de los Antiguos: 20, 40 o 60 (20 * (nDifficulty + 1), ACT5Q5) */
static int a5_rite_level(void)
{
    return 20 * (s_diff + 1);
}

static const char *a5_rite_text(void)
{
    static char buf[64];
    snprintf(buf, sizeof buf, tr("Hace falta el nivel %d para el rito"), a5_rite_level());
    return buf;
}

/* El objeto de misión del Acto 5 que tiene al lado: qué se puede hacer (NULL = nada) */
static const char *a5_action(const Player *p, const char *cls)
{
    if (strcmp(cls, "AnyaYetAgain") == 0 && p->anya_q < 4)
        return player_has(p, "ice") ? tr("A: Usar la poción de Malah") : tr("Anya está congelada: hace falta la poción de Malah");
    if (strcmp(cls, "AncientsAltar") == 0 && p->ancients_q >= 1 && p->ancients_q < 2)
        return p->level_num >= a5_rite_level() ? tr("A: Tocar el altar") : a5_rite_text();
    if (strcmp(cls, "NotKiddingLastPortal") == 0)
        return tr("A: Entrar al portal");
    return NULL;
}

/* Lo que pasa al usar un objeto del Acto 5: 1 si era uno de estos */
static int a5_interact(Player *p, const char *cls, int u, float ux, float uy)
{
    if (strcmp(cls, "AnyaYetAgain") == 0) { /* la poción la descongela y vuelve al pueblo (OperateFunction67) */
        if (p->anya_q >= 4 || !player_has(p, "ice"))
            return 1;
        take_item(p, "ice");
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].anya_q < 4)
                s_pl[i].anya_q = 4;
        missiles_effect("teleport", ux, uy);
        units_remove(u);
        quest_banner("Anya está libre: volvió a Harrogath", "music_quest_anya");
        return 1;
    }
    if (strcmp(cls, "AncientsAltar") == 0) { /* el rito: despiertan los tres Antiguos */
        if (p->ancients_q != 1 || p->level_num < a5_rite_level())
            return 1;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].ancients_q == 1)
                s_pl[i].ancients_q = 2;
        units_object_mode(u, "op", "on");
        a5_wake_ancients();
        return 1;
    }
    if (strcmp(cls, "NotKiddingLastPortal") == 0) { /* el final (d2x_out) y a Harrogath */
        if (party_lacks(offsetof(Player, a5_flags), A5F_END))
            queue_movie("d2x_out");
        for (int i = 0; i < s_num_players; i++) {
            s_pl[i].a5_flags |= A5F_END;
            if (s_pl[i].baal_q < 6)
                s_pl[i].baal_q = 6;
        }
        quest_done("qstsa5q6", NULL);
        int k = wp_first(5); /* (al lado del waypoint de Harrogath) */
        zone_load(world_waypoint_zone(k));
        if (s_wp_unit[k] >= 0) {
            float tx, ty;
            units_position(s_wp_unit[k], 1.0f, &tx, &ty);
            for (int i = 0; i < s_num_players; i++) {
                s_pl[i].x = tx + 2.0f + (float)i;
                s_pl[i].y = ty + 2.0f;
                find_free_spot(&s_pl[i].x, &s_pl[i].y);
                s_pl[i].prev_x = s_pl[i].x;
                s_pl[i].prev_y = s_pl[i].y;
            }
        }
        return 1;
    }
    (void)ux;
    (void)uy;
    return 0;
}

/* El Pergamino de Resistencia: +10 a todas, una vez */
static int a5_use_item(Player *p, int i)
{
    if (strcmp(p->bag[i].type->code, "tr2") != 0)
        return 0;
    if (p->a5_flags & A5F_RESIST) {
        show_message(p, "Ya leíste este pergamino");
        return 1;
    }
    p->a5_flags |= A5F_RESIST;
    bag_remove(p, i);
    refresh_stats(p);
    audio_play_name("item_scroll", AUDIO_NOPOS, AUDIO_NOPOS);
    show_message(p, "Ganaste 10 de resistencia a todo");
    save_character(p);
    return 1;
}

/* La puerta de la Fortaleza (de la cumbre) se abre al vencer a los Antiguos */
static const char *a5_warp_closed(const Warp *w)
{
    if (w->from == world_level_zone(120) && w->to == world_level_zone(128) && party_max(offsetof(Player, ancients_q)) < 3)
        return tr("La puerta está cerrada: hay que vencer a los Antiguos");
    return NULL;
}

static void a5_zone_loaded(int z)
{
    static const int k_from[3] = { 111, 112, 117 };
    for (int k = 0; k < 3; k++) {
        int bz = world_level_zone(k_from[k]), hz = world_level_zone(125 + k), ids[4], n = units_find_all("PortalPermanent", ids, 4);
        float hx, hy;
        if (bz < 0 || hz < 0)
            continue;
        for (int i = 0; i < n && z == bz; i++)
            if (units_zone(ids[i]) == bz) {
                /* (el portal está metido en el portón: se usa desde el lugar libre de adelante) */
                units_position(ids[i], 1.0f, &s_a5_hell_x[k], &s_a5_hell_y[k]);
                find_free_spot(&s_a5_hell_x[k], &s_a5_hell_y[k]);
            }
        if ((z == bz || z == hz) && s_a5_hell_x[k] >= 0) {
            a5_hell_spot(k, &hx, &hy);
            world_portal(PORTAL_HELL(k), bz, s_a5_hell_x[k], s_a5_hell_y[k], hz, hx + 2.0f, hy + 2.0f);
            world_portal(PORTAL_HELL_BACK(k), hz, hx, hy, bz, s_a5_hell_x[k] + 2.0f, s_a5_hell_y[k] + 2.0f);
            if (z == hz)
                a2_portal_object(hz, "PortalPermanent", hx, hy);
        }
    }
    int rescue = party_max(offsetof(Player, rescue_q)), anya = party_max(offsetof(Player, anya_q));
    int nihl = party_max(offsetof(Player, nihl_q)), ancients = party_max(offsetof(Player, ancients_q));
    int ids[8], n;
    float x, y;
    /* Las prisiones: las puertas ya rotas siguen rotas; en las otras, los cinco presos */
    if (z == world_level_zone(111)) {
        n = units_find_all("prisondoor", ids, 8);
        for (int i = 0; i < n; i++) {
            units_position(ids[i], 1.0f, &x, &y);
            if (units_zone(ids[i]) == z && (a5_door_broken(x, y) || rescue >= 2))
                units_play_death(ids[i]);
        }
        n = units_find_all("CagedWussie1", ids, 8);
        for (int i = 0; i < n && rescue < 2; i++) {
            units_position(ids[i], 1.0f, &x, &y);
            if (units_zone(ids[i]) != z || a5_door_broken(x, y))
                continue;
            const Level *lv = world_level_at(x, y);
            units_zone_begin(z);
            for (int k = 0; k < 5; k++)
                units_spawn_neutral("act5pow", x + (float)(k % 3 - 1) * 1.5f, y + (float)(k / 3) * 1.5f, lv ? lv->mon_level : 25);
            units_zone_end();
        }
    }
    /* Anya: congelada en el Frozen River hasta la poción; después en Harrogath (y Nihlathak se fue) */
    if (anya < 4 && a5_marker("DrehyaOutsideTown", z, &x, &y) == 0 && units_find_all("AnyaYetAgain", ids, 1) == 0) {
        units_zone_begin(z);
        units_spawn_object("AnyaYetAgain", x, y, NULL);
        units_zone_end();
    }
    if (anya >= 4 && a5_marker("DrehyaInTown", z, &x, &y) == 0 && units_find_all("drehya", ids, 1) == 0) {
        units_zone_begin(z);
        units_spawn_npc("drehya", x, y);
        units_zone_end();
    }
    if (anya >= 4 && (n = units_find_all("nihlathak", ids, 1)) == 1 && units_zone(ids[0]) == z)
        units_remove(ids[0]);
    if (party_max(offsetof(Player, a5_flags)) & A5F_PORTAL && (z == world_level_zone(109) || z == world_level_zone(121)))
        a5_anya_portal();
    /* Nihlathak, en los Halls of Vaught (su marcador), con la misión empezada y vivo */
    if (nihl >= 1 && nihl < 3 && !a5_still(s_a5_nihl, "Nihlathak Boss") && a5_marker("NihlathakOutsideTown", z, &x, &y) == 0) {
        const Level *lv = world_level_at(x, y);
        units_zone_begin(z);
        s_a5_nihl = units_spawn_superunique("Nihlathak Boss", x, y, lv ? lv->mon_level : 40);
        units_zone_end();
    }
    /* La cumbre: con los Antiguos vencidos, ni estatuas ni Antiguos, y la puerta abierta */
    if (z == world_level_zone(120)) {
        for (int k = 0; k < 3 && ancients >= 3; k++)
            if (units_find_all(k_a5_statues[k], ids, 1) == 1)
                units_remove(ids[0]);
        if (ancients >= 3 && units_find_all("AncientsGateway", ids, 1) == 1)
            units_object_mode(ids[0], "on", NULL);
        if (ancients == 2)
            a5_wake_ancients();
    }
    if (s_a5_chamber && (z == world_level_zone(131) || z == world_level_zone(132)))
        a5_chamber_portal();
}

/* Cada tick, en el Acto 5 */
static void a5_quest_tick(void)
{
    int id, ids[8], n;
    float x, y;
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        if (!p->level)
            continue;
        if (p->level->id == 121 && p->nihl_q == 1) /* llegó al templo */
            p->nihl_q = 2;
        if (p->level->id == 131 && p->baal_q >= 1 && p->baal_q < 2) /* y al Trono */
            p->baal_q = 2;
        /* encontró a Anya (a menos de 12 subtiles de ella) */
        if (p->anya_q == 1 && units_find("AnyaYetAgain", &x, &y) == 0 && (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) < 144.0f) {
            for (int j = 0; j < s_num_players; j++)
                if (s_pl[j].anya_q == 1)
                    s_pl[j].anya_q = 2;
            quest_banner("Encontraste a Anya: hablá con Malah", "music_quest_anya");
        }
    }
    /* Anya vuelve a Harrogath y Nihlathak se va (el pueblo no se vuelve a armar) */
    int town = world_level_zone(109), vaught = world_level_zone(124);
    if (party_max(offsetof(Player, anya_q)) >= 4 && town >= 0 && world_zone_loaded(town)) {
        if (units_find_all("drehya", ids, 1) == 0 && a5_marker("DrehyaInTown", town, &x, &y) == 0) {
            units_zone_begin(town);
            units_spawn_npc("drehya", x, y);
            units_zone_end();
        }
        if (units_find_all("nihlathak", ids, 1) == 1 && units_zone(ids[0]) == town)
            units_remove(ids[0]);
    }
    /* Nihlathak en los Halls of Vaught, si ya estaban armados al empezar la misión */
    n = party_max(offsetof(Player, nihl_q));
    if (n >= 1 && n < 3 && vaught >= 0 && world_zone_loaded(vaught) && !a5_still(s_a5_nihl, "Nihlathak Boss") &&
        a5_marker("NihlathakOutsideTown", vaught, &x, &y) == 0) {
        const Level *lv = world_level_at(x, y);
        units_zone_begin(vaught);
        s_a5_nihl = units_spawn_superunique("Nihlathak Boss", x, y, lv ? lv->mon_level : 40);
        units_zone_end();
    }
    /* Shenk (ACT5Q1_OnSiegeBossKilled) */
    if (party_max(offsetof(Player, shenk_q)) < 2 && units_find_all("Siege Boss", &id, 1) == 1 && units_is_corpse(id)) {
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].shenk_q < 2)
                s_pl[i].shenk_q = 2;
        quest_banner("Shenk murió: hablá con Larzuk", "music_quest_shenk");
        save_party();
    }
    /* Las puertas de las prisiones: al romperse sale un portal al lado de la puerta (OBJECT_CAINPORTAL, a
       2 subtiles: ACT5Q2_UpdateQuestState) y los presos de adentro (a menos de 15) caminan hasta él y se
       van (Fn131_Wussie). Se cuentan al romper la puerta: si la zona se desarma antes de que lleguen, no
       se pierden. */
    n = units_find_all("prisondoor", ids, 8);
    for (int i = 0; i < n && s_a5_ndoors < 3; i++) {
        if (!units_is_corpse(ids[i]))
            continue;
        units_position(ids[i], 1.0f, &x, &y);
        if (a5_door_broken(x, y))
            continue;
        s_a5_doors[s_a5_ndoors][0] = x;
        s_a5_doors[s_a5_ndoors][1] = y;
        s_a5_ndoors++;
        int freed = 0, pows[16], np = units_find_all("act5pow", pows, 16);
        units_zone_begin(units_zone(ids[i]));
        units_spawn_object("CainPortal", x + 2.0f, y, "on");
        units_zone_end();
        for (int k = 0; k < np; k++) {
            float px, py;
            units_position(pows[k], 1.0f, &px, &py);
            if ((px - x) * (px - x) + (py - y) * (py - y) < 225.0f) {
                units_walk_to(pows[k], x + 2.0f, y);
                freed++;
            }
        }
        for (int j = 0; j < s_num_players; j++) {
            s_pl[j].rescued = s_pl[j].rescued + 5 > 15 ? 15 : s_pl[j].rescued + 5;
            if (s_pl[j].rescue_q == 1 && s_pl[j].rescued >= 15)
                s_pl[j].rescue_q = 2;
        }
        char msg[64];
        snprintf(msg, sizeof msg, s_pl[0].rescued >= 15 ? "Liberaste a los 15 soldados: hablá con Qual-Kehk" : "Liberaste a %d soldados",
                 s_pl[0].rescued);
        quest_banner(msg, NULL);
        (void)freed;
        save_party();
    }
    /* Los presos que llegaron a su portal (a menos de 4) se van */
    if (s_a5_ndoors > 0 && (n = units_find_all("act5pow", ids, 8)) > 0)
        for (int k = n - 1; k >= 0; k--) {
            units_position(ids[k], 1.0f, &x, &y);
            for (int d = 0; d < s_a5_ndoors; d++) {
                float dx = x - (s_a5_doors[d][0] + 2.0f), dy = y - s_a5_doors[d][1];
                if (dx * dx + dy * dy < 16.0f) {
                    units_remove(ids[k]);
                    break;
                }
            }
        }
    /* Nihlathak */
    if (a5_still(s_a5_nihl, "Nihlathak Boss") && units_is_corpse(s_a5_nihl)) {
        s_a5_nihl = -1;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].nihl_q >= 1 && s_pl[i].nihl_q < 3)
                s_pl[i].nihl_q = 3;
        quest_banner("Nihlathak murió: hablá con Anya", "music_quest_nihlathak");
        save_party();
    }
    /* Los Antiguos: con los tres muertos, la experiencia y empieza la víspera */
    if (party_max(offsetof(Player, ancients_q)) == 2) {
        int dead = 0;
        for (int k = 0; k < 3; k++)
            dead += a5_still(s_a5_ancient[k], k == 0 ? "Ancient Barbarian 1" : k == 1 ? "Ancient Barbarian 2" : "Ancient Barbarian 3") &&
                    units_is_corpse(s_a5_ancient[k]);
        if (dead == 3) {
            units_set_ancients(0);
            for (int i = 0; i < s_num_players; i++) {
                Player *p = &s_pl[i];
                if (p->ancients_q == 2) {
                    p->ancients_q = 3;
                    /* ACT5Q5_RewardPlayer: lo que va del nivel al siguiente, hasta 1.400.000
                       en Normal, 20 millones en Pesadilla y 40 en Infierno */
                    static const uint32_t k_cap[3] = { 1400000, 20000000, 40000000 };
                    const CharClass *c = &s_classes[p->cls];
                    if (p->level_num < 99) {
                        uint32_t band = c->exp_next[p->level_num] - (p->level_num > 1 ? c->exp_next[p->level_num - 1] : 0);
                        gain_exp(p, band < k_cap[s_diff] ? band : k_cap[s_diff]);
                    }
                }
                p->a5_flags |= A5F_ANCIENTS;
                if (p->baal_q < 1)
                    p->baal_q = 1;
            }
            if (units_find_all("AncientsGateway", &id, 1) == 1)
                units_object_mode(id, "op", "on");
            quest_done("qstsa5q5", "music_quest_ancients");
            save_party();
        }
    }
    /* Baal: se fue a la Cámara (BaalToStairs) */
    if (units_baal_left()) {
        s_a5_chamber = 1;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].baal_q >= 1 && s_pl[i].baal_q < 3)
                s_pl[i].baal_q = 3;
        a5_chamber_portal();
        quest_banner("Baal se fue a la Cámara de la Piedra del Mundo", NULL);
        save_party();
    }
    if (!a5_still(s_a5_baal, "baalcrab") && units_find_all("baalcrab", &id, 1) == 1)
        s_a5_baal = id;
    /* (cada vez que muere, como el original: también en una partida cargada
       con Baal ya muerto, que si no se quedaba sin Tyrael ni portal) */
    if (a5_still(s_a5_baal, "baalcrab") && units_is_corpse(s_a5_baal) && s_a5_tyrael == -1) {
        units_position(s_a5_baal, 1.0f, &x, &y);
        for (int i = 0; i < s_num_players; i++) {
            if (s_pl[i].baal_q < 4)
                s_pl[i].baal_q = 4;
            if (s_diff < 2 && s_pl[i].diff_max < s_diff + 1) /* se abre la dificultad que sigue (CLIENTS_UpdateCharacterProgression) */
                s_pl[i].diff_max = s_diff + 1;
        }
        /* Tyrael aparece, 5 al noroeste (ACT5Q6_SpawnTyrael); el portal, cuando le hablan */
        float tx = x - 5.0f, ty = y - 5.0f;
        a5_free_spot(x, y, &tx, &ty, 7.0f); /* (a 4 de él, ningún portal a 2,5) */
        units_zone_begin(zone_at(tx, ty));
        s_a5_tyrael = units_spawn_npc("tyrael3", tx, ty);
        units_zone_end();
        if (s_a5_tyrael < 0)
            s_a5_tyrael = -2; /* (sin lugar en la tabla de unidades: no se reintenta) */
        quest_banner("Baal murió: hablá con Tyrael", "music_quest_baal");
        save_party();
    }
}

static void a5_quest_reset(void)
{
    for (int k = 0; k < 3; k++) {
        s_a5_hell_x[k] = -1;
        s_a5_ancient[k] = -1;
        s_a5_statue[k][2] = -1.0f;
        world_portal(PORTAL_HELL(k), -1, 0, 0, -1, 0, 0);
        world_portal(PORTAL_HELL_BACK(k), -1, 0, 0, -1, 0, 0);
    }
    world_portal(PORTAL_ANYA, -1, 0, 0, -1, 0, 0);
    world_portal(PORTAL_ANYA_BACK, -1, 0, 0, -1, 0, 0);
    world_portal(PORTAL_CHAMBER, -1, 0, 0, -1, 0, 0);
    world_portal(PORTAL_CHAMBER_BACK, -1, 0, 0, -1, 0, 0);
    s_a5_ndoors = s_a5_chamber = 0;
    s_a5_shenk = s_a5_nihl = s_a5_baal = s_a5_tyrael = -1;
    units_set_ancients(0);
}
