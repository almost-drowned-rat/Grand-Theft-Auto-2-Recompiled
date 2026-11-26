# GTA 2 widescreen (real wider view, Hor+) — patch specification

Target: gta2.exe (2004 freeware build, image base 0x400000). In-game screen mode W x H with
W/H > 4/3 (854x480, 768x480, 1138x480, 1280x720 ...) should show **more world left and
right at the same zoom as 4:3**, the HUD anchored to the real edges, and nothing culled.

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).

## 0. Conventions and helpers

Fixed point everywhere is Q14 (1.0 = 0x4000) [V]:

| helper | semantics [V] |
|---|---|
| sub_00401AF0 / sub_0041F980 / sub_0040CE00 | `*this = int << 14` (ctor from int) |
| sub_00401AD0 | `*this = raw` |
| sub_00401B10 | `st0 = (float)*this * 6.1035e-5` (to float) |
| sub_00401B20 | `*ret = *this + *other` |
| sub_00401B40 | `*ret = *this - *other` |
| sub_00401B60 | `*ret = (int64)*this * *other >> 14` (64-bit shift via sub_004D6230) |
| sub_00401B90 | `*ret = ((int64)*this << 14) / *other` |
| sub_00401BF0 | `*ret = *this / *(int*)other` (raw idiv, e.g. by 2) |
| sub_0041F9D0 | `return *this >> 14` (floor to int) |
| sub_0041E0D0 | `*this = *this * *other >> 14` (in place) |
| sub_004037E0 / sub_00403800 / sub_0040CE70 / sub_0040CE50 | `<`, `>`, `>=`, `<=` |

Static Q14 constants (set by static ctors) [V]:

| addr | value | set at |
|---|---|---|
| 0x5E3D18 | 7.0 | sub_004FCB00 |
| 0x5E3D64 | 1.0 | sub_004FCAA0 |
| 0x5E3F54 | 0.5 (0x2000) | sub_004FCD80 |
| 0x5E3F34 | 0.75 (0x3000) | sub_004FD0B0 |
| 0x5E3D98 | 0.75 (copied from 0x5E3F34) | sub_004FDFA0 |
| 0x5E3DC8 | 0 | sub_004FCA90 |
| 0x5E3E30 | 0x3FFFFF (255.99 blocks, map max) | sub_004FCDE0 |
| 0x6636C8 | 0.75 (0x3000) | sub_0052BC30 |
| 0x5E672C | 1.0 | sub_00508910 |
| 0x5E6944 | 1.5 (0x6000) | sub_00508C30 |
| 0x5E5E74 | 0.5 (0x2000) | sub_00504700 |
| float 0x573C98 = 8.0, 0x56E3D4 = 1.0, 0x575418 = 640.0, 0x575414 = 480.0 | | .rdata |

### Camera object (size 0xBC; ctor sub_0041F580) [V unless marked]

Each player owns three cameras, constructed in sub_004A83C0 at 0x004A83FC/0x004A840D/0x004A841D:
`player+0x90` (**logic camera**, the one gameplay "visible" tests use — sub_0045A800 returns
`player+0x90` at 0x0045A844), `player+0x14C` (**render camera**, copied to 0x005E3CC4 each frame
at 0x0045A5AE), `player+0x208` (second camera, tested only when `player+0x2D0` is set,
0x0045BC49; purpose [I] remote/aux view).

| off | meaning |
|---|---|
| +0x20/+0x24/+0x28/+0x2C | view rect +- 0.75 block margin (x0, x1, y0, y1), written in sub_0041E7A0 |
| +0x34 / +0x38 | followed ped / car [I] |
| +0x60 | projection scale (Q14) = width*zoom, used for x **and** y |
| +0x64 | 640*zoom (scale of the "virtual 640x480" projection) |
| +0x68 / +0x6C | viewport width / height (int) |
| +0x70 / +0x74 | screen centre x / y (int) = W/2, H/2 |
| +0x78/+0x7C/+0x80/+0x84 | visible world rect x0, x1, y0, y1 (Q14 blocks, clamped 0..0x3FFFFF) |
| +0x98/+0x9C/+0xA0 | camera x, y, height z (Q14) |
| +0xA4 | zoom factor (Q14) |
| +0xA8 | HUD scale = W/640 (Q14) |

