# GTA 2 camera distance (CAMERA DISTANCE 1.0x .. 2.0x)

Target: gta2.exe (2004 freeware build, image base 0x400000).
Goal: let the player move the gameplay camera further out by a factor F (1.0 .. 2.0).

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).
Fixed point Q14 (1.0 = 0x4000). `this` in ecx (thiscall). Source: `analysis/disasm/asm/text.asm`.
Known layout: widescreen.md (camera object, `sub_0041E7A0`, block renderer, visible tests).
Nothing here was tested on a running build.

---

## 0. Summary

* The camera has three 4-value sets (x, y, height z, zoom): **target** +0x10..+0x1C (written by
  the follow code), **step target** +0x00..+0x0C, **current** +0x98..+0xA4 (smoothed towards the
  target every tick), plus the previous frame's values at +0x88..+0x94.
* The steady-state **zoom** is one constant, **`[0x5E3EB4]` = 284/320 = 0.8875** (dead player:
  `[0x5E3ECC]` = 1136/320 = 3.55, a zoom-in on the body). Both are written only by static init and
  read only by the camera code [V xrefs].
* **Recommended hook:** at game start (or whenever the option changes), write
  `R32(0x5E3EB4) = 0x38CC / F` (and `0x5E3ECC` = its original value / F). The follow code then sets
  this target, the smoothing moves +0xA4 there, `sub_0041E7A0` builds the view rect from it, and the
  render camera is a copy of the logic camera. So one value moves everything consistently: culling,
  tiles, sprites, spawn and removal. To change it during play without a jump, let the game's zoom
  smoothing do it (it moves towards the new target over a few ticks).
* **Main side effects at 2x:** 4x the area, but the ped and car caps stay the same (the city looks
  emptier). The block renderer's per-layer tile list has **1500 slots** (the port already guards it
  at 0x0046BB9A and logs the peak), so expect dropped tiles near 2x, more so in widescreen.

---

## 1. Writers of cam+0xA0 (height) and cam+0xA4 (zoom)

### 1.1 Per-tick flow (player tick `sub_004A7E80`, control mode 0) [V]

```
sub_0041F410(cam=player+0x90, ped)   0x004A7FB9   follow: builds the target (x, y, z, zoom)
sub_0041F2F0(cam)                    0x004A7FC0   smoothing + projection/rect
if (player+0x6C == 0) memcpy(player+0x14C, cam, 0xBC)   0x004A80C2-0x004A80D2   render camera = logic camera
if (player+0x6C == 1) debug free camera on +0x14C (sub_0041EA10, sub_0041F2F0)   0x004A80DC-0x004A8123
```
Per frame the renderer uses `[0x5E3CC4]` = player+0x14C (copied at 0x0045A5AE, widescreen.md).
Control modes 2/3 follow `player+0x208` (aux camera; `sub_0041EE30` follows an object) the same way.

### 1.2 Who writes what

