#!/usr/bin/env python3
"""Vanilla equivalence matrix for CurseStone chunk generation.

Runs tools/gen_equiv_check (built by tools/run_gen_equiv.sh or linked from
a build tree) across several seeds and thread configurations and asserts
every TOTAL digest matches the sequential baseline:

  * seeds      — different worlds must each stay self-consistent
  * threads    — 1 (sequential baseline) vs 2/4/8/16 concurrent workers
                 sharing one VgenSource, the same shape the server's
                 AsyncPool runs (terrain cache + spill queues coordinate)
  * noise path — CURSESTONE_NOISE_SCALAR=1 vs the runtime SIMD dispatch

Any digest mismatch means a scheduling or kernel change altered vanilla
output; the script exits 1 and prints the offending pair.

    python3 tools/gen_equiv_matrix.py --tool /tmp/gen_equiv [--radius 4]
                                     [--threads 1,2,4,8,16] [--seeds 12345,1337]
"""
import argparse
import os
import re
import subprocess
import sys

TOTAL = re.compile(r"^TOTAL threads=(\d+) chunks=(\d+) seed=(-?\d+) radius=(\d+) digest=([0-9a-f]{16})$",
                   re.MULTILINE)


def run_tool(tool, seed, radius, threads, scalar=False):
    env = dict(os.environ)
    if scalar:
        env["CURSESTONE_NOISE_SCALAR"] = "1"
    else:
        env.pop("CURSESTONE_NOISE_SCALAR", None)
    proc = subprocess.run([tool, str(seed), str(radius), str(threads)],
                          capture_output=True, text=True, env=env, timeout=600)
    m = TOTAL.search(proc.stdout)
    if not m:
        print(proc.stdout[-2000:])
        print(proc.stderr[-2000:])
        raise RuntimeError("no TOTAL line (seed %s threads %s)" % (seed, threads))
    return m.group(1), m.group(2), m.group(5), proc.stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", default="/tmp/gen_equiv")
    ap.add_argument("--radius", type=int, default=4)
    ap.add_argument("--seeds", default="12345,1337,-777,90210")
    ap.add_argument("--threads", default="1,2,4,8,16")
    args = ap.parse_args()

    if not os.path.isfile(args.tool):
        print(f"FAIL: tool not found: {args.tool}")
        return 1

    seeds = [s.strip() for s in args.seeds.split(",") if s.strip()]
    threads_list = [int(t) for t in args.threads.split(",") if t.strip()]
    failures = 0

    print(f"radius={args.radius} seeds={seeds} threads={threads_list}")
    for seed in seeds:
        # Sequential baseline, SIMD path (the production configuration).
        bt, bc, base_digest, _ = run_tool(args.tool, seed, args.radius, 1)
        print(f"seed {seed}: baseline(threads={bt}) {bc} chunks digest={base_digest}")
        if int(bt) != 1 or int(bc) != (2 * args.radius + 1) ** 2:
            print(f"  FAIL: unexpected baseline shape (threads={bt} chunks={bc})")
            failures += 1

        # Scalar noise path must produce the exact same bytes.
        st, sc, scalar_digest, _ = run_tool(args.tool, seed, args.radius, 1, scalar=True)
        if scalar_digest != base_digest:
            print(f"  FAIL: scalar noise digest {scalar_digest} != simd {base_digest}")
            failures += 1
        else:
            print(f"  scalar-noise path digest matches ({scalar_digest})")

        # Concurrent orders through the real pipeline.
        for t in threads_list:
            if t == 1:
                continue
            ct, cc, digest, _ = run_tool(args.tool, seed, args.radius, t)
            mark = "OK " if digest == base_digest else "FAIL"
            print(f"  {mark} threads={ct:>2}: digest={digest}"
                  + ("" if digest == base_digest else f" != baseline {base_digest}"))
            if digest != base_digest:
                failures += 1

    if failures:
        print(f"FAIL: {failures} digest mismatch(es)")
        return 1
    print("ALL PASS — identical digests across seeds, worker counts and noise paths")
    return 0


if __name__ == "__main__":
    sys.exit(main())
