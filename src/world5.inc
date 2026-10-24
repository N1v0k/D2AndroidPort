/* world5.inc: el mundo del Acto 5 (lo incluye world.c, después de world4.inc).
   Como el original (D2MOO):
   - Harrogath: una sola pieza (LVLPREST_ACT5_TOWN, Expansion/Town/townWest.ds1),
     el primer nivel de gAct5OutdoorDrlgLink.
   - El Bloody Foothills (240x48), pegado al oeste de Harrogath: sus 15 tiras
     de 16x48 una al lado de la otra, de Siege To Town (contra el pueblo) a
     Siege To Barricade (DRLGOUTSIEGE_InitAct5OutdoorLevel).
   - Las barricadas (el Frigid Highlands y el Arreat Plateau, unidos como
     gAct5OutdoorDrlgLink; la Frozen Tundra aparte, gAct5TundraDrlgLink): de
     64x160 o 160x64 al azar (DRLGOUTROOM_LinkLevelsBy*). Cada una: el borde
     de acantilado alrededor y el de barranco en los lados de adelante, la
     entrada y la salida hacia la vecina, las bocas de las cuevas, las
     barricadas que cruzan (el borde secundario de lvlsub.txt con las reglas
     de 4x3barricade_rules.ds1), las prisiones del Frigid Highlands y las
     piezas especiales (el portal al infierno, el waypoint, las ruinas, los
     rellenos y los lagos), y el piso en lo que queda.
   - Las cuevas de hielo, el Frozen River, el Drifter Cavern, el Icy Cellar,
     la cumbre y la entrada del templo: en world3.inc (k_act3_mazes y
     k_act3_presets, con las del Acto 3), en las filas de abajo.
   El templo y la Fortaleza de la Piedra del Mundo van en la fase 3 de
   docs/plan-acto5.md.
   Después, la decoración al azar de lvlsub.txt (los árboles, las rocas y
   las manchas de nieve o de tierra de los tipos 10 y 11: barr_decorate).
   El piso de las celdas vacías es la tierra de Ground.dt1 (en la Tundra, la
   nieve de snow.dt1). */

#define HARRO_W 40 /* levels.txt (el DS1 tiene una fila y una columna más) */
#define HARRO_H 40
#define SIEGE_W 240 /* el Bloody Foothills */
#define SIEGE_H 48
#define SIEGE_STRIPS 15
#define SG_MAX  20 /* celdas de 8x8 de una barricada (160 / 8) */
#define BARR_GROUND_DT1 "data/global/tiles/expansion/siege/ground.dt1"
#define BARR_SNOW_DT1   "data/global/tiles/expansion/siege/snow.dt1"
#define BARR_RULES_DS1  "data/global/tiles/expansion/siege/4x3barricade_rules.ds1"

enum {
    LP_HARROGATH = 863,
    LP_SIEGE_TO_TOWN = 865, /* + 0..14: las tiras del Bloody Foothills, hasta Siege To Barricade */
    LP_BARR_TO_SIEGE = 880,
    LP_CLIFF_1 = 881, /* + 0..11: Barricade Cliff Border 1..12 */
    LP_RAVINE_1 = 893, /* + 0..11: Barricade Ravine Border 1..12 */
    LP_RAV_CLIFF_5 = 905, LP_CLIFF_RAV_7 = 906,
    LP_ENTRANCE_16X32 = 907, LP_ENTRANCE_32X16, LP_EXIT_32X16, LP_EXIT_16X32,
    LP_FROM_CAVE_32X16 = 911, LP_FROM_CAVE_16X32, LP_TO_CAVE_32X16, LP_TO_CAVE_16X32,
    LP_BARR_1 = 915, /* + 0..15: Barricade 1..16; + 16..23: sus prisiones (Barricade Prison 1..8) */
    LP_BUILDING = 939, LP_FILLER, LP_FILLER_TREASURE,
    LP_RUINS_N_1 = 942, LP_RUINS_N_2, LP_RUINS_N_TREASURE, LP_RUINS_W_1, LP_RUINS_W_2, LP_RUINS_W_TREASURE,
    LP_SNOW_LAKE_1 = 948, LP_SNOW_LAKE_2, LP_SNOW_OTHER, LP_SNOW_TREASURE,
    LP_WP_DIRT = 953, LP_WP_SNOW, LP_HELL_N, LP_HELL_W,
    LP_SNOW_BORDER = 76, /* lo que suman las de nieve a las de borde (Cliff y Ravine 1..12, Ravine-Cliff 5, Cliff-Ravine 7) */
    LP_FROM_CAVE_32X16_SNOW = 983, LP_FROM_CAVE_16X32_SNOW, LP_TO_CAVE_32X16_SNOW, LP_TO_CAVE_16X32_SNOW,
    LP_BARR_1_SNOW = 987,
};
enum { BARR_FRIGID, BARR_PLATEAU, BARR_TUNDRA };
static const char *const k_barr_names[3] = { "Rigid Highlands", "Arreat Plateau", "Tundra Wastelands" };

static DRect s_barr[3]; /* las tres barricadas, en tiles */
static int   z_siege, z_barr[3];

/* ---- La grilla de una barricada (pGrid[0] y pGrid[2] del original) ---- */

