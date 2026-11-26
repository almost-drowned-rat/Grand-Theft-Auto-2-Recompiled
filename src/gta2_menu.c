/*
 * GTA 2 Recompiled - the OPTIONS menu inside the game's own frontend
 * (docs/re/frontend.md). GTA 2's OPTIONS line started the external
 * "GTA2 Manager"; it now opens the game's unused page 12, which the port
 * fills with its own pages:
 *
 *   OPTIONS        SOUND / RECOMP OPTIONS / CHEATS
 *   SOUND          MUSIC VOLUME, EFFECTS VOLUME (the game's own registry values)
 *   RECOMP OPTIONS RENDERING (display mode, resolution, aspect, texture
 *                  filtering) / PERFORMANCE / LANGUAGE
 *   CHEATS         the game's cheats as ON/OFF switches + UNLOCK ALL AREAS
 *
 * One game page holds them all: the port keeps the page it shows, handles
 * every key on page 12 itself (Esc there would quit the game) and draws the
 * lines with the game's text function, fonts and highlight, over the game's
 * options picture. The game's own item list holds one blank line only, so
 * its page-entry check is satisfied.
 */
#include "gta2_menu.h"
#include "gta2_options.h"
#include "wr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FE_PTR        0x005EB160u   /* [ ] = the frontend object */
#define FE_PAGE       0x11Eu        /* u16 current page */
#define FE_FONT       0x11Cu        /* u16 normal menu font */
#define FE_PAGES      0x122u        /* Page[17], 0xBCA bytes each */
#define FE_EDGE       0xC9B8u       /* left, right, up, down, enter, esc, delete */
#define FE_BG         0xEDF4u       /* u8 background index */
#define FONT_HILITE   0x0067064Cu   /* u16 highlight font */
#define FE_SOUND      0x005EAF50u   /* frontend sound this frame */
#define PAGE_SIZE     0xBCAu
#define PORT_PAGE     12u

enum { SND_UP = 1, SND_DOWN = 2, SND_VALUE = 3, SND_SELECT = 5, SND_BACK = 6 };

#define FN_GOTO_PAGE  0x004587B0u   /* thiscall fe, u16 page */
#define FN_DRAW_TEXT  0x004539F0u   /* (u16 *s, Q14 x, Q14 y, u16 font, Q14 scale) */
#define FN_TEXT_W     0x004CB0C0u   /* (u16 *s, u16 font) -> pixels */
#define FN_MUSIC_VOL  0x00410600u   /* thiscall 0x5D85A0, v */
#define FN_SFX_VOL    0x004105C0u
#define AUDIO_OBJ     0x005D85A0u
#define FN_UNLOCK_ALL 0x004A8B00u   /* thiscall [0x66B404] */
#define PROFILES_PTR  0x0066B404u

#define REG_GTA2 "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2"

/* ---- pages -------------------------------------------------------------------------------- */

enum { VP_ROOT, VP_SOUND, VP_RECOMP, VP_DISPLAY, VP_PERF, VP_LANG, VP_CHEATS, VP_COUNT };
enum { L_PAGE, L_OPT, L_VOL, L_CHEAT, L_LANG, L_ACTION, L_BACK };
enum { ACT_UNLOCK_AREAS = 1, ACT_RESTART_NOW, ACT_RESTART_LATER };
enum { VOL_MUSIC, VOL_SFX };

typedef struct { const char *label; int kind, arg; } mline;
typedef struct { const char *title; int parent; const mline *l; int n; } mpage;

/* the game's cheats (docs/re/frontend.md 9.2/9.3/10): flag bytes. The menu
 * only exists in the frontend, so every flag is set before a level starts;
 * the level-start ones (lives, respect, cash, score, jail card) are applied
 * by the game at each level start while on. */
