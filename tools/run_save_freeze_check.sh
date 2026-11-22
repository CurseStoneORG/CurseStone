#!/usr/bin/env bash
# run_save_freeze_check.sh — FASE L freeze reproduction harness.
#
# Builds tools/save_freeze_check.cpp against the FULL object set of a
# build_local.sh tree (every real TU — ChunkManager, the LevelDB engine,
# the vgen generator, tiles, network) minus the executable's own
# src_CurseStone.cpp.o (it defines main()). This is the same link recipe
# the CI bench workflow uses: the harness runs the exact code the server
# runs, not a subset.
#
# Uso:   OUT=/tmp/cs_build tools/run_save_freeze_check.sh [N_chunks] [seed]
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD="${BUILD:-/tmp/cs_build}"
OBJDIR="$BUILD/obj"
OUTBIN="${OUT:-/tmp/save_freeze_check}"
EXTRA="${CXX_EXTRA:--Isrc/world/generator}"
LIBS="${LIBS:--lz -ldl -pthread}"

if [ ! -d "$OBJDIR" ] || [ -z "$(ls "$OBJDIR"/*.o 2>/dev/null)" ]; then
    echo "no object tree at $OBJDIR — run: BUILD=$BUILD bash build_local.sh" >&2
    exit 1
fi

g++ -std=c++20 -O2 -w -Isrc $EXTRA -o "$OUTBIN" \
    tools/save_freeze_check.cpp \
    $(find "$OBJDIR" -name '*.o' | grep -v src_CurseStone.cpp.o) \
    $LIBS

"$OUTBIN" "${1:-100}" "${2:-1337}"
