#include "touch.h"
#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if SDL_VERSION_ATLEAST(2, 24, 0)

/* ---- Touch controls, laid out like D2Android (github.com/N1v0k/D2Android, D2TouchOverlay) ----
   A floating joystick in the left half of the screen; "attack" (A) big at the bottom right with B, X and Y on
   an arc around it, each showing the icon of its skill; the healing and mana potions at the right edge; a menu
   row at the top right (pause, inventory, character, skills, map, run, phrases) and a small d-pad at the top
   left (the belt slots, and moving through the panels). Touching the game picture elsewhere walks there, like
   holding the left mouse button on the PC. The game sees a virtual SDL gamepad (and keys for the menu row), so
   everything else is the gamepad code it already has. Sizes are in dp, as on Android. */

enum {
    C_A, C_B, C_X, C_Y,      /* the four skill buttons of the gamepad (A also talks, picks up, uses) */
    C_HEAL, C_MANA,          /* LB and LT: the first healing / mana potion of the belt */
    C_MENU, C_INV, C_CHAR, C_SKILL, C_MAP, C_RUN, C_CHAT, /* the menu row (keys; RUN: the game's "always run") */
    C_DPAD,
    C_NUM_BOXES,
    C_STICK = C_NUM_BOXES, C_WALK, C_COUNT
};
#define FIRST_MENU C_MENU
#define LAST_MENU  C_CHAT

static const char *const k_menu_label[LAST_MENU - FIRST_MENU + 1] = { "MENU", "INV", "CHAR", "SKILL", "MAP", "RUN", "CHAT" };
static const SDL_Keycode k_menu_key[LAST_MENU - FIRST_MENU + 1] = { SDLK_ESCAPE, SDLK_i, SDLK_g, SDLK_t, SDLK_TAB, 0, SDLK_c };

typedef struct {
    float cx, cy, w, h;
    int   circle;
} Box;

#define MAX_FINGERS 10
typedef struct {
    int          used;
    SDL_FingerID id;
    int          ctl;
    float        x, y;   /* window pixels */
    float        bx, by; /* the joystick: where its base is (it follows a finger that goes further than the radius) */
} Finger;

static int           s_on;       /* touch controls in use */
static int           s_hidden;   /* hidden: a real controller was used */
static SDL_Joystick *s_js;       /* the virtual gamepad */
static SDL_JoystickID s_js_id = -1;
static Finger        s_f[MAX_FINGERS];
static int           s_mouse;    /* the walking finger holds the mouse button */
static int           s_lw, s_lh; /* window size of the last layout */
static int           s_gw = 800, s_gh = 600; /* the game picture's size (800x600, wider with widescreen) */
static float         s_dp;       /* one Android dp in pixels */
static SDL_FRect     s_pic;      /* where the game picture is in the window (s_gw x s_gh scaled) */
static Box           s_box[C_COUNT];
static float         s_stick_r, s_idle_x, s_idle_y;
static float         s_fade = 1.0f; /* < 1: what is drawn now is fainter (a panel of the game is open under it) */

/* the skill icons of A, B, X and Y (from the game, game_button_icon) */
#define ICON_PX 64
static SDL_Texture *s_icon_tex[4];
static int          s_icon_ok[4];
static int          s_icon_frame;
static SDL_Renderer *s_icon_ren;

int touch_enabled(void)
{
    return s_on;
}

int touch_is_pad(SDL_JoystickID id)
{
    return s_on && s_js && id == s_js_id;
}

