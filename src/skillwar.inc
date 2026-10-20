/* ---- Las habilidades del Brujo (se incluye desde game.c) ----

   El Brujo es de "Reign of the Warlock" (D2R) y no está en D2MOO: lo que
   hace cada una sale de skills.txt y missiles.txt de D2R (sus Param, con
   las descripciones de cada columna, y sus fórmulas) y de los textos del
   juego:
   - Demonios: el hombre cabra (cuerpo a cuerpo), el mancillado (bolas de
     fuego) y el profanador (une las almas de los enemigos de alrededor: lo
     que recibe uno lo reciben los demás). Entre los tres, hasta 1, 2 o 3
     según el Dominio demoníaco, que también les da velocidad, puntería y
     daño. Juramento de sangre: parte del daño que recibe el Brujo va a su
     demonio, que resiste más. Marca de la muerte lleva los demonios al lado
     del enemigo y le baja la defensa; Atiborrarse cura a uno con un
     cadáver; Sangre hirviente los hace estallar (pierden vida); Consumir
     sacrifica uno a cambio de vida máxima y velocidad; Vincular demonio se
     queda con un demonio enemigo herido (más fácil cuanto más herido).
   - Maleficios del arma (uno a la vez, por ln34 cuadros): Flagelo (daño
     mágico; al golpeado le baja la defensa y la puntería), Extracción (vida
     y maná por muerte; el golpeado hace un tercio menos de daño) y Purga
     (velocidad de ataque; el golpeado puede estallar con daño mágico hasta
     calc3 veces). Valen también para los proyectiles del Brujo.
     Explosión sobrenatural: un rato, cada 30 cuadros daña a los embrujados
     de alrededor, roba vida y maná y les renueva el maleficio. Resguardo
     psíquico absorbe daño y aturde a los que pegan de cerca. Dominio de la
     levitación (con un arma): daño, puntería y golpe crítico. Golpe de eco
     y Hojas reflejadas tiran copias del arma; Hoja de distorsión tira una
     que explota y lleva al Brujo hasta ahí; Tajo pega a todos en un arco.
   - Caos: Descarga de miasma y Cadena de miasma (con nubes que dañan por
     segundo), Anillo de fuego (llamas alrededor que empujan), Ola de llamas
     (un frente de fuego que deja el piso ardiendo), los sigilos (letargo:
     frena y baja la defensa; rencor: los de adentro se pelean entre ellos;
     muerte: mata a los heridos y los hace estallar), Hecatombe (fuego en
     todo alrededor, bajando la resistencia) y Abismo (atrae a los enemigos
     y estalla). Entropía mejorada potencia la miasma y el Abismo.
   Las distancias en "yardas" de D2R se pasan a subtiles con WAR_YARD. */

enum { PK_GOATMAN = 20, PK_TAINTED, PK_DEFILER };
#define WAR_YARD    0.75f /* una yarda de las descripciones, en subtiles */
#define WAR_SIGILS  12
#define WAR_PURGES  48
#define WAR_BLAST   30    /* Explosión sobrenatural: cada cuántos cuadros */
#define WAR_BIND_START 9  /* Vincular demonio: el maná al empezar (startmana); después, por cuadro */

typedef struct {
    int   used, owner, skill, lvl, kind, ticks, next;
    float x, y, r;
} WarSigil;
static WarSigil s_sigils[WAR_SIGILS];

/* Los embrujados con la Purga: cuántas veces más pueden estallar */
typedef struct {
    int unit, owner, charges, ticks;
} WarPurge;
static WarPurge s_purges[WAR_PURGES];

static int war_kind(int kind)
{
    return kind >= SK_SUMMON_GOATMAN && kind <= SK_ABYSS;
}

/* Los puntos puestos y el nivel con todo de una habilidad del Brujo, por su nombre en skills.txt */
static int war_blvl(const Player *p, const char *key)
{
    int i = skills_find(p->cls, key);
    return i >= 0 ? p->skill_lvl[i] : 0;
}

static int war_lvl(const Player *p, const char *key, const SkillDef **s)
{
    int i = skills_find(p->cls, key);
    if (s)
        *s = i >= 0 ? skills_get(p->cls, i) : NULL;
    return i >= 0 ? eff_lvl(p, i) : 0;
}

static void war_damage(Player *p, int unit, int elem, int amount)
{
    if (!units_alive_enemy(unit) || amount <= 0)
        return;
    int exp = units_damage(unit, s_one_hit ? 1 << 20 : elem == ELEM_NONE ? units_resist(unit, ELEM_NONE, amount)
                                                                           : spell_resist((int)(p - s_pl), unit, elem, amount));
    if (exp > 0)
        award_kill(p, unit, exp);
}

/* Daña a todos los de alrededor (físico y de un elemento) */
static int war_area(Player *p, float x, float y, float r, int elem, int lo, int hi, int plo, int phi)
{
    int ids[32], n = units_enemies_near(x, y, r, ids, 32);
    for (int k = 0; k < n; k++) {
        int unit = ids[k];
        if (phi > 0)
            war_damage(p, unit, ELEM_NONE, roll(plo, phi));
        if (hi > 0)
            war_damage(p, unit, elem, roll(lo, hi));
    }
    return n;
}

/* ---- Los demonios ---- */

static int war_is_demon_kind(int kind)
{
    return kind == PK_GOATMAN || kind == PK_TAINTED || kind == PK_DEFILER;
}

/* Los demonios vivos (los invocados y el vinculado) */
static int war_demons(const Player *p, int *out, int max)
{
    int n = 0;
    for (int k = 0; k < NEC_PETS && n < max; k++)
        if (war_is_demon_kind(p->pet_kind[k]) && units_pet_alive(p->pets[k]))
            out[n++] = p->pets[k];
    if (p->bind_unit >= 0 && units_converted(p->bind_unit) && units_pet_alive(p->bind_unit) && n < max)
        out[n++] = p->bind_unit;
    return n;
}

/* El demonio más cercano a (x, y), -1 si no tiene */
static int war_demon_near(const Player *p, float x, float y)
{
    int ids[8], n = war_demons(p, ids, 8), best = -1;
    float bd = 1e9f;
    for (int k = 0; k < n; k++) {
        float ux, uy;
        units_position(ids[k], 1.0f, &ux, &uy);
        float d = (ux - x) * (ux - x) + (uy - y) * (uy - y);
        if (d < bd) {
            bd = d;
            best = ids[k];
        }
    }
    return best;
}

/* El fuego de la bola del mancillado (Tainted Fire Ball: 10-14, +4-6 por nivel hasta el 8 y +8-10 después; HitShift 7) */
static void war_tainted_fire(int lvl, int *lo, int *hi)
{
    int a = 10, b = 14;
    for (int l = 2; l <= lvl; l++) {
        a += l <= 8 ? 4 : 8;
        b += l <= 8 ? 6 : 10;
    }
    *lo = (a << 7) >> 8;
    *hi = (b << 7) >> 8;
}

static int war_summon(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl);
    int kind = s->kind == SK_SUMMON_GOATMAN ? PK_GOATMAN : s->kind == SK_SUMMON_TAINTED ? PK_TAINTED : PK_DEFILER;
    const SkillDef *dms, *bos;
    int dm = war_lvl(p, "Demonic Mastery", &dms), bo = war_lvl(p, "Blood Oath", &bos);
    PetSpec ps;
    memset(&ps, 0, sizeof ps);
    ps.level = p->level_num;
    ps.hp_pct = skill_calc(s, SC_CALC1, lvl, &c); /* 35% por nivel y lo del Juramento de sangre */
    if (dm > 0) { /* Dominio demoníaco: velocidad, puntería, daño y velocidad de ataque (min(ln12, 25)) */
        ps.speed_pct = skill_dm(dms, 9, 10, dm);
        ps.ar_add = skill_ln(dms, 5, 6, dm);
        ps.dmg_pct = skill_ln(dms, 11, 12, dm);
        int ias = skill_ln(dms, 1, 2, dm);
        ps.ias = ias < 25 ? ias : 25;
    }
    if (bo > 0) { /* Juramento de sangre: resistencias y menos daño físico (dm91) */
        ps.res = skill_dm(bos, 7, 8, bo);
        ps.pdr = skill_dm(bos, 9, 10, bo);
    }
    if (s->kind != SK_SUMMON_DEFILER) { /* los dos primeros: también la suya (min(ln21, 25)) */
        int ias = skill_ln(s, 11, 12, lvl);
        ps.ias += ias < 25 ? ias : 25;
    }
    switch (s->kind) {
    case SK_SUMMON_GOATMAN: /* daño fijo, % de daño, puntería y defensa por nivel */
        ps.dmg_add = skill_ln(s, 3, 4, lvl);
        ps.dmg_pct += skill_ln(s, 5, 6, lvl);
        ps.ar_add += skill_ln(s, 7, 8, lvl);
        ps.def_add = skill_ln(s, 9, 10, lvl);
        ps.war_goat = lvl; /* sus habilidades (sumskill): aturdir, furia, frenesí y tajo */
        break;
    case SK_SUMMON_TAINTED: { /* tira bolas de fuego (con el Dominio demoníaco como dominio del fuego) */
        snprintf(ps.missile, sizeof ps.missile, "taintedfireball");
        ps.elem = ELEM_FIRE;
        war_tainted_fire(lvl, &ps.emin, &ps.emax);
        /* y Sangre hirviente: su bola hace par8 (20) % más por punto (Tainted Fire Ball EDmgSymPerCalc) */
        int pct = (dm > 0 ? skill_ln(dms, 11, 12, dm) : 0) + war_blvl(p, "Blood Boil") * 20;
        ps.emin = ps.emin * (100 + pct) / 100;
        ps.emax = ps.emax * (100 + pct) / 100;
        ps.def_add = skill_ln(s, 7, 8, lvl);
        break;
    }
    default: /* el profanador */
        ps.dmg_pct += s->param[2] * (lvl - 1 + dm);
        ps.def_add = skill_ln(s, 7, 8, lvl);
        break;
    }
    /* Entre los tres, hasta petmax (1, 2 o 3 según el Dominio demoníaco): se va el más viejo */
    int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, &c) : 1, alive = 0, oldest = -1, slot = -1;
    for (int k = 0; k < NEC_PETS; k++) {
        if (war_is_demon_kind(p->pet_kind[k]) && !units_pet_alive(p->pets[k]))
            nec_pet_drop(p, k);
        if (p->pet_kind[k] == PK_NONE) {
            if (slot < 0)
                slot = k;
            continue;
        }
        if (!war_is_demon_kind(p->pet_kind[k]))
            continue;
        alive++;
        if (oldest < 0 || p->pet_born[k] < p->pet_born[oldest])
            oldest = k;
    }
    if (alive >= (most > 0 ? most : 1) && oldest >= 0) {
        nec_pet_drop(p, oldest);
        slot = oldest;
    }
    if (slot < 0)
        return 0;
    float x = p->x + 1.0f, y = p->y + 1.0f;
    find_free_spot(&x, &y);
    int u = units_pet_summon(me, s->summon, x, y, &ps);
    if (u < 0)
        return 0;
    nec_pet_keep(p, slot, u, kind);
    overlays_add("warlockdemonground", OV_AT_UNIT, u, x, y, 0);
    return 1;
}

