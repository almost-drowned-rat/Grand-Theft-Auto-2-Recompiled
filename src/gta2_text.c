/*
 * GTA 2 recomp - text in languages the game's sprite fonts cannot draw (Polish, Russian, Japanese,
 * Chinese; translations milestone, maintainer 2026-11-26). docs/re/text.md has the game side.
 *
 * The language's glyphs come from data/fonts/<letter>.otf in the translations pak (tools/make_font.py:
 * Noto Sans / Noto Sans CJK Bold, cut to the characters the translation uses). At start every glyph of
 * that font is rasterised once into an atlas (fill + a 1 px outline version, like the game's shaded
 * sprite fonts). Three hooks:
 *   - the text drawer sub_004CC100: a string with characters outside the font is walked here; runs of the
 *     game's own characters are handed back to the drawer, ours are atlas quads in the same draw stream,
 *     coloured with the fill / edge colour of the game's own 'A' in that font, mode and remap (so '#'
 *     highlights and menu colours match);
 *   - the glyph width sub_004BF570: our advance, so centring, word wrap and the caret agree with drawing;
 *   - the upper-casing sub_004C21A0 for the new letters (Polish, Cyrillic; CJK has no case).
 * The GXT language letter (sub_004C2620 accepts only e f g i s j) is set in gta2_text_after_gxt_ctor.
 * Letters: p Polish, r Russian, c Chinese, n Japanese ('j' is the game's own kanji path - never used).
 */
#include "wr.h"
#include "render/gta2_render.h"
#include "gta2_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "overlay/third_party/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define REG_OPTION "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2\\Option"
#define STYLE (wr_rd32(0x00670684u))
#define RPX 44          /* raster line height in atlas pixels */
#define PAD 2           /* outline room around a glyph */
#define AW 2048
#define GLYPH_SCALE 0.9f   /* our letters a little smaller than the line: the game font sits inside its box */

typedef struct glyph {
    uint32_t cp;
    uint16_t x, y, w, h;      /* fill rect in the atlas; the outline rect is at (x + w + 1, y), same size */
    int16_t xoff, yoff;       /* from the pen / line top, raster px */
    uint16_t adv;             /* raster px */
} glyph;

static glyph *g_gl;
static int g_ngl;
static void *g_atlas;
static uint32_t *g_rgba;
static int g_ah;
static char g_letter;          /* our language's letter, 0 = none (all text is the game's own) */
static int g_in;               /* inside our own call of the drawer */
static uint32_t g_buf, g_modep; /* guest scratch: a u16 run, the mode word */

int gta2_text_letter_ours(char c) { return c == 'p' || c == 'r' || c == 'c' || c == 'n'; }
char gta2_text_letter(void) { return g_letter; }

static const glyph *find(uint32_t cp)
{
    int lo = 0, hi = g_ngl - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (g_gl[m].cp == cp)
            return &g_gl[m];
        if (g_gl[m].cp < cp)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return NULL;
}

/* ---- the game's font tables (docs/re/text.md 1.3) ---- */
static uint32_t font_base(uint16_t font)   /* first sprite of the font, -1 none */
{
    uint32_t st = STYLE;
    if (!st)
        return 0xFFFFFFFFu;
    uint32_t tab = wr_rd32(st + 0x1C);
    if (!tab || font >= wr_rd16(tab))
        return 0xFFFFFFFFu;
    return wr_rd16(tab + 2u + 2u * font);
}

static uint32_t font_count(uint16_t font)
{
    uint32_t st = STYLE, tab = st ? wr_rd32(st + 0x1C) : 0;
    if (!tab)
        return 0;
    uint16_t n = wr_rd16(tab);
    if (font >= n)
        return 0;
    uint32_t b = wr_rd16(tab + 2u + 2u * font);
    uint32_t e = font + 1 < n ? wr_rd16(tab + 4u + 2u * font) : wr_rd16(st + 2);
    return e > b ? e - b : 0;
}

