/* GTA 2 recomp - CONTROLLER LAYOUT picture (gta2_padview.c) */
#ifndef GTA2_PADVIEW_H
#define GTA2_PADVIEW_H
#include <stdint.h>

enum { PV_LT, PV_LB, PV_LSTICK, PV_L3, PV_DPAD, PV_SELECT, PV_RT, PV_RB, PV_Y, PV_B, PV_A, PV_X, PV_RSTICK, PV_R3,
       PV_START, PV_N };
#define PV_LX 186   /* left labels end here (game screen units, 640 wide) */
#define PV_RX 454   /* right labels start here */

typedef struct pv_item { int id, ax, ay, right, ly; } pv_item;   /* control, its point, side, label row */
extern const pv_item g_pv_items[PV_N];

const uint32_t *gta2_pad_picture(void);   /* 640x480 RGBA, made once */
#endif
