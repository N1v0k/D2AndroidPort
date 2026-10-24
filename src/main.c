#include "audio.h"
#include "bot.h"
#include "cinema.h"
#include "fmt/fs.h"
#include "charanim.h"
#include "font.h"
#include "game.h"
#include "input.h"
#include "ui.h"
#include "jobs.h"
#include "lang.h"
#include "map.h"
#include "menu.h"
#include "units.h"
#include "video.h"

#include <SDL.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    o->scale = 1;
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
       i = inventario, t = árbol de habilidades, o = LB, c = R3 (frases), . = nada) */
    const char *script = getenv("D2_INV_SCRIPT");
    if (script) {
        in->player[0].move_x = in->player[0].move_y = 0.0f;
        in->player[0].nav_up = in->player[0].nav_down = in->player[0].nav_left = 0;
        in->player[0].nav_right = in->player[0].action = in->player[0].drop = in->player[0].inventory = 0;
        in->player[0].back = in->player[0].skill_tree = in->player[0].potion_life = in->player[0].chat = 0;
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
            in->player[0].potion_life = c == 'o'; /* LB: ordenar en el inventario */
            in->player[0].chat = c == 'c';
        }
        return;
    }

    /* D2_ACTION: apretar acción cada 6 ticks sin dejar de caminar */
    if (getenv("D2_ACTION"))
        in->player[0].action = tick % 6 == 0;

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
           b = volver, x = borrar), para probar las pantallas sin ventana */
        menu_open();
        const char *script = getenv("D2_MENU_SCRIPT");
        int ticks = o->frames > 0 ? o->frames : (script ? (int)strlen(script) + 1 : 1);
        for (int t = 0; t < ticks; t++) {
            char c = script && t < (int)strlen(script) ? script[t] : '.';
            input_clear_events(&in);
            in.menu_up = c == 'u';
            in.menu_down = c == 'd';
            in.menu_left = c == 'l';
            in.menu_right = c == 'r';
            in.confirm = c == 'a';
            in.menu_back = c == 'b';
            in.menu_delete = c == 'x';
            MenuResult r = menu_tick(&in);
            dump_tick();
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
        }
        menu_render();
    } else if (o->autotest) {
        /* La prueba automática: hasta que el bot termina (o se pasa de 3 horas de juego) */
        jobs_start(); /* como en el juego: los gráficos se cargan en otro hilo */
        if (getenv("D2_AUTOTEST_DEMO")) { /* lo mismo que la demostración del menú (1 Bárbaro, 2 Amazona, 3 Hechicera, 4 Paladín, 5 Nigromante, 6 Asesina, 7 Druida, 8 Brujo; con la clase que sigue; 9 el Brujo con el Paladín) */
            int d = atoi(getenv("D2_AUTOTEST_DEMO"));
            if (d == 9)
                game_demo_start_pair(7, 3);
            else
                game_demo_start(d - 1);
        } else {
            game_start(o->players);
            if (game_autotest_start(o->autotest, o->players) != 0) {
                fprintf(stderr, "no conozco la prueba %s\n", o->autotest);
                return 2;
            }
        }
        int r = 1;
        double last = 0.0;
        /* Tope de tiempo de juego: 3 horas (la demostración pelea de verdad: 6
           por acto, y la campaña son 15); con --frames N, a los N ticks (para
           perfilar un tramo y salir normal) */
        int hours = getenv("D2_AUTOTEST_DEMO") ? 6 * game_demo_acts() : 3;
        int cap = o->frames > 0 ? o->frames : hours * 3600 * TICK_RATE;
        for (int t = 0; r == 1 && t < cap; t++) {
            r = game_autotest_input(&in, last);
            Uint64 t0 = SDL_GetPerformanceCounter();
            game_tick(&in);
            last = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
            dump_tick(); /* el sonido avanza como en el juego (y se graba con D2_AUDIO_DUMP) */
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
        game_autotest_report();
        dump_close();
        if (o->screenshot)
            save_screenshot(o->screenshot);
        return r == 0 ? 0 : 1;
    } else {
        game_start(o->players);
        int ticks = o->frames > 0 ? o->frames : 1;
        int verbose = getenv("D2_DEBUG") != NULL;
        for (int t = 0; t < ticks; t++) {
            if (o->demo)
                demo_input(&in, o, t);
            game_tick(&in);
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
                if (game_tick(&in) == GAME_EXIT) {
                    menu_open();
                    state = ST_MENU;
                }
                /* Fin de un acto: su cinemática, antes de seguir */
                const char *mv = state == ST_GAME && !o->demo ? game_take_movie() : NULL;
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
    Options o;
    if (parse_args(argc, argv, &o) != 0)
        return 2;
    crash_handler_install();

    Uint32 flags = o.headless ? 0 : (SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    if (SDL_Init(flags) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (video_init(o.headless, o.fullscreen, o.scale) != 0) {
        SDL_Quit();
        return 1;
    }
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
       (cada una se saltea con A, B, Start o Esc). No en las pruebas ni si se
       entra directo a jugar (--players). */
    int quit = 0;
    if (!o.headless && !o.players && !o.demo && !getenv("D2_NO_CINEMA")) {
        static const char *const k_start[] = { "new_bliz", "bliznorth", "d2intro" };
        for (int i = 0; i < 3 && !quit; i++)
            quit = cinema_play(k_start[i]) == 1;
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
    video_shutdown();
    SDL_Quit();
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
