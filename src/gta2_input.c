/*
 * GTA 2 Recompiled - controller mapping.
 *
 * Pad controls map to GTA 2's twelve control actions; the key each action
 * produces is the one the game's own control settings assign to it
 * (in the menus: A confirm, B back, see map_menu)
 * (registry Control\0..11, DirectInput key codes), so a player who rebinds
 * the keyboard keeps a working pad. START is Escape (pause / menu back).
 * BUTTON MAPPING in the RECOMP menu (M6) will make this table editable.
 */
#include "wr.h"
#include "platform/wr_platform.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "gta2_menu.h"
#include "gta2_input.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

int wr_reg_get_dword(const char *key, const char *name, uint32_t *out);

extern uint32_t g_gta2_frontend_ms;   /* gta2_manual.c: the frontend loop last ran */

enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_ATTACK, ACT_ENTER, ACT_JUMP, ACT_PREV, ACT_NEXT,
       ACT_SPECIAL, ACT_SPECIAL2, ACT_STATUS, ACT_N, ACT_ESCAPE = 100, ACT_RADIO_NEXT, ACT_RADIO_PREV, ACT_SPRINT };

static const uint8_t k_default_dik[ACT_N] = { 0xC8, 0xD0, 0xCB, 0xCD, 0x1D, 0x1C, 0x39, 0x2C, 0x2D, 0x0F, 0x38, 0x36 };

static int pad_action(int c)
{
    switch (c) {
    case WR_PAD_DPAD_UP: case WR_PAD_LEFT_STICK_UP: return ACT_UP;
    case WR_PAD_DPAD_DOWN: case WR_PAD_LEFT_STICK_DOWN: return ACT_DOWN;
    case WR_PAD_DPAD_LEFT: case WR_PAD_LEFT_STICK_LEFT: return ACT_LEFT;
    case WR_PAD_DPAD_RIGHT: case WR_PAD_LEFT_STICK_RIGHT: return ACT_RIGHT;
    case WR_PAD_WEST: case WR_PAD_RT: return ACT_ATTACK;
    case WR_PAD_SOUTH: return ACT_ENTER;
    case WR_PAD_EAST: return ACT_JUMP;
    case WR_PAD_LB: return ACT_PREV;
    case WR_PAD_RB: return ACT_NEXT;
    case WR_PAD_NORTH: return ACT_SPECIAL;
    case WR_PAD_LT: return ACT_SPECIAL2;
    case WR_PAD_RSTICK: return ACT_STATUS;   /* special 3 on R3 (maintainer 2026-11-26) */
    /* the radio (it changes only in a car): right stick left / right (maintainer 2026-11-26) */
    case WR_PAD_RIGHT_STICK_LEFT: return ACT_RADIO_PREV;
    case WR_PAD_RIGHT_STICK_RIGHT: return ACT_RADIO_NEXT;
    case WR_PAD_START: return ACT_ESCAPE;
    default: return -1;
    }
}


/* In the menus (the frontend, and the port's own pages): A confirms, B goes
 * back, d-pad / left stick move, START confirms. The menus read these keys
 * directly (arrows, Return, Escape), not the control settings. */
static uint8_t map_menu(int control)
{
    switch (control) {
    case WR_PAD_DPAD_UP: case WR_PAD_LEFT_STICK_UP: return 0xC8;
    case WR_PAD_DPAD_DOWN: case WR_PAD_LEFT_STICK_DOWN: return 0xD0;
    case WR_PAD_DPAD_LEFT: case WR_PAD_LEFT_STICK_LEFT: return 0xCB;
    case WR_PAD_DPAD_RIGHT: case WR_PAD_LEFT_STICK_RIGHT: return 0xCD;
    case WR_PAD_SOUTH: case WR_PAD_START: return 0x1C;   /* Return */
    case WR_PAD_EAST: return 0x01;                       /* Escape */
    case WR_PAD_NORTH: return 0xC7;                      /* Home: defaults on the port's pages */
    default: return 0;
    }
}

