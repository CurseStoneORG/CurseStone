#!/usr/bin/env bash
# run_nbt_test.sh — compila y corre el test de regresion del formato NBT
# binario (tools/nbt_test.cpp).
#
# Incluye los headers REALES de src/nbt/ y src/item/ItemNbt.h, y linkea las
# implementaciones reales (src/nbt/*.c, src/nbt/tag/*.c, src/item/ItemNbt.cpp),
# asi que falla si alguien cambia el layout en el cable, no si cambia una copia.
# El fixture de bytes esta escrito a mano a partir de la spec, no generado por
# nuestro writer: por eso el round-trip byte-a-byte prueba el formato de verdad.
#
# Uso:   tools/run_nbt_test.sh
#        CXX=clang++ CC=clang tools/run_nbt_test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
OUT="${OUT:-/tmp/nbt_test}"

OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT

# CompoundTag.c arrastra utils/Logger.h, y BinaryStream.c utils/Binary.h; los
# tres objetos .c que el test toca se compilan igual que en el resto del tree.
for c in src/nbt/NBT.c src/nbt/tag/*.c src/utils/BinaryStream.c src/utils/Binary.c \
         src/utils/Logger.c src/utils/Terminal.c src/utils/TextFormat.c; do
    "$CC" -std=c2x -O0 -w -Isrc -c "$c" -o "$OBJDIR/$(basename "$c" .c).o"
done

"$CXX" -std=c++20 -O0 -g -Isrc -o "$OUT" \
    tools/nbt_test.cpp \
    src/item/ItemNbt.cpp \
    "$OBJDIR"/*.o
"$OUT"
