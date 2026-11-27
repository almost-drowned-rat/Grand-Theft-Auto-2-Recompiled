/*
 * GTA 2 Recompiled - DAY / NIGHT cycle (ENHANCEMENTS; maintainer 2026-11-27, ahead of the ray tracing).
 *
 * The game already lights its world on the CPU: every world frame it sets the level's / script's ambient
 * (gbh_SetAmbient(sub_00401B10()) at 0x00472132, only while Screen\lighting is on - the default) and adds the
 * visible point lights (street lamps ...; gbh_AddLight 0x004614CA); docs/re/gbh_api.md 1.4. The cycle only
 * scales and tints that ambient over the time of day - at night the game's own lights carry the picture.
 *
 * The clock runs on the game's logic ticks (30 per second, standing still in pause and menus) and goes on
 * across levels; it starts at 08:00. DAY LENGTH = real minutes for one full day.
 * OFF (default): nothing changes, the original lighting path to the bit.
 */
#include "gta2_daynight.h"
#include "gta2_options.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t gta2_game_ticks(void);

#define DN_START_HOUR 8.0

/* time of day -> brightness and tint of the ambient; between two keys linear (hour, m, r, g, b) */
typedef struct { float h, m, r, g, b; } dnkey;
static const dnkey k_keys[] = {
    /* three times of day (maintainer 2026-11-27): DAY, SUNRISE / SUNSET (held for a while, not only a fade), NIGHT */
    /* m = share of the level's own ambient (levels are often dim already: 0.3 in the first district) */
    { 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },    /* night: dark blue, the lamps light the streets */
    { 4.5f, 0.50f, 0.60f, 0.72f, 1.00f },
    { 6.0f, 0.85f, 1.00f, 0.82f, 0.78f },    /* sunrise: pink */
    { 7.0f, 0.85f, 1.00f, 0.82f, 0.78f },
    { 8.0f, 1.00f, 1.00f, 1.00f, 1.00f },    /* day: the game's own picture */
    { 17.0f, 1.00f, 1.00f, 1.00f, 1.00f },
    { 18.0f, 0.85f, 1.00f, 0.75f, 0.55f },   /* sunset: orange */
    { 19.5f, 0.85f, 1.00f, 0.75f, 0.55f },
    { 21.0f, 0.50f, 0.60f, 0.72f, 1.00f },
    { 24.0f, 0.50f, 0.60f, 0.72f, 1.00f },
};

static const int k_minutes[] = { 24, 48, 96 };   /* DAY LENGTH choices */

double gta2_daynight_hour(void)
{
    int c = opt_get(OPT_DAY_LENGTH);
    double per_day = (double)k_minutes[c >= 0 && c < 3 ? c : 0] * 60.0 * 30.0;   /* ticks per day */
    static double start = -1;
    if (start < 0) {   /* GTA2_DAYNIGHT_START=<hour>: tests / screenshots at a fixed time of day */
        const char *e = getenv("GTA2_DAYNIGHT_START");
        start = e && *e ? fmod(fabs(atof(e)), 24.0) : DN_START_HOUR;
    }
    double h = start + (double)gta2_game_ticks() / per_day * 24.0;
    return fmod(h, 24.0);
}

int gta2_daynight_ambient(float game_ambient, float out[3])
{
    if (!opt_get(OPT_DAY_NIGHT))
        return 0;
    float h = (float)gta2_daynight_hour();
    int i = 0;
    while (i + 2 < (int)(sizeof k_keys / sizeof k_keys[0]) && h >= k_keys[i + 1].h)
        i++;
    const dnkey *a = &k_keys[i], *b = &k_keys[i + 1];
    float t = b->h > a->h ? (h - a->h) / (b->h - a->h) : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float m = a->m + (b->m - a->m) * t;
    out[0] = game_ambient * m * (a->r + (b->r - a->r) * t);
    out[1] = game_ambient * m * (a->g + (b->g - a->g) * t);
    out[2] = game_ambient * m * (a->b + (b->b - a->b) * t);
    /* full day on a fully lit level: the original path (lights skipped at ambient 255) */
    return !(out[0] >= 255.0f && out[1] >= 255.0f && out[2] >= 255.0f);
}