/* ---- MODERN (maintainer 2026-11-26) -----------------------------------------------------
 *            on foot                       in a car
 * stick and  walk where it points          steer (left / right)
 * d-pad      (8 directions on the d-pad)
 * R-stick    turn / aim                    left / right: radio previous / next
 * A          sprint (1.5 s, every 10 s)      gas
 * X          attack                        brake / reverse
 * B          jump                          handbrake
 * L3         special 3                     special 3
 * R3         special 1                     special 1
 * R2         attack                        attack
 * L2         special 2                     special 1
 * Y          enter                         exit
 * LB / RB    previous / next weapon
 * Select special 2, START the menu
 * The stick's direction on foot is set in the on-foot control step
 * (gta2_modern_on_foot, wrapper of sub_004A5C50); this table only presses
 * "forward" while the stick is pushed. */
static int modern_action(int c, int driving)
{
    switch (c) {
    /* stick and d-pad alike (maintainer 2026-11-26) */
    case WR_PAD_LEFT_STICK_UP: case WR_PAD_LEFT_STICK_DOWN:
        return driving ? -1 : ACT_UP;
    case WR_PAD_DPAD_UP: case WR_PAD_DPAD_DOWN: return driving ? -1 : ACT_UP;
    /* right stick: aiming on foot (analogue, gta2_state.c), the radio in a car */
    case WR_PAD_RIGHT_STICK_LEFT: return driving ? ACT_RADIO_PREV : -1;
    case WR_PAD_RIGHT_STICK_RIGHT: return driving ? ACT_RADIO_NEXT : -1;
    case WR_PAD_RIGHT_STICK_UP: case WR_PAD_RIGHT_STICK_DOWN: return -1;
    case WR_PAD_LEFT_STICK_LEFT: case WR_PAD_DPAD_LEFT: return driving ? ACT_LEFT : ACT_UP;
    case WR_PAD_LEFT_STICK_RIGHT: case WR_PAD_DPAD_RIGHT: return driving ? ACT_RIGHT : ACT_UP;
    /* car buttons (maintainer 2026-11-26): A gas, X brake, B handbrake, R2 shoot */
    case WR_PAD_RT: return ACT_ATTACK;
    case WR_PAD_SOUTH: return driving ? ACT_UP : opt_get(OPT_SPRINT) ? -1 : ACT_JUMP;   /* on foot: SPRINT (or jump without it) */
    case WR_PAD_WEST: return driving ? ACT_DOWN : ACT_ATTACK;
    case WR_PAD_EAST: return ACT_JUMP;                   /* jump on foot, handbrake in a car */
    case WR_PAD_RSTICK: return ACT_SPECIAL;              /* special 1 on R3 */
    case WR_PAD_LSTICK: return ACT_STATUS;               /* special 3 on L3 */
    case WR_PAD_BACK: return ACT_SPECIAL2;               /* special 2 on Select */
    case WR_PAD_LT: return driving ? ACT_SPECIAL : ACT_SPECIAL2;
    case WR_PAD_NORTH: return ACT_ENTER;
    default: return pad_action(c);   /* LB/RB, Back, START as CLASSIC */
    }
}

static uint8_t g_dik[ACT_N];
static uint32_t g_dik_time;

/* ---- CUSTOM (maintainer 2026-11-26): every function on a button of the
 * player's choice, one button per function; the left stick always acts
 * as the d-pad. Kept in settings.ini as pad_bind_<n> = control number. The
 * keyboard's bindings are the game's own (registry Control\0..11). */
