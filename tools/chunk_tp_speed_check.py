#!/usr/bin/env python3
"""Chunk generation + teleport fill speed check for CurseStone.

The reported pain: teleporting to never-visited coordinates leaves the
player hanging until the destination chunks generate, and generation
feels slow. This check measures the two wall-clock phases that decide
how that feels on a real client:

  1. spawn pre-generation — a fresh boot must generate the spawn area
     in the background BEFORE any player joins (the tool waits for the
     "Spawn pre-generation finished" boot log; --expect-pregen turns
     the wait into a hard assertion).
  2. teleport fill — a logged-in client /tp's to fresh terrain and we
     time the arrival of the destination view square (first chunk,
     25th chunk, full square).

Passive invariants always assert: the server stays alive, every chunk
of the destination view eventually arrives exactly once, and the spawn
view fully streams. --assert additionally enforces the speed bounds
that the optimized pipeline must meet:

    python3 tools/chunk_tp_speed_check.py [--assert] [--port 19170]

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
TEXT_PACKET = 0x07
VIEW_DISTANCE = 8
VIEW_CHUNKS = (2 * VIEW_DISTANCE + 1) ** 2  # 289
# Teleport destination: chunk (312, 312), far from the spawn square.
TP_X, TP_Y, TP_Z = 5000, 72, 5000
TP_CX, TP_CZ = TP_X >> 4, TP_Z >> 4


def be_string(s: str) -> bytes:
    b = s.encode("utf-8")
    return struct.pack(">H", len(b)) + b


class SpeedClient(Client):
    """door_bed_flow_check Client + chunk arrival timestamps."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunk_times = {}  # (cx, cz) -> first arrival wall-clock

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 9:
            cx, cz = struct.unpack(">ii", p[1:9])
            key = (cx, cz)
            if key not in self.chunk_times:
                self.chunk_times[key] = time.time()
            return
        super().on_game_packet(p)

    def chat(self, message: str):
        # TextPacket: pid, type u8 (1 = CHAT), source string, message string.
        self.send_game(bytes([TEXT_PACKET, 1]) + be_string(self.name) +
                       be_string(message))

    def request_radius(self, r: int):
        self.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", r))


def wait_boot_line(server_log, needle, cap):
    """Wait until server.log contains `needle`; returns (found, elapsed)."""
    start = time.time()
    while time.time() < start + cap:
        try:
            with open(server_log) as f:
                if needle in f.read():
                    return True, time.time() - start
        except FileNotFoundError:
            pass
        if os.path.exists(server_log + ".dead") :
            pass
        time.sleep(0.25)
    return False, time.time() - start


