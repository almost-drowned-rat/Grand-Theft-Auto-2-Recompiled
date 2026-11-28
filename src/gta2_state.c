/*
 * GTA 2 Recompiled - the local player's state, read from guest memory
 * (docs/re/gameplay_input_pause.md §2), and the MODERN on-foot control.
 */
#include "gta2_state.h"
#include "gta2_options.h"
#include "gta2_input.h"
#include "gta2_feel.h"
#include "wr.h"
#include "render/gta2_render.h"
#include "platform/wr_platform.h"

#include <math.h>
#include <stdlib.h>

#define GAME_PTR 0x005EB4FCu

uint32_t gta2_game(void) { return wr_rd32(GAME_PTR); }

uint32_t gta2_player(void)
{
    uint32_t g = gta2_game();
    return g && wr_mem_valid(g, 0x40) ? wr_rd32(g + 0x38) : 0;
}

uint32_t gta2_player_ped(void)
{
    uint32_t p = gta2_player();
    return p && wr_mem_valid(p, 0x2D0) ? wr_rd32(p + 0x2C4) : 0;
}

int gta2_player_driving(void)
{
    uint32_t ped = gta2_player_ped();
    if (!ped || !wr_mem_valid(ped, 0x280))
        return 0;
    uint32_t car = wr_rd32(ped + 0x16C);
    if (!car || wr_rd32(ped + 0x248) == 1 || !wr_mem_valid(car, 0x90))
        return 0;
    uint32_t model = wr_rd32(car + 0x84);
    if (model == 0x3B || model == 0x3C || model == 0x3D || model == 6)
        return 0;   /* train / bus type: the game uses on-foot controls */
    return 1;
}

static int angle_of(float x, float y)   /* stick -> Ang16 (0 = screen down, CCW) */
{
    int a = (int)lroundf(atan2f(x, y) * (720.0f / 3.14159265f));
    return ((a % 1440) + 1440) % 1440;
}

static int turn_toward(uint32_t player, int cur, int target)
{
    int d = target - cur;
    if (d >= 720) d -= 1440;
    if (d < -720) d += 1440;
    const int max_turn = 96;
    if (d > max_turn) d = max_turn;
    if (d < -max_turn) d = -max_turn;
    wr_wr16(player + 8, (uint16_t)(d < 0 ? d + 1440 : d));
    return d;
}

/* Twin-stick (maintainer 2026-11-26): the right stick only turns the ped,
 * the left stick says where it walks. The game moves a ped along its heading,
 * so the movement vector of the player's ped is computed from the left
 * stick's angle instead (gta2_twin_move_angle, wrapper of sub_0041FC20). */
static void modern_on_foot_steer(uint32_t player, uint32_t ped);
static uint32_t g_twin_obj;     /* the player ped's physics object while twin-stick walks */
static int g_sprint_left;
static float g_feel_scale = 1.0f;   /* the ANALOGUE feel: this tick's walk speed (0..1); the game ignores a walk command below 1.0 */
static int g_twin_angle;

/* where the player wants to walk: the left stick, else the d-pad, else WASD (MODERN keyboard) */
static float left_push(float *sx, float *sy)
{
    int dx, dy;
    float lx, ly, a, b;
    *sx = *sy = 0;
    wr_pad_stick(sx, sy);
    if (!SCHEME_ALT() && sqrtf(*sx * *sx + *sy * *sy) < 0.3f && wr_pad_dpad(&dx, &dy)) {
        *sx = (float)dx;
        *sy = (float)dy;
    }
    if (sqrtf(*sx * *sx + *sy * *sy) < 0.3f && gta2_kbm_sticks(&lx, &ly, &a, &b) && (lx || ly)) {
        *sx = lx;
        *sy = ly;
    }
    return sqrtf(*sx * *sx + *sy * *sy);
}

