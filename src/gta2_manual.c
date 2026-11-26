/*
 * GTA 2 Recompiled - native replacements for individual guest functions.
 *
 * A `void sub_XXXXXXXX(RECOMP_PARAMS)` defined here is left out of the
 * generated code (game.toml [recomp] exclude_manual names this file), so
 * direct calls bind to it; wr_manual_register covers indirect calls.
 */
#include "wr.h"

#include <stdlib.h>
#include <time.h>


/* 0x004031C0 GetDXVersion(DWORD *version, DWORD *platform), stdcall: the DX
 * SDK's detection that creates DirectDraw/DirectInput objects to tell the
 * installed DirectX apart. The runtime replaces DirectX, so answer what the
 * game needs (>= 0x601): DirectX 8.1 on the NT platform. */
void sub_004031C0(RECOMP_PARAMS)
{
    (void)base;
    uint32_t ver = WR_ARG(0), plat = WR_ARG(1);
    if (ver)
        wr_wr32(ver, 0x801);
    if (plat)
        wr_wr32(plat, 2);
    WR_RET(0, 2);
}

/* 0x00462A30 the main loop's per-iteration step: one 33 ms logic tick, one
 * draw, or reading input - and otherwise nothing, so the original spins at
 * 100 % of a core until the next tick is due (docs/re/framerate.md). Wrapped:
 * an iteration that neither ticked nor drew sleeps until shortly before the
 * deadline [0x00662748] (timeGetTime ms). Game logic and timing unchanged. */
extern void sub_00462A30_gen(RECOMP_PARAMS);
extern uint64_t gta2_frames_presented(void);
void sub_00462A30(RECOMP_PARAMS)
{
    uint32_t next0 = MEM32(0x00662748u);
    uint64_t f0 = gta2_frames_presented();
    sub_00462A30_gen(ctx, base);
    uint32_t next = MEM32(0x00662748u);
    if (next != next0 && getenv("WR_TICK_LOG")) {
        static uint64_t last;
        uint64_t t = wr_time_ns();
        static int n;
        if (last && n++ < 200)
            wr_log("tick +%.3f ms (next %u, now %u)", (double)(t - last) / 1e6, next, wr_time_ms());
        last = t;
    }
    if (next != next0 || gta2_frames_presented() != f0 || !MEM8(0x00595010u))
        return;   /* ticked, drew, or the limiter is off: no waiting */
    int32_t left = (int32_t)(next - wr_time_ms());
    if (left > 2) {
        uint64_t until = wr_time_ns() + (uint64_t)(left - 1) * 1000000u;
        while (wr_time_ns() < until) {
            uint64_t rest = until - wr_time_ns();
            struct timespec ts = { 0, (long)(rest > 900000 ? rest - 200000 : rest) };
            nanosleep(&ts, NULL);
        }
    }
}

/* 0x0045A320 the frontend (menus) loop step: a frame when timeGetTime
 * reaches [this+0xC9C8], else it returns at once and WinMain calls it again
 * - a spin at 100 % of a core. Same wrapper: sleep up to the deadline. */
extern void sub_0045A320_gen(RECOMP_PARAMS);
void sub_0045A320(RECOMP_PARAMS)
{
    uint32_t self = ctx->r_ecx;
    uint64_t f0 = gta2_frames_presented();
    sub_0045A320_gen(ctx, base);
    if (gta2_frames_presented() != f0 || !wr_mem_valid(self + 0xC9C8u, 8))
        return;
    if (MEM8(self + 0xC9CCu))
        return;   /* an update just ran: its frame is drawn by the next call */
    int32_t left = (int32_t)(MEM32(self + 0xC9C8u) - wr_time_ms());
    if (left > 2 && left < 1000) {
        uint64_t until = wr_time_ns() + (uint64_t)(left - 1) * 1000000u;
        while (wr_time_ns() < until) {
            uint64_t rest = until - wr_time_ns();
            struct timespec ts = { 0, (long)(rest > 900000 ? rest - 200000 : rest) };
            nanosleep(&ts, NULL);
        }
    }
}

void gta2_manual_register(void)
{
    wr_manual_register(0x004031C0u, sub_004031C0);
}
