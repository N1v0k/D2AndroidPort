/* Prueba: D2_A3QTEST=1 (con D2_ACT=3) recorre las seis misiones del Acto 3
   sin caminar (se incluye desde game.c, después de la del Acto 2): llega a
   los lugares que abren los pedidos, habla con los NPC, abre el atril, el
   altar y los cofres, tira de la palanca, mata al Consejo, arma la
   Voluntad en el cubo, rompe el Orbe, mata a Mefisto y a un jefe para la
   estatuilla; muestra el registro en cada paso */
static void a3t_log(const Player *p, const char *what)
{
    const Level *lv = world_level_at(p->x, p->y);
    printf("A3: %-24s| %-20s| dijo %-26s| tomo %d voluntad %d puñal %d pájaro %d templo %d guardián %d flags %4d "
           "atributos %d vida+%d |", what, lv ? lv->name : "?", s_last_talk, p->tome_q, p->will_q, p->blade_q, p->bird_q,
           p->temple_q, p->meph_q, p->a3_flags, p->stat_points, p->hp_extra);
    for (int q = 0; q < 6; q++) {
        int st;
        const char *g = quest_info(p, 3, q, &st);
        printf(" q%d=%d %s", q + 1, st, g ? g : "-");
    }
    printf("\n");
    s_last_talk[0] = '\0';
}

/* Al medio de ese nivel (armándolo): el tick siguiente anota que llegó */
static void a3t_go(Player *p, int level_id)
{
    int z = world_level_zone(level_id);
    const Level *lv = NULL;
    for (int i = 0; i < world_level_count() && !lv; i++)
        if (world_level(i)->id == level_id)
            lv = world_level(i);
    if (z < 0 || !lv || zone_load(z) != 0) {
        printf("A3: no está el nivel %d\n", level_id);
        return;
    }
    float x = (lv->x0 + lv->x1) * TILE_SUB / 2.0f, y = (lv->y0 + lv->y1) * TILE_SUB / 2.0f;
    find_free_spot(&x, &y);
    place_at(p, 0, x, y);
}

/* Levanta lo que haya en el suelo cerca de (x, y) */
static void a3t_pick(Player *p, float x, float y)
{
    p->x = x;
    p->y = y;
    for (int k = 0; k < 8; k++) {
        float ix, iy;
        int g = items_ground_nearest(p->x, p->y, 4.0f, 0);
        if (g < 0)
            break;
        Item it = *items_ground_item(g, &ix, &iy);
        items_ground_remove(g);
        if (strcmp(it.type->code, "gld") == 0)
            p->gold += it.amount;
        else if (bag_add(p, &it))
            printf("A3: levantó %s%s\n", it.name[0] ? it.name : it.type->name, it.quality == QUALITY_UNIQUE ? " (único)" : "");
    }
}

/* Abre el objeto de misión de esa clase en ese nivel (lo arma) y levanta lo que cae */
static void a3t_object(Player *p, const char *cls, const char *level)
{
    const Level *lv = world_level_named(level);
    int id;
    if (!lv || zone_load(lv->zone) != 0 || units_find_all(cls, &id, 1) != 1) {
        printf("A3: no está %s\n", cls);
        return;
    }
    float pos[2];
    units_position(id, 1.0f, &pos[0], &pos[1]);
    p->x = pos[0] + 1.0f;
    p->y = pos[1] + 1.0f;
    quest_interact(p);
    a3t_pick(p, p->x, p->y);
}

/* Usa el objeto de la mochila con ese código (la Poción de la vida) */
static void a3t_use(Player *p, const char *code)
{
    for (int i = 0; i < p->bag_count; i++)
        if (strcmp(p->bag[i].type->code, code) == 0) {
            p->inv_sel = INV_CELL0 + p->bag_y[i] * BAG_W + p->bag_x[i];
            inventory_use(p);
            return;
        }
    printf("A3: no tiene %s\n", code);
}