| function | writes | notes |
|---|---|---|
| `sub_0041F410(cam, ped)` (follow) | target via `sub_0041E3D0(x, y, z, zoom)`: `+0x10 = x`, `+0x14 = y`, `+0x18 = z + cam+0x40`, `+0x1C = zoom` (0x0041E3DB-0x0041E3FC) | zoom argument = `[0x5E3ECC]` if the ped state is 9 (dead), else **`[0x5E3EB4]`** (0x0041F53B-0x0041F546). z = the ped's (or car's) z through `sub_0041E130` against 1.0 / 2.0 (0x0041F469-0x0041F495 / 0x0041F4DA-0x0041F506) [V calls, I exact formula]. In a car `sub_0041EBF0(car, &x, &y, &z)` adds a speed/heading look-ahead (polar offset `sub_0041E210`, 240 or 180 depending on the case, x0.75 in some cases) and can raise z with speed [I: from its use of the speed against 0x5E3F74; the z write at 0x0041EC80 was not decoded fully] |
| `sub_0041F2F0(cam)` (smoothing, called by the player tick) | `+0x98/+0x9C` via `sub_00472670` (rate +0xAC/+0xB0, step 0x5E3F38); **`+0xA0`** via `sub_004727E0` (rate +0xB4, limits 0x5E4050 / 0x5E3CFC / 0x5E3DEC / 0x5E3EBC); **`+0xA4`** via `sub_00472670` (rate +0xB8, limits 0x5E3D28 / 0x5E3E54) (0x0041F341-0x0041F3C1) | mode `cam+0x3C`: 1 = smooth, 2 = **snap** (`+0x98..+0xA4 = +0x00..+0x0C`, 0x0041F31D-0x0041F336). Then `sub_0041E7A0` (rect) and `+0x00..+0x0C = +0x10..+0x1C` (0x0041F3E5-0x0041F3FC) |
| `sub_0041E010(cam)` (snap now) | `+0x98..+0xA4 = +0x00..+0x0C`, rates = 0 | callers: `sub_004A6350` (player spawn), `sub_004A6400`, `sub_004A7910` / `sub_004A7BA0` (death / busted), `sub_004A81E0`, `sub_0047DAD0` [I: script camera command] |
| `sub_0041F580` (camera ctor) | `+0xA4 = [0x5E3EB4]` (0x0041F592-0x0041F597), `+0x40 = 0` (`sub_0041DFB0`) | |
| `sub_0041E580(dst, src)` | `dst` target += src movement (copies the logic camera's motion into the debug camera) | 0x004A811C |
| `sub_0041EA10` | debug free camera: target = current ± 0.5 per held key (0x5E3F54) | modes 1 / player+0x6C == 1 |
| **"debug camera height"** `sub_004A4CB0` (player+0x76 / +0x77 held) | `cam+0x40` -= / += 0.5 (0x5E3E90), clamped to 0 .. **20.0** (0x5E3FB4) (`sub_0041E510`, `sub_0041E4E0`) for +0x90 and +0x208; reset to 0 by `sub_0041DFB0` (ctor, and the system-key handler `sub_004A7010`) | it adds to the target **height** (+0x18), not to the zoom |
| `sub_0041E7A0` (per camera, per update) | reads +0xA0/+0xA4, writes +0x60/+0x64 and the rects +0x20..+0x2C, +0x78..+0x84 | the port already replaces its tail for widescreen (src/gta2_manual.c) |

Steady state while walking **and** while driving: zoom target = `[0x5E3EB4]` (0.8875). What changes
with driving is the look-ahead (x/y) and the height (z) [I]; the zoom is constant except when dead.

---

## 2. Best hook for a factor F

**Option A (recommended): scale the zoom constant.**
```c
static const uint32_t ZOOM_WALK = 0x38CC;   // 284/320 (check: read R32(0x5E3EB4) once at start)
static const uint32_t ZOOM_DEAD = 0xE333;   // 1136/320 [I: compute 0x470<<14/0x140]
R32(0x5E3EB4) = (uint32_t)(((uint64_t)orig_walk << 14) / F_q14);
R32(0x5E3ECC) = (uint32_t)(((uint64_t)orig_dead << 14) / F_q14);
```
* Read the original values once from guest memory (they are made by static init, `sub_004FDF40` /
  `sub_004FDF70`, before the game starts) instead of hard-coding them.
* Readers are only 0x0041EEA1 (aux camera), 0x0041F546 / 0x0041F53B (follow), 0x0041F592 /
  0x0041F59D (ctor) [V xrefs], so no gameplay code depends on the exact value.
* Projection: scale = W·zoom (`+0x60`), rect half-width = (7 + z)/zoom/2 → halving zoom doubles the
  visible width and height [V widescreen.md 2a]. The renderer's tile ranges use `D/zoom` per layer
  (`sub_00472110`) and sprites use the rect, so both follow automatically.
* Changing F during play: write the constant; the follow code sets the new target next tick and the
  zoom smoothing (`sub_00472670` with its rate limits) glides there. To jump at once, also set
  `cam+0xA4` of the logic and render cameras (player+0x90, +0x14C, +0x208).

**Option B: raise the height.** Adding to `cam+0x40` (what the debug keys do) also zooms out
(`rhw = 1/(8 - z_obj + cam_z)`, rect `(7 + z)`) but changes the perspective of building sides and is
clamped at 20. Not recommended for a clean 1x..2x.

**Avoid:** writing +0xA4 after `sub_0041F2F0` each tick. The smoothing would fight it, and the
logic rect (+0x78..+0x84, made inside `sub_0041F2F0`) would not match the drawn view.

**One place suffices** [V]: the render camera is a byte copy of the logic camera every tick
(0x004A80C7-0x004A80D2), so with option A the logic rect (spawning, removal, "visible" tests) and
the drawn view always agree. Check the port's frame interpolation (src/gta2_framerate.c reads
player+0x14C +0x98..) still interpolates only x/y/z, not a stale zoom.

