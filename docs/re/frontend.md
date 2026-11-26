# GTA 2 frontend (menus), settings, saves and cheats

Target: gta2.exe (2004 freeware build, sha1 646e1d47…, image base 0x400000).
Goal: let the port add its own menu pages ("RECOMP OPTIONS", "CHEATS") that look exactly like the
game's own pages, and drive the game's settings and cheat flags from them.

Tags: **[V]** = verified in the disassembly (address given), **[I]** = inferred (reason given).
Fixed point is Q14 (1.0 = 0x4000, `sub_00401AF0` = int -> Q14), see widescreen.md section 0.
Calling conventions: `this` in ecx (thiscall) unless noted. "stdcall N" = callee pops N bytes.
All menu coordinates are in the 640x480 menu space.

---

## 0. Summary

* **There is no in-game OPTIONS page.** The main menu item OPTIONS (action 0x101) calls
  `sub_004D0820`, which minimises the game window and `ShellExecuteA`s **"GTA2 Manager.EXE"**
  (0x575970) [V 0x004D082A, 0x004D08D7]. The external tool edits the registry. When the game window
  is activated again, the WndProc `sub_004D0A00` (WM_ACTIVATE) re-reads CDVol/SFXVol/do_3d_sound and
  the controls [V 0x004D0A39-0x004D0AEA]. So every settings line (volumes, screen mode, controls,
  language) has to be built by the port. Nothing exists in the frontend to extend.
* The frontend is **one C++ object** (global `Frontend* [0x5EB160]`, 0x1EB40 bytes) with **17 fixed page
  slots**. All pages are **built in code** by `sub_00453E20` (no data tables). Pages **12, 13 and 16 are
  never written** (free slots) [V: no access in 0x451F60-0x45A460].
* A page is: item list (max 10 selectable lines), text list (max 15 static lines), cursor-marker list
  (10), cursor, default cursor. The generic draw loop `sub_00457920` and the generic input handler
  `sub_004597C0` work for **any** page id. So a page filled into slot 12 renders and navigates up/down
  natively. Only Enter/Esc/Left/Right on it need a hook (section 3.4).
* Text is UTF-16 (u16 per char). GXT lookup `sub_004C23A0(key)` returns `u16*`, and a missing key returns `L" "`.
  Drawing is `sub_004539F0(u16* s, Q14 x, Q14 y, u16 font, Q14 scale)`. The highlighted line uses the
  font id in `[0x67064C]`, normal lines use `fe+0x11C`. Any ASCII string widened to u16 can be drawn with it.
* Cheats: they are entered as the **player name** (name entry on the PLAY page). The name is hashed
  (weighted byte sum) and compared with 25 constants in `sub_004590F0`. **GOURANGA** unlocks the others. Each
  code toggles one byte flag in the `0x5EAD50-0x5EADB3` debug-flag block. That is the same block the
  registry `Debug` key fills (75 named values). Section 9.

---

## 1. Frontend object layout

### 1.1 Lifetime [V]
* `sub_00457830` (called by WinMain 0x004D14BF): `if (![0x5EB160]) [0x5EB160] = new(0x1EB40) -> sub_00456A60()`.
* Ctor `sub_00456A60`: constructs 17 pages (`eh vector ctor` `sub_004D6925(this+0x122, 0xBCA, 0x11,
  page_ctor 0x455E50, page_dtor 0x455EF0)`) [V 0x00456A98]. Then it creates the GXT object `[0x671550]`
  (`sub_004C2620`, loads `data\%c.gxt` via `sub_004C2540` 0x00456BC2), the frontend style `[0x670684]`
  (`sub_004C0820("data\\fstyle.sty")` 0x00456BD2) and `[0x671978]`, and calls the **page builder
  `sub_00453E20` at 0x00456C70**.
  After that it calls `sub_00455340` (reads `data\*.seq` = area list), `sub_00455C20`, and `sub_00452E20` (loads the TGA backgrounds).
* `sub_00459760` deletes the object (`sub_00456D90` dtor) and sets `[0x5EB160]=0` [V 0x00459794-0x004597A2].
  WinMain calls it when the frontend result is 1 or 4 (0x004D1563, 0x004D1585), and `sub_004620A0` calls it at game start.
  **So the frontend is rebuilt every time the player returns from a game.** A patch of the pages must
  therefore be applied after every `sub_00453E20` call, not once.
* Main loop per frame: `sub_0045A320` (see framerate.md). Logic `sub_0045A250` runs, then the draw `sub_004585F0`.

### 1.2 Frontend fields (offsets from `fe = [0x5EB160]`)

