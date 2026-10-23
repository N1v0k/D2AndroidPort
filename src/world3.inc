/* world3.inc: el mundo del Acto 3 (lo incluye world.c).
   Como el original (D2MOO, DrlgOutPlace.cpp y DrlgOutJung.cpp):
   - el muelle de Kurast;
   - la selva: tres niveles de 64x192 armados con piezas de 32x32
     (DRLG_GenerateJungles y DRLGOUTJUNG_BuildJungle);
   - Kurast apilada hacia el norte desde la última selva: la baja, el bazar
     y la alta (bordes, puertas y piezas al azar de 16x16, 8x16 y 8x8), el
     Causeway y Travincal (DRLGOUTPLACE_InitAct3OutdoorLevel).
   Las piezas salen de lvlprest.txt por su Def (el número de LVLPREST_*). */

#define JUNGLE_W 64
#define JUNGLE_H 192
#define JB       32 /* las piezas de la selva: 32x32 tiles */
#define JG_MAX   40 /* la grilla de piezas de las tres selvas (con un margen) */
#define KURAST_W 80
#define KURAST_H 64
#define CAUSE_W  48
#define CAUSE_H  16
#define TRAV_W   64
#define TRAV_H   64

/* Def de lvlprest.txt (LVLPREST_ACT3_* de D2MOO, LevelsIds.h) */
enum {
    LP_TOWN3 = 529, /* + las salidas (1..15): las piezas del río, RivW..RivNSEW */
    LP_HEAD = 573, LP_TAIL = 574,
    LP_WEBBY = 575, /* los claros con telarañas; +10 los del pantano, +20 los de los pigmeos */
    LP_SLUMS_BORDER_N = 605, LP_SLUMS_BORDER_S, LP_SLUMS_BORDER_E, LP_SLUMS_BORDER_W, LP_SLUMS_BORDER_NE,
    LP_SLUMS_BORDER_NW, LP_SLUMS_BORDER_SE, LP_SLUMS_BORDER_SW, LP_SLUMS_GATE_N, LP_SLUMS_GATE_S, LP_SLUMS_08X08,
    LP_SLUMS_08X16, LP_SLUMS_16X08, LP_SLUMS_16X16,
    LP_BURBS_BORDER_N = 619, LP_BURBS_BORDER_S, LP_BURBS_BORDER_E, LP_BURBS_BORDER_W, LP_BURBS_BORDER_NE,
    LP_BURBS_BORDER_NW, LP_BURBS_BORDER_SE, LP_BURBS_BORDER_SW, LP_BURBS_GATE_N, LP_BURBS_GATE_S, LP_BURBS_SEWER,
    LP_BURBS_TEMPLE, LP_BURBS_WAYPOINT, LP_BURBS_08X08, LP_BURBS_08X16, LP_BURBS_16X08, LP_BURBS_16X16,
    LP_METRO_BORDER_N = 636, LP_METRO_BORDER_S, LP_METRO_BORDER_E, LP_METRO_BORDER_W, LP_METRO_BORDER_NE,
    LP_METRO_BORDER_NW, LP_METRO_BORDER_SE, LP_METRO_BORDER_SW, LP_METRO_GATE_N, LP_METRO_GATE_S, LP_METRO_SEWER,
    LP_METROTEMPLE, LP_METRO_08X08, LP_METRO_08X16, LP_METRO_16X08, LP_METRO_16X16,
    LP_BRIDGE = 652,
    LP_TRAV_NW = 653, LP_TRAV_N, LP_TRAV_NE, LP_TRAV_SW, LP_TRAV_S, LP_TRAV_SE,
};
enum { JF_W = 1, JF_E = 2, JF_S = 4, JF_N = 8 }; /* JUNGLE_FLAG_LEFT_, RIGHT_, BOTTOM_, TOP_ */
#define J_ATTACH 2 /* JUNGLE_PRESET2_ATTACH_POINT */

/* gJunglePresets: la pieza con una salida principal (1..15) y otra a un
   claro, del lado oeste, este, sur o norte (los "RivWoE": río al oeste, claro al este) */
static const int k_jungle_pres[60] = {
    0, 545, 546, 547, 548, 0, 549, 550, 0, 0, 551, 552, 553, 554, 0, 555, 0, 556, 0, 557, 558, 0, 0, 559, 0, 0, 0, 560,
    561, 562, 563, 0, 0, 564, 565, 0, 566, 0, 567, 0, 0, 0, 568, 0, 569, 570, 0, 0, 0, 571, 0, 0, 572, 0, 0, 0,
    0, 0, 0, 0,
};
/* gSpiderForestPresets: los claros (solo la salida al claro) */
static const int k_clearing_pres[17] = { 0, 575, 576, 577, 578, 579, 580, 0, 581, 582, 583, 0, 584, 0, 0, 0, 0 };

typedef struct {
    int x, y;              /* esquina en tiles (y crece hacia el sur) */
    int dir;               /* de qué lado de su base quedó (sub_6FD83970: 0 arriba, 1-4 a los costados) */
    int base, branch[3], nbranch;
    int bx, by;            /* su primera pieza en la grilla */
    int defs[12], nclear;  /* las piezas de sus 2x6 lugares (Def) y cuántos claros */
} Jungle;

static Jungle   s_jungle[3]; /* de sur a norte: Spider Forest, Great Marsh y Flayer Jungle */
static int      s_jungle_interlink; /* bJungleInterlink: de qué lado quedan las puertas de Kurast */
static int      z_jungle[3], z_kurast[3], z_causeway, z_travincal;
static TxtTable s_lvlprest_txt, s_lvltypes_txt;
/* La fila de lvlprest.txt de cada Def (0 = no está): al armar una zona en
   partes se busca cada pieza en cada pasada, y buscarla entre las ~1100
   filas costaba más que estamparla */
#define PREST_DEFS 2048
static int s_prest_row[PREST_DEFS];

static int prest_row(int def)
{
    return def >= 0 && def < PREST_DEFS && s_prest_row[def] > 0 ? s_prest_row[def] : -1;
}
/* Los pasos entre selvas vecinas (las selvas por su número y el medio del borde, en tiles) */
static int s_jlinks[8][2], s_jlink_xy[8][2], s_njlinks;

static void jlink_add(int a, int b, int x, int y)
{
    for (int l = 0; l < s_njlinks; l++)
        if ((s_jlinks[l][0] == a && s_jlinks[l][1] == b) || (s_jlinks[l][0] == b && s_jlinks[l][1] == a))
            return; /* un paso por par (el de más arriba) */
    if (s_njlinks < 8) {
        s_jlinks[s_njlinks][0] = a;
        s_jlinks[s_njlinks][1] = b;
        s_jlink_xy[s_njlinks][0] = x;
        s_jlink_xy[s_njlinks][1] = y;
        s_njlinks++;
    }
}

/* El archivo de una pieza: File1..File6 de su fila (variant < 0: uno al azar) */
static int prest_file(int def, int variant, char *out, size_t cap, int *w, int *h)
{
    const TxtTable *t = &s_lvlprest_txt;
    int row = prest_row(def);
    if (row < 0)
        return -1;
    const char *files[6];
    int n = 0;
    for (int k = 0; k < 6; k++) {
        char col[8];
        snprintf(col, sizeof col, "File%d", k + 1);
        const char *f = txt_get(t, row, txt_col(t, col));
        if (f && f[0] && strcmp(f, "0") != 0)
            files[n++] = f;
    }
    if (n == 0)
        return -1;
    int k = variant >= 0 && variant < n ? variant : (int)(rnd() % (uint32_t)n);
    size_t len = (size_t)snprintf(out, cap, "data/global/tiles/%s", files[k]);
    for (size_t i = 0; i < len && i < cap; i++)
        out[i] = out[i] == '\\' ? '/' : (char)tolower((unsigned char)out[i]);
    if (w)
        *w = txt_get_int(t, row, "SizeX", 0);
    if (h)
        *h = txt_get_int(t, row, "SizeY", 0);
    return 0;
}

/* Los DT1 de la pieza, como DRLGROOMTILE_LoadDT1FilesForRoom: los de
   lvltypes.txt (el tipo del nivel de la zona) que pide su Dt1Mask, más
   Blank e InvisWal (Warp no: ver map_stamp) */
