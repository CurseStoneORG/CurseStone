#!/usr/bin/env python3
"""Natural-chest stream-gap check for CurseStone.

The bug this locks down: a structure chest that lands in an ALREADY
STREAMED chunk (a neighboring chunk's population spilled the block + tile
NBT into a chunk the client holds) never reached that client — no
re-stream, no UpdateBlock, no BlockEntityDataPacket — so the chest block
stayed invisible (0.15 draws the chest model from its block actor) and
tapping the empty spot opened nothing. The teleport-sweep chest check
never caught it because every teleport re-streams the whole ring, which
picks up late landings.

That first fix made every streamed chest tile ANNOUNCED, but the
announcements still rode the same unordered-RELIABLE stream as the chunk
payloads. Plain RELIABLE gives no delivery order: on a real Wi-Fi/LTE
path (datagram reordering is constant) a tile NBT can reach the client
before the tail of its chunk's split fragments, and the 0.15 client
rebuilds a chunk on arrival and destroys the block actors it held for it
— the actor the tile had just bound is wiped and the chest renders
invisible and cannot open. On loopback nothing ever reorders, which is
why the first check stayed green while the real device kept failing.
The wire now carries chunks + their tile NBT on one RELIABLE_ORDERED
channel (the chunk stream), so this check locks the fix at three levels:

One real protocol-84 client boots into a normal-generator world (fixed
seed) and walks a dense, small-step spiral out of spawn — the pacing a
real exploring player produces, where each stop only streams the ring
around it while population of freshly generated chunks keeps spilling
into chunks the client already holds. The client records every streamed
chunk (FullChunkDataPacket), every tile announcement
(BlockEntityDataPacket), the RakNet reliability + ordering channel each
one arrived on, the per-position receive order, and the raw chunk
payloads. After the walk the console saves the world and the check
parses the region files (.mca, TileEntities list) and asserts:

  1. the walk actually streamed chunks and found generated chests
  2. EVERY chest tile persisted in a streamed chunk has a
     BlockEntityDataPacket delivered for its exact position — a chest
     the server keeps but never announced is an invisible chest
  3. every FullChunkData and BlockEntityData arrived RELIABLE_ORDERED
     on the chunk-stream channel (2) — the wire guarantee that tile NBT
     can never overtake its chunk, whatever the network does
  4. every chest tile announcement is processed AFTER the chunk packet
     of its own chunk in receive order
  5. the RECEIVED chunk payload itself holds the chest: block id 54
     with facing 2..5 at every announced chest cell (the wire truth the
     client's actor binds against, not just what the .mca says)

    python3 tools/chest_stream_gap_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import glob
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import (ACK, BATCH_PACKET_ID, DATA_FIRST, DATA_LAST,  # noqa: E402
                                 NACK, Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION)
from structure_chest_check import NbtReader  # noqa: E402

BLOCK_ENTITY_DATA = 0x32
FULL_CHUNK_DATA = 0x34

# Wire layout of the 0.15 FullChunkDataPacket payload (ORDER_COLUMNS,
# 128-height world) — see world/format/Chunk.cpp chunk_wire_serialize_core:
#   32768 block ids (index = x<<11 | z<<7 | y) | 16384 data nibbles | ... 
WIRE_BLOCKS = 32768
WIRE_DATA = 16384

# RakNet reliability classes the client can observe on received frames.
RAK_RELIABLE = 2
RAK_RELIABLE_ORDERED = 3
# The one ordering channel the chunk stream (chunks + their tile NBT) must
# ride — WireStream::ChunkOrdered in the server.
CHUNK_STREAM_CHANNEL = 2


class GapClient(Client):
    """Client + streamed-chunk, tile-announcement and wire-stream capture.

    Overrides drain() with the same frame walk as the base client, but
    keeps the RakNet reliability + ordering channel of every received
    game packet (parse_datagram stays untouched — nine other checks
    unpack its 5-tuple)."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.streamed_chunks = set()  # (cx, cz)
        self.tiles = []               # (x, y, z, compound)
        # ('chunk', cx, cz) / ('tile', x, y, z) in RECEIVE order — the
        # tile-after-its-chunk assertion walks this log.
        self.wire_events = []
        # (pid, reliability, order_channel) per received game packet,
        # receive order — the channel-2 lock reads this.
        self.stream_meta = []
        # (cx, cz) -> raw FullChunkData payload, kept only during the walk
        # (≈83 KB per chunk; freed before the region-file phase).
        self.chunk_payloads = {}
        self._split_meta = {}         # split_id -> (reliability, channel)

    # ---- receive with stream metadata -----------------------------------
    @staticmethod
    def _iter_frames(data):
        """Same encapsulated-frame walk as Client.parse_datagram, plus the
        frame's (reliability, order_channel) on every yield."""
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
                channel = data[i + 3]
                i += 4
            else:
                channel = -1
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
            out.append((data[i:i + plen], has_split, split_count, split_id,
                        split_index, reliability, channel))
            i += plen
        return out

    def drain(self, seconds: float, ack=True):
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = None
            with self._rx_lock:
                if self._rx_queue:
                    data = self._rx_queue.popleft()
            if data is None:
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
            for payload, is_split, count, sid, idx, rel, ch in self._iter_frames(data):
                if not payload:
                    continue
                if is_split:
                    # Every fragment of one split shares one orderIndex and
                    # one channel — the reassembled whole inherits them.
                    self._split_meta.setdefault(sid, (rel, ch))
                    whole = self.add_fragment(payload, count, sid, idx)
                    if whole:
                        meta = self._split_meta.pop(sid, (rel, ch))
                        self._process_game(whole, meta)
                    continue
                self._process_game(payload, (rel, ch))

    def _process_game(self, payload, meta):
        """Base handle_payload's batch unwrap + ping pong, with the frame's
        (reliability, channel) attached to every sub-packet."""
        if payload[0] == 0x00 and len(payload) >= 9:
            self.send_raw(self.send_encapsulated(
                bytes([0x03]) + payload[1:9], reliability=2))
            return
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
                sub = raw[i:i + sublen]
                i += sublen
                self.stream_meta.append((sub[0], meta[0], meta[1]))
                self.on_game_packet(sub)
            return
        self.stream_meta.append((payload[0], meta[0], meta[1]))
        self.on_game_packet(payload)

    def on_game_packet(self, p: bytes):
        if not p:
            return
        if p[0] == BLOCK_ENTITY_DATA and len(p) >= 13:
            x, y, z = struct.unpack(">iii", p[1:13])
            try:
                _, tag = NbtReader(p[13:]).root()
            except (ValueError, IndexError, struct.error):
                tag = None
            if tag is not None:
                self.tiles.append((x, y, z, tag))
            self.wire_events.append(("tile", x, y, z))
            return
        if p[0] == FULL_CHUNK_DATA and len(p) >= 14:
            # Wire shape: [pid][cx i32][cz i32][order u8][dataLen i32][data].
            cx, cz = struct.unpack(">ii", p[1:9])
            data_len = struct.unpack_from(">i", p, 10)[0]
            if data_len < 0 or 14 + data_len > len(p):
                return
            self.streamed_chunks.add((cx, cz))
            self.wire_events.append(("chunk", cx, cz))
            payload = p[14:14 + data_len]
            if len(payload) >= WIRE_BLOCKS + WIRE_DATA:
                self.chunk_payloads[(cx, cz)] = payload
            return
        Client.on_game_packet(self, p)

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([0x07, 1]) +
                       struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() +
                       struct.pack(">H", len(raw)) + raw)


