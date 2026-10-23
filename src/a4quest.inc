/* a4quest.inc: las misiones del Acto 4 (lo incluye game.c, después de
   a3quest.inc). Como el original (D2MOO, QUESTS/ACT4/A4Q0..A4Q4.cpp):
   - La presentación de Tyrael (A4Q0): la primera vez que se le habla.
   - El Ángel Caído (A4Q1, izual_q): Tyrael la pide; Izual, en el Plains of
     Despair; al morir sale su espíritu (ACT4Q1_SpawnIzualGhost), que habla;
     Tyrael da 2 puntos de habilidad.
   - La Forja Infernal (A4Q3, forge_q): Caín la pide (y da la Piedra de
     Mefisto si no la tienen); Hephasto, en el River of Flame, suelta el
     Martillo; la piedra va sobre la Forja y tres golpes con el Martillo en
     la mano la rompen: salen gemas en cuatro tandas, una por jugador
     (ACT4Q3_CreateReward).
   - El Final del Terror (A4Q2, diablo_q): Tyrael la pide después de Izual;
     los cinco sellos del Santuario (el 1, el 3 y el 5 sueltan a su jefe);
     con los cinco abiertos y los tres jefes muertos, mueren los demás
     monstruos del Santuario y sale Diablo en el corazón.
   - Hadriel (A4Q4): en el último puente del río, dice lo suyo.
   Después de Diablo, Tyrael abre el portal del final (el de la expansión,
   ReallyTheLastPortal): pasa el epílogo y lleva a Harrogath (el Acto 5).
   El registro del original las muestra en ese orden (los íconos a4q1, a4q2 y
   a4q3), aunque sus textos son qstsa4q1, qstsa4q3 y qstsa4q2.

   shortcut: Diablo sale y muere sin la luz del original (los rayos que lo
   atraviesan); los jugadores no vuelven solos al pueblo después de 90
   segundos. */

enum { A4_TYRAEL = 26, A4_CAIN = 27, A4_IZUAL = 28, A4_HADRIEL = 29 }; /* (k_npc_vo) */

/* El número de texto (qstsa4qN) de cada lugar del registro */
static const int k_a4_quest_key[3] = { 1, 3, 2 };

/* Lo de esta partida */
static int   s_a4_seals;      /* los sellos abiertos (bit 0..4 = DiabloSeal1..5) */
static int   s_a4_boss_dead;  /* los jefes de los sellos 1, 3 y 5 que murieron (bit = sello) */
static int   s_a4_boss[5];    /* sus unidades (-1 = no salió) */
static int   s_a4_diablo;     /* 0 todavía no, 1 por salir (s_a4_timer), 2 salió, 3 murió */
static int   s_a4_diablo_unit = -1;
static int   s_a4_timer;      /* ticks que faltan para lo que espera */
static int   s_a4_izual;      /* 0 vivo, 1 murió (falta el espíritu), 2 salió el espíritu */
static float s_a4_izual_x, s_a4_izual_y;
static int   s_a4_smith_dead; /* Hephasto murió (soltó el Martillo) */
static int   s_a4_hits;       /* golpes a la Forja con la piedra encima */
static int   s_a4_gems, s_a4_gem_tick; /* la tanda de gemas que sigue (4..1, 0 = ninguna) */
static float s_a4_forge_x, s_a4_forge_y;

static const char *const k_a4_flash[2] = { "quest_hellforge_flash_1", "quest_hellforge_flash_2" }; /* los golpes */
static const char *const k_a4_seals[5] = { "DiabloSeal1", "DiabloSeal2", "DiabloSeal3", "DiabloSeal4", "DiabloSeal5" };
/* Los que sueltan jefe (OperateFn 54, 55 y 56 de objects.txt): dónde sale respecto del sello y quién */
static const struct { int seal, dx, dy; const char *boss; } k_a4_bosses[3] = {
    { 0, -12, -52, "Infector of Souls" }, { 2, -39, 33, "Lord De Seis" }, { 4, 32, 16, "Grand Vizier of Chaos" },
};