static void modern_aim(uint32_t player, uint32_t ped, float rx, float ry)
{
    g_twin_obj = 0;
    uint32_t obj = wr_rd32(ped + 0x168);
    if (!obj || !wr_mem_valid(obj + 0x40, 2))
        return;
    int cur = wr_rd16(obj + 0x40) % 1440;
    turn_toward(player, cur, angle_of(rx, ry));
    float sx, sy;
    float lm = left_push(&sx, &sy);
    int ang = angle_of(sx, sy);
    if (gta2_feel_active()) {   /* the ANALOGUE feel: the speed follows the push, with momentum (a slide when it lets go) */
        float vx, vy, sp = gta2_feel_move(sx, sy, lm, g_sprint_left > 0, &vx, &vy);
        if (sp <= 0.03f) {
            wr_wr32(player + 0xC, 0);   /* aim in place */
            return;
        }
        g_feel_scale = sp;
        ang = angle_of(vx, vy);   /* it moves the way its momentum goes, not straight to the stick */
        wr_wr32(player + 0xC, wr_rd32(0x0066B0F8u));   /* the walk command is only on / off (speed: g_feel_scale) */
    } else if (lm < 0.49f) {
        wr_wr32(player + 0xC, 0);   /* aim in place */
        return;
    } else {
        wr_wr32(player + 0xC, wr_rd32(0x0066B0F8u));   /* walk (+1.0) ... */
    }
    g_twin_obj = obj;                               /* ... where the left stick points */
    g_twin_angle = ang;
    static int n;
    if (getenv("WR_MODERN_LOG") && n++ % 15 == 0)
        wr_log("modern twin: heading %d move %d pos %d,%d", cur, g_twin_angle, (int32_t)wr_rd32(ped + 0x1AC),
               (int32_t)wr_rd32(ped + 0x1B0));
}

/* For the ped movement call (sub_0041FC20 returning to 0x0049A80B, the
 * object in esi): the angle to move along, or -1 to keep the heading. */
int gta2_twin_move_angle(uint32_t obj)
{
    return g_twin_obj && obj == g_twin_obj ? g_twin_angle : -1;
}

/* MODERN on foot, after the game's own on-foot step (sub_004A5C50, thiscall
 * player): turn the ped toward the stick and walk while it is pushed.
 * Heading = u16 [[ped+0x168]+0x40], 1440 per turn, 0 = screen down,
 * growing counter-clockwise; player+8 = this tick's turn (signed, wrapped),
 * player+0xC = walk command (Q14). */
/* SPRINT (GAMEPLAY option; MODERN A, CLASSIC L3, keyboard Left Shift): hold to sprint at 1.4x, paced by a stamina bar.
 * A full bar lasts 3.5 s of running. Stamina drains only while the ped is actually walking and refills after a short
 * pause (3 s from empty standing still, 6 s if the ped keeps walking). Once it
 * hits zero the sprint stays locked until the button is released and the bar is full again. Counted in on-foot steps (one per game tick, 30.3 Hz). */
#define SPRINT_FACTOR 1.4f
#define SPRINT_HZ 30.3f
#define SPRINT_REGEN_DELAY 0.5f   /* s after the last sprint step before the bar refills */
#define SPRINT_REGEN_FULL 3.0f    /* s for an empty bar to refill while standing still */
#define SPRINT_REGEN_MOVING 0.5f  /* refill speed while the ped keeps walking, relative to standing */
#define SPRINT_REARM 1.0f         /* after running dry, the bar must be full before the sprint works again */
#define SPRINT_BAR_HOLD 1.5f      /* s the bar stays up after it is full and the ped stopped sprinting */
#define SPRINT_SECS 3.5f          /* s of running a full bar lasts */
/* g_sprint_left (declared above): > 0 while sprinting this tick; read by the speed hook and the feel */
static float g_stamina = 1.0f;           /* 0..1 */
static int g_sprint_locked, g_sprint_delay, g_bar_hold;

/* 0..1, for a HUD bar: 1 = full (always 1 for INFINITE) */
float gta2_sprint_stamina(void) { return g_stamina; }
/* 1 while the bar is locked out after running dry (a HUD can flash it) */
int gta2_sprint_exhausted(void) { return g_sprint_locked; }