/* Los de lvltypes.txt del tipo del nivel de la zona que pide ese Dt1Mask */
static int prest_lib_mask(uint32_t mask, int z, char paths[34][96])
{
    const TxtTable *lt = &s_lvltypes_txt;
    const Level *lv = zone_level(z);
    static int s_type_row[256]; /* la fila de lvltypes.txt de cada nivel (más 1; 0 = sin buscar) */
    int lid = lv ? lv->id : -1, n = 0, trow;
    if (lid >= 0 && lid < 256 && s_type_row[lid]) {
        trow = s_type_row[lid] - 1;
    } else {
        char id[12];
        snprintf(id, sizeof id, "%d", lid);
        int lrow = s_levels_txt.rows ? txt_find_row(&s_levels_txt, txt_col(&s_levels_txt, "Id"), id) : -1;
        snprintf(id, sizeof id, "%d", lrow >= 0 ? txt_get_int(&s_levels_txt, lrow, "LevelType", -1) : -1);
        trow = lt->rows ? txt_find_row(lt, txt_col(lt, "Id"), id) : -1;
        if (lid >= 0 && lid < 256 && lt->rows)
            s_type_row[lid] = trow + 1;
    }
    if (trow < 0)
        return 0;
    for (int k = 0; k < 32; k++) {
        char col[8];
        snprintf(col, sizeof col, "File %d", k + 1);
        const char *f = txt_get(lt, trow, txt_col(lt, col));
        if ((mask >> k & 1u) && f && f[0] && strcmp(f, "0") != 0)
            snprintf(paths[n++], 96, "data/global/tiles/%s", f);
    }
    snprintf(paths[n++], 96, "data/global/tiles/act1/outdoors/blank.dt1");
    snprintf(paths[n++], 96, "data/global/tiles/act1/barracks/inviswal.dt1");
    return n;
}

static int prest_lib(int def, int z, char paths[34][96])
{
    const TxtTable *t = &s_lvlprest_txt;
    int row = prest_row(def);
    return row < 0 ? 0 : prest_lib_mask((uint32_t)txt_get_int(t, row, "Dt1Mask", 0), z, paths);
}

static void prest_tables_load(void);

int world_zone_dt1(int zone, char paths[34][96])
{
    if (zone < 0 || zone >= s_num_zones)
        return 0;
    switch (s_zones[zone].kind) {
    case ZK_DOCK: case ZK_JUNGLE: case ZK_KURAST: case ZK_CAUSEWAY: case ZK_TRAVINCAL: case ZK_A3MAZE: case ZK_A3PRESET:
    case ZK_FORTRESS: case ZK_MESA: case ZK_RIVER: case ZK_SANCTUM: case ZK_HARROGATH: case ZK_SIEGE: case ZK_BARRICADE:
        return prest_lib_mask(0xFFFFFFFFu, zone, paths); /* (todas sus piezas: todo el Dt1Mask) */
    case ZK_TOWN: case ZK_LUT:
        return 0; /* (se arman al llegar al acto, con la pantalla de carga) */
    default:
        /* Los de los Actos 1 y 2 estampan con los DT1 que lista cada DS1,
           que salen de los del tipo del nivel en lvltypes.txt */
        prest_tables_load();
        return prest_lib_mask(0xFFFFFFFFu, zone, paths);
    }
}

/* Estampa la pieza en la zona (con sus saltos a los niveles de Vis) */
static void prest_stamp(Map *m, int z, int def, int variant, int x, int y)
{
    char path[160], lib[34][96];
    const char *libp[34];
    int w = 0, h = 0;
    if (prest_file(def, variant, path, sizeof path, &w, &h) != 0) {
        fprintf(stderr, "lvlprest.txt no tiene la pieza %d\n", def);
        return;
    }
    if (!map_piece_now(m)) /* (va en otra pasada del armado en partes) */
        return;
    int n = prest_lib(def, z, lib);
    for (int k = 0; k < n; k++)
        libp[k] = lib[k];
    int first = m->num_specials;
    map_stamp_lib_now(m, path, x, y, n ? libp : NULL, n);
    if (w <= 0) { /* las que son un nivel entero (el muelle, los templos): toda la zona */
        w = s_zones[z].x1 - x;
        h = s_zones[z].y1 - y;
    }
    vis_warps(m, first, x, y, w + 1, h + 1, z);
}

/* ---- La selva ---- */

static int jungle_overlap(const Jungle *a, const Jungle *b)
{
    return a->x < b->x + JUNGLE_W && b->x < a->x + JUNGLE_W && a->y < b->y + JUNGLE_H && b->y < a->y + JUNGLE_H;
}

/* sub_6FD83970: arriba de la base, o a un costado y corrida un tercio o dos tercios hacia el norte */
static void jungle_place(Jungle *j, int bx, int by, int dir)
{
    static const int dx[5] = { 0, -JUNGLE_W, JUNGLE_W, -JUNGLE_W, JUNGLE_W };
    static const int dy[5] = { -JUNGLE_H, -JUNGLE_H / 3, -JUNGLE_H / 3, -2 * JUNGLE_H / 3, -2 * JUNGLE_H / 3 };
    j->x = bx + dx[dir];
    j->y = by + dy[dir];
    j->dir = dir;
}

/* DRLG_GenerateJunglesAttachPoints: el camino en zigzag de cada selva (p1,
   con números seguidos), sus puntos de unión (p2: 1 fijo, J_ATTACH posible)
   y de qué selva es cada pieza (p0) */
static void jungle_attach(int gw, int *p0, int *p1, int *p2, int *lp)
{
    int n = gw * JG_MAX;
    memset(p0, 0, sizeof(int) * (size_t)n);
    memset(p1, 0, sizeof(int) * (size_t)n);
    memset(p2, 0, sizeof(int) * (size_t)n);
    memset(lp, 0, sizeof(int) * (size_t)n);
    for (int ji = 0; ji < 3; ji++) {
        Jungle *jg = &s_jungle[ji];
        int line = ji != 0 ? jg->dir % 2 : (int)(rnd() % 2u);
        int attach = 0;
        while (attach < 2) {
            for (int y = 0; y < 6; y++)
                for (int x = 0; x < 2; x++) {
                    int k = (jg->by + y) * gw + jg->bx + x;
                    p0[k] = ji + 1;
                    p1[k] = p2[k] = lp[k] = 0;
                }
            int first = jg->by, last = jg->by + 5;
            if (ji != 0) { /* la entrada desde su base: |1|2| o |2|1| */
                int k = last * gw + jg->bx;
                p2[k + line] = 1;
                p2[k + !line] = J_ATTACH;
            }
            for (int b = 0; b < jg->nbranch; b++) { /* las salidas a las selvas que se apoyan en esta */
                static const int ox[5] = { 0, 0, 1, 0, 1 }, oy[5] = { 0, 3, 3, 1, 1 };
                int d = s_jungle[jg->branch[b]].dir;
                p2[jg->bx + ox[d] + gw * (jg->by + oy[d])] = 1;
            }
            attach = ji != 0;
            int x = jg->bx, v = 100 * (ji + 1), col = 0;
            for (int y = last; y >= first;) {
                p1[y * gw + x + line] = v++;
                if (col == 0 || y == first || rnd() % 3u) {
                    col++;
                    if (col >= 2 && y > 1) {
                        int k = y * gw + x + !line;
                        if (p2[k] == 0) {
                            p2[k] = J_ATTACH;
                            attach++;
                        }
                    }
                    y--;
                } else {
                    col = 0;
                    line = !line;
                }
            }
        }
        while (attach > 3) { /* a lo sumo tres */
            int drop = (int)(rnd() % (uint32_t)attach), found = 0, done = 0;
            for (int y = 0; y < 6 && !done; y++)
                for (int x = 0; x < 2 && !done; x++) {
                    int k = (jg->by + y) * gw + jg->bx + x;
                    if (p2[k] == J_ATTACH) {
                        if (found == drop) {
                            p2[k] = 0;
                            attach--;
                            done = 1;
                        }
                        found++;
                    }
                }
        }
    }
}

/* DRLG_JungleComputeConnexity: las salidas de cada pieza (el camino y las uniones) */
static void jungle_connect(int gw, int gh, const int *p0, const int *p1, const int *p2, int *lp)
{
    for (int y = 1; y < gh - 1; y++)
        for (int x = 1; x < gw - 1; x++) {
            int k = y * gw + x, flags = 0, best = 0x7FFFFFFF;
            const int nb[4] = { k - gw, k + gw, k + 1, k - 1 }, fl[4] = { JF_N, JF_S, JF_E, JF_W },
                      opp[4] = { JF_S, JF_N, JF_W, JF_E };
            if (p2[k] == 1) {
                for (int d = 0; d < 4; d++)
                    if (p1[nb[d]] != 0 && p1[nb[d]] < best && p0[nb[d]] == p0[k]) {
                        flags = fl[d];
                        best = p1[nb[d]];
                    }
                for (int d = 0; d < 4; d++)
                    if (flags & fl[d])
                        lp[nb[d]] |= opp[d];
                for (int d = 0; d < 4; d++)
                    if (p2[nb[d]] == 1 && p0[nb[d]] != p0[k])
                        flags |= fl[d];
            }
            if (p1[k])
                for (int d = 0; d < 4; d++)
                    if (abs(p1[nb[d]] - p1[k]) == 1)
                        flags |= fl[d];
            lp[k] |= flags;
        }
}

