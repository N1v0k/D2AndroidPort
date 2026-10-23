/* Prueba: D2_UBERTEST=1 (con D2_DIFF=2 y D2_ACT=5) recorre el evento
   Pandemonium sin caminar (endgame.inc; se incluye desde game.c, después de
   cowtest.inc): que caigan las llaves y los órganos de sus TC, la receta
   donde no va, los tres portales en Harrogath (y que no haya un cuarto), cada
   mapa con su jefe y su órgano, el portal a Tristram, los tres Uber con sus
   esbirros (un rato al lado, con D2_GOD) y la Antorcha y los Estandartes.
   Cada cosa dice BIEN o MAL. */
static int    s_ubt_bad;
static double ubt_t0;
#define UBT_PERF 400 /* los ticks que se mide al lado de los jefes */

static void ubt_check(int ok, const char *what)
{
    printf("UBER: %s %s\n", ok ? "BIEN" : "MAL ", what);
    s_ubt_bad += !ok;
}

/* Cuántas veces sale `code` en n tiradas de la clase de tesoro */
static int ubt_tc_count(const char *tc, const char *code, int n)
{
    int found = 0;
    for (int k = 0; k < n; k++) {
        Item out[16];
        int m = items_roll_drop(tc, 85, 0, out, 16);
        for (int i = 0; i < m; i++)
            found += out[i].type && strcmp(out[i].type->code, code) == 0;
    }
    return found;
}

static void ubt_fill(Player *p, const char *a, const char *b, const char *c)
{
    p->cube_count = 0;
    const char *codes[3] = { a, b, c };
    for (int k = 0; k < 3; k++) {
        Item it = items_make(items_find(codes[k]));
        cube_add(p, &it);
    }
}

/* El primer objeto con ese código en el suelo a menos de r: lo levanta (1) o no está (0) */
static int ubt_pick(Player *p, const char *code, float x, float y, float r)
{
    for (int i = 0; i < items_ground_count(); i++) {
        float ix, iy;
        const Item *it = items_ground_item(i, &ix, &iy);
        if (it && it->type && strcmp(it->type->code, code) == 0 && (ix - x) * (ix - x) + (iy - y) * (iy - y) <= r * r) {
            Item got = *it;
            items_ground_remove(i);
            bag_add(p, &got);
            return 1;
        }
    }
    return 0;
}

static int ubt_count_near(const char *id, float x, float y, float r)
{
    static int ids[512];
    int n = units_find_all(id, ids, 512), c = 0;
    for (int i = 0; i < n; i++) {
        float ux, uy;
        units_position(ids[i], 1.0f, &ux, &uy);
        c += !units_is_corpse(ids[i]) && (ux - x) * (ux - x) + (uy - y) * (uy - y) <= r * r;
    }
    return c;
}

