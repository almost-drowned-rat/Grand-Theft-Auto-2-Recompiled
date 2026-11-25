# GBH renderer (d3ddll.dll) – gbh_* interface specification

Target: native Vulkan 1.1 reimplementation of `d3ddll.dll` inside the gta2.exe static recompilation.

Conventions
- **[V addr]** = verified in code at that address; **[I]** = inferred (reason given). Every statement carries one of the two tags.
- **Image base of d3ddll.dll is 0x00E00000** (pefile `OptionalHeader.ImageBase` = 0xe00000; also the base used in `analysis/dlls/d3ddll.asm`) [V]. Neither 0x10000000 (first brief) nor 0x01000000 (the later correction; that is Dmavideo.dll's base) is right. All d3ddll addresses below are real VAs at 0xe00000. gta2.exe addresses are 0x4xxxxx/0x5xxxxx/0x6xxxxx.
- `VidState` = Dmavideo's 0x4C4-byte screen struct, see `docs/re/vid_api.md` §2.1. Only the fields d3ddll uses are repeated here.
- All exports are `__stdcall` [V: `ret N` of every export]. "float" args really are IEEE singles (loaded with `fld dword`), "int" args are integers.
- Scratch disassembly used: `$SCRATCH/re_gbh/d3d.dis` (capstone, annotated); exe: `analysis/disasm/asm/text.asm`.

---------------------------------------------------------------------------------------------------------------

## 0. Summary for the Vulkan port (all points are proven below)

1. **API**: DirectX 6. `IDirectDraw4` + `IDirect3D3` + `IDirect3DDevice3` + `IDirect3DViewport3` + `IDirect3DTexture2` + `IDirectDrawSurface4` [V, §5].
2. **Primitive**: `IDirect3DDevice3::DrawPrimitive(type, 0x1C4, verts, n, 8)`; FVF 0x1C4 = XYZRHW|DIFFUSE|SPECULAR|TEX1 = `D3DTLVERTEX`, stride 32. Triangles = TRIANGLELIST(3 verts), quads = TRIANGLEFAN(v0,v1,v2,v3) [V 0xe0437a, 0xe031bb, 0xe03c15, 0xe04653].
3. **Pre-transformed screen-space vertices**; the game projects itself. `sz` = `rhw` = the caller's `+0x08` value (≈ 1/depth). **No z-buffer** (ZENABLE never set, no Z surface created) → pure painter's order. **TEXTUREPERSPECTIVE = on** → texture coordinates (and colours) must be interpolated perspective-correct with w = 1/rhw. Viewport 0..W × 0..H, MinZ 0, MaxZ 500 [V §5.4].
4. **Blend modes** from flag bits: none → opaque; `0x080`/`0x100` → SRCALPHA/INVSRCALPHA; `0x200` → ONE/ONE additive. Colour op = texture × diffuse (RGB and A) [V §1.3].
5. **Textures** are 8-bit indexed (pitch 256) + 256-entry palettes `0x00RRGGBB`; index 0 = transparent (alpha 0) in the alpha variant, black opaque otherwise. Filtering bilinear unless flag `0x20000`/`0x10000` → point. Address mode CLAMP. No mipmaps [V §2].
6. **Fixed-function extras**: CULL NONE, DITHER on, no fog, no alpha test, no specular (specular colour forced to 0), shading GOURAUD [V §5.4].
7. **Software lighting** (flag `0x8000`) is done by the DLL on the CPU before drawing: per-vertex point lights with linear fall-off + ambient; needs world positions the game stores *after* the 4 screen vertices (`+0x80`) [V §1.4].
8. **The DLL writes into the caller's vertex array** (rhw, colour, specular, normalised UVs, DrawTile UV permutation, sprite-mode vertices 1–3). gta2 rebuilds the array before each call, so a reimplementation may work on a copy, but must produce identical results [V].
9. **FPU control word**: `gbh_InitDLL` switches the x87 to **24-bit precision + round-toward-−∞** for the lifetime of the DLL (restored in `gbh_CloseDLL`). Because gta2.exe's own x87 code runs in the same thread, every `fistp`/float→int in gta2 after InitDLL rounds down in single precision. The recomp must emulate that control word (or the game's math/rounding will differ) [V 0xe0540d–0xe0542f; consequence for exe code = I, same thread/same FPU].
10. Flip/present is not in this DLL: gta2 calls `Vid_FlipBuffers` after `gbh_EndScene` [V §6].

---------------------------------------------------------------------------------------------------------------

## 1. Vertex format, draw functions and the `flags` word

### 1.1 `GBH_Vertex` (0x20 bytes, array passed by pointer) – identical to `D3DTLVERTEX`

| off | type | in/out | meaning |
|---|---|---|---|
| 0x00 | float | in | screen x in pixels (back-buffer coordinates) [V 0xe0403f, D3D FVF] |
| 0x04 | float | in | screen y in pixels [V] |
| 0x08 | float | in | "z" = rhw-like value. Used as D3D `sz` **and** copied into `rhw` (+0x0C) [V 0xe042b8/0xe042d7, 0xe030e4/0xe03106]. If vertex 0's value is ≤ 0 the whole primitive is dropped [V 0xe0402b, 0xe02ccb, 0xe03501]. gta2 computes it as `1.0 / (8.0 − z_world + cam->0xA0)` (z in blocks) [V gta2 0x0046BC99–0x0046BCC7], i.e. 1/depth; screen x/y are `world*scale*rhw + centre` [V 0x0046BCDE–0x0046BD33]. |
| 0x0C | float | out | rhw – overwritten with +0x08 [V] |
| 0x10 | u32 | in/out | diffuse `D3DCOLOR` 0xAARRGGBB. Input **only if flags & 0x2000**; otherwise overwritten with `0xFF000000 | s<<16 | s<<8 | s`, s = `shade & 0xFF` [V 0xe04313–0xe04334, 0xe0314c–0xe03170]. Then modified by lighting if flags & 0x8000 [V]. |
| 0x14 | u32 | out | specular – always overwritten with 0 [V 0xe0433a, 0xe03179] |
| 0x18 | float | in/out | u in **texels** of the registered texture; overwritten with `u * (1/cacheSize)` [V 0xe042bb–0xe04310, 0xe030c4–0xe03149] |
| 0x1C | float | in/out | v in texels; same scaling [V] |

`cacheSize` = edge of the square power-of-two cache texture that holds the image (8,16,32,64,128; §2.4); the image is uploaded at texel (0,0) of it [V 0xe061d0], so normalised uv = texel/cacheSize. A Vulkan port that uses exactly-sized images must use `u/width_of_cache_class`, not `u/w`, only if it reproduces the padding; simplest exact equivalent: allocate each texture at its cache size (next pow2 ≥ max(w,h), §2.4) with the same edge padding.