/* DRLG_JungleUpdateAttachPointsDirections: cada unión posible se cuelga de
   una pieza del camino de su selva (y quizás de otra unión); 0 si alguna no
   pudo: se vuelve a empezar */
static int jungle_attach_dirs(int gw, int gh, const int *p0, int *p2, int *lp)
{
    for (int y = 1; y < gh - 1; y++)
        for (int x = 1; x < gw - 1; x++) {
            int k = y * gw + x, base = (int)(rnd() % 4u);
            if (p2[k] != J_ATTACH)
                continue;
            const int nb[4] = { k - gw, k + gw, k + 1, k - 1 }, fl[4] = { JF_N, JF_S, JF_E, JF_W },
                      opp[4] = { JF_S, JF_N, JF_W, JF_E };
            int id = lp[k], had_none = id == 0, look = 1;
            for (int i = 0; i < 4 && look; i++) {
                int d = (base + i) % 4;
                if (p0[nb[d]] == p0[k] && lp[nb[d]] && lp[nb[d]] < 15) {
                    id |= fl[d] << 4;
                    look = 0;
                }
            }
            if (!id)
                return 0;
            if (had_none) {
                look = 1;
                for (int i = 0; i < 4 && look; i++) {
                    int d = (base + i) % 4;
                    if (p0[nb[d]] != p0[k] && p2[nb[d]] == J_ATTACH) {
                        id |= fl[d] << 4;
                        look = 0;
                    }
                }
                look = (rnd() % 2u) != 0 && look;
                base = (int)(rnd() % 4u);
                for (int i = 0; i < 4 && look; i++) {
                    int d = (base + i) % 4;
                    if (lp[nb[d]] && lp[nb[d]] < 15 && !(id & (fl[d] << 4))) {
                        id |= fl[d] << 4;
                        look = 0;
                    }
                }
            }
            for (int d = 0; d < 4; d++)
                if (id & (fl[d] << 4))
                    lp[nb[d]] |= opp[d] << 4;
            p2[k] = 0;
            lp[k] |= id;
        }
    return 1;
}

/* DRLG_JungleNormalizeLevelPresetId: las salidas de la pieza -> su Def */
static int jungle_def(int id)
{
    int p = id % 16;
    if (p != 0) {
        if (id < 16)
            return LP_TOWN3 + p;
        int d = 0, o = 4 * (p - 1);
        if (id & (JF_W << 4)) d = k_jungle_pres[o];
        if (id & (JF_E << 4)) d = k_jungle_pres[o + 1];
        if (id & (JF_S << 4)) d = k_jungle_pres[o + 2];
        if (id & (JF_N << 4)) d = k_jungle_pres[o + 3];
        return d;
    }
    return id >= 16 && (id >> 4) < 17 ? k_clearing_pres[id >> 4] : 0;
}

/* DRLG_GenerateJungles: dónde va cada selva (la primera arriba del muelle)
   y sus piezas. Devuelve 0 si salió. */
static int jungle_generate(int town_x, int town_y)
{
    memset(s_jungle, 0, sizeof s_jungle);
    jungle_place(&s_jungle[0], town_x, town_y, 0);
    for (int i = 1; i < 3; i++) {
        int base = (int)(rnd() % (uint32_t)i), clash = 0;
        jungle_place(&s_jungle[i], s_jungle[base].x, s_jungle[base].y, (int)(rnd() % 5u));
        for (int j = 0; j < i && !clash; j++)
            clash = jungle_overlap(&s_jungle[j], &s_jungle[i]);
        if (clash) {
            i--;
            continue;
        }
        s_jungle[i].base = base;
        s_jungle[base].branch[s_jungle[base].nbranch++] = i;
    }
    int minx = s_jungle[0].x, miny = s_jungle[0].y, maxx = minx + JUNGLE_W, maxy = miny + JUNGLE_H;
    for (int i = 1; i < 3; i++) { /* (el original no baja miny: acá entran las tres) */
        minx = s_jungle[i].x < minx ? s_jungle[i].x : minx;
        miny = s_jungle[i].y < miny ? s_jungle[i].y : miny;
        maxx = s_jungle[i].x + JUNGLE_W > maxx ? s_jungle[i].x + JUNGLE_W : maxx;
        maxy = s_jungle[i].y + JUNGLE_H > maxy ? s_jungle[i].y + JUNGLE_H : maxy;
    }
    int gw = (maxx - minx) / JB + 2, gh = (maxy - miny) / JB + 2;
    if (gw > JG_MAX || gh > JG_MAX)
        return -1;
    for (int i = 0; i < 3; i++) {
        s_jungle[i].bx = (s_jungle[i].x - minx) / JB + 1;
        s_jungle[i].by = (s_jungle[i].y - miny) / JB + 1;
    }
    static int p0[JG_MAX * JG_MAX], p1[JG_MAX * JG_MAX], p2[JG_MAX * JG_MAX], lp[JG_MAX * JG_MAX];
    int tries = 0;
    do {
        if (++tries > 200)
            return -1;
        jungle_attach(gw, p0, p1, p2, lp);
        jungle_connect(gw, gh, p0, p1, p2, lp);
    } while (!jungle_attach_dirs(gw, gh, p0, p2, lp));
    /* Las uniones entre selvas: los pasos para buscar caminos, en el borde de
       las dos piezas. Entre dos claros, a 13 tiles del lado de arriba o de la
       izquierda (por ahí cruza su camino: en el medio es selva); si no, en el
       medio (el río, que se cruza por las dos orillas) */
    for (int y = 1; y < gh - 1; y++)
        for (int x = 1; x < gw - 1; x++) {
            int k = y * gw + x;
            if (!p0[k])
                continue;
            if (p0[k + 1] && p0[k + 1] != p0[k] && (lp[k] & (JF_E | JF_E << 4)) && (lp[k + 1] & (JF_W | JF_W << 4))) {
                int at = !(lp[k] & JF_E) && !(lp[k + 1] & JF_W) ? 13 : JB / 2;
                jlink_add(p0[k] - 1, p0[k + 1] - 1, minx + (x - 1 + 1) * JB, miny + (y - 1) * JB + at);
            }
            if (p0[k + gw] && p0[k + gw] != p0[k] && (lp[k] & (JF_S | JF_S << 4)) && (lp[k + gw] & (JF_N | JF_N << 4))) {
                int at = !(lp[k] & JF_S) && !(lp[k + gw] & JF_N) ? 13 : JB / 2;
                jlink_add(p0[k] - 1, p0[k + gw] - 1, minx + (x - 1) * JB + at, miny + (y - 1 + 1) * JB);
            }
        }
    for (int i = 0; i < 3; i++) {
        Jungle *jg = &s_jungle[i];
        jg->nclear = 0;
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 2; x++) {
                int def = jungle_def(lp[(jg->by + y) * gw + jg->bx + x]);
                jg->defs[y * 2 + x] = def;
                jg->nclear += def > LP_TAIL;
            }
    }
    return 0;
}

/* DRLGOUTJUNG_BuildJungle: la selva k (0 Spider Forest, 1 Great Marsh, 2
   Flayer Jungle) en su zona: la entrada desde el muelle abajo del Spider
   Forest, la salida a Kurast arriba del Flayer Jungle, los claros (con
   telarañas, pantano o pigmeos) con su archivo (el que trae el waypoint o
   una cueva) */