Camera functions: sub_0041EB70 = set viewport (W,H); sub_0041F2F0 = per-frame update
(smoothing, ends in sub_0041E7A0); sub_0041E7A0 = per-frame projection scale + view rect.

## 1. Screen centre cam+0x70 / +0x74

**[V] sub_0041EB70(this, W, H)** (0x0041EB70-0x0041EBE4):

```
+0x68 = W; +0x70 = W >> 1 (shr, 0x0041EB7F/0x0041EB84)
+0x6C = H; +0x74 = H >> 1 (0x0041EB87/0x0041EB92)
+0x60 = +0x64 = fixed(-1)           placeholders, overwritten every frame by sub_0041E7A0
+0xA8 = (W << 14) / 640             0x0041EBB8-0x0041EBD9 (sub_0040CE00 + raw idiv sub_00401BF0)
```

Callers [V]: camera ctor sub_0041F580 at 0x0041F63E with (640, 480) (`push 0x1E0`
0x0041F5FD, `push 0x280` 0x0041F631); sub_0045A4F0(W,H) for all three cameras
(0x0045A508 +0x90, 0x0045A518 +0x208, 0x0045A528 +0x14C), followed by the HUD re-layout
sub_004CABE0(0x672F40).

So the centre is **already W/2, H/2 — when the viewport is set**. The 320 you saw means the
render camera still had W=640. That is consistent with the HUD: its scale +0xA8 = W/640 was
still 1.0 (if the camera had W=854, the HUD would have been scaled x1.33, not left at 640).

**Why the viewport is not refreshed [V]: sub_004CB150** (called from sub_0045B5F0 at 0x0045B640,
and as tail jump from sub_004CB880 = after every mode change):

```
if (Vid->+0x40 == -2)        // windowed
    if (last != window_width)  { sub_0045A4F0(window_width, window_height); } last = window_width;
else                          // fullscreen
    if (last != Vid->+0x4C)   { sub_0045A4F0(Vid->+0x48, Vid->+0x4C); }  last = Vid->+0x4C;   // 0x004CB195-0x004CB1B6
```

`last` = 0x006735A0, reset to 0 only by sub_004CB140 (0x0045B636, game start). In fullscreen
the change test uses the **height**: a switch 640x480 -> 854x480 that happens after the
game-start call (via sub_004CB880) never reaches the cameras. [I] In your run the game-start call
saw VidState still at 640x480 and the switch to full_width came later through that path.

**Patch 1 (required):** replace sub_004CB150 natively: keep `lastW, lastH`; call
`sub_0045A4F0(W, H)` (W/H from window_width/height when Vid+0x40 == -2, else Vid+0x48/+0x4C)
whenever **either** changed; then tail-call sub_004619D0 and sub_004619E0 as the original.
sub_004CB140 resets both. Instruction-level alternative: at 0x004CB195 compare `[eax+0x48]`
instead of `[eax+0x4C]`, at 0x004CB1B3 store `[eax+0x48]` (width-keyed; a pure height change
with the same width would then be missed, so the two-value version is better).

Check at runtime: log cam+0x68/+0x6C at sub_0041E7A0 entry for 0x005E3CC4.

## 2. Projection scale, visible area, culling

### 2a. Why "just setting W" would zoom instead of widening [V]

sub_0041E7A0 (per frame, every camera):

```
+0x60 = fixed(+0x68 /*W*/) * zoom            0x0041E7AD-0x0041E7DA   <- 4:3 assumption
+0x64 = fixed(640) * zoom                    0x0041E7D1-0x0041E7F9
E     = ((7.0 + z) * (1.0 / zoom)) * 0.5     0x0041E7FE-0x0041E842 (local [esp+0x10])
+0x78 = clamp(x - E)   +0x7C = clamp(x + E)  0x0041E846-0x0041E8E1
E    *= 0.75                                 0x0041E8E3-0x0041E8EC (sub_0041E0D0, 0x5E3F34)
+0x80 = clamp(y - E)   +0x84 = clamp(y + E)  0x0041E8F1-0x0041E9A3
+0x20 = +0x78 - 0.75, +0x24 = +0x7C + 0.75, +0x28 = +0x80 - 0.75, +0x2C = +0x84 + 0.75   0x0041E9A5-0x0041E9FC
clamp(v): v < 0 -> 0 ; v > 0x3FFFFF -> 0x3FFFFF
```

