#include "menu.h"

#include "audio.h"
#include "cinema.h"
#include "controls.h"
#include "font.h"
#include "game.h"
#include "ui.h"
#include "video.h"
#include "lang.h"
#include "net.h"

#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Menús con los gráficos del original:
   - Principal: el fondo de la expansión, el logo con fuego y los botones.
   - Elegir personaje: la pantalla de selección con las seis partidas.
   - Clase del personaje nuevo, y confirmar el borrado. */

enum { MS_MAIN, MS_SLOTS, MS_CLASS, MS_DELETE, MS_CINEMA, MS_DIFF, MS_CONTROLS, MS_LANG, MS_PLAY, MS_NET_PLAYERS, MS_NET, MS_FIND,
       MS_LOBBY };
enum { MODE_STORY, MODE_ECO, MODE_NET_HOST, MODE_NET_JOIN };

/* Como el original: las cinemáticas entre jugar y salir (y los controles y el idioma). Uno o
   dos jugadores eligen después la historia o el Eco del Odio (docs/plan-eco-del-odio.md); la red
   local, cuántos juegan en esta máquina, crear o unirse y (al crear) la historia o el Eco */
enum { ITEM_ONE, ITEM_TWO, ITEM_NET, ITEM_CINEMA, ITEM_CONTROLS, ITEM_LANG, ITEM_QUIT };
static const char *const k_items[] = { "UN JUGADOR", "DOS JUGADORES", "RED LOCAL", "CINEMÁTICAS", "CONTROLES", "IDIOMA", "SALIR" };
#define NUM_ITEMS ((int)(sizeof k_items / sizeof k_items[0]))

/* Los menús que siguen, con los mismos botones (el último, VOLVER) */
static const char *const k_play_opts[] = { "HISTORIA", "ECO DEL ODIO", "VOLVER" };
static const char *const k_count_opts[] = { "UN JUGADOR", "DOS JUGADORES", "VOLVER" };
static const char *const k_net_opts[] = { "CREAR PARTIDA", "UNIRSE A UNA PARTIDA", "VOLVER" };
#define FIND_SHOWN 5 /* las partidas encontradas que entran como botones */

static int      s_state;
static int      s_sel;          /* botón del menú principal */
static int      s_slot_sel;     /* partida marcada (0..5) */
static int      s_class_sel;
static int      s_delete_sel;   /* 0 = sí, 1 = no */
static int      s_players;      /* 1 o 2 */
static int      s_picking;      /* qué jugador está eligiendo */
static int      s_slots[2], s_classes[2];
static uint32_t s_ticks;
static SlotInfo s_info[SAVE_SLOTS];
static char     s_note[128];    /* aviso corto ("Ya la eligió el jugador 1") */
static int      s_note_ticks;
static int      s_movie_sel;    /* en el panel de cinemáticas (la última fila es "Cancelar") */
static int      s_diff_sel;     /* la dificultad elegida (0 Normal, 1 Pesadilla, 2 Infierno) */
static int      s_diff_max;     /* la más alta que tienen abierta todos los elegidos */
static int      s_terror = 1;   /* las zonas de terror (como la casilla de crear partida del Resurrected) */
static int      s_lang_sel;     /* en la lista de idiomas */
static int      s_mode;         /* MODE_*: la historia, el Eco del Odio, crear o unirse a una partida en red */
static int      s_net_sel;      /* en el panel de la red: 0 crear, 1 unirse */
static int      s_net_players = 1; /* los de esta máquina (1 o 2) */
static char     s_net_found[8][64]; /* las partidas que contestaron */
static int      s_net_found_n, s_net_found_players[8], s_net_find_sel;
static int      s_net_found_mode[8];
static char     s_net_ip[64];   /* a la que se une */
static int      s_net_diff;     /* la que elige el anfitrión */
static int      s_play_sel;     /* historia, Eco del Odio o volver */
static int      s_play_net;     /* ese menú es el de crear una partida en red */
static int      s_count_sel;    /* en la red: uno o dos jugadores en esta máquina, o volver */
static int      s_eco_game;     /* se juega el Eco del Odio (también en red: el que crea lo elige, el que se une lo ve) */

/* Con dos jugadores en esta máquina eligen los dos a la vez, cada uno con su joystick (o su lado del teclado: WASD,
   Espacio, Retroceso y Q; las flechas, Enter, Suprimir y M): su cursor en las partidas, la clase de una vacía en el
   mismo cuadro y LISTO. Cuando están los dos, se sigue. */
enum { PK_SLOT, PK_CLASS, PK_READY };
static struct {
    int sel;   /* la partida marcada (0..5) */
    int stage; /* PK_*: eligiendo partida, la clase de la nueva o listo */
    int cls;
} s_pk[MAX_LOCAL];
static int s_dual; /* la pantalla de las partidas es la de los dos a la vez */

/* Lo que dice cada clase (en la elección de la clase y en el cuadro de la partida nueva) */
static const char *const k_class_desc[NUM_CLASSES][2] = {
    { "Pega duro", "y grita" },
    { "Arcos y", "jabalinas" },
    { "Fuego, rayos", "y frío" },
    { "Auras", "y escudo" },
    { "Maldiciones", "y muertos" },
    { "Garras, trampas", "y sombras" },
    { "Bestias, fuego", "y viento" },
    { "Demonios, maleficios", "y caos" },
};
static const char *const k_diffs[3] = { "NORMAL", "PESADILLA", "INFIERNO" };

static void read_slots(void)
{
    for (int i = 0; i < SAVE_SLOTS; i++)
        game_slot_info(i + 1, &s_info[i]);
}

int menu_is_main(void)
{
    return s_state == MS_MAIN;
}

const char *menu_movie(void)
{
    return cinema_name(s_movie_sel);
}

/* Las imágenes del menú, en el hilo de trabajos mientras carga el juego */
void menu_preload(void)
{
    static const char *const k_images[] = {
        "frontend/gameselectscreenexp", "frontend/d2logoblackleft", "frontend/d2logoblackright",
        "frontend/d2logofireleft", "frontend/d2logofireright", "frontend/widebuttonblank", "cursor/pentspin",
        "charselect/characterselectscreenexp", "charselect/charselectbox", "frontend/cinematicsselectionexp",
    };
    for (unsigned k = 0; k < sizeof k_images / sizeof k_images[0]; k++)
        ui_preload(k_images[k]);
}

void menu_open(void)
{
    pal_load_menu();
    s_ticks = 0;
    s_state = MS_MAIN;
    game_previews_changed();
    read_slots();
    audio_stop_all();     /* se callan los efectos y el ambiente de la partida */
    audio_environment(0);
    audio_music("music_options");
}

static void click(void)
{
    audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
}

/* El personaje elegido da un paso al frente, con su sonido */
static void class_sound(int cls)
{
    char name[48];
    snprintf(name, sizeof name, "cursor_%s_select", game_class_voice(cls));
    audio_play_name(name, AUDIO_NOPOS, AUDIO_NOPOS);
}

