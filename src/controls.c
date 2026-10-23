#include "controls.h"

#include "audio.h"
#include "font.h"
#include "ui.h"
#include "video.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

/* La pantalla de controles: lo que hace cada botón del joystick (input.c,
   on_button y on_axis) y cada tecla de los dos jugadores (on_key y el
   teclado mantenido de input_poll). Si se cambia un control allá, va acá. */

enum { TAB_PAD, TAB_KEYS, TAB_COUNT };
static const char *const k_tabs[TAB_COUNT] = { "JOYSTICK", "TECLADO" };
static int s_tab;

void controls_open_at(int tab)
{
    s_tab = tab >= 0 && tab < TAB_COUNT ? tab : TAB_PAD;
}

void controls_open(void)
{
    controls_open_at(input_num_pads() > 0 ? TAB_PAD : TAB_KEYS); /* lo que tiene a mano */
}

int controls_tick(const InputState *in)
{
    int lb = in->player[0].potion_life || in->player[1].potion_life;
    int rb = in->player[0].automap || in->player[1].automap;
    int step = (in->menu_right || rb) - (in->menu_left || lb);
    if (step) {
        s_tab = (s_tab + TAB_COUNT + step) % TAB_COUNT;
        audio_play_name("cursor_pass", AUDIO_NOPOS, AUDIO_NOPOS);
    }
    if (in->menu_back || in->escape || in->start || in->player[0].back || in->player[1].back) {
        audio_play_name("cursor_button_click", AUDIO_NOPOS, AUDIO_NOPOS);
        return 0;
    }
    return 1;
}

/* ---- Dibujo ---- */

static void frame_rect(int x, int y, int w, int h, uint8_t c)
{
    fb_hline(x, x + w - 1, y, c);
    fb_hline(x, x + w - 1, y + h - 1, c);
    fb_vline(x, y, y + h - 1, c);
    fb_vline(x + w - 1, y, y + h - 1, c);
}

/* El texto en `w` de ancho (si no entra, con la letra chica), a la izquierda o a la derecha */
static void text_fit(int x, int y, int w, const char *s, int scale, uint8_t c, int right)
{
    if (font_width(s, scale) > w)
        scale = 1;
    int tw = font_width(s, scale);
    font_draw(right ? x + w - tw : x, y + (font_height(2) - font_height(scale)) / 2, s, scale, c);
}

static void disc(int cx, int cy, int r, uint8_t edge, uint8_t fill)
{
    fb_ellipse(cx, cy, r, r, edge);
    fb_ellipse(cx, cy, r - 2, r - 2, fill);
}

/* Rectángulo de puntas redondas (los gatillos, los hombros, Select y Start) */
static void pill(int x, int y, int w, int h, uint8_t edge, uint8_t fill)
{
    int r = h / 2;
    fb_ellipse(x + r, y + r, r, r, edge);
    fb_ellipse(x + w - r - 1, y + r, r, r, edge);
    fb_fill_rect(x + r, y, w - 2 * r, h, edge);
    fb_ellipse(x + r, y + r, r - 2, r - 2, fill);
    fb_ellipse(x + w - r - 1, y + r, r - 2, r - 2, fill);
    fb_fill_rect(x + r, y + 2, w - 2 * r, h - 4, fill);
}

static void text_centered_v(int cx, int cy, const char *s, int scale, uint8_t c)
{
    font_draw_centered(cx, cy - font_height(scale) / 2, s, scale, c);
}

/* Los colores de A, B, X e Y (verde, rojo, azul y amarillo, como en el joystick) */
static const int k_face_ramp[4] = { R_GRASS, R_RED, R_BLUE, R_GOLD };

static void face_button(int cx, int cy, int r, int b, const char *letter)
{
    disc(cx, cy, r, COL(k_face_ramp[b], 14), COL(k_face_ramp[b], 8));
    fb_ellipse(cx - r / 3, cy - r / 3, r / 3, r / 4, COL(k_face_ramp[b], 11)); /* el brillo */
    text_centered_v(cx, cy, letter, 1, COL(R_GRAY, 15));
}