static uint32_t sprite_rec(uint16_t font, uint16_t ch)   /* guest address of the 8-byte record, 0 none */
{
    uint32_t st = STYLE, b = font_base(font);
    if (b == 0xFFFFFFFFu || ch < 0x21 || (uint32_t)(ch - 0x21) >= font_count(font))
        return 0;
    uint32_t spr = b + (ch - 0x21u) + wr_rd16(wr_rd32(st + 0x14) + 0xA);
    return wr_rd32(st + 0x20) + spr * 8u;
}

static uint32_t line_h(uint16_t font)   /* the 'A' sprite height, the game's line height */
{
    uint32_t r = sprite_rec(font, 'A');
    return r ? wr_rd8(r + 5) : 16;
}

/* a character the game font cannot draw: ours (or nothing) */
static int ours(uint16_t font, uint16_t c)
{
    if (c == 0x0A || c == 0x20 || c == 0x23 || c < 0x21)
        return 0;
    return (uint32_t)(c - 0x21) >= font_count(font);
}

/* our advance in the game's unscaled pixels; the portrait-code range and unknown characters: 0 */
static uint16_t our_w(uint16_t font, uint16_t c)
{
    const glyph *g = find(c);
    if (!g)
        return 0;
    return (uint16_t)((float)g->adv * GLYPH_SCALE * (float)line_h(font) / (float)RPX + 0.5f);
}

/* ---- the atlas ---- */
static void build_atlas(const unsigned char *ttf)
{
    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        wr_log("text: the font cannot be read");
        return;
    }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    float sc = (float)RPX / (float)(asc - desc);
    int base = (int)(asc * sc + 0.5f);
    /* every character the cut font has */
    int cap = 4096;
    g_gl = calloc((size_t)cap, sizeof *g_gl);
    for (uint32_t cp = 0xA0; cp < 0x10000 && g_gl; cp++) {
        if (cp >= 0xE000 && cp < 0xF900)
            continue;
        int gi = stbtt_FindGlyphIndex(&fi, (int)cp);
        if (!gi)
            continue;
        if (g_ngl == cap) {
            glyph *n = realloc(g_gl, (size_t)(cap *= 2) * sizeof *g_gl);
            if (!n)
                break;
            g_gl = n;
        }
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetGlyphHMetrics(&fi, gi, &adv, &lsb);
        stbtt_GetGlyphBitmapBox(&fi, gi, sc, sc, &x0, &y0, &x1, &y1);
        glyph *g = &g_gl[g_ngl++];
        g->cp = cp;
        g->w = (uint16_t)(x1 - x0 + 2 * PAD);
        g->h = (uint16_t)(y1 - y0 + 2 * PAD);
        g->xoff = (int16_t)(x0 - PAD);
        g->yoff = (int16_t)(base + y0 - PAD);
        g->adv = (uint16_t)(adv * sc + 0.5f);
    }
    /* shelf packing: fill and outline side by side */
    int px = 0, py = 0, rowh = 0;
    for (int i = 0; i < g_ngl; i++) {
        glyph *g = &g_gl[i];
        if (px + 2 * g->w + 2 > AW) {
            px = 0;
            py += rowh + 1;
            rowh = 0;
        }
        g->x = (uint16_t)px;
        g->y = (uint16_t)py;
        px += 2 * g->w + 2;
        if (g->h > rowh)
            rowh = g->h;
    }
    g_ah = py + rowh + 1;
    g_rgba = calloc((size_t)AW * (size_t)g_ah, 4);
    unsigned char *m = malloc((size_t)(RPX * 4) * (RPX * 4));
    if (!g_rgba || !m) {
        free(m);
        return;
    }
    for (int i = 0; i < g_ngl; i++) {
        glyph *g = &g_gl[i];
        int iw = g->w - 2 * PAD, ih = g->h - 2 * PAD;
        if (iw <= 0 || ih <= 0 || iw > RPX * 4 || ih > RPX * 4)
            continue;
        memset(m, 0, (size_t)iw * ih);
        stbtt_MakeCodepointBitmap(&fi, m, iw, ih, iw, sc, sc, (int)g->cp);
        for (int y = 0; y < g->h; y++)
            for (int x = 0; x < g->w; x++) {
                int sx = x - PAD, sy = y - PAD;
                unsigned a = (sx >= 0 && sy >= 0 && sx < iw && sy < ih) ? m[sy * iw + sx] : 0;
                unsigned o = 0;   /* outline: the coverage grown by 1-2 px */
                for (int dy = -PAD; dy <= PAD; dy++)
                    for (int dx = -PAD; dx <= PAD; dx++) {
                        int qx = sx + dx, qy = sy + dy;
                        if (qx >= 0 && qy >= 0 && qx < iw && qy < ih && m[qy * iw + qx] > o)
                            o = m[qy * iw + qx];
                    }
                g_rgba[(size_t)(g->y + y) * AW + g->x + x] = 0x00FFFFFFu | (uint32_t)a << 24;
                g_rgba[(size_t)(g->y + y) * AW + g->x + g->w + 1 + x] = 0x00FFFFFFu | (uint32_t)o << 24;
            }
    }
    free(m);
    wr_log("text: %d glyphs, atlas %dx%d", g_ngl, AW, g_ah);
}

