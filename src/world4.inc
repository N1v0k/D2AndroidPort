/* world4.inc: el mundo del Acto 4 (lo incluye world.c, después de world3.inc).
   Como el original (D2MOO):
   - la Fortaleza del Pandemónium: una sola pieza (LVLPREST_ACT4_FORTRESS);
   - las tres mesetas (DRLGOUTDOORS_InitAct4OutdoorLevel) unidas como
     gAct4OutdoorDrlgLink: el Outer Steppes al este de la Fortaleza, con la
     pieza de paso arriba o abajo (sub_6FD81CA0); el Plains of Despair y el
     City of the Damned pegados a la anterior por un lado al azar, corridos
     16 baldosas (sub_6FD81380), sin pisar a las demás
     (DRLGOUTPLACE_LinkAct4Outdoors);
   - el River of Flame (DRLGMAZE_PlaceAct4Lava): salas de lava de 24x24, la
     subida al City of the Damned al sur, la Forja Infernal, tres puentes al
     norte y lava alrededor de todo (DRLGMAZE_FillBlankMazeSpaces);
   - el Santuario del Caos: la grilla de 5x5 piezas encima del último puente.
   Las piezas salen de lvlprest.txt por su Def (el número de LVLPREST_ACT4_*). */

#define FORT_W    32 /* levels.txt (el DS1 tiene una fila y una columna más) */
#define FORT_H    24
#define RIV_G     16 /* la grilla del río, en salas */
#define RIV_ROOM  24 /* lvlmaze.txt: SizeX, SizeY */
#define RIV_ROOMS 6  /* lvlmaze.txt: Rooms */
#define RIV_MERGE 500 /* lvlmaze.txt: Merge (por mil) */
#define SANCT_W   (5 * RIV_ROOM)
/* El piso de las celdas de las mesetas que no tienen pieza */
#define MESA_FLOOR_DT1   "data/global/tiles/act4/mesa/floor.dt1"
#define MESA_FLOOR_STYLE 10
#define MESA_FLOOR_SEQ   0

enum {
    LP_FORTRESS = 797, LP_FORT_TRANS = 798,
    LP_MESA_BORDER = 799, /* + 0..11: Mesa Border 1..12 */
    LP_MESA_WARP = 811, LP_MESA_IZUAL = 822,
    LP_LAVA_X = 836, /* + las salidas (JF_*): Lava W..Lava NSEW */
    LP_LAVA_WARP = 852, LP_FORGE_W, LP_FORGE_E, LP_BRIDGE_1, LP_BRIDGE_2,
    LP_DIABLO_ENTRY, LP_ARM_W, LP_ARM_E, LP_ARM_S, LP_ARM_N, LP_HEART,
};
/* nMesaLvlPrestIds (24X24; +1 16X16, +2 16X08, +3 08X16, +4 08X08) y
   nPitsLvlPrestIds (16X16; +1 16X08, +2 08X16, +3 08X08) */
static const int k_mesa_pres[3] = { 812, 817, 823 };
static const int k_pits_pres[3] = { 828, 832, 832 };
static const char *const k_mesa_names[3] = { "Outer Steppes", "Plains of Despair", "City of the Damned" };

static DRect s_mesa[4];    /* la Fortaleza y las tres mesetas, en tiles */
static int   s_mesa_trans; /* la fila de celdas del Outer Steppes con el paso a la Fortaleza (1 o 4) */
static int   z_mesa[3], z_river, z_sanctum;

/* ---- La variante de cada pieza ----
   Como DRLGOUTDOORS_SpawnOutdoorLevelPresetEx: la primera vez que se pone
   una pieza en el nivel se tira su archivo al azar entre los Files de
   lvlprest.txt, y las siguientes van en ronda (así no se repiten) */
static int s_cycle[48][2], s_ncycle;

static int prest_files(int def)
{
    const TxtTable *t = &s_lvlprest_txt;
    int row = prest_row(def), n = row >= 0 ? txt_get_int(t, row, "Files", 1) : 1;
    return n > 0 ? n : 1;
}

static int outdoor_pick(int def)
{
    int k = 0;
    while (k < s_ncycle && s_cycle[k][0] != def)
        k++;
    int files = prest_files(def);
    if (k == s_ncycle) {
        if (s_ncycle >= (int)COUNT(s_cycle))
            return (int)(rnd() % (uint32_t)files);
        s_cycle[k][0] = def;
        s_cycle[k][1] = (int)(rnd() % (uint32_t)files);
        s_ncycle++;
    }
    return s_cycle[k][1] = (s_cycle[k][1] + 1) % files;
}

