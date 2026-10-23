/* ---- Las habilidades del Bárbaro y la Amazona (se incluye desde game.c) ----

   Lo que hace cada una sale de skills.txt: el daño, la puntería, las
   duraciones y los alcances con sus fórmulas y sinergias (skill_calc), y las
   animaciones de las que tienen secuencia (Estocada, Empalar, Rechazo,
   Ametrallar, Torbellino, Doble golpe, Frenesí, Salto, Salto de ataque y
   Doble lanzamiento) con las tablas del original (sequences.h): cada paso
   muestra un cuadro de un modo y algunos marcan el golpe. */

#define WARD_MAX       4
#define LEAP_ATK_RANGE 12.0f /* Salto de ataque: hasta qué distancia busca al monstruo */
#define WHIRL_DIST     8.0f  /* Torbellino: cuánto avanza si no hay a quién ir */
#define CHAIN_RANGE    8.0f  /* Golpe de rayo y Furia de rayo: hasta dónde saltan los rayos */

/* Guardia siniestra: el tótem del cadáver */
typedef struct {
    float x, y;
    int   ticks, age, radius, scare;
} Ward;
static Ward s_wards[WARD_MAX];

static void skillfx_clear(void)
{
    memset(s_wards, 0, sizeof s_wards);
}

static void melee_strike(Player *p, int target);

/* El modo de la animación de cada paso de la secuencia */
static int seq_mode(int m)
{
    switch (m) {
    case SEQ_A2: return MODE_A2;
    case SEQ_SC: return MODE_SC;
    case SEQ_TH: return MODE_TH;
    case SEQ_S1: return MODE_S1;
    case SEQ_S3: return MODE_S3;
    case SEQ_KK: return MODE_KK;
    case SEQ_S4: return MODE_S4;
    default:     return MODE_A1;
    }
}

/* Lo que suma Frenesí mientras dura (0 velocidad al moverse, 1 de ataque):
   cada acierto suma un cuarto, hasta lo que dice el nivel */
static int frenzy_bonus(const Player *p, int which)
{
    if (p->frenzy_ticks <= 0 || p->frenzy_hits <= 0)
        return 0;
    int i = skills_find(p->cls, "Frenzy");
    if (i < 0)
        return 0;
    SkillCtx c = pctx(p);
    const SkillDef *s = skills_get(p->cls, i);
    int v = skill_calc(s, which ? SC_AURASTAT2 : SC_AURASTAT1, eff_lvl(p, i), &c);
    return v * (p->frenzy_hits < 4 ? p->frenzy_hits : 4) / 4;
}

static int attack_ias(const Player *p)
{
    return p->bonus[STAT_IAS] + frenzy_bonus(p, 1) + p->au_ias + p->quick_ias + p->dru_ias + /* y Fanatismo, Rapidez y el Druida */
           war_ias(p); /* y Tajo y Hojas reflejadas del Brujo */
}

/* ---- Secuencias ---- */

static int start_seq(Player *p, int skill)
{
    const SkillDef *s = skills_get(p->cls, skill);
    CharLook look;
    char wc[4];
    build_look(p, &look, wc, sizeof wc);
    int n;
    const SeqFrame *seq = seq_get(s->seqnum, wc, &n);
    if (!seq || n <= 0)
        return 0;
    set_player_state(p, PS_ATTACK, seq_mode(seq[0].mode));
    p->attack_skill = skill;
    p->attack_kind = ATK_MELEE;
    p->seq = seq;
    p->seq_n = n;
    p->seq_pos = p->seq_hits = 0;
    p->hand = 0;
    p->hits_total = p->hits_done = 0;
    return 1;
}

/* Acribillar no tiene secuencia (skills.txt no le da seqnum): usa la
   animación de ataque y, como SKILLS_SrvDo012_Strafe del original, en el
   cuadro del golpe dispara al próximo monstruo y rebobina la animación
   Param6 por ciento (el 50%) mientras le queden flechas */
static int start_strafe(Player *p, int skill)
{
    if (!player_anim(p, MODE_A1)->ok)
        return 0;
    set_player_state(p, PS_ATTACK, MODE_A1);
    p->attack_skill = skill;
    p->attack_kind = ATK_SHOOT;
    p->seq = NULL;
    p->seq_pos = p->seq_hits = 0;
    p->hand = 0;
    p->hits_total = 1;
    p->hits_done = 0;
    return 1;
}

static int seq_again(Player *p, const SkillDef *s);
static void strafe_shot(Player *p, const SkillDef *s, int lvl);

static int strafing(const Player *p)
{
    return p->state == PS_ATTACK && !p->seq && p->attack_skill >= 0 &&
           skills_get(p->cls, p->attack_skill)->kind == SK_STRAFE;
}

static void strafe_tick(Player *p, const CharAnim *a)
{
    const SkillDef *s = skills_get(p->cls, p->attack_skill);
    int speed = a->speed * (100 + attack_ias(p)) / 100;
    p->anim_acc += speed;
    int hf = a->hit_frame > 0 ? a->hit_frame : charanim_frames(a) / 2;
    if (p->hits_done || (p->anim_acc >> 8) < hf)
        return;
    p->hits_done = 1;
    strafe_shot(p, s, eff_lvl(p, p->attack_skill));
    p->seq_hits++;
    if (seq_again(p, s)) {
        p->anim_acc = p->anim_acc * (100 - s->param[6]) / 100;
        p->hits_done = 0;
    }
}

/* Para dibujar: el modo y el cuadro de la secuencia en curso */
static int seq_frame(const Player *p, int *mode, int *frame)
{
    if (!p->seq || p->state != PS_ATTACK)
        return 0;
    int i = p->anim_acc >> 8;
    if (i >= p->seq_n)
        i = p->seq_n - 1;
    *mode = seq_mode(p->seq[i].mode);
    *frame = p->seq[i].frame;
    if (!player_anim(p, *mode)->ok)
        *mode = MODE_A1;
    return 1;
}

/* Otro monstruo pegado (el segundo golpe de Doble golpe y Frenesí); si no
   hay, el mismo */
static int second_target(const Player *p)
{
    int ids[16], n = units_enemies_near(p->x, p->y, MELEE_RANGE + 0.5f, ids, 16);
    for (int k = 0; k < n; k++)
        if (ids[k] != p->target)
            return ids[k];
    return p->target;
}

/* ---- Salto y Salto de ataque ---- */

