#!/usr/bin/env python3
"""Mob persistence check for a local CurseStone server (Linux).

Proves the per-world mobs.dat round trip with THREE server boots over
the SAME runtime directory and one fixture Lua plugin (console-driven,
no game client needed):

  boot 1  spawn 3 sheep in a row at the spawn, hurt one of them, /save,
          stop. mobs.dat must exist with the 3 rows and the exact
          (rounded) positions + health.
  boot 2  the world comes back with 3 mobs: same positions, the hurt
          sheep's health survives, the eids are fresh (runtime ids are
          never restored). Then clear every mob, /save, stop.
  boot 3  the cleared state persisted: 0 mobs, mobs.dat empty.

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

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import Client as PingClient  # noqa: E402

SHEEP_MAX_HP = 8.0  # The era registry value for the sheep.

FIXTURE_PLUGIN_YML = """name: MobPersistenceFixture
main: src/main.lua
api: 1.0.3
version: 1.0.0
author: Dr1xyDev
"""

FIXTURE_MAIN_LUA = """-- MobPersistenceFixture: the console surface the persistence check drives.
plugin.onEnable = function()
    commands.register("mobspawn", {description = "spawn n sheep in a row at spawn"},
        function(sender, args)
            local w = server.defaultWorld()
            local sx, sy, sz = w:getSpawn()
            local n = tonumber(args[1] or "0") or 0
            local ids = {}
            for i = 1, n do
                ids[#ids + 1] = w:spawnEntity("sheep", sx + 1.5 + (i - 1),
                                              sy + 0.5, sz + 1.0)
            end
            commands.reply(sender, "FIXTURESPAWNED " .. table.concat(ids, ","))
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


def parse_mobs_file(path):
    """Reads mobs.dat into (type, x, y, z, health) tuples; positions are
    rounded to the file's two decimals. Tolerates a comma decimal point
    from exotic locales."""
    rows = []
    if not os.path.isfile(path):
        return rows
    with open(path, "r", errors="replace") as f:
        for raw in f:
            fields = raw.replace(",", ".").split()
            if len(fields) != 8:
                continue
            try:
                rows.append((int(fields[0]),
                             float(fields[1]), float(fields[2]), float(fields[3]),
                             float(fields[6])))
            except ValueError:
                continue
    return rows


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

    mobs_path = os.path.join(runtime, "worlds", "world", "mobs.dat")
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

    try:
        # ================= boot 1: spawn, hurt, save, stop =================
        boot1 = boot()
        if boot1 is None:
            print("FAIL: boot 1 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot1
        try:
            console("mobspawn 3")
            m = expect(r"FIXTURESPAWNED ([\d,]+)")
            ok = m is not None and len(m.group(1).split(",")) == 3
            check(ok, "boot 1: fixture spawned 3 sheep",
                  f"reply={m.group(1) if m else 'none'}")
            if not ok:
                return 1
            first_eid = m.group(1).split(",")[0]
            console(f"mobhurt {first_eid} 3")
            m = expect(r"FIXTUREHURT (\w+) hp=([\d.-]+)")
            check(m is not None and m.group(1) == "hurt" and
                  abs(float(m.group(2)) - (SHEEP_MAX_HP - 3.0)) < 0.01,
                  "boot 1: one sheep hurt to 5/8",
                  f"reply={m.group(0) if m else 'none'}")

            console("moblist")
            m = expect(r"FIXTURELIST (\d+)\|(.*)")
            check(m is not None and m.group(1) == "3",
                  "boot 1: moblist sees the 3 bodies",
                  f"reply={m.group(0) if m else 'none'}")

            console("save")
            m = expect(r"Saved world", seconds=8.0)
            check(m is not None, "boot 1: /save ran (the /save save point)",
                  "no 'Saved world' reply")

            rows = parse_mobs_file(mobs_path)
            check(len(rows) == 3, "boot 1: mobs.dat holds 3 rows after /save",
                  f"rows={rows}")
            # The saved rows are the spawned sheep at the spawn row
            # (sx+1.5..sx+3.5, feet y+0.5) with exactly one at 5 hp.
            saved_healths = sorted(round(r[4], 2) for r in rows)
            check(saved_healths == [5.0, SHEEP_MAX_HP, SHEEP_MAX_HP],
                  "boot 1: mobs.dat carries the hurt sheep's health",
                  f"healths={saved_healths}")
            saved_positions = sorted((round(r[1], 2), round(r[2], 2), round(r[3], 2))
                                     for r in rows)

            console("stop")
            try:
                proc.wait(timeout=25)
            except Exception:
                proc.kill()
                check(False, "boot 1: stopped cleanly", "stop timed out")
        finally:
            log_fh.close()

        # ================= boot 2: the world restores =================
        boot2 = boot()
        if boot2 is None:
            print("FAIL: boot 2 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot2
        try:
            console("moblist")
            m = expect(r"FIXTURELIST (\d+)\|(.*)", seconds=10.0)
            ok = m is not None and m.group(1) == "3"
            check(ok, "boot 2: all 3 mobs came back after the restart",
                  f"reply={m.group(0) if m else 'none'}")
            if not ok:
                return 1

            restored_positions = sorted(
                (float(a), float(b), float(c)) for a, b, c in
                (p.split(":")[2].split(",") for p in m.group(2).split("|")))
            pos_ok = len(restored_positions) == 3 and all(
                abs(a[0] - b[0]) < 0.02 and abs(a[1] - b[1]) < 0.02 and
                abs(a[2] - b[2]) < 0.02
                for a, b in zip(restored_positions, saved_positions))
            check(pos_ok, "boot 2: every mob is at its saved position",
                  f"saved={saved_positions} restored={restored_positions}")

            # The hurt sheep's 5 hp survives too: the mob table carries no
            # health, so read it per entity through the health probe.
            healths = []
            for part in m.group(2).split("|"):
                eid = part.split(":")[0]
                console(f"mobprobe {eid}")
                mh = expect(r"FIXTUREHP ([\d.-]+)", seconds=6.0)
                if mh:
                    healths.append(float(mh.group(1)))
            check(any(abs(h - 5.0) < 0.01 for h in healths) and
                  sum(abs(h - SHEEP_MAX_HP) < 0.01 for h in healths) == 2,
                  "boot 2: the hurt sheep kept its 5 hp (health persists)",
                  f"healths={healths}")

            console("mobclear")
            m = expect(r"FIXTURECLEARED (\d+)")
            check(m is not None and m.group(1) == "3",
                  "boot 2: mobclear removed the 3 restored bodies",
                  f"reply={m.group(1) if m else 'none'}")

            console("save")
            expect(r"Saved world", seconds=8.0)
            rows = parse_mobs_file(mobs_path)
            check(rows == [], "boot 2: the cleared world saved an empty mobs.dat",
                  f"rows={rows}")
        finally:
            stop(proc, log_fh)

        # ================= boot 3: the cleared state persists =================
        boot3 = boot()
        if boot3 is None:
            print("FAIL: boot 3 did not answer the unconnected ping")
            return 1
        proc, console, expect, expect_from, log_fh = boot3
        try:
            console("moblist")
            m = expect(r"FIXTURELIST (\d+)\|*", seconds=10.0)
            check(m is not None and m.group(1) == "0",
                  "boot 3: no zombie mobs came back after the cleared restart",
                  f"reply={m.group(0) if m else 'none'}")
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