static void build_jungle(Map *m, int z)
{
    static const int k_files[18] = { 0, 1, 2, 1, 0, 2, 0, 2, 1, 1, 2, 0, 2, 0, 1, 2, 1, 0 };
    const Zone *zone = &s_zones[z];
    const Jungle *jg = &s_jungle[zone->floor];
    int nrand = (int)(rnd() % (uint32_t)(jg->nclear == 3 ? 6 : 2)), file = 0;
    for (int y = 0; y < 6; y++) {
        if (zone->floor == 0 && y == 5) { /* Spider Forest: la pieza del muelle */
            prest_stamp(m, z, LP_HEAD, jg->defs[11] == 0, zone->x0, zone->y0 + y * JB);
            continue;
        }
        if (zone->floor == 2 && y == 0) { /* Flayer Jungle: la de Kurast */
            prest_stamp(m, z, LP_TAIL, jg->defs[1] == 0, zone->x0, zone->y0);
            continue;
        }
        for (int x = 0; x < 2; x++) {
            int def = jg->defs[y * 2 + x], variant = -1;
            if (def > LP_TAIL) {
                def += 10 * zone->floor;
                variant = file < 3 ? k_files[file + 3 * nrand] : -1;
                file++;
            }
            if (def)
                prest_stamp(m, z, def, variant, zone->x0 + x * JB, zone->y0 + y * JB);
            else /* sin camino: barro (DRLGOUTPLACE_CreateOutdoorRoomEx con el tercer DT1 de lvltypes, DarkMud)
                    que no se camina: en la selva su piso lleva 0x120000 (DRLGOUTPLACE_InitOutdoorRoomGrids), el
                    bit 17 es bUnwalkable -> MAPTILE_UNWALKABLE -> COLLIDE_WALL. Caminable, ahí caían
                    monstruos y jefes (el de la estatuilla) encerrados entre los bordes de las vecinas */
                map_fill_floor_coll(m, TILES3 "ground/darkmud.dt1", 0, 0, COLL_WALL, zone->x0 + x * JB, zone->y0 + y * JB, JB,
                                    JB);
        }
    }
}

/* ---- Kurast ---- */

typedef struct {
    int x0, y0, cw, ch;
    uint8_t used[16][16];
} KGrid;

static int kgrid_fits(const KGrid *g, int cx, int cy, int w, int h)
{
    if (cx < 0 || cy < 0 || cx + w > g->cw || cy + h > g->ch)
        return 0;
    for (int y = cy; y < cy + h; y++)
        for (int x = cx; x < cx + w; x++)
            if (g->used[y][x])
                return 0;
    return 1;
}

/* SpawnOutdoorLevelPresetEx: la pieza en la celda (cx, cy) */
static void kgrid_put(Map *m, int z, KGrid *g, int def, int variant, int cx, int cy)
{
    int w = 0, h = 0;
    char path[160];
    if (prest_file(def, variant, path, sizeof path, &w, &h) != 0)
        return;
    w = w > 0 ? w / CELL : 1;
    h = h > 0 ? h / CELL : 1;
    for (int y = cy; y < cy + h && y < g->ch; y++)
        for (int x = cx; x < cx + w && x < g->cw; x++)
            if (x >= 0 && y >= 0)
                g->used[y][x] = 1;
    prest_stamp(m, z, def, variant, g->x0 + cx * CELL, g->y0 + cy * CELL);
}

static void kgrid_size(int def, int *w, int *h)
{
    char path[160];
    *w = *h = 0;
    prest_file(def, 0, path, sizeof path, w, h);
    *w = *w > 0 ? *w / CELL : 1;
    *h = *h > 0 ? *h / CELL : 1;
}

/* DRLGOUTDOORS_SpawnOutdoorLevelPreset: en la primera celda del interior (en orden al azar) donde entra */
static void kgrid_somewhere(Map *m, int z, KGrid *g, int def, int variant)
{
    int area = (g->cw - 2) * (g->ch - 2), w, h;
    int cx[256], cy[256];
    kgrid_size(def, &w, &h);
    for (int i = 0; i < area && i < 256; i++) {
        cx[i] = i % (g->cw - 2);
        cy[i] = i / (g->cw - 2);
    }
    for (int i = 0; i < area; i++) {
        int a = (int)(rnd() % (uint32_t)area), b = (int)(rnd() % (uint32_t)area), tx = cx[a], ty = cy[a];
        cx[a] = cx[b];
        cy[a] = cy[b];
        cx[b] = tx;
        cy[b] = ty;
    }
    for (int i = 0; i < area; i++)
        if (kgrid_fits(g, cx[i] + 1, cy[i] + 1, w, h)) {
            kgrid_put(m, z, g, def, variant, cx[i] + 1, cy[i] + 1);
            return;
        }
}

/* DRLGOUTJUNG_SpawnRandomPreset: piezas de lo..hi en las celdas libres (en orden al azar), hasta `limit` (0 = todas) */
static void kgrid_fill(Map *m, int z, KGrid *g, int lo, int hi, int limit)
{
    int n = g->cw * g->ch, placed = 0;
    int cx[256], cy[256];
    for (int i = 0; i < n && i < 256; i++) {
        cx[i] = i % g->cw;
        cy[i] = i / g->cw;
    }
    for (int i = 0; i < n; i++) {
        int a = (int)(rnd() % (uint32_t)n), b = (int)(rnd() % (uint32_t)n), tx = cx[a], ty = cy[a];
        cx[a] = cx[b];
        cy[a] = cy[b];
        cx[b] = tx;
        cy[b] = ty;
    }
    for (int i = 0; i < n; i++) {
        int def = lo + (int)(rnd() % (uint32_t)(hi - lo + 1)), w, h;
        kgrid_size(def, &w, &h);
        if (!kgrid_fits(g, cx[i], cy[i], w, h))
            continue;
        kgrid_put(m, z, g, def, -1, cx[i], cy[i]);
        if (limit > 0 && ++placed >= limit)
            break;
    }
}

/* El borde de un barrio (DRLGOUTJUNG_BuildLowerKurast, ...BuildKurastBazaar,
   ...BuildUpperKurast): las puertas al norte y al sur donde corresponde */
static void kurast_borders(Map *m, int z, KGrid *g, int base, int gate_n, int gate_s, int wide_n, int wide_s)
{
    int nw = g->cw - 1, nh = g->ch - 1;
    for (int i = 1; i < nw; i += wide_n && i == gate_n ? 2 : 1)
        kgrid_put(m, z, g, (i == gate_n ? 8 : 0) + base, -1, i, 0); /* BORDER_N o GATE_N */
    for (int i = 1; i < nw; i += wide_s && i == gate_s ? 2 : 1)
        kgrid_put(m, z, g, (i == gate_s ? 8 : 0) + base + 1, -1, i, nh); /* BORDER_S o GATE_S */
    for (int i = 1; i < nh; i++) {
        kgrid_put(m, z, g, base + 2, -1, nw, i); /* E */
        kgrid_put(m, z, g, base + 3, -1, 0, i);  /* W */
    }
    kgrid_put(m, z, g, base + 5, -1, 0, 0);   /* NW */
    kgrid_put(m, z, g, base + 4, -1, nw, 0);  /* NE */
    kgrid_put(m, z, g, base + 7, -1, 0, nh);  /* SW */
    kgrid_put(m, z, g, base + 6, -1, nw, nh); /* SE */
}

/* Las puertas de cada barrio (columna de celda): la del norte y la del sur */
static void kurast_gates(int k, int *gate_n, int *gate_s)
{
    int nw = KURAST_W / CELL - 1;
    if (k == 0) { /* la Kurast baja: al norte del lado que diga bJungleInterlink; al sur, al medio */
        *gate_n = s_jungle_interlink ? 1 : nw - 1;
        *gate_s = nw / 2;
    } else if (k == 1) { /* el bazar */
        *gate_n = s_jungle_interlink ? KURAST_W / CELL - 2 : 1;
        *gate_s = s_jungle_interlink ? 1 : KURAST_W / CELL - 2;
    } else { /* la alta: al norte, al medio (16 de ancho) */
        *gate_n = nw / 2;
        *gate_s = s_jungle_interlink ? nw - 1 : 1;
    }
}

