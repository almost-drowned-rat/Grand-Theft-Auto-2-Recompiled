# GTA 2 in-car radio (gta2.exe, 2004 freeware build)

Scope: how the game builds, cycles, opens, mutes and stops the radio stations, and where a native
extra station ("MIXTAPE") can hook in.

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).
Addresses are gta2.exe VAs. `mgr` = sound manager object **0x5DCBC8** (all radio functions are
`__thiscall` on it; the facades at 0x410560..0x4105A5 do `mov ecx,0x5DCBC8; jmp`). `snd` = the MSS
wrapper object **0x66C950** (stream slot 0 = radio, see audio.md §4). Fixed point = 18.14.

---

## 0. Summary

* The radio is **geographic**: up to **5 transmitter slots** (`mgr+0x5468 + k*0x1C`, k = 0..4), each
  with a station number `n` (= file `data\gtaudio\n.wav`), a map position and two ranges. Slot 0 is always
  station 1 (city wide, added at radio start). The other slots come from the level's mission script
  (`RADIO_STATION`-type command, opcode 0x11F). **The cycle order is the slot order.**
* F1/F2 only post a request (`mgr[0x5504]` = 2 prev / 1 next). The per-tick radio update
  `sub_004B2D50` turns it into a slot change with `sub_004B2420`. **There is no OFF position.**
* Changing the station: save the old stream's ms position into its slot, close slot 0, open `n.wav`
  (stereo, "full") or `nA.wav` (mono, far from the transmitter), volume 0, seek to the "virtual play head"
  (saved ms + elapsed ticks), then the per-tick volume/static function `sub_004B1E40` sets the volume
  (`CDVol * signal / 127`) and plays radio static for 15..29 ticks.
* Out of a radio car the slot-0 stream plays **12.wav** (state `0x66`, quiet ambience). Police-type cars
  play **101A.wav** (state `0x65`, no cycling). No HUD text exists for stations; the DJ jingles are inside
  the WAVs. Slot 1 (vocals) is never used by the radio.
* Best hooks for MIXTAPE: wrap **`sub_004B2420`** (cycle: enter/leave MIXTAPE at the wrap point),
  **`sub_004B1E40`** (mute the game stream + no static while MIXTAPE), **`sub_004B2D50`** (after it: decide
  audible / not audible), and optionally **`sub_004B2830`** (car remembers MIXTAPE). See section 7.

---

## 1. Data

### 1.1 Manager fields (radio part) [V unless marked]

| field | type | meaning | written by |
|---|---|---|---|
| mgr+0x01 | u8 | paused = `[game]==2` (`sub_00416BC0`) | `sub_00418050` 0x418064 |
| mgr+0x08 | u8 | ticks per second = 30 | ctor 0x417F4F |
| mgr+0x24 / +0x25 | u8 | SFXVol / **CDVol** (0..127) | registry (audio.md) |
| mgr+0x1454..0x1460 | u32[] | pseudo-random words (used for start positions, static timers) [I: refreshed elsewhere] | |
| mgr+0x5468 + k*0x1C | slot[5] | transmitter slots, see 1.2 | `sub_004B2BC0` add, `sub_004B2350` remove |
| mgr+0x54F4 | u8 | in radio car **last tick** | 0x4B2DC3 (copy of 0x54F5) |
| mgr+0x54F5 | u8 | in radio car **this tick** | 0x4B2E16 (=1) / 0x4B2EB5 (=0) |
| mgr+0x54F6 | u8 | signal = audible level 0..127 (0 = muted, triggers auto seek) | 0x4B2420, 0x4B2AD0, 0x4B2830, 0x4B1E40 |
| mgr+0x54F7 | u8 | **current station** = slot index 0..4, or 0x65 police scanner, 0x66 off / on foot, 0x7F not found | 0x4B2420, 0x4B2830, 0x4B2F0C |
| mgr+0x54F8 | u8 | current station of last tick (change detection) | 0x4B2DBA |
| mgr+0x54FC | u32 | **full**: 1 = stereo `n.wav`, 0 = mono `nA.wav` | 0x4B2420, 0x4B2AD0, 0x4B2830 |
| mgr+0x5500 | u32 | full of last tick | 0x4B2DC9 |
| mgr+0x5504 | u8 | change request: 0 none, **1 next, 2 previous** | `sub_004B2CD0` (key), 0x4B2E7E (auto seek), cleared by 0x4B2420 |
| mgr+0x5506 | u8 | re-tune countdown in ticks (station muted, static plays) | 0x4B24FC (15..29), 0x4B2B8D (16..25), 0x4B2E2C/0x4B2E89 (0) |
| mgr+0x5508 | u32 | id of the radio's sound entity (type 3, makes the service call `sub_004B2D50`) | `sub_004B2F20` / `sub_004B1D80` |
| mgr+0x551C | u32 | fade-in counter for 12.wav (60 ticks) | `sub_004B1BD0` |

