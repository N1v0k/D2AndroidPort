#include "cinema.h"

#include "audio.h"
#include "input.h"
#include "video.h"
#include "fmt/fs.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pl_mpeg (MIT, Dominic Szablewski): video MPEG-1, sonido MP2 y el
   contenedor MPEG-PS en un solo archivo. Es código de afuera: sin los
   avisos del proyecto. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define PL_MPEG_IMPLEMENTATION
#include "third/pl_mpeg.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* Las del menú de cinemáticas, en el orden del panel de la expansión
   (cinematicsmodalhd.json: act1X..act4X, strepilogueX, act5X y
   strlastcinematic, que es también el orden del número del paquete 0x61).
   Las dos presentaciones están siempre: la de la expansión el servidor no
   la manda nunca (D2Game no manda el 6), es la del arranque como d2intro */
static const struct {
    const char *name, *title;
} k_movies[] = {
    { "d2intro", "Presentación" }, { "act02start", "Acto II" }, { "act03start", "Acto III" },
    { "act04start", "Acto IV" },   { "act04end", "Epílogo" },   { "d2x_intro", "La búsqueda de Baal" },
    { "d2x_out", "El final de la destrucción" },
};
#define NUM_MOVIES ((int)(sizeof k_movies / sizeof k_movies[0]))
#define SEEN_ALWAYS ((1u << 0) | (1u << 5)) /* d2intro y d2x_intro */

static char     s_dir[256];
static unsigned s_seen = SEEN_ALWAYS; /* bit por película del menú */
static int      s_loaded;

static void seen_path(char *out, size_t cap)
{
    snprintf(out, cap, "%s/cinematicas.txt", s_dir);
}

static int movie_index(const char *name)
{
    for (int i = 0; i < NUM_MOVIES; i++)
        if (strcmp(k_movies[i].name, name) == 0)
            return i;
    return -1;
}

static void load_seen(void)
{
    if (s_loaded || !s_dir[0])
        return;
    s_loaded = 1;
    char path[300], line[64];
    seen_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        int i = movie_index(line);
        if (i >= 0)
            s_seen |= 1u << i;
    }
    fclose(f);
}

static void mark_seen(const char *name)
{
    int i = movie_index(name);
    load_seen();
    if (i < 0 || (s_seen & (1u << i)) || !s_dir[0])
        return;
    s_seen |= 1u << i;
    char path[300];
    seen_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (int k = 0; k < NUM_MOVIES; k++)
        if (s_seen & (1u << k))
            fprintf(f, "%s\n", k_movies[k].name);
    fclose(f);
}

void cinema_set_dir(const char *save_dir)
{
    snprintf(s_dir, sizeof s_dir, "%s", save_dir ? save_dir : "");
    s_loaded = 0;
    s_seen = SEEN_ALWAYS;
}

int cinema_count(void)
{
    return NUM_MOVIES;
}

const char *cinema_name(int i)
{
    return i >= 0 && i < NUM_MOVIES ? k_movies[i].name : "";
}

const char *cinema_title(int i)
{
    return i >= 0 && i < NUM_MOVIES ? k_movies[i].title : "";
}

int cinema_seen(int i)
{
    load_seen();
    return i >= 0 && i < NUM_MOVIES && (s_seen & (1u << i)) && cinema_exists(k_movies[i].name);
}

static FILE *open_movie(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, "data/global/video/%s.mpg", name);
    return fs_open(path);
}

int cinema_exists(const char *name)
{
    FILE *f = open_movie(name);
    if (f)
        fclose(f);
    return f != NULL;
}

static plm_t *open_plm(const char *name)
{
    FILE *f = open_movie(name);
    if (!f)
        return NULL;
    plm_buffer_t *b = plm_buffer_create_with_file(f, 1);
    plm_t *plm = b ? plm_create_with_buffer(b, 1) : NULL;
    if (plm && !plm_has_headers(plm)) {
        plm_destroy(plm);
        plm = NULL;
    }
    if (!plm)
        fprintf(stderr, "cinemática %s: no es un MPEG-1 que se pueda leer\n", name);
    return plm;
}

static void show_frame(const plm_frame_t *f)
{
    /* D2_CINEMA_SHOT=archivo.bmp,segundo: ese cuadro como se ve (depuración) */
    static double shot_at = -1.0;
    static char   shot[200];
    if (shot_at < 0.0 && getenv("D2_CINEMA_SHOT") &&
        sscanf(getenv("D2_CINEMA_SHOT"), "%199[^,],%lf", shot, &shot_at) != 2)
        shot_at = 1e9;
    if (shot_at >= 0.0 && f->time >= shot_at) {
        video_movie_shot(shot);
        shot_at = 1e9;
    }
    video_movie_frame(f->y.data, (int)f->y.width, f->cb.data, (int)f->cb.width, f->cr.data, (int)f->cr.width);
}