/* ---- Las mesetas ---- */

/* sub_6FD81430 (con a4 = 1): dónde queda `b` pegado a `a` por el lado `side`
   (0 abajo, 1 a la izquierda, 2 arriba, 3 a la derecha), corrido 16 */
static void mesa_side(const DRect *a, DRect *b, int side)
{
    switch (side) {
    case 0: b->x = a->x - 16; b->y = a->y + a->h; break;
    case 1: b->x = a->x - b->w; b->y = a->y - 16; break;
    case 2: b->x = a->x + a->w - b->w + 16; b->y = a->y - b->h; break;
    default: b->x = a->x + a->w; b->y = a->y + a->h - b->h + 16; break;
    }
}

/* El Plains of Despair (k = 2) y el City of the Damned (3): el primer lado
   al azar y después los siguientes; no pueden pisar a ninguna de antes
   salvo la que tocan (si no entra ninguno, se cambia la anterior) */
static int mesa_chain(DRect *r, int k)
{
    if (k > 3)
        return 1;
    int first = (int)(rnd() & 3);
    for (int t = 0; t < 4; t++) {
        mesa_side(&r[k - 1], &r[k], (first + t) & 3);
        int clash = 0;
        for (int j = 0; j < k - 1 && !clash; j++)
            clash = rects_overlap(&r[j], &r[k]);
        if (!clash && mesa_chain(r, k + 1))
            return 1;
    }
    return 0;
}

/* El borde de una meseta (PlaceAct1245OutdoorBorders con las piezas
   MESA_BORDER: 1 abajo, 2 a la izquierda, 3 arriba, 4 a la derecha, 5..8
   las esquinas). En el paso hacia la vecina, la celda del medio del tramo
   que comparten, va el archivo 3 de la pieza (Border1o..4o: abierta). Las
   celdas ya ocupadas (la pieza de paso a la Fortaleza) se saltean, y nada
   tapa los pasos por dentro.
   shortcut: sin el borde secundario de lvlsub.txt (AddAct124SecondaryBorder),
   como en el desierto */
static void mesa_borders(Map *m, int z, KGrid *g, const Zone *zone)
{
    int nw = g->cw - 1, nh = g->ch - 1, per = 2 * (nw + nh);
    for (int i = 0; i < per; i++) { /* en el sentido del reloj desde la esquina de arriba a la izquierda */
        int cx = i < nw ? i : i < nw + nh ? nw : i < 2 * nw + nh ? nw - (i - nw - nh) : 0;
        int cy = i < nw ? 0 : i < nw + nh ? i - nw : i < 2 * nw + nh ? nh : nh - (i - 2 * nw - nh);
        if (g->used[cy][cx])
            continue;
        int west = cx == 0, east = cx == nw, north = cy == 0, south = cy == nh;
        int piece = north && west ? 6 : north && east ? 7 : south && west ? 5 : south && east ? 8
                  : north ? 3 : south ? 1 : west ? 2 : 4;
        int def = LP_MESA_BORDER + piece - 1, variant = outdoor_pick(def);
        for (int s = 0; s < 8 && piece <= 4; s++) {
            int side = s / 2, o = zone->open[side][s % 2], along = side == 0 || side == 2 ? cx : cy;
            int on = side == 0 ? north : side == 1 ? east : side == 2 ? south : west;
            if (o >= 0 && on && along == o)
                variant = 3;
        }
        kgrid_put(m, z, g, def, variant, cx, cy);
    }
    for (int s = 0; s < 8; s++) {
        int side = s / 2, o = zone->open[side][s % 2];
        int cx = side == 1 ? nw - 1 : side == 3 ? 1 : o, cy = side == 2 ? nh - 1 : side == 0 ? 1 : o;
        if (o >= 0 && cx >= 0 && cy >= 0 && cx < g->cw && cy < g->ch)
            g->used[cy][cx] = 1;
    }
}