/* ¿Se puede caer ahí? Libre para el cuerpo y sin una pared ni una puerta en
   la línea desde el jugador (SKILLS_FindLeapTargetPosition: el trazado con
   COLLIDE_MASK_PLAYER_FLYING); si no, el Bárbaro de la demostración saltaba
   la pared de una cueva y quedaba encerrado */
static int bad_landing(const Player *p, float x1, float y1)
{
    return blocked_body(x1, y1) ||
           map_ray(&s_map, (int)p->x, (int)p->y, (int)x1, (int)y1, COLL_MISSILE_BARRIER | COLL_DOOR, NULL, NULL);
}

/* Un lugar libre (con la cruz) yendo de (x1, y1) hacia el jugador */
static void free_landing(const Player *p, float *x1, float *y1)
{
    float dx = p->x - *x1, dy = p->y - *y1, l = sqrtf(dx * dx + dy * dy);
    for (float k = 0.0f; k < l && bad_landing(p, *x1, *y1); k += 0.5f) {
        *x1 = *x1 + dx / l * 0.5f;
        *y1 = *y1 + dy / l * 0.5f;
    }
    if (bad_landing(p, *x1, *y1)) {
        *x1 = p->x;
        *y1 = p->y;
    }
}

static void jump_start(Player *p)
{
    p->jump_x0 = p->x;
    p->jump_y0 = p->y;
    p->jumping = 1;
    p->jump_f0 = p->seq_pos - 1; /* este paso */
    p->jump_f1 = p->seq_n - 1;
    for (int k = p->seq_pos; k < p->seq_n; k++)
        if (p->seq[k].event == SEQ_EV_HIT) { /* cae en el próximo golpe de la secuencia */
            p->jump_f1 = k;
            break;
        }
    audio_play_name("barbarian_leap_1", p->x, p->y);
}

static void jump_move(Player *p)
{
    float t = ((float)p->anim_acc / 256.0f - (float)p->jump_f0) / (float)(p->jump_f1 - p->jump_f0 > 0 ? p->jump_f1 - p->jump_f0 : 1);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    p->x = p->jump_x0 + (p->jump_x1 - p->jump_x0) * t;
    p->y = p->jump_y0 + (p->jump_y1 - p->jump_y0) * t;
}

static void jump_land(Player *p, const SkillDef *s, int lvl)
{
    p->x = p->jump_x1;
    p->y = p->jump_y1;
    p->jumping = 0;
    if (s->kind != SK_LEAP)
        return;
    /* Salto: empuja a los de alrededor (leapknockback) */
    SkillCtx c = pctx(p);
    float r = (float)skill_calc(s, SC_CALC1, lvl, &c) / 2.0f;
    int ids[32], n = units_enemies_near(p->x, p->y, r < 2.0f ? 2.0f : r, ids, 32);
    for (int k = 0; k < n; k++)
        units_knockback(ids[k], p->x, p->y, 2.5f);
    audio_play_name("barbarian_leap_land", p->x, p->y);
}

/* ---- Torbellino ---- */

static void whirl_move(Player *p)
{
    const CharClass *c = &s_classes[p->cls];
    float dx = p->whirl_x - p->x, dy = p->whirl_y - p->y, d = sqrtf(dx * dx + dy * dy);
    float step = c->run_speed * RUN_BONUS / TICK_RATE;
    p->sector = (p->sector + 1) % 16; /* va girando */
    if (d <= step) {
        p->whirl_x = p->x;
        p->whirl_y = p->y;
        return;
    }
    float nx = p->x + dx / d * step, ny = p->y + dy / d * step;
    if (blocked_body(nx, ny)) { /* una pared: hasta ahí llega */
        p->whirl_x = p->x;
        p->whirl_y = p->y;
        return;
    }
    p->x = nx; /* entre los monstruos: el Torbellino los atraviesa */
    p->y = ny;
}

/* Un golpe a cada uno de los que tiene al lado (una vez por vuelta) */
static void whirl_strike(Player *p)
{
    int ids[24], n = units_enemies_near(p->x, p->y, MELEE_RANGE + 0.3f, ids, 24);
    for (int k = 0; k < n; k++) {
        int done = 0;
        for (int j = 0; j < p->whirl_nhit && !done; j++)
            done = p->whirl_hit[j] == ids[k];
        if (done)
            continue;
        if (p->whirl_nhit < 24)
            p->whirl_hit[p->whirl_nhit++] = ids[k];
        melee_strike(p, ids[k]);
    }
}

/* ---- Lanzar (Doble lanzamiento) y disparar (Ametrallar) ---- */

/* Gasta uno de esa pila; si se termina pone otra igual de la mochila */
static void use_up(Player *p, Item *it)
{
    if (!it->type || --it->amount > 0)
        return;
    const ItemType *t = it->type;
    memset(it, 0, sizeof *it);
    for (int i = 0; i < p->bag_count; i++)
        if (p->bag[i].type == t) {
            *it = p->bag[i];
            bag_remove(p, i);
            show_message(p, "Sacaste más de la mochila");
            break;
        }
    refresh_look(p);
}

/* Hacia dónde apunta: el monstruo o, si no hay, hacia donde mira */
static void aim_at(const Player *p, int target, float *ax, float *ay)
{
    if (units_alive_enemy(target)) {
        float ux, uy;
        units_position(target, 1.0f, &ux, &uy);
        *ax = ux - p->x;
        *ay = uy - p->y;
    } else {
        screen_dir_to_world(p->face_x, p->face_y, ax, ay);
    }
}

static Missile *player_missile(Player *p, const char *name, float ax, float ay, int dmg_pct, int ar_pct, int thrown)
{
    Missile *mi = missiles_spawn(name, p->x, p->y, ax, ay);
    if (!mi)
        return NULL;
    int dmin, dmax;
    weapon_damage_pct(p, thrown, dmg_pct + (thrown ? p->sk_throw_dmg : 0), &dmin, &dmax);
    mi->owner = (int)(p - s_pl);
    mi->ar = player_attack_rating(p) * (100 + ar_pct + (thrown ? p->sk_throw_ar : 0)) / 100;
    mi->level = p->level_num;
    mi->dmin = dmin;
    mi->dmax = dmax;
    return mi;
}

