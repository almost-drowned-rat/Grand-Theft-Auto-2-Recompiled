# GTA 2 frame pacing and logic timing (gta2.exe, 2004 freeware build)

Scope: how the in-game loop paces frames and advances game logic, and what a
recompilation needs for 60 / 120 FPS without changing game speed.

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).
Addresses are gta2.exe VAs (base 0x400000). Fixed point = 18.14 (`sub_00401B10`: `fild; fmul 1/16384` [V 0x00401B12, const 0x0056E3C4 = 6.1035e-5]).

---

## 0. Summary

* GTA 2 is a **fixed-tick, frame-locked simulation**. One logic tick = one call of
  `sub_00461930` -> `Game::Tick` `sub_0045C3B0`. A tick is due every **33 ms of
  `timeGetTime`** (`sub_0045A460` returns 0x21) -> 30.30 Hz. That matches the measured 30.24 FPS.
* The loop draws **at most one frame per tick**. Extra iterations busy-wait (PeekMessage loop, no Sleep).
  So rendering and logic are interleaved 1:1, not decoupled.
* No delta time exists. The simulation uses no clock at all. There are no float time constants in the
  image, and all speeds, timers and animation counters are per-tick integers or 18.14 fixed-point values.
* Registry `max_frame_rate` (default 1) = **enable the 33 ms limiter**. 0 = no limiter, tick+draw as
  fast as possible, so the game runs too fast. `min_frame_rate` (default 0) = **catch-up / frame-skip
  mode**: keep the tick schedule and run up to 8 ticks without drawing. 0 = re-base the schedule at each
  tick, so the game slows down on a slow PC.
* rdtsc ("Perf: Clock speed") and QPC have **no gameplay effect**. rdtsc feeds only the `do_mike` debug
  profiler overlay. QPC is only in the CRT `/GS` cookie init.
* Recommended 60/120 FPS approach: keep the 33 ms tick exactly as it is. Draw every free loop iteration
  (or at a target rate), and **interpolate camera + sprite positions** between the last two ticks for
  the extra frames. Section 5 has the details.

---

## 1. Main loop

### 1.1 WinMain `sub_004D1170` [V]
After init, the message pump at 0x004D14FB–0x004D15A6:
`PeekMessageA` (0x004D1505). Then, if no message is pending and the app is not paused/minimised
(`bl`, `[0x673F50] != 2`, …):
* frontend active -> `sub_0045A320` (menu, 0x004D1557), which uses its own `timeGetTime` for menu
  animations/attract timer (0x0045A325, 0x00453486, 0x00456C4F, 0x00459027). This is not part of in-game timing.
* in game -> **`sub_00462A30`** (0x004D159D), one loop iteration.

There is **no Sleep / WaitMessage** in the in-game loop [V: no Sleep import call in sub_004D1170 or
sub_00462A30]. The only `Sleep` in the image is the 500 ms rdtsc calibration (0x004C38A3). The original
therefore spins at 100 % CPU between ticks.

### 1.2 Per-iteration scheduler `sub_00462A30` [V]
Globals:

| addr | type | meaning |
|---|---|---|
| 0x662748 | u32 | `next_tick_time` (timeGetTime ms) |
| 0x662740 | u8 | `ticks_since_draw` (count of ticks not yet drawn) |
| 0x6626E3 | u8 | `input_pending`: read input before the next tick |
| 0x66274C | u8 | `force_tick` (set by `sub_004619D0` after a video mode change, `sub_004CB150`) |
| 0x595010 | u8 | `limiter_on` = registry `max_frame_rate != 0` (`sub_004CB1D0` 0x004CB20B; forced 1 in network game 0x004CB1D9) |
| 0x673598 | u8 | `catchup_on` = registry `min_frame_rate != 0` (0x004CB21A; forced 1 in network game 0x004CB1E0) |
| 0x5EAD88 | u8 | debug `skip_draw` (registry debug key, 0x00451EEC / 0x00461BEC) |
| 0x673E2C | u32 | network game flag |
| 0x5EB4FC | Game* | game object |

