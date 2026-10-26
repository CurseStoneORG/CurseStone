#!/usr/bin/env python3
"""End-to-end chunk-streaming check while exploring for CurseStone.

The reported bug: while exploring, some chunks never render; walking
closer to them makes them appear. A real protocol-84 client walks a
straight line crossing many chunk boundaries and records every
FullChunkDataPacket. For every chunk that enters the view square
(Chebyshev radius = view-distance around the player's current chunk) we
measure how long the server took to send it and whether it ever arrived:

  1. login + spawn, initial stream settles (view-distance=8)
  2. walk east ~100 blocks in 8-block steps, streaming under load
  3. settle at the end and require the full view square delivered
  4. report arrival-latency percentiles (informational)

    python3 tools/chunk_explore_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

FULL_CHUNK_DATA = 0x34
REQUEST_CHUNK_RADIUS = 0x3D
CHUNK_RADIUS_UPDATED = 0x3E
MOVE_PLAYER = 0x10
EYE_HEIGHT = 1.62


class ExploreClient(Client):
    """door_bed_flow_check Client + chunk/move capture."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunk_sends = []      # (cx, cz) per FullChunkDataPacket arrival
        self.radius_replies = []

    def request_radius(self, r):
        self.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", r))

    def move_to(self, x, y, z, on_ground=True):
        body = (bytes([MOVE_PLAYER]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y + EYE_HEIGHT, z) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                bytes([0, 1 if on_ground else 0]))
        self.send_game(body)

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 9:
            cx, cz = struct.unpack(">ii", p[1:9])
            self.chunk_sends.append((cx, cz))
            return
        if p and p[0] == CHUNK_RADIUS_UPDATED and len(p) >= 5:
            self.radius_replies.append(struct.unpack(">i", p[1:5])[0])
            return
        super().on_game_packet(p)


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def wait_settled(c, quiet=2.0, cap=25.0, min_unique=0):
    start = last_new = time.time()
    seen = len(set(c.chunk_sends))
    while time.time() < start + cap:
        c.drain(0.25)
        cur = len(set(c.chunk_sends))
        if cur > seen:
            seen = cur
            last_new = time.time()
        elif time.time() - last_new >= quiet and cur >= min_unique:
            break
    return set(c.chunk_sends)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chunk exploration check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19155)
    ap.add_argument("--name", default="Explorer")
    ap.add_argument("--steps", type=int, default=26,
                    help="walk steps of 8 blocks (~104 blocks / 7 boundaries)")
    ap.add_argument("--generator", default="flat",
                    help="world generator: flat or normal")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_explore_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=Explore\nview-distance=8\ngamemode=0\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"%s\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n"
                % args.generator)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    proc = subprocess.Popen(
        [args.server], cwd=runtime, env=env,
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
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

        c = ExploreClient(args.host, args.port, args.name, verbose=args.verbose)
        print("stage: handshake+login")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(3.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.0)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        c.request_radius(8)
        c.drain(0.5)
        print(f"  eid={c.eid} spawn={c.spawn} radius_replies={c.radius_replies}")

        vd = 8
        t0 = time.time()
        base = wait_settled(c, min_unique=(2 * vd + 1) ** 2 - 20)
        print(f"stage: initial stream settled ({len(base)} chunks "
              f"in {time.time() - t0:.1f}s)")

        # ---------------- stage: walk east crossing boundaries ------------
        print("stage: exploring east")
        px, py, pz = float(c.spawn[0]), float(c.spawn[1]), float(c.spawn[2])
        enter_time = {}      # (cx,cz) -> first time inside view square
        arrive_time = {}     # (cx,cz) -> first arrival
        marks = []           # (t, (pcx, pcz))
        marks.append((time.time(), (int(px) >> 4, int(pz) >> 4)))

        def cur_chunk():
            return (int(px) >> 4, int(pz) >> 4)

        def credit_chunks():
            t_now = time.time()
            for (ccx, ccz) in c.chunk_sends:
                key = (ccx, ccz)
                if key not in arrive_time:
                    arrive_time[key] = t_now

        holes_report = []
        for step in range(args.steps):
            px += 2.0  # walking-pace steps: 2 blocks per ~0.25s
            c.move_to(px, float(c.spawn[1]), pz)
            t_move = time.time()
            c.drain(0.25)
            credit_chunks()
            pc = cur_chunk()
            marks.append((t_move, pc))
            for dx in range(-vd, vd + 1):
                for dz in range(-vd, vd + 1):
                    key = (pc[0] + dx, pc[1] + dz)
                    if key not in enter_time:
                        enter_time[key] = t_move
            # checkpoint every 5 steps: any in-view chunk with no arrival
            # after a 2s grace from when it entered view?
            if step % 5 == 4:
                grace = t_move - 2.0
                holes = [k for k, te in enter_time.items()
                         if te <= grace and k not in arrive_time]
                holes_report.append((step, pc, holes))
        end_walk = time.time()
        tail = wait_settled(c, quiet=2.0, cap=20.0)
        for (ccx, ccz) in tail:
            arrive_time.setdefault((ccx, ccz), end_walk)

        for (step, pc, holes) in holes_report:
            check(not holes,
                  f"step {step} at {pc}: view square complete "
                  f"({len(holes)} holes: {sorted(holes)[:6]})")

        # every arrival should also be credited with its enter time if it
        # was in view while walking
        final_pc = cur_chunk()
        final_square = {(final_pc[0] + dx, final_pc[1] + dz)
                        for dx in range(-vd, vd + 1)
                        for dz in range(-vd, vd + 1)}
        received = arrive_time.keys()
        missing = sorted(final_square - set(received))
        check(not missing,
              f"final view square fully delivered ({len(final_square) - len(missing)}/"
              f"{len(final_square)}); missing={missing[:8]}")

        # arrival latency relative to when the chunk entered the view square
        delays = []
        for key, ta in arrive_time.items():
            te = enter_time.get(key)
            if te is not None and key in final_square:
                delays.append((ta - te, key))
        delays.sort()
        if delays:
            vals = [d for d, _ in delays]
            p50 = vals[len(vals) // 2]
            p90 = vals[int(len(vals) * 0.9)]
            worst = delays[-1]
            print(f"  latency: n={len(vals)} p50={p50:.2f}s p90={p90:.2f}s "
                  f"max={worst[0]:.2f}s at {worst[1]}")
            late = [(d, k) for d, k in delays if d > 4.0]
            check(len(late) == 0,
                  f"no chunk took >4s to arrive after entering view "
                  f"({len(late)} late, worst={delays[-1][0]:.2f}s)")

        # during the walk no duplicate-flood sanity: unique == sends near end
        print(f"  total chunk sends={len(c.chunk_sends)} unique={len(set(c.chunk_sends))}")

    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s):")
        for f in failures:
            print("  - " + f)
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
