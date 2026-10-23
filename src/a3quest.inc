/* ---- Las misiones del Acto 3 (se incluye desde game.c) ----

   Como QUESTS/ACT3/A3Q1..A3Q6.cpp de D2MOO, con los textos y las voces de
   vo.json:
   - El Tomo de Lam Esen (A3Q1, tome_q): Alkor lo pide cuando el grupo llegó
     a la Kurast baja; está en el atril de un templo; Alkor da 5 puntos de
     atributo.
   - La Voluntad de Khalim (A3Q2, will_q): Caín la pide cuando el grupo llegó
     al Great Marsh; el ojo, el cerebro y el corazón salen de sus cofres y
     el mangual de un miembro del Consejo; el cubo los junta y la Voluntad
     rompe el Orbe cautivante de Travincal (dos golpes, en la mano), que
     abre la escalera al Durance.
   - El Puñal de la Vieja Religión (A3Q3, blade_q): Hratli lo pide cuando el
     grupo llegó al Flayer Jungle; el altar del claro de los pigmeos es un
     señuelo: sale un fetiche jefe con su escolta y la Gidbinn la suelta él
     (ACT3Q3_SpawnGidbinnBoss); Ormus da un anillo y Asheara un mercenario
     gratis.
   - El Pájaro Dorado (A3Q4, bird_q): el primer jefe (al azar o de nombre)
     que aparece en el acto (QUESTS_SetJadeFigurineBoss) suelta al morir la
     estatuilla de jade; Caín, Meshif (la cambia por el pájaro),
     Caín otra vez, Alkor (se queda con el pájaro) y, al volver, la Poción
     de la vida (+20 de vida para siempre).
   - El Templo Corrupto (A3Q5, temple_q): Ormus lo pide cuando el grupo llegó
     al Causeway; los tres del Consejo de Travincal; después los fanáticos
     huyen.
   - El Guardián (A3Q6, meph_q): Ormus lo pide cuando el Consejo murió; al
     morir Mefisto sube el puente de sangre y se abre el portal rojo.
   Más la palanca de las cloacas, que abre la escalera al nivel 2.

   El portal rojo lleva al Acto 4 (las marcas A3F_* están en game.c). */

static int s_a3_lever;        /* la palanca de las cloacas, en esta partida */
static int s_a3_opened;       /* los que ya se abrieron en esta partida (bit por k_a3_once) */
static int s_a3_orb_hits;     /* golpes al Orbe cautivante */
static int s_a3_council;      /* los del Consejo de Travincal que ya murieron (bit por cada uno) */
static int s_a3_meph_dead, s_a3_bird_dropped, s_a3_flail_dropped, s_a3_cube_dropped;
static int s_a3_bird_boss = -1, s_a3_bird_zone = -1; /* el jefe de la estatuilla y su zona */
static int s_a3_gid_boss = -1, s_a3_gid_zone = -1;   /* el fetiche que tiene la Gidbinn y su zona */

static const char *const k_council[3] = { "Ismail Vilehand", "Geleb Flamefinger", "Toorc Icefist" };
/* Los que se abren una vez por partida: los tres cofres de Khalim, el atril y el altar */
static const char *const k_a3_once[5] = { "KhalimEyeChest", "KhalimBrainChest", "KhalimHeartChest", "LamesEnsTome",
                                          "GidBInnDecoy" };

static int a3_once_bit(const char *cls)
{
    for (int k = 0; k < 5; k++)
        if (strcmp(cls, k_a3_once[k]) == 0)
            return 1 << k;
    return 0;
}
static const struct { const char *code, *key; int bit; } k_khalim[4] = { /* como las comenta Caín */
    { "qey", "A3Q2EarlyReturnEyeCain", A3F_SEEN_EYE }, { "qbr", "A3Q2EarlyReturnBrainCain", A3F_SEEN_BRAIN },
    { "qhr", "A3Q2EarlyReturnHeartCain", A3F_SEEN_HEART }, { "qf1", "A3Q2EarlyReturnFlailCain", A3F_SEEN_FLAIL },
};

static int a3_orb_smashed(void)
{
    return party_max(offsetof(Player, will_q)) >= 3;
}

/* ¿Le falta esa parte de Khalim? (no la tiene, ni la Voluntad, ni rompió el Orbe) */
static int a3_needs_part(const Player *p, const char *code)
{
    return p->will_q < 3 && !player_has(p, code) && !player_has(p, "qf2");
}

