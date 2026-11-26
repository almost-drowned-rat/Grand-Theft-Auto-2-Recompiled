#!/usr/bin/env python3
"""Checks a translation against the English source (both {key: text} JSON from tools/gxt.py).

Per key: present; the same control tokens as the English - '#' highlights / colour codes, '*' line
breaks, %d / %s placeholders, the speaker portrait codes (U+2153, U+216B..U+217A) at the same
place at the start - and every character drawable by the game font (tools/gxt.py encode).
Keys kept in English (credits, vehicle names) must be unchanged.

  tr_check.py EN.json XX.json [--keys KEYS.json] [--notes]   (exit 1 on any problem)
"""
import json
import re
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import gxt  # noqa: E402

KEEP_ENGLISH = re.compile(r"^credits$")   # vehicle names: per language (maintainer's choice)
PORTRAIT = re.compile("[⅓Ⅻ-ⅺ]")


def tokens(s):
    return (s.count("#"), s.count("*"), tuple(re.findall(r"%[ds]", s)), tuple(PORTRAIT.findall(s)))


def main(a):
    en = json.load(open(a[0], encoding="utf-8"))
    tr = json.load(open(a[1], encoding="utf-8"))
    keys = json.load(open(a[a.index("--keys") + 1], encoding="utf-8")) if "--keys" in a else list(en)
    bad, warn = [], []
    for k in keys:
        if k not in tr:
            bad.append(f"{k}: missing")
            continue
        e, t = en[k], tr[k]
        if KEEP_ENGLISH.match(k) and t != e:
            bad.append(f"{k}: must stay English")
        te, tt = tokens(e), tokens(t)
        if te[1:] != tt[1:]:
            bad.append(f"{k}: control tokens differ: * {te[1]}/{tt[1]}, % {te[2]}/{tt[2]}, portraits {te[3]}/{tt[3]}")
        if tt[0] % 2 != te[0] % 2:
            bad.append(f"{k}: '#' highlights do not pair up ({tt[0]}, the English has {te[0]})")
        elif tt[0] != te[0]:
            warn.append(f"{k}: note: {tt[0]} '#' (English {te[0]}) - other words highlighted")
        if PORTRAIT.match(e) and (not t or t[0] != e[0]):
            bad.append(f"{k}: the portrait code must stay the first character")
        try:
            gxt.encode(t, k, gxt.plain_key(k))
        except ValueError as ex:
            bad.append(str(ex))
    extra = [k for k in tr if k not in en]
    if extra:
        bad.append(f"keys not in the English: {extra[:10]}")
    for b in bad:
        print(b)
    if "--notes" in a:
        for w in warn:
            print(w)
    print(f"{len(keys)} keys checked, {len(bad)} problems, {len(warn)} notes")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