/* Lo que sale en pleno juego (los jefes de los sellos, Diablo, el espíritu
   de Izual, el portal del final) es de la zona donde sale, como el portal al
   pueblo: se borra con ella (y al cambiar de acto) y a4_zone_loaded lo vuelve
   a poner. Su lugar puede quedar para otra unidad: ¿la unidad u sigue siendo
   ese (vivo o muerto)? */
static int a4_still(int u, const char *id)
{
    return u >= 0 && u < units_count() && strcmp(units_id(u), id) == 0;
}

/* Los objetivos del registro (qstsa4q11...) */
static const char *quest_goal4(const Player *p, int q, int *state)
{
    switch (q) {
    case 0: /* izual_q: 1 Tyrael la pidió, 2 llegó al Plains, 3 murió Izual, 4 habló su espíritu, 5 la recompensa */
        *state = p->izual_q >= 5 ? 2 : p->izual_q > 0 ? 1 : 0;
        return p->izual_q >= 5 ? NULL : p->izual_q == 4 ? "qstsa4q13" : p->izual_q == 3 ? "qstsa4q13a"
                                        : p->izual_q == 2 ? "qstsa4q12" : "qstsa4q11";
    case 1: /* forge_q: 1 Caín la pidió, 4 rompió la piedra, 5 Caín la dio por cumplida; en el medio, según qué tiene */
        *state = p->forge_q >= 5 ? 2 : p->forge_q > 0 ? 1 : 0;
        if (p->forge_q >= 5)
            return NULL;
        if (p->forge_q == 4)
            return "qstsa4q34";
        if (!(p->a4_flags & A4F_STONE))
            return "qstsa4q31"; /* llevar la piedra */
        return player_has(p, "hfh") ? "qstsa4q33" : "qstsa4q32";
    default: /* diablo_q: 1 Tyrael la pidió, 2 llegó al Santuario, 3 los sellos abiertos (sale Diablo), 4 murió Diablo */
        *state = p->diablo_q >= 4 ? 2 : p->diablo_q > 0 ? 1 : 0;
        return p->diablo_q >= 4 ? NULL : p->diablo_q == 3 ? "qstsa4q22" : p->diablo_q == 2 ? "qstsa4q23" : "qstsa4q21";
    }
}

/* Los sellos que faltan (el %d de qstsa4q23) */
static int a4_seals_left(void)
{
    int n = 0;
    for (int k = 0; k < 5; k++)
        n += !(s_a4_seals >> k & 1);
    return n;
}

/* Lo que tiene para decir cada uno (en el orden de los NPCMessages del original) */
enum {
    A4N_NONE, A4N_Q1_ASK, A4N_Q1_TOLD, A4N_Q1_EARLY, A4N_Q1_DONE, A4N_Q2_ASK, A4N_Q2_TOLD, A4N_Q2_DONE, A4N_PORTAL,
    A4N_CAIN_Q1, A4N_CAIN_Q1_EARLY, A4N_CAIN_Q1_DONE, A4N_CAIN_Q2, A4N_CAIN_Q2_DONE, A4N_FORGE_ASK, A4N_FORGE_DONE,
    A4N_GHOST, A4N_HADRIEL,
};

