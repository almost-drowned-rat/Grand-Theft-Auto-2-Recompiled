/* GTA 2 recomp - FRAME RATE 60 / 120 INTERPOLATED (gta2_framerate.c) */
#ifndef GTA2_FRAMERATE_H
#define GTA2_FRAMERATE_H
#include <stdint.h>
int  gta2_fps_target(void);              /* 30 / 60 / 120 */
void gta2_fps_after_tick(void);
void gta2_fps_draw_begin(void);
void gta2_fps_draw_end(void);
int  gta2_fps_sprite_before(uint32_t spr);
void gta2_fps_sprite_after(uint32_t spr);
int  gta2_fps_extra_due(void);
#endif
