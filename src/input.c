#include "input.h"
#include "video.h"
#include "lang.h"
#include "touch.h"

#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_PADS   8     /* hay joysticks baratos que se presentan como dos */
#define DEADZONE   0.25f
#define MENU_STICK 0.6f
#define LOG_LINES  300   /* renglones de diagnóstico de los controles en el log del juego */
#define GRACE_MS   500   /* lo que llega apenas se conecta también cuenta como "ya apretado" */
#define CENTER_MAX 0.5f  /* la palanca que descansa corrida hasta esto (las de ADC baratas): ese lugar es su medio */
#define CALM_MS    400   /* el stick cuenta recién después de estar quieto esto (uno que flota nunca lo está) */
#define STEADY     0.08f /* quieto: lo que varía es menos que esto */
#define MIRROR_MS  40    /* lo mismo en dos joysticks iguales en menos de esto: es un solo control visto dos veces */
#define CODE_STICK SDL_CONTROLLER_BUTTON_MAX /* (en last_ms: las direcciones del stick, después de los botones) */

/* Lo que se mira en cada tick (no por eventos) */
enum { ARM_STICK = 1, ARM_DPAD = 2, ARM_RT = 4, ARM_RB = 8, ARM_ALL = 15 };

/* Un joystick conectado. Recién maneja a un jugador cuando se lo usa de
   verdad: el primero que se usa es el jugador 1 y el segundo, el 2. Lo que
   ya estaba apretado al conectarlo no cuenta hasta que se suelta, así el
   "fantasma" de un joystick que se ve como dos (con un botón siempre
   apretado) nunca se queda con un jugador. */
typedef struct {
    SDL_GameController *pad;
    int   player; /* -1: todavía no se usó */
    int   armed;  /* ARM_*: lo que ya se vio suelto */
    Uint8 held[SDL_CONTROLLER_BUTTON_MAX]; /* apretados al conectar: no cuentan hasta soltarlos */
    int   stuck;  /* tenía algo apretado al conectar */
    Uint32 opened_at;
    int   stick_dir, stick_x, ltrigger; /* para navegar menús y el gatillo de la poción */
    int   axis_on[SDL_CONTROLLER_AXIS_MAX]; /* para anotar en el log solo cuando cruza la mitad */
    float cx, cy;       /* el medio del stick izquierdo: donde descansa (las palancas baratas no dan 0) */
    float sx, sy;       /* dónde empezó a estar quieto (hasta que cuenta) */
    Uint32 calm_since;  /* desde cuándo está quieto */
    char  guid[64];
    int   touch;        /* el joystick virtual de los controles táctiles (touch.c): su stick ya está en el medio */
    int   mirror;       /* repite lo que hace otro joystick igual: es el mismo control visto dos veces */
    int   solo;         /* apretó algo que el otro no (dos veces seguidas: era otro control de verdad) */
    int   pend_code;    /* (el último que apretó siendo eco, para ver si el otro lo repite) */
    Uint32 pend_ms;
    Uint32 last_ms[SDL_CONTROLLER_BUTTON_MAX + 4]; /* cuándo apretó cada botón y cada dirección del stick */
} Pad;

static Pad   s_pads[MAX_PADS];
static Uint32 s_fake_clock; /* D2_FAKE_PAD: un reloj de 40 ms por tick (las pruebas sin ventana no van en tiempo real) */

static Uint32 now_ms(void)
{
    return s_fake_clock ? s_fake_clock : SDL_GetTicks();
}
static Uint8 s_key_blocked[SDL_NUM_SCANCODES]; /* teclas apretadas desde antes de arrancar */
static int   s_keys_checked;
static int   s_log_left = LOG_LINES;

/* Diagnóstico de los controles en el log (d2port.log en Batocera): qué se
   conectó, cómo está mapeado y lo que se aprieta, hasta LOG_LINES renglones */
static void input_log(const char *fmt, ...)
{
    if (s_log_left <= 0)
        return;
    if (--s_log_left == 0) {
        SDL_Log("control: (no se anotan más)");
        return;
    }
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    SDL_Log("control: %s", text);
}

static int pad_index(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (s_pads[i].pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pads[i].pad)) == id)
            return i;
    return -1;
}

static float axis(SDL_GameController *pad, SDL_GameControllerAxis a)
{
    float v = SDL_GameControllerGetAxis(pad, a) / 32767.0f;
    return v < -1.0f ? -1.0f : v;
}

/* Un eje del stick izquierdo desde su medio (cx, cy), estirado para que todo el recorrido siga dando -1..1 */
static float centered(const Pad *pd, int ax, float v)
{
    float c = ax == SDL_CONTROLLER_AXIS_LEFTX ? pd->cx : ax == SDL_CONTROLLER_AXIS_LEFTY ? pd->cy : 0.0f;
    float s = v >= c ? (v - c) / (1.0f - c) : (v - c) / (1.0f + c);
    return s < -1.0f ? -1.0f : s > 1.0f ? 1.0f : s;
}