def region_chest_tiles(region_path):
    """Every chest tile of a region file as {(x, y, z, cx, cz)}: reads
    each chunk's TileEntities list (big-endian Anvil NBT, same layout
    mob_persistence_check.py walks)."""
    out = set()
    if not os.path.isfile(region_path):
        return out
    import gzip
    with open(region_path, "rb") as f:
        data = f.read()
    for idx in range(1024):
        raw = struct.unpack_from(">I", data, idx * 4)[0]
        sector_off = (raw >> 8) * 4096
        if sector_off == 0:
            continue
        length = struct.unpack_from(">I", data, sector_off)[0]
        if length <= 5 or sector_off + 4 + length > len(data):
            continue
        compression = data[sector_off + 4]
        payload = data[sector_off + 5:sector_off + 4 + length]
        try:
            if compression == 2:
                payload = zlib.decompress(payload)
            elif compression == 1:
                payload = gzip.decompress(payload)
            else:
                continue
        except (zlib.error, OSError):
            continue
        if len(payload) < 3 or payload[0] != 10:
            continue
        try:
            name_len = struct.unpack_from(">H", payload, 1)[0]
            root, _ = _nbt_read_payload(payload, 3 + name_len, 10)
        except (ValueError, IndexError, struct.error):
            continue
        level = root.get("Level", {})
        if not isinstance(level, dict):
            continue
        for t in level.get("TileEntities", []):
            if not isinstance(t, dict) or t.get("id") != "Chest":
                continue
            try:
                tx, ty, tz = int(t["x"]), int(t["y"]), int(t["z"])
            except (KeyError, TypeError, ValueError):
                continue
            rpx = (idx & 31) - (32 if (idx & 31) > 15 else 0)
            rpz = ((idx >> 5) & 31) - (32 if ((idx >> 5) & 31) > 15 else 0)
            out.add((tx, ty, tz, rpx, rpz))
    return out


