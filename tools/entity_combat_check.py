#!/usr/bin/env python3
"""End-to-end mob combat check for a local CurseStone server (Linux).

Drives the real RakNet + protocol-84 path with ONE simulated client and a
fixture Lua plugin to prove mobs are hittable killable bodies with a
plugin-facing damage/cancel surface:

  1. the fixture spawns a sheep at the world spawn (`mobhere`); the client
     receives the AddEntityPacket broadcast of the spawn
  2. a player punch (InteractPacket ACTION_LEFT_CLICK) lands: the client
     sees the mob's HURT_ANIMATION and the era knockback motion packet
  3. the server-side health really dropped (`mobhp`: 7 of 8 after one punch)
  4. the Lua damageEntity call runs the same pipeline (`mobdmg` -> "hurt")
  5. a plugin listener CAN cancel the damage (`godmob on`): the punch
     produces no hurt flash at all
  6. lethal punches run the death sequence: DEATH_ANIMATION, then the
     RemoveEntityPacket despawn inside the death window
  7. the vanilla drop table fires: the sheep drops its guaranteed wool
  8. the Lua event surface saw everything (`mobstats`: hits, cancels,
     deaths, victim name, cause)

Usage:
    python3 tools/entity_combat_check.py [--server /path/to/CurseStone]
        [--host 127.0.0.1] [--port 19157] [--keep] [--verbose]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client as GameClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402
from linux_login_check import Client as PingClient  # noqa: E402

# Protocol-84 packet ids.
TEXT_PACKET = 0x07
ADD_ENTITY = 0x0B
REMOVE_ENTITY = 0x0C
ADD_ITEM_ENTITY = 0x0D
ENTITY_EVENT = 0x18
INTERACT = 0x1E
SET_ENTITY_MOTION = 0x23

HURT_ANIMATION = 2
DEATH_ANIMATION = 3
ACTION_LEFT_CLICK = 2
EYE = 1.62

FIXTURE_PLUGIN_YML = """name: EntityCombatFixture
main: src/main.lua
api: 1.0.3
version: 1.0.0
author: Dr1xyDev
"""

FIXTURE_MAIN_LUA = """-- EntityCombatFixture: drives the mob-combat check's Lua surface.
local mob = {
    hits = 0, plain = 0, cancels = 0, deaths = 0,
    lastName = "", lastCause = "", god = false,
}

plugin.onEnable = function()
    events.on("EntityDamageByEntity", function(e)
        local nm = e.entityName or ""
        if nm == "" then
            return 0  -- player victims carry no entityName
        end
        mob.hits = mob.hits + 1
        mob.lastName = nm
        mob.lastCause = e.cause or ""
        if mob.god then
            mob.cancels = mob.cancels + 1
            e.cancel()
        end
    end)

    events.on("EntityDamage", function(e)
        if (e.entityName or "") == "" then
            return 0
        end
        mob.plain = mob.plain + 1
    end)

    events.on("EntityDeath", function(e)
        mob.deaths = mob.deaths + 1
        mob.lastName = e.entityName or ""
    end)

    commands.register("mobhere", {description = "spawn a sheep at spawn"},
        function(sender, args)
            local w = server.defaultWorld()
            local sx, sy, sz = w:getSpawn()
            local eid = w:spawnEntity("sheep", sx + 1.5, sy + 0.5, sz + 1.0)
            commands.reply(sender, "FIXTUREMOB " .. tostring(eid))
        end)

    commands.register("mobhp", {description = "mob health probe"},
        function(sender, args)
            local id = tonumber(args[1] or "0")
            local w = server.defaultWorld()
            commands.reply(sender, "FIXTUREHP hp=" ..
                tostring(w:getEntityHealth(id)) .. " max=" ..
                tostring(w:getEntityMaxHealth(id)))
        end)

    commands.register("mobdmg", {description = "Lua damageEntity probe"},
        function(sender, args)
            local id = tonumber(args[1] or "0")
            local amt = tonumber(args[2] or "1")
            local w = server.defaultWorld()
            local result = w:damageEntity(id, amt, "entity_attack")
            commands.reply(sender, "FIXTUREDMG result=" .. tostring(result) ..
                " hp=" .. tostring(w:getEntityHealth(id)))
        end)

    commands.register("godmob", {description = "toggle zombie-god mode"},
        function(sender, args)
            mob.god = (args[1] == "on")
            commands.reply(sender, "FIXTUREGOD " .. tostring(mob.god))
        end)

    commands.register("mobstats", {description = "event surface stats"},
        function(sender, args)
            commands.reply(sender, "FIXTURESTATS hits=" .. mob.hits ..
                " plain=" .. mob.plain .. " cancels=" .. mob.cancels ..
                " deaths=" .. mob.deaths .. " lastName=" .. mob.lastName ..
                " lastCause=" .. mob.lastCause)
        end)
    return true
