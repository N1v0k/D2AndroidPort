#include "charanim.h"

#include "fmt/fs.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const AnimData *s_animdata;

void charanim_set_animdata(const AnimData *ad)
{
    s_animdata = ad;
}

/* Un DCC; si al lado hay una versión editada (.d2rw: los cuadros en crudo,
   el formato de `d2fmt raw`; no viene en el paquete), esa; si no está el
   DCC, el DC6 del mismo nombre (Tyrael tiene sus piezas en DC6) */
static int load_sprite(Sprite *s, const char *path)
{
    size_t n, len = strlen(path);
    if (len > 4 && len < 252 && strcmp(path + len - 4, ".dcc") == 0) {
        char edited[256];
        snprintf(edited, sizeof edited, "%.*s.d2rw", (int)(len - 4), path);
        uint8_t *e = fs_read(edited, &n);
        if (e) {
            int r = raw_load(e, n, s);
            free(e);
            if (r == 0)
                return 0;
            fprintf(stderr, "no se pudo leer %s: uso el original\n", edited);
        }
    }
    uint8_t *d = fs_read(path, &n);
    int dc6 = 0;
    if (!d && len > 4 && len < 252 && strcmp(path + len - 4, ".dcc") == 0) {
        char alt[256];
        snprintf(alt, sizeof alt, "%.*s.dc6", (int)(len - 4), path);
        d = fs_read(alt, &n);
        dc6 = d != NULL;
    }
    if (!d)
        return -1;
    int r = dc6 ? dc6_load(d, n, s) : dcc_load(d, n, s);
    free(d);
    return r;
}

/* Algunos archivos del juego no respetan el nombre esperado (por ejemplo el
   torso de la pícara se llama "rglglit..." dentro de la carpeta "tr"). Si
   el nombre exacto no existe, se busca en la carpeta de la capa un DCC del
   mismo modo y clase de arma. */
static int load_layer_fallback(Sprite *s, const char *dir, const char *suffix)
{
    char found[256] = "";
    if (fs_find(dir, suffix, found, sizeof found) != 0)
        return -1;
    return load_sprite(s, found);
}

int charanim_load(CharAnim *a, const CharLook *look, const char *mode, const char *wclass)
{
    memset(a, 0, sizeof *a);
    const char *base = look->base[0] ? look->base : "chars";
    char path[256];
    snprintf(path, sizeof path, "data/global/%s/%s/cof/%s%s%s.cof", base, look->token, look->token, mode, wclass);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    /* Algunos monstruos tienen ciertos modos solo sin arma ("hth"): los
       cabra del Clan Lunar usan "2hs" pero mueren con "hth" (si no, quedaban
       quietos al morir), y los esqueletos bloquean y hacen el segundo ataque
       con "hth" */
    if (!d && strcmp(wclass, "hth") != 0 && strcmp(base, "chars") != 0) {
        wclass = "hth";
        snprintf(path, sizeof path, "data/global/%s/%s/cof/%s%s%s.cof", base, look->token, look->token, mode, wclass);
        d = fs_read(path, &n);
    }
    if (!d || cof_load(d, n, &a->cof) != 0) {
        free(d);
        return -1;
    }
    free(d);

    for (int i = 0; i < a->cof.num_layers; i++) {
        const CofLayer *l = &a->cof.layers[i];
        const char *part = look->part[l->component];
        if (!part[0])
            continue;
        const char *comp = k_comp_names[l->component];
        snprintf(path, sizeof path, "data/global/%s/%s/%s/%s%s%s%s%s.dcc", base, look->token, comp,
                 look->token, comp, part, mode, l->weapon_class);
        if (load_sprite(&a->layer[l->component], path) == 0)
            continue;
        /* El torso de la Valquiria al caminar se llama "...wkhth.dcc" en vez
           de "...wlhth.dcc" */
        if (strcmp(mode, "wl") == 0) {
            char alt[256];
            snprintf(alt, sizeof alt, "data/global/%s/%s/%s/%s%s%swk%s.dcc", base, look->token, comp, look->token,
                     comp, part, l->weapon_class);
            if (load_sprite(&a->layer[l->component], alt) == 0)
                continue;
        }
        /* Las manos, el escudo y el casco: la misma pieza con otra clase de
           arma, o nada. Antes se tomaba cualquier archivo de la carpeta con
           ese modo, que podía ser otro objeto distinto en cada modo (se veía
           un escudo al caminar y otro al correr). */
        if (l->component == COMP_RH || l->component == COMP_LH || l->component == COMP_SH ||
            l->component == COMP_HD) {
            static const char *const classes[] = { "hth", "1hs", "1ht", "2hs", "2ht", "stf", "bow", "xbw", "ht1", "ht2" };
            int ok = 0;
            for (unsigned k = 0; k < sizeof classes / sizeof classes[0] && !ok; k++) {
                snprintf(path, sizeof path, "data/global/%s/%s/%s/%s%s%s%s%s.dcc", base, look->token, comp,
                         look->token, comp, part, mode, classes[k]);
                ok = load_sprite(&a->layer[l->component], path) == 0;
            }
            continue;
        }
        char dir[192], suffix[16];
        snprintf(dir, sizeof dir, "data/global/%s/%s/%s", base, look->token, comp);
        snprintf(suffix, sizeof suffix, "%s%s.dcc", mode, l->weapon_class);
        if (load_layer_fallback(&a->layer[l->component], dir, suffix) != 0)
            fprintf(stderr, "falta %s\n", path);
    }

    /* Efecto de dibujo del COF de las capas transparentes */
    static const int8_t k_effect_blend[] = { BLEND_25, BLEND_50, BLEND_75, BLEND_ADD, BLEND_MUL };
    for (int i = 0; i < a->cof.num_layers; i++) {
        const CofLayer *l = &a->cof.layers[i];
        if (l->transparent && l->component < COMP_COUNT)
            a->blend[l->component] =
                (int8_t)(1 + (l->draw_effect < sizeof k_effect_blend ? k_effect_blend[l->draw_effect] : BLEND_50));
    }

    a->speed = 256;
    a->hit_frame = a->cof.frames_per_dir / 2;
    if (s_animdata) {
        char name[16];
        snprintf(name, sizeof name, "%s%s%s", look->token, mode, wclass);
        const AnimRecord *rec = animdata_find(s_animdata, name);
        if (rec) {
            a->speed = rec->speed;
            for (int f = 0; f < rec->frames_per_dir && f < (int)sizeof rec->events; f++)
                if (rec->events[f] == 1) {
                    a->hit_frame = f;
                    break;
                }
        }
    }
    a->ok = 1;
    return 0;
}