/* Doble lanzamiento: el arma de la mano del golpe (p->hand) */
static void throw_hand(Player *p, const SkillDef *s, int lvl)
{
    Item *it = p->hand && dual_wield(p) ? &p->equip[SLOT_SHIELD] : &p->equip[SLOT_WEAPON];
    if (!it->type || !items_can_throw(it->type) || it->amount <= 0)
        return;
    SkillCtx c = pctx(p);
    float ax, ay;
    aim_at(p, p->target, &ax, &ay);
    const char *name = items_type_is(it->type, "taxe") ? "throwaxe" : items_type_is(it->type, "tkni") ? "throwknife"
                                                                                                     : "javelin";
    player_missile(p, name, ax, ay, skill_calc(s, SC_CALC1, lvl, &c), skill_tohit(s, lvl), 1);
    use_up(p, it);
}

/* Ametrallar: una flecha al monstruo que le toca */
static void strafe_shot(Player *p, const SkillDef *s, int lvl)
{
    Item *ammo = ammo_item(p, ATK_SHOOT);
    if (!ammo || ammo->amount <= 0)
        return;
    SkillCtx c = pctx(p);
    int t = p->target_k < p->num_targets ? p->targets[p->target_k] : -1;
    if (!units_alive_enemy(t))
        t = units_find_enemy(p->x, p->y, AIM_RANGE);
    float ax, ay;
    aim_at(p, t, &ax, &ay);
    const char *name = items_ammo_for(p->equip[SLOT_WEAPON].type) == 2 ? s->missile_b : s->missile;
    player_missile(p, name[0] ? name : "arrow", ax, ay, skill_calc(s, SC_CALC2, lvl, &c), skill_tohit(s, lvl), 0);
    use_up(p, ammo);
}

/* ---- La secuencia en curso ---- */

static void seq_hit(Player *p, const SkillDef *s, int lvl)
{
    switch (s->kind) {
    case SK_JAB:
    case SK_IMPALE:
    case SK_SACRIFICE:
        melee_strike(p, p->target);
        break;
    case SK_FEND:
        if (p->target_k < p->num_targets)
            melee_strike(p, p->targets[p->target_k]);
        break;
    case SK_DOUBLE_SWING:
    case SK_FRENZY:
        if (p->seq_hits == 0) {
            melee_strike(p, p->target);
        } else {
            int t2 = second_target(p);
            if (t2 >= 0)
                face_unit(p, t2);
            p->hand = 1;
            melee_strike(p, t2);
            p->hand = 0;
        }
        break;
    case SK_LEAP:
    case SK_LEAP_ATTACK:
        if (p->seq_hits == 0)
            jump_start(p);
        else if (p->seq_hits == 1)
            jump_land(p, s, lvl);
        else if (s->kind == SK_LEAP_ATTACK)
            melee_strike(p, p->target);
        break;
    case SK_WHIRLWIND:
        whirl_strike(p);
        break;
    case SK_DOUBLE_THROW:
        p->hand = p->seq_hits % 2;
        throw_hand(p, s, lvl);
        p->hand = 0;
        break;
    case SK_STRAFE:
        strafe_shot(p, s, lvl);
        break;
    default: /* Infierno, Relámpago y Rayo en cadena; las de la Asesina */
        if (sorc_kind(s->kind))
            sorc_effect(p, s, lvl);
        else if (ass_kind(s->kind))
            ass_seq_hit(p, s, lvl);
        else if (dru_kind(s->kind))
            dru_seq_hit(p, s, lvl);
        break;
    }
    p->seq_hits++;
}

/* Al terminar la secuencia: Ametrallar y Rechazo siguen con el próximo
   monstruo, el Torbellino sigue girando hasta llegar */
static int seq_again(Player *p, const SkillDef *s)
{
    if (s->kind == SK_WHIRLWIND) {
        float dx = p->whirl_x - p->x, dy = p->whirl_y - p->y;
        if (dx * dx + dy * dy < 0.1f || ++p->seq_loops > 12)
            return 0;
        p->whirl_nhit = 0;
        return 1;
    }
    if (s->kind == SK_FEND) {
        while (++p->target_k < p->num_targets)
            if (units_alive_enemy(p->targets[p->target_k])) {
                face_unit(p, p->targets[p->target_k]);
                return 1;
            }
        return 0;
    }
    if (s->kind == SK_STRAFE) { /* una flecha por monstruo, dando vueltas, hasta calc1 flechas */
        if (p->seq_hits >= p->seq_loops || p->num_targets <= 0)
            return 0;
        for (int k = 0; k < p->num_targets; k++) {
            p->target_k = (p->target_k + 1) % p->num_targets;
            if (units_alive_enemy(p->targets[p->target_k])) {
                face_unit(p, p->targets[p->target_k]);
                return 1;
            }
        }
        return 0;
    }
    if (ass_kind(s->kind))
        return ass_seq_again(p, s);
    return 0;
}

static void seq_tick(Player *p)
{
    const SkillDef *s = skills_get(p->cls, p->attack_skill);
    int lvl = eff_lvl(p, p->attack_skill);
    const CharAnim *a1 = player_anim(p, MODE_A1);
    int speed = a1->speed * (100 + attack_ias(p)) / 100;
    p->anim_acc += speed > 32 ? speed : 32;
    int upto = p->anim_acc >> 8;
    while (p->seq_pos <= upto && p->seq_pos < p->seq_n) {
        int ev = p->seq[p->seq_pos].event;
        p->seq_pos++;
        if (ev == SEQ_EV_HIT) {
            seq_hit(p, s, lvl);
            if (p->state != PS_ATTACK || !p->seq)
                return;
        }
    }
    if (p->seq && p->state == PS_ATTACK) {
        p->mode = seq_mode(p->seq[upto < p->seq_n ? upto : p->seq_n - 1].mode);
        if (s->kind == SK_WHIRLWIND) {
            whirl_move(p);
            overlays_add("whirlwind", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, 2); /* estado whirlwind */
        }
        if (p->jumping)
            jump_move(p);
    }
    if (upto >= p->seq_n) {
        if (seq_again(p, s)) {
            p->anim_acc = 0;
            p->seq_pos = 0;
            return;
        }
        if (p->jumping) { /* por las dudas: que no quede en el aire */
            p->x = p->jump_x1;
            p->y = p->jump_y1;
        }
        set_player_state(p, PS_NORMAL, MODE_NU);
    }
}

/* ---- El golpe cuerpo a cuerpo, con lo que agrega cada habilidad ---- */