static void uber_test(void)
{
    static int step, wait, k, order[3];
    static float bx, by;
    static const char *const k_boss[3] = { "uberandariel", "uberduriel", "uberizual" };
    static const char *const k_organ[3] = { "dhn", "bey", "mbr" };
    if (!getenv("D2_UBERTEST") || s_act != 5 || step < 0)
        return;
    Player *p = &s_pl[0];
    if ((int)s_ticks < wait)
        return;
    wait = (int)s_ticks + 3;
    int town = world_level_zone(UBER_TOWN);
    switch (step++) {
    case 0: {
        int ok = 1;
        for (int i = 0; i < 4; i++)
            ok &= world_level_zone(UBER_FIRST + i) >= 0;
        ubt_check(s_diff == 2, "la partida es en Infierno");
        ubt_check(ok, "los cuatro mapas del evento están en el mundo del Acto 5");
        int k1 = ubt_tc_count("Countess Item (H)", "pk1", 500), k2 = ubt_tc_count("Summoner (H)", "pk2", 500);
        int k3 = ubt_tc_count("Nihlathak (H)", "pk3", 500);
        printf("UBER: en 500 tiradas: %d llaves del Terror, %d del Odio, %d de la Destrucción\n", k1, k2, k3);
        ubt_check(k1 > 0 && k2 > 0 && k3 > 0, "la Condesa, el Invocador y Nihlathak sueltan sus llaves en Infierno");
        int o1 = ubt_tc_count("Uber Andariel", "dhn", 5), o2 = ubt_tc_count("Uber Duriel", "bey", 5);
        int o3 = ubt_tc_count("Uber Izual", "mbr", 5);
        ubt_check(o1 == 5 && o2 == 5 && o3 == 5, "los tres jefes sueltan siempre su órgano");
        int e = ubt_tc_count("Andarielq (H)", "tes", 300);
        printf("UBER: en 300 tiradas de Andariel, %d esencias\n", e);
        ubt_check(e > 0, "Andariel suelta la esencia del sufrimiento");
        /* El Token: las cuatro esencias en el cubo, y al usarlo vuelven todos los puntos */
        p->cube_count = 0;
        static const char *const k_ess[4] = { "tes", "ceh", "bet", "fed" };
        for (int i = 0; i < 4; i++) {
            Item it = items_make(items_find(k_ess[i]));
            cube_add(p, &it);
        }
        cube_transmute_now(p);
        ubt_check(p->cube_count == 1 && p->cube[0].type && strcmp(p->cube[0].type->code, "toa") == 0,
                  "las cuatro esencias dan el Token of Absolution");
        if (p->cube_count == 1) {
            Item tok = p->cube[0];
            p->cube_count = 0;
            bag_add(p, &tok);
        }
        p->stat_add[0] = 10;
        p->skill_lvl[0] = 5;
        int sp = p->stat_points, kp = p->skill_points, ti = bag_index_of(p, "toa");
        ubt_check(ti >= 0 && endgame_use_item(p, ti), "se usa el Token");
        printf("UBER: dice \"%s\"\n", p->message);
        ubt_check(p->stat_add[0] == 0 && p->skill_lvl[0] == 0 && p->stat_points >= sp + 10 && p->skill_points >= kp + 5 &&
                      bag_index_of(p, "toa") < 0,
                  "el Token devuelve los puntos de atributo y de habilidad (y se gasta)");
        if (!ok) {
            step = -1;
            break;
        }
        a3t_go(p, 110); /* el Bloody Foothills: ahí no */
        break;
    }
    case 1:
        ubt_fill(p, "pk1", "pk2", "pk3");
        cube_transmute_now(p);
        printf("UBER: dice \"%s\"\n", p->message);
        ubt_check(!s_uber_open[0] && !s_uber_open[1] && !s_uber_open[2] && p->cube_count == 3,
                  "fuera de Harrogath no se abre (y no gasta las llaves)");
        place_at(p, 0, s_town_x, s_town_y);
        break;
    case 2: {
        int opened = 0;
        for (int i = 0; i < 3; i++) {
            int before = s_uber_open[0] + s_uber_open[1] + s_uber_open[2];
            ubt_fill(p, "pk1", "pk2", "pk3");
            cube_transmute_now(p);
            for (int j = 0; j < 3; j++)
                if (s_uber_open[j] && !(opened & (1 << j)))
                    order[i] = j, opened |= 1 << j;
            ubt_check(s_uber_open[0] + s_uber_open[1] + s_uber_open[2] == before + 1 && p->cube_count == 0,
                      "las llaves abren un portal (y se gastan)");
            place_at(p, 0, p->x + 4.0f, p->y); /* (que el próximo no salga encima) */
            find_free_spot(&p->x, &p->y);
        }
        printf("UBER: se abrieron en el orden %d, %d, %d\n", order[0], order[1], order[2]);
        ubt_fill(p, "pk1", "pk2", "pk3");
        cube_transmute_now(p);
        printf("UBER: dice \"%s\"\n", p->message);
        ubt_check(p->cube_count == 3, "no hay un cuarto portal en la misma partida");
        p->cube_count = 0;
        int ids[8];
        ubt_check(units_find_all("PortalPermanent", ids, 8) >= 3, "se ven los tres portales rojos en Harrogath");
        k = 0;
        break;
    }
    case 3: { /* uno de los tres mapas: pasar por el portal */
        const Warp *w = cowt_portal(PORTAL_UBER(k), town);
        ubt_check(w && w->to == world_level_zone(UBER_FIRST + k), "el portal lleva a su mapa");
        if (!w) {
            step = -1;
            break;
        }
        place_at(p, 0, (w->x0 + w->x1) / 2.0f, (w->y0 + w->y1) / 2.0f);
        use_warp(p, w);
        wait = (int)s_ticks + 20;
        break;
    }
    case 4: {
        const Level *lv = world_level_at(p->x, p->y);
        printf("UBER: en %s (%.0f, %.0f)\n", lv ? str_get(lv->name) : "?", p->x, p->y);
        ubt_check(lv && lv->id == UBER_FIRST + k, "llegó al mapa del portal");
        ubt_check(cowt_portal(PORTAL_UBER_BACK(k), world_level_zone(UBER_FIRST + k)) != NULL, "está el portal de vuelta");
        int id = -1;
        units_find_all(k_boss[k], &id, 1);
        ubt_check(id >= 0, "el jefe del mapa está");
        if (id < 0) {
            step = 6;
            break;
        }
        units_position(id, 1.0f, &bx, &by);
        float d = sqrtf((bx - p->x) * (bx - p->x) + (by - p->y) * (by - p->y));
        printf("UBER: %s a %.0f subtiles de la llegada; %d monstruos en el acto\n", k_boss[k], d, units_count());
        place_at(p, 0, bx + 5.0f, by + 5.0f);
        find_free_spot(&p->x, &p->y);
        ubt_t0 = wall_ms();
        wait = (int)s_ticks + UBT_PERF;
        break;
    }
    case 5: {
        printf("UBER: al lado de %s, %.2f ms por tick\n", k_boss[k], (wall_ms() - ubt_t0) / UBT_PERF);
        int id = -1;
        units_find_all(k_boss[k], &id, 1);
        if (id >= 0) { /* (como si lo matara el jugador: su botín) */
            units_position(id, 1.0f, &bx, &by);
            units_damage(id, 1 << 24);
            drop_loot(id, p);
        }
        wait = (int)s_ticks + 80;
        break;
    }
    case 6: {
        ubt_check(ubt_pick(p, k_organ[k], bx, by, 6.0f), "cayó su órgano (y lo levanta)");
        const Warp *w = cowt_portal(PORTAL_UBER_BACK(k), world_level_zone(UBER_FIRST + k));
        if (w) {
            place_at(p, 0, (w->x0 + w->x1) / 2.0f, (w->y0 + w->y1) / 2.0f);
            use_warp(p, w);
        }
        wait = (int)s_ticks + 20;
        if (++k < 3)
            step = 3;
        break;
    }
    case 7: {
        const Level *lv = world_level_at(p->x, p->y);
        ubt_check(lv && lv->id == UBER_TOWN, "volvió a Harrogath");
        ubt_check(player_has(p, "dhn") && player_has(p, "bey") && player_has(p, "mbr"), "tiene los tres órganos");
        ubt_fill(p, "dhn", "bey", "mbr");
        cube_transmute_now(p);
        printf("UBER: dice \"%s\"\n", p->message);
        ubt_check(s_uber_open[3] && p->cube_count == 0, "los órganos abren el portal a Tristram");
        ubt_fill(p, "dhn", "bey", "mbr");
        cube_transmute_now(p);
        ubt_check(p->cube_count == 3, "no se abre otro a Tristram en la misma partida");
        p->cube_count = 0;
        const Warp *w = cowt_portal(PORTAL_UBER(3), town);
        if (!w) {
            ubt_check(0, "está el portal a Tristram");
            step = -1;
            break;
        }
        place_at(p, 0, (w->x0 + w->x1) / 2.0f, (w->y0 + w->y1) / 2.0f);
        use_warp(p, w);
        wait = (int)s_ticks + 20;
        break;
    }
    case 8: {
        const Level *lv = world_level_at(p->x, p->y);
        ubt_check(lv && lv->id == UBER_FIRST + 3, "llegó a Uber Tristram");
        static const char *const k_evils[3] = { "ubermephisto", "uberdiablo", "uberbaal" };
        int all = 1, id = -1;
        for (int e = 0; e < 3; e++)
            all &= units_find_all(k_evils[e], &id, 1) == 1;
        ubt_check(all, "están Mefisto, Diablo y Baal uber");
        units_find_all("uberdiablo", &id, 1);
        if (id >= 0)
            units_position(id, 1.0f, &bx, &by);
        place_at(p, 0, bx + 4.0f, by + 4.0f);
        find_free_spot(&p->x, &p->y);
        ubt_t0 = wall_ms();
        wait = (int)s_ticks + UBT_PERF;
        break;
    }
    case 9: {
        printf("UBER: con los tres, %.2f ms por tick; %d unidades\n", (wall_ms() - ubt_t0) / UBT_PERF, units_count());
        static const char *const k_minions[] = { "skeleton8", "sk_archer11", "skmage_fire7", "skmage_ltng7", "skmage_cold6",
                                                 "skmage_pois7", "demonhole", "megademon6", "wraith9", "vampire9" };
        int total = 0;
        for (unsigned m = 0; m < sizeof k_minions / sizeof k_minions[0]; m++) {
            int c = ubt_count_near(k_minions[m], bx, by, 60.0f);
            if (c)
                printf("UBER:   %d %s\n", c, k_minions[m]);
            total += c;
        }
        ubt_check(total > 0, "los Uber llaman a sus esbirros");
        static const char *const k_evils[3] = { "ubermephisto", "uberdiablo", "uberbaal" };
        for (int e = 0; e < 3; e++) {
            int id = -1;
            if (units_find_all(k_evils[e], &id, 1) == 1) {
                units_position(id, 1.0f, &bx, &by);
                units_damage(id, 1 << 24);
            }
        }
        wait = (int)s_ticks + 80;
        break;
    }
    case 10: {
        ubt_check(s_uber_torch, "al morir los tres se anota la Antorcha");
        int torch = 0, cls = -1, val = 0;
        for (int i = 0; i < items_ground_count(); i++) {
            float ix, iy;
            const Item *it = items_ground_item(i, &ix, &iy);
            if (!it || !it->type || strcmp(it->type->code, "cm2") || it->quality != QUALITY_UNIQUE)
                continue;
            torch++;
            for (int s = 0; s < it->num_stats; s++)
                if (it->stat[s] == STAT_CLASS_SKILLS)
                    cls = it->param[s], val = it->value[s];
            printf("UBER: cayó %s (+%d a las habilidades de la clase %d)\n", it->name, val, cls);
        }
        ubt_check(torch == 1 && val == 3, "cayó la Antorcha de fuego infernal, con +3 a una clase");
        ubt_check(cowt_ground("std", bx, by, 8.0f) == s_num_players, "un Estandarte de los héroes por jugador");
        /* El Diablo clon: vender una Piedra de Jordán en Infierno, y el próximo superunique es él */
        Item soj = items_make(items_find("rin"));
        ubt_check(items_make_unique_named(&soj, "The Stone of Jordan"), "se arma una Piedra de Jordán");
        dclone_sold(p, &soj);
        printf("UBER: dice \"%s\"\n", p->message);
        ubt_check(s_dclone_walks, "al venderla en Infierno, Diablo camina sobre la tierra");
        a3t_go(p, 110);
        int su = units_spawn_superunique("Dac Farren", p->x + 6.0f, p->y, 80);
        ubt_check(su >= 0 && strcmp(units_id(su), "diabloclone") == 0, "el próximo superunique es el Diablo clon");
        if (su >= 0) {
            units_position(su, 1.0f, &bx, &by);
            units_damage(su, 1 << 24);
            drop_loot(su, p);
        }
        wait = (int)s_ticks + 40;
        break;
    }
    case 11: {
        int anni = 0;
        for (int i = 0; i < items_ground_count(); i++) {
            float ix, iy;
            const Item *it = items_ground_item(i, &ix, &iy);
            if (it && it->type && !strcmp(it->type->code, "cm1") && it->quality == QUALITY_UNIQUE) {
                anni++;
                printf("UBER: cayó %s\n", it->name);
            }
        }
        ubt_check(anni == 1, "el clon suelta la Annihilus");
        /* Los Ancianos colosales: las cinco estatuas en el cubo, en el Acto 5 */
        p->cube_count = 0;
        static const char *const k_ua[5] = { "ua1", "ua2", "ua3", "ua4", "ua5" };
        for (int i = 0; i < 5; i++) {
            Item it = items_make(items_find(k_ua[i]));
            cube_add(p, &it);
        }
        cube_transmute_now(p);
        printf("UBER: dice \"%s\"\n", p->message);
        ubt_check(s_uber_open[4] && p->cube_count == 0, "las cinco estatuas abren el portal a la Colossal Summit");
        const Warp *w = cowt_portal(PORTAL_UBER(4), s_uber_from[4]);
        if (!w) {
            ubt_check(0, "está el portal a la cumbre");
            step = -1;
            break;
        }
        place_at(p, 0, (w->x0 + w->x1) / 2.0f, (w->y0 + w->y1) / 2.0f);
        use_warp(p, w);
        wait = (int)s_ticks + 20;
        break;
    }
    case 12: {
        const Level *lv = world_level_at(p->x, p->y);
        ubt_check(lv && lv->id == UBER_FIRST + 4, "llegó a la Colossal Summit");
        int n = 0;
        for (int c = 0; c < 3; c++)
            n += s_col_ids[c] >= 0;
        ubt_check(n == 3, "están Talic, Madawc y Korlic colosales");
        if (s_col_ids[1] >= 0)
            units_position(s_col_ids[1], 1.0f, &bx, &by);
        place_at(p, 0, bx + 3.0f, by + 3.0f);
        find_free_spot(&p->x, &p->y);
        ubt_t0 = wall_ms();
        wait = (int)s_ticks + UBT_PERF;
        break;
    }
    case 13: {
        printf("UBER: con los colosales, %.2f ms por tick; resistencias menos %d/%d/%d\n", (wall_ms() - ubt_t0) / UBT_PERF, p->pierce[0],
               p->pierce[1], p->pierce[2]);
        int spirits = ubt_count_near("colbarbwhirl", bx, by, 40.0f) + ubt_count_near("colbarbthrow", bx, by, 40.0f) +
                      ubt_count_near("colbarbfrenzy", bx, by, 40.0f);
        printf("UBER: %d espíritus\n", spirits);
        ubt_check(spirits > 0, "los colosales llaman a sus espíritus");
        ubt_check(p->pierce[0] > 0 || p->pierce[1] > 0 || p->pierce[2] > 0, "su aura le quita resistencia al jugador");
        /* Madawc muere último: su Joya (de rayo o de magia) */
        for (int c = 0; c < 3; c++)
            if (c != 1 && s_col_ids[c] >= 0) {
                units_damage(s_col_ids[c], 1 << 24);
                drop_loot(s_col_ids[c], p);
            }
        wait = (int)s_ticks + 10;
        break;
    }
    case 14: {
        if (s_col_ids[1] >= 0) {
            units_position(s_col_ids[1], 1.0f, &bx, &by);
            units_damage(s_col_ids[1], 1 << 24);
            drop_loot(s_col_ids[1], p);
        }
        wait = (int)s_ticks + 30;
        break;
    }
    case 15: {
        int jewels = 0, from_madawc = 0;
        for (int i = 0; i < items_ground_count(); i++) {
            float ix, iy;
            const Item *it = items_ground_item(i, &ix, &iy);
            if (it && it->type && !strcmp(it->type->code, "cjw")) {
                jewels++;
                from_madawc += strstr(items_unique_key(it), "Guardian") != NULL;
                printf("UBER: cayó %s\n", it->name);
            }
        }
        printf("UBER: último %d, TC \"%s\", \"%s\", \"%s\", soltó %d\n", s_col_last, s_col_tc[0], s_col_tc[1], s_col_tc[2], s_col_dropped);
        ubt_check(jewels == 1 && from_madawc == 1, "solo el último (Madawc) suelta una Joya colosal, de las suyas");
        printf("UBER: terminado, %d MAL\n", s_ubt_bad);
        step = -1;
        break;
    }
    }
}
