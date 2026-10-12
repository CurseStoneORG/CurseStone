#!/usr/bin/env bash
# run_nukkit_life_test.sh — compila y corre el test de regresion del
# reescrito de Vida/Daño/Muerte estilo Nukkit 1:1 (tools/nukkit_life_test.cpp).
#
# El test incluye los headers REALES del servidor (entity/Living.h) y
# linkea src/entity/Living.cpp + Entity.cpp, asi que falla si alguien
# cambia una formula, no si cambia un clon.
#
# Uso:   tools/run_nukkit_life_test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
OUT="${OUT:-/tmp/nukkit_life_test}"

OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT
for c in src/utils/MainLogger.c src/logger/AsyncMainLogger.c src/utils/Terminal.c \
         src/utils/Logger.c src/utils/TextFormat.c; do
    "$CC" -std=c2x -O0 -w -Isrc -c "$c" -o "$OBJDIR/$(basename "$c" .c).o"
done
"$CXX" -std=c++20 -O0 -g -Isrc -pthread -o "$OUT" \
    tools/nukkit_life_test.cpp \
    src/entity/Living.cpp \
    src/entity/Entity.cpp \
    src/event/EventManager.cpp \
    src/event/HandlerList.cpp \
    "$OBJDIR"/*.o -lz -ldl
"$OUT"
