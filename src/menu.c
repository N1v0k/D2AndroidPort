#include "menu.h"

#include "audio.h"
#include "cinema.h"
#include "controls.h"
#include "font.h"
#include "game.h"
#include "ui.h"
#include "video.h"
#include "lang.h"

#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Menús con los gráficos del original:
   - Principal: el fondo de la expansión, el logo con fuego y los botones.
   - Elegir personaje: la pantalla de selección con las seis partidas.
   - Clase del personaje nuevo, y confirmar el borrado. */

enum { MS_MAIN, MS_SLOTS, MS_CLASS, MS_DELETE, MS_CINEMA, MS_DIFF, MS_CONTROLS, MS_LANG, MS_ECO };

/* Como el original: las cinemáticas entre jugar y salir (y los controles y el idioma); el
   Eco del Odio (docs/plan-eco-del-odio.md), después de la historia */
enum { ITEM_ONE, ITEM_TWO, ITEM_ECO, ITEM_CINEMA, ITEM_CONTROLS, ITEM_LANG, ITEM_QUIT };
static const char *const k_items[] = { "UN JUGADOR", "DOS JUGADORES", "ECO DEL ODIO", "CINEMÁTICAS", "CONTROLES", "IDIOMA", "SALIR" };
#define NUM_ITEMS ((int)(sizeof k_items / sizeof k_items[0]))

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
static char     s_note[64];     /* aviso corto ("Ya la eligió el jugador 1") */
static int      s_note_ticks;
static int      s_movie_sel;    /* en el panel de cinemáticas (la última fila es "Cancelar") */
static int      s_diff_sel;     /* la dificultad elegida (0 Normal, 1 Pesadilla, 2 Infierno) */
static int      s_diff_max;     /* la más alta que tienen abierta todos los elegidos */
static int      s_terror = 1;   /* las zonas de terror (como la casilla de crear partida del Resurrected) */
static int      s_lang_sel;     /* en la lista de idiomas */
static int      s_mode;         /* 0 la historia, 1 el Eco del Odio */
static int      s_eco_sel;      /* en el panel del Eco: 0 un jugador, 1 dos */
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
    return s_mode;
}

static void note(const char *text)
{
    snprintf(s_note, sizeof s_note, "%s", tr(text));
    s_note_ticks = 2 * 25;
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
    if (s_mode) /* el Eco del Odio: la dificultad la da el nivel de los personajes */
        return MENU_START;
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
            if (s_sel == ITEM_ECO) {
                s_eco_sel = 0;
                s_state = MS_ECO;
                return MENU_NONE;
            }
            s_mode = 0;
            s_players = s_sel + 1;
            s_picking = 0;
            s_state = MS_SLOTS;
            read_slots();
        }
        return MENU_NONE;

    case MS_SLOTS: {
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
            else
                s_state = s_mode ? MS_ECO : MS_MAIN;
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
            if (!info->used && s_mode) {
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

    case MS_ECO: /* el Eco del Odio: uno o dos jugadores */
        if (in->menu_up || in->menu_down)
            s_eco_sel ^= 1;
        if (back) {
            click();
            s_state = MS_MAIN;
            return MENU_NONE;
        }
        if (in->confirm || in->start) {
            click();
            s_mode = 1;
            s_players = s_eco_sel + 1;
            s_picking = 0;
            s_state = MS_SLOTS;
            read_slots();
        }
        return MENU_NONE;

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
    int w = font_width(text, FONT_EXOCET_ENGRAVED);
    font_draw_centered(x + 136, y + (35 - font_height(FONT_EXOCET_ENGRAVED)) / 2 + 1, text, FONT_EXOCET_ENGRAVED,
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
        draw_wide_button(264, 268 + i * 38, k_items[i], i == s_sel);

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

static void draw_slots(void)
{
    ui_grid("charselect/characterselectscreenexp", 0, 12, 4, 0, 0);
    char text[96];
    if (s_players > 1)
        snprintf(text, sizeof text, tr("JUGADOR %d: ELEGÍ TU PERSONAJE"), s_picking + 1);
    else
        snprintf(text, sizeof text, "%s", tr("ELEGÍ TU PERSONAJE"));
    font_draw_centered(317, 34, text, FONT_EXOCET, COL(R_GOLD, 15));
    if (s_mode) /* (en rojo, el del odio) */
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
            if (s_mode) {
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
        font_draw_centered(317, 112, s_note, 2, COL(R_RED, 13));
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
    static const char *const desc[NUM_CLASSES][2] = {
        { "Pega duro", "y grita" },
        { "Arcos y", "jabalinas" },
        { "Fuego, rayos", "y frío" },
        { "Auras", "y escudo" },
        { "Maldiciones", "y muertos" },
        { "Garras, trampas", "y sombras" },
        { "Bestias, fuego", "y viento" },
        { "Demonios, maleficios", "y caos" },
    };
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

/* El Eco del Odio: qué es, y uno o dos jugadores */
static void draw_eco(void)
{
    ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
    draw_logo();
    ui_dark_box(170, 250, 460, 230);
    font_draw_centered(SCREEN_W / 2, 266, "ECO DEL ODIO", FONT_EXOCET, COL(R_RED, 14));
    static const char *const k_lines[3] = {
        "Oleadas sin fin en una arena al azar, cada una más difícil.",
        "Cada cinco oleadas, un jefe de la historia. Morir termina la corrida.",
        "La experiencia, el oro y los objetos vuelven a tu personaje.",
    };
    for (int k = 0; k < 3; k++)
        font_draw_centered(SCREEN_W / 2, 302 + k * 18, k_lines[k], 1, COL(R_GRAY, 14));
    static const char *const k_opts[2] = { "UN JUGADOR", "DOS JUGADORES" };
    for (int i = 0; i < 2; i++) {
        int y = 372 + i * 34;
        font_draw_centered(SCREEN_W / 2, y, k_opts[i], FONT_EXOCET, i == s_eco_sel ? COL(R_GOLD, 15) : COL(R_GRAY, 13));
        if (i == s_eco_sel)
            draw_pentagrams(SCREEN_W / 2, y + font_height(FONT_EXOCET) / 2, font_width(k_opts[i], FONT_EXOCET), 22);
    }
    font_draw_centered(SCREEN_W / 2, 458, "A: elegir     B: volver", 1, COL(R_GRAY, 14));
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
    case MS_ECO:    draw_eco(); break;
    case MS_CONTROLS:
        ui_grid("frontend/gameselectscreenexp", 0, 12, 4, 0, 0);
        controls_draw();
        break;
    }
}
