#!/usr/bin/env python3
"""Traffic pacing drill: a real multi-vector flood with a player inside.

Boots the server with tight traffic budgets (cursestone.yml `traffic.*`
keys), joins an honest player, then fires three flood vectors at it from
localhost at the same time:

  1. ping flood      — UNCONNECTED_PING storms from one socket
  2. handshake flood — OCR1/OCR2 storms from hundreds of rotating ports
  3. garbage flood   — random datagrams from rotating ports

What must hold while the floods run:

  * the honest player keeps chatting ([CHAT] lines reach the log) and its
    session is never closed — connected sessions never cross the per-IP
    budgets, so abuse cannot starve the players already in game;
  * the ping budget caps pong replies (a measured 200-ping burst answers
    about max-pings-per-sec, not 200);
  * the session cap refuses fresh sessions once max-sessions-per-ip is
    exhausted ("session budget exhausted" drops in the log, debug-mode on);
  * the log shows [NET] pacing lines — repeat offenders get silenced;
  * the server stays alive, and after the floods stop, budgets recover:
    pings are answered again and a fresh client can complete login.

    python3 tools/traffic_pacing_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""

import argparse
import os
import random
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

RAKNET_MAGIC = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")
TEXT_PACKET_PID = 0x07  # protocol 84: TEXT rides raw id 0x07 (not 0x85)
TEXT_TYPE_CHAT = 0x01
PING_BUDGET = 30       # must match the yml below (max-pings-per-sec)
SESSION_CAP = 4        # must match the yml below (max-sessions-per-ip)

RUNTIME_YML = """settings:
  debug-mode: true
  async-workers: 2
  autosave-ticks: 6000
world:
  format: "anvil"
  generator: "flat"
traffic:
  pacing: true
  max-packets-per-tick: 250
  max-pings-per-sec: %d
  max-handshakes-per-sec: 25
  max-new-sessions-per-sec: 6
  max-sessions-per-ip: %d
  strikes-before-pacing: 1
  pacing-hold-ms: 1500
  pacing-hold-max-ms: 6000
  pacing-expire-ms: 30000
""" % (PING_BUDGET, SESSION_CAP)

RUNTIME_PROPS = """motd=PacingDrill
server-ip=127.0.0.1
server-port={port}
max-players=8
view-distance=3
world-name=world
"""


def ping_packet():
    return (b"\x01" + struct.pack(">Q", int(time.time() * 1000)) + RAKNET_MAGIC +
            struct.pack(">Q", 0xF10AD5EED))


def ocr1_packet():
    return b"\x05" + RAKNET_MAGIC + b"\x06" + b"\x00" * 1400


def ocr2_packet():
    return (b"\x07" + RAKNET_MAGIC +
            bytes([4]) + bytes([255 - 127, 255 - 0, 255 - 0, 255 - 1]) +
            struct.pack(">H", 19132) + struct.pack(">H", 1400) +
            struct.pack(">Q", random.getrandbits(56)))


def flood_ping(host, port, seconds, counter, stop):
    """UNCONNECTED_PING storm from one socket; counts pongs it gets back.

    Sends in bursts of 100 then drains receipts for 100 ms — a real ping
    flood does not wait for each answer before sending the next ping.
    Rate-capped to a few thousand pps: the budgets trip at 30 pings/s, so
    anything above a few thousand is pure kernel-queue noise that only
    starves the honest player's datagrams without changing what is being
    verified here.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.settimeout(0.02)
    deadline = time.time() + seconds
    while time.time() < deadline and not stop.is_set():
        for _ in range(100):
            try:
                sock.sendto(ping_packet(), (host, port))
                counter[0] += 1
            except OSError:
                break
        time.sleep(0.05)  # ~2000 pps
        drained_end = time.time() + 0.05
        while time.time() < drained_end:
            try:
                data, _ = sock.recvfrom(2048)
                if data and data[0] == 0x1C:
                    counter[1] += 1
            except socket.timeout:
                break
    # Final receipt drain.
    end = time.time() + 0.5
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(2048)
            if data and data[0] == 0x1C:
                counter[1] += 1
        except socket.timeout:
            break
    sock.close()