| off | type | meaning | evidence |
|---|---|---|---|
| +0x004 | IDirectInputDevice* | keyboard; `GetDeviceState(0x100, fe+8)` via vtbl+0x24, re-Acquire (+0x1C) on DIERR_INPUTLOST | [V sub_00452000] |
| +0x008 | u8[256] | DIK key state (bit7 = down) | [V sub_00452050] |
| +0x108 | u32 | result returned to WinMain: 2 = stay in menu (ctor), 1 = quit program, 3/4 = leave frontend (4 = 60 s idle on page 0 -> attract demo) | [V 0x00456C3A, 0x004599AB, 0x0045A376, WinMain 0x004D155C-0x004D157F] |
| +0x10D | u8 | keyboard input enabled | [V sub_00453A30, 0x00452056] |
| +0x110 | u32 | mode: 1 = menu, 2 = start-game transition (`sub_00451F60`), 3 = name entry (`sub_00459E30`), 4 = `sub_00458650` (entered by Delete on PLAYER line), 5 = parental-code entry (`sub_0045A010`) | [V dispatch 0x0045A277, table 0x45A308] |
| +0x114/+0x118 | u32 | text caret blink toggle and its 2-call countdown | [V 0x00459C43-0x00459C69] |
| +0x11C | u16 | default menu font id (`[0x67066C]`, or `[0x67065C]` if language 'j') | [V sub_00453D40] |
| +0x11E | u16 | **current page id** | [V 0x004587CF, 0x00459034] |
| +0x122 | Page[17] | page slots, 0xBCA bytes each: `page(n) = fe + 0x122 + n*0xBCA` | [V 0x00456A98, 0x004597E3] |
| +0xC98C | u16[9] | name edit buffer (+0xC99E len) | [V 0x00459560, 0x00459AA5] |
| +0xC9A4 | u16[] | parental-code edit buffer | [V 0x00458517] |
| +0xC9B8..+0xC9BE | u8 | **edge flags of this frame**: left, right, up, down, enter, esc, delete | [V sub_00452050] |
| +0xC9BF..+0xC9C5 | u8 | previous state of LEFT, RIGHT, UP, DOWN, RETURN, ESC, DELETE (for edge detection) | [V sub_00452050] |
| +0xC9C8 / +0xC9CC | u32 / u8 | next frame time / frames due (framerate.md) | [V] |
| +0xC9CD | u8 | **cheats unlocked** (set by GOURANGA, ctor clears) | [V 0x00459137, 0x00456C44] |
| +0xC9D0 | u32 | time of last key on page 0 (60 s -> attract, result 4) | [V 0x0045A358-0x0045A376] |
| +0xEDF4 | u8 | **background index** (section 4.4) | [V sub_00455F90, sub_00453020] |
| +0xEDF9 | u8 | high-score area shown on page 5 | [V 0x00459BEF] |
| +0xEDFC.. | 0x24-byte records | credits lines (page 9) | [V sub_00453590] |

Edge keys [V sub_00452050]: LEFT = DIK 0xCB (`fe+0xD3`), RIGHT = 0xCD, UP = 0xC8, DOWN = 0xD0,
RETURN = 0x1C, ESC = 0x01, DELETE = 0xD3. The menu reads raw DirectInput keys, not the `Control\N` bindings.

### 1.3 Page (0xBCA bytes) [V page ctor sub_00455E50, builder, users]

| off | type | meaning |
|---|---|---|
| +0x000 | u16 | item count (selectable lines, max 10) |
| +0x002 | u16 | text count (static lines, max 15) |
| +0x004 | Item[10] | 0x82 bytes each |
| +0x518 | Text[15] | 0x6E bytes each |
| +0xB8A | Slot[10] | 6 bytes each: `u16 x, u16 y, u8 selectable (ctor 1), u8 pad` |
| +0xBC6 | u16 | **cursor** (selected item index) |
| +0xBC8 | u16 | default cursor. `sub_004587B0` copies it to the cursor on page entry [V 0x00459052-0x0045905D] |

Cursor movement `sub_00452A50` (up) / `sub_00452AA0` (down) wraps around and **skips items whose
`Slot.selectable == 0`** (`[page + 0xB8E + i*6]`) [V 0x00452A83-0x00452A8F]. The draw loop skips items
whose `Item.visible == 0` [V 0x00457D2E-0x00457D38]. The game always sets both together (e.g.
0x004588EB/0x004588F2). On page entry, if the default item is not visible it moves on, and if no item is
visible it raises fatal error 0xA5 [V 0x0045909C-0x004590C8] ("The menu contains no valid options",
0x571684 [I: the only matching message]).

### 1.4 Item (0x82 bytes) [V ctor sub_00452B30, draw sub_00457920, input sub_004597C0]

| off | type | meaning |
|---|---|---|
| +0x00 | u8 | type: **1 = button**, **2 = value selector** (Left/Right change `value`) |
| +0x01 | u8 | visible / enabled (ctor 1) |
| +0x02 | u16 | x (left edge of the text) |
| +0x04 | u16 | y (top) |
| +0x06 | u16[50] | text (the builder copies the GXT string with `wcsncpy(.., 50)` `sub_004D6CEC`) |
| +0x6A | u16 | font id, 0xFFFF = page default `fe+0x11C` |
| +0x6C | u16 | palette remap, 0xFFFF = none (draw mode 2). Otherwise draw mode 8 with this remap [V 0x00458005-0x00458041] |
| +0x6E | u16 | value (type 2), +0x70 = copy |
| +0x72 | u8[12] | per-value "allowed" flags (type 2; Left/Right skip values with 0) [V sub_00452BD0/sub_00452C30] |
| +0x7E | u16 | max value (type 2) |
| +0x80 | u16 | **action**: `< 0x101` -> go to page N (`sub_004587B0(N)`); `0x101..0x10C` -> built-in action (section 3.2) |

For type 2 the drawn text comes from `sub_00452200(page, idx, &str)` (page-specific: player name on page
1, area name on page 5) [V call 0x00457D54; I contents]. For type 1 it is `+0x06` directly [V 0x00457D5B].
**For port pages use type 1 only and write the current value into the text** (e.g. "BLOOD: ON").

### 1.5 Text (0x6E bytes) [V ctor sub_00452C80, draw 0x00458155-0x00458433]
`+0 u8 type (1 = string, 3 = sprite icon)`, `+1 u8 visible`, `+2 u16 x`, `+4 u16 y`,
`+6 u16[50] string` (type 3: `+6` = icon index 0..4 -> sprite 2/0x25/0x26/0x28/0x29 via
`sub_004CBA50(6, ..)`), `+0x6A font (0xFFFF = default)`, `+0x6C remap`.
Text lines are not selectable. Use them for a page title and help lines.

---

## 2. Page catalogue (built by `sub_00453E20`) [V]

