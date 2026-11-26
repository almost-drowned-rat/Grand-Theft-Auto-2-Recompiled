/*
 * GTA 2 recomp - translations (milestone after M6, maintainer 2026-11-26).
 *
 * GTA2-translations.pak next to the game (a zip; tools/make_translations.sh) holds
 * data/<letter>.gxt for each translated language - g = German first, built from the
 * English e.gxt by tools/gxt.py (the game font's own accents, so no new renderer for
 * DE / FR / IT). At start the pak is unpacked into data/translations/ (an entry is
 * written again only when its size or CRC changed) and that folder is read between
 * saves/ and game/: the game's own language code then finds data\\g.gxt, and the
 * LANGUAGE page offers DEUTSCH because the file is there. game/ is never touched.
 */
#include "wr.h"
#include "shaderpack/zip_read.h"
#include "gta2_translations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

/* 1 when the file at path has this size and CRC-32 */
static int same_file(const char *path, uint32_t size, uint32_t crc)
{
    struct stat st;
    if (stat(path, &st) != 0 || (uint64_t)st.st_size != size)
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint32_t c = 0;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        c = zip_crc32(c, buf, n);
    fclose(f);
    return c == crc;
}

static void mkdirs_for(const char *file)
{
    char d[1200];
    snprintf(d, sizeof d, "%s", file);
    for (char *p = d + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
#if defined(_WIN32)
            _mkdir(d);
#else
            mkdir(d, 0755);
#endif
            *p = '/';
        }
}

void gta2_translations_init(void)
{
    char pak[1100], out[1100], err[256];
    snprintf(pak, sizeof pak, "%s/GTA2-translations.pak", g_wr_paths.root);
    snprintf(out, sizeof out, "%s/translations", g_wr_paths.data);
    ZipFile *z = zip_open(pak, err, sizeof err);
    if (!z) {
        wr_log("translations: no %s", pak);
        return;
    }
    int written = 0, kept = 0;
    for (int i = 0; i < zip_count(z); i++) {
        const ZipEntry *e = zip_entry(z, i);
        char rel[600], path[1800];
        if (e->dir || !zip_safe_path(e->name, rel, sizeof rel))
            continue;
        snprintf(path, sizeof path, "%s/%s", out, rel);
        if (same_file(path, e->usize, e->crc)) {
            kept++;
            continue;
        }
        mkdirs_for(path);
        if (zip_extract_to_file(z, i, path, err, sizeof err))
            written++;
        else
            wr_log("translations: %s: %s", e->name, err);
    }
    zip_close(z);
    wr_path_add_read_dir(out);
    wr_log("translations: %s (%d files unpacked, %d up to date)", pak, written, kept);
}