static void sprint_step_stamina(uint32_t player)
{
    int b = gta2_sprint_pressed();
    if (getenv("WR_FAKE_SPRINT"))
        b = 1;
    int moving = (int32_t)wr_rd32(player + 0xC) != 0;
    if (g_sprint_locked && !b && g_stamina >= SPRINT_REARM)
        g_sprint_locked = 0;   /* released and recovered */
    int run = b && !g_sprint_locked && g_stamina > 0.0f;
    g_sprint_left = run ? 1 : 0;
    if (run && moving) {
        if (!opt_get(OPT_ENDLESS_SPRINT))   /* ENDLESS SPRINTING (cheats): the bar never drains */
            g_stamina -= 1.0f / (SPRINT_SECS * SPRINT_HZ);
        g_sprint_delay = (int)(SPRINT_REGEN_DELAY * SPRINT_HZ);
        if (g_stamina <= 0.0f) {
            g_stamina = 0.0f;
            g_sprint_locked = 1;
            g_sprint_left = 0;
        }
    } else {   /* not draining (not sprinting, or holding the button standing still) */
        if (g_sprint_delay > 0)
            g_sprint_delay--;
        else if (g_stamina < 1.0f) {
            g_stamina += (moving ? SPRINT_REGEN_MOVING : 1.0f) / (SPRINT_REGEN_FULL * SPRINT_HZ);
            if (g_stamina > 1.0f)
                g_stamina = 1.0f;
        }
    }
    if ((run && moving) || g_stamina < 1.0f || g_sprint_locked)
        g_bar_hold = (int)(SPRINT_BAR_HOLD * SPRINT_HZ);
    else if (g_bar_hold > 0)
        g_bar_hold--;
}

/* The original sprint (upstream): a 1.5 s burst at 1.4x, then 10 s before the next one; used by every scheme but ANALOGUE, and by
 * ANALOGUE with STAMINA SPRINT off. ENDLESS SPRINTING (cheats) makes it a plain hold. */
#define BURST_TICKS 45
#define BURST_REST 303
static int g_sprint_rest, g_sprint_b_prev;
static void sprint_step_burst(uint32_t player)
{
    (void)player;
    int b = gta2_sprint_pressed();
    if (getenv("WR_FAKE_SPRINT"))
        b = 1;
    if (opt_get(OPT_ENDLESS_SPRINT)) {
        g_sprint_left = b ? 1 : 0;
        g_sprint_b_prev = b;
        return;
    }
    if (g_sprint_rest > 0)
        g_sprint_rest--;
    if (b && !g_sprint_b_prev && g_sprint_rest == 0 && g_sprint_left == 0) {
        g_sprint_left = BURST_TICKS;
        g_sprint_rest = BURST_TICKS + BURST_REST;
    }
    g_sprint_b_prev = b;
    if (g_sprint_left > 0)
        g_sprint_left--;
}

/* the stamina sprint and its bar belong to the ANALOGUE scheme */
static int stamina_mode(void) { return SCHEME_ALT() && opt_get(OPT_STAMINA_SPRINT) && opt_get(OPT_SPRINT); }

static void sprint_step(uint32_t player)
{
    if (stamina_mode())
        sprint_step_stamina(player);
    else
        sprint_step_burst(player);
}

/* PUNCH (ANALOGUE; experimental). The game only has one attack. Its input step sets a flag on the ped
 * (sub_004A5010: ped+0x21C |= 0x800) and the ped's own tick acts on the player's current weapon, read through
 * sub_004A4870 (player+0x788, 0xFFFF = none = fists). While the punch button is held, the weapon is put away
 * for the length of the player ped's tick and put back right after, so the HUD, the ammo and the weapon
 * choice never see it. settings.ini: nothing to tune. Logs the first swaps ("punch: ..."). */