/* Las que el registro pide ahora (qstsa3q11...) */
static const char *quest_goal3(const Player *p, int q, int *state)
{
    switch (q) {
    case 0: /* el Tomo de Lam Esen */
        *state = p->tome_q >= 3 ? 2 : p->tome_q > 0 ? 1 : 0;
        return p->tome_q >= 3 ? NULL : p->tome_q == 2 ? "qstsa3q12" : "qstsa3q11";
    case 1: { /* la Voluntad de Khalim */
        *state = p->will_q >= 3 ? 2 : p->will_q > 0 ? 1 : 0;
        if (p->will_q >= 3)
            return NULL;
        if (p->will_q == 0)
            return (p->a3_flags & A3F_GM) ? "qstsa3q21a" : "qstsa3q21";
        if (player_has(p, "qf2"))
            return "qstsa3q26";
        if (player_has(p, "qey") && player_has(p, "qbr") && player_has(p, "qhr") && player_has(p, "qf1"))
            return "qstsa3q25";
        return !player_has(p, "qey") ? "qstsa3q21" : !player_has(p, "qbr") ? "qstsa3q22" : !player_has(p, "qf1") ? "qstsa3q23"
                                                                                                                  : "qstsa3q24";
    }
    case 2: /* la Gidbinn */
        *state = p->blade_q >= 3 ? 2 : p->blade_q > 0 ? 1 : 0;
        if (p->blade_q >= 3)
            return NULL;
        return p->blade_q == 2 ? "qstsa3q33" : p->blade_q == 1 ? "qstsa3q31" : (p->a3_flags & A3F_FJ) ? "qstsa3q31a" : "qstsa3q31";
    case 3: /* el Pájaro Dorado */
        *state = p->bird_q >= 6 ? 2 : p->bird_q > 0 ? 1 : 0;
        if (p->bird_q >= 6)
            return NULL;
        return p->bird_q <= 1 ? "qstsa3q41" : p->bird_q == 2 ? "qstsa3q42" : p->bird_q == 3 ? "qstsa3q43"
               : p->bird_q == 4 ? "qstsa3q44" : "qstsa3q45";
    case 4: /* el Templo Corrupto */
        *state = p->temple_q >= 4 ? 2 : p->temple_q > 0 ? 1 : 0;
        if (p->temple_q >= 4)
            return NULL;
        if (p->temple_q == 0)
            return (p->a3_flags & A3F_CAUSEWAY) ? "qstsa3q51a" : "qstsa3q51";
        return p->temple_q == 1 ? "qstsa3q51" : p->temple_q == 2 ? "qstsa3q52" : "qstsa3q53";
    default: /* el Guardián */
        *state = p->meph_q >= 2 ? 2 : p->meph_q > 0 ? 1 : 0;
        if (p->meph_q >= 2)
            return NULL;
        if (p->meph_q == 0)
            return p->temple_q >= 3 ? "qstsa3q61a" : "qstsa3q61";
        return !a3_orb_smashed() ? "qstsa3q61" : p->level && p->level->id == 102 ? "qstsa3q63" : "qstsa3q62";
    }
}

/* Lo que dicen los NPC del muelle de las misiones (en el orden del
   original: primero las recompensas, después los pedidos) */
enum {
    A3N_NONE, A3N_BIRD_ASHEARA, A3N_BIRD_CAIN, A3N_BIRD_MESHIF, A3N_BIRD_ASK, A3N_BIRD_ALKOR, A3N_BIRD_POTION,
    A3N_TOME_DONE, A3N_TOME_ASK, A3N_BLADE_DONE, A3N_BLADE_MERC, A3N_BLADE_ASK, A3N_WILL_ASK, A3N_WILL_DONE,
    A3N_WILL_PART, A3N_TEMPLE_DONE, A3N_TEMPLE_ASK, A3N_MEPH_ASK,
};
enum { A3_ORMUS = 19, A3_ALKOR, A3_ASHEARA, A3_HRATLI, A3_NATALYA, A3_MESHIF, A3_CAIN };

