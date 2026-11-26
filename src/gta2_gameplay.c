/*
 * GTA 2 Recompiled - GAMEPLAY options (docs/re/enhancements.md §2).
 *
 * MORE BLOOD is one setting (maintainer 2026-11-26): the dead stay with
 * their blood and go together with its pale stain. Bodies: the dead stay while a player is near (they are removed by the
 * game once they have been off screen for 40..200 ticks). The ped tick
 * (sub_004454E0) gets the off-screen counter ped+0x20E reset first, so none
 * of the three removal tests fires, and the kept body does not count against
 * the ped limits ([0x5E5BEA] all, [0x5E5BF4] on screen; recounted every
 * tick). At most MAX_BODIES are kept, the oldest go first; far away the game
 * removes them as before.
 *
 * MORE BLOOD: about one pool in four is drawn bigger, and pools dry in twice
 * the original time (about 53 s instead of 27 s): the particle tick
 * sub_0048BE60 runs their life down by one per tick, the wrapper gives every
 * other step back.
 */
#include "gta2_gameplay.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "wr.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BODIES 40
#define NEAR_BLOCKS 24   /* "near": within 24 blocks of the player (about four screens) */

static uint32_t g_game_seen;

static int near_player(int32_t x, int32_t y)
{
    uint32_t pp = gta2_player_ped();
    if (!pp || !wr_mem_valid(pp, 0x1B4))
        return 0;
    int32_t dx = (int32_t)wr_rd32(pp + 0x1AC) - x, dy = (int32_t)wr_rd32(pp + 0x1B0) - y;
    int64_t r = (int64_t)NEAR_BLOCKS << 14;
    return (int64_t)dx * dx + (int64_t)dy * dy < r * r;
}

static void new_game_check(void);
static int g_npale;
static uint32_t g_life_spr[1024];

/* ---- KEEP BODIES ---------------------------------------------------------------------------- */

static uint32_t g_body[MAX_BODIES];   /* kept dead peds, oldest first */
static uint32_t g_body_t0[MAX_BODIES];   /* when each was first kept (ms) */
#define BODY_KEEP_MS 93000   /* as long as its blood lasts: ~33 s fresh + 20 s drying + 40 s pale */
static int g_nbody;

static int body_index(uint32_t ped)
{
    for (int i = 0; i < g_nbody; i++)
        if (g_body[i] == ped)
            return i;
    return -1;
}

static void body_drop(int i)
{
    memmove(&g_body[i], &g_body[i + 1], (size_t)(g_nbody - i - 1) * sizeof g_body[0]);
    memmove(&g_body_t0[i], &g_body_t0[i + 1], (size_t)(g_nbody - i - 1) * sizeof g_body_t0[0]);
    g_nbody--;
}

/* Before the game's tick of one ped: 1 = keep it this tick (the caller then
 * undoes its count), 2 = remove it now (the caller calls sub_0043E650). */
int gta2_body_before_tick(uint32_t ped)
{
    new_game_check();
    int i = body_index(ped);
    if (!opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(ped, 0x280) || wr_rd32(ped + 0x278) != 9 ||
        wr_rd32(ped + 0x234) == 2) {   /* not dead (a reused slot), or already being removed */
        if (i >= 0)
            body_drop(i);
        return 0;
    }
    if (!near_player((int32_t)wr_rd32(ped + 0x1AC), (int32_t)wr_rd32(ped + 0x1B0))) {
        if (i >= 0)
            body_drop(i);   /* far away: the game's own rules */
        return 0;
    }
    if (i < 0) {
        if (g_nbody == MAX_BODIES)
            body_drop(0);   /* the oldest is no longer kept */
        g_body_t0[g_nbody] = wr_time_ms();
        g_body[g_nbody++] = ped;
    } else if (wr_time_ms() - g_body_t0[i] > BODY_KEEP_MS) {
        body_drop(i);
        return 2;   /* its time is up: it goes now, with its pale stain (maintainer) */
    }
    wr_wr16(ped + 0x20E, 0);
    static uint32_t last;
    if (getenv("WR_GAMEPLAY_LOG") && wr_time_ms() - last > 3000) {
        last = wr_time_ms();
        wr_log("gameplay: %d bodies kept, do_blood %u", g_nbody, wr_rd8(0x005EAD51u));
    }
    return 1;
}

/* ---- MORE BLOOD ----------------------------------------------------------------------------- */


/* Before the blood pool's particle tick (sub_0048BE60, thiscall particle). */
/* sprite -> its pool's remaining life, for the drawing (a small open table) */
#define LIFE_SLOTS 1024

static uint16_t g_life[LIFE_SLOTS];

