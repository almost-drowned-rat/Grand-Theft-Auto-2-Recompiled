# GTA 2 text drawing, measuring, wrapping and languages (gta2.exe, 2004 freeware build)

Scope: everything needed to draw characters the sprite fonts do not have (Polish, Cyrillic, Japanese,
Simplified Chinese) with our own TTF glyphs, while layout, centring and word wrap stay correct.

Tags: **[V]** = verified in the disassembly (`analysis/disasm/asm/text.asm`, address given), **[I]** = inferred
(reason given). Addresses are gta2.exe VAs. Q14 = 18.14 fixed point (1.0 = 0x4000), as in framerate.md.
Style object = `[0x670684]`, GXT object = `[0x671550]`, kanji object = `[0x661E98]`.

---

## 0. Summary

* **One draw function for all text**: `sub_004CC100` (stdcall, 9 args, `ret 0x24`). Every text on screen
  (menus, HUD, pager, mission brief, credits, the port's own menu via `sub_004539F0`) ends there [V: its 9 call
  sites are in 8 wrappers, section 1.1]. It walks the u16 string and draws **one gbh_DrawQuad per glyph**.
* **Glyph index = `char - 0x21`** + per-font base + font sprite base; **no range check** [V 0x004CC30E].
  Only `0x0A` (new line), `0x20` (space) and `0x23 '#'` (colour toggle) are special. `'*'` is *not* special
  in the drawer (only the credits parser uses it). Speaker codes are stripped before drawing (only the first
  char of a pager message).
* **One per-character width function**: `sub_004BF570` (thiscall style, `(u16 *font, u16 *ch)`) = sprite
  width byte of glyph `ch-0x21`. String width `sub_004CB0C0` and the word wrapper `sub_004C2450` (via
  `sub_004539D0`) use it. The drawer itself reads the same sprite width byte directly.
* **Word wrap** `sub_004C2450(dst, src, max_w, font)` breaks at the last space; if the line has no space it
  breaks **before the character that overflows** - so text without spaces (CJK) already wraps.
* **Language**: `Option\language` first letter, only `e f g i s j` accepted (else `e`) [V 0x004C2670].
  `'j'` switches HUD/menu fonts to two "kanji" fonts that are drawn from `data\kanji.dat` 1-bit bitmaps by the
  CPU into a scratch sprite (section 4.3). Do **not** reuse `'j'` for our CJK: it needs kanji.dat, is fixed
  16/32 px monospace and expects JIS-style codes, not Unicode.
* **Minimal hooks** (section 5): wrap `sub_004CC100` (draw our glyph, advance x) and `sub_004BF570` (report our
  width). Optionally `sub_004C21A0` (upper-casing) and `sub_004C2620` (accept new language letters).

---

## 1. Drawing chain

### 1.1 Call graph [V]

| function | args | what | calls sub_004CC100 with |
|---|---|---|---|
| `sub_004539F0` | `(s, x, y, font, scale)` stdcall 0x14 | frontend / port menu text | `&mode{2}, remap 0, blend 0, alpha 0` [V 0x00453A1D] |
| `sub_004C7280` | `(s, x, y, font, &mode, remap, blend, alpha)` stdcall 0x20 | HUD text (brief box, pager, centred texts, PAUSED ...) | x, y multiplied by HUD scale `[[0x5E3CC4]+0xA8]`, scale = that value [V 0x004C7282-0x004C72D6]. Port wraps it (`hud_anchor`, src/gta2_manual.c) |
| `sub_004BA2C0` | `(s, x, y, font)` stdcall 0x10 | HUD text (ped/car names, zone names) | same HUD scale, `&mode{2}`, 0, 0, 0 [V 0x004BA2C0-0x004BA317]. Port wraps it |
| `sub_00457920` | thiscall fe | menu lines | mode 8 + remap for items [V 0x004580C1, 0x00458421] |
| `sub_00453590`, `sub_004566C0`, `sub_004568C0`, `sub_00456FB0` | frontend | credits page, result pages, ... | direct [V call sites 0x00453799, 0x004567DC, 0x00456A17, 0x004570A7] |

