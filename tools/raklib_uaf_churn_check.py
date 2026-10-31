#!/usr/bin/env python3
"""RakLib UAF hotfix — network churn harness against a REAL server binary.

Drives the post-hotfix RakLib session teardown with the exact scenarios of
CURSESTONE_RAKLIB_UAF_INVESTIGATION.md §5/§9, against a server built with
AddressSanitizer+UBSan (build/cs-uaf-asan/CurseStone). Pre-fix, every one of
S1/S2/S3 leaves a dangling endpoint-index entry that the very next datagram
dereferences; ASan reports heap-use-after-free on the first access. The
post-fix server must survive ALL of them with zero sanitizer output.

  S1  KICK -> late ACK/DATA/OCR1 from the same socket      (x12)
      login -> chunk traffic -> console `kick` -> in the ~400ms window:
      multi-record 0xc0 ACK of server seqs seen + late 0x84 (reliability 3,
      high seq, harmless game id) + fresh OCR1 0x05.
  S2  REBIND the same client source port                   (x8)
      handshake + invalid-name login (server-side kick), close the socket,
      re-bind the SAME source port, full valid handshake+login again.
  S3  BAD-BATCH close + traffic in flight                  (x8)
      malformed batch (server closes on purpose) and, without pausing,
      more game traffic from the same endpoint, then late ACK/DATA.
  D3  endpoint churn + responsiveness watchdog             (40 cycles)
      40x (fresh ephemeral port -> handshake -> invalid-name kick), then an
      unconnected ping with a 5s watchdog and one full valid login.

After EVERY scenario the server must still answer an unconnected ping.
At the end the collected server stderr must contain no
  "ERROR: AddressSanitizer" / "runtime error:" (UBSan)
and the process must still be alive.

Usage:
    python3 tools/raklib_uaf_churn_check.py [--server build/cs-uaf-asan/CurseStone]
                                            [--host 127.0.0.1] [--port 19158]
"""
import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from linux_login_check import (Client, MAGIC, UNCONNECTED_PING, UNCONNECTED_PONG,
                               OCR1, ACK, DATA_FIRST, DATA_LAST)

BAD_BATCH = bytes([0x06]) + struct.pack(">I", 4) + b"\xde\xad\xbe\xef"


def alive(host: str, port: int, wait: float = 3.0) -> bool:
    """True if the server answers an unconnected ping within `wait`."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(wait)
    try:
        s.sendto(bytes([UNCONNECTED_PING]) + struct.pack("<Q", int(time.time() * 1000)) + MAGIC,
                 (host, port))
        data, _ = s.recvfrom(2048)
        return bool(data) and data[0] == UNCONNECTED_PONG
    except OSError:
        return False
    finally:
        s.close()


class BoundClient(Client):
    """Client whose UDP socket is bound to a FIXED local source port (S2)."""

    def __init__(self, host: str, port: int, name: str, bind_port: int):
        super().__init__(host, port, name)
        self.sock.close()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
        except OSError:
            pass
        self.sock.settimeout(0.4)
        self.sock.bind(("127.0.0.1", bind_port))


def multi_ack(seqs) -> bytes:
    """One 0xc0 ACK datagram with a single-range record per seq.

    Wire format (per SessionManager.cpp handle_ack):
      [0xC0][count u16 BE] then per record: [0x01][seq u24 LE]
    """
    body = b""
    n = 0
    for s in seqs[-64:]:
        body += b"\x01" + bytes([s & 0xFF, (s >> 8) & 0xFF, (s >> 16) & 0xFF])
        n += 1
    return bytes([ACK]) + struct.pack(">H", n) + body


def collect_and_ack(c: Client, seconds: float, seen: list):
    """Receive for `seconds`, remember server DATA seqs, ACK each one."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        data = c.recv_one(0.2)
        if data is None:
            continue
        if DATA_FIRST <= data[0] <= DATA_LAST:
            seq = data[1] | (data[2] << 8) | (data[3] << 16)
            if seq not in seen:
                seen.append(seq)
            rel = (data[4] >> 5) & 0x07
            if rel in (2, 3, 4, 6, 7):
                c.send_ack(seq)
        for payload, is_split, count, sid, idx in c.parse_datagram(data):
            if not payload:
                continue
            if is_split:
                whole = c.add_fragment(payload, count, sid, idx)
                if whole:
                    c.handle_payload(whole)
            else:
                c.handle_payload(payload)