static int a4_npc_pick(const Player *p, int who)
{
    switch (who) {
    case A4_TYRAEL:
        if (p->izual_q == 3 || p->izual_q == 4)
            return A4N_Q1_DONE;
        if (p->izual_q == 0)
            return A4N_Q1_ASK;
        if (p->izual_q < 3 && p->talk_away[who] && !(p->a4_flags & A4F_Q1_EARLY))
            return A4N_Q1_EARLY;
        if (p->izual_q < 3 && !(p->a4_flags & A4F_Q1_TOLD))
            return A4N_Q1_TOLD;
        if (p->izual_q >= 5 && p->diablo_q == 0)
            return A4N_Q2_ASK;
        if (p->diablo_q >= 1 && p->diablo_q < 4 && !(p->a4_flags & A4F_Q2_TOLD))
            return A4N_Q2_TOLD;
        if (p->diablo_q >= 4 && !(p->a4_flags & A4F_PORTAL))
            return A4N_Q2_DONE;
        return A4N_NONE;
    case A4_CAIN:
        if (p->forge_q == 0)
            return A4N_FORGE_ASK;
        if (p->forge_q == 4)
            return A4N_FORGE_DONE;
        if (p->izual_q >= 3 && !(p->a4_flags & A4F_CAIN_IZUAL))
            return A4N_CAIN_Q1_DONE;
        if (p->izual_q >= 1 && p->izual_q < 3 && p->talk_away[who] && !(p->a4_flags & A4F_Q1_EARLY))
            return A4N_CAIN_Q1_EARLY;
        if (p->izual_q >= 1 && p->izual_q < 3 && !(p->a4_flags & A4F_Q1_TOLD))
            return A4N_CAIN_Q1;
        if (p->diablo_q >= 4 && !(p->a4_flags & A4F_CAIN_DIABLO))
            return A4N_CAIN_Q2_DONE;
        if (p->diablo_q >= 1 && p->diablo_q < 4 && !(p->a4_flags & A4F_Q2_TOLD))
            return A4N_CAIN_Q2;
        return A4N_NONE;
    case A4_IZUAL:
        return p->izual_q == 3 ? A4N_GHOST : A4N_NONE;
    case A4_HADRIEL: /* (después de la Forja y de Diablo ya no tiene nada que decir) */
        return p->forge_q >= 4 && s_a4_diablo >= 3 ? A4N_NONE : A4N_HADRIEL;
    default:
        return A4N_NONE;
    }
}

static int a4_npc_news(const Player *p, int who)
{
    int n = a4_npc_pick(p, who);
    return n != A4N_NONE && n != A4N_HADRIEL;
}

/* El portal rojo del final, al lado de Tyrael (ACT4Q2 20000: el de la expansión) */
static void a4_open_portal(void)
{
    int id;
    float x, y;
    if (units_find_all("ReallyTheLastPortal", &id, 1) == 1 || units_find("tyrael2", &x, &y) != 0)
        return;
    x += 5.0f;
    find_free_spot(&x, &y);
    units_zone_begin(zone_at(x, y));
    units_spawn_object("ReallyTheLastPortal", x, y, "on");
    units_zone_end();
}

