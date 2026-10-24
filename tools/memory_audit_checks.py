#!/usr/bin/env python3
"""Malformed-input survival + session-leak checks for a local CurseStone server.

Every scenario drives a real RakNet client (reuses the harness in
linux_login_check.py) and then asserts the server is still alive and
answering unconnected pings. These reproduce the memory-safety findings of
docs/audits/memory-audit.md:

  playerlist_bomb   0x38 with count=0x7FFFFFFF must be dropped, not
                    reserve() ~2.1e9 entries (pre-fix: std::bad_alloc ->
                    std::terminate).
  zlib_bomb         batch (0x06) whose zlib stream inflates to 12 MiB must
                    be dropped by the 8 MiB output cap (pre-fix: unbounded
                    streaming inflate, pre-auth).
  nbt_depth_bomb    0x32 namedtag of ~20k nested compounds must fail the
                    parse instead of recursing to stack exhaustion.
  far_remove_block  RemoveBlockPacket (0x12) at |x|~2e9 must be ignored
                    (pre-fix: synchronous chunk generation at attacker
                    coords + region-file growth).
  split_mismatch    split fragments that change split_count mid-message
                    must be dropped (pre-fix: segments stored past
                    slot->count leaked on every free path).
  invalid_name_churn  N logins with an invalid name must close the RakNet
                    session each time (pre-fix: session stayed open and
                    kept a player slot alive); after the churn a valid
                    login must still succeed.
  ocr_flood_light   a burst of unauthenticated 0x05 datagrams must leave
                    the server responsive.

Also usable as a leak-cycle driver: --cycles N runs N connect/invalid-name
churn cycles (each ends in a server-initiated kick -> exercises the
session-teardown path) for a server running under LeakSanitizer.

Usage:
    python3 tools/memory_audit_checks.py [--host 127.0.0.1] [--port 19132]
                                         [--cycles N] [--name PREFIX]
"""
import argparse
import struct
import sys
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import (Client, MAGIC, UNCONNECTED_PING, UNCONNECTED_PONG,
                               OCR1, DATA_FIRST)

PLAYER_LIST = 0x38
BLOCK_ENTITY_DATA = 0x32
REMOVE_BLOCK = 0x12
BATCH = 0x06
LOGIN = 0x01


def alive(host: str, port: int, wait: float = 3.0) -> bool:
    """True if the server answers an unconnected ping."""
    import socket
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


def new_client(host: str, port: int, name: str) -> Client:
    c = Client(host, port, name)
    stage = c.handshake()
    if stage:
        raise RuntimeError(f"handshake failed at {stage}")
    return c


def check(label: str, host: str, port: int) -> bool:
    ok = alive(host, port)
    print(f"  [{'PASS' if ok else 'FAIL'}] server still answers pings after {label}")
    return ok


def login_valid(c: Client) -> bool:
    return c.login() == ""


def sc_playerlist_bomb(host, port, name):
    c = new_client(host, port, name)
    try:
        body = bytes([PLAYER_LIST, 0]) + struct.pack(">i", 0x7FFFFFFF)
        c.send_raw(c.send_encapsulated(body, reliability=3))
        time.sleep(0.4)
        c.drain(0.8)
        return check("player-list bomb", host, port)
    finally:
        c.sock.close()


def sc_zlib_bomb(host, port, name):
    c = new_client(host, port, name)
    ok = True
    try:
        bomb = zlib.compress(b"\x00" * (12 * 1024 * 1024), 9)
        body = bytes([BATCH]) + struct.pack(">I", len(bomb)) + bomb
        c.send_raw(c.send_encapsulated(body, reliability=3))
        time.sleep(0.4)
        c.drain(1.0)
        ok = check("12 MiB zlib bomb", host, port)
    finally:
        c.sock.close()
    # A malformed batch makes the server close that session on purpose
    # ("bad batch"); the decode path must still serve a FRESH client.
    c2 = new_client(host, port, name + "z")
    try:
        ok = ok and login_valid(c2)
        print(f"  [{'PASS' if ok else 'FAIL'}] fresh-client login after bomb")
        return ok
    finally:
        c2.sock.close()


def sc_nbt_depth_bomb(host, port, name):
    c = new_client(host, port, name)
    try:
        # ~20000 nested TAG_Compound headers (0x0A + empty name) = 60 KB,
        # one datagram. Pre-fix this recursed ~20k frames deep.
        tag = (b"\x0a\x00\x00" * 20000) + b"\x00"
        body = struct.pack(">iii", 100, 64, 100) + tag
        c.send_raw(c.send_encapsulated(bytes([BLOCK_ENTITY_DATA]) + body, reliability=3))
        time.sleep(0.4)
        c.drain(1.0)
        return check("NBT depth bomb", host, port)
    finally:
        c.sock.close()


