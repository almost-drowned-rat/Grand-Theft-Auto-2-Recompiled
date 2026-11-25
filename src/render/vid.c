/*
 * GTA 2 Recompiled - native Dmavideo.dll (Vid_*), see docs/re/vid_api.md.
 *
 * The game keeps the VidState pointer and reads a few fields itself
 * (+0x40 mode, +0x48/+0x4C size, +0x50/+0x54 CPU surface, +0x58..+0x6C pixel
 * format, +0x134 for Bink). The state lives in guest memory with those
 * fields kept exactly as the original sets them; DirectDraw objects are not
 * needed (the renderer is native too), so their slots stay 0.
 *
 * Unlike the original, Vid_SetMode/Vid_CloseScreen do the renderer side
 * themselves: the original d3ddll patched gta2's slots to get that; here
 * there is nothing to patch.
 */
#include "gta2_render.h"
#include "platform/wr_platform.h"
#include "post/shader_pass.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int w32_window_client(uint32_t hwnd, int *w, int *h);

gta2_gfx g_gfx;

/* modes the game asked for with Vid_CheckMode; id = index + 1 */
static struct { uint32_t w, h; } g_modes[32];
static int g_nmodes;
static uint32_t g_device;       /* the one VidDevice record */

/* ---- frame helpers ------------------------------------------------------------------------ */

static void ensure_vk(void)
{
    if (g_gfx.vk || wr_platform_headless())
        return;
    SDL_Window *win = wr_main_window();
    if (!win)
        return;
    const char *vs = getenv("WR_VSYNC");
    if (!wr_vk_init(win, !(vs && *vs == '0')))
        return;
    const char *preset = getenv("WR_SHADER_PRESET");
    if (preset && *preset)
        sp_set_preset(preset);
    g_gfx.nearest = wr_vk_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_gfx.linear = wr_vk_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    g_gfx.vk = 1;
}

/* Render target = the game's screen times an integer scale that fills the
 * window (RENDER RESOLUTION "native"; WR_RENDER_SCALE overrides). */
static void ensure_target(void)
{
    if (!g_gfx.vk || !g_gfx.w || !g_gfx.h)
        return;
    int ww, wh;
    wr_main_window_size(&ww, &wh);
    uint32_t scale = 1;
    const char *e = getenv("WR_RENDER_SCALE");
    if (e && *e) {
        scale = (uint32_t)atoi(e);
    } else {
        /* the smallest integer scale whose image covers the fitted rectangle:
         * the present pass then only ever scales down (sharp) */
        double fit = (double)ww / g_gfx.w < (double)wh / g_gfx.h ? (double)ww / g_gfx.w : (double)wh / g_gfx.h;
        scale = (uint32_t)fit;
        if ((double)scale < fit - 0.01)
            scale++;
    }
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    uint32_t tw = g_gfx.w * scale, th = g_gfx.h * scale;
    if (g_gfx.target && g_gfx.tw == tw && g_gfx.th == th)
        return;
    if (g_gfx.target)
        wr_vk_target_destroy(g_gfx.target);
    g_gfx.target = wr_vk_target_create(tw, th, g_gfx.linear);
    g_gfx.tw = tw;
    g_gfx.th = th;
}

VkCommandBuffer gfx_begin_drawing(void)
{
    ensure_vk();
    if (!g_gfx.vk)
        return NULL;
    if (!g_gfx.cmd) {
        ensure_target();
        if (!g_gfx.target)
            return NULL;
        g_gfx.cmd = wr_vk_frame_begin();
        if (!g_gfx.cmd)
            return NULL;
    }
    if (!g_gfx.in_target) {
        wr_vk_target_begin(g_gfx.target, g_gfx.clear_pending, g_gfx.clear_rgba);
        g_gfx.clear_pending = 0;
        g_gfx.in_target = 1;
    }
    return g_gfx.cmd;
}