static float stick(const Pad *pd, SDL_GameControllerAxis a)
{
    return centered(pd, a, axis(pd->pad, a));
}

/* Qué tiene "apretado" el joystick en este momento (en bits ARM_*) */
static int pad_active(const Pad *pd)
{
    SDL_GameController *pad = pd->pad;
    int a = 0;
    float ax = stick(pd, SDL_CONTROLLER_AXIS_LEFTX), ay = stick(pd, SDL_CONTROLLER_AXIS_LEFTY);
    if (sqrtf(ax * ax + ay * ay) > DEADZONE)
        a |= ARM_STICK;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
        a |= ARM_DPAD;
    if (axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f)
        a |= ARM_RT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
        a |= ARM_RB;
    return a;
}

static const char *arm_name(int bit)
{
    return bit == ARM_STICK ? tr("el stick izquierdo")
         : bit == ARM_DPAD  ? tr("la cruceta")
         : bit == ARM_RT    ? tr("el gatillo derecho")
                            : tr("el botón R1");
}

static const char *button_name(int b)
{
    const char *s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b);
    return s ? s : "?";
}

/* ¿Es un eco? El mismo control visto dos veces (pasa con algunos joysticks y consolas): dos joysticks del mismo
   modelo (GUID) que hacen lo mismo (code: un botón o una dirección del stick) con menos de MIRROR_MS. De los dos, el
   eco es el que no maneja a nadie (o el de después): no se une ni cuenta. Si después hace algo solo dos veces, era
   otro control. Devuelve 1 si lo de ahora se ignora. */
static int mirror_check(int i, int code)
{
    Pad *pd = &s_pads[i];
    Uint32 now = now_ms();
    pd->last_ms[code] = now;
    for (int k = 0; k < MAX_PADS; k++) {
        Pad *o = &s_pads[k];
        if (k == i || !o->pad || strcmp(o->guid, pd->guid) != 0 || !o->last_ms[code] || now - o->last_ms[code] > MIRROR_MS)
            continue;
        /* (el eco: el que no maneja a nadie; si los dos o ninguno, el de jugador más alto o el de después) */
        Pad *echo;
        if ((pd->player >= 0) != (o->player >= 0))
            echo = pd->player >= 0 ? o : pd;
        else if (pd->player >= 0)
            echo = pd->player > o->player ? pd : o;
        else
            echo = k < i ? pd : o;
        if (!echo->mirror) {
            echo->mirror = 1;
            input_log("joystick %d repite lo que hace el joystick %d: es el mismo control visto dos veces, no se usa",
                      (int)(echo - s_pads) + 1, (int)((echo == pd ? o : pd) - s_pads) + 1);
            if (echo->player >= 0) {
                input_log("joystick %d deja al jugador %d", (int)(echo - s_pads) + 1, echo->player + 1);
                echo->player = -1;
            }
        }
        echo->solo = 0;
        return echo == pd;
    }
    if (pd->mirror) { /* (lo apretó solo: se mira al final de la lectura si el otro lo repite) */
        pd->pend_code = code;
        pd->pend_ms = now;
        return 1;
    }
    return 0;
}

/* Al final de cada lectura: el eco que apretó algo que el otro no repitió en MIRROR_MS (dos veces) era otro control */
static void mirror_settle(void)
{
    Uint32 now = now_ms();
    for (int i = 0; i < MAX_PADS; i++) {
        Pad *pd = &s_pads[i];
        if (!pd->pad || !pd->mirror || !pd->pend_ms || now - pd->pend_ms <= MIRROR_MS)
            continue;
        int twin = 0;
        for (int k = 0; k < MAX_PADS; k++) {
            const Pad *o = &s_pads[k];
            Uint32 t = o->last_ms[pd->pend_code];
            twin |= k != i && o->pad && strcmp(o->guid, pd->guid) == 0 && t &&
                    (t > pd->pend_ms ? t - pd->pend_ms : pd->pend_ms - t) <= MIRROR_MS;
        }
        pd->pend_ms = 0;
        pd->solo = twin ? 0 : pd->solo + 1;
        if (pd->solo >= 2) {
            pd->mirror = 0;
            pd->solo = 0;
            input_log("joystick %d se usó solo: es otro control", i + 1);
        }
    }
}

/* Se usó de verdad: si no manejaba a nadie, toma el primer jugador libre */
static void join(int i)
{
    Pad *pd = &s_pads[i];
    if (pd->player >= 0 || pd->mirror)
        return;
    for (int p = 0; p < MAX_LOCAL; p++) {
        int taken = 0;
        for (int k = 0; k < MAX_PADS; k++)
            taken |= s_pads[k].pad && s_pads[k].player == p;
        if (!taken) {
            pd->player = p;
            input_log("joystick %d (%s) maneja al jugador %d", i + 1, SDL_GameControllerName(pd->pad), p + 1);
            return;
        }
    }
}

