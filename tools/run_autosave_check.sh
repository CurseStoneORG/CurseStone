#!/usr/bin/env bash
# run_autosave_check.sh — compila y corre el test de regresion del
# autosave incremental (tools/autosave_check.cpp).
#
# Linkea las implementaciones REALES: ChunkManager, el codec Anvil
# (.mca + NBT + zlib) y el generador Normal completo, asi que el test
# mide/cubre el mismo camino que el autosave del servidor y no una copia.
#
# El generador de verdad es lento (mide el terreno con el ruido vanilla),
# asi que el test usa una rejilla de 3x3 = 9 chunks: suficiente para ver
# el presupuesto del flush y el ir y volver por disco sin tardar minutos.
#
# Uso:   tools/run_autosave_check.sh
#        CXX=clang++ tools/run_autosave_check.sh
#        CXX_EXTRA="-I/path/a/zlib" LIBS="-l:libz.so.1 -lpthread" \
#          tools/run_autosave_check.sh
#          (para sandboxes donde zlib no esta instalada con su .so de
#           desarrollo: el .mca necesita zlib de verdad, no hay copia)
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
OUT="${OUT:-/tmp/autosave_check}"
EXTRA="${CXX_EXTRA:-}"
LIBS="${LIBS:--lz -lpthread}"

BUILD="${BUILD:-/tmp/cs_build}"
OBJDIR="$BUILD/obj"
if [ ! -d "$OBJDIR" ] || [ -z "$(ls "$OBJDIR"/*.o 2>/dev/null)" ]; then
    echo "no object tree at $OBJDIR — run: BUILD=$BUILD bash build_local.sh" >&2
    exit 1
fi

g++ -std=c++20 -O2 -w -Isrc -Isrc/world/generator -o "$OUT" \
    tools/autosave_check.cpp \
    $(find "$OBJDIR" -name '*.o' | grep -v src_CurseStone.cpp.o) \
    -lz -ldl -pthread

"$OUT" \
    tools/autosave_check.cpp \
    src/world/ChunkManager.cpp \
    src/world/format/ChunkAdapter.cpp \
    src/world/format/WorldProviderManager.cpp \
    src/world/format/anvil/Anvil.cpp \
    src/world/format/anvil/AnvilRegionLoader.cpp \
    src/world/generator/*.cpp \
    src/world/generator/*/*.cpp \
    src/world/generator/*/*/*.cpp \
    src/block/Block.cpp \
    "$OBJDIR"/*.o \
    $LIBS

"$OUT"