### 1.2 `sub_004CC100(u16 *s, Q14 x, Q14 y, u16 font, Q14 scale, u32 *mode, u32 remap, u32 blend, u8 alpha)` [V 0x004CC100-0x004CC57A]

Stack at entry: `[esp]` ret, `+4 s, +8 x, +0xC y, +0x10 font, +0x14 scale, +0x18 &mode, +0x1C remap, +0x20 blend,
+0x24 alpha`. Pseudocode:

```c
flags = sub_004CB050(blend, alpha) | 0x20000;     // 0x004CC10F: point filter always
//   blend 0: [0x67358C] | 0x80            (index 0 transparent)
//   blend 1: [0x67358C] | 0x2180, diffuse = (alpha<<27)|0xFFFFFF in all 4 verts (0x673400/20/40/60)
//   blend 2: [0x67358C] | 0x2280, same diffuse, additive          [V 0x004CB050-0x004CB0BD]
space_w = scale * sub_004BF5D0(&font);            // Q14       0x004CC13B
line_h  = scale * sub_004BF630(&font);            // Q14       0x004CC167
if (scale == [0x673630] /* = 1.0, static init sub_00567DB0 */) flags |= 0x10000;  // gbh sprite mode
cur_mode = *mode; cur_remap = remap; cx = x;
if (font == KANJI_S [0x67065C] || font == KANJI_L [0x670650])          // kanji palette
    cur_mode == 8 ? sub_00460DA0(kanji, cur_remap) : sub_004CBA40(kanji);
for (p = s; *p; p++) {
    c = *p;
    if (c == 0x0A) { cx = x; y += line_h; continue; }                  // 0x004CC1F9
    if (c == 0x20) { cx += space_w;       continue; }                  // 0x004CC21A
    if (c == 0x23) {                                                   // '#' 0x004CC233
        if (cur_mode == *mode && cur_remap == remap) { cur_mode = 8; cur_remap = 0; }
        else                                          { cur_mode = *mode; cur_remap = remap; }
        if kanji font: update kanji palette as above;
        continue;                                                      // no advance
    }
    if (font == KANJI_S)      { rec = kanji->rec16; tex = kanji->tex16; sub_00460CC0(kanji, c); }
    else if (font == KANJI_L) { rec = kanji->rec32; tex = kanji->tex32; sub_00460D30(kanji, c); }
    else {
        spr = sub_004BF550(style, font, c - 0x21);     // = fontbase[font] + (c-0x21)   NO RANGE CHECK
        spr = sub_004BF2A0(style, 7, spr);             // + sprite base of type 7 (fonts) = [style+0x14]+0xA
        rec = sub_004BF280(style, spr);                // = [style+0x20] + spr*8
        tex = sub_004C2AC0([0x671978], 7, spr, cur_mode, cur_remap);   // texture with the palette
    }
    w = scale * rec->w (u8 @+4);  h = scale * rec->h (u8 @+5);          // Q14
    verts at 0x6733F0 (4 x 0x20): (cx,y) (cx+w,y) (cx+w,y+h) (cx,y+h), z = 1e-4 (0x38D1B717),
        uv (0,0) (rec->w-1e-4,0) (rec->w-1e-4,rec->h-1e-4) (0,rec->h-1e-4)   // 0x004CC39C-0x004CC53F
    gbh_DrawQuad([0x5952C4])(flags, tex, 0x6733F0, 0xFF);              // 0x004CC546
    cx += w;                                                           // 0x004CC555
}
```

* Q14 products are `(int64)a * b >> 14` (`sub_00401B60` / `sub_00401BD0`), the advance accumulates in Q14 and
  only the vertex positions are converted to float (`sub_00401B10`). Positions are not rounded [V].
* The glyph's **top-left is (cx, y)**; glyph height = its own sprite height (all glyphs of one font are the
  same height in the shipped styles [I: line height is taken from 'A' only]).
