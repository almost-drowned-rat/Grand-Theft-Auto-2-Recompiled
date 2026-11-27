/* GTA 2 Recompiled - DAY / NIGHT cycle (see gta2_daynight.c). */
#ifndef GTA2_DAYNIGHT_H
#define GTA2_DAYNIGHT_H

/* The time of day, 0 <= h < 24 (the cycle's clock; also while it is OFF). */
double gta2_daynight_hour(void);

/* The ambient for this frame from the game's own (0..255): 1 = the cycle changes it (out = r, g, b, 0..255),
 * 0 = cycle OFF or full daylight - keep the original lighting. */
int gta2_daynight_ambient(float game_ambient, float out[3]);

/* render/gbh.c: the world of this frame is drawn (called before the HUD, sub_004CA440) - the HUD's sprites
 * carry the game's light flag too, so the cycle stops here until the next world frame. */
void gbh_daynight_world_end(void);

#endif
