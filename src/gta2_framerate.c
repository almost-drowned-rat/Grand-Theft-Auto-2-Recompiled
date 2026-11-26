/*
 * GTA 2 recomp - FRAME RATE 60 / 120 INTERPOLATED (M4b, docs/re/framerate.md).
 *
 * The game's logic stays a fixed 30.3 Hz tick (33 ms). With 60 / 120 the
 * loop draws extra frames between two ticks; every frame shows the world at
 * a point between the last two ticks: the render camera (+0x98 / +0x9C / +0xA0)
 * and each sprite's position (+0x14 / +0x18 / +0x1C, 18.14) are set to the
 * blend for the draw and put back right after it, so the logic never sees
 * them. Animation frames, the HUD, timers and lighting stay per tick. The
 * render path is re-runnable (measured: the sprite lists are filled inside the
 * draw, a second draw of the same tick draws the same sprites).
 * The picture lags the logic by up to one tick (33 ms) - the price of
 * interpolating instead of guessing ahead.
 */
#include "wr.h"
#include "gta2_options.h"
#include "gta2_framerate.h"

#include <stdlib.h>
#include <string.h>

#define TICK_NS 33000000ull

static uint64_t g_tick_ns;    /* when the last tick ran */
static uint32_t g_tick_id;    /* ticks seen */
static float g_alpha = 1.0f;  /* this draw's blend: 0 = the tick before, 1 = the last tick */
static int g_lerp;            /* a draw with interpolation is running */
static uint64_t g_last_draw;  /* the last frame's start: frames keep the target's spacing */

int gta2_fps_target(void)
{
    static const int r[] = { 30, 60, 120 };
    int v = opt_get(OPT_FRAME_RATE);
    return r[v >= 0 && v < 3 ? v : 0];
}

/* ---- the camera ---- */
static uint32_t g_cam;
static int32_t g_cam_prev[3], g_cam_cur[3];
static int g_cam_ok;

static uint32_t render_cam(void)
{
    uint32_t game = wr_rd32(0x005EB4FCu);
    if (!game || !wr_mem_valid(game + 0x1C, 4))
        return 0;
    uint32_t pl = wr_rd32(game + 0x1C);
    return pl && wr_mem_valid(pl + 0x14C + 0x98, 12) ? pl + 0x14C : 0;
}

static int32_t lerp(int32_t a, int32_t b, float t) { return a + (int32_t)((float)(b - a) * t); }

/* far apart = a cut (respawn, teleport, a new camera): no blend */
static int jump(const int32_t *a, const int32_t *b)
{
    for (int i = 0; i < 2; i++) {
        int32_t d = b[i] - a[i];
        if (d > 3 << 14 || d < -(3 << 14))
            return 1;
    }
    return 0;
}

/* After each logic tick (the loop step saw the schedule move). */
void gta2_fps_after_tick(void)
{
    g_tick_ns = wr_time_ns();
    g_tick_id++;
    uint32_t cam = render_cam();
    if (!cam) {
        g_cam_ok = 0;
        return;
    }
    int32_t now[3] = { (int32_t)wr_rd32(cam + 0x98), (int32_t)wr_rd32(cam + 0x9C), (int32_t)wr_rd32(cam + 0xA0) };
    g_cam_ok = cam == g_cam;
    memcpy(g_cam_prev, g_cam_ok ? g_cam_cur : now, sizeof now);
    memcpy(g_cam_cur, now, sizeof now);
    g_cam = cam;
}

/* ---- sprites: a small open-addressing table, pointer -> the last two ticks' positions ---- */
#define SLOTS 8192
typedef struct sslot { uint32_t ptr, tick; int32_t prev[3], cur[3]; } sslot;
static sslot g_sp[SLOTS];

