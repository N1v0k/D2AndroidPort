/* Prueba: D2_A4QTEST=1 (con D2_ACT=4) recorre las misiones del Acto 4 sin
   caminar (se incluye desde game.c, después de la del Acto 3): Tyrael se
   presenta y pide a Izual; llega al Plains, mata a Izual, le habla al
   espíritu y cobra; Caín pide la Forja (y da la piedra); mata a Hephasto,
   pone la piedra y la rompe con el Martillo (las gemas); Tyrael pide a
   Diablo; abre los cinco sellos, mata a los tres jefes y a Diablo; Tyrael
   abre el portal del final (el epílogo). Muestra el registro en cada paso. */
static void a4t_log(const Player *p, const char *what)
{
    const Level *lv = world_level_at(p->x, p->y);
    printf("A4: %-26s| %-24s| dijo %-28s| izual %d forja %d diablo %d flags %3d puntos %d sellos %d |", what,
           lv ? lv->name : "?", s_last_talk, p->izual_q, p->forge_q, p->diablo_q, p->a4_flags, p->skill_points,
           s_a4_seals);
    for (int q = 0; q < 3; q++) {
        int st;
        const char *g = quest_info(p, 4, q, &st);
        printf(" q%d=%d %s", q + 1, st, g ? g : "-");
    }
    printf("\n");
    s_last_talk[0] = '\0';
}

/* Mata al primero con ese nombre en ese nivel (lo arma) y anota dónde cayó */
static int a4t_kill(const char *id, const char *level, float *kx, float *ky)
{
    const Level *lv = world_level_named(level);
    int u;
    if (!lv || zone_load(lv->zone) != 0 || units_find_all(id, &u, 1) != 1) {
        printf("A4: no está %s en %s\n", id, level);
        return 0;
    }
    units_position(u, 1.0f, kx, ky);
    units_damage(u, 1000000);
    printf("A4: %s herido de muerte\n", units_display_name(u));
    return 1;
}

