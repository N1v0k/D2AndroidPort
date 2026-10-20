#include "audio.h"

#include "fmt/flac.h"
#include "fmt/formats.h"
#include "fmt/fs.h"
#include "game.h"
#include "jobs.h"

#include <SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VOICES     32
#define SAME_SOUND_MAX 3    /* el mismo sonido sonando a la vez (muertes, pasos...) */
#define MUSIC_FADE     38   /* ticks para apagar la música al cambiar de zona */
#define MUSIC_FADE_IN  12
#define HASH_SIZE      32768
#define MIX_MAX        4096 /* cuadros por llamada del mezclador */
#define LIMIT          30000.0f /* el limitador no deja pasar de acá */
#define SAMPLE_CACHE   (24u << 20) /* sonidos decodificados que se guardan (bytes) */
#define SAMPLE_IDLE    (30 * TICK_RATE) /* uno sin sonar hace menos que esto no se suelta */

typedef struct {
    int16_t *data;
    int      frames, channels, rate;
} Sample;

typedef struct {
    const char *name;
    char        file[96];
    uint8_t     vol_min, vol_max, pitch_min, pitch_max;
    uint8_t     group, loop, fade_in, fade_out;
    uint8_t     music, local, no_pos, defer, stop_prev, none, falloff;
} SoundDef;

typedef struct {
    /* Lo usa el mezclador (se cambia con el dispositivo bloqueado) */
    int           active;
    const Sample *s;
    uint64_t      pos, step; /* cuadro en 32.32 */
    int           loop;
    float         gl, gr;    /* ganancia actual */
    float         tl, tr;    /* ganancia a la que va en la próxima mezcla */
    int           kill;      /* se apaga cuando llega a 0 */
    /* Lo usa el juego */
    int      group, def, handle;
    float    x, y, vol, env, env_step;
    unsigned age;
} Voice;

static int        s_mode;
static TxtTable   s_table, s_envs;
static SoundDef  *s_defs;
static int        s_num_defs;
static Sample   **s_samples;
static uint8_t   *s_sample_busy; /* decodificándose en el hilo de trabajos */
static uint32_t  *s_sample_used; /* tick de audio en que sonó por última vez */
static uint32_t   s_clock;       /* ticks de audio_tick */
static SDL_mutex *s_sample_mx;
static Sample     s_missing;
static int       *s_hash;
static SDL_AudioDeviceID s_dev;

static Voice    s_voices[MAX_VOICES];
static int      s_next_handle = 1;
static unsigned s_age;
static uint32_t s_rng = 0x2545F491u;

static int   s_num_listeners;
static float s_lx[4], s_ly[4];
static int   s_sfx_volume = 7, s_music_volume = 6;

/* Lo que se lee de a pedazos mientras suena: la música y la voz de los NPC.
   El mezclador usa f, loop, gain y target; audio_tick maneja el resto. */
typedef struct {
    Flac *f;
    int   loop, ended;
    float gain, target;
} Stream;
static Stream s_music_st, s_voice_st;
static int    s_music_cur = -1, s_music_want = -1, s_music_area = -1;
static float s_music_env;

/* Ambiente de la zona */
static int s_env;
static int s_scene_handle, s_event_sound = -1, s_event_delay, s_event_timer;

static float   s_limit_gain = 1.0f;
static int     s_log; /* D2_AUDIO_LOG: muestra cada sonido que arranca */
static int     s_loaded;
static size_t  s_loaded_bytes;

static int16_t s_music_buf[MIX_MAX * 2];
static float   s_acc[MIX_MAX * 2];

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void lock(void)
{
    if (s_dev)
        SDL_LockAudioDevice(s_dev);
}

static void unlock(void)
{
    if (s_dev)
        SDL_UnlockAudioDevice(s_dev);
}

/* ---- Mezclador (corre en el hilo de audio) ---- */

