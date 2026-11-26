# GTA 2 gameplay enhancements: ped / traffic density, corpses, live aspect ratio

Target: gta2.exe (2004 freeware build, image base 0x400000).
Goal: specify three optional "gameplay enhancement" settings for the port:
(1) pedestrian (and car) density multiplier 1x..4x, (2) keep corpses and blood longer,
(3) switch the gameplay aspect ratio during play.

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).
Fixed point Q14 (1.0 = 0x4000). `this` in ecx (thiscall) unless noted. One logic tick = 33 ms
(30.3 Hz, framerate.md), so 30 ticks = about 1 s. Source: `analysis/disasm/asm/text.asm`.
Nothing here was tested on a running build. Conventions and known addresses: gameplay_input_pause.md
(game object `[0x5EB4FC]`, ped+0x278 state 9 = dead, ped+0x1AC/+0x1B0 position, world tick
`sub_0045C1F0`), frontend.md (debug flags 0x5EAD50..), widescreen.md (camera layout, Hor+ patches).

---

## 0. Summary

* **Peds.** Ambient peds are made by a generator that runs once per tick per player camera
  (`sub_004415E0` -> `sub_00440CC0` -> `sub_004404F0`). Three limits apply:
  (a) a **global cap of 50** "dummy" peds (u16 at `[[0x5E5BC0]]+0`, halved at wanted level > 3),
  (b) a **per-zone cap**: peds visible on screen < `zone.ped_density / 25` (zone info from the map
  and the mission script, default 500, so 20 on screen),
  (c) only **1-2 spawn attempts per tick per camera**, on a ring 0.5 block outside the view.
  The ped pool has **200 slots** (0x294 bytes each) and the pop function has **no empty check**
  (null dereference when full), so 4x needs a bigger pool and a guard.
* **Traffic.** Hard cap **16 recyclable traffic cars** (`cmp ecx,0x10` at 0x004B4A86 and 0x004B34FC),
  spawn interval from `zone.car_density`; every traffic car brings a driver ped from the ped pool.
* **Corpses.** A dead ambient ped stays **as long as it is inside any player's camera rect**.
  Off screen its slot is recycled after **40 ticks** (1.3 s, word `[0x5E5D80]`) for class-3 peds,
  100 ticks for class 4/6, and in any case after **200 ticks** off screen (dead branch in the ped tick).
  Corpses occupy ped-pool slots and **count toward the 50 cap and the on-screen zone count**.
  The blood pool is a separate particle with a fixed **800-tick** life (0x0048CC99).
* **Aspect.** The game already re-sets the video mode mid-game on WM_SETFOCUS and Alt+Enter with
  `sub_004CC5C0` (re-reads `Screen\full_width/full_height` from the registry and calls Vid_SetMode)
  followed by `sub_004CB880` (refreshes the three cameras + HUD layout). With the port's
  `sub_004CB150` replacement this sequence is a safe way to switch the aspect live, between ticks.

---

## 1. Pedestrian density

### 1.1 Objects and pools [V]

| object | address | size / count | ctor | notes |
|---|---|---|---|---|
| ped manager | `[0x5E5BC0]` | 0xC bytes | `sub_004416B0` (alloc at 0x0045C910) | tick `sub_00445A20` from world tick 0x0045C273 |
| ped pool | `[0x5E5BBC]` | 0x203AC bytes: `+0` free head, `+4` active head, `+8` **200 peds x 0x294**, `+0x20274` dword, `+0x203A8` u16 active count | `sub_0043E0E0` (alloc `push 0x203AC` 0x004416D8; `push 0xC8` 0x0043E0EE; `mov ecx,0xC7` 0x0043E107) | dtor `sub_0043AE40` (`push 0xC8` 0x0043AE52). Link field ped+0x160 |
| ped physics objects (ped+0x168) | `[0x66A3B4]` | 0x11944 bytes: `+0` free head, `+4` **400 x 0xB4**, link +0x78 | `sub_00435550` (alloc `push 0x11944` 0x00441729; `push 0x190` 0x0043555E; `mov ecx,0x18F` 0x00435574; last link +0x11908 0x00435586) | alloc `sub_004355A0` returns 0 when empty and `sub_004360C0` handles it [V 0x004360CE] |
| ped list nodes (8 bytes: ped, next) | `[0x5E5FBC]` | 0x324 bytes, **100 nodes** | `sub_004355E0` (alloc `push 0x324` 0x0044177E; `mov ecx,0x63` 0x004355E4; last link +0x320 0x004355FD) | pop `sub_00445EF0` has **no empty check** [V 0x00445EF0-0x00445EF7]. Users: per-tick list 0x5E5EE0 (`sub_00445F80`, cleared each tick at 0x00445A8E), car occupant lists (`sub_00445F10` from 0x0043F2A0) |
| sprites | `[0x66FF1C]` | 0x49B28 bytes, **5031 x 0x3C** | `sub_004BCA20` | shared by everything; alloc `sub_00421000` |
| particles (blood, smoke ...) | `[0x669E70]` | 0x947C bytes, **500 x 0x4C** | `sub_0048F1E0` | alloc `sub_0048C930` checks pool and sprite pool and returns 0 [V 0x0048C93C-0x0048C952] |