static const struct { const char *label; int act; } k_fn[GTA2_BIND_N] = {
    { "FORWARD", ACT_UP }, { "BACKWARD", ACT_DOWN }, { "LEFT", ACT_LEFT }, { "RIGHT", ACT_RIGHT },
    { "ATTACK", ACT_ATTACK }, { "ENTER / EXIT", ACT_ENTER }, { "JUMP / HANDBRAKE", ACT_JUMP },
    { "PREVIOUS WEAPON", ACT_PREV }, { "NEXT WEAPON", ACT_NEXT }, { "SPECIAL 1", ACT_SPECIAL },
    { "SPECIAL 2", ACT_SPECIAL2 }, { "SPECIAL 3", ACT_STATUS }, { "SPRINT", ACT_SPRINT },
    { "RADIO NEXT", ACT_RADIO_NEXT }, { "RADIO PREVIOUS", ACT_RADIO_PREV }, { "MENU", ACT_ESCAPE },
};
static const int k_bind_default[GTA2_BIND_N] = {
    WR_PAD_DPAD_UP, WR_PAD_DPAD_DOWN, WR_PAD_DPAD_LEFT, WR_PAD_DPAD_RIGHT, WR_PAD_WEST, WR_PAD_SOUTH,
    WR_PAD_EAST, WR_PAD_LB, WR_PAD_RB, WR_PAD_NORTH, WR_PAD_LT, WR_PAD_RSTICK, WR_PAD_LSTICK,
    WR_PAD_RIGHT_STICK_RIGHT, WR_PAD_RIGHT_STICK_LEFT, WR_PAD_START,
};
static int g_bind[GTA2_BIND_N];
static int g_bind_loaded;

static void bind_load(void)
{
    if (g_bind_loaded)
        return;
    g_bind_loaded = 1;
    for (int i = 0; i < GTA2_BIND_N; i++) {
        char key[24];
        snprintf(key, sizeof key, "pad_bind_%d", i);
        int c = wr_settings_get_int(key, k_bind_default[i]);
        g_bind[i] = c >= 0 && c < WR_PAD_N ? c : k_bind_default[i];
    }
}

#define BIND_SPRINT 12      /* the same index on both devices */
#define KEY_SPRINT_DEFAULT 0x2A   /* Left Shift: unused by the game */

int gta2_bind_count(int keyboard) { return keyboard ? ACT_N + 1 : GTA2_BIND_N; }
const char *gta2_bind_label(int i) { return i >= 0 && i < GTA2_BIND_N ? k_fn[i].label : ""; }

static uint8_t key_of(int act)
{
    if (act == BIND_SPRINT) {   /* the port's own key: settings.ini */
        int v = wr_settings_get_int("key_sprint", KEY_SPRINT_DEFAULT);
        return v > 0 && v < 256 ? (uint8_t)v : KEY_SPRINT_DEFAULT;
    }
    char name[8];
    uint32_t v;
    snprintf(name, sizeof name, "%d", act);
    return wr_reg_get_dword("HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control", name, &v) && v < 256
               ? (uint8_t)v : k_default_dik[act];
}

void gta2_bind_value(int keyboard, int i, char *out, size_t cap)
{
    bind_load();
    if (keyboard)
        wr_key_name(key_of(i == BIND_SPRINT ? BIND_SPRINT : k_fn[i].act), out, cap);
    else
        snprintf(out, cap, "%s", wr_pad_control_name(g_bind[i]));
}

/* a pad control for function i; the function that had it gets i's old one */
void gta2_bind_set_pad(int i, int control)
{
    bind_load();
    char key[24];
    for (int j = 0; j < GTA2_BIND_N; j++)
        if (j != i && g_bind[j] == control) {
            g_bind[j] = g_bind[i];
            snprintf(key, sizeof key, "pad_bind_%d", j);
            wr_settings_set_int(key, g_bind[j]);
        }
    g_bind[i] = control;
    snprintf(key, sizeof key, "pad_bind_%d", i);
    wr_settings_set_int(key, control);
}

/* a key for game action i (Control\i); swapped like the pad. The caller
 * makes the game read its controls again (sub_0044BF30). */