static int life_slot(uint32_t spr, int add)
{
    uint32_t h = (spr * 2654435761u) >> 22;   /* 10 bits */
    for (int i = 0; i < LIFE_SLOTS; i++) {
        int k = (int)((h + (uint32_t)i) & (LIFE_SLOTS - 1));
        if (g_life_spr[k] == spr)
            return k;
        if (!g_life_spr[k])
            return add ? (g_life_spr[k] = spr, k) : -1;
    }
    return -1;
}

/* pale stains left at the end (they never go): oldest first */
#define MAX_PALE 64
#define PALE_LIFE 2         /* held here: the pool stays as a pale stain */
#define FADE_TICKS 300      /* the last ~10 s: darker and fading */
static uint32_t g_pale[MAX_PALE];
static uint32_t g_pale_t0[MAX_PALE];   /* when each turned pale (ms) */
#define PALE_KEEP_MS 40000

void gta2_blood_before_tick(uint32_t p)
{
    if (!opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(p, 0x50) || wr_rd32(p + 0x38) != 0x27)
        return;
    uint16_t life = wr_rd16(p + 0x2C);   /* already decremented this tick */
    uint32_t spr = wr_rd32(p + 0x30);
    int pale = -1;
    for (int i = 0; i < g_npale; i++)
        if (g_pale[i] == p)
            pale = i;
    if (life == 0)
        return;   /* a pale stain pushed out by newer ones: it goes now */
    if (life <= PALE_LIFE) {
        if (pale < 0) {   /* a new pale stain; the oldest one goes when there are too many */
            if (g_npale == MAX_PALE) {
                uint32_t old = g_pale[0];
                memmove(&g_pale[0], &g_pale[1], (MAX_PALE - 1) * sizeof g_pale[0]);
                memmove(&g_pale_t0[0], &g_pale_t0[1], (MAX_PALE - 1) * sizeof g_pale_t0[0]);
                g_npale--;
                if (wr_mem_valid(old, 0x50) && wr_rd32(old + 0x38) == 0x27)
                    wr_wr16(old + 0x2C, 1);   /* its next tick frees it */
            }
            g_pale_t0[g_npale] = wr_time_ms();
            g_pale[g_npale++] = p;
        } else if (wr_time_ms() - g_pale_t0[pale] > PALE_KEEP_MS) {
            /* pale long enough: it goes (with its body, about) */
            memmove(&g_pale[pale], &g_pale[pale + 1], (size_t)(g_npale - pale - 1) * sizeof g_pale[0]);
            memmove(&g_pale_t0[pale], &g_pale_t0[pale + 1], (size_t)(g_npale - pale - 1) * sizeof g_pale_t0[0]);
            g_npale--;
            wr_wr16(p + 0x2C, 0);   /* freed by this tick (the game's test follows) */
            return;
        }
        life = PALE_LIFE + 1;   /* held: the next decrement brings it back to PALE_LIFE */
    } else if (wr_rd8(p + 0x48) & 1) {
        life++;   /* dries in twice the original time: every other decrement given back */
    }
    wr_wr16(p + 0x2C, life);
    if (spr) {
        int k = life_slot(spr, 1);
        if (k >= 0)
            g_life[k] = life;
    }
}

/* How a blood pool sprite is drawn: size, colour factor, opacity. */
void gta2_sprite_look(uint32_t spr, float *scale, float *tint, float *alpha)
{
    *scale = *tint = *alpha = 1.0f;
    if (!opt_get(OPT_MORE_BLOOD) || !wr_mem_valid(spr, 0x24))
        return;
    uint32_t st = wr_rd32(0x00665B40u);
    if (!st)
        return;
    int f = (int)wr_rd16(spr + 0x22) - (int)(uint16_t)(wr_rd16(st + 0x8CA4) + 0xBF);
    if (f < 0 || f > 5)
        return;   /* not one of the six blood pool frames */
    /* by chance (fixed per pool: from its position) the bigger pool */
    uint32_t h = wr_rd32(spr + 0x14) * 2654435761u ^ wr_rd32(spr + 0x18) * 40503u;
    h ^= h >> 15;
    if ((h & 3) == 0)
        *scale = 1.6f;
    int k = life_slot(spr, 0);
    if (k < 0)
        return;
    int life = g_life[k];
    if (life >= FADE_TICKS)
        return;
    /* drying: from fresh (1) to the pale stain it ends as (darker, 40 % opaque) */
    float t = (float)(life - PALE_LIFE) / (float)(FADE_TICKS - PALE_LIFE);
    if (t < 0) t = 0;
    *tint = 0.55f + 0.45f * t;
    *alpha = 0.40f + 0.60f * t;
}

int gta2_more_blood(void) { return opt_get(OPT_MORE_BLOOD); }