static void mix_voice(Voice *v, float *acc, int frames)
{
    const Sample *s = v->s;
    const int16_t *d = s->data;
    uint64_t len = (uint64_t)s->frames << 32;
    float dl = (v->tl - v->gl) / (float)frames, dr = (v->tr - v->gr) / (float)frames;
    float gl = v->gl, gr = v->gr;
    int i = 0;
    for (; i < frames; i++) {
        if (v->pos >= len) {
            if (!v->loop) {
                v->active = 0;
                break;
            }
            v->pos -= len;
        }
        uint32_t idx = (uint32_t)(v->pos >> 32);
        uint32_t next = idx + 1 < (uint32_t)s->frames ? idx + 1 : v->loop ? 0 : idx;
        float fr = (float)(uint32_t)v->pos * (1.0f / 4294967296.0f);
        if (s->channels == 1) {
            float a = d[idx], x = a + ((float)d[next] - a) * fr;
            acc[2 * i] += x * gl;
            acc[2 * i + 1] += x * gr;
        } else {
            float a = d[2 * idx], b = d[2 * idx + 1];
            acc[2 * i] += (a + ((float)d[2 * next] - a) * fr) * gl;
            acc[2 * i + 1] += (b + ((float)d[2 * next + 1] - b) * fr) * gr;
        }
        gl += dl;
        gr += dr;
        v->pos += v->step;
    }
    v->gl = v->tl;
    v->gr = v->tr;
    if (v->kill && v->tl <= 0.0f && v->tr <= 0.0f)
        v->active = 0;
}

static void mix_stream(Stream *st, float *acc, int frames)
{
    if (!st->f) {
        st->gain = 0.0f;
        return;
    }
    if (st->gain <= 0.0f && st->target <= 0.0f)
        return; /* en volumen 0 no se decodifica (sigue donde quedó) */
    int ch = flac_channels(st->f), got = 0;
    while (got < frames) {
        int n = flac_read(st->f, s_music_buf + got * ch, frames - got);
        if (n > 0) {
            got += n;
            continue;
        }
        if (!st->loop || flac_rewind(st->f) != 0) {
            st->ended = 1;
            break;
        }
    }
    float g = st->gain, dg = (st->target - st->gain) / (float)frames;
    for (int i = 0; i < got; i++, g += dg) {
        float l = s_music_buf[i * ch], r = ch > 1 ? s_music_buf[i * ch + 1] : l;
        acc[2 * i] += l * g;
        acc[2 * i + 1] += r * g;
    }
    st->gain = st->target;
}

static void mix(int16_t *out, int frames)
{
    while (frames > 0) {
        int n = frames > MIX_MAX ? MIX_MAX : frames;
        memset(s_acc, 0, sizeof(float) * 2 * (size_t)n);
        for (int i = 0; i < MAX_VOICES; i++)
            if (s_voices[i].active)
                mix_voice(&s_voices[i], s_acc, n);
        mix_stream(&s_music_st, s_acc, n);
        mix_stream(&s_voice_st, s_acc, n);

        /* Limitador: si muchos sonidos juntos se pasarían del máximo, baja el
           volumen de todo (de golpe, mirando el pedazo entero) y lo vuelve a
           subir de a poco */
        float peak = 0.0f;
        for (int i = 0; i < 2 * n; i++)
            peak = fmaxf(peak, fabsf(s_acc[i]));
        float target = peak > LIMIT ? LIMIT / peak : 1.0f;
        float from = s_limit_gain, to = target < from ? target : from + (1.0f - from) * 0.05f * n / 1024.0f;
        if (to > 1.0f)
            to = 1.0f;
        for (int i = 0; i < n; i++) {
            float g = i < 64 ? from + (to - from) * (float)i / 64.0f : to;
            if (g > target) /* el principio del fundido no puede pasarse */
                g = target;
            for (int c = 0; c < 2; c++) {
                float x = s_acc[2 * i + c] * g;
                out[2 * i + c] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)x);
            }
        }
        s_limit_gain = to;
        out += 2 * n;
        frames -= n;
    }
}