void gta2_bind_set_key(int i, uint8_t dik)
{
    const char *k = "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control";
    char name[8];
    uint8_t old = key_of(i);
    for (int j = 0; j <= ACT_N; j++)   /* ACT_N == BIND_SPRINT: the sprint key */
        if (j != i && key_of(j) == dik) {
            if (j == BIND_SPRINT) {
                wr_settings_set_int("key_sprint", old);
            } else {
                snprintf(name, sizeof name, "%d", j);
                wr_reg_set_dword(k, name, old);
            }
        }
    if (i == BIND_SPRINT) {
        wr_settings_set_int("key_sprint", dik);
    } else {
        snprintf(name, sizeof name, "%d", i);
        wr_reg_set_dword(k, name, dik);
    }
    g_dik_time = 0;   /* the pad follows at once */
}

void gta2_bind_defaults(int keyboard)
{
    if (keyboard) {
        const char *k = "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control";
        char name[8];
        for (int j = 0; j < ACT_N; j++) {
            snprintf(name, sizeof name, "%d", j);
            wr_reg_set_dword(k, name, k_default_dik[j]);
        }
        wr_settings_set_int("key_sprint", KEY_SPRINT_DEFAULT);
        g_dik_time = 0;
        return;
    }
    bind_load();
    for (int i = 0; i < GTA2_BIND_N; i++) {
        char key[24];
        snprintf(key, sizeof key, "pad_bind_%d", i);
        g_bind[i] = k_bind_default[i];
        wr_settings_set_int(key, g_bind[i]);
    }
}

static int custom_action(int c)
{
    bind_load();
    switch (c) {   /* the stick as the d-pad */
    case WR_PAD_LEFT_STICK_UP: c = WR_PAD_DPAD_UP; break;
    case WR_PAD_LEFT_STICK_DOWN: c = WR_PAD_DPAD_DOWN; break;
    case WR_PAD_LEFT_STICK_LEFT: c = WR_PAD_DPAD_LEFT; break;
    case WR_PAD_LEFT_STICK_RIGHT: c = WR_PAD_DPAD_RIGHT; break;
    default: break;
    }
    for (int i = 0; i < GTA2_BIND_N; i++)
        if (g_bind[i] == c)
            return k_fn[i].act;
    return -1;
}

static uint8_t map(int control)
{
    if (gta2_pause_open())   /* the pause menu: START closes it again (it opened it) */
        return control == WR_PAD_START ? 0x01 : map_menu(control);
    if (g_gta2_frontend_ms && wr_time_ms() - g_gta2_frontend_ms < 250)
        return map_menu(control);
    int driving = gta2_player_driving();
    int scheme = opt_get(OPT_CONTROL_SCHEME);
    int a = scheme == 1 ? modern_action(control, driving) : scheme == 2 ? custom_action(control) : pad_action(control);
    if (a < 0)
        return 0;
    if (a == ACT_ESCAPE)
        return 0x01;
    if (a == ACT_SPRINT)
        return 0;   /* read by the sprint itself (gta2_sprint_pressed) */
    if (a == ACT_RADIO_NEXT || a == ACT_RADIO_PREV)
        return a == ACT_RADIO_NEXT ? 0x3C : 0x3B;   /* F2 / F1 */
    uint32_t now = wr_time_ms();
    if (!g_dik_time || now - g_dik_time > 1000) {   /* follow the game's settings */
        g_dik_time = now;
        for (int i = 0; i < ACT_N; i++) {
            char name[8];
            uint32_t v;
            snprintf(name, sizeof name, "%d", i);
            g_dik[i] = wr_reg_get_dword("HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Control", name, &v) && v < 256
                           ? (uint8_t)v : k_default_dik[i];
        }
    }
    return g_dik[a];
}

void gta2_input_register(void) { wr_pad_set_map(map); }

/* SPRINT held (GAMEPLAY SPRINT on): MODERN A, CLASSIC L3, CUSTOM its binding;
 * on the keyboard its key (Left Shift by default). */
int gta2_sprint_pressed(void)
{
    if (!opt_get(OPT_SPRINT))
        return 0;
    uint8_t kb[256];
    wr_keys_keyboard_dik(kb);
    if (kb[key_of(BIND_SPRINT)] & 0x80)
        return 1;
    uint64_t c = wr_pad_controls();
    int scheme = opt_get(OPT_CONTROL_SCHEME);
    bind_load();
    int ctl = scheme == 1 ? WR_PAD_SOUTH : scheme == 2 ? g_bind[BIND_SPRINT] : WR_PAD_LSTICK;
    return (int)(c >> ctl & 1);
}