enum { SGF_HAS = 1, SGF_BLANK = 2, SGF_LINK = 4, SGF_EDGE = 8 }; /* bHasPickedFile, nUnkb08, bLvlLink, nUnkb00 */
typedef struct {
    int     gw, gh, snow;
    int16_t def[SG_MAX][SG_MAX]; /* la pieza en su celda de arriba a la izquierda (las demás que tapa: 0) */
    int8_t  pick[SG_MAX][SG_MAX];
    uint8_t fl[SG_MAX][SG_MAX];
} SGrid;

static int sg_in(const SGrid *g, int x, int y)
{
    return x >= 0 && y >= 0 && x < g->gw && y < g->gh;
}

/* El tamaño de la pieza en celdas (lvlprest.txt SizeX/SizeY / 8) */
static void sg_size(int def, int *w, int *h)
{
    char path[160];
    *w = *h = 0;
    prest_file(def, 0, path, sizeof path, w, h);
    *w = *w >= CELL ? *w / CELL : 1;
    *h = *h >= CELL ? *h / CELL : 1;
}

/* DRLGOUTDOORS_SpawnOutdoorLevelPresetEx (pick -1: el archivo que sigue en ronda) */
static void sg_spawn(SGrid *g, int x, int y, int def, int pick)
{
    int w, h;
    if (def == 0) { /* la pieza None mide 0x0: solo se borra la celda en pGrid[0] */
        if (sg_in(g, x, y))
            g->def[y][x] = 0;
        return;
    }
    sg_size(def, &w, &h);
    if (pick < 0)
        pick = outdoor_pick(def);
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (sg_in(g, i, j)) {
                g->fl[j][i] |= SGF_HAS;
                g->pick[j][i] = (int8_t)pick;
                g->def[j][i] = 0;
            }
    if (sg_in(g, x, y))
        g->def[y][x] = (int16_t)def;
}

/* DRLGOUTDOORS_GetPresetIndexFromGridCell */
static int sg_def_at(const SGrid *g, int x, int y)
{
    return sg_in(g, x, y) && (g->fl[y][x] & SGF_HAS) ? g->def[y][x] : 0;
}

/* DRLGOUTDOORS_TestOutdoorLevelPreset (sin el margen: nOffset es 0 en el Acto 5) */
static int sg_fits(const SGrid *g, int x, int y, int def)
{
    int w, h;
    sg_size(def, &w, &h);
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (!sg_in(g, i, j) || (g->fl[j][i] & (SGF_HAS | SGF_BLANK | SGF_EDGE)))
                return 0;
    return 1;
}

/* DRLGOUTDOORS_SpawnOutdoorLevelPreset: en la primera celda del interior
   (barajadas como el original) donde entra; 1 = la puso */
static int sg_somewhere(SGrid *g, int def, int pick)
{
    int w = g->gw - 2, area = w * (g->gh - 2), cx[SG_MAX * SG_MAX], cy[SG_MAX * SG_MAX];
    if (area <= 0)
        return 0;
    for (int i = 0; i < area; i++) {
        cx[i] = i % w;
        cy[i] = i / w;
    }
    for (int i = 0; i < area; i++) {
        int a = (int)(rnd() % (uint32_t)area), b = (int)(rnd() % (uint32_t)area), tx = cx[a], ty = cy[a];
        cx[a] = cx[b];
        cy[a] = cy[b];
        cx[b] = tx;
        cy[b] = ty;
    }
    for (int i = 0; i < area; i++)
        if (sg_fits(g, cx[i] + 1, cy[i] + 1, def)) {
            sg_spawn(g, cx[i] + 1, cy[i] + 1, def, pick);
            return 1;
        }
    return 0;
}

/* sub_6FD84780: la pieza que dice una celda del archivo de reglas (estilo
   48: los bordes; 49: las barricadas); 0 = vacía, -5 = cualquiera */
static int sg_rule_def(const SGrid *g, int style, int seq)
{
    static const struct { int style, lo, hi, def; } k_map[] = {
        { 49, 1, 16, LP_BARR_1 }, { 49, 31, 46, LP_BARR_1 }, { 48, 1, 1, LP_CLIFF_1 + 2 }, { 48, 2, 3, LP_CLIFF_1 },
        { 48, 4, 4, LP_CLIFF_1 + 3 }, { 48, 5, 5, LP_RAVINE_1 + 2 }, { 48, 6, 7, LP_RAVINE_1 }, { 48, 8, 8, LP_RAVINE_1 + 3 },
        { 48, 30, 30, 0 }, { 48, 31, 31, -5 },
    };
    for (unsigned k = 0; k < COUNT(k_map); k++)
        if (style == k_map[k].style && seq >= k_map[k].lo && seq <= k_map[k].hi) {
            int def = k_map[k].def;
            if (def <= 0)
                return def;
            def += seq - k_map[k].lo;
            if (g->snow)
                def += def >= LP_BARR_1 ? LP_BARR_1_SNOW - LP_BARR_1 : LP_SNOW_BORDER;
            return def;
        }
    return -5;
}

/* El borde secundario (DRLGTILESUB_AddSecondaryBorder con LVLSUB_ACT5_BARRICADE:
   BordType 2 y GridSize 2): cada grupo de las reglas se prueba en todas
   las posiciones (barajadas) y donde el patrón coincide va una de sus
   alternativas (DRLGTILESUB_TestReplaceSubPreset y _ReplaceSubPreset) */
