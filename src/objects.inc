/* ---- Los objetos que se usan: cofres, ataúdes, urnas, barriles,
   cadáveres, estantes, bibliotecas, santuarios y pozos ----
   (incluido en game.c, después de a3quest.inc)

   Como el original (D2MOO, D2Game/src/OBJECTS): el InitFn de objects.txt
   tira al crearlos la trampa, la cerradura, el santuario o las cargas del
   pozo (OBJECTS_InitFunction*); el OperateFn dice qué pasa al usarlos con A
   (ObjMode.cpp: OBJECTS_OperateFunction*), y en cada sala salen los de
   ObjGrp0..7 / ObjPrb0..7 de levels.txt con su PopulateFn y la densidad de
   objgroup.txt (OBJECTS_PopulationHandler). Lo que tira cada objeto sale de
   la semilla de la partida y de su lugar, y lo que ya se usó se recuerda en
   la partida: al volver a armar una zona salen los mismos, igual de abiertos.

   shortcut: sin los temas de sala (Themes de levels.txt: salas llenas de
   barriles o de estantes) ni el Santuario de Oirautnas (no hay nombre que dar
   vuelta: sale el de gema, como el original remapea el 16); los santuarios
   explosivo y de veneno no dejan pociones para arrojar (el port no las tiene:
   solo hacen su daño); el monstruoso cambia al monstruo por un jefe al azar
   con escolta; las bibliotecas dan portal donde el original da identificar
   (no hay identificar); el rayo de los cofres hace el daño de la nova de las
   trampas. Cuando haga falta, sumarlos acá. */

enum {
    OP_CASKET = 1, OP_SHRINE = 2, OP_URN = 3, OP_CHEST = 4, OP_BARREL = 5, OP_XBARREL = 7, OP_CORPSE = 14,
    OP_ARMOR = 19, OP_WEAPON = 20, OP_WELL = 22, OP_BOOKS = 26, OP_TELEPAD = 27, OP_XCHEST = 30, OP_BANK = 32, OP_WIRT = 33,
    OP_SOUL = 48, OP_STASH = 51,
};

#define OBJ_TRAP_MASK  0x7f   /* InteractType: la trampa (1..9) */
#define OBJ_LOCKED     0x80   /* InteractType: cerrado, pide una llave */
#define OBJ_SPARK      0x100  /* cofre brillante (InitFn 57): mágico seguro */
#define OBJ_USED       0x200  /* ya se usó */
#define OBJ_HEAL       0x400  /* santuario que tiene que salir de vida (el que falta en el nivel) */
#define OBJ_SOUL_BURNT 0x800  /* el JackInTheBox que se prendió (se ve quemado) */
#define OBJ_REACH      2.2f   /* se usa desde esta distancia al borde (subtiles): los que quedan contra la pared */
#define OBJ_TRAP_TICKS 35     /* la trampa salta a los 35 cuadros (D2GAME_SetTrapCallback) */
#define OBJ_MINUTE     1200   /* los minutos de shrines.txt, en cuadros (OperateFunction02_Shrine) */
/* COLLIDE_MASK_PLACEMENT (y el vacío, que en el original no es sala) */
#define OBJ_MASK_PLACE (COLL_WALL | COLL_ITEM | COLL_OBJECT | COLL_DOOR | COLL_NO_PATH | COLL_PET | COLL_PRESET | \
                        COLL_MONSTER | COLL_BLANK)

/* Lo que se usa de cada fila de objects.txt */
typedef struct {
    int16_t op, init, pop, sx, sy, sel, mode1, mode2, cyc1, coll0, coll1, lock, pre, dmg, xspace, yspace, orient, sub;
    int     parm[5];
    char    cls[32], name[24];
} ObjRow;
#define OBJ_ROWS 700
static ObjRow s_orow[OBJ_ROWS];
static int    s_orows;

/* shrines.txt */
typedef struct {
    int code, arg0, arg1, frames, reset, cls, lvl_min;
} ObjShrine;
#define OBJ_SHRINES 32
static ObjShrine s_shr[OBJ_SHRINES];
static int       s_nshr;
static TxtTable  s_objgroup;
static int       s_obj_loaded;

/* Lo que ya se usó en la partida (por acto, clase y lugar) */
typedef struct {
    uint16_t row, state;
    uint8_t  act, used;
    int16_t  x2, y2;
    uint32_t timer; /* santuario: cuándo vuelve a andar; pozo: cuándo recupera una carga (0 = nada) */
} ObjMem;
#define OBJ_MEM 6000
static ObjMem s_omem[OBJ_MEM];
static int    s_nomem;

/* Lo que pasa un rato después: la trampa, el fuego que dejó, el alijo de la selva */
enum { OE_NONE, OE_TRAP, OE_FIRE, OE_STASH, OE_SOUL, OE_WIRT };
typedef struct {
    int   kind, ticks, life, arg, player, unit, row, level;
    float x, y;
} ObjEvent;
#define OBJ_EVENTS 32
static ObjEvent s_oev[OBJ_EVENTS];

static int obj_txt(const char *path, TxtTable *t)
{
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int ok = d && txt_load(d, n, t) == 0;
    free(d);
    return ok;
}

static void obj_load(void)
{
    if (s_obj_loaded)
        return;
    s_obj_loaded = 1;
    for (int r = 1; r < OBJ_ROWS; r++) {
        const char *cls = units_objects_str(r, "Class");
        if (!cls[0])
            break;
        ObjRow *o = &s_orow[r];
        snprintf(o->cls, sizeof o->cls, "%s", cls);
        snprintf(o->name, sizeof o->name, "%s", units_objects_str(r, "Name"));
        o->op = (int16_t)units_objects_int(r, "OperateFn", 0);
        o->init = (int16_t)units_objects_int(r, "InitFn", 0);
        o->pop = (int16_t)units_objects_int(r, "PopulateFn", 0);
        o->sx = (int16_t)units_objects_int(r, "SizeX", 1);
        o->sy = (int16_t)units_objects_int(r, "SizeY", 1);
        o->sel = (int16_t)units_objects_int(r, "Selectable0", 0);
        o->mode1 = (int16_t)units_objects_int(r, "Mode1", 0);
        o->mode2 = (int16_t)units_objects_int(r, "Mode2", 0);
        o->cyc1 = (int16_t)units_objects_int(r, "CycleAnim1", 0);
        o->coll0 = (int16_t)units_objects_int(r, "HasCollision0", 0);
        o->coll1 = (int16_t)units_objects_int(r, "HasCollision1", 0);
        o->lock = (int16_t)units_objects_int(r, "Lockable", 0);
        o->pre = (int16_t)units_objects_int(r, "PreOperate", 0);
        o->dmg = (int16_t)units_objects_int(r, "Damage", 100);
        o->xspace = (int16_t)units_objects_int(r, "Xspace", 1);
        o->yspace = (int16_t)units_objects_int(r, "Yspace", 1);
        o->orient = (int16_t)units_objects_int(r, "Orientation", 0);
        o->sub = (int16_t)units_objects_int(r, "SubClass", 0);
        for (int k = 0; k < 5; k++) {
            char col[8];
            snprintf(col, sizeof col, "Parm%d", k);
            o->parm[k] = units_objects_int(r, col, 0);
        }
        s_orows = r + 1;
    }
    TxtTable t = { 0 };
    if (obj_txt("data/global/excel/shrines.txt", &t)) {
        for (int r = 1; r < t.rows && s_nshr < OBJ_SHRINES; r++, s_nshr++) {
            ObjShrine *s = &s_shr[s_nshr];
            s->code = txt_get_int(&t, r, "Code", 0);
            s->arg0 = txt_get_int(&t, r, "Arg0", 0);
            s->arg1 = txt_get_int(&t, r, "Arg1", 0);
            s->frames = txt_get_int(&t, r, "Duration in frames", 0);
            s->reset = txt_get_int(&t, r, "reset time in minutes", 0);
            s->cls = txt_get_int(&t, r, "effectclass", 0);
            s->lvl_min = txt_get_int(&t, r, "LevelMin", 0);
        }
        txt_free(&t);
    }
    if (!obj_txt("data/global/excel/objgroup.txt", &s_objgroup))
        fprintf(stderr, "falta objgroup.txt: no salen cofres ni barriles al azar\n");
}

static const ObjRow *obj_row(int u)
{
    int r = units_object_row(u);
    return r > 0 && r < s_orows ? &s_orow[r] : NULL;
}

/* ¿Es de los que se usan acá? (no las puertas, los waypoints ni los de las misiones) */
static int obj_supported(const ObjRow *o)
{
    switch (o ? o->op : 0) {
    case OP_CASKET: case OP_SHRINE: case OP_URN: case OP_CHEST: case OP_BARREL: case OP_XBARREL: case OP_CORPSE:
    case OP_ARMOR: case OP_WEAPON: case OP_WELL: case OP_BOOKS: case OP_XCHEST: case OP_STASH: case OP_BANK: case OP_SOUL:
    case OP_WIRT: case OP_TELEPAD:
        return o->sel;
    default:
        return 0;
    }
}

/* ¿Se puede usar ahora? Devuelve su OperateFn (0 = no) */
static int obj_usable(int u)
{
    const ObjRow *o = obj_row(u);
    if (!obj_supported(o))
        return 0;
    int st = units_object_state(u);
    if (o->op == OP_WELL)
        return (st & 0xff) > 0 ? o->op : 0;
    if (o->op == OP_BANK) /* el cofre del pueblo: siempre (los bots no lo abren: no sabrían cerrarlo) */
        return bots_playing() ? 0 : o->op;
    if (o->op == OP_TELEPAD) /* la almohadilla: siempre */
        return o->op;
    return !(st & OBJ_USED) && units_object_cur_mode(u) == 0 ? o->op : 0;
}

/* El cuadrado de la distancia de (x, y) al borde de lo que ocupa */
static float obj_dist2(int u, float x, float y)
{
    const ObjRow *o = obj_row(u);
    float ux, uy;
    units_position(u, 1.0f, &ux, &uy);
    int sx = o && o->sx > 0 ? o->sx : 1, sy = o && o->sy > 0 ? o->sy : 1;
    float x0 = (float)((int)ux - sx / 2), y0 = (float)((int)uy - sy / 2);
    float dx = x < x0 ? x0 - x : x > x0 + sx ? x - x0 - sx : 0.0f;
    float dy = y < y0 ? y0 - y : y > y0 + sy ? y - y0 - sy : 0.0f;
    return dx * dx + dy * dy;
}

/* El que se usaría desde (x, y): el más cercano al alcance, -1 = ninguno */
static int obj_near(float x, float y, float reach)
{
    int best = -1;
    float best_d = reach * reach;
    for (int u = 0; u < units_count(); u++) {
        if (!obj_usable(u))
            continue;
        float d = obj_dist2(u, x, y);
        if (d <= best_d) {
            best_d = d;
            best = u;
        }
    }
    return best;
}

/* ---- Las almohadillas del Santuario Arcano (TeleportPad1..4) ----
   Unen los tramos de las piezas de la variante 2 (sanctnse2...), que no se
   tocan caminando. Como OBJECTS_OperateFunction27_TeleportPad: lleva al
   lado de la otra de su clase en la misma sala (o, si no hay, en una de al
   lado), al lugar libre más cerca de ella. */

#define OBJ_PAD_ROOM 12 /* las salas: piezas de 12x12 tiles (lvlmaze.txt), desde la esquina de la zona */

static void obj_pad_room(int z, float x, float y, int *rx, int *ry)
{
    int x0, y0, x1, y1;
    world_zone_rect(z, &x0, &y0, &x1, &y1);
    *rx = ((int)floorf(x / TILE_SUB) - x0) / OBJ_PAD_ROOM;
    *ry = ((int)floorf(y / TILE_SUB) - y0) / OBJ_PAD_ROOM;
}

/* La pareja de la almohadilla u entre las unidades `list` (NULL: todas); -1 = ninguna */
static int obj_pad_partner(int u, const int *list, int n)
{
    int row = units_object_row(u), z = units_zone(u), rx, ry, best = -1, best_d = 2;
    float x, y;
    units_position(u, 1.0f, &x, &y);
    obj_pad_room(z, x, y, &rx, &ry);
    for (int k = 0; k < (list ? n : units_count()) && best_d > 0; k++) {
        int v = list ? list[k] : k, vx, vy;
        if (v == u || units_object_row(v) != row || units_zone(v) != z)
            continue;
        units_position(v, 1.0f, &x, &y);
        obj_pad_room(z, x, y, &vx, &vy);
        int d = abs(vx - rx) > abs(vy - ry) ? abs(vx - rx) : abs(vy - ry);
        if (d < best_d) {
            best_d = d;
            best = v;
        }
    }
    return best;
}

/* Adónde lleva la pareja v (COLLISION_GetFreeCoordinates a su lado); 0 = a ningún lado */
static int obj_pad_landing(int v, float *x, float *y)
{
    if (v < 0)
        return 0;
    units_position(v, 1.0f, x, y);
    find_free_spot(x, y);
    return 1;
}

