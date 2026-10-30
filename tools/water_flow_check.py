#!/usr/bin/env python3
"""Vanilla liquid flow check for a local CurseStone server.

Drives a real RakNet client against a freshly booted flat-world server and
asserts the vanilla MCPE 1.0.0 liquid behavior:

  pour      a water bucket pour flows outward (id 8 with rising meta),
            then settles into still sources (id 9).
  replace   a solid block can be placed INTO a water cell (vanilla mayPlace).
  drain     removing the source drains the flow back to air.
  lava      lava meeting water solidifies to cobblestone/obsidian.
  regress   a plain block still places on plain grass.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

FLOWING_WATER, STILL_WATER = 8, 9
FLOWING_LAVA, STILL_LAVA = 10, 11
STONE, COBBLESTONE, OBSIDIAN = 1, 4, 49


def wait_boot(proc, server_log, deadline_s=40):
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        try:
            with open(server_log) as f:
                if "Server ready" in f.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.4)
    return False


def cells(events, bid=None):
    return [e for e in events if e[0] == "update_block" and (bid is None or e[4] == bid)]


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone vanilla liquid flow check")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-rel/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19138)
    ap.add_argument("--name", default="WaterTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_water_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=WaterFlow\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  debug-mode: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    try:
        if not wait_boot(proc, server_log):
            print("FAIL: server never became ready")
            return 1
        print("server ready")

        c = Client(args.host, args.port, args.name, verbose=args.verbose)
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        print(f"  eid={c.eid} spawn={c.spawn}")

        # Flat terrain: grass at y=3; tests run well away from spawn (128,128).
        pour_x, pour_z = 136, 128

        # ---------------- pour: bucket water flows and settles ----------------
        print("stage: water bucket pour")
        c.events.clear()
        c.use_item(pour_x, 3, pour_z, 1, item_id=326)
        c.drain(4.0)
        flowed = cells(c.events, FLOWING_WATER)
        settled = cells(c.events, STILL_WATER)
        source = [e for e in flowed if e[1] == pour_x and e[2] == 4 and e[3] == pour_z
                  and e[5] == 0]
        ring = {(e[1], e[3]) for e in flowed
                if e[2] == 4 and (abs(e[1] - pour_x) + abs(e[3] - pour_z)) == 1}
        settled_src = [e for e in settled if e[1] == pour_x and e[2] == 4
                       and e[3] == pour_z and e[5] == 0]
        check(len(source) >= 1, "source cell became flowing water meta 0")
        check(len(ring) >= 4, f"water spread to all 4 side cells (got {len(ring)})")
        check(len(settled_src) >= 1, "source settled into still water (id 9)")

        # ---------------- replace: place stone INTO a water cell ----------------
        print("stage: place block into water")
        c.events.clear()
        c.use_item(pour_x, 4, pour_z, 4, item_id=1)  # west neighbor holds water
        c.drain(2.0)
        placed = [e for e in cells(c.events, STONE)
                  if e[1] == pour_x - 1 and e[2] == 4 and e[3] == pour_z]
        check(len(placed) >= 1, "stone placed inside the water cell")

        # ---------------- drain: remove the source ----------------
        print("stage: drain after source removal")
        c.events.clear()
        c.remove_block(pour_x, 4, pour_z)
        c.drain(5.0)
        aired = {(e[1], e[2], e[3]) for e in cells(c.events, 0) if e[2] == 4}
        check(len(aired) >= 3, f"flowing water drained to air (got {len(aired)} cells)")

        # ---------------- lava meets water ----------------
        print("stage: lava meets water")
        lx, lz = 144, 128
        c.events.clear()
        c.use_item(lx, 3, lz, 1, item_id=326)  # water first, let it settle
        c.drain(4.0)
        c.events.clear()
        c.use_item(lx + 2, 3, lz, 1, item_id=327)  # lava two cells east
        c.drain(7.0)
        solid = [e for e in cells(c.events)
                 if e[4] in (COBBLESTONE, OBSIDIAN) and e[2] == 4
                 and lx - 1 <= e[1] <= lx + 3 and lz - 1 <= e[3] <= lz + 1]
        check(len(solid) >= 1,
              f"water+lava solidified (stone ids: {sorted({e[4] for e in solid})})")

        # ---------------- regression: plain placement ----------------
        print("stage: plain block placement")
        c.events.clear()
        c.use_item(152, 3, 128, 1, item_id=1)
        c.drain(1.5)
        plain = [e for e in cells(c.events, STONE)
                 if e[1] == 152 and e[2] == 4 and e[3] == 128]
        check(len(plain) >= 1, "stone still places on plain grass")

    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        time.sleep(1.0)
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print(f"WATER_CHECK_EXIT={1 if failures else 0}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
