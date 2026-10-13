#!/usr/bin/env bash
# run_async_logger_check.sh — builds and runs the async logger regression
# check for issue #8 (tools/async_logger_check.cpp): order, asynchronous
# flushing and loss-free shutdown after the logger left pthread behind.
#
# Usage:  tools/run_async_logger_check.sh
#         CC=clang CXX=clang++ tools/run_async_logger_check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/async_logger_check}"
EXTRA="${EXTRA:-}"
LIBS="${LIBS:--lpthread}"

OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT

# The logger sources are compiled as C, same as the real build. The two
# files owned by this issue get full warnings; legacy utils are silenced
# like in the other run_*.sh scripts.
for c in src/utils/ThreadPort.c src/logger/AsyncMainLogger.c; do
    "$CC" -std=c2x -O2 -Wall -Wextra -Isrc $EXTRA -c "$c" -o "$OBJDIR/$(basename "$c" .c).o"
done
for c in src/utils/Logger.c src/utils/Terminal.c src/utils/TextFormat.c; do
    "$CC" -std=c2x -O2 -w -Isrc $EXTRA -c "$c" -o "$OBJDIR/$(basename "$c" .c).o"
done

"$CXX" -std=c++20 -O2 -Wall -Wextra -Isrc $EXTRA -o "$OUT" \
    tools/async_logger_check.cpp \
    "$OBJDIR"/*.o \
    $LIBS

# Run outside the repo so the temporary log file never lands in the tree.
( cd "$OBJDIR" && "$OUT" )