static void open_pad(int device_index)
{
    char guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index), guid, sizeof guid);
    if (!SDL_IsGameController(device_index)) {
        input_log("se ignora \"%s\" (sin mapeo de control; guid %s)", SDL_JoystickNameForIndex(device_index), guid);
        return;
    }
    if (pad_index(SDL_JoystickGetDeviceInstanceID(device_index)) >= 0)
        return; /* ya abierto */
    int i = 0;
    while (i < MAX_PADS && s_pads[i].pad)
        i++;
    if (i == MAX_PADS)
        return;
    SDL_GameController *pad = SDL_GameControllerOpen(device_index);
    if (!pad)
        return;
    Pad *pd = &s_pads[i];
    memset(pd, 0, sizeof *pd);
    pd->pad = pad;
    pd->player = -1;
    pd->opened_at = now_ms();
    pd->touch = touch_is_pad(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)));
    snprintf(pd->guid, sizeof pd->guid, "%s", guid);
    SDL_Log("joystick %d: %s", i + 1, SDL_GameControllerName(pad));
    char *map = SDL_GameControllerMapping(pad);
    input_log("joystick %d: guid %s, mapeo %s", i + 1, guid, map ? map : "-");
    SDL_free(map);
    for (int k = 0; k < MAX_PADS; k++) {
        char other[64];
        if (k == i || !s_pads[k].pad)
            continue;
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(s_pads[k].pad)), other, sizeof other);
        if (strcmp(other, guid) == 0)
            input_log("joystick %d: mismo modelo que el joystick %d (puede ser el mismo control visto dos veces)", i + 1, k + 1);
    }

    /* Cómo está al conectarlo */
    char axes[256] = "";
    size_t n = 0;
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX && n < sizeof axes; a++) {
        Sint16 v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)a);
        const char *name = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)a);
        n += (size_t)snprintf(axes + n, sizeof axes - n, " %s=%.2f", name ? name : "?", v / 32767.0f);
        pd->axis_on[a] = v > 16000 || v < -16000;
    }
    input_log("joystick %d: ejes al conectar:%s", i + 1, axes);
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b)) {
            pd->held[b] = 1;
            pd->stuck = 1;
            input_log("joystick %d: \"%s\" ya está apretado al conectar: no cuenta hasta que se suelte", i + 1,
                      button_name(b));
        }
    /* (el stick no: cuenta cuando queda quieto, y donde quedó es su medio; corrido no es un "trabado": las
       palancas de ADC de las portátiles no descansan en 0) */
    int active = pad_active(pd);
    pd->armed = ARM_ALL & ~active & ~ARM_STICK;
    for (int bit = ARM_DPAD; bit <= ARM_RB; bit <<= 1)
        if (active & bit) {
            pd->stuck = 1;
            input_log("joystick %d: %s está activo al conectar: no cuenta hasta que se suelte", i + 1, arm_name(bit));
        }
    if (active & ARM_STICK)
        input_log("joystick %d: el stick no está en el medio al conectar (%.2f, %.2f): cuenta cuando quede quieto", i + 1,
                  axis(pad, SDL_CONTROLLER_AXIS_LEFTX), axis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    pd->ltrigger = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
}

static void close_pad(SDL_JoystickID id)
{
    int i = pad_index(id);
    if (i < 0)
        return;
    input_log("joystick %d: desconectado", i + 1);
    SDL_GameControllerClose(s_pads[i].pad);
    memset(&s_pads[i], 0, sizeof s_pads[i]);
    s_pads[i].player = -1;
}

/* Los joysticks que se pueden usar: no cuenta el "fantasma" que se conectó
   con algo trabado y nunca se usó */
int input_num_pads(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PADS; i++)
        n += s_pads[i].pad && !s_pads[i].mirror && (!s_pads[i].stuck || s_pads[i].player >= 0);
    return n;
}

void input_shutdown(void)
{
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i].pad)
            SDL_GameControllerClose(s_pads[i].pad);
        memset(&s_pads[i], 0, sizeof s_pads[i]);
        s_pads[i].player = -1;
    }
}

void input_clear_events(InputState *in)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerInput *p = &in->player[i];
        p->action = p->potion_life = p->potion_mana = p->potion_stamina = p->inventory = 0;
        p->nav_up = p->nav_down = p->nav_left = p->nav_right = p->drop = p->back = 0;
        p->button[0] = p->button[1] = p->button[2] = p->button[3] = p->skill_tree = p->stats_page = 0;
        p->belt = p->automap = p->chat = 0;
    }
    in->menu_up = in->menu_down = in->menu_left = in->menu_right = in->menu_back = in->menu_delete = 0;
    in->confirm = in->start = in->escape = in->any_press = 0;
}