void gfx_flip(void)
{
    g_gfx.frames++;
    if (!g_gfx.vk)
        return;
    if (!g_gfx.cmd && !gfx_begin_drawing())
        return;
    gbh_flush();
    if (g_gfx.in_target) {
        wr_vk_target_end();
        g_gfx.in_target = 0;
    }
    /* WR_DUMP_FRAMES=N: every N-th frame's game picture to data/frame_<n>.png */
    static int dump_every = -1;
    if (dump_every < 0) {
        const char *d = getenv("WR_DUMP_FRAMES");
        dump_every = d && *d ? atoi(d) : 0;
    }
    if (dump_every > 0 && g_gfx.frames % (uint64_t)dump_every == 0) {
        char path[1300];
        snprintf(path, sizeof path, "%s/frame_%06llu.png", g_wr_paths.data, (unsigned long long)g_gfx.frames);
        wr_vk_dump_target(g_gfx.target, path);
    }
    int x, y, w, h;
    wr_vk_fit_rect(g_vk.extent.width, g_vk.extent.height, g_gfx.w, g_gfx.h, &x, &y, &w, &h);
    float black[4] = { 0, 0, 0, 1 };
    /* Slang shader chain (option-c's shader pass): game picture -> chain ->
     * swapchain image (the bars stay black), then a kept pass for overlays.
     * Without a preset: the plain scaled blit with the game's gamma. */
    static uint64_t last_ns;
    static int lx, ly, lw, lh;
    uint64_t now = wr_time_ns();
    uint32_t dt_ms = last_ns ? (uint32_t)((now - last_ns) / 1000000u) : 0;
    last_ns = now;
    int shaded = 0;
    if (g_gfx.shader_on) {
        VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.image = wr_vk_swap_image();
        vkCmdPipelineBarrier(g_gfx.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, NULL, 0, NULL, 1, &b);
        int same = x == lx && y == ly && w == lw && h == lh;
        shaded = sp_frame(g_gfx.cmd, g_gfx.frames, g_gfx.target->color, VK_FORMAT_R8G8B8A8_UNORM, g_gfx.tw, g_gfx.th,
                          wr_vk_swap_image(), g_vk.format, g_vk.extent.width, g_vk.extent.height, x, y, w, h, same,
                          dt_ms > 0 && dt_ms < 1000 ? 1000.0f / (float)dt_ms : 30.0f, dt_ms);
        lx = x; ly = y; lw = w; lh = h;
    }
    if (shaded) {
        wr_vk_present_begin(0, black);   /* keep the chain's picture; overlays go here */
    } else {
        wr_vk_present_begin(1, black);
        wr_vk_blit(g_gfx.cmd, g_gfx.target, x, y, w, h, g_gfx.gamma);
    }
    wr_vk_present_end();
    /* WR_DUMP_PRESENT=N: every N-th shown picture to data/present_<n>.png */
    static int dump_present = -1;
    if (dump_present < 0) {
        const char *d = getenv("WR_DUMP_PRESENT");
        dump_present = d && *d ? atoi(d) : 0;
    }
    if (dump_present > 0 && g_gfx.frames % (uint64_t)dump_present == 0) {
        char path[1300];
        snprintf(path, sizeof path, "%s/present_%06llu.png", g_wr_paths.data, (unsigned long long)g_gfx.frames);
        wr_vk_dump_swap(path);
    }
    wr_vk_frame_end();
    g_gfx.cmd = NULL;
    /* the chain for the next frame (built on a worker; in place at a flip) */
    if (g_gfx.vk) {
        SpDevice d = { g_vk.instance, g_vk.phys, g_vk.dev, g_vk.queue, g_vk.qfamily, WR_VK_FRAMES };
        g_gfx.shader_on = sp_tick(&d);
    }
}

/* The host window, set up once (it must not jump when the game switches
 * between its 640x480 menus and the gameplay mode): DISPLAY MODE
 * (WR_DISPLAY=windowed|borderless; the menu setting comes with M6) and the
 * ASPECT RATIO (WR_ASPECT=4:3|16:9|16:10|21:9). Windowed = 85 % of the
 * desktop at that ratio. Every game mode is fitted into it (pillar/letterbox). */
static double aspect_setting(void)
{
    const char *a = getenv("WR_ASPECT");
    if (a && !strcmp(a, "16:9")) return 16.0 / 9.0;
    if (a && !strcmp(a, "16:10")) return 16.0 / 10.0;
    if (a && !strcmp(a, "21:9")) return 64.0 / 27.0;
    return 4.0 / 3.0;
}