* Characters other than 0x0A/0x20/0x23 below 0x21 (0x0D, tab) give a negative index (16-bit wrap) -> garbage
  sprite [V: `add ebx,-0x21`, `add ax,[esp+8]` is 16-bit in sub_004BF550].

### 1.3 Font tables in the style object [V]

| field | meaning |
|---|---|
| `[style+0x1C]` -> `u16[]` | FONB: `[0]` = font count, `[1+i]` = **first sprite of font i** (exclusive prefix sum of the FONB glyph counts, made in place by `sub_004BF7B0`) [V 0x004BFDFB-0x004BFE55, 0x004BF7B0] |
| `[style+2]` u16 | total font glyphs (sum) [V 0x004BFE4C] -> count of the last font = total - base[last] |
| `[style+0x14]` -> `u16[6]` | SPRB bases; `+0xA` = first sprite of type 7 (fonts) [V sub_004BF2A0 case 7] |
| `[style+0x20]` -> 8-byte records | `+0 u32` pixel pointer (8-bit, pitch 256), `+4 u8 w`, `+5 u8 h` [V sub_004BF280, 0x004CC350, 0x00460E17] |
| space width `sub_004BF5D0` | width of glyph index 0x4D = **char 'n'** (`+0x26C` = 0x4D*8+4); kanji fonts 0x10 / 0x20 [V 0x004BF5D0] |
| line height `sub_004BF630` (`sub_004539B0` = cdecl-ish wrapper) | height of glyph index 0x20 = **char 'A'** (`+0x105`); kanji 0x11 / 0x22 [V 0x004BF630] |

Glyph counts (read from the files [V]): fstyle.sty 14 fonts `140,1,1,140,144 x10`; level styles (wil/ste/bil)
7 fonts `144,144,144,1,1,145,144`. The 1-glyph fonts are the kanji scratch sprites. So valid codes are
`0x21 .. 0x20+count` (0xB0 for 144, 0xAC for 140); higher codes silently draw the next font's glyphs.

Font-id globals, set by `sub_004BFCC0` from the font count [V 0x004BFCC0-0x004BFDAB]:

| global | fstyle (14) | level (7) | used for |
|---|---|---|---|
| 0x670678 | 0 | 0 | HUD brief box font (western) |
| 0x67065C | 1 | 3 | **KANJI_S** (16 px) |
| 0x670650 | 2 | 4 | **KANJI_L** (32 px) |
| 0x670654 | 3 | 6 | |
| 0x67066C | 4 | - | menu normal font (`fe+0x11C`) |
| 0x67064C | 5 | - | menu highlight font |
| 0x670668 | 99 | 1 | HUD main font (pager, most HUD text) |
| 0x670640 | 99 | 2 | HUD |
| 0x670674 | 99 | 5 | HUD (145 glyphs) |
| 0x670658 0x67067C 0x670664 0x670648 0x670670 0x670660 0x670680 0x670644 | 6..13 | - | frontend |

HUD font slots `sub_004CA520` [V 0x004CA520-0x004CA5CE]:

| slot | western | 'j' | user (examples) |
|---|---|---|---|
| 0x672F2C | [0x670678] | KANJI_L | mission brief box `sub_004C5FE0` / `sub_004C8A40` |
| 0x672F3C | [0x670668] | KANJI_S | pager/speaker message `sub_004C6470` / `sub_004C9430` |
| 0x672F18 0x672F24 0x672F34 0x672F14 | [0x670668] | KANJI_S | many HUD texts |
| 0x672F1C 0x672F38 | [0x670668] | [0x670668] | `sub_004C9A40`, `sub_004C8910` (numbers) |
| 0x672F28 | [0x670640] | KANJI_S | `sub_004C6940`, `sub_004C9638` |
| 0x672F30 | [0x670674] | KANJI_S | `sub_004C6AD0`, `sub_004C99E2` |
| 0x672F20 | [0x670654] | KANJI_L | `sub_004C9FA0` (PAUSED panel) |

### 1.4 Colour / palette of a glyph [V]

