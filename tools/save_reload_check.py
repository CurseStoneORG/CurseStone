#!/usr/bin/env python3
"""A4 focused save/reload check: boot -> pregen -> graceful stop -> digest
all region files; boot again on the same world -> pregen -> stop -> digest.
Region payload digests must match (chunks reload and re-save identically),
and both boots must complete their pregen without errors/deadlocks.
"""
import glob
import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

PREGEN = re.compile(r"Spawn area pregenerated: (\d+) chunks in (\d+) ms")
BAD = re.compile(r"ERROR|FATAL|deadlock|assert|Segmentation|corrupt", re.IGNORECASE)


def boot_stop(runtime, binary, port, radius=6, cap=120.0):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(binary)
    proc = subprocess.Popen([binary], cwd=runtime, env=env,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    start = time.time()
    ok = None
    log = os.path.join(runtime, "server.log")
    try:
        while time.time() - start < cap:
            try:
                with open(log) as f:
                    text = f.read()
                m = PREGEN.search(text)
                if m:
                    ok = (int(m.group(1)), int(m.group(2)))
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.2)
        time.sleep(2.0)  # let the post-pregn report land and the tick loop settle
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
            return None, "SIGKILL needed (no graceful exit)"
    if ok is None:
        return None, "pregen timeout"
    return ok, None


def digest_world(world_dir):
    """Digest ONLY the decompressed chunk payloads (the .mca 8KB header
    carries per-chunk save timestamps and level.dat carries Time/last-played,
    both legitimately change between boots)."""
    import struct
    import zlib
    h = hashlib.sha256()
    count = 0
    files = sorted(glob.glob(os.path.join(world_dir, "region", "*.mca")) +
                   glob.glob(os.path.join(world_dir, "region", "*.bin")))
    for p in files:
        h.update(os.path.basename(p).encode())
        with open(p, "rb") as f:
            data = f.read()
        if p.endswith(".bin"):
            h.update(data)
            continue
        # .mca: 1024 offset entries, 1024 timestamp entries, then records:
        # 4B length, 1B compression, payload. Digest the DECOMPRESSED payload.
        for i in range(1024):
            off = struct.unpack(">I", b"\x00" + data[i * 4:i * 4 + 3])[0]
            if off == 0:
                continue
            rec = off * 4096
            ln = struct.unpack(">I", data[rec:rec + 4])[0]
            comp = data[rec + 4]
            payload = data[rec + 5:rec + 4 + ln]
            if comp == 2:
                payload = zlib.decompress(payload)
            h.update(payload)
            count += 1
    return h.hexdigest()[:16], count


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cs_build/CurseStone"
    runtime = tempfile.mkdtemp(prefix="cs_savereload_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=19311\nmax-players=5\n"
                "motd=SaveReload\nview-distance=8\ngamemode=1\nlevel-seed=12345\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  autosave-ticks: 6000\n  async-workers: 4\n"
                "world:\n  format: \"anvil\"\n  generator: \"normal\"\n"
                "  spawn-pregen-radius: 6\nmemory:\n  max-mb: 1024\n")

    r1, err1 = boot_stop(runtime, binary, 19311)
    if not r1:
        print("FAIL boot1:", err1)
        print(open(os.path.join(runtime, "server.log")).read()[-2000:])
        return 1
    d1, n1 = digest_world(os.path.join(runtime, "worlds", "world"))
    log1 = open(os.path.join(runtime, "server.log")).read()
    bad1 = [l for l in log1.splitlines() if BAD.search(l)]

    r2, err2 = boot_stop(runtime, binary, 19311)
    if not r2:
        print("FAIL boot2:", err2)
        print(open(os.path.join(runtime, "server.log")).read()[-2000:])
        return 1
    d2, n2 = digest_world(os.path.join(runtime, "worlds", "world"))
    log2 = open(os.path.join(runtime, "server.log")).read()
    bad2 = [l for l in log2.splitlines() if BAD.search(l)]

    print(f"boot1 pregen {r1[0]} chunks {r1[1]}ms; region files {n1}; digest {d1}")
    print(f"boot2 pregen {r2[0]} chunks {r2[1]}ms; region files {n2}; digest {d2}")
    if bad1:
        print("boot1 suspicious lines:", bad1[:5])
    if bad2:
        print("boot2 suspicious lines:", bad2[:5])
    same = d1 == d2 and n1 == n2
    print("SAVE/RELOAD:", "PASS — regions byte-identical after reload+resave"
          if same else "FAIL — digests differ")
    shutil.copy(os.path.join(runtime, "server.log"), "/tmp/perf_logs/savereload_boot2.log")
    shutil.rmtree(runtime, ignore_errors=True)
    return 0 if same and not bad1 and not bad2 else 1


if __name__ == "__main__":
    sys.exit(main())