typedef struct { const char *label; const char *key; uint32_t flag; } cheat;
static const cheat g_cheats[] = {
    { "INVULNERABLE",        "cheat_invulnerable", 0x005EAD71u },
    { "ALL WEAPONS",         "cheat_all_weapons", 0x005EAD74u },
    { "KEEP WEAPONS",        "cheat_keep_weapons", 0x005EAD9Eu },
    { "INFINITE LIVES",      "cheat_infinite_lives", 0x005EAD61u },
    { "99 LIVES",            "cheat_99_lives", 0x005EAD8Fu },
    { "NO POLICE",           "cheat_no_police", 0x005EAD5Eu },
    { "MAX RESPECT",         "cheat_max_respect", 0x005EAD8Au },
    { "200000 CASH",         "cheat_cash", 0x005EAD97u },
    { "9999999 POINTS",      "cheat_points", 0x005EADA6u },
    { "JAIL FREE CARD",      "cheat_jail_card", 0x005EAD83u },
    { "DOUBLE DAMAGE",       "cheat_double_damage", 0x005EAD65u },
    { "INVISIBILITY",        "cheat_invisibility", 0x005EAD8Eu },
    { "FREE SHOPPING",       "cheat_free_shopping", 0x005EAD84u },
    { "ENDLESS ELECTROGUN",  "cheat_electrogun", 0x005EAD70u },
    { "ENDLESS FLAMETHROWER", "cheat_flamethrower", 0x005EAD9Bu },
    { "BLOOD",               "cheat_blood", 0x005EAD51u },
    { "ELVIS PEDS",          "cheat_elvis", 0x005EAD9Fu },
    { "NAKED PEDS",          "cheat_naked", 0x005EADA0u },
};
#define N_CHEATS ((int)(sizeof g_cheats / sizeof g_cheats[0]))

static const mline l_root[] = {
    { "SOUND", L_PAGE, VP_SOUND }, { "RECOMP OPTIONS", L_PAGE, VP_RECOMP }, { "CHEATS", L_PAGE, VP_CHEATS },
    { "BACK", L_BACK, 0 },
};
static const mline l_sound[] = {
    { "MUSIC VOLUME", L_VOL, VOL_MUSIC }, { "EFFECTS VOLUME", L_VOL, VOL_SFX }, { "BACK", L_BACK, 0 },
};
static const mline l_recomp[] = {
    { "RENDERING", L_PAGE, VP_DISPLAY }, { "PERFORMANCE", L_PAGE, VP_PERF }, { "LANGUAGE", L_PAGE, VP_LANG },
    { "BACK", L_BACK, 0 },
};
static const mline l_display[] = {
    { NULL, L_OPT, OPT_DISPLAY_MODE }, { NULL, L_OPT, OPT_RENDER_RES }, { NULL, L_OPT, OPT_ASPECT },
    { NULL, L_OPT, OPT_TEXTURE_FILTER }, { "BACK", L_BACK, 0 },
};
static const mline l_perf[] = {
    { NULL, L_OPT, OPT_VSYNC }, { NULL, L_OPT, OPT_PERF_FPS }, { NULL, L_OPT, OPT_PERF_FRAMETIME },
    { NULL, L_OPT, OPT_PERF_SPEED }, { "BACK", L_BACK, 0 },
};
/* LANGUAGE applies at start: once changed, RESTART NOW / RESTART LATER */
static const mline l_lang[] = { { "LANGUAGE", L_LANG, 0 }, { "BACK", L_BACK, 0 } };
static const mline l_lang_changed[] = {
    { "LANGUAGE", L_LANG, 0 }, { "RESTART NOW", L_ACTION, ACT_RESTART_NOW },
    { "RESTART LATER", L_ACTION, ACT_RESTART_LATER },
};
static mline l_cheats[N_CHEATS + 2];

static mpage g_pages[VP_COUNT] = {
    [VP_ROOT]    = { "OPTIONS", -1, l_root, 4 },
    [VP_SOUND]   = { "SOUND", VP_ROOT, l_sound, 3 },
    [VP_RECOMP]  = { "RECOMP OPTIONS", VP_ROOT, l_recomp, 4 },
    [VP_DISPLAY] = { "RENDERING", VP_RECOMP, l_display, 5 },
    [VP_PERF]    = { "PERFORMANCE", VP_RECOMP, l_perf, 5 },
    [VP_LANG]    = { "LANGUAGE", VP_RECOMP, l_lang, 2 },
    [VP_CHEATS]  = { "CHEATS", VP_ROOT, l_cheats, 0 },
};

/* ---- state --------------------------------------------------------------------------------- */

#define VISIBLE 9   /* lines shown at once; longer pages scroll */

static int g_vp = VP_ROOT, g_cur, g_top;
static int g_cur_of[VP_COUNT];   /* the line to return to */
static uint16_t g_last_page = 0xFFFF;
static uint32_t g_strbuf;        /* guest u16 text buffer */

/* languages: the game's own gxt files (Option\language, first letter) */
static const char g_lang_code[] = "efgisj";
static const char *const g_lang_name[] = { "ENGLISH", "FRANCAIS", "DEUTSCH", "ITALIANO", "ESPANOL", "JAPANESE" };
static int g_lang_have[6], g_lang_start = -1, g_lang_sel;

static uint32_t fe(void) { return wr_rd32(FE_PTR); }
static uint32_t port_page(uint32_t f) { return f + FE_PAGES + PORT_PAGE * PAGE_SIZE; }