int menu_players(void)
{
    return s_players;
}

const int *menu_slots(void)
{
    return s_slots;
}

const int *menu_classes(void)
{
    return s_classes;
}

int menu_difficulty(void)
{
    return s_diff_sel;
}

int menu_terror_zones(void)
{
    return s_terror;
}

const char *menu_language(void)
{
    return lang_code(s_lang_sel);
}

int menu_mode(void)
{
    return s_eco_game;
}

const char *menu_net_ip(void)
{
    return s_net_ip;
}

int menu_net_difficulty(void)
{
    return s_net_diff;
}

void menu_net_lobby(void)
{
    s_state = MS_LOBBY;
    s_net_diff = 0;
}

static void note(const char *text)
{
    snprintf(s_note, sizeof s_note, "%s", tr(text));
    s_note_ticks = 2 * 25;
}

void menu_net_failed(const char *why)
{
    note(why);
    s_note_ticks = 4 * 25;
    s_state = MS_NET;
}

/* Ya eligieron todos (s_slots y s_classes): la dificultad, la red o a jugar */
static MenuResult picks_done(void)
{
    if (s_mode == MODE_ECO) /* el Eco del Odio: la dificultad la da el nivel de los personajes */
        return MENU_START;
    if (s_mode == MODE_NET_HOST || s_mode == MODE_NET_JOIN) /* (la dificultad, en la sala de espera) */
        return s_mode == MODE_NET_HOST ? MENU_NET_HOST : MENU_NET_JOIN;
    /* La dificultad, si todos tienen abierta otra además de Normal (como el
       original: Pesadilla al vencer a Baal en Normal, Infierno en Pesadilla).
       Arranca en la más alta */
    s_diff_max = 2;
    for (int i = 0; i < s_players; i++) {
        const SlotInfo *info = s_slots[i] >= 1 && s_slots[i] <= SAVE_SLOTS ? &s_info[s_slots[i] - 1] : NULL;
        int m = info && info->used ? info->diff_max : 0;
        s_diff_max = m < s_diff_max ? m : s_diff_max;
    }
    s_diff_sel = s_diff_max;
    if (s_diff_max > 0) {
        s_state = MS_DIFF;
        return MENU_NONE;
    }
    return MENU_START;
}

/* El jugador que estaba eligiendo ya tiene partida: sigue el otro o se arranca */
static MenuResult picked(int slot, int cls)
{
    s_slots[s_picking] = slot;
    s_classes[s_picking] = cls;
    if (s_picking + 1 < s_players) {
        s_picking++;
        s_state = MS_SLOTS;
        return MENU_NONE;
    }
    return picks_done();
}

/* Se entra a las partidas: con dos jugadores, un cursor para cada uno */
static void slots_enter(void)
{
    s_picking = 0;
    /* (con un joystick, o solo el teclado, como antes: de a uno, así un solo control elige por los dos;
       D2_DUAL_PICK=1 lo fuerza, para las pruebas) */
    s_dual = s_players == 2 && (input_num_pads() >= 2 || getenv("D2_DUAL_PICK"));
    s_pk[0].sel = s_slot_sel;
    s_pk[1].sel = s_slot_sel ^ 1; /* (el otro, en la otra columna) */
    s_pk[0].stage = s_pk[1].stage = PK_SLOT;
    s_state = MS_SLOTS;
}

/* ¿Esa partida (0..5) ya la tomó el jugador k? (la eligió o está creando ahí su personaje) */
static int pk_taken(int k, int sel)
{
    return s_pk[k].stage != PK_SLOT && s_pk[k].sel == sel;
}

static void note_player(const char *fmt, int player)
{
    snprintf(s_note, sizeof s_note, tr(fmt), player);
    s_note_ticks = 2 * 25;
}

/* La pantalla de los dos a la vez: lo de cada jugador con lo suyo (in->player[k]) */
static MenuResult slots_dual_tick(const InputState *in)
{
    if (input_num_pads() < 2 && !getenv("D2_DUAL_PICK")) { /* (un joystick era el eco de otro, o se desconectó: de a uno) */
        s_dual = 0;
        s_picking = 0;
        s_slot_sel = s_pk[0].sel;
        return MENU_NONE;
    }
    for (int k = 0; k < 2; k++) {
        const PlayerInput *p = &in->player[k];
        int other = 1 - k, *sel = &s_pk[k].sel;
        const SlotInfo *info = &s_info[*sel];
        switch (s_pk[k].stage) {
        case PK_SLOT:
            if (p->back) { /* (afuera, como con uno) */
                click();
                if (s_mode == MODE_NET_JOIN) {
                    net_discover_start();
                    s_state = MS_FIND;
                } else {
                    s_state = MS_PLAY;
                }
                return MENU_NONE;
            }
            if (p->nav_left || p->nav_right)
                *sel ^= 1;
            if (p->nav_up)
                *sel = (*sel + SAVE_SLOTS - 2) % SAVE_SLOTS;
            if (p->nav_down)
                *sel = (*sel + 2) % SAVE_SLOTS;
            info = &s_info[*sel];
            if (p->drop && info->used && !pk_taken(other, *sel)) {
                click();
                s_slot_sel = *sel;
                s_delete_sel = 1; /* arranca en "no", por las dudas */
                s_state = MS_DELETE;
                return MENU_NONE;
            }
            if (!p->action)
                break;
            if (pk_taken(other, *sel)) {
                note_player("Ese personaje ya lo eligió el jugador %d", other + 1);
            } else if (!info->used && s_eco_game) {
                note("En el Eco del Odio se juega con un personaje que ya existe");
            } else if (!info->used) {
                click();
                s_pk[k].stage = PK_CLASS;
                s_pk[k].cls = 0;
            } else {
                class_sound(info->cls);
                s_pk[k].cls = info->cls;
                s_pk[k].stage = PK_READY;
            }
            break;
        case PK_CLASS: /* la clase de la nueva, en su cuadro: izquierda y derecha (o arriba y abajo) */
            if (p->nav_left || p->nav_up)
                s_pk[k].cls = (s_pk[k].cls + NUM_CLASSES - 1) % NUM_CLASSES;
            if (p->nav_right || p->nav_down)
                s_pk[k].cls = (s_pk[k].cls + 1) % NUM_CLASSES;
            if (p->back) {
                click();
                s_pk[k].stage = PK_SLOT;
            } else if (p->action) {
                class_sound(s_pk[k].cls);
                s_pk[k].stage = PK_READY;
            }
            break;
        default: /* listo: B lo deja elegir de nuevo */
            if (p->back) {
                click();
                s_pk[k].stage = PK_SLOT;
            }
            break;
        }
    }
    if (s_pk[0].stage != PK_READY || s_pk[1].stage != PK_READY)
        return MENU_NONE;
    for (int k = 0; k < 2; k++) {
        s_slots[k] = s_pk[k].sel + 1;
        s_classes[k] = s_pk[k].cls;
    }
    s_picking = 1;
    return picks_done();
}

