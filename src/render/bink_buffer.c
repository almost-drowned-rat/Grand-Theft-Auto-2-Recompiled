/*
 * GTA 2 Recompiled - Bink movies: the lifted binkw32.dll decodes, these
 * native BinkBuffer functions take its output to the screen.
 *
 * The original BinkBuffer code blits through DirectDraw to the primary
 * surface; here the buffer is a guest memory frame in BINKSURFACE32 (B, G, R,
 * x per pixel) that BinkBufferBlit hands to the renderer, shown 4:3 in the
 * window. HBINKBUFFER fields gta2 reads after BinkBufferLock: +0x10 surface
 * type (the CopyToBuffer flags), +0x14 Buffer, +0x18 BufferPitch.
 */
#include "gta2_render.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#define BINKSURFACE32 1u   /* this Bink build (measured): 0 = 24-bit, 1 = 32-bit B,G,R,x, 2..5 = 16-bit */

void gfx_show_frame_bgrx(const uint8_t *px, uint32_t w, uint32_t h, uint32_t pitch);

typedef struct bbuf { uint32_t va, w, h, pixels; } bbuf;
static bbuf g_bb[4];

static bbuf *find(uint32_t va)
{
    for (int i = 0; i < 4; i++)
        if (g_bb[i].va && g_bb[i].va == va)
            return &g_bb[i];
    return NULL;
}

/* HBINKBUFFER BinkBufferOpen(HWND, U32 width, U32 height, U32 flags) */
WR_HANDLER(BinkBufferOpen)
{
    (void)base;
    uint32_t w = WR_ARG(1), h = WR_ARG(2);
    bbuf *b = NULL;
    for (int i = 0; i < 4; i++)
        if (!g_bb[i].va) {
            b = &g_bb[i];
            break;
        }
    if (!b || !w || !h || w > 4096 || h > 4096)
        WR_RET(0, 4);
    b->w = w;
    b->h = h;
    b->pixels = wr_galloc(w * h * 4);
    b->va = wr_galloc(0x100);
    wr_wr32(b->va + 0x00, w);
    wr_wr32(b->va + 0x04, h);
    wr_wr32(b->va + 0x08, w);
    wr_wr32(b->va + 0x0C, h);
    wr_wr32(b->va + 0x10, BINKSURFACE32);
    wr_wr32(b->va + 0x14, 0);
    wr_wr32(b->va + 0x18, w * 4);
    wr_log("BinkBufferOpen(%ux%u) -> 32-bit memory buffer", w, h);
    WR_RET(b->va, 4);
}

WR_HANDLER(BinkBufferLock)
{
    (void)base;
    bbuf *b = find(WR_ARG(0));
    if (!b)
        WR_RET(0, 1);
    wr_wr32(b->va + 0x14, b->pixels);
    WR_RET(1, 1);
}

WR_HANDLER(BinkBufferUnlock)
{
    (void)base;
    bbuf *b = find(WR_ARG(0));
    if (b)
        wr_wr32(b->va + 0x14, 0);
    WR_RET(1, 1);
}

/* void BinkBufferBlit(HBINKBUFFER, BINKRECT *rects, U32 numrects): show it */
WR_HANDLER(BinkBufferBlit)
{
    (void)base;
    bbuf *b = find(WR_ARG(0));
    if (b)
        gfx_show_frame_bgrx((const uint8_t *)wr_ptr(b->pixels), b->w, b->h, b->w * 4);
    WR_RET(0, 3);
}

WR_HANDLER(BinkBufferClose)
{
    (void)base;
    bbuf *b = find(WR_ARG(0));
    if (b) {
        wr_heap_free(b->pixels);
        wr_heap_free(b->va);
        memset(b, 0, sizeof *b);
    }
    WR_RET(0, 1);
}