static void callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    mix((int16_t *)stream, len / 4);
}

void audio_render(int16_t *out, int frames)
{
    if (s_mode == AUDIO_MANUAL)
        mix(out, frames);
    else
        memset(out, 0, (size_t)frames * 4);
}

/* ---- Tabla de sonidos ---- */

static uint32_t hash_name(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint32_t)tolower((unsigned char)*s)) * 16777619u;
    return h;
}

int audio_find(const char *name)
{
    if (!s_hash || !name || !name[0])
        return -1;
    for (uint32_t h = hash_name(name) & (HASH_SIZE - 1);; h = (h + 1) & (HASH_SIZE - 1)) {
        int i = s_hash[h];
        if (i < 0)
            return -1;
        if (strcasecmp(s_defs[i].name, name) == 0)
            return i;
    }
}

static int load_table(TxtTable *t, const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "data/global/excel/%s.txt", name);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int r = d ? txt_load(d, n, t) : -1;
    free(d);
    if (r != 0)
        fprintf(stderr, "falta %s\n", path);
    return r;
}

static int load_sounds(void)
{
    if (load_table(&s_table, "sounds") != 0)
        return -1;
    load_table(&s_envs, "soundenviron");
    int rows = s_table.rows - 1;
    s_defs = calloc((size_t)rows, sizeof *s_defs);
    s_samples = calloc((size_t)rows, sizeof *s_samples);
    s_sample_busy = calloc((size_t)rows, 1);
    s_sample_used = calloc((size_t)rows, sizeof *s_sample_used);
    if (!s_sample_mx)
        s_sample_mx = SDL_CreateMutex();
    s_hash = malloc(sizeof(int) * HASH_SIZE);
    if (!s_defs || !s_samples || !s_sample_busy || !s_sample_used || !s_hash || rows * 2 > HASH_SIZE)
        return -1;
    for (int i = 0; i < HASH_SIZE; i++)
        s_hash[i] = -1;

    const TxtTable *t = &s_table;
    int c_name = txt_col(t, "Sound"), c_file = txt_col(t, "FileName");
    for (int i = 0; i < rows; i++) {
        int r = i + 1;
        SoundDef *d = &s_defs[i];
        d->name = txt_get(t, r, c_name);
        const char *file = txt_get(t, r, c_file);
        d->music = txt_get_int(t, r, "IsMusic", 0) == 1;
        d->local = txt_get_int(t, r, "IsLocal", 0) == 1;
        d->none = !file[0] || strcasecmp(file, "none.flac") == 0;
        snprintf(d->file, sizeof d->file, "data/%s/%s/%s", d->local ? "local" : "global",
                 d->music ? "music" : "sfx", file);
        d->vol_min = (uint8_t)txt_get_int(t, r, "Volume Min", 255);
        d->vol_max = (uint8_t)txt_get_int(t, r, "Volume Max", d->vol_min);
        d->pitch_min = (uint8_t)txt_get_int(t, r, "Pitch Min", 100);
        d->pitch_max = (uint8_t)txt_get_int(t, r, "Pitch Max", d->pitch_min);
        d->group = (uint8_t)txt_get_int(t, r, "Group Size", 0);
        d->loop = txt_get_int(t, r, "Loop", 0) == 1;
        d->fade_in = (uint8_t)txt_get_int(t, r, "Fade In", 0);
        d->fade_out = (uint8_t)txt_get_int(t, r, "Fade Out", 0);
        d->defer = txt_get_int(t, r, "Defer Inst", 0) == 1;
        d->stop_prev = txt_get_int(t, r, "Stop Inst", 0) == 1;
        d->falloff = (uint8_t)txt_get_int(t, r, "Falloff", 1);
        d->no_pos = d->music || txt_get_int(t, r, "Is2D", 0) == 1 || txt_get_int(t, r, "IsUI", 0) == 1;
        if (!d->name[0])
            continue;
        uint32_t h = hash_name(d->name) & (HASH_SIZE - 1);
        while (s_hash[h] >= 0)
            h = (h + 1) & (HASH_SIZE - 1);
        s_hash[h] = i;
    }
    s_num_defs = rows;
    return 0;
}