/* DRLGOUTDOORS_InitAct4OutdoorLevel: la meseta Zone.floor (0..2) */
static void build_mesa(Map *m, int z)
{
    static const int k_mesa_count[5] = { 1, 2, 2, 2, 4 }, k_pits_count[4] = { 1, 2, 2, 4 };
    const Zone *zone = &s_zones[z];
    int k = zone->floor;
    KGrid g;
    memset(&g, 0, sizeof g);
    g.x0 = zone->x0;
    g.y0 = zone->y0;
    g.cw = (zone->x1 - zone->x0) / CELL;
    g.ch = (zone->y1 - zone->y0) / CELL;
    s_ncycle = 0;
    if (k == 0) { /* la pieza de paso a la Fortaleza (y nada adelante) */
        kgrid_put(m, z, &g, LP_FORT_TRANS, 0, 0, s_mesa_trans);
        for (int r = s_mesa_trans; r < s_mesa_trans + 3; r++)
            g.used[r][1] = 1;
    }
    mesa_borders(m, z, &g, zone);
    uint8_t keep[16][16]; /* las de adentro que quedan libres adelante de los pasos */
    memcpy(keep, g.used, sizeof keep);
    if (k == 2) /* la bajada al River of Flame, con el waypoint */
        kgrid_somewhere(m, z, &g, LP_MESA_WARP, outdoor_pick(LP_MESA_WARP));
    for (int p = 0; p < 5; p++) {
        if (p == 4 && k == 1) /* Izual, antes de las de 8x8 */
            kgrid_somewhere(m, z, &g, LP_MESA_IZUAL, 0);
        for (int n = 0; n < k_mesa_count[p]; n++)
            kgrid_somewhere(m, z, &g, k_mesa_pres[k] + p, outdoor_pick(k_mesa_pres[k] + p));
    }
    for (int p = 0; p < 4; p++)
        for (int n = 0; n < k_pits_count[p]; n++)
            kgrid_somewhere(m, z, &g, k_pits_pres[k] + p, outdoor_pick(k_pits_pres[k] + p));
    /* Las de adentro que quedaron sin pieza: el piso de la meseta, como las
       salas vacías del original (DRLGOUTPLACE_CreateOutdoorRoomEx). Afuera
       del borde no: ahí está el precipicio. */
    for (int cy = 1; cy < g.ch - 1; cy++)
        for (int cx = 1; cx < g.cw - 1; cx++)
            if (!g.used[cy][cx] || keep[cy][cx])
                map_fill_floor(m, MESA_FLOOR_DT1, MESA_FLOOR_STYLE, MESA_FLOOR_SEQ, g.x0 + cx * CELL, g.y0 + cy * CELL, CELL,
                               CELL);
}

/* ---- El River of Flame ---- */

typedef struct {
    int8_t  id[RIV_G][RIV_G];   /* número de sala, -1 = nada */
    uint8_t ex[RIV_G][RIV_G];   /* salidas (JF_*) */
    int16_t def[RIV_G][RIV_G];  /* la pieza (las de salas comunes, al final, por sus salidas) */
    uint8_t fixed[RIV_G][RIV_G]; /* su pieza no depende de las salidas (DRLGPRESETROOMFLAG_HAS_MAP_DS1) */
    int     cx[RIV_G * RIV_G], cy[RIV_G * RIV_G], n;
    int     x0, y0, x1, y1;     /* las celdas que ocupa */
    int     bx, by;             /* el último puente, pegado al Santuario */
} River;

static River s_river;

/* ALTDIR del original: 0 O, 1 N, 2 E, 3 S y las diagonales NO, NE, SE, SO */
static const int k_adx[8] = { -1, 0, 1, 0, -1, 1, 1, -1 }, k_ady[8] = { 0, -1, 0, 1, -1, -1, 1, 1 };
static const int k_aflag[4] = { JF_W, JF_N, JF_E, JF_S };

/* Una sala pegada a `from` hacia `d` (con `def` = una especial); `link`: con
   paso entre las dos */
static int river_add(River *r, int from, int d, int def, int link)
{
    int fx = r->cx[from], fy = r->cy[from], x = fx + k_adx[d], y = fy + k_ady[d];
    if (x < 0 || y < 0 || x >= RIV_G || y >= RIV_G || r->id[y][x] >= 0)
        return -1;
    int k = r->n++;
    r->id[y][x] = (int8_t)k;
    r->cx[k] = x;
    r->cy[k] = y;
    r->def[y][x] = (int16_t)def;
    r->fixed[y][x] = def != 0;
    if (link && d < 4) {
        r->ex[fy][fx] |= (uint8_t)k_aflag[d];
        r->ex[y][x] |= (uint8_t)k_aflag[(d + 2) & 3];
    }
    return k;
}

