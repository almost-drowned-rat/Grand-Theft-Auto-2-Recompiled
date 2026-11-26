/* GTA 2 Recompiled - GAMEPLAY options: KEEP BODIES, MORE BLOOD (gta2_gameplay.c). */
#ifndef GTA2_GAMEPLAY_H
#define GTA2_GAMEPLAY_H

#include <stdint.h>

int  gta2_body_before_tick(uint32_t ped);   /* 1: kept this tick (undo its count) */
void gta2_blood_before_tick(uint32_t particle);
int  gta2_more_blood(void);

#endif
