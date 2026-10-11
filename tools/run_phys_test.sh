#!/usr/bin/env bash
# run_phys_test.sh — compila y corre el test de regresion de las primitivas
# de fisica portadas del cliente (tools/phys_test.cpp).
#
# El test incluye los headers REALES (math/AxisAlignedBB.h, math/HitResult.h,
# physics/Physics.h) y linkea el Physics.cpp + HitResult.c reales, asi que
# falla si alguien cambia una formula — no si cambia una copia. Es a proposito
# independiente de World/Entity: la fisica es geometria pura y se puede
# verificar sola (ver el comentario de cabecera de src/physics/Physics.h).
#
# Uso:   tools/run_phys_test.sh
#        CXX=clang++ tools/run_phys_test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
OUT="${OUT:-/tmp/phys_test}"

OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT

"$CC" -std=c2x -O0 -w -Isrc -c src/math/HitResult.c -o "$OBJDIR/HitResult.o"
"$CXX" -std=c++20 -O0 -g -Isrc -o "$OUT" \
    tools/phys_test.cpp \
    src/physics/Physics.cpp \
    "$OBJDIR/HitResult.o"
"$OUT"
