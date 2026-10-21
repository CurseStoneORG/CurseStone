#!/usr/bin/env python3
"""metadata_spawn_check — spawn metadata completeness (protocol 84).

Boots a two-client flat-world server and verifies every SetEntityData
property block the 0.15.10 client needs to render other players:

  scenario 1 (late viewer): the subject logs in first, the viewer joins
  second. The viewer must receive, right after the subject's ADD_PLAYER,
  a SetEntityData carrying the full spawn metadata: pose flags (0), air
  (1), nametag (2), show-nametag (3), silent (4), no-ai (15), leash
  holder (23) = -1 and leash (24) = 0.

  scenario 2 (self login): every client must receive a SetEntityData
  addressed to eid 0 with DATA_LEAD_HOLDER = -1 (the reference server
  sends it once at login).

Usage:
  python3 tools/metadata_spawn_check.py [--server /tmp/cs_build/CurseStone]
      [--host 127.0.0.1] [--port 19146] [--keep] [--verbose]
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from linux_login_check import Client  # noqa: E402

ADD_PLAYER = 0x0A
SET_ENTITY_DATA = 0x22

START_GAME = 0x09

# Metadata value types (protocol 84).
TYPE_BYTE = 0
TYPE_SHORT = 1
TYPE_INT = 2
TYPE_FLOAT = 3
TYPE_STRING = 4
TYPE_SLOT = 5
TYPE_POS = 6
TYPE_LONG = 7

PROP_FLAGS = 0
PROP_AIR = 1
PROP_NAMETAG = 2
PROP_SHOW_NAMETAG = 3
PROP_SILENT = 4
PROP_NO_AI = 15
PROP_LEAD_HOLDER = 23
PROP_LEAD = 24

PROP_PLAYER_FLAGS = 16
PROP_PLAYER_BED_POSITION = 17


def parse_metadata(blob: bytes):
    """Parses a protocol 84 metadata block: [(index, type, value)].

    Mirrors the reference readMetadata: key = (type << 5) | index,
    little-endian values, 0x7f terminator.
    """
    out = []
    i = 0
    while i < len(blob):
        key = blob[i]
        i += 1
        if key == 0x7F:
            return out, True
        etype = (key >> 5) & 0x07
        index = key & 0x1F
        if etype == TYPE_BYTE:
            if i + 1 > len(blob):
                break
            out.append((index, etype, blob[i]))
            i += 1
        elif etype == TYPE_SHORT:
            if i + 2 > len(blob):
                break
            out.append((index, etype, struct.unpack("<H", blob[i:i + 2])[0]))
            i += 2
        elif etype == TYPE_INT:
            if i + 4 > len(blob):
                break
            out.append((index, etype,
                        struct.unpack("<i", blob[i:i + 4])[0]))
            i += 4
        elif etype == TYPE_FLOAT:
            if i + 4 > len(blob):
                break
            out.append((index, etype,
                        struct.unpack("<f", blob[i:i + 4])[0]))
            i += 4
        elif etype == TYPE_STRING:
            if i + 2 > len(blob):
                break
            n = struct.unpack("<H", blob[i:i + 2])[0]
            i += 2
            if i + n > len(blob):
                break
            out.append((index, etype,
                        blob[i:i + n].decode("utf-8", "replace")))
            i += n
        elif etype == TYPE_SLOT:
            if i + 5 > len(blob):
                break
            sid, cnt, dmg = struct.unpack("<HBH", blob[i:i + 5])
            i += 5
            nbt_len = struct.unpack("<H", blob[i:i + 2])[0]
            i += 2 + nbt_len
            out.append((index, etype, (sid, cnt, dmg)))
        elif etype == TYPE_POS:
            if i + 12 > len(blob):
                break
            out.append((index, etype,
                        struct.unpack("<iii", blob[i:i + 12])))
            i += 12
        elif etype == TYPE_LONG:
            if i + 8 > len(blob):
                break
            out.append((index, etype,
                        struct.unpack("<q", blob[i:i + 8])[0]))
            i += 8
        else:
            break
    return out, False


class MetaClient(Client):
    """Login client that records SetEntityData metadata blocks and the
    ADD_PLAYER announcements, keeping their relative order."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name)
        self.verbose = verbose
        self.eid = 0
        self.spawn = (128, 66, 128)
        self.stream = []   # ("add_player", name, eid) | ("set_data", eid, entries, complete)
        self.other_eids = {}

    def handle_payload(self, payload: bytes):
        if payload and payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib.decompress(body[5:5 + clen])
                except zlib.error:
                    return
                i = 0
                while i + 4 <= len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    if sublen <= 0 or i + 4 + sublen > len(raw):
                        break
                    self.classify(raw[i + 4:i + 4 + sublen])
                    i += 4 + sublen
                return
        self.classify(payload)

    def watch(self, seconds: float):
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = self.recv_one(0.2)
            if data is None:
                continue
            if data[0] in (0xC0, 0xA0):
                continue
            if 0x80 <= data[0] <= 0x8F:
                rel = (data[4] >> 5) & 0x07
                if rel in (2, 3, 4, 6, 7):
                    self.send_ack(data[1] | (data[2] << 8) | (data[3] << 16))
            for payload, is_split, count, sid, idx in self.parse_datagram(data):
                if not payload:
                    continue
                if is_split:
                    whole = self.add_fragment(payload, count, sid, idx)
                    if whole:
                        self.classify(whole)
                else:
                    self.classify(payload)

    def classify(self, payload: bytes):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            self.handle_payload(payload)
            return
        Client.handle_payload(self, payload)
        if self.verbose:
            print(f"    [{self.name}] <- {hex(payload[0])} ({len(payload)}B)")
        if payload[0] == START_GAME and len(payload) >= 34:
            self.eid = struct.unpack(">q", payload[14:22])[0]
            self.spawn = struct.unpack(">iii", payload[22:34])
        elif payload[0] == ADD_PLAYER and len(payload) > 40:
            try:
                off = 17
                nlen = struct.unpack(">H", payload[off:off + 2])[0]
                off += 2
                uname = payload[off:off + nlen].decode("utf-8", "replace")
                off += nlen
                eid = struct.unpack(">q", payload[off:off + 8])[0]
                if uname:
                    self.other_eids[uname] = eid
                    self.stream.append(("add_player", uname, eid))
            except (struct.error, UnicodeDecodeError):
                pass
        elif payload[0] == SET_ENTITY_DATA and len(payload) >= 9:
            eid = struct.unpack(">q", payload[1:9])[0]
            entries, complete = parse_metadata(payload[9:])
            self.stream.append(("set_data", eid, entries, complete))

    def send_game(self, inner: bytes):
        self.send_raw(self.send_encapsulated(inner, reliability=3))


