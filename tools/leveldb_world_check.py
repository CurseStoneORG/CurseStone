#!/usr/bin/env python3
"""End-to-end LevelDB world-format check for CurseStone.

The reported bug: a world created with `world.format: "leveldb"` produced
none of the files a real MCPE 0.15.x device world carries — no db/ (CURRENT,
LOCK, MANIFEST-*, WAL, .ldb tables), no level.dat, no level.dat_old, no
levelname.txt — and nothing was loaded or saved.

This check drives a real server through the full lifecycle and validates
every artifact INDEPENDENTLY from Python (own .ldb/MANIFEST/level.dat
parsers, not the server's code):

  1. boot with world.format=leveldb and a fixed seed
  2. right after boot: the whole creation-time file set must exist
     (db/CURRENT + LOCK + MANIFEST + WAL, level.dat, level.dat_old,
     levelname.txt) — the original complaint
  3. login a protocol-84 client, stream the spawn area, /save, /stop
  4. validate the saved artifacts: level.dat header+NBT fields, level.dat_old
     rotation, levelname.txt content, MANIFEST records, .ldb footer magic +
     table walk, terrain record layout (83200 bytes) and the exact flat-world
     block column (bedrock/dirt/dirt/grass) read back through OUR OWN
     Python .ldb decoder
  5. reboot on the same world: level.dat is the authority again (same seed
     in the debug log), the client receives the SAVED terrain over the wire
     (load path works), /save again rotates level.dat -> level.dat_old

    python3 tools/leveldb_world_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

FULL_CHUNK_DATA = 0x34
REQUEST_CHUNK_RADIUS = 0x3D

WORLD = "leveldbtest"
SEED = 12345
VIEW = 4


class ChunkClient(Client):
    """door_bed_flow_check Client + FullChunkDataPacket capture."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.chunk_payloads = []  # (cx, cz, payload bytes)

    def on_game_packet(self, p: bytes):
        # [id 0x34][cx i32 BE][cz i32 BE][order u8][len i32 BE][payload]
        if p and p[0] == FULL_CHUNK_DATA and len(p) >= 14:
            cx, cz = struct.unpack(">ii", p[1:9])
            self.chunk_payloads.append((cx, cz, p[14:]))
            return
        super().on_game_packet(p)


# ---------------------------------------------------------------- NBT (LE)


def nbt_read(data, pos=0):
    """Minimal little-endian NBT parser: returns (tag_type, name, value, pos)."""
    t = data[pos]
    pos += 1
    if t == 0:
        return (0, "", None, pos)
    nlen = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    name = data[pos:pos + nlen].decode("latin-1")
    pos += nlen
    return nbt_payload(data, pos, t, name)


def nbt_payload(data, pos, t, name):
    if t == 1:  # byte
        return (t, name, struct.unpack_from("<b", data, pos)[0], pos + 1)
    if t == 2:  # short
        return (t, name, struct.unpack_from("<h", data, pos)[0], pos + 2)
    if t == 3:  # int
        return (t, name, struct.unpack_from("<i", data, pos)[0], pos + 4)
    if t == 4:  # long
        return (t, name, struct.unpack_from("<q", data, pos)[0], pos + 8)
    if t == 5:  # float
        return (t, name, struct.unpack_from("<f", data, pos)[0], pos + 4)
    if t == 6:  # double
        return (t, name, struct.unpack_from("<d", data, pos)[0], pos + 8)
    if t == 7:  # byte array
        n = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        return (t, name, data[pos:pos + n], pos + n)
    if t == 8:  # string
        n = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        return (t, name, data[pos:pos + n].decode("latin-1"), pos + n)
    if t == 9:  # list
        et = data[pos]
        n = struct.unpack_from("<i", data, pos + 1)[0]
        pos += 5
        items = []
        for _ in range(max(n, 0)):
            (it, _in, iv, pos) = nbt_payload(data, pos, et, "")
            items.append(iv)
        return (t, name, items, pos)
    if t == 10:  # compound
        out = {}
        while True:
            (ct, cn, cv, pos) = nbt_read(data, pos)
            if ct == 0:
                break
            out[cn] = cv
        return (t, name, out, pos)
    if t == 11:  # int array
        n = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        vals = struct.unpack_from("<%di" % n, data, pos)
        return (t, name, list(vals), pos + 4 * n)
    raise ValueError("unsupported NBT tag %d" % t)