static void a4_quest_test(void)
{
    static int step, wait;
    static float kx, ky;
    if (!getenv("D2_A4QTEST") || s_act != 4 || step < 0)
        return;
    Player *p = &s_pl[0];
    float tx = s_town_x, ty = s_town_y;
    if ((int)s_ticks < wait)
        return;
    wait = (int)s_ticks + 3;
    switch (step++) {
    case 0:
        a4t_log(p, "inicio");
        a2t_talk(p, "tyrael2");
        a4t_log(p, "Tyrael (se presenta)");
        a2t_talk(p, "tyrael2");
        a4t_log(p, "Tyrael (Izual)");
        a2t_talk(p, "cain4");
        a4t_log(p, "Caín (la Forja)");
        printf("A4: la piedra: %s\n", player_has(p, "mss") ? "la tiene" : "no la tiene");
        a3t_go(p, 105); /* el Plains of Despair */
        break;
    case 1:
        a4t_log(p, "llegó al Plains");
        a4t_kill("izual", "Plains of Despair", &kx, &ky);
        wait = (int)s_ticks + 40;
        break;
    case 2: {
        a4t_log(p, "murió Izual");
        int g;
        if (units_find_all("izualghost", &g, 1) == 1) {
            float gx, gy;
            units_position(g, 1.0f, &gx, &gy);
            place_at(p, 0, gx + 1.0f, gy + 1.0f);
            a2t_talk(p, "izualghost");
            a4t_log(p, "el espíritu de Izual");
        } else {
            printf("A4: no salió el espíritu de Izual\n");
        }
        place_at(p, 0, tx, ty);
        a2t_talk(p, "tyrael2");
        a4t_log(p, "Tyrael (recompensa)");
        a2t_talk(p, "tyrael2");
        a4t_log(p, "Tyrael (Diablo)");
        a4t_kill("The Feature Creep", "River of Flame", &kx, &ky);
        wait = (int)s_ticks + 40;
        break;
    }
    case 3: {
        a3t_pick(p, kx, ky);
        a4t_log(p, "mató a Hephasto");
        /* el Martillo en la mano y la piedra sobre la Forja; tres golpes */
        int k = bag_index_of(p, "hfh");
        if (k >= 0) {
            Item h = p->bag[k];
            bag_remove(p, k);
            if (p->equip[SLOT_WEAPON].type && !bag_add(p, &p->equip[SLOT_WEAPON]))
                printf("A4: no entra el arma\n");
            p->equip[SLOT_WEAPON] = h;
            refresh_stats(p);
        } else {
            printf("A4: no tiene el Martillo\n");
        }
        for (int hit = 0; hit < 4; hit++) {
            a3t_object(p, "SoulStoneForge", "River of Flame");
            printf("A4: Forja (vez %d): %s\n", hit + 1, (p->a4_flags & A4F_STONE) ? "la piedra encima" : "sin la piedra");
        }
        wait = (int)s_ticks + 100;
        break;
    }
    case 4: {
        int gems = 0;
        for (int i = 0; i < 8; i++) {
            float ix, iy;
            int g = items_ground_nearest(s_a4_forge_x, s_a4_forge_y, 4.0f, 0);
            if (g < 0)
                break;
            const Item *it = items_ground_item(g, &ix, &iy);
            printf("A4: gema de la Forja: %s (%s)\n", it->type->name, it->type->code);
            items_ground_remove(g);
            gems++;
        }
        printf("A4: %d gemas\n", gems);
        a4t_log(p, "rompió la piedra");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain4");
        a4t_log(p, "Caín (la Forja)");
        a3t_go(p, 108); /* el Santuario */
        break;
    }
    case 5: { /* los cinco sellos (salen los tres jefes) */
        a4t_log(p, "llegó al Santuario");
        for (int k = 0; k < 5; k++) {
            int u;
            if (units_find_all(k_a4_seals[k], &u, 1) != 1) {
                printf("A4: no está %s\n", k_a4_seals[k]);
                continue;
            }
            float sx, sy;
            units_position(u, 1.0f, &sx, &sy);
            p->x = sx + 1.0f;
            p->y = sy + 1.0f;
            quest_interact(p);
            if (k == 1)
                a4t_log(p, "dos sellos");
        }
        a4t_log(p, "los cinco sellos");
        /* El Santuario se suelta y se vuelve a armar con los jefes vivos:
           salen de nuevo, uno de cada uno */
        zone_unload(world_level_zone(108));
        zone_load(world_level_zone(108));
        for (int b = 0; b < 3; b++) {
            int ids[4], n = units_find_all(k_a4_bosses[b].boss, ids, 4), alive = 0;
            for (int i = 0; i < n; i++)
                alive += !units_is_corpse(ids[i]);
            printf("A4: al rearmar el Santuario, %s: %d\n", k_a4_bosses[b].boss, alive);
        }
        for (int k = 0; k < 5; k++)
            if (s_a4_boss[k] >= 0) {
                printf("A4: jefe del sello %d: %s\n", k + 1, units_display_name(s_a4_boss[k]));
                units_damage(s_a4_boss[k], 1000000);
            }
        wait = (int)s_ticks + 60;
        break;
    }
    case 6:
        a4t_log(p, "murieron los jefes");
        printf("A4: Diablo %s\n", s_a4_diablo_unit >= 0 ? "salió" : "no salió");
        if (s_a4_diablo_unit >= 0)
            units_damage(s_a4_diablo_unit, 10000000);
        wait = (int)s_ticks + 60;
        break;
    case 7: {
        a4t_log(p, "murió Diablo");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain4");
        a4t_log(p, "Caín (Diablo)");
        a2t_talk(p, "tyrael2");
        a4t_log(p, "Tyrael (el portal)");
        a2t_talk(p, "malachai");
        /* Al Acto 3 y de vuelta: el portal no queda en el Acto 3 y sigue al
           lado de Tyrael */
        int portal;
        act_switch(3);
        int in3 = units_find_all("ReallyTheLastPortal", &portal, 1);
        act_switch(4);
        printf("A4: el portal en el Acto 3: %d; al volver al 4: %d\n", in3,
               units_find_all("ReallyTheLastPortal", &portal, 1));
        a3t_object(p, "ReallyTheLastPortal", "The Pandemonium Fortress");
        const char *mv = game_take_movie();
        printf("A4: el portal del final: cinemática %s\n", mv ? mv : "ninguna");
        a4t_log(p, "el final");
        step = -1;
        printf("A4: terminado\n");
        break;
    }
    }
}