static sslot *slot_of(uint32_t p)
{
    uint32_t h = (p * 2654435761u) >> 19;   /* 13 bits */
    for (int i = 0; i < 16; i++) {
        sslot *s = &g_sp[(h + (uint32_t)i) & (SLOTS - 1)];
        if (s->ptr == p)
            return s;
        if (!s->ptr || g_tick_id - s->tick > 2) {   /* free or stale: take it */
            s->ptr = p;
            s->tick = 0;
            return s;
        }
    }
    return NULL;
}

/* Around one sprite's draw (sub_004BE060, ecx = sprite): its position blended.
 * Returns 1 when it changed the sprite (gta2_fps_sprite_after restores). */
static int32_t g_saved[3];
int gta2_fps_sprite_before(uint32_t spr)
{
    if (!spr || !wr_mem_valid(spr + 0x14, 12))
        return 0;
    int32_t now[3] = { (int32_t)wr_rd32(spr + 0x14), (int32_t)wr_rd32(spr + 0x18), (int32_t)wr_rd32(spr + 0x1C) };
    sslot *s = slot_of(spr);
    if (!s)
        return 0;
    if (s->tick != g_tick_id) {   /* first sight in this tick */
        if (s->tick == g_tick_id - 1)
            memcpy(s->prev, s->cur, sizeof now);
        else
            memcpy(s->prev, now, sizeof now);   /* new: no history */
        memcpy(s->cur, now, sizeof now);
        s->tick = g_tick_id;
    }
    if (!g_lerp || g_alpha >= 1.0f || jump(s->prev, s->cur))
        return 0;
    memcpy(g_saved, now, sizeof now);
    for (int i = 0; i < 3; i++)
        wr_wr32(spr + 0x14 + 4 * (uint32_t)i, (uint32_t)lerp(s->prev[i], s->cur[i], g_alpha));
    return 1;
}

void gta2_fps_sprite_after(uint32_t spr)
{
    for (int i = 0; i < 3; i++)
        wr_wr32(spr + 0x14 + 4 * (uint32_t)i, (uint32_t)g_saved[i]);
}

/* ---- one draw (sub_00461960) ---- */
static int32_t g_cam_saved[3];
static uint32_t g_cam_used;

void gta2_fps_draw_begin(void)
{
    g_lerp = 0;
    g_alpha = 1.0f;
    g_last_draw = wr_time_ns();
    if (gta2_fps_target() <= 30 || !g_tick_ns)
        return;
    float a = (float)(wr_time_ns() - g_tick_ns) / (float)TICK_NS;
    g_alpha = a < 0 ? 0 : a > 1 ? 1 : a;
    g_lerp = 1;
    g_cam_used = 0;
    uint32_t cam = render_cam();
    if (g_cam_ok && cam == g_cam && !jump(g_cam_prev, g_cam_cur)) {
        for (int i = 0; i < 3; i++) {
            g_cam_saved[i] = (int32_t)wr_rd32(cam + 0x98 + 4 * (uint32_t)i);
            wr_wr32(cam + 0x98 + 4 * (uint32_t)i, (uint32_t)lerp(g_cam_prev[i], g_cam_cur[i], g_alpha));
        }
        g_cam_used = cam;
    }
}

void gta2_fps_draw_end(void)
{
    if (g_cam_used)
        for (int i = 0; i < 3; i++)
            wr_wr32(g_cam_used + 0x98 + 4 * (uint32_t)i, (uint32_t)g_cam_saved[i]);
    g_cam_used = 0;
    g_lerp = 0;
}

/* Between loop steps with nothing due: is an extra frame due now? */
int gta2_fps_extra_due(void)
{
    int fps = gta2_fps_target();
    if (fps <= 30 || !g_tick_ns)
        return 0;
    uint64_t now = wr_time_ns(), period = 1000000000ull / (uint64_t)fps;
    if (now - g_tick_ns + 2000000ull >= TICK_NS)
        return 0;   /* the next tick is (nearly) due: let it run */
    return now - g_last_draw + 500000ull >= period;
}
