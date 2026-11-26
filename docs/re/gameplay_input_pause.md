# GTA 2 gameplay input, player state, pause and quit (gta2.exe, 2004 freeware build)

Goal: give the port what it needs for (A) a modern controller scheme ("stick points where to walk")
and (B) a real in-game pause menu (RESUME / RESTART AREA / QUIT TO MAIN MENU).

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).
Image base 0x400000. Fixed point Q14 (1.0 = 0x4000). `this` in ecx (thiscall) unless noted;
"ret N" = callee pops N bytes. Sources: `analysis/disasm/asm/text.asm`, frontend.md, framerate.md,
widescreen.md, audio.md, gbh_api.md. Nothing here was tested on a running build.

---

## 0. Summary

* **Input is digital only.** The keyboard and the (single) DirectInput joystick are read as
  *buffered events* by `sub_0044C0F0` and turned into **12 action bits** (bit i = action i) in the
  input recorder object **0x5ECAC8 + 4**. The joystick axes are only thresholded at ±750 of ±1000 into
  four digital directions. **There is no analogue path** a port could feed with stick values.
* Per tick the bits go to **player+4** and are decoded by `sub_004A4AE0` into one byte per action
  (**held** at player+0x78..+0x83, **changed** at player+0x84..+0x8D).
* On foot the game uses a turn velocity **player+8** (Ang16, accelerated by left/right, max 48 = 12°
  per tick) that is added every tick to the ped object's **heading [ped+0x168]+0x40** (Ang16,
  **1440 units = 360°**, 4 units per degree). The ped control function `sub_004A5C50` writes player+8,
  so a wrapper of it can set an exact turn toward a target direction. This is the clean hook for "stick
  points where to walk".
* Cars get **digital** controls only: steering is a tri-state byte (-1/0/+1) in the car physics
  object. No analogue steering value exists.
* The view is always **north-up**, with no camera rotation: screen right = world +x, screen down =
  world +y. Stick directions map straight to world directions.
* **Pause = DIK 0x40 (F6)**, not Esc and not P. It toggles `game->state` ([0x5EB4FC]+0) between
  1 (running) and 2 (paused). While paused the world tick does not run, but frames are still drawn and
  presented at the normal 30 Hz, the HUD shows the "PAUSED" panel, and audio closes stream slot 1 and
  plays freeze.wav.
* **Esc** opens the HUD quit prompt (player+0x78A). The game is **not** paused by it. Enter = quit,
  Esc = back. Quit = `sub_0045A4B0(game, 1, 2)`. Two ticks later the game loop returns to WinMain.
  WinMain stores the area result into the profile and saves it (`sub_004A8F90` → `sub_004A89E0`), then
  opens frontend **page 11 "PLAYER QUIT!"** with RESUME SAVED STATUS / REPLAY PREVIOUS AREA / BACK TO MAIN MENU.
* HUD drawing runs every frame in `sub_004CA440`, called at 0x0045A61F between gbh_BeginScene
  (0x0045A5F4) and gbh_EndScene (0x0045A638). A wrapper of it can draw a menu on top with
  `sub_004CC100` / `sub_004539F0` (game fonts 0..6) and, for a background, the HUD pause-panel sprites
  or a port-native translucent quad.

---

## 1. (A1) How gameplay reads the controls

### 1.1 Devices [V]
Input object `[0x5E8FA8]` (created in `sub_0045F780` 0x0045F7E4, init `sub_0044BF70`):
* keyboard device `[0x5E8F4C]`, joystick device `[0x5E8F58]` (first attached DIDEVTYPE_JOYSTICK, EnumDevices
  type 4 + callback 0x44BC40, `c_dfDIJoystick` 0x5780BC, coop level 5 = exclusive|foreground) [V 0x0044BC40-0x0044BCDE,
  0x0044BF01-0x0044BF0E].
* Joystick setup `sub_0044BCF0`: buffer size 10000, **range X and Y = -1000..+1000** (DIPROP_RANGE, by offset 0 / 4),
  **dead zone 2500** (25 %) on X and Y [V 0x0044BDB0-0x0044BE98].
* `sub_0044BF30` (Control\N, see frontend.md §6): `[0x5E8F5C+4i] = v & 0xFF` (code), `[0x5E8F1C+4i] = v >> 15`
  (**device: 0 = keyboard, 1 = joystick**) [V 0x0044BF46-0x0044BF57].

### 1.2 Event loop `sub_0044C0F0(input, u32* bits, bool apply)` (stdcall 8) [V]
Reads **one buffered event per device per pass** (`GetDeviceData` vtbl+0x28, cbObjectData 0x14) and loops while
events are pending (`sub_0044C070`). Keyboard event = `{dwOfs [0x5E8F8C], dwData [0x5E8F90]}`, joystick event =
`{[0x5E8F08], [0x5E8F0C]}`. For i = 0..11:
* keyboard, `type[i]==0 && code[i]==dwOfs`: press (`dwData & 0x80`) → `sub_0045ED10(i)` (bits |= mask[i]),
  release → `sub_0045ED30(i)` (bits &= ~mask[i]). Only if `apply` is set [V 0x0044C304-0x0044C363].
