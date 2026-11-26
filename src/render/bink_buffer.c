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
    const char *st = getenv("WR_BINK_SURFACE");
    wr_wr32(b->va + 0x10, st && *st ? (uint32_t)atoi(st) : BINKSURFACE32);
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
    static int n;
    const char *dn = getenv("WR_BINK_DUMP");
    if (b && dn && ++n == atoi(dn)) {   /* raw buffer for format checks */
        char path[1300];
        snprintf(path, sizeof path, "%s/bink_raw.bin", g_wr_paths.data);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(wr_ptr(b->pixels), 1, (size_t)b->w * b->h * 4, f);
            fclose(f);
        }
    }
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

const wr_export g_bink_overrides[] = {
    { "_BinkBufferOpen@16", BinkBufferOpen }, { "_BinkBufferLock@4", BinkBufferLock },
    { "_BinkBufferUnlock@4", BinkBufferUnlock }, { "_BinkBufferBlit@12", BinkBufferBlit },
    { "_BinkBufferClose@4", BinkBufferClose }, { "_BinkBufferSetOffset@12", BinkBufferSetOffset },
    { "_BinkBufferCheckWinPos@12", BinkBufferCheckWinPos }, { "_BinkBufferSetDDPrimary@4", BinkBufferSetDDPrimary },
    { NULL, NULL }
};