static void dpad(int cx, int cy, int arm, int half, uint8_t edge, uint8_t fill)
{
    fb_fill_rect(cx - half - 1, cy - arm - 1, 2 * half + 3, 2 * arm + 3, edge);
    fb_fill_rect(cx - arm - 1, cy - half - 1, 2 * arm + 3, 2 * half + 3, edge);
    fb_fill_rect(cx - half + 1, cy - arm + 1, 2 * half - 1, 2 * arm - 1, fill);
    fb_fill_rect(cx - arm + 1, cy - half + 1, 2 * arm - 1, 2 * half - 1, fill);
}

static void stick(int cx, int cy, int r)
{
    disc(cx, cy, r, COL(R_GRAY, 9), COL(R_GRAY, 1));
    disc(cx, cy, r - 5, COL(R_GRAY, 7), COL(R_GRAY, 4));
    fb_ellipse(cx - 2, cy - 3, (r - 5) / 2, (r - 5) / 3, COL(R_GRAY, 6));
}

/* El joystick: el cuerpo, una vez más grande (el borde) y otra adentro */
static void pad_body(int ox, int oy, int grow, uint8_t c)
{
    fb_ellipse(ox, oy - 4, 112 + grow, 46 + grow, c);
    fb_ellipse(ox - 76, oy + 26, 46 + grow, 62 + grow, c);
    fb_ellipse(ox + 76, oy + 26, 46 + grow, 62 + grow, c);
}

/* Cada botón: dónde está en el dibujo, de qué lado va su explicación, a qué altura y qué dice */
enum { BADGE_TEXT, BADGE_FACE, BADGE_STICK, BADGE_DPAD };
typedef struct {
    int         ax, ay; /* el punto del joystick (relativo al centro) */
    int         right, y;
    int         badge, face; /* BADGE_*, y el botón (A, B, X, Y = 0..3) */
    const char *name, *what;
} PadLabel;

static const PadLabel k_pad[] = {
    { -78, -62, 0, 136, BADGE_TEXT, 0, "LT", "Poción de maná" },
    { -73, -50, 0, 170, BADGE_TEXT, 0, "LB", "Poción de vida" },
    { -70, -12, 0, 210, BADGE_STICK, 0, "", "Caminar y elegir en los menús" },
    { -58, 0, 0, 244, BADGE_TEXT, 0, "L3", "Poción de aguante" },
    { -44, 28, 0, 300, BADGE_DPAD, 0, "", "Pociones del cinturón (1 a 4)" },
    { -17, -10, 0, 356, BADGE_TEXT, 0, "SELECT", "Inventario y paneles" },
    { 78, -62, 1, 136, BADGE_TEXT, 0, "RT", "Correr (mantenido)" },
    { 73, -50, 1, 170, BADGE_TEXT, 0, "RB", "Automapa" },
    { 60, -27, 1, 204, BADGE_FACE, 3, "Y", "Habilidad 4 / siguiente panel" },
    { 43, -10, 1, 234, BADGE_FACE, 2, "X", "Habilidad 3 / mover un objeto" },
    { 77, -10, 1, 264, BADGE_FACE, 1, "B", "Habilidad 2 / volver" },
    { 60, 7, 1, 294, BADGE_FACE, 0, "A", "Atacar, levantar y hablar" },
    { 32, 40, 1, 330, BADGE_TEXT, 0, "R3", "Frases (¡Ayuda!, Gracias...)" },
    { 17, -10, 1, 362, BADGE_TEXT, 0, "START", "Pausa (con Select: salir)" },
};

