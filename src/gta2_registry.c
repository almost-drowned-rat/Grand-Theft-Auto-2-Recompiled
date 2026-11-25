/*
 * GTA 2 Recompiled - registry defaults.
 *
 * GTA 2 reads its settings from HKLM\SOFTWARE\DMA Design Ltd\GTA2, which
 * "gta2 manager.exe" writes on a real install. The runtime keeps the registry
 * in saves/registry.txt; these defaults fill in what is missing.
 */
#include <stdint.h>

void wr_reg_default_dword(const char *key, const char *name, uint32_t value);
void wr_reg_default_str(const char *key, const char *name, const char *s);

#define K "HKEY_LOCAL_MACHINE\\SOFTWARE\\DMA Design Ltd\\GTA2"

void gta2_registry_defaults(void)
{
    wr_reg_default_str(K "\\Screen", "rendername", "d3ddll.dll");
    wr_reg_default_str(K "\\Screen", "videoname", "dmavideo.dll");
    wr_reg_default_dword(K "\\Screen", "full_width", 640);
    wr_reg_default_dword(K "\\Screen", "full_height", 480);
    wr_reg_default_dword(K "\\Screen", "window_width", 640);
    wr_reg_default_dword(K "\\Screen", "window_height", 480);
}