static uint32_t call_this(X86Ctx *ctx, uint32_t fn, uint32_t self, int n, const uint32_t *args)
{
    uint32_t ecx = ctx->r_ecx;
    ctx->r_ecx = self;
    uint32_t r = wr_call_guest(ctx, fn, n, args);
    ctx->r_ecx = ecx;
    return r;
}

/* ---- values ------------------------------------------------------------------------------- */

static uint32_t vol_get(int which)
{
    uint32_t v = 127;
    wr_reg_get_dword(REG_GTA2 "\\Sound", which == VOL_MUSIC ? "CDVol" : "SFXVol", &v);
    return v > 127 ? 127 : v;
}

static void vol_set(X86Ctx *ctx, int which, uint32_t v)
{
    wr_reg_set_dword(REG_GTA2 "\\Sound", which == VOL_MUSIC ? "CDVol" : "SFXVol", v);
    call_this(ctx, which == VOL_MUSIC ? FN_MUSIC_VOL : FN_SFX_VOL, AUDIO_OBJ, 1, &v);
}

static void lang_init(void)
{
    if (g_lang_start >= 0)
        return;
    char host[1200], name[32], cur[16] = "e";
    for (int i = 0; i < 6; i++) {
        snprintf(name, sizeof name, "data\\%c.gxt", g_lang_code[i]);
        g_lang_have[i] = wr_path_resolve_read(name, host, sizeof host);
    }
    wr_reg_get_str(REG_GTA2 "\\Option", "language", cur, sizeof cur);
    g_lang_start = 0;
    for (int i = 0; i < 6; i++)
        if (g_lang_code[i] == cur[0])
            g_lang_start = i;
    g_lang_sel = g_lang_start;
}

static void lang_step(int d)
{
    for (int k = 0; k < 6; k++) {
        g_lang_sel = (g_lang_sel + d + 6) % 6;
        if (g_lang_have[g_lang_sel])
            break;
    }
    char v[2] = { g_lang_code[g_lang_sel], 0 };
    wr_reg_set_str(REG_GTA2 "\\Option", "language", v);
}

static int cheat_on(int i) { return wr_rd8(g_cheats[i].flag) != 0; }

static void cheat_set(int i, int on)
{
    wr_wr8(g_cheats[i].flag, (uint8_t)(on != 0));
    wr_settings_set_int(g_cheats[i].key, on != 0);
}

/* "LABEL" + value for a line */
static void line_value(const mline *l, char *v, size_t cap)
{
    v[0] = 0;
    switch (l->kind) {
    case L_OPT: snprintf(v, cap, "%s", opt_choice_label(l->arg, opt_get(l->arg))); break;
    case L_VOL: snprintf(v, cap, "%u", (vol_get(l->arg) * 10 + 63) / 127); break;
    case L_CHEAT: snprintf(v, cap, "%s", cheat_on(l->arg) ? "ON" : "OFF"); break;
    case L_LANG: snprintf(v, cap, "%s", g_lang_name[g_lang_sel]); break;
    default: break;
    }
}

static const char *line_label(const mline *l) { return l->kind == L_OPT ? opt_label(l->arg) : l->label; }

/* ---- the game's page 12 --------------------------------------------------------------------- */

/* One blank, visible, selectable button: the game's page-entry check wants a
 * visible line; the port draws the real ones. */
static void prepare_page(uint32_t f)
{
    uint32_t p = port_page(f);
    memset(wr_ptr(p), 0, PAGE_SIZE);
    wr_wr16(p + 0, 1);                       /* one item */
    uint32_t it = p + 4;
    wr_wr8(it + 0, 1);                       /* button */
    wr_wr8(it + 1, 1);                       /* visible */
    wr_wr16(it + 2, 300);
    wr_wr16(it + 4, 210);
    wr_wr16(it + 6, ' ');
    wr_wr16(it + 0x6A, 0xFFFF);
    wr_wr16(it + 0x6C, 0xFFFF);
    wr_wr16(it + 0x80, PORT_PAGE);
    wr_wr16(p + 0xB8A, 280);                 /* slot 0 */
    wr_wr16(p + 0xB8C, 218);
    wr_wr8(p + 0xB8E, 1);
}

static void build_cheat_page(void)
{
    if (g_pages[VP_CHEATS].n)
        return;
    int n = 0;
    for (int i = 0; i < N_CHEATS; i++)
        l_cheats[n++] = (mline){ g_cheats[i].label, L_CHEAT, i };
    l_cheats[n++] = (mline){ "UNLOCK ALL AREAS", L_ACTION, ACT_UNLOCK_AREAS };
    l_cheats[n++] = (mline){ "BACK", L_BACK, 0 };
    g_pages[VP_CHEATS].n = n;
}

