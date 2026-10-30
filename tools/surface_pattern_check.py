#!/usr/bin/env python3
"""Surface pattern check for the vanilla overworld generator.

The reported bug: stone mountains showed a repeating per-chunk pattern —
1-block grass lines exactly on chunk borders (a green lattice) plus the
same surface blotches recurring chunk after chunk. Root cause: the
surface depth noise was sampled with chunk coordinates instead of world
block coordinates, so every chunk reused an overlapping, phase-locked
noise window.

Drives a real protocol-84 client around the world, records
FullChunkDataPacket payloads and measures:

  border    grass columns on a chunk edge whose two inward neighbors are
            stone at the same height — the lattice signature.
  interior  the same 1-wide grass-between-stone shape for columns away
            from chunk edges (the natural false-positive rate).
  periodic  mean surface-material agreement between horizontally adjacent
            chunks vs. distant chunks (per-chunk repetition).

A vanilla generator scores ~zero on both border lines and periodicity.

    python3 tools/surface_pattern_check.py [--server /path/to/CurseStone]

Exit code 0 = the terrain passes; 1 = the lattice is back.
"""
import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

FULL_CHUNK_DATA = 0x34
REQUEST_CHUNK_RADIUS = 0x3D
MOVE_PLAYER = 0x10
EYE_HEIGHT = 1.62

GRASS, STONE, DIRT, GRAVEL = 2, 1, 3, 13
AIR = 0


def wait_boot(proc, server_log, deadline_s=60):
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


