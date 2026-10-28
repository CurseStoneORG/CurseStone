#!/usr/bin/env python3
"""Session timeout check.

Verifies the raklib session sweep against the 5s timeout
(MCP_SESSION_TIMEOUT_MS, user-requested reduction; was 30s):

  1. A client that goes totally silent (no datagrams at all — the
     signature of a crashed client or a dead uplink) is reaped by the
     session timeout within seconds.
  2. A client that stays connected but fully idle (no game activity,
     only the RakNet keepalive pong the real client answers) is NOT
     reaped — the 4s server keepalive ping keeps its receive clock
     inside the 5s window.

The fake client machinery comes from door_bed_flow_check.py's Client:
it ACKs every reliable datagram and answers server keepalive pings
with connected pongs from a background thread, exactly like the real
client's network thread.
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone session timeout check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19157)
    ap.add_argument("--name", default="TimeoutTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_timeout_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=TimeoutCheck\nview-distance=3\ngamemode=1\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write(args.name + "\n" + args.name + "B\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  debug-mode: true\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    stderr_log = open(os.path.join(runtime, "stderr.log"), "wb")
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=stderr_log)
    server_log = os.path.join(runtime, "server.log")

    def log_text():
        try:
            with open(server_log) as f:
                return f.read()
        except FileNotFoundError:
            return ""

    def console(cmd):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    def boot_and_wait():
        deadline = time.time() + 40
        while time.time() < deadline:
            if proc.poll() is not None:
                return False
            if "Server ready" in log_text():
                return True
            time.sleep(0.4)
        return False

    try:
        if not boot_and_wait():
            print("FAIL: server never became ready")
            return 1

        # -------- stage 1: silent client is reaped fast ------------------
        print("stage: silent client reaped by the 5s timeout")
        a = Client(args.host, args.port, args.name, verbose=args.verbose)
        if not a.handshake() or not a.login():
            print("FAIL: handshake/login (A)")
            return 1
        a.drain(3.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(2.0)
        if a.eid == 0:
            print("FAIL: A never received StartGame (no eid)")
            return 1

        a.stop_keepalive()
        silence_start = time.time()
        # A stops draining AND sending: no ACKs, no pongs, no game traffic.
        reap_line = None
        deadline = time.time() + 15.0
        while time.time() < deadline:
            time.sleep(0.5)
            text = log_text()
            if ("Closing player " + args.name + " ") in text and "timeout" in text:
                reap_line = time.time() - silence_start
                break
        check(reap_line is not None, "silent session reaped with reason=timeout")
        if reap_line is not None:
            check(reap_line < 13.0,
                  f"reap happened inside the timeout window + sweep slack "
                  f"(took {reap_line:.1f}s; old timeout was 30s)")

        # -------- stage 2: idle-but-alive client is never reaped ---------
        print("stage: idle client (pong keepalive) survives the window")
        b = Client(args.host, args.port, args.name + "B", verbose=args.verbose)
        if not b.handshake() or not b.login():
            print("FAIL: handshake/login (B)")
            return 1
        b.drain(3.0)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        b.drain(2.0)
        if b.eid == 0:
            print("FAIL: B never received StartGame (no eid)")
            return 1

        idle_deadline = time.time() + 12.0
        while time.time() < idle_deadline:
            # drain() ACKs reliable traffic and pongs keepalive pings —
            # exactly what a real AFK client does. No game packets sent.
            b.drain(0.5)
        text = log_text()
        check(("Closing player " + args.name + "B ") not in text,
              "idle client with pong keepalive is not reaped")

        console("stop")
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
