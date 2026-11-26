/* GTA 2 Recompiled - the SHADERS page (gta2_shaders.c). */
#ifndef GTA2_SHADERS_H
#define GTA2_SHADERS_H

#include <stddef.h>

enum { SI_NONE, SI_UP, SI_FOLDER, SI_PRESET };
typedef struct { const char *label; int kind; int index; char key[48]; } gta2_shader_item;

void gta2_shaders_apply(void);                 /* settings -> the shader pass */
int  gta2_shader_on(void);
void gta2_shader_set_on(int on);
void gta2_shader_preset_name(char *out, size_t cap);
const char *gta2_shader_hint(void);            /* a line to show, or NULL */
void gta2_shader_browse_open(void);
int  gta2_shader_browse(const gta2_shader_item **items);
void gta2_shader_item_value(const gta2_shader_item *it, char *out, size_t cap);
int  gta2_shader_pick(const gta2_shader_item *it);   /* 1: the folder changed */
void gta2_shader_dir_title(char *out, size_t cap);

#endif
