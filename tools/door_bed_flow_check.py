#!/usr/bin/env python3
"""End-to-end door/bed interaction check for a local CurseStone server.

Drives a real RakNet session (MCPE 0.15.10, protocol 84) through the exact
packet sequence a genuine client produces for doors and beds, and verifies
the server answers with the right wire packets:

  door place  -> 2x UpdateBlockPacket (lower+upper) + LevelEventPacket 1052
  door toggle -> UpdateBlockPacket (open bit flipped) + LevelEventPacket 1003
  door break  -> 2x UpdateBlockPacket (air) + LevelEventPacket 2001
  bed place   -> 2x UpdateBlockPacket (foot+head) + LevelEventPacket 1052
  bed use day -> TextPacket "You can only sleep at night", no state change
  bed use night -> SetSpawnPositionPacket + [SLEEP] log + night skipped
                (SetTimePacket to morning + AnimatePacket WAKE_UP)
  bed break   -> 2x UpdateBlockPacket (air)

The script spawns the server itself (fresh flat world, known terrain:
grass surface at y=3, spawn 128,4,128) and drives the console through the
child's stdin, so the whole check is one command:

    python3 tools/door_bed_flow_check.py [--server /path/to/CurseStone]

Exit code 0 = every stage passed; non-zero = first failing stage.
"""
import argparse
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

MAGIC = b"\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd\x12\x34\x56\x78"

# RakLib packet ids (PMMP/RakLib numbering).
UNCONNECTED_PING = 0x01
UNCONNECTED_PONG = 0x1C
OCR1 = 0x05
OCR1_REPLY = 0x06
OCR2 = 0x07
OCR2_REPLY = 0x08
CLIENT_CONNECT = 0x09
SERVER_HANDSHAKE = 0x10
CLIENT_HANDSHAKE = 0x13
ACK = 0xC0
NACK = 0xA0
DATA_FIRST = 0x80
DATA_LAST = 0x8F
BATCH_PACKET_ID = 0x06

# Game-layer ids (protocol 84 / Info.h).
LOGIN = 0x01
PLAY_STATUS = 0x02
TEXT_PACKET = 0x07
SET_TIME_PACKET = 0x08
START_GAME = 0x09
REMOVE_BLOCK_PACKET = 0x12
UPDATE_BLOCK_PACKET = 0x13
LEVEL_EVENT_PACKET = 0x16
MOB_EQUIPMENT_PACKET = 0x1B
USE_ITEM_PACKET = 0x1F
PLAYER_ACTION_PACKET = 0x20
SET_ENTITY_DATA_PACKET = 0x22
SET_SPAWN_POSITION_PACKET = 0x26
ANIMATE_PACKET = 0x27
CONTAINER_SET_SLOT_PACKET = 0x2C
FULL_CHUNK_DATA = 0x34

PLAYER_ACTION_SPAWN_SAME_DIMENSION = 7

PKT_NAMES = {
    LOGIN: "Login", PLAY_STATUS: "PlayStatus", TEXT_PACKET: "Text",
    SET_TIME_PACKET: "SetTime", START_GAME: "StartGame",
    REMOVE_BLOCK_PACKET: "RemoveBlock", UPDATE_BLOCK_PACKET: "UpdateBlock",
    LEVEL_EVENT_PACKET: "LevelEvent", MOB_EQUIPMENT_PACKET: "MobEquipment",
    USE_ITEM_PACKET: "UseItem", PLAYER_ACTION_PACKET: "PlayerAction",
    SET_ENTITY_DATA_PACKET: "SetEntityData",
    SET_SPAWN_POSITION_PACKET: "SetSpawnPosition", ANIMATE_PACKET: "Animate",
    CONTAINER_SET_SLOT_PACKET: "ContainerSetSlot",
    FULL_CHUNK_DATA: "FullChunkData",
}


def b64u(obj) -> str:
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def addr(host: str, port: int) -> bytes:
    # RakNet address: [version=4][ip0..ip3][port BE]
    return b"\x04" + socket.inet_aton(host) + struct.pack(">H", port)