/* Lo dice y hace lo que corresponde; 1 si dijo algo */
static int a4_npc_quest(Player *p, int who, char *key, size_t cap)
{
    switch (a4_npc_pick(p, who)) {
    case A4N_Q1_ASK:
        party_raise(offsetof(Player, izual_q), 1);
        snprintf(key, cap, "A4Q1InitTyrael");
        return 1;
    case A4N_Q1_TOLD:
        p->a4_flags |= A4F_Q1_TOLD;
        snprintf(key, cap, "A4Q1AfterInitTyrael");
        return 1;
    case A4N_Q1_EARLY:
        p->a4_flags |= A4F_Q1_EARLY;
        p->talk_away[who] = 0;
        snprintf(key, cap, "A4Q1EarlyReturnTyrael");
        return 1;
    case A4N_Q1_DONE: /* la recompensa: dos puntos de habilidad */
        p->izual_q = 5;
        p->skill_points += 2;
        quest_done("qstsa4q1", NULL);
        snprintf(key, cap, "A4Q1SuccessfulTyrael");
        return 1;
    case A4N_Q2_ASK:
        party_raise(offsetof(Player, diablo_q), 1);
        snprintf(key, cap, "A4Q2InitTyrael");
        return 1;
    case A4N_Q2_TOLD:
        p->a4_flags |= A4F_Q2_TOLD;
        snprintf(key, cap, "%s", who == A4_CAIN ? "A4Q2AfterInitCain" : "A4Q2AfterInitTyrael");
        return 1;
    case A4N_Q2_DONE: /* y el portal hacia el Acto 5 */
        for (int i = 0; i < s_num_players; i++)
            s_pl[i].a4_flags |= A4F_PORTAL;
        a4_open_portal();
        snprintf(key, cap, "A4Q2ExpansionSuccessTyrael");
        return 1;
    case A4N_CAIN_Q1:
        p->a4_flags |= A4F_Q1_TOLD;
        snprintf(key, cap, "A4Q1AfterInitCain");
        return 1;
    case A4N_CAIN_Q1_EARLY:
        p->a4_flags |= A4F_Q1_EARLY;
        p->talk_away[who] = 0;
        snprintf(key, cap, "A4Q1EarlyReturnCain");
        return 1;
    case A4N_CAIN_Q1_DONE:
        p->a4_flags |= A4F_CAIN_IZUAL;
        snprintf(key, cap, "A4Q1SuccessfulCain");
        return 1;
    case A4N_CAIN_Q2:
        p->a4_flags |= A4F_Q2_TOLD;
        snprintf(key, cap, "A4Q2AfterInitCain");
        return 1;
    case A4N_CAIN_Q2_DONE:
        p->a4_flags |= A4F_CAIN_DIABLO;
        snprintf(key, cap, "A4Q2ExpansionSuccessCain");
        return 1;
    case A4N_FORGE_ASK: /* si no tiene la Piedra de Mefisto, Caín le da una (ACT4Q3 679) */
        party_raise(offsetof(Player, forge_q), 1);
        if (player_has(p, "mss") || (p->a4_flags & A4F_STONE)) {
            snprintf(key, cap, "A4Q3InitHasStoneCain");
        } else {
            give_quest_item(p, "mss");
            snprintf(key, cap, "A4Q3InitNoStoneCain");
        }
        return 1;
    case A4N_FORGE_DONE:
        p->forge_q = 5;
        quest_done("qstsa4q3", NULL);
        snprintf(key, cap, "A4Q3SuccessfulCain");
        return 1;
    case A4N_GHOST: /* el espíritu de Izual (ACT4Q1 675): ahora hay que contárselo a Tyrael */
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].izual_q == 3)
                s_pl[i].izual_q = 4;
        snprintf(key, cap, "A4Q1SuccessfulIzual");
        return 1;
    case A4N_HADRIEL: /* antes de la Forja, lo primero; después (y con Diablo vivo), lo segundo */
        snprintf(key, cap, "%s", p->forge_q >= 4 ? "HellsAngelGossip2" : "HellsAngelGossip1");
        return 1;
    default:
        return 0;
    }
}

/* El objeto de misión del Acto 4 que tiene al lado: qué se puede hacer (NULL = nada) */
static const char *a4_action(const Player *p, const char *cls)
{
    for (int k = 0; k < 5; k++)
        if (strcmp(cls, k_a4_seals[k]) == 0 && !(s_a4_seals >> k & 1))
            return tr("A: Abrir el sello");
    if (strcmp(cls, "SoulStoneForge") == 0) {
        if (p->forge_q >= 4 || party_max(offsetof(Player, forge_q)) >= 4)
            return NULL;
        if (!(p->a4_flags & A4F_STONE))
            return player_has(p, "mss") ? tr("A: Poner la Piedra de Mefisto en la Forja") : tr("Hace falta la Piedra de Mefisto");
        const ItemType *w = p->equip[SLOT_WEAPON].type;
        static char text[80];
        if (w && strcmp(w->code, "hfh") == 0) {
            snprintf(text, sizeof text, tr("A: Golpear la piedra (%d de 3)"), s_a4_hits + 1);
            return text;
        }
        return tr("Hace falta el Martillo de la Forja Infernal en la mano");
    }
    if (strcmp(cls, "ReallyTheLastPortal") == 0)
        return tr("A: Entrar al portal");
    return NULL;
}

/* Sale el jefe del sello k, donde dice el original (con su escolta) */
static void a4_seal_boss(int b, float sx, float sy)
{
    float x = sx + (float)k_a4_bosses[b].dx, y = sy + (float)k_a4_bosses[b].dy;
    find_free_spot(&x, &y);
    const Level *lv = world_level_at(x, y);
    units_zone_begin(zone_at(x, y));
    s_a4_boss[k_a4_bosses[b].seal] = units_spawn_superunique(k_a4_bosses[b].boss, x, y, lv ? lv->mon_level : 30);
    units_zone_end();
}