static void a3_quest_test(void)
{
    static int step, wait;
    static float kx, ky; /* dónde murió el último que mató la prueba */
    if (!getenv("D2_A3QTEST") || s_act != 3 || step < 0)
        return;
    Player *p = &s_pl[0];
    float tx = s_town_x, ty = s_town_y;
    if ((int)s_ticks < wait)
        return;
    wait = (int)s_ticks + 3;
    switch (step++) {
    case 0:
        a3t_log(p, "inicio");
        a2t_talk(p, "hratli");
        a3t_log(p, "Hratli (se presenta)");
        a3t_go(p, 79); /* la Kurast baja: el Tomo */
        break;
    case 1:
        a3t_log(p, "Kurast baja");
        a3t_go(p, 77); /* el Great Marsh: la Voluntad */
        break;
    case 2:
        a3t_go(p, 78); /* el Flayer Jungle: la Gidbinn */
        break;
    case 3:
        a3t_go(p, 82); /* el Causeway: el Templo */
        break;
    case 4:
        a3t_log(p, "llegó a todo");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "alkor");
        a3t_log(p, "Alkor (tomo)");
        a2t_talk(p, "cain3");
        a3t_log(p, "Caín (Khalim)");
        a2t_talk(p, "hratli");
        a3t_log(p, "Hratli (Gidbinn)");
        a2t_talk(p, "ormus");
        a3t_log(p, "Ormus (templo)");
        a3t_object(p, "LamesEnsTome", "Ruined Temple");
        break;
    case 5:
        a3t_log(p, "tomó el tomo");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "alkor");
        a3t_log(p, "Alkor (recompensa)");
        a3t_object(p, "GidBInnDecoy", "Flayer Jungle");
        if (s_a3_gid_boss >= 0) { /* el señuelo: el fetiche que sale tiene la Gidbinn */
            units_position(s_a3_gid_boss, 1.0f, &kx, &ky);
            printf("A3: salió %s\n", units_display_name(s_a3_gid_boss));
            units_damage(s_a3_gid_boss, 1000000);
        } else {
            printf("A3: no salió el fetiche de la Gidbinn\n");
        }
        wait = (int)s_ticks + 40;
        break;
    case 6:
        a3t_pick(p, kx, ky);
        a3t_log(p, "tomó la Gidbinn");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "ormus");
        a3t_log(p, "Ormus (anillo)");
        a2t_talk(p, "asheara");
        a3t_log(p, "Asheara (mercenario)");
        printf("A3: el próximo mercenario %s\n", p->free_hire ? "es gratis" : "se paga");
        a3t_object(p, "KhalimEyeChest", "Spider Cavern");
        a3t_object(p, "KhalimBrainChest", "Flayer Dungeon Level 3");
        a3t_object(p, "KhalimHeartChest", "Sewers Level 2");
        a3t_object(p, "BeneathTheCityLever", "Sewers Level 1");
        printf("A3: la palanca %s\n", s_a3_lever ? "está tirada" : "no se tiró");
        break;
    case 7:
        a3t_log(p, "ojo, cerebro y corazón");
        place_at(p, 0, tx, ty);
        for (int k = 0; k < 3; k++)
            a2t_talk(p, "cain3");
        a3t_log(p, "Caín (las partes)");
        for (int k = 0; k < 3; k++) { /* el Consejo de Travincal */
            const Level *tr = world_level_named("Travincal");
            int id;
            if (tr && zone_load(tr->zone) == 0 && units_find_all(k_council[k], &id, 1) == 1) {
                units_position(id, 1.0f, &kx, &ky);
                units_damage(id, 1000000);
                printf("A3: %s herido de muerte\n", k_council[k]);
            }
        }
        wait = (int)s_ticks + 40;
        break;
    case 8:
        a3t_log(p, "murió el Consejo");
        { /* el Mangual, al lado del primero que cayó */
            int id;
            for (int k = 0; k < 3; k++)
                if (units_find_all(k_council[k], &id, 1) == 1) {
                    units_position(id, 1.0f, &kx, &ky);
                    a3t_pick(p, kx, ky);
                }
        }
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain3");
        a3t_log(p, "Caín (el mangual)");
        /* el cubo: las cuatro partes */
        {
            static const char *const k_parts[4] = { "qey", "qbr", "qhr", "qf1" };
            if (!player_has(p, "box"))
                give_quest_item(p, "box");
            for (int k = 0; k < 4; k++)
                for (int i = 0; i < p->bag_count; i++)
                    if (strcmp(p->bag[i].type->code, k_parts[k]) == 0) {
                        if (cube_add(p, &p->bag[i]))
                            bag_remove(p, i);
                        break;
                    }
            cube_transmute_now(p);
            for (int c = 0; c < p->cube_count; c++)
                printf("A3: en el cubo %s%s\n", p->cube[c].name[0] ? p->cube[c].name : p->cube[c].type->name,
                       p->cube[c].quality == QUALITY_UNIQUE ? " (único)" : "");
            for (int c = 0; c < p->cube_count; c++) /* la Voluntad, a la mano */
                if (strcmp(p->cube[c].type->code, "qf2") == 0) {
                    p->equip[SLOT_WEAPON] = p->cube[c];
                    cube_remove(p, c);
                    refresh_stats(p);
                    break;
                }
        }
        a2t_talk(p, "cain3");
        a3t_log(p, "Caín (la Voluntad)");
        { /* la escalera del Durance, cerrada */
            int tz = world_level_zone(83), dz = world_level_zone(100);
            for (int i = 0; i < world_warp_count(); i++)
                if (world_warp(i)->from == tz && world_warp(i)->to == dz)
                    printf("A3: la escalera del Durance %s\n", a3_warp_open(p, world_warp(i)) ? "está abierta" : "está cerrada");
        }
        a3t_object(p, "CompellingOrb", "Travincal");
        a3t_object(p, "CompellingOrb", "Travincal");
        a3t_log(p, "golpeó el Orbe dos veces");
        {
            int tz = world_level_zone(83), dz = world_level_zone(100);
            for (int i = 0; i < world_warp_count(); i++)
                if (world_warp(i)->from == tz && world_warp(i)->to == dz)
                    printf("A3: la escalera del Durance %s\n", a3_warp_open(p, world_warp(i)) ? "está abierta" : "está cerrada");
        }
        place_at(p, 0, tx, ty);
        a2t_talk(p, "ormus");
        a3t_log(p, "Ormus (templo)");
        a2t_talk(p, "ormus");
        a3t_log(p, "Ormus (Mefisto)");
        a2t_kill(world_level_zone(102), "mephisto");
        wait = (int)s_ticks + 60;
        break;
    case 9:
        a3t_log(p, "murió Mefisto");
        {
            int id;
            float bx = 0, by = 0;
            if (units_find_all("MephistoBridge", &id, 1) == 1)
                units_position(id, 1.0f, &bx, &by);
            printf("A3: el puente: colisión %04x en su centro\n", map_coll(&s_map, (int)bx, (int)by));
        }
        place_at(p, 0, tx, ty);
        a2t_talk(p, "ormus");
        a3t_log(p, "Ormus (Mefisto)");
        { /* el jefe de la estatuilla: el primero que apareció */
            int b = s_a3_bird_boss;
            if (b >= 0) {
                units_position(b, 1.0f, &kx, &ky);
                units_damage(b, 1000000);
                printf("A3: jefe %s (%s) herido de muerte\n", units_display_name(b), world_zone_name(s_a3_bird_zone));
            } else {
                printf("A3: no hay jefe para la estatuilla\n");
            }
        }
        wait = (int)s_ticks + 40;
        break;
    case 10:
        a3t_pick(p, kx, ky);
        a3t_log(p, "levantó la estatuilla");
        place_at(p, 0, tx, ty);
        a2t_talk(p, "cain3");
        a3t_log(p, "Caín (estatuilla)");
        a2t_talk(p, "meshif2");
        a3t_log(p, "Meshif (el pájaro)");
        a2t_talk(p, "cain3");
        a3t_log(p, "Caín (el pájaro)");
        a2t_talk(p, "alkor");
        a3t_log(p, "Alkor (el pájaro)");
        a3t_go(p, 76); /* sale del muelle y vuelve */
        break;
    case 11:
        place_at(p, 0, tx, ty);
        break;
    case 12:
        a2t_talk(p, "alkor");
        a3t_log(p, "Alkor (la poción)");
        a3t_use(p, "xyz");
        a3t_log(p, "tomó la poción");
        step = -1;
        printf("A3: terminado\n");
        break;
    }
}
