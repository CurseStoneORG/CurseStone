#!/usr/bin/env python3
"""Focused chunk-generation throughput benchmark for CurseStone.

Boots a fresh stock server and reads the boot pre-generation line
("Spawn area pregenerated: N chunks in M ms") as the throughput metric:
vanilla behavior over N chunks, measured end to end with the real
pipeline (terrain, caves, populate, light, install).

    python3 tools/gen_bench_check.py [--radius 16] [--workers 8]
                                     [--repeats 1] [--assert]

Passes `world.spawn-pregen-radius` and `settings.async-workers` through
so the same script measures before/after and worker scaling. --assert
enforces a loose regression bound (stock 2-core dev box reference);
CI runners are faster, so the bound only fails on a big slowdown.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

LINE = re.compile(r"Spawn area pregenerated: (\d+) chunks in (\d+) ms")


def boot_and_measure(binary, radius, workers, port, cap=90.0):
    runtime = tempfile.mkdtemp(prefix="cs_genbench_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        # Fixed seed: biome layout varies per random seed and snow-covered
        # spawn changes the populate budget, so before/after comparisons
        # must hold the terrain identical.
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=GenBench\nview-distance=8\ngamemode=1\n"
                "level-seed=12345\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  autosave-ticks: 6000\n"
                "  async-workers: %d\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "  spawn-pregen-radius: %d\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n"
                % (workers, radius))

    log = os.path.join(runtime, "server.log")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(binary)
    proc = subprocess.Popen([binary], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    start = time.time()
    result = None
    try:
        while time.time() - start < cap:
            try:
                with open(log) as f:
                    m = LINE.search(f.read())
                if m:
                    result = (int(m.group(1)), int(m.group(2)))
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.25)
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
        # Keep the temp dir for crash forensics on failure; /tmp cleans itself.
        if result:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)
    if not result:
        return None
    chunks, ms = result
    return chunks, ms, chunks * 1000.0 / ms


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone generation benchmark")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--radius", type=int, default=16)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--port", type=int, default=19178)
    ap.add_argument("--repeats", type=int, default=1)
    ap.add_argument("--assert-cps", type=float, default=0.0,
                    help="fail if chunks/sec falls below this bound")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    samples = []
    for i in range(args.repeats):
        r = boot_and_measure(args.server, args.radius, args.workers,
                             args.port + i)
        if not r:
            print(f"FAIL: pre-generation line never appeared (run {i + 1})")
            return 1
        chunks, ms, cps = r
        samples.append(cps)
        print(f"  run {i + 1}: {chunks} chunks in {ms} ms "
              f"= {cps:.0f} chunks/sec (radius {args.radius}, "
              f"workers {args.workers})")

    best = max(samples)
    print(f"  best: {best:.0f} chunks/sec")
    if args.assert_cps and best < args.assert_cps:
        print(f"  FAIL: {best:.0f} chunks/sec below the "
              f"{args.assert_cps:.0f} chunks/sec bound")
        return 1
    if args.assert_cps:
        print("  PASS  generation throughput above the bound")
    print("ALL PASS" if args.assert_cps else "MEASURED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