/* Lo que pasa al usar un objeto del Acto 4: 1 si era uno de estos */
static int a4_interact(Player *p, const char *cls, int u, float ux, float uy)
{
    for (int k = 0; k < 5; k++) {
        if (strcmp(cls, k_a4_seals[k]) != 0)
            continue;
        if (s_a4_seals >> k & 1)
            return 1;
        s_a4_seals |= 1 << k;
        units_object_mode(u, "op", "on");
        audio_play_name("object_diabloseal", ux, uy);
        for (int b = 0; b < 3; b++)
            if (k_a4_bosses[b].seal == k)
                a4_seal_boss(b, ux, uy);
        return 1;
    }
    if (strcmp(cls, "SoulStoneForge") == 0) {
        if (party_max(offsetof(Player, forge_q)) >= 4)
            return 1;
        if (!(p->a4_flags & A4F_STONE)) { /* la piedra, sobre la Forja (para todos) */
            if (!player_has(p, "mss"))
                return 1;
            take_item(p, "mss");
            for (int i = 0; i < s_num_players; i++)
                s_pl[i].a4_flags |= A4F_STONE;
            units_object_mode(u, "op", "on");
            audio_play_name("quest_hellforge_place", ux, uy);
            return 1;
        }
        const ItemType *w = p->equip[SLOT_WEAPON].type;
        if (!w || strcmp(w->code, "hfh") != 0)
            return 1;
        if (++s_a4_hits <= 2) {
            audio_play_name(k_a4_flash[s_a4_hits - 1], ux, uy);
            return 1;
        }
        /* el tercero la rompe: el Martillo se va y empiezan las gemas */
        memset(&p->equip[SLOT_WEAPON], 0, sizeof p->equip[SLOT_WEAPON]);
        refresh_stats(p);
        refresh_look(p);
        units_object_mode(u, "s1", "s2");
        audio_play_name("quest_hellforge_smash", ux, uy);
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].forge_q < 4)
                s_pl[i].forge_q = 4;
        s_a4_gems = 4;
        s_a4_gem_tick = 0;
        s_a4_forge_x = ux;
        s_a4_forge_y = uy;
        quest_banner("La Piedra de Mefisto se rompió: contáselo a Caín", "music_quest_forge");
        return 1;
    }
    if (strcmp(cls, "ReallyTheLastPortal") == 0) { /* el epílogo (packet61 5) y a Harrogath (OperateFunction73) */
        if (party_lacks(offsetof(Player, a4_flags), A4F_END))
            queue_movie("act04end");
        for (int i = 0; i < s_num_players; i++)
            s_pl[i].a4_flags |= A4F_END;
        act_switch(5);
        return 1;
    }
    return 0;
}