The vertex projection (sub_0046BC70/BD40/BDF0, and the per-layer scales 0x006633CC/0x00663504
computed in sub_00472110 at 0x00472357-0x004723FF from cam+0x60) is
`sx = dx * cam60 * rhw + cam70`, `sy = dy * cam60 * rhw + cam74`, `rhw = 1/(8 - z + cam_z)`.
With cam60 = W*zoom the horizontal world span is constant (=640-equivalent) and the vertical span
shrinks: a wider mode only zooms in. The rect half-width E maps to exactly W/2 pixels for
ground z = 1 (E*cam60*rhw = W/2), and the 0.75 factor is the hard-coded 3:4 (480/640).

### 2b. Patch 2: sub_0041E7A0 (replace natively, all three cameras)

```c
int W = cam->w68, H = cam->h6c;
int32_t WeffQ = (int32_t)(((int64_t)H << 14) * 640 / 480);   // "4:3 width" for this height
int32_t A     = (int32_t)(((int64_t)W * 3 << 14) / (4 * H)); // W / Weff, 1.0 at 4:3
cam->f60 = fxmul(WeffQ, cam->zoom_a4);                       // was fxmul(W<<14, zoom)
cam->f64 = fxmul(640 << 14, cam->zoom_a4);                   // unchanged
int32_t E  = fxmul(fxmul(0x1C000 + cam->z_a0, fxdiv(0x4000, cam->zoom_a4)), 0x2000);
int32_t Ex = (A > 0x4000) ? fxmul(E, A) : E;                 // horizontal only
int32_t Ey = fxmul(E, 0x3000);                               // unchanged (0.75)
cam->x78 = clamp(cam->x98 - Ex); cam->x7c = clamp(cam->x98 + Ex);
cam->y80 = clamp(cam->y9c - Ey); cam->y84 = clamp(cam->y9c + Ey);
cam->r20 = cam->x78 - 0x3000; cam->r24 = cam->x7c + 0x3000;
cam->r28 = cam->y80 - 0x3000; cam->r2c = cam->y84 + 0x3000;
```

Keep the multiply order and the 64-bit `>>14` of sub_00401B60 to stay bit-exact at 4:3
(A == 1.0 must give the original values). Result: same zoom as 640x480 scaled to H, view
extended by `(A-1)*E` on each side. Vertical extent identical to 4:3, so the high-speed
zoom-out (camera z = +0xA0 rising) keeps working unchanged: every extent is a function of
`(7 + z)/zoom` [V].

### 2c. Patch 3: block (tile) renderer sub_00472110 [V]

Per layer L = 0..6 (`ebp = 8 - L`), with S = esp after the 4 register pushes:

```
D        = fixed(8-L) + cam_z                     0x00472162-0x00472198  -> [S+0x2C]
D/zoom   -> [S+0x18]                              0x004721A2-0x004721AD
Wy       = (D/zoom) * 0.75 (0x6636C8)             0x004721B1-0x004721C0  -> [S+0x24]
x_min    = int(cam_x - (D/zoom)/2)                0x004721C2-0x004721F6  -> [S+0x1C]
x_max    = int(cam_x + (D/zoom)/2)                0x004721FB-0x0047222E  -> [S+0x28]
y_min/max= int(cam_y -/+ Wy/2)                    0x00472255-0x004722BF  -> [S+0x18]/[S+0x24]
then spiral over (x_max-x_min+1)/2 x (y_max-y_min+1)/2 blocks via sub_0046BB90 (0x00472465-0x0047252B)
```

The x half-span (D/zoom)/2 projects to exactly cam60/(2*zoom) = Weff/2 pixels; y to 0.75*Weff/2 = H/2.

**Patch:** insert at 0x004721C2 (stack balanced there, esp == S):
`MEM32(esp+0x18) = fxmul(MEM32(esp+0x18), A);` (A as in 2b, from cam 0x005E3CC4).
Only x changes; Wy was already computed from the unscaled value. The layer-0 lighting call
`gbh_SetCamera(x_min, y_min?, x_max?, y_max?)` at 0x00472437 (only when 0x00595011 lighting is on)
uses these same ints [V], so lights widen automatically [I: argument order].

### 2d. Sprites / objects in view [V]

