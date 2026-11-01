#!/usr/bin/env bash
# CurseStone | Open Source Server Software
# Copyright (C) 2026 CurseStoneTeam
# Licensed under the GNU Lesser General Public License v3.0 or later.
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# run_harness.sh — build the server + the bot, run the full session matrix
# against a live server and (when the client libraries are available)
# validate the capture through the real 0.15.10 client parser.
#
# Usage: test/bot/run_harness.sh [build-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-harness}"
WORK="$(mktemp -d)"
trap 'kill ${SRV_PID:-0} 2>/dev/null || true' EXIT

echo "== build =="
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S "$ROOT" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target CurseStone cursestone-bot -j"$(nproc)"

BIN="$BUILD/CurseStone"
BOT="$BUILD/cursestone-bot"
[ -f "$BIN" ] && [ -f "$BOT" ] || { echo "build failed"; exit 1; }

echo "== start server =="
mkdir -p "$WORK/run" && cd "$WORK/run"
cp "$BIN" .
./CurseStone > server.log 2>&1 &
SRV_PID=$!
sleep 4
grep -q "Network bound" server.log || { echo "server did not bind"; tail -5 server.log; exit 1; }

FAILED=0
expect() {
    local name="$1"; shift
    echo "== scenario: $name =="
    if "$@" -v; then
        echo "[$name] PASS"
    else
        echo "[$name] FAIL"
        FAILED=1
    fi
}

expect login  "$BOT" --scenario login     --duration 12000
expect chunks "$BOT" --scenario chunks    --duration 30000 --capture "$WORK/capture.jsonl"
expect move   "$BOT" --scenario move      --duration 12000
expect chat   "$BOT" --scenario chat      --duration 10000
expect malformed "$BOT" --scenario malformed --duration 25000

echo "== differential validation =="
if python3 "$ROOT/test/bot/validate_capture.py" --capture "$WORK/capture.jsonl"; then
    echo "[validate] PASS"
else
    echo "[validate] FAIL (a packet would crash a real client)"
    FAILED=1
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "harness: ALL PASS"
else
    echo "harness: FAILURES"
    tail -30 server.log || true
fi
exit $FAILED