static void on_key(InputState *in, const SDL_KeyboardEvent *k)
{
    PlayerInput *p1 = &in->player[0], *p2 = &in->player[1];
    if (!k->repeat)
        input_log("tecla %s%s", SDL_GetScancodeName(k->keysym.scancode),
                  s_key_blocked[k->keysym.scancode] ? " (trabada desde el principio: no cuenta)" : "");
    if (s_key_blocked[k->keysym.scancode])
        return;
    /* F11 o Alt+Enter: pantalla completa o ventana (en la PC) */
    if (k->keysym.sym == SDLK_F11 || ((k->keysym.sym == SDLK_RETURN || k->keysym.sym == SDLK_KP_ENTER) && (k->keysym.mod & KMOD_ALT))) {
        if (!k->repeat)
            video_toggle_fullscreen();
        return;
    }
    switch (k->keysym.sym) { /* (solas, las de Alt+Tab y compañía no cuentan como apretar algo) */
    case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI: case SDLK_LCTRL: case SDLK_RCTRL:
    case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_TAB:
        break;
    default:
        if (!k->repeat)
            in->any_press = 1;
        break;
    }
    switch (k->keysym.sym) {
    case SDLK_w:     in->menu_up = 1;   p1->nav_up = 1;    return;
    case SDLK_s:     in->menu_down = 1; p1->nav_down = 1;  return;
    case SDLK_a:     in->menu_left = 1;  p1->nav_left = 1;  return;
    case SDLK_d:     in->menu_right = 1; p1->nav_right = 1; return;
    case SDLK_UP:    in->menu_up = 1;   p2->nav_up = 1;    return;
    case SDLK_DOWN:  in->menu_down = 1; p2->nav_down = 1;  return;
    case SDLK_LEFT:  in->menu_left = 1;  p2->nav_left = 1;  return;
    case SDLK_RIGHT: in->menu_right = 1; p2->nav_right = 1; return;
    default:
        break;
    }
    if (k->repeat)
        return;
    switch (k->keysym.sym) {
    case SDLK_ESCAPE:   in->escape = 1; break;
    case SDLK_AC_BACK:  in->escape = 1; break; /* (Android: el botón o el gesto de volver) */
    case SDLK_SPACE:    in->confirm = 1; in->player[0].action = in->player[0].button[0] = 1; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: in->confirm = 1; in->player[1].action = in->player[1].button[0] = 1; break;
    case SDLK_e:        in->player[0].button[1] = 1; break;
    case SDLK_r:        in->player[0].button[2] = 1; break;
    case SDLK_f:        in->player[0].button[3] = 1; break;
    case SDLK_t:        in->player[0].skill_tree = 1; break;
    case SDLK_g:        in->player[0].stats_page = 1; break;
    case SDLK_j:        in->player[1].button[1] = 1; break;
    case SDLK_l:        in->player[1].button[2] = 1; break;
    case SDLK_o:        in->player[1].button[3] = 1; break;
    case SDLK_u:        in->player[1].skill_tree = 1; break;
    case SDLK_h:        in->player[1].stats_page = 1; break;
    case SDLK_1:        in->player[0].belt = 1; break;
    case SDLK_2:        in->player[0].belt = 2; break;
    case SDLK_3:        in->player[0].belt = 3; break;
    case SDLK_4:        in->player[0].belt = 4; break;
    case SDLK_TAB:      in->player[0].automap = 1; break;
    case SDLK_i:        in->player[0].inventory = 1; break;
    case SDLK_7:        in->player[1].belt = 1; break;
    case SDLK_8:        in->player[1].belt = 2; break;
    case SDLK_9:        in->player[1].belt = 3; break;
    case SDLK_0:        in->player[1].belt = 4; break;
    case SDLK_p:        in->player[1].inventory = 1; break;
    case SDLK_q:        in->player[0].drop = 1; break;
    case SDLK_c:        in->player[0].chat = 1; break;
    case SDLK_n:        in->player[1].chat = 1; break;
    case SDLK_BACKSPACE: in->menu_back = 1; in->player[0].back = 1; break;
    case SDLK_DELETE:   in->menu_delete = 1; in->player[1].back = 1; break;
    case SDLK_m:        in->player[1].drop = 1; break;
    default: break;
    }
}