Globals [V]: **0x66BEEC** u8 radio running (set `sub_004B2F20`, cleared `sub_004B1D80`); **0x66BEED** u8 key
debounce, 10 ticks (set 0x4B2CE1, decremented 0x4B2D5C); **0x66C274** last radio vehicle (0x4B2EA5);
0x66C270/0x66C272/0x593E64/0x593E68 static-noise state (`sub_004B1E40`).

Vehicle fields [V]: `veh+0x84` model (`sub_00411940`); **`veh+0xB0` remembered station number `n`**
(written on leaving 0x4B2EE2, read on entering 0x4B2865, 0 at car init 0x424953); `veh+0x74` u16 ==
0x7D01 → no radio (`sub_0040F890`; 0x7D01 is written at 0x427031 [I: wrecked/burnt-out]);
`veh+0x68` fixed, normally 1.0 (init from 0x5E4D4C at 0x424938) – see 3.3.

Player vehicle [V `sub_004A65E0`, `sub_004A5150`, `sub_00433200`]: `player = [[0x5EB4FC]+0x38]`;
`ped = [player+0x2C4]` (`[player+0x2C8]` when `[player+0x68]` is 2 or 3); `veh = [ped+0x16C]`, else
`[ped+0x154]` when `[ped+0x27C]` is 6 or 7, else NULL.

### 1.2 Transmitter slot (0x1C bytes at mgr+0x5468 + k*0x1C) [V]

| off | type | meaning |
|---|---|---|
| +0x00 | u8 | used (1) |
| +0x04 / +0x08 | fixed | position x / y (blocks) |
| +0x0C | u32 | station number `n` (1..11) = file number = "type" |
| +0x10 | u16 | range for the mono `A` variant (mode 0) |
| +0x12 | u16 | range for the stereo full file (mode 1) |
| +0x14 | u32 | ticks since this station was last heard (+1 per tick for all 5, `sub_004B1D40`; reset to 0 when it is left) |
| +0x18 | u32 | saved ms position (when it was left) |

Ranges by station number (`sub_004B22B0`) [V]: n=1 → (A 0, full 600) and position forced to (0,0);
n=2..4 → (A 0, full 600); n=5..11 → (A 600, full 300). That matches the shipped files: `1a..4a` do not
exist, `5a..11a` do [V audio.md data].

Signal `sub_004B2180(k, mode)` [V]: Manhattan distance (blocks) player→transmitter;
`range = mode ? +0x12 : +0x10`; `d >= range` → 0; `d < 7/8 range` → 127; else linear 127→0.
The map is 256×256 blocks, so n=1..4 are always 127 (full) everywhere; n=5..11 are full within
~262 blocks and mono further away [I from the numbers].

New slot (`sub_004B2BC0(n, x, y)`, facade 0x410580): first free slot; +0x14 = +0x18 =
`rand[(k+1)%5] * rand[k%5]` (a random start point in the song) [V 0x4B2C97-0x4B2CB4].

### 1.3 Stations per level (from the shipped .SCR files, opcode 0x11F records) [V data]

The game adds slot 0 = station 1 at radio start (`sub_004B2F20` 0x4B2FA5); the script adds the rest in this
order (slots 1..4):

| level | cycle order (n) | |
|---|---|---|
| Downtown `wil.SCR` (+ downtown-*p, MP2-*) | **1, 5, 7, 6, 2** | 2 at (125.5, 80.5); 5/7/6 at map corners |
| Residential `ste.SCR` (+ res-*p, MP5-*) | **1, 5, 8, 9, 3** | |
| Industrial `bil.scr` (+ Industrial-*p, MP1-*) | **1, 10, 11, 5, 4** | |
| bonus `lorne2e/m/h` | 1, 4 / 2 / 3 | |
| bonus `mike1e/m/h` | 1, 6 | |
| bonus `mike2*` | 1 | |