* joystick, `type[i]==1` [V 0x0044C372-0x0044C4E7]:
  * code 0xE0: X axis (dwOfs 0) ≤ -750 → press, else release; 0xE1: X ≥ +750; 0xE2: Y (dwOfs 4) ≤ -750; 0xE3: Y ≥ +750.
  * other code n: button, `dwOfs == n + 0x30` (DIJOFS_BUTTON(n)), press on `dwData & 0x80`.
  * So **Control\N for a joystick = 0x8000 | code** (bit 15 = device flag). This answers the open bit-15 question in frontend.md.
* A keyboard event that matches **no** action is a "system key". `sub_0044BFB0` ORs `dwOfs << 12` into bits 12..20 and
  sets **bit 21 (0x200000) on key down** [V 0x0044BFB0-0x0044BFD6]. Enter/Esc/Space/Tab are routed this way while the
  quit prompt or the network chat is open (`sub_004C6DE0` → `sub_004C5F30`/`sub_004C5F70`) [V].
* The masks at 0x5ECAC8+8+4i are `1 << i` [V `sub_0045F780` 0x0045F78A-0x0045F7D7, `sub_0045F640` 0x0045F65F-0x0045F6AC].

### 1.3 Per-tick path [V]
1. `sub_00462A30` → `sub_004626D0` (single player 0x004627FE) → **`sub_0045FD10(0x5ECAC8)`** (input recorder):
   clears bits 12..21 (`sub_0045ED00`, `sub_0045F9F0`). By mode `+0x38` (0 = live/record, 3 = replay playback,
   2 = none): live and not paused → `sub_0044C0F0(bits, 1)` + record. **Live and paused** → `sub_0044C0F0(bits, 0)`
   (action bits frozen); the system key is kept only if `sub_0045EDC0` says it is a "pause-safe" key
   (Esc 0x01, F6 0x40, numpad 0x37/0x47-0x4F.., or one the HUD wants) [V 0x0045FEBF-0x0045FF14, 0x0045ED50-0x0045EDAD].
   The result is returned and stored by `sub_004A4930`: **player+4 = bits** [V 0x00462808-0x00462812].
2. Game::Tick `sub_0045C3B0` → `sub_0045A650` → per player `sub_004A7680` (always, also when paused) [V]:
   `sub_004A4AE0` decodes, then the system key (bits>>12 & 0x1FF) goes to `sub_004A7010` (down) or
   `sub_004A49C0` (up), then **player+4 = 0** [V 0x004A7683-0x004A76BC].
3. World tick `sub_0045C1F0` → per player `sub_004A7E80` uses the decoded bytes (section 3).

### 1.4 Decoded action bytes (player = `[[0x5EB4FC]+0x38]`) [V sub_004A4AE0]

| bit | action | held byte | changed byte (held ≠ last tick) |
|---|---|---|---|
| 0 | forward | +0x78 | +0x8B |
| 1 | backward | +0x79 | +0x8C |
| 2 | left | +0x7A | – |
| 3 | right | +0x7B | – |
| 4 | attack | +0x7C | +0x8D |
| 5 | enter/exit | +0x7D | +0x89 |
| 6 | jump/handbrake | +0x7E | +0x8A |
| 7 | prev weapon | +0x7F | +0x88 |
| 8 | next weapon | +0x80 | +0x87 |
| 9 | special 1 | +0x81 | +0x84 |
| 10 | special 2 | +0x82 | +0x85 |
| 11 | special 3 | +0x83 | +0x86 |

"Pressed this tick" = held && changed (e.g. 0x004A7EF4-0x004A7F19 for weapon change) [V].
Special 2 held + a direction moves the camera instead of the ped (`sub_004A4CF0` → `sub_0041EEF0`, the direction
bytes are cleared) [V 0x004A4CF6-0x004A4D29; I: "look around"].

### 1.5 System keys handled in play (`sub_004A7010`, jump tables 0x4A75A4 / 0x4A7514) [V]