static void on_button(InputState *in, const SDL_ControllerButtonEvent *b, int down)
{
    int i = pad_index(b->which);
    if (i < 0 || b->button >= SDL_CONTROLLER_BUTTON_MAX)
        return;
    Pad *pd = &s_pads[i];
    if (!down) {
        if (pd->held[b->button]) {
            pd->held[b->button] = 0;
            input_log("joystick %d: se soltó \"%s\": ya cuenta", i + 1, button_name(b->button));
        }
        return;
    }
    touch_pad_used(b->which); /* (un control de verdad: se esconden los táctiles) */
    if (pd->held[b->button])
        return; /* sigue trabado desde que se conectó */
    if (now_ms() - pd->opened_at < GRACE_MS) {
        pd->held[b->button] = 1;
        pd->stuck = 1;
        input_log("joystick %d: \"%s\" apretado apenas se conectó: no cuenta hasta que se suelte", i + 1,
                  button_name(b->button));
        return;
    }
    if (mirror_check(i, b->button))
        return;
    input_log("joystick %d: botón %s", i + 1, button_name(b->button));
    join(i);
    PlayerInput *p = pd->player >= 0 ? &in->player[pd->player] : NULL;
    /* Con Select apretado, Y y X van directo al árbol de habilidades y a los atributos */
    int with_select = SDL_GameControllerGetButton(pd->pad, SDL_CONTROLLER_BUTTON_BACK) && !pd->held[SDL_CONTROLLER_BUTTON_BACK];

    switch (b->button) {
    case SDL_CONTROLLER_BUTTON_A:
        in->confirm = 1;
        if (p) p->action = p->button[0] = 1;
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        if (p) p->potion_life = 1;
        break;
    case SDL_CONTROLLER_BUTTON_Y:
        if (p && with_select)
            p->skill_tree = 1;
        else if (p)
            p->button[3] = 1;
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        if (p) p->potion_stamina = 1;
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: /* R1: el automapa */
        if (p) p->automap = 1;
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: /* R3: las frases */
        if (p) p->chat = 1;
        break;
    case SDL_CONTROLLER_BUTTON_BACK:
        if (p && !SDL_GameControllerGetButton(pd->pad, SDL_CONTROLLER_BUTTON_START))
            p->inventory = 1;
        break;
    case SDL_CONTROLLER_BUTTON_START:
        /* Select + Start para salir (en Batocera no hay teclado a mano) */
        if (SDL_GameControllerGetButton(pd->pad, SDL_CONTROLLER_BUTTON_BACK) && !pd->held[SDL_CONTROLLER_BUTTON_BACK])
            in->quit = 1;
        else
            in->start = 1;
        break;
    case SDL_CONTROLLER_BUTTON_X:
        in->menu_delete = 1;
        if (p && with_select)
            p->stats_page = 1;
        else if (p)
            p->drop = p->button[2] = 1;
        break;
    case SDL_CONTROLLER_BUTTON_B:
        in->menu_back = 1;
        if (p) p->back = p->button[1] = 1;
        break;
    /* La cruceta: en los menús y paneles navega; en el juego toma la poción
       de esa casilla del cinturón (← 1, ↑ 2, → 3, ↓ 4) */
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        in->menu_left = 1;
        if (p) { p->nav_left = 1; p->belt = 1; }
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        in->menu_up = 1;
        if (p) { p->nav_up = 1; p->belt = 2; }
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        in->menu_right = 1;
        if (p) { p->nav_right = 1; p->belt = 3; }
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        in->menu_down = 1;
        if (p) { p->nav_down = 1; p->belt = 4; }
        break;
    default:
        break;
    }
}

static void on_axis(InputState *in, const SDL_ControllerAxisEvent *a)
{
    int i = pad_index(a->which);
    if (i < 0 || a->axis >= SDL_CONTROLLER_AXIS_MAX)
        return;
    Pad *pd = &s_pads[i];
    int on = a->value > 16000 || a->value < -16000;
    if (on != pd->axis_on[a->axis]) {
        pd->axis_on[a->axis] = on;
        if (on)
            touch_pad_used(a->which);
        input_log("joystick %d: eje %s %s (%.2f)", i + 1,
                  SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)a->axis), on ? "apretado" : "suelto",
                  a->value / 32767.0f);
    }
    if (a->axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
        int pressed = a->value > 16000;
        if (pressed && !pd->ltrigger && now_ms() - pd->opened_at >= GRACE_MS && !mirror_check(i, CODE_STICK + 3)) {
            join(i);
            if (pd->player >= 0)
                in->player[pd->player].potion_mana = 1;
        }
        pd->ltrigger = pressed;
        return;
    }
    if (a->axis != SDL_CONTROLLER_AXIS_LEFTX && a->axis != SDL_CONTROLLER_AXIS_LEFTY)
        return;
    if (!(pd->armed & ARM_STICK))
        return; /* el stick todavía no volvió al centro */
    float v = centered(pd, a->axis, a->value / 32767.0f);
    int dir = v < -MENU_STICK ? -1 : v > MENU_STICK ? 1 : 0;
    int *last = a->axis == SDL_CONTROLLER_AXIS_LEFTX ? &pd->stick_x : &pd->stick_dir;
    if (dir == *last)
        return;
    *last = dir;
    if (!dir || mirror_check(i, CODE_STICK + (a->axis == SDL_CONTROLLER_AXIS_LEFTX ? 0 : 1)))
        return;
    join(i);
    PlayerInput *p = pd->player >= 0 ? &in->player[pd->player] : NULL;
    if (a->axis == SDL_CONTROLLER_AXIS_LEFTX) {
        if (dir < 0) { in->menu_left = 1; if (p) p->nav_left = 1; }
        if (dir > 0) { in->menu_right = 1; if (p) p->nav_right = 1; }
    } else {
        if (dir < 0) { in->menu_up = 1; if (p) p->nav_up = 1; }
        if (dir > 0) { in->menu_down = 1; if (p) p->nav_down = 1; }
    }
}