static uint16_t g_punch_saved;
static uint32_t g_punch_cache;   /* the ped's own copy of its weapon object (ped+0x170, see sub_00434D70) */
/* The fire functions only shoot when the weapon's cooldown byte (weapon+2) is 0 and count it down themselves, so while the
 * punch button is held the current weapon's cooldown is kept above 0: it can't fire however fast it is. Called every game tick
 * before the world step, and again before the player ped's tick. */
void gta2_punch_hold(void)
{
    if (!gta2_punch_pressed())
        return;
    uint32_t pl = gta2_player();
    if (!pl || !wr_mem_valid(pl, 0x790))
        return;
    uint16_t w = wr_rd16(pl + 0x788);
    if (w >= 0x1C)
        return;   /* none (0xFFFF) or out of range */
    uint32_t wo = wr_rd32(pl + 0x718 + 4u * w);
    if (!wo || !wr_mem_valid(wo, 0x28))
        return;
    if (wr_rd8(wo + 2) < 4)
        wr_wr8(wo + 2, 4);
}

int gta2_punch_swap(uint32_t ped)
{
    gta2_punch_hold();
    if (!gta2_punch_pressed() || ped != gta2_player_ped())
        return 0;
    uint32_t pl = gta2_player();
    if (!pl || !wr_mem_valid(pl, 0x790))
        return 0;
    uint16_t w = wr_rd16(pl + 0x788);
    if (w == 0xFFFF)
        return 0;   /* already unarmed: the plain attack punches */
    g_punch_saved = w;
    wr_wr16(pl + 0x788, 0xFFFF);
    g_punch_cache = wr_rd32(ped + 0x170);   /* the ped keeps the weapon it last read: a stale copy fires the gun once */
    wr_wr32(ped + 0x170, 0);
    static int n;
    if (n < 6) {
        n++;
        wr_log("punch: weapon %d put away for the tick (attack byte %d, flag %08x, ped copy %08x)", (int)w,
               (int)wr_rd8(pl + 0x7C), (unsigned)wr_rd32(ped + 0x21C), (unsigned)g_punch_cache);
    }
    return 1;
}

void gta2_punch_restore(uint32_t ped)
{
    if (wr_rd32(ped + 0x170) == 0)
        wr_wr32(ped + 0x170, g_punch_cache);   /* the game refreshes it from the player on its own */
    uint32_t pl = gta2_player();
    if (pl && wr_mem_valid(pl, 0x790) && wr_rd16(pl + 0x788) == 0xFFFF)   /* the game chose another meanwhile: keep that */
        wr_wr16(pl + 0x788, g_punch_saved);
}

/* SPRINT BAR: a thin bar at the bottom centre of the game screen, drawn after the HUD. Shown on foot while sprinting and
 * while the bar refills, then for a moment (SPRINT_BAR_HOLD) before it fades out; green, then yellow, then red as it empties, flashing while locked out. */
void gta2_sprint_bar_draw(void)
{
    static float shown;   /* 0..1 opacity */
    int want = opt_get(OPT_SPRINT_BAR) && stamina_mode() && gta2_player_ped() && !gta2_player_driving() &&
               g_bar_hold > 0;
    shown += want ? 0.25f : -0.06f;
    shown = shown < 0 ? 0 : shown > 1 ? 1 : shown;
    if (shown <= 0.0f)
        return;
    float sw = (float)g_gfx.w, sh = (float)g_gfx.h;
    float w = floorf(sh * 0.34f), h = fmaxf(4.0f, floorf(sh * 0.014f)), t = fmaxf(1.0f, floorf(h / 4.0f));
    float x = floorf((sw - w) * 0.5f), y = floorf(sh - sh * 0.07f);
    float s = g_stamina;
    float r = s < 0.5f ? 1.0f : (1.0f - s) * 2.0f, g = s > 0.5f ? 1.0f : s * 2.0f;   /* red - yellow - green */
    if (g_sprint_locked) {
        static unsigned n;
        r = 1.0f; g = (n++ / 6) % 2 ? 0.35f : 0.0f;
    }
    uint32_t a = (uint32_t)(shown * 255.0f);
    uint32_t col = (a << 24) | ((uint32_t)(0.25f * 255) << 16) | ((uint32_t)(g * 220) << 8) | (uint32_t)(r * 255);
    gbh_port_rect(x - t, y - t, w + 2 * t, h + 2 * t, ((a * 200 / 255) << 24) | 0x101010u);   /* frame */
    gbh_port_rect(x, y, w, h, ((a * 140 / 255) << 24) | 0x202020u);                            /* empty part */
    gbh_port_rect(x, y, w * s, h, col);
}

