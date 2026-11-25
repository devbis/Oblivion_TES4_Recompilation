/* Input: XInput 1.3 shims backed by SDL game controllers, plus a virtual
 * pad fed by the keyboard (desktop) and the touch overlay (Android). */
#include "rt.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <strings.h>

static SDL_GameController *g_pads[4];
static struct {
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
} g_virt;
static uint32_t g_packet;
#ifdef GAME_OBLIVION
static int g_wheel;
#endif
static int g_nintendo;  /* swap A/B and X/Y on physical controllers */

void input_set_nintendo(int on) { g_nintendo = on; g_packet++; }

/* Skip Cutscenes: story cutscenes are levels of their own. While one is
 * playing or starting, pad 0 taps Start, which skips it, and A, which also
 * gets past the "press any key" screen of the level loading next. */
static int g_skip_cutscenes;
void input_set_skip_cutscenes(int on) { g_skip_cutscenes = on; }
/* The level being played: world (0x960894) +0x12C; level +0x64 flags, where
 * 0x20 intro, 0x40 cutscene/midtro and 0x80 outro mark cutscene levels. */
static int in_cutscene_level(void) {
#ifndef GAME_BATMAN
    return 0;  /* no cutscene skip for Marvel */
#endif
    uint32_t w = M32(0x960894), lv = w >= 0x10000 ? M32(w + 0x12C) : 0;
    if (lv < 0x10000) lv = M32(0xACA89C);  /* none yet: the one being started */
    return lv >= 0x10000 && (M8(lv + 0x64) & 0xE0);
}
static uint16_t skip_tap(void) {
    if (!g_skip_cutscenes || !in_cutscene_level()) return 0;
    uint32_t t = SDL_GetTicks() % 300;  /* Start at 0-60 ms, A at 150-210 ms */
    return t < 60 ? 0x10 : (t >= 150 && t < 210) ? 0x1000 : 0;
}

static const struct { SDL_Keycode key; uint16_t bit; } k_keymap[] = {
#ifdef GAME_OBLIVION
    {SDLK_UP, 0x1}, {SDLK_DOWN, 0x2}, {SDLK_LEFT, 0x4}, {SDLK_RIGHT, 0x8},
    {SDLK_RETURN, 0x1000}, {SDLK_ESCAPE, 0x10}, {SDLK_SPACE, 0x1000}, {SDLK_LCTRL, 0x40},
    {SDLK_TAB, 0x2000},
#else
    {SDLK_UP, 0x1}, {SDLK_DOWN, 0x2}, {SDLK_LEFT, 0x4}, {SDLK_RIGHT, 0x8},
    {SDLK_RETURN, 0x10}, {SDLK_ESCAPE, 0x20}, {SDLK_SPACE, 0x1000}, {SDLK_LCTRL, 0x2000},
    {SDLK_LSHIFT, 0x4000},
    {SDLK_TAB, 0x8000}, {SDLK_q, 0x100}, {SDLK_e, 0x200},
#endif
};

void input_set_virtual(uint16_t buttons, int16_t lx, int16_t ly, int16_t rx, int16_t ry, uint8_t lt, uint8_t rt) {
    g_virt.buttons = buttons;
    g_virt.lx = lx; g_virt.ly = ly; g_virt.rx = rx; g_virt.ry = ry;
    g_virt.lt = lt; g_virt.rt = rt;
    g_packet++;
}

void input_handle_event(const SDL_Event *e) {
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED:
        for (int i = 0; i < 4; i++)
            if (!g_pads[i]) { g_pads[i] = SDL_GameControllerOpen(e->cdevice.which); break; }
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        for (int i = 0; i < 4; i++)
            if (g_pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == e->cdevice.which) {
                SDL_GameControllerClose(g_pads[i]);
                g_pads[i] = NULL;
            }
        break;
#ifdef GAME_OBLIVION
    case SDL_MOUSEWHEEL: g_wheel += e->wheel.y * 120; break;
#endif
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        for (unsigned i = 0; i < sizeof k_keymap / sizeof k_keymap[0]; i++)
            if (k_keymap[i].key == e->key.keysym.sym) {
                if (e->type == SDL_KEYDOWN) g_virt.buttons |= k_keymap[i].bit;
                else g_virt.buttons &= (uint16_t)~k_keymap[i].bit;
                g_packet++;
            }
        {
            /* WASD = left stick; tracked from events so scripted keys work too */
            static int held[4];
            static const SDL_Keycode wasd[4] = {SDLK_w, SDLK_a, SDLK_s, SDLK_d};
            for (int i = 0; i < 4; i++)
                if (e->key.keysym.sym == wasd[i]) held[i] = e->type == SDL_KEYDOWN;
            g_virt.lx = (int16_t)((held[3] ? 32767 : 0) - (held[1] ? 32767 : 0));
            g_virt.ly = (int16_t)((held[0] ? 32767 : 0) - (held[2] ? 32767 : 0));
            g_packet++;
        }
        break;
    }
}