/* a new game: nothing kept from the last one */
static void new_game_check(void)
{
    uint32_t g = gta2_game();
    if (g != g_game_seen) {
        g_game_seen = g;
        g_nbody = 0;
        g_npale = 0;
        memset(g_life_spr, 0, sizeof g_life_spr);
    }
}


/* ---- PEDESTRIANS 1x..4x (docs/re/enhancements.md §1; maintainer 2026-11-26) ----------- *
 * Read at game start (the ped manager is built per game, sub_004416B0):
 *  - the three pools get extra entries appended to their free lists (the game
 *    never turns a pointer into an index): peds (200 + 136 per step, incl.
 *    traffic drivers), ped physics objects (400 + 136 per step) and the
 *    8-byte list nodes (100 + 100 per step)
 *  - the global cap u16 [[0x5E5BC0]] = 50 * K
 *  - the on-screen cap: the spawner compares the byte mgr+6 (peds on screen)
 *    with zone density / 25; it gets mgr+6 / K for that comparison
 *  - the spawner runs K times per tick (sub_004415E0)
 * The pool pop sub_00403890 returns 0 when empty (it had no test), and the
 * spawner pauses while fewer than 40 peds are free. */

#define FN_PED_CTOR   0x0043AE70u   /* thiscall ped */
#define FN_PHYS_CTOR  0x00497A60u   /* thiscall phys object */
#define PED_SIZE      0x294u
#define PHYS_SIZE     0xB4u

static uint32_t g_extra_ped, g_extra_phys, g_extra_nodes;   /* this game's extra arrays */
static int g_n_extra_ped;
static int g_ped_factor = 1;                                  /* K of this game */

int gta2_ped_factor(void) { return g_ped_factor; }

static uint32_t extend(X86Ctx *ctx, uint32_t last_link_va, uint32_t n, uint32_t size, uint32_t link_off,
                       uint32_t ctor)
{
    uint32_t arr = wr_heap_alloc(n * size, 1);
    if (!arr)
        return 0;
    memset(wr_ptr(arr), 0, (size_t)n * size);
    uint32_t ecx = ctx->r_ecx;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = arr + i * size;
        if (ctor) {
            ctx->r_ecx = e;
            wr_call_guest(ctx, ctor, 0, NULL);
        }
        wr_wr32(e + link_off, i + 1 < n ? e + size : 0);
    }
    ctx->r_ecx = ecx;
    wr_wr32(last_link_va, arr);   /* the original list's end now leads into the extras */
    return arr;
}

/* After the ped manager's constructor (sub_004416B0): a new game. */
void gta2_peds_game_start(X86Ctx *ctx, uint32_t mgr)
{
    /* the last game's extras are gone with its pools */
    if (g_extra_ped) wr_heap_free(g_extra_ped);
    if (g_extra_phys) wr_heap_free(g_extra_phys);
    if (g_extra_nodes) wr_heap_free(g_extra_nodes);
    g_extra_ped = g_extra_phys = g_extra_nodes = 0;
    g_n_extra_ped = 0;
    int k = opt_get(OPT_PEDESTRIANS) + 1;
    if (wr_rd32(0x00673E2Cu)) {
        if (k > 1)
            wr_log("pedestrians: network game - 1x");
        k = 1;
    }   /* network game: every player must run the same simulation */
    g_ped_factor = k;
    if (k == 1)
        return;
    uint32_t pool = wr_rd32(0x005E5BBCu), phys = wr_rd32(0x0066A3B4u), nodes = wr_rd32(0x005E5FBCu);
    if (!pool || !phys || !nodes || !mgr) {
        wr_log("pedestrians: %dx not applied (pools %08X %08X %08X, manager %08X)", k, pool, phys, nodes, mgr);
        return;
    }
    uint32_t np = 136u * (uint32_t)(k - 1);
    g_extra_ped = extend(ctx, pool + 0x20274u, np, PED_SIZE, 0x160u, FN_PED_CTOR);
    if (g_extra_ped)
        g_n_extra_ped = (int)np;
    g_extra_phys = extend(ctx, phys + 0x11908u, np, PHYS_SIZE, 0x78u, FN_PHYS_CTOR);
    g_extra_nodes = extend(ctx, nodes + 0x320u, 100u * (uint32_t)(k - 1), 8u, 4u, 0);
    wr_wr16(mgr, (uint16_t)(50 * k));
    wr_log("pedestrians: %dx - %u more peds, cap %d", k, np, 50 * k);
}

/* The spawner may run only with enough free peds (unchecked callers elsewhere). */
int gta2_peds_spawn_ok(void)
{
    uint32_t pool = wr_rd32(0x005E5BBCu);
    if (!pool)
        return 0;
    int total = 200 + g_n_extra_ped;
    return total - (int)wr_rd16(pool + 0x203A8u) >= 40;
}