---

## 3. Side effects of a farther view

| system | follows the camera? | effect at F = 2 |
|---|---|---|
| ped generator `sub_004415E0` → `sub_00440CC0` | **yes**: spawn ring = logic rect +0.5 block (enhancements.md 1.3) [V] | peds still appear off screen, but the caps do not grow: 50 dummies, on-screen count < zone density/25 (20), 1-2 tries per tick. With 4x the area the streets look about 4x emptier → pair with the PED DENSITY option (enhancements.md) |
| ped / corpse removal | **yes**: "off every camera" counter ped+0x20E reset while inside a player camera rect (enhancements.md) [V] | no popping |
| traffic | spawn uses `sub_0045BC10` (visible to any logic camera) [V] | spawns further out; cap 16 recyclable cars [V enhancements.md] → thinner traffic |
| cars "may delete" / teleport / police spawn | `sub_0045BC10` / `sub_0040CF60` on the logic rect [V widescreen.md 2e] | police and cars appear further away (later) |
| sprites in view `sub_00447390` | render rect ±1 / +1.5 blocks [V] | automatic |
| block renderer `sub_00472110` | ranges from `(8 - L + cam_z)/zoom` [V] | automatic, but **per-layer tile list max 1500** (port guard 0x0046BB9A, logs peaks ≥ 1000). At F = 2 the tile count per layer is about 4x the normal one; measure the logged peak before allowing 2x, and lower the maximum (e.g. 1.6x) or enlarge the list if it is reached |
| lighting `gbh_SetCamera` (0x00472437) | uses the same tile ints [V widescreen.md 2c] | automatic |
| map edge | rect clamped to 0..0x3FFFFF [V] | near the map border the view shows the void outside the map (already true at 1x) |
| HUD arrows | screen positions are computed from the camera each tick (`sub_004C7FC0`, uses the player position and the render camera) [I] | should stay at the screen edge; not verified |
| HUD in-world labels / `sub_0041E710` (on-screen test, 640x480 bounds) | screen-space [V widescreen.md] | unaffected |
| sound distance | not traced; the audio update uses positions, not the zoom [I] | probably unchanged (sounds of far, now visible things may be quiet) |

---

## 4. Comparisons against camera constants

* Zoom smoothing limits 0x5E3D28 / 0x5E3E54 and height limits 0x5E4050 / 0x5E3CFC / 0x5E3DEC /
  0x5E3EBC (`sub_00472670` / `sub_004727E0` arguments) are **rates**, built at static init from
  `[0x5E3D34]` divided by 1 / 4 / 40 / 5 / 12 (0x004FE255-0x004FE348) [V]; they limit the change per
  tick, not the value. A lower zoom target is reached a little more slowly [I].
* Debug height: `cam+0x40` limited to 0..20.0 (0x5E3FB4) [V].
* View rect clamp 0..0x3FFFFF (map size) [V].
* No code found that compares +0xA4 or +0xA0 with a fixed "too far / too high" value. Searched: all
  references to +0xA0/+0xA4 in the camera functions 0x0041D000-0x0041F800 and the xrefs of every
  camera constant above.
* Projection depth: `rhw = 1/(8 - z_obj + cam_z)` [V widescreen.md 2a]; option A does not touch z.

---

## 5. Open

1. Exact z formula in `sub_0041F410` / `sub_0041EBF0` (speed raise of the height) — only needed for
   option B.
2. Tile list peak at F = 1.5 / 2.0 in the densest district, with and without widescreen (port log).
3. Whether `sub_0047DAD0` and other script camera commands set their own zoom (they use the follow +
   snap functions, so they should pick up the scaled constant) [I].
4. HUD arrow behaviour and audio fall-off at 2x.
