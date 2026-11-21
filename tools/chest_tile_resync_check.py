#!/usr/bin/env python3
"""Post-spawn chest tile resync check for CurseStone.

The bug this locks down: the chest tiles of the chunks that stream while
the 0.15 loading screen is up reach a client that cannot bind block
actors yet, and a normal session never re-streams those chunks — the
used-chunk marker stays set and the spawn acknowledgment the server
waited for does not exist in the 0.15.10 protocol (the era reference
never handles an inbound PLAY_STATUS PLAYER_SPAWN). The chest then
renders as an invisible block: solid, walkable, breakable (its loot
drops), but unopenable — exactly the reported symptom.

The server now arms a tile resync at doFirstSpawn that fires one tick
later, re-sending the BlockEntityData of every tile in every chunk the
player already tracks — AFTER the client is in-world. This check drives
the real protocol-84 wire with a probe plugin that plants a chest on the
spawn column before any client exists, then asserts:

  1. the join reaches spawn and the probe planted (readback id 54)
  2. the chest's BlockEntityData is announced on the wire (the stream
     announcement born from the block-scan hydration)
  3. THE RESYNC: every chest position is announced at least TWICE — the
     second announcement is the post-spawn resync re-delivery
  4. the re-delivery arrives with NO chunk rebuild around it (the chest
     chunk is streamed exactly once) — the client's block actor binds
     against terrain it already holds, which is what makes the tile land
  5. every announcement rode RELIABLE_ORDERED on the chunk channel (2),
     so a tile can never overtake its chunk

    python3 tools/chest_tile_resync_check.py [--server /path/to/CurseStone]
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chest_stream_gap_check import RAK_RELIABLE_ORDERED, BLOCK_ENTITY_DATA, GapClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

PLAYER_NAME = "ResyncBot"
CHEST_ID = 54
PLANT_MARKER = "readback=54"
# Longest wait for the resync wave after spawn (it fires one server tick
# after doFirstSpawn; even a loaded box answers in well under a second).
RESYNC_WINDOW_S = 8.0

PLUGIN_YML = ("name: JoinChestProbe\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")

PLUGIN_MAIN_LUA = '''-- JoinChestProbe: plants one chest (id 54, facing 2) on the spawn
-- column at boot, before any client can exist, so the chest's chunk
-- streams with its tile during the loading screen — the exact delivery
-- window the resync must heal. Idempotent across restarts: when the
-- previous boot's chest still stands, it is reused as-is. The readback
-- line is what the harness waits for.
return {
  onEnable = function()
    local w = server.defaultWorld()
    if not w then
      log.error("JoinChestProbe no-world")
      return false
    end
    local sx, sy, sz = w:getSpawn()
    if not sx then
      log.error("JoinChestProbe no-spawn")
      return false
    end
    local y = sy + 16
    if y > 120 then y = 120 end
    while y > 1 and w:getBlock(sx, y, sz) == 0 do y = y - 1 end
    if w:getBlock(sx, y, sz) == 54 then
      log.info(string.format("chest already present %d/%d/%d readback=54", sx, y, sz))
      return true
    end
    w:setBlock(sx, y + 1, sz, 54, 2)
    log.info(string.format("planted chest %d/%d/%d readback=%d",
        sx, y + 1, sz, w:getBlock(sx, y + 1, sz)))
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


def boot_and_assert(proc_ref, runtime, args, env, server_log, stderr_log, console, check,
                    stage_label, need_plant_marker):
    """Boots the server (or reuses the running one), drives one join and
    asserts the resync guarantees. Returns the GapClient or None."""
    if need_plant_marker and not wait_for(server_log, PLANT_MARKER, 5.0, proc_ref[0]):
        check(False, f"[{stage_label}] probe chest ready before any client")
        return None

    a = GapClient(args.host, args.port, PLAYER_NAME)
    print(f"stage: {stage_label}")
    joined = a.handshake() and a.login() and a.eid != 0
    check(joined, f"[{stage_label}] join reached spawn")
    if not joined:
        return None
    a.drain(2.0)
    a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
    # The resync fires one tick after doFirstSpawn; this window also
    # swallows every batch the still-running spawn backlog drains.
    a.drain(RESYNC_WINDOW_S)

    chest_tiles = [(x, y, z) for x, y, z, tag in a.tiles
                   if isinstance(tag, dict) and tag.get("id") == "Chest"]
    check(len(chest_tiles) >= 1,
          f"[{stage_label}] the chest's BlockEntityData reached the wire",
          f"chest tiles seen: {len(chest_tiles)}")
    if not chest_tiles:
        return None

    from collections import Counter
    counts = Counter(chest_tiles)
    resynced = {pos: n for pos, n in counts.items() if n >= 2}
    check(len(resynced) >= 1,
          f"[{stage_label}] post-spawn resync re-delivered the chest tile (>=2 announcements)",
          f"announcement counts: {dict(counts)}")

    # The re-delivery must NOT ride a chunk rebuild: a chunk payload
    # arriving after the tile would wipe the block actor it just bound.
    chest_cx = (chest_tiles[0][0]) >> 4
    chest_cz = (chest_tiles[0][2]) >> 4
    chunk_sends = sum(1 for ev in a.wire_events
                      if ev[0] == "chunk" and ev[1] == chest_cx and ev[2] == chest_cz)
    check(chunk_sends == 1,
          f"[{stage_label}] chest chunk streamed exactly once (no wipe after the resync)",
          f"FullChunkData count for ({chest_cx},{chest_cz}): {chunk_sends}")

    # Reliability/channel of every chest tile announcement. stream_meta
    # records (pid, reliability, channel) per sub-packet in the same
    # order on_game_packet records the tile events, so the i-th 0x32
    # meta entry belongs to the i-th tile announcement.
    tile_metas = [m for m in a.stream_meta if m[0] == BLOCK_ENTITY_DATA]
    tile_events = [ev for ev in a.wire_events if ev[0] == "tile"]
    paired = list(zip(tile_metas, tile_events))
    chest_metas = []
    for m, ev in paired:
        x, y, z = ev[1], ev[2], ev[3]
        if (x, y, z) in counts:
            chest_metas.append(m)
    ordered = all(rel == RAK_RELIABLE_ORDERED and ch == 2
                  for _pid, rel, ch in chest_metas)
    check(ordered and len(chest_metas) >= 1,
          f"[{stage_label}] every chest tile announcement rode RELIABLE_ORDERED channel 2",
          f"entries: {chest_metas}")
    return a


def start_server(runtime, args, env, server_log, stderr_log):
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL,
                            stderr=open(stderr_log, "a"))

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    def boot():
        # "Server ready" from a PREVIOUS boot may still sit in the log;
        # only a marker written after this launch counts.
        try:
            offset = os.path.getsize(server_log)
        except FileNotFoundError:
            offset = 0
        deadline = time.time() + 120
        while time.time() < deadline:
            if proc.poll() is not None:
                return False
            text = read_log(server_log)[offset:]
            if "Server ready" in text:
                return True
            time.sleep(0.4)
        return False

    return proc, console, boot

def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chest tile resync check")
    ap.add_argument("--server", default="/home/z/my-project/build-chestfix/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19187)
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_chest_resync_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ChestResync\ngamemode=1\nworld-seed=20261122\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    plugins = os.path.join(runtime, "plugins", "JoinChestProbe")
    os.makedirs(os.path.join(plugins, "src"), exist_ok=True)
    with open(os.path.join(plugins, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(plugins, "src", "main.lua"), "w") as f:
        f.write(PLUGIN_MAIN_LUA)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(os.path.abspath(args.server))
    server_log = os.path.join(runtime, "server.log")
    stderr_log = os.path.join(runtime, "server-stderr.log")

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    proc = None
    try:
        # ---------- stage 1: fresh world, the chest streams pre-spawn ----
        proc, console, boot = start_server(runtime, args, env, server_log, stderr_log)
        if not boot():
            print("FAIL: server never became ready (or died during boot)")
            return 1
        a = boot_and_assert([proc], runtime, args, env, server_log, stderr_log,
                            console, check, "fresh join", need_plant_marker=True)
        if a is None:
            print("FAIL: cannot proceed without a stage-1 session")
            return 1

        # ---------- save + graceful stop ---------------------------------
        console("stop")
        try:
            rc = proc.wait(timeout=25)
            check(rc == 0, "[save+stop] graceful shutdown exit code 0", f"rc={rc}")
        except subprocess.TimeoutExpired:
            proc.kill()
            check(False, "[save+stop] graceful shutdown exit code 0", "timed out")
        proc = None

        # ---------- stage 2: restart, the chest hydrates from the .mca ---
        proc, console, boot = start_server(runtime, args, env, server_log, stderr_log)
        if not boot():
            print("FAIL: restarted server never became ready")
            return 1
        b = boot_and_assert([proc], runtime, args, env, server_log, stderr_log,
                            console, check, "restart rejoin", need_plant_marker=False)

        alive = proc is not None and proc.poll() is None
        check(alive, "server survived both sessions")
        crash_text = read_log(stderr_log)
        check("SIGSEGV" not in crash_text and "CurseStone crash" not in crash_text,
              "no crash banner in the server stderr")

        if proc is not None:
            console("stop")
            try:
                rc = proc.wait(timeout=25)
                check(rc == 0, "[final] graceful shutdown exit code 0", f"rc={rc}")
            except subprocess.TimeoutExpired:
                proc.kill()
                check(False, "[final] graceful shutdown exit code 0", "timed out")
            proc = None
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()

    if failures:
        print(f"FAIL: {len(failures)} assertion(s) failed")
        return 1
    print("OK: post-spawn tile resync re-delivers chest block actors on fresh and "
          "restarted worlds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
