/*
 * GTA 2 Recompiled - the ANALOGUE feel presets.
 *
 * The game has no analogue input (docs/re/gameplay_input_pause.md): the MODERN scheme turns the left stick into
 * a full-speed walk once it passes a threshold, and cars only know left / none / right. These presets add
 *   - walking speed that follows how far the stick is pushed,
 *   - turning that eases toward the stick instead of a fixed rate, with less forward speed while the ped is
 *     still facing away (as most third-person games do),
 *   - car steering that follows the stick sideways: the game's steering is a three-state byte (phys+0xAD),
 *     so a partial push is pulsed on and off over the ticks (error diffusion), the average being the push.
 * the ANALOGUE feel = DEFAULT changes nothing. The presets are starting points to be tuned by feel; every number can be
 * set in saves/settings.ini as feel_<name> (see gta2_feel.h; the game reads that file once, at start-up).
 */
#include "gta2_feel.h"
#include "gta2_options.h"
#include "gta2_state.h"
#include "gta2_input.h"
#include "wr.h"
#include "platform/wr_platform.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* dead_in dead_out speed_min speed_curve turn_gain turn_max turn_min slow_turn steer_dead steer_curve steer_hold accel decel walk_accel walk_decel walk_snap */
static const gta2_feel k_preset[4] = {
    { 0.30f, 0.49f, 1.00f, 1.0f, 0, 96, 0, 0.00f, 0.00f, 1.0f, 1, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },   /* DEFAULT: unused (the old behaviour) */
    { 0.20f, 0.90f, 0.30f, 1.3f, 0.30f, 80, 12, 0.45f, 0.10f, 1.6f, 2, 0.16f, 0.20f, 1.0f, 1.0f, 1.0f },   /* SMOOTH */
    { 0.18f, 0.85f, 0.50f, 1.0f, 0.55f, 120, 20, 0.20f, 0.08f, 1.2f, 1, 0.30f, 0.40f, 1.0f, 1.0f, 1.0f },  /* SNAPPY */
    { 0.22f, 0.95f, 0.25f, 1.6f, 0.16f, 56, 8, 0.65f, 0.10f, 1.6f, 2, 0.08f, 0.11f, 1.0f, 1.0f, 1.0f },    /* HEAVY on foot, SMOOTH car steering (steer_dead / steer_curve / steer_hold) */
};

static const char *const k_name[4] = { "default", "smooth", "snappy", "heavy" };

/* v0.9.0 (maintainer 2026-11-28): the PR's ANALOGUE scheme split into its parts - analogue walking is always on (the
 * on-foot feel, MODERN's stick), the car steering is the ANALOGUE STEERING switch, the pedals are analogue on their own
 * when gas / brake are mapped to the triggers (gta2_gas_on_rt / gta2_brake_on_lt). Never in a network game. */
static int single_player(void) { return !wr_rd32(0x00673E2Cu); }
int gta2_feel_active(void) { return single_player(); }   /* the on-foot feel */
/* v0.9.1 (maintainer 2026-11-28, issue #9): ANALOGUE STEERING (ON by default) makes the left stick analogue in all four
 * directions in a car - steering where the stick steers (CUSTOM: LEFT / RIGHT on the d-pad), gas / brake where they are
 * on the stick's up / down; ANALOGUE TRIGGERS (ON by default) the pedals on R2 / L2. Each layout stays as it is. */
static int steer_active(void) { return single_player() && opt_get(OPT_ANALOGUE_STEERING) && gta2_steer_stick(); }

/* the left (1) or right (2) stick, -1..1 each */
static void stick_xy(int which, float *x, float *y)
{
    *x = *y = 0;
    if (which == 1)
        wr_pad_stick(x, y);
    else if (which == 2)
        wr_pad_stick_right(x, y);
}
static int pedals_active(void) { return single_player() && (gta2_gas_analogue() || gta2_brake_analogue()); }