/* Para los bots y map_reach: las almohadillas que hay en el rectángulo (en
   subtiles), lo que ocupa cada una y adónde lleva. Solo hay en el Santuario:
   si no está armado o no toca el rectángulo, ninguna (sin mirar las unidades,
   que en la Pi cuesta) */
static int obj_pad_jumps(int x0, int y0, int x1, int y1, BotJump *out, int max)
{
    int z = world_level_zone(74), zx0, zy0, zx1, zy1;
    if (z < 0 || !world_zone_loaded(z))
        return 0;
    world_zone_rect(z, &zx0, &zy0, &zx1, &zy1);
    if (zx1 * TILE_SUB <= x0 || zy1 * TILE_SUB <= y0 || zx0 * TILE_SUB >= x1 || zy0 * TILE_SUB >= y1)
        return 0;
    static int pads[256];
    int np = 0, n = 0;
    for (int u = 0; u < units_count() && np < 256; u++) {
        const ObjRow *o = obj_row(u);
        if (o && o->op == OP_TELEPAD)
            pads[np++] = u;
    }
    for (int k = 0; k < np && n < max; k++) {
        float x, y;
        units_position(pads[k], 1.0f, &x, &y);
        if (x < x0 || y < y0 || x >= x1 || y >= y1)
            continue;
        BotJump *j = &out[n];
        j->x0 = floorf(x), j->y0 = floorf(y), j->x1 = j->x0 + 1.0f, j->y1 = j->y0 + 1.0f; /* (como obj_dist2) */
        j->reach = OBJ_REACH;
        n += obj_pad_landing(obj_pad_partner(pads[k], pads, np), &j->lx, &j->ly);
    }
    return n;
}

/* ---- Las tiradas de cada objeto: de la semilla de la partida y su lugar ---- */

static uint32_t obj_seed(int row, float x, float y, uint32_t salt)
{
    uint32_t h = s_world_seed * 0x9E3779B9u ^ (uint32_t)s_act * 0x85EBCA6Bu ^ salt;
    h ^= (uint32_t)row * 0xC2B2AE35u;
    h ^= (uint32_t)lroundf(x * 2.0f) * 0x27D4EB2Fu;
    h = (h ^ (h >> 15)) * 0x2C1B3C6Du;
    h ^= (uint32_t)lroundf(y * 2.0f) * 0x165667B1u;
    h = (h ^ (h >> 12)) * 0x297A2D39u;
    return h ^ (h >> 15);
}

static uint32_t obj_rnd(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

/* ITEMS_RollLimitedRandomNumber: de 0 a n - 1 (0 si n <= 0) */
static int obj_rn(uint32_t *s, int n)
{
    return n > 0 ? (int)(obj_rnd(s) % (uint32_t)n) : 0;
}

/* OBJECTS_InitFunction01_Shrine y OBJECTS_GetShrineId: la clase sale de
   Parm0 (1 de vida, 2 de maná, 3 los otros: 1 de 10 mágico) y de esa clase
   uno con LevelMin hasta el nivel; el 4 y el 5 son de vida y de maná, el 16
   de gema */
static int obj_shrine_roll(int row, float x, float y)
{
    const ObjRow *o = &s_orow[row];
    const Level *lv = world_level_at(x, y);
    uint32_t s = obj_seed(row, x, y, 0x73687231u);
    int lid = lv ? lv->id : 0, id = 1;
    if (s_nshr < 2)
        return 1;
    if (o->parm[0]) {
        int cls = o->parm[0] == 1 ? 2 : o->parm[0] == 2 ? 3 : obj_rn(&s, 10) == 0 ? 1 : 4, list[OBJ_SHRINES], n = 0;
        for (int k = 0; k < s_nshr; k++)
            if (s_shr[k].cls == cls)
                list[n++] = k;
        for (int t = 0; t < 8 && n > 0; t++) {
            id = list[obj_rn(&s, n)];
            id = id ? id : 1;
            if (lid >= s_shr[id].lvl_min)
                break;
        }
    } else {
        for (int t = 0; t < 8; t++) {
            id = obj_rn(&s, s_nshr - 1) + 1;
            if (lid >= s_shr[id].lvl_min)
                break;
        }
    }
    return id == 4 ? 2 : id == 5 ? 3 : id == 16 ? 18 : id;
}

/* Lo que se ve de uno que ya se usó (sin soltar nada) */
static int obj_frees_path(const ObjRow *o)
{
    return o->op == OP_BARREL || o->op == OP_XBARREL ||
           ((o->op == OP_CASKET || o->op == OP_URN || o->op == OP_CORPSE || o->op == OP_STASH) && !o->coll1);
}

/* D2_ZONETEST: ¿se pasa por donde está, rompiéndolo o abriéndolo? (un
   racimo de barriles o de cestas puede cerrar un pasillo, como en el
   original). En (*x0, *y0) y (*w, *h), los subtiles que ocupa */
static int obj_passable(int u, int *x0, int *y0, int *w, int *h)
{
    const ObjRow *o = obj_row(u);
    if (!obj_usable(u) || !obj_frees_path(o))
        return 0;
    float x, y;
    units_position(u, 1.0f, &x, &y);
    *w = o->sx > 0 ? o->sx : 1;
    *h = o->sy > 0 ? o->sy : 1;
    *x0 = (int)x - *w / 2;
    *y0 = (int)y - *h / 2;
    return 1;
}

/* ¿Se rompe o se abre para pasar? (los bots: bot_nav_set_breakable) */
static int obj_breakable_unit(int u)
{
    int x0, y0, w, h;
    return obj_passable(u, &x0, &y0, &w, &h);
}

/* Para la navegación de los bots: ¿el subtile lo tapa uno de esos? Solo se
   mira si ahí hay un objeto (COLL_OBJECT) y ninguna pared */
static int obj_breakable_cell(int sx, int sy)
{
    uint16_t c = map_coll(&s_map, sx, sy);
    if (!(c & COLL_OBJECT) || (c & COLL_MASK_PLAYER_PATH & ~COLL_OBJECT))
        return 0;
    for (int u = 0; u < units_count(); u++) {
        int x0, y0, w, h;
        if (units_object_row(u) > 0 && obj_passable(u, &x0, &y0, &w, &h) && sx >= x0 - 1 && sx <= x0 + w &&
            sy >= y0 - 1 && sy <= y0 + h) /* (con uno de margen: lo que tapa puede pasarse del tamaño) */
            return 1;
    }
    return 0;
}

static void obj_show_used(int u)
{
    const ObjRow *o = obj_row(u);
    if (!o)
        return;
    if (o->op == OP_SOUL) { /* (el que ya soltó: quemado o vacío) */
        units_object_mode(u, (units_object_state(u) & OBJ_SOUL_BURNT) ? "s2" : "s3", NULL);
        units_object_finish(u);
    } else if (o->mode2) {
        units_object_mode(u, "on", NULL);
    } else if (o->mode1) {
        units_object_mode(u, "op", NULL);
        units_object_finish(u);
    }
    if (obj_frees_path(o))
        units_object_unblock(&s_map, u);
}

static const char *const k_obj_mode_names[3] = { "nu", "op", "on" };

/* El pozo con esas cargas (lleno, a la mitad, vacío) */
static void obj_well_mode(int u, const ObjRow *o, int left)
{
    int per = o->parm[2] > 0 ? o->parm[2] : 1, m = 2 - left / per;
    m = m < 0 ? 0 : m > 2 ? 2 : m;
    if (units_object_cur_mode(u) != m)
        units_object_mode(u, k_obj_mode_names[m], NULL);
}

/* El índice de s_omem (direccionamiento abierto: el número + 1; 0 = libre) */
#define OBJ_MEM_HASH 8192
static uint16_t s_omem_hash[OBJ_MEM_HASH];

static ObjMem *obj_mem(int row, float x, float y, int create)
{
    int16_t x2 = (int16_t)lroundf(x * 2.0f), y2 = (int16_t)lroundf(y * 2.0f);
    uint32_t h = ((uint32_t)row * 73856093u ^ (uint32_t)(uint16_t)x2 * 19349663u ^ (uint32_t)(uint16_t)y2 * 83492791u ^
                  (uint32_t)s_act * 2654435761u) & (OBJ_MEM_HASH - 1);
    for (;; h = (h + 1) & (OBJ_MEM_HASH - 1)) {
        int i = s_omem_hash[h] - 1;
        if (i < 0)
            break;
        ObjMem *m = &s_omem[i];
        if (m->row == row && m->x2 == x2 && m->y2 == y2 && m->act == s_act)
            return m;
    }
    if (!create || s_nomem >= OBJ_MEM)
        return NULL;
    ObjMem *m = &s_omem[s_nomem];
    s_omem_hash[h] = (uint16_t)(++s_nomem);
    memset(m, 0, sizeof *m);
    m->row = (uint16_t)row;
    m->act = (uint8_t)s_act;
    m->x2 = x2;
    m->y2 = y2;
    return m;
}

/* Al crearlo (OBJECTS_InitHandler): la trampa y la cerradura (mlvl/8+5 % y
   mlvl/2+8 %), el santuario, las cargas del pozo; 1 de 14 de los PreOperate
   ya están abiertos. Después, lo que ya pasó en la partida. */
static void obj_init(int u)
{
    const ObjRow *o = obj_row(u);
    if (!obj_supported(o))
        return;
    int row = units_object_row(u), st = units_object_state(u) & OBJ_HEAL;
    float x, y;
    units_position(u, 1.0f, &x, &y);
    const Level *lv = world_level_at(x, y);
    int mlvl = lv ? lv->mon_level : 0;
    uint32_t s = obj_seed(row, x, y, 0x696E6974u);
    if (o->init == 2 || o->init == 3 || o->init == 57) {
        if (obj_rn(&s, 100) < mlvl / 8 + 5)
            st |= (int)(obj_rnd(&s) & 7) + 1;
        if (o->init != 2 && o->lock && obj_rn(&s, 100) < mlvl / 2 + 8)
            st |= OBJ_LOCKED;
        if (o->init == 57)
            st |= OBJ_SPARK;
    } else if (o->init == 1) {
        st |= (st & OBJ_HEAL) ? 2 : obj_shrine_roll(row, x, y);
    } else if (o->init == 16) {
        st |= 2 * (o->parm[2] > 0 ? o->parm[2] : 1);
    }
    if (o->pre && o->op != OP_SHRINE && o->op != OP_WELL && obj_rn(&s, 14) == 0)
        st |= OBJ_USED;
    const ObjMem *m = obj_mem(row, x, y, 0);
    if (m && o->op == OP_WELL) {
        st = (st & ~0xff) | (m->state & 0xff);
    } else if (m && m->used) {
        st |= OBJ_USED;
    }
    units_object_set_state(u, st);
    if (o->op == OP_WELL)
        obj_well_mode(u, o, st & 0xff);
    else if (st & OBJ_USED)
        obj_show_used(u);
    else if (units_object_cur_mode(u) == 2) /* (los que ciclan arrancan prendidos: los cuerpos de Tristram) */
        units_object_mode(u, "nu", NULL);
}

static void obj_remember(int u)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    ObjMem *m = obj_mem(units_object_row(u), x, y, 1);
    if (m) {
        m->used = 1;
        m->state = (uint16_t)units_object_state(u);
    }
}

/* ---- El reparto por sala (OBJECTS_PopulationHandler) ---- */

/* Lo que se lleva de cada nivel mientras se reparte (D2ObjectRegionStrc) */
typedef struct {
    const Level *lv;
    int rooms, seen, shrines, wells, healing;
    int shr_x[10], shr_y[10], well_x[4], well_y[4];
} ObjRegion;

/* COLLISION_CheckMaskWithSizeXY: el rectángulo de w x h centrado en (cx, cy)
   libre de `mask`. Lo que no es de la zona que se reparte cuenta como
   ocupado: así sale igual esté armada o no la de al lado. */
static int s_obj_zone = -1;

/* Lo que ya se ubicó al repartir la zona, se haya podido crear o no (a uno
   que no carga sus gráficos, como el TombAct2, le falla según cuándo termina
   el hilo de carga): así el reparto sale igual cada vez */
static uint8_t *s_occ;
static int      s_occ_x0, s_occ_y0, s_occ_w, s_occ_h;

static int obj_occupied(int x, int y)
{
    x -= s_occ_x0;
    y -= s_occ_y0;
    return s_occ && x >= 0 && y >= 0 && x < s_occ_w && y < s_occ_h && s_occ[y * s_occ_w + x];
}

static int obj_free(int cx, int cy, int w, int h, uint16_t mask)
{
    w = w > 0 ? w : 1;
    h = h > 0 ? h : 1;
    int x0 = cx - w / 2, y0 = cy - h / 2;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            if ((map_coll(&s_map, x, y) & mask) || map_zone_of(&s_map, x / TILE_SUB, y / TILE_SUB) != s_obj_zone ||
                ((mask & COLL_OBJECT) && obj_occupied(x, y)))
                return 0;
    return 1;
}

