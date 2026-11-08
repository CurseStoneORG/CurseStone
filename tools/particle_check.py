#!/usr/bin/env python3
"""End-to-end fall/sprint particle check for a local CurseStone server.

Boots its own super-flat server (same boot shape as chunk_explore_check)
and drives one bot through the real protocol-84 wire:

  1. idle standing (control)        -> zero terrain/break particle events
  2. sprint forward on the ground   -> generic terrain particles
                                       (0x4000 | 16) rising at the feet,
                                       ground block id in the data field
  3. sprint off, still walking      -> particles stop
  4. 8-block fall onto the ground   -> break-particle event 2001 at the
                                       landing cell with the ground block
                                       id in data (the protocol-84 event
                                       that also plays the landing thud)

Usage:
    python3 tools/particle_check.py --server <bin> [--port 19185]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from portal_travel_check import PortalClient  # noqa: E402
from linux_login_check import STAGE_SPAWN  # noqa: E402

MOVE_PLAYER = 0x10
LEVEL_EVENT = 0x16
PLAYER_ACTION = 0x20
START_GAME = 0x09

# Protocol-84 action ids (PlayerActionPacket.h): 9/10 are the sprint
# toggles — NOT the 8/9 that later protocol versions use.
ACTION_START_SPRINT = 9
ACTION_STOP_SPRINT = 10

EVENT_PARTICLE_DESTROY = 2001     # break particles + dig sound (thud)
PARTICLE_ADD_MASK = 0x4000        # generic particles: evid = mask | type
PARTICLE_TERRAIN = 16             # block-textured dust

GRASS_BLOCK = 2                   # super-flat surface block
EYE_HEIGHT = 1.62


class ParticleClient(PortalClient):
    """PortalClient + LevelEvent particle observers."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.terrain_events = []   # (x, y, z, data) generic terrain dust
        self.break_events = []     # (x, y, z, data) EVENT_PARTICLE_DESTROY
        self.sg_pos = None         # StartGame player position (feet)

    def classify(self, payload: bytes):
        if payload and payload[0] == LEVEL_EVENT and len(payload) >= 19:
            evid = struct.unpack(">h", payload[1:3])[0]
            x, y, z = struct.unpack(">fff", payload[3:15])
            data = struct.unpack(">i", payload[15:19])[0]
            if evid == EVENT_PARTICLE_DESTROY:
                self.break_events.append((x, y, z, data))
            elif (evid & ~0x0FFF) == PARTICLE_ADD_MASK and \
                    (evid & 0x0FFF) == PARTICLE_TERRAIN:
                self.terrain_events.append((x, y, z, data))
        elif payload and payload[0] == START_GAME and len(payload) >= 46:
            # [0] id [1:5] seed [5] dim [6:10] gen [10:14] gm [14:22] eid
            # [22:34] spawn xyz (ints) [34:46] player xyz (floats, feet)
            px, py, pz = struct.unpack(">fff", payload[34:46])
            self.sg_pos = (px, py, pz)
        PortalClient.classify(self, payload)

    def action(self, action_id):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)               # eid (server re-stamps it)
        pa += struct.pack(">i", action_id)
        pa += struct.pack(">iii", 0, 0, 0)
        pa += struct.pack(">i", 0)
        self.send_game(pa)


def ground_reference(c):
    """Server-known ground position: (x, eye_y, z) or None."""
    if c.last_reset:
        return c.last_reset[0], c.last_reset[1], c.last_reset[2]
    if c.sg_pos:
        return c.sg_pos[0], c.sg_pos[1] + EYE_HEIGHT, c.sg_pos[2]
    return None