/* Qué toca decir ahora (sin cambiar nada); *part: la parte de Khalim que comenta Caín */
static int a3_npc_pick(const Player *p, int who, int *part)
{
    if (who < A3_ORMUS || who > A3_CAIN)
        return A3N_NONE;
    if ((who == A3_CAIN || who == A3_ASHEARA) && p->bird_q <= 1 && player_has(p, "j34"))
        return who == A3_CAIN ? A3N_BIRD_CAIN : A3N_BIRD_ASHEARA;
    if (who == A3_MESHIF && p->bird_q == 2 && player_has(p, "j34"))
        return A3N_BIRD_MESHIF;
    if ((who == A3_CAIN || who == A3_ASHEARA || who == A3_HRATLI) && p->bird_q == 3 && player_has(p, "g34"))
        return A3N_BIRD_ASK;
    if (who == A3_ALKOR && p->bird_q >= 3 && p->bird_q <= 4 && player_has(p, "g34"))
        return A3N_BIRD_ALKOR;
    if (who == A3_ALKOR && p->bird_q == 5 && p->talk_away[who])
        return A3N_BIRD_POTION;
    if (who == A3_ALKOR && p->tome_q < 3 && (p->tome_q == 2 || player_has(p, "bbb")))
        return A3N_TOME_DONE;
    if (who == A3_ALKOR && p->tome_q == 0 && (p->a3_flags & A3F_LK))
        return A3N_TOME_ASK;
    if (who == A3_ORMUS && p->blade_q < 3 && player_has(p, "g33"))
        return A3N_BLADE_DONE;
    if (who == A3_ASHEARA && p->blade_q >= 3 && !(p->a3_flags & A3F_HIRED))
        return A3N_BLADE_MERC;
    if (who == A3_HRATLI && p->blade_q == 0 && (p->a3_flags & (A3F_FJ | A3F_HRATLI)) == (A3F_FJ | A3F_HRATLI))
        return A3N_BLADE_ASK;
    if (who == A3_CAIN && p->will_q == 0 && (p->a3_flags & A3F_GM))
        return A3N_WILL_ASK;
    if (who == A3_CAIN && p->will_q == 1 && player_has(p, "qf2"))
        return A3N_WILL_DONE;
    for (int k = 0; who == A3_CAIN && p->will_q >= 1 && p->will_q < 3 && k < 4; k++)
        if (!(p->a3_flags & k_khalim[k].bit) && player_has(p, k_khalim[k].code)) {
            *part = k;
            return A3N_WILL_PART;
        }
    if (who == A3_ORMUS && p->temple_q == 3)
        return A3N_TEMPLE_DONE;
    if (who == A3_ORMUS && p->temple_q == 0 && (p->a3_flags & A3F_CAUSEWAY))
        return A3N_TEMPLE_ASK;
    if (who == A3_ORMUS && p->meph_q == 0 && p->temple_q >= 3)
        return A3N_MEPH_ASK;
    return A3N_NONE;
}

static int a3_npc_news(const Player *p, int who)
{
    int part;
    return a3_npc_pick(p, who, &part) != A3N_NONE;
}