static int s_obj_lost; /* los que no se pudieron crear (sin gráficos o sin lugar para más unidades): D2_OBJ_TEST */

/* La sala que se reparte y su semilla (para el InitFn del que no se ve) */
static const int *s_pop_room;
static uint32_t  *s_pop_seed;
static void       obj_trapped_souls(int ox, int oy);

/* -1 = no va ahí; si no, la unidad (o -2: quedó ubicado pero no se pudo crear) */
static int obj_place(int z, int row, int x, int y)
{
    const ObjRow *o = &s_orow[row];
    if (map_zone_of(&s_map, x / TILE_SUB, y / TILE_SUB) != z || !obj_free(x, y, o->sx, o->sy, COLL_VOLUME))
        return -1;
    int sx = o->sx > 0 ? o->sx : 1, sy = o->sy > 0 ? o->sy : 1;
    for (int dy = 0; o->coll0 && dy < sy; dy++) /* lo que ocupa (como lo marca la unidad: HasCollision0) */
        for (int dx = 0; dx < sx; dx++) {
            int ox = x - sx / 2 + dx - s_occ_x0, oy = y - sy / 2 + dy - s_occ_y0;
            if (s_occ && ox >= 0 && oy >= 0 && ox < s_occ_w && oy < s_occ_h)
                s_occ[oy * s_occ_w + ox] = 1;
        }
    if (o->init == 46) { /* TrappedSoulPlaceholder: no se ve; pone las almas */
        obj_trapped_souls(x, y);
        return -2;
    }
    int u = units_place_object_row(&s_map, row, (float)x + 0.5f, (float)y + 0.5f);
    s_obj_lost += u < 0;
    if (u < 0 && getenv("D2_OBJ_TEST"))
        printf("objetos: %s no se pudo poner en (%d, %d)\n", o->cls, x, y);
    return u >= 0 ? u : -2;
}

/* OBJECTS_CreateObject: hasta 5 lugares al azar con 6 de margen */
static int obj_pop_common(int z, uint32_t *s, int row, int count, const int *r)
{
    const ObjRow *o = &s_orow[row];
    int last = -1;
    for (int i = 0; i < count; i++)
        for (int t = 0; t < 5; t++) {
            int x = r[0] + obj_rn(s, r[2] - o->sx - 1), y = r[1] + obj_rn(s, r[3] - o->sy - 1);
            if (x >= r[0] + 1 && y >= r[1] + 1 && x < r[0] + r[2] - 1 && y < r[1] + r[3] - 1 &&
                obj_free(x, y, o->sx + 6, o->sy + 6, OBJ_MASK_PLACE) && (last = obj_place(z, row, x, y)) != -1)
                break;
        }
    return last;
}

/* ¿Va un alma atrapada en (x, y)? Lejos del borde de la sala, 10x10 sin
   paredes, objetos ni puertas y 3x3 libre (las almas ya puestas cuentan:
   COLLIDE_MONSTER, que el port no marca en el mapa) */
static int obj_soul_spot(const int *r, int x, int y, const int (*souls)[2], int n)
{
    if (x <= r[0] + 1 || y <= r[1] + 1 || x >= r[0] + r[2] - 5 || y >= r[1] + r[3] - 5 ||
        !obj_free(x, y, 10, 10, COLL_WALL | COLL_OBJECT | COLL_DOOR) || !obj_free(x, y, 3, 3, OBJ_MASK_PLACE))
        return 0;
    for (int k = 0; k < n; k++)
        if (abs(souls[k][0] - x) <= 2 && abs(souls[k][1] - y) <= 2)
            return 0;
    return 1;
}

static int obj_soul_spawn(int x, int y, int lvl, int (*souls)[2], int *n)
{
    if (*n >= 64)
        return 0;
    int u = units_spawn_monster((obj_rnd(s_pop_seed) & 1) ? "trappedsoul2" : "trappedsoul1", (float)x + 0.5f,
                                (float)y + 0.5f, lvl);
    if (u < 0)
        return 0;
    souls[*n][0] = x;
    souls[*n][1] = y;
    (*n)++;
    return 1;
}

/* OBJECTS_InitFunction46_TrappedSoulPlaceHolder: hasta 12 lugares al azar
   de la sala; en el primero que sirve, un alma en el lugar del objeto y otras
   en racimo a saltos de 10 a 18 subtiles (cada una con menos chance de
   seguir). Las 30/256 por cada 128 subtiles de sala son las que corta.
   shortcut: el original además lo deja abierto para no repetirlo; acá las
   almas, como los otros monstruos, salen de nuevo al volver a armar la zona */
static void obj_trapped_souls(int ox, int oy)
{
    static const int k_sx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 }, k_sy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    const int *r = s_pop_room;
    uint32_t *s = s_pop_seed;
    if (!r || !s || r[2] < 5 || r[3] < 5)
        return;
    const Level *lv = world_level_at((float)ox + 0.5f, (float)oy + 0.5f);
    int lvl = lv && lv->mon_level > 0 ? lv->mon_level : 1, left = 30 * ((r[2] * r[3]) >> 7) >> 8, n = 0;
    int souls[64][2];
    for (int loops = 12; left > 0 && loops > 0; loops--) {
        int x = obj_rn(s, r[2] - 4) + r[0], y = obj_rn(s, r[3] - 4) + r[1];
        if (!obj_soul_spot(r, x, y, (const int (*)[2])souls, n))
            continue;
        left -= obj_soul_spawn(ox, oy, lvl, souls, &n);
        int base = 1, found = 1;
        while (obj_rn(s, base >> 1) == 0 && found) {
            found = 0;
            for (int c = 0, lim = 3 * (left > 4 ? left : 4); c < lim && !found; c++) {
                /* (de a un sorteo por línea: el orden de los operandos no está definido en C, y en red la Pi,
                   Linux y Windows tienen que poner las almas en el mismo lugar) */
                int step = 2 * (obj_rn(s, 5) + 5);
                x += step * k_sx[obj_rnd(s) & 7];
                step = 2 * (obj_rn(s, 5) + 5);
                y += step * k_sy[obj_rnd(s) & 7];
                found = obj_soul_spot(r, x, y, (const int (*)[2])souls, n);
            }
            if (found) {
                base++;
                left -= obj_soul_spawn(x, y, lvl, souls, &n);
            }
        }
    }
}

/* OBJECTS_PopulateFn1_CasketJarSarcophagusUrn: en racimos (las urnas y las
   cestas, pegadas) de una de las clases de su lista */
static void obj_pop_cluster(int z, uint32_t *s, int id, int count, const int *r)
{
    static const int k_casket6[] = { 3, 28 }, k_casket3[] = { 79, 53, 1 }, k_urns[] = { 4, 9, 52, 94, 95 };
    static const int k_mummy[] = { 89, 284 }, k_baskets[] = { 208, 209 };
    const int *ids;
    int n, tries = 12, base = 1, step = 0, tight = 0;
    if (id == 3) {
        ids = k_casket6, n = 2, tries = 18, base = 5, step = 5;
    } else if (id == 79 || id == 1) {
        ids = k_casket3, n = 3, base = 5, step = 5;
    } else if (id == 4) {
        ids = k_urns, n = 5, tight = 1;
    } else if (id == 89) {
        ids = k_mummy, n = 2, base = 5, step = 5;
    } else if (id == 208 || id == 209) {
        ids = k_baskets, n = 2, tight = 1;
    } else {
        return;
    }
    static const int k_sx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 }, k_sy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    const ObjRow *o = &s_orow[units_objects_row_of_id(id) > 0 ? units_objects_row_of_id(id) : 1];
    while (count > 0 && tries-- > 0) {
        int x = r[0] + obj_rn(s, r[2] - o->sx - 1), y = r[1] + obj_rn(s, r[3] - o->sy - 1);
        if (r[2] < o->sx + 2 || r[3] < o->sy + 2 || x <= r[0] + 1 || y <= r[1] + 1 || x >= r[0] - o->sx + r[2] - 2 ||
            y >= r[1] - o->sy + r[3] - 2 || !obj_free(x, y, o->sx + 7, o->sy + 7, COLL_WALL | COLL_OBJECT | COLL_DOOR) ||
            !obj_free(x, y, o->sx, o->sy, OBJ_MASK_PLACE))
            continue;
        int spawned = obj_place(z, units_objects_row_of_id(ids[obj_rn(s, n)]), x, y) != -1, ok = 1;
        while (obj_rn(s, spawned >> 1) == 0 && ok) {
            ok = 0;
            for (int c = 0, lim = 3 * (count > 4 ? count : 4); c < lim && !ok; c++) {
                int d = (int)(obj_rnd(s) & 7);
                x += 2 * (base + obj_rn(s, step)) * k_sx[d];
                y += 2 * (base + obj_rn(s, step)) * k_sy[d];
                if (tight)
                    ok = x > r[0] + 2 && y > r[1] + 2 && x < r[0] + r[2] - o->sx && y < r[1] + r[3] - o->sy &&
                         obj_free(x, y, o->sx + 2, o->sy + 2, OBJ_MASK_PLACE);
                else
                    ok = x > r[0] + 1 && y > r[1] + 1 && x < r[0] - o->sx + r[2] - 2 && y < r[1] - o->sy + r[3] - 2 &&
                         obj_free(x, y, o->sx + 7, o->sy + 7, COLL_WALL | COLL_OBJECT | COLL_DOOR) &&
                         obj_free(x, y, o->sx, o->sy, OBJ_MASK_PLACE);
            }
            if (ok)
                spawned += obj_place(z, units_objects_row_of_id(ids[obj_rn(s, n)]), x, y) != -1;
        }
        count--;
    }
}

/* OBJECTS_PopulateFn4_Barrel y Fn5_Crate: racimos separados por Xspace y
   Yspace, hasta 8 por sala; un barril de cada 3 (y de cada 4 en el racimo)
   explota; los cajones que no son cajones son urnas */
static void obj_pop_barrels(int z, uint32_t *s, int id, int count, const int *r, int barrels)
{
    static const int k_urns[] = { 4, 9, 52, 94, 95 };
    static const int k_sx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 }, k_sy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    int brow = units_objects_row_of_id(barrels ? 7 : id), xbrow = units_objects_row_of_id(11);
    if (brow <= 0)
        return;
    const ObjRow *o = &s_orow[brow];
    int k = 2 * count, placed = 0;
    while (count > 0 && k-- > 0) {
        int x = r[0] + obj_rn(s, r[2]), y = r[1] + obj_rn(s, r[3]);
        if (x <= r[0] + 2 || y <= r[1] + 2 || x >= r[0] + r[2] - o->sx || y >= r[1] + r[3] - o->sy ||
            !obj_free(x, y, o->sx + 2, o->sy + 2, OBJ_MASK_PLACE))
            continue;
        int row = barrels ? (obj_rn(s, 3) == 0 ? xbrow : brow)
                          : id == 46 ? brow : units_objects_row_of_id(k_urns[obj_rn(s, 5)]);
        placed += obj_place(z, row, x, y) != -1;
        if (placed >= 8)
            return;
        int more = 1, ok = 1;
        while (obj_rn(s, more / 2) == 0 && ok) {
            ok = 0;
            for (int t = 0; !ok && t < (barrels ? 15 : 4); t++) {
                int d = (int)(obj_rnd(s) & 7);
                x += o->xspace * k_sx[d];
                y += o->yspace * k_sy[d];
                ok = x > r[0] + 2 && y > r[1] + 2 && x < r[0] + r[2] - o->sx && y < r[1] + r[3] - o->sy &&
                     obj_free(x, y, o->sx + 2, o->sy + 2, OBJ_MASK_PLACE);
            }
            if (ok) {
                row = barrels ? ((obj_rnd(s) & 3) == 0 ? xbrow : brow)
                              : id == 46 ? brow : units_objects_row_of_id(k_urns[obj_rn(s, 5)]);
                placed += obj_place(z, row, x, y) != -1;
                more++;
                if (placed >= 8)
                    return;
            }
        }
        count--;
    }
}

/* OBJRGN_ShouldSpawnHealingShrineOrWell: pasó 3/4 del nivel sin santuario de vida */
static int obj_need_healing(const ObjRegion *g)
{
    return g->rooms > 0 && (g->seen << 7) / g->rooms > 96 && !g->healing;
}

/* OBJECTS_PopulateFn2_WaypointShrine: hasta 10 por nivel (y uno cada 8
   salas), a 50 de otro en x y en y; arriba o a la izquierda de la sala según
   su Orientation. Si falta el de vida, ese sale de vida. */