/* The player ped's movement speed factor (the movement call scales its
 * speed with it; a walk command above 1.0 is capped by the game). */
float gta2_sprint_factor(uint32_t obj)
{
    float f = (g_sprint_left > 0 ? SPRINT_FACTOR : 1.0f) * g_feel_scale;
    if (f == 1.0f)
        return 1.0f;
    uint32_t ped = gta2_player_ped();
    return ped && wr_mem_valid(ped, 0x170) && wr_rd32(ped + 0x168) == obj ? f : 1.0f;
}

void gta2_modern_on_foot(uint32_t player, uint32_t ped)
{
    g_twin_obj = 0;
    g_feel_scale = 1.0f;
    if (!gta2_feel_active())
        gta2_feel_move_reset();
    if (SCHEME_MODERNISH() && player && ped)
        modern_on_foot_steer(player, ped);
    if (player)
        sprint_step(player);   /* SPRINT: any scheme (gta2_sprint_pressed) */
    static int n;
    if (getenv("WR_MODERN_LOG") && player && ped && n++ % 15 == 0)
        wr_log("modern: walk %d sprint %d pos %d,%d", (int32_t)wr_rd32(player + 0xC), g_sprint_left,
               (int32_t)wr_rd32(ped + 0x1AC), (int32_t)wr_rd32(ped + 0x1B0));
}

static void modern_on_foot_steer(uint32_t player, uint32_t ped)
{
    g_twin_obj = 0;
    float rx = 0, ry = 0, klx, kly, krx, kry;
    int kbm = gta2_kbm_sticks(&klx, &kly, &krx, &kry);   /* MODERN keyboard + mouse */
    if ((!SCHEME_MODERNISH() && !kbm) || !player || !ped || !wr_mem_valid(ped, 0x180))
        return;
    wr_pad_stick_right(&rx, &ry);
    if (gta2_pan_held())   /* ANALOGUE: the right stick pans the camera meanwhile (gta2_input.c) */
        rx = ry = 0;
    if (kbm && rx * rx + ry * ry < 0.1f) {   /* MODERN keyboard: the mouse aims */
        rx = krx;
        ry = kry;
    }
    if (sqrtf(rx * rx + ry * ry) >= 0.35f) {   /* right stick: turn / aim (maintainer 2026-11-26) */
        modern_aim(player, ped, rx, ry);
        return;
    }
    g_twin_obj = 0;
    float sx, sy, mag = left_push(&sx, &sy);   /* the d-pad / WASD as a full push in 8 directions */
    if (gta2_feel_active()) {   /* the ANALOGUE feel (gta2_feel.c): analog speed, momentum, eased turn */
        uint32_t fobj = wr_rd32(ped + 0x168);
        if (!fobj || !wr_mem_valid(fobj + 0x40, 2))
            return;
        float vx, vy, fsp = gta2_feel_move(sx, sy, mag, g_sprint_left > 0, &vx, &vy);
        if (mag >= gta2_feel_get()->dead_in) {   /* the body turns to face the stick ... */
            int fd = angle_of(sx, sy) - (int)(wr_rd16(fobj + 0x40) % 1440);
            if (fd >= 720) fd -= 1440;
            if (fd < -720) fd += 1440;
            int fstep = gta2_feel_turn_step(fd, g_sprint_left > 0);
            wr_wr16(player + 8, (uint16_t)(fstep < 0 ? fstep + 1440 : fstep));
        }
        if (fsp <= 0.03f) {   /* standing (the slide has run out, or the stick is in the dead zone) */
            if (mag > 0.02f)
                wr_wr32(player + 0xC, 0);   /* nudged inside the dead zone: stand, whatever "forward" the game saw */
            return;
        }
        /* ... while it moves along its momentum: reversing slides through zero instead of walking an arc */
        g_twin_obj = fobj;
        g_twin_angle = angle_of(vx, vy);
        g_feel_scale = fsp;
        wr_wr32(player + 0xC, wr_rd32(0x0066B0F8u));   /* on / off; the speed is g_feel_scale */
        return;
    }
    if (mag < 0.3f)
        return;   /* centred: nothing to steer */
    uint32_t obj = wr_rd32(ped + 0x168);
    if (!obj || !wr_mem_valid(obj + 0x40, 2))
        return;
    int target = (int)lroundf(atan2f(sx, sy) * (720.0f / 3.14159265f));
    target = ((target % 1440) + 1440) % 1440;
    int cur = wr_rd16(obj + 0x40) % 1440;
    int d = target - cur;
    if (d >= 720) d -= 1440;
    if (d < -720) d += 1440;
    const int max_turn = 96;   /* 24 degrees per tick: a quick, smooth snap */
    if (d > max_turn) d = max_turn;
    if (d < -max_turn) d = -max_turn;
    wr_wr16(player + 8, (uint16_t)(d < 0 ? d + 1440 : d));
    if (mag >= 0.49f)
        wr_wr32(player + 0xC, wr_rd32(0x0066B0F8u));   /* walk forward (+1.0) */
    static int log = -1, n;
    if (log < 0)
        log = getenv("WR_MODERN_LOG") != NULL;
    if (log && n++ % 15 == 0)
        wr_log("modern: heading %d target %d pos %d,%d", cur, target, (int32_t)wr_rd32(ped + 0x1AC),
               (int32_t)wr_rd32(ped + 0x1B0));
}