**World-position block (lighting only)**: for each screen vertex *i* the DLL reads `float wx, wy, wz` at `verts + 0x80 + i*0x20` (+0,+4,+8) [V 0xe02afc–0xe02b0a: `edi = v_i+0x10`, reads `edi+0x70/74/78`]. gta2 writes them there (`vbuf + (i+4)*0x20`) in block units (fixed 16.14 / 16384) [V gta2 0x0046BBF0–0x0046BC59, vertex buffer 0x006632A0]. So the caller's array is 8 × 0x20 = 0x100 bytes; for triangles the world positions also start at +0x80 (slot 3 unused) [V: same offset in e02a80 for n=3].

### 1.2 Draw exports

```c
void __stdcall gbh_DrawTriangle   (uint32_t flags, GBH_Texture* tex, GBH_Vertex v[3+], uint32_t shade);           // 0xe04020 ret 0x10
void __stdcall gbh_DrawQuad       (uint32_t flags, GBH_Texture* tex, GBH_Vertex v[4+], uint32_t shade);           // 0xe043d0 ret 0x10 -> 0xe02cc0
void __stdcall gbh_DrawTile       (uint32_t flags, GBH_Texture* tex, GBH_Vertex v[4+], uint32_t shade);           // 0xe043f0 ret 0x10
void __stdcall gbh_DrawTilePart   (same, SAME ADDRESS 0xe043f0 as DrawTile)
void __stdcall gbh_DrawQuadClipped(uint32_t flags, GBH_Texture* tex, GBH_Vertex v[4], uint32_t shade, const GBH_Vertex clip[4]); // 0xe043a0 ret 0x14 -> 0xe034f0
void __stdcall gbh_DrawFlatRect   (GBH_Vertex v[4], uint32_t argb);                                              // 0xe04550 ret 8
```
Argument order verified in the DLL (stack offsets) and at gta2 call sites: DrawTile 0x0046C7D7 pushes `shade=[ebx+0xC]`, `verts=0x006632A0`, `tex=sub_46BB50(tile)`, `flags=[0x67358C]|[0x6633B8]` [V]; DrawQuad 0x004BE50B pushes `0xFF, 0x66FE18, tex, 0x80` [V]; DrawTriangle 0x00402968 pushes `0xFF, verts, tex=[obj+0x60], 0x4005` [V]. `shade` is used only as `& 0xFF` (0..255 brightness) [V]. No return value (eax garbage) [V].

gta2 static call sites [V, `call [slot]` enumeration]: DrawTile 18 (0x0046C7D7 … 0x0046FBF8, in the block-face renderers 0x46C2C0–0x46FBF8), DrawQuad 6 (0x004BE43B, 0x004BE458, 0x004BE50B, 0x004CBD9A, 0x004CC0B1, 0x004CC546), DrawTriangle 2 (0x00402968, 0x0046C2A9), DrawFlatRect 1 (0x0047418C). **Never called by gta2: DrawTilePart, DrawQuadClipped** (slots only filled, 0x004D3942/0x004D3A06) [V].