| id | content (GXT keys) | notes |
|---|---|---|
| 0 | **main**: `play` y=250 act 1, `options` y=270 act 0x101, `quit` y=290 act 0x102. x=300, slots x=280 y=item+8. 3 items | no-CD: all 3 replaced by `no_cd1..3`, act 9 [V 0x00453F04-0x00453F9D] |
| 1 | PLAY: `charctr` (type 2, player slot 0..7), `savepos` 0x104, `hi_scre` -> page 5, `strtlev` 0x108, `bonslev` 0x109. x=300, y=210..350. 10 texts (stats) | Delete on line 0 -> mode 4 |
| 2 | player dead (`plr_ded`): savepos/replay/mainmen | |
| 3 | area complete: nxt_lvl 0x105 / savepos / replay / contnue 0x10A / mainmen | |
| 4 | game complete, 5 high scores, 6 bonus result, 7 multiplayer result, 8 and 15 "mainmen" only, 10 "nice try", 11 player quit, 14 parental code (French) | |
| 9 | credits scroller (draw `sub_00453590`, logic `sub_00453480`) | Esc on page 0 goes here, then the program quits |
| **12, 13, 16** | **unused** (count 0) | free for the port |

Centred lines use `x = sub_004521B0(fe, text, font, 320)` (thiscall, stdcall 0xC: `cx - width/2`,
font 0xFFFF = `fe+0x11C`) [V 0x004521B0-0x004521F0]. The main menu (page 0) and page 1 use fixed x = 300.

---

## 3. Input and actions

### 3.1 Per-frame logic [V]
`sub_0045A250`: `sub_00452050` (edge flags) -> `sub_00455F90` (pick background) -> `[0x5EAF50]=0`
-> by mode: 1 -> page 9 ? `sub_00453480` : **`sub_004597C0`**. Then the audio update `sub_00410520`.
`[0x5EAF50]` = frontend sound to play this frame. The game writes 1 (cursor up), 2 (down), 3/4 (value changed),
5 (select), 6 (back), 8 (delete), 9 (cheat accepted) [V writes in sub_004597C0, sub_004590F0; I names].

### 3.2 `sub_004597C0` (mode 1, thiscall fe) [V]
1. **Enter** (`+0xC9BC`): if cursor item type == 1: `a = item.action`. `a-0x101 <= 0xB` -> jump table
   0x459DE0, else `sub_004587B0(a)` (go to page a):

   | action | handler | effect |
   |---|---|---|
   | 0x101 options | 0x004599A1 | `sub_004D0820()` -> GTA2 Manager |
   | 0x102 quit | 0x004599AB | `fe+0x108 = 1` |
   | 0x103 replay | 0x00459966 | restart area (`sub_0045E470(0x5EC070, L"")`), mode 2 |
   | 0x104 savepos | 0x0045997A | load `.svg` if `sub_00452940` finds it (`sub_00455850`), else as 0x103 |
   | 0x105 nxt_lvl, 0x107 | 0x004598CA, 0x0045984E | next area / bonus start, mode 2 |
   | 0x108 strtlev | 0x004599B7 | `sub_004556A0(area, 0)`, mode 2 |
   | 0x109 bonslev | 0x004599D5 | start the selected bonus stage |
   | 0x10A contnue | 0x00459A24 | `sub_00459590` |
   | 0x10C | 0x00459A35 | nothing (parental-code page) |
   | **0x106, 0x10B** | 0x00459A2D | fall to `sub_004587B0(0x106/0x10B)` -> **invalid page, do not use** |

   On PLAY page line 0 (type 2), Enter starts name entry (mode 3) [V 0x00459A73-0x00459AD1].
2. **Esc** (`+0xC9BD`): `idx = byte table 0x459E20[page]` (pages 0..14) = `{0,1,1,1,1,2,1,3,3,3,1,1,3,3,0}`,
   jump table 0x459E10: 0 -> page 9 (credits then quit), 1 -> page 0, 2 -> page 1, 3 -> `fe+0x108=1` (**quit**).
   **Page 12/13 map to 3 and page > 14 also quits** [V 0x00459A5E, 0x00459AF7]. So a port page must
   intercept Esc.
3. Up/Down (`+0xC9BA/+0xC9BB`): `sub_00452A50` / `sub_00452AA0`, sound 1/2. Works for any page.
4. Left/Right (`+0xC9B8/+0xC9B9`): only for a type-2 item. Page 1 line 0 writes **Player\plyrslot** with `sub_004B5D10`
   [V 0x00459D41]. Page 5 sets `+0xEDF9`. Others: page-1-only special cases.
5. Delete (`+0xC9BE`) on page 1 line 0 -> mode 4.

### 3.3 Entering a page: `sub_004587B0(this, u16 page)` (stdcall 4) [V]
It sets `fe+0x11E = page`, runs page-specific refreshes (pages 1,2,3,6,7,8,9,11,14,15), copies the default cursor
to the cursor, validates it (section 1.3), and calls `sub_00455F90`. For pages 12/13/16 only the generic part runs.

### 3.4 Smallest safe hook points
* `sub_004597C0` has one caller (0x0045A2B1, mode 1, page != 9) [V]. Wrap it. If `fe+0x11E` is a port page,
  consume the Enter/Esc/Left/Right edge flags yourself (and clear `+0xC9BC/+0xC9BD/+0xC9B8/+0xC9B9`), then call
  the original so that Up/Down, sounds and the caret keep the game's behaviour.

---

## 4. Text and drawing

### 4.1 GXT [V]
* Object `[0x671550]`: +0 = TKEY array (12-byte entries: u32 offset, char key[8]), +4 = count, +0x10 = language char.
* `sub_004C23A0(this, const char* key)` -> `sub_004C2120`: `bsearch(key, ..., 12, cmp 0x4C20E0)`.
  It returns `u16*` into TDAT. **A missing key returns `L" "` (0x575540), with no error.**
