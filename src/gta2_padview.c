/*
 * GTA 2 recomp - CONTROLLER LAYOUT (CONTROLS page, maintainer 2026-11-26): a
 * plain gamepad drawn here (no maker's artwork), with leader lines from each
 * control to its label. The picture is made once (640x480 RGBA, the game
 * screen's units); the labels are drawn live by the menu (gta2_menu.c) from
 * gta2_pad_function, so they follow the scheme and ON FOOT / IN A CAR.
 */
#include "gta2_padview.h"

#include <math.h>
#include <stdlib.h>

#define W 640
#define H 480

/* label column edges: left labels end at PV_LX, right ones start at PV_RX */
const pv_item g_pv_items[PV_N] = {
    { PV_LT, 236, 152, 0, 104 },  { PV_LB, 226, 176, 0, 132 },  { PV_LSTICK, 240, 232, 0, 178 },
    { PV_L3, 240, 244, 0, 206 },  { PV_DPAD, 274, 276, 0, 258 }, { PV_SELECT, 300, 232, 0, 298 },
    { PV_RT, 404, 152, 1, 104 },  { PV_RB, 414, 176, 1, 132 },  { PV_Y, 400, 213, 1, 168 },
    { PV_B, 421, 234, 1, 196 },   { PV_A, 400, 255, 1, 224 },   { PV_X, 379, 234, 1, 252 },
    { PV_RSTICK, 366, 274, 1, 286 }, { PV_R3, 366, 286, 1, 314 }, { PV_START, 340, 232, 1, 342 },
};

static uint32_t *g_px;

static void blend(int x, int y, int r, int g, int b, float a)
{
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0)
        return;
    if (a > 1) a = 1;
    uint32_t c = g_px[y * W + x];
    int cr = c & 255, cg = c >> 8 & 255, cb = c >> 16 & 255;
    cr += (int)((float)(r - cr) * a);
    cg += (int)((float)(g - cg) * a);
    cb += (int)((float)(b - cb) * a);
    g_px[y * W + x] = (uint32_t)cr | (uint32_t)cg << 8 | (uint32_t)cb << 16 | 0xFF000000u;
}

/* signed distances (negative inside) */
static float d_circle(float x, float y, float cx, float cy, float r) { return hypotf(x - cx, y - cy) - r; }

static float d_rrect(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2 - r, hy = (y1 - y0) / 2 - r;
    float qx = fabsf(x - cx) - hx, qy = fabsf(y - cy) - hy;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return hypotf(ox, oy) + fminf(fmaxf(qx, qy), 0) - r;
}

static float d_capsule(float x, float y, float ax, float ay, float bx, float by, float r)
{
    float px = x - ax, py = y - ay, vx = bx - ax, vy = by - ay;
    float t = (px * vx + py * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(px - vx * t, py - vy * t) - r;
}

static float d_body(float x, float y)
{
    float d = d_rrect(x, y, 196, 186, 444, 290, 44);
    d = fminf(d, d_capsule(x, y, 222, 262, 204, 336, 34));
    d = fminf(d, d_capsule(x, y, 418, 262, 436, 336, 34));
    return d;
}

static float d_dpad(float x, float y)
{
    return fminf(d_rrect(x, y, 274 - 7, 276 - 20, 274 + 7, 276 + 20, 3), d_rrect(x, y, 274 - 20, 276 - 7, 274 + 20, 276 + 7, 3));
}

/* fill (d < 0) with an antialiased edge; an outline of width ow in the second colour */
static void shape(float (*d)(float, float), int r, int g, int b, int orr, int og, int ob, float ow, int x0, int y0,
                  int x1, int y1)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float v = d((float)x + 0.5f, (float)y + 0.5f);
            blend(x, y, orr, og, ob, 0.5f - v);
            if (ow > 0)
                blend(x, y, r, g, b, 0.5f - (v + ow));
        }
}

static float g_c[3];   /* circle parameters for d_circ */
static float d_circ(float x, float y) { return d_circle(x, y, g_c[0], g_c[1], g_c[2]); }
static float g_rr[5];
static float d_rr(float x, float y) { return d_rrect(x, y, g_rr[0], g_rr[1], g_rr[2], g_rr[3], g_rr[4]); }