static void setup_window(void)
{
    static int done;
    SDL_Window *win = wr_main_window();
    if (!win || done)
        return;
    done = 1;
    const char *d = getenv("WR_DISPLAY");
    if (d && !strcmp(d, "borderless")) {
        SDL_SetWindowFullscreen(win, true);
    } else {
        int dw, dh;
        wr_display_size(&dw, &dh);
        double ar = aspect_setting();
        int h = (int)(dh * 0.85), w = (int)(h * ar + 0.5);
        if (w > (int)(dw * 0.85)) {
            w = (int)(dw * 0.85);
            h = (int)(w / ar + 0.5);
        }
        SDL_SetWindowFullscreen(win, false);
        SDL_SetWindowSize(win, w, h);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_ShowWindow(win);
    SDL_SyncWindow(win);
    if (g_gfx.vk)
        g_vk.need_recreate = 1;
}

void gfx_set_mode(uint32_t w, uint32_t h)
{
    g_gfx.w = w;
    g_gfx.h = h;
    setup_window();
    gbh_flush();
    if (g_gfx.cmd) {
        if (g_gfx.in_target) {
            wr_vk_target_end();
            g_gfx.in_target = 0;
        }
        wr_vk_frame_end();
        g_gfx.cmd = NULL;
    }
    ensure_vk();
    ensure_target();
    g_gfx.clear_pending = 1;
    memset(g_gfx.clear_rgba, 0, sizeof g_gfx.clear_rgba);
    g_gfx.clear_rgba[3] = 1.0f;
}

void gfx_shutdown(void)
{
    gbh_flush();
    if (g_gfx.cmd) {
        if (g_gfx.in_target)
            wr_vk_target_end();
        wr_vk_frame_end();
    }
    if (g_gfx.target)
        wr_vk_target_destroy(g_gfx.target);
    if (g_gfx.vk)
        wr_vk_shutdown();
    memset(&g_gfx, 0, sizeof g_gfx);
}

/* ---- Vid_ exports --------------------------------------------------------------------------- */

static uint32_t g_version;

WR_HANDLER(Vid_GetVersion)
{
    (void)base;
    if (!g_version) {
        g_version = wr_galloc(64);
        float v = 1.81f;
        memcpy(wr_ptr(g_version), &v, 4);
        wr_put_str(g_version + 4, 60, "winrecomp native video (Vulkan)");
    }
    WR_RET(g_version, 0);
}

static uint32_t g_slot_table, g_hmodule;

WR_HANDLER(Vid_InitDLL)
{
    (void)base;
    g_hmodule = WR_ARG(0);
    g_slot_table = WR_ARG(1);
    WR_RET(0, 2);
}

WR_HANDLER(Vid_Init_SYS)
{
    (void)base;
    uint32_t st = wr_galloc(VS_SIZE);
    wr_wr32(st + 0x000, 1);
    wr_wr32(st + VS_FLAGS, (WR_ARG(1) & 0x40u) | 0x220u);
    wr_wr32(st + 0x014, 2);            /* next mode id */
    wr_wr32(st + 0x018, 2);            /* next device id */
    wr_wr32(st + 0x020, 1);            /* one device */
    wr_wr32(st + VS_APPINST, WR_ARG(0));
    wr_wr32(st + VS_HMODULE, g_hmodule);
    wr_wr32(st + VS_SLOTTBL, g_slot_table);
    if (!g_device) {
        /* VidDevice: id 1 (primary), flags bit0 = can render windowed */
        g_device = wr_galloc(0x60);
        wr_wr32(g_device + 0x00, 1);
        wr_wr32(g_device + 0x04, 1);
        wr_put_str(g_device + 0x2C, 16, "display");
        wr_put_str(g_device + 0x3C, 32, "Vulkan");
        wr_wr32(g_device + 0x08, g_device + 0x2C);
        wr_wr32(g_device + 0x0C, g_device + 0x3C);
    }
    wr_wr32(st + 0x02C, g_device);
    wr_wr32(st + 0x030, g_device);
    g_gfx.state = st;
    g_gfx.gamma = 1.0f;
    wr_log("Vid_Init_SYS -> state 0x%08X", st);
    WR_RET(st, 2);
}

WR_HANDLER(Vid_FindDevice)
{
    (void)base;
    WR_RET(WR_ARG(1) == 1 ? g_device : 0, 2);
}

WR_HANDLER(Vid_SetDevice)
{
    (void)base;
    uint32_t st = WR_ARG(0), id = WR_ARG(1);
    if (st)
        wr_wr32(st + VS_CURDEV, id == 1 ? 1u : 0u);
    WR_RET(id == 0 || id == 1 ? 0 : 1, 2);
}

static uint32_t mode_id(uint32_t w, uint32_t h)
{
    for (int i = 0; i < g_nmodes; i++)
        if (g_modes[i].w == w && g_modes[i].h == h)
            return (uint32_t)i + 1;
    if (g_nmodes == 32)
        return 0;
    g_modes[g_nmodes].w = w;
    g_modes[g_nmodes].h = h;
    return (uint32_t)++g_nmodes;
}

WR_HANDLER(Vid_CheckMode)
{
    (void)base;
    uint32_t st = WR_ARG(0), w = WR_ARG(1), h = WR_ARG(2), bpp = WR_ARG(3);
    uint32_t id = 0;
    if (bpp == 16 && w >= 320 && h >= 200 && w <= 7680 && h <= 4320)
        id = mode_id(w, h);
    if (st) {
        wr_wr32(st + VS_RES_W, id ? w : 0);
        wr_wr32(st + VS_RES_H, id ? h : 0);
        wr_wr32(st + VS_RES_BPP, id ? bpp : 0);
    }
    wr_log("Vid_CheckMode(%ux%u, %u bpp) -> %u", w, h, bpp, id);
    WR_RET(id, 4);
}

WR_HANDLER(Vid_FindMode)
{
    (void)base;
    /* not used by gta2; mode records are not kept */
    WR_RET(0, 2);
}

WR_HANDLER(Vid_FindFirstMode) { (void)base; WR_RET(0, 2); }
WR_HANDLER(Vid_FindNextMode) { (void)base; WR_RET(0, 1); }

static void close_screen(uint32_t st)
{
    if (!st)
        return;
    wr_wr32(st + VS_MODE, 0);
}

WR_HANDLER(Vid_CloseScreen)
{
    (void)base;
    close_screen(WR_ARG(0));
    WR_RET(0, 1);
}

WR_HANDLER(Vid_SetMode)
{
    (void)base;
    uint32_t st = WR_ARG(0), hwnd = WR_ARG(1), id = WR_ARG(2);
    if (!st)
        WR_RET(1, 3);
    uint32_t w, h;
    if (id == 0xFFFFFFFEu) {
        int cw = 640, ch = 480;
        w32_window_client(hwnd, &cw, &ch);
        w = (uint32_t)cw;
        h = (uint32_t)ch;
    } else if (id >= 1 && (int)id <= g_nmodes) {
        w = g_modes[id - 1].w;
        h = g_modes[id - 1].h;
    } else {
        wr_log("Vid_SetMode: unknown mode id %d", (int)id);
        WR_RET(1, 3);
    }
    wr_wr32(st + VS_HWND, hwnd);
    wr_wr32(st + VS_FULLSCR, id != 0xFFFFFFFEu);
    wr_wr32(st + VS_CURDEV, 1);
    wr_wr32(st + VS_BPP, 16);
    wr_wr32(st + VS_MODE, id);
    wr_wr32(st + VS_WIDTH, w);
    wr_wr32(st + VS_HEIGHT, h);
    /* 5-6-5 */
    wr_wr32(st + VS_RSHIFT, 11); wr_wr32(st + VS_RBITS, 5);
    wr_wr32(st + VS_GSHIFT, 5);  wr_wr32(st + VS_GBITS, 6);
    wr_wr32(st + VS_BSHIFT, 0);  wr_wr32(st + VS_BBITS, 5);
    wr_wr32(st + 0x070, 0);      wr_wr32(st + 0x074, 0);
    uint32_t flags = wr_rd32(st + VS_FLAGS) & ~(VSF_LOCKED | VSF_WRITES);
    wr_wr32(st + VS_FLAGS, flags | 0x40000000u);
    wr_wr32(st + VS_SURFACE, 0);
    if (g_gfx.cpu_buf)
        wr_heap_free(g_gfx.cpu_buf);
    g_gfx.cpu_buf = wr_galloc(w * h * 2u);
    wr_log("Vid_SetMode(%s %ux%u)", id == 0xFFFFFFFEu ? "windowed" : "fullscreen", w, h);
    gfx_set_mode(w, h);
    WR_RET(0, 3);
}

WR_HANDLER(Vid_FlipBuffers)
{
    (void)base;
    gfx_flip();
    WR_RET(0, 1);
}

static int lock_writes(uint32_t st)
{
    uint32_t f = wr_rd32(st + VS_FLAGS);
    if (f & VSF_WRITES)
        return 1;
    wr_wr32(st + VS_FLAGS, f | VSF_LOCKED | VSF_WRITES);
    wr_wr32(st + VS_SURFACE, g_gfx.cpu_buf);
    wr_wr32(st + VS_PITCH, wr_rd32(st + VS_WIDTH) * 2u);
    return 0;
}

static int unlock_writes(uint32_t st)
{
    uint32_t f = wr_rd32(st + VS_FLAGS);
    int was = (f & VSF_WRITES) != 0;
    wr_wr32(st + VS_FLAGS, f & ~(VSF_LOCKED | VSF_WRITES));
    wr_wr32(st + VS_SURFACE, 0);
    return was ? 0 : 1;
}

WR_HANDLER(Vid_GrabSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    if (st)
        wr_wr32(st + VS_FLAGS, wr_rd32(st + VS_FLAGS) | VSF_LOCKED);
    WR_RET(0, 1);
}

WR_HANDLER(Vid_ReleaseSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    if (st)
        wr_wr32(st + VS_FLAGS, wr_rd32(st + VS_FLAGS) & ~VSF_LOCKED);
    WR_RET(0, 1);
}

WR_HANDLER(Vid_EnableWrites)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    uint32_t f = st ? wr_rd32(st + VS_FLAGS) : 0;
    if (!st || !(f & VSF_LOCKED) || (f & VSF_WRITES))
        WR_RET(1, 1);
    WR_RET(lock_writes(st), 1);
}