/* Cuánto sonido se tiene decodificado por delante (segundos) */
#define AUDIO_LEAD 0.3

int cinema_play(const char *name)
{
    plm_t *plm = open_plm(name);
    if (!plm)
        return -1;
    if (video_movie_begin(plm_get_width(plm), plm_get_height(plm)) != 0) {
        plm_destroy(plm);
        return -1;
    }
    mark_seen(name);
    /* El reloj es el sonido que ya salió por la placa: la placa no va
       exactamente a la par del reloj de la máquina, y en los siete minutos
       de la presentación el video se iba despegando. Sin sonido (los
       logos, o sin placa), el reloj de la máquina. */
    int has_sound = plm_get_num_audio_streams(plm) > 0 && plm_get_samplerate(plm) == AUDIO_RATE;
    int real = audio_movie_begin() && has_sound;
    plm_set_audio_enabled(plm, real);
    plm_set_loop(plm, 0);

    InputState in;
    memset(&in, 0, sizeof in);
    Uint64 freq = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    long long pushed = 0;
    int quit = 0, video_done = 0, audio_done = !real;
    plm_frame_t *next = NULL;
    double log_t = 1.0;
    for (;;) {
        input_poll(&in);
        if (in.quit) {
            quit = 1;
            break;
        }
        if (in.confirm || in.start || in.escape || in.menu_back)
            break;
        input_clear_events(&in);

        while (!audio_done && audio_movie_queued() < (int)(AUDIO_LEAD * AUDIO_RATE)) {
            plm_samples_t *smp = plm_decode_audio(plm);
            if (!smp) {
                audio_done = 1;
                break;
            }
            audio_movie_push(smp->interleaved, (int)smp->count);
            pushed += smp->count;
        }
        double clock = real ? (double)(pushed - audio_movie_queued()) / AUDIO_RATE
                            : (double)(SDL_GetPerformanceCounter() - t0) / (double)freq;
        if (!next && !video_done && !(next = plm_decode_video(plm)))
            video_done = 1;
        /* Un cuadro por vuelta; si la máquina quedó atrás, los viejos no se
           muestran (se decodifican igual: los que siguen dependen de ellos) */
        int shown = 0;
        while (next && next->time <= clock) {
            if (next->time + 0.1 >= clock) {
                show_frame(next);
                shown = 1;
            }
            next = NULL;
            if (!shown && !(next = plm_decode_video(plm)))
                video_done = 1;
            if (shown)
                break;
        }
        if (getenv("D2_CINEMA_LOG") && clock >= log_t) {
            log_t = clock + 1.0;
            printf("cinemática: %.1f s, sonido en cola %d cuadros\n", clock, audio_movie_queued());
        }
        if (real && audio_done && audio_movie_queued() == 0) { /* se acabó el sonido: sigue el reloj de la máquina */
            real = 0;
            t0 = SDL_GetPerformanceCounter() - (Uint64)(clock * (double)freq);
        }
        if (video_done && !next && !real)
            break;
        if (!shown)
            SDL_Delay(2);
    }
    audio_movie_end();
    video_movie_end();
    plm_destroy(plm);
    input_clear_events(&in);
    return quit;
}

int cinema_bench(const char *name)
{
    plm_t *plm = open_plm(name);
    if (!plm)
        return -1;
    plm_set_audio_enabled(plm, plm_get_num_audio_streams(plm) > 0);
    Uint64 freq = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    int frames = 0, chunks = 0;
    double vt = 0.0;
    for (plm_frame_t *f; (f = plm_decode_video(plm)) != NULL;) {
        frames++;
        vt = f->time;
        /* el sonido de ese tramo, como al pasarla */
        for (plm_samples_t *s; (s = plm_decode_audio(plm)) != NULL;) {
            chunks++;
            if (s->time > vt)
                break;
        }
    }
    double secs = (double)(SDL_GetPerformanceCounter() - t0) / (double)freq;
    printf("cinemática %s: %dx%d, %d cuadros (%.0f s de video) decodificados en %.1f s: %.1f cuadros por segundo "
           "(hacen falta %.0f); %d pedazos de sonido\n",
           name, plm_get_width(plm), plm_get_height(plm), frames, vt, secs, secs > 0 ? frames / secs : 0.0,
           plm_get_framerate(plm), chunks);
    plm_destroy(plm);
    return 0;
}
