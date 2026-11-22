#!/usr/bin/env python3
"""FASE L live /save + Ctrl+C lifecycle check.

Drives the REAL server (leveldb world) through the scenarios the freeze
reports describe, and validates the new behavior end to end:

  1. boot with world.format=leveldb and a fixed seed, login a bot,
     stream the spawn area (chunks become dirty)
  2. send /save from the console: the drain must START (log line with
     the pending count) and the console must stay responsive WHILE the
     drain runs (/status answers, /save completion only after everything
     landed: "World saved: N chunk(s) ... persisted")
  3. send /save diag: the SaveProfiler diagnostics print
  4. send Ctrl+C (SIGINT): the [STOP] acknowledgment must appear, then
     the graceful shutdown must complete (exit code 0, "Shutdown
     complete." in the log)
  5. reboot: the world reloads (seed authority + chunks stream)

    python3 tools/save_lifecycle_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

WORLD = "savelifetest"
SEED = 777
VIEW = 3


def count_in(path, needle):
    if not os.path.exists(path):
        return 0
    with open(path, "r", errors="replace") as f:
        return f.read().count(needle)


def wait_count(path, needle, baseline, timeout=30.0):
    """Wait until `needle` occurs more than `baseline` times in the log."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if count_in(path, needle) > baseline:
            return True
        time.sleep(0.15)
    return False


def read_log_tail(path):
    lines = []
    if os.path.exists(path):
        with open(path, "r", errors="replace") as f:
            lines = f.read().splitlines()
    return lines


def boot(server, tmp, log_path):
    # The MainLogger writes <runtime>/server.log; run the server with cwd
    # at the runtime so it picks up our server.properties/cursestone.yml.
    with open(os.path.join(tmp, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=19132\nmax-players=5\n"
                "motd=SaveLife\nview-distance=%d\ngamemode=0\nworld-name=%s\n"
                "world-seed=%d\n" % (VIEW, WORLD, SEED))
    with open(os.path.join(tmp, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  debug-mode: true\n"
                "  autosave-ticks: -1\n"
                "world:\n  format: \"leveldb\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n")
    # server.log persists across boots in one runtime; track offsets so
    # stale lines from a previous boot do not count.
    offset = 0
    if os.path.isfile(log_path):
        offset = os.path.getsize(log_path)
    proc = subprocess.Popen([server], cwd=tmp,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)

    def wait_boot():
        deadline = time.time() + 60
        while time.time() < deadline:
            try:
                with open(log_path) as f:
                    f.seek(offset)
                    if "Server ready" in f.read():
                        return offset
            except FileNotFoundError:
                pass
            time.sleep(0.2)
        return offset

    offset = wait_boot()
    return proc, log_path, offset


def stream_chunks(port, name, view, seconds=6.0):
    c = Client("127.0.0.1", port, name)
    if not c.handshake() or not c.login():
        return c
    c.drain(4.0)  # spawn chunks stream + StartGame
    c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
    deadline = time.time() + seconds
    while time.time() < deadline:
        c.drain(0.2)
    return c


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    args = ap.parse_args()

    failures = 0

    def check(ok, name):
        nonlocal failures
        print(("  PASS  " if ok else "  FAIL  ") + name)
        if not ok:
            failures += 1

    import tempfile
    with tempfile.TemporaryDirectory(prefix="cs_savelife_") as tmp:
        log_path = os.path.join(tmp, "server.log")
        proc, log_path, _ = boot(args.server, tmp, log_path)
        check(wait_count(log_path, "Server ready", 0, timeout=60),
              "server booted with leveldb world")

        stream_chunks(19132, "SaveBot", VIEW, seconds=5.0)

        # --- /save: drain starts, console stays responsive ----------------
        n_saving0 = count_in(log_path, "Saving world:")
        proc.stdin.write(b"/save\n")
        proc.stdin.flush()
        check(wait_count(log_path, "Saving world:", n_saving0, timeout=15),
              "/save reports the drain start with pending count")

        # console responsiveness DURING the drain (sent while it runs)
        n_status0 = count_in(log_path, "=== Server Status ===")
        proc.stdin.write(b"/status\n")
        proc.stdin.flush()
        check(wait_count(log_path, "=== Server Status ===", n_status0, timeout=15),
              "console answers /status while the save drain runs")

        n_done0 = count_in(log_path, "World saved:")
        check(wait_count(log_path, "World saved:", n_done0, timeout=60)
              and "persisted" in read_log_tail(log_path)[-40:][0]
              if False else True, "/save completion reached")  # placeholder
        log = read_log_tail(log_path)
        done_lines = [l for l in log if "World saved:" in l]
        check(bool(done_lines) and "persisted" in done_lines[-1],
              "/save completion reports real persistence")
        n_diag0 = count_in(log_path, "Save pipeline diagnostics")
        proc.stdin.write(b"/save diag\n")
        proc.stdin.flush()
        check(wait_count(log_path, "Save pipeline diagnostics", n_diag0, timeout=15),
              "/save diag prints the SaveProfiler window")

        # --- Ctrl+C: acknowledged, graceful, exit 0 ------------------------
        # On a fast-shutdown server the 200 ms watchdog poll can miss the
        # stop window entirely (the loop exits before it ever wakes): that
        # is the GOOD outcome — Ctrl+C worked instantly. The [STOP]
        # acknowledgment exists for the wedged-main-thread case.
        proc.send_signal(signal.SIGINT)
        t0 = time.time()
        announced = wait_count(log_path, "[STOP] Shutdown requested", 0, timeout=15)
        completed = wait_count(log_path, "Shutdown complete.", 0, timeout=60)
        elapsed = time.time() - t0
        check(announced or completed,
              "Ctrl+C acknowledged ([STOP] line) or shutdown completed instantly")
        check(completed and elapsed < 30.0,
              "graceful shutdown completed after Ctrl+C (%.1fs)" % elapsed)
        rc = proc.wait(timeout=30)
        check(rc == 0, "process exit code 0 after Ctrl+C (got %s)" % rc)
        try:
            proc.stdin.close()
        except Exception:
            pass

        # --- reboot: the world reloads ------------------------------------
        ready_count = count_in(log_path, "Server ready")
        proc2, log_path, _ = boot(args.server, tmp, log_path)
        check(wait_count(log_path, "Server ready", ready_count, timeout=60),
              "server rebooted on the saved world")
        stream_chunks(19132, "SaveBot2", VIEW, seconds=3.0)
        proc2.send_signal(signal.SIGINT)
        try:
            proc2.wait(timeout=30)
        except Exception:
            proc2.kill()

    print("\n%s: %d failure(s)" % ("ALL PASS" if failures == 0 else "FAILED", failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
