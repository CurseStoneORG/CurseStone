#!/usr/bin/env python3
"""End-to-end floating-text check for a local CurseStone server (Linux).

Boots a runtime with the FloatingTextDemo plugin, joins TWO harness
clients (both op) and drives the /ft command surface while watching the
raw wire of both. Verifies the Genisys-style tag model on protocol 84:

  - static text: one ADD_PLAYER carrier whose username is the title and
    one SET_ENTITY_DATA metadata face (invisible flag, nametag with the
    title+'\\n'+body stack, show-nametag, NoAI) on BOTH viewers;
  - title-only / body-only re-texts ride ONE metadata packet each;
  - follow mode: MOVE_PLAYER RESET packets track the target's moves;
  - the visibility toggle hides (Remove) and shows (re-spawn) the same
    carrier id;
  - the viewer allow list drops a viewer with one Remove while the
    allowed player keeps rendering;
  - TTL auto-expiry, the 128/world cap and /ft clear;
  - a follow text whose target disconnects auto-removes for the survivor.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import (Client, STAGE_SPAWN, STAGE_CHUNKS)  # noqa: E402

TEXT = 0x07
ADD_PLAYER = 0x0A
REMOVE_ENTITY = 0x0C
MOVE_PLAYER = 0x10
START_GAME = 0x09
SET_ENTITY_DATA = 0x22
CLIENT_DISCONNECT = 0x15

PROP_FLAGS = 0
PROP_NAMETAG = 2
PROP_SHOW_NAMETAG = 3
PROP_NO_AI = 15
PROP_LEAD_HOLDER = 23
PROP_LEAD = 24
FLAG_INVISIBLE = 1 << 5

PLAYER_NAMES = ("TagTester", "TagSecond")


def be_string(buf, off):
    if off + 2 > len(buf):
        return None, off
    n = int.from_bytes(buf[off:off + 2], "big")
    off += 2
    return buf[off:off + n].decode("utf-8", "replace"), off + n


def parse_add_player(p):
    """uuid(16) + BEString username + eid + x,y,z floats."""
    username, off = be_string(p, 1 + 16)
    if username is None or off + 8 > len(p):
        return None
    eid = struct.unpack(">q", p[off:off + 8])[0]
    x, y, z = struct.unpack(">fff", p[off + 8:off + 20])
    return username, eid, (x, y, z)


def parse_set_entity_data(p):
    """eid + metadata entries; strings carry LE length prefixes."""
    off = 1
    if off + 8 > len(p):
        return None
    eid = struct.unpack(">q", p[off:off + 8])[0]
    off += 8
    props = {}
    while off < len(p):
        key = p[off]
        off += 1
        if key == 0x7F:
            break
        typ = (key >> 5) & 0x07
        idx = key & 0x1F
        if typ == 0:
            props[idx] = p[off]
            off += 1
        elif typ == 1:
            props[idx] = int.from_bytes(p[off:off + 2], "little")
            off += 2
        elif typ == 2:
            props[idx] = int.from_bytes(p[off:off + 4], "little")
            off += 4
        elif typ == 3:
            props[idx] = struct.unpack("<f", p[off:off + 4])[0]
            off += 4
        elif typ == 4:
            n = int.from_bytes(p[off:off + 2], "little")
            off += 2
            props[idx] = p[off:off + n].decode("utf-8", "replace")
            off += n
        elif typ == 5:  # slot: BE id, then count+damage+nbt when held
            sid = struct.unpack(">h", p[off:off + 2])[0]
            off += 2
            if sid > 0:
                off += 1 + 2
                nbt = int.from_bytes(p[off:off + 2], "little")
                off += 2 + nbt
            props[idx] = sid
        elif typ == 6:
            off += 12
        elif typ == 7:
            props[idx] = struct.unpack("<Q", p[off:off + 8])[0]
            off += 8
        else:
            break
    return eid, props


def parse_move_player(p):
    if len(p) < 34:
        return None
    eid = struct.unpack(">q", p[1:9])[0]
    x, y, z = struct.unpack(">fff", p[9:21])
    mode = p[33]
    return eid, x, y, z, mode


class TagClient(Client):
    """Login harness client that records the floating-text wire."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.add_events = []   # (t, eid, username)
        self.meta_events = []  # (t, eid, props)
        self.move_events = []  # (t, eid, x, y, z, mode)
        self.rm_events = []    # (t, eid)
        self.chat = []         # (t, message)
        self.self_eid = None
        self.spawn = None

    def handle_payload(self, payload):
        """Base stage handling + floating-text recording for every sub."""
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib.decompress(body[5:5 + clen])
                except zlib.error:
                    return
                i = 0
                self._in_batch = True
                while i + 4 <= len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    if sublen <= 0 or i + 4 + sublen > len(raw):
                        break
                    sub = raw[i + 4:i + 4 + sublen]
                    Client.handle_payload(self, sub)  # stages (pings muted)
                    self.classify(sub)                # wire records
                    i += 4 + sublen
                self._in_batch = False
                return
        Client.handle_payload(self, payload)  # raknet-level ping answered here
        self.classify(payload)

    def send_game(self, inner):
        self.send_raw(self.send_encapsulated(inner, reliability=3))

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([TEXT, 1]) +
                       struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() +
                       struct.pack(">H", len(raw)) + raw)

    def send_position(self, eid, x, y, z):
        self.send_game(bytes([MOVE_PLAYER]) + struct.pack(">q", eid) +
                       struct.pack(">fff", x, y, z) +
                       struct.pack(">fff", 0.0, 0.0, 0.0) +
                       bytes([0, 1]))

    def classify(self, payload):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib.decompress(body[5:5 + clen])
                except zlib.error:
                    return
                i = 0
                while i + 4 < len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    sub = raw[i + 4:i + 4 + sublen]
                    self.classify(sub)
                    i += 4 + sublen
                return
        now = time.monotonic()
        gp = payload[0]
        if gp == ADD_PLAYER:
            add = parse_add_player(payload)
            if add:
                self.add_events.append((now, add[1], add[0]))
        elif gp == SET_ENTITY_DATA:
            sd = parse_set_entity_data(payload)
            if sd:
                self.meta_events.append((now, sd[0], sd[1]))
        elif gp == MOVE_PLAYER:
            mv = parse_move_player(payload)
            if mv:
                self.move_events.append((now,) + mv)
        elif gp == REMOVE_ENTITY and len(payload) >= 9:
            eid = struct.unpack(">q", payload[1:9])[0]
            self.rm_events.append((now, eid))
        elif gp == TEXT and len(payload) > 4:
            msg, _ = be_string(payload, 2 if payload[1] in (1, 3) else 2)
            if msg:
                self.chat.append((now, msg))
        elif gp == START_GAME and len(payload) >= 46 and self.self_eid is None:
            # wire: pid + seed(4) + dim(1) + generator(4) + gamemode(4)
            #       + eid(8) + spawnX/Y/Z i32 x3 + x/y/z f32 x3
            self.self_eid = struct.unpack(">q", payload[14:22])[0]
            self.spawn = struct.unpack(">fff", payload[34:46])

    def pump(self, seconds):
        """Receive + handle (pings answered) — keeps the session alive."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = self.recv_one(budget=0.05)
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
                        self.handle_payload(whole)
                    continue
                self.handle_payload(payload)


def pump_both(clients, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        for c in clients:
            c.pump(0.02)


def until(clients, pred, timeout, step=0.05):
    deadline = time.time() + timeout
    while time.time() < deadline:
        pump_both(clients, step)
        if pred():
            return True
    return False


def run(host, port, server_bin):
    if not os.path.isfile(server_bin):
        print(f"FAIL: server binary not found: {server_bin}")
        return 1

    repo = os.path.abspath(os.path.join(__file__, "..", ".."))
    runtime = tempfile.mkdtemp(prefix="cs_ftags_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=FloatTags\nview-distance=4\ngamemode=1\nworld-seed=1337\n"
                "advanced-log=true\n" % port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("TagTester\nTagSecond\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 3\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"normal\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")
    shutil.copytree(os.path.join(repo, "plugins", "FloatingTextDemo"),
                    os.path.join(runtime, "plugins", "FloatingTextDemo"))

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" +
                              os.path.dirname(os.path.abspath(server_bin))).lstrip(":")
    proc = subprocess.Popen([server_bin], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    def console(cmd):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    try:
        deadline = time.time() + 120
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

        a = TagClient(host, port, "TagTester")
        b = TagClient(host, port, "TagSecond")
        failed = a.handshake() or a.login() or not a.expect(STAGE_SPAWN, 8.0)
        if failed:
            print("FAIL: TagTester never spawned")
            return 2
        pump_both([a], 1.0)
        failed = b.handshake() or b.login() or not b.expect(STAGE_SPAWN, 8.0)
        if failed:
            print("FAIL: TagSecond never spawned")
            return 2
        pump_both([a, b], 1.5)
        check(a.spawn is not None and a.self_eid is not None,
              "StartGame parsed (eid + spawn position)")

        # ---- static spawn: the two-packet fake-player face --------------
        t0 = time.monotonic()
        a.say("/ft add Welcome|to|CurseStone")
        ok = until([a, b], lambda: _carrier_add(a, "Welcome") is not None, 4.0)
        e1 = _carrier_add(a, "Welcome")
        check(ok and e1 is not None, "static spawn: carrier AddPlayer reaches A")
        check(e1 is not None and _meta_after(a, e1, t0) is not None,
              "static spawn: metadata face reaches A")
        meta = _meta_after(a, e1, t0) if e1 else None
        check(bool(meta) and meta.get(PROP_FLAGS) == FLAG_INVISIBLE,
              "metadata: invisible flag bit set")
        check(bool(meta) and meta.get(PROP_SHOW_NAMETAG) == 1 and
              meta.get(PROP_NO_AI) == 1, "metadata: show-nametag + NoAI set")
        check(bool(meta) and meta.get(PROP_NAMETAG) == "Welcome\nto\nCurseStone",
              "metadata: nametag carries title + body stack")
        check(bool(meta) and meta.get(PROP_LEAD_HOLDER) == (1 << 64) - 1,
              "metadata: no-leash long set")
        b_e1 = _carrier_add(b, "Welcome")
        check(b_e1 is not None and b_e1 == e1, "same carrier id reaches B")
        bmeta = _meta_after(b, e1, t0) if e1 else None
        check(bool(bmeta) and bmeta.get(PROP_NAMETAG) == "Welcome\nto\nCurseStone",
              "B renders the same nametag stack")
        check(_add_count(a, e1, t0) == 1, "spawn emits exactly one AddPlayer")

        # ---- title-only re-text: one metadata packet --------------------
        t1 = time.monotonic()
        a.say(f"/ft title {e1} Hola")
        ok = until([a, b], lambda: _nametag(a, e1, after=t1) == "Hola\nto\nCurseStone", 4.0)
        check(ok, "title-only re-text: nametag becomes Hola\\nto\\nCurseStone")
        check(_nametag(b, e1, after=t1) == "Hola\nto\nCurseStone",
              "title re-text reaches B")
        check(_meta_count(a, e1, t1) == 1, "title re-text is ONE metadata packet")
        check(_add_count(a, e1, t1) == 0, "title re-text never re-spawns the carrier")

        # ---- body-only re-text: one metadata packet ---------------------
        t2 = time.monotonic()
        a.say(f"/ft body {e1} Mundo")
        ok = until([a, b], lambda: _nametag(a, e1, after=t2) == "Hola\nMundo", 4.0)
        check(ok, "body-only re-text: nametag becomes Hola\\nMundo")
        check(_meta_count(a, e1, t2) == 1, "body re-text is ONE metadata packet")

        # ---- follow mode: carrier tracks the target ---------------------
        # The 0.15 threshold keeps a still target at zero packets, so the
        # move packets must be observed while the target walks: /tp steps
        # (a raw 2-block client jump would trip the anti-cheat speed cap).
        t3 = time.monotonic()
        a.say(f"/ft follow {e1}")
        pump_both([a, b], 0.5)
        t4 = time.monotonic()
        step1_x = a.spawn[0] + 2.0
        a.say(f"/tp {step1_x:g} {a.spawn[1]:g} {a.spawn[2]:g}")
        ok = until([a, b], lambda: _near_move(a, e1, t4, 1, step1_x,
                                              a.spawn[2]), 5.0)
        mv = _move(a, e1, t4, 1)
        check(ok and mv is not None, "follow: carrier MovePlayer RESET reaches A")
        lift = float(2.1)
        if mv:
            wy = mv[2] - (a.spawn[1] + lift - 1.62)
            check(abs(mv[1] - step1_x) < 0.8 and abs(mv[3] - a.spawn[2]) < 0.6 and
                  abs(wy) < 2.5,
                  "follow: carrier anchors over the target")
        check(_near_move(b, e1, t4, 1, step1_x, a.spawn[2]) is not None,
              "follow move reaches B")
        t5 = time.monotonic()
        step2_x = a.spawn[0] + 4.0
        a.say(f"/tp {step2_x:g} {a.spawn[1]:g} {a.spawn[2]:g}")
        ok = until([a, b], lambda: _near_move(a, e1, t5, 1, step2_x,
                                              a.spawn[2]), 5.0)
        check(ok, "follow: carrier tracks the target's walk (one move per step)")

        # ---- visibility toggle ------------------------------------------
        t5 = time.monotonic()
        a.say(f"/ft hide {e1}")
        ok = until([a, b], lambda: _rm_after(a, e1, t5) and _rm_after(b, e1, t5), 4.0)
        check(ok, "hide: RemoveEntity reaches both viewers")
        adds_before = _add_count(a, e1, 0)
        t6 = time.monotonic()
        a.say(f"/ft show {e1}")
        ok = until([a, b], lambda: _add_count(a, e1, t6) > 0 and
                   _add_count(b, e1, t6) > 0, 4.0)
        check(ok, "show: carrier re-spawns for both viewers")
        check(_add_count(a, e1, t6) == 1 and adds_before + 1 == _add_count(a, e1, 0),
              "show re-uses the same carrier id")
        shown = [u for (tt, ee, u) in a.add_events if ee == e1 and tt >= t6]
        check(shown and shown[-1] == "Hola", "show restores the title as username")

        # ---- viewer allow list ------------------------------------------
        a.say("/ft here Solo")
        until([a, b], lambda: _carrier_add(a, "Solo") is not None, 4.0)
        e2 = _carrier_add(a, "Solo")
        check(e2 is not None, "second text spawns for both")
        t8 = time.monotonic()
        a.say(f"/ft viewers {e2} TagSecond")
        ok = until([a, b], lambda: _rm_after(a, e2, t8), 4.0)
        a_rm = _rm_after(a, e2, t8)
        pump_both([a, b], 1.2)
        check(ok and a_rm, "allow list: refused viewer gets one Remove")
        check(not _rm_after(b, e2, t8), "allow list: allowed viewer keeps rendering")
        t9 = time.monotonic()
        a.say(f"/ft viewers {e2} all")
        ok = until([a, b], lambda: _add_count(a, e2, t9) > 0, 4.0)
        check(ok, "allow list reset: re-arms the refused viewer")

        # ---- ttl auto-expiry --------------------------------------------
        t10 = time.monotonic()
        a.say("/ft ttl 2 Expira|ya")
        e4 = None
        until([a, b], lambda: _carrier_add(a, "Expira") is not None, 4.0)
        e4 = _carrier_add(a, "Expira")
        check(e4 is not None, "ttl text spawns")
        ok = until([a, b], lambda: e4 is not None and _rm_after(a, e4, t10) and
                   _rm_after(b, e4, t10), 6.0)
        check(ok, "ttl: text auto-despawns for both viewers")

        # ---- the 128/world cap ------------------------------------------
        alive = 2  # E1 (static, shown) + E2 (solo, re-armed)
        created, cap_replies = 0, 0
        t_cap = time.monotonic()
        for i in range(130):
            a.say("/ft here t")
            time.sleep(0.04)
            pump_both([a, b], 0.05)
        pump_both([a, b], 1.5)
        for tt, msg in a.chat:
            if tt >= t_cap and msg.startswith("Floating text #"):
                created += 1
            elif tt >= t_cap and "cap" in msg:
                cap_replies += 1
        carrier_eids = _alive_carriers(a)
        check(created == 128 - alive,
              f"cap: exactly {128 - alive} extra texts spawn (got {created})")
        check(cap_replies >= 1, "cap: the overflow answers with the cap reply")
        check(len(carrier_eids) == 128,
              f"cap: 128 live carriers on the wire (got {len(carrier_eids)})")

        # ---- /ft clear ----------------------------------------------------
        t11 = time.monotonic()
        a.say("/ft clear")
        ok = until([a, b], lambda: _rm_total(a, after=t11) >= 128, 8.0)
        check(ok, "clear: every carrier despawns for A")
        ok = until([a, b], lambda: _rm_total(b, after=t11) >= 128, 8.0)
        check(ok, "clear: every carrier despawns for B")

        # ---- follow text auto-removes when its target leaves --------------
        a.say("/ft here ridealong")
        until([a, b], lambda: _carrier_add(a, "ridealong") is not None, 4.0)
        e5 = _carrier_add(a, "ridealong")
        check(e5 is not None, "orphan text spawns")
        a.say(f"/ft follow {e5}")
        until([a, b], lambda: _move(a, e5, after=t11, mode=1) is not None, 4.0)
        t12 = time.monotonic()
        a.send_game(bytes([CLIENT_DISCONNECT]))
        ok = until([b], lambda: e5 is not None and _rm_after(b, e5, t12), 6.0)
        check(ok, "orphan: follow text auto-removes for the survivor")

        console("stop")
        pump_both([b], 0.5)

        print()
        if failures:
            print(f"FAIL ({len(failures)}):")
            for f_ in failures:
                print(f"  - {f_}")
            return 3
        print("OK: floating text wire verified end to end")
        return 0
    finally:
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


def _carrier_add(client, username):
    """The latest add event for a carrier with this username."""
    found = None
    for _, ee, u in client.add_events:
        if u == username and u not in PLAYER_NAMES:
            found = ee
    return found


def _add_count(client, eid, after):
    return sum(1 for (tt, ee, _) in client.add_events
               if ee == eid and tt >= after)


def _meta_after(client, eid, after):
    last = None
    for tt, ee, props in client.meta_events:
        if ee == eid and tt >= after:
            last = props
    return last


def _meta_count(client, eid, after):
    return sum(1 for (tt, ee, _) in client.meta_events
               if ee == eid and tt >= after)


def _nametag(client, eid, after=0):
    m = _meta_after(client, eid, after)
    return m.get(PROP_NAMETAG) if m else None


def _move(client, eid, after, mode):
    last = None
    for tt, ee, x, y, z, m in client.move_events:
        if ee == eid and m == mode and tt >= after:
            last = (eid, x, y, z, m)
    return last


def _near_move(client, eid, after, mode, x, z, tol=0.8):
    mv = _move(client, eid, after, mode)
    if mv is None:
        return None
    return mv if abs(mv[1] - x) < tol and abs(mv[3] - z) < tol else None


def _rm_after(client, eid, after):
    return any(ee == eid and tt >= after for tt, ee in client.rm_events)


def _rm_total(client, after):
    return sum(1 for (tt, _) in client.rm_events if tt >= after)


def _alive_carriers(client):
    """Carrier ids whose last add is younger than their last remove."""
    last_add, last_rm = {}, {}
    for tt, ee, u in client.add_events:
        if u not in PLAYER_NAMES:
            last_add[ee] = max(last_add.get(ee, 0.0), tt)
    for tt, ee in client.rm_events:
        last_rm[ee] = max(last_rm.get(ee, 0.0), tt)
    return {ee for ee, t in last_add.items() if t > last_rm.get(ee, 0.0)}


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone floating text E2E check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19155)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port, args.server))