/* DRLGMAZE_MergeMazeRooms: con las comunes de al lado, la mitad de las veces un paso */
static void river_merge(River *r, int k)
{
    int x = r->cx[k], y = r->cy[k];
    for (int d = 0; d < 4; d++) {
        int nx = x + k_adx[d], ny = y + k_ady[d];
        if (nx < 0 || ny < 0 || nx >= RIV_G || ny >= RIV_G || r->id[ny][nx] < 0 || r->fixed[ny][nx] ||
            (r->ex[y][x] & k_aflag[d]))
            continue;
        if ((int)(rnd() % 1000u) < RIV_MERGE) {
            r->ex[y][x] |= (uint8_t)k_aflag[d];
            r->ex[ny][nx] |= (uint8_t)k_aflag[(d + 2) & 3];
        }
    }
}

/* La forma del río (lo mismo cada vez con la misma semilla): las salas
   (DRLGMAZE_BuildBasicMaze), la subida al sur de la de más abajo, los tres
   puentes al norte de la de más arriba, la Forja (una sala sin salida al
   oeste o al este, o una nueva pegada) y la lava alrededor */
static void river_plan(River *r)
{
    memset(r, 0, sizeof *r);
    memset(r->id, -1, sizeof r->id);
    r->cx[0] = r->cy[0] = RIV_G / 2;
    r->id[RIV_G / 2][RIV_G / 2] = 0;
    r->n = 1;
    for (int tries = 0; r->n < RIV_ROOMS && tries < 4000; tries++) {
        int from = (int)(rnd() % (uint32_t)r->n), d = (int)(rnd() & 3), k = river_add(r, from, d, 0, 1);
        if (k >= 0)
            river_merge(r, k);
    }
    int s = -1, nth = -1;
    for (int k = 0; k < r->n; k++) {
        int x = r->cx[k], y = r->cy[k];
        if (y + 1 < RIV_G && r->id[y + 1][x] < 0 && (s < 0 || y > r->cy[s]))
            s = k;
        if (y > 0 && r->id[y - 1][x] < 0 && (nth < 0 || y < r->cy[nth]))
            nth = k;
    }
    if (s >= 0)
        river_add(r, s, 3, LP_LAVA_WARP, 1);
    int b = nth >= 0 ? river_add(r, nth, 1, LP_BRIDGE_1, 1) : -1;
    for (int i = 0; i < 2 && b >= 0; i++)
        b = river_add(r, b, 1, LP_BRIDGE_2, 1);
    r->bx = b >= 0 ? r->cx[b] : RIV_G / 2;
    r->by = b >= 0 ? r->cy[b] : 0;
    for (int k = 0; k < r->n; k++)
        if (!r->fixed[r->cy[k]][r->cx[k]])
            r->def[r->cy[k]][r->cx[k]] = (int16_t)(LP_LAVA_X + r->ex[r->cy[k]][r->cx[k]]);
    /* La Forja: { LAVA_W, FORGE_W, hacia el este } o { LAVA_E, FORGE_E, hacia el oeste } */
    int e = (int)(rnd() & 1), want = LP_LAVA_X + (e ? JF_E : JF_W), forge = e ? LP_FORGE_E : LP_FORGE_W, done = 0;
    for (int k = 0; k < r->n && !done; k++) {
        int x = r->cx[k], y = r->cy[k];
        if (!r->fixed[y][x] && r->def[y][x] == want) {
            r->def[y][x] = (int16_t)forge;
            r->fixed[y][x] = done = 1;
        }
    }
    for (int k = 0; k < r->n && !done; k++) {
        int x = r->cx[k], y = r->cy[k];
        if (!r->fixed[y][x] && river_add(r, k, e ? 0 : 2, forge, 1) >= 0) {
            r->def[y][x] = (int16_t)(LP_LAVA_X + r->ex[y][x]);
            done = 1;
        }
    }
    /* La lava alrededor de cada sala, salvo el último puente */
    int n0 = r->n, last = r->id[r->by][r->bx];
    for (int k = 0; k < n0; k++)
        for (int d = 0; d < 8 && k != last; d++)
            river_add(r, k, d, LP_LAVA_X, 0);
    r->x0 = r->y0 = RIV_G;
    r->x1 = r->y1 = -1;
    for (int k = 0; k < r->n; k++) {
        r->x0 = r->cx[k] < r->x0 ? r->cx[k] : r->x0;
        r->y0 = r->cy[k] < r->y0 ? r->cy[k] : r->y0;
        r->x1 = r->cx[k] > r->x1 ? r->cx[k] : r->x1;
        r->y1 = r->cy[k] > r->y1 ? r->cy[k] : r->y1;
    }
}

