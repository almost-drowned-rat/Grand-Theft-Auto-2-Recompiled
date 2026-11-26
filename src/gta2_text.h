/* GTA 2 recomp - text in languages the game's fonts cannot draw (gta2_text.c, docs/re/text.md) */
#ifndef GTA2_TEXT_H
#define GTA2_TEXT_H
#include "wr.h"
void gta2_text_init(void);                        /* after the translations: the language's font */
int  gta2_text_letter_ours(char c);              /* p r c n */
char gta2_text_letter(void);                     /* the active one, 0 none */
void gta2_text_after_gxt_ctor(uint32_t gxt);
int  gta2_text_draw(X86Ctx *ctx);                /* before sub_004CC100_gen */
int  gta2_text_width(X86Ctx *ctx, uint16_t *w);  /* sub_004BF570: 1 = ours */
int  gta2_text_upper(uint16_t c, uint16_t *out); /* sub_004C21A0: 1 = ours */
#endif
