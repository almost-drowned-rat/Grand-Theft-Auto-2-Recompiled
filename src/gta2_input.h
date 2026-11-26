/* GTA 2 Recompiled - controller schemes and the CUSTOM / keyboard bindings (gta2_input.c). */
#ifndef GTA2_INPUT_H
#define GTA2_INPUT_H

#include <stddef.h>
#include <stdint.h>

#define GTA2_BIND_N 16   /* 12 game actions + SPRINT + RADIO NEXT / PREVIOUS + MENU (the keyboard: the first 13) */

int  gta2_bind_count(int keyboard);
const char *gta2_bind_label(int i);
void gta2_bind_value(int keyboard, int i, char *out, size_t cap);
void gta2_bind_set_pad(int i, int control);
void gta2_bind_set_key(int i, uint8_t dik);   /* then call sub_0044BF30 */
void gta2_bind_defaults(int keyboard);        /* the keyboard: then call sub_0044BF30 */

int  gta2_sprint_pressed(void);
void gta2_keys_apply(void);
int  gta2_kbm_sticks(float *lx, float *ly, float *rx, float *ry);
int  gta2_game_centre(float *x, float *y);   /* the game picture's centre in window points (render/vid.c) */

#endif
