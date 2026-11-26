/* GTA 2 Recompiled - the OPTIONS menu in the game's own frontend (gta2_menu.c). */
#ifndef GTA2_MENU_H
#define GTA2_MENU_H

#include "recomp_types.h"

void gta2_menu_frontend_built(uint32_t fe);   /* after sub_00453E20 (fe = its this) */
void gta2_menu_background(void);       /* after sub_00455F90 */
int  gta2_menu_input(X86Ctx *ctx);     /* before sub_004597C0 */
void gta2_menu_after_input(void);      /* after sub_004597C0 */
void gta2_menu_draw(X86Ctx *ctx);      /* after sub_00457920 */
void gta2_menu_apply_cheats(void);     /* after sub_00451930 (Debug flags read) */

/* the pause menu over the game */
void gta2_pause_tick(X86Ctx *ctx);      /* before sub_00462A30 (between loop steps) */
void gta2_pause_draw(X86Ctx *ctx);      /* after sub_004CA440 (HUD) */
int  gta2_pause_hide_panel(void);       /* skip sub_004C9FA0 (the game's PAUSED panel) */
int  gta2_pause_open(void);

#endif