Ped pool pop `sub_00403890` [V]: `esi = [pool]; [pool] = [esi+0x160]; ...` - **no test for an empty
free list**: with 200 peds alive the next spawn reads `[0+0x160]` and crashes. Nothing checks
`+0x203A8` before allocating [V: the only accesses are 0x0043E129 and `sub_00445960`].
Callers that do test the result: `sub_004404F0` (ambient creation, 0x00440617), `sub_00473170`
(0x0047319E), 0x00480327. `sub_0043DF60` (generic "new ped", 12 callers) calls `sub_004039A0` on
the result without a test (0x0043DF72) [V].

No code computes a ped index from a pointer (scan for 0x294 finds only the pool ctor/dtor) [V scan],
so the array can move or grow as long as the free list is built over it.

### 1.2 Per-ped fields used here [V unless noted]

| field | meaning |
|---|---|
| ped+0x160 | next in pool list (free or active) |
| ped+0x168 | physics object (on foot), 0 in a car; sprite at phys+0x80 |
| ped+0x200 | unique ped id (`sub_0043AE10` searches it), from counter 0x591E84 (`sub_00435B80`) |
| ped+0x20E | u16 **state timer**: +1 per tick in `sub_0043ECC0`, reset to 0 while the ped is on screen (`sub_00436890` 0x00436909). = "ticks off screen" |
| ped+0x21C bit 0 | "alive in world"; set at alloc (0x00435B93), cleared by the removal start `sub_0043E650` (0x0043E7E9) |
| ped+0x234 | u8 removal countdown: 99 = never (set at alloc 0x00435BA2), 2 = set by `sub_0043E650` (0x0043E7E2), counted down while bit 0 is clear (0x00445909-0x0044591D); at 0 the tick returns 1 and `sub_00445960` frees the ped and its physics object |
| ped+0x238 | **class** (setter `sub_00403920`): 2 player [V `sub_004A6350`], 3 plain ambient "dummy" (default, `sub_00435720` 0x0043573C; ambient creation 0x00440632), 4 mugger / police units / gang [I from setters `sub_004404F0` type 1, `sub_004A9B40..sub_004AB060`, `sub_0043D640`], 5 script / special [I: `sub_00477560`, `sub_004802F0`, `sub_004ADB70`], 6 car thief / traffic driver [I: `sub_004404F0` type 2, `sub_0043DC60` from 0x00423567] |
| ped+0x240 | occupation (setter `sub_00403970`): 0xF mugger, 0x10 car thief, 0x13 gang, 0x17, 0x23, 0x2C ... [V values, I names] |
| ped+0x278 | state, 9 = dead |

### 1.3 Generator [V]

`sub_00445A20` (ped manager tick, world tick 0x0045C273):

```
reset counters [0x5E5BE8] (all live peds), [0x5E5BEA] (dummies), [0x5E5BEB], [0x5E5BEC], [0x5E5BF4] (on screen)
sub_00445960(pool)          // tick every active ped (sub_004454E0), free those that return 1, count
if (!skip_dummies [0x5EAD69]) sub_004415E0()         // 0x00445AAA-0x00445ABF
mgr+2 = [0x5E5BEA]; mgr+3 = [0x5E5BE8]; mgr+4 = [0x5E5BEB]; mgr+5 = [0x5E5BEC]; mgr+6 = [0x5E5BF4]   // 0x00445AC4-0x00445AEC
```

