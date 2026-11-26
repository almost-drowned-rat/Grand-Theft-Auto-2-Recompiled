/*
 * GTA 2 Recompiled - native video (Vid_*, replaces Dmavideo.dll) and
 * renderer (gbh_*, replaces d3ddll.dll) on the winrecomp Vulkan 1.1 backend.
 * Specs: docs/re/vid_api.md, docs/re/gbh_api.md.
 */
#ifndef GTA2_RENDER_H
#define GTA2_RENDER_H

#include "wr.h"
#include "vk/wr_vk.h"

/* VidState (0x4C4 bytes, guest memory; gta2 keeps it at 0x00673D20) */
#define VS_FLAGS     0x004
#define VS_RES_W     0x008
#define VS_RES_H     0x00C
#define VS_RES_BPP   0x010
#define VS_CURDEV    0x034
#define VS_BPP       0x038
#define VS_MODE      0x040
#define VS_WIDTH     0x048
#define VS_HEIGHT    0x04C
#define VS_SURFACE   0x050
#define VS_PITCH     0x054
#define VS_RSHIFT    0x058
#define VS_RBITS     0x05C
#define VS_GSHIFT    0x060
#define VS_GBITS     0x064
#define VS_BSHIFT    0x068
#define VS_BBITS     0x06C
#define VS_APPINST   0x078
#define VS_HMODULE   0x07C
#define VS_FULLSCR   0x080
#define VS_SLOTTBL   0x084
#define VS_HWND      0x4C0
#define VS_SIZE      0x4C4

#define VSF_LOCKED   0x001u
#define VSF_WRITES   0x002u

typedef struct gta2_gfx {
    uint32_t state;            /* guest VidState */
    uint32_t w, h;             /* the game's screen (logical pixels) */
    uint32_t cpu_buf;          /* guest 16-bpp back buffer for GetSurface (w*h*2) */
    float gamma;
    int clear_pending;         /* Vid_ClearScreen since the last flip */
    float clear_rgba[4];
    /* Vulkan */
    int vk;                    /* backend up */
    wr_vk_target *target;
    uint32_t tw, th;           /* target size */
    VkCommandBuffer cmd;       /* current frame, or NULL */
    int in_target;
    VkSampler nearest, linear;
    uint64_t frames;
    int shader_on;             /* a slang chain is in place */
} gta2_gfx;

extern gta2_gfx g_gfx;

/* Frame helpers used by both Vid_ and gbh_ */
VkCommandBuffer gfx_begin_drawing(void);   /* opens frame + target pass on demand */
void gfx_flip(void);
void gfx_set_mode(uint32_t w, uint32_t h);
void gfx_shutdown(void);
/* Record the renderer's pending batch (before a pass ends or a clear). */
void gbh_flush(void);
extern float g_port_quad_scale;   /* 1 = normal; set around one sprite's draw */
extern float g_port_quad_tint, g_port_quad_alpha;   /* colour factor and opacity, 1 = normal */
void gbh_port_image(const uint32_t *rgba, uint32_t iw, uint32_t ih, float x, float y, float w, float h);
void gbh_port_rect(float x, float y, float w, float h, uint32_t rgba);   /* rgba: R in the low byte */

void gta2_register_video(void);
void gta2_register_renderer(void);

#endif
