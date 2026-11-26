/*
 * GTA 2 Recompiled - the SHADERS page: RetroArch slang shaders (option-c's
 * shader pass + shader packs; THPS 2X's menu as the model).
 *
 *   SHADER   OFF / ON           settings.ini shader_on
 *   PRESET   the preset in use  settings.ini shader_preset (path of the .slangp)
 * The browser lists the packs of media/shaders (extracted to data/shaders by
 * shader_pack_start at start-up): folders first, presets with their pass
 * count, ".." to go up, NONE on top.
 */
#include "gta2_shaders.h"
#include "wr.h"
#include "post/shader_pass.h"
#include "post/wr_post.h"
#include "shaderpack/shader_pack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_dir[512];   /* the browser's folder: "" = the packs, else "<pack>/<path>" */

void gta2_shaders_apply(void)
{
    const char *p = wr_settings_get("shader_preset", NULL);
    sp_set_preset(wr_settings_get_int("shader_on", 0) && p && *p ? p : NULL);
}

int gta2_shader_on(void) { return wr_settings_get_int("shader_on", 0); }

void gta2_shader_set_on(int on)
{
    wr_settings_set_int("shader_on", on != 0);
    gta2_shaders_apply();
}

/* upper-case display name of a path's last part, without .slangp */
static void display(const char *path, char *out, size_t cap)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    size_t j = 0;
    for (; b[j] && j + 1 < cap; j++)
        out[j] = b[j] == '_' || b[j] == '-' ? ' ' : (char)toupper((unsigned char)b[j]);
    out[j] = 0;
    char *e = strstr(out, ".SLANGP");
    if (e)
        *e = 0;
}

void gta2_shader_preset_name(char *out, size_t cap)
{
    const char *p = wr_settings_get("shader_preset", NULL);
    if (!p || !*p)
        snprintf(out, cap, "NONE");
    else
        display(p, out, cap);
}

const char *gta2_shader_hint(void)
{
    int st = sp_status();
    if (st == WR_POST_SHADER_NO_LIBRARY)
        return "NEEDS LIBRASHADER IN THE GAME FOLDER";
    if (shader_pack_state() == SHADER_PACK_WORKING)
        return "PREPARING THE SHADER PACKS";
    const ShaderIndex *ix = shader_pack_index();
    if (!ix || !ix->n)
        return "NEEDS A SHADER PACK IN MEDIA SHADERS";
    if (st == WR_POST_SHADER_LOADING)
        return "LOADING";
    if (st == WR_POST_SHADER_FAILED)
        return "THIS PRESET DID NOT LOAD";
    return NULL;
}

/* ---- browser -------------------------------------------------------------------------------- */

#define MAX_ITEMS 1024
static gta2_shader_item g_items[MAX_ITEMS];
static char g_names[MAX_ITEMS][48];
static int g_nitems;

static int item_cmp(const void *a, const void *b)
{
    const gta2_shader_item *x = a, *y = b;
    if (x->kind != y->kind)
        return x->kind - y->kind;
    return strcmp(x->label, y->label);
}

int gta2_shader_browse(const gta2_shader_item **items)
{
    g_nitems = 0;
    const ShaderIndex *ix = shader_pack_index();
    g_items[g_nitems++] = (gta2_shader_item){ "NONE", SI_NONE, -1, "" };
    if (g_dir[0])
        g_items[g_nitems++] = (gta2_shader_item){ "..", SI_UP, -1, "" };
    int first = g_nitems;
    size_t dl = strlen(g_dir);
    for (int i = 0; ix && i < ix->n && g_nitems < MAX_ITEMS; i++) {
        const char *rel = ix->p[i].rel;
        if (dl && (strncmp(rel, g_dir, dl) || rel[dl] != '/'))
            continue;
        const char *rest = dl ? rel + dl + 1 : rel;
        const char *slash = strchr(rest, '/');
        char name[48];
        int folder = slash != NULL;
        size_t n = folder ? (size_t)(slash - rest) : strlen(rest);
        if (n >= sizeof name)
            n = sizeof name - 1;
        memcpy(name, rest, n);
        name[n] = 0;
        if (folder) {   /* once per folder */
            int seen = 0;
            for (int k = first; k < g_nitems; k++)
                if (g_items[k].kind == SI_FOLDER && !strcmp(g_items[k].key, name))
                    seen = 1;
            if (seen)
                continue;
        }
        char *lab = g_names[g_nitems];
        display(name, lab, 25);   /* fits the line beside its value */
        gta2_shader_item it = { lab, folder ? SI_FOLDER : SI_PRESET, folder ? -1 : i, "" };
        snprintf(it.key, sizeof it.key, "%s", name);
        g_items[g_nitems++] = it;
    }
    qsort(&g_items[first], (size_t)(g_nitems - first), sizeof g_items[0], item_cmp);
    *items = g_items;
    return g_nitems;
}

void gta2_shader_item_value(const gta2_shader_item *it, char *out, size_t cap)
{
    out[0] = 0;
    const ShaderIndex *ix = shader_pack_index();
    if (it->kind == SI_FOLDER)
        snprintf(out, cap, "FOLDER");
    else if (it->kind == SI_PRESET && ix && it->index < ix->n) {
        int p = ix->p[it->index].passes;
        if (p < 0)
            snprintf(out, cap, "BROKEN");
        else
            snprintf(out, cap, "%d PASS%s", p, p == 1 ? "" : "ES");
    }
}

/* Enter on an item: 1 = the list changed (folder / up), 0 = a choice was made */
int gta2_shader_pick(const gta2_shader_item *it)
{
    const ShaderIndex *ix = shader_pack_index();
    switch (it->kind) {
    case SI_UP: {
        char *s = strrchr(g_dir, '/');
        if (s)
            *s = 0;
        else
            g_dir[0] = 0;
        return 1;
    }
    case SI_FOLDER: {
        size_t l = strlen(g_dir);
        snprintf(g_dir + l, sizeof g_dir - l, "%s%s", l ? "/" : "", it->key);
        return 1;
    }
    case SI_NONE:
        wr_settings_set("shader_preset", "");
        gta2_shaders_apply();
        return 0;
    default:
        if (ix && it->index >= 0 && it->index < ix->n) {
            wr_settings_set("shader_preset", ix->p[it->index].path);
            wr_settings_set_int("shader_on", 1);
            gta2_shaders_apply();
        }
        return 0;
    }
}

/* the browser opens on the folder of the preset in use */
void gta2_shader_browse_open(void)
{
    g_dir[0] = 0;
    const char *p = wr_settings_get("shader_preset", NULL);
    const ShaderIndex *ix = shader_pack_index();
    for (int i = 0; p && *p && ix && i < ix->n; i++)
        if (!strcmp(ix->p[i].path, p)) {
            snprintf(g_dir, sizeof g_dir, "%s", ix->p[i].rel);
            char *s = strrchr(g_dir, '/');
            if (s)
                *s = 0;
            else
                g_dir[0] = 0;
        }
}

void gta2_shader_dir_title(char *out, size_t cap)
{
    if (!g_dir[0]) {
        snprintf(out, cap, "SHADER PACKS");
        return;
    }
    display(g_dir, out, cap);
}
