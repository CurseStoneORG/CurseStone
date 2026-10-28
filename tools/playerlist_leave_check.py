#!/usr/bin/env python3
"""End-to-end player-list cleanup check for CurseStone.

The stale tab-entry bug: with two players online, the one who stays keeps
seeing the leaver's name in the server-list tab forever — the 0.15.10
client only drops a PlayerListPacket entry when the server tells it to,
so a quit that never sends the REMOVE action leaves the row visible until
the viewer reconnects.

Two real protocol-84 clients connect to a self-managed flat server:

  1. B sees A's ADD entry (uuid bytes recorded from the wire)
  2. A disconnects with the RakNet CLIENT_DISCONNECT notification
  3. B must receive a PlayerListPacket REMOVE carrying the SAME 16 uuid
     bytes the ADD announced (parser consistency ADD <-> REMOVE)
  4. B must NOT receive a REMOVE for its own uuid
  5. A must have seen B's ADD before the leave (announceSelfToOthers
     still broadcasts the joining player to everyone)

    python3 tools/playerlist_leave_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import base64
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

PLAYER_LIST = 0x38
TYPE_ADD = 0
TYPE_REMOVE = 1


def b64u(obj) -> str:
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


class ListClient(Client):
    """door_bed_flow_check Client + per-name login uuid, PlayerListPacket
    capture and the RakNet disconnect notification."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.uid = ""           # the login uuid string this client sent
        self.player_list = []   # (type, uuid_hex, name) in arrival order

    def login_uuid(self) -> str:
        # Deterministic per-name uuid (two clients must never collide).
        digest = int(hashlib.md5(self.name.encode()).hexdigest(), 16)
        return "11111111-2222-3333-4444-%012d" % (digest % 10**12)

    def login(self) -> bool:
        uid = self.login_uuid()
        self.uid = uid
        chain_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"displayName": self.name,
                           "identity": uid,
                           "extraData": {}}) + ".sig")
        chain = json.dumps({"chain": [chain_jwt]}, separators=(",", ":")).encode()
        cdata_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"DisplayName": self.name,
                           "Identity": uid,
                           "DeviceModel": "Linux", "DeviceOS": 7,
                           "LanguageCode": "en_US",
                           "SkinId": "Standard_Custom"}) + ".sig")
        raw = struct.pack("<I", len(chain)) + chain + \
            struct.pack("<I", len(cdata_jwt)) + cdata_jwt.encode()
        comp = __import__("zlib").compress(raw)
        body = struct.pack(">I", 84) + struct.pack(">I", len(comp)) + comp
        self.send_raw(self.send_encapsulated(bytes([0x01]) + body, reliability=3))
        deadline = time.time() + 5.0
        while time.time() < deadline:
            self.drain(0.2)
            if any(ev[0] in ("update_block", "set_time", "text") for ev in self.events) \
               or self.eid != 0:
                return True
        return self.eid != 0

    def leave(self):
        # RakNet CLIENT_DISCONNECT (0x15): the clean-leave notification the
        # server turns into Player::close.
        self.send_game(bytes([0x15]))

    def uuid_bytes(self) -> bytes:
        return bytes.fromhex(self.uid.replace("-", ""))

    def on_game_packet(self, p: bytes):
        if p and p[0] == PLAYER_LIST:
            try:
                ptype = p[1]
                count = struct.unpack(">i", p[2:6])[0]
                off = 6
                for _ in range(max(0, count)):
                    if off + 16 > len(p):
                        break
                    uuid_hex = p[off:off + 16].hex()
                    off += 16
                    name = ""
                    if ptype == TYPE_ADD:
                        off += 8  # eid
                        for _ in range(3):  # name, skinId, skinData
                            slen = struct.unpack(">H", p[off:off + 2])[0]
                            off += 2
                            if _ == 0:
                                name = p[off:off + slen].decode("utf-8", "replace")
                            off += slen
                    self.player_list.append((ptype, uuid_hex, name))
            except (struct.error, IndexError) as exc:
                self.player_list.append((-1, f"parse-error: {exc}", ""))
            return
        Client.on_game_packet(self, p)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone player-list cleanup check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19147)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_list_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ListCheck\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  debug-mode: true\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=open(os.path.join(runtime, "stderr.log"), "wb"))

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    try:
        deadline = time.time() + 90
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            try:
                with open(os.path.join(runtime, "server.log")) as f:
                    if "Server ready" in f.read():
                        booted = True
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        a = ListClient(args.host, args.port, "ListOne", verbose=args.verbose)
        print("stage: A handshake+login+spawn")
        if not a.handshake() or not a.login():
            print("FAIL: A handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(1.5)
        if a.eid == 0:
            print("FAIL: A never received StartGame (no eid)")
            return 1
        print(f"  A eid={a.eid}")

        b = ListClient(args.host, args.port, "ListTwo", verbose=args.verbose)
        print("stage: B handshake+login+spawn")
        if not b.handshake() or not b.login():
            print("FAIL: B handshake/login")
            return 1
        # Keep A's session alive while B loads in: the 5s session timeout
        # reaps a client that stops answering keepalive pings, and A only
        # answers while its own drain loop runs. Interleave both drains.
        for _ in range(20):
            b.drain(0.2)
            a.drain(0.2)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        for _ in range(8):
            b.drain(0.2)
            a.drain(0.2)
        if b.eid == 0:
            print("FAIL: B never received StartGame (no eid)")
            return 1
        print(f"  B eid={b.eid}")

        # -------- 1: B saw A's ADD entry ---------------------------------
        adds_for_a = [e for e in b.player_list
                      if e[0] == TYPE_ADD and e[2] == a.name]
        check(bool(adds_for_a),
              "B receives the ADD entry for A on join",
              f"b.player_list={b.player_list}")
        a_uuid = adds_for_a[0][1] if adds_for_a else ""
        check(bool(a_uuid) and a_uuid != "0" * 32,
              "A's ADD carries real (non-zero) uuid bytes",
              f"a_uuid={a_uuid}")
        check(a_uuid == a.uuid_bytes().hex(),
              "A's ADD uuid matches the uuid A logged in with",
              f"wire={a_uuid} login={a.uuid_bytes().hex()}")

        # -------- 5: A saw B's ADD (announceSelfToOthers intact) ----------
        a.drain(2.0)  # B's announceSelfToOthers reaches A only when drained
        adds_for_b_on_a = [e for e in a.player_list
                           if e[0] == TYPE_ADD and e[2] == b.name]
        check(bool(adds_for_b_on_a),
              "A receives the ADD entry for B on join",
              f"a.player_list={a.player_list}")

        # -------- 2: A leaves --------------------------------------------
        before = len(b.player_list)
        a.leave()
        b.drain(5.0)

        # -------- 3: B gets the REMOVE with the same uuid ----------------
        removes = [e for e in b.player_list[before:] if e[0] == TYPE_REMOVE]
        matching = [e for e in removes if e[1] == a_uuid]
        check(bool(matching),
              "B receives a REMOVE carrying A's uuid after A leaves",
              f"removes={[(e[1][:8], e[2]) for e in removes]} "
              f"a_uuid={a_uuid[:8]}")

        # -------- 4: no REMOVE for B's own uuid --------------------------
        stray = [e for e in removes if e[1] == b.uuid_bytes().hex()]
        check(not stray,
              "B never receives a REMOVE for its own uuid",
              f"removes={[(e[1][:8], e[2]) for e in removes]}")

    finally:
        try:
            console("stop")
        except Exception:
            pass
        time.sleep(0.5)
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): " + "; ".join(failures))
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