static int taken_by_other(int slot)
{
    for (int i = 0; i < s_picking; i++)
        if (s_slots[i] == slot)
            return 1;
    return 0;
}

MenuResult menu_tick(const InputState *in)
{
    s_ticks++;
    if (s_note_ticks > 0)
        s_note_ticks--;
    int back = in->menu_back || in->escape;
    switch (s_state) {
    case MS_MAIN:
        if (in->escape)
            return MENU_QUIT;
        if (in->menu_up)
            s_sel = (s_sel + NUM_ITEMS - 1) % NUM_ITEMS;
        if (in->menu_down)
            s_sel = (s_sel + 1) % NUM_ITEMS;
        if (in->confirm || in->start) {
            click();
            if (s_sel == ITEM_QUIT)
                return MENU_QUIT;
            if (s_sel == ITEM_CINEMA) {
                s_movie_sel = 0;
                s_state = MS_CINEMA;
                return MENU_NONE;
            }
            if (s_sel == ITEM_CONTROLS) {
                controls_open();
                s_state = MS_CONTROLS;
                return MENU_NONE;
            }
            if (s_sel == ITEM_LANG) {
                s_lang_sel = lang_current();
                s_state = MS_LANG;
                return MENU_NONE;
            }
            if (s_sel == ITEM_NET) {
                s_count_sel = s_net_players - 1;
                s_state = MS_NET_PLAYERS;
                return MENU_NONE;
            }
            s_players = s_sel + 1; /* uno o dos: después, la historia o el Eco */
            s_play_net = 0;
            s_play_sel = 0;
            s_state = MS_PLAY;
        }
        return MENU_NONE;

    case MS_PLAY: /* la historia o el Eco del Odio (de uno o dos jugadores, o de la partida en red que se crea) */
        if (in->menu_up)
            s_play_sel = (s_play_sel + 2) % 3;
        if (in->menu_down)
            s_play_sel = (s_play_sel + 1) % 3;
        if (back || ((in->confirm || in->start) && s_play_sel == 2)) {
            click();
            s_state = s_play_net ? MS_NET : MS_MAIN;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            s_eco_game = s_play_sel == 1;
            s_mode = s_play_net ? MODE_NET_HOST : s_eco_game ? MODE_ECO : MODE_STORY;
            read_slots();
            slots_enter();
        }
        return MENU_NONE;

    case MS_NET_PLAYERS: /* la red local: cuántos juegan en esta máquina */
        if (in->menu_up)
            s_count_sel = (s_count_sel + 2) % 3;
        if (in->menu_down)
            s_count_sel = (s_count_sel + 1) % 3;
        if (back || ((in->confirm || in->start) && s_count_sel == 2)) {
            click();
            s_state = MS_MAIN;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            s_net_players = s_count_sel + 1;
            s_net_sel = 0;
            s_state = MS_NET;
        }
        return MENU_NONE;

    case MS_SLOTS: {
        if (s_dual)
            return slots_dual_tick(in);
        /* Dos columnas de tres, como la pantalla de selección del original */
        if (in->menu_left || in->menu_right)
            s_slot_sel ^= 1;
        if (in->menu_up)
            s_slot_sel = (s_slot_sel + SAVE_SLOTS - 2) % SAVE_SLOTS;
        if (in->menu_down)
            s_slot_sel = (s_slot_sel + 2) % SAVE_SLOTS;
        const SlotInfo *info = &s_info[s_slot_sel];
        if (back) {
            click();
            if (s_picking > 0)
                s_picking--;
            else if (s_mode == MODE_NET_JOIN) { /* (a la lista, buscando otra vez) */
                net_discover_start();
                s_state = MS_FIND;
            } else
                s_state = MS_PLAY;
            return MENU_NONE;
        }
        if (in->menu_delete && info->used && !taken_by_other(s_slot_sel + 1)) {
            click();
            s_delete_sel = 1; /* arranca en "no", por las dudas */
            s_state = MS_DELETE;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            if (taken_by_other(s_slot_sel + 1)) {
                note("Ese personaje ya lo eligió el jugador 1");
                return MENU_NONE;
            }
            if (!info->used && s_eco_game) {
                note("En el Eco del Odio se juega con un personaje que ya existe");
                return MENU_NONE;
            }
            if (!info->used) {
                click();
                s_class_sel = 0;
                s_state = MS_CLASS;
                return MENU_NONE;
            }
            class_sound(info->cls);
            return picked(s_slot_sel + 1, info->cls);
        }
        return MENU_NONE;
    }

    case MS_CLASS: {
        /* Izquierda y derecha, de a una; arriba y abajo, de fila (como se dibujan) */
        int per_row = NUM_CLASSES <= 5 ? NUM_CLASSES : (NUM_CLASSES + 1) / 2;
        if (in->menu_left || (per_row == NUM_CLASSES && in->menu_up))
            s_class_sel = (s_class_sel + NUM_CLASSES - 1) % NUM_CLASSES;
        if (in->menu_right || (per_row == NUM_CLASSES && in->menu_down))
            s_class_sel = (s_class_sel + 1) % NUM_CLASSES;
        if (per_row < NUM_CLASSES && (in->menu_up || in->menu_down)) {
            int col = s_class_sel % per_row, row = s_class_sel / per_row ^ 1;
            s_class_sel = row * per_row + col < NUM_CLASSES ? row * per_row + col : NUM_CLASSES - 1;
        }
        if (back) {
            click();
            s_state = MS_SLOTS;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            class_sound(s_class_sel);
            return picked(s_slot_sel + 1, s_class_sel);
        }
        return MENU_NONE;
    }

    case MS_CINEMA: {
        /* Las que no se vieron todavía no se pueden elegir (las saltea) */
        int n = cinema_count() + 1, step = in->menu_up ? -1 : in->menu_down ? 1 : 0;
        for (int k = 0; step && k < n; k++) {
            s_movie_sel = (s_movie_sel + step + n) % n;
            if (s_movie_sel == n - 1 || cinema_seen(s_movie_sel))
                break;
        }
        if (back || ((in->confirm || in->start) && s_movie_sel == n - 1)) {
            click();
            s_state = MS_MAIN;
            return MENU_NONE;
        }
        if ((in->confirm || in->start) && cinema_seen(s_movie_sel)) {
            click();
            return MENU_MOVIE;
        }
        return MENU_NONE;
    }

    case MS_CONTROLS:
        if (!controls_tick(in))
            s_state = MS_MAIN;
        return MENU_NONE;

    case MS_NET: /* la red local: crear la partida (después, la historia o el Eco) o unirse a una */
        if (in->menu_up)
            s_net_sel = (s_net_sel + 2) % 3;
        if (in->menu_down)
            s_net_sel = (s_net_sel + 1) % 3;
        if (back || ((in->confirm || in->start) && s_net_sel == 2)) {
            click();
            s_count_sel = s_net_players - 1;
            s_state = MS_NET_PLAYERS;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            s_players = s_net_players;
            s_picking = 0;
            read_slots();
            if (s_net_sel == 0) {
                s_play_net = 1;
                s_play_sel = 0;
                s_state = MS_PLAY;
            } else {
                s_mode = MODE_NET_JOIN;
                s_net_found_n = s_net_find_sel = 0;
                net_discover_start();
                s_state = MS_FIND;
            }
        }
        return MENU_NONE;

    case MS_FIND: { /* las partidas de la red local (se pregunta cada dos segundos), y el botón de volver */
        if (s_ticks % 50 == 0)
            net_discover_start();
        s_net_found_n = net_discover_poll(s_net_found, s_net_found_players, s_net_found_mode, FIND_SHOWN);
        int n = s_net_found_n + 1; /* (el último: volver) */
        if (s_net_find_sel >= n)
            s_net_find_sel = n - 1;
        if (in->menu_up)
            s_net_find_sel = (s_net_find_sel + n - 1) % n;
        if (in->menu_down)
            s_net_find_sel = (s_net_find_sel + 1) % n;
        if (back || ((in->confirm || in->start) && s_net_find_sel == n - 1)) {
            click();
            net_discover_stop();
            s_state = MS_NET;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            snprintf(s_net_ip, sizeof s_net_ip, "%s", s_net_found[s_net_find_sel]);
            s_eco_game = s_net_found_mode[s_net_find_sel] == 1;
            net_discover_stop();
            slots_enter();
        }
        return MENU_NONE;
    }

    case MS_LOBBY: { /* la sala de espera: los que se unieron; el anfitrión elige la dificultad y empieza */
        net_poll();
        if (net_error()) {
            note(net_error());
            s_note_ticks = 4 * 25;
            net_close();
            s_state = MS_NET;
            return MENU_NONE;
        }
        if (net_started())
            return MENU_NET_START;
        int cls[MAX_PLAYERS], lvl[MAX_PLAYERS], own[MAX_PLAYERS], dmax[MAX_PLAYERS];
        int n = net_lobby(cls, lvl, own, dmax, MAX_PLAYERS), top = 2;
        for (int i = 0; i < n; i++)
            top = dmax[i] < top ? dmax[i] : top;
        if (s_net_diff > top)
            s_net_diff = top;
        if (net_role() == NET_HOST && (in->menu_left || in->menu_right) && top > 0 && !s_eco_game) {
            click();
            s_net_diff = (s_net_diff + (in->menu_right ? 1 : top)) % (top + 1);
        }
        if (back) {
            click();
            net_close();
            s_state = MS_NET;
            return MENU_NONE;
        }
        if (net_role() == NET_HOST && (in->confirm || in->start)) {
            if (n < 2) {
                note("Hace falta otra máquina conectada");
                return MENU_NONE;
            }
            click();
            return MENU_NET_GO;
        }
        return MENU_NONE;
    }

    case MS_LANG: /* otro idioma: el juego vuelve a arrancar en ese (main.c) */
        if (in->menu_up)
            s_lang_sel = (s_lang_sel + LANG_COUNT - 1) % LANG_COUNT;
        if (in->menu_down)
            s_lang_sel = (s_lang_sel + 1) % LANG_COUNT;
        if (back) {
            click();
            s_state = MS_MAIN;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            s_state = MS_MAIN;
            return s_lang_sel == lang_current() ? MENU_NONE : MENU_RESTART;
        }
        return MENU_NONE;

    case MS_DIFF:
        if (in->menu_up)
            s_diff_sel = (s_diff_sel + s_diff_max) % (s_diff_max + 1);
        if (in->menu_down)
            s_diff_sel = (s_diff_sel + 1) % (s_diff_max + 1);
        if (in->menu_left || in->menu_right) {
            click();
            s_terror = !s_terror;
        }
        if (back) {
            click();
            s_state = MS_SLOTS;
            s_pk[0].stage = s_pk[1].stage = PK_SLOT; /* (con los dos a la vez: eligen de nuevo) */
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            return MENU_START;
        }
        return MENU_NONE;

    case MS_DELETE:
        if (in->menu_up || in->menu_down || in->menu_left || in->menu_right)
            s_delete_sel ^= 1;
        if (back) {
            click();
            s_state = MS_SLOTS;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            if (s_delete_sel == 0) {
                game_delete_slot(s_slot_sel + 1);
                read_slots();
            }
            s_state = MS_SLOTS;
        }
        return MENU_NONE;
    }
    return MENU_NONE;
}