Pseudocode (single player, `[0x673E2C] == 0`, 0x00462A87–0x00462B60):
```c
now = timeGetTime();                                  // 0x00462A45
due = 0;
if (limiter_on || ticks_since_draw) { if (now >= next_tick_time) due = 1; }
else due = 1;                                         // limiter off: tick immediately after a draw
if (ticks_since_draw >= 8) due = 0;                   // catch-up cap -> forced draw
else if (!catchup_on && ticks_since_draw) due = 0;    // no catch-up: always draw after each tick
if (force_tick || skip_draw) due = 1;
if (due) {                                            // ---- LOGIC TICK ----
    if (!catchup_on) next_tick_time = now;            // re-base (no catch-up)  0x00462AE2
    if (input_pending) { sub_004626D0(); input_pending = 0; }   // read + apply input
    sub_00461930();                                   // Game::Tick
    next_tick_time += sub_0045A460(game);             // +33 ms                0x00462B27
    force_tick = 0; ticks_since_draw++; input_pending = 1;
} else if (ticks_since_draw) {                        // ---- DRAW ----
    sub_00461960();                                   // render + Vid_FlipBuffers
    ticks_since_draw = 0;
} else if (input_pending) {                           // ---- INPUT ----
    sub_004626D0(); input_pending = 0;
}
if (timing_obj /*0x671E9C*/) sub_004C3760(timing_obj, drew, ticked);   // do_show_timing stats
return tick_result;
```
With the default registry (limiter on, catch-up off), each 33 ms period runs:
iteration 1 = tick, iteration 2 = draw+flip, iteration 3 = input read, then spin until the deadline.
That gives exactly one draw per tick. The tick period is 33 ms after the *actual* tick time
(re-base), so the true rate is <= 30.30 Hz (measured 30.24) [V+measurement].

Network game (`[0x673E2C] != 0`, 0x00462A56–0x00462A82): if `now >= next`: `sub_00461930()` (tick),
then `sub_004626D0()` (input exchange with peers, **draw at 0x0046278F**, receive), and `next = now + interval`.

### 1.3 Tick interval `sub_0045A460` [V]
```c
if (!network) return game->ff_flag /*+0x30*/ ? 11 : 33;
return game->net_interval /*+0x34*/;
```
* `game+0x34` is set by `sub_0045A540` from the network game-speed setting `[0x673F38]`:
  0 -> 50 ms, 1 -> 40 ms, 2 -> 33 ms [V 0x0045A550–0x0045A57E].
* `game+0x30` = **fast-forward** debug toggle. `sub_004A4750` (`flag = !flag`) is called from the
  debug key handler `sub_004A7010` (0x004A72BB), but only when `do_debug_keys` (0x5EAD87) is set.
  While on, "FF" (0x573834) is printed by `sub_0045BA10` [V]. 11 ms = 3x speed. This shows that game
  speed comes only from the tick rate.

### 1.4 What runs where [V]
* `sub_00461930` = tick: `Game::Tick sub_0045C3B0(game)`, then `sub_004C3680(timing+0x18)` (timing stats only).
* `sub_0045C3B0`: `sub_0045A650` (per player, `sub_004A7680`), then by `game->state` (`[game+0]`):
  0/1 -> `sub_0045C1F0` (full world update), 2 -> `sub_0045A690` (reduced update: players
  `sub_004A61F0`, HUD `sub_004C6D90`). Then `sub_00410520`, unless `skip_audio`, and `sub_0045A4D0`
  (`game+0x28` countdown) [V].
  [I] State 2 is a pause/"waiting" state. It has not been checked which code writes `[game+0]`.
