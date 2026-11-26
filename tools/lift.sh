#!/bin/sh
# Lift a PE module to C with winrecomp: disasm -> func_id -> abi -> recomp.
#   tools/lift.sh                       gta2.exe  -> gen/src      (prefix gta2_)
#   tools/lift.sh bink                  binkw32.dll -> gen_bink/src (prefix bink_)
# The dispatch symbols and the entry function of each module get a prefix so
# several modules link into one executable; the runtime's recomp_lookup
# routes by address (winrecomp/runtime wr_modules).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
tk=${WINRECOMP:-$here/../winrecomp}
py=${PYTHON:-$tk/.venv/bin/python}
mod=${1:-gta2}
case "$mod" in
gta2) exe=$here/extract/game/App_Executables/gta2.exe; out=$here/gen; a=$here/analysis
      cfg="--game-config $here/config/game.toml"; split="";;
bink) exe=$here/extract/game/App_Executables/binkw32.dll; out=$here/gen_bink; a=$here/analysis_bink
      cfg=""; split="--split 1000";;
*) echo "unknown module $mod"; exit 1;;
esac
exe=$(realpath "$exe")
cd "$tk"
"$py" -m tools.disasm "$exe" $cfg -o "$a/disasm" --force
"$py" -m tools.func_id "$exe" --functions "$a/disasm/functions.json" \
    --strings "$a/disasm/strings.json" --xrefs "$a/disasm/xrefs.json" --output "$a/func_id"
"$py" -m tools.abi_analysis "$exe" --disasm-dir "$a/disasm" --func-id-dir "$a/func_id" --output-dir "$a/abi"
rm -rf "$out/src"
"$py" -m tools.recomp "$exe" --game-name "$mod" $cfg --all $split \
    --disasm-dir "$a/disasm" --func-id-dir "$a/func_id" --abi-dir "$a/abi" \
    -o "$out" --gen-dir "$out/src"
# module prefix for the symbols every generated module defines
cd "$out/src"
sed -i -e "s/\brecomp_lookup(/${mod}_recomp_lookup(/; s/\brecomp_dispatch_init(/${mod}_dispatch_init(/" \
       -e "s/\brecomp_dispatch_flat_bytes(/${mod}_dispatch_flat_bytes(/; s/\brecomp_get_count(/${mod}_recomp_get_count(/" \
       -e "s/\brecomp_call_all(/${mod}_recomp_call_all(/" recomp_dispatch.c
sed -i "s/\bxbe_entry_point\b/${mod}_entry_point/g" recomp_*.c recomp_funcs.h
echo "lifted $mod -> $out/src"