static void fill_state(int idx, uint32_t g) {
    uint16_t b = 0;
    uint8_t lt = 0, rt = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    SDL_GameController *p = g_pads[idx];
    if (p) {
        static const struct { SDL_GameControllerButton b; uint16_t bit; } map[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, 0x1}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x2},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x4}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x8},
            {SDL_CONTROLLER_BUTTON_START, 0x10}, {SDL_CONTROLLER_BUTTON_BACK, 0x20},
            {SDL_CONTROLLER_BUTTON_LEFTSTICK, 0x40}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0x80},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0x100}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x200},
            {SDL_CONTROLLER_BUTTON_A, 0x1000}, {SDL_CONTROLLER_BUTTON_B, 0x2000},
            {SDL_CONTROLLER_BUTTON_X, 0x4000}, {SDL_CONTROLLER_BUTTON_Y, 0x8000},
        };
        for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
            if (SDL_GameControllerGetButton(p, map[i].b)) b |= map[i].bit;
        if (g_nintendo)
            b = (uint16_t)((b & 0x0FFF) | ((b & 0x1000) << 1) | ((b & 0x2000) >> 1) |
                           ((b & 0x4000) << 1) | ((b & 0x8000) >> 1));
        lt = (uint8_t)(SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
        rt = (uint8_t)(SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
        lx = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
        ly = (int16_t)~SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
        rx = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = (int16_t)~SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTY);
    }
    if (idx == 0) {
        b |= g_virt.buttons | skip_tap();
        if (g_virt.lt) lt = g_virt.lt;
        if (g_virt.rt) rt = g_virt.rt;
        if (g_virt.lx || g_virt.ly) { lx = g_virt.lx; ly = g_virt.ly; }
        if (g_virt.rx || g_virt.ry) { rx = g_virt.rx; ry = g_virt.ry; }
    }
    { /* a new packet number whenever the reported state changes */
        static uint16_t last_b[4];
        if (b != last_b[idx & 3]) { last_b[idx & 3] = b; g_packet++; }
    }
    M32(g) = g_packet;
    M16(g + 4) = b;
    M8(g + 6) = lt;
    M8(g + 7) = rt;
    M16(g + 8) = (uint16_t)lx;
    M16(g + 10) = (uint16_t)ly;
    M16(g + 12) = (uint16_t)rx;
    M16(g + 14) = (uint16_t)ry;
}

static int connected(uint32_t i) { return i == 0 || (i < 4 && g_pads[i]); }

SHIM(XInputGetState) {
    uint32_t i = ARG(0);
    if (!connected(i)) RET(1167, 2);
    fill_state((int)i, ARG(1));
    RET(0, 2);
}
SHIM(XInputSetState) {
    uint32_t i = ARG(0);
    if (!connected(i)) RET(1167, 2);
    if (i < 4 && g_pads[i])
        SDL_GameControllerRumble(g_pads[i], M16(ARG(1)), M16(ARG(1) + 2), 200);
    RET(0, 2);
}
SHIM(XInputGetCapabilities) {
    uint32_t i = ARG(0);
    if (!connected(i)) RET(1167, 3);
    uint32_t g = ARG(2);
    memset(MEM + g, 0, 20);
    M8(g) = 1;       /* XINPUT_DEVTYPE_GAMEPAD */
    M8(g + 1) = 1;   /* XINPUT_DEVSUBTYPE_GAMEPAD */
    M16(g + 4) = 0xF3FF;
    M8(g + 6) = 0xFF; M8(g + 7) = 0xFF;
    M16(g + 8) = 0xFFC0; M16(g + 10) = 0xFFC0; M16(g + 12) = 0xFFC0; M16(g + 14) = 0xFFC0;
    M16(g + 16) = 0xFF; M16(g + 18) = 0xFF;
    RET(0, 3);
}