/* ---- KEYBOARD schemes (maintainer 2026-11-26) -----------------------------------------------
 *   CLASSIC  the game's own keys (Control\0..11, CUSTOM MAPPING keyboard)
 *   PAD      the keyboard as a controller (option-c's layout, runtime wr_pad):
 *            WASD stick, arrows d-pad, Enter START, Backspace BACK, I/J/K/L = Y/X/A/B,
 *            U/O = LB/RB, Z/P = LT/RT - the CONTROLLER scheme then applies
 *   MODERN   mouse and keyboard:
 *            on foot  WASD walk that way, the ped faces the mouse pointer
 *            in a car W gas, S brake, A / D steer, arrows left / right radio
 *            left button attack, right button or Space jump / handbrake, E enter / exit,
 *            Q / R or the wheel previous / next weapon, F / G / C special 1 / 2 / 3,
 *            Shift sprint, Esc the menu */

static int kbm_active(void)
{
    return opt_get(OPT_KEY_SCHEME) == 1 && !gta2_pause_open() &&
           !(g_gta2_frontend_ms && wr_time_ms() - g_gta2_frontend_ms < 250);
}

static int key_down(const bool *k, int n, int sc) { return sc < n && k[sc]; }

static void kbm_hook(uint8_t st[256])
{
    if (!kbm_active())
        return;
    int n = 0;
    const bool *k = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(&n) : NULL;
    if (!k)
        return;
    float mx, my;
    SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
    int driving = gta2_player_driving();
    /* the raw letters of this layout are not the game's keys */
    static const uint8_t taken[] = { 0x11, 0x1E, 0x1F, 0x20, 0x12, 0x10, 0x13, 0x21, 0x22, 0x2E, 0x39 };
    for (size_t i = 0; i < sizeof taken; i++)
        st[taken[i]] = 0;
    if (driving) {
        st[0xCB] = st[0xCD] = 0;   /* arrows left / right: the radio */
        if (key_down(k, n, SDL_SCANCODE_W)) st[key_of(ACT_UP)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_S)) st[key_of(ACT_DOWN)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_A)) st[key_of(ACT_LEFT)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_D)) st[key_of(ACT_RIGHT)] = 0x80;
        if (key_down(k, n, SDL_SCANCODE_LEFT)) st[0x3B] = 0x80;    /* F1 */
        if (key_down(k, n, SDL_SCANCODE_RIGHT)) st[0x3C] = 0x80;   /* F2 */
    }
    if (mb & SDL_BUTTON_LMASK) st[key_of(ACT_ATTACK)] = 0x80;
    if ((mb & SDL_BUTTON_RMASK) || key_down(k, n, SDL_SCANCODE_SPACE)) st[key_of(ACT_JUMP)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_E)) st[key_of(ACT_ENTER)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_Q)) st[key_of(ACT_PREV)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_R)) st[key_of(ACT_NEXT)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_F)) st[key_of(ACT_SPECIAL)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_G)) st[key_of(ACT_SPECIAL2)] = 0x80;
    if (key_down(k, n, SDL_SCANCODE_C)) st[key_of(ACT_STATUS)] = 0x80;
    /* the wheel: one weapon step per notch, held for a few polls so the game sees it */
    static int pulse_prev, pulse_next;
    if (g_wr_mouse_wheel > 0) { pulse_next = 4; g_wr_mouse_wheel = 0; }
    if (g_wr_mouse_wheel < 0) { pulse_prev = 4; g_wr_mouse_wheel = 0; }
    if (pulse_next > 0) { if (pulse_next-- > 1) st[key_of(ACT_NEXT)] = 0x80; }
    if (pulse_prev > 0) { if (pulse_prev-- > 1) st[key_of(ACT_PREV)] = 0x80; }
}

