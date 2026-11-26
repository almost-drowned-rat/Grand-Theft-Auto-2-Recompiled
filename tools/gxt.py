#!/usr/bin/env python3
"""GTA 2 GXT text files: read, write, and the game font's character codes.

Format (verified on the freeware e.gxt / s.gxt): "GBL" + language letter + u16 version (100),
then chunks: TKEY (12-byte entries: u32 offset into TDAT, char[8] key) and TDAT (UTF-16LE
strings, each ending in a 0 word).

Characters: plain ASCII is itself. The game font's accents sit at 0x80..0xB0 (glyphs read from
the level styles' fonts, 2026-11-26): 0x80..0x95 A-grave .. U-diaeresis (uppercase), 0x96 sharp s,
0x97..0xAC a-grave .. u-diaeresis, 0xAE n-tilde, 0xAF inverted ?, 0xB0 inverted !. U+2153 and
U+216B..U+217A are the speakers' portrait codes, '#' toggles the colour - kept as they are.

  gxt.py export FILE.gxt OUT.json   - {key: text} in Unicode (accents decoded)
  gxt.py build  IN.json LETTER OUT.gxt [--like FILE.gxt]   - the key order of FILE when given
  gxt.py check  IN.json             - every character drawable by the game font
  gxt.py roundtrip FILE.gxt         - export + build must give the same keys and texts
"""
import json
import struct
import sys

UPPER = "ÀÁÂÄÆÇÈÉÊËÌÍÎÏÒÓÔÖÙÚÛÜ"
LOWER = "àáâäæçèéêëìíîïòóôöùúûü"
CODE_TO_CHAR = {}
for i, c in enumerate(UPPER):
    CODE_TO_CHAR[0x80 + i] = c
CODE_TO_CHAR[0x96] = "ß"
for i, c in enumerate(LOWER):
    CODE_TO_CHAR[0x97 + i] = c
CODE_TO_CHAR.update({0xAE: "ñ", 0xAF: "¿", 0xB0: "¡"})
CHAR_TO_CODE = {v: k for k, v in CODE_TO_CHAR.items()}
# characters the game writes as they are (besides printable ASCII)
PASS = {chr(0x2153)} | {chr(c) for c in range(0x216B, 0x217B)}


def read(path):
    d = open(path, "rb").read()
    if d[:3] != b"GBL":
        raise SystemExit(f"{path}: not a GTA 2 GXT")
    head = d[:6]
    p, ch = 6, {}
    order = []
    while p + 8 <= len(d):
        tag, n = d[p:p + 4], struct.unpack("<I", d[p + 4:p + 8])[0]
        ch[tag] = d[p + 8:p + 8 + n]
        order.append(tag)
        p += 8 + n
    keys, dat = ch[b"TKEY"], ch[b"TDAT"]
    out = {}
    for i in range(len(keys) // 12):
        off, name = struct.unpack("<I8s", keys[i * 12:i * 12 + 12])
        name = name.split(b"\0")[0].decode("latin-1")
        e = off
        while dat[e:e + 2] != b"\0\0":
            e += 2
        out[name] = dat[off:e].decode("utf-16-le", "surrogatepass")
    return head, out, d


def plain_key(key, path=""):
    """Windows UI texts, plain Latin-1 (not the game font): the network dialogs (netui*) and the
    old launcher's bob_*.gxt (gta2 manager.exe; the port does not use it)."""
    return key.startswith("netui") or "bob_" in path


def decode(s, plain=False):
    return s if plain else "".join(CODE_TO_CHAR.get(ord(c), c) for c in s)


def encode(s, where="", plain=False):
    if plain:
        return s
    out = []
    for c in s:
        if c in CHAR_TO_CODE:
            out.append(chr(CHAR_TO_CODE[c]))
        elif 0x20 <= ord(c) < 0x7F or c in PASS or c == "\n" or 0x80 <= ord(c) <= 0xB0:
            out.append(c)
        else:
            raise ValueError(f"{where}: {c!r} (U+{ord(c):04X}) is not in the game font")
    return "".join(out)


def build(texts, letter, version=100):
    keys, dat = b"", b""
    for k, v in texts.items():
        kb = k.encode("latin-1")
        if len(kb) > 8:
            raise ValueError(f"key {k!r} longer than 8")
        keys += struct.pack("<I8s", len(dat), kb)
        dat += v.encode("utf-16-le", "surrogatepass") + b"\0\0"
    head = b"GBL" + letter.upper().encode() + struct.pack("<H", version)   # the game checks "GBLE" / "GBLS" ...
    return head + b"TKEY" + struct.pack("<I", len(keys)) + keys + b"TDAT" + struct.pack("<I", len(dat)) + dat


def main(a):
    if len(a) >= 3 and a[0] == "export":
        _, t, _ = read(a[1])
        json.dump({k: decode(v, plain_key(k, a[1])) for k, v in t.items()}, open(a[2], "w", encoding="utf-8"), ensure_ascii=False,
                  indent=1)
    elif len(a) >= 4 and a[0] == "build":
        texts = json.load(open(a[1], encoding="utf-8"))
        if "--like" in a:
            _, like, _ = read(a[a.index("--like") + 1])
            missing = [k for k in like if k not in texts]
            if missing:
                raise SystemExit(f"missing keys: {missing[:10]} ({len(missing)})")
            texts = {k: texts[k] for k in like}
        open(a[3], "wb").write(build({k: encode(v, k, plain_key(k)) for k, v in texts.items()}, a[2]))
    elif len(a) >= 2 and a[0] == "check":
        texts = json.load(open(a[1], encoding="utf-8"))
        bad = 0
        for k, v in texts.items():
            try:
                encode(v, k, plain_key(k))
            except ValueError as e:
                print(e)
                bad += 1
        print(f"{len(texts)} texts, {bad} with characters the font lacks")
        return 1 if bad else 0
    elif len(a) >= 2 and a[0] == "roundtrip":
        head, t, raw = read(a[1])
        dec = {k: decode(v, plain_key(k, a[1])) for k, v in t.items()}
        again = build({k: encode(v, k, plain_key(k, a[1])) for k, v in dec.items()}, chr(head[3]), struct.unpack("<H", head[4:6])[0])
        import tempfile, os
        fd, tmp = tempfile.mkstemp(suffix=".gxt")
        os.write(fd, again)
        os.close(fd)
        h2, t2, _ = read(tmp)
        os.remove(tmp)
        same = h2 == head and t2 == t
        print(a[1], f"same texts ({len(t)} keys)" if same else "DIFFERENT texts")
        return 0 if same else 1
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