/* Pads the game is told about: pad 0 (touch overlay, keyboard, first SDL
 * controller) always, plus any further SDL controllers. */
int input_pad_count(void) {
    int n = 1;
    while (n < 4 && g_pads[n]) n++;
    return n;
}

/* Replaces the game's IsXInputDevice (0x51E280, cdecl, GUID *guidProduct),
 * which asks WMI for "IG_" device ids. Our pads use the 360 pad's product id. */
#ifdef GAME_BATMAN
void f_51E280(CPU *c) {
    RETC(M32(ARG(0)) == 0x028E045Eu);
}
#endif

static const ShimDef defs[] = {
    {"XINPUT1_3.dll", "#2", shim_XInputGetState},
    {"XINPUT1_3.dll", "#3", shim_XInputSetState},
    {"XINPUT1_3.dll", "#4", shim_XInputGetCapabilities},
    {"XINPUT1_3.dll", "XInputGetState", shim_XInputGetState},
    {"XINPUT1_3.dll", "XInputSetState", shim_XInputSetState},
    {"XINPUT1_3.dll", "XInputGetCapabilities", shim_XInputGetCapabilities},
    {NULL, NULL, NULL}
};

void input_init(void) { rt_register_shims(defs); }

#ifdef GAME_OBLIVION
/* ===================== Oblivion: pad -> keyboard + mouse =====================
 * Oblivion for PC reads only DirectInput keyboard and mouse. The phone's
 * controller is translated here and ORed into the real keyboard/mouse state
 * by dinput.c. Layout copied from the console (PS3) gameplay-controls sheet,
 * with the PC default key for each action (Xbox names; PS3 in brackets):
 *   left stick  = move (W A S D)           L3 = toggle sneak (Ctrl)
 *   right stick = look (mouse movement)    R3 = camera mode / toggle POV (R)
 *   A [Cross]    = use (Space)             B [Circle]   = journal (Tab)
 *   X [Square]   = ready weapon/torch (F)  Y [Triangle] = jump (E)
 *   LB [L1] = grab (Z)    LT [L2] = block (right mouse button)
 *   RB [R1] = cast (C)    RT [R2] = attack (left mouse button)
 *   D-pad = quick slots 1 2 3 4            Back [Select] = wait (T)
 *   Start = pause (Esc)
 * If the player rebinds a key in the game, edit the table or write
 * <save>/keys.cfg lines "BUTTON=DIKCODE" (decimal) with BUTTON one of
 * A B X Y LB RB L3 R3 UP DOWN LEFT RIGHT START BACK LT RT. */
static float g_look_x, g_look_y;
static int g_tap_frames;
/* touch: a short tap is a left click for a few polls */
void input_mouse_tap(void) { g_tap_frames = 3; }
void input_mouse_add(float dx, float dy) { g_look_x += dx; g_look_y += dy; }

typedef struct { const char *name; uint16_t bit; uint8_t dik; } KbmMap;
static KbmMap g_kbm[] = {
    {"A", 0x1000, 0x39}, {"B", 0x2000, 0x0F}, {"X", 0x4000, 0x21}, {"Y", 0x8000, 0x12},
    {"LB", 0x0100, 0x2C}, {"RB", 0x0200, 0x2E}, {"L3", 0x0040, 0x1D}, {"R3", 0x0080, 0x13},
    {"UP", 0x0001, 0x02}, {"DOWN", 0x0002, 0x03}, {"LEFT", 0x0004, 0x04}, {"RIGHT", 0x0008, 0x05},
    {"START", 0x0010, 0x01}, {"BACK", 0x0020, 0x14},
};
static uint8_t g_dik_lt = 0, g_dik_rt = 0;  /* triggers are mouse buttons unless remapped */