void touch_init(void)
{
#ifdef __ANDROID__
    s_on = !getenv("D2_TOUCH") || atoi(getenv("D2_TOUCH")) != 0;
#else
    s_on = getenv("D2_TOUCH") && atoi(getenv("D2_TOUCH")) != 0;
#endif
    if (!s_on)
        return;
    SDL_VirtualJoystickDesc d;
    SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.vendor_id = 0x1d2a;
    d.product_id = 0x7001;
    d.name = "Touch controls";
    int idx = SDL_JoystickAttachVirtualEx(&d);
    s_js = idx >= 0 ? SDL_JoystickOpen(idx) : NULL;
    if (!s_js) {
        SDL_Log("touch: no pude crear el joystick virtual: %s", SDL_GetError());
        s_on = 0;
        return;
    }
    s_js_id = SDL_JoystickInstanceID(s_js);
    /* the triggers at rest: the axis minimum (SDL takes that as their zero) */
    SDL_JoystickSetVirtualAxis(s_js, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MIN);
    SDL_JoystickSetVirtualAxis(s_js, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
    SDL_Log("touch: controles táctiles activos");
}

void touch_shutdown(void)
{
    for (int i = 0; i < 4; i++) {
        if (s_icon_tex[i])
            SDL_DestroyTexture(s_icon_tex[i]);
        s_icon_tex[i] = NULL;
    }
    if (s_js) {
        int idx = -1;
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_JoystickGetDeviceInstanceID(i) == s_js_id)
                idx = i;
        SDL_JoystickClose(s_js);
        if (idx >= 0)
            SDL_JoystickDetachVirtual(idx);
    }
    s_js = NULL;
    s_js_id = -1;
    s_on = 0;
}

/* ---- Layout (D2Android's, in dp from the bottom right corner) ---- */
static float display_dp(SDL_Renderer *r, int H)
{
    float ddpi = 0.0f, dp = 0.0f;
    SDL_Window *w = r ? SDL_RenderGetWindow(r) : NULL;
    if (w && SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(w), &ddpi, NULL, NULL) == 0 && ddpi > 0.0f)
        dp = ddpi / 160.0f;
    /* (no DPI, or a strange one: like a phone, ~420 dp tall) */
    if (dp < H / 600.0f || dp > H / 250.0f)
        dp = H / 420.0f;
    return dp;
}

/* Where the game picture goes: as big as fits, keeping its shape (as SDL's logical size does) */
static void place_picture(int W, int H, int lw, int lh)
{
    if (lw > 0 && lh > 0) {
        s_gw = lw;
        s_gh = lh;
    }
    float gw = (float)H * s_gw / s_gh, gh = (float)H;
    if (gw > W) {
        gw = (float)W;
        gh = (float)W * s_gh / s_gw;
    }
    s_pic = (SDL_FRect){ (W - gw) / 2.0f, (H - gh) / 2.0f, gw, gh };
}

static void layout(SDL_Renderer *r, int W, int H)
{
    if (W == s_lw && H == s_lh)
        return;
    s_lw = W;
    s_lh = H;
    float dp = s_dp = display_dp(r, H);

    memset(s_box, 0, sizeof s_box);
    Box *b = s_box;
    /* attack bottom right, B / X / Y on an arc around it (left, up-left, up) */
    float ax = W - 100 * dp, ay = H - 92 * dp;
    b[C_A] = (Box){ ax, ay, 92 * dp, 92 * dp, 1 };
    static const float k_arc[3] = { 180.0f, 225.0f, 270.0f };
    for (int i = 0; i < 3; i++) {
        float a = k_arc[i] * 3.14159265f / 180.0f;
        b[C_B + i] = (Box){ ax + cosf(a) * 112 * dp, ay + sinf(a) * 112 * dp, 58 * dp, 58 * dp, 1 };
    }
    /* the potions at the right edge, above the attack */
    b[C_HEAL] = (Box){ W - 30 * dp, H - 160 * dp, 50 * dp, 50 * dp, 1 };
    b[C_MANA] = (Box){ W - 30 * dp, H - 222 * dp, 50 * dp, 50 * dp, 1 };
    /* the menu row, top right, right to left */
    float half = 19 * dp, gap = 5 * dp, x = W - 8 * dp;
    for (int c = FIRST_MENU; c <= LAST_MENU; c++) {
        float hw = c == C_SKILL || c == C_MENU || c == C_CHAR || c == C_CHAT ? 26 * dp : 21 * dp;
        b[c] = (Box){ x - hw, 8 * dp + half, 2 * hw, 2 * half, 0 };
        x -= 2 * hw + gap;
    }
    /* the d-pad, small, top left */
    b[C_DPAD] = (Box){ 58 * dp, 96 * dp, 84 * dp, 84 * dp, 0 };
    /* the joystick: floating in the left half; where it waits */
    s_stick_r = 64 * dp;
    s_idle_x = fmaxf(0.15f * W, 110 * dp);
    s_idle_y = H - 120 * dp;
    b[C_STICK] = (Box){ s_idle_x, s_idle_y, 2 * s_stick_r, 2 * s_stick_r, 1 };
}