/* Lo dice y hace lo que corresponde; 1 si dijo algo */
static int a3_npc_quest(Player *p, int who, char *key, size_t cap)
{
    int part = 0;
    switch (a3_npc_pick(p, who, &part)) {
    case A3N_BIRD_ASHEARA: /* "Solo Deckard Caín puede identificar esto" */
        snprintf(key, cap, "A3Q4Init1Asheara");
        return 1;
    case A3N_BIRD_CAIN:
        p->bird_q = 2;
        snprintf(key, cap, "A3Q4Init1CainAct3");
        return 1;
    case A3N_BIRD_MESHIF: /* cambia la estatuilla por el Pájaro Dorado */
        take_item(p, "j34");
        give_quest_item(p, "g34");
        p->bird_q = 3;
        snprintf(key, cap, "A3Q4Init2MeshifAct3");
        return 1;
    case A3N_BIRD_ASK:
        if (who == A3_CAIN)
            p->bird_q = 4;
        snprintf(key, cap, who == A3_CAIN ? "A3Q4Init3CainAct3" : who == A3_ASHEARA ? "A3Q4Init3Asheara" : "A3Q4Init3Hratli");
        return 1;
    case A3N_BIRD_ALKOR: /* se queda con el pájaro; la poción, cuando vuelva */
        take_item(p, "g34");
        p->bird_q = 5;
        p->talk_away[who] = 0;
        snprintf(key, cap, "A3Q4AfterInitAlkor");
        return 1;
    case A3N_BIRD_POTION:
        give_quest_item(p, "xyz");
        p->bird_q = 6;
        quest_done("qstsa3q4", NULL);
        snprintf(key, cap, "A3Q4SuccessfulAlkor");
        return 1;
    case A3N_TOME_DONE: /* el tomo lo devuelve uno; los 5 puntos, cada uno */
        if (player_has(p, "bbb")) {
            take_item(p, "bbb");
            party_raise(offsetof(Player, tome_q), 2);
        }
        p->tome_q = 3;
        p->stat_points += 5;
        refresh_stats(p);
        quest_done("qstsa3q1", NULL);
        player_say(p, "act3_complete_lamesen");
        snprintf(key, cap, "A3Q1SuccessfulAlkor");
        return 1;
    case A3N_TOME_ASK:
        party_raise(offsetof(Player, tome_q), 1);
        snprintf(key, cap, "A3Q1InitAlkor");
        return 1;
    case A3N_BLADE_DONE: { /* Ormus se queda con la Gidbinn y da un anillo */
        take_item(p, "g33");
        party_raise(offsetof(Player, blade_q), 3);
        const ItemType *t = items_find("rin");
        if (t) {
            Item ring = items_make(t);
            items_make_magic(&ring, p->level_num > 1 ? p->level_num : 1);
            if (!bag_add(p, &ring)) {
                items_ground_add(&ring, p->x + 0.5f, p->y + 0.5f);
                inventory_full(p);
            }
        }
        quest_done("qstsa3q3", NULL);
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act3_complete_blade");
        snprintf(key, cap, "A3Q3SuccessfulOrmus");
        return 1;
    }
    case A3N_BLADE_MERC: /* el próximo Lobo de Hierro es gratis */
        p->a3_flags |= A3F_HIRED;
        p->free_hire = 1;
        snprintf(key, cap, "A3Q3SuccessfulAsheara");
        return 1;
    case A3N_BLADE_ASK:
        party_raise(offsetof(Player, blade_q), 1);
        snprintf(key, cap, "A3Q3InitHratli");
        return 1;
    case A3N_WILL_ASK:
        party_raise(offsetof(Player, will_q), 1);
        snprintf(key, cap, "A3Q2InitCain");
        return 1;
    case A3N_WILL_DONE:
        p->will_q = 2;
        snprintf(key, cap, "A3Q2SuccessfulCain");
        return 1;
    case A3N_WILL_PART:
        p->a3_flags |= k_khalim[part].bit;
        snprintf(key, cap, "%s", k_khalim[part].key);
        return 1;
    case A3N_TEMPLE_DONE:
        p->temple_q = 4;
        quest_done("qstsa3q5", NULL);
        snprintf(key, cap, "A3Q5SuccessfulOrmus");
        return 1;
    case A3N_TEMPLE_ASK:
        party_raise(offsetof(Player, temple_q), 1);
        snprintf(key, cap, "A3Q5InitOrmus");
        return 1;
    case A3N_MEPH_ASK:
        party_raise(offsetof(Player, meph_q), 1);
        snprintf(key, cap, "A3Q6InitOrmus");
        return 1;
    default:
        return 0;
    }
}

/* El objeto de misión del Acto 3 que tiene al lado: qué se puede hacer (NULL = nada) */
static const char *a3_action(const Player *p, const char *cls)
{
    static char text[80];
    if (s_a3_opened & a3_once_bit(cls))
        return NULL;
    if (strcmp(cls, "LamesEnsTome") == 0 && p->tome_q < 2 && !player_has(p, "bbb"))
        return tr("A: Tomar el Tomo de Lam Esen");
    if (strcmp(cls, "GidBInnDecoy") == 0 && p->blade_q < 3 && !player_has(p, "g33"))
        return tr("A: Tomar la Gidbinn");
    for (int k = 0; k < 3; k++) {
        static const char *const k_chests[3] = { "KhalimEyeChest", "KhalimBrainChest", "KhalimHeartChest" };
        if (strcmp(cls, k_chests[k]) == 0 && a3_needs_part(p, k_khalim[k].code))
            return tr("A: Abrir el cofre");
    }
    if (strcmp(cls, "BeneathTheCityLever") == 0 && !s_a3_lever)
        return tr("A: Tirar de la palanca");
    if (strcmp(cls, "CompellingOrb") == 0 && !a3_orb_smashed()) {
        const ItemType *w = p->equip[SLOT_WEAPON].type;
        if (w && strcmp(w->code, "qf2") == 0) {
            snprintf(text, sizeof text, tr("A: Golpear el Orbe cautivante (%d de 2)"), s_a3_orb_hits + 1);
            return text;
        }
        return tr("Hace falta la Voluntad de Khalim en la mano");
    }
    if (strcmp(cls, "HellGate") == 0 && (s_a3_meph_dead || party_max(offsetof(Player, meph_q)) >= 2))
        return tr("A: Entrar al portal");
    return NULL;
}

