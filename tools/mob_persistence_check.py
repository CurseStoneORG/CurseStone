#!/usr/bin/env python3
"""Mob persistence check for a local CurseStone server (Linux).

Proves that mobs are chunk state — stored INSIDE the world's region
files (.mca, each chunk's Entities NBT list) — with FOUR server boots
over the SAME runtime directory and one fixture Lua plugin
(console-driven, no game client needed):

  boot 1  a hand-written legacy mobs.dat (three sheep, one hurt to 5/8)
          is migrated into the region files: the file is retired, the
          .mca carries the entity NBT, and the world comes up with the
          3 bodies. /save + stop.
  boot 2  the 3 restored bodies come back from the chunks; a 4th sheep
          is spawned 40 blocks south (its own chunk, 0,2) and one
          healthy home-row sheep is hurt to 5/8; /save + stop; the
          region carries each chunk's own entity list separately.
  boot 3  only the home chunk's 3 bodies load at first (lazy per-chunk
          restore); the far sheep appears the moment its chunk loads.
          All 4 are at their saved positions with the right healths
          (both wounds survived). mobclear + /save + stop; neither
          chunk carries entities any more.
  boot 4  the cleared state persisted: zero mobs come back.

Usage:
    python3 tools/mob_persistence_check.py [--server /path/to/CurseStone]
        [--host 127.0.0.1] [--port 19159] [--keep] [--verbose]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import Client as PingClient  # noqa: E402

SHEEP_MAX_HP = 8.0  # The era registry value for the sheep.
SHEEP_ID = 13       # The sheep's protocol-84 network id (entity registry).

FIXTURE_PLUGIN_YML = """name: MobPersistenceFixture
main: src/main.lua
api: 1.0.3
version: 1.0.0
author: Dr1xyDev
"""

FIXTURE_MAIN_LUA = """-- MobPersistenceFixture: the console surface the persistence check drives.
-- All sheep live in chunk (0, 0) at ABSOLUTE coordinates (the legacy
-- rows the migration test writes), so the warm/list assertions do not
-- depend on where the generator puts the spawn.
plugin.onEnable = function()
    commands.register("mobspawn", {description = "spawn n sheep in the row (+ z offset)"},
        function(sender, args)
            local w = server.defaultWorld()
            local _, sy, _ = w:getSpawn()
            local n = tonumber(args[1] or "0") or 0
            local zoff = tonumber(args[2] or "0") or 0
            local ids = {}
            for i = 1, n do
                ids[#ids + 1] = w:spawnEntity("sheep", 1.5 + (i - 1),
                                              sy + 0.5, 1.0 + zoff)
            end
            commands.reply(sender, "FIXTURESPAWNED " .. table.concat(ids, ","))
        end)

    -- Forces a chunk column resident exactly like a player stream
    -- would, so the per-chunk mob restore has run before moblist. The
    -- z argument selects which row of the sheep chunk (0, 0) to warm —
    -- mobs restore per chunk, so a far chunk's bodies stay absent until
    -- its own chunk loads.
    commands.register("mobwarm", {description = "force-load a sheep chunk row"},
        function(sender, args)
            local w = server.defaultWorld()
            local _, sy, _ = w:getSpawn()
            local z = tonumber(args[1] or "1") or 1
            for i = 0, 3 do
                w:setBlock(1 + i, sy + 8, z, 0, 0)
            end
            commands.reply(sender, "FIXTUREWARMED " .. tostring(z))
        end)

    commands.register("moblist", {description = "sorted id:name:x,y,z list"},
        function(sender, args)
            local w = server.defaultWorld()
            local parts = {}
            for _, m in ipairs(w:getEntities()) do
                parts[#parts + 1] = string.format("%d:%s:%.2f,%.2f,%.2f",
                                                  m.id, m.name, m.x, m.y, m.z)
            end
            table.sort(parts)
            commands.reply(sender, "FIXTURELIST " .. tostring(#parts) ..
                           "|" .. table.concat(parts, "|"))
        end)

    commands.register("mobprobe", {description = "read one mob's health"},
        function(sender, args)
            local w = server.defaultWorld()
            local id = tonumber(args[1] or "0")
            commands.reply(sender, "FIXTUREHP " .. tostring(w:getEntityHealth(id)))
        end)

    commands.register("mobhurt", {description = "damage one mob by id"},
        function(sender, args)
            local w = server.defaultWorld()
            local id = tonumber(args[1] or "0")
            local amt = tonumber(args[2] or "1")
            local r = w:damageEntity(id, amt, "entity_attack")
            commands.reply(sender, "FIXTUREHURT " .. tostring(r) ..
                           " hp=" .. tostring(w:getEntityHealth(id)))
        end)

    commands.register("mobclear", {description = "remove every mob"},
        function(sender, args)
            local w = server.defaultWorld()
            local n = 0
            for _, m in ipairs(w:getEntities()) do
                if w:removeEntity(m.id) then
                    n = n + 1
                end
            end
            commands.reply(sender, "FIXTURECLEARED " .. tostring(n))
        end)
    return true
end

return plugin
"""


def wait_pred(pred, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.05)
    return pred()


def log_line(path, pattern, since_pos):
    """Scans the server log for a regex from `since_pos`; returns
    (match, new_pos)."""
    with open(path, "r", errors="replace") as f:
        f.seek(since_pos)
        data = f.read()
        pos = since_pos + len(data.encode("utf-8", "replace"))
    m = re.search(pattern, data)
    return (m, pos) if m else (None, pos)


# ----------------------------------------------------------------------
# Region (.mca) entity reader — proves the mobs really live inside the
# world's region files, not beside them.
# ----------------------------------------------------------------------

def _nbt_read_payload(buf, off, tag_id):
    """Reads one NBT payload (big-endian) at `off`; returns (value, new_off)."""
    if tag_id == 1:  # Byte
        return struct.unpack_from(">b", buf, off)[0], off + 1
    if tag_id == 2:  # Short
        return struct.unpack_from(">h", buf, off)[0], off + 2
    if tag_id == 3:  # Int
        return struct.unpack_from(">i", buf, off)[0], off + 4
    if tag_id == 4:  # Long
        return struct.unpack_from(">q", buf, off)[0], off + 8
    if tag_id == 5:  # Float
        return struct.unpack_from(">f", buf, off)[0], off + 4
    if tag_id == 6:  # Double
        return struct.unpack_from(">d", buf, off)[0], off + 8
    if tag_id == 7:  # ByteArray
        n = struct.unpack_from(">i", buf, off)[0]
        off += 4
        return bytes(buf[off:off + n]), off + n
    if tag_id == 8:  # String
        n = struct.unpack_from(">H", buf, off)[0]
        off += 2
        return buf[off:off + n].decode("utf-8", "replace"), off + n
    if tag_id == 9:  # List
        elem = buf[off]
        n = struct.unpack_from(">i", buf, off + 1)[0]
        off += 5
        out = []
        for _ in range(n):
            v, off = _nbt_read_payload(buf, off, elem)
            out.append(v)
        return out, off
    if tag_id == 10:  # Compound
        out = {}
        while True:
            child = buf[off]
            if child == 0:
                return out, off + 1
            nlen = struct.unpack_from(">H", buf, off + 1)[0]
            name = buf[off + 3:off + 3 + nlen].decode("utf-8", "replace")
            v, off = _nbt_read_payload(buf, off + 3 + nlen, child)
            out[name] = v
    if tag_id == 11:  # IntArray
        n = struct.unpack_from(">i", buf, off)[0]
        off += 4
        vals = struct.unpack_from(">%di" % n, buf, off) if n else ()
        return list(vals), off + 4 * n
    if tag_id == 12:  # LongArray
        n = struct.unpack_from(">i", buf, off)[0]
        off += 4
        vals = struct.unpack_from(">%dq" % n, buf, off) if n else ()
        return list(vals), off + 8 * n
    raise ValueError("unsupported NBT tag id %d" % tag_id)


def region_entities(region_path, cx, cz):
    """Returns the Entities list of chunk (cx, cz) from a region file:
    a list of dicts {id, pos(x,y,z), health, max_health}. An unreadable
    or absent chunk comes back as None (the caller decides if that is
    a failure)."""
    if not os.path.isfile(region_path):
        return None
    with open(region_path, "rb") as f:
        data = f.read()
    idx = (cx & 31) + (cz & 31) * 32
    raw = struct.unpack_from(">I", data, idx * 4)[0]
    sector_off = (raw >> 8) * 4096
    if sector_off == 0:
        return []
    length = struct.unpack_from(">I", data, sector_off)[0]
    compression = data[sector_off + 4]
    payload = data[sector_off + 5:sector_off + 4 + length]
    if compression == 2:
        payload = zlib.decompress(payload)
    elif compression == 1:
        import gzip
        payload = gzip.decompress(payload)
    else:
        raise ValueError("unknown region compression %d" % compression)
    # NBT file header: [tag id][u16 name length][name bytes]. The chunk
    # writer emits a root compound named "" — skip the header before the
    # compound payload, or the walk runs one End byte past the buffer.
    if len(payload) < 3 or payload[0] != 10:
        raise ValueError("chunk root is not a compound")
    name_len = struct.unpack_from(">H", payload, 1)[0]
    root, _ = _nbt_read_payload(payload, 3 + name_len, 10)
    level = root.get("Level", {})
    out = []
    for e in level.get("Entities", []):
        try:
            pos = e.get("Pos", [0.0, 0.0, 0.0])
            out.append({"id": e.get("id", ""),
                        "pos": (float(pos[0]), float(pos[1]), float(pos[2])),
                        "health": float(e.get("Health", 0.0)),
                        "max_health": float(e.get("MaxHealth", 0.0))})
        except (KeyError, IndexError, TypeError, ValueError):
            continue
    return out


def write_legacy_mobs_dat(path, rows):
    """Writes the pre-region mobs.dat format:
    `type x y z yaw pitch health max_health`."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        for r in rows:
            f.write("%d %.2f %.2f %.2f %.1f %.1f %.2f %.2f\n" %
                    (r["type"], r["x"], r["y"], r["z"], r["yaw"], r["pitch"],
                     r["health"], r["max_health"]))


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone mob persistence check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19159)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_mobpersist_")
    plugin_dir = os.path.join(runtime, "plugins", "MobPersistenceFixture", "src")
    os.makedirs(plugin_dir)
    with open(os.path.join(runtime, "plugins", "MobPersistenceFixture",
                           "plugin.yml"), "w") as f:
        f.write(FIXTURE_PLUGIN_YML)
    with open(os.path.join(plugin_dir, "main.lua"), "w") as f:
        f.write(FIXTURE_MAIN_LUA)
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=MobPersistenceCheck\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    # The legacy side file a pre-region build would leave behind: three
    # sheep in a row one block east of the spawn column, the first hurt
    # to 5/8. The spawn of a fresh flat world sits at x=z=0 (the
    # constructor only snaps the y down to terrain), so the row lands in
    # chunk (0, 0) and the migration has exactly one chunk to fold.
    legacy_path = os.path.join(runtime, "worlds", "world", "mobs.dat")
    write_legacy_mobs_dat(legacy_path, [
        {"type": SHEEP_ID, "x": 1.5, "y": 65.0, "z": 1.0, "yaw": 0.0,
         "pitch": 0.0, "health": 5.0, "max_health": SHEEP_MAX_HP},
        {"type": SHEEP_ID, "x": 2.5, "y": 65.0, "z": 1.0, "yaw": 0.0,
         "pitch": 0.0, "health": SHEEP_MAX_HP, "max_health": SHEEP_MAX_HP},
        {"type": SHEEP_ID, "x": 3.5, "y": 65.0, "z": 1.0, "yaw": 0.0,
         "pitch": 0.0, "health": SHEEP_MAX_HP, "max_health": SHEEP_MAX_HP},
    ])

    world_dir = os.path.join(runtime, "worlds", "world")
    region0 = os.path.join(world_dir, "region", "r.0.0.mca")
    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" +
                              extra_ld).lstrip(":")
    server_log = os.path.join(runtime, "server.log")
    failures = []
    checks = []

    def check(cond, label, detail=""):
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}" +
              (f" ({detail})" if detail and not cond else ""))
        checks.append(cond)
        if not cond:
            failures.append(label)

    def boot():
        log_fh = open(server_log, "a")
        proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                                stdin=subprocess.PIPE, stdout=log_fh,
                                stderr=subprocess.STDOUT)

        def console(cmd):
            try:
                proc.stdin.write((cmd + "\n").encode())
                proc.stdin.flush()
            except BrokenPipeError:
                pass

        def expect(pattern, seconds=8.0):
            pos = os.path.getsize(server_log)
            return expect_from(pattern, pos, seconds)

        def expect_from(pattern, pos, seconds=8.0):
            m = None
            deadline = time.time() + seconds
            while time.time() < deadline and m is None:
                time.sleep(0.2)
                m, pos = log_line(server_log, pattern, pos)
            return m

        # Ping gate: the world (and the fixture plugin) are up when the
        # server answers an unconnected ping.
        up = False
        deadline = time.time() + 25.0
        while time.time() < deadline and not up:
            if proc.poll() is not None:
                break
            c = PingClient(args.host, args.port, "PingProbe")
            ping = (bytes([0x01]) + struct.pack("<Q", int(time.time() * 1000)) +
                    b"\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd"
                    b"\x12\x34\x56\x78")
            c.send_raw(ping)
            up = c.expect("unconnected_ping", seconds=1.0)
            c.sock.close()
        if not up:
            proc.kill()
            log_fh.close()
            return None
        return proc, console, expect, expect_from, log_fh

    def stop(proc, log_fh):
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
            proc.wait(timeout=25)
        except Exception:
            proc.kill()
        log_fh.close()

    def region_entities_settled(region_path, cx, cz, attempts=6):
        """Reads a region file only while no server is writing it (the
        caller verifies after the boot's stop). A retry loop keeps a
        future concurrent writer from turning a mid-rewrite read into a
        parse error."""
        last = None
        for _ in range(attempts):
            try:
                return region_entities(region_path, cx, cz)
            except Exception as exc:  # noqa: BLE001 - racy read, retry
                last = exc
                time.sleep(0.5)
        raise last

    def list_mobs(console, expect, warm_z=1):
        console(f"mobwarm {warm_z}")
        expect(r"FIXTUREWARMED")
        console("moblist")
        m = expect(r"FIXTURELIST (\d+)\|(.*)", seconds=10.0)
        if m is None:
            return None, None
        mobs = []
        if m.group(2):
            for part in m.group(2).split("|"):
                eid, name, coords = part.split(":")
                x, y, z = (float(v) for v in coords.split(","))
                mobs.append({"eid": int(eid), "name": name,
                             "pos": (x, y, z)})
        mobs.sort(key=lambda mob: mob["eid"])
        return int(m.group(1)), mobs

    def probe_healths(console, expect, mobs):
        healths = []
        for mob in mobs:
            console(f"mobprobe {mob['eid']}")
            mh = expect(r"FIXTUREHP ([\d.-]+)", seconds=6.0)
            healths.append(float(mh.group(1)) if mh else -1.0)
            mob["health"] = healths[-1]
        return healths

    try:
        # ============ boot 1: legacy mobs.dat migrates into the .mca ====
        boot1 = boot()
        if boot1 is None:
            print("FAIL: boot 1 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot1
        try:
            m = expect_from(r"migrated 3 mob\(s\) from the legacy mobs\.dat",
                            0, seconds=15.0)
            check(m is not None, "boot 1: the legacy mobs.dat was migrated",
                  "no migration log line")

            count, mobs = list_mobs(console, expect)
            check(count == 3, "boot 1: the 3 migrated bodies are live",
                  f"count={count}")
            if count != 3:
                return 1
            pos_ok = all(abs(mob["pos"][0] - want) < 0.02 and
                         abs(mob["pos"][1] - 65.0) < 0.02 and
                         abs(mob["pos"][2] - 1.0) < 0.02
                         for mob, want in zip(mobs, [1.5, 2.5, 3.5]))
            check(pos_ok, "boot 1: migrated bodies sit at their legacy rows",
                  f"positions={[mob['pos'] for mob in mobs]}")
            healths = probe_healths(console, expect, mobs)
            check(sorted(round(h, 2) for h in healths) ==
                  [5.0, SHEEP_MAX_HP, SHEEP_MAX_HP],
                  "boot 1: the legacy hurt sheep kept its 5 hp",
                  f"healths={healths}")

            console("save")
            m = expect(r"Saved world", seconds=8.0)
            check(m is not None, "boot 1: /save ran (the /save save point)",
                  "no 'Saved world' reply")

            console("stop")
            try:
                proc.wait(timeout=25)
            except Exception:
                proc.kill()
                check(False, "boot 1: stopped cleanly", "stop timed out")
        finally:
            try:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
            except Exception:
                pass
            log_fh.close()

        # The region file is settled now that the boot's writer is gone:
        # verify the migrated entities INSIDE the .mca.
        check(not os.path.exists(legacy_path),
              "boot 1: the legacy mobs.dat is retired after migration")
        ents = region_entities_settled(region0, 0, 0)
        ok = ents is not None and len(ents) == 3 and all(
            e["id"] == "sheep" for e in ents)
        check(ok, "boot 1: the region file carries the 3 sheep entities",
              f"entities={ents}")
        if ok:
            hp_ok = sorted(round(e["health"], 2) for e in ents) == \
                [5.0, SHEEP_MAX_HP, SHEEP_MAX_HP]
            pos_ok = all(abs(e["pos"][0] - want) < 0.02 and
                         abs(e["pos"][2] - 1.0) < 0.02
                         for e, want in zip(sorted(ents,
                                                   key=lambda e: e["pos"][0]),
                                            [1.5, 2.5, 3.5]))
            check(hp_ok, "boot 1: the entity NBT inside the .mca has the "
                         "hurt sheep's health",
                  f"healths={[e['health'] for e in ents]}")
            check(pos_ok, "boot 1: the entity NBT inside the .mca has the "
                          "saved positions",
                  f"positions={[e['pos'] for e in ents]}")

        # ============ boot 2: chunk restore + a fresh body + a wound ====
        boot2 = boot()
        if boot2 is None:
            print("FAIL: boot 2 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot2
        try:
            count, mobs = list_mobs(console, expect)
            check(count == 3, "boot 2: the world comes back with its 3 mobs "
                              "(restored per chunk, no mobs.dat)",
                  f"count={count}")
            if count != 3:
                return 1
            boot2_positions = sorted(mob["pos"][0] for mob in mobs)
            check(all(abs(a - b) < 0.02 for a, b in
                      zip(boot2_positions, [1.5, 2.5, 3.5])),
                  "boot 2: every mob is at its saved position",
                  f"positions={boot2_positions}")

            console("mobspawn 1 40")
            m = expect(r"FIXTURESPAWNED ([\d,]+)")
            check(m is not None, "boot 2: a 4th sheep spawned 40 blocks "
                                  "south (chunk 0,2)",
                  f"reply={m.group(1) if m else 'none'}")
            if m is None:
                return 1

            console("moblist")
            m = expect(r"FIXTURELIST (\d+)\|(.*)", seconds=10.0)
            count = int(m.group(1)) if m else -1
            check(count == 4, "boot 2: moblist now sees 4 bodies "
                              "(spawnMob forced the far chunk resident)",
                  f"count={count}")
            mobs = []
            if m and m.group(2):
                for part in m.group(2).split("|"):
                    eid, name, coords = part.split(":")
                    x, y, z = (float(v) for v in coords.split(","))
                    mobs.append({"eid": int(eid), "name": name,
                                 "pos": (x, y, z)})
            mobs.sort(key=lambda mob: mob["eid"])
            healths = probe_healths(console, expect, mobs)
            # Hurt one HEALTHY sheep of the HOME row (8 hp): both the
            # legacy wound and the fresh one must survive the restart.
            row_healthy = [mob for mob in mobs
                           if abs(mob.get("health", -1) - SHEEP_MAX_HP) < 0.01
                           and abs(mob["pos"][2] - 1.0) < 0.02]
            check(len(row_healthy) >= 1,
                  "boot 2: found a healthy home-row sheep to wound",
                  f"healths={healths}")
            if row_healthy:
                console(f"mobhurt {row_healthy[0]['eid']} 3")
                m = expect(r"FIXTUREHURT (\w+) hp=([\d.-]+)")
                check(m is not None and m.group(1) == "hurt" and
                      abs(float(m.group(2)) - (SHEEP_MAX_HP - 3.0)) < 0.01,
                      "boot 2: the healthy sheep hurt to 5/8",
                      f"reply={m.group(0) if m else 'none'}")

            console("save")
            m = expect(r"Saved world", seconds=8.0)
            check(m is not None, "boot 2: /save ran", "no 'Saved world' reply")
            console("stop")
            try:
                proc.wait(timeout=25)
            except Exception:
                proc.kill()
                check(False, "boot 2: stopped cleanly", "stop timed out")
        finally:
            try:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
            except Exception:
                pass
            log_fh.close()

        ents = region_entities_settled(region0, 0, 0)
        ok = ents is not None and len(ents) == 3
        check(ok, "boot 2: the home chunk of the region file carries 3 "
                  "entities", f"entities={ents}")
        far = region_entities_settled(region0, 0, 2)
        far_ok = far is not None and len(far) == 1 and \
            abs(far[0]["pos"][0] - 1.5) < 0.02 and \
            abs(far[0]["pos"][2] - 41.0) < 0.02
        check(far_ok, "boot 2: the far chunk carries its own sheep at "
                      "(1.5, y, 41) — mob state rides its own chunk",
              f"entities={far}")

        # ============ boot 3: both wounds persisted, then clear =========
        boot3 = boot()
        if boot3 is None:
            print("FAIL: boot 3 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot3
        try:
            count, mobs = list_mobs(console, expect)
            check(count == 3, "boot 3: only the home chunk's 3 mobs loaded "
                              "(lazy per-chunk restore — the far body waits "
                              "for its chunk)",
                  f"count={count}")
            if count != 3:
                return 1
            xs = sorted(mob["pos"][0] for mob in mobs)
            check(all(abs(a - b) < 0.02 for a, b in zip(xs, [1.5, 2.5, 3.5])),
                  "boot 3: the home-row mobs are at their saved positions",
                  f"positions={[mob['pos'] for mob in mobs]}")

            count, mobs = list_mobs(console, expect, warm_z=41)
            check(count == 4, "boot 3: the far sheep appeared the moment "
                              "its chunk loaded",
                  f"count={count}")
            if count != 4:
                return 1
            xs = sorted(mob["pos"][0] for mob in mobs)
            south = [mob for mob in mobs if abs(mob["pos"][2] - 41.0) < 0.02]
            check(all(abs(a - b) < 0.02 for a, b in zip(xs, [1.5, 1.5, 2.5, 3.5]))
                  and len(south) == 1,
                  "boot 3: every mob is at its saved position "
                  "(row + the boot-2 spawn 40 blocks south)",
                  f"positions={[mob['pos'] for mob in mobs]}")
            healths = probe_healths(console, expect, mobs)
            check(sorted(round(h, 2) for h in healths) ==
                  [5.0, 5.0, SHEEP_MAX_HP, SHEEP_MAX_HP],
                  "boot 3: both 5-hp wounds survived (legacy + boot 2)",
                  f"healths={healths}")

            console("mobclear")
            m = expect(r"FIXTURECLEARED (\d+)")
            check(m is not None and m.group(1) == "4",
                  "boot 3: mobclear removed the 4 restored bodies",
                  f"reply={m.group(1) if m else 'none'}")

            console("save")
            expect(r"Saved world", seconds=8.0)
        finally:
            stop(proc, log_fh)

        ents = region_entities_settled(region0, 0, 0)
        far = region_entities_settled(region0, 0, 2)
        check(ents == [] and far == [],
              "boot 3: the cleared world left no entities in either chunk",
              f"home={ents} far={far}")

        # ============ boot 4: the cleared state persists ================
        boot4 = boot()
        if boot4 is None:
            print("FAIL: boot 4 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot4
        try:
            count, _mobs = list_mobs(console, expect)
            check(count == 0,
                  "boot 4: no zombie mobs came back after the cleared restart",
                  f"count={count}")
        finally:
            stop(proc, log_fh)

        if failures:
            print(f"  {len(failures)} check(s) FAILED")
            return 1
        print("  all mob-persistence checks PASSED")
        if not args.keep:
            shutil.rmtree(runtime, ignore_errors=True)
        return 0
    except Exception as exc:
        print(f"FAIL: unexpected exception: {exc}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
