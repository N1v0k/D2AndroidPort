#include "overlays.h"

#include "fmt/formats.h"
#include "fmt/fs.h"

#include "iso.h"
#include "jobs.h"
#include "video.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEFS     400
#define MAX_OVERLAYS 256

typedef struct {
    char   name[32];
    char   file[48];
    int    anim_rate; /* 16 = un cuadro por tick */
    int    trans;     /* se suma al fondo (como la luz) */
    int    predraw;   /* va detrás del personaje (el círculo de las auras): se dibuja sobre el piso, antes que todo */
    int    one_of;    /* 1ofN: se elige uno entre este y los N-1 que siguen */
    int    radius;    /* Radius: cuánto alumbra en la oscuridad */
    int    state;     /* 0 sin cargar, 1 listo, -1 falta, 2 cargándose (job_flag) */
    Sprite sprite;
} OverlayDef;

typedef struct {
    int   def;  /* -1 = lugar libre */
    int   at, id;
    float x, y; /* el punto, o dónde estaba lo que sigue (para notar si cambió de dueño) */
    int   age, ticks;
} Overlay;

static OverlayDef s_defs[MAX_DEFS];
static int        s_num_defs;
static Overlay    s_ov[MAX_OVERLAYS];

int overlays_init(void)
{
    TxtTable t;
    size_t n;
    uint8_t *d = fs_read("data/global/excel/overlay.txt", &n);
    int ok = d && txt_load(d, n, &t) == 0;
    free(d);
    for (int i = 0; i < MAX_OVERLAYS; i++)
        s_ov[i].def = -1;
    if (!ok) {
        fprintf(stderr, "falta data/global/excel/overlay.txt: sin efectos de las habilidades\n");
        return -1;
    }
    /* Todos (los gráficos se cargan recién al usarlos), en el orden de la
       tabla: los de 1ofN eligen entre los que siguen */
    int c_name = txt_col(&t, "overlay"), c_file = txt_col(&t, "Filename");
    for (int r = 1; r < t.rows && s_num_defs < MAX_DEFS; r++) {
        const char *name = txt_get(&t, r, c_name), *file = txt_get(&t, r, c_file);
        if (!name || !name[0] || !file)
            continue;
        OverlayDef *o = &s_defs[s_num_defs++];
        memset(o, 0, sizeof *o);
        snprintf(o->name, sizeof o->name, "%s", name);
        snprintf(o->file, sizeof o->file, "%s", file);
        for (char *c = o->file; *c; c++) /* "expansion\\poisonhit" */
            if (*c == '\\')
                *c = '/';
        o->anim_rate = txt_get_int(&t, r, "AnimRate", 16);
        o->trans = txt_get_int(&t, r, "Trans", 0);
        o->predraw = txt_get_int(&t, r, "PreDraw", 0);
        o->one_of = txt_get_int(&t, r, "1ofN", 1);
        o->radius = txt_get_int(&t, r, "Radius", 0);
        if (strcmp(o->file, "null") == 0)
            o->state = -1;
    }
    txt_free(&t);
    return 0;
}

static int find_def(const char *name)
{
    for (int i = 0; i < s_num_defs; i++)
        if (strcmp(s_defs[i].name, name) == 0)
            return i;
    return -1;
}

static void def_sprite_load(void *ptr, int arg)
{
    (void)arg;
    OverlayDef *o = ptr;
    char path[96];
    snprintf(path, sizeof path, "data/global/overlays/%s.dcc", o->file);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    job_flag_set(&o->state, d && dcc_load(d, n, &o->sprite) == 0 ? 1 : -1);
    free(d);
}

/* Como los proyectiles: la primera vez se lee en el otro hilo */
static const Sprite *def_sprite(OverlayDef *o)
{
    if (job_flag_get(&o->state) == 0) {
        job_flag_set(&o->state, 2);
        jobs_push(def_sprite_load, o, 0);
    }
    return job_flag_get(&o->state) == 1 ? &o->sprite : NULL;
}

void overlays_preload(const char *name)
{
    int d = find_def(name);
    if (d >= 0)
        def_sprite(&s_defs[d]);
}