/* What is under a finger: the nearest box it touches (with a margin), else the joystick in the left half, else
   walking on the picture */
static int hit(float x, float y)
{
    float best = 1e9f, m = 6 * s_dp;
    int ctl = -1;
    for (int c = 0; c < C_NUM_BOXES; c++) {
        const Box *b = &s_box[c];
        if (b->w <= 0.0f)
            continue;
        float dx = x - b->cx, dy = y - b->cy, d;
        if (b->circle)
            d = sqrtf(dx * dx + dy * dy) - (b->w * 0.5f + m);
        else
            d = fmaxf(fabsf(dx) - (b->w * 0.5f + m * 0.5f), fabsf(dy) - (b->h * 0.5f + m * 0.5f));
        if (d <= 0.0f && d < best) {
            best = d;
            ctl = c;
        }
    }
    if (ctl >= 0)
        return ctl;
    if (x < s_lw * 0.5f)
        return C_STICK;
    if (x >= s_pic.x && x < s_pic.x + s_pic.w && y >= s_pic.y && y < s_pic.y + s_pic.h)
        return C_WALK;
    return -1;
}

static void release_all(void)
{
    memset(s_f, 0, sizeof s_f);
}

void touch_pad_used(SDL_JoystickID id)
{
    if (!s_on || !s_js || id == s_js_id || s_hidden)
        return;
    s_hidden = 1;
    release_all();
}

static void send_key(SDL_Keycode key, int down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    SDL_PushEvent(&e);
}

/* Where a finger is, in window pixels. SDL's renderer rewrites the finger events into the logical area
   and clamps whatever is outside it (the side bars, where most controls are) to its edge, so the position comes
   from the touch device itself; only if the finger is no longer there (it was lifted), the event's position is
   mapped back from the picture. */
static void finger_pos(const SDL_TouchFingerEvent *t, float *x, float *y)
{
    int n = SDL_GetNumTouchFingers(t->touchId);
    for (int i = 0; i < n; i++) {
        SDL_Finger *f = SDL_GetTouchFinger(t->touchId, i);
        if (f && f->id == t->fingerId) {
            *x = f->x * s_lw;
            *y = f->y * s_lh;
            return;
        }
    }
    *x = s_pic.x + t->x * s_pic.w;
    *y = s_pic.y + t->y * s_pic.h;
}

