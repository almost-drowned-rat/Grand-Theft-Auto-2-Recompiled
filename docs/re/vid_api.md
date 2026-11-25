# DMAVideo (Dmavideo.dll) – Vid_* interface specification

Source: `extract/game/App_Executables/Dmavideo.dll` ("DMA Video (Direct Draw implementation). Version 1.8.1"),
disassembly `analysis/dlls/Dmavideo.asm`, consumer `gta2.exe` (`analysis/disasm/asm/text.asm`) and
`d3ddll.dll` (`analysis/dlls/d3ddll.asm`).

Tags: **[V]** = VERIFIED in code (address given). **[I]** = INFERRED (reason given).
DLL addresses are VAs in the DLL. gta2.exe addresses are `0x004xxxxx`, d3ddll addresses are `0xe0xxxx`.

> **Correction to the task brief:** the DLL's preferred image base is **0x01000000** (pefile `ImageBase`), not 0x10000000. All DLL
> addresses below (`0x01001000…`) are written in that base, as objdump prints them. [V]

DirectX level: the DLL creates `IDirectDraw` (v1) with `DirectDrawCreate` and QIs `IID_IDirectDraw4`
(`{9C59509A-39BD-11D1-8C4A-00C04FD930C5}` at 0x0100a130) → DirectX 6 interfaces (`IDirectDraw4`, `IDirectDrawSurface4`,
`DDSURFACEDESC2` size 0x7C, `DDCAPS` size 0x17C). `SetCooperativeLevel`/`SetDisplayMode`/`RestoreDisplayMode` go through the
**v1** object, `CreateSurface`/`CreateClipper`/`GetCaps`/`EnumDisplayModes` through the **v4** object. [V]

All exports are `__stdcall`. Most return `int` with **0 = success, 1 = failure** (DMA convention, *not* HRESULT); exceptions are noted.

---------------------------------------------------------------------------------------------------------------------------

## 1. Globals inside the DLL