/* DRLGOUTPLACE_BuildKurast: la Kurast baja (Zone.floor 0), el bazar (1) o la alta (2) */
static void build_kurast(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    KGrid g;
    memset(&g, 0, sizeof g);
    g.x0 = zone->x0;
    g.y0 = zone->y0;
    g.cw = KURAST_W / CELL;
    g.ch = KURAST_H / CELL;
    int k = zone->floor, gn, gs;
    kurast_gates(k, &gn, &gs);
    if (k == 0) {
        kurast_borders(m, z, &g, LP_SLUMS_BORDER_N, gn, gs, 0, 1);
        kgrid_somewhere(m, z, &g, LP_BURBS_WAYPOINT, 0);
        kgrid_fill(m, z, &g, LP_SLUMS_16X16, LP_SLUMS_16X16, 4);
        kgrid_fill(m, z, &g, LP_SLUMS_08X16, LP_SLUMS_16X08, 0);
        kgrid_fill(m, z, &g, LP_SLUMS_08X08, LP_SLUMS_08X08, 0);
    } else if (k == 1) {
        kurast_borders(m, z, &g, LP_BURBS_BORDER_N, gn, gs, 0, 0);
        kgrid_put(m, z, &g, LP_BURBS_SEWER, 0, 3, 3);
        kgrid_put(m, z, &g, LP_BURBS_SEWER, 1, KURAST_W / CELL - 4, 3);
        kgrid_somewhere(m, z, &g, LP_BURBS_TEMPLE, 0);
        kgrid_somewhere(m, z, &g, LP_BURBS_TEMPLE, 1);
        kgrid_somewhere(m, z, &g, LP_BURBS_WAYPOINT, 0);
        kgrid_fill(m, z, &g, LP_BURBS_16X16, LP_BURBS_16X16, 4);
        kgrid_fill(m, z, &g, LP_BURBS_08X16, LP_BURBS_16X08, 0);
        kgrid_fill(m, z, &g, LP_BURBS_08X08, LP_BURBS_08X08, 0);
    } else {
        kurast_borders(m, z, &g, LP_METRO_BORDER_N, gn, gs, 1, 0);
        kgrid_put(m, z, &g, LP_METRO_SEWER, 0, 3, KURAST_H / CELL - 4);
        kgrid_put(m, z, &g, LP_METRO_SEWER, 1, KURAST_W / CELL - 4, KURAST_H / CELL - 4);
        kgrid_somewhere(m, z, &g, LP_METROTEMPLE, 0);
        kgrid_somewhere(m, z, &g, LP_METROTEMPLE, 1);
        kgrid_somewhere(m, z, &g, LP_BURBS_WAYPOINT, 0);
        kgrid_fill(m, z, &g, LP_METRO_16X16, LP_METRO_16X16, 4);
        kgrid_fill(m, z, &g, LP_METRO_08X16, LP_METRO_16X08, 0);
        kgrid_fill(m, z, &g, LP_METRO_08X08, LP_METRO_08X08, 0);
    }
}

static void build_travincal(Map *m, int z)
{
    static const struct { int def, cx, cy; } k_pieces[6] = {
        { LP_TRAV_NW, 0, 0 }, { LP_TRAV_N, 2, 0 }, { LP_TRAV_NE, 6, 0 },
        { LP_TRAV_SW, 0, 4 }, { LP_TRAV_S, 2, 4 }, { LP_TRAV_SE, 6, 4 },
    };
    const Zone *zone = &s_zones[z];
    for (int k = 0; k < 6; k++)
        prest_stamp(m, z, k_pieces[k].def, -1, zone->x0 + k_pieces[k].cx * CELL, zone->y0 + k_pieces[k].cy * CELL);
}

/* ---- Los calabozos ----
   Como DRLGMAZE_GenerateLevel: salas de un tamaño fijo (lvlmaze.txt) que
   arrancan en ronda (DRLGMAZE_InitBasicMazeLayout: 2x2, o 5x5 en las
   cloacas, con las cuatro bocas en las esquinas) y crecen al azar
   (DRLGMAZE_BuildBasicMaze); después, las salas especiales
   (DRLGMAZE_PlaceAct3DungeonStuff, ...SewerStuff, ...MephistoStuff y, en
   el Acto 5, ...Act5IceStuff y ...Act5BaalStuff): cada una con una sola
   salida, del lado que toque, en una sala que ya la tenga o en una nueva
   pegada. Cada sala es la pieza de lvlprest.txt de sus salidas (base + O 1,
   E 2, S 4, N 8; las temáticas, 15 más). Del Acto 5 también: el templo de
   Nihlathak (la ronda de 2x2 salas de 40x40, sin crecer:
   DRLGMAZE_PlaceAct5TempleStuff), la Fortaleza de la Piedra del Mundo (la
   sala de la subida con tres salidas en el medio:
   DRLGMAZE_PlaceAct2TombPrev_Act5BaalPrev) y los infiernos de los portales
   rojos (DRLGMAZE_PlaceAct5LavaPresets). */

enum { A3M_SPIDER, A3M_DUNGEON, A3M_SEWER, A3M_MEPH, A3M_ICE, A3M_TEMPLE, A3M_BAAL, A3M_LAVA };

static const struct {
    int         id, type;
    const char *name;
    int         room, rooms, ring, grid, groups;
} k_act3_mazes[] = {
    { 84, A3M_SPIDER, "Spider Cave", 16, 4, 2, 2, 8 },
    { 85, A3M_SPIDER, "Spider Cavern", 16, 4, 2, 2, 8 },
    { 86, A3M_DUNGEON, "Swampy Pit Level 1", 14, 4, 2, 4, 6 },
    { 87, A3M_DUNGEON, "Swampy Pit Level 2", 14, 4, 2, 4, 6 },
    { 88, A3M_DUNGEON, "Flayer Dungeon Level 1", 14, 4, 2, 4, 6 },
    { 89, A3M_DUNGEON, "Flayer Dungeon Level 2", 14, 4, 2, 4, 6 },
    { 92, A3M_SEWER, "#92", 12, 32, 5, 8, 28 }, /* (por el Id: las cloacas del Acto 2 se llaman igual) */
    { 100, A3M_MEPH, "Durance of Hate Level 1", 12, 10, 2, 6, 12 },
    { 101, A3M_MEPH, "Durance of Hate Level 2", 12, 12, 2, 7, 14 },
    /* las cuevas de hielo del Acto 5 (Crystalline Passage, Glacial Trail y Ancients' Way) */
    { 113, A3M_ICE, "Crystalized Cavern Level 1", 16, 10, 2, 7, 14 },
    { 115, A3M_ICE, "Crystalized Cavern Level 2", 16, 10, 2, 7, 14 },
    { 118, A3M_ICE, "Glacial Caves Level 1", 16, 15, 2, 8, 20 },
    /* el templo de Nihlathak (Halls of Anguish y of Pain), los infiernos y la Fortaleza */
    { 122, A3M_TEMPLE, "Halls of Anguish", 40, 4, 2, 2, 12 },
    { 123, A3M_TEMPLE, "Halls of Death's Calling", 40, 4, 2, 2, 12 },
    { 125, A3M_LAVA, "Hell1", 24, 3, 0, 5, 12 },
    { 126, A3M_LAVA, "Hell2", 24, 3, 0, 5, 12 },
    { 127, A3M_LAVA, "Hell3", 24, 3, 0, 5, 12 },
    { 128, A3M_BAAL, "The Worldstone Keep Level 1", 16, 9, 0, 6, 14 },
    { 129, A3M_BAAL, "The Worldstone Keep Level 2", 16, 9, 0, 6, 14 },
    { 130, A3M_BAAL, "The Worldstone Keep Level 3", 16, 9, 0, 6, 14 },
};

/* Los de una sola pieza (DrlgType 2 en levels.txt): su Def y el tamaño del DS1 */
static const struct {
    int         id, def;
    const char *name;
    int         w, h, groups;
} k_act3_presets[] = {
    { 90, 703, "Swampy Pit Level 3", 41, 41, 8 },
    { 91, 704, "Flayer Dungeon Level 3", 41, 41, 8 },
    { 93, 747, "#93", 19, 23, 3 },
    { 94, 748, "Ruined Temple", 25, 25, 3 },
    { 95, 749, "Disused Fane", 25, 25, 3 },
    { 96, 750, "Forgotten Reliquary", 25, 25, 3 },
    { 97, 751, "Forgotten Temple", 25, 25, 3 },
    { 98, 752, "Ruined Fane", 25, 25, 3 },
    { 99, 753, "Disused Reliquary", 25, 25, 3 },
    { 102, 796, "Durance of Hate Level 3", 42, 30, 4 },
    /* los del Acto 5: el Frozen River (River A o B, como el original), el
       Drifter Cavern, el Icy Cellar, la cumbre del Arreat y la entrada del
       templo de Nihlathak (sin monstruos al azar: MonDen 0) */
    { 114, 1038, "Cellar of Pity", 65, 65, 12 },
    { 116, 1040, "Echo Chamber", 33, 33, 5 },
    { 119, 1041, "Glacial Caves Level 2", 33, 33, 5 },
    { 120, 1089, "Rocky Summit", 21, 29, 0 },
    { 121, 1088, "Nihlathaks Temple", 23, 28, 0 },
    /* los Halls of Vaught (uno de los cuatro NihlE/N/S/W), el Trono y la Cámara */
    { 124, 864, "Halls of Vaught", 85, 85, 12 },
    { 131, 1086, "Throne of Destruction", 41, 53, 6 },
    { 132, 1087, "The Worldstone Chamber", 56, 56, 0 },
};