/* El profanador une las almas de los de alrededor (Health Link: calc1 % del
   daño compartido, hasta calc2 almas, con el nivel de la habilidad) */
static void war_defilers_tick(Player *p)
{
    const SkillDef *s;
    int lvl = war_lvl(p, "Summon Defiler", &s);
    if (lvl <= 0 || s_ticks % TICK_RATE != 0)
        return;
    int pct = 110 * lvl * (60 - 20) / (100 * (lvl + 6)) + 20, souls = 110 * lvl * (6 - 2) / (100 * (lvl + 6)) + 2;
    for (int k = 0; k < NEC_PETS; k++) {
        if (p->pet_kind[k] != PK_DEFILER || !units_pet_alive(p->pets[k]))
            continue;
        float x, y;
        units_position(p->pets[k], 1.0f, &x, &y);
        int ids[16], n = units_enemies_near(x, y, 6.0f, ids, 16);
        for (int j = 0; j < n && j < souls; j++) {
            if (!units_linked(ids[j])) {
                float ux, uy;
                units_position(ids[j], 1.0f, &ux, &uy);
                overlays_add("curse_hit", OV_AT_UNIT, ids[j], ux, uy, 0);
            }
            units_link(ids[j], 1000 + p->pets[k], pct, (int)(p - s_pl), TICK_RATE + TICK_RATE / 2);
        }
    }
}

/* El aura del vinculado desde el nivel 20 (un jefe con Aura Enchanted): su nivel */
static int war_bind_aura_lvl(const Player *c)
{
    int l = war_lvl(c, "Bind Demon", NULL) / 3;
    return l > 1 ? l : 1;
}

/* Lo que dan las auras de los demonios (en pal_auras_tick, como los
   espíritus del Druida): el mancillado tiene Resistir el fuego (Tainted
   Resist Fire: radio ln12 y dm34 %, con el nivel de su invocación hasta el
   30) y el vinculado, desde el nivel 20, un aura del Paladín para todos */
static void war_aura_give(Player *o)
{
    for (int ci = 0; ci < s_num_players; ci++) {
        const Player *c = &s_pl[ci];
        if (c->cls != 7 || c->state >= PS_DYING)
            continue;
        int tl = war_lvl(c, "Summon Tainted", NULL), best = 0;
        tl = tl > 30 ? 30 : tl;
        for (int k = 0; k < NEC_PETS && tl > 0; k++) {
            if (c->pet_kind[k] != PK_TAINTED || !units_pet_alive(c->pets[k]))
                continue;
            float x, y;
            units_position(c->pets[k], 1.0f, &x, &y);
            float r = (float)(16 + 2 * (tl - 1)), dx = o->x - x, dy = o->y - y;
            int v = 15 + (70 - 15) * (110 * tl / (tl + 6)) / 100;
            if (dx * dx + dy * dy <= r * r && v > best)
                best = v;
        }
        o->au_res[0] += best;
        if (c->bind_aura >= 0 && c->bind_unit >= 0 && units_converted(c->bind_unit) && units_pet_alive(c->bind_unit)) {
            const SkillDef *s = skills_get(3, c->bind_aura);
            int lvl = war_bind_aura_lvl(c);
            SkillCtx x = { 3, NULL, c->level_num };
            float ux, uy;
            units_position(c->bind_unit, 1.0f, &ux, &uy);
            float r = (float)skill_calc(s, SC_AURARANGE, lvl, &x), dx = o->x - ux, dy = o->y - uy;
            if (dx * dx + dy * dy <= r * r)
                for (int k = 0; k < 3; k++)
                    if (s->aurastat[k][0])
                        aura_stat(o, s, s->aurastat[k], skill_calc(s, SC_AURASTAT1 + k, lvl, &x));
        }
    }
}

/* ---- Los maleficios ---- */

static int war_purge_find(int unit)
{
    for (int k = 0; k < WAR_PURGES; k++)
        if (s_purges[k].ticks > 0 && s_purges[k].unit == unit)
            return k;
    return -1;
}

/* ¿Está embrujado? (Flagelo baja la defensa, Extracción debilita, Purga lo marca) */
static int war_hexed(int unit)
{
    return units_cursed(unit) || units_nec_cursed(unit) == NC_WEAKEN || war_purge_find(unit) >= 0;
}

/* La Purga: el golpeado estalla (daño mágico alrededor) */
static void war_erupt(Player *p, const SkillDef *s, int lvl, int unit)
{
    SkillCtx c = pctx(p);
    float ux, uy;
    units_position(unit, 1.0f, &ux, &uy);
    int lo, hi;
    skill_elem_syn(s, lvl, &c, &lo, &hi);
    float r = (float)(s->param[7] + lvl * s->param[8]) / 100.0f * WAR_YARD;
    missiles_effect("hexpurgeexplode", ux, uy);
    overlays_add("hex_purge_explosion", OV_AT_UNIT, unit, ux, uy, 0);
    audio_play_name("Warlock_Skill_Hex_Purge_Hit", ux, uy);
    war_area(p, ux, uy, r > 1.5f ? r : 1.5f, ELEM_MAG, lo, hi, 0, 0);
}

/* Pone (o renueva) el maleficio del arma en el golpeado */
static void war_hex_apply(Player *p, int unit, int refresh_only)
{
    if (p->hex_ticks <= 0 || p->hex_skill < 0 || !units_alive_enemy(unit))
        return;
    const SkillDef *s = skills_get(p->cls, p->hex_skill), *eb;
    int lvl = eff_lvl(p, p->hex_skill), ebl = war_blvl(p, "Eldritch Blast");
    (void)war_lvl(p, "Eldritch Blast", &eb);
    float ux, uy;
    units_position(unit, 1.0f, &ux, &uy);
    /* calc1: 150 cuadros y lo que suma la Explosión sobrenatural */
    int syn = s->kind == SK_HEX_BANE ? s->param[9] : s->kind == SK_HEX_SIPHON ? s->param[8] : s->param[2];
    int ticks = 150 + ebl * syn;
    switch (s->kind) {
    case SK_HEX_BANE: { /* -% de defensa (hasta 50) y -25% de puntería */
        UnitCurse cu;
        memset(&cu, 0, sizeof cu);
        int def = skill_ln(s, 5, 6, lvl);
        cu.def_pct = -(def < 50 ? def : 50);
        cu.ar_pct = -25;
        cu.ticks = ticks;
        cu.single = 1;
        units_curse(ux, uy, 0.6f, &cu);
        overlays_add("hexbanedebuff", OV_AT_UNIT, unit, ux, uy, ticks);
        break;
    }
    case SK_HEX_SIPHON: { /* el golpeado hace un tercio menos de daño */
        NecCurse nc;
        memset(&nc, 0, sizeof nc);
        nc.kind = NC_WEAKEN;
        nc.ticks = ticks;
        nc.owner = (int)(p - s_pl);
        nc.dmg = -33;
        if (units_nec_curse(unit, &nc))
            overlays_add("hexsyphondebuff", OV_AT_UNIT, unit, ux, uy, ticks);
        break;
    }
    default: { /* Purga: si ya la tiene, puede estallar */
        int k = war_purge_find(unit);
        if (k >= 0 && !refresh_only) {
            int chance = (s->param[11] + war_blvl(p, "Sigil Death") * s->param[10]) / 100;
            if ((int)(rnd() % 100) < chance) {
                war_erupt(p, s, lvl, unit);
                if (--s_purges[k].charges <= 0) {
                    s_purges[k].ticks = 0;
                    return;
                }
            }
            s_purges[k].ticks = ticks;
            return;
        }
        if (k < 0)
            for (int j = 0; j < WAR_PURGES && k < 0; j++)
                if (s_purges[j].ticks <= 0)
                    k = j;
        if (k < 0)
            return;
        s_purges[k].unit = unit;
        s_purges[k].owner = (int)(p - s_pl);
        s_purges[k].charges = 1 + lvl / 10 < 3 ? 1 + lvl / 10 : 3;
        s_purges[k].ticks = ticks;
        overlays_add("hexpurgedebuff", OV_AT_UNIT, unit, ux, uy, ticks);
        break;
    }
    }
}

/* Cada golpe del Brujo que acierta (cuerpo a cuerpo o con sus proyectiles) */
static void war_hit(Player *p, int target, int killed)
{
    if (!killed && p->hex_ticks > 0)
        war_hex_apply(p, target, 0);
}

/* Explosión sobrenatural: a los embrujados de alrededor, daño mágico que
   roba vida y maná, y les renueva el maleficio */