static void open_vpage(int vp)
{
    g_cur_of[g_vp] = g_cur;
    g_vp = vp;
    g_cur = 0;
    g_top = 0;
}

/* Called by the game's frontend build (every frontend construction). */
void gta2_menu_frontend_built(uint32_t f)
{
    if (!f)
        return;
    build_cheat_page();
    /* main menu OPTIONS (page 0, item 1): the Manager launch -> page 12 */
    if (wr_rd16(f + 0x228) == 0x101)
        wr_wr16(f + 0x228, PORT_PAGE);
    prepare_page(f);
}

/* After the game picked the background: the options picture on page 12. */
void gta2_menu_background(void)
{
    uint32_t f = fe();
    if (f && wr_rd16(f + FE_PAGE) == PORT_PAGE)
        wr_wr8(f + FE_BG, 0);
}

static void lang_page_update(void)
{
    int changed = g_lang_start >= 0 && g_lang_sel != g_lang_start;
    g_pages[VP_LANG].l = changed ? l_lang_changed : l_lang;
    g_pages[VP_LANG].n = changed ? 3 : 2;
}

static void go_back(X86Ctx *ctx, uint32_t f)
{
    int parent = g_pages[g_vp].parent;
    wr_wr32(FE_SOUND, SND_BACK);
    if (parent < 0) {   /* leave to the main menu, cursor on OPTIONS */
        uint32_t a = 0;
        call_this(ctx, FN_GOTO_PAGE, f, 1, &a);
        wr_wr16(f + FE_PAGES + 0xBC6, 1);
        return;
    }
    g_vp = parent;
    g_cur = g_cur_of[parent];
    g_top = g_cur >= VISIBLE ? g_cur - VISIBLE + 1 : 0;
}

static void change(X86Ctx *ctx, const mline *l, int d)
{
    switch (l->kind) {
    case L_OPT: {
        int n = opt_count(l->arg);
        opt_set(l->arg, (opt_get(l->arg) + d + n) % n);
        break;
    }
    case L_VOL: {
        int v = (int)vol_get(l->arg) + d * 13;   /* 10 steps */
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        if (v > 120 && d > 0) v = 127;
        vol_set(ctx, l->arg, (uint32_t)v);
        break;
    }
    case L_CHEAT: cheat_set(l->arg, !cheat_on(l->arg)); break;
    case L_LANG: lang_step(d); break;
    default: return;
    }
    wr_wr32(FE_SOUND, d > 0 ? SND_VALUE : SND_VALUE + 1);
}

/* The game's menu input step (sub_004597C0): on page 12 the port takes every
 * key; returns 1 when it did (the caller then runs the original with all
 * edge flags cleared). */
int gta2_menu_input(X86Ctx *ctx)
{
    uint32_t f = fe();
    if (!f)
        return 0;
    uint16_t page = wr_rd16(f + FE_PAGE);
    if (getenv("WR_MENU_LOG")) {
        static int n;
        uint32_t e = wr_rd32(f + FE_EDGE) | wr_rd32(f + FE_EDGE + 4);
        if (e && n++ < 50)
            wr_log("menu: page %u edges %08X %08X", page, wr_rd32(f + FE_EDGE), wr_rd32(f + FE_EDGE + 4));
    }
    if (page != PORT_PAGE) {
        g_last_page = page;
        return 0;
    }
    if (g_last_page != PORT_PAGE) {   /* just entered from the main menu */
        lang_init();
        g_vp = VP_ROOT;
        g_cur = g_top = 0;
        g_last_page = PORT_PAGE;
    }
    const mpage *pg = &g_pages[g_vp];
    uint8_t left = wr_rd8(f + FE_EDGE + 0), right = wr_rd8(f + FE_EDGE + 1), up = wr_rd8(f + FE_EDGE + 2),
            down = wr_rd8(f + FE_EDGE + 3), enter = wr_rd8(f + FE_EDGE + 4), esc = wr_rd8(f + FE_EDGE + 5);
    for (int i = 0; i < 7; i++)
        wr_wr8(f + FE_EDGE + i, 0);
    const mline *l = &pg->l[g_cur];
    if (esc) {
        go_back(ctx, f);
    } else if (up || down) {
        g_cur = (g_cur + (down ? 1 : pg->n - 1)) % pg->n;
        wr_wr32(FE_SOUND, down ? SND_DOWN : SND_UP);
    } else if (enter) {
        if (l->kind == L_PAGE) {
            open_vpage(l->arg);
            wr_wr32(FE_SOUND, SND_SELECT);
        } else if (l->kind == L_BACK) {
            go_back(ctx, f);
        } else if (l->kind == L_ACTION && l->arg == ACT_RESTART_NOW) {
            wr_restart();
        } else if (l->kind == L_ACTION && l->arg == ACT_RESTART_LATER) {
            go_back(ctx, f);
        } else if (l->kind == L_ACTION && l->arg == ACT_UNLOCK_AREAS) {
            call_this(ctx, FN_UNLOCK_ALL, wr_rd32(PROFILES_PTR), 0, NULL);
            wr_wr32(FE_SOUND, 9);   /* the game's "cheat accepted" sound */
        } else {
            change(ctx, l, 1);
        }
    } else if (left || right) {
        change(ctx, l, right ? 1 : -1);
    }
    lang_page_update();
    pg = &g_pages[g_vp];
    if (g_cur >= pg->n)
        g_cur = pg->n - 1;
    if (g_cur < g_top)
        g_top = g_cur;
    if (g_cur >= g_top + VISIBLE)
        g_top = g_cur - VISIBLE + 1;
    return 1;
}