sub_00447390 (called per frame from sub_0045A5A0 for the three grids 0x5E6694/0x5E6690/0x5E668C)
takes the **render** camera rect: `x0 = int(cam+0x78 - 1.0)`, `x1 = int(cam+0x7C + 1.5)`,
`y0 = int(cam+0x80 - 1.0)`, `y1 = int(cam+0x84 + 1.5)`, clamps 0..255 and calls sub_00446D60,
which adds every object of those blocks to the sprite draw lists 0x5E8B80 (sub_0044B990). No
pixel constants: widened automatically by Patch 2. The per-layer sprite flush
(sub_0044B8B0 inside sub_00472110) and the sprite renderer sub_004BE060 have no 640/480 constants.

### 2e. Gameplay "visible" tests (change game behaviour) — listed separately [V]

* **sub_0040CF60(cam, x, y)** = `x >= cam+0x78 && x <= cam+0x7C && y >= cam+0x80 && y <= cam+0x84`.
* **sub_0045BC10(game, x, y)** = "visible to any player": runs sub_0040CF60 on each active
  player's **logic camera +0x90** (0x0045BC40) and, if `player+0x2D0`, on +0x208 (0x0045BC63).
  Callers: sub_0040DD50, sub_00424FF0, sub_00427D60, sub_00428540, sub_00440CC0, sub_0047CD00,
  sub_00491240, sub_004AD6C0, sub_004B0CF0 ([I] create/delete/teleport objects, cars, peds only
  when not visible).
* **sub_0045BB00** (callers sub_00491240, sub_00491550): sub_0040CF60 on the first player's
  logic camera.
* **sub_004415E0 -> sub_00440CC0(0x5E5BC0, logic cam)** (from sub_00445A20): uses the logic
  camera rect +0x78..+0x84 +- 0.5 block (0x5E5E74) as block range, [I] ped/traffic generation
  ring around the view.

Because Patch 2 is applied to every camera, these tests then cover the wider view. That is
what you want visually (otherwise cars/peds spawn or vanish inside the extra side strips,
about `(A-1)*W/2/A` px per side, 107 px at 854x480), but it changes behaviour: generation ring
and "may delete" area move outwards horizontally, so density and police spawn distance change.
**Choice:** apply A in sub_0041E7A0 for all cameras (recommended, no pop-in), or only when
`this == player+0x14C` (render camera; gameplay bit-identical to 4:3, visible pop-in at the sides).

## 3. HUD

### 3a. How it works [V]

The HUD lays out in a **virtual 640x480** space and scales uniformly by cam+0xA8 (W/640) of the
render camera. The three scaled primitives:

| primitive | args | effect |
|---|---|---|
| sub_004C71B0 (sprite) | (type, frame, x, y, pal, ...), ret 0x24 | x' = x*a8, y' = y*a8, scale a8 -> sub_004CBA50 |
| sub_004C7280 (text) | (str, x, y, font, ...), ret 0x20 | same, -> sub_004CC100 |
| sub_004BA2C0 (text) | (str, x, y, font), ret 0x10 | same, -> sub_004CC100 |

(x/y Q14, a8 loaded from `[0x5E3CC4]+0xA8` at 0x004C71B2, 0x004C7282, 0x004BA2C4.)
Positions are fixed 640-based numbers or `(640 - w)/2`, `638 - w`, `320 - w/2` formulas.

### 3b. Patch 4

1. sub_0041EB70: `+0xA8 = (H << 14) / 480` instead of `(W << 14)/640`
   (instruction level: 0x0041EBBA `push edi` -> push H, 0x0041EBC2 `0x280` -> `0x1E0`).
   HUD and arrows then scale with the height (s = H/480), the virtual 640x480 covers the
   central `640*s` x H region.
2. Replace the three primitives: `x' = x*s + anchor*off`, `off = (W<<14) - fxmul(640<<14, s)`,
   anchor 0 = left, 1/2 = centre, 1 = right, chosen by the caller's return address:

| element (function, HUD member) | anchor | return addresses (after the call) |
|---|---|---|
| weapon icon/ammo etc., small x 0x10..0x5D (sub_004C74F0, +0x27B4) | left | 0x4C758F 0x4C75C4 0x4C75F9 0x4C7650 0x4C76B1 0x4C7757 0x4C7791 0x4C77D2 0x4C7813 0x4C787C |
| pager, x 0x20, y 0x1BB/480-.. (sub_004C9430, +0xE4) | left | 0x4C9492 0x4C94E2 |
| hearts/lives, x 0x227 step 0x14, y 0x22 (+ debug value) (sub_004C7B70, +0x2840) | right | 0x4C7BFB 0x4C7C51 0x4C7CA9 |
| right-aligned text x = 640 - w, bottom (sub_004C8910, +0x2A1F) | right | 0x4C89F9 |
| right status icon + text, x = 638 - w (sub_004C9A40) | right | 0x4C9AAC 0x4C9B3B |
| right group from x 0x27F, 0x1A4, 0x1EA, 0x20B (sub_004C9C20 +0x2850, sub_004C9B40) | right | 0x4C9D16 0x4C9E3D 0x4C9F42 0x4C9F8C 0x4C9DA9 0x4C9EBD 0x4C9BAB 0x4C9C16 |
| wanted level heads, x = (640 - total)/2 (sub_004C7A30, +0x2760) | centre | 0x4C7AEF |
| 3 centred lines (640 - w)/2 (sub_004C8710, +0x2A1E) | centre | 0x4C87B0 0x4C8810 0x4C8872 |
| mission/brief text box, x = (640-w)/2, y = (480-h)/4 (layout sub_004C5FE0, draw sub_004C8A40, +0x2854) | centre | 0x4C8A8E |
| centred message box (layout sub_004C8AA0: (640-w)/2,(480-h)/2; draw sub_004C8B90) | centre | 0x4C8BDE |
| big centred items 0x140-based (sub_004C94F0, HUD this) | centre | 0x4C9557 0x4C9590 0x4C95FD 0x4C9638 0x4C967C |
| centred text y 0x20 (sub_004C9690, +0x27B8) / y 0x10 (sub_004C96F0, +0x2A20) | centre | 0x4C96E2 / 0x4C9742 |
| centred frames 0x9F-0xA2 + text (sub_004C9890, +0x4C) | centre | 0x4C9901 0x4C9944 0x4C998F 0x4C99D5 0x4C9A36 |
| centred panel x 0xE3/0x140/0x19D (sub_004C9FA0, +0x2A1C) | centre | 0x4CA012 0x4CA04C 0x4CA085 0x4CA0E2 0x4CA1C8 0x4CA41B |
| name above ped, x from virtual projection (sub_004C78A0, +0x2AF0) | centre | 0x4C79B3 |
| digit counters at struct positions (sub_004C8CA0, sub_004C8E30, sub_004C9040; from sub_004C8C80 +0x818, sub_004C92A0 +0x7E8) | **[I]** by x: x>=427 right, 213..426 centre, else left | 0x4C8D70 0x4C8DAF 0x4C8DEB 0x4C8E26, 0x4C8EB6 0x4C8EF1 0x4C8F34 0x4C8FA5 0x4C8FF1 0x4C9028, 0x4C90B8 0x4C90EF 0x4C9172 0x4C91A0 0x4C91DA 0x4C9256 0x4C928D |
| debug world labels (sub_004BACF0/sub_004BAEF0; x = screen_px*640/window_width, debug overlay only) | none needed if x is pre-divided by s instead of 640/W; otherwise leave (labels drift) [I] | 0x4BAE37 0x4BAEE2 0x4BAF9A 0x4BAFC5 |

Element names are [I] from position/frames; the anchors follow the [V] position formulas.
A safe fallback for any unlisted return address: anchor by virtual x as in the counter row.
Vertical: no offset needed (s = H/480 maps 480 exactly onto H).

3. Not scaled (raw pixel coordinates, keep): world-anchored sprites sub_004C74A0 (+0x2844) and
   sub_004C82C0 (arrows) call sub_004CBA50 directly with positions computed by the real
   projection sub_004B90E0 (uses cam +0x60/+0x70/+0x74/+0x98/+0x9C/+0xA0, [V] 0x004B911E/0x004B9176)
   in sub_004C7FC0; the arrow radius uses cam+0x60 and +0xA8 [V 0x004C817E-0x004C81A3], both
   stay consistent after Patches 2 and 4.
4. Frontend callers of sub_004CBA50/sub_004CC100 (0x453xxx-0x458xxx) run in the 640x480 menu [I]
   and need nothing.

## 4. Other 4:3 assumptions at gameplay time