void overlays_add(const char *name, int at, int id, float x, float y, int ticks)
{
    int d = find_def(name);
    if (getenv("D2_FX_DEBUG")) /* depuración: cada efecto que se pide */
        printf("efecto %s sobre %d/%d en (%.1f, %.1f), %d ticks%s\n", name, at, id, x, y, ticks,
               d < 0 ? " (no está en overlay.txt)" : "");
    if (d < 0)
        return;
    /* El mismo efecto sobre lo mismo: se renueva en vez de apilarse; si es
       el de un estado (con duración) sigue su animación y dura más */
    int slot = -1;
    for (int i = 0; i < MAX_OVERLAYS; i++)
        if (s_ov[i].def >= d && s_ov[i].def < d + (s_defs[d].one_of > 1 ? s_defs[d].one_of : 1) && at != OV_AT_POINT &&
            s_ov[i].at == at && s_ov[i].id == id) {
            slot = i;
            break;
        }
    if (slot >= 0 && ticks > 0) {
        s_ov[slot].ticks = s_ov[slot].age + ticks;
        return;
    }
    static uint32_t seed = 12345u;
    if (s_defs[d].one_of > 1) { /* doubledamage1: uno de impacting01, 02 o 03 */
        seed = seed * 1664525u + 1013904223u;
        int k = (int)((seed >> 16) % (uint32_t)s_defs[d].one_of);
        if (d + k < s_num_defs)
            d += k;
    }
    for (int i = 0; i < MAX_OVERLAYS && slot < 0; i++)
        if (s_ov[i].def < 0)
            slot = i;
    if (slot < 0)
        return;
    Overlay *o = &s_ov[slot];
    o->def = d;
    o->at = at;
    o->id = id;
    o->x = x;
    o->y = y;
    o->age = 0;
    o->ticks = ticks;
}

void overlays_tick(void)
{
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        Overlay *o = &s_ov[i];
        if (o->def < 0)
            continue;
        const Sprite *sp = def_sprite(&s_defs[o->def]);
        if (!sp && job_flag_get(&s_defs[o->def].state) == 2)
            continue; /* cargándose: espera sin avanzar */
        o->age++;
        int frames = sp ? sp->frames_per_dir : 0;
        int frame = o->age * s_defs[o->def].anim_rate / 16;
        if (!sp || frames <= 0 || (o->ticks > 0 ? o->age >= o->ticks : frame >= frames))
            o->def = -1;
    }
}

int overlays_lights(float *x, float *y, int *radius, int max, OverlayPos pos)
{
    int n = 0;
    for (int i = 0; i < MAX_OVERLAYS && n < max; i++) {
        const Overlay *o = &s_ov[i];
        if (o->def < 0 || s_defs[o->def].radius <= 0)
            continue;
        float ox = o->x, oy = o->y;
        if (o->at != OV_AT_POINT && !pos(o->at, o->id, &ox, &oy))
            continue;
        x[n] = ox;
        y[n] = oy;
        radius[n] = s_defs[o->def].radius;
        n++;
    }
    return n;
}

void overlays_remove(const char *prefix, int at, int id)
{
    size_t n = strlen(prefix);
    for (int i = 0; i < MAX_OVERLAYS; i++)
        if (s_ov[i].def >= 0 && s_ov[i].at == at && s_ov[i].id == id && strncmp(s_defs[s_ov[i].def].name, prefix, n) == 0)
            s_ov[i].def = -1;
}

void overlays_clear(void)
{
    for (int i = 0; i < MAX_OVERLAYS; i++)
        s_ov[i].def = -1;
}

static void draw_pass(float cam_x, float cam_y, OverlayPos pos, int back)
{
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        Overlay *o = &s_ov[i];
        if (o->def < 0 || s_defs[o->def].predraw != back)
            continue;
        float x = o->x, y = o->y;
        if (o->at != OV_AT_POINT) {
            /* Ya no está, o ese lugar ahora es de otro monstruo (los jugadores
               no cambian: con el Teletransporte se van lejos de golpe) */
            if (!pos(o->at, o->id, &x, &y) || (o->at == OV_AT_UNIT && fabsf(x - o->x) + fabsf(y - o->y) > 4.0f)) {
                o->def = -1;
                continue;
            }
            o->x = x;
            o->y = y;
        }
        const Sprite *sp = def_sprite(&s_defs[o->def]);
        if (!sp || sp->frames_per_dir <= 0)
            continue;
        int frame = (o->age * s_defs[o->def].anim_rate / 16) % sp->frames_per_dir;
        const SpriteFrame *f = sprite_frame(sp, 0, frame);
        if (!f->pixels)
            continue;
        float sx, sy;
        iso_to_screen(x, y, &sx, &sy);
        int px = SCREEN_W / 2 + (int)lroundf(sx - cam_x) + f->off_x;
        int py = SCREEN_H / 2 + (int)lroundf(sy - cam_y) + f->off_y;
        if (s_defs[o->def].trans)
            fb_blit_blend_spans(f->pixels, f->spans, f->width, f->height, px, py, BLEND_ADD);
        else
            fb_blit(f->pixels, f->width, f->height, px, py);
    }
}

void overlays_draw(float cam_x, float cam_y, OverlayPos pos)
{
    draw_pass(cam_x, cam_y, pos, 0);
}

void overlays_draw_back(float cam_x, float cam_y, OverlayPos pos)
{
    draw_pass(cam_x, cam_y, pos, 1);
}