static void obj_pop_shrine(int z, uint32_t *s, int row, const int *r, ObjRegion *g)
{
    const ObjRow *o = &s_orow[row];
    int heal = obj_need_healing(g), tries = heal ? 30 : 3;
    if (g->shrines >= 10 || g->shrines > g->rooms / 8)
        return;
    while (tries-- > 0) {
        if (r[2] < o->sx + 2 || r[3] < o->sy + 2)
            continue;
        for (int t = 0; t < 5; t++) {
            int x, y;
            if (o->orient == 1) {
                x = r[0] + r[2] / 4 + obj_rn(s, r[2] / 2);
                y = r[1] + 1 + obj_rn(s, 1);
            } else if (o->orient == 2) {
                x = r[0] + 1 + obj_rn(s, 1);
                y = r[1] + r[3] / 4 + obj_rn(s, r[3] / 2);
            } else {
                x = r[0] + obj_rn(s, r[2] - o->sx - 1);
                y = r[1] + obj_rn(s, r[3] - o->sy - 1);
            }
            if (x < r[0] + 1 || y < r[1] + 1 || x >= r[0] + r[2] - 1 || y >= r[1] + r[3] - 1 ||
                !obj_free(x, y, o->sx + 6, o->sy + 6, OBJ_MASK_PLACE))
                continue;
            int far = 1;
            for (int k = 0; k < g->shrines; k++)
                far &= abs(g->shr_x[k] - x) >= 50 && abs(g->shr_y[k] - y) >= 50;
            if (!far)
                continue;
            /* el de vida: la clase de su Parm1 (el original le cambia la clase) */
            int prow = row, healing = 0;
            if (heal && obj_shrine_roll(row, (float)x + 0.5f, (float)y + 0.5f) != 2) {
                int hrow = units_objects_row_of_id(o->parm[1] > 0 && o->parm[1] < 573 ? o->parm[1] : 84);
                prow = hrow > 0 ? hrow : prow;
                healing = 1;
            }
            int u = obj_place(z, prow, x, y);
            if (u == -1)
                return;
            if (healing && u >= 0)
                units_object_set_state(u, OBJ_HEAL);
            if (healing || obj_shrine_roll(prow, (float)x + 0.5f, (float)y + 0.5f) == 2)
                g->healing++;
            g->shr_x[g->shrines] = x;
            g->shr_y[g->shrines] = y;
            g->shrines++;
            return;
        }
    }
}

/* OBJECTS_PopulateFn8_Well: hasta 4 por nivel (y uno cada 8 salas), a 100 de otro */
static void obj_pop_well(int z, uint32_t *s, int row, const int *r, ObjRegion *g)
{
    const ObjRow *o = &s_orow[row];
    if (g->wells >= 4 || g->wells > g->rooms / 8)
        return;
    int sx = 2 * o->sx + 1, sy = 2 * o->sy + 1;
    if (r[2] <= sx || r[3] <= sy)
        return;
    for (int t = 0; t < 5; t++) {
        int x = r[0] + obj_rn(s, r[2] - sx - 1), y = r[1] + obj_rn(s, r[3] - sy - 1);
        if (x < r[0] + 1 || y < r[1] + 1 || x >= r[0] + r[2] - 1 || y >= r[1] + r[3] - 1)
            continue;
        int far = 1;
        for (int k = 0; k < g->wells; k++)
            far &= abs(g->well_x[k] - x) >= 100 && abs(g->well_y[k] - y) >= 100;
        if (!far || !obj_free(x, y, sx + 6, sy + 6, OBJ_MASK_PLACE))
            continue;
        if (obj_place(z, row, x, y) != -1) {
            g->well_x[g->wells] = x;
            g->well_y[g->wells] = y;
            g->wells++;
        }
        return;
    }
}

/* ¿Hay un waypoint en la sala? (en esa no se pone nada) */
static int obj_room_has_waypoint(const int *r, const float (*wp)[2], int nwp)
{
    for (int k = 0; k < nwp; k++)
        if (wp[k][0] >= r[0] && wp[k][0] < r[0] + r[2] && wp[k][1] >= r[1] && wp[k][1] < r[1] + r[3])
            return 1;
    return 0;
}

/* El poblado de una zona, de a salas (obj_zone_step): las salas, las
   regiones de cada nivel y los waypoints se cuentan al empezar */
static struct {
    int       z, n, next, col_id; /* la zona (-1 = ninguna), cuántas salas y la que sigue */
    ObjRegion reg[8];
    int       nreg;
    float     wp[8][2];
    int       nwp;
} s_pop = { .z = -1 };
static int s_pop_rooms[400][4];

static void obj_populate_end(void)
{
    free(s_occ);
    s_occ = NULL;
    s_pop_room = NULL;
    s_pop_seed = NULL;
    s_pop.z = -1;
}

static void obj_populate_begin(int z)
{
    int (*rooms)[4] = s_pop_rooms;
    int n = world_zone_rooms(z, rooms, 400);
    ObjRegion *reg = s_pop.reg;
    int nreg = 0;
    memset(s_pop.reg, 0, sizeof s_pop.reg);
    for (int i = 0; i < n; i++) { /* a subtiles, y las salas de cada nivel */
        for (int k = 0; k < 4; k++)
            rooms[i][k] *= TILE_SUB;
        const Level *lv = world_level_at(rooms[i][0] + rooms[i][2] / 2.0f, rooms[i][1] + rooms[i][3] / 2.0f);
        if (!lv || lv->zone != z || lv->town)
            continue;
        int g = 0;
        while (g < nreg && reg[g].lv != lv)
            g++;
        if (g == nreg && nreg < 8)
            reg[nreg++].lv = lv;
        if (g < nreg)
            reg[g].rooms++;
    }
    s_pop.nreg = nreg;
    s_pop.nwp = 0;
    for (int u = 0; u < units_count() && s_pop.nwp < 8; u++) { /* los waypoints de la zona */
        int row = units_object_row(u);
        if (row && row < s_orows && units_zone(u) == z && strstr(s_orow[row].cls, "aypoint")) {
            units_position(u, 1.0f, &s_pop.wp[s_pop.nwp][0], &s_pop.wp[s_pop.nwp][1]);
            s_pop.nwp++;
        }
    }
    s_pop.col_id = txt_col(&s_objgroup, "*ID");
    s_obj_zone = z;
    int zx0, zy0, zx1, zy1;
    world_zone_rect(z, &zx0, &zy0, &zx1, &zy1);
    s_occ_x0 = zx0 * TILE_SUB;
    s_occ_y0 = zy0 * TILE_SUB;
    s_occ_w = (zx1 - zx0) * TILE_SUB;
    s_occ_h = (zy1 - zy0) * TILE_SUB;
    s_occ = s_occ_w > 0 && s_occ_h > 0 ? calloc((size_t)s_occ_w * (size_t)s_occ_h, 1) : NULL;
    s_pop.z = z;
    s_pop.n = n;
    s_pop.next = 0;
}

/* Lo de la sala i (con su semilla: sale igual en cualquier pasada) */
static void obj_populate_room(int i)
{
    int z = s_pop.z, col_id = s_pop.col_id, nreg = s_pop.nreg;
    ObjRegion *reg = s_pop.reg;
    const int *r = s_pop_rooms[i];
    const Level *lv = world_level_at(r[0] + r[2] / 2.0f, r[1] + r[3] / 2.0f);
    int g = 0;
    while (g < nreg && reg[g].lv != lv)
        g++;
    if (g == nreg || obj_room_has_waypoint(r, (const float (*)[2])s_pop.wp, s_pop.nwp))
        return;
    uint32_t s = obj_seed(z, (float)r[0], (float)r[1], 0x706F7075u); /* la de la sala (pRoom->pSeed) */
    s_pop_room = r;
    s_pop_seed = &s;
    reg[g].seen++;
    for (int k = 0; k < 8; k++) {
        int grp = lv->objgrp[k], prob = obj_rn(&s, 100);
        /* (el original mira el SubClass del objeto con el número del grupo) */
        if (obj_need_healing(&reg[g]) && grp + 1 < s_orows && s_orow[grp + 1].sub)
            prob = 100;
        if (!grp || prob > lv->objprb[k])
            continue;
        char key[12];
        snprintf(key, sizeof key, "%d", grp);
        int gr = col_id >= 0 ? txt_find_row(&s_objgroup, col_id, key) : -1;
        if (gr < 0)
            break;
        int pick = obj_rn(&s, 100), acc = 0;
        for (int e = 0; e < 8; e++) {
            char col[12];
            snprintf(col, sizeof col, "ID%d", e);
            int id = txt_get_int(&s_objgroup, gr, col, 0);
            if (!id)
                break;
            snprintf(col, sizeof col, "PROB%d", e);
            acc += txt_get_int(&s_objgroup, gr, col, 0);
            if (pick >= acc)
                continue;
            snprintf(col, sizeof col, "DENSITY%d", e);
            int dens = txt_get_int(&s_objgroup, gr, col, 0), row = units_objects_row_of_id(id);
            int count = dens * ((r[2] * r[3]) >> 7) >> 8;
            if (row <= 0 || row >= s_orows)
                break;
            switch (s_orow[row].pop) {
            case 1: obj_pop_cluster(z, &s, id, count, r); break;
            case 2: obj_pop_shrine(z, &s, row, r, &reg[g]); break;
            case 3: case 7: obj_pop_common(z, &s, row, count, r); break;
            case 4: obj_pop_barrels(z, &s, id, count, r, 1); break;
            case 5: obj_pop_barrels(z, &s, id, count, r, 0); break;
            case 6: { /* los cadáveres de las pícaras: 30% con moscas */
                int u = obj_pop_common(z, &s, row, count, r), flies = u != -1 && obj_rn(&s, 100) > 70;
                if (u >= 0 && flies) {
                    float fx, fy;
                    units_position(u, 1.0f, &fx, &fy);
                    units_spawn_object("Flies", fx, fy, NULL);
                }
                break;
            }
            case 8: obj_pop_well(z, &s, row, r, &reg[g]); break;
            case 9: /* OBJECTS_PopulateFn9_TrappedSoul: uno solo (la tirada es contra 100: siempre) */
                obj_rnd(&s);
                for (int t = 0; t < count && obj_pop_common(z, &s, row, 1, r) == -1; t++) {
                }
                break;
            default: break;
            }
            break;
        }
    }
    s_pop_room = NULL;
    s_pop_seed = NULL;
}

/* La zona recién armada (entre units_zone_begin y units_zone_end): los al
   azar de sus salas y cómo está cada uno. De a partes: `begin` empieza (y
   tira lo de una zona que quedó a medias); hasta `cap` salas (0 = sin tope)
   o hasta pasarse `ms` milisegundos (0 = sin hora), por lo menos una.
   1 = terminó. */
static int obj_zone_step(int z, int begin, int cap, double ms)
{
    if (begin) {
        obj_load();
        obj_populate_end();
        if (!world_zone_is_town(z) && s_objgroup.rows > 0)
            obj_populate_begin(z);
    }
    if (s_pop.z == z) {
        double until = ms > 0 ? wall_ms() + ms : 0;
        for (int done = 0; s_pop.next < s_pop.n; done++) {
            if (done > 0 && ((cap > 0 && done >= cap) || (until > 0 && wall_ms() > until)))
                return 0;
            obj_populate_room(s_pop.next++);
        }
        obj_populate_end();
    }
    for (int u = 0; u < units_count(); u++)
        if (units_zone(u) == z && obj_row(u))
            obj_init(u);
    return 1;
}

/* ---- Al usarlos ---- */

static int obj_level_mlvl(int id)
{
    for (int i = 0; i < world_level_count(); i++)
        if (world_level(i)->id == id)
            return world_level(i)->mon_level;
    return 0;
}

/* OBJMODE_DropFromChestTCWithQuality: "Act N Chest A/B/C" (de su
   dificultad: "Act N (N) Chest A") según el tercio del rango de niveles de
   monstruos del acto en que cae el del lugar */
static void obj_chest_tc(int mlvl, char *tc, size_t cap)
{
    static const int k_ids[5][2] = { { 2, 37 }, { 41, 73 }, { 76, 102 }, { 104, 108 }, { 109, 132 } };
    static const char *const k_diff[3] = { "", " (N)", " (H)" };
    int a = s_act >= 1 && s_act <= 5 ? s_act - 1 : 0;
    int lo = obj_level_mlvl(k_ids[a][0]), hi = obj_level_mlvl(k_ids[a][1]);
    lo = lo ? lo : mlvl;
    hi = hi ? hi : mlvl;
    int off = (abs(hi - lo) + 1) / 3, idx = 0;
    if (mlvl >= off + lo)
        idx = (mlvl >= lo + 2 * off) + 1;
    snprintf(tc, cap, "Act %d%s Chest %c", a + 1, k_diff[s_diff], 'A' + idx);
}

/* Lo que sale queda alrededor de lo que ocupa (D2GAME_GetFreeSpaceEx) */
static void obj_drop_items(int u, const Item *it, int n)
{
    const ObjRow *o = obj_row(u);
    float x, y;
    units_position(u, 1.0f, &x, &y);
    float rad = (float)((o && o->sx > o->sy ? o->sx : o ? o->sy : 1)) / 2.0f + 0.6f;
    for (int i = 0; i < n; i++) {
        float px = x, py = y;
        for (int t = 0; t < 24; t++) {
            float a = (float)(rnd() % 628) / 100.0f, d = rad + (float)(rnd() % 12) / 10.0f;
            float nx = x + cosf(a) * d, ny = y + sinf(a) * d;
            if (!(map_coll(&s_map, (int)floorf(nx), (int)floorf(ny)) & (COLL_MASK_PLAYER_PATH | COLL_BLANK))) {
                px = nx;
                py = ny;
                break;
            }
        }
        items_ground_add(&it[i], px, py);
    }
}

