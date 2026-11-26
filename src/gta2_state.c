/*
 * GTA 2 Recompiled - the local player's state, read from guest memory
 * (docs/re/gameplay_input_pause.md §2), and the MODERN on-foot control.
 */
#include "gta2_state.h"
#include "gta2_options.h"
#include "wr.h"
#include "platform/wr_platform.h"

#include <math.h>
#include <stdlib.h>

#define GAME_PTR 0x005EB4FCu

uint32_t gta2_game(void) { return wr_rd32(GAME_PTR); }

uint32_t gta2_player(void)
{
    uint32_t g = gta2_game();
    return g && wr_mem_valid(g, 0x40) ? wr_rd32(g + 0x38) : 0;
}

uint32_t gta2_player_ped(void)
{
    uint32_t p = gta2_player();
    return p && wr_mem_valid(p, 0x2D0) ? wr_rd32(p + 0x2C4) : 0;
}

int gta2_player_driving(void)
{
    uint32_t ped = gta2_player_ped();
    if (!ped || !wr_mem_valid(ped, 0x280))
        return 0;
    uint32_t car = wr_rd32(ped + 0x16C);
    if (!car || wr_rd32(ped + 0x248) == 1 || !wr_mem_valid(car, 0x90))
        return 0;
    uint32_t model = wr_rd32(car + 0x84);
    if (model == 0x3B || model == 0x3C || model == 0x3D || model == 6)
        return 0;   /* train / bus type: the game uses on-foot controls */
    return 1;
}

/* MODERN on foot, after the game's own on-foot step (sub_004A5C50, thiscall
 * player): turn the ped toward the stick and walk while it is pushed.
 * Heading = u16 [[ped+0x168]+0x40], 1440 per turn, 0 = screen down,
 * growing counter-clockwise; player+8 = this tick's turn (signed, wrapped),
 * player+0xC = walk command (Q14). */
void gta2_modern_on_foot(uint32_t player, uint32_t ped)
{
    if (opt_get(OPT_CONTROL_SCHEME) != 1 || !player || !ped || !wr_mem_valid(ped, 0x180))
        return;
    float sx, sy;
    if (!wr_pad_stick(&sx, &sy))
        return;
    float mag = sqrtf(sx * sx + sy * sy);
    if (mag < 0.3f)
        return;   /* centred: the game's own result (d-pad tank controls) stays */
    uint32_t obj = wr_rd32(ped + 0x168);
    if (!obj || !wr_mem_valid(obj + 0x40, 2))
        return;
    int target = (int)lroundf(atan2f(sx, sy) * (720.0f / 3.14159265f));
    target = ((target % 1440) + 1440) % 1440;
    int cur = wr_rd16(obj + 0x40) % 1440;
    int d = target - cur;
    if (d >= 720) d -= 1440;
    if (d < -720) d += 1440;
    const int max_turn = 96;   /* 24 degrees per tick: a quick, smooth snap */
    if (d > max_turn) d = max_turn;
    if (d < -max_turn) d = -max_turn;
    wr_wr16(player + 8, (uint16_t)(d < 0 ? d + 1440 : d));
    if (mag >= 0.49f)
        wr_wr32(player + 0xC, wr_rd32(0x0066B0F8u));   /* walk forward (+1.0) */
    static int log = -1, n;
    if (log < 0)
        log = getenv("WR_MODERN_LOG") != NULL;
    if (log && n++ % 15 == 0)
        wr_log("modern: heading %d target %d pos %d,%d", cur, target, (int32_t)wr_rd32(ped + 0x1AC),
               (int32_t)wr_rd32(ped + 0x1B0));
}