/* ---- PAN (ANALOGUE): a snappier look-around and the four arrows --------------------------------------------------------- */

/* The game's look-around (Special 2) moves the camera's offsets (cam = player+0x90: x +0x48, y +0x4C, fixed-point) with an
 * acceleration and a speed limit, so it lags behind the stick and drifts on. With ANALOGUE the offsets are instead set after each
 * tick straight from the stick: offset = push * the game's own limit ([0x5E3CD0] x, [0x5E3E74] y), so the view follows the stick
 * with no delay and no drift. Which sign the game uses for each axis is learned from its own first movement while the stick is
 * pushed (the game still sees the four direction presses); until then the game's pan is left alone. If the limits do not look
 * like fixed-point integers nothing is changed (logged once). */
#define PAN_CAM_OFF   0x90u
static int32_t g_pan_old[2];
static int g_pan_snap;

void gta2_pan_pre(void)
{
    g_pan_snap = 0;
    if (!gta2_pan_held())
        return;
    uint32_t pl = gta2_player();
    if (!pl || !wr_mem_valid(pl + PAN_CAM_OFF, 0x60))
        return;
    g_pan_old[0] = (int32_t)wr_rd32(pl + PAN_CAM_OFF + 0x48);
    g_pan_old[1] = (int32_t)wr_rd32(pl + PAN_CAM_OFF + 0x4C);
    g_pan_snap = 1;
}