void charanim_free(CharAnim *a)
{
    for (int i = 0; i < COMP_COUNT; i++)
        sprite_free(&a->layer[i]);
    cof_free(&a->cof);
    memset(a, 0, sizeof *a);
}

int charanim_frames(const CharAnim *a)
{
    return a->ok ? a->cof.frames_per_dir : 1;
}

int charanim_dirs(const CharAnim *a)
{
    return a->ok ? a->cof.directions : 1;
}

/* Dirección en el orden de los DCC, girando en sentido horario desde el sur */
static const uint8_t k_dcc16[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
static const uint8_t k_dcc8[8] = { 4, 0, 5, 1, 6, 2, 7, 3 };

/* Reduce la dirección de 16 sectores a la cantidad que tiene el archivo */
static int reduce_sector(int sector, int dirs)
{
    if (dirs >= 16)
        return sector;
    int step = 16 / dirs;
    return ((sector + step / 2) / step) % dirs;
}

static int dcc_dir(int sector, int dirs)
{
    if (dirs == 16)
        return k_dcc16[sector];
    if (dirs == 8)
        return k_dcc8[reduce_sector(sector, 8)];
    return dirs > 1 ? reduce_sector(sector, dirs) : 0;
}

void charanim_draw(const CharAnim *a, int sector, int frame, int x, int y)
{
    charanim_draw_tint(a, sector, frame, x, y, NULL);
}

void charanim_draw_ghost(const CharAnim *a, int sector, int frame, int x, int y, unsigned mask, int mode)
{
    if (!a->ok)
        return;
    const Cof *c = &a->cof;
    sector = ((sector % 16) + 16) % 16;
    int cof_dir = reduce_sector(sector, c->directions);
    frame = ((frame % c->frames_per_dir) + c->frames_per_dir) % c->frames_per_dir;
    const uint8_t *order = &c->priority[(cof_dir * c->frames_per_dir + frame) * c->num_layers];
    for (int i = 0; i < c->num_layers; i++) {
        int comp = order[i];
        if (comp >= COMP_COUNT || !a->layer[comp].frames || !((mask >> comp) & 1u))
            continue;
        const Sprite *s = &a->layer[comp];
        const SpriteFrame *fr = sprite_frame(s, dcc_dir(sector, s->directions), frame < s->frames_per_dir ? frame : 0);
        fb_blit_blend_spans(fr->pixels, fr->spans, fr->width, fr->height, x + fr->off_x, y + fr->off_y, mode);
    }
}

void charanim_draw_tint(const CharAnim *a, int sector, int frame, int x, int y, const uint8_t *lut)
{
    if (!a->ok)
        return;
    const Cof *c = &a->cof;
    sector = ((sector % 16) + 16) % 16;
    int cof_dir = reduce_sector(sector, c->directions);
    frame = ((frame % c->frames_per_dir) + c->frames_per_dir) % c->frames_per_dir;

    const uint8_t *order = &c->priority[(cof_dir * c->frames_per_dir + frame) * c->num_layers];
    for (int i = 0; i < c->num_layers; i++) {
        int comp = order[i];
        if (comp >= COMP_COUNT || !a->layer[comp].frames)
            continue;
        const Sprite *s = &a->layer[comp];
        int d = dcc_dir(sector, s->directions);
        int f = frame < s->frames_per_dir ? frame : 0;
        const SpriteFrame *fr = sprite_frame(s, d, f);
        if (a->blend[comp])
            fb_blit_blend_spans(fr->pixels, fr->spans, fr->width, fr->height, x + fr->off_x, y + fr->off_y,
                                a->blend[comp] - 1);
        else if (lut)
            fb_blit_remap(fr->pixels, fr->width, fr->height, x + fr->off_x, y + fr->off_y, lut);
        else
            fb_blit_spans(fr->pixels, fr->spans, fr->width, fr->height, x + fr->off_x, y + fr->off_y);
    }
}