class Client:
    """Same RakNet machinery as linux_login_check.py, extended to (a)
    unwrap EVERY sub-packet of a batch, (b) parse the interesting server
    packets, (c) send game packets with a Slot payload."""

    def __init__(self, host: str, port: int, name: str, verbose=False):
        self.host = host
        self.port = port
        self.name = name
        self.verbose = verbose
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
        except OSError:
            pass
        self.sock.settimeout(0.2)
        self.client_id = int(time.time()) & 0xFFFFFFFF
        self.send_seq = 0
        self.message_index = 0
        self.order_index = [0] * 32
        self.splits = {}
        self.events = []          # decoded server packets we care about
        self.eid = 0
        self.spawn = (128, 4, 128)

    # ---- send ----------------------------------------------------------
    def send_raw(self, payload: bytes):
        self.sock.sendto(payload, (self.host, self.port))

    def send_encapsulated(self, inner: bytes, reliability: int = 3,
                          order_channel: int = 0):
        seq = self.send_seq
        self.send_seq += 1
        flags = (reliability & 0x07) << 5
        bit_len = len(inner) * 8
        rec = bytes([flags, (bit_len >> 8) & 0xFF, bit_len & 0xFF])
        if reliability in (2, 3, 4, 6, 7):
            mi = self.message_index
            self.message_index += 1
            rec += struct.pack("<I", mi)[:3]
        if reliability in (3, 7):
            ch = order_channel & 0x1F
            oi = self.order_index[ch]
            self.order_index[ch] = oi + 1
            rec += struct.pack("<I", oi)[:3] + bytes([ch])
        return bytes([0x84, seq & 0xFF, (seq >> 8) & 0xFF,
                      (seq >> 16) & 0xFF]) + rec + inner

    def send_game(self, payload: bytes):
        self.send_raw(self.send_encapsulated(payload, reliability=3))

    # ---- receive ---------------------------------------------------------
    @staticmethod
    def parse_datagram(data: bytes):
        out = []
        if not data or data[0] < DATA_FIRST or data[0] > DATA_LAST or len(data) < 4:
            return out
        i = 4
        while i + 3 <= len(data):
            flags = data[i]
            reliability = (flags >> 5) & 0x07
            has_split = bool(flags & 0x10)
            bit_len = (data[i + 1] << 8) | data[i + 2]
            i += 3
            plen = (bit_len + 7) // 8
            if reliability in (2, 3, 4, 6, 7):
                i += 3
            if reliability in (1, 4):
                i += 3
            if reliability in (3, 7):
                i += 4
            split_count = split_id = split_index = 0
            if has_split:
                if i + 10 > len(data):
                    break
                split_count = int.from_bytes(data[i:i + 4], "big")
                split_id = int.from_bytes(data[i + 4:i + 6], "big")
                split_index = int.from_bytes(data[i + 6:i + 10], "big")
                i += 10
            if i + plen > len(data):
                break
            out.append((data[i:i + plen], has_split,
                        split_count, split_id, split_index))
            i += plen
        return out

    def add_fragment(self, payload, count, sid, idx):
        slot = self.splits.setdefault(sid, {"count": count, "parts": {}})
        slot["parts"][idx] = payload
        if count <= 0 or len(slot["parts"]) < count:
            return None
        if any(k not in slot["parts"] for k in range(count)):
            return None
        joined = b"".join(slot["parts"][k] for k in range(count))
        del self.splits[sid]
        return joined

    def handle_payload(self, payload: bytes):
        if not payload:
            return
        # Unwrap 0xFE batch: [0xFE][0x06][u32be clen][zlib(stream)] where
        # stream = any number of [u32be len][packet] records.
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] != BATCH_PACKET_ID or len(body) <= 5:
                return
            clen = int.from_bytes(body[1:5], "big")
            try:
                raw = zlib.decompress(body[5:5 + clen])
            except zlib.error:
                return
            i = 0
            while i + 4 <= len(raw):
                sublen = int.from_bytes(raw[i:i + 4], "big")
                i += 4
                if sublen == 0 or i + sublen > len(raw):
                    break
                self.on_game_packet(raw[i:i + sublen])
                i += sublen
            return
        self.on_game_packet(payload)

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if self.verbose:
            print(f"    <- {PKT_NAMES.get(pid, hex(pid))} ({len(p)}B)")
        if pid == START_GAME and len(p) >= 34:
            # seed i32 | dimension u8 | generator i32 | gamemode i32
            # | eid i64 | spawnX i32 | spawnY i32 | spawnZ i32
            self.eid = struct.unpack(">q", p[14:22])[0]
            sx, sy, sz = struct.unpack(">iii", p[22:34])
            self.spawn = (sx, sy, sz)
        elif pid == UPDATE_BLOCK_PACKET and len(p) >= 12:
            # body: x i32 | z i32 | y u8 | blockId u8 | (flags<<4)|data u8
            x, z = struct.unpack(">ii", p[1:9])
            y = p[9]
            bid = p[10]
            meta = p[11] & 0x0F
            self.events.append(("update_block", x, y, z, bid, meta))
        elif pid == LEVEL_EVENT_PACKET and len(p) >= 19:
            evid = struct.unpack(">h", p[1:3])[0]
            data = struct.unpack(">i", p[15:19])[0]
            self.events.append(("level_event", evid, data))
        elif pid == SET_SPAWN_POSITION_PACKET and len(p) >= 13:
            sx, sy, sz = struct.unpack(">iii", p[1:13])
            self.events.append(("set_spawn", sx, sy, sz))
        elif pid == SET_TIME_PACKET and len(p) >= 6:
            t = struct.unpack(">i", p[1:5])[0]
            self.events.append(("set_time", t))
        elif pid == ANIMATE_PACKET and len(p) >= 10:
            action = p[1]
            self.events.append(("animate", action))
        elif pid == TEXT_PACKET:
            self.events.append(("text", bytes(p[:24]).hex()))
        elif pid == SET_ENTITY_DATA_PACKET:
            self.events.append(("set_entity_data",))

    def drain(self, seconds: float, ack=True):
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                data, _ = self.sock.recvfrom(65536)
            except socket.timeout:
                continue
            if not data:
                continue
            if data[0] in (ACK, NACK):
                continue
            if ack and DATA_FIRST <= data[0] <= DATA_LAST and len(data) >= 4:
                rel = (data[4] >> 5) & 0x07
                if rel in (2, 3, 4, 6, 7):
                    self.send_raw(bytes([ACK]) + struct.pack(">H", 1) + b"\x01" +
                                  bytes([data[1], data[2], data[3]]))
            for payload, is_split, count, sid, idx in self.parse_datagram(data):
                if not payload:
                    continue
                if is_split:
                    whole = self.add_fragment(payload, count, sid, idx)
                    if whole:
                        self.handle_payload(whole)
                    continue
                self.handle_payload(payload)

    def wait_event(self, kind, seconds=3.0, predicate=None):
        deadline = time.time() + seconds
        while time.time() < deadline:
            for ev in self.events:
                if ev[0] == kind and (predicate is None or predicate(ev)):
                    return ev
            self.drain(0.15)
        return None

    # ---- handshake + login (same stages as linux_login_check.py) -------
    def handshake(self) -> bool:
        self.send_raw(bytes([UNCONNECTED_PING]) +
                      struct.pack("<Q", int(time.time() * 1000)) + MAGIC)
        if not self.expect_pong(UNCONNECTED_PONG):
            return False
        self.send_raw(bytes([OCR1]) + MAGIC + b"\x06" + b"\x00" * 1400)
        if not self.expect_pong(OCR1_REPLY):
            return False
        self.send_raw(bytes([OCR2]) + MAGIC + addr(self.host, self.port) +
                      struct.pack(">H", 1464) + struct.pack(">Q", self.client_id))
        if not self.expect_pong(OCR2_REPLY):
            return False
        cc = bytes([CLIENT_CONNECT]) + struct.pack(">Q", self.client_id) + \
             struct.pack(">Q", int(time.time() * 1000)) + b"\x00"
        self.send_raw(self.send_encapsulated(cc, reliability=3))
        time.sleep(0.4)
        self.drain(0.6)
        ch = bytes([CLIENT_HANDSHAKE]) + addr(self.host, self.port) + \
            struct.pack(">H", 0) + struct.pack(">H", 1464) + \
            struct.pack(">Q", self.client_id)
        self.send_raw(self.send_encapsulated(ch, reliability=3))
        time.sleep(0.5)
        self.drain(1.0)
        return True

    def expect_pong(self, pid, seconds=2.0) -> bool:
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                data, _ = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            if data and data[0] == pid:
                return True
        return False

    def login(self) -> bool:
        chain_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"displayName": self.name,
                           "identity": "11111111-2222-3333-4444-555555555555",
                           "extraData": {}}) + ".sig")
        chain = json.dumps({"chain": [chain_jwt]}, separators=(",", ":")).encode()
        cdata_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"DisplayName": self.name,
                           "Identity": "11111111-2222-3333-4444-555555555555",
                           "DeviceModel": "Linux", "DeviceOS": 7,
                           "LanguageCode": "en_US",
                           "SkinId": "Standard_Custom"}) + ".sig")
        raw = struct.pack("<I", len(chain)) + chain + \
            struct.pack("<I", len(cdata_jwt)) + cdata_jwt.encode()
        comp = zlib.compress(raw)
        body = struct.pack(">I", 84) + struct.pack(">I", len(comp)) + comp
        self.send_raw(self.send_encapsulated(bytes([LOGIN]) + body, reliability=3))
        deadline = time.time() + 5.0
        while time.time() < deadline:
            self.drain(0.2)
            if any(ev[0] in ("update_block", "set_time", "text") for ev in self.events) \
               or self.eid != 0:
                return True
        return self.eid != 0

    # ---- game packet builders ------------------------------------------
    @staticmethod
    def slot_bytes(item_id: int, count: int = 1, damage: int = 0) -> bytes:
        if item_id <= 0:
            return struct.pack(">h", 0)
        return (struct.pack(">h", item_id) + bytes([count]) +
                struct.pack(">h", damage) + struct.pack("<H", 0))

    def use_item(self, x, y, z, face, item_id=0, damage=0):
        body = (bytes([USE_ITEM_PACKET]) +
                struct.pack(">iii", x, y, z) + bytes([face]) +
                struct.pack(">fff", 0.5, 1.0, 0.5) +
                struct.pack(">fff", x + 0.5, y + 1.0, z + 0.5) +
                struct.pack(">i", 0) +
                self.slot_bytes(item_id, 1, damage))
        self.send_game(body)

    def remove_block(self, x, y, z):
        body = (bytes([REMOVE_BLOCK_PACKET]) +
                struct.pack(">q", self.eid) +
                struct.pack(">i", x) + struct.pack(">i", z) + bytes([y]))
        self.send_game(body)

    def player_action(self, action, x=0, y=0, z=0, face=0):
        body = (bytes([PLAYER_ACTION_PACKET]) +
                struct.pack(">q", self.eid) + struct.pack(">i", action) +
                struct.pack(">iii", x, y, z) + struct.pack(">i", face))
        self.send_game(body)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone door/bed flow check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19137)
    ap.add_argument("--name", default="FlowTester")
    ap.add_argument("--keep", action="store_true",
                    help="keep the temporary runtime for inspection")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_doorbed_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=DoorBedFlow\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  debug-mode: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    proc = subprocess.Popen(
        [args.server], cwd=runtime, env=env,
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

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    try:
        # Wait for the server to finish booting.
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

        c = Client(args.host, args.port, args.name, verbose=args.verbose)
        print("stage: handshake+login")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        # Wait for spawn (chunks streamed + doFirstSpawn).
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        print(f"  eid={c.eid} spawn={c.spawn}")
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1

        # Known flat terrain: grass surface at y=3, spawn (128,4,128).
        door_x, door_z = 130, 130
        bed_x, bed_z = 132, 132

        # ---------------- door place ----------------
        print("stage: door place")
        c.events.clear()
        c.use_item(door_x, 3, door_z, 1, item_id=324)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"]
        snd = [e for e in c.events if e[0] == "level_event" and e[1] == 1052]
        lower = [e for e in ups if e[1] == door_x and e[3] == door_z and e[2] == 4]
        upper = [e for e in ups if e[1] == door_x and e[3] == door_z and e[2] == 5]
        check(len(lower) == 1 and lower[0][4] == 64 and (lower[0][5] & 0x08) == 0,
              "door lower half UpdateBlock (64, no 0x08)")
        check(len(upper) == 1 and upper[0][4] == 64 and (upper[0][5] & 0x08) != 0,
              "door upper half UpdateBlock (64, 0x08 set)")
        check(len(snd) >= 1, "door place sound LevelEvent 1052")

        # ---------------- door toggle ----------------
        print("stage: door toggle (sound)")
        c.events.clear()
        c.use_item(door_x, 4, door_z, 1, item_id=324)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"
               and e[1] == door_x and e[2] == 4 and e[3] == door_z]
        open_bit = bool(ups and (ups[0][5] & 0x04))
        snd = [e for e in c.events if e[0] == "level_event" and e[1] == 1003]
        check(open_bit, "door toggled open (0x04 set in UpdateBlock)")
        check(len(snd) >= 1, "door open sound LevelEvent 1003")

        c.events.clear()
        c.use_item(door_x, 4, door_z, 1, item_id=324)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"
               and e[1] == door_x and e[2] == 4 and e[3] == door_z]
        closed = bool(ups and (ups[0][5] & 0x04) == 0)
        snd = [e for e in c.events if e[0] == "level_event" and e[1] == 1003]
        check(closed, "door toggled closed again")
        check(len(snd) >= 1, "door close sound LevelEvent 1003")

        # ---------------- door break ----------------
        print("stage: door break (both halves)")
        c.events.clear()
        c.remove_block(door_x, 4, door_z)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"]
        air4 = [e for e in ups if e[1] == door_x and e[2] == 4 and e[3] == door_z and e[4] == 0]
        air5 = [e for e in ups if e[1] == door_x and e[2] == 5 and e[3] == door_z and e[4] == 0]
        part = [e for e in c.events if e[0] == "level_event" and e[1] == 2001]
        check(len(air4) >= 1, "clicked half removed (air update)")
        check(len(air5) >= 1, "partner half removed (air update)")
        check(len(part) >= 1, "break particles/sound LevelEvent 2001")

        # ---------------- bed place ----------------
        print("stage: bed place")
        c.events.clear()
        c.use_item(bed_x, 3, bed_z, 1, item_id=355)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"]
        foot = [e for e in ups if e[1] == bed_x and e[3] == bed_z and e[2] == 4]
        head = [e for e in ups if e[0] == "update_block" and e[2] == 4
                and e[4] == 26 and (e[5] & 0x08)
                and not (e[1] == bed_x and e[3] == bed_z)]
        check(len(foot) == 1 and foot[0][4] == 26 and (foot[0][5] & 0x08) == 0,
              "bed foot UpdateBlock (26, no 0x08)")
        check(len(head) >= 1, "bed head UpdateBlock (26, 0x08 set)")
        snd = [e for e in c.events if e[0] == "level_event" and e[1] == 1052]
        check(len(snd) >= 1, "bed place sound LevelEvent 1052")

        # ---------------- bed use during the DAY ----------------
        print("stage: bed use by day (spawn binds, sleep refused)")
        console("time set 1000")
        c.drain(0.8)
        c.events.clear()
        c.use_item(bed_x, 4, bed_z, 1, item_id=0)
        c.drain(1.0)
        spawn_pk = [e for e in c.events if e[0] == "set_spawn"]
        check(len(spawn_pk) >= 1 and spawn_pk[0][2] == 4 and
              abs(spawn_pk[0][1] - bed_x) + abs(spawn_pk[0][3] - bed_z) == 1,
              "day use binds the spawn to the bed head")
        check(not [e for e in c.events if e[0] == "set_time"],
              "day use does not skip the night")

        # /time set night must land on TIME_NIGHT (14000) — the same
        # threshold the bed's sleep check uses (PMMP/Infinity alias the
        # command to Level::TIME_NIGHT; 13000 used to "look like night"
        # while the bed still answered "you can only sleep at night").
        print("stage: /time set night = 14000")
        console("time set night")
        night_cmd = c.wait_event("set_time", 3.0, lambda ev: ev[1] == 14000)
        check(night_cmd is not None, "`time set night` sets TIME_NIGHT (14000)")

        # ---------------- bed use at NIGHT ----------------
        print("stage: bed use at night (sleep + night skip)")
        console("time set 15000")
        got_night = c.wait_event("set_time", 3.0, lambda ev: ev[1] == 15000)
        check(got_night is not None, "SetTimePacket 15000 received")
        c.events.clear()
        c.use_item(bed_x, 4, bed_z, 1, item_id=0)
        sp = c.wait_event("set_spawn", 3.0)
        check(sp is not None, "SetSpawnPositionPacket received (sleep accepted)")
        morning = c.wait_event("set_time", 6.0,
                               lambda ev: ev[1] != 15000)
        check(morning is not None,
              f"night skipped (SetTimePacket {morning[1] if morning else '?'})")
        wake = c.wait_event("animate", 3.0, lambda ev: ev[1] == 3)
        check(wake is not None, "AnimatePacket WAKE_UP(3) received")
        try:
            with open(server_log) as f:
                log_tail = f.read()
            check("[SLEEP]" in log_tail, "server logged [SLEEP]")
        except FileNotFoundError:
            check(False, "server logged [SLEEP] (no server.log)")

        # ---------------- bed break ----------------
        print("stage: bed break (both halves)")
        c.events.clear()
        c.remove_block(bed_x, 4, bed_z)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block" and e[4] == 0 and e[2] == 4]
        check(len(ups) >= 2, f"both bed halves removed ({len(ups)} air updates)")
        restored = [e for e in c.events if e[0] == "set_spawn" and
                    tuple(e[1:4]) == c.spawn]
        check(len(restored) >= 1,
              f"breaking the bed restores the world spawn point (spawn={c.spawn})")

        # Also verify the world state persisted the removal: re-place a
        # door in the same cells must succeed (cells were freed).
        c.events.clear()
        c.use_item(door_x, 3, door_z, 1, item_id=324)
        c.drain(1.0)
        lower = [e for e in c.events if e[0] == "update_block"
                 and e[1] == door_x and e[2] == 4 and e[3] == door_z and e[4] == 64]
        check(len(lower) >= 1, "door can be re-placed after break (cells freed)")

        # ---------------- ghost-block self-heal (rejection resync) ---
        # A client tapping a GHOST door (server sees AIR) must get the
        # Infinity sendBlocks rejection resync: UpdateBlockPacket(air) for
        # the clicked cell + the face-adjacent cell, clicker only.
        print("stage: ghost tap resync (use on server-air cell)")
        c.events.clear()
        ghost_x, ghost_z = 134, 130
        c.use_item(ghost_x, 4, ghost_z, 0, item_id=0)   # face 0 = bottom
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"]
        tgt = [e for e in ups if e[1] == ghost_x and e[3] == ghost_z
               and e[2] == 4 and e[4] == 0]
        side = [e for e in ups if e[1] == ghost_x and e[3] == ghost_z
                and e[2] == 3 and e[4] == 2]   # grass below (face 0)
        check(len(tgt) >= 1, "ghost tap resync: clicked cell sent as air")
        check(len(side) >= 1, "ghost tap resync: adjacent cell re-sent")

        print("stage: ghost break resync (break on server-air cell)")
        c.events.clear()
        c.remove_block(ghost_x, 4, ghost_z)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"
               and e[1] == ghost_x and e[3] == ghost_z and e[2] == 4 and e[4] == 0]
        check(len(ups) >= 1, "ghost break resync: air broadcast received")

        print()
        if failures:
            print(f"RESULT: {len(failures)} FAILURES: {failures}")
            return 2
        print("RESULT: ALL CHECKS PASSED")
        return 0
    finally:
        try:
            console("stop")
        except Exception:
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        if args.keep:
            print(f"runtime kept at {runtime}")
        else:
            shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
