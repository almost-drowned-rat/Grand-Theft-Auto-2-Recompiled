/*
 * GTA 2 Recompiled - entry point.
 *
 * Portable layout next to the executable (WR_ROOT overrides it for dev
 * runs): game/ holds the installed game (gta2.exe and data/), saves/ the
 * player's data and the registry, data/ the log.
 */
#include "wr.h"
#include "gta2_options.h"
#include "gta2_rumble.h"
#include "shaderpack/shader_pack.h"
#include "platform/wr_platform.h"
#include "render/gta2_render.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wr_register_miles(void);
wr_coop_step_fn gta2_thread_hook(X86Ctx *ctx, uint32_t start, uint32_t param);
recomp_func_t gta2_recomp_lookup(uint32_t va);
int gta2_dispatch_init(void);
recomp_func_t bink_recomp_lookup(uint32_t va);
int bink_dispatch_init(void);
extern const wr_export g_bink_overrides[];
void gta2_registry_defaults(void);
void gta2_manual_register(void);
void gta2_input_register(void);
void w32_exit(uint32_t code);

/* gta2.exe of the Rockstar freeware release (build 2004-04-27) */
static const char k_sha1[] = "646e1d479fa2aea12b3ff448fa23f6362f9e012f";

int main(int argc, char **argv)
{
    char root[1024];
    const char *env = getenv("WR_ROOT");
    if (env && *env) {
        snprintf(root, sizeof root, "%s", env);
    } else {
        const char *b = SDL_GetBasePath();
        snprintf(root, sizeof root, "%s", b ? b : ".");
    }
    size_t l = strlen(root);
    while (l > 1 && (root[l - 1] == '/' || root[l - 1] == '\\'))
        root[--l] = 0;

    snprintf(g_wr_vroot, sizeof g_wr_vroot, "%s", "C:\\GTA2");
    snprintf(g_wr_exe_name, sizeof g_wr_exe_name, "%s", "gta2.exe");
    wr_paths_init(root);
    wr_log_set_name("gta2");   /* data/gta2.log, the run before: data/gta2.previous.log */
    wr_log_init();
    wr_log_header("GTA 2 Recompiled", "build " GTA2_GIT " (" __DATE__ ")");
    wr_settings_init("GTA2");   /* saves/settings.ini, GTA2_<KEY> overrides */
    wr_crash_init();

    if (!wr_mem_init())
        return 1;
    wr_win32_register();
    wr_register_miles();
    gta2_rumble_init();
    wr_set_thread_hook(gta2_thread_hook);
    gta2_registry_defaults();
    opt_load_all();   /* RECOMP OPTIONS from settings.ini (sets the gameplay width) */
    {   /* the player's shader packs: media/shaders -> data/shaders (a thread of its own) */
        char ms[1100], ds[1100];
        snprintf(ms, sizeof ms, "%s/shaders", g_wr_paths.media);
        snprintf(ds, sizeof ds, "%s/shaders", g_wr_paths.data);
        shader_pack_show_bezels(1);   /* as the N64 recomps: bezel / border listed too */
        shader_pack_start(ms, ds);
    }
    gta2_manual_register();
    gta2_input_register();
    gta2_register_video();
    gta2_register_renderer();

    /* binkw32.dll first: gta2.exe's imports bind to its lifted code */
    char exe[1200], bink[1200];
    if (!wr_path_ci(g_wr_paths.game, "binkw32.dll", bink, sizeof bink) ||
        !wr_pe_load_dll(bink, "binkw32.dll", g_bink_overrides, bink_recomp_lookup, bink_dispatch_init)) {
        wr_log("binkw32.dll missing: movies are skipped");
        void wr_register_bink_absent(void);
        wr_register_bink_absent();
    }
    if (!wr_path_ci(g_wr_paths.game, "gta2.exe", exe, sizeof exe)) {
        wr_log("gta2.exe not found in %s", g_wr_paths.game);
        return 1;
    }
    if (!wr_pe_load(exe, gta2_recomp_lookup, gta2_dispatch_init))
        return 1;
    char hex[41];
    for (int i = 0; i < 20; i++)
        snprintf(hex + 2 * i, 3, "%02x", g_wr_image.sha1[i]);
    if (strcmp(hex, k_sha1))
        wr_log("WARNING: gta2.exe sha1 %s is not the freeware release this build was lifted from (%s)",
               hex, k_sha1);

    if (!wr_platform_init())
        return 1;
    int code = wr_run(argc, argv);
    w32_exit((uint32_t)code);
    return code;
}