static void draw_pad(void)
{
    const int ox = SCREEN_W / 2, oy = 262;
    const uint8_t edge = COL(R_GRAY, 9), part = COL(R_GRAY, 3), line = COL(R_GOLD, 7);
    /* Los gatillos y los hombros, detrás del cuerpo */
    pill(ox - 100, oy - 70, 44, 18, edge, part);
    pill(ox + 56, oy - 70, 44, 18, edge, part);
    pill(ox - 104, oy - 56, 62, 14, edge, COL(R_GRAY, 5));
    pill(ox + 42, oy - 56, 62, 14, edge, COL(R_GRAY, 5));
    pad_body(ox, oy, 3, COL(R_STONE, 11));
    pad_body(ox, oy, 0, COL(R_STONE, 3));
    fb_ellipse(ox, oy - 26, 84, 16, COL(R_STONE, 4)); /* el brillo de arriba */
    /* Las líneas hacia lo que hace cada uno (debajo de los botones) */
    for (unsigned k = 0; k < sizeof k_pad / sizeof k_pad[0]; k++) {
        const PadLabel *l = &k_pad[k];
        int bx = l->right ? 556 : SCREEN_W - 556, by = l->y + font_height(2) / 2;
        fb_line(bx + (l->right ? -16 : 16), by, ox + l->ax, oy + l->ay, line);
        fb_ellipse(ox + l->ax, oy + l->ay, 2, 2, COL(R_GOLD, 13));
    }
    stick(ox - 58, oy - 10, 18);
    stick(ox + 32, oy + 28, 16);
    dpad(ox - 30, oy + 28, 15, 5, edge, part);
    pill(ox - 26, oy - 14, 16, 8, edge, part); /* Select */
    pill(ox + 10, oy - 14, 16, 8, edge, part); /* Start */
    static const char *const k_letters[4] = { "A", "B", "X", "Y" };
    static const int k_off[4][2] = { { 0, 17 }, { 17, 0 }, { -17, 0 }, { 0, -17 } };
    for (int b = 0; b < 4; b++)
        face_button(ox + 60 + k_off[b][0], oy - 10 + k_off[b][1], 9, b, k_letters[b]);
    /* El pentagrama del menú, girando en el medio */
    ui_frame_rotated("cursor/pentspin", 0, ox, oy - 32, 20, (float)SDL_GetTicks() * 0.0015f);

    /* Las explicaciones: la insignia del botón y lo que hace */
    for (unsigned k = 0; k < sizeof k_pad / sizeof k_pad[0]; k++) {
        const PadLabel *l = &k_pad[k];
        int bx = l->right ? 556 : SCREEN_W - 556, cy = l->y + font_height(2) / 2, half = 12;
        switch (l->badge) {
        case BADGE_FACE:
            face_button(bx, cy, 11, l->face, l->name);
            break;
        case BADGE_STICK:
            stick(bx, cy, 12);
            break;
        case BADGE_DPAD:
            dpad(bx, cy, 10, 3, edge, part);
            break;
        default: {
            int w = font_width(l->name, 1) + 14;
            pill(bx - w / 2, cy - 8, w, 16, edge, part);
            text_centered_v(bx, cy, l->name, 1, COL(R_GRAY, 15));
            half = w / 2 > half ? w / 2 : half;
            break;
        }
        }
        if (l->right)
            text_fit(bx + half + 10, l->y, SCREEN_W - 12 - (bx + half + 10), l->what, 2, COL(R_GRAY, 15), 0);
        else
            text_fit(12, l->y, bx - half - 10 - 12, l->what, 2, COL(R_GRAY, 15), 1);
    }

    /* En los paneles cambia lo que hacen */
    ui_box(40, 404, SCREEN_W - 80, 150);
    font_draw_centered(SCREEN_W / 2, 414, "EN EL INVENTARIO, LAS TIENDAS Y EL COFRE", 2, COL(R_GOLD, 15));
    static const struct { int b; const char *name, *what; } k_panel[] = {
        { 0, "A", "Tomar o dejar un objeto" },
        { 2, "X", "A la mochila, al cubo o al cofre" },
        { 3, "Y", "Siguiente panel (habilidades, botones, ...)" },
        { 1, "B", "Cerrar" },
    };
    for (unsigned k = 0; k < sizeof k_panel / sizeof k_panel[0]; k++) {
        int col = (int)k % 2, row = (int)k / 2, x = 70 + col * 340, y = 446 + row * 30;
        face_button(x, y + font_height(2) / 2, 10, k_panel[k].b, k_panel[k].name);
        text_fit(x + 18, y, 300, k_panel[k].what, 2, COL(R_GRAY, 14), 0);
    }
    pill(70 - 14, 506, 28, 16, edge, part);
    text_centered_v(70, 514, "RB", 1, COL(R_GRAY, 15));
    text_fit(70 + 22, 506, 260, "Al suelo", 2, COL(R_GRAY, 14), 0);
    text_fit(400, 506, 330, "Cruceta: recorrer", 2, COL(R_GRAY, 14), 0);
    font_draw_centered(SCREEN_W / 2, 532, "Jugador 1: el primer joystick que se use   -   Jugador 2: el segundo", 1,
                       COL(R_STONE, 13));
}

