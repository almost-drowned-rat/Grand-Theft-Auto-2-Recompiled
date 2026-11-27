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

/* time of day -> the ambient; between two keys linear (hour, d, m, r, g, b):
 *   base = level's ambient raised toward full light by d (0 = the level's own, 1 = 255),
 *   ambient = base * m * tint (r, g, b) */
typedef struct { float h, d, m, r, g, b; } dnkey;
static const dnkey k_keys[] = {
    /* three times of day (maintainer 2026-11-27): DAY, SUNRISE / SUNSET (held for a while, not only a fade), NIGHT.
     * DAY is fully lit (maintainer: "remove the shadow in the day") - the game's own ambient 1.0 path, no dim areas
     * between the street lamps' light pools; levels are often dim by design (0.3 in the first district) */
    { 0.0f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },    /* night: dark blue, the lamps light the streets */
    { 4.5f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },
    { 6.0f, 0.5f, 0.90f, 1.00f, 0.82f, 0.78f },    /* sunrise: pink */
    { 7.0f, 0.5f, 0.90f, 1.00f, 0.82f, 0.78f },
    { 8.0f, 1.0f, 1.00f, 1.00f, 1.00f, 1.00f },    /* day: full light */
    { 17.0f, 1.0f, 1.00f, 1.00f, 1.00f, 1.00f },
    { 18.0f, 0.5f, 0.90f, 1.00f, 0.75f, 0.55f },   /* sunset: orange */
    { 19.5f, 0.5f, 0.90f, 1.00f, 0.75f, 0.55f },
    { 21.0f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },
    { 24.0f, 0.0f, 0.50f, 0.60f, 0.72f, 1.00f },
};

static const int k_minutes[] = { 24, 48, 96 };   /* DAY LENGTH choices */

double gta2_daynight_hour(void)
{
    int c = opt_get(OPT_DAY_LENGTH);
    double minutes = (double)k_minutes[c >= 0 && c < 3 ? c : 0];
    static double test_minutes = -1;
    if (test_minutes < 0) {   /* GTA2_DAYNIGHT_MINUTES=<m>: a test day of m real minutes (e.g. 1) */
        const char *e = getenv("GTA2_DAYNIGHT_MINUTES");
        test_minutes = e && *e && atof(e) > 0 ? atof(e) : 0;
    }
    if (test_minutes > 0)
        minutes = test_minutes;
    double per_day = minutes * 60.0 * 30.0;   /* ticks per day */
    static double start = -1;
    if (start < 0) {   /* GTA2_DAYNIGHT_START=<hour>: tests / screenshots at a fixed time of day */
        const char *e = getenv("GTA2_DAYNIGHT_START");
        start = e && *e ? fmod(fabs(atof(e)), 24.0) : DN_START_HOUR;
    }
    double h = start + (double)gta2_game_ticks() / per_day * 24.0;
    return fmod(h, 24.0);
}

/* Headlights (gta2_manual.c sub_004BE060): off in the DAY, the same span as the untinted keys (08:00-17:00), so
 * they come on as the sunset starts and go off when the sunrise has faded. A switch costs one texture rebuild per
 * visible car (the game's delta cache), once - never flickers, the hour only moves forward. */
int gta2_daynight_lights_off(void)
{
    if (!opt_get(OPT_DAY_NIGHT))
        return 0;
    double h = gta2_daynight_hour();
    return h >= 8.0 && h < 17.0;
}

/* Full NIGHT (21:00-04:30, between the sunset and the sunrise): the U-Jerk Truck's extra lamps glow only then
 * (maintainer 2026-11-27: sunrise / sunset only the two middle lamps, as the original) */
int gta2_daynight_full_night(void)
{
    if (!opt_get(OPT_DAY_NIGHT))
        return 0;
    double h = gta2_daynight_hour();
    return h >= 21.0 || h < 4.5;
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
    float d = a->d + (b->d - a->d) * t, m = a->m + (b->m - a->m) * t;
    float base = (game_ambient + (255.0f - game_ambient) * d) * m;
    out[0] = base * (a->r + (b->r - a->r) * t);
    out[1] = base * (a->g + (b->g - a->g) * t);
    out[2] = base * (a->b + (b->b - a->b) * t);
    /* full light too goes through the lighting (ambient 255 -> every vertex full): the game's own ambient 1.0 path
     * (no lighting, vertex = its shade) turned faces with shade 0 - some roofs - black (maintainer's screenshots) */
    return 1;
}