/* El sonido decodificado (la primera vez lo decodifica). Lo puede llamar
   también el hilo de trabajos: si el otro lo está decodificando, devuelve
   NULL (esa vez no suena) en vez de esperarlo. */
static Sample *get_sample(int i)
{
    SDL_LockMutex(s_sample_mx);
    Sample *have = s_samples[i];
    if (have || s_sample_busy[i]) {
        SDL_UnlockMutex(s_sample_mx);
        return have == &s_missing ? NULL : have;
    }
    s_sample_busy[i] = 1;
    SDL_UnlockMutex(s_sample_mx);

    const SoundDef *d = &s_defs[i];
    Sample *s = NULL;
    size_t size;
    uint8_t *data = d->none || d->music ? NULL : fs_read(d->file, &size);
    if (!data && !d->none && !d->music && s_log)
        printf("  falta el sonido %s (%s)\n", d->name, d->file);
    if (data) {
        s = calloc(1, sizeof *s);
        if (s)
            s->data = flac_decode(data, size, &s->frames, &s->channels, &s->rate);
        free(data);
        if (s && (!s->data || s->frames <= 0 || s->channels > 2)) {
            free(s->data);
            free(s);
            s = NULL;
        }
    }

    SDL_LockMutex(s_sample_mx);
    s_samples[i] = s ? s : &s_missing;
    s_sample_busy[i] = 0;
    if (s) {
        s_loaded++;
        s_loaded_bytes += (size_t)s->frames * (size_t)s->channels * 2;
    }
    SDL_UnlockMutex(s_sample_mx);
    return s;
}

static void preload_job(void *ptr, int i)
{
    (void)ptr;
    get_sample(i);
}

void audio_preload(int sound)
{
    if (s_mode == AUDIO_OFF || sound < 0 || sound >= s_num_defs)
        return;
    int n = s_defs[sound].group > 1 ? s_defs[sound].group : 1;
    for (int i = sound; i < sound + n && i < s_num_defs; i++) {
        s_sample_used[i] = s_clock; /* recién pedido: que el tope no lo suelte enseguida */
        if (!s_samples[i])
            jobs_push(preload_job, NULL, i); /* en otro hilo si anda (jobs.h) */
    }
}

int audio_loops(int sound)
{
    return sound >= 0 && sound < s_num_defs && s_defs[sound].loop;
}

/* ---- Inicio ---- */

int audio_init(int mode)
{
    s_mode = AUDIO_OFF;
    if (mode == AUDIO_OFF)
        return 0;
    s_log = getenv("D2_AUDIO_LOG") != NULL;
    if (load_sounds() != 0) {
        fprintf(stderr, "sin sonido: no pude cargar sounds.txt\n");
        return -1;
    }
    if (mode == AUDIO_DEVICE) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "sin sonido: %s\n", SDL_GetError());
            return -1;
        }
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof want);
        want.freq = AUDIO_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 2048;
        want.callback = callback;
        s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!s_dev) {
            fprintf(stderr, "sin sonido: %s\n", SDL_GetError());
            return -1;
        }
        SDL_PauseAudioDevice(s_dev, 0);
    }
    s_mode = mode;
    return 0;
}

