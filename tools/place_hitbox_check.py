#!/usr/bin/env python3
"""Block placement vs entity hitbox check for a local CurseStone server.

Drives a real RakNet client against a freshly booted flat-world server and
asserts the vanilla placement rule: a block whose collision volume would
intersect a player's 0.6x1.8 hitbox is refused, while blocks without a
collision volume still place.

  feet     a stone targeting the player's feet cell is refused.
  head     a stone targeting the player's head cell is refused.
  above    the first cell above the hitbox still accepts a block.
  beside   a cell one step to the side still accepts a block.
  torch    a torch (no collision volume) still places at the feet cell.
  pour     a water bucket still pours onto the pourer's own cell and an
           empty bucket scoops it back.
  other    a second online player's feet cell refuses foreign placement.
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

MOVE_PLAYER_PACKET = 0x10
STONE, TORCH = 1, 50
FLOWING_WATER, STILL_WATER = 8, 9
EYE_HEIGHT = 1.62


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


def move_player(c, x, y, z):
    """Position the player: (x, y, z) are feet coordinates."""
    body = (bytes([MOVE_PLAYER_PACKET]) +
            struct.pack(">q", c.eid) +
            struct.pack(">fff", x, y + EYE_HEIGHT, z) +
            struct.pack(">fff", 0.0, 0.0, 0.0) +
            bytes([0, 1]))
    c.send_game(body)


def placed(events, x, y, z, bid=None):
    return [e for e in events
            if e[0] == "update_block" and e[1] == x and e[2] == y and e[3] == z
            and (bid is None or e[4] == bid)]


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone placement hitbox check")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-rel/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19140)
    ap.add_argument("--name", default="HitboxTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_hitbox_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=Hitbox\nview-distance=4\ngamemode=0\n"
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

        # Stand the player exactly at (136.5, 4.0, 128.5): feet cell
        # (136,4,128), head cell (136,5,128). Flat ground is grass at y=3.
        move_player(c, 136.5, 4.0, 128.5)
        c.drain(1.0)

        # ---------------- feet: stone into the feet cell ----------------
        print("stage: feet cell refuses stone")
        c.events.clear()
        c.use_item(136, 3, 128, 1, item_id=STONE)  # top face of the ground below
        c.drain(2.0)
        check(len(placed(c.events, 136, 4, 128, STONE)) == 0,
              "stone refused in the player's feet cell")

        # ---------------- build a pillar next to the player ----------------
        print("stage: pillar build up")
        for ty in (4, 5, 6):
            c.events.clear()
            c.use_item(137, ty - 1, 128, 1, item_id=STONE)
            c.drain(1.5)
            check(len(placed(c.events, 137, ty, 128, STONE)) == 1,
                  f"pillar stone placed at (137,{ty},128)")

        # ---------------- above: first cell above the hitbox ----------------
        print("stage: cell above the hitbox accepts")
        c.events.clear()
        c.use_item(137, 6, 128, 4, item_id=STONE)  # west face toward the player
        c.drain(2.0)
        check(len(placed(c.events, 136, 6, 128, STONE)) == 1,
              "stone accepted at (136,6,128), above the 1.8-tall hitbox")

        # ---------------- head: stone into the head cell ----------------
        print("stage: head cell refuses stone")
        c.events.clear()
        c.use_item(136, 6, 128, 0, item_id=STONE)  # bottom face down into the head
        c.drain(2.0)
        check(len(placed(c.events, 136, 5, 128, STONE)) == 0,
              "stone refused in the player's head cell")

        # ---------------- beside: a free cell at feet height ----------------
        print("stage: side cell accepts")
        c.events.clear()
        c.use_item(138, 3, 128, 1, item_id=STONE)
        c.drain(2.0)
        check(len(placed(c.events, 138, 4, 128, STONE)) == 1,
              "stone accepted one cell to the side")

        # ---------------- torch: no collision volume, feet cell ----------------
        print("stage: torch places at the feet cell")
        c.events.clear()
        c.use_item(136, 3, 128, 1, item_id=TORCH)
        c.drain(2.0)
        check(len(placed(c.events, 136, 4, 128, TORCH)) == 1,
              "torch accepted in the player's own cell (no collision volume)")
        c.remove_block(136, 4, 128)
        c.drain(1.0)

        # ---------------- pour: water bucket onto the pourer ----------------
        print("stage: water bucket pour onto own cell")
        c.events.clear()
        c.use_item(136, 3, 128, 1, item_id=325, damage=8)
        c.drain(4.0)
        water = placed(c.events, 136, 4, 128, FLOWING_WATER) + \
            placed(c.events, 136, 4, 128, STILL_WATER)
        check(len(water) >= 1, "water accepted in the pourer's own cell")
        c.events.clear()
        c.use_item(136, 4, 128, 1, item_id=325, damage=0)
        c.drain(5.0)
        scooped = placed(c.events, 136, 4, 128, 0)
        check(len(scooped) >= 1, "empty bucket scooped the cell back to air")

        # ---------------- other: a second player's feet cell ----------------
        print("stage: second player's feet cell refuses foreign stone")
        c2 = Client(args.host, args.port, "HitboxBuddy", verbose=args.verbose)
        if not c2.handshake() or not c2.login():
            print("FAIL: second client handshake/login")
            return 1
        c2.drain(4.0)
        c2.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c2.drain(1.5)
        # Park the second player at a deterministic clear cell.
        sx, sz = 144.5, 132.5
        move_player(c2, sx, 4.0, sz)
        c2.drain(1.0)
        bx, bz = int(sx), int(sz)
        c.events.clear()
        c.use_item(bx, 3, bz, 1, item_id=STONE)
        c.drain(2.0)
        check(len(placed(c.events, bx, 4, bz, STONE)) == 0,
              "stone refused inside the other player's feet cell")

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

    print(f"HITBOX_CHECK_EXIT={1 if failures else 0}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