static void circle(float cx, float cy, float rad, int r, int g, int b, int orr, int og, int ob)
{
    g_c[0] = cx; g_c[1] = cy; g_c[2] = rad;
    shape(d_circ, r, g, b, orr, og, ob, 2, (int)(cx - rad - 2), (int)(cy - rad - 2), (int)(cx + rad + 2), (int)(cy + rad + 2));
}

static void rrect(float x0, float y0, float x1, float y1, float rad, int r, int g, int b, int orr, int og, int ob)
{
    g_rr[0] = x0; g_rr[1] = y0; g_rr[2] = x1; g_rr[3] = y1; g_rr[4] = rad;
    shape(d_rr, r, g, b, orr, og, ob, 2, (int)x0 - 2, (int)y0 - 2, (int)x1 + 2, (int)y1 + 2);
}

static void line(float ax, float ay, float bx, float by, int r, int g, int b)
{
    int x0 = (int)fminf(ax, bx) - 2, x1 = (int)fmaxf(ax, bx) + 2, y0 = (int)fminf(ay, by) - 2, y1 = (int)fmaxf(ay, by) + 2;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            blend(x, y, r, g, b, 0.5f - d_capsule((float)x + 0.5f, (float)y + 0.5f, ax, ay, bx, by, 0.6f));
}

const uint32_t *gta2_pad_picture(void)
{
    if (g_px)
        return g_px;
    g_px = malloc((size_t)W * H * 4);
    if (!g_px)
        return NULL;
    for (int y = 0; y < H; y++)   /* a dark, slightly lit backdrop */
        for (int x = 0; x < W; x++) {
            float l = 1.0f - hypotf((float)(x - 320) / 420.0f, (float)(y - 240) / 300.0f);
            int v = 10 + (int)(18 * (l > 0 ? l : 0));
            g_px[y * W + x] = (uint32_t)v | (uint32_t)(v + 2) << 8 | (uint32_t)(v + 10) << 16 | 0xFF000000u;
        }
    /* triggers and bumpers behind the body */
    rrect(212, 142, 262, 166, 8, 70, 72, 84, 150, 152, 165);
    rrect(378, 142, 428, 166, 8, 70, 72, 84, 150, 152, 165);
    rrect(198, 168, 262, 184, 7, 82, 84, 96, 150, 152, 165);
    rrect(378, 168, 442, 184, 7, 82, 84, 96, 150, 152, 165);
    shape(d_body, 52, 54, 64, 150, 152, 165, 2, 150, 170, 490, 380);
    /* sticks, d-pad, small buttons, face buttons */
    circle(240, 232, 21, 30, 31, 38, 120, 122, 135);
    circle(240, 232, 13, 62, 64, 76, 120, 122, 135);
    circle(366, 274, 21, 30, 31, 38, 120, 122, 135);
    circle(366, 274, 13, 62, 64, 76, 120, 122, 135);
    shape(d_dpad, 40, 41, 50, 120, 122, 135, 2, 250, 252, 298, 300);
    rrect(292, 227, 308, 237, 5, 40, 41, 50, 120, 122, 135);
    rrect(332, 227, 348, 237, 5, 40, 41, 50, 120, 122, 135);
    circle(400, 213, 10, 150, 130, 30, 220, 200, 90);   /* Y */
    circle(421, 234, 10, 150, 40, 40, 225, 110, 110);   /* B */
    circle(400, 255, 10, 40, 130, 50, 110, 210, 120);   /* A */
    circle(379, 234, 10, 40, 80, 160, 110, 150, 230);   /* X */
    /* leader lines from the label columns to the controls, a dot on each control */
    for (int i = 0; i < PV_N; i++) {
        const pv_item *it = &g_pv_items[i];
        float lx = it->right ? PV_RX - 6 : PV_LX + 6, ly = (float)it->ly + 7;
        float ex = it->right ? (float)it->ax + 20 : (float)it->ax - 20;
        if (it->id == PV_A || it->id == PV_B || it->id == PV_X || it->id == PV_Y)
            ex = (float)it->ax;
        line(lx, ly, ex, ly, 200, 190, 120);
        line(ex, ly, (float)it->ax, (float)it->ay, 200, 190, 120);
        circle((float)it->ax, (float)it->ay, 2.5f, 240, 225, 140, 240, 225, 140);
    }
    return g_px;
}