Station 5 is in all three districts, 2/3/4 are the district stations, 6..11 two gang stations per
district [I: 5 = Zaibatsu, who are in every district]. The gang car → station table of `sub_004B2830`
(model → n): 10→11, 29→7, 37→6, 41→8, 48→9, 70→5, 86→10 [V jump table 0x4B2A50/0x4B2A70].
Names of the stations are not in the exe (spoken in the WAVs).

---

## 2. Key press → station change

```
sub_004A7010 (system keys, gameplay_input_pause.md 1.5)
  F1 (DIK 0x3B) → 0x4A70F2: sub_004B2CD0(mgr, 0)       [V jmp via facade 0x4105A0]
  F2 (DIK 0x3C) → 0x4A7107: sub_004B2CD0(mgr, 1)

sub_004B2CD0(dir)                                        [V]
  if (byte[0x66BEED] > 0) return;  byte[0x66BEED] = 10;  // debounce 10 ticks
  veh = player vehicle; if (!veh) return;
  if (sub_004B2510(veh)) return;   // police-type model 3,12,17,22,30,34,52,54,84: no cycling
  if (sub_004B25A0(veh)) return;   // model 6, 0x3B..0x3D (train/bus type): no radio
  mgr[0x5504] = dir ? 1 : 2;       // 1 = next, 2 = previous

sub_004B2420()  (cycle, called from sub_004B2D50)        [V]
  c = cur;
  for (try = 0; try < 5; try++) {
      c = (mgr[0x5504] == 1) ? (c + 1) % 5 : (c ? c - 1 : 4);
      for (j = 0; j < 2; j++) {            // j=0: full (range +0x12), j=1: A (range +0x10)
          s = sub_004B2180(c, 1 - j);
          if (s > 0x41) { cur = c; signal = s; full = 1 - j; mgr[0x5504] = 0;
                          mgr[0x5506] = rand[0x1454] % 15 + 15; return; }
      }
  }
  mgr[0x5504] = 0;                          // nothing receivable: station unchanged
```

Empty slots (`+0` == 0 or n == 0) give signal 0, so they are skipped. With 5 stations in each district
every press moves exactly one slot; it wraps 4→0 / 0→4. **No OFF state** in the cycle.

---

## 3. Per-tick update and the stream

### 3.1 `sub_004B2D50` (every game tick, also while paused) [V]

Called from the sound service (`sub_00418050` → … → `sub_004163A0` for entity type 3, 0x4163E0) which
`Game::Tick sub_0045C3B0` runs in every state, including the paused state 2 (0x45C3DA, 0x45C405).

```
if (byte[0x66BEED]) byte[0x66BEED]--;
if (!game || !player) return;
paused ? sub_004B6A60(snd) : sub_0041C600(mgr);   // not radio (voice sample / ped sounds)
if (!snd.streams_enabled (+0x55)) return;
mgr[0x54F8] = cur; mgr[0x54F4] = mgr[0x54F5]; mgr[0x5500] = full;
sub_004B1D40();                                   // +1 tick on every slot's +0x14
veh = player vehicle;
if (veh && !sub_004B25A0(veh) && veh+0x74 != 0x7D01) {          // in a radio car
    mgr[0x54F5] = 1;
    if (!mgr[0x54F4])      { sub_004B2830(); sub_004B2AD0(); mgr[0x5506] = 0; }   // just got in
    else if (mgr[0x5504]) { if (cur < 5) slot[cur].ticks = 0; sub_004B2420(); }  // key request
    else                   sub_004B2AD0();                                         // signal update
    if (signal == 0)       { mgr[0x5504] = 1; sub_004B2420(); mgr[0x5506] = 0; }  // auto seek next
    sub_004B25D0(veh+0x68 != 1.0);               // stream open/switch + sub_004B1E40
    [0x66C274] = veh;
} else {                                          // on foot / bus / train / wreck
    mgr[0x54F5] = 0;
    if (mgr[0x54F4] == 1) {                      // just got out
        if ([0x66C274]) [[0x66C274]+0xB0] = slot[cur].n;   // the car remembers its station
        if (prev < 5) slot[prev].ticks = 0;
    }
    cur = 0x66;  sub_004B1BD0();                 // 12.wav ambience (3.4)
}
```

