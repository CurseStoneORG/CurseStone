#!/usr/bin/env python3
"""Measure FullChunkDataPacket wire sizes + re-stream rate after a
radius raise (radius 2 -> 8) to calibrate the chunk byte budget."""
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chunk_explore_check import ExploreClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

SERVER = "/home/z/my-project/build/cs-rel/CurseStone"
runtime = tempfile.mkdtemp(prefix="cs_probe_")
port = 19181
with open(os.path.join(runtime, "server.properties"), "w") as f:
    f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
            "motd=P\nview-distance=8\ngamemode=0\n" % port)
with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
    f.write("settings:\n  async-workers: 2\n"
            "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
            "memory:\n  max-mb: 512\n")
env = dict(os.environ)
env["LD_LIBRARY_PATH"] = os.path.dirname(SERVER)
boot = open(os.path.join(runtime, "out.log"), "w")
proc = subprocess.Popen([SERVER], cwd=runtime, env=env, stdin=subprocess.PIPE,
                        stdout=boot, stderr=subprocess.DEVNULL)
log = os.path.join(runtime, "out.log")
for _ in range(120):
    try:
        if "Server ready" in open(log).read():
            break
    except FileNotFoundError:
        pass
    time.sleep(0.4)

c = ExploreClient("127.0.0.1", port, "Cal")
sizes = {}
_orig_on_game = c.on_game_packet

def sized_on_game(p: bytes):
    if p and p[0] == 0x34:
        sizes[len(p)] = sizes.get(len(p), 0) + 1
    _orig_on_game(p)

c.on_game_packet = sized_on_game
c.handshake(); c.login(); c.drain(3.0)
c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION); c.drain(1.0)
c.request_radius(8); c.drain(0.5)
t0 = time.time()
sizes = {}
while time.time() - t0 < 20 and len(set(c.chunk_sends)) < 289:
    c.drain(0.25)
for p in []:
    sizes[len(p)] = sizes.get(len(p), 0) + 1
ks = sorted(sizes)
print("initial stream: unique=%d in %.1fs" % (len(set(c.chunk_sends)), time.time() - t0))
print("payload size histogram (bytes: count):")
for k in ks[:6] + (["..."] if len(ks) > 12 else []) + ks[-6:]:
    print("  ", k, ":", sizes.get(k, ""))

# now lower / raise and measure the re-stream rate
c.request_radius(2); c.drain(1.0)
n = len(c.chunk_sends)
c.request_radius(8)
t1 = time.time()
rates = []
while time.time() - t1 < 20:
    c.drain(0.5)
    rates.append((round(time.time() - t1, 1), len(c.chunk_sends) - n))
print("re-send timeline (s: cumulative):", rates[::4])
print("total re-sent:", len(c.chunk_sends) - n)

try:
    proc.stdin.write(b"stop\n"); proc.stdin.flush()
except Exception:
    pass
proc.wait(timeout=15)
import shutil
shutil.rmtree(runtime, ignore_errors=True)
