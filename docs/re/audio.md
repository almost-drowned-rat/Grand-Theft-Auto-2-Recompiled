# GTA 2 audio – Miles Sound System (mss32.dll) usage specification

Source: `extract/game/App_Executables/gta2.exe` (disassembly `analysis/disasm/asm/text.asm`), the shipped
`mss32.dll` (**MSS 5.0r**, version resource "5.0r", © 1991-99, base 0x21000000) and the 3D provider
`MSSDS3DH.M3D` ("Microsoft DirectSound3D hardware support", base 0x29000000). Data: `run/game/data/Audio`,
`run/game/data/GTAudio`, `run/game/data/Audio/Vocals`.

Tags: **[V]** = VERIFIED in code (address given). **[I]** = INFERRED (reason given).
gta2.exe addresses are `0x004xxxxx`; mss32 `0x210xxxxx`; MSSDS3DH `0x290xxxxx`.

> **Correction to the brief:** the DLL is Miles **5.0r**, not 6.x. The API subset used is the same (stdcall `_AIL_xxx@N`),
> but there is **no** `AIL_set_3D_orientation`/listener call, **no** `AIL_set_sample_ms_position`, and the 3D distance
> call is the 5-argument MSS 5 form. All 52 mss32 imports are used. Several are reached only through a register
> (`mov reg,[iat]; call reg`: `AIL_set_preference`, `AIL_enumerate_3D_providers`, `AIL_release_3D_sample_handle`,
> second `AIL_allocate_sample_handle`/`AIL_release_sample_handle` sites) or by tail-jump (`jmp [iat]`) – a grep for
> `call dword ptr [iat]` misses them. [V]

---------------------------------------------------------------------------------------------------------------------------

## 0. Architecture (who calls MSS)

Three layers:

| layer | object | role |
|---|---|---|
| game facade | free thunks `0x410500…0x410690`, `this = 0x5d85a0`, forward to sound manager `0x5dcbc8` | called from game code / WndProc / options |
| sound manager ("SFX/radio manager") | global object at **0x5dcbc8** (methods `0x4107xx…0x418xxx`, `0x4b1bd0…0x4b2f20`) | voice allocation, distance/pan/volume maths, radio logic. Pure game logic – stays in recompiled code |
| **MSS wrapper** | global object at **0x66c950** (methods `0x4b5ed0…0x4b73e3`) | the only code that calls mss32. **Reimplement these entry points' semantics** (or the AIL calls they make) |

Every AIL call in the exe is inside `0x4b5f70…0x4b73e3` except Bink (`0x481f20`). [V] (import xrefs)

### MSS wrapper object layout (0x66c950) [V] (ctor `sub_004B6CE0`)