def sc_far_remove_block(host, port, name):
    c = new_client(host, port, name)
    try:
        body = struct.pack(">q", 1) + struct.pack(">i", 2000000000) + \
               struct.pack(">i", -2000000000) + bytes([70])
        for k in range(6):
            c.send_raw(c.send_encapsulated(bytes([REMOVE_BLOCK]) + body, reliability=3))
        time.sleep(0.4)
        c.drain(1.0)
        return check("far RemoveBlock flood", host, port)
    finally:
        c.sock.close()


def sc_split_mismatch(host, port, name):
    """Fragments that change split_count must be dropped, then a bad batch
    forces a server-side session close (the path whose old free loop leaked
    the out-of-count segments)."""
    c = new_client(host, port, name)
    try:
        sid = 0x0AAA
        seq = 100
        mi = 500
        payload = b"S" * 300

        def frag(count, idx):
            nonlocal seq, mi
            rec = bytes([0x30, 0x07, 0xD0])  # reliability 2, has_split, bitlen 2000
            rec += struct.pack(">I", mi)[:3]
            # split fields are BE: count i32, id u16, index i32
            rec += struct.pack(">I", count) + struct.pack(">H", sid) + struct.pack(">I", idx)
            mi += 1
            d = bytes([0x84, seq & 0xFF, (seq >> 8) & 0xFF, (seq >> 16) & 0xFF]) + rec + payload
            seq += 1
            c.sock.sendto(d, (host, port))

        frag(200, 0)                      # create slot with count=200
        for idx in range(200, 256):       # mismatched fragments past slot->count
            frag(256, idx)
        time.sleep(0.3)
        # Force a server-side close of this session: an invalid batch.
        bad = bytes([BATCH]) + struct.pack(">I", 4) + b"\xde\xad\xbe\xef"
        c.send_raw(c.send_encapsulated(bad, reliability=3))
        time.sleep(0.5)
        c.drain(0.8)
        return check("split-count mismatch flood + forced close", host, port)
    finally:
        c.sock.close()


def sc_invalid_name_churn(host, port, name, cycles=12):
    ok = True
    for i in range(cycles):
        c = new_client(host, port, f"{name}{i}")
        c.name = "ab"  # too short -> server kicks and must close the session
        c.login()
        time.sleep(0.1)
        c.drain(0.4)
        c.sock.close()
    # After the churn a normal login must still be possible.
    c = new_client(host, port, name + "ok")
    try:
        ok = login_valid(c)
        print(f"  [{'PASS' if ok else 'FAIL'}] valid login after {cycles} invalid-name cycles")
        return ok and check("invalid-name churn", host, port)
    finally:
        c.sock.close()


def sc_ocr_flood(host, port, name):
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        for _ in range(150):
            s.sendto(bytes([OCR1]) + MAGIC + b"\x06" + b"\x00" * 1400, (host, port))
        time.sleep(0.5)
        return check("OCR1 flood burst", host, port)
    finally:
        s.close()


def run_cycles(host, port, name, cycles):
    """Leak-cycle driver for a server under LeakSanitizer."""
    for i in range(cycles):
        c = new_client(host, port, f"{name}{i}")
        c.name = "ab"
        c.login()          # invalid name -> server kick -> session teardown
        time.sleep(0.05)
        c.drain(0.3)
        c.sock.close()
    print(f"ran {cycles} connect/kick cycles")


SCENARIOS = [
    ("playerlist_bomb", sc_playerlist_bomb),
    ("zlib_bomb", sc_zlib_bomb),
    ("nbt_depth_bomb", sc_nbt_depth_bomb),
    ("far_remove_block", sc_far_remove_block),
    ("split_mismatch", sc_split_mismatch),
    ("invalid_name_churn", sc_invalid_name_churn),
    ("ocr_flood", sc_ocr_flood),
]


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone memory-audit survival checks")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--name", default="MemAud")
    ap.add_argument("--cycles", type=int, default=0,
                    help="leak-cycle driver mode: N connect/kick cycles, no assertions")
    ap.add_argument("--only", default="", help="comma-separated scenario filter")
    args = ap.parse_args()

    if args.cycles > 0:
        run_cycles(args.host, args.port, args.name, args.cycles)
        return 0

    if not alive(args.host, args.port):
        print("FAIL: server does not answer pings (is it running?)")
        return 1

    only = {s for s in args.only.split(",") if s}
    failed = []
    for label, fn in SCENARIOS:
        if only and label not in only:
            continue
        print(f"[{label}]")
        try:
            if not fn(args.host, args.port, args.name):
                failed.append(label)
        except Exception as e:
            print(f"  [FAIL] exception: {e}")
            failed.append(label)
        time.sleep(0.4)

    print()
    if failed:
        print(f"FAILED: {', '.join(failed)}")
        return 1
    print("ALL SCENARIOS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