static void build_river(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    River r;
    river_plan(&r);
    for (int k = 0; k < r.n; k++) {
        int x = zone->x0 + (r.cx[k] - r.x0) * RIV_ROOM, y = zone->y0 + (r.cy[k] - r.y0) * RIV_ROOM;
        prest_stamp(m, z, r.def[r.cy[k]][r.cx[k]], -1, x, y);
        room_add(x, y, RIV_ROOM, RIV_ROOM);
    }
}

/* ---- El Santuario del Caos ---- */

static void build_sanctum(Map *m, int z)
{
    /* nLavaLvlPrestIds: la entrada abajo en el medio, los brazos alrededor del corazón */
    static const int k_grid[25] = {
        LP_LAVA_X, LP_LAVA_X, LP_LAVA_X, LP_LAVA_X, LP_LAVA_X,
        LP_LAVA_X, LP_LAVA_X, LP_ARM_N, LP_LAVA_X, LP_LAVA_X,
        LP_LAVA_X, LP_ARM_W, LP_HEART, LP_ARM_E, LP_LAVA_X,
        LP_LAVA_X, LP_LAVA_X, LP_ARM_S, LP_LAVA_X, LP_LAVA_X,
        LP_LAVA_X, LP_LAVA_X, LP_DIABLO_ENTRY, LP_LAVA_X, LP_LAVA_X,
    };
    const Zone *zone = &s_zones[z];
    s_ncycle = 0;
    for (int i = 0; i < 25; i++) {
        int x = zone->x0 + RIV_ROOM * (i % 5), y = zone->y0 + RIV_ROOM * (i / 5);
        prest_stamp(m, z, k_grid[i], outdoor_pick(k_grid[i]), x, y);
        room_add(x, y, RIV_ROOM, RIV_ROOM);
    }
}

/* ---- El mundo del Acto 4 ---- */