* `sub_0045C1F0` = world tick. Per player `sub_004A7E80`, then managers in fixed order:
  0x5D81DC `sub_0040FE50`, 0x5E6680 `sub_00446890`, (lighting) 0x6622B8 `sub_0045C1E0`, 0x5E747C
  `sub_00449C40`, 0x5E7D70 `sub_0044A990`, 0x5EBB30 `sub_0045E2E0`, 0x5E5BC0 `sub_00445A20`
  (`inc word [0x5E5BF8]` = a tick counter), 0x66BB50 `sub_004B1560`, 0x669AE4 `sub_00489680`,
  0x5E4CA4 `sub_0042A4C0`, … 0x672F40 `sub_004CAB50` (HUD logic), 0x66F5DC `sub_004B9260`
  (unless `exploding_on`), `sub_004CB9C0` (countdown 0x673594) [V list 0x0045C1F0–0x0045C3A8].
  [I] The exact roles of the managers (cars, peds, objects, …) are not named yet.
* `sub_004626D0` (single player): goes to 0x004627FE, `sub_0045FD10(0x5ECAC8)` reads the controller
  bits and `sub_004A4930` applies them to the player [V].
* `sub_00461960` = draw: `sub_0045A5A0(game)` (render), `Vid_FlipBuffers [0x595044]` (0x00461971),
  then an optional `Vid_ClearScreen` [V].
* `sub_0045A5A0` = render: sets `cur_cam [0x5E3CC4] = game->cur_player(+0x1C) + 0x14C` (0x0045A5AE),
  then tiles/world, `sub_0046BB40` (0x6633C8), `sub_004B9E50`, sprites `sub_0044B890`
  (0x5E8B80, 7 layers), `sub_00447390` x3, `Vid_GetSurface` wrapper `sub_004CAEC0`,
  `gbh_BeginScene`, `sub_00472110` (lights + `gbh_SetCamera`), `sub_004B98B0`, HUD draw
  `sub_004CA440`, `do_mike` overlay `sub_00474530`, `gbh_EndScene`, `sub_004CAF50` [V].

---

## 2. Logic advance: fixed step, frame-locked

**[V]** The tick has no time input. `sub_00461930` / `sub_0045C3B0` / `sub_0045C1F0` take no
time argument, and none of the 19 direct `timeGetTime`/`GetTickCount` call sites (plus the indirect `call ebx`/`call edi` ones in sub_00462140 and sub_004C3760) is in the simulation:

| site | owner | purpose |
|---|---|---|
| 0x00462A45, 0x004627C1, 0x0046202D, 0x004619E0 | loop / game start / resume | tick schedule `0x662748` |
| 0x00462140 (0x004621FD…) | network input receive | 1000 ms peer timeout |
| 0x0040B356, 0x0040B3DE, 0x0040B489, 0x0040BFA9, 0x0040C0E6, 0x00407D57 | network object 0x674268 | session / timeouts |
| 0x0045A325, 0x00453486, 0x00456C4F, 0x00459027 | frontend | menu timing |
| 0x004C3673, 0x004C3683, 0x004C3760 (via edi) | timing object 0x671E9C | FPS/TPS statistics (`do_show_timing`) |
| 0x004C3894, 0x004C38B4 (+Sleep 500) | clock object 0x671EBC | rdtsc calibration |
| 0x004E0BE9 + QPC 0x004E0BF5 | CRT `__security_init_cookie` | XOR seed only |

**[V]** The image has no float or double time constants (searched for 1/30, 30, 0.033, 33, 1000,
0.001, 60, 1/60: none). The only per-second logic constants are integer tick counts:
* `sub_00462530`: `[0x662738]++`, `== 30` -> once-per-second network bookkeeping (0x0046254C).
* HUD timer display `sub_004C8E30`: value `/30` -> seconds, `/60` -> minutes (magic 0x88888889,
  0x004C8E36). Blink when `value % 15 <= 7` (0x004C8EF3–0x004C8F01). So **mission/HUD timers count ticks**.
* `sub_004B1BD0` (0x004B1C8C): `(60 - n)*60/30`-style fade from a per-tick countdown.
* `game+0x28` countdown (`sub_0045A4D0`), `[0x673594]` countdown (`sub_004CB9C0`): decrement once per tick.

**[I]** Physics, peds, cars, animation frames, traffic, flashing and so on are all per tick.
Reasons: there is no delta variable anywhere, logic uses fixed-point integers only, and the measured
speed is correct at 30 Hz. The FF debug mode (11 ms) runs everything 3x faster, and with
`max_frame_rate=0` the game runs too fast (well-known PC behaviour).

