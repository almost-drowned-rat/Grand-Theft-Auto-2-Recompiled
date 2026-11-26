/*
 * GTA 2 Recompiled - native replacements for individual guest functions.
 *
 * A `void sub_XXXXXXXX(RECOMP_PARAMS)` defined here is left out of the
 * generated code (game.toml [recomp] exclude_manual names this file), so
 * direct calls bind to it; wr_manual_register covers indirect calls.
 */
#include "wr.h"
#include "vk/wr_vk.h"
#include "gta2_menu.h"

#include <stdlib.h>
#include <string.h>
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
    {   /* the pause menu (gta2_menu.c) between two loop steps */
        uint32_t ecx = ctx->r_ecx, eax = ctx->r_eax;
        gta2_pause_tick(ctx);
        ctx->r_ecx = ecx;
        ctx->r_eax = eax;
    }
    uint32_t next0 = MEM32(0x00662748u);
    uint64_t f0 = gta2_frames_presented();
    sub_00462A30_gen(ctx, base);
    uint32_t next = MEM32(0x00662748u);
    if (next != next0)
        wr_overlay_game_tick();   /* GAME SPEED */
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
uint32_t g_gta2_frontend_ms;   /* last frontend step: the menus are on screen */

void sub_0045A320(RECOMP_PARAMS)
{
    g_gta2_frontend_ms = wr_time_ms();
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

/* ==== Widescreen: a real wider view (docs/re/widescreen.md) ====================
 * Hor+: at W/H > 4:3 the city is shown at the 4:3 zoom for that height with
 * more of it left and right; the HUD is laid out in the game's virtual
 * 640x480 scaled by H/480 and anchored to the real edges. All changes are
 * gated on the aspect: at exactly 4:3 the original code runs unchanged.
 * All cameras (render, logic, aux) are widened, so nothing pops in at the
 * sides; the game's spawn/despawn ring follows the wider view. */

static inline int32_t fxmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 14); }
static inline int32_t fxdiv(int32_t a, int32_t b) { return b ? (int32_t)(((int64_t)a << 14) / b) : 0; }

/* A = (W/H) / (4/3) in Q14 for a camera, 0x4000 at 4:3 (or anything unset) */
static int32_t cam_aspect(uint32_t cam)
{
    if (!cam || !wr_mem_valid(cam + 0x68, 8))
        return 0x4000;
    int32_t W = (int32_t)MEM32(cam + 0x68), H = (int32_t)MEM32(cam + 0x6C);
    if (W <= 0 || H <= 0 || (int64_t)W * 3 <= (int64_t)H * 4)
        return 0x4000;
    return (int32_t)(((int64_t)W * 3 << 14) / ((int64_t)4 * H));
}

static int32_t render_aspect(void) { return cam_aspect(MEM32(0x005E3CC4u)); }

/* 0x004CB150: refresh the cameras' viewport after a mode change. The original
 * keys the fullscreen test on the height only, so 640x480 -> 854x480 never
 * reached the cameras. Same logic, keyed on width and height. */
static uint32_t g_ws_last_w;
void sub_004CB150(RECOMP_PARAMS)
{
    uint32_t game = MEM32(0x005EB4FCu);
    if (!game) {
        g_esp += 4;
        return;
    }
    uint32_t vs = MEM32(0x00673D20u);
    uint32_t W, H, keep;
    int windowed = MEM32(vs + 0x40) == 0xFFFFFFFEu;
    if (windowed) {
        W = MEM32(0x00673578u);
        H = MEM32(0x006732E8u);
        keep = W;
    } else {
        W = MEM32(vs + 0x48);
        H = MEM32(vs + 0x4C);
        keep = H;
    }
    uint32_t last = MEM32(0x006735A0u);
    int changed = windowed ? last != W : (last != H || W != g_ws_last_w);
    if (changed) {
        uint32_t args[2] = { W, H };
        ctx->r_ecx = game;
        wr_call_guest(ctx, 0x0045A4F0u, 2, args);
    }
    g_ws_last_w = W;
    MEM32(0x006735A0u) = keep;
    wr_call_guest(ctx, 0x004619D0u, 0, NULL);
    wr_call_guest(ctx, 0x004619E0u, 0, NULL);
    g_esp += 4;
}

/* 0x0041EB70 set viewport (W, H): HUD scale +0xA8 = H/480 instead of W/640 */
extern void sub_0041EB70_gen(RECOMP_PARAMS);
void sub_0041EB70(RECOMP_PARAMS)
{
    uint32_t cam = ctx->r_ecx, W = MEM32(g_esp + 4), H = MEM32(g_esp + 8);
    sub_0041EB70_gen(ctx, base);
    if (H && (uint64_t)W * 3 > (uint64_t)H * 4)
        MEM32(cam + 0xA8) = (uint32_t)(((int64_t)H << 14) / 480);
}