/* ---- Dibujo ---- */

/* Los pentagramas a los lados de lo elegido: el de frente (cuadro 0 de
   pentspin), plano, girando sobre sí mismo. Centrados en la altura `cy` y
   a la misma distancia del texto (de ancho text_w, centrado en cx). */
static void draw_pentagrams(int cx, int cy, int text_w, int size)
{
    float a = (float)SDL_GetTicks() * 0.0015f; /* una vuelta cada 4 segundos */
    int gap = text_w / 2 + size / 2 + 12;
    ui_frame_rotated("cursor/pentspin", 0, cx - gap, cy, size, a);
    ui_frame_rotated("cursor/pentspin", 0, cx + gap, cy, size, -a);
}

/* Botón ancho del menú original (dos pedazos: 256 + 16) con el texto encima */
static void draw_wide_button(int x, int y, const char *text, int selected)
{
    ui_frame("frontend/widebuttonblank", selected ? 2 : 0, x, y);
    ui_frame("frontend/widebuttonblank", selected ? 3 : 1, x + 256, y);
    int f = font_fit(text, FONT_EXOCET_ENGRAVED, 236), w = font_width(text, f); /* (entre los pentagramas) */
    font_draw_centered(x + 136, y + (35 - font_height(f)) / 2 + 1, text, f,
                       COL(R_GRAY, 5)); /* grabado en la piedra; lo elegido lo marcan los pentagramas */
    if (selected) /* el botón mide 272 x 35: en el medio de la altura */
        draw_pentagrams(x + 136, y + 18, w, 26);
}

static void draw_logo(void)
{
    /* La mitad negra y el fuego de cada lado, animados como en el original */
    int f = (int)(s_ticks / 2) % 30;
    ui_frame_anchor("frontend/d2logoblackleft", f, 400, 120, BLEND_75);
    ui_frame_anchor("frontend/d2logoblackright", f, 400, 120, BLEND_75);
    ui_frame_anchor("frontend/d2logofireleft", f, 400, 120, BLEND_ADD);
    ui_frame_anchor("frontend/d2logofireright", f, 400, 120, BLEND_ADD);
}

