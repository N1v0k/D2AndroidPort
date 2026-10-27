#include "audio.h"
#include "bot.h"
#include "cinema.h"
#include "fmt/fs.h"
#include "charanim.h"
#include "compat.h"
#include "font.h"
#include "game.h"
#include "input.h"
#include "ui.h"
#include "jobs.h"
#include "lang.h"
#include "map.h"
#include "menu.h"
#include "net.h"
#include "touch.h"
#include "units.h"
#include "video.h"

#include <SDL.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <math.h>
#include "dmath.h" /* (sinf, atan2f... iguales en todas las máquinas: la red) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

/* Si el juego se cae, deja en el log (en la Pi, d2port.log) dónde fue: las
   direcciones salen como "d2port(+0x1234)" y se traducen con
   addr2line -f -e d2port 0x1234 */
static void crash_handler(int sig)
{
    static const char msg[] = "\nel juego se cayó (señal ";
    void *frames[48];
    char num[4] = { (char)('0' + sig / 10 % 10), (char)('0' + sig % 10), ')', '\n' };
    if (write(2, msg, sizeof msg - 1) < 0 || write(2, num, sizeof num) < 0)
        _exit(128 + sig);
    int n = backtrace(frames, 48);
    backtrace_symbols_fd(frames, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void crash_handler_install(void)
{
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGILL, crash_handler);
}
#else
static void crash_handler_install(void)
{
}
#endif

typedef struct {
    int         headless;
    int         fullscreen;
    int         scale;
    int         frames;
    int         players; /* 0 = mostrar el menú */
    int         demo;
    float       demo_angle;
    int         sprite_test;
    int         bench;
    const char *ds1_info;
    const char *autotest; /* "act1": un bot juega el acto y muestra un informe */
    const char *screenshot;
} Options;

typedef enum { ST_MENU, ST_GAME, ST_DEMO } AppState;

/* Segundos con el menú principal quieto hasta que arranca la demostración
   (D2_DEMO_SECONDS; 0 = nunca) */
static int demo_seconds(void)
{
    const char *s = getenv("D2_DEMO_SECONDS");
    return s ? atoi(s) : 10;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "uso: %s [--fullscreen] [--scale N] [--players 1|2] [--demo [grados]]\n"
            "          [--frames N] [--screenshot archivo.bmp] [--headless]\n"
            "  --players N  saltea el menú y arranca con N jugadores\n"
            "  --demo       los personajes caminan solos (con 2, en direcciones opuestas)\n"
            "  --headless   simula N ticks sin ventana (para pruebas)\n",
            argv0);
}

static int parse_args(int argc, char **argv, Options *o)
{
    memset(o, 0, sizeof *o);
    o->scale = 0; /* (sin --scale: en Windows, la ventana más grande que entra; si no, 800x600) */
    o->demo_angle = 20.0f;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            int fails = map_selftest() + bot_selftest();
            printf("prueba de la colisión: %s\n", fails ? "FALLA" : "BIEN");
            exit(fails ? 1 : 0);
        }
        if (!strcmp(argv[i], "--headless")) o->headless = 1;
        else if (!strcmp(argv[i], "--sprite-test")) o->sprite_test = 1;
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) o->bench = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ds1-info") && i + 1 < argc) o->ds1_info = argv[++i];
        else if (!strcmp(argv[i], "--fullscreen")) o->fullscreen = 1;
        else if (!strcmp(argv[i], "--demo")) {
            o->demo = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                o->demo_angle = (float)atof(argv[++i]);
        }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) o->scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--players") && i + 1 < argc) o->players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) o->screenshot = argv[++i];
        else if (!strcmp(argv[i], "--autotest") && i + 1 < argc) o->autotest = argv[++i];
        else {
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

/* Con 2 jugadores caminan en direcciones opuestas (el 1 corriendo); con uno
   solo, va y viene corriendo para probar la estamina. */
static void demo_input(InputState *in, const Options *o, int tick)
{
    if (o->players >= 2) {
        float a = o->demo_angle * 3.14159265f / 180.0f;
        in->player[0].move_x = -cosf(a);
        in->player[0].move_y = -sinf(a);
        in->player[1].move_x = cosf(a);
        in->player[1].move_y = sinf(a);
    } else {
        in->player[0].move_x = (tick / 40) % 2 ? 1.0f : -1.0f;
        in->player[0].move_y = 0.0f;
        /* D2_WALK="x,y": caminar siempre en esa dirección de pantalla */
        const char *walk = getenv("D2_WALK");
        if (walk)
            sscanf(walk, "%f,%f", &in->player[0].move_x, &in->player[0].move_y);
    }
    in->player[0].sprint = 1;
    in->player[1].sprint = 0;

    /* D2_INV_SCRIPT="ddda": una tecla cada 5 ticks para el jugador 1, como
       los botones del joystick (u/d/l/r = cruceta, a/b/x/y = botones,
       i = inventario, t = árbol de habilidades, g = atributos, o = LB, c = R3 (frases), . = nada) */
    const char *script = getenv("D2_INV_SCRIPT");
    if (script) {
        in->player[0].move_x = in->player[0].move_y = 0.0f;
        in->player[0].nav_up = in->player[0].nav_down = in->player[0].nav_left = 0;
        in->player[0].nav_right = in->player[0].action = in->player[0].drop = in->player[0].inventory = 0;
        in->player[0].back = in->player[0].skill_tree = in->player[0].stats_page = in->player[0].potion_life = in->player[0].chat = 0;
        memset(in->player[0].button, 0, sizeof in->player[0].button);
        if (tick % 5 == 0 && (size_t)(tick / 5) < strlen(script)) {
            char c = script[tick / 5];
            in->player[0].nav_up = c == 'u';
            in->player[0].nav_down = c == 'd';
            in->player[0].nav_left = c == 'l';
            in->player[0].nav_right = c == 'r';
            in->player[0].action = in->player[0].button[0] = c == 'a';
            in->player[0].drop = in->player[0].button[2] = c == 'x';
            in->player[0].back = in->player[0].button[1] = c == 'b';
            in->player[0].button[3] = c == 'y';
            in->player[0].inventory = c == 'i';
            in->player[0].skill_tree = c == 't';
            in->player[0].stats_page = c == 'g';
            in->player[0].potion_life = c == 'o'; /* LB: ordenar en el inventario */
            in->player[0].chat = c == 'c';
        }
        return;
    }

    /* D2_ACTION: apretar acción cada 6 ticks sin dejar de caminar */
    if (getenv("D2_ACTION"))
        in->player[0].action = tick % 6 == 0;

    /* D2_HOLD=a|b|x|y: quedarse quieto, apretar ese botón una vez (tick 10) y mantenerlo (la
       repetición); D2_HOLD_UNTIL=n, soltarlo en el tick n */
    if (getenv("D2_HOLD")) {
        char c = getenv("D2_HOLD")[0];
        int b = c == 'b' ? 1 : c == 'x' ? 2 : c == 'y' ? 3 : 0;
        int until = getenv("D2_HOLD_UNTIL") ? atoi(getenv("D2_HOLD_UNTIL")) : 1 << 30;
        in->player[0].move_x = in->player[0].move_y = 0.0f;
        in->player[0].button[b] = tick == 10;
        in->player[0].action = b == 0 && tick == 10;
        in->player[0].held[b] = tick >= 10 && tick < until;
        return;
    }

    /* D2_ATTACK: quedarse quieto y atacar cada medio segundo. Con
       D2_ATTACK=b, x o y usa ese botón en vez del A. */
    if (getenv("D2_ATTACK")) {
        char c = getenv("D2_ATTACK")[0];
        int b = c == 'b' ? 1 : c == 'x' ? 2 : c == 'y' ? 3 : 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            in->player[i].move_x = in->player[i].move_y = 0.0f;
            in->player[i].button[b] = tick % 12 == 0;
            in->player[i].action = b == 0 && tick % 12 == 0;
        }
    }
}