Common pipeline of Triangle/Quad/Tile (0xe04020, 0xe02cc0) [V]:
1. Reject if `v0.z ≤ 0`.
2. Reject if bbox completely outside the SetWindow rect: `minX > right || maxX < left || minY > bottom || maxY < top` (floats). No clipping of partially visible polys by the DLL (D3D clips to the viewport; DONOTCLIP not set) [V 0xe040f1–0xe0413b, 0xe02ddf–0xe02e29].
3. `if (flags & 0x10000) flags |= 0x20000` (quad path only) [V 0xe02e33].
4. Blend state from flags (§1.3) [V call 0xe02960].
5. Filter: flags & 0x20000 → TEXTUREMAG=TEXTUREMIN=NEAREST(1), else LINEAR(2); cached in 0xe48604 [V 0xe02e4b–0xe02eb8, 0xe0414b–0xe041b8].
6. Make texture resident (§2.4), SetTexture if changed (texture-switch counter++).
7. Quad only, flags & 0x10000 (**sprite mode**): vertices 1–3 are generated from vertex 0 and the texture size (w,h = tex+0x0E/+0x10): positions `v1=(x0+w−e1,y0) v2=(x0+w−e1,y0+h−e1) v3=(x0,y0+h−e1)`, all z = v0.z; uv `(0,0) (w−e2,0) (w−e2,h−e2) (0,h−e2)` with e1 = [0xe10830] = 0.001, e2 = [0xe1082c] = 0.001; on "RIVA" cards (flag 0xe13884) e1 = 0, e2 = 0.25 and x0,y0 are first passed through CRT 0xe065e0 (floor [I: MSVC floor/ceil helper with `_ctrlfp`; with the DLL's round-down mode this is floor]) [V 0xe02fc2–0xe030c1, 0xe02597–0xe025b0].
8. uv *= 1/cacheSize, rhw = z, diffuse = grey(shade) unless 0x2000, specular = 0.
9. flags & 0x8000 and ambient ≠ 255.0 → CPU lighting (§1.4).
10. DrawPrimitive; counter `globals.polys += 1` (triangle, only on success) / `+= 2` (quad, always) [V 0xe04381, 0xe031be].

**gbh_DrawTile** (0xe043f0) [V]: preprocesses UVs, then calls the quad core with the same args:
- If `!(flags & 0x4000)`: default UVs for a 64×64 tile: v0 (0.5,0.5), v1 (63.4999,0.5), v2 (63.4999,63.4999), v3 (0.5,63.4999) (0x3F000000 / 0x427DFFE6) [V 0xe043fe–0xe0441d]. gta2 always sets 0x4000 and writes its own UVs (0.0 / 63.9999 = 0x427FFFE6) [V gta2 0x0046C744–0x0046C776, flag base 0x4005 at 8 sites].
- Rotation `flags & 0x60` permutes the (u,v) pairs (positions untouched): 0x20: `v0←v3, v3←v2, v2←v1, v1←v0_old`; 0x60: `v0←v1, v1←v2, v2←v3, v3←v0_old`; 0x40: `flags ^= 0x18` (180° = flip X + flip Y) [V 0xe04420–0xe044a8].
- then flags & 0x08 (flip): swap uv(v0)↔uv(v1), uv(v3)↔uv(v2); then flags & 0x10: swap uv(v0)↔uv(v3), uv(v1)↔uv(v2) [V 0xe044ab–0xe04532].
- gta2 maps GTA2 face bits 13–15 (flip, rot) through table 0x005930D4 = {0x23,0x33,0x43,0x53,0x63,0x73,0x03,0x13} (index = face>>13) [V gta2 0x0046C37F–0x0046C38C; table dumped]; i.e. the DLL's rotation base is offset by 90° against the map's. Bits 0–2 (always 3 here) are not tested anywhere in d3ddll [V: no `test` of bits 0–2 in 0xe02810–0xe04670].

**gbh_DrawQuadClipped** (0xe034f0) [V structure / I details]: same rejection tests, also rejects if bbox of `clip[]` and of `v[]` do not overlap; copies v[4] to a local buffer and runs Sutherland–Hodgman clipping against the 4 edges of the convex polygon `clip[0..3]` (edge i→(i+1)&3, side test by 2-D cross product; intersection vertex via 0xe031d0 which interpolates x, y, u, v and the 4 colour bytes, z copied from the edge start) [V 0xe037a0–0xe03a0a, 0xe031d0]; draws the result as one TRIANGLEFAN with n vertices, `polys += n−2`. **No lighting** (no 0x8000 handling), filter/blend/texture as above [V 0xe03a10–0xe03c26]. Not used by gta2 → low priority.

**gbh_DrawFlatRect** (0xe04550) [V]: builds 4 local TL vertices from `v[i].x, v[i].y, rhw = v[i].z (+0x08)`, sz = 0, diffuse = `argb`, spec = 0, uv = 0; sets ALPHABLENDENABLE = 0, COLORKEYENABLE(41) = 0, SetTexture(0, NULL) (and invalidates the texture cache 0xe13df4 = −1), DrawPrimitive(TRIANGLEFAN, 0x1C4, 4, 8). Untextured, opaque (alpha of `argb` ignored).
Quirk [V]: it does not update the blend-state cache 0xe43e24, so if the previous draw left state 1/2, the next translucent draw with the same state will **not** re-enable blending and is drawn opaque until a different state is requested. gta2's only call (0x0047418C, sub_474110) is the debug/perf overlay [I: caller is next to the GetGlobals stats code 0x474530].

### 1.3 `flags` bits (complete list of bits d3ddll tests)

| bit | used in | meaning |
|---|---|---|
| 0x00000007 | — | not tested by d3ddll [V]; gta2 sets 3 on tiles, 5 on 0x4005 |
| 0x00000008 | DrawTile | flip X of UVs [V] |
| 0x00000010 | DrawTile | flip Y of UVs [V] |
| 0x00000060 | DrawTile | UV rotation 0x20/0x40/0x60 [V] |
| 0x00000080 | all | alpha-capable texture variant (index 0 → alpha 0, other texels alpha 1) + blend SRCALPHA/INVSRCALPHA + TEXTUREMAPBLEND=MODULATEALPHA [V 0xe029d2–0xe02a45, 0xe0282a–0xe02835] |
| 0x00000100 | all | "translucent": same blend state as 0x80, same alpha texture variant [V]; on ATI Rage Pro + RegisterTexture(arg5≠0) the texels get alpha ≈ 0x88/0xFF instead of full (§2.4) |
| 0x00000200 | all | additive: ALPHABLENDENABLE=1, SRCBLEND=ONE, DESTBLEND=ONE (TEXTUREMAPBLEND untouched) [V 0xe0296f–0xe029cc]; alpha texture variant |
| 0x00000400–0x1000 | — | not tested [V] |
| 0x00002000 | all | use caller's per-vertex diffuse colours (+0x10) instead of grey(shade) [V] |
| 0x00004000 | DrawTile | use caller UVs instead of default 0.5..63.5 [V] |
| 0x00008000 | Tri/Quad/Tile | apply dynamic lights (§1.4) [V 0xe04337, 0xe03173] |
| 0x00010000 | Quad/Tile | sprite mode: generate v1..v3 from v0 + texture size; implies 0x20000 [V] |
| 0x00020000 | all | point sampling (NEAREST) instead of bilinear [V] |

Blend state machine (0xe02960, cache 0xe43e24: 0 opaque, 1 alpha, 2 additive) [V]:
- `!(flags & 0x380)` → ALPHABLENDENABLE = 0.
- `flags & 0x200` → ALPHABLENDENABLE=1, SRC=ONE(2), DEST=ONE(2).
- else (`0x080|0x100`) → TEXTUREMAPBLEND = MODULATEALPHA(4), ALPHABLENDENABLE=1, SRC=SRCALPHA(5), DEST=INVSRCALPHA(6).
TEXTUREMAPBLEND starts as MODULATE(2) (0xe01ae8) and is never set back once MODULATEALPHA was used [V: only 2 writes of state 0x15]. Equivalent Vulkan fragment: `out.rgb = tex.rgb * vtx.rgb; out.a = tex.a * vtx.a` (opaque-format textures have a = 1) [I: D3D6 MODULATE vs MODULATEALPHA differ only when the texture has no alpha, which yields tex.a=1 anyway].

gta2 usage [V]: tile faces `flags = (face&0x1000 ? 0x80 : 0) | table[face>>13] | 0x4000 | globalLight` where `globalLight = [0x67358C] ∈ {0, 0x8000}` (0x0046C79D–0x0046C7D6, 0x0046C294). Sprites via sub_4CB050: mode 0 → `0x80|light`; mode 1 → `0x2180|light` with all 4 vertex colours `0x00FFFFFF | (a&0xFF)<<27` (alpha = a*8, a ∈ 0..31); mode 2 → `0x2280|light` (additive, same colours) [V 0x004CB050–0x004CB0BD]; 0x004CC0AB ORs 0x20000 (point-filtered quads).

### 1.4 CPU lighting (0xe02a80, called as `light(nverts, verts, 0, shade)`) [V]

```
k      = (shade & 0xFF) / 255
amb    = g_ambient                              // = SetAmbient(a)*255, default 51.0 (0xe10838)
for each vertex i:
   R=G=B=0
   for each light L (0..g_numLights-1):
      if ((L.packed & 0x30000) != 0x10000) continue          // type field must be 1
      d2 = |wpos_i - L.pos|^2
      if (d2 > L.radius2) continue
      dist = sqrt_table(d2)                                   // 4096-entry mantissa table, 12-bit precision (0xe01c30)
      f = (L.radius - dist) * L.invRadius
      if (f <= 0) continue
      f *= L.intensity
      R += L.r*f; G += L.g*f; B += L.b*f                        // L.r/g/b already /255
   c = vertex.diffuse
   r' = min(255, ((c>>16)&255) * k * R + amb)   (same for g, b)
   vertex.diffuse = (c & 0xFF000000) | int(r')<<16 | int(g')<<8 | int(b')   // fistp, rounding = toward -inf (DLL FPU mode)
```
Only executed if `g_ambient != 255.0` [V 0xe04345–0xe04356]. Note the vertex colour is multiplied by k *and* already equals grey(shade) when 0x2000 is clear, so the effective term is `shade²/255 · R` [V; looks unintended but must be reproduced].

---------------------------------------------------------------------------------------------------------------

## 2. Textures and palettes

### 2.1 Prototypes
```c
GBH_Texture* __stdcall gbh_RegisterTexture(int16 width, int16 height, uint8_t* pixels, int paletteId, int flag); // 0xe03c50 ret 0x14
void  __stdcall gbh_FreeTexture  (GBH_Texture*);                  // 0xe03d00 ret 4
void  __stdcall gbh_LockTexture  (GBH_Texture*);                  // 0xe03f70 ret 4 (eax = garbage; gta2 ignores it)
void  __stdcall gbh_UnlockTexture(GBH_Texture*);                  // 0xe03fa0 ret 4
void  __stdcall gbh_RegisterPalette(int paletteId, uint32_t* pal); // 0xe03d20 ret 8
void  __stdcall gbh_FreePalette  (int paletteId);                 // 0xe03f40 ret 4
void  __stdcall gbh_AssignPalette(GBH_Texture*, int paletteId);   // 0xe03fc0 ret 8
```
gta2 call example [V 0x00402059–0x0040209A]: `RegisterTexture(img.w (u16 @+0x0E), img.h (u16 @+0x10), pageBase + (page<<16) + (y<<8) + x, img.pal (u16 @+4), 0)`; at 0x004C2885: `(byte@+4, byte@+5, ptr, pal, 1)` [V]. Returns NULL only if malloc fails [V 0xe03c5e].

### 2.2 `GBH_Texture` (0x20 bytes, malloc'ed by RegisterTexture) [V 0xe03c50–0xe03cee]

| off | type | meaning |
|---|---|---|
| 0x00 | u16 | serial id (global counter 0xe13d54++) |
| 0x02 | u16 | 0 |
| 0x04 | u16 | low byte of the palette's converted-table pointer (unused) [V write, no reads found] |
| 0x06 | u16 | lock pitch: 0x100 while locked, else 0 |
| 0x08 | void* | lock pointer: = pixels while locked, else NULL |
| 0x0C | u8 | x offset inside the 256-wide page (always 0) |
| 0x0D | u8 | y offset (always 0); upload source = `pixels + (y<<8) + x` [V 0xe028b4–0xe028c9] |
| 0x0E | u16 | width (arg1) |
| 0x10 | u16 | height (arg2) |
| 0x12 | u8 | palette record +8 (always 1) |
| 0x13 | u8 | bit0 = locked; bit6 = currently cached in opaque variant; bit7 = "dual variant" = (arg5 ≠ 0 && ATI Rage Pro detected) else 0 |
| 0x14 | u8* | pixels: 8-bit indices, **row pitch 256 bytes** (arg3) |
| 0x18 | u16* | converted palette (512 entries, §2.3) |
| 0x1C | CacheEntry* | resident cache slot or NULL |

Lock/Unlock semantics [V]: Lock sets bit0, +8 = pixels, +6 = 0x100 and **evicts** the texture from the cache (0xe01ec0); the caller edits its own 8-bit data in place (gta2 does exactly that, e.g. sub_460CC0 → sub_4600E0/sub_460030 between Lock 0x00460CF1 and Unlock 0x00460D23). Unlock clears bit0/+6/+8. Re-upload happens lazily at the next draw. AssignPalette: if not locked → lock+evict, set +0x18/+0x12, unlock; if locked → only set +0x18/+0x12. FreeTexture: evict + free. gta2 call sites: Lock 6, Unlock 6, AssignPalette 3, Free 6, Register 7 [V callsites list].

### 2.3 Palettes [V 0xe03d20–0xe03f37]
- `pal` points to entry 0; **entries are 0x100 bytes apart** (GTA2 STY palette pages store 64 palettes interleaved: entry i of palette p at `page + i*256 + p*4`). Entry format u32 `0x00RRGGBB` (bits 16–23 R, 8–15 G, 0–7 B; bits 24–31 ignored).
- **Writes into the caller's data**: entry 0 := 0; every other entry that is 0 becomes 0x00010000 (so black ≠ key) [V 0xe03d28–0xe03d4a].
- Palette table 0xe13e00, 12-byte records indexed by paletteId: {+0 source ptr, +4 converted buffer (malloc 0x400), +8 = 1}. No bounds check [V]; table space up to 0xe43e00 [I: next global].
- Converted buffer: u16[256] in the opaque texture format, then u16[256] at +0x200 in the alpha texture format; channel = `((v >> (24|16|8 − bits)) & mask) << shift` [V]. Alpha bits are not in the palette; they are ORed at upload.
- FreePalette frees the converted buffer, sets ptr 0 (textures still pointing to it dangle) [V].

### 2.4 Texture cache / upload (D3D-side; replace by Vulkan images) [V]
- 12 classes (table 0xe107e0): 0–5 opaque size 8,16,32,64,128,256; 6–11 alpha size 8..256. Class = first with `max(w,h) ≤ size` (alpha classes if `flags & 0x380`) [V 0xe02817–0xe02854]. Slots per class = `CacheMul × {10,62,62,88,1,0, 110,126,126,40,10,0}` (0xe107f8) [V]; **256-sized classes have 0 slots** → 0xe13d20[5]/[11] = NULL → a >128 texture would dereference NULL [V code path; I: gta2 never registers >128-wide textures for drawing]. CacheMul = clamp(GetAvailableVidMem(DDSCAPS_TEXTURE).total >> 21, 1, 2), forced 2 if device has nonlocal-vidmem (AGP) cap [V 0xe023f3–0xe02480].
- Each slot = pair of square `size×size` IDirectDrawSurface4 textures (system-memory copy + video-memory copy, DDSCAPS_TEXTURE|VIDEOMEMORY|ALLOCONLOAD), loaded with `IDirect3DTexture2::Load` [V 0xe05da0, 0xe060e0, 0xe061d0]. Mipmap chain requested with count 1 if filter caps allow [V 0xe05f3d–0xe05f63].
- Formats [V 0xe05c60]: opaque = first enumerated 15/16-bpp RGB format; alpha = prefer ARGB4444 (alpha bits 4), else 1555, else any 16-bpp.
- Upload (0xe061d0) [V]: for each texel `out = pal16[idx] | (idx ? alphaOR : 0)`, where alphaOR = 0 for the opaque variant, = all alpha bits for the alpha variant, = `((0x88FFFFFF >> (32−abits)) & amask) << ashift` (≈53 % alpha with 4444) for "dual variant" textures drawn with 0x100/0x200. Then **edge padding**: if w < size, column w := column w−1; if h < size, row h := row h−1 and the **last row of the texture := row 0**. Index 0 in the opaque variant = palette[0] = 0 = opaque black.
- LRU per class (lists 0xe13d20 head / 0xe13d80 tail, entry stamp = frame counter 0xe43e4c). If the slot to recycle was used this frame: `globals.flushes++`, SetRenderState(FLUSHBATCH=50, 1), frame counter++ [V 0xe0286c–0xe02893].
- Cache entry (0x2C): +0 u16 flags (0x8000 = evicted), +2 u16 alpha-format flag, +4 u16 class size code, +6 u16 class index, +8 frame stamp, +0xC float 1/size, +0x10 float (1/256)/tex+0x12, +0x14 same ×255 (both unused [V no reads]), +0x18 owner texture, +0x1C/+0x20 LRU links, +0x24 surface record [V 0xe0203b–0xe0207b].
- Variant switching for "dual" textures (+0x13 bit7): drawn with 0x300 → must be in alpha variant (bit6 clear) else evict+reupload; drawn without → opaque variant [V 0xe041dc–0xe04221]. For normal textures (bit7 = 0) the variant chosen at first use sticks until eviction [V].

Vulkan equivalent [I]: per GBH_Texture keep an RGBA8 image of size `cacheSize²` (or exact size + scaled UVs) built from index data + palette (alpha = idx≠0 for 0x380 draws); rebuild on Lock/Unlock/AssignPalette (dirty flag); point/linear sampler per draw; CLAMP_TO_EDGE.

---------------------------------------------------------------------------------------------------------------

## 3. Image table (2-D blits of TGA images onto the back buffer)

```c
int  __stdcall gbh_InitImageTable(int count);           // 0xe04670 ret 4: malloc count*16 (zeroed); 0 ok, -1 no memory [V]
int  __stdcall gbh_FreeImageTable(void);                // 0xe046c0 ret 0: Release all surfaces, free table; returns 0 [V]
int  __stdcall gbh_LoadImage(const uint8_t* tga);       // 0xe04730 ret 4
int  __stdcall gbh_BlitImage(int img, int srcL, int srcT, int srcR, int srcB, int dstX, int dstY); // 0xe04a10 ret 0x1c
int  __stdcall gbh_BlitBuffer(int,int,int,int,int,int); // 0xe04be0 ret 0x18: stub, returns 0 [V]; never called by gta2
void __stdcall gbh_PrintBitmap(GBH_Bitmap*);            // 0xe05650 ret 4: software blit, never called by gta2
```
Table entry (16 B, base ptr 0xe13894, count 0xe13898) [V]: {+0 used, +4 width, +8 height, +0xC IDirectDrawSurface4*}.

**LoadImage** [V 0xe04730–0xe049fe]: in-memory TGA; requires `colormap type (byte1) == 0`, `image type (byte2) == 2` (uncompressed true-colour), `bpp (byte 0x10) == 16`. Width u16 @+0x0C, height u16 @+0x0E, pixels at `+0x12 + idLength(byte0)`, **rows bottom-up** (written from last row upward), 16-bit X1R5G5B5 little-endian (bits 10–14 R, 5–9 G, 0–4 B) converted to the back-buffer format (each 5-bit channel shifted to the top of the destination channel). Surface = offscreen plain (system memory if HAL caps has DDCAPS_CANBLTSYSMEM). Returns slot index (first free), −1 table full, −2 bad format/NULL, −3 DirectDraw error.
gta2: InitImageTable 0x00452E2E (count from 0x592DC0), LoadImage 0x00452DFB, BlitImage 8 sites in sub_453020/sub_453590 (frontend backgrounds; e.g. 0x004530A3), FreeImageTable via 0x00452E70 [V].

**BlitImage** [V 0xe04a10–0xe04bcf]: `img ≤ count` and used else −1; all of srcL,srcT,srcR,srcB ≥ 0, srcL,srcR ≤ w, srcT,srcB ≤ h else −2; dstX,dstY ≥ 0 and `dstX+(srcR−srcL) ≤ screenW`, `dstY+(srcB−srcT) ≤ screenH` (VidState +0x48/+0x4C) else −3; if the image surface IsLost → Release, mark free, return **−10** (gta2 checks −10 at 0x004530A9 and reloads); else `backbuffer->Blt(&{dstX,dstY,dstX+w,dstY+h}, img, &{srcL,srcT,srcR,srcB}, DDBLT_WAIT, NULL)` → 0 or −4. RECTs are right/bottom-exclusive; 1:1, no colour key, opaque copy. gta2 calls it between gbh_BeginScene and gbh_EndScene (sub_4585F0 → sub_453020) [V], so the copy is ordered with the 3-D draws of that frame [I: DD Blt to the render target executes immediately; a Vulkan port should record it as a full-opacity textured quad / copy in submission order].

**PrintBitmap** (unused; for completeness) [V 0xe05650–0xe05a04]: struct `{s16 x, y, w, h, srcPitch; u16 flags; u8* src; u8* lut}`; destination = screen table from MakeScreenTable `row[y] + 2x`; clip rect globals 0xe13dd8..0xe13de4 are never written (0) [V]; flags 0x1000 skips clipping; `flags & 0x180`: 0 copy `lut[idx*256]` (u16), 0x80 skip idx 0, 0x100 50 % blend `((dst&m)+(src&m))>>1` with m = 0xe13d00, 0x180 = key + blend.

---------------------------------------------------------------------------------------------------------------

## 4. Remaining exports

| export | proto | behaviour |
|---|---|---|
| gbh_InitDLL 0xe053f0 | `void (VidState*)` | malloc 0x8008 (0xe43e20, unused); **FPU CW := (CW & ~0xF00) | 0x400** (24-bit precision, round down), old CW saved 0xe1385c; store VidState* at 0xe13dc8; bind its own 22 Vid_ pointers via GetProcAddress(VidState+0x7C) (0xe04f10; missing → MessageBox "Error Patching DLL Function"); build sqrt table (0xe01c30, 4096 entries); **patch gta2's Vid slots** through VidState+0x84: +0x20 CloseScreen→gbh_CloseScreen 0xe04d00, +0x24 SetMode→0xe04d80, +0x40 GetSurface→0xe04db0, +0x44 FreeSurface→0xe04dc0 [V 0xe053f0–0xe054b1]. gta2: 0x004D4754 (arg = VidState from 0x673D20) [V]. |
| gbh_Init 0xe04ee0 | `int (int unused)` | runs D3D init 0xe02340; on failure returns its nonzero value (1); else stores arg (0xe43e40, never read), runs SetColourDepth, returns 0 [V]. gta2: `gbh_Init(0)` 0x004CAEA2 (from WinMain 0x004D142E, after the mode set) [V]. Since the SetMode wrapper also runs 0xe02340, Init re-creates the D3D context and texture cache (previous ones leaked) [V code; I: harmless]. |
| SetMode wrapper 0xe04d80 | Vid_SetMode signature (3 args) | `VidState+0x34 = 0`; real Vid_SetMode; if it returned 0 → D3D init 0xe02340, whose result is returned [V]. |
| gbh_CloseScreen 0xe04d00 | `void (VidState*)` (stdcall 4) | evict+free all cache entries of all 12 classes, destroy D3D context (0xe017a0: release textures, viewport, device, Direct3D), then real Vid_CloseScreen [V]. Reached via gta2's CloseScreen slot. |
| gbh_CloseDLL 0xe054c0 | `void (void)` | same cleanup as CloseScreen without calling Vid; writes back gta2's 4 slots **with the addresses of d3ddll's pointer variables** (0xe107a8 …), not their values (bug) [V 0xe05545–0xe05584]; restores FPU CW; frees sqrt table + 0x8008 buffer. gta2: 0x004D3750, then FreeLibrary [V]. |
| gbh_GetGlobals 0xe055c0 | `GBH_Globals* (void)` | returns 0xe43ea0 [V], layout §4.1. |
| gbh_GetUsedCache 0xe04cd0 | `int (int cls)` | number of entries in class `cls` (0..11) whose frame stamp == current frame (used this frame) [V]. gta2: 0x0047456A in the stats overlay loop [V]. |
| gbh_SetWindow 0xe02640 | `void (float left, float top, float right, float bottom)` | stores the trivial-reject rectangle (0xe43e08/10/0c/14); also caches VidState+4 & 1 (unused) [V]. gta2 0x004CAF38: `(0, 0, (float)(W−1), (float)(H−1))` each frame [V]. Not a scissor. |
| gbh_SetCamera 0xe01e90 | `void (float, float, float, float)` | stores to 0xe13d64/70/68/74, **never read** → no-op [V]. gta2 0x00472437 passes 4 int→float camera values [V]. |
| gbh_SetColourDepth 0xe04dd0 | `void (void)` | derives screen packing from VidState +0x58/5C (R shift/bits), +0x60/64 (G), +0x68/6C (B) for ConvertColour/Convert16BitGraphic/LoadImage-free paths; 0xe13d00 = ~(lowest bit of each channel) [V]. gta2: sub_4CAEB0 (0x00456BE2, 0x0045B63B, 0x004CB880) then ConvertColourBank loop [V]. |
| gbh_ConvertColour 0xe02720 | `uint32 (int r, int g, int b)` | `((r&255)>>(8−rb))<<rs | (g…)<<gs | (b…)<<bs` = back-buffer pixel value [V]. Never called by gta2 [V]. |
| gbh_Convert16BitGraphic 0xe02780 | `void (uint16* buf, int bytes)` | in place, bytes/2 pixels, X1R5G5B5 → screen format [V]. Not imported by gta2 [V string list]. |
| gbh_BeginScene 0xe04bf0 | `void (void)` | if VidState+4 & 1 (back buffer locked) → real Vid_FreeSurface and remember (0xe43e18 = 1); zero per-frame upload counters (0xe43f10[12]); globals +0/+4/+8 = 0; texture cache id = −1; frame counter++; `IDirect3DDevice3::BeginScene` [V]. |
| gbh_EndScene 0xe04c70 | `void (void)` | `EndScene`; if it unlocked → real Vid_GetSurface (re-lock); globals+0xC = 64-bit 0xe1084c:0xe10848 / 0xe138a4:0xe138a0 = 0/1 = 0 [V]. **No flip.** |
| gbh_BeginLevel / gbh_EndLevel | `void (void)` | both = 0xe04cc0 `ret` (no-op) [V]. Not called by gta2 statically [V]. |
| gbh_ResetLights 0xe01d40 | `void (void)` | light count = 0 [V]. gta2 0x0047240F per frame [V]. |
| gbh_SetAmbient 0xe01d50 | `void (float a)` | ambient = a × 255 (default 51.0) [V]. gta2: 1.0 at 0x00456BEC, level ambient (fixed/16384) at 0x00472132 and 0x004C6DAE [V]. |
| gbh_AddLight 0xe01d70 | `void (const GBH_Light*)` | append to array 0xe459e0, record 0x2C; **no bound check** [V]; §4.2. gta2: 0x004614CA (sub_461400, lights of the visible 4×4-block cells) [V]. |
| gbh_Plot 0xe026d0 | `void (int,int,int,int)` | stub `ret 0x10` [V]. |
| DrawLine 0xe026e0 | `void (int x0,int y0,int x1,int y1,int c)` | computes \|dx\|,\|dy\| into its own args and returns – no drawing [V]. Not called [V]. |
| SetShadeTableA 0xe02710 | `void (5 × int)` | stub `ret 0x14` [V]. Not called [V]. |
| ConvertColourBank 0xe02800 | `void (void*)` | stub `ret 4` [V]. gta2 calls it per 64-palette bank (0x004CAE29) – nothing to do [V]. |
| MakeScreenTable 0xe02690 | `void (uint8* base, int pitch, unsigned rows)` | `row[i] = base + i*pitch` into 0xe43f40, stores pitch, pitch/2, rows [V]. Only consumer is PrintBitmap [V]. gta2: `(VidState+0x50, +0x54, +0x4C)` every frame at 0x004CAEE0 [V]. |
| DllMain 0xe055d0 | | sets 0xe138a0 = 1, 0xe138a4 = 0, OutputDebugString of the reason [V]. |

### 4.1 `GBH_Globals` (returned by gbh_GetGlobals, 0xe43ea0) [V writers; gta2 readers]

| off | meaning | gta2 reads |
|---|---|---|
| 0x00 | primitives drawn this frame (tri +1, quad +2, clipped n−2) | 0x0047458D |
| 0x04 | texture switches (SetTexture) this frame | 0x0047458F |
| 0x08 | cache flushes this frame (slot reused in same frame) | – |
| 0x0C | "scene time" = always 0 (EndScene result) | 0x00474CB1 (perf graph) |
| 0x10 | u32[12] slots per cache class (CacheMul × base) | – |
| 0x40 | u32[12] slots actually created per class | 0x004745A5.. (`ebx+0x40`) |
| 0x70 | u32[12] uploads per class this frame (reset in BeginScene) | 0x00474560 loop |
The pointer is also stored by gta2 at 0x00675360 (no reads) [V]. gta2 only reads; never writes [V: no stores through the returned pointer found in sub_474530/sub_474C2E].

### 4.2 `GBH_Light` (input, 0x14 bytes) and internal record [V 0xe01d70, gta2 0x00461494–0x004614CA]
| in off | type | meaning | internal (0x2C rec) |
|---|---|---|---|
| 0x00 | u32 packed | bits 0–7 intensity, bits 8–15 radius, bits 16–17 type (must be 1 to be applied), rest unused | +0 raw; +4 intensity/255; +8 radius = byte1/255×8 (blocks); +0xC radius²; +0x10 1/radius |
| 0x04 | float | x (blocks; gta2 = fixed/16384) | +0x14 |
| 0x08 | float | y | +0x18 |
| 0x0C | float | z | +0x1C |
| 0x10 | u32 | colour 0x00RRGGBB | +0x20/24/28 = R/255, G/255, B/255 |
Light positions and vertex world positions (§1.1) use the same fixed→float scale [V]; both are absolute map coordinates [I: vertex wpos = fixed `x + cam->0x98` via sub_401B20, the offset is the camera shake/offset term]. Capacity 256 [I: array 0xe459e0..0xe485e0 / 0x2C].

---------------------------------------------------------------------------------------------------------------

## 5. How the DLL gets DirectDraw/Direct3D, and what it sets

### 5.1 Objects [V]
- VidState (gbh_InitDLL arg): `+0x8C` IDirectDraw → `QueryInterface(IID_IDirect3D3 {bb223240-e72b-11d0-a9b4-00aa00c0993e} @0xe0d488)` → ctx+0x24 [V 0xe01335; GUID decoded from bytes, answers vid_api.md open question 3]; `+0x120` IDirectDraw4 (GetCaps 0x2C, GetAvailableVidMem 0x5C, GetDeviceIdentifier 0x6C, CreateSurface 0x18); `+0x138` back buffer IDirectDrawSurface4 (render target, GetSurfaceDesc 0x58, Blt 0x14, DeleteAttachedSurface 0x20); `+0x48/+0x4C` width/height; `+0x58..+0x6C` pixel format; `+0x1C8/+0x344` DDCAPS HAL/HEL (0x17C bytes, refilled); `+0x7C` Dmavideo HMODULE; `+0x84` gta2 slot-address table; `+0x04` flags bit0 = locked.
- D3D context (malloc 0x64, global 0xe485e0) [V 0xe01300]: +0 VidState*, +4/+8 device list, +0xC next index, +0x10 device count, +0x14 selected device, +0x18 selected index, +0x24 IDirect3D3, +0x28 IDirect3DDevice3, +0x2C IDirect3DViewport3, +0x30 D3DVIEWPORT2, +0x5C DDBD render depth mask, +0x60 Z-buffer surface (only ever released, never created → always NULL).
- Device: `IDirect3D3::EnumDevices` (vt 0x0C) callback 0xe014a0 keeps devices whose `dwDeviceRenderBitDepth` matches the back buffer and that support Gouraud shading; **picks the device with enumeration index 2** (0xe02370 `push 2`) [V]; `IDirect3D3::CreateDevice(guid, backbuffer, &dev, NULL)` (vt 0x20), `CreateViewport` (0x18), `dev->AddViewport` (0x14), `SetCurrentViewport` (0x30), `viewport->SetViewport2` (0x44), `dev->EnumTextureFormats` (0x20, callback 0xe05ba0) [V 0xe01840].
- Vendor quirks via `GetDeviceIdentifier` (only if registry `HKLM\SOFTWARE\DMA Design Ltd\GTA2\Screen\special_recognition` ≠ 0, read/created by 0xe02250 [V; default when absent = I: it creates the value]): 0x12D2/0x18,0x19 "RIVA" → sprite epsilons 0/0.25 + snap (0xe13884=1); 0x1002/0x4742 "ATI_RAGEPRO" → dual-variant textures (0xe13888=1); 0x104C/0x3D07 "PERMEDIA2" → 0xe13884=1 [V 0xe02574–0xe02614]. A port should behave like the default (no quirk) path.

### 5.2 IDirect3DDevice3 methods used [V vtable offsets]
BeginScene 0x24, EndScene 0x28, SetRenderState 0x58, SetLightState 0x60, DrawPrimitive 0x70, SetTexture 0x98, EnumTextureFormats 0x20, AddViewport 0x14, SetCurrentViewport 0x30. IDirect3DTexture2::Load 0x14. IDirectDrawSurface4: Blt 0x14, GetSurfaceDesc 0x58, IsLost 0x60, Lock 0x64, Restore 0x6C, Unlock 0x80, PageLock 0x94, PageUnlock 0x98.

### 5.3 Render states set once after device creation (0xe01a90) [V]
| state (id) | value |
|---|---|
| CULLMODE (22) | 1 NONE |
| ALPHABLENDENABLE (27) | 0 (set 1 then 0) |
| SRCBLEND (19) / DESTBLEND (20) | 5 SRCALPHA / 6 INVSRCALPHA |
| TEXTUREMAPBLEND (21) | 2 MODULATE |
| ALPHAREF (24) / ALPHAFUNC (25) | 0x8000 / 5 GREATER (alpha test itself disabled) |
| ALPHATESTENABLE (15) | 0 |
| SUBPIXEL (31) | 1 |
| TEXTUREPERSPECTIVE (4) | 1 |
| DITHERENABLE (26) | 1 |
| TEXTUREMAG (17) / TEXTUREMIN (18) | 2 LINEAR |
| SHADEMODE (9) | 2 GOURAUD |
| light state COLORMODEL (3) | 2 RGB |
| TEXTUREADDRESS (3) | 3 CLAMP |
Never set anywhere: ZENABLE, ZWRITEENABLE, ZFUNC, FOGENABLE, FOGCOLOR, SPECULARENABLE, ANTIALIAS, WRAPU/V [V: complete list of SetRenderState call sites in §5.4]. Per-draw changes: ALPHABLENDENABLE/SRCBLEND/DESTBLEND/TEXTUREMAPBLEND (0xe02960), TEXTUREMAG/MIN (filter), FLUSHBATCH (50) on cache thrash, COLORKEYENABLE (41) = 0 in DrawFlatRect.

### 5.4 Viewport [V 0xe019a4–0xe01a03]
D3DVIEWPORT2 {X=0, Y=0, W=VidState+0x48, H=+0x4C, clipX=0, clipY=0, clipW=(float)W, clipH=(float)H, MinZ=0.0, MaxZ=500.0}. With XYZRHW vertices only the pixel mapping matters: vertex (x,y) in pixels, y down. Vulkan: viewport = full swapchain/back buffer extent at the game's resolution, `gl_Position = (2x/W−1, 2y/H−1, z?, 1) * w` with w = 1/rhw for perspective-correct varyings; depth test off.
SetRenderState call sites (complete) [V]: 0xe01a9f–0xe01b7f (init), 0xe02890, 0xe029a6/b9/cc, 0xe02a0c/1f/32/45/64, 0xe02e75/a5/b8, 0xe03a48/74/87, 0xe04175/a5/b8, 0xe04601/14.

---------------------------------------------------------------------------------------------------------------

## 6. Call order from gta2.exe

Startup [V; mode-set details in vid_api.md §5]:
1. WinMain → sub_4CC6D0 → Vid_InitDLL/Init_SYS/SetDevice → mode set (Vid_SetMode, still unwrapped) → sub_4CAE50 → sub_4D4730("d3ddll.dll" name buffer 0x673478, VidState): LoadLibrary, 39 GetProcAddress (slots 0x5952A8–0x595340, DrawLine 0x67535C; table §6.1), **gbh_InitDLL** 0x004D4754, **gbh_GetGlobals** 0x004D475A.
2. **gbh_Init(0)** 0x004CAEA2 (WinMain 0x004D142E).
3. Level/frontend load: InitImageTable/LoadImage, RegisterPalette (0x004C29A2, 0x00401FD6), RegisterTexture, SetColourDepth (0x004CAEB0) + ConvertColourBank, SetAmbient(1.0) 0x00456BEC.
4. Later mode changes go through the wrapped Vid_SetMode/CloseScreen (D3D torn down/recreated; textures evicted, GBH_Texture objects survive and re-upload lazily [V: CloseScreen only frees cache entries]).

Per in-game frame (sub_461960 → sub_45A5A0) [V]:
```
sub_4CAEC0 (0x0045A5EF): Vid_GetSurface (0x004CAEC9, lock) ; MakeScreenTable (0x004CAEE0) ; gbh_SetWindow(0,0,W-1,H-1) (0x004CAF38)
gbh_BeginScene            0x0045A5F4      (DLL unlocks the back buffer, BeginScene)
sub_472110: gbh_SetAmbient 0x00472132 ; gbh_ResetLights 0x0047240F ; gbh_SetCamera 0x00472437 ; gbh_AddLight* 0x004614CA ;
            world: gbh_DrawTile (0x0046C7D7…), gbh_DrawTriangle (0x0046C2A9), sprites gbh_DrawQuad (0x004BE43B…, 0x004CBD9A…)
sub_4B98B0 / sub_4CA440   (more 2-D/3-D drawing, HUD)
[debug] sub_474530: gbh_GetGlobals 0x00474552, gbh_GetUsedCache 0x0047456A, gbh_DrawFlatRect 0x0047418C
gbh_EndScene              0x0045A638      (EndScene, DLL re-locks)
sub_4CAF50: Vid_FreeSurface 0x004CAF56 (unlock)
Vid_FlipBuffers           0x00461971      <- present
Vid_ClearScreen           0x004619BC      (only if overlay flags 0x5EAD9C/8C/5F/67/59 set)
```
Frontend frame sub_4585F0: 0x004585F3 sub_4CAEC0 → gbh_BeginScene 0x004585F8 → sub_453020 (gbh_BlitImage 0x004530A3…) → sub_458590 → gbh_EndScene 0x0045860C → FreeSurface → Vid_FlipBuffers 0x0045861D → Vid_ClearScreen 0x0045863B [V].
Other frame-ish paths with Flip/Clear: Bink 0x00481E6A/0x00481E88, startup 0x004CC79A–0x004CC7C5 [V].

### 6.1 gta2 pointer slots (filled in sub_4D3770, all `GetProcAddress` results) [V]
gbh_InitDLL 0x5952A8, Init 0x5952AC, CloseDLL 0x5952B0, SetWindow 0x5952B4, GetGlobals 0x5952B8, DrawTile 0x5952BC, DrawTilePart 0x5952C0, DrawQuad 0x5952C4, DrawQuadClipped 0x5952C8, DrawFlatRect 0x5952CC, DrawTriangle 0x5952D0, PrintBitmap 0x5952D4, SetColourDepth 0x5952D8, Plot 0x5952DC, ConvertColour 0x5952E0, RegisterTexture 0x5952E4, SetCamera 0x5952E8, ResetLights 0x5952EC, AddLight 0x5952F0, SetAmbient 0x5952F4, GetUsedCache 0x5952F8, FreeTexture 0x5952FC, RegisterPalette 0x595300, FreePalette 0x595304, AssignPalette 0x595308, LockTexture 0x59530C, UnlockTexture 0x595310, BeginScene 0x595314, EndScene 0x595318, BeginLevel 0x59531C, EndLevel 0x595320, MakeScreenTable 0x595324, ConvertColourBank 0x595328, SetShadeTableA 0x59532C, InitImageTable 0x595330, FreeImageTable 0x595334, LoadImage 0x595338, BlitImage 0x59533C, BlitBuffer 0x595340, DrawLine 0x67535C. Default before load = stub 0x004D35C0 (0x004D3637 …) [V]. gbh_Convert16BitGraphic is not imported [V].
Slots with **no call site** in gta2: DrawTilePart, DrawQuadClipped, PrintBitmap, Plot, ConvertColour, DrawLine, SetShadeTableA, BlitBuffer, BeginLevel, EndLevel [V: only the GetProcAddress store / stub init reference them].

---------------------------------------------------------------------------------------------------------------

## 7. Open questions

1. Exact meaning of light type bits 16–17 (only value 1 lights anything) and of gta2's light struct layout beyond what sub_461400 copies (+0 packed, +4/8/0xC pos, +0x10 colour, +0x20 next) – not traced into the map loader.
2. 0xe065e0 is a CRT rounding helper used only on RIVA/Permedia2 – floor vs. ceil not confirmed (irrelevant if the port takes the non-quirk path).
3. Whether `IDirect3D3::EnumDevices` index 2 is the HAL device on real systems (callback accepts HAL or HEL desc) – irrelevant for Vulkan, but explains why "RGB emulation" is never used.
4. Behaviour for textures with max(w,h) > 128 (class 5/11 has 0 slots → NULL deref at 0xe0286c): confirm gta2 never draws such textures (STY sprites max 128? – check sprite sizes at load).
5. `GBH_Texture+0x04` (low byte of palette buffer ptr) and cache entry +0x10/+0x14 floats are written but never read in d3ddll; could softdll/3dfx renderers read them? (not analysed).
6. The FPU control word change in gbh_InitDLL affects gta2's own float→int conversions (round-down, 24-bit): the recomp's x87 emulation must honour it from that call on – verify how the recomp currently models FLDCW.
7. DrawFlatRect's stale blend cache (§1.2) – confirm visually whether the perf overlay causes an opaque sprite afterwards (only matters if the overlay is ever enabled).
8. Range/meaning of gta2's `shade` argument per draw type (tiles: `[ebx+0xC]`, sprites: 0xFF) and of the ambient value fed to SetAmbient per level.