/* ---- El teclado ---- */

/* Las flechas se dibujan (la letra del juego no las tiene) */
static int arrow_dir(const char *k)
{
    return !strcmp(k, "UP") ? 1 : !strcmp(k, "DOWN") ? 2 : !strcmp(k, "LEFT") ? 3 : !strcmp(k, "RIGHT") ? 4 : 0;
}

static int key_scale(const char *k)
{
    return strlen(k) <= 2 ? 2 : 1;
}

static int keycap_w(const char *k)
{
    if (arrow_dir(k))
        return 24;
    int w = font_width(k, key_scale(k)) + 14;
    return w < 24 ? 24 : w;
}

/* Una tecla: la cara clara con su brillo arriba y el costado oscuro abajo */
static void keycap(int x, int y, const char *k)
{
    int w = keycap_w(k), h = 24;
    fb_fill_rect(x, y, w, h, COL(R_GRAY, 9));
    fb_fill_rect(x + 1, y + h - 5, w - 2, 4, COL(R_GRAY, 2));
    fb_fill_rect(x + 1, y + 1, w - 2, h - 6, COL(R_GRAY, 5));
    fb_hline(x + 2, x + w - 3, y + 1, COL(R_GRAY, 11));
    int cx = x + w / 2, cy = y + (h - 4) / 2;
    int d = arrow_dir(k);
    if (d) {
        uint8_t c = COL(R_GRAY, 15);
        for (int i = 0; i < 6; i++) {
            if (d == 1)
                fb_hline(cx - i, cx + i, cy - 3 + i, c);
            else if (d == 2)
                fb_hline(cx - i, cx + i, cy + 3 - i, c);
            else if (d == 3)
                fb_vline(cx - 3 + i, cy - i, cy + i, c);
            else
                fb_vline(cx + 3 - i, cy - i, cy + i, c);
        }
        return;
    }
    text_centered_v(cx, cy + 1, k, key_scale(k), COL(R_GRAY, 15));
}

/* Las teclas de una fila (separadas por "|"), centradas en cx */
static void key_group(int cx, int y, const char *keys)
{
    char buf[96], *parts[8];
    int n = 0, total = 0;
    snprintf(buf, sizeof buf, "%s", keys);
    for (char *t = strtok(buf, "|"); t && n < 8; t = strtok(NULL, "|"))
        parts[n++] = t;
    if (n == 1 && !strcmp(parts[0], "-")) {
        font_draw_centered(cx, y + 4, "-", 2, COL(R_GRAY, 9));
        return;
    }
    for (int i = 0; i < n; i++)
        total += keycap_w(parts[i]) + (i ? 4 : 0);
    int x = cx - total / 2;
    for (int i = 0; i < n; i++) {
        keycap(x, y, parts[i]);
        x += keycap_w(parts[i]) + 4;
    }
}

static const struct { const char *what, *p1, *p2; } k_keys[] = {
    { "Caminar", "W|A|S|D", "UP|LEFT|DOWN|RIGHT" },
    { "Correr (mantenido)", "Shift izq.", "Shift der." },
    { "Atacar, levantar y hablar", "Espacio", "Enter" },
    { "Habilidad 2", "E", "J" },
    { "Habilidad 3", "R", "L" },
    { "Habilidad 4", "F", "O" },
    { "Pociones del cinturón", "1|2|3|4", "7|8|9|0" },
    { "Inventario", "I", "P" },
    { "Árbol de habilidades", "T", "U" },
    { "Automapa", "Tab", "-" },
    { "Tirar el objeto", "Q", "M" },
    { "Volver / cerrar", "Retroceso", "Supr" },
    { "Frases", "C", "N" },
};

