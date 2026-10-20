#include "missiles.h"

#include "audio.h"
#include "fmt/formats.h"
#include "fmt/fs.h"
#include "game.h"
#include "jobs.h"
#include "video.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MISSILES 768
#define PI_F         3.14159265f

/* Velocidad de missiles.txt -> subtiles por tick (la misma escala que la
   de los personajes: velocidad * 5/6 subtiles por segundo) */
#define SPEED_SCALE (5.0f / 6.0f / TICK_RATE)

struct MissileDef {
    char   name[32];
    char   celfile[40];
    float  speed;
    int    range;
    int    anim_len;
    int    loop;
    char   explosion[32];
    char   hit_sub[32];  /* CltHitSubMissile1: lo que suelta al tocar el piso */
    int    pierce;       /* Perforar lo puede hacer atravesar */
    uint16_t coll;       /* lo del mapa que lo frena (por su CollideType) */
    int    elem, emin, emax, dmin, dmax;
    int    elev_min[5], elev_max[5]; /* MinELev1..5 y MaxELev1..5: lo que sube por nivel de la habilidad */
    int    trans;     /* 1 = se suma al fondo (fuego, hielo, explosiones) */
    int    light;     /* Light: cuánto alumbra en la oscuridad (0 = nada) */
    char   prog_overlay[32]; /* ProgOverlay: el efecto sobre el que toca (el destello del rayo) */
    int    travel_sound, hit_sound, impact_sound; /* sounds.txt (-1 = nada) */
    int    gfx_state; /* 0 = sin cargar, 1 = bien, -1 = sin gráfico, 2 = cargándose (job_flag) */
    Sprite sprite;
};

static MissileDef *s_defs;
static int         s_num_defs;
static Missile     s_missiles[MAX_MISSILES];
static int         s_top; /* uno más que el último lugar usado */
static float       s_prev[MAX_MISSILES][2];

