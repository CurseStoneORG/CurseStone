#!/usr/bin/env bash
# CurseStone launcher for Linux and Termux (Android).
cd "$(dirname "$0")" || exit 1

# Permissions first.
chmod +x "$0" ./CurseStone 2>/dev/null

BIN="./CurseStone"

if [ ! -f "$BIN" ]; then
  echo "[start] CurseStone binary not found in this folder"
  exit 1
fi

# Restart automatically after a crash; stop on a clean exit.
while true; do
  "$BIN"
  code=$?
  [ "$code" -eq 0 ] && break
  echo "[start] Server exited with code $code, restarting in 5s (Ctrl+C to cancel)..."
  sleep 5
done