int touch_event(InputState *in, const SDL_Event *e)
{
    if (!s_on || (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERUP && e->type != SDL_FINGERMOTION))
        return 0;
    if (e->tfinger.touchId == SDL_MOUSE_TOUCHID)
        return 1;
    if (s_lw <= 0 || s_lh <= 0)
        return 1; /* (nothing drawn yet) */
    float x, y;
    finger_pos(&e->tfinger, &x, &y);
    Finger *f = NULL;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (s_f[i].used && s_f[i].id == e->tfinger.fingerId)
            f = &s_f[i];
    if (e->type == SDL_FINGERDOWN) {
        in->any_press = 1; /* (skips a cinematic) */
        if (s_hidden) {   /* they come back: this touch does nothing else */
            s_hidden = 0;
            return 1;
        }
        if (!f)
            for (int i = 0; i < MAX_FINGERS && !f; i++)
                if (!s_f[i].used)
                    f = &s_f[i];
        if (!f)
            return 1;
        memset(f, 0, sizeof *f);
        f->ctl = hit(x, y);
        if (f->ctl < 0)
            return 1;
        f->used = 1;
        f->id = e->tfinger.fingerId;
        f->x = f->bx = x;
        f->y = f->by = y;
        if (f->ctl >= FIRST_MENU && f->ctl <= LAST_MENU) {
            if (f->ctl == C_RUN)
                game_toggle_always_run();
            else
                send_key(k_menu_key[f->ctl - FIRST_MENU], 1);
        }
        return 1;
    }
    if (!f)
        return 1;
    if (e->type == SDL_FINGERUP) {
        if (f->ctl >= FIRST_MENU && f->ctl <= LAST_MENU && f->ctl != C_RUN)
            send_key(k_menu_key[f->ctl - FIRST_MENU], 0);
        f->used = 0;
        return 1;
    }
    f->x = x;
    f->y = y;
    if (f->ctl == C_STICK) { /* (the base follows a finger that goes further than the radius) */
        float dx = x - f->bx, dy = y - f->by, d = sqrtf(dx * dx + dy * dy);
        if (d > s_stick_r) {
            f->bx = x - dx * s_stick_r / d;
            f->by = y - dy * s_stick_r / d;
        }
    }
    return 1;
}

/* The d-pad direction under a finger: 0 none, 1 left, 2 up, 3 right, 4 down */
static int dpad_dir(float x, float y)
{
    const Box *b = &s_box[C_DPAD];
    float dx = x - b->cx, dy = y - b->cy;
    if (fabsf(dx) < b->w * 0.1f && fabsf(dy) < b->h * 0.1f)
        return 0;
    if (fabsf(dx) > fabsf(dy))
        return dx < 0 ? 1 : 3;
    return dy < 0 ? 2 : 4;
}

static Sint16 to_axis(float v)
{
    v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
    return (Sint16)(v * 32767.0f);
}