WR_HANDLER(Vid_DisableWrites)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    uint32_t f = st ? wr_rd32(st + VS_FLAGS) : 0;
    if (!st || !(f & VSF_WRITES))
        WR_RET(1, 1);
    wr_wr32(st + VS_FLAGS, f & ~VSF_WRITES);
    wr_wr32(st + VS_SURFACE, 0);
    WR_RET(0, 1);
}

WR_HANDLER(Vid_GetSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    if (!st || !g_gfx.cpu_buf)
        WR_RET(1, 1);
    WR_RET(lock_writes(st), 1);
}

WR_HANDLER(Vid_FreeSurface)
{
    (void)base;
    uint32_t st = WR_ARG(0);
    WR_RET(st ? (uint32_t)unlock_writes(st) : 1u, 1);
}

WR_HANDLER(Vid_ClearScreen)
{
    (void)base;
    uint32_t r = WR_ARG(1) & 0xFF, g = WR_ARG(2) & 0xFF, b = WR_ARG(3) & 0xFF;
    g_gfx.clear_rgba[0] = (float)r / 255.0f;
    g_gfx.clear_rgba[1] = (float)g / 255.0f;
    g_gfx.clear_rgba[2] = (float)b / 255.0f;
    g_gfx.clear_rgba[3] = 1.0f;
    /* gta2 always clears the whole screen right after a flip: the next
     * frame's target pass starts with this clear. Inside a pass: clear now. */
    if (g_gfx.in_target && g_gfx.cmd) {
        gbh_flush();
        VkClearAttachment ca = { VK_IMAGE_ASPECT_COLOR_BIT, 0, { { { 0 } } } };
        memcpy(ca.clearValue.color.float32, g_gfx.clear_rgba, 16);
        VkClearRect cr = { { { 0, 0 }, { g_gfx.tw, g_gfx.th } }, 0, 1 };
        vkCmdClearAttachments(g_gfx.cmd, 1, &ca, 1, &cr);
    } else {
        g_gfx.clear_pending = 1;
    }
    if (g_gfx.cpu_buf && g_gfx.w)
        memset(wr_ptr(g_gfx.cpu_buf), 0, g_gfx.w * g_gfx.h * 2u);
    WR_RET(0, 8);
}