/* Rayos que saltan de un monstruo a otro (Golpe de rayo) */
static void chain_lightning(Player *p, int first, int hits, int lo, int hi)
{
    int done[16], n = 0, cur = first;
    while (cur >= 0 && n < hits && n < 16) {
        done[n++] = cur;
        float x, y;
        units_position(cur, 1.0f, &x, &y);
        missiles_effect("lightninghit", x, y);
        overlays_add("lightning", OV_AT_UNIT, cur, x, y, 0); /* ProgOverlay de lightningstrike */
        int exp = units_damage(cur, s_one_hit ? 1 << 20 : units_resist(cur, ELEM_LTNG, roll(lo, hi)));
        if (exp > 0)
            award_kill(p, cur, exp);
        int ids[24], m = units_enemies_near(x, y, CHAIN_RANGE, ids, 24), next = -1;
        float best = 1e9f;
        for (int k = 0; k < m; k++) {
            int seen = 0;
            for (int j = 0; j < n && !seen; j++)
                seen = done[j] == ids[k];
            float ux, uy;
            units_position(ids[k], 1.0f, &ux, &uy);
            float d = (ux - x) * (ux - x) + (uy - y) * (uy - y);
            if (!seen && d < best) {
                best = d;
                next = ids[k];
            }
        }
        cur = next;
    }
    audio_play_name("sorceress_lightning_1", p->x, p->y);
}