* Strings are 16-bit. The menu strings are plain ASCII-range code units, all upper case. `#` is a colour/format
  escape (skipped by the width function).
  `sub_004C2250` upper-cases in place via the language tables 0x594CF8/0x594DF8 (used on names).
* Available menu keys (e.gxt): `play options quit back sure yes no loading mainmen replay contnue savepos
  charctr clrchar hi_scre strtlev bonslev entrnam ...`. `back` = "BACK" exists [V e.gxt dump].
  e.gxt has no volume/screen/controls keys except the PSX leftovers `psxmus` "MUSIC VOLUME",
  `psxeff` "EFFECTS VOLUME", `psxtext` "TEXT SPEED:", `psxfast/psxnorm/psxslow` [V e.gxt]. Using
  them keeps the 5 other languages translated for free [I].

### 4.2 Draw functions [V]
* `sub_004539F0(const u16* s, Q14 x, Q14 y, u16 font, Q14 scale)` stdcall 0x14 = `sub_004CC100(s, x, y,
  font, scale, &mode{=2}, 0, 0, 0)` (stdcall 0x24). Q14 values are passed by value (`x << 14`), scale 1.0 = 0x4000.
* Mode 8 + remap: `sub_004CC100(s, x, y, font, scale, &mode{=8}, remap, 0, 0)` [V 0x00458005-0x00458041].
* Width: `sub_004CB0C0(const u16* s, u16 font)` stdcall 8 -> pixels (space = font space width, `\n` new line,
  `#` ignored) [V 0x004CB0C0-0x004CB13A].
* **Selected line** = same position, font **`[0x67064C]`** (normal lines: item font or `fe+0x11C`) [V 0x00457D8F].
  So the highlight is a different font sprite set (the fstyle.sty font table set by `sub_004BFCxx`:
  `[0x67066C]=4` normal, `[0x67064C]=5` highlight in the western table) [V 0x004BFCF7, 0x004BFD00].
* Must run between `gbh_BeginScene [0x595314]` and `gbh_EndScene [0x595318]`, i.e. inside `sub_004585F0`:
  `GetSurface wrapper sub_004CAEC0` -> BeginScene -> background `sub_00453020` -> content `sub_00458590`
  (mode 1: page 9 ? `sub_00453590` : **`sub_00457920`**) -> EndScene -> `Vid_FlipBuffers` [V 0x004585F0-0x00458642].
* **Arbitrary ASCII**: yes. Widen each byte to u16 (upper case, since the menu fonts have only capitals in all
  observed strings [I]), terminate with 0, and call `sub_004539F0` from a wrapper of `sub_00457920`.

### 4.3 Line layout to copy
Main menu: x=300, y=250/270/290 (step 20), marker slot x=280 y+8. Page 1: x=300, y=210,230,250,270,350.
Result pages: centred at 320, y=0x188.. step 0x14. Title lines: Text type 1 at x=0x23, y=0xB (`plr_ded` style)
[V builder].

### 4.4 Backgrounds [V]
`sub_00455F90` sets `fe+0xEDF4` from page/cursor: page 0 cursor 0/1/2 -> 1/0/2, page 1 -> 0xA/0xB/6/7+area/3+bonus,
page 5 -> 0xC, 2 -> 0xD, 3/6/7/11/14 -> 0x10, 4/10 -> 0xF, 9 -> 0x11, **other pages (12, 13, 16) -> 7**.
`sub_00452E80` maps the index to (left 278 px, right 362 px) entries of the TGA table 0x591F68 (0x88 bytes each:
name[0x80], image handle +0x84): 0 = 1_Options+1, 1 = 1_Play+1, 2 = 1_Quit+1, 3..5 = 2_Bonus1..3+2, 6 = 2_League+2,
7..9 = 2_Level1..3+2, 10 = 2_Name+2, 11 = 2_Restart+2, 0xC.. = full-screen (3_Tables, PlayerDead, MPLose,
GameComplete, LevelComplete, Credits). Blit with `gbh_BlitImage [0x59533C]` in `sub_00453020`.
**Index 0 (1_Options.tga + 1.tga) is the natural "options" look**. Set it for port pages after `sub_00455F90`.

---

## 5. Settings that exist (all in the registry; the frontend writes only `plyrslot`)

Registry helpers on `[0x66C6B4]` (registry.cpp): open key Sound `sub_004B4F90`, Debug `sub_004B51F0`,
Option `sub_004B5260`, Control `sub_004B52D0`, Screen `sub_004B5340`, Network `sub_004B58E0`, Player `sub_004B5BF0` [V string refs].

| key\value | read by | written by | meaning |
|---|---|---|---|
| Sound\CDVol, SFXVol (0..127) | `sub_004B5070` at 0x004618A9/0x004618C5, WndProc 0x004D0A4F | `sub_004B5110` at 0x004A716A (in-game music volume +-10 key path in `sub_004A7010` [I: gating not checked]) | applied with `sub_00410600` (music) / `sub_004105C0` (sfx) on audio object 0x5D85A0 [V 0x004D0A5A, 0x004D0A76] |
| Sound\do_3d_sound | `sub_004B5000` 0x00451F2D, 0x004D0A9C | `sub_004B5180` | -> `[0x5EAD50]` |
| Option\language (string) | `sub_004B5490` 0x004C266B | none | section 7 |
| Option\text_speed | `sub_004B5500` via `sub_004C6DC0` | none | HUD text speed [I] |
| Screen\gamma, full_width/height, window_width/height, max/min_frame_rate, exploding_on, lighting, start_mode, ... | `sub_004B5660/5700/5770` (sub_004CB1D0, sub_004CB290, sub_004CB3F0, sub_004CB990, sub_004CB9C0, ...) | none | framerate.md, widescreen.md |
| Player\plyrslot | `sub_004B5C60` 0x00458B63 | `sub_004B5D10` 0x00459BC3/0x00459D46 | selected player profile 0..7 |
| Control\0..11 | section 6 | default creation only | |
| Debug\* | `sub_00451930` | none | section 11 |

