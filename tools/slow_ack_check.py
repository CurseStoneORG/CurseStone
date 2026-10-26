#!/usr/bin/env python3
"""End-to-end congestion-backlog check for CurseStone.

The chunk-hole bug: past MCP_MAX_INFLIGHT_RELIABLE (512 un-ACKed
reliable datagrams) the server used to REFUSE new reliable payloads —
but the game layer had already marked those chunks as sent, so the
client never received them and the terrain stayed invisible until the
player wandered away and back. A slow-ACKing client (real phone on
Wi-Fi/mobile data) hits that ceiling during normal chunk streaming.

A protocol-84 client streams a radius-3 square (49 chunks x ~83 KB =
~2900 datagrams, far past the 512 ceiling) while ACKing only every
1.5 s:

  * OLD server: the first ~6 payloads fill the window, the rest are
    refused/dropped forever -> the client ends up with a handful of
    chunks and 40+ permanent holes.
  * NEW server: the refused payloads wait in the per-session backlog
    and replay as ACKs free recovery slots -> ALL 49 chunks arrive.

    python3 tools/slow_ack_check.py [--server /path/to/CurseStone]

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


class SlowAckClient(Client):
    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunk_sends = []

    def request_radius(self, r):
        self.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", r))

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 9:
            cx, cz = struct.unpack(">ii", p[1:9])
            self.chunk_sends.append((cx, cz))
            return
        super().on_game_packet(p)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone slow-ACK backlog check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19167)
    ap.add_argument("--radius", type=int, default=3)
    ap.add_argument("--generator", default="normal",
                    help="normal: real terrain (~20 datagrams/chunk) so the "
                         "reliable window actually fills; flat compresses to "
                         "~300 B/chunk and never trips the ceiling")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_slowack_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=SlowAck\nview-distance=8\ngamemode=0\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"%s\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n"
                % args.generator)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    boot_out = os.path.join(runtime, "boot_out.log")
    with open(boot_out, "w") as bo:
        proc = subprocess.Popen(
            [args.server], cwd=runtime, env=env,
            stdin=subprocess.PIPE, stdout=bo, stderr=subprocess.DEVNULL)

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
                if "Server ready" in open(boot_out).read():
                    booted = True
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        c = SlowAckClient(args.host, args.port, "SlowAck")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        # Lock the radius BEFORE any chunk streams so the server never
        # starts the default radius-8 blast; the view square is radius-3
        # (49 chunks x ~83 KB) from tick one.
        c.request_radius(args.radius)
        c.drain(0.5)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.0)
        if c.eid == 0:
            print("FAIL: no StartGame")
            return 1

        pcx, pcz = c.spawn[0] >> 4, c.spawn[2] >> 4
        expected = {(pcx + dx, pcz + dz)
                    for dx in range(-args.radius, args.radius + 1)
                    for dz in range(-args.radius, args.radius + 1)}
        ring = {k for k in expected
                if max(abs(k[0] - pcx), abs(k[1] - pcz)) >= 2}

        # Baseline: the whole square arrives (ACKs flowing normally).
        start = time.time()
        while time.time() < start + 25 and len(set(c.chunk_sends)) < len(expected):
            c.drain(0.25)
        base = set(c.chunk_sends)
        check(len(base & expected) == len(expected),
              f"baseline radius-{args.radius} square delivered "
              f"({len(base & expected)}/{len(expected)})")

        # Force a full ring re-stream: lowering discards the ring on both
        # sides (the 0.15 client drops chunks beyond the granted radius and
        # the server un-marks them), raising schedules it all again.
        c.request_radius(1)
        c.drain(1.0)
        n_raise = len(c.chunk_sends)
        c.request_radius(args.radius)

        # Burst WITHOUT acks: the reliable window fills (512 datagrams hold
        # roughly six 83 KB chunk payloads) and the rest must queue in the
        # session backlog instead of being refused.
        print("stage: ring re-stream with ACKs withheld (4s)")
        c.drain(4.0, ack=False)
        resent_silent = {k for k in c.chunk_sends[n_raise:] if k in ring}
        print(f"  ring chunks delivered while silent: {len(resent_silent)}/{len(ring)}")

        # Now ACK slowly: every ACK batch frees recovery slots and the
        # backlog replays the deferred payloads.
        print("stage: slow ACKs until settled")
        start = time.time()
        last_new = time.time()
        seen = {k for k in c.chunk_sends[n_raise:] if k in ring}
        while time.time() < start + 40:
            c.drain(1.5)  # ack=True default — each batch frees ~a payload
            cur = {k for k in c.chunk_sends[n_raise:] if k in ring}
            if len(cur) > len(seen):
                seen = cur
                last_new = time.time()
            elif time.time() - last_new >= 4.0:
                break

        missing = sorted(ring - seen)
        check(not missing,
              f"full ring re-streamed over slow ACKs ({len(seen)}/{len(ring)}); "
              f"missing={missing[:6]}")
        # Informational: whether the burst actually crossed the reliable
        # window depends on how well the terrain compressed (flat chunks
        # shrink to ~300 B and never fill 512 datagrams). The ceiling +
        # backlog replay mechanics are asserted deterministically by
        # tools/backlog_unit_check.cpp; this e2e proves end-to-end delivery
        # integrity under delayed ACKs.
        print(f"  info: {len(resent_silent)}/{len(ring)} ring chunks arrived "
              f"before the first ACK batch")

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
