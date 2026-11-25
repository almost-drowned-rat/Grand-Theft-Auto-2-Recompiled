#!/bin/sh
# GLSL -> SPIR-V C arrays (committed; rerun after edits).
set -eu
cd "$(dirname "$0")"
for s in gbh.vert gbh.frag; do
    glslc --target-env=vulkan1.1 -O -mfmt=c "$s" -o "$(echo "$s" | tr . _).spv.h"
done