No frontend line exists for resolution, gamma, volumes, controls or language [V: OPTIONS -> external tool; no other
`RegSetValueExA` paths from frontend code]. The port stores the registry in saves/registry.txt. A port page can
write it through the same helpers (thiscall on 0x66C6B4) or natively, and must apply the value itself
(e.g. volume: `sub_00410600(0x5D85A0, v)`, `sub_004105C0(0x5D85A0, v)`) [V call shape 0x004D0A54-0x004D0A76].

---

## 6. Controls (Control\N)

* `sub_0044BF30` loops i = 0..11: `v = sub_004B55A0(reg, i, default[i])`. That opens the Control key, builds the value name with
  `sprintf("%d")` (0x56E59C) and, **if the value is missing, writes the default** (`RegSetValueExA` 0x004B5609).
  Then `[0x5E8F5C + 4i] = v & 0xFF` (DIK scan code) and `[0x5E8F1C + 4i] = v >> 15` (device/type flag) [V 0x0044BF33-0x0044BF62].
* Defaults table **0x591E9C** = `C8 D0 CB CD 1D 1C 39 2C 2D 0F 38 36` (Up, Down, Left, Right, LCtrl, Return, Space,
  Z, X, Tab, LAlt, RShift) [V].
  Meaning [I from the defaults / GTA2 Manager]: 0 forward, 1 backward, 2 left, 3 right, 4 attack,
  5 enter/exit vehicle, 6 jump/handbrake, 7 previous weapon, 8 next weapon, 9 special 1 (Tab), 10 special 2 (Alt), 11 special 3 (RShift).
* Re-read after GTA2 Manager: WndProc WM_ACTIVATE -> `sub_0044C040` [V call 0x004D0AEA; I that it reaches sub_0044BF70 -> sub_0044BF30].
* No redefine page exists in gta2.exe. A port page can write `Control\N` and then call `sub_0044BF30()` to apply [I: same path as the WndProc].
  The `v >> 15` flag encoding (joystick buttons) is still open.

---

## 7. Language

* No frontend selection [V: `language` only read at 0x004C265C].
* `sub_004C2620` (GXT ctor): reads string Option\language into 0x5E9808, takes the first char. It must be one of `e f g i s j`,
  otherwise it falls back to 'e'. Stored in `gxt+0x10` [V 0x004C2670-0x004C26A7].
* `sub_004C2540`: file `data\%c.gxt` (0x57555C), chunk names `GBL%c` [V].
  'j' selects the kanji fonts (`sub_00452E60`: `gxt+0x10 == 'j'`) and `data\kanji.dat` [V]. The keyboard layouts
  `data\keyboard\{eng,fre,ger,ita,spa,por}_kb.cfg` are chosen near 0x004610D3 [V strings].
* `sub_00451800` probes which of e/f/g/i/j/s.gxt exist. "French only" -> `[0x5EAD73]=1` (enables the French parental-code
  page 14) [V 0x00451800-0x00451925; I meaning].
* The menu strings are copied at build time, so a runtime change needs: set `gxt+0x10`, `sub_004C2540` (reload),
  then rebuild the pages. **Simplest: write the registry value and rebuild the frontend** (`sub_00459760` + `sub_00457830`) [I].

---

## 8. Save files

Paths are relative to the game directory. The port's file layer already sends all writes to `saves/` and reads
`saves/` first (winrecomp runtime/src/wr_paths.c) [V source]. No change is needed.

| file | built by | I/O |
|---|---|---|
| `player\plyslotN.svg` (status save, N = profile) | `sub_004528A0(slot, buf)` (itoa + "player\\plyslot" + ".svg", 0x5733C8/0x5733C0); save `sub_0047EF40`: `sprintf("player\\plyslot%d.svg")` 0x0047EF64 into 0x5E9808 | write `sub_00402CF0` (fopen "wb") + `sub_00402DA0` (fopen "ab") chunks [V 0x0047F00F-0x0047F09A] |
| `player\plyslotN.dat` (profile: name, unlocked areas, scores) | 0x004A8840 / 0x004A88A6 (".dat") | `sub_004A89E0` saves (called after name entry/cheats/unlocks) [V callers; I format] |
| `player\hiscores.hsc` | 0x004A8B6B | [V string] |
| `test\replay.rep`, `test\error.log`, `test\warning.log`, `test\MISS_LOG.TXT` | 0x0045F250.., 0x0051A632, 0x00481B9C | [V] |

CRT `fopen` = `sub_004D68DE`. Modes "wb" 0x56E3E8, "rb" 0x56E3EC, "ab" 0x56E3F0. Game wrappers in File.cpp:
`sub_004029C0, 00402B20, 00402CF0 (wb), 00402DA0 (ab), 00402E50 (wb), 00403160` [V].

---

## 9. Cheat codes

### 9.1 Entry and comparison [V]
* Player name entry: PLAY page, line 0 (PLAYER), Enter -> mode 3 (`sub_00459E30`). When the name is confirmed,
  `sub_00459540` copies the edit buffer (9 u16) to the profile name (`[0x66B404] + 0x2730 + slot*0xA4`), calls the checker
  **`sub_004590F0(name)`**, and then saves the profile `sub_004A89E0`.