static void draw_main(void)
{
    ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
    draw_logo();
    for (int i = 0; i < NUM_ITEMS; i++)
        draw_wide_button(264, 262 + i * 35, k_items[i], i == s_sel);

    char buf[64];
    snprintf(buf, sizeof buf, tr("Joysticks conectados: %d"), input_num_pads());
    font_draw_centered(SCREEN_W / 2, SCREEN_H - 50, buf, 2, COL(R_GRAY, 13));
    if (s_sel == 1) {
        font_draw_centered(SCREEN_W / 2, SCREEN_H - 30, "Jugador 1: el primer joystick que se use o WASD   -   Jugador 2: el segundo o flechas",
                           1, COL(R_STONE, 12));
    }
}

/* Lugares de la lista: 2 columnas de 3 filas dentro del marco */
static void slot_box(int i, int *x, int *y)
{
    *x = 37 + (i % 2) * 272;
    *y = 133 + (i / 2) * 95;
}

/* Las partidas con los dos a la vez: el cursor de cada jugador (J1 dorado, J2 azul), LISTO, y en la vacía que se
   está creando, la clase que se elige */
static void draw_slots_dual(void)
{
    ui_grid("charselect/characterselectscreenexp", 0, 12, 4, 0, 0);
    char text[96];
    font_draw_centered(317, 34, "JUGADORES: ELIJAN SUS PERSONAJES", FONT_EXOCET, COL(R_GOLD, 15));
    if (s_eco_game)
        font_draw_centered(317, 90, "ECO DEL ODIO", FONT_EXOCET, COL(R_RED, 14));
    int bw = 272, bh = 90, w0, h0, w1, h1;
    if (ui_frame_size("charselect/charselectbox", 0, &w0, &h0) && ui_frame_size("charselect/charselectbox", 1, &w1, &h1)) {
        bw = 256 + w1;
        bh = h0;
    }
    static const int k_col[2] = { R_GOLD, R_BLUE };
    for (int i = 0; i < SAVE_SLOTS; i++) {
        int x, y, owner = -1, mine = 0;
        slot_box(i, &x, &y);
        const SlotInfo *info = &s_info[i];
        for (int k = 0; k < 2; k++) {
            mine |= s_pk[k].sel == i;
            if (pk_taken(k, i))
                owner = k;
        }
        if (mine) {
            ui_frame("charselect/charselectbox", 0, x, y);
            ui_frame("charselect/charselectbox", 1, x + 256, y);
        }
        if (owner >= 0 && !info->used) { /* la nueva: la clase que se elige (o la elegida) */
            int c = s_pk[owner].cls, choosing = s_pk[owner].stage == PK_CLASS;
            game_draw_class_hero(c, x + 44, y + 84, (int)s_ticks);
            if (choosing)
                snprintf(text, sizeof text, "<  %s  >", game_class_name(c));
            else
                snprintf(text, sizeof text, "%s", game_class_name(c));
            font_draw(x + 92, y + 12, text, 2, COL(k_col[owner], 15));
            font_draw(x + 92, y + 34, k_class_desc[c][0], 1, COL(R_GRAY, 14));
            font_draw(x + 92, y + 46, k_class_desc[c][1], 1, COL(R_GRAY, 14));
            if (choosing)
                font_draw(x + 92, y + 64, "A: crear     B: volver", 1, COL(R_STONE, 13));
        } else if (info->used) {
            game_draw_slot_hero(i + 1, x + 44, y + 84, (int)s_ticks);
            font_draw(x + 92, y + 16, game_class_name(info->cls), 2, COL(R_GOLD, 15));
            snprintf(text, sizeof text, tr("Nivel %d  %s"), info->level, game_class_name(info->cls));
            font_draw(x + 92, y + 36, text, 2, COL(R_GRAY, 15));
            if (owner >= 0)
                snprintf(text, sizeof text, tr("Lo eligió el jugador %d"), owner + 1);
            else
                snprintf(text, sizeof text, tr("Partida %d"), i + 1);
            font_draw(x + 92, y + 56, text, 2, owner >= 0 ? COL(k_col[owner], 15) : COL(R_GRASS, 13));
            if (s_eco_game) {
                if (info->eco_best > 0)
                    snprintf(text, sizeof text, tr("Mejor oleada: %d"), info->eco_best);
                else
                    snprintf(text, sizeof text, "%s", tr("Sin corridas en el Eco"));
                font_draw(x + 92, y + 74, text, 1, COL(R_RED, 13));
            }
        } else {
            snprintf(text, sizeof text, tr("Partida %d vacía"), i + 1);
            font_draw(x + 92, y + 26, text, 2, COL(R_STONE, 13));
            font_draw(x + 92, y + 46, "A: crear un personaje", 1, COL(R_STONE, 12));
        }
        /* Los cursores: el marco de cada uno (si están los dos, el del J2 adentro) y su marca arriba a la derecha */
        int tag_x = x + bw - 8;
        for (int k = 1; k >= 0; k--) {
            if (s_pk[k].sel != i)
                continue;
            int inset = s_pk[1 - k].sel == i && k == 1 ? 4 : 0;
            uint8_t c = COL(k_col[k], 15);
            fb_fill_rect(x + 2 + inset, y + 2 + inset, bw - 4 - 2 * inset, 2, c);
            fb_fill_rect(x + 2 + inset, y + bh - 4 - inset, bw - 4 - 2 * inset, 2, c);
            fb_fill_rect(x + 2 + inset, y + 2 + inset, 2, bh - 4 - 2 * inset, c);
            fb_fill_rect(x + bw - 4 - inset, y + 2 + inset, 2, bh - 4 - 2 * inset, c);
            char tag[32], label[48];
            snprintf(tag, sizeof tag, tr("J%d"), k + 1);
            if (s_pk[k].stage == PK_READY)
                snprintf(label, sizeof label, "%s  %s", tag, tr("LISTO"));
            else
                snprintf(label, sizeof label, "%s", tag);
            int lw = font_width(label, 2);
            font_draw(tag_x - lw, y + 8, label, 2, c);
            tag_x -= lw + 10;
        }
    }
    font_draw_centered(317, 530, "A: jugar     X: borrar     B: volver", 2, COL(R_GRAY, 14));
    int ready = (s_pk[0].stage == PK_READY) + (s_pk[1].stage == PK_READY);
    if (ready == 1 && s_note_ticks <= 0) { /* (el aviso va en el mismo lugar) */
        snprintf(text, sizeof text, tr("Esperando al jugador %d"), s_pk[0].stage == PK_READY ? 2 : 1);
        font_draw_centered(317, 112, text, 2, COL(R_GRAY, 15));
    }
    if (s_note_ticks > 0)
        font_draw_centered(317, 112, s_note, font_fit(s_note, 2, 600), COL(R_RED, 13));
}