static void sg_barricades(SGrid *g)
{
    static Ds1 s_rules;
    static int s_loaded;
    if (!s_loaded) {
        s_loaded = 1;
        size_t n;
        uint8_t *d = fs_read(BARR_RULES_DS1, &n);
        if (!d || ds1_load(d, n, &s_rules) != 0)
            fprintf(stderr, "falta %s: las barricadas del Acto 5 no van a tener sus barreras\n", BARR_RULES_DS1);
        free(d);
    }
    for (int gi = 0; gi < s_rules.num_groups; gi++) {
        const Ds1Group *grp = &s_rules.groups[gi];
        int w = 1 + g->gw - 2 * grp->w, h = 1 + g->gh - 2 * grp->h, area = w * h;
        if (area <= 0 || area > SG_MAX * SG_MAX)
            continue;
        int cx[SG_MAX * SG_MAX], cy[SG_MAX * SG_MAX];
        for (int i = 0; i < area; i++) {
            cx[i] = i % w;
            cy[i] = i / w;
        }
        for (int i = 0; i < area; i++) {
            int a = (int)(rnd() % (uint32_t)area), b = (int)(rnd() % (uint32_t)area), tx = cx[a], ty = cy[a];
            cx[a] = cx[b];
            cy[a] = cy[b];
            cx[b] = tx;
            cy[b] = ty;
        }
        for (int i = 0; i < area; i++) {
            int bx = cx[i] - cx[i] % 2, by = cy[i] - cy[i] % 2, ok = 1;
            for (int j = 0; j < grp->h && ok; j++)
                for (int k = 0; k < grp->w && ok; k++) {
                    const Ds1Cell *c = ds1_wall(&s_rules, 0, grp->x + k, grp->y + j);
                    if (!(c->prop1 & 1))
                        continue;
                    int want = sg_rule_def(g, c->style, c->sequence), x = bx + k * 2, y = by + j * 2;
                    if (want != -5)
                        ok = sg_in(g, x, y) && g->def[y][x] == want && !(g->fl[y][x] & SGF_LINK);
                }
            if (!ok)
                continue;
            int alt = ((int)(rnd() % (uint32_t)(grp->alts > 0 ? grp->alts : 1)) + 1) * (grp->w + 1);
            for (int j = 0; j < grp->h; j++)
                for (int k = 0; k < grp->w; k++) {
                    const Ds1Cell *c = ds1_wall(&s_rules, 0, grp->x + alt + k, grp->y + j);
                    int x = bx + k * 2, y = by + j * 2;
                    if (!sg_in(g, x, y))
                        continue;
                    if (c->prop1 & 1) {
                        int def = sg_rule_def(g, c->style, c->sequence);
                        if (def != -5)
                            sg_spawn(g, x, y, def, 0);
                    } else { /* DRLGOUTDOORS_SetBlankGridCell */
                        g->def[y][x] = 0;
                        g->fl[y][x] = SGF_BLANK;
                    }
                }
        }
    }
}

/* Las prisiones del Frigid Highlands (DRLGOUTSIEGE_PlacePrisons): tres de
   las barricadas Barricade 1..8 pasan a ser su prisión (los presos de la
   misión) */
static void sg_prisons(SGrid *g)
{
    int placed = 0;
    for (int t = 0; t < 90 && placed < 3; t++) {
        int x = 2 * (int)(rnd() % (uint32_t)(g->gw / 2)), y = 2 * (int)(rnd() % (uint32_t)(g->gh / 2));
        int def = sg_def_at(g, x, y);
        if (def >= LP_BARR_1 && def <= LP_BARR_1 + 7) {
            sg_spawn(g, x, y, def + 16, -1);
            placed++;
        }
    }
    int rx = 2 * (int)(rnd() % (uint32_t)(g->gw / 2)), ry = 2 * (int)(rnd() % (uint32_t)(g->gh / 2));
    for (int j = 0; j < g->gh && placed < 3; j++)
        for (int i = 0; i < g->gw && placed < 3; i++) {
            int x = (i + rx) % g->gw, y = (j + ry) % g->gh, def = sg_def_at(g, x, y);
            if (def >= LP_BARR_1 && def <= LP_BARR_1 + 7) {
                sg_spawn(g, x, y, def + 16, -1);
                placed++;
            }
        }
    if (placed < 3)
        fprintf(stderr, "Frigid Highlands: solo %d prisiones de 3\n", placed);
}

/* ---- La decoración al azar de lvlsub.txt (DRLGTILESUB_PickSubThemes y
   DoSubstitutions) ----
   En cada sala de 8x8 de la barricada, los de su SubType de levels.txt (10
   el Frigid Highlands y el Arreat Plateau, 11 la Frozen Tundra) con su
   Prob del SubTheme; de cada uno, hasta Max veces un grupo de su DS1 de
   reglas al azar, en el primero de Trials lugares (o de todos, barajados,
   con -1) donde todo lo del grupo cae sobre piso liso sin paredes; ahí va
   el grupo (piso, paredes, sombras y objetos) con los DT1 de su Dt1Mask.
   Va al final del armado, una pieza por sala: el piso ya está, y el azar
   de cada sala sale de la semilla de la zona (como la pSeed de cada sala) */
