/*
 * GTA 2 recomp - controller rumble on explosions (M4c, maintainer 2026-11-26).
 * The game has no force feedback of its own (the DirectInput effect GUIDs in
 * the exe are library data, no code uses them). An explosion is told by its
 * sound: every sample start reaches gta2_rumble_sound (winrecomp Miles hook);
 * the game sets a sound's volume by its distance, so a near blast rumbles hard
 * and a far one softly.
 * GTA2_SOUND_LOG=1 logs every start (data, length, volume, rate) - the way the
 * explosion samples were found.
 */
#include "wr.h"
#include "platform/wr_platform.h"
#include "gta2_options.h"
#include "gta2_rumble.h"

#include <stdlib.h>

static void on_start(uint32_t addr, uint32_t len, float vol, uint32_t rate)
{
    static int log = -1;
    if (log < 0)
        log = getenv("GTA2_SOUND_LOG") != NULL;
    if (log)
        wr_log("sound start: data %08X len %u vol %.2f rate %u", addr, len, (double)vol, rate);
    /* the explosion sounds (found with GTA2_SOUND_LOG, maintainer's test 2026-11-26): the bang
     * (61182 bytes) and the long rumble after it (127022 bytes); every blast played the rumble,
     * most the bang too. Known by their length: the same in every district's sound bank. */
    if (len != 61182 && len != 127022)
        return;
    if (!opt_get(OPT_RUMBLE) || vol <= 0.02f)
        return;
    static uint64_t last_ns;
    static float last_strength;
    uint64_t now = wr_time_ns();
    float strength = vol > 1.0f ? 1.0f : vol;
    if (len == 61182)
        strength = strength / 0.62f > 1.0f ? 1.0f : strength / 0.62f;   /* the bang plays at 0.62 when close */
    if (now - last_ns < 400000000ull && strength <= last_strength)
        return;   /* bang + rumble of one blast: one pulse */
    last_ns = now;
    last_strength = strength;
    /* a hard thump that dies away: the heavy motor full, the light one half */
    wr_pad_rumble(strength, strength * 0.5f, (uint32_t)(250 + 450 * strength));
}

void gta2_rumble_init(void)
{
    g_wr_miles_start_hook = on_start;
}