void gta2_text_init(void)
{
    char lang[8] = "";
    if (!wr_reg_get_str(REG_OPTION, "language", lang, sizeof lang) || !gta2_text_letter_ours(lang[0]))
        return;
    char guest[32], host[1200];
    snprintf(guest, sizeof guest, "data\\fonts\\%c.otf", lang[0]);
    if (!wr_path_resolve_read(guest, host, sizeof host)) {
        wr_log("text: no %s - language '%c' stays English", guest, lang[0]);
        return;
    }
    FILE *f = fopen(host, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *ttf = malloc((size_t)n);
    if (!ttf || fread(ttf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(ttf);
        return;
    }
    fclose(f);
    build_atlas(ttf);
    free(ttf);   /* everything needed is in the atlas and the glyph table */
    if (g_ngl)
        g_letter = lang[0];
}

/* the GXT object was made (sub_004C2620): our letter instead of the 'e' the game fell back to */
void gta2_text_after_gxt_ctor(uint32_t gxt)
{
    char lang[8] = "";
    if (gxt && wr_reg_get_str(REG_OPTION, "language", lang, sizeof lang) && gta2_text_letter_ours(lang[0]))
        wr_wr8(gxt + 0x10, (uint8_t)lang[0]);
}

/* ---- colours: the game's own 'A' of this font, mode and remap ---- */
typedef struct col { uint16_t font; uint32_t mode, remap; uint32_t fill, edge; } col;
static col g_col[64];
static int g_ncol;

static void colours(X86Ctx *ctx, uint16_t font, uint32_t mode, uint32_t remap, uint32_t *fill, uint32_t *edge)
{
    /* sampled at every draw: the game fades text in and out through the palette (the brief box), so a
     * colour kept from an earlier frame would lag; the last good one stands in while there is none */
    int k = -1;
    for (int i = 0; i < g_ncol; i++)
        if (g_col[i].font == font && g_col[i].mode == mode && g_col[i].remap == remap)
            k = i;
    *fill = 0xFFFFFFFFu;
    *edge = 0xFF000000u;
    uint32_t st = STYLE, b = font_base(font);
    if (b == 0xFFFFFFFFu || !wr_rd32(0x00671978u))
        return;
    (void)st;
    uint32_t spr = b + ('A' - 0x21u);   /* font-relative: sub_004C2AC0 adds the sprite base of type 7 itself */
    uint32_t r[6] = { ctx->r_eax, ctx->r_ecx, ctx->r_edx, ctx->r_ebx, ctx->r_esi, ctx->r_edi };
    uint32_t a[4] = { 7, spr, mode, remap };
    ctx->r_ecx = wr_rd32(0x00671978u);
    uint32_t tex = wr_call_guest(ctx, 0x004C2AC0u, 4, a);
    ctx->r_eax = r[0]; ctx->r_ecx = r[1]; ctx->r_edx = r[2]; ctx->r_ebx = r[3]; ctx->r_esi = r[4]; ctx->r_edi = r[5];
    int ok = tex && gbh_port_tex_colours(tex, fill, edge);
    static int log = -1;
    if (log < 0)
        log = getenv("GTA2_TEXT_LOG") != NULL;
    if (log)
        wr_log("text: colours font %u mode %u remap %u: tex %08X ok %d fill %08X edge %08X", font, mode, remap, tex, ok,
               *fill, *edge);
    if (!ok) {
        if (k >= 0) {
            *fill = g_col[k].fill;
            *edge = g_col[k].edge;
        }
        return;
    }
    if (k < 0 && g_ncol < 64)
        k = g_ncol++;
    if (k >= 0)
        g_col[k] = (col){ font, mode, remap, *fill, *edge };
}

/* ---- the drawer (sub_004CC100, stdcall, ret 0x24): 1 = done here ---- */
int gta2_text_draw(X86Ctx *ctx)
{
    if (!g_letter || g_in)
        return 0;
    uint32_t sp = ctx->r_esp, s = wr_rd32(sp + 4);
    uint16_t font = wr_rd16(sp + 0x10);
    if (!s)
        return 0;
    int any = 0;
    for (uint32_t p = s, n = 0; n < 4096; p += 2, n++) {
        uint16_t c = wr_rd16(p);
        if (!c)
            break;
        if (ours(font, c)) {
            any = 1;
            break;
        }
    }
    if (!any)
        return 0;
    if (!g_buf) {
        g_buf = wr_galloc(2 * 520);
        g_modep = wr_galloc(4);
        if (!g_atlas && g_rgba) {
            g_atlas = gbh_port_atlas_create(g_rgba, AW, (uint32_t)g_ah);
        }
    }
    if (!g_atlas && g_rgba)
        g_atlas = gbh_port_atlas_create(g_rgba, AW, (uint32_t)g_ah);
    int32_t x0 = (int32_t)wr_rd32(sp + 8), cx = x0, y = (int32_t)wr_rd32(sp + 0xC);
    int32_t sc = (int32_t)wr_rd32(sp + 0x14);
    uint32_t mode0 = wr_rd32(wr_rd32(sp + 0x18)), remap0 = wr_rd32(sp + 0x1C);
    uint32_t blend = wr_rd32(sp + 0x20), alpha = wr_rd8(sp + 0x24);
    uint32_t cm = mode0, cr = remap0;
    int32_t lh = (int32_t)line_h(font) * sc;   /* Q14 */
    float lhp = (float)lh / 16384.0f, f = GLYPH_SCALE * lhp / (float)RPX;
    float ydrop = (1.0f - GLYPH_SCALE) * lhp * 0.75f;   /* keep the baseline near the game font's */
    uint32_t amul = blend ? (alpha * 255u) / 31u : 255u;
    g_in = 1;
    uint32_t p = s;
    for (int guard = 0; guard < 4096; guard++) {
        uint16_t c = wr_rd16(p);
        if (!c)
            break;
        if (c == 0x0A) {
            cx = x0;
            y += lh;
            p += 2;
            continue;
        }
        if (c == 0x20) {
            uint32_t r = sprite_rec(font, 'n');
            cx += sc * (int32_t)(r ? wr_rd8(r + 4) : 8);
            p += 2;
            continue;
        }
        if (c == 0x23) {
            if (cm == mode0 && cr == remap0) {
                cm = 8;
                cr = 0;
            } else {
                cm = mode0;
                cr = remap0;
            }
            p += 2;
            continue;
        }
        if (ours(font, c)) {
            const glyph *g = find(c);
            if (g && g_atlas) {
                uint32_t fill, edge;
                colours(ctx, font, cm, cr, &fill, &edge);
                fill = (fill & 0x00FFFFFFu) | (amul << 24);
                edge = (edge & 0x00FFFFFFu) | (amul << 24);
                float gx = (float)cx / 16384.0f + g->xoff * f, gy = (float)y / 16384.0f + g->yoff * f + ydrop;
                float gw = g->w * f, gh = g->h * f;
                float iu = 1.0f / AW, iv = 1.0f / (float)g_ah;
                gbh_port_atlas_quad(g_atlas, gx, gy, gw, gh, (g->x + g->w + 1) * iu, g->y * iv,
                                    (g->x + 2 * g->w + 1) * iu, (g->y + g->h) * iv, edge, blend == 2);
                gbh_port_atlas_quad(g_atlas, gx, gy, gw, gh, g->x * iu, g->y * iv, (g->x + g->w) * iu,
                                    (g->y + g->h) * iv, fill, blend == 2);
            }
            cx += sc * (int32_t)our_w(font, c);
            p += 2;
            continue;
        }
        /* a run of the game's own characters: back to the drawer */
        int n = 0;
        int32_t adv = 0;
        while (n < 512) {
            uint16_t d = wr_rd16(p + 2u * (uint32_t)n);
            if (!d || d == 0x0A || d == 0x20 || d == 0x23 || ours(font, d))
                break;
            wr_wr16(g_buf + 2u * (uint32_t)n, d);
            uint32_t r = sprite_rec(font, d);
            adv += sc * (int32_t)(r ? wr_rd8(r + 4) : 0);
            n++;
        }
        wr_wr16(g_buf + 2u * (uint32_t)n, 0);
        wr_wr32(g_modep, cm);
        uint32_t a[9] = { g_buf, (uint32_t)cx, (uint32_t)y, font, (uint32_t)sc, g_modep, cr, blend, alpha };
        uint32_t r[6] = { ctx->r_eax, ctx->r_ecx, ctx->r_edx, ctx->r_ebx, ctx->r_esi, ctx->r_edi };
        wr_call_guest(ctx, 0x004CC100u, 9, a);
        ctx->r_eax = r[0]; ctx->r_ecx = r[1]; ctx->r_edx = r[2]; ctx->r_ebx = r[3]; ctx->r_esi = r[4]; ctx->r_edi = r[5];
        cx += adv;
        p += 2u * (uint32_t)n;
    }
    g_in = 0;
    /* the original then returns at once on an empty string and does its own stack cleanup */
    wr_wr16(g_buf + 2u * 519u, 0);
    wr_wr32(sp + 4, g_buf + 2u * 519u);
    return 0;
}

/* ---- the width (sub_004BF570, thiscall style, (u16 *font, u16 *ch), ret 8): 1 = ours, *w set ---- */
int gta2_text_width(X86Ctx *ctx, uint16_t *w)
{
    if (!g_letter)
        return 0;
    uint32_t sp = ctx->r_esp;
    uint16_t font = wr_rd16(wr_rd32(sp + 4)), c = wr_rd16(wr_rd32(sp + 8));
    if (!ours(font, c))
        return 0;
    *w = our_w(font, c);
    return 1;
}

/* ---- upper case for the new letters (sub_004C21A0, thiscall gxt, (u16 c), ret 4) ---- */
int gta2_text_upper(uint16_t c, uint16_t *out)
{
    if (!g_letter)
        return 0;
    uint16_t u = c;
    if (c >= 'a' && c <= 'z')
        u = (uint16_t)(c - 0x20);
    else if (c >= 0x430 && c <= 0x44F)
        u = (uint16_t)(c - 0x20);              /* Cyrillic а..я */
    else if (c >= 0x450 && c <= 0x45F)
        u = (uint16_t)(c - 0x50);              /* ѐ..џ */
    else if (c >= 0x97 && c <= 0xAC)
        u = (uint16_t)(c - 0x17);              /* the game font's lower accents -> upper (tools/gxt.py) */
    else if (c >= 0x100 && c <= 0x177 && (c & 1) && c != 0x131 && c != 0x138 && c != 0x149 &&
             !(c >= 0x139 && c <= 0x148))
        u = (uint16_t)(c - 1);                 /* Latin Extended-A pairs (ą ć ę ś ...) */
    else if (c >= 0x139 && c <= 0x148 && !(c & 1))
        u = (uint16_t)(c - 1);                 /* ĺ ľ ł ń ň: the odd-even shift in this block */
    else if (c >= 0x179 && c <= 0x17E && !(c & 1))
        u = (uint16_t)(c - 1);                 /* ź ż ž */
    *out = u;
    return 1;
}
