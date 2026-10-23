/* Prueba: D2_TZTEST=1 (con D2_DIFF=2, D2_ACT=1, D2_TZ=1, D2_TZ_ZONE=2 y D2_TZ_HERALD=1) recorre
   las zonas de terror sin caminar (endgame.inc; se incluye desde game.c, después de ubertest.inc):
   lo que se leyó de desecratedzones.json, la rotación, las clases de tesoro por nivel, los
   monstruos de las Cold Plains aterrorizadas (y los del Blood Moor, que no), un Heraldo que sale,
   muere y sube el grado, una astilla y un amuleto Sunder. Cada cosa dice BIEN o MAL. */
static int s_tzt_bad;

static void tzt_check(int ok, const char *what)
{
    printf("TERROR: %s %s\n", ok ? "BIEN" : "MAL ", what);
    s_tzt_bad += !ok;
}

/* Los monstruos vivos del nivel: cuántos, cuántos aterrorizados y el nivel más bajo */
static int tzt_level_units(int level_id, int *terror, int *min_level, int *sample)
{
    int n = 0;
    *terror = 0;
    *min_level = 1000;
    *sample = -1;
    for (int i = 0; i < units_count(); i++) {
        float x, y;
        if (!units_alive_enemy(i))
            continue;
        units_position(i, 1.0f, &x, &y);
        const Level *lv = world_level_at(x, y);
        if (!lv || lv->id != level_id)
            continue;
        n++;
        *terror += units_is_terror(i);
        if (units_level(i) < *min_level)
            *min_level = units_level(i);
        if (*sample < 0 && !units_is_unique(i) && !units_is_champion(i))
            *sample = i;
    }
    return n;
}

