#!/usr/bin/env bash
# run_anvil_async_check.sh — builds and runs the regression harness for
# the async Anvil autosave pipeline (tools/anvil_async_check.cpp).
#
# Links the REAL implementations: Anvil (encode + background writer +
# region I/O), ChunkAdapter and the NBT codec, so the measured main-thread
# cost and the round-trip integrity cover the exact path the live server
# autosave uses, not a copy.
#
# Uso:   tools/run_anvil_async_check.sh
#        CXX=clang++ tools/run_anvil_async_check.sh
#        LIBS="-l:libz.so.1 -lpthread" tools/run_anvil_async_check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
OUT="${OUT:-/tmp/anvil_async_check}"
EXTRA="${CXX_EXTRA:-}"
LIBS="${LIBS:--lz -lpthread}"

OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT

# The .c files of the storage / NBT layer compile as C, same as the real
# build (Chunk.c's light helpers read the block tables Block.cpp fills;
# the harness never exercises light math, so it links the neutral stubs
# declared inside anvil_async_check.cpp).
for c in src/world/format/Chunk.c \
         src/world/format/ChunkSection.c \
         src/world/format/anvil/AnvilChunkCodec.c \
         src/nbt/NBT.c src/nbt/tag/*.c \
         src/utils/Binary.c src/utils/BinaryStream.c src/utils/Logger.c \
         src/utils/MainLogger.c src/utils/Terminal.c src/utils/TextFormat.c \
         src/utils/ThreadPort.c \
         src/logger/AsyncMainLogger.c; do
    "$CC" -std=c2x -O2 -w -Isrc $EXTRA -c "$c" -o "$OBJDIR/$(basename "$c" .c).o"
done

"$CXX" -std=c++20 -O2 -w -Isrc $EXTRA -o "$OUT" \
    tools/anvil_async_check.cpp \
    src/world/format/ChunkAdapter.cpp \
    src/world/format/WorldProviderManager.cpp \
    src/world/format/anvil/Anvil.cpp \
    src/world/format/anvil/AnvilRegionLoader.cpp \
    "$OBJDIR"/*.o \
    $LIBS

"$OUT"
