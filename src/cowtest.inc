/* Prueba: D2_COWTEST=1 (en el Acto 1) recorre el nivel de las vacas sin
   caminar (cows.inc; se incluye desde game.c, después de las de las
   misiones): usa el cuerpo de Wirt en Tristram (la pierna y el oro), prueba
   la receta donde no va (sin Baal, fuera del campamento), abre el portal en
   el campamento, pasa por él a la granja, cuenta las vacas, mata al Rey Vaca
   (las pociones y ya no se abre), vuelve por el portal de la granja y mira
   que se guarde. Cada cosa dice BIEN o MAL. */
static int    s_cowt_bad;
static double cowt_t0;
#define COWT_PERF 300 /* los ticks que se mide */

static void cowt_check(int ok, const char *what)
{
    printf("COW: %s %s\n", ok ? "BIEN" : "MAL ", what);
    s_cowt_bad += !ok;
}

/* Cuántos objetos con ese código hay en el suelo a menos de r de (x, y) */
static int cowt_ground(const char *code, float x, float y, float r)
{
    int n = 0;
    for (int i = 0; i < items_ground_count(); i++) {
        float ix, iy;
        const Item *it = items_ground_item(i, &ix, &iy);
        if (it && it->type && strcmp(it->type->code, code) == 0 && (ix - x) * (ix - x) + (iy - y) * (iy - y) <= r * r)
            n++;
    }
    return n;
}

/* La pierna y una pila de pergaminos en el cubo (vacío) */
static void cowt_fill_cube(Player *p, int scrolls)
{
    p->cube_count = 0;
    Item leg = items_make(items_find("leg")), tsc = items_make(items_find("tsc"));
    tsc.amount = scrolls;
    cube_add(p, &leg);
    cube_add(p, &tsc);
}

/* El portal con ese número que sale de la zona `from` */
static const Warp *cowt_portal(int id, int from)
{
    for (int i = 0; i < world_warp_count(); i++) {
        const Warp *w = world_warp(i);
        if (w->portal == id + 1 && w->from == from)
            return w;
    }
    return NULL;
}