static int obj_is_magic(const Item *it)
{
    return it->quality == QUALITY_MAGIC || it->quality == QUALITY_RARE || it->quality == QUALITY_SET ||
           it->quality == QUALITY_UNIQUE || it->quality == QUALITY_CRAFTED;
}

/* Una tirada de la clase de tesoro del cofre: 0 nada, 1 soltó, 2 el primero es mágico o mejor */
static int obj_chest_drop(int u, const Player *p, int quality)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    const Level *lv = world_level_at(x, y);
    int mlvl = lv && lv->mon_level > 0 ? lv->mon_level : 1;
    char tc[24];
    obj_chest_tc(mlvl, tc, sizeof tc);
    Item out[6];
    int n = items_roll_drop_quality(tc, mlvl, p ? p->bonus[STAT_MAGIC_FIND] : 0, quality, out, 6);
    obj_drop_items(u, out, n);
    return n == 0 ? 0 : obj_is_magic(&out[0]) ? 2 : 1;
}

static void obj_drop_code(int u, const char *code, int ilvl, int quality)
{
    Item out[2];
    int n = items_roll_drop_quality(code, ilvl, 0, quality, out, 1);
    obj_drop_items(u, out, n);
}

/* D2GAME_DoKeyCheck: gasta una llave de la mochila */
static int obj_use_key(Player *p)
{
    int k = bag_index_of(p, "key");
    if (k < 0)
        return 0;
    if (p->bag[k].amount > 1)
        p->bag[k].amount--;
    else
        bag_remove(p, k);
    return 1;
}

static int obj_has_key(const Player *p)
{
    return bag_index_of(p, "key") >= 0;
}

/* OBJRGN_GetTrapMonsterId: el zombi (la momia en el Acto 2) o los
   esqueletos del nivel; si no hay, la cimitarra voladora (en el Acto 1 no
   sale: NULL) */
static const char *obj_trap_monster(float x, float y)
{
    static const char *const k_skel[] = { "skeleton", "sk_archer", "skmage_pois", "skmage_cold", "skmage_fire", "skmage_ltng" };
    static char id[24];
    const Level *lv = world_level_at(x, y);
    const char *base = s_act == 2 ? "mummy" : "zombie";
    for (int k = 0; lv && k < lv->num_mon; k++) {
        const char *m = lv->monsters[k];
        size_t len = strlen(m);
        while (len > 0 && isdigit((unsigned char)m[len - 1]))
            len--;
        if (strlen(base) == len && strncmp(m, base, len) == 0) {
            snprintf(id, sizeof id, "%s1", base);
            return id;
        }
        for (unsigned s = 0; s < sizeof k_skel / sizeof k_skel[0]; s++)
            if (strlen(k_skel[s]) == len && strncmp(m, k_skel[s], len) == 0) {
                snprintf(id, sizeof id, "%s1", k_skel[s]);
                return id;
            }
    }
    return s_act == 1 ? NULL : "flyingscimitar";
}

/* D2GAME_SpawnTrapMonster: al lado del objeto, de la zona del objeto */
static void obj_spawn_monsters(int u, int n)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    const char *id = obj_trap_monster(x, y);
    const Level *lv = world_level_at(x, y);
    if (!id)
        return;
    for (int k = 0; k < n; k++) {
        float mx = x + (float)k, my = y + 1.0f;
        find_free_spot(&mx, &my);
        units_zone_begin(units_zone(u));
        units_spawn_monster(id, mx, my, lv ? lv->mon_level : 1);
        units_zone_end();
    }
}

static void obj_event(int kind, int ticks, int u, const Player *p, int arg)
{
    for (int i = 0; i < OBJ_EVENTS; i++) {
        ObjEvent *e = &s_oev[i];
        if (e->kind != OE_NONE)
            continue;
        memset(e, 0, sizeof *e);
        e->kind = kind;
        e->ticks = ticks;
        e->arg = arg;
        e->player = p ? (int)(p - s_pl) : 0;
        e->unit = u;
        e->row = units_object_row(u);
        units_position(u, 1.0f, &e->x, &e->y);
        const Level *lv = world_level_at(e->x, e->y);
        e->level = lv && lv->mon_level > 0 ? lv->mon_level : 1;
        return;
    }
}

/* D2GAME_SetTrapCallback: la trampa salta a los 35 cuadros (la de los
   monstruos, si hay cuáles: la cimitarra no sale en el Acto 1) */
static void obj_set_trap(int u, const Player *p, int trap)
{
    if (trap <= 0 || trap > 9)
        return;
    float x, y;
    units_position(u, 1.0f, &x, &y);
    if (trap >= 8 && !obj_trap_monster(x, y))
        return;
    obj_event(OE_TRAP, OBJ_TRAP_TICKS, u, p, trap);
    audio_play_name("object_trap_trigger", x, y);
}

/* OBJEVAL_ApplyTrapObjectDamage: entre 1/32 y 1/8 de la vida que le queda
   (por Damage de objects.txt), si le pega (65% por lo menos) */
static int obj_trap_damage(int hp, int lvl, int dex, int def, int dmg_pct)
{
    int lv = lvl > 0 ? lvl : 1, extra = lv >> 2 > 0 ? (int)(rnd() % (uint32_t)(lv >> 2)) : 0;
    int param = 2 * ((uint8_t)(lv + extra) - 5 * (dex >> 1) - lv), chance = param - def + 125;
    if (chance < 65)
        chance = 65;
    if ((int)(rnd() % 100) >= chance || hp <= 0)
        return 0;
    int hp256 = hp * 256, mn = hp256 >> 5 > 1 ? hp256 >> 5 : 1, mx = hp256 >> 3 > mn + 1 ? hp256 >> 3 : mn + 1;
    return ((int)(rnd() % (uint32_t)(mx - mn + 256)) + mn) * dmg_pct / 100 / 256;
}

static void obj_hurt_player(Player *t, int dmg_pct, int elem)
{
    if (t->state >= PS_DYING || (t->level && t->level->town))
        return;
    int dmg = obj_trap_damage(t->hp, t->level_num, player_dex(t), player_defense(t), dmg_pct);
    if (elem != ELEM_NONE)
        dmg = player_elem_hit(t, elem, dmg);
    if (dmg > 0)
        take_damage(t, dmg, 0);
}

static void obj_hurt_monster(int i, int dmg_pct, int elem, const Player *by)
{
    int hp, max;
    units_health(i, &hp, &max);
    int dmg = units_resist(i, elem, obj_trap_damage(hp, units_level(i), 0, units_defense(i), dmg_pct));
    if (dmg > 0 && units_damage(i, dmg) > 0) /* (lo que mata un objeto no da experiencia) */
        drop_loot(i, by);
}

/* Un proyectil de la trampa al jugador que la abrió, del lugar del objeto */
static Missile *obj_trap_missile(const ObjEvent *e, const char *name, float dx, float dy, int elem, int emin, int emax)
{
    Missile *mi = missiles_spawn(name, e->x, e->y, dx, dy);
    if (!mi)
        return NULL;
    mi->owner = -1;
    mi->source = -1;
    mi->level = e->level;
    mi->ar = 100;
    mi->flags |= MF_ALWAYS_HIT;
    mi->dmin = mi->dmax = 0;
    mi->elem = elem;
    mi->emin = emin;
    mi->emax = emax;
    return mi;
}

/* Las trampas (gpObjectTrapHandlerTable): 1 rayo, 2 y 6 saeta de fuego, 3
   nube de veneno (PrimePoisonNova), 4 nova, 5 y 7 fuego en el piso, 8 y 9
   monstruos. Las de trap-*.txt disparan una vez, con su habilidad en 1. */
static void obj_trap_fire(const ObjEvent *e)
{
    const Player *p = &s_pl[e->player < s_num_players ? e->player : 0];
    float dx = p->x - e->x, dy = p->y - e->y;
    int lo, hi, elem, dmin, dmax;
    switch (e->arg) {
    case 1:
        missiles_level_damage("trapnova", 1, &lo, &hi);
        obj_trap_missile(e, missiles_exists("chainlightning") ? "chainlightning" : "trapnova", dx, dy, ELEM_LTNG, lo, hi);
        break;
    case 2: case 6:
        missiles_base_damage("trapfirebolt", &elem, &lo, &hi, &dmin, &dmax);
        obj_trap_missile(e, "trapfirebolt", dx, dy, ELEM_FIRE, lo, hi);
        break;
    case 3:
        missiles_level_damage("primepoisoncloud", 1, &lo, &hi);
        for (int s = 0; s < 16; s++) {
            float a = (float)s * PI_F / 8.0f;
            Missile *c = obj_trap_missile(e, "primepoisoncloud", cosf(a), sinf(a), ELEM_POIS, lo, hi);
            if (!c)
                break;
            c->vx = cosf(a) * 0.35f;
            c->vy = sinf(a) * 0.35f;
            c->flags |= MF_CLOUD;
            c->elen = 3 * TICK_RATE;
            c->radius10 = 20;
            c->life = 40;
        }
        break;
    case 4:
        missiles_level_damage("trapnova", 1, &lo, &hi);
        for (int s = 0; s < 16; s++) {
            float a = (float)s * PI_F / 8.0f;
            obj_trap_missile(e, "trapnova", cosf(a), sinf(a), ELEM_LTNG, lo, hi);
        }
        break;
    case 5: case 7: { /* fuego en el piso un rato (FireLarge y al lado FireSmall) */
        int big = units_spawn_object("FireLarge", e->x, e->y, "on");
        int small = units_spawn_object("FireSmall", e->x + 1.0f, e->y, "on");
        int ids[2] = { big, small };
        for (int k = 0; k < 2; k++) {
            if (ids[k] < 0)
                continue;
            for (int i = 0; i < OBJ_EVENTS; i++)
                if (s_oev[i].kind == OE_NONE) {
                    ObjEvent *f = &s_oev[i];
                    memset(f, 0, sizeof *f);
                    f->kind = OE_FIRE;
                    f->ticks = 25;
                    f->life = 10 * TICK_RATE; /* shortcut: el original no dice cuánto dura; 10 s */
                    f->unit = ids[k];
                    f->row = units_object_row(ids[k]);
                    f->arg = k == 0 ? 2 : 1; /* Parm0 + 1 de FireLarge y FireSmall */
                    units_position(ids[k], 1.0f, &f->x, &f->y);
                    f->level = e->level;
                    break;
                }
        }
        audio_play_name("object_fire_loop_brazier", e->x, e->y);
        break;
    }
    case 8: case 9:
        if (units_object_row(e->unit) == e->row)
            obj_spawn_monsters(e->unit, 1 + (int)(rnd() % 2));
        break;
    default:
        break;
    }
}

/* El sonido al usarlo (el original lo elige el cliente por la clase) */
static void obj_sound(int u, const ObjRow *o)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    char low[24], name[40];
    size_t i = 0;
    for (; o->name[i] && i + 1 < sizeof low; i++)
        low[i] = (char)tolower((unsigned char)o->name[i]);
    low[i] = '\0';
    int n1 = 1 + (int)(rnd() % 5), n3 = 1 + (int)(rnd() % 3);
    if (o->op == OP_CHEST || o->op == OP_XCHEST)
        snprintf(name, sizeof name, o->sx * o->sy >= 3 ? "object_chest_large" : "object_chest_small");
    else if (o->op == OP_CASKET)
        snprintf(name, sizeof name, strstr(low, "bed") ? "object_bed" : strstr(low, "hole") ? "object_grave" : "object_casket");
    else if (o->op == OP_BARREL || strstr(low, "crate") || strstr(low, "log"))
        snprintf(name, sizeof name, "object_wood_break_%d", n1);
    else if (o->op == OP_XBARREL)
        snprintf(name, sizeof name, "object_barrel_explode");
    else if (strstr(low, "urn"))
        snprintf(name, sizeof name, "object_urn_break_%d", n1);
    else if (strstr(low, "basket"))
        snprintf(name, sizeof name, "object_basket_%d", n1);
    else if (strstr(low, "jug") || strstr(low, "jar"))
        snprintf(name, sizeof name, "object_glass_break_%d", n3);
    else if (strstr(low, "cocoon"))
        snprintf(name, sizeof name, "object_cocoon_%d", n3);
    else if (strstr(low, "ratnest"))
        snprintf(name, sizeof name, "object_ratnest_%d", n3);
    else if (strstr(low, "skull"))
        snprintf(name, sizeof name, "object_skullpile_%d", n1);
    else if (strstr(low, "goo"))
        snprintf(name, sizeof name, "object_goopile_%d", n3);
    else if (strstr(low, "rock") || strstr(low, "boulder"))
        snprintf(name, sizeof name, strstr(low, "pile") ? "object_rockpile" : "object_stone_small");
    else if (o->op == OP_STASH)
        snprintf(name, sizeof name, "object_stash_jungle_%d", 1 + (int)(rnd() % 4));
    else if (strstr(low, "stash") || strstr(low, "pillar"))
        snprintf(name, sizeof name, "object_stash_mephisto_%d", 1 + (int)(rnd() % 2));
    else if (o->op == OP_ARMOR)
        snprintf(name, sizeof name, "object_armorstand");
    else if (o->op == OP_WEAPON)
        snprintf(name, sizeof name, "object_weaponrack");
    else if (o->op == OP_BOOKS)
        snprintf(name, sizeof name, "object_bookshelf");
    else if (o->op == OP_WELL)
        snprintf(name, sizeof name, "object_well");
    else
        snprintf(name, sizeof name, "object_corpse_loot");
    audio_play_name(name, x, y);
}