static void tz_test(void)
{
    static int step, wait, kills;
    static float hx, hy;
    if (!getenv("D2_TZTEST") || step < 0)
        return;
    Player *p = &s_pl[0];
    if ((int)s_ticks < wait || s_tz_on < 0)
        return;
    wait = (int)s_ticks + 3;
    switch (step++) {
    case 0: {
        tzt_check(s_tz_loaded == 1 && s_tz_nzones == 34, "se leyeron las 34 zonas del json");
        int acts = 1;
        for (int a = 0; a < 5; a++)
            acts &= s_tz_act[a].n > 0;
        printf("TERROR: actos %d %d %d %d %d niveles; Infierno %d..%d +%d, %d%% de experiencia\n", s_tz_act[0].n, s_tz_act[1].n,
               s_tz_act[2].n, s_tz_act[3].n, s_tz_act[4].n, s_tz_diff[2].min, s_tz_diff[2].max, s_tz_diff[2].boost, s_tz_diff[2].xp);
        tzt_check(acts && s_tz_act[0].n == 37, "los cinco grupos de las astillas (el del Acto 1, con 37 niveles)");
        tzt_check(s_tz_diff[0].min == 3 && s_tz_diff[0].max == 45 && s_tz_diff[1].max == 71 && s_tz_diff[2].min == 70 &&
                      s_tz_diff[2].max == 96 && s_tz_diff[2].boost == 2 && s_tz_diff[2].xp == 25,
                  "los límites de nivel de cada dificultad");
        tzt_check(s_tz_minutes == 30 && s_tz_start == 1764892800LL && s_tz_seed == 16665365343970128666ull,
                  "el turno de 30 minutos desde el 5 de diciembre de 2025, con su semilla");
        tzt_check(s_tz_ntiers == 5 && s_tz_tier[0].hp == 1000 && s_tz_tier[4].hp == 2000 && s_tz_tier[4].nmin == 14 &&
                      s_tz_tier[1].tc == 3,
                  "los cinco grados de Heraldo");
        tzt_check(s_tz_npool == 10 && s_tz_pool_mod[9] == 30, "los modificadores de más de los únicos (y el aura)");
        /* La rotación: nunca la misma dos veces seguidas, y pasan muchas zonas */
        int seen[TZ_ZONES] = { 0 }, distinct = 0, repeat = 0;
        for (long long t = 1000; t < 1200; t++) {
            int z = tz_zone_of(t);
            repeat += z == tz_zone_of(t - 1);
            distinct += !seen[z]++;
        }
        printf("TERROR: en 200 turnos, %d zonas distintas\n", distinct);
        tzt_check(!repeat && distinct == 34, "la rotación no repite seguido y pasa por todas");
        /* Las clases de tesoro por nivel (sub_6FC52110) */
        printf("TERROR: H2H A a 68: %s; a 87: %s; Andariel aterrorizada a 96: %s\n", items_tc_for_level("Act 1 (H) H2H A", 68),
               items_tc_for_level("Act 1 (H) H2H A", 87), items_tc_for_level("Andarielq (H) Desecrated A", 96));
        tzt_check(!strcmp(items_tc_for_level("Act 1 (H) H2H A", 68), "Act 1 (H) H2H C") &&
                      !strcmp(items_tc_for_level("Act 1 (H) H2H A", 62), "Act 1 (H) H2H A"),
                  "la clase de tesoro sube dentro de su grupo con el nivel");
        tzt_check(!strcmp(items_tc_for_level("Andarielq (H) Desecrated A", 96), "Andarielq (H) Desecrated H"),
                  "la de Andariel aterrorizada llega a la H a nivel 96");
        /* El nivel del terror: el del jugador 1 + 2, entre 70 y 96 */
        int keep = p->level_num, b30, b80, b99;
        p->level_num = 30, b30 = tz_base();
        p->level_num = 80, b80 = tz_base();
        p->level_num = 99, b99 = tz_base();
        p->level_num = keep;
        tzt_check(b30 == 70 && b80 == 82 && b99 == 96, "el nivel del terror (70 con nivel 30, 82 con 80, 96 con 99)");
        p->level_num = 85;
        tzt_check(s_tz_on == 1 && s_tz_cur == 2 && tz_current() == &s_tz_zone[2], "están aterrorizadas las Cold Plains (D2_TZ_ZONE=2)");
        printf("TERROR: dice \"%s\"\n", p->message);
        char line[192];
        tz_pause_line(line, sizeof line, 1000);
        printf("TERROR: en la pausa: \"%s\"\n", line);
        tzt_check(line[0] != '\0', "la pausa dice la zona y cuánto le falta");
        a3t_go(p, 3);
        wait = (int)s_ticks + 20;
        break;
    }
    case 1: {
        int terror, lo, sample, n = tzt_level_units(3, &terror, &lo, &sample);
        printf("TERROR: Cold Plains: %d monstruos, %d aterrorizados, nivel más bajo %d\n", n, terror, lo);
        tzt_check(n > 0 && terror == n && lo >= 87, "todos los de las Cold Plains, aterrorizados y de nivel 87 o más");
        tzt_check(tz_level_on(p->level) && p->banner_ticks > 0, "el cartel del nivel dice que está aterrorizado");
        if (sample >= 0) {
            char desc[160];
            units_describe(sample, desc, sizeof desc);
            printf("TERROR: %s (nivel %d): \"%s\", tesoro %s\n", units_display_name(sample), units_level(sample), desc,
                   units_treasure_class(sample));
            tzt_check(strstr(desc, str_get("EnteringDesecratedLevelNotification")) != NULL, "abajo del nombre dice que está aterrorizado");
            tzt_check(strcmp(units_treasure_class(sample), "Act 1 (H) H2H A") != 0, "su clase de tesoro va con su nivel");
        }
        a3t_go(p, 2);
        wait = (int)s_ticks + 20;
        break;
    }
    case 2: {
        int terror, lo, sample, n = tzt_level_units(2, &terror, &lo, &sample);
        printf("TERROR: Blood Moor: %d monstruos, %d aterrorizados, nivel más bajo %d\n", n, terror, lo);
        tzt_check(n > 0 && terror == 0 && lo < 80, "los del Blood Moor, no (su nivel de siempre)");
        a3t_go(p, 3);
        wait = (int)s_ticks + 10;
        break;
    }
    case 3: { /* Una muerte con D2_TZ_HERALD=1: el Heraldo avisa */
        int terror, lo, sample;
        tzt_level_units(3, &terror, &lo, &sample);
        if (sample < 0) {
            tzt_check(0, "hay un monstruo para matar");
            step = -1;
            break;
        }
        units_damage(sample, 1 << 24);
        award_kill(p, sample, 0);
        kills = s_tz_kills;
        printf("TERROR: dice \"%s\"\n", p->message);
        tzt_check(kills == 1 && s_tz_herald_ticks == TZ_HERALD_WAIT, "la muerte despierta a un Heraldo (sale en cinco segundos)");
        wait = (int)s_ticks + TZ_HERALD_WAIT + 4;
        break;
    }
    case 4: {
        int h = s_tz_herald;
        tzt_check(h >= 0 && units_herald_tier(h) == 1, "salió un Heraldo de grado 1");
        if (h < 0) {
            step = -1;
            break;
        }
        units_position(h, 1.0f, &hx, &hy);
        char desc[160];
        units_describe(h, desc, sizeof desc);
        int hp, max_hp;
        units_health(h, &hp, &max_hp);
        int minions = ubt_count_near(units_id(h), hx, hy, 6.0f) - 1;
        printf("TERROR: %s (%s, nivel %d, vida %d): \"%s\", %d esbirros, tesoro %s\n", units_display_name(h), units_id(h),
               units_level(h), max_hp, desc, minions, units_treasure_class(h));
        printf("TERROR: dice \"%s\"\n", p->message);
        tzt_check(!strcmp(units_display_name(h), str_get("HeraldName1")), "se llama Heraldo del Temor");
        tzt_check(minions >= 5, "con sus esbirros");
        tzt_check(strstr(desc, str_get("monsteruniqueprop9")) != NULL, "siempre con un aura");
        tzt_check(strstr(units_treasure_class(h), "Herald") != NULL, "su clase de tesoro es la del Heraldo");
        units_damage(h, 1 << 24);
        drop_loot(h, p);
        wait = (int)s_ticks + 10;
        break;
    }
    case 5: {
        tzt_check(s_tz_herald < 0 && s_tz_tier_next == 1, "muerto el Heraldo, el próximo es de grado 2");
        int sunder = 0, shards = 0;
        for (int k = 0; k < 4000; k++) {
            Item out[16];
            int m = items_roll_drop("Act 1 (H) Herald Item A", 90, 0, out, 16);
            for (int i = 0; i < m; i++) {
                sunder += out[i].quality == QUALITY_UNIQUE && strstr(items_unique_key(&out[i]), "PreCrafted") != NULL;
                shards += out[i].type && !strncmp(out[i].type->code, "xa", 2);
            }
        }
        printf("TERROR: en 4000 tiradas del Heraldo: %d amuletos Sunder, %d astillas\n", sunder, shards);
        tzt_check(sunder > 0 && shards > 0, "los Heraldos sueltan amuletos Sunder y astillas");
        /* La astilla del Este: el Acto 2 entero, el resto de la partida */
        Item it = items_make(items_find("xa2"));
        bag_add(p, &it);
        int si = bag_index_of(p, "xa2");
        tzt_check(si >= 0 && endgame_use_item(p, si) && s_tz_shard == 1 && bag_index_of(p, "xa2") < 0, "se usa la astilla del Este");
        printf("TERROR: dice \"%s\"\n", p->message);
        tzt_check(!tz_level_on(world_level_at(p->x, p->y)) && tz_current() == &s_tz_act[1], "ahora es el Acto 2, no las Cold Plains");
        /* Sunder: un zombi de Infierno (inmune al frío) con el amuleto */
        units_zone_begin(zone_at(p->x, p->y));
        int z = units_spawn_monster("zombie1", p->x + 2.0f, p->y, 70);
        units_zone_end();
        int before = z >= 0 ? units_resist(z, ELEM_COLD, 1000) : -1;
        Item ch = items_make(items_find("cm3"));
        int made = items_make_unique_named(&ch, "PreCrafted Cold Rupture");
        bag_add(p, &ch);
        refresh_stats(p);
        tz_sunder();
        int after = z >= 0 ? units_resist(z, ELEM_COLD, 1000) : -1;
        printf("TERROR: zombi: %d de 1000 de frío sin el amuleto, %d con (%d de Sunder, %d de resistencia al frío)\n", before, after,
               p->bonus[STAT_SUNDER_COLD], p->bonus[STAT_RES_COLD]);
        tzt_check(made && before == 0 && after == 50, "el amuleto Sunder deja la inmunidad al frío en 95");
        printf("TERROR: terminado, %d MAL\n", s_tzt_bad);
        step = -1;
        break;
    }
    }
}