void gta2_keys_apply(void)
{
    int sc = opt_get(OPT_KEY_SCHEME);
    wr_pad_keyboard_as_pad(sc == 2);
    g_wr_dik_hook = sc == 1 ? kbm_hook : NULL;
}

/* MODERN keyboard on foot: WASD as the left stick, the mouse pointer (from
 * the game picture's centre) as the right one; 0 = not this scheme */
int gta2_kbm_sticks(float *lx, float *ly, float *rx, float *ry)
{
    *lx = *ly = *rx = *ry = 0;
    if (!kbm_active())
        return 0;
    int n = 0;
    const bool *k = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(&n) : NULL;
    if (k) {
        *lx = (float)(key_down(k, n, SDL_SCANCODE_D) - key_down(k, n, SDL_SCANCODE_A));
        *ly = (float)(key_down(k, n, SDL_SCANCODE_S) - key_down(k, n, SDL_SCANCODE_W));
    }
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    float cx, cy;
    if (gta2_game_centre(&cx, &cy)) {
        float dx = mx - cx, dy = my - cy, d = sqrtf(dx * dx + dy * dy);
        if (d > 12.0f) {   /* a pointer on the ped: keep the heading */
            *rx = dx / d;
            *ry = dy / d;
        }
    }
    return 1;
}

/* ---- CONTROL OVERVIEW: every function with its controller button and its key for
 * the schemes in use, on foot and in a car (maintainer 2026-11-26). Controller
 * tokens are wr_pad_control_name's; the PAD keyboard shows the key behind them. */
typedef struct ov_row {
    const char *label;
    const char *classic, *modern;   /* controller, " / " between alternatives */
    int act;                        /* CLASSIC keyboard + CUSTOM controller: ACT_* / bind index, -1 none */
    const char *kbm;                /* MODERN keyboard */
} ov_row;

static const ov_row k_ov[] = {
    { "ON FOOT", NULL, NULL, -1, NULL },
    { "WALK", "D-PAD / STICK", "STICK / D-PAD (ANY WAY)", -2, "W A S D (ANY WAY)" },
    { "TURN / AIM", "D-PAD / STICK", "R-STICK", -3, "MOUSE" },
    { "ATTACK", "X / RT", "X / RT", ACT_ATTACK, "LEFT MOUSE" },
    { "JUMP", "B", "B", ACT_JUMP, "RIGHT MOUSE / SPACE" },
    { "SPRINT", "L3", "A", BIND_SPRINT, "LEFT SHIFT" },
    { "ENTER VEHICLE", "A", "Y", ACT_ENTER, "E" },
    { "PREVIOUS WEAPON", "LB", "LB", ACT_PREV, "Q / WHEEL DOWN" },
    { "NEXT WEAPON", "RB", "RB", ACT_NEXT, "R / WHEEL UP" },
    { "SPECIAL 1", "Y", "R3", ACT_SPECIAL, "F" },
    { "SPECIAL 2", "LT", "SELECT / LT", ACT_SPECIAL2, "G" },
    { "SPECIAL 3", "R3", "L3", ACT_STATUS, "C" },
    { "MENU", "START", "START", -4, "ESC" },
    { "IN A CAR", NULL, NULL, -1, NULL },
    { "GAS", "D-PAD UP / STICK UP", "A", ACT_UP, "W" },
    { "BRAKE / REVERSE", "D-PAD DOWN / STICK DOWN", "X", ACT_DOWN, "S" },
    { "STEER", "D-PAD / STICK LEFT RIGHT", "STICK / D-PAD LEFT RIGHT", -5, "A / D" },
    { "HANDBRAKE", "B", "B", ACT_JUMP, "RIGHT MOUSE / SPACE" },
    { "ATTACK", "X / RT", "RT", ACT_ATTACK, "LEFT MOUSE" },
    { "EXIT VEHICLE", "A", "Y", ACT_ENTER, "E" },
    { "RADIO", "R-STICK LEFT RIGHT", "R-STICK LEFT RIGHT", -6, "ARROW LEFT / RIGHT" },
    { "SPECIAL 1", "Y", "R3 / LT", ACT_SPECIAL, "F" },
};

