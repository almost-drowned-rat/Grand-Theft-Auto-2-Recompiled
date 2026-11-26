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
#include "gta2_rumble.h"

#include <stdlib.h>

static void on_start(uint32_t addr, uint32_t len, float vol, uint32_t rate)
{
    static int log = -1;
    if (log < 0)
        log = getenv("GTA2_SOUND_LOG") != NULL;
    if (log)
        wr_log("sound start: data %08X len %u vol %.2f rate %u", addr, len, (double)vol, rate);
}

void gta2_rumble_init(void)
{
    g_wr_miles_start_hook = on_start;
}
