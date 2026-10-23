#include "ui.h"

#include "fmt/formats.h"
#include "fmt/fs.h"
#include "jobs.h"
#include "video.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UI 160

enum { UI_LOADING, UI_READY, UI_MISSING };
typedef struct {
    char   path[48];
    Sprite sprite;
    int    state; /* UI_* (job_flag_get/set): lo pone el hilo que lo lee */
} UiImage;

static UiImage s_images[MAX_UI];
static int     s_count;

static void load_job(void *ptr, int arg)
{
    (void)arg;
    UiImage *im = ptr;
    char file[96];
    snprintf(file, sizeof file, "data/global/ui/%s.dc6", im->path);
    size_t n;
    uint8_t *d = fs_read(file, &n);
    int ok = d && dc6_load(d, n, &im->sprite) == 0;
    free(d);
    if (!ok)
        fprintf(stderr, "falta ui/%s.dc6\n", im->path);
    job_flag_set(&im->state, ok ? UI_READY : UI_MISSING);
}

/* El de ese nombre; si no estaba, se lee en el hilo de trabajos (en la Pi
   el panel de los waypoints tardaba 18 ms en el hilo del juego) en su turno
   (LOAD_QUEUE) o adelante de lo que espera (LOAD_FIRST), o en este hilo
   (LOAD_NOW, y sin el hilo de trabajos) */
enum { LOAD_QUEUE, LOAD_FIRST, LOAD_NOW };
static UiImage *find(const char *path, int how)
{
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_images[i].path, path) == 0)
            return &s_images[i];
    if (s_count == MAX_UI)
        return NULL;
    UiImage *im = &s_images[s_count++];
    snprintf(im->path, sizeof im->path, "%s", path);
    job_flag_set(&im->state, UI_LOADING);
    if (how == LOAD_QUEUE)
        jobs_push(load_job, im, 0);
    else if (how == LOAD_NOW || !jobs_push_first(load_job, im, 0))
        load_job(im, 0);
    return im;
}

/* Mientras se lee no está: ese cuadro se dibuja sin él */
static const Sprite *get(const char *path)
{
    const UiImage *im = find(path, LOAD_FIRST);
    return im && job_flag_get(&im->state) == UI_READY ? &im->sprite : NULL;
}

void ui_preload(const char *path)
{
    find(path, LOAD_QUEUE);
}

static const SpriteFrame *frame_of(const char *path, int frame)
{
    const Sprite *s = get(path);
    if (!s || frame < 0 || frame >= s->directions * s->frames_per_dir)
        return NULL;
    const SpriteFrame *f = &s->frames[frame];
    return f->pixels ? f : NULL;
}

int ui_frame_count(const char *path)
{
    const Sprite *s = get(path);
    return s ? s->directions * s->frames_per_dir : 0;
}

int ui_frame_size(const char *path, int frame, int *w, int *h)
{
    const SpriteFrame *f = frame_of(path, frame);
    *w = f ? f->width : 0;
    *h = f ? f->height : 0;
    return f != NULL;
}

void ui_frame(const char *path, int frame, int x, int y)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (f)
        fb_blit_spans(f->pixels, f->spans, f->width, f->height, x, y);
}

void ui_frame_anchor(const char *path, int frame, int ax, int ay, int blend)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (!f)
        return;
    if (blend < 0)
        fb_blit_spans(f->pixels, f->spans, f->width, f->height, ax + f->off_x, ay + f->off_y);
    else
        fb_blit_blend_spans(f->pixels, f->spans, f->width, f->height, ax + f->off_x, ay + f->off_y, blend);
}

void ui_frame_blend(const char *path, int frame, int x, int y, int blend)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (f)
        fb_blit_blend_spans(f->pixels, f->spans, f->width, f->height, x, y, blend);
}

void ui_frame_scaled(const char *path, int frame, int x, int y, int w, int h, int level)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (f)
        fb_blit_scaled(f->pixels, f->width, f->height, x, y, w, h, level);
}

void ui_frame_rotated(const char *path, int frame, int cx, int cy, int size, float angle)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (f)
        fb_blit_rotated(f->pixels, f->width, f->height, cx, cy, size, angle);
}