* `sub_004590F0`: `s = sub_004C2280(name)` (u16 -> char into 0x671880, chars >= 0x80 -> '#'). If `strlen(s) > 16`, nothing happens.
  `h = sum (signed char)s[i] * W[i]` with **W = 0x591F48 = {0x33D, 0x2F9, 0x17, 0x281, 0x2B, 0x329, 0x2A5, 0xBF}**
  (index 8..15 read into the next table, the bytes of "data\frontend\1.tga". Only names longer than 8 chars are affected).
  It is case-sensitive, but the stored name is upper case [I].
* `h == 0x49362` (**GOURANGA**) -> `fe+0xC9CD = 1`, sound 9. Every other code is ignored while `fe+0xC9CD == 0`.
  The unlock is not saved: it lasts until the frontend object is rebuilt (`sub_00456A60` clears it, after every game) [V 0x00456C44].
* Each code **toggles** its byte (`flag = !flag`) and plays sound 9. Entering it again turns it off.
* Separately, `sub_004A9210` compares the profile name with plain `L"MUCHCASH"` (0x5744D0, `wcscmp`) and, outside
  network play, adds 500000 via `sub_0041DC40(game+0x38, 0x7A120)`. It is not gated by GOURANGA [V]. It has no direct
  caller (virtual/indirect) [I: called at level start].
* Network game: `sub_00461AF0` (from 0x00461E77, only if `[0x673E2C] != 0`) clears every flag listed below [V].
  Replays: `sub_0045EEC0` writes the flags into the replay header and `sub_0045F270` restores them on replay load [V].

### 9.2 Table (names found by matching the hash; "?" = original name not recovered, any string with that hash works, e.g. the listed A-Z collision)

| hash | code | flag | read by | effect | when read |
|---|---|---|---|---|---|
| 0x49362 | GOURANGA | fe+0xC9CD | 0x0045914D | unlock other codes | entry |
| 0x484DF | GOREFEST | 0x5EAD51 (= Debug do_blood) | sub_004411B0 | blood/gore effects [I] | at run time (per event) |
| 0x4878D | BUCKFAST | 0x5EAD93 | sub_004A6DA0 | `[[0x5E5BC0]+7] = 1` | level start [I effect unknown] |
| 0x4DA77 | ? (ZZLZAABQ) | 0x5EAD70 | sub_004A5300, sub_004A5690, sub_004A6DA0 | weapon 3 (ElectroGun) ammo 0xFFFF and not used up [I: `cmp weapon,3`] | per shot + level start |
| 0x41611 | MADEMAN | 0x5EAD8A | sub_004A6DA0 | respect 100 with every gang (`sub_0045DA20(gang, player, 100)`) | level start |
| 0x46BE8 | LASVEGAS | 0x5EAD9F | sub_004404F0 | ped look variant 3 (Elvis peds [I]) | ped creation |
| 0x36F62 | NEKKID | 0x5EADA0 | sub_004404F0 | ped graphic 0x1A (naked peds [I]) | ped creation |
| 0x4657B | ? (CUFUAAAA) | 0x5EAD84 (= do_free_shopping) | sub_00428FA0 (+0x69), sub_00461C60 | free respray/shops [I] | object creation |
| 0x4639F | ? (DUESAAAA) | 0x5EAD6E | none | no effect (dead code) | |
| 0x33A69 | CUTIE1 | 0x5EAD8F | sub_004A6DA0 | lives = 99 (`sub_004A50B0(player+0x684, 99)`) | level start |
| 0x47AF1 | ? (BZDYAAAA) | 0x5EAD9E (= keep_weapons_after_death) | sub_004A7910, sub_004A7BA0 | keep weapons after death/arrest | at run time (death) |
| 0x49771 | GODOFGTA | 0x5EAD74 (= get_all_weapons) | sub_004A5B40 | all weapons 0..10 except 7 with full ammo | player creation |
| 0x478FB | ? (ITLVAAAA) | 0x5EAD71 (= do_invulnerable) | sub_0043ECC0 | player ped health 9999 (`ped+0x208`) | ped creation |
| 0x44D2F | DANISGOD | 0x5EAD97 | sub_004A6DA0 | score/cash = 200000 (`sub_004A50B0(player+0x2D4, 0x30D40)`) | level start |
| 0x478A9 | ? (DVKZAAAA) | 0x5EAD5D | sub_0045A5A0, sub_0045C1F0, 0x4B7EB0-0x4B8C80 | skips draw `sub_004B98B0` and tick `sub_004B9260` of object [0x66F5DC] | every tick |
| 0x45EC2 | FLAMEON | 0x5EAD9B | sub_004A5300, sub_004A5690, sub_004A6DA0 | weapon 8 (flamethrower) unlimited | per shot + level start |
| 0x45118 | ? (BVEMAAAA) | 0x5EAD83 | sub_004A6DA0, sub_004A7BA0 | power-up 4 "Get Outta Jail Free" | level start |
| 0x4672D | IAMDAVEJ | 0x5EADA6 | sub_004A6DA0 | score = 9999999 | level start |
| 0x4A98B | ? (SXDXAAAA) | 0x5EAD7B | sub_004A6DA0 | `sub_0044B2C0(player+0x6BC, 9)` (score multiplier [I]) | level start |
| 0x45B2C | ? (DSERAAAA) | 0x5EAD5C | none | **unlocks all areas** in the profile (`sub_004A8B00`) and saves | one-shot at entry |
| 0x49C76 | ? (OVMZAAAA) | 0x5EAD98 | none | unlock area (1,0) (`sub_004A8A90(1,0)`) | one-shot at entry |
| 0x5073D | TUMYFROG | 0x5EAD6A | none | all areas + `sub_004A8A90(2,2)` | one-shot at entry |
| 0x4D5C4 | SCHURULZ | 0x5EAD65 | sub_004A48E0, sub_004A59A0, sub_004A6DA0 | power-up 7 (double damage), timer `player+0x702` not counted down | level start + every tick |
| 0x4B28C | ? (TZBXAAAA) | 0x5EAD8E | sub_004A48E0, sub_004A5710, sub_004A59A0, sub_004A6DA0 | power-up 11 (invisibility), timer `+0x70A` frozen | level start + every tick |
| 0x45AEF | ? (AWFQAAAA) | 0x5EAD6D | sub_00426AC0 | unknown | run time |

