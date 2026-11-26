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

#include <stdio.h>

int wr_reg_get_dword(const char *key, const char *name, uint32_t *out);

enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_ATTACK, ACT_ENTER, ACT_JUMP, ACT_PREV, ACT_NEXT,
       ACT_SPECIAL, ACT_SPECIAL2, ACT_STATUS, ACT_N, ACT_ESCAPE = 100, ACT_RADIO_NEXT, ACT_RADIO_PREV };

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


extern uint32_t g_gta2_frontend_ms;

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
 * L3         special 1                     special 1
 * R2         attack                        attack
 * L2         special 2                     special 1
 * Y          enter                         exit
 * LB / RB    previous / next weapon
 * R3 special 3, START the menu, Select unused
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
    case WR_PAD_SOUTH: return driving ? ACT_UP : -1;     /* on foot: SPRINT (gta2_state.c) */
    case WR_PAD_WEST: return driving ? ACT_DOWN : ACT_ATTACK;
    case WR_PAD_EAST: return ACT_JUMP;                   /* jump on foot, handbrake in a car */
    case WR_PAD_LSTICK: return ACT_SPECIAL;              /* special 1 on L3 */
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
    { "SPECIAL 2", ACT_SPECIAL2 }, { "SPECIAL 3", ACT_STATUS }, { "RADIO NEXT", ACT_RADIO_NEXT },
    { "RADIO PREVIOUS", ACT_RADIO_PREV }, { "MENU", ACT_ESCAPE },
};
static const int k_bind_default[GTA2_BIND_N] = {
    WR_PAD_DPAD_UP, WR_PAD_DPAD_DOWN, WR_PAD_DPAD_LEFT, WR_PAD_DPAD_RIGHT, WR_PAD_WEST, WR_PAD_SOUTH,
    WR_PAD_EAST, WR_PAD_LB, WR_PAD_RB, WR_PAD_NORTH, WR_PAD_LT, WR_PAD_RSTICK, WR_PAD_RIGHT_STICK_RIGHT,
    WR_PAD_RIGHT_STICK_LEFT, WR_PAD_START,
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

int gta2_bind_count(int keyboard) { return keyboard ? ACT_N : GTA2_BIND_N; }
const char *gta2_bind_label(int i) { return i >= 0 && i < GTA2_BIND_N ? k_fn[i].label : ""; }

static uint8_t key_of(int act)
{
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
        wr_key_name(key_of(k_fn[i].act), out, cap);
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
    for (int j = 0; j < ACT_N; j++)
        if (j != i && key_of(j) == dik) {
            snprintf(name, sizeof name, "%d", j);
            wr_reg_set_dword(k, name, old);
        }
    snprintf(name, sizeof name, "%d", i);
    wr_reg_set_dword(k, name, dik);
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
