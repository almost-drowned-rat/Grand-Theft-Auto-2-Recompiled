/* GTA 2 recomp - MIXTAPE radio station (gta2_mixtape.c) */
#ifndef GTA2_MIXTAPE_H
#define GTA2_MIXTAPE_H
void gta2_mixtape_init(void);
int  gta2_mixtape_available(void);          /* 1 = music found in media/ */
int  gta2_mixtape_album_count(void);
const char *gta2_mixtape_album_name(int i);
const char *gta2_mixtape_album_key(int i);
void gta2_mixtape_listen(int on);
void gta2_mixtape_volume(float v);
void gta2_mixtape_rebuild(void);
const char *gta2_mixtape_song(void);
/* the radio cycle hooks (gta2_manual.c wrappers) */
#include "wr.h"
int  gta2_mixtape_on(void);
int  gta2_mixtape_audible(void);
void gta2_radio_after_key(void);
void gta2_radio_cycle(X86Ctx *ctx, void (*gen)(X86Ctx *, uint8_t *), uint8_t *base);
void gta2_radio_before_volume(void);
void gta2_radio_after_tick(void);
void gta2_radio_stopped(void);
#endif