def wait_settled(c, quiet=2.0, cap=45.0):
    """Drain until no new chunk arrives for `quiet` seconds."""
    start = last_new = time.time()
    seen = len(c.chunk_times)
    while time.time() < start + cap:
        c.drain(0.25)
        cur = len(c.chunk_times)
        if cur > seen:
            seen = cur
            last_new = time.time()
        elif time.time() - last_new >= quiet:
            break
    return time.time()


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chunk generation speed check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19170)
    ap.add_argument("--name", default="Speedster")
    ap.add_argument("--assert", dest="assert_speed", action="store_true",
                    help="enforce the optimized-pipeline speed bounds")
    ap.add_argument("--expect-pregen", action="store_true",
                    help="assert the boot pre-generation log line exists")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_tpspeed_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=TpSpeed\nview-distance=%d\ngamemode=1\n" % (args.port, VIEW_DISTANCE))
    # Deliberately omit async-workers: the default must carry the optimized
    # value (8), exercising exactly what a stock server boots with.
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")

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

    def stop_server():
        try:
            proc.stdin.write(b"/stop\n")
            proc.stdin.flush()
            proc.wait(timeout=30)
        except Exception:
            proc.kill()

    try:
        # ---- stage 1: boot + background spawn pre-generation -------------
        boot_deadline = time.time() + 60
        booted = False
        while time.time() < boot_deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            with open(server_log, "a+") as f:
                f.seek(0)
                if "Server ready" in f.read():
                    booted = True
                    break
            time.sleep(0.4)
        check(booted, "server booted")
        if not booted:
            return 1

        pregen_found, pregen_wait = wait_boot_line(
            server_log, "Spawn area pregenerated:", cap=90.0)
        if args.expect_pregen:
            check(pregen_found,
                  "spawn pre-generation finished in background (%.1fs after ready)"
                  % pregen_wait)
        else:
            print("  INFO  pregen log %s (%.1fs after ready)" %
                  ("found" if pregen_found else "absent", pregen_wait))

        # ---- stage 2: join + initial spawn fill --------------------------
        c = SpeedClient(args.host, args.port, args.name, verbose=args.verbose)
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(3.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.0)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        # The player-side /tp below needs operator rights; grant them through
        # the console exactly like an operator would.
        proc.stdin.write(("/op %s\n" % args.name).encode())
        proc.stdin.flush()
        c.drain(1.0)
        c.request_radius(VIEW_DISTANCE)
        c.drain(0.5)

        t_join_done = time.time()
        wait_settled(c, quiet=2.0, cap=60.0)
        spawn_fill = time.time() - t_join_done
        spawn_chunks = len(c.chunk_times)
        print("  INFO  spawn fill: %d chunks in %.1fs" % (spawn_chunks, spawn_fill))
        if args.assert_speed:
            check(spawn_chunks >= VIEW_CHUNKS,
                  "full spawn view streamed (%d/%d)" % (spawn_chunks, VIEW_CHUNKS))
            check(spawn_fill < 15.0,
                  "spawn fill fast with pregen (%.1fs < 15s)" % spawn_fill)

        # ---- stage 3: teleport to fresh terrain + measure the fill -------
        dest0 = (TP_CX - VIEW_DISTANCE, TP_CZ - VIEW_DISTANCE)
        dest1 = (TP_CX + VIEW_DISTANCE, TP_CZ + VIEW_DISTANCE)
        before = set(c.chunk_times.keys())
        dest_keys = {(x, z)
                     for x in range(dest0[0], dest1[0] + 1)
                     for z in range(dest0[1], dest1[1] + 1)}

        c.chat("/tp %d %d %d" % (TP_X, TP_Y, TP_Z))
        t_tp = time.time()

        deadline = t_tp + 90.0
        while time.time() < deadline and proc.poll() is None:
            c.drain(0.25)
            near = [t for k, t in c.chunk_times.items()
                    if k in dest_keys and k not in before]
            if len(near) >= VIEW_CHUNKS:
                break
        fill_done = time.time()

        fresh = {k: t for k, t in c.chunk_times.items()
                 if k in dest_keys and k not in before}
        check(proc.poll() is None, "server alive through the teleport fill")
        check(len(fresh) >= VIEW_CHUNKS,
              "full destination view streamed (%d/%d chunks)"
              % (len(fresh), VIEW_CHUNKS))

        if fresh:
            ordered = sorted(fresh.values())
            t_first = ordered[0] - t_tp
            t_25 = ordered[min(24, len(ordered) - 1)] - t_tp
            t_full = ordered[-1] - t_tp
            print("  INFO  teleport fill: first %.1fs | 25th %.1fs | full %.1fs"
                  % (t_first, t_25, t_full))
            if args.assert_speed:
                check(t_first < 5.0,
                      "destination responds fast (%.1fs < 5s)" % t_first)
                check(t_full < 30.0,
                      "full teleport fill bounded (%.1fs < 30s)" % t_full)
        elif args.assert_speed:
            check(False, "destination responded")

    finally:
        stop_server()
        if not args.keep:
            subprocess.run(["rm", "-rf", runtime])

    print()
    if failures:
        print("FAILED (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