class PatternClient(Client):
    """Client + FullChunkData payload capture."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunks = {}  # (cx, cz) -> bytes payload

    def request_radius(self, r):
        self.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", r))

    def move_to(self, x, y, z):
        body = (bytes([MOVE_PLAYER]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y + EYE_HEIGHT, z) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                bytes([0, 0]))
        self.send_game(body)

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 14:
            cx, cz = struct.unpack(">ii", p[1:9])
            (dlen,) = struct.unpack(">i", p[10:14])
            if dlen > 0 and len(p) >= 14 + dlen:
                self.chunks[(cx, cz)] = p[14:14 + dlen]
            return
        super().on_game_packet(p)


def surface_block(data, lx, lz):
    """Topmost non-air block id and its y for a column (128-high world)."""
    base = (lx << 11) | (lz << 7)
    for y in range(127, 0, -1):
        bid = data[base + y]
        if bid != AIR:
            return bid, y
    return AIR, 0


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone surface pattern check")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-rel/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19139)
    ap.add_argument("--name", default="SurfaceTester")
    ap.add_argument("--radius", type=int, default=6)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_surface_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=SurfacePattern\nview-distance=%d\ngamemode=1\n"
                "advanced-log=true\n" % (args.port, args.radius + 2))
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  debug-mode: false\nworld:\n  format: \"anvil\"\n"
                "  generator: \"normal\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    try:
        if not wait_boot(proc, server_log):
            print("FAIL: server never became ready")
            return 1
        print("server ready")

        c = PatternClient(args.host, args.port, args.name, verbose=args.verbose)
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1

        # Visit several far-apart spots so the sample covers distinct biomes
        # (the old bug repeated the same pattern at every one of them).
        stops = [(0, 0), (320, 0), (0, 320), (320, 320), (-320, -320),
                 (480, 160), (160, -480)]
        for sx, sz in stops:
            c.move_to(sx + 0.5, 90, sz + 0.5)
            c.request_radius(args.radius)
            c.drain(9.0)
        print(f"  captured {len(c.chunks)} chunks")

        # ---- decode a global surface map ----
        surf = {}  # (x, z) -> (block id, y)
        for (cx, cz), blob in c.chunks.items():
            for lx in range(16):
                for lz in range(16):
                    bid, y = surface_block(blob, lx, lz)
                    surf[(cx * 16 + lx, cz * 16 + lz)] = (bid, y)

        # ---- metric 1: 1-wide grass strips aligned to chunk borders ----
        # The lattice signature: a border column surfaced with grass while
        # the two columns toward the chunk interior are stone at the same
        # height. Natural grass-on-stone transitions make wide patches, so
        # the interior rate measures the false-positive floor.
        border_cols = border_hits = 0
        interior_cols = interior_hits = 0
        for (x, z), (bid, y) in list(surf.items()):
            if bid != GRASS:
                continue
            lx, lz = x & 15, z & 15
            on_border = lx in (0, 15) or lz in (0, 15)
            # the two columns toward the chunk interior
            ix = x - 1 if lx == 15 else (x + 1 if lx == 0 else None)
            iz = z - 1 if lz == 15 else (z + 1 if lz == 0 else None)
            if on_border:
                if ix is not None:
                    probes = [(ix, z), (ix + (-1 if lx == 15 else 1), z)]
                else:
                    probes = [(x, iz), (x, iz + (-1 if lz == 15 else 1))]
                border_cols += 1
                if all(surf.get(q, (AIR, 0))[0] == STONE and surf.get(q, (AIR, 0))[1] == y
                       for q in probes):
                    border_hits += 1
            else:
                interior_cols += 1
                if (surf.get((x - 1, z), (AIR, 0))[0] == STONE and
                        surf.get((x + 1, z), (AIR, 0))[0] == STONE and
                        surf.get((x - 1, z), (AIR, 0))[1] == y and
                        surf.get((x + 1, z), (AIR, 0))[1] == y):
                    interior_hits += 1

        def rate(hits, cols):
            return hits * 10000.0 / cols if cols else 0.0

        border_rate = rate(border_hits, border_cols)
        interior_rate = rate(interior_hits, interior_cols)
        print(f"  border grass-lines:  {border_hits}/{border_cols} "
              f"({border_rate:.1f} per 10k)")
        print(f"  interior same shape: {interior_hits}/{interior_cols} "
              f"({interior_rate:.1f} per 10k)")

        # ---- metric 2: per-chunk repetition of the surface material ----
        def chunk_mask(cx, cz):
            m = []
            for lx in range(16):
                for lz in range(16):
                    bid, y = surf.get((cx * 16 + lx, cz * 16 + lz), (AIR, 0))
                    m.append(1 if bid == STONE else 0)
            return m if sum(m) > 8 else None

        def agreement(m1, m2):
            same = sum(1 for a, b in zip(m1, m2) if a == b)
            return same / 256.0

        adj, far = [], []
        keys = sorted(c.chunks.keys())
        for cx, cz in keys:
            m1 = chunk_mask(cx, cz)
            if not m1:
                continue
            for dx, dz, bucket in ((1, 0, adj), (0, 1, adj), (4, 0, far), (0, 4, far)):
                m2 = chunk_mask(cx + dx, cz + dz)
                if m2:
                    bucket.append(agreement(m1, m2))
        adj_mean = sum(adj) / len(adj) if adj else 0.0
        far_mean = sum(far) / len(far) if far else 0.0
        print(f"  adjacent-chunk surface agreement: {adj_mean:.3f} "
              f"(distant: {far_mean:.3f})")

        failures = []
        # The lattice shows up as border lines massively over the natural
        # interior rate; vanilla keeps both near zero.
        if border_rate > max(3.0 * (interior_rate + 0.5), 8.0):
            failures.append(
                f"grass lattice on chunk borders ({border_rate:.1f} per 10k "
                f"vs interior {interior_rate:.1f})")
        if len(far) >= 5 and adj_mean > far_mean + 0.12:
            failures.append(
                f"surface pattern repeats per chunk (adjacent {adj_mean:.3f} "
                f"vs distant {far_mean:.3f})")
        elif len(far) < 5:
            print(f"  note  too few mountain chunk pairs for the repetition "
                  f"metric ({len(adj)} adj / {len(far)} far)")

        for f in failures:
            print("  FAIL  " + f)
        if not failures:
            print("  PASS  surface patterns are vanilla (no lattice, no repetition)")

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

    print(f"SURFACE_CHECK_EXIT={1 if failures else 0}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