| DIK | key | effect | gate |
|---|---|---|---|
| 0x01 | Esc | quit prompt `player+0x78A = 1` (`sub_004C5F60`); with `skip_quit_confirm` 0x5EADAB: quit now `sub_0045A4B0(game,1,2)` | none |
| 0x3B / 0x3C | F1 / F2 | `sub_004B2CD0(0x5DCBC8, 0/1)` (needs a player vehicle, `sub_004A65E0`) [I: radio station -/+] | none |
| 0x3D / 0x3E | F3 / F4 | music volume -10 / +10, written to Sound\CDVol (0x004A716A) | **none** (answers frontend.md's open question) |
| **0x40** | **F6** | **pause toggle `sub_0045BAA0`** | none |
| 0x41 | F7 | HUD+0xE4 `sub_004C6830` [I: pager/message] | local player |
| 0x42 | F8 | `sub_004D0820` (minimise + GTA2 Manager) | local player |
| 0x43 | F9 | HUD+0x4C `sub_004A4770` (clears two fields) | local player |
| 0x44 | F10 | network chat | network only |
| 0x2E, 0x47-0x58, 0xB5, 0xC7, 0xC9, 0xD1, 2..0x0B | debug (FF, frame step numpad +, camera, ...) | `do_debug_keys` 0x5EAD87 |

`sub_004CA5D0` runs first and can consume the key (quit prompt / chat, section 6) [V 0x004A7029].

### 1.6 Consequences for the port
* Analogue values can't be fed in. A joystick axis only becomes a key-like direction at ±75 % [V 1.2]. Feeding
  virtual DIK presses, as the port does now, is equivalent.
* System keys of one input read are **ORed** into bits 12..20 [V `sub_0044BFB0`]. Two different system keys in one
  tick (33 ms) give a garbage code. Inject at most one system key per tick.
* Remapping a pad control while it is held can leave a stuck bit (press of DIK A, release of DIK B). Release the old
  key before switching. Unpause flushes all DI buffers and clears the held bits (`sub_0045BA60`, section 5.2).
* The port's START currently sends Esc, which opens the **quit prompt** and does not pause. Pause is F6.

---

## 2. (A2) Player, ped and vehicle state

Pointers [V]: `game = [0x5EB4FC]` (0 outside a game); `game+0x00` state (1 run, 2 paused, 0 single-step);
`game+0x04..+0x18` Player*[6], `+0x1C` viewed player (camera), `+0x23` u8 player count, `+0x24` local index,
**`+0x38` local player** (= players[+0x24]) [V ctor `sub_0045C4D0` 0x0045C50E-0x0045C599]. Player size 0x85C
(ctor `sub_004A83C0`).

| field | meaning | evidence |
|---|---|---|
| player+0x2C4 | **player ped** (Ped*) | [V set in `sub_004A5B40` 0x004A5B69; getter `sub_0041D020`] |
| player+0x2C8 / +0x2CC | second ped / object for control modes 2/3 | [V `sub_004A5130`, `sub_004A5150`, 0x004A7FD1] |
| player+0x68 | control mode: **0 normal** (ped control), 1 debug free camera, 2 control +0x2C8, 3 camera only (no control) | [V switch 0x004A7F5B, table 0x4A81CC; player creation writes 0 at 0x004A5BCF] [I names of 2/3] |
| player+0x2F | non-zero: ped input skipped (`sub_004A76D0` not called) | [V 0x004A7F9D] [I: script control lock] |
| player+0x30 | non-zero: enter/exit bits cleared every tick | [V 0x004A7ED0-0x004A7ED8] |
| player+0x28 | non-zero: attack ignored | [V 0x004A788C] |
| player+0x640 | busted flag (set from `ped+0x21C & 0x20`) → `sub_004A7BA0` | [V 0x004A813A-0x004A816D] |
| player+0x78A | Esc quit prompt shown | [V section 6] |
| player+0x788 / +0x718 | current weapon index (0xFFFF none) / weapon array | [V 0x004A789D-0x004A78B6] |
| ped+0x15C | owning Player* | [V `sub_00435C40` 0x00435C47] |
| ped+0x168 | ped physics object (on foot); heading at +0x40, back pointer +0x7C | [V `sub_004361B0`, `sub_0049BD10`] |
| **ped+0x16C** | **current vehicle** (Car*), 0 = on foot | [V `sub_00416B60`, `sub_00403B80`; set by `sub_00436070` 0x0043607D] |
| ped+0x150 | target car of an enter order | [V `sub_00403AA0` from 0x004A7781] |
| ped+0x154 | car being entered/left while `ped+0x27C ∈ {6,7}` | [V `sub_00433200`] [I meaning of 6/7] |
| ped+0x258 | objective: 0x23 enter car, 0x25 enter train/bus-type, 0x24 leave car, 0x26 leave train-type | [V `sub_004A76D0` 0x004A7726-0x004A77D6] |
| ped+0x278 | state: **9 = dead** (`sub_004411B0` 0x004411FA, `sub_0043E650`), **10 = in car** (`sub_00436070` 0x0043608F), 8, 7, 3 others | [V writes] [I names other than 9/10] |
| ped+0x248 | 1 = in vehicle **without control** (no driving, no on-foot control) | [V `sub_004A5040`, 0x004A78DA-0x004A78E3] [I: passenger / getting in] |
| ped+0x1AC / +0x1B0 | world x / y (Q14 blocks) | [V getters `sub_00403A00` / `sub_00403A10`] |
| car+0x54 | driver ped | [V `sub_004221D0` 0x004221E4] [I] |
| car+0x58 | car physics object (control bytes, section 3.3) | [V 0x00429892] |
| car+0x50 | car sprite; heading = u16 at [car+0x50]+0 (Ang16) | [V `sub_004361B0` 0x004361D7-0x004361E1] |
| car+0x84 | model; 0x3B/0x3C/0x3D/6 = train/bus-type (passenger, on-foot controls) | [V `sub_00403BA0`] [I names] |

**Per-frame classification** (read after the tick, from the port's frame hook):
```c
if (!game || [0x595018] /*frontend active*/) -> MENU/FRONTEND
P = [game+0x38]; ped = [P+0x2C4];
if ([game] == 2) -> PAUSED;      if (byte[P+0x78A]) -> QUIT_PROMPT
if (!ped || [P+0x68] != 0 || byte[P+0x2F]) -> NO_CONTROL (cutscene/script/debug)
if ([ped+0x278] == 9) -> DEAD;   if (byte[P+0x640]) -> BUSTED
car = [ped+0x16C];
if (!car) -> ([ped+0x258] == 0x23 || 0x25) ? ON_FOOT_ENTERING : ON_FOOT
if ([ped+0x248] == 1) -> IN_CAR_NO_CONTROL
if (car_is_train_type(car))  -> PASSENGER (game uses on-foot controls)
-> DRIVING (car = vehicle; [car+0x54] == ped expected)
```
This is the same decision tree `sub_004A76D0` uses to pick `sub_004A4C40` (drive) or `sub_004A5C50` (on foot)
[V 0x004A76E0-0x004A7901]. "Is driving" is simplest as: `sub_004A76D0` reached `sub_004A4C40` this tick. A port
wrapper of `sub_004A4C40` / `sub_004A5C50` can set a flag. Note: pressing any direction while the ped walks to a car
(objective 0x23) cancels the entry (`sub_0043E1E0`, called by the on-foot code for every direction) [V 0x0043E1E0-0x0043E25C].

---

## 3. (A3) Heading, turning and steering

### 3.1 Angle unit [V]
Ang16 = u16, normalised to **0..1439 (0x5A0)**: `sub_00401C10` adds/subtracts 1440. 1° = 4 units (base angle
`[0x66B20C] = 4`, from 0x574308). Sin/cos tables: `T_sin[i] = sin(i·π/720)` at 0x5D3938 (`sub_0040F390`),
`T_cos` at 0x5D6718 (`sub_0040E6B0`); lookup `sub_0040F500` / `sub_0040F520` [V].

### 3.2 On foot
* `sub_004A5C50(player, ped)` (thiscall, ret 4) is the **on-foot control** [V]:
  * `player+8` = **turn velocity** (Ang16, signed via wrap). Left held: `+8 += step`, clamped to +max. Right held:
    `+8 -= step`, clamped to -max. Neither held: `+8 = 0` [V 0x004A5C81-0x004A5DDD].
  * step = `player+0xA` = 20 (5°/tick², set at player creation 0x004A5B99 from 3+1+1 base angles) [V]. While attack is
    held and forward is not, step = `[0x66B3EC]` = 12 [V 0x004A5C93-0x004A5CA8].
  * max = `[0x66B3C8]` = 48 (12°/tick ≈ 360°/s) normally, `[0x66B284]` = 24 when `player+0x64` is set
    [V statics `sub_00546840`, `sub_00546850`, `sub_00545A60` (×12), `sub_00545A00` (×6)] [I meaning of +0x64].
  * `player+0xC` = **walk command** (Q14): forward → `[0x66B0F8]` = +1.0, backward → -1.0, else 0 [V 0x004A5DE1-0x004A5E1D].
  * Jump on edge of +0x7E (`sub_00433C40`), special 1 edge → `sub_00433DD0(ped, 0x14)` [V].
* The ped physics object (`ped+0x168`, class with tick `sub_0049A560` / `sub_0049BD10`) adds the turn velocity every
  tick: **`obj+0x40 += player+8`** (`sub_00436140` = read `[ped+0x15C]+8`, `sub_0041FA70` add + wrap)
  [V 0x0049A988-0x0049A99B, 0x0049BD45-0x0049BD5C]. The walk speed comes from `player+0xC` via `sub_00436200`
  [V 0x0049A9C1-0x0049A9CE].
* **Heading = u16 at `[ped+0x168]+0x40`** (`sub_00433A40` returns it, `sub_004361B0` = "ped heading": object,
  else the car's heading, else 0) [V].
* Movement vector: `dx = v·T_sin[θ]`, `dy = v·T_cos[θ]` with v > 0 (`sub_0041FC20` 0x0049A7FD-0x0049A822. The walk speed
  `[0x66A5F8]` = 6·0x100 Q14 positive by its static chain) [V call]. With the map index order of `sub_0042A830`
  (row = 2nd arg) this gives [I]: **θ = 0 → +y (south, screen down), 360 → +x (east, right), 720 → north (up),
  1080 → west (left)**. θ grows counter-clockwise on screen, which matches "left adds". **Verify once at run time**:
  walk forward and compare the sign of the change of `ped+0x1AC/+0x1B0` with θ.
* Order inside one tick [V]: `sub_004A7E80` (player, writes +8/+0xC) runs before the ped objects' tick (later manager
  in `sub_0045C1F0`). So a value written into player+8 **after `sub_004A5C50` returns** is the one applied this tick.

**Safe ways to point the ped:**
1. **Recommended: wrap `sub_004A5C50`** (single caller 0x004A7901). Call the original, then if the stick is active:
   `cur = u16[[ped+0x168]+0x40]`, `d = wrap_signed(target − cur)` (−720..719), `d = clamp(d, −48, +48)` (or larger for a
   faster snap: the add has no limit), store `u16[player+8] = (d < 0) ? d + 1440 : d`. Press the virtual *forward* key
   (so +0xC, the "walking" checks and the car-entry cancel all behave natively) and never press left/right. Pressing
   *attack* without *forward* also works (aim in place); the game's slow-turn rule only affects the step, which the
   wrapper overrides.
2. Without a hook: write `[[ped+0x168]+0x40]` directly between ticks (when no left/right key is sent the game adds 0).
   It snaps instantly and skips the game's turn feel. Check `ped+0x168 != 0` first (it is 0 in a car) [V `sub_004361B0`].
* Replays (`play_replay`/`constant_replay_save`) record key bits only, so option 1 or 2 breaks replay sync. Disable them
  or ignore them; they are debug features.

### 3.3 Car steering [V]
`sub_004A4C40(player, car)` → `sub_00429810(car, fwd, back, left, right, handbrake, special1, special1-edge, attack)`
(ret 0x20) → `sub_0049FB00(phys=[car+0x58], fwd, back, left, right, handbrake)`:
* **`phys+0xAD` steering: right only → 0xFF (-1), left only → +1, both/none → 0** [V 0x0049FB98-0x0049FBD2].
* `phys+0x91` accelerate, `phys+0x93` brake/reverse (depends on motion state `sub_0040F840`), `phys+0x92` handbrake [V].
* So steering and throttle are **digital**. The turn rate comes from the physics code. A stick can only choose
  left/none/right (optionally pulse it for partial steering [I]).
* "Point to drive" (optional): car heading = `u16[[car+0x50]+0]` (same Ang16 convention [I]). Steer left/right toward
  the stick angle with a dead band (±8–12° [I]), hold *forward* while the stick is pushed. Steering a moving car toward
  an absolute direction is a design choice, not a game feature. Keep the default car scheme "tank" (left/right = steer).

---

## 4. (A4) Camera orientation [V]
World→screen `sub_0046BBF0` / `sub_0046BC70`: `sx = (wx + cam+0x98)·zoom(cam+0x60)·rhw + cam+0x70`,
`sy = (wy + cam+0x9C)·zoom·rhw + cam+0x74`. There is no angle term, and no camera field is read with sin/cos
[V 0x0046BC10-0x0046BD24]. The view is **always north-up**: screen right = world +x, screen down = world +y.
A stick direction (sx, sy) maps 1:1 to world (x, y). With 3.2 the target angle is [I] `θ = atan2(sx, sy)` in Ang16
units (`θ = round(atan2(sx, sy)·720/π) mod 1440`, so stick down → 0, stick right → 360).

---

## 5. (B5) The existing pause

### 5.1 Trigger and flag [V]
* Key **F6 (DIK 0x40)** → `sub_004A7010` case 12 (0x004A71A7) → **`sub_0045BAA0(game)`**: state 1 → `sub_0045A6E0`
  (state = 2), else → `sub_0045BA60` (resume). Both do nothing unless `game+0x23 == 1` (single player) [V 0x0045A6E0,
  0x0045BA63].
* Flag: **`[game+0] == 2`** (`sub_00416BC0` is the "paused?" test) [V].
* Debug: numpad + (0x4E) → `sub_0045A6F0` state 0 = "run one tick, then pause again" (state 0 runs the world once and
  sets 2, 0x0045C3F0-0x0045C40A) [V; gated by do_debug_keys].

### 5.2 Resume `sub_0045BA60(game)` [V]
State = 1; `sub_0044BFF0` flushes the keyboard and joystick DI buffers (GetDeviceData with NULL buffer, INFINITE);
`sub_004A6900(player)` zeroes player+8/+0xC and sends all-zero controls to a driven car; `sub_004A4EA0(player)`;
`sub_0045AD90` clears the held action bits (0x5ECAC8+4 &= 0xFFFFF000).

### 5.3 What stops [V]
* Game::Tick `sub_0045C3B0` state 2 → `sub_0045A690` instead of the world tick `sub_0045C1F0`: only per-player
  `sub_004A61F0` (camera upkeep), HUD pause-panel logic `sub_004C6D90` (HUD+0x2A1C) and the `[0x673594]` countdown.
  All world managers, peds, cars, mission scripts, mission/HUD timers (they live in the world tick) stop.
* Still running: input read and system keys (Esc prompt, F6), audio update `sub_00410520`, and the game-end countdown
  `sub_0045A4D0` (so a quit started while paused completes).
* Audio `sub_00418050` (state-2 edge, `[0x5DCB58]` = last state): on entering pause it **closes stream slot 1**
  (`sub_004B6860(0x66C950, 1)` = AIL_close_stream) and plays **freeze.wav** in slot 1 at volume 127
  [V 0x0041807B-0x004180A0; audio.md].
* The tick scheduler (`sub_00462A30`) is not affected: ticks continue at 33 ms, so no catch-up happens on resume.
* Separate app-level pause: window inactive → `[0x673F50] = 2` and WinMain stops calling the game loop at all
  (no ticks, no frames) [V WndProc 0x004D0C28; framerate.md 1.1].

### 5.4 Pause/unpause from port code
Call between two loop iterations (e.g. in the port's wrapper of `sub_00462A30`, before the original):
* pause: `if ([game]==1 && byte[game+0x23]==1) sub_0045A6E0(game);` (or inject one F6 press)
* resume: `sub_0045BA60(game);` (not a plain `[game]=1`: the flush/reset in 5.2 prevents stuck keys).
While the port menu is open, send **no** virtual keys to the game (F6 would toggle, Esc would open the prompt) and
filter the real keyboard the same way.

---

## 6. (B6) Esc quit prompt

* Shown when **`byte[player+0x78A] != 0`** (set by Esc, `sub_004C5F60`) [V]. The game keeps running (no state
  change) [V: no other write]. Esc also works while paused (0x01 is a pause-safe key) [V 0x0045ED8B].
* Drawn by `sub_004C8710` (last call of the HUD draw, 0x004CA510): GXT `quit1` "Is that it?", `quit2` "Press the ENTER
  key to quit play.", `quit3` "Press the ESC key to resume play.", centred lines, font `[0x672F24]` [V 0x004C8731-0x004C886D].
  The PAUSED panel is suppressed while the prompt is open [V 0x004C9FC2-0x004C9FCD].
* Answers `sub_004C8690` (HUD+0x2A1E, runs first in `sub_004A7010` via `sub_004CA5D0`) [V]:
  * **Enter (0x1C)**: `+0x78A = 0`. If the player is the local one (`sub_0041DC70` = byte[player+0]),
    **`sub_0045A4B0(game, 1, 2)`**. Network: `sub_00461DD0(0x665704, ...)` [V 0x004C86A8-0x004C86E8].
  * **Esc (0x01)**: `+0x78A = 0`, key consumed (returns al = 1 through `mov al, al`, eax = 1) [V 0x004C86F3-0x004C8701].
  * Other keys: not consumed and the game continues normally.
* **End-of-game path** [V]:
  1. `sub_0045A4B0(game, delay, reason)` → `sub_0045A480(delay, reason, 0)`: if no end is pending (`game+0x28 == -1`)
     or the new delay is shorter: `game+0x28 = delay`, `game+0x2C = reason`, `[0x5EC070+0x404] = 0`.
  2. Every tick (also paused) `sub_0045A4D0` decrements +0x28 and returns 1 when it is 0. That is Game::Tick's
     return value → `sub_00462A30` → WinMain 0x004D15A2.
  3. WinMain 0x004D1628: `switch (game+0x2C)` (table 0x4D178C):
     1 → DestroyWindow (exit program); **2 = quit** → `sub_0045EC20(0x5EC070, player)`, `sub_004A8F90(profile [0x66B404], player)`
     (stores the area result and **saves the profile** `sub_004A89E0` at 0x004A903D), `sub_004A90A0(profile)`, then
     frontend page **11** (or 6 in a bonus area, `sub_0045E540`); 3 = dead/busted with no lives (from `sub_004A7910` /
     `sub_004A7BA0`, push 3) → page 2 (or 6); 4 = area complete → page 3 (or 6); 5 → page 7 (multiplayer); 6 → page 0.
  4. `sub_00462060` destroys the game (input recorder, `sub_00461910`, `[0x5EB4FC] = 0`), `[0x595018] = 1`, and at
     0x004D14BF the frontend is rebuilt and `sub_004587B0(fe, page)` opens the page (page passed in **edi**) [V 0x004D14CA].
  * No `.svg` (save point) is written. A quit records stats and the profile only.
* **Page 11 "PLAYER QUIT!"** (builder 0x0045439D-0x004544CC): item 0 `savepos` "RESUME SAVED STATUS" (action 0x104,
  y 0x188), item 1 `replay` "REPLAY PREVIOUS AREA" (0x103 = restart the area just left, `sub_0045E470(0x5EC070, L"")`,
  y 0x19C), item 2 `mainmen` "BACK TO MAIN MENU" (y 0x1B0, action from di [I: 0 = page 0]) [V].

**Port actions:**
* QUIT TO MAIN MENU = exactly the prompt's yes: `sub_0045A4B0(game, 1, 2)` (thiscall, ret 8), also allowed while paused.
  The game ends 2 ticks later. Optionally, when the frontend opens page 11 after a port-initiated quit, call
  `sub_004587B0(fe, 0)` to land on the main menu (hook point: frontend.md §11 wrappers, or check `fe+0x11E == 11` once).
* RESTART AREA = the same quit, then on page 11 select item 1 and confirm: in the port's `sub_004597C0` wrapper (frontend.md
  §3.4) set the cursor `page11+0xBC6 = 1` and the Enter edge `fe+0xC9BC = 1` once, so the game's own 0x103 handler runs.
* Never call these in network games (`[0x673E2C] != 0`).

---

## 7. (B7) Drawing over gameplay

* Frame: `sub_00461960` → render `sub_0045A5A0`: ... `gbh_BeginScene` (0x0045A5F4) → lights `sub_00472110` →
  `sub_004B98B0` → **HUD `sub_004CA440(HUD = [0x672F40])` (0x0045A61F)** → do_mike overlay → `gbh_EndScene` (0x0045A638) →
  `Vid_FlipBuffers` (0x00461971) [V framerate.md 1.4, gbh_api.md §6].
* `sub_004CA440` returns at once if `skip_user` (0x5EAD64) is set. Otherwise it draws all HUD members and ends with the
  pause panel `sub_004C9FA0` (0x004CA4F9) and the quit prompt (0x004CA510) [V]. **A wrapper of `sub_004CA440` (call the
  original, then draw) draws over everything**, inside Begin/EndScene, with `[0x5E3CC4]` (render camera) valid.
* Text functions usable in gameplay:
  * `sub_004CC100(u16* s, Q14 x, Q14 y, u16 font, Q14 scale, int* mode{2}, remap, 0, 0)` (ret 0x24): real pixel
    coordinates; uses the current style `[0x670684]` [V 0x004CC129].
  * `sub_004539F0(s, x, y, font, scale)` (ret 0x14) is only a mode-2 wrapper of it with no frontend dependency
    [V 0x004539F0-0x00453A1D], so it works in gameplay too.
  * `sub_004C7280(s, x, y, font, int* mode, 0, 0, 0)` (ret 0x20): HUD 640×480 virtual space ×`cam+0xA8` (widescreen.md §3).
    The port's widescreen anchoring keys on return addresses. A call from port code falls back to the "anchor by x" rule,
    so prefer `sub_004CC100` with pixel positions computed from the back-buffer size.
  * Width `sub_004CB0C0(s, font)`, line height `sub_004CC0C0(s, font)` [V].
  * GXT `[0x671550]` is the game's own object (`sub_0045C4D0` 0x0045C5FB), so `sub_004C23A0` works in play [V]. Useful keys:
    `pause` "PAUSED", `contnue` "CONTINUE", `replay`, `mainmen` "BACK TO MAIN MENU", `quit1..3`.
* **Fonts in gameplay**: the frontend object and fstyle are gone (`sub_004620A0` → `sub_00459760`), and `[0x670684]` is the
  level style (`sub_004C08D0`, 0x0045C644) [V]. `sub_004BFCC0` fills the logical font table for a 14-font style (fstyle) or
  a 7-font style; any other count is fatal [V 0x004BFCC4, 0x004BFD66]. For the 7-font style the ids are 0..6:
  `[0x670678]=0, [0x670668]=1, [0x670640]=2, [0x67065C]=3, [0x670650]=4, [0x670674]=5, [0x670654]=6` [V 0x004BFD6C-0x004BFDA2].
  HUD copies (`sub_004CA520`, non-Japanese) [V]: quit prompt / most HUD text `[0x672F24]` = 1, "PAUSED" title
  `[0x672F20]` = 6, `[0x672F28]` = 2, `[0x672F30]` = 5, `[0x672F2C]` = 0. **Do not use frontend ids 4/5 (`[0x67066C]`,
  `[0x67064C]`) in play**: they keep fstyle values that mean other glyph sets in the level style [I]. For a selected line,
  use another game font (e.g. 6 for the title, 1 normal, 2 or 5 highlighted) or a palette remap (mode 8 + remap,
  frontend.md §4.2) [I: which fonts look right needs a screenshot].
* **Background panel**:
  * The game's pause panel = HUD sprites type 6, frames **0x86 / 0x88 / 0x87** at virtual (227,180) / (320,180) / (413,180),
    drawn with `sub_004C71B0(6, frame, Q14 x, Q14 y, [0x672F98], &mode{2}, 0, 0, 0)` (ret 0x24) [V 0x004C9FD3-0x004CA080].
    A port menu can reuse them for the GTA 2 look. Their size is not measured [I].
  * `gbh_DrawFlatRect(GBH_Vertex v[4], argb)` (slot `[0x5952CC]`, stdcall 8) fills a quad, but it is **opaque** in d3ddll
    (alpha ignored) [V gbh_api.md §1.2] and in the port (`src/render/gbh.c` `gbh_DrawFlatRect`, BLEND_OPAQUE, alpha forced
    0xFF). Set v[i].z (+0x08) > 0.
  * A translucent dim needs a small port-native draw in the port's renderer (gbh.c: same `emit` with an alpha blend mode)
    called from the HUD wrapper. No guest code is needed for it.
* While the port menu is shown: skip the game's PAUSED panel by wrapping `sub_004C9FA0` (only caller 0x004CA4F9) [V], and
  keep `player+0x78A = 0`.

---

## 8. (B8) Frames while paused
* `sub_00462A30` has no state check: tick (Game::Tick in pause mode), draw (`sub_00461960` + Vid_FlipBuffers) and input
  read go on at 30.3 Hz exactly as in play [V framerate.md 1.2]. So **frames keep being drawn and presented**, and the HUD
  draw (and a port overlay in it) runs every frame. The menu can animate at the tick rate. With the port's 60/120 FPS
  scheme it can animate per presented frame.
* The world picture is frozen because no world tick runs. [I] The paused scene still shows peds/cars: framerate.md §3
  says the sprite lists are cleared after each draw and refilled by the tick. If the paused frame comes out without sprites
  in the port, the snapshot/restore described there is needed here too (check with F6 in the current build).

---

## 9. Recommended implementation

### (A) Modern controls
1. Keep the current virtual-key layer for buttons (attack, enter/exit, jump, weapons, specials) and for menus.
2. Add a per-frame state probe (section 2 pseudocode) after each tick: FRONTEND / PAUSED / PROMPT / NO_CONTROL / DEAD /
   BUSTED / ON_FOOT / PASSENGER / DRIVING.
3. ON_FOOT (and PASSENGER) with "modern" scheme: wrapper of **`sub_004A5C50`**. Original first, then override
   `u16 player+8` with the clamped signed difference to the stick angle (section 3.2, θ = atan2(sx, sy) [I, verify
   once]). Map stick deflection > ~0.3 to the virtual *forward* key. Never send left/right on foot. Keep tank controls
   as an option (current behaviour).
4. DRIVING: default tank steering (stick X → left/right keys, triggers → forward/back), because the game has only
   -1/0/+1 steering (`phys+0xAD`). Optional "point to drive": compare `u16[[car+0x50]+0]` with the stick angle and press
   left/right outside a dead band.
5. Joystick path of the game: leave Control\N on the keyboard type (bit 15 = 0). The game's joystick support is
   digital and only reads the first device.

### (B) Pause menu
1. START (instead of Esc): if state is ON_FOOT/DRIVING/... and `[game]==1`, single player, no prompt → `sub_0045A6E0(game)`
   (authentic: freeze.wav, world stops), open the port menu. If already paused by F6, open the menu too.
2. Draw: wrapper of `sub_004CA440` → after the original, dim (native translucent quad) + panel (sprites 0x86-0x88 or quad) +
   lines with `sub_004CC100` (title font 6 "PAUSED" from GXT `pause`, items font 1, highlight another font/remap).
   Wrap `sub_004C9FA0` to hide the game's own PAUSED panel while the menu is open.
3. Input while open: consume all pad input in the port, inject no DIK keys, filter Esc/F6 from the real keyboard.
4. RESUME → `sub_0045BA60(game)`. QUIT TO MAIN MENU → `sub_0045A4B0(game, 1, 2)` (= prompt "yes"), close the menu, let 2 ticks
   run, then optionally jump from page 11 to page 0. RESTART AREA → same quit, then auto-select item 1 (REPLAY, 0x103)
   on page 11 via the frontend input wrapper. Call all of them between loop iterations.
5. Keyboard Esc can keep the original quit prompt. If Esc should open the port menu instead, the port must not forward
   DIK 0x01 to the game during play (leave `skip_quit_confirm` 0x5EADAB at 0, or Esc would quit at once).

---

## 10. Open questions
1. Confirm the heading convention at run time (θ = 0 → +y/south assumed; section 3.2): log `[[ped+0x168]+0x40]` and the
   change of `ped+0x1AC/+0x1B0` while walking forward.
2. Confirm that paused frames still show peds/cars in the port (sprite-list producer, framerate.md §3/§6).
3. Exact meaning of player+0x68 modes 2/3, player+0x2F/+0x30/+0x64, ped+0x248 and ped+0x27C states 6/7, and the
   remaining ped+0x278 values (3, 7, 8). They are named here from their use only.
4. Which game fonts (0..6) look best as normal/highlight in a menu, and the pixel size of HUD sprites 0x86-0x88.
5. Car heading word `[car+0x50]+0` uses the same Ang16 convention as the ped object (assumed).
6. F1/F2 (`sub_004B2CD0`) = radio station change, and F7/F9 effects, are inferred from code shape only.
7. Behaviour of `sub_0045A4B0` quit during a mission cutscene / `player+0x68 == 3` (should be the same path; untested).
