#!/usr/bin/env python3
"""End-to-end debug-mode check for CurseStone.

The scattered console debug (RakNet [NET] diagnostics, [PKT] packet
traces, MainLogger::debug lines) used to print unconditionally — the
default cursestone.yml even shipped advanced-log: true — making the
console unreadable during normal play. Everything now sits behind the
single `settings.debug-mode` key:

  boot 1: fresh runtime, NO debug-mode key (default false), legacy
          advanced-log: true present and ignored
          -> seed file contains `debug-mode: false`
          -> a client session with an unknown packet id and 7 idle
             seconds produces NO [PKT] / [NET] / [RakLib][diag] lines
  boot 2: same runtime but debug-mode: true
          -> the unknown packet id shows its [PKT] line and the
             reliability layer shows its [NET] health line

    python3 tools/debug_mode_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402


def boot_server(binary, runtime, port, yml_extra):
    """Boot a server in `runtime`. yml_extra=None means: write NO
    cursestone.yml so the server seeds its defaults (seeding check).
    Otherwise yml_extra lines are inserted under `settings:`."""
    if yml_extra is not None:
        with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
            f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                    "  advanced-log: true\n" + yml_extra +
                    "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                    "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=DebugMode\nview-distance=8\ngamemode=0\n" % port)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(binary)
    # server.log is written by the async logger in deferred 4KB/25s flushes
    # and may not even exist after a quiet boot — capture STDOUT instead,
    # which the worker flushes promptly, and use it for the boot check.
    boot_out = os.path.join(runtime, "boot_out.log")
    boot_err = os.path.join(runtime, "boot_err.log")
    with open(boot_out, "w") as bo, open(boot_err, "w") as be:
        proc = subprocess.Popen(
            [binary], cwd=runtime, env=env,
            stdin=subprocess.PIPE, stdout=bo, stderr=be)
    deadline = time.time() + 60
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("server exited during boot")
        try:
            with open(boot_out) as f:
                if "Server ready" in f.read():
                    return proc, [boot_out, boot_err,
                                  os.path.join(runtime, "server.log")]
        except FileNotFoundError:
            pass
        time.sleep(0.3)
    raise RuntimeError("server never became ready")


def probe_session(host, port, name, seconds=7.0):
    """Connect, spawn, poke an unknown packet id, then idle."""
    c = Client(host, port, name)
    if not c.handshake() or not c.login():
        raise RuntimeError("handshake/login failed")
    c.drain(3.0)
    c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
    c.drain(1.0)
    if c.eid == 0:
        raise RuntimeError("no StartGame")
    c.send_game(bytes([0x77]) + b"\x00" * 8)  # unknown game packet id
    end = time.time() + seconds
    while time.time() < end:
        c.drain(0.25)
    return c


def read_log(paths):
    text = ""
    for path in paths:
        try:
            with open(path, "r", errors="replace") as f:
                text += f.read()
        except FileNotFoundError:
            pass
    return text


def stop(proc):
    try:
        proc.stdin.write(b"stop\n")
        proc.stdin.flush()
    except (BrokenPipeError, OSError):
        pass
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone debug-mode check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19161)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    base = args.port
    runtimes = [tempfile.mkdtemp(prefix="cs_dbgmode_a_"),
                tempfile.mkdtemp(prefix="cs_dbgmode_b_"),
                tempfile.mkdtemp(prefix="cs_dbgmode_c_")]
    try:
        # ---------------- stage 0: default file seeding --------------------
        print("stage: fresh runtime seeds cursestone.yml defaults")
        proc, logs = boot_server(args.server, runtimes[0], base, None)
        stop(proc)
        time.sleep(0.3)
        yml = open(os.path.join(runtimes[0], "cursestone.yml")).read()
        check("debug-mode: false" in yml,
              "fresh cursestone.yml seeds debug-mode: false")
        check("advanced-log" not in yml,
              "seeded file drops the legacy advanced-log key")

        # ---------------- boot 1: default (no debug-mode key) ------------- 
        print("stage: boot with defaults (debug-mode absent)")
        proc, logs = boot_server(args.server, runtimes[1], base + 1, "")
        try:
            probe_session(args.host, base + 1, "QuietGuy", seconds=7.0)
            time.sleep(0.6)  # async logger drain
            text = read_log(logs)
            check("[PKT]" not in text,
                  "no [PKT] packet traces while debug-mode off")
            check("[NET]" not in text,
                  "no [NET] RakNet diagnostics while debug-mode off")
            check("[RakLib][diag]" not in text,
                  "no [RakLib][diag] lines while debug-mode off")
            check("[CHUNKS]" not in text,
                  "no verbose gameplay traces while debug-mode off")
            check("Debug mode: disabled" in text,
                  "boot line reports debug mode state")
        finally:
            stop(proc)
            time.sleep(0.3)

        # ---------------- boot 2: debug-mode true -------------------------
        print("stage: boot with debug-mode: true")
        proc, logs = boot_server(args.server, runtimes[2], base + 2,
                                 "  debug-mode: true\n")
        try:
            probe_session(args.host, base + 2, "ChattyGuy", seconds=9.0)
            time.sleep(0.6)
            text = read_log(logs)
            check("[PKT]" in text,
                  "[PKT] traces appear with debug-mode on")
            check("Debug mode: enabled" in text,
                  "boot line reports debug mode enabled")
            check("[NET]" in text,
                  "[NET] RakNet diagnostics appear with debug-mode on")
        finally:
            stop(proc)
            time.sleep(0.3)
    finally:
        if not args.keep:
            import shutil
            for r in runtimes:
                shutil.rmtree(r, ignore_errors=True)

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