### 3.2 Station on entering – `sub_004B2830` [V]

1. Police-type model (`sub_004B2510`) → `full = 0`, `cur = 0x65` (plays `101A.wav`, fixed signal 0x73 in
   `sub_004B2AD0`).
2. `veh+0xB0 != 0` → `cur = sub_004B1E00(veh+0xB0)` (slot with that n, 0x7F if none → signal 0 → auto seek).
3. else `full = 1`; gang car (table 1.3) → its station if its signal ≥ 0x32 (signal stored).
4. else random (`rand[0x1454] % 5`): 0 → first slot with n ∈ {6,9,10}; 1 → n ∈ {7,8,11}; 2 → n = 5;
   3 → first slot with A-range 0 and n ≠ 1 (the district station 2/3/4); 4 → station 1. Not found → station 1.

`sub_004B2AD0` (no key) [V]: keeps `signal` up to date and switches full ↔ A with hysteresis (to full when
full-signal > 0x6E, to A when it drops < 0x5A); each switch sets `mgr[0x5506] = rand % 10 + 16`.

### 3.3 Stream open / switch – `sub_004B25D0(rate2x)` [V]

```
full = (mgr[0x54FC] == 1);
if (mgr[0x54F4] != mgr[0x54F5])                 // first tick in the car
    goto open;
changed = (prev != cur);
if (mgr[0x5500] == full_dw) {                   // same variant
    if (!changed) goto tail;
    if (prev < 5) slot[prev].saved_ms = sub_004B6930(snd, 0);   // AIL_stream_ms_position
    goto open;
}
if (changed) { (save as above); goto open; }
// only full <-> A changed: same station, keep position
pos = sub_004B6930(0); close; open n (full ? "n.wav" : "nA.wav");
if (rate2x) rate *= 2; if (pos >= total) pos = 0; seek(pos); goto tail;

open:
  sub_004B6860(snd, 0);                                          // AIL_close_stream slot 0
  n = (cur == 0x65) ? 0x65 : slot[cur].n;
  sub_004B70C0(snd, 0, n, full);                                 // data\gtaudio\n[.A].WAV, loop forever
  sub_004B6890(snd, 0, 0);                                       // volume 0
  if (rate2x) sub_004B68C0(snd, 0, 2 * sub_004B68E0(snd, 0));    // playback rate x2
  total = sub_004B6960(snd, 0); if (!total) { close; return; }
  pos = slot[cur].ticks * 1000 / mgr[8] + slot[cur].saved_ms;
  if (pos > total) pos %= total;
  sub_004B6910(snd, 0, pos);                                     // AIL_set_stream_ms_position
tail:
  sub_004B1E40();
```

**Correction to audio.md §4.2:** the "×2 rate" test at 0x4B2627 / 0x4B27CF reads the **function argument**
(`[esp+0x18]`), not the `full` local (`[esp+0x10]`). The argument is `veh+0x68 != 1.0` (0x4B2E90..0x4B2EA0,
`sub_00403820` = dword compare, 0x66BF7C = fixed 1.0 from `sub_00401AF0(1)` at 0x54AFD0). Normal cars have
`veh+0x68 = 1.0` (0x424938), so the original game does **not** double the rate for normal stereo play [V
code; meaning of `veh+0x68` I: a per-car fixed scale].

### 3.4 Volume, static, tunnel – `sub_004B1E40` (every tick in a car) [V]

```
if (!mgr[0x54F4]) mgr[0x5506] = 0;
if (mgr[0x5506]) { mgr[0x5506]--; signal = 0; }          // re-tuning: station silent
else if (under a roof/bridge (sub_00463850 on [0x662C08], veh x,y,z)) signal -= signal / 4;
vol = CDVol * signal / 127;
static = (signal < 0x73 && !paused) ? min(127 - signal, 100) (slewed +5 / -10 per tick) : 0;
if (paused) { static = 0; vol /= 2; }                    // pause: radio goes on at half volume
if (static) play SFX 0x89 (+ 0x8A when A variant) via sub_004171A0 (2D sound, params at mgr+0x30..0x94);
sub_004B6890(snd, 0, vol);                               // AIL_set_stream_volume slot 0
```