* Colour is the **palette** of the glyph texture, chosen by `sub_004C2AC0(7, spr, mode, remap)`: `mode-2`
  (0..6) picks a palette table (`+0x1548 .. +0x15C0` of `[0x671978]`); `remap` selects the remap inside it
  [V 0x004C2AC0-0x004C2B81]. Callers pass mode 2 (base palette) or mode 8 + a remap (menu items).
* **`'#'` toggles** between the caller's `(mode, remap)` and `(8, 0)` (the highlight remap) [V 0x004CC239-0x004CC264].
* `blend`/`alpha` (args 8/9) add translucency or additive blending + a white diffuse with 5-bit alpha
  (section 1.2); with 0 the vertex colour is the gbh default white.
* Kanji fonts: two palette indices `kanji+0x24` (fill) / `+0x25` (shadow): default 0xFF/0xF1
  (`sub_004CBA40`), remap 0 -> 0x88/0x83, 5 -> 0x27/0x23, 6 -> 0x47/0x43 (`sub_00460DA0`), ctor 0xFE/0xF7.

### 1.5 Special codes

| code | where handled | effect |
|---|---|---|
| 0x0A | drawer, width, height, wrap | new line; x back to start, y += line height (scaled) [V] |
| 0x20 | drawer, width, wrap | advance = width of 'n' (scaled); wrap break point [V] |
| 0x23 `#` | drawer, width (0 width), wrap (0 width) | colour toggle (1.4) [V] |
| `#W #B #G #C`, `*` | credits parser `sub_00449D70` only | `#`+letter = credits line colour (+6: W 0, B 2, G 3, C 1, other 4 [V 0x00449DE4-0x00449E14]); `*` = end of credits line / empty line (+0x14 y) [V 0x00449DB7, 0x00449E43]. Not used by the drawer. |
| U+216B..U+217A (first char of a message) | `sub_004C63F0` from `sub_004C6470` | speaker portrait; char **removed** (string starts at +2) [V 0x004C657A]. Map: 216B->6, 216C->1, 216D->7, 216E->8, 2170->9, 2172->4, 2173->5, 2179->2, 217A->3; others (216F, 2171, 2174..2178) -> 0 = not a speaker, char kept, speaker = 8 [V table 0x004C6424] |
| U+2153 | nowhere | only in e.gxt key `2495` ("⅓Here is another #Taxi#"). Not a speaker code, so it stays in the text and is drawn as glyph 0x2132 -> garbage/out of range [V: no constant 0x2153 in the exe]. Treat as zero-width in our hook. |

The portrait is drawn by `sub_004C9430`: sprite `6, (speaker-1)*3 + [obj+0x50C] + 0x13` at (0x20, 0x1BB); text at
x = 0x40, y = 0x1E0 - line_h * lines [V 0x004C9442-0x004C94DD].

---

## 2. Measuring chain [V]

| function | signature | result |
|---|---|---|
| `sub_004BF570` | thiscall style, `(u16 *font, u16 *ch)` ret 8, u16 in ax | `[style+0x20][(fontbase+sprbase7+ch)*8 - 0x104]` = width byte of glyph `ch-0x21`; KANJI_S 0x10, KANJI_L 0x20. **32-bit index, no range check** [V 0x004BF5B0-0x004BF5C0] |
| `sub_004539D0` | `(u16 font, u16 ch)` stdcall 8 | wrapper of the above (by value) [V] |
| `sub_004BF5D0` / `sub_004C23D0` | `(&font)` / `(font)` | space width = width of 'n' |
| `sub_004BF630` / `sub_004539B0` | `(&font)` / `(font)` | line height = height of 'A' |
| `sub_004CB0C0` | `(u16 *s, u16 font)` stdcall 8 | **unscaled** pixel width of the widest line: space -> space_w, 0x0A -> new line, '#' -> 0, else `sub_004BF570` [V 0x004CB0C0-0x004CB13A] |
| `sub_004CC0C0` | `(u16 *s, u16 font)` stdcall 8 | unscaled height = line_h * (1 + count of 0x0A) [V 0x004CC0C0] |
| `sub_004521B0` | thiscall fe `(s, font, cx)` | `cx - sub_004CB0C0/2` (menu centring) |

