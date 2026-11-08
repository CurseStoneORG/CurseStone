#!/usr/bin/env bash
# run_fire_flag_wire_check.sh — compila y corre el check de regresion del
# flag on-fire (el overlay de quema en el cliente 0.15.10). Linkea el
# SetEntityDataPacket REAL y el helper de pose flags, igual que el servidor.
#
# Uso:   tools/run_fire_flag_wire_check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/fire_flag_check}"

"$CXX" -std=c++20 -O2 -w -Isrc -o "$OUT" \
    tools/fire_flag_wire_check.cpp \
    src/network/protocol/SetEntityDataPacket.cpp \
    src/network/protocol/DataPacket.cpp \
    -lpthread

"$OUT"