void ui_frame_rows(const char *path, int frame, int x, int y, int from_y)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (!f)
        return;
    if (from_y < 0)
        from_y = 0;
    if (from_y >= f->height)
        return;
    fb_blit(f->pixels + from_y * f->width, f->width, f->height - from_y, x, y + from_y);
}

void ui_frame_part(const char *path, int frame, int x, int y, int sx, int sy, int w, int h)
{
    const SpriteFrame *f = frame_of(path, frame);
    if (!f)
        return;
    if (sx < 0) { w += sx; x -= sx; sx = 0; }
    if (sy < 0) { h += sy; y -= sy; sy = 0; }
    if (sx + w > f->width) w = f->width - sx;
    if (sy + h > f->height) h = f->height - sy;
    if (w > 0 && h > 0)
        fb_blit_spans_part(f->pixels, f->spans, f->width, sx, sy, w, h, x, y);
}

/* Los pedazos de una fila miden lo que el más ancho de su columna y los de
   una columna lo que el más alto de su fila (256, salvo los del borde) */
void ui_grid(const char *path, int first, int count, int cols, int x, int y)
{
    int col_x[16] = { 0 }, row_y[16] = { 0 };
    int rows = (count + cols - 1) / cols;
    if (cols > 15 || rows > 15)
        return;
    for (int c = 0; c < cols; c++) {
        int w = 0;
        for (int r = 0; r < rows; r++) {
            int fw, fh;
            if (r * cols + c < count && ui_frame_size(path, first + r * cols + c, &fw, &fh) && fw > w)
                w = fw;
        }
        col_x[c + 1] = col_x[c] + w;
    }
    for (int r = 0; r < rows; r++) {
        int h = 0;
        for (int c = 0; c < cols; c++) {
            int fw, fh;
            if (r * cols + c < count && ui_frame_size(path, first + r * cols + c, &fw, &fh) && fh > h)
                h = fh;
        }
        row_y[r + 1] = row_y[r] + h;
    }
    for (int k = 0; k < count; k++)
        ui_frame(path, first + k, x + col_x[k % cols], y + row_y[k / cols]);
}

void ui_dark_box(int x, int y, int w, int h)
{
    fb_shade_rect(x, y, w, h, 4);
}

void ui_box(int x, int y, int w, int h)
{
    fb_shade_rect(x, y, w, h, 5);
    fb_hline(x, x + w - 1, y, COL(R_GOLD, 7));
    fb_hline(x, x + w - 1, y + h - 1, COL(R_GOLD, 7));
    fb_vline(x, y, y + h - 1, COL(R_GOLD, 7));
    fb_vline(x + w - 1, y, y + h - 1, COL(R_GOLD, 7));
}

void ui_loading(int step, int steps)
{
    static uint32_t pal[256];
    static int have_pal = -1;
    if (have_pal < 0) {
        size_t n;
        uint8_t *d = fs_read("data/global/palette/loading/pal.dat", &n);
        have_pal = d && pal_load(d, n, pal) == 0;
        free(d);
    }
    uint32_t saved[256];
    memcpy(saved, g_fb.pal, sizeof saved);
    if (have_pal)
        memcpy(g_fb.pal, pal, sizeof pal);
    find("loading/loadingscreen_blank", LOAD_NOW); /* (se muestra ya, mientras carga) */
    int frames = ui_frame_count("loading/loadingscreen_blank");
    int f = steps > 1 ? step * (frames - 1) / (steps - 1) : frames - 1;
    f = f < 0 ? 0 : f >= frames ? frames - 1 : f;
    fb_fill_rect(0, 0, SCREEN_W, SCREEN_H, 0);
    int w = 256, h = 256;
    ui_frame_size("loading/loadingscreen_blank", f, &w, &h);
    ui_frame("loading/loadingscreen_blank", f, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2);
    video_present();
    SDL_PumpEvents(); /* que el sistema no lo dé por colgado mientras carga */
    memcpy(g_fb.pal, saved, sizeof saved);
}