/* Se abre: la animación (abriéndose y abierto, como OBJECTS_ChestEnd), deja
   pasar si su modo abierto no choca, se recuerda y salta su trampa */
static void obj_open(int u, const Player *p, int trap)
{
    const ObjRow *o = obj_row(u);
    if (o->mode1)
        units_object_mode(u, "op", o->mode2 ? "on" : NULL);
    else if (o->mode2)
        units_object_mode(u, "on", NULL);
    units_object_set_state(u, units_object_state(u) | OBJ_USED);
    if (obj_frees_path(o))
        units_object_unblock(&s_map, u);
    obj_remember(u);
    obj_sound(u, o);
    obj_set_trap(u, p, trap);
}

/* OBJECTS_OperateFunction07_ExplodingBarrel: les pega a los de alrededor
   (a 3) y hace explotar a los barriles de al lado (a 2) */
static void obj_explode(int u, const Player *p, int depth)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    const ObjRow *o = obj_row(u);
    int dmg = o ? o->dmg : 100;
    obj_open(u, p, 0);
    for (int i = 0; i < s_num_players; i++) {
        float dx = s_pl[i].x - x, dy = s_pl[i].y - y;
        if (dx * dx + dy * dy <= 9.0f)
            obj_hurt_player(&s_pl[i], dmg, ELEM_NONE);
    }
    int ids[64], n = units_enemies_near(x, y, 3.0f, ids, 64);
    for (int k = 0; k < n; k++)
        obj_hurt_monster(ids[k], dmg, ELEM_NONE, p);
    for (int v = 0; v < units_count() && depth < 16; v++) {
        if (v == u || obj_usable(v) != OP_XBARREL)
            continue;
        float vx, vy;
        units_position(v, 1.0f, &vx, &vy);
        if ((vx - x) * (vx - x) + (vy - y) * (vy - y) <= 4.0f)
            obj_explode(v, p, depth + 1);
    }
}

/* OBJECTS_OperateFunction22_Well: vida, maná y estamina (Parm1/256 del
   máximo; Parm3 dice cuáles), saca el veneno y el frío y cura a la
   mercenaria; gasta una carga solo si curó algo, y a los Parm0 cuadros
   recupera una */
static void obj_well(Player *p, int u, const ObjRow *o)
{
    int st = units_object_state(u), left = st & 0xff, add = o->parm[1], used = 0;
    if (left <= 0)
        return;
    if (p->hp < p->hp_max && (o->parm[3] & 2)) {
        p->hp += p->hp_max * add >> 8;
        p->hp = p->hp > p->hp_max ? p->hp_max : p->hp;
        used = 1;
    }
    if (p->mana < p->mana_max && (o->parm[3] & 1)) {
        p->mana += p->mana_max * add >> 8;
        p->mana = p->mana > p->mana_max ? p->mana_max : p->mana;
        used = 1;
    }
    if (p->stamina < p->stamina_max) {
        p->stamina += p->stamina_max * (float)add / 256.0f;
        p->stamina = p->stamina > p->stamina_max ? p->stamina_max : p->stamina;
        used = 1;
    }
    if (p->poison_ticks > 0 || p->chill_ticks > 0) {
        p->poison_ticks = p->chill_ticks = 0;
        used = 1;
    }
    if (units_merc_alive(p->merc_unit)) {
        int hp, max;
        units_merc_health(p->merc_unit, &hp, &max);
        if (hp < max && units_ally_heal(p->merc_unit, max * add >> 8))
            used = 1;
    }
    if (!used)
        return;
    left--;
    units_object_set_state(u, (st & ~0xff) | left);
    obj_well_mode(u, o, left);
    obj_sound(u, o);
    float x, y;
    units_position(u, 1.0f, &x, &y);
    ObjMem *m = obj_mem(units_object_row(u), x, y, 1);
    if (m) {
        m->state = (uint16_t)left;
        m->timer = s_ticks + (uint32_t)o->parm[0] + 1;
    }
}

/* ---- Santuarios ---- */

/* Lo que suman los santuarios que duran a las propiedades del jugador
   (gpShrineTable_6FD28D18): combate, resistencias, habilidad, maná */
static int shrine_props(const Player *p, ItemProp *pr, int n, int cap)
{
    static const struct { int code, stat, arg; } k_props[] = {
        { 7, STAT_DMG_PCT_ALL, 1 }, { 7, STAT_AR_PCT, 0 }, { 8, STAT_RES_FIRE, 0 },     { 9, STAT_RES_COLD, 0 },
        { 10, STAT_RES_LTNG, 0 },   { 11, STAT_RES_POIS, 0 }, { 12, STAT_ALL_SKILLS, 0 }, { 13, STAT_MANA_REGEN, 0 },
    };
    if (p->shrine_ticks <= 0)
        return n;
    for (unsigned k = 0; k < sizeof k_props / sizeof k_props[0] && n < cap; k++)
        if (k_props[k].code == p->shrine_kind) {
            pr[n].stat = (uint16_t)k_props[k].stat;
            pr[n].param = 0;
            pr[n].value = k_props[k].arg ? p->shrine_arg1 : p->shrine_arg0;
            n++;
        }
    return n;
}

/* El de portal: uno al pueblo al lado (D2GAME_SHRINES_Portal) */
static void shrine_portal(Player *p)
{
    tp_open(p);
}

/* El de gema (D2GAME_SHRINES_Gem): la primera gema de la mochila que tiene
   una mejor cae mejorada; si no hay, una mellada al azar */
static void shrine_gem(Player *p)
{
    static const char *const k_chipped[6] = { "gcw", "gcr", "gcg", "gcb", "gcy", "gcv" };
    const ItemType *t = NULL;
    for (int k = 0; k < p->bag_count && !t; k++) {
        const ItemType *g = p->bag[k].type;
        if (g && g->better_gem[0] && items_type_is(g, "gem") && items_find(g->better_gem)) {
            t = items_find(g->better_gem);
            bag_remove(p, k);
        }
    }
    if (!t)
        t = items_find(k_chipped[rnd() % 6]);
    if (t) {
        Item it = items_make(t);
        items_ground_add(&it, p->x + 0.6f, p->y + 0.6f);
    }
}

/* El de fuego (D2GAME_SHRINES_Storm): los de alrededor pierden Arg0% de su
   vida y salen 16 bolas de fuego del jugador (en nivel / 5, de 1 a 8) */
static void shrine_fire(Player *p, int u, const ObjShrine *s)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    float r = s->arg1 < 40 ? (float)s->arg1 : 40.0f; /* (las salas de al lado) */
    for (int i = 0; i < s_num_players; i++) {
        Player *t = &s_pl[i];
        float dx = t->x - x, dy = t->y - y;
        if (t->state < PS_DYING && dx * dx + dy * dy <= r * r && t->hp > 1)
            take_damage(t, t->hp * s->arg0 / 100 > 0 ? t->hp * s->arg0 / 100 : 1, 0);
    }
    int ids[128], n = units_enemies_near(x, y, r, ids, 128);
    for (int k = 0; k < n; k++) {
        int hp, max;
        units_health(ids[k], &hp, &max);
        if (hp * s->arg0 / 100 > 0 && units_damage(ids[k], hp * s->arg0 / 100) > 0)
            drop_loot(ids[k], p);
    }
    int fb = skills_find(2, "Fire Ball"), lvl = p->level_num / 5, lo = 0, hi = 0;
    lvl = lvl < 1 ? 1 : lvl > 8 ? 8 : lvl;
    if (fb >= 0)
        skill_elem(skills_get(2, fb), lvl, &lo, &hi);
    for (int ix = 1; ix < 5; ix++)
        for (int iy = 1; iy < 5; iy++)
            spell_missile(p, "fireball", x, y, (float)(ix & 1 ? 5 * ix : -5 * ix), (float)(iy & 1 ? 5 * iy : -5 * iy),
                          ELEM_FIRE, lo, hi, 0);
}

/* El explosivo y el de veneno: seis estallidos alrededor (a 6) que les
   pegan a los monstruos, con el daño de sus pociones (en nivel / 5) */
static void shrine_burst(Player *p, int u, int elem)
{
    static const int k_dx[6] = { -6, -6, 0, 0, 6, 6 }, k_dy[6] = { 6, -6, 6, -6, 6, -6 };
    const char *pot = elem == ELEM_FIRE ? "explosivepotion" : "chokinggaspoition";
    float x, y;
    units_position(u, 1.0f, &x, &y);
    int lvl = p->level_num / 5, lo = 0, hi = 0;
    lvl = lvl < 1 ? 1 : lvl > 8 ? 8 : lvl;
    if (elem == ELEM_FIRE)
        missiles_level_damage(pot, lvl, &lo, &hi);
    else
        missiles_poison_total(pot, &lo, &hi);
    for (int k = 0; k < 6; k++) {
        float bx = x + (float)k_dx[k], by = y + (float)k_dy[k];
        if (elem == ELEM_FIRE) {
            missiles_effect("fireexplode", bx, by);
            int ids[32], n = units_enemies_near(bx, by, 2.5f, ids, 32);
            for (int m = 0; m < n; m++) {
                int exp = units_damage(ids[m], units_resist(ids[m], ELEM_FIRE, roll(lo, hi)));
                if (exp > 0)
                    award_kill(p, ids[m], exp);
            }
        } else {
            Missile *c = missiles_spawn("poisonexplosioncloud", bx, by, 0.0f, 0.0f);
            if (!c)
                continue;
            c->flags |= MF_EFFECT | MF_CLOUD;
            c->owner = (int)(p - s_pl);
            c->elem = ELEM_POIS;
            c->emin = lo;
            c->emax = hi;
            c->elen = missiles_elen(pot) > 0 ? missiles_elen(pot) : 2 * TICK_RATE;
            c->radius10 = 25;
        }
    }
}

/* El monstruoso (D2GAME_SHRINES_Monster): el monstruo común más cercano
   pasa a ser un jefe con escolta */
static void shrine_monster(Player *p)
{
    int ids[64], n = units_enemies_near(p->x, p->y, 30.0f, ids, 64), best = -1;
    float bd = 1e9f;
    for (int k = 0; k < n; k++) {
        if (units_is_unique(ids[k]) || units_is_champion(ids[k]) || units_ally_owner(ids[k]) >= 0)
            continue;
        float mx, my;
        units_position(ids[k], 1.0f, &mx, &my);
        float d = (mx - p->x) * (mx - p->x) + (my - p->y) * (my - p->y);
        if (d < bd) {
            bd = d;
            best = ids[k];
        }
    }
    if (best < 0)
        return;
    char id[32];
    float mx, my;
    snprintf(id, sizeof id, "%s", units_id(best));
    units_position(best, 1.0f, &mx, &my);
    int z = units_zone(best), lvl = units_level(best);
    units_remove(best);
    units_zone_begin(z);
    units_spawn_boss_pack(id, mx, my, lvl);
    units_zone_end();
}