static void draw_slots(void)
{
    if (s_dual) {
        draw_slots_dual();
        return;
    }
    ui_grid("charselect/characterselectscreenexp", 0, 12, 4, 0, 0);
    char text[96];
    if (s_players > 1)
        snprintf(text, sizeof text, tr("JUGADOR %d: ELEGÍ TU PERSONAJE"), s_picking + 1);
    else
        snprintf(text, sizeof text, "%s", tr("ELEGÍ TU PERSONAJE"));
    font_draw_centered(317, 34, text, FONT_EXOCET, COL(R_GOLD, 15));
    if (s_eco_game) /* (en rojo, el del odio) */
        font_draw_centered(317, 90, "ECO DEL ODIO", FONT_EXOCET, COL(R_RED, 14));

    for (int i = 0; i < SAVE_SLOTS; i++) {
        int x, y;
        slot_box(i, &x, &y);
        const SlotInfo *info = &s_info[i];
        if (i == s_slot_sel) {
            ui_frame("charselect/charselectbox", 0, x, y);
            ui_frame("charselect/charselectbox", 1, x + 256, y);
        }
        int dim = taken_by_other(i + 1);
        if (info->used) {
            game_draw_slot_hero(i + 1, x + 44, y + 84, (int)s_ticks);
            font_draw(x + 92, y + 16, game_class_name(info->cls), 2, dim ? COL(R_STONE, 9) : COL(R_GOLD, 15));
            snprintf(text, sizeof text, tr("Nivel %d  %s"), info->level, game_class_name(info->cls));
            font_draw(x + 92, y + 36, text, 2, dim ? COL(R_STONE, 9) : COL(R_GRAY, 15));
            snprintf(text, sizeof text, dim ? tr("Lo eligió el jugador 1") : tr("Partida %d"), i + 1);
            font_draw(x + 92, y + 56, text, 2, dim ? COL(R_STONE, 9) : COL(R_GRASS, 13));
            if (s_eco_game) {
                if (info->eco_best > 0)
                    snprintf(text, sizeof text, tr("Mejor oleada: %d"), info->eco_best);
                else
                    snprintf(text, sizeof text, "%s", tr("Sin corridas en el Eco"));
                font_draw(x + 92, y + 74, text, 1, dim ? COL(R_STONE, 9) : COL(R_RED, 13));
            }
        } else {
            snprintf(text, sizeof text, tr("Partida %d vacía"), i + 1);
            font_draw(x + 92, y + 26, text, 2, COL(R_STONE, 13));
            font_draw(x + 92, y + 46, "A: crear un personaje", 1, COL(R_STONE, 12));
        }
    }
    font_draw_centered(317, 530, "A: jugar     X: borrar     B: volver", 2, COL(R_GRAY, 14));
    if (s_note_ticks > 0)
        font_draw_centered(317, 112, s_note, font_fit(s_note, 2, 600), COL(R_RED, 13));
}

/* Contorno del personaje elegido: doble línea dorada con las esquinas
   remarcadas (sin rellenar: se ve el personaje adentro) */
static void draw_select_outline(int x, int y, int w, int h)
{
    uint8_t outer = COL(R_GOLD, 15), inner = COL(R_GOLD, 8);
    fb_fill_rect(x, y, w, 1, outer);
    fb_fill_rect(x, y + h - 1, w, 1, outer);
    fb_fill_rect(x, y, 1, h, outer);
    fb_fill_rect(x + w - 1, y, 1, h, outer);
    fb_fill_rect(x + 2, y + 2, w - 4, 1, inner);
    fb_fill_rect(x + 2, y + h - 3, w - 4, 1, inner);
    fb_fill_rect(x + 2, y + 2, 1, h - 4, inner);
    fb_fill_rect(x + w - 3, y + 2, 1, h - 4, inner);
    const int c = 12; /* las esquinas, más gruesas */
    for (int k = 0; k < 2; k++) {
        int cx = k ? x + w - c : x;
        fb_fill_rect(cx, y, c, 3, outer);
        fb_fill_rect(cx, y + h - 3, c, 3, outer);
    }
    fb_fill_rect(x, y, 3, c, outer);
    fb_fill_rect(x + w - 3, y, 3, c, outer);
    fb_fill_rect(x, y + h - c, 3, c, outer);
    fb_fill_rect(x + w - 3, y + h - c, 3, c, outer);
}

static void draw_class_choice(void)
{
    ui_grid("charselect/characterselectscreenexp", 0, 12, 4, 0, 0);
    font_draw_centered(317, 34, "ELEGÍ LA CLASE DEL PERSONAJE", FONT_EXOCET, COL(R_GOLD, 15));
    const char *const(*desc)[2] = k_class_desc;
    /* Dentro del marco (de 46 a 591): hasta 5 en una fila; si son más, en
       dos filas (la de abajo centrada) */
    int per_row = NUM_CLASSES <= 5 ? NUM_CLASSES : (NUM_CLASSES + 1) / 2;
    int rows = (NUM_CLASSES + per_row - 1) / per_row, bw = 545 / per_row, two = rows > 1;
    for (int c = 0; c < NUM_CLASSES; c++) {
        int row = c / per_row, col = c % per_row;
        int in_row = row == rows - 1 ? NUM_CLASSES - row * per_row : per_row;
        int x = 46 + (545 - in_row * bw) / 2 + col * bw, y = two ? 116 + row * 174 : 160, cx = x + bw / 2;
        if (c == s_class_sel)
            draw_select_outline(x + 1, y + 8, bw - 2, two ? 168 : 178);
        game_draw_class_hero(c, cx, y + (two ? 108 : 120), (int)s_ticks);
        font_draw_centered(cx, y + (two ? 118 : 132), game_class_name(c), FONT_EXOCET,
                           c == s_class_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 13));
        font_draw_centered(cx, y + (two ? 140 : 154), desc[c][0], 1, COL(R_GRAY, 14));
        font_draw_centered(cx, y + (two ? 152 : 166), desc[c][1], 1, COL(R_GRAY, 14));
    }
    char text[48];
    snprintf(text, sizeof text, tr("Partida %d"), s_slot_sel + 1);
    font_draw_centered(317, 110, text, 2, COL(R_GRASS, 13));
    font_draw_centered(317, 530, "A: crear     B: volver", 2, COL(R_GRAY, 14));
}

/* El panel de cinemáticas de la expansión (cinematicsselectionexp: siete
   filas y el botón de abajo, 326x427), sobre la pantalla principal */