static void war_blast(Player *p)
{
    if (p->eldritch_skill < 0)
        return;
    const SkillDef *s = skills_get(p->cls, p->eldritch_skill);
    int lvl = eff_lvl(p, p->eldritch_skill), me = (int)(p - s_pl);
    SkillCtx c = pctx(p);
    int lo, hi, steal = skill_ln(s, 1, 2, lvl);
    skill_elem_syn(s, lvl, &c, &lo, &hi);
    int ids[24], n = units_enemies_near(p->x, p->y, 5.0f * WAR_YARD + 1.5f, ids, 24), done = 0;
    for (int k = 0; k < n; k++) {
        if (!war_hexed(ids[k]))
            continue;
        float ux, uy;
        units_position(ids[k], 1.0f, &ux, &uy);
        int d = s_one_hit ? 1 << 20 : spell_resist(me, ids[k], ELEM_MAG, roll(lo, hi));
        int exp = units_damage(ids[k], d);
        p->hp += d * steal / 100;
        p->mana += d * steal / 100;
        overlays_add("surge_hex_explode", OV_AT_UNIT, ids[k], ux, uy, 0);
        if (exp > 0)
            award_kill(p, ids[k], exp);
        else
            war_hex_apply(p, ids[k], 1);
        done++;
    }
    if (p->hp > p->hp_max) p->hp = p->hp_max;
    if (p->mana > p->mana_max) p->mana = p->mana_max;
    if (done)
        overlays_add("warlock_psychic_cast_1_no_hex", OV_AT_PLAYER, me, p->x, p->y, 0);
}

/* ---- Los sigilos ---- */

static void war_sigil(Player *p, const SkillDef *s, int lvl, float x, float y)
{
    int me = (int)(p - s_pl), slot = -1;
    for (int k = 0; k < WAR_SIGILS; k++) /* uno de cada clase por jugador (calc3: el límite es 1) */
        if (s_sigils[k].used && s_sigils[k].owner == me && s_sigils[k].kind == s->kind) {
            slot = k;
            break;
        }
    for (int k = 0; k < WAR_SIGILS && slot < 0; k++)
        if (!s_sigils[k].used)
            slot = k;
    if (slot < 0)
        return;
    WarSigil *g = &s_sigils[slot];
    g->used = 1;
    g->owner = me;
    g->skill = (int)(s - skills_get(p->cls, 0));
    g->lvl = lvl;
    g->kind = s->kind;
    g->ticks = 100 + lvl * 25; /* la duración del sigilo (range de missiles.txt) */
    g->next = 0;
    g->x = x;
    g->y = y;
    int rng = lvl >= 20 ? 8 : lvl >= 10 ? 6 : 4;
    g->r = (float)rng * WAR_YARD + 1.0f;
    const char *base = s->kind == SK_SIGIL_LETHARGY ? "sigillethargy" : s->kind == SK_SIGIL_RANCOR ? "sigilrancor" : "sigildeath";
    char name[32];
    snprintf(name, sizeof name, "%s%s", base, lvl >= 20 ? "large" : lvl >= 10 ? "medium" : "small");
    Missile *mi = missiles_effect(name, x, y);
    if (mi)
        mi->life = g->ticks;
}

/* Lo que hace cada sigilo a los que están adentro (cada medio segundo) */
static void war_sigils_tick(void)
{
    for (int k = 0; k < WAR_SIGILS; k++) {
        WarSigil *g = &s_sigils[k];
        if (!g->used)
            continue;
        if (--g->ticks <= 0 || g->owner >= s_num_players) {
            g->used = 0;
            continue;
        }
        if (--g->next > 0)
            continue;
        g->next = 12; /* auralencalc 12 */
        Player *p = &s_pl[g->owner];
        const SkillDef *s = skills_get(p->cls, g->skill);
        int ids[32], n = units_enemies_near(g->x, g->y, g->r, ids, 32);
        for (int j = 0; j < n; j++) {
            int u = ids[j];
            float ux, uy;
            units_position(u, 1.0f, &ux, &uy);
            switch (g->kind) {
            case SK_SIGIL_LETHARGY: { /* frena a la mitad y baja la defensa */
                units_slow(u, -s->param[7], 18);
                UnitCurse cu;
                memset(&cu, 0, sizeof cu);
                cu.def_pct = skill_ln(s, 5, 6, g->lvl);
                cu.ticks = 18;
                cu.single = 1;
                units_curse(ux, uy, 0.6f, &cu);
                break;
            }
            case SK_SIGIL_RANCOR: /* enfurecidos (ln34 % de daño, ln56 % de velocidad de ataque); calc1 (75%): se pelean entre ellos */
                units_enrage(u, skill_ln(s, 3, 4, g->lvl), skill_ln(s, 5, 6, g->lvl), 18);
                if (units_nec_cursed(u) != NC_CONFUSE && (int)(rnd() % 100) < skill_calc(s, SC_CALC1, g->lvl, NULL)) {
                    NecCurse nc;
                    memset(&nc, 0, sizeof nc);
                    nc.kind = NC_CONFUSE;
                    nc.ticks = 3 * TICK_RATE;
                    nc.owner = g->owner;
                    if (units_nec_curse(u, &nc))
                        overlays_add("curse_hit", OV_AT_UNIT, u, ux, uy, 0);
                }
                break;
            default: { /* muerte: los de menos de calc1% (calc2% los jefes) mueren y estallan */
                int hp, mx;
                units_health(u, &hp, &mx);
                int lim = units_is_unique(u) ? skill_ln(s, 3, 4, g->lvl) : skill_ln(s, 1, 2, g->lvl);
                if (mx <= 0 || hp * 100 >= mx * lim)
                    break;
                int exp = units_damage(u, hp + 1);
                if (exp > 0)
                    award_kill(p, u, exp);
                missiles_effect("ringoffireexplode", ux, uy);
                audio_play_name("Warlock_Skill_Seal_Death_Hit", ux, uy);
                /* Sigil Death Explosion: 70-120% de la vida del muerto, de fuego */
                int lo = mx * 70 / 100, hi = mx * 120 / 100;
                war_area(p, ux, uy, 3.0f, ELEM_FIRE, lo, hi, 0, 0);
                break;
            }
            }
        }
    }
}

/* ---- Consumir: lo que da cada demonio (consumestat de monpet.txt) ---- */

static TxtTable s_monpet;
static int      s_monpet_state; /* 0 sin cargar, 1 cargada, -1 no está */

static int war_monpet_row(const char *id)
{
    if (s_monpet_state == 0) {
        size_t n;
        uint8_t *d = fs_read("data/global/excel/monpet.txt", &n);
        s_monpet_state = d && txt_load(d, n, &s_monpet) == 0 ? 1 : -1;
        free(d);
    }
    return s_monpet_state > 0 && id && id[0] ? txt_find_row(&s_monpet, txt_col(&s_monpet, "monster"), id) : -1;
}

