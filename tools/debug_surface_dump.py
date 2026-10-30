#!/usr/bin/env python3
"""Debug: boot a server, capture chunks, dump surface stats."""
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

FULL_CHUNK_DATA = 0x34
REQUEST_CHUNK_RADIUS = 0x3D
MOVE_PLAYER = 0x10
EYE_HEIGHT = 1.62


def wait_boot(proc, server_log, deadline_s=60):
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        try:
            with open(server_log) as f:
                if "Server ready" in f.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.4)
    return False


class C(Client):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.chunks = {}

    def on_game_packet(self, p: bytes):
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 14:
            cx, cz = struct.unpack(">ii", p[1:9])
            (dlen,) = struct.unpack(">i", p[10:14])
            if dlen > 0 and len(p) >= 14 + dlen:
                self.chunks[(cx, cz)] = p[14:14 + dlen]
            return
        super().on_game_packet(p)


def main():
    runtime = tempfile.mkdtemp(prefix="cs_dbg_")
    port = 19142
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=Dbg\nview-distance=8\ngamemode=1\nadvanced-log=true\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\nworld:\n  format: \"anvil\"\n"
                "  generator: \"normal\"\n")
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = "/home/z/my-project/build/cs-rel"
    proc = subprocess.Popen(["/home/z/my-project/build/cs-rel/CurseStone"],
                            cwd=runtime, env=env, stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log = os.path.join(runtime, "server.log")
    try:
        assert wait_boot(proc, log), "boot"
        c = C("127.0.0.1", port, "DbgTester")
        assert c.handshake() and c.login(), "login"
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        print(f"eid={c.eid} spawn={c.spawn}")
        for sx, sz in [(0, 0), (320, 0), (0, 320), (320, 320)]:
            c.send_game(bytes([MOVE_PLAYER]) + struct.pack(">q", c.eid) +
                        struct.pack(">fff", sx + 0.5, 95, sz + 0.5) +
                        struct.pack(">fff", 0, 0, 0) + bytes([0, 0]))
            c.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", 6))
            c.drain(9.0)
        print(f"chunks={len(c.chunks)}")
        hist = {}
        maxy = 0
        for (cx, cz), blob in c.chunks.items():
            for lx in range(16):
                for lz in range(16):
                    base = (lx << 11) | (lz << 7)
                    for y in range(127, 0, -1):
                        bid = blob[base + y]
                        if bid != 0:
                            hist[bid] = hist.get(bid, 0) + 1
                            maxy = max(maxy, y)
                            break
        top = sorted(hist.items(), key=lambda kv: -kv[1])[:12]
        print("surface histogram:", top)
        print("max surface y:", maxy)
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        time.sleep(1.0)
        proc.terminate()


if __name__ == "__main__":
    main()