int input_any(const InputState *in)
{
    if (in->confirm || in->start || in->escape || in->menu_up || in->menu_down || in->menu_left || in->menu_right ||
        in->menu_back || in->menu_delete || in->mouse_down)
        return 1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const PlayerInput *p = &in->player[i];
        if (p->move_x != 0.0f || p->move_y != 0.0f || p->sprint || p->action || p->inventory || p->skill_tree || p->stats_page ||
            p->potion_life || p->potion_mana || p->potion_stamina || p->belt || p->automap || p->chat || p->drop || p->back ||
            p->button[0] || p->button[1] || p->button[2] || p->button[3])
            return 1;
    }
    return 0;
}

/* ---- D2_FAKE_PAD (pruebas): joysticks virtuales de SDL con los problemas de las portátiles ----
   Uno por ';', con "clave=valor" separados por ',':
     lx=, ly=   donde descansa el stick (-1..1; una palanca de ADC sin calibrar, como las Anbernic H700: ly=0.35)
     noise=     cuánto tiembla al azar en cada tick
     float=1    sin palanca: el eje Y salta al azar por todo el recorrido (un ADC suelto)
     mirror=1   el mismo control visto dos veces (dos joysticks iguales que hacen lo mismo)
     push=T1-T2 entre esos ticks, el stick abajo de verdad
     a=T        el botón A en el tick T (down=T: la cruceta abajo)
     stuck=1    la cruceta abajo apretada desde que se conecta
     at=T       se conecta en el tick T
   Los tiempos (lo apretado al conectar, el eco) van con un reloj de 40 ms por tick. */
#if SDL_VERSION_ATLEAST(2, 24, 0)
#define FAKE_MAX 4
typedef struct {
    float lx, ly, noise, fly;
    int   floating, mirror, push0, push1, a_at, down_at, stuck, at, n;
    SDL_Joystick *js[2];
} FakePad;
static FakePad  s_fake[FAKE_MAX];
static int      s_nfake = -1;
static uint32_t s_fake_rnd = 1;

static float fake_rand(void)
{
    s_fake_rnd = s_fake_rnd * 1103515245u + 12345u;
    return (float)((s_fake_rnd >> 8) & 0xffff) / 32768.0f - 1.0f;
}

static void fake_parse(void)
{
    s_nfake = 0;
    const char *p = getenv("D2_FAKE_PAD");
    while (p && *p && s_nfake < FAKE_MAX) {
        FakePad *f = &s_fake[s_nfake++];
        memset(f, 0, sizeof *f);
        f->push0 = f->push1 = f->a_at = f->down_at = -1;
        while (*p && *p != ';') {
            char key[16] = "", val[32] = "";
            int n = 0;
            if (sscanf(p, "%15[^=;,]=%31[^;,]%n", key, val, &n) < 2 || n <= 0) {
                p++;
                continue;
            }
            p += n;
            if (!strcmp(key, "lx")) f->lx = (float)atof(val);
            else if (!strcmp(key, "ly")) f->ly = (float)atof(val);
            else if (!strcmp(key, "noise")) f->noise = (float)atof(val);
            else if (!strcmp(key, "float")) f->floating = atoi(val);
            else if (!strcmp(key, "mirror")) f->mirror = atoi(val);
            else if (!strcmp(key, "push")) sscanf(val, "%d-%d", &f->push0, &f->push1);
            else if (!strcmp(key, "a")) f->a_at = atoi(val);
            else if (!strcmp(key, "down")) f->down_at = atoi(val);
            else if (!strcmp(key, "stuck")) f->stuck = atoi(val);
            else if (!strcmp(key, "at")) f->at = atoi(val);
            if (*p == ',')
                p++;
        }
        if (*p == ';')
            p++;
    }
}

static Sint16 fake_axis(float v)
{
    v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
    return (Sint16)(v * 32767.0f);
}

int input_fake_pads(int tick)
{
    if (s_nfake < 0)
        fake_parse();
    if (!s_nfake)
        return 0;
    s_fake_clock = 1000u + (Uint32)tick * 40u;
    for (int i = 0; i < s_nfake; i++) {
        FakePad *f = &s_fake[i];
        if (tick < f->at)
            continue;
        for (int k = f->n; k < (f->mirror ? 2 : 1); k++) { /* (el eco: el mismo modelo, mismo GUID) */
            SDL_VirtualJoystickDesc d;
            SDL_zero(d);
            d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            d.naxes = SDL_CONTROLLER_AXIS_MAX;
            d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            d.vendor_id = 0x1d2a;
            d.product_id = (Uint16)(0x4000 + i);
            d.name = "D2 falso (como un RG40XX)";
            int idx = SDL_JoystickAttachVirtualEx(&d);
            f->js[k] = idx >= 0 ? SDL_JoystickOpen(idx) : NULL;
            if (!f->js[k])
                fprintf(stderr, "D2_FAKE_PAD: no pude crear el joystick virtual: %s\n", SDL_GetError());
            f->n = k + 1;
        }
        float lx = f->lx + f->noise * fake_rand(), ly = f->ly + f->noise * fake_rand();
        if (f->floating) { /* (un ADC suelto: salta a cualquier lado cada dos ticks) */
            if (tick % 2 == 0)
                f->fly = fake_rand();
            ly = f->fly;
        }
        if (tick >= f->push0 && tick <= f->push1)
            ly = 1.0f;
        for (int k = 0; k < f->n; k++) {
            if (!f->js[k])
                continue;
            SDL_JoystickSetVirtualAxis(f->js[k], SDL_CONTROLLER_AXIS_LEFTX, fake_axis(lx));
            SDL_JoystickSetVirtualAxis(f->js[k], SDL_CONTROLLER_AXIS_LEFTY, fake_axis(ly));
            SDL_JoystickSetVirtualButton(f->js[k], SDL_CONTROLLER_BUTTON_A, tick == f->a_at);
            SDL_JoystickSetVirtualButton(f->js[k], SDL_CONTROLLER_BUTTON_DPAD_DOWN, f->stuck || tick == f->down_at);
        }
    }
    return 1;
}
#else
int input_fake_pads(int tick)
{
    (void)tick;
    return 0;
}
#endif

