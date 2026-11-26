/*
 * GTA 2 Recompiled - GAMEPLAY options (docs/re/enhancements.md §2).
 *
 * KEEP BODIES: the dead stay while a player is near (they are removed by the
 * game once they have been off screen for 40..200 ticks). The ped tick
 * (sub_004454E0) gets the off-screen counter ped+0x20E reset first, so none
 * of the three removal tests fires, and the kept body does not count against
 * the ped limits ([0x5E5BEA] all, [0x5E5BF4] on screen; recounted every
 * tick). At most MAX_BODIES are kept, the oldest go first; far away the game
 * removes them as before.
 *
 * MORE BLOOD: every blood pool comes with two more, slightly offset (a
 * bigger stain), and pools do not dry up at all (the particle tick
 * sub_0048BE60 runs their life down by one per tick; the wrapper gives it
 * back). At most MAX_BLOOD are kept so the shared 500-particle pool always
 * has room for smoke and explosions.
 */
#include "gta2_gameplay.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "wr.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BODIES 40
#define MAX_BLOOD  96
#define NEAR_BLOCKS 24   /* "near": within 24 blocks of the player (about four screens) */

static uint32_t g_game_seen;

static int near_player(int32_t x, int32_t y)
{
    uint32_t pp = gta2_player_ped();
    if (!pp || !wr_mem_valid(pp, 0x1B4))
        return 0;
    int32_t dx = (int32_t)wr_rd32(pp + 0x1AC) - x, dy = (int32_t)wr_rd32(pp + 0x1B0) - y;
    int64_t r = (int64_t)NEAR_BLOCKS << 14;
    return (int64_t)dx * dx + (int64_t)dy * dy < r * r;
}

static void new_game_check(void);
static int g_nbig, g_big_next;
static int g_nblood;   /* MORE BLOOD: pools kept (below) */

/* ---- KEEP BODIES ---------------------------------------------------------------------------- */

static uint32_t g_body[MAX_BODIES];   /* kept dead peds, oldest first */
static int g_nbody;

static int body_index(uint32_t ped)
{
    for (int i = 0; i < g_nbody; i++)
        if (g_body[i] == ped)
            return i;
    return -1;
}

static void body_drop(int i)
{
    memmove(&g_body[i], &g_body[i + 1], (size_t)(g_nbody - i - 1) * sizeof g_body[0]);
    g_nbody--;
}

/* Before the game's tick of one ped: 1 = keep it this tick (the caller then
 * undoes its count). */
int gta2_body_before_tick(uint32_t ped)
{
    new_game_check();
    int i = body_index(ped);
    if (!opt_get(OPT_KEEP_BODIES) || !wr_mem_valid(ped, 0x280) || wr_rd32(ped + 0x278) != 9 ||
        wr_rd32(ped + 0x234) == 2) {   /* not dead (a reused slot), or already being removed */
        if (i >= 0)
            body_drop(i);
        return 0;
    }
    if (!near_player((int32_t)wr_rd32(ped + 0x1AC), (int32_t)wr_rd32(ped + 0x1B0))) {
        if (i >= 0)
            body_drop(i);   /* far away: the game's own rules */
        return 0;
    }
    if (i < 0) {
        if (g_nbody == MAX_BODIES)
            body_drop(0);   /* the oldest is no longer kept */
        g_body[g_nbody++] = ped;
    }
    wr_wr16(ped + 0x20E, 0);
    static uint32_t last;
    if (getenv("WR_GAMEPLAY_LOG") && wr_time_ms() - last > 3000) {
        last = wr_time_ms();
        wr_log("gameplay: %d bodies kept, %d blood pools kept, do_blood %u", g_nbody, g_nblood, wr_rd8(0x005EAD51u));
    }
    return 1;
}

/* ---- MORE BLOOD ----------------------------------------------------------------------------- */

static uint32_t g_blood[MAX_BLOOD];

/* Before the blood pool's particle tick (sub_0048BE60, thiscall particle). */
void gta2_blood_before_tick(uint32_t p)
{
    new_game_check();
    if (!opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(p, 0x50))
        return;
    int i = -1;
    for (int k = 0; k < g_nblood; k++)
        if (g_blood[k] == p)
            i = k;
    uint32_t spr = wr_rd32(p + 0x30);
    int keep = wr_rd32(p + 0x38) == 0x27 && spr && wr_mem_valid(spr, 0x40) && wr_rd16(p + 0x2C) > 1;
    if (!keep) {
        if (i >= 0) {
            memmove(&g_blood[i], &g_blood[i + 1], (size_t)(g_nblood - i - 1) * sizeof g_blood[0]);
            g_nblood--;
        }
        return;
    }
    if (i < 0) {
        if (g_nblood == MAX_BLOOD) {
            memmove(&g_blood[0], &g_blood[1], (size_t)(MAX_BLOOD - 1) * sizeof g_blood[0]);
            g_nblood--;
        }
        g_blood[g_nblood++] = p;
    }
    wr_wr16(p + 0x2C, (uint16_t)(wr_rd16(p + 0x2C) + 1));   /* the tick's decrement given back */
}

int gta2_more_blood(void) { return opt_get(OPT_MORE_BLOOD); }

/* a new game: nothing kept from the last one */
static void new_game_check(void)
{
    uint32_t g = gta2_game();
    if (g != g_game_seen) {
        g_game_seen = g;
        g_nbody = 0;
        g_nblood = 0;
        g_nbig = g_big_next = 0;
    }
}

/* ---- the big stain: one pool drawn bigger (no overlapping sprites) -------------------- */

#define MAX_BIG 128
static uint32_t g_big[MAX_BIG][2];   /* x, y of pools created "under the body" */

void gta2_blood_mark_big(uint32_t x, uint32_t y)
{
    g_big[g_big_next][0] = x;
    g_big[g_big_next][1] = y;
    g_big_next = (g_big_next + 1) % MAX_BIG;
    if (g_nbig < MAX_BIG)
        g_nbig++;
}

/* The scale to draw sprite spr with (1 = as the game draws it). */
float gta2_sprite_scale(uint32_t spr)
{
    if (!g_nbig || !opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(spr, 0x24))
        return 1.0f;
    uint32_t st = wr_rd32(0x00665B40u);
    if (!st)
        return 1.0f;
    int f = (int)wr_rd16(spr + 0x22) - (int)(uint16_t)(wr_rd16(st + 0x8CA4) + 0xBF);
    if (f < 0 || f > 5)
        return 1.0f;   /* not a blood pool frame */
    uint32_t x = wr_rd32(spr + 0x14), y = wr_rd32(spr + 0x18);
    for (int i = 0; i < g_nbig; i++)
        if (g_big[i][0] == x && g_big[i][1] == y)
            return 1.9f;
    return 1.0f;
}