static float ovr(const char *key, float v)
{
    const char *s = wr_settings_get(key, NULL);
    return s && *s ? (float)atof(s) : v;
}

const gta2_feel *gta2_feel_get(void)
{
    static gta2_feel cur;
    static int loaded = -1;
    int p = 1 + opt_get(OPT_WALK_PRESET);   /* ENHANCEMENTS > ANALOGUE > WALKING PRESET: SMOOTH / SNAPPY / HEAVY (v0.9.1) */
    if (p < 1 || p > 3)
        p = 3;
    if (p != loaded) {   /* a change of preset: its numbers, then the settings.ini overrides */
        cur = k_preset[p];
        if (p > 0) {
            cur.dead_in = ovr("feel_dead_in", cur.dead_in);
            cur.dead_out = ovr("feel_dead_out", cur.dead_out);
            cur.speed_min = ovr("feel_speed_min", cur.speed_min);
            cur.speed_curve = ovr("feel_speed_curve", cur.speed_curve);
            cur.turn_gain = ovr("feel_turn_gain", cur.turn_gain);
            cur.turn_max = ovr("feel_turn_max", cur.turn_max);
            cur.turn_min = ovr("feel_turn_min", cur.turn_min);
            cur.slow_turn = ovr("feel_slow_turn", cur.slow_turn);
            cur.accel = ovr("feel_accel", cur.accel);
            cur.decel = ovr("feel_decel", cur.decel);
            cur.walk_accel = ovr("feel_walk_accel", cur.walk_accel);
            cur.walk_decel = ovr("feel_walk_decel", cur.walk_decel);
            cur.walk_snap = ovr("feel_walk_snap", cur.walk_snap);
            if (cur.walk_accel < 0.01f) cur.walk_accel = 0.01f;
            if (cur.walk_decel < 0.01f) cur.walk_decel = 0.01f;
            if (cur.accel < 0.01f) cur.accel = 0.01f;
            if (cur.decel < 0.01f) cur.decel = 0.01f;
            if (cur.dead_out < cur.dead_in + 0.05f)
                cur.dead_out = cur.dead_in + 0.05f;
        }
        if (p != loaded)
            wr_log("feel: the ANALOGUE feel %s", k_name[p]);
        loaded = p;
    }
    /* the car steering comes from the ANALOGUE STEERING page (OPT_STEER_*), read every time so a change applies at once */
    static const float k_dead[8] = { 0.04f, 0.06f, 0.08f, 0.10f, 0.12f, 0.14f, 0.16f, 0.20f };
    static const float k_curve[8] = { 1.0f, 1.2f, 1.4f, 1.6f, 1.8f, 2.0f, 2.4f, 3.0f };
    int id = opt_get(OPT_STEER_DEAD), ic = opt_get(OPT_STEER_CURVE), ih = opt_get(OPT_STEER_HOLD);
    cur.steer_dead = k_dead[id < 0 ? 0 : id > 7 ? 7 : id];
    cur.steer_curve = k_curve[ic < 0 ? 0 : ic > 7 ? 7 : ic];
    cur.steer_hold = (float)(ih < 0 ? 1 : ih > 3 ? 4 : ih + 1);
    /* ENHANCEMENTS > ANALOGUE > WALKING DEAD ZONE (v0.9.1; was the preset's / feel_dead_in) */
    static const float k_wdead[6] = { 0.10f, 0.14f, 0.18f, 0.22f, 0.26f, 0.30f };
    int iw = opt_get(OPT_WALK_DEAD);
    cur.dead_in = k_wdead[iw < 0 ? 0 : iw > 5 ? 5 : iw];
    if (cur.dead_out < cur.dead_in + 0.05f)
        cur.dead_out = cur.dead_in + 0.05f;
    return &cur;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

float gta2_feel_walk_speed(float mag)
{
    const gta2_feel *f = gta2_feel_get();
    if (mag <= f->dead_in)
        return 0;
    float t = clamp01((mag - f->dead_in) / (f->dead_out - f->dead_in));
    return f->speed_min + (1.0f - f->speed_min) * powf(t, f->speed_curve);
}

int gta2_feel_turn_step(int d, int sprinting)
{
    const gta2_feel *f = gta2_feel_get();
    int ad = abs(d);
    if (!ad)
        return 0;
    if (!sprinting && f->walk_snap > 0.5f)
        return d;   /* walking: the body faces the stick at once (no turn animation to hide it behind) */
    int step = (int)lroundf((float)d * f->turn_gain);
    int mn = (int)f->turn_min, mx = (int)f->turn_max;
    if (abs(step) < mn)
        step = d > 0 ? mn : -mn;      /* no slow creep while far away */
    if (abs(step) > ad)
        step = d;                     /* arrive exactly, never overshoot */
    if (step > mx) step = mx;
    if (step < -mx) step = -mx;
    return step;
}

float gta2_feel_turn_slow(int d)
{
    const gta2_feel *f = gta2_feel_get();
    float e = clamp01((float)abs(d) / 720.0f);
    return 1.0f - f->slow_turn * powf(e, 0.7f);
}

/* on-foot momentum: a velocity in stick space (x right, y up as the stick reports it) that chases the stick's
 * target (direction * walk speed) by at most accel per tick when it has to speed up, decel when it has to slow
 * down, stop or turn around - so a reversal brakes through zero (a slide) instead of the ped walking an arc. */
static float g_vx, g_vy;

void gta2_feel_move_reset(void) { g_vx = g_vy = 0; }

float gta2_feel_move(float sx, float sy, float mag, int sprinting, float *vx, float *vy)
{
    const gta2_feel *f = gta2_feel_get();
    float tx = 0, ty = 0, tm = 0;
    if (mag > 0.0001f && mag >= f->dead_in) {
        tm = gta2_feel_walk_speed(mag);
        tx = sx / mag * tm;
        ty = sy / mag * tm;
    }
    float dx = tx - g_vx, dy = ty - g_vy;
    float len = sqrtf(dx * dx + dy * dy);
    float vm = sqrtf(g_vx * g_vx + g_vy * g_vy);
    float step = sprinting ? (tm > vm + 0.001f ? f->accel : f->decel)
                           : (tm > vm + 0.001f ? f->walk_accel : f->walk_decel);
    if (len <= step) {
        g_vx = tx;
        g_vy = ty;
    } else {
        g_vx += dx / len * step;
        g_vy += dy / len * step;
    }
    float sp = sqrtf(g_vx * g_vx + g_vy * g_vy);
    if (sp < 0.04f && tm == 0) {   /* the slide has run out */
        g_vx = g_vy = 0;
        sp = 0;
    }
    if (vx) *vx = g_vx;
    if (vy) *vy = g_vy;
    return sp > 1.0f ? 1.0f : sp;
}

/* ---- cars -------------------------------------------------------------------------------- */

/* the stick's sideways push -> steering, -1 (left) .. 1 (right), 0 inside the dead zone */
static float steer_of(float x)
{
    const gta2_feel *f = gta2_feel_get();
    float ax = fabsf(x);
    if (ax <= f->steer_dead)
        return 0;
    float t = powf(clamp01((ax - f->steer_dead) / (1.0f - f->steer_dead)), f->steer_curve);
    return x < 0 ? -t : t;
}

/* a trigger's pull, 0..1 (the most of any connected controller); the port's own pad layer only knows pressed / not */
static float trigger_pull(int right)
{
    int n = 0;
    float best = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) {
        SDL_Gamepad *p = SDL_GetGamepadFromID(ids[i]);
        if (!p)
            continue;
        float a = (float)SDL_GetGamepadAxis(p, right ? SDL_GAMEPAD_AXIS_RIGHT_TRIGGER : SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
        if (a > best)
            best = a;
    }
    SDL_free(ids);
    return best;
}

/* the stick's push one way as a pedal pull 0..1: its own dead zone first (a resting stick drifts a little), the same
 * as the steering's (ANALOGUE STEERING > DEAD ZONE) */
static float stick_pull(float v)
{
    float d = gta2_feel_get()->steer_dead;
    return v <= d ? 0 : clamp01((v - d) / (1.0f - d));
}

/* pull -> pedal: 0 under a small dead zone, full from "pedal_full", then floor + (1 - floor) * x^curve (curve > 1 = less throttle in the middle).
 * settings.ini: feel_pedal_dead, feel_pedal_full, feel_pedal_curve, feel_pedal_floor (read once). */
/* ENHANCEMENTS > ANALOGUE > PEDALS DEAD ZONE / FULL AT (v0.9.1; were feel_pedal_dead / _full in settings.ini) - for the
 * forward gas's linear pull and the brake / reverse alike, read every time so a change applies at once */
static void pedal_range(float *dead, float *full)
{
    static const float k_pd[5] = { 0.0f, 0.05f, 0.10f, 0.15f, 0.20f };
    int d = opt_get(OPT_PEDAL_DEAD), f = opt_get(OPT_PEDAL_FULL);
    *dead = k_pd[d < 0 ? 0 : d > 4 ? 4 : d];
    *full = 0.70f + 0.05f * (float)(f < 0 ? 0 : f > 6 ? 6 : f);
    if (*full < *dead + 0.1f)
        *full = *dead + 0.1f;
}

static float pedal_of(float t)
{
    static float curve = -1, floor_;
    if (curve < 0) {
        /* v0.9.1 (maintainer 2026-11-28, stick and triggers): "the less I press, the less gas - with a minimal press
         * really minimal acceleration"; was 0.80 / 2.0 / floor 0.30 (a touch gave 30 %) */
        curve = ovr("feel_pedal_curve", 1.8f);    /* > 1: fine at the bottom - a quarter pull ~7 %, half ~28 % */
        floor_ = ovr("feel_pedal_floor", 0.0f);   /* no step at the first touch: the gas starts at 0 */
    }
    float dead, full;
    pedal_range(&dead, &full);
    if (t <= dead)
        return 0;
    return floor_ + (1.0f - floor_) * powf(clamp01((t - dead) / (full - dead)), curve);
}

static float g_pull_r, g_pull_l, g_duty = -1;
static int g_gas_lead;   /* the gas pedal is pressed more than the brake: its pull sets a target speed */
static float g_gas_pull; /* that pull, 0..1 linear */

void gta2_feel_car_pre(void)
{
    g_duty = -1;
    if (!pedals_active())
        return;
    uint32_t ped = gta2_player_ped();
    if (!ped || !gta2_player_driving() || !wr_mem_valid(ped, 0x170))
        return;
    uint32_t player = wr_rd32(ped + 0x15C);
    if (!player || !wr_mem_valid(player, 0x90))
        return;
    int gs = gta2_gas_analogue(), bs = gta2_brake_analogue();
    float gx, gy, bx, by;   /* y down = +: up is the gas, down the brake */
    stick_xy(gs == GTA2_PEDAL_LSTICK ? 1 : gs == GTA2_PEDAL_RSTICK ? 2 : 0, &gx, &gy);
    stick_xy(bs == GTA2_PEDAL_LSTICK ? 1 : bs == GTA2_PEDAL_RSTICK ? 2 : 0, &bx, &by);
    g_pull_r = gs == GTA2_PEDAL_TRIGGER ? trigger_pull(1) : stick_pull(-gy);
    g_pull_l = bs == GTA2_PEDAL_TRIGGER ? trigger_pull(0) : stick_pull(by);
    float pr = gs ? pedal_of(g_pull_r) : 0, pl = bs ? pedal_of(g_pull_l) : 0;
    int dig = wr_rd8(player + 0x78) || wr_rd8(player + 0x79);   /* Cross / Square / keys: full, as before */
    if (pr > 0)
        wr_wr8(player + 0x78, 1);   /* the game's own gas / brake / reverse logic sees the pull as a press ... */
    if (pl > 0)
        wr_wr8(player + 0x79, 1);
    g_duty = dig ? -1 : (pr > pl ? pr : pl);   /* ... and the throttle it gives is gated to the pull afterwards */
    g_gas_lead = pr > 0 && pr >= pl;
    /* the gas pedal's pull for the target speed: linear past the pedals' DEAD ZONE, full from FULL AT */
    float t = g_pull_r, dz, fu;
    pedal_range(&dz, &fu);
    g_gas_pull = t <= dz ? 0 : clamp01((t - dz) / (fu - dz));
}

/* v0.9.1 (maintainer 2026-11-28: "shouldn't the car get slower when I ease off?"): with the gas pedal the pull is a
 * target speed - pedal x this car's top speed - and the game's gas is given only while the car is slower, so easing
 * off lets the game's own drag slow it down (the PR only gated how often the gas was given: less pull = slower
 * acceleration, but the same top speed in the end). The speed is |phys+0x40, +0x44| (the velocity, sub_00421150 via
 * sub_00421E70); the top speed is the game's own for this model (gta2_feel_car_top, from the car data the game's
 * limiter sub_004A1B20 uses); only if that is unknown, the most the car reached, starting from feel_vmax. */
static uint32_t g_top_car, g_top;
void gta2_feel_car_top(uint32_t car, uint32_t top)
{
    g_top_car = car;
    g_top = top;
}

/* At the target the drive stays on and the speed is held there the way the game's own limiter holds the top speed
 * (sub_004A1B20: the velocity scaled down to the limit), so it does not pulse; only well above it (after easing off)
 * the drive goes off and the game's drag slows the car. (The first try switched phys+0x91 - the brake, not the gas -
 * which the maintainer felt as "gas, brake, gas, brake".) pull: 0..1, linear (half a pull = half the top speed). */
static int gas_for_speed(uint32_t car, uint32_t phys, float pull, float *speed_out, float *vmax_out)
{
    static uint32_t vcar;
    static float vmax, last_target;
    static int held;
    float vx = (float)(int32_t)wr_rd32(phys + 0x40), vy = (float)(int32_t)wr_rd32(phys + 0x44);
    float s = sqrtf(vx * vx + vy * vy);
    if (car != vcar) {
        vcar = car;
        vmax = ovr("feel_vmax", 6000.0f);
        held = 0;
    }
    if (car == g_top_car && g_top)
        vmax = (float)g_top;   /* the game's own top speed */
    else if (s > vmax)
        vmax = s;
    *speed_out = s;
    *vmax_out = vmax;
    /* held: the car reached the target from below (accelerating) and the target did not drop since - only then the speed
     * is held at it; when the target drops (easing off, letting go) the drive just goes off and the car rolls on and
     * slows down by the game's own drag, as before (maintainer 2026-11-28: "it used to roll when I let go") */
    float target = pull * vmax;
    if (held && target > last_target)
        last_target = target;   /* the target the car is held at: the highest since it got there */
    if (target < last_target - 0.02f * vmax)   /* eased off (more than the stick's jitter): roll from here */
        held = 0;
    if (s <= target) {
        held = 1;              /* below: full gas, the game's own acceleration */
        last_target = target;
        return 1;
    }
    if (!held)
        return 0;              /* above a lower target: roll */
    if (s > 0) {               /* at the target from below: drive on, the speed held there */
        float k = target / s;
        wr_wr32(phys + 0x40, (uint32_t)(int32_t)lroundf(vx * k));
        wr_wr32(phys + 0x44, (uint32_t)(int32_t)lroundf(vy * k));
    }
    return 1;
}

void gta2_feel_car_step(void)
{
    gta2_feel_move_reset();   /* in a car: the next time on foot starts standing */
    static float err, acc_gas;
    static int hold_left, out;
    static int logged, bad, tlogged;
    int steer_on = steer_active(), trig_on = pedals_active();
    if (!steer_on && !trig_on)
        return;
    uint32_t ped = gta2_player_ped();
    if (!ped || !gta2_player_driving() || !wr_mem_valid(ped, 0x170)) {
        err = 0;
        hold_left = 0;
        return;
    }
    uint32_t car = wr_rd32(ped + 0x16C);
    if (!car || !wr_mem_valid(car, 0x60))
        return;
    uint32_t phys = wr_rd32(car + 0x58);
    if (wr_rd32(car + 0x54) != ped || !phys || !wr_mem_valid(phys, 0xB0)) {
        if (!bad++)
            wr_log("feel: the car's driver / physics fields are not what docs/re/gameplay_input_pause.md says "
                   "(car %08x driver %08x ped %08x phys %08x) - steering left alone", car, wr_rd32(car + 0x54), ped, phys);
        return;
    }
    if (trig_on && g_duty >= 0) {
        /* ANALOGUE pedals (v0.9.1, maintainer 2026-11-28). The game's own flags (sub_0049FB00, docs/re/
         * gameplay_input_pause.md 3.3): phys+0x93 = drive forward (forward held, standing or rolling forward),
         * phys+0x94 = drive backward, phys+0x91 = brake (the pedal against the rolling direction). The PR gated 0x91
         * as "the gas" - so forward driving was always full gas. Now: forward = a target speed (gas_for_speed,
         * linear in the pull); backward and the brake = gated to the pull. */
        static float acc_rev, acc_brk;
        float speed = 0, vmax = 0;
        if (wr_rd8(phys + 0x93) && g_gas_lead)
            wr_wr8(phys + 0x93, (uint8_t)gas_for_speed(car, phys, g_gas_pull, &speed, &vmax));
        if (wr_rd8(phys + 0x94)) {
            acc_rev += g_duty;
            int on = acc_rev >= 1.0f;
            if (on)
                acc_rev -= 1.0f;
            wr_wr8(phys + 0x94, (uint8_t)on);
        } else
            acc_rev = 0;
        if (wr_rd8(phys + 0x91)) {
            acc_brk += g_duty;
            int on = acc_brk >= 1.0f;
            if (on)
                acc_brk -= 1.0f;
            wr_wr8(phys + 0x91, (uint8_t)on);
        } else
            acc_brk = 0;
        (void)acc_gas;
        static int lines, tk;
        if ((tk++ % 30) == 0 && lines++ < 600)   /* test logging: 5 minutes of driving at 2 lines a second */
            wr_log("feel: pedals gas %.2f brake %.2f -> %.2f (forward %d, backward %d, brake %d) speed %.0f / target %.0f / top %.0f",
                   g_pull_r, g_pull_l, g_duty, wr_rd8(phys + 0x93), wr_rd8(phys + 0x94), wr_rd8(phys + 0x91), speed,
                   g_gas_pull * vmax, vmax);
        if (!tlogged++)
            wr_log("feel: analog pedals active (car %08x phys %08x)", car, phys);
    }
    if (!steer_on)
        return;
    float sx, sy;
    stick_xy(gta2_steer_stick(), &sx, &sy);
    float t = steer_of(sx);
    if (t == 0) {   /* centred: the game's own steering stays (d-pad, keyboard), no leftover pulse */
        err = 0;
        hold_left = 0;
        return;
    }
    const gta2_feel *f = gta2_feel_get();
    int hold = (int)f->steer_hold;
    if (hold_left <= 0) {   /* error diffusion: the average of the pulses over time is t */
        err += t * (float)hold;
        float th = 0.5f * (float)hold;
        out = err > th ? 1 : err < -th ? -1 : 0;
        err -= (float)(out * hold);
        hold_left = hold;
    }
    hold_left--;
    wr_wr8(phys + 0xAD, (uint8_t)(out > 0 ? 0xFF : out < 0 ? 1 : 0));   /* right only = 0xFF, left only = 1 */
    if (!logged++)
        wr_log("feel: analog car steering active (car %08x phys %08x)", car, phys);
}