static void normalize(PlayerInput *p)
{
    float len = sqrtf(p->move_x * p->move_x + p->move_y * p->move_y);
    if (len > 1.0f) {
        p->move_x /= len;
        p->move_y /= len;
    }
}

void input_poll(InputState *in)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (touch_event(in, &e))
            continue;
        switch (e.type) {
        case SDL_QUIT:                    in->quit = 1; break;
        case SDL_APP_WILLENTERBACKGROUND: /* (Android: se va a otra app; el sistema puede cerrar el juego) */
        case SDL_APP_TERMINATING:         in->suspend = 1; break;
        case SDL_RENDER_DEVICE_RESET:     video_device_reset(); break;
        case SDL_CONTROLLERDEVICEADDED:   open_pad(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: close_pad(e.cdevice.which); break;
        case SDL_KEYDOWN:                 on_key(in, &e.key); break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (e.cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE)
                in->any_press = 1;
            on_button(in, &e.cbutton, 1);
            break;
        case SDL_CONTROLLERBUTTONUP:      on_button(in, &e.cbutton, 0); break;
        case SDL_CONTROLLERAXISMOTION:    on_axis(in, &e.caxis); break;
        case SDL_MOUSEMOTION:
            in->mouse_x = e.motion.x;
            in->mouse_y = e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            in->mouse_x = e.button.x;
            in->mouse_y = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT)
                in->mouse_down = e.type == SDL_MOUSEBUTTONDOWN;
            if (e.type == SDL_MOUSEBUTTONDOWN)
                in->any_press = 1;
            break;
        }
    }

    touch_update(in); /* (los dedos: el joystick virtual y el mouse) */

    for (int i = 0; i < MAX_PLAYERS; i++) {
        in->player[i].move_x = in->player[i].move_y = 0.0f;
        in->player[i].sprint = 0;
    }

    /* Teclado. Una tecla apretada desde antes de arrancar (la puede dejar
       así otro programa) no cuenta hasta que se suelta. */
    int nkeys = 0;
    const Uint8 *ks = SDL_GetKeyboardState(&nkeys);
    if (nkeys > SDL_NUM_SCANCODES)
        nkeys = SDL_NUM_SCANCODES;
    if (!s_keys_checked) {
        s_keys_checked = 1;
        for (int sc = 0; sc < nkeys; sc++)
            if (ks[sc]) {
                s_key_blocked[sc] = 1;
                input_log("la tecla %s ya está apretada al arrancar: no cuenta hasta que se suelte",
                          SDL_GetScancodeName((SDL_Scancode)sc));
            }
    }
    Uint8 k[SDL_NUM_SCANCODES];
    for (int sc = 0; sc < SDL_NUM_SCANCODES; sc++) {
        int down = sc < nkeys && ks[sc];
        if (!down && s_key_blocked[sc]) {
            s_key_blocked[sc] = 0;
            input_log("la tecla %s se soltó", SDL_GetScancodeName((SDL_Scancode)sc));
        }
        k[sc] = (Uint8)(down && !s_key_blocked[sc]);
    }
    PlayerInput *p1 = &in->player[0], *p2 = &in->player[1];
    p1->sprint = k[SDL_SCANCODE_LSHIFT];
    p2->sprint = k[SDL_SCANCODE_RSHIFT];
    if (k[SDL_SCANCODE_A]) p1->move_x -= 1.0f;
    if (k[SDL_SCANCODE_D]) p1->move_x += 1.0f;
    if (k[SDL_SCANCODE_W]) p1->move_y -= 1.0f;
    if (k[SDL_SCANCODE_S]) p1->move_y += 1.0f;
    /* Los botones de habilidad mantenidos (las teclas de on_key) */
    p1->held[0] = k[SDL_SCANCODE_SPACE];
    p1->held[1] = k[SDL_SCANCODE_E];
    p1->held[2] = k[SDL_SCANCODE_R];
    p1->held[3] = k[SDL_SCANCODE_F];
    p2->held[0] = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
    p2->held[1] = k[SDL_SCANCODE_J];
    p2->held[2] = k[SDL_SCANCODE_L];
    p2->held[3] = k[SDL_SCANCODE_O];
    if (k[SDL_SCANCODE_LEFT])  p2->move_x -= 1.0f;
    if (k[SDL_SCANCODE_RIGHT]) p2->move_x += 1.0f;
    if (k[SDL_SCANCODE_UP])    p2->move_y -= 1.0f;
    if (k[SDL_SCANCODE_DOWN])  p2->move_y += 1.0f;

    /* Joysticks: el stick (caminar) y correr se miran en cada tick */
    mirror_settle();
    for (int i = 0; i < MAX_PADS; i++) {
        Pad *pd = &s_pads[i];
        if (!pd->pad)
            continue;
        int active = pad_active(pd);
        Uint32 now = now_ms();
        if (now - pd->opened_at < GRACE_MS && (active & pd->armed)) {
            pd->armed &= ~active; /* apareció apenas se conectó: como si ya estuviera apretado */
            pd->stuck = 1;
        }
        int newly = ~active & ~pd->armed & ARM_ALL & ~ARM_STICK;
        /* El stick cuenta recién quieto CALM_MS (variando menos de STEADY) a menos de CENTER_MAX del medio, y ese
           lugar pasa a ser su medio (las palancas de ADC de las portátiles, como las Anbernic H700, no dan 0). Uno
           que flota (un modelo sin palanca) nunca queda quieto: no cuenta. Después, el medio sigue de a poco lo que
           dé quieto (se corren con el tiempo). */
        float lx = axis(pd->pad, SDL_CONTROLLER_AXIS_LEFTX), ly = axis(pd->pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (pd->touch) { /* (los dedos: el medio es el medio de verdad, sin esperar ni correrlo) */
            pd->cx = pd->cy = 0.0f;
            if (!(pd->armed & ARM_STICK))
                active = pad_active(pd);
            pd->armed |= ARM_STICK;
        } else if (!(pd->armed & ARM_STICK)) {
            if (!pd->calm_since || fabsf(lx - pd->sx) > STEADY || fabsf(ly - pd->sy) > STEADY || fabsf(lx) > CENTER_MAX ||
                fabsf(ly) > CENTER_MAX) {
                pd->sx = lx;
                pd->sy = ly;
                pd->calm_since = now ? now : 1;
            } else if (now - pd->calm_since >= CALM_MS) {
                pd->cx = pd->sx;
                pd->cy = pd->sy;
                pd->armed |= ARM_STICK;
                active = pad_active(pd);
                input_log("joystick %d: el stick quedó quieto en (%.2f, %.2f): ese es su medio, ya cuenta", i + 1, pd->cx, pd->cy);
            }
        } else if (!(active & ARM_STICK) && fabsf(lx) <= CENTER_MAX && fabsf(ly) <= CENTER_MAX) {
            pd->cx += (lx - pd->cx) * 0.02f;
            pd->cy += (ly - pd->cy) * 0.02f;
        }
        for (int bit = 1; bit <= ARM_RB; bit <<= 1)
            if (newly & bit)
                input_log("joystick %d: %s se soltó: ya cuenta", i + 1, arm_name(bit));
        pd->armed |= newly;
        int use = active & pd->armed;
        if ((use & ARM_STICK) && !pd->mirror) { /* (un eco que mueve el stick igual que su par) */
            for (int k = 0; k < MAX_PADS; k++) {
                const Pad *o = &s_pads[k];
                if (k == i || !o->pad || strcmp(o->guid, pd->guid) != 0 || !(o->armed & ARM_STICK))
                    continue;
                if (fabsf(axis(o->pad, SDL_CONTROLLER_AXIS_LEFTX) - axis(pd->pad, SDL_CONTROLLER_AXIS_LEFTX)) < 0.02f &&
                    fabsf(axis(o->pad, SDL_CONTROLLER_AXIS_LEFTY) - axis(pd->pad, SDL_CONTROLLER_AXIS_LEFTY)) < 0.02f) {
                    pd->last_ms[CODE_STICK + 2] = now;
                    mirror_check(k, CODE_STICK + 2);
                }
            }
        }
        if (use && !pd->mirror)
            join(i);
        if (pd->player < 0 || pd->mirror)
            continue;
        PlayerInput *p = &in->player[pd->player];
        if (use & ARM_STICK) {
            p->move_x += stick(pd, SDL_CONTROLLER_AXIS_LEFTX);
            p->move_y += stick(pd, SDL_CONTROLLER_AXIS_LEFTY);
        }
        if (use & ARM_RT) /* R2 */
            p->sprint = 1;
        static const SDL_GameControllerButton k_hold[4] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
                                                            SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y };
        for (int b = 0; b < 4; b++) /* (los trabados desde que se conectó no cuentan) */
            if (SDL_GameControllerGetButton(pd->pad, k_hold[b]) && !pd->held[k_hold[b]])
                p->held[b] = 1;
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
        normalize(&in->player[i]);
}