/* 0x0041E7A0 per-frame projection scale + view rect: at W/H > 4:3 the scale
 * comes from the 4:3 width for this height, and the rect widens by A. */
extern void sub_0041E7A0_gen(RECOMP_PARAMS);
void sub_0041E7A0(RECOMP_PARAMS)
{
    uint32_t cam = ctx->r_ecx;
    sub_0041E7A0_gen(ctx, base);
    int32_t A = cam_aspect(cam);
    if (A == 0x4000)
        return;
    int32_t H = (int32_t)MEM32(cam + 0x6C);
    int32_t zoom = (int32_t)MEM32(cam + 0xA4);
    int32_t weff = (int32_t)(((int64_t)H * 640 / 480) << 14);
    MEM32(cam + 0x60) = (uint32_t)fxmul(weff, zoom);
    int32_t E = fxmul(fxmul(0x1C000 + (int32_t)MEM32(cam + 0xA0), fxdiv(0x4000, zoom)), 0x2000);
    int32_t Ex = fxmul(E, A);
    int32_t x = (int32_t)MEM32(cam + 0x98);
    int32_t x0 = x - Ex, x1 = x + Ex;
    if (x0 < 0) x0 = 0;
    if (x0 > 0x3FFFFF) x0 = 0x3FFFFF;
    if (x1 < 0) x1 = 0;
    if (x1 > 0x3FFFFF) x1 = 0x3FFFFF;
    MEM32(cam + 0x78) = (uint32_t)x0;
    MEM32(cam + 0x7C) = (uint32_t)x1;
    MEM32(cam + 0x20) = (uint32_t)(x0 - 0x3000);
    MEM32(cam + 0x24) = (uint32_t)(x1 + 0x3000);
}

/* hook 0x004721C2 (block renderer): tile x half-span [esp+0x18] *= A */
void gta2_ws_tile_span(RECOMP_PARAMS)
{
    int32_t A = render_aspect();
    if (A != 0x4000)
        MEM32(g_esp + 0x18) = (uint32_t)fxmul((int32_t)MEM32(g_esp + 0x18), A);
}

/* hook 0x0046BB9A: the per-layer tile list has 1500 slots and no bound */
static uint32_t g_ws_tile_peak;
int gta2_ws_tile_guard(RECOMP_PARAMS)
{
    uint32_t n = MEM32(ctx->r_edi + 0x2EFC);
    if (n > g_ws_tile_peak) {
        g_ws_tile_peak = n;
        if (n >= 1000)
            wr_log("widescreen: tile list peak %u of 1500", n);
    }
    return n >= 1500;   /* full: drop the tile (on_true -> the function's epilogue) */
}

/* hooks after `push 0x280` / `push 0x1E0` / `push &zero`: 640x480 bounds */
static uint32_t g_ws_lowbound;   /* guest Q14 for sub_0041E710's lower x bound */
void gta2_ws_vis_lo(RECOMP_PARAMS)       /* 0x0041E72E: x >= 320 - 320*A */
{
    int32_t A = render_aspect();
    if (A == 0x4000)
        return;
    if (!g_ws_lowbound)
        g_ws_lowbound = wr_galloc(4);
    MEM32(g_ws_lowbound) = (uint32_t)((320 << 14) - fxmul(320 << 14, A));
    MEM32(g_esp) = g_ws_lowbound;
}
void gta2_ws_vis_hi(RECOMP_PARAMS)       /* 0x0041E740: x < 320 + 320*A */
{
    int32_t A = render_aspect();
    if (A != 0x4000)
        MEM32(g_esp) = (uint32_t)(320 + ((320 * (int64_t)A) >> 14));
}
void gta2_ws_cull_w(RECOMP_PARAMS)       /* 0x004B9392: real width */
{
    uint32_t cam = MEM32(0x005E3CC4u);
    if (render_aspect() != 0x4000)
        MEM32(g_esp) = MEM32(cam + 0x68);
}
void gta2_ws_cull_h(RECOMP_PARAMS)       /* 0x004B93C0: real height */
{
    uint32_t cam = MEM32(0x005E3CC4u);
    if (render_aspect() != 0x4000)
        MEM32(g_esp) = MEM32(cam + 0x6C);
}