void gta2_pan_post(void)
{
    if (!g_pan_snap)
        return;
    g_pan_snap = 0;
    uint32_t pl = gta2_player();
    if (!pl || !gta2_pan_held())
        return;
    int32_t lim[2] = { (int32_t)wr_rd32(0x005E3CD0u), (int32_t)wr_rd32(0x005E3E74u) };
    static int warned;
    if (lim[0] <= 0 || lim[1] <= 0 || lim[0] > 0x10000000 || lim[1] > 0x10000000) {
        if (!warned++)
            wr_log("pan: limits %d / %d are not fixed-point integers - direct pan off", lim[0], lim[1]);
        return;
    }
    float rx = 0, ry = 0;
    wr_pad_stick_right(&rx, &ry);
    float st[2] = { rx, -ry };   /* + = right, + = up (the stick's y is down-positive) */
    static int sign[2];          /* offset's sign for "right" / "up", 0 = not learned yet */
    static int logged;
    for (int i = 0; i < 2; i++) {
        uint32_t a = pl + PAN_CAM_OFF + (i ? 0x4Cu : 0x48u);
        int32_t now = (int32_t)wr_rd32(a), d = now - g_pan_old[i];
        if (!sign[i]) {
            if (st[i] > 0.6f || st[i] < -0.6f) {
                if (d != 0) {
                    sign[i] = ((d > 0) == (st[i] > 0)) ? 1 : -1;
                    wr_log("pan: axis %d sign %d (stick %.2f moved the offset %d -> %d, limit %d)", i, sign[i], st[i],
                           g_pan_old[i], now, lim[i]);
                }
            }
            continue;
        }
        float v = st[i];
        v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
        float dz = 0.15f;   /* inside the stick's own dead zone the view is centred */
        v = v > dz ? (v - dz) / (1.0f - dz) : v < -dz ? (v + dz) / (1.0f - dz) : 0.0f;
        int32_t target = (int32_t)(v * (float)lim[i]) * sign[i];
        if (logged++ < 4 && target != 0)
            wr_log("pan: axis %d -> %d (game %d, limit %d)", i, target, now, lim[i]);
        wr_wr32(a, (uint32_t)target);
    }
}

/* One filled triangle of 2 * n rows of rects; dir 0 up, 1 down, 2 left, 3 right; (cx, cy) its middle. */
static void pan_arrow(float cx, float cy, int dir, float s, uint32_t col)
{
    int n = 8;
    for (int i = 0; i < n; i++) {
        float len = s * (float)(i + 1) / (float)n;      /* the row grows from the tip to the base */
        float off = s * ((float)i / (float)n - 0.5f);
        float w = len, t = s / (float)n + 0.5f;
        switch (dir) {
        case 0: gbh_port_rect(cx - w * 0.5f, cy + off, w, t, col); break;
        case 1: gbh_port_rect(cx - w * 0.5f, cy - off - t, w, t, col); break;
        case 2: gbh_port_rect(cx + off, cy - w * 0.5f, t, w, col); break;
        default: gbh_port_rect(cx - off - t, cy - w * 0.5f, t, w, col); break;
        }
    }
}

void gta2_pan_arrows_draw(void)
{
    static float shown;
    shown += gta2_pan_held() ? 0.3f : -0.12f;
    shown = shown < 0 ? 0 : shown > 1 ? 1 : shown;
    if (shown <= 0.0f)
        return;
    float rx = 0, ry = 0;
    wr_pad_stick_right(&rx, &ry);
    float sw = (float)g_gfx.w, sh = (float)g_gfx.h;
    float s = floorf(sh * 0.045f), m = floorf(sh * 0.05f);
    float push[4] = { ry < 0 ? -ry : 0, ry > 0 ? ry : 0, rx < 0 ? -rx : 0, rx > 0 ? rx : 0 };   /* up down left right */
    float px[4] = { sw * 0.5f, sw * 0.5f, m + s, sw - m - s }, py[4] = { m + s, sh - m - s, sh * 0.5f, sh * 0.5f };
    for (int d = 0; d < 4; d++) {
        float p = push[d] > 1.0f ? 1.0f : push[d];
        uint32_t a = (uint32_t)(shown * (110.0f + 145.0f * p));
        uint32_t c = (uint32_t)(200 + 55 * p);
        pan_arrow(px[d] + 2, py[d] + 2, d, s, ((a / 2) << 24) | 0x000000u);   /* shadow */
        pan_arrow(px[d], py[d], d, s, (a << 24) | (c << 16) | (c << 8) | c);
    }
}