def login(host, port, name):
    for attempt in range(3):
        c = ParticleClient(host, port, name)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        # Wait only for StartGame here: everything after it (the spawn
        # MOVE_PLAYER MODE_RESET included) must reach start_rx so the
        # classify hooks can see it.
        if not c.expect(STAGE_SPAWN, seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.start_rx()
        c.watch(3.0)
        return c
    return None


def run(host: str, port: int, server_bin: str) -> int:
    name = "DustBot"
    runtime = tempfile.mkdtemp(prefix="cs_particle_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=Dust\ngamemode=1\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(server_bin)
    proc = subprocess.Popen([server_bin], cwd=runtime, env=env,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    try:
        deadline = time.time() + 40
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            try:
                with open(server_log) as f:
                    if "Server ready" in f.read():
                        booted = True
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        c = login(host, port, name)
        if not c:
            print("FAIL: could not complete login")
            return 1
        ref = ground_reference(c)
        if not ref:
            print("FAIL: never learned the spawn position")
            return 1
        sx, gy, sz = float(ref[0]), float(ref[1]), float(ref[2])
        print(f"[{name}] login OK; spawn ground (eye) = "
              f"({sx:.2f}, {gy:.2f}, {sz:.2f})")

        # ---- 1. idle control: standing still emits nothing -------------
        print("[DustBot] scenario 1: idle standing emits no particles...")
        t0, b0 = len(c.terrain_events), len(c.break_events)
        c.watch(2.0)
        idle_terr = len(c.terrain_events) - t0
        idle_break = len(c.break_events) - b0
        print(f"[{name}] idle: terrain={idle_terr} break={idle_break}")
        check(idle_terr == 0 and idle_break == 0,
              "idle standing emits no particle events")

        # ---- 2. sprint forward: terrain dust kicks up at the feet ------
        print("[DustBot] scenario 2: sprinting kicks up terrain dust...")
        c.action(ACTION_START_SPRINT)
        time.sleep(0.3)
        t0, b0 = len(c.terrain_events), len(c.break_events)
        x = sx
        for _ in range(20):
            x += 0.3
            c.move(x, gy, sz, on_ground=True)
            time.sleep(0.06)
        c.watch(1.0)
        sprint_terr = c.terrain_events[t0:]
        sprint_break = c.break_events[b0:]
        near = [e for e in sprint_terr
                if abs(e[2] - sz) < 1.5 and sx - 1.0 <= e[0] <= x + 1.0]
        grassy = [e for e in near if (e[3] & 0xFF) == GRASS_BLOCK]
        print(f"[{name}] sprint: terrain={len(sprint_terr)} "
              f"(near={len(near)}, grass={len(grassy)}) "
              f"break={len(sprint_break)}")
        check(len(near) >= 4,
              f"sprint emits terrain dust at the feet ({len(near)} events)")
        check(len(grassy) == len(near) and len(near) > 0,
              "sprint dust carries the ground block id in data")
        check(len(sprint_break) == 0,
              "sprint dust is silent (no break/dig events)")

        # ---- 3. sprint off: walking emits nothing ----------------------
        print("[DustBot] scenario 3: dust stops when the sprint ends...")
        c.action(ACTION_STOP_SPRINT)
        time.sleep(0.3)
        t0 = len(c.terrain_events)
        for _ in range(15):
            x += 0.25
            c.move(x, gy, sz, on_ground=True)
            time.sleep(0.07)
        c.watch(1.0)
        walk_terr = len(c.terrain_events) - t0
        print(f"[{name}] walk after sprint: terrain={walk_terr}")
        check(walk_terr == 0,
              f"plain walking emits no dust ({walk_terr} events)")

        # ---- 4. hard landing: break-particle burst at the feet ---------
        print("[DustBot] scenario 4: hard landing bursts ground dust...")
        b0 = len(c.break_events)
        # Rise 8 blocks airborne, then descend in 2-block steps so the
        # server accumulates a real fall distance (> 3 blocks).
        c.move(sx, gy + 8.0, sz, on_ground=False)
        time.sleep(0.25)
        for step in (6.0, 4.0, 2.0):
            c.move(sx, gy + step, sz, on_ground=False)
            time.sleep(0.15)
        c.move(sx, gy, sz, on_ground=True)   # touchdown
        c.watch(1.5)
        landing = c.break_events[b0:]
        near_land = [e for e in landing
                     if abs(e[0] - sx) < 2.5 and abs(e[2] - sz) < 2.5 and
                     abs(e[1] - gy) < 3.0]
        grassy_land = [e for e in near_land if (e[3] & 0xFF) == GRASS_BLOCK]
        print(f"[{name}] landing: break={len(landing)} "
              f"(near={len(near_land)}, grass={len(grassy_land)}) "
              f"first={landing[0] if landing else None}")
        check(len(near_land) >= 1,
              f"hard landing emits the break-particle event "
              f"({len(near_land)} events)")
        check(len(grassy_land) == len(near_land) and len(near_land) > 0,
              "landing dust carries the ground block id in data")
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    print(f"[DustBot] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone fall/sprint particles E2E")
    ap.add_argument("--server", default="/tmp/cs_part/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19185)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port, args.server))