/* Un tick en red: manda los controles de los de esta máquina para el tick t + la demora y, si ya
   están los de todos para t, lo juega. bots: los controles salen del bot de la prueba (todos en
   in->player, por su número en la partida); si no, de los joysticks. 0 = esperando, 1 = jugó,
   2 = se terminó (salir o se cortó la red). wait: esperar hasta tenerlos (las pruebas sin ventana). */
/* D2_MONKEY=semilla (pruebas, con --autotest): además de los bots, de vez en cuando se aprieta algo al
   azar (los paneles, el cinturón, tirar, los menús, la pausa, el mouse, caminar para un lado) para
   encontrar caídas en lo que los bots no tocan; D2_MONKEY_RATE=n: una cosa cada n ticks (8). Con su
   propio sorteo: no toca el del juego */
static void monkey_input(InputState *in, int players, int tick)
{
    static uint32_t s;
    static int rate, hold, who, clicked, tidy = -1;
    static float mx, my;
    if (!s) {
        s = (uint32_t)atoi(getenv("D2_MONKEY")) * 2654435761u | 1u;
        rate = getenv("D2_MONKEY_RATE") ? atoi(getenv("D2_MONKEY_RATE")) : 8;
        rate = rate < 1 ? 1 : rate;
    }
#define MONKEY_RND() (s ^= s << 13, s ^= s >> 17, s ^= s << 5, s)
    if (clicked) /* (el clic dura un tick) */
        in->mouse_down = clicked = 0;
    if (hold > 0) {
        hold--;
        in->player[who].move_x = mx;
        in->player[who].move_y = my;
    }
    if (tick == tidy) /* (al rato cierra lo que abrió, como cualquiera: si no, el bot no sigue) */
        game_autotest_close_ui();
    if (MONKEY_RND() % (uint32_t)rate)
        return;
    if (tidy < tick)
        tidy = tick + 10 + (int)(MONKEY_RND() % 150);
    who = (int)(MONKEY_RND() % (uint32_t)(players > 0 ? players : 1));
    PlayerInput *p = &in->player[who];
    switch (MONKEY_RND() % 25) {
    case 0: p->action = 1; break;
    case 1: case 2: p->button[MONKEY_RND() % 4] = 1; break;
    case 3: p->skill_tree = 1; break;
    case 4: p->stats_page = 1; break;
    case 5: p->potion_life = 1; break;
    case 6: p->potion_mana = 1; break;
    case 7: p->potion_stamina = 1; break;
    case 8: p->automap = 1; break;
    case 9: p->chat = 1; break;
    case 10: p->belt = 1 + (int)(MONKEY_RND() % 4); break;
    case 11: p->inventory = 1; break;
    case 12: p->nav_up = 1; break;
    case 13: p->nav_down = 1; break;
    case 14: p->nav_left = 1; break;
    case 15: p->nav_right = 1; break;
    case 16: p->drop = 1; break;
    case 17: p->back = 1; break;
    case 18: {
        float a = (float)(MONKEY_RND() % 628) / 100.0f;
        mx = cosf(a);
        my = sinf(a);
        hold = 5 + (int)(MONKEY_RND() % 40);
        break;
    }
    case 19:
        in->mouse_x = (int)(MONKEY_RND() % SCREEN_W);
        in->mouse_y = (int)(MONKEY_RND() % SCREEN_H);
        in->mouse_down = clicked = (int)(MONKEY_RND() % 2);
        break;
    case 20:
        switch (MONKEY_RND() % 4) {
        case 0: in->menu_up = 1; break;
        case 1: in->menu_down = 1; break;
        case 2: in->menu_left = 1; break;
        default: in->menu_right = 1; break;
        }
        break;
    case 21: in->confirm = 1; break;
    case 22: in->menu_back = 1; break;
    case 23: in->menu_delete = 1; break;
    default:
        if (MONKEY_RND() % 4 == 0)
            in->escape = 1;
        else
            in->start = 1;
        break;
    }
#undef MONKEY_RND
}

static uint32_t s_net_sent = UINT32_MAX; /* el último tick para el que ya se mandaron los controles */
static uint32_t s_net_tick;              /* el próximo tick de la partida en red */

static int net_step(const InputState *in, uint32_t t, int bots, int wait)
{
    if (s_net_sent != t + NET_DELAY) {
        PlayerInput loc[MAX_LOCAL];
        int n = game_num_local();
        if (bots)
            for (int k = 0; k < n; k++) {
                loc[k] = in->player[game_local_player(k)];
                loc[k].always_run = 0;
            }
        else
            n = game_local_inputs(in, loc);
        net_send_input(t + NET_DELAY, loc, n);
        s_net_sent = t + NET_DELAY;
    }
    PlayerInput all[MAX_PLAYERS];
    for (;;) { /* (lo que ya llegó se juega aunque después se haya cortado: así todas llegan al final de
                  una corrida del Eco, que termina en el mismo tick en todas) */
        net_poll();
        if (net_frame(t, all))
            break;
        if (net_error())
            return 2;
        if (!wait)
            return 0;
        SDL_Delay(1);
    }
    game_set_frame(all);
    int r = game_tick(in);
    game_set_frame(NULL);
    if ((t + 1) % 25 == 0)
        net_send_hash(t + 1, game_state_hash());
    return r == GAME_EXIT ? 3 : 1; /* (3: terminó bien, como la salida del Eco) */
}

/* Lo de esta máquina para la red: sus jugadores con la partida guardada de cada uno (texts: lo que
   hay que liberar después de crear o unirse) */
static void net_local_from(int n, const int *slots, const int *classes, NetLocal *mine, char **texts)
{
    memset(mine, 0, sizeof *mine);
    mine->num = n < 1 ? 1 : n > MAX_LOCAL ? MAX_LOCAL : n;
    for (int k = 0; k < mine->num; k++) {
        texts[k] = NULL;
        mine->cls[k] = classes ? classes[k] : k;
        mine->level[k] = 1;
        SlotInfo info;
        if (slots[k] > 0 && game_slot_info(slots[k], &info)) {
            mine->cls[k] = info.cls;
            mine->level[k] = info.level;
            mine->diffmax[k] = info.diff_max;
            texts[k] = game_slot_text(slots[k], &mine->save_len[k]);
            mine->save[k] = texts[k];
        }
    }
}

/* Llegó el comienzo: la partida con lo que mandó el anfitrión (local_slots: las partidas guardadas
   de los jugadores de esta máquina) */