Power-up indices of `sub_004A5780(player, n)` follow GXT c28..c40 [V jump table 0x4A596C]: 0 multiplier +1, 1 life +1,
2 health, 3 armour, 4 jail-free card, 5 cop bribe, 6 invulnerability (1200 ticks), 7 double damage (1800), 8 fast reload (1800),
9 ElectroFingers (2100), 10 respect +20, 11 invisibility (1800), 12 instant gang.
Player pointer: `[[0x5EB4FC] + 0x1C]` (framerate.md 0x0045A5AE). `sub_004A6DA0` (thiscall player) is called once,
from game init `sub_0045B5F0` at 0x0045B6BE [V].

### 9.3 Toggling at run time (question 10)
* **Real flags (write the byte at any time, effect follows)**: 0x5EAD51 blood, 0x5EAD70 / 0x5EAD9B infinite
  ElectroGun / flamethrower ammo (the "ammo 0xFFFF" part happens at level start only), 0x5EAD9E keep weapons,
  0x5EAD61 do_infinite_lives (`sub_004A4D50`), 0x5EAD5D, 0x5EAD5E skip_police (section 11).
* **Flag + power-up**: 0x5EAD65 / 0x5EAD8E only freeze an active timer. To turn them on during play also call
  `sub_004A5780(player, 7 / 11)`. Turning them off leaves the timer running out normally [I from the timer code].
* **Read at creation time (affects only new objects)**: LASVEGAS/NEKKID (new peds), free shopping (new shops),
  do_invulnerable (next player spawn), get_all_weapons (next player creation).
  For immediate effect during play, a port action must apply it once: e.g. set `ped+0x208` / give weapons [I].
* **One-shot actions, not flags** (a menu entry should be a button "APPLY", not ON/OFF): DANISGOD, IAMDAVEJ, MADEMAN,
  CUTIE1, BUCKFAST, 0x5EAD83, 0x5EAD7B, MUCHCASH (all at level start, via `sub_004A6DA0` or `sub_004A9210`). Calling
  `sub_004A6DA0(player)` again applies all set ones at once [I: it only sets clamped values].
  Area unlocks (0x5EAD5C/98/6A) change the saved profile permanently (`sub_004A8B00` / `sub_004A8A90` + `sub_004A89E0`).
* Do not change flags while a replay is recorded or played (flags are part of the replay header), and expect network
  games to clear them [V 9.1].

---

## 10. (question 11) Debug flags without codes

`sub_00451930` reads 75 bool values from `HKLM\...\GTA2\Debug` with `sub_004B53B0` and stores them [V 0x00451930-0x00451F5F]:

| addr | name | addr | name | addr | name |
|---|---|---|---|---|---|
| 5EADAA | play_replay | 5EADA3 | constant_replay_save | 5EAD69 | skip_dummies |
| 5EAD80 | do_test | 5EADAE | skip_mission | 5EADA7 | show_cycle |
| 5EAD7D | do_brian_test | 5EAD8D | do_iain_test | 5EAD90 | skip_tiles |
| 5EAD95 | do_show_counters | 5EAD82 | do_show_camera | 5EAD58 | do_show_input |
| 5EAD79 | do_show_timing | 5EADB1 | do_show_collision_box | 5EAD85 | do_show_physics |
| 5EAD6F | do_show_imaginary | 5EAD64 | skip_user | 5EAD53 | skip_traffic_lights |
| 5EAD5B | skip_recycling | 5EADB0 | log_collisions | 5EAD96 | do_show_traffic_lights_info |
| 5EADA1 | do_show_ids | 5EADAC | limit_recycling | 5EAD75 | no_annoying_chars |
| 5EAD78 | skip_slopes | 5EAD67/59/8C/5F/9C | skip_left/right/top/bottom/lid | 5EAD99 | log_routefinder |
| 5EAD81 | do_mike | 5EAD56 | skip_particles | 5EAD5A | show_hidden_faces |
| 5EAD74 | get_all_weapons | 5EADAD | do_exit_after_replay | 5EAD94 | dont_get_car_back |
| 5EAD55 | do_show_instruments | 5EAD6B | skip_ambulance | 5EAD5E | skip_police |
| 5EAD7C | skip_frontend | 5EAD71 | do_invulnerable | 5EADB2 | show_all_arrows |
| 5EAD7F | do_show_horn | 5EAD9E | keep_weapons_after_death | 5EADAF | skip_skidmarks |
| 5EAD89 | do_show_junc_ids | 5EAD68 | do_corner_window | 5EAD61 | do_infinite_lives |
| 5EAD7E | do_load_savegame | 5EAD72 | skip_audio | 5EAD87 | do_debug_keys |
| 5EAD63 | log_random | 5EAD7A | log_random_extra | 5EAD91 | log_input |
| 5EAD77 | log_directinput | 5EAD86 | ignore_replay_header | 5EAD57 | skip_trains |
| 5EAD54 | skip_buses | 5EADAB | skip_quit_confirm | 5EADA8 | do_sync_check |
| 5EAD8B | skip_fire_engines | 5EADA5 | show_brief_number | 5EAD6C | skip_window_check |
| 5EADA4 | skip_replay_sync_check | 5EAD52 | do_show_object_ids | 5EADB3 | do_kill_phones_on_answer |
| 5EAD66 | do_miss_logging | 5EAD9A | do_text_id_test | 5EAD60/9D/A9 | do_police_1/2/3 |
| 5EAD88 | skip_draw | 5EAD84 | do_free_shopping | 5EAD92 | do_release_replay |
| 5EAD51 | do_blood | 5EAD50 | do_3d_sound (Sound key) | 5EAD76 | show_player_names (Network key) |

