#!/bin/sh
# Builds GTA2-translations.pak from the translation work files (private/translations/<lang>/):
# merges part_*.json + keep_english.json into <lang>.json, checks it (tools/tr_check.py, 0 problems
# needed), builds data/<letter>.gxt in the English key order (tools/gxt.py) and zips all languages.
#   tools/make_translations.sh GAME_DATA_DIR OUT.pak de:g [fr:f ...]
set -e
here=$(cd "$(dirname "$0")" && pwd)
data=$1; out=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); shift 2
work=$here/../private/translations
tmp=$(mktemp -d)
mkdir -p "$tmp/data"
for pair in "$@"; do
    lang=${pair%%:*}; letter=${pair##*:}
    python3 -I - "$work/$lang" "$work/$lang.json" <<'P'
import json, glob, sys
d = {}
for f in sorted(glob.glob(sys.argv[1] + "/part_*.json")) + [sys.argv[1] + "/keep_english.json"]:
    d.update(json.load(open(f, encoding="utf-8")))
json.dump(d, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False, indent=1)
P
    python3 -I "$here/tr_check.py" "$work/en.json" "$work/$lang.json" | tail -1
    python3 -I "$here/tr_check.py" "$work/en.json" "$work/$lang.json" > /dev/null
    python3 -I "$here/gxt.py" build "$work/$lang.json" "$letter" "$tmp/data/$letter.gxt" --like "$data/e.gxt"
done
rm -f "$out"
(cd "$tmp" && python3 -I -c "import zipfile,os,sys
z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED)
for f in sorted(os.listdir('data')): z.write('data/'+f)
z.close()" "$out")
rm -rf "$tmp"
echo "$out"
