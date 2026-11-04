#!/usr/bin/env python3
"""End-to-end global sky light check for CurseStone.

CurseStone ships GLOBAL sky light (the Aquamarine populateSkyLight
semantics: every block reads sky light 15, caves and oceans included —
no shadows anywhere). The client never recomputes light; it renders
exactly the nibbles the server sends in every FullChunkDataPacket, so
the wire is the contract: for every streamed chunk the 16384-byte sky
section must be all 0xFF, on flat terrain AND on the normal generator
(ocean floors, caves, tree canopies — everything bright).

Boots the server twice (flat + normal generator), logs a protocol-84
client in, captures every FullChunkDataPacket and asserts:

  1. chunks actually arrived (no vacuous pass)
  2. every payload is the full 83204-byte column format
  3. data[49152:65536] (sky light) is 0xFF for every single byte

    python3 tools/global_light_wire_check.py [--server /path/to/CurseStone]

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

K_BLOCKS = 32768
K_DATA = 16384
K_SKY = 16384
PAYLOAD_LEN = 83204  # blocks + data + sky + blocklight + heightmap + biome cols + extra
SKY_OFF = K_BLOCKS + K_DATA
SKY_END = SKY_OFF + K_SKY


class LightClient(Client):
    """door_bed_flow_check Client + raw FullChunkData capture."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunks = {}  # (cx, cz) -> payload bytes

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 14:
            cx, cz = struct.unpack(">ii", p[1:9])
            (order,) = struct.unpack_from(">B", p, 9)
            (dlen,) = struct.unpack_from(">i", p, 10)
            data = p[14:14 + max(0, dlen)]
            # ORDER_COLUMNS (0) is the payload that carries the light
            if order == 0:
                self.chunks[(cx, cz)] = data
            return
        super().on_game_packet(p)


def boot_server(binary, runtime, port, generator, env):
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=GlobalLight\nview-distance=6\ngamemode=1\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"%s\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n"
                % generator)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("LightProbe\n")
    return subprocess.Popen(
        [binary], cwd=runtime, env=env,
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone global sky light wire check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19161)
    ap.add_argument("--min-chunks", type=int, default=9)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)

    for generator in ("flat", "normal"):
        runtime = tempfile.mkdtemp(prefix="cs_globallight_%s_" % generator)
        print(f"stage: generator={generator}")
        proc = boot_server(args.server, runtime, args.port, generator, env)
        try:
            ready = False
            deadline = time.time() + 40
            while time.time() < deadline:
                if proc.poll() is not None:
                    break
                try:
                    with open(os.path.join(runtime, "server.log")) as f:
                        if "Server ready" in f.read():
                            ready = True
                            break
                except FileNotFoundError:
                    pass
                time.sleep(0.4)
            if not ready:
                check(False, f"[{generator}] server became ready")
                continue
            check(True, f"[{generator}] server became ready")

            c = LightClient(args.host, args.port, "LightProbe", verbose=args.verbose)
            if not (c.handshake() and c.login()):
                check(False, f"[{generator}] handshake/login")
                continue
            c.drain(4.0)
            c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
            c.drain(1.5)
            if c.eid == 0:
                check(False, f"[{generator}] got StartGame (eid)")
                continue
            try:
                c.request_radius(6)
            except Exception:
                pass  # optional; the spawn stream already carries chunks

            deadline = time.time() + 30.0
            while time.time() < deadline and len(c.chunks) < args.min_chunks:
                c.drain(0.4)

            n = len(c.chunks)
            check(n >= args.min_chunks,
                  f"[{generator}] streamed {n} chunks (min {args.min_chunks})")
            if n:
                bad_len = [k for k, d in c.chunks.items() if len(d) != PAYLOAD_LEN]
                check(not bad_len,
                      f"[{generator}] every payload is the {PAYLOAD_LEN}-byte "
                      f"column format" + (f" (bad: {bad_len[:3]})" if bad_len else ""))

                dark = []
                for key, d in c.chunks.items():
                    if len(d) < SKY_END:
                        continue
                    sky = d[SKY_OFF:SKY_END]
                    if sky != b"\xFF" * K_SKY:
                        bad = next(i for i, b in enumerate(sky) if b != 0xFF)
                        dark.append((key, SKY_OFF + bad, sky[bad]))
                check(not dark,
                      f"[{generator}] sky light section is all 0xFF in every "
                      f"chunk (global light on the wire)"
                      + (f" (first offenders: {dark[:3]})" if dark else ""))

                # Informational: how far the spawn stream reached.
                xs = [k[0] for k in c.chunks]
                zs = [k[1] for k in c.chunks]
                print(f"    info: chunk span x[{min(xs)}..{max(xs)}] "
                      f"z[{min(zs)}..{max(zs)}]")
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
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)
        args.port += 1

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