/* Los calabozos de un acto (3 o 5) en las filas de abajo: sus zonas y niveles */
static void a3_add_dungeons(int act)
{
    for (int k = 0; k < (int)COUNT(k_act3_mazes); k++) {
        if ((k_act3_mazes[k].id > 102 ? 5 : 3) != act)
            continue;
        int x, y, w = k_act3_mazes[k].grid * k_act3_mazes[k].room + 1;
        pack(w, w, &x, &y);
        int z = add_zone(ZK_A3MAZE, k_act3_mazes[k].id, x, y, x + w, y + w);
        add_level(k_act3_mazes[k].name, z, x, y, x + w, y + w, k_act3_mazes[k].groups, 1);
    }
    for (int k = 0; k < (int)COUNT(k_act3_presets); k++) {
        if ((k_act3_presets[k].id > 102 ? 5 : 3) != act)
            continue;
        int x, y, w = k_act3_presets[k].w, h = k_act3_presets[k].h;
        pack(w, h, &x, &y);
        int z = add_zone(ZK_A3PRESET, k_act3_presets[k].id, x, y, x + w, y + h);
        add_level(k_act3_presets[k].name, z, x, y, x + w, y + h, k_act3_presets[k].groups, 1);
    }
}

static int act3_preset(int id)
{
    for (int k = 0; k < (int)COUNT(k_act3_presets); k++)
        if (k_act3_presets[k].id == id)
            return k;
    return 0;
}

/* Las salidas de una sala de la grilla, en las letras de lvlprest.txt */
static int a3_flags(int ex)
{
    return (ex & EX_W ? JF_W : 0) | (ex & EX_E ? JF_E : 0) | (ex & EX_S ? JF_S : 0) | (ex & EX_N ? JF_N : 0);
}

/* Una sala especial con una sola salida, hacia k_dirs[d]: una que ya la
   tenga, o una nueva pegada a otra (la primera en orden que tenga lugar) */
static int a3_special(Maze *mz, uint8_t *fixed, int d)
{
    for (int i = 0; i < mz->count; i++)
        if (!fixed[i] && mz->exits[mz->cy[i]][mz->cx[i]] == k_dirs[d])
            return fixed[i] = 1, i;
    for (int i = 0; i < mz->count; i++) {
        int r = fixed[i] ? -1 : maze_add(mz, i, d ^ 1);
        if (r >= 0)
            return fixed[r] = 1, r;
    }
    return -1;
}

/* Un infierno de los portales rojos (DRLGMAZE_PlaceAct5LavaPresets): la sala
   del medio (NS o EW), dos puntas pegadas (N y S, o E y O, con el archivo
   que dice la tabla del original) y la Lava X del Acto 4 alrededor de las
   tres (DRLGMAZE_FillBlankMazeSpaces) */
static void build_lava(Map *m, int z, int room, int g)
{
    static const struct { int dx, dy, def, pick; } k_sets[4][2] = {
        { { 0, -1, 1054, 0 }, { 0, 1, 1053, 1 } }, { { 0, -1, 1054, 1 }, { 0, 1, 1053, 0 } },
        { { -1, 0, 1055, 1 }, { 1, 0, 1056, 0 } }, { { -1, 0, 1055, 0 }, { 1, 0, 1056, 1 } },
    };
    const Zone *zone = &s_zones[z];
    int set = (int)(rnd() & 3u), c = g / 2, rx[3] = { c, c + k_sets[set][0].dx, c + k_sets[set][1].dx };
    int ry[3] = { c, c + k_sets[set][0].dy, c + k_sets[set][1].dy };
    uint8_t used[MAZE_MAX][MAZE_MAX] = { { 0 } };
    for (int i = 0; i < 3; i++) {
        int def = i == 0 ? (set < 2 ? 1058 : 1057) : k_sets[set][i - 1].def, pick = i == 0 ? -1 : k_sets[set][i - 1].pick;
        prest_stamp(m, z, def, pick, zone->x0 + rx[i] * room, zone->y0 + ry[i] * room);
        room_add(zone->x0 + rx[i] * room, zone->y0 + ry[i] * room, room, room);
        used[ry[i]][rx[i]] = 1;
    }
    for (int i = 0; i < 3; i++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = rx[i] + dx, y = ry[i] + dy;
                if (x < 0 || y < 0 || x >= g || y >= g || used[y][x])
                    continue;
                used[y][x] = 1;
                prest_stamp(m, z, 836, 0, zone->x0 + x * room, zone->y0 + y * room);
            }
}

/* La grilla de un calabozo: la ronda (o la sala del medio de la
   Fortaleza), las salas al azar y las especiales, con su pieza en def.
   Devuelve cuántas especiales no entraron */
static int a3_layout(Maze *mz, uint8_t *fixed, int *def, int k, const Zone *zone)
{
    /* el orden de las tablas del original (N, E, S, O) en k_dirs */
    static const int k_od[4] = { 0, 2, 1, 3 };
    int type = k_act3_mazes[k].type, ring = k_act3_mazes[k].ring, g = k_act3_mazes[k].grid, missing = 0;
    memset(mz, 0, sizeof *mz);
    memset(mz->room, -1, sizeof mz->room);
    memset(fixed, 0, MAZE_ROOMS);
    memset(def, 0, MAZE_ROOMS * sizeof *def);
    mz->gw = mz->gh = g;
    if (type == A3M_BAAL) {
        /* la sala de la subida en el medio, con tres salidas (falta la de
           después de las tres al azar, en el orden O, N, E, S del original) */
        static const int k_orig[4] = { 3, 0, 2, 1 };              /* O, N, E, S en k_dirs */
        static const int k_prev[4] = { 1075, 1077, 1076, 1074 }; /* sin O: NSE; sin N: SEW; sin E: NSW; sin S: NEW */
        int start = (int)(rnd() % 4u);
        mz->cx[0] = mz->cy[0] = g / 2;
        mz->room[g / 2][g / 2] = 0;
        mz->count = 1;
        for (int i = 0; i < 3; i++)
            maze_add(mz, 0, k_orig[(start + i) % 4]);
        def[0] = k_prev[(start + 3) % 4];
        fixed[0] = 1;
    } else {
        /* la ronda: desde la esquina de abajo a la derecha, al norte, al oeste, al sur y al este */
        int ox = (g - ring) / 2, oy = (g - ring) / 2, last = 0;
        mz->cx[0] = ox + ring - 1;
        mz->cy[0] = oy + ring - 1;
        mz->room[mz->cy[0]][mz->cx[0]] = 0;
        mz->count = 1;
        static const int k_walk[4] = { 0, 3, 1, 2 };
        for (int side = 0; side < 4; side++)
            for (int i = 0; i < ring - 1 - (side == 3); i++)
                last = maze_add(mz, last, k_walk[side]);
        mz->exits[mz->cy[last]][mz->cx[last]] |= EX_E;
        mz->exits[mz->cy[0]][mz->cx[0]] |= EX_W;
    }
    if (type == A3M_TEMPLE) {
        /* la bajada en la NE, la NW o la SW (al azar) y, en los Halls of
           Death's Calling, el waypoint en la que sigue (NW, SW o la SE de la subida) */
        static const int k_tf[4] = { JF_N | JF_E, JF_N | JF_W, JF_S | JF_W, JF_S | JF_E };
        static const int k_tdown[3] = { 1046, 1047, 1048 }, k_twp[4] = { 1049, 1050, 1052, 1051 };
        int start = (int)(rnd() % 3u);
        for (int i = 0; i < mz->count; i++) {
            int f = a3_flags(mz->exits[mz->cy[i]][mz->cx[i]]);
            if (f == k_tf[start])
                def[i] = k_tdown[start];
            else if (zone->floor == 123 && f == k_tf[start + 1])
                def[i] = k_twp[start + 1];
            fixed[i] = def[i] != 0;
        }
    }
    if (type == A3M_SEWER) /* las cuatro bocas en las esquinas de la ronda */
        for (int i = 0; i < mz->count; i++) {
            int f = a3_flags(mz->exits[mz->cy[i]][mz->cx[i]]);
            def[i] = f == (JF_S | JF_W) ? 735 : f == (JF_S | JF_E) ? 736 : f == (JF_N | JF_W) ? 737 : f == (JF_N | JF_E) ? 738 : 0;
            fixed[i] = def[i] != 0;
        }
    for (int tries = 0; type != A3M_SPIDER && mz->count < k_act3_mazes[k].rooms && tries < 4000; tries++) {
        int from = (int)(rnd() % (uint32_t)mz->count), d = (int)(rnd() % 4u);
        if (!fixed[from])
            maze_add(mz, from, d);
    }
    /* las especiales: la dirección de la primera es al azar y las demás siguen la ronda N, E, S, O */
    int dir = (int)(rnd() % 4u), sp[5], nsp = 0;
    if (type == A3M_DUNGEON) { /* la subida y la bajada */
        sp[nsp++] = 695;
        sp[nsp++] = 699;
    } else if (type == A3M_SEWER) { /* el desagüe (la bajada) y el cofre */
        sp[nsp++] = 739;
        sp[nsp++] = 743;
    } else if (type == A3M_MEPH) { /* la subida, el waypoint (en el 2) y la bajada */
        sp[nsp++] = 784;
        if (zone->floor == 101)
            sp[nsp++] = 792;
        sp[nsp++] = 788;
    } else if (type == A3M_ICE) { /* la subida, la siguiente, la bajada; el tema (en el Glacial Trail) y el waypoint */
        sp[nsp++] = 1018;
        sp[nsp++] = 1022;
        sp[nsp++] = 1026;
        if (zone->floor == 115)
            sp[nsp++] = 1030;
        sp[nsp++] = 1034;
    } else if (type == A3M_BAAL) { /* la bajada y, en el nivel 2, el waypoint */
        sp[nsp++] = 1078;
        if (zone->floor == 129)
            sp[nsp++] = 1082;
    }
    for (int s = 0; s < nsp; s++) {
        int od = (dir + s) % 4, r = a3_special(mz, fixed, k_od[od]);
        /* el orden de las piezas especiales en lvlprest.txt: O, E, S, N
           (los waypoints de hielo y las de la Fortaleza: N, S, E, O) */
        static const int k_slot[4] = { 3, 1, 2, 0 }, k_nsew[4] = { 0, 2, 1, 3 };
        if (r >= 0)
            def[r] = sp[s] + (sp[s] == 1034 || sp[s] == 1078 || sp[s] == 1082 ? k_nsew[od] : k_slot[od]);
        else
            missing++;
    }
    return missing;
}

