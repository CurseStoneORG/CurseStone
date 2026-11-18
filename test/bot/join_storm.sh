#!/usr/bin/env bash
# CurseStone | Open Source Server Software
# Copyright (C) 2026 CurseStoneORG
# Licensed under the GNU Lesser General Public License v3.0 or later.
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# join_storm.sh — point N protocol-84 bots at one server at the same time
# and report who logged in, who spawned and how many chunks each received.
# The harness that produced the "single-streamer" observation in
# docs/THREADING.md; with per-username bot identities (Bot.cpp) every
# mode below must show every bot reaching its full 289-chunk view.
#
# Usage: join_storm.sh [N_BOTS] [DURATION_MS] [MODE]
#   MODE simultaneous (default) — all bots connect at the same instant
#        staggered          — one scout marches out first, the wave joins
#                             12 s later, mid-stream of the scout
#        march              — every bot marches to its own destination on
#                             a 500-block ring (disjoint chunk regions)
# Env: SRV_BIN, BOT_BIN, SEED — binaries and world seed to use.
set -u

N="${1:-8}"
DUR="${2:-45000}"
MODE="${3:-simultaneous}"
SRV_BIN="${SRV_BIN:-$ROOT/build/CurseStone}"
BOT_BIN="${BOT_BIN:-$ROOT/build/cursestone-bot}"
SEED="${SEED:-12345}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

case "$MODE" in
simultaneous | staggered | march) ;;
*)
    echo "unknown mode: $MODE (simultaneous | staggered | march)" >&2
    exit 2
    ;;
esac

WORK="$(mktemp -d)"
trap 'kill ${SRV_PID:-0} 2>/dev/null || true' EXIT
cd "$WORK" || exit 1
printf 'world-seed=%s\n' "$SEED" > server.properties
cp "$SRV_BIN" ./CurseStone
[ -x ./CurseStone ] && [ -x "$BOT_BIN" ] || {
    echo "missing server or bot binary (SRV_BIN / BOT_BIN)" >&2
    exit 1
}

mkfifo console.in
./CurseStone < console.in > server.log 2>&1 &
SRV_PID=$!
exec 3>console.in # hold the FIFO open so the console reader sees no EOF

BOUND=0
for _ in $(seq 1 80); do
    if grep -q "Network bound" server.log 2>/dev/null; then BOUND=1; break; fi
    kill -0 "$SRV_PID" 2>/dev/null || break
    sleep 0.25
done
[ "$BOUND" -eq 1 ] || {
    echo "server did not bind:" >&2
    tail -5 server.log >&2
    exit 1
}

# Console status sampler — TPS/MSPT land in server.log for the report.
(
    while kill -0 "$SRV_PID" 2>/dev/null; do
        echo "status" >&3
        sleep 5
    done
) & SAMPLER_PID=$!

# Launch one bot; stdout+stderr into its own log (verbose so the
# login/spawn timeline can be cut from the marked lines). Bots are
# waited BY PID — a bare `wait` would also wait the console sampler,
# which never exits while the server lives.
BOT_PIDS=()
run_bot() {
    local name="$1" dur="$2" goto="$3"
    if [ -n "$goto" ]; then
        "$BOT_BIN" --scenario move --duration "$dur" --goto "$goto" \
            --username "$name" -v > "bots/$name.log" 2>&1 &
    else
        "$BOT_BIN" --scenario chunks --duration "$dur" \
            --username "$name" -v > "bots/$name.log" 2>&1 &
    fi
    BOT_PIDS+=("$!")
}

mkdir -p bots
case "$MODE" in
simultaneous)
    for i in $(seq 1 "$N"); do run_bot "Storm$i" "$DUR" ""; done ;;
staggered)
    run_bot "Scout" "$DUR" "400,400"
    sleep 12 # let the scout get mid-march before the wave hits
    for i in $(seq 1 "$N"); do run_bot "Late$i" "$DUR" ""; done ;;
march)
    for i in $(seq 1 "$N"); do
        goto=$(awk -v i="$i" 'BEGIN{printf "%d,%d", 500*cos(i*3.14159265/8), 500*sin(i*3.14159265/8)}')
        run_bot "March$i" "$DUR" "$goto"
    done ;;
esac

for pid in "${BOT_PIDS[@]}"; do wait "$pid"; done
echo "all bots done"
kill "$SAMPLER_PID" 2>/dev/null
echo "stop" >&3
sleep 2
exec 3>&-

echo "================ PER-BOT RESULTS ================"
for f in bots/*.log; do
    name=$(basename "$f" .log)
    echo "$name: $(grep -o 'state=.*' "$f" | tail -1) $(grep -o 'result: .*' "$f" | tail -1)"
done
echo "================ DUPLICATE-IDENTITY KICKS (must be 0) ================"
grep -c 'logged in from another location' server.log
echo "================ SERVER TPS/MSPT SAMPLES ================"
grep -E 'TPS|Tick:' server.log | head -20