int gta2_overview_count(void) { return (int)(sizeof k_ov / sizeof k_ov[0]); }
const char *gta2_overview_label(int i) { return i >= 0 && i < gta2_overview_count() ? k_ov[i].label : ""; }
int gta2_overview_is_head(int i) { return i >= 0 && i < gta2_overview_count() && !k_ov[i].classic; }

/* the keyboard pad's key for a controller token */
static const char *pad_key(const char *t)
{
    static const char *const m[][2] = {
        { "A", "K" }, { "B", "L" }, { "X", "J" }, { "Y", "I" }, { "LB", "U" }, { "RB", "O" }, { "LT", "Z" },
        { "RT", "P" }, { "START", "ENTER" }, { "SELECT", "BACKSPACE" }, { "D-PAD", "ARROWS" },
        { "STICK", "W A S D" }, { "D-PAD UP", "ARROW UP" }, { "D-PAD DOWN", "ARROW DOWN" },
        { "STICK UP", "W" }, { "STICK DOWN", "S" }, { "D-PAD LEFT RIGHT", "ARROWS LEFT RIGHT" },
        { "STICK LEFT RIGHT", "A D" },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++)
        if (!strcmp(t, m[i][0]))
            return m[i][1];
    return NULL;   /* sticks clicked, the right stick: no key */
}

/* "X / RT" -> "J / P"; tokens without a key are left out */
static void to_pad_keys(const char *s, char *out, size_t cap)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%s", s);
    out[0] = 0;
    for (char *t = strtok(buf, "/"); t; t = strtok(NULL, "/")) {
        while (*t == ' ') t++;
        size_t n = strlen(t);
        while (n && t[n - 1] == ' ') t[--n] = 0;
        char *paren = strstr(t, " (");
        if (paren) *paren = 0;
        const char *k = pad_key(t);
        if (k)
            snprintf(out + strlen(out), cap - strlen(out), "%s%s", out[0] ? " / " : "", k);
    }
    if (!out[0])
        snprintf(out, cap, "-");
}

static int bind_of_act(int act)
{
    for (int i = 0; i < GTA2_BIND_N; i++)
        if (k_fn[i].act == act || (act == BIND_SPRINT && i == BIND_SPRINT))
            return i;
    return -1;
}

void gta2_overview_value(int i, char *out, size_t cap)
{
    out[0] = 0;
    if (gta2_overview_is_head(i) || i < 0 || i >= gta2_overview_count())
        return;
    const ov_row *r = &k_ov[i];
    int ps = opt_get(OPT_CONTROL_SCHEME), ks = opt_get(OPT_KEY_SCHEME);
    char pad[96], key[96];
    if (ps == 2 && r->act >= 0 && bind_of_act(r->act) >= 0)
        gta2_bind_value(0, bind_of_act(r->act), pad, sizeof pad);
    else if (ps == 2 && r->act == -6)
        snprintf(pad, sizeof pad, "%s %s", wr_pad_control_name(g_bind[14]), wr_pad_control_name(g_bind[13]));
    else
        snprintf(pad, sizeof pad, "%s", ps == 1 ? r->modern : r->classic);
    if (ks == 1)
        snprintf(key, sizeof key, "%s", r->kbm);
    else if (ks == 2)
        to_pad_keys(pad, key, sizeof key);
    else if (r->act >= 0)
        wr_key_name(key_of(r->act), key, sizeof key);
    else {
        static const char *const fixed[] = { "", "", "ARROWS", "ARROWS", "ESC", "ARROW LEFT / RIGHT", "F1 / F2" };
        snprintf(key, sizeof key, "%s", fixed[-r->act]);
    }
    snprintf(out, cap, "%s   |   %s", pad, key);
}