end

return plugin
"""


class MobClient(GameClient):
    """Login client that records the mob-combat wire traffic."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name)
        self.verbose = verbose
        self.entity_events = []   # (eid, event)
        self.motions = []         # (eid, mx, my, mz)
        self.add_entities = []    # (eid, type)
        self.removed_eids = []    # eid
        self.item_drops = []      # (eid, itemId, count)

    def on_game_packet(self, p: bytes):
        """Records the mob-combat traffic; the base class handles the
        batch unwrap, stage bookkeeping and the RakNet keepalive pong."""
        super().on_game_packet(p)
        try:
            if p[0] == ENTITY_EVENT and len(p) >= 10:
                eid = struct.unpack(">q", p[1:9])[0]
                self.entity_events.append((eid, p[9]))
            elif p[0] == SET_ENTITY_MOTION and len(p) >= 21:
                eid = struct.unpack(">q", p[1:9])[0]
                mx, my, mz = struct.unpack(">fff", p[9:21])
                self.motions.append((eid, mx, my, mz))
            elif p[0] == ADD_ENTITY and len(p) >= 13:
                # AddEntityPacket: pid(1) + eid(8) + type(4) + ...
                eid = struct.unpack(">q", p[1:9])[0]
                etype = struct.unpack(">i", p[9:13])[0]
                self.add_entities.append((eid, etype))
            elif p[0] == REMOVE_ENTITY and len(p) >= 9:
                self.removed_eids.append(struct.unpack(">q", p[1:9])[0])
            elif p[0] == ADD_ITEM_ENTITY and len(p) >= 12:
                # ItemEntity: eid(8) + itemId(int16) + count(byte) + ...
                eid = struct.unpack(">q", p[1:9])[0]
                item_id = struct.unpack(">h", p[9:11])[0]
                count = p[11]
                self.item_drops.append((eid, item_id, count))
        except (struct.error, IndexError):
            pass

    def interact(self, action, target_eid):
        it = bytes([INTERACT, action])
        it += struct.pack(">q", target_eid)
        self.send_game(it)

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([TEXT_PACKET, 1]) +
                       struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() +
                       struct.pack(">H", len(raw)) + raw)


def wait_pred(pred, seconds, client=None):
    end = time.time() + seconds
    while time.time() < end:
        if client:
            client.drain(0.1)
        if pred():
            return True
        if not client:
            time.sleep(0.05)
    return pred()