Counting in the ped tick `sub_004454E0` (jump table 0x00445948 on class-3, before any removal test):
class 3 -> `++[0x5E5BEA]` (0x0044555D); class 4/6 -> `++[0x5E5BEA]` unless ped+0x28C == 1, then
`++[0x5E5BEC]`; class 5 -> `++[0x5E5BEB]`. So **dead peds and peds already being removed are counted**.
`[0x5E5BF4]` is incremented in `sub_00436890` (0x00436902) whenever the ped's sprite is inside a
player camera (dead peds included) and for the player (0x0043EDA6). The debug overlay
`sub_0045B750` (do_show_counters) prints mgr+2 as "dummy_chars" and the zone value as "density";
`sub_00445A20` (do_iain_test) prints `[0x5E5BF4]` as "num peds on screen".

`sub_004415E0` (the global cap):

```
cap = u16 [[0x5E5BC0]+0];                     // 50, written only by the ctor (mov word [esi],0x32 at 0x0044183E) [V scan]
if ([[0x66B7A0]+0x654] > 3) cap >>= 1;        // 0x004415EE-0x004415FC; [I] +0x654 = wanted level
if ([0x5E5BEA] >= cap) return;                // 0x00441601-0x0044160A
for each player camera (sub_0045A800 / sub_0045A850 = logic camera player+0x90):
    [0x5E5BDE] = (camera moving faster than [0x5E5E4C]) ? 1 : 2    // attempts; 1 = only on the leading edge (dir [0x5E5F40])
    sub_00440CC0(mgr, cam)
```

`sub_00440CC0(mgr, cam)` (thiscall, ret 4): spawn ring = logic camera rect `+0x78..+0x84` grown by
0.5 block (0x5E5E74) [V 0x00440CE1-0x00440D52]. Loop `[0x5E5BDE]` times: pick an edge
(`[0x5E5BCE]`, cycles 0..3, or the leading edge) and a position along it (cursor `[0x5E5BCF]` +
random), then:

```
zi = sub_00465250([0x662C08], x, y)           // zone info of that block (see 1.4)
if (mgr+6 /*on-screen peds, previous tick*/ >= u16 zi+0xA / 25) skip   // 0x0044103C-0x0044105E (0x51EB851F, sar 3)
if (!sub_00466910(map, x, y)) skip            // [I] walkable ground
if (sub_0045BC10(game, x, y)) skip            // visible to a player
if (sub_00433430(map, ...)) skip              // [I] blocked
sub_004404F0(x, y, z, ...)                    // create ambient ped
```

`sub_004404F0`: pops a ped (0x0044051C), `++[0x5E5BCC]` (creation counter), rolls 0..999 and picks the
type from the zone ratios (+0xC mugger, +0xE car thief, +0x10 elvis, +0x12 gang, +0x14 police ped)
[V 0x0044054B-0x004405ED]. Special types are rate limited by globals that `sub_0043E650` decrements:
mugger `[0x5E5BD0]` and car thief `[0x5E5BD1]` one at a time, `[0x5E5BD2]`, `[0x5E5BD3]`, gang
`[0x5E5BD4]` < 8 (0x004405C5) [V]. Cheats LASVEGAS / NEKKID / no_annoying_chars override the type
(0x004405F3-0x00440610). Created peds start with ped+0x20E = 1 (0x00440640).

### 1.4 Zone info (the per-area numbers) [V]

Map object `[0x662C08]`: `+0x328` zones present, `+0x32C` zone list (u16 count first), `+0x330` u8
zone -> info index, `+0x334` info array of **22-byte records** (index 0 = default).
`sub_00465250(x, y)` finds the first zone of type 1, 0xA or 0xF that contains (x, y) (`sub_00465090`)
and returns its record (`sub_00462C60`), else record 0.

| off | meaning | default (`sub_00464330` 0x00464474-0x004644AE) | setter |
|---|---|---|---|
| +0x0 | car density (0..1000) | 500 | `sub_00462CE0` |
| +0x2 / +0x4 / +0x6 | car type ratios [I good / bad / police] | 300 / 300 / 100 | `sub_00462D00..` |
| +0x8 | car ratio [I gang] | 0 | |
| +0xA | **ped density** (on-screen cap = value / 25) | 500 | |
| +0xC / +0xE / +0x10 / +0x12 / +0x14 | ped ratios mugger / car thief / elvis / gang / police | 50 / 40 / 10 / 200 / 40 | ... `sub_00462E20` |