void audio_shutdown(void)
{
    if (s_log)
        printf("sonidos decodificados: %d (%.1f MB)\n", s_loaded, s_loaded_bytes / 1048576.0);
    if (s_dev) {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
    flac_close(s_music_st.f);
    flac_close(s_voice_st.f);
    s_music_st.f = s_voice_st.f = NULL;
    for (int i = 0; i < s_num_defs; i++)
        if (s_samples[i] && s_samples[i] != &s_missing) {
            free(s_samples[i]->data);
            free(s_samples[i]);
        }
    free(s_samples);
    free(s_sample_busy);
    s_sample_busy = NULL;
    free(s_sample_used);
    s_sample_used = NULL;
    free(s_defs);
    free(s_hash);
    s_samples = NULL;
    s_defs = NULL;
    s_hash = NULL;
    s_num_defs = 0;
    txt_free(&s_table);
    txt_free(&s_envs);
    s_mode = AUDIO_OFF;
}

/* ---- Efectos ---- */

/* Volumen de cada lado según la distancia al jugador más cercano y de
   qué lado de la pantalla está */
static void spatial(const Voice *v, float *l, float *r)
{
    const SoundDef *d = &s_defs[v->def];
    float g = v->vol * v->env * (float)s_sfx_volume / 10.0f;
    if (v->x == AUDIO_NOPOS || d->no_pos || s_num_listeners == 0) {
        *l = *r = g;
        return;
    }
    static const float k_near[4] = { 3.0f, 6.0f, 10.0f, 20.0f };
    static const float k_far[4] = { 20.0f, 34.0f, 48.0f, 80.0f };
    int f = d->falloff < 4 ? d->falloff : 3;
    float best = 0.0f, pan = 0.0f;
    for (int i = 0; i < s_num_listeners; i++) {
        float dx = v->x - s_lx[i], dy = v->y - s_ly[i], dist = sqrtf(dx * dx + dy * dy);
        float a = dist <= k_near[f] ? 1.0f : dist >= k_far[f] ? 0.0f : (k_far[f] - dist) / (k_far[f] - k_near[f]);
        a *= a;
        if (a > best) {
            best = a;
            pan = (dx - dy) * 16.0f / 400.0f; /* posición en la pantalla, de -1 a 1 */
            pan = pan > 1.0f ? 1.0f : pan < -1.0f ? -1.0f : pan;
            pan *= 0.7f;
        }
    }
    *l = g * best * (pan > 0.0f ? 1.0f - pan : 1.0f);
    *r = g * best * (pan < 0.0f ? 1.0f + pan : 1.0f);
}

static Voice *find_handle(int handle)
{
    if (handle <= 0)
        return NULL;
    for (int i = 0; i < MAX_VOICES; i++)
        if (s_voices[i].active && s_voices[i].handle == handle)
            return &s_voices[i];
    return NULL;
}

int audio_play_vol(int sound, float x, float y, int volume)
{
    if (s_mode == AUDIO_OFF || sound < 0 || sound >= s_num_defs)
        return 0;
    const SoundDef *first = &s_defs[sound];
    int pick = sound;
    if (first->group > 1) {
        int n = first->group;
        if (sound + n > s_num_defs)
            n = s_num_defs - sound;
        pick = sound + (int)(rnd() % (uint32_t)n);
    }
    const SoundDef *d = &s_defs[pick];
    const Sample *smp = get_sample(pick);
    if (!smp)
        return 0;
    s_sample_used[pick] = s_clock;

    /* Cuántos de este mismo sonido están sonando */
    int same = 0, oldest = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &s_voices[i];
        if (!v->active || v->group != sound || v->kill)
            continue;
        same++;
        if (oldest < 0 || v->age < s_voices[oldest].age)
            oldest = i;
    }
    if (same > 0 && first->defer)
        return 0;

    Voice nv;
    memset(&nv, 0, sizeof nv);
    nv.active = 1;
    nv.s = smp;
    nv.loop = d->loop;
    nv.group = sound;
    nv.def = pick;
    nv.x = x;
    nv.y = y;
    int vol = volume >= 0 ? volume
                          : d->vol_min + (d->vol_max > d->vol_min ? (int)(rnd() % (uint32_t)(d->vol_max - d->vol_min + 1)) : 0);
    nv.vol = (float)vol / 255.0f;
    nv.env = d->fade_in ? 0.0f : 1.0f;
    nv.env_step = d->fade_in ? 1.0f / d->fade_in : 0.0f;
    int pitch = d->pitch_min + (d->pitch_max > d->pitch_min ? (int)(rnd() % (uint32_t)(d->pitch_max - d->pitch_min + 1)) : 0);
    nv.step = (((uint64_t)smp->rate << 32) / AUDIO_RATE) * (uint64_t)(pitch > 0 ? pitch : 100) / 100;
    spatial(&nv, &nv.tl, &nv.tr);
    if (!nv.loop && nv.tl <= 0.0f && nv.tr <= 0.0f)
        return 0; /* muy lejos: no se oye */
    nv.gl = nv.tl;
    nv.gr = nv.tr;
    if (s_log)
        printf("  sonido %s (%s) vol %.2f/%.2f\n", first->name, d->file, nv.tl, nv.tr);
    nv.age = ++s_age;
    nv.handle = s_next_handle++;
    if (s_next_handle <= 0)
        s_next_handle = 1;

    lock();
    if (first->stop_prev) { /* corta el anterior */
        for (int i = 0; i < MAX_VOICES; i++)
            if (s_voices[i].active && s_voices[i].group == sound) {
                s_voices[i].tl = s_voices[i].tr = 0.0f;
                s_voices[i].kill = 1;
            }
    }
    int slot = -1;
    if (same >= SAME_SOUND_MAX && !first->stop_prev)
        slot = oldest; /* reemplaza al más viejo del mismo sonido */
    for (int i = 0; slot < 0 && i < MAX_VOICES; i++)
        if (!s_voices[i].active)
            slot = i;
    if (slot < 0) { /* todas ocupadas: el más viejo que no se repite solo */
        for (int i = 0; i < MAX_VOICES; i++)
            if (!s_voices[i].loop && (slot < 0 || s_voices[i].age < s_voices[slot].age))
                slot = i;
    }
    if (slot >= 0)
        s_voices[slot] = nv;
    unlock();
    return slot >= 0 ? nv.handle : 0;
}