/* Cambia cada `from` por `to` */
static void war_replace(char *dst, size_t cap, const char *src, const char *from, const char *to)
{
    size_t o = 0, fl = strlen(from), tl = strlen(to);
    while (*src && o + 1 < cap) {
        if (!strncmp(src, from, fl) && o + tl + 1 < cap) {
            memcpy(dst + o, to, tl);
            o += tl;
            src += fl;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
}

/* Una fórmula de monpet.txt con las del motor: sksrc('X'.campo) es
   skill('X'.campo), el nivel de Consumir es lvl y diff la dificultad
   (Normal = 1: la tabla pone (diff==1) para Normal) */
static int war_monpet_calc(const SkillDef *s, int lvl, const SkillCtx *c, const char *src)
{
    char a[256], b[256];
    war_replace(a, sizeof a, src, "\"", "");
    war_replace(b, sizeof b, a, "sksrc (", "skill(");
    war_replace(a, sizeof a, b, "sksrc(", "skill(");
    war_replace(b, sizeof b, a, "skill('Consume'.lvl)", "lvl");
    war_replace(a, sizeof a, b, "diff", "1");
    return skill_eval(s, a, lvl, c);
}

/* Guarda lo que da el demonio `unit` al sacrificarlo (hasta 5 propiedades) */
static void war_consume_take(Player *p, const SkillDef *s, int lvl, int unit)
{
    p->consume_n = 0;
    int row = war_monpet_row(units_id(unit));
    if (row < 0)
        return;
    SkillCtx c = pctx(p);
    for (int k = 1; k <= 5 && p->consume_n < 5; k++) {
        char col[16];
        snprintf(col, sizeof col, "consumestat%d", k);
        int stat = itemstats_find(txt_get(&s_monpet, row, txt_col(&s_monpet, col)));
        snprintf(col, sizeof col, "consumecalc%d", k);
        const char *calc = txt_get(&s_monpet, row, txt_col(&s_monpet, col));
        if (stat < 0 || stat >= STAT_COUNT || stat == STAT_AURA || !calc[0])
            continue; /* (item_aura: las auras de los objetos no están hechas; solo las tienen jefes que no se vinculan) */
        int v = war_monpet_calc(s, lvl, &c, calc);
        if (v == 0)
            continue;
        p->consume_stat[p->consume_n] = stat;
        p->consume_val[p->consume_n++] = v;
    }
}

/* En refresh_stats, antes de las pasivas: lo de Consumir que va como de los
   objetos (resistencias, dominios, golpe aplastante, + a las habilidades) */
static void war_consume_stats(Player *p)
{
    if (p->consume_ticks <= 0)
        return;
    for (int k = 0; k < p->consume_n; k++) {
        int st = p->consume_stat[k], v = p->consume_val[k];
        if (st == 25) /* damagepercent: en war_passives */
            continue;
        if (st == STAT_ALL_SKILLS) {
            for (int i = 0; i < MAX_CLASS_SKILLS; i++)
                p->item_sk[i] += v;
            continue;
        }
        p->bonus[st] += v;
    }
}

/* ---- Vincular demonio: se canaliza ----
   Mientras se mantiene apretado el botón: gasta (mana + lvlmana * (lvl - 1))
   << manashift / 256 de maná por cuadro y cada 12 (monpet.txt calc1) prueba
   (bindchancecalc) calc3 + calc4 %: dm34 + los puntos de Marca de la muerte
   + (100 - % de vida) / 5, si tiene menos del 99% de la vida */
static void war_bind_finish(Player *p, const SkillDef *s, int lvl, int t);

static void war_bind_tick(Player *p)
{
    if (p->bind_ch_skill < 0)
        return;
    const SkillDef *s = skills_get(p->cls, p->bind_ch_skill);
    int lvl = eff_lvl(p, p->bind_ch_skill), t = p->bind_ch_target;
    int held = p->bind_ch_button >= 0 && ((p->held_mask >> p->bind_ch_button) & 1);
    if (p->state != PS_CAST || p->attack_skill != p->bind_ch_skill || !held || !units_alive_enemy(t) ||
        !units_is_demon(t)) {
        p->bind_ch_skill = -1; /* lo soltó (o el demonio ya no está): la animación termina sola */
        return;
    }
    float ux, uy;
    units_position(t, 1.0f, &ux, &uy);
    float dx = ux - p->x, dy = uy - p->y, d = sqrtf(dx * dx + dy * dy);
    if (d > AIM_RANGE + 2.0f) {
        show_message(p, "El demonio se alejó");
        p->bind_ch_skill = -1;
        return;
    }
    p->bind_ch_acc += (s->mana + s->lvlmana * (lvl - 1)) << s->manashift;
    int use = p->bind_ch_acc >> 8;
    p->bind_ch_acc &= 255;
    if (use > p->mana) {
        show_message(p, "No tenés suficiente maná");
        p->bind_ch_skill = -1;
        set_player_state(p, PS_NORMAL, MODE_NU);
        return;
    }
    p->mana -= use;
    /* La animación sigue: antes de terminar vuelve al cuadro del lanzamiento */
    const CharAnim *a = player_anim(p, p->mode);
    int frames = charanim_frames(a), hf = a->hit_frame > 0 ? a->hit_frame : frames / 2;
    if ((p->anim_acc >> 8) >= frames - 1)
        p->anim_acc = hf << 8;
    face_unit(p, t);
    /* La cadena del Brujo al demonio (binddemonchannel): unos eslabones a lo largo */
    if (p->bind_ch_frames % 8 == 0) {
        int n = (int)(d / 1.2f) + 1;
        for (int k = 1; k <= n && k <= 12; k++) {
            float f = (float)k / (float)(n + 1);
            Missile *mi = missiles_effect("binddemonchannel", p->x + dx * f, p->y + dy * f);
            if (mi && k > 1 && mi->sound > 0) { /* el sonido, uno solo */
                audio_stop(mi->sound);
                mi->sound = 0;
            }
        }
    }
    if (++p->bind_ch_frames % 12 != 0)
        return;
    int hp, mx;
    units_health(t, &hp, &mx);
    int hpct = mx > 0 ? hp * 100 / mx : 100;
    int chance = hpct < 99 ? skill_dm(s, 3, 4, lvl) + war_blvl(p, "Death Mark") + (100 - hpct) / 5 : 0;
    if (getenv("D2_ST_DEBUG"))
        printf("      J%d canaliza Vincular a %s (%d/%d): %d%%, maná %d\n", (int)(p - s_pl) + 1, units_id(t), hp, mx,
               chance, p->mana);
    if ((int)(rnd() % 100) >= chance)
        return;
    p->bind_ch_skill = -1;
    war_bind_finish(p, s, lvl, t);
}

/* Se lo vinculó: pelea para el Brujo con lo de la habilidad (sus passivestat) y,
   según el nivel, los modificadores de jefe (calc2..calc5): muy fuerte desde el 5,
   muy rápido desde el 10, golpe espectral desde el 15 y un aura desde el 20 */
static void war_bind_finish(Player *p, const SkillDef *s, int lvl, int t)
{
    int me = (int)(p - s_pl);
    float ux, uy;
    units_position(t, 1.0f, &ux, &uy);
    if (p->bind_unit >= 0 && units_converted(p->bind_unit)) /* uno solo (petmax 1): el anterior se va */
        units_pet_remove(p->bind_unit);
    p->bind_aura = -1;
    if (!units_bind(t, me)) {
        show_message(p, "Ese demonio no se puede vincular");
        return;
    }
    p->bind_unit = t;
    const SkillDef *dms, *bos;
    int dm = war_lvl(p, "Demonic Mastery", &dms), bo = war_lvl(p, "Blood Oath", &bos);
    /* daño ln56 + Dominio demoníaco ln21, daño fijo ln43, defensa ln91, puntería ln78 + Dominio ln56 */
    units_pet_boost(t, 0, skill_ln(s, 5, 6, lvl) + (dm > 0 ? skill_ln(dms, 11, 12, dm) : 0), skill_ln(s, 13, 14, lvl) / 10);
    int ias = dm > 0 ? skill_ln(dms, 1, 2, dm) : 0;
    units_pet_bonus(t, ias < 25 ? ias : 25, bo > 0 ? skill_dm(bos, 9, 10, bo) : 0, bo > 0 ? skill_dm(bos, 7, 8, bo) : 0,
                    skill_ln(s, 9, 10, lvl), skill_ln(s, 7, 8, lvl) + (dm > 0 ? skill_ln(dms, 5, 6, dm) : 0));
    units_pet_mods(t, lvl >= 5, lvl >= 10, lvl >= 15);
    units_pet_cap_hp(t, 12000 + 600 * (s_num_players - 1)); /* BoundStat1 */
    if (lvl >= 20) { /* Aura Enchanted: un aura del Paladín al azar */
        static const char *const k_auras[] = { "Might", "Blessed Aim", "Fanaticism", "Concentration" };
        p->bind_aura = skills_find(3, k_auras[rnd() % (sizeof k_auras / sizeof k_auras[0])]);
    }
    overlays_add("warlockdemonground", OV_AT_UNIT, t, ux, uy, 0);
    overlays_add("warlockdemontorso", OV_AT_UNIT, t, ux, uy, 0);
    audio_play_name("Warlock_Skill_Demon_Summon_Bound_Silence", ux, uy);
    show_message(p, "Demonio vinculado");
    if (getenv("D2_ST_DEBUG")) {
        int mx, hp = units_pet_hp(t, &mx);
        printf("      J%d vinculó a %s (nivel %d): vida %d/%d\n", me + 1, units_id(t), lvl, hp, mx);
    }
}

/* ---- Al lanzarla ---- */

/* Las nubes de miasma (daño mágico por segundo, con la Entropía mejorada) */
static void war_cloud(Player *p, const char *name, float x, float y, int lvl)
{
    const SkillDef *es;
    int el = war_lvl(p, "Enhanced Entropy", &es), lo, hi;
    missiles_level_damage(name, lvl, &lo, &hi);
    /* sinergias: la otra miasma y el Abismo (par9 = 20% de la Descarga) */
    int syn = (war_blvl(p, "Miasma Chains") + war_blvl(p, "Miasma Bolt") + war_blvl(p, "Abyss")) * 20;
    int pct = syn + (el > 0 ? skill_ln(es, 1, 2, el) : 0);
    Missile *mi = missiles_effect(name, x, y);
    if (!mi)
        return;
    mi->flags |= MF_BURN;
    mi->owner = (int)(p - s_pl);
    mi->elem = ELEM_MAG;
    mi->emin = (lo > 0 ? lo : 2) * (100 + pct) / 100;
    mi->emax = (hi > lo ? hi : lo + 2) * (100 + pct) / 100;
    int rad = el >= 10 ? 4 : el >= 5 ? 3 : 2;
    mi->radius10 = (int)((float)rad * WAR_YARD * 10.0f * (float)(100 + (el > 0 ? skill_ln(es, 3, 4, el) : 0)) / 100.0f) + 8;
    int life = 50 + (el > 0 ? (el * 5 < 100 ? el * 5 : 100) : 0);
    mi->life = life * (100 + (el > 0 ? skill_ln(es, 5, 6, el) : 0)) / 100;
}

/* Los proyectiles del arma (Golpe de eco, Hojas reflejadas): el daño del arma con +pct% */
static Missile *war_blade(Player *p, float ax, float ay, int pct, int ar_pct, const char *mark)
{
    Missile *mi = player_missile(p, "echoingstrike", ax, ay, pct, ar_pct, 0);
    if (mi)
        mi->trail = mark;
    return mi;
}

/* 1 si arrancó (gasta el maná) */
static int war_start(Player *p, int skill, int cost)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    int in_town = p->level && p->level->town;
    p->cast_target = -1;
    if (p->war_cd[skill] > 0) {
        show_message(p, "Todavía no se puede");
        return 0;
    }
    if (in_town && !s->intown) {
        show_message(p, "En el pueblo no se pelea");
        return 0;
    }
    int demons[8], nd = war_demons(p, demons, 8);
    switch (s->kind) {
    case SK_DEATH_MARK:
    case SK_BLOOD_BOIL:
    case SK_ENGORGE:
    case SK_CONSUME:
        if (nd == 0) {
            show_message(p, "Necesitás un demonio");
            return 0;
        }
        break;
    default:
        break;
    }
    switch (s->kind) {
    case SK_CLEAVE: /* un golpe cuerpo a cuerpo que alcanza a todos en el arco (con el segundo ataque, A2) */
        if (!p->equip[SLOT_WEAPON].type) {
            show_message(p, "Necesitás un arma");
            return 0;
        }
        start_attack(p, skill);
        if (p->mode != MODE_A2 && player_anim(p, MODE_A2)->ok) {
            set_player_state(p, PS_ATTACK, MODE_A2);
            p->hits_total = 1;
            p->hits_done = 0;
        }
        overlays_add("warlock_psychic_cast_1", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
        p->mana -= cost;
        return 1;
    case SK_MIRRORED_BLADES: /* de cerca, calc5 golpes: el suyo y los de las copias del arma */
        if (!p->equip[SLOT_WEAPON].type) {
            show_message(p, "Necesitás un arma");
            return 0;
        }
        if (units_find_enemy(p->x, p->y, MELEE_RANGE) >= 0) {
            start_attack(p, skill);
            int n = 2 + lvl / 5;
            p->hits_total = n > 5 ? 5 : n;
            p->hits_done = 0;
            overlays_add("warlock_psychic_cast_1", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
            p->mana -= cost;
            return 1;
        }
        pal_aim(p, AIM_RANGE); /* de lejos, las tira */
        break;
    case SK_ENGORGE: /* un cadáver cerca */
        p->cast_target = units_find_corpse(p->x, p->y, 8.0f);
        if (p->cast_target < 0) {
            show_message(p, "No hay ningún cadáver cerca");
            return 0;
        }
        break;
    case SK_BIND_DEMON: /* un demonio enemigo cerca */
        pal_aim(p, AIM_RANGE);
        if (!units_alive_enemy(p->target) || !units_is_demon(p->target)) {
            int ids[16], n = units_enemies_near(p->x, p->y, AIM_RANGE, ids, 16);
            p->target = -1;
            for (int k = 0; k < n && p->target < 0; k++)
                if (units_is_demon(ids[k]))
                    p->target = ids[k];
        }
        if (p->target < 0) {
            show_message(p, "No hay ningún demonio cerca");
            return 0;
        }
        { /* monpet.txt calc2: a un jefe, desde el nivel 15 (a los de misión y de nombre propio, nunca) */
            int rank = units_bind_rank(p->target);
            if (rank < 0) {
                show_message(p, "A ese no se lo puede vincular");
                return 0;
            }
            if (rank > 0 && war_blvl(p, "Bind Demon") < 15) {
                show_message(p, "Para un jefe hace falta el nivel 15");
                return 0;
            }
        }
        if (p->mana < WAR_BIND_START) {
            show_message(p, "No tenés suficiente maná");
            return 0;
        }
        /* Se canaliza mientras se mantiene apretado (war_bind_tick) */
        face_unit(p, p->target);
        p->bind_ch_skill = skill;
        p->bind_ch_target = p->target;
        p->bind_ch_button = p->last_button;
        p->bind_ch_frames = p->bind_ch_acc = 0;
        p->attack_skill = skill;
        play_skill_sound(p, s);
        p->mana -= WAR_BIND_START;
        set_player_state(p, PS_CAST, MODE_SC);
        p->hits_done = 1; /* sin efecto al final: lo hace la canalización */
        return 1;
    case SK_ECHOING_STRIKE:
    case SK_BLADE_WARP:
        if (!p->equip[SLOT_WEAPON].type) {
            show_message(p, "Necesitás un arma");
            return 0;
        }
        pal_aim(p, AIM_RANGE);
        break;
    default:
        pal_aim(p, AIM_RANGE);
        break;
    }
    p->attack_skill = skill;
    p->hits_done = 0;
    play_skill_sound(p, s);
    p->mana -= cost;
    if (s->delay > 0)
        p->war_cd[skill] = s->delay;
    (void)lvl;
    if (!player_anim(p, MODE_SC)->ok) {
        cast_effect(p);
        return 1;
    }
    set_player_state(p, PS_CAST, MODE_SC);
    return 1;
}

/* ---- El efecto (en el cuadro del lanzamiento) ---- */

static int war_effect(Player *p, const SkillDef *s, int lvl)
{
    SkillCtx c = pctx(p);
    int me = (int)(p - s_pl), lo = 0, hi = 0, plo = 0, phi = 0, skill = (int)(s - skills_get(p->cls, 0));
    float tx = p->cast_x, ty = p->cast_y, ax, ay;
    if (units_alive_enemy(p->target))
        units_position(p->target, 1.0f, &tx, &ty);
    if (s->summon[0])
        return war_summon(p, s, lvl);
    int demons[8], nd = war_demons(p, demons, 8);
    switch (s->kind) {
    case SK_DEATH_MARK: { /* los demonios saltan al enemigo; le baja la defensa */
        int t = units_alive_enemy(p->target) ? p->target : units_find_enemy(p->x, p->y, (float)s->param[9] * WAR_YARD);
        if (t < 0)
            return 0;
        float ux, uy;
        units_position(t, 1.0f, &ux, &uy);
        for (int k = 0; k < nd; k++) {
            float a = (float)k * 2.1f, x = ux + cosf(a) * 1.2f, y = uy + sinf(a) * 1.2f;
            if (player_blocked(x, y, x, y))
                x = ux, y = uy;
            units_pet_place(demons[k], x, y);
            overlays_add("warlock_death_mark_pet", OV_AT_UNIT, demons[k], x, y, 0);
        }
        UnitCurse cu;
        memset(&cu, 0, sizeof cu);
        cu.def_flat = -skill_ln(s, 5, 6, lvl);
        cu.ticks = skill_ln(s, 3, 4, lvl);
        cu.single = 1;
        units_curse(ux, uy, 0.6f, &cu);
        /* normal_damage_reduction y magic_damage_reduction ln12 (negativas): cada golpe le hace más */
        units_mark(t, -skill_ln(s, 1, 2, lvl), cu.ticks);
        overlays_add("death_mark", OV_AT_UNIT, t, ux, uy, cu.ticks);
        return 1;
    }
    case SK_ENGORGE: { /* el demonio más cercano come el cadáver y se cura */
        int corpse = p->cast_target;
        if (corpse < 0)
            return 0;
        float cx, cy;
        units_position(corpse, 1.0f, &cx, &cy);
        int d = war_demon_near(p, cx, cy);
        if (d < 0)
            return 0;
        int mx, hp = units_pet_hp(d, &mx);
        (void)hp;
        units_ally_heal(d, mx * skill_ln(s, 3, 4, lvl) / 100);
        /* y por ln56 cuadros (las aurastat de engorge): velocidad de ataque min(ln12, 35), par7 % menos de
           daño físico, robo de vida ln21, defensa y regeneración por punto del Juramento de sangre (pa11, pa12) */
        {
            const SkillDef *bos;
            (void)war_lvl(p, "Blood Oath", &bos);
            int bo = war_blvl(p, "Blood Oath"), haste = skill_ln(s, 1, 2, lvl);
            units_pet_buff(d, skill_ln(s, 5, 6, lvl), haste < 35 ? haste : 35, s->param[7],
                           bos ? bo * bos->param[11] : 0, bos ? bo * bos->param[12] : 0, skill_ln(s, 11, 12, lvl));
        }
        units_consume_corpse(corpse);
        float dx, dy;
        units_position(d, 1.0f, &dx, &dy);
        missiles_effect("engorgecorpseeffect", cx, cy);
        overlays_add("engorgestate", OV_AT_UNIT, d, dx, dy, skill_ln(s, 5, 6, lvl));
        return 1;
    }
    case SK_BLOOD_BOIL: { /* cada demonio pierde vida y estalla (físico y fuego) */
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int pct = skill_ln(s, 1, 2, lvl) / (nd > 0 ? nd : 1);
        pct = pct < 5 ? 5 : pct;
        float r = (float)(lvl >= 10 ? 7 : lvl >= 5 ? 6 : 5) * WAR_YARD + 0.5f;
        for (int k = 0; k < nd; k++) {
            int mx, hp = units_pet_hp(demons[k], &mx), cost = mx * pct / 100;
            if (cost >= hp)
                cost = hp - 1;
            if (cost > 0)
                units_pet_damage(demons[k], cost);
            float x, y;
            units_position(demons[k], 1.0f, &x, &y);
            missiles_effect("bloodboilexplode", x, y);
            war_area(p, x, y, r, ELEM_FIRE, lo, hi, plo, phi);
        }
        return nd;
    }
    case SK_CONSUME: { /* sacrifica el demonio más cercano (también el vinculado): vida máxima, velocidad y lo suyo */
        int d = war_demon_near(p, p->x, p->y);
        if (d < 0)
            return 0;
        float x, y;
        units_position(d, 1.0f, &x, &y);
        missiles_effect("bloodboilexplode", x, y);
        war_consume_take(p, s, lvl, d);
        if (d == p->bind_unit) {
            units_pet_remove(d);
            p->bind_unit = -1;
            p->bind_aura = -1;
        } else {
            for (int k = 0; k < NEC_PETS; k++)
                if (war_is_demon_kind(p->pet_kind[k]) && p->pets[k] == d)
                    nec_pet_drop(p, k);
        }
        p->consume_ticks = skill_ln(s, 1, 2, lvl);
        p->consume_life = skill_ln(s, 3, 4, lvl);
        p->consume_speed = skill_dm(s, 5, 6, lvl);
        overlays_add("warlockconsume", OV_AT_PLAYER, me, p->x, p->y, 0);
        refresh_stats(p);
        return 1;
    }
    case SK_BIND_DEMON: /* se canaliza (war_bind_tick) */
        return 0;
    case SK_HEX_BANE:
    case SK_HEX_SIPHON:
    case SK_HEX_PURGE: /* el maleficio del arma (uno a la vez) */
        p->hex_skill = skill;
        p->hex_ticks = skill_ln(s, 3, 4, lvl);
        refresh_stats(p);
        overlays_add("hex_enchant", OV_AT_PLAYER, me, p->x, p->y, 0);
        return 1;
    case SK_ELDRITCH_BLAST: /* un rato, cada 30 cuadros */
        p->eldritch_skill = skill;
        p->eldritch_ticks = s->param[3] + war_blvl(p, "Psychic Ward") * s->param[4];
        p->eldritch_next = 0;
        overlays_add("surge_hex_explode", OV_AT_PLAYER, me, p->x, p->y, 0);
        return 1;
    case SK_PSYCHIC_WARD: /* absorbe ln34 (+ par8 por punto de Levitación y de Tajo) */
        p->ward = p->ward_max = skill_ln(s, 3, 4, lvl) + (war_blvl(p, "Levitate") + war_blvl(p, "Cleave")) * s->param[8];
        p->ward_skill = skill;
        overlays_add("psychicward", OV_AT_PLAYER, me, p->x, p->y, 2 * TICK_RATE);
        return 1;
    case SK_ECHOING_STRIKE: { /* ecos del arma que atraviesan (más con las Hojas reflejadas) */
        aim_at(p, p->target, &ax, &ay);
        int n = 1 + war_blvl(p, "Mirrored Blades") / 5;
        int pct = skill_ln(s, 1, 2, lvl) + (war_blvl(p, "Mirrored Blades") + war_blvl(p, "Blade Warp")) * s->param[8];
        skill_phys_syn(s, lvl, &c, &plo, &phi);
        float base = atan2f(ay, ax);
        for (int k = 0; k < n && k < 5; k++) {
            float a = base + ((float)k - (float)(n - 1) / 2.0f) * 0.18f;
            Missile *mi = war_blade(p, cosf(a), sinf(a), pct, skill_tohit(s, lvl), "war:echo");
            if (!mi)
                break;
            mi->dmin = mi->dmin * 116 / 128 + plo; /* SrcDam 116: el 90% del arma, y lo de la habilidad */
            mi->dmax = mi->dmax * 116 / 128 + phi;
            mi->flags |= MF_PIERCE;
        }
        return n;
    }
    case SK_MIRRORED_BLADES: { /* calc5 copias del arma con dm12% del daño */
        aim_at(p, p->target, &ax, &ay);
        int n = 2 + lvl / 5;
        n = n > 5 ? 5 : n;
        int pct = skill_dm(s, 1, 2, lvl) - 100;
        float base = atan2f(ay, ax);
        for (int k = 0; k < n; k++) {
            float a = base + ((float)k - (float)(n - 1) / 2.0f) * 0.14f;
            if (!war_blade(p, cosf(a), sinf(a), pct, skill_tohit(s, lvl), "war:mirror"))
                break;
        }
        return n;
    }
    case SK_BLADE_WARP: { /* un arma astral que explota donde pega y lleva al Brujo ahí */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        Missile *mi = spell_missile(p, "bladewarp", p->x, p->y, ax, ay, ELEM_MAG, lo, hi, 0);
        if (!mi)
            return 0;
        mi->flags |= MF_EXPLODE;
        mi->radius10 = (int)((float)s->param[1] * WAR_YARD * 10.0f) + 10;
        mi->trail = "war:warp";
        return 1;
    }
    case SK_SIGIL_LETHARGY:
    case SK_SIGIL_RANCOR:
    case SK_SIGIL_DEATH:
        war_sigil(p, s, lvl, tx, ty);
        return 1;
    case SK_RING_OF_FIRE: { /* llamas alrededor que empujan */
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int n = lvl >= 10 ? 32 : 16, group = hit_group_new();
        for (int k = 0; k < n; k++) {
            float a = (float)k * 2.0f * PI_F / (float)n;
            Missile *mi = spell_missile(p, "ringoffire", p->x, p->y, cosf(a), sinf(a), ELEM_FIRE, lo, hi, 0);
            if (!mi)
                break;
            mi->flags |= MF_GROUP | MF_KNOCK;
            mi->group = group;
            mi->trail = "war:ring"; /* DmgCalc1: 100 + cuadros * par1 (5) % (war_missile_pct) */
        }
        return n;
    }
    case SK_MIASMA_BOLT: { /* la descarga y, donde pega, la nube */
        spell_aim(p, &ax, &ay);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        Missile *mi = spell_missile(p, "miasmabolt", p->x, p->y, ax, ay, ELEM_MAG, lo, hi, 0);
        if (!mi)
            return 0;
        mi->trail = "war:miasma";
        mi->count = lvl;
        return 1;
    }
    case SK_MIASMA_CHAINS: { /* de uno a otro (hasta min(5 + lvl/4, 10)): daño y una nube en cada uno */
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int most = 5 + lvl / 4, hit[10], nh = 0, cur = units_alive_enemy(p->target) ? p->target
                                                                                   : units_find_enemy(p->x, p->y, AIM_RANGE);
        most = most > 10 ? 10 : most;
        while (cur >= 0 && nh < most) {
            hit[nh++] = cur;
            float ux, uy;
            units_position(cur, 1.0f, &ux, &uy);
            missiles_effect("miasmapuff", ux, uy);
            war_cloud(p, "miasmachainscloud", ux, uy, lvl);
            war_damage(p, cur, ELEM_MAG, roll(lo, hi));
            /* el próximo: el más cercano que no tocó */
            int ids[24], n = units_enemies_near(ux, uy, 8.0f, ids, 24), next = -1;
            float bd = 1e9f;
            for (int k = 0; k < n; k++) {
                int seen = 0;
                for (int j = 0; j < nh && !seen; j++)
                    seen = hit[j] == ids[k];
                if (seen)
                    continue;
                float x, y;
                units_position(ids[k], 1.0f, &x, &y);
                float d = (x - ux) * (x - ux) + (y - uy) * (y - uy);
                if (d < bd) {
                    bd = d;
                    next = ids[k];
                }
            }
            cur = next;
        }
        return nh;
    }
    case SK_FLAME_WAVE: { /* un frente de fuego que avanza y deja el piso ardiendo */
        spell_aim(p, &ax, &ay);
        float l = sqrtf(ax * ax + ay * ay);
        ax /= l > 0.0f ? l : 1.0f;
        ay /= l > 0.0f ? l : 1.0f;
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int flo, fhi, half = skill_dm(s, 1, 2, lvl), group = hit_group_new();
        missiles_level_damage("flamewavelingerfire", lvl, &flo, &fhi);
        int syn = (war_blvl(p, "Ring of Fire") + war_blvl(p, "Apocalypse")) * s->param[9];
        for (int k = -half; k <= half; k++) {
            float ox = -ay * (float)k * 0.7f, oy = ax * (float)k * 0.7f;
            Missile *mi = spell_missile(p, "flamewave", p->x + ox, p->y + oy, ax, ay, ELEM_FIRE, lo, hi, 0);
            if (!mi)
                break;
            mi->flags |= MF_PIERCE | MF_GROUP | MF_MAKER;
            mi->group = group;
            mi->trail = "flamewavelingerfire";
            mi->burn_min = (flo > 0 ? flo : 3) * (100 + syn + p->sk_fire_pct) / 100;
            mi->burn_max = (fhi > flo ? fhi : flo + 3) * (100 + syn + p->sk_fire_pct) / 100;
            mi->burn_life = 50;
            mi->mx = mi->my = -1000.0f;
        }
        audio_play_name("Warlock_Skill_Flame_Wave_Travel_03", p->x, p->y);
        return 1;
    }
    case SK_APOCALYPSE: { /* fuego infernal alrededor, bajando la resistencia al fuego */
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        int res = skill_ln(s, 1, 2, lvl);
        res = res > 40 ? 40 : res;
        float r = (float)(lvl >= 20 ? 17 : lvl >= 10 ? 15 : 13) * WAR_YARD;
        overlays_add("apocalypse", OV_AT_PLAYER, me, p->x, p->y, 0);
        int ids[48], n = units_enemies_near(p->x, p->y, r, ids, 48);
        for (int k = 0; k < n; k++) {
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            NecCurse nc;
            memset(&nc, 0, sizeof nc);
            nc.kind = NC_LOWERRES;
            nc.ticks = 2 * TICK_RATE;
            nc.owner = me;
            nc.res = res;
            units_nec_curse(ids[k], &nc);
            missiles_effect("fireexplode", ux, uy);
            war_damage(p, ids[k], ELEM_FIRE, roll(lo, hi));
        }
        audio_play_name("Warlock_Skill_Apocalypse_01", p->x, p->y);
        return n;
    }
    case SK_ABYSS: { /* una grieta que atrae y después estalla */
        const SkillDef *es;
        int el = war_lvl(p, "Enhanced Entropy", &es);
        p->abyss_skill = skill;
        p->abyss_x = tx;
        p->abyss_y = ty;
        p->abyss_ticks = 50 * (100 + (el > 0 ? skill_ln(es, 9, 10, el) : 0)) / 100;
        Missile *mi = missiles_effect("abysscenter", tx, ty);
        if (mi)
            mi->life = p->abyss_ticks;
        return 1;
    }
    default:
        return 0;
    }
}

/* ---- Golpes y proyectiles ---- */

/* El % de daño de más de los golpes del Brujo (Tajo: calc1) */
static int war_strike_pct(const Player *p, const SkillDef *s, int lvl)
{
    if (s->kind == SK_MIRRORED_BLADES) /* los golpes de las copias: calc6 (dm12) % del daño */
        return p->hits_done > 1 ? skill_dm(s, 1, 2, lvl) - 100 : 0;
    if (s->kind != SK_CLEAVE)
        return 0;
    return skill_ln(s, 1, 2, lvl) +
           (war_blvl(p, "Mirrored Blades") + war_blvl(p, "Hex Purge") + war_blvl(p, "Eldritch Blast")) * s->param[9];
}

/* Tajo: el golpe alcanza también a los de al lado, en un arco de calc5 x 12 grados */
static void war_after_hit(Player *p, const SkillDef *s, int lvl, int target, int phys, int killed)
{
    (void)phys;
    (void)killed;
    if (s->kind != SK_CLEAVE || p->war_cleaving)
        return;
    float ux, uy;
    units_position(target, 1.0f, &ux, &uy);
    float face = atan2f(uy - p->y, ux - p->x);
    int slices = lvl >= 20 ? 20 : lvl + 10;
    float half = (float)(slices * 12) * PI_F / 360.0f;
    int ids[16], n = units_enemies_near(p->x, p->y, MELEE_RANGE + 0.8f, ids, 16);
    p->war_cleaving = 1;
    for (int k = 0; k < n; k++) {
        if (ids[k] == target)
            continue;
        float x, y;
        units_position(ids[k], 1.0f, &x, &y);
        float d = atan2f(y - p->y, x - p->x) - face;
        while (d > PI_F) d -= 2.0f * PI_F;
        while (d < -PI_F) d += 2.0f * PI_F;
        if (fabsf(d) <= half)
            melee_strike(p, ids[k]);
    }
    p->war_cleaving = 0;
}

/* La velocidad de ataque de más de Tajo y Hojas reflejadas (calc3 = dm34) mientras se pega con ellas */
static int war_ias(const Player *p)
{
    if (p->cls != 7 || p->state != PS_ATTACK || p->attack_skill < 0)
        return 0;
    const SkillDef *s = skills_get(p->cls, p->attack_skill);
    return s->kind == SK_CLEAVE || s->kind == SK_MIRRORED_BLADES ? skill_dm(s, 3, 4, eff_lvl(p, p->attack_skill)) : 0;
}

/* El % del daño de un proyectil del Brujo al pegar (el Anillo de fuego: 100 + 5 por cuadro que lleva) */
static int war_missile_pct(const Missile *mi)
{
    return mi->trail && !strcmp(mi->trail, "war:ring") ? 100 + mi->age * 5 : 100;
}

/* Un proyectil del Brujo pegó: el maleficio (y la nube de la miasma la pone war_missile_end) */
static void war_missile_hit(Missile *mi, int unit, int killed)
{
    if (mi->owner < 0 || mi->owner >= s_num_players)
        return;
    Player *p = &s_pl[mi->owner];
    if (p->cls == 7)
        war_hit(p, unit, killed);
}

/* Al terminar: la Descarga de miasma deja su nube; la Hoja de distorsión lleva al Brujo */
static void war_missile_end(Missile *mi)
{
    if (!mi->trail || mi->owner < 0 || mi->owner >= s_num_players)
        return;
    Player *p = &s_pl[mi->owner];
    if (!strcmp(mi->trail, "war:miasma")) {
        war_cloud(p, "miasmaboltcloud", mi->x, mi->y, mi->count > 0 ? mi->count : 1);
    } else if (!strcmp(mi->trail, "war:warp")) {
        missiles_effect("bladewarpexplode", mi->x, mi->y);
        if (p->state < PS_DYING && !player_blocked(mi->x, mi->y, mi->x, mi->y) && world_level_at(mi->x, mi->y) == p->level) {
            missiles_effect("teleport", p->x, p->y);
            p->x = p->prev_x = mi->x;
            p->y = p->prev_y = mi->y;
            overlays_add("teleport", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
        }
    }
}

/* ---- Lo que recibe ---- */

/* Resguardo psíquico (absorbe) y Juramento de sangre (parte va al demonio); devuelve lo que queda */
static int war_take_damage(Player *p, int dmg)
{
    if (p->ward > 0 && dmg > 0) {
        int a = dmg < p->ward ? dmg : p->ward;
        p->ward -= a;
        dmg -= a;
        overlays_add("psychicward_hit0", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 0);
        if (p->ward <= 0)
            overlays_remove("psychicward", OV_AT_PLAYER, (int)(p - s_pl));
    }
    const SkillDef *bos;
    int bo = war_lvl(p, "Blood Oath", &bos);
    if (bo > 0 && dmg > 1) {
        int ids[8], n = war_demons(p, ids, 8), best = -1, best_hp = 0;
        for (int k = 0; k < n; k++) {
            int mx, hp = units_pet_hp(ids[k], &mx);
            if (hp > best_hp) {
                best_hp = hp;
                best = ids[k];
            }
        }
        if (best >= 0) {
            int part = dmg * skill_dm(bos, 1, 2, bo) / 100;
            int pdr = skill_dm(bos, 9, 10, bo);
            if (part > 0) {
                units_pet_damage(best, part * (100 - (pdr < 50 ? pdr : 50)) / 100);
                dmg -= part;
            }
        }
    }
    return dmg;
}

/* Un monstruo le pegó cuerpo a cuerpo: el Resguardo psíquico lo aturde */
static void war_struck(Player *p, int attacker)
{
    if (p->ward <= 0 || p->ward_skill < 0 || !units_alive_enemy(attacker))
        return;
    const SkillDef *s = skills_get(p->cls, p->ward_skill);
    int stun = skill_dm(s, 1, 2, eff_lvl(p, p->ward_skill));
    float ux, uy;
    units_position(attacker, 1.0f, &ux, &uy);
    units_stun(attacker, stun > 0 ? stun : 25);
    overlays_add("stun", OV_AT_UNIT, attacker, ux, uy, stun);
}

/* ---- Las pasivas y lo que dura ---- */

/* Lo del Brujo en refresh_passives: la Levitación (con un arma), el
   maleficio puesto y Consumir */
static void war_passives(Player *p)
{
    const SkillDef *s;
    int lvl = war_lvl(p, "Levitate", &s);
    /* passive_mastery_item_req_percent = -min(ln78, 50): las armas piden menos */
    p->war_req_pct = 0;
    if (lvl > 0) {
        int req = skill_ln(s, 7, 8, lvl);
        p->war_req_pct = req < 50 ? req : 50;
    }
    if (p->consume_ticks > 0) /* Consumir: el % de daño de algunos demonios (damagepercent) */
        for (int k = 0; k < p->consume_n; k++)
            if (p->consume_stat[k] == 25)
                p->sk_dmg_pct += p->consume_val[k];
    if (lvl > 0 && p->equip[SLOT_WEAPON].type) {
        p->sk_dmg_pct += skill_ln(s, 3, 4, lvl);
        p->sk_ar_pct += skill_ln(s, 5, 6, lvl);
        int crit = skill_dm(s, 1, 2, lvl);
        p->sk_crit = 100 - (100 - p->sk_crit) * (100 - crit) / 100;
    }
    if (p->hex_ticks > 0 && p->hex_skill >= 0) {
        const SkillDef *h = skills_get(p->cls, p->hex_skill);
        int hl = eff_lvl(p, p->hex_skill), en = war_blvl(p, "Engorge");
        SkillCtx c = pctx(p);
        p->sk_ar_pct += skill_tohit(h, hl);
        switch (h->kind) {
        case SK_HEX_BANE: { /* daño mágico en el arma */
            int lo, hi;
            skill_elem_syn(h, hl, &c, &lo, &hi);
            p->bonus[STAT_MAG_MIN] += lo;
            p->bonus[STAT_MAG_MAX] += hi;
            break;
        }
        case SK_HEX_SIPHON: /* vida y maná por cada muerte */
            p->bonus[STAT_HEAL_KILL] += skill_ln(h, 1, 2, hl) + en * h->param[9];
            p->bonus[STAT_MANA_KILL] += skill_ln(h, 5, 6, hl) + en * h->param[9];
            break;
        default: { /* velocidad de ataque */
            int ias = skill_ln(h, 5, 6, hl);
            p->bonus[STAT_IAS] += ias < 30 ? ias : 30;
            break;
        }
        }
    }
    if (p->consume_ticks > 0) {
        p->bonus[STAT_LIFE_PCT] += p->consume_life;
        p->sk_speed += p->consume_speed;
    }
}

static void war_tick(Player *p)
{
    if (p->cls != 7)
        return;
    int me = (int)(p - s_pl);
    for (int k = 0; k < MAX_CLASS_SKILLS; k++)
        if (p->war_cd[k] > 0)
            p->war_cd[k]--;
    if (p->hex_ticks > 0 && --p->hex_ticks == 0)
        refresh_stats(p);
    if (p->consume_ticks > 0 && --p->consume_ticks == 0)
        refresh_stats(p);
    if (p->ward > 0 && s_ticks % (2 * TICK_RATE) == 0) /* el escudo mental se ve mientras le quede */
        overlays_add("psychicward", OV_AT_PLAYER, me, p->x, p->y, 2 * TICK_RATE);
    if (p->eldritch_ticks > 0) {
        p->eldritch_ticks--;
        if (--p->eldritch_next <= 0) {
            p->eldritch_next = WAR_BLAST;
            war_blast(p);
        }
    }
    if (p->bind_unit >= 0 && !(units_converted(p->bind_unit) && units_pet_alive(p->bind_unit))) {
        p->bind_unit = -1;
        p->bind_aura = -1;
    }
    war_bind_tick(p);
    war_defilers_tick(p);
    if (s_ticks % AURA_SHOW == 0) { /* las auras de los demonios se ven en ellos */
        const AuraLook *rf = aura_look(SK_RESIST_FIRE);
        int tl = war_lvl(p, "Summon Tainted", NULL);
        for (int k = 0; k < NEC_PETS && rf && tl > 0; k++)
            if (p->pet_kind[k] == PK_TAINTED && units_pet_alive(p->pets[k])) {
                float x, y;
                units_position(p->pets[k], 1.0f, &x, &y);
                if (rf->back)
                    overlays_add(rf->back, OV_AT_UNIT, p->pets[k], x, y, AURA_SHOW);
            }
        const AuraLook *bl = p->bind_aura >= 0 && p->bind_unit >= 0 ? aura_look(skills_get(3, p->bind_aura)->kind) : NULL;
        if (bl) {
            float x, y;
            units_position(p->bind_unit, 1.0f, &x, &y);
            if (bl->back)
                overlays_add(bl->back, OV_AT_UNIT, p->bind_unit, x, y, AURA_SHOW);
            if (bl->front)
                overlays_add(bl->front, OV_AT_UNIT, p->bind_unit, x, y, AURA_SHOW);
        }
    }
    if (p->abyss_ticks > 0) { /* el Abismo atrae; cada 12 cuadros daña; al cerrarse estalla */
        const SkillDef *s = skills_get(p->cls, p->abyss_skill);
        int lvl = eff_lvl(p, p->abyss_skill), lo, hi;
        SkillCtx c = pctx(p);
        skill_elem_syn(s, lvl, &c, &lo, &hi);
        const SkillDef *es;
        int el = war_lvl(p, "Enhanced Entropy", &es), pct = el > 0 ? skill_ln(es, 7, 8, el) : 0;
        lo = lo * (100 + pct) / 100;
        hi = hi * (100 + pct) / 100;
        float pull = 7.0f * WAR_YARD + (float)el / 5.0f;
        int ids[32], n = units_enemies_near(p->abyss_x, p->abyss_y, pull, ids, 32);
        if (p->abyss_ticks % 3 == 0)
            for (int k = 0; k < n; k++) {
                float ux, uy;
                units_position(ids[k], 1.0f, &ux, &uy);
                if (!units_is_unique(ids[k]))
                    units_knockback(ids[k], ux + (ux - p->abyss_x), uy + (uy - p->abyss_y), 0.35f);
            }
        if (p->abyss_ticks % 12 == 0)
            war_area(p, p->abyss_x, p->abyss_y, pull, ELEM_MAG, lo / 4, hi / 4, 0, 0);
        if (--p->abyss_ticks == 0) {
            missiles_effect("abyssexplode", p->abyss_x, p->abyss_y);
            audio_play_name("Warlock_Skill_Abyss_Close_03", p->abyss_x, p->abyss_y);
            war_area(p, p->abyss_x, p->abyss_y, (float)skill_ln(s, 1, 2, lvl) * WAR_YARD + 0.5f, ELEM_MAG, lo, hi, 0, 0);
        }
    }
}

/* Lo de todos (una vez por tick): los sigilos y las marcas de la Purga */
static void war_world_tick(void)
{
    war_sigils_tick();
    for (int k = 0; k < WAR_PURGES; k++)
        if (s_purges[k].ticks > 0 && (--s_purges[k].ticks == 0 || !units_alive_enemy(s_purges[k].unit)))
            s_purges[k].ticks = 0;
}

/* Termina todo (al empezar una ronda de la prueba) */
static void war_clear(Player *p)
{
    int me = (int)(p - s_pl);
    for (int k = 0; k < NEC_PETS; k++)
        if (war_is_demon_kind(p->pet_kind[k]))
            nec_pet_drop(p, k);
    if (p->bind_unit >= 0 && units_converted(p->bind_unit))
        units_pet_remove(p->bind_unit);
    p->bind_unit = p->bind_aura = p->bind_ch_skill = -1;
    p->hex_ticks = p->ward = p->eldritch_ticks = p->consume_ticks = p->abyss_ticks = p->consume_n = 0;
    for (int k = 0; k < WAR_SIGILS; k++)
        if (s_sigils[k].owner == me)
            s_sigils[k].used = 0;
    memset(p->war_cd, 0, sizeof p->war_cd);
}

/* ---- Los íconos ----
   waskillicon.dc6 de D2R es una copia del del Druida (el Brujo solo tiene
   íconos HD). Cada ícono se arma con lo que se ve de la habilidad: su
   proyectil, su efecto o el demonio que invoca (el cuadro del medio,
   achicado sobre un fondo oscuro con borde); los sigilos con el tono de su
   estado (states.txt). Dos cuadros por habilidad, como en los .dc6: el
   segundo, más oscuro. */

#define WAR_ICON 48

static const struct {
    const char *key, *file;
    int         hue; /* tabla de tonos (colorshift) o -1 */
} k_war_icons[] = {
    { "Summon Goatman", "monsters/gm/tr/gmtrlitnu2hs", -1 },
    { "Demonic Mastery", "overlays/warlockdemontorso", -1 },
    { "Death Mark", "overlays/deathmark", -1 },
    { "Summon Tainted", "monsters/bh/tr/bhtrlitnuhth", -1 },
    { "Summon Defiler", "monsters/45/tr/45trlitnuhth", -1 },
    { "Blood Oath", "overlays/warlockdemonground", -1 },
    { "Engorge", "overlays/engorgestate", -1 },
    { "Blood Boil", "missiles/exparrowexplode", 107 },
    { "Consume", "overlays/warlock_consume", -1 },
    { "Bind Demon", "overlays/warlock_hex_enchant", 130 },
    { "Levitate", "missiles/blessedhammer", -1 },
    { "Eldritch Blast", "missiles/warcryshockwave", -1 },
    { "Hex Bane", "overlays/hexbanedebuffeffect", -1 },
    { "Hex Siphon", "overlays/hexsyphondebuffeffect", -1 },
    { "Psychic Ward", "overlays/manashieldball", -1 },
    { "Echoing Strike", "missiles/blessedhammer", 106 },
    { "Hex Purge", "overlays/hexpurgedebuffeffect", -1 },
    { "Blade Warp", "missiles/exparrowexplode", -1 },
    { "Cleave", "overlays/warlock_psychic_cast_1", -1 },
    { "Mirrored Blades", "overlays/warlock_psychic_cast_2", -1 },
    { "Sigil Lethargy", "missiles/grimwardsmall", 103 },
    { "Ring of Fire", "missiles/fireball", -1 },
    { "Miasma Bolt", "missiles/holyboltmissile", -1 },
    { "Sigil Rancor", "missiles/grimwardmedium", 106 },
    { "Enhanced Entropy", "missiles/poisonsparks", -1 },
    { "Flame Wave", "missiles/groundfirebig", -1 },
    { "Miasma Chains", "missiles/poisonsmokepuff", -1 },
    { "Sigil Death", "missiles/grimwardlarge", 107 },
    { "Apocalypse", "overlays/apocalypse", -1 },
    { "Abyss", "missiles/meteortargetindicator", -1 },
};

/* Un cuadro de WAR_ICON x WAR_ICON con `src` achicado al centro */
static void war_icon_frame(SpriteFrame *dst, const SpriteFrame *src, const uint8_t *hue, int dark)
{
    int n = WAR_ICON;
    dst->width = dst->height = n;
    dst->off_x = dst->off_y = 0;
    dst->spans = NULL;
    dst->pixels = calloc((size_t)n * (size_t)n, 1);
    if (!dst->pixels)
        return;
    uint8_t bg = COL(R_GRAY, 1), edge = COL(R_STONE, 7);
    memset(dst->pixels, bg, (size_t)n * (size_t)n);
    if (src && src->width > 0 && src->height > 0) {
        int box = n - 6, w = src->width, h = src->height;
        int big = w > h ? w : h, dw = w * box / big, dh = h * box / big;
        if (big < box) { /* los chicos se agrandan hasta el doble como mucho */
            int k = box / big > 2 ? 2 : box / big;
            dw = w * k;
            dh = h * k;
        }
        dw = dw < 1 ? 1 : dw;
        dh = dh < 1 ? 1 : dh;
        int x0 = (n - dw) / 2, y0 = (n - dh) / 2;
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                uint8_t c = src->pixels[(y * h / dh) * w + x * w / dw];
                if (!c)
                    continue;
                if (hue)
                    c = hue[c];
                dst->pixels[(y0 + y) * n + x0 + x] = c ? c : bg;
            }
    }
    for (int k = 0; k < n; k++) { /* el borde */
        dst->pixels[k] = dst->pixels[(n - 1) * n + k] = edge;
        dst->pixels[k * n] = dst->pixels[k * n + n - 1] = edge;
    }
    if (dark)
        for (int k = 0; k < n * n; k++)
            dst->pixels[k] = g_light[14][dst->pixels[k]];
}

/* La hoja de íconos del Brujo (frames en el orden de IconCel). Si están sus
   íconos HD pasados a 48x48 con la paleta (tools/hd_icons.py, desde el
   waskillicon.sprite del Resurrected), esos; si no, armados con los efectos. */
static int war_icons_build(Sprite *out)
{
    memset(out, 0, sizeof *out);
    size_t hn;
    uint8_t *hd = fs_read("data/global/ui/spells/waskillicon.d2rw", &hn);
    if (hd) {
        int ok = raw_load(hd, hn, out) == 0;
        free(hd);
        if (ok)
            return 1;
    }
    int cls = 7, most = 0;
    for (int i = 0; i < skills_count(cls); i++)
        if (skills_get(cls, i)->icon + 2 > most)
            most = skills_get(cls, i)->icon + 2;
    if (most <= 0)
        return 0;
    out->directions = 1;
    out->frames_per_dir = most;
    out->frames = calloc((size_t)most, sizeof *out->frames);
    if (!out->frames)
        return 0;
    for (int f = 0; f < most; f++) /* (los que no son de nadie: vacíos) */
        war_icon_frame(&out->frames[f], NULL, NULL, f & 1);
    for (unsigned k = 0; k < sizeof k_war_icons / sizeof k_war_icons[0]; k++) {
        int i = skills_find(cls, k_war_icons[k].key);
        if (i < 0)
            continue;
        int cel = skills_get(cls, i)->icon;
        char path[96];
        snprintf(path, sizeof path, "data/global/%s.dcc", k_war_icons[k].file);
        size_t n;
        uint8_t *d = fs_read(path, &n);
        Sprite sp;
        memset(&sp, 0, sizeof sp);
        int ok = d && dcc_load(d, n, &sp) == 0 && sp.frames_per_dir > 0;
        free(d);
        const SpriteFrame *src = ok ? &sp.frames[sp.frames_per_dir / 2] : NULL;
        const uint8_t *hue = k_war_icons[k].hue >= 0 ? fb_hue(k_war_icons[k].hue) : NULL;
        for (int dark = 0; dark < 2 && cel + dark < most; dark++) {
            free(out->frames[cel + dark].pixels);
            war_icon_frame(&out->frames[cel + dark], src, hue, dark);
        }
        if (ok)
            sprite_free(&sp);
    }
    return 1;
}
