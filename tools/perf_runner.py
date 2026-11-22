#!/usr/bin/env python3
"""Focused Phase I profiling runner + A4 stability check for CurseStone.

Boots the real server with a fixed seed at 1/2/4/8 async workers, captures
the [Perf] pregen scaling / locks / stage-breakdown lines from server.log,
and samples RSS during the run. Keeps every log (unlike gen_bench_check.py)
and prints a compact summary table.

  python3 tools/perf_runner.py --server /tmp/cs_build/CurseStone --radius 12
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

PREGEN = re.compile(r"Spawn area pregenerated: (\d+) chunks in (\d+) ms")
SCALING = re.compile(r"\[Perf\] pregen scaling: (.*)")
LOCKS = re.compile(r"\[Perf\] pregen locks: (.*)")
STAGE = re.compile(r"\[Perf\] (spawn pregen): (.*)")


def rss_of(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) // 1024
    except OSError:
        pass
    return 0


def boot(binary, workers, radius, port, cap=240.0):
    runtime = tempfile.mkdtemp(prefix="cs_perf_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=PerfRun\nview-distance=8\ngamemode=1\n"
                "level-seed=12345\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  autosave-ticks: 6000\n"
                "  async-workers: %d\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "  spawn-pregen-radius: %d\n"
                "memory:\n  max-mb: 1024\n  gc-interval-ticks: 200\n"
                % (workers, radius))
    log = os.path.join(runtime, "server.log")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(binary)
    proc = subprocess.Popen([binary], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    peak_rss = 0
    stop = threading.Event()

    def sampler():
        nonlocal peak_rss
        while not stop.is_set():
            peak_rss = max(peak_rss, rss_of(proc.pid))
            time.sleep(0.2)

    th = threading.Thread(target=sampler, daemon=True)
    th.start()
    start = time.time()
    result = None
    lines = []
    try:
        while time.time() - start < cap:
            try:
                with open(log) as f:
                    text = f.read()
                m = PREGEN.search(text)
                if m:
                    time.sleep(0.5)
                    with open(log) as f:
                        text = f.read()
                    lines = [l for l in text.splitlines() if "[Perf]" in l]
                    result = (int(m.group(1)), int(m.group(2)))
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.25)
    finally:
        stop.set()
        th.join(timeout=1)
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
    if result:
        shutil.copy(log, "/tmp/perf_logs/w%d_server.log" % workers)
        shutil.rmtree(runtime, ignore_errors=True)
    return result, lines, peak_rss


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--radius", type=int, default=12)
    ap.add_argument("--workers", default="1,2,4,8")
    ap.add_argument("--port", type=int, default=19220)
    os.makedirs("/tmp/perf_logs", exist_ok=True)
    args = ap.parse_args()

    port = args.port
    for w in [int(x) for x in args.workers.split(",")]:
        res, lines, peak = boot(args.server, w, args.radius, port)
        port += 1
        if not res:
            print(f"workers={w}: TIMEOUT (no pregen report in cap)")
            continue
        chunks, ms = res
        print(f"== workers={w} ==")
        print(f"  pregen {chunks} chunks in {ms} ms "
              f"({chunks * 1000.0 / ms:.1f} chunks/s) peak_rss={peak}MB")
        for l in lines:
            print("  " + l)


if __name__ == "__main__":
    main()