static void melee_strike(Player *p, int target)
{
    if (!units_alive_enemy(target))
        return;
    float ux, uy;
    units_position(target, 1.0f, &ux, &uy);
    float dx = ux - p->x, dy = uy - p->y;
    float reach = MELEE_RANGE + 0.5f + units_radius(target) - 0.45f; /* al borde: los grandes desde más lejos */
    if (dx * dx + dy * dy > reach * reach)
        return;

    const SkillDef *s = p->attack_skill >= 0 ? skills_get(p->cls, p->attack_skill) : NULL;
    int lvl = s ? eff_lvl(p, p->attack_skill) : 0;
    SkillCtx c = pctx(p);
    int ar_pct = 0, dmg_pct = 0, dmg_flat = 0, elo = 0, ehi = 0, eelem = ELEM_LTNG;
    if (s) {
        ar_pct = skill_tohit(s, lvl);
        switch (s->kind) {
        case SK_BASH:         dmg_pct = skill_calc(s, SC_CALC1, lvl, &c); dmg_flat = skill_calc(s, SC_CALC2, lvl, &c); break;
        case SK_STUN:         ar_pct += skill_ln(s, 1, 2, lvl); dmg_pct = skill_calc(s, SC_CALC1, lvl, &c); break;
        case SK_FEND:         dmg_pct = skill_calc(s, SC_CALC2, lvl, &c); break;
        case SK_CONCENTRATE:
        case SK_JAB:
        case SK_IMPALE:
        case SK_DOUBLE_SWING:
        case SK_FRENZY:
        case SK_WHIRLWIND:
        case SK_BERSERK:      dmg_pct = skill_calc(s, SC_CALC1, lvl, &c); break;
        case SK_LEAP_ATTACK: {
            int lo, hi;
            dmg_pct = skill_calc(s, SC_CALC1, lvl, &c);
            skill_phys_syn(s, lvl, &c, &lo, &hi);
            dmg_flat = roll(lo, hi);
            break;
        }
        case SK_POWER_STRIKE: skill_elem_syn(s, lvl, &c, &elo, &ehi); break;
        case SK_SACRIFICE:
        case SK_CHARGE:       dmg_pct = skill_calc(s, SC_CALC1, lvl, &c); break;
        case SK_ZEAL:         dmg_pct = skill_calc(s, SC_CALC2, lvl, &c); break;
        case SK_FIRE_CLAWS:   skill_elem_syn(s, lvl, &c, &elo, &ehi); eelem = ELEM_FIRE; break;
        default:
            if (ass_kind(s->kind)) /* los golpes finales (y lo del tigre) */
                dmg_pct = ass_strike_pct(p, s, lvl);
            else if (dru_kind(s->kind))
                dmg_pct = dru_strike_pct(p, s, lvl);
            else if (war_kind(s->kind))
                dmg_pct = war_strike_pct(p, s, lvl);
            break;
        }
    }
    if (units_is_undead(target)) { /* Santuario: contra los no muertos */
        ar_pct += p->au_undead_ar;
        dmg_pct += p->au_undead_dmg;
    }
    int ar_flat = 0; /* los objetos: contra los no muertos y los demonios */
    if (units_is_undead(target)) {
        dmg_pct += p->bonus[STAT_DMG_UNDEAD];
        ar_flat += p->bonus[STAT_AR_UNDEAD];
    }
    if (units_is_demon(target)) {
        dmg_pct += p->bonus[STAT_DMG_DEMON];
        ar_flat += p->bonus[STAT_AR_DEMON];
    }

    /* Probabilidad de pegar del original:
       2 * AR / (AR + defensa del monstruo) * nivel / (nivel + nivel del monstruo) */
    int ar = (player_attack_rating(p) + ar_flat) * (100 + ar_pct) / 100, ac = units_defense(target), mlvl = units_level(target);
    if (p->bonus[STAT_IGNORE_AC] > 0) /* "ignora la defensa del objetivo" */
        ac = 0;
    else if (p->bonus[STAT_TARGET_AC]) { /* "-N% a la defensa del objetivo" */
        int cut = p->bonus[STAT_TARGET_AC] < 0 ? -p->bonus[STAT_TARGET_AC] : p->bonus[STAT_TARGET_AC];
        ac = ac * (100 - (cut > 100 ? 100 : cut)) / 100;
    }
    int chance = (int)(200LL * ar / (ar + ac > 0 ? ar + ac : 1) * p->level_num / (p->level_num + mlvl));
    chance = chance < 5 ? 5 : chance > 95 ? 95 : chance;
    if (!(s && s->kind == SK_SMITE) && (int)(rnd() % 100) >= chance) /* Castigar no falla */
        return;

    int dmin, dmax, kick = s && ass_kick(s->kind);
    if (s && s->kind == SK_SMITE)
        smite_damage(p, dmg_pct, &dmin, &dmax);
    else if (kick) /* las patadas: el daño de las botas */
        kick_damage(p, dmg_pct, &dmin, &dmax);
    else
        weapon_damage_pct(p, 0, dmg_pct, &dmin, &dmax);
    int dmg = dmin + (int)(rnd() % (uint32_t)(dmax - dmin + 1)) + dmg_flat;
    if ((int)(rnd() % 100) < crit_chance(p))
        dmg *= 2; /* golpe crítico (dominios, Golpe crítico) o mortal (objetos): el daño físico se duplica */
    if (dmg < 1)
        dmg = 1;
    int phys = dmg;
    /* Cada parte con su resistencia (la física también: ResDm) */
    dmg = units_resist(target, ELEM_NONE, phys) + (kick ? 0 : elemental_damage(p, target));
    if (ehi > 0) /* Golpe poderoso: rayo; Garras de fuego: fuego */
        dmg += units_resist(target, eelem, elo + (int)(rnd() % (uint32_t)(ehi - elo + 1)));
    if (s && s->kind == SK_CONVERSION && getenv("D2_CONVERT_ALWAYS"))
        dmg = 1; /* para probar la Conversión: que no lo mate */
    if (s && s->kind == SK_VENGEANCE) /* fuego, frío y rayo */
        dmg += vengeance_damage(p, s, lvl, target, ux, uy);

    audio_play_name(k_impact_sounds[weapon_hit_class(p)], ux, uy);
    if (s_one_hit)
        dmg = 1 << 20;
    int lifetap = units_lifetap(target); /* Robar vida: cura una parte de lo que le pega */
    int exp = units_damage(target, dmg);
    ass_venom(p, target, exp > 0); /* Veneno */
    if (p->cls == 6)
        dru_hit_effects(p, target, phys, exp > 0); /* Furia salvaje roba vida, Vapulear aturde */
    if (p->cls == 7)
        war_hit(p, target, exp > 0); /* el maleficio del arma del Brujo */
    if (lifetap > 0)
        p->hp += (s_one_hit ? phys : dmg) * lifetap / 100;
    if (p->curse == A4C_IRONMAIDEN) /* la Doncella de hierro de un Caballero del Olvido: le vuelve 200 % + 25 por nivel */
        take_damage(p, phys * (200 + 25 * (p->curse_lvl - 1)) / 100, 0);
    s_shown_target = target;
    s_shown_ticks = 3 * TICK_RATE;
    /* Robo de vida y de maná: un porcentaje del daño hecho */
    p->hp += phys * p->bonus[STAT_LIFESTEAL] / 100;
    p->mana += phys * p->bonus[STAT_MANASTEAL] / 100;
    if (exp > 0) {
        p->mana += p->bonus[STAT_MANA_KILL];
        p->hp += p->bonus[STAT_HEAL_KILL];
        share_exp(p, exp);
        drop_loot(target, p);
    } else if (!kick) {
        item_hit_effects(p, target, 0, 0); /* golpe aplastante, heridas abiertas... */
    }
    /* El efecto de la habilidad sobre el que golpeó (srvoverlay de skills.txt,
       como SKILLS_SrvDo002 del original): el destello de Concentración y el
       de Salto ofensivo. Porrazo y Golpe fulminante no tienen. */
    if (s && s->srvoverlay[0])
        overlays_add(s->srvoverlay, OV_AT_UNIT, target, ux, uy, 0);
    if (s) {
        switch (s->kind) {
        case SK_BASH:
            if (!exp)
                units_knockback(target, p->x, p->y, 1.2f);
            break;
        case SK_STUN:
            if (!exp) {
                int t = skill_elen_syn(s, lvl, &c);
                units_stun(target, t);
                overlays_add("stun", OV_AT_UNIT, target, ux, uy, t); /* las estrellas mientras dura */
            }
            break;
        case SK_IMPALE:
            if (!exp) /* lo frena (velocitypercent y attackrate -dm78) */
                units_slow(target, -skill_calc(s, SC_AURASTAT1, lvl, &c), skill_calc(s, SC_AURALEN, lvl, &c));
            break;
        case SK_CHARGED_STRIKE: {
            /* Rayos cargados que salen del monstruo zigzagueando */
            int n = skill_calc(s, SC_CALC1, lvl, &c), lo, hi;
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            for (int k = 0; k < n && k < 12; k++) {
                float a = (float)(rnd() % 628) / 100.0f;
                Missile *mi = missiles_spawn(s->missile, ux, uy, cosf(a), sinf(a));
                if (!mi)
                    break;
                mi->owner = (int)(p - s_pl);
                mi->ar = ar;
                mi->level = p->level_num;
                mi->elem = ELEM_LTNG;
                mi->emin = lo;
                mi->emax = hi;
                mi->flags |= MF_CHARGED | MF_ALWAYS_HIT;
                mi->life = 40;
            }
            break;
        }
        case SK_LIGHTNING_STRIKE: {
            int lo, hi;
            skill_elem_syn(s, lvl, &c, &lo, &hi);
            chain_lightning(p, exp ? -1 : target, skill_calc(s, SC_CALC2, lvl, &c), lo, hi);
            if (exp) { /* ya murió: los rayos saltan desde ahí */
                int ids[2];
                if (units_enemies_near(ux, uy, CHAIN_RANGE, ids, 1) > 0)
                    chain_lightning(p, ids[0], skill_calc(s, SC_CALC2, lvl, &c) - 1, lo, hi);
            }
            break;
        }
        case SK_FRENZY: /* cada acierto lo acelera un poco más */
            if (p->frenzy_hits < 4)
                p->frenzy_hits++;
            p->frenzy_ticks = skill_calc(s, SC_AURALEN, lvl, &c);
            overlays_add("frenzy", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, p->frenzy_ticks); /* estado frenzy */
            break;
        case SK_BERSERK: /* la defensa queda en 0 un rato */
            p->berserk_ticks = skill_calc(s, SC_CALC2, lvl, &c);
            overlays_add("berserkfront", OV_AT_PLAYER, (int)(p - s_pl), p->x, p->y, p->berserk_ticks); /* estado berserk */
            break;
        case SK_LEAP_ATTACK:
            missiles_effect(s->missile, p->x, p->y);
            overlays_add("doubledamage1", OV_AT_UNIT, target, ux, uy, 0); /* ProgOverlay de leapattack */
            break;
        default:
            if (pal_kind(s->kind))
                pal_after_hit(p, s, lvl, target, ux, uy, phys, exp > 0);
            else if (nec_kind(s->kind))
                nec_after_hit(p, s, lvl, target, exp > 0);
            else if (ass_kind(s->kind))
                ass_after_hit(p, s, lvl, target, phys, exp > 0);
            else if (dru_kind(s->kind))
                dru_after_hit(p, s, lvl, target, phys, exp > 0);
            else if (war_kind(s->kind))
                war_after_hit(p, s, lvl, target, phys, exp > 0);
            break;
        }
    }
    if (p->hp > p->hp_max) p->hp = p->hp_max;
    if (p->mana > p->mana_max) p->mana = p->mana_max;
}