/* Al armarse una zona: lo que ya pasó en la partida */
static void a4_zone_loaded(int z)
{
    int ids[8], n;
    for (int k = 0; k < 5; k++)
        if ((s_a4_seals >> k & 1) && units_find_all(k_a4_seals[k], ids, 1) == 1 && units_zone(ids[0]) == z)
            units_object_mode(ids[0], "on", NULL);
    if (party_max(offsetof(Player, forge_q)) >= 4 && units_find_all("SoulStoneForge", ids, 1) == 1 && units_zone(ids[0]) == z)
        units_object_mode(ids[0], "s2", NULL);
    else if (party_max(offsetof(Player, a4_flags)) & A4F_STONE && units_find_all("SoulStoneForge", ids, 1) == 1 &&
             units_zone(ids[0]) == z)
        units_object_mode(ids[0], "on", NULL);
    /* Izual muerto en esta partida no vuelve (su espíritu sí, si falta hablarle) */
    if (s_a4_izual && (n = units_find_all("izual", ids, 8)) > 0)
        for (int i = 0; i < n; i++)
            if (units_zone(ids[i]) == z)
                units_remove(ids[i]);
    if (s_a4_izual == 2 && party_max(offsetof(Player, izual_q)) <= 3 && units_find_all("izualghost", ids, 1) == 0 &&
        zone_at(s_a4_izual_x, s_a4_izual_y) == z) {
        units_zone_begin(z);
        units_spawn_npc("izualghost", s_a4_izual_x, s_a4_izual_y);
        units_zone_end();
    }
    /* Los jefes de los sellos que salieron y no murieron vuelven a salir (si
       no se fueron caminando a otra zona armada, donde siguen) */
    if (z == world_level_zone(108))
        for (int b = 0; b < 3; b++) {
            int k = k_a4_bosses[b].seal;
            float sx, sy;
            if ((s_a4_seals >> k & 1) && !(s_a4_boss_dead >> k & 1) && !a4_still(s_a4_boss[k], k_a4_bosses[b].boss) &&
                units_find_all(k_a4_seals[k], ids, 1) == 1) {
                units_position(ids[0], 1.0f, &sx, &sy);
                a4_seal_boss(b, sx, sy);
            }
        }
    /* (se soltó con Diablo vivo: sale de nuevo) */
    if (s_a4_diablo == 2 && z == world_level_zone(108) && !a4_still(s_a4_diablo_unit, "diablo"))
        s_a4_diablo = 1;
    /* El portal del final queda abierto al lado de Tyrael (también al volver
       al acto o al cargar la partida) */
    int portal = 0;
    for (int i = 0; i < s_num_players; i++)
        portal |= s_pl[i].a4_flags & A4F_PORTAL;
    if (portal && units_find_all("tyrael2", ids, 1) == 1 && units_zone(ids[0]) == z)
        a4_open_portal();
}