static void draw_cinema(void)
{
    ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
    draw_logo();
    const int x = (SCREEN_W - 326) / 2, y = 140;
    ui_frame("frontend/cinematicsselectionexp", 0, x, y);
    ui_frame("frontend/cinematicsselectionexp", 1, x + 256, y);
    ui_frame("frontend/cinematicsselectionexp", 2, x, y + 256);
    ui_frame("frontend/cinematicsselectionexp", 3, x + 256, y + 256);
    font_draw_centered(x + 163, y + 24, "ELEGÍ UNA CINEMÁTICA", FONT_EXOCET, COL(R_GOLD, 15));
    int n = cinema_count();
    for (int i = 0; i <= n; i++) {
        int last = i == n, cy = last ? y + 392 : y + 81 + i * 43; /* el centro de cada fila del dibujo */
        const char *text = last ? "CANCELAR" : cinema_title(i);
        int ok = last || cinema_seen(i), fy = cy - font_height(FONT_EXOCET) / 2;
        font_draw_centered(x + 163, fy, text, FONT_EXOCET,
                           !ok ? COL(R_STONE, 6) : i == s_movie_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 14));
        if (i == s_movie_sel)
            draw_pentagrams(x + 163, cy, font_width(text, FONT_EXOCET), 22);
    }
    font_draw_centered(SCREEN_W / 2, SCREEN_H - 30, "A: ver     B: volver     (las que todavía no se vieron no se pueden elegir)",
                       1, COL(R_GRAY, 13));
}

static void draw_delete(void)
{
    draw_slots();
    const SlotInfo *info = &s_info[s_slot_sel];
    ui_dark_box(150, 200, 334, 150);
    char text[96];
    snprintf(text, sizeof text, tr("¿Borrar %s de nivel %d?"), game_class_name(info->cls), info->level);
    font_draw_centered(317, 220, text, 3, COL(R_GOLD, 15));
    font_draw_centered(317, 250, "Se pierde todo: nivel, equipo y oro", 1, COL(R_GRAY, 14));
    static const char *const opts[2] = { "SÍ, BORRAR", "NO" };
    for (int i = 0; i < 2; i++) {
        int y = 280 + i * 30;
        font_draw_centered(317, y, opts[i], FONT_EXOCET, i == s_delete_sel ? COL(R_RED, 14) : COL(R_GRAY, 13));
        if (i == s_delete_sel)
            draw_pentagrams(317, y + font_height(FONT_EXOCET) / 2, font_width(opts[i], FONT_EXOCET), 22);
    }
}

/* Los idiomas, cada uno con su nombre en su idioma (las letras que no están
   en las fuentes del original salen de la .ttf del Resurrected) */
static void draw_languages(void)
{
    ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
    int h = 70 + LANG_COUNT * 24 + 40, y0 = (SCREEN_H - h) / 2;
    ui_dark_box(220, y0, 360, h);
    font_draw_centered(SCREEN_W / 2, y0 + 16, "IDIOMA", FONT_EXOCET, COL(R_GOLD, 15));
    for (int i = 0; i < LANG_COUNT; i++) {
        int y = y0 + 56 + i * 24;
        uint8_t c = i == s_lang_sel ? COL(R_GOLD, 15) : i == lang_current() ? COL(R_GRAY, 15) : COL(R_GRAY, 12);
        font_draw_centered(SCREEN_W / 2, y, lang_name(i), FONT_FORMAL, c);
        if (i == s_lang_sel)
            draw_pentagrams(SCREEN_W / 2, y + font_height(FONT_FORMAL) / 2, font_width(lang_name(i), FONT_FORMAL), 20);
    }
    /* Las voces y las cinemáticas son las del idioma que se importó */
    const char *inst = lang_installed();
    int ly = y0 + h - 34;
    if (inst[0] && strcmp(inst, lang_code(s_lang_sel)) != 0) {
        char buf[128];
        snprintf(buf, sizeof buf, tr("Las voces siguen en %s (el idioma que se importó)"), lang_name(lang_find(inst)));
        font_draw_centered(SCREEN_W / 2, ly, buf, 1, COL(R_STONE, 13));
    }
    font_draw_centered(SCREEN_W / 2, ly + 16, "A: elegir (el juego vuelve a arrancar)     B: volver", 1, COL(R_GRAY, 14));
}

/* La dificultad: las tres, con las cerradas apagadas */
static void draw_difficulty(void)
{
    draw_slots();
    ui_dark_box(150, 180, 334, 200);
    font_draw_centered(317, 196, "ELEGÍ LA DIFICULTAD", FONT_EXOCET, COL(R_GOLD, 15));
    for (int i = 0; i < 3; i++) {
        int y = 240 + i * 34, open = i <= s_diff_max;
        font_draw_centered(317, y, k_diffs[i], FONT_EXOCET,
                           !open ? COL(R_STONE, 6) : i == s_diff_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 13));
        if (i == s_diff_sel)
            draw_pentagrams(317, y + font_height(FONT_EXOCET) / 2, font_width(k_diffs[i], FONT_EXOCET), 22);
    }
    if (s_diff_max < 2)
        font_draw_centered(317, 334, "Se abre al vencer a Baal en la anterior", 1, COL(R_STONE, 12));
    /* (en violeta, el color del terror) */
    font_draw_centered(317, 348, s_terror ? "Izq. / der.: zonas de terror SÍ" : "Izq. / der.: zonas de terror NO", 1,
                       s_terror ? g_text_shift[11][COL(R_GRAY, 15)] : COL(R_STONE, 12));
    font_draw_centered(317, 362, "A: jugar     B: volver", 1, COL(R_GRAY, 14));
}

/* Un menú como el principal (el fondo, el logo y los botones de piedra con los pentagramas): arriba,
   lo que se eligió antes; abajo de los botones, qué es el marcado (renglones separados por \n) */
static void draw_choice(const char *title, uint8_t title_col, const char *const *opts, int n, int sel, const char *info)
{
    ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
    draw_logo();
    font_draw_centered(SCREEN_W / 2, 233, title, FONT_EXOCET, title_col);
    for (int i = 0; i < n; i++)
        draw_wide_button(264, 262 + i * 35, opts[i], i == sel);
    int y = 262 + n * 35 + 12;
    while (info && *info) {
        char line[160];
        size_t len = strcspn(info, "\n");
        snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), info);
        font_draw_centered(SCREEN_W / 2, y, line, 1, COL(R_GRAY, 14));
        y += 16;
        info += len + (info[len] == '\n');
    }
    font_draw_centered(SCREEN_W / 2, SCREEN_H - 30, "A: elegir     B: volver", 1, COL(R_GRAY, 13));
    if (s_note_ticks > 0)
        font_draw_centered(SCREEN_W / 2, SCREEN_H - 52, s_note, font_fit(s_note, 2, SCREEN_W - 20), COL(R_RED, 13));
}