def wait_pred(fn, seconds, client=None):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if fn():
            return True
        if client:
            client.watch(0.2)
        else:
            time.sleep(0.2)
    return fn()


def login(host, port, name, retries=3, verbose=False):
    for attempt in range(retries):
        c = MetaClient(host, port, name, verbose=verbose)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        if not c.expect("start_game", seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.expect("chunks_streamed", seconds=5.0)
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def finish(proc, log_fh, failures, runtime, keep):
    try:
        proc.stdin.write(b"stop\n")
        proc.stdin.flush()
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    log_fh.close()
    if failures:
        print(f"\nFAILED ({len(failures)}): " + ", ".join(failures))
        code = 1
    else:
        print("\nALL CHECKS PASSED")
        code = 0
    if keep:
        print(f"runtime kept: {runtime}")
    else:
        import shutil
        try:
            shutil.rmtree(runtime)
        except OSError:
            pass
    return code


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone metadata spawn check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19146)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_meta_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=MetaCheck\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" +
                              extra_ld).lstrip(":")

    server_log = os.path.join(runtime, "server.log")
    log_fh = open(server_log, "w")
    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=log_fh,
                            stderr=subprocess.STDOUT)

    failures = []

    def check(cond, label, detail=""):
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}" +
              (f" ({detail})" if detail and not cond else ""))
        if not cond:
            failures.append(label)

    up = False
    deadline = time.time() + 20.0
    while time.time() < deadline and not up:
        if proc.poll() is not None:
            break
        c = MetaClient(args.host, args.port, "PingProbe")
        ping = (bytes([0x01]) + struct.pack("<Q", int(time.time() * 1000)) +
                b"\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd"
                b"\x12\x34\x56\x78")
        c.send_raw(ping)
        up = c.expect("unconnected_ping", seconds=1.0)
        c.sock.close()
    if not up:
        print("FAIL: server did not answer the unconnected ping")
        proc.kill()
        log_fh.close()
        return 1

    try:
        subj = login(args.host, args.port, "MetaSubject", verbose=args.verbose)
        viewer = login(args.host, args.port, "MetaViewer", verbose=args.verbose)
        if not subj or not viewer:
            check(False, "both clients login")
            return finish(proc, log_fh, failures, runtime, args.keep)

        # The viewer joins second, so the subject announces itself to the
        # viewer during the viewer's first spawn.
        ok = wait_pred(
            lambda: viewer.other_eids.get("MetaSubject") is not None,
            6.0, client=viewer)
        subject_eid = viewer.other_eids.get("MetaSubject")
        check(ok and subject_eid is not None, "viewer knows the subject eid",
              f"other_eids={viewer.other_eids}")

        # The full metadata SetEntityData must follow the subject's
        # ADD_PLAYER in the viewer's stream.
        set_idx = None
        add_idx = None
        for i, ev in enumerate(viewer.stream):
            if add_idx is None and ev[0] == "add_player" and ev[2] == subject_eid:
                add_idx = i
            if (add_idx is not None and i > add_idx and
                    ev[0] == "set_data" and ev[1] == subject_eid):
                set_idx = i
                break

        check(set_idx is not None,
              "viewer receives SetEntityData for the subject after ADD_PLAYER")

        if set_idx is not None:
            entries, complete = viewer.stream[set_idx][2], viewer.stream[set_idx][3]
            props = {e[0]: e for e in entries}
            check(complete, "metadata block is 0x7F-terminated")

            def prop(idx):
                return props.get(idx)

            f = prop(PROP_FLAGS)
            check(f is not None and f[1] == TYPE_BYTE,
                  "pose flags present (index 0, byte)",
                  f"got {f}")
            a = prop(PROP_AIR)
            check(a is not None and a[1] == TYPE_SHORT and a[2] == 300,
                  "air present (index 1, short, 300)",
                  f"got {a}")
            n = prop(PROP_NAMETAG)
            check(n is not None and n[1] == TYPE_STRING and
                  n[2] == "MetaSubject",
                  "nametag present (index 2, string)",
                  f"got {n}")
            st = prop(PROP_SHOW_NAMETAG)
            check(st is not None and st[1] == TYPE_BYTE and st[2] == 1,
                  "show-nametag present (index 3, byte, 1)",
                  f"got {st}")
            si = prop(PROP_SILENT)
            check(si is not None and si[1] == TYPE_BYTE and si[2] == 0,
                  "silent present (index 4, byte, 0)",
                  f"got {si}")
            na = prop(PROP_NO_AI)
            check(na is not None and na[1] == TYPE_BYTE and na[2] == 0,
                  "no-ai present (index 15, byte, 0)",
                  f"got {na}")
            lh = prop(PROP_LEAD_HOLDER)
            check(lh is not None and lh[1] == TYPE_LONG and lh[2] == -1,
                  "leash holder present (index 23, long, -1)",
                  f"got {lh}")
            ld = prop(PROP_LEAD)
            check(ld is not None and ld[1] == TYPE_BYTE and ld[2] == 0,
                  "leash present (index 24, byte, 0)",
                  f"got {ld}")

            sleep_pos = prop(PROP_PLAYER_BED_POSITION)
            sleep_flag = prop(PROP_PLAYER_FLAGS)
            check(sleep_pos is None and sleep_flag is None,
                  "no sleep metadata while awake",
                  f"got pos={sleep_pos} flags={sleep_flag}")

        # Scenario 2: both clients got the login leash-holder reset.
        def has_login_lead(client):
            for ev in client.stream:
                if ev[0] == "set_data" and ev[1] == 0:
                    for (idx, etype, val) in ev[2]:
                        if idx == PROP_LEAD_HOLDER and etype == TYPE_LONG and val == -1:
                            return True
            return False

        check(wait_pred(lambda: has_login_lead(viewer), 3.0, client=viewer),
              "viewer received SetEntityData(eid 0, LEAD_HOLDER=-1) at login")
        check(wait_pred(lambda: has_login_lead(subj), 3.0, client=subj),
              "subject received SetEntityData(eid 0, LEAD_HOLDER=-1) at login")

        # Nothing in the new metadata may clobber the pose flag byte:
        # a fresh player is neither sneaking nor sprinting.
        if set_idx is not None:
            entries = viewer.stream[set_idx][2]
            flags = next((e for e in entries if e[0] == PROP_FLAGS), None)
            check(flags is not None and flags[2] == 0,
                  "fresh player pose flags are all clear",
                  f"got {flags}")

    finally:
        pass

    return finish(proc, log_fh, failures, runtime, args.keep)


if __name__ == "__main__":
    sys.exit(main())
