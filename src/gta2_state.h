/* GTA 2 Recompiled - what the local player is doing (docs/re/gameplay_input_pause.md §2). */
#ifndef GTA2_STATE_H
#define GTA2_STATE_H

#include <stdint.h>

uint32_t gta2_game(void);           /* the game object, 0 outside a game */
uint32_t gta2_player(void);         /* the local player, 0 outside a game */
uint32_t gta2_player_ped(void);     /* the player's ped, or 0 */
int      gta2_player_driving(void); /* 1: driving a car (not a passenger, not a train) */

#endif