**There is no delta-time variable and no per-frame time scale** that could be set to 0.5.

---

## 3. Render path side effects (needed for extra frames)

[V, static] Call-graph closure: 317 functions are reachable from render `sub_0045A5A0`, and 168 of
them are not reachable from the tick or from input (`sub_0045C3B0`, `sub_0045FD10`, `sub_004A4930`).
Every direct write to a global in those render-only functions goes to renderer scratch:
0x6632B8–0x66364C (tile/clip state), 0x66FE28–0x66FE94, 0x6733F8–0x673468, 0x5E3CC4 (cur_cam),
0x673474/0x673584 (surface), 0x5E8B78/0x5E8B7C (profiler), 0x5E85BC, 0x594DF0. The only
read-modify-write through a pointer is `inc [ebx+0x2F00]` (polygon counter) in the tile drawers.
The CRT `rand` (`sub_004D71F7`) is **not** reachable from render.
The sprite lists are consumed during render (`sub_004C4D60` clears `*list = 0` after drawing,
0x004C4D6E). Those lists are rebuilt by the tick [I: the producer is in logic, since render only clears them].

Consequence [I]: calling `sub_0045A5A0` + flip several times per tick does not change gameplay
state. But **the second draw of the same tick would show no sprites**, because the lists are cleared
after the first draw. A recomp must either snapshot the lists and restore them before each extra
draw, or skip the clear (`sub_004C4D60` 0x004C4D6E) and clear only at the start of the next tick.
This must be verified at run time. Writes through pointers in deeper callees (for example a
"last drawn" field on sprites) are not fully excluded by this static scan.

---

## 4. "Perf: Clock speed" / rdtsc / QPC [V]

* Clock object at **0x671EBC**, built by `sub_004C3910` (via thunk 0x00560C85).
  `sub_004C3880`: rdtsc, GetTickCount, `Sleep(500)`, rdtsc, GetTickCount, then
  `[obj] = (dTSC) / (dTick)` = cycles per ms (`__aulldiv` `sub_004D6260`). The log line
  "Perf: Clock speed = %d MHz" = `cycles_per_ms / 1000` (magic 0x10624DD3 >> 6) goes to
  OutputDebugStringA.
* Users: `sub_004C3950` = raw rdtsc (called only by `sub_0044B8B0` / `sub_0044B990`, render-side
  sprite draw/sort timers that accumulate into 0x5E8B7C / 0x5E8B78). `sub_004C3970` = cycles -> ms
  (only `sub_00474530`, the `do_mike` debug profiler overlay, which prints those totals and resets them).
* So rdtsc only feeds **debug profiling**. QPC is only the CRT security cookie (0x004E0BF5).
  Neither affects gameplay. In a recomp, rdtsc can return any monotonic value.

---

## 5. 60 / 120 FPS: recommended approach

### 5.1 Approaches that do not work
* **Running logic per rendered frame** (`max_frame_rate=0` or interval 16/8 ms) speeds everything up:
  movement, ped/car physics, sprite animation frames, HUD/mission timers (`/30`), flashing (`%15`),
  the once-per-30-ticks network counter, all countdowns. The image has no time scale to compensate.
  Changing every per-tick constant is not feasible.
* **Drawing the same tick twice** without interpolation gives no visible gain. It also needs the
  sprite-list fix (section 3).

### 5.2 Recommended: 30.3 Hz fixed tick + interpolated extra frames
1. **Keep the tick** exactly: 33 ms (or 11/net interval from `sub_0045A460`), same input-then-tick order.
   Choose one scheduler, and keep the original semantics as the default (`min_frame_rate`=0 re-base):
   * original re-base semantics (`next = now + 33`) -> about 30.2 Hz, bit-exact with the original, or
   * accumulator (`next += 33`, as with `catchup_on`) -> exactly 30.30 Hz.