| VA | meaning | evidence |
|---|---|---|
| 0x0100ffe8 | `HMODULE` given to `Vid_InitDLL` arg1 (gta2 passes the dmavideo module handle). Used as `hInstance` of the test window class and copied to `state+0x7C` | [V] 0x01002f08, 0x010015f8, 0x0100128f |
| 0x0100fff8 | pointer given to `Vid_InitDLL` arg2 (gta2's table of Vid fn-pointer slot addresses), copied to `state+0x84` | [V] 0x01002f0d, 0x01001604 |
| 0x0100ffe4 | HRESULT of the last `IDirectDraw::SetCooperativeLevel(hwnd, DDSCL_NORMAL)` done during screen tear-down. `Vid_SetGamma` (wrongly) tests this instead of its own HRESULTs | [V] 0x01001b0f, 0x01002d60 |
| 0x0100ffec | HWND of the "3DMA Windows Test" window (only with Init flag 0x40) | [V] 0x0100130a |
| 0x0100fff0 / 0x0100fff4 | debug live-object counters for IDirectDraw (v1) / IDirectDraw4; inc on create, dec on release, never read | [V] e.g. 0x01001735, 0x01001764 |
| 0x0100d020 | version block returned by `Vid_GetVersion` | [V] 0x01001000 |

Internal helpers: 0x01002f40 = `malloc`, 0x01002ff0 = `free`, 0x01002f30 = `jmp DirectDrawCreate`, 0x01002f36 = `jmp DirectDrawEnumerateA` [V];
0x010030e0 = CRT `_CIpow` (st1^st0), 0x010030b0 = `_ftol` [V by code shape + "pow" string at 0x0100d10c].

---------------------------------------------------------------------------------------------------------------------------

## 2. Data structures

### 2.1 `VidState` (the "screen" struct) – 0x4C4 bytes, `malloc`ed by `Vid_Init_SYS` (0x010015db), freed by `Vid_ShutDown_SYS`

gta2.exe keeps the pointer in **0x00673d20** [V 0x004CC711]. The same pointer is handed to the renderer with
`gbh_InitDLL(state)` [V 0x004D4754] (d3ddll stores it in 0xe13dc8 [V 0xe05442]).

| off | size | type / meaning | written by | read by (outside DLL) |
|---|---|---|---|---|
| 0x000 | 4 | always 1 (magic/version?) | Init_SYS [V 0x01001616] | – |
| 0x004 | 4 | **flags** (see 2.2) | Init_SYS, SetMode, Flip, Lock fns | gta2 writes bits 0x10/0x40/0x100 [V 0x004CC5F4, 0x004CC74E, 0x004CB606, 0x004CB846]; d3ddll reads bit 0 [V 0xe04bfd, 0xe0266c] |
| 0x008 | 4 | "result" width – output of CheckMode/FindMode/FindFirst/FindNext (0 on miss) | [V 0x01001937, 0x010019db, 0x01001a3e, 0x01001ab9] | – |
| 0x00C | 4 | "result" height (same functions) | [V] | – |
| 0x010 | 4 | "result" bpp (Check/FindMode) / **bpp filter** of FindFirst/FindNextMode | [V 0x01001931, 0x01001a05] | – |
| 0x014 | 4 | next mode id to assign (starts 1; ids are global over all devices) | [V 0x01001643, 0x0100141b] | – |
| 0x018 | 4 | next device id to assign (starts 2; primary = 1) | [V 0x0100164a, 0x01001575] | – |
| 0x01C | 4 | number of enumerated modes | [V 0x010013e6] | gta2 dead code 0x004CB8C9 |
| 0x020 | 4 | number of enumerated devices | [V 0x0100145f] | – |
| 0x024 | 4 | `VidMode*` mode list head | [V 0x01001404] | gta2 dead code 0x004CB8EA |
| 0x028 | 4 | `VidMode*` mode list tail | [V 0x0100140f] | – |
| 0x02C | 4 | `VidDevice*` device list head | [V 0x01001515] | – |
| 0x030 | 4 | `VidDevice*` device list tail | [V 0x01001521] | – |
| 0x034 | 4 | **current device id** (0 = none; DirectDraw objects exist only while ≠0) | SetDevice/SetMode [V 0x01001d1b, 0x010020da, 0x01002408] | d3ddll SetMode wrapper writes 0 [V 0xe04d8f] |
| 0x038 | 4 | current bpp (from the created back buffer's DDSURFACEDESC2) | SetMode [V 0x010022c7, 0x01002661] | – |
| 0x03C | 4 | FindFirst/FindNextMode cursor (`VidMode*` of the record after the last hit) | [V 0x01001a38, 0x01001ab3] | – |
| 0x040 | 4 | **current mode id**; 0 = no screen open, **-2 (0xFFFFFFFE) = windowed** | SetMode/CloseScreen [V 0x010022ca, 0x01002614, 0x01001b46] | gta2 [V 0x004CAEEC, 0x004CB15F]; dead code 0x004CB925 |
| 0x044 | 4 | zeroed by Init_SYS, never used | [V 0x0100165a] | – |
| 0x048 | 4 | **screen width** (fullscreen: mode width; windowed: client width) | SetMode [V 0x010022d4, 0x01002623] | gta2 ClearScreen args, viewport [V 0x004CC78A, 0x004CAEFF]; d3ddll [V 0xe04abd] |
| 0x04C | 4 | **screen height** | SetMode [V 0x010022e5, 0x0100262a] | gta2 [V 0x004CAED4 (MakeScreenTable), 0x004CC787]; |
| 0x050 | 4 | **locked surface pointer** (`lpSurface` of back buffer) while writes enabled, else 0 | Enable/DisableWrites, GetSurface [V 0x01002a1a, 0x01002b00, 0x01002a55] | gta2 MakeScreenTable [V 0x004CAEDA]; BinkCopyToBuffer [V 0x00481E52] |
| 0x054 | 4 | **pitch in bytes** (`lPitch`) while writes enabled | [V 0x01002a1d, 0x01002afd] | gta2 [V 0x004CAED7, 0x00481E4F] |
| 0x058 | 4 | red shift (bit index of lowest set bit of dwRBitMask) | SetMode [V 0x010022de, 0x01002631] | ClearScreen; d3ddll SetColourDepth [V 0xe04e58] |
| 0x05C | 4 | red bit count | [V] | gta2 Bink format select [V 0x00481F68]; d3ddll [V 0xe04e61] |
| 0x060 | 4 | green shift | [V] | d3ddll [V 0xe04e2a] |
| 0x064 | 4 | green bit count | [V] | gta2 [V 0x00481F74]; d3ddll |
| 0x068 | 4 | blue shift | [V] | d3ddll [V 0xe04de3] |
| 0x06C | 4 | blue bit count | [V] | gta2 [V 0x00481F7B]; d3ddll |
| 0x070 | 4 | "extra" bits shift (lowest bit not covered by R|G|B within bpp; e.g. alpha/unused bit of 1555) | [V 0x01002308, 0x0100265b] | – |
| 0x074 | 4 | "extra" bit count | [V] | – |
| 0x078 | 4 | Init_SYS arg1 (gta2: its `hInstance`); never used by the DLL | [V 0x0100161f] | – |
| 0x07C | 4 | `HMODULE` of dmavideo (from InitDLL arg1) | [V 0x01001601] | **d3ddll does `GetProcAddress(state->0x7C, "Vid_…")` for all 22 exports** [V 0xe04f19..] |
| 0x080 | 4 | 1 = fullscreen (exclusive), 0 = windowed | SetMode [V 0x01001df7] | – |
| 0x084 | 4 | `void***` gta2 slot table (InitDLL arg2) | [V 0x0100160e] | **d3ddll patches gta2's Vid fn pointers through it** (§6) [V 0xe05462] |
| 0x088 | 4 | last HRESULT (scratch) | everywhere | – |
| 0x08C | 4 | `IDirectDraw*` (v1) | DirectDrawCreate [V 0x01001724, 0x01002066] | d3ddll QI → IDirect3D3 [V 0xe01335] |
| 0x090–0x11F | 0x90 | never touched by the DLL | [V by offset scan] | ? |
| 0x120 | 4 | `IDirectDraw4*` | QI [V 0x01001755, 0x01002097] | d3ddll GetCaps / GetAvailableVidMem / GetDeviceIdentifier [V 0xe023d0, 0xe02413, 0xe02560] |
| 0x124–0x133 | 0x10 | never touched | [V] | ? |
| 0x134 | 4 | `IDirectDrawSurface4*` **primary** (front) | SetMode [V 0x010021a9, 0x01002479] | gta2 `BinkBufferSetDDPrimary(state->0x134)` [V 0x00481FD4] |
| 0x138 | 4 | `IDirectDrawSurface4*` **back buffer** (fullscreen: attached back buffer; windowed: offscreen surface) | SetMode [V 0x01002272, 0x010025b3] | d3ddll render target [V 0xe0138b and 6 more] |
| 0x13C | 0x7C | `DDSURFACEDESC2` – used as CreateSurface desc **and** as Lock desc of the back buffer. Sub-fields used: 0x140 dwFlags, 0x144 dwHeight, 0x148 dwWidth, 0x14C lPitch, 0x150 dwBackBufferCount, 0x160 lpSurface, 0x1A4 ddsCaps.dwCaps | [V] | via 0x50/0x54 |
| 0x1B8 | 4 | `IDirectDrawClipper*` (windowed only; never released → leak) | [V 0x010024a6] | – |
| 0x1BC–0x1C7 | 0xC | never touched | [V] | ? |
| 0x1C8 | 0x17C | `DDCAPS` driver (HAL) caps of the current device (dwSize=0x17C). 0x1D0 = dwCaps2, 0x204 = dwVidMemTotal, 0x208 = dwVidMemFree | Init_SYS / mode-record helper [V 0x010017ac, 0x010011c9] | d3ddll re-fills it [V 0xe023e6] and tests 0x1CC (dwCaps) bit 0x80000000 [V 0xe047d3] |
| 0x344 | 0x17C | `DDCAPS` HEL caps | same | d3ddll |
| 0x4C0 | 4 | `HWND` passed to SetMode | [V 0x01001d78] | – |

### 2.2 `VidState.flags` (+0x04)

| bit | set by | meaning |
|---|---|---|
| 0x00000001 | Lock fns | back buffer currently **locked** [V 0x010026f0, 0x01002744] |
| 0x00000002 | Enable/DisableWrites | **writes enabled** (0x50/0x54 valid) [V 0x01002a05] |
| 0x00000010 | gta2 (`tripple_buffer` setting, 0x0067359c) | request 2 back buffers (triple buffering) [V gta2 0x004CC5EC; DLL 0x01002191] |
| 0x00000020 | Init_SYS (always) | allow 1-back-buffer fallback [V 0x01001622, 0x010021d0] |
| 0x00000040 | Init_SYS arg2&0x40, and gta2 when renderer==d3ddll (0x00673580==1) | create surfaces with `DDSCAPS_3DDEVICE` (D3D render target) [V gta2 0x004CC749/0x004CC74E; DLL 0x01002184, 0x01002586] |
| 0x00000080 | never set by anyone seen | fullscreen: no system-memory fallback; windowed: back buffer in video memory [V 0x010021fe, 0x0100254b] |
| 0x00000100 | gta2 before every SetMode | **not tested anywhere in Dmavideo** [V by search]; purpose unknown (see open questions) |
| 0x00000200 | Init_SYS (always) | Flip with `DDFLIP_NOVSYNC` (flags 9 instead of 1) [V 0x01001622, 0x010027ff] |
| 0x10000000 | Flip/ClearScreen | a surface was found lost and `Restore()`d during the last check [V 0x01002798, 0x010027ca, 0x01002ce8] |
| 0x20000000 | SetMode | back buffer is in video memory [V 0x01002144, 0x01002569] |
| 0x40000000 | SetMode | fullscreen chain was created with 1 back buffer [V 0x010021ca] |
| 0x80000000 | SetMode | fullscreen chain was created with 2 back buffers (cleared on fallback) [V 0x01002144, 0x010021ba] |

### 2.3 `VidMode` record – 0x40 bytes, singly linked, built from `DDSURFACEDESC2` by helper 0x01001010

| off | meaning | source in DDSURFACEDESC2 |
|---|---|---|
| 0x00 | mode id (1..N, unique across devices; -2 used only in the temporary record for windowed) | `state+0x14++` [V 0x01001412] |
| 0x04 | device id the mode belongs to | `state+0x34` [V 0x0100142c] |
| 0x08 | width | dwWidth (+0x0C) [V 0x01001021] |
| 0x0C | height | dwHeight (+0x08) [V 0x01001027] |
| 0x10 | pitch as reported by EnumDisplayModes | lPitch (+0x10) [V 0x01001033] |
| 0x14 | bpp | ddpf.dwRGBBitCount (+0x54) [V 0x0100102d] |
| 0x18/0x1C | red shift / red bits | from dwRBitMask (+0x58) [V 0x01001085] |
| 0x20/0x24 | green shift / bits | dwGBitMask (+0x5C) [V 0x010010b4] |
| 0x28/0x2C | blue shift / bits | dwBBitMask (+0x60) [V 0x010010e3] |
| 0x30/0x34 | extra shift / bits = lowest run of `~(R|G|B)` within bpp; 0/0 if none or if shift ≥ bpp | [V 0x010010f2–0x01001168] |
| 0x38 | next | [V 0x01001409] |
| 0x3C | always 1 | [V 0x0100141e] |

Bit counting: "shift" = number of trailing zero bits, "bits" = length of the following run of ones [V]. If dwRBitMask == 0
(palettised 8-bit mode) all of 0x18..0x2C are set to 0 and the function returns *before* writing 0x30/0x34 (left as is) [V 0x01001051].
If arg3 (state) ≠ NULL the helper additionally re-reads DDCAPS of the current device into state+0x1C8/0x344 and writes
`device->vidmem = dwVidMemFree` (0x208); on GetCaps failure it releases `state+0x8C` (!) [V 0x01001177–0x010011f1].

Mode enumeration: `IDirectDraw4::EnumDisplayModes(0, NULL, state, cb=0x01001340)` once per device during `Vid_Init_SYS`
[V 0x010017f4]. Every reported mode (all bpp incl. 8/24/32, all refresh duplicates) is appended; callback always returns 1
(DDENUMRET_OK) [V 0x01001435].

### 2.4 `VidDevice` record – 0x2E + strlen(name) + strlen(desc) bytes, built in DirectDrawEnumerate callback 0x01001450

| off | meaning |
|---|---|
| 0x00 | device id: **1 = primary (GUID NULL)**, others 2,3,… [V 0x01001530, 0x0100156e] |
| 0x04 | flags; bit0 = `DDCAPS2_CANRENDERWINDOWED` (0x80000 in HAL dwCaps2) [V 0x010017bf–0x010017cb] |
| 0x08 | `char*` driver name (lpDriverName, points to +0x2C) [V 0x010014a9] |
| 0x0C | `char*` driver description (lpDriverDescription, after name) [V 0x010014c1] |
| 0x10 | next [V 0x0100151a] |
| 0x14 | `GUID*` for DirectDrawCreate: NULL for primary, else → +0x18 [V 0x01001528, 0x01001552] |
| 0x18 | GUID (16 bytes; zero for primary) |
| 0x28 | video memory: dwVidMemTotal after Init_SYS [V 0x010017d4], later overwritten with dwVidMemFree by SetMode [V 0x010011d8] |
| 0x2C | string storage |

Device enumeration: `DirectDrawEnumerateA(cb, state)` (or `DirectDrawEnumerateExA(wrapper 0x01001590, state, 7)` if
Init flag 0x400 and the export exists in ddraw.dll) [V 0x01001671–0x01001695].

### 2.5 Version block (0x0100d020)

`struct { float version; /* 1.81f, 0x3FE7AE14 */ char text[]; /* "DMA Video (Direct Draw implementation). Version 1.8.1" */ }` [V hexdump].

---------------------------------------------------------------------------------------------------------------------------

## 3. Exports

Prototypes use `VidState*` = §2.1. `HRESULT` returns are raw DirectDraw codes.

### Vid_GetVersion – 0x01001000
`const VidVersion* __stdcall Vid_GetVersion(void);` returns 0x0100d020 [V]. gta2 resolves it but never calls it [V: no call through slot 0x0059501c].

### Vid_InitDLL – 0x01002f00
`int __stdcall Vid_InitDLL(HMODULE hSelf, void*** slotTable);` stores the args in 0x0100ffe8 / 0x0100fff8, returns 0 [V].
gta2: `Vid_InitDLL(hDmavideo /*LoadLibrary result 0x006752e8*/, (void***)0x00675300)` at **0x004D34FF** [V]. Must be called before
Init_SYS (Init_SYS copies both into the state).

### Vid_Init_SYS – 0x010015b0
`VidState* __stdcall Vid_Init_SYS(HINSTANCE appInst, uint32_t initFlags);` [V]
1. `LoadLibraryA("ddraw.dll")`; NULL → return NULL. Immediately `FreeLibrary` (presence check only; later `GetProcAddress` uses the freed handle, works because DDRAW is statically imported) [V 0x010015be–0x010015d5].
2. malloc 0x4C4 (NULL → return NULL); fields: [0]=1, 0x04 = `(initFlags & 0x40) | 0x220`, 0x14=1, 0x18=2, 0x78=appInst, 0x7C=g_hModule, 0x84=g_slotTable, everything else used = 0 [V 0x010015f8–0x01001669].
3. Device enumeration (§2.4). If it fails: frees the state and then continues with NULL → would crash unless flag 0x800 (bug) [V 0x010016a4–0x010016bc].
4. Unless `initFlags & 0x800`: for each device: `state+0x34=id`; `DirectDrawCreate(guid,&dd1,NULL)`; `dd1->QueryInterface(IID_IDirectDraw4,&dd4)`; `dd4->GetCaps(&caps@0x1C8,&hel@0x344)`; set device flag/vidmem; `dd4->EnumDisplayModes(0,NULL,state,0x01001340)`; release dd4 and dd1; `state+0x34=0` [V 0x010016db–0x01001858]. Any Create/QI/GetCaps failure → returns NULL (state leaked) [V 0x01001744, 0x01001772, 0x010017e1].
5. Returns the state. **No DirectDraw object is alive afterwards.**

initFlags: 0x40 = 3D (also makes the mode callback create a test window "3DMA Windows Test"/class "MY_MENU", try a `PRIMARY|3DDEVICE` surface and hit an **`int3`** at 0x010013bd – debug leftover), 0x400 = use DirectDrawEnumerateExA, 0x800 = skip per-device caps/mode enumeration [V].
gta2: `Vid_Init_SYS(hInstance /*0x00673e28, WinMain arg1*/, 0)` at **0x004CC705**, result → 0x00673d20 [V].

### Vid_FindDevice – 0x01001950
`VidDevice* __stdcall Vid_FindDevice(VidState*, int deviceId);` linear search of the device list, NULL if not found [V].
gta2 0x004CB6FD: `dev = Vid_FindDevice(state, videodevice /*0x00673588*/); return dev && (dev->flags & 1);` = "device can render windowed" [V 0x004CB707].

### Vid_SetDevice – 0x01001b60
`int __stdcall Vid_SetDevice(VidState*, int deviceId);` [V]
- `id == state->curDevice` → return 0.
- If a device is current: if a screen is open (0x40≠0 && dd1 && primary) do the CloseScreen sequence (§Vid_CloseScreen); release dd4, dd1; curDevice=0.
- `id == 0` → return 0.
- Look up the device (**NULL deref if id unknown**, 0x01001c5a), release leftovers, `DirectDrawCreate(dev->guid,&state->dd1,NULL)`, QI IDirectDraw4 → `state->dd4`. Fail → release, return 1. Success → curDevice=id, return 0.
gta2: `Vid_SetDevice(state, videodevice)` at **0x004CC718**, result ignored [V].

### Vid_CheckMode – 0x010018d0
`int __stdcall Vid_CheckMode(VidState*, int width, int height, int bpp);` returns the **mode id** of the first record with exact
w/h/bpp whose device == curDevice (or any device if curDevice==0); also writes state+0x08/0x0C/0x10. 0 if not found (and zeroes them) [V].
gta2: `id = Vid_CheckMode(state, full_width, full_height, 16)` at **0x004CB58A**; fallback `Vid_CheckMode(state,640,480,16)` at **0x004CB5C4** [V]. gta2 only ever asks for **16 bpp**.

### Vid_FindMode – 0x01001980
`VidMode* __stdcall Vid_FindMode(VidState*, int modeId);` returns the **record pointer** (not the id) for `modeId` on the current device; writes 0x08/0x0C/0x10. `modeId == -2` with curDevice > 1 → 0 immediately; -2 is never in the list, so FindMode(-2) always returns 0 [V]. Not called by gta2 [V].

### Vid_FindFirstMode – 0x010019f0 / Vid_FindNextMode – 0x01001a70
`int __stdcall Vid_FindFirstMode(VidState*, int bpp);` `int __stdcall Vid_FindNextMode(VidState*);`
Iterator over modes with exactly `bpp` (stored as filter in state+0x10) on the current device. Return the mode id (0 = end),
write w/h to 0x08/0x0C, keep cursor in 0x3C. At end they clear 0x10 and 0x3C (FindFirst also 0x08/0x0C) [V]. Not called by gta2 or d3ddll [V].

### Vid_SetMode – 0x01001d30
`int __stdcall Vid_SetMode(VidState*, HWND hwnd, int modeId /* -2 = windowed */);` returns 0 = OK, 1 = fail; `state==NULL` → 1 [V].
Common: `UpdateWindow(hwnd)`; state+0x4C0 = hwnd; if a screen is open → CloseScreen sequence; state+0x80 = (modeId != -2) [V 0x01001d72–0x01001df7].

**Fullscreen (modeId ≠ -2)** [V 0x01001e03–0x01002311]:
1. If a device is current: release dd4/dd1, curDevice = 0 (DirectDraw is always re-created).
2. Find mode record by id (any device); miss → return 1. Writes 0x08/0x0C/0x10.
3. SetDevice(mode->device) inline (DirectDrawCreate + QI v4); fail → 1.
4. `dd1->SetCooperativeLevel(hwnd, 0x51 = DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN|DDSCL_ALLOWREBOOT)`; `dd1->SetDisplayMode(w,h,bpp)` (v1, 3 args); fail → 1.
5. flags |= 0xA0000000; desc@0x13C: dwFlags=`DDSD_CAPS|DDSD_BACKBUFFERCOUNT`, caps=`PRIMARYSURFACE|FLIP|COMPLEX|VIDEOMEMORY` (0x4218; +`3DDEVICE` = 0x6218 if flag 0x40).
   a. if flag 0x10: BackBufferCount=2, `dd4->CreateSurface`.
   b. else/fail: flags=(flags&0x3FFFFFFF)|0x40000000, count=1, try if flags&0x30.
   c. fail: flags &= 0x9FFFFFFF; if flag 0x80 → return 1; else drop VIDEOMEMORY, retry; fail → 1.
6. `primary->GetAttachedSurface({DDSCAPS_BACKBUFFER}, &state->back)` → 0x138; fail → 1.
7. `back->GetSurfaceDesc`, build mode record (helper with state → refreshes caps / device vidmem).
8. Writes 0x38 bpp, **0x40 = modeId**, 0x48 = w, 0x4C = h, 0x58..0x74 pixel format. Return 0.

**Windowed (modeId == -2)** [V 0x01002320–0x01002670]:
1. If no device current: create DirectDraw on device id 1 (primary) and set curDevice=1 (**the current device is kept if one is set**).
2. `dd1->SetCooperativeLevel(hwnd, DDSCL_NORMAL)`.
3. Primary: dwFlags=`DDSD_CAPS`, caps=`DDSCAPS_PRIMARYSURFACE` → 0x134.
4. `dd4->CreateClipper(0,&clip@0x1B8,NULL)`; `clip->SetHWnd(0,hwnd)`; `primary->SetClipper(clip)`.
5. `GetClientRect(hwnd)`; back buffer: dwFlags=`CAPS|HEIGHT|WIDTH` (7), size = client size, caps = `VIDEOMEMORY` (flag 0x80, sets 0x20000000) or `SYSTEMMEMORY` (clears 0x20000000); if flag 0x40: caps = (caps & ~SYSTEMMEMORY) | VIDEOMEMORY | 3DDEVICE and 0x20000000 set. Note: no `DDSCAPS_OFFSCREENPLAIN`. → 0x138.
6. GetSurfaceDesc + mode record; **0x40 = -2**, 0x48/0x4C = client size, 0x38 bpp, 0x58..0x74 (desktop pixel format). Return 0.

Failure paths leave already created surfaces in 0x134/0x138 with 0x40==0, so they are never released (leak) [V].
gta2 calls: fullscreen **0x004CB617** `Vid_SetMode(state, hwnd /*0x00673d18*/, id)`; windowed **0x004CB858** `Vid_SetMode(state, hwnd, -2)` [V]. Both go through slot 0x00595040, which **after d3ddll is loaded points to d3ddll 0xe04d80** (§6).

### Vid_CloseScreen – 0x01001ad0
`void __stdcall Vid_CloseScreen(VidState*);` [V] (eax undefined; callers ignore it.)
Only if `0x40 != 0 && dd1 && primary`: `dd1->RestoreDisplayMode()`; `dd1->SetCooperativeLevel(state->hwnd, DDSCL_NORMAL)` (hr → 0x0100ffe4); `primary->Release()`; if windowed also `back->Release()` (fullscreen back is attached → released with primary; the AddRef from GetAttachedSurface is leaked); 0x134 = 0x138 = 0x40 = 0. DirectDraw objects stay alive; clipper is never released.
gta2: **0x004CC5D5** (start of every mode (re)set), **0x004D0B98** (WM_KILLFOCUS, only when the device cannot render windowed, followed by `ShowWindow(hwnd, SW_SHOWMINNOACTIVE)`) [V]. Slot 0x0059503c → d3ddll 0xe04d00 after gbh_InitDLL.

### Vid_FlipBuffers – 0x01002750
`HRESULT __stdcall Vid_FlipBuffers(VidState*);` [V] (undefined return if state/surfaces NULL; gta2 never tests it.)
1. If `primary->IsLost()==DDERR_SURFACELOST`: flag 0x10000000, `Restore()`; restore failure → return its hr. Else clear 0x10000000. Same for back buffer (last result wins).
2. Fullscreen: `primary->Flip(NULL, DDFLIP_WAIT | (flags&0x200 ? DDFLIP_NOVSYNC : 0))` – i.e. **vsync is off by default** (Init_SYS always sets 0x200) [V 0x01002802].
3. Windowed: dst = client rect of state->hwnd converted to screen coords (`GetClientRect`+`ClientToScreen`), src = {0,0,state->0x48,state->0x4C}; `primary->Blt(&dst, back, &src, DDBLT_WAIT, NULL)` (clipper does occlusion) [V 0x01002828–0x010028cf].
gta2 per frame: **0x0045861D** (frontend), **0x00461971** (in-game), **0x00481E6A** (Bink movie), **0x004CC7A7** (init) [V].

### Vid_GrabSurface – 0x01002690
`void __stdcall Vid_GrabSurface(VidState*);` Lock without exposing the pointer: if not locked: restore primary if lost, zero desc@0x13C (dwSize 0x7C), `back->Lock(NULL, &desc, DDLOCK_WAIT, NULL)`; on success flag 0x01 [V]. Return value is garbage (Lock hr on failure, `flags|1` on success) [V]. Not called by gta2/d3ddll [V].

### Vid_ReleaseSurface – 0x01002700
`void __stdcall Vid_ReleaseSurface(VidState*);` if locked: `back->Unlock(NULL)`, restore primary if lost, clear 0x01 [V]. Not called directly by gta2/d3ddll.

### Vid_EnableWrites – 0x010029f0 / Vid_DisableWrites – 0x01002a40
`int __stdcall Vid_EnableWrites(VidState*);` requires locked && !writes: set 0x02, `0x50 = desc.lpSurface`, `0x54 = desc.lPitch`; return 0; otherwise 1 [V].
`int __stdcall Vid_DisableWrites(VidState*);` requires locked && writes: clear 0x02, `0x50 = 0` (0x54 kept); return 0; otherwise 1 [V]. Not called directly by gta2/d3ddll.

### Vid_GetSurface – 0x01002a70
`int __stdcall Vid_GetSurface(VidState*);` = GrabSurface + EnableWrites inline. Returns 0 when it enabled writes (0x50/0x54 valid), 1 if state NULL, lock failed, or writes already enabled [V]. **The pointer/pitch are not returned – callers read state+0x50 / state+0x54.** The locked surface is the **back buffer** (0x138).
gta2: wrapper sub_004CAEC0 – `Vid_GetSurface(state)` at **0x004CAEC9**, then `MakeScreenTable(state->0x50, state->0x54, state->0x4C)` (d3ddll, slot 0x00595324) and `gbh_SetWindow(0.f, 0.f, (float)(W-1), (float)(H-1))` where W,H = window_width/height (0x00673578/0x006732e8) if `state->0x40 == -2`, else state->0x48/0x4C; also stores W-1 → 0x00673584, H-1 → 0x00673474 [V 0x004CAEC0–0x004CAF38]. Called from 0x004585F3, 0x0045A5EF, 0x00481E2A [V].

### Vid_FreeSurface – 0x01002b30
`int __stdcall Vid_FreeSurface(VidState*);` = DisableWrites + ReleaseSurface inline; returns 0 if writes had been enabled, else 1; always unlocks if locked [V].
gta2: sub_004CAF50 → **0x004CAF56**; called at 0x00458612, tail-jump 0x0045A63E, 0x00481E5E [V].

### Vid_ClearScreen – 0x01002bb0
`HRESULT __stdcall Vid_ClearScreen(VidState*, int r, int g, int b, int left, int top, int right, int bottom);` [V]
- Color: each component `& 0xFF`, then `(c >> (8 - bits)) & ((1<<bits)-1)) << shift` using state 0x58/0x5C (r = arg2), 0x60/0x64 (g = arg3), 0x68/0x6C (b = arg4); OR'ed into `DDBLTFX.dwFillColor` [V 0x01002bc6–0x01002ca5].
- Rect = `RECT{left,top,right,bottom}` (args 5..8, DirectDraw convention: right/bottom exclusive) [V 0x01002bce–0x01002bf6].
- `back->Blt(&rect, NULL, NULL, DDBLT_COLORFILL, &fx)` (no DDBLT_WAIT). Loops while `DDERR_WASSTILLDRAWING`; on `DDERR_SURFACELOST`: `back->Restore()`, flag 0x10000000, rect := full screen {0,0,W,H}, retry if Restore succeeded. Returns the last hr [V 0x01002cae–0x01002d09].
- Only the **back buffer** is cleared.
gta2 always passes black and the full screen: `Vid_ClearScreen(state, 0,0,0, 0,0, state->0x48, state->0x4C)` at **0x0045863B, 0x004619BC, 0x00481E88, 0x004CC79A, 0x004CC7C5**, each right after a FlipBuffers (clears the new back buffer) [V].

### Vid_SetGamma – 0x01002d20
`int __stdcall Vid_SetGamma(VidState*, float gammaR, float gammaG, float gammaB);` [V]
- primary == NULL → 1.
- `primary->QueryInterface(IID_IDirectDrawGammaControl {69C11C3E-B46B-11D1-AD7A-00C04FC29B4E} @0x0100a210)`, `GetGammaRamp(0,&ramp)`, then for each channel `ramp[i] = (WORD)_ftol(pow(i/255.0f, 1.0f/gamma) * 65535.0f)`, i=0..255 (x starts at 0 and adds 1/255 per step), `SetGammaRamp(0,&ramp)`, Release, return 0.
- Error checks after each call test the stale global 0x0100ffe4, not the call's hr (bug): if QI fails the code still dereferences the interface pointer [V 0x01002d5a–0x01002ebb].
gta2: sub_004CB930 – `Vid_SetGamma(state, g, g, g)` with `g = (float)(regGamma * 0.1)` (registry "gamma", default 10 → 1.0) at **0x004CB952**; non-zero return arms a 30-frame retry counter 0x00673594 (sub_004CB9C0) [V].

### Vid_ShutDown_SYS – 0x010028e0
`void __stdcall Vid_ShutDown_SYS(VidState*);` CloseScreen sequence, release dd4 and dd1, free all mode records, all device records, the state [V].
gta2: sub_004D2DA0 – `Vid_ShutDown_SYS(state)` through slot 0x00595050 at **0x004D2DB4**, then `FreeLibrary(dmavideo)` and all 22 slots are reset to the stub 0x004D2CF0 ("A DMA Video Function was called without the DLL being loaded") [V]; caller sets 0x00673d20 = 0 [V 0x004CB8A4].

### Vid_WindowProc – 0x01002ef0
`LRESULT __stdcall Vid_WindowProc(VidState*, HWND, UINT msg, WPARAM, LPARAM);` → `xor eax,eax; ret 0x14` – **does nothing, returns 0** [V].
gta2: `Vid_WindowProc(state, hwnd, msg, wParam, lParam)` at **0x004D0DB9** for every message that falls through gta2's WndProc 0x004D0A00 (if state ≠ NULL), return value ignored, then `DefWindowProcA` [V; register mapping ebp=hwnd, esi=msg, ebx=wParam, edi=lParam from 0x004D0A04–0x004D0A13].

---------------------------------------------------------------------------------------------------------------------------

## 4. gta2.exe binding

Loader sub_004D2E50(name = registry `Screen\videoname`, buffer 0x006732f0): `LoadLibraryA` at 0x004D2F3B → 0x006752e8, 22× `GetProcAddress`
(missing → MessageBox "Can't Find Function Called %s", return -1), then `Vid_InitDLL` [V]. Slots and the slot-address table at 0x00675300
(table entry *i* holds the address of slot *i*; built at 0x004D2E56–0x004D2F31) [V]:

| tbl off | slot | export | | tbl off | slot | export |
|---|---|---|---|---|---|---|
| 0x00 | 0x0059501c | GetVersion | | 0x2C | 0x00595048 | ReleaseSurface |
| 0x04 | 0x00595020 | Init_SYS | | 0x30 | 0x0059504c | GrabSurface |
| 0x08 | 0x00595024 | CheckMode | | 0x34 | 0x00595050 | ShutDown_SYS |
| 0x0C | 0x00595028 | FindMode | | 0x38 | 0x00595054 | EnableWrites |
| 0x10 | 0x0059502c | FindFirstMode | | 0x3C | 0x00595058 | DisableWrites |
| 0x14 | 0x00595030 | FindNextMode | | 0x40 | 0x0059505c | GetSurface |
| 0x18 | 0x00595034 | FindDevice | | 0x44 | 0x00595060 | FreeSurface |
| 0x1C | 0x00595038 | SetDevice | | 0x48 | 0x00595064 | ClearScreen |
| 0x20 | 0x0059503c | CloseScreen | | 0x4C | 0x00595068 | WindowProc |
| 0x24 | 0x00595040 | SetMode | | 0x50 | 0x0059506c | InitDLL |
| 0x28 | 0x00595044 | FlipBuffers | | 0x54 | 0x00595070 | SetGamma |

Exports gta2 actually calls: InitDLL, Init_SYS, SetDevice, FindDevice, CheckMode, SetMode, CloseScreen, GetSurface, FreeSurface,
FlipBuffers, ClearScreen, SetGamma, WindowProc, ShutDown_SYS [V: all `call dword ptr [slot]` sites enumerated]. Never called by gta2:
GetVersion, FindMode, FindFirstMode, FindNextMode, GrabSurface, ReleaseSurface, EnableWrites, DisableWrites [V].
Dead code in gta2 that walks the mode list directly: 0x004CB8C0 `(index,&w,&h)` (uses state 0x1C/0x24, mode 0x00/0x08/0x0C/0x38) and
0x004CB920 `return state->0x40` – no references anywhere in the exe [V by byte search].

Relevant gta2 settings: 0x00595014 `start_mode` (0 = window, 1 = fullscreen), 0x006732e4/0x006732e0 `full_width/full_height`,
0x00673578/0x006732e8 `window_width/height`, 0x0067359c `tripple_buffer` (forced 1 for softdll), 0x00673588 `videodevice`,
0x00673580 renderer kind (0 = softdll.dll, 2 = 3dfx.dll, 1 = anything else, i.e. d3ddll) [V 0x004CB290–0x004CB4C7].

---------------------------------------------------------------------------------------------------------------------------

## 5. Call order

### Startup (WinMain sub_004D1170)
1. 0x004D128F sub_004CB3F0: registry → renderer/video names + device ids, settings [V].
2. 0x004D130B `CreateWindowExA` → hwnd 0x00673d18; ShowWindow [V].
3. 0x004D140F sub_004CC6D0 [V]:
   1. sub_004D2E50: LoadLibrary dmavideo, GetProcAddress×22, **Vid_InitDLL** (0x004D34FF).
   2. **Vid_Init_SYS**(hInstance, 0) 0x004CC705 → state.
   3. **Vid_SetDevice**(state, videodevice) 0x004CC718.
   4. state NULL → fatal error; renderer==1 → `state->flags |= 0x40` (0x004CC74E).
   5. desktop BITSPIXEL==16 && **Vid_FindDevice** says CANRENDERWINDOWED → windowed allowed (0x006735a4).
   6. sub_004CC5C0 = **mode set** (below).
   7. sub_004CAE50 → sub_004D4730: load d3ddll, **gbh_InitDLL(state)** (0x004D4754) – patches 4 slots (§6), gbh_GetGlobals.
   8. sub_004CB990: **Vid_SetGamma** (0x004CB952).
   9. **Vid_ClearScreen**(black, full) 0x004CC79A → **Vid_FlipBuffers** 0x004CC7A7 → **Vid_ClearScreen** 0x004CC7C5.
4. 0x004D142E `gbh_Init(0)` (d3ddll 0xe04ee0 → its D3D init 0xe02340 on the already-open screen) [V].

### Mode (re)set – sub_004CC5C0 [V]
`Vid_CloseScreen(state)` (0x004CC5D5) → set/clear flag 0x10 from tripple_buffer → if start_mode==0 and windowed allowed:
sub_004CB730 (SetWindowLong style 0x10CF0000, SetWindowPos/ShowWindow, size window so client = window_width×height, `flags |= 0x100`,
`Vid_SetMode(state, hwnd, -2)` 0x004CB858; on failure retry 640×480, then fall back to fullscreen and set start_mode=1) else
sub_004CB570 (`Vid_CheckMode(state, full_w, full_h, 16)`, fallback 640×480×16, `flags |= 0x100`, `Vid_SetMode(state, hwnd, id)` 0x004CB617,
error message if it fails, `SetWindowLong(GWL_STYLE, WS_VISIBLE)`, SetWindowPos/UpdateWindow/ShowWindow) → sub_004CB990 (SetGamma).
Triggers: startup; main loop when settings changed (sub_004CC6A0 from 0x004D14F6, followed by sub_004CB880 = gbh_SetColourDepth + viewport resize);
WM_SETFOCUS (0x004D0B01); a WM_SYSCOMMAND/WM_SYSKEYDOWN branch (0x004D0D7D, after forcing start_mode=1 via sub_004CB4D0) [V call sites; which key/command triggers it is [I], not traced].
After step 3.7 the SetMode/CloseScreen calls go to the d3ddll wrappers.

### Focus loss – WM_KILLFOCUS (case 8 of jump table 0x004D0E10) [V 0x004D0B2B–0x004D0BAE]
If `!(FindDevice(videodevice)->flags & 1)`: `Vid_CloseScreen` + `ShowWindow(SW_SHOWMINNOACTIVE)`; flag 0x006735a5 = 1 makes the next
WM_SETFOCUS / settings pass re-run the mode set.

### Per frame [V]
- Frontend sub_004585F0: GetSurface-wrapper 0x004CAEC0 (**Vid_GetSurface**, MakeScreenTable, gbh_SetWindow) → gbh_BeginScene → draw → gbh_EndScene → **Vid_FreeSurface** (0x004CAF56) → **Vid_FlipBuffers** (0x0045861D) → **Vid_ClearScreen** (0x0045863B).
- In-game sub_0045A5A0 + sub_00461960: 0x004CAEC0 → gbh_BeginScene → world/HUD → gbh_EndScene → FreeSurface (tail jump 0x0045A63E); then FlipBuffers (0x00461971) and, only if some overlay flags are set, ClearScreen (0x004619BC).
- d3ddll `gbh_BeginScene` (0xe04bf0): if `state->flags & 1` (locked) it calls **Vid_FreeSurface** itself (0xe04c0c) and `gbh_EndScene` (0xe04c70) re-locks with **Vid_GetSurface** (0xe04c8b). So with the D3D renderer the lock is only held outside BeginScene/EndScene; the CPU pointer is meaningful for software renderers and Bink.
- Bink sub_00481E00: if renderer==3dfx (2): 0x004CAEC0 (lock), `BinkCopyToBuffer(bink, state->0x50, state->0x54, bink->height, 0, 0, fmt|0x4000000)`, FreeSurface, Flip, Clear; pixel format chosen from state 0x5C/0x64/0x6C (5/5/5 → 2, 5/6/5 → 3/4…, 0x00481F68–0x00481FCB). Otherwise BinkBuffer path with `BinkBufferSetDDPrimary(state->0x134)` (0x00481FDB) [V].

### Shutdown [V]
WM_DESTROY (case 2, 0x004D0C74), fatal error (0x00450547) and 0x00462825 → sub_004CB890: sub_004D3730 (`gbh_CloseDLL`, FreeLibrary d3ddll) →
sub_004D2DA0: **Vid_ShutDown_SYS(state)** 0x004D2DB4, FreeLibrary dmavideo, slots → stub → state global = 0.

---------------------------------------------------------------------------------------------------------------------------

## 6. What the renderer (d3ddll) takes from Vid

d3ddll imports no DirectDraw; everything comes from `VidState` handed over by `gbh_InitDLL(VidState*)` (0xe053f0) [V]:
- **Its own Vid_ function table**: `GetProcAddress(state->0x7C /*dmavideo HMODULE*/, "Vid_…")` for all 22 names into 0xe10788…0xe107d8 (sub 0xe04f10) [V]. Used: CloseScreen (0xe107a8), SetMode (0xe107ac), GetSurface (0xe107c8), FreeSurface (0xe107cc) [V call sites 0xe04d72, 0xe04d96, 0xe04db5/0xe04c8b, 0xe04dc5/0xe04c0c].
- **Patches gta2's slots via state->0x84** [V 0xe0545c–0xe054aa]:
  - tbl+0x20 (CloseScreen) → 0xe04d00: free all textures/palettes, release D3D device objects (0xe017a0), then real Vid_CloseScreen.
  - tbl+0x24 (SetMode) → 0xe04d80: `state->0x34 = 0` (forces DirectDraw re-creation even in windowed mode), real Vid_SetMode, and on success (0) re-initialises Direct3D (0xe02340).
  - tbl+0x40 (GetSurface) → 0xe04db0, tbl+0x44 (FreeSurface) → 0xe04dc0: plain pass-through.
- **COM objects**: `state->0x8C` IDirectDraw → QI for the D3D interface at 0xe0d488 (IDirect3D3 [I: DX6 level, QI on DD object]) [V 0xe01335]; `state->0x138` back buffer = render target (GetSurfaceDesc → bit depth for device enumeration) [V 0xe0138b]; `state->0x120` IDirectDraw4: `GetCaps` into state 0x1C8/0x344 (overwrites Vid's copies) [V 0xe023e6], `GetAvailableVidMem` [V 0xe0242b], `GetDeviceIdentifier` → vendor/device quirks (0x12D2 dev 0x18/0x19, 0x1002 dev 0x4742, 0x104C dev 0x3D07) [V 0xe02569–0xe0260d].
- **Plain fields**: flags bit0 (locked) [V 0xe04bfd, 0xe0266c], 0x48 width [V 0xe04abd], 0x58–0x6C pixel format → its 16-bit colour packing tables (gbh_SetColourDepth 0xe04dd0) [V], 0x1CC HAL dwCaps & DDCAPS_CANBLTSYSMEM(0x80000000) [V 0xe047d3].

**Native reimplementation consequence [I]:** a single native module should own window/swapchain (Vid) and renderer (gbh). The
contract gta2 relies on is: the slots behave like the *wrapped* versions (SetMode also (re)creates the renderer, CloseScreen also
drops renderer resources), state+0x48/0x4C/0x40 reflect the screen, state+0x50/0x54 give a CPU-writable 16-bpp back buffer between
GetSurface/FreeSurface (MakeScreenTable / Bink 3dfx path / software renderer), state+0x58..0x6C describe its 16-bit format (use 5-6-5:
shifts 11/5/0, bits 5/6/5), Flip presents, ClearScreen fills the back buffer, state+0x134 is only needed for the BinkBuffer path.

---------------------------------------------------------------------------------------------------------------------------

## 7. Open questions

1. **Flag 0x100** is set by gta2 before every SetMode (0x004CB603, 0x004CB843) but tested by neither Dmavideo nor (by a byte-level search of `+0x5`/`ah,1` patterns) d3ddll. Possibly read by softdll.dll/3dfx.dll (not analysed) or a leftover.
2. Struct regions 0x090–0x11F, 0x124–0x133, 0x1BC–0x1C7 are untouched by Dmavideo; whether d3ddll/other renderers use them was not exhaustively checked (only `[reg+off]` loads right after loading 0xe13dc8 were scanned).
3. IID at d3ddll 0xe0d488 was not decoded byte-by-byte (assumed IID_IDirect3D3); the rest of d3ddll's D3D setup (0xe01300/0xe01b90) is out of scope here.
4. `Vid_GrabSurface`/`Vid_ReleaseSurface` return values are undefined (eax garbage); prototypes given as `void`. Same for CloseScreen/ShutDown/FlipBuffers-early-exit.
5. VidMode +0x3C is always 1 and never read – meaning unknown (maybe "valid"). VidState +0x00 = 1 likewise.
6. Whether DirectDraw `EnumDisplayModes` fills `lPitch` for VidMode +0x10 on real drivers (field is never read, so irrelevant for the port).
7. The exact value semantics of gta2's Bink surface-type constants (0x00664ff8 = 2/3/4/5) vs. Bink's BINKSURFACE* enum were not cross-checked.