static void obj_shrine(Player *p, int u, const ObjRow *o)
{
    int id = units_object_state(u) & 0xff;
    id = id > 0 && id < s_nshr ? id : 1;
    const ObjShrine *s = &s_shr[id];
    float x, y;
    units_position(u, 1.0f, &x, &y);
    char key[16];
    snprintf(key, sizeof key, "ShrMsg%d", id);
    show_message(p, str_get(key));
    switch (s->code) {
    case 1: p->hp = p->hp_max; p->mana = p->mana_max; break;
    case 2: p->hp = p->hp_max; break;
    case 3: p->mana = p->mana_max; break;
    case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 15: /* los que duran (uno a la vez) */
        p->shrine_kind = s->code;
        p->shrine_ticks = s->frames;
        p->shrine_arg0 = s->arg0;
        p->shrine_arg1 = s->arg1;
        refresh_stats(p);
        break;
    case 14: /* estamina sin fin */
        p->stamina = p->stamina_max;
        p->stamina_boost_ticks = s->frames;
        break;
    case 17: shrine_portal(p); break;
    case 18: shrine_gem(p); break;
    case 19: shrine_fire(p, u, s); break;
    case 20: shrine_monster(p); break;
    case 21: shrine_burst(p, u, ELEM_FIRE); break;
    case 22: shrine_burst(p, u, ELEM_POIS); break;
    default: break;
    }
    static const char *const k_snd[23] = {
        NULL, "shrine_refill", "shrine_recharge", "shrine_recharge", "shrine_exchange", "shrine_exchange",
        "shrine_armorboost", "shrine_combatboost", "shrine_resistfire", "shrine_resistcold", "shrine_resistlightning",
        "shrine_resistpoison", "shrine_skill", "shrine_recharge", "shrine_refill", "shrine_experience", "shrine_ofenirhs",
        "shrine_portal", "shrine_gemupgrade", "shrine_storm", "shrine_warping", "shrine_exploding", "shrine_poison",
    };
    if (s->code > 0 && s->code < 23 && k_snd[s->code])
        audio_play_name(k_snd[s->code], x, y);
    units_object_mode(u, "op", !o->cyc1 && o->mode2 ? "on" : NULL);
    units_object_set_state(u, units_object_state(u) | OBJ_USED);
    ObjMem *m = obj_mem(units_object_row(u), x, y, 1);
    if (m) {
        m->used = 1;
        m->timer = s->reset > 0 ? s_ticks + (uint32_t)(s->reset * OBJ_MINUTE) + 1 : 0;
    }
}

/* Para D2_OBJ_SHOTS (main.c): cuándo el J1 usó el último y de qué tipo */
static uint32_t s_obj_fx_tick;
static int      s_obj_fx_kind = -1;

int game_debug_obj(int *kind)
{
    *kind = s_obj_fx_kind;
    return s_obj_fx_kind >= 0 ? (int)(s_ticks - s_obj_fx_tick) : -1;
}

/* Usa el objeto (las OperateFn de ObjMode.cpp); 1 si hizo algo */
static int obj_operate(Player *p, int u)
{
    int op = obj_usable(u);
    const ObjRow *o = obj_row(u);
    if (!op || !o)
        return 0;
    if (p == &s_pl[0]) {
        s_obj_fx_tick = s_ticks;
        s_obj_fx_kind = op;
    }
    int st = units_object_state(u), trap = st & OBJ_TRAP_MASK;
    float x, y;
    units_position(u, 1.0f, &x, &y);
    const Level *lv = world_level_at(x, y);
    int mlvl = lv && lv->mon_level > 0 ? lv->mon_level : 1;
    switch (op) {
    case OP_BANK: /* OBJECTS_OperateFunction32_Bank: abre el cofre */
        stash_show(p);
        return 1;
    case OP_CHEST: { /* OBJECTS_OperateFunction04_Chest */
        if (eco_chest_open(p, u)) { /* el cofre del final del Eco del Odio (eco.inc) */
            obj_open(u, p, 0);
            break;
        }
        int drops = 1, spark = (st & OBJ_SPARK) != 0;
        if (st & OBJ_LOCKED) {
            if (p->cls != 5 && !obj_use_key(p)) { /* la Asesina los abre sin llave */
                player_say(p, "needkey_1");
                show_message(p, "Hace falta una llave");
                return 1;
            }
            audio_play_name("item_key_used", p->x, p->y);
            drops = 2;
        }
        int q = spark ? ((int)(rnd() % 100) < 5 ? QUALITY_RARE : QUALITY_MAGIC) : 0, magic = 0;
        if (strcmp(o->cls, "ConsolationChest") == 0) { /* la clase 397: una tirada de la suerte */
            int r = (int)(rnd() % 10000), gold = 0;
            if (r < 1200) { /* dos intentos de único, de conjunto o raro */
                int qq = r < 200 ? QUALITY_UNIQUE : r < 600 ? QUALITY_SET : QUALITY_RARE;
                for (int k = 0; k < 2 && !magic; k++)
                    magic = obj_chest_drop(u, p, qq) == 2;
            } else {
                for (int k = 0; k < 10 && magic < (r < 3200 ? 3 : r < 6200 ? 2 : 1); k++)
                    magic += obj_chest_drop(u, p, QUALITY_MAGIC) == 2;
                gold = r < 3200 ? 0 : r < 6200 ? 7 : 5;
            }
            const ItemType *gt = items_find("gld");
            for (int k = 0; gt && k < gold; k++) {
                Item g = items_make(gt);
                g.amount = 1 + (int)(rnd() % (unsigned)(mlvl * 12 + 8));
                obj_drop_items(u, &g, 1);
            }
        } else if ((int)(rnd() % 100) >= 25 || spark || (st & OBJ_LOCKED)) {
            for (int k = 0; k < drops; k++)
                magic += obj_chest_drop(u, p, q) == 2;
            for (int k = 0; spark && !magic && k < 10; k++)
                magic = obj_chest_drop(u, p, q) == 2;
        }
        obj_open(u, p, trap);
        break;
    }
    case OP_CASKET: /* OBJECTS_OperateFunction01_Casket: se abre cuando suelta algo */
        for (int k = 0; k < 20 && !obj_chest_drop(u, p, 0); k++) {
        }
        if (rnd() % 10000 >= 8192)
            obj_spawn_monsters(u, 1);
        obj_open(u, p, trap);
        break;
    case OP_URN: /* OBJECTS_OperateFunction03_Urn_Basket_Jar: 21% */
        if (rnd() % 100 <= 20)
            obj_chest_drop(u, p, 0);
        obj_open(u, p, trap);
        break;
    case OP_CORPSE: /* OBJECTS_OperateFunction14_Corpse */
        obj_chest_drop(u, p, 0);
        obj_open(u, p, trap);
        break;
    case OP_WIRT: { /* OBJECTS_OperateFunction33_WirtsBody: la pierna; a los 10 cuadros, el oro (cows.inc) */
        const ItemType *leg = items_find("leg");
        if (leg) {
            Item it = items_make(leg);
            obj_drop_items(u, &it, 1);
        }
        obj_open(u, p, 0);
        obj_event(OE_WIRT, 10, u, p, 0);
        break;
    }
    case OP_STASH: /* OBJECTS_OperateFunction51_JungleStash: suelta a los Parm1 cuadros */
        obj_open(u, p, trap);
        obj_event(OE_STASH, o->parm[1] + 1, u, p, 0);
        break;
    case OP_BARREL: /* OBJECTS_OperateFunction05_Barrel: lo patea; 18% monstruo, 21% algo */
        if (p->state == PS_NORMAL)
            set_player_state(p, PS_HIT, player_anim(p, MODE_KK)->ok ? MODE_KK : MODE_A1);
        if (rnd() % 10000 >= 8192)
            obj_spawn_monsters(u, 1);
        if (rnd() % 100 <= 20)
            obj_chest_drop(u, p, 0);
        obj_open(u, p, 0);
        break;
    case OP_XBARREL:
        if (p->state == PS_NORMAL)
            set_player_state(p, PS_HIT, player_anim(p, MODE_KK)->ok ? MODE_KK : MODE_A1);
        obj_explode(u, p, 0);
        break;
    case OP_ARMOR: case OP_WEAPON: { /* D2GAME_DropArmor / DropWeapon: superior, del nivel de la zona menos uno */
        int il = mlvl > 1 ? mlvl - 1 : 1;
        char code[20];
        snprintf(code, sizeof code, "%s%d", op == OP_ARMOR ? "armo" : "weap", (il + 2) / 3 * 3);
        obj_drop_code(u, code, il, QUALITY_SUPERIOR);
        obj_open(u, p, 0);
        break;
    }
    case OP_BOOKS: { /* OBJECTS_OperateFunction26_BookShelf: 65% pergamino, si no tomo (acá no hay tomo: una
                        pila de 5 pergaminos, las cargas con que sale) */
        int tome = rnd() % 20 > 12;
        Item out[2];
        int n = items_roll_drop_quality("tsc", mlvl, 0, 0, out, 1);
        if (n > 0 && tome)
            out[0].amount = 5;
        obj_drop_items(u, out, n);
        obj_open(u, p, 0);
        break;
    }
    case OP_XCHEST: /* OBJECTS_OperateFunction30_ExplodingChest: le pega al que lo abre */
        obj_hurt_player(p, o->dmg, ELEM_NONE);
        obj_hurt_player(p, o->dmg, ELEM_FIRE);
        obj_open(u, p, 0);
        audio_play_name("object_barrel_explode", x, y);
        break;
    case OP_SOUL: /* OBJECTS_OperateFunction48_TrappedSoul: 1 de 10 se prende (sub_6FC750D0: a los 20 cuadros
                     quema a los de al lado y a los 25 suelta); si no, suelta y se apaga */
        if (rnd() % 100 >= 90) {
            units_object_mode(u, "op", "on");
            units_object_set_state(u, st | OBJ_USED | OBJ_SOUL_BURNT);
            obj_remember(u);
            obj_event(OE_SOUL, 20, u, p, 0);
            audio_play_name("object_fire_loop_hellbrazier", x, y);
        } else if (obj_chest_drop(u, p, 0)) {
            units_object_mode(u, "s3", NULL);
            units_object_set_state(u, st | OBJ_USED);
            obj_remember(u);
            obj_sound(u, o);
        }
        break;
    case OP_WELL:
        obj_well(p, u, o);
        break;
    case OP_SHRINE:
        obj_shrine(p, u, o);
        break;
    case OP_TELEPAD: { /* OBJECTS_OperateFunction27_TeleportPad */
        float tx, ty;
        if (!obj_pad_landing(obj_pad_partner(u, NULL, 0), &tx, &ty))
            return 0;
        p->x = p->prev_x = tx;
        p->y = p->prev_y = ty;
        break;
    }
    default:
        return 0;
    }
    return 1;
}

/* ---- Cada tick: las trampas, el fuego, los santuarios que vuelven y los pozos que se llenan ---- */

static int obj_find_mem_unit(const ObjMem *m)
{
    for (int u = 0; u < units_count(); u++) {
        if (units_object_row(u) != m->row)
            continue;
        float x, y;
        units_position(u, 1.0f, &x, &y);
        if ((int16_t)lroundf(x * 2.0f) == m->x2 && (int16_t)lroundf(y * 2.0f) == m->y2)
            return u;
    }
    return -1;
}

static void obj_selftest(void);

static void obj_tick(void)
{
    static int test = -1;
    if (test < 0)
        test = getenv("D2_OBJ_TEST") && atoi(getenv("D2_OBJ_TEST")) >= 2;
    if (test)
        obj_selftest();
    for (int i = 0; i < OBJ_EVENTS; i++) {
        ObjEvent *e = &s_oev[i];
        if (e->kind == OE_NONE || --e->ticks > 0)
            continue;
        if (e->kind == OE_TRAP) {
            obj_trap_fire(e);
            e->kind = OE_NONE;
        } else if (e->kind == OE_STASH) {
            if (units_object_row(e->unit) == e->row)
                obj_chest_drop(e->unit, &s_pl[e->player < s_num_players ? e->player : 0], 0);
            e->kind = OE_NONE;
        } else if (e->kind == OE_WIRT) { /* el caso OBJECT_WIRTSBODY de los QUESTFN: 10 a 19 pilas de oro, de a una cada 10 cuadros */
            const ItemType *gt = items_find("gld");
            if (e->arg == 0)
                e->arg = (int)(rnd() % 10) + 10;
            if (units_object_row(e->unit) == e->row && gt) {
                Item g = items_make(gt);
                g.amount = 1 + (int)(rnd() % (unsigned)(e->level * 12 + 8));
                obj_drop_items(e->unit, &g, 1);
            }
            e->ticks = 10;
            if (--e->arg <= 0 || units_object_row(e->unit) != e->row)
                e->kind = OE_NONE;
        } else if (e->kind == OE_SOUL) { /* sub_6FC750D0: abriéndose espera; prendido quema (a Parm0 + 1) y suelta */
            const ObjRow *o = units_object_row(e->unit) == e->row ? obj_row(e->unit) : NULL;
            if (!o) {
                e->kind = OE_NONE;
            } else if (units_object_cur_mode(e->unit) == 1) {
                e->ticks = 10;
            } else if (e->arg == 0) {
                for (int k = 0; k < s_num_players; k++) {
                    float dx = s_pl[k].x - e->x, dy = s_pl[k].y - e->y, reach = (float)(o->parm[0] + 1);
                    if (dx * dx + dy * dy <= reach * reach)
                        obj_hurt_player(&s_pl[k], o->dmg, ELEM_FIRE);
                }
                e->arg = 1;
                e->ticks = 25;
            } else {
                if (obj_chest_drop(e->unit, &s_pl[e->player < s_num_players ? e->player : 0], 0))
                    units_object_mode(e->unit, "s1", "s2");
                e->kind = OE_NONE;
            }
        } else if (e->kind == OE_FIRE) { /* sub_6FC74D10: quema a los que están a Parm0 + 1 cada 15 a 50 cuadros */
            int alive = units_object_row(e->unit) == e->row;
            for (int k = 0; alive && k < s_num_players; k++) {
                float dx = s_pl[k].x - e->x, dy = s_pl[k].y - e->y;
                if (dx * dx + dy * dy <= (float)(e->arg * e->arg))
                    obj_hurt_player(&s_pl[k], 100, ELEM_FIRE);
            }
            int wait = 15 + (int)(rnd() % 35);
            e->life -= wait;
            e->ticks = wait;
            if (e->life <= 0 || !alive) {
                if (alive)
                    units_remove(e->unit);
                e->kind = OE_NONE;
            }
        }
    }
    if (s_ticks % TICK_RATE)
        return;
    for (int i = 0; i < s_nomem; i++) {
        ObjMem *m = &s_omem[i];
        if (!m->timer || m->act != s_act || (int32_t)(s_ticks - m->timer) < 0)
            continue;
        m->timer = 0;
        int u = obj_find_mem_unit(m);
        const ObjRow *o = m->row < s_orows ? &s_orow[m->row] : NULL;
        if (!o)
            continue;
        if (o->op == OP_SHRINE) { /* sub_6FC74B00: vuelve a andar */
            m->used = 0;
            if (u >= 0) {
                units_object_set_state(u, units_object_state(u) & ~OBJ_USED);
                units_object_mode(u, "nu", NULL);
            }
        } else if (o->op == OP_WELL) { /* sub_6FC74B40: una carga más */
            int max = 2 * (o->parm[2] > 0 ? o->parm[2] : 1);
            m->state = (uint16_t)((m->state & 0xff) < max ? (m->state & 0xff) + 1 : max);
            if (u >= 0) {
                units_object_set_state(u, (units_object_state(u) & ~0xff) | (m->state & 0xff));
                obj_well_mode(u, o, m->state & 0xff);
            }
        }
    }
}

