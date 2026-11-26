/*
 * GTA 2 recomp - the MIXTAPE radio station: the player's own music (M4c,
 * maintainer 2026-11-26).
 *
 * Music: every folder below media/ that holds music is one album (winrecomp
 * media_scan: MP3 / FLAC / WAV / Ogg, .m3u order). SOUND options: SOURCE =
 * ALL ALBUMS or one album, PLAYBACK MODE = PLAY IN ORDER / SHUFFLE
 * (saves/settings.ini mixtape_source = all | <album path>, mixtape_mode =
 * order | shuffle).
 * Like the game's own stations it plays on while nobody listens: leaving it
 * keeps the song and its position with the game tick; coming back skips the
 * game time since then (song lengths from the files), so it is somewhere
 * later, maybe in another song. Game time, not wall time: the pause stops it.
 * The radio hook (which station, when it is audible) is gta2_mixtape_listen.
 */
#include "wr.h"
#include "audio/wr_audio.h"
#include "media/media_decode.h"
#include "media/media_playlist.h"
#include "media/media_scan.h"
#include "gta2_mixtape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MediaLibrary g_lib;
static MediaPlaylist g_pl;
static int g_ready, g_have;
static uint32_t *g_len_ms;          /* per playlist entry, 0 = not probed yet */

/* the virtual play head: song g_pl.pos at g_pos_ms when the game tick was g_at_tick */
static uint32_t g_pos_ms, g_at_tick;
static int g_listening;
static MediaDecoder *g_dec;         /* the song playing (audio thread reads it) */
static uint64_t g_dec_frames;       /* frames played of it */

uint32_t gta2_game_ticks(void);

static const char *song_path(int i)
{
    const MediaSong *s = &g_pl.items[i];
    return g_lib.albums[s->album].tracks[s->track].path;
}

static uint32_t song_len(int i)
{
    if (!g_len_ms[i]) {
        uint32_t l = media_probe_length_ms(song_path(i));
        g_len_ms[i] = l ? l : 180000;   /* unknown (an MP3 without a header): 3 minutes */
    }
    return g_len_ms[i];
}

static void build(void)
{
    media_playlist_free(&g_pl);
    free(g_len_ms);
    g_len_ms = NULL;
    g_have = 0;
    if (!g_lib.nalbums)
        return;
    const char *src = wr_settings_get("mixtape_source", "all");
    int shuffle = !strcmp(wr_settings_get("mixtape_mode", "order"), "shuffle");
    int sel = strcmp(src, "all") ? media_find_album(&g_lib, src) : -1;
    MediaPlayMode m = sel >= 0 ? (shuffle ? MEDIA_ALBUM_SHUFFLE : MEDIA_ALBUM_ORDER)
                               : (shuffle ? MEDIA_CUSTOM_SHUFFLE : MEDIA_CUSTOM_ORDER);
    if (media_playlist_build(&g_pl, &g_lib, 0, m, sel, (uint32_t)wr_time_ns()) <= 0 || !g_pl.n)
        return;
    g_len_ms = calloc((size_t)g_pl.n, sizeof *g_len_ms);
    g_have = g_len_ms != NULL;
    g_pos_ms = 0;
    g_at_tick = gta2_game_ticks();
}

void gta2_mixtape_init(void)
{
    if (g_ready)
        return;
    g_ready = 1;
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/media", g_wr_paths.root);
    media_scan(dir, &g_lib);
    wr_log("mixtape: %d album(s) in %s", g_lib.nalbums, dir);
    build();
}

int gta2_mixtape_available(void) { return g_have; }
int gta2_mixtape_album_count(void) { return g_lib.nalbums; }
const char *gta2_mixtape_album_name(int i) { return i >= 0 && i < g_lib.nalbums ? g_lib.albums[i].name : ""; }
const char *gta2_mixtape_album_key(int i) { return i >= 0 && i < g_lib.nalbums ? g_lib.albums[i].rel : ""; }

/* ---- the audio thread's side ---- */
static long feed(int16_t *out, long frames)
{
    long got = 0;
    while (got < frames && g_dec) {
        long n = media_decoder_read(g_dec, out + 2 * got, frames - got);
        if (n > 0) {
            got += n;
            g_dec_frames += (uint64_t)n;
            continue;
        }
        /* the song is over: the next one (opening is quick; no seek here) */
        media_decoder_close(g_dec);
        g_dec = NULL;
        media_playlist_next(&g_pl);
        g_dec_frames = 0;
        for (int tries = 0; tries < g_pl.n && !g_dec; tries++) {
            g_dec = media_decoder_open(song_path(g_pl.pos));
            if (!g_dec)
                media_playlist_next(&g_pl);
        }
    }
    return got;
}

/* the play head now: the song and ms into it, after the game time since the last mark */
static void advance_to_now(void)
{
    uint32_t t = gta2_game_ticks();
    uint64_t ms = (uint64_t)g_pos_ms + (uint64_t)(t - g_at_tick) * 33u;
    for (int guard = 0; guard < 10000 && ms >= song_len(g_pl.pos); guard++) {
        ms -= song_len(g_pl.pos);
        media_playlist_next(&g_pl);
    }
    g_pos_ms = (uint32_t)ms;
    g_at_tick = t;
}

/* Audible or not: the radio is on MIXTAPE in a car (and the game is not paused). */
void gta2_mixtape_listen(int on)
{
    on = on && g_have;
    if (on == g_listening)
        return;
    g_listening = on;
    if (on) {
        advance_to_now();
        MediaDecoder *d = NULL;
        for (int tries = 0; tries < g_pl.n && !d; tries++) {
            d = media_decoder_open(song_path(g_pl.pos));
            if (!d) {
                media_playlist_next(&g_pl);
                g_pos_ms = 0;
            }
        }
        if (d && g_pos_ms)
            media_decoder_seek_ms(d, g_pos_ms);   /* off the audio thread */
        wr_audio_music(NULL);
        g_dec = d;
        g_dec_frames = (uint64_t)g_pos_ms * 44100u / 1000u;
        wr_audio_music(feed);
        wr_log("mixtape: on, %s at %u s", d ? song_path(g_pl.pos) : "-", g_pos_ms / 1000);
    } else {
        wr_audio_music(NULL);   /* the mixer lets go of the decoder */
        g_pos_ms = (uint32_t)(g_dec_frames * 1000u / 44100u);
        g_at_tick = gta2_game_ticks();
        media_decoder_close(g_dec);
        g_dec = NULL;
        wr_log("mixtape: off at song %d, %u s", g_pl.pos, g_pos_ms / 1000);
    }
}

void gta2_mixtape_volume(float v) { wr_audio_music_volume(v); }

/* SOURCE or PLAYBACK MODE changed: a new list (from its start) */
void gta2_mixtape_rebuild(void)
{
    int was = g_listening;
    gta2_mixtape_listen(0);
    build();
    gta2_mixtape_listen(was);
}

/* the song's name for the HUD line, "" when none */
const char *gta2_mixtape_song(void)
{
    if (!g_have)
        return "";
    const MediaSong *s = &g_pl.items[g_pl.pos];
    return g_lib.albums[s->album].tracks[s->track].name;
}