/* HUD primitives: anchor the virtual-640 x to the real left/centre/right */
enum { ANC_LEFT, ANC_CENTRE, ANC_RIGHT, ANC_BYX };
static const struct { uint32_t ret; uint8_t anc; } k_hud_anchor[] = {
    { 0x4C758F, ANC_LEFT }, { 0x4C75C4, ANC_LEFT }, { 0x4C75F9, ANC_LEFT }, { 0x4C7650, ANC_LEFT },
    { 0x4C76B1, ANC_LEFT }, { 0x4C7757, ANC_LEFT }, { 0x4C7791, ANC_LEFT }, { 0x4C77D2, ANC_LEFT },
    { 0x4C7813, ANC_LEFT }, { 0x4C787C, ANC_LEFT }, { 0x4C9492, ANC_LEFT }, { 0x4C94E2, ANC_LEFT },
    { 0x4C7BFB, ANC_RIGHT }, { 0x4C7C51, ANC_RIGHT }, { 0x4C7CA9, ANC_RIGHT }, { 0x4C89F9, ANC_RIGHT },
    { 0x4C9AAC, ANC_RIGHT }, { 0x4C9B3B, ANC_RIGHT },
    /* sub_004C9C20's icons ($, x, ...) take their x from the rolling counter
     * they belong to, which sub_0044B500 already anchored: no second shift */
    { 0x4C9D16, ANC_LEFT }, { 0x4C9E3D, ANC_LEFT }, { 0x4C9F42, ANC_LEFT }, { 0x4C9F8C, ANC_LEFT },
    { 0x4C9DA9, ANC_LEFT }, { 0x4C9EBD, ANC_LEFT },
    { 0x4C9BAB, ANC_RIGHT }, { 0x4C9C16, ANC_RIGHT },
    { 0x4C7AEF, ANC_CENTRE }, { 0x4C87B0, ANC_CENTRE }, { 0x4C8810, ANC_CENTRE }, { 0x4C8872, ANC_CENTRE },
    { 0x4C8A8E, ANC_CENTRE }, { 0x4C8BDE, ANC_CENTRE }, { 0x4C9557, ANC_CENTRE }, { 0x4C9590, ANC_CENTRE },
    { 0x4C95FD, ANC_CENTRE }, { 0x4C9638, ANC_CENTRE }, { 0x4C967C, ANC_CENTRE }, { 0x4C96E2, ANC_CENTRE },
    { 0x4C9742, ANC_CENTRE }, { 0x4C9901, ANC_CENTRE }, { 0x4C9944, ANC_CENTRE }, { 0x4C998F, ANC_CENTRE },
    { 0x4C99D5, ANC_CENTRE }, { 0x4C9A36, ANC_CENTRE }, { 0x4CA012, ANC_CENTRE }, { 0x4CA04C, ANC_CENTRE },
    { 0x4CA085, ANC_CENTRE }, { 0x4CA0E2, ANC_CENTRE }, { 0x4CA1C8, ANC_CENTRE }, { 0x4CA41B, ANC_CENTRE },
    { 0x4C79B3, ANC_CENTRE },
};

static void hud_anchor(X86Ctx *ctx, uint32_t xoff)
{
    int32_t A = render_aspect();
    if (A == 0x4000)
        return;
    uint32_t ret = MEM32(g_esp);
    int anc = ANC_BYX;
    for (size_t i = 0; i < sizeof k_hud_anchor / sizeof k_hud_anchor[0]; i++)
        if (k_hud_anchor[i].ret == ret) {
            anc = k_hud_anchor[i].anc;
            break;
        }
    int32_t x = (int32_t)MEM32(g_esp + xoff);
    if (anc == ANC_BYX)   /* data-placed counters, unlisted callers: by position */
        anc = x >= (427 << 14) ? ANC_RIGHT : x >= (213 << 14) ? ANC_CENTRE : ANC_LEFT;
    if (getenv("WR_HUD_LOG") && gta2_frames_presented() == (uint64_t)atoi(getenv("WR_HUD_LOG")))
        wr_log("hud: ret 0x%08X x %.1f anchor %d", ret, x / 16384.0, anc);
    int32_t extra = fxmul(640 << 14, A) - (640 << 14);   /* virtual width beyond 640 */
    if (anc == ANC_RIGHT)
        x += extra;
    else if (anc == ANC_CENTRE)
        x += extra / 2;
    MEM32(g_esp + xoff) = (uint32_t)x;
}

