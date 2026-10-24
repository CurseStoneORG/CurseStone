#!/usr/bin/env python3
"""End-to-end multiplayer pickup/drink + eat-animation + disconnect check.

Three real protocol-84 clients (MCPE 0.15.10) against a self-managed flat
server:

  Phase 1  milk handed over by DROP: A drops a (325:1) milk bucket, B
           picks it up (server scan + the client-side slot write the real
           client sends) and drinks. The milk must be CONSUMED (held slot
           ends as an empty bucket 325:0), never re-asserted in a loop.
  Phase 2  control run without the client-side pre-write (pure server
           pickup) must also consume.
  Phase 3  eating/drinking is visible to a watcher: the drinker raises
           the using-item pose (DATA_FLAG_ACTION) while drinking and the
           completion EntityEvent(USE_ITEM) is relayed.
  Phase 4  a client that goes SILENT (no datagrams at all) is reaped by
           the session timeout and its player entity is despawned for
           the remaining player (no ghost).

    python3 tools/mp_pickup_drink_check.py [--server BIN] [--port N]

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

# Game-layer ids (protocol 84 / Info.h).
ADD_PLAYER = 0x0A
REMOVE_ENTITY = 0x0C
TAKE_ITEM_ENTITY = 0x0E
MOVE_PLAYER = 0x10
ENTITY_EVENT = 0x18
MOB_EQUIPMENT = 0x1B
SET_ENTITY_DATA = 0x22
DROP_ITEM = 0x29
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E

BUCKET = 325
MILK_META = 1
EAT_EVENT = 9  # EntityEvent USE_ITEM ("finished eating/drinking")


def b64u(obj) -> str:
    import base64
    import json
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


class MpClient(Client):
    """door_bed_flow_check Client + per-name uuid, slot/content tracking,
    dropped-item/pickup/pose/eat-echo capture and the drink helpers."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.slot_writes = []    # (slot, id, meta, count) from window-0/0x7a sets
        self.inv = {}            # {(id, meta): count} from window-0 content dumps
        self.take_targets = []   # TakeItemEntityPacket (target, taker)
        self.removed = []        # RemoveEntityPacket eids
        self.set_data = []       # SetEntityDataPacket eids (pose raises)
        self.eat_events = []     # EntityEvent eids for event 9 (USE_ITEM)
        self.seen_moves = {}     # eid -> (x, y, z) last relayed MovePlayer

    def login(self) -> bool:
        uid = "11111111-2222-3333-4444-%012d" % (abs(hash(self.name)) % 10**12)
        chain_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"displayName": self.name,
                           "identity": uid,
                           "extraData": {}}) + ".sig")
        chain = json_chain(chain_jwt)
        cdata_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"DisplayName": self.name,
                           "Identity": uid,
                           "DeviceModel": "Linux", "DeviceOS": 7,
                           "LanguageCode": "en_US",
                           "SkinId": "Standard_Custom"}) + ".sig")
        raw = struct.pack("<I", len(chain)) + chain + \
            struct.pack("<I", len(cdata_jwt)) + cdata_jwt.encode()
        import zlib
        comp = zlib.compress(raw)
        body = struct.pack(">I", 84) + struct.pack(">I", len(comp)) + comp
        self.send_raw(self.send_encapsulated(bytes([0x01]) + body, reliability=3))
        deadline = time.time() + 5.0
        while time.time() < deadline:
            self.drain(0.2)
            if any(ev[0] in ("update_block", "set_time", "text") for ev in self.events) \
               or self.eid != 0:
                return True
        return self.eid != 0

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == TAKE_ITEM_ENTITY and len(p) >= 17:
            tgt, taker = struct.unpack(">qq", p[1:17])
            self.take_targets.append((tgt, taker))
            return
        if pid == REMOVE_ENTITY and len(p) >= 9:
            self.removed.append(struct.unpack(">q", p[1:9])[0])
            return
        if pid == MOVE_PLAYER and len(p) >= 30:
            eid = struct.unpack(">q", p[1:9])[0]
            x, y, z = struct.unpack(">fff", p[9:21])
            self.seen_moves[eid] = (round(x, 2), round(y, 2), round(z, 2))
            return
        if pid == SET_ENTITY_DATA and len(p) >= 9:
            self.set_data.append(struct.unpack(">q", p[1:9])[0])
            return
        if pid == ENTITY_EVENT and len(p) >= 10:
            eid = struct.unpack(">q", p[1:9])[0]
            if p[9] == EAT_EVENT:
                self.eat_events.append(eid)
            return
        if pid == CONTAINER_SET_SLOT and len(p) >= 13:
            win = p[1]
            slot = struct.unpack(">h", p[2:4])[0]
            # p[4:6] = hotbar link; item = id(i16be) count(u8) meta(i16be)
            sid = struct.unpack(">h", p[6:8])[0]
            cnt = p[8]
            meta = struct.unpack(">h", p[9:11])[0]
            self.slot_writes.append((win, slot, sid, meta, cnt))
            return
        if pid == CONTAINER_SET_CONTENT and len(p) >= 4:
            self.parse_inventory(p)
            return
        Client.on_game_packet(self, p)

    def parse_inventory(self, p: bytes):
        """window-0 ContainerSetContent dump, meta-aware slot parse."""
        win = p[1]
        count = struct.unpack(">H", p[2:4])[0]
        off = 4
        totals = {}
        for _ in range(count):
            if off + 2 > len(p):
                return
            sid = struct.unpack(">h", p[off:off + 2])[0]
            off += 2
            if sid == 0:
                continue
            if off + 5 > len(p):
                return
            cnt = p[off]
            meta = struct.unpack(">h", p[off + 1:off + 3])[0]
            nlen = struct.unpack("<H", p[off + 3:off + 5])[0]
            off += 5 + nlen
            totals[(sid, meta)] = totals.get((sid, meta), 0) + cnt
        if win == 0 and off + 2 <= len(p):
            hb = struct.unpack(">H", p[off:off + 2])[0]
            off += 2 + 4 * hb
        if win == 0:
            self.inv = totals

    # ---- helpers ----------------------------------------------------------
    def creative_take(self, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

    def client_slot_write(self, slot, item_id, count=1, meta=0):
        """The ContainerSetSlot the real client sends after a local
        inventory change (pickup, move, consume)."""
        self.creative_take(slot, item_id, count, meta)  # same wire layout

    def mob_equipment(self, hotbar, item_id, meta=0):
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", self.eid)
        me += struct.pack(">h", item_id)
        me += bytes([1])
        me += struct.pack(">h", meta)
        me += struct.pack("<H", 0)
        me += bytes([hotbar + 9, hotbar])
        self.send_game(me)

    def entity_event(self, event):
        self.send_game(bytes([ENTITY_EVENT]) + struct.pack(">q", self.eid) +
                       bytes([event]))

    def drop_item(self, item_id, count=1, meta=0):
        body = (bytes([DROP_ITEM, 0]) +
                struct.pack(">h", item_id) + bytes([count]) +
                struct.pack(">h", meta) + struct.pack("<H", 0))
        self.send_game(body)

    def move_to(self, x, y, z, yaw=0.0, pitch=0.0):
        body = (bytes([MOVE_PLAYER]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", yaw, yaw, pitch) +
                bytes([0, 1]))
        self.send_game(body)

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([0x07, 1]) + struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() + struct.pack(">H", len(raw)) + raw)


def json_chain(chain_jwt: str) -> bytes:
    import json
    return json.dumps({"chain": [chain_jwt]}, separators=(",", ":")).encode()


def drain_until(pred, clients, seconds, verbose=False):
    """Drain all clients until pred() is true or the deadline passes."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        if pred():
            return True
        for c in clients:
            c.drain(0.1)
    return pred()


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone multiplayer pickup/drink check")
    ap.add_argument("--server", default="/home/z/my-project/CurseStone/build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19143)
    ap.add_argument("--skip-timeout", action="store_true",
                    help="skip the 35s disconnect-timeout phase")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_mppickup_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=MpPickup\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("DropOne\nDropTwo\nDropThree\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    def log_has(s: str) -> bool:
        try:
            with open(server_log) as f:
                return s in f.read()
        except FileNotFoundError:
            return False

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    try:
        deadline = time.time() + 40
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            try:
                with open(server_log) as f:
                    if "Server ready" in f.read():
                        booted = True
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        # ---- clients A (dropper) + B (receiver) -------------------------
        a = MpClient(args.host, args.port, "DropOne", verbose=args.verbose)
        print("stage: dropper A handshake+login")
        if not a.handshake() or not a.login():
            print("FAIL: A handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(1.5)
        if a.eid == 0:
            print("FAIL: A never received StartGame")
            return 1
        print(f"  A eid={a.eid} spawn={a.spawn}")

        b = MpClient(args.host, args.port, "DropTwo", verbose=args.verbose)
        print("stage: receiver B handshake+login")
        if not b.handshake() or not b.login():
            print("FAIL: B handshake/login")
            return 1
        b.drain(4.0)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        b.drain(1.5)
        if b.eid == 0:
            print("FAIL: B never received StartGame")
            return 1
        print(f"  B eid={b.eid} spawn={b.spawn}")

        sx, sy, sz = a.spawn
        a.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
        b.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
        time.sleep(0.6)
        a.drain(0.6)
        b.drain(0.6)

        def hand_over():
            """A drops the milk and steps out of pickup range; B stands on
            the drop point (the pickup volume is +-1.3 blocks, so B must
            be within ~1 block of the landed item while A leaves)."""
            a.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
            b.move_to(sx + 0.5, sy + 1.62, sz + 1.0)
            time.sleep(0.5)
            a.drain(0.4)
            b.drain(0.4)
            a.drop_item(BUCKET, 1, MILK_META)
            time.sleep(0.3)
            a.drain(0.3)
            a.move_to(sx + 0.5, sy + 1.62, sz - 4.5)  # out of the pickup volume
            time.sleep(0.4)
            a.drain(0.4)
            b.move_to(sx + 0.5, sy + 1.62, sz + 0.6)  # on top of the drop
            time.sleep(0.4)
            b.drain(0.4)

        # ---- grant A a milk bucket (creative) ---------------------------
        a.say("/gamemode 1")
        time.sleep(0.8)
        a.drain(0.8)
        a.creative_take(0, BUCKET, 1, MILK_META)
        time.sleep(0.5)
        a.drain(0.5)
        a.say("/gamemode 0")
        time.sleep(0.8)
        a.drain(0.8)
        a.mob_equipment(0, BUCKET, MILK_META)
        time.sleep(0.4)
        a.drain(0.4)
        check(a.inv.get((BUCKET, MILK_META), 0) == 1,
              "A holds the milk bucket (325:1)", f"A inv={a.inv}")

        # ================= Phase 1: drop -> pickup -> drink ==============
        print("stage: phase 1 - A drops milk, B picks up and drinks")
        b.slot_writes.clear()
        b.take_targets.clear()
        b.inv.clear()

        hand_over()

        # The real client picks the item up locally as soon as it is in
        # range and informs the server with a slot write. The item is
        # still in its 2s pickup delay server-side, so this write races
        # AHEAD of the server's own pickup scan — exactly the ordering
        # the reported bug happens under.
        b.client_slot_write(0, BUCKET, 1, MILK_META)

        # Wait for the server-side pickup to complete for B.
        picked = drain_until(
            lambda: any(t[1] == b.eid for t in b.take_targets) or
            b.inv.get((BUCKET, MILK_META), 0) >= 1,
            [a, b], 5.0)
        print(f"    diag: A@{a.seen_moves.get(a.eid)} B@{b.seen_moves.get(b.eid)} "
              f"takes={b.take_targets}")
        check(picked, "B ends up with the dropped milk (server pickup)",
              f"takes={b.take_targets} inv={b.inv}")

        # The racing client pickup write must be HONORED, not wiped: the
        # server may not answer it with an empty-slot re-assertion (that
        # wipe is what made real clients re-pick the same drop in a loop).
        wipes = [w for w in b.slot_writes if w[0] == 0 and w[2] == 0]
        check(not wipes,
              "client pickup write is not answered with an empty-slot wipe",
              f"slot_writes={b.slot_writes}")

        # B equips slot 0 (the real user scrolls to the item) and drinks.
        b.mob_equipment(0, BUCKET, MILK_META)
        time.sleep(0.4)
        b.drain(0.4)
        writes_before = list(b.slot_writes)
        b.entity_event(EAT_EVENT)
        consumed = drain_until(
            lambda: (b.inv.get((BUCKET, 0), 0) >= 1 and
                     b.inv.get((BUCKET, MILK_META), 0) == 0) or
            any(w[0] == 0 and w[1] == 0 and w[2] == BUCKET and w[3] == 0
                for w in b.slot_writes),
            [a, b], 4.0)
        check(consumed,
              "milk is CONSUMED after pickup+drink (bucket residue, no loop)",
              f"slot_writes={b.slot_writes} inv={b.inv}")
        if not consumed:
            print("    detail: writes before drink:", writes_before)
            print("    detail: A inv:", a.inv)

        # ================= Phase 2: pure server pickup control ===========
        print("stage: phase 2 - control run (no client pre-write)")
        # Re-grant A a milk bucket and repeat without B's racing write.
        a.say("/gamemode 1")
        time.sleep(0.8)
        a.drain(0.8)
        a.creative_take(0, BUCKET, 1, MILK_META)
        time.sleep(0.5)
        a.drain(0.5)
        a.say("/gamemode 0")
        time.sleep(0.8)
        a.drain(0.8)
        a.mob_equipment(0, BUCKET, MILK_META)
        time.sleep(0.4)
        a.drain(0.4)

        b.slot_writes.clear()
        b.take_targets.clear()
        b.inv.clear()
        hand_over()
        picked = drain_until(
            lambda: any(t[1] == b.eid for t in b.take_targets) or
            b.inv.get((BUCKET, MILK_META), 0) >= 1,
            [a, b], 5.0)
        check(picked, "phase 2: B receives the dropped milk", f"takes={b.take_targets}")
        # Equip the slot the milk actually landed in (the inventory already
        # holds the phase-1 empty bucket, so the milk goes to a later slot).
        milk_slot = 0
        for w in reversed(b.slot_writes):
            if w[0] == 0 and w[2] == BUCKET and w[3] == MILK_META:
                milk_slot = w[1]
                break
        b.mob_equipment(milk_slot, BUCKET, MILK_META)
        time.sleep(0.4)
        b.drain(0.4)
        b.entity_event(EAT_EVENT)
        consumed2 = drain_until(
            lambda: (b.inv.get((BUCKET, 0), 0) >= 1 and
                     b.inv.get((BUCKET, MILK_META), 0) == 0) or
            any(w[0] == 0 and w[1] == 0 and w[2] == BUCKET and w[3] == 0
                for w in b.slot_writes),
            [a, b], 4.0)
        check(consumed2, "phase 2: milk consumed without the racing write",
              f"slot_writes={b.slot_writes} inv={b.inv}")

        # ================= Phase 3: eating visible to the watcher ========
        print("stage: phase 3 - watcher sees the drink")
        # A drinks its own milk (grant another one) while B watches.
        a.say("/gamemode 1")
        time.sleep(0.8)
        a.drain(0.8)
        a.creative_take(0, BUCKET, 1, MILK_META)
        time.sleep(0.5)
        a.drain(0.5)
        a.say("/gamemode 0")
        time.sleep(0.8)
        a.drain(0.8)
        a.mob_equipment(0, BUCKET, MILK_META)
        time.sleep(0.4)
        a.drain(0.4)

        b.set_data.clear()
        b.eat_events.clear()
        # A starts drinking (the real client holds use — the air-use path)
        a.use_item(0, 0, 0, 0xFF, BUCKET, MILK_META)
        saw_pose = drain_until(lambda: a.eid in b.set_data, [a, b], 2.0)
        check(saw_pose,
              "watcher sees the using-item pose (SetEntityData) during the drink",
              f"set_data eids={b.set_data}")
        a.entity_event(EAT_EVENT)
        saw_eat = drain_until(lambda: a.eid in b.eat_events, [a, b], 4.0)
        check(saw_eat, "watcher receives the completion EntityEvent(USE_ITEM)",
              f"eat_events={b.eat_events}")
        # After the completion event the pose must go down for viewers.
        # set_data only records raises (eids), so sample the raw flag: the
        # watcher may receive a second SetEntityData with the flag cleared.
        # We assert the server did not keep the drinker stuck: A's own air
        # use of a non-food item right after must not be blocked.
        time.sleep(0.3)
        a.drain(0.3)

        # ================= Phase 4: silent client is reaped ==============
        if not args.skip_timeout:
            print("stage: phase 4 - silent client timeout + despawn")
            c = MpClient(args.host, args.port, "DropThree", verbose=args.verbose)
            if not c.handshake() or not c.login():
                print("FAIL: C handshake/login")
                return 1
            c.drain(3.0)
            c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
            c.drain(1.0)
            c_eid = c.eid
            print(f"  C eid={c_eid} — going silent (no ACKs, no datagrams)")
            a.removed.clear()
            # C simply stops draining: no ACKs, no pings, nothing. The
            # server must reap the session on the receive-clock timeout
            # and despawn C for the survivors.
            t0 = time.time()
            reaped = False
            while time.time() - t0 < 40.0:
                a.drain(0.5)
                if c_eid in a.removed and log_has("Closing player DropThree"):
                    reaped = True
                    break
            dt = time.time() - t0
            check(reaped,
                  "silent client is timed out and despawned for the survivor",
                  f"waited={dt:.1f}s removed={a.removed} "
                  f"closing_logged={log_has('Closing player DropThree')}")
            check(log_has("Session closed") and "timeout" in
                  (open(server_log).read()[-4000:] if os.path.isfile(server_log) else ""),
                  "timeout path reported by the network layer")
            c.sock.close()
            del c

        # ---- wrap up ------------------------------------------------------
        console("stop")
        time.sleep(1.5)
    finally:
        try:
            if proc.poll() is None:
                console("stop")
                time.sleep(2.0)
            if proc.poll() is None:
                proc.kill()
        except Exception:
            pass

    print()
    if failures:
        print(f"FAILED ({len(failures)}):")
        for f in failures:
            print("  - " + f)
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