def late_data(c: Client, game_id: int):
    """A late DATA 0x84 with a jump in sequence + reliability 3."""
    c.send_seq += 250  # deliberately HIGH seq
    c.send_raw(c.send_encapsulated(bytes([game_id]) + b"\x00" * 8, reliability=3))


def ocr1(c: Client):
    c.send_raw(bytes([OCR1]) + MAGIC + b"\x06" + b"\x00" * 1400)


def main() -> int:
    ap = argparse.ArgumentParser(description="RakLib UAF churn check (ASan server)")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-uaf-asan/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19158)
    ap.add_argument("--keep", action="store_true", help="keep the runtime dir")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_uaf_churn_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=10\n"
                "motd=UafChurn\nview-distance=3\ngamemode=1\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("UafChurner\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  debug-mode: false\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 1024\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(os.path.abspath(args.server))
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    stderr_log = open(os.path.join(runtime, "stderr.log"), "wb")
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=stderr_log)
    server_log = os.path.join(runtime, "server.log")
    stderr_path = os.path.join(runtime, "stderr.log")

    def log_text():
        try:
            with open(server_log) as f:
                return f.read()
        except FileNotFoundError:
            return ""

    def console(cmd):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass

    results = []  # (scenario, ok, note)

    def scenario(label, fn):
        print(f"[{label}]")
        try:
            ok, note = fn()
        except Exception as e:
            ok, note = False, f"exception: {e}"
        results.append((label, ok, note))
        print(f"  -> {'PASS' if ok else 'FAIL'} {note}")
        if not alive(args.host, args.port):
            ok2 = False
            print("  -> FAIL server stopped answering pings right after the scenario")
        else:
            ok2 = True
        results.append((label + "+ping", ok2, "unconnected ping"))
        print()

    def s1_once(i):
        name = f"KickMe{i}"
        c = Client(args.host, args.port, name)
        try:
            if c.handshake():
                return False, f"handshake failed at {c.handshake()}"
            if c.login():
                return False, "login not answered"
            c.drain(1.5)                       # chunk traffic
            seen = []
            collect_and_ack(c, 0.4, seen)      # record server seqs
            console(f"kick {name}")
            time.sleep(0.05)
            # --- the ~400ms window after the kick, SAME socket ------------
            if seen:
                c.send_raw(multi_ack(seen))    # (1) late ACK of server seqs
            late_data(c, 0x01)                 # (2) late DATA, reliability 3
            ocr1(c)                            # (3) fresh OCR1, same endpoint
            c.drain(0.6)
            return True, f"acked={len(seen)} seqs, late 0x84+OCR1 sent"
        finally:
            c.sock.close()

    def s2_once(i):
        sport = 41000 + i
        name = f"Rebind{i}"
        c1 = BoundClient(args.host, args.port, name, sport)
        try:
            if c1.handshake():
                return False, f"first handshake failed at {c1.handshake()}"
            c1.name = "ab"                     # invalid -> server-side kick
            c1.login()
            time.sleep(0.35)                   # let the teardown drain
            c1.drain(0.4)
        finally:
            c1.sock.close()
        # re-bind the SAME source port: post-fix this must be a clean slate
        c2 = BoundClient(args.host, args.port, name + "ok", sport)
        try:
            if c2.handshake():
                return False, f"REBIND handshake failed at {c2.handshake()}"
            if c2.login():
                return False, "REBIND valid login not answered"
            c2.drain(0.5)
            return True, f"same source port {sport} usable after kick"
        finally:
            c2.sock.close()

    def s3_once(i):
        name = f"BadBatch{i}"
        c = Client(args.host, args.port, name)
        try:
            if c.handshake():
                return False, f"handshake failed at {c.handshake()}"
            if c.login():
                return False, "login not answered"
            c.drain(1.0)
            c.send_raw(c.send_encapsulated(BAD_BATCH, reliability=3))
            # in-flight traffic immediately after the bad batch (the close
            # is still being processed): things that would elicit replies
            c.send_raw(c.send_encapsulated(
                bytes([0x00]) + struct.pack(">Q", int(time.time() * 1000)), reliability=2))
            c.send_raw(c.send_encapsulated(bytes([0x01]) + b"\x00" * 8, reliability=3))
            c.send_raw(c.send_encapsulated(
                bytes([0x35]) + struct.pack(">iii", 0, 64, 0), reliability=3))
            time.sleep(0.3)
            c.send_raw(multi_ack([0, 1, 2, 3]))  # late ACKs
            late_data(c, 0x01)                   # late DATA, high seq
            c.drain(0.6)
            return True, "bad batch + 3 in-flight + late ACK/0x84 survived"
        finally:
            c.sock.close()

    def d3():
        t0 = time.time()
        for i in range(40):
            c = Client(args.host, args.port, f"D3Churn{i}")
            try:
                if c.handshake():
                    return False, f"cycle {i}: handshake failed at {c.handshake()}"
                c.name = "ab"
                c.login()                      # invalid -> server-side kick
                time.sleep(0.05)
                c.drain(0.25)
            finally:
                c.sock.close()
        churn_s = time.time() - t0
        if not alive(args.host, args.port, wait=5.0):
            return False, f"40 kick cycles in {churn_s:.0f}s then NO ping within 5s (watchdog)"
        c = Client(args.host, args.port, "D3Final")
        try:
            if c.handshake():
                return False, f"post-churn handshake failed at {c.handshake()}"
            if c.login():
                return False, "post-churn valid login not answered"
            c.drain(0.5)
            return True, f"40 kick cycles in {churn_s:.0f}s, ping + valid login ok"
        finally:
            c.sock.close()

    try:
        # ---- boot --------------------------------------------------------
        deadline = time.time() + 60
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server died during boot")
                print(open(stderr_path, errors="replace").read()[-3000:])
                return 1
            if "Server ready" in log_text():
                break
            time.sleep(0.4)
        else:
            print("FAIL: server never became ready")
            return 1
        if not alive(args.host, args.port):
            print("FAIL: server up but does not answer pings")
            return 1
        print(f"server ready (pid {proc.pid}), churn begins\n")

        for i in range(12):
            scenario(f"S1 kick->late traffic x12 [{i}]", lambda i=i: s1_once(i))
        for i in range(8):
            scenario(f"S2 rebind same port x8 [{i}]", lambda i=i: s2_once(i))
        for i in range(8):
            scenario(f"S3 bad-batch + in-flight x8 [{i}]", lambda i=i: s3_once(i))
        scenario("D3 endpoint churn x40 + watchdog", d3)
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        time.sleep(0.5)

    stderr_text = ""
    try:
        stderr_text = open(stderr_path, errors="replace").read()
    except FileNotFoundError:
        pass

    sanitizer_hits = []
    for marker in ("ERROR: AddressSanitizer", "runtime error:", "ERROR: LeakSanitizer"):
        if marker in stderr_text:
            sanitizer_hits.append(marker)

    print("=" * 60)
    for label, ok, note in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {label}: {note}")
    print("=" * 60)

    rc = 0
    if sanitizer_hits:
        rc = 1
        print(f"FAILED: sanitizer output in server stderr: {sanitizer_hits}")
        idx = stderr_text.find("ERROR: AddressSanitizer")
        if idx < 0:
            idx = stderr_text.find("runtime error:")
        if idx < 0:
            idx = 0
        print("---- server stderr (sanitizer report excerpt) ----")
        print(stderr_text[idx:idx + 4000])
        print("--------------------------------------------------")
    failed = [l for l, ok, _ in results if not ok]
    if failed:
        rc = rc or 1
        print(f"FAILED scenarios: {len(failed)}: {failed}")
    if rc == 0:
        print("ALL SCENARIOS PASSED (no sanitizer reports)")
    if args.keep:
        print(f"runtime kept at {runtime}")
    else:
        shutil.rmtree(runtime, ignore_errors=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
