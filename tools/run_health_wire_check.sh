#!/usr/bin/env bash
# run_health_wire_check.sh — bounded end-to-end check of the health / death /
# respawn packets a real client receives.
#
# Starts a throwaway CurseStone server in a temp data dir, runs
# tools/health_wire_check.py (a real RakNet protocol-84 client) against it,
# and always kills the server. The whole run is capped so it can never
# outlive the calling command.
#
# Uso:   tools/run_health_wire_check.sh
#        BIN=/path/to/CurseStone tools/run_health_wire_check.sh
set -uo pipefail
cd "$(dirname "$0")/.."

BIN="${BIN:-/tmp/cs_build/CurseStone}"
if [ ! -x "$BIN" ]; then
    echo "missing $BIN — build it first: ./build_local.sh" >&2
    exit 2
fi

# Throwaway world/config so nothing is written into the repo.
DATA="$(mktemp -d)"
SRV_PID=""
cleanup() {
    if [ -n "$SRV_PID" ] && kill -0 "$SRV_PID" 2>/dev/null; then
        kill "$SRV_PID" 2>/dev/null
        wait "$SRV_PID" 2>/dev/null
    fi
    rm -rf "$DATA"
}
trap cleanup EXIT INT TERM

"$BIN" --data "$DATA" >"$DATA/server.log" 2>&1 &
SRV_PID=$!

# Wait for the UDP socket to be bound (Server logs "Network bound to ...").
for _ in $(seq 1 80); do
    if grep -q "Network bound to" "$DATA/server.log" 2>/dev/null; then break; fi
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "server died during startup:" >&2
        tail -30 "$DATA/server.log" >&2
        exit 3
    fi
    sleep 0.25
done

python3 tools/health_wire_check.py "$@"
rc=$?

echo "--- server log (health/death related) ---"
grep -iE "respawn|died|health|\[COMBAT\]" "$DATA/server.log" | tail -25
exit $rc