void touch_update(InputState *in)
{
    if (!s_on || !s_js)
        return;
    int down[C_COUNT] = { 0 }, dir = 0;
    float sx = 0.0f, sy = 0.0f, wx = 0.0f, wy = 0.0f;
    int walk = 0;
    if (!s_hidden)
        for (int i = 0; i < MAX_FINGERS; i++) {
            const Finger *f = &s_f[i];
            if (!f->used)
                continue;
            down[f->ctl] = 1;
            if (f->ctl == C_DPAD) {
                dir = dpad_dir(f->x, f->y);
            } else if (f->ctl == C_STICK) {
                /* past a small dead zone (5 dp) the stick is at once over the game's (0.25): it answers to small
                   movements, and the direction is the finger's, exactly */
                float dx = f->x - f->bx, dy = f->y - f->by, d = sqrtf(dx * dx + dy * dy), dz = 5 * s_dp;
                if (d > dz) {
                    float mag = 0.3f + 0.7f * (d - dz) / (s_stick_r - dz);
                    mag = mag > 1.0f ? 1.0f : mag;
                    sx = dx / d * mag;
                    sy = dy / d * mag;
                }
            } else if (f->ctl == C_WALK) {
                walk = 1;
                wx = (f->x - s_pic.x) * s_gw / s_pic.w;
                wy = (f->y - s_pic.y) * s_gh / s_pic.h;
            }
        }
    static const int k_btn[4] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
                                  SDL_CONTROLLER_BUTTON_Y };
    for (int c = 0; c < 4; c++)
        SDL_JoystickSetVirtualButton(s_js, k_btn[c], (Uint8)down[C_A + c]);
    SDL_JoystickSetVirtualButton(s_js, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, (Uint8)down[C_HEAL]);
    SDL_JoystickSetVirtualButton(s_js, SDL_CONTROLLER_BUTTON_DPAD_LEFT, dir == 1);
    SDL_JoystickSetVirtualButton(s_js, SDL_CONTROLLER_BUTTON_DPAD_UP, dir == 2);
    SDL_JoystickSetVirtualButton(s_js, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, dir == 3);
    SDL_JoystickSetVirtualButton(s_js, SDL_CONTROLLER_BUTTON_DPAD_DOWN, dir == 4);
    SDL_JoystickSetVirtualAxis(s_js, SDL_CONTROLLER_AXIS_LEFTX, to_axis(sx));
    SDL_JoystickSetVirtualAxis(s_js, SDL_CONTROLLER_AXIS_LEFTY, to_axis(sy));
    SDL_JoystickSetVirtualAxis(s_js, SDL_CONTROLLER_AXIS_TRIGGERLEFT, down[C_MANA] ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
    SDL_JoystickSetVirtualAxis(s_js, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
    /* touching the picture: like the left mouse button held there (player 1 walks towards the finger) */
    if (walk) {
        in->mouse_x = (int)wx;
        in->mouse_y = (int)wy;
        in->mouse_down = 1;
        s_mouse = 1;
    } else if (s_mouse) {
        in->mouse_down = 0;
        s_mouse = 0;
    }
}

/* ---- Drawing: triangles with SDL_RenderGeometry, in window pixels ---- */
static const unsigned char k_glyph[][8] = { /* letter and 7 rows of 5 dots */
    { 'A', 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, { 'B', 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
    { 'C', 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, { 'D', 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
    { 'E', 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, { 'F', 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
    { 'G', 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, { 'H', 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    { 'I', 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, { 'J', 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c },
    { 'K', 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 'L', 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
    { 'M', 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 'N', 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 },
    { 'O', 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, { 'P', 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
    { 'Q', 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, { 'R', 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
    { 'S', 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, { 'T', 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    { 'U', 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, { 'V', 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
    { 'W', 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, { 'X', 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
    { 'Y', 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 }, { 'Z', 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    { '0', 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, { '1', 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
    { '2', 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, { '3', 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
    { '4', 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
};

static SDL_Color faded(SDL_Color c)
{
    c.a = (Uint8)(c.a * s_fade);
    return c;
}

static void text(SDL_Renderer *r, float cx, float cy, float p, const char *s, SDL_Color c)
{
    c = faded(c);
    SDL_FRect px[300];
    int n = 0, len = (int)strlen(s);
    float x = cx - (len * 6 - 1) * p * 0.5f, y = cy - 3.5f * p;
    for (int i = 0; i < len; i++, x += 6.0f * p)
        for (size_t g = 0; g < sizeof k_glyph / sizeof k_glyph[0]; g++) {
            if (k_glyph[g][0] != (unsigned char)s[i])
                continue;
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                    if ((k_glyph[g][1 + row] >> (4 - col)) & 1 && n < 300)
                        px[n++] = (SDL_FRect){ x + col * p, y + row * p, p + 1.0f, p + 1.0f }; /* (+1: no gaps) */
        }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRectsF(r, px, n);
}

/* The outline of a box (circle or rounded rectangle), shrunk by `in` */
#define OUTLINE_PTS 48
static int outline(const Box *b, float in, SDL_FPoint *pt)
{
    if (b->circle) {
        float rad = b->w * 0.5f - in;
        for (int i = 0; i < OUTLINE_PTS; i++) {
            float a = (float)i * 6.2831853f / OUTLINE_PTS;
            pt[i] = (SDL_FPoint){ b->cx + cosf(a) * rad, b->cy + sinf(a) * rad };
        }
        return OUTLINE_PTS;
    }
    float hw = b->w * 0.5f - in, hh = b->h * 0.5f - in;
    float cr = fminf(hw, hh) * 0.35f;
    static const float sx[4] = { 1, -1, -1, 1 }, sy[4] = { 1, 1, -1, -1 };
    int n = 0;
    for (int k = 0; k < 4; k++) {
        float ccx = b->cx + sx[k] * (hw - cr), ccy = b->cy + sy[k] * (hh - cr);
        for (int i = 0; i < OUTLINE_PTS / 4; i++) {
            float a = (k + (float)i / (OUTLINE_PTS / 4 - 1)) * 1.5707963f;
            pt[n++] = (SDL_FPoint){ ccx + cosf(a) * cr, ccy + sinf(a) * cr };
        }
    }
    return n;
}

static void fill_box(SDL_Renderer *r, const Box *b, float in, SDL_Color c)
{
    c = faded(c);
    SDL_FPoint pt[OUTLINE_PTS];
    int n = outline(b, in, pt);
    SDL_Vertex v[OUTLINE_PTS + 1];
    int idx[OUTLINE_PTS * 3];
    v[0] = (SDL_Vertex){ { b->cx, b->cy }, c, { 0, 0 } };
    for (int i = 0; i < n; i++) {
        v[i + 1] = (SDL_Vertex){ pt[i], c, { 0, 0 } };
        idx[i * 3] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = (i + 1) % n + 1;
    }
    SDL_RenderGeometry(r, NULL, v, n + 1, idx, n * 3);
}

static void ring_box(SDL_Renderer *r, const Box *b, float thick, SDL_Color c)
{
    c = faded(c);
    SDL_FPoint o[OUTLINE_PTS], i2[OUTLINE_PTS];
    int n = outline(b, 0.0f, o);
    outline(b, thick, i2);
    SDL_Vertex v[OUTLINE_PTS * 2];
    int idx[OUTLINE_PTS * 6];
    for (int i = 0; i < n; i++) {
        v[i * 2] = (SDL_Vertex){ o[i], c, { 0, 0 } };
        v[i * 2 + 1] = (SDL_Vertex){ i2[i], c, { 0, 0 } };
        int j = (i + 1) % n;
        int *t = &idx[i * 6];
        t[0] = i * 2; t[1] = i * 2 + 1; t[2] = j * 2;
        t[3] = j * 2; t[4] = i * 2 + 1; t[5] = j * 2 + 1;
    }
    SDL_RenderGeometry(r, NULL, v, n * 2, idx, n * 6);
}

static void triangle(SDL_Renderer *r, float x0, float y0, float x1, float y1, float x2, float y2, SDL_Color c)
{
    c = faded(c);
    SDL_Vertex v[3] = { { { x0, y0 }, c, { 0, 0 } }, { { x1, y1 }, c, { 0, 0 } }, { { x2, y2 }, c, { 0, 0 } } };
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

static const SDL_Color k_fill = { 12, 10, 8, 140 }, k_fill_on = { 255, 240, 200, 110 };
static const SDL_Color k_edge = { 150, 128, 90, 200 }, k_edge_on = { 255, 225, 150, 255 }; /* (gold, as the game) */
static const SDL_Color k_text = { 225, 210, 175, 235 };

/* The skill icons of A, B, X and Y, from the game, every few frames (they change with the skills, mana, cooldowns) */
static void refresh_icons(SDL_Renderer *r)
{
    if (s_icon_ren != r) {
        for (int i = 0; i < 4; i++) {
            if (s_icon_tex[i])
                SDL_DestroyTexture(s_icon_tex[i]);
            s_icon_tex[i] = NULL;
        }
        s_icon_ren = r;
    }
    if (s_icon_frame++ % 6)
        return;
    static uint32_t buf[ICON_PX * ICON_PX];
    for (int b = 0; b < 4; b++) {
        s_icon_ok[b] = game_button_icon(b, buf, ICON_PX);
        if (!s_icon_ok[b])
            continue;
        if (!s_icon_tex[b]) {
            s_icon_tex[b] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ICON_PX, ICON_PX);
            if (!s_icon_tex[b]) {
                s_icon_ok[b] = 0;
                continue;
            }
            SDL_SetTextureBlendMode(s_icon_tex[b], SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(s_icon_tex[b], SDL_ScaleModeLinear);
        }
        SDL_UpdateTexture(s_icon_tex[b], NULL, buf, ICON_PX * 4);
    }
}

/* A skill button: the icon in the circle (or its letter), the letter of the gamepad button in its color */
static void skill_button(SDL_Renderer *r, int c, int on)
{
    static const SDL_Color face[4] = { { 110, 210, 100, 255 }, { 235, 90, 80, 255 }, { 90, 150, 240, 255 },
                                       { 240, 205, 70, 255 } };
    static const char *const letter[4] = { "A", "B", "X", "Y" };
    const Box *b = &s_box[c];
    int i = c - C_A;
    fill_box(r, b, 0.0f, on ? k_fill_on : k_fill);
    if (s_icon_ok[i] && s_icon_tex[i]) {
        float s = b->w * 0.70f;
        SDL_FRect d = { b->cx - s / 2, b->cy - s / 2, s, s };
        SDL_SetTextureAlphaMod(s_icon_tex[i], (Uint8)((on ? 255 : 215) * s_fade));
        SDL_RenderCopyF(r, s_icon_tex[i], NULL, &d);
        /* the gamepad letter, small, at the bottom right of the circle */
        Box tag = { b->cx + b->w * 0.33f, b->cy + b->w * 0.33f, b->w * 0.34f, b->w * 0.34f, 1 };
        fill_box(r, &tag, 0.0f, (SDL_Color){ 10, 8, 6, 220 });
        text(r, tag.cx, tag.cy, tag.w * 0.085f, letter[i], face[i]);
    } else {
        text(r, b->cx, b->cy, b->w * 0.07f, letter[i], face[i]);
    }
    ring_box(r, b, 2.0f * s_dp, on ? k_edge_on : k_edge);
}

/* A potion: the orb's color with a little flask */
static void potion_button(SDL_Renderer *r, int c, int on)
{
    const Box *b = &s_box[c];
    SDL_Color col = c == C_HEAL ? (SDL_Color){ 150, 20, 20, 200 } : (SDL_Color){ 25, 40, 160, 200 };
    if (on)
        col.a = 255;
    fill_box(r, b, 0.0f, k_fill);
    fill_box(r, b, 5 * s_dp, col);
    float u = b->w / 10.0f;
    SDL_SetRenderDrawColor(r, 235, 225, 200, 220);
    SDL_FRect neck = { b->cx - 0.6f * u, b->cy - 3.0f * u, 1.2f * u, 1.6f * u };
    SDL_RenderFillRectF(r, &neck);
    Box flask = { b->cx, b->cy + 0.6f * u, 4.2f * u, 4.2f * u, 1 };
    ring_box(r, &flask, 0.8f * u, (SDL_Color){ 235, 225, 200, 220 });
    ring_box(r, b, 2.0f * s_dp, on ? k_edge_on : k_edge);
}

void touch_draw(SDL_Renderer *r)
{
    if (!s_on || !r)
        return;
    int W, H;
    if (SDL_GetRendererOutputSize(r, &W, &H) != 0 || W <= 0 || H <= 0)
        return;
    int lw, lh;
    SDL_RenderGetLogicalSize(r, &lw, &lh);
    SDL_RenderSetLogicalSize(r, 0, 0); /* (draws over the whole window, the bars at the sides too) */
    layout(r, W, H);
    place_picture(W, H, lw, lh); /* (every frame: the menus are 800 wide, the game with widescreen wider) */
    if (!s_hidden) {
        refresh_icons(r);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        int down[C_COUNT] = { 0 }, dir = 0;
        const Finger *stick = NULL;
        for (int i = 0; i < MAX_FINGERS; i++)
            if (s_f[i].used) {
                down[s_f[i].ctl] = 1;
                if (s_f[i].ctl == C_DPAD)
                    dir = dpad_dir(s_f[i].x, s_f[i].y);
                if (s_f[i].ctl == C_STICK)
                    stick = &s_f[i];
            }
        /* with a panel of the game open (inventory, shop, ...) the round buttons are fainter: they still work, and what
           is under them shows (in widescreen the panels are close to them) */
        float fade = game_panel_open() ? 0.45f : 1.0f;
        for (int c = C_A; c <= C_MANA; c++) { /* (a pressed one, at full strength) */
            s_fade = down[c] ? 1.0f : fade;
            if (c <= C_Y)
                skill_button(r, c, down[c]);
            else
                potion_button(r, c, down[c]);
        }
        s_fade = 1.0f;
        for (int c = FIRST_MENU; c <= LAST_MENU; c++) {
            int on = down[c] || (c == C_RUN && game_always_run());
            const Box *b = &s_box[c];
            fill_box(r, b, 0.0f, on ? k_fill_on : k_fill);
            ring_box(r, b, 1.5f * s_dp, on ? k_edge_on : k_edge);
            const char *label = k_menu_label[c - FIRST_MENU];
            float p = fminf(b->h * 0.075f, b->w * 0.78f / (strlen(label) * 6 - 1));
            text(r, b->cx, b->cy, p, label, on && c == C_RUN ? (SDL_Color){ 255, 230, 120, 255 } : k_text);
        }

        /* the d-pad: four keys with their arrow */
        const Box *dp = &s_box[C_DPAD];
        float arm = dp->w / 3.0f;
        static const float ox[4] = { -1, 0, 1, 0 }, oy[4] = { 0, -1, 0, 1 };
        for (int k = 0; k < 4; k++) {
            Box key = { dp->cx + ox[k] * arm, dp->cy + oy[k] * arm, arm * 0.92f, arm * 0.92f, 0 };
            int on = dir == k + 1;
            fill_box(r, &key, 0.0f, on ? k_fill_on : k_fill);
            ring_box(r, &key, 1.5f * s_dp, on ? k_edge_on : k_edge);
            float a = arm * 0.2f, tx = key.cx, ty = key.cy;
            if (ox[k] != 0.0f)
                triangle(r, tx + ox[k] * a, ty, tx - ox[k] * a, ty - a, tx - ox[k] * a, ty + a, k_text);
            else
                triangle(r, tx, ty + oy[k] * a, tx - a, ty - oy[k] * a, tx + a, ty - oy[k] * a, k_text);
        }

        /* the joystick: where the finger put it, or (without a finger) where it waits, faint */
        Box base = { s_idle_x, s_idle_y, 2 * s_stick_r, 2 * s_stick_r, 1 };
        float kx = base.cx, ky = base.cy;
        if (stick) {
            base.cx = stick->bx;
            base.cy = stick->by;
            kx = stick->x;
            ky = stick->y;
        }
        fill_box(r, &base, 0.0f, (SDL_Color){ 0, 0, 0, stick ? 70 : 35 });
        ring_box(r, &base, 1.5f * s_dp, stick ? (SDL_Color){ 255, 255, 255, 150 } : (SDL_Color){ 255, 255, 255, 80 });
        Box knob = { kx, ky, s_stick_r * 0.85f, s_stick_r * 0.85f, 1 };
        fill_box(r, &knob, 0.0f, stick ? (SDL_Color){ 255, 255, 255, 90 } : (SDL_Color){ 255, 255, 255, 35 });
        ring_box(r, &knob, 1.5f * s_dp, stick ? (SDL_Color){ 255, 255, 255, 220 } : (SDL_Color){ 255, 255, 255, 110 });
    }
    /* (what the rest expects: no blending, black for clearing and the logical size from before) */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderSetLogicalSize(r, lw, lh);
}

#else /* old SDL: no virtual joysticks, no touch controls */
void touch_init(void) {}
int touch_enabled(void) { return 0; }
int touch_is_pad(SDL_JoystickID id) { (void)id; return 0; }
int touch_event(InputState *in, const SDL_Event *e) { (void)in; (void)e; return 0; }
void touch_update(InputState *in) { (void)in; }
void touch_pad_used(SDL_JoystickID id) { (void)id; }
void touch_draw(SDL_Renderer *r) { (void)r; }
void touch_shutdown(void) {}
#endif