static void net_begin_game(const int *local_slots)
{
    const NetStart *ns = net_started();
    game_set_difficulty(ns->diff);
    game_set_terror_zones(ns->terror);
    game_set_mode(ns->mode);
    int slots[MAX_PLAYERS], classes[MAX_PLAYERS];
    game_net_begin(ns, local_slots, slots, classes);
    game_start_slots(ns->num_players, slots, classes);
    game_net_started();
    s_net_sent = UINT32_MAX;
    s_net_tick = 0;
}

/* Se terminó la partida en red (salió alguno o se cortó): se guarda y al menú, con el porqué (ended:
   terminó bien, como la salida del Eco del Odio: no hay porqué) */
static void net_leave(int ended)
{
    char why[160];
    snprintf(why, sizeof why, "%s", net_error() && !ended ? net_error() : "");
    game_save();
    net_close();
    game_net_end();
    menu_open();
    if (why[0])
        menu_net_failed(why);
}

/* Las pruebas de la red sin ventana (docs/plan-lan.md): D2_NET_HOST=N crea la partida y empieza
   cuando hay N jugadores; D2_NET_JOIN=ip se une (D2_NET_PORT, si no el 6112). --players dice cuántos
   son de esta máquina (1 o 2), con las partidas D2_NET_SLOTS="1,2" de D2_SAVE_DIR (sin, personajes
   nuevos de las clases D2_CLASSES). Devuelve los jugadores de la partida, -1 = no se pudo. */
static int net_test_begin(int local)
{
    /* D2_NET_PREPLAY=N: antes, N ticks de otra partida (la prueba de que una máquina que ya jugó
       empieza la de red igual que una recién abierta) */
    if (getenv("D2_NET_PREPLAY")) {
        InputState z;
        memset(&z, 0, sizeof z);
        game_start(2);
        for (int t = 0; t < atoi(getenv("D2_NET_PREPLAY")); t++) {
            z.player[0].move_x = (t / 50) % 2 ? 1.0f : -0.5f;
            z.player[0].action = t % 40 == 0;
            game_tick(&z);
        }
        printf("red: antes se jugaron %s ticks de otra partida\n", getenv("D2_NET_PREPLAY"));
    }
    NetLocal mine;
    int lslots[MAX_LOCAL] = { 0 }, lcls[MAX_LOCAL] = { 0, 1 };
    char *texts[MAX_LOCAL] = { NULL };
    const char *se = getenv("D2_NET_SLOTS"), *ce = getenv("D2_CLASSES");
    for (int k = 0; k < MAX_LOCAL; k++) {
        lslots[k] = se && *se ? atoi(se) : 0;
        lcls[k] = ce && *ce ? atoi(ce) : k;
        if (se && *se)
            se += strcspn(se, ","), se += *se == ',';
        if (ce && *ce)
            ce += strcspn(ce, ","), ce += *ce == ',';
    }
    net_local_from(local, lslots, lcls, &mine, texts);
    mine.mode = getenv("D2_ECO") != NULL; /* (D2_ECO=1: una corrida del Eco del Odio en red) */
    int port = getenv("D2_NET_PORT") ? atoi(getenv("D2_NET_PORT")) : NET_PORT;
    int host = getenv("D2_NET_HOST") != NULL;
    int ok = host ? net_host(port, &mine) : net_join(getenv("D2_NET_JOIN"), port, &mine);
    for (int k = 0; k < MAX_LOCAL; k++)
        free(texts[k]);
    if (ok != 0)
        return -1;
    int want = host ? atoi(getenv("D2_NET_HOST")) : 0;
    Uint32 t0 = SDL_GetTicks();
    while (!net_started()) {
        net_poll();
        if (net_error())
            return -1;
        int c[MAX_PLAYERS], l[MAX_PLAYERS], o[MAX_PLAYERS];
        if (host && net_lobby(c, l, o, NULL, MAX_PLAYERS) >= want) {
            uint32_t seed = getenv("D2_SEED") ? (uint32_t)strtoul(getenv("D2_SEED"), NULL, 10) : (uint32_t)time(NULL);
            net_start(getenv("D2_DIFF") ? atoi(getenv("D2_DIFF")) : 0, mine.mode, 1, seed, (long long)time(NULL));
        }
        if (SDL_GetTicks() - t0 > 600000) {
            fprintf(stderr, "red: nadie se unió en 10 minutos\n");
            return -1;
        }
        SDL_Delay(5);
    }
    net_begin_game(lslots);
    return net_started()->num_players;
}

/* D2_VIDEO=archivo.mp4 (con --autotest): un video de la prueba, pasando cada cuadro a ffmpeg (D2_FFMPEG:
   cuál; si no, "ffmpeg"). D2_VIDEO_EVERY=N: un cuadro cada N ticks (el video va N veces más rápido) */
static FILE     *s_video;
static uint32_t *s_video_buf;
static int       s_video_every = 1;
static long      s_video_frames;

static void video_rec_open(void)
{
#ifndef _WIN32
    const char *out = getenv("D2_VIDEO");
    if (!out)
        return;
    char cmd[1200];
    video_lock_width(1); /* (el video, de un solo tamaño: 800 x 600) */
    snprintf(cmd, sizeof cmd,
             "%s -y -loglevel error -f rawvideo -pixel_format bgra -video_size %dx%d -framerate %d -i - "
             "-c:v libx264 -preset slow -crf %d -pix_fmt yuv420p -movflags +faststart \"%s\"",
             getenv("D2_FFMPEG") ? getenv("D2_FFMPEG") : "ffmpeg", SCREEN_W, SCREEN_H, TICK_RATE,
             getenv("D2_VIDEO_CRF") ? atoi(getenv("D2_VIDEO_CRF")) : 28, out); /* (D2_VIDEO_CRF: la calidad, más alto = más chico) */
    s_video = popen(cmd, "w");
    s_video_buf = malloc((size_t)SCREEN_W * SCREEN_H * 4);
    s_video_every = getenv("D2_VIDEO_EVERY") && atoi(getenv("D2_VIDEO_EVERY")) > 0 ? atoi(getenv("D2_VIDEO_EVERY")) : 1;
    if (!s_video || !s_video_buf)
        fprintf(stderr, "no pude grabar el video %s\n", out);
#endif
}

static void video_rec_frame(int t)
{
    if (!s_video || !s_video_buf || t % s_video_every)
        return;
    game_render(1.0f);
    video_grab_argb(s_video_buf);
    fwrite(s_video_buf, 4, (size_t)SCREEN_W * SCREEN_H, s_video);
    s_video_frames++;
}

static void video_rec_close(void)
{
#ifndef _WIN32
    if (s_video) {
        pclose(s_video);
        printf("video: %s (%ld cuadros, %.1f minutos)\n", getenv("D2_VIDEO"), s_video_frames,
               s_video_frames / (double)TICK_RATE / 60.0);
    }
#endif
    s_video = NULL;
    free(s_video_buf);
    s_video_buf = NULL;
}

static int save_screenshot(const char *path)
{
    if (video_save_bmp(path) != 0) {
        fprintf(stderr, "no pude guardar %s: %s\n", path, SDL_GetError());
        return 1;
    }
    printf("captura: %s\n", path);
    return 0;
}

