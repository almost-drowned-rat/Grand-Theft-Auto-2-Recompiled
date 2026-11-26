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

#include <stdio.h>

int wr_reg_get_dword(const char *key, const char *name, uint32_t *out);

enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_ATTACK, ACT_ENTER, ACT_JUMP, ACT_PREV, ACT_NEXT,
       ACT_SPECIAL, ACT_SPECIAL2, ACT_STATUS, ACT_N, ACT_ESCAPE = 100 };

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
    case WR_PAD_BACK: return ACT_STATUS;
    case WR_PAD_START: return ACT_ESCAPE;
    default: return -1;
    }
}

static uint8_t g_dik[ACT_N];
static uint32_t g_dik_time;

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
    default: return 0;
    }
}

static uint8_t map(int control)
{
    if (g_gta2_frontend_ms && wr_time_ms() - g_gta2_frontend_ms < 250)
        return map_menu(control);
    int a = pad_action(control);
    if (a < 0)
        return 0;
    if (a == ACT_ESCAPE)
        return 0x01;
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