2. **Replace `sub_00462A30`** in the recomp with a decoupled loop:
   `while (now >= next) { input; tick; snapshot(); next += 33; }` (cap 8, as the original), then
   `alpha = 1 - (next - now)/33` in [0,1], then `render_interpolated(alpha)`, then present (vsync or
   a 60/120 limiter). Sleep or wait instead of spinning.
3. **Input**: read input once per tick, right before the tick. This is the same data flow as the
   original (`input_pending`), with lower latency, and it does not change speed.
4. **State to interpolate** (prev tick -> current tick, applied temporarily before `sub_0045A5A0`
   and restored afterwards):
   * **Camera** = `cur_player(+0x1C) + 0x14C` (pointer stored in 0x5E3CC4).
     Fields read by render: **+0x98 / +0x9C (x / y offset, 18.14), +0xA0 (height z)** [V: sub_0046BBF0
     adds +0x98/+0x9C to world x/y; sub_0046BC70 uses +0xA0 for perspective], **+0x60 (scale/zoom)**,
     +0x70/+0x74 (screen centre, int) [V sub_0046BC70], +0xA8 (used by `sub_0044B490`) [V].
     The camera is the most important part. In a top-down game, camera scroll at 30 Hz causes most of
     the judder.
   * **Sprites** (everything in the 7 sprite lists drawn by `sub_004BE060`): position fields
     **+0x14 / +0x18 / +0x1C** (x, y, z 18.14) [I: read together and passed to the projector in
     sub_004BE060 0x004BE15C–0x004BE164]. Rotation: candidate +0x00 (Ang16), not verified [I].
     +0x22/+0x24 (word, sprite/frame ids) must **not** be interpolated: animation frames stay per tick.
   * Not interpolated: HUD, text, timers, sprite frames, palette/flash, lighting levels. They are drawn
     with the latest tick values, which matches the original timing.
5. **Discontinuities**: skip interpolation when a value jumps more than a threshold (teleport,
   respawn, camera cut, angle wrap). Use the shortest angle path. Identify sprites by pointer plus a
   validity or generation check, because pools reuse objects. Sprites that exist in only one of the
   two ticks are drawn at the current position.
6. **Latency**: interpolating between tick n-1 and n adds up to 33 ms of display delay.
   Extrapolation (alpha > 1) avoids this but overshoots on collisions. Use interpolation by default.
7. **Network game**: keep the original path (draw inside `sub_004626D0`). If 60/120 is wanted there
   too, apply the same interpolation, but never change the tick or exchange cadence.

Expected result: game speed and behaviour are exactly as at 30 Hz (the tick code is untouched). Camera
scrolling and object motion are smooth at the display rate. Sprite animation stays at the original
30 Hz frame steps, which is correct.

---

## 6. Open questions

1. Sprite struct: confirm x/y/z at +0x14/+0x18/+0x1C and find the rotation field. Write a dynamic
   trace of `sub_004BE060` args over two ticks. Find where the 7 sprite lists (0x5E8B80, nodes from
   0x672578/0x67257C) are filled, to place the snapshot hook.
2. Confirm at run time that a second `sub_0045A5A0` in the same tick draws nothing from the sprite
   lists (section 3), and decide between snapshotting the lists and moving the clear.
3. Does the render path write per-object state through pointers (for example "visible this frame",
   last screen position used by the tick for culling or AI)? If yes, extra renders could change logic
   through visibility tests. Check which fields of sprite/car/ped objects are written in render-only
   functions (dynamic watch).
4. Camera fields +0x60 (zoom) and +0x98/+0x9C: confirm the camera update happens only in the tick
   (expected in the per-player `sub_004A7E80`), and check whether camera shake writes +0x98 per tick.
5. Meaning of `game->state` (`[game+0]` 0/1/2) and who writes it. Is state 2 pause?
6. Is there a game-internal RNG besides CRT `rand` (registry debug `log_random` suggests one), and is
   any of it reachable from render or HUD draw code?
7. Map tile animation (water/animated tiles) and HUD flashing: confirm they advance in the tick
   (0x672F40 `sub_004CAB50` HUD logic vs `sub_004CA440` HUD draw) and not by draw count.