/* Dibuja al Bárbaro y a la Amazona en las 16 direcciones (en orden de
   giro, 0 = mirando hacia abajo) para revisar el orden de las capas.
   D2_SPRITE_TOKENS="wk" (o "wk,ne") y D2_SPRITE_MODE="sc,1ht,8" eligen los
   personajes, la animación, la clase de arma y el cuadro (con un .d2rw al
   lado del DCC, el dibujo editado).
   D2_SPRITE_ARMOR="med" o "hvy": el cuerpo con esa armadura (la cabeza, sin casco) */
static int run_sprite_test(const Options *o)
{
    char tokens[2][4] = { "ba", "am" }, mode[4] = "tw", wclass[4] = "hth";
    int ntok = 2, frame = 0;
    const char *env = getenv("D2_SPRITE_TOKENS");
    if (env) {
        ntok = sscanf(env, "%3[^,],%3s", tokens[0], tokens[1]);
        ntok = ntok < 1 ? 1 : ntok;
    }
    if (getenv("D2_SPRITE_MODE"))
        sscanf(getenv("D2_SPRITE_MODE"), "%3[^,],%3[^,],%d", mode, wclass, &frame);
    static const int lit[] = { COMP_HD, COMP_TR, COMP_LG, COMP_RA, COMP_LA, COMP_S1, COMP_S2 };
    const char *armor = getenv("D2_SPRITE_ARMOR") ? getenv("D2_SPRITE_ARMOR") : "lit";

    clip_reset();
    fb_clear(COL(R_STONE, 3));
    char num[8];
    /* D2_TILE_SHEET="act1/town/floor.dt1,0": los pisos de ese estilo con su
       secuencia (para ver qué pieza es cada una, por ejemplo las del camino) */
    const char *sheet = getenv("D2_TILE_SHEET");
    if (sheet) {
        char file[128], path[200];
        int style = 0, skip = 0;
        if (sscanf(sheet, "%127[^,],%d,%d", file, &style, &skip) < 1)
            return 1;
        snprintf(path, sizeof path, "data/global/tiles/%s", file);
        size_t n;
        uint8_t *d = fs_read(path, &n);
        Dt1 dt1;
        if (!d || dt1_load(d, n, &dt1) != 0) {
            free(d);
            fprintf(stderr, "no pude leer %s\n", path);
            return 1;
        }
        free(d);
        int k = 0;
        for (int i = 0; i < dt1.num_tiles; i++) {
            const Dt1Tile *t = &dt1.tiles[i];
            if (t->orientation != 0 || t->main_index != style || !t->pixels || skip-- > 0)
                continue;
            int x = 10 + (k % 4) * 195, y = 10 + (k / 4) * 100;
            fb_blit_spans(t->pixels, t->spans, t->img_w, t->img_h, x, y + t->img_y);
            if (!getenv("D2_TILE_NOLABEL")) {
                snprintf(num, sizeof num, "%d", t->sub_index);
                font_draw(x + 70, y + 30, num, 2, COL(R_GOLD, 15));
            }
            printf("hoja %d: secuencia %d (y %d, alto %d)\n", k, t->sub_index, t->img_y, t->img_h);
            k++;
        }
        dt1_free(&dt1);
        return o->screenshot ? save_screenshot(o->screenshot) : 0;
    }
    for (int c = 0; c < ntok; c++) {
        CharLook look;
        memset(&look, 0, sizeof look);
        strcpy(look.token, tokens[c]);
        for (unsigned i = 0; i < sizeof lit / sizeof lit[0]; i++)
            snprintf(look.part[lit[i]], sizeof look.part[lit[i]], "%s", lit[i] == COMP_HD ? "lit" : armor);
        CharAnim tw;
        if (charanim_load(&tw, &look, mode, wclass) != 0) {
            fprintf(stderr, "no hay %s%s%s\n", tokens[c], mode, wclass);
            return 1;
        }
        for (int s = 0; s < 16; s++) {
            int x = 50 + (s % 8) * 100, y = 110 + (c * 2 + s / 8) * 145;
            charanim_draw(&tw, s, frame, x, y);
            snprintf(num, sizeof num, "%d", s);
            font_draw_centered(x, y + 8, num, 2, COL(R_GOLD, 14));
        }
        charanim_free(&tw);
    }
    return o->screenshot ? save_screenshot(o->screenshot) : 0;
}

/* Muestra qué parte de cada tile de un DS1 bloquea el paso:
   '#' casi todo, '+' algo, '.' nada (con x hacia la derecha y y hacia abajo) */
static int run_ds1_info(const char *path)
{
    Map m;
    if (map_load(&m, path) != 0)
        return 1;
    printf("%s: %dx%d tiles\n", path, m.width, m.height);
    for (int ty = 0; ty < m.height; ty++) {
        printf("  ");
        for (int tx = 0; tx < m.width; tx++) {
            int blocked = 0;
            for (int sy = 0; sy < 5; sy++)
                for (int sx = 0; sx < 5; sx++)
                    blocked += map_blocked(&m, tx * 5 + sx, ty * 5 + sy);
            putchar(blocked >= 10 ? '#' : blocked > 0 ? '+' : '.');
        }
        putchar('\n');
    }
    /* Las baldosas especiales: los saltos (estilo < 8, con la secuencia) y las marcas */
    for (int i = 0; i < m.num_specials; i++)
        printf("S %d %d orientación %d estilo %d secuencia %d\n", m.specials[i].x, m.specials[i].y,
               m.specials[i].orientation, m.specials[i].style, m.specials[i].sequence);
    /* D2_DS1_FLOORS=1: cada piso (capa 0) con su estilo, secuencia y DT1, para
       estudiar cómo están armadas las piezas (los caminos de tierra) */
    for (int ty = 0; getenv("D2_DS1_FLOORS") && ty < m.height; ty++)
        for (int tx = 0; tx < m.width; tx++) {
            const Dt1Tile *t = map_floor(&m, 0, tx, ty);
            const char *file = "-";
            for (int f = 0; t && f < m.num_dt1; f++)
                if (t >= m.dt1[f].tiles && t < m.dt1[f].tiles + m.dt1[f].num_tiles)
                    file = m.dt1_path[f];
            if (t)
                printf("F %d %d %d %d %s\n", tx, ty, t->main_index, t->sub_index, file);
        }
    map_free(&m);
    return 0;
}

/* D2_AUDIO_DUMP=archivo.wav: en las pruebas sin ventana graba lo que suena
   (un tick de audio por cada tick del juego) */
static FILE *s_dump;
static long  s_dump_frames;

