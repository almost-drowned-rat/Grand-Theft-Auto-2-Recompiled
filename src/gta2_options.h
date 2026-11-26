/*
 * GTA 2 Recompiled - RECOMP OPTIONS as data.
 *
 * Every option is a list of choices kept in saves/settings.ini (GTA2_<KEY>
 * overrides one run). The menu pages only page through this table:
 * opt_count / opt_choice_label / opt_get / opt_set. opt_set stores the
 * choice and applies it at once (where the game allows; ASPECT RATIO's
 * gameplay width applies when the next game starts).
 */
#ifndef GTA2_OPTIONS_H
#define GTA2_OPTIONS_H

enum {
    OPT_DISPLAY_MODE,      /* WINDOWED / BORDERLESS / FULLSCREEN (hidden on Android) */
    OPT_RENDER_RES,        /* NATIVE / 480P ... 2160P */
    OPT_ASPECT,            /* 4:3 / 16:9 / 16:10 / 21:9 */
    OPT_TEXTURE_FILTER,    /* ORIGINAL / NEAREST / BILINEAR */
    OPT_FRAME_RATE,        /* 30 / 60 INTERPOLATED / 120 INTERPOLATED (M4b) */
    OPT_VSYNC,             /* OFF / ON */
    OPT_PERF_FPS,          /* OFF / ON  \                               */
    OPT_PERF_FRAMETIME,    /* OFF / ON   } stored together: performance_overlay */
    OPT_PERF_SPEED,        /* OFF / ON  /                               */
    OPT_RADIO_HALF_RATE,   /* OFF / ON (stereo radio stream rate, see docs/re/audio.md) */
    OPT_CONTROL_SCHEME,    /* CLASSIC / MODERN / CUSTOM (controller) */
    OPT_KEY_SCHEME,        /* CLASSIC / MODERN (keyboard only, as a controller) / KEYBOARD + MOUSE */
    OPT_PEDESTRIANS,       /* 1X / 2X / 3X / 4X (from the next game) */
    OPT_TRAFFIC,           /* 1X / 2X / 3X / 4X */
    OPT_SPRINT,            /* OFF / ON */
    OPT_MORE_BLOOD,        /* OFF / ON: blood stays longer, dries, and the bodies stay with it */
    OPT_COUNT
};

void        opt_load_all(void);               /* at start, before the window opens */
const char *opt_label(int id);                /* the line's name, e.g. "ASPECT RATIO" */
int         opt_count(int id);                /* number of choices */
const char *opt_choice_label(int id, int c);  /* e.g. "16:9" */
int         opt_get(int id);
int         opt_default(int id);
void        opt_set(int id, int c);           /* store + apply */
int         opt_visible(int id);              /* 0: hidden on this platform */

/* Values the renderer reads */
double opt_aspect_ratio(void);                /* the chosen width / height */
int    opt_render_height(void);               /* 0 = NATIVE (cover the window) */
int    opt_texture_filter(void);              /* -1 original, 0 nearest, 1 bilinear */
int    opt_display_mode(void);                /* 0 windowed, 1 borderless, 2 fullscreen */

#endif