static void lower_copy(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

int missiles_init(void)
{
    size_t n;
    uint8_t *d = fs_read("data/global/excel/missiles.txt", &n);
    TxtTable t = { 0 };
    if (!d || txt_load(d, n, &t) != 0) {
        free(d);
        fprintf(stderr, "falta missiles.txt: no va a haber proyectiles\n");
        return -1;
    }
    free(d);
    s_defs = calloc((size_t)t.rows, sizeof *s_defs);
    s_num_defs = 0;
    for (int r = 1; r < t.rows && s_defs; r++) {
        const char *name = txt_get(&t, r, txt_col(&t, "Missile"));
        if (!name[0])
            continue;
        MissileDef *m = &s_defs[s_num_defs++];
        lower_copy(m->name, sizeof m->name, name);
        lower_copy(m->celfile, sizeof m->celfile, txt_get(&t, r, txt_col(&t, "CelFile")));
        m->speed = txt_get_int(&t, r, "Vel", 0) * SPEED_SCALE;
        m->range = txt_get_int(&t, r, "Range", 20);
        /* Lo del mapa que frena cada tipo de choque (la tabla del original,
           gCollTable de la 1.10 descrita en la Phrozen Keep): los que no
           chocan (0, 4) y los que solo chocan con otros proyectiles (7) pasan
           todo; el 8 frena también en el terreno bloqueado. Los bits de las
           unidades los mira la prueba de impacto aparte. */
        static const uint16_t k_coll[9] = { 0, COLL_MISSILE_BARRIER, COLL_MISSILE_BARRIER, COLL_MISSILE_BARRIER, 0,
                                            COLL_MISSILE_BARRIER, COLL_MISSILE_BARRIER, 0,
                                            COLL_MISSILE_BARRIER | COLL_WALL };
        int ct = txt_get_int(&t, r, "CollideType", 0);
        m->coll = k_coll[ct >= 0 && ct <= 8 ? ct : 0];
        m->anim_len = txt_get_int(&t, r, "AnimLen", 1);
        m->loop = txt_get_int(&t, r, "LoopAnim", 0);
        lower_copy(m->explosion, sizeof m->explosion, txt_get(&t, r, txt_col(&t, "ExplosionMissile")));
        lower_copy(m->hit_sub, sizeof m->hit_sub, txt_get(&t, r, txt_col(&t, "CltHitSubMissile1")));
        m->pierce = txt_get_int(&t, r, "Pierce", 0);
        static const char *const elems[] = { "", "fire", "cold", "ltng", "pois", "mag" };
        const char *et = txt_get(&t, r, txt_col(&t, "EType"));
        for (int e = 1; e < 6; e++)
            if (strcmp(et, elems[e]) == 0)
                m->elem = e;
        m->emin = txt_get_int(&t, r, "EMin", 0);
        m->emax = txt_get_int(&t, r, "EMax", m->emin);
        for (int k = 0; k < 5; k++) {
            char col[16];
            snprintf(col, sizeof col, "MinELev%d", k + 1);
            m->elev_min[k] = txt_get_int(&t, r, col, 0);
            snprintf(col, sizeof col, "MaxELev%d", k + 1);
            m->elev_max[k] = txt_get_int(&t, r, col, 0);
        }
        m->trans = txt_get_int(&t, r, "Trans", 0);
        m->light = txt_get_int(&t, r, "Light", 0);
        lower_copy(m->prog_overlay, sizeof m->prog_overlay, txt_get(&t, r, txt_col(&t, "ProgOverlay")));
        m->dmin = txt_get_int(&t, r, "MinDamage", 0);
        m->dmax = txt_get_int(&t, r, "MaxDamage", m->dmin);
        { /* en 1/256 << HitShift, como el original (8 = puntos enteros; el
             abanico de Andariel no tiene: 1792 son 7 puntos) */
            int hs = txt_get_int(&t, r, "HitShift", 0);
            hs = hs < 0 ? 0 : hs > 8 ? 8 : hs;
            m->dmin = (m->dmin << hs) >> 8;
            m->dmax = (m->dmax << hs) >> 8;
        }
        m->travel_sound = audio_find(txt_get(&t, r, txt_col(&t, "TravelSound")));
        m->hit_sound = audio_find(txt_get(&t, r, txt_col(&t, "HitSound")));
        /* HitClass (hitclass.txt; los bits altos son otras marcas) */
        static const char *const k_impacts[] = {
            NULL, "impact_punch_1", "impact_blade_swing_1", "impact_blade_swing_1", "impact_blade_swing_1",
            "impact_blade_swing_1", "impact_blade_thrust_1", "impact_blade_thrust_1", "impact_blunt_1",
            "impact_blunt_1", "impact_arrow_1", "impact_arrow_1", "impact_claw_1",
        };
        int hc = txt_get_int(&t, r, "HitClass", 0) & 15;
        m->impact_sound = hc < (int)(sizeof k_impacts / sizeof k_impacts[0]) && k_impacts[hc] ? audio_find(k_impacts[hc]) : -1;
    }
    txt_free(&t);
    return 0;
}

static MissileDef *find_def(const char *name)
{
    char key[32];
    lower_copy(key, sizeof key, name);
    for (int i = 0; i < s_num_defs; i++)
        if (strcmp(s_defs[i].name, key) == 0)
            return &s_defs[i];
    return NULL;
}

int missiles_exists(const char *name)
{
    return find_def(name) != NULL;
}

void missiles_base_damage(const char *name, int *elem, int *emin, int *emax, int *dmin, int *dmax)
{
    const MissileDef *m = find_def(name);
    *elem = m ? m->elem : 0;
    *emin = m ? m->emin : 0;
    *emax = m ? m->emax : 0;
    *dmin = m ? m->dmin : 0;
    *dmax = m ? m->dmax : 0;
}

void missiles_level_damage(const char *name, int lvl, int *emin, int *emax)
{
    static const int k_from[5] = { 2, 9, 17, 23, 29 }, k_to[5] = { 8, 16, 22, 28, 99 };
    const MissileDef *m = find_def(name);
    *emin = m ? m->emin : 0;
    *emax = m ? m->emax : 0;
    for (int k = 0; m && k < 5; k++) {
        int n = (lvl < k_to[k] ? lvl : k_to[k]) - k_from[k] + 1;
        if (n <= 0)
            break;
        *emin += n * m->elev_min[k];
        *emax += n * m->elev_max[k];
    }
}

static void def_sprite_load(void *ptr, int arg)
{
    (void)arg;
    MissileDef *m = ptr;
    char path[96];
    snprintf(path, sizeof path, "data/global/missiles/%s.dcc", m->celfile);
    size_t n;
    uint8_t *d = m->celfile[0] && strcmp(m->celfile, "null") != 0 ? fs_read(path, &n) : NULL;
    if (!d && m->celfile[0] && strcmp(m->celfile, "null") != 0) { /* el de D2R que apunta a otro: el de su nombre */
        snprintf(path, sizeof path, "data/global/missiles/%s.dcc", m->name); /* (binddemonchannel) */
        d = fs_read(path, &n);
    }
    job_flag_set(&m->gfx_state, d && dcc_load(d, n, &m->sprite) == 0 ? 1 : -1);
    free(d);
}

/* El gráfico se lee la primera vez que se dispara, en el otro hilo (en la
   Pi uno grande tardaba hasta 0,6 s y trababa el juego): mientras, el
   proyectil vuela sin dibujarse */
static const Sprite *def_sprite(MissileDef *m)
{
    if (job_flag_get(&m->gfx_state) == 0) {
        job_flag_set(&m->gfx_state, 2);
        jobs_push(def_sprite_load, m, 0);
    }
    return job_flag_get(&m->gfx_state) == 1 ? &m->sprite : NULL;
}

Missile *missiles_spawn(const char *name, float x, float y, float dx, float dy)
{
    MissileDef *def = find_def(name);
    if (!def)
        return NULL;
    int slot = -1;
    for (int i = 0; i < MAX_MISSILES && slot < 0; i++)
        if (!s_missiles[i].used)
            slot = i;
    if (slot < 0)
        return NULL;
    if (slot >= s_top)
        s_top = slot + 1;
    Missile *mi = &s_missiles[slot];
    memset(mi, 0, sizeof *mi);
    mi->def = def;
    mi->x = x;
    mi->y = y;
    float l = sqrtf(dx * dx + dy * dy);
    if (l > 0.0001f) {
        mi->vx = dx / l * def->speed;
        mi->vy = dy / l * def->speed;
    }
    mi->life = def->range > 0 ? def->range : 1;
    mi->source = -1;
    mi->used = 1;
    mi->sound = audio_play(def->travel_sound, x, y);
    if (def->speed <= 0.0f)
        mi->flags |= MF_EFFECT; /* explosiones y nubes: quietas */
    s_prev[slot][0] = x;
    s_prev[slot][1] = y;
    return mi;
}

Missile *missiles_effect(const char *name, float x, float y)
{
    Missile *mi = name && name[0] ? missiles_spawn(name, x, y, 0.0f, 0.0f) : NULL;
    if (mi) {
        mi->flags |= MF_EFFECT;
        mi->vx = mi->vy = 0.0f;
        /* Se muestra la animación entera una vez */
        if (mi->def->anim_len > 0 && mi->life < mi->def->anim_len)
            mi->life = mi->def->anim_len;
    }
    return mi;
}

int missiles_light(const Missile *mi)
{
    return mi->def->light;
}

const char *missiles_prog_overlay(const Missile *mi)
{
    return mi->def->prog_overlay[0] ? mi->def->prog_overlay : NULL;
}

int missiles_can_pierce(const Missile *mi)
{
    return mi->def->pierce;
}

const char *missiles_chain_next(const Missile *mi)
{
    return mi->def->explosion[0] ? mi->def->explosion : mi->def->hit_sub[0] ? mi->def->hit_sub : NULL;
}

/* Hasta el último lugar usado: los recorridos de cada tick y cada cuadro
   no pasan por los 256 */
int missiles_capacity(void)
{
    return s_top;
}

Missile *missiles_get(int i)
{
    return i >= 0 && i < MAX_MISSILES && s_missiles[i].used ? &s_missiles[i] : NULL;
}

void missiles_remove(int i)
{
    if (i < 0 || i >= MAX_MISSILES || !s_missiles[i].used)
        return;
    /* Los que suenan mientras vuelan (bola de fuego, rayo, nube de veneno) se callan */
    if (audio_loops(s_missiles[i].def->travel_sound))
        audio_stop(s_missiles[i].sound);
    s_missiles[i].used = 0;
    while (s_top > 0 && !s_missiles[s_top - 1].used)
        s_top--;
}

void missiles_clear(void)
{
    for (int i = 0; i < MAX_MISSILES; i++)
        missiles_remove(i);
    memset(s_missiles, 0, sizeof s_missiles);
    s_top = 0;
}

void missiles_impact_sound(const Missile *mi)
{
    if (mi->def->hit_sound < 0)
        audio_play(mi->def->impact_sound, mi->x, mi->y);
}

const char *missiles_explosion(const Missile *mi)
{
    return mi->def->explosion[0] ? mi->def->explosion : NULL;
}

void missiles_end(int i)
{
    Missile *mi = missiles_get(i);
    if (!mi)
        return;
    float x = mi->x, y = mi->y;
    char boom[32];
    snprintf(boom, sizeof boom, "%s", mi->def->explosion);
    audio_play(mi->def->hit_sound, x, y);
    missiles_remove(i);
    if (boom[0])
        missiles_effect(boom, x, y);
}

int missiles_step(int i, const Map *m)
{
    Missile *mi = missiles_get(i);
    if (!mi)
        return 2;
    s_prev[i][0] = mi->x;
    s_prev[i][1] = mi->y;
    mi->age++;
    if (mi->vz > 0.0f) { /* cae: termina al tocar el piso */
        mi->z -= mi->vz;
        if (mi->z <= 0.0f) {
            mi->z = 0.0f;
            return 2;
        }
    }
    if (--mi->life <= 0)
        return 2;
    if (mi->flags & MF_EFFECT)
        return 0;
    /* Dos medios pasos para no atravesar paredes finas */
    for (int k = 0; k < 2; k++) {
        mi->x += mi->vx * 0.5f;
        mi->y += mi->vy * 0.5f;
        if (map_coll(m, (int)floorf(mi->x), (int)floorf(mi->y)) & mi->def->coll)
            return 1;
    }
    audio_move(mi->sound, mi->x, mi->y);
    return 0;
}

void missiles_position(int i, float alpha, float *x, float *y)
{
    const Missile *mi = &s_missiles[i];
    *x = s_prev[i][0] + (mi->x - s_prev[i][0]) * alpha;
    *y = s_prev[i][1] + (mi->y - s_prev[i][1]) * alpha;
}

/* Dirección de pantalla -> dirección del DCC. Con 16 y 32 direcciones el
   orden es el del juego: primero las 4 diagonales y las 4 rectas, después
   las intermedias. */
static int dcc_dir(float vx, float vy, int dirs)
{
    if (dirs <= 1)
        return 0;
    static const int k_dir16[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
    static const int k_dir8[8] = { 4, 0, 5, 1, 6, 2, 7, 3 };
    /* Vector en pantalla (isométrico) y sector en sentido horario desde el sur */
    float sx = vx - vy, sy = (vx + vy) * 0.5f;
    float a = atan2f(-sx, sy); /* 0 = abajo, crece hacia la izquierda (horario visto en pantalla) */
    if (a < 0.0f)
        a += 2.0f * PI_F;
    int n = dirs >= 32 ? 32 : dirs >= 16 ? 16 : 8;
    int sector = (int)lroundf(a / (2.0f * PI_F / n)) % n;
    if (n == 8)
        return k_dir8[sector];
    if (n == 16)
        return k_dir16[sector];
    return sector % 2 ? 16 + sector / 2 : k_dir16[sector / 2];
}

void missiles_draw(int i, int sx, int sy)
{
    Missile *mi = &s_missiles[i];
    const Sprite *sp = def_sprite((MissileDef *)mi->def);
    if (!sp || sp->frames_per_dir <= 0)
        return;
    int dir = dcc_dir(mi->vx, mi->vy, sp->directions);
    if (dir >= sp->directions)
        dir = 0;
    int frame = mi->age;
    if (mi->def->loop)
        frame %= sp->frames_per_dir;
    else if (frame >= sp->frames_per_dir)
        frame = sp->frames_per_dir - 1;
    const SpriteFrame *f = sprite_frame(sp, dir, frame);
    if (!f->pixels)
        return;
    sy -= (int)mi->z;
    if (mi->def->trans)
        fb_blit_blend_spans(f->pixels, f->spans, f->width, f->height, sx + f->off_x, sy + f->off_y, BLEND_ADD);
    else
        fb_blit_spans(f->pixels, f->spans, f->width, f->height, sx + f->off_x, sy + f->off_y);
}
