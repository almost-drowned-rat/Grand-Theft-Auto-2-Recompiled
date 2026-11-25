#!/bin/sh
# Lift gta2.exe to C with winrecomp: disasm -> func_id -> abi -> recomp.
# Usage: tools/lift.sh [path/to/gta2.exe]   (default: extract/game/App_Executables/gta2.exe)
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
tk=${WINRECOMP:-$here/../winrecomp}
py=${PYTHON:-$tk/.venv/bin/python}
exe=$(realpath "${1:-$here/extract/game/App_Executables/gta2.exe}")
cfg=$here/config/game.toml
a=$here/analysis
cd "$tk"
"$py" -m tools.disasm "$exe" --game-config "$cfg" -o "$a/disasm" --force
"$py" -m tools.func_id "$exe" --functions "$a/disasm/functions.json" \
    --strings "$a/disasm/strings.json" --xrefs "$a/disasm/xrefs.json" --output "$a/func_id"
"$py" -m tools.abi_analysis "$exe" --disasm-dir "$a/disasm" --func-id-dir "$a/func_id" --output-dir "$a/abi"
"$py" -m tools.recomp "$exe" --game-name gta2 --game-config "$cfg" --all \
    --disasm-dir "$a/disasm" --func-id-dir "$a/func_id" --abi-dir "$a/abi" \
    -o "$here/gen" --gen-dir "$here/gen/src"
