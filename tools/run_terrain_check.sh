#!/usr/bin/env bash
# Arranca CurseStone en un directorio limpio, corre tools/terrain_check.py
# y lo apaga SIEMPRE (daemon en su propio session, wait, kill).
#
#   ./tools/run_terrain_check.sh [segundos]
#
# Variables de entorno:
#   CURSESTONE_BIN  ruta al binario. Si no se define, se buscan los
#                   directorios de build habituales de CMake.
#   ACK             cuantos PlayStatus(PLAYER_SPAWN) mandar (0 = ninguno)
#   ACKEVERY        segundos entre ACKs (0 = todos de una)
#   CS_RUN_DIR      directorio del mundo de prueba
set -uo pipefail
cd "$(dirname "$0")/.."

SECS="${1:-12}"

if [ -z "${CURSESTONE_BIN:-}" ]; then
  for cand in build/CurseStone build/Release/CurseStone build/out/CurseStone \
              ./CurseStone /tmp/cs_build/CurseStone; do
    if [ -x "$cand" ]; then CURSESTONE_BIN="$cand"; break; fi
  done
fi
if [ -z "${CURSESTONE_BIN:-}" ] || [ ! -x "$CURSESTONE_BIN" ]; then
  echo "[run] no encuentro el binario de CurseStone."
  echo "       Compilalo (ver BUILDING.md) o exporta CURSESTONE_BIN=/ruta/CurseStone"
  exit 1
fi
echo "[run] binario: $CURSESTONE_BIN"

RUN="${CS_RUN_DIR:-/tmp/cs_run}"
mkdir -p "$RUN"

if [ -f "$RUN/server.pid" ] && kill -0 "$(cat "$RUN/server.pid")" 2>/dev/null; then
  kill "$(cat "$RUN/server.pid")" 2>/dev/null || true
  sleep 1
fi

# El primer arranque genera el mundo; despues reutiliza el disco.
if [ ! -d "$RUN/worlds" ]; then
  echo "[run] generando mundo por primera vez (puede tardar)..."
fi

setsid "$CURSESTONE_BIN" --data "$RUN" > "$RUN/stdout.log" 2>&1 < /dev/null &
SRV=$!
echo "$SRV" > "$RUN/server.pid"
trap 'kill -TERM -$SRV 2>/dev/null; kill -TERM $SRV 2>/dev/null; true' EXIT

# Espera a que el puerto UDP responda al ping de RakNet.
for i in $(seq 1 60); do
  if grep -q "Server started\|Listening\|Server IP" "$RUN/stdout.log" 2>/dev/null; then break; fi
  if ! kill -0 "$SRV" 2>/dev/null; then echo "[run] el servidor murio:"; tail -30 "$RUN/stdout.log"; exit 1; fi
  sleep 1
done
sleep 2

python3 tools/terrain_check.py --seconds "$SECS" --port 19132 --ack "${ACK:-1}" --ack-every "${ACKEVERY:-0}"
RC=$?

echo "--- ultimas lineas del servidor ---"
tail -15 "$RUN/stdout.log"
exit $RC