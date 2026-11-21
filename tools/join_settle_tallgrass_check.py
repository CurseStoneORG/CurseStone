#!/usr/bin/env python3
"""Join-settle tall grass check — reproduces the reported join crash.

The vanilla generator decorates plains and forest spawn areas with tall
grass (block id 31), but the block registry never carried a flyweight
for it, so Block::get(31) handed back null and a saved join spot inside
the grass dereferenced it bare in Player::settleJoinPosition() — the
SIGSEGV right after "Player ... logged in" (join settle runs inside
doFirstSpawn on every join).

This check drives the real wire path: a fake client joins, a Lua probe
plugin plants tall grass on the spawn column (the standing cell + the
cell above it) and logs the readback, the client's session dies (stop
keepalive -> the server reaps it after its 5s silence timeout and saves
the player), and a second client with the same identity rejoins — the
exact flow that used to end in "=== CurseStone crash === SIGSEGV".
Exit 0 means the server survived the rejoin with the grass still in the
settle path.

Usage:
    python3 tools/join_settle_tallgrass_check.py [--server BIN] [--port P]
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

TALL_GRASS_ID = 31
PLAYER_NAME = "SettleBot"
PLANT_MARKER = "readback=31/31"

PLUGIN_YML = ("name: JoinSettleProbe\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")

PLUGIN_MAIN_LUA = '''-- JoinSettleProbe: plants tall grass (id 31 — an id the registry must
-- answer for) on the spawn column the moment the first player joins,
-- then logs the readback for the harness to assert on. The saved join
-- spot is the cell the player's feet settled into, one above the first
-- solid ground, so both candidate cells get covered.
local planted = false

return {
  onEnable = function()
    events.on("PlayerJoin", function(e)
      if planted then return end
      local w = server.defaultWorld()
      if not w then
        log.error("JoinSettle no-world")
        return
      end
      local sx, sy, sz = w:getSpawn()
      if not sx then
        log.error("JoinSettle no-spawn")
        return
      end
      local y = sy + 16
      if y > 250 then y = 250 end
      while y > 1 and w:getBlock(sx, y, sz) == 0 do y = y - 1 end
      w:setBlock(sx, y + 1, sz, 31)
      w:setBlock(sx, y + 2, sz, 31)
      w:save()
      planted = true
      log.info(string.format(
          "planted tallgrass %d/%d/%d and %d/%d/%d readback=%d/%d",
          sx, y + 1, sz, sx, y + 2, sz,
          w:getBlock(sx, y + 1, sz), w:getBlock(sx, y + 2, sz)))
    end)
    return true
  end
}
'''


def read_log(path: str) -> str:
    try:
        with open(path) as f:
            return f.read()
    except FileNotFoundError:
        return ""


def wait_for(server_log: str, needle: str, seconds: float, proc=None) -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False  # server died while we were waiting
        if needle in read_log(server_log):
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone join-settle tall grass check")
    ap.add_argument("--server", default="/home/z/my-project/build-v103/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19179)
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_join_settle_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=JoinSettle\ngamemode=1\nworld-seed=20260501\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    plugins = os.path.join(runtime, "plugins", "JoinSettleProbe")
    os.makedirs(os.path.join(plugins, "src"), exist_ok=True)
    with open(os.path.join(plugins, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(plugins, "src", "main.lua"), "w") as f:
        f.write(PLUGIN_MAIN_LUA)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(os.path.abspath(args.server))
    server_log = os.path.join(runtime, "server.log")
    stderr_log = os.path.join(runtime, "server-stderr.log")
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL,
                            stderr=open(stderr_log, "w"))

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    def boot_server():
        deadline = time.time() + 120
        while time.time() < deadline:
            if proc.poll() is not None:
                return False
            if "Server ready" in read_log(server_log):
                return True
            time.sleep(0.4)
        return False

    try:
        if not boot_server():
            print("FAIL: server never became ready (or died during boot)")
            return 1

        # ---- first join: spawn, then let the probe plant the grass ----
        a = Client(args.host, args.port, PLAYER_NAME)
        print("stage: first join (spawn + probe plant)")
        if not a.handshake() or not a.login() or a.eid == 0:
            print("FAIL: first join never reached spawn")
            return 1
        a.drain(3.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(2.0)
        if a.spawn:
            print(f"  spawn = {a.spawn}")

        check(wait_for(server_log, PLANT_MARKER, 10.0, proc),
              "probe plugin planted tall grass (id 31) on the spawn column")
        updates = [ev for ev in a.events
                   if ev[0] == "update_block" and ev[4] == TALL_GRASS_ID]
        check(len(updates) >= 2, "both plant cell updates reached the client",
              f"saw {len(updates)}")
        if failures:
            print("FAIL: cannot proceed without the planted probe")
            return 1

        # ---- die like a crashed client: silence, then server-side reap ----
        a.stop_keepalive()
        print("stage: session death (silence timeout + player save)")
        reaped = (wait_for(server_log, PLAYER_NAME + " left the game", 16.0, proc)
                  or "Session closed" in read_log(stderr_log))
        check(reaped, "first session reaped with its position saved")

        # ---- the rejoin: settle walks the grass cell ----
        b = Client(args.host, args.port, PLAYER_NAME)
        print("stage: rejoin onto the tall grass cell")
        joined = b.handshake() and b.login() and b.eid != 0
        if joined:
            b.drain(3.0)
            b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
            b.drain(2.0)
        check(joined, "rejoin reached spawn (settle walked the grass cell)")

        alive = proc.poll() is None
        check(alive, "server survived the rejoin (no SIGSEGV)")
        spawns = read_log(server_log).count("First spawn for " + PLAYER_NAME)
        check(spawns >= 2, "two first-spawns logged", f"count={spawns}")
        crash_text = read_log(stderr_log)
        check("SIGSEGV" not in crash_text and "CurseStone crash" not in crash_text,
              "no crash banner in the server stderr")

        console("stop")
        try:
            rc = proc.wait(timeout=20)
            check(rc == 0, "graceful shutdown exit code 0", f"rc={rc}")
        except subprocess.TimeoutExpired:
            proc.kill()
            check(False, "graceful shutdown exit code 0", "timed out")
    finally:
        if proc.poll() is None:
            proc.kill()

    if failures:
        print(f"FAIL: {len(failures)} assertion(s) failed")
        return 1
    print("OK: rejoin onto a tall grass cell settles without a crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
