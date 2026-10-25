#!/usr/bin/env python3
"""End-to-end chunk-radius reload check for CurseStone.

The stale used-chunks bug: when a client lowers its render distance the
MCPE 0.15.10 client discards every stored chunk beyond the new radius,
but the server kept those chunks marked as already sent. Raising the
radius back found nothing to re-stream, so the ring between the small
and the large radius stayed invisible forever (only a relogin restored
it — orderChunks-equivalent never re-sent chunks still present in
used_chunks_).

A real protocol-84 client drives a self-managed flat server:

  1. login + spawn, let the initial stream settle (view-distance=8)
  2. RequestChunkRadius(2)  -> ChunkRadiusUpdated(2)
     and no chunk outside Chebyshev radius 2 may arrive afterwards
  3. RequestChunkRadius(8)  -> ChunkRadiusUpdated(8)
     the server must RE-SEND the ring it already delivered before the
     decrease (Chebyshev 3..8 around the player chunk); the old server
     re-sent nothing
  4. RequestChunkRadius(2) again -> silence: no re-send storm, the
     dedup bookkeeping stays coherent after a full up-down-up cycle

    python3 tools/chunk_radius_reload_check.py [--server /path/to/CurseStone]

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


class RadiusClient(Client):
    """door_bed_flow_check Client + FullChunkData capture and radius
    requests."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunk_sends = []  # (cx, cz) per FullChunkDataPacket arrival

    def request_radius(self, r):
        self.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", r))

    def radius_replies(self):
        return [ev[1] for ev in self.events if ev[0] == "chunk_radius_updated"]

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 9:
            cx, cz = struct.unpack(">ii", p[1:9])
            self.chunk_sends.append((cx, cz))
            return
        if p and p[0] == CHUNK_RADIUS_UPDATED and len(p) >= 5:
            self.events.append(("chunk_radius_updated",
                                struct.unpack(">i", p[1:5])[0]))
            return
        super().on_game_packet(p)


def chebyshev(ax, az, bx, bz):
    return max(abs(ax - bx), abs(az - bz))


def wait_settled(c, min_unique, quiet=2.0, cap=14.0):
    """Drain until no new unique chunk arrives for `quiet` seconds."""
    start = time.time()
    last_new = time.time()
    seen = set(c.chunk_sends)
    while time.time() < start + cap:
        c.drain(0.25)
        cur = set(c.chunk_sends)
        if len(cur) > len(seen):
            seen = cur
            last_new = time.time()
        elif time.time() - last_new >= quiet and len(cur) >= min_unique:
            break
    return set(c.chunk_sends)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chunk-radius reload check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19148)
    ap.add_argument("--name", default="RadiusTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_radius_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ChunkRadius\nview-distance=8\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

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

        c = RadiusClient(args.host, args.port, args.name, verbose=args.verbose)
        print("stage: handshake+login")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        pcx, pcz = c.spawn[0] >> 4, c.spawn[2] >> 4
        print(f"  eid={c.eid} spawn={c.spawn} chunk=({pcx},{pcz})")

        # ---------------- phase A: initial stream at view-distance 8 ------
        print("stage: initial stream (radius 8)")
        before = wait_settled(c, min_unique=200)
        initial_unique = len(before)
        ring_full = {k for k in before if chebyshev(k[0], k[1], pcx, pcz) > 2}
        check(initial_unique >= 200,
              f"initial stream delivered ({initial_unique} chunks)")

        # ---------------- phase B: lower to 2 -----------------------------
        print("stage: lower radius to 2")
        c.request_radius(2)
        c.drain(1.0)
        replies = c.radius_replies()
        check(bool(replies) and replies[-1] == 2,
              f"ChunkRadiusUpdated(2) reply (got {replies})")
        n_lower = len(c.chunk_sends)
        c.drain(2.0)
        lowered_sends = c.chunk_sends[n_lower:]
        far = [k for k in lowered_sends if chebyshev(k[0], k[1], pcx, pcz) > 2]
        check(not far,
              f"no chunk beyond radius 2 while lowered ({len(lowered_sends)} sends)")

        # ---------------- phase C: raise back to 8 ------------------------
        # The client discarded the ring when the radius dropped; the server
        # must re-send it even though it delivered those chunks before.
        print("stage: raise radius back to 8")
        c.request_radius(8)
        c.drain(1.0)
        replies = c.radius_replies()
        check(bool(replies) and replies[-1] == 8,
              f"ChunkRadiusUpdated(8) reply (got {replies})")
        n_raise = len(c.chunk_sends)
        after = wait_settled(c, min_unique=len(before))
        resent = c.chunk_sends[n_raise:]
        resent_ring = {k for k in resent if k in before
                       and chebyshev(k[0], k[1], pcx, pcz) > 2}
        check(len(resent_ring) >= 100,
              f"ring re-streamed after raise ({len(resent_ring)}/{len(ring_full)} "
              f"old ring chunks re-sent, {len(resent)} total sends)")
        beyond = [k for k in resent if chebyshev(k[0], k[1], pcx, pcz) > 8]
        check(not beyond, "no chunk beyond radius 8 after raise")
        check(after >= before, "no previously delivered chunk lost "
              f"({len(after)} unique now vs {len(before)} before)")

        # ---------------- phase D: lower again -> silence -----------------
        print("stage: lower to 2 again (no re-send storm)")
        c.request_radius(2)
        c.drain(1.0)
        replies = c.radius_replies()
        check(bool(replies) and replies[-1] == 2,
              f"second ChunkRadiusUpdated(2) reply (got {replies})")
        n_last = len(c.chunk_sends)
        c.drain(3.0)
        tail = c.chunk_sends[n_last:]
        check(not tail, f"no re-send storm after second lower ({len(tail)} sends)")

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