static void build_act3_maze(Map *m, int z)
{
    const Zone *zone = &s_zones[z];
    int k = 0;
    while (k < (int)COUNT(k_act3_mazes) - 1 && k_act3_mazes[k].id != zone->floor)
        k++;
    int type = k_act3_mazes[k].type;
    if (type == A3M_LAVA) {
        build_lava(m, z, k_act3_mazes[k].room, k_act3_mazes[k].grid);
        return;
    }
    /* Si una especial no entra (las salas libres tienen ocupado ese lado),
       el original (DRLGMAZE_AddSpecialPreset) no la pone y el nivel queda
       sin su waypoint o su salida: con la semilla 29, el Glacial Trail sin
       waypoint. Se arma de nuevo con los números al azar que siguen, como
       maze_build (la primera vuelta es la de siempre) */
    Maze mz;
    uint8_t fixed[MAZE_ROOMS], theme[MAZE_ROOMS] = { 0 };
    int def[MAZE_ROOMS], missing = 0;
    for (int attempt = 0; attempt < 60 && (attempt == 0 || missing); attempt++)
        missing = a3_layout(&mz, fixed, def, k, zone);
    if (missing)
        fprintf(stderr, "%s: no entraron %d salas especiales\n", zone->name, missing);
    /* las temáticas: un quinto de las salas, por lo menos dos (DRLGMAZE_RollAct_1_2_3_BasicPresets; en el Acto 5 no hay) */
    for (int n = type == A3M_SPIDER || type >= A3M_ICE ? 0 : (mz.count / 5 + 1 > 2 ? mz.count / 5 + 1 : 2), tries = 0;
         n > 0 && tries < 40; tries++) {
        int i = (int)(rnd() % (uint32_t)mz.count);
        if (!fixed[i] && !theme[i])
            theme[i] = 1, n--;
    }
    for (int i = 0; i < mz.count; i++) {
        int f = a3_flags(mz.exits[mz.cy[i]][mz.cx[i]]), d = def[i];
        if (!d && type == A3M_SPIDER) { /* las cuatro de la ronda; en cada cueva, una con el cofre */
            d = f == (JF_S | JF_W) ? 659 : f == (JF_S | JF_E) ? 660 : f == (JF_N | JF_W) ? 661 : 662;
            if (zone->floor == 84 && d == 662)
                d = 664;
            if (zone->floor == 85 && d == 661)
                d = 663;
        } else if (!d && type == A3M_TEMPLE) { /* NE, NW, SE (la de la subida) y SW */
            d = f == (JF_N | JF_E) ? 1042 : f == (JF_N | JF_W) ? 1043 : f == (JF_S | JF_E) ? 1044 : 1045;
        } else if (!d) {
            d = (type == A3M_DUNGEON ? 664 : type == A3M_SEWER ? 704 : type == A3M_ICE ? 1002 : type == A3M_BAAL ? 1058 : 753) +
                f + (theme[i] ? 15 : 0);
        }
        int room = k_act3_mazes[k].room;
        if (getenv("D2_MAZEDUMP"))
            printf("  %s sala %d (%d, %d) pieza %d salidas %d\n", zone->name, i, mz.cx[i], mz.cy[i], d, f);
        prest_stamp(m, z, d, -1, zone->x0 + mz.cx[i] * room, zone->y0 + mz.cy[i] * room);
        room_add(zone->x0 + mz.cx[i] * room, zone->y0 + mz.cy[i] * room, room, room);
    }
}

/* ---- El mundo del Acto 3 ---- */

/* lvlprest.txt y lvltypes.txt (las piezas de los Actos 3 y 4) */
static void prest_tables_load(void)
{
    if (s_lvlprest_txt.rows)
        return;
    size_t n;
    uint8_t *d = fs_read("data/global/excel/lvlprest.txt", &n);
    if (!d || txt_load(d, n, &s_lvlprest_txt) != 0)
        fprintf(stderr, "falta data/global/excel/lvlprest.txt\n");
    free(d);
    int col = txt_col(&s_lvlprest_txt, "Def");
    for (int r = s_lvlprest_txt.rows - 1; r >= 1; r--) { /* (de abajo para arriba: queda la primera, como txt_find_row) */
        const char *v = txt_get(&s_lvlprest_txt, r, col);
        int def = v[0] ? atoi(v) : -1;
        if (def >= 0 && def < PREST_DEFS)
            s_prest_row[def] = r;
    }
    d = fs_read("data/global/excel/lvltypes.txt", &n);
    if (!d || txt_load(d, n, &s_lvltypes_txt) != 0)
        fprintf(stderr, "falta data/global/excel/lvltypes.txt\n");
    free(d);
}

/* KillEdge y FillBlanks de cada archivo de lvlprest.txt (ninguno está en dos
   filas con distintos), por el hash de su nombre desde "tiles/" */
typedef struct {
    uint32_t hash;
    uint8_t  kill, fill;
} PrestFile;
static PrestFile *s_pfiles;
static int        s_npfiles;

static uint32_t pfile_hash(const char *p)
{
    uint32_t h = 2166136261u;
    for (; *p; p++)
        h = (h ^ (uint8_t)(*p == '\\' ? '/' : tolower((unsigned char)*p))) * 16777619u;
    return h;
}

static int pfile_cmp(const void *a, const void *b)
{
    uint32_t x = ((const PrestFile *)a)->hash, y = ((const PrestFile *)b)->hash;
    return x < y ? -1 : x > y;
}

/* map_set_stamp_rules: cómo se estampa cada pieza (DRLGPRESET_AddPresetRoomMapTiles).
   El piso en blanco es el 30.1 en el Santuario Arcano y el 30.0 en los demás
   (DRLGROOMTILE_LoadInitRoomTiles: nTileSequence = nivel == Santuario) */
static int stamp_rules(const char *ds1_path, int *kill, int *blank)
{
    prest_tables_load();
    const TxtTable *t = &s_lvlprest_txt;
    if (!s_pfiles && t->rows > 1 && (s_pfiles = malloc((size_t)(t->rows * 6) * sizeof *s_pfiles)) != NULL) {
        int ck = txt_col(t, "KillEdge"), cf = txt_col(t, "FillBlanks");
        for (int r = 1; r < t->rows; r++)
            for (int k = 0; k < 6; k++) {
                char col[8];
                snprintf(col, sizeof col, "File%d", k + 1);
                const char *f = txt_get(t, r, txt_col(t, col));
                if (f && f[0] && strcmp(f, "0") != 0)
                    s_pfiles[s_npfiles++] = (PrestFile){ pfile_hash(f), (uint8_t)(atoi(txt_get(t, r, ck)) != 0),
                                                         (uint8_t)(atoi(txt_get(t, r, cf)) != 0) };
            }
        qsort(s_pfiles, (size_t)s_npfiles, sizeof *s_pfiles, pfile_cmp);
    }
    const char *name = strstr(ds1_path, "tiles/");
    name = name ? name + 6 : ds1_path;
    PrestFile key = { pfile_hash(name), 0, 0 };
    const PrestFile *e = s_pfiles ? bsearch(&key, s_pfiles, (size_t)s_npfiles, sizeof *s_pfiles, pfile_cmp) : NULL;
    if (!e)
        return 0;
    *kill = e->kill;
    *blank = e->fill ? 1 + (strncmp(name, "act2/arcane/", 12) == 0) : 0;
    return 1;
}