/* El objeto de Kurast se abre: lo que sale y lo que cambia. 1 si era uno de estos. */
static int a3_interact(Player *p, const char *cls, int u, float ux, float uy)
{
    s_a3_opened |= a3_once_bit(cls);
    if (strcmp(cls, "LamesEnsTome") == 0) {
        units_object_mode(u, "op", "on");
        audio_play_name("object_tome", ux, uy);
        give_quest_item(p, "bbb");
        return 1;
    }
    if (strcmp(cls, "GidBInnDecoy") == 0) { /* el señuelo: sale el fetiche que tiene la Gidbinn, con su escolta */
        units_object_mode(u, "op", "on");
        audio_play_name("object_gidbinn_decoy", ux, uy);
        const Level *lv = world_level_at(ux, uy);
        float bx = ux + 3.0f, by = uy + 3.0f;
        find_free_spot(&bx, &by);
        s_a3_gid_zone = units_zone(u);
        units_zone_begin(s_a3_gid_zone);
        s_a3_gid_boss = units_spawn_boss_pack("fetish11", bx, by, lv ? lv->mon_level : 20);
        units_zone_end();
        if (s_a3_gid_boss < 0) /* (no salió: se puede volver a tocar) */
            s_a3_opened &= ~a3_once_bit(cls);
        return 1;
    }
    for (int k = 0; k < 3; k++) {
        static const char *const k_chests[3] = { "KhalimEyeChest", "KhalimBrainChest", "KhalimHeartChest" };
        if (strcmp(cls, k_chests[k]) != 0)
            continue;
        units_object_mode(u, "op", "on");
        audio_play_name("object_chest_large", ux, uy);
        int n = 0;
        for (int i = 0; i < s_num_players; i++)
            if (a3_needs_part(&s_pl[i], k_khalim[k].code))
                drop_at(k_khalim[k].code, 0, ux + 0.8f * (float)n++, uy + 0.6f);
        for (int g = 0; g < 2 + (int)(rnd() % 3u); g++)
            drop_at("gld", 30 + (int)(rnd() % 120u), ux - 1.0f + 0.7f * (float)g, uy - 0.7f);
        return 1;
    }
    if (strcmp(cls, "BeneathTheCityLever") == 0) { /* la escalera al nivel 2 de las cloacas */
        s_a3_lever = 1;
        units_object_mode(u, "op", "on");
        audio_play_name("object_sewer_lever", ux, uy);
        int stairs;
        if (units_find_all("BeneathTheCityStairs", &stairs, 1) == 1)
            units_object_mode(stairs, "op", "on");
        audio_play_name("object_stairs", ux, uy);
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act3_complete_beneath");
        return 1;
    }
    if (strcmp(cls, "CompellingOrb") == 0) { /* dos golpes con la Voluntad en la mano */
        const ItemType *w = p->equip[SLOT_WEAPON].type;
        if (!w || strcmp(w->code, "qf2") != 0)
            return 1;
        if (++s_a3_orb_hits < 2)
            return 1;
        units_object_mode(u, "op", "on");
        int orb, door;
        if (units_find_all("compellingorb", &orb, 1) == 1)
            units_play_death(orb);
        if (units_find_all("MephistoDoor", &door, 1) == 1)
            units_object_mode(door, "op", "on");
        audio_play_name("compellingorb_death", ux, uy);
        for (int i = 0; i < s_num_players; i++) { /* las partes de Khalim se van (ACT3Q5_UnitIterate_DeleteKhalimItems) */
            Player *o = &s_pl[i];
            static const char *const k_parts[5] = { "qey", "qbr", "qhr", "qf1", "qf2" };
            for (int k = 0; k < 5; k++)
                take_item(o, k_parts[k]);
            for (int s = 0; s < SLOT_COUNT; s++)
                if (o->equip[s].type && strcmp(o->equip[s].type->code, "qf2") == 0) {
                    memset(&o->equip[s], 0, sizeof o->equip[s]);
                    refresh_stats(o);
                    refresh_look(o);
                }
            if (o->will_q < 3)
                o->will_q = 3;
        }
        quest_done("qstsa3q2", "music_quest_compelling");
        return 1;
    }
    if (strcmp(cls, "HellGate") == 0) {
        /* Al Acto 4, los dos; la primera vez, con su cinemática
           (QUESTS_HellPortalActChange) */
        if (!s_a3_meph_dead && party_max(offsetof(Player, meph_q)) < 2)
            return 1;
        if (party_lacks(offsetof(Player, a3_flags), A3F_HELLGATE))
            queue_movie("act04start");
        for (int i = 0; i < s_num_players; i++)
            s_pl[i].a3_flags |= A3F_HELLGATE;
        act_switch(4);
        return 1;
    }
    return 0;
}