extern void sub_004C71B0_gen(RECOMP_PARAMS);
void sub_004C71B0(RECOMP_PARAMS) { hud_anchor(ctx, 12); sub_004C71B0_gen(ctx, base); }   /* (type, frame, x, ...) */
extern void sub_004C7280_gen(RECOMP_PARAMS);
void sub_004C7280(RECOMP_PARAMS) { hud_anchor(ctx, 8); sub_004C7280_gen(ctx, base); }    /* (str, x, ...) */
extern void sub_004BA2C0_gen(RECOMP_PARAMS);
void sub_004BA2C0(RECOMP_PARAMS) { hud_anchor(ctx, 8); sub_004BA2C0_gen(ctx, base); }    /* (str, x, y, font) */

/* 0x0044B500 rolling counter draw (thiscall, x, y as virtual ints): all four
 * callers (0x004C9C94, 0x004C9DBB, 0x004C9F01, 0x004C9F4F) are the right-hand
 * HUD group (money, multiplier, lives counters) - anchor right. */
extern void sub_0044B500_gen(RECOMP_PARAMS);
void sub_0044B500(RECOMP_PARAMS)
{
    int32_t A = render_aspect();
    if (A != 0x4000)
        MEM32(g_esp + 4) += (uint32_t)((fxmul(640 << 14, A) - (640 << 14)) >> 14);
    sub_0044B500_gen(ctx, base);
}

void gta2_manual_register(void)
{
    wr_manual_register(0x004031C0u, sub_004031C0);
    wr_manual_register(0x004CB150u, sub_004CB150);
}

/* ---- OPTIONS menu in the frontend (gta2_menu.c, docs/re/frontend.md) ---------------------- */

extern void sub_00453E20_gen(RECOMP_PARAMS);
void sub_00453E20(RECOMP_PARAMS)   /* builds every frontend page */
{
    uint32_t self = ctx->r_ecx;
    sub_00453E20_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_menu_frontend_built(self);
    ctx->r_eax = eax;
}

extern void sub_00455F90_gen(RECOMP_PARAMS);
void sub_00455F90(RECOMP_PARAMS)   /* picks the background picture */
{
    sub_00455F90_gen(ctx, base);
    gta2_menu_background();
}

extern void sub_004597C0_gen(RECOMP_PARAMS);
void sub_004597C0(RECOMP_PARAMS)   /* menu input step (thiscall fe) */
{
    uint32_t self = ctx->r_ecx;
    gta2_menu_input(ctx);          /* page 12: takes and clears the edge flags */
    ctx->r_ecx = self;
    sub_004597C0_gen(ctx, base);
    gta2_menu_after_input();
}

extern void sub_00457920_gen(RECOMP_PARAMS);
void sub_00457920(RECOMP_PARAMS)   /* draws the page's lines (inside Begin/EndScene) */
{
    sub_00457920_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_menu_draw(ctx);
    ctx->r_eax = eax;
}

extern void sub_00451930_gen(RECOMP_PARAMS);
void sub_00451930(RECOMP_PARAMS)   /* reads the registry Debug flags at start */
{
    sub_00451930_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_menu_apply_cheats();
    ctx->r_eax = eax;
}

/* ---- MODERN controls on foot (gta2_state.c) ----------------------------------------------- */
void gta2_modern_on_foot(uint32_t player, uint32_t ped);

extern void sub_004A5C50_gen(RECOMP_PARAMS);
void sub_004A5C50(RECOMP_PARAMS)   /* on-foot control step (thiscall player, ped; ret 4) */
{
    uint32_t player = ctx->r_ecx, ped = MEM32(ctx->r_esp + 4);
    sub_004A5C50_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_modern_on_foot(player, ped);
    ctx->r_eax = eax;
}

/* ---- the pause menu over the HUD ---------------------------------------------------------- */
extern void sub_004CA440_gen(RECOMP_PARAMS);
void sub_004CA440(RECOMP_PARAMS)   /* draws the HUD each frame (thiscall HUD) */
{
    sub_004CA440_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    gta2_pause_draw(ctx);
    ctx->r_eax = eax;
}

extern void sub_004C9FA0_gen(RECOMP_PARAMS);
void sub_004C9FA0(RECOMP_PARAMS)   /* the game's PAUSED panel (thiscall, plain ret) */
{
    if (gta2_pause_hide_panel()) {
        ctx->r_esp += 4;   /* ret */
        return;
    }
    sub_004C9FA0_gen(ctx, base);
}

/* ---- twin-stick walking (gta2_state.c) ----------------------------------------------------- */
int gta2_twin_move_angle(uint32_t obj);