### 3.5 Off / on foot – `sub_004B1BD0` (cur == 0x66) [V]

First tick (`prev != 0x66`): save `prev` ms (if < 5), close slot 0, open `12.WAV` (full), volume 0, seek to
`rand[0x145C] % total`, `mgr[0x551C] = 60`. Later ticks: fade in over 60 ticks to `SFXVol*30/127`
(paused: no countdown, capped at 30/60) or `SFXVol*60/127` once done [V 0x4B1C75-0x4B1D3F; I: ambience].

---

## 4. Start, stop, enter, leave, pause

| event | what happens | kept |
|---|---|---|
| level start | `sub_0045D...` 0x45D331 → facade 0x410560 → **`sub_004B2F20`**: 0x66BEEC=1, slots cleared, cur = prev = 1, adds station 1 at (0,0), registers sound entity type 3 (mgr+0x5508) | – |
| script `RADIO_STATION` (opcode 0x11F) | `sub_004753B0` → 0x410580 → `sub_004B2BC0(n, x, y)` | |
| script removes a station | 0x47F82E → 0x410590 → `sub_004B2350(x, y)` (slot at that position cleared) | |
| level end | 0x45D3EC → 0x410570 → **`sub_004B1D80`**: closes slots 0 and 1, clears all 5 slots, unregisters the entity | nothing |
| sound shutdown / frontend music entity (type 2) | `sub_004164D0` 0x4164DD, `sub_00417DB0` 0x417E41 → `sub_004B1D80` | |
| enter a radio car | 3.1 "just got in": `sub_004B2830` picks station, `sub_004B25D0` opens it at its virtual play head | per-slot ms + ticks |
| leave the car / car wrecked (`veh+0x74 == 0x7D01`) / bus, train | cur = 0x66, `veh+0xB0 = n`, stream switches to 12.wav | car's station in `veh+0xB0`; slot position |
| pause (`[game]==2`) | radio logic keeps running each tick; stream volume halved, no static; slot tick counters keep counting | everything |
| mission cut scene / control lock | **no check** in the radio code – it only looks at "player in a radio car" | |
| Wasted / Busted | ped leaves the car → like leaving | |

---

## 5. HUD

No station name or number is drawn by the game [V: no code outside 0x4B1BD0-0x4B2FE0 and the ctor 0x417F2A
reads mgr+0x5468..0x551C; the radio functions call only stream/SFX wrappers]. The station identity is the
DJ jingle inside each WAV. A "MIXTAPE" label has to be drawn natively, e.g. after the HUD `sub_004CA440`
with the game's text function as `gta2_menu.c put_text` does (`gta2_pause_draw`).

---

## 6. Facts that matter for an extra station

* Cycle state is only `cur` (0..4) + the request byte; the game never needs a 6th slot.
* The game stream can keep playing **muted** while MIXTAPE is on: switching back then works with the
  normal virtual play head (the muted station even moves on in real time).
* "In a radio car" = `mgr[0x54F5] == 1` after `sub_004B2D50`; `cur < 5` excludes police scanner (0x65) and
  off (0x66). Paused = `mgr[0x01]` (or `[[0x5EB4FC]] == 2`). Radio running = `byte[0x66BEEC]`.
* Auto seek (`signal == 0`) also goes through `sub_004B2420` with request 1 – it must not be mistaken for
  a key press.
* Police cars and buses/trains never reach the cycle (`sub_004B2CD0` gates) [V].

---

## 7. Recommended hooks (MIXTAPE)

State (native): `mix_on`, `key_dir` (0/1/2), optional `mix_car`.