static void dump_put32(FILE *f, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static void dump_header(FILE *f, uint32_t frames)
{
    static const uint8_t fmt[16] = { 16, 0, 0, 0, 1, 0, 2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0 };
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    dump_put32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(fmt, 1, sizeof fmt, f);
    fputc(4, f); fputc(0, f); fputc(16, f); fputc(0, f);
    fwrite("data", 1, 4, f);
    dump_put32(f, frames * 4);
}

static void dump_tick(void)
{
    audio_tick();
    if (!s_dump)
        return;
    static int16_t buf[AUDIO_RATE / TICK_RATE * 2];
    audio_render(buf, AUDIO_RATE / TICK_RATE);
    fwrite(buf, 4, AUDIO_RATE / TICK_RATE, s_dump);
    s_dump_frames += AUDIO_RATE / TICK_RATE;
}

static void dump_close(void)
{
    if (!s_dump)
        return;
    dump_header(s_dump, (uint32_t)s_dump_frames);
    fclose(s_dump);
    s_dump = NULL;
    printf("audio grabado: %.1f s\n", s_dump_frames / (double)AUDIO_RATE);
}

static int run_headless(const Options *o)
{
    if (o->sprite_test)
        return run_sprite_test(o);
    if (o->ds1_info)
        return run_ds1_info(o->ds1_info);

    InputState in;
    memset(&in, 0, sizeof in);
    if (getenv("D2_AUDIO_DUMP")) {
        s_dump = fopen(getenv("D2_AUDIO_DUMP"), "wb");
        if (s_dump)
            dump_header(s_dump, 0);
    }

    if (o->players == 0) {
        /* D2_MENU_SCRIPT="dda": una tecla por tick en el menú (u/d/l/r, a = aceptar,
           b = volver, x = borrar), para probar las pantallas sin ventana (D2_MENU_DELAY=ms: espera
           entre ticks, para la red) */
        menu_open();
        const char *script = getenv("D2_MENU_SCRIPT");
        int ticks = o->frames > 0 ? o->frames : (script ? (int)strlen(script) + 1 : 1);
        for (int t = 0; t < ticks; t++) {
            char c = script && t < (int)strlen(script) ? script[t] : '.';
            input_clear_events(&in);
            if (input_fake_pads(t)) /* (D2_FAKE_PAD: lo de los joysticks virtuales, y encima lo del guion) */
                input_poll(&in);
            in.menu_up |= c == 'u';
            in.menu_down |= c == 'd';
            in.menu_left |= c == 'l';
            in.menu_right |= c == 'r';
            in.confirm |= c == 'a';
            in.menu_back |= c == 'b';
            in.menu_delete |= c == 'x';
            /* (y las de cada jugador, para la pantalla de los dos a la vez: minúsculas el 1, mayúsculas el 2) */
            for (int k = 0; k < 2; k++) {
                PlayerInput *pi = &in.player[k];
                char lc = k ? (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : 0) : (c >= 'a' && c <= 'z' ? c : 0);
                pi->nav_up |= lc == 'u';
                pi->nav_down |= lc == 'd';
                pi->nav_left |= lc == 'l';
                pi->nav_right |= lc == 'r';
                pi->action |= lc == 'a';
                pi->back |= lc == 'b';
                pi->drop |= lc == 'x';
            }
            if (getenv("D2_MENU_DELAY")) /* (pruebas de la red: ticks de verdad, para que la otra máquina conteste) */
                SDL_Delay((Uint32)atoi(getenv("D2_MENU_DELAY")));
            MenuResult r = menu_tick(&in);
            dump_tick();
            if (getenv("D2_RENDER_EVERY")) /* (pruebas: el dibujo de cada pantalla del menú) */
                menu_render();
            if (r == MENU_START) {
                game_set_difficulty(menu_difficulty());
                game_set_terror_zones(menu_terror_zones());
                game_set_mode(menu_mode());
                game_start_slots(menu_players(), menu_slots(), menu_classes());
                game_render(1.0f);
                dump_close();
                return o->screenshot ? save_screenshot(o->screenshot) : 0;
            }
            if (r == MENU_RESTART) { /* (sin ventana no se vuelve a arrancar: solo se guarda) */
                game_set_language(menu_language());
                printf("idioma elegido: %s\n", menu_language());
            }
            if (r == MENU_NET_HOST || r == MENU_NET_JOIN) { /* (la red: para ver la sala de espera) */
                NetLocal mine;
                char *texts[MAX_LOCAL];
                net_local_from(menu_players(), menu_slots(), menu_classes(), &mine, texts);
                mine.mode = menu_mode();
                int port = getenv("D2_NET_PORT") ? atoi(getenv("D2_NET_PORT")) : NET_PORT;
                int ok = r == MENU_NET_HOST ? net_host(port, &mine) : net_join(menu_net_ip(), port, &mine);
                for (int k = 0; k < mine.num; k++)
                    free(texts[k]);
                if (ok == 0)
                    menu_net_lobby();
                else
                    menu_net_failed("No pude conectarme a esa partida");
            }
        }
        menu_render();
    } else if (o->autotest) {
        /* La prueba automática: hasta que el bot termina (o se pasa de 3 horas de juego) */
        if (!getenv("D2_NO_JOBS")) /* (D2_NO_JOBS: todo en el hilo del juego, para comparar) */
            jobs_start(); /* como en el juego: los gráficos se cargan en otro hilo */
        if (getenv("D2_AUTOTEST_DEMO")) { /* lo mismo que la demostración del menú (1 Bárbaro, 2 Amazona, 3 Hechicera, 4 Paladín, 5 Nigromante, 6 Asesina, 7 Druida, 8 Brujo; con la clase que sigue; 9 el Brujo con el Paladín) */
            int d = atoi(getenv("D2_AUTOTEST_DEMO"));
            if (d == 9)
                game_demo_start_pair(7, 3);
            else
                game_demo_start(d - 1);
        } else if (getenv("D2_NET_HOST") || getenv("D2_NET_JOIN")) { /* la prueba en red: cada máquina con su bot */
            int n = net_test_begin(o->players);
            if (n < 0 || game_autotest_start(o->autotest, n) != 0) {
                fprintf(stderr, "no pude empezar la prueba en red\n");
                return 2;
            }
        } else {
            game_start(o->players);
            if (game_autotest_start(o->autotest, o->players) != 0) {
                fprintf(stderr, "no conozco la prueba %s\n", o->autotest);
                return 2;
            }
        }
        int r = 1;
        double last = 0.0;
        video_rec_open();
        /* Tope de tiempo de juego: 3 horas (la demostración pelea de verdad: 6
           por acto, y la campaña son 15); con --frames N, a los N ticks (para
           perfilar un tramo y salir normal) */
        int hours = getenv("D2_AUTOTEST_DEMO") ? 6 * game_demo_acts() : 3;
        int cap = o->frames > 0 ? o->frames : hours * 3600 * TICK_RATE;
        for (int t = 0; r == 1 && t < cap; t++) {
            r = game_autotest_input(&in, last);
            if (getenv("D2_MONKEY"))
                monkey_input(&in, o->players, t);
            Uint64 t0 = SDL_GetPerformanceCounter();
            if (net_role() != NET_OFF) { /* en red: con los controles de todas las máquinas */
                int st = net_step(&in, (uint32_t)t, 1, 1);
                if (st == 3) {
                    printf("red: la partida terminó (la salida del Eco)\n");
                    r = 0;
                } else if (st == 2) {
                    fprintf(stderr, "red: %s\n", net_error() ? net_error() : "se terminó");
                    r = -1;
                }
            } else {
                game_tick(&in);
            }
            last = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
            dump_tick(); /* el sonido avanza como en el juego (y se graba con D2_AUDIO_DUMP) */
            video_rec_frame(t);
            /* D2_FX_SHOTS=prefijo: capturas de los efectos de los jefes y de
               las Piedras Cairn (a los 5, 30, 61 y 100 ticks de empezar; con
               D2_FX_EVERY=n, cada n ticks) */
            int fx_kind, fx_age = game_debug_fx(&fx_kind);
            int fx_every = getenv("D2_FX_EVERY") ? atoi(getenv("D2_FX_EVERY")) : 0;
            if (getenv("D2_FX_SHOTS") && fx_age >= 0 &&
                (fx_every > 0 ? fx_age % fx_every == 0
                              : (fx_age == 5 || fx_age == 30 || fx_age == 61 || fx_age == 100))) {
                char shot[256];
                snprintf(shot, sizeof shot, "%s_%d_%d.bmp", getenv("D2_FX_SHOTS"), fx_kind, fx_age);
                game_render(1.0f);
                save_screenshot(shot);
            }
            /* D2_OBJ_SHOTS=prefijo: el J1 abriendo un cofre, un barril... (a 1, 15 y 40 ticks) */
            int ob_kind, ob_age = game_debug_obj(&ob_kind);
            if (getenv("D2_OBJ_SHOTS") && (ob_age == 1 || ob_age == 15 || ob_age == 40)) {
                char shot[256];
                snprintf(shot, sizeof shot, "%s_%07d_%d_%d.bmp", getenv("D2_OBJ_SHOTS"), t - ob_age, ob_kind, ob_age);
                game_render(1.0f);
                save_screenshot(shot);
            }
            /* D2_SHOTS=prefijo,desde,hasta,cada: capturas entre esos ticks
               (para revisar animaciones; en la prueba de habilidades, los de
               la ronda) */
            if (getenv("D2_SHOTS")) {
                char prefix[200] = "";
                int from = 0, to = 0, every = 1, now = game_debug_round_tick() >= 0 ? game_debug_round_tick() : t;
                if (sscanf(getenv("D2_SHOTS"), "%199[^,],%d,%d,%d", prefix, &from, &to, &every) >= 3 && now >= from &&
                    now <= to && (now - from) % (every > 0 ? every : 1) == 0) {
                    char shot[256];
                    snprintf(shot, sizeof shot, "%s_%06d.bmp", prefix, now);
                    game_render(1.0f);
                    save_screenshot(shot);
                }
            }
            /* Dibujar de vez en cuando, también para medirlo (D2_RENDER_EVERY=1:
               en cada tick, como el juego, para perfilar el dibujo) */
            static int render_every = -1;
            if (render_every < 0)
                render_every = getenv("D2_RENDER_EVERY") && atoi(getenv("D2_RENDER_EVERY")) > 0
                                   ? atoi(getenv("D2_RENDER_EVERY")) : 25;
            if (t % render_every == 0) {
                Uint64 r0 = SDL_GetPerformanceCounter();
                game_render(1.0f);
                game_autotest_render_time((double)(SDL_GetPerformanceCounter() - r0) * 1000.0 /
                                          (double)SDL_GetPerformanceFrequency());
            }
        }
        video_rec_close();
        game_autotest_report();
        if (net_role() != NET_OFF) {
            printf("red: %s\n", net_error() ? net_error() : "sin separarse");
            net_close();
            game_net_end();
        }
        dump_close();
        if (o->screenshot)
            save_screenshot(o->screenshot);
        return r == 0 ? 0 : 1;
    } else {
        game_start(o->players);
        int ticks = o->frames > 0 ? o->frames : 1;
        int verbose = getenv("D2_DEBUG") != NULL;
        int perf = getenv("D2_TICK_PERF") ? atoi(getenv("D2_TICK_PERF")) : 0; /* cada cuántos ticks, lo que tarda la lógica */
        double perf_sum = 0, perf_max = 0;
        for (int t = 0; t < ticks; t++) {
            if (o->demo)
                demo_input(&in, o, t);
            else if (input_fake_pads(t)) /* (D2_FAKE_PAD: los joysticks virtuales de las pruebas) */
                input_poll(&in);
            Uint64 p0 = SDL_GetPerformanceCounter();
            game_tick(&in);
            if (!o->demo)
                input_clear_events(&in);
            double pms = (double)(SDL_GetPerformanceCounter() - p0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
            perf_sum += pms;
            perf_max = pms > perf_max ? pms : perf_max;
            if (perf > 0 && (t + 1) % perf == 0) {
                char line[128];
                game_perf_line(line, sizeof line);
                printf("tick %d: lógica %.2f ms (peor %.1f), %s\n", t + 1, perf_sum / perf, perf_max, line);
                perf_sum = perf_max = 0;
            }
            dump_tick();
            /* D2_SHOTS=prefijo,desde,hasta,cada: también acá (los proyectiles de un D2_SPAWN) */
            if (getenv("D2_SHOTS")) {
                char prefix[200] = "";
                int from = 0, to = 0, step = 1;
                if (sscanf(getenv("D2_SHOTS"), "%199[^,],%d,%d,%d", prefix, &from, &to, &step) >= 3 && t >= from &&
                    t <= to && (t - from) % (step > 0 ? step : 1) == 0) {
                    char shot[256];
                    snprintf(shot, sizeof shot, "%s_%06d.bmp", prefix, t);
                    game_render(1.0f);
                    save_screenshot(shot);
                }
            }
            int every = getenv("D2_EVERY") ? atoi(getenv("D2_EVERY")) : 250;
            if (verbose && every > 0 && (t + 1) % every == 0) {
                printf("tick %d (%.0f s):\n", t + 1, (t + 1) / (float)TICK_RATE);
                game_print_status();
            }
        }
        game_render(1.0f);
        dump_close();
        if (getenv("D2_SAVE_DIR"))
            game_save();

        /* Mide cuánto tarda en dibujarse cada cuadro, sin contar la presentación */
        if (o->bench > 0) {
            game_bench_parts(NULL, 0);
            double conv = 0.0;
            Uint64 t0 = SDL_GetPerformanceCounter();
            for (int i = 0; i < o->bench; i++) {
                game_render((float)(i % 10) / 10.0f);
                conv += video_convert_bench(); /* la conversión para la GPU (con la oscuridad) */
            }
            double ms = ((double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency() -
                         conv) / o->bench;
            conv /= o->bench;
            printf("dibujo: %.2f ms por cuadro (%.0f cuadros por segundo)\n", ms, 1000.0 / ms);
            if (getenv("D2_BENCH_PARTS")) {
                char parts[160];
                game_bench_parts(parts, sizeof parts);
                printf("partes: %s; conversión %.2f ms\n", parts, conv);
            }
        }
    }
    return o->screenshot ? save_screenshot(o->screenshot) : 0;
}

static int s_restart; /* al salir, el juego vuelve a arrancar (se eligió otro idioma) */

static int run_window(const Options *o)
{
    const double tick_s = 1.0 / TICK_RATE;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0.0, fps_time = 0.0;
    int fps_frames = 0, frame = 0, tick = 0;
    /* Para el log: cuánto tarda cada parte (en la Pi se ve en
       /userdata/system/logs/d2port.log) */
    double t_logic = 0.0, t_draw = 0.0, t_present = 0.0, t_worst = 0.0, t_upload = 0.0;
    int logic_ticks = 0;
    /* Lo mismo cada segundo, para VER RENDIMIENTO en la pausa */
    double s_time = 0.0, s_logic = 0.0, s_draw = 0.0, s_upload = 0.0, s_present = 0.0, s_worst = 0.0;
    int s_frames = 0, s_ticks = 0;

    AppState state = ST_MENU;
    if (o->players > 0) {
        game_start(o->players);
        state = ST_GAME;
    } else {
        menu_open();
    }

    InputState in, demo_in;
    memset(&in, 0, sizeof in);
    memset(&demo_in, 0, sizeof demo_in);
    int idle = 0, demo_next = 0; /* ticks con el menú quieto; la próxima pareja de la rotación */
    int movie = 0;                /* se pasó una cinemática en este tick */
    int restart_wait = 0;         /* D2_RESTART_LANG */
    double demo_ms = 0.0;

    while (!in.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)freq;
        last = now;
        if (dt > 0.25)
            dt = 0.25;
        acc += dt;

        input_poll(&in);
        if (in.suspend) { /* (Android: pasa a segundo plano o se cierra: que lo jugado quede guardado) */
            in.suspend = 0;
            if (state == ST_GAME)
                game_save();
        }
        while (acc >= tick_s && !in.quit) {
            if (state == ST_MENU) {
                MenuResult r = menu_tick(&in);
                if (r == MENU_QUIT) {
                    in.quit = 1;
                } else if (r == MENU_START) {
                    game_set_difficulty(menu_difficulty());
                    game_set_terror_zones(menu_terror_zones());
                    game_set_mode(menu_mode());
                    game_start_slots(menu_players(), menu_slots(), menu_classes());
                    state = ST_GAME;
                } else if (r == MENU_MOVIE) {
                    in.quit = cinema_play(menu_movie()) == 1;
                    movie = 1;
                } else if (r == MENU_RESTART) { /* otro idioma: se guarda y se arranca de nuevo */
                    game_set_language(menu_language());
                    s_restart = 1;
                    in.quit = 1;
                } else if (r == MENU_NET_HOST || r == MENU_NET_JOIN) { /* la red: crear la partida o unirse */
                    NetLocal mine;
                    char *texts[MAX_LOCAL];
                    net_local_from(menu_players(), menu_slots(), menu_classes(), &mine, texts);
                    mine.mode = menu_mode();
                    int ok = r == MENU_NET_HOST ? net_host(NET_PORT, &mine) : net_join(menu_net_ip(), NET_PORT, &mine);
                    for (int k = 0; k < mine.num; k++)
                        free(texts[k]);
                    if (ok == 0)
                        menu_net_lobby();
                    else
                        menu_net_failed(r == MENU_NET_HOST ? "No pude crear la partida (¿el puerto 6112 está ocupado?)"
                                                           : "No pude conectarme a esa partida");
                } else if (r == MENU_NET_GO) { /* el anfitrión la empieza */
                    net_start(menu_net_difficulty(), menu_mode(), menu_terror_zones(),
                              (uint32_t)time(NULL) ^ (uint32_t)SDL_GetPerformanceCounter(), (long long)time(NULL));
                } else if (r == MENU_NET_START) { /* llegó el comienzo: a jugar */
                    net_begin_game(menu_slots());
                    state = ST_GAME;
                }
                /* D2_RESTART_LANG=deDE (prueba): como si se eligiera ese idioma en el menú */
                const char *rl = getenv("D2_RESTART_LANG");
                if (rl && state == ST_MENU && !s_restart && strncmp(rl, lang_strings_code(), 4) != 0 && ++restart_wait > 10) {
                    printf("prueba: vuelve a arrancar en %s\n", rl);
                    game_set_language(rl);
                    s_restart = 1;
                    in.quit = 1;
                }
                /* Quieto en el menú principal: el bot se pone a jugar solo */
                idle = menu_is_main() && !input_any(&in) ? idle + 1 : 0;
                if (state == ST_MENU && demo_seconds() > 0 && idle >= demo_seconds() * TICK_RATE) {
                    int c1, c2;
                    game_demo_rotation(demo_next++, &c1, &c2);
                    game_demo_start_pair(c1, c2);
                    state = ST_DEMO;
                    idle = 0;
                }
                audio_tick();
            } else if (state == ST_DEMO) {
                if (input_any(&in)) { /* Start (o cualquier botón): vuelve al menú */
                    game_demo_stop();
                    menu_open();
                    state = ST_MENU;
                } else {
                    Uint64 l0 = SDL_GetPerformanceCounter();
                    game_take_movie(); /* los bots no paran a ver cinemáticas */
                    if (game_autotest_input(&demo_in, demo_ms) != 1) {
                        /* Terminó el acto (o se trabó): otra, con las clases que siguen */
                        game_demo_stop();
                        int c1, c2;
                        game_demo_rotation(demo_next++, &c1, &c2);
                        game_demo_start_pair(c1, c2);
                    } else {
                        game_tick(&demo_in);
                    }
                    input_clear_events(&demo_in);
                    demo_ms = (double)(SDL_GetPerformanceCounter() - l0) / (double)freq * 1000.0;
                }
                audio_tick();
            } else {
                if (o->demo)
                    demo_input(&in, o, tick);
                Uint64 l0 = SDL_GetPerformanceCounter();
                if (net_role() != NET_OFF) { /* en red: con los controles de todas las máquinas */
                    int st = net_step(&in, s_net_tick, 0, 0);
                    if (st == 0) { /* todavía no llegaron los de todos: se espera (sin perder lo que se apretó) */
                        acc = acc > tick_s ? tick_s : acc;
                        break;
                    }
                    if (st == 1) {
                        s_net_tick++;
                    } else {
                        net_leave(st == 3);
                        state = ST_MENU;
                    }
                } else if (game_tick(&in) == GAME_EXIT) {
                    menu_open();
                    state = ST_MENU;
                }
                /* Fin de un acto: su cinemática, antes de seguir (en red no: los otros seguirían jugando) */
                const char *mv = state == ST_GAME && !o->demo ? game_take_movie() : NULL;
                if (net_role() != NET_OFF)
                    mv = NULL;
                if (mv) {
                    in.quit = cinema_play(mv) == 1;
                    movie = 1;
                }
                double lt = (double)(SDL_GetPerformanceCounter() - l0) / (double)freq * 1000.0;
                t_logic += lt;
                if (lt > t_worst)
                    t_worst = lt;
                logic_ticks++;
                s_logic += lt;
                if (lt > s_worst)
                    s_worst = lt;
                s_ticks++;
                audio_tick();
            }
            input_clear_events(&in);
            acc -= tick_s;
            tick++;
            if (movie) { /* el tiempo que duró la cinemática no se juega */
                movie = 0;
                acc = 0.0;
                last = SDL_GetPerformanceCounter();
            }
        }

        /* COLOR 16 BITS de la pausa (o D2_RGB565 si nunca se tocó) */
        if (game_wants_rgb565() >= 0 && game_wants_rgb565() != video_is_565())
            video_set_565(game_wants_rgb565());
        Uint64 d0 = SDL_GetPerformanceCounter();
        if (state == ST_MENU)
            menu_render();
        else
            game_render((float)(acc / tick_s));
        Uint64 d1 = SDL_GetPerformanceCounter();
        video_present();
        Uint64 d2 = SDL_GetPerformanceCounter();
        t_draw += (double)(d1 - d0) / (double)freq * 1000.0;
        t_present += (double)(d2 - d1) / (double)freq * 1000.0;
        s_draw += (double)(d1 - d0) / (double)freq * 1000.0;
        s_upload += video_upload_ms();
        t_upload += video_upload_ms();
        s_present += (double)(d2 - d1) / (double)freq * 1000.0 - video_upload_ms();
        s_frames++;
        s_time += dt;
        if (s_time >= 1.0) {
            game_set_perf((float)(s_frames / s_time), s_ticks ? (float)(s_logic / s_ticks) : 0.0f, (float)s_worst,
                          (float)(s_draw / s_frames), (float)(s_upload / s_frames), (float)(s_present / s_frames));
            s_time = s_logic = s_draw = s_upload = s_present = s_worst = 0.0;
            s_frames = s_ticks = 0;
        }

        frame++;
        fps_frames++;
        fps_time += dt;
        if (fps_time >= 5.0) {
            /* La presentación incluye esperar la sincronía vertical (si la hay) */
            printf("fps: %.1f (lógica %.2f ms por tick, la peor %.1f; dibujo %.2f ms; subir %.2f ms; presentar %.2f ms "
                   "por cuadro)\n",
                   fps_frames / fps_time, logic_ticks ? t_logic / logic_ticks : 0.0, t_worst, t_draw / fps_frames,
                   t_upload / fps_frames, (t_present - t_upload) / fps_frames);
            fflush(stdout);
            fps_frames = logic_ticks = 0;
            fps_time = t_logic = t_draw = t_present = t_worst = t_upload = 0.0;
        }
        if (o->frames > 0 && frame >= o->frames)
            break;
        if (!video_has_vsync())
            SDL_Delay(1);
    }
    /* Cerrar la ventana, Select + Start o que Batocera mate el proceso
       (SDL convierte SIGTERM en un pedido de salida): se guarda antes */
    if (state == ST_GAME)
        game_save();
    return o->screenshot ? save_screenshot(o->screenshot) : 0;
}

int main(int argc, char **argv)
{
    compat_start(); /* (Windows: la carpeta del .exe y el registro; Android: la carpeta de la app) */
    Options o;
    if (parse_args(argc, argv, &o) != 0)
        return 2;
    crash_handler_install();
#ifdef __ANDROID__
    /* El teléfono: pantalla completa y acostado; el botón de volver es Esc (la pausa); los dedos van a los
       controles táctiles (touch.c), no al mouse; el acelerómetro no es un joystick */
    o.fullscreen = 1;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    if (compat_check_data() != 0)
        return 1;
#endif

    Uint32 flags = o.headless ? (getenv("D2_FAKE_PAD") ? SDL_INIT_GAMECONTROLLER : 0) /* (los joysticks virtuales de las pruebas) */
                              : (SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    if (SDL_Init(flags) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (video_init(o.headless, o.fullscreen, o.scale) != 0) {
        SDL_Quit();
        return 1;
    }
    if (!o.headless)
        touch_init(); /* (Android, o D2_TOUCH=1: los controles táctiles, un joystick virtual) */
    /* Sonido: en la ventana, la placa de sonido (D2_NOSOUND lo apaga); sin
       ventana solo si se graba con D2_AUDIO_DUMP */
    audio_init(o.headless ? (getenv("D2_AUDIO_DUMP") ? AUDIO_MANUAL : AUDIO_OFF)
                          : (getenv("D2_NOSOUND") ? AUDIO_OFF : AUDIO_DEVICE));

    /* Las partidas se guardan en D2_SAVE_DIR (en Batocera, el lanzador la
       apunta a /userdata/saves/ports/d2port) o en ./save. En las pruebas sin
       ventana solo si se pide con D2_SAVE_DIR. */
    const char *save_dir = getenv("D2_SAVE_DIR");
    game_set_save_dir(save_dir ? save_dir : o.headless ? NULL : "save");
    cinema_set_dir(save_dir ? save_dir : o.headless ? NULL : "save");

    /* D2_CINEMA_BENCH=nombre: cuánto tarda en decodificarse (sin pantalla);
       D2_CINEMA=nombre: pasa esa y sale */
    if (getenv("D2_CINEMA_BENCH") || (getenv("D2_CINEMA") && !o.headless)) {
        int r = getenv("D2_CINEMA_BENCH") ? cinema_bench(getenv("D2_CINEMA_BENCH")) : cinema_play(getenv("D2_CINEMA"));
        audio_shutdown();
        input_shutdown();
        video_shutdown();
        SDL_Quit();
        return r < 0 ? 1 : 0;
    }
    /* Al arrancar, como el original: los logos de Blizzard y la presentación
       (cada una se saltea con cualquier tecla o botón). Los logos, con su
       sonido: el video de logos del Resurrected (la misma animación de los
       dos del original; los clásicos que trae vienen mudos); si no se
       importó, los clásicos. No en las pruebas ni si se entra directo a
       jugar (--players). */
    int quit = 0;
    if (!o.headless && !o.players && !o.demo && !getenv("D2_NO_CINEMA")) {
        int r = cinema_play("blizzardlogos");
        quit = r == 1;
        if (r < 0) {
            quit = cinema_play("new_bliz") == 1;
            if (!quit)
                quit = cinema_play("bliznorth") == 1;
        }
        if (!quit)
            quit = cinema_play("d2intro") == 1;
    }

    int rc = quit ? 0 : 1;
    if (!quit)
        ui_loading(0, 6); /* el original muestra la puerta mientras carga */
    /* Los gráficos de los monstruos de una zona nueva, los sonidos y los
       paneles se cargan en otro hilo para que el juego no se trabe. Arranca
       antes de game_init: lo que pide la carga (los sonidos de las
       habilidades, las baldosas y los gráficos del pueblo) se hace ahí
       mientras sigue. Las pruebas sin ventana cargan todo en el momento,
       para que den siempre igual (D2_ASYNC=1 las hace como el juego). */
    if (!quit && (!o.headless || getenv("D2_ASYNC")))
        jobs_start();
    if (!quit && !o.headless)
        menu_preload(); /* (lo primero que se ve después de cargar) */
    if (!quit && game_init() == 0)
        rc = o.headless ? run_headless(&o) : run_window(&o);

    jobs_wait(3000); /* que nadie esté decodificando lo que se libera */
    audio_shutdown();
    input_shutdown();
    touch_shutdown();
    video_shutdown();
    SDL_Quit();
    if (s_restart && compat_restart() == 0) /* (Android: lo arranca de nuevo la actividad) */
        s_restart = 0;
    if (s_restart) { /* otra vez, sin las cinemáticas del principio */
        fflush(NULL);
#ifdef _WIN32
        _putenv("D2_NO_CINEMA=1");
        _execv(argv[0], (const char *const *)argv);
#else
        setenv("D2_NO_CINEMA", "1", 1);
        execv("/proc/self/exe", argv);
        execv(argv[0], argv);
#endif
        fprintf(stderr, "no pude volver a arrancar el juego\n");
    }
    return rc;
}