#define DECO_ROWS 8
static TxtTable s_lvlsub_txt;

typedef struct {
    int row;      /* la de lvlsub.txt */
    Ds1 rules;    /* su DS1 (leído una vez) */
    int ok;
} DecoRule;

static uint32_t deco_rnd(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

/* ¿Entra el grupo con su esquina en el tile (x, y)? (sub_6FD8B010) */
static int deco_fits(const Map *m, const Ds1 *r, const Ds1Group *grp, int x, int y, int style)
{
    for (int j = 0; j < grp->h; j++)
        for (int i = 0; i < grp->w; i++) {
            int has = (r->num_floors > 0 && ds1_floor(r, 0, grp->x + i, grp->y + j)->prop1) ||
                      (r->num_walls > 0 && ds1_wall(r, 0, grp->x + i, grp->y + j)->prop1);
            if (has && !map_plain_floor(m, x + i, y + j, style, 0))
                return 0;
        }
    return 1;
}

static void barr_decorate(Map *m, int z, const SGrid *g)
{
    static DecoRule s_rules[DECO_ROWS];
    static int s_nrules = -1, s_type = -1;
    const Zone *zone = &s_zones[z];
    const Level *lv = zone_level(z);
    char id[12];
    snprintf(id, sizeof id, "%d", lv ? lv->id : -1);
    int lrow = txt_find_row(&s_levels_txt, txt_col(&s_levels_txt, "Id"), id);
    int type = lrow >= 0 ? txt_get_int(&s_levels_txt, lrow, "SubType", -1) : -1;
    int theme = lrow >= 0 ? txt_get_int(&s_levels_txt, lrow, "SubTheme", -1) : -1;
    if (type < 0 || theme < 0 || theme > 4)
        return;
    if (!s_lvlsub_txt.rows) {
        size_t n;
        uint8_t *d = fs_read("data/global/excel/lvlsub.txt", &n);
        if (!d || txt_load(d, n, &s_lvlsub_txt) != 0)
            fprintf(stderr, "falta data/global/excel/lvlsub.txt\n");
        free(d);
    }
    if (type != s_type) { /* las reglas del tipo (de las barricadas: una sola vez por tipo) */
        for (int k = 0; k < s_nrules; k++)
            if (s_rules[k].ok)
                ds1_free(&s_rules[k].rules);
        s_nrules = 0;
        s_type = type;
        int tcol = txt_col(&s_lvlsub_txt, "Type");
        for (int r = 1; r < s_lvlsub_txt.rows && s_nrules < DECO_ROWS; r++) {
            const char *t = txt_get(&s_lvlsub_txt, r, tcol);
            if (!t[0] || atoi(t) != type || strcmp(txt_get(&s_lvlsub_txt, r, txt_col(&s_lvlsub_txt, "Name")), "Expansion") == 0)
                continue;
            DecoRule *dr = &s_rules[s_nrules++];
            char path[160];
            snprintf(path, sizeof path, "data/global/tiles/%s", txt_get(&s_lvlsub_txt, r, txt_col(&s_lvlsub_txt, "File")));
            size_t n;
            uint8_t *d = fs_read(path, &n);
            dr->row = r;
            dr->ok = d && ds1_load(d, n, &dr->rules) == 0 && dr->rules.num_groups > 0;
            if (!dr->ok)
                fprintf(stderr, "falta %s: sin esa decoración\n", path);
            free(d);
        }
    }
    int style = g->snow ? 6 : 0; /* (el piso liso de la barricada: el del relleno) */
    for (int cy = 0; cy < g->gh; cy++)
        for (int cx = 0; cx < g->gw; cx++) {
            /* Solo en las salas de relleno (DRLGOUTPLACE_CreateOutdoorRoomEx):
               las de una pieza no se decoran, aunque tengan ese piso (un
               tronco caído tapaba la salida del hueco del portal rojo) */
            if (!map_piece_now(m) || (g->fl[cy][cx] & (SGF_HAS | SGF_BLANK)))
                continue;
            uint32_t seed = zone->seed ^ (uint32_t)(cy * g->gw + cx + 1) * 0x9E3779B9u;
            int x0 = zone->x0 + cx * CELL, y0 = zone->y0 + cy * CELL;
            /* PickSubThemes: los de este tipo que van en esta sala */
            int mask = 0;
            for (int k = 0; k < s_nrules; k++) {
                char col[8];
                snprintf(col, sizeof col, "Prob%d", theme);
                if ((int)(deco_rnd(&seed) % 100) < txt_get_int(&s_lvlsub_txt, s_rules[k].row, col, 0))
                    mask |= 1 << k;
            }
            for (int k = 0; k < s_nrules; k++) {
                if (!(mask >> k & 1) || !s_rules[k].ok)
                    continue;
                const Ds1 *r = &s_rules[k].rules;
                char col[8], lib[34][96];
                const char *libp[34];
                snprintf(col, sizeof col, "Max%d", theme);
                int max = txt_get_int(&s_lvlsub_txt, s_rules[k].row, col, 0);
                snprintf(col, sizeof col, "Trials%d", theme);
                int trials = txt_get_int(&s_lvlsub_txt, s_rules[k].row, col, 0);
                int nlib = prest_lib_mask((uint32_t)txt_get_int(&s_lvlsub_txt, s_rules[k].row, "Dt1Mask", 0), z, lib);
                for (int i = 0; i < nlib; i++)
                    libp[i] = lib[i];
                for (int xyz = 0; xyz < max; xyz++) {
                    const Ds1Group *grp = &r->groups[deco_rnd(&seed) % (uint32_t)r->num_groups];
                    int ax = CELL - grp->w, ay = CELL - grp->h, px = -1, py = -1;
                    if (ax <= 0 || ay <= 0)
                        continue;
                    if (trials != -1) {
                        for (int t = 0; t < trials && px < 0; t++) {
                            int nx = (int)(deco_rnd(&seed) % (uint32_t)ax) + 1, ny = (int)(deco_rnd(&seed) % (uint32_t)ay) + 1;
                            if (deco_fits(m, r, grp, x0 + nx, y0 + ny, style))
                                px = nx, py = ny;
                        }
                    } else { /* (todos los lugares, barajados) */
                        int pos[64], np = ax * ay;
                        for (int i = 0; i < np; i++)
                            pos[i] = i;
                        for (int i = 0; i < np; i++) {
                            int a = (int)(deco_rnd(&seed) % (uint32_t)np), b = (int)(deco_rnd(&seed) % (uint32_t)np), t = pos[a];
                            pos[a] = pos[b];
                            pos[b] = t;
                        }
                        for (int i = 0; i < np && px < 0; i++)
                            if (deco_fits(m, r, grp, x0 + pos[i] % ax + 1, y0 + pos[i] / ax + 1, style))
                                px = pos[i] % ax + 1, py = pos[i] / ax + 1;
                    }
                    if (px >= 0)
                        map_stamp_part_now(m, r, grp->x, grp->y, grp->w, grp->h, x0 + px, y0 + py, libp, nlib);
                }
            }
        }
}

/* DRLGOUTSIEGE_InitAct5OutdoorLevel para una barricada (Zone.floor =
   BARR_*), en la grilla */
static void barr_plan(SGrid *g, const Zone *zone)
{
    static const int k_step[12][2] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 },
                                       { 0, 1 },  { -1, 0 }, { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
    int k = zone->floor, snow = k == BARR_TUNDRA, sb = snow ? LP_SNOW_BORDER : 0;
    int nw = g->gw - 2, nh = g->gh - 2;
    /* Los lados con el nivel vecino (la vuelta de vértices con la marca de
       unión): el Frigid Highlands y el Arreat Plateau entre sí */
    int link[4] = { 0 }; /* arriba, a la derecha, abajo, a la izquierda */
    if (k != BARR_TUNDRA) {
        const DRect *a = &s_barr[k], *b = &s_barr[k == BARR_FRIGID ? BARR_PLATEAU : BARR_FRIGID];
        link[0] = a->y == b->y + b->h;
        link[1] = a->x + a->w == b->x;
        link[2] = a->y + a->h == b->y;
        link[3] = a->x == b->x + b->w;
    }
    /* La vuelta: los lados (Cliff Border 3 arriba, 4 a la derecha, 1 abajo,
       2 a la izquierda) y las esquinas (7, 8, 5 y 6), en el sentido del reloj */
    static const int k_vx[4] = { 0, 1, 1, 0 }, k_vy[4] = { 0, 0, 1, 1 };
    static const int k_side[4] = { 2, 3, 0, 1 }, k_corner[4] = { 6, 7, 4, 5 };
    for (int e = 0; e < 4; e++) {
        int px = k_vx[e] * nw, py = k_vy[e] * nh, qx = k_vx[(e + 1) % 4] * nw, qy = k_vy[(e + 1) % 4] * nh;
        int dx = (qx > px) - (qx < px), dy = (qy > py) - (qy < py), x = px, y = py;
        while (x != qx || y != qy) {
            x += 2 * dx;
            y += 2 * dy;
            sg_spawn(g, x, y, LP_CLIFF_1 + sb + k_side[e], -1);
            g->fl[y][x] |= SGF_EDGE;
        }
        if (link[e]) {
            int lx = ((px > qx ? px : qx) - 4 * abs(dx)) & ~1, ly = ((py > qy ? py : qy) - 4 * abs(dy)) & ~1;
            g->fl[ly][lx] |= SGF_LINK;
            if (sg_in(g, lx + 2 * abs(dx), ly + 2 * abs(dy)))
                g->fl[ly + 2 * abs(dy)][lx + 2 * abs(dx)] |= SGF_LINK;
        }
        sg_spawn(g, qx, qy, LP_CLIFF_1 + sb + k_corner[e], -1);
        g->fl[qy][qx] |= SGF_EDGE;
    }
    if (k == BARR_FRIGID) { /* sub_6FD846C0: la unión con el Bloody Foothills */
        g->fl[g->gh - 4][nw] |= SGF_LINK;
        g->fl[g->gh - 3][nw] |= SGF_LINK;
    }
    /* El barranco: de la esquina de arriba a la derecha bajando hasta la de
       abajo a la izquierda (el lado de la derecha y el de abajo) */
    for (int x = nw, y = 0, guard = 0; (x != 0 || y != nh) && guard < 4 * SG_MAX; guard++) {
        int idx = g->def[y][x] - LP_CLIFF_1 - sb;
        if (idx < 0 || idx > 11)
            break;
        sg_spawn(g, x, y, LP_RAVINE_1 + sb + idx, -1);
        x += 2 * k_step[idx][0];
        y += 2 * k_step[idx][1];
    }
    sg_spawn(g, nw, 0, LP_CLIFF_RAV_7 + sb, -1);
    sg_spawn(g, 0, nh, LP_RAV_CLIFF_5 + sb, -1);
    /* DRLGOUTSIEGE_PlaceBarricadeEntrancesAndExits: arriba y a la izquierda
       la salida, abajo y a la derecha la entrada */
    for (int i = 0; i < g->gw; i++)
        if (g->fl[0][i] & SGF_LINK) {
            sg_spawn(g, i, 0, LP_EXIT_32X16, -1);
            break;
        }
    for (int i = 0; i < g->gw; i++)
        if (g->fl[nh][i] & SGF_LINK) {
            sg_spawn(g, i, nh, LP_ENTRANCE_32X16, -1);
            break;
        }
    for (int i = 0; i < g->gh; i++)
        if (g->fl[i][0] & SGF_LINK) {
            sg_spawn(g, 0, i, LP_EXIT_16X32, -1);
            break;
        }
    for (int i = 0; i < g->gh; i++)
        if (g->fl[i][nw] & SGF_LINK) {
            sg_spawn(g, nw, i, LP_ENTRANCE_16X32, -1);
            break;
        }
    /* DRLGOUTSIEGE_PlaceCaves: la boca hacia la cueva que sigue (arriba o a
       la izquierda) y, en la Tundra, la de la que viene (abajo o a la derecha) */
    int tall = g->gw <= g->gh;
    if (k == BARR_TUNDRA)
        sg_spawn(g, tall ? 2 : nw, tall ? nh : 2, tall ? LP_FROM_CAVE_32X16_SNOW : LP_FROM_CAVE_16X32_SNOW, 0);
    if (k != BARR_FRIGID)
        sg_spawn(g, tall ? 2 : 0, tall ? 0 : 2,
                 snow ? (tall ? LP_TO_CAVE_32X16_SNOW : LP_TO_CAVE_16X32_SNOW) : (tall ? LP_TO_CAVE_32X16 : LP_TO_CAVE_16X32), 0);
    if (k == BARR_FRIGID) { /* DRLGOUTSIEGE_ConnectBarricadeAndSiege */
        sg_spawn(g, g->gw - 2, g->gh - 4, LP_BARR_TO_SIEGE, -1);
        sg_spawn(g, g->gw - 2, g->gh - 6, LP_RAVINE_1 + 3, -1);
    }
    sg_barricades(g);
    if (k == BARR_FRIGID)
        sg_prisons(g);
    /* DRLGOUTSIEGE_PlaceSpecialPresets: la de cada fila según si el nivel es
       angosto (la N) o ancho (la W), las veces que dice */
    static const struct { int lv, tall_def, wide_def, pick, times; } k_special[] = {
        { BARR_FRIGID, LP_HELL_N, LP_HELL_W, 0, 1 },
        { BARR_PLATEAU, LP_HELL_N, LP_HELL_W, 0, 1 },
        { BARR_TUNDRA, LP_HELL_N, LP_HELL_W, 1, 1 },
        { BARR_PLATEAU, LP_WP_DIRT, LP_WP_DIRT, -1, 1 },
        { BARR_TUNDRA, LP_WP_SNOW, LP_WP_SNOW, -1, 1 },
        { BARR_FRIGID, LP_RUINS_N_TREASURE, LP_RUINS_W_TREASURE, -1, 1 },
        { BARR_FRIGID, LP_RUINS_N_1, LP_RUINS_W_1, -1, 4 },
        { BARR_FRIGID, LP_RUINS_N_2, LP_RUINS_W_2, -1, 4 },
        { BARR_PLATEAU, LP_FILLER_TREASURE, LP_FILLER_TREASURE, -1, 1 },
        { BARR_PLATEAU, LP_BUILDING, LP_BUILDING, -1, 1 },
        { BARR_PLATEAU, LP_FILLER, LP_FILLER, -1, 5 },
        { BARR_TUNDRA, LP_SNOW_LAKE_1, LP_SNOW_LAKE_1, -1, 4 },
        { BARR_TUNDRA, LP_SNOW_LAKE_2, LP_SNOW_LAKE_2, -1, 4 },
        { BARR_TUNDRA, LP_SNOW_OTHER, LP_SNOW_OTHER, -1, 4 },
        { BARR_TUNDRA, LP_SNOW_TREASURE, LP_SNOW_TREASURE, -1, 3 },
    };
    for (unsigned s = 0; s < COUNT(k_special); s++) {
        if (k_special[s].lv != k)
            continue;
        int def = g->gw < g->gh ? k_special[s].tall_def : k_special[s].wide_def, added = 0;
        for (int t = 0; t < k_special[s].times; t++)
            added = sg_somewhere(g, def, k_special[s].pick);
        if (!added && s < 5)
            fprintf(stderr, "%s: no entró la pieza %d\n", k_barr_names[k], def);
    }
}

/* DRLGOUTDOORS_GenerateLevel: las piezas en su celda y el piso en las que
   quedaron sin pieza (DRLGOUTPLACE_CreateOutdoorRoomEx) */
static void build_barricade(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    SGrid g;
    memset(&g, 0, sizeof g);
    g.gw = (zone->x1 - zone->x0) / CELL;
    g.gh = (zone->y1 - zone->y0) / CELL;
    g.snow = zone->floor == BARR_TUNDRA;
    if (g.gw > SG_MAX || g.gh > SG_MAX)
        return;
    s_ncycle = 0;
    barr_plan(&g, zone);
    if (getenv("D2_A5GRID")) { /* depuración: la grilla (la pieza en su celda, + lo que tapa, . vacía) */
        printf("%s, %dx%d celdas:\n", k_barr_names[zone->floor], g.gw, g.gh);
        for (int cy = 0; cy < g.gh; cy++) {
            for (int cx = 0; cx < g.gw; cx++)
                printf(g.def[cy][cx] ? " %4d" : (g.fl[cy][cx] & SGF_HAS) ? "    +" : "    .", g.def[cy][cx]);
            printf("\n");
        }
    }
    for (int cy = 0; cy < g.gh; cy++)
        for (int cx = 0; cx < g.gw; cx++) {
            int x = zone->x0 + cx * CELL, y = zone->y0 + cy * CELL;
            if ((g.fl[cy][cx] & SGF_HAS) && g.def[cy][cx])
                prest_stamp(m, z, g.def[cy][cx], g.pick[cy][cx], x, y);
            else if (!(g.fl[cy][cx] & (SGF_HAS | SGF_BLANK)))
                map_fill_floor(m, g.snow ? BARR_SNOW_DT1 : BARR_GROUND_DT1, g.snow ? 6 : 0, 0, x, y, CELL, CELL);
        }
    barr_decorate(m, z, &g);
}

/* El Bloody Foothills: las 15 tiras, de la del pueblo (a la derecha) a la de
   la barricada */
static void build_siege(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    s_ncycle = 0;
    for (int i = 0; i < SIEGE_STRIPS; i++)
        prest_stamp(m, z, LP_SIEGE_TO_TOWN + i, 0, zone->x0 + SIEGE_W - 16 * (i + 1), zone->y0);
}

/* ---- La disposición del acto ---- */

/* ---- El evento Pandemonium (ACT1Q4_CreateUberRunPortal y ...FinalePortal) ----
   Solo en Infierno, como el original: Matron's Den (una cripta), las Forgotten
   Sands (un desierto), el Furnace of Pain (un infierno de lava) y Uber
   Tristram (Tri_Town4.ds1), en las filas de abajo. Se llega solo por los
   portales rojos del cubo en Harrogath (uber.inc). */
static void uber_add_zones(void)
{
    int x, y, w = MAZE_MAX * 8 + 1, z;
    pack(w, w, &x, &y);
    z = add_zone(ZK_UBERDEN, 133, x, y, x + w, y + w);
    add_level("#133", z, x, y, x + w, y + w, 20, 1);
    pack(DESERT_W, DESERT_W, &x, &y);
    z = add_zone(ZK_DESERT, DZ_SANDS, x, y, x + DESERT_W, y + DESERT_W);
    for (int k = 0; k < 4; k++)
        s_zones[z].open[k][0] = s_zones[z].open[k][1] = -1;
    add_level("#134", z, x, y, x + DESERT_W, y + DESERT_W, 24, 0);
    for (int k = 0; k < (int)COUNT(k_act3_mazes); k++)
        if (k_act3_mazes[k].id == 135) {
            w = k_act3_mazes[k].grid * k_act3_mazes[k].room + 1;
            pack(w, w, &x, &y);
            z = add_zone(ZK_A3MAZE, 135, x, y, x + w, y + w);
            add_level(k_act3_mazes[k].name, z, x, y, x + w, y + w, k_act3_mazes[k].groups, 1);
        }
    for (int k = 0; k < (int)COUNT(k_act3_presets); k++)
        if (k_act3_presets[k].id == 136 || k_act3_presets[k].id == 137) {
            int pw = k_act3_presets[k].w, ph = k_act3_presets[k].h;
            pack(pw, ph, &x, &y);
            z = add_zone(ZK_A3PRESET, k_act3_presets[k].id, x, y, x + pw, y + ph);
            add_level(k_act3_presets[k].name, z, x, y, x + pw, y + ph, 0, 0);
        }
}

static int layout_act5(Map *m)
{
    prest_tables_load();
    /* En coordenadas del original: Harrogath, el Bloody Foothills al oeste,
       el Frigid Highlands al oeste de él (alineado abajo, 16 más arriba,
       LinkLevelsByLevelCoords) y el Arreat Plateau con el corrimiento de
       LinkLevelsByOffsetCoords */
    static const int k_plateau_off[4][2] = { { 0, -160 }, { -96, -64 }, { -64, -96 }, { -160, 0 } };
    DRect town = { 1000, 1000, HARRO_W, HARRO_H }, siege = { 760, 1000, SIEGE_W, SIEGE_H };
    int rf = (int)(rnd() & 1), rp = (int)(rnd() & 1), rt = (int)(rnd() & 1);
    DRect *f = &s_barr[BARR_FRIGID], *p = &s_barr[BARR_PLATEAU], *t = &s_barr[BARR_TUNDRA];
    f->w = rf ? 160 : 64;
    f->h = rf ? 64 : 160;
    f->x = siege.x - f->w;
    f->y = siege.h - f->h + siege.y - 16;
    p->w = rp ? 160 : 64;
    p->h = rp ? 64 : 160;
    p->x = f->x + k_plateau_off[rp + 2 * rf][0];
    p->y = f->y + k_plateau_off[rp + 2 * rf][1];
    t->w = rt ? 160 : 64;
    t->h = rt ? 64 : 160;
    DRect *r[4] = { &town, &siege, f, p };
    int minx = town.x, miny = town.y, maxx = 0, maxy = 0;
    for (int i = 0; i < 4; i++) {
        minx = r[i]->x < minx ? r[i]->x : minx;
        miny = r[i]->y < miny ? r[i]->y : miny;
        maxx = r[i]->x + r[i]->w > maxx ? r[i]->x + r[i]->w : maxx;
        maxy = r[i]->y + r[i]->h > maxy ? r[i]->y + r[i]->h : maxy;
    }
    for (int i = 0; i < 4; i++) {
        r[i]->x += CELL - minx;
        r[i]->y += CELL - miny;
    }
    z_town = add_zone(ZK_HARROGATH, 0, town.x, town.y, town.x + town.w, town.y + town.h);
    add_level("Harrogath", z_town, town.x, town.y, town.x + town.w, town.y + town.h, 0, 0);
    z_siege = add_zone(ZK_SIEGE, 0, siege.x, siege.y, siege.x + siege.w, siege.y + siege.h);
    /* (sin grupos al azar: MonDen 0; sus monstruos son los de las tiras) */
    add_level("Bloody Foothills", z_siege, siege.x, siege.y, siege.x + siege.w, siege.y + siege.h, 0, 0);
    for (int k = 0; k < 2; k++) {
        const DRect *q = &s_barr[k];
        z_barr[k] = add_zone(ZK_BARRICADE, k, q->x, q->y, q->x + q->w, q->y + q->h);
        add_level(k_barr_names[k], z_barr[k], q->x, q->y, q->x + q->w, q->y + q->h, 25, 0);
    }
    /* La Frozen Tundra aparte (se llega por las cuevas), las cuevas de hielo,
       la cumbre y la entrada del templo, en las filas de abajo */
    s_pack_x = 0;
    s_pack_y = maxy - miny + 2 * CELL + PACK_GAP;
    s_pack_h = 0;
    pack(t->w, t->h, &t->x, &t->y);
    z_barr[BARR_TUNDRA] = add_zone(ZK_BARRICADE, BARR_TUNDRA, t->x, t->y, t->x + t->w, t->y + t->h);
    add_level(k_barr_names[BARR_TUNDRA], z_barr[BARR_TUNDRA], t->x, t->y, t->x + t->w, t->y + t->h, 25, 0);
    a3_add_dungeons(5);
    if (s_world_diff == 2 || s_world_arena) /* (y en el Eco del Odio: dos de sus arenas) */
        uber_add_zones();
    for (int i = 0; i < s_num_levels; i++) /* los saltos de las Vis de levels.txt (para buscar caminos) */
        for (int k = 0; k < 8; k++)
            add_jump(s_levels[i].zone, zone_of_level_id(s_levels[i].vis[k]));
    int width = maxx - minx + 2 * CELL > PACK_W ? maxx - minx + 2 * CELL : PACK_W, height = s_pack_y + s_pack_h;
    if (map_create(m, width, height) != 0)
        return -1;
    /* Los pasos caminando (en subtiles): el pueblo y el Bloody Foothills, el
       Bloody Foothills y la unión con el Frigid Highlands (abajo a la derecha
       de este) y el Frigid Highlands con el Arreat Plateau (en el tramo que
       comparten, cerca de su punta: donde van la salida y la entrada) */
    add_link(z_town, z_siege, (float)(town.x * TILE_SUB), (float)((town.y + HARRO_H / 2) * TILE_SUB));
    add_link(z_siege, z_barr[BARR_FRIGID], (float)(siege.x * TILE_SUB), (float)((f->y + f->h - 16) * TILE_SUB));
    if (p->y + p->h == f->y)
        add_link(z_barr[BARR_FRIGID], z_barr[BARR_PLATEAU], (float)((f->x + f->w - 32) * TILE_SUB), (float)(f->y * TILE_SUB));
    else
        add_link(z_barr[BARR_FRIGID], z_barr[BARR_PLATEAU], (float)(f->x * TILE_SUB), (float)((f->y + f->h - 32) * TILE_SUB));
    if (getenv("D2_DEBUG")) {
        printf("mundo del Acto 5 de %dx%d tiles, %d zonas\n", width, height, s_num_zones);
        for (int z = 0; z < s_num_zones; z++)
            printf("  zona %2d: %-26s (%d, %d)-(%d, %d)\n", z, s_zones[z].name, s_zones[z].x0, s_zones[z].y0, s_zones[z].x1,
                   s_zones[z].y1);
    }
    return 0;
}