| wrap | before / after `_gen` | purpose |
|---|---|---|
| **`sub_004B2CD0`** (key, `ecx`=mgr, arg dir) | after: if `mgr[0x5504]` became non-zero, `key_dir = mgr[0x5504]` | tell key presses from auto seek |
| **`sub_004B2420`** (cycle) | see below | enter / leave MIXTAPE at the wrap point |
| **`sub_004B1E40`** (volume/static) | before: if `mix_on`: `mgr[0x5506]=0`, `mgr[0x54F6]=0x7F` (no static); after: `sub_004B6890(ecx=0x66C950, 0, 0)` (game stream muted) | silence the game station |
| **`sub_004B2D50`** (tick) | after: `listen = mix_on && mgr[0x54F5]==1 && mgr[0x54F7] < 5 && byte[0x66BEEC] && !mgr[0x01]` → `gta2_mixtape_listen(listen)`; show the HUD label when `mix_on` turned on or the car was entered with it | when MIXTAPE is audible |
| `sub_004B2830` (optional) | before: `veh = sub_004A65E0([[0x5EB4FC]+0x38])`; `mix_on = (veh == mix_car)` (or a tag in `veh+0xB0`, then set it to 0 before `_gen`, because `sub_004B1E00` would return 0x7F) | a car remembers MIXTAPE |
| `sub_004B1D80` (stop) | after: `mix_on = 0`, `listen(0)` | level end |

`sub_004B2420` wrapper:

```c
void sub_004B2420(RECOMP_PARAMS) {
    uint32_t m = ecx;                       /* 0x5DCBC8 */
    uint8_t dir = MEM8(m + 0x5504), old = MEM8(m + 0x54F7);
    int key = key_dir != 0; key_dir = 0;
    if (mix_on) {
        if (!key) { sub_004B2420_gen(...); return; }   /* auto seek under MIXTAPE: stay */
        mix_on = 0;
        MEM8(m + 0x54F7) = (dir == 1) ? 4 : 0;          /* next → first station from slot 0, prev → from slot 4 */
        uint8_t keep = MEM8(m + 0x5506); MEM8(m + 0x5506) = 0;
        sub_004B2420_gen(...);
        if (!MEM8(m + 0x5506)) { MEM8(m + 0x54F7) = old; MEM8(m + 0x5506) = keep; }  /* nothing found */
        return;
    }
    if (!key || old >= 5) { sub_004B2420_gen(...); return; }
    /* save what _gen may change, run it, detect the wrap */
    uint8_t s6 = MEM8(m + 0x54F6), s06 = MEM8(m + 0x5506); uint32_t sfc = MEM32(m + 0x54FC);
    MEM8(m + 0x5506) = 0;
    sub_004B2420_gen(...);
    uint8_t now = MEM8(m + 0x54F7);
    int found = MEM8(m + 0x5506) != 0;                  /* _gen sets 15..29 on success */
    int wrapped = !found || (dir == 1 ? now <= old : now >= old);
    if (wrapped) {                                      /* past the last (first) station → MIXTAPE */
        MEM8(m + 0x54F7) = old; MEM8(m + 0x54F6) = s6; MEM32(m + 0x54FC) = sfc; MEM8(m + 0x5506) = 0;
        mix_on = 1;                                     /* (s06 not needed: MIXTAPE has no re-tune) */
    }
}
```

Result: cycle `1 → 5 → 7 → 6 → 2 → MIXTAPE → 1 …` (Downtown); previous from station 1 goes to MIXTAPE, previous
from MIXTAPE goes to the last station. While MIXTAPE is on, `cur` stays on the last game station (muted, still
"playing"), so leaving the car writes that station into `veh+0xB0` – the optional `sub_004B2830` hook (or a
native `mix_car = [0x66C274]` taken in the `sub_004B2D50` wrapper when `mgr[0x54F4]==1 && mgr[0x54F5]==0`)
restores MIXTAPE for the same car. Car pointers are pooled and reused [I], so a remembered car can be a new car
later; harmless.

Silence rules for the native player: out of the car, bus/train/wreck, police scanner → `mgr[0x54F5]==0` or
`cur >= 5`; level end → `sub_004B1D80`; pause → `mgr[0x01]` (the game itself only halves the radio there, so
`listen(0)` or half volume are both faithful choices). There is no cut-scene state to follow.

---

## 8. Open points

1. `veh+0x68` meaning (when is it ≠ 1.0, i.e. when does the game really double the rate).
2. The refresh of the random words mgr+0x1454..0x1460 (not traced).
3. Station names per number (only in the audio).
