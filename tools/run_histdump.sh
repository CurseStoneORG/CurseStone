#!/usr/bin/env bash
# run_gen_equiv.sh — compiles and runs the vanilla generation equivalence
# check (tools/gen_histdump.cpp) against the real generator sources.
#
#   tools/run_gen_equiv.sh [seed] [radius] [threads]
#
# Environment:
#   CURSESTONE_NOISE_SCALAR=1  forces the scalar noise path (SIMD guard
#                              comparison). Two runs — with and without —
#                              must print identical digests.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/gen_equiv}"
INC="-Isrc -Isrc/world/generator"
STD="-std=c++20 -O2 -w"

# Sources the equivalence tool needs: the vgen vanilla generator tree plus
# the performance module and the infrastructure it links (chunk format,
# NBT, tiles, logger, utils). Rebuilt only when a source is newer than the
# binary — the check runs many times in a row.
SRCS=$(ls tools/gen_histdump.cpp \
    src/world/generator/vgen/*.cpp \
    src/world/generator/vgen/synth/*.cpp \
    src/world/generator/vgen/biome/*.cpp \
    src/world/generator/vgen/feature/*.cpp \
    src/world/generator/vgen/material/*.cpp \
    src/world/generator/vgen/newbiome/*.cpp \
    src/world/generator/vgen/structure/*.cpp \
    src/world/generator/vgen/v1/*.cpp \
    src/world/performance/*.cpp \
    src/world/performance/platform/*.cpp \
    src/world/format/*.cpp \
    src/world/format/anvil/*.cpp \
    src/nbt/*.cpp src/nbt/tag/*.cpp \
    src/logger/*.cpp \
    src/utils/*.cpp \
    src/math/Math.cpp \
    src/tile/*.cpp \
    src/world/loot/SimpleLoot.cpp \
    src/item/Item.cpp src/item/ItemNbt.cpp \
    src/inventory/BaseInventory.cpp \
    src/block/Block.cpp src/block/BlockRegistry.cpp 2>/dev/null)

REBUILD=0
for f in $SRCS; do
    [ "$f" -nt "$OUT" ] && REBUILD=1 && break
done

if [ "$REBUILD" = "1" ] || [ ! -x "$OUT" ]; then
    rm -f "$OUT" "$OUT".obj_*.o
    pids=()
    for f in $SRCS; do
        o="$OUT.obj_${f//\//_}.o"
        rm -f "$o"
        # The AVX2 kernels need the ISA enabled for their TU only (same
        # rule as build_local.sh and CMakeLists); everything else stays
        # generic so the scalar fallback keeps its baseline codegen.
        if [ "$f" = "src/world/performance/NoiseOptimizerAVX2.cpp" ]; then
            ( $CXX $STD $INC -mavx2 -mno-fma -c "$f" -o "$o" ) &
        else
            ( $CXX $STD $INC -c "$f" -o "$o" ) &
        fi
        pids+=($!)
        if [ ${#pids[@]} -ge 8 ]; then wait; pids=(); fi
    done
    wait
    $CXX -pthread -o "$OUT" "$OUT".obj_*.o -lm -lz
fi
exec "$OUT" "${1:-12345}" "${2:-6}" "${3:-1}"