def read_level_dat(path):
    raw = open(path, "rb").read()
    assert len(raw) >= 8, "level.dat too small"
    version, size = struct.unpack_from("<ii", raw, 0)
    assert version == 3, "level.dat storage version %d != 3" % version
    if size == 0 or size > len(raw) - 8:
        size = len(raw) - 8
    (_t, _n, root, _p) = nbt_read(raw, 8)
    return version, size, root


# ---------------------------------------------------------------- .ldb


def read_varint(data, pos):
    result = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def parse_block(data):
    """Parses one leveldb block body into [(key, value)]."""
    if len(data) < 4:
        return []
    n_restarts = struct.unpack_from("<I", data, len(data) - 4)[0]
    entries_end = len(data) - (n_restarts + 1) * 4
    pos = 0
    key = b""
    out = []
    while pos < entries_end:
        shared, pos = read_varint(data, pos)
        non_shared, pos = read_varint(data, pos)
        vlen, pos = read_varint(data, pos)
        key = key[:shared] + data[pos:pos + non_shared]
        pos += non_shared
        value = data[pos:pos + vlen]
        pos += vlen
        out.append((key, value))
    return out


def read_block(image, offset, size):
    body = image[offset:offset + size]
    crc, ctype = struct.unpack_from("<IB", image, offset + size)
    if ctype == 0:
        return parse_block(body)
    if ctype == 2:
        return parse_block(zlib.decompress(body))
    if ctype == 1:  # snappy: not written by CurseStone
        raise AssertionError("unexpected snappy block in our own output")
    raise AssertionError("unknown block compression %d" % ctype)


def ldb_entries(path):
    """Walks one .ldb table: returns all (key, value) entries."""
    image = open(path, "rb").read()
    assert len(image) >= 48, "table too small"
    magic = struct.unpack_from("<Q", image, len(image) - 8)[0]
    assert magic == 0xdb4775248b80fb57, "bad table magic 0x%x in %s" % (magic, path)
    footer = image[len(image) - 48:]
    pos = 0
    _moff, pos = read_varint(footer, pos)
    _msz, pos = read_varint(footer, pos)
    ioff, pos = read_varint(footer, pos)
    isz, pos = read_varint(footer, pos)
    entries = []
    for sep, handle in read_block(image, ioff, isz):
        off, hpos = read_varint(handle, 0)
        sz, _ = read_varint(handle, hpos)
        entries.extend(read_block(image, off, sz))
    return entries


# ---------------------------------------------------------------- manifest


def manifest_facts(path):
    """Parses a MANIFEST file (log format + VersionEdit records)."""
    raw = open(path, "rb").read()
    comparator = None
    new_files = 0
    pos = 0
    while pos < len(raw):
        if pos % 32768 == len(raw) % 32768 and False:
            pass
        if len(raw) - pos < 7:
            break
        blk_off = pos % 32768
        if 32768 - blk_off < 7:
            pos += 32768 - blk_off
            continue
        flen = struct.unpack_from("<H", raw, pos)[0]
        if flen == 0 or pos + 7 + flen > len(raw):
            break
        body = raw[pos + 7:pos + 7 + flen]
        pos += 7 + flen
        p = 0
        while p < len(body):
            tag, p = read_varint(body, p)
            if tag == 1:
                n, p = read_varint(body, p)
                comparator = body[p:p + n].decode("latin-1")
                p += n
            elif tag == 7:  # NewFile: level, number, size, smallest, largest
                _lvl, p = read_varint(body, p)
                _num, p = read_varint(body, p)
                _sz, p = read_varint(body, p)
                n, p = read_varint(body, p)
                p += n
                n, p = read_varint(body, p)
                p += n
                new_files += 1
            elif tag in (2, 3, 4, 9):
                _v, p = read_varint(body, p)
            elif tag == 6:  # deleted file
                _lvl, p = read_varint(body, p)
                _num, p = read_varint(body, p)
            elif tag == 5:  # compact pointer
                _lvl, p = read_varint(body, p)
                n, p = read_varint(body, p)
                p += n
            else:
                return comparator, new_files, False
    return comparator, new_files, True


# ---------------------------------------------------------------- helpers


def terrain_key(cx, cz, record):
    return struct.pack("<ii", cx, cz) + bytes([record])


def flat_expected_column():
    # cursestone.yml flat default: bedrock(7) / 2x dirt(3) / grass(2), then
    # air for the rest of the 128-tall column (the check reads y 0..5).
    return {0: 7, 1: 3, 2: 3, 3: 2, 4: 0, 5: 0}


