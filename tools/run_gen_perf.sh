#!/usr/bin/env bash
# run_gen_perf.sh — compila y corre el perfil de generacion de chunks.
# Mide cuánto cuesta un chunk con el generador Normal (ruido vanilla) y
# deja ver la mejora antes/después de optimizar.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/gen_perf}"

gens="src/world/generator/Normal.cpp src/world/generator/EmptyGenerator.cpp src/world/generator/Nether.cpp src/world/generator/Flat.cpp src/world/generator/GenerationTask.cpp"
noise="src/world/generator/noise/ImprovedNoise.cpp src/world/generator/noise/PerlinNoise.cpp src/world/generator/noise/Synth.cpp"
tshape="src/world/generator/TerrainShape.cpp"
pop="src/world/generator/populator/*.cpp"
biome="src/world/generator/biome/*.cpp"
carver="src/world/generator/carver/*.cpp"
obj="src/world/generator/object/*.cpp src/world/generator/object/trees/*.cpp"
struct="src/world/generator/structure/*.cpp src/world/generator/structure/village/*.cpp"

$CXX -std=c++20 -O2 -Isrc -o "$OUT" \
    tools/gen_perf.cpp $gens $noise $tshape $pop $biome $carver $obj $struct \
    src/world/format/*.c \
    src/world/format/anvil/*.c \
    src/nbt/*.c src/nbt/tag/*.c \
    src/logger/*.c \
    src/utils/*.c \
    src/math/Vector3.c \
    src/block/Block.cpp src/block/BlockRegistry.cpp \
    -lm -lpthread
"$OUT" "${1:-64}"