static void cow_test(void)
{
    static int step, wait;
    static float wx, wy, kx, ky;
    if (!getenv("D2_COWTEST") || s_act != 1 || step < 0)
        return;
    Player *p = &s_pl[0];
    if ((int)s_ticks < wait)
        return;
    wait = (int)s_ticks + 3;
    int town = world_level_zone(COW_TOWN), farm = world_level_zone(COW_LEVEL);
    switch (step++) {
    case 0:
        cowt_check(farm >= 0, "la Granja Muu Muu está en el mundo del Acto 1");
        a3t_go(p, 38); /* Tristram */
        break;
    case 1: {
        int u = -1;
        if (units_find_all("Wirt", &u, 1) != 1)
            units_find_all("WIRT", &u, 1);
        cowt_check(u >= 0, "el cuerpo de Wirt está en Tristram");
        if (u < 0) {
            step = -1;
            break;
        }
        units_position(u, 1.0f, &wx, &wy);
        place_at(p, 0, wx + 1.0f, wy + 1.0f);
        cowt_check(obj_operate(p, u) == 1, "se usa el cuerpo de Wirt");
        cowt_check(cowt_ground("leg", wx, wy, 4.0f) == 1, "cayó la Pierna de Wirt");
        cowt_check(!obj_usable(u), "el cuerpo ya no se usa de nuevo");
        place_at(p, 0, wx + 15.0f, wy); /* (lejos: que no levante el oro al pasar) */
        find_free_spot(&p->x, &p->y);
        wait = (int)s_ticks + 230; /* (hasta 19 pilas, de a una cada 10 cuadros) */
        break;
    }
    case 2: {
        int gold = cowt_ground("gld", wx, wy, 6.0f);
        printf("COW: %d pilas de oro\n", gold);
        cowt_check(gold >= 10 && gold <= 19, "cayeron de 10 a 19 pilas de oro");
        for (int i = 0; i < items_ground_count(); i++) { /* la pierna (entre el oro) */
            float ix, iy;
            const Item *it = items_ground_item(i, &ix, &iy);
            if (it && it->type && strcmp(it->type->code, "leg") == 0) {
                Item leg = *it;
                items_ground_remove(i);
                bag_add(p, &leg);
                break;
            }
        }
        cowt_check(player_has(p, "leg"), "levantó la pierna");
        place_at(p, 0, s_town_x, s_town_y);
        break;
    }
    case 3: /* sin Baal: no */
        p->baal_q = 0;
        cowt_fill_cube(p, 5);
        cube_transmute_now(p);
        printf("COW: dice \"%s\"\n", p->message);
        cowt_check(!s_cow_open && p->cube_count == 2, "sin Baal muerto no se abre (y no gasta nada)");
        p->baal_q = 6;
        a3t_go(p, 2); /* el Blood Moor (y lo que tarda un tick ahí, para comparar con la granja) */
        cowt_t0 = wall_ms();
        wait = (int)s_ticks + COWT_PERF;
        break;
    case 4: /* fuera del campamento: no */
        printf("COW: en el Blood Moor, %.2f ms por tick (%d unidades)\n", (wall_ms() - cowt_t0) / COWT_PERF, units_count());
        cube_transmute_now(p);
        printf("COW: dice \"%s\"\n", p->message);
        cowt_check(!s_cow_open && p->cube_count == 2, "fuera del campamento no se abre");
        place_at(p, 0, s_town_x, s_town_y);
        break;
    case 5: { /* en el campamento, con Baal muerto: sí */
        cube_transmute_now(p);
        printf("COW: dice \"%s\"\n", p->message);
        cowt_check(s_cow_open, "en el campamento con Baal muerto se abre");
        cowt_check(p->cube_count == 1 && p->cube[0].type && strcmp(p->cube[0].type->code, "tsc") == 0 &&
                       p->cube[0].amount == 4,
                   "gastó la pierna y un pergamino");
        const Warp *w = cowt_portal(PORTAL_COW, town);
        cowt_check(w && w->to == farm, "el portal del campamento lleva a la granja");
        int ids[8], n = units_find_all("PortalPermanent", ids, 8);
        cowt_check(n >= 1, "se ve el portal rojo en el campamento");
        cowt_fill_cube(p, 3); /* otro: ya se abrió en esta partida */
        cube_transmute_now(p);
        cowt_check(p->cube_count == 2, "no se abre otro en la misma partida");
        if (!w) {
            step = -1;
            break;
        }
        place_at(p, 0, (w->x0 + w->x1) / 2.0f, (w->y0 + w->y1) / 2.0f);
        use_warp(p, w);
        wait = (int)s_ticks + 20;
        break;
    }
    case 6: {
        const Level *lv = world_level_at(p->x, p->y);
        cowt_check(lv && lv->id == COW_LEVEL, "el portal llevó a la granja");
        printf("COW: en la granja desde el tick %u\n", s_ticks);
        static int ids[1024];
        int cows = units_find_all("hellbovine", ids, 1024), escort = units_find_all("cowking", ids, 1024);
        int king = -1;
        units_find_all("The Cow King", &king, 1);
        printf("COW: %d vacas, el Rey Vaca %s con %d de escolta\n", cows, king >= 0 ? "está" : "no está", escort);
        float best = 1e9f;
        int near = 0;
        for (int k = 0; k < cows; k++) {
            float cx, cy;
            units_position(ids[k], 1.0f, &cx, &cy);
            float d = sqrtf((cx - p->x) * (cx - p->x) + (cy - p->y) * (cy - p->y));
            best = d < best ? d : best;
            near += d < 25.0f;
        }
        printf("COW: la más cerca a %.1f subtiles, %d a menos de 25\n", best, near);
        cowt_check(cows >= 50, "la granja está llena de vacas");
        cowt_check(king >= 0, "el Rey Vaca está en el vivac");
        cowt_check(cowt_portal(PORTAL_COW_BACK, farm) != NULL, "está el portal de vuelta en la granja");
        if (king < 0) {
            step = -1;
            break;
        }
        /* Un rato al lado del Rey Vaca, entre las vacas (con D2_GOD no se muere) */
        float bx, by;
        units_position(king, 1.0f, &bx, &by);
        bx += 5.0f;
        by += 5.0f;
        find_free_spot(&bx, &by);
        place_at(p, 0, bx, by);
        printf("COW: al lado del Rey Vaca desde el tick %u\n", s_ticks);
        cowt_t0 = wall_ms();
        wait = (int)s_ticks + COWT_PERF;
        break;
    }
    case 7: {
        printf("COW: en la granja, %.2f ms por tick (%d unidades)\n", (wall_ms() - cowt_t0) / COWT_PERF, units_count());
        int king = -1;
        units_find_all("The Cow King", &king, 1);
        if (king < 0) {
            cowt_check(0, "el Rey Vaca sigue ahí");
            step = -1;
            break;
        }
        units_position(king, 1.0f, &kx, &ky);
        units_damage(king, 1000000);
        wait = (int)s_ticks + 80;
        break;
    }
    case 8: {
        cowt_check(s_cow_king_dead && p->cow_king, "murió el Rey Vaca y se anotó");
        cowt_check(cowt_ground("vps", kx, ky, 4.0f) == 8, "dejó 8 pociones de aguante");
        s_cow_open = 0; /* (como en otra partida: lo que frena es el Rey Vaca) */
        cowt_fill_cube(p, 2);
        cube_transmute_now(p);
        printf("COW: dice \"%s\"\n", p->message);
        cowt_check(p->cube_count == 2 && !s_cow_open, "con el Rey Vaca muerto ya no se abre");
        s_cow_open = 1;
        p->cube_count = 0;
        const Warp *w = cowt_portal(PORTAL_COW_BACK, farm);
        if (w) {
            place_at(p, 0, (w->x0 + w->x1) / 2.0f, (w->y0 + w->y1) / 2.0f);
            use_warp(p, w);
        }
        wait = (int)s_ticks + 20;
        break;
    }
    case 9: {
        const Level *lv = world_level_at(p->x, p->y);
        cowt_check(lv && lv->id == COW_TOWN, "el portal de la granja vuelve al campamento");
        save_character(p);
        static Player back; /* (es grande para la pila) */
        memset(&back, 0, sizeof back);
        back.slot = p->slot;
        back.cls = p->cls;
        back.dialog_npc = -1;
        if (p->slot)
            cowt_check(load_character(&back) && back.cow_king == 1, "se guarda que mató al Rey Vaca");
        else
            printf("COW: sin D2_SAVE_DIR no se prueba el guardado\n");
        printf("COW: terminado, %d MAL\n", s_cowt_bad);
        step = -1;
        break;
    }
    }
}