WR_HANDLER(Vid_SetGamma)
{
    (void)base;
    float g;
    uint32_t bits = WR_ARG(1);
    memcpy(&g, &bits, 4);
    if (g > 0.05f && g < 10.0f)
        g_gfx.gamma = g;
    WR_RET(0, 4);
}

WR_HANDLER(Vid_ShutDown_SYS)
{
    (void)base;
    close_screen(WR_ARG(0));
    gfx_shutdown();
    WR_RET(0, 1);
}

WR_HANDLER(Vid_WindowProc) { (void)base; WR_RET(0, 5); }

#define E(n) { #n, n }
static const wr_export k_vid_exports[] = {
    E(Vid_CheckMode), E(Vid_ClearScreen), E(Vid_CloseScreen), E(Vid_DisableWrites), E(Vid_EnableWrites),
    E(Vid_FindDevice), E(Vid_FindFirstMode), E(Vid_FindMode), E(Vid_FindNextMode), E(Vid_FlipBuffers),
    E(Vid_FreeSurface), E(Vid_GetSurface), E(Vid_GetVersion), E(Vid_GrabSurface), E(Vid_InitDLL),
    E(Vid_Init_SYS), E(Vid_ReleaseSurface), E(Vid_SetDevice), E(Vid_SetGamma), E(Vid_SetMode),
    E(Vid_ShutDown_SYS), E(Vid_WindowProc),
    { NULL, NULL }
};
#undef E

static wr_module k_vid_module = { "dmavideo.dll", k_vid_exports, 0 };

void gta2_register_video(void) { wr_module_register(&k_vid_module); }