Useful for a cheats page [I unless stated]:
* `do_infinite_lives` 0x5EAD61 (lives change `sub_004A4D50`, run time).
* `skip_police` 0x5EAD5E. The multiplayer "Cops ON/OFF" setting writes the same byte (`sub_0045A540`, `sub_0045D3D0`) [V],
  so the game expects it to change per game. It is read in the world tick, so it is safest set before level start.
* `keep_weapons_after_death`, `get_all_weapons`, `do_invulnerable`, `do_free_shopping`, `do_blood`: see 9.2.
* `no_annoying_chars`, `skip_skidmarks`, `skip_particles`, `show_all_arrows`: cosmetic. `skip_quit_confirm`: no ESC prompt.
* Unsafe / not for players: `skip_mission`, `skip_user`, `skip_tiles`/`skip_*` faces, `skip_draw`, `skip_audio`,
  `skip_frontend`, `do_load_savegame`, every `log_*`/`do_test`/`*_test`/replay flag, `do_debug_keys`
  (enables `sub_004A7010` debug keys incl. fast-forward), and the `do_show_*` overlays (they need debug text).

---

## 11. Recommended hook plan

1. **Pages: wrap `sub_00453E20`** (thiscall fe, one caller 0x00456C70, runs on every frontend construction).
   After the original:
   * Main menu: **re-target OPTIONS** (`page0.item1.action` at `fe+0x228`: 0x101 -> 12). It keeps the 3 lines and the
     1_Options picture, and replaces the useless GTA2-Manager launch (there is no such tool on Linux/Android).
     Optionally rename the text (`fe+0x1AE`, u16[50]) to "OPTIONS" or "RECOMP OPTIONS" (x stays 300).
     *Alternative: 4th line*: item3 at `fe+0x22A + 0x82` = copy of item2 with y=310 (0x136), action 12, slot3 =
     {280, 318, selectable 1} at `fe+0x122+0xB8A+3*6`, count `fe+0x122` = 4. It also needs hook 2 for its picture
     (cursor 3 leaves EDF4 unchanged [V 0x00455FDF]).
   * Fill page 12 ("RECOMP OPTIONS") and page 13 ("CHEATS") like page 1: `count`, items type 1, `visible=1`, x = centred
     via `sub_004521B0(fe, text, 0xFFFF, 320)` or fixed 300, y = 0xD2 + 0x14*i, slot i = {0x118, y+8, 1}, font 0xFFFF,
     remap 0xFFFF. Title = Text 0 (type 1, x 0x23, y 0xB), last item "BACK" (`back` GXT key). Max 10 items per page.
     More lines need a second page (16) or scrolling done by the hook.
   * Items that open a sub-page can use action = page id (12/13/16): native navigation, no hook needed.
2. **Background: wrap `sub_00455F90`**. After it, if `fe+0x11E` is 12/13/16 set `fe+0xEDF4 = 0`.
3. **Input: wrap `sub_004597C0`** (section 3.4). For port pages handle Enter/Left/Right (toggle the value, rewrite the
   item text, set `[0x5EAF50]` to 3 or 5, store the setting) and Esc (`sub_004587B0(fe, parent)`, `[0x5EAF50]=6`). Clear the
   consumed edge flags, then call the original.
4. **Extra drawing** (value columns, help text): wrap `sub_00457920` and call `sub_004539F0` after it (inside
   Begin/EndScene). Highlight = font `[0x67064C]`, normal = `fe+0x11C`.
5. **Settings**: keep port settings in the port's settings.ini. For game settings write the registry (saves/registry.txt) and
   apply: volumes `sub_00410600/sub_004105C0` on 0x5D85A0, controls `sub_0044BF30`, language = rebuild frontend.
6. **Cheats page (13)**: ON/OFF lines write the bytes of 9.3 "real flags" directly. Lines for creation/level-start
   cheats are stored as flags and take effect at the next level start; show "(NEXT LEVEL)". One-shot actions are
   buttons (unlock all areas = `sub_004A8B00([0x66B404])` + profile save happens inside). GOURANGA is not needed,
   because the port writes the flags itself. The flags are globals, not saved; the port must re-apply its saved choices
   after `sub_00451930` (Debug read at start) and after each frontend construction, and never in network games.
7. During play the game has no menu (Esc only shows the HUD quit prompt `quit1..3`, strings used at 0x004C8731-0x004C8816). An in-game cheat
   toggle needs a port overlay or a key. The flag writes themselves are safe at any tick for the "real flags".

## 12. Open questions

* Exact meaning of BUCKFAST (`[0x5E5BC0]+7`), 0x5EAD5D (object 0x66F5DC; framerate.md links that tick to
  `exploding_on`, which contradicts this, so recheck), 0x5EAD6D (`sub_00426AC0`), 0x5EAD7B (multiplier 9?).
* Original names of the 11 codes found only by hash (collisions listed work in-game if the name entry accepts them; not tested).
* Exact power-up/weapon field layout of the player (+0x6F4.. timers, +0x718 weapon array [I]).
* `Control\N` bit 15 encoding (`v >> 15` -> 0x5E8F1C).
* Whether the in-game music volume key path in `sub_004A7010` is gated by `do_debug_keys`.
* Item type 2 text callback `sub_00452200` per page (not needed if port pages use type 1).
* Not tested on a running build: every hook above is derived from the disassembly only.