WR_HANDLER(BinkBufferSetOffset) { (void)base; WR_RET(1, 3); }
WR_HANDLER(BinkBufferCheckWinPos) { (void)base; WR_RET(0, 3); }
WR_HANDLER(BinkBufferSetDDPrimary) { (void)base; WR_RET(1, 1); }

/* BinkWait (lifted, 0x10008800) returns 1 while the next frame is not due.
 * GTA 2's menu loop then returns at once and asks again - a busy loop at
 * 100 % CPU for the whole movie. Same answer, but sleep 1 ms when waiting. */
#define BINK_WAIT_VA 0x10008800u

WR_HANDLER(BinkWaitSleep)
{
    (void)base;
    uint32_t bk = WR_ARG(0);
    uint32_t r = wr_call_guest(ctx, BINK_WAIT_VA, 1, &bk);
    if (r) {
        wr_coop_run(ctx);
        struct timespec ts = { 0, 1000000L };
        nanosleep(&ts, NULL);
    }
    WR_RET(r, 1);
}

const wr_export g_bink_overrides[] = {
    { "_BinkWait@4", BinkWaitSleep },
    { "_BinkBufferOpen@16", BinkBufferOpen }, { "_BinkBufferLock@4", BinkBufferLock },
    { "_BinkBufferUnlock@4", BinkBufferUnlock }, { "_BinkBufferBlit@12", BinkBufferBlit },
    { "_BinkBufferClose@4", BinkBufferClose }, { "_BinkBufferSetOffset@12", BinkBufferSetOffset },
    { "_BinkBufferCheckWinPos@12", BinkBufferCheckWinPos }, { "_BinkBufferSetDDPrimary@4", BinkBufferSetDDPrimary },
    { NULL, NULL }
};

/* ---- Bink's background thread, cooperatively ---------------------------------------------
 * BinkOpen starts sub_10007A40(bink) when a movie has sound. One loop
 * iteration of it, as a cooperative step (winrecomp wr_coop_*):
 *   +0x240 = 2 while running; BinkClose sets 0 and waits for 3
 *   unless paused (+0x28 bit 31) or reading is done (+0x108): background
 *     read [+0x120](bink + 0x110); no pause while the read buffer is at most
 *     half full ((+0x148+1)*100/(+0x144+1) <= 50), else 5 ms; 10 ms when idle
 *   every 10 ms the sound service sub_100077A0(bink) if a track is open
 *     (+0x1D8 != -1). */
#define BINK_THREAD_START 0x10007A40u
#define BINK_SOUND_SERVICE 0x100077A0u

static uint64_t g_bink_sound_due;

static int bink_thread_step(X86Ctx *ctx, uint32_t bk)
{
    if (wr_rd32(bk + 0x240) != 2) {
        wr_wr32(bk + 0x240, 3);
        return -1;
    }
    int ms = 10;
    if (!(wr_rd32(bk + 0x28) & 0x80000000u) && !wr_rd32(bk + 0x108)) {
        uint32_t arg = bk + 0x110;
        wr_call_guest(ctx, wr_rd32(bk + 0x120), 1, &arg);
        uint32_t fill = (wr_rd32(bk + 0x148) + 1) * 100u / (wr_rd32(bk + 0x144) + 1);
        ms = fill <= 50 ? 0 : 5;
    }
    uint64_t now = wr_time_ns();
    if (now >= g_bink_sound_due) {
        g_bink_sound_due = now + 10000000ull;
        if (wr_rd32(bk + 0x1D8) != 0xFFFFFFFFu)
            wr_call_guest(ctx, BINK_SOUND_SERVICE, 1, &bk);
    }
    return ms;
}

wr_coop_step_fn gta2_thread_hook(X86Ctx *ctx, uint32_t start, uint32_t param)
{
    (void)ctx;
    if (start != BINK_THREAD_START)
        return NULL;
    wr_wr32(param + 0x240, 2);   /* what the thread does first */
    g_bink_sound_due = 0;
    return bink_thread_step;
}
