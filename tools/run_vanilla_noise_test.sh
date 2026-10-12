#!/usr/bin/env bash
# run_vanilla_noise_test.sh — compila y corre la prueba de oro del ruido
# vanilla (tools/vanilla_noise_test.cpp).
#
# Compila las implementaciones REALES de src/world/generator/noise/vanilla/ y
# las compara contra los valores volcados del propio SSMGAlt/PocketEdition para
# la semilla 1337. Si alguien toca el MT19937, la tabla de permutacion o la
# suma de octavas, el test falla aqui y no en el mundo.
#
# Uso:   tools/run_vanilla_noise_test.sh
#        CXX=clang++ tools/run_vanilla_noise_test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/vanilla_noise_test}"

"$CXX" -std=c++20 -O2 -Isrc -o "$OUT" \
    tools/vanilla_noise_test.cpp \
    src/world/generator/noise/*.cpp
"$OUT"