static int layout_act3(Map *m)
{
    prest_tables_load();
    s_jungle_interlink = (int)((rnd() >> 5) & 1u);
    s_njlinks = 0;
    /* En coordenadas del original (y hacia el sur): el muelle en (0, 0), la
       selva arriba, Kurast arriba de la última selva (centrada) */
    if (jungle_generate(0, 0) != 0) {
        fprintf(stderr, "no pude armar la selva\n");
        return -1;
    }
    /* de sur a norte: el Spider Forest es la de más abajo */
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (s_jungle[b].y > s_jungle[a].y) {
                Jungle t = s_jungle[a];
                s_jungle[a] = s_jungle[b];
                s_jungle[b] = t;
                for (int l = 0; l < s_njlinks; l++) /* (las uniones apuntan a las selvas por su número) */
                    for (int e = 0; e < 2; e++)
                        s_jlinks[l][e] = s_jlinks[l][e] == a ? b : s_jlinks[l][e] == b ? a : s_jlinks[l][e];
            }
    const Jungle *fj = &s_jungle[2];
    int kx = fj->x + JUNGLE_W / 2 - KURAST_W / 2, ky = fj->y;
    int rect[9][4] = { { 0, 0, DOCK_W, DOCK_H } };
    for (int i = 0; i < 3; i++) {
        rect[1 + i][0] = s_jungle[i].x;
        rect[1 + i][1] = s_jungle[i].y;
        rect[1 + i][2] = JUNGLE_W;
        rect[1 + i][3] = JUNGLE_H;
    }
    for (int i = 0; i < 3; i++) {
        ky -= KURAST_H;
        rect[4 + i][0] = kx;
        rect[4 + i][1] = ky;
        rect[4 + i][2] = KURAST_W;
        rect[4 + i][3] = KURAST_H;
    }
    ky -= CAUSE_H;
    rect[7][0] = fj->x + JUNGLE_W / 2 - CAUSE_W / 2;
    rect[7][1] = ky;
    rect[7][2] = CAUSE_W;
    rect[7][3] = CAUSE_H;
    ky -= TRAV_H;
    rect[8][0] = fj->x + JUNGLE_W / 2 - TRAV_W / 2;
    rect[8][1] = ky;
    rect[8][2] = TRAV_W;
    rect[8][3] = TRAV_H;
    int minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (int i = 0; i < 9; i++) {
        minx = rect[i][0] < minx ? rect[i][0] : minx;
        miny = rect[i][1] < miny ? rect[i][1] : miny;
        maxx = rect[i][0] + rect[i][2] > maxx ? rect[i][0] + rect[i][2] : maxx;
        maxy = rect[i][1] + rect[i][3] > maxy ? rect[i][1] + rect[i][3] : maxy;
    }
    int ox = CELL - minx, oy = CELL - miny;
    for (int i = 0; i < 9; i++) {
        rect[i][0] += ox;
        rect[i][1] += oy;
    }
    for (int i = 0; i < 3; i++) {
        s_jungle[i].x += ox;
        s_jungle[i].y += oy;
    }
    z_town = add_zone(ZK_DOCK, 0, rect[0][0], rect[0][1], rect[0][0] + DOCK_W, rect[0][1] + DOCK_H);
    add_level("Kurast Docktown", z_town, rect[0][0], rect[0][1], rect[0][0] + DOCK_W - 1, rect[0][1] + DOCK_H - 1, 0, 0);
    static const char *const k_jungle_names[3] = { "Spider Forest", "Great Marsh", "Flayer Jungle" };
    for (int i = 0; i < 3; i++) {
        int x = rect[1 + i][0], y = rect[1 + i][1];
        z_jungle[i] = add_zone(ZK_JUNGLE, i, x, y, x + JUNGLE_W, y + JUNGLE_H);
        add_level(k_jungle_names[i], z_jungle[i], x, y, x + JUNGLE_W, y + JUNGLE_H, 40, 0);
    }
    static const char *const k_kurast_names[3] = { "Lower Kurast", "Kurast Bazaar", "Upper Kurast" };
    for (int i = 0; i < 3; i++) {
        int x = rect[4 + i][0], y = rect[4 + i][1];
        z_kurast[i] = add_zone(ZK_KURAST, i, x, y, x + KURAST_W, y + KURAST_H);
        add_level(k_kurast_names[i], z_kurast[i], x, y, x + KURAST_W, y + KURAST_H, 20, 0);
    }
    z_causeway = add_zone(ZK_CAUSEWAY, 0, rect[7][0], rect[7][1], rect[7][0] + CAUSE_W, rect[7][1] + CAUSE_H);
    add_level("Kurast Causeway", z_causeway, rect[7][0], rect[7][1], rect[7][0] + CAUSE_W, rect[7][1] + CAUSE_H, 6, 0);
    z_travincal = add_zone(ZK_TRAVINCAL, 0, rect[8][0], rect[8][1], rect[8][0] + TRAV_W, rect[8][1] + TRAV_H);
    add_level("Travincal", z_travincal, rect[8][0], rect[8][1], rect[8][0] + TRAV_W, rect[8][1] + TRAV_H, 16, 0);
    /* Los calabozos, en filas debajo */
    s_pack_x = 0;
    s_pack_y = maxy - miny + 2 * CELL + PACK_GAP;
    s_pack_h = 0;
    a3_add_dungeons(3);
    for (int i = 0; i < s_num_levels; i++) /* los saltos de las Vis de levels.txt (para buscar caminos) */
        for (int k = 0; k < 8; k++)
            add_jump(s_levels[i].zone, zone_of_level_id(s_levels[i].vis[k]));
    int width = maxx - minx + 2 * CELL > PACK_W ? maxx - minx + 2 * CELL : PACK_W, height = s_pack_y + s_pack_h;
    if (map_create(m, width, height) != 0)
        return -1;
    /* Los pasos caminando: el muelle y la entrada de la selva, las uniones
       entre selvas, la última selva y la Kurast baja, y las puertas de
       Kurast hasta Travincal (en subtiles) */
    add_link(z_town, z_jungle[0], (float)((rect[0][0] + DOCK_W / 2) * TILE_SUB), (float)(rect[0][1] * TILE_SUB));
    for (int l = 0; l < s_njlinks; l++)
        add_link(z_jungle[s_jlinks[l][0]], z_jungle[s_jlinks[l][1]], (float)((s_jlink_xy[l][0] + ox) * TILE_SUB),
                 (float)((s_jlink_xy[l][1] + oy) * TILE_SUB));
    for (int i = 0; i < 3; i++) {
        int gn, gs;
        kurast_gates(i, &gn, &gs);
        const int *r = rect[4 + i];
        if (i == 0)
            add_link(z_jungle[2], z_kurast[0], (float)((r[0] + gs * CELL + CELL) * TILE_SUB), (float)((r[1] + KURAST_H) * TILE_SUB));
        int to = i < 2 ? z_kurast[i + 1] : z_causeway;
        add_link(z_kurast[i], to, (float)((r[0] + gn * CELL + (i == 2 ? CELL : CELL / 2)) * TILE_SUB), (float)(r[1] * TILE_SUB));
    }
    add_link(z_causeway, z_travincal, (float)((rect[7][0] + CAUSE_W / 2) * TILE_SUB), (float)(rect[7][1] * TILE_SUB));
    if (getenv("D2_DEBUG")) {
        printf("mundo del Acto 3 de %dx%d tiles (interlink %d), %d zonas\n", width, height, s_jungle_interlink, s_num_zones);
        for (int z = 0; z < s_num_zones; z++)
            printf("  zona %2d: %-18s (%d, %d)-(%d, %d)\n", z, s_zones[z].name, s_zones[z].x0, s_zones[z].y0, s_zones[z].x1,
                   s_zones[z].y1);
        for (int i = 0; i < 3; i++) {
            printf("  %s (dir %d, base %d):", k_jungle_names[i], s_jungle[i].dir, s_jungle[i].base);
            for (int k = 0; k < 12; k++)
                printf(" %d", s_jungle[i].defs[k]);
            printf(" (%d claros)\n", s_jungle[i].nclear);
        }
        for (int l = 0; l < s_njlinks; l++)
            printf("  unión %s - %s\n", k_jungle_names[s_jlinks[l][0]], k_jungle_names[s_jlinks[l][1]]);
    }
    return 0;
}