/* La historia o el Eco del Odio (uno o dos jugadores, o la partida en red que se crea) */
static void draw_play(void)
{
    static const char *const k_info[3] = {
        "Los cinco actos de la historia, de Normal a Infierno.",
        "Oleadas sin fin en una arena al azar, cada una más difícil.\n"
        "Cada cinco oleadas, un jefe de la historia. Morir termina la corrida.\n"
        "La experiencia, el oro y los objetos vuelven a tu personaje.",
        NULL,
    };
    /* (los renglones se traducen de a uno: el texto de cada uno es el que está en las tablas) */
    char info[512] = "";
    if (k_info[s_play_sel]) {
        const char *s = k_info[s_play_sel];
        while (*s) {
            char line[160];
            size_t len = strcspn(s, "\n");
            snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), s);
            snprintf(info + strlen(info), sizeof info - strlen(info), "%s%s", info[0] ? "\n" : "", tr(line));
            s += len + (s[len] == '\n');
        }
    }
    char title[64];
    if (s_play_net)
        snprintf(title, sizeof title, "%s", tr("CREAR PARTIDA"));
    else
        snprintf(title, sizeof title, "%s", tr(s_players > 1 ? "DOS JUGADORES" : "UN JUGADOR"));
    draw_choice(title, COL(R_GOLD, 15), k_play_opts, 3, s_play_sel, info);
}

/* La red local: cuántos juegan en esta máquina */
static void draw_net_players(void)
{
    char info[256];
    snprintf(info, sizeof info, "%s\n%s\n%s", tr("¿Cuántos juegan en esta máquina?"),
             tr("Hasta 8 jugadores en varias máquinas de la misma red (1 o 2 en cada una)."),
             tr("Todas tienen que tener la misma versión del juego."));
    draw_choice(tr("RED LOCAL"), COL(R_GOLD, 15), k_count_opts, 3, s_count_sel, s_count_sel < 2 ? info : NULL);
}

/* La red local: crear la partida o unirse a una */
static void draw_net(void)
{
    static const char *const k_info[3] = {
        "Creás la partida: las otras máquinas de la red la encuentran solas.",
        "Buscás una partida que se creó en otra máquina de esta red.",
        NULL,
    };
    char title[96];
    snprintf(title, sizeof title, "%s: %s", tr("RED LOCAL"), tr(s_net_players > 1 ? "DOS JUGADORES" : "UN JUGADOR"));
    draw_choice(title, COL(R_GOLD, 15), k_net_opts, 3, s_net_sel, k_info[s_net_sel] ? tr(k_info[s_net_sel]) : NULL);
}

/* Las partidas que contestaron en la red local: un botón cada una (con su dirección) y volver */
static void draw_find(void)
{
    const char *opts[FIND_SHOWN + 1];
    for (int i = 0; i < s_net_found_n; i++)
        opts[i] = s_net_found[i];
    opts[s_net_found_n] = "VOLVER";
    char info[128];
    if (s_net_found_n == 0)
        snprintf(info, sizeof info, "%s", tr("Buscando partidas en la red..."));
    else if (s_net_find_sel < s_net_found_n)
        snprintf(info, sizeof info, tr(s_net_found_mode[s_net_find_sel] == 1 ? "Eco del Odio (jugadores: %d)" : "Historia (jugadores: %d)"),
                 s_net_found_players[s_net_find_sel]);
    else
        info[0] = '\0';
    draw_choice(tr("UNIRSE A UNA PARTIDA"), COL(R_GOLD, 15), opts, s_net_found_n + 1, s_net_find_sel, info);
}

/* La sala de espera: los jugadores de todas las máquinas y la dificultad */
static void draw_lobby(void)
{
    ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
    int host = net_role() == NET_HOST;
    ui_dark_box(150, 140, 500, 380);
    font_draw_centered(SCREEN_W / 2, 156, host ? "TU PARTIDA EN LA RED" : "SALA DE ESPERA", FONT_EXOCET, COL(R_GOLD, 15));
    if (s_eco_game) /* (en rojo, el del odio) */
        font_draw_centered(SCREEN_W / 2, 386, "ECO DEL ODIO", FONT_EXOCET, COL(R_RED, 14));
    char t[128];
    if (host) {
        snprintf(t, sizeof t, tr("Las otras máquinas la encuentran solas (esta es %s)"), net_my_ip());
        font_draw_centered(SCREEN_W / 2, 186, t, 1, COL(R_GRAY, 14));
    }
    static const uint8_t k_col[MAX_PLAYERS] = { R_GOLD, R_BLUE, R_GRASS, R_RED, R_CLOTH, R_SKIN, R_WOOD, R_GRAY };
    int cls[MAX_PLAYERS], lvl[MAX_PLAYERS], own[MAX_PLAYERS], dmax[MAX_PLAYERS];
    int n = net_lobby(cls, lvl, own, dmax, MAX_PLAYERS);
    for (int i = 0; i < n; i++) {
        snprintf(t, sizeof t, tr("J%d  %s  nivel %d  (máquina %d)"), i + 1, game_class_name(cls[i]), lvl[i], own[i] + 1);
        font_draw_centered(SCREEN_W / 2, 210 + i * 24, t, 2, COL(k_col[i], 14));
    }
    if (host && s_eco_game) { /* (en el Eco, la dificultad la da el nivel de los personajes) */
        font_draw_centered(SCREEN_W / 2, 424, "La dificultad la da el nivel de los personajes", 1, COL(R_GRAY, 14));
        font_draw_centered(SCREEN_W / 2, 470, "A: empezar     B: cerrar", 1, COL(R_GRAY, 14));
    } else if (host) {
        static const char *const k_d[3] = { "NORMAL", "PESADILLA", "INFIERNO" };
        snprintf(t, sizeof t, "< %s >", tr(k_d[s_net_diff]));
        font_draw_centered(SCREEN_W / 2, 420, t, FONT_EXOCET, COL(R_GOLD, 15));
        font_draw_centered(SCREEN_W / 2, 470, "A: empezar     Izq. / der.: dificultad     B: cerrar", 1, COL(R_GRAY, 14));
    } else {
        font_draw_centered(SCREEN_W / 2, 430, "Esperando que el anfitrión empiece...", 2, COL(R_GRAY, 14));
        font_draw_centered(SCREEN_W / 2, 470, "B: salir", 1, COL(R_GRAY, 14));
    }
    if (s_note_ticks > 0)
        font_draw_centered(SCREEN_W / 2, 446, s_note, 2, COL(R_RED, 13));
}

void menu_render(void)
{
    clip_reset();
    fb_clear(0);
    switch (s_state) {
    case MS_MAIN:   draw_main(); break;
    case MS_SLOTS:  draw_slots(); break;
    case MS_CLASS:  draw_class_choice(); break;
    case MS_DELETE: draw_delete(); break;
    case MS_CINEMA: draw_cinema(); break;
    case MS_DIFF:   draw_difficulty(); break;
    case MS_LANG:   draw_languages(); break;
    case MS_PLAY:   draw_play(); break;
    case MS_NET_PLAYERS: draw_net_players(); break;
    case MS_NET:    draw_net(); break;
    case MS_FIND:   draw_find(); break;
    case MS_LOBBY:  draw_lobby(); break;
    case MS_CONTROLS:
        ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
        controls_draw();
        break;
    }
}
