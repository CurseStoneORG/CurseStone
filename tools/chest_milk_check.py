#!/usr/bin/env python3
"""End-to-end milk bucket + chest lid/sound check for a local CurseStone server.

One or two real protocol-84 clients (MCPE 0.15.10) drive every path over
the wire and verify the server answers with the right packets:

milk (BUCKET aux 1 — id 335 never appears on the 0.15 wire):
  1. creative slot grant lands as (325, meta 1) in the inventory
  2. swiftness potion still drinks (ADD speed) — eat path regression guard
  3. milk drink removes the effect (MobEffect EVENT_REMOVE) + eat echo
     EntityEvent(9) addressed to the real runtime id
  4. milk stack is swapped for an empty bucket (325, meta 0)
  5. repeated eat events with the empty bucket do nothing (no drink loop,
     no residue duplication, no effect packets)
  6. water bucket (325:8) is not edible
  7. milk drinks at full hunger (alwaysEdible for the milk variant)

chest (BlockEventPacket drives the lid animation + open/close sounds):
  8. chest block places (UpdateBlock id 54)
  9. opener gets ContainerOpen + BlockEvent(case1=1) at the chest coords
 10. a second client in the same chunk also sees the open block event
 11. a second opener does NOT duplicate the open event (one per session)
 12. first client closing does NOT close the lid while one viewer remains
 13. the last viewer closing sends BlockEvent(case1=0) to everyone
 14. reopening after a full open/close cycle fires a fresh open event

The script spawns the server itself (fresh flat world, known terrain:
grass surface at y=3, spawn 128,4,128) and drives the console through the
child's stdin:

    python3 tools/chest_milk_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

# Game-layer ids (protocol 84 / Info.h) this suite adds on top of the
# door/bed client's parsing.
BLOCK_EVENT = 0x17
ENTITY_EVENT = 0x18
MOB_EFFECT = 0x19
CONTAINER_OPEN = 0x2A
CONTAINER_CLOSE = 0x2B
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E
MOB_EQUIPMENT = 0x1B

# Item ids / variants.
BUCKET = 325
MILK_META = 1
WATER_META = 8
POTION = 373
META_SWIFTNESS = 14
CHEST_BLOCK = 54

EVENT_ADD = 1
EVENT_REMOVE = 3
EAT_EVENT = 9  # EntityEvent "finished eating/drinking"


def b64u(obj) -> str:
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


class MilkClient(Client):
    """door_bed_flow_check Client + per-name login uuid, milk/chest packet
    parsing and the game helpers this suite needs."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.effect_pkts = []   # (event, effectId, amplifier, duration)
        self.block_events = []  # (x, y, z, case1, case2)
        self.container_opens = []  # (windowid, type, x, y, z)
        self.container_closes = []  # windowid
        self.inv = {}           # {(id, meta): count} from window-0 dumps
        self.eat_echo_eids = []  # EntityEvent echoes addressed to us

    # ---- login with a per-name uuid (two clients must not collide) ------
    def login(self) -> bool:
        uid = "11111111-2222-3333-4444-%012d" % (abs(hash(self.name)) % 10**12)
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

    # ---- extra packet parsing -------------------------------------------
    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == MOB_EFFECT and len(p) >= 17:
            eid = struct.unpack(">q", p[1:9])[0]
            event = p[9]
            eff = p[10]
            amp = p[11]
            particles = p[12]
            dur = struct.unpack(">i", p[13:17])[0]
            self.effect_pkts.append((eid, event, eff, amp, dur, particles))
            return
        if pid == BLOCK_EVENT and len(p) >= 21:
            x, y, z, c1, c2 = struct.unpack(">iiiii", p[1:21])
            self.block_events.append((x, y, z, c1, c2))
            return
        if pid == CONTAINER_OPEN and len(p) >= 17:
            win = p[1]
            typ = p[2]
            x, y, z = struct.unpack(">iii", p[5:17])
            self.container_opens.append((win, typ, x, y, z))
            return
        if pid == CONTAINER_CLOSE and len(p) >= 2:
            self.container_closes.append(p[1])
            return
        if pid == ENTITY_EVENT and len(p) >= 10:
            eid = struct.unpack(">q", p[1:9])[0]
            ev = p[9]
            if eid == self.eid and ev == EAT_EVENT:
                self.eat_echo_eids.append(eid)
            return
        if pid == CONTAINER_SET_CONTENT and len(p) >= 4:
            self.parse_inventory(p)
            return
        Client.on_game_packet(self, p)

    def parse_inventory(self, p: bytes):
        """window-0 ContainerSetContent dump with META-aware slot parsing:
        slot = id(i16be) count(u8) meta(i16be) nbtlen(u16le) [nbt]."""
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
                continue  # empty slot: 2 bytes only
            if off + 5 > len(p):
                return
            cnt = p[off]
            meta = struct.unpack(">h", p[off + 1:off + 3])[0]
            nlen = struct.unpack("<H", p[off + 3:off + 5])[0]
            off += 5 + nlen
            key = (sid, meta)
            totals[key] = totals.get(key, 0) + cnt
        # trailing hotbar dump on window 0
        if win == 0 and off + 2 <= len(p):
            hb = struct.unpack(">H", p[off:off + 2])[0]
            off += 2 + 4 * hb
        if win == 0:
            self.inv = totals

    # ---- game helpers -----------------------------------------------------
    def creative_take(self, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

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

    def eat(self):
        """Finish-drinking event, like the real client at the end of the
        1.6 s eating animation."""
        self.entity_event(EAT_EVENT)

    def container_close(self, windowid):
        self.send_game(bytes([CONTAINER_CLOSE, windowid]))

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([0x07, 1]) + struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() + struct.pack(">H", len(raw)) + raw)

    def effects_for(self, effect_id, since=0):
        return [e for e in self.effect_pkts[since:] if e[2] == effect_id]


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone milk/chest flow check")
    ap.add_argument("--server", default="/home/z/my-project/CurseStone/build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19139)
    ap.add_argument("--keep", action="store_true",
                    help="keep the temporary runtime for inspection")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_chestmilk_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ChestMilk\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("MilkOne\nMilkTwo\n")
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
            print("FAIL: server never printed Done")
            return 1

        a = MilkClient(args.host, args.port, "MilkOne", verbose=args.verbose)
        print("stage: client A handshake+login")
        if not a.handshake() or not a.login():
            print("FAIL: A handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(1.5)
        print(f"  eid={a.eid} spawn={a.spawn}")
        if a.eid == 0:
            print("FAIL: A never received StartGame (no eid)")
            return 1

        # ---------------- milk ----------------
        print("stage: milk bucket clears effects (325:1)")
        # Creative-stock: survival slot writes are validated away, so the
        # grant runs in creative mode (same dance as potion_check).
        def missing():
            return [(s, i, c, m) for (s, i, c, m) in
                    ((0, POTION, 1, META_SWIFTNESS),
                     (1, BUCKET, 1, MILK_META),
                     (2, BUCKET, 1, WATER_META),
                     (3, CHEST_BLOCK, 1, 0))
                    if a.inv.get((i, m), 0) < c]

        def stock():
            need = missing()
            if not need:
                return
            a.say("/gamemode 1")
            time.sleep(0.8)
            for slot, item_id, cnt, meta in need:
                a.mob_equipment(slot, item_id, meta)
                time.sleep(0.1)
                a.creative_take(slot, item_id, cnt, meta)
                time.sleep(0.1)
            a.say("/gamemode 0")
            time.sleep(0.8)
            a.drain(0.8)

        for _ in range(3):
            stock()
            if not missing():
                break
        check(a.inv.get((POTION, META_SWIFTNESS), 0) == 1 and
              a.inv.get((BUCKET, MILK_META), 0) == 1 and
              a.inv.get((BUCKET, WATER_META), 0) == 1,
              "creative grant lands as (325:1) + (325:8) + potion",
              f"inv={a.inv}")

        # effect source: drink the swiftness potion first (proven eat path)
        a.mob_equipment(0, POTION, META_SWIFTNESS)
        time.sleep(0.3)
        since = len(a.effect_pkts)
        a.eat()
        a.drain(1.2)
        adds = [e for e in a.effect_pkts[since:] if e[1] == EVENT_ADD]
        check(len(adds) == 1 and adds[0][2] == 1,
              "swiftness potion still drinks (ADD speed)", f"pkts={a.effect_pkts[since:]}")

        # milk drink — full hunger on purpose (alwaysEdible variant gate).
        # Wait out the 1.6 s per-drink cooldown first: a second eat event
        # inside the window is silently dropped (same as the real client).
        time.sleep(1.8)
        since = len(a.effect_pkts)
        echoes = len(a.eat_echo_eids)
        a.mob_equipment(1, BUCKET, MILK_META)
        time.sleep(0.3)
        a.eat()
        a.drain(1.4)
        removes = [e for e in a.effect_pkts[since:] if e[1] == EVENT_REMOVE]
        check(len(removes) == 1 and removes[0][2] == 1,
              "milk drink removes the speed effect (MobEffect REMOVE)",
              f"pkts={a.effect_pkts[since:]}")
        check(len(a.eat_echo_eids) > echoes and
              all(e == a.eid for e in a.eat_echo_eids[echoes:]),
              "eat echo EntityEvent(9) addressed to the real runtime id",
              f"echoes={a.eat_echo_eids[echoes:]}")
        check(a.inv.get((BUCKET, MILK_META), 0) == 0 and
              a.inv.get((BUCKET, 0), 0) >= 1,
              "milk stack swapped for an empty bucket (325:0)",
              f"inv={a.inv}")

        # drink-loop guard: eat events with the empty bucket must no-op
        since = len(a.effect_pkts)
        a.eat()
        a.drain(0.6)
        a.eat()
        a.drain(0.6)
        check(len(a.effect_pkts) == since,
              "empty bucket eat events: no effect packets (no loop)",
              f"pkts={a.effect_pkts[since:]}")
        check(a.inv.get((BUCKET, 0), 0) == 1 and
              a.inv.get((BUCKET, MILK_META), 0) == 0,
              "empty bucket eat events: no residue duplication",
              f"inv={a.inv}")

        # water bucket is not edible
        a.mob_equipment(2, BUCKET, WATER_META)
        time.sleep(0.3)
        a.eat()
        a.drain(0.8)
        check(a.inv.get((BUCKET, WATER_META), 0) == 1,
              "water bucket (325:8) is not edible", f"inv={a.inv}")

        # ---------------- chest ----------------
        print("stage: chest lid block events")
        # chest in slot 3 already (stocked above) — no re-equip needed
        chest_x, chest_z = 134, 134
        a.events.clear()
        a.use_item(chest_x, 3, chest_z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        ups = [e for e in a.events if e[0] == "update_block"
               and e[1] == chest_x and e[2] == 4 and e[3] == chest_z]
        check(bool(ups) and ups[0][4] == CHEST_BLOCK,
              "chest block places (UpdateBlock id 54)", f"ups={a.events}")

        # second client joins and shares the chunk
        b = MilkClient(args.host, args.port, "MilkTwo", verbose=args.verbose)
        if not b.handshake() or not b.login():
            print("FAIL: B handshake/login")
            return 1
        # Keep A's session alive while B loads in: the 5s session timeout
        # reaps a client that stops answering keepalive pings, and A only
        # answers while its own drain loop runs. Interleave both drains.
        deadline = time.time() + 4.0
        while time.time() < deadline:
            b.drain(0.2)
            a.drain(0.2)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        deadline = time.time() + 1.5
        while time.time() < deadline:
            b.drain(0.2)
            a.drain(0.2)
        print(f"  B eid={b.eid}")

        # A opens the chest
        a.block_events.clear()
        b.block_events.clear()
        b.container_opens.clear()
        a.container_opens.clear()
        a.use_item(chest_x, 4, chest_z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        b.drain(0.8)  # B must READ the broadcast too before we assert on it
        a_open = [e for e in a.block_events
                  if e[:3] == (chest_x, 4, chest_z) and e[3] == 1]
        check(bool(a_open), "opener gets BlockEvent(case1=1) at chest coords",
              f"A={a.block_events}")
        win = a.container_opens[-1][0] if a.container_opens else 0
        check(bool(a.container_opens) and
              a.container_opens[-1][1:] == (0, chest_x, 4, chest_z) and win >= 2,
              "opener gets ContainerOpen(type container, real window id)",
              f"opens={a.container_opens}")
        b_open = [e for e in b.block_events
                  if e[:3] == (chest_x, 4, chest_z) and e[3] == 1]
        check(bool(b_open), "second client in the same chunk sees the open event",
              f"B={b.block_events}")
        check(not b.container_opens,
              "second client does NOT get the container window", f"B={b.container_opens}")

        # B opens too — the lid is already open, no duplicate event
        a.block_events.clear()
        b.block_events.clear()
        b.use_item(chest_x, 4, chest_z, 1)
        b.drain(1.2)
        a.drain(0.6)
        dup_open = ([e for e in a.block_events if e[3] == 1] +
                    [e for e in b.block_events if e[3] == 1])
        check(not dup_open,
              "second opener does NOT duplicate the open event",
              f"A={a.block_events} B={b.block_events}")

        # A closes — lid must stay open for B
        a.block_events.clear()
        b.block_events.clear()
        a.container_close(win)
        a.drain(1.2)
        b.drain(0.6)
        close_events = ([e for e in a.block_events if e[3] == 0] +
                        [e for e in b.block_events if e[3] == 0])
        check(not close_events and win in a.container_closes,
              "first viewer closing: close ack but lid stays open",
              f"close={a.container_closes} ev={close_events}")

        # B closes — last viewer, lid closes for everyone
        a.block_events.clear()
        b.block_events.clear()
        b.container_close(win)
        b.drain(1.2)
        a.drain(0.8)
        a_closed = [e for e in a.block_events
                    if e[:3] == (chest_x, 4, chest_z) and e[3] == 0]
        b_closed = [e for e in b.block_events
                    if e[:3] == (chest_x, 4, chest_z) and e[3] == 0]
        check(bool(a_closed) and bool(b_closed),
              "last viewer closing sends BlockEvent(case1=0) to everyone",
              f"A={a.block_events} B={b.block_events}")

        # full cycle reset: reopening fires a fresh open event
        a.block_events.clear()
        a.use_item(chest_x, 4, chest_z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        re_open = [e for e in a.block_events
                   if e[:3] == (chest_x, 4, chest_z) and e[3] == 1]
        check(bool(re_open),
              "reopening after a full cycle fires a fresh open event",
              f"A={a.block_events}")

        console("stop")
        time.sleep(1.0)
    finally:
        try:
            console("stop")
        except Exception:
            pass
        time.sleep(0.5)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED ({len(failures)}):")
        for f in failures:
            print("  - " + f)
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