/* ---- drawing -------------------------------------------------------------------------------- */

static void put_text(X86Ctx *ctx, const char *s, int x, int y, uint16_t font, uint32_t scale)
{
    if (!g_strbuf)
        g_strbuf = wr_galloc(256);
    int i = 0;
    for (; s[i] && i < 120; i++)
        wr_wr16(g_strbuf + 2 * (uint32_t)i, (uint16_t)(unsigned char)s[i]);
    wr_wr16(g_strbuf + 2 * (uint32_t)i, 0);
    uint32_t a[5] = { g_strbuf, (uint32_t)x << 14, (uint32_t)y << 14, font, scale };
    wr_call_guest(ctx, FN_DRAW_TEXT, 5, a);
}

static int text_w(X86Ctx *ctx, const char *s, uint16_t font, uint32_t scale)
{
    int i = 0;
    for (; s[i] && i < 120; i++)
        wr_wr16(g_strbuf + 2 * (uint32_t)i, (uint16_t)(unsigned char)s[i]);
    wr_wr16(g_strbuf + 2 * (uint32_t)i, 0);
    uint32_t a[2] = { g_strbuf, font };
    return (int)((uint64_t)wr_call_guest(ctx, FN_TEXT_W, 2, a) * scale >> 14);
}

#define X_LABEL 290
#define X_RIGHT 615
#define Y_FIRST 205
#define Y_STEP  20
#define SCALE   0x3000u   /* 0.75: label and value fit the right half */

/* After the game drew page 12 (its blank line): the port's lines. */
void gta2_menu_draw(X86Ctx *ctx)
{
    uint32_t f = fe();
    if (!f || wr_rd16(f + FE_PAGE) != PORT_PAGE || g_last_page != PORT_PAGE)
        return;
    if (!g_strbuf)
        g_strbuf = wr_galloc(256);
    uint16_t font = wr_rd16(f + FE_FONT), hi = wr_rd16(FONT_HILITE);
    const mpage *pg = &g_pages[g_vp];
    put_text(ctx, pg->title, X_LABEL, Y_FIRST - 38, font, 0x4000);
    char v[64];
    for (int i = g_top; i < pg->n && i < g_top + VISIBLE; i++) {
        const mline *l = &pg->l[i];
        int y = Y_FIRST + (i - g_top) * Y_STEP;
        uint16_t fo = i == g_cur ? hi : font;
        put_text(ctx, line_label(l), X_LABEL, y, fo, SCALE);
        line_value(l, v, sizeof v);
        if (v[0])
            put_text(ctx, v, X_RIGHT - text_w(ctx, v, fo, SCALE), y, fo, SCALE);
    }
    if (g_top > 0)
        put_text(ctx, "...", X_LABEL, Y_FIRST - 16, font, SCALE);
    if (g_top + VISIBLE < pg->n)
        put_text(ctx, "...", X_LABEL, Y_FIRST + VISIBLE * Y_STEP - 6, font, SCALE);
    if (g_vp == VP_LANG && g_lang_sel != g_lang_start)
        put_text(ctx, "THE NEW LANGUAGE NEEDS A RESTART", X_LABEL, Y_FIRST + 4 * Y_STEP, font, SCALE);
}

/* After the game read its Debug flags at start: the saved cheats. */
void gta2_menu_apply_cheats(void)
{
    for (int i = 0; i < N_CHEATS; i++)
        if (wr_settings_get_int(g_cheats[i].key, 0))
            wr_wr8(g_cheats[i].flag, 1);
}