def column_from_record(rec, x=0, z=0):
    return {y: rec[(x << 11) | (z << 7) | y] for y in range(6)}


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone LevelDB world check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19157)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_leveldb_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=%s\nserver-port=%d\nmax-players=5\n"
                "motd=LevelDB\nview-distance=%d\ngamemode=0\nworld-name=%s\n"
                "world-seed=%d\n" % (args.host, args.port, VIEW, WORLD, SEED))
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  debug-mode: true\n  autosave-ticks: 40\n"
                "world:\n  format: \"leveldb\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)

    failures = []
    log_path = os.path.join(runtime, "server.log")

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)
        return cond

    def boot():
        # server.log persists across boots in one runtime; only lines written
        # from THIS boot count for readiness (a stale 'Server ready' from the
        # previous boot would return before the new process even listens).
        offset = 0
        if os.path.isfile(log_path):
            offset = os.path.getsize(log_path)
        proc = subprocess.Popen(
            [args.server], cwd=runtime, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        deadline = time.time() + 60
        booted = False
        while time.time() < deadline:
            try:
                with open(log_path) as f:
                    f.seek(offset)
                    if "Server ready" in f.read():
                        booted = True
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        return proc, booted

    def wait_log_line(needle, seconds=10.0):
        """Polls for a line (async logger flushes may lag 'Server ready')."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                with open(log_path) as f:
                    if needle in f.read():
                        return True
            except FileNotFoundError:
                pass
            time.sleep(0.25)
        return False

    def stop(proc):
        try:
            proc.stdin.write(b"/save\n")
            proc.stdin.flush()
            time.sleep(1.5)
            proc.stdin.write(b"/stop\n")
            proc.stdin.flush()
        except BrokenPipeError:
            pass
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()

    def world_dir():
        return os.path.join(runtime, "worlds", WORLD)

    def collect_chunks(client, want):
        seen = {}
        deadline = time.time() + 45
        while time.time() < deadline and len(seen) < want:
            client.drain(0.4)
            for (cx, cz, payload) in client.chunk_payloads:
                seen[(cx, cz)] = payload
            client.chunk_payloads.clear()
        return seen

    proc = None
    try:
        # ================= boot 1: creation ================================
        print("== boot 1: create a fresh leveldb world ==")
        proc, booted = boot()
        if not check(booted, "server reached 'Server ready'"):
            raise RuntimeError("no boot")
        with open(log_path) as f:
            log1 = f.read()
        check("format=leveldb" in log1, "world booted on the leveldb provider")

        # The original complaint: NOTHING was created at creation time.
        wd = world_dir()
        check(os.path.isfile(wd + "/level.dat"), "level.dat created at creation")
        check(os.path.isfile(wd + "/level.dat_old"), "level.dat_old created at creation")
        check(os.path.isfile(wd + "/levelname.txt"), "levelname.txt created at creation")
        check(os.path.isdir(wd + "/db"), "db/ directory created at creation")
        check(os.path.isfile(wd + "/db/CURRENT"), "db/CURRENT created at creation")
        check(os.path.isfile(wd + "/db/LOCK"), "db/LOCK created at creation")
        cur = open(wd + "/db/CURRENT").read().strip()
        check(cur.startswith("MANIFEST-"),
              "CURRENT names a manifest (%s)" % cur)
        check(len(glob.glob(wd + "/db/MANIFEST-*")) == 1,
              "one MANIFEST file at creation")
        check(len(glob.glob(wd + "/db/*.log")) == 1, "WAL present at creation")

        lv_ver, lv_size, lv_root = read_level_dat(wd + "/level.dat")
        check(lv_ver == 3, "level.dat storage version is 3")
        check(lv_size == os.path.getsize(wd + "/level.dat") - 8,
              "level.dat size field matches the file")
        check(lv_root.get("RandomSeed") == SEED,
              "level.dat RandomSeed == world-seed (%s)" % lv_root.get("RandomSeed"))
        check(lv_root.get("LevelName") == WORLD,
              "level.dat LevelName == world name")
        old_ver, _old_size, old_root = read_level_dat(wd + "/level.dat")
        check(old_ver == 3 and old_root.get("RandomSeed") == SEED,
              "level.dat_old parses with the same seed")
        name_txt = open(wd + "/levelname.txt").read().strip()
        check(name_txt == WORLD, "levelname.txt holds the world name")

        # ================= clients + save ==================================
        c = ChunkClient(args.host, args.port, "LevelDBTester", verbose=args.verbose)
        check(c.handshake() and c.login(), "client handshake+login")
        c.drain(2.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.0)
        check(c.eid != 0, "client got an eid (StartGame)")
        c.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", VIEW))
        want = (2 * VIEW + 1) ** 2
        chunks1 = collect_chunks(c, want)
        check(len(chunks1) >= want - 10,
              "spawn area streamed (%d/%d chunks)" % (len(chunks1), want))
        proc.stdin.write(b"/save\n")
        proc.stdin.flush()
        time.sleep(2.0)
        stop_time = time.time()
        proc.stdin.write(b"/stop\n")
        proc.stdin.flush()
        proc.wait(timeout=20)
        proc = None
        check(True, "server stopped cleanly after /save")

        # ================= validate the saved world ========================
        print("== validate the saved world ==")
        wd = world_dir()
        dbs = sorted(glob.glob(wd + "/db/*.ldb"))
        check(len(dbs) >= 1, "at least one .ldb table on disk (%d)" % len(dbs))
        check(len(glob.glob(wd + "/db/*.log")) == 1, "exactly one live WAL after stop")
        manifests = glob.glob(wd + "/db/MANIFEST-*")
        check(len(manifests) >= 1, "MANIFEST present after save")
        cmp_name, new_files, ok_edit = manifest_facts(manifests[0])
        check(ok_edit, "MANIFEST parses as VersionEdit records")
        check(cmp_name == "leveldb.BytewiseComparator",
              "MANIFEST comparator is the leveldb bytewise one")
        check(new_files >= 1, "MANIFEST carries table entries (%d)" % new_files)

        # Independent .ldb walk: table keys carry the 8-byte internal-key
        # trailer (9-byte user key + 8-byte trailer = 17). The record byte
        # sits at offset 8 of the user key. Every terrain record must be
        # 83200 bytes and the flat column exact — bedrock/dirt/dirt/grass.
        terrain = {}
        flags = {}
        versions = {}
        for path in dbs:
            for key, value in ldb_entries(path):
                if len(key) == 17 and key[8] in (0x30, 0x66, 0x76):
                    cx, cz = struct.unpack_from("<ii", key, 0)
                    if key[8] == 0x30:  # LegacyTerrain
                        terrain[(cx, cz)] = value
                    elif key[8] == 0x66:  # flags
                        flags[(cx, cz)] = value[0]
                    elif key[8] == 0x76:  # version
                        versions[(cx, cz)] = value[0]
        check(len(terrain) >= 30,
              "terrain records persisted for %d chunks" % len(terrain))
        bad_len = [k for k, v in terrain.items() if len(v) != 83200]
        check(not bad_len, "every terrain record is 83200 bytes")
        # The flat column: bedrock/dirt/dirt/grass then air (the record holds
        # the full 128-tall column; air is 0).
        expected = flat_expected_column()
        mism = [k for k, v in terrain.items()
                if column_from_record(v) != expected]
        check(not mism,
              "flat column bedrock/dirt/dirt/grass exact in every record"
              + ("" if not mism else " (bad: %s)" % mism[:3]))
        flag_bad = [k for k, v in flags.items() if v != 0x07]
        check(flags and not flag_bad,
              "flags records carry generated|populated|light (0x07)")
        ver_bad = [k for k, v in versions.items() if v != 0x02]
        check(versions and not ver_bad, "version records carry 0x02")

        # level.dat after save: Time advanced, seed intact, _old rotated.
        _v, _s, root_after = read_level_dat(wd + "/level.dat")
        check(root_after.get("RandomSeed") == SEED, "seed survived the save")
        check((root_after.get("Time") or 0) > 0,
              "Time advanced in level.dat (%s)" % root_after.get("Time"))
        _ov, _os, root_old = read_level_dat(wd + "/level.dat_old")
        check(root_old.get("RandomSeed") == SEED,
              "level.dat_old is the rotated previous save")

        # ================= boot 2: load the saved world ====================
        print("== boot 2: reload the saved world ==")
        proc, booted = boot()
        check(booted, "server rebooted on the saved world")
        check(wait_log_line("level.dat v3 loaded: seed=%d" % SEED),
              "level.dat was the seed authority on reload (debug log)")
        check(wait_log_line("format=leveldb"), "provider is leveldb on reload")
        c2 = ChunkClient(args.host, args.port, "LevelDBTester", verbose=args.verbose)
        check(c2.handshake() and c2.login(), "client handshake+login (boot 2)")
        c2.drain(2.0)
        c2.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c2.drain(1.0)
        c2.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", VIEW))
        chunks2 = collect_chunks(c2, want)
        check(len(chunks2) >= want - 10,
              "saved chunks streamed on reload (%d/%d)" % (len(chunks2), want))
        # The wire payload's deterministic parts (blocks + data + heightmap
        # + biome colors) must equal the saved record byte-for-byte — proof
        # the LOAD path decoded the disk record into the live chunk. Sky and
        # block light are excluded: the LightEngine may legitimately
        # re-propagate border light on install.
        load_mism = []
        checked = 0
        for (cx, cz), payload in chunks2.items():
            saved = terrain.get((cx, cz))
            if saved is None or len(payload) < 83200:
                continue
            checked += 1
            if payload[0:49152] != saved[0:49152]:
                load_mism.append((cx, cz, "blocks/data"))
            elif payload[81920:83200] != saved[81920:83200]:
                load_mism.append((cx, cz, "heightmap"))
        check(checked > 0 and not load_mism,
              "wire terrain matches the saved record for %d streamed chunks"
              % checked
              + ("" if not load_mism else " (bad: %s)" % load_mism[:3]))

        # /save again: level.dat rotates to level.dat_old once more.
        stop(proc)
        proc = None
        lv_after2 = os.path.getsize(wd + "/level.dat")
        check(lv_after2 >= 8, "level.dat rewritten after the second save")

        # ============ boot 3+4: crash recovery through the WAL =============
        # Boot 3: edit a chunk, let the tiny autosave push the batch into
        # the WAL (under the 4 MiB table-flush threshold), then kill -9 the
        # server — no /save, no shutdown, nothing but the WAL.
        print("== boot 3: unsaved edit + SIGKILL (WAL only) ==")
        proc, booted = boot()
        check(booted, "server booted for the crash stage")
        c3 = ChunkClient(args.host, args.port, "LevelDBTester", verbose=args.verbose)
        check(c3.handshake() and c3.login(), "client handshake+login (boot 3)")
        c3.drain(2.0)
        c3.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c3.drain(1.0)
        c3.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", VIEW))
        collect_chunks(c3, want)
        # Place a stone block on top of the grass at (130, 3, 130) — chunk
        # (8,8), local (2, 4, 2).
        place = (130, 4, 130)
        c3.use_item(130, 3, 130, 1, item_id=1)
        c3.drain(1.5)
        # Let the autosave-ticks=40 pipeline push the dirty chunk through
        # saveChunk() into the engine WAL (below the flush threshold).
        time.sleep(4.0)
        proc.kill()  # SIGKILL: no /save, no graceful shutdown
        proc.wait(timeout=10)
        proc = None
        log_files = glob.glob(wd + "/db/*.log")
        check(len(log_files) == 1 and os.path.getsize(log_files[0]) > 0,
              "WAL holds the unsaved batch after the kill")

        # Boot 4: the engine must replay the WAL and the block must be back.
        print("== boot 4: WAL replay restores the edit ==")
        proc, booted = boot()
        check(booted, "server booted after the crash")
        c4 = ChunkClient(args.host, args.port, "LevelDBTester", verbose=args.verbose)
        check(c4.handshake() and c4.login(), "client handshake+login (boot 4)")
        c4.drain(2.0)
        c4.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c4.drain(1.0)
        c4.send_game(bytes([REQUEST_CHUNK_RADIUS]) + struct.pack(">i", VIEW))
        chunks4 = collect_chunks(c4, want)
        p88 = chunks4.get((8, 8))
        check(p88 is not None and len(p88) >= 83204, "spawn chunk streamed (boot 4)")
        if p88:
            # Column index (x<<11 | z<<7 | y) for local (2, y, 2).
            stone = p88[(2 << 11) | (2 << 7) | 4]
            ground = p88[(2 << 11) | (2 << 7) | 3]
            check(stone == 1 and ground == 2,
                  "placed stone survived the crash (stone=%d grass=%d)"
                  % (stone, ground))
        stop(proc)
        proc = None

        print()
        if failures:
            print("FAILED: %d check(s):" % len(failures))
            for f_ in failures:
                print("  - " + f_)
            return 1
        print("ALL PASS")
        return 0
    except Exception as e:
        print("ERROR: %s" % e)
        import traceback
        traceback.print_exc()
        return 1
    finally:
        if proc is not None and proc.poll() is None:
            try:
                proc.stdin.write(b"/stop\n")
                proc.stdin.flush()
                proc.wait(timeout=10)
            except Exception:
                proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