def _nbt_read_payload(buf, off, tag_id):
    """Reads one big-endian NBT payload at `off`; returns (value, new_off)."""
    if tag_id == 1:
        return struct.unpack_from(">b", buf, off)[0], off + 1
    if tag_id == 2:
        return struct.unpack_from(">h", buf, off)[0], off + 2
    if tag_id == 3:
        return struct.unpack_from(">i", buf, off)[0], off + 4
    if tag_id == 4:
        return struct.unpack_from(">q", buf, off)[0], off + 8
    if tag_id == 5:
        return struct.unpack_from(">f", buf, off)[0], off + 4
    if tag_id == 6:
        return struct.unpack_from(">d", buf, off)[0], off + 8
    if tag_id == 7:
        n = struct.unpack_from(">i", buf, off)[0]
        off += 4
        return bytes(buf[off:off + n]), off + n
    if tag_id == 8:
        n = struct.unpack_from(">H", buf, off)[0]
        off += 2
        return buf[off:off + n].decode("utf-8", "replace"), off + n
    if tag_id == 9:
        elem = buf[off]
        n = struct.unpack_from(">i", buf, off + 1)[0]
        off += 5
        out = []
        for _ in range(n):
            v, off = _nbt_read_payload(buf, off, elem)
            out.append(v)
        return out, off
    if tag_id == 10:
        out = {}
        while True:
            child = buf[off]
            if child == 0:
                return out, off + 1
            nlen = struct.unpack_from(">H", buf, off + 1)[0]
            name = buf[off + 3:off + 3 + nlen].decode("utf-8", "replace")
            v, off = _nbt_read_payload(buf, off + 3 + nlen, child)
            out[name] = v
    if tag_id == 11:
        n = struct.unpack_from(">i", buf, off)[0]
        off += 4
        vals = struct.unpack_from(">%di" % n, buf, off) if n else ()
        return list(vals), off + 4 * n
    if tag_id == 12:
        n = struct.unpack_from(">i", buf, off)[0]
        off += 4
        vals = struct.unpack_from(">%dq" % n, buf, off) if n else ()
        return list(vals), off + 8 * n
    raise ValueError("unsupported NBT tag id %d" % tag_id)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chest stream-gap check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19176)
    ap.add_argument("--steps", type=int, default=36,
                    help="spiral stops (each ~16 blocks)")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_chestgap_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ChestGap\nview-distance=3\ngamemode=1\nworld-seed=20260417\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("ChestGap\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 4\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"normal\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=open(os.path.join(runtime, "server-stderr.log"), "w"))
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
        deadline = time.time() + 120
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

        a = GapClient(args.host, args.port, "ChestGap", verbose=args.verbose)
        print("stage: handshake+login+spawn")
        if not a.handshake() or not a.login():
            print("FAIL: handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(2.5)
        if a.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        print(f"  eid={a.eid}")

        # Dense small-step spiral out of spawn — the pacing of a real
        # exploring walk: each stop only streams the ring around it while
        # neighboring population keeps spilling into held chunks.
        steps_xz = []
        ring = 0
        while len(steps_xz) < args.steps:
            ring += 1
            for dx in range(-ring, ring + 1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 + dx * 16, 128 - ring * 16))
            for dz in range(-ring + 1, ring + 1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 + ring * 16, 128 + dz * 16))
            for dx in range(ring - 1, -ring - 1, -1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 + dx * 16, 128 + ring * 16))
            for dz in range(ring - 1, -ring, -1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 - ring * 16, 128 + dz * 16))
        for i, (sx, sz) in enumerate(steps_xz):
            if proc.poll() is not None:
                break
            a.say(f"/tp {sx} 90 {sz}")
            a.drain(2.0)

        streamed = a.streamed_chunks
        announced = {(x, y, z) for x, y, z, tag in a.tiles
                     if isinstance(tag, dict) and tag.get("id") == "Chest"}

        # The chest's spawn compound is the era-reference Chest layout:
        # id/x/y/z (+pairx/pairz while paired, +CustomName when named).
        # The Items list is the DISK layout — the era-reference servers
        # never put it on the wire, and the 0.15 client binds its chest
        # contents at open time through ContainerSetContent. A chest
        # compound that drags Items along is the one wire shape that
        # diverges from every reference server whose chests render.
        fat_chests = []
        for x, y, z, tag in a.tiles:
            if not (isinstance(tag, dict) and tag.get("id") == "Chest"):
                continue
            allowed = {"id", "x", "y", "z", "CustomName"}
            if "pairx" in tag or "pairz" in tag:
                allowed |= {"pairx", "pairz"}
            extra = sorted(set(tag.keys()) - allowed)
            if extra:
                fat_chests.append(((x, y, z), extra))

        # Wire-stream truth (the invisible-chest regression lock): chunks
        # and their tile NBT must arrive RELIABLE_ORDERED on the chunk
        # stream, and every chest tile must be processed after its own
        # chunk's packet in receive order.
        bad_stream = sorted({(pid, rel, ch)
                             for pid, rel, ch in a.stream_meta
                             if pid in (FULL_CHUNK_DATA, BLOCK_ENTITY_DATA)
                             and (rel, ch) != (RAK_RELIABLE_ORDERED, CHUNK_STREAM_CHANNEL)})
        chunk_seen = set()
        tiles_before_chunk = []
        for ev in a.wire_events:
            if ev[0] == "chunk":
                chunk_seen.add((ev[1], ev[2]))
            elif (ev[1] >> 4, ev[3] >> 4) not in chunk_seen:
                # Tile tuple is ('tile', x, y, z): the chunk key comes from
                # x and z (ev[1]/ev[3]), never y.
                tiles_before_chunk.append(ev[1:])

        # Wire truth of every announced chest cell: the chunk payload the
        # client received must carry block id 54 with facing 2..5 there —
        # that is the client-side grid the block actor binds against. The
        # disk (.mca) check below never sees this; a serializer that drops
        # the chest cell or its facing nibble passes every disk check and
        # still ships invisible chests.
        wire_bad_state = []
        for (x, y, z, tag) in a.tiles:
            if not (isinstance(tag, dict) and tag.get("id") == "Chest"):
                continue
            payload = a.chunk_payloads.get((x >> 4, z >> 4))
            if payload is None or len(payload) < WIRE_BLOCKS + WIRE_DATA or not (0 <= y < 128):
                wire_bad_state.append(((x, y, z), "no full chunk payload on the wire"))
                continue
            idx = ((x & 15) << 11) | ((z & 15) << 7) | y
            bid = payload[idx]
            nib = payload[WIRE_BLOCKS + (idx >> 1)]
            meta_ = (nib >> 4) if (idx & 1) else (nib & 0x0F)
            if bid != 54 or not (2 <= meta_ <= 5):
                wire_bad_state.append(((x, y, z), (bid, meta_)))

        # The payloads were only needed for the decode above; drop them so
        # the save phase never doubles the check's memory footprint.
        a.chunk_payloads.clear()

        # Flush the world so every held chunk's tiles reach the region
        # files, then read the chests back out of the .mca.
        console("save")
        deadline = time.time() + 25
        while time.time() < deadline and proc.poll() is None:
            time.sleep(1.0)
            try:
                with open(server_log) as f:
                    if "Saved" in f.read():
                        break
            except FileNotFoundError:
                pass
        console("stop")
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()

        world_dir = None
        for cand in glob.glob(os.path.join(runtime, "worlds", "*")):
            if os.path.isdir(os.path.join(cand, "region")):
                world_dir = cand
                break
        if world_dir is None:
            print("FAIL: no world region dir under the runtime")
            return 1

        persisted = set()
        for region in glob.glob(os.path.join(world_dir, "region", "*.mca")):
            persisted |= region_chest_tiles(region)
        streamed_positions = {(x, y, z, cx, cz)
                              for (x, y, z, cx, cz) in persisted
                              if (cx, cz) in streamed}
        missing = sorted(streamed_positions - {(x, y, z, cx, cz)
                                               for (x, y, z) in announced
                                               for (cx, cz) in [(x >> 4, z >> 4)]})

        # THE assertion that mirrors what the 0.15 client does with the
        # stream: a chest tile over a non-chest block cannot bind its block
        # actor — the model is drawn by the actor, so the chest renders
        # invisible and tapping it does nothing. Every persisted chest tile
        # must sit on an actual chest block (id 54, facing 2..5). Sections
        # are 16 blocks high (section Y = y // 16); the per-section block
        # index is (y%16)*256 + z*16 + x and the data nibble packs two
        # cells per byte with the LOW nibble for the even index.
        region_cache = {}

        def chest_block_state(cx, cz, x, y, z):
            rx, rz = cx >> 5, cz >> 5
            path = os.path.join(world_dir, "region", f"r.{rx}.{rz}.mca")
            if not os.path.isfile(path):
                return None
            if path not in region_cache:
                with open(path, "rb") as f:
                    region_cache[path] = f.read()
            data = region_cache[path]
            idx = (cx & 31) + (cz & 31) * 32
            raw = struct.unpack_from(">I", data, idx * 4)[0]
            sector_off = (raw >> 8) * 4096
            if sector_off == 0:
                return None
            length = struct.unpack_from(">I", data, sector_off)[0]
            payload = zlib.decompress(data[sector_off + 5:sector_off + 4 + length])
            name_len = struct.unpack_from(">H", payload, 1)[0]
            root, _ = _nbt_read_payload(payload, 3 + name_len, 10)
            level = root.get("Level", {})
            if not isinstance(level, dict):
                return None
            secs = {int(s.get("Y", -1)): s for s in level.get("Sections", [])
                    if isinstance(s, dict)}
            sec = secs.get(y // 16)
            if not sec:
                return None  # whole section missing — no chest block there
            blocks = sec.get("Blocks")
            bdata = sec.get("Data")
            if not isinstance(blocks, bytes) or not isinstance(bdata, bytes):
                return None
            li = (y % 16) * 256 + (z - cz * 16) * 16 + (x - cx * 16)
            bid = blocks[li]
            nib = bdata[li // 2]
            bmeta = (nib >> 4) if (li % 2 == 1) else (nib & 0x0F)
            return (bid, bmeta)

        broken_blocks = []
        for (x, y, z, cx, cz) in sorted(streamed_positions):
            state = chest_block_state(cx, cz, x, y, z)
            if state is None or state[0] != 54 or not (2 <= state[1] <= 5):
                broken_blocks.append(((x, y, z, cx, cz), state))

        check(len(streamed) > 0, "the walk streamed chunks",
              f"chunks={len(streamed)}")
        check(len(announced) > 0, "generated chest tiles announced at all",
              f"tiles={len(a.tiles)} chests={len(announced)}")
        check(len(streamed_positions) > 0,
              "streamed chunks hold persisted chest tiles",
              f"persisted={len(persisted)} in-stream={len(streamed_positions)}")
        check(not missing,
              "every chest in a streamed chunk was announced to the client",
              f"missing={missing[:6]} of {len(missing)}")
        check(not broken_blocks,
              "every chest tile stands on a chest block (id 54, facing 2..5)",
              f"broken={broken_blocks[:4]}")
        check(not fat_chests,
              "chest spawn compounds carry the era-reference keys (no disk layout)",
              f"fat={fat_chests[:3]}")
        check(not bad_stream,
              "chunk + tile packets arrive RELIABLE_ORDERED on the chunk stream (channel 2)",
              f"bad={bad_stream[:4]} of {len(bad_stream)}")
        check(not tiles_before_chunk,
              "every chest tile announcement is processed after its own chunk",
              f"before={tiles_before_chunk[:4]} of {len(tiles_before_chunk)}")
        check(not wire_bad_state,
              "the received chunk payload holds a chest (id 54, facing 2..5) at every announced chest cell",
              f"bad={wire_bad_state[:4]} of {len(wire_bad_state)}")

        print(f"  sweep: chunks={len(streamed)} announced-chests={len(announced)} "
              f"persisted={len(persisted)} in-streamed-chunks={len(streamed_positions)} "
              f"missing={len(missing)} broken-blocks={len(broken_blocks)} fat={len(fat_chests)} "
              f"bad-stream={len(bad_stream)} tiles-before-chunk={len(tiles_before_chunk)} "
              f"wire-bad-state={len(wire_bad_state)}")
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("RESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