static void kbm_load_cfg(void) {
    static int done;
    if (done) return;
    done = 1;
    char path[1100];
    snprintf(path, sizeof path, "%s/keys.cfg", g_save_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char name[16]; int code;
    while (fscanf(f, " %15[A-Za-z0-9]=%d", name, &code) == 2) {
        for (unsigned i = 0; i < sizeof g_kbm / sizeof g_kbm[0]; i++)
            if (!strcasecmp(name, g_kbm[i].name)) g_kbm[i].dik = (uint8_t)code;
        if (!strcasecmp(name, "LT")) g_dik_lt = (uint8_t)code;
        if (!strcasecmp(name, "RT")) g_dik_rt = (uint8_t)code;
    }
    fclose(f);
}

/* keys[256]: DIK states to OR in (0x80 = down); returns mouse delta/wheel/buttons accumulated since the last call. */
void input_kbm_poll(uint8_t keys[256], int *mdx, int *mdy, int *wheel, uint8_t mb[3], int mouse) {
    kbm_load_cfg();
    *mdx = *mdy = *wheel = 0; mb[0] = mb[1] = mb[2] = 0;
    uint16_t b = g_virt.buttons;
    int lt = g_virt.lt, rt = g_virt.rt;
    int16_t lx = g_virt.lx, ly = g_virt.ly, rx = g_virt.rx, ry = g_virt.ry;
    SDL_GameController *p = g_pads[0];
    if (p) {
        static const struct { SDL_GameControllerButton b; uint16_t bit; } map[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, 0x1}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x2},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x4}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x8},
            {SDL_CONTROLLER_BUTTON_START, 0x10}, {SDL_CONTROLLER_BUTTON_BACK, 0x20},
            {SDL_CONTROLLER_BUTTON_LEFTSTICK, 0x40}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0x80},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0x100}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x200},
            {SDL_CONTROLLER_BUTTON_A, 0x1000}, {SDL_CONTROLLER_BUTTON_B, 0x2000},
            {SDL_CONTROLLER_BUTTON_X, 0x4000}, {SDL_CONTROLLER_BUTTON_Y, 0x8000},
        };
        for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
            if (SDL_GameControllerGetButton(p, map[i].b)) b |= map[i].bit;
        if (g_nintendo) b = (uint16_t)((b & 0x0FFF) | ((b & 0x1000) << 1) | ((b & 0x2000) >> 1) | ((b & 0x4000) << 1) | ((b & 0x8000) >> 1));
        int t = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7; if (t > lt) lt = t;
        t = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7; if (t > rt) rt = t;
        int v;
        if (abs(v = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX)) > abs(lx)) lx = (int16_t)v;
        if (abs(v = ~SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY)) > abs(ly)) ly = (int16_t)v;
        if (abs(v = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTX)) > abs(rx)) rx = (int16_t)v;
        if (abs(v = ~SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTY)) > abs(ry)) ry = (int16_t)v;
    }
    for (unsigned i = 0; i < sizeof g_kbm / sizeof g_kbm[0]; i++)
        if ((b & g_kbm[i].bit) && g_kbm[i].dik) keys[g_kbm[i].dik] = 0x80;
    const int T = 12000;  /* stick threshold for digital movement */
    if (ly > T) keys[0x11] = 0x80;
    if (ly < -T) keys[0x1F] = 0x80;
    if (lx < -T) keys[0x1E] = 0x80;
    if (lx > T) keys[0x20] = 0x80;
    if (rt > 100 && g_dik_rt) keys[g_dik_rt] = 0x80;
    if (lt > 100 && g_dik_lt) keys[g_dik_lt] = 0x80;
    if (!mouse) return;  /* keyboard call: nothing consumed */
    mb[0] = (rt > 100 && !g_dik_rt) ? 0x80 : 0;
    mb[1] = (lt > 100 && !g_dik_lt) ? 0x80 : 0;
    if (g_tap_frames > 0) { mb[0] = 0x80; g_tap_frames--; }
    /* right stick: pixels per second, with a dead zone, scaled by the time since the last poll */
    static Uint32 last;
    Uint32 now = SDL_GetTicks();
    float dt = last ? (now - last) / 1000.0f : 0.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;
    float fx = rx / 32767.0f, fy = -ry / 32767.0f;
    float m = sqrtf(fx * fx + fy * fy);
    float dx = 0, dy = 0;
    if (m > 0.15f) { float k = (m - 0.15f) / 0.85f; k = k * k * 900.0f * dt / m; dx = fx * k; dy = fy * k; }
    g_look_x += dx; g_look_y += dy;
    *mdx = (int)g_look_x; *mdy = (int)g_look_y;
    g_look_x -= *mdx; g_look_y -= *mdy;
    *wheel = g_wheel; g_wheel = 0;
}
#endif
