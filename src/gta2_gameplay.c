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
 * MORE BLOOD: about one pool in four is drawn bigger, and pools dry in twice
 * the original time (about 53 s instead of 27 s): the particle tick
 * sub_0048BE60 runs their life down by one per tick, the wrapper gives every
 * other step back.
 */
#include "gta2_gameplay.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "wr.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BODIES 40
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
        wr_log("gameplay: %d bodies kept, do_blood %u", g_nbody, wr_rd8(0x005EAD51u));
    }
    return 1;
}

/* ---- MORE BLOOD ----------------------------------------------------------------------------- */


/* Before the blood pool's particle tick (sub_0048BE60, thiscall particle). */
void gta2_blood_before_tick(uint32_t p)
{
    if (!opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(p, 0x50) || wr_rd32(p + 0x38) != 0x27 || wr_rd16(p + 0x2C) < 2)
        return;
    /* dries in twice the original time (maintainer 2026-11-26): every
     * other tick's decrement is given back. The pool's +0x48 counts 3..0
     * every tick once it has grown, so two of its four steps qualify. */
    if (wr_rd8(p + 0x48) & 1)
        wr_wr16(p + 0x2C, (uint16_t)(wr_rd16(p + 0x2C) + 1));
}

int gta2_more_blood(void) { return opt_get(OPT_MORE_BLOOD); }

/* a new game: nothing kept from the last one */
static void new_game_check(void)
{
    uint32_t g = gta2_game();
    if (g != g_game_seen) {
        g_game_seen = g;
        g_nbody = 0;
    }
}

/* ---- bigger pools: by chance a pool's sprite is drawn 1.6x about its centre
 * (one sprite; the game already places pools beside or under the body itself;
 * maintainer 2026-11-26) -------------------------------------------------------- */

/* The scale to draw sprite spr with (1 = as the game draws it). */
float gta2_sprite_scale(uint32_t spr)
{
    if (!opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(spr, 0x24))
        return 1.0f;
    uint32_t st = wr_rd32(0x00665B40u);
    if (!st)
        return 1.0f;
    int f = (int)wr_rd16(spr + 0x22) - (int)(uint16_t)(wr_rd16(st + 0x8CA4) + 0xBF);
    if (getenv("WR_GAMEPLAY_LOG")) {
        static int n;
        if (n++ % 2000 == 0)
            wr_log("scale: sprite %08X frame %u (blood base %u)", spr, wr_rd16(spr + 0x22),
                   (unsigned)(uint16_t)(wr_rd16(st + 0x8CA4) + 0xBF));
    }
    if (f < 0 || f > 5)
        return 1.0f;   /* not one of the six blood pool frames */
    if (getenv("WR_GAMEPLAY_LOG")) {
        static int m;
        if (m++ % 300 == 0)
            wr_log("scale: blood sprite %08X frame %d", spr, f);
    }
    /* by chance (fixed per pool: from its position) the normal or the bigger pool */
    uint32_t h = wr_rd32(spr + 0x14) * 2654435761u ^ wr_rd32(spr + 0x18) * 40503u;
    h ^= h >> 15;
    return (h & 3) == 0 ? 1.6f : 1.0f;   /* about one pool in four is the big one */
}
