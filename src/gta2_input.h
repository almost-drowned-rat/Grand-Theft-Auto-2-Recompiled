/* GTA 2 Recompiled - controller schemes and the CUSTOM / keyboard bindings (gta2_input.c). */
#ifndef GTA2_INPUT_H
#define GTA2_INPUT_H

#include <stddef.h>
#include <stdint.h>

#define GTA2_BIND_N 14   /* 12 game actions + RADIO + MENU (the keyboard: the first 12) */

int  gta2_bind_count(int keyboard);
const char *gta2_bind_label(int i);
void gta2_bind_value(int keyboard, int i, char *out, size_t cap);
void gta2_bind_set_pad(int i, int control);
void gta2_bind_set_key(int i, uint8_t dik);   /* then call sub_0044BF30 */
void gta2_bind_defaults(int keyboard);        /* the keyboard: then call sub_0044BF30 */

#endif
