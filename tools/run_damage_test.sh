#!/usr/bin/env bash
# run_damage_test.sh — compila y corre el test de regresion del sistema de
# dano de Genisys (tools/damage_test.cpp).
#
# El test incluye los headers REALES del servidor (event/entity/
# EntityDamageEvent.h y entity/damage/DamageCalculator.h) y linkea
# src/entity/Entity.cpp, asi que falla si alguien cambia una formula, no si
# cambia una copia.
#
# Uso:   tools/run_damage_test.sh
#        CXX=g++ tools/run_damage_test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/damage_test}"

# Entity.cpp -> EventManager -> MainLogger: se compilan los tres TUs del
# servidor que el test toca, nada mas. MainLogger.c es C puro.
CC="${CC:-gcc}"
OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT
for c in src/utils/MainLogger.c src/logger/AsyncMainLogger.c src/utils/Terminal.c \
         src/utils/Logger.c src/utils/TextFormat.c; do
    "$CC" -std=c2x -O0 -w -Isrc -c "$c" -o "$OBJDIR/$(basename "$c" .c).o"
done
"$CXX" -std=c++20 -O0 -g -Isrc -pthread -o "$OUT" \
    tools/damage_test.cpp \
    src/entity/Entity.cpp \
    src/event/EventManager.cpp \
    src/event/HandlerList.cpp \
    "$OBJDIR"/*.o -lz -ldl
"$OUT"