static int layout_act4(Map *m)
{
    prest_tables_load();
    /* En coordenadas del original (y hacia el sur): la Fortaleza en (0, 0) y
       el Outer Steppes al este, con el paso arriba (0x400000) o abajo (0x800000) */
    DRect *r = s_mesa;
    r[0] = (DRect){ 0, 0, FORT_W, FORT_H };
    r[1] = (DRect){ FORT_W, 0, 80, 64 }; /* levels.txt: SizeX, SizeY */
    r[2] = (DRect){ 0, 0, 64, 80 };
    r[3] = (DRect){ 0, 0, 80, 64 };
    if (!(rnd() & 1)) {
        r[1].y = r[0].y - 8;
        s_mesa_trans = 1;
    } else {
        r[1].y = r[0].y + r[0].h - r[1].h + 8;
        s_mesa_trans = 4;
    }
    if (!mesa_chain(r, 2)) {
        fprintf(stderr, "no pude armar las mesetas\n");
        return -1;
    }
    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (int i = 0; i < 4; i++) {
        minx = r[i].x < minx ? r[i].x : minx;
        miny = r[i].y < miny ? r[i].y : miny;
        maxx = r[i].x + r[i].w > maxx ? r[i].x + r[i].w : maxx;
        maxy = r[i].y + r[i].h > maxy ? r[i].y + r[i].h : maxy;
    }
    for (int i = 0; i < 4; i++) {
        r[i].x += CELL - minx;
        r[i].y += CELL - miny;
    }
    z_town = add_zone(ZK_FORTRESS, 0, r[0].x, r[0].y, r[0].x + r[0].w, r[0].y + r[0].h);
    add_level("The Pandemonium Fortress", z_town, r[0].x, r[0].y, r[0].x + r[0].w, r[0].y + r[0].h, 0, 0);
    for (int i = 0; i < 3; i++) {
        const DRect *q = &r[1 + i];
        z_mesa[i] = add_zone(ZK_MESA, i, q->x, q->y, q->x + q->w, q->y + q->h);
        add_level(k_mesa_names[i], z_mesa[i], q->x, q->y, q->x + q->w, q->y + q->h, 20, 0);
    }
    /* El río y el Santuario, debajo: el último puente pegado a la entrada
       del Santuario (la pieza de abajo en el medio) */
    s_pack_x = 0;
    s_pack_y = maxy - miny + 2 * CELL + PACK_GAP;
    s_pack_h = 0;
    z_sanctum = add_zone(ZK_SANCTUM, 0, 0, 0, 0, 0);
    z_river = add_zone(ZK_RIVER, 0, 0, 0, 0, 0);
    uint32_t saved = s_rng;
    s_rng = s_zones[z_river].seed;
    river_plan(&s_river);
    s_rng = saved;
    int rw = (s_river.x1 - s_river.x0 + 1) * RIV_ROOM, rh = (s_river.y1 - s_river.y0 + 1) * RIV_ROOM;
    int rx = 2 * RIV_ROOM - (s_river.bx - s_river.x0) * RIV_ROOM; /* (con el Santuario en x = 0) */
    int left = rx < 0 ? rx : 0, right = rx + rw > SANCT_W ? rx + rw : SANCT_W, px, py;
    pack(right - left, SANCT_W + rh, &px, &py);
    int sx = px - left, sy = py, ry = sy + SANCT_W + (s_river.y0 - s_river.by) * RIV_ROOM;
    rx += sx;
    Zone *zs = &s_zones[z_sanctum], *zr = &s_zones[z_river];
    zs->x0 = sx;
    zs->y0 = sy;
    zs->x1 = sx + SANCT_W;
    zs->y1 = sy + SANCT_W;
    zr->x0 = rx;
    zr->y0 = ry;
    zr->x1 = rx + rw;
    zr->y1 = ry + rh;
    add_level("River of Flame", z_river, rx, ry, rx + rw, ry + rh, 30, 0);
    add_level("Chaos Sanctum", z_sanctum, sx, sy, sx + SANCT_W, sy + SANCT_W, 20, 0);
    for (int i = 0; i < s_num_levels; i++) /* los saltos de las Vis de levels.txt (para buscar caminos) */
        for (int k = 0; k < 8; k++) {
            int a = s_levels[i].zone, b = zone_of_level_id(s_levels[i].vis[k]);
            if (!((a == z_river && b == z_sanctum) || (a == z_sanctum && b == z_river))) /* (ahí se pasa caminando) */
                add_jump(a, b);
        }
    int width = maxx - minx + 2 * CELL > PACK_W ? maxx - minx + 2 * CELL : PACK_W, height = s_pack_y + s_pack_h;
    if (map_create(m, width, height) != 0)
        return -1;
    /* Los pasos caminando: la Fortaleza y el Outer Steppes (por la pieza de
       paso), las mesetas entre sí (el borde abierto del medio) y el último
       puente con el Santuario (en subtiles) */
    add_link(z_town, z_mesa[0], (float)(r[1].x * TILE_SUB), (float)((r[0].y + FORT_H / 2) * TILE_SUB));
    desert_openings(z_mesa[0], z_mesa[1], 1);
    desert_openings(z_mesa[1], z_mesa[2], 1);
    add_link(z_river, z_sanctum, (float)((sx + 2 * RIV_ROOM + RIV_ROOM / 2) * TILE_SUB), (float)((sy + SANCT_W) * TILE_SUB));
    if (getenv("D2_DEBUG")) {
        printf("mundo del Acto 4 de %dx%d tiles (paso en la fila %d), %d zonas\n", width, height, s_mesa_trans, s_num_zones);
        for (int z = 0; z < s_num_zones; z++)
            printf("  zona %2d: %-26s (%d, %d)-(%d, %d)\n", z, s_zones[z].name, s_zones[z].x0, s_zones[z].y0, s_zones[z].x1,
                   s_zones[z].y1);
        for (int y = s_river.y0; y <= s_river.y1; y++) {
            printf("   ");
            for (int x = s_river.x0; x <= s_river.x1; x++)
                printf(" %4d", s_river.def[y][x]);
            printf("\n");
        }
    }
    return 0;
}