/* Al armarse una zona: lo que ya pasó en la partida (el Orbe roto, la
   escalera abierta, la palanca, el puente) */
/* ¿Todavía puede salir la estatuilla de jade? (ACT3Q4_Callback13_PlayerStartedGame) */
static int a3_bird_open(void)
{
    for (int i = 0; i < s_num_players; i++)
        if (player_has(&s_pl[i], "j34") || player_has(&s_pl[i], "g34"))
            return 0;
    return !s_a3_bird_dropped && party_max(offsetof(Player, bird_q)) == 0;
}

static void a3_zone_loaded(int z)
{
    int id;
    /* Si se rearma la zona del fetiche de la Gidbinn, se fue con ella: el
       altar se puede volver a tocar; la del jefe del pájaro: es el próximo */
    if (s_a3_gid_boss >= 0 && z == s_a3_gid_zone) {
        s_a3_gid_boss = -1;
        s_a3_opened &= ~a3_once_bit("GidBInnDecoy");
    }
    if (z == s_a3_bird_zone)
        s_a3_bird_boss = -1;
    if (s_act == 3 && s_a3_bird_boss < 0 && a3_bird_open() && (s_a3_bird_boss = units_bird_boss_in_zone(z)) >= 0)
        s_a3_bird_zone = z;
    for (int k = 0; k < 5; k++)
        if ((s_a3_opened >> k & 1) && units_find_all(k_a3_once[k], &id, 1) == 1 && units_zone(id) == z)
            units_object_mode(id, "on", NULL);
    if (a3_orb_smashed()) {
        if (units_find_all("CompellingOrb", &id, 1) == 1 && units_zone(id) == z)
            units_object_mode(id, "on", NULL);
        if (units_find_all("compellingorb", &id, 1) == 1 && units_zone(id) == z)
            units_remove(id);
        if (units_find_all("MephistoDoor", &id, 1) == 1 && units_zone(id) == z)
            units_object_mode(id, "on", NULL);
    }
    if (s_a3_lever && units_find_all("BeneathTheCityStairs", &id, 1) == 1 && units_zone(id) == z)
        units_object_mode(id, "on", NULL);
    if ((s_a3_meph_dead || party_max(offsetof(Player, meph_q)) >= 2) && units_find_all("MephistoBridge", &id, 1) == 1 &&
        units_zone(id) == z) {
        units_object_mode(id, "on", NULL);
        units_object_unblock(&s_map, id);
        if (units_find_all("HellGate", &id, 1) == 1)
            units_object_mode(id, "on", NULL);
    }
}

/* ¿Se puede usar ese salto? (la escalera al Durance, hasta romper el Orbe;
   la del nivel 2 de las cloacas, hasta tirar de la palanca) */
static const char *a3_warp_closed(const Warp *w)
{
    if (s_act == 5) /* (los del Acto 5: a5quest.inc) */
        return a5_warp_closed(w);
    if (s_act != 3)
        return NULL;
    if (w->to == world_level_zone(100) && w->from == world_level_zone(83) && !a3_orb_smashed())
        return tr("La escalera está cerrada: hay que romper el Orbe cautivante");
    if (w->to == world_level_zone(93) && !s_a3_lever)
        return tr("La reja está cerrada");
    return NULL;
}