def log_line(proc_log_path, pattern, since_pos):
    """Scans the server log for a regex from `since_pos`; returns
    (match, new_pos)."""
    with open(proc_log_path, "r", errors="replace") as f:
        f.seek(since_pos)
        data = f.read()
        pos = since_pos + len(data.encode("utf-8", "replace"))
    m = re.search(pattern, data)
    return (m, pos) if m else (None, pos)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone mob combat check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19157)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_mobcombat_")
    plugin_dir = os.path.join(runtime, "plugins", "EntityCombatFixture", "src")
    os.makedirs(plugin_dir)
    with open(os.path.join(runtime, "plugins", "EntityCombatFixture",
                           "plugin.yml"), "w") as f:
        f.write(FIXTURE_PLUGIN_YML)
    with open(os.path.join(plugin_dir, "main.lua"), "w") as f:
        f.write(FIXTURE_MAIN_LUA)
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=MobCombatCheck\nview-distance=4\ngamemode=0\n"
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

    def console(cmd):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []
    checks = []

    def check(cond, label, detail=""):
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}" +
              (f" ({detail})" if detail and not cond else ""))
        checks.append(cond)
        if not cond:
            failures.append(label)

    # Wait for the server to answer a ping before logging in.
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
        print("FAIL: server did not answer the unconnected ping")
        proc.kill()
        log_fh.close()
        return 1

    try:
        cli = MobClient(args.host, args.port, "MobHunter", verbose=args.verbose)
        ok = False
        try:
            # door_bed's Client returns True on success for both stages.
            ok = bool(cli.handshake()) and bool(cli.login())
        except Exception:
            ok = False
        check(ok, "client login")
        if not ok:
            return finish(proc, log_fh, failures, runtime, args.keep)
        # Spawn completion: the server only treats the body as a live
        # viewer/attacker after the client confirms the same-dimension
        # spawn (the same flow the other protocol-84 checks drive).
        cli.drain(4.0)
        cli.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        cli.drain(1.5)
        check(cli.eid != 0, "client spawned (StartGame eid received)")
        cli.drain(1.0)

        log_pos = 0
        # ---- 1. the fixture spawns the sheep ----
        console("mobhere")
        m = None
        deadline = time.time() + 6.0
        while time.time() < deadline and m is None:
            time.sleep(0.2)
            m, log_pos = log_line(server_log, r"FIXTUREMOB (\d+)", log_pos)
        check(m is not None, "fixture spawned the sheep (mobhere reply)")
        if not m:
            return finish(proc, log_fh, failures, runtime, args.keep)
        mob_eid = int(m.group(1))
        got_add = wait_pred(lambda: any(e[0] == mob_eid for e in cli.add_entities),
                            4.0, client=cli)
        check(got_add, "client received the sheep's AddEntityPacket",
              f"add_entities={cli.add_entities}")

        def hurt_events_for(eid):
            return [e for e in cli.entity_events if e[0] == eid and e[1] == HURT_ANIMATION]

        def motion_for(eid):
            return [mm for mm in cli.motions if mm[0] == eid]

        # ---- 2. the punch lands: hurt flash + knockback ----
        ev0, mo0 = len(hurt_events_for(mob_eid)), len(motion_for(mob_eid))
        cli.interact(ACTION_LEFT_CLICK, mob_eid)
        got_hurt = wait_pred(lambda: len(hurt_events_for(mob_eid)) > ev0, 4.0,
                             client=cli)
        check(got_hurt, "punch flashes the mob (HURT_ANIMATION)",
              f"events={cli.entity_events}")
        cli.drain(0.5)
        check(len(motion_for(mob_eid)) > mo0,
              "punch knocks the mob back (SetEntityMotion)",
              f"motions={cli.motions}")

        # ---- 3. server-side health really dropped ----
        console(f"mobhp {mob_eid}")
        m = None
        deadline = time.time() + 5.0
        while time.time() < deadline and m is None:
            time.sleep(0.2)
            m, log_pos = log_line(server_log, r"FIXTUREHP hp=([\d.]+) max=([\d.]+)",
                                  log_pos)
        check(m is not None and abs(float(m.group(1)) - 7.0) < 0.01 and
              abs(float(m.group(2)) - 8.0) < 0.01,
              "mob health probe reads 7/8 after one punch",
              f"reply={m.group(0) if m else 'none'}")

        # ---- 4. the Lua damageEntity pipeline ----
        console(f"mobdmg {mob_eid} 5")
        m = None
        deadline = time.time() + 5.0
        while time.time() < deadline and m is None:
            time.sleep(0.2)
            m, log_pos = log_line(server_log, r'FIXTUREDMG result=(\w+) hp=([\d.-]+)',
                                  log_pos)
        check(m is not None and m.group(1) == "hurt" and
              abs(float(m.group(2)) - 2.0) < 0.01,
              "damageEntity applies through the pipeline (hurt, hp 2)",
              f"reply={m.group(0) if m else 'none'}")

        # ---- 5. a plugin can cancel the damage ----
        console("godmob on")
        time.sleep(0.8)
        cli.drain(0.5)  # settle the damageEntity flash before the baseline
        ev1 = len(hurt_events_for(mob_eid))
        mo1 = len(motion_for(mob_eid))
        cli.interact(ACTION_LEFT_CLICK, mob_eid)
        cli.drain(1.2)
        check(len(hurt_events_for(mob_eid)) == ev1 and
              len(motion_for(mob_eid)) == mo1,
              "cancelled punch produces no hurt flash and no knockback",
              f"events={cli.entity_events} motions={cli.motions}")

        # ---- 6. lethal punches run the death sequence ----
        console("godmob off")
        time.sleep(0.8)
        def death_events_for(eid):
            return [e for e in cli.entity_events if e[0] == eid and e[1] == DEATH_ANIMATION]
        for _ in range(2):
            cli.interact(ACTION_LEFT_CLICK, mob_eid)
            time.sleep(0.4)
        got_death = wait_pred(lambda: len(death_events_for(mob_eid)) > 0, 4.0,
                              client=cli)
        check(got_death, "lethal blow plays the death animation (EntityEvent 3)",
              f"events={cli.entity_events}")
        got_rm = wait_pred(lambda: mob_eid in cli.removed_eids, 3.5, client=cli)
        check(got_rm, "the body despawns after the death window",
              f"removed={cli.removed_eids}")

        # ---- 7. the vanilla drop table ----
        got_drop = wait_pred(
            lambda: any(d[1] == 35 and d[2] == 1 for d in cli.item_drops),
            3.0, client=cli)
        check(got_drop, "the sheep dropped its guaranteed wool (id 35)",
              f"drops={cli.item_drops}")

        # ---- 8. the Lua event surface saw everything ----
        console("mobstats")
        m = None
        deadline = time.time() + 5.0
        while time.time() < deadline and m is None:
            time.sleep(0.2)
            m, log_pos = log_line(
                server_log,
                r"FIXTURESTATS hits=(\d+) plain=(\d+) cancels=(\d+) "
                r"deaths=(\d+) lastName=(\S+) lastCause=(\S+)", log_pos)
        ok = (m is not None and m.group(1) == "4" and m.group(2) == "1" and
              m.group(3) == "1" and m.group(4) == "1" and
              m.group(5) == "sheep" and m.group(6) == "entity_attack")
        check(ok, "Lua events saw 4 hits, 1 plain, 1 cancel, 1 sheep death",
              f"reply={m.group(0) if m else 'none'}")

        # ---- bonus: an unknown mob id is NOT a cancelled hit ----
        console("mobdmg 999999 1")
        m = None
        deadline = time.time() + 5.0
        while time.time() < deadline and m is None:
            time.sleep(0.2)
            m, log_pos = log_line(server_log, r"FIXTUREDMG result=(\w+)", log_pos)
        check(m is not None and m.group(1) == "nil",
              "damageEntity on an unknown id reads nil (not 'cancelled')",
              f"reply={m.group(0) if m else 'none'}")

        return finish(proc, log_fh, failures, runtime, args.keep)
    except Exception as exc:
        print(f"FAIL: unexpected exception: {exc}")
        return finish(proc, log_fh, failures or ["exception"], runtime, args.keep)


def finish(proc, log_fh, failures, runtime, keep):
    try:
        proc.stdin.write(b"stop\n")
        proc.stdin.flush()
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    log_fh.close()
    if failures:
        print(f"  {len(failures)} check(s) FAILED")
        return 1
    print("  all mob-combat checks PASSED")
    if not keep:
        import shutil
        shutil.rmtree(runtime, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
