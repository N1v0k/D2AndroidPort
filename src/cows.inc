/* ---- El nivel secreto de las vacas ----
   (incluido en game.c, después de objects.inc)

   Como el original (D2MOO): el cuerpo de Wirt en Tristram suelta la Pierna
   de Wirt la primera vez que se usa (OBJECTS_OperateFunction33_WirtsBody, en
   objects.inc). La pierna con el portal en el cubo (cubemain.txt: "Wirt's Leg
   + Tome of Town Portal -> Cow Portal") abre un portal rojo al lado del
   jugador hacia la Granja Muu Muu (ACT1Q4_CreateCowPortal): solo en el
   Campamento de las Pícaras, con Baal muerto (A5Q6 cumplida), si no mató al
   Rey Vaca (A1Q4 QFLAG_CUSTOM6) y uno por partida (QFLAG_CUSTOM7, del juego:
   no se guarda). Si no, "imposible". El de vuelta está en la granja, donde
   llega (world_cow_arrival). Al morir el Rey Vaca (en el medio del vivac,
   bivouac.ds1), todos los que están en la granja ya no pueden abrirlo y deja
   8 pociones de aguante (ACT1Q4_UnitIterate_SetCustom6Flag). El conjunto del
   Rey Vaca solo cae de las vacas (ITEMDROPFLAG_HELLBOVINE, items.c).

   Diferencia: el port no tiene el Tomo de Portal (pedido del usuario: los
   pergaminos se apilan de a 20), así que la receta es la pierna con la pila
   de pergaminos de portal, y gasta uno. */

#define PORTAL_COW      22 /* del campamento a la granja */
#define PORTAL_COW_BACK 23
#define COW_LEVEL       39 /* Moo Moo Farm en levels.txt */
#define COW_TOWN        1  /* Rogue Encampment */

static int   s_cow_open;       /* el portal ya se abrió en esta partida */
static int   s_cow_linked;     /* está en los saltos del mundo del Acto 1 armado */
static float s_cow_x, s_cow_y; /* el del campamento */
static int   s_cow_king_dead;  /* el Rey Vaca ya murió en esta partida */

static void cow_reset(void)
{
    s_cow_open = s_cow_linked = s_cow_king_dead = 0;
    world_portal(PORTAL_COW, -1, 0, 0, -1, 0, 0);
    world_portal(PORTAL_COW_BACK, -1, 0, 0, -1, 0, 0);
}

/* El dibujo de los portales en la zona z, si está abierto */
static void cow_zone_loaded(int z)
{
    float fx, fy;
    if (!s_cow_open || s_act != 1 || world_cow_arrival(&fx, &fy) != 0)
        return;
    a2_portal_object(z, "PortalPermanent", s_cow_x, s_cow_y);
    a2_portal_object(z, "PortalPermanent", fx, fy);
}

/* Los dos saltos (otra vez cada vez que se arma el mundo del Acto 1) */
static void cow_link(void)
{
    int town = world_level_zone(COW_TOWN), farm = world_level_zone(COW_LEVEL);
    float fx, fy;
    if (town < 0 || farm < 0 || world_cow_arrival(&fx, &fy) != 0)
        return;
    world_portal(PORTAL_COW, town, s_cow_x, s_cow_y, farm, fx + 2.0f, fy + 2.0f);
    world_portal(PORTAL_COW_BACK, farm, fx, fy, town, s_cow_x + 2.0f, s_cow_y + 2.0f);
    s_cow_linked = 1;
    cow_zone_loaded(town);
    cow_zone_loaded(farm);
}

/* ¿Hay en el cubo la pierna y los pergaminos, y nada más? Devuelve dónde
   están los pergaminos (-1 = no es la receta) */
static int cow_recipe_scrolls(const Player *p)
{
    int leg = -1, tsc = -1;
    if (p->cube_count != 2)
        return -1;
    for (int k = 0; k < 2; k++) {
        const char *code = p->cube[k].type ? p->cube[k].type->code : "";
        if (strcmp(code, "leg") == 0)
            leg = k;
        else if (strcmp(code, "tsc") == 0)
            tsc = k;
    }
    return leg >= 0 && tsc >= 0 ? tsc : -1;
}

/* El botón Transmutar con la pierna de Wirt y los pergaminos de portal: el
   portal a la granja (o "imposible"). 0 = no es esta receta */
static int cow_recipe(Player *p)
{
    int tsc = cow_recipe_scrolls(p);
    if (tsc < 0)
        return 0;
    const char *why = NULL;
    if (s_cow_open)
        why = "El portal ya se abrió en esta partida";
    else if (p->cow_king)
        why = "Ya mataste al Rey Vaca: el portal no se abre más";
    else if (p->baal_q < 4)
        why = "Primero hay que derrotar a Baal";
    else if (!p->level || p->level->id != COW_TOWN)
        why = "Solo se abre en el Campamento de las Pícaras";
    if (why) {
        show_message(p, why);
        player_say(p, "impossible_1");
        return 1;
    }
    /* Al lado del jugador (QUESTS_GetFreePosition, a 3), en el campamento */
    float x = p->x + 2.0f, y = p->y + 1.0f;
    find_free_spot(&x, &y);
    if (world_level_at(x, y) != p->level) {
        x = p->x;
        y = p->y;
    }
    if (p->cube[tsc].amount > 1)
        p->cube[tsc].amount--;
    else
        cube_remove(p, tsc);
    for (int k = 0; k < p->cube_count; k++)
        if (strcmp(p->cube[k].type->code, "leg") == 0) {
            cube_remove(p, k);
            break;
        }
    s_cow_open = 1;
    s_cow_x = x;
    s_cow_y = y;
    cow_link();
    audio_play_name("cursor_convert_item", AUDIO_NOPOS, AUDIO_NOPOS);
    audio_play_name("object_xportalopen", x, y);
    char msg[128]; /* (el nombre del juego: "El Nivel Secreto de la Vaca") */
    snprintf(msg, sizeof msg, tr("Se abrió un portal a %s"), str_get("Moo Moo Farm"));
    show_message(p, msg);
    save_character(p);
    return 1;
}

/* Cada tick: el portal en el mundo del Acto 1 y la muerte del Rey Vaca (se
   busca solo con alguien en la granja) */
static void cow_tick(void)
{
    if (s_act != 1) {
        s_cow_linked = 0;
        return;
    }
    if (s_cow_open && !s_cow_linked)
        cow_link();
    int inside = 0, id;
    for (int i = 0; i < s_num_players; i++)
        inside |= s_pl[i].level && s_pl[i].level->id == COW_LEVEL;
    if (s_cow_king_dead || !inside || units_find_all("The Cow King", &id, 1) != 1 || !units_is_corpse(id))
        return;
    s_cow_king_dead = 1;
    for (int i = 0; i < s_num_players; i++)
        if (s_pl[i].level && s_pl[i].level->id == COW_LEVEL)
            s_pl[i].cow_king = 1;
    float x, y;
    units_position(id, 1.0f, &x, &y);
    for (int k = 0; k < 8; k++) /* (dwDropItemCode 'vps': ocho veces) */
        drop_at("vps", 0, x - 1.5f + 0.5f * (float)(k % 4) * 2.0f, y - 0.7f + 1.4f * (float)(k / 4));
    save_party();
}
