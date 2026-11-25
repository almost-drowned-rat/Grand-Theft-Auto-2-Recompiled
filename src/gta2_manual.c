/*
 * GTA 2 Recompiled - native replacements for individual guest functions.
 *
 * A `void sub_XXXXXXXX(RECOMP_PARAMS)` defined here is left out of the
 * generated code (game.toml [recomp] exclude_manual names this file), so
 * direct calls bind to it; wr_manual_register covers indirect calls.
 */
#include "wr.h"


/* 0x004031C0 GetDXVersion(DWORD *version, DWORD *platform), stdcall: the DX
 * SDK's detection that creates DirectDraw/DirectInput objects to tell the
 * installed DirectX apart. The runtime replaces DirectX, so answer what the
 * game needs (>= 0x601): DirectX 8.1 on the NT platform. */
void sub_004031C0(RECOMP_PARAMS)
{
    (void)base;
    uint32_t ver = WR_ARG(0), plat = WR_ARG(1);
    if (ver)
        wr_wr32(ver, 0x801);
    if (plat)
        wr_wr32(plat, 2);
    WR_RET(0, 2);
}

void gta2_manual_register(void)
{
    wr_manual_register(0x004031C0u, sub_004031C0);
}