static void draw_keys(void)
{
    const int c1 = 452, c2 = 652, top = 132, step = 28;
    font_draw(48, 102, "ACCIÓN", 2, COL(R_GOLD, 13));
    font_draw_centered(c1, 102, "JUGADOR 1", 2, COL(R_GOLD, 15));
    font_draw_centered(c2, 102, "JUGADOR 2", 2, COL(R_GOLD, 15));
    int n = (int)(sizeof k_keys / sizeof k_keys[0]);
    for (int i = 0; i < n; i++) {
        int y = top + i * step;
        if (i % 2 == 0)
            fb_blend_rect(36, y - 2, SCREEN_W - 72, step, COL(R_GOLD, 3), BLEND_25);
        text_fit(48, y + 3, c1 - 100 - 48, k_keys[i].what, 2, COL(R_GRAY, 15), 0);
        key_group(c1, y, k_keys[i].p1);
        key_group(c2, y, k_keys[i].p2);
    }
    int y = top + n * step + 12;
    fb_hline(60, SCREEN_W - 60, y - 6, COL(R_GOLD, 6));
    keycap(150, y, "Esc");
    text_fit(150 + keycap_w("Esc") + 10, y + 3, 200, "Pausa (los dos)", 2, COL(R_GRAY, 14), 0);
    font_draw(430, y + 3, "Mouse:", 2, COL(R_GOLD, 13));
    text_fit(430 + font_width("Mouse:", 2) + 8, y + 3, SCREEN_W - 40 - (430 + font_width("Mouse:", 2) + 8),
             "con el clic, el jugador 1 camina", 2, COL(R_GRAY, 14), 0);
}

void controls_draw(void)
{
    fb_shade_rect(0, 0, SCREEN_W, SCREEN_H, 5);
    /* El título entre dos pentagramas */
    const char *title = "CONTROLES";
    int tw = font_width(title, 6);
    float a = (float)SDL_GetTicks() * 0.0015f;
    font_draw_centered(SCREEN_W / 2, 10, title, 6, COL(R_GOLD, 15));
    ui_frame_rotated("cursor/pentspin", 0, SCREEN_W / 2 - tw / 2 - 30, 32, 30, a);
    ui_frame_rotated("cursor/pentspin", 0, SCREEN_W / 2 + tw / 2 + 30, 32, 30, -a);
    /* Las solapas, con LB y RB a los lados */
    int w0 = font_width(k_tabs[0], 3) + 40, w1 = font_width(k_tabs[1], 3) + 40, x = (SCREEN_W - w0 - w1 - 12) / 2;
    const uint8_t edge = COL(R_GRAY, 9), part = COL(R_GRAY, 3);
    pill(x - 44, 64, 30, 16, edge, part);
    text_centered_v(x - 29, 72, "LB", 1, COL(R_GRAY, 15));
    pill(x + w0 + 12 + w1 + 14, 64, 30, 16, edge, part);
    text_centered_v(x + w0 + 12 + w1 + 29, 72, "RB", 1, COL(R_GRAY, 15));
    for (int t = 0; t < TAB_COUNT; t++) {
        int w = t ? w1 : w0, sel = t == s_tab;
        if (sel) {
            fb_blend_rect(x, 58, w, 28, COL(R_GOLD, 4), BLEND_50);
            frame_rect(x, 58, w, 28, COL(R_GOLD, 15));
        } else {
            frame_rect(x, 58, w, 28, COL(R_GRAY, 6));
        }
        font_draw_centered(x + w / 2, 72 - font_height(3) / 2, k_tabs[t], 3, sel ? COL(R_GOLD, 15) : COL(R_GRAY, 11));
        x += w + 12;
    }
    if (s_tab == TAB_PAD)
        draw_pad();
    else
        draw_keys();
    font_draw_centered(SCREEN_W / 2, 580, "Izquierda / derecha (o LB / RB): otra solapa    B: volver", 1,
                       COL(R_STONE, 12));
}