The setters are called from the mission script interpreter (`sub_00475230` region) [I: the GTA 2
script's MAP_ZONE info]. The values can therefore change during a mission; scale them at the
comparison, not in the table.

### 1.5 What to change for a factor K (1..4)

| item | where | change | notes |
|---|---|---|---|
| global cap | u16 `[[0x5E5BC0]+0]` | `50*K` | write after every `sub_004416B0` (new manager per game, 0x0045C92A) or patch 0x0044183E. Wanted > 3 still halves it |
| zone cap | 0x00441055-0x0044105E | compare `mgr+6 < (density/25)*K` | hook before `cmp ecx, edx` at 0x0044105C: `edx *= K` |
| spawn attempts | `sub_00440CC0` | call it K times per camera from `sub_004415E0`, re-checking the global cap between calls with the delta of `[0x5E5BCC]` | do **not** multiply `[0x5E5BDE]` in place: it persists when `sub_00433E90(cam)` is false (0x00441635) and would grow every tick |
| ped pool | 1.1 | about `200 + 120*(K-1)` slots (e.g. 560 at 4x) | replace `sub_0043E0E0` (build the free list over an external array; keep `+0`, `+4`, `+0x20274`, `+0x203A8` at their offsets), `sub_0043AE40` (destruct that array) and the alloc size 0x004416D8 if the header grows |
| physics pool | 1.1 | >= ped pool + 50 | alloc 0x00441729, counts 0x0043555E / 0x00435574, last link 0x00435586 |
| list nodes | 1.1 | 100 -> 400 | alloc 0x0044177E, count 0x004355E4, last link 0x004355FD. Several hit lists fill with peds in fights (0x5E5EE0 via `sub_004350A0` from the weapon code 0x4CDxxx); exhaustion = null dereference |
| guard | `sub_00403890` (ped pool pop) | return 0 when `[pool] == 0`, log it | protects `sub_004404F0` and the two other checked callers; additionally skip the ambient generator when free slots < 32 (N - `+0x203A8`) so unchecked callers (`sub_0043DF60`) never see an empty pool |

What scales with it automatically: nothing else. Muggers, car thieves and gang members stay rate
limited (1.3); police on foot come from the police manager `[0x66B7A0]`, not from density [I].
Traffic is a separate system (1.6). The per-ped tick cost is linear, but some helpers walk the
whole active list per call (`sub_0043AE10` id search, list `sub_00446010`) [V], so expect super-linear
CPU cost; measure at 4x. The spawn ring follows the (widened) logic camera (widescreen.md 2e).

### 1.6 Car traffic density [V]

* Car manager `[0x5E4CA4]` (ctor `sub_00428FA0`, tick `sub_0042A4C0` from 0x0045C294): `+0x28`
  "recycled cars" (traffic), `+0x40` "prot. recycled cars", `+0x3C` mission cars, `+0x34` unit cars
  (debug strings at 0x57380C..), `+0x54` spawn cooldown (decremented per tick 0x0042A4F4).
  Car pool `[0x5E4CA0]`: **306 x 0xBC** (`sub_00426DB0`, alloc 0xE0C4 at 0x00428FDE).
* Traffic generator `[0x5E4CA8]`, `sub_004B4E60` per tick: skipped entirely by **skip_recycling**
  0x5EAD5B (0x004B4E60-0x004B4E6D); per player camera `sub_004B4A60` -> `sub_004B34E0(dir...)`.
* Cap: `if (mgr+0x40 + mgr+0x28 == 16) no spawn` at **0x004B4A86** and **0x004B34FC** (both
  `cmp ecx, 0x10` + `je`, equality). **limit_recycling** 0x5EADAC: stop at 2 recycled cars (0x004B4A60-0x004B4A78).
* Interval: player+0x680 = credit, +1 per tick up to 100 (`sub_004A7E80` 0x004A7EA6-0x004A7EB8).
  `sub_00424980(player, zone_info, factor [0x66C460], out)`: `d = car_density/1000`;
  `thr = 100 - min(99, int(d * factor * 100))` -> player+0x682 (0x00424A38); spawn only if
  credit >= thr, which then costs `thr` (0x004B4973-0x004B49A0). `factor` comes from
  `sub_004B4A60` (camera area / 86 times a wanted-level constant, 0x004B4AF1-0x004B4B6F) [V formula,
  I meaning]. Zone car density 0 = no traffic (0x004B3A13).
* Each traffic car gets a driver ped (`sub_00423560` 0x004B48E2 or `sub_00423510` 0x004B4900) [V].
* Recycling of cars: `sub_00424090`, per car: counter car+0x76 reset while visible to any player
  (margin `[0x5E4CB8]`), else +1 (max 9999); removed (`sub_00421470`) at >= 130 ticks when the
  recycle conditions hold (0x00424187) [V].

For K: change both `je` at 0x004B4A86 / 0x004B34FC to `>= 16*K`; scale `factor` by K in a wrapper of
`sub_00424980` (arg 3, Q14), which shortens the interval (thr floors at 1); budget **16*(K-1) more
driver peds** in the ped pool. The car pool (306) is not the limit [I: traffic + parked + mission cars
stay far below it in normal play; verify with the "cars:%d" counter `sub_0045AD30`].

---

## 2. Dead bodies and blood

### 2.1 Death [V]

`sub_004411B0` (kill ped): sets state 9 / sub-state 0xF (0x004411FA), and if **do_blood**
0x5EAD51, on foot and phys+0x6C != 0x11: `sub_0048CC50([0x669E74], x, y, z)` = **blood pool**
(0x004413C0-0x004413FA). Immediate removal (`sub_0043E650`) at death only for occupation 2
(tail jump 0x00441289, jump table 0x00441404/0x00441418) and for non-player peds killed inside a
car (0x0044131A-0x00441359); occupation 0x17 and 0x18..0x1A first ask other managers
(`sub_00473010`, `sub_004A9500`). Everyone else stays as a corpse.

### 2.2 Removal of a corpse, per tick [V]

`sub_004454E0` (ped tick) -> `sub_0043ECC0` (per class, jump table 0x0043F16C):

* **class 3** (0x0043F0A7, on foot): `++ped+0x20E`; if state == 1: remove at exactly 200; else
  `if (ped+0x20E >= (s16)[0x5E5D80]) sub_0043E650(ped)` (**0x0043F0EB-0x0043F0F9**, then 0x0043F15B);
  otherwise `sub_00436890(ped)` resets ped+0x20E to 0 when on screen. `[0x5E5D80]` = 40, copied from
  0x591E7C by `sub_004416B0` (0x004418A4) and the static init `sub_00505C80`. **Alive dummies use the
  same limit** (off screen 40 ticks -> recycled).
* **class 4 / 6** (0x0043EF76): `++ped+0x20E`; `<= 100` -> reset if on screen (**0x0043EFC7**
  `cmp dx, 0x64`); `> 100`: occupation 0x2C waits for > 500 and its group; 0x23 or
  `sub_004331E0` (occupation 0x17..0x1B, 0x25) is kept; else removed (0x0043F02A).
* **class 5** (0x0043EF36): never removed by this switch on foot.
* **in a car** (phys == 0, 0x0043F10B): a dead class-3 occupant is removed at once (0x0043F14E).
* then the **dead branch** of `sub_004454E0` (0x00445754, reached for state 9 because
  `sub_00436890` clears 0x591E8C for states 8/9): `if (ped+0x20E > 200 && occupation != 0x17 &&
  class != 5) sub_0043E650(ped)` (**0x004457A7** `cmp word [esi+0x20e], 0xC8`).

On-screen test: `sub_00436890` -> `sub_00491EA0(phys)` -> `sub_0045C420(game, sprite, [0x66A4D8])`
-> `sub_0045BBA0` per player -> `sub_0045AEA0`: sprite x/y inside logic camera player+0x90 rect
**+0x20..+0x2C** (view rect + 0.75 block), or camera +0x208 when player+0x2D0 [V]. Margin
`[0x66A4D8]` is never written [V scan] -> 0 [I: zero-initialised .data].

Removal pipeline: `sub_0043E650` decrements the type counters, removes the ped from lists, frees
group links, clears ped+0x21C bit 0 and sets ped+0x234 = 2; two ticks later the tick sees
ped+0x234 == 0, calls `sub_0043BB50` and returns 1; `sub_00445960` frees the physics object
(`sub_00435F70`, sprite gone) and moves the ped to the free list [V]. `sub_0043E650` is called from
42 places in all game phases [V], so calling it from port code at a tick boundary is safe.

Result in the original: a corpse lasts **as long as any player sees it**; off screen it goes after
40 ticks (class 3) or 101..201 ticks (others). There is **no corpse count limit** [V: no other
removal of state-9 peds by count found]. Corpses keep their ped slot, physics object and sprite,
and are counted in `[0x5E5BEA]` (global cap) and `[0x5E5BF4]` (zone on-screen cap), so many corpses
on screen already reduce new spawns.

`skip_recycling` / `limit_recycling` are traffic flags only (1.6); they do not touch peds [V: their
only readers are 0x00424093, 0x004B4E60, 0x004B4A60 and the registry/cheat code].

### 2.3 Blood pool [V]

`sub_0048CC50`: particle from `[0x669E70]` (fails quietly when the particle or sprite pool is full),
type **0x27** (+0x38, only this creator sets it), **life +0x2C = +0x2E = 0x320 = 800 ticks** (26 s,
`mov eax, 0x320` at **0x0048CC99**), grows over 6 frames (+0x46/+0x48). The particle tick
`sub_00490760` decrements +0x2C for every particle first (0x0049076B), the type-0x27 handler
`sub_0048BE60` returns 1 (free) at 0. Life runs on and off screen; the pool is not linked to the ped.

### 2.4 Recommended implementation

Setting CORPSES: ORIGINAL / LONG (e.g. 60 s off screen) / UNTIL AREA LEFT.

1. One helper `corpse_limit(ped)` in ticks (u16, < 65535). For UNTIL AREA LEFT: return "keep"
   while the corpse is within R blocks (e.g. 16, about two screens) of any player ped
   (ped+0x1AC/+0x1B0 of `[player+0x2C4]`), else remove; cap the age at a maximum (e.g. 5 min).
2. Hooks, each only for `ped+0x278 == 9`, original value otherwise:
   * 0x0043F0EB (class 3): limit `[0x5E5D80]` -> `corpse_limit`.
   * 0x0043EFC7 (class 4/6): 100 -> `corpse_limit`.
   * 0x004457A7 (dead branch): 200 -> `corpse_limit`. **Required**: patching only 0x0043F0EB lets
     the dead branch remove every off-screen corpse after 201 ticks.
3. Optional, recommended with LONG / UNTIL AREA LEFT: do not count corpses: skip `++[0x5E5BEA]`
   (0x0044555D path) and `++[0x5E5BF4]` (0x00436902) for state 9, so living density stays as before.
4. Budget (required with 3): in a wrapper of `sub_00445A20`, before calling the original, walk the
   active list (`[[0x5E5BBC]+4]`, next = ped+0x160), count `state == 9 && (ped+0x21C & 1)`.
   If corpses > MAX (e.g. 32) or free slots (N - `+0x203A8`) < 40: `sub_0043E650` on the corpse with the
   largest ped+0x20E (off screen first, on-screen ones only as last resort).
5. Blood: life at 0x0048CC99 (writes both +0x2C and +0x2E) -> e.g. 9000 ticks (5 min). For UNTIL AREA
   LEFT wrap `sub_0048BE60`: while the sprite (+0x30) is within R blocks of a player, `++[p+0x2C]`
   (undoes the decrement at 0x0049076B). Blood pools beyond 500 particles in total silently stop
   smoke/explosion effects; either cap long-lived pools (e.g. 64, free the oldest by setting its +0x2C
   to 1) or enlarge the particle pool (alloc 0x947C at 0x00491BB2, count 0x1F4 at 0x0048F1EC, loop
   0x1F3 at 0x0048F1FC; the last link +0x9468 and the active count +0x9478 sit right after the array,
   +0x9478 is used by `sub_00491C50` 0x00491C5C / 0x00491C66, so it must move with it).

Risks: pool exhaustion crashes (1.1) if step 4 is skipped; more sprites drawn per frame; dead
script peds (class 5) are unchanged; network games and replays must use identical settings, the
simulation changes [I: the game is deterministic lock-step, framerate.md]; blood "longer than the
corpse" looks odd in ORIGINAL mode, so tie both to the same setting.

---

## 3. Changing the gameplay aspect ratio during play

### 3.1 How the mode is chosen and set [V]

* Registry read `sub_004CB290` (no args, returns 1 if anything changed): in the frontend
  (`[0x595018] != 0`) forces 640x480; in game reads `window_width` -> `[0x673578]`,
  `window_height` -> `[0x6732E8]`, **`full_width` -> `[0x6732E4]`, `full_height` -> `[0x6732E0]`**
  (0x004CB2E8-0x004CB342), `start_mode` -> `[0x595014]` (1 = fullscreen path), `tripple_buffer`.
* `sub_004CC5C0` (full re-set): `sub_004CB1D0` (frame-rate keys), `sub_004CB290`, `sub_004CADA0`,
  Vid_CloseScreen (slot 0x59503C), then windowed `sub_004CB730` (uses window_width/height) or
  fullscreen **`sub_004CB570`**: `Vid_CheckMode(full_width, full_height, 16)` (0x004CB58A), fallback
  640x480 written back to the registry, `Vid_SetMode` (0x004CB617), window calls, `sub_004CAD30`;
  finally `sub_004CB990` (gamma).
* `sub_004CB880` = `sub_004CAEB0` (gbh_SetColourDepth + one-time `sub_004CADE0`) then
  `sub_004CB150` (camera refresh -> `sub_0045A4F0(W, H)`: `sub_0041EB70` on cameras player+0x90,
  +0x208, +0x14C and HUD re-layout `sub_004CABE0(0x672F40)`; then `sub_004619D0` force_tick and
  `sub_004619E0` re-base the tick clock). The port replaces `sub_004CB150` (src/gta2_manual.c) so a
  width-only change refreshes the cameras (widescreen.md Patch 1).
* Callers: game entry `sub_004CC6A0` from WinMain 0x004D14F6 (once per frontend/game switch, only if
  `sub_004CB290` reports a change or `[0x6735A5]`); and **mid-game** from the WndProc `sub_004D0A00`:
  WM_SETFOCUS (0x004D0B01 `sub_004CC5C0`, 0x004D0B06 `sub_004CB880`, 0x004D0B0B `[0x6735A5]=0`,
  skipped while a movie plays, `sub_00481DD0`), Alt+Enter (0x004D0D2C) and SC_MAXIMIZE (0x004D0D7D).
  WndProc runs from DispatchMessage in the WinMain pump (0x004D1505-0x004D152A), i.e. **between**
  loop iterations, never inside a tick or a draw [V]. So the original already changes the mode
  in the middle of a game.

### 3.2 What depends on the width, and when it is refreshed [V]

| state | source | refreshed |
|---|---|---|
| VidState +0x48/+0x4C, CPU surface +0x50/+0x54 | Vid_SetMode (port: `src/render/vid.c`, reallocates `cpu_buf`, `gfx_set_mode`) | at the mode set; the surface pointer is fetched again every frame (`sub_004CAEC0` GetSurface) |
| screen table, clip window | `sub_004CAEC0`: MakeScreenTable(+0x50, +0x54, +0x4C), gbh_SetWindow(0,0,W-1,H-1), `[0x673584]`/`[0x673474]` | every frame |
| camera viewport +0x68/+0x6C, centre +0x70/+0x74, HUD scale +0xA8 | `sub_0041EB70` (port hook: +0xA8 = H/480) | `sub_004CB150` -> `sub_0045A4F0` |
| projection +0x60/+0x64, view rects +0x78..+0x84 / +0x20..+0x2C (spawn ring, visibility, sprite query) | `sub_0041E7A0` (port Hor+ hook) | every camera update (per tick / frame); +0x60/+0x64 hold -1 until then (0x0041EB70) |
| tile span, HUD anchors, exploding-score cull, `sub_0041E710` bounds | port hooks read A from the render camera per call | every call |
| HUD element layout | `sub_004CABE0` | `sub_0045A4F0` |

Nothing else caches the width at game time [V: widescreen.md section 4 scan of 0x280/0x1E0 immediates;
`sub_004CAEC0` is the only gameplay reader of VidState +0x48 besides ClearScreen 0x004CC78A].

### 3.3 Safe sequence

On the game thread, at the start of a WinMain loop iteration (e.g. in the port's wrapper of
`sub_00462A30`, before calling it), never inside the pause-menu draw or a tick:

1. Preconditions: in game (`[0x595018] == 0`), `[0x5EB4FC] != 0`, no movie (`sub_00481DD0()` == 0),
   surface not locked (VidState+4 bit 1 clear), single player (`[0x673E2C] == 0`) [I: the logic
   camera rect changes the simulation, so network games must not change it alone].
2. Write `Screen\full_width` = `round(480*ratio) & ~1`, `full_height` = 480 (what `apply_aspect` does).
3. `sub_004CC5C0()` then `sub_004CB880()`, then `[0x6735A5] = 0` - the exact WM_SETFOCUS sequence.
   (Or `sub_004CC6A0()`, which runs the same two calls only when `sub_004CB290` sees the change.)
4. Nothing else to refresh (3.2). The forced tick from `sub_004619D0` runs the camera update before
   the next draw, so the -1 placeholders in +0x60/+0x64 are not drawn [I: framerate.md, force_tick
   runs a tick at the next iteration].

Visible side effects: going narrower turns the side strips into "off screen", so peds there start
their 40-tick and cars their 130-tick recycle timers; going wider shows empty side strips until the
spawn ring (outside the new view) fills them as the player moves. Both are harmless.

Not safe: writing only VidState w/h or only the camera fields: the CPU surface and the renderer's
targets would keep the old size, and `sub_004CB150`'s change test (`[0x6735A0]`, port `g_ws_last_w`)
would be bypassed. Calling the sequence from inside a tick (camera objects in use) or while the
surface is locked.

[I] Because WM_SETFOCUS re-reads the registry, an alt-tab after changing ASPECT RATIO may already
apply it today if the port's window layer delivers WM_SETFOCUS to `sub_004D0A00`; worth a quick test,
it confirms the path.

---

## 4. Recommended implementation (summary)

1. **PED DENSITY 1x/2x/3x/4x**: enlarge the ped pool (~560), physics pool (>= ped pool + 50), list
   nodes (400) at boot; guard `sub_00403890`; per game set `[[0x5E5BC0]] = 50*K`; zone compare at
   0x0044105C `* K`; call `sub_00440CC0` K times per camera with a cap re-check; skip generation when
   fewer than 32 ped slots are free. Optional **TRAFFIC DENSITY**: 0x004B4A86 / 0x004B34FC
   `== 16` -> `>= 16*K`, factor arg of `sub_00424980` `* K`.
2. **CORPSES**: hooks at 0x0043F0EB, 0x0043EFC7 and 0x004457A7 for state 9 with one limit
   (time and/or distance), corpses excluded from the two counters, a corpse budget in a
   `sub_00445A20` wrapper that removes the oldest off-screen corpse with `sub_0043E650`; blood life
   0x0048CC99 raised (and frozen near players for UNTIL AREA LEFT) with a cap on long-lived pools.
3. **LIVE ASPECT**: registry write + `sub_004CC5C0` + `sub_004CB880` at a loop-iteration boundary,
   in game, single player, no movie. Keep the port's `sub_004CB150` replacement.

## 5. Open questions

1. Names of ped classes 4/5/6 and occupations (from setters only); which class police on foot use.
2. `[[0x66B7A0]+0x654]` assumed to be the wanted level (halves the ped cap, picks traffic constants).
3. Real peak use of the 200 ped slots, 100 list nodes, 500 particles and 5031 sprites in normal play
   (log `+0x203A8`, `[0x669E70]+0x9478` and free-list lengths) to size the pools for 4x.
4. Whether `sub_004404F0` and other creation paths cope with `sub_004360C0` failing (physics pool
   full); size the physics pool so it cannot happen.
5. Whether more spawn attempts per tick at 4x make peds pop in visibly in narrow streets
   (generation is outside the view, so it should not).
6. The car pool (306) users besides traffic, and the traffic `factor` formula in `sub_004B4A60`
   (0x004B4AF1-0x004B4B6F; table 0x004B4E24 by wanted level).
7. Whether the port delivers WM_SETFOCUS / Alt+Enter to the guest WndProc (3.3 note).
8. Margin `[0x66A4D8]` assumed 0 (never written).