| offset | type | meaning |
|---|---|---|
| +0x0000 | HDIGDRIVER | from `AIL_waveOutOpen` (also read directly by Bink setup at 0x458ff1/0x45900a) |
| +0x0004 | u8 | 0x63 ('c'), read by `0x416630` (movie-name selection) – not audio |
| +0x0005 | char[14] | `"data\\gtaudio\\"` prefix for streams |
| +0x0055 | u8 | streams enabled (1 after ctor) |
| +0x0058 | HSAMPLE[16] | 2D sample handles (count in +0x1eb0) |
| +0x0098 | HSAMPLE | extra 2D "voice" sample (8-bit mono) |
| +0x009c | HSTREAM[2] | stream slot 0 (music/radio), slot 1 (vocals) |
| +0x00a4 | u8 | sound bank loaded |
| +0x00a8 | SDT entry[320] (24 bytes each) | bank directory, read straight from the .SDT |
| +0x1ea8 / +0x1eac | void* | bank buffer from `AIL_mem_alloc_lock` (both same pointer) |
| +0x1eb0 | u8 | number of 2D samples (always 16) |
| +0x1eb1 | u8 | bytes/sample of output (2 = 16-bit) |
| +0x1eb2 | u8 | number of allocated 3D samples |
| +0x1eb4 | HPROVIDER[256] | enumerated 3D providers |
| +0x22b4 | char*[256] | their names (malloc'd copies, 0x50 bytes) |
| +0x26b4 | int | open provider index (-1 = none) |
| +0x26b8 | int | EAX available flag |
| +0x26bc | int | current EAX environment |
| +0x26c0 | HPROVIDER | open 3D provider |
| +0x26c4 | H3DSAMPLE[16] | 3D sample handles |
| +0x2704/08/0c | float | EAX effect volume / decay / damping cache (init -1.0) |
| +0x2710 | int | provider count |
| +0x2714 | u8 | value passed as pref 33 (always 0, see §1) |

---------------------------------------------------------------------------------------------------------------------------

## 1. Init / shutdown

### 1.1 Startup – `sub_004B7250` (called via facade 0x410500 → `0x416490`, from 0x461898 game start-up) [V]

```
AIL_startup()                                            0x4b7253
open_digital(16bit=1, pref33=[+0x2714]=0, rate=22050)    0x4b7269 -> sub_004B6DE0
  fail -> AIL_shutdown() (0x4b7272), return 0  => sound manager byte [0x5dcbc8] = 0 = SOUND DISABLED
enumerate 3D providers                                   0x4b7280 -> sub_004B6600
[+0x1eb0] = 16; alloc 2D samples                          0x4b7290 -> sub_004B6F70(1)
AIL_set_digital_master_volume(dig, 127)                   0x4b729e   (only call; never changed later)
```

If init fails, the manager flag at `0x5dcbc8+0` stays 0 and every facade/manager entry returns early → the game runs
silently (no retry). [V] 0x4164b0, 0x416496, 0x4164d6.

### 1.2 `sub_004B6DE0(bool16, u8 pref33, int rate)` – preferences + waveOutOpen [V]

Preferences (`AIL_set_preference(number, value)`, called through `edi` = [0x56e2c0]):

| addr | number | value | meaning (mss32 evidence) |
|---|---|---|---|
| 0x4b6e52 | 1 | **37** | DIG_MIXER_CHANNELS (default 64 @0x2100127b) – max simultaneous HSAMPLEs incl. stream/Bink samples |
| 0x4b6e58 | 15 | **0** | DIG_USE_WAVEOUT = NO → DirectSound (slot 15 gates every API between waveOut/DS paths, e.g. 0x210032bb; MSSDS3DH string "M3D provider requires AIL_USE_WAVEOUT==NO") |
| 0x4b6e6a | 33 | **0** (`[+0x2714]`, always 0) | default 1 (0x210012bd). Together with slot 15 selects the implementation: 0 → path 0x21011750 (LoadLibrary dsound, per-HSAMPLE DirectSound secondary buffers, volume/pan via `IDirectSoundBuffer::SetVolume/SetPan`, see §2.6); ≠0 → MSS software mixer. [I] name unknown ("use MSS mixer") |
| 0x4b6e70 | 31 | 1 | equal to default (0x210012a3); DS-only use at 0x2100eb29. [I] no behavioural change |

`AIL_waveOutOpen(&this->dig, NULL, WAVE_MAPPER(-1), &pcm)` at 0x4b6e7c with PCMWAVEFORMAT on the stack:

| field | value |
|---|---|
| wFormatTag | 1 (PCM) |
| nChannels | **2** (constant) |
| nSamplesPerSec | **22050** (all callers pass 0x5622) |
| wBitsPerSample | 16 (`bool16`=1 in every call; 8 if 0) |
| nBlockAlign / nAvgBytesPerSec | 4 / 88200 |

Return value ≠ 0 → function returns 0 (no fallback format, no retry). On success: store pref33 in +0x2714, allocate the
extra voice sample (`sub_004B5F70`: `AIL_allocate_sample_handle`, `AIL_init_sample`, `AIL_set_sample_type(s, 0, 0)` =
**8-bit mono unsigned**), then `AIL_mem_alloc_lock(6 100 000)` (0x4b6e98) for the bank buffer. If that fails: release
the voice sample, `AIL_waveOutClose`, return 0. [V]

### 1.3 2D sample allocation – `sub_004B6F70(1)` [V]

1. release all 2D samples (`sub_004B5FD0`, `AIL_release_sample_handle` ×16),
2. release all 3D samples + close provider (`sub_004B6EE0`, `AIL_release_3D_sample_handle`, `AIL_close_3D_provider`,
   then **`Sleep(1500)`** at 0x4b65f3),
3. (dead code: if +0x2714≠0, `AIL_waveOutClose` + reopen with pref33=0),
4. for i<16: `AIL_allocate_sample_handle(dig)`, `AIL_init_sample`, **`AIL_set_sample_type(s, 1, 1)`** =
   DIG_F_MONO_16 + DIG_PCM_SIGN (0x4b6fd7). Allocation result is not checked.

`AIL_init_sample` is only called here – **never before an individual play**; every play re-sets address, rate,
volume, pan, loop count, loop block. [V]

### 1.4 3D provider selection – `sub_004B6600` + `sub_004B72B0` [V]

* Enumeration (startup, 0x4b6600): `AIL_enumerate_3D_providers(&next, &prov[i], &name)` until it returns 0 or 256
  entries; names copied.
* Enable 3D (`sub_004B72B0(u8* count_out)`, called from manager `sub_00416640(on)` = facade 0x410670, which is driven by
  registry value **`do_3d_sound`** at start-up 0x4618ea and on WM_SETFOCUS 0x4d0aa7; WM_KILLFOCUS forces it off 0x4d0b45):
  1. release 2D samples and any 3D samples/provider (same as §1.3 steps 1-2, incl. the 1.5 s Sleep),
  2. search the list for `strncmp(name, "Microsoft DirectSound3D hardware support", 30) == 0` (0x4b7323). **That is the
     only provider accepted** (EAX/A3D/software providers are ignored),
  3. open it (`sub_004B6550`): `AIL_open_3D_provider(prov)`; result ≠ 0 → try next name. On success:
     `AIL_3D_provider_attribute(prov, "EAX environment selection", &v)` with v preloaded 0; if v ≠ -1 → EAX flag=1 and
     `AIL_set_3D_provider_preference(prov, "EAX environment selection", &17)` (17 = EAX_ENVIRONMENT_MOUNTAINS; accepted
     range <26). MSSDS3DH has no EAX attributes, so this is a no-op there,
  4. `AIL_3D_provider_attribute(prov, "Maximum supported samples", count_out)` (0x4b737d). Note: game passes a **byte**
     pointer, MSS writes an S32 (clobbers the next 3 bytes, harmless). Clamp >16 → 16; **< 8 → close provider, fail**,
  5. `AIL_allocate_3D_sample_handle(prov)` ×count; any 0 → close provider, fail (already-allocated handles leak).
* On failure the manager immediately goes back to 2D (`sub_004B6F70(1)`, 16 channels, 3D flag 0) [V] 0x41667a.
* MSSDS3DH "Maximum supported samples" returns a global set at activation (0x29001872) – the DS hardware 3D buffer
  count. [I] On any modern Windows/Wine DS3D has no hardware buffers → 3D always fails → **2D is the normal path**.
* 2D and 3D are **mutually exclusive**: in 3D mode there are no 2D SFX handles (only the voice sample at +0x98). [V]

An unused EAX setter exists at 0x4b6460 (no callers): sets "EAX effect volume" (if ≥0 && ≤1), "EAX decay time"
(0.1…20), "EAX damping" (0…2) via `AIL_set_3D_provider_preference`. [V]

### 1.5 Shutdown – `sub_004B7000` (facade 0x410510 → `0x4164d0`, called 0x4620d2) [V]

1. slot-0 stream: **blocking fade-out**: `v = AIL_stream_volume(s)`; for v..1: `AIL_set_stream_volume(s, v--)`,
   `AIL_delay(1)` (1/60 s per step → up to ≈2.1 s),
2. `AIL_close_stream` both slots,
3. release voice sample, 3D samples + provider (+Sleep 1500), 2D samples,
4. `AIL_mem_free_lock(bank)`, `AIL_waveOutClose(dig)`, tail-jump `AIL_shutdown()`.

Before that the manager stops all 16 channels (`end_sample`/`end_3D_sample`) and its radio (`sub_004B1D80`). [V]

### 1.6 Other

* Ctor writes `HKLM\Software\Aureal\A3D` `SplashScreen=0`, `SplashAudio=0` (REG_DWORD) – disables the A3D splash.
  Irrelevant for a port. [V] `sub_004B5ED0`.

---------------------------------------------------------------------------------------------------------------------------

## 2. Sound bank and 2D samples

### 2.1 Bank files (.SDT + .RAW) – `sub_004B6B40(name)` [V]

Called from manager `0x416d30(style_path)` (facade 0x410550): the name is the style filename without directory and
extension (`sub_00416570`, copies up to the first '.', e.g. `data\bil.sty` → `bil`).

* `data\audio\<name>.RAW`: fopen "rb", fseek end, ftell; **size > 6 100 000 → fail**; read the whole file into the
  bank buffer.
* `data\audio\<name>.SDT`: `fread(this+0xa8, 24, 320, f)` – up to 320 entries. The table is **not cleared first**:
  a short SDT (FSTYLE: 18 entries) leaves entries 18…319 of the previous bank (ctor defaults: off 0, len 0,
  rate 11025, var 0, loopstart 0, loopend -1). [V] 0x4b6cb9, ctor 0x4b6d31.
* Banks shipped: `bil`, `ste`, `wil` (320 entries, ~6.0 MB each) and `fstyle` (frontend, 18 entries). File names are
  mixed case (`bil.raw` vs `STE.RAW`) – a Linux port needs case-insensitive lookup.

SDT entry (24 bytes, little-endian):

| off | type | meaning | accessor |
|---|---|---|---|
| +0x00 | u32 | byte offset into .RAW | used by `set_sample_address` |
| +0x04 | u32 | byte length | `sub_004B60A0` |
| +0x08 | u32 | sample rate Hz (5000…22050; 0 → treated as 1) | `sub_004B6020` |
| +0x0C | u32 | random pitch variation range (Hz; 0/500/600/700/1000/2000) | `sub_004B6040` → `0x4166e0` adds ±(rand % var) |
| +0x10 | s32 | loop start (bytes) – 0 except 9 entries | `sub_004B6060` |
| +0x14 | s32 | loop end (bytes), always -1 (= end) | `sub_004B6080` |

Entries are contiguous (off[i+1] = off[i]+len[i]) [V data].

**Sample format inside .RAW is mixed** [V data + code]:
* normal SFX: **16-bit signed mono** (played through the 16 SFX handles typed `(1,1)`; 3D info says bits=16),
* entries 0x45…0x88 (5500 Hz, odd lengths, mean ≈127.6): **8-bit unsigned mono** – police-scanner voice words, only
  played through the voice sample (+0x98, typed `(0,0)`).

### 2.2 2D SFX play sequence (manager `sub_00417845` path 0x417c22…0x417ca0, 2D mode) [V]

All wrapper functions take the channel index 0…15; they do nothing when the handle is 0.

| order | wrapper | AIL call | value |
|---|---|---|---|
| 1 | 0x4b60c0 | `AIL_set_sample_address(h, bank+off, len)` | only if bank loaded (+0xa4) |
| 2 | 0x4b6150 | `AIL_set_sample_playback_rate(h, rate)` | channel rate (SDT rate ± random var, Doppler-like adjust) in Hz |
| 3 | 0x4b6110 | `AIL_set_sample_volume(h, vol)` | `(SFXVol * min(v,63)*2) >> 7`, v = channel volume 0…63 → 0…126 |
| 4 | 0x4b6130 | `AIL_set_sample_pan(h, pan)` | 10…117, centre **63** (see 2.4) |
| 5 | 0x4b6190 | `AIL_set_sample_loop_count(h, n)` | 0 = loop forever, 1 = once |
| 6 | 0x4b6170 | `AIL_set_sample_loop_block(h, start, end)` | from SDT (+0x10, +0x14) = (0, -1) almost always |
| 7 | 0x4b61e0 | `AIL_start_sample(h)` | |

Per-update (`sub_00417488`, every game tick, ~0x417640…0x4178c9) for playing channels: rate (`set_sample_playback_rate`,
change limited to ±6000 Hz per update), volume (±10 per update), pan (±10 per update).
Stop: `0x4b6200` → `AIL_end_sample`. [V]

SFXVol = registry `SFXVol` (0…127, default 127, stored in manager +0x24 by 0x4105c0); `[mgr+0x1a]` is always 1
(0x41800c) so the `min(v,63)*2` scaling is always used. [V]

### 2.3 Status polling [V]

| wrapper | call | test |
|---|---|---|
| 0x4b61b0 (2D) | `AIL_sample_status` | `== 4` (SMP_PLAYING) → playing |
| 0x4b6380 (3D) | `AIL_3D_sample_status` | `== 4` |
| 0x4b63d0 (3D stop) | `AIL_3D_sample_status == 4` then `AIL_end_3D_sample` | |
| 0x4b6a40 (voice) | `AIL_sample_status != 2` (SMP_DONE) → busy | |

The manager also predicts end-of-sound itself: `0x416890` sets duration = `len * ticks_per_sec / rate` for loop count ≠0,
-3 (= endless) for loop count 0. [V]

### 2.4 Pan values (manager `sub_00416E50`) [V]

`pan = 63` if distance is 0; else `r = min(63, |dx|*64/dist)`, `t = table[0x591df8 + r]`; right side (dx>0):
`min(63 + t, 117)`, left: `max(63 - t, 10)`. The value is passed straight to `AIL_set_sample_pan`. The voice sample
uses pan 64 (0x4b6a0c, 0x4b6af6).

### 2.5 Voice sample (+0x98, 8-bit unsigned mono) [V]

* `sub_004B69B0(entry)` (police scanner word queue `0x41c000`, up to 15 queued, one at a time when status is DONE):
  `set_sample_address(bank+off, len)`, rate = SDT rate (5500), pan 64, loop count 1, `AIL_start_sample`; volume
  `0x4b6990`: `AIL_set_sample_volume(SFXVol*110/128)`.
* `sub_004B6A80(a, b)` (`0x418100`, a ∈ 0x45…0x47 / 0x73…0x77, b ∈ 0x57…0x59 / 0x83…0x87 random): only if idle;
  address = bank+off[a], length = off[b]-off[a] (several words concatenated), **rate fixed 18050 Hz** (0x4682, ≈3.3×
  the data's 5500 Hz → fast garbled chatter), pan 64, loop 1, start; volume SFXVol*50/128.
* `0x4b6a60`/`0x4b6b20` → `AIL_end_sample` (on pause 0x4b2d90 and every 20th tick 0x4181ac).

### 2.6 How MSS 5 turns volume/pan into sound (for an exact SDL mix) [V mss32]

With GTA 2's preferences (DirectSound, per-sample DS buffers):
* volume: `eff = clamp(vol * master / 127, 0, 127)` (master = 127) → `IDirectSoundBuffer::SetVolume(2000*log10(eff/127))`
  (0x21011f00/0x21011f90; eff 0 → -10000 = silence). ⇒ **linear amplitude gain = vol/127**.
* pan (0x21012000): p<64 → right gain `p/63` (left 1.0); p>64 → left gain `(127-p)/63` (right 1.0); p=64 no change
  (both 1.0). So 63 and 64 are both centre; 10 → right ≈0.16; 117 → left ≈0.16. Pan does not reduce the near side.
* rate: per-buffer `SetFrequency(rate)` – plain resampling, no pitch-preserving.
* Output is mixed by DirectSound into the 22050 Hz/16-bit/stereo primary buffer. [I] an SDL port may mix at 44.1/48 kHz;
  only relative behaviour matters.

---------------------------------------------------------------------------------------------------------------------------

## 3. 3D samples (only when "do_3d_sound" and a DS3D hardware provider with ≥8 voices exist)

### 3.1 Start sequence (manager 0x417adc…0x417bef, 3D mode) [V]

| order | wrapper | AIL call | value |
|---|---|---|---|
| 1 | 0x4b6220 | `AIL_set_3D_sample_info(h, &info)` | see struct |
| 2 | 0x4b6320 | `AIL_set_3D_sample_playback_rate(h, rate)` | Hz |
| 3 | 0x4b62b0 | `AIL_set_3D_sample_volume(h, (SFXVol*min(v,63)*2)>>7)` | same formula as 2D (channel field +0x60) |
| 4 | 0x4b6360 | `AIL_set_3D_sample_loop_count(h, n)` | 0 = forever |
| 5 | 0x4b6340 | `AIL_set_3D_sample_loop_block(h, start, end)` | SDT loop |
| 6 | 0x4b62d0 | `AIL_set_3D_position(h, x, y, z)` | see 3.2 |
| 7 | 0x4b62f0 | `AIL_set_3D_sample_float_distances(h, max, min, max, min)` | max = D, min = D/4 |
| 8 | 0x4b63b0 | `AIL_start_3D_sample(h)` | |

`AILSOUNDINFO` built by 0x4b6220 (9 dwords on the stack, last three left uninitialised):

| off | field | value |
|---|---|---|
| +0x00 | format | 1 (WAVE_FORMAT_PCM; MSSDS3DH rejects anything else 0x29002447) |
| +0x04 | data_ptr | bank + SDT off |
| +0x08 | data_len | SDT len |
| +0x0c | rate | channel rate (arg 3) |
| +0x10 | bits | `[+0x1eb1]*8` = 16 |
| +0x14 | channels | 1 (provider requires mono, 0x29002461) |
| +0x18…+0x20 | samples, block_size, initial_ptr | garbage (not set) |

Returns `AIL_set_3D_sample_info != 0`.

Per update (0x4177e5…0x417875): `set_3D_position` + `set_3D_sample_float_distances` again, rate (±6000 Hz/update)
and volume (±10/update) as in 2D. No velocity/Doppler calls exist. Stop: `0x4b63d0`.

### 3.2 Coordinates [V]

* Positions are **listener-relative**: `d = object_pos - listener_pos` per axis in 18.14 fixed point (`sub_00401B40`),
  converted to float by `*2^-14` (`sub_00401B10`, constant 6.1035e-5 @0x56e3c4) → units = **world blocks**.
* Call: **`AIL_set_3D_position(h, dx, 0.0, -dy)`** (0x41783b, 0x417baf): world X → MSS X, world height ignored (Y = 0),
  world Y → −Z.
* Listener = manager +0x1468/+0x146c/+0x1470 (`sub_00417030`: followed player/camera object position). **The game
  never touches the MSS listener**, so it stays at origin with DS3D default orientation (front +Z, top +Y, left-handed):
  world +X (screen right) = right speaker, world −Y (screen up / north) = front.
* "Non-positional" sounds (channel flag +0x18): position (0,0,−0) and distance forced to fixed(100000) (0x417b4e).
* D = channel +0x64, set per sound type by the manager, values 10…130 blocks [V immediates in 0x411xxx-0x415xxx].

### 3.3 Distance semantics (MSSDS3DH) [V]

`M3D_set_3D_sample_distances(S, max_front, min_front, max_back, min_back)` (0x29001db0) →
`IDirectSound3DBuffer::SetMaxDistance(max_front, DEFERRED)`, `SetMinDistance(min_front, DEFERRED)`,
`listener->CommitDeferredSettings()`; back values only stored. Position → `SetPosition(x,y,z, DS3D_IMMEDIATE)`
(0x29001f41). 3D volume → `SetVolume(2000*log10(v/127))` (0x29001b40; no digital-master scaling).
DS3D default rolloff 1.0 ⇒ **gain = 1 for dist ≤ D/4, (D/4)/dist up to D, constant (=0.25) beyond D**. [I] DS3D spec.
On top, the manager fades the 3D channel volume linearly to 0 between 0.75·D and D (`sub_00416960`, 0x417d57). [V]

---------------------------------------------------------------------------------------------------------------------------

## 4. Streams

Two slots: **slot 0** = music/radio/frontend, **slot 1** = vocals. All via `AIL_open_stream(dig, filename, 0)`
(stream_mem = 0 → MSS default buffer). Failure (missing file) → handle 0, silently ignored. [V]

### 4.1 Open – `sub_004B70C0(slot, n, full)` [V]

* slot 0: `"data\\gtaudio\\" + sprintf("%d", n) + (full ? "" : "A") + ".WAV"`, e.g. `data\gtaudio\7.WAV` or `7A.WAV`;
  `AIL_set_stream_loop_count(s, 0)` (endless).
* slot 1: `"data\\audio\\vocals\\" + table[n]` (table @0x574698, 30-byte entries, 98 names `accuracyb.wav`…`laughd.wav`);
  loop count 1.
* Always closes the slot first, then `AIL_start_stream`.
* Frontend variant `sub_004B6700(i)`: only if slot 0 is empty: `data\gtaudio\` + `d.wav`/`m.wav`/`a.wav` (i=0/1/2),
  loop 0, start. (`M.wav` is not shipped → silently nothing.)

| file set | format [V data] | used for |
|---|---|---|
| `GTAudio\1…12.wav` | IMA ADPCM (0x11) **stereo** 22050 Hz, ~16 MB (1.wav 32 MB) | radio stations ("full", n = station table +0x5474), 12 = special state (see below) |
| `GTAudio\5a…11a.wav` | IMA ADPCM **mono** 22050 Hz, same duration | radio "A" variant (`full`=0); `1a…4a`, `12a` not shipped |
| `GTAudio\101a.wav` | IMA ADPCM stereo | station number 0x65 (=101) with full=0 |
| `GTAudio\A.wav`, `D.wav` | PCM 16-bit stereo 22050 | frontend music (`a.wav`, `d.wav`) |
| `Audio\Vocals\*.wav` | IMA ADPCM, 43 mono + 54 stereo, 22050 | announcer ("WASTED", "FREEZE" …) |

Callers: radio `0x4b2614/0x4b27ca` (slot 0, `full = (mgr[0x54fc] == 1)`), `0x4b1c2d` (slot 0, n=12, full=1),
announcer `0x41090b/0x41156f` (slot 1, then `set_stream_volume(1, SFXVol)`), pause `0x418092` (slot 1, n=7
`freeze.wav`, volume 127). [V]

### 4.2 Stream controls [V]

| wrapper | AIL call | notes |
|---|---|---|
| 0x4b6890(slot, v) | `AIL_set_stream_volume(s, v & 0xff)` | 0…127 |
| 0x4b66d0(v) | `AIL_set_stream_volume(slot0, v)` | frontend: `CDVol*6/10` (0x411aaf) |
| 0x4b68c0(slot, r) / 0x4b68e0(slot) | `AIL_set_stream_playback_rate` / `AIL_stream_playback_rate` (22050 if no stream) | radio: when `full`, **rate = 2 × current rate** (0x4b2636, 0x4b27de) – see open questions |
| 0x4b6930(slot) | `AIL_stream_ms_position(s, NULL, &cur)` → current ms | saved per station on switch |
| 0x4b6960(slot) | `AIL_stream_ms_position(s, &total, NULL)` → total ms | |
| 0x4b6910(slot, ms) | `AIL_set_stream_ms_position(s, ms)` | **seeking is used** |
| 0x4b66a0() | `AIL_stream_status(slot0) == 2` (SMP_DONE); true if no stream | frontend track change |
| 0x4b67b0 / 0x4b6860(slot) | `AIL_close_stream` | |
| 0x4b67e0 | blocking fade-out (`AIL_delay(1)` per volume step) then close | called from `0x416c10` |

Radio behaviour (manager `0x4b25d0`, `0x4b1bd0`): per-station "virtual play head": when leaving a station its current ms
is saved; when returning, position = saved + elapsed_ticks·1000/ticks_per_s, wrapped modulo the stream's total length,
applied with `set_stream_ms_position`; volume starts at 0 and is ramped by the manager using **CDVol** (+0x25). State 0x66
plays `12.WAV` looped, seeks to `rand % total`, fades in over 60 ticks to `SFXVol*30/127` (or `*60/127`). [V code, I purpose]

---------------------------------------------------------------------------------------------------------------------------

## 5. Focus, master volume, memory, delay

| item | where | behaviour |
|---|---|---|
| `AIL_digital_handle_release(dig)` | 0x4b6677 via 0x416620 / facade 0x410640 | WndProc WM_SIZE minimized (0x4d0c2f) and WM_KILLFOCUS (0x4d0b4f, after switching 3D off). Return ignored |
| `AIL_digital_handle_reacquire(dig)` | 0x4b6687 via `0x416d61` / facade 0x410650 | WM_SIZE restored/maximized (0x4d0c19) and WM_SETFOCUS (0x4d0a3e). Manager first resets all channel records (0x4169a0/0x4169e0) – sounds are not resumed. Then re-reads `CDVol`/`SFXVol`/`do_3d_sound` from registry |
| `AIL_set_digital_master_volume(dig, 127)` | 0x4b729e | once at start-up; **SFX volume is not applied here** – it is multiplied into every sample volume (§2.2) |
| `AIL_mem_alloc_lock(6100000)` / `AIL_mem_free_lock` | 0x4b6e98 / 0x4b7092 | the single bank buffer (= max .RAW size) |
| `AIL_delay(1)` | 0x4b6825, 0x4b704b | 1 MSS tick = 1/60 s, used only inside blocking stream fade-outs |

Volume settings: registry `SFXVol` → manager +0x24 (facade 0x4105c0, clamp 127), `CDVol` → +0x25 (0x410600); options
menu steps ±10 (0x4a7119…). [V]

---------------------------------------------------------------------------------------------------------------------------

## 6. Bink movies [V]

`sub_00481F20(name, dig)` (callers 0x459003/0x45901d pass `[0x66c950]` = the HDIGDRIVER):
`BinkSetSoundSystem(BinkOpenMiles, dig)` (0x481f2c), `BinkSetIOSize(0x927c0)`, `BinkOpen(name, 0x04000000)`.
Movie audio therefore plays through Bink's own Miles back-end on the **same digital driver** (Bink allocates its own
HSAMPLE; that is one reason for DIG_MIXER_CHANNELS = 37). No volume call is made for movies. A native port must give
the Bink replacement its own mixer voice/stream.

---------------------------------------------------------------------------------------------------------------------------

## 7. Call-site table (all AIL imports)

| IAT | function | call sites (wrapper fn) |
|---|---|---|
| 0x56e2b0 | AIL_startup | 0x4b7253 (7250) |
| 0x56e2b8 | AIL_shutdown | 0x4b7272 (7250), 0x4b70b4 jmp (7000) |
| 0x56e2c0 | AIL_set_preference | 0x4b6e52, 0x4b6e58, 0x4b6e6a, 0x4b6e70 (6DE0, via edi) |
| 0x56e2c4 | AIL_waveOutOpen | 0x4b6e7c (6DE0) |
| 0x56e2cc | AIL_waveOutClose | 0x4b6eb2 (6DE0), 0x4b6f8c (6F70), 0x4b70a7 (7000), 0x4b72cd (72B0) |
| 0x56e2b4 | AIL_set_digital_master_volume | 0x4b729e |
| 0x56e2c8 / 0x56e374 | AIL_mem_alloc_lock / free_lock | 0x4b6e98 / 0x4b7092 |
| 0x56e304 / 0x56e300 | AIL_digital_handle_release / reacquire | 0x4b6677 / 0x4b6687 |
| 0x56e2e4 | AIL_delay | 0x4b6825 (67E0), 0x4b704b (7000) |
| 0x56e36c | AIL_allocate_sample_handle | 0x4b5f80 (voice), 0x4b6fc5 (6F70, via ebp) |
| 0x56e370 | AIL_init_sample | 0x4b5f8d, 0x4b6fca |
| 0x56e378 | AIL_set_sample_type | 0x4b5f9e (0,0), 0x4b6fd7 (1,1) |
| 0x56e368 | AIL_release_sample_handle | 0x4b5fbe, 0x4b5fff (via ebp) |
| 0x56e364 | AIL_set_sample_address | 0x4b60f7, 0x4b69e8, 0x4b6ad5 |
| 0x56e360 | AIL_set_sample_volume | 0x4b6120 jmp, 0x4b69a6 |
| 0x56e35c | AIL_set_sample_pan | 0x4b6140 jmp, 0x4b6a0d, 0x4b6af6 |
| 0x56e358 | AIL_set_sample_playback_rate | 0x4b6160 jmp, 0x4b69fe, 0x4b6ae7 |
| 0x56e354 | AIL_set_sample_loop_block | 0x4b6180 jmp |
| 0x56e350 | AIL_set_sample_loop_count | 0x4b61a0 jmp, 0x4b6a1c, 0x4b6b05 |
| 0x56e348 | AIL_start_sample | 0x4b61f0 jmp, 0x4b6a2e jmp, 0x4b6b12 |
| 0x56e344 | AIL_end_sample | 0x4b6210 jmp, 0x4b6a6b, 0x4b6b2b |
| 0x56e34c | AIL_sample_status | 0x4b61bd, 0x4b6a47 |
| 0x56e308 | AIL_enumerate_3D_providers | 0x4b662c (via ebp) |
| 0x56e310 / 0x56e30c | AIL_open_3D_provider / close | 0x4b656b / 0x4b65de |
| 0x56e314 | AIL_3D_provider_attribute | 0x4b65a2 (EAX env), 0x4b737d (max samples) |
| 0x56e318 | AIL_set_3D_provider_preference | 0x4b644e, 0x4b64bf/0x4b6500/0x4b6541 (unused fn, via edi) |
| 0x56e2ac / 0x56e2bc | AIL_allocate / release_3D_sample_handle | 0x4b73a7 / 0x4b6f05 (via ebx) |
| 0x56e340 | AIL_set_3D_sample_info | 0x4b6290 |
| 0x56e33c / 0x56e330 | AIL_set_3D_sample_volume / playback_rate | 0x4b62c3 jmp / 0x4b6333 jmp |
| 0x56e338 | AIL_set_3D_position | 0x4b62e3 jmp |
| 0x56e334 | AIL_set_3D_sample_float_distances | 0x4b630c |
| 0x56e328 / 0x56e32c | AIL_set_3D_sample_loop_count / loop_block | 0x4b6373 jmp / 0x4b6353 jmp |
| 0x56e320 / 0x56e31c | AIL_start_3D_sample / end_3D_sample | 0x4b63c3 jmp / 0x4b63fc jmp |
| 0x56e324 | AIL_3D_sample_status | 0x4b6390, 0x4b63e4 |
| 0x56e2ec | AIL_open_stream | 0x4b6779 (6700), 0x4b720a (70C0) |
| 0x56e2f0 / 0x56e2f4 | AIL_set_stream_loop_count / start_stream | 0x4b678d, 0x4b7226 / 0x4b679a, 0x4b7234 |
| 0x56e2e8 | AIL_close_stream | 0x4b67c5, 0x4b6839, 0x4b6874, 0x4b7063 |
| 0x56e2f8 | AIL_set_stream_volume | 0x4b66ed, 0x4b6821 (ebp), 0x4b68b1 jmp, 0x4b7043 |
| 0x56e2e0 | AIL_stream_volume | 0x4b67f6, 0x4b7021 |
| 0x56e2fc | AIL_stream_status | 0x4b66b2 |
| 0x56e2d0 | AIL_stream_ms_position | 0x4b6947 (cur), 0x4b6977 (total) |
| 0x56e2d4 | AIL_set_stream_ms_position | 0x4b6923 jmp |
| 0x56e2d8 / 0x56e2dc | AIL_stream_playback_rate / set_stream_playback_rate | 0x4b68f3 jmp / 0x4b68d3 jmp |
| 0x56e264 | BinkSetSoundSystem (binkw32) | 0x481f2c with `BinkOpenMiles` (IAT 0x56e260) |

---------------------------------------------------------------------------------------------------------------------------

## 8. Minimal native-mixer contract (summary)

* One output; 16 SFX voices (16-bit signed mono from the bank), 1 voice for 8-bit unsigned mono, 2 streams (IMA ADPCM
  mono/stereo or PCM WAV, seekable by ms, loopable, volume, rate), plus Bink audio.
* Gains: `vol/127` linear; pan per §2.6 (one-sided attenuation, 63/64 = centre); voice rate = plain resample.
* Loop count 0 = endless, n = n plays; loop block (start, -1) in bytes.
* Status: report SMP_PLAYING (4) while playing, SMP_DONE (2) when finished; stream status DONE after the last loop.
* Optional 3D: listener at origin, positions (dx, 0, −dy) in blocks, inverse-distance with min D/4, max D (clamped,
  not muted); or simply report "Maximum supported samples" < 8 so the game stays in 2D (matches modern Windows).
* `AIL_delay(n)` = sleep n/60 s (blocking fades). `AIL_close_3D_provider` is followed by the game's own Sleep(1500).

---------------------------------------------------------------------------------------------------------------------------

## 9. Open questions

1. **Radio rate doubling**: with `full = (mgr[0x54fc]==1)` the game opens the stereo `N.WAV` and sets the stream rate
   to 2× `AIL_stream_playback_rate`. Either MSS 5.0r reports half the rate for stereo IMA ADPCM streams (then 2× =
   normal speed) or that state is genuinely sped up. Needs a run under real mss32 (Wine) logging the returned rate, or
   analysis of the ADPCM stream setup in mss32 (`0x2101fd60…`, format check at 0x2102025e). Meaning of state 0x54fc
   (and of station states 0x65/0x66) not traced.
2. Exact names of preferences 31 and 33 (MSS 5.0r header not available); behaviour inferred from code paths only.
3. 2D attenuation/pan tables (0x591df8, distance→volume `0x416da0`) are game logic in recompiled code – not needed for
   the mixer, not documented in detail.
4. Purpose of the 18050 Hz concatenated-voice burst (`0x4b6a80`) – inferred as scanner chatter; confirm by ear.
5. Whether MSS 5 `AIL_set_sample_address` while a sample is playing restarts it (the game always calls
   `start_sample` afterwards, so a port can treat start as restart-from-0).
6. DS3D behaviour beyond max distance and the cone/HRTF of the original hardware provider cannot be verified without
   DS3D hardware; the §3.3 model is the DirectSound specification.