int audio_play(int sound, float x, float y)
{
    return audio_play_vol(sound, x, y, -1);
}

int audio_play_name(const char *name, float x, float y)
{
    return audio_play(audio_find(name), x, y);
}

void audio_stop(int handle)
{
    Voice *v = find_handle(handle);
    if (!v)
        return;
    const SoundDef *d = &s_defs[v->def];
    lock();
    if (d->fade_out) {
        v->env_step = -1.0f / d->fade_out;
    } else {
        v->tl = v->tr = 0.0f;
        v->kill = 1;
    }
    unlock();
}

void audio_stop_all(void)
{
    lock();
    for (int i = 0; i < MAX_VOICES; i++)
        if (s_voices[i].active) {
            s_voices[i].tl = s_voices[i].tr = 0.0f;
            s_voices[i].kill = 1;
        }
    unlock();
    s_scene_handle = 0;
}

void audio_move(int handle, float x, float y)
{
    Voice *v = find_handle(handle);
    if (v) {
        v->x = x;
        v->y = y;
    }
}

void audio_listeners(int n, const float *x, const float *y)
{
    s_num_listeners = n < 4 ? n : 4;
    for (int i = 0; i < s_num_listeners; i++) {
        s_lx[i] = x[i];
        s_ly[i] = y[i];
    }
}

void audio_set_volume(int sfx, int music)
{
    s_sfx_volume = sfx < 0 ? 0 : sfx > 10 ? 10 : sfx;
    s_music_volume = music < 0 ? 0 : music > 10 ? 10 : music;
}

void audio_get_volume(int *sfx, int *music)
{
    *sfx = s_sfx_volume;
    *music = s_music_volume;
}

/* ---- Música y ambiente ---- */

void audio_music(const char *name)
{
    if (s_mode == AUDIO_OFF)
        return;
    s_music_area = name ? audio_find(name) : -1;
    s_music_want = s_music_area;
}

void audio_music_event(const char *name)
{
    if (s_mode == AUDIO_OFF)
        return;
    int m = audio_find(name);
    if (m >= 0)
        s_music_want = m;
}