/* ---- CONTROLLER LAYOUT labels (gta2_padview.c): "LT  SPECIAL 2" for the scheme in use ---- */
#include "gta2_padview.h"

static const char *act_name(int a, int driving)
{
    switch (a) {
    case ACT_UP: return driving ? "GAS" : "FORWARD";
    case ACT_DOWN: return driving ? "BRAKE" : "BACK";
    case ACT_LEFT: case ACT_RIGHT: return driving ? "STEER" : "TURN";
    case ACT_ATTACK: return "ATTACK";
    case ACT_ENTER: return driving ? "EXIT CAR" : "ENTER CAR";
    case ACT_JUMP: return driving ? "HANDBRAKE" : "JUMP";
    case ACT_PREV: return "PREV WEAPON";
    case ACT_NEXT: return "NEXT WEAPON";
    case ACT_SPECIAL: return "SPECIAL 1";
    case ACT_SPECIAL2: return "SPECIAL 2";
    case ACT_STATUS: return "SPECIAL 3";
    case ACT_ESCAPE: return "MENU";
    case ACT_RADIO_NEXT: case ACT_RADIO_PREV: return driving ? "RADIO" : NULL;
    case ACT_SPRINT: return driving ? NULL : "SPRINT";
    default: return NULL;
    }
}

static int control_act(int c, int driving)
{
    int sc = opt_get(OPT_CONTROL_SCHEME);
    if (opt_get(OPT_SPRINT) && !driving) {   /* SPRINT's button (gta2_sprint_pressed) */
        bind_load();
        int sp = sc == 1 ? WR_PAD_SOUTH : sc == 2 ? g_bind[BIND_SPRINT] : WR_PAD_LSTICK;
        if (c == sp)
            return ACT_SPRINT;
    }
    if (sc == 1)
        return modern_action(c, driving);
    if (sc == 2) {
        bind_load();
        for (int i = 0; i < GTA2_BIND_N; i++)
            if (g_bind[i] == c && i != BIND_SPRINT)
                return k_fn[i].act;
        return -1;
    }
    return pad_action(c);
}

void gta2_pad_function(int id, int driving, char *out, size_t cap)
{
    static const struct { int ctl; const char *tok; } k[PV_N] = {
        [PV_LT] = { WR_PAD_LT, "LT" }, [PV_LB] = { WR_PAD_LB, "LB" }, [PV_LSTICK] = { -1, "STICK" },
        [PV_L3] = { WR_PAD_LSTICK, "L3" }, [PV_DPAD] = { -2, "D-PAD" }, [PV_SELECT] = { WR_PAD_BACK, "SELECT" },
        [PV_RT] = { WR_PAD_RT, "RT" }, [PV_RB] = { WR_PAD_RB, "RB" }, [PV_Y] = { WR_PAD_NORTH, "Y" },
        [PV_B] = { WR_PAD_EAST, "B" }, [PV_A] = { WR_PAD_SOUTH, "A" }, [PV_X] = { WR_PAD_WEST, "X" },
        [PV_RSTICK] = { -3, "R-STICK" }, [PV_R3] = { WR_PAD_RSTICK, "R3" }, [PV_START] = { WR_PAD_START, "START" },
    };
    if (id < 0 || id >= PV_N) {
        out[0] = 0;
        return;
    }
    int sc = opt_get(OPT_CONTROL_SCHEME), c = k[id].ctl;
    const char *f = NULL;
    if (c == -1 || c == -2) {   /* the left stick, the d-pad */
        if (sc == 1)
            f = driving ? "STEER" : "WALK ANY WAY";
        else if (driving)
            f = "GAS BRAKE STEER";
        else
            f = "WALK AND TURN";
    } else if (c == -3) {   /* the right stick */
        if (sc == 1)
            f = driving ? "RADIO LEFT RIGHT" : "AIM AND TURN";
        else
            f = driving ? "RADIO LEFT RIGHT" : NULL;
    } else
        f = act_name(control_act(c, driving), driving);
    snprintf(out, cap, "%s  %s", k[id].tok, f ? f : "-");
}