Callers of `sub_004CB0C0` [V]: frontend `sub_004521B0 sub_00453590 sub_004566C0 sub_004568C0 sub_00457140
sub_00457920`; HUD `sub_004C5FE0` (brief x = (640-w)/2), `sub_004C6940 sub_004C69D0 sub_004C6A60 sub_004C6AD0`,
`sub_004C8710`, `sub_004C8910` (x = 640-w), `sub_004C8AA0` (x = (640-w)/2, y = (480-h)/2), `sub_004C9A40`
(x = 638-w), `sub_004C9FA0`. Callers of `sub_004539D0`: `sub_004C2450` (wrap) and the name-entry caret code
`sub_00459E30`, `sub_0045A010`.

**Widths are the sprite width byte only**: no kerning table, no spacing constant; glyphs sit edge to edge
(the sprites contain their own spacing) [V: the drawer advances by exactly `rec->w * scale`].
Nothing else reads glyph widths [V: `sub_004BF570` has only the two callers above; `sub_004BF550` only
`sub_004CC100` and `sub_00460DE0`].

---

## 3. Word wrap [V]

### 3.1 `sub_004C2450(u16 *dst, u16 *src, int max_w, u16 font)` stdcall 0x10 -> line count [V 0x004C2450-0x004C253B]

```c
lines = 1; w = 0; sp_src = sp_dst = NULL; space_w = sub_004C23D0(font);
for (; *src; src++, dst++) {
    *dst = *src; c = *src;
    if (c == 0x0A)      { lines++; w = 0; sp_src = sp_dst = NULL; }
    else if (c == 0x20) { w += space_w; sp_src = src; sp_dst = dst; }
    else if (c != 0x23)   w += sub_004539D0(font, c);
    if (w > max_w) {
        if (sp_src && sp_dst) { *sp_dst = 0x0A; src = sp_src; dst = sp_dst; sp_src = sp_dst = NULL; w = 0; }
        else { *dst++ = 0x0A; w = sub_004539D0(font, *src); *dst = *src; }  // break before this char
        lines++;
    }
}
*dst = 0; return lines;
```