def flood_handshake(host, port, seconds, counter, stop):
    """OCR1+OCR2 storms, a fresh source port per cycle (rotating-port scan).

    ~8k requests/s: the handshake budget trips at 25/s and the session cap
    at 4, so this saturates both budgets by three orders of magnitude while
    staying inside what the recv loop can ingest without starving the
    honest player's datagrams.
    """
    deadline = time.time() + seconds
    while time.time() < deadline and not stop.is_set():
        for _ in range(40):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
            try:
                sock.sendto(ocr1_packet(), (host, port))
                sock.sendto(ocr2_packet(), (host, port))
                counter[0] += 2
            except OSError:
                sock.close()
                return
            sock.close()
        time.sleep(0.01)  # ~8k datagrams/s


def flood_garbage(host, port, seconds, counter, stop):
    """Random datagrams from rotating ports — pure noise for the drain loop.

    ~5k datagrams/s, same reasoning as the handshake vector.
    """
    deadline = time.time() + seconds
    payload = random.randbytes(96)
    while time.time() < deadline and not stop.is_set():
        for _ in range(50):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                sock.sendto(payload, (host, port))
                counter[0] += 1
            except OSError:
                sock.close()
                return
            sock.close()
        time.sleep(0.01)  # ~5k datagrams/s


def measured_ping_burst(host, port, count, window):
    """Sends `count` pings inside `window` seconds, returns pong replies."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.05)
    pongs = 0
    start = time.time()
    for _ in range(count):
        try:
            sock.sendto(ping_packet(), (host, port))
        except OSError:
            break
        if time.time() - start >= window:
            break
        while True:
            try:
                data, _ = sock.recvfrom(2048)
                if data and data[0] == 0x1C:
                    pongs += 1
            except socket.timeout:
                break
    end = time.time()
    # One last drain for stragglers.
    deadline = time.time() + 0.6
    while time.time() < deadline:
        try:
            data, _ = sock.recvfrom(2048)
            if data and data[0] == 0x1C:
                pongs += 1
        except socket.timeout:
            break
    sock.close()
    return pongs, end - start


def alive(host, port, wait=4.0):
    """True if the server answers an unconnected ping within `wait`."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(wait)
    try:
        sock.sendto(ping_packet(), (host, port))
        data, _ = sock.recvfrom(2048)
        return bool(data) and data[0] == 0x1C
    except OSError:
        return False
    finally:
        sock.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone traffic pacing drill")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19166)
    ap.add_argument("--name", default="HonestBot")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_pacing_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write(RUNTIME_PROPS.format(port=args.port))
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write(RUNTIME_YML)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write(args.name + "\nAfterBot\n")

    stderr_log = open(os.path.join(runtime, "stderr.log"), "wb")
    proc = subprocess.Popen([args.server], cwd=runtime, stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, stderr=stderr_log)
    server_log = os.path.join(runtime, "server.log")
    stderr_path = os.path.join(runtime, "stderr.log")

    def log_text():
        """server.log (MainLogger) + stderr.log ([NET] pacing/drop lines)."""
        parts = []
        for path in (server_log, stderr_path):
            try:
                with open(path, errors="replace") as f:
                    parts.append(f.read())
            except FileNotFoundError:
                pass
        return "\n".join(parts)

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

    try:
        deadline = time.time() + 60
        ready = False
        while time.time() < deadline:
            if proc.poll() is not None:
                break
            if "Server ready" in log_text():
                ready = True
                break
            time.sleep(0.4)
        check(ready, "server booted")
        check("Traffic pacing: on" in log_text(),
              "loadConfig applied the traffic.* budgets (Traffic pacing: on)")
        if not ready:
            print("FAIL: server never became ready")
            return 1

        # ---- honest player joins and plays --------------------------------
        a = Client(args.host, args.port, args.name, verbose=args.verbose)
        if not a.handshake() or not a.login():
            print("FAIL: honest client handshake/login (A)")
            return 1
        a.drain(3.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(2.0)
        check(a.eid != 0, "honest client spawned (eid received)")

        # Baseline chat (reliable-ordered, no flood): proves the chat probe
        # itself works before anything is blamed on the floods.
        msg = b"baseline1"
        inner = (bytes([TEXT_PACKET_PID, TEXT_TYPE_CHAT]) +
                 struct.pack(">H", len(args.name)) + args.name.encode() +
                 struct.pack(">H", len(msg)) + msg)
        a.send_game(inner)
        a.drain(0.6)
        time.sleep(1.0)
        check("baseline1" in log_text(), "baseline chat reaches the log (no flood)")

        # ---- measured ping burst: the budget caps pong replies ------------
        print("stage: measured ping burst (200 pings, budget %d/s)" % PING_BUDGET)
        pongs, took = measured_ping_burst(args.host, args.port, 200, 1.0)
        print(f"        -> {pongs} pongs for 200 pings in {took:.2f}s")
        check(pongs <= PING_BUDGET + 30,
              f"ping budget caps replies ({pongs} <= {PING_BUDGET}+30)")
        check(pongs >= 1, "budget does not silence a first-burst source entirely")
        time.sleep(2.5)  # let the 1.5 s first hold expire before the wave

        # ---- the wave: three vectors at once, honest player inside --------
        print("stage: wave — ping + handshake + garbage floods, 6 s, bot inside")
        counters = {"ping": [0, 0], "hs": [0, 0], "garbage": [0]}
        stop = threading.Event()
        threads = [
            threading.Thread(target=flood_ping,
                             args=(args.host, args.port, 6.0, counters["ping"], stop)),
            threading.Thread(target=flood_handshake,
                             args=(args.host, args.port, 6.0, counters["hs"], stop)),
            threading.Thread(target=flood_garbage,
                             args=(args.host, args.port, 6.0, counters["garbage"], stop)),
        ]
        for t in threads:
            t.start()

        # The player keeps playing under fire: chats every ~1.5 s across the
        # whole wave. Under a flood a few honest datagrams are lost to kernel
        # queue overflow no matter how good the pacing is, so each chat is
        # retried (a real client's reliability layer does the same) and the
        # DELIVERY is asserted after the wave: the server log is written by
        # an async logger whose flush lags under load, so in-flight log
        # reads cannot be the source of truth. What must hold is: every
        # chat sent under fire reaches the log, the session is never
        # closed, and the fresh client joins right after.
        sent_under_fire = []
        for i in (1, 2, 3):
            time.sleep(2.0)
            msg = f"still-here-{i}-under-fire".encode()
            sent_under_fire.append(msg.decode())
            inner = (bytes([TEXT_PACKET_PID, TEXT_TYPE_CHAT]) +
                     struct.pack(">H", len(args.name)) +
                     args.name.encode() + struct.pack(">H", len(msg)) + msg)
            for _ in range(3):  # reliability-style retries for lost datagrams
                a.send_raw(a.send_encapsulated(inner, reliability=0))
                a.drain(0.3)
                time.sleep(0.3)
        for t in threads:
            t.join()
        print(f"        -> sent: pings={counters['ping'][0]} "
              f"handshakes={counters['hs'][0]} garbage={counters['garbage'][0]} "
              f"pongs-back={counters['ping'][1]}")

        time.sleep(3.0)  # let the async logger flush what it queued
        text = log_text()
        delivered = [m for m in sent_under_fire if m in text]
        check(len(delivered) == len(sent_under_fire),
              f"honest chats sent under fire all reach the log "
              f"({len(delivered)}/{len(sent_under_fire)})")

        text = log_text()
        check(proc.poll() is None, "server process survived the wave")
        check("[NET] pacing" in text, "budgets escalated to pacing ([NET] pacing lines)")
        check("session budget exhausted" in text,
              "session cap refused fresh sessions during the wave")
        check(counters["ping"][1] * 10 < max(1, counters["ping"][0]),
              "paced wave: fewer than 1 in 10 pings got a pong back")
        check(("Closing player " + args.name + " ") not in text,
              "honest session was never closed by the floods")

        # ---- recovery ------------------------------------------------------
        print("stage: recovery — budgets expire, fresh client joins")
        time.sleep(10.0)  # hold ceiling (6 s) + zombie session timeout (5 s)
        check(alive(args.host, args.port), "pings answered again after recovery")
        b = Client(args.host, args.port, "AfterBot", verbose=args.verbose)
        joined = b.handshake() and b.login()
        if joined:
            b.drain(3.0)
            b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
            b.drain(2.0)
        check(joined and b.eid != 0, "fresh client completes login after the floods")
        check(proc.poll() is None, "server still alive at the end")
        console("stop")
        a.stop_keepalive()
        if joined:
            b.stop_keepalive()
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