static int a3_warp_open(Player *p, const Warp *w)
{
    const char *msg = a3_warp_closed(w);
    if (!msg)
        return 1;
    static uint32_t said[MAX_PLAYERS];
    int i = (int)(p - s_pl);
    if (s_ticks - said[i] > 3 * TICK_RATE || said[i] == 0) {
        said[i] = s_ticks;
        show_message(p, msg);
    }
    return 0;
}

/* Cada tick: los lugares a los que llegó el grupo, los objetos que levantó,
   el Consejo, Mefisto y el primer jefe (el de la estatuilla) */
static void a3_quest_tick(void)
{
    static const struct { int level, flag; } k_reach[] = { { 79, A3F_LK }, { 77, A3F_GM }, { 78, A3F_FJ }, { 82, A3F_CAUSEWAY } };
    static const struct { int level; const char *line; int bit; } k_finds[] = {
        { 92, "act3_find_beneath", 1 << 16 }, { 83, "act3_find_blackened", 1 << 17 }, { 100, "act3_find_guardian", 1 << 18 },
    };
    for (int i = 0; i < s_num_players; i++) {
        Player *p = &s_pl[i];
        if (!p->level)
            continue;
        for (unsigned k = 0; k < sizeof k_reach / sizeof k_reach[0]; k++)
            if (p->level->id == k_reach[k].level && !(p->a3_flags & k_reach[k].flag))
                for (int j = 0; j < s_num_players; j++) /* (el original lo anota para todo el grupo) */
                    s_pl[j].a3_flags |= k_reach[k].flag;
        for (unsigned k = 0; k < sizeof k_finds / sizeof k_finds[0]; k++)
            if (p->level->id == k_finds[k].level && !(p->said & k_finds[k].bit)) {
                p->said |= k_finds[k].bit;
                player_say(p, k_finds[k].line);
            }
        if (p->level->id == 83 && p->temple_q == 1) /* encontró el Templo */
            p->temple_q = 2;
        if (p->bird_q == 0 && player_has(p, "j34")) {
            p->bird_q = 1;
            player_say(p, "act3_find_jade");
        }
        if (p->tome_q < 2 && player_has(p, "bbb")) {
            p->tome_q = 2;
            player_say(p, "act3_find_lamesen");
        }
        if (p->blade_q < 2 && player_has(p, "g33"))
            p->blade_q = 2;
    }
    /* El Consejo de Travincal: el primero que muere suelta el Mangual (uno por
       jugador que lo necesita) y el que sigue, un Cubo Horádrico a los que no
       tienen (ACT3Q5_Callback08_MonsterKilled); los tres, el Templo */
    for (int k = 0; k < 3; k++) {
        int id;
        float x, y;
        if ((s_a3_council >> k & 1) || units_find_all(k_council[k], &id, 1) != 1 || !units_is_corpse(id))
            continue;
        s_a3_council |= 1 << k;
        units_position(id, 1.0f, &x, &y);
        int n = 0;
        if (!s_a3_flail_dropped) {
            for (int i = 0; i < s_num_players; i++)
                if (a3_needs_part(&s_pl[i], "qf1"))
                    drop_at("qf1", 0, x + 0.8f * (float)n++, y);
            s_a3_flail_dropped = n > 0; /* (si nadie lo necesitaba, lo suelta el próximo) */
        } else if (!s_a3_cube_dropped) {
            s_a3_cube_dropped = 1;
            for (int i = 0; i < s_num_players; i++)
                if (!player_has(&s_pl[i], "box"))
                    drop_at("box", 0, x + 0.8f * (float)n++, y);
        }
        if (s_a3_council == 7) {
            for (int i = 0; i < s_num_players; i++)
                if (s_pl[i].temple_q < 3)
                    s_pl[i].temple_q = 3;
            quest_banner("El Consejo murió: volvé con Ormus", "music_quest_compelling");
            for (int i = 0; i < s_num_players; i++)
                player_say(&s_pl[i], "act3_complete_blackened");
        }
        save_party();
    }
    units_zealots_flee(party_max(offsetof(Player, temple_q)) >= 3);
    /* Mefisto: el puente de sangre y el portal rojo (ACT3Q6_SetObjectModes) */
    int id;
    if (!s_a3_meph_dead && units_find_all("mephisto", &id, 1) == 1 && units_is_corpse(id)) {
        s_a3_meph_dead = 1;
        for (int i = 0; i < s_num_players; i++)
            if (s_pl[i].meph_q < 2)
                s_pl[i].meph_q = 2;
        int obj;
        if (units_find_all("MephistoBridge", &obj, 1) == 1) {
            float ox, oy;
            units_object_mode(obj, "op", "on");
            units_object_unblock(&s_map, obj);
            units_position(obj, 1.0f, &ox, &oy);
            audio_play_name("object_bridge_mephisto", ox, oy);
        }
        if (units_find_all("HellGate", &obj, 1) == 1)
            units_object_mode(obj, "op", "on");
        /* y la Piedra de Mefisto (ACT3Q6_Callback08: la de la Forja Infernal del Acto 4), para los que no la tienen */
        float mx, my;
        units_position(id, 1.0f, &mx, &my);
        for (int i = 0, n = 0; i < s_num_players; i++)
            if (!player_has(&s_pl[i], "mss") && s_pl[i].forge_q < 4 && !(s_pl[i].a4_flags & A4F_STONE))
                drop_at("mss", 0, mx + 0.8f * (float)n++, my);
        quest_done("qstsa3q6", "music_quest_andariel");
        for (int i = 0; i < s_num_players; i++)
            player_say(&s_pl[i], "act3_complete_guardian");
        save_party();
    }
    /* La Gidbinn: la suelta el fetiche jefe del altar al morir (ACT3Q3_Callback08_MonsterKilled) */
    float bx, by;
    if (s_a3_gid_boss >= 0 && units_zone(s_a3_gid_boss) != s_a3_gid_zone) { /* (se soltó su zona) */
        s_a3_gid_boss = -1;
        s_a3_opened &= ~a3_once_bit("GidBInnDecoy");
    }
    if (s_a3_gid_boss >= 0 && units_is_corpse(s_a3_gid_boss)) {
        units_position(s_a3_gid_boss, 1.0f, &bx, &by);
        s_a3_gid_boss = -1;
        drop_at("g33", 0, bx, by);
    }
    /* El Pájaro Dorado: la estatuilla de jade, del jefe marcado al morir
       (ACT3Q4_Callback08_MonsterKilled); si se soltó su zona, es el próximo */
    if (s_a3_bird_boss >= 0 && units_zone(s_a3_bird_boss) != s_a3_bird_zone)
        s_a3_bird_boss = -1;
    if (s_a3_bird_boss >= 0 && units_is_corpse(s_a3_bird_boss)) {
        int n = 0;
        units_position(s_a3_bird_boss, 1.0f, &bx, &by);
        s_a3_bird_boss = -1;
        if (a3_bird_open()) {
            s_a3_bird_dropped = 1;
            if (getenv("D2_AT_TRACE"))
                printf("  la estatuilla de jade cae en (%.0f, %.0f) %s\n", bx, by, world_zone_name(zone_at(bx, by)));
            for (int i = 0; i < s_num_players; i++) /* (una para cada uno, como el Mangual) */
                drop_at("j34", 0, bx + 0.8f * (float)n++, by);
        }
    }
}

/* D2_ZONETEST: Mefisto muerto, para que las zonas se armen con el puente arriba */
static void a3_zone_test_prep(void)
{
    s_a3_meph_dead = 1;
}

static void a3_quest_reset(void)
{
    s_a3_lever = s_a3_orb_hits = s_a3_council = s_a3_opened = 0;
    s_a3_meph_dead = s_a3_bird_dropped = s_a3_flail_dropped = s_a3_cube_dropped = 0;
    s_a3_bird_boss = s_a3_gid_boss = s_a3_bird_zone = s_a3_gid_zone = -1;
}

/* La Poción de la vida: 20 de vida para siempre, una vez */
static int a3_use_item(Player *p, int i)
{
    if (strcmp(p->bag[i].type->code, "xyz") != 0)
        return 0;
    if (p->a3_flags & A3F_LIFE) {
        show_message(p, "Ya tomaste esta poción");
        return 1;
    }
    p->a3_flags |= A3F_LIFE;
    p->hp_extra += 20;
    bag_remove(p, i);
    refresh_stats(p);
    audio_play_name("item_potion_drink", AUDIO_NOPOS, AUDIO_NOPOS);
    show_message(p, "Ganaste 20 de vida");
    save_character(p);
    return 1;
}