static int env_row(int env)
{
    char idx[12];
    snprintf(idx, sizeof idx, "%d", env);
    return txt_find_row(&s_envs, txt_col(&s_envs, "Index"), idx);
}

static const char *env_get(int env, const char *col)
{
    return txt_get(&s_envs, env_row(env), txt_col(&s_envs, col));
}

void audio_environment(int env)
{
    if (s_mode == AUDIO_OFF || env == s_env)
        return;
    s_env = env;
    audio_stop(s_scene_handle);
    s_scene_handle = 0;
    s_event_sound = -1;
    if (env <= 0 || env_row(env) < 0) {
        audio_music(NULL);
        return;
    }
    const char *song = env_get(env, "Song");
    audio_music(song[0] ? song : NULL);
    s_scene_handle = audio_play_name(env_get(env, "Day Ambience"), AUDIO_NOPOS, AUDIO_NOPOS);
    s_event_sound = audio_find(env_get(env, "Day Event"));
    /* El del pueblo solo está en alta definición: van los pájaros del campo */
    if (s_event_sound >= 0 && !get_sample(s_event_sound))
        s_event_sound = audio_find("event_wild_day_1");
    s_event_delay = atoi(env_get(env, "Event Delay"));
    if (s_event_delay < 25)
        s_event_delay = 25;
    s_event_timer = s_event_delay / 2 + (int)(rnd() % (uint32_t)s_event_delay);
}

static void music_tick(void)
{
    int ended;
    lock();
    ended = s_music_st.ended;
    s_music_st.ended = 0;
    unlock();
    if (ended && s_music_cur == s_music_want) {
        /* Terminó un tema que no se repite: vuelve la música de la zona */
        s_music_want = s_music_area != s_music_cur ? s_music_area : -1;
        s_music_env = 0.0f;
    }

    if (s_music_want != s_music_cur) {
        if (s_music_st.f && s_music_env > 0.0f) {
            s_music_env -= 1.0f / MUSIC_FADE;
        } else {
            lock();
            Flac *old = s_music_st.f;
            s_music_st.f = NULL;
            unlock();
            flac_close(old);
            s_music_cur = s_music_want;
            if (s_log)
                printf("  música: %s\n", s_music_cur >= 0 ? s_defs[s_music_cur].name : "(nada)");
            if (s_music_cur >= 0) {
                Flac *f = flac_open(s_defs[s_music_cur].file);
                if (!f)
                    fprintf(stderr, "falta la música %s\n", s_defs[s_music_cur].file);
                lock();
                s_music_st.f = f;
                s_music_st.loop = s_defs[s_music_cur].loop;
                s_music_st.ended = 0;
                s_music_st.gain = 0.0f;
                unlock();
            }
            s_music_env = 0.0f;
        }
    } else if (s_music_st.f && s_music_env < 1.0f) {
        s_music_env += 1.0f / MUSIC_FADE_IN;
    }
    if (s_music_env < 0.0f) s_music_env = 0.0f;
    if (s_music_env > 1.0f) s_music_env = 1.0f;
    float target = 0.0f;
    if (s_music_cur >= 0)
        target = s_defs[s_music_cur].vol_max / 255.0f * (float)s_music_volume / 10.0f * s_music_env;
    lock();
    s_music_st.target = target;
    unlock();
}

int audio_voice(const char *path)
{
    if (s_mode == AUDIO_OFF || !path)
        return 0;
    Flac *f = flac_open(path);
    if (!f) {
        if (s_log)
            printf("  falta la voz %s\n", path);
        return 0;
    }
    int ticks = flac_rate(f) ? (int)(flac_total(f) * TICK_RATE / (uint64_t)flac_rate(f)) : 0;
    if (s_log)
        printf("  voz: %s (%.1f s)\n", path, ticks / (float)TICK_RATE);
    lock();
    Flac *old = s_voice_st.f;
    s_voice_st.f = f;
    s_voice_st.loop = 0;
    s_voice_st.ended = 0;
    s_voice_st.gain = s_voice_st.target = (float)s_sfx_volume / 10.0f;
    unlock();
    flac_close(old);
    return ticks;
}