| where | original [V] | change |
|---|---|---|
| sub_0040CFC0 "virtual" world->screen (cam+0x64, centre 0x140 at 0x0040CFFE, 0xF0 at 0x0040D055) | 640x480 virtual coords | keep (consistent with HUD space; centre-anchored) |
| sub_0041E710 on-virtual-screen test: `0 <= sx < 640 && 0 <= sy < 480` (0x0041E72E zero, 0x0041E740 `push 0x280`, 0x0041E75C zero, 0x0041E76E `push 0x1E0`); used by sub_004C78A0 (name labels) and sub_004C7CF0 (arrow logic via sub_004C7E60) | 4:3 bounds | x bounds -> `320 - 320*A` .. `320 + 320*A` (Q14), y unchanged |
| sub_004B92B0 (drawn by sub_004B98B0/sub_004B94B0 each frame unless 0x5EAD5D, i.e. registry `exploding_on`) screen-space sprites ("exploding scores" [I]) via real projection sub_004B90E0, culled to `0 <= x < 640` (0x004B9392 `push 0x280`) and `0 <= y < 480` (0x004B93C0 `push 0x1E0`) | culled at x = 640 | `0x280 -> W`, `0x1E0 -> H` (real pixels) |
| sub_004B94B0 spawns them inside render cam rect +0x20..+0x2C | | automatic |
| gbh_SetWindow(0,0,W-1,H-1) each frame (sub_004CAEC0, 0x004CAF38) | already real size | none |
| speed zoom (cam z +0xA0) | extents use (7+z), projection 1/(8-z_v+z) | automatic |
| text boxes / mission messages / pause panel | centred 640-based | centre anchor (section 3) |
| fades, letterbox | no gameplay-time 640/480 constants found outside the HUD (scan of all `0x280/0x1E0/0x140/0xF0` immediates; menu code 0x453xxx-0x4587B0 excluded) [V scan] | none known |
| sub_0040D120 (debug, called from sub_0045C1F0) uses sub_0040CF60/sub_0040CFC0 on +0x14C | debug text | optional |

## 5. Minimal patch set (summary)

1. sub_004CB150: refresh cameras on width **or** height change (else the camera stays 640x480).
2. sub_0041EB70: `+0xA8 = H/480`.
3. sub_0041E7A0: `+0x60 = (H*4/3)*zoom`; view rect half-width `E*A`, half-height `E*0.75`.
4. 0x004721C2 in sub_00472110: `[esp+0x18] *= A` (tile x range).
5. HUD primitives sub_004C71B0 / sub_004C7280 / sub_004BA2C0: add anchor offset by return address.
6. sub_0041E710: x bounds `320 -/+ 320*A`.
7. sub_004B92B0: cull bounds 640/480 -> W/H.

At W/H = 4/3 (A = 1.0, s = W/640) all of this reproduces the original numbers exactly.

## 6. Open questions

1. Runtime order: does the recomp switch to full_width before or after 0x0045B640? (Explains the
   640 camera; Patch 1 makes it irrelevant, but confirm with a log of cam+0x68.)
2. Gameplay choice in 2e: widen the logic camera +0x90 (no pop-in, changed spawn/despawn area)
   or render camera only (identical gameplay, side pop-in)?
3. Exact screen positions of the digit counters (sub_004C8CA0/8E30/9040): set from mission
   script/data; find the writer of their x to give fixed anchors instead of the x heuristic.
4. Role of camera +0x208 (tested when player+0x2D0 != 0).
5. Exact semantics of sub_00440CC0 (assumed ped/traffic generation) and of each sub_0045BC10 caller.
6. Argument order of gbh_SetCamera at 0x00472437 (lighting window) — only matters if lights are
   culled wrongly at the sides.
7. Very wide modes (21:9 at 1138x480, A = 1.78): the tile spiral adds ~1.8x more entries per
   layer. The per-layer tile list of the renderer 0x006633C8 (entries at +0x1C + i*8, count at
   +0x2EFC, filled by sub_0046BB90) holds (0x2EFC-0x1C)/8 = 1500 entries and sub_0046BB90 has
   **no bounds check** [V 0x0046BB9A-0x0046BBA6]; it also does a linear duplicate search, so cost
   grows quadratically. Measure the peak count at maximum zoom-out (fast car) before allowing
   very wide ratios; clamp A or add a bound in the native replacement if needed.