/* Cada tick, en el Acto 4 */
static void a4_quest_tick(void)
{
    int id;
    float x, y;
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        if (!p->level)
            continue;
        if (p->level->id == 105 && p->izual_q == 1) /* llegó al Plains of Despair */
            p->izual_q = 2;
        if (p->level->id == 108 && p->diablo_q >= 1 && p->diablo_q < 2) /* y al Santuario */
            p->diablo_q = 2;
        if (p->level->id == 108 && p->diablo_q < 3 && a4_seals_left() == 0 && s_a4_diablo >= 1)
            p->diablo_q = 3;
    }
    /* Izual: al morir, a los 3 ticks sale su espíritu donde cayó */
    if (!s_a4_izual && units_find_all("izual", &id, 1) == 1 && units_is_corpse(id)) {
        s_a4_izual = 1;
        s_a4_timer = 3;
        units_position(id, 1.0f, &s_a4_izual_x, &s_a4_izual_y);
        /* El espíritu sale donde cayó o, si ahí no entra, lo más cerca que se
           pueda (D2GAME_SpawnMonster con -1, 3 y 5 en ACT4Q1_SpawnIzualGhost):
           Izual es grande y caía pegado a un borde, donde no se llegaba a
           hablarle */
        find_free_spot(&s_a4_izual_x, &s_a4_izual_y);
        for (int i = 0; i < s_num_players; i++) {
            if (s_pl[i].izual_q < 3)
                s_pl[i].izual_q = 3;
            player_say(&s_pl[i], "act4_free_izual");
        }
        quest_banner("Izual murió: hablá con su espíritu", "music_quest_izual");
        save_party();
    }
    if (s_a4_izual == 1 && --s_a4_timer <= 0) {
        s_a4_izual = 2;
        units_zone_begin(zone_at(s_a4_izual_x, s_a4_izual_y));
        units_spawn_npc("izualghost", s_a4_izual_x, s_a4_izual_y);
        units_zone_end();
    }
    /* Hephasto: suelta el Martillo de la Forja Infernal (ACT4Q3_Callback08_MonsterKilled) */
    if (!s_a4_smith_dead && units_find_all("The Feature Creep", &id, 1) == 1 && units_is_corpse(id)) {
        s_a4_smith_dead = 1;
        units_position(id, 1.0f, &x, &y);
        int need = 0;
        for (int i = 0; i < s_num_players; i++)
            need |= s_pl[i].forge_q < 4 && !player_has(&s_pl[i], "hfh");
        find_free_spot(&x, &y); /* (cae donde se puede levantar) */
        if (need)
            drop_at("hfh", 0, x, y);
        if (getenv("D2_AT_TRACE"))
            printf("  el Martillo cae en (%.1f, %.1f)\n", x, y);
    }
    /* Las gemas de la Forja: cada 20 cuadros una tanda (una por jugador): una
       perfecta, dos impecables y una común (ACT4Q3_CreateReward) */
    if (s_a4_gems > 0 && ++s_a4_gem_tick >= 20) {
        static const char *const k_gems[3][7] = {
            { "gpv", "gpr", "gpb", "gpy", "gpg", "gpw", "skz" },
            { "gzv", "glr", "glb", "gly", "glg", "glw", "skl" },
            { "gsv", "gsr", "gsb", "gsy", "gsg", "gsw", "sku" },
        };
        int tier = s_a4_gems == 4 ? 0 : s_a4_gems == 1 ? 2 : 1;
        s_a4_gem_tick = 0;
        for (int i = 0; i < s_num_players; i++) {
            float a = (float)(rnd() % 628u) / 100.0f;
            drop_at(k_gems[tier][rnd() % 7u], 0, s_a4_forge_x + cosf(a) * 1.5f, s_a4_forge_y + sinf(a) * 1.5f);
        }
        s_a4_gems--;
    }
    /* Los jefes de los sellos y Diablo */
    for (int b = 0; b < 3; b++) {
        int k = k_a4_bosses[b].seal, u = s_a4_boss[k];
        if (a4_still(u, k_a4_bosses[b].boss) && units_is_corpse(u)) {
            s_a4_boss[k] = -1;
            s_a4_boss_dead |= 1 << k;
        }
    }
    int bosses = (s_a4_boss_dead & 1) + (s_a4_boss_dead >> 2 & 1) + (s_a4_boss_dead >> 4 & 1);
    if (!s_a4_diablo && a4_seals_left() == 0 && bosses == 3) {
        /* ACT4Q2_KillAllMonstersInCS: los demás monstruos del Santuario mueren, y a los 10 sale Diablo */
        int sanct = world_level_zone(108);
        for (int u = 0; u < units_count(); u++)
            if (units_alive_enemy(u) && units_zone(u) == sanct)
                units_play_death(u);
        s_a4_diablo = 1;
        s_a4_timer = 10;
    }
    if (s_a4_diablo == 1 && --s_a4_timer <= 0 && units_find("DiabloStart", &x, &y) == 0) {
        const Level *lv = world_level_at(x, y);
        units_zone_begin(zone_at(x, y));
        s_a4_diablo_unit = units_spawn_monster("diablo", x, y, lv ? lv->mon_level : 40);
        units_zone_end();
        s_a4_diablo = s_a4_diablo_unit >= 0 ? 2 : 1;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].diablo_q >= 1 && s_pl[i].diablo_q < 3)
                s_pl[i].diablo_q = 3;
    }
    if (s_a4_diablo == 2 && a4_still(s_a4_diablo_unit, "diablo") && units_is_corpse(s_a4_diablo_unit)) {
        s_a4_diablo = 3;
        for (int i = 0; i < s_num_players; i++) {
            if (s_pl[i].diablo_q < 4)
                s_pl[i].diablo_q = 4;
            player_say(&s_pl[i], "act4_kill_diablo");
        }
        quest_done("qstsa4q2", "music_quest_diablo");
        save_party();
    }
}

static void a4_quest_reset(void)
{
    s_a4_seals = s_a4_boss_dead = s_a4_diablo = s_a4_izual = s_a4_smith_dead = s_a4_hits = s_a4_gems = 0;
    s_a4_diablo_unit = -1;
    for (int k = 0; k < 5; k++)
        s_a4_boss[k] = -1;
}