void audio_voice_stop(void)
{
    lock();
    Flac *old = s_voice_st.f;
    s_voice_st.f = NULL;
    unlock();
    flac_close(old);
}

/* Si los sonidos decodificados pasan de SAMPLE_CACHE, se sueltan los que
   hace más que no suenan (y no están sonando): se vuelven a decodificar si
   hacen falta. Corre en el hilo del juego, el mismo que los hace sonar. */
static void trim_samples(void)
{
    if (s_loaded_bytes <= SAMPLE_CACHE)
        return;
    const Sample *playing[MAX_VOICES];
    int np = 0;
    lock();
    for (int i = 0; i < MAX_VOICES; i++)
        if (s_voices[i].active)
            playing[np++] = s_voices[i].s;
    unlock();
    while (s_loaded_bytes > SAMPLE_CACHE * 3 / 4) {
        int oldest = -1;
        SDL_LockMutex(s_sample_mx);
        for (int i = 0; i < s_num_defs; i++) {
            Sample *s = s_samples[i];
            if (!s || s == &s_missing || s_sample_busy[i] || s_clock - s_sample_used[i] < SAMPLE_IDLE)
                continue;
            int busy = 0;
            for (int k = 0; k < np && !busy; k++)
                busy = playing[k] == s;
            if (!busy && (oldest < 0 || s_sample_used[i] < s_sample_used[oldest]))
                oldest = i;
        }
        Sample *s = oldest >= 0 ? s_samples[oldest] : NULL;
        if (s) {
            s_samples[oldest] = NULL;
            s_loaded--;
            s_loaded_bytes -= (size_t)s->frames * (size_t)s->channels * 2;
        }
        SDL_UnlockMutex(s_sample_mx);
        if (!s)
            break;
        free(s->data);
        free(s);
    }
}

size_t audio_memory(void)
{
    return s_loaded_bytes;
}

void audio_tick(void)
{
    if (s_mode == AUDIO_OFF)
        return;
    music_tick();
    if (++s_clock % TICK_RATE == 0)
        trim_samples();

    /* Voz: sigue el volumen de los efectos; al terminar se cierra */
    lock();
    int voice_done = s_voice_st.f && s_voice_st.ended;
    s_voice_st.target = (float)s_sfx_volume / 10.0f;
    unlock();
    if (voice_done)
        audio_voice_stop();

    /* Ruidos sueltos del ambiente (pájaros, viento, goteras) cerca de un jugador */
    if (s_event_sound >= 0 && s_num_listeners > 0 && --s_event_timer <= 0) {
        int who = (int)(rnd() % (uint32_t)s_num_listeners);
        float a = (float)(rnd() % 628) / 100.0f, r = 4.0f + (float)(rnd() % 1000) / 100.0f;
        audio_play(s_event_sound, s_lx[who] + cosf(a) * r, s_ly[who] + sinf(a) * r);
        s_event_timer = s_event_delay / 2 + (int)(rnd() % (uint32_t)s_event_delay);
    }

    /* Fundidos y volumen según dónde está cada uno */
    float tl[MAX_VOICES], tr[MAX_VOICES];
    int   kill[MAX_VOICES];
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &s_voices[i];
        kill[i] = 0;
        if (!v->active || v->kill)
            continue;
        v->env += v->env_step;
        if (v->env >= 1.0f) {
            v->env = 1.0f;
            v->env_step = 0.0f;
        } else if (v->env <= 0.0f && v->env_step < 0.0f) {
            v->env = 0.0f;
            kill[i] = 1;
        }
        spatial(v, &tl[i], &tr[i]);
    }
    lock();
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &s_voices[i];
        if (!v->active || v->kill)
            continue;
        v->tl = tl[i];
        v->tr = tr[i];
        v->kill = kill[i];
    }
    unlock();
}