extern void sub_0041FC20_gen(RECOMP_PARAMS);
void sub_0041FC20(RECOMP_PARAMS)   /* (u16 *angle, speed *, out dx *, out dy *) stdcall: a vector along an angle */
{
    uint32_t ret = MEM32(ctx->r_esp);
    /* the player ped's walk step / velocity from its heading (object in
     * esi, angle = [esi+0x40]); measured: 0x0049ACBA and 0x0049AEA7 */
    if ((ret == 0x0049ACBAu || ret == 0x0049AEA7u || ret == 0x0049A80Bu) &&
        MEM32(ctx->r_esp + 4) == ctx->r_esi + 0x40u) {
        int a = gta2_twin_move_angle(ctx->r_esi);
        if (a >= 0) {
            static uint32_t tmp;
            if (!tmp)
                tmp = wr_galloc(16);
            MEM16(tmp) = (uint16_t)a;
            MEM32(ctx->r_esp + 4) = tmp;   /* move along the left stick, keep facing the right one */
        }
    }
    sub_0041FC20_gen(ctx, base);
}

/* ---- GAMEPLAY: KEEP BODIES, MORE BLOOD (gta2_gameplay.c) ----------------------------------- */
#include "gta2_gameplay.h"

extern void sub_004454E0_gen(RECOMP_PARAMS);
void sub_004454E0(RECOMP_PARAMS)   /* one ped's tick (thiscall ped) */
{
    uint32_t ped = ctx->r_ecx;
    if (!gta2_body_before_tick(ped)) {
        sub_004454E0_gen(ctx, base);
        return;
    }
    uint8_t all = MEM8(0x005E5BEAu);
    uint16_t shown = MEM16(0x005E5BF4u);
    ctx->r_ecx = ped;
    sub_004454E0_gen(ctx, base);
    MEM8(0x005E5BEAu) = all;      /* a kept body does not count against the ped limits */
    MEM16(0x005E5BF4u) = shown;
}

extern void sub_0048BE60_gen(RECOMP_PARAMS);
void sub_0048BE60(RECOMP_PARAMS)   /* the blood pool particle's tick (thiscall particle) */
{
    uint32_t p = ctx->r_ecx;
    gta2_blood_before_tick(p);
    ctx->r_ecx = p;
    sub_0048BE60_gen(ctx, base);
}

extern void sub_0048CC50_gen(RECOMP_PARAMS);
void sub_0048CC50(RECOMP_PARAMS)   /* creates a blood pool at (x, y, z), stdcall 0xC */
{
    uint32_t x = MEM32(ctx->r_esp + 4), y = MEM32(ctx->r_esp + 8), z = MEM32(ctx->r_esp + 12);
    uint32_t ecx = ctx->r_ecx;
    if (!gta2_more_blood()) {
        sub_0048CC50_gen(ctx, base);
        return;
    }
    /* MORE BLOOD (maintainer 2026-11-26), by chance:
     *  under the body: a big stain - the pool and four more, overlapping
     *  beside it: one pool only, moved a little to a random side */
    static uint32_t rnd = 0x2545F491u;
    rnd ^= x * 2654435761u ^ y * 40503u;
    rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
    if (!(rnd & 1)) {
        static const int32_t dir[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                           { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
        int d = (int)(rnd >> 4) & 7;
        int32_t step = d < 4 ? 3600 : 2550;   /* about 0.22 block, the same on the diagonals */
        MEM32(ctx->r_esp + 4) = x + (uint32_t)(dir[d][0] * step);
        MEM32(ctx->r_esp + 8) = y + (uint32_t)(dir[d][1] * step);
        if (getenv("WR_GAMEPLAY_LOG"))
            wr_log("blood: beside");
        sub_0048CC50_gen(ctx, base);
        return;
    }
    if (getenv("WR_GAMEPLAY_LOG"))
        wr_log("blood: under");
    sub_0048CC50_gen(ctx, base);
    uint32_t eax = ctx->r_eax;
    static const int32_t off[4][2] = { { 2000, 300 }, { -2000, -300 }, { -300, 2000 }, { 300, -2000 } };
    for (int i = 0; i < 4; i++) {
        uint32_t a[3] = { x + (uint32_t)off[i][0], y + (uint32_t)off[i][1], z };
        ctx->r_ecx = ecx;
        uint32_t sp = ctx->r_esp;
        for (int k = 2; k >= 0; k--) { ctx->r_esp -= 4; MEM32(ctx->r_esp) = a[k]; }
        ctx->r_esp -= 4;
        MEM32(ctx->r_esp) = 0;   /* return address (unused by the callee) */
        sub_0048CC50_gen(ctx, base);
        ctx->r_esp = sp;
    }
    ctx->r_eax = eax;
}
