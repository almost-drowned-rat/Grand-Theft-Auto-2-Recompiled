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

static int angle_of(float x, float y)   /* stick -> Ang16 (0 = screen down, CCW) */
{
    int a = (int)lroundf(atan2f(x, y) * (720.0f / 3.14159265f));
    return ((a % 1440) + 1440) % 1440;
}

static int turn_toward(uint32_t player, int cur, int target)
{
    int d = target - cur;
    if (d >= 720) d -= 1440;
    if (d < -720) d += 1440;
    const int max_turn = 96;
    if (d > max_turn) d = max_turn;
    if (d < -max_turn) d = -max_turn;
    wr_wr16(player + 8, (uint16_t)(d < 0 ? d + 1440 : d));
    return d;
}

/* Twin-stick (maintainer 2026-11-26): the right stick only turns the ped,
 * the left stick says where it walks. The game moves a ped along its heading,
 * so the movement vector of the player's ped is computed from the left
 * stick's angle instead (gta2_twin_move_angle, wrapper of sub_0041FC20). */
static uint32_t g_twin_obj;     /* the player ped's physics object while twin-stick walks */
static int g_twin_angle;

static void modern_aim(uint32_t player, uint32_t ped, float rx, float ry)
{
    g_twin_obj = 0;
    uint32_t obj = wr_rd32(ped + 0x168);
    if (!obj || !wr_mem_valid(obj + 0x40, 2))
        return;
    int cur = wr_rd16(obj + 0x40) % 1440;
    turn_toward(player, cur, angle_of(rx, ry));
    float sx = 0, sy = 0;
    int dx, dy;
    wr_pad_stick(&sx, &sy);
    if (sqrtf(sx * sx + sy * sy) < 0.49f && wr_pad_dpad(&dx, &dy)) {
        sx = (float)dx;
        sy = (float)dy;
    }
    if (sqrtf(sx * sx + sy * sy) < 0.49f) {
        wr_wr32(player + 0xC, 0);   /* aim in place */
        return;
    }
    wr_wr32(player + 0xC, wr_rd32(0x0066B0F8u));   /* walk (+1.0) ... */
    g_twin_obj = obj;                               /* ... where the left stick points */
    g_twin_angle = angle_of(sx, sy);
    static int n;
    if (getenv("WR_MODERN_LOG") && n++ % 15 == 0)
        wr_log("modern twin: heading %d move %d pos %d,%d", cur, g_twin_angle, (int32_t)wr_rd32(ped + 0x1AC),
               (int32_t)wr_rd32(ped + 0x1B0));
}

/* For the ped movement call (sub_0041FC20 returning to 0x0049A80B, the
 * object in esi): the angle to move along, or -1 to keep the heading. */
int gta2_twin_move_angle(uint32_t obj)
{
    return g_twin_obj && obj == g_twin_obj ? g_twin_angle : -1;
}

/* MODERN on foot, after the game's own on-foot step (sub_004A5C50, thiscall
 * player): turn the ped toward the stick and walk while it is pushed.
 * Heading = u16 [[ped+0x168]+0x40], 1440 per turn, 0 = screen down,
 * growing counter-clockwise; player+8 = this tick's turn (signed, wrapped),
 * player+0xC = walk command (Q14). */
void gta2_modern_on_foot(uint32_t player, uint32_t ped)
{
    g_twin_obj = 0;
    if (opt_get(OPT_CONTROL_SCHEME) != 1 || !player || !ped || !wr_mem_valid(ped, 0x180))
        return;
    float rx = 0, ry = 0;
    wr_pad_stick_right(&rx, &ry);
    if (sqrtf(rx * rx + ry * ry) >= 0.35f) {   /* right stick: turn / aim (maintainer 2026-11-26) */
        modern_aim(player, ped, rx, ry);
        return;
    }
    g_twin_obj = 0;
    float sx = 0, sy = 0;
    wr_pad_stick(&sx, &sy);
    float mag = sqrtf(sx * sx + sy * sy);
    int dx, dy;
    if (mag < 0.3f && wr_pad_dpad(&dx, &dy)) {   /* the d-pad as a full push in 8 directions */
        sx = (float)dx;
        sy = (float)dy;
        mag = 1.0f;
    }
    if (mag < 0.3f)
        return;   /* centred: nothing to steer */
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