* Breaks by **width**, at the last space of the line; with no space on the line it inserts a break **before the
  overflowing character** (and that char's width starts the new line). So CJK text without spaces wraps
  per character already, as long as `sub_004539D0` returns the right widths. No kinsoku rules (a line may
  start with `。` or `、`) [V: no other tests].
* A forced break inserts one extra u16 into `dst` -> dst grows by one char per forced break [V]. Spaces are not
  removed (the space becomes the 0x0A).
* Units: unscaled pixels in the 640x480 space.

### 3.2 Users and buffer sizes

| caller | src | max_w | font | dst / size [I from object layout] |
|---|---|---|---|---|
| `sub_004C5FE0` mission brief box | obj+2 (0xC8 bytes = 100 chars) | 0x244 (580) | [0x672F2C] | obj+0xCA .. +0x1BC = 121 chars; x/y at +0x1BC/+0x1C0 |
| `sub_004C6470` pager / speaker message | GXT text (copied to 0x5E9F08 with `swprintf`) | `[0x595004]` = 0x240 (576) | [0x672F3C] | obj+0 .. +0x502 = 641 chars; lines at +0x508 |
| `sub_004C8AA0` centred HUD message | arg | 0x280 (640) | arg | obj+0 .. +0xA4 = 82 chars; then `sub_004C2250` upper-case if font is [0x670674]/[0x670678] |
| `sub_00456820` frontend help line | GXT key = number | 0x230 (560) | [0x67066C] | stack buffer ~0x1F8 bytes; drawn at (40, 270) |

With the debug flag `[0x5EADA5]` the pager text is prefixed with its key via format `"(%s)%s"` (0x575698) [V].

### 3.3 Byte / < 0x100 assumptions [V]

* Drawer and width: index = `ch - 0x21` without bound -> any char above the font's last glyph (0xB0) draws/
  measures a wrong sprite; CJK codes (0x4E00+) index up to ~0.5 MB past the sprite record table.
* `sub_004C21A0` (upper-case, used by `sub_004C2250`): a-z -> A-Z, 0x80..0xFF via the tables 0x594DF8 (French:
  accented -> plain capital, e.g. 0x97 a-grave -> 'A') or 0x594CF8 (German/Italian/Spanish: lower accent ->
  upper accent, 0x97 -> 0x80). Other chars unchanged. Language index = letter - 'e' through the byte map at
  0x4C2234 `{e:0, f:1, g:2, h:3, i:2, j:3, k..r:3, s:2}`; result 3 = **no change at all, not even a-z**;
  letters outside e..s (e.g. 'c', 'z') -> no change [V 0x004C21A0-0x004C221E].
  Callers: `sub_004587B0` (frontend), `sub_004C6060`, `sub_004C6960`, `sub_004C6AF0`, `sub_004C8AA0`.
* Name entry (`sub_00459E30`, `sub_0045A010`): typed chars come from `data\keyboard\*_kb.cfg`, chosen by the
  Windows keyboard layout (`GetKeyboardLayoutNameA`, `sub_00461060`), not by the language [V 0x00461081].
* GXT strings are full u16 everywhere else (`wcsncpy`, `swprintf`, `wcslen`) [V]. Nothing truncates to bytes.

---

## 4. Languages

### 4.1 Letter [V]

* `sub_004C2620` (GXT ctor): `Option\language` string -> 0x5E9808, first char must be `e f g i s j`, else
  `'e'`; stored in `gxt+0x10` [V 0x004C2670-0x004C26A7]. Port menu: `g_lang_code = "efgisj"` (src/gta2_menu.c).
* `sub_004C2540`: file `data\%c.gxt` (0x57555C), header checked as `"GBL%c"` with the letter upper-cased
  (`letter - 0x20`, 0x575554) [V 0x004C2550-0x004C2572].
* Readers of `gxt+0x10`: `sub_004C2540` (file), `sub_004C21A0` (upper-case), `sub_00452E60` (`== 'j'`) [V].
* `sub_00451800` probes which of e/f/g/i/j/s.gxt exist (French-only flag, frontend.md section 7).

### 4.2 Every `'j'` special case [V: the three callers of `sub_00452E60`]

| site | effect for 'j' |
|---|---|
| `sub_00453D40` (frontend build) | menu default font `fe+0x11C` = KANJI_S instead of [0x67066C] (highlight font [0x67064C] unchanged) |
| `sub_0045C4D0` 0x0045D2B3 (game init) | `new Kanji` (0x28 bytes, `sub_00460FB0`) -> `[0x661E98]`; loads `data\kanji.dat` |
| `sub_004CA520` (HUD init) | HUD font slots -> KANJI_S / KANJI_L (table 1.3) |
| `sub_004C21A0` | no upper-casing |

No other language test exists (no different wrapping, no double-byte decoding in the GXT reader).

### 4.3 The Japanese (kanji) renderer [V unless marked]

* `data\kanji.dat`: chunk file like the GXT (`sub_00403160`, version 100), chunks `KIDX` -> `kanji+0`
  (u16 table, entry count `size/2` at `+8`) and `KBIT` -> `kanji+4` (glyph bitmaps, **32 bytes = 16x16 1-bit**,
  MSB first) [V sub_0045FF70, 0x00460F10].
* `sub_00460DE0` (level start): uses the single glyph of KANJI_S / KANJI_L as scratch: `+0x10/+0x1C` sprite
  record, `+0xC/+0x18` its pixels, `+0x14/+0x20` its texture (mode 8, remap 8).
* Per character (`sub_00460CC0` small, `sub_00460D30` large): `code = ch < 0x100 ? ch << 8 : ch`;
  `g = KIDX[code]` (0xFFFF -> `KIDX[0x2100]`); `gbh_LockTexture`; expand bitmap `KBIT + g*32` into the scratch
  pixels (pitch 256) with colour `+0x24` (`sub_004600E0`, or 2x scaled `sub_00460480`); shadow pass with `+0x25`
  (`sub_00460030(pixels, 16, 17)` / `(32, 34)`); `gbh_UnlockTexture` -> the texture is re-uploaded for every
  character, then the drawer draws the scratch sprite. Monospace 16 (line 17) / 32 (line 34).
* Codes: `ch<<8` for single bytes and `KIDX[0x2100]` as fallback suggest **JIS X 0208 row/cell codes**
  (0x2121..0x7E7E) in j.gxt, not Unicode [I]. We have neither j.gxt nor kanji.dat.
* Reusable ideas: the HUD/menu font switch points (`sub_004CA520`, `sub_00453D40`) and the "draw into a scratch
  texture per char" trick. Not reusable as is: 16 px 1-bit, no Unicode, needs kanji.dat, and the 1-glyph
  fonts break every other text (scores, names) drawn with the same font slots [I].
  Wrapping needs nothing special: `sub_004C2450` already breaks per character (3.1).

---

## 5. Hook plan for "our glyph for characters outside the font"

### 5.1 What counts as "ours"
`ours(font, c)` = `c >= 0x21 + count(font)` (count from 1.3, normally 0xB1+), or a code we decide to
override; never 0x0A/0x20/0x23. Keep the game's 0x80..0xB0 accent codes as they are (tools/gxt.py), so
Polish `ó` stays the game glyph 0xA6 and `ą ę ł ś ż ź ć ń`, Cyrillic, CJK use their real Unicode values.
U+2150..U+218F not consumed as speaker -> zero width, nothing drawn (fixes key 2495).

### 5.2 Draw: wrap `sub_004CC100` (stdcall, ret 0x24)

```c
extern void sub_004CC100_gen(RECOMP_PARAMS);
static int g_in_text;
void sub_004CC100(RECOMP_PARAMS)
{
    uint32_t sp = ctx->r_esp, s = MEM32(sp + 4);
    uint16_t font = MEM16(sp + 0x10);
    if (g_in_text || font == KANJI_S || font == KANJI_L || !has_ours(s, font)) { sub_004CC100_gen(ctx, base); return; }
    int32_t x0 = MEM32(sp + 8), cx = x0, y = MEM32(sp + 0xC), sc = MEM32(sp + 0x14);
    uint32_t pmode = MEM32(sp + 0x18), remap = MEM32(sp + 0x1C), blend = MEM32(sp + 0x20), alpha = MEM8(sp + 0x24);
    uint32_t mode = MEM32(pmode), cm = mode, cr = remap;          // '#' state, as 1.2
    g_in_text = 1;
    for each char c:
        0x0A: cx = x0; y += q14mul(sc, line_h(font));
        0x20: cx += q14mul(sc, space_w(font));
        0x23: toggle (cm,cr) between (mode,remap) and (8,0);
        ours: draw_ttf(c, cx, y, q14mul(sc, our_w(font,c)), q14mul(sc, line_h(font)),
                       colour_of(font, cm, cr), flags_of(blend, alpha)); cx += q14mul(sc, our_w(font,c));
        else: collect a run of native chars (no 0x0A/0x20/0x23/ours) into a guest buffer, call the
              original via wr_call_guest(0x004CC100, {buf, cx, y, font, sc, &cm_guest, cr, blend, alpha})
              (re-enters this wrapper -> g_in_text -> _gen), then cx += sum q14mul(sc, rec->w).
    g_in_text = 0;
    MEM32(sp + 4) = EMPTY_U16_STRING;     // the original returns at once on "" (0x004CC1E8) ...
    sub_004CC100_gen(ctx, base);          // ... and does the stdcall cleanup itself
}
```

* Feeding runs with **(cm, cr) as the base mode** is exact because the run contains no `'#'`.
* Advance: use exactly the original arithmetic `(int64)(w << 14) * sc >> 14` per glyph so the native glyphs
  land where they would without the hook (bit-exact at 4:3).
* `draw_ttf` must go through the same quad stream as gbh_DrawQuad (painter's order, no z-buffer, gbh_api.md);
  top-left = (cx, y) in back-buffer pixels = Q14/16384 (the HUD wrappers already applied HUD scale and the
  port's widescreen anchor to x/y/scale). Use point filtering? The game sets 0x20000 (point); a TTF glyph
  rendered at the target pixel size looks best linear [I].
* **Colour**: the font glyphs are palette art with outline/shading. Take the colours from a reference glyph
  of the same font with the same `(cm, cr)`: `tex = sub_004C2AC0([0x671978], 7, sub_004BF2A0(7,
  sub_004BF550(font, 'A'-0x21)), cm, cr)`; the port's gbh keeps `gtex` (pixels + `pal->rgba`) for that guest
  `GBH_Texture*` (`tex_find`, src/render/gbh.c). Sample per row the main fill colour and the outline colour
  -> vertical ramp + 1 px outline for the TTF mask. Cache per (font, cm, cr, palette serial).
  Apply `blend`/`alpha` like `sub_004CB050` (translucent/additive, alpha<<3).
* Size: height = `line_h(font)` (the 'A' sprite height, e.g. ~16 px HUD font) x scale; the glyph box should be
  this tall and include the same top/bottom padding as the sprites, so line spacing stays.

### 5.3 Widths: wrap `sub_004BF570` (thiscall style, `(u16 *font, u16 *ch)`, ret 8)

```c
void sub_004BF570(RECOMP_PARAMS)
{
    uint32_t sp = ctx->r_esp;
    uint16_t font = MEM16(MEM32(sp + 4)), c = MEM16(MEM32(sp + 8));
    if (!ours(font, c)) { sub_004BF570_gen(ctx, base); return; }
    MEM32(sp + 8) = GUEST_U16_A;          // a safe char so the original reads a valid record
    sub_004BF570_gen(ctx, base);          // does the ret 8 cleanup
    ctx->r_eax = (ctx->r_eax & 0xFFFF0000u) | our_w(font, c);   // unscaled 640x480 pixels
}
```

This one hook fixes `sub_004CB0C0` (centring, right alignment, menu widths), `sub_004539D0` (word wrap and the
name-entry caret) and the port's `text_w` (`FN_TEXT_W`). `our_w` must be the **same value** the draw hook
advances by (unscaled, integer, in the font's pixel grid), e.g. the TTF advance rounded at the font's
`line_h` size, so measure == draw. `sub_004BF5D0` (space) and `sub_004BF630` (line height) need no change.

### 5.4 Optional hooks

| hook | why |
|---|---|
| `sub_004C2620` (GXT ctor, thiscall) | accept new letters (e.g. `p`, `r`, `c`/`z` for zh, Japanese not as `j`): call `_gen`, then if the registry letter is ours write it to `gxt+0x10`. `sub_004C2540` then loads `data\<l>.gxt` with header `GBL<L>`. Also add the letter to the port's `g_lang_code` |
| `sub_004C21A0` (thiscall gxt, `(u16 c)` ret 4) | upper-casing for the new languages: as written, a new letter gives no upper-casing at all (3.3). Add Polish/Cyrillic upper-case (CJK unchanged) |
| `sub_004C2450` | optional kinsoku for CJK (no line start with `。、」！？`), keep the dst growth limit (3.2) |

### 5.5 Things to watch [I]

* The brief box source holds 100 u16, the centred message 82: CJK strings are short in chars, Polish/Russian
  long strings may need the same limits as the existing languages (check against `en.json`).
* Never select fonts KANJI_S/KANJI_L for our languages (they have 1 glyph); i.e. never set the letter `'j'`
  unless kanji.dat exists.
* `ours()` must use the font of the call; the menu highlight font [0x67064C] and normal font [0x67066C] are
  different sprite sets with their own widths.