/* ---- Arrancar las habilidades ---- */

/* Los monstruos a menos de `range`, del más cercano al más lejano */
static int nearest_enemies(const Player *p, float range, int *out, int max)
{
    int n = units_enemies_near(p->x, p->y, range, out, max);
    float d[32];
    for (int k = 0; k < n && k < 32; k++) {
        float x, y;
        units_position(out[k], 1.0f, &x, &y);
        d[k] = (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y);
    }
    for (int i = 1; i < n && i < 32; i++)
        for (int j = i; j > 0 && d[j] < d[j - 1]; j--) {
            float td = d[j];
            d[j] = d[j - 1];
            d[j - 1] = td;
            int t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

/* Las que tienen secuencia y se usan cuerpo a cuerpo o moviéndose. 1 si
   arrancó; si no, deja el aviso */
static int start_seq_skill(Player *p, int skill)
{
    const SkillDef *s = skills_get(p->cls, skill);
    int lvl = eff_lvl(p, skill);
    SkillCtx c = pctx(p);
    p->target = units_find_enemy(p->x, p->y, MELEE_RANGE);
    switch (s->kind) {
    case SK_DOUBLE_SWING:
    case SK_FRENZY:
        if (!dual_wield(p)) {
            show_message(p, "Necesitás dos armas");
            return 0;
        }
        break;
    case SK_DOUBLE_THROW:
        if (!dual_wield(p) || !items_can_throw(p->equip[SLOT_SHIELD].type) ||
            !items_can_throw(p->equip[SLOT_WEAPON].type)) {
            show_message(p, "Necesitás un arma para lanzar en cada mano");
            return 0;
        }
        p->target = units_find_enemy(p->x, p->y, AIM_RANGE);
        break;
    case SK_FEND: {
        p->num_targets = nearest_enemies(p, MELEE_RANGE + 0.5f, p->targets, 16);
        int most = skill_calc(s, SC_CALC1, lvl, &c);
        if (p->num_targets > most)
            p->num_targets = most;
        if (p->num_targets <= 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        p->target = p->targets[0];
        p->target_k = 0;
        break;
    }
    case SK_STRAFE: {
        Item *ammo = ammo_item(p, ATK_SHOOT);
        if (!items_ammo_for(p->equip[SLOT_WEAPON].type) || !ammo || ammo->amount <= 0) {
            show_message(p, "Necesitás un arco con flechas");
            return 0;
        }
        p->num_targets = nearest_enemies(p, (float)skill_calc(s, SC_AURARANGE, lvl, &c) / 2.0f, p->targets, 16);
        p->target = p->num_targets > 0 ? p->targets[0] : -1;
        p->target_k = 0;
        break;
    }
    case SK_LEAP: {
        float ax, ay, range = (float)skill_calc(s, SC_AURARANGE, lvl, &c) / 2.0f;
        screen_dir_to_world(p->face_x, p->face_y, &ax, &ay);
        float l = sqrtf(ax * ax + ay * ay);
        p->jump_x1 = p->x + ax / (l > 0.0f ? l : 1.0f) * range;
        p->jump_y1 = p->y + ay / (l > 0.0f ? l : 1.0f) * range;
        free_landing(p, &p->jump_x1, &p->jump_y1);
        break;
    }
    case SK_LEAP_ATTACK: {
        p->target = units_find_enemy(p->x, p->y, LEAP_ATK_RANGE);
        if (p->target < 0) {
            show_message(p, "No hay a quién");
            return 0;
        }
        float ux, uy;
        units_position(p->target, 1.0f, &ux, &uy);
        float dx = p->x - ux, dy = p->y - uy, l = sqrtf(dx * dx + dy * dy);
        float r = units_radius(p->target) + 1.2f; /* cae pegado al monstruo */
        p->jump_x1 = l > r ? ux + dx / l * r : p->x;
        p->jump_y1 = l > r ? uy + dy / l * r : p->y;
        free_landing(p, &p->jump_x1, &p->jump_y1);
        break;
    }
    case SK_WHIRLWIND: {
        int t = units_find_enemy(p->x, p->y, AIM_RANGE);
        float ax, ay;
        aim_at(p, t, &ax, &ay);
        float l = sqrtf(ax * ax + ay * ay);
        float dist = t >= 0 ? l + 3.0f : WHIRL_DIST; /* atraviesa al monstruo */
        p->whirl_x = p->x + ax / (l > 0.0f ? l : 1.0f) * dist;
        p->whirl_y = p->y + ay / (l > 0.0f ? l : 1.0f) * dist;
        p->whirl_nhit = 0;
        break;
    }
    default:
        break;
    }
    if (p->target >= 0)
        face_unit(p, p->target);
    if (s->kind == SK_STRAFE ? !start_strafe(p, skill) : !start_seq(p, skill)) {
        show_message(p, "No se puede con esta arma");
        return 0;
    }
    p->seq_loops = 0;
    if (s->kind == SK_STRAFE) {
        /* Cuántas flechas, como SKILLS_SrvSt08_Strafe: tantas como monstruos
           haya, pero al menos calc3 y como mucho calc1 */
        int most = skill_calc(s, SC_CALC1, lvl, &c), least = skill_calc(s, SC_CALC3, lvl, &c);
        if (least > most)
            least = most;
        int n = p->num_targets > least ? p->num_targets : least;
        p->seq_loops = n < most ? n : most;
    }
    play_skill_sound(p, s);
    return 1;
}

/* ---- Lo que hacen los gritos, los tótems y las invocaciones ---- */

static void party_buff(Player *caster, int what, int value, int ticks, const char *cast_overlay, const char *overlay)
{
    for (int i = 0; i < s_num_players; i++) {
        Player *o = &s_pl[i];
        float dx = o->x - caster->x, dy = o->y - caster->y;
        if (o->state >= PS_DYING || dx * dx + dy * dy > SHOUT_RADIUS * SHOUT_RADIUS)
            continue;
        if (what == SK_BATTLE_ORDERS) {
            int old_hp = o->hp_max, old_mana = o->mana_max;
            o->bo_pct = value;
            o->bo_ticks = ticks;
            refresh_stats(o);
            /* lo que tenía crece en la misma proporción */
            o->hp = o->hp * o->hp_max / (old_hp > 0 ? old_hp : 1);
            o->mana = o->mana * o->mana_max / (old_mana > 0 ? old_mana : 1);
        } else if (what == SK_BATTLE_COMMAND) {
            o->bc_bonus = value;
            o->bc_ticks = ticks;
            refresh_stats(o);
        }
        /* Como el estado de states.txt: castoverlay al recibirlo y overlay1 mientras dura */
        if (cast_overlay)
            overlays_add(cast_overlay, OV_AT_PLAYER, i, o->x, o->y, 0);
        if (overlay)
            overlays_add(overlay, OV_AT_PLAYER, i, o->x, o->y, ticks);
    }
}

/* El efecto de un estado sobre los monstruos de alrededor que lo tienen
   (`flag` de units_status): su castoverlay una vez y su overlay1 mientras
   dura. Devuelve a cuántos marcó. */
static int mark_status(float x, float y, float radius, int flag, const char *overlay, const char *cast, int ticks)
{
    int ids[64], n = units_enemies_near(x, y, radius, ids, 64), marked = 0;
    for (int k = 0; k < n; k++) {
        if (!(units_status(ids[k]) & flag))
            continue;
        float ux, uy;
        units_position(ids[k], 1.0f, &ux, &uy);
        if (cast)
            overlays_add(cast, OV_AT_UNIT, ids[k], ux, uy, 0);
        overlays_add(overlay, OV_AT_UNIT, ids[k], ux, uy, ticks);
        marked++;
    }
    return marked;
}

/* Al quedar envenenado: el estado poison de states.txt (castoverlay
   poisonhit y onsound impact_poison_1); el verde lo pone units_draw */
static void poison_fx(int unit)
{
    float x, y;
    units_position(unit, 1.0f, &x, &y);
    overlays_add("poisonhit", OV_AT_UNIT, unit, x, y, 0);
    audio_play_name("impact_poison_1", x, y);
}

static void ward_spawn(float x, float y, int ticks, int radius, int scare)
{
    for (int i = 0; i < WARD_MAX; i++)
        if (s_wards[i].ticks <= 0) {
            s_wards[i] = (Ward){ x, y, ticks, 0, radius, scare };
            missiles_effect("grimwardmediumstart", x, y);
            return;
        }
}

static void wards_tick(void)
{
    for (int i = 0; i < WARD_MAX; i++) {
        Ward *w = &s_wards[i];
        if (w->ticks <= 0)
            continue;
        w->age++;
        if (w->age == 8) { /* la animación del tótem parado, hasta que termina */
            Missile *m = missiles_effect("grimwardmedium", w->x, w->y);
            if (m) {
                m->life = w->ticks;
                m->flags |= MF_CHAIN; /* sigue con grimwardmediumstop */
            }
        }
        if (w->age % 5 == 0) { /* los monstruos de alrededor huyen (estado terror) */
            units_scare(w->x, w->y, (float)w->radius, w->scare);
            mark_status(w->x, w->y, (float)w->radius, UNIT_SCARED, "curseterror", NULL, w->scare);
        }
        w->ticks--;
    }
}

/* El Señuelo: una copia de la Amazona quieta que los monstruos atacan */
static void decoy_spawn(Player *p, int hp, int ticks)
{
    float ax, ay;
    screen_dir_to_world(p->face_x, p->face_y, &ax, &ay);
    float l = sqrtf(ax * ax + ay * ay);
    float x = p->x + ax / (l > 0.0f ? l : 1.0f) * 3.0f, y = p->y + ay / (l > 0.0f ? l : 1.0f) * 3.0f;
    free_landing(p, &x, &y);
    p->decoy_x = x;
    p->decoy_y = y;
    p->decoy_hp = p->decoy_hp_max = hp;
    p->decoy_ticks = ticks;
    p->decoy_sector = p->sector;
    overlays_add("dopllezon_appear", OV_AT_POINT, 0, x, y, 0); /* sumoverlay de Dopplezon */
}

/* La Valquiria: un aliado que pelea; si ya había una, la nueva la reemplaza */
/* La Valquiria se dibuja como la Amazona con el equipo que le da
   monequip.txt según el nivel de la habilidad (lanza, lanza de guerra o pica
   de guerra; armadura de placas, del caos o sagrada; y la diadema) */
static void valkyrie_look(int lvl, CharLook *look, char *wclass, size_t wcap)
{
    static Player v; /* solo el equipo: el dibujo se arma como el de un jugador */
    memset(&v, 0, sizeof v);
    v.cls = 1;
    static const struct {
        const char *loc;
        int         slot;
    } k_locs[] = { { "rarm", SLOT_WEAPON }, { "tors", SLOT_BODY }, { "head", SLOT_HELM } };
    for (unsigned k = 0; k < sizeof k_locs / sizeof k_locs[0]; k++) {
        char code[8] = "";
        if (units_monequip("valkyrie", lvl, k_locs[k].loc, code, sizeof code))
            v.equip[k_locs[k].slot].type = items_find(code);
    }
    build_look(&v, look, wclass, wcap);
}

static void valkyrie_spawn(Player *p, const SkillDef *s, int lvl)
{
    if (p->valk_unit >= 0)
        units_pet_remove(p->valk_unit);
    SkillCtx c = pctx(p);
    float ax, ay;
    screen_dir_to_world(p->face_x, p->face_y, &ax, &ay);
    p->valk_unit = units_pet_spawn((int)(p - s_pl), s->summon, p->x + ax, p->y + ay, p->level_num,
                                   skill_calc(s, SC_CALC1, lvl, &c), skill_calc(s, SC_CALC2, lvl, &c),
                                   skill_calc(s, SC_AURASTAT1, lvl, &c));
    p->valk_dead = 0;
    if (p->valk_unit >= 0) {
        CharLook look;
        char wclass[4];
        valkyrie_look(lvl, &look, wclass, sizeof wclass);
        units_pet_set_look(p->valk_unit, &look, wclass);
        overlays_add("valkyriestart", OV_AT_UNIT, p->valk_unit, p->x + ax, p->y + ay, 0);
    }
}

/* Cada tick: lo que dura de las habilidades */
static void skills_tick(Player *p)
{
    if (p->bo_ticks > 0 && --p->bo_ticks == 0)
        refresh_stats(p);
    if (p->bc_ticks > 0 && --p->bc_ticks == 0) {
        p->bc_bonus = 0;
        refresh_stats(p);
    }
    if (p->frenzy_ticks > 0 && --p->frenzy_ticks == 0)
        p->frenzy_hits = 0;
    if (p->berserk_ticks > 0)
        p->berserk_ticks--;
    if (p->decoy_ticks > 0 && (--p->decoy_ticks == 0 || p->decoy_hp <= 0))
        p->decoy_ticks = 0;
    if (p->valk_unit >= 0 && !units_pet_alive(p->valk_unit) && ++p->valk_dead > 2 * TICK_RATE) {
        units_pet_remove(p->valk_unit);
        p->valk_unit = -1;
    }
    sorc_tick(p);
    pal_tick(p);
    nec_tick(p);
    ass_tick(p);
    dru_tick(p);
    war_tick(p);
}

/* ---- Proyectiles de las habilidades ---- */

/* Perforar: el proyectil sigue de largo */
static int pierce_roll(const Missile *mi)
{
    if (mi->owner < 0 || mi->owner >= s_num_players || (mi->flags & (MF_MERC | MF_FURY | MF_IMMOLATE |
                                                                      MF_FREEZE_BURST | MF_EXPLODE)))
        return 0;
    const Player *p = &s_pl[mi->owner];
    return p->sk_pierce > 0 && missiles_can_pierce(mi) && (int)(rnd() % 100) < p->sk_pierce;
}

/* Furia de rayo: rayos a los monstruos de alrededor del que recibió la jabalina */
static void fury_bolts(const Missile *mi, int hit)
{
    /* Como MISSMODE_SrvHit20_LightningFury: un rayo (furylightning) desde
       donde pegó a cada uno de los que hay alrededor, también al que tocó,
       hasta calc1; el radio es el aurarangecalc de la habilidad */
    const Player *p = &s_pl[mi->owner];
    int sk = skills_find(p->cls, "Lightning Fury");
    SkillCtx c = pctx(p);
    float radius = sk >= 0 ? (float)skill_calc(skills_get(p->cls, sk), SC_AURARANGE, eff_lvl(p, sk), &c) / 2.0f : CHAIN_RANGE;
    if (radius < 1.0f)
        radius = 1.0f;
    int ids[32], n = units_enemies_near(mi->x, mi->y, radius, ids, 31), shot = 0;
    for (int k = 0; k < n; k++) /* el que tocó, primero */
        if (ids[k] == hit) {
            ids[k] = ids[0];
            ids[0] = hit;
        }
    for (int k = 0; k < n && shot < mi->count; k++) {
        float x, y;
        units_position(ids[k], 1.0f, &x, &y);
        float dx = x - mi->x, dy = y - mi->y;
        if (dx * dx + dy * dy < 0.01f) {
            dx = mi->vx;
            dy = mi->vy;
        }
        Missile *b = missiles_spawn("furylightning", mi->x, mi->y, dx, dy);
        if (!b)
            break;
        if (ids[k] != hit && b->num_hit < MISSILE_HITS) /* sale pegado al que tocó: lo atraviesa */
            b->hit[b->num_hit++] = hit;
        if (ids[k] == hit) { /* el del que tocó pega enseguida: que se vea el rayo sobre él */
            missiles_effect("lightninghit", x, y);
            overlays_add("lightning", OV_AT_UNIT, hit, x, y, 0);
        }
        b->owner = mi->owner;
        b->ar = mi->ar;
        b->level = mi->level;
        b->elem = ELEM_LTNG;
        b->emin = mi->emin;
        b->emax = mi->emax;
        b->flags |= MF_ALWAYS_HIT;
        shot++;
    }
}

/* Inmolación y Flecha congelante: la explosión al chocar */
static void burst_explode(Player *p, const Missile *mi)
{
    float r = mi->radius10 / 10.0f;
    int ids[32], n = units_enemies_near(mi->x, mi->y, r > 1.0f ? r : 1.0f, ids, 32);
    for (int k = 0; k < n; k++) {
        int elem = (mi->flags & MF_FREEZE_BURST) ? ELEM_COLD : ELEM_FIRE;
        int exp = units_damage(ids[k], s_one_hit ? 1 << 20 : units_resist(ids[k], elem, roll(mi->emin, mi->emax)));
        if (exp > 0)
            award_kill(p, ids[k], exp);
        else if (mi->flags & MF_FREEZE_BURST)
            units_freeze(ids[k], mi->elen);
    }
    if (mi->flags & MF_FREEZE_BURST) {
        missiles_effect("freezingarrowexp1", mi->x, mi->y);
        missiles_effect("freezingarrowexp2", mi->x, mi->y);
        return;
    }
    /* El fuego del piso (immolationfire): 12-24 de daño por segundo, +4 por nivel */
    Missile *f = missiles_effect("immolationfire", mi->x, mi->y);
    if (f) {
        f->flags |= MF_BURN;
        f->owner = mi->owner;
        f->emin = 12 + 4 * (mi->count - 1);
        f->emax = 24 + 4 * (mi->count - 1);
        f->radius10 = 20;
        f->life = 100;
    }
}

/* El fuego del piso quema a los que están encima (el daño es por segundo) */
static void burn_tick(Missile *mi)
{
    if (mi->owner >= s_num_players)
        return;
    mi->burn_acc += (float)roll(mi->emin, mi->emax) / TICK_RATE;
    int d = (int)mi->burn_acc;
    if (d < 1)
        return;
    mi->burn_acc -= (float)d;
    if (mi->owner < 0) { /* de un monstruo (el Muro de fuego y el Meteoro de los vampiros): quema a los jugadores */
        float r = mi->radius10 / 10.0f;
        for (int k = 0; k < s_num_players; k++) {
            Player *p = &s_pl[k];
            float dx = p->x - mi->x, dy = p->y - mi->y;
            if (p->state < PS_DYING && !(p->level && p->level->town) && dx * dx + dy * dy <= r * r)
                take_damage(p, player_elem_hit(p, ELEM_FIRE, d), 0);
        }
        return;
    }
    /* (el Campo de choque quema con rayo; la miasma del Brujo es mágica) */
    int elem = mi->elem == ELEM_LTNG || mi->elem == ELEM_MAG ? mi->elem : ELEM_FIRE;
    int dmg_elem = elem;
    int ids[16], n = units_enemies_near(mi->x, mi->y, mi->radius10 / 10.0f, ids, 16);
    for (int k = 0; k < n; k++) {
        int exp = units_damage(ids[k], units_resist(ids[k], dmg_elem, d));
        if (exp > 0)
            award_kill(&s_pl[mi->owner], ids[k], exp);
    }
}