/* Partida nueva: nada se usó todavía */
static void obj_reset(void)
{
    s_nomem = 0;
    memset(s_omem_hash, 0, sizeof s_omem_hash);
    memset(s_oev, 0, sizeof s_oev);
    for (int i = 0; i < MAX_PLAYERS; i++)
        s_pl[i].shrine_ticks = s_pl[i].shrine_kind = 0;
}

/* ---- Con A y el cartel ---- */

/* El objeto que A usaría (o -1) y su cartel ("A: Arcón"); NULL si nada */
static const char *obj_action(const Player *p, int *unit)
{
    static char text[96];
    int u = obj_near(p->x, p->y, OBJ_REACH);
    *unit = u;
    if (u < 0)
        return NULL;
    const ObjRow *o = obj_row(u);
    int st = units_object_state(u);
    const char *name = str_get(o->name);
    char key[16];
    if (o->op == OP_SHRINE) {
        snprintf(key, sizeof key, "ShrId%d", (st & 0xff) > 0 ? st & 0xff : 1);
        name = str_exists(key) ? str_get(key) : str_get("Shrine");
    }
    if (o->op == OP_CHEST && (st & OBJ_LOCKED)) {
        name = str_exists("lockedchest") ? str_get("lockedchest") : name;
        if (p->cls != 5 && !obj_has_key(p)) {
            snprintf(text, sizeof text, tr("%s: hace falta una llave"), name);
            return text;
        }
        if (p->cls != 5) {
            snprintf(text, sizeof text, tr("A: %s (gasta una llave)"), name);
            return text;
        }
    }
    snprintf(text, sizeof text, tr("A: %s"), name);
    return text;
}

/* A al lado de uno: 1 si lo usó */
static int obj_interact(Player *p)
{
    int u;
    if (!obj_action(p, &u))
        return 0;
    return obj_operate(p, u);
}

/* Con A: ¿el objeto antes que el salto de al lado? (el más cerca, o si el salto está cerrado) */
static int obj_first(const Player *p)
{
    int u = obj_near(p->x, p->y, OBJ_REACH);
    const Warp *w = portal_near(p);
    if (u < 0)
        return 0;
    if (!w || warp_closed(w))
        return 1;
    float wx = (w->x0 + w->x1) / 2, wy = (w->y0 + w->y1) / 2;
    return obj_dist2(u, p->x, p->y) <= (wx - p->x) * (wx - p->x) + (wy - p->y) * (wy - p->y);
}

/* Para los bots: el cofre, ataúd, urna, barril o cadáver más cercano que
   vale la pena abrir (no los santuarios, los pozos ni lo que explota; los
   cerrados, si tiene llave), a menos de `range`; -1 = ninguno. Los que
   dejó (no llegaba) no cuentan. */
static float s_obj_skip[16][2];
static int   s_obj_nskip;

static void obj_bot_skip(int u)
{
    float x, y;
    units_position(u, 1.0f, &x, &y);
    s_obj_skip[s_obj_nskip % 16][0] = x;
    s_obj_skip[s_obj_nskip % 16][1] = y;
    s_obj_nskip++;
}

static int obj_bot_target(const Player *p, float range)
{
    int best = -1;
    float bd = range * range;
    for (int u = 0; u < units_count(); u++) {
        int op = obj_usable(u);
        if (!op || op == OP_SHRINE || op == OP_WELL || op == OP_XBARREL || op == OP_XCHEST || op == OP_TELEPAD)
            continue;
        if (op == OP_CHEST && (units_object_state(u) & OBJ_LOCKED) && p->cls != 5 && !obj_has_key(p))
            continue;
        float d = obj_dist2(u, p->x, p->y), x, y;
        if (d >= bd || zone_at(p->x, p->y) != units_zone(u))
            continue;
        units_position(u, 1.0f, &x, &y);
        int skipped = 0;
        for (int k = 0; k < 16 && k < s_obj_nskip; k++)
            skipped |= fabsf(s_obj_skip[k][0] - x) < 0.1f && fabsf(s_obj_skip[k][1] - y) < 0.1f;
        if (!skipped) {
            bd = d;
            best = u;
        }
    }
    return best;
}

/* Prueba sin gráficos (D2_OBJ_TEST=1: lo que hay en cada zona armada; =2
   además, en la zona del J1, usa cada uno y revisa: el cerrado sin llave no
   se abre y con llave la gasta, lo que se usó ya no se usa, y al volver a
   armar la zona siguen igual) */
static void zone_unload(int z);
static int  zone_load(int z);

/* D2_OBJ_TEST=3 (con D2_OBJ_SHOTS): uno de cada clase de la zona, de a uno:
   el J1 al lado (la captura con el cartel) y a los 30 ticks lo abre */
static int obj_gallery(Player *p, int z)
{
    static int zone = -1, next, list[64], n, step;
    static uint32_t at;
    static float gx, gy;
    if (next < n && (next > 0 || step == 1)) { /* quieto al lado (el bot lo movería): también mientras se abre */
        p->x = p->prev_x = gx;
        p->y = p->prev_y = gy;
    }
    if (z != zone) {
        zone = z;
        n = next = step = 0;
        at = s_ticks + 50;
        for (int u = 0; u < units_count() && n < 64; u++) {
            if (units_zone(u) != z || !obj_usable(u))
                continue;
            int dup = 0;
            for (int k = 0; k < n; k++)
                dup |= units_object_row(list[k]) == units_object_row(u);
            if (!dup)
                list[n++] = u;
        }
    }
    if (next >= n)
        return 0;
    if ((int32_t)(s_ticks - at) < 0)
        return 1;
    int u = list[next];
    if (step == 0) {
        const ObjRow *o = obj_row(u);
        float ux, uy;
        units_position(u, 1.0f, &ux, &uy);
        p->x = p->prev_x = gx = ux;
        p->y = p->prev_y = gy = uy + (float)(o ? o->sy / 2 : 0) + 1.0f;
        if ((units_object_state(u) & OBJ_LOCKED) && bag_index_of(p, "key") < 0) {
            Item key = items_make(items_find("key"));
            key.amount = 1;
            bag_add(p, &key);
        }
        s_obj_fx_tick = s_ticks;
        s_obj_fx_kind = 0;
        step = 1;
        at = s_ticks + 30;
    } else {
        p->state = PS_NORMAL;
        int act;
        if (obj_action(p, &act) && act == u)
            obj_interact(p);
        printf("objetos: galería %s (%d de %d)\n", units_id(u), next + 1, n);
        step = 0;
        next++;
        at = s_ticks + 60;
    }
    return 1;
}

static void obj_selftest(void)
{
    static uint64_t done;
    Player *p = &s_pl[0];
    int z = zone_at(p->x, p->y);
    if (atoi(getenv("D2_OBJ_TEST")) >= 3) {
        if (z >= 0 && !world_zone_is_town(z) && obj_gallery(p, z))
            p->state = p->state == PS_HIT ? PS_HIT : PS_NORMAL;
        return;
    }
    if (z < 0 || z >= 64 || ((done >> z) & 1) || world_zone_is_town(z) || p->state >= PS_DYING)
        return;
    done |= 1ull << z;
    static int list[2048];
    int n = 0, bad = 0, opened = 0, locked = 0, keyless = 0, drops = 0;
    for (int u = 0; u < units_count() && n < 2048; u++)
        if (units_zone(u) == z && obj_usable(u) && obj_row(u)->op != OP_SHRINE && obj_row(u)->op != OP_WELL &&
            obj_row(u)->op != OP_XBARREL && obj_row(u)->op != OP_TELEPAD)
            list[n++] = u;
    float px = p->x, py = p->y;
    int hp = p->hp;
    for (int k = 0; k < n; k++) {
        int u = list[k];
        if (!obj_usable(u))
            continue;
        const ObjRow *o = obj_row(u);
        float ux, uy;
        units_position(u, 1.0f, &ux, &uy);
        p->x = ux;
        p->y = uy + (float)(o->sy / 2) + 1.0f;
        int before = 0;
        for (int g = 0; g < items_ground_count(); g++)
            before += items_ground_item(g, NULL, NULL) != NULL;
        int st = units_object_state(u), act;
        if (!obj_action(p, &act) || act != u) { /* lo de al lado no es este: se salta */
            continue;
        }
        if ((st & OBJ_LOCKED) && o->op == OP_CHEST) {
            locked++;
            while (bag_index_of(p, "key") >= 0)
                bag_remove(p, bag_index_of(p, "key"));
            obj_interact(p);
            if (p->cls != 5 && !obj_usable(u)) {
                printf("objetos MAL: %s cerrado se abrió sin llave\n", o->cls);
                bad++;
            }
            keyless += obj_usable(u) != 0;
            Item key = items_make(items_find("key"));
            key.amount = 1;
            bag_add(p, &key);
        }
        p->state = PS_NORMAL;
        obj_interact(p);
        for (int k = 0; k < 20 && o->op == OP_SOUL && obj_usable(u); k++) /* (si no soltó nada, se puede de nuevo) */
            obj_interact(p);
        p->state = PS_NORMAL;
        if (obj_usable(u)) {
            printf("objetos MAL: %s sigue sin usar\n", o->cls);
            bad++;
        }
        if ((st & OBJ_LOCKED) && o->op == OP_CHEST && p->cls != 5 && bag_index_of(p, "key") >= 0) {
            printf("objetos MAL: %s no gastó la llave\n", o->cls);
            bad++;
        }
        int after = 0;
        for (int g = 0; g < items_ground_count(); g++)
            after += items_ground_item(g, NULL, NULL) != NULL;
        drops += after - before;
        opened++;
    }
    int left = 0;
    for (int u = 0; u < units_count(); u++)
        left += units_zone(u) == z && obj_usable(u) != 0;
    p->x = px;
    p->y = py;
    p->hp = hp > 0 ? hp : 1;
    /* armar la zona de nuevo: lo abierto sigue abierto */
    zone_unload(z);
    zone_load(z);
    int again = 0;
    for (int u = 0; u < units_count(); u++)
        again += units_zone(u) == z && obj_usable(u) != 0;
    if (again != left) {
        printf("objetos MAL: al armar %s de nuevo hay %d para usar (antes %d)\n", world_zone_name(z), again, left);
        bad++;
    }
    printf("objetos %s: %s: %d usados (%d cerrados, %d no se abrieron sin llave), %d cosas en el piso, %d quedan\n",
           bad ? "MAL" : "bien", world_zone_name(z), opened, locked, keyless, drops, left);
}

static void obj_report(int z)
{
    int counts[OBJ_ROWS] = { 0 }, total = 0, locked = 0, traps = 0;
    for (int u = 0; u < units_count(); u++)
        if (units_zone(u) == z && obj_usable(u)) {
            counts[units_object_row(u)]++;
            total++;
            locked += (units_object_state(u) & OBJ_LOCKED) != 0;
            traps += (units_object_state(u) & OBJ_TRAP_MASK) != 0 && obj_row(u)->op != OP_SHRINE && obj_row(u)->op != OP_WELL;
        }
    printf("objetos de %s: %d para usar (%d cerrados, %d con trampa; %d unidades, %d sin lugar):", world_zone_name(z), total,
           locked, traps, units_count(), s_obj_lost);
    for (int r = 1; r < s_orows; r++)
        if (counts[r])
            printf(" %s %d", s_orow[r].cls, counts[r]);
    printf("\n");
}
